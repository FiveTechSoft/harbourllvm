# Harbour LLVM Backend

**An [xBase](https://en.wikipedia.org/wiki/XBase) compiler that emits LLVM IR
directly and links a native executable in-process — no external C/C++
toolchain on PATH, no second-stage build.**

[![GitHub Pages report](https://img.shields.io/badge/report-fivetechsoft.github.io%2Fharbourllvm-58a6ff?style=flat-square)](https://fivetechsoft.github.io/harbourllvm/)
[![License](https://img.shields.io/badge/license-GPL%20%2B%20Harbour%20exception-2ea043?style=flat-square)](LICENSE.txt)
[![Status](https://img.shields.io/badge/scope-complete-3fb950?style=flat-square)](#status)
[![Upstream](https://img.shields.io/badge/upstream-harbour%2Fcore-8b949e?style=flat-square)](https://github.com/harbour/core)

A fork of [Harbour](https://github.com/harbour/core) — the free,
multi-platform Clipper/xBase compiler — turning `harbour` itself into the
code generator. Just as `clang` compiles Objective-C straight to machine
code and links against the precompiled `libobjc` runtime, this `harbour`
compiles `.prg` straight to a native binary and links against the
precompiled Harbour runtime (`hbvm`, `hbrtl`, …).

> The upstream Harbour README is preserved as
> [`README.harbour.md`](README.harbour.md).

---

## Quickstart

```sh
git clone https://github.com/FiveTechSoft/harbourllvm
cd harbourllvm/core
make                                          # or: win-make.exe on Windows (MinGW)

cat > hello.prg <<'EOF'
function Main()
   ? "hello, world"
   return nil
EOF

./bin/<platform>/harbour -GL hello.prg        # writes hello.ll, hello.o, hello.exe
./hello
# hello, world
```

The intermediate `.ll` and `.o` are kept next to the binary — open them to
see the LLVM IR.

---

## What it does

- Parses xBase `.prg` source like the standard `harbour -GC` does.
- **Emits LLVM IR text** (`.ll`) instead of C — selected via `-GL`.
- Embeds **libLLVM (C API)** to turn that IR into a native object file in
  the same process.
- Embeds **LLD** (via a small C++ shim) to link the executable — also
  in-process, no external linker on PATH.
- For every function whose pcode lies entirely inside the supported subset,
  emits **straight-line native code**: one LLVM basic block per pcode
  opcode, each calling an exported `hb_vmsh_*` runtime op shim — no
  interpreter dispatch loop.
- Functions using opcodes outside the subset fall back, whole-function, to
  the `hb_vmExecute` interpreter — **correctness is always preserved**.

## What you achieve

- **Zero external toolchain.** No gcc / MSVC / clang / lld required on the
  end-user's machine. One self-contained `harbour.exe`.
- **100% Harbour-compatible.** The straight-line shims call the same
  `hb_vm*` helpers the interpreter uses; every corpus program is
  diff-checked byte-for-byte against the standard C-backend output.
- **Nine opcode groups straight-lined** (see [Status](#status)).
- **Permanent fallback safety net.** A small set of intentionally-unsupported
  opcodes (timestamp / date literals, `$` substring, static-variable frames,
  var-arg frames, hidden strings, …) keep working through the interpreter —
  programs using them always build and always run, just without
  straight-lining.
- **Open to future type specialization.** Removing the dispatch loop is the
  first half of the win; specializing local int/double slots is a natural
  next step.

---

## Status

> **All planned scope complete** — Plans 1-3 + opcode groups A–I. Latest run
> published continuously at
> **<https://fivetechsoft.github.io/harbourllvm/>**.

### Foundation

| # | Deliverable | State |
|---|-------------|-------|
| **1** — IR text emitter | `harbour -GL` emits LLVM IR text (`.ll`) equivalent to the C backend; validated with clang. | ✅ done |
| **2** — Embed libLLVM + LLD | `harbour -GL` produces an `.exe` directly — no external C/C++ toolchain on PATH. | ✅ done |
| **3** — Unroll pcode to IR | Exported runtime op shims; straight-line IR; no interpreter dispatch loop. | ✅ done |

### Opcode-group extensions

| Group | Covers | State |
|-------|--------|-------|
| **A** — FOR loops + compound assignment | `FOR..NEXT` / `FOR..STEP`, `+=`/`-=`/`*=`/`/=`/`%=`/`^=`/`++`/`--`. | ✅ done |
| **B** — Arrays + hashes | Array/hash literals, element access/assignment, `Array()`, `LOCAL a[m,n]`. | ✅ done |
| **C** — RDD fields, memvars, aliases | Database-field access, memvars, undeclared variables, aliased access, workarea selection. | ✅ done |
| **D** — OOP messages | `oObj:method()`, `Self`, object-variable references, `WITH OBJECT`. | ✅ done |
| **E** — FOR EACH | `FOR EACH ... NEXT` and `DESCEND` over arrays, hashes, strings. | ✅ done |
| **F** — SWITCH | `SWITCH` lowered to a real LLVM `switch` over the matched case index. | ✅ done |
| **G** — Codeblocks | Codeblock literal construction `{|args| ...}`; block bodies still run through `hb_vmExecute` on `Eval()` — identical to the C backend. | ✅ done |
| **H** — Macros | 14 compiler-emitted macro opcodes: `&var`, `&("expr")`, `text…endtext`, `@&var`, aliased `M->&fld`. | ✅ done |
| **I** — SEQUENCE | `BEGIN SEQUENCE … RECOVER … END` (try/catch) and `… ALWAYS … END` (try/finally) via compile-time region tracking + region-aware per-shim dispatch. | ✅ done |

Spec + plan for every unit lives in
[`docs/superpowers/`](docs/superpowers/) — one spec, one plan, executed
task-by-task with two-stage code review per task.

---

## Architecture in one paragraph

`harbour -GL` runs the full Harbour compiler pipeline up to pcode generation
unchanged, then dispatches to the LLVM backend. For each function the
backend asks: *does every opcode lie inside the A–I subset?* If yes, it
emits the function as **straight-line IR** — one basic block per opcode,
each ending in a `call i32 @hb_vmsh_*` to the runtime op shim, followed by
the standard action-request check (`icmp ne` + conditional `br` to
`%epilogue` on non-zero). Inside an active `BEGIN SEQUENCE` region, that
check becomes a 3-way dispatch (`HB_BREAK_REQUESTED` → matching RECOVER,
`HB_QUIT_REQUESTED` → matching ALWAYS, else → propagate). If any opcode is
outside the subset, the function's body is just one call to
`hb_vmExecute(@.pcode.<func>, %symbols)` — fallback. Either way the module
symbol table is registered through an `@llvm.global_ctors` constructor, so
the runtime sees the same symbols whether straight-line or interpreted.

---

## FAQ

### Does `harbour -GL` produce a `.exe` directly?

Yes — in one command, in-process, with **no external C/C++ compiler or linker
required on PATH**. Verified:

```sh
$ ls hello.*
hello.prg                                 # source
$ harbour -GL -ohello hello.prg
$ ls hello.*
hello.prg  hello.ll  hello.o  hello.exe   # source + IR + object + binary
$ ./hello
hello, world
```

The intermediate `.ll` and `.o` are kept next to the binary for inspection.

### Is the `.exe` a pre-built template that gets patched, or freshly compiled each time?

**Freshly compiled and linked every time.** Each invocation produces a unique
binary (verifiable via `sha1sum`) — the IR is generated fresh from the
source, the object is compiled fresh, the executable is linked fresh. There
is no "template + patch" shortcut.

Two different programs produce different IRs of different sizes, different
objects of different sizes, and different binaries with different SHA1s. The
~1.3 MB common floor is the Harbour runtime statically linked in — the
variable delta on top is the program's own native code.

### Who actually does the linking?

`harbour.exe` itself, via embedded LLD. The `src/llvmbe/` directory builds
`libhbllvm.a`, which contains:

- **libLLVM** (C API) — turns the IR into a native object file.
- **LLD** — the LLVM linker, wrapped by a tiny C++ shim
  (`src/compiler/hb_lldshim.cpp`) so C code can call `lld::mingw::link`
  in-process.

When `harbour -GL` runs, after emitting the `.ll` it calls into
`libhbllvm.a`'s entry points (`emitObject` then `linkExe`) — no process
fork, no external tool, no shell-out.

`libhbllvm.a` is linked **only into `harbour.exe`** via a dispatch table
(`g_hb_llvm_backend` plus a `__attribute__((constructor))` registrar), so
`hbmk2` and `hbrun` stay small — they don't drag in LLVM/LLD.

### How are libraries linked into the final executable?

`harbour.exe` builds a `ld.lld`-style argv in `src/compiler/hb_llvmobj.c`
and hands it to LLD. The argv references three sets of libraries, all
shipped with Harbour:

| Set | Location | Contents |
|-----|----------|----------|
| **Bundled MinGW runtime** | `lib/win/mingw64-rt/` | `crt2.o`, `crtbegin.o`, `crtend.o`, `libgcc.a`, `libgcc_eh.a`, `libmingw32.a`, `libmingwex.a`, `libmoldname.a`, `libmsvcrt.a`, plus all Win32 import libs (`libkernel32.a`, `libuser32.a`, `libws2_32.a`, …) |
| **Harbour runtime** | `lib/win/mingw64/` | `libhbvm.a`, `libhbrtl.a`, `libhbcommon.a`, `libhblang.a`, `libhbcpage.a`, … |
| **GT driver registration** | `lib/win/mingw64-rt/hb_llvmgtstd.o` | Pre-compiled `gtstd` registration object (forced in via `--undefined HB_FUN_HB_GT_STD_DEFAULT`) |

Plus this program's own freshly-compiled `.o`.

The link line looks roughly like:

```
ld.lld --subsystem console -o hello.exe \
  <prefix>/lib/win/mingw64-rt/crt2.o \
  hello.o \
  <prefix>/lib/win/mingw64-rt/hb_llvmgtstd.o \
  --undefined HB_FUN_HB_GT_STD_DEFAULT \
  -L <prefix>/lib/win/mingw64 \
  -L <prefix>/lib/win/mingw64-rt \
  --start-group -lhbvm -lhbrtl -lhbcommon ... --end-group \
  -lkernel32 -luser32 -lws2_32 ... \
  -lstdc++ -lmingw32 -lgcc -lgcc_eh -lmingwex -lmoldname -lmsvcrt
```

**Result: a fully self-contained `.exe`.** The end-user machine needs no
toolchain at all — Harbour ships its own C runtime, MinGW import libs, the
Harbour runtime, and the LLVM/LLD code generator inside `harbour.exe`. One
install, one binary, runnable executables out.

### Is there anything left to implement? Anything to fix?

No within the planned scope. All nine opcode groups (A–I) complete + all
foundation plans (1-3). 26 corpus programs verified byte-identical against
the C backend; CI publishes every run to GitHub Pages. The permanent-
fallback opcode set (timestamp / date literals, `$` substring, static-frame,
var-arg-frame, hidden strings, …) is intentional infrastructure — programs
using those opcodes still build and run via the interpreter fallback.

---

## How it is verified

[`tests/llvm/run.sh`](tests/llvm/run.sh) is the gate. For every program in
`tests/llvm/*.prg` it:

1. compiles with the **C backend** (`harbour -GC` → `hbmk2`) → reference exe;
2. compiles with the **LLVM backend** (`harbour -GL`) → `.ll` IR text;
3. runs the **LLVM verifier** (`clang -x ir`) on the IR;
4. turns the IR into a native object and links it via `hbmk2` → LLVM exe;
5. runs both executables and **diffs the output**;
6. writes an HTML report with every program's source, IR, output, and
   diff status — the page you see on GitHub Pages.

Any failure on any program is a hard fail; CI publishes the result
automatically:

➡ **<https://fivetechsoft.github.io/harbourllvm/>**

Run it locally with:

```sh
CLANG=clang tests/llvm/run.sh
# -> build/llvm-ci/index.html
```

---

## Project layout

```
core/
├── bin/<platform>/harbour      # the compiler binary (built)
├── include/hbvmsh.h            # exported runtime op shim declarations
├── src/
│   ├── compiler/
│   │   ├── genllvm.c           # the LLVM IR emitter (-GL backend)
│   │   ├── hb_pcdec.{c,h}      # pcode-opcode decoder used by the backend
│   │   └── …
│   └── vm/
│       └── hvm.c               # the VM; hb_vmsh_* shims live at the end
├── tests/llvm/                 # corpus + run.sh + smoke tests
└── docs/superpowers/           # specs and implementation plans
    ├── README.md               # index of every spec + plan
    ├── specs/                  # 10 design documents
    └── plans/                  # 12 step-by-step implementation plans
```

---

## Building

```sh
# Linux / macOS
make

# Windows (MinGW)
set HB_COMPILER=mingw64
win-make.exe
```

The C toolchain is needed to build *Harbour itself*, not to use the LLVM
backend afterwards. After the build, an end user runs `harbour -GL foo.prg`
and gets `foo.exe` directly — no C compiler on their PATH.

---

## License

Same as Harbour — **GPL-compatible with the Harbour exception**. See
[`LICENSE.txt`](LICENSE.txt). The Harbour exception means linking
proprietary code against the Harbour runtime is explicitly allowed.

---

## Acknowledgements

- The [Harbour project](https://harbour.github.io/) and everyone who has
  worked on `harbour/core` over the years — the entire xBase frontend,
  runtime, RDDs, and the bulk of the compiler are theirs.
- The [LLVM project](https://llvm.org/) and LLD authors for a backend
  toolchain clean enough to embed.
