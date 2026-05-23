//
// Group G corpus — codeblocks straight-lined by the LLVM backend.
//
// Exercises the compatibility-critical cases: parameterless block,
// parameterised block via Eval(), block capturing an enclosing local
// (detached local), nested codeblocks, and a block stored then evaluated.
//
function Main()
   local bAdd    := {| x, y | x + y }
   local nBase   := 100
   local bCapture := {| x | x + nBase }
   local bNested := {| x | Eval( {| y | y * 2 }, x ) + 1 }
   local bStored

   ? Eval( {|| "no args" } )
   ? Eval( bAdd, 3, 4 )
   ? Eval( bCapture, 5 )
   ? Eval( bNested, 10 )

   bStored := {| x | x - 1 }
   ? Eval( bStored, 42 )
   return nil
