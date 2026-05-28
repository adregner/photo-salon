#!/bin/bash
# clang-cl wrapper for cross-compiling to x86_64 Windows (MSVC ABI).
# Works on macOS (Homebrew LLVM) and Linux (LLVM 19 from apt).
_proj="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname)" == "Darwin" ]]; then
    _clang_cl="/opt/homebrew/opt/llvm/bin/clang-cl"
else
    _clang_cl="/usr/lib/llvm-19/bin/clang-cl"
fi
exec "$_clang_cl" \
    --target=x86_64-windows-msvc \
    /std:c++17 \
    /Zc:__cplusplus \
    -imsvc "${_proj}/windows/msvc/include" \
    -imsvc "${_proj}/windows/sdk/include/ucrt" \
    -imsvc "${_proj}/windows/sdk/include/shared" \
    -imsvc "${_proj}/windows/sdk/include/um" \
    "$@"
