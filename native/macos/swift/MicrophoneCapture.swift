import Foundation
import AVFoundation
import AudioToolbox
import CoreMedia

// MARK: - Microphone Capture Manager

class MicrophoneCaptureManager {
    private var gain: Float = 1.0

    /// Request microphone access - this may trigger Bluetooth device connection
    func requestAccess() async -> Bool {
        return await withCheckedContinuation { continuation in
            AVCaptureDevice.requestAccess(for: .audio) { granted in
                continuation.resume(returning: granted)
            }
        }
    }

    /// Check current authorization status
    func authorizationStatus() -> AVAuthorizationStatus {
        return AVCaptureDevice.authorizationStatus(for: .audio)
    }

    func setGain(_ gain: Double) {
        self.gain = Float(max(0.0, min(2.0, gain)))  // Allow up to 2x gain
    }

    func getGain() -> Float {
        return gain
    }

    /// Get list of available audio input devices using AVCaptureDevice
    func getAvailableInputDevices() -> [AVCaptureDevice] {
        let discoverySession = AVCaptureDevice.DiscoverySession(
            deviceTypes: [.microphone, .external],
            mediaType: .audio,
            position: .unspecified
        )
        return discoverySession.devices
    }

    /// Get the default audio input device
    func getDefaultInputDevice() -> AVCaptureDevice? {
        return AVCaptureDevice.default(for: .audio)
    }

    /// Find device by UID
    func findDevice(byUID uid: String) -> AVCaptureDevice? {
        return AVCaptureDevice(uniqueID: uid)
    }
}

// MARK: - Microphone Errors

enum MicrophoneError: Error {
    case deviceNotFound(String)
    case noDefaultInputDevice
    case permissionDenied
    case sessionConfigurationFailed(Error)
    case formatError(String)
    case captureSessionError(String)

    var localizedDescription: String {
        switch self {
        case .deviceNotFound(let uid):
            return "Microphone device not found: \(uid)"
        case .noDefaultInputDevice:
            return "No default input device available"
        case .permissionDenied:
            return "Microphone permission denied"
        case .sessionConfigurationFailed(let error):
            return "Failed to configure capture session: \(error.localizedDescription)"
        case .formatError(let message):
            return "Audio format error: \(message)"
        case .captureSessionError(let message):
            return "Capture session error: \(message)"
        }
    }
}

// MARK: - AVCaptureSession-based Microphone Recorder

class MicrophoneRecorder: NSObject {
    private let captureSession = AVCaptureSession()
    private var audioOutput: AVCaptureAudioDataOutput?
    private let audioQueue = DispatchQueue(label: "com.coreaudio.microphone.capture", qos: .userInitiated)

    private var outputHandler: NativeAudioOutputHandler
    private var targetSampleRate: Double?
    private var chunkDuration: Double
    private var gain: Float
    private var deviceUID: String?

    private var audioBuffer: AudioBuffer?
    private var converter: AudioFormatConverter?
    private var finalFormat: AudioStreamBasicDescription?
    private var sourceFormat: AudioStreamBasicDescription?
    private var isRecording = false
    private var hasEmittedMetadata = false

    init(
        outputHandler: NativeAudioOutputHandler,
        convertToSampleRate: Double? = nil,
        chunkDuration: Double = 0.2,
        gain: Float = 1.0,
        deviceUID: String? = nil
    ) {
        self.outputHandler = outputHandler
        self.targetSampleRate = convertToSampleRate
        self.chunkDuration = chunkDuration
        self.gain = gain
        self.deviceUID = deviceUID
        super.init()
    }

    func startRecording() throws {
        guard !isRecording else { return }

        // Get the audio device
        let device: AVCaptureDevice
        if let uid = deviceUID {
            guard let foundDevice = AVCaptureDevice(uniqueID: uid) else {
                throw MicrophoneError.deviceNotFound(uid)
            }
            device = foundDevice
        } else {
            guard let defaultDevice = AVCaptureDevice.default(for: .audio) else {
                throw MicrophoneError.noDefaultInputDevice
            }
            device = defaultDevice
        }

        // Configure the capture session
        captureSession.beginConfiguration()

        // Remove any existing inputs/outputs
        for input in captureSession.inputs {
            captureSession.removeInput(input)
        }
        for output in captureSession.outputs {
            captureSession.removeOutput(output)
        }

        // Add audio input
        do {
            let audioInput = try AVCaptureDeviceInput(device: device)
            if captureSession.canAddInput(audioInput) {
                captureSession.addInput(audioInput)
            } else {
                captureSession.commitConfiguration()
                throw MicrophoneError.captureSessionError("Cannot add audio input to session")
            }
        } catch let error as MicrophoneError {
            captureSession.commitConfiguration()
            throw error
        } catch {
            captureSession.commitConfiguration()
            throw MicrophoneError.sessionConfigurationFailed(error)
        }

        // Add audio output
        let audioOutput = AVCaptureAudioDataOutput()
        audioOutput.setSampleBufferDelegate(self, queue: audioQueue)

        if captureSession.canAddOutput(audioOutput) {
            captureSession.addOutput(audioOutput)
            self.audioOutput = audioOutput
        } else {
            captureSession.commitConfiguration()
            throw MicrophoneError.captureSessionError("Cannot add audio output to session")
        }

        captureSession.commitConfiguration()

        // Start the session
        isRecording = true
        hasEmittedMetadata = false

        captureSession.startRunning()

        if !captureSession.isRunning {
            isRecording = false
            throw MicrophoneError.captureSessionError("Failed to start capture session")
        }

        outputHandler.handleStreamStart()
    }

