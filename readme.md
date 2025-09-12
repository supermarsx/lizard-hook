# Lizard Hook

[![CI](https://img.shields.io/github/actions/workflow/status/supermarsx/lizard-hook/ci.yml?branch=main)](https://github.com/supermarsx/lizard-hook/actions/workflows/ci.yml)
[![Coverage](https://img.shields.io/codecov/c/github/supermarsx/lizard-hook)](https://codecov.io/gh/supermarsx/lizard-hook)
[![Downloads](https://img.shields.io/github/downloads/supermarsx/lizard-hook/total)](https://github.com/supermarsx/lizard-hook/releases)
[![Stars](https://img.shields.io/github/stars/supermarsx/lizard-hook)](https://github.com/supermarsx/lizard-hook/stargazers)
[![Forks](https://img.shields.io/github/forks/supermarsx/lizard-hook)](https://github.com/supermarsx/lizard-hook/network/members)
[![Watchers](https://img.shields.io/github/watchers/supermarsx/lizard-hook)](https://github.com/supermarsx/lizard-hook/watchers)
[![Issues](https://img.shields.io/github/issues/supermarsx/lizard-hook)](https://github.com/supermarsx/lizard-hook/issues)
[![Commit Activity](https://img.shields.io/github/commit-activity/m/supermarsx/lizard-hook)](https://github.com/supermarsx/lizard-hook/graphs/commit-activity)
[![Made with C++](https://img.shields.io/badge/Made%20with-C++-blue)](#)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](license.md)

Lizard Hook is a lightweight cross-platform keyboard overlay that plays a short
FLAC sample and spawns fading emoji badges without stealing focus.

## Features

- Global keyboard hook with audio and visual feedback
- Cross-platform support for Windows, macOS, and Linux (X11)
- Embedded assets with optional external overrides
- Live configuration reload on file changes
- Low CPU usage and minimal resource footprint
- Customizable sound and emoji atlas

## Prerequisites

- [CMake](https://cmake.org/) **3.24** or newer
- [Ninja](https://ninja-build.org/)
- A working OpenGL 3.3 driver
- Platform toolchains:
  - **Windows:** [MinGW-w64](https://mingw-w64.org/) GCC 12+
  - **macOS:** Apple Clang 14+ (via Xcode command line tools)
  - **Linux:** GCC 12+ or Clang 15+

## Building

The project uses CMake presets for each platform. Configure and build with:

### Windows (MinGW-w64)

```sh
cmake --preset win-mingw
cmake --build build/win-mingw
```

### macOS

```sh
cmake --preset macos
cmake --build build/macos
```

### Linux

```sh
cmake --preset linux
cmake --build build/linux
```

## Usage

Run the built binary to start the keyboard overlay:

- **Windows:** `build\\win-mingw\\lizard-hook.exe`
- **macOS:** `./build/macos/lizard-hook`
- **Linux:** `./build/linux/lizard-hook`

Provide a custom configuration file with `--config path/to/lizard.json`. Without
this option, the paths described below are searched. The overlay reacts to
global key presses by playing a short FLAC sample and spawning fading emoji
badges without stealing focus.

## Assets

Default sound and emoji assets are stored in the `assets/` directory and are
embedded into the executable at build time. The build system converts
`assets/lizard-processed-clean-no-meta.flac` and `assets/lizard-regular.png`
into binary arrays that are linked into the final binary. The PNG is also used
as the macOS tray icon.

To override these defaults at runtime, set `sound_path` and `emoji_path` in the
`lizard.json` configuration file to point to external files. When these paths
are provided, external assets will be loaded instead of the embedded ones. For
`emoji_path`, the overlay looks for sprite coordinates in `<emoji_path>.json` or
an `emoji_atlas.json` file in the same directory. If the atlas is missing or
invalid, the embedded defaults are used and an error is logged.

## Configuration

All available configuration options are documented in `lizard.json.sample`.
Copy this file to `lizard.json` and edit as needed.

Common options include:

- `enabled` and `mute` to toggle overlay and audio
- `sound_cooldown_ms` and `max_concurrent_playbacks` to manage audio bursts
- `badges_per_second_max`, `badge_min_px`, `badge_max_px` to tune visuals
- `fullscreen_pause` to suspend in full-screen apps
- `exclude_processes` to ignore specific executables
- `sound_path` and `emoji_path` for external assets
- `logging_level` to control verbosity
- `logging_path` to set the log file location

Invalid `logging_level` values log a warning and fall back to `info`.

Configuration values are loaded from the first location that exists:

1. A path supplied via the `--config` command-line option.
2. The per-user config directory:
   - Windows: `%LOCALAPPDATA%/LizardHook/lizard.json`
   - macOS: `$HOME/Library/Application Support/LizardHook/lizard.json`
   - Linux: `$XDG_CONFIG_HOME/lizard_hook/lizard.json` or `~/.config/lizard_hook/lizard.json`
3. `lizard.json` located next to the executable.

The application watches the selected file and reloads it automatically when it changes.

## Limitations

- Global keyboard hooks are unavailable on Wayland. On Wayland systems the
  application can only operate in per-window mode.
- Requires a functional OpenGL driver; headless environments are unsupported.

For testing instructions and contribution guidelines, see
[development.md](development.md).

## License

This project is released under the [MIT License](license.md).
