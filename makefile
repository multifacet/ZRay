all: zray examples

.PHONY: zray clean examples zray-cycles

zray-perf: export CFLAGS=-DUSE_HW_PERF_COUNTERS
zray-perf:
	$(MAKE) -C src

zray-cycles: export CFLAGS=-DPROFILE_RUNTIME_TSC
zray-cycles: 
	$(MAKE) -C src
	$(MAKE) -C examples/autoTest

zray-overhead: export CFLAGS=-DTSC_PROFILE
zray-overhead: 
	$(MAKE) -C src
	$(MAKE) -C examples/autoTest

zray:
	$(MAKE) -j $(nproc) -C src

examples: zray
	$(MAKE) -j $(nproc) -C examples

#Build example with m5op dump
examples_gem5: zray
	$(MAKE) -j $(nproc) -C examples gem5

#Build with m5op and "zray(?)"
examples_gem5_zray: zray
	$(MAKE) -j $(nproc) -C examples gem5_zray

clean:
	$(MAKE) -C src clean
	$(MAKE) -C examples clean
	$(MAKE) -C examples/autoTest clean
	rm -f *.zlog
