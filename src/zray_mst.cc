// ZRay: portable compiler-assisted memory traffic characterization.
// MST (LLVM PGO-style) counter placement strategy.
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

/******************************
 * MST (LLVM PGO-style) counter placement strategy for ZRay.
 *
 * Selected with --placement=mst. Instead of ZRay's native post-dominator-set +
 * loop-hoisting placement, this instruments the non-tree edges of LLVM's
 * maximum-weight spanning tree, matching the placement LLVM PGO actually ships.
 *
 * This exists to answer one question: how does ZRay's placement compare to
 * LLVM PGO's, in counters emitted and runtime overhead, while producing the same
 * measurement? Placement is the only independent variable -- the counter array,
 * the runtime increment call, the timing events and the thread-exit scaffolding
 * are all shared verbatim with the native arms.
 *
 * Two things placement drags along with it, which the comparison must account for:
 *
 *   1. A ZRay counter *is* a block execution count. An MST counter is an EDGE
 *      traversal count, so per-block counts must be recovered by solving flow
 *      over the spanning tree. That happens offline in post_process, never in
 *      the timed path, so the overhead comparison stays honest.
 *
 *   2. Recovering block counts needs the CFG topology, which ZRay's flat
 *      ProfileData log does not carry. This arm therefore emits a separate
 *      per-function sidecar; see include/zray_mst.h for the format.
 *
 * Requires --full-scan: CFGMST operates over a whole function. Applying it to a
 * pragma ROI would require closing the region's sub-CFG against a synthetic
 * super-node over its boundary edges, or outlining the region into a function --
 * transformations LLVM PGO does not perform.
 */
#include "zray_pass.h"
#include "zray_mst.h"

#include <unordered_map>

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"

// CFGMST.h is an internal LLVM library header -- not installed by an LLVM
// distribution -- so it is vendored under include/third_party. We bind to LLVM's
// real MST implementation rather than reimplement it, so that this arm measures
// PGO's actual edge-weighting and tree construction.
#include "third_party/CFGMST.h"

namespace
{
    // Minimal Edge / BBInfo types satisfying CFGMST's requirements, mirroring
    // PGOEdge / BBInfo in llvm's PGOInstrumentation.cpp (which are not exported).
    struct ZEdge
    {
        const llvm::BasicBlock *SrcBB;
        const llvm::BasicBlock *DestBB;
        uint64_t Weight;
        bool InMST = false;
        bool Removed = false;
        bool IsCritical = false;

        ZEdge(const llvm::BasicBlock *Src, const llvm::BasicBlock *Dest, uint64_t W = 1)
            : SrcBB(Src), DestBB(Dest), Weight(W) {}

        std::string infoString() const
        {
            return (llvm::Twine(Removed ? "-" : " ") + (InMST ? " " : "*") +
                    (IsCritical ? "c" : " ") + "  W=" + llvm::Twine(Weight))
                .str();
        }
    };

    struct ZBBInfo
    {
        ZBBInfo *Group;
        uint32_t Index;
        uint32_t Rank = 0;

        ZBBInfo(unsigned IX) : Group(this), Index(IX) {}

        std::string infoString() const
        {
            return (llvm::Twine("Index=") + llvm::Twine(Index)).str();
        }

        // Only meaningful for the use/reconstruction side; unused here.
        void addOutEdge(ZEdge *) {}
        void addInEdge(ZEdge *) {}
    };
} // anonymous namespace

