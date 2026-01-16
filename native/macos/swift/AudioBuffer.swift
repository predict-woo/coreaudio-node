import CoreAudio
import Foundation

public class AudioBuffer {
    private var buffer: [UInt8]
    private var writeIndex: Int = 0
    private var readIndex: Int = 0
    private var availableBytes: Int = 0
    private let maxBufferSize: Int

    private let bytesPerChunk: Int
    private let chunkDuration: Double

    public init(format: AudioStreamBasicDescription, chunkDuration: Double = 0.2) {
        // Pre-calculate chunk parameters
        let bytesPerFrame = Int(format.mBytesPerFrame)
        let samplesPerChunk = Int(format.mSampleRate * chunkDuration)
        self.bytesPerChunk = samplesPerChunk * bytesPerFrame
        self.chunkDuration = Double(samplesPerChunk) / format.mSampleRate

        // Calculate max buffer size to hold ~10 seconds of audio
        let bytesPerSecond = Int(format.mSampleRate) * bytesPerFrame
        self.maxBufferSize = bytesPerSecond * 10

        // Pre-allocated ring buffer
        self.buffer = Array(repeating: 0, count: maxBufferSize)
    }

    public func append(_ data: Data) {
        guard availableBytes + data.count <= maxBufferSize else {
            return
        }

        data.withUnsafeBytes { bytes in
            let sourceBytes = bytes.bindMemory(to: UInt8.self)
            let dataSize = sourceBytes.count

            if writeIndex + dataSize <= maxBufferSize {
                buffer.replaceSubrange(writeIndex..<writeIndex + dataSize, with: sourceBytes)
                writeIndex = (writeIndex + dataSize) % maxBufferSize
            } else {
                let firstChunkSize = maxBufferSize - writeIndex
                let secondChunkSize = dataSize - firstChunkSize

                buffer.replaceSubrange(writeIndex..<maxBufferSize, with: sourceBytes.prefix(firstChunkSize))
                buffer.replaceSubrange(0..<secondChunkSize, with: sourceBytes.suffix(secondChunkSize))

                writeIndex = secondChunkSize
            }
        }

        availableBytes += data.count
    }

    public func processChunks() -> [AudioPacket] {
        var packets: [AudioPacket] = []

        while let packet = nextChunk() {
            packets.append(packet)
        }

        return packets
    }

    private func nextChunk() -> AudioPacket? {
        guard availableBytes >= bytesPerChunk else { return nil }

        var chunkData = Data(capacity: bytesPerChunk)

        if readIndex + bytesPerChunk <= maxBufferSize {
            chunkData.append(contentsOf: buffer[readIndex..<readIndex + bytesPerChunk])
            readIndex = (readIndex + bytesPerChunk) % maxBufferSize
        } else {
            let firstChunkSize = maxBufferSize - readIndex
            let secondChunkSize = bytesPerChunk - firstChunkSize

            chunkData.append(contentsOf: buffer[readIndex..<maxBufferSize])
            chunkData.append(contentsOf: buffer[0..<secondChunkSize])

            readIndex = secondChunkSize
        }

        availableBytes -= bytesPerChunk

        return AudioPacket(
            timestamp: Date(),
            duration: chunkDuration,
            data: chunkData
        )
    }
}
