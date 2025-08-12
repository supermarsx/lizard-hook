# lizard-hook
Lizard Meme Keyboard Hook

## Prerequisites

Linux builds require the GTK3 development files:

```sh
sudo apt-get install libgtk-3-dev
```

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
