// ZRay: portable compiler-assisted memory traffic characterization.
// Interfaces for the ZRay LLVM pass.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

// TODO: Standardize name formatting, usage of pointers versus references, its getting kinda bad

// Name Space:      SNAKE_CASE
// Type Names:      snake_case
// Variable Names:  PascalCase
// Class Names:     PascalCase
// Function Names:  camelCase
#ifndef ZRAY_PASS_H
#define ZRAY_PASS_H

#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/PostDominators.h>
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instruction.def>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/DebugInfoMetadata.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Pass.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <inttypes.h>
#include <fstream>

#include "zray_dyn.h"
#include "zray_util.h"

namespace zray
{
    // #define IGNORE_BB
    using namespace llvm;

    typedef std::vector<std::pair<std::vector<llvm::BasicBlock *>, size_t>> bb_sf_pair_vec;

    typedef std::pair<size_t, std::vector<llvm::BasicBlock *>> szt_bbvec;

    struct PragmaRegion
    {
        std::vector<BasicBlock*> BBList;
        Function * F;
        size_t PragmaRegionID;
        size_t GroupID;
        int32_t ParentRegionID;
    };


    // Compiler pass to insert instrumentation
    // Since we are modifying global address space,
    // module pass is more appropriate than function pass
    struct ZRayPass : public ModulePass
    {
        static char ID;

        //bool ApplyPostDomSets = false;
        //bool ApplyLoopHoisting;

        size_t HoistedCounters = 0;
        size_t StaticHoistedCounters = 0;
        size_t DynHoistedCounters = 0;
        size_t StaticBasicBlocksHoisted = 0;
        size_t DynBasicBlocksHoisted = 0;
        size_t ROI_Count = 0;

        struct LoopStats
        {
            size_t TotalLoops = 0;
            size_t TotalLoopInvarBackEdge = 0;
            size_t TotalLoopCanonical = 0;
            size_t TotalLoopStaticBounds = 0;
            size_t TotalLoopDynBounds = 0;
        };

        size_t BasicBlockCount;
        size_t IgnoredBasicBlockCount;
        size_t RegionCounterEventTotal;
        size_t RegionCounterEventMax;
        size_t IndirectFunctionCounterOffset;
        size_t GlobalCounterEventTotal;
        size_t TimingEventCount;
        size_t TotalLoadCount;
        size_t TotalStoreCount;
        size_t CountArraySize;

        llvm::PostDominatorTree *PostDomTree;
        llvm::DominatorTree *PreDomTree;

        // TODO: See if we can use some sort of referenced datastructure to reduce number of searches made
        std::vector<szt_bbvec> BasicBlockGroupList;
        std::set<BasicBlock *> BasicBlocksVisited;
        std::set<BasicBlock *> SplitBasicBlocksVisited;

        GlobalVariable *CounterArray;
        GlobalVariable *CounterArrayRegionOffset;
        GlobalVariable *LoadRuntimeArray;
        GlobalVariable *StoreRuntimeArray;
        GlobalVariable *LoadRuntimeArrayRegionOffset;
        GlobalVariable *StoreRuntimeArrayRegionOffset;
        std::vector<StoreInst *> RegionOffsetUpdateInstList;
        std::vector<StoreInst *> LoadRuntimeRegionOffsetUpdateInstList;
        std::vector<StoreInst *> StoreRuntimeRegionOffsetUpdateInstList;

        std::ofstream PragmaRegionLogFile;

        //std::vector<llvm::BasicBlock *> OrderedCFG;

        std::unordered_map<llvm::Value *, size_t> StackAddressList;

        std::map<std::string, std::pair<Function *, size_t>> CloneFunctionMap;
        std::vector<std::string> ClonedFunctionNames;
        std::vector<std::string> PendingFunctionClones;

        std::vector<PragmaRegion*> PRList;

        ZRayPass() : ModulePass(ID) {}

        // TODO: Verify new pragma region counter is working
        // std::vector<std::pair<size_t, size_t>> boundsList;
        //size_t PragmaRegionCount = 0;
        // std::vector<size_t> groupIDList;

