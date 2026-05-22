# Opcode Group E — FOR EACH — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so `FOR EACH` loops over arrays, hashes, and strings emit straight-line native code instead of falling back to the `hb_vmExecute` interpreter.

**Architecture:** Pure replication of the group A–D pattern. 4 pcode opcodes each get: a `hb_vmsh_*` shim appended to `src/vm/hvm.c` (mirroring its `hb_vmExecute` case, returning the action request), `fSupported = HB_TRUE` in the `hb_pcInfo[]` decoder table, and an emitter `case` in `genllvm.c`. The loop's control flow (`JUMPFALSE` after `ENUMSTART` / `ENUMNEXT`) uses jump opcodes already supported since Plan 3 — no new control-flow handling.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-22-opcode-group-e-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode groups A, B, C, D.

---

## Build environment

Every build/test step uses:

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

Build Harbour from the repo root with `./win-make.exe`. **Known build note:** `win-make` aborts at a pre-existing, unrelated `harbour-32-x64.dll` link failure — predates this work, irrelevant (the LLVM backend links static `.a` libs). Static libraries and `bin/win/mingw64/harbour.exe` build BEFORE that step. The `harbour.exe` Makefile tracks only `.o` deps — if a `genllvm.c` change did not relink `harbour.exe`, run `touch src/main/harbour.c` then `./win-make.exe`. `hbmk2`: `bin/win/mingw64/hbmk2.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

**Shell note:** do not combine `cd` with output redirection (`>`) in one command — Claude Code blocks it. Run a standalone `cd` first (the Bash working directory persists between calls), or redirect with an absolute path.

---

## The 4 opcodes (reference)

Verified against the `hb_vmExecute` switch in `src/vm/hvm.c`.

| Opcode | Len | Interpreter case body | Shim |
|--------|-----|-----------------------|------|
| `HB_P_ENUMSTART` | 3 | `hb_vmEnumStart( (unsigned char) pCode[1], (unsigned char) pCode[2] );` | `hb_vmsh_enumstart` |
| `HB_P_ENUMNEXT` | 1 | `hb_vmEnumNext();` | `hb_vmsh_enumnext` |
| `HB_P_ENUMPREV` | 1 | `hb_vmEnumPrev();` | `hb_vmsh_enumprev` |
| `HB_P_ENUMEND` | 1 | `hb_vmEnumEnd();` | `hb_vmsh_enumend` |

All four helpers (`hb_vmEnumStart`, `hb_vmEnumNext`, `hb_vmEnumPrev`, `hb_vmEnumEnd`) are `static` in `hvm.c` — reachable from the shim block at the end of the same file. The implementer MUST open `src/vm/hvm.c`, confirm each `case` body and helper name verbatim, and reproduce it. If any case body differs from this table, follow `hvm.c`.

---

### Task 1: The 4 FOR EACH op shims

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 4 declarations)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 4 shim implementations to the existing group D shim block)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_e.c`

- [ ] **Step 1: Add the declarations to `hbvmsh.h`**

Inside the existing `HB_EXTERN_BEGIN ... HB_EXTERN_END` block in `include/hbvmsh.h`, after the group D declarations, add:

```c
/* --- group E: FOR EACH --- */
extern HB_EXPORT int hb_vmsh_enumstart( int nVars, int nDescend );
extern HB_EXPORT int hb_vmsh_enumnext( void );
extern HB_EXPORT int hb_vmsh_enumprev( void );
extern HB_EXPORT int hb_vmsh_enumend( void );
```

- [ ] **Step 2: Append the 4 shim implementations to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group D shim block, add the 4 implementations. Each starts with `HB_STACK_TLS_PRELOAD` as its first statement and ends with `return ( int ) hb_stackGetActionRequest();` — identical shape to the group A–D shims:

```c
/* --- group E: FOR EACH --- */

HB_EXPORT int hb_vmsh_enumstart( int nVars, int nDescend )
{
   HB_STACK_TLS_PRELOAD
   hb_vmEnumStart( ( unsigned char ) nVars, ( unsigned char ) nDescend );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_enumnext( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmEnumNext();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_enumprev( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmEnumPrev();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_enumend( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmEnumEnd();
   return ( int ) hb_stackGetActionRequest();
}
```

