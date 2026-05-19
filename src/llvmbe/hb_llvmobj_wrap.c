/* Wrapper that includes the LLVM back-end object emitter / linker from
 * src/compiler/ into the libhbllvm.a archive.  Having it here (instead of
 * in libhbcplr.a) means hbrun and other tools that link libhbcplr.a do not
 * drag in the LLVM + LLD static libraries. */
#include "../compiler/hb_llvmobj.c"
