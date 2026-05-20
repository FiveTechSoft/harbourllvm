# Harbour LLVM Plan 3 — Straight-line IR (remove the interpreter loop)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `harbour -GL` emits one LLVM IR instruction sequence per pcode opcode — straight-line native code — instead of handing the pcode byte array to the `hb_vmExecute` interpreter, eliminating the bytecode dispatch overhead.

**Architecture:** A new exported runtime "op shim" (`hb_vmsh_*` functions added inside `src/vm/hvm.c`, the only translation unit that can see the `static` VM op functions) replicates each interpreter `switch` case as a callable function. `genllvm.c` decodes each function's pcode, builds a basic-block graph from the jump targets, and emits a `call` to the matching shim per opcode plus `br` for control flow. Any function containing an opcode outside the supported subset falls back, whole-function, to the existing `hb_vmExecute` path — so correctness is total and the speedup lands incrementally.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Prerequisites (done):** Plan 1 (`-GL` emits IR) and Plan 2 (`harbour -GL` embeds LLVM+LLD, produces a native `.exe`).

---

## Scope

**In scope — the "functional subset" of opcodes** (the same subset Spec 1 named): push of locals / statics / literals / symbols, arithmetic, comparisons, logical ops, jumps, function/procedure calls, frame setup, return, pop, duplicate, line markers.

**Out of scope (whole-function fallback to `hb_vmExecute`):** macros `&`, codeblocks, RDD field ops, BEGIN SEQUENCE / RECOVER, FOR EACH, WITH OBJECT, SWITCH, the compound `*EQ` opcodes, hashes, arrays. A function using any of these is emitted exactly as today (interpreter call). This keeps every program correct.

**Not in scope at all:** type specialization / unboxing (the 5-50x). Plan 3 removes the *dispatch* overhead only — an honest, moderate speedup. Type specialization is a possible Plan 4.

---

## Correctness model

The interpreter checks an *action request* after every opcode (`hb_stackGetActionRequest()` — set on error, QUIT, BREAK, a RETURN from a nested call). Straight-line IR must preserve this. Therefore **every shim function returns `int`** — the current action request (`0` = continue). `genllvm.c` emits, after each opcode's shim call, a branch: if the request is non-zero, jump to the function epilogue; otherwise fall through to the next opcode.

This makes every pcode instruction its own basic block. Jump targets then fall naturally on block boundaries — the same structure the jumps need anyway.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/vm/hvm.c` (modify) | Append the exported `hb_vmsh_*` op-shim functions at end of file — they can call the `static` op functions. |
| `include/hbvmsh.h` (new) | Public declarations of every `hb_vmsh_*` shim. |
| `src/compiler/hb_pcdec.h` (new) | The opcode decoder table type + the subset table declaration. |
| `src/compiler/hb_pcdec.c` (new) | The opcode table: per opcode → operand size, supported flag, shim name. Plus `hb_pcodeInstrLen()`. |
| `src/compiler/genllvm.c` (modify) | Replace the per-function `hb_vmExecute` call with the straight-line emitter; keep the fallback path. |
| `src/compiler/Makefile` (modify) | Add `hb_pcdec.c`. |
| `tests/llvm/` (new `.prg` files) | Subset-coverage and fallback test programs. |

---

### Task 1: Exported VM op shim

**Files:**
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append shim functions at end of file)
- Create: `c:\HarbourLLVM\core\include\hbvmsh.h`
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke.c`

Each shim replicates one interpreter `switch` case from `hb_vmExecute` (the switch spans `hvm.c` ~1392-2969) and returns the action request. They live at the end of `hvm.c` so they can call the `static` ops (`hb_vmPlus`, `hb_vmPushLocal`, `hb_vmPopLogical`, …).

- [ ] **Step 1: Create the header**

`include/hbvmsh.h`:

