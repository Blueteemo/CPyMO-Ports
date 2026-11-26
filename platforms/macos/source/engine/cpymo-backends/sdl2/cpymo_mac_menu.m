#import <Cocoa/Cocoa.h>
#include <stdio.h>
@interface CPyMOGameMenu : NSObject
- (void)showAbout:(id)sender;
@end
@implementation CPyMOGameMenu
- (void)showAbout:(id)sender {
    NSURL *creditsURL = [NSBundle.mainBundle URLForResource:@"About" withExtension:@"txt"];
    NSString *c = creditsURL ? [NSString stringWithContentsOfURL:creditsURL encoding:NSUTF8StringEncoding error:nil] : @"";
    NSMutableParagraphStyle *layout = [[NSMutableParagraphStyle alloc] init];
    layout.alignment = NSTextAlignmentCenter;
    layout.lineSpacing = 5;
    NSAttributedString *credits = [[NSAttributedString alloc] initWithString:
        [(c ?: @"") stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]
        attributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12], NSParagraphStyleAttributeName:layout}];
    NSURL *iconURL = [NSBundle.mainBundle URLForResource:@"CPyMO" withExtension:@"icns"];
    NSImage *icon = iconURL ? [[NSImage alloc] initWithContentsOfURL:iconURL] : nil;
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName: @"CPyMO",
        NSAboutPanelOptionApplicationVersion: [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"0.3.2",
        NSAboutPanelOptionVersion: @"CPyMO Aria 1.1.9",
        NSAboutPanelOptionApplicationIcon: icon ?: [NSImage imageNamed:NSImageNameApplicationIcon],
        NSAboutPanelOptionCredits: credits}];
}
@end
static CPyMOGameMenu *delegate;
static NSMenu *menu(NSMenu *bar, NSString *name) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
    item.submenu = [[NSMenu alloc] initWithTitle:name];
    [bar addItem:item];
    return item.submenu;
}
static NSMenuItem *add(NSMenu *m, NSString *name, SEL action, NSString *key, id target) {
    NSMenuItem *i = [m addItemWithTitle:name action:action keyEquivalent:key];
    i.target = target;
    return i;
}
static void cpymo_mac_translate_window_items(NSMenu *m) {
    if (!m) return;
    NSDictionary *titles = @{
        @"Fill": @"填满", @"Center": @"居中", @"Fill Center": @"填满／居中",
        @"Move & Resize": @"移动与调整大小", @"Full Screen Tile": @"全屏平铺",
        @"Left": @"左侧", @"Right": @"右侧", @"Top": @"上方", @"Bottom": @"下方",
        @"Arrange": @"排列", @"Return to Previous Size": @"恢复原大小",
        @"Move to Left Side of Screen": @"移到屏幕左侧", @"Move to Right Side of Screen": @"移到屏幕右侧",
        @"Tile Window to Left of Screen": @"将窗口拼贴到屏幕左侧",
        @"Tile Window to Right of Screen": @"将窗口拼贴到屏幕右侧",
        @"Enter Full Screen": @"进入全屏", @"Exit Full Screen": @"退出全屏",
        @"Bring All to Front": @"将全部窗口置于最前"
    };
    for (NSMenuItem *item in m.itemArray) {
        NSString *replacement = titles[item.title];
        if (replacement) item.title = replacement;
        cpymo_mac_translate_window_items(item.submenu);
    }
}
void cpymo_mac_prepare_menus(void) {
    NSString *title = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDisplayName"];
    NSProcessInfo.processInfo.processName = title.length ? title : @"CPyMO";
}
void cpymo_mac_apply_game_icon(void) {
    NSURL *url = [NSBundle.mainBundle URLForResource:@"GameIcon" withExtension:@"icns"];
    NSImage *image = url ? [[NSImage alloc] initWithContentsOfURL:url] : nil;
    if (image) NSApp.applicationIconImage = image;
}
void cpymo_mac_install_menus(void) {
    @autoreleasepool {
        delegate = [[CPyMOGameMenu alloc] init];
        cpymo_mac_apply_game_icon();
        NSMenu *bar = [[NSMenu alloc] initWithTitle:@""];
        NSMenu *app = menu(bar, @"CPyMO");
        add(app, @"隐藏 CPyMO", @selector(hide:), @"h", NSApp);
        add(app, @"隐藏其他应用", @selector(hideOtherApplications:), @"h", NSApp).keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        add(app, @"全部显示", @selector(unhideAllApplications:), @"", NSApp);
        [app addItem:[NSMenuItem separatorItem]];
        add(app, @"退出 CPyMO", @selector(terminate:), @"q", NSApp);
        NSMenu *window = menu(bar, @"窗口");
        add(window, @"最小化", @selector(performMiniaturize:), @"m", nil);
        add(window, @"缩放", @selector(performZoom:), @"", nil);
        [window addItem:[NSMenuItem separatorItem]];
        add(window, @"将全部窗口置于最前", @selector(arrangeInFront:), @"", NSApp);
        NSMenu *help = menu(bar, @"帮助");
        add(help, @"关于 CPyMO", @selector(showAbout:), @"", delegate);
        NSApp.mainMenu = bar;
        NSApp.windowsMenu = window;
        NSApp.helpMenu = help;
        cpymo_mac_translate_window_items(window);
        [NSNotificationCenter.defaultCenter addObserverForName:NSMenuDidBeginTrackingNotification object:nil
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
                NSMenu *tracked = note.object;
                for (NSMenu *m = tracked; m; m = m.supermenu) {
                    if (m == window) { cpymo_mac_translate_window_items(window); break; }
                }
            }];
    }
}
int cpymo_mac_verify_menus(void) {
    NSMenu *app = NSApp.mainMenu.itemArray.firstObject.submenu;
    NSMenu *help = NSApp.helpMenu;
    int ok = NSProcessInfo.processInfo.processName.length > 0 &&
       [app.title isEqualToString:@"CPyMO"] && [help.title isEqualToString:@"帮助"] &&
       [app itemWithTitle:@"关于 CPyMO"] == nil && [help itemWithTitle:@"关于 CPyMO"] != nil &&
       [help itemWithTitle:@"CPyMO 帮助"] == nil;
    NSMenu *test = [[NSMenu alloc] init];
    NSMenuItem *fill = [test addItemWithTitle:@"Fill" action:@selector(zoom:) keyEquivalent:@""];
    NSMenuItem *center = [test addItemWithTitle:@"Center" action:@selector(center) keyEquivalent:@""];
    cpymo_mac_translate_window_items(test);
    ok = ok && [fill.title isEqualToString:@"填满"] && [center.title isEqualToString:@"居中"]
        && fill.action == @selector(zoom:) && center.action == @selector(center);
    if (ok) puts("PASS: CPyMO game menu, About under Help, Fill and Center translated without changing actions");
    return ok;
}
