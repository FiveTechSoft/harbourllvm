# Opcode Subset Extension — Group I: SEQUENCE

Date: 2026-05-23
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover all 7 SEQUENCE opcodes, so a
function using `BEGIN SEQUENCE … RECOVER … END SEQUENCE` (try/catch) or
`BEGIN SEQUENCE … ALWAYS … END SEQUENCE` (try/finally) emits straight-line
native code instead of falling back, whole-function, to the `hb_vmExecute`
interpreter. Group I is the last of the planned opcode-group extensions —
opcode groups A–H + I cover every straight-line subset originally scoped.

## Context

The straight-line LLVM backend (Plans 1-3, opcode groups A–H) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions using
an opcode outside the subset fall back, whole-function, to the interpreter.
Even after group I many opcodes remain unsupported (PUSHTIMESTAMP, INSTRING,
PUSHDATE, SFRAME, STATICS, VFRAME, LARGEFRAME, PUSHSTRHIDDEN, THREADSTATICS,
PARAMETER, …) — the fallback path is permanent.

## Why SEQUENCE is harder than groups A–H

Groups A–H translate each opcode to a shim call followed by the standard
action-request check (`%c = icmp ne i32 %r, 0; br i1 %c, label %epilogue,
label %<next>`). A non-zero action request always means "unwind to function
exit". For SEQUENCE that is wrong — a `HB_BREAK_REQUESTED` inside an active
`BEGIN SEQUENCE` region must instead route to the matching `RECOVER` block;
a `HB_QUIT_REQUESTED` inside an active `ALWAYS` region must route to the
`ALWAYS` handler. The dispatch is region-aware.

The Harbour interpreter handles this with a runtime `bCanRecover` flag and
a sequence stack of `HB_IT_RECOVER` envelopes — on every iteration of its
opcode loop, it checks the action request and rewrites `pCode` to the recover
address when appropriate. Straight-line LLVM code cannot rewrite an
instruction pointer mid-function. Group I solves this with two cooperating
mechanisms:

1. **Compile-time region tracking** in the emitter — `hb_llvmSLEmitBody`
   walks pcode opcode-by-opcode and maintains a stack of active SEQUENCE
   regions (`{ region_id, recover_pos, always_pos, is_always }`). Pushed on
   `SEQBEGIN`/`SEQALWAYS`, popped on `SEQEND`/`ALWAYSEND`.

2. **Region-aware per-shim dispatch** — shims emitted *inside* an active
   region branch on non-zero action request to a per-shim dispatch block that
   distinguishes `HB_BREAK_REQUESTED` from `HB_QUIT_REQUESTED` and routes to
   the correct in-function recover/always label. Outside any region, the
   existing `br i1 ... epilogue` shape is unchanged — no overhead.

The runtime envelope is *also* pushed (matching the interpreter byte-for-byte)
so `hb_vmRequestBreak` and the error subsystem, which walk the sequence stack,
see the same `HB_IT_RECOVER` items they always see — cross-language API
compatibility is preserved.

## The SEQUENCE opcode set

| # | Opcode | Bytes | Role |
|---|--------|-------|------|
| 113 | `HB_P_SEQBEGIN` | 4 | start try/catch — operand = signed 3-byte offset from SEQBEGIN to RECOVER |
| 114 | `HB_P_SEQEND` | 4 | end try body — operand = signed 3-byte offset from SEQEND past the recover handler (normal-exit branch target) |
| 115 | `HB_P_SEQRECOVER` | 1 | start of recover handler body — pops envelope, leaves break value on stack |
| 166 | `HB_P_SEQALWAYS` | 4 | start try/finally — operand = signed 3-byte offset from SEQALWAYS to ALWAYSEND |
| 167 | `HB_P_ALWAYSBEGIN` | 4 | start of always-handler body — operand = signed 3-byte offset from ALWAYSBEGIN to ALWAYSEND |
| 168 | `HB_P_ALWAYSEND` | 1 | end of always-handler — pops envelope, restores pending action request |
| 178 | `HB_P_SEQBLOCK` | 1 | push a codeblock-form recover (`BEGIN SEQUENCE WITH bBlock`) — forwards to `hb_vmSeqBlock` |

All 7 are currently `HB_FALSE` in `hb_pcInfo[]` with correct `HB_PCK_FIXED`
lengths — only the `fSupported` flag flips. No new decode kind, no length-
computation change.

Action request constants (`include/hbvm.h`): `HB_QUIT_REQUESTED = 1`,
`HB_BREAK_REQUESTED = 2`, `HB_ENDPROC_REQUESTED = 4`. Envelope flags
(`src/vm/hvm.c`): `HB_SEQ_CANRECOVER = 64`, `HB_SEQ_DOALWAYS = 128`.
Stack offsets relative to top of stack: `HB_RECOVER_STATE = -1` (envelope),
`HB_RECOVER_VALUE = -2` (break-value slot).

