/* hb_llvmgtwvt.c — registers gtwvt (Windows virtual terminal GT) as the
 * default driver for -GL output. Selected via -gtwvt. */
#define HB_LLVMGT_NAME      "WVT"
#define HB_LLVMGT_INIT_SYM  _hb_llvm_gt_setdef_wvt_
#include "hb_llvmgt_template.h"
