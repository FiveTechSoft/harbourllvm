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

HB_LLVM_LIBDIR  := -L$(shell $(HB_LLVM_CONFIG) --libdir)
HB_LLVM_SYSLIBS := -lstdc++ $(shell $(HB_LLVM_CONFIG) --system-libs)
HB_LLVM_LIBS    := -llldMinGW -llldCOFF -llldCommon
HB_LLVM_LIBS    += $(shell $(HB_LLVM_CONFIG) --libs all)

# --whole-archive forces libhbllvm.a to be fully linked (including its
# __attribute__((constructor)) that registers the LLVM back-end dispatch table).
# Without --whole-archive the linker would omit unreferenced translation units
# and the constructor would never run.
LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(HB_LLVM_LIBDIR) -Wl,--start-group -Wl,--whole-archive -lhbllvm -Wl,--no-whole-archive $(LDLIBS) $(HB_LLVM_LIBS) -Wl,--end-group $(HB_LLVM_SYSLIBS) $(LDSTRIP)

include $(TOP)$(ROOT)config/bin.mk
