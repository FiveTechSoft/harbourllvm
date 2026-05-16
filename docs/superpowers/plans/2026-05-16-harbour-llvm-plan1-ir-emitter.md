# Harbour LLVM Plan 1 — LLVM IR Text Emitter

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `harbour -GL hello.prg` emits `hello.ll` (LLVM IR text) that, compiled with an existing `clang` and linked against the Harbour runtime libraries, runs identically to the standard C build.

**Architecture:** New output language `HB_LANG_LLVM`. The compiler frontend is unchanged. A new `genllvm.c` emits LLVM IR text directly — no libLLVM dependency. Plan 1 mirrors exactly what `genc.c` produces: per-function pcode byte arrays plus a `hb_vmExecute()` call, and a symbol table registered through an `@llvm.global_ctors` constructor. The produced executable still interprets pcode (interpreter removal is Plan 3); Plan 1 only proves the IR path is correct.

**Tech Stack:** C (Harbour compiler sources), LLVM IR text format, existing `clang` for validation only.

**Repository:** Official `harbour/core` (cloned from https://github.com/harbour/core) at `c:\HarbourLLVM\core`. Line numbers cited below are approximate — verify against the actual files, which may have shifted upstream.

**Scope note:** This is the first of three plans for Spec 1. Plan 2 embeds libLLVM + lld so no external compiler is needed. Plan 3 unrolls pcode into straight-line IR and removes the interpreter loop. Plan 1 targets **x86_64 only** (64-bit `HB_SYMB` layout hardcoded); other widths are a later plan.

---

### Task 1: Add the `HB_LANG_LLVM` output language

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbcompdf.h:58-63`

- [ ] **Step 1: Add the enum value**

In `include/hbcompdf.h`, change the `HB_LANGUAGES` enum from:

```c
typedef enum
{
   HB_LANG_C,                      /* C language (by default) <file.c> */
   HB_LANG_PORT_OBJ,               /* Portable objects <file.hrb> */
   HB_LANG_PORT_OBJ_BUF            /* Portable objects in memory buffer */
} HB_LANGUAGES;                    /* supported Harbour output languages */
```

to:

```c
typedef enum
{
   HB_LANG_C,                      /* C language (by default) <file.c> */
   HB_LANG_PORT_OBJ,               /* Portable objects <file.hrb> */
   HB_LANG_PORT_OBJ_BUF,           /* Portable objects in memory buffer */
   HB_LANG_LLVM                    /* LLVM IR text <file.ll> */
} HB_LANGUAGES;                    /* supported Harbour output languages */
```

- [ ] **Step 2: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbcompdf.h
git commit -m "compiler: add HB_LANG_LLVM output language enum"
```

---

### Task 2: Parse the `-GL` command-line flag

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\cmdcheck.c` (the `case 'G':` block, near line 362-394)

- [ ] **Step 1: Add the `L` sub-case**

Inside `cmdcheck.c`, locate the `case 'G':` switch on `HB_TOUPPER( szSwPtr[ 1 ] )`. After the existing `case 'H':` block (which sets `HB_LANG_PORT_OBJ` and does `szSwPtr += 2;`), add:

```c
case 'L':
   HB_COMP_PARAM->iLanguage = HB_LANG_LLVM;
   szSwPtr += 2;
   break;
```

- [ ] **Step 2: Build the compiler and verify the flag is accepted**

Build per the project's normal procedure (see Task 6, Step 1). Then run:

```bash
cd c:/HarbourLLVM/core
bin/<plat>/harbour -GL -q tests/llvm/hello.prg
```

Expected after Task 4: no "unknown switch" error. Before Task 3/4 it may produce no `.ll` file yet — that is fine; this step only confirms the switch parses without error.

- [ ] **Step 3: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/cmdcheck.c
git commit -m "compiler: parse -GL switch to select LLVM IR output"
```

---

### Task 3: Dispatch `HB_LANG_LLVM` to the LLVM generator

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hbmain.c` — `hb_compGenOutput()` (near line 3861-3881)

- [ ] **Step 1: Add the dispatch case**

In `hb_compGenOutput()`, the `switch( iLanguage )` currently has cases `HB_LANG_C`, `HB_LANG_PORT_OBJ`, `HB_LANG_PORT_OBJ_BUF`. Add:

```c
      case HB_LANG_LLVM:
         hb_compGenLLVMCode( HB_COMP_PARAM, HB_COMP_PARAM->pFileName );
         break;
```

- [ ] **Step 2: Declare the generator**

Find where `hb_compGenCCode` is declared as a prototype (search `hbcomp.h` for `hb_compGenCCode`). Add next to it:

```c
extern void hb_compGenLLVMCode( HB_COMP_DECL, PHB_FNAME pFileName );
```

- [ ] **Step 3: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hbmain.c include/hbcomp.h
git commit -m "compiler: dispatch HB_LANG_LLVM to hb_compGenLLVMCode"
```

---

### Task 4: Create `genllvm.c` — a standalone LLVM IR text emitter

**Files:**
- Create: `c:\HarbourLLVM\core\src\compiler\genllvm.c` (new file — the official repo has no LLVM backend)

The emitter has **no libLLVM dependency** — it only `fprintf`s text. It mirrors `genc.c`: study `hb_compGenCCode` (genc.c:162-388), `hb_compGenCCompact` (genc.c:2741-2771), and the symbol-table loop (genc.c:279-342) — the new emitter reproduces the same data as IR.

The produced `.ll` has this shape (for `function Main()`):

```llvm
; Harbour LLVM IR — generated from hello.prg
%HB_SYMB = type { i8*, i64, i8*, i8* }

declare void @hb_vmExecute(i8*, %HB_SYMB*)
declare %HB_SYMB* @hb_vmProcessSymbols(%HB_SYMB*, i16, i8*, i32, i16)

@symbols = internal global %HB_SYMB* null
@.modname = private constant [10 x i8] c"hello.prg\00"
@.sym.0   = private constant [5 x i8] c"MAIN\00"
@.sym.1   = private constant [5 x i8] c"QOUT\00"
@.pcode.MAIN = internal constant [N x i8] c"\24\03\00..."

@symbols_table = internal global [2 x %HB_SYMB] [
  %HB_SYMB { i8* getelementptr([5 x i8], [5 x i8]* @.sym.0, i32 0, i32 0),
             i64 517,
             i8* bitcast(void()* @HB_FUN_MAIN to i8*),
             i8* null },
  %HB_SYMB { i8* getelementptr([5 x i8], [5 x i8]* @.sym.1, i32 0, i32 0),
             i64 1,
             i8* null,
             i8* null }
]

define void @HB_FUN_MAIN() {
  %s = load %HB_SYMB*, %HB_SYMB** @symbols
  call void @hb_vmExecute(
    i8* getelementptr([N x i8], [N x i8]* @.pcode.MAIN, i32 0, i32 0),
    %HB_SYMB* %s)
  ret void
}

define internal void @hb_vm_SymbolInit() {
  %r = call %HB_SYMB* @hb_vmProcessSymbols(
    %HB_SYMB* getelementptr([2 x %HB_SYMB], [2 x %HB_SYMB]* @symbols_table, i32 0, i32 0),
    i16 2,
    i8* getelementptr([10 x i8], [10 x i8]* @.modname, i32 0, i32 0),
    i32 0, i16 3)
  store %HB_SYMB* %r, %HB_SYMB** @symbols
  ret void
}

@llvm.global_ctors = appending global [1 x { i32, void()*, i8* }]
  [ { i32, void()*, i8* } { i32 65535, void()* @hb_vm_SymbolInit, i8* null } ]
```

Notes baked into the design:
- `%HB_SYMB` = 4 pointer-sized slots; the `scope` union is modelled as `i64` (x86_64 only — see scope note). Scope value = bitwise-OR of `HB_FS_*` flags, e.g. `HB_FS_PUBLIC|HB_FS_FIRST|HB_FS_LOCAL` = `0x1|0x4|0x200` = `517`.
- External symbols (not defined in this module, e.g. `QOUT`) get `i8* null` for the function pointer; the VM resolves them dynamically.
- Function symbol name = `HB_FUN_` + the PRG name (`HB_FUNCNAME` macro). Non-identifier characters are encoded `x` + 2 hex digits, exactly as `hb_compGenCFunc` does (genc.c:457-491) — copy that encoding routine.
- pcode bytes come from `pFunc->pCode[0 .. pFunc->nPCodePos-1]`; emit as an escaped IR string constant (`\HH` per byte). Array length `N` = `pFunc->nPCodePos`.
- No `target triple` / `target datalayout` line is emitted — `clang` fills the host default. (Plan 2 will set these explicitly.)
- No `main()` is emitted; the runtime libraries supply it, exactly as with the C backend.

- [ ] **Step 1: Write the failing test program**

Create `c:\HarbourLLVM\core\tests\llvm\hello.prg`:

```harbour
function Main()
   ? "Hello world"
   return nil
```

- [ ] **Step 2: Create `genllvm.c`**

Create `src/compiler/genllvm.c` as a standalone emitter (no libLLVM, no `hbllvm.h`). Structure:

```c
/*
 * LLVM IR text generation for the Harbour compiler.
 */
#define _HB_API_INTERNAL_
#include "hbcomp.h"

/* Emit one byte of pcode as an IR string escape. */
static void hb_llvmEmitByte( FILE * yyc, HB_BYTE b )
{
   fprintf( yyc, "\\%02X", ( unsigned ) b );
}

/* Emit HB_FUN_<name>, encoding non-identifier chars as x<hex>.
   Mirror hb_compGenCFunc() in genc.c:457-491. */
static void hb_llvmEmitFuncName( FILE * yyc, const char * szName )
{
   const char * p;
   fprintf( yyc, "HB_FUN_" );
   for( p = szName; *p; ++p )
   {
      if( ( *p >= 'A' && *p <= 'Z' ) || ( *p >= 'a' && *p <= 'z' ) ||
          ( *p >= '0' && *p <= '9' ) || *p == '_' )
         fputc( *p, yyc );
      else
         fprintf( yyc, "x%02X", ( unsigned char ) *p );
   }
}

void hb_compGenLLVMCode( HB_COMP_DECL, PHB_FNAME pFileName )
{
   char      szFileName[ HB_PATH_MAX ];
   PHB_HFUNC pFunc;
   FILE *    yyc;

   if( ! pFileName->szExtension )
      pFileName->szExtension = ".ll";
   hb_fsFNameMerge( szFileName, pFileName );

   yyc = hb_fopen( szFileName, "w" );
   if( ! yyc )
   {
      hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                       HB_COMP_ERR_CREATE_OUTPUT, szFileName, NULL );
      return;
   }

   /* --- module header --- */
   fprintf( yyc, "; Harbour LLVM IR - generated from %s\n",
            HB_COMP_PARAM->szFile );
   fprintf( yyc, "%%HB_SYMB = type { i8*, i64, i8*, i8* }\n\n" );
   fprintf( yyc, "declare void @hb_vmExecute(i8*, %%HB_SYMB*)\n" );
   fprintf( yyc, "declare %%HB_SYMB* @hb_vmProcessSymbols("
                 "%%HB_SYMB*, i16, i8*, i32, i16)\n\n" );
   fprintf( yyc, "@symbols = internal global %%HB_SYMB* null\n\n" );

   /* --- pcode globals, one per function --- */
   pFunc = HB_COMP_PARAM->functions.pFirst;
   while( pFunc )
   {
      HB_SIZE n;
      fprintf( yyc, "@.pcode." );
      hb_llvmEmitFuncName( yyc, pFunc->szName );
      fprintf( yyc, " = internal constant [%lu x i8] c\"",
               ( unsigned long ) pFunc->nPCodePos );
      for( n = 0; n < pFunc->nPCodePos; ++n )
         hb_llvmEmitByte( yyc, pFunc->pCode[ n ] );
      fprintf( yyc, "\"\n" );
      pFunc = pFunc->pNext;
   }
   fprintf( yyc, "\n" );

   /* --- symbol-name string constants + @symbols_table ---
      Iterate HB_COMP_PARAM->symbols exactly as genc.c:279-342 does.
      For each symbol emit a private [len x i8] name constant, then a
      %HB_SYMB element: { name-ptr, i64 <scope flags>, fn-ptr-or-null,
      i8* null }. Scope flags = the same HB_FS_* OR-ing genc.c prints. */

   /* --- module-name constant --- */

   /* --- function definitions --- */
   pFunc = HB_COMP_PARAM->functions.pFirst;
   while( pFunc )
   {
      fprintf( yyc, "define void @" );
      hb_llvmEmitFuncName( yyc, pFunc->szName );
      fprintf( yyc, "() {\n" );
      fprintf( yyc, "  %%s = load %%HB_SYMB*, %%HB_SYMB** @symbols\n" );
      fprintf( yyc, "  call void @hb_vmExecute(i8* getelementptr("
                    "[%lu x i8], [%lu x i8]* @.pcode.",
               ( unsigned long ) pFunc->nPCodePos,
               ( unsigned long ) pFunc->nPCodePos );
      hb_llvmEmitFuncName( yyc, pFunc->szName );
      fprintf( yyc, ", i32 0, i32 0), %%HB_SYMB* %%s)\n" );
      fprintf( yyc, "  ret void\n}\n\n" );
      pFunc = pFunc->pNext;
   }

   /* --- @hb_vm_SymbolInit constructor + @llvm.global_ctors --- */

   fclose( yyc );

   if( ! HB_COMP_PARAM->fQuiet )
      hb_compOutStd( HB_COMP_PARAM, "LLVM IR output done\n" );
}
```

The commented-out sections (symbol table, module-name constant, constructor) are completed by copying the corresponding `genc.c` loops and emitting the IR from the template shown above. The engineer implementing this MUST read `genc.c:279-342` for the exact `PHB_HSYMBOL` fields and scope-flag printing, and reproduce them.

- [ ] **Step 3: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c tests/llvm/hello.prg
git commit -m "compiler: add genllvm.c standalone LLVM IR text emitter"
```

---

### Task 5: Wire `genllvm.c` into the compiler build

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\Makefile` (the `C_SOURCES :=` list near line 15-22)

- [ ] **Step 1: Add `genllvm.c` to the source list**

In `src/compiler/Makefile`, the `C_SOURCES` list contains `genc.c` and `gencc.c`. Add `genllvm.c` immediately after `gencc.c`:

```make
   genc.c \
   gencc.c \
   genllvm.c \
   genhrb.c \
```

- [ ] **Step 2: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/Makefile
git commit -m "build: compile genllvm.c into the Harbour compiler library"
```

---

### Task 6: Build, generate IR, and validate against the C backend

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\run.sh` (validation script)

- [ ] **Step 1: Rebuild the Harbour compiler**

Run the project's standard build (the one that produces `bin/<plat>/harbour`). On Windows this is `win-make.exe` driven by `make.bat` / the existing `go*.bat`; confirm with the repo's `INSTALL` docs. Expected: `bin/<plat>/harbour` rebuilds with no errors and `genllvm.c` compiles.

- [ ] **Step 2: Generate both outputs**

```bash
cd c:/HarbourLLVM/core
bin/<plat>/harbour -GC -q -o tests/llvm/hello_c   tests/llvm/hello.prg
bin/<plat>/harbour -GL -q -o tests/llvm/hello_ll  tests/llvm/hello.prg
```

Expected: `tests/llvm/hello_c.c` and `tests/llvm/hello_ll.ll` are both created.

- [ ] **Step 3: Verify the `.ll` parses**

```bash
clang -S -emit-llvm -x ir tests/llvm/hello_ll.ll -o NUL
```

Expected: exit code 0, no parse errors. If it fails, the IR text is malformed — fix `genllvm.c` and repeat from Step 1.

- [ ] **Step 4: Build executables from both backends**

Link against the prebuilt Harbour runtime libraries (`hbvm`, `hbrtl`, `hblang`, `hbcpage`, `hbrdd`, `hbmacro`, `hbcommon`, a `gt*` driver) found under `lib/<plat>/`:

```bash
cd c:/harbour/tests/llvm
clang hello_c.c  -I../../include <runtime-libs> -o hello_c.exe
clang hello_ll.ll               <runtime-libs> -o hello_ll.exe
```

Expected: both link with no unresolved symbols.

- [ ] **Step 5: Compare runtime behaviour**

```bash
cd c:/harbour/tests/llvm
./hello_c.exe  > out_c.txt
./hello_ll.exe > out_ll.txt
diff out_c.txt out_ll.txt
```

Expected: `diff` reports no differences — both print `Hello world`. This is the success criterion for Plan 1.

- [ ] **Step 6: Save the validation script and commit**

Write `tests/llvm/run.sh` containing Steps 2-5 so the comparison is repeatable, then:

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/run.sh
git commit -m "test: add LLVM IR backend validation script for Plan 1"
```

---

## Self-Review

**Spec coverage:** Plan 1 covers Spec 1's "codegen core" foundation — emitting an LLVM module per `.prg` and an end-to-end path to a runnable executable — minus interpreter removal (Plan 3) and libLLVM/lld embedding (Plan 2). The "no external C compiler" success criterion is **not** met by Plan 1; it is met by Plan 2. This is stated in the scope note.

**Placeholder scan:** Task 4's `genllvm.c` leaves the symbol-table / constructor emission as guided sections rather than literal code, because the `PHB_HSYMBOL` struct layout is not in this plan's context. They are anchored to `genc.c:279-342` as the pattern to copy and to the concrete IR template shown — an engineer can complete them without further design decisions.

**Type consistency:** `hb_compGenLLVMCode` signature matches the declaration added in Task 3. `%HB_SYMB` layout, `@symbols`, `@symbols_table`, and `hb_vmProcessSymbols`/`hb_vmExecute` declarations are identical across the Task 4 template and emitter code.

**Known limitation:** x86_64-only (`i64` scope slot). Recorded in the scope note; 32-bit handling deferred.
