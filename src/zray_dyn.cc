// ZRay: portable compiler-assisted memory traffic characterization.
// ZRay runtime: counters, timing, sampling, and stats output.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#include "zray_dyn.h"

#ifdef USE_HW_PERF_COUNTERS
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#endif

std::string LOGO = "\
 ____________  _____   __\n\
|___  /| ___ \\/ _ \\ \\ / /\n\
   / / | |_/ / /_\\ \\ V / \n\
  / /  |    /|  _  |\\ /  \n\
./ /___| |\\ \\| | | || |  \n\
\\_____/\\_| \\_\\_| |_/\\_/\n";

std::mutex log_mutex;

std::mutex host_interface_mutex;

struct TimingProfile
{
    // bool end = false;
    size_t Count = 0;
    size_t StartTime = 0;
    size_t TotalTimeElapsed = 0;
#ifdef USE_HW_PERF_COUNTERS
    size_t llc_misses = 0;
    size_t l1_misses = 0;
    size_t insn_count = 0;
#endif
};

// Must exceed the number of instrumented regions in any target binary. The per-region
// thread_local arrays below (TimingProfiles, pragmaDisable, LLCMissArray) are indexed by
// region id; if a target has more regions than this, startTimingEvent/endTimingEvent write
// CLOCK_MONOTONIC timestamps out of bounds into the adjacent counter arrays, corrupting a
// counter slot with a time value (full-scan gapbs kernels have ~82 regions). Sized generously;
// the proper fix is to allocate these from PragmaRegionCount at init like the counter arrays.
#define PRAGMA_REGION_LIMIT 4096

// The SAMPLING_PERIOD variable is inversely proportional to the number of samples collected
// Setting it to 1 enables instrumentation for each and every ROI call.
// Setting it to 100 enables instrumentation for every hundredth ROI call.
// Setting it to 0 disables instrumentation.

namespace {
    int SAMPLING_PERIOD = []() {
        const char* env_value = std::getenv("ZRAY_SAMPLE_RATE");
        if (env_value != nullptr) {
            bool is_numeric = true;
            for (const char* ptr = env_value; *ptr != '\0'; ++ptr) {
                if (!std::isdigit(*ptr)) {
                    is_numeric = false;
                    break;
                }
            }
            if (is_numeric) {
                return std::atoi(env_value);
            } else {
                std::cerr << "ZRAY_SAMPLE_RATE has non-numeric characters, was there a typo? Reverting to default and instrumenting every ROI access.\n";
                return 1;  // Default value
            }
        } else {
            return 1;  // Default value if environment variable is not set
        }
    }();
}

thread_local TimingProfile TimingProfiles[PRAGMA_REGION_LIMIT];

// Define data structures and setup code for reading HW perf counter stats when available
#ifdef USE_HW_PERF_COUNTERS
// Function to perform perf_event_open syscall
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
        int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
thread_local struct perf_event_attr pe1, pe2, pe3;
thread_local int l1_miss_events, llc_miss_events, perf_insn_count;
thread_local size_t l1_misses_start, llc_misses_start, insn_count_start;
thread_local size_t l1_misses_end, llc_misses_end, insn_count_end;
thread_local bool perfInit = false;
thread_local size_t LLCMissArray[PRAGMA_REGION_LIMIT];
#endif

// Control over sampling at ROI granularity, a bit like representative sampling
thread_local bool pragmaDisable[PRAGMA_REGION_LIMIT];
// Single sampling control variable for all ROI, like random sampling. Starts disabled:
// a thread is outside every region until it enters one, and exits now restore the saved
// value rather than unconditionally disabling, so the bottom of the stack has to be
// right for counting to stop when the last region closes.
thread_local bool globalDisable = true;

// Nesting depth per region. A region can be re-entered before it exits -- most obviously
// when the instrumented function recurses, but also through indirect calls or one ROI
// nested inside another. Only the outermost entry may start the timer and only the
// matching outermost exit may stop it; without this, an inner exit stops the clock and
// latches globalDisable, silently dropping every counter increment in the remainder of
// the outer call. Depth is tracked here rather than relying on the pass never emitting
// an unbalanced pair, because the failure is silent: counters read zero while the
// elapsed time still looks plausible.
thread_local size_t RegionDepth[PRAGMA_REGION_LIMIT];

thread_local std::vector<std::pair<std::string, zray::ProfileData> > RegionProfileList;
thread_local std::vector<size_t> RegionProfileCounts;

thread_local size_t ROI_TRACKER = 0;

size_t ActiveGroupNumber;
size_t LogIteration = 0;

// Weak symbols to be overwritten by generated counters in IR pass.
extern thread_local size_t volatile CounterArray[] __attribute__((weak));
extern thread_local volatile size_t LoadRuntimeArray[] __attribute__((weak));
extern thread_local volatile size_t StoreRuntimeArray[] __attribute__((weak));
extern size_t PragmaRegionCount __attribute__((weak));
extern size_t ZRAY_CounterDimension __attribute__((weak)); //Full width of 2d counter array

bool Initialized = false;

// Handle indirect counter indexing.
//
// Set by startTimingEvent to the base of the currently-active region's row.
// The increment helpers index off this rather than off their pragmaRegionID
// argument, because that argument is a compile-time constant baked in from the
// enclosing call site (see insertCounterArrayInc in zray_codegen.cc). A cloned
// or indirectly-called function body is instrumented once but may run under a
// different region than the one it was cloned for, so only a runtime value can
// attribute its counts correctly. See e89cd57.
//
// The gap noted in e89cd57 -- a caller that runs blocks after a callee opens its own
// region would keep using the callee's base -- is closed by the save/restore stack
// below. Do not "fix" it instead by reverting to pragmaRegionID; that breaks clones
// and regresses SPEC.
thread_local size_t counterBaseIndex = 0;

// Saved counting state, pushed by the outermost entry to a region and popped by its
// matching exit, so a region nested inside another hands control back rather than
// leaving the outer region attributing into the inner one's row for the rest of its
// body. Depth is bounded by the number of distinct regions that can be simultaneously
// live, which cannot exceed PRAGMA_REGION_LIMIT.
struct RegionSave
{
    size_t base;
    bool disable;
};
thread_local RegionSave RegionSaveStack[PRAGMA_REGION_LIMIT];
thread_local size_t RegionSaveTop = 0;

