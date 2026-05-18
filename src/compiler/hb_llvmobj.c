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
#if defined( _WIN32 )
   #include <process.h>   /* getpid() on MinGW */
#else
   #include <unistd.h>
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
 * Derived from: hbmk2 -trace -gtstd link command for hello.prg. */
static const char * const s_hbRuntimeLibs[] = {
   "hbextern", "hbdebug", "hbvm", "hbrtl", "hblang", "hbcpage",
   "gtcgi", "gtpca", "gtstd", "gtwin", "gtwvt", "gtgui",
   "hbrdd", "hbuddall", "hbusrrdd",
   "rddntx", "rddcdx", "rddnsx", "rddfpt",
   "hbrdd", "hbhsx", "hbsix",
   "hbmacro", "hbcplr", "hbpp", "hbcommon", "hbmainstd",
   "hbpcre", "hbzlib"
};

/* MinGW system libraries the Harbour runtime depends on.
 * Derived from: hbmk2 -trace link command. */
static const char * const s_sysLibs[] = {
   "winmm", "kernel32", "user32", "gdi32", "advapi32",
   "ws2_32", "iphlpapi", "winspool", "comctl32", "comdlg32",
   "shell32", "uuid", "ole32", "oleaut32",
   "mpr", "mapi32", "imm32", "msimg32", "wininet"
};

/* Maximum number of argv slots needed.
 * Fixed direct args (16): ld.lld --subsystem console -o exe obj
 *   --undefined sym --start-group --end-group
 *   -lstdc++ -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt
 * PUSH_DUP/PUSH_FILE args: crt2 crtbegin gtstd -Lharbour -Lsys -Lstdcxx -Lgcc
 *   + runtime libs + system libs + crtend
 * Total budget with margin = 32 + runtimeLibs + sysLibs */
#define HB_LLD_MAX_ARGS  ( 32 + \
   ( sizeof( s_hbRuntimeLibs ) / sizeof( s_hbRuntimeLibs[ 0 ] ) ) + \
   ( sizeof( s_sysLibs )       / sizeof( s_sysLibs[ 0 ] ) ) )

/* Helper: run a gcc query command and return the directory part of the output.
 * The caller must free() the returned string. Returns NULL on failure. */
static char * s_gccQueryDir( const char * szCmd, const char * szFallbackBasename )
{
   FILE * fp;
   char   buf[ 1024 ];
   char * p;
   size_t n;

   fp = popen( szCmd, "r" );
   if( fp == NULL )
      return NULL;

   buf[ 0 ] = '\0';
   if( fgets( buf, sizeof( buf ), fp ) == NULL )
   {
      pclose( fp );
      return NULL;
   }
   pclose( fp );

   n = strlen( buf );
   while( n > 0 && ( buf[ n - 1 ] == '\n' || buf[ n - 1 ] == '\r' ) )
      buf[ --n ] = '\0';

   /* If GCC printed just the basename it means it didn't find it */
   if( szFallbackBasename && strcmp( buf, szFallbackBasename ) == 0 )
      return NULL;

   p = strrchr( buf, '/' );
   if( p == NULL )
      p = strrchr( buf, '\\' );
   if( p == NULL )
      return NULL;
   *p = '\0';

   return strdup( buf );
}

/* Query GCC for the directory that contains libkernel32.a (the MinGW import-lib
 * directory).  Returns a heap-allocated path string the caller must free(),
 * or NULL on failure. */
static char * s_mingwSysLibDir( void )
{
   return s_gccQueryDir( "gcc -m64 -print-file-name=libkernel32.a", "libkernel32.a" );
}

/* Query GCC for the directory that contains libgcc.a.
 * This is also where crtbegin.o / crtend.o live. */
static char * s_mingwGccLibDir( void )
{
   return s_gccQueryDir( "gcc -m64 -print-libgcc-file-name", NULL );
}

/* Query GCC for the directory that contains libstdc++.a (may differ from
 * the import-lib dir and the gcc dir). */
static char * s_mingwStdcxxLibDir( void )
{
   return s_gccQueryDir( "gcc -m64 -print-file-name=libstdc++.a", "libstdc++.a" );
}

