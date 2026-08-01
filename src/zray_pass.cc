// ZRay: portable compiler-assisted memory traffic characterization.
// Pass entry point, region-of-interest discovery, and counter placement.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

/******************************
 * Hayden Coffey
 *
 * Compiler pass used to insert
 * instrumentation.
 */
#include "zray_pass.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/SCCIterator.h"
#include <algorithm>
#include <queue>

#include "llvm/Support/Threading.h"

//Distributing and Parallelizing Non-canonical Loops

namespace zray
{

    static cl::opt<bool> FullScan("full-scan", cl::desc("Auto instrument all functions"), cl::value_desc("true/false"), cl::Hidden, cl::init(false));
    static cl::opt<bool> ApplyPostDomSets("postdomset", cl::desc("Use post-domination set optimization."), cl::value_desc("true/false"), cl::Hidden, cl::init(true));
    static cl::opt<bool> ApplyLoopHoisting("loophoist", cl::desc("Use loop hoisting optimization."), cl::value_desc("true/false"), cl::Hidden, cl::init(true));
    static cl::opt<bool> ApplyMIRPass("mirpass", cl::desc("Enable LLVM MIR pass."), cl::value_desc("true/false"), cl::Hidden, cl::init(false));
    static cl::opt<bool> ApplyFunctionCloning("functionclone", cl::desc("Enable function cloning."), cl::value_desc("true/false"), cl::Hidden, cl::init(true));
    static cl::opt<bool> EnableHostMonitor("hostmonitor", cl::desc("Enable host thread monitoring"), cl::value_desc("true/false"), cl::Hidden, cl::init(false));

    // Create a BFS ordering of CFG blocks
    std::vector<llvm::BasicBlock *> ZRayPass::orderBasicBlocks(Function &F)
    {
        // CFG Block Vector and queue for BFS
        std::vector<llvm::BasicBlock *> block_list;
        std::queue<llvm::BasicBlock *> processing_order;

        if (F.getBasicBlockList().size() == 0)
        {
            return block_list;
        }

        // Enumerate the SCCs the CFG in reverse topological order of the SCC DAG.
        // https://eli.thegreenplace.net/2013/09/16/analyzing-function-cfgs-with-llvm
        for (scc_iterator<Function *> I = scc_begin(&F),
                                      IE = scc_end(&F);
             I != IE; ++I)
        {
            // Obtain the vector of BBs in this SCC and print it out.
            const std::vector<BasicBlock *> &SCCBBs = *I;

            for (auto BBI : SCCBBs)
            {
                block_list.push_back(BBI);
            }
        }

        std::reverse(block_list.begin(), block_list.end());

        return block_list;
    }

    std::vector<std::vector<llvm::BasicBlock *> *> *inst_all_blocks(std::vector<llvm::BasicBlock *> *npd)
    {
        auto pd = new std::vector<std::vector<llvm::BasicBlock *> *>();
        for (int i = 0; i < npd->size(); i++)
        {
            std::vector<llvm::BasicBlock *> *tmp_blocks = new std::vector<llvm::BasicBlock *>();
            tmp_blocks->push_back((*npd)[i]);
            pd->push_back(tmp_blocks);
        }

        return pd;
    }

    // Create sets of basic blocks to associate with a profile event
    std::vector<std::vector<llvm::BasicBlock *> *> *ZRayPass::createPostDomSets(std::vector<llvm::BasicBlock *> *npd, const llvm::PostDominatorTree *pdTree)
    {
        auto pd = new std::vector<std::vector<llvm::BasicBlock *> *>();

        // Create post domination sets
        while (!npd->empty())
        {
            std::vector<llvm::BasicBlock *> *tmp_blocks = new std::vector<llvm::BasicBlock *>();
            std::vector<size_t> processedIndexes;

            // Select and remove block B from npd list
            llvm::BasicBlock *b = (*npd)[0];
            tmp_blocks->push_back(b);
            npd->erase(npd->begin());

            // Find all blocks that postdominate B in npd, remove and place them in B's set.
            for (int i = 0; i < npd->size(); i++)
            {
                if (pdTree->dominates((*npd)[i], b) && ApplyPostDomSets)
                {
                    tmp_blocks->push_back((*npd)[i]);
                    processedIndexes.push_back(i);
                }
            }
            for (int i = processedIndexes.size() - 1; i >= 0; i--)
            {
                npd->erase(npd->begin() + processedIndexes[i]);
            }

            // Add new set to collection
            pd->push_back(tmp_blocks);
        }

        return pd;
    }

