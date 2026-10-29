# Windows desktop installer (alpha)

The Windows setup artifact is built from the same tested C++ runtime package as the portable ZIP. It contains a self-contained .NET Windows Forms host that embeds the existing dashboard using Microsoft's WebView2 control, plus the runtime, web assets, per-user launcher/update helpers, configuration example, and license.

## User experience

- The installer is per-user (`%LOCALAPPDATA%\Programs\Local AI Runtime`) and does not request administrator elevation.
- The Start menu shortcut launches `LocalAIRuntime.Desktop.exe`. An optional desktop shortcut can be selected during setup.
- The host invokes `scripts\Start-LocalAIRuntime.ps1` in a hidden, noninteractive PowerShell process. The launcher handles existing-instance checks and interrupted portable-update recovery, and refuses to stop another app that owns the configured port.
- The runtime is started without a console window and the local dashboard opens in a native desktop window.
- Closing the window minimizes to the notification area. The tray menu can reopen the window, ask the dashboard to check for updates, or **Exit and stop local engines** using the loopback graceful-shutdown API.
- The WebView2 browser profile and all runtime settings/engines/models/logs live below `%LOCALAPPDATA%\LocalAIRuntime`, outside the program directory.
- On first launch, the launcher persists an opaque `runtime-instance.id` in that user-data directory and passes the canonical data root into the runtime process. `/v1/system/identity` lets the shell confirm that the HTTP listener serves the same persistent data directory; a healthy old runtime that cannot prove its identity is not silently reused.
- Uninstall removes the program directory and shortcuts but deliberately preserves user data. A separate explicit data-removal choice is still planned.

## Updates and trust boundary

The portable Windows updater is disabled when `web\desktop-shell.json` identifies an installed desktop build. Otherwise, applying the portable ZIP could replace the runtime while leaving the desktop host on a different build. Desktop builds that contain `scripts\Apply-LocalAIRuntimeDesktopUpdate.ps1` expose a native **Download & install desktop update** action in the dashboard and the tray menu. The shell reads official GitHub Releases metadata, accepts only the exact setup asset from the expected repository/tag URL, checks the advertised size and SHA-256 digest, and stages the installer under `%LOCALAPPDATA%\LocalAIRuntime\updates`.

After explicit user confirmation, a detached Windows PowerShell 5.1 process waits for the desktop shell to exit, gracefully stops the runtime, creates a complete hash-verified backup of the application directory, and runs the official per-user Inno Setup package in the existing install location. It verifies the new runtime version and desktop payload, restarts the shell and requires `/health` from the expected installed executable plus `/v1/system/identity` matching the persistent user-data identity. If setup or health verification fails, it restores the full previous installation and checks that the previous shell/runtime starts. Durable status records allow a later launch to recover after a process or machine interruption; recovery waits for any still-running staged setup process, transfers helper ownership before restarting the shell, and includes terminal records in newest-status selection so an old failed attempt cannot roll back a newer successful update. Backups remain in the per-user updates folder for diagnostics. User settings, engine binaries, model weights, logs and the WebView2 profile remain outside the program folder and are not modified by setup.

This update path provides checksum-based integrity and rollback, not publisher authentication. The installer and desktop host are not code-signed in this alpha; Windows may show an unknown-publisher/SmartScreen warning. The setup installs Microsoft's WebView2 Evergreen Runtime using the signed Microsoft bootstrapper; first-time setup may need internet access. SHA-256 fetched from GitHub release metadata detects corruption/mismatch but does not authenticate this project's publisher.

## CI validation

The canonical Windows job builds the host as a self-contained `win-x64` app, downloads and verifies the Microsoft-signed WebView2 bootstrapper, compiles the Inno Setup script, silently installs into an isolated path, runs the installed host's payload verifier, checks the expected runtime/web/update files, and executes a desktop-update smoke against that installed package. The desktop smoke injects an installer failure after replacement and a forced helper interruption, verifies full-directory rollback/recovery and user-data preservation, then confirms a successful setup/payload update before uninstall. The existing portable ZIP smoke and update/rollback tests remain separate. A successful CI job does not replace clean-machine and real-model manual acceptance on Windows 10/11.
