//
// Sentinel: this program MUST fall back to hb_vmExecute. Uses BEGIN SEQUENCE,
// which emits HB_P_SEQBEGIN (113) and HB_P_SEQEND (114) — both HB_FALSE in
// the straight-line decoder, forcing the whole function to the interpreter.
// Distinct from statics.prg (which falls back via HB_P_SFRAME) so the two
// sentinels exercise different unsupported paths.
//
function Main()
   local nResult := 0
   begin sequence
      nResult := 42
   end sequence
   ? nResult
   return nil
