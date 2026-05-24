# Plan 2-macOS — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an in-process LLVM `.ll → .o → Mach-O .exe` path to `harbour -GL` on macOS x86_64, so the same Windows-MinGW behavior holds on macOS: one command, no external `clang`/`ld`/`hbmk2` on PATH, native executable in milliseconds.

**Architecture:** Three source-code changes (LLD darwin shim, host-OS dispatch in `linkExe`, per-host target triple) + two build-system changes (host-aware LLD lib selection in `src/main/Makefile`, build `hb_llvmgtstd.o` as Mach-O on macOS) + safety net (forced reference to drag the registrar constructor into the binary, since macOS `ld64` doesn't have a `--whole-archive` equivalent we can name uniformly).

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text, LLD Mach-O driver (C++), libLLVM C API (C).

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-24-plan-2-macos-design.md`.

**Prerequisites:** Plans 1-3, opcode groups A–I, Level-A Makefile portability (commit `7a03e6911b`). Verified Standard Harbour builds on macOS (commit verified on Antos-iMac.local, see memory `imac-local-network.md`).

**Cross-platform testing target:** iMac on local network — see memory `imac-local-network.md` for hostname, IP, user, access method. Requires user-supplied SSH password.

---

## Build environment

Windows host (where most source edits happen — Windows build still verifies the patches don't break the MinGW path):

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

macOS host (the iMac, for the new code path's actual verification):

```sh
# Once LLVM 19.1.7 macOS x86_64 tarball is extracted at ~/llvm-sdk/LLVM-19.1.7-macOS-X64/
export HB_LLVM_PREFIX="$HOME/llvm-sdk/LLVM-19.1.7-macOS-X64"
# (HB_HAS_LLVM auto-detects via $(wildcard $(HB_LLVM_CONFIG)) — yes when llvm-config is there.)
make
```

**Known on Windows:** `win-make` aborts at the pre-existing `harbour-32-x64.dll` link failure — irrelevant; static libs + `harbour.exe` build BEFORE that step. After editing `hvm.c` or `harbour.c`, `touch` the appropriate file before `./win-make.exe`. After editing `genllvm.c`, also `touch src/main/harbour.c` to force `harbour.exe` to relink.

**Shell note:** do not combine `cd` with `>` in one command — run a standalone `cd` first.

---

## Symbol-prefix reference

Mach-O symbols use a leading underscore convention. libLLVM auto-applies the underscore when the IR's `target triple` is `x86_64-apple-darwin`, so user-level symbols in the `.ll` (e.g. `@HB_FUN_MAIN`) get emitted as `_HB_FUN_MAIN` in the Mach-O object. But linker arguments (`-u`, `--undefined`) operate at the linker symbol level, so the underscore must be written explicitly on those flags.

| Layer | `HB_FUN_MAIN` notation |
|-------|-----------------------|
| IR (`.ll`) source | `@HB_FUN_MAIN` (no underscore — libLLVM adds it) |
| Mach-O object symbol | `_HB_FUN_MAIN` |
| `ld64.lld` `-u` flag | `-u _HB_FUN_MAIN` |
| PE/COFF (Windows, for comparison) | `HB_FUN_MAIN` (no underscore on x86_64 Windows) |

---

### Task 1: LLD Mach-O shim

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_lldshim.h`
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_lldshim.cpp`

- [ ] **Step 1: Read the existing shim**

Read `src/compiler/hb_lldshim.cpp` and `src/compiler/hb_lldshim.h` first. The MinGW shim is ~21 lines; the Mach-O shim mirrors its shape exactly, swapping `mingw` → `darwin` and `lld::mingw::link` → `lld::macho::link`.

- [ ] **Step 2: Add the Mach-O declaration to `hb_lldshim.h`**

After the existing `hb_lld_link_mingw` declaration, add:

```c
/* Link a Mach-O executable from the given argv. Returns 0 on success,
 * non-zero on failure. Only meaningful when LLD was built with the
 * macho driver, i.e. on macOS hosts. */
extern int hb_lld_link_macho( int argc, const char ** argv );
```

Guard nothing — the prototype is harmless on Windows; the function is only defined on macOS via the `#ifdef __APPLE__` block in the `.cpp`.

- [ ] **Step 3: Add the Mach-O shim body to `hb_lldshim.cpp`**

At the end of `hb_lldshim.cpp`, after `hb_lld_link_mingw`, add:

```cpp
#if defined( __APPLE__ )

LLD_HAS_DRIVER( macho )   /* declares lld::macho::link */

extern "C" int hb_lld_link_macho( int argc, const char ** argv )
{
   llvm::ArrayRef< const char * > args( argv, static_cast< size_t >( argc ) );

   bool ok = lld::macho::link( args,
                                llvm::outs(),
                                llvm::errs(),
                                /* exitEarly  */ false,
                                /* disableOut */ false );
   return ok ? 0 : 1;
}

#endif /* __APPLE__ */
```

The `#if defined( __APPLE__ )` guard keeps the file compiling on Windows where libLLD ships only with the mingw + coff drivers (no `lld::macho::link` symbol). The macro `LLD_HAS_DRIVER( macho )` declares the function and registers the driver; LLD requires this macro be invoked exactly once per driver per binary.

- [ ] **Step 4: Verify the Windows build is unchanged**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe 2>&1 | grep -E "lldshim|error:" | head -5
```

Expected: `hb_lldshim_wrap.cpp` (which `#include`s `hb_lldshim.cpp`) recompiles clean. No errors. The `__APPLE__` block is skipped by the preprocessor.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_lldshim.h src/compiler/hb_lldshim.cpp
git commit -m "compiler: add LLD Mach-O shim hb_lld_link_macho (Plan 2-macOS T1)

Mirrors the existing hb_lld_link_mingw byte-for-byte, swapping the
LLD driver from lld::mingw::link to lld::macho::link. Guarded by
#if defined(__APPLE__) so Windows builds (which ship LLD without the
macho driver) compile unchanged. The full Mach-O link argv is wired
in Task 2's hb_llvmobj.c host-OS dispatch.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Host-OS dispatch in `hb_llvmobj.c`

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_llvmobj.c`

- [ ] **Step 1: Read the existing MinGW dispatch**

Read `src/compiler/hb_llvmobj.c` lines 130-280 (the `linkExe` function + its argv construction + the final `hb_lld_link_mingw` call). Understand:
- How `s_hbRuntimeLibs[]` and `s_sysLibs[]` are iterated to build `-l<lib>` args.
- Where `szLibDir` and `szRtDir` come from.
- The exact `--start-group` / `--end-group` / `--whole-archive` flag layout.

- [ ] **Step 2: Add macOS SDK path discovery helper**

Above `linkExe` add a static helper that runs `xcrun --show-sdk-path` once and caches the result. Skipped entirely on non-Apple builds (guarded by `#if defined( __APPLE__ )`):

