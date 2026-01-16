#include "audio_bridge.h"
#include "wasapi_capture.h"
#include <combaseapi.h>
#include <cstring>

// Thread-local COM initialization tracking
static thread_local bool comInitialized = false;

static void EnsureComInitialized() {
    if (!comInitialized) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == S_FALSE) {  // S_FALSE means already initialized
            comInitialized = true;
        }
    }
}

// ============================================================================
// Audio Capture Session Management
// ============================================================================

extern "C" {

AudioRecorderHandle audio_create(
    AudioDataCallback dataCallback,
    AudioEventCallback eventCallback,
    AudioMetadataCallback metadataCallback,
    void* userContext
) {
    EnsureComInitialized();
    
    auto* capture = new WasapiCapture(
        dataCallback,
        eventCallback,
        metadataCallback,
        userContext
    );
    
    return static_cast<AudioRecorderHandle>(capture);
}

int32_t audio_start_system_audio(
    AudioRecorderHandle handle,
    double sampleRate,
    double chunkDurationMs,
    bool mute,
    bool isMono,
    bool emitSilence,
    const int32_t* includeProcesses,
    int32_t includeProcessCount,
    const int32_t* excludeProcesses,
    int32_t excludeProcessCount
) {
    if (!handle) return -1;
    
    auto* capture = static_cast<WasapiCapture*>(handle);
    return capture->StartSystemAudio(
        sampleRate,
        chunkDurationMs,
        mute,  // Ignored on Windows
        isMono,
        emitSilence,
        includeProcesses,
        includeProcessCount,
        excludeProcesses,
        excludeProcessCount
    );
}

int32_t audio_start_microphone(
    AudioRecorderHandle handle,
    double sampleRate,
    double chunkDurationMs,
    bool isMono,
    bool emitSilence,
    const char* deviceUID,
    double gain
) {
    if (!handle) return -1;
    
    auto* capture = static_cast<WasapiCapture*>(handle);
    return capture->StartMicrophone(
        sampleRate,
        chunkDurationMs,
        isMono,
        emitSilence,
        deviceUID,
        gain
    );
}

int32_t audio_stop(AudioRecorderHandle handle) {
    if (!handle) return -1;
    
    auto* capture = static_cast<WasapiCapture*>(handle);
    return capture->Stop();
}

void audio_destroy(AudioRecorderHandle handle) {
    if (!handle) return;
    
    auto* capture = static_cast<WasapiCapture*>(handle);
    capture->Stop();
    delete capture;
}

bool audio_is_running(AudioRecorderHandle handle) {
    if (!handle) return false;
    
    auto* capture = static_cast<WasapiCapture*>(handle);
    return capture->IsRunning();
}

// ============================================================================
// Device Enumeration
// ============================================================================

int32_t audio_list_devices(AudioDeviceInfo** devices, int32_t* count) {
    EnsureComInitialized();
    
    if (!devices || !count) return -1;
    
    std::vector<AudioDeviceInfo> deviceList = AudioDeviceEnumerator::ListAllDevices();
    
    if (deviceList.empty()) {
        *devices = nullptr;
        *count = 0;
        return 0;
    }
    
    // Allocate array of AudioDeviceInfo
    *devices = new AudioDeviceInfo[deviceList.size()];
    *count = static_cast<int32_t>(deviceList.size());
    
    // Copy device info (strings are already allocated by the enumerator)
    for (size_t i = 0; i < deviceList.size(); i++) {
        (*devices)[i] = deviceList[i];
    }
    
    return 0;
}

void audio_free_device_list(AudioDeviceInfo* devices, int32_t count) {
    if (!devices) return;
    
    for (int32_t i = 0; i < count; i++) {
        if (devices[i].uid) free(devices[i].uid);
        if (devices[i].name) free(devices[i].name);
        if (devices[i].manufacturer) free(devices[i].manufacturer);
    }
    
    delete[] devices;
}

char* audio_get_default_input_device(void) {
    EnsureComInitialized();
    
    std::wstring id = AudioDeviceEnumerator::GetDefaultInputDeviceId();
    if (id.empty()) return nullptr;
    
    // Convert to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, id.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return nullptr;
    
    char* result = static_cast<char*>(malloc(len));
    WideCharToMultiByte(CP_UTF8, 0, id.c_str(), -1, result, len, nullptr, nullptr);
    return result;
}

char* audio_get_default_output_device(void) {
    EnsureComInitialized();
    
    std::wstring id = AudioDeviceEnumerator::GetDefaultOutputDeviceId();
    if (id.empty()) return nullptr;
    
    // Convert to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, id.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return nullptr;
    
    char* result = static_cast<char*>(malloc(len));
    WideCharToMultiByte(CP_UTF8, 0, id.c_str(), -1, result, len, nullptr, nullptr);
    return result;
}

// ============================================================================
// Permissions
// ============================================================================

int32_t audio_system_permission_status(void) {
    return AudioPermissions::GetSystemAudioStatus();
}

void audio_system_permission_request(PermissionCallback callback, void* context) {
    AudioPermissions::RequestSystemAudio(callback, context);
}

bool audio_system_permission_available(void) {
    return AudioPermissions::IsSystemAudioAvailable();
}

bool audio_open_system_settings(void) {
    return AudioPermissions::OpenSystemSettings();
}

int32_t audio_mic_permission_status(void) {
    EnsureComInitialized();
    return AudioPermissions::GetMicrophoneStatus();
}

void audio_mic_permission_request(PermissionCallback callback, void* context) {
    EnsureComInitialized();
    AudioPermissions::RequestMicrophone(callback, context);
}

}  // extern "C"
