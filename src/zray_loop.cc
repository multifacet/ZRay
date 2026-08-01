// ZRay: portable compiler-assisted memory traffic characterization.
// Loop analysis: trip counts, scale factors, and counter hoisting.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

/******************************
 * Hayden Coffey
 *
 * Code pertaining to LLVM IR
 * loop handling.
 */
#include "zray_pass.h"

namespace zray
{

    void ZRayPass::removeLoopBB(std::vector<llvm::BasicBlock *> &npd, Function &F)
    {
        errs() << "Going into remove, size is " << npd.size() << "\n";

        for (Loop *loop : *LI)
        {
            for (llvm::BasicBlock *bb : loop->getBlocks())
            {
                auto search = std::find(npd.begin(), npd.end(), bb);
                if (search != npd.end())
                {
                    npd.erase(search);
                }
            }
            errs() << "=========\n";
        }

        errs() << "Leaving remove, size is " << npd.size() << "\n";
    }

    // Creates a tree data structure to model loops and subloops
    void ZRayPass::_createLoopTree(std::vector<llvm::BasicBlock*> OrderedCFG, LoopTreeNode * parent, Loop * loop, size_t b, size_t e)
    {
        LoopTreeNode * next_parent = parent;
        LoopTreeNode *child;

        //This is a loop we want to instrument
        if (INBOUNDS(b, e, getBBOffset(OrderedCFG, loop->getHeader()) - loop->getHeader()->size()))
        {
            child = new LoopTreeNode(parent, new LoopData(loop));
            getBackedgeTakenCount(child->NodeData->loop, &(child->NodeData->ScaleFactor));
            parent->ChildList.push_back(child);

            CollectSingleLoopStatistics(loop, ROILS);

            next_parent = child;
        }
        else
        {
            errs() << "Ignoring the loop : " << *loop << "\n";
            errs() << "Offset : " << getBBOffset(OrderedCFG, loop->getHeader()) - loop->getHeader()->size() << "\n";
            errs() << "B / E : " << b << " / " << e << "\n";
        }

        for (Loop * _loop : loop->getSubLoopsVector())
        {
        //    Loop_Tree_Node *child = new Loop_Tree_Node(root, new LoopData(loop));
         //   getBackedgeTakenCount(child->data->loop, &(child->data->sf));
            _createLoopTree(OrderedCFG, next_parent, _loop, b, e);
            //root->children.push_back(child);
        }
    }

    ZRayPass::LoopTreeNode *ZRayPass::createLoopTree(std::vector<llvm::BasicBlock*> OrderedCFG, size_t b, size_t e)
    {
        // Create root node, all depth 0 loops are children to this
        LoopTreeNode *root = new LoopTreeNode(nullptr, new LoopData(nullptr));
        for (Loop *loop : *LI)
        {
            _createLoopTree(OrderedCFG, root, loop, b, e);
        }

        return root;
    }

    // If we can add current node to parent's set, do so.
    // Otherwise, (i.e. parent SF could not be calculated) create new set
    void ZRayPass::colorLoopTreeSF(LoopTreeNode *root)
    {
        if (root->ParentNode != nullptr)
        {
            if (root->NodeData->ScaleFactor == 0)
            {
                root->NodeData->SetColor = 0;
            }
            else if (root->ParentNode->NodeData->SetColor != 0)
            {
                root->NodeData->SetColor = root->ParentNode->NodeData->SetColor;
            }
            else
            {
                root->NodeData->SetColor = LOOP_SF_SET;
                LOOP_SF_SET++;
            }
        }

        for (auto c : root->ChildList)
        {
            colorLoopTreeSF(c);
        }
    }

    // Return first node in tree that has the given set ID
    ZRayPass::LoopTreeNode *ZRayPass::findLoopTreeSet(LoopTreeNode *root, size_t id)
    {
        if (root->NodeData->SetColor == id)
        {
            return root;
        }

        LoopTreeNode *tmp;
        for (auto c : root->ChildList)
        {
            tmp = findLoopTreeSet(c, id);
            if (tmp != nullptr)
            {
                return tmp;
            }
        }

        return nullptr;
    }

