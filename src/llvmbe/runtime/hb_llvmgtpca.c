/* hb_llvmgtpca.c — registers gtpca (ANSI/PCA scriptable GT) as the default
 * GT for -GL output. Selected via -gtpca. Cross-platform. */
#define HB_LLVMGT_NAME      "PCA"
#define HB_LLVMGT_INIT_SYM  _hb_llvm_gt_setdef_pca_
#include "hb_llvmgt_template.h"
