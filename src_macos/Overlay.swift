import Cocoa

class OverlayView: NSView {
    var currentName: String = ""
    var targetName: String = ""
    
    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        
        let bounds = self.bounds
        
        // Target dimensions (standard points, Cocoa automatically scales to Retina resolution)
        let boxW: CGFloat = 420
        let boxH: CGFloat = 120
        
        // Centered box rectangle
        let boxX = (bounds.width - boxW) / 2
        let boxY = (bounds.height - boxH) / 2
        let boxRect = NSRect(x: boxX, y: boxY, width: boxW, height: boxH)
        
        // 1. Draw rounded rectangle background (Orange: 255, 140, 0)
        let path = NSBezierPath(roundedRect: boxRect, xRadius: 14, yRadius: 14)
        NSColor(red: 255/255, green: 140/255, blue: 0/255, alpha: 1.0).setFill()
        path.fill()
        
        // 2. Draw inset border (Cyan: 0, 220, 220)
        let strokeRect = boxRect.insetBy(dx: 2, dy: 2)
        let strokePath = NSBezierPath(roundedRect: strokeRect, xRadius: 12, yRadius: 12)
        NSColor(red: 0/255, green: 220/255, blue: 220/255, alpha: 1.0).setStroke()
        strokePath.lineWidth = 4
        strokePath.stroke()
        
        // 3. Draw text in PingFang SC
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = .center
        
        let fontLine1 = NSFont(name: "PingFangSC-Semibold", size: 20) ?? NSFont.boldSystemFont(ofSize: 20)
        let fontLine2 = NSFont(name: "PingFangSC-Semibold", size: 32) ?? NSFont.boldSystemFont(ofSize: 32)
        
        let attrsLine1: [NSAttributedString.Key: Any] = [
            .font: fontLine1,
            .foregroundColor: NSColor.black,
            .paragraphStyle: paragraphStyle
        ]
        let attrsLine2: [NSAttributedString.Key: Any] = [
            .font: fontLine2,
            .foregroundColor: NSColor.black,
            .paragraphStyle: paragraphStyle
        ]
        
        // Line 1: Current input
        let text1 = "当前: \(currentName)"
        let size1 = text1.size(withAttributes: attrsLine1)
        
        // Line 1 should center vertically in top half of the box
        let line1Y = boxY + boxH / 2 + (boxH / 2 - size1.height) / 2
        let rect1 = NSRect(x: boxX + 24, y: line1Y, width: boxW - 48, height: size1.height)
        text1.draw(in: rect1, withAttributes: attrsLine1)
        
        // Line 2: Target input
        let text2: String
        if !targetName.isEmpty && targetName != currentName {
            text2 = "→  \(targetName)"
        } else {
            text2 = currentName
        }
        let size2 = text2.size(withAttributes: attrsLine2)
        
        // Line 2 should center vertically in bottom half of the box
        let line2Y = boxY + (boxH / 2 - size2.height) / 2
        let rect2 = NSRect(x: boxX + 24, y: line2Y, width: boxW - 48, height: size2.height)
        text2.draw(in: rect2, withAttributes: attrsLine2)
    }
}

class OverlayWindow: NSWindow {
    static let shared = OverlayWindow()
    
    private var overlayView: OverlayView? = nil
    
    init() {
        let screenRect = NSScreen.main?.frame ?? NSRect(x: 0, y: 0, width: 1920, height: 1080)
        super.init(
            contentRect: screenRect,
            styleMask: [.borderless],
            backing: .buffered,
            defer: false
        )
        
        self.isOpaque = false
        self.backgroundColor = .clear
        self.hasShadow = false
        self.level = .statusBar
        self.ignoresMouseEvents = true
        self.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        
        let view = OverlayView(frame: screenRect)
        self.contentView = view
        self.overlayView = view
    }
    
    func show(currentName: String, targetName: String) {
        let screenRect = NSScreen.main?.frame ?? NSRect(x: 0, y: 0, width: 1920, height: 1080)
        self.setFrame(screenRect, display: true)
        
        if let view = overlayView {
            view.frame = screenRect
            view.currentName = currentName
            view.targetName = targetName
            view.needsDisplay = true
        }
        
        self.alphaValue = CGFloat(AppConfig.shared.overlayOpacity) / 255.0
        self.orderFrontRegardless()
    }
    
    func hide() {
        self.orderOut(nil)
    }
}