    bool ZRayPass::instrumentPostDomSet(std::vector<llvm::BasicBlock *> *npd, PostDominatorTree *PDtree, size_t pragma_region,
                                        size_t group_id, Function &F, bool IsIndirect)
    {
        auto pd = createPostDomSets(npd, PDtree);
        // auto pd = inst_all_blocks(npd);

        bool insertedSled = false;
        for (auto set : *pd)
        {
            insertedSled |= insertPostDomSetEvents(set, pragma_region, group_id, F, IsIndirect);
        }

        return insertedSled;
    }

    // Update given profile with instructions from BB list. Tag each block.
    void ZRayPass::recordTagBlocks(const std::vector<llvm::BasicBlock *> *pd, size_t scaleFactor, ProfileData &profile, Module *M)
    {
        for (auto block : *pd)
        {
            for (BasicBlock::iterator inst = block->begin(); inst != block->end(); inst++)
            {
                recordInst((inst), scaleFactor, profile, M);
                // May need to clone functions called using call and invoke
                if ((isa<CallInst>(inst)) || (isa<InvokeInst>(inst)))
                {
                    llvm::CallBase *callInst = llvm::cast<llvm::CallBase>(inst);
                    processCallInst(callInst, profile.PragmaRegionID);
                }
            }
            insertBBTag(M, block->getFirstInsertionPt(), GlobalCounterEventTotal, scaleFactor);
        }
    }

    void ZRayPass::processCallInst(CallBase *callInst, size_t PragmaRegionID)
    {
        // Ignore function if it is not defined
        Function *f = callInst->getCalledFunction();
        if (f == nullptr)
        {
            errs() << "Function: "
                   << "nullptr"
                   << " Not defined\n";
            return;
        }
        else if (f->isIntrinsic())
        {
            auto intrinsicID = f->getIntrinsicID();

            switch (intrinsicID)
            {
            // Intrinsics with models implemented
            case Intrinsic::memcpy:
            case Intrinsic::memmove:
            case Intrinsic::memset:
                break;

            // Intrinsics we are ignoring
            case Intrinsic::lifetime_start:
            case Intrinsic::lifetime_end:
            case Intrinsic::vastart: // va_start/va_end macro in C.
            case Intrinsic::vaend:
            case Intrinsic::xray_customevent:
            case Intrinsic::dbg_value:
                break;
            default:
                errs() << "Function: " << f->getName() << " Not defined\n";
            }

            return;
        }
        else if (!f->size())
        {
            if ((ApplyFunctionCloning) && (f->getName().str() == "__kmpc_fork_call"))
            {
                // errs() << "__kmpc_fork_call\n";
                std::string tmp_str;
                raw_string_ostream ss(tmp_str);
                callInst->print(ss);
                // errs() << "Str: " << tmp_str << '\n';
                // Extract the outlined ".omp..." microtask name from the printed
                // call. Some __kmpc_fork_call sites (seen in cc.cc) don't render a
                // ".omp" token here; guard against string::npos so substr() doesn't
                // throw and abort the whole pass.
                size_t OmpPos = tmp_str.find(".omp");
                if (OmpPos == std::string::npos)
                {
                    errs() << "zray: warning: __kmpc_fork_call with no \".omp\" outlined "
                           << "name in printed call; skipping OpenMP microtask "
                           << "cloning for this call.\n";
                    return;
                }
                tmp_str = tmp_str.substr(OmpPos);
                tmp_str = tmp_str.substr(0, tmp_str.find(' '));
                // errs() << "Omp: " << tmp_str << '\n';

                // Set f to forked function
                f = _M->getFunction(tmp_str);
                if (f == nullptr)
                {
                    errs() << "zray: warning: outlined function \"" << tmp_str
                           << "\" not found in module; skipping OpenMP microtask "
                           << "cloning for this call.\n";
                    return;
                }

                // Function is defined, proceed with clone
                Function *newF = processFunctionClone(f, PragmaRegionID);

                /* for (int i = 0; i < callInst->getNumOperands(); ++i)
                {
                    errs() << "Operand " << i << ": ";
                    callInst->getOperand(i)->print(errs());
                    errs() << '\n';
                    callInst->getOperand(i)->getType()->print(errs());
                    errs() << '\n';
                } */

                // errs() << "Operand 2: ";
                // callInst->getOperand(2)->print(errs());
                // errs() << '\n';
                if(llvm::ConstantExpr* CE = llvm::dyn_cast<llvm::ConstantExpr>(callInst->getOperand(2)))
                {
                    callInst->setOperand(2, CE->getBitCast(newF, callInst->getOperand(2)->getType()));
                }
                // errs() << '\n';
                // errs() << "getType: ";
                // callInst->getOperand(2)->getType()->print(errs());
                // errs() << '\n';
                // errs() << "Without pointer cast: ";
                // callInst->getOperand(2)->stripPointerCasts()->print(errs());
                // errs() << '\n';
                // callInst->getOperand(2)->stripPointerCasts()->getType()->print(errs());
                // errs() << '\n';
                return;
            }
            errs() << "Function: " << f->getName() << " Not defined\n";

            //if(f->getName().str() == "malloc")
            //{
            //    auto customMalloc = _M->getFunction("__libc_malloc_impl");
            //    if(customMalloc != nullptr)
            //    {
            //        errs() << "Found malloc defintion! Applying...\n";
            //        f = customMalloc;
            //    }
            //    else
            //    {
            //        return;
            //    }

            //}
            //else
            //{
            //    return;
            //}
            return;
        }

        // Don't instrument thread exit handler or we will cause a recursive crash
        if (f->getName().str() == "_Z14on_thread_exitv")
        {
            return;
        }
        // errs() << "Processing Function Call: " << f->getName() << "\n";
        // errs() << "Basic Block Count: " << f->size() << "\n";

        // Function is defined, proceed with clone
        Function *newF = processFunctionClone(f, PragmaRegionID);

        // Replace called function with clone
        callInst->setCalledFunction(newF);
    }

