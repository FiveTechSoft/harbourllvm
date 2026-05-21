# Opcode Group A — FOR loops + compound assignment — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so FOR loops and the compound-assignment / inc-dec operators emit straight-line native code instead of falling back to the `hb_vmExecute` interpreter.

**Architecture:** Pure replication of the Plan 3 pattern. 28 pcode opcodes each get: a `hb_vmsh_*` shim appended to `src/vm/hvm.c` (mirroring its `hb_vmExecute` switch case, returning the action request), `fSupported = HB_TRUE` in the `hb_pcInfo[]` decoder table, and an emitter `case` in `genllvm.c`. No new files, no new mechanisms.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-21-opcode-group-a-design.md`.

**Prerequisites (done):** Plans 1-3 — `harbour -GL` emits straight-line IR for a functional opcode subset with a whole-function interpreter fallback.

---

## Build environment

Every build/test step uses:

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

Build Harbour from the repo root with `./win-make.exe`. Compiler: `bin/win/mingw64/harbour.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

---

## The 28 opcodes (reference)

Verified against the `hb_vmExecute` switch in `src/vm/hvm.c`. Each row: opcode, instruction length, interpreter `case` body to mirror.

| Opcode | Len | Interpreter case body (to mirror in the shim) |
|--------|-----|-----------------------------------------------|
| `HB_P_FORTEST` | 1 | `hb_vmForTest();` |
| `HB_P_INC` | 1 | `hb_vmInc( hb_stackItemFromTop( -1 ) );` |
| `HB_P_DEC` | 1 | `hb_vmDec( hb_stackItemFromTop( -1 ) );` |
| `HB_P_DUPLUNREF` | 1 | `hb_vmDuplUnRef();` |
| `HB_P_PUSHUNREF` | 1 | `hb_vmPushUnRef();` |
| `HB_P_PLUSEQPOP` | 1 | `pR = hb_itemUnRef( hb_stackItemFromTop(-2) ); hb_vmPlus( pR, pR, hb_stackItemFromTop(-1) ); hb_stackPop(); hb_stackPop();` |
| `HB_P_MINUSEQPOP` | 1 | as PLUSEQPOP with `hb_vmMinus` |
| `HB_P_MULTEQPOP` | 1 | as PLUSEQPOP with `hb_vmMult` |
| `HB_P_DIVEQPOP` | 1 | as PLUSEQPOP with `hb_vmDivide` |
| `HB_P_MODEQPOP` | 1 | as PLUSEQPOP with `hb_vmModulus` |
| `HB_P_EXPEQPOP` | 1 | as PLUSEQPOP with `hb_vmPower` |
| `HB_P_DECEQPOP` | 1 | `hb_vmDec( hb_itemUnRef( hb_stackItemFromTop(-1) ) ); hb_stackPop();` |
| `HB_P_INCEQPOP` | 1 | `hb_vmInc( hb_itemUnRef( hb_stackItemFromTop(-1) ) ); hb_stackPop();` |
| `HB_P_PLUSEQ` | 1 | `pR = hb_itemUnRef( hb_stackItemFromTop(-2) ); pV = hb_stackItemFromTop(-1); hb_vmPlus( pR, pR, pV ); hb_itemCopy( pV, pR ); hb_itemMove( hb_stackItemFromTop(-2), pV ); hb_stackDec();` |
| `HB_P_MINUSEQ` | 1 | as PLUSEQ with `hb_vmMinus` |
| `HB_P_MULTEQ` | 1 | as PLUSEQ with `hb_vmMult` |
| `HB_P_DIVEQ` | 1 | as PLUSEQ with `hb_vmDivide` |
| `HB_P_MODEQ` | 1 | as PLUSEQ with `hb_vmModulus` |
| `HB_P_EXPEQ` | 1 | as PLUSEQ with `hb_vmPower` |
| `HB_P_DECEQ` | 1 | `pR = hb_stackItemFromTop(-1); pV = hb_itemUnRef(pR); hb_vmDec(pV); pT = hb_stackAllocItem(); hb_itemCopy(pT,pV); hb_itemMove(pR,pT); hb_stackDec();` |
| `HB_P_INCEQ` | 1 | as DECEQ with `hb_vmInc` |
| `HB_P_PUSHLOCALREF` | 3 | `hb_vmPushLocalByRef( HB_PCODE_MKSHORT( &pCode[1] ) );` |
| `HB_P_PUSHSTATICREF` | 3 | `hb_vmPushStaticByRef( HB_PCODE_MKUSHORT( &pCode[1] ) );` |
| `HB_P_LOCALINC` | 3 | `pL = hb_stackLocalVariable( HB_PCODE_MKUSHORT(&pCode[1]) ); hb_vmInc( HB_IS_BYREF(pL) ? hb_itemUnRef(pL) : pL );` |
| `HB_P_LOCALDEC` | 3 | as LOCALINC with `hb_vmDec` |
| `HB_P_LOCALINCPUSH` | 3 | `pL = hb_stackLocalVariable(idx); if( HB_IS_BYREF(pL) ) pL = hb_itemUnRef(pL); hb_vmInc(pL); hb_itemCopy( hb_stackAllocItem(), pL );` |
| `HB_P_LOCALADDINT` | 5 | `hb_vmAddInt( hb_stackLocalVariable( HB_PCODE_MKUSHORT(&pCode[1]) ), HB_PCODE_MKSHORT(&pCode[3]) );` |
| `HB_P_LOCALNEARADDINT` | 4 | `hb_vmAddInt( hb_stackLocalVariable( pCode[1] ), HB_PCODE_MKSHORT(&pCode[2]) );` |

