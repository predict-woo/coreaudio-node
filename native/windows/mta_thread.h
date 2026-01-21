#pragma once

#include <windows.h>
#include <combaseapi.h>

/**
 * MTA Reference - Keeps the COM Multi-Threaded Apartment (MTA) alive using CoIncrementMTAUsage.
 * 
 * This is a cleaner approach than creating a dedicated thread. CoIncrementMTAUsage (Windows 8+)
 * keeps the MTA alive without needing an explicit thread, and avoids issues with creating
 * threads during DLL loading.
 * 
 * The problem: Electron's main thread uses STA (Single-Threaded Apartment),
 * but native modules often need MTA for WASAPI and other COM operations.
 * Calling CoInitializeEx with a different apartment model on an already-
 * initialized thread fails with RPC_E_CHANGED_MODE.
 * 
 * The solution: Use CoIncrementMTAUsage to keep the MTA alive. This allows
 * COM objects to be created and used from any thread without explicitly
 * initializing COM on that thread.
 */

// Global MTA cookie - used to decrement MTA usage on cleanup
extern CO_MTA_USAGE_COOKIE g_mtaCookie;

// Initialize the MTA - call this early in module initialization
// Returns true if MTA is available (either we initialized it or it was already active)
inline bool InitializeMTAThread() {
    if (g_mtaCookie != nullptr) {
        return true;  // Already initialized
    }
    
    // Try to increment MTA usage (Windows 8+)
    // This keeps the MTA alive without needing a dedicated thread
    HRESULT hr = CoIncrementMTAUsage(&g_mtaCookie);
    
    if (SUCCEEDED(hr)) {
        return true;
    }
    
    // CoIncrementMTAUsage failed - MTA might not be available
    // This shouldn't happen on Windows 8+, but handle it gracefully
    g_mtaCookie = nullptr;
    return false;
}

// Cleanup the MTA - call this on module unload
inline void CleanupMTAThread() {
    if (g_mtaCookie) {
        CoDecrementMTAUsage(g_mtaCookie);
        g_mtaCookie = nullptr;
    }
}
