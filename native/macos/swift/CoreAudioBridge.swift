import Foundation
import CoreAudio
import AVFoundation
import AudioToolbox

// MARK: - Global State Management

/// Opaque handle type for the audio capture session
public typealias AudioRecorderHandle = UnsafeMutableRawPointer

/// Callback type for receiving audio data
public typealias AudioDataCallback = @convention(c) (
    UnsafePointer<UInt8>,  // data pointer
    Int32,                  // data length
    UnsafeMutableRawPointer? // user context
) -> Void

/// Callback type for receiving events (start, stop, error)
public typealias AudioEventCallback = @convention(c) (
    Int32,                   // event type: 0=start, 1=stop, 2=error
    UnsafePointer<CChar>?,   // message (for errors)
    UnsafeMutableRawPointer? // user context
) -> Void

/// Callback type for receiving metadata
public typealias AudioMetadataCallback = @convention(c) (
    Double,   // sample rate
    UInt32,   // channels per frame
    UInt32,   // bits per channel
    Bool,     // is float
    UnsafePointer<CChar>, // encoding string
    UnsafeMutableRawPointer? // user context
) -> Void

// MARK: - Session State

/// Audio source type
enum AudioSource {
    case systemAudio
    case microphone(deviceUID: String?)
}

/// Unified session for both system audio and microphone recording
class AudioRecorderSession {
    var source: AudioSource?
    var tapManager: AudioTapManager?
    var recorder: NativeAudioRecorder?
    var micCaptureManager: MicrophoneCaptureManager?
    var micRecorder: MicrophoneRecorder?
    var isRunning: Bool = false

    let dataCallback: AudioDataCallback?
    let eventCallback: AudioEventCallback?
    let metadataCallback: AudioMetadataCallback?
    let userContext: UnsafeMutableRawPointer?

    init(
        dataCallback: AudioDataCallback?,
        eventCallback: AudioEventCallback?,
        metadataCallback: AudioMetadataCallback?,
        userContext: UnsafeMutableRawPointer?
    ) {
        self.dataCallback = dataCallback
        self.eventCallback = eventCallback
        self.metadataCallback = metadataCallback
        self.userContext = userContext
    }

    func emitData(_ data: Data) {
        data.withUnsafeBytes { buffer in
            if let baseAddress = buffer.baseAddress?.assumingMemoryBound(to: UInt8.self) {
                dataCallback?(baseAddress, Int32(buffer.count), userContext)
            }
        }
    }

    func emitEvent(_ eventType: Int32, message: String? = nil) {
        if let msg = message {
            msg.withCString { cstr in
                eventCallback?(eventType, cstr, userContext)
            }
        } else {
            eventCallback?(eventType, nil, userContext)
        }
    }

    func emitMetadata(_ metadata: NativeAudioMetadata) {
        metadata.encoding.withCString { encodingCStr in
            metadataCallback?(
                metadata.sampleRate,
                metadata.channelsPerFrame,
                metadata.bitsPerChannel,
                metadata.isFloat,
                encodingCStr,
                userContext
            )
        }
    }
}

// MARK: - C-Compatible API

/// Creates a new audio recorder session
/// Returns an opaque handle that must be freed with coreaudio_destroy
@_cdecl("audio_create")
public func audio_create(
    dataCallback: AudioDataCallback?,
    eventCallback: AudioEventCallback?,
    metadataCallback: AudioMetadataCallback?,
    userContext: UnsafeMutableRawPointer?
) -> AudioRecorderHandle? {
    let session = AudioRecorderSession(
        dataCallback: dataCallback,
        eventCallback: eventCallback,
        metadataCallback: metadataCallback,
        userContext: userContext
    )

    return Unmanaged.passRetained(session).toOpaque()
}

/// Start system audio capture
@_cdecl("audio_start_system_audio")
public func audio_start_system_audio(
    handle: AudioRecorderHandle,
    sampleRate: Double,           // 0 for native rate
    chunkDurationMs: Double,      // chunk duration in milliseconds
    mute: Bool,
    isMono: Bool,
    emitSilence: Bool,            // ignored on macOS - always emits continuous audio
    includeProcesses: UnsafePointer<Int32>?,
    includeProcessCount: Int32,
    excludeProcesses: UnsafePointer<Int32>?,
    excludeProcessCount: Int32
) -> Int32 {
    guard let session = Unmanaged<AudioRecorderSession>.fromOpaque(handle).takeUnretainedValue() as AudioRecorderSession? else {
        return -1
    }

    if session.isRunning {
        return -2 // Already running
    }

    // Convert process arrays
    var includeList: [Int32] = []
    var excludeList: [Int32] = []

    if let ptr = includeProcesses, includeProcessCount > 0 {
        includeList = Array(UnsafeBufferPointer(start: ptr, count: Int(includeProcessCount)))
    }

    if let ptr = excludeProcesses, excludeProcessCount > 0 {
        excludeList = Array(UnsafeBufferPointer(start: ptr, count: Int(excludeProcessCount)))
    }

    // Determine process configuration
    let (processes, isExclusive): ([Int32], Bool)
    if !includeList.isEmpty {
        processes = includeList
        isExclusive = false
    } else if !excludeList.isEmpty {
        processes = excludeList
        isExclusive = true
    } else {
        processes = []
        isExclusive = true
    }

    // Create tap configuration
    let tapConfig = TapConfiguration(
        processes: processes,
        muteBehavior: mute ? .muted : .unmuted,
        isExclusive: isExclusive,
        isMono: isMono
    )

    // Set up audio tap
    let tapManager = AudioTapManager()
    do {
        try tapManager.setupAudioTap(with: tapConfig)
    } catch {
        session.emitEvent(2, message: "Failed to setup audio tap: \(error)")
        return -3
    }

    guard let deviceID = tapManager.getDeviceID() else {
        session.emitEvent(2, message: "Failed to get device ID")
        return -4
    }

    session.source = .systemAudio
    session.tapManager = tapManager

    // Create native output handler that calls our callbacks
    let outputHandler = NativeAudioOutputHandler(session: session)

    // Convert chunk duration from ms to seconds
    let chunkDurationSec = chunkDurationMs / 1000.0

    // Create recorder
    let targetSampleRate: Double? = sampleRate > 0 ? sampleRate : nil
    let recorder: NativeAudioRecorder
    do {
        recorder = try NativeAudioRecorder(
            deviceID: deviceID,
            outputHandler: outputHandler,
            convertToSampleRate: targetSampleRate,
            chunkDuration: chunkDurationSec
        )
    } catch AudioFormatError.formatUnavailable(let deviceID, let status) {
        session.emitEvent(2, message: "Failed to get audio format from device \(deviceID): OSStatus \(status)")
        return -5
    } catch {
        session.emitEvent(2, message: "Failed to create recorder: \(error)")
        return -6
    }

    session.recorder = recorder
    session.isRunning = true

    // Start recording in a background thread
    DispatchQueue.global(qos: .userInitiated).async {
        recorder.startRecording()

        // Run the audio processing loop
        while session.isRunning {
            let result = CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.01, false)
            if result == CFRunLoopRunResult.stopped || result == CFRunLoopRunResult.finished {
                break
            }
        }
    }

    return 0
}

