import Foundation
import CoreAudio
import AVFoundation
import AppKit
import Darwin

public typealias MicActivityChangeCallback = @convention(c) (Bool, UnsafeMutableRawPointer?) -> Void
public typealias MicActivityDeviceCallback = @convention(c) (UnsafePointer<CChar>?, UnsafePointer<CChar>?, Bool, UnsafeMutableRawPointer?) -> Void
public typealias MicActivityErrorCallback = @convention(c) (UnsafePointer<CChar>?, UnsafeMutableRawPointer?) -> Void

class MicActivityMonitor {
    private var changeCallback: MicActivityChangeCallback?
    private var deviceCallback: MicActivityDeviceCallback?
    private var errorCallback: MicActivityErrorCallback?
    private var userContext: UnsafeMutableRawPointer?
    
    private var monitoredDevices: [AudioDeviceID: Bool] = [:]
    private var listenerBlocks: [AudioDeviceID: AudioObjectPropertyListenerBlock] = [:]
    private var isRunning = false
    private var monitorAllDevices = true
    private var lastAggregateState = false
    
    private let queue = DispatchQueue(label: "com.native-audio-node.mic-activity", qos: .userInitiated)
    
    init(
        changeCallback: MicActivityChangeCallback?,
        deviceCallback: MicActivityDeviceCallback?,
        errorCallback: MicActivityErrorCallback?,
        userContext: UnsafeMutableRawPointer?
    ) {
        self.changeCallback = changeCallback
        self.deviceCallback = deviceCallback
        self.errorCallback = errorCallback
        self.userContext = userContext
    }
    
    deinit {
        _ = stop()
    }
    
    func start(scope: String) -> Int32 {
        guard !isRunning else { return 0 }
        
        monitorAllDevices = (scope == "all")
        isRunning = true
        
        if monitorAllDevices {
            setupAllInputDeviceListeners()
        } else {
            setupDefaultInputDeviceListener()
        }
        
        return 0
    }
    
    func stop() -> Int32 {
        guard isRunning else { return 0 }
        
        isRunning = false
        removeAllListeners()
        monitoredDevices.removeAll()
        
        return 0
    }
    
    func isActive() -> Bool {
        return monitoredDevices.values.contains(true)
    }
    
    func getActiveDeviceIds() -> [String] {
        var activeIds: [String] = []
        for (deviceId, isActive) in monitoredDevices {
            if isActive, let uid = getDeviceUID(deviceId: deviceId) {
                activeIds.append(uid)
            }
        }
        return activeIds
    }
    
    private func getProcessName(pid: pid_t) -> String? {
        let maxPathSize = 4096
        let pathBuffer = UnsafeMutablePointer<CChar>.allocate(capacity: maxPathSize)
        defer { pathBuffer.deallocate() }
        
        let pathLength = proc_pidpath(pid, pathBuffer, UInt32(maxPathSize))
        if pathLength > 0 {
            let path = String(cString: pathBuffer)
            return (path as NSString).lastPathComponent
        }
        return nil
    }
    
    func getActiveProcesses() -> [(pid: pid_t, name: String, bundleId: String)] {
        var result: [(pid: pid_t, name: String, bundleId: String)] = []
        
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyProcessObjectList,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        var dataSize: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0, nil,
            &dataSize
        )
        
        guard status == noErr && dataSize > 0 else { return result }
        
        let count = Int(dataSize) / MemoryLayout<AudioObjectID>.size
        var processIDs = [AudioObjectID](repeating: 0, count: count)
        
