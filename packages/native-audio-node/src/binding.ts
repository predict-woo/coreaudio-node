import { createRequire } from 'module'
import type {
  AudioDevice,
  AudioRecorderNativeConstructor,
  MicActivityMonitorNativeConstructor,
} from './types.js'

const require = createRequire(import.meta.url)

export type PermissionStatus = 'unknown' | 'denied' | 'authorized'

export interface NativeAddon {
  // Audio recorder class
  AudioRecorderNative: AudioRecorderNativeConstructor

  // Mic activity monitor class
  MicActivityMonitorNative: MicActivityMonitorNativeConstructor

  // Device management
  listDevices(): AudioDevice[]
  getDefaultInputDevice(): string | null
  getDefaultOutputDevice(): string | null

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

let cachedBinding: NativeAddon | null = null

/**
 * Get the platform-specific package name for the current OS and architecture.
 */
function getPlatformPackage(): string {
  const platform = process.platform
  const arch = process.arch

  // Map Node.js platform/arch to our package names
  const platformMap: Record<string, Record<string, string>> = {
    darwin: {
      arm64: '@native-audio-node/darwin-arm64',
      x64: '@native-audio-node/darwin-x64',
    },
    win32: {
      x64: '@native-audio-node/win32-x64',
      arm64: '@native-audio-node/win32-arm64',
    },
  }

  const platformPackages = platformMap[platform]
  if (!platformPackages) {
    throw new Error(
      `Unsupported platform: ${platform}. native-audio-node only supports macOS and Windows.`
    )
  }

  const packageName = platformPackages[arch]
  if (!packageName) {
    throw new Error(
      `Unsupported architecture: ${arch} on ${platform}. ` +
        `Supported architectures: ${Object.keys(platformPackages).join(', ')}`
    )
  }

  return packageName
}

/**
 * Load the native addon from the platform-specific package.
 * This function is called lazily and caches the result.
 */
export function loadBinding(): NativeAddon {
  if (cachedBinding) {
    return cachedBinding
  }

  const packageName = getPlatformPackage()

  try {
    // The platform package exports the .node binary path or the addon itself
    cachedBinding = require(packageName) as NativeAddon
    return cachedBinding
  } catch (error) {
    const err = error as Error & { code?: string }

    if (err.code === 'MODULE_NOT_FOUND') {
      throw new Error(
        `Could not find native module for ${process.platform}-${process.arch}.\n` +
          `Expected package: ${packageName}\n\n` +
          `This usually means:\n` +
          `1. The package wasn't installed correctly. Try: npm install native-audio-node\n` +
          `2. Your platform/architecture combination is not supported.\n` +
          `3. You're using an older version of npm that doesn't support optionalDependencies properly.\n\n` +
          `Supported platforms:\n` +
          `  - macOS: arm64 (Apple Silicon), x64 (Intel)\n` +
          `  - Windows: x64, arm64`
      )
    }

    throw new Error(
      `Failed to load native module from ${packageName}: ${err.message}`
    )
  }
}

/**
 * Get the AudioRecorderNative constructor from the native addon.
 */
export function getAudioRecorderNative(): AudioRecorderNativeConstructor {
  return loadBinding().AudioRecorderNative
}

/**
 * Get the MicActivityMonitorNative constructor from the native addon.
 */
export function getMicActivityMonitorNative(): MicActivityMonitorNativeConstructor {
  return loadBinding().MicActivityMonitorNative
}
