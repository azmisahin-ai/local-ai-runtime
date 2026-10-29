#!/bin/bash
# Double-clickable macOS launcher. The terminal remains open so the user can
# see startup errors and stop the foreground runtime with Ctrl+C.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
(sleep 3; open "http://127.0.0.1:8000" >/dev/null 2>&1 || true) &
exec "$ROOT/scripts/start-runtime.sh"