```c
/*
 * Harbour VM op shim — exported one-call-per-opcode entry points used by
 * the straight-line LLVM backend. Each function performs exactly what the
 * corresponding hb_vmExecute() switch case does, and returns the current
 * action request (0 = continue, non-zero = unwind to the function epilogue).
 */
#ifndef HB_VMSH_H_
#define HB_VMSH_H_

#include "hbvmpub.h"

HB_EXTERN_BEGIN

/* literals */
extern HB_EXPORT int hb_vmsh_pushnil( void );
extern HB_EXPORT int hb_vmsh_pushlogical( HB_BOOL fValue );
extern HB_EXPORT int hb_vmsh_pushint( int iValue );
extern HB_EXPORT int hb_vmsh_pushlong( HB_MAXINT nValue );
extern HB_EXPORT int hb_vmsh_pushdouble( double dValue, int iWidth, int iDec );
extern HB_EXPORT int hb_vmsh_pushstring( const char * szText, HB_SIZE nLen );

/* locals / statics */
extern HB_EXPORT int hb_vmsh_pushlocal( int iLocal );
extern HB_EXPORT int hb_vmsh_poplocal( int iLocal );
extern HB_EXPORT int hb_vmsh_pushstatic( HB_USHORT uiStatic );
extern HB_EXPORT int hb_vmsh_popstatic( HB_USHORT uiStatic );

/* symbol push (pSym points into the module @symbols_table) */
extern HB_EXPORT int hb_vmsh_pushsymbol( PHB_SYMB pSym );

/* arithmetic (operate on the VM stack, like the interpreter cases) */
extern HB_EXPORT int hb_vmsh_plus( void );
extern HB_EXPORT int hb_vmsh_minus( void );
extern HB_EXPORT int hb_vmsh_mult( void );
extern HB_EXPORT int hb_vmsh_divide( void );
extern HB_EXPORT int hb_vmsh_modulus( void );
extern HB_EXPORT int hb_vmsh_power( void );
extern HB_EXPORT int hb_vmsh_negate( void );

/* comparisons */
extern HB_EXPORT int hb_vmsh_equal( void );
extern HB_EXPORT int hb_vmsh_exactlyequal( void );
extern HB_EXPORT int hb_vmsh_notequal( void );
extern HB_EXPORT int hb_vmsh_less( void );
extern HB_EXPORT int hb_vmsh_lessequal( void );
extern HB_EXPORT int hb_vmsh_greater( void );
extern HB_EXPORT int hb_vmsh_greaterequal( void );

/* logical */
extern HB_EXPORT int hb_vmsh_and( void );
extern HB_EXPORT int hb_vmsh_or( void );
extern HB_EXPORT int hb_vmsh_not( void );

/* stack misc */
extern HB_EXPORT int hb_vmsh_pop( void );
extern HB_EXPORT int hb_vmsh_duplicate( void );

/* frame / parameters */
extern HB_EXPORT int hb_vmsh_frame( HB_USHORT uiLocals, unsigned char ucParams );

/* calls — uiParams is the operand of HB_P_FUNCTION/DO; the symbol was
 * already pushed by a preceding HB_P_PUSHSYM. */
extern HB_EXPORT int hb_vmsh_function( HB_USHORT uiParams );
extern HB_EXPORT int hb_vmsh_do( HB_USHORT uiParams );

/* return value */
extern HB_EXPORT int hb_vmsh_retvalue( void );

/* For conditional jumps: pop the top item as a logical and report it.
 * *pfValue receives the popped logical; the return value is the action
 * request (a non-logical top item raises an error -> non-zero request). */
extern HB_EXPORT int hb_vmsh_poplogical( HB_BOOL * pfValue );

HB_EXTERN_END

#endif
```

- [ ] **Step 2: Append the shim implementations to `hvm.c`**

At the very end of `src/vm/hvm.c`, after the last function, add `#include "hbvmsh.h"` (near the other includes is also fine) and the implementations. Each one mirrors its interpreter case exactly, then returns `hb_stackGetActionRequest()`. Representative implementations — the engineer writes one per declared shim, following the matching `case` in the `hb_vmExecute` switch:

```c
/* --- Harbour VM op shim for the straight-line LLVM backend --- */

int hb_vmsh_pushlocal( int iLocal )
{
   hb_vmPushLocal( iLocal );
   return hb_stackGetActionRequest();
}

int hb_vmsh_poplocal( int iLocal )
{
   hb_vmPopLocal( iLocal );
   return hb_stackGetActionRequest();
}

int hb_vmsh_pushint( int iValue )
{
   hb_vmPushInteger( iValue );
   return hb_stackGetActionRequest();
}

int hb_vmsh_pushstring( const char * szText, HB_SIZE nLen )
{
   hb_vmPushString( szText, nLen );
   return hb_stackGetActionRequest();
}

int hb_vmsh_pushsymbol( PHB_SYMB pSym )
{
   hb_vmPushSymbol( pSym );
   return hb_stackGetActionRequest();
}

int hb_vmsh_plus( void )
{
   hb_vmPlus( hb_stackItemFromTop( -2 ),
              hb_stackItemFromTop( -2 ),
              hb_stackItemFromTop( -1 ) );
   hb_stackPop();
   return hb_stackGetActionRequest();
}

int hb_vmsh_negate( void )
{
   hb_vmNegate();
   return hb_stackGetActionRequest();
}

int hb_vmsh_less( void )
{
   hb_vmLess();
   return hb_stackGetActionRequest();
}

int hb_vmsh_not( void )
{
   hb_vmNot();
   return hb_stackGetActionRequest();
}

int hb_vmsh_pop( void )
{
   hb_stackPop();
   return hb_stackGetActionRequest();
}

int hb_vmsh_frame( HB_USHORT uiLocals, unsigned char ucParams )
{
   hb_vmFrame( uiLocals, ucParams );
   return hb_stackGetActionRequest();
}

int hb_vmsh_function( HB_USHORT uiParams )
{
   hb_itemSetNil( hb_stackReturnItem() );
   hb_vmProc( uiParams );
   hb_stackPushReturn();
   return hb_stackGetActionRequest();
}

int hb_vmsh_do( HB_USHORT uiParams )
{
   hb_vmProc( uiParams );
   return hb_stackGetActionRequest();
}

int hb_vmsh_retvalue( void )
{
   hb_stackPopReturn();
   hb_stackReturnItem()->type &= ~HB_IT_MEMOFLAG;
   return hb_stackGetActionRequest();
}

int hb_vmsh_poplogical( HB_BOOL * pfValue )
{
   *pfValue = hb_vmPopLogical();
   return hb_stackGetActionRequest();
}
```