/// Start microphone capture
@_cdecl("audio_start_microphone")
public func audio_start_microphone(
    handle: AudioRecorderHandle,
    sampleRate: Double,
    chunkDurationMs: Double,
    isMono: Bool,
    emitSilence: Bool,                  // ignored on macOS - always emits continuous audio
    deviceUID: UnsafePointer<CChar>?,   // NULL for default device
    gain: Double                        // 0.0 to 1.0
) -> Int32 {
    guard let session = Unmanaged<AudioRecorderSession>.fromOpaque(handle).takeUnretainedValue() as AudioRecorderSession? else {
        return -1
    }

    if session.isRunning {
        return -2 // Already running
    }

    // Convert device UID if provided
    let deviceUIDString: String? = deviceUID != nil ? String(cString: deviceUID!) : nil

    session.source = .microphone(deviceUID: deviceUIDString)

    // Create microphone capture manager (for gain and device discovery)
    let micCaptureManager = MicrophoneCaptureManager()
    micCaptureManager.setGain(gain)
    session.micCaptureManager = micCaptureManager

    // Create native output handler that calls our callbacks
    let outputHandler = NativeAudioOutputHandler(session: session)

    // Convert chunk duration from ms to seconds
    let chunkDurationSec = chunkDurationMs / 1000.0

    // Create microphone recorder using AVAudioEngine
    let targetSampleRate: Double? = sampleRate > 0 ? sampleRate : nil
    let micRecorder = MicrophoneRecorder(
        outputHandler: outputHandler,
        convertToSampleRate: targetSampleRate,
        chunkDuration: chunkDurationSec,
        gain: micCaptureManager.getGain(),
        deviceUID: deviceUIDString
    )

    session.micRecorder = micRecorder
    session.isRunning = true

    // Start recording - AVCaptureSession handles the audio capture
    do {
        try micRecorder.startRecording()
    } catch MicrophoneError.deviceNotFound(let uid) {
        session.isRunning = false
        session.emitEvent(2, message: "Microphone device not found: \(uid)")
        return -3
    } catch MicrophoneError.permissionDenied {
        session.isRunning = false
        session.emitEvent(2, message: "Microphone permission denied")
        return -4
    } catch MicrophoneError.noDefaultInputDevice {
        session.isRunning = false
        session.emitEvent(2, message: "No default input device available")
        return -5
    } catch MicrophoneError.sessionConfigurationFailed(let error) {
        session.isRunning = false
        session.emitEvent(2, message: "Failed to configure capture session: \(error.localizedDescription)")
        return -6
    } catch MicrophoneError.captureSessionError(let message) {
        session.isRunning = false
        session.emitEvent(2, message: "Capture session error: \(message)")
        return -7
    } catch {
        session.isRunning = false
        session.emitEvent(2, message: "Failed to start microphone: \(error)")
        return -8
    }

    return 0
}

/// Stops the audio capture session
@_cdecl("audio_stop")
public func audio_stop(handle: AudioRecorderHandle) -> Int32 {
    guard let session = Unmanaged<AudioRecorderSession>.fromOpaque(handle).takeUnretainedValue() as AudioRecorderSession? else {
        return -1
    }

    if !session.isRunning {
        return 0 // Already stopped
    }

    session.isRunning = false

    // Stop system audio recorder if running
    session.recorder?.stopRecording()
    session.recorder = nil
    session.tapManager = nil

    // Stop microphone recorder if running
    session.micRecorder?.stopRecording()
    session.micRecorder = nil
    session.micCaptureManager = nil

    session.source = nil

    return 0
}

/// Destroys the session and frees resources
@_cdecl("audio_destroy")
public func audio_destroy(handle: AudioRecorderHandle) {
    // Stop if running
    _ = audio_stop(handle: handle)

    // Release the session
    Unmanaged<AudioRecorderSession>.fromOpaque(handle).release()
}

/// Returns whether the session is currently recording
@_cdecl("audio_is_running")
public func audio_is_running(handle: AudioRecorderHandle) -> Bool {
    guard let session = Unmanaged<AudioRecorderSession>.fromOpaque(handle).takeUnretainedValue() as AudioRecorderSession? else {
        return false
    }
    return session.isRunning
}
