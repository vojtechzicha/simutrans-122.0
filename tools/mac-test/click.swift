import Foundation
import CoreGraphics
// usage: click X Y [left|middle|right|move]
let a = CommandLine.arguments
let p = CGPoint(x: Double(a[1])!, y: Double(a[2])!)
let kind = a.count > 3 ? a[3] : "left"
func post(_ t: CGEventType, _ b: CGMouseButton) {
    CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: p, mouseButton: b)?.post(tap: .cghidEventTap)
}
let q = CGPoint(x: p.x-6, y: p.y-6)
CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: q, mouseButton: .left)?.post(tap: .cghidEventTap); usleep(250000)
post(.mouseMoved, .left); usleep(250000)
switch kind {
case "middle": post(.otherMouseDown, .center); usleep(80000); post(.otherMouseUp, .center)
case "right":  post(.rightMouseDown, .right); usleep(80000); post(.rightMouseUp, .right)
case "move":   break
default:       post(.leftMouseDown, .left); usleep(80000); post(.leftMouseUp, .left)
}
