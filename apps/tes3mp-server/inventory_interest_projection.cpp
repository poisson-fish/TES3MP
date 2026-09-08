#include "inventory_interest_projection.hpp"

#include <tes3mp/protocol_frame.hpp>

#include <algorithm>
#include <limits>

namespace TES3MP::ServerApp
{
    namespace
    {
        std::uint32_t chunkCount(std::size_t size) noexcept
        {
            const auto count = std::max<std::size_t>(
                1, (size + MaximumInventoryBaselineChunkStacks - 1) / MaximumInventoryBaselineChunkStacks);
            return count > std::numeric_limits<std::uint32_t>::max() ? 0 : static_cast<std::uint32_t>(count);
        }

        std::uint32_t groundChunkCount(std::size_t size) noexcept
        {
            const auto count = std::max<std::size_t>(
                1, (size + MaximumGroundItemBaselineChunkItems - 1) / MaximumGroundItemBaselineChunkItems);
            return count > std::numeric_limits<std::uint32_t>::max() ? 0 : static_cast<std::uint32_t>(count);
        }

        InventoryBaselineHeader header(SessionId target, SessionGeneration generation, ServerTick tick,
            CanonicalRevision revision, std::uint32_t index, std::uint32_t count) noexcept
        {
            return { target, generation, tick, revision, index, count };
        }

        std::vector<EquipmentBinding> equipmentBindings(const CanonicalPlayerInventoryState& inventory)
        {
            std::vector<EquipmentBinding> result;
            for (std::size_t index = 0; index < inventory.equipment.size(); ++index)
                if (inventory.equipment[index])
                    result.push_back({ static_cast<EquipmentSlot>(index), *inventory.equipment[index] });
            return result;
        }

        PublicEquipmentMember publicEquipment(PlayerId player, const CanonicalPlayerInventoryState* inventory) noexcept
        {
            PublicEquipmentMember result{ .player = player };
            if (!inventory)
                return result;
            for (std::size_t index = 0; index < inventory->equipment.size(); ++index)
                if (inventory->equipment[index])
                    if (const auto* stack = inventory->findStack(*inventory->equipment[index]))
                        result.slots[index] = stack->prototypeId;
            return result;
        }

        bool addFrame(std::vector<std::vector<std::byte>>& owned,
            std::vector<OutboundQueueSet::AtomicMessage>& messages, TransportConnectionId connection,
            TransportChannel channel, MessageClass messageClass, MessageKind kind, std::vector<std::byte> payload)
        {
            auto frame = encodeProtocolFrame(messageClass, kind, payload);
            if (!std::holds_alternative<std::vector<std::byte>>(frame))
                return false;
            owned.push_back(std::get<std::vector<std::byte>>(std::move(frame)));
            messages.push_back({ connection, channel, owned.back() });
            return true;
        }
    }

