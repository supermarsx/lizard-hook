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

Format the source tree with:

```sh
cmake --build build --target format
```

Run static analysis and formatting checks with:

```sh
cmake --build build --target lint
```

## Assets

Default sound and emoji image assets are stored in the `assets/` directory and
are embedded into the executable at build time. The build system converts
`assets/lizard-processed-clean-no-meta.flac` and `assets/lizard-regular.png`
into binary arrays that are linked into the final binary.

To override these defaults at runtime, set `sound_path` and `emoji_path` in the
`lizard.json` configuration file to point to external files. When these paths
are provided, external assets will be loaded instead of the embedded ones.