All helper functions used above (`hb_vmForTest`, `hb_vmInc`, `hb_vmDec`, `hb_vmDuplUnRef`, `hb_vmPushUnRef`, `hb_vmPushLocalByRef`, `hb_vmPushStaticByRef`, `hb_vmAddInt`, `hb_itemUnRef`, `hb_itemCopy`, `hb_itemMove`, `hb_stackDec`, `hb_stackAllocItem`, `hb_stackItemFromTop`, `hb_stackLocalVariable`, `hb_stackPop`, the arithmetic ops) are visible from inside `hvm.c` — the implementer MUST confirm each against `hvm.c` and reproduce the case body verbatim.

---

### Task 1: The 28 op shims

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 28 declarations)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 28 shim implementations to the existing Plan 3 shim block)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_a.c`

- [ ] **Step 1: Add the declarations to `hbvmsh.h`**

Inside the existing `HB_EXTERN_BEGIN ... HB_EXTERN_END` block in `include/hbvmsh.h`, after the Plan 3 declarations, add:

```c
/* --- group A: FOR loops + compound assignment --- */
extern HB_EXPORT int hb_vmsh_fortest( void );
extern HB_EXPORT int hb_vmsh_inc( void );
extern HB_EXPORT int hb_vmsh_dec( void );
extern HB_EXPORT int hb_vmsh_duplunref( void );
extern HB_EXPORT int hb_vmsh_pushunref( void );
extern HB_EXPORT int hb_vmsh_pluseqpop( void );
extern HB_EXPORT int hb_vmsh_minuseqpop( void );
extern HB_EXPORT int hb_vmsh_multeqpop( void );
extern HB_EXPORT int hb_vmsh_diveqpop( void );
extern HB_EXPORT int hb_vmsh_modeqpop( void );
extern HB_EXPORT int hb_vmsh_expeqpop( void );
extern HB_EXPORT int hb_vmsh_deceqpop( void );
extern HB_EXPORT int hb_vmsh_inceqpop( void );
extern HB_EXPORT int hb_vmsh_pluseq( void );
extern HB_EXPORT int hb_vmsh_minuseq( void );
extern HB_EXPORT int hb_vmsh_multeq( void );
extern HB_EXPORT int hb_vmsh_diveq( void );
extern HB_EXPORT int hb_vmsh_modeq( void );
extern HB_EXPORT int hb_vmsh_expeq( void );
extern HB_EXPORT int hb_vmsh_deceq( void );
extern HB_EXPORT int hb_vmsh_inceq( void );
extern HB_EXPORT int hb_vmsh_pushlocalref( int iLocal );
extern HB_EXPORT int hb_vmsh_pushstaticref( int iStatic );
extern HB_EXPORT int hb_vmsh_localinc( int iLocal );
extern HB_EXPORT int hb_vmsh_localdec( int iLocal );
extern HB_EXPORT int hb_vmsh_localincpush( int iLocal );
extern HB_EXPORT int hb_vmsh_localaddint( int iLocal, int iAdd );
extern HB_EXPORT int hb_vmsh_localnearaddint( int iLocal, int iAdd );
```

- [ ] **Step 2: Append the 28 shim implementations to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing Plan 3 shim block, add the 28 implementations. Each starts with `HB_STACK_TLS_PRELOAD` (the first statement, exactly as the Plan 3 shims do) and ends with `return ( int ) hb_stackGetActionRequest();`. Representative implementations — the implementer writes all 28, mirroring the table above and the verified `hb_vmExecute` cases:

```c
HB_EXPORT int hb_vmsh_fortest( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmForTest();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_inc( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmInc( hb_stackItemFromTop( -1 ) );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pluseqpop( void )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pResult = hb_itemUnRef( hb_stackItemFromTop( -2 ) );
   hb_vmPlus( pResult, pResult, hb_stackItemFromTop( -1 ) );
   hb_stackPop();
   hb_stackPop();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pluseq( void )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pResult = hb_itemUnRef( hb_stackItemFromTop( -2 ) );
   PHB_ITEM pValue  = hb_stackItemFromTop( -1 );
   hb_vmPlus( pResult, pResult, pValue );
   hb_itemCopy( pValue, pResult );
   hb_itemMove( hb_stackItemFromTop( -2 ), pValue );
   hb_stackDec();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_inceq( void )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pResult = hb_stackItemFromTop( -1 );
   PHB_ITEM pValue  = hb_itemUnRef( pResult );
   PHB_ITEM pTemp;
   hb_vmInc( pValue );
   pTemp = hb_stackAllocItem();
   hb_itemCopy( pTemp, pValue );
   hb_itemMove( pResult, pTemp );
   hb_stackDec();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushlocalref( int iLocal )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushLocalByRef( iLocal );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_localinc( int iLocal )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pLocal = hb_stackLocalVariable( iLocal );
   hb_vmInc( HB_IS_BYREF( pLocal ) ? hb_itemUnRef( pLocal ) : pLocal );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_localincpush( int iLocal )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pLocal = hb_stackLocalVariable( iLocal );
   if( HB_IS_BYREF( pLocal ) )
      pLocal = hb_itemUnRef( pLocal );
   hb_vmInc( pLocal );
   hb_itemCopy( hb_stackAllocItem(), pLocal );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_localaddint( int iLocal, int iAdd )
{
   HB_STACK_TLS_PRELOAD
   hb_vmAddInt( hb_stackLocalVariable( iLocal ), ( HB_SHORT ) iAdd );
   return ( int ) hb_stackGetActionRequest();
}
```

The remaining shims (`hb_vmsh_dec`, `hb_vmsh_duplunref`, `hb_vmsh_pushunref`, `hb_vmsh_minuseqpop`/`multeqpop`/`diveqpop`/`modeqpop`/`expeqpop`, `hb_vmsh_deceqpop`/`inceqpop`, `hb_vmsh_minuseq`/`multeq`/`diveq`/`modeq`/`expeq`, `hb_vmsh_deceq`, `hb_vmsh_pushstaticref`, `hb_vmsh_localdec`, `hb_vmsh_localnearaddint`) follow the identical shape — each mirrors its row in the reference table / its `hb_vmExecute` case verbatim. `hb_vmsh_localnearaddint` is identical to `hb_vmsh_localaddint` (same body — the 1-byte vs 2-byte operand difference is handled by the *emitter*, not the shim; both shims take a decoded `int iLocal`).

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_a.c` — compile/link-only proof the new symbols are exported:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 6 ];
   p[ 0 ] = ( void * ) hb_vmsh_fortest;
   p[ 1 ] = ( void * ) hb_vmsh_pluseqpop;
   p[ 2 ] = ( void * ) hb_vmsh_inceq;
   p[ 3 ] = ( void * ) hb_vmsh_pushlocalref;
   p[ 4 ] = ( void * ) hb_vmsh_localinc;
   p[ 5 ] = ( void * ) hb_vmsh_localaddint;
   printf( "group A shims linkable: %p %p %p %p %p %p\n",
           p[0], p[1], p[2], p[3], p[4], p[5] );
   return 0;
}
```

- [ ] **Step 4: Rebuild Harbour and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/shim_smoke_a.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -o build/shim_smoke_a.exe
./build/shim_smoke_a.exe
```

