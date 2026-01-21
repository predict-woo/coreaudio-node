# Electron Windows Crash Fix: Delay-Load Hook Symbol Resolution

## Problem Summary

The native N-API module crashed immediately upon loading in Electron on Windows, while working perfectly in standalone Node.js. Exit code: `0xFFFF4FC3` (4294930435).

## Symptoms

```
[MeetingDetection] Creating MicrophoneActivityMonitor instance...
[native-audio-node] Init: Starting module initialization
[native-audio-node] AudioRecorderWrapper::Init: ENTERING
[crash - exit code 4294930435]
```

The crash occurred during `NODE_API_MODULE` initialization, specifically when calling the first N-API function (`napi_open_handle_scope`).

## Debugging Timeline

### 1. Initial Hypothesis: COM/MTA Issues

Since the module uses WASAPI (COM-based), initial debugging focused on COM initialization. Tried `CoIncrementMTAUsage` instead of dedicated MTA thread. **Result: Still crashed.**

### 2. Narrowing Down with Debug Logging

Added `OutputDebugStringA` + `fprintf(stderr)` logging to trace execution:

```cpp
DebugLog("AudioRecorderWrapper::Init: ENTERING");
DebugLog("AudioRecorderWrapper::Init: About to create HandleScope with env=%p", env);
Napi::HandleScope scope(env);  // <-- CRASH HERE
DebugLog("AudioRecorderWrapper::Init: HandleScope created");  // Never reached
```

### 3. SEH Guard Reveals Exception Code

Wrapped the N-API call in a SEH `__try/__except` block:

```cpp
static napi_handle_scope SafeOpenHandleScope(napi_env env) {
    napi_handle_scope scope = nullptr;
    __try {
        napi_status status = napi_open_handle_scope(env, &scope);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        DebugLog("SEH EXCEPTION! Code: 0x%08X", code);
        return nullptr;
    }
    return scope;
}
```

**Result:**

```
SEH EXCEPTION! Code: 0xC0000005
napi_open_handle_scope CRASHED!
```

`0xC0000005` = `EXCEPTION_ACCESS_VIOLATION`. The `napi_open_handle_scope` function pointer was resolving to an invalid address.

## Root Cause

### How N-API Symbol Resolution Works on Windows

Native Node addons on Windows use **delay-load linking** against `node.exe`. The linker flag `/DELAYLOAD:NODE.EXE` tells the loader not to resolve N-API symbols at load time, but to defer resolution until first use.

When a delay-loaded function is first called:

1. The delay-load helper intercepts the call
2. It invokes the `__pfnDliNotifyHook2` callback chain
3. The hook resolves the DLL and function address
4. Subsequent calls go directly to the resolved address

### cmake-js Default Hook

cmake-js provides `win_delay_load_hook.cc`:

```cpp
static FARPROC WINAPI load_exe_hook(unsigned int event, DelayLoadInfo* info) {
  if (event == dliNotePreGetProcAddress) {
    FARPROC ret = GetProcAddress(node_dll, info->dlp.szProcName);
    if (ret) return ret;
    ret = GetProcAddress(nw_dll, info->dlp.szProcName);
    return ret;
  }
  if (event == dliStartProcessing) {
    node_dll = GetModuleHandleA("node.dll");  // NULL in Electron
    nw_dll = GetModuleHandleA("nw.dll");      // NULL in Electron
    return NULL;
  }
  if (event == dliNotePreLoadLibrary) {
    if (_stricmp(info->szDll, "node.exe") != 0) return NULL;
    if (!node_dll) node_dll = GetModuleHandleA(NULL);  // Fallback
    return (FARPROC)node_dll;
  }
  return NULL;
}
```

### Why It Failed in Electron

The issue is subtle. In Electron:

- `GetModuleHandleA("node.dll")` returns `NULL` (no separate node.dll)
- `GetModuleHandleA("nw.dll")` returns `NULL` (not NW.js)
- `node_dll` stays `NULL` until `dliNotePreLoadLibrary` is called

