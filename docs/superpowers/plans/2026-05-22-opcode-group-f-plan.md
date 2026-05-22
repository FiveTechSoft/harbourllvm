# Opcode Group F — SWITCH — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so a `SWITCH` statement with a runtime selector emits straight-line native code (an LLVM `switch` instruction) instead of falling back to the `hb_vmExecute` interpreter.

**Architecture:** `HB_P_SWITCH` is the single opcode in group F. It is variable-length with an embedded jump table, so group F touches three places: a new `hb_vmsh_switchidx` shim in `src/vm/hvm.c` that reuses `hb_vmSwitch`'s exact match logic and returns the matched case index; a decoder change in `src/compiler/hb_pcdec.c` (a new `HB_PCK_SWITCH` decode kind that computes the variable instruction length); and an emitter `case` in `src/compiler/genllvm.c` that emits the case table as a constant, calls the shim, and emits a real LLVM `switch`.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-22-opcode-group-f-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode groups A–E.

---

## Build environment

Every build/test step uses:

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

Build Harbour from the repo root with `./win-make.exe`. **Known build note:** `win-make` aborts at a pre-existing, unrelated `harbour-32-x64.dll` link failure — predates this work, irrelevant. Static libraries and `bin/win/mingw64/harbour.exe` build BEFORE that step. The `harbour.exe` Makefile tracks only `.o` deps — if a `genllvm.c` change did not relink `harbour.exe`, run a standalone `touch src/main/harbour.c` then `./win-make.exe`. `hbmk2`: `bin/win/mingw64/hbmk2.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

**Shell note:** do not combine `cd` with output redirection (`>`) in one command — Claude Code blocks it. Run a standalone `cd` first (the Bash working directory persists between calls), or redirect with an absolute path.

---

## `HB_P_SWITCH` instruction layout (reference)

`[ HB_P_SWITCH ][ caseCount lo ][ caseCount hi ][ entry 0 ][ entry 1 ] ... [ entry N-1 ]`

`caseCount` is a 2-byte LE unsigned (`HB_PCODE_MKUSHORT(&pCode[1])`). Each entry = a literal-push opcode followed by a jump opcode:
- literal: `HB_P_PUSHLONG` (5 bytes) | `HB_P_PUSHSTRSHORT` (`2 + b[1]` bytes) | `HB_P_PUSHNIL` (1 byte — default / `OTHERWISE`);
- jump: `HB_P_JUMPNEAR` (2 bytes) | `HB_P_JUMP` (3 bytes) | `HB_P_JUMPFAR` (4 bytes).

Total instruction length = `3 + Σ(literal bytes + jump bytes)`. This is exactly what `hb_vmSwitch` (in `src/vm/hvm.c`) walks; the implementer MUST read `hb_vmSwitch` and the `case HB_P_SWITCH:` in `hb_vmExecute` and reproduce the byte arithmetic verbatim.

---

### Task 1: The `hb_vmsh_switchidx` shim

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 1 declaration)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 1 shim to the group E shim block)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_f.c`

- [ ] **Step 1: Add the declaration to `hbvmsh.h`**

Inside the existing `HB_EXTERN_BEGIN ... HB_EXTERN_END` block in `include/hbvmsh.h`, after the group E declarations, add:

```c
/* --- group F: SWITCH --- */
/* Walk the SWITCH case table (pTable points at the first case entry, i.e.
 * the bytes after the 3-byte HB_P_SWITCH header). Returns the 0-based index
 * of the first matching case, or caseCount if none matches. Pops the switch
 * selector from the stack, exactly as the interpreter's hb_vmSwitch does.
 * Unlike the other hb_vmsh_* shims this returns a case index, not an action
 * request — it runs no user code and cannot set one. */
extern HB_EXPORT int hb_vmsh_switchidx( const unsigned char * pTable,
                                        int caseCount );
```

- [ ] **Step 2: Append the shim implementation to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group E shim block, add:

