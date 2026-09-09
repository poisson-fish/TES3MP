#include <tes3mp/inventory_replication.hpp>
#include <tes3mp/protocol_frame.hpp>

#include "generated/client_inventory_transaction_command_generated.h"
#include "generated/latest_wins_equipment_snapshot_generated.h"
#include "generated/reliable_container_inventory_baseline_generated.h"
#include "generated/reliable_ground_item_baseline_generated.h"
#include "generated/reliable_player_inventory_baseline_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace
{
    namespace PlayerSchema = TES3MP::Protocol::Schema::PlayerInventoryBaseline;
    namespace ContainerSchema = TES3MP::Protocol::Schema::ContainerInventoryBaseline;
    namespace GroundSchema = TES3MP::Protocol::Schema::GroundItemBaseline;
    namespace EquipmentSchema = TES3MP::Protocol::Schema::EquipmentSnapshot;
    namespace CommandSchema = TES3MP::Protocol::Schema::InventoryCommand;
    using Error = TES3MP::InventoryReplicationDecodeError;
    using Code = TES3MP::InventoryReplicationDecodeErrorCode;

    constexpr std::size_t MinimumBytes = sizeof(flatbuffers::uoffset_t) * 2 + 4;

    template <class Struct>
    Struct copyStruct(const flatbuffers::Vector<const Struct*>* values, std::size_t index) noexcept
    {
        static_assert(std::is_trivially_copyable_v<Struct>);
        Struct result{};
        std::memcpy(&result, values->Data() + index * sizeof(Struct), sizeof(Struct));
        return result;
    }

    constexpr Error error(Code code, std::size_t observed = 0, std::size_t limit = 0, std::size_t index = 0) noexcept
    {
        return { code, observed, limit, index };
    }

    std::optional<Error> validatePrefix(std::span<const std::byte> payload, std::size_t maximum) noexcept
    {
        if (payload.size() < MinimumBytes)
            return error(Code::PayloadTooSmall, payload.size(), MinimumBytes);
        if (payload.size() > maximum)
            return error(Code::PayloadTooLarge, payload.size(), maximum);
        const auto declared
            = flatbuffers::GetSizePrefixedBufferLength(reinterpret_cast<const std::uint8_t*>(payload.data()));
        if (declared != payload.size())
            return error(Code::PayloadLengthMismatch, payload.size(), declared);
        return std::nullopt;
    }

    flatbuffers::Verifier verifier(std::span<const std::byte> payload, std::size_t maximum)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = 8;
        options.max_tables = 16;
        options.max_size = maximum + 1;
        options.check_alignment = true;
        options.check_nested_flatbuffers = false;
        return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    std::vector<std::byte> take(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return { begin, begin + builder.GetSize() };
    }

    template <class Value>
    std::variant<Value, Error> strong(std::uint64_t raw, std::size_t index = 0)
    {
        auto value = Value::fromValue(raw);
        return value ? std::variant<Value, Error>(*value)
                     : std::variant<Value, Error>(error(Code::InvalidStrongValue, raw, 0, index));
    }

    template <class Value>
    const Value* value(const std::variant<Value, Error>& decoded)
    {
        return std::get_if<Value>(&decoded);
    }

    std::optional<Error> validateHeader(const TES3MP::InventoryBaselineHeader& header) noexcept
    {
        if (header.chunkCount == 0 || header.chunkIndex >= header.chunkCount)
            return error(Code::InvalidChunk, header.chunkIndex, header.chunkCount);
        return std::nullopt;
    }

    std::optional<Error> validateStacks(std::span<const TES3MP::CanonicalItemStack> stacks) noexcept
    {
        if (stacks.size() > TES3MP::MaximumInventoryBaselineChunkStacks)
            return error(Code::TooManyEntries, stacks.size(), TES3MP::MaximumInventoryBaselineChunkStacks);
        for (std::size_t index = 0; index < stacks.size(); ++index)
        {
            if (stacks[index].count == 0)
                return error(Code::InvalidCommandShape, 0, 1, index);
            if (index && stacks[index - 1].stackId >= stacks[index].stackId)
                return error(Code::EntriesNotStrictlySorted, stacks[index].stackId.value(),
                    stacks[index - 1].stackId.value(), index);
        }
        return std::nullopt;
    }

    std::variant<TES3MP::CanonicalItemStack, Error> decodeStack(std::uint64_t stackRaw, std::uint64_t prototypeRaw,
        std::uint32_t count, std::uint32_t condition, std::uint32_t charge, bool hasSoul, std::uint64_t soulRaw,
        std::size_t index)
    {
        auto stack = strong<TES3MP::ItemStackId>(stackRaw, index);
        auto prototype = strong<TES3MP::ItemPrototypeId>(prototypeRaw, index);
        if (const auto* failure = std::get_if<Error>(&stack))
            return *failure;
        if (const auto* failure = std::get_if<Error>(&prototype))
            return *failure;
        if (count == 0)
            return error(Code::InvalidCommandShape, count, 1, index);
        std::optional<TES3MP::ActorPrototypeId> soul;
        if (hasSoul)
        {
            auto decoded = strong<TES3MP::ActorPrototypeId>(soulRaw, index);
            if (const auto* failure = std::get_if<Error>(&decoded))
                return *failure;
            soul = *value(decoded);
        }
        return TES3MP::CanonicalItemStack{ *value(stack), *value(prototype), count, condition, charge, soul };
    }

    std::variant<TES3MP::InventoryBaselineHeader, Error> decodeHeader(std::uint64_t sessionRaw,
        std::uint64_t generationRaw, std::uint64_t tickRaw, std::uint64_t revisionRaw, std::uint32_t chunkIndex,
        std::uint32_t chunkCount)
    {
        auto session = strong<TES3MP::SessionId>(sessionRaw);
        auto generation = strong<TES3MP::SessionGeneration>(generationRaw);
        auto tick = strong<TES3MP::ServerTick>(tickRaw);
        auto revision = strong<TES3MP::CanonicalRevision>(revisionRaw);
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&tick), std::get_if<Error>(&revision) };
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        TES3MP::InventoryBaselineHeader result{ *value(session), *value(generation), *value(tick), *value(revision),
            chunkIndex, chunkCount };
        if (const auto failure = validateHeader(result))
            return *failure;
        return result;
    }

    template <class Cell>
    std::variant<TES3MP::CellId, Error> decodeCell(const Cell& cell)
    {
        auto space = strong<TES3MP::CellSpaceId>(cell.cell_space_id());
        if (const auto* failure = std::get_if<Error>(&space))
            return *failure;
        const auto kind = static_cast<std::uint8_t>(cell.kind());
        if (kind == 1)
        {
            if (cell.grid_x() != 0 || cell.grid_y() != 0)
                return error(Code::InvalidInteriorGrid);
            return TES3MP::CellId::interior(*value(space));
        }
        if (kind == 2)
            return TES3MP::CellId::exterior(*value(space), cell.grid_x(), cell.grid_y());
        return error(Code::InvalidCellKind, kind);
    }

    ContainerSchema::Cell encodeContainerCell(const TES3MP::CellId& cell)
    {
        if (const auto* interior = cell.asInterior())
            return { interior->cellSpace().value(), 0, 0, ContainerSchema::CellKind::Interior, 0, 0, 0 };
        const auto& exterior = *cell.asExterior();
        return { exterior.worldspace().value(), exterior.gridX(), exterior.gridY(), ContainerSchema::CellKind::Exterior,
            0, 0, 0 };
    }

    GroundSchema::Cell encodeGroundCell(const TES3MP::CellId& cell)
    {
        if (const auto* interior = cell.asInterior())
            return { interior->cellSpace().value(), 0, 0, GroundSchema::CellKind::Interior, 0, 0, 0 };
        const auto& exterior = *cell.asExterior();
        return { exterior.worldspace().value(), exterior.gridX(), exterior.gridY(), GroundSchema::CellKind::Exterior, 0,
            0, 0 };
    }

    template <class SchemaStack>
    SchemaStack encodeStack(const TES3MP::CanonicalItemStack& stack)
    {
        return { stack.stackId.value(), stack.prototypeId.value(), stack.count, stack.condition,
            stack.enchantmentCharge, stack.soulPrototype ? stack.soulPrototype->value() : 0,
            static_cast<std::uint8_t>(stack.soulPrototype.has_value()), 0, 0 };
    }

    std::optional<Error> validateCommandShape(const TES3MP::ClientInventoryTransactionCommand& command) noexcept
    {
        const bool transfer = command.kind == TES3MP::InventoryTransactionKind::TakeFromContainer
            || command.kind == TES3MP::InventoryTransactionKind::PutIntoContainer;
        if (transfer
            && (!command.containerId || !command.stackId || !command.expectedContainerRevision || command.slot
                || command.expectedWorldItemRevision))
            return error(Code::InvalidCommandShape);
        if (command.kind == TES3MP::InventoryTransactionKind::EquipItem
            && (!command.stackId || !command.slot || command.containerId || command.expectedContainerRevision
                || command.expectedWorldItemRevision || command.count != 1))
            return error(Code::InvalidCommandShape);
        if (command.kind == TES3MP::InventoryTransactionKind::UnequipItem
            && (!command.slot || command.containerId || command.expectedContainerRevision
                || command.expectedWorldItemRevision || command.count != 1))
            return error(Code::InvalidCommandShape);
        if (command.kind == TES3MP::InventoryTransactionKind::DropItem
            && (!command.stackId || command.containerId || command.slot || command.expectedContainerRevision
                || command.expectedWorldItemRevision))
            return error(Code::InvalidCommandShape);
        if (command.kind == TES3MP::InventoryTransactionKind::PickupItem
            && (!command.stackId || !command.expectedWorldItemRevision || command.containerId || command.slot
                || command.expectedContainerRevision))
            return error(Code::InvalidCommandShape);
        if ((transfer || command.kind == TES3MP::InventoryTransactionKind::DropItem
                || command.kind == TES3MP::InventoryTransactionKind::PickupItem)
            && (command.count == 0 || command.count > TES3MP::MaximumTransferCount))
            return error(Code::InvalidCommandShape, command.count, TES3MP::MaximumTransferCount);
        return std::nullopt;
    }
}

