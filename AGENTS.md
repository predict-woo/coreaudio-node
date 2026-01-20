# AGENTS.md - native-audio-node

Guide for AI agents working on this codebase.

## Quick Reference

```bash
# Install dependencies
pnpm install

# Full build (native + copy + TypeScript)
pnpm run build

# Build native addon only
pnpm run build:native
pnpm run build:native:debug  # Debug build

# Build TypeScript only
pnpm run build:ts

# Watch mode for TypeScript
pnpm run dev

# Copy built binary to platform package
pnpm run copy-binary

# Run examples (manual testing)
node examples/record-system.mjs
node examples/record-mic.mjs
node examples/test-devices.mjs
node examples/test-permission.mjs
```

## Project Structure

```
native-audio-node/
├── packages/
│   ├── native-audio-node/       # Main TypeScript package (edit here)
│   │   ├── src/                 # TypeScript source
│   │   │   ├── index.ts         # Public exports
│   │   │   ├── types.ts         # TypeScript interfaces
│   │   │   ├── binding.ts       # Native module loader
│   │   │   ├── base-recorder.ts # Abstract base class
│   │   │   ├── system-audio-recorder.ts
│   │   │   ├── microphone-recorder.ts
│   │   │   ├── devices.ts       # Device enumeration
│   │   │   └── permission.ts    # Permission management
│   │   └── tsup.config.ts       # Build config
│   │
│   ├── darwin-arm64/            # macOS Apple Silicon binary
│   ├── darwin-x64/              # macOS Intel binary
│   ├── win32-x64/               # Windows x64 binary
│   └── win32-arm64/             # Windows ARM64 binary
│
├── native/                       # Native code (C++/Swift)
│   ├── napi/                    # Node-API wrapper
│   ├── macos/swift/             # macOS Swift implementation
│   └── windows/                 # Windows WASAPI implementation
│
└── examples/                    # Usage examples (used for testing)
```

## Code Style

### TypeScript

- **ES2022 target**, **NodeNext modules**
- **Strict mode** enabled - do not bypass with `as any`, `@ts-ignore`, etc.
- **ESM-first** - all packages use `"type": "module"`

### Formatting (Prettier)

```json
{
  "trailingComma": "es5",
  "semi": false,
  "singleQuote": true,
  "printWidth": 120
}
```

- **No semicolons**
- **Single quotes** for strings
- **Trailing commas** in objects/arrays (ES5 style)
- **120 character** line width

### Imports

```typescript
// Use .js extension for local imports (ESM requirement)
import { BaseAudioRecorder } from './base-recorder.js'

// Use 'type' keyword for type-only imports
import type { AudioChunk, AudioMetadata } from './types.js'

// Group imports: external packages first, then local
import { EventEmitter } from 'events'
import { createRequire } from 'module'

import { getAudioRecorderNative } from './binding.js'
import type { AudioRecorderEvents } from './types.js'
```

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Variables, functions | camelCase | `sampleRate`, `getDefaultInputDevice` |
| Classes | PascalCase | `SystemAudioRecorder`, `PermissionError` |
| Interfaces | PascalCase | `AudioChunk`, `AudioRecorderOptions` |
| Constants | camelCase or UPPER_SNAKE | `cachedBinding`, `POLL_INTERVAL` |
| Files | kebab-case | `base-recorder.ts`, `system-audio-recorder.ts` |

### Types

- Define interfaces in `types.ts` for public APIs
- Export types from `index.ts`
- Use explicit return types on public functions
- Prefer interfaces over type aliases for object shapes

```typescript
// Good: Interface for object shape
export interface AudioChunk {
  data: Buffer
}

// Good: Type alias for union types
export type PermissionStatus = 'unknown' | 'denied' | 'authorized'
```

### Classes

- Use abstract base class pattern for shared functionality
- Use `protected` for members accessible to subclasses
- Use `private` for internal implementation details

```typescript
export abstract class BaseAudioRecorder {
  protected events = new EventEmitter()
  protected native: AudioRecorderNativeClass
  private pollInterval: ReturnType<typeof setInterval> | null = null

  abstract start(): Promise<void>
}
```

### Documentation

- JSDoc comments on all public APIs
- Include `@example` blocks with working code
- Document platform-specific behavior with `**macOS:**` / `**Windows:**` prefixes

```typescript
/**
 * Check the current system audio recording permission status.
 *
 * **macOS:** Uses TCC private API - may not be available on all systems.
 * **Windows:** Always returns 'authorized' (loopback capture doesn't require permission).
 *
 * @returns 'authorized' | 'denied' | 'unknown'
 */
export function getSystemAudioPermissionStatus(): PermissionStatus {
```

### Error Handling

- Create custom error classes for specific error types
- Use Promise-based async patterns
- Provide descriptive error messages

```typescript
export class PermissionError extends Error {
  public readonly status: PermissionStatus

  constructor(message: string, status: PermissionStatus) {
    super(message)
    this.name = 'PermissionError'
    this.status = status
  }
}
```

### Async Patterns

```typescript
// Wrap callback-based APIs in Promises
export function requestSystemAudioPermission(): Promise<boolean> {
  return new Promise((resolve) => {
    loadBinding().requestSystemAudioPermission((granted) => {
      resolve(granted)
    })
  })
}
```

## Key Technical Notes

### Binary Loading

Platform-specific binaries are loaded via `optionalDependencies`. The `binding.ts` file detects the current platform/arch and requires the correct package:

```typescript
const packageName = `@native-audio-node/${process.platform}-${process.arch}`
const addon = require(packageName)
```

### Platform Support

- **macOS**: 14.2+ (Sonoma), Core Audio, requires TCC permission
- **Windows**: 10 2004+, WASAPI, no permission needed for system audio

### Audio Format

- Raw PCM audio data
- 32-bit float (default) or 16-bit int
- Mono or stereo
- Sample rates: 8kHz-48kHz

## Common Tasks

### Adding a New Public API

1. Add interface to `types.ts`
2. Implement in appropriate file
3. Export from `index.ts`
4. Add JSDoc with `@example`
5. Run `pnpm run build:ts` to verify

### Testing Changes

No formal test framework - use examples:

```bash
pnpm run build
node examples/record-system.mjs -d 3
node examples/test-devices.mjs
```

### Debugging Native Code

```bash
pnpm run build:native:debug
# Use lldb/gdb to debug the .node binary
```