Expected: Harbour rebuilds (`hvm.c` compiles with the 28 new shims, no errors), and `shim_smoke_a.exe` prints six non-null pointers.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_a.c
git commit -m "vm: add group A op shims (FOR loops + compound assignment)"
```

---

### Task 2: Decoder table — mark the 28 opcodes supported

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c` (flip 28 `fSupported` flags)

The 28 opcodes already have correct `kind` (`HB_PCK_FIXED`) and `nLen` in `hb_pcInfo[]` (verified during Plan 3). Only the `fSupported` field changes from `HB_FALSE` to `HB_TRUE`.

- [ ] **Step 1: Flip the flags**

In `src/compiler/hb_pcdec.c`, change the third field from `HB_FALSE` to `HB_TRUE` for exactly these 28 table rows (identify each by its `/* NN HB_P_<name> */` comment):

`HB_P_DEC` (17), `HB_P_INC` (23), `HB_P_FORTEST` (10), `HB_P_PUSHLOCALREF` (96), `HB_P_PUSHSTATICREF` (104), `HB_P_LOCALNEARADDINT` (126), `HB_P_PLUSEQPOP` (135), `HB_P_MINUSEQPOP` (136), `HB_P_MULTEQPOP` (137), `HB_P_DIVEQPOP` (138), `HB_P_PLUSEQ` (139), `HB_P_MINUSEQ` (140), `HB_P_MULTEQ` (141), `HB_P_DIVEQ` (142), `HB_P_LOCALADDINT` (153), `HB_P_MODEQPOP` (154), `HB_P_EXPEQPOP` (155), `HB_P_MODEQ` (156), `HB_P_EXPEQ` (157), `HB_P_DUPLUNREF` (158), `HB_P_PUSHUNREF` (165), `HB_P_DECEQPOP` (169), `HB_P_INCEQPOP` (170), `HB_P_DECEQ` (171), `HB_P_INCEQ` (172), `HB_P_LOCALDEC` (173), `HB_P_LOCALINC` (174), `HB_P_LOCALINCPUSH` (175).

