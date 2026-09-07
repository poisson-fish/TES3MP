#ifndef TES3MP_ACTOR_CATALOG_HPP
#define TES3MP_ACTOR_CATALOG_HPP

#include "content_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumActorCatalogEntries = 4096;
    inline constexpr std::size_t MaximumActorWaypoints = 32;

    enum class ActorAiPackageKind : std::uint8_t
    {
        Idle,
        Travel,
        Wander,
    };

    enum class ActorActivity : std::uint8_t
    {
        Idle,
        Travel,
        Wander,
    };

    class ActorAiPackage
    {
    public:
        static std::optional<ActorAiPackage> create(
            ActorAiPackageKind kind, std::span<const Position3> waypoints) noexcept;

        constexpr ActorAiPackageKind kind() const noexcept { return mKind; }
        std::span<const Position3> waypoints() const noexcept { return mWaypoints; }

        friend bool operator==(const ActorAiPackage&, const ActorAiPackage&) noexcept = default;

    private:
        ActorAiPackage(ActorAiPackageKind kind, std::vector<Position3> waypoints) noexcept
            : mKind(kind), mWaypoints(std::move(waypoints)) {}

        ActorAiPackageKind mKind;
        std::vector<Position3> mWaypoints;
    };

    struct ActorCatalogEntry
    {
        ActorId actorId;
        EntityId entityId;
        ActorPrototypeId prototypeId;
        Transform initialRoot;
        ActorAiPackage aiPackage;

        friend bool operator==(const ActorCatalogEntry&, const ActorCatalogEntry&) noexcept = default;
    };

    class ActorCatalog
    {
    public:
        static std::optional<ActorCatalog> create(
            const ContentManifest& manifest, std::span<const ActorCatalogEntry> entries) noexcept;

        constexpr ContentManifestId contentManifestId() const noexcept { return mContentManifestId; }
        std::span<const ActorCatalogEntry> entries() const noexcept { return mEntries; }
        const ActorCatalogEntry* find(ActorId actorId) const noexcept;

        friend bool operator==(const ActorCatalog&, const ActorCatalog&) noexcept = default;

    private:
        ActorCatalog(ContentManifestId contentManifestId, std::vector<ActorCatalogEntry> entries) noexcept
            : mContentManifestId(contentManifestId), mEntries(std::move(entries)) {}

        ContentManifestId mContentManifestId;
        std::vector<ActorCatalogEntry> mEntries;
    };
}

#endif