## Group I touches the usual three places + region tracking

1. **`src/compiler/hb_pcdec.c` — decoder.** Flip `fSupported` HB_FALSE → HB_TRUE
   for the 7 rows. Update the file-header "in scope" comment. NO length-
   computation change.
2. **`src/vm/hvm.c` — 7 shims.** Each shim is a 5–20 line wrapper around the
   exact interpreter case body, calling the same `hb_*` helpers with the same
   arguments. Returns `int` — 0 if the dispatch should fall through, or an
   action-request-like discriminator value that the emitter uses to route.
3. **`src/compiler/genllvm.c` — emitter — two changes:**
   - Add region tracking to `hb_llvmSLEmitBody`.
   - When inside an active region, modify the per-shim action-request check
     from "br to %epilogue on non-zero" to "br to a per-shim dispatch block".
   - Add 7 emitter `case` arms.

## The shims — `hb_vmsh_seq*`

**`hb_vmsh_seqbegin( const unsigned char * pRecoverAddr )`** — pushes the
`HB_IT_RECOVER` envelope exactly as the interpreter's `HB_P_SEQBEGIN` case
does (lines 2040–2080 of `hvm.c`): allocates the break-value slot, allocates
the recover envelope, sets `pItem->item.asRecover.recover = pRecoverAddr`
(a pointer into `@.pcode.<func>` at the SEQRECOVER opcode — used only for
cross-language compatibility, not for actual control flow in straight-line),
`base = hb_stackGetRecoverBase()`, `flags = (bCanRecover ? HB_SEQ_CANRECOVER : 0)`,
`request = 0`. Sets new recover base via `hb_stackSetRecoverBase`. Returns 0.

**`hb_vmsh_seqalways( const unsigned char * pAlwaysAddr )`** — same as
`hb_vmsh_seqbegin`, but `flags |= HB_SEQ_DOALWAYS` (mirroring interpreter
`HB_P_SEQALWAYS` case lines 1941–1981). The first envelope address argument
is the eventual ALWAYSBEGIN block (or ALWAYSEND — interpreter stores the
RECOVER/END address).

**`hb_vmsh_seqend()`** — pops the envelope and break-value slot (mirroring
interpreter `HB_P_SEQEND` lines 2083–2109): reads previous `bCanRecover` from
envelope `flags & HB_SEQ_CANRECOVER`, restores recover base, `hb_stackDec()`,
`hb_stackPop()`. Returns 0.

**`hb_vmsh_seqrecover()`** — mirrors interpreter `HB_P_SEQRECOVER` lines
2111–2129: restores recover base from envelope, `hb_stackDec()`. Leaves the
break value on stack (next opcode pops it). Returns 0.

**`hb_vmsh_alwaysbegin( const unsigned char * pAlwaysEndAddr )`** — mirrors
interpreter `HB_P_ALWAYSBEGIN` lines 1984–2000: updates the topmost envelope's
`recover` to `pAlwaysEndAddr`, folds the current `request` into `flags`,
clears `request`, and if `HB_ENDPROC_REQUESTED` was set, moves the return
value into the break-value slot. Returns 0.

**`hb_vmsh_alwaysend()`** — mirrors interpreter `HB_P_ALWAYSEND` lines
2002+: computes the combined `uiPrevAction | uiCurrAction` from envelope
state, sets the action request to the highest-priority pending action (QUIT
> BREAK > ENDPROC > 0), restores `bCanRecover` and recover base, pops the
envelope, restores the return value if appropriate. Returns the action
request the emitter should see (so the emitter can re-dispatch).

**`hb_vmsh_seqblock()`** — one-liner forwarding to the existing `static`
`hb_vmSeqBlock()` (line 107 declaration; mirrors interpreter line 1936–1939).
Returns 0.

Each shim begins with `HB_STACK_TLS_PRELOAD` (every shim touches the eval
stack). Each lives in `hvm.c` so it can call the `static` helpers
(`hb_stackGetRecoverBase`, `hb_stackSetRecoverBase`, `hb_vmSeqBlock`).

## Emitter — compile-time region tracking + region-aware dispatch

`hb_llvmSLEmitBody` adds a region stack (local C array, fixed small size —
SEQUENCE nesting deeper than, say, 16 levels is unrealistic in real code;
overflow falls back the whole function for safety):

```c
struct seq_region {
   HB_SIZE  recover_pos;     /* in-function pcode offset of SEQRECOVER (or ALWAYSEND for ALWAYS) */
   HB_BOOL  is_always;
};
struct seq_region seq_stack[ 16 ];
int seq_depth = 0;
```

