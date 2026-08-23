import Foundation
import IOKit

// Private CoreDisplay / IOKit DDC API Signatures
typealias IOAVServiceCreateWithService_Type = @convention(c) (CFAllocator?, io_service_t) -> UnsafeMutableRawPointer?
typealias IOAVServiceWriteI2C_Type = @convention(c) (UnsafeMutableRawPointer, UInt32, UInt32, UnsafePointer<UInt8>, UInt32) -> IOReturn
typealias IOAVServiceReadI2C_Type = @convention(c) (UnsafeMutableRawPointer, UInt32, UInt32, UnsafeMutablePointer<UInt8>, UInt32) -> IOReturn
typealias IOAVServiceCopyEDID_Type = @convention(c) (UnsafeMutableRawPointer, UnsafeMutablePointer<Unmanaged<CFData>?>) -> IOReturn

struct MonitorInfoMac {
    var avService: UnsafeMutableRawPointer
    var description: String
    var currentInput: UInt32
    var inputCount: Int
    var inputs: [UInt32]
    var selectedIndex: Int
}

class DDCController {
    static let shared = DDCController()
    
    private var coreDisplay: UnsafeMutableRawPointer? = nil
    private var fIOAVServiceCreateWithService: IOAVServiceCreateWithService_Type? = nil
    private var fIOAVServiceWriteI2C: IOAVServiceWriteI2C_Type? = nil
    private var fIOAVServiceReadI2C: IOAVServiceReadI2C_Type? = nil
    private var fIOAVServiceCopyEDID: IOAVServiceCopyEDID_Type? = nil
    
    var monitors: [MonitorInfoMac] = []
    
    init() {
        loadPrivateAPIs()
    }
    
    private func loadPrivateAPIs() {
        // Load CoreDisplay private library
        let path = "/System/Library/Frameworks/CoreDisplay.framework/CoreDisplay"
        self.coreDisplay = dlopen(path, RTLD_LAZY)
        guard let lib = self.coreDisplay else {
            logWrite("DDCController: Failed to load CoreDisplay framework. Running on Intel or Apple Silicon?")
            return
        }
        
        if let sym = dlsym(lib, "IOAVServiceCreateWithService") {
            self.fIOAVServiceCreateWithService = unsafeBitCast(sym, to: IOAVServiceCreateWithService_Type.self)
        }
        if let sym = dlsym(lib, "IOAVServiceWriteI2C") {
            self.fIOAVServiceWriteI2C = unsafeBitCast(sym, to: IOAVServiceWriteI2C_Type.self)
        }
        if let sym = dlsym(lib, "IOAVServiceReadI2C") {
            self.fIOAVServiceReadI2C = unsafeBitCast(sym, to: IOAVServiceReadI2C_Type.self)
        }
        if let sym = dlsym(lib, "IOAVServiceCopyEDID") {
            self.fIOAVServiceCopyEDID = unsafeBitCast(sym, to: IOAVServiceCopyEDID_Type.self)
        }
        logWrite("DDCController: Loaded Apple Silicon CoreDisplay APIs successfully.")
    }
    
    func enumerateMonitors() {
        self.monitors.removeAll()
        
        guard let createAVService = fIOAVServiceCreateWithService else {
            logWrite("DDCController: Private DDC APIs not loaded.")
            return
        }
        
        var iter: io_iterator_t = 0
        let matching = IOServiceMatching("DCPAVServiceProxy")
        let kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter)
        
        guard kr == KERN_SUCCESS else {
            logWrite("DDCController: IOServiceGetMatchingServices failed.")
            return
        }
        
        let locationKey = "Location" as CFString
        
        var service = IOIteratorNext(iter)
        var idx = 0
        while service != 0 {
            defer {
                IOObjectRelease(service)
                service = IOIteratorNext(iter)
            }
            
            // Filter external monitors
            if let locationVal = IORegistryEntryCreateCFProperty(service, locationKey, kCFAllocatorDefault, 0)?.takeRetainedValue() as? String {
                if locationVal != "External" {
                    continue
                }
            } else {
                continue
            }
            
            guard let avService = createAVService(kCFAllocatorDefault, service) else {
                continue
            }
            
            // Read EDID to get display name
            var monitorName = "External Display"
            var unmanagedEdid: Unmanaged<CFData>? = nil
            if let copyEDID = fIOAVServiceCopyEDID, copyEDID(avService, &unmanagedEdid) == 0, let edidCF = unmanagedEdid?.takeRetainedValue() {
                let edid = edidCF as Data
                if let name = parseMonitorName(from: edid) {
                    monitorName = name
                }
            }
            
            logWrite("DDCController: Found monitor at idx \(idx): \(monitorName)")
            
            // Try to read current input value
            var currentInput: UInt32 = 0
            if let cur = readVCP(avService: avService, vcp: 0x60) {
                currentInput = UInt32(cur)
            }
            
            var monitor = MonitorInfoMac(
                avService: avService,
                description: monitorName,
                currentInput: currentInput,
                inputCount: 0,
                inputs: [],
                selectedIndex: 0
            )
            
            // Check if user has manual override config in INI
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
                logWrite("DDCController: Loaded overrides from INI successfully for \(monitorName)")
            } else {
                // Fallback / initial setup: populate standard inputs list
                // Values: 5 (HDMI 1), 6 (HDMI 2), 7 (DP 1), 8 (DP 2), 15 (DP 1 alt), 16 (DP 2 alt), 17 (HDMI 1 alt), 18 (HDMI 2 alt)
                let fallbackInputs: [UInt32] = [5, 6, 7, 8, 15, 16, 17, 18]
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
                
                // Merge into Config
                config.mergeInputs(monitorIdx: idx, monitorName: monitorName, values: initialInputs)
            }
            