namespace
{
    // Choose where to physically place an edge counter, splitting the edge if it
    // is critical (matching real LLVM PGO). Returns the block the counter lives
    // in and an insertion iterator; may mutate the CFG via SplitEdge, in which
    // case DidSplitOut is set so the caller can charge the added block to this
    // arm's static cost.
    llvm::BasicBlock::iterator edgeInsertionPoint(const llvm::BasicBlock *SrcC,
                                                  const llvm::BasicBlock *DstC,
                                                  llvm::BasicBlock *&PlacedOut,
                                                  bool &DidSplitOut)
    {
        DidSplitOut = false;
        using namespace llvm;
        if (SrcC == nullptr)
        {
            // Fake entry edge: count function invocations at the entry block.
            BasicBlock *D = const_cast<BasicBlock *>(DstC);
            PlacedOut = D;
            return D->getFirstInsertionPt();
        }
        if (DstC == nullptr)
        {
            // Fake exit edge: count exits just before the source's terminator.
            BasicBlock *S = const_cast<BasicBlock *>(SrcC);
            PlacedOut = S;
            return S->getTerminator()->getIterator();
        }

        BasicBlock *S = const_cast<BasicBlock *>(SrcC);
        BasicBlock *D = const_cast<BasicBlock *>(DstC);
        if (S->getTerminator()->getNumSuccessors() == 1)
        {
            // Non-critical: src has this edge as its only successor.
            PlacedOut = S;
            return S->getTerminator()->getIterator();
        }
        if (D->hasNPredecessors(1))
        {
            // Non-critical: dst has this edge as its only predecessor.
            PlacedOut = D;
            return D->getFirstInsertionPt();
        }
        // Critical edge: split it and place the counter in the new block. The
        // split block is genuine runtime cost and is charged to the MST arm --
        // avoiding the split would measure a fictional, cheaper PGO.
        BasicBlock *NB = SplitEdge(S, D);
        PlacedOut = NB;
        DidSplitOut = true;
        return NB->getFirstInsertionPt();
    }
} // anonymous namespace

