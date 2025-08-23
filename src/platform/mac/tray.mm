#include "../tray.hpp"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "embedded.h"
#include <dispatch/dispatch.h>
#include <thread>
#include <future>

namespace lizard::platform {

namespace {
TrayState g_state;
TrayCallbacks g_callbacks;
NSStatusItem *g_item = nil;
NSMenu *g_menu = nil;
NSMenuItem *g_enabled = nil;
NSMenuItem *g_mute = nil;
NSMenuItem *g_fullscreen = nil;
NSMenuItem *g_fps_auto = nil;
NSMenuItem *g_fps_fixed_60 = nil;
NSMenuItem *g_fps_fixed_75 = nil;
NSMenuItem *g_fps_fixed_120 = nil;
NSMenuItem *g_fps_fixed_144 = nil;
NSMenuItem *g_fps_fixed_165 = nil;
NSMenuItem *g_fps_fixed_240 = nil;
NSMenu *g_fps_menu = nil;
NSMenu *g_fps_fixed_menu = nil;
std::jthread g_thread;

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
- (void)fpsAuto:(id)sender {
  g_state.fps_mode = FpsMode::Auto;
  if (g_callbacks.set_fps_mode)
    g_callbacks.set_fps_mode(FpsMode::Auto);
  rebuild_menu();
}
- (void)fpsFixed:(id)sender {
  g_state.fps_mode = FpsMode::Fixed;
  g_state.fps_fixed = (int)[sender tag];
  if (g_callbacks.set_fps_mode)
    g_callbacks.set_fps_mode(FpsMode::Fixed);
  if (g_callbacks.set_fps_fixed)
    g_callbacks.set_fps_fixed(g_state.fps_fixed);
  rebuild_menu();
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
  [g_fps_auto
      setState:g_state.fps_mode == FpsMode::Auto ? NSControlStateValueOn : NSControlStateValueOff];
  [g_fps_fixed_60 setState:(g_state.fps_mode == FpsMode::Fixed && g_state.fps_fixed == 60)
                               ? NSControlStateValueOn
                               : NSControlStateValueOff];
  [g_fps_fixed_75 setState:(g_state.fps_mode == FpsMode::Fixed && g_state.fps_fixed == 75)
                               ? NSControlStateValueOn
                               : NSControlStateValueOff];
  [g_fps_fixed_120 setState:(g_state.fps_mode == FpsMode::Fixed && g_state.fps_fixed == 120)
                                ? NSControlStateValueOn
                                : NSControlStateValueOff];
  [g_fps_fixed_144 setState:(g_state.fps_mode == FpsMode::Fixed && g_state.fps_fixed == 144)
                                ? NSControlStateValueOn
                                : NSControlStateValueOff];
  [g_fps_fixed_165 setState:(g_state.fps_mode == FpsMode::Fixed && g_state.fps_fixed == 165)
                                ? NSControlStateValueOn
                                : NSControlStateValueOff];
  [g_fps_fixed_240 setState:(g_state.fps_mode == FpsMode::Fixed && g_state.fps_fixed == 240)
                                ? NSControlStateValueOn
                                : NSControlStateValueOff];
}

bool init_thread() {
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
    g_fps_menu = [[NSMenu alloc] initWithTitle:@"FPS"];
    g_fps_auto = [[NSMenuItem alloc] initWithTitle:@"Auto"
                                            action:@selector(fpsAuto:)
                                     keyEquivalent:@""];
    [g_fps_auto setTarget:g_target];
    g_fps_fixed_menu = [[NSMenu alloc] initWithTitle:@"Fixed"];
    auto make_fixed = ^(NSString *title, int value) {
      NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
                                                    action:@selector(fpsFixed:)
                                             keyEquivalent:@""];
      [item setTarget:g_target];
      [item setTag:value];
      [g_fps_fixed_menu addItem:item];
      return item;
    };
    g_fps_fixed_60 = make_fixed(@"60", 60);
    g_fps_fixed_75 = make_fixed(@"75", 75);
    g_fps_fixed_120 = make_fixed(@"120", 120);
    g_fps_fixed_144 = make_fixed(@"144", 144);
    g_fps_fixed_165 = make_fixed(@"165", 165);
    g_fps_fixed_240 = make_fixed(@"240", 240);
    NSMenuItem *fixed_item = [[NSMenuItem alloc] initWithTitle:@"Fixed"
                                                        action:nil
                                                 keyEquivalent:@""];
    [fixed_item setSubmenu:g_fps_fixed_menu];
    [g_fps_menu addItem:g_fps_auto];
    [g_fps_menu addItem:fixed_item];
    NSMenuItem *fps_root = [[NSMenuItem alloc] initWithTitle:@"FPS" action:nil keyEquivalent:@""];
    [fps_root setSubmenu:g_fps_menu];
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
    [g_menu addItem:fps_root];
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

void shutdown_thread() {
  @autoreleasepool {
    if (g_item) {
      [[NSStatusBar systemStatusBar] removeStatusItem:g_item];
      g_item = nil;
    }
    g_menu = nil;
    g_target = nil;
  }
}

void tray_thread(std::stop_token st, std::promise<bool> ready) {
  bool ok = init_thread();
  ready.set_value(ok);
  if (!ok)
    return;
  while (!st.stop_requested()) {
    @autoreleasepool {
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                               beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
  }
  shutdown_thread();
}

} // namespace

bool init_tray(const TrayState &state, const TrayCallbacks &callbacks) {
  g_state = state;
  g_callbacks = callbacks;
  std::promise<bool> ready;
  auto fut = ready.get_future();
  g_thread = std::jthread(tray_thread, std::move(ready));
  return fut.get();
}

void update_tray(const TrayState &state) {
  g_state = state;
  dispatch_async(dispatch_get_main_queue(), ^{
    rebuild_menu();
  });
}

void shutdown_tray() {
  if (g_thread.joinable())
    g_thread.request_stop();
  if (g_thread.joinable())
    g_thread.join();
}

} // namespace lizard::platform

#endif
