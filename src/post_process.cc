// ZRay: portable compiler-assisted memory traffic characterization.
// Offline reader that aggregates host-mode runtime logs.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#include <fstream>
#include <iostream>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "zray_dyn.h"
#include "zray_mst.h"

std::string LOGO = "\
 ____________  _____   __\n\
|___  /| ___ \\/ _ \\ \\ / /\n\
   / / | |_/ / /_\\ \\ V / \n\
  / /  |    /|  _  |\\ /  \n\
./ /___| |\\ \\| | | || |  \n\
\\_____/\\_| \\_\\_| |_/\\_/\n";

zray::ProfileData operator+(zray::ProfileData X, zray::ProfileData Y)
{
    zray::ProfileData Z;
    Z.GroupNumber = X.GroupNumber;
    Z.PragmaRegionID = X.PragmaRegionID;
    Z.LoadCount = X.LoadCount + Y.LoadCount;
    Z.FloatLoadCount = X.FloatLoadCount + Y.FloatLoadCount;
    Z.StoreCount = X.StoreCount + Y.StoreCount;
    Z.FloatStoreCount = X.FloatStoreCount + Y.FloatStoreCount;
    Z.BytesRead = X.BytesRead + Y.BytesRead;
    Z.BytesWritten = X.BytesWritten + Y.BytesWritten;
    Z.IntInstructionCount = X.IntInstructionCount + Y.IntInstructionCount;
    Z.FpInstructionCount = X.FpInstructionCount + Y.FpInstructionCount;
    Z.TermInstructionCount = X.TermInstructionCount + Y.TermInstructionCount;
    Z.MemInstructionCount = X.MemInstructionCount + Y.MemInstructionCount;
    Z.CastInstructionCount = X.CastInstructionCount + Y.CastInstructionCount;
    Z.GlobalOpReadCount = X.GlobalOpReadCount + Y.GlobalOpReadCount;
    Z.GlobalOpWriteCount = X.GlobalOpWriteCount + Y.GlobalOpWriteCount;
    Z.StackReadCount = X.StackReadCount + Y.StackReadCount;
    Z.StackWriteCount = X.StackWriteCount + Y.StackWriteCount;
    Z.HeapReadCount = X.HeapReadCount + Y.HeapReadCount;
    Z.HeapWriteCount = X.HeapWriteCount + Y.HeapWriteCount;
    Z.OtherInstCount = X.OtherInstCount + Y.OtherInstCount;
    Z.IntrinsicLoad = X.IntrinsicLoad + Y.IntrinsicLoad;
    Z.IntrinsicStore = X.IntrinsicStore + Y.IntrinsicStore;
    Z.TotalInstCount = X.TotalInstCount + Y.TotalInstCount;
    Z.SplitCounters = X.SplitCounters + Y.SplitCounters;
    Z.CounterInstCount = X.CounterInstCount + Y.CounterInstCount;
    return Z;
}

//Apply scaled instruction mix data onto given profile using index into counter array
zray::ProfileData ApplyCounter(zray::ProfileData Profile, size_t index, size_t * CounterArray)
{
    Profile.LoadCount = Profile.LoadCount * CounterArray[index];
    Profile.FloatLoadCount = Profile.FloatLoadCount * CounterArray[index];
    Profile.StoreCount = Profile.StoreCount * CounterArray[index];
    Profile.FloatStoreCount = Profile.FloatStoreCount * CounterArray[index];
    Profile.BytesRead = Profile.BytesRead * CounterArray[index];
    Profile.BytesWritten = Profile.BytesWritten * CounterArray[index];
    Profile.IntInstructionCount = Profile.IntInstructionCount * CounterArray[index];
    Profile.FpInstructionCount = Profile.FpInstructionCount * CounterArray[index];
    Profile.TermInstructionCount = Profile.TermInstructionCount * CounterArray[index];
    Profile.MemInstructionCount = Profile.MemInstructionCount * CounterArray[index];
    Profile.CastInstructionCount = Profile.CastInstructionCount * CounterArray[index];
    Profile.GlobalOpReadCount = Profile.GlobalOpReadCount * CounterArray[index];
    Profile.GlobalOpWriteCount = Profile.GlobalOpWriteCount * CounterArray[index];
    Profile.StackReadCount = Profile.StackReadCount * CounterArray[index];
    Profile.StackWriteCount = Profile.StackWriteCount * CounterArray[index];
    Profile.HeapReadCount = Profile.HeapReadCount * CounterArray[index];
    Profile.HeapWriteCount = Profile.HeapWriteCount * CounterArray[index];
    Profile.OtherInstCount = Profile.OtherInstCount * CounterArray[index];
    Profile.IntrinsicLoad = Profile.IntrinsicLoad * CounterArray[index];
    Profile.IntrinsicStore = Profile.IntrinsicStore * CounterArray[index];
    Profile.TotalInstCount = Profile.TotalInstCount * CounterArray[index];
    Profile.CounterInstCount = Profile.CounterInstCount * CounterArray[index];

    return Profile;
}

