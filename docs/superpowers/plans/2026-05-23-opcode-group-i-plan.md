# Opcode Group I — SEQUENCE — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend to cover all 7 SEQUENCE opcodes (`SEQBEGIN`, `SEQEND`, `SEQRECOVER`, `SEQALWAYS`, `ALWAYSBEGIN`, `ALWAYSEND`, `SEQBLOCK`) so functions using `BEGIN SEQUENCE / RECOVER / END` (try/catch) or `BEGIN SEQUENCE / ALWAYS / END` (try/finally) emit straight-line native code instead of falling back, whole-function, to the `hb_vmExecute` interpreter. Last planned opcode-group extension.

**Architecture:** Seven thin `hb_vmsh_seq*` / `hb_vmsh_always*` shims in `src/vm/hvm.c`, each forwarding to the same `hb_*` helpers and reproducing the same envelope bookkeeping the interpreter's `HB_P_SEQ*` cases do. Decoder rows flip `fSupported` to `HB_TRUE` (no length change). Emitter in `src/compiler/genllvm.c` gains a compile-time region stack and two new `HB_EMIT_*_SHIM_SEQ` macros that emit a per-shim action-request *dispatch* (distinguishing `HB_BREAK_REQUESTED` from `HB_QUIT_REQUESTED`) routing to the correct in-function recover/always label; outside any region the existing `br i1 ... epilogue` shape is unchanged. Corpus exercises every SEQUENCE form. `tests/llvm/fallback.prg` repointed from `BEGIN SEQUENCE` to `HB_P_PUSHTIMESTAMP` (which stays unsupported) so the sentinel test still falls back.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-23-opcode-group-i-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode groups A–H.

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
- `win-make` aborts at a pre-existing, unrelated `harbour-32-x64.dll` link failure — predates this work, irrelevant. Static libraries and `bin/win/mingw64/harbour.exe` build BEFORE that step.
- **`hvm.c` is compiled via a unity file.** `libhbvm.a` is built from `src/vm/hvmall.c`, which `#include`s `hvm.c`. `make` tracks only `hvmall.c`. After editing `hvm.c` you MUST `touch src/vm/hvmall.c` before `./win-make.exe`, or the new shims will not land in `libhbvm.a`. Verify with `nm lib/win/mingw64/libhbvm.a | grep "hb_vmsh_seq\|hb_vmsh_always"` after the build.
- The `harbour.exe` Makefile tracks only `.o` deps — if a `genllvm.c` change did not relink `harbour.exe` (`win-make` says `harbour.exe is up to date`), run a standalone `touch src/main/harbour.c` then `./win-make.exe`.
- `hbmk2`: `bin/win/mingw64/hbmk2.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.
- Shell note: do not combine `cd` with output redirection (`>`) in one command. Run a standalone `cd` first.

---

## Opcode → interpreter case map (reference)

The implementer MUST read each `case HB_P_*:` arm in `hb_vmExecute` and confirm every shim mirrors its interpreter case body byte-for-byte.

| # | Opcode | Bytes | Interpreter case | Lines (hvm.c) |
|---|--------|-------|------------------|---------------|
| 113 | `HB_P_SEQBEGIN` | 4 | push `HB_IT_RECOVER` envelope; `pCode += 4` | 2040–2080 |
| 114 | `HB_P_SEQEND` | 4 | restore prev state; `hb_stackDec()`; `hb_stackPop()`; `pCode += MKINT24` | 2083–2109 |
| 115 | `HB_P_SEQRECOVER` | 1 | restore prev state; `hb_stackDec()`; leave break value on stack | 2111–2129 |
| 166 | `HB_P_SEQALWAYS` | 4 | like SEQBEGIN but `flags |= HB_SEQ_DOALWAYS` | 1941–1981 |
| 167 | `HB_P_ALWAYSBEGIN` | 4 | update envelope recover-addr; fold request into flags; clear request; move return value if ENDPROC | 1984–2000 |
| 168 | `HB_P_ALWAYSEND` | 1 | restore action request from envelope; pop envelope; restore return value | 2002–2037 |
| 178 | `HB_P_SEQBLOCK` | 1 | `hb_vmSeqBlock()` | 1936–1939 |

Action request constants (`include/hbvm.h` lines 136-138): `HB_QUIT_REQUESTED = 1`, `HB_BREAK_REQUESTED = 2`, `HB_ENDPROC_REQUESTED = 4`.
Envelope flags (`src/vm/hvm.c` lines 269-270): `HB_SEQ_CANRECOVER = 64`, `HB_SEQ_DOALWAYS = 128`.
Stack offsets (`src/vm/hvm.c` lines 266-267): `HB_RECOVER_STATE = -1` (envelope), `HB_RECOVER_VALUE = -2` (break value slot).
`HB_IT_RECOVER = 0x80000` (`include/hbapi.h` line 90).

`hb_vmSeqBlock`, `hb_stackGetRecoverBase`, `hb_stackSetRecoverBase`, and `bCanRecover` (the local flag in `hb_vmExecute`) are all `static` in `hvm.c` — only reachable from the same TU (the shim block at end of `hvm.c`). The shims do NOT need the `bCanRecover` local because the compile-time region stack carries the equivalent information per straight-line function.

---

### Task 1: The seven `hb_vmsh_seq*` / `hb_vmsh_always*` shims

**Files:**
- Modify: `include/hbvmsh.h` (add 7 declarations)
- Modify: `src/vm/hvm.c` (append 7 shims after the group H block)
- Test: `tests/llvm/shim_smoke_i.c`

- [ ] **Step 1: Add 7 declarations to `hbvmsh.h`**

In `include/hbvmsh.h`, inside `HB_EXTERN_BEGIN … HB_EXTERN_END`, after the last group H declaration (`hb_vmsh_macrosend`), add:

```c
/* --- group I: SEQUENCE --- */
/* Each shim mirrors its interpreter case body byte-for-byte; the envelope
 * pushed on the stack is the same HB_IT_RECOVER item the interpreter pushes,
 * so hb_vmRequestBreak() and the error subsystem (which walk the sequence
 * stack) see identical layout. Group I uses compile-time region tracking in
 * the emitter for the actual control transfer — the runtime envelope is for
 * cross-language compatibility, not control flow. */

