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
//   MACROSEND        oErr:_Description( &cArg )    (macro arg in method call — obj:Meth(&x))
//   MACROPUSHPARE    x := ( &cExpr )               (macro in parenthesised expr list)
//   MACROPOPALIASED  M->&cFld := 42                (aliased memvar write with macro field)
//   MACROARRAYGEN    { &cParts }                   (macro-element array literal)
//   MACROPUSHINDEX   aArr[ &cIdx ]                 (macro array subscript index)
//   MACROSYMBOL      DO &cName                     (compile macro string to symbol name)
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
   SendMsg()
   PareSub()
   AliasWrite()
   ArrayGen()
   ArrayIndex()
   SymbolCall()
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

// MACROSEND — macro arg in a method call: oObj:Method( &cArg )
// The Error class _Description setter is called with a macro-expanded arg.
function SendMsg()
   local oErr := ErrorNew()
   local cArg := "'send-ok'"
   oErr:_Description( &cArg )
   ? oErr:Description()
   return nil

// MACROPUSHPARE — macro inside a parenthesized expression list: x := ( &cExpr )
function PareSub()
   local x
   local cExpr := "6 * 7"
   x := ( &cExpr )
   ? x
   return nil

// MACROPOPALIASED — aliased memvar write with macro field name: M->&cFld := value
function AliasWrite()
   local   cFld := "BAR"
   private BAR  := 0
   M->&cFld := 42
   ? BAR
   return nil

// MACROARRAYGEN — macro-element array literal: { &cParts }
function ArrayGen()
   local cParts := "11, 22, 33"
   local aArr   := { &cParts }
   ? aArr[ 1 ]
   return nil

// MACROPUSHINDEX — macro array subscript index: aArr[ &cIdx ]
function ArrayIndex()
   local aArr := { 10, 20, 30 }
   local cIdx  := "2"
   ? aArr[ &cIdx ]
   return nil

// MACROSYMBOL — compile macro string to symbol name: DO &cName
function SymbolCall()
   local cName := "SayHi"
   DO &cName
   return nil

function SayHi()
   ? "symbol-call"
   return nil
