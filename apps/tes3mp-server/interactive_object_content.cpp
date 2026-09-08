#include "interactive_object_content.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_INTERACTIVE_OBJECTS_V1";
        constexpr std::size_t MaximumFields = 32;

        template <class Value>
        std::optional<Value> number(std::string_view text) noexcept
        {
            Value value{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        std::optional<std::int64_t> coordinate(std::string_view text) noexcept
        {
            const auto parsed = number<std::int64_t>(text);
            return parsed && *parsed >= -MaximumInteractiveObjectCoordinate
                    && *parsed <= MaximumInteractiveObjectCoordinate
                ? parsed
                : std::nullopt;
        }

        std::optional<std::size_t> tokenize(
            std::string_view line, std::array<std::string_view, MaximumFields>& output) noexcept
        {
            std::size_t count = 0;
            for (std::size_t begin = 0; begin < line.size();)
            {
                while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t'))
                    ++begin;
                if (begin == line.size())
                    break;
                if (count == output.size())
                    return std::nullopt;
                auto end = begin;
                while (end < line.size() && line[end] != ' ' && line[end] != '\t')
                    ++end;
                output[count++] = line.substr(begin, end - begin);
                begin = end;
            }
            return count;
        }

        std::optional<CellId> parseCell(std::span<const std::string_view> fields, std::size_t& consumed) noexcept
        {
            if (fields.size() < 2)
                return std::nullopt;
            const auto rawSpace = number<std::uint64_t>(fields[1]);
            const auto space = rawSpace ? CellSpaceId::fromValue(*rawSpace) : std::nullopt;
            if (!space)
                return std::nullopt;
            if (fields[0] == "interior")
            {
                consumed = 2;
                return CellId::interior(*space);
            }
            if (fields[0] != "exterior" || fields.size() < 4)
                return std::nullopt;
            const auto x = number<std::int32_t>(fields[2]);
            const auto y = number<std::int32_t>(fields[3]);
            if (!x || !y)
                return std::nullopt;
            consumed = 4;
            return CellId::exterior(*space, *x, *y);
        }

        template <class Value>
        std::optional<std::optional<Value>> optionalId(std::string_view text) noexcept
        {
            if (text == "none")
                return std::optional<Value>{};
            const auto raw = number<std::uint64_t>(text);
            const auto value = raw ? Value::fromValue(*raw) : std::nullopt;
            return value ? std::optional<std::optional<Value>>(*value) : std::nullopt;
        }

        std::optional<Transform> parseTransform(const CellId& cell, std::span<const std::string_view> fields) noexcept
        {
            if (fields.size() != 6)
                return std::nullopt;
            std::array<std::int64_t, 3> position{};
            std::array<std::uint32_t, 3> orientation{};
            for (std::size_t index = 0; index < 3; ++index)
            {
                const auto parsedPosition = coordinate(fields[index]);
                const auto parsedOrientation = number<std::uint32_t>(fields[index + 3]);
                if (!parsedPosition || !parsedOrientation)
                    return std::nullopt;
                position[index] = *parsedPosition;
                orientation[index] = *parsedOrientation;
            }
            return Transform(cell, Position3(position[0], position[1], position[2]),
                Orientation3(Turn32::fromValue(orientation[0]), Turn32::fromValue(orientation[1]),
                    Turn32::fromValue(orientation[2])));
        }
    }

    InteractiveObjectContentLoadResult loadInteractiveObjectContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path))
            return InteractiveObjectContentError::Unavailable;
        if (std::filesystem::file_size(path) > MaximumInteractiveObjectContentBytes)
            return InteractiveObjectContentError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return InteractiveObjectContentError::Unavailable;
        std::string text;
        text.reserve(MaximumInteractiveObjectContentBytes + 1);
        char byte = 0;
        while (stream.get(byte))
        {
            text.push_back(byte);
            if (text.size() > MaximumInteractiveObjectContentBytes)
                return InteractiveObjectContentError::TooLarge;
        }
        if (!stream.eof())
            return InteractiveObjectContentError::Unavailable;

        std::optional<ContentManifestId> declaredManifest;
        std::vector<InteractiveObjectCatalogEntry> entries;
        std::map<CellId, std::size_t> cellCounts;
        std::size_t lineNumber = 0;
        for (std::size_t begin = 0; begin <= text.size();)
        {
            ++lineNumber;
            const auto lineEnd = text.find('\n', begin);
            const auto length = (lineEnd == std::string::npos ? text.size() : lineEnd) - begin;
            auto line = std::string_view(text).substr(begin, length);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (lineNumber == 1)
            {
                if (line != Header)
                    return InteractiveObjectContentError::Malformed;
            }
            else if (!line.empty())
            {
                std::array<std::string_view, MaximumFields> fields{};
                const auto count = tokenize(line, fields);
                if (!count || *count == 0)
                    return InteractiveObjectContentError::Malformed;
                const auto values = std::span(fields).first(*count);
                if (values[0] == "manifest")
                {
                    if (values.size() != 2 || declaredManifest)
                        return InteractiveObjectContentError::Malformed;
                    declaredManifest = ContentManifestId::fromHex(values[1]);
                    if (!declaredManifest)
                        return InteractiveObjectContentError::Malformed;
                }
                else if (values[0] == "object")
                {
                    if (values.size() < 14)
                        return InteractiveObjectContentError::Malformed;
                    const auto rawObject = number<std::uint64_t>(values[1]);
                    const auto objectId = rawObject ? InteractiveObjectId::fromValue(*rawObject) : std::nullopt;
                    const auto kind = values[2] == "standard" ? std::optional(InteractiveObjectKind::StandardDoor)
                        : values[2] == "teleport"             ? std::optional(InteractiveObjectKind::TeleportDoor)
                                                              : std::nullopt;
                    std::size_t cellFieldCount = 0;
                    const auto cell = parseCell(values.subspan(3), cellFieldCount);
                    std::size_t next = 3 + cellFieldCount;
                    if (!objectId || !kind || !cell || next + 9 > values.size())
                        return InteractiveObjectContentError::Malformed;
                    const auto transform = parseTransform(*cell, values.subspan(next, 6));
                    const auto lockLevel = number<std::uint32_t>(values[next + 6]);
                    const auto key = optionalId<KeyPrototypeId>(values[next + 7]);
                    const auto trap = optionalId<TrapPrototypeId>(values[next + 8]);
                    next += 9;
                    if (!transform || !lockLevel || !key || !trap || (*lockLevel == 0 && (*key).has_value()))
                        return InteractiveObjectContentError::Malformed;

                    std::optional<TeleportDestination> destination;
                    if (*kind == InteractiveObjectKind::TeleportDoor)
                    {
                        std::size_t destinationCellFields = 0;
                        const auto destinationCell = parseCell(values.subspan(next), destinationCellFields);
                        next += destinationCellFields;
                        if (!destinationCell || next + 6 != values.size())
                            return InteractiveObjectContentError::Malformed;
                        const auto destinationTransform = parseTransform(*destinationCell, values.subspan(next, 6));
                        if (!destinationTransform)
                            return InteractiveObjectContentError::Malformed;
                        destination = TeleportDestination{ *destinationCell, *destinationTransform };
                        next += 6;
                    }
                    if (next != values.size())
                        return InteractiveObjectContentError::Malformed;

                    entries.push_back({ *objectId, *kind, *cell, *transform, destination,
                        ObjectLockDeclaration{ *lockLevel != 0, *lockLevel, *key },
                        ObjectTrapDeclaration{ (*trap).has_value(), *trap } });
                    if (entries.size() > MaximumInteractiveObjectCatalogEntries)
                        return InteractiveObjectContentError::TooLarge;
                    if (++cellCounts[*cell] > MaximumInteractiveObjectsPerCell)
                        return InteractiveObjectContentError::CellCapacityExceeded;
                }
                else
                    return InteractiveObjectContentError::Malformed;
            }
            if (lineEnd == std::string::npos)
                break;
            begin = lineEnd + 1;
        }
        if (!declaredManifest)
            return InteractiveObjectContentError::Malformed;
        if (*declaredManifest != manifest.id())
            return InteractiveObjectContentError::ManifestMismatch;
        auto catalog = InteractiveObjectCatalog::create(manifest, entries);
        return catalog ? InteractiveObjectContentLoadResult(std::move(*catalog))
                       : InteractiveObjectContentLoadResult(InteractiveObjectContentError::InvalidCatalog);
    }
    catch (...)
    {
        return InteractiveObjectContentError::Unavailable;
    }
}
