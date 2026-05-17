# Harbour LLVM Plan 2 — Embed libLLVM + LLD

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `harbour -GL hello.prg` produces a runnable `hello.exe` directly — no external C compiler, no external linker — by embedding libLLVM and LLD inside `harbour.exe`.

**Architecture:** Plan 1's `genllvm.c` already emits correct LLVM IR text. Plan 2 adds an embedded back half: parse that IR with the libLLVM **C API**, emit a native object file with `LLVMTargetMachineEmitToFile`, then link the object with the prebuilt Harbour runtime archives by calling **LLD as a library** through a small C++ shim. libLLVM and LLD are statically linked into `harbour.exe`.

**Tech Stack:** Harbour compiler (C), LLVM C API (`llvm-c/`), LLD C++ driver (`lld::mingw::link`), MinGW-w64 GCC 16.1.0 toolchain.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Platform:** Windows x86_64, MinGW (`win/mingw64`). The official LLVM Windows binaries are MSVC-ABI and **cannot** be linked by a MinGW-built `harbour.exe`; therefore LLVM + LLD are built from source with the same MinGW toolchain. Other platforms are a later plan.

**Prerequisite (Plan 1):** complete — `harbour -GL` emits validated LLVM IR.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/compiler/hb_lldshim.cpp` (new) | C++ shim: one `extern "C"` function wrapping `lld::mingw::link`. The only C++ in the compiler. |
| `src/compiler/hb_lldshim.h` (new) | C-callable declaration of the shim. |
| `src/compiler/hb_llvmobj.c` (new) | libLLVM C-API code: parse `.ll` → emit `.o`; build the linker argv and call the shim. |
| `src/compiler/hb_llvmobj.h` (new) | Declarations of `hb_llvmEmitObject` / `hb_llvmLinkExe`. |
| `src/compiler/genllvm.c` (modify) | After writing the `.ll`, when an executable is requested, call the emit+link path. |
| `src/compiler/Makefile` (modify) | Add the new sources, a C++ compile rule, and the LLVM include path. |
| `src/main/Makefile` (modify) | Link `harbour.exe` against the LLVM + LLD static libs and `libstdc++`. |
| `config/llvm-paths.mk` (new) | Single place that records the LLVM install prefix; included by the two Makefiles. |

---

### Task 1: Build LLVM + LLD from source with MinGW

**Files:**
- Create: `c:\HarbourLLVM\core\config\llvm-paths.mk`
- Create: `c:\HarbourLLVM\core\tools\build-llvm.sh` (the reproducible build script)

This task installs a MinGW-ABI LLVM+LLD SDK. It is environment setup, not TDD — the "test" is that `llvm-config` and the libraries exist and are usable by the MinGW toolchain.

- [ ] **Step 1: Verify build prerequisites**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
gcc --version          # MinGW-w64 GCC 16.1.0 expected
cmake --version        # bundled with winlibs
mingw32-make --version # GNU Make 4.4.1
python --version || py --version   # LLVM's build needs Python 3
```

Expected: `gcc`, `cmake`, `mingw32-make` present. If Python is missing, install a portable Python 3 and put it on PATH — the LLVM CMake build requires it. Confirm at least ~40 GB free disk on `c:`.

- [ ] **Step 2: Get the LLVM source**

Use the LLVM release that matches a recent stable (19.x). Shallow-clone to keep it small:

```bash
cd c:/
git clone --depth 1 --branch llvmorg-19.1.7 \
  https://github.com/llvm/llvm-project.git llvm-src
```

Expected: `c:/llvm-src/llvm/CMakeLists.txt` exists.

- [ ] **Step 3: Write the build script**

Create `c:\HarbourLLVM\core\tools\build-llvm.sh`:

```bash
#!/usr/bin/env bash
# Build a MinGW-ABI static LLVM + LLD SDK for the Harbour LLVM backend.
# One-time, ~1-2h. Output install prefix: c:/llvm-mingw
set -euo pipefail
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"

SRC="${1:-c:/llvm-src}"
PREFIX="${2:-c:/llvm-mingw}"
BUILD="$SRC/build-mingw"

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

mingw32-make -C "$BUILD" -j"$(nproc)"
mingw32-make -C "$BUILD" install
echo "LLVM+LLD installed to $PREFIX"
```

