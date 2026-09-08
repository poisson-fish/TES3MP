#ifndef TES3MP_SERVER_COMBAT_CONTENT_HPP
#define TES3MP_SERVER_COMBAT_CONTENT_HPP

#include <tes3mp/actor_catalog.hpp>
#include <tes3mp/combat_world.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <variant>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumCombatContentBytes = 4 * 1024 * 1024;

    enum class CombatContentErrorCode
    {
        Unavailable,
        TooLarge,
        Malformed,
        ManifestMismatch,
        InvalidSettings,
        InvalidPlayerTemplate,
        InvalidActorSet,
        InvalidWeaponCatalog,
        InvalidWorld,
    };

    struct CombatContentError
    {
        CombatContentErrorCode code = CombatContentErrorCode::Unavailable;
        std::size_t line = 0;
    };

    struct CombatContent
    {
        OpenMwMeleeSettings settings;
        CanonicalPlayerCombatTemplate playerTemplate;
        MeleeWeaponCatalog weapons;
        CanonicalCombatWorld world;
    };

    using CombatContentLoadResult = std::variant<CombatContent, CombatContentError>;

    CombatContentLoadResult loadCombatContent(const std::filesystem::path& path,
        const ContentManifest& manifest, const ActorCatalog& actors,
        const ItemPrototypeCatalog& items) noexcept;
    std::string describeCombatContentError(CombatContentError error);
}

#endif
