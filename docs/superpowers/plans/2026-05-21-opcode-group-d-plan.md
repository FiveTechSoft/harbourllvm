# Opcode Group D — OOP messages — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the straight-line LLVM backend so method calls (`oObj:method()`), `Self`, object-variable references, and `WITH OBJECT ... END` blocks emit straight-line native code instead of falling back to the `hb_vmExecute` interpreter.

**Architecture:** Pure replication of the group A/B/C pattern. 9 pcode opcodes (8 distinct shims — `SEND` and `SENDSHORT` share one) each get: a `hb_vmsh_*` shim appended to `src/vm/hvm.c` (mirroring its `hb_vmExecute` case, returning the action request), `fSupported = HB_TRUE` in the `hb_pcInfo[]` decoder table, and an emitter `case` in `genllvm.c`. The 2 symbol-operand opcodes pass a `PHB_SYMB *` from the module `@symbols_table` exactly as the existing `HB_P_PUSHSYM` / group C emitter cases do.

**Tech Stack:** Harbour compiler (C), Harbour VM (C), LLVM IR text.

**Repository:** `c:\HarbourLLVM\core`, default branch `master`.

**Spec:** `docs/superpowers/specs/2026-05-21-opcode-group-d-design.md`.

**Prerequisites (done):** Plans 1-3 and opcode groups A, B, C.

---

## Build environment

Every build/test step uses:

```
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64
export HB_PLATFORM=win
```

Build Harbour from the repo root with `./win-make.exe`. **Known build note:** `win-make` aborts at a pre-existing, unrelated `harbour-32-x64.dll` link failure — predates this work, irrelevant (the LLVM backend links static `.a` libs). Static libraries and `bin/win/mingw64/harbour.exe` build BEFORE that step. The `harbour.exe` Makefile tracks only `.o` deps — if a `genllvm.c` change did not relink `harbour.exe`, run `touch src/main/harbour.c && ./win-make.exe`. `hbmk2`: `bin/win/mingw64/hbmk2.exe`. LLVM verifier: `C:/Program Files/LLVM/bin/amdclang++.exe`.

---

## The 9 opcodes (reference)

Verified against the `hb_vmExecute` switch in `src/vm/hvm.c`.

| Opcode | Len | Interpreter case body (to mirror) | Shim |
|--------|-----|-----------------------------------|------|
| `HB_P_PUSHSELF` | 1 | `hb_vmPush( hb_stackSelfItem() );` | `hb_vmsh_pushself` |
| `HB_P_PUSHOVARREF` | 1 | `hb_vmPushObjectVarRef();` | `hb_vmsh_pushovarref` |
| `HB_P_WITHOBJECTSTART` | 1 | `hb_vmWithObjectStart();` | `hb_vmsh_withobjectstart` |
| `HB_P_WITHOBJECTEND` | 1 | `hb_stackPop(); hb_stackPop();` | `hb_vmsh_withobjectend` |
| `HB_P_FUNCPTR` | 1 | `hb_vmFuncPtr();` | `hb_vmsh_funcptr` |
| `HB_P_MESSAGE` | 3 | `hb_vmPushSymbol( pSym );` | `hb_vmsh_message` |
| `HB_P_WITHOBJECTMESSAGE` | 3 | see below | `hb_vmsh_withobjectmessage` |
| `HB_P_SEND` | 3 | see below (count operand) | `hb_vmsh_send` |
| `HB_P_SENDSHORT` | 2 | see below (count operand) | `hb_vmsh_send` (shared) |

`HB_P_SEND` / `HB_P_SENDSHORT` — the interpreter case is:
```c
hb_itemSetNil( hb_stackReturnItem() );
hb_vmSend( <count> );
pCode += <len>;
if( pCode[ 0 ] == HB_P_POP )   /* the micro-opt — NOT reproduced */
   pCode++;
else
   hb_stackPushReturn();
```
The shim drops the POP-peek micro-opt and **always** pushes the return:
`hb_itemSetNil( hb_stackReturnItem() ); hb_vmSend( uiParams ); hb_stackPushReturn();`. A following `HB_P_POP` is already a supported opcode and pops it — semantically identical.