Note: the build runs in git-bash, but CMake's "MinGW Makefiles" generator rejects `sh.exe` on PATH. The script invokes `cmake` directly; if CMake still errors about `sh.exe`, run Steps 4 from a plain `cmd.exe` with only the winlibs `bin` on PATH.

- [ ] **Step 4: Run the build**

```bash
chmod +x c:/HarbourLLVM/core/tools/build-llvm.sh
c:/HarbourLLVM/core/tools/build-llvm.sh c:/llvm-src c:/llvm-mingw
```

Expected (after ~1-2h): `c:/llvm-mingw/bin/llvm-config.exe`, `c:/llvm-mingw/include/llvm-c/Core.h`, `c:/llvm-mingw/lib/libLLVMCore.a`, and the LLD libs `c:/llvm-mingw/lib/liblldCommon.a`, `liblldMinGW.a`, `liblldCOFF.a`.

- [ ] **Step 5: Verify the SDK**

```bash
c:/llvm-mingw/bin/llvm-config.exe --version          # 19.1.7
c:/llvm-mingw/bin/llvm-config.exe --libs core irreader x86codegen
ls c:/llvm-mingw/lib/liblld*.a
```

Expected: `llvm-config` prints a version and a `-l...` list; the three `lld` archives exist.

- [ ] **Step 6: Record the prefix and commit**

Create `c:\HarbourLLVM\core\config\llvm-paths.mk`:

```make
# Location of the MinGW-ABI LLVM + LLD SDK built by tools/build-llvm.sh.
# Override on the command line if installed elsewhere.
HB_LLVM_PREFIX ?= c:/llvm-mingw
HB_LLVM_CONFIG := $(HB_LLVM_PREFIX)/bin/llvm-config
```

```bash
cd c:/HarbourLLVM/core
git add tools/build-llvm.sh config/llvm-paths.mk
git commit -m "build: add MinGW LLVM+LLD SDK build script and path config"
```

---

### Task 2: LLD C++ shim

**Files:**
- Create: `c:\HarbourLLVM\core\src\compiler\hb_lldshim.h`
- Create: `c:\HarbourLLVM\core\src\compiler\hb_lldshim.cpp`
- Test: `c:\HarbourLLVM\core\tests\llvm\shimtest.c`

LLD has no C API. This shim is the single C++ translation unit in the compiler; it exposes one `extern "C"` function so the rest of the C code can link a PE executable.

- [ ] **Step 1: Write the header**

`src/compiler/hb_lldshim.h`:

```c
/* C-callable wrapper around the LLD MinGW linker driver. */
#ifndef HB_LLDSHIM_H_
#define HB_LLDSHIM_H_

#if defined( __cplusplus )
extern "C" {
#endif

/* Link with the LLD MinGW driver.
 * argv mirrors a `ld`-style invocation, argv[0] is the program name,
 * e.g. { "ld.lld", "-o", "hello.exe", "hello.o", "-Lc:/.../lib", "-lhbvm", ... }.
 * Returns 0 on success, non-zero on failure. */
int hb_lld_link_mingw( int argc, const char ** argv );

#if defined( __cplusplus )
}
#endif

#endif
```

- [ ] **Step 2: Write the shim**

`src/compiler/hb_lldshim.cpp`:

```cpp
/* C++ shim: wrap lld::mingw::link so C code can link a PE executable
 * without spawning an external linker. */
#include "hb_lldshim.h"

#include "lld/Common/Driver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

LLD_HAS_DRIVER( mingw )   /* declares lld::mingw::link */

extern "C" int hb_lld_link_mingw( int argc, const char ** argv )
{
   llvm::ArrayRef< const char * > args( argv, static_cast< size_t >( argc ) );

   bool ok = lld::mingw::link( args,
                               llvm::outs(),
                               llvm::errs(),
                               /* exitEarly  */ false,
                               /* disableOut */ false );
   return ok ? 0 : 1;
}
```

- [ ] **Step 3: Write a standalone test**

`tests/llvm/shimtest.c` — a tiny program that links a trivial object through the shim:

```c
#include "hb_lldshim.h"
#include <stdio.h>

int main( void )
{
   const char * argv[] = {
      "ld.lld", "-o", "build/shimtest_out.exe", "build/shimtest_obj.o"
   };
   int rc = hb_lld_link_mingw( 4, argv );
   printf( "lld returned %d\n", rc );
   return rc;
}
```

