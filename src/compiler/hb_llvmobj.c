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

#if defined( __APPLE__ )

#include <stdio.h>     /* popen, pclose, fgets, FILE */
#include <stdlib.h>    /* NULL */
#include <string.h>    /* strlen */

/* Returns a pointer to a static buffer holding the macOS SDK path, or
 * NULL on failure. Cached after first successful call. */
static const char * hb_macos_sdk_path( void )
{
   static char s_sdkPath[ 4096 ];
   static int  s_initialised = 0;

   if( ! s_initialised )
   {
      FILE * fp = popen( "xcrun --show-sdk-path 2>/dev/null", "r" );
      if( fp )
      {
         if( fgets( s_sdkPath, sizeof( s_sdkPath ), fp ) != NULL )
         {
            size_t n = strlen( s_sdkPath );
            /* trim trailing newline */
            while( n > 0 && ( s_sdkPath[ n - 1 ] == '\n' ||
                              s_sdkPath[ n - 1 ] == '\r' ) )
               s_sdkPath[ --n ] = '\0';
         }
         pclose( fp );
      }
      s_initialised = 1;
   }
   return s_sdkPath[ 0 ] ? s_sdkPath : NULL;
}

#endif /* __APPLE__ */

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
 * Budget with margin = 40 + runtimeLibs + sysLibs + 128 for user libs */
#define HB_LLD_MAX_ARGS  ( 40 + 128 + \
   ( sizeof( s_hbRuntimeLibs ) / sizeof( s_hbRuntimeLibs[ 0 ] ) ) + \
   ( sizeof( s_sysLibs )       / sizeof( s_sysLibs[ 0 ] ) ) )

int hb_llvmLinkExe( const char * szObjPath, const char * szLibDir,
                    const char * szExePath,
                    const char * const * aszUserLibDirs, int nUserLibDirCount,
                    const char * const * aszUserLibNames, int nUserLibNameCount )
{
   const char * argv[ HB_LLD_MAX_ARGS ];
   char         szLibArg[ 4096 ];
   int          argc = 0;
   unsigned     i;
   int          rc;

   /* Heap-allocated strings we build for -L/-l/file args; freed after call. */
   char *       apszAlloc[ HB_LLD_MAX_ARGS ];
   int          nAlloc = 0;

#define PUSH_DUP( str )  do { \
      char * _p = strdup( str ); \
      apszAlloc[ nAlloc++ ] = _p; \
      argv[ argc++ ] = _p; \
   } while( 0 )

#define PUSH_LDIR( dir )  do { \
      snprintf( szLibArg, sizeof( szLibArg ), "-L%s", ( dir ) ); \
      PUSH_DUP( szLibArg ); \
   } while( 0 )

   /* --- Build the linker argument vector (platform-specific) --- */

