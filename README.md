# lizard-hook
Lizard Meme Keyboard Hook

## Testing

Build and run the test suite:

```sh
cmake -S . -B build
cmake --build build
cmake --build build --target test
```

Third-party dependencies are cloned at configure time; an Internet connection is required for the first build.

Static analysis and formatting checks can be run with:

```sh
cmake --build build --target lint
```

## Configuration

A complete example configuration with comments lives in `lizard.json.sample`.
Copy this file to `lizard.json` and adjust values as needed. Remove the comments before use, as the parser expects valid JSON.

Configuration files are located using the following precedence (first existing wins):

1. Path supplied via `--config` on the command line
2. User config directory
   - Windows: `%LOCALAPPDATA%/LizardHook/lizard.json`
   - macOS: `~/Library/Application Support/LizardHook/lizard.json`
   - Linux: `$XDG_CONFIG_HOME/lizard_hook/lizard.json` or `~/.config/lizard_hook/lizard.json`
3. `lizard.json` next to the executable

## Assets

Default sound and emoji image assets are stored in the `assets/` directory and
are embedded into the executable at build time. The build system converts
`assets/lizard-processed-clean-no-meta.flac` and `assets/lizard-regular.png`
into binary arrays that are linked into the final binary.

To override these defaults at runtime, set `sound_path` and `emoji_path` in the
`lizard.json` configuration file to point to external files. When these paths
are provided, external assets will be loaded instead of the embedded ones.
