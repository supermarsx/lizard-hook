# AGENT SPEC — Lizard Hook

> Ground truth: use v1.1 updates. OpenGL + PNG atlas + MinGW‑w64 default. Cross‑platform targets (Windows/macOS/Linux). No emoji fonts; no D3D/D2D.

---

## Objectives

- Build, test, and package **Lizard Hook**: a lightweight keyboard‑event reactive overlay that plays a short FLAC sample and spawns fading emoji badges without stealing focus.

- Produce a **single portable binary** per platform (no installer). Assets embedded by default; configurable overrides allowed.

- Enforce performance (<2% CPU under typical bursts) and stability constraints. No telemetry.

## Non‑Objectives

- No network I/O, cloud dependencies, or online updates.

- No elevated privileges. No kernel drivers. No Wayland global keyboard capture (document limitation).

---

## Platforms & Constraints

- **Windows 10 (2004+) / Windows 11**, x64 (primary), x86 optional.

- **macOS 12+** (Intel + Apple Silicon via universal build if possible).

- **Linux**: X11 compositing WM required; **Wayland:** global key hook unsupported → run per‑window mode only.

- **Graphics:** OpenGL 3.3 Core (or GLES3 where needed). Single context. Instanced draws for badges.

- **Audio:** miniaudio (WASAPI/CoreAudio/ALSA) + dr_flac; polyphony capped by `max_concurrent_playbacks`.

- **Input hooks:** Windows `WH_KEYBOARD_LL`; macOS CGEventTap (Accessibility permission); Linux XInput2 raw (fallback XRecord).

---

## Repository Layout (enforced)

```
/ (repo root)
  /src
    app/            # lifecycle, config, tray, CLI
    hook/           # OS-specific keyboard hooks
    audio/          # miniaudio integration, mixer, dr_flac decode
    overlay/        # GL context, renderer, badge system, atlas
    util/           # time, RNG, logging, platform utils, fs
    platform/       # win/, mac/, linux/ shims (tray, window, etc.)
  /assets           # lizard.flac, emoji atlas PNG + atlas.json
  /cmake            # toolchains/, modules/
  /third_party      # header-only deps (see below)
  /include          # public headers (if any)
  /build            # out-of-source builds
  CMakeLists.txt
  CMakePresets.json
  lizard.json.sample
  VERSION
  README.md
  LICENSE
```

---

## Toolchain & System Requirements

### Common

- **C++20**, CMake **≥3.24**, Ninja latest.

- **Static linking** where feasible; no runtime DLLs for third‑party.

- **OpenGL loader**: `glad` (core 3.3) generated and vendored.

- **Image decode**: `stb_image` for PNG; pack sprites into an atlas (tooling below).

- **JSON**: `nlohmann/json` (header‑only).

- **Logging**: `spdlog` (header‑only) with rotating sink; levels via config.

- **CLI**: `cxxopts` (header‑only).

- **Unit test**: `Catch2` v3 (header‑only).

- **Lint/format**: `clang-tidy`, `cppcheck` (optional), `clang-format`.

### Windows (default)

- **Compiler**: MinGW‑w64 **GCC 12+**.

- **Link libs**: `opengl32`, `gdi32`, `user32`, `shell32`, `dwmapi`, `winmm`.

- **Executable**: `/SUBSYSTEM:WINDOWS` equivalent for MinGW, topmost layered window.

- **Optional alt path** (flag `-DMSVC_PATH=ON`): MSVC 19.x + vcpkg/none; not default.

### macOS

- **Compiler**: Apple Clang 14+.

- **Frameworks**: `AppKit`, `CoreGraphics`, `CoreText`, `CoreVideo`, `CoreAudio`, `AudioToolbox`, `IOKit`.

- **Codesign**: ad‑hoc by default; hardened runtime optional. Requires Accessibility permission for CGEventTap.

### Linux (X11)

- **Compiler**: GCC 12+ or Clang 15+.

