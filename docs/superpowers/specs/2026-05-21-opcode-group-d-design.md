# Opcode Subset Extension — Group D: OOP messages

Date: 2026-05-21
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover the object-oriented message
opcodes, so method calls (`oObj:method()`), `Self`, object-variable
references, and `WITH OBJECT ... END` blocks are emitted as straight-line
native code instead of falling back to the `hb_vmExecute` interpreter.

## Context

The straight-line LLVM backend (Plans 1-3, opcode groups A, B, C) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions using
an opcode outside the subset fall back, whole-function, to the interpreter.
Group D is the fourth of nine planned opcode-group extensions (A–I).

## Architecture

Identical to groups A, B, C — no new files, no new mechanisms. Each opcode
gets a `hb_vmsh_*` shim appended to `src/vm/hvm.c`, declared in
`include/hbvmsh.h`; `fSupported = HB_TRUE` in `hb_pcInfo[]`; and an emitter
`case` in `genllvm.c`.

## The 9 opcodes / 8 distinct shims

**No-operand (5)** — shim `int hb_vmsh_X(void)`, instruction length 1:
- `HB_P_PUSHSELF` → `hb_vmPush( hb_stackSelfItem() )`
- `HB_P_PUSHOVARREF` → `hb_vmPushObjectVarRef()`
- `HB_P_WITHOBJECTSTART` → `hb_vmWithObjectStart()`
- `HB_P_WITHOBJECTEND` → `hb_stackPop(); hb_stackPop();`
- `HB_P_FUNCPTR` → `hb_vmFuncPtr()`

**Symbol-operand (2)** — shim `int hb_vmsh_X(PHB_SYMB pSym)`, the symbol
pointer supplied by the emitter as a `getelementptr` into `@symbols_table`
(the established `HB_P_PUSHSYM` pattern):
- `HB_P_MESSAGE` (len 3) → `hb_vmPushSymbol( pSym )`
- `HB_P_WITHOBJECTMESSAGE` (len 3) → see the 0xFFFF note below

**Count-operand (2 opcodes, 1 shim)** — shim `int hb_vmsh_send(int uiParams)`:
- `HB_P_SEND` (len 3, 2-byte count)
- `HB_P_SENDSHORT` (len 2, 1-byte count)

## Two design decisions

**`SEND` / `SENDSHORT` — drop the POP-peek micro-optimization.** The
interpreter's `HB_P_SEND` case, after the send, peeks at the next opcode: if
it is `HB_P_POP`, it skips both the result-push and that POP. The straight-line
backend does NOT reproduce this peek. `hb_vmsh_send` always does
`hb_itemSetNil( hb_stackReturnItem() ); hb_vmSend( uiParams );
hb_stackPushReturn();` — it always pushes the return value. When a `HB_P_POP`
follows, it is already a supported opcode and its own emitter `case` pops the
value. Always-push-then-pop is semantically identical to skip-both; the
straight-line backend simply forgoes a micro-optimization. No peek-ahead, no
fused basic blocks.

**`WITHOBJECTMESSAGE` — the 0xFFFF operand.** The interpreter resolves the
operand as a symbol index *unless* it is `0xFFFF`, the sentinel for the
`:&macro` form (the symbol was already pushed by `HB_P_MACROSYMBOL`, a macro
opcode). The emitter handles this: for a real index it passes
`getelementptr @symbols_table`; for `0xFFFF` it passes `%HB_SYMB* null`. The
shim reproduces the interpreter's branch as `if( pSym != NULL )
hb_vmPushSymbol( pSym );` followed by the with-object push. A `WITH OBJECT`
using `:&macro` also needs macro opcodes (group H) and so still falls back
whole-function until then; an ordinary `WITH OBJECT o ... :method ... END`
straight-lines.

## Correctness

- Each shim reproduces its `hb_vmExecute` case verbatim (minus the SEND
  micro-opt as described), starts with `HB_STACK_TLS_PRELOAD`, and returns
  `( int ) hb_stackGetActionRequest()` — the same shape as every group A/B/C
  shim.
- The symbol pointer for `MESSAGE` / `WITHOBJECTMESSAGE` is decoded by the
  emitter from `HB_PCODE_MKUSHORT(&pCode[off+1])`; the `SEND` count from the
  same macro, the `SENDSHORT` count from `pCode[off+1]` (1 byte, unsigned).

## Testing

- New `tests/llvm/oop.prg` — a small class defined with Harbour's OOP API
  (`HBClass`-style or `TClass`), an object created, methods called, an
  instance variable read/written; printed.
- Built with `-GL` and with the C backend; outputs must be byte-identical
  (`tests/llvm/run.sh`), and the program must straight-line (no
  `call void @hb_vmExecute` in `HB_FUN_MAIN`) unless it legitimately uses an
  opcode outside the A+B+C+D subset, in which case a correct fallback is
  acceptable and is reported.

## Out of scope

`HB_P_MMESSAGE` and `HB_P_MACROSEND` (message sends through the macro `&`
operator) are group H. `HB_P_PUSHFUNCSYM` is already supported (Plan 3). A
program mixing a group D opcode with an opcode outside the straight-line
subset still falls back whole-function. Opcode groups E–I get their own later
specs.
