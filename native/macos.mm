#import <AppKit/AppKit.h>

bool athanorMacAnimationsEnabled()
{
    return ![[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}
