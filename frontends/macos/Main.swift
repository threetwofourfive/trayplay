import Cocoa
import SwiftUI

// MARK: - App State & ViewModel

final class ConsoleViewModel: ObservableObject {
    enum ConsoleState: Equatable {
        case waiting
        case active(cartridge: String)
        case error
    }

    @Published var state: ConsoleState = .waiting

    var statusBadgeText: String {
        switch state {
        case .waiting: return "● WAITING"
        case .active: return "● ACTIVE"
        case .error: return "● ERROR"
        }
    }

    var statusBadgeColor: Color {
        switch state {
        case .waiting: return Color(red: 0.95, green: 0.77, blue: 0.06)
        case .active: return Color(red: 0.0, green: 1.0, blue: 0.8)
        case .error: return Color(red: 1.0, green: 0.28, blue: 0.34)
        }
    }

    var screenTitle: String {
        switch state {
        case .waiting: return "▶ TRAYPLAY ◀"
        case .active: return "▶ GAME RUNNING ◀"
        case .error: return "▶ ERROR ◀"
        }
    }

    var screenStatusText: String {
        switch state {
        case .waiting: return "[ NO CARTRIDGE LOADED ]"
        case .active(let name): return "[ CARTRIDGE: \(name.uppercased()) ]"
        case .error: return "[ CARTRIDGE ERROR ]"
        }
    }

    var screenStatusColor: Color {
        switch state {
        case .waiting: return Color(red: 0.95, green: 0.77, blue: 0.06)
        case .active: return Color(red: 0.18, green: 0.80, blue: 0.44)
        case .error: return Color(red: 1.0, green: 0.28, blue: 0.34)
        }
    }

    func loadCartridge(from url: URL) {
        guard url.pathExtension.lowercased() == "gpak" else {
            state = .error
            return
        }
        let fileName = url.lastPathComponent
        state = .active(cartridge: fileName)
    }

    func eject() {
        state = .waiting
    }

    func setError() {
        state = .error
    }
}

// MARK: - SwiftUI Popover View

struct ConsolePopoverView: View {
    @ObservedObject var viewModel: ConsoleViewModel

    var body: some View {
        VStack(spacing: 12) {
            // Header
            HStack(spacing: 6) {
                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: 14))
                    .foregroundColor(Color(red: 0.87, green: 0.90, blue: 0.91))

                Text("TRAYPLAY")
                    .font(.system(size: 13, weight: .heavy))
                    .foregroundColor(Color(red: 0.87, green: 0.90, blue: 0.91))

                Spacer()

                Text(viewModel.statusBadgeText)
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundColor(viewModel.statusBadgeColor)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 3)
                    .background(viewModel.statusBadgeColor.opacity(0.15))
                    .clipShape(Capsule())
            }

            // 16:9 Screen / Canvas Area
            ZStack(alignment: .bottomLeading) {
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color(red: 0.01, green: 0.015, blue: 0.02))

                // Center labels
                VStack(spacing: 14) {
                    Text(viewModel.screenTitle)
                        .font(.system(size: 17, weight: .black, design: .monospaced))
                        .foregroundColor(Color(red: 0.0, green: 1.0, blue: 0.8))

                    Text(viewModel.screenStatusText)
                        .font(.system(size: 11, weight: .bold, design: .monospaced))
                        .foregroundColor(viewModel.screenStatusColor)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)

                // Bottom display metadata
                Text("DISPLAY: 320x180")
                    .font(.system(size: 9, weight: .medium, design: .monospaced))
                    .foregroundColor(Color(red: 0.34, green: 0.38, blue: 0.44))
                    .padding(10)
            }
            .frame(width: 420, height: 236)
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(Color(red: 0.14, green: 0.15, blue: 0.19), lineWidth: 2)
            )

            // Controls
            HStack(spacing: 8) {
                Button(action: selectCartridgeFile) {
                    HStack {
                        Spacer()
                        Text("Load Game (.gpak)")
                            .font(.system(size: 12, weight: .bold))
                        Spacer()
                    }
                    .padding(.vertical, 6)
                }
                .buttonStyle(.borderedProminent)
                .tint(Color(red: 0.18, green: 0.21, blue: 0.26))

                Button(action: { viewModel.eject() }) {
                    Text("Eject")
                        .font(.system(size: 12, weight: .medium))
                        .padding(.vertical, 6)
                        .padding(.horizontal, 12)
                }
                .buttonStyle(.bordered)
            }
        }
        .padding(14)
        .frame(width: 448)
        .background(VisualEffectView(material: .popover, blendingMode: .behindWindow))
    }

    private func selectCartridgeFile() {
        let panel = NSOpenPanel()
        panel.title = "Select TrayPlay Cartridge (.gpak)"
        panel.allowedContentTypes = []
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.canChooseFiles = true

        if panel.runModal() == .OK, let url = panel.url {
            viewModel.loadCartridge(from: url)
        }
    }
}

