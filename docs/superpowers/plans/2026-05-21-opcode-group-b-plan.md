# Opcode Group B — arrays + hashes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so array literals, hash literals, element access/assignment, multi-dimensional array creation, and `...`-parameter forwarding emit straight-line native code instead of falling back to the `hb_vmExecute` interpreter.

**Architecture:** Pure replication of the group A pattern. 7 pcode opcodes each get: a `hb_vmsh_*` shim appended to `src/vm/hvm.c` (mirroring its `hb_vmExecute` switch case, returning the action request), `fSupported = HB_TRUE` in the `hb_pcInfo[]` decoder table, and an emitter `case` in `genllvm.c`. No new files, no new mechanisms.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-21-opcode-group-b-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode group A — `harbour -GL` emits straight-line IR for FOR loops + compound assignment, with a whole-function interpreter fallback.

---

## Build environment

Every build/test step uses:

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

Build Harbour from the repo root with `./win-make.exe`. **Known build note:** `win-make` aborts at a pre-existing, unrelated `harbour-32-x64.dll` link failure (`hashfunc.o`/`pvalue.o` undefined symbols). That failure predates this work and is irrelevant — the LLVM backend links static `.a` libs. The static libraries (`libhbvm.a`, `libhbcplr.a`) and `bin/win/mingw64/harbour.exe` are built BEFORE that DLL step. After a build that changes `genllvm.c`, the `harbour.exe` Makefile tracks only `.o` deps — if `harbour.exe` did not relink, `touch src/main/harbour.c` and run `./win-make.exe` again. Compiler: `bin/win/mingw64/harbour.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

---

## The 7 opcodes (reference)

Verified against the `hb_vmExecute` switch in `src/vm/hvm.c`. Each interpreter `case` is a single call to one `static` helper; the loop over the VM stack is inside the helper, so each shim is mechanical.

| Opcode | Len | Interpreter case body | Helper signature (confirm in `hvm.c`) |
|--------|-----|-----------------------|----------------------------------------|
| `HB_P_ARRAYPUSH` | 1 | `hb_vmArrayPush();` | `static void hb_vmArrayPush( void )` |
| `HB_P_ARRAYPUSHREF` | 1 | `hb_vmArrayPushRef();` | `static void hb_vmArrayPushRef( void )` |
| `HB_P_ARRAYPOP` | 1 | `hb_vmArrayPop();` | `static void hb_vmArrayPop( void )` |
| `HB_P_PUSHAPARAMS` | 1 | `hb_vmPushAParams();` | `static void hb_vmPushAParams( void )` |
| `HB_P_ARRAYDIM` | 3 | `hb_vmArrayDim( HB_PCODE_MKUSHORT( &pCode[1] ) );` | `static void hb_vmArrayDim( HB_USHORT uiDimensions )` |
| `HB_P_ARRAYGEN` | 3 | `hb_vmArrayGen( HB_PCODE_MKUSHORT( &pCode[1] ) );` | `static void hb_vmArrayGen( HB_SIZE nElements )` |
| `HB_P_HASHGEN` | 3 | `hb_vmHashGen( HB_PCODE_MKUSHORT( &pCode[1] ) );` | `static void hb_vmHashGen( HB_SIZE nElements )` |

The implementer MUST open `src/vm/hvm.c`, confirm each `case` body and each helper signature, and reproduce the case verbatim. If a helper's name or signature differs from this table, follow `hvm.c`.

---

### Task 1: The 7 array/hash op shims

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 7 declarations)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 7 shim implementations to the existing group A shim block)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_b.c`

- [ ] **Step 1: Add the declarations to `hbvmsh.h`**

Inside the existing `HB_EXTERN_BEGIN ... HB_EXTERN_END` block in `include/hbvmsh.h`, after the group A declarations, add:

```c
/* --- group B: arrays + hashes --- */
extern HB_EXPORT int hb_vmsh_arraypush( void );
extern HB_EXPORT int hb_vmsh_arraypushref( void );
extern HB_EXPORT int hb_vmsh_arraypop( void );
extern HB_EXPORT int hb_vmsh_pushaparams( void );
extern HB_EXPORT int hb_vmsh_arraydim( int iCount );
extern HB_EXPORT int hb_vmsh_arraygen( int iCount );
extern HB_EXPORT int hb_vmsh_hashgen( int iCount );
```

