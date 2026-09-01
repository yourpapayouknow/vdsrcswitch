import Cocoa
import CoreGraphics
import Carbon

enum ModeState: Int {
    case idle = 0
    case timing    // Shift+V has been pressed, waiting for hold threshold (default 300ms)
    case active    // Overlay active, wait for Tab to cycle
    case selecting // Tab has been pressed at least once to cycle inputs
    case cancelling // Cancel state
}

class KeyboardHook {
    static let shared = KeyboardHook()
    
    private var eventTap: CFMachPort? = nil
    private var runLoopSource: CFRunLoopSource? = nil
    
    var state: ModeState = .idle
    
    var shiftDown = false
    var vDown = false
    var tabDown = false
    
    private var timer: Timer? = nil
    
    func install() -> Bool {
        // Enforce Zsh / Zsh rules - logging startup
        logWrite("KeyboardHook: Installing CGEventTap...")
        
        let eventMask = (1 << CGEventType.keyDown.rawValue) |
                        (1 << CGEventType.keyUp.rawValue) |
                        (1 << CGEventType.flagsChanged.rawValue)
        
        // We capture events at the session level to allow swallowing
        guard let tap = CGEvent.tapCreate(
            tap: .cgSessionEventTap,
            place: .headInsertEventTap,
            options: .defaultTap,
            eventsOfInterest: CGEventMask(eventMask),
            callback: { (proxy, type, event, refcon) -> Unmanaged<CGEvent>? in
                return KeyboardHook.shared.handleEvent(type: type, event: event, proxy: proxy)
            },
            userInfo: nil
        ) else {
            logWrite("KeyboardHook: CGEvent.tapCreate failed. Do we have Accessibility permissions?")
            return false
        }
        
        self.eventTap = tap
        self.runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        
        if let source = self.runLoopSource {
            CFRunLoopAddSource(CFRunLoopGetCurrent(), source, .commonModes)
            CGEvent.tapEnable(tap: tap, enable: true)
            logWrite("KeyboardHook: CGEvent.tapEnable installed successfully.")
            return true
        }
        
        return false
    }
    
