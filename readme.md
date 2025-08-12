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