struct RegionLog
{
    size_t tid = 0;
    std::string functionName = "";
    size_t regionID = 0;

    size_t TotalInstCount = 0;
    size_t CounterInstCount = 0;
    size_t RegionProfileCount = 0;

    size_t BytesRead = 0;
    size_t BytesWritten = 0;
    size_t IntrinsicLoad = 0;
    size_t IntrinsicStore = 0;

    size_t LoadCount = 0;
    size_t StoreCount = 0;
    size_t IntInstructionCount = 0;
    size_t FpInstructionCount = 0;
    size_t CastInstructionCount = 0;
    size_t GlobalOpReadCount = 0;
    size_t GlobalOpWriteCount = 0;

    size_t StackReadCount = 0;
    size_t StackWriteCount = 0;

    size_t HeapReadCount = 0;
    size_t HeapWriteCount = 0;
};

//Track latest region log to calculate deltas
//First map is keyed on thread ID, second map is keyed on ROI ID.
std::unordered_map<size_t, std::unordered_map<size_t, RegionLog>* > RegionLogs;


//Read ROI counter profiles into Counts and called function counter profiles into IndirectProfiles
void read_pd_sets(std::vector<std::pair<std::string, zray::ProfileData> > & Counts,
        std::vector<std::pair<std::string, zray::ProfileData> > & IndirectProfiles, 
        size_t ZRAY_CounterDimension, size_t * CounterArray, size_t * LoadRuntimeArray, size_t * StoreRuntimeArray,
        std::vector<std::pair<std::string, zray::ProfileData> > & RegionProfileList,
        std::vector<size_t> & RegionProfileCounts
        )
{
    using namespace std;

    // std::cout << "Right at start of read_pd_sets\n";
    char *LogFileName = std::getenv("ZRAY_LOGFILE");
    ifstream regionProfiles;
    // std::cout << "Before opening test_log.zlog\n";
    regionProfiles.open(LogFileName, std::ios::binary);
    // std::cout << "After opening test_log.zlog\n";

    zray::ProfileData inProfile;

    size_t FunctionNameLen;
    int InCheck;
    int Check;

    //Read in profiledata struct
    while (regionProfiles.read(reinterpret_cast<char *>(&inProfile), sizeof(zray::ProfileData)))
    {
        //Read in and verify checksum
        regionProfiles.read(reinterpret_cast<char *>(&InCheck), sizeof(InCheck));
        Check = checksum((char *)&inProfile, sizeof(zray::ProfileData));
        if (Check != InCheck)
        {
            cout << "Tool Runtime: Checksum mismatch!\n";
            cout << InCheck << " " << Check << "\n";
        }

        //Read in function name
        regionProfiles.read(reinterpret_cast<char *>(&FunctionNameLen), sizeof(FunctionNameLen));
        string FunctionName(FunctionNameLen, '\0');
        regionProfiles.read(&FunctionName[0], FunctionNameLen);

        if(inProfile.IsIndirect)
        {
            IndirectProfiles.push_back({FunctionName, inProfile});
            continue;
        }

        int index = inProfile.PostDomSetID + ZRAY_CounterDimension*inProfile.PragmaRegionID;
        inProfile = ApplyCounter(inProfile, index, CounterArray);

        // Add runtime data
        inProfile.IntrinsicLoad += LoadRuntimeArray[index];
        inProfile.IntrinsicStore += StoreRuntimeArray[index];

        RegionProfileList[inProfile.PragmaRegionID].first = FunctionName;
        RegionProfileList[inProfile.PragmaRegionID].second.GroupNumber = inProfile.GroupNumber;
        RegionProfileList[inProfile.PragmaRegionID].second.PragmaRegionID = inProfile.PragmaRegionID;
        RegionProfileList[inProfile.PragmaRegionID].second = RegionProfileList[inProfile.PragmaRegionID].second + inProfile;

        // First basic block in ROI has zray counters at start of basic block, before customevent
        if (inProfile.PostDomSetID != 0)
            RegionProfileCounts[inProfile.PragmaRegionID] += CounterArray[index];

        Counts.push_back(pair<string, zray::ProfileData>(FunctionName,inProfile));
    }
    regionProfiles.close();
}

