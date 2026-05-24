//
// Group I corpus — SEQUENCE opcodes straight-lined by the LLVM backend.
//
// Exercises the compatibility-critical paths in distinct functions:
//
//   TryNormal      try body completes normally, no BREAK
//   TryCatch       BREAK in try body, RECOVER catches
//   CrossFn        caller has SEQUENCE, callee does BREAK
//   Nested         inner try/catch inside outer try/catch
//   TryFinallyOk   ALWAYS runs after normal completion
//   TryFinallyBrk  BREAK in try, ALWAYS runs, no inner RECOVER, outer SEQUENCE catches
//

function Main()
   TryNormal()
   TryCatch()
   CrossFn()
   Nested()
   TryFinallyOk()
   TryFinallyBrk()
   ? "main done"
   return nil

function TryNormal()
   begin sequence
      ? "try-normal"
   end sequence
   ? "after-normal"
   return nil

function TryCatch()
   begin sequence
      ? "try-catch-pre"
      break
      ? "unreachable"
   recover
      ? "caught"
   end sequence
   ? "after-catch"
   return nil

function CrossFn()
   begin sequence
      Inner()
      ? "unreachable-outer"
   recover
      ? "caught-cross"
   end sequence
   return nil

function Inner()
   ? "inner-pre"
   break
   ? "unreachable-inner"
   return nil

function Nested()
   begin sequence
      begin sequence
         break
      recover
         ? "inner-caught"
      end sequence
      ? "after-inner"
      break
   recover
      ? "outer-caught"
   end sequence
   return nil

function TryFinallyOk()
   begin sequence
      ? "try-fin-ok"
   always
      ? "always-ran-ok"
   end sequence
   return nil

function TryFinallyBrk()
   begin sequence
      begin sequence
         ? "try-fin-brk"
         break
      always
         ? "always-ran-brk"
      end sequence
   recover
      ? "outer-recovered"
   end sequence
   return nil