```c
#if defined( __APPLE__ )
#include <stdio.h>
#include <string.h>

/* Returns a pointer to a static buffer holding the macOS SDK path, or
 * NULL on failure. Cached after first successful call. */
static const char * hb_macos_sdk_path( void )
{
   static char s_sdkPath[ 4096 ];
   static int  s_initialised = 0;

   if( ! s_initialised )
   {
      FILE * fp = popen( "xcrun --show-sdk-path 2>/dev/null", "r" );
      if( fp )
      {
         if( fgets( s_sdkPath, sizeof( s_sdkPath ), fp ) != NULL )
         {
            size_t n = strlen( s_sdkPath );
            /* trim trailing newline */
            while( n > 0 && ( s_sdkPath[ n - 1 ] == '\n' ||
                              s_sdkPath[ n - 1 ] == '\r' ) )
               s_sdkPath[ --n ] = '\0';
         }
         pclose( fp );
      }
      s_initialised = 1;
   }
   return s_sdkPath[ 0 ] ? s_sdkPath : NULL;
}
#endif /* __APPLE__ */
```

- [ ] **Step 3: Add the Mach-O argv branch to `linkExe`**

Find the existing argv-construction block in `linkExe`. Currently it has a single path that builds the MinGW argv and calls `hb_lld_link_mingw`. Wrap that block in `#if defined( __MINGW32__ ) || defined( _WIN32 )` … `#elif defined( __APPLE__ )` … `#else` … `#endif`. The MinGW branch keeps the existing body verbatim. The macOS branch builds an `ld64.lld`-style argv:

```c
#elif defined( __APPLE__ )
   {
      const char * pszSdk = hb_macos_sdk_path();
      char szCrt1[ 4096 ];
      char szGtObj[ 4096 ];

      if( pszSdk == NULL )
      {
         fprintf( stderr, "harbour -GL: xcrun --show-sdk-path failed. "
                          "Install Xcode Command Line Tools:\n"
                          "  xcode-select --install\n" );
         return 1;
      }

      snprintf( szCrt1, sizeof( szCrt1 ), "%s/usr/lib/crt1.o", pszSdk );
      /* libdir's parent's sibling: lib/darwin/clang holds hb_llvmgtstd.o
       * when built by Task 4's lib/darwin/clang-rt/ Makefile addition. */
      snprintf( szGtObj, sizeof( szGtObj ), "%s/hb_llvmgtstd.o", szLibDir );

      argv[ argc++ ] = "ld64.lld";
      argv[ argc++ ] = "-arch";
      argv[ argc++ ] = "x86_64";
      argv[ argc++ ] = "-platform_version";
      argv[ argc++ ] = "macos";
      argv[ argc++ ] = "13.0.0";
      argv[ argc++ ] = "13.0.0";
      argv[ argc++ ] = "-syslibroot";
      argv[ argc++ ] = pszSdk;
      argv[ argc++ ] = "-o";
      argv[ argc++ ] = szExePath;
      argv[ argc++ ] = szCrt1;
      argv[ argc++ ] = szObjPath;
      argv[ argc++ ] = szGtObj;
      argv[ argc++ ] = "-u";
      argv[ argc++ ] = "_HB_FUN_HB_GT_STD_DEFAULT";   /* Mach-O underscore */

      PUSH_LDIR( szLibDir );   /* lib/darwin/clang (Harbour runtime) */

      /* No --start-group / --end-group on Mach-O — ld64 resolves
       * forward references via multi-pass scanning automatically. */
      {
         int i;
         char szLibArg[ 64 ];
         for( i = 0; i < ( int )( sizeof( s_hbRuntimeLibs ) /
                                  sizeof( s_hbRuntimeLibs[ 0 ] ) ); ++i )
         {
            snprintf( szLibArg, sizeof( szLibArg ), "-l%s",
                      s_hbRuntimeLibs[ i ] );
            argv[ argc++ ] = strdup( szLibArg );
         }
      }
      argv[ argc++ ] = "-lSystem";
      argv[ argc++ ] = "-lc++";

      rc = hb_lld_link_macho( argc, argv );
   }
#else
   #error "Plan 2 (in-process EXE) not supported on this platform yet."
#endif
```

Notes:
- `strdup` on the per-lib argv slot because `snprintf` writes into a local buffer that goes out of scope before `hb_lld_link_macho` reads it. The leak per invocation is negligible (one `harbour -GL` per process).
- `PUSH_LDIR` is the existing macro that appends `-L<dir>` to argv — reuse the same name.
- The `13.0.0 13.0.0` arguments are the minimum-and-SDK platform version. They tie the binary to macOS 13+; this matches the iMac (13.7.8) and the LLVM SDK build target. Read from `MACOSX_DEPLOYMENT_TARGET` env var as a future enhancement; v1 hard-codes.
- `s_hbRuntimeLibs[]` already exists at the top of the file with the right library names (`hbvm`, `hbrtl`, `hbcommon`, …) — verify those libs all exist in `lib/darwin/clang/` after a `make` on macOS.
- `s_sysLibs[]` is Windows-specific (`kernel32`, `user32`, `ws2_32`, …) — the macOS branch skips it. `-lSystem` covers libc, libpthread, libdl etc. on macOS.

- [ ] **Step 4: Skip the Mach-O argv compilation on Windows**

The new helper, the new branch, and the underscore-prefixed symbol references are all inside `#if defined( __APPLE__ )` blocks → on Windows preprocessor strips them and `hb_llvmobj.c` compiles unchanged. Verify:

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
touch src/compiler/hb_llvmobj.c
./win-make.exe 2>&1 | grep -E "hb_llvmobj|error:" | head -5
```

Expected: clean recompile, no errors.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_llvmobj.c
git commit -m "compiler: add macOS in-process Mach-O link path to linkExe (Plan 2-macOS T2)

Adds an #if defined(__APPLE__) branch that builds an ld64.lld-style
argv (-arch x86_64, -platform_version macos, -syslibroot via
xcrun, crt1.o, -lSystem, -lc++ + the Harbour runtime libs from
lib/darwin/clang/) and dispatches to hb_lld_link_macho (Task 1's
shim). xcrun SDK path cached. Mach-O symbol-underscore convention
applied to linker-level symbol references (-u _HB_FUN_HB_GT_STD_DEFAULT).
Windows MinGW branch unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Per-host target triple in `genllvm.c`

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Find the IR header emission**

Search `genllvm.c` for the place that emits the LLVM IR module header — likely around the `; Harbour LLVM IR - generated from` line near the top of `hb_compGenLLVMCode`. Check whether a `target triple` line is currently emitted. (Quick grep: `grep -n 'target triple' src/compiler/genllvm.c`.)

- [ ] **Step 2: Add a per-host target triple at the top of the IR**

Right after the module-name comment and BEFORE the `%HB_SYMB = type ...` line, add:

```c
#if defined( __APPLE__ )
   fprintf( yyc, "target triple = \"x86_64-apple-darwin\"\n" );