void write_basicblock_csv(const zray::ProfileData & inProfile, const std::string FunctionName,
        size_t * CounterArray, size_t * LoadRuntimeArray, size_t * StoreRuntimeArray, size_t ZRAY_CounterDimension, size_t tid)
{
        int index = inProfile.PostDomSetID + ZRAY_CounterDimension*inProfile.PragmaRegionID;

        // Get binary name using snippet proided here https://stackoverflow.com/a/12254992
#if defined(PLATFORM_POSIX) || defined(__linux__)
        std::string app_name;
        std::ifstream("/proc/self/comm") >> app_name;
#endif
        // Write to csv file
        std::ofstream csvfile;
        csvfile.open("zray_application_stats.csv", std::ios_base::app);
#ifdef USE_HW_PERF_COUNTERS
        csvfile << app_name << "," << tid << ",Basic Block," << inProfile.PragmaRegionID << "," << inProfile.GroupNumber << "," << index << "," << FunctionName << ",NA,NA,"
#else
        csvfile << app_name << "," << tid << ",Basic Block," << inProfile.PragmaRegionID << "," << inProfile.GroupNumber << "," << index << "," << FunctionName << ",NA,"
#endif
            << inProfile.TotalInstCount << "," << inProfile.CounterInstCount << "," << CounterArray[index] << ","  << LoadRuntimeArray[index] << "," << StoreRuntimeArray[index] << ",NA,NA,"
            << (inProfile.BytesRead + (inProfile.IntrinsicLoad)) << "," << (inProfile.BytesWritten + (inProfile.IntrinsicStore)) << ","
            << inProfile.LoadCount << "," << inProfile.StoreCount << "," << inProfile.IntInstructionCount << "," << inProfile.FpInstructionCount << ","
            << inProfile.CastInstructionCount << "," << inProfile.GlobalOpReadCount << "," << inProfile.GlobalOpWriteCount << "," << inProfile.StackReadCount
            << "," << inProfile.StackWriteCount << "," << inProfile.HeapReadCount << "," << inProfile.HeapWriteCount << "," << inProfile.IntrinsicLoad << ","
            << inProfile.IntrinsicStore << "\n";
        csvfile.close();
}

void write_cloned_basicblock_csv(const zray::ProfileData & inProfile, const std::string FunctionName)
{
}

//Apply counts from included library functions
void apply_indirect_counts(std::vector<std::pair<std::string, zray::ProfileData>> & RegionProfileList,
        std::vector<std::pair<std::string, zray::ProfileData>> IndirectProfiles,
        size_t ZRAY_CounterDimension, size_t * CounterArray, size_t * LoadRuntimeArray, size_t * StoreRuntimeArray,
        std::vector<size_t> & RegionProfileCounts, size_t tid)
{
    using namespace std;
    for(auto RegionProfile : RegionProfileList)
    {
        for(auto IndirProf : IndirectProfiles)
        {
            zray::ProfileData tmpProf;
            int index = IndirProf.second.PostDomSetID + ZRAY_CounterDimension*RegionProfile.second.PragmaRegionID;

            tmpProf = ApplyCounter(IndirProf.second, index, CounterArray);
            // Add runtime data
            tmpProf.IntrinsicLoad += LoadRuntimeArray[index];
            tmpProf.IntrinsicStore += StoreRuntimeArray[index];
            RegionProfileList[RegionProfile.second.PragmaRegionID].second = RegionProfileList[RegionProfile.second.PragmaRegionID].second + tmpProf;
            RegionProfileCounts[RegionProfile.second.PragmaRegionID] += CounterArray[index];

            // Get binary name using snippet proided here https://stackoverflow.com/a/12254992
#if defined(PLATFORM_POSIX) || defined(__linux__)
            std::string app_name;
            std::ifstream("/proc/self/comm") >> app_name;
#endif
            // Write to csv file
            ofstream csvfile;
            csvfile.open("zray_application_stats.csv", std::ios_base::app);
#ifdef USE_HW_PERF_COUNTERS
            csvfile << app_name << "," << tid << ",Cloned Basic Block," << tmpProf.PragmaRegionID << "," << tmpProf.GroupNumber << "," << index << "," << IndirProf.first << ",NA,NA,"
#else
            csvfile << app_name << "," << tid << ",Cloned Basic Block," << tmpProf.PragmaRegionID << "," << tmpProf.GroupNumber << "," << index << "," << IndirProf.first << ",NA,"
#endif
                << tmpProf.TotalInstCount << "," << tmpProf.CounterInstCount << "," << CounterArray[index] << "," << LoadRuntimeArray[index] << "," << StoreRuntimeArray[index] << ",NA,NA,"
                << (tmpProf.BytesRead + (tmpProf.IntrinsicLoad)) << "," << (tmpProf.BytesWritten + (tmpProf.IntrinsicStore)) << ","
                << tmpProf.LoadCount << "," << tmpProf.StoreCount << "," << tmpProf.IntInstructionCount << "," << tmpProf.FpInstructionCount << ","
                << tmpProf.CastInstructionCount << "," << tmpProf.GlobalOpReadCount << "," << tmpProf.GlobalOpWriteCount << "," << tmpProf.StackReadCount
                << "," << tmpProf.StackWriteCount << "," << tmpProf.HeapReadCount << "," << tmpProf.HeapWriteCount << "," << tmpProf.IntrinsicLoad << ","
                << tmpProf.IntrinsicStore << "\n";
            csvfile.close();
        }
    }
}

