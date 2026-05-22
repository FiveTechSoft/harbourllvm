# Opcode Group G — Codeblocks — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so a function that builds a codeblock literal (`{|args| ... }`) emits straight-line native code instead of falling back, whole-function, to the `hb_vmExecute` interpreter.

**Architecture:** Three opcodes — `HB_P_PUSHBLOCK`, `HB_P_PUSHBLOCKSHORT`, `HB_P_PUSHBLOCKLARGE` — become a single `hb_vmsh_pushblock` shim call. The shim, in `src/vm/hvm.c`, dispatches on the opcode byte to the existing static helpers `hb_vmPushBlock` / `hb_vmPushBlockShort` with the exact offsets the interpreter uses. The decoder already computes these opcodes' variable length (`HB_PCK_VARBLOCK`); only the `fSupported` flag flips. The emitter passes a pointer into the function's `@.pcode.<func>` global at the opcode offset (the block body stays inline there) and the `@symbols_table` base. The block *body* still runs through `hb_vmExecute` on `Eval()` — identical to the C backend.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-22-opcode-group-g-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode groups A–F.

---

## Build environment

Every build/test step uses:

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

Build Harbour from the repo root with `./win-make.exe`.

**Known build notes:**
- `win-make` aborts at a pre-existing, unrelated `harbour-32-x64.dll` link failure (`hb_paramError` / `hashfunc.o` / `pvalue.o`) — predates this work, irrelevant. Static libraries and `bin/win/mingw64/harbour.exe` build BEFORE that step.
- **`hvm.c` is compiled via a unity file.** `libhbvm.a` is built from `src/vm/hvmall.c`, which `#include`s `hvm.c`. `make` tracks only `hvmall.c`, not the `#include`d `hvm.c`. After editing `hvm.c` you MUST `touch src/vm/hvmall.c` before `./win-make.exe`, or the new shim will not land in `libhbvm.a`. Verify with `nm lib/win/mingw64/libhbvm.a | grep hb_vmsh_pushblock` after the build.
- The `harbour.exe` Makefile tracks only `.o` deps — if a `genllvm.c` change did not relink `harbour.exe` (`win-make` says `harbour.exe is up to date`), run a standalone `touch src/main/harbour.c` then `./win-make.exe`.
- `hbmk2`: `bin/win/mingw64/hbmk2.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

**Shell note:** do not combine `cd` with output redirection (`>`) in one command — Claude Code blocks it. Run a standalone `cd` first (the Bash working directory persists between calls), or redirect with an absolute path.

---

## `HB_P_PUSHBLOCK*` instruction layout (reference)

`HB_P_PUSHBLOCK` (89): `[opcode][size: 2-byte LE][paramCount: 2][localCount: 2][localTable: 2·localCount][body pcode … HB_P_ENDBLOCK]`. `size` is the whole-instruction byte length. The interpreter does `hb_vmPushBlock( pCode + 3, pSymbols, 0 )` then `pCode += size`.

`HB_P_PUSHBLOCKLARGE` (161): same, with a 3-byte `size`. Interpreter does `hb_vmPushBlock( pCode + 4, pSymbols, 0 )`.

`HB_P_PUSHBLOCKSHORT` (90): `[opcode][size: 1-byte][body pcode … HB_P_ENDBLOCK]` — no param count, no local table. Interpreter does `hb_vmPushBlockShort( pCode + 2, pSymbols, 0 )`.

(The `0` is `nLen`; for statically-compiled non-macro code `bDynCode` is false. The implementer should read `case HB_P_PUSHBLOCK:` / `PUSHBLOCKLARGE:` / `PUSHBLOCKSHORT:` in `hb_vmExecute`, `src/vm/hvm.c`, to confirm.)

The decoder rows are already `HB_PCK_VARBLOCK` and `hb_pcodeInstrLen` already returns the correct `size` for all three (the size operand IS the total instruction length) — verify by reading the `HB_PCK_VARBLOCK` arm of `hb_pcodeInstrLen` in `src/compiler/hb_pcdec.c`.

---

### Task 1: The `hb_vmsh_pushblock` shim

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 1 declaration)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 1 shim after the group F shim)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_g.c`