- [ ] **Step 4: Compile the shim and test, verify it links a PE**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
LLVMINC=$(c:/llvm-mingw/bin/llvm-config --includedir)
LLVMLIBDIR=$(c:/llvm-mingw/bin/llvm-config --libdir)
mkdir -p build
# a trivial object for the linker to consume
printf 'int mainCRTStartup(void){return 0;}\n' > build/shimtest_src.c
gcc -c build/shimtest_src.c -o build/shimtest_obj.o
g++ -c src/compiler/hb_lldshim.cpp -I src/compiler -I "$LLVMINC" -o build/hb_lldshim.o
gcc -c tests/llvm/shimtest.c -I src/compiler -o build/shimtest.o
g++ build/shimtest.o build/hb_lldshim.o -o build/shimtest.exe \
  -L "$LLVMLIBDIR" -llldMinGW -llldCommon \
  $(c:/llvm-mingw/bin/llvm-config --libs option support) \
  -lstdc++
./build/shimtest.exe
```

Expected: `lld returned 0` and `build/shimtest_out.exe` is created (a do-nothing PE). If LLD reports missing libraries, that is acceptable for this isolated test as long as `hb_lld_link_mingw` itself runs and returns LLD's status — the point is the shim compiles, links, and reaches `lld::mingw::link`.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_lldshim.h src/compiler/hb_lldshim.cpp tests/llvm/shimtest.c
git commit -m "compiler: add LLD MinGW linker C++ shim"
```

---

### Task 3: IR text → native object file

**Files:**
- Create: `c:\HarbourLLVM\core\src\compiler\hb_llvmobj.h`
- Create: `c:\HarbourLLVM\core\src\compiler\hb_llvmobj.c`
- Test: `c:\HarbourLLVM\core\tests\llvm\objtest.c`

- [ ] **Step 1: Write the header**

`src/compiler/hb_llvmobj.h`:

```c
/* Embedded LLVM back end: turn LLVM IR text into a native executable. */
#ifndef HB_LLVMOBJ_H_
#define HB_LLVMOBJ_H_

#if defined( __cplusplus )
extern "C" {
#endif

/* Parse the LLVM IR text file szLLPath and emit a native object file to
 * szObjPath. Returns 0 on success; on failure returns non-zero and writes a
 * message to stderr. */
int hb_llvmEmitObject( const char * szLLPath, const char * szObjPath );

/* Link szObjPath with the Harbour runtime archives found under szLibDir into
 * the executable szExePath, using the embedded LLD driver.
 * Returns 0 on success, non-zero on failure. */
int hb_llvmLinkExe( const char * szObjPath, const char * szLibDir,
                    const char * szExePath );

#if defined( __cplusplus )
}
#endif

#endif
```

- [ ] **Step 2: Write the object emitter**

`src/compiler/hb_llvmobj.c` — first the IR→object half (the link half is added in Task 4). Based on the LLVM C API (`llvm-c/`):

```c
/* Embedded LLVM back end for the Harbour compiler. */
#include "hb_llvmobj.h"
#include "hb_lldshim.h"

#include "llvm-c/Core.h"
#include "llvm-c/IRReader.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hb_llvmEmitObject( const char * szLLPath, const char * szObjPath )
{
   LLVMContextRef     ctx;
   LLVMMemoryBufferRef buf = NULL;
   LLVMModuleRef      mod  = NULL;
   LLVMTargetRef      target = NULL;
   LLVMTargetMachineRef tm;
   LLVMTargetDataRef  layout;
   char *             triple;
   char *             err  = NULL;

   LLVMInitializeX86TargetInfo();
   LLVMInitializeX86Target();
   LLVMInitializeX86TargetMC();
   LLVMInitializeX86AsmPrinter();

   ctx = LLVMContextCreate();

   if( LLVMCreateMemoryBufferWithContentsOfFile( szLLPath, &buf, &err ) )
   {
      fprintf( stderr, "harbour -GL: cannot read IR '%s': %s\n", szLLPath, err );
      LLVMDisposeMessage( err );
      LLVMContextDispose( ctx );
      return 1;
   }

   if( LLVMParseIRInContext( ctx, buf, &mod, &err ) )   /* consumes buf */
   {
      fprintf( stderr, "harbour -GL: IR parse error: %s\n", err );
      LLVMDisposeMessage( err );
      LLVMContextDispose( ctx );
      return 1;
   }

   triple = LLVMGetDefaultTargetTriple();   /* x86_64-w64-windows-gnu */
   LLVMSetTarget( mod, triple );

   if( LLVMGetTargetFromTriple( triple, &target, &err ) )
   {
      fprintf( stderr, "harbour -GL: target lookup failed: %s\n", err );
      LLVMDisposeMessage( err );
      LLVMDisposeMessage( triple );
      LLVMDisposeModule( mod );
      LLVMContextDispose( ctx );
      return 1;
   }

   tm = LLVMCreateTargetMachine( target, triple, "generic", "",
                                 LLVMCodeGenLevelDefault,
                                 LLVMRelocDefault,
                                 LLVMCodeModelDefault );

   layout = LLVMCreateTargetDataLayout( tm );
   LLVMSetModuleDataLayout( mod, layout );
   LLVMDisposeTargetData( layout );

   if( LLVMTargetMachineEmitToFile( tm, mod, szObjPath, LLVMObjectFile, &err ) )
   {
      fprintf( stderr, "harbour -GL: object emit failed: %s\n", err );
      LLVMDisposeMessage( err );
      LLVMDisposeTargetMachine( tm );
      LLVMDisposeMessage( triple );
      LLVMDisposeModule( mod );
      LLVMContextDispose( ctx );
      return 1;
   }

   LLVMDisposeTargetMachine( tm );
   LLVMDisposeMessage( triple );
   LLVMDisposeModule( mod );
   LLVMContextDispose( ctx );
   return 0;
}

/* hb_llvmLinkExe is added in Task 4. */
```