size_t return_counter_element(int index, size_t * CounterArray)
{
    return CounterArray[index];
}

void print_counter_element(int index, size_t * CounterArray)
{
    using namespace std;
    cout << "Counter element " << index << " is " << CounterArray[index] << "\n";
}

void print_counter_array(size_t * CounterArray, size_t PragmaRegionCount, size_t ZRAY_CounterDimension)
{
    using namespace std;

    cout << "-:";
    for (int i = 0; i < ZRAY_CounterDimension; i++)
    {
        if(i > 20) break;

        cout << setw(10) << i;
    }
    cout << "\n";

    for (int i = 0; i < PragmaRegionCount; i++)
    {
        cout << i << ":";
        for (int j = 0; j < ZRAY_CounterDimension; j++)
        {
            if(j > 20) break;

            cout << setw(10) << CounterArray[i * ZRAY_CounterDimension + j];
        }
        cout << "\n";
    }
}

#ifdef USE_HW_PERF_COUNTERS
void zray_finalize(size_t * CounterArray, size_t TimingProfiles, size_t * LoadRuntimeArray, size_t * StoreRuntimeArray, size_t * LLCMissCount, size_t PragmaRegionCount, size_t ZRAY_CounterDimension, size_t tid, size_t & LogIteration)
#else
void zray_finalize(size_t * CounterArray, size_t TimingProfiles, size_t * LoadRuntimeArray, size_t * StoreRuntimeArray, size_t PragmaRegionCount, size_t ZRAY_CounterDimension, size_t tid, size_t & LogIteration)
#endif
{
    timespec preprocess_start_time, preprocess_end_time;
    clock_gettime(CLOCK_MONOTONIC, &preprocess_start_time);
    using namespace std;
    
    std::vector<std::pair<std::string, zray::ProfileData> > RegionProfileList;
    std::vector<size_t> RegionProfileCounts;

    std::vector<std::pair<std::string, zray::ProfileData> > counts;
    std::vector<std::pair<std::string, zray::ProfileData> > IndirectProfiles;
    
    //  Profile data objects
    for (int i = 0; i < PragmaRegionCount; i++)
    {
        RegionProfileList.push_back(std::pair<std::string, zray::ProfileData>(string(), zray::ProfileData()));
        RegionProfileCounts.push_back(0);
    }

    ofstream csvfile;
    // Initialize stat csv
    if(LogIteration == 0) {
        // Open the csv in truncate mode. We don't want to multiple copies of data to be saved in it across multiple runs.
        // This is optional though, and we could modify it to append.
        csvfile.open("zray_application_stats.csv", std::ios_base::trunc);
#ifdef USE_HW_PERF_COUNTERS
        csvfile << "App name,Thread Iter,Entry type,Region,Group ID,Counter Index,Function,Time Elapsed (ns),LLC Misses,Total Insns,Counter Insns,Counter Value, Load Runtime Array Value, Store Runtime Array Value,Estimated Load BW (MB/s),Estimated Store BW (MB/s),Read Bytes,Written Bytes,Loads,Stores,Int Insns,FP Insns,Cast Inst,Global Read,Global Write,Stack Read,Stack Write,Heap Read,Heap Write,Intrinsic Load,Instrinsic Store\n";
#else
        csvfile << "App name,Thread Iter,Entry type,Region,Group ID,Counter Index,Function,Time Elapsed (ns),Total Insns,Counter Insns,Counter Value, Load Runtime Array Value, Store Runtime Array Value,Estimated Load BW (MB/s),Estimated Store BW (MB/s),Read Bytes,Written Bytes,Loads,Stores,Int Insns,FP Insns,Cast Inst,Global Read,Global Write,Stack Read,Stack Write,Heap Read,Heap Write,Intrinsic Load,Instrinsic Store\n";
#endif
        csvfile.close();
    }
    
    read_pd_sets(counts, IndirectProfiles, ZRAY_CounterDimension, CounterArray, LoadRuntimeArray, StoreRuntimeArray, RegionProfileList, RegionProfileCounts);

    for (const auto & prof : counts)
    {
        write_basicblock_csv(prof.second, prof.first, CounterArray, LoadRuntimeArray, StoreRuntimeArray, ZRAY_CounterDimension, tid);
    }

    apply_indirect_counts(RegionProfileList, IndirectProfiles, ZRAY_CounterDimension, CounterArray, LoadRuntimeArray, StoreRuntimeArray, RegionProfileCounts, tid);

    // Variables for bandwidth
    double read_bw = 0;
    double write_bw = 0;

    // Get binary name using snippet proided here https://stackoverflow.com/a/12254992
#if defined(PLATFORM_POSIX) || defined(__linux__)
    std::string app_name;
    std::ifstream("/proc/self/comm") >> app_name;
#endif

    // Write pragma region sums to log file
    int i = 0;
    csvfile.open("zray_application_stats.csv", std::ios_base::app);

    LogIteration++;

    for (auto &p : RegionProfileList)
    {
        RegionLog rLog = RegionLog{tid, p.first, (size_t)i, p.second.TotalInstCount, 
        p.second.CounterInstCount, RegionProfileCounts[i], p.second.BytesRead, p.second.BytesWritten, p.second.IntrinsicLoad,
        p.second.IntrinsicStore, p.second.LoadCount, p.second.StoreCount, p.second.IntInstructionCount, p.second.FpInstructionCount,
        p.second.CastInstructionCount, p.second.GlobalOpReadCount, p.second.GlobalOpWriteCount, p.second.StackReadCount, p.second.StackWriteCount,
        p.second.HeapReadCount, p.second.HeapWriteCount};
        
        //Bandwidth correction factor
        double correction_factor = 1.0;
        // double correction_factor = 0;
        /*size_t counterOverhead = p.second.TotalInstCount - p.second.CounterInstCount;
        if(counterOverhead > 0)
        {
            correction_factor = (p.second.TotalInstCount * 1.0)/(counterOverhead);
            size_t memoryAccessCount = (p.second.LoadCount + (p.second.IntrinsicLoad/8 + 1)) + (p.second.StoreCount + (p.second.IntrinsicStore/8) + 1);
            if(memoryAccessCount > 0)
            {
                correction_factor *= 1 + (((double) RegionProfileCounts[i])/(memoryAccessCount));
                correction_factor = sqrt(sqrt(correction_factor));
            }
        }*/

        if(correction_factor > 0)
        {
            read_bw = ((p.second.BytesRead + p.second.IntrinsicLoad) * 1.0 * correction_factor / (TimingProfiles / 1000000000.0)) / (1 << 20);
            write_bw = ((p.second.BytesWritten + p.second.IntrinsicStore) * 1.0 * correction_factor / (TimingProfiles / 1000000000.0)) / (1 << 20);
        }
        else
        {
            read_bw = 0;
            write_bw = 0;
        }

        csvfile << app_name << "," << tid << ",Region," << i << "," << p.second.GroupNumber << ",NA," << p.first << "," << TimingProfiles << ","
#ifdef USE_HW_PERF_COUNTERS
            << LLCMissCount[i] << "," << p.second.TotalInstCount << "," << p.second.CounterInstCount << "," << RegionProfileCounts[i] << ",NA," << read_bw
#else
            << "," << p.second.TotalInstCount << "," << p.second.CounterInstCount << "," << RegionProfileCounts[i] << ",NA," << read_bw
#endif
            << "," << write_bw << "," << (p.second.BytesRead + p.second.IntrinsicLoad) << "," << (p.second.BytesWritten + p.second.IntrinsicStore) << ","
            << p.second.LoadCount << "," << p.second.StoreCount << "," << p.second.IntInstructionCount << "," << p.second.FpInstructionCount << ","
            << p.second.CastInstructionCount << "," << p.second.GlobalOpReadCount << "," << p.second.GlobalOpWriteCount << "," << p.second.StackReadCount
            << "," << p.second.StackWriteCount << "," << p.second.HeapReadCount << "," << p.second.HeapWriteCount << "," << p.second.IntrinsicLoad << ","
            << p.second.IntrinsicStore << "\n";
        i++;
    }

    clock_gettime(CLOCK_MONOTONIC, &preprocess_end_time);

    csvfile.close();

#ifdef PROFILE_RUNTIME_TSC
    printf("TSC_DATA\n");
    printf("Cycle total: %lu\n", totalCycles);
    if (iterations != 0)
        printf("Average: %lu\n", totalCycles / iterations);
#endif
}


