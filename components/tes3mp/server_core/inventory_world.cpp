#include <tes3mp/inventory_world.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>

namespace TES3MP
{
    namespace
    {
        bool validStack(const CanonicalItemStack& stack, const ItemPrototypeCatalog& catalog) noexcept
        {
            const auto* declaration = catalog.find(stack.prototypeId);
            return declaration && stack.count != 0 && (declaration->stackable || stack.count == 1)
                && stack.condition <= declaration->maxCondition
                && stack.enchantmentCharge <= declaration->maxEnchantmentCharge;
        }

        std::optional<std::size_t> stackIndex(
            std::span<const CanonicalItemStack> stacks, const InventoryTransactionCommand& command) noexcept
        {
            if (!command.stackId)
                return std::nullopt;

            const auto found = std::ranges::lower_bound(stacks, *command.stackId, {}, &CanonicalItemStack::stackId);
            if (found == stacks.end() || found->stackId != *command.stackId
                || found->prototypeId != command.prototypeId)
                return std::nullopt;
            return static_cast<std::size_t>(found - stacks.begin());
        }

        std::optional<std::size_t> mergeIndex(std::span<const CanonicalItemStack> stacks,
            const CanonicalItemStack& source, const ItemPrototypeCatalog& catalog) noexcept
        {
            const auto found = std::ranges::find_if(
                stacks, [&](const CanonicalItemStack& candidate) { return candidate.canStackWith(source, catalog); });
            if (found == stacks.end())
                return std::nullopt;
            return static_cast<std::size_t>(found - stacks.begin());
        }

        std::size_t equipmentUseCount(const CanonicalPlayerInventoryState& player, ItemStackId stack,
            std::optional<std::size_t> excluded = {}) noexcept
        {
            std::size_t result = 0;
            for (std::size_t index = 0; index < player.equipment.size(); ++index)
            {
                if ((!excluded || *excluded != index) && player.equipment[index] == stack)
                    ++result;
            }
            return result;
        }

        bool addWouldOverflow(std::uint32_t current, std::uint32_t addition) noexcept
        {
            return std::numeric_limits<std::uint32_t>::max() - current < addition;
        }

    }

    bool CanonicalItemStack::canStackWith(
        const CanonicalItemStack& other, const ItemPrototypeCatalog& catalog) const noexcept
    {
        if (prototypeId != other.prototypeId)
            return false;
        const auto* declaration = catalog.find(prototypeId);
        return declaration && declaration->stackable && condition == other.condition
            && enchantmentCharge == other.enchantmentCharge && soulPrototype == other.soulPrototype;
    }

    const CanonicalItemStack* CanonicalPlayerInventoryState::findStack(ItemStackId id) const noexcept
    {
        const auto found = std::ranges::find(stacks, id, &CanonicalItemStack::stackId);
        return found == stacks.end() ? nullptr : &*found;
    }

    CanonicalItemStack* CanonicalPlayerInventoryState::findStack(ItemStackId id) noexcept
    {
        const auto found = std::ranges::find(stacks, id, &CanonicalItemStack::stackId);
        return found == stacks.end() ? nullptr : &*found;
    }

    std::uint64_t CanonicalPlayerInventoryState::totalWeight(const ItemPrototypeCatalog& catalog) const noexcept
    {
        std::uint64_t total = 0;
        for (const auto& stack : stacks)
        {
            if (const auto* declaration = catalog.find(stack.prototypeId))
            {
                const auto stackWeight
                    = static_cast<std::uint64_t>(declaration->weightUnits) * static_cast<std::uint64_t>(stack.count);
                if (std::numeric_limits<std::uint64_t>::max() - total < stackWeight)
                    return std::numeric_limits<std::uint64_t>::max();
                total += stackWeight;
            }
        }
        return total;
    }

    std::vector<KeyPrototypeId> CanonicalPlayerInventoryState::collectKeys(
        const ItemPrototypeCatalog& catalog) const noexcept
    try
    {
        std::vector<KeyPrototypeId> keys;
        keys.reserve(stacks.size());
        for (const auto& stack : stacks)
        {
            if (const auto* declaration = catalog.find(stack.prototypeId);
                stack.count != 0 && declaration && declaration->keyId)
                keys.push_back(*declaration->keyId);
        }
        std::ranges::sort(keys);
        const auto [first, last] = std::ranges::unique(keys);
        keys.erase(first, last);
        return keys;
    }
    catch (...)
    {
        return {};
    }

