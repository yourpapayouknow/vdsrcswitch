import Foundation
import IOKit
import CoreGraphics

public typealias IOAVService = AnyObject

// Dynamic Loader Helpers for Private CoreDisplay APIs
private let coreDisplayLib = dlopen("/System/Library/Frameworks/CoreDisplay.framework/CoreDisplay", RTLD_LAZY)

private func getCDSymbol<T>(_ name: String, type: T.Type) -> T? {
    guard let lib = coreDisplayLib else { return nil }
    if let sym = dlsym(lib, name) {
        return unsafeBitCast(sym, to: T.self)
    }
    return nil
}

private let fIOAVServiceCreateWithService = getCDSymbol("IOAVServiceCreateWithService", type: (@convention(c) (CFAllocator?, io_service_t) -> Unmanaged<IOAVService>?).self)
private let fIOAVServiceWriteI2C = getCDSymbol("IOAVServiceWriteI2C", type: (@convention(c) (IOAVService, UInt32, UInt32, UnsafePointer<UInt8>, UInt32) -> IOReturn).self)
private let fIOAVServiceReadI2C = getCDSymbol("IOAVServiceReadI2C", type: (@convention(c) (IOAVService, UInt32, UInt32, UnsafeMutablePointer<UInt8>, UInt32) -> IOReturn).self)
private let fCoreDisplay_DisplayCreateInfoDictionary = getCDSymbol("CoreDisplay_DisplayCreateInfoDictionary", type: (@convention(c) (CGDirectDisplayID) -> Unmanaged<CFDictionary>?).self)

// AppleSiliconDDC Implementation ported to Pure Swift with Dynamic Loading
public class AppleSiliconDDC: NSObject {
    static let ARM64_DDC_7BIT_ADDRESS: UInt8 = 0x37
    static let ARM64_DDC_DATA_ADDRESS: UInt8 = 0x51
    static let MAX_MATCH_SCORE: Int = 20

    public struct IOregService {
        public var edidUUID: String = ""
        public var manufacturerID: String = ""
        public var productName: String = ""
        public var serialNumber: Int64 = 0
        public var alphanumericSerialNumber: String = ""
        public var location: String = ""
        public var ioDisplayLocation: String = ""
        public var transportUpstream: String = ""
        public var transportDownstream: String = ""
        public var service: IOAVService?
        public var serviceLocation: Int = 0
        public var displayAttributes: NSDictionary?
    }

    public struct Arm64Service {
        var displayID: CGDirectDisplayID = 0
        var service: IOAVService?
        var serviceLocation: Int = 0
        var discouraged: Bool = false
        var dummy: Bool = false
        var serviceDetails: IOregService
        var matchScore: Int = 0
    }

    static func getServiceMatches(displayIDs: [CGDirectDisplayID]) -> [Arm64Service] {
        let ioregServicesForMatching = self.getIoregServicesForMatching()
        var matchedDisplayServices: [Arm64Service] = []
        var scoredCandidateDisplayServices: [Int: [Arm64Service]] = [:]
        for displayID in displayIDs {
            for ioregServiceForMatching in ioregServicesForMatching {
                let score = self.ioregMatchScore(displayID: displayID, ioregEdidUUID: ioregServiceForMatching.edidUUID, ioDisplayLocation: ioregServiceForMatching.ioDisplayLocation, ioregProductName: ioregServiceForMatching.productName, ioregSerialNumber: ioregServiceForMatching.serialNumber, serviceLocation: ioregServiceForMatching.serviceLocation)
                let discouraged = self.checkIfDiscouraged(ioregService: ioregServiceForMatching)
                let dummy = self.checkIfDummy(ioregService: ioregServiceForMatching)
                let displayService = Arm64Service(displayID: displayID, service: ioregServiceForMatching.service, serviceLocation: ioregServiceForMatching.serviceLocation, discouraged: discouraged, dummy: dummy, serviceDetails: ioregServiceForMatching, matchScore: score)
                if scoredCandidateDisplayServices[score] == nil {
                    scoredCandidateDisplayServices[score] = []
                }
                scoredCandidateDisplayServices[score]?.append(displayService)
            }
        }
        var takenServiceLocations: [Int] = []
        var takenDisplayIDs: [CGDirectDisplayID] = []
        for score in stride(from: self.MAX_MATCH_SCORE, to: 0, by: -1) {
            if let scoredCandidateDisplayService = scoredCandidateDisplayServices[score] {
                for candidateDisplayService in scoredCandidateDisplayService where !(takenDisplayIDs.contains(candidateDisplayService.displayID) || takenServiceLocations.contains(candidateDisplayService.serviceLocation)) {
                    takenDisplayIDs.append(candidateDisplayService.displayID)
                    takenServiceLocations.append(candidateDisplayService.serviceLocation)
                    matchedDisplayServices.append(candidateDisplayService)
                }
            }
        }
        return matchedDisplayServices
    }

