#!/usr/bin/env node

import {
  getSystemAudioPermissionStatus,
  isSystemAudioPermissionAvailable,
  requestSystemAudioPermission,
  openSystemSettings,
  ensureSystemAudioPermission,
  PermissionError,
} from '../dist/index.js'

console.log('Testing System Audio Permission API\n')

// Check if permission API is available
// macOS: Uses TCC private API
// Windows: Always returns true (no permission needed for loopback)
const available = isSystemAudioPermissionAvailable()
console.log(`Permission API available: ${available}`)

if (!available) {
  console.log('⚠️  Permission API not available - permission checking may not work')
  process.exit(0)
}

// Check current permission status
// macOS: Returns 'unknown', 'denied', or 'authorized'
// Windows: Always returns 'authorized'
const status = getSystemAudioPermissionStatus()
console.log(`Current permission status: ${status}`)

if (status === 'authorized') {
  console.log('✅ Permission already granted!')
  process.exit(0)
}

if (status === 'denied') {
  console.log('❌ Permission denied. Opening System Settings...')
  openSystemSettings()
  console.log('\nmacOS: Please enable "System Audio Recording Only" for your terminal app.')
  console.log('Then run this test again.')
  process.exit(1)
}

// Status is 'unknown' - try requesting permission
console.log('\n📋 Requesting permission...')

try {
  const granted = await requestSystemAudioPermission()
  console.log(`Permission request result: ${granted ? 'granted' : 'denied'}`)

  if (granted) {
    console.log('✅ Permission granted!')
  } else {
    console.log('❌ Permission denied or dismissed.')
    console.log('\nOpening System Settings...')
    openSystemSettings()
    console.log('macOS: Please enable "System Audio Recording Only" for your terminal app.')
  }
} catch (error) {
  console.error('Error requesting permission:', error)
}