extern HB_EXPORT int hb_vmsh_seqbegin     ( const unsigned char * pRecoverAddr );
extern HB_EXPORT int hb_vmsh_seqend       ( void );
extern HB_EXPORT int hb_vmsh_seqrecover   ( void );
extern HB_EXPORT int hb_vmsh_seqalways    ( const unsigned char * pAlwaysAddr );
extern HB_EXPORT int hb_vmsh_alwaysbegin  ( const unsigned char * pAlwaysEndAddr );
extern HB_EXPORT int hb_vmsh_alwaysend    ( void );
extern HB_EXPORT int hb_vmsh_seqblock     ( void );
```

- [ ] **Step 2: Append the 7 shim implementations to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group H shim block, add:

```c
/* --- group I: SEQUENCE --- */

HB_EXPORT int hb_vmsh_seqbegin( const unsigned char * pRecoverAddr )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pItem;

   /* 1) clear the storage for value returned by BREAK statement */
   hb_stackAllocItem()->type = HB_IT_NIL;

   /* 2) recover envelope */
   pItem = hb_stackAllocItem();
   pItem->type = HB_IT_RECOVER;
   pItem->item.asRecover.recover = pRecoverAddr;
   pItem->item.asRecover.base    = hb_stackGetRecoverBase();
   /* Straight-line code does not use bCanRecover (compile-time tracking
    * does); set CANRECOVER unconditionally so a nested interpreter-run
    * SEQUENCE that walks the stack sees this as a valid recover frame. */
   pItem->item.asRecover.flags   = HB_SEQ_CANRECOVER;
   pItem->item.asRecover.request = 0;

   hb_stackSetRecoverBase( hb_stackTopOffset() );
   return 0;
}

HB_EXPORT int hb_vmsh_seqend( void )
{
   HB_STACK_TLS_PRELOAD
   hb_stackSetRecoverBase( hb_stackItemFromTop( HB_RECOVER_STATE )->item.asRecover.base );
   hb_stackDec();           /* pop the envelope */
   hb_stackPop();           /* pop the break-value slot */
   return 0;
}

HB_EXPORT int hb_vmsh_seqrecover( void )
{
   HB_STACK_TLS_PRELOAD
   hb_stackSetRecoverBase( hb_stackItemFromTop( HB_RECOVER_STATE )->item.asRecover.base );
   hb_stackDec();           /* pop envelope; leave break-value as next stack top */
   return 0;
}

HB_EXPORT int hb_vmsh_seqalways( const unsigned char * pAlwaysAddr )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pItem;

   hb_stackAllocItem()->type = HB_IT_NIL;

   pItem = hb_stackAllocItem();
   pItem->type = HB_IT_RECOVER;
   pItem->item.asRecover.recover = pAlwaysAddr;
   pItem->item.asRecover.base    = hb_stackGetRecoverBase();
   pItem->item.asRecover.flags   = HB_SEQ_DOALWAYS | HB_SEQ_CANRECOVER;
   pItem->item.asRecover.request = 0;

   hb_stackSetRecoverBase( hb_stackTopOffset() );
   return 0;
}

HB_EXPORT int hb_vmsh_alwaysbegin( const unsigned char * pAlwaysEndAddr )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pRecover = hb_stackItemFromTop( HB_RECOVER_STATE );

   pRecover->item.asRecover.recover = pAlwaysEndAddr;
   /* fold pending request into flags, clear request */
   pRecover->item.asRecover.flags  |= pRecover->item.asRecover.request;
   pRecover->item.asRecover.request = 0;
   /* preserve RETURN value if ENDPROC was requested */
   if( pRecover->item.asRecover.flags & HB_ENDPROC_REQUESTED )
      hb_itemMove( hb_stackItemFromTop( HB_RECOVER_VALUE ), hb_stackReturnItem() );
   return 0;
}

HB_EXPORT int hb_vmsh_alwaysend( void )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM   pRecover    = hb_stackItemFromTop( HB_RECOVER_STATE );
   HB_USHORT  uiPrevAction = pRecover->item.asRecover.flags;
   HB_USHORT  uiCurrAction = pRecover->item.asRecover.request;
   int        iAction;

   hb_stackSetRecoverBase( pRecover->item.asRecover.base );

   if( ( uiCurrAction | uiPrevAction ) & HB_QUIT_REQUESTED )
      iAction = HB_QUIT_REQUESTED;
   else if( ( uiCurrAction | uiPrevAction ) & HB_BREAK_REQUESTED )
      iAction = HB_BREAK_REQUESTED;
   else if( ( uiCurrAction | uiPrevAction ) & HB_ENDPROC_REQUESTED )
      iAction = HB_ENDPROC_REQUESTED;
   else
      iAction = 0;

   hb_stackSetActionRequest( ( HB_USHORT ) iAction );
   hb_stackDec();           /* remove the ALWAYS envelope */

   /* restore RETURN value if not overloaded inside ALWAYS code */
   if( ! ( uiCurrAction & HB_ENDPROC_REQUESTED ) &&
         (   uiPrevAction & HB_ENDPROC_REQUESTED ) )
      hb_stackPopReturn();
   else
      hb_stackPop();

   return iAction;
}

