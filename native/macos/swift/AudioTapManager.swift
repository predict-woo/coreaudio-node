import AVFoundation
import AudioToolbox
import CoreAudio
import Foundation

class AudioTapManager {
    private var tapID: AudioObjectID?
    private var deviceID: AudioObjectID?

    deinit {
        if let tapID = tapID {
            AudioHardwareDestroyProcessTap(tapID)
            self.tapID = nil
        }

        if let deviceID = deviceID {
            AudioHardwareDestroyAggregateDevice(deviceID)
            self.deviceID = nil
        }
    }

    /// Sets up the audio tap and aggregate device
    func setupAudioTap(with config: TapConfiguration) throws {
        tapID = try createSystemAudioTap(with: config)
        deviceID = try createAggregateDevice()

        guard let tapID = tapID, let deviceID = deviceID else {
            throw AudioTeeError.setupFailed
        }

        try addTapToAggregateDevice(tapID: tapID, deviceID: deviceID)
    }

    /// Returns the aggregate device ID for recording
    func getDeviceID() -> AudioObjectID? {
        return deviceID
    }

    private func createSystemAudioTap(with config: TapConfiguration) throws -> AudioObjectID {
        let description = CATapDescription()

        description.name = "audiotee-tap"
        description.processes = try translatePIDsToProcessObjects(config.processes)
        description.isPrivate = true
        description.muteBehavior = config.muteBehavior.coreAudioValue
        description.isMixdown = true
        description.isMono = config.isMono
        description.isExclusive = config.isExclusive
        description.deviceUID = nil
        description.stream = 0

        var tapID = AudioObjectID(kAudioObjectUnknown)
        let status = AudioHardwareCreateProcessTap(description, &tapID)

        guard status == kAudioHardwareNoError else {
            throw AudioTeeError.tapCreationFailed(status)
        }

        return tapID
    }

    private func createAggregateDevice() throws -> AudioObjectID {
        let uid = UUID().uuidString
        let description = [
            kAudioAggregateDeviceNameKey: "audiotee-aggregate-device",
            kAudioAggregateDeviceUIDKey: uid,
            kAudioAggregateDeviceSubDeviceListKey: [] as CFArray,
            kAudioAggregateDeviceMasterSubDeviceKey: 0,
            kAudioAggregateDeviceIsPrivateKey: true,
            kAudioAggregateDeviceIsStackedKey: false,
        ] as [String: Any]

        var deviceID: AudioObjectID = 0
        let status = AudioHardwareCreateAggregateDevice(description as CFDictionary, &deviceID)

        guard status == kAudioHardwareNoError else {
            throw AudioTeeError.aggregateDeviceCreationFailed(status)
        }

        return deviceID
    }

    private func addTapToAggregateDevice(tapID: AudioObjectID, deviceID: AudioObjectID) throws {
        // Get the tap's UID
        var propertyAddress = getPropertyAddress(selector: kAudioTapPropertyUID)
        var propertySize = UInt32(MemoryLayout<CFString>.stride)
        var tapUID: CFString = "" as CFString
        _ = withUnsafeMutablePointer(to: &tapUID) { tapUID in
            AudioObjectGetPropertyData(tapID, &propertyAddress, 0, nil, &propertySize, tapUID)
        }

        // Add the tap to the aggregate device
        propertyAddress = getPropertyAddress(selector: kAudioAggregateDevicePropertyTapList)
        let tapArray = [tapUID] as CFArray
        propertySize = UInt32(MemoryLayout<CFArray>.stride)

        let status = withUnsafePointer(to: tapArray) { ptr in
            AudioObjectSetPropertyData(deviceID, &propertyAddress, 0, nil, propertySize, ptr)
        }

        guard status == kAudioHardwareNoError else {
            throw AudioTeeError.tapAssignmentFailed(status)
        }
    }
}