#endif
```

(Other platforms can be added later — the absence of a target triple line is fine for Windows where the existing build path works without it.)

- [ ] **Step 3: Verify Windows build is unchanged**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
# if 'harbour.exe up to date': touch src/main/harbour.c && ./win-make.exe
```

Then check a generated `.ll` on Windows does NOT contain a `target triple = "x86_64-apple-darwin"` line (it's gated by `__APPLE__`):

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
printf 'function Main()\n   ? "test"\n   return nil\n' > build/triple_check.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/triple_check build/triple_check.prg
grep -c 'target triple' build/triple_check.ll
```

Expected: `0` (Windows: no target triple line — same as before this change).

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit target triple x86_64-apple-darwin on macOS hosts (Plan 2-macOS T3)

Without an explicit target triple in the IR, libLLVM defaults to the
build host's triple — fine on Windows, broken on macOS where we
need libLLVM to apply the Mach-O symbol-underscore convention and
the macOS object file layout. #if defined(__APPLE__) gated so Windows
builds emit identical IR to before.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Build the `hb_llvmgtstd.o` Mach-O object on macOS

**Files:**
- Modify: `c:\HarbourLLVM\core\Makefile` (or wherever `lib/win/mingw64-rt/` is built — find via `grep -rn hb_llvmgtstd`)
- Possibly create: `c:\HarbourLLVM\core\lib\darwin\clang\Makefile` (a small Makefile to build the Mach-O `.o`)

- [ ] **Step 1: Locate the existing hb_llvmgtstd.c source + Windows build rule**

Find where `hb_llvmgtstd.o` (the GT std driver registration object) comes from on Windows. It's pre-shipped in `lib/win/mingw64-rt/hb_llvmgtstd.o`. The source `.c` file may live in `src/llvmbe/` or `src/main/` or be checked-in alongside the `.o`. Use:

```bash
cd c:/HarbourLLVM/core
grep -rn hb_llvmgtstd | head -10
find . -name 'hb_llvmgtstd*' -type f
```

Read the .c source. It should be a tiny file that registers `HB_FUN_HB_GT_STD_DEFAULT` (the standard terminal GT driver) so the linker pulls in the gtstd subsystem.

- [ ] **Step 2: Add a build rule that compiles hb_llvmgtstd.c to lib/darwin/clang/hb_llvmgtstd.o on macOS**

The cleanest place is to add a target to `src/llvmbe/Makefile` that runs only when `HB_PLATFORM=darwin` AND `HB_HAS_LLVM=yes`. The target compiles `hb_llvmgtstd.c` (locating the shared source) with the macOS clang into a Mach-O object placed at `lib/darwin/clang/hb_llvmgtstd.o`.

If the source currently lives only inside `lib/win/mingw64-rt/`, MOVE it (or copy) to a shared location like `src/llvmbe/runtime/hb_llvmgtstd.c` and update both Windows + macOS build rules to point at it.

A minimal Makefile snippet (in `src/llvmbe/Makefile` after the existing rules):

```makefile
ifeq ($(HB_PLATFORM),darwin)
ifeq ($(HB_HAS_LLVM),yes)

HB_LLVMGT_OBJ := $(TOP)$(ROOT)lib/darwin/clang/hb_llvmgtstd.o
HB_LLVMGT_SRC := $(TOP)$(ROOT)src/llvmbe/runtime/hb_llvmgtstd.c

all-local: $(HB_LLVMGT_OBJ)

$(HB_LLVMGT_OBJ): $(HB_LLVMGT_SRC)
	$(CC) -c -O2 -fno-common $(HB_USER_CFLAGS) -o $@ $<

endif
endif
```

Adapt to the project's Makefile conventions — read other rules in the same file first.

- [ ] **Step 3: Update `src/main/Makefile`'s `HB_LLVM_LIBS` per host**

Currently `HB_LLVM_LIBS := -llldMinGW -llldCOFF -llldCommon`. Add a host-OS switch:

```makefile
ifeq ($(HB_HAS_LLVM),yes)

ifeq ($(HB_PLATFORM),darwin)
   HB_LLVM_LIBS := -llldMachO -llldCommon -llldCore
else
   HB_LLVM_LIBS := -llldMinGW -llldCOFF -llldCommon
endif

HB_LLVM_LIBS += $(shell $(HB_LLVM_CONFIG) --libs all)
HB_LLVM_LIBDIR  := -L$(shell $(HB_LLVM_CONFIG) --libdir)
HB_LLVM_SYSLIBS := -lstdc++ $(shell $(HB_LLVM_CONFIG) --system-libs)

ifeq ($(HB_PLATFORM),darwin)
   # macOS uses libc++ by default; -lstdc++ is not the macOS C++ ABI lib
   HB_LLVM_SYSLIBS := -lc++ $(shell $(HB_LLVM_CONFIG) --system-libs)
   # No --whole-archive / --start-group on macOS ld64; let the linker
   # do multi-pass resolution. Force-pull the constructor with -u.
   LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(HB_LLVM_LIBDIR) -u _hb_llvmBackendRegister -lhbllvm $(LDLIBS) $(HB_LLVM_LIBS) $(HB_LLVM_SYSLIBS) $(LDSTRIP)
else
   LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(HB_LLVM_LIBDIR) -Wl,--start-group -Wl,--whole-archive -lhbllvm -Wl,--no-whole-archive $(LDLIBS) $(HB_LLVM_LIBS) -Wl,--end-group $(HB_LLVM_SYSLIBS) $(LDSTRIP)
endif

endif
```

The `-u _hb_llvmBackendRegister` on macOS forces `ld64` to pull `libhbllvm.a`'s registrar object even without `--whole-archive`. This is the macOS equivalent of dragging the `__attribute__((constructor))` translation unit in.

- [ ] **Step 4: Verify on Windows nothing regressed**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe 2>&1 | grep -E "harbour\.exe|error:" | head -5
```

Expected: `harbour.exe` relinks identically; the macOS Makefile branches are `ifeq ($(HB_PLATFORM),darwin)` and skipped on Windows.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/llvmbe/Makefile src/main/Makefile $(any-hb_llvmgtstd-source-move)
git commit -m "build: per-host LLD lib selection + macOS hb_llvmgtstd.o (Plan 2-macOS T4)

- src/main/Makefile: ifeq HB_PLATFORM darwin -> use -llldMachO instead
  of -llldMinGW/-llldCOFF, -lc++ instead of -lstdc++. macOS LD_RULE
  uses -u _hb_llvmBackendRegister to force-pull the constructor
  (no --whole-archive equivalent on ld64).
- src/llvmbe/Makefile: build lib/darwin/clang/hb_llvmgtstd.o from
  shared C source on macOS hosts.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: macOS verification on the iMac

**Prerequisites for this task:**
- iMac on the local network, accessible via SSH (see memory `imac-local-network.md`: hostname `Antos-iMac.local`, IP `192.168.18.184`, user `Anto`, password user-supplied).
- LLVM 19.1.7 macOS x86_64 tarball extracted to `~/llvm-sdk/LLVM-19.1.7-macOS-X64/` on the iMac. Download:
  ```sh
  mkdir -p ~/llvm-sdk && cd ~/llvm-sdk
  curl -fSL https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/LLVM-19.1.7-macOS-X64.tar.xz -o llvm.tar.xz
  tar xf llvm.tar.xz && rm llvm.tar.xz
  ~/llvm-sdk/LLVM-19.1.7-macOS-X64/bin/llvm-config --version    # 19.1.7
  ```

- [ ] **Step 1: Pull the Task 1-4 commits + rebuild on the iMac**

Via SSH (use paramiko from the Windows host — see `imac-local-network.md` for the snippet):

```sh
cd ~/HarbourLLVM
git pull --rebase --quiet
make clean
HB_LLVM_PREFIX=$HOME/llvm-sdk/LLVM-19.1.7-macOS-X64 make
```

Expected: `make` builds `libhbllvm.a` this time (`HB_HAS_LLVM` detects to `yes` because llvm-config is now present). `harbour` binary relinks with `libhbllvm.a` embedded.

- [ ] **Step 2: Smoke `harbour -GL hello.prg` end-to-end**

```sh
cd /tmp
cat > hello.prg <<'EOF'
function Main()
   ? "hello, world"
   return nil
EOF
~/HarbourLLVM/bin/darwin/clang/harbour -GL hello.prg
ls -la hello.*
./hello
```

Expected: `hello.prg`, `hello.ll`, `hello.o`, `hello` (Mach-O 64-bit executable) — `./hello` prints `hello, world`.

If any of those are missing OR if `harbour -GL` says "LLVM IR output done" without producing `.o`/`hello`, the registrar didn't fire — diagnose via `nm -gU ~/HarbourLLVM/bin/darwin/clang/harbour | grep hb_llvmBackendRegister` (must show a `T` symbol, not be missing).

- [ ] **Step 3: Run the full corpus via run.sh in macOS mode**

```sh
cd ~/HarbourLLVM
CLANG=$(xcrun -find clang) tests/llvm/run.sh build/llvm-macos
```

Expected: `RESULT: all programs validated and matched the C backend`. Same 26 corpus programs, all diff-vs-C-backend empty.

- [ ] **Step 4: Commit any iMac-specific runtime adjustments + push**

If the corpus exposed a Mach-O-specific bug (e.g. a missing symbol prefix in some emit case), fix it, repeat Steps 2-3 until green. Commit each fix individually.

```sh
git add <files>
git commit -m "compiler: <fix description> (Plan 2-macOS T5)"
git push origin master
```

- [ ] **Step 5: Update README + GitHub Pages footer to mention macOS support**

In `README.md`:
- Status table: add a row for "Plan 2-macOS" with **done** state.
- Quickstart: mention macOS works too.
- FAQ: update "Does `harbour -GL` produce a `.exe` directly?" to mention macOS in addition to Windows.

In `tests/llvm/run.sh` (HTML template):
- Hero / stats / footer: refer to "Plans 1-3 + opcode groups A-I + Plan 2-macOS" or similar.

```sh
git add README.md tests/llvm/run.sh
git commit -m "docs: mark Plan 2-macOS done in README + landing page (Plan 2-macOS T5)"
git push origin master
```

---

## Self-Review

**Spec coverage:** The spec's three components are covered — T1 the LLD darwin shim, T2 the host-OS dispatch in `linkExe` + SDK path discovery, T3 the per-host target triple in `genllvm.c`. The spec's build-system section is T4. The testing section is T5. The risks the spec called out are addressed: Risk 1 (no `--whole-archive` on ld64) by `-u _hb_llvmBackendRegister` in T4 Step 3; Risk 2 (xcrun cost) by caching in T2 Step 2; Risk 3 (underscore prefix) by writing it explicitly in T2 Step 3 and at any other linker-level reference; Risk 4 (deployment target) by hard-coding `13.0.0 13.0.0` and noting the env-var enhancement as future; Risk 5 (no Universal Binary) by scoping to x86_64 only.

**Placeholder scan:** The shim body, the helper function, the argv branch, the Makefile snippets, and the test commands are all shown in full. T4 Step 1 has a "locate the source" sub-step which is inherently discovery — the implementer searches and reports back; no synthesised path. T5 Step 4 says "fix whatever surfaces" — that's by design, because Mach-O runtime issues can't all be predicted in advance.

**Type consistency:** The shim's signature `hb_lld_link_macho(int, const char **)` is identical to `hb_lld_link_mingw`. The argv slots in T2 are `const char *` (matching the existing pattern). The Makefile variables follow the existing `HB_LLVM_*` naming.

**Known risks not yet eliminated:**

1. The `-platform_version macos 13.0.0 13.0.0` is hard-coded. macOS < 13 binaries built with this will fail to run on older OSes; a follow-up should read `MACOSX_DEPLOYMENT_TARGET` from env.
2. `s_hbRuntimeLibs[]` library list assumes the macOS lib names match Windows (`hbvm`, `hbrtl`, etc.) — verify in T5 Step 1; the `lib/darwin/clang/` directory after `make` shows the actual names.
3. The macOS LLD `-llldCore` library name is correct as of LLVM 19; if 20+ renames, the Makefile needs an update. v1 pins to LLVM 19.1.7.
4. T4 Step 2's `hb_llvmgtstd.c` source-location decision — if the source already lives outside `lib/win/mingw64-rt/` (e.g. it's generated), the move-or-copy step needs re-thinking. The implementer's discovery step decides.
