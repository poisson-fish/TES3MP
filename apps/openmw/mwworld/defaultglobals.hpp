#ifndef OPENMW_MWWORLD_DEFAULTGLOBALS_HPP
#define OPENMW_MWWORLD_DEFAULTGLOBALS_HPP
#include "globalvariablename.hpp"
#include <components/esm3/variant.hpp>
#include <vector>
#include <utility>
namespace MWWorld
{
    // Stock fallback globals for base Morrowind and total conversions.
    std::vector<std::pair<GlobalVariableName, ESM::Variant>> generateDefaultGlobals();
}
#endif