    static public func read(service: IOAVService?, command: UInt8, writeSleepTime: UInt32? = nil, numOfWriteCycles: UInt8? = nil, readSleepTime: UInt32? = nil, numOfRetryAttemps: UInt8? = nil, retrySleepTime: UInt32? = nil) -> (current: UInt16, max: UInt16)? {
        var values: (UInt16, UInt16)?
        var send: [UInt8] = [command]
        var reply = [UInt8](repeating: 0, count: 11)
        if Self.performDDCCommunication(service: service, send: &send, reply: &reply, writeSleepTime: writeSleepTime, numOfWriteCycles: numOfWriteCycles, readSleepTime: readSleepTime, numOfRetryAttemps: numOfRetryAttemps, retrySleepTime: retrySleepTime) {
            let max = UInt16(reply[6]) * 256 + UInt16(reply[7])
            let current = UInt16(reply[8]) * 256 + UInt16(reply[9])
            values = (current, max)
        } else {
            values = nil
        }
        return values
    }

    static public func write(service: IOAVService?, command: UInt8, value: UInt16, writeSleepTime: UInt32? = nil, numOfWriteCycles: UInt8? = nil, numOfRetryAttemps: UInt8? = nil, retrySleepTime: UInt32? = nil) -> Bool {
        var send: [UInt8] = [command, UInt8(value >> 8), UInt8(value & 255)]
        var reply: [UInt8] = []
        return Self.performDDCCommunication(service: service, send: &send, reply: &reply, writeSleepTime: writeSleepTime, numOfWriteCycles: numOfWriteCycles, numOfRetryAttemps: numOfRetryAttemps, retrySleepTime: retrySleepTime)
    }

    static func performDDCCommunication(service: IOAVService?, send: inout [UInt8], reply: inout [UInt8], writeSleepTime: UInt32? = nil, numOfWriteCycles: UInt8? = nil, readSleepTime: UInt32? = nil, numOfRetryAttemps: UInt8? = nil, retrySleepTime: UInt32? = nil) -> Bool {
        let dataAddress = ARM64_DDC_DATA_ADDRESS
        var success = false
        guard service != nil else {
            return success
        }
        var packet: [UInt8] = [UInt8(0x80 | (send.count + 1)), UInt8(send.count)] + send + [0]
        packet[packet.count - 1] = self.checksum(chk: send.count == 1 ? ARM64_DDC_7BIT_ADDRESS << 1 : (ARM64_DDC_7BIT_ADDRESS << 1) ^ dataAddress, data: &packet, start: 0, end: packet.count - 2)
        for _ in 1 ... (numOfRetryAttemps ?? 4) + 1 {
            for _ in 1 ... max((numOfWriteCycles ?? 2) + 0, 1) {
                usleep(writeSleepTime ?? 10000)
                if let writeI2C = fIOAVServiceWriteI2C {
                    success = writeI2C(service!, UInt32(ARM64_DDC_7BIT_ADDRESS), UInt32(dataAddress), &packet, UInt32(packet.count)) == 0
                }
            }
            if !reply.isEmpty {
                usleep(readSleepTime ?? 50000)
                if let readI2C = fIOAVServiceReadI2C {
                    if readI2C(service!, UInt32(ARM64_DDC_7BIT_ADDRESS), UInt32(dataAddress), &reply, UInt32(reply.count)) == 0 {
                        success = self.checksum(chk: 0x50, data: &reply, start: 0, end: reply.count - 2) == reply[reply.count - 1]
                    }
                }
            }
            if success {
                return success
            }
            usleep(retrySleepTime ?? 20000)
        }
        return success
    }