// https://stackoverflow.com/questions/12254980/how-to-get-the-filename-of-the-currently-running-executable-in-c
// https://stackoverflow.com/questions/1528298/get-path-of-executable
std::string executableName()
{
#if defined(PLATFORM_POSIX) || defined(__linux__) // check defines for your setup
    char buff[1024];
    readlink("/proc/self/exe", buff, 1024);
    return std::string(buff);

#elif defined(_WIN32)

    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return buf;

#else

    static_assert(false, "unrecognized platform");

#endif
}

/* void timingEvent(size_t size)
{
    // TimingProfile *tp = &(TimingProfiles[*(size_t *)v]);
    timespec current_time;

    if (!TimingProfiles[size].end)
    //TODO: Should track per ROI, assuming only one ROI for now.
    //If even, we have finished ROI, if odd, currently executing
    //ROI_TRACKER++;

    if (!tp->end)
    {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        TimingProfiles[size].StartTime = current_time.tv_sec * 1000000000 + current_time.tv_nsec;
        TimingProfiles[size].end = true;
    }
    else
    {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        TimingProfiles[size].TotalTimeElapsed += (current_time.tv_sec * 1000000000 + current_time.tv_nsec) - (TimingProfiles[size].StartTime);
        TimingProfiles[size].end = false;
        ++TimingProfiles[size].Count;
    }
} */

