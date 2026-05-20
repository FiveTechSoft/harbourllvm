ROOT := ../../

include $(TOP)$(ROOT)config/llvm-paths.mk
CXX              := g++
HB_USER_CFLAGS   += -I$(shell $(HB_LLVM_CONFIG) --includedir)

#YACC_FLAGS := -p hb_comp
YACC_SOURCE := harbour.y
YACC_DEPEND := complex.c
YACC_HEADERS := \
   hbcomp.h \
   hbcompdf.h \
   hbsetup.h \
   hbpcode.h \
   hbdefs.h \
   hberrors.h \
   hbpp.h \

C_SOURCES := \
   cmdcheck.c \
   compi18n.c \
   complex.c \
   expropta.c \
   exproptb.c \
   genc.c \
   gencc.c \
   genllvm.c \
   genhrb.c \
   hb_llvmstub.c \
   hb_pcdec.c \
   hbcmplib.c \
   hbcomp.c \
   hbdbginf.c \
   hbdead.c \
   hbfix.c \
   hbfunchk.c \
   hbgenerr.c \
   hbident.c \
   hblbl.c \
   hbmain.c \
   hbopt.c \
   hbpcode.c \
   hbstripl.c \
   hbusage.c \
   ppcomp.c \

# hb_llvmobj.c and hb_lldshim.cpp live in src/llvmbe/Makefile (libhbllvm.a)
# so that hbrun and other tools that link libhbcplr.a do not drag in LLVM+LLD.
# hb_llvmstub.c provides weak fallback symbols so libhbcplr.a links standalone.

LIBNAME := hbcplr

include $(TOP)$(ROOT)config/lib.mk
