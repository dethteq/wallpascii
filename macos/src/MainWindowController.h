#import <AppKit/AppKit.h>

// The single wallpascii window: settings on the left, live preview on the
// right, output options and Save at the bottom.
@interface MainWindowController : NSWindowController

- (void)openDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)openPhotoAtPath:(NSString*)path;

@end
