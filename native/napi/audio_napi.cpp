#include <napi.h>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <cstring>
#include "audio_bridge.h"

// Forward declarations
class AudioRecorderWrapper;

// Thread-safe queue for events
struct AudioEvent {
    int32_t type;          // 0=data, 1=start, 2=stop, 3=error, 4=metadata
    std::vector<uint8_t> data;
    std::string message;
    double sampleRate;
    uint32_t channelsPerFrame;
    uint32_t bitsPerChannel;
    bool isFloat;
    std::string encoding;
};

class AudioRecorderWrapper : public Napi::ObjectWrap<AudioRecorderWrapper> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    AudioRecorderWrapper(const Napi::CallbackInfo& info);
    ~AudioRecorderWrapper();

private:
    static Napi::FunctionReference constructor;

    // Instance methods
    Napi::Value StartSystemAudio(const Napi::CallbackInfo& info);
    Napi::Value StartMicrophone(const Napi::CallbackInfo& info);
    Napi::Value Stop(const Napi::CallbackInfo& info);
    Napi::Value IsRunning(const Napi::CallbackInfo& info);
    Napi::Value ProcessEvents(const Napi::CallbackInfo& info);

    // Callbacks from Swift
    static void OnData(const uint8_t* data, int32_t length, void* context);
    static void OnEvent(int32_t eventType, const char* message, void* context);
    static void OnMetadata(double sampleRate, uint32_t channelsPerFrame,
                          uint32_t bitsPerChannel, bool isFloat,
                          const char* encoding, void* context);

    // Queue management
    void QueueEvent(AudioEvent event);
    std::vector<AudioEvent> DrainEvents();

    AudioRecorderHandle handle_;
    std::mutex eventMutex_;
    std::queue<AudioEvent> eventQueue_;
    std::atomic<bool> isDestroyed_{false};
};

Napi::FunctionReference AudioRecorderWrapper::constructor;

Napi::Object AudioRecorderWrapper::Init(Napi::Env env, Napi::Object exports) {
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "AudioRecorderNative", {
        InstanceMethod("startSystemAudio", &AudioRecorderWrapper::StartSystemAudio),
        InstanceMethod("startMicrophone", &AudioRecorderWrapper::StartMicrophone),
        InstanceMethod("stop", &AudioRecorderWrapper::Stop),
        InstanceMethod("isRunning", &AudioRecorderWrapper::IsRunning),
        InstanceMethod("processEvents", &AudioRecorderWrapper::ProcessEvents),
    });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("AudioRecorderNative", func);
    return exports;
}

