import { createRequire } from 'module'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const require = createRequire(import.meta.url)

export type PermissionStatus = 'unknown' | 'denied' | 'authorized'

interface NativeAddon {
  // System audio permission
  getSystemAudioPermissionStatus(): PermissionStatus
  isSystemAudioPermissionAvailable(): boolean
  requestSystemAudioPermission(callback: (granted: boolean) => void): void
  // Microphone permission
  getMicPermissionStatus(): PermissionStatus
  requestMicPermission(callback: (granted: boolean) => void): void
  // Shared
  openSystemSettings(): boolean
}

let addon: NativeAddon | null = null

function loadAddon(): NativeAddon {
  if (addon) return addon

  const paths = [
    path.join(__dirname, '..', 'build', 'Release', 'native_audio.node'),
    path.join(__dirname, '..', '..', 'build', 'Release', 'native_audio.node'),
  ]

  for (const addonPath of paths) {
    try {
      addon = require(addonPath)
      return addon!
    } catch {
      // Try next path
    }
  }

  throw new Error('Failed to load native audio addon')
}

// ============================================================================
// System Audio Permission
// ============================================================================

/**
 * Check the current system audio recording permission status.
 *
 * **macOS:** Uses TCC private API - may not be available on all systems.
 * **Windows:** Always returns 'authorized' (loopback capture doesn't require permission).
 *
 * @returns 'authorized' | 'denied' | 'unknown'
 */
export function getSystemAudioPermissionStatus(): PermissionStatus {
  return loadAddon().getSystemAudioPermissionStatus()
}

/**
 * Check if the permission API is available.
 *
 * **macOS:** This uses private macOS APIs that may not be available on all systems.
 * **Windows:** Always returns true.
 */
export function isSystemAudioPermissionAvailable(): boolean {
  return loadAddon().isSystemAudioPermissionAvailable()
}

/**
 * Request system audio recording permission.
 *
 * **macOS:** This may not show a dialog - macOS doesn't have a standard permission
 * dialog for system audio. Users may need to manually add the app in System Settings.
 * **Windows:** Always resolves to true immediately (no permission needed).
 *
 * @returns Promise that resolves to true if permission was granted
 */
export function requestSystemAudioPermission(): Promise<boolean> {
  return new Promise((resolve) => {
    loadAddon().requestSystemAudioPermission((granted) => {
      resolve(granted)
    })
  })
}

// ============================================================================
// Microphone Permission
// ============================================================================

/**
 * Check the current microphone recording permission status.
 *
 * **macOS:** Uses the standard AVFoundation API.
 * **Windows:** Checks if microphone devices are accessible.
 *
 * @returns 'authorized' | 'denied' | 'unknown'
 */
export function getMicrophonePermissionStatus(): PermissionStatus {
  return loadAddon().getMicPermissionStatus()
}

/**
 * Request microphone recording permission.
 *
 * **macOS:** This will show the standard macOS permission dialog if permission hasn't been determined.
 * **Windows:** Windows 10+ may show a permission prompt automatically when accessing the mic.
 *
 * @returns Promise that resolves to true if permission was granted
 */
export function requestMicrophonePermission(): Promise<boolean> {
  return new Promise((resolve) => {
    loadAddon().requestMicPermission((granted) => {
      resolve(granted)
    })
  })
}

// ============================================================================
// Shared
// ============================================================================

/**
 * Open system settings to the appropriate audio/privacy pane.
 *
 * **macOS:** Opens System Settings to the Screen & System Audio Recording pane.
 * **Windows:** Opens Windows Sound Settings.
 *
 * @returns true if settings was opened successfully
 */
export function openSystemSettings(): boolean {
  return loadAddon().openSystemSettings()
}

/**
 * Error thrown when permission is required but not granted.
 */
export class PermissionError extends Error {
  public readonly status: PermissionStatus

  constructor(message: string, status: PermissionStatus) {
    super(message)
    this.name = 'PermissionError'
    this.status = status
  }
}

/**
 * Ensure system audio recording permission is granted.
 * If not authorized, will attempt to request permission or throw an error.
 * @throws PermissionError if permission is denied
 */
export async function ensureSystemAudioPermission(): Promise<void> {
  if (!isSystemAudioPermissionAvailable()) {
    // TCC API not available, can't check permission
    // Recording may work or fail silently
    return
  }

  const status = getSystemAudioPermissionStatus()

  if (status === 'authorized') {
    return
  }

  if (status === 'unknown') {
    // Try to request permission (may not show dialog)
    const granted = await requestSystemAudioPermission()
    if (granted) {
      return
    }
  }

  // Permission denied or request failed
  throw new PermissionError(
    'System audio recording permission is required. Please grant permission in System Settings.',
    getSystemAudioPermissionStatus()
  )
}

/**
 * Ensure microphone recording permission is granted.
 * If not authorized, will show the permission dialog or throw an error.
 * @throws PermissionError if permission is denied
 */
export async function ensureMicrophonePermission(): Promise<void> {
  const status = getMicrophonePermissionStatus()

  if (status === 'authorized') {
    return
  }

  if (status === 'unknown') {
    // Request permission - this will show the dialog
    const granted = await requestMicrophonePermission()
    if (granted) {
      return
    }
  }

  // Permission denied or request failed
  throw new PermissionError(
    'Microphone recording permission is required. Please grant permission in System Settings.',
    getMicrophonePermissionStatus()
  )
}