- [ ] **Step 2: Append the 7 shim implementations to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group A shim block, add the 7 implementations. Each starts with `HB_STACK_TLS_PRELOAD` as its first statement and ends with `return ( int ) hb_stackGetActionRequest();` — identical shape to the group A shims:

```c
/* --- group B: arrays + hashes --- */

HB_EXPORT int hb_vmsh_arraypush( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmArrayPush();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_arraypushref( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmArrayPushRef();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_arraypop( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmArrayPop();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushaparams( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushAParams();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_arraydim( int iCount )
{
   HB_STACK_TLS_PRELOAD
   hb_vmArrayDim( ( HB_USHORT ) iCount );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_arraygen( int iCount )
{
   HB_STACK_TLS_PRELOAD
   hb_vmArrayGen( ( HB_SIZE ) iCount );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_hashgen( int iCount )
{
   HB_STACK_TLS_PRELOAD
   hb_vmHashGen( ( HB_SIZE ) iCount );
   return ( int ) hb_stackGetActionRequest();
}
```

Before writing these, confirm against `hvm.c`: the exact helper names (`hb_vmArrayPush` etc.), that each is reachable from the end of `hvm.c` (they are `static` in the same file — fine), and the exact parameter types of `hb_vmArrayDim` / `hb_vmArrayGen` / `hb_vmHashGen` so the casts match. If any `case` body does more than the single call shown in the reference table, reproduce it verbatim instead.

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_b.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 7 ];
   p[ 0 ] = ( void * ) hb_vmsh_arraypush;
   p[ 1 ] = ( void * ) hb_vmsh_arraypushref;
   p[ 2 ] = ( void * ) hb_vmsh_arraypop;
   p[ 3 ] = ( void * ) hb_vmsh_pushaparams;
   p[ 4 ] = ( void * ) hb_vmsh_arraydim;
   p[ 5 ] = ( void * ) hb_vmsh_arraygen;
   p[ 6 ] = ( void * ) hb_vmsh_hashgen;
   printf( "group B shims linkable: %p %p %p %p %p %p %p\n",
           p[0], p[1], p[2], p[3], p[4], p[5], p[6] );
   return 0;
}
```

- [ ] **Step 4: Rebuild Harbour and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/shim_smoke_b.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -o build/shim_smoke_b.exe
./build/shim_smoke_b.exe
```

Expected: `hvm.c` compiles with the 7 new shims (no errors); the build reaches and rebuilds `libhbvm.a` before the known unrelated DLL-link abort; `shim_smoke_b.exe` prints seven non-null pointers.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_b.c
git commit -m "vm: add group B op shims (arrays + hashes)"
```

---

### Task 2: Decoder table — mark the 7 opcodes supported

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`

The 7 opcodes already have correct `kind` (`HB_PCK_FIXED`) and `nLen` (1 or 3) in `hb_pcInfo[]`. Only the `fSupported` field changes from `HB_FALSE` to `HB_TRUE`.

- [ ] **Step 1: Flip the flags**

In `src/compiler/hb_pcdec.c`, change the third field from `HB_FALSE` to `HB_TRUE` for exactly these 7 table rows (identify each by its `/* NN HB_P_<name> */` comment):

`HB_P_ARRAYPUSH` (1), `HB_P_ARRAYPOP` (2), `HB_P_ARRAYDIM` (3), `HB_P_ARRAYGEN` (4), `HB_P_ARRAYPUSHREF` (148), `HB_P_HASHGEN` (177), `HB_P_PUSHAPARAMS` (180).

Do NOT change `kind` or `nLen` on any row. Do NOT touch any opcode not in this list.

- [ ] **Step 2: Update the file-header comment**

`hb_pcdec.c` has a header comment listing the supported subset. Add the 7 group B opcode names to the "in scope (fSupported = HB_TRUE)" list so the comment matches the table.