HB_EXPORT int hb_vmsh_seqblock( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmSeqBlock();
   return ( int ) hb_stackGetActionRequest();
}
```

Before writing, read each `case HB_P_*:` arm in `hb_vmExecute` (`src/vm/hvm.c` per the table above) and confirm every shim mirrors the interpreter byte-for-byte. The straight-line SEQBEGIN sets `HB_SEQ_CANRECOVER` unconditionally (a deliberate simplification — the compile-time region stack carries the per-function recover-eligibility info); a stack walker from interpreted code still sees a valid envelope. Confirm `hb_stackPopReturn` exists (it does — used at line 2033). Confirm `hb_itemMove` takes `(PHB_ITEM dst, PHB_ITEM src)` (it does — used at line 1998).

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_i.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[] = {
      ( void * ) hb_vmsh_seqbegin,
      ( void * ) hb_vmsh_seqend,
      ( void * ) hb_vmsh_seqrecover,
      ( void * ) hb_vmsh_seqalways,
      ( void * ) hb_vmsh_alwaysbegin,
      ( void * ) hb_vmsh_alwaysend,
      ( void * ) hb_vmsh_seqblock
   };
   int i;
   for( i = 0; i < ( int )( sizeof( p ) / sizeof( p[ 0 ] ) ); ++i )
      printf( "group I shim %d linkable: %p\n", i, p[ i ] );
   return 0;
}
```

- [ ] **Step 4: Rebuild and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
touch src/vm/hvmall.c
./win-make.exe
```

Then (separate command):

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
nm lib/win/mingw64/libhbvm.a | grep -E "T hb_vmsh_(seq|always)" | sort
gcc tests/llvm/shim_smoke_i.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -Wl,--noinhibit-exec -o build/shim_smoke_i.exe
./build/shim_smoke_i.exe
```

Expected: `hvm.c` compiles clean; `nm | grep` lists 7 `T` symbols (`hb_vmsh_alwaysbegin`, `hb_vmsh_alwaysend`, `hb_vmsh_seqalways`, `hb_vmsh_seqbegin`, `hb_vmsh_seqblock`, `hb_vmsh_seqend`, `hb_vmsh_seqrecover`); `shim_smoke_i.exe` prints 7 lines each with a non-null pointer.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_i.c
git commit -m "vm: add group I op shims hb_vmsh_seq*/hb_vmsh_always* (SEQUENCE)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Decoder — mark all 7 SEQUENCE opcodes supported

**Files:**
- Modify: `src/compiler/hb_pcdec.c`
- Modify: `tests/llvm/pcdectest.c`

The 7 rows are already `HB_PCK_FIXED` with the correct `nLen`. Task 2 only flips `fSupported` and updates the header comment. NO length-computation change.

- [ ] **Step 1: Flip the seven table rows**

In `src/compiler/hb_pcdec.c`'s `hb_pcInfo[]`, change `fSupported` from `HB_FALSE` to `HB_TRUE` for:

| Row | Opcode |
|-----|--------|
| 113 | `HB_P_SEQBEGIN` |
| 114 | `HB_P_SEQEND` |
| 115 | `HB_P_SEQRECOVER` |
| 166 | `HB_P_SEQALWAYS` |
| 167 | `HB_P_ALWAYSBEGIN` |
| 168 | `HB_P_ALWAYSEND` |
| 178 | `HB_P_SEQBLOCK` |

Read each row first; change ONLY the `HB_FALSE` token; keep `kind`, `nLen`, and trailing comment untouched.

- [ ] **Step 2: Update the file-header "in scope" comment**

After the `Group H additions (macros):` paragraph, add:

```c
 *
 * Group I additions (SEQUENCE):
 *   HB_P_SEQBEGIN, HB_P_SEQEND, HB_P_SEQRECOVER,
 *   HB_P_SEQALWAYS, HB_P_ALWAYSBEGIN, HB_P_ALWAYSEND,
 *   HB_P_SEQBLOCK
```

- [ ] **Step 3: Add a decoder test**

In `tests/llvm/pcdectest.c`, after the Group H test block and before `return 0;`, add:

```c
   {
      /* Group I: the 7 SEQUENCE opcodes are now in the straight-line subset. */
      assert( hb_pcInfo[ HB_P_SEQBEGIN    ].fSupported );
      assert( hb_pcInfo[ HB_P_SEQEND      ].fSupported );
      assert( hb_pcInfo[ HB_P_SEQRECOVER  ].fSupported );
      assert( hb_pcInfo[ HB_P_SEQALWAYS   ].fSupported );
      assert( hb_pcInfo[ HB_P_ALWAYSBEGIN ].fSupported );
      assert( hb_pcInfo[ HB_P_ALWAYSEND   ].fSupported );
      assert( hb_pcInfo[ HB_P_SEQBLOCK    ].fSupported );
      printf( "pcdec: 7 SEQUENCE opcodes supported\n" );
   }
```

Also update the `Expected:` block in the file-header comment of `pcdectest.c` to add: `pcdec: 7 SEQUENCE opcodes supported`.

