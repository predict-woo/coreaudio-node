import AudioToolbox
import CoreAudio
import Foundation

public enum AudioFormatError: Error {
    case deviceNotReady(AudioObjectID)
    case formatUnavailable(AudioObjectID, OSStatus)

    var localizedDescription: String {
        switch self {
        case .deviceNotReady(let deviceID):
            return "Audio device \(deviceID) is not ready"
        case .formatUnavailable(let deviceID, let status):
            return "Failed to get stream format from device \(deviceID): OSStatus \(status)"
        }
    }
}

public class AudioFormatManager {
    public static func getDeviceFormat(deviceID: AudioObjectID) throws -> AudioStreamBasicDescription {
        let deviceReadyTimeout = 3.0  // Increased timeout
        let pollInterval = 0.1
        let maxPolls = Int(deviceReadyTimeout / pollInterval)

        var deviceReady = false
        for poll in 1...maxPolls {
            if isAudioDeviceValid(deviceID) {
                deviceReady = true
                break
            }

            if poll == maxPolls {
                break
            }

            Thread.sleep(forTimeInterval: pollInterval)
        }

        let maxRetries = 5  // Increased retries
        let retryDelayMs = 50  // Increased delay
        var lastStatus: OSStatus = noErr

        for attempt in 1...maxRetries {
            var propertyAddress = getPropertyAddress(
                selector: kAudioDevicePropertyStreamFormat,
                scope: kAudioDevicePropertyScopeInput
            )
            var propertySize = UInt32(MemoryLayout<AudioStreamBasicDescription>.stride)
            var streamFormat = AudioStreamBasicDescription()
            let status = AudioObjectGetPropertyData(
                deviceID, &propertyAddress, 0, nil, &propertySize, &streamFormat
            )

            if status == noErr {
                return streamFormat
            }

            lastStatus = status

            if attempt < maxRetries {
                Thread.sleep(forTimeInterval: Double(retryDelayMs) / 1000.0)
            }
        }

        throw AudioFormatError.formatUnavailable(deviceID, lastStatus)
    }
}