        ScalarEvolution *SE;
        LoopInfo *LI;
        Module *_M;

        // Compiler pass methods
        virtual void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool runOnModule(Module &M) override;
        bool runOnFunction(Function &F, Module *M, bool InstAll = false, bool IsIndirect = false);
        bool runOnIndirectFunction(Function &F);
        bool initMain(Function &F);

        // Parsing
        // size_t getBBOffset(Function::iterator FI, Function &F);
        size_t getGroupID(std::string Inst);
        bool isToolFlag(std::string Inst, std::string PragmaName);
        bool isIfElse(std::string Name);
        void recordInst(llvm::BasicBlock::iterator I, size_t ScaleFactor, ProfileData &Profile, Module *M);
        void clearRecords(ProfileData *Profile);
        void splitInstrumentedBlocks(BasicBlock * /*, std::vector<szt_bbvec>, DominatorTree * */, Module *M, Function &F, size_t pragmaRegionID);
        void recordPragmaRegions(BasicBlock *, std::vector<szt_bbvec>, DominatorTree *);
        void recordRegion(size_t Begin, size_t End, ProfileData &RegionProfile, Function &F, Module *M);
        std::vector<llvm::BasicBlock *> *getBasicBlocks(size_t Begin, size_t End, Function &F);
        size_t getBBOffset(std::vector<llvm::BasicBlock*> OrderedCFG, llvm::BasicBlock *I);
        size_t getPreviousBBOffset(std::vector<llvm::BasicBlock*> OrderedCFG, llvm::BasicBlock *I);
        std::vector<llvm::BasicBlock *> orderBasicBlocks(Function &F);
        void recordStackAddresses(std::vector<llvm::BasicBlock*> BlockList);
        bool detectPragma(Function &F);
        bool detectPragma(BasicBlock *bb);
        std::vector<std::vector<llvm::BasicBlock *> *> *createPostDomSets(std::vector<llvm::BasicBlock *> *NonPostDomSet,
                                                                          const llvm::PostDominatorTree *PostDomTree);

