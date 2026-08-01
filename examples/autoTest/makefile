#Hayden Coffey
targets=./bin \
		./bin/test1 \
		./bin/test2 \
		./bin/test3 \
		./bin/test4 \
		./bin/test5 \
		./bin/test6

all: $(targets)

cxxFlags=$(shell ${CONFIG} --cxxflags)
ldFlags=$(shell ${CONFIG} --ldflags --libs)

flags=-fxray-instrument -O0 -Xclang -disable-O0-optnone
requiredPasses=-mem2reg

./bin:
	mkdir $@

#Test programs
%.ll: %.cc ../../bin/libzray.so
	$(CUSTOM_CC) -o tmp_$<.ll $< -std=c++14 $(CFLAGS) $(flags) $(optLevel) -S -emit-llvm -fverbose-asm 
	$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load ../../bin/libzray.so -zray --debug-pass=Arguments  -S < tmp_$<.ll > $@

%.ll: %.c ../../bin/libzray.so
	$(CUSTOM_CC) -o tmp_$<.ll $< $(flags) $(optLevel) -S -emit-llvm -fverbose-asm 
	$(CUSTOM_OPT) -enable-new-pm=0 $(optLevel) $(requiredPasses) -load ../../bin/libzray.so -zray --debug-pass=Arguments  -S < tmp_$<.ll > $@

./bin/%: %.ll ../../bin/zray_runtime.ll
	#Link modules together
	$(CUSTOM_LINK) -o linked_$<.ll $^ 
	#Link into binary, zip metadata and concat
	$(CUSTOM_CC) -o $@_tmp linked_$<.ll -std=c++14 $(flags)
	zip tool_file.zip tool_file
	cat $@_tmp tool_file.zip > $@
	chmod +x $@
	#Cleanup
	rm $@_tmp tool_file tool_file.zip

clean:
	rm -rf *.ll $(targets)
