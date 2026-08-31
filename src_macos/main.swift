import Cocoa
import Foundation
import ApplicationServices

private var s_log: FileHandle? = nil

func logOpen() {
    let path = "/tmp/vdsrcswitch_debug.log"
    let fileManager = FileManager.default
    if !fileManager.fileExists(atPath: path) {
        fileManager.createFile(atPath: path, contents: nil, attributes: nil)
    }
    s_log = FileHandle(forWritingAtPath: path)
    s_log?.seekToEndOfFile()
}

func logWrite(_ msg: String) {
    guard let log = s_log else { return }
    let timestamp = DateFormatter.localizedString(from: Date(), dateStyle: .short, timeStyle: .medium)
    let line = "[\(timestamp)] \(msg)\n"
    if let data = line.data(using: .utf8) {
        log.write(data)
    }
}

func logClose() {
    s_log?.closeFile()
    s_log = nil
}

func checkSingleInstance() -> Bool {
    let lockPath = "/tmp/vdsrcswitch.lock"
    let fd = open(lockPath, O_RDWR | O_CREAT, 0o666)
    if fd < 0 {
        return false
    }
    if flock(fd, LOCK_EX | LOCK_NB) < 0 {
        // Already running
        close(fd)
        return false
    }
    // Keep lock fd open to hold the lock
    return true
}

func checkAccessibilityPermissions() -> Bool {
    let checkOptionPrompt = kAXTrustedCheckOptionPrompt.takeRetainedValue() as String
    let options = [checkOptionPrompt: true] as CFDictionary
    return AXIsProcessTrustedWithOptions(options)
}

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        logWrite("AppDelegate: Application did finish launching.")
        
        // Load AppConfig
        AppConfig.shared.load()
        logWrite("AppDelegate: Configuration loaded.")
        
        // Traverse DDC monitors
        logWrite("AppDelegate: Scanning DDC monitors...")
        DDCController.shared.enumerateMonitors()
        logWrite("AppDelegate: Display enumeration finished.")
        
        // Install keyboard tap hook
        if !KeyboardHook.shared.install() {
            logWrite("AppDelegate: FATAL - Keyboard hook installation failed.")
            NSApp.terminate(nil)
        }
        
        // Setup Status Item Menu Bar
        StatusMenuManager.shared.setup()
    }
    
    func applicationWillTerminate(_ notification: Notification) {
        logWrite("AppDelegate: Terminating. Cleaning up KeyboardHook...")
        KeyboardHook.shared.uninstall()
        logWrite("=== vdsrcswitch terminated ===")
        logClose()
    }
}

// Main Execution
logOpen()
logWrite("=== vdsrcswitch starting (macOS daemon mode) ===")

// Handle CLI flags
if CommandLine.arguments.contains("--uninstall") {
    logWrite("CLI: Uninstall requested.")
    // Trigger uninstall tasks (replicates Windows logic)
    print("Uninstalling launch agent and cleaning files...")
    let plistPath = NSString(string: "~/Library/LaunchAgents/com.vdsrcswitch.daemon.plist").expandingTildeInPath
    let binaryPath = "/usr/local/bin/vdsrcswitch_macos"
    
    let fm = FileManager.default
    do {
        if fm.fileExists(atPath: plistPath) {
            try fm.removeItem(atPath: plistPath)
        }
        if fm.fileExists(atPath: binaryPath) {
            try fm.removeItem(atPath: binaryPath)
        }
        print("vdsrcswitch removed from startup.")
    } catch {
        print("Failed to remove some startup files: \(error)")
    }
    logClose()
    exit(0)
}

// Enforce single instance
if !checkSingleInstance() {
    logWrite("Single instance check failed. Another instance is already running. Exit.")
    print("Another instance is already running.")
    logClose()
    exit(0)
}
logWrite("Single instance verified.")

// Verify Accessibility permissions
if !checkAccessibilityPermissions() {
    logWrite("Accessibility permissions not granted. Prompting user and exiting.")
    print("Accessibility permissions are required. Please grant permission in System Settings and restart the daemon.")
    logClose()
    exit(1)
}
logWrite("Accessibility permissions verified.")

// Start application runloop
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory) // Accessory mode: no Dock icon, supports status item menu and window presentation
app.run()
