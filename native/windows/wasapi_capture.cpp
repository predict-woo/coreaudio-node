#include "wasapi_capture.h"
#include <combaseapi.h>
#include <avrt.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

// Helper to convert narrow string to wide string
static std::wstring Utf8ToWide(const char* str) {
    if (!str) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &result[0], len);
    return result;
}

// Helper to convert wide string to narrow string
static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

// ============================================================================
// ActivationCompletionHandler Implementation
// ============================================================================

ActivationCompletionHandler::ActivationCompletionHandler()
    : refCount_(1), completed_(false), operation_(nullptr) {
    completionEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

ActivationCompletionHandler::~ActivationCompletionHandler() {
    if (completionEvent_) {
        CloseHandle(completionEvent_);
    }
    if (operation_) {
        operation_->Release();
    }
}

ULONG STDMETHODCALLTYPE ActivationCompletionHandler::AddRef() {
    return InterlockedIncrement(&refCount_);
}

ULONG STDMETHODCALLTYPE ActivationCompletionHandler::Release() {
    ULONG ref = InterlockedDecrement(&refCount_);
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE ActivationCompletionHandler::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
        *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE ActivationCompletionHandler::ActivateCompleted(
    IActivateAudioInterfaceAsyncOperation* op) {
    operation_ = op;
    op->AddRef();
    completed_ = true;
    SetEvent(completionEvent_);
    return S_OK;
}

HRESULT ActivationCompletionHandler::Wait(DWORD timeout) {
    WaitForSingleObject(completionEvent_, timeout);
    return completed_ ? S_OK : E_FAIL;
}

// ============================================================================
// WasapiCapture Implementation
// ============================================================================

WasapiCapture::WasapiCapture(
    AudioDataCallback dataCallback,
    AudioEventCallback eventCallback,
    AudioMetadataCallback metadataCallback,
    void* userContext
) : dataCallback_(dataCallback),
    eventCallback_(eventCallback),
    metadataCallback_(metadataCallback),
    userContext_(userContext),
    audioClient_(nullptr),
    captureClient_(nullptr),
    mixFormat_(nullptr),
    running_(false),
    stopEvent_(nullptr),
    targetSampleRate_(0),
    chunkDurationMs_(200),
    isMono_(true),
    gain_(1.0),
    emitSilence_(true),
    samplesPerChunk_(0),
    resampleRatio_(1.0) {

    stopEvent_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

WasapiCapture::~WasapiCapture() {
    Stop();

    if (stopEvent_) {
        CloseHandle(stopEvent_);
    }
    if (mixFormat_) {
        CoTaskMemFree(mixFormat_);
    }
    if (captureClient_) {
        captureClient_->Release();
    }
    if (audioClient_) {
        audioClient_->Release();
    }
}

HRESULT WasapiCapture::InitializeSystemLoopback() {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    HRESULT hr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) return hr;

    // Get default render device for loopback
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();
    if (FAILED(hr)) return hr;

    // Activate audio client
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient_);
    device->Release();
    if (FAILED(hr)) return hr;

    // Get mix format
    hr = audioClient_->GetMixFormat(&mixFormat_);
    if (FAILED(hr)) return hr;

    // Initialize with loopback flag
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        10000000,  // 1 second buffer (100ns units)
        0,
        mixFormat_,
        nullptr
    );
    if (FAILED(hr)) return hr;

    // Get capture client
    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient_);

    return hr;
}

