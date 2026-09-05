#!/usr/bin/env bash
set -euo pipefail

cd /workspace
mkdir -p build/linux

mapfile -d '' portable_sources < <(find src -name '*.cpp' \
    ! -path 'src/engine/platform/win32/*' \
    ! -path 'src/engine/platform/linux/*' \
    ! -name 'win32_editor.cpp' \
    ! -path 'src/tools/*' \
    -print0)

common_flags=(-std=c++20 -Wall -Wextra -Wpedantic -Werror -Isrc)
g++ "${common_flags[@]}" tests/test_main.cpp "${portable_sources[@]}" \
    -o build/linux/tests

mapfile -d '' runner_sources < <(find src -name '*.cpp' \
    ! -path 'src/engine/platform/win32/*' \
    ! -path 'src/engine/platform/linux/*' \
    ! -name 'win32_editor.cpp' \
    ! -path 'src/tools/*' \
    ! -name 'game.cpp' \
    -print0)
g++ "${common_flags[@]}" "${runner_sources[@]}" src/game/game.cpp \
    src/tools/playtest_runner.cpp -o build/linux/playtest_runner

mapfile -d '' game_sources < <(find src -name '*.cpp' \
    ! -path 'src/engine/platform/win32/*' \
    ! -path 'src/engine/platform/linux/*' \
    ! -name 'win32_editor.cpp' \
    ! -path 'src/tools/*' \
    ! -name 'game.cpp' \
    -print0)
g++ "${common_flags[@]}" "${game_sources[@]}" \
    src/game/game.cpp src/engine/platform/linux/linux_platform.cpp \
    src/engine/platform/linux/linux_image_decoder.cpp \
    src/engine/platform/linux/linux_main.cpp -lX11 -lpng -o build/linux/game

build/linux/tests
build/linux/playtest_runner --all --audit-root /tmp/underworld_docker_audit
