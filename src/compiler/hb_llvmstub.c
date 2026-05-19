/*
 * LLVM back-end dispatch table — defined in libhbcplr.a.
 *
 * All function pointers are NULL by default (no LLVM back end linked).
 * When libhbllvm.a is linked (as in harbour.exe), its module constructor
 * calls hb_llvmBackendRegister() to fill the table with the real
 * implementations.  Tools that link libhbcplr.a without libhbllvm.a
 * (hbrun, hbmk2, etc.) get a zero-initialised table; genllvm.c checks
 * for NULL before calling and skips the object/exe steps in that case.
 */

#include "hb_llvmobj.h"

#include <stddef.h>   /* NULL */

/* The dispatch table lives in libhbcplr.a so every binary that links the
 * compiler library gets the right default without needing libhbllvm.a. */
HB_LLVM_BACKEND g_hb_llvm_backend = { NULL, NULL, NULL };

void hb_llvmBackendRegister( const HB_LLVM_BACKEND * pBackend )
{
   g_hb_llvm_backend = *pBackend;
}
