import { BaseAudioRecorder } from './base-recorder.js'
import type { SystemAudioRecorderOptions } from './types.js'

/**
 * Captures system audio on macOS using Core Audio process taps.
 * Requires macOS 14.2+ and system audio recording permission.
 *
 * @example
 * ```typescript
 * import { SystemAudioRecorder } from 'coreaudio-node'
 *
 * const recorder = new SystemAudioRecorder({
 *   sampleRate: 16000,
 *   chunkDurationMs: 100,
 *   mute: true,
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
export class SystemAudioRecorder extends BaseAudioRecorder {
  private options: SystemAudioRecorderOptions

  constructor(options: SystemAudioRecorderOptions = {}) {
    super()
    this.options = options
  }

  /**
   * Start capturing system audio.
   * @throws Error if already running or if permission is denied
   */
  start(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.running) {
        reject(new Error('SystemAudioRecorder is already running'))
        return
      }

      try {
        this.native.startSystemAudio({
          sampleRate: this.options.sampleRate,
          chunkDurationMs: this.options.chunkDurationMs,
          mute: this.options.mute,
          stereo: this.options.stereo,
          emitSilence: this.options.emitSilence ?? true,
          includeProcesses: this.options.includeProcesses,
          excludeProcesses: this.options.excludeProcesses,
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
