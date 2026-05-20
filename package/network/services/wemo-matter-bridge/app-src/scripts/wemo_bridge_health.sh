#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd -P)"
WORKSPACE_ROOT="$(cd "${REPO_ROOT}/.." && pwd -P)"

BIN_DIR="${WORKSPACE_ROOT}/bin"
VAR_DIR="${WORKSPACE_ROOT}/var"
IPC_PORT="${WEMO_IPC_PORT:-49153}"
MODE="process"

usage() {
  cat <<'EOF'
Usage: scripts/wemo_bridge_health.sh [options]

Checks basic runtime health for wemo_ctrl + wemo-bridge-app.

Options:
  --workspace <path>   Workspace root containing bin/ and var/.
  --systemd            Check using systemd service states.
  --ipc-port <port>    IPC port expected for wemo_ctrl (default: 49153).
  -h, --help           Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace)
      WORKSPACE_ROOT="$(cd "$2" && pwd -P)"
      BIN_DIR="${WORKSPACE_ROOT}/bin"
      VAR_DIR="${WORKSPACE_ROOT}/var"
      shift 2
      ;;
    --systemd)
      MODE="systemd"
      shift
      ;;
    --ipc-port)
      IPC_PORT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

ok=0
warn=0
fail=0

report_ok() {
  printf 'OK   %s\n' "$*"
  ok=$((ok + 1))
}

report_warn() {
  printf 'WARN %s\n' "$*"
  warn=$((warn + 1))
}

report_fail() {
  printf 'FAIL %s\n' "$*"
  fail=$((fail + 1))
}

check_binary() {
  local path="$1"
  local name="$2"
  if [[ -x "$path" ]]; then
    report_ok "$name binary exists: $path"
  else
    report_fail "$name binary missing or not executable: $path"
  fi
}

check_process() {
  local pattern="$1"
  local label="$2"
  if pgrep -af "$pattern" >/dev/null 2>&1; then
    report_ok "$label process running"
  else
    report_fail "$label process not running"
  fi
}

check_systemd_service() {
  local svc="$1"
  if systemctl is-active --quiet "$svc"; then
    report_ok "$svc is active"
  else
    report_fail "$svc is not active"
  fi
}

check_ipc_port() {
  local port="$1"
  if command -v ss >/dev/null 2>&1; then
    if ss -ltn "( sport = :$port )" | tail -n +2 | grep -q .; then
      report_ok "wemo_ctrl IPC port is listening on tcp/$port"
    else
      report_fail "wemo_ctrl IPC port not listening on tcp/$port"
    fi
  else
    report_warn "'ss' command not found; skipped IPC port check"
  fi
}

check_log_file() {
  local path="$1"
  local label="$2"
  if [[ -f "$path" ]]; then
    report_ok "$label log exists: $path"
  else
    report_warn "$label log missing: $path"
  fi
}

WEMO_CTRL_BIN="${BIN_DIR}/wemo_ctrl"
WEMO_BRIDGE_BIN="${BIN_DIR}/wemo-bridge-app"
WEMO_CTRL_LOG="${VAR_DIR}/log/wemo_ctrl.log"
WEMO_BRIDGE_LOG="${VAR_DIR}/log/wemo_bridge.log"

check_binary "$WEMO_CTRL_BIN" "wemo_ctrl"
check_binary "$WEMO_BRIDGE_BIN" "wemo-bridge-app"

if [[ "$MODE" == "systemd" ]]; then
  check_systemd_service "wemo-ctrl.service"
  check_systemd_service "wemo-bridge-app.service"
else
  check_process "${WEMO_CTRL_BIN}" "wemo_ctrl"
  check_process "${WEMO_BRIDGE_BIN}" "wemo-bridge-app"
fi

check_ipc_port "$IPC_PORT"
check_log_file "$WEMO_CTRL_LOG" "wemo_ctrl"
check_log_file "$WEMO_BRIDGE_LOG" "wemo-bridge-app"

printf '\nSummary: ok=%d warn=%d fail=%d\n' "$ok" "$warn" "$fail"
if [[ "$fail" -gt 0 ]]; then
  exit 1
fi

