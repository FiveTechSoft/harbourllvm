# Location of the MinGW-ABI LLVM + LLD SDK built by tools/build-llvm.sh.
# Override on the command line if installed elsewhere.
HB_LLVM_PREFIX ?= c:/llvm-mingw
HB_LLVM_CONFIG := $(HB_LLVM_PREFIX)/bin/llvm-config