/* Query GCC for a specific file path (not directory).
 * Returns a heap-allocated path string the caller must free(), or NULL. */
static char * s_gccQueryFile( const char * szCmd, const char * szFallbackBasename )
{
   FILE * fp;
   char   buf[ 1024 ];
   size_t n;

   fp = popen( szCmd, "r" );
   if( fp == NULL )
      return NULL;

   buf[ 0 ] = '\0';
   if( fgets( buf, sizeof( buf ), fp ) == NULL )
   {
      pclose( fp );
      return NULL;
   }
   pclose( fp );

   n = strlen( buf );
   while( n > 0 && ( buf[ n - 1 ] == '\n' || buf[ n - 1 ] == '\r' ) )
      buf[ --n ] = '\0';

   if( szFallbackBasename && strcmp( buf, szFallbackBasename ) == 0 )
      return NULL;

   return strdup( buf );
}

/* Build and compile a tiny C stub that sets GTSTD as the default GT driver.
 * This replicates what hbmk2 -gtstd injects when it generates its C stub.
 * Returns a heap-allocated path to the compiled .o (caller must free()), or
 * NULL on failure.  szHarbourInclude is the path to Harbour's include dir. */
static char * s_buildGtstdStub( const char * szHarbourInclude )
{
   /* HB_FUNC_EXTERN declares an undefined reference to HB_FUN_HB_GT_STD_DEFAULT.
    * This forces the linker to pull in the gtstd registration code from
    * libgtstd.a, so that hb_gt_Base() succeeds at runtime.
    * This mirrors what hbmk2 -gtstd generates via EXTERNAL HB_GT_STD_DEFAULT.
    * We do NOT call the function — Harbour's VM symbol scanning handles that. */
   static const char s_szSrc[] =
      "#include \"hbvmpub.h\"\n"
      "#include \"hbinit.h\"\n"
      "#include \"hbapi.h\"\n"
      /* extern reference — just the declaration, no call */
      "HB_FUNC_EXTERN( HB_GT_STD_DEFAULT );\n"
      /* A dummy data reference ensures the linker keeps the symbol alive */
      "static void * const s_hb_gt_std_ref = (void *) HB_FUNCNAME( HB_GT_STD_DEFAULT );\n"
      "HB_CALL_ON_STARTUP_BEGIN( _hb_llvm_gt_setdef_ )\n"
      "   hb_vmSetDefaultGT( \"STD\" );\n"
      "   ( void ) s_hb_gt_std_ref;\n"   /* suppress unused-variable warning */
      "HB_CALL_ON_STARTUP_END( _hb_llvm_gt_setdef_ )\n"
      "#if defined( HB_PRAGMA_STARTUP )\n"
      "   #pragma startup _hb_llvm_gt_setdef_\n"
      "#elif defined( HB_DATASEG_STARTUP )\n"
      "   #define HB_DATASEG_BODY    HB_DATASEG_FUNC( _hb_llvm_gt_setdef_ )\n"
      "   #include \"hbiniseg.h\"\n"
      "#endif\n";

   char   szCFile[ 512 ];
   char   szOFile[ 512 ];
   char   szCmd[  2048 ];
   FILE * fp;
   int    rc;

   /* Write the C source to a temp file */
   snprintf( szCFile, sizeof( szCFile ), "%s/_hb_llvm_gtstd_%u.c",
             getenv( "TEMP" ) ? getenv( "TEMP" ) : ".", (unsigned) getpid() );
   snprintf( szOFile, sizeof( szOFile ), "%s/_hb_llvm_gtstd_%u.o",
             getenv( "TEMP" ) ? getenv( "TEMP" ) : ".", (unsigned) getpid() );

   fp = fopen( szCFile, "w" );
   if( fp == NULL )
      return NULL;
   fputs( s_szSrc, fp );
   fclose( fp );

   /* Compile it with GCC — same flags hbmk2 uses for its generated stub */
   snprintf( szCmd, sizeof( szCmd ),
             "gcc -c \"%s\" -I\"%s\" -O3 -D__USE_MINGW_ANSI_STDIO=0 -o \"%s\"",
             szCFile, szHarbourInclude, szOFile );
   rc = system( szCmd );
   remove( szCFile );  /* clean up the .c regardless */

   if( rc != 0 )
      return NULL;

   return strdup( szOFile );
}

