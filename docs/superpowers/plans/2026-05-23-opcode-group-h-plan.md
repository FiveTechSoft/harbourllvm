# Opcode Group H — Macros — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so a function using any `&var` / `&("expr")` / `text...endtext` / `@&var` form emits straight-line native code instead of falling back, whole-function, to the `hb_vmExecute` interpreter.

**Architecture:** Fourteen compiler-emitted macro opcodes (38–47, 123, 124, 127, 146) become 14 thin `hb_vmsh_macro*` shims in `src/vm/hvm.c`, each forwarding to the existing `hb_macro*` (exported from `libhbmacro.a`) or `hb_vmMacro*` (`static` in `hvm.c`) helper with identical arguments and returning `hb_stackGetActionRequest()`. Decoder rows flip `fSupported` to `HB_TRUE` (no length-computation change — all already `HB_PCK_FIXED` with correct `nLen`). Emitter adds 14 `declare` lines and 14 `case` arms — most follow the existing `HB_EMIT_NOARG_SHIM` / single-int-arg patterns. Corpus exercises every major macro form.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), Harbour macro compiler (`libhbmacro.a`), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-23-opcode-group-h-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode groups A–G.

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
- **`hvm.c` is compiled via a unity file.** `libhbvm.a` is built from `src/vm/hvmall.c`, which `#include`s `hvm.c`. `make` tracks only `hvmall.c`. After editing `hvm.c` you MUST `touch src/vm/hvmall.c` before `./win-make.exe`, or the new shims will not land in `libhbvm.a`. Verify with `nm lib/win/mingw64/libhbvm.a | grep hb_vmsh_macro` after the build.
- The `harbour.exe` Makefile tracks only `.o` deps — if a `genllvm.c` change did not relink `harbour.exe` (`win-make` says `harbour.exe is up to date`), run a standalone `touch src/main/harbour.c` then `./win-make.exe`.
- `hbmk2`: `bin/win/mingw64/hbmk2.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

**Shell note:** do not combine `cd` with output redirection (`>`) in one command. Run a standalone `cd` first (working dir persists), or redirect with an absolute path.

---

## Opcode → helper map (reference)

The implementer MUST read each `case HB_P_MACRO*:` arm of `hb_vmExecute` in `src/vm/hvm.c` (around lines 2616–2706) and confirm every shim mirrors its interpreter case byte-for-byte. Summary:

| # | Opcode | Bytes | Interpreter call (shim must reproduce) |
|---|--------|-------|----------------------------------------|
| 38 | `HB_P_MACROPOP` | 2 | `hb_macroSetValue( hb_stackItemFromTop(-1), pCode[1] )` |
| 39 | `HB_P_MACROPOPALIASED` | 2 | `hb_macroPopAliasedValue( hb_stackItemFromTop(-2), hb_stackItemFromTop(-1), pCode[1] )` |
| 40 | `HB_P_MACROPUSH` | 2 | `hb_macroGetValue( hb_stackItemFromTop(-1), 0, pCode[1] )` |
| 41 | `HB_P_MACROARRAYGEN` | 3 | `hb_vmMacroArrayGen( HB_PCODE_MKUSHORT(&pCode[1]) )` |
| 42 | `HB_P_MACROPUSHLIST` | 2 | `hb_macroGetValue( hb_stackItemFromTop(-1), HB_P_MACROPUSHLIST, pCode[1] )` |
| 43 | `HB_P_MACROPUSHINDEX` | 1 | `hb_vmMacroPushIndex()` |
| 44 | `HB_P_MACROPUSHPARE` | 2 | `hb_macroGetValue( hb_stackItemFromTop(-1), HB_P_MACROPUSHPARE, pCode[1] )` |
| 45 | `HB_P_MACROPUSHALIASED` | 2 | `hb_macroPushAliasedValue( hb_stackItemFromTop(-2), hb_stackItemFromTop(-1), pCode[1] )` |
| 46 | `HB_P_MACROSYMBOL` | 1 | `hb_macroPushSymbol( hb_stackItemFromTop(-1) )` |
| 47 | `HB_P_MACROTEXT` | 1 | `hb_macroTextValue( hb_stackItemFromTop(-1) )` |
| 123 | `HB_P_MACROFUNC` | 3 | `hb_vmMacroFunc( HB_PCODE_MKUSHORT(&pCode[1]) )` |
| 124 | `HB_P_MACRODO` | 3 | `hb_vmMacroDo( HB_PCODE_MKUSHORT(&pCode[1]) )` |
| 127 | `HB_P_MACROPUSHREF` | 1 | `hb_macroPushReference( hb_stackItemFromTop(-1) )` |
| 146 | `HB_P_MACROSEND` | 3 | `hb_vmMacroSend( HB_PCODE_MKUSHORT(&pCode[1]) )` |

`hb_macro*` are declared in `include/hbapi.h` (lines 1135–1146), exported from `libhbmacro.a`. `hb_vmMacro*` are `static` in `hvm.c` (around lines 129–133 declarations, definitions later) — only callable from a TU that sees them, i.e. the shim block at the end of `hvm.c`.

---

### Task 1: The fourteen `hb_vmsh_macro*` shims

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 14 declarations)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 14 shims after the group G shim)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_h.c`

