// ZRay: portable compiler-assisted memory traffic characterization.
// Offline reader that aggregates host-mode runtime logs.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#include <fstream>
#include <iostream>
#include "zray_dyn.h"

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


int main()
{
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
#ifdef USE_HW_PERF_COUNTERS
        zray_finalize(counterArray, timeDelta, loadruntimeArray, storeruntimeArray, llcMisses, roiCount, counterWidth, tid, LogIteration);
#else
        zray_finalize(counterArray, timeDelta, loadruntimeArray, storeruntimeArray, roiCount, counterWidth, tid, LogIteration);
#endif

        epochNum++;
    }

    logfile.close();
}
