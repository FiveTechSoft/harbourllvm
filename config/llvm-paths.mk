# Location of the MinGW-ABI LLVM + LLD SDK built by tools/build-llvm.sh.
# Override on the command line if installed elsewhere.
HB_LLVM_PREFIX ?= c:/llvm-mingw
HB_LLVM_CONFIG := $(HB_LLVM_PREFIX)/bin/llvm-config

# Auto-detect whether the embedded LLVM backend can be built. The runtime
# code in src/compiler/ (genllvm.c, hb_pcdec.c, hb_llvmstub.c) is portable
# and never includes LLVM headers directly, so standard Harbour builds
# cleanly on platforms without LLVM installed — `harbour -GC` is fully
# functional. Only `harbour -GL` (the in-process LLVM-to-native EXE path)
# requires the SDK. See README's FAQ.
HB_HAS_LLVM := $(if $(wildcard $(HB_LLVM_CONFIG)),yes,no)

ifeq ($(HB_HAS_LLVM),yes)
   HB_LLVM_CFLAGS := -I$(shell $(HB_LLVM_CONFIG) --includedir)
else
   HB_LLVM_CFLAGS :=
endif
