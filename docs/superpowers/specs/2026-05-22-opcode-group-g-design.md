# Opcode Subset Extension — Group G: Codeblocks

Date: 2026-05-22
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover the codeblock-literal opcodes
`HB_P_PUSHBLOCK`, `HB_P_PUSHBLOCKSHORT`, and `HB_P_PUSHBLOCKLARGE`, so a
function that builds a codeblock (`{|args| ... }`) is emitted as straight-line
native code instead of falling back, whole-function, to the `hb_vmExecute`
interpreter.

## Context

The straight-line LLVM backend (Plans 1-3, opcode groups A–F) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions using
an opcode outside the subset fall back, whole-function, to the interpreter.
Group G is the seventh of the planned opcode-group extensions.

## What a codeblock opcode is

A codeblock literal `{|x| x + 1}` compiles to a single `HB_P_PUSHBLOCK*`
opcode whose operand is a self-contained pcode buffer (the block body) plus a
parameter count and a table of referenced enclosing-function locals. The body
pcode is laid out *inline*, immediately after the operand header, and is
terminated by `HB_P_ENDBLOCK`.

At run time `HB_P_PUSHBLOCK*` does **not** execute the body — it only
constructs the codeblock value (an `HB_IT_BLOCK` item) and pushes it on the
stack. The body runs later, when the block is `Eval()`-ed, via
`hb_codeblockEval` → `hb_vmExecute`. The outer function's instruction stream
*skips over* the inline body: `hb_vmExecute`'s `HB_P_PUSHBLOCK` case does
`pCode += nSize`, where `nSize` is the whole-instruction length.

### The codeblock opcode set

| Opcode | Emitted by `harbour.exe`? | Group G |
|--------|---------------------------|---------|
| `HB_P_PUSHBLOCK` (89) | yes — codeblock literal | covered |
| `HB_P_PUSHBLOCKSHORT` (90) | yes — small block body | covered |
| `HB_P_PUSHBLOCKLARGE` (161) | yes — large block body | covered |
| `HB_P_ENDBLOCK` (6) | yes — terminates a block body | implicit (see below) |
| `HB_P_MPUSHBLOCK` (59), `HB_P_MPUSHBLOCKLARGE` (159) | **no** — emitted only by the runtime macro compiler, never by `harbour.exe` | out of scope (never reaches the LLVM backend) |
| `HB_P_SEQBLOCK` (178) | yes — but only as the `BEGIN SEQUENCE … RECOVER` handler | out of scope — belongs to group I (SEQUENCE) |

Verified against `src/compiler/genc.c`, `harbour.y`, and `hbmain.c`: the
compiler emits codeblock literals only as `PUSHBLOCK`/`PUSHBLOCKSHORT`/
`PUSHBLOCKLARGE`. `MPUSHBLOCK*` exist only in pcode produced at run time by
the macro compiler (`&(...)`) — that buffer is run by `hb_vmExecute` directly
and never passes through `genllvm.c`. Group G therefore covers **every
codeblock literal a compiled `.prg` can contain**.

`HB_P_ENDBLOCK` is internal to a block body. Because the outer function skips
the whole `PUSHBLOCK*` instruction (variable-length, see Decoder below), the
body opcodes — including the trailing `ENDBLOCK` — are never scanned as
standalone instructions of the outer function. `HB_P_ENDBLOCK` stays
unsupported in the decoder table and that is correct.

## Scope: outer function only

Group G straight-lines the function that *creates* the codeblock. The block
**body** keeps running through `hb_vmExecute` when the block is evaluated —
exactly as the C backend does (the C backend embeds pcode and calls
`hb_vmExecute` for every function and every block body). There is therefore no
behavioral difference and no parity gain to be had from also straight-lining
block bodies; that is explicitly out of scope.

This keeps group G on the same four-part pattern as groups A–F.

## Why group G is (mostly) the simple A–F pattern

`HB_P_PUSHBLOCK*` is variable-length, but the decoder *already* models that:
the three opcodes are `HB_PCK_VARBLOCK` rows in `hb_pcInfo[]`, and
`hb_pcodeInstrLen` already returns the correct whole-instruction length for
`VARBLOCK` (the size operand *is* the total instruction size). The only
decoder change is flipping the `fSupported` flag — no new decode kind, unlike
group F.

