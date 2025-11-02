gotcha — specs only, no code. here’s a tight C++ build spec your AI agent can execute.

# Project: Lizard Hook (Win32 C++)

## 1) Goal

On every keypress:

1. play a short FLAC “Lizard. Lizard. Lizard.” sample (debounced).
2. spawn a floating, click-through overlay element: a randomly chosen lizard emoji inside a soft circular badge that fades out and disappears.

Zero disruption to user input or focus. Lightweight, no installer required.

## 2) Target Platform

* OS: Windows 10 (2004+) and Windows 11 (all channels)
* Arch: x64 (primary), x86 (optional)
* Toolchain: MSVC 19.x (Visual Studio 2022) or clang-cl; C++20
* Subsystem: Windows (/SUBSYSTEM\:WINDOWS), single-EXE

## 3) Third-Party / System Dependencies

* **Keyboard hook**: Win32 API `SetWindowsHookExW(WH_KEYBOARD_LL)`
* **Audio (FLAC)**: default = **miniaudio** (WASAPI) + **dr\_flac** (header-only).
  Alternative (compile-time switch): **Media Foundation** (uses OS FLAC decoder on Win10+).
* **Rendering**: **Direct2D** (with DirectWrite for emoji text)
  Alternative: **Direct3D11 + Direct2D interop** if we need GPU composition (flag).
* **Emoji font**: prefer **Segoe UI Emoji**; fallback to default system emoji-capable font.

Most third-party libs are vendored in `third_party/` as source (no dynamic DLLs). `spdlog` is fetched via CMake at configure time. Static link where possible.

## 4) High-Level Architecture

