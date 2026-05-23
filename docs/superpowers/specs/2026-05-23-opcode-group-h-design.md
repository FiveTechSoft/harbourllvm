# Opcode Subset Extension — Group H: Macros

Date: 2026-05-23
Status: Approved design

## Goal

Extend the straight-line LLVM backend to cover every compiler-emitted macro
opcode, so a function using `&var` (value substitution), `&("expr")`
(runtime-compiled call or assignment), `text &x endtext` (text substitution),
or `@&var` (macro reference) emits straight-line native code instead of
falling back, whole-function, to the `hb_vmExecute` interpreter.

## Context

The straight-line LLVM backend (Plans 1-3, opcode groups A–G) emits one
`hb_vmsh_*` shim call per pcode opcode for a supported subset; functions
using an opcode outside the subset fall back, whole-function, to the
interpreter. Group H is the eighth of the planned opcode-group extensions.

The runtime macro compiler (`libhbmacro.a`) is already linked into the
straight-line build path, so group H adds no new dependency.

## The macro opcode set

Group H covers all 14 compiler-emitted macro opcodes. The runtime macro
compiler also emits the `HB_P_M*` family (`MPUSHBLOCK`, `MPUSHSTR`,
`MPUSHBLOCKLARGE`, `MPUSHSTRLARGE`), but those appear only in pcode
generated at run time and never pass through `genllvm.c` — same exclusion
as group G's `MPUSHBLOCK*`.

| # | Opcode | Bytes | Interpreter call |
|---|--------|-------|------------------|
| 38 | `HB_P_MACROPOP` | 2 | `hb_macroSetValue( top(-1), pCode[1] )` |
| 39 | `HB_P_MACROPOPALIASED` | 2 | `hb_macroPopAliasedValue( top(-2), top(-1), pCode[1] )` |
| 40 | `HB_P_MACROPUSH` | 2 | `hb_macroGetValue( top(-1), 0, pCode[1] )` |
| 41 | `HB_P_MACROARRAYGEN` | 3 | `hb_vmMacroArrayGen( MKUSHORT(&pCode[1]) )` |
| 42 | `HB_P_MACROPUSHLIST` | 2 | `hb_macroGetValue( top(-1), HB_P_MACROPUSHLIST, pCode[1] )` |
| 43 | `HB_P_MACROPUSHINDEX` | 1 | `hb_vmMacroPushIndex()` |
| 44 | `HB_P_MACROPUSHPARE` | 2 | `hb_macroGetValue( top(-1), HB_P_MACROPUSHPARE, pCode[1] )` |
| 45 | `HB_P_MACROPUSHALIASED` | 2 | `hb_macroPushAliasedValue( top(-2), top(-1), pCode[1] )` |
| 46 | `HB_P_MACROSYMBOL` | 1 | `hb_macroPushSymbol( top(-1) )` |
| 47 | `HB_P_MACROTEXT` | 1 | `hb_macroTextValue( top(-1) )` |
| 123 | `HB_P_MACROFUNC` | 3 | `hb_vmMacroFunc( MKUSHORT(&pCode[1]) )` |
| 124 | `HB_P_MACRODO` | 3 | `hb_vmMacroDo( MKUSHORT(&pCode[1]) )` |
| 127 | `HB_P_MACROPUSHREF` | 1 | `hb_macroPushReference( top(-1) )` |
| 146 | `HB_P_MACROSEND` | 3 | `hb_vmMacroSend( MKUSHORT(&pCode[1]) )` |

The decoder rows are already `HB_PCK_FIXED` with correct `nLen` for all 14 —
only the `fSupported` flag flips. No new decode kind, no length-computation
change.

## Group H is the simple A–G pattern, scaled to 14 opcodes

Group H touches the usual three places:

1. **`src/compiler/hb_pcdec.c` — decoder.** Flip `fSupported` to `HB_TRUE`
   for the 14 rows. Update the file-header "in scope" comment.
2. **`src/vm/hvm.c` — 14 shims** in three operand-shape families
   (see below). Each shim is a 1–3 line wrapper around the existing helper.
3. **`src/compiler/genllvm.c` — emitter** — 14 `declare` lines and 14 `case`
   arms (most reuse the existing `HB_EMIT_NOARG_SHIM` macro; the others are
   small fixed-shape `fprintf` blocks with the standard action-request check).

## The shims — `hb_vmsh_macro*`

The 14 shims group by operand shape:

**No-operand (4 shims):**

```c
extern HB_EXPORT int hb_vmsh_macropushindex( void );
extern HB_EXPORT int hb_vmsh_macropushref  ( void );
extern HB_EXPORT int hb_vmsh_macrosymbol   ( void );
extern HB_EXPORT int hb_vmsh_macrotext     ( void );
```

**1-byte flag operand (6 shims):**

```c
extern HB_EXPORT int hb_vmsh_macropop         ( int flag );
extern HB_EXPORT int hb_vmsh_macropopaliased  ( int flag );
extern HB_EXPORT int hb_vmsh_macropush        ( int flag );
extern HB_EXPORT int hb_vmsh_macropushlist    ( int flag );
extern HB_EXPORT int hb_vmsh_macropushpare    ( int flag );
extern HB_EXPORT int hb_vmsh_macropushaliased ( int flag );
```