Before writing these, confirm against `hvm.c`: the exact helper names and that `hb_vmEnumStart` takes two `unsigned char` (or `int`) parameters in the order (nVars, nDescend) — match whatever the actual signature is and cast accordingly. If any `case` body does more than the single call shown, reproduce it verbatim.

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_e.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 4 ];
   p[ 0 ] = ( void * ) hb_vmsh_enumstart;
   p[ 1 ] = ( void * ) hb_vmsh_enumnext;
   p[ 2 ] = ( void * ) hb_vmsh_enumprev;
   p[ 3 ] = ( void * ) hb_vmsh_enumend;
   printf( "group E shims linkable: %p %p %p %p\n", p[0], p[1], p[2], p[3] );
   return 0;
}
```

- [ ] **Step 4: Rebuild Harbour and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/shim_smoke_e.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -o build/shim_smoke_e.exe
./build/shim_smoke_e.exe
```

Expected: `hvm.c` compiles with the 4 new shims (no errors); `libhbvm.a` rebuilds before the known unrelated DLL-link abort; `shim_smoke_e.exe` prints four non-null pointers. (`--noinhibit-exec` on the `gcc` link is acceptable if the known incomplete-DLL situation requires it.)

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_e.c
git commit -m "vm: add group E op shims (FOR EACH)"
```

---

### Task 2: Decoder table — mark the 4 opcodes supported

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`

The 4 opcodes already have correct `kind` (`HB_PCK_FIXED`) and `nLen` (3 or 1) in `hb_pcInfo[]`. Only the `fSupported` field changes.

- [ ] **Step 1: Flip the flags**

In `src/compiler/hb_pcdec.c`, change the third field from `HB_FALSE` to `HB_TRUE` for exactly these 4 table rows (identify each by its `/* NN HB_P_<name> */` comment):

`HB_P_ENUMSTART` (129), `HB_P_ENUMNEXT` (130), `HB_P_ENUMPREV` (131), `HB_P_ENUMEND` (132).

Do NOT change `kind` or `nLen` on any row. Do NOT touch any opcode not in this list.

- [ ] **Step 2: Update the file-header comment**

`hb_pcdec.c` has a header comment listing the supported subset. Add the 4 group E opcode names to the "in scope (fSupported = HB_TRUE)" list so the comment matches the table.

- [ ] **Step 3: Rebuild and verify**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: `libhbcplr.a` rebuilds before the known DLL abort; `pcdectest.exe` still prints `pcdec: 4 instructions, all boundaries correct` and `pcdec: jump analysis correct`. `git diff` shows exactly 4 `HB_FALSE`→`HB_TRUE` flips in the table.

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c
git commit -m "compiler: mark group E opcodes supported in the decoder table"
```

---

### Task 3: Emitter cases for the 4 opcodes

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Add the `declare` lines**

In the module-header emission of `genllvm.c`, where the group D `declare i32 @hb_vmsh_*` lines are emitted, add:

```c
   fprintf( yyc, "declare i32 @hb_vmsh_enumstart(i32, i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_enumnext()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_enumprev()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_enumend()\n" );
```

- [ ] **Step 2: Add the emitter cases**

In `hb_llvmSLEmitBody`'s opcode `switch`, add cases for the 4 opcodes.

The **3 no-operand opcodes** (`HB_P_ENUMNEXT`→`enumnext`, `HB_P_ENUMPREV`→`enumprev`, `HB_P_ENUMEND`→`enumend`) follow the existing `HB_EMIT_NOARG_SHIM` pattern — add three invocations before the `#undef HB_EMIT_NOARG_SHIM`.

The **`HB_P_ENUMSTART`** opcode has two 1-byte operands. Mirror the existing group E-style two-operand cases — specifically the group A `HB_P_LOCALADDINT` / `HB_P_FRAME` emitter case, which decodes two operands and passes both to a two-argument shim. Decode `nVars` from `pCode[pos+1]` (1 byte, unsigned) and `nDescend` from `pCode[pos+2]` (1 byte, unsigned), and emit `call i32 @hb_vmsh_enumstart(i32 <nVars>, i32 <nDescend>)`, then the standard action-request `icmp`/`br` to `%epilogue` / next block.

Every case ends with the same action-request check + branch the group A–D cases use, via the existing next-block-label helper (`label %epilogue` when `nextOff >= nPCSize`). Study the existing `HB_P_FRAME` case (two `i32` shim args) for the exact two-operand emission shape, and the `HB_EMIT_NOARG_SHIM` cases for the no-operand shape.

- [ ] **Step 3: Rebuild and check the IR for a FOR EACH program**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
printf 'function Main()\n   local x, n := 0\n   for each x in { 4, 5, 6 }\n      n := n + x\n   next\n   ? n\n   return nil\n' > build/fetmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3e build/fetmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3e.ll -o NUL
grep -c "hb_vmsh_enumstart\|hb_vmsh_enumnext" build/t3e.ll
```
Then, in a separate command (no `cd`+redirect compound):
```bash
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3e.ll | grep -c "call void @hb_vmExecute"
```

Expected: clean rebuild (relink `harbour.exe` if needed — `touch src/main/harbour.c` then `./win-make.exe`); `t3e.ll` passes the LLVM verifier (only the benign `-Woverride-module` warning); the enum-shim grep is ≥ 1; the `awk|grep` count is `0` (`HB_FUN_MAIN` no longer falls back).

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for group E opcodes (FOR EACH)"
```