`HB_P_WITHOBJECTMESSAGE` — the interpreter case (operand `wSymPos = HB_PCODE_MKUSHORT(&pCode[1])`):
```c
if( wSymPos != 0xFFFF )
   hb_vmPushSymbol( pSymbols + wSymPos );
pWith = hb_stackWithObjectItem();
if( pWith )
   hb_vmPush( pWith );
else
   hb_stackAllocItem()->type = HB_IT_NIL;
```
The shim takes a `PHB_SYMB pSym` (NULL for the 0xFFFF case) and reproduces it as `if( pSym != NULL ) hb_vmPushSymbol( pSym ); ...`.

The implementer MUST open `src/vm/hvm.c`, confirm each `case` body and helper name verbatim, and reproduce it. If any case body differs from this table, follow `hvm.c`.

---

### Task 1: The 8 OOP op shims

**Files:**
- Modify: `c:\HarbourLLVM\core\include\hbvmsh.h` (add 8 declarations)
- Modify: `c:\HarbourLLVM\core\src\vm\hvm.c` (append 8 shim implementations to the existing group C shim block)
- Test: `c:\HarbourLLVM\core\tests\llvm\shim_smoke_d.c`

- [ ] **Step 1: Add the declarations to `hbvmsh.h`**

Inside the existing `HB_EXTERN_BEGIN ... HB_EXTERN_END` block in `include/hbvmsh.h`, after the group C declarations, add:

```c
/* --- group D: OOP messages --- */
extern HB_EXPORT int hb_vmsh_pushself( void );
extern HB_EXPORT int hb_vmsh_pushovarref( void );
extern HB_EXPORT int hb_vmsh_withobjectstart( void );
extern HB_EXPORT int hb_vmsh_withobjectend( void );
extern HB_EXPORT int hb_vmsh_funcptr( void );
extern HB_EXPORT int hb_vmsh_message( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_withobjectmessage( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_send( int uiParams );
```

- [ ] **Step 2: Append the 8 shim implementations to `hvm.c`**

At the end of `src/vm/hvm.c`, after the existing group C shim block, add the 8 implementations. Each starts with `HB_STACK_TLS_PRELOAD` as its first statement and ends with `return ( int ) hb_stackGetActionRequest();` — identical shape to the group A/B/C shims:

```c
/* --- group D: OOP messages --- */

HB_EXPORT int hb_vmsh_pushself( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPush( hb_stackSelfItem() );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_pushovarref( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushObjectVarRef();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_withobjectstart( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmWithObjectStart();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_withobjectend( void )
{
   HB_STACK_TLS_PRELOAD
   hb_stackPop();   /* remove with object envelope */
   hb_stackPop();   /* remove implicit object       */
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_funcptr( void )
{
   HB_STACK_TLS_PRELOAD
   hb_vmFuncPtr();
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_message( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   hb_vmPushSymbol( pSym );
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_withobjectmessage( PHB_SYMB pSym )
{
   HB_STACK_TLS_PRELOAD
   PHB_ITEM pWith;
   if( pSym != NULL )
      hb_vmPushSymbol( pSym );
   pWith = hb_stackWithObjectItem();
   if( pWith )
      hb_vmPush( pWith );
   else
      hb_stackAllocItem()->type = HB_IT_NIL;
   return ( int ) hb_stackGetActionRequest();
}

HB_EXPORT int hb_vmsh_send( int uiParams )
{
   HB_STACK_TLS_PRELOAD
   hb_itemSetNil( hb_stackReturnItem() );
   hb_vmSend( ( HB_USHORT ) uiParams );
   hb_stackPushReturn();
   return ( int ) hb_stackGetActionRequest();
}
```

