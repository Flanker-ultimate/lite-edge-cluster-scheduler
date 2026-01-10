#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: script/clean_workspace.sh [--with-logs] [--reset-agent-id]

Clean workspace data directories for client/slave/master.
  --with-logs        also remove workspace/{slave,master}/log/*
  --reset-agent-id   remove .agent_config.json to regenerate device ID
EOF
}

WITH_LOGS=0
RESET_AGENT_ID=0

for arg in "$@"; do
  case "$arg" in
    --with-logs) WITH_LOGS=1 ;;
    --reset-agent-id) RESET_AGENT_ID=1 ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown option: $arg" >&2
      usage
      exit 1
      ;;
  esac
done

cd "$PROJECT_ROOT"

echo "============================================"
echo "cleaning workspace"
echo "path: $PROJECT_ROOT"
echo "============================================"

# client: clear request results
rm -rf workspace/client/data/*/rst/* || true

# slave: clear inputs/outputs and optionally logs
rm -rf workspace/slave/data/*/input/* || true
rm -rf workspace/slave/data/*/output/* || true
if [[ "$WITH_LOGS" -eq 1 ]]; then
  rm -rf workspace/slave/log/* || true
fi

# master: clear uploads and optionally logs
rm -rf workspace/master/data/upload/* || true
if [[ "$WITH_LOGS" -eq 1 ]]; then
  rm -rf workspace/master/log/* || true
fi

if [[ "$RESET_AGENT_ID" -eq 1 ]]; then
  rm -f .agent_config.json || true
fi

echo "============================================"
echo "clean completed"
echo "============================================"
