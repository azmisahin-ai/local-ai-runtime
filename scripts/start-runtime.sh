#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ./local-ai-runtime ]]; then
  echo "Runtime executable is missing or not executable: $ROOT/local-ai-runtime" >&2
  exit 1
fi
if [[ ! -f config/engines.json ]]; then
  cp config/engines.example.json config/engines.json
  echo "Created config/engines.json. Edit engine executable/model paths to enable inference." >&2
fi

echo "Starting Local AI Runtime at http://127.0.0.1:8000"
echo "Press Ctrl+C to stop."
./local-ai-runtime --config config/engines.json --web-dir web