Do NOT change `kind` or `nLen` for any row. Do NOT touch any other opcode.

- [ ] **Step 2: Update the file-header comment**

The `hb_pcdec.c` header comment lists the supported subset. Add the 28 group A opcode names to that "in scope (fSupported = HB_TRUE)" list so the comment stays consistent with the table.

- [ ] **Step 3: Rebuild and verify**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: clean rebuild; `pcdectest.exe` still prints `pcdec: 4 instructions, all boundaries correct` and `pcdec: jump analysis correct` (the existing decoder tests are unaffected — flipping `fSupported` does not change lengths).

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c
git commit -m "compiler: mark group A opcodes supported in the decoder table"
```

---

### Task 3: Emitter cases for the 28 opcodes

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

Add, to the straight-line body emitter (`hb_llvmSLEmitBody`) and the module-header `declare` block, the IR for the 28 opcodes.

- [ ] **Step 1: Add the `declare` lines**

In the module-header emission of `genllvm.c`, where the Plan 3 `declare i32 @hb_vmsh_*` lines are emitted, add:

```c
   fprintf( yyc, "declare i32 @hb_vmsh_fortest()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_inc()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_dec()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_duplunref()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushunref()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pluseqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_minuseqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_multeqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_diveqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_modeqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_expeqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_deceqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_inceqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pluseq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_minuseq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_multeq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_diveq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_modeq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_expeq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_deceq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_inceq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlocalref(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushstaticref(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localinc(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localdec(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localincpush(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localaddint(i32, i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localnearaddint(i32, i32)\n" );