        status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0, nil,
            &dataSize,
            &processIDs
        )
        
        guard status == noErr else { return result }
        
        for procID in processIDs {
            var inputAddr = AudioObjectPropertyAddress(
                mSelector: kAudioProcessPropertyIsRunningInput,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain
            )
            var isRunningInput: UInt32 = 0
            var inputSize = UInt32(MemoryLayout<UInt32>.size)
            let inputStatus = AudioObjectGetPropertyData(procID, &inputAddr, 0, nil, &inputSize, &isRunningInput)
            
            guard inputStatus == noErr && isRunningInput != 0 else { continue }
            
            var pidAddr = AudioObjectPropertyAddress(
                mSelector: kAudioProcessPropertyPID,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain
            )
            var pid: pid_t = -1
            var pidSize = UInt32(MemoryLayout<pid_t>.size)
            AudioObjectGetPropertyData(procID, &pidAddr, 0, nil, &pidSize, &pid)
            
            var bundleAddr = AudioObjectPropertyAddress(
                mSelector: kAudioProcessPropertyBundleID,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain
            )
            var bundleRef: Unmanaged<CFString>?
            var bundleSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
            AudioObjectGetPropertyData(procID, &bundleAddr, 0, nil, &bundleSize, &bundleRef)
            let bundleId = (bundleRef?.takeUnretainedValue() as String?) ?? ""
            
            let app = NSRunningApplication(processIdentifier: pid)
            var name = app?.localizedName
            
            if name == nil {
                name = getProcessName(pid: pid)
            }
            
            result.append((pid: pid, name: name ?? "PID \(pid)", bundleId: bundleId))
        }
        
        return result
    }
    
    private func setupAllInputDeviceListeners() {
        let inputDevices = getInputDevices()
        for deviceId in inputDevices {
            setupDeviceListener(deviceId: deviceId)
        }
        
        setupDeviceListChangeListener()
    }
    
    private func setupDefaultInputDeviceListener() {
        guard let defaultDeviceId = getDefaultInputDeviceID() else {
            emitError("No default input device found")
            return
        }
        setupDeviceListener(deviceId: defaultDeviceId)
    }
    
    private func setupDeviceListener(deviceId: AudioDeviceID) {
        guard listenerBlocks[deviceId] == nil else { return }
        
        let isRunning = checkDeviceIsRunning(deviceId: deviceId)
        let previousState = monitoredDevices[deviceId]
        monitoredDevices[deviceId] = isRunning
        
        if previousState != isRunning {
            emitDeviceChange(deviceId: deviceId, isActive: isRunning)
            checkAndEmitAggregateChange()
        }
        
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceIsRunningSomewhere,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        let listenerBlock: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            self?.queue.async {
                self?.handleDeviceRunningChange(deviceId: deviceId)
            }
        }
        
        let status = AudioObjectAddPropertyListenerBlock(
            deviceId,
            &propertyAddress,
            queue,
            listenerBlock
        )
        
        if status == noErr {
            listenerBlocks[deviceId] = listenerBlock
        } else {
            emitError("Failed to add listener for device \(deviceId): \(status)")
        }
    }
    
    private func setupDeviceListChangeListener() {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        let listenerBlock: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            self?.queue.async {
                self?.handleDeviceListChange()
            }
        }
        
        let status = AudioObjectAddPropertyListenerBlock(
            AudioObjectID(kAudioObjectSystemObject),
            &propertyAddress,
            queue,
            listenerBlock
        )
        
        if status != noErr {
            emitError("Failed to add device list listener: \(status)")
        }
    }
    
    private func handleDeviceRunningChange(deviceId: AudioDeviceID) {
        let isRunning = checkDeviceIsRunning(deviceId: deviceId)
        let previousState = monitoredDevices[deviceId]
        
        guard previousState != isRunning else { return }
        
        monitoredDevices[deviceId] = isRunning
        emitDeviceChange(deviceId: deviceId, isActive: isRunning)
        checkAndEmitAggregateChange()
    }
    
    private func handleDeviceListChange() {
        guard monitorAllDevices else { return }
        
        let currentInputDevices = Set(getInputDevices())
        let monitoredDeviceIds = Set(monitoredDevices.keys)
        
        let newDevices = currentInputDevices.subtracting(monitoredDeviceIds)
        for deviceId in newDevices {
            setupDeviceListener(deviceId: deviceId)
        }
        
        let removedDevices = monitoredDeviceIds.subtracting(currentInputDevices)
        for deviceId in removedDevices {
            removeDeviceListener(deviceId: deviceId)
            if monitoredDevices[deviceId] == true {
                monitoredDevices.removeValue(forKey: deviceId)
                checkAndEmitAggregateChange()
            } else {
                monitoredDevices.removeValue(forKey: deviceId)
            }
        }
    }
    
    private func removeDeviceListener(deviceId: AudioDeviceID) {
        guard let listenerBlock = listenerBlocks[deviceId] else { return }
        
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceIsRunningSomewhere,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        AudioObjectRemovePropertyListenerBlock(
            deviceId,
            &propertyAddress,
            queue,
            listenerBlock
        )
        
        listenerBlocks.removeValue(forKey: deviceId)
    }
    
    private func removeAllListeners() {
        for deviceId in listenerBlocks.keys {
            removeDeviceListener(deviceId: deviceId)
        }
    }
    
    private func checkDeviceIsRunning(deviceId: AudioDeviceID) -> Bool {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceIsRunningSomewhere,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        var isRunning: UInt32 = 0
        var dataSize = UInt32(MemoryLayout<UInt32>.size)
        
        let status = AudioObjectGetPropertyData(
            deviceId,
            &propertyAddress,
            0, nil,
            &dataSize,
            &isRunning
        )
        
        return status == noErr && isRunning > 0
    }
    
    private func getInputDevices() -> [AudioDeviceID] {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        var dataSize: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &propertyAddress,
            0, nil,
            &dataSize
        )
        
        guard status == noErr else { return [] }
        
        let deviceCount = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIds = [AudioDeviceID](repeating: 0, count: deviceCount)
        
        status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &propertyAddress,
            0, nil,
            &dataSize,
            &deviceIds
        )
        
        guard status == noErr else { return [] }
        
        return deviceIds.filter { hasInputChannels(deviceId: $0) }
    }
    
    private func hasInputChannels(deviceId: AudioDeviceID) -> Bool {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreamConfiguration,
            mScope: kAudioDevicePropertyScopeInput,
            mElement: kAudioObjectPropertyElementMain
        )
        
        var dataSize: UInt32 = 0
        let status = AudioObjectGetPropertyDataSize(
            deviceId,
            &propertyAddress,
            0, nil,
            &dataSize
        )
        
        return status == noErr && dataSize > 0
    }
    
    private func getDefaultInputDeviceID() -> AudioDeviceID? {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultInputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        var deviceId: AudioDeviceID = 0
        var dataSize = UInt32(MemoryLayout<AudioDeviceID>.size)
        
        let status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &propertyAddress,
            0, nil,
            &dataSize,
            &deviceId
        )
        
        return status == noErr && deviceId != kAudioObjectUnknown ? deviceId : nil
    }
    
    private func getDeviceUID(deviceId: AudioDeviceID) -> String? {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceUID,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        var uid: Unmanaged<CFString>?
        var dataSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        
        let status = withUnsafeMutablePointer(to: &uid) { uidPtr in
            AudioObjectGetPropertyData(
                deviceId,
                &propertyAddress,
                0, nil,
                &dataSize,
                uidPtr
            )
        }
        
        guard status == noErr, let cfString = uid?.takeUnretainedValue() else {
            return nil
        }
        return cfString as String
    }
    
    private func getDeviceName(deviceId: AudioDeviceID) -> String? {
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceNameCFString,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        
        var name: Unmanaged<CFString>?
        var dataSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        
        let status = withUnsafeMutablePointer(to: &name) { namePtr in
            AudioObjectGetPropertyData(
                deviceId,
                &propertyAddress,
                0, nil,
                &dataSize,
                namePtr
            )
        }
        
        guard status == noErr, let cfString = name?.takeUnretainedValue() else {
            return nil
        }
        return cfString as String
    }
    
    private func checkAndEmitAggregateChange() {
        let currentState = isActive()
        if currentState != lastAggregateState {
            lastAggregateState = currentState
            changeCallback?(currentState, userContext)
        }
    }
    
    private func emitDeviceChange(deviceId: AudioDeviceID, isActive: Bool) {
        guard let uid = getDeviceUID(deviceId: deviceId) else { return }
        let name = getDeviceName(deviceId: deviceId) ?? "Unknown"
        
        uid.withCString { uidPtr in
            name.withCString { namePtr in
                deviceCallback?(uidPtr, namePtr, isActive, userContext)
            }
        }
    }
    
    private func emitError(_ message: String) {
        message.withCString { messagePtr in
            errorCallback?(messagePtr, userContext)
        }
    }
}

