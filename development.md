# Development

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