HRESULT WasapiCapture::InitializeProcessLoopback(DWORD targetPid, PROCESS_LOOPBACK_MODE mode) {
    // Set up process-specific loopback parameters
    AUDIOCLIENT_ACTIVATION_PARAMS activationParams = {};
    activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activationParams.ProcessLoopbackParams.TargetProcessId = targetPid;
    activationParams.ProcessLoopbackParams.ProcessLoopbackMode = mode;

    PROPVARIANT activateParams = {};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(activationParams);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&activationParams);

    // Create completion handler
    auto* completionHandler = new ActivationCompletionHandler();
    IActivateAudioInterfaceAsyncOperation* asyncOp = nullptr;

    // Use VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK for process-specific capture
    HRESULT hr = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &activateParams,
        completionHandler,
        &asyncOp
    );

    if (FAILED(hr)) {
        completionHandler->Release();
        return hr;
    }

    // Wait for activation to complete
    completionHandler->Wait(5000);  // 5 second timeout

    // Get the result
    HRESULT activateResult = S_OK;
    IUnknown* activatedInterface = nullptr;
    
    auto* op = completionHandler->GetOperation();
    if (!op) {
        completionHandler->Release();
        if (asyncOp) asyncOp->Release();
        return E_FAIL;
    }

    hr = op->GetActivateResult(&activateResult, &activatedInterface);
    completionHandler->Release();
    if (asyncOp) asyncOp->Release();

    if (FAILED(hr) || FAILED(activateResult)) {
        return FAILED(hr) ? hr : activateResult;
    }

    // Get IAudioClient from the activated interface
    hr = activatedInterface->QueryInterface(__uuidof(IAudioClient), (void**)&audioClient_);
    activatedInterface->Release();

    if (FAILED(hr)) {
        return hr;
    }

    // Get the mix format
    hr = audioClient_->GetMixFormat(&mixFormat_);
    if (FAILED(hr)) return hr;

    // Initialize the audio client
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,  // No additional flags needed for process loopback
        10000000,  // 1 second buffer
        0,
        mixFormat_,
        nullptr
    );
    if (FAILED(hr)) return hr;

    // Get capture client
    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient_);

    return hr;
}

HRESULT WasapiCapture::InitializeMicrophone(const wchar_t* deviceId) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    HRESULT hr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) return hr;

    if (deviceId && wcslen(deviceId) > 0) {
        // Get specific device
        hr = enumerator->GetDevice(deviceId, &device);
    } else {
        // Get default input device
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    }
    enumerator->Release();
    if (FAILED(hr)) return hr;

    // Activate audio client
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient_);
    device->Release();
    if (FAILED(hr)) return hr;

    // Get mix format
    hr = audioClient_->GetMixFormat(&mixFormat_);
    if (FAILED(hr)) return hr;

    // Initialize for capture
    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        10000000,  // 1 second buffer
        0,
        mixFormat_,
        nullptr
    );
    if (FAILED(hr)) return hr;

    // Get capture client
    hr = audioClient_->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient_);

    return hr;
}

HRESULT WasapiCapture::FinalizeInitialization() {
    if (!mixFormat_) return E_FAIL;

    // Determine output sample rate
    double outputSampleRate = targetSampleRate_ > 0 ? targetSampleRate_ : mixFormat_->nSamplesPerSec;
    resampleRatio_ = outputSampleRate / mixFormat_->nSamplesPerSec;

    // Calculate samples per chunk based on output sample rate
    samplesPerChunk_ = static_cast<size_t>((chunkDurationMs_ / 1000.0) * outputSampleRate);

    // Report metadata
    if (metadataCallback_) {
        metadataCallback_(
            outputSampleRate,
            isMono_ ? 1 : mixFormat_->nChannels,
            32,  // Always output 32-bit float
            true,
            "pcm_f32le",
            userContext_
        );
    }

    return S_OK;
}

int32_t WasapiCapture::StartSystemAudio(
    double sampleRate,
    double chunkDurationMs,
    bool mute,  // Ignored on Windows
    bool isMono,
    bool emitSilence,
    const int32_t* includeProcesses,
    int32_t includeCount,
    const int32_t* excludeProcesses,
    int32_t excludeCount
) {
    if (running_) return -2;

    targetSampleRate_ = sampleRate;
    chunkDurationMs_ = chunkDurationMs > 0 ? chunkDurationMs : 200;
    isMono_ = isMono;
    emitSilence_ = emitSilence;

    HRESULT hr;

    // Determine capture mode
    if (includeCount > 0 && includeProcesses != nullptr) {
        // Include mode: capture only from specified process
        // Note: WASAPI only supports one process at a time
        hr = InitializeProcessLoopback(
            static_cast<DWORD>(includeProcesses[0]),
            PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
        );
    } else if (excludeCount > 0 && excludeProcesses != nullptr) {
        // Exclude mode: capture everything except specified process
        hr = InitializeProcessLoopback(
            static_cast<DWORD>(excludeProcesses[0]),
            PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
        );
    } else {
        // System-wide loopback
        hr = InitializeSystemLoopback();
    }

    if (FAILED(hr)) {
        if (eventCallback_) {
            eventCallback_(2, "Failed to initialize audio capture", userContext_);
        }
        return -3;
    }

    hr = FinalizeInitialization();
    if (FAILED(hr)) {
        if (eventCallback_) {
            eventCallback_(2, "Failed to finalize audio initialization", userContext_);
        }
        return -4;
    }

    // Start the audio client
    hr = audioClient_->Start();
    if (FAILED(hr)) {
        if (eventCallback_) {
            eventCallback_(2, "Failed to start audio client", userContext_);
        }
        return -5;
    }

    running_ = true;
    ResetEvent(stopEvent_);

    // Emit start event
    if (eventCallback_) {
        eventCallback_(0, nullptr, userContext_);
    }

    // Start capture thread
    captureThread_ = std::thread(&WasapiCapture::CaptureThread, this);

    return 0;
}

