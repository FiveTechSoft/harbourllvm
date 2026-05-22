//
// Group F corpus — SWITCH statements straight-lined by the LLVM backend.
//
// The selector is made non-constant with Val() so the compiler emits a real
// HB_P_SWITCH (a constant selector would be folded to a plain JUMP).  Each
// non-default case ends with EXIT — Harbour's SWITCH is C-style fall-through.
//
function Main()
   Classify( Val( "1" ) )
   Classify( Val( "2" ) )
   Classify( Val( "9" ) )
   ClassifyStr( "x" )
   ClassifyStr( "z" )
   return nil

function Classify( n )
   switch n
   case 1
      ? "one"
      exit
   case 2
      ? "two"
      exit
   otherwise
      ? "many"
   end
   return nil

function ClassifyStr( c )
   switch c
   case "x"
      ? "ex"
      exit
   otherwise
      ? "?"
   end
   return nil