The engineer MUST read the exact `case` body for every remaining declared shim (`hb_vmsh_pushnil`, `hb_vmsh_pushlogical`, `hb_vmsh_pushlong`, `hb_vmsh_pushdouble`, `hb_vmsh_pushstatic`, `hb_vmsh_popstatic`, `hb_vmsh_minus`, `hb_vmsh_mult`, `hb_vmsh_divide`, `hb_vmsh_modulus`, `hb_vmsh_power`, `hb_vmsh_equal`, `hb_vmsh_exactlyequal`, `hb_vmsh_notequal`, `hb_vmsh_lessequal`, `hb_vmsh_greater`, `hb_vmsh_greaterequal`, `hb_vmsh_and`, `hb_vmsh_or`, `hb_vmsh_duplicate`) in the `hb_vmExecute` switch and reproduce it. `hb_vmPushLong`'s shim must push an `HB_MAXINT` the way the `HB_P_PUSHLONG`/`HB_P_PUSHLONGLONG` cases do. If a needed runtime helper (`hb_stackGetActionRequest`, `hb_stackPopReturn`, `hb_stackPushReturn`, `hb_itemSetNil`) is itself `static`, route through the nearest exported equivalent or add it to the shim set — verify each against `include/`.

- [ ] **Step 3: Write a smoke test**

`tests/llvm/shim_smoke.c` — links against the VM library and calls a few shims to prove they are exported and linkable:

```c
#include "hbvmsh.h"
#include <stdio.h>

/* Compile/link-only smoke test: proves the shim symbols are exported.
   Not executed (the VM is not initialised here). */
int main( void )
{
   void * p[ 4 ];
   p[ 0 ] = ( void * ) hb_vmsh_pushlocal;
   p[ 1 ] = ( void * ) hb_vmsh_plus;
   p[ 2 ] = ( void * ) hb_vmsh_function;
   p[ 3 ] = ( void * ) hb_vmsh_retvalue;
   printf( "shim symbols linkable: %p %p %p %p\n", p[0], p[1], p[2], p[3] );
   return 0;
}
```

- [ ] **Step 4: Rebuild Harbour and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/shim_smoke.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -o build/shim_smoke.exe
./build/shim_smoke.exe
```

Expected: Harbour rebuilds (`hvm.c` compiles with the shim), and `shim_smoke.exe` prints four non-null pointers — proving the `hb_vmsh_*` symbols are exported and linkable.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke.c
git commit -m "vm: add exported op shim for the straight-line LLVM backend"
```

---

### Task 2: pcode opcode decoder

**Files:**
- Create: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.h`
- Create: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`
- Modify: `c:\HarbourLLVM\core\src\compiler\Makefile`
- Test: `c:\HarbourLLVM\core\tests\llvm\pcdectest.c`

A table mapping every `HB_P_*` opcode to: total instruction length (fixed opcodes), a "supported by the straight-line emitter" flag, and the shim name. `genllvm.c` uses it both to walk pcode and to decide fallback.

- [ ] **Step 1: Create the header**

`src/compiler/hb_pcdec.h`:

```c
/* pcode opcode decoder for the straight-line LLVM backend. */
#ifndef HB_PCDEC_H_
#define HB_PCDEC_H_

#include "hbpcode.h"
#include "hbdefs.h"

typedef enum
{
   HB_PCK_FIXED,     /* fixed-length: nLen is the whole instruction length  */
   HB_PCK_STR1,      /* 1-byte length prefix then string data               */
   HB_PCK_STR2,      /* 2-byte length prefix then string data               */
   HB_PCK_STR3,      /* 3-byte length prefix then string data               */
   HB_PCK_VARBLOCK,  /* HB_P_PUSHBLOCK family: size is the operand           */
   HB_PCK_UNKNOWN    /* not modelled — forces whole-function fallback        */
} HB_PCKIND;

typedef struct
{
   HB_PCKIND kind;   /* how to compute the instruction length               */
   HB_USHORT nLen;   /* for HB_PCK_FIXED: total bytes incl. the opcode byte  */
   HB_BOOL   fSupported;  /* HB_TRUE if the straight-line emitter handles it */
} HB_PCINFO;

/* One entry per opcode value 0..HB_P_LAST_PCODE-1. */
extern const HB_PCINFO hb_pcInfo[];

/* Total byte length of the instruction at pCode (handles variable-length
 * string/block opcodes). Returns 0 if the opcode is HB_PCK_UNKNOWN. */
extern HB_SIZE hb_pcodeInstrLen( const HB_BYTE * pCode );

#endif
```

