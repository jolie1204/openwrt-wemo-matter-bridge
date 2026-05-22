#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TAG="${1:-}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"

usage() {
  cat <<'EOF'
Usage: scripts/release/create_github_release.sh <tag> [--upload-only]

Creates or updates a GitHub Release using files staged in dist/.
Requires GitHub CLI (`gh`) authenticated with repo write access.
EOF
}

if [[ -z "$TAG" || "${TAG:-}" == "-h" || "${TAG:-}" == "--help" ]]; then
  usage
  exit 2
fi

UPLOAD_ONLY=0
if [[ "${2:-}" == "--upload-only" ]]; then
  UPLOAD_ONLY=1
fi

command -v gh >/dev/null 2>&1 || {
  echo "gh CLI is required" >&2
  exit 1
}

if [[ ! -d "$DIST_DIR" || -z "$(find "$DIST_DIR" -maxdepth 1 -type f -print -quit)" ]]; then
  TAG="$TAG" "$ROOT_DIR/scripts/release/package_release_assets.sh"
fi

if [[ "$UPLOAD_ONLY" -eq 1 ]] || gh release view "$TAG" >/dev/null 2>&1; then
  gh release upload "$TAG" "$DIST_DIR"/* --clobber
else
  gh release create "$TAG" "$DIST_DIR"/* --generate-notes
fi