- [ ] **Step 4: Rebuild and run the decoder test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
touch src/compiler/hb_pcdec.c
./win-make.exe
```

Then:

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: prints all prior lines plus `pcdec: 7 SEQUENCE opcodes supported`.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c tests/llvm/pcdectest.c
git commit -m "compiler: mark 7 SEQUENCE opcodes supported in the decoder (group I)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Emitter — region tracking + SEQ dispatch + 7 case arms

**Files:** Modify `src/compiler/genllvm.c`.

This is the largest task. Split into substeps. Before starting, re-read:
- `hb_llvmSLEmitBody` signature and the existing big opcode `switch` shape
- `HB_EMIT_NOARG_SHIM` and `HB_EMIT_INT1_SHIM` macros (the templates the new SEQ variants mirror)
- The group F `case HB_P_SWITCH:` arm (uses the in-place `@.pcode.<func>` GEP technique that group I's SEQBEGIN/SEQALWAYS/ALWAYSBEGIN also use)
- The group G `case HB_P_PUSHBLOCK*:` arm (uses `pFunc->szName` via `hb_llvmEmitFuncName`)

- [ ] **Step 1: Add the 7 IR `declare` lines**

In `hb_compGenLLVMCode`'s module-header emission, immediately after the last group H declaration (`hb_vmsh_macrosend(i32)`), add:

```c
   /* Group I: SEQUENCE shim declarations */
   fprintf( yyc, "declare i32 @hb_vmsh_seqbegin(i8*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_seqend()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_seqrecover()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_seqalways(i8*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_alwaysbegin(i8*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_alwaysend()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_seqblock()\n" );
```

- [ ] **Step 2: Add the region stack to `hb_llvmSLEmitBody`**

At the top of `hb_llvmSLEmitBody`, alongside the existing local declarations, add:

```c
   /* Group I: compile-time SEQUENCE region tracking */
   struct {
      HB_SIZE recover_pos;   /* in-function offset of SEQRECOVER or ALWAYSEND (BREAK lands here) */
      HB_SIZE always_pos;    /* in-function offset of ALWAYSEND (QUIT/ENDPROC lands here); 0 if not an ALWAYS region */
      HB_BOOL fIsAlways;
   } seq_stack[ 16 ];
   int seq_depth = 0;
```

(Nesting deeper than 16 is unrealistic in practical code. If it ever happens the emitter must produce correct output anyway — see Step 5 fallback note.)

- [ ] **Step 3: Add a region-aware action-request dispatch helper**

Right after the existing `HB_EMIT_INT1_SHIM` macro definition, add a helper *function* (NOT a macro — it needs to emit conditional IR based on `seq_depth`):

```c
/* Emit the action-request branch for the shim call at `pos`. When inside an
 * active SEQUENCE region (seq_depth > 0), route HB_BREAK_REQUESTED to the
 * topmost RECOVER target and HB_QUIT_REQUESTED to the topmost ALWAYS target;
 * everything else goes to %epilogue. When outside any region, emit the
 * standard "br i1 %c<pos>, label %epilogue, label %<next>" shape. */
static void hb_llvmSLEmitActionCheck( FILE * yyc, HB_SIZE pos,
                                       const char * szNextLabel,
                                       void * seq_stack_ptr, int seq_depth )
{
   if( seq_depth == 0 )
   {
      fprintf( yyc,
               "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
               "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
               ( unsigned long ) pos, ( unsigned long ) pos,
               ( unsigned long ) pos, szNextLabel );
   }
   else
   {
      /* In a SEQUENCE region. Distinguish BREAK vs QUIT vs other. */
      typeof( ( ( void ) 0, seq_stack_ptr ) ) sp = seq_stack_ptr; /* see Step 2 type */
      /* Find topmost RECOVER target (any region) and topmost ALWAYS target. */
      HB_SIZE recover_target = ( HB_SIZE ) -1;
      HB_SIZE always_target  = ( HB_SIZE ) -1;
      int i;
      for( i = seq_depth - 1; i >= 0; --i )
      {
         if( recover_target == ( HB_SIZE ) -1 )
            recover_target = ( ( typeof( sp ) ) sp )[ i ].recover_pos;
         if( always_target == ( HB_SIZE ) -1 && ( ( typeof( sp ) ) sp )[ i ].fIsAlways )
            always_target = ( ( typeof( sp ) ) sp )[ i ].always_pos;
         if( recover_target != ( HB_SIZE ) -1 && always_target != ( HB_SIZE ) -1 )
            break;
      }
      fprintf( yyc,
               "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
               "  br i1 %%c%lu, label %%seqd%lu, label %%%s\n"
               "seqd%lu:\n"
               "  %%brk%lu = icmp eq i32 %%r%lu, 2\n"   /* HB_BREAK_REQUESTED */
               "  br i1 %%brk%lu, label %%i%lu, label %%seqq%lu\n"
               "seqq%lu:\n",
               ( unsigned long ) pos, ( unsigned long ) pos,
               ( unsigned long ) pos, ( unsigned long ) pos, szNextLabel,
               ( unsigned long ) pos,
               ( unsigned long ) pos, ( unsigned long ) pos,
               ( unsigned long ) pos, ( unsigned long ) recover_target,
               ( unsigned long ) pos,
               ( unsigned long ) pos );
      if( always_target != ( HB_SIZE ) -1 )
      {
         fprintf( yyc,
                  "  %%qit%lu = icmp eq i32 %%r%lu, 1\n"   /* HB_QUIT_REQUESTED */
                  "  br i1 %%qit%lu, label %%i%lu, label %%epilogue\n",
                  ( unsigned long ) pos, ( unsigned long ) pos,
                  ( unsigned long ) pos, ( unsigned long ) always_target );
      }
      else
      {
         fprintf( yyc, "  br label %%epilogue\n" );
      }
   }
}
```

NOTE on the `typeof` use — the project may be C89; if `typeof` is not accepted by the project's gcc invocation, replace with a small explicit type passed alongside (i.e. inline the loop body in the helper without `typeof`, or move the seq_stack to file scope with a named type). Read the project's compile flags first; if `-std=c99` or `-std=gnu99` or default gcc, `typeof` works. The win-make output for `hvmall.c` and `genllvm.c` shows `gcc -W -Wall -O3` without `-std=`, which defaults to gnu17 in modern gcc — `typeof` works.

A cleaner alternative: lift the `seq_stack` definition to a file-static named type at the top of `genllvm.c`, then `static void hb_llvmSLEmitActionCheck(... struct hb_seq_region *seq_stack, int seq_depth)`. Implementer's choice; either is correct. The plan's intent is "the macro/helper knows about the region stack".

- [ ] **Step 4: Modify EVERY existing shim-emitting case to use the new dispatch helper instead of the inline action-request check**

In `HB_EMIT_NOARG_SHIM` and `HB_EMIT_INT1_SHIM` (the macros at the top of the switch), AND in every inlined single-int-operand case (the ARRAYDIM/ARRAYGEN/HASHGEN/LOCAL*/STATIC*/FRAME/FUNCTION/DO/MESSAGE/etc.), change the trailing two lines:

```c
            "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
            "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