    func uninstall() {
        logWrite("KeyboardHook: Uninstalling event tap...")
        self.timer?.invalidate()
        self.timer = nil
        
        if let source = self.runLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, .commonModes)
            self.runLoopSource = nil
        }
        self.eventTap = nil
    }
    
    private func handleEvent(type: CGEventType, event: CGEvent, proxy: CGEventTapProxy) -> Unmanaged<CGEvent>? {
        let keyCode = event.getIntegerValueField(.keyboardEventKeycode)
        
        let isShift = (keyCode == 56 || keyCode == 60 || keyCode == 63) // Left Shift (56), Right Shift (60)
        let isV = (keyCode == 9)
        let isTab = (keyCode == 48)
        
        // Update key states
        if type == .flagsChanged {
            self.shiftDown = event.flags.contains(.maskShift)
        } else if type == .keyDown {
            if isV { self.vDown = true }
            if isTab { self.tabDown = true }
        } else if type == .keyUp {
            if isV { self.vDown = false }
            if isTab { self.tabDown = false }
        }
        
        let isRepeat = (type == .keyDown && event.getIntegerValueField(.keyboardEventAutorepeat) != 0)
        
        // State Machine transitions
        if type == .keyDown || (type == .flagsChanged && self.shiftDown) {
            if !isRepeat {
                // If Shift and V are both down, trigger 300ms hold timer
                if self.shiftDown && self.vDown && self.state == .idle {
                    self.state = .timing
                    logWrite("KeyboardHook: Triggered Shift+V hold timer (\(AppConfig.shared.activationHoldMs)ms)")
                    DispatchQueue.main.async {
                        self.startHoldTimer()
                    }
                }
                
                // Cycle next via Tab
                if isTab && (self.state == .active || self.state == .selecting) {
                    self.state = .selecting
                    logWrite("KeyboardHook: Tab down - Cycle requested")
                    DispatchQueue.main.async {
                        self.cycleInput()
                    }
                }
            }
        } else if type == .keyUp || (type == .flagsChanged && !self.shiftDown) {
            // Cancel timer if Shift or V is released during timing
            if (isShift || isV) && self.state == .timing {
                logWrite("KeyboardHook: Shift/V released during timing. Cancel timer.")
                self.state = .idle
                self.timer?.invalidate()
                self.timer = nil
            }
            // Cancel overlay if Shift or V is released from active state (no selection made)
            else if (isShift || isV) && self.state == .active {
                logWrite("KeyboardHook: Shift/V released from active state. Close overlay.")
                self.state = .idle
                DispatchQueue.main.async {
                    OverlayWindow.shared.hide()
                    self.resetSelectionIndex()
                }
            }
            
            // Check for committing changes when all keys are released
            if !self.shiftDown && !self.vDown && !self.tabDown {
                if self.state == .selecting {
                    logWrite("KeyboardHook: All keys released! Committing target sources.")
                    self.state = .idle
                    DispatchQueue.main.async {
                        OverlayWindow.shared.hide()
                        DDCController.shared.commitAll()
                    }
                } else if self.state == .active {
                    logWrite("KeyboardHook: All keys released from active state. Cancel overlay.")
                    self.state = .idle
                    DispatchQueue.main.async {
                        OverlayWindow.shared.hide()
                        self.resetSelectionIndex()
                    }
                }
            }
        }
        
        // Swallow events if timing or active
        if self.state != .idle {
            if isShift || isV || isTab {
                return nil // Return nil to swallow the event
            }
        }
        
        return Unmanaged.passUnretained(event)
    }
    
    private func startHoldTimer() {
        self.timer?.invalidate()
        
        // Asynchronously refresh live current input during the hold delay
        DispatchQueue.global(qos: .userInitiated).async {
            for i in 0..<DDCController.shared.monitors.count {
                _ = DDCController.shared.refreshCurrentInput(monitorIdx: i)
            }
        }
        
        let interval = Double(AppConfig.shared.activationHoldMs) / 1000.0
        self.timer = Timer.scheduledTimer(withTimeInterval: interval, repeats: false) { [weak self] _ in
            guard let self = self else { return }
            if self.state == .timing {
                self.state = .active
                logWrite("KeyboardHook: Hold threshold reached. Mode is ACTIVE.")
                
                self.resetSelectionIndex()
                
                // Show initial overlay (show current, don't cycle yet)
                let monCount = DDCController.shared.monitors.count
                let curName = monCount > 0 ? AppConfig.shared.getInputName(monitorIdx: 0, value: DDCController.shared.monitors[0].currentInput) : "?"
                let tgtVal = monCount > 0 ? (DDCController.shared.monitors[0].inputCount > 0 ? DDCController.shared.monitors[0].inputs[DDCController.shared.monitors[0].selectedIndex] : DDCController.shared.monitors[0].currentInput) : 0
                let tgtName = monCount > 0 ? AppConfig.shared.getInputName(monitorIdx: 0, value: tgtVal) : "?"
                
                OverlayWindow.shared.show(currentName: curName, targetName: tgtName)
            }
        }
    }
    
    private func cycleInput() {
        guard DDCController.shared.monitors.count > 0 else { return }
        
        let targetVal = DDCController.shared.cycleNext(monitorIdx: 0)
        for i in 1..<DDCController.shared.monitors.count {
            _ = DDCController.shared.cycleNext(monitorIdx: i)
        }
        
        let curName = AppConfig.shared.getInputName(monitorIdx: 0, value: DDCController.shared.monitors[0].currentInput)
        let tgtName = AppConfig.shared.getInputName(monitorIdx: 0, value: targetVal)
        
        OverlayWindow.shared.show(currentName: curName, targetName: tgtName)
    }
    
    private func resetSelectionIndex() {
        for i in 0..<DDCController.shared.monitors.count {
            var mon = DDCController.shared.monitors[i]
            for (idx, v) in mon.inputs.enumerated() {
                if v == mon.currentInput {
                    mon.selectedIndex = idx
                    break
                }
            }
            DDCController.shared.monitors[i] = mon
        }
    }
}
