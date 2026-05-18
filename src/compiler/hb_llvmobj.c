/* Embedded LLVM back end for the Harbour compiler. */
#include "hb_llvmobj.h"
#include "hb_lldshim.h"

#include "llvm-c/Core.h"
#include "llvm-c/IRReader.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hb_llvmEmitObject( const char * szLLPath, const char * szObjPath )
{
   LLVMContextRef     ctx;
   LLVMMemoryBufferRef buf = NULL;
   LLVMModuleRef      mod  = NULL;
   LLVMTargetRef      target = NULL;
   LLVMTargetMachineRef tm;
   LLVMTargetDataRef  layout;
   char *             triple;
   char *             err  = NULL;

   LLVMInitializeX86TargetInfo();
   LLVMInitializeX86Target();
   LLVMInitializeX86TargetMC();
   LLVMInitializeX86AsmPrinter();

   ctx = LLVMContextCreate();

   if( LLVMCreateMemoryBufferWithContentsOfFile( szLLPath, &buf, &err ) )
   {
      fprintf( stderr, "harbour -GL: cannot read IR '%s': %s\n", szLLPath, err );
      LLVMDisposeMessage( err );
      LLVMContextDispose( ctx );
      return 1;
   }

   if( LLVMParseIRInContext( ctx, buf, &mod, &err ) )   /* consumes buf */
   {
      fprintf( stderr, "harbour -GL: IR parse error: %s\n", err );
      LLVMDisposeMessage( err );
      LLVMContextDispose( ctx );
      return 1;
   }

   triple = LLVMGetDefaultTargetTriple();   /* x86_64-w64-windows-gnu */
   LLVMSetTarget( mod, triple );

   if( LLVMGetTargetFromTriple( triple, &target, &err ) )
   {
      fprintf( stderr, "harbour -GL: target lookup failed: %s\n", err );
      LLVMDisposeMessage( err );
      LLVMDisposeMessage( triple );
      LLVMDisposeModule( mod );
      LLVMContextDispose( ctx );
      return 1;
   }

   tm = LLVMCreateTargetMachine( target, triple, "generic", "",
                                 LLVMCodeGenLevelDefault,
                                 LLVMRelocDefault,
                                 LLVMCodeModelDefault );

   layout = LLVMCreateTargetDataLayout( tm );
   LLVMSetModuleDataLayout( mod, layout );
   LLVMDisposeTargetData( layout );

   if( LLVMTargetMachineEmitToFile( tm, mod, szObjPath, LLVMObjectFile, &err ) )
   {
      fprintf( stderr, "harbour -GL: object emit failed: %s\n", err );
      LLVMDisposeMessage( err );
      LLVMDisposeTargetMachine( tm );
      LLVMDisposeMessage( triple );
      LLVMDisposeModule( mod );
      LLVMContextDispose( ctx );
      return 1;
   }

   LLVMDisposeTargetMachine( tm );
   LLVMDisposeMessage( triple );
   LLVMDisposeModule( mod );
   LLVMContextDispose( ctx );
   return 0;
}

/* hb_llvmLinkExe is added in Task 4. */