```
+ the corresponding `(unsigned long) pos, ...` args

into a call to the new helper:

```c
            /* terminating br emitted by the dispatch helper */
);
hb_llvmSLEmitActionCheck( yyc, pos, szNextLabel, seq_stack, seq_depth );
```

This is a substantial textual change — every case that uses the standard action-request check needs to switch. Read the file first; about 30-40 sites will need touching. Pattern is mechanical (same `fprintf` truncation + helper call) but volume is high. Take care that the `fprintf` argument lists shrink correctly — the helper handles the trailing `%c<pos>/icmp/br` lines, the per-shim `fprintf` only emits the `%r<pos> = call ...` line.

Concretely the existing `HB_EMIT_NOARG_SHIM` becomes:

```c
#define HB_EMIT_NOARG_SHIM( nm ) \
            do { \
               fprintf( yyc, \
                        "  %%r%lu = call i32 @hb_vmsh_" nm "()\n", \
                        ( unsigned long ) pos ); \
               hb_llvmSLEmitActionCheck( yyc, pos, szNextLabel, \
                                          seq_stack, seq_depth ); \
            } while( 0 )
```

And `HB_EMIT_INT1_SHIM`:

```c
#define HB_EMIT_INT1_SHIM( nm, val ) \
            do { \
               fprintf( yyc, \
                        "  %%r%lu = call i32 @hb_vmsh_" nm "(i32 %u)\n", \
                        ( unsigned long ) pos, ( unsigned ) ( val ) ); \
               hb_llvmSLEmitActionCheck( yyc, pos, szNextLabel, \
                                          seq_stack, seq_depth ); \
            } while( 0 )