- [ ] **Step 2: Create the table**

`src/compiler/hb_pcdec.c`. The table has `HB_P_LAST_PCODE` entries. Every opcode the straight-line emitter does NOT handle is `{ HB_PCK_FIXED, <len>, HB_FALSE }` (still needs a correct length so the decoder can skip it when scanning a fallback function) or `{ HB_PCK_UNKNOWN, 0, HB_FALSE }` if even its length is variable/unmodelled. Operand sizes come from the `hb_vmExecute` switch in `hvm.c` (each case advances `pCode` by a known amount). Supported subset entries are marked `HB_FALSE`/`HB_TRUE` per the Scope section.

```c
#include "hb_pcdec.h"

/* Indexed by opcode. Length = 1 (opcode) + operand bytes.
 * Only the functional subset is fSupported = HB_TRUE; everything else is
 * HB_FALSE so genllvm.c falls the whole function back to hb_vmExecute. */
const HB_PCINFO hb_pcInfo[] =
{
   /* HB_P_AND            0 */ { HB_PCK_FIXED, 1, HB_TRUE  },
   /* HB_P_ARRAYPUSH      1 */ { HB_PCK_FIXED, 1, HB_FALSE },
   /* HB_P_ARRAYPOP       2 */ { HB_PCK_FIXED, 1, HB_FALSE },
   /* ... one row per opcode, in numeric order, through HB_P_LAST_PCODE-1 ...
    *
    * Fill EVERY row. Get each opcode's length from the matching case in the
    * hb_vmExecute switch (src/vm/hvm.c): the `pCode += N` at the end of the
    * case is N. For string opcodes use HB_PCK_STR1/STR2/STR3 with nLen 0.
    * For HB_P_PUSHBLOCK / PUSHBLOCKSHORT / PUSHBLOCKLARGE use HB_PCK_VARBLOCK.
    *
    * Mark fSupported = HB_TRUE only for the Scope "in scope" opcodes:
    *   HB_P_PUSHNIL, HB_P_TRUE, HB_P_FALSE, HB_P_ZERO, HB_P_ONE,
    *   HB_P_PUSHBYTE, HB_P_PUSHINT, HB_P_PUSHLONG, HB_P_PUSHLONGLONG,
    *   HB_P_PUSHDOUBLE, HB_P_PUSHSTR, HB_P_PUSHSTRSHORT, HB_P_PUSHSTRLARGE,
    *   HB_P_PUSHLOCAL, HB_P_PUSHLOCALNEAR, HB_P_POPLOCAL, HB_P_POPLOCALNEAR,
    *   HB_P_PUSHSTATIC, HB_P_POPSTATIC, HB_P_PUSHSYM, HB_P_PUSHSYMNEAR,
    *   HB_P_PUSHFUNCSYM, HB_P_PLUS, HB_P_MINUS, HB_P_MULT, HB_P_DIVIDE,
    *   HB_P_MODULUS, HB_P_POWER, HB_P_NEGATE, HB_P_EQUAL, HB_P_EXACTLYEQUAL,
    *   HB_P_NOTEQUAL, HB_P_LESS, HB_P_LESSEQUAL, HB_P_GREATER,
    *   HB_P_GREATEREQUAL, HB_P_AND, HB_P_OR, HB_P_NOT, HB_P_POP,
    *   HB_P_DUPLICATE, HB_P_JUMP, HB_P_JUMPNEAR, HB_P_JUMPFAR,
    *   HB_P_JUMPFALSE, HB_P_JUMPFALSENEAR, HB_P_JUMPFALSEFAR,
    *   HB_P_JUMPTRUE, HB_P_JUMPTRUENEAR, HB_P_JUMPTRUEFAR,
    *   HB_P_FUNCTION, HB_P_FUNCTIONSHORT, HB_P_DO, HB_P_DOSHORT,
    *   HB_P_FRAME, HB_P_RETVALUE, HB_P_ENDPROC, HB_P_LINE, HB_P_NOOP
    */
};

HB_SIZE hb_pcodeInstrLen( const HB_BYTE * pCode )
{
   const HB_PCINFO * pInfo = &hb_pcInfo[ pCode[ 0 ] ];

   switch( pInfo->kind )
   {
      case HB_PCK_FIXED:
         return pInfo->nLen;
      case HB_PCK_STR1:
         return ( HB_SIZE ) 2 + pCode[ 1 ];
      case HB_PCK_STR2:
         return ( HB_SIZE ) 3 + HB_PCODE_MKUSHORT( &pCode[ 1 ] );
      case HB_PCK_STR3:
         return ( HB_SIZE ) 4 + HB_PCODE_MKUINT24( &pCode[ 1 ] );
      case HB_PCK_VARBLOCK:
         /* HB_P_PUSHBLOCK: total size is the 2-byte operand;
            PUSHBLOCKSHORT: 1-byte; PUSHBLOCKLARGE: 3-byte — match hvm.c. */
         if( pCode[ 0 ] == HB_P_PUSHBLOCKSHORT )
            return pCode[ 1 ];
         if( pCode[ 0 ] == HB_P_PUSHBLOCKLARGE )
            return HB_PCODE_MKUINT24( &pCode[ 1 ] );
         return HB_PCODE_MKUSHORT( &pCode[ 1 ] );
      default:
         return 0;   /* HB_PCK_UNKNOWN */
   }
}
```

