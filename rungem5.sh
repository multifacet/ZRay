#!/bin/sh
cmd=$1
#shift
#$GEM5_PATH/build/X86/gem5.opt $GEM5_PATH/configs/example/se.py --cmd=$cmd --cpu-type=TimingSimpleCPU --l1d_size=64kB --l1i_size=16kB --caches
#$GEM5_PATH/build/X86/gem5.opt $GEM5_PATH/configs/example/se.py --cmd=$cmd --options="$2 $3 $4" --cpu-type=TimingSimpleCPU --l1d_size=64kB --l1i_size=16kB --caches
#$GEM5_PATH/build/X86/gem5.opt $GEM5_PATH/configs/example/se.py -h 
$GEM5_PATH/build/X86/gem5.opt $GEM5_PATH/configs/example/se.py --cmd=$cmd --options="$2 $3 $4" --cpu-type=TimingSimpleCPU --l1d_size=64kB --l1i_size=16kB --caches --mem-size=16GB -n=2
