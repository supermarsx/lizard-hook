# Tray Icon Manual QA

## Purpose
Verify that the system tray icon loads correctly and errors are logged when tray initialization fails.

## Steps
1. Build and run **Lizard Hook** on Windows.
2. After startup, confirm a green lizard icon appears in the system tray.
3. Right-click the icon and ensure the context menu opens with expected entries.
4. Inspect `lizard.log` and confirm no errors were logged during initialization.
5. To test the fallback path, temporarily comment out the `IDI_LIZARD_TRAY` entry in `resources.rc` and rebuild. Run the app and confirm the default application icon appears and an error is logged indicating the custom icon failed to load.
6. To test error handling, use a tool like `Process Explorer` to prevent tray icons from registering and run the app. Confirm an error is logged for the failed `Shell_NotifyIconW` call and the application continues without a tray icon.
