# Harbour LLVM Backend

A fork of [Harbour](https://github.com/harbour/core) — the free, multi-platform
Clipper/xBase compiler — that adds an **LLVM backend**.

The goal: make Harbour an LLVM frontend, the way Objective-C is a frontend to
clang. The compiler parses `.prg` source, emits **LLVM IR directly**, and
produces a native executable — so an end user can build xBase applications
**without installing a separate C compiler**.

> This repository tracks `harbour/core`. The upstream Harbour README is kept as
> [`README.harbour.md`](README.harbour.md).

## Why

Standard Harbour compiles `.prg` → `.c`, then hands the C off to an external
compiler (gcc / MSVC / clang) and a linker. The C compiler is a required, heavy
dependency for every developer.

Routing through LLVM removes that dependency: `harbour` itself becomes the code
generator. Just as `clang` compiles Objective-C straight to machine code and
links against the precompiled `libobjc` runtime, `harbour` compiles xBase
straight to a native binary and links against the precompiled Harbour runtime
(`hbvm`, `hbrtl`, …).

## Status

| Plan | Deliverable | State |
|------|-------------|-------|
| 1 — IR text emitter | `harbour -GL` emits LLVM IR text (`.ll`) equivalent to the C backend; validated with clang. | **done** |
| 2 — Embed libLLVM + lld | `harbour -GL` produces an `.exe` directly — no external C compiler. | **done** |
| 3 — Unroll pcode to IR | Exported runtime op shim; straight-line IR; no interpreter loop. | **done** |

Plan 2 (Windows x86_64 / MinGW): `harbour.exe` embeds the libLLVM C API to
turn its IR into a native object file and embeds the LLD linker (via a small
C++ shim) to link it — using MinGW runtime objects bundled in
`lib/win/mingw64-rt/`. `harbour -GL foo.prg` produces a runnable `foo.exe`
with **no external C compiler or linker** on `PATH`, output identical to the C
backend. LLVM lives in a side library (`libhbllvm.a`) embedded only into
`harbour.exe`, so `hbmk2` / `hbrun` stay small.

Plan 3: for functions within a supported pcode subset (locals, arithmetic,
comparisons, logical ops, jumps, function calls, return), `harbour -GL` now
emits **straight-line native code** — one LLVM basic block per pcode opcode,
each calling an exported `hb_vmsh_*` op shim — instead of handing the pcode
array to the `hb_vmExecute` bytecode interpreter. Functions using opcodes
outside the subset (codeblocks, `FOR` loops, RDD ops, …) fall back,
whole-function, to the interpreter, so every program stays correct. This
removes the dispatch overhead; type specialization (the larger speedup) is
possible future work.

The full design and step-by-step plans live in
[`docs/superpowers/`](docs/superpowers/).

## Using the LLVM backend

```sh
harbour -GL hello.prg      # -> hello.ll, hello.o, and hello.exe
```

`-GL` selects the `HB_LANG_LLVM` output language, alongside the existing `-GC`
(C) and `-GH` (portable object) backends. On a Harbour built with the embedded
backend, `-GL` writes the LLVM IR, compiles it to a native object, and links
`hello.exe` — all in-process, no external toolchain. The intermediate `.ll`
and `.o` are kept for inspection.

A function within the supported pcode subset is emitted as straight-line IR —
one basic block per opcode, calling the `hb_vmsh_*` runtime op shim directly,
with no bytecode dispatch. A function using an opcode outside the subset falls
back to an LLVM function that hands its pcode to the `hb_vmExecute`
interpreter. The module symbol table is registered, in both cases, through an
`@llvm.global_ctors` constructor.

## How it is verified

[`tests/llvm/run.sh`](tests/llvm/run.sh) compiles every program in
`tests/llvm/*.prg` with both backends, runs the LLVM verifier on the emitted IR,
links the IR into a native executable, and diffs its output against the C
backend. It also writes an HTML report containing the generated IR.

A GitHub Actions workflow ([`.github/workflows/llvm-ir.yml`](.github/workflows/llvm-ir.yml))
runs this on every push and **publishes the report — including the generated
LLVM IR — to GitHub Pages**:

➡ **https://fivetechsoft.github.io/harbourllvm/**

So the intermediate-code generation can be reviewed in a browser, no local
build required.

## Building

Harbour builds with a standard C toolchain (this is needed to build *Harbour
itself*, not to use the LLVM backend afterwards).

```sh
# Linux / macOS
make

# Windows (MinGW)
set HB_COMPILER=mingw64
win-make.exe
```

Run the LLVM backend checks locally:

```sh
tests/llvm/run.sh
```

## License

Same as Harbour — GPL-compatible with the Harbour exception. See
[`LICENSE.txt`](LICENSE.txt).
