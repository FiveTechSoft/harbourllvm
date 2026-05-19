# lib/win/mingw64-rt — Bundled MinGW-w64 CRT and Import Libraries

These files are collected from the **winlibs GCC 16.1.0** toolchain
(`c:/Users/Anto/winlibs/mingw64/`, x86_64-w64-mingw32 target) by the
script `tools/collect-mingw-rt.sh`.  They are committed here so that
`harbour -GL` can produce a Windows PE executable using the embedded LLD
linker **without requiring any external C compiler or MinGW toolchain at
end-user run time**.

## Contents

| File | Origin | Purpose |
|------|--------|---------|
| `crt2.o` | `x86_64-w64-mingw32/lib/` | MinGW CRT startup object — provides `mainCRTStartup` |
| `crtbegin.o` | `lib/gcc/x86_64-w64-mingw32/16.1.0/` | GCC C++ global constructor init |
| `crtend.o` | `lib/gcc/x86_64-w64-mingw32/16.1.0/` | GCC C++ global destructor fini |
| `libgcc.a` | `lib/gcc/x86_64-w64-mingw32/16.1.0/` | GCC low-level runtime |
| `libgcc_eh.a` | `lib/gcc/x86_64-w64-mingw32/16.1.0/` | GCC exception handling |
| `libstdc++.a` | `lib/` | C++ standard library (needed by Harbour internals) |
| `libmingw32.a` | `x86_64-w64-mingw32/lib/` | MinGW main entry point wrapper |
| `libmingwex.a` | `x86_64-w64-mingw32/lib/` | MinGW POSIX extension layer |
| `libmoldname.a` | `x86_64-w64-mingw32/lib/` | Old-style CRT name compatibility |
| `libmsvcrt.a` | `x86_64-w64-mingw32/lib/` | MSVCRT import library |
| `libucrt.a` | `x86_64-w64-mingw32/lib/` | UCRT import library |
| `libucrtbase.a` | `x86_64-w64-mingw32/lib/` | UCRTBase import library |
| `libkernel32.a` … `libntdll.a` | `x86_64-w64-mingw32/lib/` | Win32 API import libraries |

## License

- **MinGW-w64 runtime** (`crt2.o`, `libmingw32.a`, `libmingwex.a`, `libmoldname.a`, Win32 import libs): MinGW-w64 runtime exception — freely redistributable in binary form; see https://github.com/mingw-w64/mingw-w64/blob/master/COPYING.MinGW-w64-runtime.txt
- **libgcc / libgcc_eh**: GCC Runtime Library Exception (GPL v3 + runtime exception) — freely redistributable when linked into an application; see https://www.gnu.org/licenses/gcc-exception-3.1.html
- **libstdc++**: GCC Runtime Library Exception (LGPL 2.1 + runtime exception) — freely redistributable; see https://www.gnu.org/licenses/gcc-exception-3.1.html
- **UCRT** (`libucrt.a`, `libucrtbase.a`): Microsoft Windows SDK import stubs — freely redistributable

## Regenerating

```bash
# From the repo root, with winlibs on PATH:
tools/collect-mingw-rt.sh
```