Note: `LLVMParseIRInContext` (not the `2` variant) is used because it consumes the buffer — fewer ownership steps. The `LLVMInitializeX86*` calls are the explicit per-target form (the `LLVMInitializeNative*` inline wrappers also work; explicit X86 is unambiguous for this x86_64-only plan).

- [ ] **Step 3: Write the test**

`tests/llvm/objtest.c`:

```c
#include "hb_llvmobj.h"
#include <stdio.h>

int main( int argc, char ** argv )
{
   if( argc != 3 )
   {
      fprintf( stderr, "usage: objtest <in.ll> <out.o>\n" );
      return 2;
   }
   return hb_llvmEmitObject( argv[ 1 ], argv[ 2 ] );
}
```

- [ ] **Step 4: Build the test and verify an object is produced**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
LLVMINC=$(c:/llvm-mingw/bin/llvm-config --includedir)
LLVMLIBDIR=$(c:/llvm-mingw/bin/llvm-config --libdir)
# generate a fresh .ll with the Plan 1 backend
bin/win/mingw64/harbour.exe -GL -q -obuild/objtest_in tests/llvm/hello.prg
gcc -c src/compiler/hb_llvmobj.c -I src/compiler -I "$LLVMINC" -o build/hb_llvmobj.o
gcc -c tests/llvm/objtest.c -I src/compiler -o build/objtest.o
g++ build/objtest.o build/hb_llvmobj.o -o build/objtest.exe \
  -L "$LLVMLIBDIR" \
  $(c:/llvm-mingw/bin/llvm-config --libs core irreader x86codegen target) \
  -lstdc++
./build/objtest.exe build/objtest_in.ll build/objtest_out.o
file build/objtest_out.o
```

Expected: exit 0 and `build/objtest_out.o` is a `COFF object` / `PE` object file (verify with `file`, or `gcc -c` can later consume it). If `file` is unavailable, check the object is non-empty and that `gcc build/objtest_out.o -o /tmp/x.exe -lhbvm ...` would accept it — minimal check: non-zero size and exit 0.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_llvmobj.h src/compiler/hb_llvmobj.c tests/llvm/objtest.c
git commit -m "compiler: add embedded LLVM IR-to-object emitter"
```

---

### Task 4: Embedded link driver

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_llvmobj.c` (add `hb_llvmLinkExe`)
- Test: `c:\HarbourLLVM\core\tests\llvm\linktest.c`

`hb_llvmLinkExe` builds the `ld.lld`-style argument vector — the object, the Harbour runtime archives, the MinGW system libraries — and calls `hb_lld_link_mingw`.

The Harbour runtime archive set that a console program needs (under `lib/win/mingw64/`, MinGW names `lib<name>.a`): `hbdebug hbvm hbrtl hblang hbcpage hbrdd hbmacro hbcommon hbpp gtwin gtcgi gtstd gtpca` plus the bundled 3rd-party `hbzlib hbpcre2` (names as built). The link must also pass the MinGW system libraries.

- [ ] **Step 1: Add `hb_llvmLinkExe` to `hb_llvmobj.c`**

Append to `src/compiler/hb_llvmobj.c`:

```c
/* Harbour runtime archives a console program links against, in dependency
 * order. Names are the base names; the .a is found under szLibDir. */