int32_t WasapiCapture::StartMicrophone(
    double sampleRate,
    double chunkDurationMs,
    bool isMono,
    bool emitSilence,
    const char* deviceId,
    double gain
) {
    if (running_) return -2;

    targetSampleRate_ = sampleRate;
    chunkDurationMs_ = chunkDurationMs > 0 ? chunkDurationMs : 200;
    isMono_ = isMono;
    emitSilence_ = emitSilence;
    gain_ = gain;

    std::wstring wideDeviceId = deviceId ? Utf8ToWide(deviceId) : L"";

    HRESULT hr = InitializeMicrophone(wideDeviceId.empty() ? nullptr : wideDeviceId.c_str());

    if (FAILED(hr)) {
        if (eventCallback_) {
            eventCallback_(2, "Failed to initialize microphone capture", userContext_);
        }
        return -3;
    }

    hr = FinalizeInitialization();
    if (FAILED(hr)) {
        if (eventCallback_) {
            eventCallback_(2, "Failed to finalize audio initialization", userContext_);
        }
        return -4;
    }

    // Start the audio client
    hr = audioClient_->Start();
    if (FAILED(hr)) {
        if (eventCallback_) {
            eventCallback_(2, "Failed to start audio client", userContext_);
        }
        return -5;
    }

    running_ = true;
    ResetEvent(stopEvent_);

    // Emit start event
    if (eventCallback_) {
        eventCallback_(0, nullptr, userContext_);
    }

    // Start capture thread
    captureThread_ = std::thread(&WasapiCapture::CaptureThread, this);

    return 0;
}

int32_t WasapiCapture::Stop() {
    if (!running_) return 0;

    running_ = false;
    SetEvent(stopEvent_);

    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    if (audioClient_) {
        audioClient_->Stop();
    }

    // Emit stop event
    if (eventCallback_) {
        eventCallback_(1, nullptr, userContext_);
    }

    return 0;
}

void WasapiCapture::CaptureThread() {
    // Initialize COM for this worker thread (required for WASAPI)
    // Use MTA for worker threads as it's more suitable for background processing
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitializedByUs = (comHr == S_OK);
    // S_FALSE means already initialized, RPC_E_CHANGED_MODE means different mode
    // Either way, COM is usable
    
    // Increase thread priority for audio processing
    DWORD taskIndex = 0;
    HANDLE taskHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    UINT32 packetLength = 0;
    BYTE* data = nullptr;
    UINT32 numFramesAvailable = 0;
    DWORD flags = 0;

    // Initialize timing for silence generation
    lastDataTime_ = std::chrono::steady_clock::now();
    auto chunkDuration = std::chrono::milliseconds(static_cast<int64_t>(chunkDurationMs_));

    while (running_) {
        // Wait for audio data or stop event
        DWORD waitResult = WaitForSingleObject(stopEvent_, 10);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }

        bool receivedAudio = false;

        // Get available audio data
        HRESULT hr = captureClient_->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            if (eventCallback_) {
                eventCallback_(2, "Failed to get packet size", userContext_);
            }
            break;
        }

        while (packetLength > 0 && running_) {
            hr = captureClient_->GetBuffer(&data, &numFramesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data != nullptr) {
                ProcessAudioData(data, numFramesAvailable);
                receivedAudio = true;
            }

            hr = captureClient_->ReleaseBuffer(numFramesAvailable);
            if (FAILED(hr)) break;

            hr = captureClient_->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;
        }

        // Update last data time if we received audio
        if (receivedAudio) {
            lastDataTime_ = std::chrono::steady_clock::now();
        }

        // Generate silence if enabled and no audio received for too long
        if (emitSilence_ && !receivedAudio) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDataTime_);
            
            if (elapsed >= chunkDuration) {
                // Generate silent chunk
                size_t numChannels = isMono_ ? 1 : (mixFormat_ ? mixFormat_->nChannels : 2);
                size_t silentSamples = samplesPerChunk_ * numChannels;
                
                if (silentSamples > 0 && dataCallback_) {
                    std::vector<float> silentBuffer(silentSamples, 0.0f);
                    dataCallback_(
                        reinterpret_cast<const uint8_t*>(silentBuffer.data()),
                        static_cast<int32_t>(silentSamples * sizeof(float)),
                        userContext_
                    );
                }
                
                lastDataTime_ = now;
            }
        }
    }

    if (taskHandle) {
        AvRevertMmThreadCharacteristics(taskHandle);
    }
    
    // Uninitialize COM if we initialized it
    if (comInitializedByUs) {
        CoUninitialize();
    }
}