        // Code generation/modification
        GlobalVariable *getConstantInt64GV(Module *M, size_t Val);
        GlobalVariable *getProfileStruct(Module *M, ProfileData Profile);
        GlobalVariable *getArray(Module *M, size_t Val);
        GlobalVariable *getLoadRuntimeArray(Module *M, size_t Val);
        GlobalVariable *getStoreRuntimeArray(Module *M, size_t Val);
        GlobalVariable *setArrayWidth(Module *M, size_t Val);
        GlobalVariable *setPragmaRegionCount(Module *M, size_t Val);
        GlobalVariable *getArrayRegionOffset(Module *M);
        GlobalVariable *getLoadRuntimeArrayRegionOffset(Module *M);
        GlobalVariable *getStoreRuntimeArrayRegionOffset(Module *M);
        GetElementPtrInst *getArrayElementPtr(Module *M, GlobalVariable *Array, size_t Index);
#if 0
        GetElementPtrInst *getArrayElementPtr(Module *M, GlobalVariable *array, llvm::Value * index);
#endif
        void setCustomEventHandler(Module *M, inst_iterator PositionIterator);
        void insertThreadExitHandler(Module *M, llvm::BasicBlock::iterator posI, llvm::DILocation *DI);
        void insertCounterArrayInc(Module *M, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile);
        void insertLoadArrayInc(Module *M, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile, Value *Size);
        void insertStoreArrayInc(Module *M, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile, Value *Size);
        void insertLoadStoreArrayInc(Module *M, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile, Value *Size);
        // void insertCustomEvent(Module *M, Function &F, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile, bool IsIndirect);
        void insertCustomEvent(Module *M, Function &F, const ProfileData &Profile, bool IsIndirect);
        void initHostInterface(Module *M, inst_iterator posI, llvm::DILocation *DI);
        void registerThreadInterface(Module *M, llvm::BasicBlock::iterator posI, llvm::DILocation *DI);
        void unregisterThreadInterface(Module *M, llvm::BasicBlock::iterator posI, llvm::DILocation *DI);
        void insertThreadExitHandler(Module *M, llvm::BasicBlock::iterator posI, llvm::DILocation *DI, bool EnableMonitor);
        //void insertCustomEvent(Module *M, Function &F, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile, bool IsIndirect);
        void insertEndMarker(Module *M, llvm::BasicBlock::reverse_iterator posI);
        void insertLoadRuntimeIntrinsicEvent(Module *M, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile, Value * size);
        void insertStoreRuntimeIntrinsicEvent(Module *M, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile, Value * size);
        void insertDynamicLoopEvent(Module *M, Function &F, llvm::Loop *L, llvm::BasicBlock::iterator posI, const ProfileData &profile, bool IsIndirect);
        void insertTimerEvent(Module *M, llvm::BasicBlock::iterator PositionIterator, const ProfileData &Profile);
        void insertStartTimerEvent(Module *M, llvm::BasicBlock::iterator PositionIterator, const size_t &pragmaRegionID);
        void insertEndTimerEvent(Module *M, llvm::BasicBlock::iterator PositionIterator, const size_t  &pragmaRegionID);
        size_t stripTimingEvents(Function *F);
        void insertBBTag(Module *M, llvm::BasicBlock::iterator PositionIterator, size_t CustomEventID, size_t ScaleFactor);
        void insertDiamondEvents(ProfileData &Profile, size_t RegionStart, size_t &RegionEnd, Function &F);
        void recordTagBlocks(const std::vector<llvm::BasicBlock *> *PostDomSet, size_t ScaleFactor, ProfileData &Profile, Module *M);
        bool instrumentPostDomSet(std::vector<llvm::BasicBlock *> *NonPostDomSet, PostDominatorTree *PostDomTree,
                                  size_t PragmaRegionID, size_t GroupNumber, Function &F, bool IsIndirect);
        bool insertPostDomSetEvents(const std::vector<llvm::BasicBlock *> *PostDomSet, size_t PragmaRegionID,
                                    size_t GroupNumber, Function &F, bool IsIndirect, llvm::BasicBlock *TargetBlock = nullptr, size_t ScaleFactor = 1);
        bool insertPostDomSetEvents(const bb_sf_pair_vec *pd, size_t PragmaRegionID, size_t GroupNumber,
                                    Function &F, llvm::BasicBlock *TargetBlock, bool IsIndirect);
        bool insertDynamicLoopCounter(const std::vector<llvm::BasicBlock *> *blocks, size_t pragmaRegion, size_t groupID,
                                      Function &F, llvm::Loop *L, bool IsIndirect);
        void insertPrint(Module *M, IRBuilder<> * builder, std::string message, Value * val = nullptr);
        void registerPragmaPtrInst(Module *M, BasicBlock *BB, size_t GroupID);
        void applyPragmaPtrInst(Module *M, StoreInst *Store);
        void applyLoadRuntimePragmaPtrInst(Module *M, StoreInst *Store);
        void applyStoreRuntimePragmaPtrInst(Module *M, StoreInst *Store);
        void processPragmaPtrInstList(Module *M);
        void InstrumentPragmaRegion(PragmaRegion * PR, bool IsIndirect);

        void processCallInst(CallBase *callInst, size_t PragmaRegionID);
        Function *processFunctionClone(Function *f, size_t PragmaRegionID);
        // Loops

        struct LoopData
        {
            Loop *loop;
            size_t SetColor;
            size_t ScaleFactor;
            BasicBlock * Header; //Save header so we can use it to recover loops later

            LoopData(Loop *L)
            {
                loop = L;
                SetColor = 0;
                ScaleFactor = 0;

                if(L != nullptr)
                {
                    Header = L->getHeader();
                }
                else
                {
                    Header = nullptr;
                }
            }

