# Opcode Group C — RDD fields, memvars, aliases — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so database-field access, memory-variable access, undeclared-variable access, aliased access, and workarea selection emit straight-line native code instead of falling back to the `hb_vmExecute` interpreter.

**Architecture:** Pure replication of the group A/B pattern. 16 pcode opcodes (14 distinct shims — two `*NEAR` opcodes reuse their non-NEAR shim) each get: a `hb_vmsh_*` shim appended to `src/vm/hvm.c` (mirroring its `hb_vmExecute` switch case, returning the action request), `fSupported = HB_TRUE` in the `hb_pcInfo[]` decoder table, and an emitter `case` in `genllvm.c`. The 13 symbol-operand opcodes pass a `PHB_SYMB *` from the module `@symbols_table` exactly as the existing `HB_P_PUSHSYM` emitter case does.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-21-opcode-group-c-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode groups A, B — `harbour -GL` emits straight-line IR for a functional subset plus FOR loops, compound assignment, arrays and hashes, with a whole-function interpreter fallback.

---

## Build environment

Every build/test step uses:

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

Build Harbour from the repo root with `./win-make.exe`. **Known build note:** `win-make` aborts at a pre-existing, unrelated `harbour-32-x64.dll` link failure (`hashfunc.o`/`pvalue.o` undefined symbols) — predates this work, irrelevant (the LLVM backend links static `.a` libs). The static libraries and `bin/win/mingw64/harbour.exe` build BEFORE that step. The `harbour.exe` Makefile tracks only `.o` deps — if a `genllvm.c` change did not relink `harbour.exe`, run `touch src/main/harbour.c && ./win-make.exe`. Compiler: `bin/win/mingw64/harbour.exe`. `hbmk2`: `bin/win/mingw64/hbmk2.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

---

## The 16 opcodes (reference)

Verified against the `hb_vmExecute` switch in `src/vm/hvm.c`. Each interpreter `case` is one helper call (push) or a helper call plus `hb_stackPop()` (pop). The symbol-operand cases resolve the operand as `pSymbols + index`.

| Opcode | Len | Interpreter case body | Shim |
|--------|-----|-----------------------|------|
| `HB_P_PUSHALIAS` | 1 | `hb_vmPushAlias();` | `hb_vmsh_pushalias` |
| `HB_P_POPALIAS` | 1 | `hb_vmPopAlias();` | `hb_vmsh_popalias` |
| `HB_P_SWAPALIAS` | 1 | `hb_vmSwapAlias();` | `hb_vmsh_swapalias` |
| `HB_P_PUSHFIELD` | 3 | `hb_rddGetFieldValue( hb_stackAllocItem(), pSym );` | `hb_vmsh_pushfield` |
| `HB_P_POPFIELD` | 3 | `hb_rddPutFieldValue( hb_stackItemFromTop(-1), pSym ); hb_stackPop();` | `hb_vmsh_popfield` |
| `HB_P_PUSHMEMVAR` | 3 | `hb_memvarGetValue( hb_stackAllocItem(), pSym );` | `hb_vmsh_pushmemvar` |
| `HB_P_PUSHMEMVARREF` | 3 | `hb_memvarGetRefer( hb_stackAllocItem(), pSym );` | `hb_vmsh_pushmemvarref` |
| `HB_P_POPMEMVAR` | 3 | `hb_memvarSetValue( pSym, hb_stackItemFromTop(-1) ); hb_stackPop();` | `hb_vmsh_popmemvar` |
| `HB_P_PUSHVARIABLE` | 3 | `hb_vmPushVariable( pSym );` | `hb_vmsh_pushvariable` |
| `HB_P_POPVARIABLE` | 3 | `hb_memvarSetValue( pSym, hb_stackItemFromTop(-1) ); hb_stackPop();` | `hb_vmsh_popvariable` |
| `HB_P_PUSHALIASEDFIELD` | 3 | `hb_vmPushAliasedField( pSym );` | `hb_vmsh_pushaliasedfield` |
| `HB_P_PUSHALIASEDFIELDNEAR` | 2 | `hb_vmPushAliasedField( pSym );` | `hb_vmsh_pushaliasedfield` (shared) |
| `HB_P_POPALIASEDFIELD` | 3 | `hb_vmPopAliasedField( pSym );` | `hb_vmsh_popaliasedfield` |
| `HB_P_POPALIASEDFIELDNEAR` | 2 | `hb_vmPopAliasedField( pSym );` | `hb_vmsh_popaliasedfield` (shared) |
| `HB_P_PUSHALIASEDVAR` | 3 | `hb_vmPushAliasedVar( pSym );` | `hb_vmsh_pushaliasedvar` |
| `HB_P_POPALIASEDVAR` | 3 | `hb_vmPopAliasedVar( pSym );` | `hb_vmsh_popaliasedvar` |

`pSym` above = `pSymbols + <index operand>`. The implementer MUST open `src/vm/hvm.c`, confirm each `case` body and helper name verbatim, and reproduce it. `HB_P_POPVARIABLE`'s case is wrapped in a `{ }` block and historically had commented-out field-fallback logic — reproduce only the live code (the `hb_memvarSetValue` + `hb_stackPop()` it actually executes).

---

### Task 1: The 14 RDD/memvar/alias op shims

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 14 declarations)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 14 shim implementations to the existing group B shim block)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_c.c`