    Function *ZRayPass::processFunctionClone(Function *f, size_t PragmaRegionID)
    {
        // Have we cloned this function before? Check map
        auto searchMapping = CloneFunctionMap.find(f->getName().str());
        auto searchList = std::find(ClonedFunctionNames.begin(), ClonedFunctionNames.end(), f->getName().str());
        if (searchMapping != CloneFunctionMap.end())
        {
            // Hit, return previously cloned function
            return (searchMapping->second).first;
        }
        else if (searchList != ClonedFunctionNames.end())
        {
            // This is a function clone
            return f;
        }
        else
        {
            // Miss, clone function and store into map
            ValueToValueMapTy VMap;
            Function *newF = CloneFunction(f, VMap);
            CloneFunctionMap.insert({f->getName().str(), {newF, PragmaRegionID}});
            ClonedFunctionNames.push_back(newF->getName().str());

            // Mark function clone key to be instrumented
            PendingFunctionClones.push_back(f->getName().str());

            return newF;
        }
    }

    // Overload version for inserting Loop SF PD sets
    bool ZRayPass::insertPostDomSetEvents(const bb_sf_pair_vec *pd, size_t pragmaRegion, size_t groupID,
                                          Function &F, llvm::BasicBlock *ceBlock, bool IsIndirect)
    {
        Module *M = F.getParent();

        ProfileData profile;
        clearRecords(&profile);

        profile.PragmaRegionID = pragmaRegion;
        profile.GroupNumber = groupID;
        profile.EnableMIRPass = ApplyMIRPass;

        if (IsIndirect)
        {
            profile.PostDomSetID = IndirectFunctionCounterOffset;
            profile.IsIndirect = true;
        }
        else
        {
            profile.PostDomSetID = RegionCounterEventTotal;
        }

        // Update profile with block instructions and tag each block
        for (auto block_pair : *pd)
        {
            recordTagBlocks(&(block_pair.first), block_pair.second, profile, M);
        }

#ifdef IGNORE_BB
        if (profile.storeCount || profile.loadCount)
        {
#endif
            insertCounterArrayInc(M, ceBlock->getFirstInsertionPt(), profile);
            insertCustomEvent(M, F, profile, IsIndirect);

            for (auto block_pair : *pd)
            {
                BasicBlockCount += block_pair.first.size();
            }

            return true;

#ifdef IGNORE_BB
        }
#endif

        for (auto block_pair : *pd)
        {
            IgnoredBasicBlockCount += block_pair.first.size();
        }

        return false;
    }

    // Insert CE for given PD set of basic blocks
    bool ZRayPass::insertPostDomSetEvents(const std::vector<llvm::BasicBlock *> *pd, size_t pragmaRegion, size_t groupID,
                                          Function &F, bool IsIndirect, llvm::BasicBlock *ceBlock, size_t scaleFactor)
    {
        Module *M = F.getParent();

        ProfileData profile;
        clearRecords(&profile);

        profile.PragmaRegionID = pragmaRegion;
        profile.GroupNumber = groupID;
        profile.EnableMIRPass = ApplyMIRPass;

        if (IsIndirect)
        {
            profile.PostDomSetID = IndirectFunctionCounterOffset;
            profile.IsIndirect = true;
        }
        else
        {
            profile.PostDomSetID = RegionCounterEventTotal;
        }

        // Update profile with block instructions and tag each block
        recordTagBlocks(pd, scaleFactor, profile, M);

#ifdef IGNORE_BB
        if (profile.storeCount || profile.loadCount)
        {
#endif

            if (ceBlock == nullptr)
            {
                insertCounterArrayInc(M, (*pd)[0]->getFirstInsertionPt(), profile);
                insertCustomEvent(M, F, profile, IsIndirect);
            }
            else
            {
                insertCounterArrayInc(M, ceBlock->getFirstInsertionPt(), profile);
                insertCustomEvent(M, F, profile, IsIndirect);
            }

            BasicBlockCount += pd->size();

            return true;

#ifdef IGNORE_BB
        }
#endif

        IgnoredBasicBlockCount += pd->size();

        return false;
    }