- [ ] **Step 3: Rebuild and verify**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: `libhbcplr.a` rebuilds (before the known DLL abort); `pcdectest.exe` still prints `pcdec: 4 instructions, all boundaries correct` and `pcdec: jump analysis correct` (flipping `fSupported` does not change instruction lengths). `git diff` shows exactly 7 `HB_FALSE`→`HB_TRUE` flips in the table.

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c
git commit -m "compiler: mark group B opcodes supported in the decoder table"
```

---

### Task 3: Emitter cases for the 7 opcodes

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Add the `declare` lines**

In the module-header emission of `genllvm.c`, where the group A `declare i32 @hb_vmsh_*` lines are emitted, add:

```c
   fprintf( yyc, "declare i32 @hb_vmsh_arraypush()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_arraypushref()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_arraypop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushaparams()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_arraydim(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_arraygen(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_hashgen(i32)\n" );
```

- [ ] **Step 2: Add the emitter cases**

In `hb_llvmSLEmitBody`'s opcode `switch`, add cases for the 7 opcodes.

The **4 no-operand opcodes** (`HB_P_ARRAYPUSH`, `HB_P_ARRAYPUSHREF`, `HB_P_ARRAYPOP`, `HB_P_PUSHAPARAMS`) follow the existing `HB_EMIT_NOARG_SHIM` pattern that group A used for its no-operand opcodes — add four invocations mapping each opcode to its shim name (`HB_P_ARRAYPUSH`→`arraypush`, `HB_P_ARRAYPUSHREF`→`arraypushref`, `HB_P_ARRAYPOP`→`arraypop`, `HB_P_PUSHAPARAMS`→`pushaparams`). If `HB_EMIT_NOARG_SHIM` is `#undef`-ed before the group A operand cases, place the four new no-operand cases before that `#undef`.

The **3 count-operand opcodes** (`HB_P_ARRAYDIM`, `HB_P_ARRAYGEN`, `HB_P_HASHGEN`) mirror the existing group A `HB_P_LOCALINC` emitter case (a single 2-byte-operand shim call): decode `HB_PCODE_MKUSHORT(&pCode[pos+1])` and emit `call i32 @hb_vmsh_arraydim(i32 <count>)` (resp. `_arraygen`, `_hashgen`), then the standard action-request `icmp`/`br` to `%epilogue` / next block.

Every case ends with the same action-request check + branch the group A cases use, and respects the dangling-label guard (`label %epilogue` when `nextOff >= nPCSize`) via the existing next-block-label helper.

- [ ] **Step 3: Rebuild and check the IR for an array literal**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
# ensure harbour.exe relinked (see the build note); if not, touch + rebuild:
# touch src/main/harbour.c && ./win-make.exe
printf 'function Main()\n   local a := { 1, 2, 3 }\n   ? a[ 2 ]\n   return nil\n' > build/arrtmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3b build/arrtmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3b.ll -o NUL
grep -c "hb_vmsh_arraygen\|hb_vmsh_arraypush" build/t3b.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3b.ll | grep -c "call void @hb_vmExecute"
```

Expected: clean rebuild; `t3b.ll` passes the LLVM verifier (only the benign `-Woverride-module` warning); `grep -c` for the array shims is ≥ 1; the `awk|grep` count is `0` (`HB_FUN_MAIN` no longer falls back to the interpreter).

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for group B opcodes (arrays + hashes)"
```

---

### Task 4: Corpus — verify arrays and hashes

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\arraylit.prg`
- Create: `c:\HarbourLLVM\core\tests\llvm\hashlit.prg`
- Create: `c:\HarbourLLVM\core\tests\llvm\arraydim.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh`

- [ ] **Step 1: Write the new corpus programs**

`tests/llvm/arraylit.prg`:

```harbour
function Main()
   local a := { 10, 20, 30 }
   a[ 1 ] := 99
   ? a[ 1 ], a[ 2 ], a[ 3 ]
   ? Len( a )
   return nil
```

`tests/llvm/hashlit.prg`:

```harbour
function Main()
   local h := { "a" => 1, "b" => 2, "c" => 3 }
   ? h[ "b" ]
   ? Len( h )
   return nil
```

`tests/llvm/arraydim.prg`:

```harbour
function Main()
   local a := Array( 3, 2 )
   a[ 2 ][ 1 ] := 7
   ? a[ 2 ][ 1 ]
   ? Len( a ), Len( a[ 1 ] )
   return nil