- [ ] **Step 1: Add the declarations to `hbvmsh.h`**

Inside the existing `HB_EXTERN_BEGIN ... HB_EXTERN_END` block in `include/hbvmsh.h`, after the group B declarations, add:

```c
/* --- group C: RDD fields, memvars, aliases --- */
extern HB_EXPORT int hb_vmsh_pushalias( void );
extern HB_EXPORT int hb_vmsh_popalias( void );
extern HB_EXPORT int hb_vmsh_swapalias( void );
extern HB_EXPORT int hb_vmsh_pushfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushmemvar( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushmemvarref( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popmemvar( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushvariable( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popvariable( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushaliasedfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popaliasedfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushaliasedvar( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popaliasedvar( PHB_SYMB pSym );
```

`PHB_SYMB` is already in scope via `hbvmpub.h` (the header `hbvmsh.h` already includes), the same type the existing `hb_vmsh_pushsymbol` shim uses.

- [ ] **Step 2: Append the 14 shim implementations to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group B shim block, add the 14 implementations. Each starts with `HB_STACK_TLS_PRELOAD` as its first statement and ends with `return ( int ) hb_stackGetActionRequest();` — identical shape to the group A/B shims. The 14 in full:

```c
/* --- group C: RDD fields, memvars, aliases --- */

HB_EXPORT int hb_vmsh_pushalias( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushAlias();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_popalias( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPopAlias();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_swapalias( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmSwapAlias();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushfield( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_rddGetFieldValue( hb_stackAllocItem(), pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_popfield( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_rddPutFieldValue( hb_stackItemFromTop( -1 ), pSym );
   hb_stackPop();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushmemvar( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_memvarGetValue( hb_stackAllocItem(), pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushmemvarref( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_memvarGetRefer( hb_stackAllocItem(), pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_popmemvar( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_memvarSetValue( pSym, hb_stackItemFromTop( -1 ) );
   hb_stackPop();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushvariable( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushVariable( pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_popvariable( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_memvarSetValue( pSym, hb_stackItemFromTop( -1 ) );
   hb_stackPop();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushaliasedfield( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushAliasedField( pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_popaliasedfield( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPopAliasedField( pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushaliasedvar( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushAliasedVar( pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_popaliasedvar( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPopAliasedVar( pSym );
   return ( int ) hb_stackGetActionRequest();
}
```

