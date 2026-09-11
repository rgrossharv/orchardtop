#!/bin/bash
# Publish a repaired OrchardTop release and upgrade it through the Homebrew tap.
#
# Before running this script, update src/btop.cpp to a new version (for example
# 1.4.10), commit the source/workflow changes on main, and run:
#
#   ./scripts/release-brew.sh --dry-run
#   ./scripts/release-brew.sh
#
# The script uses GitHub CLI browser authentication when needed. It never asks
# for a password and never force-pushes either repository.
set -euo pipefail

cd "$(dirname "$0")/.."

source_repo="rgrossharv/orchardtop"
tap_repo="rgrossharv/homebrew-orchardtop"
tap_name="rgrossharv/orchardtop"
formula_rel="Formula/orchardtop.rb"
version="$(sed -n 's/.*const string Version = "\([^"]*\)";.*/\1/p' src/btop.cpp)"
tag="v$version"
branch="$(git symbolic-ref --quiet --short HEAD)"
commit="$(git rev-parse HEAD)"
dry_run=false

if [[ "${1:-}" == "--dry-run" ]]; then
    dry_run=true
elif [[ $# -ne 0 ]]; then
    echo "Usage: scripts/release-brew.sh [--dry-run]" >&2
    exit 2
fi

[[ "$branch" == main ]] || { echo "Release from main only (found $branch)." >&2; exit 1; }
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo "src/btop.cpp does not contain a stable release version: $version" >&2
    exit 1
}
[[ -z "$(git status --porcelain)" ]] || {
    echo "Commit the source and workflow changes before releasing." >&2
    exit 1
}
case "$(git remote get-url origin)" in
    https://github.com/rgrossharv/orchardtop|https://github.com/rgrossharv/orchardtop.git|git@github.com:rgrossharv/orchardtop.git) ;;
    *) echo "Unexpected orchardtop origin; refusing to publish." >&2; exit 1 ;;
esac

command -v gh >/dev/null || { echo "Install GitHub CLI first: brew install gh" >&2; exit 1; }
command -v brew >/dev/null || { echo "Homebrew is required for the formula upgrade." >&2; exit 1; }

remote_tag="$(git ls-remote --tags origin "refs/tags/$tag")" || {
    echo "Could not query origin for $tag; check network/authentication and retry." >&2
    exit 1
}
if [[ -n "$remote_tag" ]]; then
    echo "$tag already exists on GitHub. Choose a new version; the failed v1.4.9 tag is intentionally not overwritten." >&2
    exit 1
fi

if [[ "$dry_run" == true ]]; then
    echo "Would build with $(brew --prefix gcc@15 2>/dev/null || echo 'Homebrew GCC 15')/bin/g++-15."
    echo "Would push $commit to main and create $tag in $source_repo."
    echo "Would update $tap_repo/$formula_rel, push it, then brew upgrade orchardtop."
    exit 0
fi

if ! gh auth status --hostname github.com >/dev/null 2>&1; then
    gh auth login --hostname github.com --git-protocol https --web
fi
gh auth setup-git --hostname github.com

brew update --quiet
if ! brew list --versions gcc@15 >/dev/null 2>&1; then
    brew install gcc@15
fi
gcc_prefix="$(brew --prefix gcc@15)"
gcc="${gcc_prefix}/bin/g++-15"
[[ -x "$gcc" ]] || { echo "Homebrew GCC 15 was not found at $gcc." >&2; exit 1; }

./scripts/test-power.sh
make clean QUIET=true
make QUIET=true CXX="$gcc"
python3 tests/theme_smoke.py

git fetch origin main --tags
git merge-base --is-ancestor origin/main HEAD || {
    echo "Remote main has newer commits. Integrate them and retest first." >&2
    exit 1
}

git tag -a "$tag" -m "OrchardTop $version: repaired release and Homebrew upgrade"
git push --atomic origin HEAD:refs/heads/main "refs/tags/$tag"

run_id=""
for ((attempt = 0; attempt < 60; attempt++)); do
    run_id="$(gh run list --repo "$source_repo" --workflow release.yml --commit "$commit" --event push --limit 30 --json databaseId,headBranch --jq ".[] | select(.headBranch == \"$tag\") | .databaseId" | head -1)"
    [[ -n "$run_id" ]] && break
    sleep 5
done
[[ -n "$run_id" ]] || { echo "No release workflow appeared for $tag." >&2; exit 1; }
gh run watch "$run_id" --repo "$source_repo" --exit-status

release_json="$(gh release view "$tag" --repo "$source_repo" --json tagName,isDraft,isPrerelease,assets,url)"
printf '%s\n' "$release_json" | grep -q '"isDraft":false'
printf '%s\n' "$release_json" | grep -q 'orchardtop-macos-arm64.tar.gz'
printf '%s\n' "$release_json" | grep -q 'checksums.txt'

tap_dir="$(mktemp -d "${TMPDIR:-/tmp}/orchardtop-homebrew.XXXXXX")"
trap 'rm -rf "$tap_dir"' EXIT INT TERM
git clone --quiet "https://github.com/$tap_repo.git" "$tap_dir"
formula="$tap_dir/$formula_rel"
[[ -f "$formula" ]] || { echo "Tap formula not found: $formula_rel" >&2; exit 1; }

FORMULA_PATH="$formula" RELEASE_TAG="$tag" RELEASE_VERSION="$version" RELEASE_REVISION="$commit" ruby <<'RUBY'
path = ENV.fetch("FORMULA_PATH")
tag = ENV.fetch("RELEASE_TAG")
version = ENV.fetch("RELEASE_VERSION")
revision = ENV.fetch("RELEASE_REVISION")
formula = File.read(path)
formula.sub!(/tag:\s*"v[^"]+"/, "tag: \"#{tag}\"") or abort "Formula tag field not found"
formula.sub!(/revision:\s*"[0-9a-f]+"/, "revision: \"#{revision}\"") or abort "Formula revision field not found"
formula.sub!(/version\s+"[^"]+"/, "version \"#{version}\"") or abort "Formula version field not found"
unless formula.include?("depends_on \"gcc@15\"")
  formula.sub!(/(depends_on arch: :arm64\n)/, "\\1  depends_on \"gcc@15\"\n") or abort "Formula architecture dependency not found"
end
formula.sub!(/system "make", "QUIET=true"/, 'system "make", "QUIET=true", "CXX=#{Formula["gcc@15"].opt_bin}/g++-15"') or abort "Formula build command not found"
File.write(path, formula)
RUBY

ruby -c "$formula" >/dev/null
git -C "$tap_dir" diff --check
git -C "$tap_dir" diff --quiet && {
    echo "The Homebrew formula already matched $tag; refusing to create an empty tap commit." >&2
    exit 1
}
git -C "$tap_dir" add "$formula_rel"
git -C "$tap_dir" commit -m "Update orchardtop to $version"
git -C "$tap_dir" push origin HEAD:main

brew update --quiet
brew tap "$tap_name" >/dev/null 2>&1 || true
if brew list --formula orchardtop >/dev/null 2>&1; then
    brew upgrade orchardtop
else
    brew install "$tap_name/orchardtop"
fi
brew test orchardtop
installed="$(brew list --versions orchardtop | awk '{print $2}')"
[[ "$installed" == "$version" ]] || {
    echo "Homebrew installed $installed, expected $version." >&2
    exit 1
}
brew_bin="$(brew --prefix)/bin/orchardtop"
"$brew_bin" --version
echo "Published $tag and upgraded Homebrew orchardtop to $version."
echo "Binary: $brew_bin"