- [ ] **Step 1: Add the declaration to `hbvmsh.h`**

In `include/hbvmsh.h`, inside the `HB_EXTERN_BEGIN … HB_EXTERN_END` block, after the group F declaration (`hb_vmsh_switchidx`), add:

```c
/* --- group G: codeblocks --- */
/* Construct a codeblock value from a PUSHBLOCK* instruction and push it on
 * the stack, exactly as the interpreter's HB_P_PUSHBLOCK* cases do. pCode
 * points at the PUSHBLOCK* opcode byte itself; pSymbols is the module symbol
 * table. Runs no user code (the block body executes later, on Eval) and so
 * cannot set an action request — always returns 0. */
extern HB_EXPORT int hb_vmsh_pushblock( const unsigned char * pCode,
                                        PHB_SYMB pSymbols );
```

- [ ] **Step 2: Append the shim implementation to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group F shim (`hb_vmsh_switchidx`), add:

```c
/* --- group G: codeblocks --- */

HB_EXPORT int hb_vmsh_pushblock( const unsigned char * pCode,
                                 PHB_SYMB pSymbols )
{
   switch( pCode[ 0 ] )
   {
      case HB_P_PUSHBLOCK:
         hb_vmPushBlock( pCode + 3, pSymbols, 0 );
         break;
      case HB_P_PUSHBLOCKLARGE:
         hb_vmPushBlock( pCode + 4, pSymbols, 0 );
         break;
      case HB_P_PUSHBLOCKSHORT:
         hb_vmPushBlockShort( pCode + 2, pSymbols, 0 );
         break;
   }
   return 0;
}
```

Before writing this, read the `case HB_P_PUSHBLOCK:`, `case HB_P_PUSHBLOCKLARGE:`, and `case HB_P_PUSHBLOCKSHORT:` arms of `hb_vmExecute` in `src/vm/hvm.c` and confirm the `pCode + 3` / `pCode + 4` / `pCode + 2` offsets and the `nLen` argument. Note the interpreter passes `bDynCode ? nSize - N : 0` — for the LLVM backend `bDynCode` is always false (it compiles static `.prg` pcode, not runtime macro pcode), so `0` is correct. `hb_vmPushBlock` and `hb_vmPushBlockShort` are `static` in `hvm.c` — reachable from the shim block. Unlike the action-request-returning shims, this shim runs no user code; it returns `0` unconditionally, like `hb_vmsh_switchidx` does for its index.

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_g.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p = ( void * ) hb_vmsh_pushblock;
   printf( "group G shim linkable: %p\n", p );
   return 0;
}
```

- [ ] **Step 4: Rebuild Harbour and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
touch src/vm/hvmall.c
./win-make.exe
```

Then verify the symbol landed and link the smoke test (separate command):

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
nm lib/win/mingw64/libhbvm.a | grep hb_vmsh_pushblock
gcc tests/llvm/shim_smoke_g.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -Wl,--noinhibit-exec -o build/shim_smoke_g.exe
./build/shim_smoke_g.exe
```

Expected: `hvm.c` compiles with the new shim (no errors); `nm` prints a `T hb_vmsh_pushblock` line; `libhbvm.a` rebuilds before the known unrelated DLL-link abort; `shim_smoke_g.exe` prints a non-null pointer. (`-Wl,--noinhibit-exec` is required because the partially-built tree has unrelated unresolved RDD symbols; the smoke test only takes the function's address, it never calls it, so the produced exe runs fine.)

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_g.c
git commit -m "vm: add group G op shim hb_vmsh_pushblock (codeblocks)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Decoder — mark `HB_P_PUSHBLOCK*` supported

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`
- Modify: `c:\HarbourLLVM\core\tests\llvm\pcdectest.c`

The three `HB_P_PUSHBLOCK*` rows are already `HB_PCK_VARBLOCK` with the correct length computation. Task 2 only flips their `fSupported` flag and updates the header comment. There is NO length-computation change.

- [ ] **Step 1: Flip the three table rows**

In `src/compiler/hb_pcdec.c`'s `hb_pcInfo[]`, change these three rows:

```c
   /* 89  HB_P_PUSHBLOCK      */ { HB_PCK_VARBLOCK, 0,  HB_FALSE }, /* 2-byte size operand */
   /* 90  HB_P_PUSHBLOCKSHORT */ { HB_PCK_VARBLOCK, 0,  HB_FALSE }, /* 1-byte size operand */
```
to
```c
   /* 89  HB_P_PUSHBLOCK      */ { HB_PCK_VARBLOCK, 0,  HB_TRUE  }, /* 2-byte size operand */
   /* 90  HB_P_PUSHBLOCKSHORT */ { HB_PCK_VARBLOCK, 0,  HB_TRUE  }, /* 1-byte size operand */
```
and
```c
   /* 161 HB_P_PUSHBLOCKLARGE*/ { HB_PCK_VARBLOCK, 0,  HB_FALSE }, /* 3-byte size operand */
```
to
```c
   /* 161 HB_P_PUSHBLOCKLARGE*/ { HB_PCK_VARBLOCK, 0,  HB_TRUE  }, /* 3-byte size operand */
```

(Match the exact existing whitespace/comment of each row when editing — read the rows first. Do NOT change `HB_P_MPUSHBLOCK` (59), `HB_P_MPUSHBLOCKLARGE` (159), or `HB_P_ENDBLOCK` (6) — they stay `HB_FALSE`.)

- [ ] **Step 2: Update the file-header "in scope" comment**

In the comment block at the top of `src/compiler/hb_pcdec.c`, after the `Group F additions (SWITCH):` paragraph, add:

```c
 *
 * Group G additions (codeblocks):
 *   HB_P_PUSHBLOCK, HB_P_PUSHBLOCKSHORT, HB_P_PUSHBLOCKLARGE
```

- [ ] **Step 3: Add a decoder test**

In `tests/llvm/pcdectest.c`, after the `HB_P_SWITCH length correct` test block and before `return 0;` in `main()`, add:

```c
   {
      /* Group G: the three PUSHBLOCK* opcodes are now in the straight-line
       * subset, and a PUSHBLOCKSHORT's length is its 1-byte size operand. */
      HB_BYTE blk[] = { HB_P_PUSHBLOCKSHORT, 4, HB_P_PUSHNIL, HB_P_ENDBLOCK };
      assert( hb_pcodeInstrLen( blk ) == 4 );
      assert( hb_pcInfo[ HB_P_PUSHBLOCK      ].fSupported );
      assert( hb_pcInfo[ HB_P_PUSHBLOCKSHORT ].fSupported );
      assert( hb_pcInfo[ HB_P_PUSHBLOCKLARGE ].fSupported );
      printf( "pcdec: HB_P_PUSHBLOCK* supported\n" );
   }
```

- [ ] **Step 4: Rebuild and run the decoder test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
touch src/compiler/hb_pcdec.c
./win-make.exe
```

Then (separate command):

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: `libhbcplr.a` rebuilds before the known DLL abort; `pcdectest.exe` prints all prior lines plus `pcdec: HB_P_PUSHBLOCK* supported`.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c tests/llvm/pcdectest.c
git commit -m "compiler: mark HB_P_PUSHBLOCK* supported in the decoder (group G)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Emitter case for `HB_P_PUSHBLOCK*`

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Add the `declare` line**

In `hb_compGenLLVMCode`'s module-header emission, immediately after the group F declaration line `fprintf( yyc, "declare i32 @hb_vmsh_switchidx(i8*, i32)\n" );`, add:

```c
   /* Group G: codeblock shim declaration */
   fprintf( yyc, "declare i32 @hb_vmsh_pushblock(i8*, %%HB_SYMB*)\n" );
