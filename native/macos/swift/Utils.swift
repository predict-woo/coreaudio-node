import AVFoundation
import AudioToolbox
import CoreAudio
import Foundation

func isAudioDeviceValid(_ deviceID: AudioObjectID) -> Bool {
    var address = getPropertyAddress(selector: kAudioDevicePropertyDeviceIsAlive)

    var isAlive: UInt32 = 0
    var size = UInt32(MemoryLayout<UInt32>.size)
    let status = AudioObjectGetPropertyData(deviceID, &address, 0, nil, &size, &isAlive)

    return status == kAudioHardwareNoError && isAlive == 1
}

func getPropertyAddress(
    selector: AudioObjectPropertySelector,
    scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal,
    element: AudioObjectPropertyElement = kAudioObjectPropertyElementMain
) -> AudioObjectPropertyAddress {
    return AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: element)
}

func translatePIDsToProcessObjects(_ pids: [Int32]) throws -> [AudioObjectID] {
    guard !pids.isEmpty else {
        return []
    }

    var processObjects: [AudioObjectID] = []
    var failedPIDs: [Int32] = []

    for pid in pids {
        var address = getPropertyAddress(selector: kAudioHardwarePropertyTranslatePIDToProcessObject)
        var processObject: AudioObjectID = 0
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        var mutablePid = pid

        let status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            UInt32(MemoryLayout<pid_t>.size),
            &mutablePid,
            &size,
            &processObject
        )

        if status == kAudioHardwareNoError && processObject != kAudioObjectUnknown {
            processObjects.append(processObject)
        } else {
            failedPIDs.append(pid)
        }
    }

    if !failedPIDs.isEmpty {
        throw AudioTeeError.pidTranslationFailed(failedPIDs)
    }

    return processObjects
}