    // Get list of BB present in given loop but not any of the subloops
    void ZRayPass::getUniqueLoopBB(llvm::Loop *L, std::vector<llvm::BasicBlock *> &bb_list)
    {
        for (auto bb : L->getBlocks())
        {
            bool in_subloop = false;
            for (auto subloop : L->getSubLoops())
            {
                if (subloop->contains(bb))
                {
                    in_subloop = true;
                }
            }

            if (!in_subloop)
            {
                bb_list.push_back(bb);
            }
        }
    }

    void ZRayPass::recolorSFLoopSet(LoopTreeNode *root, size_t old_id)
    {
        if (root->NodeData->SetColor != old_id)
        {
            return;
        }

        root->NodeData->SetColor = LOOP_SF_SET;

        for (auto c : root->ChildList)
        {
            recolorSFLoopSet(c, old_id);
        }
    }

    // std::vector<std::pair<std::vector<llvm::BasicBlock *>, size_t>> header_pd_blocks;
    bool ZRayPass::instrumentSFLoopSet(LoopTreeNode *root, bb_sf_pair_vec &header_pd_blocks,
                                        PostDominatorTree *pdtree, size_t outer_sf, size_t set_id, 
                                        size_t pragmaRegion, size_t groupID,
                                        std::vector<llvm::BasicBlock *> &pragmaBlocks, Function &F, bool IsIndirect)
    {
        if (root->NodeData->SetColor != set_id)
        {
            return false;
        }

        // Determine SF value for this loop
        // If loop header PD parent loop header, we apply outer SF to inner SF
        // Otherwise, we need to split this set
        size_t sf = root->NodeData->ScaleFactor;
        llvm::BasicBlock *loop_header = root->NodeData->loop->getHeader();
        if (root->ParentNode->NodeData->loop != nullptr && root->ParentNode->NodeData->SetColor == root->NodeData->SetColor)
        {
            llvm::BasicBlock *parent_header = root->ParentNode->NodeData->loop->getHeader();
            if (pdtree->dominates(loop_header, parent_header))
            {
                sf *= outer_sf;
            }
            else
            {
                // Recoloring this subtree will create a new set to process
                recolorSFLoopSet(root, root->NodeData->SetColor);
                // errs() << "splitting edge\n";
                // root->data->print();
                LOOP_SF_SET++;
                return false;
            }
        }

        bool inserted_event = false;

        std::vector<llvm::BasicBlock *> unique_bb;
        getUniqueLoopBB(root->NodeData->loop, unique_bb);

        // Blocks that do not PD loop header
        std::vector<llvm::BasicBlock *> non_pd_blocks;

        // Allocate list for this loop
        header_pd_blocks.push_back(std::pair<std::vector<llvm::BasicBlock *>, size_t>(std::vector<llvm::BasicBlock *>(), sf));

        // Sort loop BB into either PD set or non PD set
        for (auto b : unique_bb)
        {
            auto search = std::find(pragmaBlocks.begin(), pragmaBlocks.end(), b);
            if (search == pragmaBlocks.end())
            {
                continue;
            }

            if (pdtree->dominates(b, loop_header))
            {
                header_pd_blocks.back().first.push_back(b);
                pragmaBlocks.erase(search);
            }
            else
            {
                non_pd_blocks.push_back(b);
                // Updated function to immediately remove basic blocks
                // from original list after adding to queues for analysis
                pragmaBlocks.erase(search);
            }
        }

        // Instrument non PD blocks
        inserted_event |= instrumentPostDomSet(&non_pd_blocks, pdtree, pragmaRegion, groupID, F, IsIndirect);

        // Process sub-loops
        for (auto c : root->ChildList)
        {
            inserted_event |= instrumentSFLoopSet(c, header_pd_blocks, pdtree, sf, set_id, pragmaRegion, groupID, pragmaBlocks, F, IsIndirect);
        }

        return inserted_event;
    }

