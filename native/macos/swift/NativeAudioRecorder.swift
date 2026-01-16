import AudioToolbox
import CoreAudio
import Foundation

// MARK: - Native Metadata Structure

struct NativeAudioMetadata {
    let sampleRate: Double
    let channelsPerFrame: UInt32
    let bitsPerChannel: UInt32
    let isFloat: Bool
    let encoding: String
}

// MARK: - Native Output Handler

class NativeAudioOutputHandler {
    weak var session: AudioRecorderSession?

    init(session: AudioRecorderSession) {
        self.session = session
    }

    func handleAudioPacket(_ packet: AudioPacket) {
        session?.emitData(packet.data)
    }

    func handleMetadata(_ metadata: NativeAudioMetadata) {
        session?.emitMetadata(metadata)
    }

    func handleStreamStart() {
        session?.emitEvent(0) // 0 = start
    }

    func handleStreamStop() {
        session?.emitEvent(1) // 1 = stop
    }
}

// MARK: - Native Audio Recorder

public class NativeAudioRecorder {
    private var deviceID: AudioObjectID
    private var ioProcID: AudioDeviceIOProcID?
    private var finalFormat: AudioStreamBasicDescription!
    private var audioBuffer: AudioBuffer?
    private var outputHandler: NativeAudioOutputHandler
    private var converter: AudioFormatConverter?

    init(
        deviceID: AudioObjectID,
        outputHandler: NativeAudioOutputHandler,
        convertToSampleRate: Double? = nil,
        chunkDuration: Double = 0.2
    ) throws {
        self.deviceID = deviceID
        self.outputHandler = outputHandler

        // Get source format and set up conversion if requested
        let sourceFormat = try AudioFormatManager.getDeviceFormat(deviceID: deviceID)

        // Set up the audio buffer using source format and configurable chunk duration
        self.audioBuffer = AudioBuffer(format: sourceFormat, chunkDuration: chunkDuration)

        if let targetSampleRate = convertToSampleRate {
            // Validate sample rate
            guard AudioFormatConverter.isValidSampleRate(targetSampleRate) else {
                self.converter = nil
                self.finalFormat = sourceFormat
                return
            }

            do {
                let converter = try AudioFormatConverter.toSampleRate(targetSampleRate, from: sourceFormat)
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

    func startRecording() {
        // Send metadata for final format
        let metadata = createMetadata(for: finalFormat)
        outputHandler.handleMetadata(metadata)
        outputHandler.handleStreamStart()

        setupAndStartIOProc()
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

    private func setupAndStartIOProc() {
        var status = AudioDeviceCreateIOProcID(
            deviceID,
            { (inDevice, inNow, inInputData, inInputTime, outOutputData, inOutputTime, inClientData) -> OSStatus in
                let recorder = Unmanaged<NativeAudioRecorder>.fromOpaque(inClientData!).takeUnretainedValue()
                return recorder.processAudio(inInputData)
            },
            Unmanaged.passUnretained(self).toOpaque(),
            &ioProcID
        )

        guard status == noErr else {
            outputHandler.session?.emitEvent(2, message: "Failed to create IO proc: \(status)")
            return
        }

        status = AudioDeviceStart(deviceID, ioProcID)

        if status != noErr {
            cleanupIOProc()
            outputHandler.session?.emitEvent(2, message: "Failed to start audio device: \(status)")
        }
    }

    private func processAudio(_ inputData: UnsafePointer<AudioBufferList>) -> OSStatus {
        let bufferList = inputData.pointee
        let firstBuffer = bufferList.mBuffers

        guard firstBuffer.mData != nil && firstBuffer.mDataByteSize > 0 else {
            return noErr
        }

        // Append raw audio data to buffer
        let audioData = Data(bytes: firstBuffer.mData!, count: Int(firstBuffer.mDataByteSize))
        audioBuffer?.append(audioData)

        processAudioBuffer()

        return noErr
    }

    func stopRecording() {
        processAudioBuffer()
        outputHandler.handleStreamStop()
        cleanupIOProc()
    }

    private func processAudioBuffer() {
        // Process and send complete chunks, applying conversion if needed
        audioBuffer?.processChunks().forEach { packet in
            let processedPacket = converter?.transform(packet) ?? packet
            outputHandler.handleAudioPacket(processedPacket)
        }
    }

    private func cleanupIOProc() {
        if let ioProcID = ioProcID {
            AudioDeviceStop(deviceID, ioProcID)
            AudioDeviceDestroyIOProcID(deviceID, ioProcID)
            self.ioProcID = nil
        }
    }
}
