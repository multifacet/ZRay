#!/bin/bash

move_file() {
    filename=$1

    if [ ! -f ./"$filename" ] && [ ! -f ./examples/"$filename" ]; then
        echo "File $filename not present, building examples."
        make examples
    fi

    if [ ! -f ./"$filename" ]; then
        mv ./examples/"$filename" .
        echo "File $filename moved from the examples directory to the current directory."
    else
        echo "File $filename already exists in the current directory."
    fi
}

if [ -f ./"tool_log_file.txt" ]; then
    rm tool_log_file.txt
fi

#Verify zlog file is present
move_file test_log.zlog

. ./setupEnv.sh
TOOL_INST=1 XRAY_OPTIONS="patch_premain=true" /usr/bin/time --verbose ./bin/mem $1 $2 $3