```

For inlined cases (e.g. `case HB_P_ARRAYDIM:`), truncate the trailing two lines and the corresponding args from the `fprintf`, then call `hb_llvmSLEmitActionCheck(...)`.

**Critical:** Some existing inlined cases have non-standard action-request shapes (e.g. `case HB_P_PUSHFUNCSYM:` emits TWO shim calls with an intermediate jump label `%ipfs<pos>a`). Those cases keep their existing shape — they are NOT inside SEQUENCE regions in a way the new dispatch needs to handle. Treat them as exceptions: leave their bodies unchanged, but their LAST shim call's action check still needs to route correctly if inside a SEQUENCE region. Pragmatic rule: if a case emits multiple shim calls with their own intermediate labels, only the FINAL action check (the one before `br ... %<next>`) needs converting. The implementer should read the file and decide per-case; about half a dozen are "complex" multi-call cases.

If converting every site is too risky in one task, an acceptable simplification: emit dispatch only for shims emitted via `HB_EMIT_NOARG_SHIM` / `HB_EMIT_INT1_SHIM`. The complex multi-call inlined cases (PUSHFUNCSYM, two-stage MESSAGE, etc.) keep the existing `br ... epilogue` shape. The risk: a BREAK arising from those specific complex paths inside a SEQUENCE region would propagate up instead of going to RECOVER. Document this limitation; the corpus's BREAK cases should use simple shim paths to avoid hitting it.

- [ ] **Step 5: Add the 7 SEQUENCE emitter case arms**

In `hb_llvmSLEmitBody`'s opcode `switch`, immediately before the `default:` arm (after the group H cases), add:

```c
         /* Group I: SEQUENCE */
         case HB_P_SEQBEGIN:
         {
            HB_ISIZ disp = HB_PCODE_MKINT24( &pCode[ pos + 1 ] );
            HB_SIZE recover_pos = ( HB_SIZE )( ( HB_ISIZ ) pos + disp );
            if( seq_depth >= 16 )
               /* nesting too deep; bail by emitting fallback (this whole
                * function should NOT have entered hb_llvmSLEmitBody — Pass A
                * fAllSupported should have rejected it via a future check) */
               { fprintf( yyc, "  br label %%epilogue\n" ); break; }
            seq_stack[ seq_depth ].recover_pos = recover_pos;
            seq_stack[ seq_depth ].always_pos  = 0;
            seq_stack[ seq_depth ].fIsAlways   = HB_FALSE;
            seq_depth++;
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_seqbegin("
                     "i8* getelementptr([%lu x i8], [%lu x i8]* @.pcode.",
                     ( unsigned long ) pos,
                     ( unsigned long ) nPCSize, ( unsigned long ) nPCSize );
            hb_llvmEmitFuncName( yyc, pFunc->szName );
            fprintf( yyc, ", i32 0, i32 %lu))\n",
                     ( unsigned long ) recover_pos );
            /* shim always returns 0; standard dispatch handles next */
            hb_llvmSLEmitActionCheck( yyc, pos, szNextLabel,
                                       seq_stack, seq_depth );
            break;
         }

         case HB_P_SEQEND:
         {
            HB_ISIZ disp = HB_PCODE_MKINT24( &pCode[ pos + 1 ] );
            HB_SIZE end_pos = ( HB_SIZE )( ( HB_ISIZ ) pos + disp );
            char szEndLabel[ 32 ];
            if( seq_depth > 0 )
               seq_depth--;     /* pop region BEFORE emitting next-shim dispatch */
            if( end_pos >= nPCSize )
               hb_strncpy( szEndLabel, "epilogue", sizeof( szEndLabel ) - 1 );
            else
               hb_snprintf( szEndLabel, sizeof( szEndLabel ), "i%lu",
                            ( unsigned long ) end_pos );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_seqend()\n"
                     "  br label %%%s\n",
                     ( unsigned long ) pos, szEndLabel );
            break;
         }

         case HB_P_SEQRECOVER:
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_seqrecover()\n"
                     "  br label %%%s\n",
                     ( unsigned long ) pos, szNextLabel );
            break;

         case HB_P_SEQALWAYS:
         {
            HB_ISIZ disp = HB_PCODE_MKINT24( &pCode[ pos + 1 ] );
            HB_SIZE always_pos = ( HB_SIZE )( ( HB_ISIZ ) pos + disp );
            if( seq_depth >= 16 )
               { fprintf( yyc, "  br label %%epilogue\n" ); break; }
            seq_stack[ seq_depth ].recover_pos = always_pos;
            seq_stack[ seq_depth ].always_pos  = always_pos;
            seq_stack[ seq_depth ].fIsAlways   = HB_TRUE;
            seq_depth++;
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_seqalways("
                     "i8* getelementptr([%lu x i8], [%lu x i8]* @.pcode.",
                     ( unsigned long ) pos,
                     ( unsigned long ) nPCSize, ( unsigned long ) nPCSize );
            hb_llvmEmitFuncName( yyc, pFunc->szName );
            fprintf( yyc, ", i32 0, i32 %lu))\n",
                     ( unsigned long ) always_pos );
            hb_llvmSLEmitActionCheck( yyc, pos, szNextLabel,
                                       seq_stack, seq_depth );
            break;
         }

         case HB_P_ALWAYSBEGIN:
         {
            HB_ISIZ disp = HB_PCODE_MKINT24( &pCode[ pos + 1 ] );
            HB_SIZE always_end_pos = ( HB_SIZE )( ( HB_ISIZ ) pos + disp );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_alwaysbegin("
                     "i8* getelementptr([%lu x i8], [%lu x i8]* @.pcode.",
                     ( unsigned long ) pos,
                     ( unsigned long ) nPCSize, ( unsigned long ) nPCSize );
            hb_llvmEmitFuncName( yyc, pFunc->szName );
            fprintf( yyc, ", i32 0, i32 %lu))\n"
                          "  br label %%%s\n",
                     ( unsigned long ) always_end_pos, szNextLabel );
            break;
         }

         case HB_P_ALWAYSEND:
         {
            if( seq_depth > 0 )
               seq_depth--;     /* pop the ALWAYS region BEFORE next-shim dispatch */
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_alwaysend()\n",
                     ( unsigned long ) pos );
            /* Standard dispatch: shim returns the pending action (0 / 1 /
             * 2 / 4). Re-route via the (now-popped) outer region's stack. */
            hb_llvmSLEmitActionCheck( yyc, pos, szNextLabel,
                                       seq_stack, seq_depth );
            break;
         }

         case HB_P_SEQBLOCK:
            HB_EMIT_NOARG_SHIM( "seqblock" );
            break;
```

- [ ] **Step 6: Add a SEQUENCE-region precheck**

Before emitting body, validate that the function's SEQBEGIN/SEQALWAYS nesting depth never exceeds 16. If it does, return `HB_FALSE` from `hb_llvmSLPrecheck` so the function falls back. Add this scan to `hb_llvmSLPrecheck` (the function that decides whether a function is fully supported).

Read `hb_llvmSLPrecheck` first (it iterates pcode using `hb_pcodeInstrLen` and checks `fSupported`). Add a depth-tracking pass:

```c
   /* Group I: bail if SEQUENCE/ALWAYS nesting exceeds the emitter's stack. */
   {
      HB_SIZE  off = 0;
      int      depth = 0;
      while( off < nPCSize )
      {
         HB_BYTE op = pCode[ off ];
         if( op == HB_P_SEQBEGIN || op == HB_P_SEQALWAYS )
         {
            if( ++depth > 16 )
               return HB_FALSE;
         }
         else if( op == HB_P_SEQEND || op == HB_P_ALWAYSEND )
            depth--;
         off += hb_pcodeInstrLen( &pCode[ off ] );
      }
   }
```

Place it after the existing per-opcode-supported check.

- [ ] **Step 7: Rebuild Harbour**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
```