- [ ] **Step 1: Add the declarations to `hbvmsh.h`**

In `include/hbvmsh.h`, inside the `HB_EXTERN_BEGIN … HB_EXTERN_END` block, after the group G declaration (`hb_vmsh_pushblock`), add:

```c
/* --- group H: macros --- */
/* Each macro shim calls the same hb_macro* / hb_vmMacro* helper the
 * interpreter calls, returning the current action request — macro code IS
 * user code (the runtime macro compiler can raise errors). */

/* no-operand (1-byte instruction) */
extern HB_EXPORT int hb_vmsh_macropushindex( void );
extern HB_EXPORT int hb_vmsh_macropushref  ( void );
extern HB_EXPORT int hb_vmsh_macrosymbol   ( void );
extern HB_EXPORT int hb_vmsh_macrotext     ( void );

/* 1-byte flag operand */
extern HB_EXPORT int hb_vmsh_macropop         ( int flag );
extern HB_EXPORT int hb_vmsh_macropopaliased  ( int flag );
extern HB_EXPORT int hb_vmsh_macropush        ( int flag );
extern HB_EXPORT int hb_vmsh_macropushlist    ( int flag );
extern HB_EXPORT int hb_vmsh_macropushpare    ( int flag );
extern HB_EXPORT int hb_vmsh_macropushaliased ( int flag );

/* 2-byte MKUSHORT operand */
extern HB_EXPORT int hb_vmsh_macroarraygen ( int usFlags );
extern HB_EXPORT int hb_vmsh_macrodo       ( int usParams );
extern HB_EXPORT int hb_vmsh_macrofunc     ( int usParams );
extern HB_EXPORT int hb_vmsh_macrosend     ( int usParams );
```

- [ ] **Step 2: Append the 14 shim implementations to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group G shim (`hb_vmsh_pushblock`), add:

```c
/* --- group H: macros --- */

/* no-operand shims */

HB_EXPORT int hb_vmsh_macropushindex( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmMacroPushIndex();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macropushref( void )
{
   HB_STACK_TLS_PRELOAD
   hb_macroPushReference( hb_stackItemFromTop( -1 ) );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macrosymbol( void )
{
   HB_STACK_TLS_PRELOAD
   hb_macroPushSymbol( hb_stackItemFromTop( -1 ) );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macrotext( void )
{
   HB_STACK_TLS_PRELOAD
   hb_macroTextValue( hb_stackItemFromTop( -1 ) );
   return ( int ) hb_stackGetActionRequest();
}

/* 1-byte flag shims */

HB_EXPORT int hb_vmsh_macropop( int flag )
{
   HB_STACK_TLS_PRELOAD
   hb_macroSetValue( hb_stackItemFromTop( -1 ), flag );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macropopaliased( int flag )
{
   HB_STACK_TLS_PRELOAD
   hb_macroPopAliasedValue( hb_stackItemFromTop( -2 ),
                            hb_stackItemFromTop( -1 ), flag );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macropush( int flag )
{
   HB_STACK_TLS_PRELOAD
   hb_macroGetValue( hb_stackItemFromTop( -1 ), 0, flag );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macropushlist( int flag )
{
   HB_STACK_TLS_PRELOAD
   hb_macroGetValue( hb_stackItemFromTop( -1 ), HB_P_MACROPUSHLIST, flag );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macropushpare( int flag )
{
   HB_STACK_TLS_PRELOAD
   hb_macroGetValue( hb_stackItemFromTop( -1 ), HB_P_MACROPUSHPARE, flag );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macropushaliased( int flag )
{
   HB_STACK_TLS_PRELOAD
   hb_macroPushAliasedValue( hb_stackItemFromTop( -2 ),
                             hb_stackItemFromTop( -1 ), flag );
   return ( int ) hb_stackGetActionRequest();
}

/* 2-byte MKUSHORT shims */

HB_EXPORT int hb_vmsh_macroarraygen( int usFlags )
{
   HB_STACK_TLS_PRELOAD
   hb_vmMacroArrayGen( ( HB_USHORT ) usFlags );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macrodo( int usParams )
{
   HB_STACK_TLS_PRELOAD
   hb_vmMacroDo( ( HB_USHORT ) usParams );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macrofunc( int usParams )
{
   HB_STACK_TLS_PRELOAD
   hb_vmMacroFunc( ( HB_USHORT ) usParams );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_macrosend( int usParams )
{
   HB_STACK_TLS_PRELOAD
   hb_vmMacroSend( ( HB_USHORT ) usParams );
   return ( int ) hb_stackGetActionRequest();
}
```

Before writing, read each `case HB_P_MACRO*:` arm in `hb_vmExecute` (`src/vm/hvm.c`, lines around 2616–2706) and confirm each shim's helper call matches its interpreter case exactly — same helper, same arguments, same order. Note `hb_macroGetValue` takes three args; `HB_P_MACROPUSH` uses `0` as the second (`iContext`), while `HB_P_MACROPUSHLIST` and `HB_P_MACROPUSHPARE` use their own opcode value as `iContext`. The `hb_vmMacro*` helpers are `static` and only callable from `hvm.c` (the shim block lives there). All 14 shims need `HB_STACK_TLS_PRELOAD` because each either calls `hb_stackItemFromTop` directly or invokes `hb_stackGetActionRequest`.

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_h.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[] = {
      ( void * ) hb_vmsh_macropushindex,
      ( void * ) hb_vmsh_macropushref,
      ( void * ) hb_vmsh_macrosymbol,
      ( void * ) hb_vmsh_macrotext,
      ( void * ) hb_vmsh_macropop,
      ( void * ) hb_vmsh_macropopaliased,
      ( void * ) hb_vmsh_macropush,
      ( void * ) hb_vmsh_macropushlist,
      ( void * ) hb_vmsh_macropushpare,
      ( void * ) hb_vmsh_macropushaliased,
      ( void * ) hb_vmsh_macroarraygen,
      ( void * ) hb_vmsh_macrodo,
      ( void * ) hb_vmsh_macrofunc,
      ( void * ) hb_vmsh_macrosend
   };
   int i;
   for( i = 0; i < ( int )( sizeof( p ) / sizeof( p[ 0 ] ) ); ++i )
      printf( "group H shim %2d linkable: %p\n", i, p[ i ] );
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

Then verify (separate command):

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
nm lib/win/mingw64/libhbvm.a | grep "T hb_vmsh_macro" | sort
gcc tests/llvm/shim_smoke_h.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -Wl,--noinhibit-exec -o build/shim_smoke_h.exe
./build/shim_smoke_h.exe
```

Expected: `hvm.c` compiles clean; `nm | grep` lists 14 `T hb_vmsh_macro*` symbols (one per shim); `shim_smoke_h.exe` prints 14 lines each with a non-null pointer. `--noinhibit-exec` is required because the partially-built tree has unresolved RDD symbols; the smoke test never calls the shims, only takes their addresses.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_h.c
git commit -m "vm: add group H op shims hb_vmsh_macro* (macros)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Decoder — mark all 14 macro opcodes supported

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`
- Modify: `c:\HarbourLLVM\core\tests\llvm\pcdectest.c`

The 14 rows are already `HB_PCK_FIXED` with the correct `nLen`. Task 2 only flips `fSupported` and updates the header comment. NO length-computation change.

