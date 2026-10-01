#!/usr/bin/env bash
set -euo pipefail

REPOSITORY_DIR="${BCDSCRIBE_REPOSITORY_DIR:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)}"
SOURCE_REPOSITORY="UsefulVideos/BCDScribe"
SOURCE_URL="https://github.com/${SOURCE_REPOSITORY}.git"

if ! git -C "$REPOSITORY_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'Git publishing skipped: not inside a Git work tree.\n' >&2
    exit 1
fi
if ! command -v gh >/dev/null 2>&1 || ! gh auth status --hostname github.com >/dev/null 2>&1; then
    printf 'Git publishing skipped: authenticate GitHub CLI with `gh auth login`.\n' >&2
    exit 1
fi

ACCOUNT="$(gh api user --jq '.login')"

remote_repository() {
    local url="$1"
    url="${url#https://github.com/}"
    url="${url#http://github.com/}"
    url="${url#ssh://git@github.com/}"
    url="${url#git@github.com:}"
    url="${url%.git}"
    printf '%s' "${url,,}"
}

remote_exists() {
    git -C "$REPOSITORY_DIR" remote get-url "$1" >/dev/null 2>&1
}

if [[ "$ACCOUNT" == "UsefulVideos" ]]; then
    if ! remote_exists origin; then
        git -C "$REPOSITORY_DIR" remote add origin "$SOURCE_URL"
    elif [[ "$(remote_repository "$(git -C "$REPOSITORY_DIR" remote get-url origin)")" != "${SOURCE_REPOSITORY,,}" ]]; then
        printf 'Git publishing skipped: UsefulVideos origin is not %s; leaving remotes unchanged.\n' \
            "$SOURCE_REPOSITORY" >&2
        exit 1
    fi
    exit 0
fi

FORK_REPOSITORY="${ACCOUNT}/BCDScribe"
FORK_EXISTS=0
if gh api "repos/$FORK_REPOSITORY" --jq '.full_name' >/dev/null 2>&1; then
    FORK_EXISTS=1
    FORK_PARENT="$(gh api "repos/$FORK_REPOSITORY" --jq 'if .fork then .parent.full_name else "NOT_A_FORK" end')"
    if [[ "${FORK_PARENT,,}" != "${SOURCE_REPOSITORY,,}" ]]; then
        printf 'Git publishing skipped: %s exists but is not a fork of %s.\n' \
            "$FORK_REPOSITORY" "$SOURCE_REPOSITORY" >&2
        exit 1
    fi
fi

if (( ! FORK_EXISTS )); then
    (cd "$REPOSITORY_DIR" && gh repo fork "$SOURCE_REPOSITORY" --default-branch-only)
    FORK_PARENT="$(gh api "repos/$FORK_REPOSITORY" --jq 'if .fork then .parent.full_name else "" end')"
    if [[ "${FORK_PARENT,,}" != "${SOURCE_REPOSITORY,,}" ]]; then
        printf 'Git publishing skipped: could not verify fork %s.\n' "$FORK_REPOSITORY" >&2
        exit 1
    fi
fi

if remote_exists origin; then
    ORIGIN_REPOSITORY="$(remote_repository "$(git -C "$REPOSITORY_DIR" remote get-url origin)")"
    if [[ "$ORIGIN_REPOSITORY" == "${SOURCE_REPOSITORY,,}" ]]; then
        if remote_exists upstream; then
            UPSTREAM_REPOSITORY="$(remote_repository "$(git -C "$REPOSITORY_DIR" remote get-url upstream)")"
            if [[ "$UPSTREAM_REPOSITORY" != "${SOURCE_REPOSITORY,,}" ]]; then
                printf 'Git publishing skipped: upstream already points to %s; leaving remotes unchanged.\n' \
                    "$UPSTREAM_REPOSITORY" >&2
                exit 1
            fi
            git -C "$REPOSITORY_DIR" remote remove origin
        else
            git -C "$REPOSITORY_DIR" remote rename origin upstream
        fi
    elif [[ "$ORIGIN_REPOSITORY" != "${FORK_REPOSITORY,,}" ]]; then
        printf 'Git publishing skipped: origin points to %s instead of the expected fork %s.\n' \
            "$ORIGIN_REPOSITORY" "$FORK_REPOSITORY" >&2
        exit 1
    fi
fi

if remote_exists upstream; then
    UPSTREAM_REPOSITORY="$(remote_repository "$(git -C "$REPOSITORY_DIR" remote get-url upstream)")"
    if [[ "$UPSTREAM_REPOSITORY" != "${SOURCE_REPOSITORY,,}" ]]; then
        printf 'Git publishing skipped: upstream points to %s instead of %s.\n' \
            "$UPSTREAM_REPOSITORY" "$SOURCE_REPOSITORY" >&2
        exit 1
    fi
else
    git -C "$REPOSITORY_DIR" remote add upstream "$SOURCE_URL"
fi

if ! remote_exists origin; then
    git -C "$REPOSITORY_DIR" remote add origin "https://github.com/${FORK_REPOSITORY}.git"
fi
