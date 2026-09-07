#ifndef TES3MP_CONTENT_IDENTITY_HPP
#define TES3MP_CONTENT_IDENTITY_HPP

#include "movement_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t ContentManifestIdBytes = 32;
    inline constexpr std::size_t MaximumContentCellSpaces = 256;
    inline constexpr std::size_t MaximumContentCells = 4096;

    enum class CellSpaceKind : std::uint8_t
    {
        Interior = 0,
        Exterior = 1,
    };

    struct CellSpaceDeclaration
    {
        CellSpaceId id;
        CellSpaceKind kind;

        friend constexpr bool operator==(CellSpaceDeclaration, CellSpaceDeclaration) noexcept = default;
        friend constexpr auto operator<=>(CellSpaceDeclaration, CellSpaceDeclaration) noexcept = default;
    };

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
        static std::optional<ContentManifest> create(ContentManifestId id,
            std::span<const CellSpaceDeclaration> cellSpaces, std::span<const CellId> cells,
            AppearanceId defaultAppearance, MovementProfile movementProfile) noexcept;

        constexpr ContentManifestId id() const noexcept { return mId; }
        constexpr AppearanceId defaultAppearance() const noexcept { return mDefaultAppearance; }
        constexpr MovementProfile movementProfile() const noexcept { return mMovementProfile; }
        std::span<const CellSpaceDeclaration> cellSpaces() const noexcept { return mCellSpaces; }
        std::span<const CellId> cells() const noexcept { return mCells; }
        bool contains(const CellId& cell) const noexcept;
        std::optional<CellSpaceKind> kind(CellSpaceId id) const noexcept;

        friend bool operator==(const ContentManifest&, const ContentManifest&) noexcept = default;

    private:
        ContentManifest(ContentManifestId id, std::vector<CellSpaceDeclaration> cellSpaces,
            std::vector<CellId> cells, AppearanceId defaultAppearance, MovementProfile movementProfile) noexcept
            : mId(id), mDefaultAppearance(defaultAppearance), mCellSpaces(std::move(cellSpaces)),
              mCells(std::move(cells)), mMovementProfile(movementProfile) {}

        ContentManifestId mId;
        AppearanceId mDefaultAppearance;
        std::vector<CellSpaceDeclaration> mCellSpaces;
        std::vector<CellId> mCells;
        MovementProfile mMovementProfile;
    };

    // Deterministic identity for engine-independent tests and proof fixtures only.
    ContentManifestId testContentManifestId() noexcept;
    ContentManifest testContentManifest() noexcept;
    std::optional<std::vector<CellSpaceDeclaration>> parseCellSpaceDeclarations(std::string_view value);
    std::optional<std::vector<CellId>> parseContentCells(std::string_view value);
    std::optional<MovementProfile> parseMovementProfile(std::string_view value) noexcept;
}

#endif
