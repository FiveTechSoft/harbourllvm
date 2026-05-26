/* Embedded LLVM back end: turn LLVM IR text into a native executable. */
#ifndef HB_LLVMOBJ_H_
#define HB_LLVMOBJ_H_

#if defined( __cplusplus )
extern "C" {
#endif

/* Parse the LLVM IR text file szLLPath and emit a native object file to
 * szObjPath. Returns 0 on success; on failure returns non-zero and writes a
 * message to stderr. */
int hb_llvmEmitObject( const char * szLLPath, const char * szObjPath );

/* Link szObjPath with the Harbour runtime archives found under szLibDir into
 * the executable szExePath, using the embedded LLD driver.
 * aszUserLibDirs/nUserLibDirCount: extra -L<dir> entries from -L<dir> CLI option.
 * aszUserLibNames/nUserLibNameCount: extra -l<name> entries from -uselib= CLI option.
 * szGtName: GT driver to register as default (lowercase: "std","win","wvt",
 *           "cgi","pca","gui"); NULL falls back to "std" for compatibility.
 *           Picks the hb_llvmgt<name>.o startup stub and force-undefines
 *           HB_FUN_HB_GT_<UPPER>_DEFAULT to pull libgt<name>.a into the link.
 * Returns 0 on success, non-zero on failure. */
int hb_llvmLinkExe( const char * szObjPath, const char * szLibDir,
                    const char * szExePath,
                    const char * const * aszUserLibDirs, int nUserLibDirCount,
                    const char * const * aszUserLibNames, int nUserLibNameCount,
                    const char * szGtName );

/* Fill szBuf with the absolute path of the Harbour runtime lib directory
 * (lib/win/mingw64), resolved relative to the running harbour.exe.
 * harbour.exe lives at <prefix>/bin/win/mingw64/harbour.exe;
 * the runtime libs are at <prefix>/lib/win/mingw64. */
void hb_llvmRuntimeLibDir( char * szBuf, int nBufLen );

/*
 * Function-pointer dispatch table for the LLVM back end.
 *
 * libhbcplr.a defines the table with NULL pointers (no LLVM).
 * libhbllvm.a (linked only into harbour.exe) calls
 * hb_llvmBackendRegister() from a module constructor to fill the table.
 *
 * genllvm.c calls through the table; if the pointers are NULL the
 * function is absent (non-LLVM build) and the compiler emits only the .ll.
 */
typedef int  (*HB_LLVM_EMIT_FN)( const char *, const char * );
typedef int  (*HB_LLVM_LINK_FN)( const char *, const char *, const char *,
                                  const char * const *, int,
                                  const char * const *, int,
                                  const char * );
typedef void (*HB_LLVM_RTDIR_FN)( char *, int );

typedef struct _HB_LLVM_BACKEND
{
   HB_LLVM_EMIT_FN  emitObject;
   HB_LLVM_LINK_FN  linkExe;
   HB_LLVM_RTDIR_FN runtimeLibDir;
} HB_LLVM_BACKEND;

/* Defined in hb_llvmstub.c (libhbcplr.a); all pointers are NULL initially. */
extern HB_LLVM_BACKEND g_hb_llvm_backend;

/* Called by libhbllvm.a's module constructor to fill the table. */
void hb_llvmBackendRegister( const HB_LLVM_BACKEND * pBackend );

#if defined( __cplusplus )
}
#endif

#endif