    bool ZRayPass::insertDynamicLoopCounter(const std::vector<llvm::BasicBlock *> *blocks, size_t pragmaRegion, size_t groupID,
                                            Function &F, llvm::Loop *L, bool IsIndirect)
    {
        if (blocks->empty())
        {
            return false;
        }

        Module *M = F.getParent();

        ProfileData profile;
        clearRecords(&profile);

        profile.PragmaRegionID = pragmaRegion;
        profile.GroupNumber = groupID;
        profile.EnableMIRPass = ApplyMIRPass;

#ifdef IGNORE_BB
        if (profile.storeCount || profile.loadCount)
        {
#endif
            if (IsIndirect)
            {
                profile.PostDomSetID = IndirectFunctionCounterOffset;
                profile.IsIndirect = true;
            }
            else
            {
                profile.PostDomSetID = RegionCounterEventTotal;
            }

            auto oldHeader = L->getHeader();

            auto bb = llvm::SplitEdge(L->getLoopPredecessor(), L->getHeader());

            LI = &(getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo());

            PreDomTree->recalculate(F);
            PostDomTree->recalculate(F);

            SE = &(getAnalysis<ScalarEvolutionWrapperPass>(F).getSE());
            L = LI->getLoopFor(oldHeader);

            const SCEV *v = SE->getBackedgeTakenCount(L, llvm::ScalarEvolution::ExitCountKind::Exact);
            if (v->getSCEVType() == SCEVTypes::scCouldNotCompute || !SE->isLoopInvariant(v, L))
            {
                //Does leave a leftover basic block in this edge case, may want to clean that up.
                errs() << "Scalar evolution failed to regain SCEV compute! No longer hoisting.\n";
                return false;
            }
            //SE->forgetAllLoops(); //Doesn't seem to work, need full reset

            // Update profile with block instructions and tag each block
            recordTagBlocks(blocks, 1, profile, M);

            insertDynamicLoopEvent(M, F, L, bb->getFirstInsertionPt(), profile, IsIndirect);

            HoistedCounters++;
            DynHoistedCounters++;

            return true;
#ifdef IGNORE_BB
        }
#endif

        return false;
    }

    void ZRayPass::recordRegion(size_t begin, size_t end, ProfileData &regionProfile, Function &F, Module *M)
    {
        // TODO: Make sure loop begining/end bounds are instructions inside the loop for scale factor adjustment
        std::vector<LoopData> loopDataList = getLoopInRegion(begin, end, F);
        errs() << "Print results========\n";

        size_t pos = begin;
        // Record instruction types inside pragma region
        for (inst_iterator I = inst_begin(F) + begin,
                           E = inst_begin(F) + end;
             I != E; I++)
        {
            size_t scaleFactor = getScaleFactor(loopDataList, pos, F);
            recordInst((I.getInstructionIterator()), scaleFactor, regionProfile, M);
            pos++;
        }
    }

    // Analysis passes we want to run beforehand
    void ZRayPass::getAnalysisUsage(AnalysisUsage &AU) const
    {
        // Specify we need the loopinfo pass to run before this pass
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addRequired<ScalarEvolutionWrapperPass>();
        AU.addRequired<PostDominatorTreeWrapperPass>();
    }