AudioRecorderWrapper::AudioRecorderWrapper(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<AudioRecorderWrapper>(info) {
    Napi::Env env = info.Env();

    handle_ = audio_create(
        &AudioRecorderWrapper::OnData,
        &AudioRecorderWrapper::OnEvent,
        &AudioRecorderWrapper::OnMetadata,
        this
    );

    if (!handle_) {
        Napi::Error::New(env, "Failed to create AudioRecorder session").ThrowAsJavaScriptException();
    }
}

AudioRecorderWrapper::~AudioRecorderWrapper() {
    isDestroyed_ = true;
    if (handle_) {
        audio_destroy(handle_);
        handle_ = nullptr;
    }
}

Napi::Value AudioRecorderWrapper::StartSystemAudio(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Options object expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object options = info[0].As<Napi::Object>();

    // Extract options with defaults
    double sampleRate = 0;
    if (options.Has("sampleRate") && options.Get("sampleRate").IsNumber()) {
        sampleRate = options.Get("sampleRate").As<Napi::Number>().DoubleValue();
    }

    double chunkDurationMs = 200;
    if (options.Has("chunkDurationMs") && options.Get("chunkDurationMs").IsNumber()) {
        chunkDurationMs = options.Get("chunkDurationMs").As<Napi::Number>().DoubleValue();
    }

    bool mute = false;
    if (options.Has("mute") && options.Get("mute").IsBoolean()) {
        mute = options.Get("mute").As<Napi::Boolean>().Value();
    }

    bool isMono = true;
    if (options.Has("stereo") && options.Get("stereo").IsBoolean()) {
        isMono = !options.Get("stereo").As<Napi::Boolean>().Value();
    }

    bool emitSilence = true;
    if (options.Has("emitSilence") && options.Get("emitSilence").IsBoolean()) {
        emitSilence = options.Get("emitSilence").As<Napi::Boolean>().Value();
    }

    // Handle process arrays
    std::vector<int32_t> includeProcesses;
    std::vector<int32_t> excludeProcesses;

    if (options.Has("includeProcesses") && options.Get("includeProcesses").IsArray()) {
        Napi::Array arr = options.Get("includeProcesses").As<Napi::Array>();
        for (uint32_t i = 0; i < arr.Length(); i++) {
            if (arr.Get(i).IsNumber()) {
                includeProcesses.push_back(arr.Get(i).As<Napi::Number>().Int32Value());
            }
        }
    }

    if (options.Has("excludeProcesses") && options.Get("excludeProcesses").IsArray()) {
        Napi::Array arr = options.Get("excludeProcesses").As<Napi::Array>();
        for (uint32_t i = 0; i < arr.Length(); i++) {
            if (arr.Get(i).IsNumber()) {
                excludeProcesses.push_back(arr.Get(i).As<Napi::Number>().Int32Value());
            }
        }
    }

    int32_t result = audio_start_system_audio(
        handle_,
        sampleRate,
        chunkDurationMs,
        mute,
        isMono,
        emitSilence,
        includeProcesses.empty() ? nullptr : includeProcesses.data(),
        static_cast<int32_t>(includeProcesses.size()),
        excludeProcesses.empty() ? nullptr : excludeProcesses.data(),
        static_cast<int32_t>(excludeProcesses.size())
    );

    if (result != 0) {
        std::string errorMsg = "Failed to start system audio recording: error code " + std::to_string(result);
        Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
        return env.Null();
    }

    return env.Undefined();
}

Napi::Value AudioRecorderWrapper::StartMicrophone(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Options object expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object options = info[0].As<Napi::Object>();

    // Extract options with defaults
    double sampleRate = 0;
    if (options.Has("sampleRate") && options.Get("sampleRate").IsNumber()) {
        sampleRate = options.Get("sampleRate").As<Napi::Number>().DoubleValue();
    }

    double chunkDurationMs = 200;
    if (options.Has("chunkDurationMs") && options.Get("chunkDurationMs").IsNumber()) {
        chunkDurationMs = options.Get("chunkDurationMs").As<Napi::Number>().DoubleValue();
    }

    bool isMono = true;
    if (options.Has("stereo") && options.Get("stereo").IsBoolean()) {
        isMono = !options.Get("stereo").As<Napi::Boolean>().Value();
    }

    bool emitSilence = true;
    if (options.Has("emitSilence") && options.Get("emitSilence").IsBoolean()) {
        emitSilence = options.Get("emitSilence").As<Napi::Boolean>().Value();
    }

    const char* deviceUID = nullptr;
    std::string deviceUIDStr;
    if (options.Has("deviceId") && options.Get("deviceId").IsString()) {
        deviceUIDStr = options.Get("deviceId").As<Napi::String>().Utf8Value();
        deviceUID = deviceUIDStr.c_str();
    }

    double gain = 1.0;
    if (options.Has("gain") && options.Get("gain").IsNumber()) {
        gain = options.Get("gain").As<Napi::Number>().DoubleValue();
    }

    int32_t result = audio_start_microphone(
        handle_,
        sampleRate,
        chunkDurationMs,
        isMono,
        emitSilence,
        deviceUID,
        gain
    );

    if (result != 0) {
        std::string errorMsg = "Failed to start microphone recording: error code " + std::to_string(result);
        Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
        return env.Null();
    }

    return env.Undefined();
}

Napi::Value AudioRecorderWrapper::Stop(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    int32_t result = audio_stop(handle_);
    if (result != 0) {
        Napi::Error::New(env, "Failed to stop recording").ThrowAsJavaScriptException();
    }

    return env.Undefined();
}

Napi::Value AudioRecorderWrapper::IsRunning(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), audio_is_running(handle_));
}