void WasapiCapture::ProcessAudioData(const BYTE* data, UINT32 numFrames) {
    if (!mixFormat_ || numFrames == 0) return;

    // Convert to float (assuming input is float - WASAPI typically provides float)
    const float* floatData = reinterpret_cast<const float*>(data);
    size_t numChannels = mixFormat_->nChannels;
    size_t totalSamples = numFrames * numChannels;

    std::vector<float> processedData;

    // Apply gain if capturing microphone
    if (gain_ != 1.0) {
        processedData.resize(totalSamples);
        for (size_t i = 0; i < totalSamples; i++) {
            processedData[i] = floatData[i] * static_cast<float>(gain_);
        }
        floatData = processedData.data();
    }

    // Convert to mono if needed
    std::vector<float> monoData;
    if (isMono_ && numChannels > 1) {
        ConvertToMono(floatData, numFrames, monoData);
        floatData = monoData.data();
        numChannels = 1;
        totalSamples = numFrames;
    }

    // Resample if needed
    std::vector<float> resampledData;
    size_t outputFrames = numFrames;
    if (resampleRatio_ != 1.0) {
        ResampleAudio(floatData, numFrames * numChannels, resampledData);
        floatData = resampledData.data();
        outputFrames = resampledData.size() / numChannels;
        totalSamples = resampledData.size();
    }

    // Add to chunk buffer
    size_t outputSamples = outputFrames * numChannels;
    for (size_t i = 0; i < outputSamples; i++) {
        chunkBuffer_.push_back(floatData[i]);
    }

    // Emit chunks when buffer is full
    size_t samplesNeeded = samplesPerChunk_ * (isMono_ ? 1 : numChannels);
    while (chunkBuffer_.size() >= samplesNeeded && dataCallback_) {
        // Emit chunk
        dataCallback_(
            reinterpret_cast<const uint8_t*>(chunkBuffer_.data()),
            static_cast<int32_t>(samplesNeeded * sizeof(float)),
            userContext_
        );

        // Remove emitted samples from buffer
        chunkBuffer_.erase(chunkBuffer_.begin(), chunkBuffer_.begin() + samplesNeeded);
    }
}

void WasapiCapture::ConvertToMono(const float* input, size_t frames, std::vector<float>& output) {
    size_t channels = mixFormat_->nChannels;
    output.resize(frames);

    for (size_t i = 0; i < frames; i++) {
        float sum = 0.0f;
        for (size_t c = 0; c < channels; c++) {
            sum += input[i * channels + c];
        }
        output[i] = sum / channels;
    }
}

void WasapiCapture::ResampleAudio(const float* input, size_t inputSamples, std::vector<float>& output) {
    size_t outputSamples = static_cast<size_t>(inputSamples * resampleRatio_);
    output.resize(outputSamples);

    // Simple linear interpolation resampling
    for (size_t i = 0; i < outputSamples; i++) {
        double srcIndex = i / resampleRatio_;
        size_t srcIndexInt = static_cast<size_t>(srcIndex);
        double frac = srcIndex - srcIndexInt;

        if (srcIndexInt + 1 < inputSamples) {
            output[i] = static_cast<float>(
                input[srcIndexInt] * (1.0 - frac) + input[srcIndexInt + 1] * frac
            );
        } else if (srcIndexInt < inputSamples) {
            output[i] = input[srcIndexInt];
        } else {
            output[i] = 0.0f;
        }
    }
}

// ============================================================================
// AudioDeviceEnumerator Implementation
// ============================================================================