```

- [ ] **Step 2: Update `run.sh`'s straight-line assertion set**

`tests/llvm/run.sh` asserts which programs straight-line vs fall back. Add `arraylit`, `hashlit`, `arraydim` to the straight-lined-expected set (checked like `arith`/`loop`: must contain `hb_vmsh_`, must NOT contain `call void @hb_vmExecute` in `HB_FUN_MAIN`). Keep `fallback` in the fallback set. If `run.sh` keys the assertion off a hard-coded program-name list/`case` pattern, update that; do not break the existing report.

Note: if any of these three programs legitimately falls back (e.g. an opcode the array helpers emit that is still outside the subset), do NOT force it into the straight-lined set — leave it where it lands and report it in Task 4 Step 5. The diff-against-C-backend is the correctness gate; straight-lining is the goal but a correct fallback is acceptable.

- [ ] **Step 3: Run the new programs end-to-end**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
for p in arraylit hashlit arraydim; do
  env PATH="/c/Windows/System32:/c/Windows" \
    bin/win/mingw64/harbour.exe -GL -q -obuild/gb_$p tests/llvm/$p.prg
  ./build/gb_$p.exe > build/gb_${p}_ll.out
  bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/gb_${p}_c tests/llvm/$p.prg
  ./build/gb_${p}_c.exe > build/gb_${p}_c.out
  diff build/gb_${p}_c.out build/gb_${p}_ll.out && echo "$p: MATCH" || echo "$p: DIFF"
done
```

Expected: `arraylit: MATCH`, `hashlit: MATCH`, `arraydim: MATCH` — the straight-line backend output is byte-identical to the C backend.

- [ ] **Step 4: Confirm the array programs straight-lined**

```bash
cd c:/HarbourLLVM/core
for p in arraylit hashlit arraydim; do
  echo -n "$p hb_vmsh_ count: "; grep -c "hb_vmsh_" build/gb_$p.ll
  echo -n "$p hb_vmExecute in MAIN: "
  awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/gb_$p.ll | grep -c "call void @hb_vmExecute"
done
```

Expected: each program's `hb_vmsh_` count ≥ 1; each `hb_vmExecute in MAIN` count is `0`. (If a program shows a non-zero fallback count, report it — see Step 2's note.)

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gb
```

Expected: `run.sh` ends with `RESULT: all programs validated and matched the C backend`, and the report shows `arraylit`/`hashlit`/`arraydim` straight-lined (or, if any fell back, that program still MATCHes the C backend — report which).

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/arraylit.prg tests/llvm/hashlit.prg tests/llvm/arraydim.prg tests/llvm/run.sh
git commit -m "test: corpus for group B — arrays and hashes straight-lined"
```

---

## Self-Review

**Spec coverage:** The spec's two opcode sub-groups (4 no-operand + 3 count-operand = 7) are covered — Task 1 the shims, Task 2 the decoder flags, Task 3 the emitter cases, Task 4 the array-literal / hash-literal / `Array()` corpus. The spec's testing section (`arraylit.prg`, `hashlit.prg`, `arraydim.prg`, diff against the C backend, straight-line assertion) is Task 4.

**Placeholder scan:** All 7 shims are shown in full in Task 1 Step 2 (there are only 7 — no "remaining N follow the pattern" shorthand needed). Task 3's no-operand cases are anchored to the existing `HB_EMIT_NOARG_SHIM` pattern and the count-operand cases to the existing group A `HB_P_LOCALINC` case — both are concrete existing references, not placeholders. Task 2's flip list names all 7 opcodes with their numeric values. No "TBD"/"handle edge cases"/vague steps.

**Type consistency:** Shim names and signatures are identical across `hbvmsh.h` (Task 1 Step 1), the implementations (Task 1 Step 2), and the IR `declare`s (Task 3 Step 1): the 4 no-operand shims are `int (void)` / `i32 ()`; `arraydim`/`arraygen`/`hashgen` are `int (int)` / `i32 (i32)`. The 7 opcode names in Task 2's flip list match the reference table and the spec.

**Known risk:** the `ARRAYDIM`/`ARRAYGEN`/`HASHGEN` helpers consume a runtime-variable number of stack items driven by the count operand — but that logic lives entirely inside the existing `static` helper, unchanged; the shim only forwards the decoded count. The Task 4 diff-against-C-backend corpus is the gate that catches any deviation.