Napi::Value AudioRecorderWrapper::ProcessEvents(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::vector<AudioEvent> events = DrainEvents();
    Napi::Array result = Napi::Array::New(env, events.size());

    for (size_t i = 0; i < events.size(); i++) {
        const AudioEvent& event = events[i];
        Napi::Object obj = Napi::Object::New(env);

        obj.Set("type", Napi::Number::New(env, event.type));

        switch (event.type) {
            case 0: // data
                {
                    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(
                        env, event.data.data(), event.data.size()
                    );
                    obj.Set("data", buffer);
                }
                break;

            case 1: // start
            case 2: // stop
                // No additional data needed
                break;

            case 3: // error
                obj.Set("message", Napi::String::New(env, event.message));
                break;

            case 4: // metadata
                obj.Set("sampleRate", Napi::Number::New(env, event.sampleRate));
                obj.Set("channelsPerFrame", Napi::Number::New(env, event.channelsPerFrame));
                obj.Set("bitsPerChannel", Napi::Number::New(env, event.bitsPerChannel));
                obj.Set("isFloat", Napi::Boolean::New(env, event.isFloat));
                obj.Set("encoding", Napi::String::New(env, event.encoding));
                break;
        }

        result.Set(i, obj);
    }

    return result;
}

void AudioRecorderWrapper::OnData(const uint8_t* data, int32_t length, void* context) {
    AudioRecorderWrapper* self = static_cast<AudioRecorderWrapper*>(context);
    if (self->isDestroyed_) return;

    AudioEvent event;
    event.type = 0;
    event.data.assign(data, data + length);
    self->QueueEvent(std::move(event));
}

void AudioRecorderWrapper::OnEvent(int32_t eventType, const char* message, void* context) {
    AudioRecorderWrapper* self = static_cast<AudioRecorderWrapper*>(context);
    if (self->isDestroyed_) return;

    AudioEvent event;
    // eventType from Swift: 0=start, 1=stop, 2=error
    // We remap: 1=start, 2=stop, 3=error (0 is reserved for data)
    event.type = eventType + 1;
    if (message) {
        event.message = message;
    }
    self->QueueEvent(std::move(event));
}

void AudioRecorderWrapper::OnMetadata(double sampleRate, uint32_t channelsPerFrame,
                                  uint32_t bitsPerChannel, bool isFloat,
                                  const char* encoding, void* context) {
    AudioRecorderWrapper* self = static_cast<AudioRecorderWrapper*>(context);
    if (self->isDestroyed_) return;

    AudioEvent event;
    event.type = 4;
    event.sampleRate = sampleRate;
    event.channelsPerFrame = channelsPerFrame;
    event.bitsPerChannel = bitsPerChannel;
    event.isFloat = isFloat;
    event.encoding = encoding ? encoding : "";
    self->QueueEvent(std::move(event));
}

void AudioRecorderWrapper::QueueEvent(AudioEvent event) {
    std::lock_guard<std::mutex> lock(eventMutex_);
    eventQueue_.push(std::move(event));
}

std::vector<AudioEvent> AudioRecorderWrapper::DrainEvents() {
    std::lock_guard<std::mutex> lock(eventMutex_);
    std::vector<AudioEvent> events;
    while (!eventQueue_.empty()) {
        events.push_back(std::move(eventQueue_.front()));
        eventQueue_.pop();
    }
    return events;
}

// ============================================================================
// Device Enumeration
// ============================================================================

Napi::Value ListDevices(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    int32_t count = 0;

    AudioDeviceInfo* deviceList = nullptr;
    int32_t result = audio_list_devices(&deviceList, &count);
    if (result != 0 || deviceList == nullptr || count == 0) {
        return Napi::Array::New(env, 0);
    }

    Napi::Array arr = Napi::Array::New(env, count);
    for (int32_t i = 0; i < count; i++) {
        const AudioDeviceInfo& device = deviceList[i];

        Napi::Object obj = Napi::Object::New(env);
        obj.Set("id", Napi::String::New(env, device.uid ? device.uid : ""));
        obj.Set("name", Napi::String::New(env, device.name ? device.name : ""));
        obj.Set("manufacturer", Napi::String::New(env, device.manufacturer ? device.manufacturer : ""));
        obj.Set("isDefault", Napi::Boolean::New(env, device.isDefault));
        obj.Set("isInput", Napi::Boolean::New(env, device.isInput));
        obj.Set("isOutput", Napi::Boolean::New(env, device.isOutput));
        obj.Set("sampleRate", Napi::Number::New(env, device.sampleRate));
        obj.Set("channelCount", Napi::Number::New(env, device.channelCount));
        arr.Set(i, obj);
    }

    audio_free_device_list(deviceList, count);
    return arr;
}