```

- [ ] **Step 2: Add the emitter case to `hb_llvmSLEmitBody`**

In `hb_llvmSLEmitBody`'s opcode `switch` (the one with `case HB_P_SWITCH:`), add the following `case` immediately before the `default:` arm. All three opcodes share one body — the shim dispatches on the opcode byte:

```c
         /* Group G: codeblock literal — construct the block value via the
          * shim. The block body pcode stays inline in @.pcode.<func>; the
          * shim hands hb_vmPushBlock a pointer into it. No action-request
          * check (constructing a block runs no user code). */
         case HB_P_PUSHBLOCK:
         case HB_P_PUSHBLOCKSHORT:
         case HB_P_PUSHBLOCKLARGE:
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushblock("
                     "i8* getelementptr([%lu x i8], [%lu x i8]* @.pcode.",
                     ( unsigned long ) pos,
                     ( unsigned long ) nPCSize, ( unsigned long ) nPCSize );
            hb_llvmEmitFuncName( yyc, pFunc->szName );
            fprintf( yyc,
                     ", i32 0, i32 %lu), "
                     "%%HB_SYMB* getelementptr([%d x %%HB_SYMB], "
                     "[%d x %%HB_SYMB]* @symbols_table, i32 0, i32 0))\n"
                     "  br label %%%s\n",
                     ( unsigned long ) pos,
                     iSymCount, iSymCount, szNextLabel );
            break;
```

Notes for the implementer:
- `pos`, `nPCSize`, `pFunc`, `iSymCount`, and `szNextLabel` are all already in scope in `hb_llvmSLEmitBody` — confirm by reading the function signature and the existing `case HB_P_SWITCH:` (which uses `pos`, `nPCSize`, `pFunc->szName`) and `case HB_P_PUSHSYM:` (which uses `iSymCount` for the `@symbols_table` GEP).
- The first GEP (`@.pcode.<func>`) points at `pos` — the `PUSHBLOCK*` opcode byte. The shim adds the `+3`/`+4`/`+2`. This is the same in-place-pcode-pointer technique `case HB_P_SWITCH:` uses.
- The second GEP yields the `@symbols_table` base pointer (`i32 0, i32 0`), matching what the spec requires for `pSymbols`.
- `szNextLabel` is the fall-through block — already computed at the top of the loop as `"epilogue"` or `"i<nextOff>"`, where `nextOff = pos + hb_pcodeInstrLen(&pCode[pos])` skips the whole inline block body. Do not recompute it.
- This case emits exactly two IR lines (the `call` and the `br`) — no `icmp`/`br i1`, no `switch`.

- [ ] **Step 3: Rebuild Harbour**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
```

If `win-make` reports `harbour.exe is up to date`, force the relink (separate command):

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
touch src/main/harbour.c
./win-make.exe
```

Expected: `genllvm.c` compiles clean; `bin/win/mingw64/harbour.exe` is relinked (a fresh timestamp / a `gcc … -o …/harbour.exe …` line in the output) before the known unrelated DLL abort.

- [ ] **Step 4: Check the IR for a codeblock program**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
printf 'function Main()\n   local b := {| x | x + 1 }\n   ? Eval( b, 10 )\n   return nil\n' > build/cbtmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3g build/cbtmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3g.ll -o NUL
grep -c "call i32 @hb_vmsh_pushblock" build/t3g.ll
```

Then separately:

```bash
cd c:/HarbourLLVM/core
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3g.ll | grep -c "call void @hb_vmExecute"
```

Expected: clean rebuild; `t3g.ll` passes the LLVM verifier (only the benign `-Woverride-module` warning); `grep -c "call i32 @hb_vmsh_pushblock"` is `1`; the `awk|grep` count is `0` (`HB_FUN_MAIN` no longer falls back).

- [ ] **Step 5: Run it end-to-end**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/t3grun build/cbtmp.prg
./build/t3grun.exe
```

Expected: prints `11` (`Eval( {|x| x+1}, 10 )`).

- [ ] **Step 6: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for HB_P_PUSHBLOCK* (group G)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Corpus — verify codeblocks

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\codeblock.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh`

- [ ] **Step 1: Write the new corpus program**

`tests/llvm/codeblock.prg` — exercise the compatibility-critical cases: a parameterless block, a block with a parameter via `Eval()`, a block capturing an enclosing-function local (detached local), nested codeblocks, and a block stored then evaluated later.

```harbour
function Main()
   local bAdd    := {| x, y | x + y }
   local nBase   := 100
   local bCapture := {| x | x + nBase }
   local bNested := {| x | Eval( {| y | y * 2 }, x ) + 1 }
   local bStored

   ? Eval( {|| "no args" } )
   ? Eval( bAdd, 3, 4 )
   ? Eval( bCapture, 5 )
   ? Eval( bNested, 10 )

   bStored := {| x | x - 1 }
   ? Eval( bStored, 42 )
   return nil
```

