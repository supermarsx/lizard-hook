#include "../tray.hpp"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "embedded.h"

namespace lizard::platform {

namespace {
TrayState g_state;
TrayCallbacks g_callbacks;
NSStatusItem *g_item = nil;
NSMenu *g_menu = nil;
NSMenuItem *g_enabled = nil;
NSMenuItem *g_mute = nil;
NSMenuItem *g_fullscreen = nil;
NSMenuItem *g_fps = nil;

@interface TrayTarget : NSObject
@end

@implementation TrayTarget
- (void)toggleEnabled:(id)sender {
  g_state.enabled = !g_state.enabled;
  if (g_callbacks.toggle_enabled)
    g_callbacks.toggle_enabled(g_state.enabled);
  [g_enabled setState:g_state.enabled ? NSControlStateValueOn : NSControlStateValueOff];
}
- (void)toggleMute:(id)sender {
  g_state.muted = !g_state.muted;
  if (g_callbacks.toggle_mute)
    g_callbacks.toggle_mute(g_state.muted);
  [g_mute setState:g_state.muted ? NSControlStateValueOn : NSControlStateValueOff];
}
- (void)toggleFullscreen:(id)sender {
  g_state.fullscreen_pause = !g_state.fullscreen_pause;
  if (g_callbacks.toggle_fullscreen_pause)
    g_callbacks.toggle_fullscreen_pause(g_state.fullscreen_pause);
  [g_fullscreen setState:g_state.fullscreen_pause ? NSControlStateValueOn : NSControlStateValueOff];
}
- (void)toggleFPS:(id)sender {
  g_state.show_fps = !g_state.show_fps;
  if (g_callbacks.toggle_fps)
    g_callbacks.toggle_fps(g_state.show_fps);
  [g_fps setState:g_state.show_fps ? NSControlStateValueOn : NSControlStateValueOff];
}
- (void)openConfig:(id)sender {
  if (g_callbacks.open_config)
    g_callbacks.open_config();
}
- (void)openLogs:(id)sender {
  if (g_callbacks.open_logs)
    g_callbacks.open_logs();
}
- (void)quit:(id)sender {
  if (g_callbacks.quit)
    g_callbacks.quit();
}
@end

TrayTarget *g_target = nil;

void rebuild_menu() {
  [g_enabled setState:g_state.enabled ? NSControlStateValueOn : NSControlStateValueOff];
  [g_mute setState:g_state.muted ? NSControlStateValueOn : NSControlStateValueOff];
  [g_fullscreen setState:g_state.fullscreen_pause ? NSControlStateValueOn : NSControlStateValueOff];
  [g_fps setState:g_state.show_fps ? NSControlStateValueOn : NSControlStateValueOff];
}

} // namespace

bool init_tray(const TrayState &state, const TrayCallbacks &callbacks) {
  g_state = state;
  g_callbacks = callbacks;
  @autoreleasepool {
    g_target = [TrayTarget new];
    g_item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    NSData *iconData = [NSData dataWithBytes:lizard::assets::lizard_regular_png
                                      length:lizard::assets::lizard_regular_png_len];
    NSImage *icon = [[NSImage alloc] initWithData:iconData];
    [g_item.button setImage:icon];
    g_menu = [[NSMenu alloc] initWithTitle:@""];
    g_enabled = [[NSMenuItem alloc] initWithTitle:@"Enabled"
                                           action:@selector(toggleEnabled:)
                                    keyEquivalent:@""];
    [g_enabled setTarget:g_target];
    g_mute = [[NSMenuItem alloc] initWithTitle:@"Mute"
                                        action:@selector(toggleMute:)
                                 keyEquivalent:@""];
    [g_mute setTarget:g_target];
    g_fullscreen = [[NSMenuItem alloc] initWithTitle:@"Pause in Fullscreen"
                                              action:@selector(toggleFullscreen:)
                                       keyEquivalent:@""];
    [g_fullscreen setTarget:g_target];
    g_fps = [[NSMenuItem alloc] initWithTitle:@"Show FPS"
                                       action:@selector(toggleFPS:)
                                keyEquivalent:@""];
    [g_fps setTarget:g_target];
    NSMenuItem *cfg = [[NSMenuItem alloc] initWithTitle:@"Open Config"
                                                 action:@selector(openConfig:)
                                          keyEquivalent:@""];
    [cfg setTarget:g_target];
    NSMenuItem *logs = [[NSMenuItem alloc] initWithTitle:@"Open Logs"
                                                  action:@selector(openLogs:)
                                           keyEquivalent:@""];
    [logs setTarget:g_target];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                  action:@selector(quit:)
                                           keyEquivalent:@""];
    [quit setTarget:g_target];
    [g_menu addItem:g_enabled];
    [g_menu addItem:g_mute];
    [g_menu addItem:g_fullscreen];
    [g_menu addItem:g_fps];
    [g_menu addItem:[NSMenuItem separatorItem]];
    [g_menu addItem:cfg];
    [g_menu addItem:logs];
    [g_menu addItem:[NSMenuItem separatorItem]];
    [g_menu addItem:quit];
    [g_item setMenu:g_menu];
    rebuild_menu();
  }
  return true;
}

void update_tray(const TrayState &state) {
  g_state = state;
  @autoreleasepool {
    rebuild_menu();
  }
}

void shutdown_tray() {
  @autoreleasepool {
    if (g_item) {
      [[NSStatusBar systemStatusBar] removeStatusItem:g_item];
      g_item = nil;
    }
    g_menu = nil;
    g_target = nil;
  }
}

} // namespace lizard::platform

#endif