- [ ] **Step 1: Flip the fourteen table rows**

In `src/compiler/hb_pcdec.c`'s `hb_pcInfo[]`, change the `fSupported` field of these rows from `HB_FALSE` to `HB_TRUE` (read each row first and change ONLY the `HB_FALSE` token in each; keep `kind`, `nLen`, and the trailing comment untouched):

| Row | Opcode |
|-----|--------|
| 38 | `HB_P_MACROPOP` |
| 39 | `HB_P_MACROPOPALIASED` |
| 40 | `HB_P_MACROPUSH` |
| 41 | `HB_P_MACROARRAYGEN` |
| 42 | `HB_P_MACROPUSHLIST` |
| 43 | `HB_P_MACROPUSHINDEX` |
| 44 | `HB_P_MACROPUSHPARE` |
| 45 | `HB_P_MACROPUSHALIASED` |
| 46 | `HB_P_MACROSYMBOL` |
| 47 | `HB_P_MACROTEXT` |
| 123 | `HB_P_MACROFUNC` |
| 124 | `HB_P_MACRODO` |
| 127 | `HB_P_MACROPUSHREF` |
| 146 | `HB_P_MACROSEND` |

Do NOT change `HB_P_MPUSHBLOCK` (59), `HB_P_MPUSHSTR` (125), `HB_P_MPUSHBLOCKLARGE` (159), `HB_P_MPUSHSTRLARGE` (160) — they stay `HB_FALSE` (runtime-macro-compiler-only, never reach the LLVM backend).

- [ ] **Step 2: Update the file-header "in scope" comment**

After the `Group G additions (codeblocks):` paragraph, add:

```c
 *
 * Group H additions (macros):
 *   HB_P_MACROPOP, HB_P_MACROPOPALIASED, HB_P_MACROPUSH,
 *   HB_P_MACROARRAYGEN, HB_P_MACROPUSHLIST, HB_P_MACROPUSHINDEX,
 *   HB_P_MACROPUSHPARE, HB_P_MACROPUSHALIASED, HB_P_MACROSYMBOL,
 *   HB_P_MACROTEXT, HB_P_MACROFUNC, HB_P_MACRODO, HB_P_MACROPUSHREF,
 *   HB_P_MACROSEND
```

(Match the exact comment style of the surrounding `Group X additions` paragraphs.)

- [ ] **Step 3: Add a decoder test**

In `tests/llvm/pcdectest.c`, after the Group G test block (the one that prints `pcdec: HB_P_PUSHBLOCK* supported`) and before `return 0;`, add:

```c
   {
      /* Group H: the 14 compiler-emitted macro opcodes are now in the
       * straight-line subset. MPUSHBLOCK / MPUSHSTR family stays HB_FALSE. */
      assert( hb_pcInfo[ HB_P_MACROPOP         ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPOPALIASED  ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSH        ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROARRAYGEN    ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHLIST    ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHINDEX   ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHPARE    ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHALIASED ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROSYMBOL      ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROTEXT        ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROFUNC        ].fSupported );
      assert( hb_pcInfo[ HB_P_MACRODO          ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHREF     ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROSEND        ].fSupported );
      /* MPUSH* family must stay unsupported. */
      assert( ! hb_pcInfo[ HB_P_MPUSHBLOCK      ].fSupported );
      assert( ! hb_pcInfo[ HB_P_MPUSHSTR        ].fSupported );
      assert( ! hb_pcInfo[ HB_P_MPUSHBLOCKLARGE ].fSupported );
      assert( ! hb_pcInfo[ HB_P_MPUSHSTRLARGE   ].fSupported );
      printf( "pcdec: 14 macro opcodes supported, MPUSH* family still unsupported\n" );
   }
```

Also update the `Expected:` block in the file-header comment to add the new line: `pcdec: 14 macro opcodes supported, MPUSH* family still unsupported`.

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

Expected: prints all prior lines plus `pcdec: 14 macro opcodes supported, MPUSH* family still unsupported`.

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c tests/llvm/pcdectest.c
git commit -m "compiler: mark 14 macro opcodes supported in the decoder (group H)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Emitter cases for the 14 macro opcodes

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Add the 14 `declare` lines**

