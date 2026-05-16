# Harbour LLVM Backend — Spec 1: Foundation + Codegen Core

Date: 2026-05-16
Status: Approved design

## Goal

Make Harbour an LLVM frontend, like Objective-C is to clang: parse `.prg`,
emit LLVM IR directly, produce a native `.exe` with **no external C compiler**
required by the end user.

End-user package = `harbour.exe` + precompiled runtime libs + headers.
No gcc / clang / msvc installation needed to build applications.

This document covers **Spec 1** only. The full effort is decomposed into four
sub-projects (each gets its own spec → plan → implementation cycle):

1. **Foundation + Codegen Core** — this spec.
2. Type specialization / optimization (the 5-50x speedup).
3. xBase edge cases — macros `&`, codeblocks, RDD, OOP, runtime eval, GC.
4. Additional target platforms beyond Windows x64.

## Scope of Spec 1

### Deliverable

`harbour -llvm hello.prg` produces a runnable `hello.exe` with **no
gcc/clang/msvc on PATH**. Behaviour identical to current compiled mode, with
**no pcode interpretation loop** in the produced executable.

### In scope

Language subset: locals, parameters, arithmetic, strings, conditionals,
loops, function calls, return values.

### Out of scope (deferred)

- Macros `&`, codeblocks, RDD / databases, OOP — Spec 3.
- Type-specialized optimization / 5-50x speedup — Spec 2. Spec 1 only removes
  the interpreter dispatch; speedup is moderate.
- Target platforms other than Windows x64 (validated first; others are config) — Spec 4.

## Architecture

```
harbour.exe  (statically links libLLVM + lld libraries)
  │
  ├─ existing frontend: .prg → in-memory pcode  (src/compiler/, UNCHANGED)
  │
  ├─ src/llvm/  (rewritten — old llvm_*.c scaffolding discarded):
  │    hbllvmgen.c    pcode → LLVM IR module
  │    hbllvmemit.c   IR module → object file (TargetMachine)
  │    hbllvmlink.c   object + runtime libs → .exe (via lld library)
  │    hbllvm.h       types, public API
  │
  └─ lib/<triple>/    precompiled runtime static libs
                      (hbvm, hbrtl, hbmacro, hbpp, hbcommon, hbrdd,
                       hblang, hbcpage, gt*)
```

### Decisions

- **Codegen API:** LLVM C API (`llvm-c/`) — stable, already used by prior
  scaffolding. Drop to C++ only where the C API lacks a needed entry point.
- **Linker:** `lld` linked as a **library** (`lldCOFF` / `lldELF` /
  `lldMachO`), embedded in `harbour.exe`. No external `.exe`. (lld is a
  linker, not a C compiler.) Fallback: bundle `lld.exe` if the library API
  proves impractical — still satisfies "no C compiler".
- The existing `src/llvm/llvm_*.c` scaffolding (~1167 lines, non-functional)
  is discarded and rewritten.
- The `-llvm` command-line flag already exists in
  `src/compiler/cmdcheck.c` and is reused.

## pcode → IR Mapping

Today `harbour foo.prg` emits `foo.c` containing pcode as a **byte array**
plus a symbol table; at runtime `hb_vmExecute()` walks the array through a
large `switch` and **interprets** it.

Spec 1 **unrolls** that `switch` into straight-line IR. Each PRG function
becomes an LLVM function `void NAME(void)` using the Harbour calling
convention (arguments via the VM stack, exactly as `HB_FUNC` does in C mode).

| pcode                          | IR emitted                                  |
|--------------------------------|---------------------------------------------|
| `HB_P_PUSHLOCAL n`             | `call void @hb_vmPushLocal(i32 n)`           |
| `HB_P_PUSHINTEGER`             | `call void @hb_vmPushInteger(...)`           |
| `HB_P_PLUS` / `MINUS` / `MULT` | `call void @hb_vmPlus()` etc.                |
| `HB_P_FUNCTION n`              | `call void @hb_vmFunction(i16 n)`            |
| `HB_P_JUMP*`                   | basic block + `br`                           |
| `HB_P_DO` / `SEND`             | corresponding runtime `call`                 |
| `HB_P_RETVALUE` / `ENDPROC`    | `ret void`                                   |

Key point: the produced `.exe` contains **no pcode array and no interpreter
loop**. Operations remain runtime calls (not yet inlined — that is Spec 2),
but the dispatch overhead is gone, giving a moderate speedup and the base for
later optimization.

