import { EventEmitter } from 'events'
import { createRequire } from 'module'
import path from 'path'
import { fileURLToPath } from 'url'
import type {
  AudioRecorderEvents,
  AudioChunk,
  AudioMetadata,
  AudioRecorderNativeClass,
  AudioRecorderNativeConstructor,
} from './types.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const require = createRequire(import.meta.url)

let AudioRecorderNative: AudioRecorderNativeConstructor

function loadNativeAddon(): AudioRecorderNativeConstructor {
  const paths = [
    // Development: build/Release relative to dist/
    path.join(__dirname, '..', 'build', 'Release', 'native_audio.node'),
    // Installed package: build/Release at package root
    path.join(__dirname, '..', '..', 'build', 'Release', 'native_audio.node'),
  ]

  for (const addonPath of paths) {
    try {
      const addon = require(addonPath)
      return addon.AudioRecorderNative
    } catch {
      // Try next path
    }
  }

  throw new Error(
    `Failed to load native audio addon. Make sure you've built the native module with 'npm run build:native'.`
  )
}

AudioRecorderNative = loadNativeAddon()

/**
 * Abstract base class for audio recorders.
 * Provides shared EventEmitter functionality, polling, and lifecycle management.
 */
export abstract class BaseAudioRecorder {
  protected events = new EventEmitter()
  protected native: AudioRecorderNativeClass
  protected running = false
  protected pollInterval: ReturnType<typeof setInterval> | null = null
  protected metadata: AudioMetadata | null = null

  constructor() {
    // Check platform at construction time
    const supportedPlatforms = ['darwin', 'win32']
    if (!supportedPlatforms.includes(process.platform)) {
      throw new Error(
        `native-audio-node only supports macOS and Windows. Current platform: ${process.platform}`
      )
    }

    this.native = new AudioRecorderNative()
  }

  on<K extends keyof AudioRecorderEvents>(event: K, listener: AudioRecorderEvents[K]): this {
    this.events.on(event, listener)
    return this
  }

  once<K extends keyof AudioRecorderEvents>(event: K, listener: AudioRecorderEvents[K]): this {
    this.events.once(event, listener)
    return this
  }

  off<K extends keyof AudioRecorderEvents>(event: K, listener: AudioRecorderEvents[K]): this {
    this.events.off(event, listener)
    return this
  }

  removeAllListeners<K extends keyof AudioRecorderEvents>(event?: K): this {
    this.events.removeAllListeners(event)
    return this
  }

  protected emit<K extends keyof AudioRecorderEvents>(
    event: K,
    ...args: Parameters<AudioRecorderEvents[K]>
  ): boolean {
    return this.events.emit(event, ...args)
  }

  protected processNativeEvents(): void {
    const events = this.native.processEvents()

    for (const event of events) {
      switch (event.type) {
        case 0: // data
          if (event.data) {
            const chunk: AudioChunk = { data: event.data }
            this.emit('data', chunk)
          }
          break

        case 1: // start
          this.emit('start')
          break

        case 2: // stop
          this.emit('stop')
          break

        case 3: // error
          this.emit('error', new Error(event.message || 'Unknown error'))
          break

        case 4: // metadata
          this.metadata = {
            sampleRate: event.sampleRate!,
            channelsPerFrame: event.channelsPerFrame!,
            bitsPerChannel: event.bitsPerChannel!,
            isFloat: event.isFloat!,
            encoding: event.encoding!,
          }
          this.emit('metadata', this.metadata)
          break
      }
    }
  }

  protected startPolling(): void {
    // Start polling for events from the native addon
    // Use a fast interval to ensure low latency for audio data
    this.pollInterval = setInterval(() => {
      if (this.running) {
        this.processNativeEvents()
      }
    }, 10) // Poll every 10ms for responsive audio
  }

  protected stopPolling(): void {
    if (this.pollInterval) {
      clearInterval(this.pollInterval)
      this.pollInterval = null
    }
  }

  /**
   * Start audio capture. Must be implemented by subclasses.
   */
  abstract start(): Promise<void>

  /**
   * Stop audio capture.
   */
  stop(): Promise<void> {
    return new Promise((resolve) => {
      if (!this.running) {
        resolve()
        return
      }

      // Stop the polling interval
      this.stopPolling()

      // Process any remaining events
      this.processNativeEvents()

      // Stop the native addon
      this.native.stop()
      this.running = false

      resolve()
    })
  }

  /**
   * Check if the recorder is currently active.
   */
  isActive(): boolean {
    return this.running
  }

  /**
   * Get the current audio metadata.
   * Returns null if recording hasn't started or metadata hasn't been received yet.
   */
  getMetadata(): AudioMetadata | null {
    return this.metadata
  }
}
