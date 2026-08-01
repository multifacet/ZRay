// ZRay: portable compiler-assisted memory traffic characterization.
// IR parsing: per-basic-block static instruction mix and access sizes.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

/******************************
 * Hayden Coffey
 *
 * Code used for parsing
 * LLVM IR.
 */

#include "zray_pass.h"
#include <stack>
#include <set>

namespace zray
{
#define PASS_BEGIN "ZRAY_ROI_BEGIN"
#define PASS_END "ZRAY_ROI_END"

    bool ZRayPass::detectPragma(Function &F)
    {
        // String to hold instruction we are parsing
        std::string tmp_str;
        raw_string_ostream ss(tmp_str);
        for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; I++)
        {
            if (!((*I).isDebugOrPseudoInst()) && ((*I).getOpcode() == AddrSpaceCastInst::Call))
            {
                ss << *I << "\n";

                if (isToolFlag(ss.str(), PASS_BEGIN))
                {
                    return true;
                }
            }
            tmp_str.clear();
        }

        return false;
    }

    bool ZRayPass::detectPragma(BasicBlock *bb)
    {
        // String to hold instruction we are parsing
        std::string tmp_str;
        raw_string_ostream ss(tmp_str);
        for (BasicBlock::iterator I = bb->begin(); I != bb->end(); ++I)
        {
            if (!((*I).isDebugOrPseudoInst()) && ((*I).getOpcode() == AddrSpaceCastInst::Call))
            {
                ss << *I << "\n";

                if (isToolFlag(ss.str(), PASS_BEGIN))
                {
                    return true;
                }
            }
            tmp_str.clear();
        }

        return false;
    }

    void ZRayPass::splitInstrumentedBlocks(BasicBlock *bb/*, std::vector<szt_bbvec> id_stack*/, Module *M, Function &F, size_t pragmaRegionID)
    {
        // Color this node in the CFG
        SplitBasicBlocksVisited.insert(bb);

        // Check for begin pragmas
        for (BasicBlock::iterator I = bb->begin(); I != bb->end(); I++)
        {
            // String to hold instruction we are parsing
            std::string tmp_str;
            raw_string_ostream ss(tmp_str);
            // size_t groupID;
            // errs() << "Opcode: " << (*I).getOpcode() << "\n";
            if(I == bb->end()) {
                errs() << "End of basic block\n";
            }

            if (!((*I).isDebugOrPseudoInst()) && ((*I).getOpcode() == AddrSpaceCastInst::Call))
            {
                ss << *I << "\n";
                // errs() << "Condition 1\n";

                // If begin pragma found, push onto groupID stack
                if ((isToolFlag(ss.str(), PASS_BEGIN)) && (bb->begin() != I))
                {
                    //errs() << "A - Pass Begin: At " << bb->getName() << "\n";
                    BasicBlock::iterator duplicate_I = I;
                    llvm::DominatorTree splitDomTree{F};
                    //auto domTreeUpdater = llvm::DomTreeUpdater(llvm::DomTreeUpdater::UpdateStrategy(0));
                    llvm::LoopInfo splitLoopInfo{splitDomTree};
                    // llvm::AliasAnalysis &splitAA = getAnalysis<AAResultsWrapperPass>(F).getAAResults();
                    // llvm::MemorySSA splitMSSA{F, &splitAA, &splitDomTree};
                    // llvm::MemorySSAUpdater splitMSSAU{&splitMSSA};
                    // llvm::SplitBlock(bb, &(*duplicate_I), &splitDomTree, &splitLoopInfo, &splitMSSAU);
                    //llvm::SplitBlock(bb, &(*duplicate_I), &domTreeUpdater, &splitLoopInfo);
                    llvm::SplitBlock(bb, &(*duplicate_I), &splitDomTree, &splitLoopInfo);
                    //errs() << "B - Pass Begin: At " << bb->getName() << "\n";
                    break;
                }
                // errs() << "After begin test\n";
                if ((isToolFlag(ss.str(), PASS_END)) && (bb->end() != I))
                {
                    // errs() << "Pass End: At " << bb->getName() << "\n";
                    insertEndTimerEvent(M, I, pragmaRegionID);
                    BasicBlock::iterator duplicate_I = I;
                    llvm::DominatorTree splitDomTree{F};
                    llvm::LoopInfo splitLoopInfo{splitDomTree};
                    // llvm::AliasAnalysis &splitAA = getAnalysis<AAResultsWrapperPass>(F).getAAResults();
                    // llvm::MemorySSA splitMSSA{F, &splitAA, &splitDomTree};
                    // llvm::MemorySSAUpdater splitMSSAU{&splitMSSA};
                    // llvm::SplitBlock(bb, &(*++duplicate_I), &splitDomTree, &splitLoopInfo, &splitMSSAU);
                    llvm::SplitBlock(bb, &(*++duplicate_I), &splitDomTree, &splitLoopInfo);
                    //errs() << "Pass End: At " << bb->getName() << "\n";
                    break;
                }
                ss.flush();
            }
            //errs() << "End of for loop\n";
        }

        // Recursive call on successors not visited yet
        for (auto bb_child : successors(bb))
        {
            if (SplitBasicBlocksVisited.find(bb_child) == SplitBasicBlocksVisited.end())
            {
                splitInstrumentedBlocks(bb_child/*, id_stack*/, M, F, pragmaRegionID);
            }
        }
    }

    void ZRayPass::recordPragmaRegions(BasicBlock *bb, std::vector<szt_bbvec> id_stack, DominatorTree *dtree)
    {
        // Color this node in the CFG
        BasicBlocksVisited.insert(bb);

        // Check for begin pragmas
        for (BasicBlock::iterator I = bb->begin(); I != bb->end(); I++)
        {
            // String to hold instruction we are parsing
            std::string tmp_str;
            raw_string_ostream ss(tmp_str);
            size_t groupID;

            if (!((*I).isDebugOrPseudoInst()) && ((*I).getOpcode() == AddrSpaceCastInst::Call))
            {
                ss << *I << "\n";

                // If begin pragma found, push onto groupID stack
                if (isToolFlag(ss.str(), PASS_BEGIN))
                {
                    groupID = getGroupID(ss.str());
                    auto it = std::find_if(BasicBlockGroupList.begin(), BasicBlockGroupList.end(),
                                           [&groupID](const szt_bbvec &e)
                                           { return e.first == groupID; });

                    // First time encountering this group id, allocate lists for it
                    if (it == BasicBlockGroupList.end())
                    {
                            //Can we find the previous pragma region by looking at the n-1 index for
                            //this region?
                            errs() << "==============================\n";
                            errs() << ss.str() << "\n";
                            errs() << "Couldn't find ID " << groupID << "\n";
                            if(id_stack.size() == 0)
                            {
                                errs() << "Outer ID is null, this is outermost PR.\n";
                            }
                            else
                            {
                                errs() << "Outer ID is " << id_stack.back().first << "\n";
                            }
                            errs() << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
                        BasicBlockGroupList.push_back(szt_bbvec(groupID, std::vector<BasicBlock *>()));
                        id_stack.push_back(szt_bbvec(groupID, std::vector<BasicBlock *>()));

                        id_stack.back().second.push_back(bb);
                    }
                    // Have seen this group id before, add to proper stack. Potential nested region
                    else
                    {
                        auto it = std::find_if(id_stack.begin(), id_stack.end(),
                                               [&groupID](const szt_bbvec &e)
                                               { return e.first == groupID; });

                        if (it == id_stack.end())
                        {
                            id_stack.push_back(szt_bbvec(groupID, std::vector<BasicBlock *>()));
                            id_stack.back().second.push_back(bb);
                        }
                        else
                        {
                            it->second.push_back(bb);
                        }
                    }
                }
            }
        }

        // Add BB to all open group stacks
        for (auto stack_pair : id_stack)
        {
            if (!stack_pair.second.empty())
            {
                auto it = std::find_if(BasicBlockGroupList.begin(), BasicBlockGroupList.end(),
                                       [&stack_pair](const szt_bbvec &e)
                                       { return e.first == stack_pair.first; });
                it->second.push_back(bb);
            }
        }

        // Check for end pragmas
        for (BasicBlock::iterator I = bb->begin(); I != bb->end(); I++)
        {
            // String to hold instruction we are parsing
            std::string tmp_str;
            raw_string_ostream ss(tmp_str);
            size_t groupID;
            if (!((*I).isDebugOrPseudoInst()) && ((*I).getOpcode() == AddrSpaceCastInst::Call))
            {
                ss << *I << "\n";

                // If found,
                if (isToolFlag(ss.str(), PASS_END))
                {
                    groupID = getGroupID(ss.str());
                    auto it = std::find_if(id_stack.begin(), id_stack.end(),
                                           [&groupID](const szt_bbvec &e)
                                           { return e.first == groupID; });

                    // Verify with domination
                    if (!dtree->dominates(it->second.back(), bb))
                    {
                        errs() << "zray: warning: region end not dominated by region begin (group id " << groupID << ")\n";
                        errs() << "\tCurrent BLOCK : " << *bb << "\n";
                        errs() << "\tComparing with : " << *it->second.back() << "\n";
                    }

                    // pop from groupID stack
                    it->second.pop_back();
                }
            }
        }

        // Recursive call on successors not visited yet
        for (auto bb_child : successors(bb))
        {
            if (BasicBlocksVisited.find(bb_child) == BasicBlocksVisited.end())
            {
                recordPragmaRegions(bb_child, id_stack, dtree);
            }
        }
    }

    // Return true if instruction is custom pragma
    bool ZRayPass::isToolFlag(std::string inst, std::string pragmaName)
    {
        using namespace std;
        inst = reduce(inst);
        string split = " ";
        string token = inst.substr(0, inst.find(split));

        size_t state = 0;

        size_t pos = 0;
        while ((pos = inst.find(split)) != std::string::npos)
        {
            token = inst.substr(0, pos);
            switch (state)
            {
            case 0:
                if (token == "asm")
                    state++;
                break;
            case 1:
                if (token == "sideeffect")
                    state++;
                else
                    return false;
                break;
            case 2:
                if (token.find(pragmaName) != string::npos)
                    return true;
                else
                    return false;
                break;
            }

            inst.erase(0, pos + split.length());
        }
        return false;
    }

    bool ZRayPass::isIfElse(std::string name)
    {
        return name.find("if.then") != std::string::npos || name.find("if.else") != std::string::npos;
    }

    // Return true if instruction is custom pragma
    size_t ZRayPass::getGroupID(std::string inst)
    {
        using namespace std;
        inst = reduce(inst);
        string split = " ";
        string token = inst.substr(0, inst.find(split));

        string pragmaName = PASS_BEGIN;

        size_t state = 0;

        size_t pos = 0;
        while ((pos = inst.find(split)) != std::string::npos)
        {
            token = inst.substr(0, pos);
            switch (state)
            {
            case 0:
                if (token == "asm")
                    state++;
                break;
            case 1:
                if (token == "sideeffect")
                    state++;
                else
                    return 0;
                break;
            case 2:
                // Extract group ID from string
                inst.erase(0, pos + split.length());
                pos = inst.find('"');
                token = inst.substr(0, pos);
                return atoi(token.c_str());
            }

            inst.erase(0, pos + split.length());
        }
        return 0;
    }

    // Get end of basic block instruction offset from begining of function
    // Subtract size if you want the offset to the begining of the BB
    size_t ZRayPass::getBBOffset(std::vector<llvm::BasicBlock*> OrderedCFG, llvm::BasicBlock *I)
    {
        size_t offset = 0;

        auto it = std::find(OrderedCFG.begin(), OrderedCFG.end(), I);
        while (it != OrderedCFG.begin())
        {
            // errs() << *it << "\n";
            offset += (*it)->size();
            --it;
        }
        offset += (*it)->size();
        return offset;
    }

    // Get offset to beginning of previous basic block - useful for split blocks resulting
    // from ROI in loops
    size_t ZRayPass::getPreviousBBOffset(std::vector<llvm::BasicBlock*> OrderedCFG, llvm::BasicBlock *I)
    {
        size_t offset = 0;
        int disregard = 0;

        auto it = std::find(OrderedCFG.begin(), OrderedCFG.end(), I);
        while (it != OrderedCFG.begin())
        {
            if((disregard == 0) || (disregard == 1)) {
                --it;
                ++disregard;
                continue;
            }
            // errs() << *it << "\n";
            offset += (*it)->size();
            --it;
        }
        offset += (*it)->size();
        return offset;
    }

    size_t getGlobalOpCount(llvm::Instruction *I)
    {
        size_t count = 0;

        // https://stackoverflow.com/questions/25761390/how-to-find-which-global-variables-are-used-in-a-function-by-using-llvm-api
        for (const Value *Op : I->operands())
            if (const GlobalValue *G = dyn_cast<GlobalValue>(Op))
            {
                count++;
            }

        return count;
    }

    size_t getPointerOpCount(llvm::Instruction *I)
    {
        size_t count = 0;

        for (const Value *Op : I->operands())
            if (const PointerType *G = dyn_cast<PointerType>(Op->getType()))
            {
                count++;
            }

        return count;
    }

    std::pair<int, int> getLargeConstantOpCount(llvm::Instruction *I)
    {
        int count = 0, bytes = 0;
        // errs() << "Instruction: ";
        // I->print(errs());
        // errs() << "\n";

        for (const Value *Op : I->operands()) {
            // errs() << "Operand: ";
            // Op->print(errs());
            // errs() << "\n";
            // errs() << "Operand Type: ";
            // Op->getType()->print(errs());
            // errs() << "\n";
            if (const ConstantFP *CFP = dyn_cast<ConstantFP>(Op)) {
                // errs() << "FP Const: ";
                // Op->print(errs());
                // errs() << "\n";
                llvm::APFloat APF = CFP->getValueAPF();
                if (APF.bitcastToAPInt().getBitWidth() == 64) {
                    std::string fp_op;
                    llvm::raw_string_ostream stream(fp_op);
                    Op->print(stream);

                    // Most aggressive in flagging FP constants as loads, can lead to overcounting
                    // if((fp_op.substr(8,11) != ".000000e+00") && (fp_op.substr(9,11) != ".000000e+00")) {

                    // Most conservative check for flagging FP constants as loads, can lead to undercounting
                    // if(fp_op.substr(7,2) == "0x") {

                    // Compromise between the two filters mentioned above, seems to find the right balance for the workloads tested so far
                    if((fp_op.substr(8,8) != ".000000e") && (fp_op.substr(9,8) != ".000000e")) {
                        count++;
                        bytes += 8;
                    }
                }
            } else if (const ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(Op)) {
                // errs() << "Data Vector Const: ";
                // Op->print(errs());
                // errs() << "\n";
                unsigned numElements = CDV->getNumElements();
                if (numElements == 0) {
                    continue;
                }
                Constant *firstElement = CDV->getElementAsConstant(0);
                if (const ConstantInt *CI = dyn_cast<ConstantInt>(firstElement)) {
                    if ((CI->getSExtValue() <= 127) && (CI->getSExtValue() >= -128)) {
                        // errs() << "Value: " << CI->getSExtValue() << "\n";
                        continue;
                    }
                }
                int elementSizeBytes = CDV->getElementByteSize();
                // errs() << "Element size: " << elementSizeBytes << "\n";
                /* May need to account for optimizations/ISA extensions that allow for broadcasting values from memory
                 * This for loop attempts to do that, detecting vectors for which all elements have the same value. Future work could
                 * involve identifying the types of instructions and exact semantics and optimizations followed for major ISAs */
                for (unsigned i = 1; i < numElements; ++i) {
                    Constant *currentElement = cast<Constant>(CDV->getElementAsConstant(i));
                    if (currentElement != firstElement) {
                        count += numElements;
                        bytes += numElements * elementSizeBytes;
                        continue;
                    }
                }
                ++count;
                bytes += elementSizeBytes;
            }/* else if (const ConstantVector *CV = dyn_cast<ConstantVector>(Op)) {
                // TODO: Not encountered so far. Need to add code to handle this when it does show up.
                errs() << "Vector Const: ";
                Op->print(errs());
                errs() << "\n";
            } else if (const ConstantInt *CI = dyn_cast<ConstantInt>(Op)) {
                // errs() << "Integer Const: ";
                // I->print(errs());
                // errs() << "\n";
                // Add a check for large immediates
                // This is not perfect, only filtering out values which are larger than 32 bits in size
                // Ideally, we would take ISA and operation into account here
                // Needs more testing, commenting out for now
                if ((CI->getSExtValue() >= 2147483648) || (CI->getSExtValue() < -2147483648)) {
                    count++;
                }
            }*/
	}

        return std::make_pair(count, bytes);
    }

    // Record IR values that correspond to stack addresses
    void ZRayPass::recordStackAddresses(std::vector<llvm::BasicBlock *> BlockList)
    {
        for (auto block : BlockList)
        {
            for (BasicBlock::iterator I = block->begin(); I != block->end(); I++)
            {
                if (PHINode *PN = dyn_cast<PHINode>(&*I)) {
                    Value *Incoming = PN->getIncomingValue(0);
                    if (!Incoming->getType()->isPointerTy()) {
                        continue;
                    }
                    if (StackAddressList.find(dyn_cast<Value>(&(*I))) != StackAddressList.end())
                    {
                        // errs() << "Phi:";
                        // I->print(errs());
                        // errs() << "\n";
                        for (Value *Op : I->users())
                        {
                            StackAddressList.insert(std::pair<llvm::Value *, size_t>(Op, 0));
                        }
                    }
                }
                switch (I->getOpcode())
                {
                case AddrSpaceCastInst::Alloca: // Runtime stack memory allocation
                    StackAddressList.insert(std::pair<llvm::Value *, size_t>(dyn_cast<Value>(&(*I)), 0));
                    // errs() << "Alloca:";
                    // I->print(errs());
                    // errs() << "\n";
                    for (Value *Op : I->users())
                    {
                        // errs() << "Alloca users:";
                        // Op->print(errs());
                        // errs() << "\n";
                        StackAddressList.insert(std::pair<llvm::Value *, size_t>(Op, 0));
                    }
                    break;
                case AddrSpaceCastInst::GetElementPtr:
                    if (StackAddressList.find(dyn_cast<Value>(&(*I))) != StackAddressList.end())
                    {
                        // errs() << "GEP:";
                        // I->print(errs());
                        // errs() << "\n";
                        for (Value *Op : I->users())
                        {
                            StackAddressList.insert(std::pair<llvm::Value *, size_t>(Op, 0));
                        }
                    }
                    break;
                }
            }
        }
    }

    // Determine instruction type and record relevant info
    void ZRayPass::recordInst(llvm::BasicBlock::iterator I, size_t scaleFactor, ProfileData &profile, Module *M)
    {
        using namespace llvm;

        int count = getPointerOpCount(&(*I));
        auto large_const_info = getLargeConstantOpCount(&(*I));

        profile.TotalInstCount += scaleFactor;

        std::string zray_check;
        llvm::raw_string_ostream ss(zray_check);
        // ss << (*I);
        // if((zray_check.substr(2,4) == "call") || (zray_check.substr(2,9) == "tail call"))
        // {
        //     profile.StackReadCount += scaleFactor;
        //     profile.StackWriteCount += scaleFactor;
        //     return;
        // }

        // TODO: Assuming for now that an instruction only reads/writes to one address
        bool callNotDetected = true;
        if (I->mayReadFromMemory())
        {
            // profile.loadCount += scaleFactor*count;
            // errs() << "Read: ";
            // I->print(errs());
            // errs() << "\n";
            callNotDetected = false;
            profile.LoadCount += scaleFactor;
        }
        if (I->mayWriteToMemory())
        {
            // errs() << "Write: ";
            // I->print(errs());
            // errs() << "\n";
            // profile.storeCount += scaleFactor*count;
            callNotDetected = false;
            profile.StoreCount += scaleFactor;
        }

        // Extract number of bytes read and/or written.
        //
        // The access size is taken from the type of the value actually loaded or
        // stored, not from the pointer operand's pointee type. Under LLVM's opaque
        // pointers (the default from LLVM 15 onward) a pointer carries no pointee
        // type at all, and asking for one asserts:
        //   "Attempting to get element type of opaque pointer"
        // For typed-pointer IR the two are identical, so this is equivalent there
        // and correct in both modes.
        const DataLayout &dataLayout = M->getDataLayout();
        uint64_t accessSize;
        if (I->getOpcode() == AddrSpaceCastInst::Load)
        {
            LoadInst *accessInst = dyn_cast<LoadInst>(I);
            accessSize = dataLayout.getTypeStoreSize(accessInst->getType());
            profile.BytesRead += accessSize * scaleFactor;
        }
        if (I->getOpcode() == AddrSpaceCastInst::Store)
        {
            StoreInst *accessInst = dyn_cast<StoreInst>(I);
            accessSize = dataLayout.getTypeStoreSize(accessInst->getValueOperand()->getType());
            profile.BytesWritten += accessSize * scaleFactor;
        }
        if (I->getOpcode() == AddrSpaceCastInst::AtomicCmpXchg)
        {
            AtomicCmpXchgInst *accessInst = dyn_cast<AtomicCmpXchgInst>(I);
            accessSize = dataLayout.getTypeStoreSize(accessInst->getNewValOperand()->getType());
            profile.BytesRead += accessSize * scaleFactor;
            profile.BytesWritten += accessSize * scaleFactor;
        }
        if (I->getOpcode() == AddrSpaceCastInst::AtomicRMW)
        {
            AtomicRMWInst *accessInst = dyn_cast<AtomicRMWInst>(I);
            accessSize = dataLayout.getTypeStoreSize(accessInst->getValOperand()->getType());
            profile.BytesRead += accessSize * scaleFactor;
            profile.BytesWritten += accessSize * scaleFactor;
        }

        switch (I->getOpcode())
        {
        // Terminator Instructions==========
        case AddrSpaceCastInst::Ret:
        case AddrSpaceCastInst::Br:
        case AddrSpaceCastInst::Switch:
        case AddrSpaceCastInst::IndirectBr:
        case AddrSpaceCastInst::Invoke:
        case AddrSpaceCastInst::Resume:
        case AddrSpaceCastInst::Unreachable:
        case AddrSpaceCastInst::CleanupRet:
        case AddrSpaceCastInst::CatchRet:
        case AddrSpaceCastInst::CatchSwitch:
        case AddrSpaceCastInst::CallBr:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            profile.TermInstructionCount += scaleFactor;
            break;
            //==================================

            // Standard Unary Operators=========
        case AddrSpaceCastInst::FNeg:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            break;
            //==================================

            // Standard Binary Operators========
        case AddrSpaceCastInst::Add:
        case AddrSpaceCastInst::Sub:
        case AddrSpaceCastInst::Mul:
        case AddrSpaceCastInst::UDiv:
        case AddrSpaceCastInst::SDiv:
        case AddrSpaceCastInst::URem:
        case AddrSpaceCastInst::SRem: // Signed division remainder
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            if (large_const_info.first)
            {
                profile.MemInstructionCount += scaleFactor * large_const_info.first;
                profile.GlobalOpReadCount += scaleFactor * large_const_info.first;
                profile.LoadCount += scaleFactor * large_const_info.first;
                profile.BytesRead += scaleFactor * large_const_info.second;
            }
            profile.IntInstructionCount += scaleFactor;
            break;

        case AddrSpaceCastInst::FAdd:
        case AddrSpaceCastInst::FSub:
        case AddrSpaceCastInst::FMul:
        case AddrSpaceCastInst::FDiv:
        case AddrSpaceCastInst::FRem:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            if (large_const_info.first)
            {
                profile.MemInstructionCount += scaleFactor * large_const_info.first;
                profile.GlobalOpReadCount += scaleFactor * large_const_info.first;
                profile.LoadCount += scaleFactor * large_const_info.first;
                profile.BytesRead += scaleFactor * large_const_info.second;
            }
            profile.FpInstructionCount += scaleFactor;
            break;
            //==================================

            // Logical Operators (Int)==========
        case AddrSpaceCastInst::Shl:
        case AddrSpaceCastInst::LShr:
        case AddrSpaceCastInst::AShr:
        case AddrSpaceCastInst::And:
        case AddrSpaceCastInst::Or:
        case AddrSpaceCastInst::Xor:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            if (large_const_info.first)
            {
                profile.MemInstructionCount += scaleFactor * large_const_info.first;
                profile.GlobalOpReadCount += scaleFactor * large_const_info.first;
                profile.LoadCount += scaleFactor * large_const_info.first;
                profile.BytesRead += scaleFactor * large_const_info.second;
            }
            profile.IntInstructionCount += scaleFactor;
            break;
            //==================================

            // Memory Operators=================
        case AddrSpaceCastInst::Load:
            profile.MemInstructionCount += scaleFactor;

            if (getGlobalOpCount(&(*I)))
            {
                profile.GlobalOpReadCount += scaleFactor;
            }
            else if (StackAddressList.find(dyn_cast<Value>(&(*I))) != StackAddressList.end())
            {
                profile.StackReadCount += scaleFactor;
                profile.BytesRead -= accessSize * scaleFactor;
                // profile.LoadCount -= scaleFactor;
            }
            else
            {
                profile.HeapReadCount += scaleFactor;
            }

            break;
        case AddrSpaceCastInst::Store:
            profile.MemInstructionCount += scaleFactor;

            if (getGlobalOpCount(&(*I)))
            {
                profile.GlobalOpWriteCount += scaleFactor;
            }
            else if (StackAddressList.find(dyn_cast<Value>(&(*I))) != StackAddressList.end())
            {
                // errs() << "Stack Write: ";
                // I->print(errs());
                // errs() << "\n";
                profile.StackWriteCount += scaleFactor;
                profile.BytesWritten -= accessSize * scaleFactor;
                // profile.StoreCount -= scaleFactor;
            }
            else
            {
                profile.HeapWriteCount += scaleFactor;
            }
            break;

            // https://stackoverflow.com/questions/39436498/llvm-how-to-get-return-value-of-an-instruction
        case AddrSpaceCastInst::Alloca: // Runtime stack memory allocation
        case AddrSpaceCastInst::GetElementPtr:
        case AddrSpaceCastInst::Fence:
        case AddrSpaceCastInst::AtomicCmpXchg:
        case AddrSpaceCastInst::AtomicRMW:
            profile.MemInstructionCount += scaleFactor;
            break;
            //==================================

            // Cast operators===================
        case AddrSpaceCastInst::Trunc:
        case AddrSpaceCastInst::ZExt:
        case AddrSpaceCastInst::SExt: // Sign extend integer
        case AddrSpaceCastInst::FPToUI:
        case AddrSpaceCastInst::FPToSI:
        case AddrSpaceCastInst::UIToFP:
        case AddrSpaceCastInst::SIToFP:
        case AddrSpaceCastInst::FPTrunc:
        case AddrSpaceCastInst::FPExt:
        case AddrSpaceCastInst::PtrToInt:
        case AddrSpaceCastInst::IntToPtr:
        case AddrSpaceCastInst::BitCast:
        case AddrSpaceCastInst::AddrSpaceCast:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            profile.CastInstructionCount += scaleFactor;
            break;
            //==================================

            // Other============================
        case AddrSpaceCastInst::ICmp:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            profile.IntInstructionCount += scaleFactor;
            break;
        case AddrSpaceCastInst::FCmp:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            profile.FpInstructionCount += scaleFactor;
            break;

        //Found a call instruction, check if it is an intrinsic operation we model
        case AddrSpaceCastInst::Call:
            ss << (*I);
            if ((zray_check.find("TimingEvent") != std::string::npos) || (zray_check.find("ZRAY_ROI") != std::string::npos) || (zray_check.find("incrementCounterArray") != std::string::npos) || (zray_check.find("incrementLoad") != std::string::npos) || (zray_check.find("incrementStore") != std::string::npos)) {
                profile.LoadCount -= scaleFactor;
                profile.StoreCount -= scaleFactor;
                profile.TotalInstCount -= scaleFactor;
                return;
            }
            if (callNotDetected) {
                profile.LoadCount += scaleFactor;
                profile.StoreCount += scaleFactor;
            }
            profile.StackReadCount += scaleFactor;
            profile.StackWriteCount += scaleFactor;
            // errs() << "Stack Write: ";
            // I->print(errs());
            // errs() << "\n";
            // profile.LoadCount -= scaleFactor;
            // profile.StoreCount -= scaleFactor;
            if (isa<CallInst>(I))
            {
                // Cast to call instruction and retrieve function
                llvm::CallInst *callInst = llvm::cast<llvm::CallInst>(I);
                Function *f = callInst->getCalledFunction();

                // If it is an intrinsic, model its behavior and apply to profile
                if (f != nullptr && f->isIntrinsic())
                {
                    auto intrinsicID = f->getIntrinsicID();
                    switch (intrinsicID)
                    {
                    case Intrinsic::memset:
                    {
                        /*if (callInst->getNumOperands() < 3) {
                            errs() << "Less than 3 operands! memset operand count: " << callInst->getNumOperands() << "\n";
                            return;
                        }*/
                        Value *size = callInst->getOperand(2);
                        // errs() << "Target Triple: " << M->getTargetTriple() << "\n";
                        auto parent_fn_attr = I->getFunction()->getAttributes();
                        StringRef targetCPU = parent_fn_attr.getFnAttr("target-cpu").getValueAsString();
                        StringRef targetFeatures = parent_fn_attr.getFnAttr("target-features").getValueAsString();
                        // errs() << "Target CPU: " << targetCPU << "\n";
                        // errs() << "Target Features: " << targetFeatures << "\n";
                        // if (targetCPU.find("x86") != llvm::StringLiteral::npos) {
                            if (auto *constSize = dyn_cast<ConstantInt>(size))
                            {
                                uint64_t sizeValue = constSize->getValue().getZExtValue();
                                // This is only needed to account for MIR optimizations
                                // As of now, we disable the MIR pass, so this code has been disabled as well
                                /* if (targetFeatures.find("avx") != llvm::StringLiteral::npos)
                                {
                                    if((sizeValue > 0) && (sizeValue < 128))
                                    {
                                        return;
                                    }
                                }
                                if((sizeValue > 0) && (sizeValue < 64))
                                {
                                    return;
                                } */
                                profile.IntrinsicStore += scaleFactor * sizeValue;
                                return;
                            }
                        // }
                        BasicBlock::iterator runtime_ip(I);
                        ++runtime_ip;
                        insertStoreArrayInc(M, runtime_ip, profile, size);
                        // insertStoreRuntimeIntrinsicEvent(M, runtime_ip, profile, size);
                        break;
                    }
                    case Intrinsic::memcpy:
                    case Intrinsic::memmove:
                    {
                        /*if (callInst->getNumOperands() < 3) {
                            errs() << "Less than 3 operands! memcpy/memmove operand count: " << callInst->getNumOperands() << "\n";
                            return;
                        }*/
                        Value *size = callInst->getOperand(2);
                        // errs() << "Target Triple: " << M->getTargetTriple() << "\n";
                        auto parent_fn_attr = I->getFunction()->getAttributes();
                        StringRef targetCPU = parent_fn_attr.getFnAttr("target-cpu").getValueAsString();
                        StringRef targetFeatures = parent_fn_attr.getFnAttr("target-features").getValueAsString();
                        // errs() << "Target CPU: " << targetCPU << "\n";
                        // errs() << "Target Features: " << targetFeatures << "\n";
                        // if (targetCPU.find("x86") != llvm::StringLiteral::npos) {
                            if (auto *constSize = dyn_cast<ConstantInt>(size))
                            {
                                uint64_t sizeValue = constSize->getValue().getZExtValue();
                                // This is only needed to account for MIR optimizations
                                // As of now, we disable the MIR pass, so this code has been disabled as well
                                /* if (targetFeatures.find("avx") != llvm::StringLiteral::npos)
                                {
                                    if((sizeValue > 0) && (sizeValue < 257))
                                    {
                                        return;
                                    }
                                }
                                if((sizeValue > 0) && (sizeValue < 129))
                                {
                                    return;
                                } */
                                profile.IntrinsicLoad += scaleFactor * sizeValue;
                                profile.IntrinsicStore += scaleFactor * sizeValue;
                                return;
                            }
                        // }
                        BasicBlock::iterator runtime_ip(I);
                        ++runtime_ip;
                        insertLoadStoreArrayInc(M, runtime_ip, profile, size);
                        // insertStoreRuntimeIntrinsicEvent(M, runtime_ip, profile, size);
                        // insertLoadRuntimeIntrinsicEvent(M, runtime_ip, profile, size);
                        // BasicBlock::iterator runtime_ip1(I);
                        // ++runtime_ip1;
                        // insertLoadRuntimeIntrinsicEvent(M, runtime_ip1, profile, size);
                        // if (auto *constSize = dyn_cast<ConstantInt>(size))
                        // {
                        //     uint64_t sizeValue = constSize->getValue().getZExtValue();

                            // Assuming that size is in bytes, and we are performing 8 byte load/store operations
                        //     profile.IntrinsicLoad += scaleFactor * (sizeValue / 8);
                        //     profile.IntrinsicStore += scaleFactor * (sizeValue / 8);
                        // }
                        break;
                    }
                    }
                }
            }
        case AddrSpaceCastInst::PHI:
        case AddrSpaceCastInst::Select:
        case AddrSpaceCastInst::UserOp1:
        case AddrSpaceCastInst::UserOp2:
        case AddrSpaceCastInst::VAArg:
        case AddrSpaceCastInst::ShuffleVector:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            profile.OtherInstCount += scaleFactor;
            break;

        case AddrSpaceCastInst::ExtractElement:
        case AddrSpaceCastInst::ExtractValue:
            profile.MemInstructionCount += scaleFactor;
            break;

        case AddrSpaceCastInst::InsertValue:
        case AddrSpaceCastInst::InsertElement:
            profile.MemInstructionCount += scaleFactor;
            break;

        case AddrSpaceCastInst::LandingPad:
        case AddrSpaceCastInst::CleanupPad:
        case AddrSpaceCastInst::CatchPad:
        case AddrSpaceCastInst::Freeze:
            if (count)
            {
                profile.MemInstructionCount += scaleFactor * count;
            }
            profile.OtherInstCount += scaleFactor;
            break;

        default:
            std::string opName = I->getOpcodeName(I->getOpcode());
            std::string errorMsg = "Missing inst type: " + opName;

            ASSERT(false, errorMsg.c_str());
            break;
        }
    }

    void ZRayPass::clearRecords(ProfileData *profile)
    {
        memset(profile, 0, sizeof(ProfileData));
    }
} // namespace zray
