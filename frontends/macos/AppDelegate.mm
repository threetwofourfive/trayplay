#import <Cocoa/Cocoa.h>

// Dummy Objective-C++ AppKit entry point for macOS frontend
@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (strong, nonatomic) NSStatusItem *statusItem;
@property (strong, nonatomic) NSPopover *popover;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // Initialize NSStatusItem and NSPopover
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Teardown
}

@end

int main(int argc, const char * argv[]) {
    return NSApplicationMain(argc, argv);
}