```c
/* --- group F: SWITCH --- */

HB_EXPORT int hb_vmsh_switchidx( const unsigned char * pTable, int caseCount )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pSwitch = hb_vmSwitchGet();
   int      iIdx    = caseCount;   /* default: no case matched */

   if( pSwitch )
   {
      int i;
      for( i = 0; i < caseCount; ++i )
      {
         HB_BOOL fFound = HB_FALSE;

         /* literal-push opcode of this case entry */
         switch( pTable[ 0 ] )
         {
            case HB_P_PUSHLONG:
               if( HB_IS_NUMINT( pSwitch ) )
                  fFound = HB_ITEM_GET_NUMINTRAW( pSwitch ) ==
                           HB_PCODE_MKLONG( &pTable[ 1 ] );
               pTable += 5;
               break;
            case HB_P_PUSHSTRSHORT:
               if( HB_IS_STRING( pSwitch ) )
                  fFound = ( HB_SIZE ) pTable[ 1 ] - 1 == pSwitch->item.asString.length &&
                           memcmp( pSwitch->item.asString.value, &pTable[ 2 ],
                                   pSwitch->item.asString.length ) == 0;
               pTable += 2 + pTable[ 1 ];
               break;
            case HB_P_PUSHNIL:
               fFound = HB_TRUE;
               pTable++;
               break;
         }

         /* jump opcode of this case entry — skip its bytes only */
         switch( pTable[ 0 ] )
         {
            case HB_P_JUMPNEAR: pTable += 2; break;
            case HB_P_JUMP:     pTable += 3; break;
            case HB_P_JUMPFAR:  pTable += 4; break;
         }

         if( fFound )
         {
            iIdx = i;
            break;
         }
      }
   }

   hb_stackPop();
   return iIdx;
}
```

Before writing this, read `hb_vmSwitch` and the `case HB_P_SWITCH:` in `src/vm/hvm.c` and confirm: the literal-compare predicates (`HB_IS_NUMINT` + `HB_ITEM_GET_NUMINTRAW` raw equality for `PUSHLONG`; `HB_IS_STRING` + length + `memcmp` for `PUSHSTRSHORT`; always-true for `PUSHNIL`), the per-entry byte advances, and the trailing `hb_stackPop()`. `hb_vmSwitchGet` is `static` in `hvm.c` — reachable from the shim block. If `hb_vmSwitch` skips the whole match loop when the selector is absent, the `if( pSwitch )` guard above reproduces that exactly (no case, not even the default, matches). The shim starts with `HB_STACK_TLS_PRELOAD` like every other shim. It does NOT return an action request — it returns the index.

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_f.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p = ( void * ) hb_vmsh_switchidx;
   printf( "group F shim linkable: %p\n", p );
   return 0;
}
```

- [ ] **Step 4: Rebuild Harbour and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/shim_smoke_f.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -o build/shim_smoke_f.exe
./build/shim_smoke_f.exe
```

Expected: `hvm.c` compiles with the new shim (no errors); `libhbvm.a` rebuilds before the known unrelated DLL-link abort; `shim_smoke_f.exe` prints a non-null pointer. (`--noinhibit-exec` on the `gcc` link is acceptable if the known incomplete-DLL situation requires it.)

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_f.c
git commit -m "vm: add group F op shim hb_vmsh_switchidx (SWITCH)"
```

---

### Task 2: Decoder — variable length for `HB_P_SWITCH`

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.h`
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`
- Modify: `c:\HarbourLLVM\core\tests\llvm\pcdectest.c`

`HB_P_SWITCH` is currently `{ HB_PCK_UNKNOWN, 0, HB_FALSE }`, which makes `hb_pcodeInstrLen` return 0 and forces a whole-function fallback. Task 2 gives it a real length.

- [ ] **Step 1: Add the `HB_PCK_SWITCH` decode kind**

In `src/compiler/hb_pcdec.h`, the `HB_PCKIND` enum currently has values like `HB_PCK_FIXED`, `HB_PCK_STR1/STR2/STR3`, `HB_PCK_VARBLOCK`, `HB_PCK_UNKNOWN` (read the actual enum). Add a new value `HB_PCK_SWITCH` immediately before `HB_PCK_UNKNOWN`:

```c
   HB_PCK_SWITCH,    /* HB_P_SWITCH: header + variable case table */