    const CanonicalItemStack* CanonicalContainerInventoryState::findStack(ItemStackId id) const noexcept
    {
        const auto found = std::ranges::find(stacks, id, &CanonicalItemStack::stackId);
        return found == stacks.end() ? nullptr : &*found;
    }

    CanonicalItemStack* CanonicalContainerInventoryState::findStack(ItemStackId id) noexcept
    {
        const auto found = std::ranges::find(stacks, id, &CanonicalItemStack::stackId);
        return found == stacks.end() ? nullptr : &*found;
    }

    std::uint64_t CanonicalContainerInventoryState::totalWeight(const ItemPrototypeCatalog& catalog) const noexcept
    {
        std::uint64_t total = 0;
        for (const auto& stack : stacks)
        {
            if (const auto* declaration = catalog.find(stack.prototypeId))
            {
                const auto stackWeight
                    = static_cast<std::uint64_t>(declaration->weightUnits) * static_cast<std::uint64_t>(stack.count);
                if (std::numeric_limits<std::uint64_t>::max() - total < stackWeight)
                    return std::numeric_limits<std::uint64_t>::max();
                total += stackWeight;
            }
        }
        return total;
    }

    std::optional<CanonicalInventoryWorld> CanonicalInventoryWorld::create(const ContentManifest& manifest,
        const ItemPrototypeCatalog& catalog, std::span<const CanonicalPlayerInventoryState> players,
        std::span<const CanonicalContainerInventoryState> containers,
        std::span<const CanonicalWorldItemState> worldItems, std::optional<ItemStackId> nextItemStackId) noexcept
    try
    {
        if (catalog.contentManifestId() != manifest.id() || players.size() > MaximumInventoryPlayers
            || containers.size() > MaximumInventoryContainers || worldItems.size() > MaximumWorldItemStacks)
            return std::nullopt;

        std::vector<CanonicalPlayerInventoryState> sortedPlayers(players.begin(), players.end());
        std::ranges::sort(sortedPlayers, {}, &CanonicalPlayerInventoryState::player);
        std::vector<CanonicalContainerInventoryState> sortedContainers(containers.begin(), containers.end());
        std::ranges::sort(sortedContainers, {}, &CanonicalContainerInventoryState::containerId);
        std::vector<CanonicalWorldItemState> sortedWorldItems(worldItems.begin(), worldItems.end());
        std::ranges::sort(sortedWorldItems, {}, [](const CanonicalWorldItemState& item) { return item.stack.stackId; });

        for (auto& player : sortedPlayers)
            std::ranges::sort(player.stacks, {}, &CanonicalItemStack::stackId);
        for (auto& container : sortedContainers)
            std::ranges::sort(container.stacks, {}, &CanonicalItemStack::stackId);

        std::vector<ItemStackId> stackIds;
        for (std::size_t index = 0; index < sortedPlayers.size(); ++index)
        {
            const auto& player = sortedPlayers[index];
            if ((index != 0 && sortedPlayers[index - 1].player == player.player)
                || player.stacks.size() > MaximumPlayerInventoryStacks)
                return std::nullopt;
            for (const auto& stack : player.stacks)
            {
                if (!validStack(stack, catalog))
                    return std::nullopt;
                stackIds.push_back(stack.stackId);
            }
            for (std::size_t slot = 0; slot < player.equipment.size(); ++slot)
            {
                if (!player.equipment[slot])
                    continue;
                const auto* stack = player.findStack(*player.equipment[slot]);
                const auto equipmentSlot = static_cast<EquipmentSlot>(slot);
                const auto* declaration = stack ? catalog.find(stack->prototypeId) : nullptr;
                if (!stack || !declaration || (declaration->slotMask & slotToMask(equipmentSlot)) == 0
                    || equipmentUseCount(player, stack->stackId) > stack->count)
                    return std::nullopt;
            }
        }

        for (std::size_t index = 0; index < sortedContainers.size(); ++index)
        {
            const auto& container = sortedContainers[index];
            if ((index != 0 && sortedContainers[index - 1].containerId == container.containerId)
                || !manifest.contains(container.cell) || container.stacks.size() > MaximumContainerStacks)
                return std::nullopt;
            for (const auto& stack : container.stacks)
            {
                if (!validStack(stack, catalog))
                    return std::nullopt;
                stackIds.push_back(stack.stackId);
            }
            if (container.capacityWeight != 0 && container.totalWeight(catalog) > container.capacityWeight)
                return std::nullopt;
        }

        for (const auto& item : sortedWorldItems)
        {
            if (!manifest.contains(item.cell) || !validStack(item.stack, catalog))
                return std::nullopt;
            stackIds.push_back(item.stack.stackId);
        }

        std::ranges::sort(stackIds);
        if (std::ranges::adjacent_find(stackIds) != stackIds.end())
            return std::nullopt;

        const auto maximumStackId = stackIds.empty() ? 0 : stackIds.back().value();
        if (nextItemStackId && nextItemStackId->value() <= maximumStackId)
            return std::nullopt;
        if (!nextItemStackId && maximumStackId != std::numeric_limits<std::uint64_t>::max())
            nextItemStackId = ItemStackId::fromValue(maximumStackId + 1);

        return CanonicalInventoryWorld(manifest, catalog, std::move(sortedPlayers), std::move(sortedContainers),
            std::move(sortedWorldItems), nextItemStackId);
    }
    catch (...)
    {
        return std::nullopt;
    }