- **Packages** (Debian/Ubuntu names): `xorg-dev`, `libxi-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, `libx11-dev`, `libxext-dev`, `libasound2-dev`.

- **Tray**: SNI/AppIndicator; optional fallback to legacy system tray.

---

## Third‑Party (vendored in `third_party/`)

- `miniaudio` (audio backend abstraction)

- `dr_flac` (FLAC decode)

- `glad` (OpenGL loader, generated for 3.3 core)

- `stb_image` (PNG)

- `nlohmann/json` (config)

- `spdlog` (logging)

- `cxxopts` (CLI)

- `readerwriterqueue` (optional lock‑free queue for hook→audio/overlay events)

> All third‑party as source; compile into the binary. No dynamic DLLs.

---

## Configuration (runtime)

File: `lizard.json` (search order: CLI `--config`, platform config dir, alongside EXE).

Keys (subset):

- `enabled` (bool, default true)

- `mute` (bool, default false)

- `sound_cooldown_ms` (int, default 150)

- `max_concurrent_playbacks` (int, default 16)

- `badges_per_second_max` (int, default 12)

- `badge_min_px` / `badge_max_px` (int, default 60/108)

- `emoji_pngs` (array of sprite names) or `emoji_atlas` (path)

- `fullscreen_pause` (bool, default true)

- `exclude_processes` (array of exe/process names)

- `ignore_injected` (bool, default true)

- `badge_spawn_strategy` (`"random_screen"|"near_caret"`)

- `volume_percent` (0–100, default 65)

- `fps_mode` (`"auto"|"fixed"`), `fps_fixed` (int, default 60)

- `dpi_scaling_mode` (`"per_monitor_v2"|"system"`)

- `logging_level` (`"error"|"warn"|"info"|"debug"`)

---

## Runtime Behavior (must‑haves)

- **Keyboard**: physical keydown triggers → enqueue sound (polyphonic) + badge spawn (rate‑limited).

- **Audio**: debounce via `sound_cooldown_ms`; never cut playing voices; LRU drop when > `max_concurrent_playbacks`.

- **Overlay**: click‑through, topmost, no focus change; GL render loop targets display refresh (auto detect; fallback 60 FPS). Back‑pressure if active badges >150; resume at <80.

- **Full‑screen guard**: pause in true full‑screen; hotkey toggles.

- **Tray**: Enable/Disable, Mute/Unmute, Pause in Fullscreen, FPS mode, Open Config, Open Logs, Quit.

- **Hotkeys** (global): Ctrl+Shift+F9 enable/disable; Ctrl+Shift+F10 mute; Ctrl+Shift+F11 reload config.

- **Shutdown**: unhook, stop audio, destroy GL window/context; single‑instance mutex.

---

## Security & Privacy

- No telemetry or network calls. No PII collection. Log file redaction not required (no sensitive data captured).

- No elevation. Respect macOS Accessibility consent prompts. Document Linux Wayland limitation.

- Signed binaries if cert available; otherwise clear publisher in file properties.

---

## Build Orchestration (agent‑executable)

### Presets

`CMakePresets.json` (authoritative):

```json
{
  "version": 4,
  "configurePresets": [
    {"name":"win-mingw-release","generator":"Ninja","binaryDir":"build/win-rel","cacheVariables":{"CMAKE_BUILD_TYPE":"Release","CMAKE_TOOLCHAIN_FILE":"cmake/toolchains/mingw.cmake"}},
    {"name":"linux-release","generator":"Ninja","binaryDir":"build/linux-rel","cacheVariables":{"CMAKE_BUILD_TYPE":"Release"}},
    {"name":"macos-release","generator":"Ninja","binaryDir":"build/macos-rel","cacheVariables":{"CMAKE_BUILD_TYPE":"Release"}},
    {"name":"*-debug","inherits":"*-release","cacheVariables":{"CMAKE_BUILD_TYPE":"Debug"}}
  ],
  "buildPresets": [
    {"name":"win-release","configurePreset":"win-mingw-release"},
    {"name":"linux-release","configurePreset":"linux-release"},
    {"name":"macos-release","configurePreset":"macos-release"}
  ]
}
```

### Commands (canonical)

```
# Windows (PowerShell)
cmake --preset win-mingw-release
cmake --build --preset win-release -j

