#!/usr/bin/env sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"
# Keep the repository-local assets directory as the default root. An explicit
# --asset-root supplied by the caller appears later and overrides it.
exec python3 -m tools.content_studio --asset-root "$SCRIPT_DIR/assets" "$@"
