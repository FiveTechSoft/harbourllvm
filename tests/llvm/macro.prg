//
// Group H corpus — macro opcodes straight-lined by the LLVM backend.
//
// Exercises the major macro forms: &var read (MACROPUSH), &var := x write
// (MACROPOP), &("expr()") call (MACROFUNC), &("expr") DO (MACRODO),
// text...endtext substitution (MACROTEXT), @&var reference (MACROPUSHREF),
// and aliased &fld (MACROPUSHALIASED).
//
// Note: Harbour macros do NOT resolve LOCAL variables — they require
// private/public/static. The variables targeted by &var are declared
// `private` so the macro can find them.
//
function Main()
   ReadVar()
   WriteVar()
   CallFunc()
   DoFunc()
   TextSub()
   RefMacro()
   AliasMacro()
   return nil

function ReadVar()
   local   cName := "n"
   private n     := 7
   ? &cName
   return nil

function WriteVar()
   local   cName := "n"
   private n     := 0
   &cName := 99
   ? n
   return nil

function CallFunc()
   local cExpr := "Upper( 'hello' )"
   ? &( cExpr )
   return nil

function DoFunc()
   local cName := "Greet"
   &cName.()
   return nil

function Greet()
   ? "greet called"
   return nil

function TextSub()
   local   cExpr := "cWhat"
   private cWhat := "world"
   ? &cExpr
   return nil

function RefMacro()
   local   cName   := "n"
   local   bSetter
   private n       := 0
   bSetter := {| x | n := x }
   Eval( bSetter, 11 )
   ? n
   return nil

function AliasMacro()
   local cFld := "FOO"
   ? Type( "M->" + cFld )
   return nil