# Linux
cmake --preset linux-release
cmake --build --preset linux-release -j

# macOS
cmake --preset macos-release
cmake --build --preset macos-release -j
```

### Packaging

- Embed `assets/lizard.flac` and emoji atlas PNG into binary resources by default.

- Also emit a portable zip per platform containing: `LizardHook[.exe]`, `lizard.json.sample`, `README.md`, `LICENSE`, `VERSION`.

- Windows: add `.ico` multi‑size icon.

---

## Continuous Integration (GitHub Actions)

Matrix jobs: `windows-latest` (mingw), `ubuntu-latest`, `macos-latest`.

- Cache: CMake/Ninja build cache keyed by hash of `CMakeLists.txt` + `CMakePresets.json` + `/third_party`.

- Steps: checkout → configure (preset) → build → unit tests → package artifacts → (optional) code sign.

- Upload artifacts: zipped portable bundles; include logs on failure.

---

## Quality Gates & Testing

### Static / Style

- `clang-format` check (CI fails on diff).

- `clang-tidy` (core checks: modernize, readability, bugprone, performance, portability-misuse).

- `cppcheck` (informational).

### Unit Tests (Catch2)

- **Config**: parsing/merging, precedence, defaults.

- **Audio**: voice allocator (LRU drop), cooldown semantics (concurrency).

- **Overlay math**: easing/timelines, lifetime, back‑pressure thresholds.

- **Random**: seeded RNG reproducibility for badge params.

- **Hook filter**: injected event ignore logic; per‑app exclude list.

### Integration Tests (headless where possible)

- Simulate burst input (10 CPS × 10 s) → verify no missed frame >50 ms median; memory <+10 MB.

- Device change simulation (default audio device swap) → audio recovers.

- Full‑screen detection toggles pause (Windows: YouTube in browser; macOS: full‑screen window; Linux: X11 full‑screen hints).

### Manual QA (checklist)

- Badges render DPI‑correct on multi‑monitor.

- Overlay never steals focus; click‑through verified.

- Global hotkeys work; do not conflict with OS defaults.

- Tray menu items function and rate‑limited notifications show.

- RDP/session change does not crash; overlay re‑initializes.

### Performance Budgets

- CPU: <1% idle, <2% typical bursts (modern CPU).

- Memory WS: <50 MB including decoded audio.

---

## Runtime Flags / CLI

```
--config <path>
--enable | --disable
--mute | --unmute
--reset               # restore defaults in platform config dir
--verbose             # elevate logging level
```

---

## Graphics & Animation Rules

- **Badge**: circular soft BG (radial alpha), optional subtle rotation ±5°, scale jitter; size 60–108 px (DPI‑aware).

- **Animation**: fade‑in 60–120 ms; hold ~100 ms; fade‑out 200–600 ms; total 0.7–1.2 s randomized.

- **Motion**: light drift (Bezier / velocity+noise). Avoid busy GPU work; batch via instancing.

- **Frame pacing**: delta‑time update; auto detect refresh (Win: EnumDisplaySettingsEx / DXGI alt, macOS: CGDisplay, Linux: XRandR). Fallback 60 FPS.

---

## Input Hooks

- **Windows**: `SetWindowsHookExW(WH_KEYBOARD_LL)` on dedicated thread; message pump via `GetMessageW`. Ignore `LLKHF_INJECTED` if configured. Fail‑safe: gracefully disable features and notify via tray.

- **macOS**: `CGEventTapCreate` with kCGHIDEventTap; runloop thread; requires Accessibility permission.

- **Linux/X11**: XInput2 raw events (preferred) or XRecord fallback. Wayland unsupported for global capture.

---

## Audio Policy

- Decode FLAC to PCM at init; keep in RAM.

- Each keystroke spawns a voice; never cut existing voices. Enforce `max_concurrent_playbacks` (default 16) with LRU drop.

- Device hot‑swap: reinit on default device change; brief glitch acceptable (<200 ms).

---

## Error Handling & Logging

- Structured logs with timestamps + levels; rotating files in platform app‑data dir.

- On critical init failure (GL/audio/hook), keep process alive; present tray notification and degrade features.

- All subsystems use RAII wrappers; deterministic teardown sequence.

---

## Assets & Atlas

- `assets/emoji_atlas.png` + `assets/emoji_atlas.json` (sprite rects, names). Build step validates atlas indices.

- `assets/lizard.flac` length ≤2 s.

- Embed as binary resources by default; optional external override path in config.

---

## Coding Standards

- C++20, no raw `new/delete`; prefer `unique_ptr`, `span`, `std::chrono`, `std::thread`/`jthread`.

- Avoid exceptions across module boundaries; use `tl::expected`/status codes internally where pragmatic.

- Namespaces per subsystem; `#pragma once` guards; PIMPL where ABI stability matters.