static const char * const s_hbRuntimeLibs[] = {
   "hbdebug", "hbvm", "hbrtl", "hblang", "hbcpage", "hbrdd",
   "hbmacro", "hbcommon", "hbpp", "gtstd", "hbzlib", "hbpcre2"
};

/* MinGW system libraries the Harbour runtime depends on. */
static const char * const s_sysLibs[] = {
   "ws2_32", "iphlpapi", "winspool", "user32", "gdi32",
   "advapi32", "ole32", "comdlg32", "comctl32", "uuid"
};

int hb_llvmLinkExe( const char * szObjPath, const char * szLibDir,
                    const char * szExePath )
{
   const char * argv[ 64 ];
   char libArg[ 512 ];
   int  argc = 0;
   unsigned i;
   int  rc;

   argv[ argc++ ] = "ld.lld";
   argv[ argc++ ] = "-o";
   argv[ argc++ ] = szExePath;
   argv[ argc++ ] = szObjPath;

   hb_snprintf( libArg, sizeof( libArg ), "-L%s", szLibDir );
   argv[ argc++ ] = hb_strdup( libArg );   /* freed by process exit */

   for( i = 0; i < sizeof( s_hbRuntimeLibs ) / sizeof( s_hbRuntimeLibs[ 0 ] ); ++i )
   {
      hb_snprintf( libArg, sizeof( libArg ), "-l%s", s_hbRuntimeLibs[ i ] );
      argv[ argc++ ] = hb_strdup( libArg );
   }
   for( i = 0; i < sizeof( s_sysLibs ) / sizeof( s_sysLibs[ 0 ] ); ++i )
   {
      hb_snprintf( libArg, sizeof( libArg ), "-l%s", s_sysLibs[ i ] );
      argv[ argc++ ] = hb_strdup( libArg );
   }

   rc = hb_lld_link_mingw( argc, argv );
   if( rc != 0 )
      fprintf( stderr, "harbour -GL: link failed (lld rc=%d)\n", rc );
   return rc;
}
```

Add `#include "hbapi.h"` near the top of `hb_llvmobj.c` for `hb_snprintf` / `hb_strdup` (or use `snprintf` / `strdup` from the C library if linking `hbapi.h` into this TU is awkward — pick one and be consistent).

- [ ] **Step 2: Write the link test**

`tests/llvm/linktest.c`:

```c
#include "hb_llvmobj.h"
#include <stdio.h>

int main( int argc, char ** argv )
{
   if( argc != 4 )
   {
      fprintf( stderr, "usage: linktest <in.o> <libdir> <out.exe>\n" );
      return 2;
   }
   return hb_llvmLinkExe( argv[ 1 ], argv[ 2 ], argv[ 3 ] );
}
```

- [ ] **Step 3: Build and run the full chain test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
LLVMINC=$(c:/llvm-mingw/bin/llvm-config --includedir)
LLVMLIBDIR=$(c:/llvm-mingw/bin/llvm-config --libdir)
bin/win/mingw64/harbour.exe -GL -q -obuild/lt_in tests/llvm/hello.prg
gcc -c src/compiler/hb_llvmobj.c -I src/compiler -I include -I "$LLVMINC" -o build/hb_llvmobj.o
g++ -c src/compiler/hb_lldshim.cpp -I src/compiler -I "$LLVMINC" -o build/hb_lldshim.o
gcc -c tests/llvm/linktest.c -I src/compiler -o build/linktest.o
g++ build/linktest.o build/hb_llvmobj.o build/hb_lldshim.o -o build/linktest.exe \
  -L "$LLVMLIBDIR" \
  -llldMinGW -llldCommon \
  $(c:/llvm-mingw/bin/llvm-config --libs core irreader x86codegen target option support) \
  -L lib/win/mingw64 -lhbcommon -lstdc++
