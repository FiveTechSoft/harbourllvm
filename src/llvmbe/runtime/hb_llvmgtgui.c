/* hb_llvmgtgui.c — registers gtgui (Windows GUI subsystem GT) as the
 * default driver for -GL output. Selected via -gtgui. */
#define HB_LLVMGT_NAME      "GUI"
#define HB_LLVMGT_INIT_SYM  _hb_llvm_gt_setdef_gui_
#include "hb_llvmgt_template.h"
