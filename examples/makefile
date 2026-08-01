#targets=../bin \
	../bin/test \
	../bin/asmTest \
	../bin/diamond \
	../bin/mem \
	../bin/nestedLoop

#Currently tested case
targets=$(ZRAY_BIN_PATH)/mem

all: $(targets)

cxxFlags=$(shell ${CONFIG} --cxxflags)
ldFlags=$(shell ${CONFIG} --ldflags --libs)

optLevel=-O2

ifeq ($(MAKECMDGOALS), gem5)
	CFLAGS=-DGEM5_BUILD
else ifeq ($(MAKECMDGOALS), gem5_zray)
	CFLAGS=-DGEM5_ZRAY_BUILD
endif

#flags=-ggdb #If we want debugging symbols
ifeq ($(MACHINE_ARCH),x86_64)
	flags=-fxray-instrument -Xclang -disable-O0-optnone -I$(GEM5_PATH)/include -L$(GEM5_PATH)/util/m5/build/x86/out -ggdb
	#flags=-fxray-instrument -Xclang -disable-O0-optnone -I$(GEM5_PATH)/include -L$(GEM5_PATH)/util/m5/build/x86/out -ggdb
	#-lm5 if using gem5
else ifeq ($(MACHINE_ARCH), riscv64)
	flags=-Xclang -disable-O0-optnone -I/usr/include/riscv64-linux-gnu -march=rv64g -msmall-data-limit=0 -latomic
endif

#requiredPasses=
requiredPasses=-mem2reg

$(ZRAY_BIN_PATH):
	mkdir $@

#Build IR files from CC files.
%.ll: %.cc $(ZRAY_BIN_PATH)/libzray.so $(ZRAY_BIN_PATH)/libzray_check_sf.so
	$(CUSTOM_CC) -o tmp_$<.ll $< -std=c++14 $(CFLAGS) $(flags) $(optLevel) -S -emit-llvm -fverbose-asm  -fvisibility=hidden -I../include

ifeq ($(MAKECMDGOALS), gem5)
	#Build without instrumentation
	#$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) --debug-pass=Arguments  -S < tmp_$<.ll > $@
	#Build with instrumentation
	$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --debug-pass=Arguments  -S < tmp_$<.ll > $@
	#$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --full-scan --debug-pass=Arguments  -S < tmp_$<.ll > $@
else
	#$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) --debug-pass=Arguments  -S < tmp_$<.ll > $@
	$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --loophoist=false --postdomset=true --debug-pass=Arguments  -S < tmp_$<.ll > $@
	#$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --full-scan --debug-pass=Arguments  -S < tmp_$<.ll > $@
endif

#Build IR files from C files.
%.ll: %.c $(ZRAY_BIN_PATH)/libzray.so
	$(CUSTOM_C) -o tmp_$<.ll $< -std=c++14 $(CFLAGS) $(flags) $(optLevel) -S -emit-llvm -fverbose-asm  -fvisibility=hidden -I../include

ifeq ($(MAKECMDGOALS), gem5)
	#Build without instrumentation
	#$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) --debug-pass=Arguments  -S < tmp_$<.ll > $@
	#Build with instrumentation
	$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --debug-pass=Arguments  -S < tmp_$<.ll > $@
	#$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --full-scan --debug-pass=Arguments  -S < tmp_$<.ll > $@
else
	$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --debug-pass=Arguments  -S < tmp_$<.ll > $@
	#$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load $(ZRAY_BIN_PATH)/libzray.so -zray --full-scan --debug-pass=Arguments  -S < tmp_$<.ll > $@
endif

$(ZRAY_BIN_PATH)/%: %.ll $(ZRAY_BIN_PATH)/zray_runtime.ll
	#Link modules together
	$(CUSTOM_LINK) -o linked_$<.ll $^
	#Assemble into binary, 
	$(CUSTOM_CC) -o $@ linked_$<.ll -std=c++14 $(flags)

#$(ZRAY_BIN_PATH)/mem:
#	$(CUSTOM_CC) -O2 -o $@ mem.cc -ggdb -mllvm --debug-pass=Arguments 
#	#$(CUSTOM_CC) -O2 -o $@ mem.cc -ggdb --debug-pass=Arguments 

.PHONY: gem5 gem5_zray

gem5: $(targets)

gem5_zray: $(targets)

clean:
	rm -rf *.ll $(targets) *.zlog *.s *.o
