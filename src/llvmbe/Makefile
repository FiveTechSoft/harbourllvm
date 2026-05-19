ROOT := ../../

include $(TOP)$(ROOT)config/llvm-paths.mk
CXX            := g++
HB_USER_CFLAGS += -I$(shell $(HB_LLVM_CONFIG) --includedir)
HB_USER_CFLAGS += -I$(TOP)$(ROOT)src/compiler

# Thin wrapper sources that #include the real files from src/compiler/.
# This keeps the LLVM+LLD objects out of libhbcplr.a so that hbrun and
# other tools linking libhbcplr.a do not need to link LLVM+LLD.
C_SOURCES := \
   hb_llvmobj_wrap.c \

CPP_SOURCES := \
   hb_lldshim_wrap.cpp \

LIBNAME := hbllvm

include $(TOP)$(ROOT)config/lib.mk
