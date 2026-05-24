# Plan 2-macOS — In-process EXE generation on macOS via embedded LLD darwin driver

Date: 2026-05-24
Status: Approved design

## Goal

`harbour -GL foo.prg` produces a native Mach-O executable on macOS x86_64
**in-process**, with no external `clang` / `ld` / `hbmk2` on PATH — the
same Windows-MinGW promise of [Plan 2](2026-05-16-harbour-llvm-backend-design.md),
ported to macOS.

## Context

Plan 2 embedded libLLVM (C API) + LLD into `harbour.exe` so that
`harbour -GL` could turn LLVM IR into a native PE/COFF executable in a
single process. That work was scoped to Windows x86_64 / MinGW. On macOS
today, after the [Level-A Makefile portability patch](../../README.md#faq)
(commit `7a03e6911b`), standard Harbour builds cleanly and `harbour -GL`
emits the `.ll` text — but the `.ll → .o → .exe` in-process steps are
skipped because `g_hb_llvm_backend` is the NULL-stub from `hb_llvmstub.c`
(no `libhbllvm.a` built on macOS).

This spec extends Plan 2 to macOS x86_64 by adding a Mach-O-aware
`linkExe` path, an `lld::macho::link` shim, and host-appropriate
build-system wiring.

## Why macOS needs different work

LLD ships four front-end drivers (`coff`, `mingw`, `elf`, `macho`). They
differ in command-line syntax, in the system libraries they expect, and in
the output file format. The existing `hb_lld_link_mingw` in
`src/compiler/hb_lldshim.cpp` cannot produce a Mach-O executable — the
`lld::mingw::link` driver only writes PE/COFF.

Three concrete consequences:

- A new LLD driver registration is needed:
  `LLD_HAS_DRIVER( darwin )` plus a `hb_lld_link_macho` C wrapper.
- The argv shape for Mach-O is different: no `--start-group`/`--end-group`,
  needs `-arch x86_64`, `-platform_version macos N.N N.N`,
  `-syslibroot <SDK>`, `-lSystem`. Crt files live at
  `<SDK>/usr/lib/crt1.o`.
- `genllvm.c` currently emits a Windows target triple. On macOS the IR
  needs `x86_64-apple-darwin` so libLLVM produces Mach-O-compatible
  symbol mangling (underscore prefix on user symbols).

## Scope

**In scope (v1):**

- macOS x86_64 only — the iMac test target is x86_64.
- Use `xcrun --show-sdk-path` at run time to discover the macOS SDK
  (Xcode Command Line Tools — `xcode-select --install` — is a standard
  macOS dev install). Do NOT ship a `lib/darwin/clang-rt/` mirror of
  `lib/win/mingw64-rt/`; rely on the system SDK.
- Same Harbour runtime libraries the corpus path already uses
  (`lib/darwin/clang/libhb*.a`).

**Explicitly out of scope (v1) — each gets a follow-up spec if needed:**

- Apple Silicon (arm64) — needs a separate target triple, possibly
  Universal Binary support, and an arm64 macOS LLVM SDK build.
- Linux ELF (`lld::elf::link`) — same architecture, different driver and
  crt set.
- Automatic code-signing — Mach-O binaries on macOS work unsigned for
  local execution; if Gatekeeper friction surfaces on distribution,
  invoke `codesign --sign -` ad-hoc as a follow-up.
- Cross-compilation — host-target only.

## Architecture

Three components, each with a clear interface.

### Component 1 — LLD Mach-O shim in `src/compiler/hb_lldshim.cpp`

Mirror the existing `hb_lld_link_mingw`. Add:

```cpp
LLD_HAS_DRIVER( darwin )   /* declares lld::macho::link */

extern "C" int hb_lld_link_macho( int argc, const char ** argv )
{
   llvm::ArrayRef< const char * > args( argv, ( size_t ) argc );
   bool ok = lld::macho::link( args,
                                llvm::outs(), llvm::errs(),
                                false /* exitEarly */,
                                false /* disableOut */ );
   return ok ? 0 : 1;
}
```

Header `src/compiler/hb_lldshim.h` exports the C entry point.

### Component 2 — Per-host `linkExe` dispatch in `src/compiler/hb_llvmobj.c`

Wrap the existing MinGW argv construction in `#if defined( _WIN32 )` (or
better, `#if defined( __MINGW32__ )`). Add an `#elif defined( __APPLE__ )`
branch that:

1. Caches the SDK path via `popen( "xcrun --show-sdk-path", "r" )` on first
   call. Errors out cleanly if `xcrun` is absent (asks the user to install
   Xcode Command Line Tools).
2. Builds an `ld64.lld`-style argv:
   ```
   ld64.lld
   -arch x86_64
   -platform_version macos 13.0.0 13.0.0
   -syslibroot <SDK>
   -o foo
   <SDK>/usr/lib/crt1.o
   <bundled-or-from-make>/lib/darwin/clang/hb_llvmgtstd.o
   -u _HB_FUN_HB_GT_STD_DEFAULT
   foo.o
   -L <prefix>/lib/darwin/clang
   -lhbvm -lhbrtl -lhbcommon ... (the same Harbour runtime libs the corpus uses)
   -lSystem
   -lc++
   ```
3. Calls `hb_lld_link_macho( argc, argv )`.

Note the Mach-O symbol prefix: `--undefined HB_FUN_HB_GT_STD_DEFAULT`
becomes `-u _HB_FUN_HB_GT_STD_DEFAULT` (leading underscore is the Mach-O
convention; libLLVM's target triple handles user symbols, but undef
references on the linker command line are at the linker-symbol level so
the underscore must be written explicitly).

### Component 3 — Per-host target triple in `src/compiler/genllvm.c`

Currently the IR header is emitted without a `target triple` (or with a
hardcoded Windows triple). On macOS emit:

```llvm
target triple = "x86_64-apple-darwin"
```

Compile-time switch via `#if defined( __APPLE__ )` — selecting at compile
time matches the rest of Plan 2's design (the same `harbour.exe` binary
only ever produces output for the host it was built on).

