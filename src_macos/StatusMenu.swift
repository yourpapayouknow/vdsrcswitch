import Cocoa
import Foundation

class StatusMenuManager: NSObject {
    static let shared = StatusMenuManager()
    
    var statusItem: NSStatusItem?
    private var displaysSubmenu: NSMenu?
    
    func setup() {
        // Create Status Item
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        
        if let button = statusItem?.button {
            // Use a template computer/display symbol (adapts to light/dark mode)
            if #available(macOS 11.0, *) {
                button.image = NSImage(systemSymbolName: "display.and.arrow.down", accessibilityDescription: "vdsrcswitch")
            } else {
                button.image = NSImage(named: NSImage.computerName)
            }
            button.image?.isTemplate = true
        }
        
        // Create Main Menu
        let menu = NSMenu()
        
        // Title Item
        let titleItem = NSMenuItem(title: "vdsrcswitch (Mac)", action: nil, keyEquivalent: "")
        titleItem.isEnabled = false
        menu.addItem(titleItem)
        
        menu.addItem(NSMenuItem.separator())
        
        // Rescan Displays Item
        let rescanItem = NSMenuItem(title: "Rescan Displays", action: #selector(rescanDisplays), keyEquivalent: "r")
        rescanItem.target = self
        menu.addItem(rescanItem)
        
        // Reload Config Item
        let reloadItem = NSMenuItem(title: "Reload Config", action: #selector(reloadConfig), keyEquivalent: "l")
        reloadItem.target = self
        menu.addItem(reloadItem)
        
        menu.addItem(NSMenuItem.separator())
        
        // Dynamic list menu item (we'll update this submenu dynamically)
        let displaysMenuContainer = NSMenuItem(title: "Displays", action: nil, keyEquivalent: "")
        displaysSubmenu = NSMenu()
        displaysMenuContainer.submenu = displaysSubmenu
        menu.addItem(displaysMenuContainer)
        
        menu.addItem(NSMenuItem.separator())
        
        // Quit Item
        let quitItem = NSMenuItem(title: "Quit", action: #selector(quitApp), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)
        
        statusItem?.menu = menu
        
        // Initial populate of the displays
        updateDisplayMenu()
    }
    
    // Updates the display list and their input sources
    func updateDisplayMenu() {
        guard let submenu = displaysSubmenu else { return }
        submenu.removeAllItems()
        
        let controllers = DDCController.shared.monitors
        if controllers.isEmpty {
            let noDisplaysItem = NSMenuItem(title: "No DDC/CI displays found", action: nil, keyEquivalent: "")
            noDisplaysItem.isEnabled = false
            submenu.addItem(noDisplaysItem)
            return
        }
        
        for (monIdx, mon) in controllers.enumerated() {
            // Monitor Name Header
            let monHeader = NSMenuItem(title: "\(mon.description)", action: nil, keyEquivalent: "")
            monHeader.isEnabled = false
            submenu.addItem(monHeader)
            
            // Loop through inputs of this monitor
            let config = AppConfig.shared
            let monitorConfig = monIdx < config.monitorCount ? config.monitors[monIdx] : nil
            
            for inputIdx in 0..<mon.inputCount {
                let v = mon.inputs[inputIdx]
                // Retrieve input name from AppConfig if available, otherwise friendly name
                var name = "Input \(v)"
                if let mc = monitorConfig {
                    if let inputSrc = mc.inputs.first(where: { $0.value == v }) {
                        name = inputSrc.name
                    }
                }
                
                let inputItem = NSMenuItem(title: "  \(name)", action: #selector(selectInput(_:)), keyEquivalent: "")
                inputItem.target = self
                inputItem.representedObject = ["monitorIdx": monIdx, "value": v]
                
                // Show checkmark if current input matches this source
                if v == mon.currentInput {
                    inputItem.state = .on
                } else {
                    inputItem.state = .off
                }
                
                submenu.addItem(inputItem)
            }
            
            if monIdx < controllers.count - 1 {
                submenu.addItem(NSMenuItem.separator())
            }
        }
    }
    
    @objc private func selectInput(_ sender: NSMenuItem) {
        guard let dict = sender.representedObject as? [String: Any],
              let monitorIdx = dict["monitorIdx"] as? Int,
              let value = dict["value"] as? UInt32 else {
            return
        }
        
        logWrite("StatusMenu: User clicked menu item to set monitor \(monitorIdx) to \(value)")
        
        let success = DDCController.shared.setInput(monitorIdx: monitorIdx, value: value)
        if success {
            logWrite("StatusMenu: Successfully switched input.")
            // Update monitor struct currentInput
            var mon = DDCController.shared.monitors[monitorIdx]
            mon.currentInput = value
            DDCController.shared.monitors[monitorIdx] = mon
            
            // Refresh menu checkmarks
            updateDisplayMenu()
        } else {
            logWrite("StatusMenu: Failed to switch input.")
        }
    }
    
    @objc private func rescanDisplays() {
        logWrite("StatusMenu: Rescanning displays...")
        DDCController.shared.enumerateMonitors()
        updateDisplayMenu()
    }
    
    @objc private func reloadConfig() {
        logWrite("StatusMenu: Reloading configuration...")
        AppConfig.shared.load()
        updateDisplayMenu()
    }
    
    @objc private func quitApp() {
        logWrite("StatusMenu: Quitting app.")
        NSApp.terminate(nil)
    }
}
