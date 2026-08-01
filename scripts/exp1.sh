#!/bin/bash

sf1=12
sf2=22

#threads=(16)
threads=( 1 2 4 8 16 )
script_to_run="./run_script.sh"

for n in "${threads[@]}"; do
    ./scripts/test_wrapper.sh ./scripts/run_mem.sh $sf1 $sf2 $n
    #./scripts/test_wrapper.sh ./scripts/run_mem_inst.sh $sf1 $sf2 $n
done
