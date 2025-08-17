# lizard-hook
Lizard Meme Keyboard Hook

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

## Testing

Build and run the test suite:

```sh
cmake -S . -B build
cmake --build build
cmake --build build --target test
```

Third-party dependencies are cloned at configure time; an Internet connection is required for the first build.

Format the source tree with:

```sh
cmake --build build --target format
```

Run static analysis and formatting checks with:

```sh
cmake --build build --target lint
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

Default sound and emoji image assets are stored in the `assets/` directory and
are embedded into the executable at build time. The build system converts
`assets/lizard-processed-clean-no-meta.flac` and `assets/lizard-regular.png`
into binary arrays that are linked into the final binary.

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

## Contributing

Contributions are welcome! Please:

1. Fork the repository and create a topic branch.
2. Ensure the project builds and tests pass:

   ```sh
   cmake -S . -B build
   cmake --build build
   cmake --build build --target test
   cmake --build build --target lint
   ```

3. Use `cmake --build build --target format` to apply formatting.
4. Open a pull request describing your changes.

## License

This project is released under the [MIT License](license.md).
