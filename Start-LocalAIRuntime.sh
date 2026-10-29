#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
URL="http://127.0.0.1:8000"

# Open the dashboard for interactive desktop sessions, but keep the runtime
# usable on headless machines and servers where no browser helper exists.
case "$(uname -s)" in
  Darwin)
    (sleep 3; open "$URL" >/dev/null 2>&1 || true) &
    ;;
  Linux)
    if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] && command -v xdg-open >/dev/null 2>&1; then
      (sleep 3; xdg-open "$URL" >/dev/null 2>&1 || true) &
    fi
    ;;
esac
exec "$ROOT/scripts/start-runtime.sh"