- [ ] **Step 3: Write the decoder test**

`tests/llvm/pcdectest.c` — walks a hand-built pcode buffer and checks instruction boundaries:

```c
#include "hb_pcdec.h"
#include <stdio.h>
#include <assert.h>

int main( void )
{
   /* HB_P_PUSHINT(3 bytes) HB_P_PUSHINT(3) HB_P_PLUS(1) HB_P_ENDPROC(1) */
   HB_BYTE buf[] = {
      HB_P_PUSHINT, 2, 0,
      HB_P_PUSHINT, 3, 0,
      HB_P_PLUS,
      HB_P_ENDPROC
   };
   HB_SIZE pos = 0;
   int     n   = 0;
   while( pos < sizeof( buf ) )
   {
      HB_SIZE len = hb_pcodeInstrLen( &buf[ pos ] );
      assert( len > 0 );
      pos += len;
      ++n;
   }
   assert( pos == sizeof( buf ) );
   assert( n == 4 );
   printf( "pcdec: %d instructions, all boundaries correct\n", n );
   return 0;
}
```

- [ ] **Step 4: Wire into the build and test**

Add `hb_pcdec.c` to `C_SOURCES` in `src/compiler/Makefile` (after `hb_llvmstub.c`). Then:

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: `pcdec: 4 instructions, all boundaries correct`.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.h src/compiler/hb_pcdec.c src/compiler/Makefile tests/llvm/pcdectest.c
git commit -m "compiler: add pcode opcode decoder table for the LLVM backend"
```

---

### Task 3: Basic-block analysis (jump-target discovery)

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.h` (add the block-map API)
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c` (implement it)
- Modify: `c:\HarbourLLVM\core\tests\llvm\pcdectest.c` (add a jump test)

To emit `br` instructions the emitter needs, for any pcode offset, whether a basic block starts there. A block starts at offset 0, at every jump target, and right after every jump/call.

- [ ] **Step 1: Add the block-map API to `hb_pcdec.h`**

Append before the final `#endif`:

```c
/* Per-function basic-block map. After hb_pcodeAnalyze() succeeds,
 * abLeader[off] is HB_TRUE when a basic block begins at pcode offset off.
 * fAllSupported is HB_TRUE only if every opcode in the function is in the
 * straight-line subset (genllvm.c uses the interpreter fallback otherwise). */
typedef struct
{
   HB_BYTE * abLeader;       /* nSize bytes, 1 = block leader              */
   HB_SIZE   nSize;          /* pcode length                               */
   HB_BOOL   fAllSupported;  /* HB_FALSE -> caller must use the fallback    */
} HB_PCMAP;

/* Scan pcode[0..nSize); fill *pMap. Returns HB_TRUE on a clean scan
 * (instruction boundaries consistent), HB_FALSE on a malformed stream.
 * The caller frees pMap->abLeader with hb_xfree(). */
extern HB_BOOL hb_pcodeAnalyze( const HB_BYTE * pCode, HB_SIZE nSize,
                                HB_PCMAP * pMap );

/* Signed jump displacement of the jump instruction at pCode (relative to
 * the START of that instruction). Valid only for jump opcodes. */
extern HB_ISIZ hb_pcodeJumpOffset( const HB_BYTE * pCode );
```

- [ ] **Step 2: Implement in `hb_pcdec.c`**

