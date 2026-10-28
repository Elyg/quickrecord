// winlist [pid]: on-screen windows as "pid id layer x y w h owner | title"
import CoreGraphics
import Foundation
let want = CommandLine.arguments.count > 1 ? Int(CommandLine.arguments[1]) : nil
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as! [[String: Any]]
for w in list {
    let pid = w[kCGWindowOwnerPID as String] as! Int
    if let want, want != pid { continue }
    let b = w[kCGWindowBounds as String] as! [String: Double]
    let id = w[kCGWindowNumber as String] as! Int
    let layer = w[kCGWindowLayer as String] as! Int
    let owner = w[kCGWindowOwnerName as String] as? String ?? ""
    let name = w[kCGWindowName as String] as? String ?? ""
    print(pid, id, layer, Int(b["X"]!), Int(b["Y"]!), Int(b["Width"]!), Int(b["Height"]!), owner, "|", name)
}