    bool ZRayPass::_instrumentDynamicLoops(LoopTreeNode *root, PostDominatorTree *pdtree, size_t pragmaRegion, size_t groupID,
                                           std::vector<llvm::BasicBlock *> &pragmaBlocks, Function &F, bool IsIndirect)
    {
        bool inserted_event = false;
        // If this is a loop we could not instrument before, process it
        if (root->NodeData->SetColor == 0)
        {
            std::vector<llvm::BasicBlock *> unique_bb;
            std::vector<llvm::BasicBlock *> blocks;
            std::vector<llvm::BasicBlock *> pd_blocks;
            std::vector<llvm::BasicBlock *> npd_blocks;

            //Reseting the loop since splitting edges requires us
            //to rerun all of the analysis passes and I can't
            //find a clean way to rerun ScalarEvolution and LoopInfo
            //without invalidating all of the existing loop structures.
            root->NodeData->loop = LI->getLoopFor(root->NodeData->Header);


            llvm::BasicBlock *loop_header = root->NodeData->Header;

            getUniqueLoopBB(root->NodeData->loop, unique_bb);

            // Is this a loop we can determine trip count for at runtime?
            const SCEV *v = SE->getBackedgeTakenCount(root->NodeData->loop, llvm::ScalarEvolution::ExitCountKind::Exact);

            if (v->getSCEVType() != SCEVTypes::scCouldNotCompute && SE->isLoopInvariant(v, root->NodeData->loop))
            //if(false)
            {
                //Trip count is a loop invariant, we can continue

                // TODO: Implement hoist chaining

                // Sort the blocks that make up this loop into two sets,
                // one set that is post-dom by loop header, and the other
                // which isn't.
                for (auto b : unique_bb)
                {
                    auto search = std::find(pragmaBlocks.begin(), pragmaBlocks.end(), b);
                    if (search == pragmaBlocks.end())
                    {
                        continue;
                    }

                    if (pdtree->dominates(b, loop_header))
                    {
                        pd_blocks.push_back(b);
                        // Updated function to immediately remove basic blocks
                        // from original list after adding to queues for analysis
                        pragmaBlocks.erase(search);
                    }
                    else
                    {
                        npd_blocks.push_back(b);
                        // Updated function to immediately remove basic blocks
                        // from original list after adding to queues for analysis
                        pragmaBlocks.erase(search);
                    }
                }
            }
            else
            {
                //We can't calculate trip count at run time, need to instrument manually.
                for (auto b : unique_bb)
                {
                    auto search = std::find(pragmaBlocks.begin(), pragmaBlocks.end(), b);
                    if (search == pragmaBlocks.end())
                    {
                        continue;
                    }

                    npd_blocks.push_back(b);
                    // Updated function to immediately remove basic blocks
                    // from original list after adding to queues for analysis
                    pragmaBlocks.erase(search);
                }
            }

            DynBasicBlocksHoisted += pd_blocks.size();
            // Instrument blocks which are post-dom by header

            bool hoisted = insertDynamicLoopCounter(&pd_blocks, pragmaRegion, groupID, F, root->NodeData->loop, IsIndirect);
            if(!hoisted)
            {
                //Either no blocks were in the set to be hoisted or we hit an
                //edge case, when we split the edge to hoist the counter, Scalar Evolution failed to recompute the loop SF

                if(!pd_blocks.empty())
                {
                    pd_blocks.clear();
                    npd_blocks.clear();

                    //We can't calculate trip count at run time, need to instrument manually.
                    for (auto b : unique_bb)
                    {
                        auto search = std::find(pragmaBlocks.begin(), pragmaBlocks.end(), b);
                        if (search == pragmaBlocks.end())
                        {
                            continue;
                        }

                        npd_blocks.push_back(b);
                        // Updated function to immediately remove basic blocks
                        // from original list after adding to queues for analysis
                        pragmaBlocks.erase(search);
                    }
                }
            }

            inserted_event |= hoisted;

            // Instrument non PD blocks, confusing function name, I know.
            inserted_event |= instrumentPostDomSet(&npd_blocks, pdtree, pragmaRegion, groupID, F, IsIndirect);
        }

        // Recursively process loop tree
        for (auto c : root->ChildList)
        {
            inserted_event |= _instrumentDynamicLoops(c, pdtree, pragmaRegion, groupID, pragmaBlocks, F, IsIndirect);
        }

        return inserted_event;
    }

