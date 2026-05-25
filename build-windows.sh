#!/usr/bin/env bash
cmake -B _build_win --toolchain cmake/toolchains/windows-x86_64-clang-cl.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build _build_win
