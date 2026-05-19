/* Embedded LLVM back end for the Harbour compiler. */

/* GCC 16 emits -Wformat-truncation for snprintf calls where the theoretical
 * maximum (buffer_size - 1 + suffix) slightly exceeds the destination size.
 * In all cases here the paths are well under the limit in practice; suppress
 * the false-positive diagnostic. */
#if defined( __GNUC__ )
#  pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include "hb_llvmobj.h"
#include "hb_lldshim.h"

#include "llvm-c/Core.h"
#include "llvm-c/IRReader.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined( _WIN32 ) || defined( __WIN32__ ) || defined( WIN32 )
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

int hb_llvmEmitObject( const char * szLLPath, const char * szObjPath )
{
   LLVMContextRef     ctx;
   LLVMMemoryBufferRef buf = NULL;
   LLVMModuleRef      mod  = NULL;
   LLVMTargetRef      target = NULL;
   LLVMTargetMachineRef tm     = NULL;
   LLVMTargetDataRef  layout  = NULL;
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

   if( tm == NULL )
   {
      fprintf( stderr, "harbour -GL: cannot create target machine for '%s'\n", triple );
      LLVMDisposeMessage( triple );
      LLVMDisposeModule( mod );
      LLVMContextDispose( ctx );
      return 1;
   }

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

/* Harbour runtime archives a console program links against, in dependency
 * order (inside --start-group/--end-group so ordering is forgiving).
 * Names are base names; the .a is found under szLibDir as lib<name>.a.
 * Derived from: hbmk2 -trace -gtstd link command for hello.prg.
 *
 * All standard GT drivers are included so that libhbrtl.a's gtsys.o can
 * resolve HB_FUN_HB_GT_WIN (a deliberate Harbour force-link mechanism).
 * The bundled hb_llvmgtstd.o sets gtstd as the active GT at runtime via
 * hb_vmSetDefaultGT("STD"), overriding the Windows default (gtwin). */
static const char * const s_hbRuntimeLibs[] = {
   "hbextern", "hbdebug", "hbvm", "hbrtl", "hblang", "hbcpage",
   "gtcgi", "gtpca", "gtstd", "gtwin", "gtwvt", "gtgui",
   "hbrdd", "hbuddall", "hbusrrdd",
   "rddntx", "rddcdx", "rddnsx", "rddfpt",
   "hbrdd", "hbhsx", "hbsix",
   "hbmacro", "hbcplr", "hbpp", "hbcommon", "hbmainstd",
   "hbpcre", "hbzlib"
};

/* Windows API import libraries the Harbour runtime depends on.
 * Derived from: hbmk2 -trace link command. */
static const char * const s_sysLibs[] = {
   "winmm", "kernel32", "user32", "gdi32", "advapi32",
   "ws2_32", "iphlpapi", "winspool", "comctl32", "comdlg32",
   "shell32", "uuid", "ole32", "oleaut32",
   "mpr", "mapi32", "imm32", "msimg32", "wininet"
};

/* Maximum number of argv slots needed.
 * Fixed args: ld.lld --subsystem console -o exe --undefined sym
 *             --start-group --end-group  + crt2 crtbegin crtend user_obj
 *             -L harbour -L mingw64-rt
 *             + runtime libs + sys libs
 *             + mingwex libgcc libgcc_eh libmingw32 libmingwex libmoldname
 *               libmsvcrt libstdc++
 * Budget with margin = 40 + runtimeLibs + sysLibs */
#define HB_LLD_MAX_ARGS  ( 40 + \
   ( sizeof( s_hbRuntimeLibs ) / sizeof( s_hbRuntimeLibs[ 0 ] ) ) + \
   ( sizeof( s_sysLibs )       / sizeof( s_sysLibs[ 0 ] ) ) )

int hb_llvmLinkExe( const char * szObjPath, const char * szLibDir,
                    const char * szExePath )
{
   const char * argv[ HB_LLD_MAX_ARGS ];
   char         szLibArg[ 4096 ];
   int          argc = 0;
   unsigned     i;
   int          rc;

   /* Heap-allocated strings we build for -L/-l/file args; freed after call. */
   char *       apszAlloc[ HB_LLD_MAX_ARGS ];
   int          nAlloc = 0;

   /* The bundled mingw64-rt directory is a sibling of szLibDir's parent:
    * szLibDir = <prefix>/lib/win/mingw64
    * mingw64-rt = <prefix>/lib/win/mingw64-rt
    * We build it with plain string operations — no process spawning. */
   char szRtDir[ 4096 ];

#define PUSH_DUP( str )  do { \
      char * _p = strdup( str ); \
      apszAlloc[ nAlloc++ ] = _p; \
      argv[ argc++ ] = _p; \
   } while( 0 )