Before writing these, confirm against `hvm.c`: the exact helper names (`hb_vmPushObjectVarRef`, `hb_vmWithObjectStart`, `hb_vmFuncPtr`, `hb_stackWithObjectItem`, `hb_stackSelfItem`, `hb_vmSend`, `hb_itemSetNil`, `hb_stackReturnItem`, `hb_stackPushReturn`, `hb_vmPush`, `hb_vmPushSymbol`) and that each is reachable from the end of `hvm.c` (the `static` ones — `hb_vmPushObjectVarRef`, `hb_vmWithObjectStart`, `hb_vmFuncPtr` — are in the same file, fine). The `WITHOBJECTMESSAGE` shim declares `pWith` at the top of the block (C89 style, matching the codebase). If any `case` body differs from the reference table, reproduce it verbatim.

- [ ] **Step 3: Write the smoke test**

`tests/llvm/shim_smoke_d.c`:

```c
#include "hbvmsh.h"
#include <stdio.h>

int main( void )
{
   void * p[ 8 ];
   p[ 0 ] = ( void * ) hb_vmsh_pushself;
   p[ 1 ] = ( void * ) hb_vmsh_pushovarref;
   p[ 2 ] = ( void * ) hb_vmsh_withobjectstart;
   p[ 3 ] = ( void * ) hb_vmsh_withobjectend;
   p[ 4 ] = ( void * ) hb_vmsh_funcptr;
   p[ 5 ] = ( void * ) hb_vmsh_message;
   p[ 6 ] = ( void * ) hb_vmsh_withobjectmessage;
   p[ 7 ] = ( void * ) hb_vmsh_send;
   printf( "group D shims linkable: %p %p %p %p %p %p %p %p\n",
           p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7] );
   return 0;
}
```

- [ ] **Step 4: Rebuild Harbour and link the smoke test**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/shim_smoke_d.c -I include -L lib/win/mingw64 \
    -lhbvm -lhbrtl -lhbcommon -lhblang -lhbcpage -lgtstd -lhbpcre -lhbzlib \
    -lhbmacro -lhbrdd -lhbpp -o build/shim_smoke_d.exe
./build/shim_smoke_d.exe
```

Expected: `hvm.c` compiles with the 8 new shims (no errors); `libhbvm.a` rebuilds before the known unrelated DLL-link abort; `shim_smoke_d.exe` prints eight non-null pointers. (`--noinhibit-exec` on the `gcc` link is acceptable if the known incomplete-DLL situation requires it.)

- [ ] **Step 5: Commit**

```bash
cd c:/HarbourLLVM/core
git add include/hbvmsh.h src/vm/hvm.c tests/llvm/shim_smoke_d.c
git commit -m "vm: add group D op shims (OOP messages)"
```

---

### Task 2: Decoder table — mark the 9 opcodes supported

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\hb_pcdec.c`

The 9 opcodes already have correct `kind` (`HB_PCK_FIXED`) and `nLen` (1, 2, or 3) in `hb_pcInfo[]`. Only the `fSupported` field changes.

- [ ] **Step 1: Flip the flags**

In `src/compiler/hb_pcdec.c`, change the third field from `HB_FALSE` to `HB_TRUE` for exactly these 9 table rows (identify each by its `/* NN HB_P_<name> */` comment):

`HB_P_FUNCPTR` (14), `HB_P_MESSAGE` (48), `HB_P_PUSHSELF` (102), `HB_P_SEND` (111), `HB_P_SENDSHORT` (112), `HB_P_WITHOBJECTSTART` (143), `HB_P_WITHOBJECTMESSAGE` (144), `HB_P_WITHOBJECTEND` (145), `HB_P_PUSHOVARREF` (147).

Do NOT change `kind` or `nLen` on any row. Do NOT touch any opcode not in this list. `HB_P_PUSHFUNCSYM` (176) is already `HB_TRUE` — leave it.

