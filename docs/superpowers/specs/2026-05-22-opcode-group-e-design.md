# Opcode Subset Extension — Group E: FOR EACH

Date: 2026-05-22
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover the `FOR EACH` iteration
opcodes, so `FOR EACH x IN aCollection ... NEXT` loops (and the `DESCEND`
variant) over arrays, hashes, and strings are emitted as straight-line native
code instead of falling back to the `hb_vmExecute` interpreter.

## Context

The straight-line LLVM backend (Plans 1-3, opcode groups A–D) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions using
an opcode outside the subset fall back, whole-function, to the interpreter.
Group E is the fifth of nine planned opcode-group extensions (A–I).

## Architecture

Identical to groups A–D — no new files, no new mechanisms. Each opcode gets a
`hb_vmsh_*` shim appended to `src/vm/hvm.c`, declared in `include/hbvmsh.h`;
`fSupported = HB_TRUE` in `hb_pcInfo[]`; and an emitter `case` in `genllvm.c`.

A `FOR EACH` loop compiles to:
```
ENUMSTART nVars, nDescend     ; init; pushes a "should the loop run" logical
JUMPFALSE  <skip-to-ENUMEND>  ; already-supported opcode
<loop body>
ENUMNEXT  (or ENUMPREV)       ; advance; pushes "exhausted" logical
JUMPFALSE  <back to body>     ; already-supported opcode
ENUMEND                       ; cleanup
```
`ENUMNEXT` / `ENUMPREV` do **not** jump — they leave a logical on the stack
for the following `JUMPFALSE`, which is already a supported opcode (Plan 3).
The straight-line emitter therefore needs no special control-flow handling for
group E; each ENUM opcode is an ordinary call-and-branch basic block.

## The 4 opcodes

**No-operand (3)** — shim `int hb_vmsh_X(void)`, instruction length 1:
- `HB_P_ENUMNEXT` → `hb_vmEnumNext()`
- `HB_P_ENUMPREV` → `hb_vmEnumPrev()`
- `HB_P_ENUMEND` → `hb_vmEnumEnd()`

**Two-operand (1)** — instruction length 3, two 1-byte unsigned operands:
- `HB_P_ENUMSTART` — interpreter case is
  `hb_vmEnumStart( (unsigned char) pCode[1], (unsigned char) pCode[2] )`
  (`pCode[1]` = number of loop iterators, `pCode[2]` = descend flag). Shim
  `int hb_vmsh_enumstart(int nVars, int nDescend)` casts each to
  `unsigned char` for the helper call.

## Correctness

- Each shim reproduces its `hb_vmExecute` case verbatim, starts with
  `HB_STACK_TLS_PRELOAD`, and returns `( int ) hb_stackGetActionRequest()` —
  the same shape as every group A–D shim.
- `hb_vmEnumStart` is internally stateful (it manipulates several stack slots
  and may dispatch object enumerator methods), but the interpreter `case` is a
  single call — the shim is mechanical, exactly as group B's `ARRAYGEN` /
  `HASHGEN` wrap their complex helpers.
- `ENUMSTART`'s two operands are decoded by the emitter from `pCode[off+1]`
  and `pCode[off+2]` (1 byte each, unsigned) and passed as the two `i32`
  shim arguments.

## Testing

- New `tests/llvm/foreach.prg` — a `FOR EACH x IN aArray ... NEXT` loop that
  sums the elements; printed. If a `FOR EACH ... DESCEND` form compiles
  cleanly it is added too (exercising `ENUMPREV`).
- Built with `-GL` and with the C backend; outputs must be byte-identical
  (`tests/llvm/run.sh`), and the program must straight-line (no
  `call void @hb_vmExecute` in `HB_FUN_MAIN`) unless it legitimately uses an
  opcode outside the A–E subset, in which case a correct fallback is
  acceptable and is reported.

## Out of scope

`FOR EACH` over a user-defined object enumerator dispatches OOP methods at
runtime — that is helper behaviour, not codegen; the shim forwards it
unchanged. A program mixing a group E opcode with an opcode outside the
straight-line subset still falls back whole-function. Opcode groups F–I get
their own later specs.
