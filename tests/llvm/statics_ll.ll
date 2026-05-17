; Harbour LLVM IR - generated from tests/llvm/statics.prg
%HB_SYMB = type { i8*, i64, i8*, i8* }

declare void @hb_vmExecute(i8*, %HB_SYMB*)
declare %HB_SYMB* @hb_vmProcessSymbols(%HB_SYMB*, i16, i8*, i32, i16)
declare void @hb_INITLINES()

@symbols = internal global %HB_SYMB* null

@.pcode.HB_FUN_STATICS = internal constant [1 x i8] c"\07"
@.pcode.HB_FUN_MAIN = internal constant [28 x i8] c"\74\03\00\24\03\00\68\01\00\7A\87\24\04\00\B0\02\00\67\01\00\14\01\24\05\00\64\6E\07"
@.pcode.hb_INITSTATICS = internal constant [13 x i8] c"\75\03\00\01\00\74\03\00\79\52\01\00\07"

@.sym.0 = private constant [8 x i8] c"\53\54\41\54\49\43\53\00"
@.sym.1 = private constant [5 x i8] c"\4D\41\49\4E\00"
@.sym.2 = private constant [5 x i8] c"\51\4F\55\54\00"
@.sym.3 = private constant [20 x i8] c"\28\5F\49\4E\49\54\53\54\41\54\49\43\53\30\30\30\30\31\29\00"

@.modname = private constant [23 x i8] c"\74\65\73\74\73\2F\6C\6C\76\6D\2F\73\74\61\74\69\63\73\2E\70\72\67\00"

@symbols_table = internal global [4 x %HB_SYMB] [
  %HB_SYMB { i8* getelementptr([8 x i8], [8 x i8]* @.sym.0, i32 0, i32 0),
           i64 517,
           i8* bitcast(void()* @HB_FUN_STATICS to i8*),
           i8* null },
  %HB_SYMB { i8* getelementptr([5 x i8], [5 x i8]* @.sym.1, i32 0, i32 0),
           i64 513,
           i8* bitcast(void()* @HB_FUN_MAIN to i8*),
           i8* null },
  %HB_SYMB { i8* getelementptr([5 x i8], [5 x i8]* @.sym.2, i32 0, i32 0),
           i64 1,
           i8* null,
           i8* null },
  %HB_SYMB { i8* getelementptr([20 x i8], [20 x i8]* @.sym.3, i32 0, i32 0),
           i64 536,
           i8* bitcast(void()* @hb_INITSTATICS to i8*),
           i8* null }
]

define void @HB_FUN_STATICS() {
  %s = load %HB_SYMB*, %HB_SYMB** @symbols
  call void @hb_vmExecute(i8* getelementptr([1 x i8], [1 x i8]* @.pcode.HB_FUN_STATICS, i32 0, i32 0), %HB_SYMB* %s)
  ret void
}

define void @HB_FUN_MAIN() {
  %s = load %HB_SYMB*, %HB_SYMB** @symbols
  call void @hb_vmExecute(i8* getelementptr([28 x i8], [28 x i8]* @.pcode.HB_FUN_MAIN, i32 0, i32 0), %HB_SYMB* %s)
  ret void
}

define void @hb_INITSTATICS() {
  %s = load %HB_SYMB*, %HB_SYMB** @symbols
  call void @hb_vmExecute(i8* getelementptr([13 x i8], [13 x i8]* @.pcode.hb_INITSTATICS, i32 0, i32 0), %HB_SYMB* %s)
  ret void
}

define internal void @hb_vm_SymbolInit() {
  %r = call %HB_SYMB* @hb_vmProcessSymbols(
    %HB_SYMB* getelementptr([4 x %HB_SYMB], [4 x %HB_SYMB]* @symbols_table, i32 0, i32 0),
    i16 4,
    i8* getelementptr([23 x i8], [23 x i8]* @.modname, i32 0, i32 0),
    i32 0, i16 3)
  store %HB_SYMB* %r, %HB_SYMB** @symbols
  ret void
}

@llvm.global_ctors = appending global [1 x { i32, void()*, i8* }]
  [ { i32, void()*, i8* } { i32 65535, void()* @hb_vm_SymbolInit, i8* null } ]
