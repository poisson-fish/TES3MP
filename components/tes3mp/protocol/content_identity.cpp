#include <tes3mp/content_identity.hpp>

#include <algorithm>

namespace TES3MP
{
    std::optional<ContentManifest> ContentManifest::create(ContentManifestId id, CellSpaceId interiorCell,
        CellSpaceId exteriorWorldspace, AppearanceId defaultAppearance) noexcept
    {
        if (interiorCell.value() == exteriorWorldspace.value()
            || interiorCell.value() == defaultAppearance.value()
            || exteriorWorldspace.value() == defaultAppearance.value())
            return std::nullopt;
        return ContentManifest(id, interiorCell, exteriorWorldspace, defaultAppearance);
    }

    std::optional<ContentManifestId> ContentManifestId::fromBytes(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() != ContentManifestIdBytes
            || std::all_of(bytes.begin(), bytes.end(), [](std::byte value) { return value == std::byte{}; }))
            return std::nullopt;
        std::array<std::byte, ContentManifestIdBytes> result{};
        std::copy(bytes.begin(), bytes.end(), result.begin());
        return ContentManifestId(result);
    }

    std::optional<ContentManifestId> ContentManifestId::fromHex(std::string_view hex) noexcept
    {
        if (hex.size() != ContentManifestIdBytes * 2)
            return std::nullopt;
        const auto nibble = [](char value) -> std::optional<std::uint8_t> {
            if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
            if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
            if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
            return std::nullopt;
        };
        std::array<std::byte, ContentManifestIdBytes> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const auto high = nibble(hex[index * 2]);
            const auto low = nibble(hex[index * 2 + 1]);
            if (!high || !low)
                return std::nullopt;
            result[index] = static_cast<std::byte>((*high << 4u) | *low);
        }
        return fromBytes(result);
    }

    ContentManifestId testContentManifestId() noexcept
    {
        std::array<std::byte, ContentManifestIdBytes> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::byte>(index + 1);
        return *ContentManifestId::fromBytes(bytes);
    }

    ContentManifest testContentManifest() noexcept
    {
        return *ContentManifest::create(testContentManifestId(), *CellSpaceId::fromValue(7),
            *CellSpaceId::fromValue(8), *AppearanceId::fromValue(1));
    }
}
