#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-appimage}"
OUTPUT="${OUTPUT:-$ROOT_DIR/dist/BCDScribe-x86_64.AppImage}"
LINUXDEPLOY_BIN="${LINUXDEPLOY_BIN:-linuxdeploy}"
QT_PLUGIN_BIN="${QT_PLUGIN_BIN:-linuxdeploy-plugin-qt}"
APPIMAGETOOL_BIN="${APPIMAGETOOL_BIN:-appimagetool}"
QMAKE_BIN="${QMAKE:-}"

resolve_executable() {
    local executable="$1"
    if [[ "$executable" == */* ]]; then
        [[ -x "$executable" ]] || {
            printf 'Not executable: %s\n' "$executable" >&2
            exit 1
        }
        printf '%s\n' "$executable"
    else
        command -v "$executable" || {
            printf 'Required tool not found: %s\n' "$executable" >&2
            exit 1
        }
    fi
}

if [[ "$(uname -m)" != "x86_64" ]]; then
    printf 'This packaging script currently targets x86_64 Linux.\n' >&2
    exit 1
fi

if [[ -z "$QMAKE_BIN" ]]; then
    QMAKE_BIN="$(command -v qmake6 || command -v qmake || true)"
fi
if [[ -z "$QMAKE_BIN" ]]; then
    printf 'Could not find qmake; set QMAKE to the Qt 6 qmake executable.\n' >&2
    exit 1
fi

LINUXDEPLOY_PATH="$(resolve_executable "$LINUXDEPLOY_BIN")"
QT_PLUGIN_PATH="$(resolve_executable "$QT_PLUGIN_BIN")"
APPIMAGETOOL_PATH="$(resolve_executable "$APPIMAGETOOL_BIN")"
TOOLS_DIR="$(mktemp -d)"
APPDIR="$(mktemp -d)"
trap 'rm -rf "$TOOLS_DIR" "$APPDIR"' EXIT

ln -s "$LINUXDEPLOY_PATH" "$TOOLS_DIR/linuxdeploy"
ln -s "$QT_PLUGIN_PATH" "$TOOLS_DIR/linuxdeploy-plugin-qt"
mkdir -p "$(dirname -- "$OUTPUT")"
if [[ -e "$OUTPUT" ]]; then
    printf 'Refusing to overwrite existing output: %s\n' "$OUTPUT" >&2
    exit 1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

PATH="$TOOLS_DIR:$PATH" QMAKE="$QMAKE_BIN" APPIMAGE_EXTRACT_AND_RUN=1 \
    "$TOOLS_DIR/linuxdeploy" --appdir "$APPDIR" --plugin qt
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 \
    "$APPIMAGETOOL_PATH" "$APPDIR" "$OUTPUT"

printf 'Created %s\n' "$OUTPUT"