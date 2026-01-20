// Common audio chunk structure
export interface AudioChunk {
  data: Buffer
}

// Audio metadata from native layer
export interface AudioMetadata {
  sampleRate: number
  channelsPerFrame: number
  bitsPerChannel: number
  isFloat: boolean
  encoding: string
}

// Common options shared by all recorder types
export interface AudioRecorderOptions {
  sampleRate?: number
  chunkDurationMs?: number
  stereo?: boolean
  /**
   * Emit silent audio chunks when no audio is playing.
   * 
   * **macOS:** Always emits continuous audio (this option has no effect).
   * **Windows:** By default (true), generates silent buffers to match macOS behavior.
   * Set to false for efficiency if you only want events when audio is actually playing.
   * 
   * @default true
   */
  emitSilence?: boolean
}

// System audio specific options
export interface SystemAudioRecorderOptions extends AudioRecorderOptions {
  /**
   * Mute the captured processes' audio output.
   * **macOS only** - This option has no effect on Windows.
   */
  mute?: boolean
  /**
   * Only capture audio from these process IDs.
   * **Note:** On Windows, only the first process ID is used (OS limitation).
   */
  includeProcesses?: number[]
  /**
   * Capture audio from all processes except these process IDs.
   * **Note:** On Windows, only the first process ID is used (OS limitation).
   */
  excludeProcesses?: number[]
}

// Microphone specific options
export interface MicrophoneRecorderOptions extends AudioRecorderOptions {
  deviceId?: string
  gain?: number
}

// Audio device information
export interface AudioDevice {
  id: string
  name: string
  manufacturer?: string
  isDefault: boolean
  isInput: boolean
  isOutput: boolean
  sampleRate: number
  channelCount: number
}

// Events shared by all recorder types
export interface AudioRecorderEvents {
  data: (chunk: AudioChunk) => void
  metadata: (metadata: AudioMetadata) => void
  start: () => void
  stop: () => void
  error: (error: Error) => void
}

// Native addon event interface (internal)
export interface NativeEvent {
  type: number // 0=data, 1=start, 2=stop, 3=error, 4=metadata
  data?: Buffer
  message?: string
  sampleRate?: number
  channelsPerFrame?: number
  bitsPerChannel?: number
  isFloat?: boolean
  encoding?: string
}

// ============================================================================
// Microphone Activity Monitor Types
// ============================================================================

/**
 * Options for MicrophoneActivityMonitor.
 */
export interface MicrophoneActivityMonitorOptions {
  /**
   * Which devices to monitor.
   * - 'all': Monitor all input devices (default)
   * - 'default': Only monitor the system default input device
   * @default 'all'
   */
  scope?: 'all' | 'default'

  /**
   * Polling fallback interval in milliseconds.
   * Used when native event listeners are unavailable.
   * Set to 0 to disable fallback polling.
   * @default 2000
   */
  fallbackPollInterval?: number
}

/**
 * Events emitted by MicrophoneActivityMonitor.
 */
export interface MicrophoneActivityMonitorEvents {
  /**
   * Emitted when the aggregate microphone activity state changes.
   * `isActive` is true if ANY monitored microphone is currently in use.
   */
  change: (isActive: boolean) => void

  /**
   * Emitted when a specific device's activity state changes.
   * Provides granular per-device tracking.
   */
  deviceChange: (device: AudioDevice, isActive: boolean) => void

  /**
   * Emitted when an error occurs during monitoring.
   */
  error: (error: Error) => void
}

/**
 * Native event from mic activity monitor.
 * @internal
 */
export interface MicActivityNativeEvent {
  type: number // 0=change, 1=deviceChange, 2=error
  isActive?: boolean
  deviceId?: string
  deviceName?: string
  message?: string
}

/**
 * Native mic activity monitor class interface.
 * @internal
 */
export interface MicActivityMonitorNativeClass {
  start(scope: string): void
  stop(): void
  isActive(): boolean
  getActiveDeviceIds(): string[]
  processEvents(): MicActivityNativeEvent[]
}

/**
 * Native mic activity monitor constructor.
 * @internal
 */
export interface MicActivityMonitorNativeConstructor {
  new (): MicActivityMonitorNativeClass
}

// Native addon interface (internal)
export interface AudioRecorderNativeClass {
  startSystemAudio(options: {
    sampleRate?: number
    chunkDurationMs?: number
    mute?: boolean
    stereo?: boolean
    emitSilence?: boolean
    includeProcesses?: number[]
    excludeProcesses?: number[]
  }): void
  startMicrophone(options: {
    sampleRate?: number
    chunkDurationMs?: number
    stereo?: boolean
    emitSilence?: boolean
    deviceId?: string
    gain?: number
  }): void
  stop(): void
  isRunning(): boolean
  processEvents(): NativeEvent[]
}

export interface AudioRecorderNativeConstructor {
  new (): AudioRecorderNativeClass
}
