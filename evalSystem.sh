#!/bin/zsh

if [ $# != 1 ]; then
    echo "Need to specify make target."
    echo "Try: ${0} tool_overhead"
    exit
fi

curDir=$(pwd)
autoBinPath=./examples/autoTest/bin

#Clean build
make clean -j$(nproc)

#Rebuild tool with TSC measurements
#Build test cases 
make $1 -j 1
#make $1 -j$(nproc)

#Run tests and direct output to measurement files
cd $autoBinPath
export XRAY_OPTIONS="patch_premain=true"

numTests=6
for i in $(seq 1 $numTests); do
    ./test${i} > test${i}_tsc &
done

wait < <(jobs -p)

#Gather measurements in directory
cd $curDir
resultsDir=./results/$(date +"%Y-%m-%d-%H:%M.%S")
mkdir $resultsDir 

for i in $(seq 1 $numTests); do
    mv $autoBinPath/test${i}_tsc $resultsDir
done

#Output summary
python ./util/parseResults.py $resultsDir