```

- [ ] **Step 2: Compute the `HB_P_SWITCH` length in `hb_pcodeInstrLen`**

In `src/compiler/hb_pcdec.c`, `hb_pcodeInstrLen` has a `switch( pInfo->kind )` (read it). Add a `case HB_PCK_SWITCH:` arm that walks the case table:

```c
      case HB_PCK_SWITCH:
      {
         /* [opcode][caseCount:2][N entries]; each entry = literal + jump */
         HB_USHORT       count = HB_PCODE_MKUSHORT( &pCode[ 1 ] );
         const HB_BYTE * p     = pCode + 3;
         HB_USHORT       k;
         for( k = 0; k < count; ++k )
         {
            switch( p[ 0 ] )          /* literal-push opcode */
            {
               case HB_P_PUSHLONG:      p += 5;             break;
               case HB_P_PUSHSTRSHORT:  p += 2 + p[ 1 ];    break;
               case HB_P_PUSHNIL:       p += 1;             break;
               default:                 return 0;  /* malformed */
            }
            switch( p[ 0 ] )          /* jump opcode */
            {
               case HB_P_JUMPNEAR:      p += 2;  break;
               case HB_P_JUMP:          p += 3;  break;
               case HB_P_JUMPFAR:       p += 4;  break;
               default:                 return 0;  /* malformed */
            }
         }
         return ( HB_SIZE ) ( p - pCode );
      }
```

This must exactly mirror `hb_vmSwitch`'s byte arithmetic — verify against `src/vm/hvm.c`.

- [ ] **Step 3: Flip the `HB_P_SWITCH` table row**

In `src/compiler/hb_pcdec.c`'s `hb_pcInfo[]`, change the `HB_P_SWITCH` (133) row from `{ HB_PCK_UNKNOWN, 0, HB_FALSE }` to `{ HB_PCK_SWITCH, 0, HB_TRUE }`. Update the file-header comment's "in scope" list to add `HB_P_SWITCH`.

- [ ] **Step 4: Add a decoder test**

In `tests/llvm/pcdectest.c`, after the existing tests in `main()` and before `return 0;`, add a test that builds a minimal `HB_P_SWITCH` instruction and checks `hb_pcodeInstrLen` returns the right total. A 1-case switch with a `PUSHLONG`+`JUMP` entry: header 3 + entry (5 + 3) = 11 bytes:

```c
   {
      /* HB_P_SWITCH, count=1, [ PUSHLONG 7 , JUMP +0 ] -> total 11 bytes */
      HB_BYTE sw[] = {
         HB_P_SWITCH, 1, 0,
         HB_P_PUSHLONG, 7, 0, 0, 0,
         HB_P_JUMP, 0, 0
      };
      HB_SIZE len = hb_pcodeInstrLen( sw );
      assert( len == 11 );
      printf( "pcdec: HB_P_SWITCH length correct\n" );
   }
```

- [ ] **Step 5: Rebuild and run the decoder test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: `libhbcplr.a` rebuilds before the known DLL abort; `pcdectest.exe` prints all prior lines plus `pcdec: HB_P_SWITCH length correct`.

- [ ] **Step 6: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.h src/compiler/hb_pcdec.c tests/llvm/pcdectest.c
git commit -m "compiler: decode HB_P_SWITCH variable length (group F)"
```

---

### Task 3: Emitter case for `HB_P_SWITCH`

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Add the `declare` line**

In the module-header emission of `genllvm.c`, where the group E `declare i32 @hb_vmsh_*` lines are emitted, add:

```c
   fprintf( yyc, "declare i32 @hb_vmsh_switchidx(i8*, i32)\n" );
```

- [ ] **Step 2: Add the `HB_P_SWITCH` emitter case**

In `hb_llvmSLEmitBody`'s opcode `switch`, add a `case HB_P_SWITCH:`. It does NOT use `HB_EMIT_NOARG_SHIM` and does NOT use the standard action-request check (the shim returns a case index, not an action request — it runs no user code). It:

1. **Decodes the case table** — `caseCount = HB_PCODE_MKUSHORT(&pCode[pos+1])`; walk the `caseCount` entries starting at `pos+3`, and for each entry record the target offset. Within an entry: the literal opcode is at table position `t`, advance past it (`PUSHLONG` +5, `PUSHSTRSHORT` +`2+b[1]`, `PUSHNIL` +1) to the jump opcode at position `j`; the jump's absolute function offset is `pos + 3 + j`; read the displacement (`JUMPNEAR` = `(signed char) b[j+1]`; `JUMP` = `HB_PCODE_MKSHORT(&b[j+1])`; `JUMPFAR` = `HB_PCODE_MKINT24(&b[j+1])` — reuse the sign rules the existing Plan 3 jump emitter uses); the case target offset is `(pos + 3 + j) + displacement`. Advance past the jump opcode to the next entry.

