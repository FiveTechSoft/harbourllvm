/* hb_llvmgtstd.c — startup stub compiled into every hb_llvmLinkExe output.
 * Sets GTSTD as the default GT driver so programs linked via the embedded LLD
 * back end write to stdout rather than opening a Windows console window.
 *
 * This replicates what hbmk2 -gtstd injects into its auto-generated C stub.
 */
#include "hbvmpub.h"   /* brings in HB_EXPORT and Harbour type definitions */
#include "hbinit.h"
#include "hbapi.h"

HB_CALL_ON_STARTUP_BEGIN( _hb_llvm_gt_setdef_ )
   hb_vmSetDefaultGT( "STD" );
HB_CALL_ON_STARTUP_END( _hb_llvm_gt_setdef_ )

#if defined( HB_PRAGMA_STARTUP )
   #pragma startup _hb_llvm_gt_setdef_
#elif defined( HB_DATASEG_STARTUP )
   #define HB_DATASEG_BODY    HB_DATASEG_FUNC( _hb_llvm_gt_setdef_ )
   #include "hbiniseg.h"
#endif
