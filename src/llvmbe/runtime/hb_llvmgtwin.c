/* hb_llvmgtwin.c — registers gtwin (Windows native console GT) as the
 * default driver for -GL output built via the embedded LLD back end.
 * Pre-shipped as lib/win/mingw64-rt/hb_llvmgtwin.o; selected via -gtwin. */
#define HB_LLVMGT_NAME      "WIN"
#define HB_LLVMGT_INIT_SYM  _hb_llvm_gt_setdef_win_
#include "hb_llvmgt_template.h"