# emit object, then link it
./build/objtest.exe build/lt_in.ll build/lt_in.o
./build/linktest.exe build/lt_in.o lib/win/mingw64 build/lt_hello.exe
./build/lt_hello.exe
```

Expected: `build/lt_hello.exe` is created and prints `Hello world`. If LLD reports unresolved symbols, adjust `s_hbRuntimeLibs` / `s_sysLibs` until the link resolves — compare against the link command `hbmk2 -trace` prints for a normal C-backend build of `hello.prg`.

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_llvmobj.c tests/llvm/linktest.c
git commit -m "compiler: add embedded LLD link driver"
```

---

### Task 5: Build-system integration

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\Makefile`
- Modify: `c:\HarbourLLVM\core\src\main\Makefile`

`harbour.exe` must compile the C++ shim, compile `hb_llvmobj.c`, and link the LLVM + LLD static libraries and `libstdc++`.

- [ ] **Step 1: Add the new sources to the compiler library**

In `src/compiler/Makefile`, add `hb_llvmobj.c` to `C_SOURCES` (after `genllvm.c`) and add a `CPP_SOURCES` entry for the shim. Add the LLVM include path. At the top, after `ROOT := ../../`:

```make
include $(TOP)$(ROOT)config/llvm-paths.mk
CFLAGS  += -I$(shell $(HB_LLVM_CONFIG) --includedir)
```

Add `hb_llvmobj.c` to the `C_SOURCES` list and `hb_lldshim.cpp` to the C++ source list (`CPP_SOURCES` — confirm the variable name the Harbour `config/lib.mk` uses for C++ files; if the build system has no C++ support, add an explicit rule that compiles `hb_lldshim.cpp` with `$(CXX)` and adds the object to the archive).

- [ ] **Step 2: Link the LLVM + LLD libraries into `harbour.exe`**

In `src/main/Makefile`, after `ROOT := ../../`:

```make
include $(TOP)$(ROOT)config/llvm-paths.mk
HB_USER_LDFLAGS += -L$(shell $(HB_LLVM_CONFIG) --libdir)
HB_USER_LIBS    += lldMinGW lldCommon \
   $(patsubst -l%,%,$(shell $(HB_LLVM_CONFIG) --libs core irreader x86codegen target option support)) \
   stdc++
```

(The `-L` for the LLVM lib dir and the LLVM/LLD library list are appended; `stdc++` pulls the C++ runtime needed by the shim and by LLVM.)

- [ ] **Step 3: Rebuild Harbour**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
```

Expected: `hb_lldshim.cpp` and `hb_llvmobj.c` compile, `harbour.exe` relinks with the LLVM + LLD libraries, no unresolved symbols. `harbour.exe` grows substantially in size (tens of MB).

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/Makefile src/main/Makefile
git commit -m "build: link embedded LLVM + LLD into harbour.exe"
```

---

### Task 6: Wire `-GL` to produce an executable, end-to-end

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`
- Test: `c:\HarbourLLVM\core\tests\llvm\hello.prg` (existing)

`harbour -GL foo.prg` must, after writing the IR, emit an object and link `foo.exe`. The intermediate `.ll` and `.o` are written next to the output and kept (useful for inspection).

- [ ] **Step 1: Call the embedded back end from `hb_compGenLLVMCode`**

In `genllvm.c`, add `#include "hb_llvmobj.h"`. At the end of `hb_compGenLLVMCode`, after the existing `fclose( yyc )` and before the "done" message, add:

```c
   /* Plan 2: drive the embedded back end — IR -> object -> executable. */
   {
      char szObj[ HB_PATH_MAX ];
      char szExe[ HB_PATH_MAX ];
      char szLibDir[ HB_PATH_MAX ];

      hb_strncpy( szObj, szFileName, sizeof( szObj ) - 1 );
      hb_strncpy( szExe, szFileName, sizeof( szExe ) - 1 );
      {  /* swap ".ll" for ".o" / ".exe" */
         char * pDotObj = strrchr( szObj, '.' );
         char * pDotExe = strrchr( szExe, '.' );
         if( pDotObj ) hb_strncpy( pDotObj, ".o",   sizeof( szObj ) - ( pDotObj - szObj ) - 1 );
         if( pDotExe ) hb_strncpy( pDotExe, ".exe", sizeof( szExe ) - ( pDotExe - szExe ) - 1 );
      }

      /* runtime archives ship next to harbour.exe: <harbour>/../../lib/<plat> */
      hb_llvmRuntimeLibDir( szLibDir, sizeof( szLibDir ) );

      if( hb_llvmEmitObject( szFileName, szObj ) == 0 )
      {
         if( hb_llvmLinkExe( szObj, szLibDir, szExe ) == 0 )
         {
            if( ! HB_COMP_PARAM->fQuiet )
               hb_compOutStd( HB_COMP_PARAM, "LLVM: executable created\n" );
         }
         else
            hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                             HB_COMP_ERR_CREATE_OUTPUT, szExe, NULL );
      }
      else
         hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                          HB_COMP_ERR_CREATE_OUTPUT, szObj, NULL );
   }
```

