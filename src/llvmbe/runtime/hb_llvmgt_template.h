/* hb_llvmgt_template.h — shared body for all hb_llvmgt<name>.c startup stubs.
 *
 * The wrapper .c defines two macros before #include'ing this header:
 *   HB_LLVMGT_NAME      string literal passed to hb_vmSetDefaultGT, e.g. "WIN"
 *   HB_LLVMGT_INIT_SYM  unique startup function identifier (no quotes),
 *                       e.g. _hb_llvm_gt_setdef_win_
 *
 * The result is exactly the same shape as the original hb_llvmgtstd.c: a
 * single HB_CALL_ON_STARTUP entry that registers the chosen GT as the
 * runtime default before main() returns control to user code.
 */
#include "hbvmpub.h"
#include "hbinit.h"
#include "hbapi.h"

HB_CALL_ON_STARTUP_BEGIN( HB_LLVMGT_INIT_SYM )
   hb_vmSetDefaultGT( HB_LLVMGT_NAME );
HB_CALL_ON_STARTUP_END( HB_LLVMGT_INIT_SYM )

#if defined( HB_PRAGMA_STARTUP )
   #pragma startup HB_LLVMGT_INIT_SYM
#elif defined( HB_DATASEG_STARTUP )
   #define HB_DATASEG_BODY    HB_DATASEG_FUNC( HB_LLVMGT_INIT_SYM )
   #include "hbiniseg.h"
#endif