#define PUSH_LDIR( dir )  do { \
      snprintf( szLibArg, sizeof( szLibArg ), "-L%s", ( dir ) ); \
      PUSH_DUP( szLibArg ); \
   } while( 0 )

   /* Build the bundled runtime dir path: replace the last path component
    * of szLibDir ("mingw64") with "mingw64-rt". */
   {
      char szTmp[ 4096 ];
      char * p;
      snprintf( szTmp, sizeof( szTmp ), "%s", szLibDir );
      /* Normalise backslashes */
      for( p = szTmp; *p; ++p )
         if( *p == '\\' ) *p = '/';
      /* Strip trailing slash if any */
      p = szTmp + strlen( szTmp ) - 1;
      while( p > szTmp && *p == '/' ) *p-- = '\0';
      /* Find last path separator and replace the final component */
      p = strrchr( szTmp, '/' );
      if( p )
         snprintf( szRtDir, sizeof( szRtDir ), "%.*s/mingw64-rt",
                   (int)( p - szTmp ), szTmp );
      else
         /* Flat layout fallback: look in a sibling dir */
         snprintf( szRtDir, sizeof( szRtDir ), "%s/../mingw64-rt", szTmp );
   }

   /* --- Build the ld.lld argument vector --- */

   argv[ argc++ ] = "ld.lld";
   argv[ argc++ ] = "--subsystem";
   argv[ argc++ ] = "console";
   argv[ argc++ ] = "-o";
   argv[ argc++ ] = szExePath;

   /* CRT startup objects — must come first, before the user object.
    * crt2.o provides mainCRTStartup; crtbegin.o/.end.o wrap C++ ctors/dtors. */
   {
      char szPath[ 8192 ];
      snprintf( szPath, sizeof( szPath ), "%s/crt2.o",     szRtDir );
      PUSH_DUP( szPath );
      snprintf( szPath, sizeof( szPath ), "%s/crtbegin.o", szRtDir );
      PUSH_DUP( szPath );
   }

   /* User's compiled object */
   argv[ argc++ ] = szObjPath;

   /* Bundled GT default stub: sets GTSTD as the active GT via
    * hb_vmSetDefaultGT("STD"), overriding the platform default (gtwin).
    * Shipped as lib/win/mingw64-rt/hb_llvmgtstd.o — no compilation at runtime. */
   {
      char szPath[ 8192 ];
      snprintf( szPath, sizeof( szPath ), "%s/hb_llvmgtstd.o", szRtDir );
      PUSH_DUP( szPath );
   }

   /* Force-pull HB_FUN_HB_GT_STD_DEFAULT from libgtstd.a to ensure the gtstd
    * registration code is linked in so hb_gt_Base() finds the STD driver. */
   argv[ argc++ ] = "--undefined";
   argv[ argc++ ] = "HB_FUN_HB_GT_STD_DEFAULT";

   /* Library search paths */
   PUSH_LDIR( szLibDir );    /* Harbour runtime archives: lib/win/mingw64    */
   PUSH_LDIR( szRtDir );     /* bundled CRT + import libs: lib/win/mingw64-rt */

   /* Harbour runtime archives — --start-group handles circular references */
   argv[ argc++ ] = "--start-group";

   for( i = 0; i < sizeof( s_hbRuntimeLibs ) / sizeof( s_hbRuntimeLibs[ 0 ] ); ++i )
   {
      snprintf( szLibArg, sizeof( szLibArg ), "-l%s", s_hbRuntimeLibs[ i ] );
      PUSH_DUP( szLibArg );
   }

   argv[ argc++ ] = "--end-group";

   /* Windows API import libraries */
   for( i = 0; i < sizeof( s_sysLibs ) / sizeof( s_sysLibs[ 0 ] ); ++i )
   {
      snprintf( szLibArg, sizeof( szLibArg ), "-l%s", s_sysLibs[ i ] );
      PUSH_DUP( szLibArg );
   }

   /* MinGW + GCC runtime libraries — same order GCC uses internally.
    * All resolved from the bundled mingw64-rt directory via the -L above. */
   argv[ argc++ ] = "-lstdc++";
   argv[ argc++ ] = "-lmingw32";
   argv[ argc++ ] = "-lgcc";
   argv[ argc++ ] = "-lgcc_eh";
   argv[ argc++ ] = "-lmingwex";
   argv[ argc++ ] = "-lmoldname";
   argv[ argc++ ] = "-lmsvcrt";

   /* CRT end object — GCC places it after all user code and libraries */
   {
      char szPath[ 8192 ];
      snprintf( szPath, sizeof( szPath ), "%s/crtend.o", szRtDir );
      PUSH_DUP( szPath );
   }