Napi::Value GetDefaultInputDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    char* uid = audio_get_default_input_device();
    if (uid == nullptr) {
        return env.Null();
    }

    Napi::String result = Napi::String::New(env, uid);
    free(uid);
    return result;
}

Napi::Value GetDefaultOutputDevice(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    char* uid = audio_get_default_output_device();
    if (uid == nullptr) {
        return env.Null();
    }

    Napi::String result = Napi::String::New(env, uid);
    free(uid);
    return result;
}

// ============================================================================
// System Audio Permission API
// ============================================================================

Napi::Value GetSystemAudioPermissionStatus(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    int32_t status = audio_system_permission_status();

    std::string statusStr;
    switch (status) {
        case 0: statusStr = "unknown"; break;
        case 1: statusStr = "denied"; break;
        case 2: statusStr = "authorized"; break;
        default: statusStr = "unknown"; break;
    }

    return Napi::String::New(env, statusStr);
}

Napi::Value IsSystemAudioPermissionAvailable(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), audio_system_permission_available());
}

Napi::Value OpenSystemSettings(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), audio_open_system_settings());
}

// Thread-safe function for permission request callback
struct PermissionRequestContext {
    Napi::ThreadSafeFunction tsfn;
};

void SystemAudioPermissionCallback(bool granted, void* context) {
    auto* ctx = static_cast<PermissionRequestContext*>(context);

    ctx->tsfn.BlockingCall([granted](Napi::Env env, Napi::Function callback) {
        callback.Call({Napi::Boolean::New(env, granted)});
    });

    ctx->tsfn.Release();
    delete ctx;
}

Napi::Value RequestSystemAudioPermission(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Callback function expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Function callback = info[0].As<Napi::Function>();

    auto* ctx = new PermissionRequestContext();
    ctx->tsfn = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "SystemAudioPermissionCallback",
        0,
        1
    );

    audio_system_permission_request(SystemAudioPermissionCallback, ctx);

    return env.Undefined();
}

// ============================================================================
// Microphone Permission API
// ============================================================================

Napi::Value GetMicPermissionStatus(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    int32_t status = audio_mic_permission_status();

    std::string statusStr;
    switch (status) {
        case 0: statusStr = "unknown"; break;
        case 1: statusStr = "denied"; break;
        case 2: statusStr = "authorized"; break;
        default: statusStr = "unknown"; break;
    }

    return Napi::String::New(env, statusStr);
}

void MicPermissionCallback(bool granted, void* context) {
    auto* ctx = static_cast<PermissionRequestContext*>(context);

    ctx->tsfn.BlockingCall([granted](Napi::Env env, Napi::Function callback) {
        callback.Call({Napi::Boolean::New(env, granted)});
    });

    ctx->tsfn.Release();
    delete ctx;
}

Napi::Value RequestMicPermission(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Callback function expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Function callback = info[0].As<Napi::Function>();

    auto* ctx = new PermissionRequestContext();
    ctx->tsfn = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "MicPermissionCallback",
        0,
        1
    );

    audio_mic_permission_request(MicPermissionCallback, ctx);

    return env.Undefined();
}

// ============================================================================
// Module initialization
// ============================================================================

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    AudioRecorderWrapper::Init(env, exports);

    // Device enumeration
    exports.Set("listDevices", Napi::Function::New(env, ListDevices));
    exports.Set("getDefaultInputDevice", Napi::Function::New(env, GetDefaultInputDevice));
    exports.Set("getDefaultOutputDevice", Napi::Function::New(env, GetDefaultOutputDevice));

    // System audio permission functions
    exports.Set("getSystemAudioPermissionStatus", Napi::Function::New(env, GetSystemAudioPermissionStatus));
    exports.Set("isSystemAudioPermissionAvailable", Napi::Function::New(env, IsSystemAudioPermissionAvailable));
    exports.Set("requestSystemAudioPermission", Napi::Function::New(env, RequestSystemAudioPermission));
    exports.Set("openSystemSettings", Napi::Function::New(env, OpenSystemSettings));

    // Microphone permission functions
    exports.Set("getMicPermissionStatus", Napi::Function::New(env, GetMicPermissionStatus));
    exports.Set("requestMicPermission", Napi::Function::New(env, RequestMicPermission));

    return exports;
}

NODE_API_MODULE(native_audio, Init)