- [ ] **Step 2: Add `hb_llvmRuntimeLibDir` to `hb_llvmobj.c` / `.h`**

It resolves `lib/win/mingw64` relative to the running `harbour.exe`. Declare in `hb_llvmobj.h`:

```c
/* Fill szBuf with the absolute path of the Harbour runtime lib directory,
 * resolved relative to the running executable. */
void hb_llvmRuntimeLibDir( char * szBuf, int nBufLen );
```

Implement in `hb_llvmobj.c` using Harbour's own path API (`hb_vmExecutable()` returns the running exe path; the runtime libs are at `<exe_dir>/../../lib/<plat>/<comp>`). Use `hb_fsBaseDirBuff`/`hb_vmExecutable` from `hbapi.h`. Keep it small and copy the existing path-handling idiom from `src/compiler` or `src/common/hbfsapi.c`.

- [ ] **Step 3: Rebuild Harbour**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
```

Expected: clean build.

- [ ] **Step 4: End-to-end test with NO external C compiler on PATH**

```bash
cd c:/HarbourLLVM/core
# deliberately strip the MinGW toolchain from PATH — harbour must not need it
env PATH="/c/Windows/System32:/c/Windows" \
  bin/win/mingw64/harbour.exe -GL -q -obuild/e2e_hello tests/llvm/hello.prg
./build/e2e_hello.exe
```

Expected: `harbour.exe` (with no `gcc`/`clang` reachable) writes `build/e2e_hello.ll`, `build/e2e_hello.o`, and `build/e2e_hello.exe`; running `e2e_hello.exe` prints `Hello world`. This is the success criterion for Plan 2.

- [ ] **Step 5: Compare against the C backend**

```bash
cd c:/HarbourLLVM/core
./build/e2e_hello.exe > build/e2e_ll.out
bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/e2e_c tests/llvm/hello.prg
./build/e2e_c.exe > build/e2e_c.out
diff build/e2e_c.out build/e2e_ll.out
```

Expected: `diff` reports no differences.

- [ ] **Step 6: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c src/compiler/hb_llvmobj.c src/compiler/hb_llvmobj.h
git commit -m "compiler: -GL produces a native executable via embedded LLVM+LLD"
```

---

## Self-Review

**Spec coverage:** Plan 2 implements Spec 1's Plan 2 ("Embed libLLVM + lld; `harbour -GL` produces `.exe` directly, no external C compiler"). Task 1 builds the MinGW-ABI SDK (the spec's Risk 1); Tasks 2-4 are the embedded codegen+link; Tasks 5-6 wire it in and verify with a stripped PATH (the "no C compiler" criterion). The pcode interpreter is still used at runtime — removing it is Plan 3, out of scope here.

**Placeholder scan:** Task 1 is environment setup (build commands + verification), not TDD — this is appropriate and not a placeholder. Two points are intentionally left to the implementer's inspection rather than guessed: (a) the exact C++ source variable name in Harbour's `config/lib.mk` (Task 5 Step 1), and (b) the precise final `s_hbRuntimeLibs`/`s_sysLibs` set (Task 4 Step 3), which must be reconciled against `hbmk2 -trace` output for the real link. Both are anchored to a concrete verification command. `hb_llvmRuntimeLibDir` (Task 6 Step 2) is specified by contract and pointed at the existing path idiom to copy.

**Type consistency:** `hb_llvmEmitObject(const char*, const char*)`, `hb_llvmLinkExe(const char*, const char*, const char*)`, `hb_llvmRuntimeLibDir(char*, int)`, and `hb_lld_link_mingw(int, const char**)` have identical signatures in their headers, definitions, and call sites across Tasks 2-6.

**Known risks:** Task 1 (building LLVM+LLD from source, ~1-2h, disk/Python prerequisites) is the highest-risk task; if it fails the whole plan is blocked. The MinGW-vs-MSVC ABI constraint is the reason it cannot be skipped with a prebuilt SDK.