    static func checksum(chk: UInt8, data: inout [UInt8], start: Int, end: Int) -> UInt8 {
        var chkd: UInt8 = chk
        for i in start ... end {
            chkd ^= data[i]
        }
        return chkd
    }

    static func ioregMatchScore(displayID: CGDirectDisplayID, ioregEdidUUID: String, ioDisplayLocation: String = "", ioregProductName: String = "", ioregSerialNumber: Int64 = 0, serviceLocation _: Int = 0) -> Int {
        var matchScore = 0
        if let getInfoDict = fCoreDisplay_DisplayCreateInfoDictionary, let dictionary = getInfoDict(displayID)?.takeRetainedValue() as NSDictionary? {
            if let kDisplayYearOfManufacture = dictionary[kDisplayYearOfManufacture] as? Int64,
               let kDisplayWeekOfManufacture = dictionary[kDisplayWeekOfManufacture] as? Int64,
               let kDisplayVendorID = dictionary[kDisplayVendorID] as? Int64,
               let kDisplayProductID = dictionary[kDisplayProductID] as? Int64,
               let kDisplayVerticalImageSize = dictionary[kDisplayVerticalImageSize] as? Int64,
               let kDisplayHorizontalImageSize = dictionary[kDisplayHorizontalImageSize] as? Int64 {
                struct KeyLoc {
                    var key: String
                    var loc: Int
                }
                let edidUUIDSearchKeys: [KeyLoc] = [
                    KeyLoc(key: String(format: "%04x", UInt16(max(0, min(kDisplayVendorID, 256 * 256 - 1)))).uppercased(), loc: 0),
                    KeyLoc(key: String(format: "%02x", UInt8((UInt16(max(0, min(kDisplayProductID, 256 * 256 - 1))) >> (0 * 8)) & 0xFF)).uppercased()
                        + String(format: "%02x", UInt8((UInt16(max(0, min(kDisplayProductID, 256 * 256 - 1))) >> (1 * 8)) & 0xFF)).uppercased(), loc: 4),
                    KeyLoc(key: String(format: "%02x", UInt8(max(0, min(kDisplayWeekOfManufacture, 256 - 1)))).uppercased()
                        + String(format: "%02x", UInt8(max(0, min(kDisplayYearOfManufacture - 1990, 256 - 1)))).uppercased(), loc: 19),
                    KeyLoc(key: String(format: "%02x", UInt8(max(0, min(kDisplayHorizontalImageSize / 10, 256 - 1)))).uppercased()
                        + String(format: "%02x", UInt8(max(0, min(kDisplayVerticalImageSize / 10, 256 - 1)))).uppercased(), loc: 30),
                ]
                for searchKey in edidUUIDSearchKeys where searchKey.key != "0000" && searchKey.key == ioregEdidUUID.prefix(searchKey.loc + 4).suffix(4) {
                    matchScore += 1
                }
            }
            if ioDisplayLocation != "", let kIODisplayLocation = dictionary[kIODisplayLocationKey] as? String, ioDisplayLocation == kIODisplayLocation {
                matchScore += 10
            }
            if ioregProductName != "", let nameList = dictionary["DisplayProductName"] as? [String: String], let name = nameList["en_US"] ?? nameList.first?.value, name.lowercased() == ioregProductName.lowercased() {
                matchScore += 1
            }
            if ioregSerialNumber != 0, let serial = dictionary[kDisplaySerialNumber] as? Int64, serial == ioregSerialNumber {
                matchScore += 1
            }
        }
        return matchScore
    }

    static func ioregIterateToNextObjectOfInterest(interests: [String], iterator: inout io_iterator_t) -> (name: String, entry: io_service_t, preceedingEntry: io_service_t)? {
        var entry: io_service_t = IO_OBJECT_NULL
        var preceedingEntry: io_service_t = IO_OBJECT_NULL
        let name = UnsafeMutablePointer<CChar>.allocate(capacity: MemoryLayout<io_name_t>.size)
        defer {
            name.deallocate()
        }
        while true {
            preceedingEntry = entry
            entry = IOIteratorNext(iterator)
            guard IORegistryEntryGetName(entry, name) == KERN_SUCCESS, entry != MACH_PORT_NULL else {
                break
            }
            let nameString = String(cString: name)
            for interest in interests where entry != IO_OBJECT_NULL && nameString.contains(interest) {
                return (nameString, entry, preceedingEntry)
            }
        }
        return nil
    }