    std::optional<InventoryInterestDelivery> projectInventoryInterestBaseline(const CanonicalServerState& players,
        const CanonicalInventoryWorld& inventory, SessionId target, ServerTick tick,
        CanonicalRevision canonicalRevision)
    try
    {
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        const auto* playerInventory = player ? inventory.findPlayer(player->playerId()) : nullptr;
        if (!session || !player || !playerInventory)
            return std::nullopt;

        InventoryInterestDelivery delivery{ .targetSession = target };
        const auto bindings = equipmentBindings(*playerInventory);
        const auto playerChunks = chunkCount(playerInventory->stacks.size());
        if (playerChunks == 0)
            return std::nullopt;
        delivery.playerInventory.reserve(playerChunks);
        for (std::uint32_t index = 0; index < playerChunks; ++index)
        {
            const auto offset = static_cast<std::size_t>(index) * MaximumInventoryBaselineChunkStacks;
            const auto count = std::min(MaximumInventoryBaselineChunkStacks,
                playerInventory->stacks.size() - std::min(offset, playerInventory->stacks.size()));
            const auto stacks
                = std::span(playerInventory->stacks).subspan(std::min(offset, playerInventory->stacks.size()), count);
            const auto currentBindings = index == 0 ? std::span(bindings) : std::span<const EquipmentBinding>{};
            auto baseline = ReliablePlayerInventoryBaseline::create(
                header(target, session->sessionGeneration(), tick, canonicalRevision, index, playerChunks),
                player->playerId(), playerInventory->revision, stacks, currentBindings);
            if (!std::holds_alternative<ReliablePlayerInventoryBaseline>(baseline))
                return std::nullopt;
            delivery.playerInventory.push_back(std::get<ReliablePlayerInventoryBaseline>(std::move(baseline)));
        }

        const auto& cell = player->transform().cell();
        for (const auto& container : inventory.containers())
        {
            if (container.cell != cell)
                continue;
            const auto chunks = chunkCount(container.stacks.size());
            if (chunks == 0)
                return std::nullopt;
            for (std::uint32_t index = 0; index < chunks; ++index)
            {
                const auto offset = static_cast<std::size_t>(index) * MaximumInventoryBaselineChunkStacks;
                const auto count = std::min(MaximumInventoryBaselineChunkStacks,
                    container.stacks.size() - std::min(offset, container.stacks.size()));
                auto baseline = ReliableContainerInventoryBaseline::create(
                    header(target, session->sessionGeneration(), tick, canonicalRevision, index, chunks),
                    container.containerId, container.cell, container.position, container.revision,
                    container.capacityWeight,
                    std::span(container.stacks).subspan(std::min(offset, container.stacks.size()), count));
                if (!std::holds_alternative<ReliableContainerInventoryBaseline>(baseline))
                    return std::nullopt;
                delivery.containers.push_back(std::get<ReliableContainerInventoryBaseline>(std::move(baseline)));
            }
        }

        std::vector<GroundItemInterestMember> ground;
        for (const auto& item : inventory.worldItems())
            if (item.cell == cell)
                ground.push_back({ item.stack, item.position, item.revision });
        const auto groundChunks = groundChunkCount(ground.size());
        if (groundChunks == 0)
            return std::nullopt;
        delivery.groundItems.reserve(groundChunks);
        for (std::uint32_t index = 0; index < groundChunks; ++index)
        {
            const auto offset = static_cast<std::size_t>(index) * MaximumGroundItemBaselineChunkItems;
            const auto count
                = std::min(MaximumGroundItemBaselineChunkItems, ground.size() - std::min(offset, ground.size()));
            auto baseline = ReliableGroundItemBaseline::create(
                header(target, session->sessionGeneration(), tick, canonicalRevision, index, groundChunks), cell,
                std::span(ground).subspan(std::min(offset, ground.size()), count));
            if (!std::holds_alternative<ReliableGroundItemBaseline>(baseline))
                return std::nullopt;
            delivery.groundItems.push_back(std::get<ReliableGroundItemBaseline>(std::move(baseline)));
        }

        std::vector<PublicEquipmentMember> equipment;
        for (const auto& visible : players.players())
            if (visible.transform().cell() == cell)
                equipment.push_back(publicEquipment(visible.playerId(), inventory.findPlayer(visible.playerId())));
        auto equipmentSnapshot = LatestWinsEquipmentSnapshot::create(
            target, session->sessionGeneration(), tick, canonicalRevision, equipment);
        if (!std::holds_alternative<LatestWinsEquipmentSnapshot>(equipmentSnapshot))
            return std::nullopt;
        delivery.equipment = std::get<LatestWinsEquipmentSnapshot>(std::move(equipmentSnapshot));
        return delivery;
    }
    catch (...)
    {
        return std::nullopt;
    }

    bool appendInventoryInterestMessages(std::vector<std::vector<std::byte>>& owned,
        std::vector<OutboundQueueSet::AtomicMessage>& messages, TransportConnectionId connection,
        const InventoryInterestDelivery& delivery)
    try
    {
        if (!delivery.equipment)
            return false;
        const auto additional
            = delivery.playerInventory.size() + delivery.containers.size() + delivery.groundItems.size() + 1;
        owned.reserve(owned.size() + additional);
        messages.reserve(messages.size() + additional);
        for (const auto& baseline : delivery.playerInventory)
            if (!addFrame(owned, messages, connection, TransportChannel::ReliableOrdered,
                    MessageClass::ReliableOperation, MessageKind::ReliablePlayerInventoryBaseline,
                    encodeReliablePlayerInventoryBaseline(baseline)))
                return false;
        for (const auto& baseline : delivery.containers)
            if (!addFrame(owned, messages, connection, TransportChannel::ReliableOrdered,
                    MessageClass::ReliableOperation, MessageKind::ReliableContainerInventoryBaseline,
                    encodeReliableContainerInventoryBaseline(baseline)))
                return false;
        for (const auto& baseline : delivery.groundItems)
            if (!addFrame(owned, messages, connection, TransportChannel::ReliableOrdered,
                    MessageClass::ReliableOperation, MessageKind::ReliableGroundItemBaseline,
                    encodeReliableGroundItemBaseline(baseline)))
                return false;
        return addFrame(owned, messages, connection, TransportChannel::LatestWins, MessageClass::LatestWinsSnapshot,
            MessageKind::LatestWinsEquipmentSnapshot, encodeLatestWinsEquipmentSnapshot(*delivery.equipment));
    }
    catch (...)
    {
        return false;
    }
}
