#!/bin/sh
# OrchardTop installer for Apple Silicon Macs.
# Usage: curl -fsSL https://raw.githubusercontent.com/rgrossharv/orchardtop/main/install.sh | sh

set -eu

repo="rgrossharv/orchardtop"
asset="orchardtop-macos-arm64.tar.gz"
base_url="https://github.com/${repo}/releases/latest/download"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "OrchardTop currently needs macOS." >&2
    exit 1
fi

if [ "$(uname -m)" != "arm64" ]; then
    echo "OrchardTop currently needs an Apple Silicon Mac." >&2
    exit 1
fi

if [ -z "${HOME:-}" ]; then
    echo "HOME is not set, so the installer cannot choose an install folder." >&2
    exit 1
fi

install_root="${ORCHARDTOP_INSTALL_DIR:-$HOME/.local}"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/orchardtop-install.XXXXXX")"
trap 'rm -rf "$tmp_dir"' EXIT INT TERM

echo "Downloading OrchardTop..."
curl --fail --silent --show-error --location \
    "${base_url}/${asset}" \
    --output "${tmp_dir}/${asset}"
curl --fail --silent --show-error --location \
    "${base_url}/checksums.txt" \
    --output "${tmp_dir}/checksums.txt"

expected="$(awk -v wanted="$asset" '$2 == wanted { print $1; exit }' "${tmp_dir}/checksums.txt")"
if [ -z "$expected" ]; then
    echo "The release did not include a checksum for ${asset}." >&2
    exit 1
fi

actual="$(shasum -a 256 "${tmp_dir}/${asset}" | awk '{ print $1 }')"
if [ "$actual" != "$expected" ]; then
    echo "The downloaded file failed its checksum check." >&2
    exit 1
fi

mkdir -p "$install_root"
tar -xzf "${tmp_dir}/${asset}" -C "$install_root"
chmod 755 "${install_root}/bin/orchardtop"

echo "OrchardTop was installed in ${install_root}."
if ! printf '%s' ":${PATH}:" | grep -q ":${install_root}/bin:"; then
    echo "Add this folder to PATH if the orchardtop command is not found:"
    echo "  export PATH=\"${install_root}/bin:\$PATH\""
fi
echo "Run it with: ${install_root}/bin/orchardtop"