    bool ZRayPass::runOnModule(Module &M)
    {
        GlobalCounterEventTotal = 0;
        RegionCounterEventMax = 0;
        RegionCounterEventTotal = 0;
        IndirectFunctionCounterOffset = 0;
        TimingEventCount = 0;
        BasicBlockCount = 0;
        TotalLoadCount = 0;
        TotalStoreCount = 0;
        IgnoredBasicBlockCount = 0;
        
        PostDomTree = new PostDominatorTree();
        PreDomTree = new DominatorTree();

        //Not used for instrumentation, but to learn more about what
        //types of loops are present in the workload.

        _M = &M;

        // size_t BBCountApp = 0;
        // ProfileData StaticMixProf;
        // clearRecords(&StaticMixProf);
        // std::ofstream MixLogFile;
        // MixLogFile.open("./IR_StaticMixInfo.log", std::ios::out);

        // for (auto curFunc = M.getFunctionList().begin(),
        //         endFunc = M.getFunctionList().end();
        //         curFunc != endFunc; curFunc++)
        // {
        //     size_t BBCountFunc = 0;
        //     ProfileData StaticMixFuncProf;
        //     clearRecords(&StaticMixFuncProf);
        //     for(BasicBlock &BB : *curFunc)
        //     {
        //         for(Instruction &I : BB)
        //         {
        //             // recordInst(BasicBlock::iterator(I), 1, StaticMixProf, &M);
        //             // recordInst(BasicBlock::iterator(I), 1, StaticMixFuncProf, &M);
        //         }
        //         BBCountApp++;
        //         BBCountFunc++;
        //     }

        //     MixLogFile << "Function: " << curFunc->getName().str() << "\n";
        //     MixLogFile << "Loads      : " << StaticMixFuncProf.LoadCount << "\n";
        //     MixLogFile << "Stores     : " << StaticMixFuncProf.StoreCount << "\n";
        //     MixLogFile << "BasicBlocks: " << BBCountFunc << "\n";
        //     MixLogFile << "Total\n";
        //     MixLogFile << "Loads      : " << StaticMixProf.LoadCount << "\n";
        //     MixLogFile << "Stores     : " << StaticMixProf.StoreCount << "\n";
        //     MixLogFile << "BasicBlocks: " << BBCountApp<< "\n";
        // }

        // MixLogFile.close();

        // Iterate over functions and check for pragmas
        bool modified = false;
        // CounterArrayRegionOffset = getArrayRegionOffset(&M);
        // LoadRuntimeArrayRegionOffset = getLoadRuntimeArrayRegionOffset(&M);
        // StoreRuntimeArrayRegionOffset = getStoreRuntimeArrayRegionOffset(&M);

        char *LogFileName = std::getenv("ZRAY_LOGFILE");

        errs() << "ZRAY: Entering Module Pass\n";

        if (LogFileName == nullptr)
        {
            errs() << "Missing output logfile name. Set env \"ZRAY_LOGFILE\"\n";
            return 0;
        }

        if (FullScan)
        {
            errs() << "IR PASS: FULL SCAN\n";
        }
        else
        {
            errs() << "IR PASS: SELECTIVE SCAN\n";
        }

        // Identify pragma regions
        for (auto curFunc = M.getFunctionList().begin(),
                  endFunc = M.getFunctionList().end();
             curFunc != endFunc; curFunc++)
        {
            CollectLoopStatistics(*curFunc, AppLS);
            bool FoundPragma =  runOnFunction(*curFunc, _M, FullScan);

            modified = modified | FoundPragma;
        }

        PragmaRegionLogFile.open(LogFileName, std::ios::out | std::ios::binary);

        if (modified)
        {
            // Clone and insert functions in pragma regions.
            for (int i = 0; i < PRList.size(); i++)
            {
                // Identify functions we can clone in each pragma region.
                // Replace call inst with call to cloned function.
                for (auto block : PRList[i]->BBList)
                {
                    for (BasicBlock::iterator inst = block->begin(); inst != block->end(); inst++)
                    {
                        // May need to clone functions called using call and invoke
                        if ((isa<CallInst>(inst)) || (isa<InvokeInst>(inst)))
                        {
                            llvm::CallBase *callInst = llvm::cast<llvm::CallBase>(inst);
                            processCallInst(callInst, i);
                        }
                    }
                }
            }


            ROI_Count = PRList.size();
            // Apply instrumentation
            for (auto PR : PRList)
            {
                InstrumentPragmaRegion(PR, false);
            }

            size_t maxIndirect = 0;
            size_t MaxIndirectRegionSize[ROI_Count];
            for (int i = 0; i < ROI_Count; i++)
            {
                MaxIndirectRegionSize[i] = 0;
            }

            if(ApplyFunctionCloning)
            {
                //Apply instrumentation to cloned functions
                while (!PendingFunctionClones.empty())
                {
                    auto tmpVec = PendingFunctionClones;
                    for (auto key : tmpVec)
                    {
                        IndirectFunctionCounterOffset = RegionCounterEventMax;
                        auto func = CloneFunctionMap[key].first;

                        auto OrderedCFG = orderBasicBlocks(*func);
                        recordStackAddresses(OrderedCFG);

                        PragmaRegion * ClonePR  = new PragmaRegion();

                        ClonePR->GroupID = 0;
                        // ClonePR->PragmaRegionID = PRList.size();
                        ClonePR->PragmaRegionID = CloneFunctionMap[key].second;
                        ClonePR->F = func;
                        ClonePR->BBList = OrderedCFG;

                        PRList.push_back(ClonePR);

                        InstrumentPragmaRegion(ClonePR, true);

                        PendingFunctionClones.erase(find(PendingFunctionClones.begin(), PendingFunctionClones.end(), key));

                        if(IndirectFunctionCounterOffset > maxIndirect)
                        {
                            maxIndirect = IndirectFunctionCounterOffset;
                            if(maxIndirect > RegionCounterEventMax)
                            {
                                RegionCounterEventMax = maxIndirect;
                            }
                        }

                    }
                }
            }

            // Insert init function
            for (auto curFunc = M.getFunctionList().begin(),
                    endFunc = M.getFunctionList().end();
                    curFunc != endFunc; curFunc++)
            {
                if (curFunc->getName() == "main")
                {
                    modified = modified | initMain(*curFunc);
                    break;
                }
            }

            // Calculate required counter array width
            if(maxIndirect > RegionCounterEventMax)
            {
                CountArraySize = maxIndirect;
            }
            else
            {
                CountArraySize = RegionCounterEventMax;
            }
            //CountArraySize = IndirectFunctionCounterOffset;

            errs() << "At end , list size is " << PRList.size() << " ROI count is : " << ROI_Count << "\n";
            // Allocate global counter array and update placeholder instructions
            setArrayWidth(&M, CountArraySize);
            setPragmaRegionCount(&M, ROI_Count);
            CounterArray = getArray(&M, CountArraySize * ROI_Count);
            LoadRuntimeArray = getLoadRuntimeArray(&M, CountArraySize * ROI_Count);
            StoreRuntimeArray = getStoreRuntimeArray(&M, CountArraySize * ROI_Count);
            processPragmaPtrInstList(&M);

        }

        PragmaRegionLogFile.close();

        errs() << "Pragma Regions found : " << PRList.size() << "\n";
        errs() << "Timing Events Inserted: " << TimingEventCount << "\n";
        errs() << "Total Counters Inserted: " << GlobalCounterEventTotal << "\n";
        errs() << "Most Counters in Region: " << RegionCounterEventMax << "\n";
        errs() << "BB Covered: " << BasicBlockCount << "\n";
        errs() << "BB Ignored: " << IgnoredBasicBlockCount << "\n";
        errs() << "Loads Covered: " << TotalLoadCount << "\n";
        errs() << "Stores Covered: " << TotalStoreCount << "\n";

        errs() << "==========Application Loop Analysis:\n";

        errs() << "Total Loops: " << AppLS.TotalLoops << "\n";
        errs() << "Static Bound Loops: " << AppLS.TotalLoopStaticBounds << "\n";
        errs() << "Dynamic Bound Loops: " << AppLS.TotalLoopDynBounds << "\n";
        errs() << "Invariant Trip Counts: " << AppLS.TotalLoopInvarBackEdge << "\n";
        errs() << "Canonical Loops: " << AppLS.TotalLoopCanonical << "\n";
        errs() << "----------\n";
        errs() << "---Functions Containing ROI Loop Stats:\n";
        errs() << "Total Loops: " << ROILS.TotalLoops << "\n";
        errs() << "Static Bound Loops: " << ROILS.TotalLoopStaticBounds << "\n";
        errs() << "Dynamic Bound Loops: " << ROILS.TotalLoopDynBounds << "\n";
        errs() << "Invariant Trip Counts: " << ROILS.TotalLoopInvarBackEdge << "\n";
        errs() << "Canonical Loops: " << ROILS.TotalLoopCanonical << "\n";
        errs() << "---\n";

        errs() << "Counters Hoisted " << HoistedCounters << "\n";
        errs() << "Static Loops Hoisted " << StaticHoistedCounters << "\n";
        errs() << "Runtime Loops Hoisted " << DynHoistedCounters << "\n";
        errs() << "Static Basic Blocks Covered : " << StaticBasicBlocksHoisted << "\n";
        errs() << "Runtime Basic Blocks Covered : " << DynBasicBlocksHoisted << "\n";

        return modified;
    }