* **App Core**: lifecycle, config, single-instance mutex, tray icon.
* **Hook Manager**: sets/unsets low-level keyboard hook in a dedicated thread.
* **Audio Engine**: preloads FLAC into memory, decodes to PCM at init, plays on key events with cooldown.
* **Overlay Manager**: manages one topmost, click-through layered window spanning the current desktop; spawns animated “badges” with per-frame alpha.
* **Animator**: timeline + easing for opacity/scale; 60 FPS target using a high-res timer.
* **Config Manager**: loads `lizard.json` (see §10), environment overrides, hotkeys.
* **Telemetry**: none. (Explicitly disabled.)
* **Logging**: rotating file in `%LOCALAPPDATA%\LizardHook\logs\`; verbose levels controlled by config.

## 5) UX / Behavior

* On any keypress (including repeats):

  * **Sound**: always trigger playback; bursts are limited by `max_concurrent_playbacks` via voice pooling.
  * **Badge**: spawn 1 circular badge at a random screen location (or near caret if available; see §11), limited by `badges_per_second_max` (default 12).
* **Badge visuals**:

  * Circle: soft background (blur-like look via radial alpha), white stroke at 30% opacity.
  * Emoji: randomly chosen from list (default includes 🦎; optional extra reptiles).
  * Animation: fade+scale from 0.9→1.0 (50 ms), hold (100 ms), fade to 0 (400–900 ms randomized), total lifetime \~700–1,200 ms.
  * Size: random diameter 60–108 px (DPI-aware).
* **Focus**: never steals focus; overlay is click-through.
* **Full screen guard**: auto-pause overlay + sound when a true full-screen window is active (toggleable).
* **Tray menu**: Enable/Disable, Mute, Pause in Fullscreen (on/off), Quit, Open Config, Open Logs.
* **Hotkeys**:

  * Ctrl+Shift+F9 → toggle Enable/Disable
  * Ctrl+Shift+F10 → toggle Mute
  * Ctrl+Shift+F11 → reload config

## 6) Keyboard Hooking Details

* Use `WH_KEYBOARD_LL` with `SetWindowsHookExW` in dedicated thread; message loop via `GetMessageW`.
* Process **both** keydown and keyup? Default: keydown only (`WM_KEYDOWN`/`WM_SYSKEYDOWN`).
* Exclusions:

  * Ignore synthetic events flagged by `LLKHF_INJECTED` (configurable).
  * Optional per-app exclusion list (process names).
* Fail-safe: if hook fails, show tray balloon and keep app running (overlay/audio disabled).

## 7) Overlay Rendering

* Create a **borderless, topmost, layered, transparent, click-through** window:

  * Styles: `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST`
  * Per-monitor DPI awareness v2.
* Composition:

  * **Direct2D** HWND render target (or DComp if using D3D11 path).
  * Double-buffered; drive frame pump with `QueryPerformanceCounter` + `Sleep(0)`/`timeBeginPeriod` minimal; aim for \~60 FPS without busy-wait.
* Badge objects:

  * Struct with `spawn_time`, `lifetime_ms`, `pos`, `diameter_px`, `emoji_codepoint`, random seed, state.
  * Easing: `cubicOut` for fade/scale; jitter ≤1 px to avoid banding (optional).
* Z-order: always above desktop, below full-screen exclusive if detected.

## 8) Audio Playback

* **Default**: miniaudio (WASAPI shared), decoded PCM cached in RAM.
* Low latency play calls; allow overlap (polyphony) up to `max_concurrent_playbacks` (default 16).
* Legacy `sound_cooldown_ms` is deprecated; voice pooling/LRU handles burst control.
* **Volume** 0–100% (default 65%).
* If device changes, auto-reinit.

## 9) Assets

* `assets/lizard.flac` (≤2 s). Embedded into the EXE as a binary resource; also overridable via config path.
* Optional additional samples array for randomization (weighted).

## 10) Configuration (`lizard.json`)

* Location precedence: CLI `--config`, `%LOCALAPPDATA%\LizardHook\lizard.json`, alongside EXE.
* Keys (examples):

  * `enabled` (bool, default true)
  * `mute` (bool, default false)
  * `sound_cooldown_ms` (deprecated; retained for backwards compatibility)
  * `max_concurrent_playbacks` (int, default 16)
  * `badges_per_second_max` (int, default 12)
  * `badge_min_px` / `badge_max_px` (int, default 60/108)
  * `emoji`: array of strings (default `["🦎"]`)
  * `emoji_weighted`: optional map `{ "🦎": 1.0, "🐉": 0.2 }` (mutually exclusive with `emoji`)
  * `fullscreen_pause` (bool, default true)
  * `exclude_processes`: array of exe names (case-insensitive)
  * `ignore_injected` (bool, default true)
  * `audio_backend` (`"miniaudio"` | `"mediafoundation"`)
  * `badge_spawn_strategy` (`"random_screen"` | `"near_caret"`)
  * `volume_percent` (0–100)
  * `dpi_scaling_mode` (`"per_monitor_v2"` | `"system"`)
  * `logging_level` (`"error"|"warn"|"info"|"debug"`)

## 11) Spawn Position Strategy

* Default `"random_screen"`: choose random monitor (weighted by area), pick a point with 24 px inset from edges.
* `"near_caret"`: attempt `GetGUIThreadInfo` + caret rect; if fails, fallback to random on same monitor as foreground window.

## 12) Performance & Limits

* CPU target: <1% on idle, <2% during bursts on a modern CPU.
* Memory: <50 MB working set (decoded audio included).
* Frame pacing: drop frames gracefully under load; coalesce updates.
* Back-pressure: if >100 active badges, stop spawning new until count <60.

## 13) Security & Stability

* No elevated privileges required.
* Signed binary (if cert available). If not, ensure clear provenance in file properties.
* Clean teardown: unhook, stop audio, destroy overlay before exit.
* Single-instance mutex; second launch focuses tray menu.

## 14) Build & Repo Layout

```
/ (repo root)
  /src
    app/        (WinMain, tray, config)
    hook/       (LL hook thread)
    audio/      (engine + backend adapters)
    overlay/    (window, renderer, badge system)
    util/       (timer, rng, logging)
  /assets
  /third_party  (miniaudio, dr_flac)
  /include
  /build        (out-of-source)
  CMakeLists.txt
  lizard.json.sample
  VERSION
  README.md
  LICENSE