Before writing these, confirm against `hvm.c`: the exact helper names and that each is reachable from the end of `hvm.c` (`hb_vmPushAlias` / `hb_vmPopAlias` / `hb_vmSwapAlias` / `hb_vmPushAliasedField` / `hb_vmPopAliasedField` / `hb_vmPushAliasedVar` / `hb_vmPopAliasedVar` / `hb_vmPushVariable` are `static` in `hvm.c` — fine, same file; `hb_rddGetFieldValue` / `hb_rddPutFieldValue` / `hb_memvarSetValue` are exported, `hb_memvarGetValue` / `hb_memvarGetRefer` are extern — all callable). If any `case` body does more than what the reference table shows, reproduce it verbatim.

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_c.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 7 ];
   p[ 0 ] = ( void * ) hb_vmsh_pushalias;
   p[ 1 ] = ( void * ) hb_vmsh_pushfield;
   p[ 2 ] = ( void * ) hb_vmsh_popfield;
   p[ 3 ] = ( void * ) hb_vmsh_pushmemvar;
   p[ 4 ] = ( void * ) hb_vmsh_popmemvar;
   p[ 5 ] = ( void * ) hb_vmsh_pushaliasedfield;
   p[ 6 ] = ( void * ) hb_vmsh_pushvariable;
   printf( "group C shims linkable: %p %p %p %p %p %p %p\n",
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
gcc tests/llvm/shim_smoke_c.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -o build/shim_smoke_c.exe
./build/shim_smoke_c.exe
```

Expected: `hvm.c` compiles with the 14 new shims (no errors); `libhbvm.a` rebuilds before the known unrelated DLL-link abort; `shim_smoke_c.exe` prints seven non-null pointers. (`--noinhibit-exec` on the `gcc` link is acceptable if the known incomplete-DLL situation requires it — the shim symbols themselves must resolve.)

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_c.c
git commit -m "vm: add group C op shims (RDD fields, memvars, aliases)"
```

---

### Task 2: Decoder table — mark the 16 opcodes supported

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`

The 16 opcodes already have correct `kind` (`HB_PCK_FIXED`) and `nLen` (1, 2, or 3) in `hb_pcInfo[]`. Only the `fSupported` field changes from `HB_FALSE` to `HB_TRUE`.

- [ ] **Step 1: Flip the flags**

In `src/compiler/hb_pcdec.c`, change the third field from `HB_FALSE` to `HB_TRUE` for exactly these 16 table rows (identify each by its `/* NN HB_P_<name> */` comment):

`HB_P_POPALIAS` (74), `HB_P_POPALIASEDFIELD` (75), `HB_P_POPALIASEDFIELDNEAR` (76), `HB_P_POPALIASEDVAR` (77), `HB_P_POPFIELD` (78), `HB_P_POPMEMVAR` (81), `HB_P_POPVARIABLE` (83), `HB_P_PUSHALIAS` (85), `HB_P_PUSHALIASEDFIELD` (86), `HB_P_PUSHALIASEDFIELDNEAR` (87), `HB_P_PUSHALIASEDVAR` (88), `HB_P_PUSHFIELD` (91), `HB_P_PUSHMEMVAR` (98), `HB_P_PUSHMEMVARREF` (99), `HB_P_PUSHVARIABLE` (109), `HB_P_SWAPALIAS` (119).

Do NOT change `kind` or `nLen` on any row. Do NOT touch any opcode not in this list.

- [ ] **Step 2: Update the file-header comment**

`hb_pcdec.c` has a header comment listing the supported subset. Add the 16 group C opcode names to the "in scope (fSupported = HB_TRUE)" list so the comment matches the table.

- [ ] **Step 3: Rebuild and verify**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: `libhbcplr.a` rebuilds before the known DLL abort; `pcdectest.exe` still prints `pcdec: 4 instructions, all boundaries correct` and `pcdec: jump analysis correct`. `git diff` shows exactly 16 `HB_FALSE`→`HB_TRUE` flips in the table.

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c
git commit -m "compiler: mark group C opcodes supported in the decoder table"
```

---

### Task 3: Emitter cases for the 16 opcodes

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Add the `declare` lines**

In the module-header emission of `genllvm.c`, where the group B `declare i32 @hb_vmsh_*` lines are emitted, add the 14 distinct shims:

```c
   fprintf( yyc, "declare i32 @hb_vmsh_pushalias()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popalias()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_swapalias()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushfield(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popfield(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushmemvar(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushmemvarref(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popmemvar(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushvariable(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popvariable(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushaliasedfield(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popaliasedfield(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushaliasedvar(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popaliasedvar(%%HB_SYMB*)\n" );
```

(The `%%` is the `fprintf` escape for a literal `%`, so the emitted IR reads `%HB_SYMB*` — matching the existing `declare ... @hb_vmsh_pushsymbol(%HB_SYMB*)` line.)

- [ ] **Step 2: Add the emitter cases**

In `hb_llvmSLEmitBody`'s opcode `switch`, add cases for the 16 opcodes.

The **3 no-operand opcodes** (`HB_P_PUSHALIAS`→`pushalias`, `HB_P_POPALIAS`→`popalias`, `HB_P_SWAPALIAS`→`swapalias`) follow the existing `HB_EMIT_NOARG_SHIM` pattern that groups A/B used — add three invocations before the `#undef HB_EMIT_NOARG_SHIM`.

The **13 symbol-operand opcodes** mirror the existing `HB_P_PUSHSYM` emitter case. That case decodes a 2-byte symbol index and emits a `getelementptr` into `@symbols_table` passed to `hb_vmsh_pushsymbol`. For each group C symbol-operand opcode, do the same:
- decode the symbol index — `HB_PCODE_MKUSHORT(&pCode[pos+1])` for the 2-byte forms (`PUSHFIELD`, `POPFIELD`, `PUSHMEMVAR`, `PUSHMEMVARREF`, `POPMEMVAR`, `PUSHVARIABLE`, `POPVARIABLE`, `PUSHALIASEDFIELD`, `POPALIASEDFIELD`, `PUSHALIASEDVAR`, `POPALIASEDVAR`); `pCode[pos+1]` (1 byte, unsigned) for the 2 `*NEAR` forms (`PUSHALIASEDFIELDNEAR`, `POPALIASEDFIELDNEAR`);
- emit `%symOFF = getelementptr [K x %HB_SYMB], [K x %HB_SYMB]* @symbols_table, i32 0, i32 <idx>` (reuse exactly the `getelementptr` form the `HB_P_PUSHSYM` case emits — `K` is the module symbol count already known in `genllvm.c`);
- emit `%rOFF = call i32 @hb_vmsh_<name>(%HB_SYMB* %symOFF)` with the shim name from the reference table (`PUSHALIASEDFIELDNEAR` calls `hb_vmsh_pushaliasedfield`; `POPALIASEDFIELDNEAR` calls `hb_vmsh_popaliasedfield`);
- then the standard action-request `icmp`/`br` to `%epilogue` / next block.

Study the existing `HB_P_PUSHSYM` case for the exact `getelementptr` text and SSA-naming convention, and reproduce it. Every case ends with the same action-request check + branch the group A/B cases use, via the existing next-block-label helper (`label %epilogue` when `nextOff >= nPCSize`).

- [ ] **Step 3: Rebuild and check the IR for a memvar program**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
# ensure harbour.exe relinked; if not: touch src/main/harbour.c && ./win-make.exe
printf 'function Main()\n   private pvar\n   pvar := 42\n   ? pvar\n   return nil\n' > build/mvtmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3c build/mvtmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3c.ll -o NUL
grep -c "hb_vmsh_pushmemvar\|hb_vmsh_popmemvar" build/t3c.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3c.ll | grep -c "call void @hb_vmExecute"
```

Expected: clean rebuild; `t3c.ll` passes the LLVM verifier (only the benign `-Woverride-module` warning); the memvar-shim grep is ≥ 1; the `awk|grep` count is `0` (`HB_FUN_MAIN` no longer falls back).

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for group C opcodes (RDD fields, memvars, aliases)"
```

---

### Task 4: Corpus — verify memvars and fields

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\memvar.prg`
- Create: `c:\HarbourLLVM\core\tests\llvm\field.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh`

- [ ] **Step 1: Write the new corpus programs**

`tests/llvm/memvar.prg` (exercises `PUSHMEMVAR` / `POPMEMVAR` via a PRIVATE variable):

```harbour
function Main()
   private pCount := 0
   pCount := pCount + 5
   pCount := pCount * 3
   ? pCount
   return nil
```

`tests/llvm/field.prg` (creates a temporary DBF, exercises `PUSHFIELD` / `POPFIELD` and the alias opcodes):

```harbour
function Main()
   local cFile := "build/grpc_test.dbf"
   dbCreate( cFile, { { "NAME", "C", 10, 0 }, { "AGE", "N", 3, 0 } } )
   dbUseArea( .T., , cFile, "T" )
   dbAppend()
   T->NAME := "Harbour"
   T->AGE  := 35
   ? T->NAME, T->AGE
   dbCloseArea()
   FErase( cFile )
   return nil
```

- [ ] **Step 2: Update `run.sh`'s straight-line assertion set**

`tests/llvm/run.sh` asserts which programs straight-line vs fall back. Add `memvar` and `field` to the straight-lined-expected set (checked like `arith`/`loop`: must contain `hb_vmsh_`, must NOT contain `call void @hb_vmExecute` in `HB_FUN_MAIN`). Keep `fallback` in the fallback set.

Note: `field.prg` calls `dbCreate`/`dbUseArea`/`dbAppend`/`dbCloseArea`/`FErase` — ordinary function calls (`HB_P_FUNCTION`/`DO`, already supported) — so it is expected to straight-line. If `field.prg` (or `memvar.prg`) legitimately falls back because it emits an opcode still outside the A+B+C subset, do NOT force it into the straight-lined set — leave it where it lands and report it in Task 4 Step 5. The diff-against-C-backend is the correctness gate; a correct fallback is acceptable.

- [ ] **Step 3: Run the new programs end-to-end**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
for p in memvar field; do
  env PATH="/c/Windows/System32:/c/Windows" \
    bin/win/mingw64/harbour.exe -GL -q -obuild/gc_$p tests/llvm/$p.prg
  ./build/gc_$p.exe > build/gc_${p}_ll.out 2>&1
  bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/gc_${p}_c tests/llvm/$p.prg
  ./build/gc_${p}_c.exe > build/gc_${p}_c.out 2>&1
  diff build/gc_${p}_c.out build/gc_${p}_ll.out && echo "$p: MATCH" || echo "$p: DIFF"
done
```

Expected: `memvar: MATCH` and `field: MATCH` — the straight-line backend output is byte-identical to the C backend. (`memvar.prg` prints `15`; `field.prg` prints `Harbour  35`.)

- [ ] **Step 4: Confirm the programs straight-lined**

```bash
cd c:/HarbourLLVM/core
for p in memvar field; do
  echo -n "$p hb_vmsh_ count: "; grep -c "hb_vmsh_" build/gc_$p.ll
  echo -n "$p hb_vmExecute in MAIN: "
  awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/gc_$p.ll | grep -c "call void @hb_vmExecute"
done
```

Expected: each program's `hb_vmsh_` count ≥ 1; each `hb_vmExecute in MAIN` count is `0`. (If a program shows a non-zero fallback count, report it — see Step 2's note.)

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gc
```

Expected: `run.sh` ends with `RESULT: all programs validated and matched the C backend`, and the report shows `memvar`/`field` straight-lined (or, if either fell back, that program still MATCHes the C backend — report which).

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/memvar.prg tests/llvm/field.prg tests/llvm/run.sh
git commit -m "test: corpus for group C — memvars and RDD fields straight-lined"
```