    static func getIORegServiceAppleCDC2Properties(entry: io_service_t) -> IOregService {
        var ioregService = IOregService()
        if let unmanagedEdidUUID = IORegistryEntryCreateCFProperty(entry, "EDID UUID" as CFString, kCFAllocatorDefault, IOOptionBits(kIORegistryIterateRecursively)), let edidUUID = unmanagedEdidUUID.takeRetainedValue() as? String {
            ioregService.edidUUID = edidUUID
        }
        let cpath = UnsafeMutablePointer<CChar>.allocate(capacity: MemoryLayout<io_string_t>.size)
        IORegistryEntryGetPath(entry, kIOServicePlane, cpath)
        ioregService.ioDisplayLocation = String(cString: cpath)
        cpath.deallocate()
        if let unmanagedDisplayAttrs = IORegistryEntryCreateCFProperty(entry, "DisplayAttributes" as CFString, kCFAllocatorDefault, IOOptionBits(kIORegistryIterateRecursively)), let displayAttrs = unmanagedDisplayAttrs.takeRetainedValue() as? NSDictionary {
            ioregService.displayAttributes = displayAttrs
            if let productAttrs = displayAttrs.value(forKey: "ProductAttributes") as? NSDictionary {
                if let manufacturerID = productAttrs.value(forKey: "ManufacturerID") as? String {
                    ioregService.manufacturerID = manufacturerID
                }
                if let productName = productAttrs.value(forKey: "ProductName") as? String {
                    ioregService.productName = productName
                }
                if let serialNumber = productAttrs.value(forKey: "SerialNumber") as? Int64 {
                    ioregService.serialNumber = serialNumber
                }
                if let alphanumericSerialNumber = productAttrs.value(forKey: "AlphanumericSerialNumber") as? String {
                    ioregService.alphanumericSerialNumber = alphanumericSerialNumber
                }
            }
        }
        if let unmanagedTransport = IORegistryEntryCreateCFProperty(entry, "Transport" as CFString, kCFAllocatorDefault, IOOptionBits(kIORegistryIterateRecursively)), let transport = unmanagedTransport.takeRetainedValue() as? NSDictionary {
            if let upstream = transport.value(forKey: "Upstream") as? String {
                ioregService.transportUpstream = upstream
            }
            if let downstream = transport.value(forKey: "Downstream") as? String {
                ioregService.transportDownstream = downstream
            }
        }
        return ioregService
    }

    static func setIORegServiceDCPAVServiceProxy(entry: io_service_t, ioregService: inout IOregService) {
        if let unmanagedLocation = IORegistryEntryCreateCFProperty(entry, "Location" as CFString, kCFAllocatorDefault, IOOptionBits(kIORegistryIterateRecursively)), let location = unmanagedLocation.takeRetainedValue() as? String {
            ioregService.location = location
            if location == "External" {
                if let createAV = fIOAVServiceCreateWithService {
                    ioregService.service = createAV(kCFAllocatorDefault, entry)?.takeRetainedValue()
                }
            }
        }
    }

    static public func getIoregServicesForMatching() -> [IOregService] {
        var serviceLocation = 0
        var ioregServicesForMatching: [IOregService] = []
        let ioregRoot: io_registry_entry_t = IORegistryGetRootEntry(kIOMainPortDefault)
        defer {
            IOObjectRelease(ioregRoot)
        }
        var iterator = io_iterator_t()
        defer {
            IOObjectRelease(iterator)
        }
        var ioregService = IOregService()
        guard IORegistryEntryCreateIterator(ioregRoot, "IOService", IOOptionBits(kIORegistryIterateRecursively), &iterator) == KERN_SUCCESS else {
            return ioregServicesForMatching
        }
        let keyDCPAVServiceProxy = "DCPAVServiceProxy"
        let keysFramebuffer = ["AppleCLCD2", "IOMobileFramebufferShim"]
        while true {
            guard let objectOfInterest = ioregIterateToNextObjectOfInterest(interests: [keyDCPAVServiceProxy] + keysFramebuffer, iterator: &iterator) else {
                break
            }
            if keysFramebuffer.contains(objectOfInterest.name) {
                ioregService = self.getIORegServiceAppleCDC2Properties(entry: objectOfInterest.entry)
                serviceLocation += 1
                ioregService.serviceLocation = serviceLocation
            } else if objectOfInterest.name == keyDCPAVServiceProxy {
                self.setIORegServiceDCPAVServiceProxy(entry: objectOfInterest.entry, ioregService: &ioregService)
                ioregServicesForMatching.append(ioregService)
            }
            IOObjectRelease(objectOfInterest.entry)
        }
        return ioregServicesForMatching
    }

