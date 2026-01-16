import { BaseAudioRecorder } from './base-recorder.js'
import type { MicrophoneRecorderOptions } from './types.js'

/**
 * Captures microphone audio on macOS using Core Audio.
 * Requires macOS 14.2+ and microphone recording permission.
 *
 * @example
 * ```typescript
 * import { MicrophoneRecorder, listAudioDevices } from 'coreaudio-node'
 *
 * // List available input devices
 * const devices = listAudioDevices().filter(d => d.isInput)
 * console.log('Available microphones:', devices)
 *
 * // Record from default microphone
 * const recorder = new MicrophoneRecorder({
 *   sampleRate: 16000,
 *   chunkDurationMs: 100,
 *   gain: 0.8,
 * })
 *
 * recorder.on('data', (chunk) => {
 *   console.log(`Received ${chunk.data.length} bytes`)
 * })
 *
 * await recorder.start()
 * // ... record audio
 * await recorder.stop()
 * ```
 */
export class MicrophoneRecorder extends BaseAudioRecorder {
  private options: MicrophoneRecorderOptions

  constructor(options: MicrophoneRecorderOptions = {}) {
    super()
    this.options = options
  }

  /**
   * Start capturing microphone audio.
   * @throws Error if already running or if permission is denied
   */
  start(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.running) {
        reject(new Error('MicrophoneRecorder is already running'))
        return
      }

      try {
        this.native.startMicrophone({
          sampleRate: this.options.sampleRate,
          chunkDurationMs: this.options.chunkDurationMs,
          stereo: this.options.stereo,
          emitSilence: this.options.emitSilence ?? true,
          deviceId: this.options.deviceId,
          gain: this.options.gain,
        })

        this.running = true
        this.startPolling()
        resolve()
      } catch (error) {
        reject(error)
      }
    })
  }
}