void startTimingEvent(size_t size)
{
    // Re-entry into a region already being timed. Leave the running measurement and
    // the enable flag alone; the outermost entry owns both.
    if (RegionDepth[size]++ != 0) {
        return;
    }

    // Outermost entry: remember the enclosing region's counting state so the matching
    // exit can hand it back.
    if (RegionSaveTop < PRAGMA_REGION_LIMIT) {
        RegionSaveStack[RegionSaveTop] = {counterBaseIndex, globalDisable};
    }
    ++RegionSaveTop;

#ifdef USE_HW_PERF_COUNTERS
    if(!perfInit) {
        perfInit = true;
        memset(&pe1, 0, sizeof(struct perf_event_attr));
        memset(&pe2, 0, sizeof(struct perf_event_attr));
        pe1.type = PERF_TYPE_HW_CACHE;
        pe2.type = PERF_TYPE_HW_CACHE;
        pe3.type = PERF_TYPE_HARDWARE;
        pe1.size = sizeof(struct perf_event_attr);
        pe2.size = sizeof(struct perf_event_attr);
        pe1.config = (PERF_COUNT_HW_CACHE_L1D) |
                     (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                     (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        pe2.config = (PERF_COUNT_HW_CACHE_LL) |
                     (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                     (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        pe3.config = PERF_COUNT_HW_INSTRUCTIONS;
        l1_miss_events = perf_event_open(&pe1, 0, -1, -1, 0);
        if (l1_miss_events == -1) {
          fprintf(stderr, "zray: error: could not open perf event counter 1 (L1 misses)\n");
          exit(EXIT_FAILURE);
        }
        llc_miss_events = perf_event_open(&pe2, 0, -1, -1, 0);
        if (llc_miss_events == -1) {
          fprintf(stderr, "zray: error: could not open perf event counter 2 (LLC misses)\n");
          exit(EXIT_FAILURE);
        }
        perf_insn_count = perf_event_open(&pe3, 0, -1, -1, 0);
        if (perf_insn_count == -1) {
          fprintf(stderr, "zray: error: could not open perf event counter 3 (instructions)\n");
          exit(EXIT_FAILURE);
        }
        ioctl(l1_miss_events, PERF_EVENT_IOC_ENABLE, 0);
        ioctl(llc_miss_events, PERF_EVENT_IOC_ENABLE, 0);
        ioctl(perf_insn_count, PERF_EVENT_IOC_ENABLE, 0);
    }
#endif
    if((SAMPLING_PERIOD != 0) && (TimingProfiles[size].Count % SAMPLING_PERIOD == 0)) {
        globalDisable = false;
#ifdef USE_HW_PERF_COUNTERS
        read(l1_miss_events, &l1_misses_start, sizeof(uint64_t));
        read(llc_miss_events, &llc_misses_start, sizeof(uint64_t));
        read(perf_insn_count, &insn_count_start, sizeof(uint64_t));
#endif
    } else {
        return;
    }
    counterBaseIndex = size*ZRAY_CounterDimension;
    // TimingProfile *tp = &(TimingProfiles[*(size_t *)v]);
    timespec current_time;

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    TimingProfiles[size].StartTime = current_time.tv_sec * 1000000000 + current_time.tv_nsec;
}

void endTimingEvent(size_t size)
{
    // An exit with no matching entry. The pass should never emit one, so say so rather
    // than wrapping the depth counter around and disabling the region for the rest of
    // the run.
    if (RegionDepth[size] == 0) {
        std::cerr << "zray: warning: unbalanced region exit for region " << size
                  << ", ignoring\n";
        return;
    }
    // Still inside an outer entry of the same region: not an exit, so neither the
    // timer nor the invocation count moves. Count stays one per region invocation,
    // which is also what startTimingEvent's sampling modulo reads.
    if (--RegionDepth[size] != 0) {
        return;
    }

    // Outermost exit: hand counting back to the enclosing region, if any. Taken before
    // the globalDisable check below so a sampled-out region still restores.
    RegionSave saved = {counterBaseIndex, true};
    if (RegionSaveTop > 0) {
        --RegionSaveTop;
        if (RegionSaveTop < PRAGMA_REGION_LIMIT) {
            saved = RegionSaveStack[RegionSaveTop];
        }
    }

    // TimingProfile *tp = &(TimingProfiles[*(size_t *)v]);
    ++TimingProfiles[size].Count;
    if (globalDisable) {
        counterBaseIndex = saved.base;
        globalDisable = saved.disable;
        return;
    }
    timespec current_time;

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    TimingProfiles[size].TotalTimeElapsed += (current_time.tv_sec * 1000000000 + current_time.tv_nsec) - (TimingProfiles[size].StartTime);
#ifdef USE_HW_PERF_COUNTERS
    read(l1_miss_events, &l1_misses_end, sizeof(uint64_t));
    read(llc_miss_events, &llc_misses_end, sizeof(uint64_t));
    read(perf_insn_count, &insn_count_end, sizeof(uint64_t));
    TimingProfiles[size].l1_misses += l1_misses_end - l1_misses_start;
    TimingProfiles[size].llc_misses += llc_misses_end - llc_misses_start;
    LLCMissArray[size] += llc_misses_end - llc_misses_start;
    TimingProfiles[size].insn_count += insn_count_end - insn_count_start;
#endif
    counterBaseIndex = saved.base;
    globalDisable = saved.disable;
}

// void incrementCounterArray(size_t pragmaRegionID, size_t postDomSetID, bool enable)
void incrementCounterArray(size_t pragmaRegionID, size_t postDomSetID)
{
    // if (enable) {
    if (!globalDisable) {
        ++CounterArray[postDomSetID + counterBaseIndex];
    }
}

void incrementCounterArraySF(size_t pragmaRegionID, size_t postDomSetID, size_t SF)
{
    if (!globalDisable) {
        CounterArray[postDomSetID + counterBaseIndex] += SF;
    }
}

void incrementLoadArray(size_t pragmaRegionID, size_t postDomSetID, size_t size)
{
    if (!globalDisable) {
        LoadRuntimeArray[postDomSetID + counterBaseIndex] += size;
    }
}

void incrementStoreArray(size_t pragmaRegionID, size_t postDomSetID, size_t size)
{
    if (!globalDisable) {
        StoreRuntimeArray[postDomSetID + counterBaseIndex] += size;
    }
}

void incrementLoadStoreArray(size_t pragmaRegionID, size_t postDomSetID, size_t size)
{
    if (!globalDisable) {
        LoadRuntimeArray[postDomSetID + counterBaseIndex] += size;
        StoreRuntimeArray[postDomSetID + counterBaseIndex] += size;
    }
}

#ifdef PROFILE_RUNTIME_TSC
static unsigned long totalCycles = 0;
static unsigned long iterations = 0;
void zray_inst_tsc(void *v, size_t size)
{
    unsigned long lo, hi, tscStart, tscEnd;
    asm("rdtsc"
            : "=a"(lo), "=d"(hi));
    tscStart = lo | (hi << 32);

    timingEvent(v, size);

    asm("rdtsc"
            : "=a"(lo), "=d"(hi));
    tscEnd = lo | (hi << 32);

    totalCycles += (tscEnd - tscStart);
    iterations++;
}
#endif

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
zray::ProfileData ApplyCounter(zray::ProfileData Profile, size_t index)
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

//Read ROI counter profiles into Counts and called function counter profiles into IndirectProfiles
void read_pd_sets(std::vector<std::pair<std::string, zray::ProfileData> > & Counts,
        std::vector<std::pair<std::string, zray::ProfileData> > & IndirectProfiles)
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

        if (ActiveGroupNumber != 0 && inProfile.GroupNumber != ActiveGroupNumber)
        {
            continue;
        }

        int index = inProfile.PostDomSetID + ZRAY_CounterDimension*inProfile.PragmaRegionID;
        inProfile = ApplyCounter(inProfile, index);

        // Add runtime data
        inProfile.IntrinsicLoad += LoadRuntimeArray[index];
        inProfile.IntrinsicStore += StoreRuntimeArray[index];
        // cout << "After apply counter: profile loads: " << inProfile.LoadCount << " profile stores: " << inProfile.StoreCount << "\n";

        RegionProfileList[inProfile.PragmaRegionID].first = FunctionName;
        RegionProfileList[inProfile.PragmaRegionID].second.GroupNumber = inProfile.GroupNumber;
        RegionProfileList[inProfile.PragmaRegionID].second.PragmaRegionID = inProfile.PragmaRegionID;
        RegionProfileList[inProfile.PragmaRegionID].second = RegionProfileList[inProfile.PragmaRegionID].second + inProfile;

        // First basic block in ROI has zray counters at start of basic block, before customevent
        // if (inProfile.PostDomSetID != 0)
        //     RegionProfileCounts[inProfile.PragmaRegionID] += CounterArray[index];
        RegionProfileCounts[inProfile.PragmaRegionID] += CounterArray[index];

        Counts.push_back(pair<string, zray::ProfileData>(FunctionName,inProfile));
    }
    regionProfiles.close();
}

void write_basicblock_csv(const zray::ProfileData & inProfile, const std::string FunctionName)
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
        csvfile << app_name << "," << LogIteration << ",Basic Block," << inProfile.PragmaRegionID << "," << inProfile.GroupNumber << "," << index << "," << FunctionName << ",NA,"
            << inProfile.TotalInstCount << "," << inProfile.CounterInstCount << "," << CounterArray[index] + LoadRuntimeArray[index] + StoreRuntimeArray[index] << ",NA,NA,"
            << (inProfile.BytesRead + (inProfile.IntrinsicLoad)) << "," << (inProfile.BytesWritten + (inProfile.IntrinsicStore)) << ","
            << inProfile.LoadCount << "," << inProfile.StoreCount << "," << inProfile.IntInstructionCount << "," << inProfile.FpInstructionCount << ","
            << inProfile.CastInstructionCount << "," << inProfile.GlobalOpReadCount << "," << inProfile.GlobalOpWriteCount << "," << inProfile.StackReadCount
            << "," << inProfile.StackWriteCount << "," << inProfile.HeapReadCount << "," << inProfile.HeapWriteCount << "," << inProfile.IntrinsicLoad << ","
            << inProfile.IntrinsicStore << "\n";
        csvfile.close();
}

//Apply counts from included library functions
void apply_indirect_counts(std::vector<std::pair<std::string, zray::ProfileData>> & RegionProfileList,
        std::vector<std::pair<std::string, zray::ProfileData>> IndirectProfiles)
{
    using namespace std;
    for(auto RegionProfile : RegionProfileList)
    {
        for(auto IndirProf : IndirectProfiles)
        {
            zray::ProfileData tmpProf;
            int index = IndirProf.second.PostDomSetID + ZRAY_CounterDimension*RegionProfile.second.PragmaRegionID;

            tmpProf = ApplyCounter(IndirProf.second, index);

            // Add runtime data
            tmpProf.IntrinsicLoad += LoadRuntimeArray[index];
            tmpProf.IntrinsicStore += StoreRuntimeArray[index];
            // cout << "[Indirect profile] After apply counter: profile loads: " << tmpProf.LoadCount << " profile stores: " << tmpProf.StoreCount << "\n";
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
            csvfile << app_name << "," << LogIteration << ",Cloned Basic Block," << tmpProf.PragmaRegionID << "," << tmpProf.GroupNumber << "," << index << "," << IndirProf.first << ",NA,"
                << tmpProf.TotalInstCount << "," << tmpProf.CounterInstCount << "," << CounterArray[index] + LoadRuntimeArray[index] + StoreRuntimeArray[index] << ",NA,NA,"
                << (tmpProf.BytesRead + (tmpProf.IntrinsicLoad)) << "," << (tmpProf.BytesWritten + (tmpProf.IntrinsicStore)) << ","
                << tmpProf.LoadCount << "," << tmpProf.StoreCount << "," << tmpProf.IntInstructionCount << "," << tmpProf.FpInstructionCount << ","
                << tmpProf.CastInstructionCount << "," << tmpProf.GlobalOpReadCount << "," << tmpProf.GlobalOpWriteCount << "," << tmpProf.StackReadCount
                << "," << tmpProf.StackWriteCount << "," << tmpProf.HeapReadCount << "," << tmpProf.HeapWriteCount << "," << tmpProf.IntrinsicLoad << ","
                << tmpProf.IntrinsicStore << "\n";
            csvfile.close();
        }
    }
}

size_t return_counter_element(int index)
{
    return CounterArray[index];
}

void print_counter_element(int index)
{
    using namespace std;
    cout << "Counter element " << index << " is " << CounterArray[index] << "\n";
}

void print_counter_array()
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

void zray_finalize()
{
    timespec preprocess_start_time, preprocess_end_time;
    clock_gettime(CLOCK_MONOTONIC, &preprocess_start_time);
    using namespace std;
    std::lock_guard<std::mutex> guard(log_mutex);

    std::vector<std::pair<std::string, zray::ProfileData> > counts;
    std::vector<std::pair<std::string, zray::ProfileData> > IndirectProfiles;

    cout << "Pragma region count is : " << PragmaRegionCount << "\n";
    //  Profile data objects
    for (int i = 0; i < PragmaRegionCount; i++)
    {
        RegionProfileList.push_back(std::pair<std::string, zray::ProfileData>(string(), zray::ProfileData()));
        RegionProfileCounts.push_back(0);
    }

    cout << LOGO;
    cout << "Total counter array size is " << ZRAY_CounterDimension * PragmaRegionCount << "\n";

    print_counter_array();

    ofstream csvfile;
    // Initialize stat csv
    if(LogIteration == 0) {
        // Open the csv in truncate mode. We don't want to multiple copies of data to be saved in it across multiple runs.
        // This is optional though, and we could modify it to append.
        csvfile.open("zray_application_stats.csv", std::ios_base::trunc);
        csvfile << "App name,Thread Iter,Entry type,Region,Group ID,Counter Index,Function,Time Elapsed (ns),Total Insns,Counter Insns,ZRay Counters Incremented,Estimated Load BW (MB/s),Estimated Store BW (MB/s),Read Bytes,Written Bytes,Loads,Stores,Int Insns,FP Insns,Cast Inst,Global Read,Global Write,Stack Read,Stack Write,Heap Read,Heap Write,Intrinsic Load,Instrinsic Store\n";
        csvfile.close();
    }

    cout << "Combining counters with profile data...\n";
    cout << "Reading and applying post-dom sets... ";
    cout.flush();

    read_pd_sets(counts, IndirectProfiles);

    for (const auto & prof : counts)
    {
        write_basicblock_csv(prof.second, prof.first);
    }

    cout << "done\n";

    cout << "Applying indirect function counts... ";
    cout.flush();
    apply_indirect_counts(RegionProfileList, IndirectProfiles);
    cout << "done\n";

    size_t total_loads = 0;
    size_t total_stores = 0;
    size_t total_heap_loads = 0;
    size_t total_heap_stores = 0;
    size_t split_count_total = 0;
    // Variables for bandwidth
    double read_bw = 0;
    double write_bw = 0;

    // Formatting widths
    size_t width1 = 20;
    size_t width2 = 20;

    // Get binary name using snippet proided here https://stackoverflow.com/a/12254992
#if defined(PLATFORM_POSIX) || defined(__linux__)
    std::string app_name;
    std::ifstream("/proc/self/comm") >> app_name;
#endif

    // Write pragma region sums to log file
    int i = 0;
    ofstream logfile;
    logfile.open("tool_log_file.txt", std::ios_base::app);
    csvfile.open("zray_application_stats.csv", std::ios_base::app);

    logfile << "\nLog Iteration: " << LogIteration << "\n";
    LogIteration++;

    for (auto &p : RegionProfileList)
    {
        if (i == 0)
        {
            logfile << "\nPROFILING DATA:\n";
        }

        // Disable bandwidth correction for now
        double omaf, nmaf, msf;
        omaf = ((double) p.second.LoadCount + p.second.StoreCount + p.second.IntrinsicLoad/8 + p.second.IntrinsicStore/8) / (p.second.TotalInstCount);
        nmaf = ((double) p.second.LoadCount + p.second.StoreCount + p.second.IntrinsicLoad/8 + p.second.IntrinsicStore/8 + (2*RegionProfileCounts[i])) / (p.second.TotalInstCount + (2*RegionProfileCounts[i]));
	msf = nmaf/omaf;
        double correction_factor = 1.0;
        correction_factor = (p.second.TotalInstCount * 1.0 + (msf * RegionProfileCounts[i]))/(p.second.TotalInstCount);
        // double correction_factor = (p.second.TotalInstCount * 1.0)/(p.second.TotalInstCount - p.second.CounterInstCount);
        // correction_factor *= 1 + (((double) RegionProfileCounts[i])/((p.second.LoadCount + (p.second.IntrinsicLoad/8 + 1)) + (p.second.StoreCount + (p.second.IntrinsicStore/8) + 1)));
        // correction_factor = sqrt(sqrt(correction_factor));
        // if (correction_factor >= 1.15) {
        //     correction_factor /= 1.15;
        // } else {
        //     correction_factor = 1.0;
        // }
        read_bw = ((p.second.BytesRead + p.second.IntrinsicLoad) * 1.0 * correction_factor / (TimingProfiles[i].TotalTimeElapsed / 1000000000.0)) / (1 << 20);
        write_bw = ((p.second.BytesWritten + p.second.IntrinsicStore) * 1.0 * correction_factor / (TimingProfiles[i].TotalTimeElapsed / 1000000000.0)) / (1 << 20);

        logfile << "#Region " + to_string(i) << setw(width2) << "(Group ID " << p.second.GroupNumber << ")\n";
        logfile << setw(width1) << "FUNCTION:" << setw(width2) << p.first << "\n";
        logfile << setw(width1) << "TIME ELAPSED (ns):" << setw(width2) << (TimingProfiles[i].TotalTimeElapsed) << "\n";
#ifdef USE_HW_PERF_COUNTERS
        logfile << setw(width1) << "L1 READ MISSES:" << setw(width2) << (TimingProfiles[i].l1_misses) << "\n";
        logfile << setw(width1) << "LLC READ MISSES:" << setw(width2) << (TimingProfiles[i].llc_misses) << "\n";
        logfile << setw(width1) << "INSN COUNT:" << setw(width2) << (TimingProfiles[i].insn_count) << "\n";
        logfile << setw(width1) << "REAL LOAD BW (MB/s):" << setw(width2) << ((TimingProfiles[i].llc_misses) * 64.0/(TimingProfiles[i].TotalTimeElapsed / 1000000000.0)) / (1 << 20) << "\n";
        logfile << setw(width1) << "LLC MPKI:" << setw(width2) << (TimingProfiles[i].llc_misses * 1000.0)/(TimingProfiles[i].insn_count * 1.0) << "\n";
        logfile << setw(width1) << "LFMR:" << setw(width2) << ((double) TimingProfiles[i].llc_misses)/((double) TimingProfiles[i].l1_misses) << "\n";
        logfile << setw(width1) << "FLOPS:" << setw(width2) << ((double) p.second.FpInstructionCount)/((double) TimingProfiles[i].TotalTimeElapsed / 1000000000.0) << "\n";
        logfile << setw(width1) << "FP INTENSITY:" << setw(width2) << ((double) p.second.FpInstructionCount)/((double) TimingProfiles[i].llc_misses * 64.0) << "\n";
#endif
        logfile << setw(width1) << "NUM. TIMING EVENTS:" << setw(width2) << (TimingProfiles[i].Count) << "\n";
        logfile << setw(width1) << "HEAP READ BYTES:" << setw(width2) << (p.second.BytesRead + p.second.IntrinsicLoad) << "\n";
        logfile << setw(width1) << "HEAP WRITTEN BYTES:" << setw(width2) << (p.second.BytesWritten + p.second.IntrinsicStore) << "\n";
        logfile << setw(width1) << "LOAD BW (MB/s):" << setw(width2) << read_bw << "\n";
        logfile << setw(width1) << "WRITE BW (MB/s):" << setw(width2) << write_bw << "\n";
        logfile << setw(width1) << "REGION LOADS:" << setw(width2) << p.second.LoadCount << "\n";
        logfile << setw(width1) << "FP LOADS:" << setw(width2) << p.second.FloatLoadCount << "\n";
        logfile << setw(width1) << "INT LOADS:" << setw(width2) << p.second.LoadCount - p.second.FloatLoadCount << "\n";
        logfile << setw(width1) << "REGION STORES:" << setw(width2) << p.second.StoreCount << "\n";
        logfile << setw(width1) << "FP STORES:" << setw(width2) << p.second.FloatStoreCount << "\n";
        logfile << setw(width1) << "INT STORES:" << setw(width2) << p.second.StoreCount - p.second.FloatStoreCount << "\n";
        logfile << setw(width1) << "INT INST:" << setw(width2) << p.second.IntInstructionCount << "\n";
        logfile << setw(width1) << "FP INST:" << setw(width2) << p.second.FpInstructionCount << "\n";
        logfile << setw(width1) << "TERM INST:" << setw(width2) << p.second.TermInstructionCount << "\n";
        logfile << setw(width1) << "MEM INST:" << setw(width2) << p.second.MemInstructionCount << "\n";
        logfile << setw(width1) << "CAST INST:" << setw(width2) << p.second.CastInstructionCount << "\n";
        logfile << setw(width1) << "GLB READ:" << setw(width2) << p.second.GlobalOpReadCount << "\n";
        logfile << setw(width1) << "GLB WRITE:" << setw(width2) << p.second.GlobalOpWriteCount << "\n";
        logfile << setw(width1) << "STACK R:" << setw(width2) << p.second.StackReadCount << "\n";
        logfile << setw(width1) << "STACK W:" << setw(width2) << p.second.StackWriteCount << "\n";
        logfile << setw(width1) << "HEAP R:" << setw(width2) << p.second.HeapReadCount << "\n";
        logfile << setw(width1) << "HEAP W:" << setw(width2) << p.second.HeapWriteCount << "\n";
        logfile << setw(width1) << "OTHER INST:" << setw(width2) << p.second.OtherInstCount << "\n";
        logfile << setw(width1) << "INTRINSIC LOAD:" << setw(width2) << p.second.IntrinsicLoad<< "\n";
        logfile << setw(width1) << "INTRINSIC STORE:" << setw(width2) << p.second.IntrinsicStore<< "\n";
        logfile << setw(width1) << "TOTAL INST:" << setw(width2) << p.second.TotalInstCount << "\n";
        logfile << setw(width1) << "COUNTER INST:" << setw(width2) << 8 * RegionProfileCounts[i] << "\n";
        logfile << setw(width1) << "SPLIT COUNTERS:" << setw(width2) << p.second.SplitCounters << "\n";
        logfile << setw(width1) << "OVERHEAD INCREMENTS:" << setw(width2) << RegionProfileCounts[i] << "\n";
        split_count_total += p.second.SplitCounters;
        // total_loads += p.second.LoadCount + (p.second.IntrinsicLoad/8 + 1);
        // total_stores += p.second.StoreCount + (p.second.IntrinsicStore/8 + 1);
        total_loads += p.second.LoadCount + (p.second.IntrinsicLoad/8 + 1);
        total_stores += p.second.StoreCount + (p.second.IntrinsicStore/8 + 1);
        total_heap_loads += p.second.LoadCount - p.second.StackReadCount + (p.second.IntrinsicLoad/8 + 1);
        total_heap_stores += p.second.StoreCount - p.second.StackWriteCount + (p.second.IntrinsicStore/8 + 1);
        // total_loads += p.second.LoadCount - p.second.StackReadCount + (p.second.IntrinsicLoad/8 + 1);
        // total_stores += p.second.StoreCount - p.second.StackWriteCount + (p.second.IntrinsicStore + p.second.IntrinsicLoad/8 - p.second.IntrinsicLoad + 1);
        if(total_loads == 1)
            total_loads = 0;
        if(total_stores == 1)
            total_stores = 0;
        if(total_heap_loads == 1)
            total_heap_loads = 0;
        if(total_heap_stores == 1)
            total_heap_stores = 0;
        csvfile << app_name << "," << LogIteration - 1 << ",Region," << i << "," << p.second.GroupNumber << ",NA," << p.first << "," << TimingProfiles[i].TotalTimeElapsed << ","
            << p.second.TotalInstCount << "," << p.second.CounterInstCount << "," << RegionProfileCounts[i] << "," << read_bw << "," << write_bw << ","
            << (p.second.BytesRead + p.second.IntrinsicLoad) << "," << (p.second.BytesWritten + p.second.IntrinsicStore) << ","
            << p.second.LoadCount << "," << p.second.StoreCount << "," << p.second.IntInstructionCount << "," << p.second.FpInstructionCount << ","
            << p.second.CastInstructionCount << "," << p.second.GlobalOpReadCount << "," << p.second.GlobalOpWriteCount << "," << p.second.StackReadCount
            << "," << p.second.StackWriteCount << "," << p.second.HeapReadCount << "," << p.second.HeapWriteCount << "," << p.second.IntrinsicLoad << ","
            << p.second.IntrinsicStore << "\n";
        i++;
    }

    //TODO: How do we print counts in shared library functions without blowing up log?
    /*logfile << "COUNTERS================================\n";
    for (int i = 0; i < counts.size(); i++)
    {
        logfile << "Index: " << i << "\n";
        logfile << setw(width1) << "REGION ID:" << setw(width2) << counts[i].second.PragmaRegionID << "\n";
        logfile << setw(width1) << "FUNCTION:" << setw(width2) << counts[i].first << "\n";
        logfile << setw(width1) << "GROUP ID:" << setw(width2) << counts[i].second.GroupNumber << "\n";
        logfile << setw(width1) << "COUNTER VALUE:" << setw(width2) << CounterArray[i] << "\n";
        logfile << setw(width1) << "LOADS:" << setw(width2) << counts[i].second.LoadCount << "\n";
        logfile << setw(width1) << "STORES:" << setw(width2) << counts[i].second.StoreCount << "\n";
        logfile << setw(width1) << "BYTES READ:" << setw(width2) << counts[i].second.BytesRead << "\n";
        logfile << setw(width1) << "BYTES WRITTEN:" << setw(width2) << counts[i].second.BytesWritten << "\n";
    }*/

    logfile << "TOTAL HEAP LOADS : " << total_heap_loads << "\n";
    logfile << "TOTAL HEAP STORES: " << total_heap_stores << "\n";
    logfile << "TOTAL LOADS : " << total_loads << "\n";
    logfile << "TOTAL STORES: " << total_stores << "\n";
    logfile << "SPLIT COUNTERS: " << split_count_total/2 << "\n";
    clock_gettime(CLOCK_MONOTONIC, &preprocess_end_time);
    size_t totalPreProcessTime = (preprocess_end_time.tv_sec * 1000000000 + preprocess_end_time.tv_nsec) - (preprocess_start_time.tv_sec * 1000000000 + preprocess_start_time.tv_nsec);
    logfile << "TOTAL POSTPROCESS TIME: " << (totalPreProcessTime / 1000000000.0) << "\n";

    logfile.close();
    csvfile.close();

#ifdef PROFILE_RUNTIME_TSC
    printf("TSC_DATA\n");
    printf("Cycle total: %lu\n", totalCycles);
    if (iterations != 0)
        printf("Average: %lu\n", totalCycles / iterations);
#endif
#ifdef USE_HW_PERF_COUNTERS
    ioctl(l1_miss_events, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(llc_miss_events, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(perf_insn_count, PERF_EVENT_IOC_DISABLE, 0);
#endif
}

// Run at application startup to initialize runtime
void zray_runtime_init(void *v, size_t regionCount, size_t ceCount)
{
    if (Initialized || std::getenv("ZRAY_INST") == nullptr)
    {
        return;
    }

    // If no group is specified to patch (patchGroupNumber == 0), run all custom sleds
    ActiveGroupNumber = 0;
    char *patchGroupNumberStr = std::getenv("ZRAY_PATCH_ID");

    if (patchGroupNumberStr != NULL)
    {
        ActiveGroupNumber = atoi(patchGroupNumberStr);
    }

    // Bind instrumentation function
#ifdef PROFILE_RUNTIME_TSC
    //__xray_set_customevent_handler(zray_inst_tsc);
#else
    //__xray_set_customevent_handler(timingEvent);
#endif

    Initialized = true;
}

//INTERFACE EMULATION CODE================================================
//
//
bool HostInit = false;
thread_local bool AccelInit = false;


struct roi_descriptor{
    volatile void* counterAddr;
    volatile void* loadRuntimeAddr;
    volatile void* storeRuntimeAddr;
#ifdef USE_HW_PERF_COUNTERS
    volatile void* llcMissAddr;
#endif
    void* roiTracker;
    size_t time0;
};

size_t COUNTER_WIDTH = 0;
size_t ROI_COUNT = 0;

std::unordered_map<size_t, roi_descriptor> registered_threads;

void __start_host_proc();
void __host_poll_proc(int period);
void __register_thread();
void __unregister_thread();

void __poll_counters_host_to_accel(void * hostMemory, void * counterAddress, size_t counterSize,
        void * hostTiming,
        void * hostLoadRuntime, void * accelLoadRuntime, size_t loadRuntimeSize,
        void * hostStoreRuntime, void * accelStoreRuntime, size_t storeRuntimeSize,
#ifdef USE_HW_PERF_COUNTERS
        void * hostLLCMissAddr, void * accelLLCMissAddr, size_t llcMissSize, void * hostRoiTracker, void * accelRoiTracker);
#else
        void * hostRoiTracker, void * accelRoiTracker);
#endif


void __register_thread_accel_to_host(size_t tid, volatile void * arrayAddr, volatile void * loadRuntimeAddr,
#ifdef USE_HW_PERF_COUNTERS
        volatile void * storeRuntimeAddr, volatile void * llcMissAddr, void * roiTracker, size_t counterWidth, size_t roiCount);
#else
        volatile void * storeRuntimeAddr, void * roiTracker, size_t counterWidth, size_t roiCount);
#endif
void __unregister_thread_accel_to_host(size_t tid);

//Allocate memory for host polling process and start it
void __start_host_proc()
{
    new std::thread(__host_poll_proc, 2000);
}

//Periodically check counters and record them, period in ms
void __host_poll_proc(int period)
{
    std::ofstream logfile;
    std::string logfileName = "zray_host_log.bin";


    while (true)
    {
        std::vector<size_t> threadIDs;

        std::vector<size_t *> arrayData;

        std::vector<size_t> timingData;

        std::vector<size_t *> loadRuntimeData;
        std::vector<size_t *> storeRuntimeData;

        std::vector<size_t> roiTrackerData;

        size_t * dataPtr;
        size_t timeStamp = 0;
        size_t roiTracker = 0;

#ifdef USE_HW_PERF_COUNTERS
        size_t * llcMisses;
        std::vector<size_t *> llcMissData;
#endif
        size_t * loadRuntimePtr;
        size_t * storeRuntimePtr;

        auto start = std::chrono::system_clock::now();


        host_interface_mutex.lock();

        std::cout << "Starting poll\n";

        //Read counters to memory
        for (const auto& entry : registered_threads)
        {
            //ASSUME: All threads have identical counter array shapes, same # of ROI.
            //So we can write the size of the counters once to the log and use it for all entries.

            size_t tid = entry.first;
            roi_descriptor  roi = entry.second;

            size_t counterArraySize = COUNTER_WIDTH * ROI_COUNT;
            size_t * counterArrayAddr = (size_t*)roi.counterAddr;

            size_t loadRuntimeSize = counterArraySize;
            size_t * loadRuntimeAddr = (size_t*)roi.loadRuntimeAddr;

            size_t storeRuntimeSize = counterArraySize;
            size_t * storeRuntimeAddr = (size_t*)roi.storeRuntimeAddr;

            size_t * accelRoiTracker = (size_t*)(roi.roiTracker);

            dataPtr = new size_t[counterArraySize];
            loadRuntimePtr = new size_t[loadRuntimeSize];
            storeRuntimePtr = new size_t[storeRuntimeSize];

#ifdef USE_HW_PERF_COUNTERS
            size_t * accelLLCMissAddr = (size_t*)(roi.llcMissAddr);
            llcMisses = new size_t[ROI_COUNT];
#endif

            __poll_counters_host_to_accel((void*)dataPtr, (void*)counterArrayAddr,
                    counterArraySize*sizeof(size_t), (void*) &timeStamp,
                    (void*) loadRuntimePtr, (void*)loadRuntimeAddr, loadRuntimeSize * sizeof(size_t),
                    (void*) storeRuntimePtr, (void*) storeRuntimeAddr,storeRuntimeSize * sizeof(size_t),
#ifdef USE_HW_PERF_COUNTERS
                    (void*) llcMisses, (void*) accelLLCMissAddr, ROI_COUNT * sizeof(size_t), (void*) &roiTracker, (void*) accelRoiTracker);
#else
                    (void*) &roiTracker, (void*) accelRoiTracker);
#endif

            threadIDs.push_back(tid);

            arrayData.push_back(dataPtr);

            timingData.push_back(timeStamp);

            loadRuntimeData.push_back(loadRuntimePtr);
            storeRuntimeData.push_back(storeRuntimePtr);

            roiTrackerData.push_back(roiTracker);
#ifdef USE_HW_PERF_COUNTERS
            llcMissData.push_back(llcMisses);
#endif
        }


        //Write counters to disk
        logfile.open(logfileName, std::ios_base::app | std::ios::binary);

        for (int i = 0; i < threadIDs.size(); i++)
        {
            if (!HostInit)
            {
                logfile.write((char*)&COUNTER_WIDTH, sizeof(size_t));
                logfile.write((char*)&ROI_COUNT, sizeof(size_t));
                HostInit = true;
            }

            auto it = registered_threads.find(threadIDs[i]);

            logfile.write((char*)(&threadIDs[i]), sizeof(size_t));

            //TODO: Test out the even/odd ROI tracking later.
            //logfile.write((char*)(&roiTrackerData[i]), sizeof(size_t));

            logfile.write((char*)arrayData[i], sizeof(size_t) * COUNTER_WIDTH * ROI_COUNT);
            delete [] arrayData[i];

            size_t timeDelta = timingData[i] - it->second.time0;
            logfile.write((char*)&(timeDelta), sizeof(size_t));
            it->second.time0 = timingData[i];

            logfile.write((char*)loadRuntimeData[i], sizeof(size_t) * COUNTER_WIDTH * ROI_COUNT);
            delete [] loadRuntimeData[i];

            logfile.write((char*)storeRuntimeData[i], sizeof(size_t) * COUNTER_WIDTH * ROI_COUNT);
            delete [] storeRuntimeData[i];

#ifdef USE_HW_PERF_COUNTERS
            logfile.write((char*)llcMissData[i], sizeof(size_t) * ROI_COUNT);
            delete [] llcMissData[i];
#endif
        }

        host_interface_mutex.unlock();

        logfile.close();

        auto end = std::chrono::system_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        //Sleep for remaining time in period
        int time_remaining = period - elapsed_time.count();
        if (time_remaining > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(time_remaining));
        }

    }
}