2. **Emits the case table as a private constant** — emit the raw bytes of the table (from `pCode[pos+3]` for `instrLen - 3` bytes, where `instrLen` is `hb_pcodeInstrLen(&pCode[pos])`) as a `private constant [K x i8] c"\HH\HH..."` global, using the existing per-byte hex-escape helper the emitter already uses for pcode/string constants. Give it a unique name, e.g. `@.switchtab.<pos>`.

3. **Emits the shim call** — in the `i<pos>` block:
   `%r<pos> = call i32 @hb_vmsh_switchidx(i8* getelementptr([K x i8], [K x i8]* @.switchtab.<pos>, i32 0, i32 0), i32 <caseCount>)`.

4. **Emits an LLVM `switch`** — `switch i32 %r<pos>, label %i<fallthrough> [ i32 0, label %i<t0>  i32 1, label %i<t1> ... ]`, where `t<k>` is case `k`'s decoded target offset and `<fallthrough>` is the offset of the instruction immediately after the whole `HB_P_SWITCH` (i.e. `pos + instrLen`; if that is `>= nPCSize` use `%epilogue`, matching the existing dangling-label guard). The shim's "no match" return value `caseCount` is not in `[0, caseCount-1]`, so it routes to the `switch` default label — make the default label the `<fallthrough>` block. This block terminator IS the `switch`; do not also emit a `br`.

The string-constant emission (step 2) and the jump-displacement decoding (step 1) reuse helpers already in `genllvm.c` — study how the existing `HB_P_PUSHSTR` case emits a byte-array constant and how the `HB_P_JUMP*` cases decode displacements, and reuse those.

- [ ] **Step 3: Rebuild and check the IR for a SWITCH program**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
```
Then (separate command — the build note's relink may be needed: standalone `touch src/main/harbour.c` then `./win-make.exe`):
```bash
cd c:/HarbourLLVM/core
printf 'function Main()\n   local n := 2\n   switch n\n   case 1\n      ? "one"\n   case 2\n      ? "two"\n   otherwise\n      ? "other"\n   end\n   return nil\n' > build/swtmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3f build/swtmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3f.ll -o NUL
grep -c "hb_vmsh_switchidx" build/t3f.ll
```
Then separately:
```bash
cd c:/HarbourLLVM/core
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3f.ll | grep -c "call void @hb_vmExecute"
```

Expected: clean rebuild; `t3f.ll` passes the LLVM verifier (only the benign `-Woverride-module` warning); `grep -c hb_vmsh_switchidx` ≥ 1; the `awk|grep` count is `0` (`HB_FUN_MAIN` no longer falls back). If the compiler constant-folds `switch n` because `n` is a constant, change `swtmp.prg` so the selector is non-constant — e.g. read it from a function: `local n := Val( "2" )`.

- [ ] **Step 4: Run it end-to-end**

```bash
cd c:/HarbourLLVM/core
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/t3frun build/swtmp.prg
./build/t3frun.exe
```

Expected: prints `two` (selector 2 takes `case 2`).

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for HB_P_SWITCH (group F)"
```

---

### Task 4: Corpus — verify SWITCH statements

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\switchstmt.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh`

- [ ] **Step 1: Write the new corpus program**

`tests/llvm/switchstmt.prg` — the selector must be non-constant so the compiler emits `HB_P_SWITCH` rather than folding to a `JUMP`. Use `Val()` of a string to make each selector runtime-valued, and exercise integer cases, a string case, and the `OTHERWISE` default:

```harbour
function Main()
   Classify( Val( "1" ) )
   Classify( Val( "2" ) )
   Classify( Val( "9" ) )
   ClassifyStr( "x" )
   ClassifyStr( "z" )
   return nil

function Classify( n )
   switch n
   case 1
      ? "one"
   case 2
      ? "two"
   otherwise
      ? "many"
   end
   return nil

function ClassifyStr( c )
   switch c
   case "x"
      ? "ex"
   otherwise
      ? "?"
   end
   return nil