namespace TES3MP
{
    std::variant<ReliablePlayerInventoryBaseline, InventoryReplicationDecodeError>
    ReliablePlayerInventoryBaseline::create(InventoryBaselineHeader header, PlayerId player, InventoryRevision revision,
        std::span<const CanonicalItemStack> stacks, std::span<const EquipmentBinding> equipment)
    {
        if (const auto failure = validateHeader(header))
            return *failure;
        if (const auto failure = validateStacks(stacks))
            return *failure;
        if (header.chunkIndex != 0 && !equipment.empty())
            return error(Code::InvalidChunk, header.chunkIndex, 0);
        if (equipment.size() > static_cast<std::size_t>(EquipmentSlot::Count))
            return error(Code::TooManyEntries, equipment.size(), static_cast<std::size_t>(EquipmentSlot::Count));
        for (std::size_t index = 0; index < equipment.size(); ++index)
        {
            if (static_cast<std::uint8_t>(equipment[index].slot) >= static_cast<std::uint8_t>(EquipmentSlot::Count))
                return error(Code::InvalidEquipmentSlot, static_cast<std::size_t>(equipment[index].slot), 0, index);
            if (index && equipment[index - 1].slot >= equipment[index].slot)
                return error(Code::EntriesNotStrictlySorted, static_cast<std::size_t>(equipment[index].slot),
                    static_cast<std::size_t>(equipment[index - 1].slot), index);
        }
        return ReliablePlayerInventoryBaseline{ header, player, revision, { stacks.begin(), stacks.end() },
            { equipment.begin(), equipment.end() } };
    }

