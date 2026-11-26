import AppKit
import SwiftUI

private struct Game: Identifiable, Hashable {
    let url: URL
    let title: String
    let credits: [String]
    var id: URL { url }
    var icon: URL? {
        ["icon.png", "icon.jpg", "icon.jpeg", "icon.bmp", "icon.webp", "system/icon.png", "system/icon.jpg", "system/icon.bmp"]
            .map { url.appendingPathComponent($0) }.first {
                guard let meta = try? $0.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]) else { return false }
                return meta.isRegularFile == true && (meta.fileSize ?? Int.max) <= 4_000_000
            }
    }
    static func read(_ url: URL) -> Game? {
        let dir = url.resolvingSymlinksInPath().standardizedFileURL
        guard let config = try? String(contentsOf: dir.appendingPathComponent("gameconfig.txt"), encoding: .utf8) else { return nil }
        let title = config.split(whereSeparator: \.isNewline).compactMap { line -> String? in
            let fields = line.split(separator: ",", maxSplits: 1)
            guard fields.count == 2, fields[0].trimmingCharacters(in: .whitespaces).lowercased() == "gametitle" else { return nil }
            return String(fields[1]).trimmingCharacters(in: .whitespaces)
        }.first
        let lines = title?.replacingOccurrences(of: "\\r\\n", with: "\n")
            .replacingOccurrences(of: "\\n", with: "\n")
            .components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }.filter { !$0.isEmpty } ?? []
        return Game(url: dir, title: lines.first ?? dir.lastPathComponent, credits: Array(lines.dropFirst()))
    }
    static func scan(_ url: URL) -> [Game] {
        if let game = read(url) { return [game] }
        let dirs = (try? FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles])) ?? []
        return dirs.filter { (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true }.compactMap(read)
    }
}

private struct Item: Identifiable {
    let id: Int
    let url: URL
    let title: String
    let credits: [String]
    let icon: URL?
}
extension Notification.Name {
    static let openLocation = Self("CPyMO.OpenLocation")
    static let openGames = Self("CPyMO.OpenGames")
    static let searchGames = Self("CPyMO.SearchGames")
    static let toggleDetails = Self("CPyMO.ToggleDetails")
    static let startGame = Self("CPyMO.StartGame")
}