The standard per-shim emission (`HB_EMIT_NOARG_SHIM`, `HB_EMIT_INT1_SHIM`,
the inlined cases) currently ends with `br i1 %c<pos>, label %epilogue,
label %<next>`. When `seq_depth > 0`, that final `br` instead targets a
per-shim dispatch block:

```
  %r<pos>  = call i32 @hb_vmsh_xxx(...)
  %c<pos>  = icmp ne i32 %r<pos>, 0
  br i1 %c<pos>, label %seqd<pos>, label %<next>
seqd<pos>:
  ; route on action request value
  %brk<pos> = icmp eq i32 %r<pos>, 2     ; HB_BREAK_REQUESTED
  br i1 %brk<pos>, label %seqr_<top_region_id>, label %seqd_q<pos>
seqd_q<pos>:
  %qit<pos> = icmp eq i32 %r<pos>, 1     ; HB_QUIT_REQUESTED
  br i1 %qit<pos>, label %seqa_<topmost_always_region>, label %epilogue
```

If no enclosing region is an ALWAYS, the QUIT path collapses directly to
`%epilogue`. If no enclosing region is a non-ALWAYS RECOVER, the BREAK path
collapses to the topmost ALWAYS or to `%epilogue`. The emitter decides the
exact targets from its `seq_stack`.