Group G touches the usual three places:

1. **`src/compiler/hb_pcdec.c` — decoder.** Flip the `fSupported` flag of the
   `HB_P_PUSHBLOCK` (89), `HB_P_PUSHBLOCKSHORT` (90), and
   `HB_P_PUSHBLOCKLARGE` (161) rows from `HB_FALSE` to `HB_TRUE`. Update the
   file-header "in scope" comment. No length-computation change.
2. **`src/vm/hvm.c` — one shim** `hb_vmsh_pushblock` (see below).
3. **`src/compiler/genllvm.c` — emitter** — a `case` for each of the three
   opcodes that emits the shim call.

## Instruction layout

`HB_P_PUSHBLOCK` = `[opcode][size: 2-byte LE][paramCount: 2][localCount: 2]`
`[localTable: 2·localCount bytes][body pcode … HB_P_ENDBLOCK]`.
`size` is the whole-instruction byte length.

`HB_P_PUSHBLOCKLARGE` is identical with a 3-byte `size`.

`HB_P_PUSHBLOCKSHORT` = `[opcode][size: 1-byte][body pcode … HB_P_ENDBLOCK]` —
no parameter count and no local table (a short block references no enclosing
locals).

The interpreter `case`s pass, respectively, `pCode + 3`, `pCode + 4`, and
`pCode + 2` to the static helpers `hb_vmPushBlock` / `hb_vmPushBlockShort`,
together with the module symbol table `pSymbols` and `nLen = 0` (for
statically-compiled, non-macro code `bDynCode` is false, so the body pcode is
treated as static, not copied).

## The shim — `hb_vmsh_pushblock`

```c
/* Construct a codeblock value from a PUSHBLOCK* instruction and push it on
 * the stack, exactly as the interpreter's HB_P_PUSHBLOCK* cases do.
 * pCode points at the PUSHBLOCK* opcode byte itself; pSymbols is the module
 * symbol table. Runs no user code (the body executes later, on Eval). */
extern HB_EXPORT int hb_vmsh_pushblock( const unsigned char * pCode,
                                        PHB_SYMB pSymbols );
```

The shim reads `pCode[0]` and dispatches to the existing `static` helpers with
the *exact* offsets and `nLen` the interpreter uses:

- `HB_P_PUSHBLOCK` → `hb_vmPushBlock( pCode + 3, pSymbols, 0 )`
- `HB_P_PUSHBLOCKLARGE` → `hb_vmPushBlock( pCode + 4, pSymbols, 0 )`
- `HB_P_PUSHBLOCKSHORT` → `hb_vmPushBlockShort( pCode + 2, pSymbols, 0 )`

It returns `0`. Constructing a codeblock runs no user code and cannot set an
action request, so — like `hb_vmsh_switchidx` — the shim does not return an
action request and the emitter omits the standard action-request check. The
shim is appended to `hvm.c` next to the existing group shims, follows the same
`HB_STACK_TLS_PRELOAD` / `HB_EXPORT` shape, and because it lives in `hvm.c` it
can call the `static` `hb_vmPushBlock` / `hb_vmPushBlockShort`.

### Why this is bit-identical to the interpreter

The shim calls the *same* helpers the interpreter calls, with the same
arguments. `hb_vmPushBlock` reads the parameter count, the referenced-local
table, and detaches the enclosing-function locals via `hb_codeblockNew` — all
of that works against the live stack frame, which the straight-line code sets
up through `hb_vmsh_frame` / `hb_vmsh_pushlocal` using the same `hb_stackItem`
mechanism as the interpreter. Detached locals, parameter counts, nested
codeblocks, and a block stored and evaluated later therefore all behave
identically. The diff-against-the-C-backend test is the per-program proof.

## Two pointer-lifetime requirements

`hb_codeblockNew`, called with `nLen = 0`, stores a *pointer into the body
pcode* (it does not copy it) and the `pSymbols` pointer. Both must stay valid
for the lifetime of the codeblock — potentially the whole program. The
straight-line backend already satisfies both:

1. **Body pcode.** The emitter must pass a pointer *into the function's own
   `@.pcode.<func>` global* (the same `internal constant` byte array the
   fallback path hands to `hb_vmExecute`). It is module-lifetime. The emitter
   passes a `getelementptr` into `@.pcode.<func>` at the `PUSHBLOCK*` opcode
   offset — the same in-place-table technique group F uses for the `SWITCH`
   case table. No separate constant is emitted.
2. **Symbol table.** The straight-line module's `@symbols_table` is an
   `internal global` relocated in place by the module's `@llvm.global_ctors`
   constructor; after the constructor runs it is the live, relocated table.
   The emitter passes `getelementptr @symbols_table, i32 0, i32 0` (the base
   pointer) as `pSymbols`. The implementer MUST confirm, by reading the
   existing `hb_vmsh_pushsymbol` emitter case and the `global_ctors` setup,
   that `@symbols_table` is the correct already-relocated table to hand to a
   codeblock — this is the one real risk in the design.

## Emitter

At an `HB_P_PUSHBLOCK` / `HB_P_PUSHBLOCKSHORT` / `HB_P_PUSHBLOCKLARGE` opcode
the emitter, in the `i<pos>` block:

```
  %r<pos> = call i32 @hb_vmsh_pushblock(
                i8* getelementptr([N x i8], [N x i8]* @.pcode.<func>,
                                  i32 0, i32 <pos>),
                %HB_SYMB* getelementptr([M x %HB_SYMB], [M x %HB_SYMB]*
                                  @symbols_table, i32 0, i32 0))
  br label %<next>
```

`<pos>` is the opcode offset, `<next>` is the fall-through block at
`pos + hb_pcodeInstrLen(&pCode[pos])` — i.e. the instruction *after* the whole
inline block body (or `%epilogue` if that is past the end of pcode, reusing
the existing dangling-label guard). No action-request check, no `switch`, just
the call and an unconditional branch — the simplest emitter case in the
backend.

The `declare i32 @hb_vmsh_pushblock(i8*, %HB_SYMB*)` line is added to the
module-header shim-declaration block next to the group F declaration.

Pass B (`hb_llvmSLEmitStrings`) needs no change: it walks instructions with
`hb_pcodeInstrLen`, so a `PUSHBLOCK*` advances it past the whole body in one
step — string literals *inside* a block body are not emitted as `@.sl.str`
globals (they stay inline in `@.pcode.<func>` and are read inline by
`hb_vmExecute` when the body runs, exactly as in the fallback path).

## Correctness

- The shim calls the same `hb_vmPushBlock` / `hb_vmPushBlockShort` the
  interpreter calls, with identical arguments — codeblock construction is
  bit-identical.
- The block body is unchanged pcode inside `@.pcode.<func>`; evaluating the
  block runs that pcode through `hb_vmExecute`, exactly as the C backend does.
- The decoder already computes the correct whole-instruction length for
  `VARBLOCK`, so the straight-line walk of the rest of the outer function
  stays synchronised across the inline body.
- A function mixing a codeblock with an opcode still outside the A–G subset
  (e.g. `&(...)` macros, `BEGIN SEQUENCE`) still falls back whole-function and
  stays correct.

## Testing

- New `tests/llvm/codeblock.prg` exercising the compatibility-critical cases:
  a parameterless block; a block with parameters invoked via `Eval()`; a block
  that captures an enclosing-function local (detached local); nested
  codeblocks (a block inside a block); and a block stored in a variable and
  evaluated later. Results printed.
- Built with `-GL` and with the C backend; outputs must be byte-identical
  (`tests/llvm/run.sh`), and the program must straight-line (no
  `call void @hb_vmExecute` in `HB_FUN_MAIN`). `codeblock` is added to
  `run.sh`'s straight-line-required set.

## Out of scope

- Straight-lining codeblock **bodies** — they run through `hb_vmExecute`, as in
  the C backend; no behavioral difference.
- `HB_P_MPUSHBLOCK` / `HB_P_MPUSHBLOCKLARGE` — produced only by the runtime
  macro compiler, never by `harbour.exe`; they never reach the LLVM backend.
- `HB_P_SEQBLOCK` — a codeblock push, but inseparable from the SEQUENCE
  opcodes (`SEQBEGIN` / `SEQEND` / `SEQRECOVER`); it belongs to group I.
- Opcode groups H (macros) and I (SEQUENCE) get their own later specs.