    const CanonicalPlayerInventoryState* CanonicalInventoryWorld::findPlayer(PlayerId player) const noexcept
    {
        const auto found = std::ranges::lower_bound(mPlayers, player, {}, &CanonicalPlayerInventoryState::player);
        return found != mPlayers.end() && found->player == player ? &*found : nullptr;
    }

    CanonicalPlayerInventoryState* CanonicalInventoryWorld::findMutablePlayer(PlayerId player) noexcept
    {
        const auto found = std::ranges::lower_bound(mPlayers, player, {}, &CanonicalPlayerInventoryState::player);
        return found != mPlayers.end() && found->player == player ? &*found : nullptr;
    }

    const CanonicalContainerInventoryState* CanonicalInventoryWorld::findContainer(ContainerId container) const noexcept
    {
        const auto found
            = std::ranges::lower_bound(mContainers, container, {}, &CanonicalContainerInventoryState::containerId);
        return found != mContainers.end() && found->containerId == container ? &*found : nullptr;
    }

    CanonicalContainerInventoryState* CanonicalInventoryWorld::findMutableContainer(ContainerId container) noexcept
    {
        const auto found
            = std::ranges::lower_bound(mContainers, container, {}, &CanonicalContainerInventoryState::containerId);
        return found != mContainers.end() && found->containerId == container ? &*found : nullptr;
    }

    const CanonicalWorldItemState* CanonicalInventoryWorld::findWorldItem(ItemStackId stack) const noexcept
    {
        const auto found = std::ranges::lower_bound(
            mWorldItems, stack, {}, [](const CanonicalWorldItemState& item) { return item.stack.stackId; });
        return found != mWorldItems.end() && found->stack.stackId == stack ? &*found : nullptr;
    }

    std::vector<KeyPrototypeId> CanonicalInventoryWorld::collectVerifiedKeys(PlayerId player) const noexcept
    {
        const auto* playerState = findPlayer(player);
        return playerState ? playerState->collectKeys(mCatalog) : std::vector<KeyPrototypeId>{};
    }

    std::optional<ItemStackId> CanonicalInventoryWorld::allocateNextStackId() noexcept
    {
        const auto result = mNextItemStackId;
        if (!result || result->value() == std::numeric_limits<std::uint64_t>::max())
            mNextItemStackId = std::nullopt;
        else
            mNextItemStackId = ItemStackId::fromValue(result->value() + 1);
        return result;
    }

    bool CanonicalInventoryWorld::ensurePlayer(PlayerId player) noexcept
    try
    {
        const auto found = std::ranges::lower_bound(mPlayers, player, {}, &CanonicalPlayerInventoryState::player);
        if (found != mPlayers.end() && found->player == player)
            return true;
        if (mPlayers.size() >= MaximumInventoryPlayers)
            return false;
        mPlayers.insert(found, CanonicalPlayerInventoryState{ .player = player });
        return true;
    }
    catch (...)
    {
        return false;
    }