void __poll_counters_host_to_accel(void * hostMemory, void * counterAddress, size_t counterSize,
        void * hostTiming,
        void * hostLoadRuntime, void * accelLoadRuntime, size_t loadRuntimeSize,
        void * hostStoreRuntime, void * accelStoreRuntime, size_t storeRuntimeSize,
#ifdef USE_HW_PERF_COUNTERS
        void * hostLLCMissAddr, void * accelLLCMissAddr, size_t llcMissSize, void * hostRoiTracker, void * accelRoiTracker)
#else
        void * hostRoiTracker, void * accelRoiTracker)
#endif
{
    //Copy counters
    memcpy((void*)hostMemory, (void*)counterAddress, counterSize);
    memcpy((void*)hostLoadRuntime, (void*)accelLoadRuntime, loadRuntimeSize);
    memcpy((void*)hostStoreRuntime, (void*)accelStoreRuntime, storeRuntimeSize);
    memcpy((void*)hostRoiTracker, (void*)accelRoiTracker, sizeof(size_t));
#ifdef USE_HW_PERF_COUNTERS
    memcpy((void*)hostLLCMissAddr, (void*)accelLLCMissAddr, llcMissSize);
#endif

    timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    *((size_t*)hostTiming) = (current_time.tv_sec * 1000000000 + current_time.tv_nsec);
}