            void print()
            {
                errs() << "--------------------------\n";
                errs() << "SF Value : " << ScaleFactor << "\n";
                errs() << "SF Set : " << SetColor << "\n";

                if (loop == nullptr)
                    errs() << "Loop is null\n";
                else
                {
                    errs() << *loop << "\n";
                    //   for(auto Begin : loop->getBlocks())
                    //   {
                    //       errs() << "\t" << *Begin << "\n";
                    //   }
                }
                errs() << "--------------------------\n";
            }
        };

        struct LoopTreeNode
        {
            LoopTreeNode *ParentNode;
            std::vector<LoopTreeNode *> ChildList;
            LoopData *NodeData;

            LoopTreeNode(LoopTreeNode *P, LoopData *D)
            {
                ParentNode = P;
                NodeData = D;
            }

            void print()
            {
                errs() << "Node:\n";
                if (NodeData != nullptr)
                {
                    NodeData->print();
                }
                else
                {
                    errs() << "nullptr\n";
                }

                errs() << "VVVV Children VVV\n";
                for (auto c : ChildList)
                {
                    c->print();
                }
                errs() << "^^^^ Children ^^^^\n";
            }
        };

        //Application wide loop stats
        LoopStats AppLS; 
        //ROI loop stats
        LoopStats ROILS;

        std::vector<LoopData> getLoopInRegion(size_t Begin, size_t End, Function &F);
        bool getBackedgeTakenCount(const Loop *L, size_t *Count);
        size_t getScaleFactor(std::vector<LoopData> LoopList, size_t Position, Function &F);
        LoopData loopToLoopData(Loop *l, Function &F);
        std::vector<LoopData> loopVToLoopDataV(std::vector<Loop *> LoopVector, Function &F);
        void removeLoopBB(std::vector<llvm::BasicBlock *> &NonPostDomSet, Function &F);
        void insertLoopSFTags(Function &F);
        bool processLoops(Function &F);

        size_t LOOP_SF_SET;
        LoopTreeNode *createLoopTree(std::vector<llvm::BasicBlock*> OrderedCFG, size_t Begin, size_t End);
        LoopTreeNode *findLoopTreeSet(LoopTreeNode *Root, size_t ID);
        // void _createLoopTree(Loop_Tree_Node *Root);
        void _createLoopTree(std::vector<llvm::BasicBlock*> OrderedCFG, LoopTreeNode * Parent, Loop * Loop, size_t Begin, size_t End);
        void colorLoopTreeSF(LoopTreeNode *Root);
        void getUniqueLoopBB(llvm::Loop *L, std::vector<llvm::BasicBlock *> &BasicBlockList);
        bool instrumentSFLoopSet(LoopTreeNode *Root, bb_sf_pair_vec &HeaderPostDomBlocks, PostDominatorTree *PostDomTree,
                                 size_t OuterScaleFactor, size_t SetID, size_t PragmaRegionID, size_t GroupNumber,
                                 std::vector<llvm::BasicBlock *> &PragmaBasicBlocks, Function &F, bool IsIndirect);
        bool _instrumentDynamicLoops(LoopTreeNode *Root, PostDominatorTree *PostDomTree, size_t PragmaRegionID,
                                     size_t GroupNumber, std::vector<llvm::BasicBlock *> &PragmaBasicBlocks, Function &F, bool IsIndirect);
        bool instrumentDynamicLoops(LoopTreeNode *Root, PostDominatorTree *PostDomTree, size_t PragmaRegionID,
                                    size_t GroupNumber, std::vector<llvm::BasicBlock *> &PragmaBasicBlocks, Function &F, bool IsIndirect);
        void recolorSFLoopSet(LoopTreeNode *Root, size_t OldID);

        void CollectLoopStatistics(Function &F, LoopStats &LS);
        void _CollectLoopStatistics(Loop * L, LoopStats &LS);
        void CollectSingleLoopStatistics(Loop *L, LoopStats &LS);
        // void refineLoopTree(Loop_Tree_Node *Root, size_t Begin, size_t End);
    };

    size_t getBBOffset(Function::iterator FI, Function &F);
}

#endif