In `hb_compGenLLVMCode`'s module-header emission, immediately after the group G declaration line `fprintf( yyc, "declare i32 @hb_vmsh_pushblock(i8*, %%HB_SYMB*)\n" );`, add:

```c
   /* Group H: macro shim declarations */
   fprintf( yyc, "declare i32 @hb_vmsh_macropushindex()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macropushref()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macrosymbol()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macrotext()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macropop(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macropopaliased(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macropush(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macropushlist(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macropushpare(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macropushaliased(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macroarraygen(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macrodo(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macrofunc(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_macrosend(i32)\n" );
```

- [ ] **Step 2: Add the 14 emitter cases to `hb_llvmSLEmitBody`**

In `hb_llvmSLEmitBody`'s opcode `switch` (the one with `case HB_P_PUSHBLOCK:`), immediately before the `default:` arm, add the 14 macro cases. The 4 no-operand cases reuse the existing `HB_EMIT_NOARG_SHIM` macro:

```c
         /* Group H: macros — no-operand */
         case HB_P_MACROPUSHINDEX: HB_EMIT_NOARG_SHIM( "macropushindex" ); break;
         case HB_P_MACROPUSHREF:   HB_EMIT_NOARG_SHIM( "macropushref"   ); break;
         case HB_P_MACROSYMBOL:    HB_EMIT_NOARG_SHIM( "macrosymbol"    ); break;
         case HB_P_MACROTEXT:      HB_EMIT_NOARG_SHIM( "macrotext"      ); break;
```

The 10 int-operand cases follow the same `fprintf` shape as the existing `case HB_P_ARRAYDIM:` (`genllvm.c` lines 936–947) — one helper macro added at the top of `hb_llvmSLEmitBody`'s `switch` block, just below the existing `HB_EMIT_NOARG_SHIM` definition, then 10 one-line `case` lines using it. The macro:

```c
#define HB_EMIT_INT1_SHIM( nm, val ) \
            fprintf( yyc, \
                     "  %%r%lu = call i32 @hb_vmsh_" nm "(i32 %u)\n" \
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n" \
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n", \
                     ( unsigned long ) pos, ( unsigned ) ( val ), \
                     ( unsigned long ) pos, ( unsigned long ) pos, \
                     ( unsigned long ) pos, szNextLabel )
```

Then the 10 cases (place after the no-operand four, still before `default:`):

```c
         /* Group H: macros — 1-byte flag operand */
         case HB_P_MACROPOP:
            HB_EMIT_INT1_SHIM( "macropop",         pCode[ pos + 1 ] ); break;
         case HB_P_MACROPOPALIASED:
            HB_EMIT_INT1_SHIM( "macropopaliased",  pCode[ pos + 1 ] ); break;
         case HB_P_MACROPUSH:
            HB_EMIT_INT1_SHIM( "macropush",        pCode[ pos + 1 ] ); break;
         case HB_P_MACROPUSHLIST:
            HB_EMIT_INT1_SHIM( "macropushlist",    pCode[ pos + 1 ] ); break;
         case HB_P_MACROPUSHPARE:
            HB_EMIT_INT1_SHIM( "macropushpare",    pCode[ pos + 1 ] ); break;
         case HB_P_MACROPUSHALIASED:
            HB_EMIT_INT1_SHIM( "macropushaliased", pCode[ pos + 1 ] ); break;

         /* Group H: macros — 2-byte MKUSHORT operand */
         case HB_P_MACROARRAYGEN:
            HB_EMIT_INT1_SHIM( "macroarraygen", HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] ) ); break;
         case HB_P_MACROFUNC:
            HB_EMIT_INT1_SHIM( "macrofunc",     HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] ) ); break;
         case HB_P_MACRODO:
            HB_EMIT_INT1_SHIM( "macrodo",       HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] ) ); break;
         case HB_P_MACROSEND:
            HB_EMIT_INT1_SHIM( "macrosend",     HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] ) ); break;
```