            self.monitors.append(monitor)
            idx += 1
        }
        
        IOObjectRelease(iter)
        AppConfig.shared.save()
    }
    
    func getInput(monitorIdx: Int) -> UInt32? {
        guard monitorIdx >= 0 && monitorIdx < monitors.count else { return nil }
        let mon = monitors[monitorIdx]
        if let val = readVCP(avService: mon.avService, vcp: 0x60) {
            return UInt32(val)
        }
        return nil
    }
    
    func setInput(monitorIdx: Int, value: UInt32) -> Bool {
        guard monitorIdx >= 0 && monitorIdx < monitors.count else { return false }
        let mon = monitors[monitorIdx]
        
        logWrite("DDCController: Setting monitor \(monitorIdx) (\(mon.description)) input to \(value) (0x\(String(value, radix: 16).uppercased()))")
        
        return writeVCP(avService: mon.avService, vcp: 0x60, value: value)
    }
    
    func cycleNext(monitorIdx: Int) -> UInt32 {
        guard monitorIdx >= 0 && monitorIdx < monitors.count else { return 0 }
        var mon = monitors[monitorIdx]
        
        if mon.inputCount > 1 {
            mon.selectedIndex = (mon.selectedIndex + 1) % mon.inputCount
            monitors[monitorIdx] = mon
            return mon.inputs[mon.selectedIndex]
        } else if mon.inputCount == 1 {
            // Fallback mode: try currentInput + 1
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
            
            if target != mon.currentInput {
                let success = setInput(monitorIdx: i, value: target)
                if success {
                    var updated = monitors[i]
                    updated.currentInput = target
                    monitors[i] = updated
                }
            } else {
                logWrite("DDCController: Monitor \(i) target is identical to current. Skipping.")
            }
        }
    }
    
    // Low-level write VCP via IOAVService
    private func writeVCP(avService: UnsafeMutableRawPointer, vcp: UInt8, value: UInt32) -> Bool {
        guard let writeI2C = fIOAVServiceWriteI2C else { return false }
        
        var data = [UInt8](repeating: 0, count: 6)
        data[0] = 0x84 // Length byte (0x80 | length of payload = 4)
        data[1] = 0x03 // DDC write VCP opcode
        data[2] = vcp  // VCP code (e.g. 0x60)
        data[3] = UInt8((value >> 8) & 0xFF)
        data[4] = UInt8(value & 0xFF)
        data[5] = 0x6E ^ 0x51 ^ data[0] ^ data[1] ^ data[2] ^ data[3] ^ data[4] // DDC Checksum
        
        // Write retry loop (standard 3 attempts as in i2cwrite.m)
        var success = false
        for attempt in 1...3 {
            let err = writeI2C(avService, 0x37, 0x51, &data, 6)
            if err == 0 {
                success = true
                break
            } else {
                logWrite("DDCController: Write attempt \(attempt) failed with code: \(err)")
            }
            usleep(32000)
        }
        return success
    }
    
    // Low-level read VCP via IOAVService
    private func readVCP(avService: UnsafeMutableRawPointer, vcp: UInt8) -> UInt16? {
        guard let writeI2C = fIOAVServiceWriteI2C, let readI2C = fIOAVServiceReadI2C else { return nil }
        
        var req = [UInt8](repeating: 0, count: 4)
        req[0] = 0x82 // Length (0x80 | 2)
        req[1] = 0x01 // DDC read VCP opcode
        req[2] = vcp
        req[3] = 0x6E ^ 0x51 ^ req[0] ^ req[1] ^ req[2] // Checksum
        
        let writeErr = writeI2C(avService, 0x37, 0x51, &req, 4)
        guard writeErr == 0 else {
            logWrite("DDCController: Read request write failed: \(writeErr)")
            return nil
        }
        
        usleep(40000) // Standard DDC read delay (40ms)
        
        var reply = [UInt8](repeating: 0, count: 11)
        let readErr = readI2C(avService, 0x37, 0x51, &reply, 11)
        guard readErr == 0 else {
            logWrite("DDCController: Read response failed: \(readErr)")
            return nil
        }
        
        // Verify reply format
        // reply[1] is length (0x88 is typical 0x80 | 8)
        // reply[2] is command (0x02 reply)
        // reply[3] is result (0x00 no error)
        // reply[4] is VCP code
        guard reply[1] == 0x88 && reply[2] == 0x02 && reply[3] == 0x00 && reply[4] == vcp else {
            logWrite("DDCController: Read reply format invalid: \(reply)")
            return nil
        }
        
        let val = (UInt16(reply[8]) << 8) | UInt16(reply[9])
        return val
    }
    
    // Standard EDID monitor name parsing helper
    private func parseMonitorName(from edid: Data) -> String? {
        guard edid.count >= 128 else { return nil }
        let descriptors = [54, 72, 90, 108]
        for start in descriptors {
            guard start + 18 <= edid.count else { continue }
            let block = edid.subdata(in: start..<(start + 18))
            if block[0] == 0x00 && block[1] == 0x00 && block[2] == 0x00 && block[3] == 0xFC {
                // Monitor Name Descriptor
                let nameBytes = block.subdata(in: 5..<18)
                if let nameStr = String(data: nameBytes, encoding: .ascii) {
                    let trimmed = nameStr.trimmingCharacters(in: .whitespacesAndNewlines)
                    if !trimmed.isEmpty {
                        return trimmed
                    }
                }
            }
        }
        return nil
    }
    
    deinit {
        if let lib = coreDisplay {
            dlclose(lib)
        }
    }
}
