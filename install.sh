#!/usr/bin/env bash
# Install a published, checksum-verified native or desktop release without build tools.
set -euo pipefail
usage() { echo 'Usage: bash install.sh VERSION [--core] [--prefix DIRECTORY]'; }
die() { echo "agent-3d-cad: $*" >&2; exit 1; }
if [[ ${1:-} == --help ]]; then usage; exit 0; fi
version=${1:-}; [[ $# -gt 0 ]] && shift
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-preview\.[1-9][0-9]*)?$ ]] || { usage >&2; exit 1; }
prefix=${HOME:?}/.local/share/agent-3d-cad
package=agent-3d-cad-desktop
while [[ $# -gt 0 ]]; do
  case $1 in
    --core) package=agent-3d-cad; shift ;;
    --prefix) [[ $# -gt 1 && -n $2 ]] || die '--prefix needs a directory'; prefix=$2; shift 2 ;;
    *) die "Unknown option: $1" ;;
  esac
done
case $(uname -s) in
  Darwin) system=Darwin; [[ $(sw_vers -productVersion | cut -d. -f1) -ge 15 ]] || die 'macOS 15 or newer is required' ;;
  Linux) system=Linux
    glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null) || die 'A glibc Linux distribution is required'
    glibc=${glibc#glibc }; major=${glibc%%.*}; minor=${glibc#*.}; minor=${minor%%.*}
    [[ $major -gt 2 || ( $major -eq 2 && $minor -ge 39 ) ]] || die 'glibc 2.39 or newer (such as Ubuntu 24.04) is required' ;;
  *) die 'Use install.ps1 on Windows; this OS is unsupported' ;;
esac
case $(uname -m) in arm64|aarch64) arch=arm64 ;; x86_64|amd64) arch=x64 ;; *) die 'Only arm64 and x64 are supported' ;; esac
# A translated shell reports x86_64 on Apple silicon. Choose the native service
# when Rosetta explicitly identifies the process; Intel/unknown stay x64.
if [[ $system == Darwin && $arch == x64 ]] && [[ $(sysctl -n sysctl.proc_translated 2>/dev/null || true) == 1 ]]; then
  arch=arm64
fi
for command in curl tar mktemp; do command -v "$command" >/dev/null || die "Missing OS utility: $command"; done
if command -v shasum >/dev/null; then hash_file() { shasum -a 256 "$1" | cut -d' ' -f1; }
elif command -v sha256sum >/dev/null; then hash_file() { sha256sum "$1" | cut -d' ' -f1; }
else die 'A SHA-256 utility (shasum or sha256sum) is required'; fi
name=$package-$version-$system-$arch
archive=$name.tar.gz
base=https://github.com/cfaulkingham/agent-3d-cad/releases/download/v$version
stage=$(mktemp -d)
locked=false
stage_install=''
cleanup() { rm -rf -- "$stage"; if [[ -n $stage_install ]]; then rm -rf -- "$stage_install"; fi; if $locked; then rmdir "$prefix/.install-lock"; fi; }
trap cleanup EXIT
echo "Downloading ${archive}..."
curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 "$base/SHA256SUMS" --output "$stage/SHA256SUMS"
curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 "$base/$archive" --output "$stage/$archive"
expected=$(awk -v file="$archive" '$2 == file {print $1}' "$stage/SHA256SUMS")
[[ $expected =~ ^[a-fA-F0-9]{64}$ ]] || die 'Missing or duplicate release checksum'
actual=$(hash_file "$stage/$archive")
[[ $actual == "$expected" ]] || die 'Archive checksum mismatch; nothing installed'
tar -tzf "$stage/$archive" > "$stage/entries"
while IFS= read -r entry; do
  [[ $entry == "$name" || $entry == "$name/"* ]] || die 'Unexpected archive root'
  [[ /$entry/ != *'/../'* && $entry != /* ]] || die 'Unsafe archive path'
done < "$stage/entries"
tar -xzf "$stage/$archive" -C "$stage"
[[ -x $stage/$name/bin/agent-3d-cad ]] || die 'Archive has no native executable'
mkdir -p -- "$prefix"
prefix=$(cd "$prefix" && pwd -P)
mkdir "$prefix/.install-lock" 2>/dev/null || die 'Another installation is running (or remove a stale .install-lock)'
locked=true
[[ ! -e $prefix/$name && ! -L $prefix/$name ]] || die "Already installed: $prefix/$name"
# Copy to an unpublished directory on the destination filesystem, then rename.
stage_install=$(mktemp -d "$prefix/.staging-XXXXXX")
if ! cp -R "$stage/$name/." "$stage_install/"; then rm -rf -- "$stage_install"; die 'Copy failed'; fi
mv "$stage_install" "$prefix/$name"
stage_install=''
exe=$prefix/$name/bin/agent-3d-cad
echo "Installed: $exe"
echo 'Keep your CAD workspace outside this application directory.'
echo 'Generate client settings with:'
printf '  %q config --client codex --workspace %q\n' "$exe" "$HOME/Documents/Agent CAD"
if [[ $package == agent-3d-cad-desktop ]]; then
  printf 'Open the standalone viewer:\n  %q viewer --workspace %q\n' "$exe" "$HOME/Documents/Agent CAD"
fi