// ===================== MST placement arm (--placement=mst) =====================
// Reads the per-function sidecar (include/zray_mst.h), reconstructs each block's
// execution count from the measured non-tree edge counters by solving flow over
// the spanning tree, then scales each block's static mix by its count and
// aggregates per-function and overall. See the header comment in src/zray_mst.cc.

struct MstBlock { uint64_t Id; zray::ProfileData Mix; };
struct MstEdge  { uint64_t Src; uint64_t Dst; bool InMst; int64_t CtrIdx; };
struct MstFuncRecord
{
    std::string Name;
    uint64_t Region;
    bool Indirect;
    std::vector<MstBlock> Blocks;
    std::vector<MstEdge> Edges;
};

static bool parse_mst_sidecar(const std::string &Path, std::vector<MstFuncRecord> &Out)
{
    std::ifstream F(Path, std::ios::binary);
    if (!F)
    {
        return false;
    }
    uint64_t Magic;
    while (F.read(reinterpret_cast<char *>(&Magic), sizeof(Magic)))
    {
        if (Magic != ZRAY_MST_MAGIC)
        {
            std::cerr << "MST sidecar: bad magic, aborting parse\n";
            return false;
        }
        MstFuncRecord R;
        uint8_t Indirect;
        uint64_t NameLen;
        F.read(reinterpret_cast<char *>(&R.Region), sizeof(R.Region));
        F.read(reinterpret_cast<char *>(&Indirect), sizeof(Indirect));
        R.Indirect = Indirect;
        F.read(reinterpret_cast<char *>(&NameLen), sizeof(NameLen));
        R.Name.resize(NameLen);
        F.read(&R.Name[0], NameLen);

        uint64_t NumBlocks;
        F.read(reinterpret_cast<char *>(&NumBlocks), sizeof(NumBlocks));
        R.Blocks.resize(NumBlocks);
        for (uint64_t i = 0; i < NumBlocks; i++)
        {
            F.read(reinterpret_cast<char *>(&R.Blocks[i].Id), sizeof(uint64_t));
            F.read(reinterpret_cast<char *>(&R.Blocks[i].Mix), sizeof(zray::ProfileData));
        }
        uint64_t NumEdges;
        F.read(reinterpret_cast<char *>(&NumEdges), sizeof(NumEdges));
        R.Edges.resize(NumEdges);
        for (uint64_t i = 0; i < NumEdges; i++)
        {
            uint8_t M;
            F.read(reinterpret_cast<char *>(&R.Edges[i].Src), sizeof(uint64_t));
            F.read(reinterpret_cast<char *>(&R.Edges[i].Dst), sizeof(uint64_t));
            F.read(reinterpret_cast<char *>(&M), sizeof(M));
            R.Edges[i].InMst = M;
            F.read(reinterpret_cast<char *>(&R.Edges[i].CtrIdx), sizeof(int64_t));
        }
        Out.push_back(std::move(R));
    }
    return true;
}