//TODO: May need a lock here so that the host doesn't poll a thread that has exited
//Maybe a lock around anything that modifies the map, and around the polling code

//Add thread ROI data to host polling list
void __register_thread()
{
    if(AccelInit)
        return;

    AccelInit = true;

    size_t hashID = std::hash<std::thread::id>{}(std::this_thread::get_id());

    volatile void * counterArrayAddr = CounterArray;
    volatile void * loadRuntimeAddr = LoadRuntimeArray;
    volatile void * storeRuntimeAddr = StoreRuntimeArray;

#ifdef USE_HW_PERF_COUNTERS
    volatile void * llcMissAddr = LLCMissArray;
    __register_thread_accel_to_host(hashID, counterArrayAddr, loadRuntimeAddr, storeRuntimeAddr, llcMissAddr, &ROI_TRACKER, ZRAY_CounterDimension, PragmaRegionCount);
#else
    __register_thread_accel_to_host(hashID, counterArrayAddr, loadRuntimeAddr, storeRuntimeAddr, &ROI_TRACKER, ZRAY_CounterDimension, PragmaRegionCount);
#endif
}

void __register_thread_accel_to_host(size_t tid, volatile void * arrayAddr, volatile void * loadRuntimeAddr,
#ifdef USE_HW_PERF_COUNTERS
        volatile void * storeRuntimeAddr, volatile void * llcMissAddr, void * roiTracker, size_t counterWidth, size_t roiCount)
