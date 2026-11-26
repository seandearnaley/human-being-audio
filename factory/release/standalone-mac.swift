import CoreGraphics
import Foundation

// Open the executable inside the installed bundle and identify its window by PID.
let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let executable = Bundle(url: url)?.executableURL else { fatalError("Missing app executable") }
let app = Process()
app.executableURL = executable
try app.run()
defer { if app.isRunning { app.terminate() } }
let deadline = Date().addingTimeInterval(30)
var opened = false
while Date() < deadline && app.isRunning {
    let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
    if windows.contains(where: {
        guard $0[kCGWindowOwnerPID as String] as? pid_t == app.processIdentifier,
              $0[kCGWindowLayer as String] as? Int == 0,
              let bounds = $0[kCGWindowBounds as String] as? [String: Double] else { return false }
        return (bounds["Width"] ?? 0) > 200 && (bounds["Height"] ?? 0) > 100
    }) {
        print("Opened standalone window for " + url.lastPathComponent)
        opened = true
        break
    }
    try await Task.sleep(for: .milliseconds(100))
}
if !opened {
    throw NSError(domain: "StudioStandalone", code: 1, userInfo: [NSLocalizedDescriptionKey: "No standalone window appeared for " + url.path])
}