// Solve flow over the spanning tree: measured (non-tree) edges seed the known
// counts, tree edges are filled by leaf-elimination (a node with exactly one
// unknown incident edge is resolved by conservation in==out). Returns each
// block's execution count = sum of its incoming edge counts.
static std::unordered_map<uint64_t, uint64_t>
reconstruct_block_counts(const MstFuncRecord &R, const size_t *CounterArray, size_t Width)
{
    size_t Base = R.Region * Width;
    size_t E = R.Edges.size();
    std::vector<long long> ECount(E, -1); // -1 == unknown

    // node -> list of (edgeIndex, isOutEdge)
    std::unordered_map<uint64_t, std::vector<std::pair<size_t, bool>>> Inc;
    std::unordered_map<uint64_t, int> Unk; // # unknown incident edges

    for (size_t i = 0; i < E; i++)
    {
        const MstEdge &e = R.Edges[i];
        if (!e.InMst)
        {
            ECount[i] = static_cast<long long>(CounterArray[Base + e.CtrIdx]);
        }
        Inc[e.Src].push_back({i, true});
        Inc[e.Dst].push_back({i, false});
        if (e.InMst)
        {
            Unk[e.Src]++;
            Unk[e.Dst]++;
        }
        else
        {
            // ensure both endpoints exist in Unk with a baseline of 0
            Unk[e.Src] += 0;
            Unk[e.Dst] += 0;
        }
    }

    std::vector<uint64_t> Work;
    for (auto &kv : Unk)
    {
        if (kv.second == 1)
        {
            Work.push_back(kv.first);
        }
    }
    while (!Work.empty())
    {
        uint64_t n = Work.back();
        Work.pop_back();
        if (Unk[n] != 1)
        {
            continue;
        }
        long long KnownIn = 0, KnownOut = 0, UnkEdge = -1;
        bool UnkIsOut = false;
        for (auto &pr : Inc[n])
        {
            size_t ei = pr.first;
            bool isOut = pr.second;
            if (ECount[ei] < 0)
            {
                UnkEdge = static_cast<long long>(ei);
                UnkIsOut = isOut;
            }
            else if (isOut)
            {
                KnownOut += ECount[ei];
            }
            else
            {
                KnownIn += ECount[ei];
            }
        }
        if (UnkEdge < 0)
        {
            Unk[n] = 0;
            continue;
        }
        long long Val = UnkIsOut ? (KnownIn - KnownOut) : (KnownOut - KnownIn);
        if (Val < 0)
        {
            Val = 0;
        }
        ECount[UnkEdge] = Val;
        const MstEdge &e = R.Edges[UnkEdge];
        Unk[e.Src]--;
        Unk[e.Dst]--;
        if (Unk[e.Src] == 1)
        {
            Work.push_back(e.Src);
        }
        if (Unk[e.Dst] == 1)
        {
            Work.push_back(e.Dst);
        }
    }

    std::unordered_map<uint64_t, uint64_t> BC;
    for (const MstBlock &b : R.Blocks)
    {
        BC[b.Id] = 0;
    }
    for (size_t i = 0; i < E; i++)
    {
        long long c = ECount[i] < 0 ? 0 : ECount[i];
        BC[R.Edges[i].Dst] += static_cast<uint64_t>(c);
    }
    return BC;
}