If `harbour.exe is up to date`, force the relink:
```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
touch src/main/harbour.c
./win-make.exe
```

Expected: `genllvm.c` compiles clean; `harbour.exe` relinks.

- [ ] **Step 8: Check the IR for a tiny try/catch program**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
printf 'function Main()\n   begin sequence\n      ? "try"\n      break\n      ? "unreachable"\n   recover\n      ? "caught"\n   end sequence\n   ? "done"\n   return nil\n' > build/sqtmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3i build/sqtmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3i.ll -o NUL
grep -c "call i32 @hb_vmsh_seqbegin\|call i32 @hb_vmsh_seqrecover\|call i32 @hb_vmsh_seqend" build/t3i.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3i.ll | grep -c "call void @hb_vmExecute"
```

Expected: IR verifies clean; total shim count is 3 (one each of seqbegin/seqrecover/seqend); `HB_FUN_MAIN` fallback count is 0.

- [ ] **Step 9: Run it end-to-end**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/t3irun build/sqtmp.prg
./build/t3irun.exe
```

Expected output (with leading blank line from `?`):
```

try
caught
done
```

- [ ] **Step 10: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for 7 SEQUENCE opcodes (group I)

Adds compile-time region tracking + region-aware per-shim action-request
dispatch. Inside an active BEGIN SEQUENCE region, HB_BREAK_REQUESTED routes
to the matching RECOVER block in-function; HB_QUIT_REQUESTED routes to the
matching ALWAYS block; everything else propagates to %epilogue. Outside any
region the existing 'br i1 ... epilogue' shape is unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Corpus — verify SEQUENCE

**Files:**
- Create: `tests/llvm/sequence.prg`
- Modify: `tests/llvm/run.sh`

- [ ] **Step 1: Write the corpus program**

Create `tests/llvm/sequence.prg`:

```harbour
//
// Group I corpus — SEQUENCE opcodes straight-lined by the LLVM backend.
//
// Exercises the compatibility-critical paths in distinct functions:
//
//   TryNormal      try body completes normally, no BREAK
//   TryCatch       BREAK in try body, RECOVER catches
//   CrossFn        caller has SEQUENCE, callee does BREAK
//   Nested         inner try/catch inside outer try/catch
//   TryFinallyOk   ALWAYS runs after normal completion
//   TryFinallyBrk  BREAK in try, ALWAYS runs, no RECOVER, BREAK propagates
//
function Main()
   TryNormal()
   TryCatch()
   CrossFn()
   Nested()
   TryFinallyOk()
   TryFinallyBrk()
   ? "main done"
   return nil

function TryNormal()
   begin sequence
      ? "try-normal"
   end sequence
   ? "after-normal"
   return nil

function TryCatch()
   begin sequence
      ? "try-catch-pre"
      break
      ? "unreachable"
   recover
      ? "caught"
   end sequence
   ? "after-catch"
   return nil

function CrossFn()
   begin sequence
      Inner()
      ? "unreachable-outer"
   recover
      ? "caught-cross"
   end sequence
   return nil

function Inner()
   ? "inner-pre"
   break
   ? "unreachable-inner"
   return nil

function Nested()
   begin sequence
      begin sequence
         break
      recover
         ? "inner-caught"
      end sequence
      ? "after-inner"
      break
   recover
      ? "outer-caught"
   end sequence
   return nil

function TryFinallyOk()
   begin sequence
      ? "try-fin-ok"
   always
      ? "always-ran-ok"
   end sequence
   return nil

function TryFinallyBrk()
   begin sequence
      begin sequence
         ? "try-fin-brk"
         break
      always
         ? "always-ran-brk"
      end sequence
   recover
      ? "outer-recovered"
   end sequence
   return nil
```

(Note: the BREAK statement in Harbour without an argument propagates with a NIL value; if any backend errors on a particular form, adjust the program. The corpus is judged by diff-vs-C-backend, NOT by what the output is.)

- [ ] **Step 2: Add `sequence` to `run.sh`'s straight-line-required set**

In `tests/llvm/run.sh`, change the `case "$name" in` line ending `…|codeblock|macro)` to `…|codeblock|macro|sequence)`. Update the adjacent comment to add `I (sequence)`. Read the surrounding `case` first.

- [ ] **Step 3: Run end-to-end + diff vs C backend**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/gi_seq tests/llvm/sequence.prg
./build/gi_seq.exe > build/gi_seq_ll.out 2>&1
bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/gi_seq_c tests/llvm/sequence.prg
./build/gi_seq_c.exe > build/gi_seq_c.out 2>&1
diff build/gi_seq_c.out build/gi_seq_ll.out
```

Expected: empty `diff`.

If `diff` shows differences — INVESTIGATE; this is the correctness gate. Do not commit broken behavior.

- [ ] **Step 4: Confirm per-function straight-line**

```bash
cd c:/HarbourLLVM/core
echo "===PER-OPCODE COUNTS==="
for op in seqbegin seqend seqrecover seqalways alwaysbegin alwaysend seqblock; do
   c=$(grep -c "call i32 @hb_vmsh_${op}(" build/gi_seq.ll)
   echo "$op: $c"
done
echo "===PER-FUNCTION FALLBACK==="
for fn in HB_FUN_MAIN HB_FUN_TRYNORMAL HB_FUN_TRYCATCH HB_FUN_CROSSFN HB_FUN_INNER HB_FUN_NESTED HB_FUN_TRYFINALLYOK HB_FUN_TRYFINALLYBRK; do
   c=$(awk -v fn="$fn" '$0 ~ "^define.*@" fn "\\(\\)"{f=1} f{print} f&&/^\}$/{exit}' build/gi_seq.ll | grep -c "call void @hb_vmExecute")
   echo "$fn: $c"