```

- [ ] **Step 2: Update `run.sh`'s straight-line assertion set**

`tests/llvm/run.sh` asserts which programs straight-line vs fall back. Add `switchstmt` to the straight-lined-expected set (checked like `arith`/`loop`: must contain `hb_vmsh_`, must NOT contain `call void @hb_vmExecute` in `HB_FUN_MAIN`). Keep `fallback` in the fallback set. Study the current `run.sh` straight-line/fallback assertion logic and integrate cleanly.

Note: if `switchstmt.prg` legitimately falls back because it emits an opcode still outside the A–F subset, do NOT force it into the straight-lined set — leave it where it lands and report it in Task 4 Step 5. The diff-against-C-backend is the correctness gate; a correct fallback is acceptable.

- [ ] **Step 3: Run the new program end-to-end**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/gf_switch tests/llvm/switchstmt.prg
./build/gf_switch.exe > build/gf_switch_ll.out 2>&1
bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/gf_switch_c tests/llvm/switchstmt.prg
./build/gf_switch_c.exe > build/gf_switch_c.out 2>&1
diff build/gf_switch_c.out build/gf_switch_ll.out
```

Expected: `diff` reports no differences — the straight-line backend output is byte-identical to the C backend. (`switchstmt.prg` prints `one`, `two`, `many`, `ex`, `?`.)

- [ ] **Step 4: Confirm the program straight-lined**

```bash
cd c:/HarbourLLVM/core
grep -c "hb_vmsh_switchidx" build/gf_switch.ll
awk '/^define.*@HB_FUN_CLASSIFY\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/gf_switch.ll | grep -c "call void @hb_vmExecute"
```

Expected: `hb_vmsh_switchidx` count ≥ 1; the `HB_FUN_CLASSIFY` fallback count is `0` (the function containing the `SWITCH` straight-lined). (If non-zero fallback, report it — see Step 2's note.)

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gf
```

Expected: `run.sh` ends with `RESULT: all programs validated and matched the C backend`, and the report shows `switchstmt` straight-lined (or, if it fell back, that it still MATCHes the C backend — report which).

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/switchstmt.prg tests/llvm/run.sh
git commit -m "test: corpus for group F — SWITCH statements straight-lined"
```

---

## Self-Review

**Spec coverage:** The spec's three required changes are covered — Task 1 the `hb_vmsh_switchidx` shim, Task 2 the decoder (`HB_PCK_SWITCH` kind + variable-length computation + the table-row flip), Task 3 the emitter case (table constant + shim call + LLVM `switch`), Task 4 the SWITCH corpus. The spec's testing section (`switchstmt.prg` with integer cases, a string case, an `OTHERWISE`, diff against the C backend) is Task 4.

**Placeholder scan:** The shim is shown in full in Task 1 Step 2. The decoder length arm is shown in full in Task 2 Step 2. Task 3 Step 2 describes the emitter case as four concrete sub-steps anchored to existing `genllvm.c` machinery (the `HB_P_PUSHSTR` byte-array-constant emission and the `HB_P_JUMP*` displacement decoding) rather than literal code, because the exact local variable names and helper signatures in `genllvm.c` are not in this plan's context — but every value to compute (caseCount, per-case target offset, table length, fallthrough offset) and every IR line to emit (the `declare`, the constant, the `call`, the `switch`) is named explicitly. This mirrors how the group A–E plans anchored their emitter steps to existing cases. No "TBD"/"handle edge cases"/vague steps.

**Type consistency:** `hb_vmsh_switchidx( const unsigned char *, int )` returning `int` is identical in `hbvmsh.h` (Task 1 Step 1), the implementation (Task 1 Step 2), and the IR `declare i32 @hb_vmsh_switchidx(i8*, i32)` (Task 3 Step 1). The new `HB_PCK_SWITCH` enum value (Task 2 Step 1) is used by the `hb_pcodeInstrLen` arm (Task 2 Step 2) and the `hb_pcInfo[]` row (Task 2 Step 3).

**Known risks:** (1) the decoder length walk must exactly match `hb_vmSwitch`'s byte arithmetic — Task 2 Step 2 is explicitly anchored to `hb_vmSwitch` and the decoder unit test (Step 4) checks a known-length instruction. (2) `hb_vmsh_switchidx` returning an index rather than an action request is a deliberate departure from the other shims — Task 1 and Task 3 both call it out so the emitter does not wrongly add an action-request check. (3) The corpus selector must be non-constant or the compiler folds the `SWITCH` to a plain `JUMP` and `HB_P_SWITCH` is never emitted — Task 3 Step 3 and Task 4 Step 1 both use `Val()` to force a runtime selector.
