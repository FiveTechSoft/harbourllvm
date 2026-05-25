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

HB_LLVM_LIBDIR := -L$(shell $(HB_LLVM_CONFIG) --libdir)

ifeq ($(HB_PLATFORM),linux)
# Linux x86_64 (ELF / ld.lld). Link the system LLVM 18 + LLD static libs.
# --whole-archive on libhbllvm.a drags in the __attribute__((constructor))
# that registers the LLVM back-end dispatch table.
# libstdc++/libgcc are linked dynamically (system default) — same approach as
# the unmodified Harbour build, keeping the harbour binary small (~120 MB
# vs ~150 MB with -static-libstdc++).
HB_LLVM_LIBS    := -llldELF -llldCommon
HB_LLVM_LIBS    += $(shell $(HB_LLVM_CONFIG) --libs all)
# -lz: liblldELF.a calls deflate (compresses .debug_* sections).
# -lzstd: same, optional zstd compression of debug info.
# -ltinfo: terminal info needed by some LLVM diagnostic paths.
HB_LLVM_SYSLIBS := $(shell $(HB_LLVM_CONFIG) --system-libs) -lstdc++ -lz -lzstd -ltinfo
LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(HB_LLVM_LIBDIR) -Wl,--start-group -Wl,--whole-archive -lhbllvm -Wl,--no-whole-archive $(LDLIBS) $(HB_LLVM_LIBS) -Wl,--end-group $(HB_LLVM_SYSLIBS) $(LDSTRIP)
else ifeq ($(HB_PLATFORM),darwin)
# macOS (Mach-O / ld64): use MachO + Core LLD flavours; link against libc++.
# ld64 has no --whole-archive equivalent — use -u _hb_llvmBackendRegister to
# force-pull the __attribute__((constructor)) from libhbllvm.a that registers
# the LLVM back-end dispatch table at startup.
HB_LLVM_LIBS    := -llldMachO -llldCommon
HB_LLVM_LIBS    += $(shell $(HB_LLVM_CONFIG) --libs all)
# Statically link the LLVM SDK's libc++/libc++abi/libunwind. The dylib variant
# fails at startup: dyld pulls /usr/lib/libc++abi.dylib transitively via
# libSystem.B.dylib BEFORE @rpath/libc++.1.dylib loads from the SDK, producing
# an ABI mix (SDK libc++ inline definitions vs. system libc++abi vtables) that
# crashes in DoIOSInit::imbue() with rip=0 (NULL virtual dispatch through
# std::__1::collate<char>'s vtable). Static linking sidesteps the conflict
# entirely — harbour becomes self-contained and has no @rpath dependency.
# -nostdlib++ suppresses clang's auto-injected -lc++.
HB_LLVM_SYSLIBS := -nostdlib++ \
    $(HB_LLVM_PREFIX)/lib/libc++.a \
    $(HB_LLVM_PREFIX)/lib/libc++abi.a \
    $(HB_LLVM_PREFIX)/lib/libunwind.a \
    $(shell $(HB_LLVM_CONFIG) --system-libs)
# Use the LLVM SDK's bundled ld64.lld via -fuse-ld=lld + -B. The system
# /usr/bin/ld on macOS hosts with older Xcode CLT (14.x) can't parse
# LLVM 19 bitcode in libLLVM*.a archives ("Unknown attribute kind (86)").
# The SDK ships its own ld64.lld in $(HB_LLVM_PREFIX)/bin which understands
# its own bitcode format. -B passes that dir to clang for linker lookup.
HB_LLVM_LD_DARWIN := -fuse-ld=lld -B$(HB_LLVM_PREFIX)/bin
LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(HB_LLVM_LD_DARWIN) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(HB_LLVM_LIBDIR) -u _hb_llvmBackendInit -lhbllvm $(LDLIBS) $(HB_LLVM_LIBS) $(HB_LLVM_SYSLIBS) $(LDSTRIP)
else
# Windows MinGW path (unchanged):
# Standalone harbour.exe: statically link the MinGW C/C++ runtime +
# winpthread so the binary does not depend on libgcc_s_seh-1.dll /
# libstdc++-6.dll / libwinpthread-1.dll being on PATH. Win32 system libs
# (kernel32, user32, advapi32, ...) remain dynamic — they ARE the OS.
#
# Three pieces are needed because the explicit -lstdc++ from
# HB_LLVM_SYSLIBS would otherwise resolve to libstdc++-6.dll and pull
# libwinpthread-1.dll transitively. So:
#  - drop the explicit -lstdc++ (gcc driver re-adds it as a static
#    archive when -static-libstdc++ is set);
#  - keep -static-libgcc / -static-libstdc++ early in LD_RULE for the
#    driver-injected references;
#  - end the link with -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
#    so any transitive reference resolves against the static archives.
# --whole-archive forces libhbllvm.a to be fully linked (including its
# __attribute__((constructor)) that registers the LLVM back-end dispatch table).
# Without --whole-archive the linker would omit unreferenced translation units
# and the constructor would never run.
HB_LLVM_LIBS    := -llldMinGW -llldCOFF -llldCommon
HB_LLVM_LIBS    += $(shell $(HB_LLVM_CONFIG) --libs all)
HB_LLVM_STATIC_RUNTIME := -static-libgcc -static-libstdc++
HB_LLVM_SYSLIBS := $(shell $(HB_LLVM_CONFIG) --system-libs) -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
LD_RULE = $(LD) $(LDFLAGS) $(HB_LDFLAGS) $(HB_USER_LDFLAGS) $(HB_LLVM_STATIC_RUNTIME) $(LD_OUT)$(subst /,$(DIRSEP),$(BIN_DIR)/$@) $(^F) $(HB_LLVM_LIBDIR) -Wl,--start-group -Wl,--whole-archive -lhbllvm -Wl,--no-whole-archive $(LDLIBS) $(HB_LLVM_LIBS) -Wl,--end-group $(HB_LLVM_SYSLIBS) $(LDSTRIP)
endif

endif

include $(TOP)$(ROOT)config/bin.mk