// MARK: - C Bridge

private var monitors: [UnsafeMutableRawPointer: MicActivityMonitor] = [:]
private let monitorsLock = NSLock()

@_cdecl("mic_activity_create")
public func mic_activity_create(
    changeCallback: MicActivityChangeCallback?,
    deviceCallback: MicActivityDeviceCallback?,
    errorCallback: MicActivityErrorCallback?,
    userContext: UnsafeMutableRawPointer?
) -> UnsafeMutableRawPointer? {
    let monitor = MicActivityMonitor(
        changeCallback: changeCallback,
        deviceCallback: deviceCallback,
        errorCallback: errorCallback,
        userContext: userContext
    )
    
    let pointer = Unmanaged.passRetained(monitor).toOpaque()
    
    monitorsLock.lock()
    monitors[pointer] = monitor
    monitorsLock.unlock()
    
    return pointer
}

@_cdecl("mic_activity_start")
public func mic_activity_start(
    handle: UnsafeMutableRawPointer?,
    scope: UnsafePointer<CChar>?
) -> Int32 {
    guard let handle = handle else { return -1 }
    
    monitorsLock.lock()
    let monitor = monitors[handle]
    monitorsLock.unlock()
    
    guard let monitor = monitor else { return -1 }
    
    let scopeStr = scope.map { String(cString: $0) } ?? "all"
    return monitor.start(scope: scopeStr)
}

@_cdecl("mic_activity_stop")
public func mic_activity_stop(handle: UnsafeMutableRawPointer?) -> Int32 {
    guard let handle = handle else { return -1 }
    
    monitorsLock.lock()
    let monitor = monitors[handle]
    monitorsLock.unlock()
    
    guard let monitor = monitor else { return -1 }
    
    return monitor.stop()
}