@MainActor final class MenuDelegate: NSObject, NSApplicationDelegate {
    var reopenLibrary: (() -> Void)?
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows: Bool) -> Bool {
        if let window = sender.windows.first(where: { $0.title == "游戏库" && $0.isVisible }) {
            window.makeKeyAndOrderFront(nil)
        } else if let reopenLibrary {
            reopenLibrary()
        } else {
            return true
        }
        return false
    }
    @objc func about() {
        let icon = Bundle.main.url(forResource: "CPyMO", withExtension: "icns").flatMap(NSImage.init(contentsOf:))
        let words = Bundle.main.url(forResource: "About", withExtension: "txt")
            .flatMap { try? String(contentsOf: $0, encoding: .utf8) } ?? ""
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = .center; paragraph.lineSpacing = 5
        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationName: "CPyMO", .applicationVersion: "0.3.6",
            .version: "CPyMO Aria 1.1.9", .applicationIcon: (icon ?? NSApp.applicationIconImage ?? NSImage()) as Any,
            .credits: NSAttributedString(string: words.trimmingCharacters(in: .whitespacesAndNewlines),
                attributes: [.font: NSFont.systemFont(ofSize: 12), .paragraphStyle: paragraph])
        ])
    }
    @objc private func openLocation() { NotificationCenter.default.post(name: .openLocation, object: nil) }
    @objc private func openGames() { NotificationCenter.default.post(name: .openGames, object: nil) }
    @objc private func searchGames() { NotificationCenter.default.post(name: .searchGames, object: nil) }
    @objc private func toggleDetails() { NotificationCenter.default.post(name: .toggleDetails, object: nil) }
    @objc private func startGame() { NotificationCenter.default.post(name: .startGame, object: nil) }

    private func makeMenu(_ bar: NSMenu, _ title: String) -> NSMenu {
        let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        let submenu = NSMenu(title: title)
        item.submenu = submenu
        bar.addItem(item)
        return submenu
    }
    private func add(_ menu: NSMenu, _ title: String, _ action: Selector, _ key: String = "",
                     modifiers: NSEvent.ModifierFlags = .command, target: AnyObject? = nil) {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        item.keyEquivalentModifierMask = modifiers
        item.target = target
        menu.addItem(item)
    }
    func installMenus() {
        let bar = NSMenu()
        let application = makeMenu(bar, "CPyMO")
        add(application, "隐藏 CPyMO", #selector(NSApplication.hide(_:)), "h", target: NSApp)
        add(application, "隐藏其他应用", #selector(NSApplication.hideOtherApplications(_:)), "h", modifiers: [.command, .option], target: NSApp)
        add(application, "全部显示", #selector(NSApplication.unhideAllApplications(_:)), target: NSApp)
        application.addItem(.separator())
        add(application, "退出 CPyMO", #selector(NSApplication.terminate(_:)), "q", target: NSApp)
        let file = makeMenu(bar, "文件")
        add(file, "添加游戏…", #selector(openGames), "o", target: self)
        add(file, "添加游戏目录…", #selector(openLocation), "o", modifiers: [.command, .shift], target: self)
        add(file, "开始游戏", #selector(startGame), "\r", target: self)
        file.addItem(.separator())
        add(file, "关闭窗口", #selector(NSWindow.performClose(_:)), "w")
        let edit = makeMenu(bar, "编辑")
        add(edit, "撤销", Selector(("undo:")), "z")
        add(edit, "重做", Selector(("redo:")), "z", modifiers: [.command, .shift])
        edit.addItem(.separator())
        add(edit, "剪切", #selector(NSText.cut(_:)), "x")
        add(edit, "复制", #selector(NSText.copy(_:)), "c")
        add(edit, "粘贴", #selector(NSText.paste(_:)), "v")
        add(edit, "全选", #selector(NSText.selectAll(_:)), "a")
        let view = makeMenu(bar, "显示")
        add(view, "搜索游戏", #selector(searchGames), "f", target: self)
        add(view, "显示或隐藏游戏详情", #selector(toggleDetails), "i", modifiers: [.command, .option], target: self)
        add(view, "进入或退出全屏", #selector(NSWindow.toggleFullScreen(_:)), "f", modifiers: [.command, .control])
        let window = makeMenu(bar, "窗口")
        add(window, "最小化", #selector(NSWindow.performMiniaturize(_:)), "m")
        add(window, "缩放", #selector(NSWindow.performZoom(_:)))
        window.addItem(.separator())
        add(window, "将全部窗口置于最前", #selector(NSApplication.arrangeInFront(_:)), target: NSApp)
        let help = makeMenu(bar, "帮助")
        add(help, "关于 CPyMO", #selector(about), target: self)
        NSApp.mainMenu = bar
        NSApp.windowsMenu = window
        NSApp.helpMenu = help
    }
}

private struct OriginalPreview: View {
    @ObservedObject var state: PreviewState
    let delegate: MenuDelegate
    @Environment(\.openWindow) private var openWindow
    @FocusState private var searchFocused: Bool
    @FocusState private var gamesFocused: Bool
    private var dark: Bool { state.effectiveScheme == .dark }
    private var paper: Color {
        dark ? Color(red: 0.136, green: 0.145, blue: 0.160) : Color(red: 0.962, green: 0.966, blue: 0.973)
    }
    private var bluePaper: Color {
        dark ? Color(red: 0.151, green: 0.185, blue: 0.248) : Color(red: 0.900, green: 0.928, blue: 0.974)
    }
    private var ink: Color {
        dark ? Color(red: 0.93, green: 0.95, blue: 0.99) : Color(red: 0.12, green: 0.16, blue: 0.23)
    }
    private var muted: Color {
        dark ? Color(red: 0.68, green: 0.73, blue: 0.81) : Color(red: 0.35, green: 0.41, blue: 0.51)
    }
    private var blue: Color {
        dark ? Color(red: 0.46, green: 0.64, blue: 0.98) : Color(red: 0.20, green: 0.39, blue: 0.89)
    }
    private var launchBlue: Color { Color(red: 0.20, green: 0.39, blue: 0.89) }
    private var rule: Color {
        dark ? Color(red: 0.24, green: 0.255, blue: 0.28) : Color(red: 0.878, green: 0.892, blue: 0.918)
    }
    private var items: [Item] {
        let base = state.visibleGames
        let q = state.query.trimmingCharacters(in: .whitespacesAndNewlines)
        let filtered = q.isEmpty ? base : base.filter { $0.title.localizedCaseInsensitiveContains(q) }
        return state.reversed ? Array(filtered.reversed()) : filtered
    }
    private var game: Item? { items.first { $0.id == state.selected } }
    private var heading: String { state.tab == 1 ? "最近游玩" : state.locations.first { $0.id == state.tab }?.name ?? "全部游戏" }

    var body: some View {
        NavigationSplitView {
            sidebar
                .navigationSplitViewColumnWidth(min: 172, ideal: 207, max: 260)
        } detail: {
            VStack(spacing: 0) {
                libraryHeading
                library
            }
            .background(paper)
            .toolbar {
                ToolbarItem(placement: .automatic) { searchBox }
                ToolbarItemGroup(placement: .primaryAction) {
                    Button { state.choose(.game) } label: { Label("添加游戏", systemImage: "plus") }
                        .help("添加游戏")
                    appearanceMenu
                    Button { state.showDetails.toggle() } label: { Label("游戏详情", systemImage: "sidebar.right") }
                        .help("显示或隐藏游戏详情")
                }
            }
            .background {
                Button("搜索游戏") { searchFocused = true }.keyboardShortcut("f", modifiers: .command)
                    .opacity(0).accessibilityHidden(true).allowsHitTesting(false)
            }
        }
        .inspector(isPresented: $state.showDetails) {
            inspector.inspectorColumnWidth(min: 228, ideal: 268, max: 345)
        }
        .foregroundStyle(ink)
        .environment(\.colorScheme, state.effectiveScheme)
        .preferredColorScheme(state.effectiveScheme)
        .background(WindowAppearanceLink(state: state).accessibilityHidden(true))
        .onChange(of: state.tab) { _,_ in reconcile() }
        .onChange(of: state.query) { _,_ in reconcile() }
        .onChange(of: state.games.map(\.id)) { _,_ in reconcile() }
        .onAppear {
            delegate.reopenLibrary = { openWindow(id: "library") }
            state.refresh()
            let args = CommandLine.arguments
            if let i = args.firstIndex(of: "--test-client"), args.count > i + 2 {
                state.testClient(root: URL(fileURLWithPath: args[i + 1]), out: args[i + 2])
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in state.refresh() }
        .onReceive(NotificationCenter.default.publisher(for: .openGames)) { _ in state.choose(.game) }
        .onReceive(NotificationCenter.default.publisher(for: .openLocation)) { _ in state.choose(.location) }
        .onReceive(NotificationCenter.default.publisher(for: .searchGames)) { _ in searchFocused = true }
        .onReceive(NotificationCenter.default.publisher(for: .toggleDetails)) { _ in state.showDetails.toggle() }
        .onReceive(NotificationCenter.default.publisher(for: .startGame)) { _ in
            if let game, !searchFocused { state.play(game) }
        }
        .alert("CPyMO", isPresented: Binding(get: { state.message != nil }, set: { if !$0 { state.message = nil } })) {
            Button("好") { state.message = nil }
        } message: { Text(state.message ?? "") }

    }
    private func reconcile() { if game == nil { state.selected = items.first?.id ?? 0 } }
    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 9) {
                Image(nsImage: NSApp.applicationIconImage).resizable().scaledToFit().frame(width: 44, height: 44)
                if let mark = LibraryImages.wordmark {
                    Image(nsImage: mark).renderingMode(.template).resizable().interpolation(.high).scaledToFit()
                        .frame(width: 64, height: 22).foregroundStyle(ink)
                        .offset(y: 1).accessibilityLabel("CPyMO")
                } else { Text("CPyMO").font(.system(size: 19, weight: .bold)) }
            }.padding(.top, 15).padding(.bottom, 25)
            sidebarHeading("浏览")
            tab("全部游戏", "gamecontroller", 0, count: state.games.count)
            tab("最近游玩", "clock", 1)
            HStack {
                sidebarHeading("位置").padding(.bottom, -8)
                Spacer()
                Button { state.choose(.location) } label: {
                    Image(systemName: "plus").font(.system(size: 11, weight: .semibold))
                        .foregroundStyle(muted).frame(width: 24, height: 24)
                        .contentShape(Rectangle())
                }.buttonStyle(.plain).help("添加游戏目录").accessibilityLabel("添加游戏目录")
            }.padding(.top, 22).padding(.trailing, 1).padding(.bottom, 4)
            ForEach(state.locations) { location in
                tab(location.online ? location.name : location.name + "（暂不可用）", "folder", location.id)
                    .help(location.url.path)
                    .contextMenu {
                        Button("在 Finder 中显示") { NSWorkspace.shared.activateFileViewerSelecting([location.url]) }
                            .disabled(!location.online)
                        Button("移除游戏目录", role: .destructive) { state.removeLocation(location) }
                    }
            }
            Spacer()
        }
        .padding(.horizontal, 15)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
    private func sidebarHeading(_ title: String) -> some View {
        Text(title).font(.system(size: 11, weight: .semibold)).foregroundStyle(muted)
            .padding(.leading, 9).padding(.bottom, 8)
    }
    private func tab(_ title: String, _ symbol: String, _ id: Int, count: Int? = nil) -> some View {
        let active = state.tab == id
        return Button { state.tab = id } label: {
            HStack(spacing: 10) {
                Image(systemName: symbol).foregroundStyle(blue).frame(width: 18)
                Text(title).fontWeight(active ? .semibold : .regular).lineLimit(1)
                Spacer(minLength: 4)
                if let count { Text(String(count)).font(.caption).foregroundStyle(muted) }
            }.font(.system(size: 14)).padding(.horizontal, 10).frame(height: 37)
                .contentShape(Rectangle())
        }.buttonStyle(.plain)
         .background {
             if active {
                 RoundedRectangle(cornerRadius: 10).fill(blue.opacity(0.115))
             }
         }
         .glassEffect(active && state.glass ? .regular.tint(blue.opacity(0.17)) : .identity, in: .rect(cornerRadius: 10))
         .accessibilityAddTraits(active ? .isSelected : [])
    }
    private var libraryHeading: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(heading).font(.system(size: 25, weight: .semibold)).lineLimit(1)
                .accessibilityAddTraits(.isHeader)
            Text("共 \(items.count) 款").font(.system(size: 12)).foregroundStyle(muted)
        }.frame(maxWidth: .infinity, alignment: .leading)
            .padding(.leading, 16).padding(.top, 10).padding(.bottom, 3)
    }
    private var searchBox: some View {
        HStack(spacing: 7) {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("搜索游戏", text: $state.query).textFieldStyle(.plain).focused($searchFocused)
            if !state.query.isEmpty {
                Button("清除搜索", systemImage: "xmark.circle.fill") { state.query = "" }
                    .labelStyle(.iconOnly).buttonStyle(.plain)
            }
        }.padding(.horizontal, 9).frame(width: 144, height: 26)
    }
    private var appearanceMenu: some View {
        Menu {
            ForEach(PreviewAppearance.allCases) { option in
                Toggle(option.rawValue, isOn: Binding(
                    get: { state.appearance == option },
                    set: { _ in state.setAppearance(option) }
                ))
            }
        } label: {
            Label("外观", systemImage: state.appearance.icon)
        }.menuIndicator(.hidden)
            .accessibilityLabel("外观：\(state.appearance.rawValue)").help("外观：\(state.appearance.rawValue)")
    }
    private var library: some View {
        VStack(alignment: .leading, spacing: 0) {
            if items.isEmpty {
                if !state.query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                    ContentUnavailableView("没有找到匹配的游戏", systemImage: "magnifyingglass")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if state.games.isEmpty && state.tab == 0 {
                    emptyLibrary
                } else {
                    ContentUnavailableView(state.tab == 1 ? "还没有游玩记录" :
                                            (state.locations.first { $0.id == state.tab }?.online == false ? "游戏目录暂不可用" : "这个目录没有游戏"),
                                           systemImage: state.tab == 1 ? "clock" : "folder")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            } else {
                HStack(spacing: 12) {
                    if state.libraryMode == .list { Text("游戏").padding(.leading, 46) }
                    Spacer()
                    Button { state.reversed.toggle() } label: {
                        HStack(spacing: 5) {
                            Text("上次游玩")
                            Image(systemName: state.reversed ? "chevron.up" : "chevron.down")
                                .font(.system(size: 8, weight: .semibold))
                        }
                    }.buttonStyle(.plain).help("按上次游玩排序")
                    LibraryDisplaySwitch(selection: state.libraryMode, accent: blue, dark: dark) { mode in
                        if state.libraryMode != mode {
                            state.setLibraryMode(mode)
                            if !searchFocused { gamesFocused = true }
                        }
                    }

                }.font(.system(size: 11, weight: .semibold)).foregroundStyle(muted)
                    .padding(.horizontal, 15).padding(.top, 10).padding(.bottom, 6)
                gameViewport

            }
            if !items.isEmpty {
                Label("双击游戏即可开始", systemImage: "cursorarrow.click.2")
                    .font(.system(size: 11)).foregroundStyle(muted)
                    .padding(.leading, 15).padding(.vertical, 11)
            }
        }.padding(.leading, 8).padding(.trailing, 16)
    }
    private var gameViewport: some View {
        GeometryReader { geo in
            let cols = TileLayout.columns(for: geo.size.width)
            ScrollViewReader { proxy in
                ScrollView {
                    if state.libraryMode == .list {
                        VStack(spacing: 5) {
                            ForEach(items) { row($0).id($0.id) }
                        }.padding(.vertical, 2)
                    } else {
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(minimum: 56), spacing: 10), count: cols),
                                  alignment: .leading, spacing: 10) {
                            ForEach(items) { tile($0).id($0.id) }
                        }.padding(.horizontal, 14).padding(.vertical, 5)
                    }
                }
                .focusable().focusEffectDisabled()
                .focused($gamesFocused)
                .defaultFocus($gamesFocused, true)
                .onMoveCommand { direction in
                    guard !searchFocused,
                          let next = TileLayout.move(items.map(\.id), from: state.selected,
                                                     direction: direction, columns: cols, mode: state.libraryMode) else { return }
                    state.selected = next
                    proxy.scrollTo(next)
                }
                .onChange(of: state.libraryMode) { _,_ in
                    DispatchQueue.main.async { proxy.scrollTo(state.selected) }
                }
            }
        }
    }

    private func tile(_ item: Item) -> some View {
        let active = state.selected == item.id
        return VStack(spacing: 7) {
            artwork(item, size: 52)
            Text(item.title)
                .font(.system(size: 12, weight: active ? .semibold : .regular))
                .multilineTextAlignment(.center).lineLimit(2)
                .frame(maxWidth: .infinity)
        }.padding(.horizontal, 7)
            .frame(maxWidth: .infinity, minHeight: 104, maxHeight: 104, alignment: .center)
            .background {
                if active { RoundedRectangle(cornerRadius: 12).fill(blue.opacity(0.10)) }
            }
            .glassEffect(active && state.glass ? .regular.tint(blue.opacity(0.15)) : .identity, in: .rect(cornerRadius: 12))
            .contentShape(.rect(cornerRadius: 12))
            .onTapGesture(count: 2) { state.selected = item.id; state.play(item) }
            .onTapGesture { state.selected = item.id; gamesFocused = true }
            .help(item.title)
            .accessibilityAddTraits(active ? [.isSelected, .isButton] : .isButton)
            .accessibilityAction { state.selected = item.id }
    }

    private var emptyLibrary: some View {
        VStack(spacing: 13) {
            Image(systemName: "gamecontroller").font(.system(size: 35, weight: .regular))
                .foregroundStyle(blue)
            Text("游戏库还是空的").font(.title2.weight(.semibold))
            Text("先添加一款游戏，或者把存放多款游戏的目录加进来。")
                .font(.callout).foregroundStyle(muted)
                .multilineTextAlignment(.center)
            HStack(spacing: 10) {
                Button { state.choose(.game) } label: { Label("添加游戏", systemImage: "plus") }
                    .buttonStyle(.glassProminent).tint(launchBlue)
                Button { state.choose(.location) } label: { Label("添加游戏目录", systemImage: "folder.badge.plus") }
                    .buttonStyle(.glass)
            }.padding(.top, 3)
        }.padding(22).frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func row(_ item: Item) -> some View {
        let active = state.selected == item.id
        return HStack(spacing: 13) {
            artwork(item, size: 32)
            Text(item.title).font(.system(size: 15, weight: active ? .semibold : .regular)).lineLimit(1)
            Spacer()
            Text(state.lastPlayed(item)).font(.caption).foregroundStyle(muted)
        }
        .padding(.leading, 15).padding(.trailing, 19).frame(height: 55)
        .background {
            if active { RoundedRectangle(cornerRadius: 11).fill(blue.opacity(0.10)) }
        }
        .glassEffect(active && state.glass ? .regular.tint(blue.opacity(0.15)) : .identity, in: .rect(cornerRadius: 11))
        .overlay(alignment: .leading) {
            if active { Capsule().fill(blue.opacity(0.76)).frame(width: 3, height: 21).padding(.leading, 5) }
        }
        .overlay(alignment: .bottom) {
            if !active { rule.opacity(0.57).frame(height: 1).padding(.leading, 55).padding(.trailing, 12) }
        }
        .contentShape(Rectangle())
        .onTapGesture(count: 2) { state.selected = item.id; state.play(item) }
        .onTapGesture { state.selected = item.id; gamesFocused = true }
        .accessibilityAddTraits(active ? .isSelected : [])
        .accessibilityAction { state.selected = item.id }
    }
    private var inspector: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("游戏详情").font(.system(size: 13, weight: .semibold)).foregroundStyle(muted)
                Spacer()
                if let game {
                    Menu {
                        Button("在 Finder 中显示") { NSWorkspace.shared.activateFileViewerSelecting([game.url]) }
                        Button("从游戏库移除", role: .destructive) {
                            state.removeGame(game); reconcile()
                        }
                    } label: { Image(systemName: "ellipsis").frame(width: 21, height: 18) }
                    .menuStyle(.borderlessButton).menuIndicator(.hidden)
                    .padding(.horizontal, 5).padding(.vertical, 6)
                    .background(.white.opacity(dark ? 0.06 : 0.45), in: Capsule())
                    .glassEffect(state.glass ? .regular.interactive() : .identity)
                    .accessibilityLabel("更多操作")
                }
            }.padding(.bottom, 20)
            if let game {
                HStack(spacing: 11) {
                    artwork(game, size: 48)
                    Text(game.title).font(.system(size: 22, weight: .semibold)).lineLimit(2)
                }.padding(.bottom, 20)
                rule.frame(height: 1).modifier(InspectorEdgeAlignment()).padding(.bottom, 17)
                metadata("文件夹", game.url.lastPathComponent).help(game.url.path)
                ForEach(Array(game.credits.prefix(2).enumerated()), id: \.offset) { item in
                    Text(item.element).font(.caption).foregroundStyle(muted).padding(.top, 5)
                }
                metadata("上次游玩", state.lastPlayed(game)).padding(.top, 16)
                Spacer(minLength: 12)
                rule.frame(height: 1).modifier(InspectorEdgeAlignment()).padding(.bottom, 17)
                Button { state.play(game) } label: {
                    HStack(spacing: 8) {
                        Image(systemName: "play.fill").font(.system(size: 12, weight: .semibold))
                        Text(state.isPlaying ? "游戏运行中" : "开始游戏").font(.system(size: 15, weight: .semibold))
                    }.frame(maxWidth: .infinity).frame(height: 34)
                }.buttonStyle(.glassProminent).tint(launchBlue).disabled(state.isPlaying || searchFocused)
                  .keyboardShortcut(.return, modifiers: [])
                  .modifier(InspectorEdgeAlignment())
                  .padding(.bottom, 16)
            } else {
                Text("选择一个游戏查看详情").font(.callout).foregroundStyle(muted)
                Spacer()
            }
        }.padding(.top, 16).padding(.leading, 20).padding(.trailing, 14)
    }
    private func metadata(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            Text(label).font(.caption).foregroundStyle(muted)
            Text(value).font(.system(size: 13)).lineLimit(2)
        }
    }
    private func artwork(_ item: Item, size: CGFloat) -> some View {
        Group {
            if let url = item.icon, let art = LibraryImages.gameIcon(at: url) {
                Image(nsImage: art).resizable().interpolation(.none).scaledToFit()
            } else {
                Image(systemName: "gamecontroller").foregroundStyle(muted)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(.quaternary, in: RoundedRectangle(cornerRadius: size * 0.19))
            }
        }.frame(width: size, height: size).clipShape(.rect(cornerRadius: size * 0.19))
         .accessibilityHidden(true)
    }
}

private func adjacentID(_ ids: [Int], selected: Int, down: Bool) -> Int? {
    guard !ids.isEmpty else { return nil }
    guard let i = ids.firstIndex(of: selected) else { return down ? ids.first : ids.last }
    return ids[min(ids.count - 1, max(0, i + (down ? 1 : -1)))]
}

@MainActor private enum LibraryImages {
    static let wordmark = Bundle.main.url(forResource: "CPyMOWordmark", withExtension: "png")
        .flatMap(NSImage.init(contentsOf:))
    private static let icons: NSCache<NSString, NSImage> = {
        let cache = NSCache<NSString, NSImage>()
        cache.countLimit = 256
        return cache
    }()
    static func gameIcon(at url: URL) -> NSImage? {
        guard let meta = try? url.resourceValues(forKeys: [.contentModificationDateKey, .fileSizeKey]) else {
            return nil
        }
        let stamp = meta.contentModificationDate?.timeIntervalSinceReferenceDate ?? 0
        let key = "\(url.path)|\(stamp)|\(meta.fileSize ?? 0)" as NSString
        if let cached = icons.object(forKey: key) { return cached }
        guard let image = NSImage(contentsOf: url) else { return nil }
        icons.setObject(image, forKey: key)
        return image
    }
}

private struct InspectorEdgeAlignment: ViewModifier {
    func body(content: Content) -> some View {
        // The inspector's text keeps its 20/14 inset. Rules and button use 17/17.
        content.padding(.leading, -3).padding(.trailing, 3)
    }
}

private struct ToolSurface: ViewModifier {
    let dark: Bool
    let active: Bool
    let interactive: Bool
    func body(content: Content) -> some View {
        content
            .background(.white.opacity(dark ? 0.07 : 0.50), in: Capsule())
            .glassEffect(active ? (interactive ? .regular.interactive() : .regular) : .identity, in: .capsule)
    }
}

private enum PreviewAppearance: String, CaseIterable, Identifiable {
    case system = "跟随系统", light = "浅色", dark = "深色"
    var id: Self { self }
    var scheme: ColorScheme? {
        switch self { case .system: nil; case .light: .light; case .dark: .dark }
    }
    var icon: String {
        switch self { case .system: "circle.lefthalf.filled"; case .light: "sun.max"; case .dark: "moon" }
    }
}

private enum LibraryMode: String, Hashable {
    case list, icons
}
private struct LibraryDisplaySwitch: View {
    let selection: LibraryMode
    let accent: Color
    let dark: Bool
    let choose: (LibraryMode) -> Void

    private static let segment: CGFloat = 33
    private static let inset: CGFloat = 2
    private static let height: CGFloat = 26
    private static let snap = Animation.spring(response: 0.29, dampingFraction: 0.72)
    @StateObject private var motion = SwitchMotion()

    static func position(at x: CGFloat) -> CGFloat {
        min(segment, max(0, x - inset - segment / 2))
    }
    static func destination(at x: CGFloat) -> LibraryMode {
        x < inset + segment ? .list : .icons
    }

    var body: some View {
        let resting = selection == .list ? 0 : Self.segment
        ZStack(alignment: .leading) {
                Capsule().fill(.primary.opacity(dark ? 0.085 : 0.065))
                Capsule().strokeBorder(.primary.opacity(dark ? 0.12 : 0.07), lineWidth: 1)
                GlassEffectContainer(spacing: 0) {
                    Capsule()
                    .fill(accent.opacity(motion.position == nil ? 0.09 : 0.16))
                    .frame(width: Self.segment, height: Self.height - Self.inset * 2)
                    .glassEffect(.regular.tint(accent.opacity(0.21)).interactive(), in: .capsule)
                    .scaleEffect(motion.position == nil ? 1 : 1.07)
                    .offset(x: Self.inset + (motion.position ?? resting))
                    .animation(Self.snap, value: selection)
                    .animation(Self.snap, value: motion.position == nil)
                    .frame(width: Self.segment * 2 + Self.inset * 2, height: Self.height, alignment: .leading)
                }
                .allowsHitTesting(false).accessibilityHidden(true)
                // Keep actionable symbols above, outside the glass compositing container.
                HStack(spacing: 0) {
                    control(.list, "列表", "list.bullet")
                    control(.icons, "图标", "square.grid.2x2")
                }.padding(.horizontal, Self.inset).zIndex(1)
        }
            .frame(width: Self.segment * 2 + Self.inset * 2, height: Self.height)
            .contentShape(Capsule())
            .coordinateSpace(name: "CPyMO.LibraryDisplaySwitch")
            .simultaneousGesture(
                DragGesture(minimumDistance: 4, coordinateSpace: .named("CPyMO.LibraryDisplaySwitch"))
                    .onChanged { gesture in
                        motion.wasDragged = true
                        motion.position = Self.position(at: gesture.location.x)
                    }
                    .onEnded { gesture in
                        let next = Self.destination(at: gesture.location.x)
                        choose(next)
                        motion.position = nil
                        DispatchQueue.main.async { motion.wasDragged = false }
                    }
            )
            .accessibilityElement(children: .contain)
            .accessibilityLabel("游戏显示方式")
            .help("点击或拖动切换列表与图标")
    }
    private func control(_ mode: LibraryMode, _ title: String, _ icon: String) -> some View {
        Button {
            guard !motion.wasDragged else { return }
            choose(mode)
        } label: {
            Image(systemName: icon)
                .font(.system(size: 12, weight: selection == mode ? .semibold : .medium))
                .foregroundStyle(selection == mode
                    ? (dark ? Color.white : Color(red: 0.10, green: 0.16, blue: 0.30))
                    : Color.primary.opacity(0.69))
                .frame(width: Self.segment, height: Self.height)
                .contentShape(Rectangle())
        }.buttonStyle(.plain)
            .accessibilityLabel(title)
            .accessibilityAddTraits(selection == mode ? .isSelected : [])
    }
}
@MainActor private final class SwitchMotion: ObservableObject {
    @Published var position: CGFloat?
    var wasDragged = false
}
private enum TileLayout {
    static func columns(for width: CGFloat) -> Int {
        max(1, Int((max(0, width - 28) + 10) / (106 + 10)))
    }
    static func move(_ ids: [Int], from selected: Int, direction: MoveCommandDirection,
                     columns: Int, mode: LibraryMode) -> Int? {
        if mode == .list {
            guard direction == .up || direction == .down else { return nil }
            return adjacentID(ids, selected: selected, down: direction == .down)
        }
        guard !ids.isEmpty else { return nil }
        guard let i = ids.firstIndex(of: selected) else { return ids.first }
        let c = max(1, columns)
        let next: Int
        switch direction {
        case .left: next = max(0, i - 1)
        case .right: next = min(ids.count - 1, i + 1)
        case .up: next = i >= c ? i - c : i
        case .down: next = (i / c + 1) * c < ids.count ? min(i + c, ids.count - 1) : i
        @unknown default: return nil
        }
        return ids[next]
    }
}

private enum ImportKind: String, Identifiable {
    case game = "添加游戏", location = "添加游戏目录"
    var id: Self { self }
}

private struct PreviewLocation: Identifiable {
    let id: Int
    let url: URL
    let online: Bool
    var name: String { url.lastPathComponent }
}

@MainActor private final class PreviewState: ObservableObject {
    @Published private(set) var games: [Item] = []
    @Published private(set) var locations: [PreviewLocation] = []
    @Published private(set) var isPlaying = false
    @Published var message: String?
    var recent: [String: Date] = [:]
    private var manual: Set<URL> = []
    private var roots: Set<URL> = []
    private var ignored: Set<URL> = []
    private var visibleAtRoot: [Int: Set<URL>] = [:]
    private var snapshotAtRoot: [URL: [Item]] = [:]
    private var stableIDs: [URL: Int] = [:]
    private var nextID = 3
    private let defaults: UserDefaults?
    @Published private(set) var libraryMode: LibraryMode = .list
    func setLibraryMode(_ mode: LibraryMode) {
        libraryMode = mode
        defaults?.set(mode.rawValue, forKey: "libraryDisplayMode")
    }
    private var running: NSRunningApplication?
    private var runningObserver: NSKeyValueObservation?

    init(defaults: UserDefaults? = .standard) {
        self.defaults = defaults
        libraryMode = defaults?.string(forKey: "libraryDisplayMode").flatMap(LibraryMode.init(rawValue:)) ?? .list
        #if CPYMO_DEMO
        if let demo = Bundle.main.resourceURL?.appendingPathComponent("DemoLibrary") { roots = [demo] }
        return
        #endif
        let previous = defaults == nil ? nil : UserDefaults(suiteName: "local.cpymo.macos.library.preview")
        let earlier = defaults == nil ? nil : UserDefaults(suiteName: "local.cpymo.macos.library")
        func stored(_ key: String) -> Set<URL> {
            Set((defaults?.stringArray(forKey: key) ?? []).map { Self.canonical(URL(fileURLWithPath: $0)) })
        }
        manual = stored("manualGamePaths"); roots = stored("libraryDirectories"); ignored = stored("ignoredGamePaths")
        if let defaults, defaults.object(forKey: "manualGamePaths") == nil {
            let paths = previous?.string(forKey: "paths") ?? earlier?.string(forKey: "paths") ?? defaults.string(forKey: "paths") ?? ""
            if let old = try? JSONDecoder().decode([URL].self, from: Data(paths.utf8)) {
                manual = Set(old.map(Self.canonical))
            }
        }
        recent = (defaults?.data(forKey: "recents") ?? previous?.data(forKey: "recents") ?? earlier?.data(forKey: "recents"))
            .flatMap { try? JSONDecoder().decode([String: Date].self, from: $0) } ?? [:]
    }
    private static func canonical(_ url: URL) -> URL { url.resolvingSymlinksInPath().standardizedFileURL }
    private func id(for url: URL) -> Int {
        if let prior = stableIDs[url] { return prior }
        nextID += 1; stableIDs[url] = nextID; return nextID
    }
    private func readGame(_ input: URL) -> Item? {
        guard let game = Game.read(input) else { return nil }
        return Item(id: id(for: game.url), url: game.url, title: game.title, credits: game.credits, icon: game.icon)
    }
    func refresh() {
        var resolved: [URL: Item] = [:]
        for url in manual {
            if let game = readGame(url) { resolved[game.url] = game }
        }
        var places: [PreviewLocation] = []
        var membership: [Int: Set<URL>] = [:]
        for url in roots.sorted(by: { $0.path.localizedStandardCompare($1.path) == .orderedAscending }) {
            let i = id(for: url)
            let children = try? FileManager.default.contentsOfDirectory(
                at: url, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles])
            places.append(PreviewLocation(id: i, url: url, online: children != nil))
            let current: [Item]
            if let children {
                current = children.filter { (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true }
                    .compactMap(readGame)
                snapshotAtRoot[url] = current
            } else { current = snapshotAtRoot[url] ?? [] }
            membership[i] = Set(current.map(\.url))
            for game in current where !ignored.contains(game.url) { resolved[game.url] = game }
        }
        visibleAtRoot = membership
        locations = places
        games = resolved.values.sorted {
            let l = recent[$0.url.path] ?? .distantPast, r = recent[$1.url.path] ?? .distantPast
            return l == r ? $0.title.localizedStandardCompare($1.title) == .orderedAscending : l > r
        }
        if !visibleGames.contains(where: { $0.id == selected }) { selected = visibleGames.first?.id ?? 0 }
    }
    var visibleGames: [Item] {
        if tab == 1 { return games.filter { recent[$0.url.path] != nil } }
        if tab >= 2 { return games.filter { visibleAtRoot[tab]?.contains($0.url) == true } }
        return games
    }
    func lastPlayed(_ game: Item) -> String {
        guard let date = recent[game.url.path] else { return "未游玩" }
        if Date().timeIntervalSince(date) < 60 { return "刚刚" }
        if Calendar.current.isDateInToday(date) { return "今天" }
        if Calendar.current.isDateInYesterday(date) { return "昨天" }
        let f = DateFormatter(); f.locale = Locale(identifier: "zh_CN"); f.setLocalizedDateFormatFromTemplate("MMMd")
        return f.string(from: date)
    }
    private func save() {
        defaults?.set(manual.map(\.path).sorted(), forKey: "manualGamePaths")
        defaults?.set(roots.map(\.path).sorted(), forKey: "libraryDirectories")
        defaults?.set(ignored.map(\.path).sorted(), forKey: "ignoredGamePaths")
        if let data = try? JSONEncoder().encode(recent) { defaults?.set(data, forKey: "recents") }
    }
    @discardableResult func addGame(_ url: URL) -> Bool {
        guard let game = readGame(url) else { return false }
        manual.insert(game.url); ignored.remove(game.url)
        tab = 0; query = ""; save(); refresh(); selected = game.id
        return true
    }
    @discardableResult func addDirectory(_ input: URL) -> Bool {
        let url = Self.canonical(input)
        guard FileManager.default.isReadableFile(atPath: url.path), Game.read(url) == nil,
              (try? url.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true else { return false }
        roots.insert(url); tab = id(for: url); query = ""; save(); refresh()
        return true
    }
    func removeGame(_ game: Item) { manual.remove(game.url); ignored.insert(game.url); save(); refresh() }
    func removeLocation(_ place: PreviewLocation) {
        roots.remove(place.url); snapshotAtRoot[place.url] = nil
        if tab == place.id { tab = 0 }; save(); refresh()
    }
    private var activePanel: NSOpenPanel?
    func choose(_ type: ImportKind) {
        if let current = activePanel { current.makeKeyAndOrderFront(nil); return }
        // Start outside SwiftUI's view update; AppKit manages the nonblocking sheet lifecycle.
        DispatchQueue.main.async { [weak self] in self?.presentImportPanel(type) }
    }
    private func presentImportPanel(_ type: ImportKind) {
        guard activePanel == nil else { return }
        let panel = NSOpenPanel()
        panel.title = type.rawValue
        panel.prompt = "添加"
        panel.canChooseFiles = false; panel.canChooseDirectories = true; panel.allowsMultipleSelection = false
        panel.message = type == .game ? "选择游戏文件夹" : "选择存放游戏的目录"
        activePanel = panel
        let finish: (NSApplication.ModalResponse) -> Void = { [weak self, weak panel] result in
            guard let self, let panel else { return }
            self.activePanel = nil
            guard result == .OK, let input = panel.url else { return }
            #if CPYMO_DEMO
            self.message = "演示版只展示界面，不会写入游戏库。"
            return
            #endif
            if type == .game {
                if !self.addGame(input) { self.message = "请选择包含 gameconfig.txt 的单个游戏文件夹。要添加整个目录，请使用左侧「位置」的加号。" }
            } else if !self.addDirectory(input) {
                self.message = "请选择存放游戏的上一级目录，而不是单个游戏文件夹。"
            }
        }
        if let window, window.isVisible { panel.beginSheetModal(for: window, completionHandler: finish) }
        else { panel.begin(completionHandler: finish) }
    }
    func play(_ game: Item) {
        #if CPYMO_DEMO
        message = "这是界面演示版，不能启动游戏。"
        return
        #endif
        guard !isPlaying else { return }
        guard Game.read(game.url) != nil else { message = "游戏目录暂不可用。"; return }
        do {
            let session = try GameSession.prepare(game)
            let settings = NSWorkspace.OpenConfiguration()
            settings.arguments = [game.url.path]
            settings.activates = true; settings.createsNewApplicationInstance = true
            isPlaying = true
            NSWorkspace.shared.openApplication(at: session.app, configuration: settings) { [self] app, error in
                Task { @MainActor in
                    if let app {
                        self.running = app
                        self.recent[game.url.path] = Date(); self.save(); self.refresh()
                        if app.isTerminated { self.completeGame(session); return }
                        self.runningObserver = app.observe(\.isTerminated, options: [.new]) { [weak self] _, change in
                            if change.newValue == true { Task { @MainActor in self?.completeGame(session) } }
                        }
                    } else {
                        self.completeGame(session)
                        self.message = "启动失败：\(error?.localizedDescription ?? "无法打开游戏")"
                    }
                }
            }
        } catch { message = "启动失败：\(error.localizedDescription)" }
    }
    private func completeGame(_ session: GameSession) {
        runningObserver = nil; running = nil; isPlaying = false
        try? FileManager.default.removeItem(at: session.folder)
    }
    private var clientTestStarted = false
    func testClient(root: URL, out: String) {
        guard !clientTestStarted else { return }
        try? "started".write(toFile: out + ".started", atomically: true, encoding: .utf8)
        clientTestStarted = true
        func wrap(_ lines: [String], pass: Bool) {
            try? (lines.joined(separator: "\n") + "\n" + (pass ? "PASS" : "FAIL"))
                .write(toFile: out, atomically: true, encoding: .utf8)
            NSApp.terminate(nil)
        }
        let movie = root.appendingPathComponent("movie")
        let first = addGame(movie)
        let single = games.count == 1 && locations.isEmpty
        let rootsOK = addDirectory(root) && games.count == 15 && locations.count == 1
        let newDir = root.appendingPathComponent("discovered")
        try? FileManager.default.createDirectory(at: newDir, withIntermediateDirectories: true)
        try? "gametitle,新增样本".write(to: newDir.appendingPathComponent("gameconfig.txt"), atomically: true, encoding: .utf8)
        refresh()
        let rescan = games.count == 16
        tab = 0
        refresh()
        guard first, single, rootsOK, rescan,
              let fixture = games.first(where: { $0.url == Self.canonical(movie) }) else {
            wrap(["single=\(single) roots=\(rootsOK) rescan=\(rescan) total=\(games.count)"], pass: false); return
        }
        func scrollViews(_ v: NSView) -> [NSScrollView] {
            (v as? NSScrollView).map { [$0] } ?? v.subviews.flatMap(scrollViews)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(500)) {
            let scrolls = self.window?.contentView.map(scrollViews) ?? []
            let overflow = scrolls.contains { $0.hasVerticalScroller && ($0.documentView?.bounds.height ?? 0) > $0.contentSize.height }
            let help = NSApp.mainMenu?.items.contains {
                $0.submenu?.item(withTitle: "关于 CPyMO") != nil && $0 !== NSApp.mainMenu?.items.first
            } == true
            var lines = ["single, folder, dedup, detection: PASS", "scroll viewport overflow: \(overflow) (\(scrolls.count) scroll views)", "launcher About in Help: \(help)"]
            self.play(fixture)
            @MainActor func verify(_ count: Int) {
                if count > 35 {
                    if let app = self.running { _ = app.terminate() }
                    wrap(lines + ["launch timeout: \(self.message ?? "-")"], pass: false); return
                }
                if let app = self.running, !app.isTerminated,
                   let location = app.bundleURL, let gameBundle = Bundle(url: location) {
                    let name = app.localizedName ?? ""
                    let icon = gameBundle.url(forResource: "GameIcon", withExtension: "icns")
                    let iconOK = icon.flatMap(NSImage.init(contentsOf:)) != nil
                    let creditsOK = gameBundle.url(forResource: "About", withExtension: "txt") != nil
                    lines += ["running game: \(name); PID=\(app.processIdentifier)", "padded icon decodes: \(iconOK); shared About: \(creditsOK)"]
                    let nativeName = name == fixture.title
                    DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(650)) {
                        let alive = !app.isTerminated
                        _ = app.terminate()
                        wrap(lines + ["game remained running: \(alive)"], pass: overflow && help && nativeName && iconOK && creditsOK && alive)
                    }
                } else {
                    DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(150)) { verify(count + 1) }
                }
            }
            verify(0)
        }
    }
    @Published var tab = 0
    @Published var selected = 0
    @Published var query = ""
    @Published var glass = true
    @Published private(set) var appearance: PreviewAppearance = .system
    @Published private var systemScheme: ColorScheme = PreviewState.systemColorScheme()
    private var observation: NSKeyValueObservation?
    private weak var window: NSWindow?
    private var didVerifyWindow = false
    private var didVerifyPanel = false
    private var didVerifyReopen = false

    var effectiveScheme: ColorScheme { appearance.scheme ?? systemScheme }

    private static func scheme(for appearance: NSAppearance?) -> ColorScheme {
        appearance?.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? .dark : .light
    }

    private static func systemColorScheme() -> ColorScheme {
        scheme(for: NSApp?.effectiveAppearance)
    }

    private func syncSystemAppearance() {
        let current = PreviewState.systemColorScheme()
        if systemScheme != current { systemScheme = current }
        if appearance == .system { refreshWindow() }
    }

    func attach(_ w: NSWindow) {
        if window === w { return }
        window = w
        observation = NSApp.observe(\.effectiveAppearance, options: [.new]) { [weak self] _, _ in
            Task { @MainActor in self?.syncSystemAppearance() }
        }
        syncSystemAppearance()
        applyWindowAppearance(appearance)
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--test-client"), args.count > i + 2 {
            let root = URL(fileURLWithPath: args[i + 1]), out = args[i + 2]
            DispatchQueue.main.async { self.testClient(root: root, out: out) }
        }
        if let i = args.firstIndex(of: "--test-import-panels"), args.count > i+1, !didVerifyPanel {
            didVerifyPanel = true
            let out = args[i+1]
            DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(400)) {
                self.choose(.game)
                DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(500)) {
                    let game = self.activePanel
                    let single = game != nil && game === w.attachedSheet && game?.title == "添加游戏" && game?.message == "选择游戏文件夹"
                        && game?.canChooseDirectories == true && game?.allowsMultipleSelection == false
                    game?.cancel(nil)
                    DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(380)) {
                        self.choose(.location)
                        DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(500)) {
                            let location = self.activePanel
                            let folder = location != nil && location === w.attachedSheet && location?.title == "添加游戏目录" && location?.message == "选择存放游戏的目录"
                            location?.cancel(nil)
                            let credits = Bundle.main.url(forResource: "About", withExtension: "txt")
                                .flatMap { try? String(contentsOf: $0, encoding: .utf8) } ?? ""
                            let lines = credits.split(whereSeparator: \.isNewline).map(String.init)
                            let widths = lines.map { ($0 as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: 12)]).width }
                            let fit = lines.count == 3 && widths.allSatisfy { $0 < 160 }
                            let msg = "game sheet=\(single); directory sheet=\(folder); credits widths=\(widths)\n" + (single && folder && fit ? "PASS" : "FAIL")
                            try? msg.write(toFile: out, atomically: true, encoding: .utf8)
                            NSApp.terminate(nil)
                        }
                    }
                }
            }
        }
        if !didVerifyReopen, let i = args.firstIndex(of: "--test-reopen"), args.count > i+1 {
            didVerifyReopen = true
            let out = args[i+1]
            DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(350)) {
                w.performClose(nil)
                DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(250)) {
                    try? "window closed=\(!w.isVisible); pid=\(ProcessInfo.processInfo.processIdentifier)"
                      .write(toFile: out+".closed", atomically: true, encoding: .utf8)
                    DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(3)) {
                        let reopened = NSApp.windows.contains { $0.title == "游戏库" && $0.isVisible }
                        try? "reopened=\(reopened); pid=\(ProcessInfo.processInfo.processIdentifier)"
                          .write(toFile: out, atomically: true, encoding: .utf8)
                        NSApp.terminate(nil)
                    }
                }
            }
        }
        if !didVerifyWindow, let n = args.firstIndex(of: "--test-window-appearance"), args.indices.contains(n + 1) {
            didVerifyWindow = true
            let path = args[n + 1]
            DispatchQueue.main.async { self.verifyWindowAppearance(path: path) }
        }
    }

    func setAppearance(_ choice: PreviewAppearance) {
        if choice == .system { syncSystemAppearance() }
        appearance = choice
        applyWindowAppearance(choice)
    }

    private func applyWindowAppearance(_ choice: PreviewAppearance) {
        switch choice {
        case .system: window?.appearance = nil
        case .light: window?.appearance = NSAppearance(named: .aqua)
        case .dark: window?.appearance = NSAppearance(named: .darkAqua)
        }
        refreshWindow()
    }

    private func refreshWindow() {
        window?.contentView?.needsDisplay = true
        window?.invalidateShadow()
    }

    private func verifyWindowAppearance(path: String) {
        guard let window else { return }
        // Simulate the two system appearances in this test process only.
        var lines: [String] = []
        var pass = true
        func inspect(_ label: String) -> [NSRect] {
            window.contentView?.superview?.layoutSubtreeIfNeeded()
            let frames = [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton].compactMap { kind -> NSRect? in
                guard let button = window.standardWindowButton(kind), !button.isHidden else { return nil }
                return window.convertToScreen(button.convert(button.bounds, to: nil))
            }
            let centers = frames.map { window.frame.maxY - $0.midY }
            lines.append("\(label): native toolbar=\(window.toolbar != nil), style=\(window.toolbarStyle.rawValue) \(frames.map { "\($0.width)x\($0.height)" }) top-centers=\(centers)")
            pass = pass && window.toolbar != nil && frames.count == 3
                && centers.allSatisfy { $0 >= 20 }
            return frames
        }
        let initial = inspect("initial")
        lines.append("title=\(window.title) default-frame=\(window.frame.size)")
        if let items = window.toolbar?.visibleItems {
            for item in items {
                let box = item.view.flatMap { $0.window == window ? $0.convert($0.bounds, to: nil) : nil }
                lines.append("toolbar-item=\(item.itemIdentifier.rawValue) frame=\(String(describing: box))")
            }
        }

        let scenarios: [(NSAppearance.Name, PreviewAppearance)] = [(.darkAqua, .light), (.aqua, .dark)]
        func run(_ index: Int) {
            guard index < scenarios.count else {
                NSApp.appearance = nil
                try? (lines.joined(separator: "\n") + "\n" + (pass ? "PASS" : "FAIL"))
                    .write(toFile: path, atomically: true, encoding: .utf8)
                NSApp.terminate(nil)
                return
            }
            let (base, manual) = scenarios[index]
            NSApp.appearance = NSAppearance(named: base)
            // Let the simulated OS preference propagate before exercising the menu action.
            DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(150)) {
                self.setAppearance(manual)
                DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(150)) {
                    let forcedFrames = inspect("manual-\(manual.rawValue)")
                    let explicitOK = Self.scheme(for: window.effectiveAppearance) == manual.scheme
                        && self.effectiveScheme == manual.scheme
                    self.setAppearance(.system)
                    let expected: ColorScheme = base == .darkAqua ? .dark : .light
                    let followed = Self.scheme(for: window.effectiveAppearance) == expected
                        && self.effectiveScheme == expected
                    let restoredFrames = inspect("restored-\(base.rawValue)")
                    lines.append("system=\(base.rawValue) forced=\(explicitOK) restored-immediately=\(followed)")
                    pass = pass && explicitOK && followed
                        && forcedFrames.map(\.size) == initial.map(\.size)
                        && restoredFrames.map(\.size) == initial.map(\.size)
                    run(index + 1)
                }
            }
        }
        run(0)
    }
    @Published var showSidebar = true
    @Published var showDetails = true
    @Published var reversed = false
}