```c
#include "hbapi.h"   /* hb_xgrab / hb_xgrabz / hb_xfree */

HB_ISIZ hb_pcodeJumpOffset( const HB_BYTE * pCode )
{
   switch( pCode[ 0 ] )
   {
      case HB_P_JUMPNEAR:
      case HB_P_JUMPFALSENEAR:
      case HB_P_JUMPTRUENEAR:
         return ( signed char ) pCode[ 1 ];
      case HB_P_JUMP:
      case HB_P_JUMPFALSE:
      case HB_P_JUMPTRUE:
         return HB_PCODE_MKSHORT( &pCode[ 1 ] );
      case HB_P_JUMPFAR:
      case HB_P_JUMPFALSEFAR:
      case HB_P_JUMPTRUEFAR:
         return HB_PCODE_MKINT24( &pCode[ 1 ] );
      default:
         return 0;
   }
}

static HB_BOOL hb_pcodeIsJump( HB_BYTE op )
{
   switch( op )
   {
      case HB_P_JUMP:      case HB_P_JUMPNEAR:      case HB_P_JUMPFAR:
      case HB_P_JUMPFALSE: case HB_P_JUMPFALSENEAR: case HB_P_JUMPFALSEFAR:
      case HB_P_JUMPTRUE:  case HB_P_JUMPTRUENEAR:  case HB_P_JUMPTRUEFAR:
         return HB_TRUE;
      default:
         return HB_FALSE;
   }
}

HB_BOOL hb_pcodeAnalyze( const HB_BYTE * pCode, HB_SIZE nSize, HB_PCMAP * pMap )
{
   HB_SIZE pos;

   pMap->nSize         = nSize;
   pMap->fAllSupported = HB_TRUE;
   pMap->abLeader      = ( HB_BYTE * ) hb_xgrabz( nSize + 1 );
   if( nSize > 0 )
      pMap->abLeader[ 0 ] = HB_TRUE;          /* entry is always a leader */

   pos = 0;
   while( pos < nSize )
   {
      HB_BYTE op  = pCode[ pos ];
      HB_SIZE len = hb_pcodeInstrLen( &pCode[ pos ] );

      if( len == 0 || pos + len > nSize )     /* malformed / unmodelled */
      {
         hb_xfree( pMap->abLeader );
         pMap->abLeader = NULL;
         return HB_FALSE;
      }
      if( ! hb_pcInfo[ op ].fSupported )
         pMap->fAllSupported = HB_FALSE;

      if( hb_pcodeIsJump( op ) )
      {
         HB_ISIZ disp   = hb_pcodeJumpOffset( &pCode[ pos ] );
         HB_ISIZ target = ( HB_ISIZ ) pos + disp;   /* relative to instr start */

         if( target < 0 || ( HB_SIZE ) target > nSize )
         {
            hb_xfree( pMap->abLeader );
            pMap->abLeader = NULL;
            return HB_FALSE;
         }
         if( ( HB_SIZE ) target < nSize )
            pMap->abLeader[ target ] = HB_TRUE;     /* jump target leads   */
         if( pos + len < nSize )
            pMap->abLeader[ pos + len ] = HB_TRUE;  /* fall-through leads  */
      }
      pos += len;
   }
   return HB_TRUE;
}
```

Note: Harbour jump displacements are relative to the *start* of the jump instruction (the `hb_vmExecute` cases do `pCode += disp` before consuming the operand) — confirm against the `HB_P_JUMP*` cases in `hvm.c` and adjust the `target` base if the codebase proves otherwise.

- [ ] **Step 3: Add a jump test to `pcdectest.c`**

Append to `main()` before `return 0;`:

```c
   {
      /* HB_P_JUMP +0: a 3-byte jump to its own end (offset 3). */
      HB_BYTE jbuf[] = { HB_P_JUMP, 3, 0, HB_P_ENDPROC };
      HB_PCMAP map;
      HB_BOOL  ok = hb_pcodeAnalyze( jbuf, sizeof( jbuf ), &map );
      assert( ok );
      assert( map.abLeader[ 0 ] == 1 );   /* entry           */
      assert( map.abLeader[ 3 ] == 1 );   /* jump target     */
      hb_xfree( map.abLeader );
      printf( "pcdec: jump analysis correct\n" );
   }
```

- [ ] **Step 4: Build and test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler \
    -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: both lines print — `pcdec: 4 instructions...` and `pcdec: jump analysis correct`.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.h src/compiler/hb_pcdec.c tests/llvm/pcdectest.c
git commit -m "compiler: add pcode basic-block / jump-target analysis"
```

---

### Task 4: Straight-line IR emitter

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

Replace the per-function body. For each non-fallback function, emit one basic block per pcode instruction; each block calls the shim, checks the returned action request, and branches.

- [ ] **Step 1: Add includes and the `declare`s**

In `genllvm.c`, add `#include "hb_pcdec.h"` and `#include "hbvmsh.h"`. In the module-header emission, after the existing `declare`s, emit a `declare` for every shim the emitter can call:

```c
   fprintf( yyc, "declare i32 @hb_vmsh_pushnil()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlogical(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushint(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlong(i64)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushdouble(double, i32, i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushstring(i8*, i64)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlocal(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_poplocal(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushstatic(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popstatic(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushsymbol(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_plus()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_minus()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_mult()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_divide()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_modulus()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_power()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_negate()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_equal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_exactlyequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_notequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_less()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_lessequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_greater()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_greaterequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_and()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_or()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_not()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_duplicate()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_frame(i32, i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_function(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_do(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_retvalue()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_poplogical(i32*)\n" );
```

- [ ] **Step 2: Add the per-function straight-line emitter**

Add a static function `hb_llvmEmitFuncBody`. It is given the open file, the function, and its already-emitted LLVM name. It runs `hb_pcodeAnalyze`; if that fails or `fAllSupported` is false, it returns `HB_FALSE` (caller emits the fallback). Otherwise it emits the body and returns `HB_TRUE`.

Emission model — for a function with pcode of length `N`:

```
define void @<name>() {
entry:
  br label %i0
i<off>:                       ; one block per instruction leader
  %r<off> = call i32 @hb_vmsh_<op>( <operands> )
  %c<off> = icmp ne i32 %r<off>, 0
  br i1 %c<off>, label %epilogue, label %<next-or-target>
  ...
epilogue:
  ret void
}
```

Rules the emitter follows:
- Walk pcode by `hb_pcodeInstrLen`. Each instruction offset `off` gets a label `i<off>:`. (Emitting a label per instruction — not only per leader — is simplest and LLVM folds straight-line blocks; or emit labels only where `abLeader[off]` and let non-leaders fall through. Either is fine; per-instruction labels are simplest to get right.)
- **Push/op/pop opcodes** → `%rOFF = call i32 @hb_vmsh_X(operands)`, then the action-request check, then `br` to the next instruction's label.
- **HB_P_PUSHSYM idx / PUSHSYMNEAR / PUSHFUNCSYM** → the symbol pointer is `getelementptr([K x %HB_SYMB], [K x %HB_SYMB]* @symbols_table, i32 0, i32 <idx>)`; pass it to `@hb_vmsh_pushsymbol`.
- **HB_P_PUSHSTR / PUSHSTRSHORT / PUSHSTRLARGE** → emit a `private constant` for the string bytes (reuse `hb_llvmEmitByte`) and pass its `getelementptr` + length to `@hb_vmsh_pushstring`.
- **HB_P_LINE / HB_P_NOOP** → emit nothing but the label and an unconditional `br` to the next (or skip entirely and let the previous block target the next real instruction).
- **HB_P_JUMP / JUMPNEAR / JUMPFAR** → unconditional `br label %i<target>` where `target = off + hb_pcodeJumpOffset(...)`.
- **HB_P_JUMPFALSE\* / JUMPTRUE\*** → allocate an `i32` slot in `entry` (`%jpОFF = alloca i32`), `call i32 @hb_vmsh_poplogical(i32* %jpOFF)`, request-check, then `%vOFF = load i32, i32* %jpOFF`, `%bOFF = icmp ne i32 %vOFF, 0`, and `br i1 %bOFF, label %<taken>, label %<not-taken>` — for `JUMPFALSE*` taken = target when the value is **false** (so swap the labels), for `JUMPTRUE*` taken = target when true.
- **HB_P_FUNCTION / FUNCTIONSHORT / DO / DOSHORT** → `call i32 @hb_vmsh_function(i32 <params>)` (or `_do`), then the action-request check.
- **HB_P_FRAME** → `call i32 @hb_vmsh_frame(i32 <locals>, i32 <params>)`. **HB_P_RETVALUE** → `call i32 @hb_vmsh_retvalue()`.
- **HB_P_ENDPROC** → `br label %epilogue`.
- After the last instruction, emit `epilogue:` then `ret void`.
- Operand decoding (signed/unsigned widths) per the opcode — e.g. `HB_P_PUSHLOCALNEAR` is `(signed char)pCode[1]`, `HB_P_PUSHLOCAL` is `HB_PCODE_MKSHORT(&pCode[1])`, `HB_P_PUSHINT` is `HB_PCODE_MKSHORT`, `HB_P_PUSHBYTE` is `(signed char)`, `HB_P_PUSHLONG` is `HB_PCODE_MKLONG`, `HB_P_FRAME` is two unsigned bytes. Match the `hb_vmExecute` cases exactly.

All `alloca`s for `poplogical` slots MUST be emitted in the `entry:` block (LLVM requires allocas dominate their uses; placing them in `entry` is the simple correct rule).

- [ ] **Step 3: Wire the emitter into the function-definition loop**

In the existing per-function `define` loop, replace the fixed three-line body. For each function: emit the `define ... {` line as today, then call `hb_llvmEmitFuncBody`. If it returns `HB_FALSE`, emit the **current** body (the `%s = load @symbols` + `call @hb_vmExecute(@.pcode.X, %s)` + `ret void`) — unchanged fallback. If it returns `HB_TRUE`, the body is already emitted. Close with `}`.

Keep emitting the `@.pcode.*` globals for ALL functions (the fallback path still needs them). A future cleanup can drop them for fully-straight-lined functions; not in this task (YAGNI).

- [ ] **Step 4: Build and verify IR for the hello program**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
bin/win/mingw64/harbour.exe -GL -q -obuild/t4 tests/llvm/hello.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t4.ll -o NUL
```

Expected: `harbour -GL` produces `build/t4.ll`; it now contains `call i32 @hb_vmsh_*` blocks for `HB_FUN_MAIN` (not a single `hb_vmExecute` call), and the LLVM verifier accepts it (only the benign `-Woverride-module` warning).

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR per pcode op, interpreter fallback retained"
```

---