@_cdecl("mic_activity_destroy")
public func mic_activity_destroy(handle: UnsafeMutableRawPointer?) {
    guard let handle = handle else { return }
    
    monitorsLock.lock()
    if let monitor = monitors.removeValue(forKey: handle) {
        _ = monitor.stop()
    }
    monitorsLock.unlock()
    
    Unmanaged<MicActivityMonitor>.fromOpaque(handle).release()
}

@_cdecl("mic_activity_is_active")
public func mic_activity_is_active(handle: UnsafeMutableRawPointer?) -> Bool {
    guard let handle = handle else { return false }
    
    monitorsLock.lock()
    let monitor = monitors[handle]
    monitorsLock.unlock()
    
    return monitor?.isActive() ?? false
}

@_cdecl("mic_activity_get_active_device_ids")
public func mic_activity_get_active_device_ids(
    handle: UnsafeMutableRawPointer?,
    deviceIds: UnsafeMutablePointer<UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?>?,
    count: UnsafeMutablePointer<Int32>?
) -> Int32 {
    guard let handle = handle,
          let deviceIds = deviceIds,
          let count = count else {
        return -1
    }
    
    monitorsLock.lock()
    let monitor = monitors[handle]
    monitorsLock.unlock()
    
    guard let monitor = monitor else {
        deviceIds.pointee = nil
        count.pointee = 0
        return -1
    }
    
    let activeIds = monitor.getActiveDeviceIds()
    
    if activeIds.isEmpty {
        deviceIds.pointee = nil
        count.pointee = 0
        return 0
    }
    
    let arrayPtr = UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>.allocate(capacity: activeIds.count)
    
    for (index, uid) in activeIds.enumerated() {
        arrayPtr[index] = strdup(uid)
    }
    
    deviceIds.pointee = arrayPtr
    count.pointee = Int32(activeIds.count)
    
    return 0
}

@_cdecl("mic_activity_free_device_ids")
public func mic_activity_free_device_ids(
    deviceIds: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?,
    count: Int32
) {
    guard let deviceIds = deviceIds else { return }
    
    for i in 0..<Int(count) {
        if let ptr = deviceIds[i] {
            free(ptr)
        }
    }
    
    deviceIds.deallocate()
}

@_cdecl("mic_activity_get_active_processes")
public func mic_activity_get_active_processes(
    handle: UnsafeMutableRawPointer?,
    pids: UnsafeMutablePointer<UnsafeMutablePointer<Int32>?>?,
    names: UnsafeMutablePointer<UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?>?,
    bundleIds: UnsafeMutablePointer<UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?>?,
    count: UnsafeMutablePointer<Int32>?
) -> Int32 {
    guard let handle = handle,
          let pids = pids,
          let names = names,
          let bundleIds = bundleIds,
          let count = count else {
        return -1
    }
    
    monitorsLock.lock()
    let monitor = monitors[handle]
    monitorsLock.unlock()
    
    guard let monitor = monitor else {
        pids.pointee = nil
        names.pointee = nil
        bundleIds.pointee = nil
        count.pointee = 0
        return -1
    }
    
    let activeProcesses = monitor.getActiveProcesses()
    
    if activeProcesses.isEmpty {
        pids.pointee = nil
        names.pointee = nil
        bundleIds.pointee = nil
        count.pointee = 0
        return 0
    }
    
    let pidsPtr = UnsafeMutablePointer<Int32>.allocate(capacity: activeProcesses.count)
    let namesPtr = UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>.allocate(capacity: activeProcesses.count)
    let bundleIdsPtr = UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>.allocate(capacity: activeProcesses.count)
    
    for (index, proc) in activeProcesses.enumerated() {
        pidsPtr[index] = proc.pid
        namesPtr[index] = strdup(proc.name)
        bundleIdsPtr[index] = strdup(proc.bundleId)
    }
    
    pids.pointee = pidsPtr
    names.pointee = namesPtr
    bundleIds.pointee = bundleIdsPtr
    count.pointee = Int32(activeProcesses.count)
    
    return 0
}

@_cdecl("mic_activity_free_processes")
public func mic_activity_free_processes(
    pids: UnsafeMutablePointer<Int32>?,
    names: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?,
    bundleIds: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?,
    count: Int32
) {
    if let pids = pids {
        pids.deallocate()
    }
    
    if let names = names {
        for i in 0..<Int(count) {
            if let ptr = names[i] {
                free(ptr)
            }
        }
        names.deallocate()
    }
    
    if let bundleIds = bundleIds {
        for i in 0..<Int(count) {
            if let ptr = bundleIds[i] {
                free(ptr)
            }
        }
        bundleIds.deallocate()
    }
}
