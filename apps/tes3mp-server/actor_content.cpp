#include "actor_content.hpp"

#include <tes3mp/actor_replication.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <string_view>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_ACTORS_V1";
        constexpr std::size_t MaximumFields = 111;

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
            return parsed && *parsed >= -MaximumActorCoordinate && *parsed <= MaximumActorCoordinate
                ? parsed : std::nullopt;
        }

        std::optional<std::size_t> tokenize(
            std::string_view line, std::array<std::string_view, MaximumFields>& output) noexcept
        {
            std::size_t count = 0;
            for (std::size_t begin = 0; begin < line.size();)
            {
                while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) ++begin;
                if (begin == line.size()) break;
                if (count == output.size()) return std::nullopt;
                auto end = begin;
                while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
                output[count++] = line.substr(begin, end - begin);
                begin = end;
            }
            return count;
        }

        std::optional<CellId> parseCell(std::span<const std::string_view> fields, std::size_t& next) noexcept
        {
            if (fields.size() < 2) return std::nullopt;
            const auto rawSpace = number<std::uint64_t>(fields[1]);
            const auto space = rawSpace ? CellSpaceId::fromValue(*rawSpace) : std::nullopt;
            if (!space) return std::nullopt;
            if (fields[0] == "interior")
            {
                next = 2;
                return CellId::interior(*space);
            }
            if (fields[0] != "exterior" || fields.size() < 4) return std::nullopt;
            const auto x = number<std::int32_t>(fields[2]);
            const auto y = number<std::int32_t>(fields[3]);
            if (!x || !y) return std::nullopt;
            next = 4;
            return CellId::exterior(*space, *x, *y);
        }

        std::optional<ActorAiPackageKind> packageKind(std::string_view value) noexcept
        {
            if (value == "idle") return ActorAiPackageKind::Idle;
            if (value == "travel") return ActorAiPackageKind::Travel;
            if (value == "wander") return ActorAiPackageKind::Wander;
            return std::nullopt;
        }
    }

    ActorContentLoadResult loadActorContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path)) return ActorContentError::Unavailable;
        if (std::filesystem::file_size(path) > MaximumActorContentBytes) return ActorContentError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return ActorContentError::Unavailable;
        std::string text;
        text.reserve(MaximumActorContentBytes + 1);
        char byte = 0;
        while (stream.get(byte))
        {
            text.push_back(byte);
            if (text.size() > MaximumActorContentBytes) return ActorContentError::TooLarge;
        }
        if (!stream.eof()) return ActorContentError::Unavailable;

        std::optional<ContentManifestId> declaredManifest;
        std::vector<ActorCatalogEntry> entries;
        std::map<CellId, std::size_t> cellCounts;
        std::size_t lineNumber = 0;
        for (std::size_t begin = 0; begin <= text.size();)
        {
            ++lineNumber;
            const auto lineEnd = text.find('\n', begin);
            const auto length = (lineEnd == std::string::npos ? text.size() : lineEnd) - begin;
            auto line = std::string_view(text).substr(begin, length);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (lineNumber == 1)
            {
                if (line != Header) return ActorContentError::Malformed;
            }
            else if (!line.empty())
            {
                std::array<std::string_view, MaximumFields> fields{};
                const auto count = tokenize(line, fields);
                if (!count || *count == 0) return ActorContentError::Malformed;
                const auto values = std::span(fields).first(*count);
                if (values[0] == "manifest")
                {
                    if (values.size() != 2 || declaredManifest) return ActorContentError::Malformed;
                    declaredManifest = ContentManifestId::fromHex(values[1]);
                    if (!declaredManifest) return ActorContentError::Malformed;
                }
                else if (values[0] == "actor")
                {
                    if (values.size() < 13) return ActorContentError::Malformed;
                    const auto rawActor = number<std::uint64_t>(values[1]);
                    const auto rawEntity = number<std::uint64_t>(values[2]);
                    const auto rawPrototype = number<std::uint64_t>(values[3]);
                    const auto actor = rawActor ? ActorId::fromValue(*rawActor) : std::nullopt;
                    const auto entity = rawEntity ? EntityId::fromValue(*rawEntity) : std::nullopt;
                    const auto prototype = rawPrototype ? ActorPrototypeId::fromValue(*rawPrototype) : std::nullopt;
                    std::size_t cellFields = 0;
                    const auto cell = parseCell(values.subspan(4), cellFields);
                    const auto first = 4 + cellFields;
                    if (!actor || !entity || !prototype || !cell || !manifest.contains(*cell)
                        || first + 7 > values.size() || (values.size() - (first + 7)) % 3 != 0)
                        return ActorContentError::Malformed;
                    std::array<std::int64_t, 3> positionValues{};
                    std::array<std::uint32_t, 3> orientationValues{};
                    for (std::size_t i = 0; i < 3; ++i)
                    {
                        const auto parsedPosition = coordinate(values[first + i]);
                        const auto parsedOrientation = number<std::uint32_t>(values[first + 3 + i]);
                        if (!parsedPosition || !parsedOrientation) return ActorContentError::Malformed;
                        positionValues[i] = *parsedPosition;
                        orientationValues[i] = *parsedOrientation;
                    }
                    const auto kind = packageKind(values[first + 6]);
                    if (!kind) return ActorContentError::Malformed;
                    std::vector<Position3> waypoints;
                    for (std::size_t index = first + 7; index < values.size(); index += 3)
                    {
                        std::array<std::int64_t, 3> waypoint{};
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            const auto parsed = coordinate(values[index + axis]);
                            if (!parsed) return ActorContentError::Malformed;
                            waypoint[axis] = *parsed;
                        }
                        waypoints.emplace_back(waypoint[0], waypoint[1], waypoint[2]);
                    }
                    auto package = ActorAiPackage::create(*kind, waypoints);
                    if (!package) return ActorContentError::Malformed;
                    const auto zero = [](std::uint32_t value) { return Turn32::fromValue(value); };
                    entries.push_back({ *actor, *entity, *prototype,
                        Transform(*cell, Position3(positionValues[0], positionValues[1], positionValues[2]),
                            Orientation3(zero(orientationValues[0]), zero(orientationValues[1]),
                                zero(orientationValues[2]))), std::move(*package) });
                    if (entries.size() > MaximumActorCatalogEntries) return ActorContentError::TooLarge;
                    if (++cellCounts[*cell] > MaximumActorInterestMembers)
                        return ActorContentError::CellCapacityExceeded;
                }
                else return ActorContentError::Malformed;
            }
            if (lineEnd == std::string::npos) break;
            begin = lineEnd + 1;
        }
        if (!declaredManifest) return ActorContentError::Malformed;
        if (*declaredManifest != manifest.id()) return ActorContentError::ManifestMismatch;
        auto catalog = ActorCatalog::create(manifest, entries);
        return catalog ? ActorContentLoadResult(std::move(*catalog))
                       : ActorContentLoadResult(ActorContentError::InvalidCatalog);
    }
    catch (...)
    {
        return ActorContentError::Unavailable;
    }
}
