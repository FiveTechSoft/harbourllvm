; Harbour LLVM IR - generated from tests/llvm/hello.prg
%HB_SYMB = type { i8*, i64, i8*, i8* }

declare void @hb_vmExecute(i8*, %HB_SYMB*)
declare %HB_SYMB* @hb_vmProcessSymbols(%HB_SYMB*, i16, i8*, i32, i16)
declare void @hb_INITSTATICS()
declare void @hb_INITLINES()

@symbols = internal global %HB_SYMB* null

@.pcode.HB_FUN_HELLO = internal constant [1 x i8] c"\07"
@.pcode.HB_FUN_MAIN = internal constant [28 x i8] c"\24\02\00\B0\02\00\6A\0C\48\65\6C\6C\6F\20\77\6F\72\6C\64\00\14\01\24\03\00\64\6E\07"

@.sym.0 = private constant [6 x i8] c"\48\45\4C\4C\4F\00"
@.sym.1 = private constant [5 x i8] c"\4D\41\49\4E\00"
@.sym.2 = private constant [5 x i8] c"\51\4F\55\54\00"

@.modname = private constant [21 x i8] c"\74\65\73\74\73\2F\6C\6C\76\6D\2F\68\65\6C\6C\6F\2E\70\72\67\00"

@symbols_table = internal global [3 x %HB_SYMB] [
  %HB_SYMB { i8* getelementptr([6 x i8], [6 x i8]* @.sym.0, i32 0, i32 0),
           i64 517,
           i8* bitcast(void()* @HB_FUN_HELLO to i8*),
           i8* null },
  %HB_SYMB { i8* getelementptr([5 x i8], [5 x i8]* @.sym.1, i32 0, i32 0),
           i64 513,
           i8* bitcast(void()* @HB_FUN_MAIN to i8*),
           i8* null },
  %HB_SYMB { i8* getelementptr([5 x i8], [5 x i8]* @.sym.2, i32 0, i32 0),
           i64 1,
           i8* null,
           i8* null }
]

define void @HB_FUN_HELLO() {
  %s = load %HB_SYMB*, %HB_SYMB** @symbols
  call void @hb_vmExecute(i8* getelementptr([1 x i8], [1 x i8]* @.pcode.HB_FUN_HELLO, i32 0, i32 0), %HB_SYMB* %s)
  ret void
}

define void @HB_FUN_MAIN() {
  %s = load %HB_SYMB*, %HB_SYMB** @symbols
  call void @hb_vmExecute(i8* getelementptr([28 x i8], [28 x i8]* @.pcode.HB_FUN_MAIN, i32 0, i32 0), %HB_SYMB* %s)
  ret void
}

define internal void @hb_vm_SymbolInit() {
  %r = call %HB_SYMB* @hb_vmProcessSymbols(
    %HB_SYMB* getelementptr([3 x %HB_SYMB], [3 x %HB_SYMB]* @symbols_table, i32 0, i32 0),
    i16 3,
    i8* getelementptr([21 x i8], [21 x i8]* @.modname, i32 0, i32 0),
    i32 0, i16 3)
  store %HB_SYMB* %r, %HB_SYMB** @symbols
  ret void
}

@llvm.global_ctors = appending global [1 x { i32, void()*, i8* }]
  [ { i32, void()*, i8* } { i32 65535, void()* @hb_vm_SymbolInit, i8* null } ]
