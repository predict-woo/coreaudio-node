import AVFoundation
import CoreAudio
import Foundation

public class AudioFormatConverter {
    private let avConverter: AVAudioConverter
    private let sourceFormat: AVAudioFormat
    private let targetFormat: AVAudioFormat

    public init(sourceFormat: AudioStreamBasicDescription, targetFormat: AudioStreamBasicDescription) throws {
        var mutableSourceFormat = sourceFormat
        var mutableTargetFormat = targetFormat

        guard let sourceAVFormat = AVAudioFormat(streamDescription: &mutableSourceFormat),
              let targetAVFormat = AVAudioFormat(streamDescription: &mutableTargetFormat)
        else {
            throw AudioConverterError.invalidFormat
        }

        guard let converter = AVAudioConverter(from: sourceAVFormat, to: targetAVFormat) else {
            throw AudioConverterError.creationFailed
        }

        self.sourceFormat = sourceAVFormat
        self.targetFormat = targetAVFormat
        self.avConverter = converter
    }

    public var targetFormatDescription: AudioStreamBasicDescription {
        return targetFormat.streamDescription.pointee
    }

    public func transform(_ packet: AudioPacket) -> AudioPacket {
        let inputData = packet.data

        let inputFrameCount = inputData.count / Int(sourceFormat.streamDescription.pointee.mBytesPerFrame)
        let outputFrameCount = Int(Double(inputFrameCount) * (targetFormat.sampleRate / sourceFormat.sampleRate))

        guard let inputBuffer = AVAudioPCMBuffer(
            pcmFormat: sourceFormat,
            frameCapacity: AVAudioFrameCount(inputFrameCount)
        ) else {
            return packet
        }

        inputData.withUnsafeBytes { bytes in
            let dest = inputBuffer.audioBufferList.pointee.mBuffers.mData!
            dest.copyMemory(from: bytes.baseAddress!, byteCount: inputData.count)
        }
        inputBuffer.frameLength = AVAudioFrameCount(inputFrameCount)

        guard let outputBuffer = AVAudioPCMBuffer(
            pcmFormat: targetFormat,
            frameCapacity: AVAudioFrameCount(outputFrameCount)
        ) else {
            return packet
        }

        var error: NSError?
        _ = avConverter.convert(to: outputBuffer, error: &error) { _, outStatus in
            outStatus.pointee = .haveData
            return inputBuffer
        }

        guard outputBuffer.frameLength > 0 else {
            return packet
        }

        let outputData = Data(
            bytes: outputBuffer.audioBufferList.pointee.mBuffers.mData!,
            count: Int(outputBuffer.frameLength * targetFormat.streamDescription.pointee.mBytesPerFrame)
        )

        return AudioPacket(
            timestamp: packet.timestamp,
            duration: packet.duration,
            data: outputData
        )
    }

    public static func toSampleRate(_ sampleRate: Double, from sourceFormat: AudioStreamBasicDescription) throws -> AudioFormatConverter {
        var targetFormat = AudioStreamBasicDescription()
        targetFormat.mSampleRate = sampleRate
        targetFormat.mFormatID = kAudioFormatLinearPCM
        targetFormat.mFormatFlags = kAudioFormatFlagIsPacked | kAudioFormatFlagIsSignedInteger
        targetFormat.mFramesPerPacket = 1
        targetFormat.mBitsPerChannel = 16
        targetFormat.mChannelsPerFrame = sourceFormat.mChannelsPerFrame
        targetFormat.mBytesPerFrame = (targetFormat.mBitsPerChannel / 8) * sourceFormat.mChannelsPerFrame
        targetFormat.mBytesPerPacket = targetFormat.mFramesPerPacket * targetFormat.mBytesPerFrame

        return try AudioFormatConverter(sourceFormat: sourceFormat, targetFormat: targetFormat)
    }

    public static func isValidSampleRate(_ sampleRate: Double) -> Bool {
        return [8000, 16000, 22050, 24000, 32000, 44100, 48000].contains(sampleRate)
    }
}
