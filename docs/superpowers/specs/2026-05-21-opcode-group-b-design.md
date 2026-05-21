# Opcode Subset Extension — Group B: arrays + hashes

Date: 2026-05-21
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover the array and hash pcode
opcodes, so array literals (`{ 1, 2, 3 }`), hash literals (`{ "k" => v }`),
element access (`a[ i ]`), element assignment (`a[ i ] := x`),
multi-dimensional array creation (`Array( n, m )`), and `...`-parameter
forwarding are emitted as straight-line native code instead of falling back to
the `hb_vmExecute` interpreter.

## Context

The straight-line LLVM backend (Plans 1-3, opcode group A) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions using
an opcode outside the subset fall back, whole-function, to the interpreter.
Group B is the second of nine planned opcode-group extensions (A–I).

## Architecture

Identical to group A — no new files, no new mechanisms. Each opcode gets:

1. A `hb_vmsh_*` shim appended to `src/vm/hvm.c` (the only TU that sees the
   `static` VM op helpers), declared in `include/hbvmsh.h`. Each shim
   replicates the body of the matching `case HB_P_*` in the `hb_vmExecute`
   switch and returns the action request (`int`, 0 = continue).
2. Its `hb_pcInfo[]` row in `src/compiler/hb_pcdec.c` flipped to
   `fSupported = HB_TRUE` (all 7 already have correct `kind`/`nLen`).
3. An emitter `case` in `genllvm.c`'s straight-line body emitter, following the
   group A per-opcode pattern.

Each interpreter `case` for these opcodes is a single call to one `static`
helper (`hb_vmArrayPush`, `hb_vmArrayGen`, `hb_vmHashGen`, …). Any loop over
the VM stack lives inside that helper, not in the `case` body — so the shim is
mechanical, exactly as in group A.

## The 7 opcodes

**No-operand (4)** — shim is `int hb_vmsh_X(void)`, instruction length 1:
- `HB_P_ARRAYPUSH` → `hb_vmArrayPush()`
- `HB_P_ARRAYPUSHREF` → `hb_vmArrayPushRef()`
- `HB_P_ARRAYPOP` → `hb_vmArrayPop()`
- `HB_P_PUSHAPARAMS` → `hb_vmPushAParams()`

**Count-operand (3)** — shim is `int hb_vmsh_X(int iCount)`, instruction
length 3, operand is a 2-byte count decoded with `HB_PCODE_MKUSHORT`:
- `HB_P_ARRAYDIM` → `hb_vmArrayDim( (HB_USHORT) iCount )`
- `HB_P_ARRAYGEN` → `hb_vmArrayGen( (HB_SIZE) iCount )`
- `HB_P_HASHGEN` → `hb_vmHashGen( (HB_SIZE) iCount )`

The shim takes a decoded `int`; it casts to the helper's parameter type
(`HB_USHORT` for `hb_vmArrayDim`, `HB_SIZE` for `hb_vmArrayGen`/`hb_vmHashGen`)
— the implementer confirms the exact signatures against `hvm.c`.

## Correctness

- Each shim reproduces its `hb_vmExecute` case verbatim, starts with
  `HB_STACK_TLS_PRELOAD`, and returns `( int ) hb_stackGetActionRequest()` —
  the same shape as every group A shim.
- The count operand for `ARRAYDIM`/`ARRAYGEN`/`HASHGEN` is decoded by the
  emitter from `HB_PCODE_MKUSHORT(&pCode[off+1])` and passed as the shim
  argument; the helper consumes that many stack items internally.

## Testing

- New `tests/llvm/arraylit.prg` — array literal `{ 1, 2, 3 }`, element read
  `a[ 2 ]`, element assignment `a[ 1 ] := 99`.
- New `tests/llvm/hashlit.prg` — hash literal `{ "a" => 1, "b" => 2 }` and a
  lookup.
- New `tests/llvm/arraydim.prg` — `Array( 3, 4 )` (multi-dimensional, exercises
  `ARRAYDIM`) and element access.
- Each is built with `-GL` and with the C backend; outputs must be
  byte-identical (`tests/llvm/run.sh`), and each must straight-line (no
  `call void @hb_vmExecute` in `HB_FUN_MAIN`).

## Out of scope

A program that mixes an array opcode with an opcode outside the straight-line
subset (codeblocks, RDD field access, OOP messages) still falls back
whole-function to the interpreter. Opcode groups C–I get their own later specs.