---

## Self-Review

**Spec coverage:** The spec's three opcode sub-groups (3 no-operand + 11 symbol-operand shims covering 13 opcodes — two `*NEAR` opcodes reuse two of those shims = 16 opcodes / 14 shims) are covered — Task 1 the 14 shims, Task 2 the 16 decoder flags, Task 3 the 16 emitter cases, Task 4 the memvar / field corpus. The spec's testing section (`memvar.prg`, `field.prg`, diff against the C backend, straight-line assertion) is Task 4.

**Placeholder scan:** All 14 shims are shown in full in Task 1 Step 2. Task 3's no-operand cases are anchored to the existing `HB_EMIT_NOARG_SHIM` pattern and the symbol-operand cases to the existing `HB_P_PUSHSYM` case — both concrete existing references, not placeholders. Task 2's flip list names all 16 opcodes with numeric values. No "TBD"/"handle edge cases"/vague steps.

**Type consistency:** Shim names and signatures are identical across `hbvmsh.h` (Task 1 Step 1), the implementations (Task 1 Step 2), and the IR `declare`s (Task 3 Step 1): the 3 no-operand shims are `int (void)` / `i32 ()`; the 11 symbol-operand shims are `int (PHB_SYMB)` / `i32 (%HB_SYMB*)`. The reference table, the Task 2 flip list, and the spec all name the same 16 opcodes; `PUSHALIASEDFIELDNEAR`/`POPALIASEDFIELDNEAR` consistently route to `hb_vmsh_pushaliasedfield`/`hb_vmsh_popaliasedfield`.

**Known risk:** `field.prg` exercises the RDD layer (creating/opening a real DBF). The group C opcodes themselves only forward to the runtime helpers; the DBF lifecycle is runtime behaviour, not codegen. The Task 4 diff-against-C-backend corpus is the gate that catches any deviation; `field.prg` cleans up its DBF with `FErase`.
