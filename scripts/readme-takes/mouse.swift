// Mouse events for scripts/guide-record.sh: `guide-mouse click X Y [ctrl]`,
// `guide-mouse move X Y`, `guide-mouse drag X0 Y0 X1 Y1 [right]` (global
// points, top-left origin, as System Events reports window positions).
// Needs the Accessibility permission, like the keystrokes.
import CoreGraphics
import Foundation

let args = CommandLine.arguments
func point(_ i: Int) -> CGPoint {
    CGPoint(x: Double(args[i])!, y: Double(args[i + 1])!)
}
func post(_ type: CGEventType, _ at: CGPoint, _ button: CGMouseButton = .left,
          _ flags: CGEventFlags = []) {
    let e = CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: at,
                    mouseButton: button)!
    e.flags = flags
    e.post(tap: .cghidEventTap)
    usleep(30_000)
}

switch args.count > 1 ? args[1] : "" {
case "glide" where args.count >= 6:
    let a = point(2), b = point(4)
    for i in 0...30 {
        let t = Double(i) / 30, s = t * t * (3 - 2 * t)
        post(.mouseMoved, CGPoint(x: a.x + (b.x - a.x) * s, y: a.y + (b.y - a.y) * s))
        usleep(10_000)
    }
case "circle" where args.count >= 5:
    let c = point(2), r = Double(args[4])!
    func at(_ t: Double) -> CGPoint { CGPoint(x: c.x + r * cos(t), y: c.y + r * 0.8 * sin(t)) }
    let start = -Double.pi / 2
    post(.mouseMoved, at(start))
    post(.leftMouseDown, at(start))
    for i in 1...60 { post(.leftMouseDragged, at(start + 2 * Double.pi * 1.08 * Double(i) / 60)) }
    post(.leftMouseUp, at(start + 2 * Double.pi * 1.08))
case "move" where args.count >= 4:
    post(.mouseMoved, point(2))
case "click" where args.count >= 4:
    let flags: CGEventFlags = args.count > 4 && args[4] == "ctrl" ? .maskControl : []
    let at = point(2)
    post(.mouseMoved, at)
    post(.leftMouseDown, at, .left, flags)
    post(.leftMouseUp, at, .left, flags)
case "drag" where args.count >= 6:
    let right = args.count > 6 && args[6] == "right"
    let (down, drag, up): (CGEventType, CGEventType, CGEventType) = right
        ? (.rightMouseDown, .rightMouseDragged, .rightMouseUp)
        : (.leftMouseDown, .leftMouseDragged, .leftMouseUp)
    let button: CGMouseButton = right ? .right : .left
    let from = point(2), to = point(4)
    post(.mouseMoved, from)
    post(down, from, button)
    for i in 1...20 {
        let t = Double(i) / 20
        post(drag, CGPoint(x: from.x + (to.x - from.x) * t, y: from.y + (to.y - from.y) * t), button)
    }
    post(up, to, button)
default:
    FileHandle.standardError.write("usage: guide-mouse click X Y [ctrl] | move X Y | drag X0 Y0 X1 Y1 [right]\n".data(using: .utf8)!)
    exit(2)
}