**2-byte MKUSHORT operand (4 shims):**

```c
extern HB_EXPORT int hb_vmsh_macroarraygen ( int usFlags );
extern HB_EXPORT int hb_vmsh_macrodo       ( int usParams );
extern HB_EXPORT int hb_vmsh_macrofunc     ( int usParams );
extern HB_EXPORT int hb_vmsh_macrosend     ( int usParams );
```

Each shim body is the same shape (example — `hb_vmsh_macropush`):

```c
HB_EXPORT int hb_vmsh_macropush( int flag )
{
   HB_STACK_TLS_PRELOAD
   hb_macroGetValue( hb_stackItemFromTop( -1 ), 0, ( HB_BYTE ) flag );
   return ( int ) hb_stackGetActionRequest();
}
```

`HB_STACK_TLS_PRELOAD` is required for every shim — all 14 either call
`hb_stackItemFromTop` directly or invoke a helper that uses the TLS stack
inside its own stack-aware code. Returning `hb_stackGetActionRequest()`
matches the action-request convention every other shim follows (groups A–E),
because macro code IS user code — it can raise errors (undefined variable,
parse error in the macro string, type mismatch in the compiled expression)
that the interpreter would honor as an action request.

`hb_macro*` symbols are exported from `libhbmacro.a`. `hb_vmMacro*`
(`hb_vmMacroFunc`, `hb_vmMacroDo`, `hb_vmMacroSend`, `hb_vmMacroPushIndex`,
`hb_vmMacroArrayGen`) are `static` inside `hvm.c` — reachable from the shim
block because it lives in the same translation unit.

## Emitter

Each opcode gets one `case` in `hb_llvmSLEmitBody`. The no-operand four use
the existing `HB_EMIT_NOARG_SHIM( name )` macro. The 1-byte and 2-byte
variants each emit:

```
  %r<pos> = call i32 @hb_vmsh_macro<x>(i32 <flag>)
  %c<pos> = icmp ne i32 %r<pos>, 0
  br i1 %c<pos>, label %epilogue, label %<next>
```

— identical in shape to existing single-int-arg shim emissions (e.g. the
group A `LOCALADDINT` case or the group B `ARRAYDIM` case). The 1-byte
variants read `pCode[pos+1]` as a `(unsigned) HB_BYTE`; the 2-byte variants
use `HB_PCODE_MKUSHORT( &pCode[pos+1] )`.

The 14 `declare` lines are added to `hb_compGenLLVMCode`'s module-header
shim-declaration block, next to the group G declaration.

## Correctness

- Each shim calls the same helper the interpreter calls, with identical
  arguments — macro execution is bit-identical.
- `hb_stackGetActionRequest()` propagates errors from the runtime macro
  compiler (parse errors, undefined-variable substitutions, etc.) so the
  straight-line code unwinds to its `epilogue` exactly as the interpreter
  would unwind to its post-case action-request check.
- The decoder length stays at the existing `HB_PCK_FIXED` values; the
  straight-line walk of the rest of the function stays synchronised.
- A function mixing a macro with an opcode still outside the A–H subset
  still falls back whole-function and stays correct.
- `pSymbols` is not passed to any macro shim — macro compilation queries the
  module's symbol table through global TLS (the same path the interpreter
  uses), so no `@symbols_table` GEP is needed for group H.

## Testing

- New `tests/llvm/macro.prg` exercising the major forms in distinct functions
  so each is reachable as a separate straight-lined function and the diff
  catches any per-opcode discrepancy:
  - `&var` read (`HB_P_MACROPUSH`),
  - `&var := x` write (`HB_P_MACROPOP`),
  - `&("Foo()")` function call (`HB_P_MACROFUNC`),
  - `&("Foo")` invoked via `DO` (`HB_P_MACRODO`),
  - `text &x endtext` text substitution (`HB_P_MACROTEXT`),
  - `@&var` reference (`HB_P_MACROPUSHREF`),
  - an aliased macro (`area->&fld`, `HB_P_MACROPUSHALIASED`).
- Built with `-GL` and with the C backend; outputs must be byte-identical
  (`tests/llvm/run.sh`). The functions containing the macros must
  straight-line (no `call void @hb_vmExecute` inside them). `macro` is added
  to `run.sh`'s straight-line-required set.

## Out of scope

- `HB_P_MPUSHBLOCK` / `HB_P_MPUSHSTR` / `HB_P_MPUSHBLOCKLARGE` /
  `HB_P_MPUSHSTRLARGE` — produced only by the runtime macro compiler, never
  by `harbour.exe`; they never reach the LLVM backend.
- Straight-lining the body of a macro-compiled expression — the runtime
  macro compiler still produces interpreter pcode (the `HB_P_M*` family)
  which `hb_vmExecute` runs; identical to the C backend.
- Opcode group I (SEQUENCE) gets its own later spec.