#if defined( __MINGW32__ ) || defined( _WIN32 )
   {
      /* Two supported layouts:
       *   - Dev tree:  szLibDir = <prefix>/lib/win/mingw64, MinGW CRT
       *                + Win32 import libs in sibling lib/win/mingw64-rt/
       *   - Release zip (flat): szLibDir = <prefix>/lib, everything merged
       *                in that one dir.
       * Detect by basename: if szLibDir ends in "/mingw64", use the
       * "-rt" sibling; otherwise szRtDir == szLibDir (flat). */
      char szRtDir[ 4096 ];
      {
         char   szTmp[ 4096 ];
         char * p;
         snprintf( szTmp, sizeof( szTmp ), "%s", szLibDir );
         for( p = szTmp; *p; ++p )
            if( *p == '\\' ) *p = '/';
         p = szTmp + strlen( szTmp );
         while( p > szTmp && *( p - 1 ) == '/' ) *--p = '\0';
         p = strrchr( szTmp, '/' );
         if( p && strcmp( p + 1, "mingw64" ) == 0 )
            snprintf( szRtDir, sizeof( szRtDir ), "%.*s/mingw64-rt",
                      ( int )( p - szTmp ), szTmp );
         else
            snprintf( szRtDir, sizeof( szRtDir ), "%s", szLibDir );
      }

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

      /* User-supplied library search paths (-L<dir> CLI option) */
      {
         int j;
         for( j = 0; j < nUserLibDirCount; ++j )
            PUSH_LDIR( aszUserLibDirs[ j ] );
      }

      /* Harbour runtime archives — --start-group handles circular references */
      argv[ argc++ ] = "--start-group";

      for( i = 0; i < sizeof( s_hbRuntimeLibs ) / sizeof( s_hbRuntimeLibs[ 0 ] ); ++i )
      {
         snprintf( szLibArg, sizeof( szLibArg ), "-l%s", s_hbRuntimeLibs[ i ] );
         PUSH_DUP( szLibArg );
      }

      argv[ argc++ ] = "--end-group";

      /* User-supplied libraries (-uselib=<name> CLI option) — after runtime
       * libs so that symbol resolution lets user libs depend on the runtime. */
      {
         int j;
         for( j = 0; j < nUserLibNameCount; ++j )
         {
            snprintf( szLibArg, sizeof( szLibArg ), "-l%s", aszUserLibNames[ j ] );
            PUSH_DUP( szLibArg );
         }
      }

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

      rc = hb_lld_link_mingw( argc, argv );
   }

#elif defined( __APPLE__ )
   {
      const char * pszSdk = hb_macos_sdk_path();
      char szCrt1[ 4096 ];
      char szGtObj[ 4096 ];

      if( pszSdk == NULL )
      {
         fprintf( stderr, "harbour -GL: xcrun --show-sdk-path failed. "
                          "Install Xcode Command Line Tools:\n"
                          "  xcode-select --install\n" );
         return 1;
      }

      snprintf( szCrt1, sizeof( szCrt1 ), "%s/usr/lib/crt1.o", pszSdk );
      /* hb_llvmgtstd.o is built next to the Harbour runtime libs by T4. */
      snprintf( szGtObj, sizeof( szGtObj ), "%s/hb_llvmgtstd.o", szLibDir );

      argv[ argc++ ] = "ld64.lld";
      argv[ argc++ ] = "-arch";
      argv[ argc++ ] = "x86_64";
      argv[ argc++ ] = "-platform_version";
      argv[ argc++ ] = "macos";
      argv[ argc++ ] = "13.0.0";
      argv[ argc++ ] = "13.0.0";
      argv[ argc++ ] = "-syslibroot";
      argv[ argc++ ] = pszSdk;
      argv[ argc++ ] = "-o";
      argv[ argc++ ] = szExePath;
      argv[ argc++ ] = szCrt1;
      argv[ argc++ ] = szObjPath;
      argv[ argc++ ] = szGtObj;
      argv[ argc++ ] = "-u";
      argv[ argc++ ] = "_HB_FUN_HB_GT_STD_DEFAULT";   /* Mach-O underscore */

      PUSH_LDIR( szLibDir );   /* lib/darwin/clang (Harbour runtime) */

      /* User-supplied library search paths (-L<dir> CLI option) */
      {
         int j;
         for( j = 0; j < nUserLibDirCount; ++j )
            PUSH_LDIR( aszUserLibDirs[ j ] );
      }

      /* No --start-group / --end-group on Mach-O — ld64 resolves
       * forward references via multi-pass scanning automatically. */
      {
         int j;
         char szLibArg2[ 64 ];
         for( j = 0; j < ( int )( sizeof( s_hbRuntimeLibs ) /
                                   sizeof( s_hbRuntimeLibs[ 0 ] ) ); ++j )
         {
            snprintf( szLibArg2, sizeof( szLibArg2 ), "-l%s",
                      s_hbRuntimeLibs[ j ] );
            PUSH_DUP( szLibArg2 );
         }
      }

      /* User-supplied libraries (-uselib=<name> CLI option) */
      {
         int j;
         for( j = 0; j < nUserLibNameCount; ++j )
         {
            snprintf( szLibArg, sizeof( szLibArg ), "-l%s", aszUserLibNames[ j ] );
            PUSH_DUP( szLibArg );
         }
      }

      argv[ argc++ ] = "-lSystem";
      argv[ argc++ ] = "-lc++";

      rc = hb_lld_link_macho( argc, argv );
   }

