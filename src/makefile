targets=../bin ../bin/libzray.so ../bin/zray_runtime.ll ../bin/libzray_check_sf.so ../bin/libzray_noinline.so ../bin/zray_post_process
all: $(targets)

zray_objs=zray_pass.o \
		   zray_util.o \
		   zray_loop.o \
		   zray_parse.o \
		   zray_codegen.o

zray_check_objs=zray_check_sf.o

# --- Toolchain -------------------------------------------------------------
# Normally set by `. ./setupEnv.sh`, but this makefile is usable standalone:
# if CUSTOM_CONFIG / CUSTOM_CC are unset it falls back to LLVM_BIN, then to
# whatever llvm-config is on PATH.
#
# Both are validated below. Without the check, an unset CUSTOM_CONFIG expands
# to `$(shell --includedir)` — make reports "--includedir: No such file or
# directory" and passes a bare `-I` that silently swallows the next flag.
# Prefer llvm-config on PATH; otherwise fall back to the Debian/Ubuntu location
# for llvm-15-dev, which is the same default setupEnv.sh uses.
LLVM_BIN ?= $(or $(patsubst %/,%,$(dir $(shell command -v llvm-config 2>/dev/null))),/usr/lib/llvm-15/bin)
CUSTOM_CONFIG ?= $(if $(LLVM_BIN),$(LLVM_BIN)/llvm-config,llvm-config)
CUSTOM_CC ?= $(if $(LLVM_BIN),$(LLVM_BIN)/clang++,clang++)

ifeq ($(shell command -v $(CUSTOM_CONFIG) 2>/dev/null),)
$(error No usable llvm-config at '$(CUSTOM_CONFIG)'. Run `. ./setupEnv.sh` from \
the repo root, or set LLVM_BIN to your LLVM 15 bin/ directory (Debian/Ubuntu: \
`sudo apt install llvm-15-dev clang-15`, then LLVM_BIN=/usr/lib/llvm-15/bin))
endif

ifeq ($(shell command -v $(CUSTOM_CC) 2>/dev/null),)
$(error No usable C++ compiler at '$(CUSTOM_CC)'. If CUSTOM_CC is exported by \
your shell profile it may point at a stale toolchain; unset it and run \
`. ./setupEnv.sh` from the repo root)
endif

# Include paths come from llvm-config so any LLVM 15 install works (distro
# package or a local build).
cxxFlags=-I$(shell ${CUSTOM_CONFIG} --includedir) -std=c++14 -D_GNU_SOURCE -D_DEBUG -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -g
ldFlags=$(shell ${CUSTOM_CONFIG} --ldflags)

CLANG_PATH=$(shell ${CUSTOM_CONFIG} --bindir)
# Only used by the commented-out RISC-V cross-compile recipe at the bottom of
# this file. Override with: make GNU_PATH=/path/to/riscv-gnu-toolchain/build
GNU_PATH?=/path/to/riscv-gnu-toolchain/build

# Normally exported by setupEnv.sh; default it so a standalone `make` works.
# An unset MACHINE_ARCH used to fall through both arms and leave `flags` empty,
# dropping -I../include, -O2 and -fPIC — which surfaced as a missing header.
MACHINE_ARCH ?= $(shell uname -m)

commonFlags=-O2 -fPIC -I../include -g -MMD -Wall
ifeq ($(MACHINE_ARCH),x86_64)
flags=$(commonFlags)
else ifeq ($(MACHINE_ARCH),riscv64)
flags=$(commonFlags) -I/usr/include/riscv64-linux-gnu -march=rv64g -fno-rtti
else
# Unknown host: build with the common flags rather than silently dropping them.
$(warning zray: warning: unrecognized MACHINE_ARCH '$(MACHINE_ARCH)'; using default flags)
flags=$(commonFlags)
endif

../bin:
	mkdir $@

zray_util.o: zray_util.cc ../include/zray_util.h
zray_codegen.o: zray_codegen.cc ../include/zray_pass.h
zray_parse.o: zray_parse.cc ../include/zray_pass.h
zray_loop.o: zray_loop.cc ../include/zray_pass.h
zray_pass.o: zray_pass.cc ../include/zray_pass.h
noinline_pass.o: noinline_pass.cc ../include/noinline_pass.h ../include/zray_util.h
$(zray_objs):
	$(CUSTOM_CC) -c -o $@ $< $(cxxFlags) $(CFLAGS) $(flags)

zray_check_sf.o: zray_check_sf.cc ../include/zray_check_sf.h ../include/zray_util.h
$(zray_check_objs):
	$(CUSTOM_CC) -c -o $@ $< $(cxxFlags) $(CFLAGS) $(flags)

#Build compiler pass library
../bin/libzray.so: $(zray_objs) zray_util.o
	$(CUSTOM_CC) -shared -o $@ $^ $(cxxFlags) $(CFLAGS) $(ldFlags) $(flags)

../bin/libzray_check_sf.so: $(zray_check_objs) 
	$(CUSTOM_CC) -shared -o $@ $^ $(cxxFlags) $(CFLAGS) $(ldFlags) $(flags)

../bin/libzray_noinline.so: noinline_pass.cc zray_util.o
	$(CUSTOM_CC) -shared -o $@ $^ $(cxxFlags) $(CFLAGS) $(ldFlags) $(flags)

../bin/zray_post_process: post_process.cc
	$(CUSTOM_CC) -o $@ $^ $(cxxFlags) $(CFLAGS) $(ldFlags) $(flags)
#If we want to use libzip
#$(CUSTOM_CC) -o $@ $< -std=c++14 $(flags) $(CFLAGS) -S -emit-llvm -fverbose-asm -I../libzip/lib -I../libzip/build

#Build profiler runtime
../bin/zray_runtime.ll: zray_dyn.cc ../include/zray_dyn.h
	$(CUSTOM_CC) -o $@ $< -std=c++14 $(flags) $(CFLAGS) -g -S -emit-llvm -fverbose-asm

clean:
	rm -rf *.ll *.o *.d $(targets) *.zlog

-include *.d
#$(CC) -o tool_dyn_riscv.ll $< -std=c++14 \
#	--target=riscv64 \
#	-march=rv64gc \
#	--sysroot=$(GNU_PATH)/sysroot \
#	--gcc-toolchain=$(GNU_PATH) \
#	-I$(GNU_PATH)/sysroot/usr/include \
#	-I$(GNU_PATH)/riscv64-unknown-linux-gnu/include/c++/11.1.0 \
#	-I$(GNU_PATH)/riscv64-unknown-linux-gnu/include/c++/11.1.0/riscv64-unknown-linux-gnu/lib64/lp64/ \
#	$(flags) $(CFLAGS) -S -emit-llvm -fverbose-asm
