#!/usr/bin/env bash
set -euo pipefail

REPOSITORY_DIR="${BCDSCRIBE_REPOSITORY_DIR:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)}"
APPIMAGE_PATH="${BCDSCRIBE_APPIMAGE_PATH:-$REPOSITORY_DIR/dist/BCDScribe-x86_64.AppImage}"
DRY_RUN=0

for argument in "$@"; do
    case "$argument" in
        --dry-run) DRY_RUN=1 ;;
        --help|-h)
            printf 'Usage: ./create-release.sh [--dry-run]\n'
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$argument" >&2
            exit 2
            ;;
    esac
done

if ! git -C "$REPOSITORY_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'Release skipped: not inside a Git working tree.\n' >&2
    exit 0
fi
if ! command -v gh >/dev/null 2>&1; then
    printf 'Release skipped: GitHub CLI (gh) is not installed.\n' >&2
    exit 0
fi
if ! gh auth status --hostname github.com >/dev/null 2>&1; then
    printf 'Release skipped: authenticate GitHub CLI with `gh auth login`.\n' >&2
    exit 0
fi
if (( ! DRY_RUN )) && ! "$REPOSITORY_DIR/ensure-publish-remote.sh"; then
    printf 'Release skipped: could not prepare the GitHub source/fork remotes.\n' >&2
    exit 0
fi
if ! git -C "$REPOSITORY_DIR" remote get-url origin >/dev/null 2>&1; then
    printf 'Release skipped: configure the origin remote first.\n' >&2
    exit 0
fi

BRANCH="$(git -C "$REPOSITORY_DIR" symbolic-ref --quiet --short HEAD || true)"
if [[ -z "$BRANCH" ]]; then
    printf 'Release skipped: the repository is in detached-HEAD state.\n' >&2
    exit 0
fi

ACCOUNT="$(gh api user --jq '.login')"
if [[ "$ACCOUNT" == "UsefulVideos" ]]; then
    REPOSITORY="UsefulVideos/BCDScribe"
    FORK_HAS_RELEASE=1
else
    REPOSITORY="${ACCOUNT}/BCDScribe"
    FORK_PARENT="$(gh api "repos/$REPOSITORY" --jq 'if .fork then .parent.full_name else "" end' 2>/dev/null || true)"
    if [[ "${FORK_PARENT,,}" != "usefulvideos/bcdscribe" ]]; then
        printf 'Release skipped: %s is not a verified fork of UsefulVideos/BCDScribe.\n' \
            "$REPOSITORY" >&2
        exit 0
    fi
    FORK_HAS_RELEASE=0
fi

HEAD_COMMIT="$(git -C "$REPOSITORY_DIR" rev-parse HEAD)"
LATEST_TAG="$(gh release list --repo "$REPOSITORY" --limit 100 --json tagName,isLatest \
    --jq '[.[] | select(.isLatest)] | first | .tagName // empty')"
if [[ "$ACCOUNT" != "UsefulVideos" && -n "$LATEST_TAG" ]]; then
    FORK_HAS_RELEASE=1
fi

if [[ -z "$LATEST_TAG" && "$ACCOUNT" != "UsefulVideos" ]]; then
    LATEST_TAG="$(gh release list --repo UsefulVideos/BCDScribe --limit 100 --json tagName,isLatest \
        --jq '[.[] | select(.isLatest)] | first | .tagName // empty')"
fi

if [[ -z "$LATEST_TAG" ]]; then
    NEXT_TAG="v0.1.0"
    NOTES_START=()
else
    if [[ ! "$LATEST_TAG" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        printf 'Release skipped: latest tag %s is not a vMAJOR.MINOR.PATCH version.\n' "$LATEST_TAG" >&2
        exit 0
    fi
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    patch="${BASH_REMATCH[3]}"

    if ! git -C "$REPOSITORY_DIR" rev-parse --verify --quiet "refs/tags/$LATEST_TAG" >/dev/null; then
        TAG_REMOTE=origin
        if [[ "$ACCOUNT" != "UsefulVideos" && "$FORK_HAS_RELEASE" == 0 ]]; then
            TAG_REMOTE=upstream
        fi
        git -C "$REPOSITORY_DIR" fetch --quiet "$TAG_REMOTE" "refs/tags/$LATEST_TAG:refs/tags/$LATEST_TAG"
    fi
    RELEASE_COMMIT="$(git -C "$REPOSITORY_DIR" rev-parse "$LATEST_TAG^{commit}")"
    if [[ "$HEAD_COMMIT" == "$RELEASE_COMMIT" && "$FORK_HAS_RELEASE" == 1 ]]; then
        printf 'Release skipped: HEAD is already released as %s.\n' "$LATEST_TAG"
        exit 0
    fi
    if [[ "$HEAD_COMMIT" != "$RELEASE_COMMIT" ]] &&
       ! git -C "$REPOSITORY_DIR" merge-base --is-ancestor "$RELEASE_COMMIT" "$HEAD_COMMIT"; then
        printf 'Release skipped: HEAD does not descend from latest release %s.\n' "$LATEST_TAG" >&2
        exit 0
    fi

    NEXT_PATCH=$((10#$patch + 1))
    NEXT_TAG="v${major}.${minor}.${NEXT_PATCH}"
    NOTES_START=(--notes-start-tag "$LATEST_TAG")
fi

if [[ ! -f "$APPIMAGE_PATH" ]]; then
    printf 'Release skipped: AppImage not found at %s.\n' "$APPIMAGE_PATH" >&2
    exit 0
fi

if (( DRY_RUN )); then
    printf 'Would create %s from commit %s and upload %s\n' "$NEXT_TAG" "$HEAD_COMMIT" "$APPIMAGE_PATH"
    exit 0
fi

cd "$REPOSITORY_DIR"
gh release create "$NEXT_TAG" "$APPIMAGE_PATH" \
    --repo "$REPOSITORY" \
    --target "$HEAD_COMMIT" \
    --title "BCDScribe $NEXT_TAG" \
    --generate-notes \
    "${NOTES_START[@]}"