int hb_llvmLinkExe( const char * szObjPath, const char * szLibDir,
                    const char * szExePath )
{
   const char * argv[ HB_LLD_MAX_ARGS ];
   char         szLibArg[ 1024 ];
   int          argc = 0;
   unsigned     i;
   int          rc;
   char *       szSysLibDir    = NULL;  /* contains libkernel32.a, etc.     */
   char *       szGccLibDir    = NULL;  /* contains libgcc.a, crtbegin.o    */
   char *       szStdcxxLibDir = NULL;  /* contains libstdc++.a              */
   char *       szCrt2         = NULL;  /* crt2.o — provides mainCRTStartup  */
   char *       szCrtBegin     = NULL;  /* crtbegin.o — GCC C++ init         */
   char *       szCrtEnd       = NULL;  /* crtend.o   — GCC C++ fini         */
   char *       szGtstdObj     = NULL;  /* gtstd default-setter stub          */
   /* The Harbour include dir is szLibDir/../../include — simplified here by
    * searching relative to szLibDir. If it's lib/win/mingw64, go up two levels
    * and append "include".  This heuristic is good enough for the test rig. */
   char         szHbInc[ 512 ];

   /* Heap-allocated strings we build for -L/-l/-file args; freed after call. */
   char *       apszAlloc[ HB_LLD_MAX_ARGS ];
   int          nAlloc = 0;

#define PUSH_DUP( str )  do { \
      char * _p = strdup( str ); \
      apszAlloc[ nAlloc++ ] = _p; \
      argv[ argc++ ] = _p; \
   } while( 0 )

#define PUSH_LDIR( dir )  do { \
      if( dir ) { \
         snprintf( szLibArg, sizeof( szLibArg ), "-L%s", dir ); \
         PUSH_DUP( szLibArg ); \
         free( dir ); \
         dir = NULL; \
      } \
   } while( 0 )

