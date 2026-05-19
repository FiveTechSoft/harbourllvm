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

HB_LLVM_LDFLAGS := -L$(shell $(HB_LLVM_CONFIG) --libdir)
HB_LLVM_LDFLAGS += -Wl,--start-group
HB_LLVM_LDFLAGS += -llldMinGW -llldCOFF -llldCommon
HB_LLVM_LDFLAGS += $(shell $(HB_LLVM_CONFIG) --libs all)
HB_LLVM_LDFLAGS += -Wl,--end-group
HB_LLVM_LDFLAGS += -lstdc++ $(shell $(HB_LLVM_CONFIG) --system-libs)

# Custom LD_RULE so LLVM/LLD libs are appended after the Harbour libs.
# Must be set before config/bin.mk includes config/rules.mk (which guards with ifeq).
LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(LDLIBS) $(HB_LLVM_LDFLAGS) $(LDSTRIP)

include $(TOP)$(ROOT)config/bin.mk
