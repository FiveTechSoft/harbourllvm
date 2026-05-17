#!/usr/bin/env bash
#
# Build a MinGW-ABI static LLVM + LLD SDK for the Harbour LLVM backend.
#
# The official LLVM Windows binaries are MSVC-ABI and cannot be linked by a
# MinGW-built harbour.exe, so LLVM + LLD are built from source with the same
# MinGW-w64 toolchain. One-time, ~1-2h.
#
# Usage:  tools/build-llvm.sh [llvm_src_dir] [install_prefix]
# Default: tools/build-llvm.sh c:/llvm-src c:/llvm-mingw
#
set -euo pipefail

# Minimal PATH: winlibs toolchain + the Windows Store Python + Windows itself.
# The git-bash dirs are deliberately excluded — CMake's "MinGW Makefiles"
# generator refuses to run when sh.exe (shipped by git-bash) is on PATH.
export PATH="/c/Users/Anto/winlibs/mingw64/bin:/c/Users/Anto/AppData/Local/Microsoft/WindowsApps:/c/Windows/System32:/c/Windows"

SRC="${1:-c:/llvm-src}"
PREFIX="${2:-c:/llvm-mingw}"
BUILD="$SRC/build-mingw"
JOBS="${NUMBER_OF_PROCESSORS:-8}"

echo "LLVM source : $SRC"
echo "install to  : $PREFIX"
echo "parallel    : $JOBS jobs"

cmake -S "$SRC/llvm" -B "$BUILD" -G "MinGW Makefiles" \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="lld" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_ENABLE_ZLIB=OFF \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DLLVM_BUILD_LLVM_DYLIB=OFF \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"

mingw32-make -C "$BUILD" -j"$JOBS"
mingw32-make -C "$BUILD" install

echo "LLVM+LLD installed to $PREFIX"