done
```

Expected: each opcode count ≥ 1 (or 0 for SEQBLOCK if the corpus has no `BEGIN SEQUENCE WITH bBlock` form — that's acceptable); per-function fallback all `0`.

- [ ] **Step 5: Full run.sh**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gi
```

Expected: ends `RESULT: all programs validated and matched the C backend`. (Note: at THIS task, `fallback.prg` will FAIL because `BEGIN SEQUENCE` no longer falls back; Task 5 fixes that.)

If `fallback` is the only failure, that is expected — proceed to Task 5 to fix.

- [ ] **Step 6: Commit**

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/sequence.prg tests/llvm/run.sh
git commit -m "test: corpus for group I — SEQUENCE straight-lined

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Repoint `fallback.prg` to `HB_P_PUSHTIMESTAMP`

**Files:** Modify `tests/llvm/fallback.prg`.

The sentinel currently uses `BEGIN SEQUENCE`, which group I now straight-lines. Switch to `HB_P_PUSHTIMESTAMP` (opcode 22, 9 bytes, fixed; remains `HB_FALSE` in the decoder after group I).

- [ ] **Step 1: Overwrite `tests/llvm/fallback.prg`**

```harbour
//
// Sentinel: this program MUST fall back to hb_vmExecute. Uses a timestamp
// literal {^ ... }, which emits HB_P_PUSHTIMESTAMP (opcode 22, HB_FALSE in
// the straight-line decoder), forcing whole-function fallback. Distinct from
// statics.prg (HB_P_SFRAME) and other corpus programs so the three sentinels
// exercise different unsupported paths.
//
function Main()
   local dStamp := {^ 2026-01-01 12:00:00 }
   ? Hour( dStamp )
   return nil
```

(If Harbour's timestamp literal syntax differs in this fork — read `src/compiler/harbour.y` for the exact production — adjust the literal to whatever the parser accepts. Verify by compiling: it must emit `HB_P_PUSHTIMESTAMP` (grep the IR for the bytecode at HB_FUN_MAIN with `bin/win/mingw64/harbour.exe -GH ...` to dump pcode names if needed, or directly check that the LLVM `HB_FUN_MAIN` body falls back.)

- [ ] **Step 2: Verify**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
bin/win/mingw64/harbour.exe -GL -q -obuild/fb_i tests/llvm/fallback.prg
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/fb_i.ll | grep -c "call void @hb_vmExecute"
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/fb_i_run tests/llvm/fallback.prg
./build/fb_i_run.exe
```

Expected: `HB_FUN_MAIN` fallback count = 1; program prints `12` (Hour of 12:00:00).

- [ ] **Step 3: Full run.sh**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gi-final 2>&1 | tail -10
```

Expected: ends `RESULT: all programs validated and matched the C backend` — every corpus including `sequence` AND `fallback` passes.

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/fallback.prg
git commit -m "test: repoint fallback.prg sentinel from BEGIN SEQUENCE to HB_P_PUSHTIMESTAMP

Group I straight-lines BEGIN SEQUENCE; fallback.prg's previous sentinel
no longer falls back. Switch to a timestamp literal which emits
HB_P_PUSHTIMESTAMP (opcode 22), permanently HB_FALSE in the decoder and
not on any planned-future-group roadmap — a stable fallback sentinel.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:** Spec's three required changes are covered — Task 1 the 7 shims, Task 2 the decoder flips, Task 3 the emitter (region stack + dispatch helper + 7 emitter cases + precheck for depth>16). The spec's testing section is Task 4 (corpus exercising all 6 compatibility cases). The sentinel-break for `fallback.prg` is Task 5.

**Placeholder scan:** All 7 shims shown in full in Task 1 Step 2. The opcode→helper map is a complete table. Decoder flip is given as a complete table. Emitter cases are given as complete code. Corpus program is shown in full. The one approximation is the `typeof` use in Step 3's helper — explicitly called out with an alternative.

**Type consistency:** All 7 shims take either `void` or `const unsigned char *`, return `int`. IR `declare` lines (Task 3 Step 1) match. Call sites (Task 3 Step 5) match.

**Known risks:**
1. The emitter dispatch helper must touch every site that currently uses the standard action-request check. Step 4 is the largest part of Task 3 — about 30-40 sites. If too risky for one task, the implementer can split Task 3 into 3a (helper + region stack), 3b (convert NOARG/INT1 macros), 3c (convert inlined cases + 7 new cases).
2. ALWAYS semantics: `ALWAYSEND` returns the pending action request, which the emitter re-dispatches through whatever OUTER region the popped region sat in. The corpus's `TryFinallyBrk` (BREAK inside ALWAYS-only, no outer RECOVER → BREAK propagates) covers this.
3. The `seq_depth` overflow safety: Task 3 Step 6 adds a precheck that bails to fallback if SEQUENCE nesting > 16, so the emitter's fixed `seq_stack[16]` is never overrun.
4. fallback.prg's new sentinel (Task 5) — Harbour's timestamp literal syntax may differ in this fork. The plan flags this and tells the implementer to verify by inspecting `src/compiler/harbour.y` if `{^ ... }` doesn't parse cleanly.
5. **Safety hatch (from spec):** if any corpus diff fails after Task 4, revert just the 7 `fSupported` flips from Task 2 — SEQUENCE-using functions immediately fall back, behavior is identical to before group I. The shims + emitter cases stay (harmless).
