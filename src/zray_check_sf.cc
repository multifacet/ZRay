// ZRay: portable compiler-assisted memory traffic characterization.
// Standalone analysis pass for checking loop scale factors.
//
// Authors: Hayden Coffey
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#include "zray_check_sf.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/SCCIterator.h"

static cl::opt<std::string> OutputFileName("zray-sf-output-file", cl::desc("File to write measured scale factors to"), cl::value_desc("filename"), cl::Required);
static cl::opt<bool> CompareVals("zray-sf-compare", cl::desc("Compare measured scale factors against the static estimates"), cl::value_desc("true/false"), cl::Hidden);

// Analysis passes we want to run beforehand
void SFCheckPass::getAnalysisUsage(AnalysisUsage &AU) const
{
	// Specify we need the loopinfo pass to run before this pass
	AU.addRequired<LoopInfoWrapperPass>();
	AU.addRequired<ScalarEvolutionWrapperPass>();
}

bool SFCheckPass::runOnModule(Module &M)
{
	bool modified = false;


	for (auto curFunc = M.getFunctionList().begin(),
			  endFunc = M.getFunctionList().end();
		 curFunc != endFunc; curFunc++)
	{
		if(curFunc->isDeclaration())
		{
			continue;	
		}

		modified = modified | runOnFunction(*curFunc);
	}


	if(CompareVals)
	{
		size_t InputSF;
		std::ifstream LogFile;
		LogFile.open(OutputFileName, std::ios::in);
		LogFile >> InputSF;
		LogFile.close();

		errs() << "Previous SF is: " << InputSF << "\n";
		errs() << "Current SF is : " << TotalTripCount << "\n";
		errs() << "Net Gain : " << TotalTripCount - InputSF << "\n";

		if(TotalTripCount - InputSF > 0)
		{
			errs() << "Generating true file\n";

			std::ofstream TrueFile;
			TrueFile.open(OutputFileName + "_true", std::ios::out);
			TrueFile.close();

		}
	}
	else
	{
		errs() << "Total trip: " << TotalTripCount << "\n";
		std::ofstream LogFile;
		LogFile.open(OutputFileName, std::ios::out | std::ios::app);
		LogFile << TotalTripCount << "\n";
		LogFile.close();
	}

	return modified;
}

bool SFCheckPass::runOnFunction(Function &F)
{

	LI = &(getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo());
	SE = &(getAnalysis<ScalarEvolutionWrapperPass>(F).getSE());

	size_t sf;
	for(Loop *L: *LI)
	{
		sf = 0;
		getBackedgeTakenCount(L, &sf);
		TotalTripCount+=sf;
		evalLoopSF(L);
	}

	return false;
}

void SFCheckPass::evalLoopSF(const Loop * L)
{
	size_t sf;
	for(Loop *ls : L->getSubLoops())
	{
		sf = 0;
		getBackedgeTakenCount(ls, &sf);
		TotalTripCount+=sf;
		evalLoopSF(ls);
	}
}

// Set @count to loop back edge taken count if it can be calculated
bool SFCheckPass::getBackedgeTakenCount(const Loop *L, size_t *count)
{
	const SCEV *v = SE->getBackedgeTakenCount(L, llvm::ScalarEvolution::ExitCountKind::Exact);

	errs() << "SCEV\n";
	errs() << *v << "\n";

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

char SFCheckPass::ID = 0;
static RegisterPass<SFCheckPass> X("zray-check-sf", "ZRay: verify loop scale factors against measured trip counts", false, false);

static RegisterStandardPasses Y(
	PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
	[](const PassManagerBuilder &Builder,
	   legacy::PassManagerBase &PM)
	{ PM.add(new SFCheckPass()); });