- [ ] **Step 2: Update the file-header comment**

`hb_pcdec.c` has a header comment listing the supported subset. Add the 9 group D opcode names to the "in scope (fSupported = HB_TRUE)" list so the comment matches the table.

- [ ] **Step 3: Rebuild and verify**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c -I include -I src/compiler -L lib/win/mingw64 -lhbcommon -o build/pcdectest.exe
./build/pcdectest.exe
```

Expected: `libhbcplr.a` rebuilds before the known DLL abort; `pcdectest.exe` still prints `pcdec: 4 instructions, all boundaries correct` and `pcdec: jump analysis correct`. `git diff` shows exactly 9 `HB_FALSE`→`HB_TRUE` flips in the table.

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/hb_pcdec.c
git commit -m "compiler: mark group D opcodes supported in the decoder table"
```

---

### Task 3: Emitter cases for the 9 opcodes

**Files:**
- Modify: `c:\HarbourLLVM\core\src\compiler\genllvm.c`

- [ ] **Step 1: Add the `declare` lines**

In the module-header emission of `genllvm.c`, where the group C `declare i32 @hb_vmsh_*` lines are emitted, add the 8 distinct shims:

```c
   fprintf( yyc, "declare i32 @hb_vmsh_pushself()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushovarref()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_withobjectstart()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_withobjectend()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_funcptr()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_message(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_withobjectmessage(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_send(i32)\n" );
```

(`%%` is the `fprintf` escape for a literal `%`, so the emitted IR reads `%HB_SYMB*`.)

- [ ] **Step 2: Add the emitter cases**

In `hb_llvmSLEmitBody`'s opcode `switch`, add cases for the 9 opcodes.

The **5 no-operand opcodes** (`HB_P_PUSHSELF`→`pushself`, `HB_P_PUSHOVARREF`→`pushovarref`, `HB_P_WITHOBJECTSTART`→`withobjectstart`, `HB_P_WITHOBJECTEND`→`withobjectend`, `HB_P_FUNCPTR`→`funcptr`) follow the existing `HB_EMIT_NOARG_SHIM` pattern — add five invocations before the `#undef HB_EMIT_NOARG_SHIM`.

The **2 symbol-operand opcodes** mirror the existing `HB_P_PUSHSYM` / group C symbol-operand emitter cases:
- `HB_P_MESSAGE` — decode `HB_PCODE_MKUSHORT(&pCode[pos+1])`, emit the `getelementptr [K x %HB_SYMB], [K x %HB_SYMB]* @symbols_table, i32 0, i32 <idx>` and `call i32 @hb_vmsh_message(%HB_SYMB* <gep>)`.
- `HB_P_WITHOBJECTMESSAGE` — decode `HB_PCODE_MKUSHORT(&pCode[pos+1])`. **If the decoded index == 0xFFFF**, pass `%HB_SYMB* null` to `hb_vmsh_withobjectmessage` (no `getelementptr`); **otherwise** emit the same `getelementptr` and pass it. (The shim branches on NULL — see the spec.)

The **2 count-operand opcodes** (`HB_P_SEND`, `HB_P_SENDSHORT`) mirror the existing group A `HB_P_LOCALINC` / group B `HB_P_ARRAYDIM` count-operand cases — a single shim call with a decoded integer:
- `HB_P_SEND` — count `HB_PCODE_MKUSHORT(&pCode[pos+1])`, emit `call i32 @hb_vmsh_send(i32 <count>)`.
- `HB_P_SENDSHORT` — count `pCode[pos+1]` (1 byte, unsigned), emit `call i32 @hb_vmsh_send(i32 <count>)`.

Every case ends with the same action-request `icmp`/`br` to `%epilogue` / next block the group A/B/C cases use, via the existing next-block-label helper (`label %epilogue` when `nextOff >= nPCSize`). Study the existing `HB_P_PUSHSYM` case for the exact `getelementptr` text / SSA-naming and reproduce it.

