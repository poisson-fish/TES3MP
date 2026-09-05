#ifndef TES3MP_CONTENT_IDENTITY_HPP
#define TES3MP_CONTENT_IDENTITY_HPP

#include "spatial_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace TES3MP
{
    inline constexpr std::size_t ContentManifestIdBytes = 32;

    class ContentManifestId
    {
    public:
        static std::optional<ContentManifestId> fromBytes(std::span<const std::byte> bytes) noexcept;
        static std::optional<ContentManifestId> fromHex(std::string_view hex) noexcept;

        constexpr std::span<const std::byte, ContentManifestIdBytes> bytes() const noexcept { return mBytes; }
        friend constexpr bool operator==(ContentManifestId, ContentManifestId) noexcept = default;
        friend constexpr auto operator<=>(ContentManifestId, ContentManifestId) noexcept = default;

    private:
        explicit constexpr ContentManifestId(
            std::array<std::byte, ContentManifestIdBytes> bytes) noexcept : mBytes(bytes) {}

        std::array<std::byte, ContentManifestIdBytes> mBytes{};
    };

    class ContentManifest
    {
    public:
        static std::optional<ContentManifest> create(ContentManifestId id, CellSpaceId interiorCell,
            CellSpaceId exteriorWorldspace, AppearanceId defaultAppearance) noexcept;

        constexpr ContentManifestId id() const noexcept { return mId; }
        constexpr CellSpaceId interiorCell() const noexcept { return mInteriorCell; }
        constexpr CellSpaceId exteriorWorldspace() const noexcept { return mExteriorWorldspace; }
        constexpr AppearanceId defaultAppearance() const noexcept { return mDefaultAppearance; }

        constexpr bool contains(const CellId& cell) const noexcept
        {
            if (const auto* interior = cell.asInterior())
                return interior->cellSpace() == mInteriorCell;
            const auto* exterior = cell.asExterior();
            return exterior && exterior->worldspace() == mExteriorWorldspace
                && exterior->gridX() == 0 && exterior->gridY() == 0;
        }

        friend constexpr bool operator==(ContentManifest, ContentManifest) noexcept = default;

    private:
        constexpr ContentManifest(ContentManifestId id, CellSpaceId interiorCell,
            CellSpaceId exteriorWorldspace, AppearanceId defaultAppearance) noexcept
            : mId(id), mInteriorCell(interiorCell), mExteriorWorldspace(exteriorWorldspace),
              mDefaultAppearance(defaultAppearance) {}

        ContentManifestId mId;
        CellSpaceId mInteriorCell;
        CellSpaceId mExteriorWorldspace;
        AppearanceId mDefaultAppearance;
    };

    // Deterministic identity for engine-independent tests and proof fixtures only.
    ContentManifestId testContentManifestId() noexcept;
    ContentManifest testContentManifest() noexcept;
}

#endif
