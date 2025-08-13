#include "../window.hpp"

#ifdef __APPLE__
#include <Cocoa/Cocoa.h>

namespace lizard::platform {

namespace {
NSWindow *g_window = nil;

float compute_dpi(NSWindow *w) {
  NSScreen *screen = [w screen];
  CGFloat dpi = [screen backingScaleFactor] * 96.0;
  return dpi / 96.0f;
}
} // namespace

Window create_overlay_window(const WindowDesc &desc) {
  Window result{};
  @autoreleasepool {
    NSUInteger style = NSWindowStyleMaskBorderless;
    g_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, desc.width, desc.height)
                                           styleMask:style
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    [g_window setLevel:NSStatusWindowLevel];
    [g_window setOpaque:NO];
    [g_window setIgnoresMouseEvents:YES];
    [g_window makeKeyAndOrderFront:nil];

    NSOpenGLPixelFormatAttribute attrs[] = {NSOpenGLPFAOpenGLProfile,
                                            NSOpenGLProfileVersion3_2Core,
                                            NSOpenGLPFAColorSize,
                                            24,
                                            NSOpenGLPFAAlphaSize,
                                            8,
                                            NSOpenGLPFADoubleBuffer,
                                            NSOpenGLPFAAccelerated,
                                            0};
    NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    NSOpenGLContext *ctx = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
    [pf release];
    [ctx setView:[g_window contentView]];
    [ctx makeCurrentContext];

    result.native = g_window;
    result.dpiScale = compute_dpi(g_window);
    result.glContext = ctx;
  }
  return result;
}

void destroy_window(Window &window) {
  @autoreleasepool {
    if (window.glContext) {
      [(NSOpenGLContext *)window.glContext release];
      window.glContext = nullptr;
    }
    if (g_window) {
      [g_window close];
      g_window = nil;
    }
    window.native = nullptr;
  }
}

void poll_events(Window &window) {
  @autoreleasepool {
    NSEvent *event;
    while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES])) {
      [NSApp sendEvent:event];
    }
  }
}

} // namespace lizard::platform

#endif
