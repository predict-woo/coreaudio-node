# native-audio-node

Native audio capture for Node.js - system audio and microphone recording for **macOS** and **Windows**.

A TypeScript-first library that provides low-latency access to native audio APIs for capturing both system audio output (everything playing through speakers/headphones) and microphone input.

## Features

- **System Audio Capture** - Record all audio playing on your system, or filter by specific processes
- **Microphone Recording** - Capture from any audio input device with gain control
- **Microphone Activity Monitoring** - Detect when any app uses the microphone, with process identification
- **Cross-Platform** - Native support for macOS (Core Audio) and Windows (WASAPI)
- **Low Latency** - 10ms event polling for real-time audio processing
- **Sample Rate Conversion** - Built-in resampling to common rates (8kHz-48kHz)
- **Process Filtering** - Include or exclude specific application audio
- **Device Selection** - Choose from available input devices programmatically
- **TypeScript Native** - Full type definitions and ESM-first design
- **Zero Build Dependencies** - Pre-built binaries for all supported platforms

## Platform Support

| Feature | macOS | Windows |
|---------|-------|---------|
| System audio capture | ✅ | ✅ |
| Microphone capture | ✅ | ✅ |
| Microphone activity monitoring | ✅ | ✅ |
| Identify processes using mic | ✅ | ❌ |
| Process filtering (include) | ✅ Multiple PIDs | ✅ Single PID* |
| Process filtering (exclude) | ✅ Multiple PIDs | ✅ Single PID* |
| Mute captured processes | ✅ | ❌ |
| Continuous audio stream | ✅ Always | ✅ Via `emitSilence`** |
| Sample rate conversion | ✅ | ✅ |
| Device enumeration | ✅ | ✅ |

*Windows limitation: WASAPI process loopback only supports a single process ID per capture stream.

**macOS always emits continuous audio data (silence when nothing plays). Windows WASAPI only emits when audio is playing, but `emitSilence: true` (default) generates silent buffers to match macOS behavior.

## Requirements

### macOS
- **macOS 14.2+** (Sonoma or later)
- **Node.js 20+**

### Windows
- **Windows 10 2004+** (Build 19041 or later)
- **Node.js 20+**

## Installation

```bash
npm install native-audio-node
```

Pre-built binaries are automatically installed for your platform. No build tools required!

## Quick Start

### Recording System Audio

```typescript
import { SystemAudioRecorder } from 'native-audio-node'

const recorder = new SystemAudioRecorder({
  sampleRate: 16000,      // Resample to 16kHz
  chunkDurationMs: 100,   // 100ms audio chunks
  mute: false,            // Don't mute system audio (macOS only)
})

recorder.on('metadata', (meta) => {
  console.log(`Format: ${meta.sampleRate}Hz, ${meta.bitsPerChannel}bit`)
})

recorder.on('data', (chunk) => {
  // chunk.data is a Buffer containing raw PCM audio
  console.log(`Received ${chunk.data.length} bytes`)
})

await recorder.start()

// Record for 10 seconds
setTimeout(async () => {
  await recorder.stop()
}, 10000)
```

### Recording Microphone

```typescript
import { MicrophoneRecorder, listAudioDevices } from 'native-audio-node'

// List available input devices
const devices = listAudioDevices().filter(d => d.isInput)
console.log('Available microphones:', devices.map(d => d.name))

const recorder = new MicrophoneRecorder({
  sampleRate: 16000,
  gain: 0.8,              // 80% gain (0.0-2.0)
  deviceId: devices[0].id // Optional: specific device
})

recorder.on('data', (chunk) => {
  // Process microphone audio
})

await recorder.start()
```

## API Reference

### Classes

#### `SystemAudioRecorder`

Captures system audio output (everything playing through speakers/headphones).

```typescript
import { SystemAudioRecorder } from 'native-audio-node'

const recorder = new SystemAudioRecorder(options?: SystemAudioRecorderOptions)
```

**Options:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `sampleRate` | `number` | Device native | Target sample rate (8000, 16000, 22050, 24000, 32000, 44100, 48000) |
| `chunkDurationMs` | `number` | `200` | Audio chunk duration in milliseconds (0-5000) |
| `stereo` | `boolean` | `false` | Record in stereo (true) or mono (false) |
| `mute` | `boolean` | `false` | Mute system audio while recording (**macOS only**) |
| `emitSilence` | `boolean` | `true` | Emit silent chunks when no audio is playing (**Windows only** - macOS always emits) |
| `includeProcesses` | `number[]` | - | Only capture audio from these process IDs (Windows: first PID only) |
| `excludeProcesses` | `number[]` | - | Exclude audio from these process IDs (Windows: first PID only) |

**Methods:**

| Method | Returns | Description |
|--------|---------|-------------|
| `start()` | `Promise<void>` | Start audio capture |
| `stop()` | `Promise<void>` | Stop audio capture |
| `isActive()` | `boolean` | Check if currently recording |
| `getMetadata()` | `AudioMetadata \| null` | Get current audio format info |

---

#### `MicrophoneRecorder`

Captures audio from microphone input devices.

