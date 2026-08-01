#!/bin/sh

# ZRay builds against a stock LLVM 15 install. Point LLVM_BIN at the bin/
# directory of your LLVM 15 (distro package or local build), e.g.
#   LLVM_BIN=/usr/lib/llvm-15/bin . ./setupEnv.sh
LLVM_BIN="${LLVM_BIN:-/usr/lib/llvm-15/bin}"

export GEM5_PATH=$(pwd)/../gem5

export ZRAY_BIN_PATH=$(pwd)/bin

#export CUSTOM_C=clang
#export CUSTOM_CC=clang++
#export CUSTOM_LINK=llvm-link
#export CUSTOM_OPT=opt
#export CUSTOM_CONFIG=llvm-config

export CUSTOM_C=${LLVM_BIN}/clang
export CUSTOM_CC=${LLVM_BIN}/clang++
export CUSTOM_LINK=${LLVM_BIN}/llvm-link
export CUSTOM_OPT=${LLVM_BIN}/opt
export CUSTOM_CONFIG=${LLVM_BIN}/llvm-config

export MACHINE_ARCH=$(uname -m)
#Standardizing what version of LLVM we use could save/cause headache
#Appears to work with : LLVM-12, 13
#Breaks on: LLVM-10
if [[ "$HOST" == "tboard" ]]; then
export C=clang
export CC=clang++
export LINK=llvm-link
export OPT=opt
export CONFIG=llvm-config
fi
#else
#    export C=clang-12
#    export CC=clang++-12
#    export LINK=llvm-link-12
#    export OPT=opt-12
#    export CONFIG=llvm-config-12
#fi

export ZRAY_LOGFILE="zray.zlog"
