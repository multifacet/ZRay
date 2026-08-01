// ZRay: portable compiler-assisted memory traffic characterization.
// Auxiliary pass preventing inlining of functions containing regions.
//
// Authors: Hayden Coffey
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

/******************************
 * Ashwin Poduval and Hayden Coffey
 *
 * Compiler pass used to prevent
 * instrumented functions
 * from being inlined
 */
#include "noinline_pass.h"
#include "zray_util.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/SCCIterator.h"
#include <algorithm>
#include <queue>

#include "llvm/Support/Threading.h"

#define PASS_BEGIN "ZRAY_ROI_BEGIN"
#define PASS_END "ZRAY_ROI_END"

bool ROINoInlinePass::isToolFlag(std::string inst, std::string pragmaName)
{
    using namespace std;
    inst = zray::reduce(inst);
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

bool ROINoInlinePass::detectPragma(Function &F)
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

bool ROINoInlinePass::runOnModule(Module &M)
{
    // Iterate over functions and check for pragmas
    bool modified = false;

    // Identify pragma regions
    for (auto curFunc = M.getFunctionList().begin(),
              endFunc = M.getFunctionList().end();
         curFunc != endFunc; curFunc++)
    {
        bool FoundPragma =  runOnFunction(*curFunc);
        // bool FoundPragma =  runOnFunction(*curFunc, FullScan);

        modified = modified | FoundPragma;
    }

    return modified;
}

// Pass to run on each function @F
bool ROINoInlinePass::runOnFunction(Function &F, bool InstAll, bool IsIndirect)
{
    // Do nothing if function does not contain an instrumentation pragma
    if (F.empty() || (!InstAll && detectPragma(F) == false))
    {
        return false;
    }

    F.addFnAttr(Attribute::NoInline);
    return true;
}

char ROINoInlinePass::ID = 0;
static RegisterPass<ROINoInlinePass> X("zray-noinline", "Set noinline attribute for all functions with regions of interest", false, false);

static RegisterStandardPasses Y(
    PassManagerBuilder::EP_EarlyAsPossible,
    [](const PassManagerBuilder &Builder,
       legacy::PassManagerBase &PM)
    { PM.add(new ROINoInlinePass()); });
