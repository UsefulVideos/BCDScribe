#!/usr/bin/env bash
set -euo pipefail

REPOSITORY_DIR="${BCDSCRIBE_REPOSITORY_DIR:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)}"

if ! git -C "$REPOSITORY_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'Git publish skipped: %s is not inside a Git work tree.\n' "$REPOSITORY_DIR" >&2
    exit 0
fi

if ! "$REPOSITORY_DIR/ensure-publish-remote.sh"; then
    printf 'Git publish skipped: could not prepare the GitHub source/fork remotes.\n' >&2
    exit 0
fi

BRANCH="$(git -C "$REPOSITORY_DIR" symbolic-ref --quiet --short HEAD || true)"
if [[ -z "$BRANCH" ]]; then
    printf 'Git publish skipped: the repository is in detached-HEAD state.\n' >&2
    exit 0
fi

git -C "$REPOSITORY_DIR" add --all
if git -C "$REPOSITORY_DIR" diff --cached --quiet; then
    printf 'Git publish: no repository changes to commit.\n'
    exit 0
fi

if ! git -C "$REPOSITORY_DIR" config user.name >/dev/null ||
   ! git -C "$REPOSITORY_DIR" config user.email >/dev/null; then
    ACCOUNT="$(gh api user --jq '.login')"
    ACCOUNT_ID="$(gh api user --jq '.id')"
    git -C "$REPOSITORY_DIR" config user.name "$ACCOUNT"
    git -C "$REPOSITORY_DIR" config user.email "${ACCOUNT_ID}+${ACCOUNT}@users.noreply.github.com"
fi

git -C "$REPOSITORY_DIR" commit -m "Update repository after CMake build"
git -C "$REPOSITORY_DIR" push origin "HEAD:refs/heads/$BRANCH"