- [ ] **Step 3: Rebuild and check the IR for an OOP program**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
./win-make.exe
# ensure harbour.exe relinked; if not: touch src/main/harbour.c && ./win-make.exe
printf 'function Main()\n   local o := ErrorNew()\n   o:description := "hi"\n   ? o:description\n   return nil\n' > build/ooptmp.prg
bin/win/mingw64/harbour.exe -GL -q -obuild/t3d build/ooptmp.prg
"/c/Program Files/LLVM/bin/amdclang++.exe" -S -emit-llvm -x ir build/t3d.ll -o NUL
grep -c "hb_vmsh_send\|hb_vmsh_message" build/t3d.ll
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/t3d.ll | grep -c "call void @hb_vmExecute"
```

Expected: clean rebuild; `t3d.ll` passes the LLVM verifier (only the benign `-Woverride-module` warning); the OOP-shim grep is ≥ 1; the `awk|grep` count is `0` (`HB_FUN_MAIN` no longer falls back). (`ErrorNew()` is a built-in that returns an object — it exercises `MESSAGE`/`SEND` for the `:description` access/assignment.)

- [ ] **Step 4: Commit**

```bash
cd c:/HarbourLLVM/core
git add src/compiler/genllvm.c
git commit -m "compiler: emit straight-line IR for group D opcodes (OOP messages)"
```

---

### Task 4: Corpus — verify OOP message sends

**Files:**
- Create: `c:\HarbourLLVM\core\tests\llvm\oop.prg`
- Modify: `c:\HarbourLLVM\core\tests\llvm\run.sh`

- [ ] **Step 1: Write the new corpus program**

`tests/llvm/oop.prg` — uses Harbour's `Error` built-in class (always available, no contrib needed) so the test exercises method/ivar access (`MESSAGE`/`SEND`) without defining a class:

```harbour
function Main()
   local oErr := ErrorNew()
   oErr:description := "disk full"
   oErr:subcode := 42
   ? oErr:description
   ? oErr:subcode
   ? UseObj( oErr )
   return nil

function UseObj( o )
   return o:subcode + 1
```

(If `ErrorNew` / the `Error` class is not directly usable this way, fall back to a `WITH OBJECT oErr ... END` block over the same object — both exercise the group D opcodes. The implementer picks whichever compiles; the goal is a program that emits `MESSAGE`/`SEND` and ideally `WITHOBJECT*`.)

- [ ] **Step 2: Update `run.sh`'s straight-line assertion set**

`tests/llvm/run.sh` asserts which programs straight-line vs fall back. Add `oop` to the straight-lined-expected set (checked like `arith`/`loop`: must contain `hb_vmsh_`, must NOT contain `call void @hb_vmExecute` in `HB_FUN_MAIN`). Keep `fallback` in the fallback set.

Note: if `oop.prg` legitimately falls back because it emits an opcode still outside the A+B+C+D subset, do NOT force it into the straight-lined set — leave it where it lands and report it in Task 4 Step 5. The diff-against-C-backend is the correctness gate; a correct fallback is acceptable.

- [ ] **Step 3: Run the new program end-to-end**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
export HB_COMPILER=mingw64 HB_PLATFORM=win
cd c:/HarbourLLVM/core
env PATH="/c/Windows/System32:/c/Windows" \
  bin/win/mingw64/harbour.exe -GL -q -obuild/gd_oop tests/llvm/oop.prg
./build/gd_oop.exe > build/gd_oop_ll.out 2>&1
bin/win/mingw64/hbmk2.exe -q -gtstd -obuild/gd_oop_c tests/llvm/oop.prg
./build/gd_oop_c.exe > build/gd_oop_c.out 2>&1
diff build/gd_oop_c.out build/gd_oop_ll.out && echo "oop: MATCH" || echo "oop: DIFF"
```

Expected: `oop: MATCH` — the straight-line backend output is byte-identical to the C backend.