- Zero‑allocation per frame in render loop (amortize, pre‑reserve).

- Threading: lock‑free queue for event fan‑out; avoid priority inversion; use atomics for flags.

- DPI awareness (per‑monitor v2 on Windows); convert logical↔physical px correctly.

- Strict warnings: `-Wall -Wextra -Wpedantic` (+ MSVC analogues when used). Treat warnings as errors in CI.

---

## Packaging & Release

- Semver in `VERSION`. Changelog in GitHub Releases.

- Artifacts: `LizardHook-win-x64.zip`, `LizardHook-macos.zip`, `LizardHook-linux-x64.tar.gz`.

- Optional code signing: Windows Authenticode; macOS codesign + notarization (if account available).

---

## Risks & Mitigations

- **Emoji rendering artifacts** → pre‑multiplied alpha in atlas; gamma‑correct blending.

- **Antivirus false positives (global hook)** → code signing + public documentation.

- **Wayland lack of global hooks** → document limitation; per‑window mode.

- **Audio spam** → cooldown + polyphony cap + per‑app excludes.

---

## Verification (acceptance)

Functional

- Physical keydown → 1 sound (subject to cooldown) + 1 badge.

- Overlay click‑through; no input interception; no focus changes.

- Hotkeys operate globally unless paused by full‑screen guard.

- Badge renders on all monitors; DPI correct.

Performance

- 10 CPS for 10 s → no frame time median >50 ms; memory growth <10 MB.

Resilience

- Default device change → audio recovers.

- Full‑screen video → paused if `fullscreen_pause` enabled.

- RDP/session change → process stable; overlay re‑inits.

Config

- Live reload within 1 s of file write.

Uninstall

- App exits cleanly; only leaves config/log files.

---

## Agent Task Graph (canonical)

1. **sync**: checkout repository @ main.

2. **deps.vendor**: ensure `third_party/` contains required headers; verify checksums.

3. **env.provision**: install toolchain (platform‑specific), ensure CMake ≥3.24, Ninja.

4. **build.configure**: run preset for current platform.

5. **build.compile**: build Release with `-j` cores.

6. **test.unit**: run Catch2 tests; fail on non‑zero.

7. **lint.static**: run `clang-format --dry-run`, `clang-tidy` on `src/**`.

8. **test.integration**: run headless integration suite (simulated input).

9. **package.bundle**: embed assets, produce portable archive, attach VERSION.

10. **artifact.verify**: check binary size budget, dependency ldd/otool/dumpbin (no unwanted DLLs/so/dylibs).

11. **publish**: upload artifacts; generate release notes from commit range.

---

## Developer Utilities (optional)

- **Atlas packer**: script to pack individual PNGs → `emoji_atlas.png` + JSON; validates frame rects.

- **Input generator**: headless tool to synthesize key events for tests (platform shims; macOS requires sandbox exceptions or local runner).

- **Perf HUD** (debug only): show FPS, badge count; disabled in Release.

---

## Maintenance

- Keep third‑party headers pinned; update quarterly with CI canary.

- Run security static analysis (CodeQL) weekly.

- Rotate logs; cap disk usage to 20 MB.