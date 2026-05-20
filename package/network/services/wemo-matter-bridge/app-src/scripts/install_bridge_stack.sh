#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
BRIDGE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd -P)"
WORKSPACE_ROOT="$(cd "${BRIDGE_ROOT}/.." && pwd -P)"

OPENWEMO_ROOT="${WORKSPACE_ROOT}/openwemo-bridge-core"
BIN_DIR="${WORKSPACE_ROOT}/bin"

BUILD_OPENWEMO=1
RESTART_STACK=1

usage() {
  cat <<'EOF'
Usage: scripts/install_bridge_stack.sh [options]

Build and deploy WeMo bridge binaries into a stack bin directory.

Options:
  --workspace <path>       Workspace root containing sibling repos.
  --openwemo-root <path>   Path to openwemo-bridge-core.
  --bin-dir <path>         Target bin directory for deployed binaries.
  --skip-openwemo-build    Skip building openwemo-bridge-core.
  --no-restart             Do not restart bridge stack.
  -h, --help               Show this help.

Default sibling layout:
  <workspace>/
    connectedhomeip/
    openwemo-bridge-core/
    wemo-matter-bridge/
    bin/
EOF
}

log() {
  printf '[install_bridge_stack] %s\n' "$*"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace)
      WORKSPACE_ROOT="$(cd "$2" && pwd -P)"
      OPENWEMO_ROOT="${WORKSPACE_ROOT}/openwemo-bridge-core"
      BIN_DIR="${WORKSPACE_ROOT}/bin"
      shift 2
      ;;
    --openwemo-root)
      OPENWEMO_ROOT="$(cd "$2" && pwd -P)"
      shift 2
      ;;
    --bin-dir)
      BIN_DIR="$(cd "$2" && pwd -P)"
      shift 2
      ;;
    --skip-openwemo-build)
      BUILD_OPENWEMO=0
      shift
      ;;
    --no-restart)
      RESTART_STACK=0
      shift
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

MATTER_APP_DIR="${BRIDGE_ROOT}/matter-bridge-app"
STACK_SCRIPT="${BIN_DIR}/bridge_stack.sh"
BRIDGE_BIN_SRC="${MATTER_APP_DIR}/out/ethernet/wemo-bridge-app"
BRIDGE_BIN_DST="${BIN_DIR}/wemo-bridge-app"
WEMO_CTRL_SRC="${OPENWEMO_ROOT}/wemo_ctrl/wemo_ctrl"
WEMO_CTRL_DST="${BIN_DIR}/wemo_ctrl"
WEMO_ENGINE_SO_SRC="${OPENWEMO_ROOT}/wemo_engine/libwemoengine.so"
WEMO_ENGINE_SO_DST="${BIN_DIR}/libwemoengine.so"

if [[ ! -d "${MATTER_APP_DIR}" ]]; then
  echo "Missing matter-bridge-app directory: ${MATTER_APP_DIR}" >&2
  exit 1
fi

mkdir -p "${BIN_DIR}"

log "Building Matter bridge app"
(
  cd "${MATTER_APP_DIR}"
  ./build_wemo_bridge.sh
)

if [[ ! -x "${BRIDGE_BIN_SRC}" ]]; then
  echo "Expected binary not found: ${BRIDGE_BIN_SRC}" >&2
  exit 1
fi

if [[ "${BUILD_OPENWEMO}" -eq 1 ]]; then
  if [[ ! -d "${OPENWEMO_ROOT}" ]]; then
    echo "Missing openwemo-bridge-core path: ${OPENWEMO_ROOT}" >&2
    exit 1
  fi

  log "Building openwemo-bridge-core"
  make -C "${OPENWEMO_ROOT}"

  if [[ ! -x "${WEMO_CTRL_SRC}" ]]; then
    echo "Expected binary not found: ${WEMO_CTRL_SRC}" >&2
    exit 1
  fi
fi

log "Deploying ${BRIDGE_BIN_DST}"
cp "${BRIDGE_BIN_SRC}" "${BRIDGE_BIN_DST}"
chmod +x "${BRIDGE_BIN_DST}"

if [[ "${BUILD_OPENWEMO}" -eq 1 ]]; then
  log "Deploying ${WEMO_CTRL_DST}"
  cp "${WEMO_CTRL_SRC}" "${WEMO_CTRL_DST}"
  chmod +x "${WEMO_CTRL_DST}"

  if [[ -f "${WEMO_ENGINE_SO_SRC}" ]]; then
    log "Deploying ${WEMO_ENGINE_SO_DST}"
    cp "${WEMO_ENGINE_SO_SRC}" "${WEMO_ENGINE_SO_DST}"
  fi
fi

if [[ "${RESTART_STACK}" -eq 1 && -x "${STACK_SCRIPT}" ]]; then
  log "Restarting bridge stack using ${STACK_SCRIPT}"
  "${STACK_SCRIPT}" stop || true
  "${STACK_SCRIPT}" start
  "${STACK_SCRIPT}" status || true
else
  log "Install complete. Restart manually if needed:"
  log "  ${STACK_SCRIPT} stop"
  log "  ${STACK_SCRIPT} start"
  log "  ${STACK_SCRIPT} status"
fi

