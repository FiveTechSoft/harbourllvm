/* C++ shim: wrap lld::mingw::link so C code can link a PE executable
 * without spawning an external linker. */
#include "hb_lldshim.h"

#include "lld/Common/Driver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

LLD_HAS_DRIVER( mingw )   /* declares lld::mingw::link */

extern "C" int hb_lld_link_mingw( int argc, const char ** argv )
{
   llvm::ArrayRef< const char * > args( argv, static_cast< size_t >( argc ) );

   bool ok = lld::mingw::link( args,
                               llvm::outs(),
                               llvm::errs(),
                               /* exitEarly  */ false,
                               /* disableOut */ false );
   return ok ? 0 : 1;
}
