#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <chrono>

// ============================================================================
// Windows 10 2004+ Process Loopback API Definitions
// These types are defined in audioclientactivationparams.h but that header
// may not be available in all SDK versions. We define them manually here.
// ============================================================================

#ifndef AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK

// Process loopback mode enum
typedef enum _PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;

// Process loopback parameters
typedef struct _AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;

// Activation type enum
typedef enum _AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;

// Activation parameters structure
typedef struct _AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
} AUDIOCLIENT_ACTIVATION_PARAMS;

#endif // AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK

// Virtual audio device string for process loopback
#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

#include "audio_bridge.h"

// Forward declarations
class WasapiCapture;

// Completion handler for async audio interface activation
class ActivationCompletionHandler : public IActivateAudioInterfaceCompletionHandler {
public:
    ActivationCompletionHandler();
    ~ActivationCompletionHandler();

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;

    // IActivateAudioInterfaceCompletionHandler
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override;

    HRESULT Wait(DWORD timeout = INFINITE);
    IActivateAudioInterfaceAsyncOperation* GetOperation() { return operation_; }

private:
    LONG refCount_;
    HANDLE completionEvent_;
    std::atomic<bool> completed_;
    IActivateAudioInterfaceAsyncOperation* operation_;
};

// Main WASAPI capture class
class WasapiCapture {
public:
    WasapiCapture(
        AudioDataCallback dataCallback,
        AudioEventCallback eventCallback,
        AudioMetadataCallback metadataCallback,
        void* userContext
    );
    ~WasapiCapture();

    // System audio capture (loopback)
    int32_t StartSystemAudio(
        double sampleRate,
        double chunkDurationMs,
        bool mute,  // Ignored on Windows
        bool isMono,
        bool emitSilence,  // Generate silent buffers when no audio is playing
        const int32_t* includeProcesses,
        int32_t includeCount,
        const int32_t* excludeProcesses,
        int32_t excludeCount
    );

    // Microphone capture
    int32_t StartMicrophone(
        double sampleRate,
        double chunkDurationMs,
        bool isMono,
        bool emitSilence,  // Generate silent buffers when no audio is playing
        const char* deviceId,
        double gain
    );

    int32_t Stop();
    bool IsRunning() const { return running_; }

private:
    // Initialize system-wide loopback (fallback for older Windows or no process filter)
    HRESULT InitializeSystemLoopback();

    // Initialize process-specific loopback (Windows 10 2004+)
    HRESULT InitializeProcessLoopback(DWORD targetPid, PROCESS_LOOPBACK_MODE mode);

    // Initialize microphone capture
    HRESULT InitializeMicrophone(const wchar_t* deviceId);

    // Common initialization after audio client is set up
    HRESULT FinalizeInitialization();

    // Audio capture thread
    void CaptureThread();

    // Convert audio data to target format
    void ProcessAudioData(const BYTE* data, UINT32 numFrames);

    // Sample rate conversion (simple linear interpolation)
    void ResampleAudio(const float* input, size_t inputFrames, std::vector<float>& output);

    // Convert stereo to mono
    void ConvertToMono(const float* input, size_t frames, std::vector<float>& output);

    // Callbacks
    AudioDataCallback dataCallback_;
    AudioEventCallback eventCallback_;
    AudioMetadataCallback metadataCallback_;
    void* userContext_;

    // Audio client interfaces
    IAudioClient* audioClient_;
    IAudioCaptureClient* captureClient_;
    WAVEFORMATEX* mixFormat_;

    // Capture state
    std::thread captureThread_;
    std::atomic<bool> running_;
    HANDLE stopEvent_;

    // Audio format settings
    double targetSampleRate_;
    double chunkDurationMs_;
    bool isMono_;
    double gain_;
    bool emitSilence_;
    
    // Silence generation tracking
    std::chrono::steady_clock::time_point lastDataTime_;

    // Buffer for accumulating audio chunks
    std::vector<float> chunkBuffer_;
    size_t samplesPerChunk_;

    // Resampling state
    double resampleRatio_;
    std::vector<float> resampleBuffer_;
};

// Device enumeration helper
class AudioDeviceEnumerator {
public:
    static std::vector<AudioDeviceInfo> ListAllDevices();
    static std::wstring GetDefaultInputDeviceId();
    static std::wstring GetDefaultOutputDeviceId();

private:
    static bool GetDeviceInfo(IMMDevice* device, AudioDeviceInfo& info, bool isInput);
    static std::wstring GetDeviceProperty(IPropertyStore* props, const PROPERTYKEY& key);
};

// Permission helpers (Windows doesn't require explicit permissions for audio)
class AudioPermissions {
public:
    // System audio permission - always granted on Windows (loopback doesn't need permission)
    static int32_t GetSystemAudioStatus() { return 2; }  // authorized
    static void RequestSystemAudio(PermissionCallback callback, void* context);
    static bool IsSystemAudioAvailable() { return true; }

    // Microphone permission - check Windows privacy settings
    static int32_t GetMicrophoneStatus();
    static void RequestMicrophone(PermissionCallback callback, void* context);

    // Open Windows Sound Settings
    static bool OpenSystemSettings();
};
