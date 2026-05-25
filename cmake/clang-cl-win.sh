#!/bin/bash
# clang-cl wrapper for cross-compiling to x86_64 Windows (MSVC ABI) from macOS.
# Uses Microsoft C++ ABI — compatible with MSVC-compiled Qt static libraries.
_proj="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec /opt/homebrew/opt/llvm/bin/clang-cl \
    --target=x86_64-windows-msvc \
    /std:c++17 \
    /Zc:__cplusplus \
    -imsvc "${_proj}/windows/msvc/include" \
    -imsvc "${_proj}/windows/sdk/include/ucrt" \
    -imsvc "${_proj}/windows/sdk/include/shared" \
    -imsvc "${_proj}/windows/sdk/include/um" \
    "$@"