```

- [ ] **Step 2: Add the emitter cases**

In `hb_llvmSLEmitBody`'s opcode `switch`, add cases for the 28 opcodes.

The **21 no-operand opcodes** follow the existing `HB_EMIT_NOARG_SHIM` pattern Plan 3 established — if that is a macro, add the 21 macro invocations (`HB_EMIT_NOARG_SHIM( HB_P_FORTEST, "fortest" )`, etc.). If Plan 3 used explicit per-case `fprintf`s, follow that form. Each no-operand case emits: `%rOFF = call i32 @hb_vmsh_X()`, the action-request check (`icmp ne i32 %rOFF, 0`), and `br i1` to `%epilogue` / next block — identical in shape to the Plan 3 arithmetic cases.

The **2 ref-push opcodes** mirror the existing `HB_P_PUSHLOCAL` / `HB_P_PUSHSTATIC` emitter cases — decode the 2-byte index operand (`HB_PCODE_MKSHORT` signed for `HB_P_PUSHLOCALREF`, `HB_PCODE_MKUSHORT` for `HB_P_PUSHSTATICREF`), emit `call i32 @hb_vmsh_pushlocalref(i32 <idx>)` / `@hb_vmsh_pushstaticref`, then the standard request check.

The **5 local direct-modify opcodes**:
- `HB_P_LOCALINC` / `HB_P_LOCALDEC` / `HB_P_LOCALINCPUSH` — decode `HB_PCODE_MKUSHORT(&pCode[off+1])`, emit `call i32 @hb_vmsh_localinc(i32 <idx>)` (resp. `_localdec`, `_localincpush`).
- `HB_P_LOCALADDINT` — decode index `HB_PCODE_MKUSHORT(&pCode[off+1])` and addend `HB_PCODE_MKSHORT(&pCode[off+3])`, emit `call i32 @hb_vmsh_localaddint(i32 <idx>, i32 <addend>)`.
- `HB_P_LOCALNEARADDINT` — decode index `pCode[off+1]` (1 byte, unsigned) and addend `HB_PCODE_MKSHORT(&pCode[off+2])`, emit `call i32 @hb_vmsh_localnearaddint(i32 <idx>, i32 <addend>)`.

Every case ends with the same action-request check + branch the Plan 3 cases use, and respects the dangling-label guard (`label %epilogue` when `nextOff >= nPCSize`) that Plan 3 added.

- [ ] **Step 3: Rebuild and check the IR for a FOR loop**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
bin/win/mingw64/harbour.exe -GL -q -obuild/t3a tests/llvm/loop.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3a.ll -o NUL
grep -c "hb_vmsh_fortest\|hb_vmsh_localinc" build/t3a.ll
grep -c "call void @hb_vmExecute" build/t3a.ll
```

Expected: clean rebuild; `t3a.ll` passes the LLVM verifier (only the benign `-Woverride-module` warning); `t3a.ll` now contains `hb_vmsh_fortest` / `hb_vmsh_localinc` calls; `HB_FUN_MAIN` no longer routes through `hb_vmExecute` (the `grep -c "call void @hb_vmExecute"` count is 0 — note the `declare` line uses `declare void`, not `call void`, so it is not matched).

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for group A opcodes (FOR loops, compound assignment)"
```

---

### Task 4: Corpus — verify FOR loops and compound assignment

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\compound.prg`
- Create: `c:\HarbourLLVM\core\tests\llvm\forstep.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh` (move `loop` to the straight-lined-expected set)

- [ ] **Step 1: Write the new corpus programs**

`tests/llvm/compound.prg`:

```harbour
function Main()
   local n := 10
   n += 5
   n -= 3
   n *= 2
   n /= 4
   n++
   n--
   ? n
   return nil
```

`tests/llvm/forstep.prg`:

```harbour
function Main()
   local i, nSum := 0
   for i := 10 to 1 step -2
      nSum += i
   next
   for i := 0 to 20 step 5
      nSum += i
   next
   ? nSum
   return nil
```

