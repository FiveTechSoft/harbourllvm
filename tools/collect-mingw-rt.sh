#!/usr/bin/env bash
# collect-mingw-rt.sh — gather MinGW CRT objects, libgcc, and Win32/UCRT import
# libraries from the winlibs toolchain into lib/win/mingw64-rt/ so that
# hb_llvmLinkExe can reference them by bundled path at runtime (no gcc needed).
#
# Run once from the repo root:
#   tools/collect-mingw-rt.sh
#
# The resolved paths below were discovered by running:
#   gcc -m64 -print-file-name=crt2.o
#   gcc -m64 -print-libgcc-file-name
#   gcc -m64 -print-file-name=libkernel32.a
#   gcc -m64 -print-search-dirs
# against winlibs GCC 16.1.0 at c:/Users/Anto/winlibs/mingw64.
#
# MinGW runtime and libgcc are redistributable under LGPL 2.1 / GPLv3 + runtime
# exception (see lib/win/mingw64-rt/README.md).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$REPO_ROOT/lib/win/mingw64-rt"

# Hard-coded source directories (resolved from gcc -print-file-name output)
# winlibs GCC 16.1.0 for x86_64-w64-mingw32
GCC_VER_DIR="C:/Users/Anto/winlibs/mingw64/lib/gcc/x86_64-w64-mingw32/16.1.0"
SYSLIB_DIR="C:/Users/Anto/winlibs/mingw64/x86_64-w64-mingw32/lib"
STDCXX_DIR="C:/Users/Anto/winlibs/mingw64/lib"

mkdir -p "$DEST"

echo "Collecting into $DEST ..."

# --- CRT startup objects ---
for f in crtbegin.o crtend.o; do
    cp "$GCC_VER_DIR/$f" "$DEST/$f"
    echo "  $f"
done
cp "$SYSLIB_DIR/crt2.o" "$DEST/crt2.o"
echo "  crt2.o"

# --- GCC support libraries ---
for f in libgcc.a libgcc_eh.a; do
    cp "$GCC_VER_DIR/$f" "$DEST/$f"
    echo "  $f"
done

# --- MinGW C runtime support libraries ---
for f in libmingw32.a libmingwex.a libmoldname.a; do
    cp "$SYSLIB_DIR/$f" "$DEST/$f"
    echo "  $f"
done

# --- MSVCRT / UCRT import libraries ---
for f in libmsvcrt.a libucrt.a libucrtbase.a; do
    cp "$SYSLIB_DIR/$f" "$DEST/$f"
    echo "  $f"
done

# --- C++ runtime ---
cp "$STDCXX_DIR/libstdc++.a" "$DEST/libstdc++.a"
echo "  libstdc++.a"

# --- Win32 API import libraries ---
for f in \
    libkernel32.a \
    libuser32.a \
    libgdi32.a \
    libadvapi32.a \
    libws2_32.a \
    libiphlpapi.a \
    libwinspool.a \
    libcomctl32.a \
    libcomdlg32.a \
    libshell32.a \
    libuuid.a \
    libole32.a \
    liboleaut32.a \
    libwinmm.a \
    libmpr.a \
    libmapi32.a \
    libimm32.a \
    libmsimg32.a \
    libwininet.a \
    libntdll.a \
    ; do
    cp "$SYSLIB_DIR/$f" "$DEST/$f"
    echo "  $f"
done

echo ""
echo "Done. $(ls "$DEST" | wc -l) files in $DEST"
echo "Total size: $(du -sh "$DEST" | cut -f1)"
