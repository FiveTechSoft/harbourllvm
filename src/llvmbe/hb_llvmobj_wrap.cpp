/* Wrapper that includes the LLVM back-end object emitter / linker from
 * src/compiler/ into the libhbllvm.a archive. Having it here (instead of
 * in libhbcplr.a) means hbrun and other tools that link libhbcplr.a do not
 * drag in the LLVM + LLD static libraries.
 *
 * .cpp because the macOS LLVM 19 official tarball's llvm-c headers
 * transitively pull in C++ STL headers (std::optional etc.) that won't
 * compile under a C compiler. Wrapping the .c source via #include from a
 * .cpp lets clang++/g++ accept the headers. The included C source is
 * itself C-compatible; C++ accepts it as a superset. extern "C" linkage
 * is applied to keep the exported symbols C-callable. */
extern "C" {
#include "../compiler/hb_llvmobj.c"
}
