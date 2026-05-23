// fallback.prg — exercises the interpreter fallback path.
// Uses a static variable (HB_P_STATICNAME / HB_P_PUSHSTATIC / HB_P_POPSTATIC)
// which are not yet straight-lined, so HB_FUN_MAIN must still route through
// hb_vmExecute.
function Main()
   static nCount := 0
   nCount := nCount + 1
   ? nCount
   return nil