static std::unordered_map<std::string, zray::ProfileData> MstPerFunc;
static zray::ProfileData MstOverall;

static void mst_finalize(const std::vector<MstFuncRecord> &Recs, const size_t *CounterArray, size_t Width)
{
    for (const MstFuncRecord &R : Recs)
    {
        auto BC = reconstruct_block_counts(R, CounterArray, Width);
        for (const MstBlock &b : R.Blocks)
        {
            uint64_t Cnt = BC[b.Id];
            if (Cnt == 0)
            {
                continue;
            }
            size_t Tmp[1] = {Cnt};
            zray::ProfileData Scaled = ApplyCounter(b.Mix, 0, Tmp);
            MstPerFunc[R.Name] = MstPerFunc[R.Name] + Scaled;
            MstOverall = MstOverall + Scaled;
        }
    }
}

static void write_mst_csv()
{
    std::ofstream csv("zray_application_stats.csv", std::ios::trunc);
    csv << "app,scope,function,loads,stores,bytes_read,bytes_written,int_inst,fp_inst,total_inst\n";
    const char *AppEnv = std::getenv("ZRAY_APP_NAME");
    std::string app = AppEnv ? AppEnv : "app";
    for (auto &kv : MstPerFunc)
    {
        const zray::ProfileData &p = kv.second;
        csv << app << ",Function," << kv.first << "," << p.LoadCount << "," << p.StoreCount << ","
            << p.BytesRead << "," << p.BytesWritten << "," << p.IntInstructionCount << ","
            << p.FpInstructionCount << "," << p.TotalInstCount << "\n";
    }
    const zray::ProfileData &o = MstOverall;
    csv << app << ",Overall,ALL," << o.LoadCount << "," << o.StoreCount << ","
        << o.BytesRead << "," << o.BytesWritten << "," << o.IntInstructionCount << ","
        << o.FpInstructionCount << "," << o.TotalInstCount << "\n";
    csv.close();

    std::cout << "MST totals: loads=" << o.LoadCount << " stores=" << o.StoreCount
              << " bytesR=" << o.BytesRead << " bytesW=" << o.BytesWritten
              << " totalInst=" << o.TotalInstCount << "\n";
}