- [ ] **Step 2: Add `codeblock` to `run.sh`'s straight-line-required set**

In `tests/llvm/run.sh`, find the `case "$name" in` block listing the corpus programs that must NOT fall back (it currently ends `foreach|switchstmt)`). Change that line to add `codeblock`:

```bash
               foreach|switchstmt|codeblock)
```

(Read the surrounding `case` first; change only that one pattern line. Update the adjacent comment that enumerates the groups — `… E (foreach), and F (switchstmt) corpus` — to `… F (switchstmt), and G (codeblock) corpus`.)

- [ ] **Step 3: Run the new program end-to-end and diff against the C backend**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/gg_cb tests/llvm/codeblock.prg
./build/gg_cb.exe > build/gg_cb_ll.out 2>&1
bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/gg_cb_c tests/llvm/codeblock.prg
./build/gg_cb_c.exe > build/gg_cb_c.out 2>&1
diff build/gg_cb_c.out build/gg_cb_ll.out
```

Expected: `diff` reports no differences — the straight-line backend output is byte-identical to the C backend. (The program prints `no args`, `7`, `105`, `21`, `41`.)

- [ ] **Step 4: Confirm the program straight-lined**

```bash
cd c:/HarbourLLVM/core
grep -c "call i32 @hb_vmsh_pushblock" build/gg_cb.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/gg_cb.ll | grep -c "call void @hb_vmExecute"
```

Expected: `hb_vmsh_pushblock` call count is ≥ 5 (one per codeblock literal — the five `{| … |}` in `Main`); the `HB_FUN_MAIN` fallback count is `0`.

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gg
```

Expected: `run.sh` ends with `RESULT: all programs validated and matched the C backend`, and the report shows `codeblock` straight-lined (badge `SL ok`).

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/codeblock.prg tests/llvm/run.sh
git commit -m "test: corpus for group G — codeblocks straight-lined

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:** The spec's three required changes are covered — Task 1 the `hb_vmsh_pushblock` shim (`src/vm/hvm.c` + `hbvmsh.h`), Task 2 the decoder (`fSupported` flip for the three `HB_P_PUSHBLOCK*` rows; no new decode kind, as the spec notes), Task 3 the emitter case (the `declare` line + one shared `case` for all three opcodes). The spec's testing section (`codeblock.prg` with a parameterless block, a parameterised block, a detached-local capture, nested blocks, and a stored-then-evaluated block; diff against the C backend) is Task 4.

**Placeholder scan:** The shim is shown in full in Task 1 Step 2. The decoder change is the exact three rows in Task 2 Step 1. The emitter case is shown in full in Task 3 Step 2. The corpus program is shown in full in Task 4 Step 1. No "TBD"/"handle edge cases"/vague steps.

**Type consistency:** `hb_vmsh_pushblock( const unsigned char *, PHB_SYMB )` returning `int` is identical in `hbvmsh.h` (Task 1 Step 1), the implementation (Task 1 Step 2), and the IR `declare i32 @hb_vmsh_pushblock(i8*, %HB_SYMB*)` (Task 3 Step 1) and call site (Task 3 Step 2). The shim returns `0` (Task 1 Step 2), matching the spec's "always returns 0" and the emitter's no-action-request-check decision (Task 3 Step 2).

**Known risks:** (1) The `pSymbols` argument — the spec flags this as the one real risk. Task 3 Step 2 passes `@symbols_table` base; the implementer should confirm against the existing `hb_vmsh_pushsymbol` emitter case (`case HB_P_PUSHSYM:`) that `@symbols_table` is the correct, already-relocated table. The corpus's detached-local and nested-block cases (Task 4) exercise it; the diff-against-C-backend is the gate. (2) The unity-file build gotcha — after editing `hvm.c` you must `touch src/vm/hvmall.c`, called out in the build-environment section and Task 1 Step 4. (3) The `harbour.exe` relink — Task 3 Step 3 includes the standalone `touch src/main/harbour.c` fallback.
