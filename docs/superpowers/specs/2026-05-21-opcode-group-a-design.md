# Opcode Subset Extension — Group A: FOR loops + compound assignment

Date: 2026-05-21
Status: Approved design

## Goal

Extend the straight-line LLVM backend (Plan 3) to cover the FOR-loop and
compound-assignment pcode opcodes, so `FOR..NEXT` / `FOR..STEP` loops and the
`+=` `-=` `*=` `/=` `%=` `^=` `++` `--` operators are emitted as straight-line
native code instead of falling back to the `hb_vmExecute` interpreter.

## Context

Plan 3 gave `harbour -GL` a straight-line emitter for a functional opcode
subset, with a whole-function fallback to the interpreter for any unsupported
opcode. `tests/llvm/loop.prg` currently falls back because a FOR loop emits
`HB_P_FORTEST`, `HB_P_PUSHLOCALREF`, and the compound-assignment opcodes — none
of which are in the subset.

This is the first of nine planned opcode-group extensions (A–I). Group A is the
lowest-risk, highest-value group: every opcode is fixed-length and maps to a
mechanical shim.

## Architecture

Identical to Plan 3 — no new files, no new mechanisms. Each opcode gets:

1. A `hb_vmsh_*` shim function appended to `src/vm/hvm.c` (the only TU that
   sees the `static` VM op helpers), declared in `include/hbvmsh.h`. Each shim
   replicates the body of the matching `case HB_P_*` in the `hb_vmExecute`
   switch and returns the action request (`int`, 0 = continue).
2. Its `hb_pcInfo[]` row in `src/compiler/hb_pcdec.c` flipped to
   `fSupported = HB_TRUE` (all 28 already have correct `kind`/`nLen`).
3. An emitter `case` in `genllvm.c`'s straight-line body emitter, following the
   existing per-opcode pattern (call the shim, check the returned action
   request, branch to the next block or the epilogue).

## The 28 opcodes

**No-operand stack ops (21)** — shim is `int hb_vmsh_X(void)`:
`HB_P_FORTEST`, `HB_P_PLUSEQ`, `HB_P_MINUSEQ`, `HB_P_MULTEQ`, `HB_P_DIVEQ`,
`HB_P_MODEQ`, `HB_P_EXPEQ`, `HB_P_PLUSEQPOP`, `HB_P_MINUSEQPOP`,
`HB_P_MULTEQPOP`, `HB_P_DIVEQPOP`, `HB_P_MODEQPOP`, `HB_P_EXPEQPOP`,
`HB_P_DECEQ`, `HB_P_INCEQ`, `HB_P_DECEQPOP`, `HB_P_INCEQPOP`, `HB_P_INC`,
`HB_P_DEC`, `HB_P_DUPLUNREF`, `HB_P_PUSHUNREF`.

**Push-by-reference (2)** — shim is `int hb_vmsh_X(int iIndex)`, operand is a
2-byte index (`HB_PCODE_MKSHORT` signed for local, `HB_PCODE_MKUSHORT` for
static): `HB_P_PUSHLOCALREF`, `HB_P_PUSHSTATICREF`.

**Local direct-modify (5)**:
- `HB_P_LOCALINC`, `HB_P_LOCALDEC`, `HB_P_LOCALINCPUSH` — `int hb_vmsh_X(int
  iLocal)`, 2-byte unsigned local index.
- `HB_P_LOCALADDINT` — `int hb_vmsh_localaddint(int iLocal, int iAdd)`,
  2-byte unsigned local index + 2-byte signed addend.
- `HB_P_LOCALNEARADDINT` — `int hb_vmsh_localnearaddint(int iLocal, int iAdd)`,
  1-byte unsigned local index + 2-byte signed addend.

Total: 21 + 2 + 5 = 28 distinct shims.

## Correctness

- Each compound-assignment `case` in the interpreter calls `static` helpers
  (`hb_itemUnRef`, `hb_vmInc`, `hb_vmDec`, `hb_stackDec`, `hb_itemMove`,
  `hb_itemCopy`, `hb_vmAddInt`, the arithmetic ops). The shim lives in `hvm.c`,
  so all are reachable. Each shim must reproduce its `case` body exactly,
  including the `HB_IS_BYREF`/`hb_itemUnRef` handling in the `LOCAL*` cases.
- Each shim returns `hb_stackGetActionRequest()`; the emitter checks it after
  the call, exactly as Plan 3 does.
- Operand widths/signedness must match the `hb_vmExecute` cases precisely.

## Testing

- `tests/llvm/loop.prg` must now straight-line: the verification asserts
  `HB_FUN_MAIN` contains `hb_vmsh_` and no `call void @hb_vmExecute`.
- New `tests/llvm/compound.prg` exercising `+=`, `-=`, `*=`, `/=`, `++`, `--`.
- New `tests/llvm/forstep.prg` exercising `FOR..STEP` with a non-1 and a
  negative step.
- Each program is built with `-GL` and with the C backend; outputs must be
  byte-identical (`tests/llvm/run.sh`).

## Out of scope

Opcode groups B–I (arrays, RDD/fields, OOP, FOR EACH, SWITCH, codeblocks,
macros, SEQUENCE) — each gets its own later spec.