    static func checkIfDummy(ioregService: IOregService) -> Bool {
        if ioregService.manufacturerID == "AOC", ioregService.productName == "28E850" {
            return true
        }
        return false
    }

    static func checkIfDiscouraged(ioregService _: IOregService) -> Bool {
        false
    }
}

// MonitorInfoMac definition
struct MonitorInfoMac {
    var avService: IOAVService
    var description: String
    var currentInput: UInt32
    var inputCount: Int
    var inputs: [UInt32]
    var selectedIndex: Int
}

// DDCController using AppleSiliconDDC
class DDCController {
    static let shared = DDCController()
    
    var monitors: [MonitorInfoMac] = []
    
    func enumerateMonitors() {
        self.monitors.removeAll()
        
        // 1. Get all active display IDs from CoreGraphics
        let maxDisplays: UInt32 = 16
        var activeDisplays = [CGDirectDisplayID](repeating: 0, count: Int(maxDisplays))
        var displayCount: UInt32 = 0
        let err = CGGetActiveDisplayList(maxDisplays, &activeDisplays, &displayCount)
        guard err == .success else {
            logWrite("DDCController: CGGetActiveDisplayList failed.")
            return
        }
        
        let displayIDs = Array(activeDisplays.prefix(Int(displayCount)))
        logWrite("DDCController: Active CoreGraphics Display IDs: \(displayIDs)")
        
        // 2. Match display IDs to registry services using AppleSiliconDDC matching algorithm
        let matchedServices = AppleSiliconDDC.getServiceMatches(displayIDs: displayIDs)
        
        // 3. Populate monitors list
        for (idx, match) in matchedServices.enumerated() {
            guard let service = match.service else {
                logWrite("DDCController: Candidate \(idx) has no AVService proxy. Skipping.")
                continue
            }
            
            // Filter external monitors
            if match.serviceDetails.location != "External" {
                continue
            }
            
            let monitorName = match.serviceDetails.productName.isEmpty ? "External Display" : match.serviceDetails.productName
            logWrite("DDCController: Matched display ID \(match.displayID) -> \(monitorName)")
            
            // Read current VCP 0x60 input source
            var currentInput: UInt32 = 0
            if let readResult = AppleSiliconDDC.read(service: service, command: 0x60) {
                currentInput = UInt32(readResult.current)
                logWrite("DDCController: Read VCP 0x60 = \(currentInput) (0x\(String(currentInput, radix: 16).uppercased()))")
            } else {
                logWrite("DDCController: Read VCP 0x60 failed on startup.")
            }
            
            var monitor = MonitorInfoMac(
                avService: service,
                description: monitorName,
                currentInput: currentInput,
                inputCount: 0,
                inputs: [],
                selectedIndex: 0
            )
            
            // Apply config overrides or default fallbacks
            let config = AppConfig.shared
            if idx < config.monitorCount && config.monitors[idx].inputCount > 0 {
                let mc = config.monitors[idx]
                monitor.inputCount = mc.inputCount
                monitor.inputs = mc.inputs.map { $0.value }
                monitor.selectedIndex = 0
                for (i, v) in monitor.inputs.enumerated() {
                    if v == currentInput {
                        monitor.selectedIndex = i
                        break
                    }
                }
                logWrite("DDCController: Applied override settings from INI: \(monitor.inputs)")
            } else {
                let fallbackInputs: [UInt32] = [5, 6, 7, 8]
                var initialInputs = [UInt32]()
                if currentInput != 0 {
                    initialInputs.append(currentInput)
                }
                for v in fallbackInputs {
                    if v != currentInput {
                        initialInputs.append(v)
                    }
                }
                monitor.inputs = initialInputs
                monitor.inputCount = initialInputs.count
                monitor.selectedIndex = 0
                
                config.mergeInputs(monitorIdx: idx, monitorName: monitorName, values: initialInputs)
                logWrite("DDCController: Generated fallback configuration: \(initialInputs)")
            }
            
            self.monitors.append(monitor)
        }
        
        AppConfig.shared.save()
    }
    
