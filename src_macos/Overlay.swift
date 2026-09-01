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
        
        // 3. Draw text with balanced typography and solid font weights
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = .center
        
        let fontLine1 = NSFont.systemFont(ofSize: 18, weight: .bold)
        let fontLine2 = NSFont.systemFont(ofSize: 32, weight: .heavy)
        
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
        
        // Line 2: Target input
        let text2: String
        if !targetName.isEmpty && targetName != currentName {
            text2 = "→  \(targetName)"
        } else {
            text2 = currentName
        }
        let size2 = text2.size(withAttributes: attrsLine2)
        
        // Vertically center the combined two-line text block with balanced spacing
        let spacing: CGFloat = 8
        let totalTextHeight = size1.height + spacing + size2.height
        let startY = boxY + (boxH - totalTextHeight) / 2
        
        // In Cocoa (unflipped coordinates), bottom line is drawn at lower Y, top line at higher Y
        let line2Y = startY
        let line1Y = startY + size2.height + spacing
        
        let rect1 = NSRect(x: boxX + 20, y: line1Y, width: boxW - 40, height: size1.height)
        text1.draw(in: rect1, withAttributes: attrsLine1)
        
        let rect2 = NSRect(x: boxX + 20, y: line2Y, width: boxW - 40, height: size2.height)
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
