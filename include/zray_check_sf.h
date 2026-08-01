// ZRay: portable compiler-assisted memory traffic characterization.
// Interfaces for the scale-factor checking pass.
//
// Authors: Hayden Coffey
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#ifndef ZRAY_CHECK_SF_H
#define ZRAY_CHECK_SF_H

#include <llvm/Pass.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/ScalarEvolution.h>

#include <llvm/Support/CommandLine.h>

#include <fstream>

#include "zray_util.h"

using namespace llvm;

struct SFCheckPass : public ModulePass
{
	static char ID;

	size_t TotalTripCount = 0;

	ScalarEvolution *SE;
	LoopInfo *LI;

	virtual void getAnalysisUsage(AnalysisUsage &AU) const override;
	bool runOnModule(Module &M) override;
	bool runOnFunction(Function &F);

	bool getBackedgeTakenCount(const Loop *L, size_t *count);

	void evalLoopSF(const Loop * L);

	SFCheckPass() : ModulePass(ID) {}
};

#endif