    func refreshCurrentInput(monitorIdx: Int) -> UInt32? {
        guard monitorIdx >= 0 && monitorIdx < monitors.count else { return nil }
        var mon = monitors[monitorIdx]
        
        var readResult = AppleSiliconDDC.read(service: mon.avService, command: 0x60)
        if readResult == nil {
            logWrite("DDCController: DDC read failed for monitor \(monitorIdx), re-enumerating monitors...")
            enumerateMonitors()
            guard monitorIdx < monitors.count else { return nil }
            mon = monitors[monitorIdx]
            readResult = AppleSiliconDDC.read(service: mon.avService, command: 0x60)
        }
        
        if let result = readResult {
            let cur = UInt32(result.current)
            mon.currentInput = cur
            for (i, v) in mon.inputs.enumerated() {
                if v == cur {
                    mon.selectedIndex = i
                    break
                }
            }
            monitors[monitorIdx] = mon
            logWrite("DDCController: Refreshed monitor \(monitorIdx) current input = \(cur)")
            return cur
        }
        
        return nil
    }
    
    func getInput(monitorIdx: Int) -> UInt32? {
        return refreshCurrentInput(monitorIdx: monitorIdx)
    }
    
    func setInput(monitorIdx: Int, value: UInt32) -> Bool {
        guard monitorIdx >= 0 && monitorIdx < monitors.count else { return false }
        var mon = monitors[monitorIdx]
        
        logWrite("DDCController: Setting monitor \(monitorIdx) (\(mon.description)) input to \(value) (0x\(String(value, radix: 16).uppercased()))")
        
        var success = AppleSiliconDDC.write(service: mon.avService, command: 0x60, value: UInt16(value))
        if !success {
            logWrite("DDCController: Direct DDC write failed. Re-enumerating displays and retrying...")
            enumerateMonitors()
            guard monitorIdx < monitors.count else { return false }
            mon = monitors[monitorIdx]
            success = AppleSiliconDDC.write(service: mon.avService, command: 0x60, value: UInt16(value))
        }
        
        if success {
            logWrite("DDCController: DDC write succeeded for value \(value)")
            mon.currentInput = value
            for (i, v) in mon.inputs.enumerated() {
                if v == value {
                    mon.selectedIndex = i
                    break
                }
            }
            monitors[monitorIdx] = mon
            return true
        } else {
            logWrite("DDCController: DDC write failed after retry.")
            return false
        }
    }
    
    func cycleNext(monitorIdx: Int) -> UInt32 {
        guard monitorIdx >= 0 && monitorIdx < monitors.count else { return 0 }
        var mon = monitors[monitorIdx]
        
        if mon.inputCount > 1 {
            mon.selectedIndex = (mon.selectedIndex + 1) % mon.inputCount
            monitors[monitorIdx] = mon
            return mon.inputs[mon.selectedIndex]
        } else if mon.inputCount == 1 {
            var next = mon.currentInput + 1
            if next > 0x1F { next = 1 }
            mon.inputs[0] = next
            mon.selectedIndex = 0
            monitors[monitorIdx] = mon
            return next
        }
        return mon.currentInput
    }
    
    func commitAll() {
        logWrite("DDCController: Committing all inputs...")
        for i in 0..<monitors.count {
            let mon = monitors[i]
            let target: UInt32
            if mon.inputCount > 0 && mon.selectedIndex < mon.inputCount {
                target = mon.inputs[mon.selectedIndex]
            } else {
                target = mon.currentInput
            }
            
            let success = setInput(monitorIdx: i, value: target)
            if success {
                logWrite("DDCController: Monitor \(i) successfully switched to target \(target)")
            } else {
                logWrite("DDCController: Monitor \(i) failed to switch to target \(target)")
            }
        }
    }
}