    bool CanonicalInventoryWorld::ensureContainer(
        ContainerId container, CellId cell, Position3 position, std::uint32_t capacityWeight) noexcept
    try
    {
        if (!mManifest.contains(cell))
            return false;
        const auto found
            = std::ranges::lower_bound(mContainers, container, {}, &CanonicalContainerInventoryState::containerId);
        if (found != mContainers.end() && found->containerId == container)
            return found->cell == cell && found->position == position && found->capacityWeight == capacityWeight;
        if (mContainers.size() >= MaximumInventoryContainers)
            return false;
        mContainers.insert(found,
            CanonicalContainerInventoryState{
                .containerId = container,
                .cell = cell,
                .position = position,
                .capacityWeight = capacityWeight,
            });
        return true;
    }
    catch (...)
    {
        return false;
    }

    InventoryTransactionOutcome applyInventoryTransaction(CanonicalInventoryWorld& world,
        const CanonicalServerState& players, const InventoryTransactionCommand& command,
        const InventoryValidationContext& context, ServerTick tick) noexcept
    try
    {
        static_assert(std::is_nothrow_move_constructible_v<CanonicalItemStack>);
        static_assert(std::is_nothrow_move_assignable_v<CanonicalItemStack>);
        static_assert(std::is_nothrow_move_constructible_v<CanonicalWorldItemState>);
        static_assert(std::is_nothrow_move_assignable_v<CanonicalWorldItemState>);

        auto makeOutcome = [&](InventoryTransactionResultCode code,
                               std::optional<ItemStackId> affectedStack = std::nullopt) noexcept {
            const auto* player = world.findPlayer(command.player);
            const auto* container = command.containerId ? world.findContainer(*command.containerId) : nullptr;
            const auto* worldItem = command.stackId ? world.findWorldItem(*command.stackId) : nullptr;
            return InventoryTransactionOutcome{
                .code = code,
                .player = command.player,
                .newInventoryRevision = player ? player->revision : command.expectedInventoryRevision,
                .containerId = command.containerId,
                .newContainerRevision = container ? std::optional(container->revision) : std::nullopt,
                .newWorldItemRevision = worldItem ? std::optional(worldItem->revision) : std::nullopt,
                .affectedStackId = affectedStack,
            };
        };

        const auto& catalog = world.mCatalog;
        const auto* playerState = players.findPlayer(command.player);
        auto* player = world.findMutablePlayer(command.player);
        if (!playerState || !player)
            return makeOutcome(InventoryTransactionResultCode::PlayerNotFound);
        if (player->revision != command.expectedInventoryRevision)
            return makeOutcome(InventoryTransactionResultCode::StaleInventoryRevision);
        if (tick < player->lastChangeTick || tick < playerState->lastSpatialChangeTick())
            return makeOutcome(InventoryTransactionResultCode::TickRegression);

        const auto& playerTransform = playerState->transform();
        const auto& playerCell = playerTransform.cell();
        const auto playerRoot = playerTransform.position();

        const bool transfersCount = command.kind == InventoryTransactionKind::TakeFromContainer
            || command.kind == InventoryTransactionKind::PutIntoContainer
            || command.kind == InventoryTransactionKind::DropItem
            || command.kind == InventoryTransactionKind::PickupItem;
        if (transfersCount && (command.count == 0 || command.count > MaximumTransferCount))
            return makeOutcome(InventoryTransactionResultCode::InsufficientCount);

        const ItemPrototypeDeclaration* declaration = nullptr;
        if (command.kind != InventoryTransactionKind::UnequipItem)
        {
            declaration = catalog.find(command.prototypeId);
            if (!declaration)
                return makeOutcome(InventoryTransactionResultCode::PrototypeNotFound);
        }

        switch (command.kind)
        {
            case InventoryTransactionKind::TakeFromContainer:
            {
                if (!command.containerId)
                    return makeOutcome(InventoryTransactionResultCode::ContainerNotFound);
                if (!command.expectedContainerRevision)
                    return makeOutcome(InventoryTransactionResultCode::MissingExpectedRevision);
                auto* container = world.findMutableContainer(*command.containerId);
                if (!container)
                    return makeOutcome(InventoryTransactionResultCode::ContainerNotFound);
                if (*command.expectedContainerRevision != container->revision)
                    return makeOutcome(InventoryTransactionResultCode::StaleContainerRevision);
                if (tick < container->lastChangeTick)
                    return makeOutcome(InventoryTransactionResultCode::TickRegression);
                if (container->cell != playerCell)
                    return makeOutcome(InventoryTransactionResultCode::CellMismatch);
                if (!positionsWithinReach(playerRoot, container->position, context.maxReach)
                    || !positionsWithinReach(command.interactionOrigin, container->position, context.maxReach))
                    return makeOutcome(InventoryTransactionResultCode::PlayerOutOfReach);
                if (!positionsWithinReach(playerRoot, command.interactionOrigin, context.maxReach))
                    return makeOutcome(InventoryTransactionResultCode::OriginOutOfReach);

                const auto sourceIndex = stackIndex(container->stacks, command);
                if (!sourceIndex)
                    return makeOutcome(InventoryTransactionResultCode::ItemNotFound);
                const auto source = container->stacks[*sourceIndex];
                if (source.count < command.count)
                    return makeOutcome(InventoryTransactionResultCode::InsufficientCount);
                const auto destinationIndex = mergeIndex(player->stacks, source, catalog);
                if (destinationIndex && addWouldOverflow(player->stacks[*destinationIndex].count, command.count))
                    return makeOutcome(InventoryTransactionResultCode::ArithmeticOverflow);
                if (!destinationIndex && player->stacks.size() >= MaximumPlayerInventoryStacks)
                    return makeOutcome(InventoryTransactionResultCode::PlayerInventoryFull);

                const auto nextPlayerRevision = player->revision.next();
                const auto nextContainerRevision = container->revision.next();
                if (!nextPlayerRevision || !nextContainerRevision)
                    return makeOutcome(InventoryTransactionResultCode::RevisionExhausted);
                const bool wholeStack = source.count == command.count;
                const bool needsNewId = !destinationIndex && !wholeStack;
                if (needsNewId && !world.mNextItemStackId)
                    return makeOutcome(InventoryTransactionResultCode::StackIdExhausted);
                if (!destinationIndex)
                    player->stacks.reserve(player->stacks.size() + 1);

                CanonicalItemStack transferred = source;
                transferred.count = command.count;
                if (needsNewId)
                    transferred.stackId = *world.allocateNextStackId();
                ItemStackId resultStackId = transferred.stackId;
                if (destinationIndex)
                {
                    player->stacks[*destinationIndex].count += command.count;
                    resultStackId = player->stacks[*destinationIndex].stackId;
                }
                else
                {
                    const auto destination = std::ranges::lower_bound(
                        player->stacks, transferred.stackId, {}, &CanonicalItemStack::stackId);
                    player->stacks.insert(destination, std::move(transferred));
                }
                if (wholeStack)
                    container->stacks.erase(container->stacks.begin() + static_cast<std::ptrdiff_t>(*sourceIndex));
                else
                    container->stacks[*sourceIndex].count -= command.count;
                player->revision = *nextPlayerRevision;
                player->lastChangeTick = tick;
                container->revision = *nextContainerRevision;
                container->lastChangeTick = tick;
                return makeOutcome(InventoryTransactionResultCode::Success, resultStackId);
            }

            case InventoryTransactionKind::PutIntoContainer:
            {
                if (!command.containerId)
                    return makeOutcome(InventoryTransactionResultCode::ContainerNotFound);
                if (!command.expectedContainerRevision)
                    return makeOutcome(InventoryTransactionResultCode::MissingExpectedRevision);
                auto* container = world.findMutableContainer(*command.containerId);
                if (!container)
                    return makeOutcome(InventoryTransactionResultCode::ContainerNotFound);
                if (*command.expectedContainerRevision != container->revision)
                    return makeOutcome(InventoryTransactionResultCode::StaleContainerRevision);
                if (tick < container->lastChangeTick)
                    return makeOutcome(InventoryTransactionResultCode::TickRegression);
                if (container->cell != playerCell)
                    return makeOutcome(InventoryTransactionResultCode::CellMismatch);
                if (!positionsWithinReach(playerRoot, container->position, context.maxReach)
                    || !positionsWithinReach(command.interactionOrigin, container->position, context.maxReach))
                    return makeOutcome(InventoryTransactionResultCode::PlayerOutOfReach);
                if (!positionsWithinReach(playerRoot, command.interactionOrigin, context.maxReach))
                    return makeOutcome(InventoryTransactionResultCode::OriginOutOfReach);

                const auto sourceIndex = stackIndex(player->stacks, command);
                if (!sourceIndex)
                    return makeOutcome(InventoryTransactionResultCode::ItemNotFound);
                const auto source = player->stacks[*sourceIndex];
                if (source.count < command.count)
                    return makeOutcome(InventoryTransactionResultCode::InsufficientCount);
                if (equipmentUseCount(*player, source.stackId) > source.count - command.count)
                    return makeOutcome(InventoryTransactionResultCode::ItemEquipped);

                if (container->capacityWeight != 0)
                {
                    const auto currentWeight = container->totalWeight(catalog);
                    const auto addedWeight = static_cast<std::uint64_t>(declaration->weightUnits) * command.count;
                    if (currentWeight > container->capacityWeight
                        || addedWeight > static_cast<std::uint64_t>(container->capacityWeight) - currentWeight)
                        return makeOutcome(InventoryTransactionResultCode::ContainerCapacityExceeded);
                }
                const auto destinationIndex = mergeIndex(container->stacks, source, catalog);
                if (destinationIndex && addWouldOverflow(container->stacks[*destinationIndex].count, command.count))
                    return makeOutcome(InventoryTransactionResultCode::ArithmeticOverflow);
                if (!destinationIndex && container->stacks.size() >= MaximumContainerStacks)
                    return makeOutcome(InventoryTransactionResultCode::ContainerCapacityExceeded);

                const auto nextPlayerRevision = player->revision.next();
                const auto nextContainerRevision = container->revision.next();
                if (!nextPlayerRevision || !nextContainerRevision)
                    return makeOutcome(InventoryTransactionResultCode::RevisionExhausted);
                const bool wholeStack = source.count == command.count;
                const bool needsNewId = !destinationIndex && !wholeStack;
                if (needsNewId && !world.mNextItemStackId)
                    return makeOutcome(InventoryTransactionResultCode::StackIdExhausted);
                if (!destinationIndex)
                    container->stacks.reserve(container->stacks.size() + 1);

                CanonicalItemStack transferred = source;
                transferred.count = command.count;
                if (needsNewId)
                    transferred.stackId = *world.allocateNextStackId();
                ItemStackId resultStackId = transferred.stackId;
                if (destinationIndex)
                {
                    container->stacks[*destinationIndex].count += command.count;
                    resultStackId = container->stacks[*destinationIndex].stackId;
                }
                else
                {
                    const auto destination = std::ranges::lower_bound(
                        container->stacks, transferred.stackId, {}, &CanonicalItemStack::stackId);
                    container->stacks.insert(destination, std::move(transferred));
                }
                if (wholeStack)
                    player->stacks.erase(player->stacks.begin() + static_cast<std::ptrdiff_t>(*sourceIndex));
                else
                    player->stacks[*sourceIndex].count -= command.count;
                player->revision = *nextPlayerRevision;
                player->lastChangeTick = tick;
                container->revision = *nextContainerRevision;
                container->lastChangeTick = tick;
                return makeOutcome(InventoryTransactionResultCode::Success, resultStackId);
            }

            case InventoryTransactionKind::EquipItem:
            {
                if (!command.slot || !command.stackId)
                    return makeOutcome(InventoryTransactionResultCode::SlotNotCompatible);
                const auto slot = static_cast<std::size_t>(*command.slot);
                if (slot >= player->equipment.size() || (declaration->slotMask & slotToMask(*command.slot)) == 0)
                    return makeOutcome(InventoryTransactionResultCode::SlotNotCompatible);
                const auto sourceIndex = stackIndex(player->stacks, command);
                if (!sourceIndex)
                    return makeOutcome(InventoryTransactionResultCode::ItemNotFound);
                const auto stackId = player->stacks[*sourceIndex].stackId;
                if (player->equipment[slot] == stackId)
                    return makeOutcome(InventoryTransactionResultCode::Success, stackId);
                if (equipmentUseCount(*player, stackId, slot) >= player->stacks[*sourceIndex].count)
                    return makeOutcome(InventoryTransactionResultCode::EquipmentQuantityExceeded);
                const auto nextRevision = player->revision.next();
                if (!nextRevision)
                    return makeOutcome(InventoryTransactionResultCode::RevisionExhausted);
                player->equipment[slot] = stackId;
                player->revision = *nextRevision;
                player->lastChangeTick = tick;
                return makeOutcome(InventoryTransactionResultCode::Success, stackId);
            }

            case InventoryTransactionKind::UnequipItem:
            {
                if (!command.slot)
                    return makeOutcome(InventoryTransactionResultCode::SlotNotCompatible);
                const auto slot = static_cast<std::size_t>(*command.slot);
                if (slot >= player->equipment.size())
                    return makeOutcome(InventoryTransactionResultCode::SlotNotCompatible);
                if (!player->equipment[slot])
                    return makeOutcome(InventoryTransactionResultCode::Success);
                if (command.stackId && command.stackId != player->equipment[slot])
                    return makeOutcome(InventoryTransactionResultCode::ItemNotFound);
                const auto nextRevision = player->revision.next();
                if (!nextRevision)
                    return makeOutcome(InventoryTransactionResultCode::RevisionExhausted);
                const auto unequippedStack = *player->equipment[slot];
                player->equipment[slot] = std::nullopt;
                player->revision = *nextRevision;
                player->lastChangeTick = tick;
                return makeOutcome(InventoryTransactionResultCode::Success, unequippedStack);
            }

            case InventoryTransactionKind::DropItem:
            {
                if (!positionsWithinReach(playerRoot, command.interactionOrigin, context.maxReach))
                    return makeOutcome(InventoryTransactionResultCode::OriginOutOfReach);
                const auto sourceIndex = stackIndex(player->stacks, command);
                if (!sourceIndex)
                    return makeOutcome(InventoryTransactionResultCode::ItemNotFound);
                const auto source = player->stacks[*sourceIndex];
                if (source.count < command.count)
                    return makeOutcome(InventoryTransactionResultCode::InsufficientCount);
                if (equipmentUseCount(*player, source.stackId) > source.count - command.count)
                    return makeOutcome(InventoryTransactionResultCode::ItemEquipped);
                if (world.mWorldItems.size() >= MaximumWorldItemStacks)
                    return makeOutcome(InventoryTransactionResultCode::WorldItemCapacityExceeded);
                const auto nextRevision = player->revision.next();
                if (!nextRevision)
                    return makeOutcome(InventoryTransactionResultCode::RevisionExhausted);
                const bool wholeStack = source.count == command.count;
                if (!wholeStack && !world.mNextItemStackId)
                    return makeOutcome(InventoryTransactionResultCode::StackIdExhausted);
                world.mWorldItems.reserve(world.mWorldItems.size() + 1);

                CanonicalItemStack dropped = source;
                dropped.count = command.count;
                if (!wholeStack)
                    dropped.stackId = *world.allocateNextStackId();
                if (wholeStack)
                    player->stacks.erase(player->stacks.begin() + static_cast<std::ptrdiff_t>(*sourceIndex));
                else
                    player->stacks[*sourceIndex].count -= command.count;
                const auto destination = std::ranges::lower_bound(world.mWorldItems, dropped.stackId, {},
                    [](const CanonicalWorldItemState& item) { return item.stack.stackId; });
                world.mWorldItems.insert(destination,
                    CanonicalWorldItemState{
                        .stack = dropped,
                        .cell = playerCell,
                        .position = command.interactionOrigin,
                        .lastChangeTick = tick,
                    });
                player->revision = *nextRevision;
                player->lastChangeTick = tick;
                auto outcome = makeOutcome(InventoryTransactionResultCode::Success, dropped.stackId);
                outcome.newWorldItemRevision = WorldItemRevision::initial();
                return outcome;
            }

            case InventoryTransactionKind::PickupItem:
            {
                if (!command.stackId)
                    return makeOutcome(InventoryTransactionResultCode::WorldItemNotFound);
                if (!command.expectedWorldItemRevision)
                    return makeOutcome(InventoryTransactionResultCode::MissingExpectedRevision);
                const auto worldItemIterator = std::ranges::lower_bound(world.mWorldItems, *command.stackId, {},
                    [](const CanonicalWorldItemState& item) { return item.stack.stackId; });
                if (worldItemIterator == world.mWorldItems.end() || worldItemIterator->stack.stackId != *command.stackId
                    || worldItemIterator->stack.prototypeId != command.prototypeId)
                    return makeOutcome(InventoryTransactionResultCode::WorldItemNotFound);
                const auto worldItemIndex = static_cast<std::size_t>(worldItemIterator - world.mWorldItems.begin());
                const auto source = worldItemIterator->stack;
                if (*command.expectedWorldItemRevision != worldItemIterator->revision)
                    return makeOutcome(InventoryTransactionResultCode::StaleWorldItemRevision);
                if (tick < worldItemIterator->lastChangeTick)
                    return makeOutcome(InventoryTransactionResultCode::TickRegression);
                if (worldItemIterator->cell != playerCell)
                    return makeOutcome(InventoryTransactionResultCode::CellMismatch);
                if (!positionsWithinReach(playerRoot, worldItemIterator->position, context.maxReach)
                    || !positionsWithinReach(command.interactionOrigin, worldItemIterator->position, context.maxReach))
                    return makeOutcome(InventoryTransactionResultCode::PlayerOutOfReach);
                if (!positionsWithinReach(playerRoot, command.interactionOrigin, context.maxReach))
                    return makeOutcome(InventoryTransactionResultCode::OriginOutOfReach);
                if (source.count < command.count)
                    return makeOutcome(InventoryTransactionResultCode::InsufficientCount);

                const auto destinationIndex = mergeIndex(player->stacks, source, catalog);
                if (destinationIndex && addWouldOverflow(player->stacks[*destinationIndex].count, command.count))
                    return makeOutcome(InventoryTransactionResultCode::ArithmeticOverflow);
                if (!destinationIndex && player->stacks.size() >= MaximumPlayerInventoryStacks)
                    return makeOutcome(InventoryTransactionResultCode::PlayerInventoryFull);
                const auto nextPlayerRevision = player->revision.next();
                if (!nextPlayerRevision)
                    return makeOutcome(InventoryTransactionResultCode::RevisionExhausted);
                const bool wholeStack = source.count == command.count;
                const auto nextWorldItemRevision
                    = wholeStack ? std::optional<WorldItemRevision>{} : worldItemIterator->revision.next();
                if (!wholeStack && !nextWorldItemRevision)
                    return makeOutcome(InventoryTransactionResultCode::RevisionExhausted);
                const bool needsNewId = !destinationIndex && !wholeStack;
                if (needsNewId && !world.mNextItemStackId)
                    return makeOutcome(InventoryTransactionResultCode::StackIdExhausted);
                if (!destinationIndex)
                    player->stacks.reserve(player->stacks.size() + 1);

                CanonicalItemStack pickedUp = source;
                pickedUp.count = command.count;
                if (needsNewId)
                    pickedUp.stackId = *world.allocateNextStackId();
                ItemStackId resultStackId = pickedUp.stackId;
                if (destinationIndex)
                {
                    player->stacks[*destinationIndex].count += command.count;
                    resultStackId = player->stacks[*destinationIndex].stackId;
                }
                else
                {
                    const auto destination
                        = std::ranges::lower_bound(player->stacks, pickedUp.stackId, {}, &CanonicalItemStack::stackId);
                    player->stacks.insert(destination, std::move(pickedUp));
                }
                if (wholeStack)
                    world.mWorldItems.erase(world.mWorldItems.begin() + static_cast<std::ptrdiff_t>(worldItemIndex));
                else
                {
                    auto& worldItem = world.mWorldItems[worldItemIndex];
                    worldItem.stack.count -= command.count;
                    worldItem.revision = *nextWorldItemRevision;
                    worldItem.lastChangeTick = tick;
                }
                player->revision = *nextPlayerRevision;
                player->lastChangeTick = tick;
                return makeOutcome(InventoryTransactionResultCode::Success, resultStackId);
            }
        }

        return makeOutcome(InventoryTransactionResultCode::InternalError);
    }
    catch (...)
    {
        const auto* player = world.findPlayer(command.player);
        return InventoryTransactionOutcome{
            .code = InventoryTransactionResultCode::InternalError,
            .player = command.player,
            .newInventoryRevision = player ? player->revision : command.expectedInventoryRevision,
        };
    }
}
