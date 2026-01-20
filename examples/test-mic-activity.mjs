#!/usr/bin/env node

import { MicrophoneActivityMonitor, listAudioDevices } from '../packages/native-audio-node/dist/index.js'
import { parseArgs } from 'util'

const { values } = parseArgs({
  options: {
    duration: { type: 'string', short: 'd', default: '30' },
    scope: { type: 'string', short: 's', default: 'all' },
    help: { type: 'boolean', short: 'h', default: false },
  },
  allowPositionals: true,
})

if (values.help) {
  console.log(`
test-mic-activity - Monitor microphone usage by any application

Usage: node test-mic-activity.mjs [options]

Options:
  -d, --duration <seconds>  Monitoring duration in seconds (default: 30)
  -s, --scope <scope>       'all' or 'default' (default: all)
  -h, --help                Show this help message

Examples:
  node test-mic-activity.mjs -d 60
  node test-mic-activity.mjs -s default
`)
  process.exit(0)
}

const duration = parseInt(values.duration, 10) * 1000
const scope = values.scope

console.log('Microphone Activity Monitor')
console.log('===========================')
console.log(`Scope: ${scope}`)
console.log(`Duration: ${duration / 1000} seconds`)
console.log('')

const inputDevices = listAudioDevices().filter((d) => d.isInput)
console.log(`Found ${inputDevices.length} input device(s):`)
for (const device of inputDevices) {
  const defaultMarker = device.isDefault ? ' (default)' : ''
  console.log(`  - ${device.name}${defaultMarker}`)
}
console.log('')

const monitor = new MicrophoneActivityMonitor({ scope })

monitor.on('change', (isActive) => {
  const timestamp = new Date().toISOString()
  if (isActive) {
    console.log(`[${timestamp}] 🎤 MICROPHONE IN USE`)
  } else {
    console.log(`[${timestamp}] 🔇 Microphone idle`)
  }
})

monitor.on('deviceChange', (device, isActive) => {
  const timestamp = new Date().toISOString()
  const status = isActive ? '🟢 active' : '⚪ inactive'
  console.log(`[${timestamp}] Device "${device.name}": ${status}`)
})

monitor.on('error', (error) => {
  console.error('Error:', error.message)
})

console.log('Starting monitor...')
console.log('(Try using any app that accesses your microphone)')
console.log('')

monitor.start()

const initialState = monitor.isActive()
console.log(`Initial state: ${initialState ? 'ACTIVE' : 'idle'}`)

const activeDevices = monitor.getActiveDevices()
if (activeDevices.length > 0) {
  console.log('Currently active devices:')
  for (const device of activeDevices) {
    console.log(`  - ${device.name}`)
  }
}
console.log('')

process.on('SIGINT', () => {
  console.log('\nStopping monitor...')
  monitor.stop()
  process.exit(0)
})

process.on('SIGTERM', () => {
  monitor.stop()
  process.exit(0)
})

setTimeout(() => {
  console.log('\nDuration reached, stopping monitor...')
  monitor.stop()
  process.exit(0)
}, duration)