The problem: **`dliNotePreGetProcAddress` is called BEFORE `dliNotePreLoadLibrary`** in some code paths, and when `node_dll` is still `NULL`, `GetProcAddress(NULL, ...)` returns garbage or causes an access violation when the returned "address" is called.

The event sequence observed:

```
dliStartProcessing       -> node_dll = NULL, nw_dll = NULL
dliNotePreGetProcAddress -> GetProcAddress(NULL, "napi_open_handle_scope") = ???
CRASH
```

The cmake-js hook assumes `dliNotePreLoadLibrary` always precedes `dliNotePreGetProcAddress`, but this isn't guaranteed.

## The Fix

Override `__pfnDliNotifyHook2` with a hook that:

1. Eagerly initializes the module handle in `dliStartProcessing`
2. Always returns a valid function pointer in `dliNotePreGetProcAddress`

```cpp
static HMODULE g_node_module = NULL;

static FARPROC WINAPI electronDelayLoadHook(unsigned int event, DelayLoadInfo* info) {
    if (event == dliStartProcessing) {
        // Eagerly get the main process handle
        g_node_module = GetModuleHandleA(NULL);
        return NULL;
    }

    if (event == dliNotePreLoadLibrary) {
        if (_stricmp(info->szDll, "node.exe") == 0 || _stricmp(info->szDll, "NODE.EXE") == 0) {
            return (FARPROC)g_node_module;
        }
        return NULL;
    }

    if (event == dliNotePreGetProcAddress) {
        // g_node_module is guaranteed non-NULL here
        return GetProcAddress(g_node_module, info->dlp.szProcName);
    }

    return NULL;
}

decltype(__pfnDliNotifyHook2) __pfnDliNotifyHook2 = electronDelayLoadHook;
```

Key differences from cmake-js hook:

1. **Eager initialization**: `g_node_module` is set in `dliStartProcessing`, not lazily in `dliNotePreLoadLibrary`
2. **Direct resolution**: `dliNotePreGetProcAddress` uses the pre-initialized `g_node_module`, never passing `NULL` to `GetProcAddress`
3. **No fallback chain**: We don't try `node.dll` or `nw.dll` since we know we're in Electron

## Why It Worked in Node.js

In standalone Node.js, `GetModuleHandleA("node.dll")` or the executable itself exports the N-API symbols correctly. The cmake-js hook's assumptions hold because the module handle is valid by the time `dliNotePreGetProcAddress` is called (or the race condition doesn't manifest due to different initialization order).

## N-API ABI Compatibility

After fixing the delay-load hook, tested building with plain Node.js headers:

```bash
npx cmake-js rebuild --arch=x64  # Uses Node.js 25.3.0 headers
```

This binary works perfectly in Electron 37.10.3. **The `--runtime=electron` flag is unnecessary for N-API modules** - N-API provides a stable ABI across Node.js and Electron versions. The only requirement is correct delay-load symbol resolution at runtime.

## Files Changed

- `native/napi/audio_napi.cpp`: Added custom `electronDelayLoadHook` and `__pfnDliNotifyHook2` definition

## Build Notes

The custom hook definition in our `.cpp` file takes precedence over cmake-js's `win_delay_load_hook.cc` because:

1. Both define the same symbol `__pfnDliNotifyHook2`
2. Our object file is linked first (appears earlier in the link command)
3. First definition wins for non-weak symbols

Verified with:

```bash
npx cmake-js rebuild --runtime=electron --runtime-version=37.10.3 --arch=x64
```

## Verification

After the fix, delay-load resolution logs show correct behavior:

```
DELAY LOAD: dliStartProcessing
DELAY LOAD: Main module handle: 00007FF685FF0000
DELAY LOAD: dliNotePreLoadLibrary for node.exe
DELAY LOAD: Returning main module handle 00007FF685FF0000 for node.exe
DELAY LOAD: dliNotePreGetProcAddress for napi_open_handle_scope from node.exe
DELAY LOAD: GetProcAddress(napi_open_handle_scope) = 00007FF686532E10
DELAY LOAD: dliNoteEndProcessing
```

All N-API symbols resolve to valid addresses within the Electron process.