namespace zray
{
    bool ZRayPass::instrumentMST(std::vector<llvm::BasicBlock *> *NonPostDomSet, size_t PragmaRegionID,
                                 size_t GroupNumber, Function &F, bool IsIndirect)
    {
        if (F.empty())
        {
            return false;
        }

        Module *M = F.getParent();

        // Edge weights come from branch probability / block frequency so that
        // hot edges are preferentially placed in the tree (left uninstrumented),
        // exactly as LLVM PGO does.
        BranchProbabilityInfo &BPI = getAnalysis<BranchProbabilityInfoWrapperPass>(F).getBPI();
        BlockFrequencyInfo &BFI = getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();

        // InstrumentFuncEntry=false matches LLVM PGO's default (-pgo-instrument-entry).
        llvm::CFGMST<ZEdge, ZBBInfo> MST(F, /*InstrumentFuncEntry=*/false, &BPI, &BFI);

        // (1) Build the block table BEFORE placing any counters, so the counter
        //     calls we insert are not miscounted as part of a block's static mix.
        //     Block ids are CFGMST BBInfo indices (shared with the edge table).
        //     The fake super-node (nullptr) is emitted with an all-zero mix.
        std::vector<std::pair<uint64_t, ProfileData>> BlockTable;
        BlockTable.reserve(MST.BBInfos.size());
        for (auto &BI : MST.BBInfos)
        {
            const llvm::BasicBlock *BB = BI.first;
            uint64_t BlockId = BI.second->Index;

            ProfileData Mix;
            clearRecords(&Mix);
            if (BB != nullptr)
            {
                llvm::BasicBlock *MBB = const_cast<llvm::BasicBlock *>(BB);
                for (llvm::BasicBlock::iterator I = MBB->begin(); I != MBB->end(); ++I)
                {
                    recordInst(I, 1, Mix, M);
                }
                // Keep the pass's static coverage diagnostics ("Loads/Stores/BB
                // Covered") consistent with the ZRAY arms — same code, same static
                // totals — so the invariant-metrics sanity check holds.
                TotalLoadCount += Mix.LoadCount;
                TotalStoreCount += Mix.StoreCount;
                BasicBlockCount += 1;
            }
            BlockTable.emplace_back(BlockId, Mix);
        }

        // (2) Place one counter per non-tree edge, reusing ZRAY's runtime counter
        //     array + indexing. No mix is attached to the counter: it measures
        //     only edge traversals. Record each edge's assigned counter index for
        //     the edge table. Placement may split critical edges (mutating the
        //     CFG); block ids above and edge endpoints below refer to the original
        //     blocks, which remain valid.
        std::unordered_map<const ZEdge *, size_t> EdgeCounter;
        size_t InstrumentedCount = 0;
        for (auto &E : MST.AllEdges)
        {
            if (E->Removed || E->InMST)
            {
                continue;
            }

            llvm::BasicBlock *Placed = nullptr;
            bool DidSplit = false;
            llvm::BasicBlock::iterator Pt = edgeInsertionPoint(E->SrcBB, E->DestBB, Placed, DidSplit);
            if (DidSplit)
            {
                StaticSplitBlocksAdded++;
            }

            size_t Index = IsIndirect ? IndirectFunctionCounterOffset : RegionCounterEventTotal;

            ProfileData profile;
            clearRecords(&profile);
            profile.PragmaRegionID = PragmaRegionID;
            profile.GroupNumber = GroupNumber;
            profile.IsIndirect = IsIndirect;
            profile.PostDomSetID = Index;

            insertCounterArrayInc(M, Pt, profile);

            // Advance the counter allocation exactly as insertCustomEvent does,
            // but without writing a ZRAY-format log record (the MST arm emits its
            // own sidecar instead).
            GlobalCounterEventTotal++;
            RegionCounterEventTotal++;
            if (IsIndirect)
            {
                IndirectFunctionCounterOffset++;
            }

            EdgeCounter[E.get()] = Index;
            InstrumentedCount++;
        }

        // (3) Build the edge table over the full (non-stale) edge set, tagging
        //     tree membership and the counter index for instrumented edges.
        struct EdgeRow
        {
            uint64_t Src;
            uint64_t Dst;
            uint8_t InMST;
            int64_t CounterIndex;
        };
        std::vector<EdgeRow> EdgeTable;
        for (auto &E : MST.AllEdges)
        {
            if (E->Removed)
            {
                continue;
            }
            uint64_t Src = MST.getBBInfo(E->SrcBB).Index;
            uint64_t Dst = MST.getBBInfo(E->DestBB).Index;
            int64_t CtrIdx = -1;
            if (!E->InMST)
            {
                auto It = EdgeCounter.find(E.get());
                CtrIdx = (It != EdgeCounter.end()) ? static_cast<int64_t>(It->second) : -1;
            }
            EdgeTable.push_back({Src, Dst, static_cast<uint8_t>(E->InMST ? 1 : 0), CtrIdx});
        }

        // (4) Serialize the per-function sidecar. See include/zray_mst.h.
        if (MstSidecarFile.is_open())
        {
            auto W = [&](const void *P, size_t N)
            { MstSidecarFile.write(reinterpret_cast<const char *>(P), N); };

            uint64_t Magic = ZRAY_MST_MAGIC;
            uint64_t RegionID = PragmaRegionID;
            uint8_t Indirect = IsIndirect ? 1 : 0;
            std::string Name = F.getName().str();
            uint64_t NameLen = Name.size();

            W(&Magic, sizeof(Magic));
            W(&RegionID, sizeof(RegionID));
            W(&Indirect, sizeof(Indirect));
            W(&NameLen, sizeof(NameLen));
            W(Name.data(), NameLen);

            uint64_t NumBlocks = BlockTable.size();
            W(&NumBlocks, sizeof(NumBlocks));
            for (auto &B : BlockTable)
            {
                W(&B.first, sizeof(uint64_t));
                W(&B.second, sizeof(ProfileData));
            }

            uint64_t NumEdges = EdgeTable.size();
            W(&NumEdges, sizeof(NumEdges));
            for (auto &Row : EdgeTable)
            {
                W(&Row.Src, sizeof(Row.Src));
                W(&Row.Dst, sizeof(Row.Dst));
                W(&Row.InMST, sizeof(Row.InMST));
                W(&Row.CounterIndex, sizeof(Row.CounterIndex));
            }
        }

        errs() << "MST[" << F.getName() << "]: " << MST.AllEdges.size()
               << " edges, " << InstrumentedCount << " instrumented (non-tree), "
               << BlockTable.size() << " blocks in sidecar.\n";

        return InstrumentedCount != 0;
    }
} // namespace zray