- [ ] **Step 2: Update `run.sh`'s straight-line assertion set**

`tests/llvm/run.sh` asserts which programs straight-line vs fall back. Plan 3 left `loop` in the "fallback expected" set. Now `loop`, `compound`, and `forstep` must all straight-line. Edit the assertion logic so `loop`, `compound`, `forstep` are checked like `arith`/`calls` (must contain `hb_vmsh_`, must NOT contain `call void @hb_vmExecute` in `HB_FUN_MAIN`). Keep `fallback` in the fallback-expected set. If `run.sh` keys this off a hard-coded program-name list, update that list; if it auto-detects, ensure the report still records each program's straight-line/fallback status.

- [ ] **Step 3: Run the full corpus**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
for p in loop compound forstep; do
  env PATH="/c/Windows/System32:/c/Windows" \
    bin/win/mingw64/harbour.exe -GL -q -obuild/ga_$p tests/llvm/$p.prg
  ./build/ga_$p.exe > build/ga_${p}_ll.out
  bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/ga_${p}_c tests/llvm/$p.prg
  ./build/ga_${p}_c.exe > build/ga_${p}_c.out
  diff build/ga_${p}_c.out build/ga_${p}_ll.out && echo "$p: MATCH" || echo "$p: DIFF"
done
```

Expected: `loop: MATCH`, `compound: MATCH`, `forstep: MATCH` — the straight-line backend output is byte-identical to the C backend. (`loop.prg` prints `55`; the others print their computed sums.)

- [ ] **Step 4: Confirm `loop.prg` is now straight-lined**

```bash
cd c:/HarbourLLVM/core
grep -c "hb_vmsh_fortest" build/ga_loop.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/ga_loop.ll | grep -c "call void @hb_vmExecute"
```

Expected: the first `grep` is ≥ 1 (FORTEST straight-lined); the second is `0` (no interpreter call in `HB_FUN_MAIN`).

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-ga
```

Expected: `run.sh` ends with `RESULT: all programs validated and matched the C backend`, and the report shows `loop`/`compound`/`forstep` as straight-lined.

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/compound.prg tests/llvm/forstep.prg tests/llvm/run.sh
git commit -m "test: corpus for group A — FOR loops and compound assignment straight-lined"
```

---

## Self-Review

**Spec coverage:** The spec's three opcode sub-groups (21 no-operand, 2 ref-push, 5 local direct-modify = 28) are all covered — Task 1 the shims, Task 2 the decoder flags, Task 3 the emitter cases, Task 4 the FOR-loop / compound / `FOR..STEP` corpus. The spec's testing section (`loop.prg` straight-lines, new `compound.prg` and `forstep.prg`, diff against the C backend) is Task 4.

**Placeholder scan:** Task 1 Step 2 shows 9 representative shims in full and names the remaining 19 with their exact source (the reference table + the `hb_vmExecute` cases) — this is a mechanical expansion of a fully-shown pattern against an enumerated list, the same approach used and accepted in the Plan 3 shim task, not an under-specified placeholder. Task 3 Step 2 likewise anchors the 21 no-operand cases to the existing Plan 3 `HB_EMIT_NOARG_SHIM` pattern and gives the operand decoding for all 7 opcodes that have operands explicitly.

**Type consistency:** Shim names and signatures are identical across `hbvmsh.h` (Task 1 Step 1), the implementations (Task 1 Step 2), and the IR `declare`s (Task 3 Step 1): the 21 no-operand shims are `int (void)` / `i32 ()`, `pushlocalref`/`pushstaticref`/`localinc`/`localdec`/`localincpush` are `int (int)` / `i32 (i32)`, `localaddint`/`localnearaddint` are `int (int,int)` / `i32 (i32,i32)`. The 28 opcode names in Task 2's flip list match the reference table.

**Known risk:** the shims must mirror their `hb_vmExecute` cases bit-exactly (especially the `PLUSEQ`-family `hb_itemCopy`/`hb_itemMove`/`hb_stackDec` sequence and the `LOCAL*` `HB_IS_BYREF` handling). The Task 4 diff-against-C-backend corpus is the gate that catches any deviation.
