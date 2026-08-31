import Foundation

struct InputSource {
    var value: UInt32
    var name: String
}

struct MonitorConfig {
    var name: String
    var inputCount: Int
    var inputs: [InputSource]
}

class AppConfig {
    static let shared = AppConfig()
    
    var activationHoldMs: UInt32 = 300
    var overlayOpacity: UInt8 = 230
    var monitorCount: Int = 0
    var monitors: [MonitorConfig] = []
    var iniPath: String = ""
    
    init() {
        let exeURL = URL(fileURLWithPath: CommandLine.arguments[0])
        let exeDir = exeURL.deletingLastPathComponent()
        self.iniPath = exeDir.appendingPathComponent("vdsrcswitch.ini").path
    }
    
    func load() {
        let fileManager = FileManager.default
        guard fileManager.fileExists(atPath: iniPath) else {
            // If INI doesn't exist, we will write defaults later after monitor scan
            return
        }
        
        do {
            let content = try String(contentsOfFile: iniPath, encoding: .utf8)
            let lines = content.components(separatedBy: .newlines)
            
            var currentSection = ""
            var currentMonitorIdx = -1
            
            // Temporary structures for loading monitors
            var tempMonitors = [Int: MonitorConfig]()
            
            for line in lines {
                let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
                if trimmed.isEmpty || trimmed.hasPrefix(";") || trimmed.hasPrefix("#") {
                    continue
                }
                
                if trimmed.hasPrefix("[") && trimmed.hasSuffix("]") {
                    currentSection = String(trimmed.dropFirst().dropLast()).lowercased()
                    if currentSection.hasPrefix("monitor_") {
                        if let idx = Int(currentSection.replacingOccurrences(of: "monitor_", with: "")) {
                            currentMonitorIdx = idx
                            tempMonitors[idx] = MonitorConfig(name: "", inputCount: 0, inputs: [])
                        } else {
                            currentMonitorIdx = -1
                        }
                    } else {
                        currentMonitorIdx = -1
                    }
                    continue
                }
                
                let parts = trimmed.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: true)
                guard parts.count == 2 else { continue }
                
                let key = parts[0].trimmingCharacters(in: .whitespaces).lowercased()
                let value = parts[1].trimmingCharacters(in: .whitespaces)
                
                if currentSection == "settings" {
                    if key == "activation_hold_ms" {
                        if let ms = UInt32(value) {
                            self.activationHoldMs = max(50, min(2000, ms))
                        }
                    } else if key == "overlay_opacity" {
                        if let op = UInt8(value) {
                            self.overlayOpacity = op
                        }
                    } else if key == "monitor_count" {
                        if let count = Int(value) {
                            self.monitorCount = count
                        }
                    }
                } else if currentMonitorIdx >= 0 {
                    guard var mc = tempMonitors[currentMonitorIdx] else { continue }
                    
                    if key == "monitor_name" {
                        mc.name = value
                    } else if key == "input_count" {
                        if let count = Int(value) {
                            mc.inputCount = count
                            // Ensure arrays have spaces
                            while mc.inputs.count < count {
                                mc.inputs.append(InputSource(value: 0, name: ""))
                            }
                        }
                    } else if key.hasPrefix("input_") {
                        let suffix = key.replacingOccurrences(of: "input_", with: "")
                        if suffix.hasSuffix("_value") {
                            let idxStr = suffix.replacingOccurrences(of: "_value", with: "")
                            if let idx = Int(idxStr), idx >= 0 {
                                while mc.inputs.count <= idx {
                                    mc.inputs.append(InputSource(value: 0, name: ""))
                                }
                                if let val = UInt32(value) {
                                    mc.inputs[idx].value = val
                                }
                            }
                        } else if suffix.hasSuffix("_name") {
                            let idxStr = suffix.replacingOccurrences(of: "_name", with: "")
                            if let idx = Int(idxStr), idx >= 0 {
                                while mc.inputs.count <= idx {
                                    mc.inputs.append(InputSource(value: 0, name: ""))
                                }
                                mc.inputs[idx].name = value
                            }
                        }
                    }
                    tempMonitors[currentMonitorIdx] = mc
                }
            }
            
            // Build monitors array
            self.monitors.removeAll()
            let sortedKeys = tempMonitors.keys.sorted()
            for key in sortedKeys {
                if let mc = tempMonitors[key] {
                    self.monitors.append(mc)
                }
            }
            self.monitorCount = self.monitors.count
        } catch {
            print("Failed to load config: \(error)")
        }
    }
    
    func save() {
        var content = ""
        content += "[settings]\n"
        content += "activation_hold_ms=\(activationHoldMs)\n"
        content += "overlay_opacity=\(overlayOpacity)\n"
        content += "monitor_count=\(monitors.count)\n\n"
        
        for (m, mc) in monitors.enumerated() {
            content += "[monitor_\(m)]\n"
            content += "monitor_name=\(mc.name)\n"
            content += "input_count=\(mc.inputs.count)\n"
            for (i, input) in mc.inputs.enumerated() {
                content += "input_\(i)_value=\(input.value)\n"
                content += "input_\(i)_name=\(input.name)\n"
            }
            content += "\n"
        }
        
        do {
            try content.write(toFile: iniPath, atomically: true, encoding: .utf8)
        } catch {
            print("Failed to save config: \(error)")
        }
    }
    
    func getInputName(monitorIdx: Int, value: UInt32) -> String {
        if monitorIdx >= 0 && monitorIdx < monitors.count {
            let mc = monitors[monitorIdx]
            for input in mc.inputs {
                if input.value == value {
                    return input.name
                }
            }
        }
        return "Input \(value)"
    }
    
    func mergeInputs(monitorIdx: Int, monitorName: String, values: [UInt32]) {
        while monitors.count <= monitorIdx {
            monitors.append(MonitorConfig(name: "", inputCount: 0, inputs: []))
        }
        
        var mc = monitors[monitorIdx]
        if mc.name.isEmpty {
            mc.name = monitorName
        }
        
        for v in values {
            let found = mc.inputs.contains { $0.value == v }
            if !found && mc.inputs.count < 16 {
                let friendlyName: String
                switch v {
                case 1: friendlyName = "VGA"
                case 3: friendlyName = "DVI"
                case 4: friendlyName = "DVI 2"
                case 5: friendlyName = "HDMI 1"
                case 6: friendlyName = "HDMI 2"
                case 7: friendlyName = "DP 1"
                case 8: friendlyName = "DP 2"
                case 9: friendlyName = "DVI 1"
                case 10: friendlyName = "DVI 2"
                case 15: friendlyName = "DP 1 (Alt)"
                case 16: friendlyName = "DP 2 (Alt)"
                case 17: friendlyName = "HDMI 1 (Alt)"
                case 18: friendlyName = "HDMI 2 (Alt)"
                case 27: friendlyName = "USB-C"
                default: friendlyName = "Input \(v)"
                }
                mc.inputs.append(InputSource(value: v, name: friendlyName))
            }
        }
        
        mc.inputCount = mc.inputs.count
        monitors[monitorIdx] = mc
        self.monitorCount = monitors.count
        save()
    }
}