#else
#  error "Plan 2 (in-process EXE) not supported on this platform yet."
#endif

#undef PUSH_LDIR
#undef PUSH_DUP

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
 * Fill szBuf with the absolute path of the Harbour runtime lib directory,
 * derived relative to the running harbour.exe.
 *
 * Release-zip layout (flat):
 *   <prefix>/bin/harbour.exe
 *   <prefix>/lib/      <- what we return (Harbour runtime + MinGW CRT merged)
 *
 * Dev-tree fallback layout (Plan 2 original Windows tree):
 *   <prefix>/bin/win/mingw64/harbour.exe
 *   <prefix>/lib/win/mingw64/   <- alternative return
 *
 * Strategy: get the exe path via GetModuleFileNameA, normalise slashes,
 * strip the exe filename. Try the flat layout first (sibling lib/ next to
 * bin/). If that lib/ doesn't exist, fall back to the dev-tree layout
 * (walk up 3 dirs then append lib/win/mingw64). This lets the same
 * harbour.exe run from the release zip AND from the dev build tree.
 *
 * Windows-only (Plan 2 is x86_64 Windows only).
 */
void hb_llvmRuntimeLibDir( char * szBuf, int nBufLen )
{
#if defined( _WIN32 ) || defined( __WIN32__ ) || defined( WIN32 )
   char   szExe[ 4096 ];
   char   szTry[ 4096 ];
   char * p;
   int    i;
   DWORD  attr;

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
      *p = '\0';   /* szExe now holds the bin directory */

   /* Flat layout attempt: walk up one dir (out of bin/), append /lib. */
   {
      char szBin[ 4096 ];
      snprintf( szBin, sizeof( szBin ), "%s", szExe );
      p = strrchr( szBin, '/' );
      if( p )
         *p = '\0';
      snprintf( szTry, sizeof( szTry ), "%s/lib", szBin );
      attr = GetFileAttributesA( szTry );
      if( attr != INVALID_FILE_ATTRIBUTES &&
          ( attr & FILE_ATTRIBUTE_DIRECTORY ) )
      {
         snprintf( szBuf, ( size_t ) nBufLen, "%s", szTry );
         return;
      }
   }

   /* Dev-tree fallback: walk up three directory components (mingw64, win,
    * bin) to reach <prefix>, then append lib/win/mingw64. */
   for( i = 0; i < 3; ++i )
   {
      p = strrchr( szExe, '/' );
      if( p )
         *p = '\0';
      else
      {
         szBuf[ 0 ] = '.';
         szBuf[ 1 ] = '\0';
         return;
      }
   }

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
 * an executable. Registers the real LLVM back-end implementations into the
 * dispatch table defined in libhbcplr.a (g_hb_llvm_backend).
 *
 * Using GCC's __attribute__((constructor)) ensures this runs before main(),
 * so the table is filled before any compiler call reaches genllvm.c.
 *
 * NOT static: macOS ld64 has no --whole-archive equivalent. To force the
 * archive member to be pulled, src/main/Makefile passes -u _hb_llvmBackendInit
 * on macOS; that requires the symbol be externally visible. On Windows,
 * --whole-archive drags it in regardless of visibility. extern "C" linkage
 * (the file is #include'd from a .cpp via `extern "C" {}`) keeps the
 * Mach-O symbol name as plain `_hb_llvmBackendInit` with no C++ mangling.
 */
extern void hb_llvmBackendInit( void ) __attribute__((constructor));
extern void hb_llvmBackendInit( void )
{
   static const HB_LLVM_BACKEND s_backend = {
      hb_llvmEmitObject,
      hb_llvmLinkExe,
      hb_llvmRuntimeLibDir
   };
   hb_llvmBackendRegister( &s_backend );
}
