#!/usr/bin/env bash
set -euo pipefail

cd /workspace
mkdir -p build/linux

mode=${1:-all}
shift || true
jobs=${UNDERWORLD_BUILD_JOBS:-$(nproc)}

case "$mode" in
    clean)
        make -f docker/linux_build.mk clean
        ;;
    tests)
        make -f docker/linux_build.mk -j"$jobs" tests
        build/linux/tests
        ;;
    playtest)
        make -f docker/linux_build.mk -j"$jobs" playtest
        if [[ $# -gt 0 ]]; then
            build/linux/playtest_runner --scenario "$1" --audit-root /tmp/underworld_docker_audit
        else
            build/linux/playtest_runner --all --audit-root /tmp/underworld_docker_audit
        fi
        ;;
    game)
        make -f docker/linux_build.mk -j"$jobs" game
        ;;
    build)
        make -f docker/linux_build.mk -j"$jobs" all
        ;;
    all)
        make -f docker/linux_build.mk -j"$jobs" all
        build/linux/tests
        build/linux/playtest_runner --all --audit-root /tmp/underworld_docker_audit
        ;;
    *)
        echo "unknown Linux build mode: $mode" >&2
        exit 2
        ;;
esac
