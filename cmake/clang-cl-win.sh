#!/bin/bash
# clang-cl wrapper for cross-compiling to x86_64 Windows (MSVC ABI).
# Works on macOS (Homebrew LLVM) and Linux (LLVM from apt / apt.llvm.org).
_proj="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The MSVC STL refuses to compile under a compiler older than it expects:
#   error STL1000: Unexpected compiler version, expected Clang <N> or newer.
# It is a hard static_assert, not a warning. The version required tracks the
# MSVC toolset the headers came from -- 14.51 wants Clang 20 -- so this floor
# and MsvcToolset in windows/toolchain/versions.psd1 move together.
_min_llvm=20

if [[ "$(uname)" == "Darwin" ]]; then
    _llvm_bin="${PHOTO_SALON_LLVM_BIN:-/opt/homebrew/opt/llvm/bin}"
    _clang="$_llvm_bin/clang-cl"
    _driver=()
else
    # Newest installed LLVM at or above the floor. Ubuntu/Debian packages do not
    # ship a clang-cl binary, so clang is used with --driver-mode=cl instead.
    if [ -n "${PHOTO_SALON_LLVM_BIN:-}" ]; then
        _llvm_bin="$PHOTO_SALON_LLVM_BIN"
    else
        _llvm_bin=""
        for _d in $(ls -d /usr/lib/llvm-* 2>/dev/null | sort -t- -k2 -n -r); do
            _v="${_d##*-}"
            if [ "$_v" -ge "$_min_llvm" ] 2>/dev/null && [ -x "$_d/bin/clang" ]; then
                _llvm_bin="$_d/bin"
                break
            fi
        done
        if [ -z "$_llvm_bin" ]; then
            echo "error: no LLVM >= $_min_llvm found under /usr/lib/llvm-*." >&2
            echo "       The vendored MSVC STL headers require Clang $_min_llvm or newer." >&2
            echo "       Install it:  curl -fsSL https://apt.llvm.org/llvm.sh | sudo bash -s $_min_llvm" >&2
            echo "       and:         sudo apt-get install -y lld-$_min_llvm llvm-$_min_llvm" >&2
            echo "       Or set PHOTO_SALON_LLVM_BIN to a suitable bin directory." >&2
            exit 1
        fi
    fi
    _clang="$_llvm_bin/clang"
    _driver=(--driver-mode=cl)
fi

exec "$_clang" "${_driver[@]}" \
    --target=x86_64-windows-msvc \
    /std:c++17 \
    /Zc:__cplusplus \
    -imsvc "${_proj}/windows/msvc/include" \
    -imsvc "${_proj}/windows/sdk/include/ucrt" \
    -imsvc "${_proj}/windows/sdk/include/shared" \
    -imsvc "${_proj}/windows/sdk/include/um" \
    "$@"
