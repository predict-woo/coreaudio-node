import { EventEmitter } from 'events'
import { getMicActivityMonitorNative } from './binding.js'
import { listAudioDevices } from './devices.js'
import type {
  MicrophoneActivityMonitorEvents,
  MicrophoneActivityMonitorOptions,
  MicActivityMonitorNativeClass,
  MicActivityNativeEvent,
  AudioDevice,
  AudioProcess,
} from './types.js'

const DEFAULT_POLL_INTERVAL = 100

/**
 * Monitors microphone usage by any application on the system.
 *
 * Uses native event listeners (Core Audio on macOS, WASAPI on Windows)
 * for efficient, event-driven detection.
 *
 * @example
 * ```typescript
 * import { MicrophoneActivityMonitor } from 'native-audio-node'
 *
 * const monitor = new MicrophoneActivityMonitor()
 *
 * monitor.on('change', (isActive) => {
 *   console.log(isActive ? 'Mic in use!' : 'Mic idle')
 * })
 *
 * monitor.start()
 *
 * // Check current state
 * console.log('Active:', monitor.isActive())
 *
 * // Later...
 * monitor.stop()
 * ```
 */
export class MicrophoneActivityMonitor {
  private events = new EventEmitter()
  private native: MicActivityMonitorNativeClass
  private running = false
  private pollInterval: ReturnType<typeof setInterval> | null = null
  private options: Required<MicrophoneActivityMonitorOptions>
  private deviceCache: Map<string, AudioDevice> = new Map()

  constructor(options?: MicrophoneActivityMonitorOptions) {
    const supportedPlatforms = ['darwin', 'win32']
    if (!supportedPlatforms.includes(process.platform)) {
      throw new Error(`native-audio-node only supports macOS and Windows. Current platform: ${process.platform}`)
    }

    this.options = {
      scope: options?.scope ?? 'all',
      fallbackPollInterval: options?.fallbackPollInterval ?? 2000,
    }

    const MicActivityMonitorNative = getMicActivityMonitorNative()
    this.native = new MicActivityMonitorNative()
  }

  on<K extends keyof MicrophoneActivityMonitorEvents>(event: K, listener: MicrophoneActivityMonitorEvents[K]): this {
    this.events.on(event, listener)
    return this
  }

  once<K extends keyof MicrophoneActivityMonitorEvents>(event: K, listener: MicrophoneActivityMonitorEvents[K]): this {
    this.events.once(event, listener)
    return this
  }

  off<K extends keyof MicrophoneActivityMonitorEvents>(event: K, listener: MicrophoneActivityMonitorEvents[K]): this {
    this.events.off(event, listener)
    return this
  }

  removeAllListeners<K extends keyof MicrophoneActivityMonitorEvents>(event?: K): this {
    this.events.removeAllListeners(event)
    return this
  }

  /**
   * Start monitoring microphone activity.
   * Registers native listeners for instant notifications.
   */
  start(): void {
    if (this.running) {
      return
    }

    this.refreshDeviceCache()
    this.native.start(this.options.scope)
    this.running = true
    this.startPolling()
  }

  /**
   * Stop monitoring and release resources.
   */
  stop(): void {
    if (!this.running) {
      return
    }

    this.stopPolling()
    this.processNativeEvents()
    this.native.stop()
    this.running = false
  }

  /**
   * Check current state synchronously.
   * @returns true if any microphone is currently in use
   */
  isActive(): boolean {
    return this.native.isActive()
  }

  /**
   * Get list of currently active input devices.
   * @returns Array of devices currently being used
   */
  getActiveDevices(): AudioDevice[] {
    const activeIds = this.native.getActiveDeviceIds()
    const devices: AudioDevice[] = []

    for (const id of activeIds) {
      const device = this.deviceCache.get(id)
      if (device) {
        devices.push(device)
      }
    }

    return devices
  }

  /**
   * Get list of processes currently using the microphone.
   *
   * **macOS:** Uses Core Audio's kAudioHardwarePropertyProcessObjectList API
   * to identify which applications are actively using microphone input.
   * Returns process name, PID, and bundle identifier.
   *
   * **Windows:** Uses WASAPI IAudioSessionManager2 to enumerate active capture
   * sessions on the default microphone device. Returns process name and PID.
   * Bundle ID is always empty (Windows doesn't have bundle identifiers).
   *
   * @returns Array of processes currently using the microphone
   *
   * @example
   * ```typescript
   * const processes = monitor.getActiveProcesses()
   * for (const proc of processes) {
   *   console.log(`${proc.name} (PID: ${proc.pid}) is using the mic`)
   * }
   * ```
   */
  getActiveProcesses(): AudioProcess[] {
    return this.native.getActiveProcesses()
  }

  /**
   * Check if the monitor is currently running.
   */
  isRunning(): boolean {
    return this.running
  }

  private refreshDeviceCache(): void {
    this.deviceCache.clear()
    const devices = listAudioDevices()
    for (const device of devices) {
      if (device.isInput) {
        this.deviceCache.set(device.id, device)
      }
    }
  }

  private startPolling(): void {
    this.pollInterval = setInterval(() => {
      if (this.running) {
        this.processNativeEvents()
      }
    }, DEFAULT_POLL_INTERVAL)
  }

  private stopPolling(): void {
    if (this.pollInterval) {
      clearInterval(this.pollInterval)
      this.pollInterval = null
    }
  }

  private processNativeEvents(): void {
    const events = this.native.processEvents()

    for (const event of events) {
      this.handleNativeEvent(event)
    }
  }

  private handleNativeEvent(event: MicActivityNativeEvent): void {
    switch (event.type) {
      case 0: // change
        if (event.isActive !== undefined) {
          const processes = this.native.getActiveProcesses()
          this.events.emit('change', event.isActive, processes)
        }
        break

      case 1: // deviceChange
        if (event.deviceId && event.isActive !== undefined) {
          let device = this.deviceCache.get(event.deviceId)
          if (!device && event.deviceName) {
            device = this.createMinimalDevice(event.deviceId, event.deviceName)
            this.deviceCache.set(event.deviceId, device)
          }
          if (device) {
            this.events.emit('deviceChange', device, event.isActive)
          }
        }
        break

      case 2: // error
        this.events.emit('error', new Error(event.message || 'Unknown error'))
        break
    }
  }

  private createMinimalDevice(id: string, name: string): AudioDevice {
    return {
      id,
      name,
      isDefault: false,
      isInput: true,
      isOutput: false,
      sampleRate: 0,
      channelCount: 0,
    }
  }
}
