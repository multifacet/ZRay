// ZRay: portable compiler-assisted memory traffic characterization.
// Profile data structures shared by the pass and the runtime.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#ifndef ZRAY_DATA_H
#define ZRAY_DATA_H

#include <inttypes.h>
#include <iostream>
#include <iomanip>

namespace zray
{
    struct ProfileData
    {
        size_t PostDomSetID;
        size_t PragmaRegionID;
        size_t GroupNumber;
        size_t StoreCount;
        size_t FloatStoreCount;
        size_t LoadCount;
        size_t FloatLoadCount;
        size_t BytesRead;
        size_t BytesWritten;
        size_t IntInstructionCount;
        size_t FpInstructionCount;
        size_t TermInstructionCount;
        size_t MemInstructionCount;
        size_t CastInstructionCount;
        size_t GlobalOpReadCount;
        size_t GlobalOpWriteCount;
        size_t StackReadCount;
        size_t StackWriteCount;
        size_t HeapReadCount;
        size_t HeapWriteCount;
        size_t OtherInstCount;
        size_t IntrinsicLoad;
        size_t IntrinsicStore;
        size_t TotalInstCount;
        size_t CounterInstCount;
        size_t SplitCounters;
        bool IsIndirect;
        bool EnableMIRPass;

        ProfileData()
        {
            PostDomSetID = 0;
            PragmaRegionID = 0;
            GroupNumber = 0;
            StoreCount = 0;
            FloatStoreCount = 0;
            LoadCount = 0;
            FloatLoadCount = 0;
            BytesRead = 0;
            BytesWritten = 0;
            IntInstructionCount = 0;
            FpInstructionCount = 0;
            TermInstructionCount = 0;
            MemInstructionCount = 0;
            CastInstructionCount = 0;
            GlobalOpReadCount = 0;
            GlobalOpWriteCount = 0;
            StackReadCount = 0;
            StackWriteCount = 0;
            HeapReadCount = 0;
            HeapWriteCount = 0;
            OtherInstCount = 0;
            IntrinsicLoad = 0;
            IntrinsicStore = 0;
            TotalInstCount = 0;
            CounterInstCount = 0;
            SplitCounters = 0;
            IsIndirect=false;
        }
    };
}

constexpr int checksum(const char *data, size_t length)
{
    int sum = 0;
    for (size_t i = 0; i < length; i++)
    {
        sum ^= data[i];
    }
    return sum;
}

#endif
