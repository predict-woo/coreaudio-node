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
