#include "inventory_content.hpp"

#include <tes3mp/inventory_replication.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_INVENTORY_V1";
        constexpr std::size_t MaximumFields = 24;

        template <class Value>
        std::optional<Value> number(std::string_view text) noexcept
        {
            Value value{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
                ? std::optional<Value>(value)
                : std::nullopt;
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

        std::optional<CellId> cell(std::span<const std::string_view> fields, std::size_t& consumed) noexcept
        {
            if (fields.size() < 2)
                return std::nullopt;
            const auto raw = number<std::uint64_t>(fields[1]);
            const auto space = raw ? CellSpaceId::fromValue(*raw) : std::nullopt;
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

        std::optional<CanonicalItemStack> stack(std::span<const std::string_view> fields) noexcept
        {
            if (fields.size() != 6)
                return std::nullopt;
            const auto stackRaw = number<std::uint64_t>(fields[0]);
            const auto prototypeRaw = number<std::uint64_t>(fields[1]);
            const auto count = number<std::uint32_t>(fields[2]);
            const auto condition = number<std::uint32_t>(fields[3]);
            const auto charge = number<std::uint32_t>(fields[4]);
            const auto soul = optionalId<ActorPrototypeId>(fields[5]);
            const auto stackId = stackRaw ? ItemStackId::fromValue(*stackRaw) : std::nullopt;
            const auto prototype = prototypeRaw ? ItemPrototypeId::fromValue(*prototypeRaw) : std::nullopt;
            if (!stackId || !prototype || !count || *count == 0 || !condition || !charge || !soul)
                return std::nullopt;
            return CanonicalItemStack{ *stackId, *prototype, *count, *condition, *charge, *soul };
        }
    }

    InventoryContentLoadResult loadInventoryContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path))
            return InventoryContentError::Unavailable;
        if (std::filesystem::file_size(path) > MaximumInventoryContentBytes)
            return InventoryContentError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return InventoryContentError::Unavailable;
        std::string text;
        text.reserve(MaximumInventoryContentBytes + 1);
        char byte = 0;
        while (stream.get(byte))
        {
            text.push_back(byte);
            if (text.size() > MaximumInventoryContentBytes)
                return InventoryContentError::TooLarge;
        }
        if (!stream.eof())
            return InventoryContentError::Unavailable;

        std::optional<ContentManifestId> declaredManifest;
        std::vector<ItemPrototypeDeclaration> prototypes;
        std::map<ContainerId, CanonicalContainerInventoryState> containers;
        std::vector<std::pair<ContainerId, CanonicalItemStack>> containerItems;
        std::vector<CanonicalWorldItemState> groundItems;
        std::size_t lineNumber = 0;
        for (std::size_t begin = 0; begin <= text.size();)
        {
            ++lineNumber;
            const auto end = text.find('\n', begin);
            const auto length = (end == std::string::npos ? text.size() : end) - begin;
            auto line = std::string_view(text).substr(begin, length);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (lineNumber == 1)
            {
                if (line != Header)
                    return InventoryContentError::Malformed;
            }
            else if (!line.empty())
            {
                std::array<std::string_view, MaximumFields> fields{};
                const auto count = tokenize(line, fields);
                if (!count || *count == 0)
                    return InventoryContentError::Malformed;
                const auto values = std::span(fields).first(*count);
                if (values[0] == "manifest")
                {
                    if (values.size() != 2 || declaredManifest)
                        return InventoryContentError::Malformed;
                    declaredManifest = ContentManifestId::fromHex(values[1]);
                    if (!declaredManifest)
                        return InventoryContentError::Malformed;
                }
                else if (values[0] == "prototype")
                {
                    if (values.size() != 10)
                        return InventoryContentError::Malformed;
                    const auto rawId = number<std::uint64_t>(values[1]);
                    const auto id = rawId ? ItemPrototypeId::fromValue(*rawId) : std::nullopt;
                    const auto category = number<std::uint8_t>(values[2]);
                    const auto weight = number<std::uint32_t>(values[3]);
                    const auto itemValue = number<std::uint32_t>(values[4]);
                    const auto condition = number<std::uint32_t>(values[5]);
                    const auto charge = number<std::uint32_t>(values[6]);
                    const auto slots = number<std::uint32_t>(values[7]);
                    const auto stackable = number<std::uint8_t>(values[8]);
                    const auto key = optionalId<KeyPrototypeId>(values[9]);
                    if (!id || !category || *category > static_cast<std::uint8_t>(ItemCategory::Weapon) || !weight
                        || !itemValue || !condition || !charge || !slots || !stackable || *stackable > 1 || !key)
                        return InventoryContentError::Malformed;
                    prototypes.push_back({ *id, static_cast<ItemCategory>(*category), *weight, *itemValue, *condition,
                        *charge, *slots, *stackable != 0, *key });
                    if (prototypes.size() > MaximumItemPrototypes)
                        return InventoryContentError::TooLarge;
                }
                else if (values[0] == "container")
                {
                    if (values.size() < 8)
                        return InventoryContentError::Malformed;
                    const auto rawId = number<std::uint64_t>(values[1]);
                    const auto id = rawId ? ContainerId::fromValue(*rawId) : std::nullopt;
                    std::size_t cellFields = 0;
                    const auto parsedCell = cell(values.subspan(2), cellFields);
                    const auto next = 2 + cellFields;
                    if (!id || !parsedCell || values.size() != next + 4)
                        return InventoryContentError::Malformed;
                    const auto x = number<std::int64_t>(values[next]);
                    const auto y = number<std::int64_t>(values[next + 1]);
                    const auto z = number<std::int64_t>(values[next + 2]);
                    const auto capacity = number<std::uint32_t>(values[next + 3]);
                    if (!x || !y || !z || !capacity || containers.contains(*id))
                        return InventoryContentError::Malformed;
                    containers.emplace(*id,
                        CanonicalContainerInventoryState{ *id, *parsedCell, Position3(*x, *y, *z),
                            ContainerRevision::initial(), ServerTick::initial(), *capacity, {} });
                    if (containers.size() > MaximumInventoryContainers)
                        return InventoryContentError::TooLarge;
                }
                else if (values[0] == "container_item")
                {
                    if (values.size() != 8)
                        return InventoryContentError::Malformed;
                    const auto rawContainer = number<std::uint64_t>(values[1]);
                    const auto container = rawContainer ? ContainerId::fromValue(*rawContainer) : std::nullopt;
                    const auto parsed = stack(values.subspan(2));
                    if (!container || !parsed)
                        return InventoryContentError::Malformed;
                    containerItems.emplace_back(*container, *parsed);
                }
                else if (values[0] == "ground_item")
                {
                    if (values.size() < 12)
                        return InventoryContentError::Malformed;
                    const auto parsed = stack(values.subspan(1, 6));
                    std::size_t cellFields = 0;
                    const auto parsedCell = cell(values.subspan(7), cellFields);
                    const auto next = 7 + cellFields;
                    if (!parsed || !parsedCell || values.size() != next + 3)
                        return InventoryContentError::Malformed;
                    const auto x = number<std::int64_t>(values[next]);
                    const auto y = number<std::int64_t>(values[next + 1]);
                    const auto z = number<std::int64_t>(values[next + 2]);
                    if (!x || !y || !z)
                        return InventoryContentError::Malformed;
                    groundItems.push_back({ *parsed, *parsedCell, Position3(*x, *y, *z), WorldItemRevision::initial(),
                        ServerTick::initial() });
                    if (groundItems.size() > MaximumWorldItemStacks)
                        return InventoryContentError::TooLarge;
                }
                else
                    return InventoryContentError::Malformed;
            }
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
        if (!declaredManifest)
            return InventoryContentError::Malformed;
        if (*declaredManifest != manifest.id())
            return InventoryContentError::ManifestMismatch;
        std::ranges::sort(prototypes, {}, &ItemPrototypeDeclaration::id);
        auto catalog = ItemPrototypeCatalog::create(manifest, prototypes);
        if (!catalog)
            return InventoryContentError::InvalidCatalog;
        for (auto& [containerId, item] : containerItems)
        {
            const auto found = containers.find(containerId);
            if (found == containers.end())
                return InventoryContentError::Malformed;
            found->second.stacks.push_back(std::move(item));
        }
        std::vector<CanonicalContainerInventoryState> containerStates;
        for (auto& [ignored, container] : containers)
        {
            (void)ignored;
            std::ranges::sort(container.stacks, {}, &CanonicalItemStack::stackId);
            containerStates.push_back(std::move(container));
        }
        std::ranges::sort(groundItems, {}, [](const CanonicalWorldItemState& item) { return item.stack.stackId; });
        auto world = CanonicalInventoryWorld::create(manifest, *catalog, {}, containerStates, groundItems);
        if (!world)
            return InventoryContentError::InvalidWorld;

        std::map<CellId, std::size_t> reliableMessages;
        std::map<CellId, std::size_t> groundCounts;
        for (const auto& container : world->containers())
        {
            reliableMessages[container.cell] += std::max<std::size_t>(1,
                (container.stacks.size() + MaximumInventoryBaselineChunkStacks - 1)
                    / MaximumInventoryBaselineChunkStacks);
            groundCounts.try_emplace(container.cell, 0);
        }
        for (const auto& item : world->worldItems())
            ++groundCounts[item.cell];
        for (const auto& [itemCell, count] : groundCounts)
            reliableMessages[itemCell] += std::max<std::size_t>(
                1, (count + MaximumGroundItemBaselineChunkItems - 1) / MaximumGroundItemBaselineChunkItems);
        if (std::ranges::any_of(reliableMessages,
                [](const auto& entry) { return entry.second > MaximumInventoryReliableMessagesPerCell; }))
            return InventoryContentError::InvalidWorld;
        return InventoryContent{ std::move(*catalog), std::move(*world) };
    }
    catch (...)
    {
        return InventoryContentError::Unavailable;
    }
}
