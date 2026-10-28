# Security Policy

## Scope

This is an early alpha. The local HTTP API can start and stop configured engine executables and forwards chat requests to a configured loopback upstream.

## Supported reporting

Please report security issues privately to the repository owner using GitHub's private vulnerability reporting feature if it is enabled. Do not include API keys, model files, personal data, or full sensitive logs in public issues.

## Current limitations

- The server binds to loopback only and validates Host and Origin headers.
- There is no authentication for local clients. Local processes can reach the API.
- Do not run the service for mutually untrusted local users.
- Do not expose the port through a reverse proxy, tunnel, LAN bind, or port-forwarding rule.
- Engine executable paths and configuration files are trusted local admin inputs.
- The app does not download or execute installers automatically.

Security fixes may require breaking configuration changes while the project is pre-1.0.


## Updates and release artifacts

The local dashboard may query the public GitHub Releases API for version metadata when the user opens it. It does not upload local data and does not silently download, execute, or replace binaries. If the release feed is unavailable, local runtime use should continue.

Release assets include a SHA-256 manifest to detect accidental corruption. A checksum published beside an artifact is not a digital signature and cannot independently establish publisher identity. Until signed provenance/signatures and rollback are implemented, users should review the release page and install updates explicitly. Never expose the unauthenticated loopback API to a network interface.
