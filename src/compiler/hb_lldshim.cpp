/* C++ shim: wrap one LLD driver per host so C code can link a native
 * executable without spawning an external linker. Each host pulls in
 * exactly the LLD driver it needs (and corresponding -llldXxx library
 * in src/main/Makefile) — registering a driver for which the matching
 * library isn't linked produces 'undefined symbol: lld::xxx::link'. */
#include "hb_lldshim.h"

#include "lld/Common/Driver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

#if defined( __APPLE__ )

LLD_HAS_DRIVER( macho )   /* declares lld::macho::link */

extern "C" int hb_lld_link_macho( int argc, const char ** argv )
{
   llvm::ArrayRef< const char * > args( argv, static_cast< size_t >( argc ) );

   bool ok = lld::macho::link( args,
                                llvm::outs(),
                                llvm::errs(),
                                /* exitEarly  */ false,
                                /* disableOut */ false );
   return ok ? 0 : 1;
}

#elif defined( __linux__ )

LLD_HAS_DRIVER( elf )   /* declares lld::elf::link */

extern "C" int hb_lld_link_elf( int argc, const char ** argv )
{
   llvm::ArrayRef< const char * > args( argv, static_cast< size_t >( argc ) );

   bool ok = lld::elf::link( args,
                             llvm::outs(),
                             llvm::errs(),
                             /* exitEarly  */ false,
                             /* disableOut */ false );
   return ok ? 0 : 1;
}

#elif defined( _WIN32 ) || defined( __MINGW32__ )

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

#endif
