# Windows desktop installer (alpha)

The Windows setup artifact is built from the same tested C++ runtime package as the portable ZIP. It contains a self-contained .NET Windows Forms host that embeds the existing dashboard using Microsoft's WebView2 control, plus the runtime, web assets, per-user launcher/update helpers, configuration example, and license.

## User experience

- The installer is per-user (`%LOCALAPPDATA%\Programs\Local AI Runtime`) and does not request administrator elevation.
- The Start menu shortcut launches `LocalAIRuntime.Desktop.exe`. An optional desktop shortcut can be selected during setup.
- The host invokes `scripts\Start-LocalAIRuntime.ps1` in a hidden, noninteractive PowerShell process. The launcher handles existing-instance checks and interrupted portable-update recovery, and refuses to stop another app that owns the configured port.
- The runtime is started without a console window and the local dashboard opens in a native desktop window.
- Closing the window minimizes to the notification area. The tray menu can reopen the window, ask the dashboard to check for updates, or **Exit and stop local engines** using the loopback graceful-shutdown API.
- The WebView2 browser profile and all runtime settings/engines/models/logs live below `%LOCALAPPDATA%\LocalAIRuntime`, outside the program directory.
- Uninstall removes the program directory and shortcuts but deliberately preserves user data. A separate explicit data-removal choice is still planned.

## Updates and trust boundary

The portable Windows updater is disabled when `web\desktop-shell.json` identifies an installed desktop build. Otherwise, applying the portable ZIP could replace the runtime while leaving the desktop host on a different build. The current alpha dashboard directs desktop users to the official release page and full setup installer, asks them to exit the app from its tray menu before running setup, and recommends verifying `SHA256SUMS.txt`.

This is not yet an atomic in-app desktop updater. The installer and desktop host are not code-signed in this alpha; Windows may show an unknown-publisher warning. The setup installs Microsoft's WebView2 Evergreen Runtime using the signed Microsoft bootstrapper; first-time setup may need internet access. SHA-256 provides integrity checking but does not authenticate this project's publisher.

## CI validation

The canonical Windows job builds the host as a self-contained `win-x64` app, downloads and verifies the Microsoft-signed WebView2 bootstrapper, compiles the Inno Setup script, silently installs into an isolated path, runs the installed host's payload verifier, checks the expected runtime/web/update files, then uninstalls and confirms the main executable is removed. The existing portable ZIP smoke and update/rollback tests remain separate. A successful CI job does not replace clean-machine and real-model manual acceptance on Windows 10/11.
