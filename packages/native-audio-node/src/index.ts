// Recorder classes
export { SystemAudioRecorder } from './system-audio-recorder.js'
export { MicrophoneRecorder } from './microphone-recorder.js'

// Microphone activity monitoring
export { MicrophoneActivityMonitor } from './microphone-activity-monitor.js'

// Device enumeration
export { listAudioDevices, getDefaultInputDevice, getDefaultOutputDevice } from './devices.js'

// Types
export type {
  AudioRecorderOptions,
  SystemAudioRecorderOptions,
  MicrophoneRecorderOptions,
  MicrophoneActivityMonitorOptions,
  MicrophoneActivityMonitorEvents,
  AudioChunk,
  AudioMetadata,
  AudioDevice,
  AudioRecorderEvents,
} from './types.js'

// Permission API
export {
  // System audio permission
  getSystemAudioPermissionStatus,
  isSystemAudioPermissionAvailable,
  requestSystemAudioPermission,
  ensureSystemAudioPermission,
  // Microphone permission
  getMicrophonePermissionStatus,
  requestMicrophonePermission,
  ensureMicrophonePermission,
  // Shared
  openSystemSettings,
  PermissionError,
} from './permission.js'
export type { PermissionStatus } from './permission.js'
