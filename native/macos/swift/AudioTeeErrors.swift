import Foundation

public enum AudioTeeError: Error {
    case setupFailed
    case tapCreationFailed(OSStatus)
    case aggregateDeviceCreationFailed(OSStatus)
    case tapAssignmentFailed(OSStatus)
    case pidTranslationFailed([Int32])
}

public enum AudioConverterError: Error {
    case invalidFormat
    case creationFailed
}
