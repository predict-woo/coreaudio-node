#!/usr/bin/env node

import { SystemAudioRecorder, getSystemAudioPermissionStatus, isSystemAudioPermissionAvailable, openSystemSettings } from '../dist/index.js'
import { writeFileSync } from 'fs'
import { parseArgs } from 'util'

// Parse command line arguments
const { values } = parseArgs({
  options: {
    output: { type: 'string', short: 'o', default: 'recording.wav' },
    duration: { type: 'string', short: 'd', default: '10' },
    'sample-rate': { type: 'string', short: 's' },
    mute: { type: 'boolean', short: 'm', default: false },
    stereo: { type: 'boolean', default: false },
    help: { type: 'boolean', short: 'h', default: false },
  },
  allowPositionals: true,
})

if (values.help) {
  console.log(`
record-wav - Record system audio to WAV file

Usage: node record-wav.mjs [options]

Options:
  -o, --output <file>      Output WAV file (default: recording.wav)
  -d, --duration <seconds> Recording duration in seconds (default: 10)
  -s, --sample-rate <rate> Target sample rate (8000, 16000, 44100, 48000)
  -m, --mute               Mute audio while recording
      --stereo             Record in stereo (default: mono)
  -h, --help               Show this help message

Examples:
  node record-wav.mjs -o output.wav -d 5
  node record-wav.mjs -s 16000 -d 30 -m
`)
  process.exit(0)
}

const outputFile = values.output
const duration = parseInt(values.duration, 10) * 1000 // Convert to ms
const sampleRate = values['sample-rate'] ? parseInt(values['sample-rate'], 10) : undefined
const mute = values.mute
const stereo = values.stereo

console.log(`Recording to: ${outputFile}`)
console.log(`Duration: ${duration / 1000} seconds`)
if (sampleRate) console.log(`Sample rate: ${sampleRate} Hz`)
console.log(`Mute: ${mute}`)
console.log(`Channels: ${stereo ? 'stereo' : 'mono'}`)
console.log('')

// Buffer to collect all audio data
const audioChunks = []
let metadata = null

const recorder = new SystemAudioRecorder({
  sampleRate,
  chunkDurationMs: 100,
  mute,
  stereo,
})

recorder.on('metadata', (meta) => {
  metadata = meta
  console.log(
    `Audio format: ${meta.sampleRate}Hz, ${meta.channelsPerFrame}ch, ${meta.bitsPerChannel}bit ${
      meta.isFloat ? 'float' : 'int'
    }`
  )
})

recorder.on('start', () => {
  console.log('Recording started...')
})

recorder.on('data', (chunk) => {
  audioChunks.push(chunk.data)
  // Show progress
  const totalBytes = audioChunks.reduce((sum, c) => sum + c.length, 0)
  const seconds = totalBytes / (metadata.sampleRate * metadata.channelsPerFrame * (metadata.bitsPerChannel / 8))
  process.stdout.write(`\rRecorded: ${seconds.toFixed(1)}s`)
})

recorder.on('error', (error) => {
  console.error('\nError:', error.message)
  process.exit(1)
})

// Create WAV header
function createWavHeader(dataLength, sampleRate, channels, bitsPerSample, isFloat) {
  const byteRate = sampleRate * channels * (bitsPerSample / 8)
  const blockAlign = channels * (bitsPerSample / 8)

  // Determine audio format: 1 = PCM, 3 = IEEE float
  const audioFormat = isFloat ? 3 : 1

  const header = Buffer.alloc(44)

  // RIFF header
  header.write('RIFF', 0)
  header.writeUInt32LE(36 + dataLength, 4) // File size - 8
  header.write('WAVE', 8)

  // fmt subchunk
  header.write('fmt ', 12)
  header.writeUInt32LE(16, 16) // Subchunk1 size (16 for PCM)
  header.writeUInt16LE(audioFormat, 20) // Audio format
  header.writeUInt16LE(channels, 22) // Num channels
  header.writeUInt32LE(sampleRate, 24) // Sample rate
  header.writeUInt32LE(byteRate, 28) // Byte rate
  header.writeUInt16LE(blockAlign, 32) // Block align
  header.writeUInt16LE(bitsPerSample, 34) // Bits per sample

  // data subchunk
  header.write('data', 36)
  header.writeUInt32LE(dataLength, 40) // Data size

  return header
}

// Handle graceful shutdown
let isShuttingDown = false

async function shutdown() {
  if (isShuttingDown) return
  isShuttingDown = true

  console.log('\n\nStopping recording...')
  await recorder.stop()

  // Combine all chunks
  const audioData = Buffer.concat(audioChunks)
  console.log(`Total audio data: ${audioData.length} bytes`)

  if (!metadata) {
    console.error('No metadata received, cannot write WAV file')
    process.exit(1)
  }

  // Create WAV file
  const wavHeader = createWavHeader(
    audioData.length,
    metadata.sampleRate,
    metadata.channelsPerFrame,
    metadata.bitsPerChannel,
    metadata.isFloat
  )

  const wavFile = Buffer.concat([wavHeader, audioData])
  writeFileSync(outputFile, wavFile)

  console.log(`Saved to: ${outputFile}`)
  console.log(`File size: ${(wavFile.length / 1024 / 1024).toFixed(2)} MB`)

  process.exit(0)
}

// Handle Ctrl+C
process.on('SIGINT', shutdown)
process.on('SIGTERM', shutdown)

// Check permission before starting
function checkPermission() {
  if (!isSystemAudioPermissionAvailable()) {
    console.log('Warning: Permission API not available, proceeding anyway...\n')
    return true
  }

  const status = getSystemAudioPermissionStatus()

  if (status === 'authorized') {
    return true
  }

  if (status === 'denied') {
    console.error('System audio recording permission denied.')
    console.error('   Opening System Settings...\n')
    openSystemSettings()
    console.error('   Please enable "System Audio Recording Only" for your terminal app.')
    console.error('   Then run this command again.\n')
    return false
  }

  // Status is 'unknown' - app not yet in System Settings list
  // macOS doesn't show a permission dialog for System Audio Recording,
  // so we need to direct the user to manually add the app in System Settings
  console.error('System audio recording permission not configured.')
  console.error('   Opening System Settings...\n')
  openSystemSettings()
  console.error('   Please add your terminal app to "System Audio Recording Only".')
  console.error('   Then run this command again.\n')
  return false
}

// Start recording
try {
  const hasPermission = checkPermission()
  if (!hasPermission) {
    process.exit(1)
  }

  await recorder.start()

  // Stop after duration
  setTimeout(shutdown, duration)
} catch (error) {
  console.error('Failed to start recording:', error.message)
  process.exit(1)
}