- [ ] **Step 4: Confirm the program straight-lined**

```bash
cd c:/HarbourLLVM/core
echo -n "oop hb_vmsh_ count: "; grep -c "hb_vmsh_" build/gd_oop.ll
echo -n "oop hb_vmExecute in MAIN: "
awk '/^define.*@HB_FUN_MAIN\(\)/{f=1} f{print} f&&/^\}$/{exit}' build/gd_oop.ll | grep -c "call void @hb_vmExecute"
```

Expected: `hb_vmsh_` count ≥ 1; `hb_vmExecute in MAIN` count is `0`. (If non-zero fallback, report it — see Step 2's note.)

- [ ] **Step 5: Run `run.sh` and commit**

```bash
export PATH="/c/Users/Anto/winlibs/mingw64/bin:$PATH"
cd c:/HarbourLLVM/core
CLANG="/c/Program Files/LLVM/bin/amdclang++.exe" tests/llvm/run.sh build/llvm-gd
```

Expected: `run.sh` ends with `RESULT: all programs validated and matched the C backend`, and the report shows `oop` straight-lined (or, if it fell back, that it still MATCHes the C backend — report which).

```bash
cd c:/HarbourLLVM/core
git add tests/llvm/oop.prg tests/llvm/run.sh
git commit -m "test: corpus for group D — OOP message sends straight-lined"
```

---

## Self-Review

**Spec coverage:** The spec's three opcode sub-groups (5 no-operand + 2 symbol-operand + 2 count-operand, 9 opcodes / 8 shims) are covered — Task 1 the 8 shims, Task 2 the 9 decoder flags, Task 3 the 9 emitter cases, Task 4 the OOP corpus. The spec's two design decisions — the `SEND` always-push (no POP-peek) and the `WITHOBJECTMESSAGE` 0xFFFF→NULL handling — are in Task 1 Step 2 (the shim bodies) and Task 3 Step 2 (the emitter's 0xFFFF→`%HB_SYMB* null` branch). The spec's testing section (`oop.prg`, diff against the C backend) is Task 4.

**Placeholder scan:** All 8 shims are shown in full in Task 1 Step 2. Task 3's no-operand cases are anchored to the existing `HB_EMIT_NOARG_SHIM` pattern, the symbol-operand cases to the existing `HB_P_PUSHSYM` case, the count-operand cases to the existing group A/B count-operand cases — all concrete existing references. Task 2's flip list names all 9 opcodes with numeric values. The `oop.prg` test gives a concrete program with a documented fallback (`WITH OBJECT`) if `ErrorNew` is awkward. No "TBD"/vague steps.

**Type consistency:** Shim names and signatures are identical across `hbvmsh.h` (Task 1 Step 1), the implementations (Task 1 Step 2), and the IR `declare`s (Task 3 Step 1): 5 no-operand shims `int (void)` / `i32 ()`; `hb_vmsh_message` and `hb_vmsh_withobjectmessage` `int (PHB_SYMB)` / `i32 (%HB_SYMB*)`; `hb_vmsh_send` `int (int)` / `i32 (i32)`. `HB_P_SEND` and `HB_P_SENDSHORT` consistently route to `hb_vmsh_send`. The 9 opcode names in Task 2's flip list match the reference table and the spec.

**Known risk:** the `WITHOBJECTMESSAGE` 0xFFFF case requires the emitter to branch (emit `null` vs a `getelementptr`) — a new wrinkle vs groups A-C, but small and explicitly specified in Task 3 Step 2. The Task 4 diff-against-C-backend corpus is the gate. If `oop.prg` does not actually emit `WITHOBJECTMESSAGE` (it only emits `MESSAGE`/`SEND`), the 0xFFFF branch is exercised only by inspection — acceptable; a `WITH OBJECT` variant in `oop.prg` would exercise the non-0xFFFF `WITHOBJECTMESSAGE` path.
