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
| **1 — IR text emitter** | `harbour -GL` emits LLVM IR text (`.ll`) equivalent to the C backend; validated with clang. | **in progress** |
| 2 — Embed libLLVM + lld | `harbour -GL` produces an `.exe` directly — no external C compiler. | planned |
| 3 — Unroll pcode to IR | Exported runtime op shim; straight-line IR; no interpreter loop. | planned |

The full design and step-by-step plans live in
[`docs/superpowers/`](docs/superpowers/).

## Using the LLVM backend

```sh
harbour -GL hello.prg      # emit hello.ll (LLVM IR text)
```

`-GL` selects the `HB_LANG_LLVM` output language, alongside the existing `-GC`
(C) and `-GH` (portable object) backends.

For Plan 1 the emitted IR mirrors what the C backend produces: each function
becomes an LLVM function that hands its pcode to the Harbour VM, and the module
symbol table is registered through an `@llvm.global_ctors` constructor. The
program still uses the pcode interpreter at runtime — removing that is Plan 3.

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