    func stopRecording() {
        guard isRecording else { return }

        isRecording = false

        // Process any remaining audio
        if let buffer = audioBuffer, let format = finalFormat {
            buffer.processChunks().forEach { packet in
                let processedPacket = converter?.transform(packet) ?? packet
                outputHandler.handleAudioPacket(processedPacket)
            }
        }

        // Stop the session
        captureSession.stopRunning()

        outputHandler.handleStreamStop()
    }

    private func setupAudioProcessing(from formatDescription: CMAudioFormatDescription) {
        let asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDescription)?.pointee

        guard let format = asbd else {
            return
        }

        // Create source format (what we receive from the capture session)
        // AVCaptureSession typically delivers Float32 interleaved audio
        let sourceFormat = AudioStreamBasicDescription(
            mSampleRate: format.mSampleRate,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: format.mFormatFlags,
            mBytesPerPacket: format.mBytesPerPacket,
            mFramesPerPacket: format.mFramesPerPacket,
            mBytesPerFrame: format.mBytesPerFrame,
            mChannelsPerFrame: format.mChannelsPerFrame,
            mBitsPerChannel: format.mBitsPerChannel,
            mReserved: 0
        )

        self.sourceFormat = sourceFormat

        // Set up audio buffer
        self.audioBuffer = AudioBuffer(format: sourceFormat, chunkDuration: chunkDuration)

        // Set up converter if needed
        if let targetRate = targetSampleRate, AudioFormatConverter.isValidSampleRate(targetRate) {
            do {
                let converter = try AudioFormatConverter.toSampleRate(targetRate, from: sourceFormat)
                self.converter = converter
                self.finalFormat = converter.targetFormatDescription
            } catch {
                self.converter = nil
                self.finalFormat = sourceFormat
            }
        } else {
            self.converter = nil
            self.finalFormat = sourceFormat
        }
    }

    private func createMetadata(for format: AudioStreamBasicDescription) -> NativeAudioMetadata {
        let isFloat = format.mFormatFlags & kAudioFormatFlagIsFloat != 0
        let encoding = isFloat ? "pcm_f32le" : "pcm_s16le"

        return NativeAudioMetadata(
            sampleRate: format.mSampleRate,
            channelsPerFrame: format.mChannelsPerFrame,
            bitsPerChannel: format.mBitsPerChannel,
            isFloat: isFloat,
            encoding: encoding
        )
    }
}

// MARK: - AVCaptureAudioDataOutputSampleBufferDelegate

extension MicrophoneRecorder: AVCaptureAudioDataOutputSampleBufferDelegate {
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard isRecording else { return }

        // Get the format description on first buffer
        if !hasEmittedMetadata {
            if let formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer) {
                setupAudioProcessing(from: formatDescription)

                if let format = finalFormat {
                    let metadata = createMetadata(for: format)
                    outputHandler.handleMetadata(metadata)
                    hasEmittedMetadata = true
                }
            }
        }

        guard audioBuffer != nil else { return }

        // Get audio buffer list from sample buffer
        var blockBuffer: CMBlockBuffer?
        var audioBufferList = AudioBufferList()
        var bufferListSize = MemoryLayout<AudioBufferList>.size

        let status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            sampleBuffer,
            bufferListSizeNeededOut: nil,
            bufferListOut: &audioBufferList,
            bufferListSize: bufferListSize,
            blockBufferAllocator: nil,
            blockBufferMemoryAllocator: nil,
            flags: 0,
            blockBufferOut: &blockBuffer
        )

        guard status == noErr else { return }

        // Process the audio data
        let audioBuffer = audioBufferList.mBuffers
        guard let dataPointer = audioBuffer.mData else { return }

        let frameCount = Int(CMSampleBufferGetNumSamples(sampleBuffer))
        let channelCount = Int(self.sourceFormat?.mChannelsPerFrame ?? 1)
        let bytesPerFrame = Int(self.sourceFormat?.mBytesPerFrame ?? 4)

        // Apply gain if needed
        if gain != 1.0 {
            let floatPointer = dataPointer.assumingMemoryBound(to: Float32.self)
            let sampleCount = frameCount * channelCount

            for i in 0..<sampleCount {
                var sample = floatPointer[i] * gain
                // Clamp to prevent clipping
                sample = max(-1.0, min(1.0, sample))
                floatPointer[i] = sample
            }
        }

        // Convert to Data and add to buffer
        let dataLength = frameCount * bytesPerFrame
        let audioData = Data(bytes: dataPointer, count: dataLength)

        self.audioBuffer?.append(audioData)
        processChunks()
    }

    private func processChunks() {
        audioBuffer?.processChunks().forEach { packet in
            let processedPacket = converter?.transform(packet) ?? packet
            outputHandler.handleAudioPacket(processedPacket)
        }
    }
}
