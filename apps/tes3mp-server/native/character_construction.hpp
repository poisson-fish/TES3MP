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

    // Discover selectable winning records, without a manual race/class catalog.
    // Bounds or ambiguous IDs reject the complete catalog; never publish a subset.
    // Spawns and starting items remain explicit campaign policy.
    std::optional<CharacterContentCatalog> discoverNativeCharacterContent(ContentManifestId manifest,
        Transform creationSpawn, Transform completionSpawn, std::span<const StartingItem> startingInventory,
        const MWWorld::ESMStore& store) noexcept;
}

#endif