---

### Task 4: Corpus — verify FOR EACH loops

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\foreach.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh`

- [ ] **Step 1: Write the new corpus program**

`tests/llvm/foreach.prg`:

```harbour
function Main()
   local x, nSum := 0, cJoin := ""
   for each x in { 10, 20, 30, 40 }
      nSum += x
   next
   for each x in "abc"
      cJoin += x
   next
   ? nSum
   ? cJoin
   return nil
```

(The first loop exercises `ENUMSTART`/`ENUMNEXT`/`ENUMEND` over an array; the second over a string. `+=` is a group A opcode, already supported.)

- [ ] **Step 2: Update `run.sh`'s straight-line assertion set**

`tests/llvm/run.sh` asserts which programs straight-line vs fall back. Add `foreach` to the straight-lined-expected set (checked like `arith`/`loop`: must contain `hb_vmsh_`, must NOT contain `call void @hb_vmExecute` in `HB_FUN_MAIN`). Keep `fallback` in the fallback set. Study the current `run.sh` straight-line/fallback assertion logic and integrate cleanly.

Note: if `foreach.prg` legitimately falls back because it emits an opcode still outside the A–E subset, do NOT force it into the straight-lined set — leave it where it lands and report it in Task 4 Step 5. The diff-against-C-backend is the correctness gate; a correct fallback is acceptable.

- [ ] **Step 3: Run the new program end-to-end**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/ge_foreach tests/llvm/foreach.prg
./build/ge_foreach.exe > build/ge_foreach_ll.out 2>&1
bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/ge_foreach_c tests/llvm/foreach.prg
./build/ge_foreach_c.exe > build/ge_foreach_c.out 2>&1
diff build/ge_foreach_c.out build/ge_foreach_ll.out
```

Expected: `diff` reports no differences — the straight-line backend output is byte-identical to the C backend. (`foreach.prg` prints `100` then `abc`.)

- [ ] **Step 4: Confirm the program straight-lined**

```bash
cd c:/HarbourLLVM/core
grep -c "hb_vmsh_enumstart" build/ge_foreach.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/ge_foreach.ll | grep -c "call void @hb_vmExecute"
```

Expected: `hb_vmsh_enumstart` count ≥ 1; `hb_vmExecute` in `HB_FUN_MAIN` count is `0`. (If non-zero fallback, report it — see Step 2's note.)

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-ge
```

Expected: `run.sh` ends with `RESULT: all programs validated and matched the C backend`, and the report shows `foreach` straight-lined (or, if it fell back, that it still MATCHes the C backend — report which).

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/foreach.prg tests/llvm/run.sh
git commit -m "test: corpus for group E — FOR EACH loops straight-lined"
```

---

## Self-Review

**Spec coverage:** The spec's two opcode sub-groups (3 no-operand + 1 two-operand = 4) are covered — Task 1 the 4 shims, Task 2 the 4 decoder flags, Task 3 the 4 emitter cases, Task 4 the FOR EACH corpus. The spec's testing section (`foreach.prg` over an array and a string, diff against the C backend, straight-line assertion) is Task 4.

**Placeholder scan:** All 4 shims are shown in full in Task 1 Step 2. Task 3's no-operand cases are anchored to the existing `HB_EMIT_NOARG_SHIM` pattern and the `ENUMSTART` two-operand case to the existing `HB_P_FRAME` / `HB_P_LOCALADDINT` two-operand cases — both concrete existing references. Task 2's flip list names all 4 opcodes with numeric values. No "TBD"/vague steps.

**Type consistency:** Shim names and signatures are identical across `hbvmsh.h` (Task 1 Step 1), the implementations (Task 1 Step 2), and the IR `declare`s (Task 3 Step 1): 3 no-operand shims `int (void)` / `i32 ()`; `hb_vmsh_enumstart` `int (int, int)` / `i32 (i32, i32)`. The 4 opcode names in Task 2's flip list match the reference table and the spec.

**Known risk:** `hb_vmEnumStart` is internally stateful and the FOR EACH loop relies on the already-supported `JUMPFALSE` for control flow — no new emitter mechanism is needed (confirmed in the spec and the exploration). The Task 4 diff-against-C-backend corpus is the gate that catches any deviation; `foreach.prg` exercises both an array and a string container.
