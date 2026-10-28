#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -x "$ROOT/local-ai-runtime" ]]; then
  RUNTIME="$ROOT/local-ai-runtime"
elif [[ -x "$ROOT/build/local-ai-runtime" ]]; then
  RUNTIME="$ROOT/build/local-ai-runtime"
elif [[ -x "$ROOT/build/Release/local-ai-runtime" ]]; then
  RUNTIME="$ROOT/build/Release/local-ai-runtime"
else
  echo "Runtime executable not found. Run ./scripts/build.sh first, or extract a release package." >&2
  echo "Checked: $ROOT/local-ai-runtime, $ROOT/build/local-ai-runtime, $ROOT/build/Release/local-ai-runtime" >&2
  exit 1
fi

if [[ -n "${XDG_DATA_HOME:-}" ]]; then
  DATA_ROOT="$XDG_DATA_HOME/local-ai-runtime"
elif [[ "$(uname -s)" == "Darwin" ]]; then
  DATA_ROOT="${HOME:-/tmp}/Library/Application Support/LocalAIRuntime"
else
  DATA_ROOT="${HOME:-/tmp}/.local/share/local-ai-runtime"
fi
CONFIG_DIR="$DATA_ROOT/config"
CONFIG_PATH="$CONFIG_DIR/engines.json"
EXAMPLE_PATH="$ROOT/config/engines.example.json"
LEGACY_CONFIG_PATH="$ROOT/config/engines.json"
mkdir -p "$CONFIG_DIR" "$DATA_ROOT"
if [[ ! -f "$CONFIG_PATH" ]]; then
  if [[ -f "$LEGACY_CONFIG_PATH" ]]; then
    cp "$LEGACY_CONFIG_PATH" "$CONFIG_PATH"
    echo "Migrated existing settings to $CONFIG_PATH" >&2
  else
    cp "$EXAMPLE_PATH" "$CONFIG_PATH"
    echo "Created user config: $CONFIG_PATH" >&2
  fi
fi

# Keep logs and mutable data outside the extracted runtime folder. This lets a
# user replace/upgrade the app package without overwriting settings or logs.
cd "$DATA_ROOT"
echo "Starting Local AI Runtime at http://127.0.0.1:8000"
echo "Using executable: $RUNTIME"
echo "User data: $DATA_ROOT"
echo "Press Ctrl+C to stop."
exec "$RUNTIME" --config "$CONFIG_PATH" --web-dir "$ROOT/web"
