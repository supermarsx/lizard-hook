#include "../window.hpp"

#ifdef __APPLE__
#include <Cocoa/Cocoa.h>
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <vector>

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
    g_window =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(desc.x, desc.y, desc.width, desc.height)
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

std::pair<float, float> cursor_pos() {
  @autoreleasepool {
    CGPoint p = [NSEvent mouseLocation];
    CGFloat min_x = CGFLOAT_MAX;
    CGFloat min_y = CGFLOAT_MAX;
    CGFloat max_x = -CGFLOAT_MAX;
    CGFloat max_y = -CGFLOAT_MAX;
    for (NSScreen *s in [NSScreen screens]) {
      NSRect frame = [s frame];
      min_x = std::min(min_x, frame.origin.x);
      min_y = std::min(min_y, frame.origin.y);
      max_x = std::max(max_x, frame.origin.x + frame.size.width);
      max_y = std::max(max_y, frame.origin.y + frame.size.height);
    }
    float w = max_x - min_x;
    float h = max_y - min_y;
    float x = w > 0.0f ? static_cast<float>((p.x - min_x) / w) : 0.0f;
    float y = h > 0.0f ? static_cast<float>((max_y - p.y) / h) : 0.0f;
    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    return {x, y};
  }
}

bool fullscreen_window_present() {
  @autoreleasepool {
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) {
      return false;
    }
    bool full = false;
    std::vector<NSScreen *> seen;
    CFIndex count = CFArrayGetCount(list);
    for (CFIndex i = 0; i < count && !full; ++i) {
      NSDictionary *info = (NSDictionary *)CFArrayGetValueAtIndex(list, i);
      NSNumber *layer = info[(id)kCGWindowLayer];
      if ([layer intValue] != 0) {
        continue;
      }
      CGRect bounds;
      CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)info[(id)kCGWindowBounds], &bounds);
      for (NSScreen *s in [NSScreen screens]) {
        if (std::find(seen.begin(), seen.end(), s) != seen.end()) {
          continue;
        }
        if (NSEqualRects([s frame], bounds)) {
          full = true;
          break;
        }
        if (NSIntersectsRect([s frame], bounds)) {
          seen.push_back(s);
          break;
        }
      }
    }
    CFRelease(list);
    return full;
  }
}

void make_context_current(Window &window) {
  @autoreleasepool {
    if (window.glContext) {
      [(NSOpenGLContext *)window.glContext makeCurrentContext];
    }
  }
}

void clear_current_context(Window &) {
  @autoreleasepool { [NSOpenGLContext clearCurrentContext]; }
}

void swap_buffers(Window &window) {
  @autoreleasepool {
    if (window.glContext) {
      [(NSOpenGLContext *)window.glContext flushBuffer];
    }
  }
}

} // namespace lizard::platform

#endif