The `%seqr_<region>` label (emitted once per SEQBEGIN region, before the
recover body's first instruction) is a small block that branches
unconditionally to `%i<recover_pos>` — the natural per-instruction block
the emitter creates for the SEQRECOVER opcode. The action-request clearing
and break-value push happen in the SEQRECOVER opcode shim itself (matching
interpreter behavior).

This approach uses an existing pattern from group F (the `HB_P_SWITCH`
emitter's `switch i32 %r<pos>, label %<fallthrough> [...]`) — a per-shim
dispatch is exactly the same shape, just smaller (2 cases instead of N).

To keep the change minimal, the emitter encapsulates the dispatch in two
new local macros placed alongside `HB_EMIT_NOARG_SHIM` /
`HB_EMIT_INT1_SHIM`:

- `HB_EMIT_NOARG_SHIM_SEQ( nm )` — like `HB_EMIT_NOARG_SHIM` but with the
  seq-region dispatch shape.
- `HB_EMIT_INT1_SHIM_SEQ( nm, val )` — like `HB_EMIT_INT1_SHIM` likewise.

At emission time, the code chooses between the regular and the `_SEQ`
variant based on `seq_depth > 0`. This minimises the diff to the existing
emitter cases — they become two-line `if/else` between the regular and SEQ
form.

## The 7 emitter `case` arms

- **`HB_P_SEQBEGIN`** — compute `recover_pos = pos + HB_PCODE_MKINT24(&pCode[pos+1])`.
  Push `{ recover_pos, is_always = HB_FALSE }` onto `seq_stack`. Emit
  `call i32 @hb_vmsh_seqbegin(i8* getelementptr([N x i8], [N x i8]* @.pcode.<func>, i32 0, i32 <recover_pos>))`,
  standard action check (this shim returns 0 so the check is trivial),
  branch to `%<next>`.
- **`HB_P_SEQEND`** — pop region. Emit `call i32 @hb_vmsh_seqend()`. Then
  compute `end_pos = pos + HB_PCODE_MKINT24(&pCode[pos+1])` and emit
  `br label %i<end_pos>` (unconditional — normal-exit skip past the recover
  handler; matching interpreter behavior).
- **`HB_P_SEQRECOVER`** — emit `call i32 @hb_vmsh_seqrecover()`, branch to
  `%<next>` (the recover body's first real instruction).
- **`HB_P_SEQALWAYS`** — like SEQBEGIN but call `hb_vmsh_seqalways`; push
  `{ recover_pos, is_always = HB_TRUE }`.
- **`HB_P_ALWAYSBEGIN`** — compute `always_end_pos = pos + HB_PCODE_MKINT24(&pCode[pos+1])`.
  Emit `call i32 @hb_vmsh_alwaysbegin(i8* gep ...pcode... <always_end_pos>)`,
  branch to `%<next>`.
- **`HB_P_ALWAYSEND`** — pop region. Emit `call i32 @hb_vmsh_alwaysend()`,
  capture the returned action request in `%r<pos>`, then perform the standard
  action-request check at the OUTER region (re-dispatch into whatever
  enclosing region the popped region was inside; if outermost, branch to
  `%epilogue` on non-zero or `%<next>` on zero).
- **`HB_P_SEQBLOCK`** — emit via `HB_EMIT_NOARG_SHIM( "seqblock" )` (regular
  no-arg, no SEQ dispatch needed — SEQBLOCK just pushes a value, doesn't open
  a region).

7 IR `declare` lines added to the module-header shim-declaration block.

## Correctness

- Each shim calls the same `hb_*` helpers the interpreter calls, with
  identical arguments — envelope state is bit-identical to interpreter.
- `hb_vmRequestBreak` still sees the topmost envelope (same `HB_IT_RECOVER`
  layout) and writes the break value into the same slot.
- Cross-function unwind: a BREAK raised in a called function returns
  `HB_BREAK_REQUESTED` from the callee's shim → the caller's per-shim check
  catches it. If the caller is in a SEQUENCE region, the region-aware
  dispatch routes to the recover block. Otherwise the existing
  `br i1 ... epilogue` shape propagates the request to the caller's caller.
- Nested SEQUENCEs work because the region stack tracks depth at compile
  time and the runtime envelope chain tracks depth at run time. They
  remain consistent because every SEQBEGIN/END pair both runs the shim
  (env push/pop) AND updates the compile-time stack.
- The decoder length stays at the existing `HB_PCK_FIXED` values; the
  straight-line walk of the rest of the function stays synchronised.

## Sentinel break — fallback.prg must be repointed

`tests/llvm/fallback.prg` currently uses `BEGIN SEQUENCE … END SEQUENCE` as
its "guaranteed to fall back" sentinel (set in group G to be distinct from
`statics.prg`'s `HB_P_SFRAME` fallback path). Group I makes BEGIN SEQUENCE
straight-line, so `fallback.prg` would no longer fall back — the sentinel
test would fail.

Repoint `fallback.prg` to `HB_P_PUSHTIMESTAMP` (timestamp literal
`{^ 2026-01-01 12:00:00 }`). It is a single 1-opcode fixed-9-byte instruction
that is HB_FALSE in the decoder, simple syntax, no other group dependencies,
not on any planned-future-group roadmap. After group I, the still-unsupported
set includes PUSHTIMESTAMP, INSTRING, PUSHDATE, SFRAME, STATICS,
VFRAME/LARGEFRAME, PUSHSTRHIDDEN, THREADSTATICS, PARAMETER, M*-runtime-only —
all genuine permanent-fallback opcodes.

## Testing

- New `tests/llvm/sequence.prg` exercising the compatibility-critical cases
  in separate functions:
  - **Try/catch normal completion** — `BEGIN SEQUENCE / op / END SEQUENCE`
    with no BREAK; should run try body, skip recover, continue.
  - **Try/catch with BREAK** — `BREAK` raised inside try, recover catches
    and prints something; verify recover body runs.
  - **Cross-function unwind** — caller has `BEGIN SEQUENCE`, callee does
    `BREAK`; verify outer caller's recover catches.
  - **Nested SEQUENCE** — inner try/catch inside outer try/catch; verify
    inner catches without disturbing outer.
  - **Try/finally normal** — `BEGIN SEQUENCE / op / ALWAYS / always-body /
    END SEQUENCE`; verify always runs after normal completion.
  - **Try/finally with BREAK** — BREAK in try, no RECOVER, only ALWAYS;
    verify always runs THEN BREAK propagates (or is swallowed per Harbour
    semantics — corpus matches whatever the C backend produces).
  - **SEQBLOCK form** — `BEGIN SEQUENCE WITH {|oErr| ... }` (if Harbour
    supports it); exercise `HB_P_SEQBLOCK`.
- Built with `-GL` and with the C backend; outputs must be byte-identical
  (`tests/llvm/run.sh`); each per-function fallback count must be 0;
  per-opcode grep counts ≥ 1 for `seqbegin`, `seqend`, `seqrecover`,
  `seqalways`, `alwaysbegin`, `alwaysend`, `seqblock`. `sequence` is added
  to `run.sh`'s straight-line-required set.

## Safety hatch

If any corpus diff fails or unwind semantics drift from the interpreter,
revert just the 7 `fSupported` flips. The shims and emitter cases stay in
the codebase (dead but harmless). SEQUENCE-using functions immediately
return to the fallback path → identical to today's behavior. No behavior
regression.

## Out of scope

- `HB_P_M*` macro-runtime-only opcodes — same exclusion as groups G/H.
- The not-yet-supported permanent-fallback opcodes (PUSHTIMESTAMP, INSTRING,
  PUSHDATE, SFRAME, STATICS, VFRAME, LARGEFRAME, PUSHSTRHIDDEN,
  THREADSTATICS, PARAMETER) stay HB_FALSE. The fallback path is permanent.
- Group I is the last planned opcode-group extension. After group I the
  straight-line subset covers every opcode the project originally scoped
  (the original 9-group decomposition from the project README).
