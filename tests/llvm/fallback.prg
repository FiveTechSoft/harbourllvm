//
// Sentinel: this program MUST fall back to hb_vmExecute. Uses a timestamp
// literal {^ ... }, which emits HB_P_PUSHTIMESTAMP (opcode 22, HB_FALSE in
// the straight-line decoder), forcing whole-function fallback. Distinct
// from statics.prg (HB_P_SFRAME) so the sentinels exercise different
// unsupported paths.
//
function Main()
   local dStamp := {^ 2026-01-01 12:00:00 }
   ? hb_TToS( dStamp )
   return nil
