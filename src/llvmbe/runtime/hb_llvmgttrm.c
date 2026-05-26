/* hb_llvmgttrm.c — registers gttrm (UNIX terminal escape-sequence GT) as
 * the default driver for -GL output. Selected via -gttrm.
 * Linux/macOS only (gttrm is not built on Windows). */
#define HB_LLVMGT_NAME      "TRM"
#define HB_LLVMGT_INIT_SYM  _hb_llvm_gt_setdef_trm_
#include "hb_llvmgt_template.h"