#undef PUSH_LDIR
#undef PUSH_DUP

   rc = hb_lld_link_mingw( argc, argv );
   if( rc != 0 )
      fprintf( stderr, "harbour -GL: link failed (lld rc=%d)\n", rc );

   /* Free heap-allocated argument strings */
   for( i = 0; i < ( unsigned ) nAlloc; ++i )
      free( apszAlloc[ i ] );

   return rc;
}

/*
 * hb_llvmRuntimeLibDir
 *
 * Fill szBuf with the absolute path of the Harbour runtime lib directory
 * (lib/win/mingw64), derived relative to the running harbour.exe.
 *
 * Layout:  <prefix>/bin/win/mingw64/harbour.exe
 *          <prefix>/lib/win/mingw64/   <- what we return
 *
 * Strategy: get the exe path via GetModuleFileNameA, normalise slashes,
 * strip the exe filename, then go up three directory components (mingw64,
 * win, bin) to reach <prefix>, then append lib/win/mingw64.
 *
 * Windows-only (Plan 2 is x86_64 Windows only).
 */
void hb_llvmRuntimeLibDir( char * szBuf, int nBufLen )
{
#if defined( _WIN32 ) || defined( __WIN32__ ) || defined( WIN32 )
   char   szExe[ 4096 ];
   char * p;
   int    i;

   szBuf[ 0 ] = '\0';

   if( GetModuleFileNameA( NULL, szExe, ( DWORD ) sizeof( szExe ) ) == 0 )
   {
      fprintf( stderr, "harbour -GL: GetModuleFileNameA failed\n" );
      return;
   }

   /* Normalise backslashes to forward slashes */
   for( p = szExe; *p; ++p )
      if( *p == '\\' ) *p = '/';

   /* Strip trailing filename: find last '/' and terminate */
   p = strrchr( szExe, '/' );
   if( p )
      *p = '\0';   /* szExe now holds <prefix>/bin/win/mingw64 */

   /* Walk up three directory components to reach <prefix>:
    * 1) mingw64  2) win  3) bin */
   for( i = 0; i < 3; ++i )
   {
      p = strrchr( szExe, '/' );
      if( p )
         *p = '\0';
      else
      {
         /* Unexpected layout — fall back to cwd */
         szBuf[ 0 ] = '.';
         szBuf[ 1 ] = '\0';
         return;
      }
   }

   /* szExe is now <prefix>; append the lib sub-path */
   snprintf( szBuf, ( size_t ) nBufLen, "%s/lib/win/mingw64", szExe );

#else
   /* Non-Windows stub — not used in Plan 2 */
   ( void ) nBufLen;
   szBuf[ 0 ] = '\0';
#endif
}

/*
 * hb_llvmBackendInit
 *
 * Module constructor: called automatically when libhbllvm.a is linked into
 * an executable.  Registers the real LLVM back-end implementations into the
 * dispatch table defined in libhbcplr.a (g_hb_llvm_backend).
 *
 * Using GCC's __attribute__((constructor)) ensures this runs before main(),
 * so the table is filled before any compiler call reaches genllvm.c.
 */
static void hb_llvmBackendInit( void ) __attribute__((constructor));
static void hb_llvmBackendInit( void )
{
   static const HB_LLVM_BACKEND s_backend = {
      hb_llvmEmitObject,
      hb_llvmLinkExe,
      hb_llvmRuntimeLibDir
   };
   hb_llvmBackendRegister( &s_backend );
}