**Symbol table** (`HB_INIT_SYMBOLS`): emitted as an IR global plus a
constructor function calling `hb_vmProcessSymbols()`. `main` emits the startup
sequence (`hb_vmInit()` etc.).

**Control flow:** a basic-block graph is built from the pcode jump offsets —
first pass marks block leaders, second pass emits blocks and branches.

**Opcode coverage:** Spec 1 covers the ~60-80 opcodes of the functional
subset. Any unsupported opcode raises an explicit error
("opcode not yet supported") — never a silent failure or corrupt `.exe`.

## Build and Runtime Libraries

### Building harbour.exe (once, with a C++ toolchain — permitted)

Requires static LLVM + LLD libraries. Two paths:

- **Prebuilt:** an LLVM release that ships static libs, if available.
- **From source:** `cmake` LLVM with `LLVM_ENABLE_PROJECTS=lld`, static
  build. Slow (~1h) but done once. This is the documented reproducible path.

Resulting `harbour.exe` is ~80-150 MB (static LLVM). Accepted.

### Precompiled runtime libraries

The standard Harbour `make` build already produces `hbvm.lib`, `hbrtl.lib`,
etc. They are copied into `lib/<triple>/` and versioned with the
distribution. `harbour.exe` + `lib/` + headers = the end-user package; no
compiler installation needed.

### Target selection

`-llvm` uses the host triple by default; `-target=<triple>` for cross
compilation. Each triple needs its own precompiled `lib/<triple>/`.
Windows x64 first.

### End-to-end driver

`harbour -llvm hello.prg` → IR → temporary object → lld links against
`lib/<triple>/*.lib` → `hello.exe`. No manual steps.

## Testing

- **Feature corpus:** small `.prg` files per feature (locals, arithmetic,
  strings, loops, calls). Each: compile `-llvm`, run, compare output against
  the standard C build (reference oracle).
- **"No compiler" test:** clean PATH with no gcc/clang/msvc → `harbour -llvm`
  must still produce a `.exe` that runs.
- **Regression:** unsupported opcode → explicit error, never a corrupt `.exe`.
- CI: build `harbour-llvm`, run the corpus.

## Risks (by impact)

1. **Building static LLVM + LLD on Windows** — most likely blocker.
   Mitigation: document a reproducible cmake build; use a prebuilt with
   static libs if one exists.
2. **Harbour calling convention** — `HB_FUNC` bodies depend on VM-stack
   macros. The emitted LLVM function must replicate the prologue/epilogue
   exactly (`hb_vmStackBase` etc.). Mitigation: use the currently generated
   `.c` as a 1:1 template.
3. **Opcode coverage** — ~200 opcodes total. Spec 1 covers the subset; the
   rest raise a clear error.
4. **Symbol table / init order** — `HB_INIT_SYMBOLS` plus constructors.
   Mitigation: copy the pattern from the generated `.c`.
5. **lld as a library** — less documented than the CLI. Fallback: bundle
   `lld.exe` (still no C compiler).

## Success Criteria

- Full feature corpus passes.
- Clean PATH with no C compiler still produces a runnable `.exe`.
- No interpreter dispatch loop in the produced executable.

## Addendum (2026-05-16) — findings from codebase exploration

Two facts found while writing the implementation plan refine this spec:

1. **Generated C does not translate pcode to C.** `genc.c` emits each
   function as a static pcode byte array plus a `hb_vmExecute(pcode,
   symbols)` call — the compiled `.exe` interprets pcode at runtime.
   Confirms the analysis above.

2. **The per-opcode runtime functions (`hb_vmPlus`, `hb_vmEqual`, ...) are
   `static` in `src/vm/hvm.c`** — not exported, not linkable. Unrolling the
   interpreter switch into straight-line IR calls (Risk 2) therefore
   requires either exporting them or adding a thin exported runtime shim.
   This is a runtime-side change, compiled once with C.

Consequently Spec 1 is implemented as **three sequential plans**, each
producing working, testable software:

- **Plan 1 — LLVM IR text emitter.** `harbour -GL` emits `.ll` equivalent
  to the C backend; validated with an existing `clang`. Still interprets
  pcode. No libLLVM dependency.
- **Plan 2 — Embed libLLVM + lld.** `harbour -GL` produces a `.exe`
  directly; no external C compiler. Meets the "no C compiler" criterion.
- **Plan 3 — Unroll pcode to IR.** Add the exported runtime op shim, emit
  straight-line IR, remove the interpreter loop.
