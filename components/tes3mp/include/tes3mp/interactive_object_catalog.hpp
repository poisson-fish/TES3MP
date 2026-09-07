#ifndef TES3MP_INTERACTIVE_OBJECT_CATALOG_HPP
#define TES3MP_INTERACTIVE_OBJECT_CATALOG_HPP

#include "content_identity.hpp"
#include "spatial_types.hpp"
#include "value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumInteractiveObjectsPerCell = 512;
    inline constexpr std::size_t MaximumInteractiveObjectCatalogEntries = 16384;

    enum class InteractiveObjectKind : std::uint8_t
    {
        StandardDoor,
        TeleportDoor,
    };

    struct TeleportDestination
    {
        CellId cell;
        Transform transform;

        friend bool operator==(const TeleportDestination&, const TeleportDestination&) noexcept = default;
    };

    struct ObjectLockDeclaration
    {
        bool lockedByDefault = false;
        std::uint32_t lockLevel = 0;
        std::optional<KeyPrototypeId> keyId = std::nullopt;

        friend bool operator==(const ObjectLockDeclaration&, const ObjectLockDeclaration&) noexcept = default;
    };

    struct ObjectTrapDeclaration
    {
        bool trappedByDefault = false;
        std::optional<TrapPrototypeId> trapId = std::nullopt;

        friend bool operator==(const ObjectTrapDeclaration&, const ObjectTrapDeclaration&) noexcept = default;
    };

    struct InteractiveObjectCatalogEntry
    {
        InteractiveObjectId objectId;
        InteractiveObjectKind kind;
        CellId cell;
        Transform transform;
        std::optional<TeleportDestination> destination = std::nullopt;
        ObjectLockDeclaration lock{};
        ObjectTrapDeclaration trap{};

        friend bool operator==(const InteractiveObjectCatalogEntry&, const InteractiveObjectCatalogEntry&) noexcept
            = default;
    };

    class InteractiveObjectCatalog
    {
    public:
        static std::optional<InteractiveObjectCatalog> create(
            const ContentManifest& manifest, std::span<const InteractiveObjectCatalogEntry> entries) noexcept;

        constexpr ContentManifestId contentManifestId() const noexcept { return mContentManifestId; }
        std::span<const InteractiveObjectCatalogEntry> entries() const noexcept { return mEntries; }
        const InteractiveObjectCatalogEntry* find(InteractiveObjectId id) const noexcept;

        friend bool operator==(const InteractiveObjectCatalog&, const InteractiveObjectCatalog&) noexcept = default;

    private:
        InteractiveObjectCatalog(
            ContentManifestId contentManifestId, std::vector<InteractiveObjectCatalogEntry> entries) noexcept
            : mContentManifestId(contentManifestId)
            , mEntries(std::move(entries))
        {
        }

        ContentManifestId mContentManifestId;
        std::vector<InteractiveObjectCatalogEntry> mEntries;
    };
}

#endif
