#!/bin/bash
# clang-cl wrapper for cross-compiling to x86_64 Windows (MSVC ABI).
# Works on macOS (Homebrew LLVM) and Linux (LLVM 19 from apt).
_proj="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
_common_flags=(
    --target=x86_64-windows-msvc
    /std:c++17
    /Zc:__cplusplus
    -imsvc "${_proj}/windows/msvc/include"
    -imsvc "${_proj}/windows/sdk/include/ucrt"
    -imsvc "${_proj}/windows/sdk/include/shared"
    -imsvc "${_proj}/windows/sdk/include/um"
)
if [[ "$(uname)" == "Darwin" ]]; then
    exec "/opt/homebrew/opt/llvm/bin/clang-cl" "${_common_flags[@]}" "$@"
else
    # Ubuntu LLVM packages don't ship clang-cl; use clang with --driver-mode=cl.
    exec "/usr/lib/llvm-19/bin/clang" --driver-mode=cl "${_common_flags[@]}" "$@"
fi
