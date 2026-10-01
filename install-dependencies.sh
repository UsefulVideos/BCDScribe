#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_BIN="${LOCAL_BIN:-$HOME/.local/bin}"
DRY_RUN=0
BUILD_AFTER_SETUP=0

usage() {
    cat <<'EOF'
Usage: ./install-dependencies.sh [--dry-run] [--build]

Install BCDScribe's native build dependencies and AppImage packaging tools.
Package installation may prompt for sudo. --build also creates the AppImage.
Set UPDATE_APPIMAGE_TOOLS=1 to refresh the tools if they are already installed.
EOF
}

for argument in "$@"; do
    case "$argument" in
        --dry-run) DRY_RUN=1 ;;
        --build) BUILD_AFTER_SETUP=1 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$argument" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -r /etc/os-release ]]; then
    printf 'Cannot identify this Linux distribution (/etc/os-release is missing).\n' >&2
    exit 1
fi
# shellcheck disable=SC1091
. /etc/os-release

if (( EUID == 0 )); then
    SUDO=()
elif command -v sudo >/dev/null 2>&1; then
    SUDO=(sudo)
else
    printf 'Install sudo or run this script as root to install system packages.\n' >&2
    exit 1
fi

run_privileged() {
    if (( DRY_RUN )); then
        printf '[dry-run]'
        printf ' %q' "${SUDO[@]}" "$@"
        printf '\n'
    else
        "${SUDO[@]}" "$@"
    fi
}

DISTRO_FAMILY="${ID_LIKE:-} ${ID:-}"
if [[ "$DISTRO_FAMILY" == *debian* || "$DISTRO_FAMILY" == *ubuntu* ]] && command -v apt-get >/dev/null 2>&1; then
    run_privileged apt-get update
    run_privileged apt-get install -y \
        build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-wayland libhivex-dev \
        pkg-config curl file patchelf squashfs-tools
elif [[ "$DISTRO_FAMILY" == *fedora* || "$DISTRO_FAMILY" == *rhel* || "$DISTRO_FAMILY" == *centos* ]] && command -v dnf >/dev/null 2>&1; then
    run_privileged dnf install -y \
        gcc-c++ make cmake qt6-qtbase-devel qt6-qtwayland hivex-devel pkgconf-pkg-config \
        curl file patchelf squashfs-tools
elif [[ "$DISTRO_FAMILY" == *arch* ]] && command -v pacman >/dev/null 2>&1; then
    run_privileged pacman -S --needed --noconfirm \
        base-devel cmake qt6-base qt6-wayland hivex pkgconf curl file patchelf squashfs-tools
elif [[ "$DISTRO_FAMILY" == *suse* ]] && command -v zypper >/dev/null 2>&1; then
    run_privileged zypper refresh
    run_privileged zypper install -y \
        gcc-c++ make cmake qt6-base-devel qt6-wayland-devel hivex-devel pkg-config \
        curl file patchelf squashfs
else
    printf 'Unsupported distribution: %s (%s). Supported families: Debian/Ubuntu, Fedora/RHEL, Arch, and openSUSE.\n' \
        "${PRETTY_NAME:-unknown}" "${ID:-unknown}" >&2
    exit 1
fi

install_appimage_tool() {
    local command_name="$1"
    local filename="$2"
    local url="$3"
    local destination="$LOCAL_BIN/$filename"

    if [[ -x "$destination" && "${UPDATE_APPIMAGE_TOOLS:-0}" != "1" ]]; then
        printf '%s is already installed; set UPDATE_APPIMAGE_TOOLS=1 to refresh it.\n' "$command_name"
    elif (( DRY_RUN )); then
        printf '[dry-run] download %s to %s\n' "$url" "$destination"
    else
        local temporary_file
        temporary_file="$(mktemp)"
        curl -fL --retry 2 "$url" -o "$temporary_file"
        install -Dm755 "$temporary_file" "$destination"
        rm -f "$temporary_file"
    fi

    if (( ! DRY_RUN )); then
        ln -sfn "$filename" "$LOCAL_BIN/$command_name"
    fi
}

if (( ! DRY_RUN )); then
    mkdir -p "$LOCAL_BIN"
fi
install_appimage_tool linuxdeploy linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/latest/download/linuxdeploy-x86_64.AppImage
install_appimage_tool linuxdeploy-plugin-qt linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/latest/download/linuxdeploy-plugin-qt-x86_64.AppImage
install_appimage_tool appimagetool appimagetool-x86_64.AppImage \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage

if [[ ":$PATH:" != *":$LOCAL_BIN:"* ]]; then
    printf 'Add %s to PATH to run the AppImage tools by name.\n' "$LOCAL_BIN"
fi

if (( BUILD_AFTER_SETUP )); then
    if (( DRY_RUN )); then
        printf '[dry-run] %s/build-appimage.sh\n' "$ROOT_DIR"
    else
        "$ROOT_DIR/build-appimage.sh"
    fi
fi