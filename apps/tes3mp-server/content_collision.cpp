#include "content_collision.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_COLLISION_V1";

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
            const auto value = number<std::int64_t>(text);
            if (!value || *value < -MaximumCollisionCoordinate || *value > MaximumCollisionCoordinate)
                return std::nullopt;
            return value;
        }

        std::optional<std::size_t> tokenize(
            std::string_view line, std::array<std::string_view, 11>& output) noexcept
        {
            std::size_t count = 0;
            std::size_t begin = 0;
            while (begin < line.size())
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

        std::optional<CellId> cellId(
            std::span<const std::string_view> fields, std::size_t& next) noexcept
        {
            if (fields.size() < 2)
                return std::nullopt;
            const auto rawSpace = number<std::uint64_t>(fields[1]);
            const auto space = rawSpace ? CellSpaceId::fromValue(*rawSpace) : std::nullopt;
            if (!space)
                return std::nullopt;
            if (fields[0] == "interior")
            {
                next = 2;
                return CellId::interior(*space);
            }
            if (fields[0] != "exterior" || fields.size() < 4)
                return std::nullopt;
            const auto gridX = number<std::int32_t>(fields[2]);
            const auto gridY = number<std::int32_t>(fields[3]);
            if (!gridX || !gridY)
                return std::nullopt;
            next = 4;
            return CellId::exterior(*space, *gridX, *gridY);
        }

        bool bounded(Position3 value) noexcept
        {
            return value.x() >= -MaximumCollisionCoordinate && value.x() <= MaximumCollisionCoordinate
                && value.y() >= -MaximumCollisionCoordinate && value.y() <= MaximumCollisionCoordinate
                && value.z() >= -MaximumCollisionCoordinate && value.z() <= MaximumCollisionCoordinate;
        }

        bool inside(Position3 value, Position3 minimum, Position3 maximum) noexcept
        {
            return value.x() > minimum.x() && value.x() < maximum.x()
                && value.y() > minimum.y() && value.y() < maximum.y()
                && value.z() > minimum.z() && value.z() < maximum.z();
        }

        bool attemptedStepMatchesVelocity(const ServerCollisionRequest& request) noexcept
        {
            const auto current = request.currentRoot.position();
            const auto attempted = request.attemptedPosition;
            const auto velocity = request.attemptedVelocity;
            return velocity.x() >= -static_cast<std::int64_t>(MaximumMovementProfileQuantaPerTick)
                && velocity.x() <= static_cast<std::int64_t>(MaximumMovementProfileQuantaPerTick)
                && velocity.y() >= -static_cast<std::int64_t>(MaximumMovementProfileQuantaPerTick)
                && velocity.y() <= static_cast<std::int64_t>(MaximumMovementProfileQuantaPerTick)
                && velocity.z() >= -static_cast<std::int64_t>(MaximumMovementProfileQuantaPerTick)
                && velocity.z() <= static_cast<std::int64_t>(MaximumMovementProfileQuantaPerTick)
                && current.x() + velocity.x() == attempted.x()
                && current.y() + velocity.y() == attempted.y()
                && current.z() + velocity.z() == attempted.z();
        }

        struct Fraction
        {
            std::int64_t numerator;
            std::int64_t denominator;
        };

        bool less(Fraction left, Fraction right) noexcept
        {
            return left.numerator * right.denominator < right.numerator * left.denominator;
        }

        bool segmentIntersectsAxisAlignedBox(
            Position3 start, Position3 end, Position3 minimum, Position3 maximum) noexcept
        {
            Fraction entry{ 0, 1 };
            Fraction exit{ 1, 1 };
            const std::array starts{ start.x(), start.y(), start.z() };
            const std::array ends{ end.x(), end.y(), end.z() };
            const std::array minima{ minimum.x(), minimum.y(), minimum.z() };
            const std::array maxima{ maximum.x(), maximum.y(), maximum.z() };
            for (std::size_t axis = 0; axis < starts.size(); ++axis)
            {
                const auto delta = ends[axis] - starts[axis];
                if (delta == 0)
                {
                    if (starts[axis] <= minima[axis] || starts[axis] >= maxima[axis])
                        return false;
                    continue;
                }
                const auto denominator = delta > 0 ? delta : -delta;
                const Fraction axisEntry{ delta > 0 ? minima[axis] - starts[axis]
                                                     : starts[axis] - maxima[axis], denominator };
                const Fraction axisExit{ delta > 0 ? maxima[axis] - starts[axis]
                                                    : starts[axis] - minima[axis], denominator };
                if (less(entry, axisEntry))
                    entry = axisEntry;
                if (less(axisExit, exit))
                    exit = axisExit;
                if (less(exit, entry))
                    return false;
            }
            return less(Fraction{ 0, 1 }, exit) && !less(Fraction{ 1, 1 }, entry);
        }
    }

    std::variant<std::unique_ptr<ContentCollisionProvider>, ContentCollisionError>
    ContentCollisionProvider::load(const std::filesystem::path& path, const ContentManifest& manifest) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path))
            return ContentCollisionError::Unavailable;
        if (std::filesystem::file_size(path) > MaximumCollisionContentBytes)
            return ContentCollisionError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return ContentCollisionError::Unavailable;
        std::string text;
        text.reserve(MaximumCollisionContentBytes + 1);
        char byte = 0;
        while (stream.get(byte))
        {
            text.push_back(byte);
            if (text.size() > MaximumCollisionContentBytes)
                return ContentCollisionError::TooLarge;
        }
        if (!stream.eof())
            return ContentCollisionError::Unavailable;

        std::vector<CellId> cells;
        std::vector<CollisionBox> boxes;
        std::optional<ContentManifestId> declaredManifest;
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
                    return ContentCollisionError::Malformed;
            }
            else if (!line.empty())
            {
                std::array<std::string_view, 11> fields{};
                const auto count = tokenize(line, fields);
                if (!count || *count == 0)
                    return ContentCollisionError::Malformed;
                const auto values = std::span(fields).first(*count);
                if (values[0] == "manifest")
                {
                    if (values.size() != 2 || declaredManifest)
                        return ContentCollisionError::Malformed;
                    declaredManifest = ContentManifestId::fromHex(values[1]);
                    if (!declaredManifest)
                        return ContentCollisionError::Malformed;
                }
                else if (values[0] == "cell")
                {
                    std::size_t next = 0;
                    const auto cell = cellId(values.subspan(1), next);
                    if (!cell || next + 1 != values.size() || !manifest.contains(*cell))
                        return ContentCollisionError::Malformed;
                    cells.push_back(*cell);
                    if (cells.size() > MaximumContentCells)
                        return ContentCollisionError::TooLarge;
                }
                else if (values[0] == "solid")
                {
                    std::size_t next = 0;
                    const auto cell = cellId(values.subspan(1), next);
                    const auto firstCoordinate = next + 1;
                    if (!cell || firstCoordinate + 6 != values.size() || !manifest.contains(*cell))
                        return ContentCollisionError::Malformed;
                    std::array<std::int64_t, 6> coordinates{};
                    for (std::size_t index = 0; index < coordinates.size(); ++index)
                    {
                        const auto parsed = coordinate(values[firstCoordinate + index]);
                        if (!parsed)
                            return ContentCollisionError::Malformed;
                        coordinates[index] = *parsed;
                    }
                    const Position3 minimum(coordinates[0], coordinates[1], coordinates[2]);
                    const Position3 maximum(coordinates[3], coordinates[4], coordinates[5]);
                    if (minimum.x() >= maximum.x() || minimum.y() >= maximum.y()
                        || minimum.z() >= maximum.z())
                        return ContentCollisionError::Malformed;
                    boxes.push_back({ *cell, minimum, maximum });
                    if (boxes.size() > MaximumCollisionBoxes)
                        return ContentCollisionError::TooLarge;
                }
                else
                    return ContentCollisionError::Malformed;
            }
            if (lineEnd == std::string::npos)
                break;
            begin = lineEnd + 1;
        }
        if (!declaredManifest)
            return ContentCollisionError::Malformed;
        if (*declaredManifest != manifest.id())
            return ContentCollisionError::ManifestMismatch;
        std::ranges::sort(cells);
        if (std::ranges::adjacent_find(cells) != cells.end()
            || !std::ranges::equal(cells, manifest.cells()))
            return ContentCollisionError::IncompleteCells;
        std::ranges::sort(boxes);
        if (std::ranges::adjacent_find(boxes) != boxes.end())
            return ContentCollisionError::Malformed;
        if (std::ranges::any_of(boxes,
                [&](const CollisionBox& box) { return !std::ranges::binary_search(cells, box.cell); }))
            return ContentCollisionError::Malformed;
        return std::unique_ptr<ContentCollisionProvider>(
            new ContentCollisionProvider(*declaredManifest, std::move(cells), std::move(boxes)));
    }
    catch (...)
    {
        return ContentCollisionError::Unavailable;
    }

    std::optional<ServerCollisionResult> ContentCollisionProvider::resolve(
        const ServerCollisionRequest& request) noexcept
    {
        const auto current = request.currentRoot.position();
        if (request.contentManifest != mManifest
            || !std::ranges::binary_search(mCells, request.currentRoot.cell())
            || !bounded(current) || !attemptedStepMatchesVelocity(request))
            return std::nullopt;
        if (!bounded(request.attemptedPosition))
            return ServerCollisionResult{ current, LinearVelocity3(0, 0, 0) };
        const auto first = std::ranges::lower_bound(mBoxes, request.currentRoot.cell(), {}, &CollisionBox::cell);
        for (auto box = first; box != mBoxes.end() && box->cell == request.currentRoot.cell(); ++box)
        {
            if (segmentIntersectsAxisAlignedBox(current, request.attemptedPosition, box->minimum, box->maximum))
                return ServerCollisionResult{ current, LinearVelocity3(0, 0, 0) };
        }
        return ServerCollisionResult{ request.attemptedPosition, request.attemptedVelocity };
    }

    bool ContentCollisionProvider::canOccupy(const CellId& cell, Position3 position) const noexcept
    {
        if (!bounded(position) || !std::ranges::binary_search(mCells, cell))
            return false;
        const auto first = std::ranges::lower_bound(mBoxes, cell, {}, &CollisionBox::cell);
        const auto last = std::ranges::upper_bound(mBoxes, cell, {}, &CollisionBox::cell);
        return std::ranges::none_of(first, last, [&](const CollisionBox& box) {
            return inside(position, box.minimum, box.maximum);
        });
    }
}
