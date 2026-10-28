# Changelog

This project follows a lightweight Keep a Changelog structure. GitHub Release notes are generated from merged commits when a version tag is published; update this file with user-visible changes during each development cycle.

## Unreleased

### Added
- Startup update check in the local dashboard against published stable GitHub Releases; updates are not silently installed.
- Public project site with live release downloads and a version-controlled feature manifest.
- Tag-driven Windows x64 and Linux x64 release packaging with SHA-256 checksums.
- macOS compile/test CI in addition to Windows and Linux CI.
- A Windows Winsock header-order fix for MSVC/Boost.Asio compilation.

### Security
- Keep the runtime API bound to loopback by default.
- Release binaries are not self-replaced automatically in this alpha; manual review is required before installing a package.