private struct WindowAppearanceLink: NSViewRepresentable {
    let state: PreviewState
    func makeNSView(context: Context) -> BridgeView {
        let v = BridgeView()
        v.state = state
        return v
    }
    func updateNSView(_ view: BridgeView, context: Context) { view.state = state }
    final class BridgeView: NSView {
        weak var state: PreviewState?
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let window { state?.attach(window) }
        }
    }
}

@main struct PreviewApp: App {
    private let delegate = MenuDelegate()
    #if CPYMO_DEMO
    @StateObject private var state = PreviewState(defaults: nil)
    #else
    @StateObject private var state = PreviewState(defaults: (CommandLine.arguments.contains("--test-client") || CommandLine.arguments.contains("--test-reopen") || CommandLine.arguments.contains("--test-import-panels")) ? nil : .standard)
    #endif
    init() {
        if CommandLine.arguments.contains("--test-grid") {
            let ids = Array(0..<16)
            precondition(LibraryDisplaySwitch.position(at: -20) == 0)
            precondition(LibraryDisplaySwitch.position(at: 100) == 33)
            precondition(LibraryDisplaySwitch.destination(at: 16) == .list)
            precondition(LibraryDisplaySwitch.destination(at: 51) == .icons)
            precondition(TileLayout.columns(for: 560) == 4)
            precondition(TileLayout.columns(for: 400) == 3)
            precondition(TileLayout.move(ids, from: 1, direction: .down, columns: 4, mode: .icons) == 5)
            precondition(TileLayout.move(ids, from: 5, direction: .up, columns: 4, mode: .icons) == 1)
            precondition(TileLayout.move(ids, from: 5, direction: .left, columns: 4, mode: .icons) == 4)
            precondition(TileLayout.move(ids, from: 5, direction: .right, columns: 4, mode: .icons) == 6)
            precondition(TileLayout.move(ids, from: 15, direction: .down, columns: 4, mode: .icons) == 15)
            let key = "local.cpymo.qa." + UUID().uuidString
            if let test = UserDefaults(suiteName: key) {
                let a = PreviewState(defaults: test)
                precondition(a.libraryMode == .list)
                a.selected = 3
                a.setLibraryMode(.icons)
                precondition(a.selected == 3 && PreviewState(defaults: test).libraryMode == .icons)
                test.removePersistentDomain(forName: key)
            }
            print("PASS: adaptive grid, four directions, retained selection, list default and saved preference")
            exit(0)
        }
        if CommandLine.arguments.contains("--test-selection") {
            let ids = [0, 2, 4]
            precondition(adjacentID(ids, selected: 0, down: true) == 2)
            precondition(adjacentID(ids, selected: 4, down: false) == 2)
            precondition(adjacentID(ids, selected: 4, down: true) == 4)
            precondition(adjacentID(ids, selected: 0, down: false) == 0)
            precondition(adjacentID([], selected: 0, down: true) == nil)
            print("PASS: selection follows visible rows; boundaries are stable")
            exit(0)
        }
    }
    var body: some Scene {
        WindowGroup("游戏库", id: "library") {
            OriginalPreview(state: state, delegate: delegate).frame(minWidth: 960, minHeight: 540)
        }.defaultSize(width: 1152, height: 648).defaultLaunchBehavior(.presented)
        .commands {
            CommandGroup(replacing: .appInfo) {}
            CommandGroup(replacing: .help) {
                Button("关于 CPyMO") { delegate.about() }
            }
            CommandGroup(replacing: .newItem) {
                Button("添加游戏…") { state.choose(.game) }.keyboardShortcut("o", modifiers: .command)
                Button("添加游戏目录…") { state.choose(.location) }.keyboardShortcut("o", modifiers: [.command, .shift])
            }
        }
    }
}