// MARK: - Native NSVisualEffectView Bridge

struct VisualEffectView: NSViewRepresentable {
    let material: NSVisualEffectView.Material
    let blendingMode: NSVisualEffectView.BlendingMode

    func makeNSView(context: Context) -> NSVisualEffectView {
        let visualEffectView = NSVisualEffectView()
        visualEffectView.material = material
        visualEffectView.blendingMode = blendingMode
        visualEffectView.state = .active
        return visualEffectView
    }

    func updateNSView(_ visualEffectView: NSVisualEffectView, context: Context) {
        visualEffectView.material = material
        visualEffectView.blendingMode = blendingMode
    }
}

// MARK: - Application Delegate & Status Item Controller

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var statusItem: NSStatusItem!
    private var popover: NSPopover!
    private let viewModel = ConsoleViewModel()

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Hide dock icon (MenuBar Agent)
        NSApp.setActivationPolicy(.accessory)

        // Setup status item
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = statusItem.button {
            button.image = NSImage(
                systemSymbolName: "gamecontroller.fill",
                accessibilityDescription: "TrayPlay"
            )
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
            button.action = #selector(handleStatusItemClick(_:))
            button.target = self
        }

        // Setup popover hosting SwiftUI
        let popover = NSPopover()
        popover.contentSize = NSSize(width: 448, height: 340)
        popover.behavior = .transient
        popover.animates = true
        popover.contentViewController = NSHostingController(
            rootView: ConsolePopoverView(viewModel: viewModel)
        )
        self.popover = popover
    }

    @objc private func handleStatusItemClick(_ sender: NSStatusBarButton) {
        guard let event = NSApp.currentEvent else { return }

        if event.type == .rightMouseUp {
            // Right-click: Context Menu
            if popover.isShown {
                popover.performClose(sender)
            }
            showContextMenu(sender)
        } else {
            // Left-click: Toggle SwiftUI Popover
            togglePopover(sender)
        }
    }

    private func togglePopover(_ sender: NSStatusBarButton) {
        if popover.isShown {
            popover.performClose(sender)
        } else {
            popover.show(relativeTo: sender.bounds, of: sender, preferredEdge: .minY)
            popover.contentViewController?.view.window?.makeKey()
        }
    }

    private func showContextMenu(_ sender: NSStatusBarButton) {
        let menu = NSMenu()

        let titleItem = NSMenuItem(title: "🎮 TrayPlay Microconsole", action: nil, keyEquivalent: "")
        titleItem.isEnabled = false
        menu.addItem(titleItem)

        menu.addItem(NSMenuItem.separator())

        let openItem = NSMenuItem(title: "Open Screen", action: #selector(openScreenAction), keyEquivalent: "o")
        openItem.target = self
        menu.addItem(openItem)

        menu.addItem(NSMenuItem.separator())

        let quitItem = NSMenuItem(title: "Quit", action: #selector(quitAction), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)

        statusItem.menu = menu
        statusItem.button?.performClick(nil)
        // Reset statusItem.menu so future left-clicks still trigger the button action
        statusItem.menu = nil
    }

    @objc private func openScreenAction() {
        if let button = statusItem.button {
            togglePopover(button)
        }
    }

    @objc private func quitAction() {
        NSApp.terminate(nil)
    }
}

// MARK: - Entry Point

@main
enum MainEntry {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.run()
    }
}