#else
        volatile void * storeRuntimeAddr, void * roiTracker, size_t counterWidth, size_t roiCount)
#endif
{
    std::lock_guard<std::mutex> guard(host_interface_mutex);

    if(ROI_COUNT == 0)
    {
        ROI_COUNT = roiCount;
        COUNTER_WIDTH = counterWidth;
    }

    // std::cout << "Registering thread " << tid << "\n";

    auto it = registered_threads.find(tid);
    if (it == registered_threads.end())
    {
        timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        size_t initTime = (current_time.tv_sec * 1000000000 + current_time.tv_nsec);
#ifdef USE_HW_PERF_COUNTERS
        registered_threads[tid] = roi_descriptor {arrayAddr, loadRuntimeAddr, storeRuntimeAddr, llcMissAddr, roiTracker, initTime};
#else
        registered_threads[tid] = roi_descriptor {arrayAddr, loadRuntimeAddr, storeRuntimeAddr, roiTracker, initTime};
#endif
    }
    else
    {
        std::cerr << "zray: warning: duplicate entry in host registered threads\n";
    }
}

//Remove thread from host polling list
void __unregister_thread()
{
    size_t hashID = std::hash<std::thread::id>{}(std::this_thread::get_id());
    __unregister_thread_accel_to_host(hashID);
}