@MainActor private struct GameSession {
    let folder: URL
    let app: URL
    static func prepare(_ game: Item) throws -> Self {
        let fm = FileManager.default
        let parent = fm.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CPyMO/Game Sessions", isDirectory: true)
        return try prepare(game, under: parent)
    }
    static func prepare(_ game: Item, under parent: URL) throws -> Self {
        let fm = FileManager.default
        let folder = parent.appendingPathComponent(UUID().uuidString, isDirectory: true)
        let safeName = game.title.replacingOccurrences(of: "/", with: "／").replacingOccurrences(of: ":", with: "：")
        let app = folder.appendingPathComponent(String(safeName.prefix(70)) + ".app", isDirectory: true)
        let contents = app.appendingPathComponent("Contents", isDirectory: true)
        let macos = contents.appendingPathComponent("MacOS", isDirectory: true)
        let resources = contents.appendingPathComponent("Resources", isDirectory: true)
        let bundle = Bundle.main
        do {
            try fm.createDirectory(at: macos, withIntermediateDirectories: true)
            try fm.createDirectory(at: resources, withIntermediateDirectories: true)
            for (name, ext, destination) in [("CPyMO", nil, macos), ("CPyMO", "icns", resources), ("About", "txt", resources)] as [(String, String?, URL)] {
                guard let src = bundle.url(forResource: name, withExtension: ext) else {
                    throw CocoaError(.fileNoSuchFile)
                }
                try fm.copyItem(at: src, to: destination.appendingPathComponent(src.lastPathComponent))
            }
            var iconName = "CPyMO.icns"
            if let icon = game.icon.flatMap(NSImage.init(contentsOf:)), let icns = tileIcon(icon) {
                iconName = "GameIcon.icns"
                try icns.write(to: resources.appendingPathComponent(iconName), options: .atomic)
            }
            let props: [String: Any] = [
                "CFBundleIdentifier": "local.cpymo.macos.game." + folder.lastPathComponent.lowercased(),
                "CFBundleName": game.title, "CFBundleDisplayName": game.title,
                "CFBundleDevelopmentRegion": "zh_CN", "CFBundleLocalizations": ["zh-Hans"],
                "CFBundleExecutable": "CPyMO", "CFBundlePackageType": "APPL",
                "CFBundleIconFile": iconName, "LSMinimumSystemVersion": "27.2", "NSHighResolutionCapable": true,
                "CFBundleShortVersionString": "0.3.6", "CFBundleVersion": "10"
            ]
            let data = try PropertyListSerialization.data(fromPropertyList: props, format: .xml, options: 0)
            try data.write(to: contents.appendingPathComponent("Info.plist"), options: .atomic)
            // This is a fresh, private session bundle; signing does not modify the installed app or game.
            let signer = Process()
            signer.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
            signer.arguments = ["--force", "--sign", "-", app.path]
            signer.standardError = FileHandle.nullDevice
            try signer.run(); signer.waitUntilExit()
            guard signer.terminationStatus == 0 else {
                throw NSError(domain: "CPyMO", code: 1, userInfo: [NSLocalizedDescriptionKey: "无法为游戏窗口准备签名。"])
            }
            return Self(folder: folder, app: app)
        } catch {
            try? fm.removeItem(at: folder)
            throw error
        }
    }
    private static func tileIcon(_ source: NSImage) -> Data? {
        let n = 256
        guard source.size.width > 0, source.size.height > 0,
              let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: n, pixelsHigh: n, bitsPerSample: 8,
                    samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0),
              let context = NSGraphicsContext(bitmapImageRep: rep) else { return nil }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = context
        context.imageInterpolation = NSImageInterpolation.none
        context.cgContext.clear(CGRect(x: 0, y: 0, width: n, height: n))
        let k = 202.0 / max(source.size.width, source.size.height)
        let w = source.size.width * k, h = source.size.height * k
        source.draw(in: CGRect(x: (Double(n) - w)/2, y: (Double(n) - h)/2, width: w, height: h))
        NSGraphicsContext.restoreGraphicsState()
        guard let png = rep.representation(using: .png, properties: [:]) else { return nil }
        func size(_ value: Int) -> Data {
            var n = UInt32(value).bigEndian
            return withUnsafeBytes(of: &n) { Data($0) }
        }
        return Data("icns".utf8) + size(16 + png.count) + Data("ic08".utf8) + size(8 + png.count) + png
    }
}