```

* **Build system**: CMake ≥3.24; presets for Release/Debug; static runtime `/MT` for Release.
* **CI**: GitHub Actions (x64 Release artifact).

## 15) CLI Flags

* `--config <path>`
* `--disable` / `--enable`
* `--mute` / `--unmute`
* `--reset` (restore defaults in `%LOCALAPPDATA%`)
* `--verbose`

## 16) Tray UX Requirements

* Icon: 16/20/24/32 px ICO (lizard silhouette).
* Left click: toggle enable (visual check).
* Right click: full menu.
* Balloon tips on state changes (rate-limited).

## 17) Full-Screen Detection (Pause)

* Check foreground window rect equals monitor work area and has `WS_POPUP` without caption; if so, pause.
* Additionally, poll DWM `DwmGetWindowAttribute` for cloaked/exclusive hints (best effort).
* If paused, suppress badges and audio but keep hook active.

## 18) Testing & Acceptance Criteria

* **Functional**:

  * Any physical keydown triggers sound (subject to cooldown) and a badge.
  * Badge renders correctly on all monitors and DPI scales.
  * Overlay never captures mouse/keyboard; apps remain interactive.
  * Hotkeys work even when a game or full-screen app is focused (unless paused).
* **Performance**:

  * 10 CPS for 10 seconds: no missed frames > 50 ms median; no memory growth >10 MB.
* **Edge cases**:

  * Device change (default audio device switched) → playback still works.
  * Full-screen YouTube (browser) → paused if enabled.
  * RDP session change → no crash; overlay re-inits if needed.
* **Config**:

  * Live reload applies within 1 s after file write.
* **Uninstall behavior**:

  * App exits cleanly; leaves only config/log files.

## 19) Risks & Mitigations

* **Emoji glyph fallback**: some fonts may render B/W. Mitigate by DirectWrite font fallback to Segoe UI Emoji; optionally ship a color emoji fallback note in README.
* **Antivirus false positives** (global hook): mitigate by code signing + clear docs.
* **Audio spam**: cooldown + max concurrent + per-app excludes.

## 20) Nice-to-Have (post-MVP, behind flags)

* Particle sparkle around badge.
* Multiple FLAC variants, weighted random.
* Per-app profiles.
* OSC/WebSocket remote toggle.
* OBS “streamer mode” (badge only, no sound).

---

If you want, I can turn this into a machine-readable **build brief** for your agent (e.g., a `BUILD_SPEC.yaml` with tasks, targets, and acceptance checks).

---

# v1.1 — Updates per user requirements (MinGW, OpenGL, PNG emojis, cross‑platform)

## Toolchain & Targets

* **Toolchain (Windows):** MinGW‑w64 (GCC 12+). Provide `cmake/toolchains/mingw.cmake` and a `CMakePresets.json` entry.
* **Rendering:** **OpenGL 3.3 Core** (or GLES3 where needed) replaces Direct2D/D3D. All badge rendering uses GL.
* **Assets:** **Embedded PNG** emojis (packed into a texture atlas). No emoji fonts.
* **Cross‑platform intent:**

  * Windows: full feature set (global keyboard hook, tray, click‑through overlay).
  * macOS: event‑tap global hook (CGEventTap), NSStatusItem tray, non‑opaque overlay window with `ignoresMouseEvents=YES`.
  * Linux: X11 global key via XInput2 (preferred) or XRecord; tray via StatusNotifier/AppIndicator; click‑through ARGB window with input‑shape. **Wayland:** no reliable global key hook—documented limitation; app can run in per‑window mode only.

## Threads (hard requirement)

* **Main/UI**: owns OpenGL context + overlay + render loop.
* **Tray**: dedicated thread (Win32 message pump / macOS runloop / GTK/Qt loop) managing tray icon + menu.
* **Hook**: dedicated OS‑specific keyboard hook loop posting events to a lock‑free queue.
* **Sound**: dedicated mixer/playback thread (miniaudio callbacks).
* **Emoji/Anim Worker (optional)**: computes trajectories/easing to keep render thread light.

## System Tray Controls

* Menu: Enable/Disable, Mute, Pause in Fullscreen, **FPS: Auto/Fixed** (with submenu for 60/75/120/144/165/240), Open Config, Open Logs, Quit.
* Notifications: show state changes (rate‑limited). Windows uses `Shell_NotifyIcon`; macOS `NSStatusBar`; Linux SNI/AppIndicator.

## FPS Detection & Fallback

* Auto‑detect monitor refresh:

  * **Windows:** `EnumDisplaySettingsEx` (or DXGI if available).
  * **macOS:** `CGDisplayCopyDisplayMode`.
  * **Linux/X11:** XRandR.
* If detection fails: **fallback = 60 FPS**. Render loop uses delta‑time; frame pacing avoids busy‑wait.

## Audio Polyphony & Debounce Policy

* **Each keystroke = independent playback voice.**
* If a key is pressed before any debounce time, **play an additional voice simultaneously**; never cut previous audio.
* Limit via `max_concurrent_playbacks` (default **16**; configurable). LRU drop oldest voice when limit exceeded.
* Visual debounce/rate‑limit may still cap badges per second, independent of audio.

## Badges: Flying, Fades, Low Disturbance

* **Motion:** each badge follows a light “flight” path (Bezier or velocity+noise drift) upward/sideways; minimal screen occupation.
* **Fades:** explicit **fade‑in** (60–120 ms) and **fade‑out** (200–600 ms). Lifetime \~0.7–1.2 s (randomized).
* **Multiples:** many badges may coexist; back‑pressure limits (e.g., cap 150 active; resume at <80).
* **PNG Atlas:** select random lizard sprite; optional subtle rotation ±5° and scale jitter to avoid repetition.

## OpenGL Overlay & Click‑through

* Single OpenGL context; instanced draws for badges; radial alpha for circular badge BG.
* **Click‑through:**

  * Windows: layered window `WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST`.
  * macOS: non‑opaque NSWindow with `ignoresMouseEvents=YES`.
  * Linux X11: ARGB visual + input shape region; compositing WM required. Wayland: feature limited.

## RAII & Memory Clearance

* **Guards everywhere:** unique ownership for GL objects, hooks, audio devices; scope guards for cleanup; zero memory of PCM buffers after use where applicable.
* No raw `new/delete` in app code; use smart pointers and custom handle wrappers; deterministic teardown order.

## Config Additions (`lizard.json`)

* `emoji_pngs` (array of atlas sprite names) or `emoji_atlas` (path).
* `max_concurrent_playbacks` (int, default 16).
* `fps_mode` (`"auto"|"fixed"`), `fps_fixed` (default 60).
* Platform config roots: Windows `%LOCALAPPDATA%/LizardHook/`, Linux `$XDG_CONFIG_HOME/lizard_hook/`, macOS `~/Library/Application Support/LizardHook/`.

## Build Notes (Windows MinGW)

* Link: `opengl32`, `gdi32`, `user32`, `shell32`, `dwmapi`, `winmm`.
* Statically link runtime if possible; produce a single EXE with embedded assets.

## Keyboard Hooks — Documentation Summary

* **Windows:** `SetWindowsHookExW(WH_KEYBOARD_LL)` in its own thread.
* **macOS:** `CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, ...)` with CFRunLoop; requires Accessibility permission.
* **Linux/X11:** XInput2 raw events via XISelectEvents; fallback XRecord; requires X11 (not Wayland). On Wayland, global capture is not available without compositor‑specific protocols.

(These updates override earlier Direct2D/emoji‑font/MSVC‑specific mentions.)
