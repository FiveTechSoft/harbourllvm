/* hb_llvmgtcgi.c — registers gtcgi as the default GT for -GL output.
 * Selected via -gtcgi. Cross-platform. */
#define HB_LLVMGT_NAME      "CGI"
#define HB_LLVMGT_INIT_SYM  _hb_llvm_gt_setdef_cgi_
#include "hb_llvmgt_template.h"