std::vector<AudioDeviceInfo> AudioDeviceEnumerator::ListAllDevices() {
    std::vector<AudioDeviceInfo> devices;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) return devices;

    // Get input devices
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        collection->GetCount(&count);

        std::wstring defaultInput = GetDefaultInputDeviceId();

        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(collection->Item(i, &device))) {
                AudioDeviceInfo info = {};
                if (GetDeviceInfo(device, info, true)) {
                    // Check if default
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(device->GetId(&id))) {
                        info.isDefault = (defaultInput == id);
                        CoTaskMemFree(id);
                    }
                    devices.push_back(info);
                }
                device->Release();
            }
        }
        collection->Release();
    }

    // Get output devices
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        collection->GetCount(&count);

        std::wstring defaultOutput = GetDefaultOutputDeviceId();

        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(collection->Item(i, &device))) {
                AudioDeviceInfo info = {};
                if (GetDeviceInfo(device, info, false)) {
                    // Check if default
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(device->GetId(&id))) {
                        info.isDefault = (defaultOutput == id);
                        CoTaskMemFree(id);
                    }
                    devices.push_back(info);
                }
                device->Release();
            }
        }
        collection->Release();
    }

    enumerator->Release();
    return devices;
}

bool AudioDeviceEnumerator::GetDeviceInfo(IMMDevice* device, AudioDeviceInfo& info, bool isInput) {
    LPWSTR id = nullptr;
    if (SUCCEEDED(device->GetId(&id))) {
        std::string idStr = WideToUtf8(id);
        info.uid = _strdup(idStr.c_str());
        CoTaskMemFree(id);
    }

    IPropertyStore* props = nullptr;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
        std::wstring name = GetDeviceProperty(props, PKEY_Device_FriendlyName);
        info.name = _strdup(WideToUtf8(name).c_str());

        std::wstring manufacturer = GetDeviceProperty(props, PKEY_Device_DeviceDesc);
        info.manufacturer = _strdup(WideToUtf8(manufacturer).c_str());

        props->Release();
    }

    info.isInput = isInput;
    info.isOutput = !isInput;
    info.isDefault = false;

    // Get audio format info
    IAudioClient* client = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) {
        WAVEFORMATEX* format = nullptr;
        if (SUCCEEDED(client->GetMixFormat(&format))) {
            info.sampleRate = format->nSamplesPerSec;
            info.channelCount = format->nChannels;
            CoTaskMemFree(format);
        }
        client->Release();
    }

    return info.uid != nullptr && info.name != nullptr;
}

std::wstring AudioDeviceEnumerator::GetDeviceProperty(IPropertyStore* props, const PROPERTYKEY& key) {
    PROPVARIANT value;
    PropVariantInit(&value);

    if (SUCCEEDED(props->GetValue(key, &value))) {
        if (value.vt == VT_LPWSTR && value.pwszVal) {
            std::wstring result = value.pwszVal;
            PropVariantClear(&value);
            return result;
        }
        PropVariantClear(&value);
    }
    return L"";
}

std::wstring AudioDeviceEnumerator::GetDefaultInputDeviceId() {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    std::wstring result;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (SUCCEEDED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        if (SUCCEEDED(hr)) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(device->GetId(&id))) {
                result = id;
                CoTaskMemFree(id);
            }
            device->Release();
        }
        enumerator->Release();
    }
    return result;
}

std::wstring AudioDeviceEnumerator::GetDefaultOutputDeviceId() {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    std::wstring result;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (SUCCEEDED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (SUCCEEDED(hr)) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(device->GetId(&id))) {
                result = id;
                CoTaskMemFree(id);
            }
            device->Release();
        }
        enumerator->Release();
    }
    return result;
}

// ============================================================================
// AudioPermissions Implementation
// ============================================================================

void AudioPermissions::RequestSystemAudio(PermissionCallback callback, void* context) {
    // System audio loopback doesn't require permission on Windows
    if (callback) {
        callback(true, context);
    }
}

int32_t AudioPermissions::GetMicrophoneStatus() {
    // On Windows, microphone access is controlled through privacy settings
    // We can try to enumerate capture devices - if we can, we have access
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) return 0;  // unknown

    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        collection->GetCount(&count);
        collection->Release();
        enumerator->Release();
        return count > 0 ? 2 : 1;  // authorized if devices found, denied otherwise
    }

    enumerator->Release();
    return 0;  // unknown
}

void AudioPermissions::RequestMicrophone(PermissionCallback callback, void* context) {
    // Windows 10+ may show a permission prompt automatically when accessing the mic
    // For now, just check if we can access it
    int32_t status = GetMicrophoneStatus();
    if (callback) {
        callback(status == 2, context);
    }
}

bool AudioPermissions::OpenSystemSettings() {
    // Open Windows Sound Settings
    ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOW);
    return true;
}