void __unregister_thread_accel_to_host(size_t tid)
{
    std::lock_guard<std::mutex> guard(host_interface_mutex);

    auto it = registered_threads.find(tid);
    if (it != registered_threads.end())
    {
        registered_threads.erase(tid);
    }
    else
    {
        std::cerr << "zray: warning: tried to remove nonexistent entry in host "
                     "registered threads (thread id " << tid << ")\n";
    }
}


//INTERFACE EMULATION CODE^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

// https://stackoverflow.com/questions/20112221/invoking-a-function-automatically-on-stdthread-exit-in-c11
#include <stack>
#include <functional>

__attribute__((noinline)) void on_thread_exit()
{
    class ThreadExiter
    {
        std::stack<std::function<void()>> exit_funcs;

        public:
        ThreadExiter() = default;
        ThreadExiter(ThreadExiter const &) = delete;
        void operator=(ThreadExiter const &) = delete;
        ~ThreadExiter()
        {
            zray_finalize();
            /* while (!exit_funcs.empty())
            {
                exit_funcs.top()();
                exit_funcs.pop();
            }*/
        }
        /* void add(std::function<void()> func)
        {
            exit_funcs.push(std::move(func));
        }*/
    };

    thread_local ThreadExiter exiter;
}

__attribute__((noinline)) void on_thread_exit_monitor()
{
    class ThreadExiter
    {
        std::stack<std::function<void()>> exit_funcs;

        public:
        ThreadExiter() = default;
        ThreadExiter(ThreadExiter const &) = delete;
        void operator=(ThreadExiter const &) = delete;
        ~ThreadExiter()
        {
            zray_finalize();
            __unregister_thread();
            while (!exit_funcs.empty())
            {
                exit_funcs.top()();
                exit_funcs.pop();
            }
        }
        void add(std::function<void()> func)
        {
            exit_funcs.push(std::move(func));
        }
    };

    thread_local ThreadExiter exiter;
}
//-------------------------------
