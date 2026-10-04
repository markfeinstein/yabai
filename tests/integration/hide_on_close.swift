// Two persistent test windows. The target orders itself out on close and later
// returns; the anchor remains visible so BSP membership is never inferred from
// singleton split metadata.
import AppKit

func argument(_ name: String, _ fallback: Double) -> Double {
    let args = CommandLine.arguments
    guard let i = args.firstIndex(of: name), i + 1 < args.count, let value = Double(args[i + 1]) else { return fallback }
    return value
}

final class Delegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let reshowAfter = argument("--reshow-after", 5)
    let quitAfter = argument("--quit-after", 60)
    var anchor: NSWindow!
    var target: NSWindow!

    func applicationDidFinishLaunching(_ notification: Notification) {
        anchor = NSWindow(contentRect: NSRect(x: 100, y: 150, width: 500, height: 350),
                          styleMask: [.titled, .resizable], backing: .buffered, defer: false)
        anchor.title = "hide-on-close-anchor"
        anchor.isReleasedWhenClosed = false
        anchor.makeKeyAndOrderFront(nil)

        target = NSWindow(contentRect: NSRect(x: 650, y: 150, width: 500, height: 350),
                          styleMask: [.titled, .closable, .resizable, .miniaturizable],
                          backing: .buffered, defer: false)
        target.title = "hide-on-close-target"
        target.isReleasedWhenClosed = false
        target.delegate = self
        target.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        print("anchor \(anchor.windowNumber)")
        print("target \(target.windowNumber)")
        fflush(stdout)
        DispatchQueue.main.asyncAfter(deadline: .now() + quitAfter) { NSApp.terminate(nil) }
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        sender.orderOut(nil)
        print("ordered out")
        fflush(stdout)
        if reshowAfter > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + reshowAfter) {
                sender.makeKeyAndOrderFront(nil)
                print("ordered in")
                fflush(stdout)
            }
        }
        return false
    }
}

let app = NSApplication.shared
let delegate = Delegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
