#!/usr/bin/env node

import {
  MicrophoneRecorder,
  listAudioDevices,
  getMicrophonePermissionStatus,
  requestMicrophonePermission,
  openSystemSettings,
} from '../dist/index.js'
import { writeFileSync } from 'fs'
import { parseArgs } from 'util'

// Parse command line arguments
const { values } = parseArgs({
  options: {
    output: { type: 'string', short: 'o', default: 'mic-recording.wav' },
    duration: { type: 'string', short: 'd', default: '5' },
    'sample-rate': { type: 'string', short: 's' },
    'device-id': { type: 'string', short: 'i' },
    gain: { type: 'string', short: 'g', default: '1.0' },
    'list-devices': { type: 'boolean', short: 'l', default: false },
    help: { type: 'boolean', short: 'h', default: false },
  },
  allowPositionals: true,
})

if (values.help) {
  console.log(`
record-mic - Record microphone audio to WAV file

Usage: node record-mic.mjs [options]

Options:
  -o, --output <file>      Output WAV file (default: mic-recording.wav)
  -d, --duration <seconds> Recording duration in seconds (default: 5)
  -s, --sample-rate <rate> Target sample rate (8000, 16000, 44100, 48000)
  -i, --device-id <id>     Device UID to record from (default: system default)
  -g, --gain <value>       Microphone gain 0.0-1.0 (default: 1.0)
  -l, --list-devices       List available input devices
  -h, --help               Show this help message

Examples:
  node record-mic.mjs -o output.wav -d 10
  node record-mic.mjs -s 16000 -g 0.8
  node record-mic.mjs -l  # list available microphones
`)
  process.exit(0)
}

// List devices if requested
if (values['list-devices']) {
  console.log('\nAvailable input devices:\n')
  const devices = listAudioDevices().filter((d) => d.isInput)
  for (const device of devices) {
    const defaultMarker = device.isDefault ? ' (default)' : ''
    console.log(`  ${device.id}`)
    console.log(`    Name: ${device.name}${defaultMarker}`)
    console.log(`    Manufacturer: ${device.manufacturer || 'Unknown'}`)
    console.log(`    Sample Rate: ${device.sampleRate} Hz`)
    console.log(`    Channels: ${device.channelCount}`)
    console.log('')
  }
  process.exit(0)
}

const outputFile = values.output
const duration = parseInt(values.duration, 10) * 1000 // Convert to ms
const sampleRate = values['sample-rate'] ? parseInt(values['sample-rate'], 10) : undefined
const deviceId = values['device-id']
const gain = parseFloat(values.gain)

console.log(`Recording to: ${outputFile}`)
console.log(`Duration: ${duration / 1000} seconds`)
if (sampleRate) console.log(`Sample rate: ${sampleRate} Hz`)
if (deviceId) console.log(`Device ID: ${deviceId}`)
console.log(`Gain: ${gain}`)
console.log('')

// Buffer to collect all audio data
const audioChunks = []
let metadata = null

const recorder = new MicrophoneRecorder({
  sampleRate,
  chunkDurationMs: 100,
  deviceId,
  gain,
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
async function checkPermission() {
  const status = getMicrophonePermissionStatus()

  if (status === 'authorized') {
    return true
  }

  if (status === 'denied') {
    console.error('Microphone recording permission denied.')
    console.error('   Opening System Settings...\n')
    openSystemSettings()
    console.error('   Please enable "Microphone" access for your terminal app.')
    console.error('   Then run this command again.\n')
    return false
  }

  // Status is 'unknown' - request permission
  console.log('Requesting microphone permission...')
  const granted = await requestMicrophonePermission()
  if (!granted) {
    console.error('Microphone permission not granted.')
    return false
  }
  return true
}

// Start recording
try {
  const hasPermission = await checkPermission()
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
