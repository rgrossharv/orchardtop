#!/bin/bash
# Publish the prepared version and upgrade the Homebrew tap. Safe to resume.
set -euo pipefail
cd "$(dirname "$0")/.."
fail() { echo "$*" >&2; exit 1; }
repo=rgrossharv/orchardtop
tap_repo=rgrossharv/homebrew-orchardtop
formula_name=rgrossharv/orchardtop/orchardtop
version=$(sed -n 's/.*const string Version = "\([^"]*\)";.*/\1/p' src/btop.cpp)
tag="v$version"
commit=$(git rev-parse HEAD)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'Invalid source version.'
[[ $(git branch --show-current) == main ]] || fail 'Run from main.'
[[ -z $(git status --porcelain) ]] || fail 'Commit local changes first.'
case $(git remote get-url origin) in
    https://github.com/rgrossharv/orchardtop|https://github.com/rgrossharv/orchardtop.git|git@github.com:rgrossharv/orchardtop.git) ;;
    *) fail 'Unexpected origin repository.' ;;
esac
if [[ $# == 1 && $1 == --dry-run ]]; then
    echo "Would publish $tag at $commit, update $tap_repo, and upgrade $formula_name."
    echo 'This preview does not contact GitHub or modify Homebrew.'
    exit 0
fi
[[ $# == 0 ]] || fail 'Usage: scripts/release-brew.sh [--dry-run]'
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || fail 'Run on an Apple Silicon Mac.'
for command in gh brew ruby python3; do
    command -v "$command" >/dev/null || fail "Missing $command; install it before continuing."
done
gh auth status --hostname github.com >/dev/null 2>&1 || gh auth login --hostname github.com --git-protocol https --web
gh auth setup-git --hostname github.com
git fetch origin main --tags
git merge-base --is-ancestor origin/main HEAD || fail 'Remote main has new changes. Integrate them before releasing.'
# Fetch failures stop the script; a missing tag is never inferred from an error.
remote_tag=$(git ls-remote --tags origin "refs/tags/$tag")
if git rev-parse --verify "refs/tags/$tag" >/dev/null 2>&1; then
    [[ $(git rev-list -n 1 "$tag") == "$commit" ]] || fail "$tag belongs to another commit. Choose a new version."
fi

if [[ -z "$remote_tag" ]]; then
    brew update --quiet
    gcc="$(brew --prefix gcc@15)/bin/g++-15"
    [[ -x "$gcc" ]] || brew install gcc@15
    ruby tests/update_homebrew_test.rb
    ./scripts/test-power.sh
    make clean QUIET=true
    make QUIET=true CXX="$gcc" LDFLAGS='-static-libgcc -static-libstdc++'
    python3 tests/theme_smoke.py
    [[ -z $(git status --porcelain) ]] || fail 'Source changed during validation.'
    git rev-parse --verify "refs/tags/$tag" >/dev/null 2>&1 || git tag -a "$tag" -m "OrchardTop $version"
    git push --atomic origin HEAD:refs/heads/main "refs/tags/$tag"
fi

run_id=''
for ((attempt=0; attempt<60; attempt++)); do
    run_id=$(gh run list --repo "$repo" --workflow release.yml --commit "$commit" --branch "$tag" --event push --limit 1 --json databaseId --jq '.[0].databaseId // empty')
    [[ -n "$run_id" ]] && break
    sleep 5
done
[[ -n "$run_id" ]] || fail "No workflow found for $tag. Check GitHub Actions and rerun this script."
gh run watch "$run_id" --repo "$repo" --exit-status
[[ $(gh run view "$run_id" --repo "$repo" --json conclusion --jq .conclusion) == success ]] || fail "Release build failed: https://github.com/$repo/actions/runs/$run_id"
[[ $(gh release view "$tag" --repo "$repo" --json isDraft,isPrerelease --jq '(.isDraft == false) and (.isPrerelease == false)') == true ]] || fail 'Release is not published as stable.'

work=$(mktemp -d "${TMPDIR:-/tmp}/orchardtop-release.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/assets"
gh release download "$tag" --repo "$repo" --dir "$work/assets" --pattern orchardtop-macos-arm64.tar.gz --pattern checksums.txt
expected=$(awk '$2 == "orchardtop-macos-arm64.tar.gz" {print $1}' "$work/assets/checksums.txt")
actual=$(shasum -a 256 "$work/assets/orchardtop-macos-arm64.tar.gz" | awk '{print $1}')
[[ "$expected" == "$actual" ]] || fail 'Release checksum mismatch.'

git clone --quiet "https://github.com/$tap_repo.git" "$work/tap"
ruby scripts/update-homebrew.rb "$work/tap/Formula/orchardtop.rb" "$version" "$commit"
ruby -c "$work/tap/Formula/orchardtop.rb"
git -C "$work/tap" diff --check
if ! git -C "$work/tap" diff --quiet; then
    git -C "$work/tap" add Formula/orchardtop.rb
    git -C "$work/tap" commit -m "Update orchardtop to $version"
    git -C "$work/tap" push origin HEAD:main
fi

brew tap rgrossharv/orchardtop
brew update --quiet
formula_version=$(brew info --json=v2 "$formula_name" | ruby -rjson -e 'puts JSON.parse(STDIN.read).fetch("formulae").first.fetch("versions").fetch("stable")')
[[ "$formula_version" == "$version" ]] || fail "Homebrew still sees $formula_version. Update your tap and rerun."
if [[ -n $(brew list --versions "$formula_name") ]]; then
    brew upgrade "$formula_name"
else
    brew install "$formula_name"
fi
brew test "$formula_name"
prefix=$(brew --prefix "$formula_name")
installed_output=$("$prefix/bin/orchardtop" --version)
echo "$installed_output"
echo "$installed_output" | ruby -e 'exit(STDIN.read.match?(/\b#{Regexp.escape(ARGV[0])}(?=\+|\s|\e|$)/) ? 0 : 1)' "$version" || fail 'Installed version does not match.'
test -f "$prefix/share/orchardtop/themes/apple-dark.theme" || fail 'Installed theme is missing.'
echo "Published $tag and verified the Homebrew installation. Launch: $prefix/bin/orchardtop"
echo 'If your shell opens an older ~/.local/bin copy, use the path above.'