    // Pass to insert call to runtime init function.
    bool ZRayPass::initMain(Function &F)
    {
        DILocation *DI = nullptr;
        for (auto &I : F.getEntryBlock())
        {
            if (I.getDebugLoc())
            {
                DI = I.getDebugLoc();
                break;
            }
        }

        Module *M = F.getParent();
        setCustomEventHandler(M, inst_begin(F));
        if(EnableHostMonitor)
        {
            initHostInterface(M, inst_begin(F), DI);
        }

        return true;
    }

    // Pass to run on each function @F
    bool ZRayPass::runOnFunction(Function &F, Module *M, bool InstAll, bool IsIndirect)
    {
        // Do nothing if function does not contain an instrumentation pragma
        if (F.empty() || (!InstAll && detectPragma(F) == false))
        {
            return false;
        }

        errs() << "+++++++++++++++++++++++\n";
        errs() << "Parsing Function: ";
        errs().write_escaped(F.getName()) << '\n';

        BasicBlockGroupList.clear();

        PostDomTree->recalculate(const_cast<Function &>(F));
        PreDomTree->recalculate(const_cast<Function &>(F));

        errs() << "Ordering basic blocks...\n";

        auto OrderedCFG = orderBasicBlocks(F);

        recordStackAddresses(OrderedCFG);

        // Test out new function for splitting basic blocks
        if (!InstAll) {
            errs() << "Split basic blocks...\n";
            splitInstrumentedBlocks(OrderedCFG[0], M, F, PRList.size());
        }
        errs() << "Reorder basic blocks...\n";
        OrderedCFG = orderBasicBlocks(F);
        // recordStackAddresses(OrderedCFG);

        // Recalculate loop info, domination trees and SE
        LI = &(getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo());
        PostDomTree->recalculate(const_cast<Function &>(F));
        PreDomTree->recalculate(const_cast<Function &>(F));
        SE = &(getAnalysis<ScalarEvolutionWrapperPass>(F).getSE());

        if (!InstAll)
        {
            // Record pragma bounds/IDs
            errs() << "Identifying region of interest basic blocks...\n";
            recordPragmaRegions(OrderedCFG[0], std::vector<szt_bbvec>(), PreDomTree);
        }
        else
        {
            // Instrument entire function
            errs() << "Instrumenting all blocks...\n";
            BasicBlockGroupList.push_back(szt_bbvec(0, OrderedCFG));
        }

        errs() << "Identified " << BasicBlockGroupList.size() << " region(s) of interest.\n";

        if (!BasicBlockGroupList.empty())
        {
            for (int i = 0; i < BasicBlockGroupList.size(); i++)
            {
                // TODO: Consider recording immediate parent pragma region ID,
                // could be useful when updating indirect pointers
                PragmaRegion *tmpPR = new PragmaRegion();
                tmpPR->PragmaRegionID = PRList.size();
                tmpPR->GroupID = BasicBlockGroupList[i].first;
                tmpPR->BBList = BasicBlockGroupList[i].second;
                tmpPR->F = &F;
                PRList.push_back(tmpPR);
            }
        }

        return true;
    }

