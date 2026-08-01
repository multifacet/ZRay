// ZRay: portable compiler-assisted memory traffic characterization.
// Shared helpers used by the pass and the auxiliary passes.
//
// Authors: Hayden Coffey
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

/******************************
 * Hayden Coffey
 *
 * Misc. functions.
 */
#include "zray_util.h"

namespace zray
{
    // Allow us to add offsets to instruction iterator
    inst_iterator operator+(inst_iterator it, size_t index)
    {
        for (size_t i = 0; i < index; i++)
        {
            it++;
        }

        return it;
    }

    inst_iterator operator+(size_t index, inst_iterator it)
    {
        for (size_t i = 0; i < index; i++)
        {
            it++;
        }

        return it;
    }

    // Remove leading/trailing whitespace from string
    //  https://stackoverflow.com/questions/1798112/removing-leading-and-trailing-spaces-from-a-string
    std::string trim(const std::string &str,
                     const std::string &whitespace)
    {
        const auto strBegin = str.find_first_not_of(whitespace);
        if (strBegin == std::string::npos)
            return ""; // no content

        const auto strEnd = str.find_last_not_of(whitespace);
        const auto strRange = strEnd - strBegin + 1;

        return str.substr(strBegin, strRange);
    }

    std::string reduce(const std::string &str,
                       const std::string &fill,
                       const std::string &whitespace)
    {
        // trim first
        auto result = trim(str, whitespace);

        // replace sub ranges
        auto beginSpace = result.find_first_of(whitespace);
        while (beginSpace != std::string::npos)
        {
            const auto endSpace = result.find_first_not_of(whitespace, beginSpace);
            const auto range = endSpace - beginSpace;

            result.replace(beginSpace, range, fill);

            const auto newStart = beginSpace + fill.length();
            beginSpace = result.find_first_of(whitespace, newStart);
        }

        return result;
    }

    // Mangles given function to IR appropriate name, may be able to remove if we can find
    // what API call is responsible for generating function names.
    //
    // Examples:
    //_Z3addv   : the Z3 seems to indicate function name is 3 chars long, after the function name, the letters mark argument types
    //_Z3addvd  : add(void, double)
    //_Z4addvd  : addv(double) : 4 char name "addv" with a double as its arg
    //
    // Why this exists: LLVM ships a demangler but no supported API for the
    // reverse direction, so a plain C++ signature cannot be turned into a
    // mangled symbol through the library. The pass needs mangled names to
    // reference the C++ runtime entry points from generated IR, hence this.
    std::string mangleFunctionName(std::string func)
    {
        std::string mangled;
        std::string args;
        std::string prefix = "_Z";
        size_t size;
        size_t end;

        // Remove whitespace
        func.erase(remove_if(func.begin(), func.end(), isspace), func.end());
        mangled = func;

        size = mangled.find('(');
        end = mangled.find(')');

        ASSERT(size != std::string::npos, "mangleFunctionName: Missing '('");
        ASSERT(end != std::string::npos, "mangleFunctionName: Missing ')'");

        prefix = prefix + std::to_string(size);

        args = mangled.substr(size, end);

        // Build function with prefix
        mangled = prefix + mangled.substr(0, size);

        // Remove ( )
        args.erase(0, 1);
        args.pop_back();

        // Iterate over arguments
        std::stringstream ss(args);
        while (ss.good())
        {
            std::string substr;
            getline(ss, substr, ',');

            if(substr.empty())
            {
                mangled+= 'v';
                continue;
            }

            if (substr.find("*") != std::string::npos)
            {
                mangled += 'P';
            }

            if (substr.find("void") != std::string::npos)
            {
                mangled += 'v';
            }
            else if (substr.find("size_t") != std::string::npos)
            {
                mangled += 'm';
            }
            else
            {
                std::string errorMsg = "Unsupported function argument type : " + substr;
                ASSERT(false, errorMsg.c_str());
            }
        }

        //errs() << "Mangled: " << mangled << "\n";
        return mangled;
    }

}
