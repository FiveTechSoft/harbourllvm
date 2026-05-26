ROOT := ../../

include $(TOP)$(ROOT)config/llvm-paths.mk
CXX            := g++
HB_USER_CFLAGS += $(HB_LLVM_CFLAGS)
HB_USER_CFLAGS += -I$(TOP)$(ROOT)src/compiler

# LLVM 19 headers use C++17 features (std::optional, trailing return).
# macOS clang from Xcode CLT defaults to gnu++98; force C++17 so the
# wrapper .cpp files compile. Universal — Windows MinGW gcc accepts -std
# unconditionally too.
HB_USER_CFLAGS += -std=c++17

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

# Compile hb_llvmgt<name>.c -> <lib-dir>/hb_llvmgt<name>.o so the embedded
# LLVM back-end can find them at link time via szLibDir. Output goes next
# to the Harbour runtime archives. One object per supported GT driver so
# that `harbour -GL -gt<name>` can pick a non-default driver at link time.
#
# Linux/macOS GT set: gtstd (default), gtcgi, gtpca, gttrm — matches the
# `s_hbRuntimeLibs` list in src/compiler/hb_llvmobj.c for these platforms.
# (Windows pre-ships hb_llvmgt{std,win,wvt,gui,cgi,pca}.o under
#  lib/win/mingw64-rt/ — its set includes the Win32-only gtwin/wvt/gui.)
#
# Guard: only when HB_HAS_LLVM=yes (LLVM SDK is present).
ifeq ($(HB_HAS_LLVM),yes)

HB_LLVMGT_NAMES := std cgi pca trm

ifeq ($(HB_PLATFORM),darwin)
HB_LLVMGT_DIR := $(TOP)$(ROOT)lib/darwin/clang
else ifeq ($(HB_PLATFORM),linux)
HB_LLVMGT_DIR := $(TOP)$(ROOT)lib/linux/gcc
endif

ifdef HB_LLVMGT_DIR
HB_LLVMGT_OBJS := $(foreach n,$(HB_LLVMGT_NAMES),$(HB_LLVMGT_DIR)/hb_llvmgt$(n).o)

first:: $(HB_LLVMGT_OBJS)

$(HB_LLVMGT_DIR)/hb_llvmgt%.o: $(TOP)$(ROOT)src/llvmbe/runtime/hb_llvmgt%.c
	$(CC) -c -O2 -fno-common -I$(TOP)$(ROOT)include -o $@ $<
endif

endif
