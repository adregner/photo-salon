#!/usr/bin/env bash
#
# Fetches the pre-built Windows dependencies the cross-compile links against.
#
# What to fetch, and what each archive must hash to, comes from
# windows-deps.lock. That file is generated on a Windows machine by
# windows/toolchain/Make-WindowsToolchain.ps1 -- see doc/WINDOWS.md. Nothing
# here is hand-maintained; to change a dependency, regenerate the bundle and
# commit the new lock.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
LOCK="$ROOT/windows-deps.lock"
DEST="$ROOT/windows"

[ -f "$LOCK" ] || { echo "error: $LOCK not found" >&2; exit 1; }

TMPDIR_WORK="$(mktemp -d "${TMPDIR:-/tmp}/photo-salon-deps.XXXXXX")"
trap 'rm -rf "$TMPDIR_WORK"' EXIT

# Only KEY=value lines; comments and blanks are ignored. The `|| [ -n "$line" ]`
# keeps the last line if the file has no trailing newline.
while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        ''|\#*) continue ;;
        *=*) export "PSLOCK_${line%%=*}=${line#*=}" ;;
    esac
done < "$LOCK"

lock() {
    local var="PSLOCK_$1"
    printf '%s' "${!var:?missing $1 in windows-deps.lock}"
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1   # macOS
    fi
}

fetch() {
    local key="$1"
    local file dir sha marker stamp tmp actual
    file="$(lock "${key}_FILE")"
    dir="$(lock "${key}_DIR")"
    sha="$(lock "${key}_SHA256")"
    marker="$DEST/$dir"
    # Keyed by content, not just presence: bumping the bundle tag has to
    # invalidate a tree that is already on disk from the previous one.
    stamp="$DEST/.${key}.sha256"

    if [ -d "$marker" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$sha" ]; then
        echo "  skip  windows/$dir (up to date)"
        return
    fi

    if [ -d "$marker" ]; then
        echo "  stale windows/$dir -- removing"
        # Remove the archive root, not just the marker: for qt the marker is
        # qt-6.11/x64 but the archive unpacks qt-6.11/.
        rm -rf "$DEST/${dir%%/*}"
    fi

    echo "  fetch $file -> windows/$dir"
    tmp="$TMPDIR_WORK/$file"
    curl -fL --progress-bar "$(lock BASE_URL)/$file" -o "$tmp"

    actual="$(sha256_of "$tmp")"
    if [ "$actual" != "$sha" ]; then
        echo "error: $file hashes $actual, expected $sha" >&2
        echo "       The published artifact and windows-deps.lock disagree." >&2
        exit 1
    fi

    mkdir -p "$DEST"
    tar -xzf "$tmp" -C "$DEST"
    printf '%s' "$sha" > "$stamp"
}

# The linker resolves /DEFAULTLIB directives by opening a file with exactly the
# name the directive carries, and those names use whatever casing the source
# wrote -- WS2_32.lib, uuid.lib, icuin.Lib. That is harmless on Windows and on
# macOS's case-insensitive default, but on a case-sensitive filesystem every
# variant needs to exist. The bundles ship each library once, under its Windows
# SDK casing; the rest are symlinks made here.
add_case_aliases() {
    local dir="$1" f base stem alias
    [ -d "$dir" ] || return 0
    # The SDK's own casing is inconsistent — 170 of its 453 x64 import libraries
    # are not all-lowercase (WS2_32.Lib, User32.Lib, icuin.Lib), so a plain
    # *.lib glob misses most of them on a case-sensitive filesystem.
    for f in "$dir"/*.[Ll][Ii][Bb]; do
        [ -e "$f" ] || continue
        base="$(basename "$f")"
        stem="${base%.*}"
        for alias in "$(printf '%s' "$stem" | tr '[:upper:]' '[:lower:]').lib" \
                     "$(printf '%s' "$stem" | tr '[:lower:]' '[:upper:]').lib"; do
            # -e follows symlinks and is false for a dangling one; on a
            # case-insensitive filesystem it is already true for every alias,
            # which is exactly when we want to skip.
            [ "$alias" = "$base" ] || [ -e "$dir/$alias" ] || ln -s "$base" "$dir/$alias"
        done
    done
}

echo "Windows cross-compilation dependencies (bundle $(lock BUNDLE_TAG), CRT $(lock CRT_LINKAGE))"
for key in MSVC SDK QT CODECS; do
    fetch "$key"
done

echo "Creating case-insensitive library aliases..."
add_case_aliases "$DEST/msvc/lib"
add_case_aliases "$DEST/sdk/lib/um"
add_case_aliases "$DEST/sdk/lib/ucrt"

echo "Done."