The `HB_EMIT_INT1_SHIM` macro is defined exactly once (next to `HB_EMIT_NOARG_SHIM`); place it right after that definition. The 1-byte cases read `pCode[pos+1]` directly (HB_BYTE, implicitly promoted to int for the macro's `unsigned` argument); the 2-byte cases use `HB_PCODE_MKUSHORT`. All 10 forward the `(unsigned)`-coerced operand to the i32 shim parameter.

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

Expected: `genllvm.c` compiles clean; `bin/win/mingw64/harbour.exe` is relinked (a `gcc … -o …/harbour.exe …` line in the output) before the known unrelated DLL abort.

- [ ] **Step 4: Check the IR for a tiny macro program**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
printf 'function Main()\n   local cVar := "n"\n   local n := 42\n   ? &cVar\n   return nil\n' > build/mctmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3h build/mctmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3h.ll -o NUL
grep -c "call i32 @hb_vmsh_macropush" build/t3h.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3h.ll | grep -c "call void @hb_vmExecute"
```

Expected: clean rebuild; IR passes the verifier (only the benign `-Woverride-module` warning); `hb_vmsh_macropush` call count ≥ 1; `HB_FUN_MAIN` fallback count is `0`.

- [ ] **Step 5: Run it end-to-end**

```bash
cd c:/HarbourLLVM/core
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/t3hrun build/mctmp.prg
./build/t3hrun.exe
```

Expected: prints `42` (the macro `&cVar` resolves to local `n`, value `42`).

- [ ] **Step 6: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for 14 macro opcodes (group H)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Corpus — verify macros

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\macro.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh`

- [ ] **Step 1: Write the new corpus program**

Create `tests/llvm/macro.prg` with this exact content. Each macro form lives in its own function so the per-opcode straight-lining is independently exercised:

```harbour
//
// Group H corpus — macro opcodes straight-lined by the LLVM backend.
//
// Exercises the major macro forms: &var read (MACROPUSH), &var := x write
// (MACROPOP), &("expr()") call (MACROFUNC), &("expr") DO (MACRODO),
// text...endtext substitution (MACROTEXT), @&var reference (MACROPUSHREF),
// and aliased &fld (MACROPUSHALIASED).
//
function Main()
   ReadVar()
   WriteVar()
   CallFunc()
   DoFunc()
   TextSub()
   RefMacro()
   AliasMacro()
   return nil

function ReadVar()
   local cName := "n"
   local n     := 7
   ? &cName
   return nil

function WriteVar()
   local cName := "n"
   local n     := 0
   &cName := 99
   ? n
   return nil

function CallFunc()
   local cExpr := "Upper( 'hello' )"
   ? &( cExpr )
   return nil

function DoFunc()
   local cName := "Greet"
   &cName.()
   return nil

function Greet()
   ? "greet called"
   return nil

function TextSub()
   local cWhat := "world"
   local cMsg  := "hello, &cWhat"
   ? cMsg
   return nil

function RefMacro()
   local cName  := "n"
   local n      := 0
   local bSetter := {| x | n := x }
   Eval( bSetter, 11 )
   ? @&cName
   ? n
   return nil

function AliasMacro()
   local cFld := "FOO"
   ? Type( "M->" + cFld )
   return nil
```

(Note: `RefMacro` exercises `@&` reference creation; the printed `@&cName` is a reference object — the output line is implementation-defined but identical between the C backend and the LLVM backend, which is what the diff gate checks. The aliased macro test uses `Type()` over `M->FOO` to exercise a memvar macro path without requiring an open workarea.)

- [ ] **Step 2: Add `macro` to `run.sh`'s straight-line-required set**

In `tests/llvm/run.sh`, find the `case "$name" in` block listing the corpus programs that must NOT fall back. It currently ends `foreach|switchstmt|codeblock)`. Change that line to add `macro`:

Before:
```
               foreach|switchstmt|codeblock)
```
After:
```
               foreach|switchstmt|codeblock|macro)
```

Also update the comment immediately above the `case "$name" in` block to add `H (macro)`. Read the actual surrounding comment first and match its exact wording.

Change ONLY that one pattern line and the comment. Do not touch the rest of `run.sh`.

- [ ] **Step 3: Run the new program end-to-end and diff against the C backend**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
env PATH="/c/Windows/System32:/c/Windows" bin/win/mingw64/harbour.exe -GL -q -obuild/gh_mc tests/llvm/macro.prg
./build/gh_mc.exe > build/gh_mc_ll.out 2>&1
bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/gh_mc_c tests/llvm/macro.prg
./build/gh_mc_c.exe > build/gh_mc_c.out 2>&1
diff build/gh_mc_c.out build/gh_mc_ll.out
```

Expected: `diff` reports no differences — straight-line backend output is byte-identical to the C backend.

- [ ] **Step 4: Confirm the macro functions straight-lined**

```bash
cd c:/HarbourLLVM/core
grep -c "call i32 @hb_vmsh_macro" build/gh_mc.ll
for fn in HB_FUN_READVAR HB_FUN_WRITEVAR HB_FUN_CALLFUNC HB_FUN_DOFUNC HB_FUN_TEXTSUB HB_FUN_REFMACRO HB_FUN_ALIASMACRO; do
   c=$(awk -v fn="$fn" '$0 ~ "^define.*@" fn "\\(\\)"{f=1} f{print} f&&/^\}$/{exit}' build/gh_mc.ll | grep -c "call void @hb_vmExecute")
   echo "$fn fallback count: $c"
done
```

Expected: `hb_vmsh_macro` call count ≥ 7 (at least one per macro-using function); every per-function fallback count is `0`. (If a function legitimately falls back because it uses some opcode still outside the A–H subset, report it — the diff-vs-C is the gate; but `run.sh`'s `macro` hard-fail rule applies to `HB_FUN_MAIN` only, and Main contains no macros, so Main itself will straight-line trivially regardless.)

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gh
```

Expected: ends `RESULT: all programs validated and matched the C backend`; the report shows `macro` with `SL ok`.

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/macro.prg tests/llvm/run.sh
git commit -m "test: corpus for group H — macros straight-lined

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:** The spec's three required changes are covered — Task 1 the 14 `hb_vmsh_macro*` shims (`src/vm/hvm.c` + `hbvmsh.h`), Task 2 the decoder (14 `fSupported` flips; no length change, as the spec notes; MPUSH* family explicitly left HB_FALSE), Task 3 the emitter (14 `declare` lines + 4 no-operand `HB_EMIT_NOARG_SHIM` cases + 10 int-operand cases via a new local `HB_EMIT_INT1_SHIM` macro). The spec's testing section (`macro.prg` with one function per major macro form, diff against C backend) is Task 4.

**Placeholder scan:** All 14 shims are shown in full in Task 1 Step 2. The opcode→helper mapping is given as a complete table in the reference section. The decoder flip is given as a complete table in Task 2 Step 1. The emitter cases are given as complete code (the new `HB_EMIT_INT1_SHIM` macro and all 14 `case` lines) in Task 3 Step 2. The corpus program is shown in full in Task 4 Step 1. No "TBD"/"handle edge cases"/vague steps.

**Type consistency:** Every shim takes `int` (no operand) or `int flag` / `int usFlags` / `int usParams`, returns `int` — identical across `hbvmsh.h` declarations (Task 1 Step 1), shim implementations (Task 1 Step 2), IR `declare i32 @hb_vmsh_macro*(i32)` lines (Task 3 Step 1), and emitter call sites (Task 3 Step 2). The 1-byte cases read `pCode[pos+1]` and the 2-byte cases use `HB_PCODE_MKUSHORT(&pCode[pos+1])` — both fit the `i32` parameter without truncation (HB_BYTE = 0..255, HB_USHORT = 0..65535).

**Known risks:** (1) The runtime macro compiler can raise errors mid-compile (parse error, undefined variable, etc.); every shim returns `hb_stackGetActionRequest()` and the emitter emits the standard `icmp ne / br i1 ... epilogue` check, so unwind behavior is identical to the interpreter. (2) `HB_P_MACROTEXT` (text-substitution) is rarely used in modern Harbour — the corpus exercises it explicitly (`TextSub`) so any per-opcode regression is caught by the diff-vs-C gate. (3) The unity-file build gotcha — after editing `hvm.c` you must `touch src/vm/hvmall.c`, called out in the build-environment section and Task 1 Step 4. (4) The `harbour.exe` relink — Task 3 Step 3 includes the standalone `touch src/main/harbour.c` fallback.