    std::variant<ReliableContainerInventoryBaseline, InventoryReplicationDecodeError>
    ReliableContainerInventoryBaseline::create(InventoryBaselineHeader header, ContainerId container, CellId cell,
        Position3 position, ContainerRevision revision, std::uint32_t capacityWeight,
        std::span<const CanonicalItemStack> stacks)
    {
        if (const auto failure = validateHeader(header))
            return *failure;
        if (const auto failure = validateStacks(stacks))
            return *failure;
        return ReliableContainerInventoryBaseline{ header, container, std::move(cell), position, revision,
            capacityWeight, { stacks.begin(), stacks.end() } };
    }

    std::variant<ReliableGroundItemBaseline, InventoryReplicationDecodeError> ReliableGroundItemBaseline::create(
        InventoryBaselineHeader header, CellId cell, std::span<const GroundItemInterestMember> items)
    {
        if (const auto failure = validateHeader(header))
            return *failure;
        if (items.size() > MaximumGroundItemBaselineChunkItems)
            return error(Code::TooManyEntries, items.size(), MaximumGroundItemBaselineChunkItems);
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (items[index].stack.count == 0)
                return error(Code::InvalidCommandShape, 0, 0, index);
            if (index && items[index - 1].stack.stackId >= items[index].stack.stackId)
                return error(Code::EntriesNotStrictlySorted, items[index].stack.stackId.value(),
                    items[index - 1].stack.stackId.value(), index);
        }
        return ReliableGroundItemBaseline{ header, std::move(cell), { items.begin(), items.end() } };
    }

    std::variant<LatestWinsEquipmentSnapshot, InventoryReplicationDecodeError> LatestWinsEquipmentSnapshot::create(
        SessionId target, SessionGeneration generation, ServerTick tick, CanonicalRevision revision,
        std::span<const PublicEquipmentMember> members)
    {
        if (members.size() > MaximumEquipmentSnapshotPlayers)
            return error(Code::TooManyEntries, members.size(), MaximumEquipmentSnapshotPlayers);
        for (std::size_t index = 1; index < members.size(); ++index)
            if (members[index - 1].player >= members[index].player)
                return error(Code::EntriesNotStrictlySorted, members[index].player.value(),
                    members[index - 1].player.value(), index);
        return LatestWinsEquipmentSnapshot{ target, generation, tick, revision, { members.begin(), members.end() } };
    }

    std::vector<std::byte> encodeReliablePlayerInventoryBaseline(const ReliablePlayerInventoryBaseline& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto& h = input.header;
        const auto header = PlayerSchema::CreatePlayerInventoryBaselineHeader(builder, h.targetSessionId.value(),
            h.targetSessionGeneration.value(), h.serverTick.value(), h.canonicalRevision.value(), input.player.value(),
            input.revision.value(), h.chunkIndex, h.chunkCount);
        std::vector<PlayerSchema::ItemStack> stacks;
        for (const auto& stack : input.stacks)
            stacks.push_back(encodeStack<PlayerSchema::ItemStack>(stack));
        std::vector<PlayerSchema::EquipmentBinding> equipment;
        for (const auto& binding : input.equipment)
            equipment.emplace_back(binding.stackId.value(), static_cast<std::uint8_t>(binding.slot), 0, 0, 0);
        const auto root
            = PlayerSchema::CreateReliablePlayerInventoryBaselineDirect(builder, header, &stacks, &equipment);
        PlayerSchema::FinishSizePrefixedReliablePlayerInventoryBaselineBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeReliableContainerInventoryBaseline(const ReliableContainerInventoryBaseline& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto& h = input.header;
        const auto header = ContainerSchema::CreateContainerInventoryBaselineHeader(builder, h.targetSessionId.value(),
            h.targetSessionGeneration.value(), h.serverTick.value(), h.canonicalRevision.value(),
            input.container.value(), input.revision.value(), input.capacityWeight, h.chunkIndex, h.chunkCount);
        const auto cell = encodeContainerCell(input.cell);
        const ContainerSchema::Position3 position(input.position.x(), input.position.y(), input.position.z());
        std::vector<ContainerSchema::ItemStack> stacks;
        for (const auto& stack : input.stacks)
            stacks.push_back(encodeStack<ContainerSchema::ItemStack>(stack));
        const auto root = ContainerSchema::CreateReliableContainerInventoryBaselineDirect(
            builder, header, &cell, &position, &stacks);
        ContainerSchema::FinishSizePrefixedReliableContainerInventoryBaselineBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeReliableGroundItemBaseline(const ReliableGroundItemBaseline& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto& h = input.header;
        const auto header = GroundSchema::CreateGroundItemBaselineHeader(builder, h.targetSessionId.value(),
            h.targetSessionGeneration.value(), h.serverTick.value(), h.canonicalRevision.value(), h.chunkIndex,
            h.chunkCount);
        const auto cell = encodeGroundCell(input.cell);
        std::vector<GroundSchema::GroundItem> items;
        for (const auto& item : input.items)
            items.emplace_back(item.stack.stackId.value(), item.stack.prototypeId.value(), item.stack.count,
                item.stack.condition, item.stack.enchantmentCharge,
                item.stack.soulPrototype ? item.stack.soulPrototype->value() : 0,
                static_cast<std::uint8_t>(item.stack.soulPrototype.has_value()), 0, 0, item.position.x(),
                item.position.y(), item.position.z(), item.revision.value());
        const auto root = GroundSchema::CreateReliableGroundItemBaselineDirect(builder, header, &cell, &items);
        GroundSchema::FinishSizePrefixedReliableGroundItemBaselineBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeLatestWinsEquipmentSnapshot(const LatestWinsEquipmentSnapshot& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = EquipmentSchema::CreateEquipmentSnapshotHeader(builder, input.targetSessionId.value(),
            input.targetSessionGeneration.value(), input.serverTick.value(), input.canonicalRevision.value());
        std::vector<EquipmentSchema::EquipmentMember> members;
        for (const auto& member : input.members)
        {
            std::array<std::uint64_t, static_cast<std::size_t>(EquipmentSlot::Count)> slots{};
            for (std::size_t index = 0; index < slots.size(); ++index)
                slots[index] = member.slots[index] ? member.slots[index]->value() : 0;
            members.emplace_back(member.player.value(), slots[0], slots[1], slots[2], slots[3], slots[4], slots[5],
                slots[6], slots[7], slots[8], slots[9], slots[10], slots[11], slots[12], slots[13], slots[14],
                slots[15], slots[16], slots[17], slots[18]);
        }
        const auto root = EquipmentSchema::CreateLatestWinsEquipmentSnapshotDirect(builder, header, &members);
        EquipmentSchema::FinishSizePrefixedLatestWinsEquipmentSnapshotBuffer(builder, root);
        return take(builder);
    }

    std::vector<std::byte> encodeClientInventoryTransactionCommand(const ClientInventoryTransactionCommand& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header = CommandSchema::CreateClientCommandHeader(builder, input.sessionId.value(),
            input.sessionGeneration.value(), input.commandSequence.value(), input.commandId.value(),
            input.observedCanonicalRevision.value());
        const CommandSchema::Position3 origin(
            input.interactionOrigin.x(), input.interactionOrigin.y(), input.interactionOrigin.z());
        const auto root = CommandSchema::CreateClientInventoryTransactionCommand(builder, header,
            static_cast<CommandSchema::InventoryTransactionKind>(input.kind), input.prototypeId.value(), input.count,
            input.expectedInventoryRevision.value(), &origin, input.containerId.has_value(),
            input.containerId ? input.containerId->value() : 0, input.stackId.has_value(),
            input.stackId ? input.stackId->value() : 0, input.slot.has_value(),
            input.slot ? static_cast<std::uint8_t>(*input.slot) : 0, input.expectedContainerRevision.has_value(),
            input.expectedContainerRevision ? input.expectedContainerRevision->value() : 0,
            input.expectedWorldItemRevision.has_value(),
            input.expectedWorldItemRevision ? input.expectedWorldItemRevision->value() : 0);
        CommandSchema::FinishSizePrefixedClientInventoryTransactionCommandBuffer(builder, root);
        return take(builder);
    }

    PlayerInventoryBaselineDecodeResult decodeReliablePlayerInventoryBaseline(std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!PlayerSchema::SizePrefixedReliablePlayerInventoryBaselineBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!PlayerSchema::VerifySizePrefixedReliablePlayerInventoryBaselineBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = PlayerSchema::GetSizePrefixedReliablePlayerInventoryBaseline(bytes);
        if (!root->header())
            return error(Code::MissingRequiredField);
        const auto* h = root->header();
        auto header = decodeHeader(h->target_session_id(), h->target_session_generation(), h->server_tick(),
            h->canonical_revision(), h->chunk_index(), h->chunk_count());
        auto player = strong<PlayerId>(h->player_id());
        auto revision = strong<InventoryRevision>(h->inventory_revision());
        if (const auto* failure = std::get_if<Error>(&header))
            return *failure;
        if (const auto* failure = std::get_if<Error>(&player))
            return *failure;
        if (const auto* failure = std::get_if<Error>(&revision))
            return *failure;
        std::vector<CanonicalItemStack> stacks;
        const auto* encodedStacks = root->stacks();
        const std::size_t count = encodedStacks ? encodedStacks->size() : 0;
        if (count > MaximumInventoryBaselineChunkStacks)
            return error(Code::TooManyEntries, count, MaximumInventoryBaselineChunkStacks);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto current = copyStruct(encodedStacks, index);
            auto stack
                = decodeStack(current.stack_id(), current.prototype_id(), current.count(), current.condition(),
                    current.enchantment_charge(), current.has_soul() != 0, current.soul_prototype_id(), index);
            if (const auto* failure = std::get_if<Error>(&stack))
                return *failure;
            stacks.push_back(std::get<CanonicalItemStack>(std::move(stack)));
        }
        std::vector<EquipmentBinding> equipment;
        if (const auto* encoded = root->equipment())
        {
            for (std::size_t index = 0; index < encoded->size(); ++index)
            {
                const auto current = copyStruct(encoded, index);
                if (current.slot() >= static_cast<std::uint8_t>(EquipmentSlot::Count))
                    return error(Code::InvalidEquipmentSlot, current.slot(), 0, index);
                auto stack = strong<ItemStackId>(current.stack_id(), index);
                if (const auto* failure = std::get_if<Error>(&stack))
                    return *failure;
                equipment.push_back({ static_cast<EquipmentSlot>(current.slot()), *value(stack) });
            }
        }
        return ReliablePlayerInventoryBaseline::create(
            std::get<InventoryBaselineHeader>(header), *value(player), *value(revision), stacks, equipment);
    }

    ContainerInventoryBaselineDecodeResult decodeReliableContainerInventoryBaseline(std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!ContainerSchema::SizePrefixedReliableContainerInventoryBaselineBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!ContainerSchema::VerifySizePrefixedReliableContainerInventoryBaselineBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = ContainerSchema::GetSizePrefixedReliableContainerInventoryBaseline(bytes);
        if (!root->header() || !root->cell() || !root->position())
            return error(Code::MissingRequiredField);
        const auto* h = root->header();
        auto header = decodeHeader(h->target_session_id(), h->target_session_generation(), h->server_tick(),
            h->canonical_revision(), h->chunk_index(), h->chunk_count());
        auto container = strong<ContainerId>(h->container_id());
        auto revision = strong<ContainerRevision>(h->container_revision());
        auto cell = decodeCell(*root->cell());
        const std::array failures{ std::get_if<Error>(&header), std::get_if<Error>(&container),
            std::get_if<Error>(&revision), std::get_if<Error>(&cell) };
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        std::vector<CanonicalItemStack> stacks;
        if (const auto* encoded = root->stacks())
        {
            if (encoded->size() > MaximumInventoryBaselineChunkStacks)
                return error(Code::TooManyEntries, encoded->size(), MaximumInventoryBaselineChunkStacks);
            for (std::size_t index = 0; index < encoded->size(); ++index)
            {
                const auto current = copyStruct(encoded, index);
                auto stack
                    = decodeStack(current.stack_id(), current.prototype_id(), current.count(), current.condition(),
                        current.enchantment_charge(), current.has_soul() != 0, current.soul_prototype_id(), index);
                if (const auto* failure = std::get_if<Error>(&stack))
                    return *failure;
                stacks.push_back(std::get<CanonicalItemStack>(std::move(stack)));
            }
        }
        const auto* position = root->position();
        return ReliableContainerInventoryBaseline::create(std::get<InventoryBaselineHeader>(header), *value(container),
            std::get<CellId>(cell), Position3(position->x(), position->y(), position->z()), *value(revision),
            h->capacity_weight(), stacks);
    }

    GroundItemBaselineDecodeResult decodeReliableGroundItemBaseline(std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!GroundSchema::SizePrefixedReliableGroundItemBaselineBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!GroundSchema::VerifySizePrefixedReliableGroundItemBaselineBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = GroundSchema::GetSizePrefixedReliableGroundItemBaseline(bytes);
        if (!root->header() || !root->cell())
            return error(Code::MissingRequiredField);
        const auto* h = root->header();
        auto header = decodeHeader(h->target_session_id(), h->target_session_generation(), h->server_tick(),
            h->canonical_revision(), h->chunk_index(), h->chunk_count());
        auto cell = decodeCell(*root->cell());
        if (const auto* failure = std::get_if<Error>(&header))
            return *failure;
        if (const auto* failure = std::get_if<Error>(&cell))
            return *failure;
        std::vector<GroundItemInterestMember> items;
        if (const auto* encoded = root->items())
        {
            if (encoded->size() > MaximumGroundItemBaselineChunkItems)
                return error(Code::TooManyEntries, encoded->size(), MaximumGroundItemBaselineChunkItems);
            for (std::size_t index = 0; index < encoded->size(); ++index)
            {
                const auto current = copyStruct(encoded, index);
                auto stack
                    = decodeStack(current.stack_id(), current.prototype_id(), current.count(), current.condition(),
                        current.enchantment_charge(), current.has_soul() != 0, current.soul_prototype_id(), index);
                auto revision = strong<WorldItemRevision>(current.revision(), index);
                if (const auto* failure = std::get_if<Error>(&stack))
                    return *failure;
                if (const auto* failure = std::get_if<Error>(&revision))
                    return *failure;
                items.push_back({ std::get<CanonicalItemStack>(std::move(stack)),
                    Position3(current.x(), current.y(), current.z()), *value(revision) });
            }
        }
        return ReliableGroundItemBaseline::create(
            std::get<InventoryBaselineHeader>(header), std::get<CellId>(cell), items);
    }

    EquipmentSnapshotDecodeResult decodeLatestWinsEquipmentSnapshot(std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, LatestWinsSnapshotMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!EquipmentSchema::SizePrefixedLatestWinsEquipmentSnapshotBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, LatestWinsSnapshotMaximumPayloadBytes);
        if (!EquipmentSchema::VerifySizePrefixedLatestWinsEquipmentSnapshotBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = EquipmentSchema::GetSizePrefixedLatestWinsEquipmentSnapshot(bytes);
        if (!root->header())
            return error(Code::MissingRequiredField);
        const auto* h = root->header();
        auto session = strong<SessionId>(h->target_session_id());
        auto generation = strong<SessionGeneration>(h->target_session_generation());
        auto tick = strong<ServerTick>(h->server_tick());
        auto revision = strong<CanonicalRevision>(h->canonical_revision());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&tick), std::get_if<Error>(&revision) };
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        std::vector<PublicEquipmentMember> members;
        if (const auto* encoded = root->members())
        {
            if (encoded->size() > MaximumEquipmentSnapshotPlayers)
                return error(Code::TooManyEntries, encoded->size(), MaximumEquipmentSnapshotPlayers);
            for (std::size_t index = 0; index < encoded->size(); ++index)
            {
                const auto current = copyStruct(encoded, index);
                auto player = strong<PlayerId>(current.player_id(), index);
                if (const auto* failure = std::get_if<Error>(&player))
                    return *failure;
                PublicEquipmentMember member{ .player = *value(player) };
                const std::array raw{ current.helmet(), current.cuirass(), current.greaves(),
                    current.left_pauldron(), current.right_pauldron(), current.left_gauntlet(),
                    current.right_gauntlet(), current.boots(), current.shirt(), current.pants(), current.skirt(),
                    current.robe(), current.left_ring(), current.right_ring(), current.amulet(), current.belt(),
                    current.carried_right(), current.carried_left(), current.ammunition() };
                for (std::size_t slot = 0; slot < raw.size(); ++slot)
                    if (raw[slot])
                    {
                        auto prototype = strong<ItemPrototypeId>(raw[slot], index);
                        if (const auto* failure = std::get_if<Error>(&prototype))
                            return *failure;
                        member.slots[slot] = *value(prototype);
                    }
                members.push_back(std::move(member));
            }
        }
        return LatestWinsEquipmentSnapshot::create(
            *value(session), *value(generation), *value(tick), *value(revision), members);
    }

    InventoryTransactionCommandDecodeResult decodeClientInventoryTransactionCommand(std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload, ReliableOperationMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!CommandSchema::SizePrefixedClientInventoryTransactionCommandBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload, ReliableOperationMaximumPayloadBytes);
        if (!CommandSchema::VerifySizePrefixedClientInventoryTransactionCommandBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = CommandSchema::GetSizePrefixedClientInventoryTransactionCommand(bytes);
        if (!root->header() || !root->interaction_origin())
            return error(Code::MissingRequiredField);
        const auto* h = root->header();
        auto session = strong<SessionId>(h->session_id());
        auto generation = strong<SessionGeneration>(h->session_generation());
        auto sequence = strong<CommandSequence>(h->command_sequence());
        auto commandId = strong<CommandId>(h->command_id());
        auto canonicalRevision = strong<CanonicalRevision>(h->observed_canonical_revision());
        auto prototype = strong<ItemPrototypeId>(root->prototype_id());
        auto inventoryRevision = strong<InventoryRevision>(root->expected_inventory_revision());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&sequence), std::get_if<Error>(&commandId), std::get_if<Error>(&canonicalRevision),
            std::get_if<Error>(&prototype), std::get_if<Error>(&inventoryRevision) };
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        if (static_cast<std::uint8_t>(root->kind()) > static_cast<std::uint8_t>(InventoryTransactionKind::PickupItem))
            return error(Code::InvalidTransactionKind, static_cast<std::size_t>(root->kind()));
        ClientInventoryTransactionCommand command{ .sessionId = *value(session),
            .sessionGeneration = *value(generation),
            .commandSequence = *value(sequence),
            .commandId = *value(commandId),
            .observedCanonicalRevision = *value(canonicalRevision),
            .kind = static_cast<InventoryTransactionKind>(root->kind()),
            .prototypeId = *value(prototype),
            .count = root->count(),
            .expectedInventoryRevision = *value(inventoryRevision),
            .interactionOrigin = Position3(
                root->interaction_origin()->x(), root->interaction_origin()->y(), root->interaction_origin()->z()) };
        if (root->has_container())
        {
            auto decoded = strong<ContainerId>(root->container_id());
            if (const auto* failure = std::get_if<Error>(&decoded))
                return *failure;
            command.containerId = *value(decoded);
        }
        if (root->has_stack())
        {
            auto decoded = strong<ItemStackId>(root->stack_id());
            if (const auto* failure = std::get_if<Error>(&decoded))
                return *failure;
            command.stackId = *value(decoded);
        }
        if (root->has_slot())
        {
            if (root->slot() >= static_cast<std::uint8_t>(EquipmentSlot::Count))
                return error(Code::InvalidEquipmentSlot, root->slot());
            command.slot = static_cast<EquipmentSlot>(root->slot());
        }
        if (root->has_container_revision())
        {
            auto decoded = strong<ContainerRevision>(root->expected_container_revision());
            if (const auto* failure = std::get_if<Error>(&decoded))
                return *failure;
            command.expectedContainerRevision = *value(decoded);
        }
        if (root->has_world_item_revision())
        {
            auto decoded = strong<WorldItemRevision>(root->expected_world_item_revision());
            if (const auto* failure = std::get_if<Error>(&decoded))
                return *failure;
            command.expectedWorldItemRevision = *value(decoded);
        }
        if (const auto failure = validateCommandShape(command))
            return *failure;
        return command;
    }
}