#define PUSH_FILE( f )  do { \
      if( f ) { \
         PUSH_DUP( f ); \
         free( f ); \
         f = NULL; \
      } \
   } while( 0 )

   /* Derive the Harbour include directory from szLibDir.
    * Assumption: szLibDir is something like ".../lib/win/mingw64".
    * The include dir is typically ".../include".
    * We go up to the first component that looks like "lib" and replace with
    * "include".  Fallback: just use szLibDir/../../../include. */
   {
      char szTmp[ 512 ];
      char * pLib;
      snprintf( szTmp, sizeof( szTmp ), "%s", szLibDir );
      /* Convert backslashes to forward slashes for uniformity */
      for( char * p = szTmp; *p; ++p )
         if( *p == '\\' ) *p = '/';
      /* Find the last "/lib" component and replace everything from there with /include */
      pLib = NULL;
      {
         char * p = szTmp;
         while( ( p = strstr( p, "/lib" ) ) != NULL )
         {
            pLib = p;
            p++;
         }
      }
      if( pLib )
         snprintf( szHbInc, sizeof( szHbInc ), "%.*sinclude",
                   (int)( pLib - szTmp ), szTmp );
      else
         snprintf( szHbInc, sizeof( szHbInc ), "%s/../../../include", szLibDir );
   }

   /* Discover the MinGW lib directories that GCC uses implicitly.
    * LLD does not search these paths on its own, so we must be explicit. */
   szSysLibDir    = s_mingwSysLibDir();
   szGccLibDir    = s_mingwGccLibDir();
   szStdcxxLibDir = s_mingwStdcxxLibDir();

   /* CRT startup objects that GCC adds automatically for -mconsole programs. */
   szCrt2     = s_gccQueryFile( "gcc -m64 -print-file-name=crt2.o",     "crt2.o"     );
   szCrtBegin = s_gccQueryFile( "gcc -m64 -print-file-name=crtbegin.o", "crtbegin.o" );
   szCrtEnd   = s_gccQueryFile( "gcc -m64 -print-file-name=crtend.o",   "crtend.o"   );

   /* GTSTD default-setter stub: compile from embedded source */
   szGtstdObj = s_buildGtstdStub( szHbInc );

   argv[ argc++ ] = "ld.lld";
   argv[ argc++ ] = "--subsystem";
   argv[ argc++ ] = "console";
   argv[ argc++ ] = "-o";
   argv[ argc++ ] = szExePath;

   /* CRT startup objects must come first (before the user object). */
   PUSH_FILE( szCrt2     );
   PUSH_FILE( szCrtBegin );

   /* User object */
   argv[ argc++ ] = szObjPath;

   /* Force-pull HB_FUN_HB_GT_STD_DEFAULT from libgtstd.a.
    * With --start-group/--end-group this causes the gtstd registration code
    * to be linked in, so hb_gt_Base() succeeds at runtime.
    * This mirrors hbmk2 -gtstd's EXTERNAL HB_GT_STD_DEFAULT. */
   argv[ argc++ ] = "--undefined";
   argv[ argc++ ] = "HB_FUN_HB_GT_STD_DEFAULT";

   /* GTSTD default stub (sets the default GT name at startup) */
   PUSH_FILE( szGtstdObj );

   /* Library search paths: Harbour, MinGW import libs, stdc++, gcc */
   snprintf( szLibArg, sizeof( szLibArg ), "-L%s", szLibDir );
   PUSH_DUP( szLibArg );
   PUSH_LDIR( szSysLibDir );
   PUSH_LDIR( szStdcxxLibDir );
   PUSH_LDIR( szGccLibDir );

   /* Harbour runtime archives — --start-group handles circular references. */
   argv[ argc++ ] = "--start-group";

   for( i = 0; i < sizeof( s_hbRuntimeLibs ) / sizeof( s_hbRuntimeLibs[ 0 ] ); ++i )
   {
      snprintf( szLibArg, sizeof( szLibArg ), "-l%s", s_hbRuntimeLibs[ i ] );
      PUSH_DUP( szLibArg );
   }

   argv[ argc++ ] = "--end-group";

   /* MinGW C/C++ runtime — same order GCC uses */
   argv[ argc++ ] = "-lstdc++";
   argv[ argc++ ] = "-lmingw32";
   argv[ argc++ ] = "-lgcc";
   argv[ argc++ ] = "-lgcc_eh";
   argv[ argc++ ] = "-lmingwex";
   argv[ argc++ ] = "-lmsvcrt";

   /* Windows system import libraries */
   for( i = 0; i < sizeof( s_sysLibs ) / sizeof( s_sysLibs[ 0 ] ); ++i )
   {
      snprintf( szLibArg, sizeof( szLibArg ), "-l%s", s_sysLibs[ i ] );
      PUSH_DUP( szLibArg );
   }

   /* CRT end object (GCC places it after all user code and libs) */
   PUSH_FILE( szCrtEnd );

#undef PUSH_FILE
#undef PUSH_LDIR
#undef PUSH_DUP

   rc = hb_lld_link_mingw( argc, argv );
   if( rc != 0 )
      fprintf( stderr, "harbour -GL: link failed (lld rc=%d)\n", rc );

   /* Clean up the temp gtstd stub object (it's in apszAlloc already freed,
    * but we need to remove the file itself).  Find it by checking which
    * allocated string ends in ".o" and starts with the TEMP prefix. */
   {
      const char * szTemp = getenv( "TEMP" );
      if( szTemp == NULL )
         szTemp = ".";
      for( i = 0; i < ( unsigned ) nAlloc; ++i )
      {
         const char * s = apszAlloc[ i ];
         size_t       n = strlen( s );
         if( n > 2 && s[ n - 2 ] == '.' && s[ n - 1 ] == 'o' &&
             strstr( s, "_hb_llvm_gtstd_" ) != NULL )
         {
            remove( s );
         }
      }
   }

   /* Free heap-allocated argument strings */
   for( i = 0; i < ( unsigned ) nAlloc; ++i )
      free( apszAlloc[ i ] );

   return rc;
}