### Task 5: End-to-end verification and the pcode-subset corpus

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\arith.prg`
- Create: `c:\HarbourLLVM\core\tests\llvm\loop.prg`
- Create: `c:\HarbourLLVM\core\tests\llvm\calls.prg`
- Create: `c:\HarbourLLVM\core\tests\llvm\fallback.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh` (add a "no interpreter loop" assertion)

- [ ] **Step 1: Write the corpus programs**

`tests/llvm/arith.prg` (locals, arithmetic, literals):

```harbour
function Main()
   local a := 7, b := 5
   ? a + b, a - b, a * b, a / b
   ? -a, ( a + b ) * 2
   return nil
```

`tests/llvm/loop.prg` (jumps, comparisons):

```harbour
function Main()
   local i, nSum := 0
   for i := 1 to 10
      nSum := nSum + i
   next
   ? nSum
   return nil
```

`tests/llvm/calls.prg` (function calls, return values):

```harbour
function Main()
   ? Twice( 21 )
   return nil

function Twice( n )
   return n * 2
```

`tests/llvm/fallback.prg` (uses a codeblock — outside the subset, must hit the fallback and still be correct):

```harbour
function Main()
   local b := {| x | x + 1 }
   ? Eval( b, 41 )
   return nil
```

- [ ] **Step 2: Add the "no interpreter loop" check to `run.sh`**

In `tests/llvm/run.sh`, after the IR is generated for each program, add a check: for programs other than `fallback`, the emitted `.ll` must contain `hb_vmsh_` and must NOT contain `call void @hb_vmExecute` inside the `HB_FUN_MAIN` definition. Implement it as a grep step that records pass/fail into the existing report. For `fallback.prg` the opposite: it is expected to still contain the `hb_vmExecute` call (fallback path) — assert that, so the fallback is proven to trigger.

- [ ] **Step 3: Build and run the full corpus**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
for p in arith loop calls fallback; do
  env PATH="/c/Windows/System32:/c/Windows" \
    bin/win/mingw64/harbour.exe -GL -q -obuild/e2e_$p tests/llvm/$p.prg
  ./build/e2e_$p.exe > build/e2e_${p}_ll.out
  bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/e2e_${p}_c tests/llvm/$p.prg
  ./build/e2e_${p}_c.exe > build/e2e_${p}_c.out
  diff build/e2e_${p}_c.out build/e2e_${p}_ll.out && echo "$p: MATCH" || echo "$p: DIFF"
done
```

Expected: all four print `MATCH` — the straight-line backend (and, for `fallback.prg`, the fallback path) produces output identical to the C backend, with the MinGW toolchain off PATH.

- [ ] **Step 4: Confirm the dispatch loop is gone where expected**

```bash
cd c:/HarbourLLVM/core
grep -c "hb_vmsh_" build/e2e_arith.ll      # > 0  : straight-line emitted
grep -c "hb_vmExecute" build/e2e_arith.ll  # 0 calls inside HB_FUN_MAIN
grep -c "hb_vmExecute" build/e2e_fallback.ll  # > 0 : fallback used
```

Expected: `arith.ll` uses the shims and has no `hb_vmExecute` call in `HB_FUN_MAIN`; `fallback.ll` still routes through `hb_vmExecute`.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/arith.prg tests/llvm/loop.prg tests/llvm/calls.prg \
        tests/llvm/fallback.prg tests/llvm/run.sh
git commit -m "test: pcode-subset corpus and no-interpreter-loop assertions"
```

---

## Self-Review

**Spec coverage:** Plan 3 implements Spec 1's Plan 3 ("exported runtime op shim, unroll pcode to straight-line IR, remove the interpreter loop"). Task 1 = the shim; Task 2 = the opcode decoder; Task 3 = jump/basic-block analysis; Task 4 = the straight-line emitter with the whole-function fallback; Task 5 = end-to-end + corpus + the assertion that the dispatch loop is gone. The spec addendum's note that the op functions are `static` in `hvm.c` is handled by putting the shim inside `hvm.c` (Task 1).

**Placeholder scan:** Task 1 Step 2 and Task 2 Step 2 deliberately leave parts as "fill every row / write one per declared shim, following the shown pattern" — this is a mechanical expansion of a fully-shown pattern against a named, enumerated list, anchored to the exact source (`hb_vmExecute` switch in `hvm.c`), not an under-specified placeholder. Every shim is declared in the header and every opcode of the subset is enumerated in the Scope section and the table comment.

**Type consistency:** the shim functions return `int` everywhere (header, implementations, IR `declare`s); `hb_vmsh_poplogical` takes an `i32*` out-parameter consistently in the header and the IR `declare`. `HB_PCINFO` / `HB_PCMAP` / `hb_pcodeInstrLen` / `hb_pcodeAnalyze` / `hb_pcodeJumpOffset` signatures match across `hb_pcdec.h`, `hb_pcdec.c`, and the genllvm.c call sites.

**Known risks:** (1) getting every shim bit-identical to its interpreter case — mitigated by the whole-function fallback and the diff-against-C-backend corpus. (2) Jump-displacement base (instruction start vs operand end) — Task 3 Step 2 flags this explicitly to verify against `hvm.c`. (3) Action-request semantics mid-function — handled uniformly by the return-int + branch-to-epilogue model.