    bool ZRayPass::instrumentDynamicLoops(LoopTreeNode *root, PostDominatorTree *pdtree, size_t pragmaRegion,
                                          size_t groupID, std::vector<llvm::BasicBlock *> &pragmaBlocks, Function &F, bool IsIndirect)
    {
        bool inserted_event = false;

        // Recursively process loop tree
        for (auto c : root->ChildList)
        {
            inserted_event |= _instrumentDynamicLoops(c, pdtree, pragmaRegion, groupID, pragmaBlocks, F, IsIndirect);
        }

        return inserted_event;
    }

    // Set @count to loop back edge taken count if it can be calculated.
    //
    // Backedge-taken count, not trip count, is the right basis for ZRay's scale
    // factors: it is what the hoisted counter must be multiplied by. Note that
    // LoopInfo models natural loops only — it is not complete cycle detection,
    // so irreducible control flow is not covered here and falls back to
    // per-block counting.
    bool ZRayPass::getBackedgeTakenCount(const Loop *L, size_t *count)
    {
        if (!SE->hasLoopInvariantBackedgeTakenCount(L))
        {
            return false;
        }

        const SCEV *v = SE->getBackedgeTakenCount(L, llvm::ScalarEvolution::ExitCountKind::Exact);

        if (v->getSCEVType() == SCEVTypes::scCouldNotCompute)
        {
            *count = 0;
            return false;
        }

        if (v->getSCEVType() == SCEVTypes::scConstant)
        {
            std::string tmp_str;
            raw_string_ostream ss(tmp_str);
            v->print(ss);
            *count = strtoumax(ss.str().c_str(), nullptr, 10);
            ASSERT(*count != UINTMAX_MAX, "Loop Backedge count overflow!");

            return true;
        }

        return false;
    }

    void ZRayPass::CollectLoopStatistics(Function &F, LoopStats &LS)
    {
        if (F.empty())
        {
            return;
        }

        LI = &(getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo());
        SE = &(getAnalysis<ScalarEvolutionWrapperPass>(F).getSE());

        for (Loop *loop : *LI)
        {
            _CollectLoopStatistics(loop, LS);
        }
    }

    void ZRayPass::CollectSingleLoopStatistics(Loop *L, LoopStats &LS)
    {
        LS.TotalLoops++;

        if (SE->hasLoopInvariantBackedgeTakenCount(L))
        {
            LS.TotalLoopInvarBackEdge++;
        }

        if(L->isCanonical(*SE))
        {
            LS.TotalLoopCanonical++;
        }

        const SCEV *v = SE->getBackedgeTakenCount(L, llvm::ScalarEvolution::ExitCountKind::Exact);
        if (v->getSCEVType() == SCEVTypes::scConstant)
        {
            LS.TotalLoopStaticBounds++;
        }
        else if(v->getSCEVType() != SCEVTypes::scCouldNotCompute)
        {
            LS.TotalLoopDynBounds++;
        }
    }

    void ZRayPass::_CollectLoopStatistics(Loop *L, LoopStats &LS)
    {

        CollectSingleLoopStatistics(L, LS);

        for (Loop *_loop : L->getSubLoopsVector())
        {
            _CollectLoopStatistics(_loop, LS);
        }
    }

} // namespace zray