```typescript
import { MicrophoneRecorder } from 'native-audio-node'

const recorder = new MicrophoneRecorder(options?: MicrophoneRecorderOptions)
```

**Options:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `sampleRate` | `number` | Device native | Target sample rate |
| `chunkDurationMs` | `number` | `200` | Audio chunk duration in milliseconds |
| `stereo` | `boolean` | `false` | Record in stereo or mono |
| `emitSilence` | `boolean` | `true` | Emit silent chunks when no audio (**Windows only** - macOS always emits) |
| `deviceId` | `string` | System default | Device UID (from `listAudioDevices()`) |
| `gain` | `number` | `1.0` | Microphone gain (0.0-2.0) |

---

#### `MicrophoneActivityMonitor`

Monitors microphone usage by any application on the system. Detects when apps start/stop using the microphone and identifies which processes are recording.

```typescript
import { MicrophoneActivityMonitor } from 'native-audio-node'

const monitor = new MicrophoneActivityMonitor(options?: MicrophoneActivityMonitorOptions)
```

**Options:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `scope` | `'all' \| 'default'` | `'all'` | Monitor all input devices or only the default |
| `fallbackPollInterval` | `number` | `2000` | Polling interval in ms when native events unavailable |

**Methods:**

| Method | Returns | Description |
|--------|---------|-------------|
| `start()` | `void` | Start monitoring microphone activity |
| `stop()` | `void` | Stop monitoring and release resources |
| `isActive()` | `boolean` | Check if any microphone is currently in use |
| `isRunning()` | `boolean` | Check if the monitor is currently running |
| `getActiveDevices()` | `AudioDevice[]` | Get list of devices currently being used |
| `getActiveProcesses()` | `AudioProcess[]` | Get list of processes using the microphone (**macOS only**) |

**Events:**

```typescript
interface MicrophoneActivityMonitorEvents {
  change: (isActive: boolean, processes: AudioProcess[]) => void
  deviceChange: (device: AudioDevice, isActive: boolean) => void
  error: (error: Error) => void
}
```

| Event | Payload | Description |
|-------|---------|-------------|
| `change` | `isActive`, `processes` | Aggregate mic activity changed; includes processes on macOS |
| `deviceChange` | `device`, `isActive` | Specific device activity changed |
| `error` | `Error` | An error occurred during monitoring |

**Example:**

```typescript
import { MicrophoneActivityMonitor } from 'native-audio-node'

const monitor = new MicrophoneActivityMonitor()

monitor.on('change', (isActive, processes) => {
  if (isActive) {
    console.log('Microphone in use!')
    // On macOS, processes contains apps using the mic
    for (const proc of processes) {
      console.log(`  ${proc.name} (PID: ${proc.pid})`)
    }
  } else {
    console.log('Microphone idle')
  }
})

monitor.on('deviceChange', (device, isActive) => {
  console.log(`${device.name}: ${isActive ? 'active' : 'inactive'}`)
})

monitor.start()

// Query current state anytime
console.log('Is mic active?', monitor.isActive())
console.log('Active processes:', monitor.getActiveProcesses())

// Later...
monitor.stop()
```

---

### Events

Both recorder classes emit the following events:

```typescript
interface AudioRecorderEvents {
  data: (chunk: AudioChunk) => void
  metadata: (metadata: AudioMetadata) => void
  start: () => void
  stop: () => void
  error: (error: Error) => void
}
```

| Event | Payload | Description |
|-------|---------|-------------|
| `data` | `AudioChunk` | Raw PCM audio data chunk |
| `metadata` | `AudioMetadata` | Audio format information (emitted once after start) |
| `start` | - | Recording has started |
| `stop` | - | Recording has stopped |
| `error` | `Error` | An error occurred |

---

### Types

#### `AudioChunk`

```typescript
interface AudioChunk {
  data: Buffer  // Raw PCM audio bytes
}
```

#### `AudioMetadata`

```typescript
interface AudioMetadata {
  sampleRate: number        // Hz (e.g., 48000, 16000)
  channelsPerFrame: number  // 1 (mono) or 2 (stereo)
  bitsPerChannel: number    // 32 (float) or 16 (int)
  isFloat: boolean          // true = 32-bit float, false = 16-bit int
  encoding: string          // "pcm_f32le" or "pcm_s16le"
}
```

#### `AudioDevice`

```typescript
interface AudioDevice {
  id: string                // Unique device identifier
  name: string              // Human-readable name
  manufacturer?: string     // Device manufacturer
  isDefault: boolean        // Is system default device
  isInput: boolean          // Supports input (microphone)
  isOutput: boolean         // Supports output (speakers)
  sampleRate: number        // Native sample rate
  channelCount: number      // Number of channels
}
```

#### `AudioProcess`

Information about a process using audio input (**macOS only**).

```typescript
interface AudioProcess {
  pid: number       // Process ID
  name: string      // Process name (e.g., "Zoom", "node")
  bundleId: string  // macOS bundle identifier (e.g., "us.zoom.xos")
}
```

---

### Device Management