Same gate suppresses any Windows-specific `target triple` line in non-
Windows builds.

## Build system changes

- `config/llvm-paths.mk`: `HB_HAS_LLVM` detection already in place (Level
  A). User installs LLVM 19.1.7 macOS x86_64 tarball from
  `https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/LLVM-19.1.7-macOS-X64.tar.xz`
  to `~/llvm-sdk/LLVM-19.1.7-macOS-X64/`, then sets
  `HB_LLVM_PREFIX=~/llvm-sdk/LLVM-19.1.7-macOS-X64` on the make command
  line. `llvm-config` becomes findable → `HB_HAS_LLVM=yes` →
  `src/llvmbe/` builds.
- `src/main/Makefile`: the `HB_LLVM_LIBS` line currently hardcodes
  `-llldMinGW -llldCOFF -llldCommon`. Detect the host:
  ```makefile
  ifeq ($(HB_PLATFORM),darwin)
     HB_LLVM_LIBS := -llldMachO -llldCommon -llldCore
  else
     HB_LLVM_LIBS := -llldMinGW -llldCOFF -llldCommon
  endif
  ```
  Skip the `-lstdc++` line on macOS (clang's `-lc++` is the macOS C++ ABI;
  often it is the default and need not be named). Skip `-Wl,--whole-archive`
  / `-Wl,--start-group` — macOS `ld64`/`lld64` use different mechanisms;
  the constructor that registers `g_hb_llvm_backend` runs from any
  archive member with `__attribute__((constructor))` on macOS without
  special linker flags because the macOS linker pulls in any object that
  has a non-weak init routine.
- `src/llvmbe/Makefile`: no per-platform change needed; the existing
  `HB_HAS_LLVM=yes` gate already handles building/skipping.
- `lib/darwin/clang/Makefile` (new or extend): build `hb_llvmgtstd.o` as
  a Mach-O object from the same source the Windows side uses (
  `lib/win/mingw64-rt/`'s source — promote to a shared location like
  `src/llvmbe/runtime/hb_llvmgtstd.c`).

## Testing

