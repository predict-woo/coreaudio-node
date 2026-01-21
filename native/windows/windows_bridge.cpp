#include "audio_bridge.h"
#include "wasapi_capture.h"
#include "mta_thread.h"
#include <combaseapi.h>
#include <cstring>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <Psapi.h>
#include <vector>

#pragma comment(lib, "Psapi.lib")

// Global MTA cookie - keeps COM MTA alive for the module's lifetime
// Uses CoIncrementMTAUsage (Windows 8+) for cleaner MTA management
CO_MTA_USAGE_COOKIE g_mtaCookie = nullptr;

// Helper function to get process name from PID
static std::wstring GetProcessNameFromPid(DWORD pid) {
    std::wstring name;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess) {
        WCHAR buffer[MAX_PATH];
        if (GetModuleBaseNameW(hProcess, NULL, buffer, MAX_PATH) > 0) {
            name = buffer;
            // Remove .exe extension if present
            size_t extPos = name.rfind(L".exe");
            if (extPos != std::wstring::npos && extPos == name.length() - 4) {
                name = name.substr(0, extPos);
            }
        }
        CloseHandle(hProcess);
    }
    return name;
}

// Alias for InitializeMTAThread from mta_thread.h
// Ensure the MTA is initialized before any COM operation
static bool EnsureMTAInitialized() {
    return InitializeMTAThread();
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
    EnsureMTAInitialized();
    
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
    if (!devices || !count) return -1;
    
    *devices = nullptr;
    *count = 0;
    
    // Ensure the MTA thread is running
    if (!EnsureMTAInitialized()) {
        return 0;  // Return empty list if MTA failed
    }
    
    try {
        std::vector<AudioDeviceInfo> deviceList = AudioDeviceEnumerator::ListAllDevices();
        
        if (!deviceList.empty()) {
            // Allocate array of AudioDeviceInfo
            *devices = new AudioDeviceInfo[deviceList.size()];
            *count = static_cast<int32_t>(deviceList.size());
            
            // Copy device info (strings are already allocated by the enumerator)
            for (size_t i = 0; i < deviceList.size(); i++) {
                (*devices)[i] = deviceList[i];
            }
        }
    }
    catch (...) {
        // Catch any exceptions and return empty list
        *devices = nullptr;
        *count = 0;
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
    EnsureMTAInitialized();
    
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
    EnsureMTAInitialized();
    
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
    EnsureMTAInitialized();
    return AudioPermissions::GetMicrophoneStatus();
}

void audio_mic_permission_request(PermissionCallback callback, void* context) {
    EnsureMTAInitialized();
    AudioPermissions::RequestMicrophone(callback, context);
}

// ============================================================================
// Microphone Activity Monitor (stub - full implementation TODO)
// ============================================================================

struct MicActivityMonitorState {
    MicActivityChangeCallback changeCallback;
    MicActivityDeviceCallback deviceCallback;
    MicActivityErrorCallback errorCallback;
    void* userContext;
    bool isRunning;
};

MicActivityMonitorHandle mic_activity_create(
    MicActivityChangeCallback changeCallback,
    MicActivityDeviceCallback deviceCallback,
    MicActivityErrorCallback errorCallback,
    void* userContext
) {
    // Note: We don't initialize COM here to avoid conflicts with Electron's COM state
    // COM will be initialized lazily when needed by specific functions
    
    auto* state = new MicActivityMonitorState{
        changeCallback,
        deviceCallback,
        errorCallback,
        userContext,
        false
    };
    
    return static_cast<MicActivityMonitorHandle>(state);
}

int32_t mic_activity_start(MicActivityMonitorHandle handle, const char* scope) {
    if (!handle) return -1;
    
    auto* state = static_cast<MicActivityMonitorState*>(handle);
    state->isRunning = true;
    
    // TODO: Implement WASAPI session monitoring
    // - Use IAudioSessionManager2::RegisterSessionNotification
    // - Track capture endpoint sessions
    // - Emit change events when sessions become active/inactive
    
    return 0;
}

int32_t mic_activity_stop(MicActivityMonitorHandle handle) {
    if (!handle) return -1;
    
    auto* state = static_cast<MicActivityMonitorState*>(handle);
    state->isRunning = false;
    
    return 0;
}

void mic_activity_destroy(MicActivityMonitorHandle handle) {
    if (!handle) return;
    
    auto* state = static_cast<MicActivityMonitorState*>(handle);
    state->isRunning = false;
    delete state;
}

bool mic_activity_is_active(MicActivityMonitorHandle handle) {
    if (!handle) return false;
    
    // TODO: Query active capture sessions
    return false;
}

int32_t mic_activity_get_active_device_ids(
    MicActivityMonitorHandle handle,
    char*** deviceIds,
    int32_t* count
) {
    if (!handle || !deviceIds || !count) return -1;
    
    // TODO: Return list of devices with active capture sessions
    *deviceIds = nullptr;
    *count = 0;
    
    return 0;
}

void mic_activity_free_device_ids(char** deviceIds, int32_t count) {
    if (!deviceIds) return;
    
    for (int32_t i = 0; i < count; i++) {
        if (deviceIds[i]) free(deviceIds[i]);
    }
    
    free(deviceIds);
}

int32_t mic_activity_get_active_processes(
    MicActivityMonitorHandle handle,
    int32_t** pids,
    char*** names,
    char*** bundleIds,
    int32_t* count
) {
    if (!pids || !names || !bundleIds || !count) {
        return -1;
    }

    // Initialize outputs
    *pids = nullptr;
    *names = nullptr;
    *bundleIds = nullptr;
    *count = 0;

    // Ensure the MTA thread is running
    if (!EnsureMTAInitialized()) {
        return 0;  // Return empty if MTA failed
    }

    try {
        HRESULT hr = S_OK;
        
        // Get device enumerator
        IMMDeviceEnumerator* pEnumerator = nullptr;
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&pEnumerator)
        );
        if (FAILED(hr) || !pEnumerator) {
            return 0;  // Return empty instead of error for robustness
        }

        // Get default capture (microphone) device
        IMMDevice* pDevice = nullptr;
        hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
        if (FAILED(hr) || !pDevice) {
            pEnumerator->Release();
            return 0;  // No mic device, return empty
        }

        // Activate session manager
        IAudioSessionManager2* pSessionManager = nullptr;
        hr = pDevice->Activate(
            __uuidof(IAudioSessionManager2),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&pSessionManager)
        );
        if (FAILED(hr) || !pSessionManager) {
            pDevice->Release();
            pEnumerator->Release();
            return 0;
        }

        // Get session enumerator
        IAudioSessionEnumerator* pSessionEnum = nullptr;
        hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
        if (FAILED(hr) || !pSessionEnum) {
            pSessionManager->Release();
            pDevice->Release();
            pEnumerator->Release();
            return 0;
        }

        // Get session count
        int sessionCount = 0;
        hr = pSessionEnum->GetCount(&sessionCount);
        if (FAILED(hr) || sessionCount == 0) {
            pSessionEnum->Release();
            pSessionManager->Release();
            pDevice->Release();
            pEnumerator->Release();
            return 0;
        }

        // Collect active sessions
        std::vector<DWORD> activePids;
        std::vector<std::wstring> activeNames;

        for (int i = 0; i < sessionCount; i++) {
            IAudioSessionControl* pSessionControl = nullptr;
            hr = pSessionEnum->GetSession(i, &pSessionControl);
            if (FAILED(hr) || !pSessionControl) continue;

            // Check if session is active
            AudioSessionState state;
            hr = pSessionControl->GetState(&state);
            if (FAILED(hr) || state != AudioSessionStateActive) {
                pSessionControl->Release();
                continue;
            }

            // Get IAudioSessionControl2 for process info
            IAudioSessionControl2* pSessionControl2 = nullptr;
            hr = pSessionControl->QueryInterface(
                __uuidof(IAudioSessionControl2),
                reinterpret_cast<void**>(&pSessionControl2)
            );
            if (FAILED(hr) || !pSessionControl2) {
                pSessionControl->Release();
                continue;
            }

            // Skip system sounds
            hr = pSessionControl2->IsSystemSoundsSession();
            if (hr == S_OK) {
                pSessionControl2->Release();
                pSessionControl->Release();
                continue;
            }

            // Get process ID
            DWORD pid = 0;
            hr = pSessionControl2->GetProcessId(&pid);
            if (FAILED(hr) || pid == 0) {
                // AUDCLNT_S_NO_SINGLE_PROCESS means multi-process session - skip
                pSessionControl2->Release();
                pSessionControl->Release();
                continue;
            }

            // Avoid duplicates
            bool duplicate = false;
            for (DWORD existingPid : activePids) {
                if (existingPid == pid) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                pSessionControl2->Release();
                pSessionControl->Release();
                continue;
            }

            // Get process name
            std::wstring procName = GetProcessNameFromPid(pid);
            if (procName.empty()) {
                procName = L"Unknown";
            }

            activePids.push_back(pid);
            activeNames.push_back(procName);

            pSessionControl2->Release();
            pSessionControl->Release();
        }

        // Cleanup COM objects
        pSessionEnum->Release();
        pSessionManager->Release();
        pDevice->Release();
        pEnumerator->Release();

        // Allocate output arrays
        if (activePids.empty()) {
            return 0;
        }

        size_t n = activePids.size();
        *pids = static_cast<int32_t*>(malloc(n * sizeof(int32_t)));
        *names = static_cast<char**>(malloc(n * sizeof(char*)));
        *bundleIds = static_cast<char**>(malloc(n * sizeof(char*)));

        if (!*pids || !*names || !*bundleIds) {
            free(*pids);
            free(*names);
            free(*bundleIds);
            *pids = nullptr;
            *names = nullptr;
            *bundleIds = nullptr;
            return 0;
        }

        for (size_t i = 0; i < n; i++) {
            (*pids)[i] = static_cast<int32_t>(activePids[i]);

            // Convert wide string to UTF-8
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, activeNames[i].c_str(), -1, nullptr, 0, nullptr, nullptr);
            (*names)[i] = static_cast<char*>(malloc(utf8Len));
            if ((*names)[i]) {
                WideCharToMultiByte(CP_UTF8, 0, activeNames[i].c_str(), -1, (*names)[i], utf8Len, nullptr, nullptr);
            }

            // Windows doesn't have bundle IDs - use empty string
            (*bundleIds)[i] = _strdup("");
        }

        *count = static_cast<int32_t>(n);
        return 0;
    }
    catch (...) {
        // Catch any exception and return empty result
        return 0;
    }
}

void mic_activity_free_processes(
    int32_t* pids,
    char** names,
    char** bundleIds,
    int32_t count
) {
    if (pids) free(pids);
    
    if (names) {
        for (int32_t i = 0; i < count; i++) {
            if (names[i]) free(names[i]);
        }
        free(names);
    }
    
    if (bundleIds) {
        for (int32_t i = 0; i < count; i++) {
            if (bundleIds[i]) free(bundleIds[i]);
        }
        free(bundleIds);
    }
}

}  // extern "C"