```typescript
import {
  listAudioDevices,
  getDefaultInputDevice,
  getDefaultOutputDevice
} from 'native-audio-node'

// List all audio devices
const devices = listAudioDevices()

// Get input devices only
const microphones = devices.filter(d => d.isInput)

// Get output devices only
const speakers = devices.filter(d => d.isOutput)

// Get default devices
const defaultMic = getDefaultInputDevice()      // Returns device UID or null
const defaultSpeaker = getDefaultOutputDevice() // Returns device UID or null
```

---

### Permission Management

#### Platform Differences

| Permission | macOS | Windows |
|------------|-------|---------|
| System Audio | Requires TCC permission | No permission needed |
| Microphone | Requires user consent | May prompt via Windows Privacy |

#### System Audio Permission

```typescript
import {
  getSystemAudioPermissionStatus,
  isSystemAudioPermissionAvailable,
  requestSystemAudioPermission,
  ensureSystemAudioPermission,
  openSystemSettings,
  PermissionError,
} from 'native-audio-node'

// Check current status
// macOS: Returns 'unknown', 'denied', or 'authorized'
// Windows: Always returns 'authorized'
const status = getSystemAudioPermissionStatus()

// Open system settings (macOS: Privacy pane, Windows: Sound settings)
openSystemSettings()

// Ensure permission is granted (throws PermissionError if denied on macOS)
try {
  await ensureSystemAudioPermission()
  // Permission granted, safe to start recording
} catch (err) {
  if (err instanceof PermissionError) {
    console.log('Permission status:', err.status)
  }
}
```

#### Microphone Permission

```typescript
import {
  getMicrophonePermissionStatus,
  requestMicrophonePermission,
  ensureMicrophonePermission,
  PermissionError,
} from 'native-audio-node'

// Check current status
const status = getMicrophonePermissionStatus()

// Request permission (shows system dialog if needed)
const granted = await requestMicrophonePermission()

// Or use convenience function
try {
  await ensureMicrophonePermission()
} catch (err) {
  if (err instanceof PermissionError) {
    console.log('Microphone access denied')
  }
}
```

---

## Examples

### Process-Specific Recording

```typescript
import { SystemAudioRecorder } from 'native-audio-node'

// Record only from a specific process
const recorder = new SystemAudioRecorder({
  includeProcesses: [12345],  // Process ID
  // Note: On Windows, only the first PID is used
})

await recorder.start()
```

### Real-time Audio Processing

```typescript
import { MicrophoneRecorder } from 'native-audio-node'

const recorder = new MicrophoneRecorder({
  sampleRate: 16000,
  chunkDurationMs: 50,  // 50ms chunks for low latency
})

recorder.on('data', (chunk) => {
  // Calculate RMS (volume level)
  const samples = new Int16Array(chunk.data.buffer)
  let sum = 0
  for (const sample of samples) {
    sum += sample * sample
  }
  const rms = Math.sqrt(sum / samples.length)
  const db = 20 * Math.log10(rms / 32768)
  console.log(`Volume: ${db.toFixed(1)} dB`)
})

await recorder.start()
```

### Microphone Activity Monitoring

Detect when any application uses the microphone:

```typescript
import { MicrophoneActivityMonitor } from 'native-audio-node'

const monitor = new MicrophoneActivityMonitor()

monitor.on('change', (isActive, processes) => {
  if (isActive) {
    console.log('🎤 Microphone in use!')
    // macOS: identify which apps are using the mic
    if (processes.length > 0) {
      console.log('Apps using mic:')
      for (const proc of processes) {
        console.log(`  - ${proc.name} (PID: ${proc.pid})`)
      }
    }
  } else {
    console.log('🔇 Microphone idle')
  }
})

monitor.start()

// Check state programmatically
setInterval(() => {
  const processes = monitor.getActiveProcesses()
  if (processes.length > 0) {
    console.log('Currently recording:', processes.map(p => p.name).join(', '))
  }
}, 5000)
```

---

## Troubleshooting

### macOS: "System audio recording permission not configured"

1. Open **System Settings > Privacy & Security > Screen & System Audio Recording**
2. Scroll to **"System Audio Recording Only"** section
3. Add and enable your terminal app
4. Restart the terminal app

### macOS: "Microphone permission denied"

1. Open **System Settings > Privacy & Security > Microphone**
2. Enable access for your terminal app
3. Restart the app if needed

### Windows: No audio captured

1. Ensure Windows 10 version 2004 or later
2. Check **Settings > Privacy > Microphone** for mic access
3. For system audio, no permissions are needed

---

## Architecture

```
TypeScript API
     |
BaseAudioRecorder (EventEmitter, 10ms polling)
     |
Native NAPI Wrapper (C++)
     |
+--------------------+--------------------+
|      macOS         |      Windows       |
|   (Swift Bridge)   |    (WASAPI C++)    |
+--------------------+--------------------+
     |                        |
+--------------------+--------------------+
|  AudioTapManager   |  WasapiCapture     |
|  MicrophoneCapture |  (Loopback/Mic)    |
+--------------------+--------------------+
     |                        |
Core Audio / AVFoundation    WASAPI
```

---

## License

MIT
