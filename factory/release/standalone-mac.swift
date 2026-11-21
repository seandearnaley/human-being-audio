import CoreGraphics
import Foundation

// Test the installed app through Launch Services, as a musician opening it would.
import AppKit
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let app = try await NSWorkspace.shared.openApplication(at: url, configuration: .init())
defer { app.terminate() }
let deadline = Date().addingTimeInterval(30)
while Date() < deadline && !app.isTerminated {
    let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
    if windows.contains(where: {
        guard $0[kCGWindowOwnerPID as String] as? pid_t == app.processIdentifier,
              $0[kCGWindowLayer as String] as? Int == 0,
              let bounds = $0[kCGWindowBounds as String] as? [String: Double] else { return false }
        return (bounds["Width"] ?? 0) > 200 && (bounds["Height"] ?? 0) > 100
    }) {
        print("Opened standalone window for " + url.lastPathComponent)
        break
    }
    try await Task.sleep(for: .milliseconds(100))
}
if Date() >= deadline || app.isTerminated {
    throw NSError(domain: "StudioStandalone", code: 1, userInfo: [NSLocalizedDescriptionKey: "No standalone window appeared for " + url.path])
}
