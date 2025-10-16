#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build"
CONFIG=""
EXAMPLES="OFF"
TARGET=""
CONFIGURE_ARGS=()

usage() {
    cat <<'USAGE'
Usage: build.sh [options] [-- <additional cmake configure args>]

Options:
  -b, --build-dir DIR   Build directory (default: build inside project root)
  -c, --config NAME     Build configuration name (e.g. Debug, Release)
      --examples        Enable example targets (equivalent to -DZMESH_BUILD_EXAMPLES=ON)
      --no-examples     Disable example targets (default)
  -t, --target NAME     Only build the provided target
  -h, --help            Show this help message and exit

Any arguments following `--` are forwarded to the CMake configure step.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--build-dir)
            [[ $# -lt 2 ]] && { echo "Missing value for $1" >&2; exit 1; }
            BUILD_DIR="$2"
            shift 2
            ;;
        -c|--config)
            [[ $# -lt 2 ]] && { echo "Missing value for $1" >&2; exit 1; }
            CONFIG="$2"
            shift 2
            ;;
        --examples)
            EXAMPLES="ON"
            shift
            ;;
        --no-examples)
            EXAMPLES="OFF"
            shift
            ;;
        -t|--target)
            [[ $# -lt 2 ]] && { echo "Missing value for $1" >&2; exit 1; }
            TARGET="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            CONFIGURE_ARGS+=("$@")
            break
            ;;
        *)
            CONFIGURE_ARGS+=("$1")
            shift
            ;;
    esac
done

if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$SCRIPT_DIR/$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(
    -S "$SCRIPT_DIR"
    -B "$BUILD_DIR"
    -DZMESH_BUILD_EXAMPLES="$EXAMPLES"
)

if [[ -n "$CONFIG" ]]; then
    CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE="$CONFIG")
fi

CMAKE_ARGS+=("${CONFIGURE_ARGS[@]}")

cmake "${CMAKE_ARGS[@]}"

BUILD_CMD=(cmake --build "$BUILD_DIR")

if [[ -n "$CONFIG" ]]; then
    BUILD_CMD+=(--config "$CONFIG")
fi

if [[ -n "$TARGET" ]]; then
    BUILD_CMD+=(--target "$TARGET")
fi

cmake "${BUILD_CMD[@]}"
