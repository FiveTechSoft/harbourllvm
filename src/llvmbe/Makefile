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
# hb_llvmobj_wrap.cpp wraps the C source via extern "C" because the macOS
# official LLVM 19 tarball's llvm-c headers transitively pull in C++ STL
# (std::optional etc.) that won't compile under a C compiler. The wrapper
# is compiled as C++ on all platforms; symbol linkage stays C.
CPP_SOURCES := \
   hb_llvmobj_wrap.cpp \
   hb_lldshim_wrap.cpp \

endif

LIBNAME := hbllvm

include $(TOP)$(ROOT)config/lib.mk

# macOS: compile hb_llvmgtstd.c -> lib/darwin/clang/hb_llvmgtstd.o (Mach-O).
# The output lands next to the Harbour runtime archives so hb_llvmobj.c can
# find it at link time via szLibDir (lib/darwin/clang/).
# Guard: only when HB_HAS_LLVM=yes (LLVM SDK is present) on a darwin host.
ifeq ($(HB_PLATFORM),darwin)
ifeq ($(HB_HAS_LLVM),yes)

HB_LLVMGT_OBJ := $(TOP)$(ROOT)lib/darwin/clang/hb_llvmgtstd.o
HB_LLVMGT_SRC := $(TOP)$(ROOT)src/llvmbe/runtime/hb_llvmgtstd.c

first:: $(HB_LLVMGT_OBJ)

$(HB_LLVMGT_OBJ): $(HB_LLVMGT_SRC)
	$(CC) -c -O2 -fno-common -I$(TOP)$(ROOT)include -o $@ $<

endif
endif
