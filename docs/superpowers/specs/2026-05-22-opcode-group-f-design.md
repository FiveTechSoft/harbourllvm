# Opcode Subset Extension — Group F: SWITCH

Date: 2026-05-22
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover `HB_P_SWITCH`, so a `SWITCH`
statement with a runtime (non-constant) selector is emitted as straight-line
native code instead of falling back to the `hb_vmExecute` interpreter.

## Context

The straight-line LLVM backend (Plans 1-3, opcode groups A–E) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions using
an opcode outside the subset fall back, whole-function, to the interpreter.
Group F is the sixth of the planned opcode-group extensions.

`HB_P_SWITCH` is the one opcode in group F. A `SWITCH` whose selector is a
compile-time constant is already folded by the compiler to a direct
`HB_P_JUMP` (supported since Plan 3); only a runtime-selector `SWITCH` emits
`HB_P_SWITCH`.

## Why group F is not the simple A–E pattern

`HB_P_SWITCH` is variable-length and carries an embedded jump table; the
interpreter `case` (`pCode = hb_vmSwitch( pCode + 3, count )`) computes a jump
internally. Group F therefore touches three places, not just the usual
shim + flag + emitter case:

1. **`src/compiler/hb_pcdec.c` — decoder.** `HB_P_SWITCH` is currently
   `HB_PCK_UNKNOWN` (forces whole-function fallback). It needs:
   - a new decode kind `HB_PCK_SWITCH`, and a `hb_pcodeInstrLen` arm that
     computes the real instruction length by walking the case table;
   - `hb_pcodeAnalyze` extended so the jump targets embedded inside the
     `HB_P_SWITCH` table are registered as basic-block leaders.
2. **`src/vm/hvm.c` — one shim** `hb_vmsh_switchidx` (see below).
3. **`src/compiler/genllvm.c` — emitter** — emit the case table as a private
   constant, call the shim, and emit a real LLVM `switch` instruction.

## Instruction layout

`HB_P_SWITCH` = `[opcode][caseCount: 2-byte LE][N case entries]`. Each case
entry is a literal-push opcode followed by a jump opcode:
- literal: `HB_P_PUSHLONG` (5 bytes) | `HB_P_PUSHSTRSHORT` (2 + len bytes) |
  `HB_P_PUSHNIL` (1 byte — the `OTHERWISE`/default clause);
- jump: `HB_P_JUMPNEAR` (2) | `HB_P_JUMP` (3) | `HB_P_JUMPFAR` (4).

Total instruction length = `3 + Σ(literal + jump)` over the N entries.

## The shim — `hb_vmsh_switchidx`

Reproducing the comparison in IR risks a semantic gap (`hb_vmSwitch` matches a
`PUSHLONG` case only when the selector `HB_IS_NUMINT`; a generic equality op
does not). To stay bit-exact, group F uses one shim that reuses
`hb_vmSwitch`'s exact match predicates:

```c
/* Walk the SWITCH case table; return the 0-based index of the first case
 * whose literal matches the selector on the stack top, or caseCount if none
 * matches. Pops the selector (as the interpreter's hb_vmSwitch does). */
extern HB_EXPORT int hb_vmsh_switchidx( const unsigned char * pTable,
                                        int caseCount );
```

`pTable` points at the first case entry (the bytes after the 3-byte header).
The shim peeks the selector with `hb_vmSwitchGet()`, walks the entries with
the same per-literal comparison logic as `hb_vmSwitch` (raw `NUMINT` equality
for `PUSHLONG`, length+`memcmp` for `PUSHSTRSHORT`, always-true for
`PUSHNIL`), records the first matching index, calls `hb_stackPop()`, and
returns the index. The shim is appended to `hvm.c` next to the existing group
shims and follows the same `HB_STACK_TLS_PRELOAD` / `HB_EXPORT` shape; because
it is in `hvm.c` it can call the `static` `hb_vmSwitchGet`.

## Emitter

At an `HB_P_SWITCH` opcode the emitter:
1. decodes the case table — for each entry, the literal and the jump target
   offset (`target = <jump-opcode offset> + displacement`, the displacement
   read with the same sign rules Plan 3 uses for `JUMP`/`JUMPNEAR`/`JUMPFAR`);
2. emits the raw case-table bytes as a `private constant [K x i8]` global;
3. emits `%idx = call i32 @hb_vmsh_switchidx(i8* <table-gep>, i32 <caseCount>)`,
   the standard action-request check, then
4. an LLVM `switch i32 %idx, label %<fallthrough> [ i32 0, label %i<t0>
   i32 1, label %i<t1> ... ]` where `t<k>` is case `k`'s target block and the
   `fallthrough` label is the block at the offset immediately after the whole
   `HB_P_SWITCH` instruction (the index `caseCount` "no match" result lands
   there).

Each `i<t<k>>` target is an ordinary straight-line block — Task-2's
`hb_pcodeAnalyze` change guarantees a block leader exists at every such
offset.

## Correctness

- `hb_vmsh_switchidx` reuses `hb_vmSwitch`'s exact comparison predicates, so
  case matching is bit-identical to the interpreter; it pops the selector
  exactly as `hb_vmSwitch` does.
- The default (`PUSHNIL`/`OTHERWISE`) clause is just another entry; the shim
  returns its index when reached, and the emitter's LLVM `switch` routes it
  like any other case.
- The instruction length the decoder computes must exactly match the
  interpreter's table walk, or the straight-line walk of the rest of the
  function desynchronises — the Task-2 decoder change is verified against
  `hb_vmSwitch`'s own `pCode +=` arithmetic.

## Testing

- New `tests/llvm/switchstmt.prg` — a `SWITCH` over a runtime variable with
  several integer `CASE`s, a string `CASE`, and an `OTHERWISE` clause; run for
  several selector values so multiple arms (and the default) are taken;
  printed.
- Built with `-GL` and with the C backend; outputs must be byte-identical
  (`tests/llvm/run.sh`), and the program must straight-line (no
  `call void @hb_vmExecute` in `HB_FUN_MAIN`).

## Out of scope

A `SWITCH` with a compile-time-constant selector already lowers to a direct
`HB_P_JUMP` and needs nothing from group F. A program mixing `HB_P_SWITCH`
with an opcode outside the straight-line subset still falls back
whole-function. Opcode groups G–I (codeblocks, macros, SEQUENCE) get their own
later specs.
