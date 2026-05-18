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
 * Returns 0 on success, non-zero on failure. */
int hb_llvmLinkExe( const char * szObjPath, const char * szLibDir,
                    const char * szExePath );

#if defined( __cplusplus )
}
#endif

#endif
