ROOT := ../../

include $(TOP)$(ROOT)config/llvm-paths.mk
CXX            := g++
HB_USER_CFLAGS += $(HB_LLVM_CFLAGS)
HB_USER_CFLAGS += -I$(TOP)$(ROOT)src/compiler

# Thin wrapper sources that #include the real files from src/compiler/.
# This keeps the LLVM+LLD objects out of libhbcplr.a so that hbrun and
# other tools linking libhbcplr.a do not need to link LLVM+LLD.
#
# When the LLVM SDK is absent (HB_HAS_LLVM=no — e.g. macOS/Linux build
# without llvm-config) the wrapper sources cannot compile. src/Makefile
# drops this subdir from DIRS in that case, so this guard is defensive.
ifeq ($(HB_HAS_LLVM),yes)
C_SOURCES := \
   hb_llvmobj_wrap.c \

CPP_SOURCES := \
   hb_lldshim_wrap.cpp \

endif

LIBNAME := hbllvm

include $(TOP)$(ROOT)config/lib.mk
