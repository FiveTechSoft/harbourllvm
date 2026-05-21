# Opcode Subset Extension — Group C: RDD fields, memvars, aliases

Date: 2026-05-21
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover the database-field, memory-
variable, undeclared-variable, and workarea-alias pcode opcodes, so field
access (`customer->name`), memvar access (`PRIVATE`/`PUBLIC` variables),
undeclared-variable access, aliased access (`alias->field`), and workarea
selection are emitted as straight-line native code instead of falling back to
the `hb_vmExecute` interpreter.

## Context

The straight-line LLVM backend (Plans 1-3, opcode groups A and B) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions using
an opcode outside the subset fall back, whole-function, to the interpreter.
Group C is the third of nine planned opcode-group extensions (A–I).

## Architecture

Identical to groups A and B — no new files, no new mechanisms. Each opcode
gets:

1. A `hb_vmsh_*` shim appended to `src/vm/hvm.c` (the only TU that sees the
   `static` VM op helpers), declared in `include/hbvmsh.h`. Each shim
   replicates the body of the matching `case HB_P_*` in the `hb_vmExecute`
   switch and returns the action request (`int`, 0 = continue).
2. Its `hb_pcInfo[]` row in `src/compiler/hb_pcdec.c` flipped to
   `fSupported = HB_TRUE` (all 16 already have correct `kind`/`nLen`).
3. An emitter `case` in `genllvm.c`'s straight-line body emitter.

### Symbol-pointer operands — the key point

13 of the 16 opcodes carry a symbol-table index operand: the interpreter
resolves it as `pSymbols + index`, a `PHB_SYMB *` pointer into the module
symbol table — exactly as `HB_P_PUSHSYM` does. The straight-line emitter
already handles this for `HB_P_PUSHSYM`: it emits
`getelementptr([K x %HB_SYMB], [K x %HB_SYMB]* @symbols_table, i32 0,
i32 <idx>)` and passes the result to `hb_vmsh_pushsymbol(%HB_SYMB*)`.

Group C's symbol-operand shims take a `PHB_SYMB *` argument the same way; the
emitter passes the same `getelementptr`. No new mechanism — a proven pattern.

## The 16 opcodes / 14 distinct shims

**No-operand (3)** — shim `int hb_vmsh_X(void)`, instruction length 1:
- `HB_P_PUSHALIAS` → `hb_vmPushAlias()`
- `HB_P_POPALIAS` → `hb_vmPopAlias()`
- `HB_P_SWAPALIAS` → `hb_vmSwapAlias()`

**Symbol-pointer operand (11 shims, 13 opcodes)** — shim
`int hb_vmsh_X(PHB_SYMB pSym)`, the symbol pointer supplied by the emitter:
- `HB_P_PUSHFIELD` (len 3) → `hb_vmsh_pushfield`
- `HB_P_POPFIELD` (len 3) → `hb_vmsh_popfield`
- `HB_P_PUSHMEMVAR` (len 3) → `hb_vmsh_pushmemvar`
- `HB_P_PUSHMEMVARREF` (len 3) → `hb_vmsh_pushmemvarref`
- `HB_P_POPMEMVAR` (len 3) → `hb_vmsh_popmemvar`
- `HB_P_PUSHVARIABLE` (len 3) → `hb_vmsh_pushvariable`
- `HB_P_POPVARIABLE` (len 3) → `hb_vmsh_popvariable`
- `HB_P_PUSHALIASEDFIELD` (len 3) and `HB_P_PUSHALIASEDFIELDNEAR` (len 2) →
  both use `hb_vmsh_pushaliasedfield`
- `HB_P_POPALIASEDFIELD` (len 3) and `HB_P_POPALIASEDFIELDNEAR` (len 2) →
  both use `hb_vmsh_popaliasedfield`
- `HB_P_PUSHALIASEDVAR` (len 3) → `hb_vmsh_pushaliasedvar`
- `HB_P_POPALIASEDVAR` (len 3) → `hb_vmsh_popaliasedvar`

The `*NEAR` opcodes differ from their non-NEAR forms only in operand width
(1-byte vs 2-byte index) — that is the emitter's decode concern; the shim is
identical, so the two NEAR opcodes add no new shims (16 opcodes → 14 shims).

## Correctness

- Each shim reproduces its `hb_vmExecute` case verbatim, starts with
  `HB_STACK_TLS_PRELOAD`, and returns `( int ) hb_stackGetActionRequest()` —
  the same shape as every group A/B shim. The `pop`-family cases call a helper
  then `hb_stackPop()`; the shim does both.
- The symbol pointer is decoded by the emitter from the index operand
  (`HB_PCODE_MKUSHORT(&pCode[off+1])` for the 2-byte forms,
  `pCode[off+1]` unsigned for the 1-byte `*NEAR` forms) and passed as the
  `getelementptr` into `@symbols_table`.

## Testing

- New `tests/llvm/memvar.prg` — a `PRIVATE` variable assigned and read back
  (exercises `PUSHMEMVAR` / `POPMEMVAR`); printed.
- New `tests/llvm/field.prg` — creates a small temporary DBF, opens it, writes
  and reads a field (exercises `PUSHFIELD` / `POPFIELD` and the alias
  opcodes), then closes and deletes it.
- Each is built with `-GL` and with the C backend; outputs must be
  byte-identical (`tests/llvm/run.sh`), and each must straight-line (no
  `call void @hb_vmExecute` in `HB_FUN_MAIN`) unless it legitimately uses an
  opcode outside the A+B+C subset, in which case a correct fallback is
  acceptable and is reported.

## Out of scope

A program mixing a group C opcode with an opcode outside the straight-line
subset (codeblocks, OOP messages, FOR EACH, SWITCH, macros, SEQUENCE) still
falls back whole-function to the interpreter. Opcode groups D–I get their own
later specs.
