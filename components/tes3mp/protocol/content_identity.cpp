#include <tes3mp/content_identity.hpp>

#include <algorithm>
#include <charconv>

namespace TES3MP
{
    namespace
    {
        template <class T>
        std::optional<T> decimal(std::string_view value)
        {
            T result{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                return std::nullopt;
            return result;
        }

        std::optional<CellSpaceKind> parseKind(std::string_view value)
        {
            if (value == "interior") return CellSpaceKind::Interior;
            if (value == "exterior") return CellSpaceKind::Exterior;
            return std::nullopt;
        }
    }
    std::optional<ContentManifest> ContentManifest::create(ContentManifestId id,
        std::span<const CellSpaceDeclaration> cellSpaces, std::span<const CellId> cells,
        AppearanceId defaultAppearance) noexcept
    {
        if (cellSpaces.empty() || cellSpaces.size() > MaximumContentCellSpaces
            || cells.empty() || cells.size() > MaximumContentCells)
            return std::nullopt;
        try
        {
            std::vector<CellSpaceDeclaration> sortedSpaces(cellSpaces.begin(), cellSpaces.end());
            std::ranges::sort(sortedSpaces, {}, &CellSpaceDeclaration::id);
            for (std::size_t index = 0; index < sortedSpaces.size(); ++index)
            {
                if (sortedSpaces[index].id.value() == defaultAppearance.value()
                    || (index != 0 && sortedSpaces[index - 1].id == sortedSpaces[index].id))
                    return std::nullopt;
            }

            std::vector<CellId> sortedCells(cells.begin(), cells.end());
            std::ranges::sort(sortedCells);
            for (std::size_t index = 0; index < sortedCells.size(); ++index)
            {
                if (index != 0 && sortedCells[index - 1] == sortedCells[index])
                    return std::nullopt;
                const auto idValue = sortedCells[index].asInterior()
                    ? sortedCells[index].asInterior()->cellSpace()
                    : sortedCells[index].asExterior()->worldspace();
                const auto declaration = std::ranges::lower_bound(sortedSpaces, idValue, {}, &CellSpaceDeclaration::id);
                const auto expectedKind = sortedCells[index].asInterior()
                    ? CellSpaceKind::Interior : CellSpaceKind::Exterior;
                if (declaration == sortedSpaces.end() || declaration->id != idValue
                    || declaration->kind != expectedKind)
                    return std::nullopt;
            }
            for (const auto& declaration : sortedSpaces)
            {
                if (std::ranges::none_of(sortedCells, [&](const CellId& cell) {
                        if (const auto* interior = cell.asInterior())
                            return declaration.kind == CellSpaceKind::Interior
                                && interior->cellSpace() == declaration.id;
                        return declaration.kind == CellSpaceKind::Exterior
                            && cell.asExterior()->worldspace() == declaration.id;
                    }))
                    return std::nullopt;
            }
            return ContentManifest(id, std::move(sortedSpaces), std::move(sortedCells), defaultAppearance);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ContentManifest::contains(const CellId& cell) const noexcept
    {
        return std::ranges::binary_search(mCells, cell);
    }

    std::optional<CellSpaceKind> ContentManifest::kind(CellSpaceId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mCellSpaces, id, {}, &CellSpaceDeclaration::id);
        return found != mCellSpaces.end() && found->id == id ? std::optional(found->kind) : std::nullopt;
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
        const auto interior = *CellSpaceId::fromValue(7);
        const auto exterior = *CellSpaceId::fromValue(8);
        const std::array spaces{ CellSpaceDeclaration{ interior, CellSpaceKind::Interior },
            CellSpaceDeclaration{ exterior, CellSpaceKind::Exterior } };
        const std::array cells{ CellId::interior(interior), CellId::exterior(exterior, 0, 0) };
        return *ContentManifest::create(testContentManifestId(), spaces, cells, *AppearanceId::fromValue(1));
    }

    std::optional<std::vector<CellSpaceDeclaration>> parseCellSpaceDeclarations(std::string_view value)
    try
    {
        if (value.empty() || value.back() == ';') return std::nullopt;
        std::vector<CellSpaceDeclaration> result;
        for (std::size_t begin = 0; begin < value.size();)
        {
            const auto end = value.find(';', begin);
            const auto entry = value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
            const auto colon = entry.find(':');
            if (colon == std::string_view::npos || entry.find(':', colon + 1) != std::string_view::npos)
                return std::nullopt;
            const auto kind = parseKind(entry.substr(0, colon));
            const auto rawId = decimal<std::uint64_t>(entry.substr(colon + 1));
            const auto id = rawId ? CellSpaceId::fromValue(*rawId) : std::nullopt;
            if (!kind || !id) return std::nullopt;
            result.push_back({ *id, *kind });
            if (result.size() > MaximumContentCellSpaces) return std::nullopt;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        return result.empty() ? std::nullopt : std::optional(std::move(result));
    }
    catch (...) { return std::nullopt; }

    std::optional<std::vector<CellId>> parseContentCells(std::string_view value)
    try
    {
        if (value.empty() || value.back() == ';') return std::nullopt;
        std::vector<CellId> result;
        for (std::size_t begin = 0; begin < value.size();)
        {
            const auto end = value.find(';', begin);
            const auto entry = value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
            const auto first = entry.find(':');
            const auto second = first == std::string_view::npos ? first : entry.find(':', first + 1);
            const auto kind = first == std::string_view::npos ? std::nullopt : parseKind(entry.substr(0, first));
            const auto idEnd = second == std::string_view::npos ? entry.size() : second;
            const auto rawId = first == std::string_view::npos ? std::nullopt
                : decimal<std::uint64_t>(entry.substr(first + 1, idEnd - first - 1));
            const auto id = rawId ? CellSpaceId::fromValue(*rawId) : std::nullopt;
            if (!kind || !id) return std::nullopt;
            if (*kind == CellSpaceKind::Interior)
            {
                if (second != std::string_view::npos) return std::nullopt;
                result.push_back(CellId::interior(*id));
            }
            else
            {
                if (second == std::string_view::npos) return std::nullopt;
                const auto third = entry.find(':', second + 1);
                if (third == std::string_view::npos || entry.find(':', third + 1) != std::string_view::npos)
                    return std::nullopt;
                const auto x = decimal<std::int32_t>(entry.substr(second + 1, third - second - 1));
                const auto y = decimal<std::int32_t>(entry.substr(third + 1));
                if (!x || !y) return std::nullopt;
                result.push_back(CellId::exterior(*id, *x, *y));
            }
            if (result.size() > MaximumContentCells) return std::nullopt;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        return result.empty() ? std::nullopt : std::optional(std::move(result));
    }
    catch (...) { return std::nullopt; }
}