- New `tests/llvm/sequence.prg`-equivalent verification: extend
  `tests/llvm/run.sh` to detect host OS via `uname` and use the right
  `harbour -GL` invocation. On macOS with `HB_HAS_LLVM=yes`, the in-
  process path produces a Mach-O executable; without LLVM SDK installed,
  fall back to external `clang` link (same Linux CI behavior).
- Smoke: `harbour -GL hello.prg` on the iMac, then `./hello` must print
  `hello, world`.
- Full corpus: every program in `tests/llvm/*.prg` must produce output
  byte-identical to the C backend on the same machine. Same diff gate as
  Windows.
- CI: add a `macos-13-x64` matrix entry in
  `.github/workflows/llvm-ir.yml` that installs LLVM from Homebrew
  (or downloads the official tarball) and runs the existing corpus
  script.

## Correctness

- The shim calls the same `lld::macho::link` driver that Apple's modern
  `clang -fuse-ld=lld` would call — identical link behavior.
- Mach-O symbol mangling, init/term sections, and the `@llvm.global_ctors`
  → Mach-O `__mod_init_func` lowering are all handled by libLLVM when the
  target triple is `x86_64-apple-darwin` — no source changes elsewhere.
- The `g_hb_llvm_backend` dispatch table's existing NULL-check in
  `genllvm.c` already covers any build where the registration constructor
  did not run — on macOS, if `__attribute__((constructor))` doesn't pull
  the libhbllvm.a archive members, the dispatch stays NULL and the
  fallback (emit `.ll` only) kicks in. A safety net.

## Risks

1. **`-Wl,--whole-archive` equivalent on macOS.** macOS `ld64`/`lld64`
   pull archive members based on undefined-symbol references, not via
   group flags. If `g_hb_llvm_backend`'s registrar isn't pulled, the
   in-process EXE path silently no-ops. Mitigation: a forced reference
   in `harbour.c` (or in `hb_llvmstub.c`'s public symbol) to drag the
   real registrar in.
2. **macOS SDK path discovery.** `xcrun` adds a small startup cost
   (~10 ms) per `harbour -GL`. Cache the result. If `xcrun` is missing
   (no Xcode CLT), surface a clear error message pointing to
   `xcode-select --install`.
3. **Underscore symbol prefix.** Mach-O symbols are `_-prefixed`. The IR
   doesn't write `_`; libLLVM adds it during object emission when the
   target triple is `x86_64-apple-darwin`. But raw linker arguments
   (`-u`, `-undefined`) must include the underscore. Audit every
   linker-level symbol reference.
4. **macOS 13+ deployment target.** `-platform_version macos 13.0.0
   13.0.0` ties the produced binary to macOS 13+; older OSes won't run
   it. v1 ships hard-coded; a future enhancement reads `-mmacosx-version-min`
   from env or from `MACOSX_DEPLOYMENT_TARGET`.
5. **Universal Binary not supported.** Plan ships x86_64 only; the iMac
   is x86_64, the Apple Silicon path is a separate spec.

## Safety hatch

The Level-A patch already provides a clean fallback: if
`g_hb_llvm_backend` stays NULL (because libhbllvm.a wasn't built, or
because the macOS registrar didn't fire), `harbour -GL` emits the `.ll`
text only and exits cleanly. No behavior regression — same shape as
today's macOS build. Plan 2-macOS is purely additive.

## Out of scope (revisited, with notes for future specs)

- **Linux ELF port** — substantially parallel to this spec. Add
  `LLD_HAS_DRIVER( elf )` + `hb_lld_link_elf` + `__linux__` branch in
  `hb_llvmobj.c` + `crt1.o`/`crti.o`/`crtn.o` + `-dynamic-linker
  /lib64/ld-linux-x86-64.so.2` + `-lc`. The macOS work in this spec
  removes most of the structural barriers; Linux becomes a smaller
  follow-up.
- **Apple Silicon arm64** — requires `LLD_HAS_DRIVER` covers it via
  `darwin`, but needs an arm64 LLVM SDK build + arm64 target triple +
  potentially Universal Binary support. Separate spec.
