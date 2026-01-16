import { createRequire } from 'module'
import path from 'path'
import { fileURLToPath } from 'url'
import type { AudioDevice } from './types.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const require = createRequire(import.meta.url)

interface NativeAddon {
  listDevices(): AudioDevice[]
  getDefaultInputDevice(): string | null
  getDefaultOutputDevice(): string | null
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

/**
 * List all audio devices on the system.
 * Returns both input and output devices with their properties.
 *
 * @example
 * ```typescript
 * import { listAudioDevices } from 'coreaudio-node'
 *
 * const devices = listAudioDevices()
 * const inputDevices = devices.filter(d => d.isInput)
 * console.log('Available microphones:', inputDevices.map(d => d.name))
 * ```
 */
export function listAudioDevices(): AudioDevice[] {
  return loadAddon().listDevices()
}

/**
 * Get the UID of the default input device (microphone).
 * @returns Device UID string, or null if no default input device
 */
export function getDefaultInputDevice(): string | null {
  return loadAddon().getDefaultInputDevice()
}

/**
 * Get the UID of the default output device (speakers).
 * @returns Device UID string, or null if no default output device
 */
export function getDefaultOutputDevice(): string | null {
  return loadAddon().getDefaultOutputDevice()
}
