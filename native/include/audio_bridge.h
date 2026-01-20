#ifndef AUDIO_BRIDGE_H
#define AUDIO_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle type for the audio capture session
typedef void* AudioRecorderHandle;

// Callback types
typedef void (*AudioDataCallback)(const uint8_t* data, int32_t length, void* context);
typedef void (*AudioEventCallback)(int32_t eventType, const char* message, void* context);
typedef void (*AudioMetadataCallback)(double sampleRate, uint32_t channelsPerFrame,
                                       uint32_t bitsPerChannel, bool isFloat,
                                       const char* encoding, void* context);

// Create a new audio recorder session
AudioRecorderHandle audio_create(
    AudioDataCallback dataCallback,
    AudioEventCallback eventCallback,
    AudioMetadataCallback metadataCallback,
    void* userContext
);

// Start system audio capture
// Note: mute parameter only works on macOS (silently ignored on Windows)
// Note: Windows only supports single process filtering (first PID is used)
// Note: emitSilence generates silent buffers when no audio is playing (Windows only, macOS always emits)
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
);

// Start microphone capture
// Note: emitSilence generates silent buffers when no audio is playing (Windows only, macOS always emits)
int32_t audio_start_microphone(
    AudioRecorderHandle handle,
    double sampleRate,
    double chunkDurationMs,
    bool isMono,
    bool emitSilence,
    const char* deviceUID,  // NULL for default device
    double gain             // 0.0 to any positive value (1.0 = unity gain)
);

// Stop audio capture
int32_t audio_stop(AudioRecorderHandle handle);

// Destroy the session and free resources
void audio_destroy(AudioRecorderHandle handle);

// Check if session is running
bool audio_is_running(AudioRecorderHandle handle);

// ============================================================================
// Device Enumeration
// ============================================================================

// Device info structure (platform-independent)
typedef struct {
    char* uid;           // Unique identifier (caller must free)
    char* name;          // Display name (caller must free)
    char* manufacturer;  // Manufacturer name (caller must free, may be NULL)
    bool isDefault;
    bool isInput;
    bool isOutput;
    double sampleRate;
    uint32_t channelCount;
} AudioDeviceInfo;

// List all audio devices
// Returns 0 on success, populates devices array and count
// Caller must free with audio_free_device_list
int32_t audio_list_devices(AudioDeviceInfo** devices, int32_t* count);

// Free device list allocated by audio_list_devices
void audio_free_device_list(AudioDeviceInfo* devices, int32_t count);

// Get default input device UID (caller must free)
char* audio_get_default_input_device(void);

// Get default output device UID (caller must free)
char* audio_get_default_output_device(void);

// ============================================================================
// System Audio Permission API
// ============================================================================

// Status: 0 = unknown, 1 = denied, 2 = authorized
int32_t audio_system_permission_status(void);

// Request permission callback type
typedef void (*PermissionCallback)(bool granted, void* context);

// Request system audio permission (async)
// On Windows, this is always granted (no permission needed for loopback)
void audio_system_permission_request(PermissionCallback callback, void* context);

// Check if permission API is available
// Returns true on macOS 14.2+, always true on Windows
bool audio_system_permission_available(void);

// Open System Settings to permission pane
// On Windows, opens Sound settings
bool audio_open_system_settings(void);

// ============================================================================
// Microphone Permission API
// ============================================================================

// Status: 0 = unknown, 1 = denied, 2 = authorized
int32_t audio_mic_permission_status(void);

// Request microphone permission (async)
void audio_mic_permission_request(PermissionCallback callback, void* context);

// ============================================================================
// Microphone Activity Monitor API
// ============================================================================

typedef void* MicActivityMonitorHandle;

// Event types: 0=change (aggregate), 1=deviceChange (per-device), 2=error
typedef void (*MicActivityChangeCallback)(bool isActive, void* context);
typedef void (*MicActivityDeviceCallback)(const char* deviceId, const char* deviceName, bool isActive, void* context);
typedef void (*MicActivityErrorCallback)(const char* message, void* context);

MicActivityMonitorHandle mic_activity_create(
    MicActivityChangeCallback changeCallback,
    MicActivityDeviceCallback deviceCallback,
    MicActivityErrorCallback errorCallback,
    void* userContext
);

// scope: "all" or "default"
int32_t mic_activity_start(MicActivityMonitorHandle handle, const char* scope);

int32_t mic_activity_stop(MicActivityMonitorHandle handle);

void mic_activity_destroy(MicActivityMonitorHandle handle);

bool mic_activity_is_active(MicActivityMonitorHandle handle);

// Get list of active device IDs (caller must free with mic_activity_free_device_ids)
int32_t mic_activity_get_active_device_ids(
    MicActivityMonitorHandle handle,
    char*** deviceIds,
    int32_t* count
);

void mic_activity_free_device_ids(char** deviceIds, int32_t count);

// Get list of processes currently using microphone input
// Returns parallel arrays: pids, names, bundleIds (caller must free with mic_activity_free_processes)
int32_t mic_activity_get_active_processes(
    MicActivityMonitorHandle handle,
    int32_t** pids,
    char*** names,
    char*** bundleIds,
    int32_t* count
);

void mic_activity_free_processes(int32_t* pids, char** names, char** bundleIds, int32_t count);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_BRIDGE_H
