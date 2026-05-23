//
// Group H corpus — macro opcodes straight-lined by the LLVM backend.
//
// One function per pcode opcode the Harbour compiler emits for macros. Each
// function uses the precise xBase form that triggers its target opcode:
//
//   MACROPUSH        x := &cName            (macro read into local)
//   MACROPOP         &cName := 99           (macro write)
//   MACROFUNC        Upper( &cArg )         (named call, macro in arglist)
//   MACROPUSHLIST    Upper( &cArg ), Show( &cArg )   (macro arg list — co-emitted w/ MACROFUNC and MACRODO)
//   MACRODO          Show( &cArg )          (statement-position named call w/ macro arg)
//   MACROTEXT        "hello, &cWhat"        (string literal w/ macro substitution)
//   MACROPUSHREF     TakeRef( @&cName )     (pass-by-reference macro)
//   MACROPUSHALIASED ? M->&cFld             (aliased memvar w/ macro field name)
//
// Note: Harbour macros do NOT resolve LOCAL variables — every &-target is
// declared private (or memvar, for the aliased case).
//
function Main()
   local   cN := "nMain"
   private nMain := "ready"
   // direct macro op in Main so run.sh's HB_FUN_MAIN hard-fail check
   // catches a regression in any group-H shim — without this, Main()
   // contains no macros and the gate is vacuous.
   ? &cN
   ReadVar()
   WriteVar()
   CallFunc()
   DoFunc()
   TextSub()
   RefMacro()
   AliasMacro()
   return nil

function ReadVar()
   local   x
   local   cName := "n"
   private n     := 7
   // assignment form — `? &cName` would compile to MACROPUSHLIST+MACRODO, not MACROPUSH.
   x := &cName
   ? x
   return nil

function WriteVar()
   local   cName := "n"
   private n     := 0
   &cName := 99
   ? n
   return nil

function CallFunc()
   local cArg := "'hello'"
   ? Upper( &cArg )
   return nil

function DoFunc()
   local cArg := "'called'"
   Show( &cArg )
   return nil

function Show( cText )
   ? cText
   return nil

function TextSub()
   local   cStr
   private cWhat := "world"
   cStr := "hello, &cWhat"
   ? cStr
   return nil

function RefMacro()
   local   cName := "n"
   private n     := 0
   TakeRef( @&cName )
   ? n
   return nil

function TakeRef( xRef )
   xRef := 33
   return nil

function AliasMacro()
   local   cFld := "FOO"
   private FOO  := 99
   ? M->&cFld
   return nil