int main()
{
    // MST arm: if a sidecar sits next to ZRAY_LOGFILE, take the MST path instead
    // of the native ZRay post-dom log path. Presence of the sidecar is the only
    // signal needed -- the pass writes it only under --placement=mst.
    const char *LogEnv = std::getenv("ZRAY_LOGFILE");
    std::vector<MstFuncRecord> MstRecs;
    bool MstMode = false;
    if (LogEnv != nullptr)
    {
        std::string SidecarPath = std::string(LogEnv) + ".mst";
        std::ifstream Probe(SidecarPath, std::ios::binary);
        if (Probe.good())
        {
            Probe.close();
            MstMode = parse_mst_sidecar(SidecarPath, MstRecs) && !MstRecs.empty();
        }
    }

    std::ifstream logfile;
    std::string logfileName = "zray_host_log.bin";

    logfile.open(logfileName, std::ios::binary);

    size_t tid;
    //size_t roiTracker;

    size_t * origCounterArray;
    size_t * counterArray;
    size_t timeDelta;
    size_t * loadruntimeArray;
    size_t * storeruntimeArray;
#ifdef USE_HW_PERF_COUNTERS
    size_t * llcMisses;
#endif

    size_t counterWidth;
    size_t roiCount;

    logfile.read((char*)&counterWidth, sizeof(size_t));
    logfile.read((char*)&roiCount, sizeof(size_t));
    
    size_t arraySize = counterWidth * roiCount;
    size_t runtimeSize = arraySize;

    size_t LogIteration = 0;

    std::unordered_map<size_t, size_t*> CounterLogs;
    
    size_t epochNum = 0;
    while(logfile.read((char*)&tid, sizeof(size_t)))
    {
        //logfile.read((char*)&roiTracker, sizeof(size_t));
        
        //Read in counter array and subtract previous array off it
        origCounterArray = new size_t[arraySize];
        memset(origCounterArray, 0, arraySize * sizeof(size_t));

        logfile.read((char*)origCounterArray, sizeof(size_t)*arraySize);

        auto it = CounterLogs.find(tid);
        if (it == CounterLogs.end())
        {
            CounterLogs[tid] = origCounterArray;
            counterArray = origCounterArray;
        }
        else
        {
            counterArray = new size_t[arraySize];
            memset(counterArray, 0, arraySize * sizeof(size_t));

            for (int i = 0; i < arraySize; i++)
            {
                counterArray[i] = origCounterArray[i] - (it->second)[i];
                if((long long) counterArray[i] < 0)
                {
                    std::cerr << "zray: error: overflow in counter delta (thread id " << tid << ")\n";

                    std::cout << "Previous Counter\n";
                    print_counter_array(it->second, roiCount, counterWidth);
                    std::cout << "Original Counter\n";
                    print_counter_array(origCounterArray, roiCount, counterWidth);

                    exit(1);
                }
            }

            CounterLogs[tid] = origCounterArray;
        }

        logfile.read((char*)&timeDelta, sizeof(size_t));

        loadruntimeArray = new size_t[runtimeSize];

        logfile.read((char*)loadruntimeArray, sizeof(size_t)*runtimeSize);

        storeruntimeArray = new size_t[runtimeSize];

        logfile.read((char*)storeruntimeArray, sizeof(size_t)*runtimeSize);

#ifdef USE_HW_PERF_COUNTERS
        llcMisses = new size_t[roiCount];

        logfile.read((char*)llcMisses, sizeof(size_t)*roiCount);
#endif
        
        std::cout << "Processing epoch " << epochNum << "\n";
        if (MstMode)
        {
            mst_finalize(MstRecs, counterArray, counterWidth);
        }
        else
        {
#ifdef USE_HW_PERF_COUNTERS
            zray_finalize(counterArray, timeDelta, loadruntimeArray, storeruntimeArray, llcMisses, roiCount, counterWidth, tid, LogIteration);
#else
            zray_finalize(counterArray, timeDelta, loadruntimeArray, storeruntimeArray, roiCount, counterWidth, tid, LogIteration);
#endif
        }

        epochNum++;
    }

    logfile.close();

    if (MstMode)
    {
        write_mst_csv();
    }
}