    void ZRayPass::InstrumentPragmaRegion(PragmaRegion *PR, bool IsIndirect)
    {
        //  Process detected pragma regions
        if (PR->BBList.empty())
        {
            return;
        }

        Function *F = PR->F;

        errs() << "-----------------------\n";
        errs() << "Instrumenting Function: ";
        errs().write_escaped(F->getName()) << '\n';

        LI = &(getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo());
        PostDomTree->recalculate(const_cast<Function &>(*F));
        PreDomTree->recalculate(const_cast<Function &>(*F));
        SE = &(getAnalysis<ScalarEvolutionWrapperPass>(*F).getSE());

        auto OrderedCFG = orderBasicBlocks(*F);

        DILocation *DI = nullptr;
        for (auto &I : F->getEntryBlock())
        {
            if (I.getDebugLoc())
            {
                DI = I.getDebugLoc();
                break;
            }
        }
        // Add object to be deconstructed on thread exit for cleanup code in functions that are not clones
        // FIXME: Test/confirm that the thread exit handler is needed for cloned omp outlines
        if(!IsIndirect || (F->getName().substr(0, 4) == ".omp"))
        // if(!IsIndirect)
        {
            insertThreadExitHandler(_M, F->getEntryBlock().getFirstInsertionPt(), DI, EnableHostMonitor);

            if(EnableHostMonitor)
            {
                errs() << "Host Monitoring: Applying thread registration.\n";
                registerThreadInterface(_M, F->getEntryBlock().getFirstInsertionPt(), DI);
            }
        }

        size_t b, e;
        size_t pragmaRegion = PR->PragmaRegionID;

        ProfileData regionProfile;
        size_t groupID = PR->GroupID;
        bool insertedSled = false;
        RegionCounterEventTotal = 0;
        LOOP_SF_SET = 1; // Numbering scheme for creating SF sets
        clearRecords(&regionProfile);
        regionProfile.EnableMIRPass = ApplyMIRPass;

        errs() << "Pragma Region " << pragmaRegion << "\n";
        errs() << "Group ID: " << groupID << "\n";

        llvm::BasicBlock *beginBlock = nullptr, *endBlock = nullptr;
        std::vector<llvm::BasicBlock *> *npd = &PR->BBList;

        // Don't update counter array TLS pointer when entering called function
        if (!IsIndirect)
        {
            // Set instruction to update TLS pointer for selecting counter array
            registerPragmaPtrInst(_M, (*npd)[0], pragmaRegion);
        }

        b = e = getBBOffset(OrderedCFG, (*npd)[0]);
        for (auto bb : *npd)
        {
            size_t tmp = getBBOffset(OrderedCFG, bb);
            if (tmp > e)
            {
                e = tmp;
                endBlock = bb;
            }
            if (tmp - bb->size() < b)
            {
                b = tmp - bb->size();
                beginBlock = bb;
            }
        }

        if(detectPragma(beginBlock)) {
            errs() << "Detected pragma in b\n";
            size_t tmp = getPreviousBBOffset(OrderedCFG, beginBlock);
            if (tmp < b) {
                b = tmp;
            }
        }

        errs() << "Region Bounds : " << b << " " << e << "\n";
        errs() << "Basic Block Count : " << npd->size() << "\n";

        regionProfile.PragmaRegionID = pragmaRegion;

        // Don't insert timer events into called functions
        if (!IsIndirect)
        {
            // If begin and end fall into same basic block
            if (endBlock == nullptr)
                endBlock = beginBlock;
        }

        // Instrument blocks that are part of loops========================

        if (ApplyLoopHoisting)
        {
            // Create tree of loops
            LoopTreeNode *loop_tree_root = createLoopTree(OrderedCFG, b, e);

            errs() << "Coloring loop tree\n";
            colorLoopTreeSF(loop_tree_root);
            errs() << "Done coloring loop tree\n";

            // Iterate over SF sets
            for (int j = 1; j < LOOP_SF_SET; j++)
            {
                // Blocks that PD loop header and their scale factor
                bb_sf_pair_vec header_pd_blocks;

                errs() << "SET : " << j << "----------------------------\n";

                errs() << "Searching for set header\n";
                // Get top node of SF subtree
                LoopTreeNode *lroot = findLoopTreeSet(loop_tree_root, j);
                errs() << "Done\n";

                // Instrument blocks that don't PD loop headers and collect nested SF info
                insertedSled |= instrumentSFLoopSet(lroot, header_pd_blocks, PostDomTree, 1, j, pragmaRegion, groupID, *npd, *F, IsIndirect);

                // Create basic block before outer loop header for later use as instrumentation point
                auto bb = llvm::SplitEdge(lroot->NodeData->loop->getLoopPredecessor(), lroot->NodeData->loop->getHeader());

                HoistedCounters++;
                StaticHoistedCounters++;

                StaticBasicBlocksHoisted += header_pd_blocks.size();
                // Instrument blocks we can apply a SF to and insert instrumentation point into BB
                insertedSled |= insertPostDomSetEvents(&header_pd_blocks, pragmaRegion, groupID, *F, bb, IsIndirect);
            }

            // Instrument remaining loops we could not calculate SF for
            insertedSled |= instrumentDynamicLoops(loop_tree_root, PostDomTree, pragmaRegion, groupID, *npd, *F, IsIndirect);

            // Remove loop BBs from pool - UPDATE: We no longer do this. Instead, basic blocks
            // selected for analysis during instrumentSFLoopSet or insertPostDomSetEvents are immediately
            // removed from npd in those functions - they directly alter the basic block list.
            // removeLoopBB(*npd, *F);
        }

        // Instrument non-loop blocks
        insertedSled |= instrumentPostDomSet(npd, PostDomTree, regionProfile.PragmaRegionID, groupID, *F, IsIndirect);

        // We only add the start/initial timing event at the end
        // This ensures that a counter increment for the first block in a region
        // will be called after we enable sampling in the timing event
        // Don't insert timer events into called functions
        if (!IsIndirect)
        {
            insertStartTimerEvent(_M, beginBlock->getFirstInsertionPt(), pragmaRegion);
            if (FullScan)
            {
                insertEndTimerEvent(_M, --endBlock->end(), pragmaRegion);
            }
        }

        if (!insertedSled)
        {
            errs() << "-------^^^Not inserted^^^-------\n";
        }
        else
        {
            errs() << "+++++++^^^Inserted^^^+++++++\n";
        }

        errs() << "Region " << pragmaRegion << ": " << RegionCounterEventTotal << " counters...\n";

        if (!IsIndirect && RegionCounterEventTotal > RegionCounterEventMax)
        {
            RegionCounterEventMax = RegionCounterEventTotal;
        }

        // TODO: We should be printing out the counts specific to this function. Right now this is across all functions
        errs() << "========================\n";
        errs().write_escaped(F->getName()) << '\n';
        errs() << "Timing Events Inserted: " << TimingEventCount << "\n";
        errs() << "Total Counters Inserted: " << GlobalCounterEventTotal << "\n";
        errs() << "BB Covered: " << BasicBlockCount << "\n";
        errs() << "BB Ignored: " << IgnoredBasicBlockCount << "\n";
        errs() << "Loads Covered: " << TotalLoadCount << "\n";
        errs() << "Stores Covered: " << TotalStoreCount << "\n";
        errs() << "========================\n";
    }

    char ZRayPass::ID = 0;
    static RegisterPass<ZRayPass> X("zray", "ZRay: region-level memory traffic instrumentation", false, false);

    static RegisterStandardPasses Y(
        PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
        [](const PassManagerBuilder &Builder,
           legacy::PassManagerBase &PM)
        { PM.add(new ZRayPass()); });
} // namespace zray
