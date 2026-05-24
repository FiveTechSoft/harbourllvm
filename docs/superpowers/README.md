# Harbour LLVM backend — specs and plans

This directory holds the design specifications (`specs/`) and step-by-step
implementation plans (`plans/`) for every piece of work in the Harbour LLVM
backend. Each unit was brainstormed into a spec, turned into a plan, then
executed task-by-task via subagent-driven development with per-task spec and
code-quality review.

Each row below points to the spec + the plan; the spec is the **what**, the
plan is the **how**. All units listed are complete and on `master`.

## Foundation — Plans 1-3

| # | Spec | Plan |
|---|------|------|
| 1 — IR text emitter | [`specs/2026-05-16-harbour-llvm-backend-design.md`](specs/2026-05-16-harbour-llvm-backend-design.md) | [`plans/2026-05-16-harbour-llvm-plan1-ir-emitter.md`](plans/2026-05-16-harbour-llvm-plan1-ir-emitter.md) |
| 2 — Embed libLLVM + LLD | (same spec) | [`plans/2026-05-17-harbour-llvm-plan2-embed-llvm.md`](plans/2026-05-17-harbour-llvm-plan2-embed-llvm.md) |
| 3 — Unroll pcode to IR | (same spec) | [`plans/2026-05-20-harbour-llvm-plan3-straightline-ir.md`](plans/2026-05-20-harbour-llvm-plan3-straightline-ir.md) |

After Plan 3 the straight-line backend covers a baseline pcode subset
(locals, arithmetic, comparisons, logical ops, jumps, function calls,
return). The opcode-group extensions below extend that subset
one category at a time.

## Opcode-group extensions — Groups A-I

| Group | Topic | Spec | Plan |
|-------|-------|------|------|
| A | FOR loops + compound assignment | [spec](specs/2026-05-21-opcode-group-a-design.md) | [plan](plans/2026-05-21-opcode-group-a-plan.md) |
| B | Arrays + hashes | [spec](specs/2026-05-21-opcode-group-b-design.md) | [plan](plans/2026-05-21-opcode-group-b-plan.md) |
| C | RDD fields, memvars, aliases | [spec](specs/2026-05-21-opcode-group-c-design.md) | [plan](plans/2026-05-21-opcode-group-c-plan.md) |
| D | OOP messages | [spec](specs/2026-05-21-opcode-group-d-design.md) | [plan](plans/2026-05-21-opcode-group-d-plan.md) |
| E | FOR EACH | [spec](specs/2026-05-22-opcode-group-e-design.md) | [plan](plans/2026-05-22-opcode-group-e-plan.md) |
| F | SWITCH | [spec](specs/2026-05-22-opcode-group-f-design.md) | [plan](plans/2026-05-22-opcode-group-f-plan.md) |
| G | Codeblocks | [spec](specs/2026-05-22-opcode-group-g-design.md) | [plan](plans/2026-05-22-opcode-group-g-plan.md) |
| H | Macros (`&var` / `&("expr")` / `text…endtext` / `@&var`) | [spec](specs/2026-05-23-opcode-group-h-design.md) | [plan](plans/2026-05-23-opcode-group-h-plan.md) |
| I | SEQUENCE (`BEGIN SEQUENCE … RECOVER/ALWAYS … END`) | [spec](specs/2026-05-23-opcode-group-i-design.md) | [plan](plans/2026-05-23-opcode-group-i-plan.md) |

## Permanent-fallback opcodes

These opcodes are deliberately left out of the straight-line subset. Functions
using them fall back, whole-function, to the `hb_vmExecute` interpreter —
correct behavior, but no LLVM straight-lining. They were never part of the
nine-group decomposition; the fallback path is permanent infrastructure.

| Opcode | xBase construct |
|--------|----------------|
| `HB_P_PUSHTIMESTAMP` | timestamp literal `{^ … }` |
| `HB_P_INSTRING` | `$` substring test |
| `HB_P_PUSHDATE` | date literal `{… - … - …}` (some forms) |
| `HB_P_SFRAME` / `HB_P_STATICS` | static variable frames |
| `HB_P_VFRAME` / `HB_P_LARGEFRAME` / `HB_P_LARGEVFRAME` | variable-argument frames |
| `HB_P_PUSHSTRHIDDEN` | obfuscated string literals |
| `HB_P_THREADSTATICS` | thread-local statics |
| `HB_P_PARAMETER` | extended parameter setup |
| `HB_P_SWAP`, `HB_P_PUSHVPARAMS` | edge-case stack ops |
| `HB_P_M*` family | runtime macro-compiler-only pcode (never emitted by `harbour.exe`) |

The `tests/llvm/fallback.prg` sentinel exercises one of these (currently
`HB_P_PUSHTIMESTAMP`) to keep `tests/llvm/run.sh` honest — if a future change
accidentally straight-lines it, the sentinel test fails.

## Workflow

Every unit followed the same loop:

1. **Brainstorm** the spec — one or two clarifying questions, scope check,
   design with explicit risks.
2. **Write the plan** — bite-sized tasks, full code in each step, exact
   commands and expected output, frequent commits.
3. **Execute subagent-driven** — fresh subagent per task, two-stage review
   (spec compliance, then code quality) between tasks, fixes folded back
   into the same task.
4. **Verify against the C backend** — `tests/llvm/run.sh` compiles every
   `tests/llvm/*.prg` with both backends and diffs the output. Any
   difference is a hard failure.
5. **Mark done** — README group row + memory pointer.
