ROOT := ../../

include $(TOP)$(ROOT)config/llvm-paths.mk

C_SOURCES := \

C_MAIN := harbour.c

ifneq ($(filter $(HB_PLATFORM),win wce),)
RC_SOURCES := harbour.rc
endif

LIBS := \
   hbcplr \
   hbpp \
   hbnortl \
   hbcommon \

# Embedded LLVM backend: link libhbllvm.a (which contains libLLVM + LLD)
# only when HB_HAS_LLVM=yes. When absent, hb_llvmstub.c (compiled into
# libhbcplr.a) supplies a NULL-initialised g_hb_llvm_backend dispatch
# table; genllvm.c's NULL-check then skips the .ll -> .o -> .exe path,
# leaving `harbour -GL` to emit the .ll only. `harbour -GC` is fully
# functional regardless.
ifeq ($(HB_HAS_LLVM),yes)

HB_LLVM_LIBDIR  := -L$(shell $(HB_LLVM_CONFIG) --libdir)
HB_LLVM_SYSLIBS := -lstdc++ $(shell $(HB_LLVM_CONFIG) --system-libs)
HB_LLVM_LIBS    := -llldMinGW -llldCOFF -llldCommon
HB_LLVM_LIBS    += $(shell $(HB_LLVM_CONFIG) --libs all)

# --whole-archive forces libhbllvm.a to be fully linked (including its
# __attribute__((constructor)) that registers the LLVM back-end dispatch table).
# Without --whole-archive the linker would omit unreferenced translation units
# and the constructor would never run.
LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(HB_LLVM_LIBDIR) -Wl,--start-group -Wl,--whole-archive -lhbllvm -Wl,--no-whole-archive $(LDLIBS) $(HB_LLVM_LIBS) -Wl,--end-group $(HB_LLVM_SYSLIBS) $(LDSTRIP)

endif

include $(TOP)$(ROOT)config/bin.mk
