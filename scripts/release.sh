#!/bin/bash
# Publish the prepared commit, wait for GitHub's build, and install that release.
# Run from a regular Terminal; gh opens browser authentication when needed.
set -euo pipefail
cd "$(dirname "$0")/.."
repo=rgrossharv/orchardtop
version=$(sed -n 's/.*const string Version = "\([^"]*\)";.*/\1/p' src/btop.cpp)
tag="v$version"
branch=$(git symbolic-ref --quiet --short HEAD)
commit=$(git rev-parse HEAD)
[[ "$branch" == main ]] || { echo 'Release from main only.' >&2; exit 1; }
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1
[[ -z "$(git status --porcelain)" ]] || { echo 'Commit the prepared changes before releasing.' >&2; exit 1; }
case "$(git remote get-url origin)" in
    https://github.com/rgrossharv/orchardtop|https://github.com/rgrossharv/orchardtop.git|git@github.com:rgrossharv/orchardtop.git) ;;
    *) echo 'Unexpected origin; refusing to publish.' >&2; exit 1 ;;
esac
if [[ "${1:-}" == --dry-run ]]; then
    echo "Would push $commit on main, publish $tag via GitHub Actions, then install into ${ORCHARDTOP_INSTALL_DIR:-$HOME/.local}."
    exit 0
fi
[[ $# == 0 ]] || { echo 'Usage: scripts/release.sh [--dry-run]' >&2; exit 1; }
command -v gh >/dev/null || { echo 'Install GitHub CLI first: brew install gh' >&2; exit 1; }
./scripts/test-power.sh
if ! gh auth status --hostname github.com >/dev/null 2>&1; then
    gh auth login --hostname github.com --git-protocol https --web
fi
gh auth setup-git --hostname github.com
git fetch origin main --tags
git merge-base --is-ancestor origin/main HEAD || { echo 'Remote main has newer commits. Integrate them and retest first.' >&2; exit 1; }
if git rev-parse --verify "refs/tags/$tag" >/dev/null 2>&1; then
    [[ "$(git rev-list -n 1 "$tag")" == "$commit" ]] || { echo "$tag already belongs to another commit." >&2; exit 1; }
else
    git tag -a "$tag" -m "OrchardTop $version: battery power accuracy and built-in apple-dark"
fi
# No force push. Atomic publication keeps main and the release tag together.
git push --atomic origin HEAD:refs/heads/main "refs/tags/$tag"
run_id=''
for ((attempt=0; attempt<60; attempt++)); do
    run_id=$(gh run list --repo "$repo" --workflow release.yml --commit "$commit" --event push --limit 30 --json databaseId,headBranch --jq ".[] | select(.headBranch == \"$tag\") | .databaseId" | head -1)
    [[ -n "$run_id" ]] && break
    sleep 5
done
[[ -n "$run_id" ]] || { echo 'No release workflow appeared. Check GitHub Actions, then rerun this script.' >&2; exit 1; }
gh run watch "$run_id" --repo "$repo" --exit-status
# Install this exact tag, even if another release was published in the meantime.
ORCHARDTOP_VERSION="$tag" sh ./install.sh
"${ORCHARDTOP_INSTALL_DIR:-$HOME/.local}/bin/orchardtop" --version
gh release view "$tag" --repo "$repo" --json url --jq .url
echo "Launch the upgrade with: ${ORCHARDTOP_INSTALL_DIR:-$HOME/.local}/bin/orchardtop"
echo 'If a Homebrew copy is earlier on PATH, use the full path above or put ~/.local/bin first.'
