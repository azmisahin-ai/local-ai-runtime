#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ -x ./local-ai-runtime ]]; then
  RUNTIME=./local-ai-runtime
elif [[ -x ./build/local-ai-runtime ]]; then
  RUNTIME=./build/local-ai-runtime
elif [[ -x ./build/Release/local-ai-runtime ]]; then
  RUNTIME=./build/Release/local-ai-runtime
else
  echo "Runtime executable not found. Run ./scripts/build.sh first, or extract a release package." >&2
  echo "Checked: $ROOT/local-ai-runtime, $ROOT/build/local-ai-runtime, $ROOT/build/Release/local-ai-runtime" >&2
  exit 1
fi

if [[ ! -f config/engines.json ]]; then
  cp config/engines.example.json config/engines.json
  echo "Created config/engines.json. Edit engine executable/model paths to enable inference." >&2
fi

echo "Starting Local AI Runtime at http://127.0.0.1:8000"
echo "Using executable: $RUNTIME"
echo "Press Ctrl+C to stop."
exec "$RUNTIME" --config config/engines.json --web-dir web
