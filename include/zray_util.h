// ZRay: portable compiler-assisted memory traffic characterization.
// Shared helper declarations.
//
// Authors: Hayden Coffey
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#ifndef __INSTR_UTIL_H
#define __INSTR_UTIL_H

#include <llvm/IR/InstIterator.h>
#include <sstream>
#include <llvm/Pass.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/ScalarEvolution.h>

namespace zray
{
    using namespace llvm;
// Assert with error messages
//  https://stackoverflow.com/questions/3767869/adding-message-to-assert
#ifndef NDEBUG
#define ASSERT(condition, message)                                        \
    do                                                                    \
    {                                                                     \
        if (!(condition))                                                 \
        {                                                                 \
            errs() << "Assertion `" #condition "` failed in " << __FILE__ \
                   << " line " << __LINE__ << ": " << message << "\n";    \
            std::terminate();                                             \
        }                                                                 \
    } while (false)
#else
#define ASSERT(condition, message) \
    do                             \
    {                              \
    } while (false)
#endif

#define INBOUNDS(lower, upper, val) (lower <= val && val <= upper)

    inst_iterator operator+(inst_iterator it, size_t index);
    inst_iterator operator+(size_t index, inst_iterator it);

    std::string trim(const std::string &str,
                     const std::string &whitespace = " \t");

    std::string reduce(const std::string &str,
                       const std::string &fill = " ",
                       const std::string &whitespace = " \t");

    std::string mangleFunctionName(std::string func);

    bool getBackedgeTakenCount(const Loop *L, Function &F, size_t *count);
} // namespace zray
#endif
