#ifndef TES3MP_NATIVE_CHARACTER_CONSTRUCTION_HPP
#define TES3MP_NATIVE_CHARACTER_CONSTRUCTION_HPP

#include <tes3mp/character_profile.hpp>

namespace MWWorld { class ESMStore; }
namespace TES3MP::Native
{
    // The winning, manifest-bound store must remain immutable and outlive the
    // returned catalog. Selection validation stays in the profile state machine;
    // native construction additionally checks the actual winning records.
    CharacterContentCatalog withNativeCharacterConstruction(
        const CharacterContentCatalog& selections, const MWWorld::ESMStore& store);
}

#endif
