#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
image_name=${UNDERWORLD_LINUX_IMAGE:-dungeon-underworld-linux-build}

docker build -f "$repo_root/docker/Dockerfile.linux" -t "$image_name" "$repo_root"
docker run --rm --mount "type=bind,src=$repo_root,dst=/workspace" "$image_name"
