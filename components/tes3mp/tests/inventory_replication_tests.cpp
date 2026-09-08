#include <tes3mp/inventory_replication.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/protocol_handshake.hpp>

#include <cassert>
#include <span>
#include <vector>

namespace
{
    using namespace TES3MP;
    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    InventoryBaselineHeader header()
    {
        return { id<SessionId>(1), SessionGeneration::initial(), id<ServerTick>(2), id<CanonicalRevision>(3), 0, 1 };
    }
}

int main()
{
    using namespace TES3MP;
    const CanonicalItemStack stack{ id<ItemStackId>(10), id<ItemPrototypeId>(20), 2, 30, 40, id<ActorPrototypeId>(50) };
    const EquipmentBinding binding{ EquipmentSlot::CarriedRight, stack.stackId };
    auto player = ReliablePlayerInventoryBaseline::create(
        header(), id<PlayerId>(1), id<InventoryRevision>(4), std::span(&stack, 1), std::span(&binding, 1));
    assert(std::holds_alternative<ReliablePlayerInventoryBaseline>(player));
    const auto playerBytes = encodeReliablePlayerInventoryBaseline(std::get<ReliablePlayerInventoryBaseline>(player));
    assert(decodeReliablePlayerInventoryBaseline(playerBytes) == player);

    const auto cell = CellId::interior(id<CellSpaceId>(7));
    auto container = ReliableContainerInventoryBaseline::create(
        header(), id<ContainerId>(2), cell, Position3(1, 2, 3), id<ContainerRevision>(5), 100, std::span(&stack, 1));
    assert(std::holds_alternative<ReliableContainerInventoryBaseline>(container));
    const auto containerBytes
        = encodeReliableContainerInventoryBaseline(std::get<ReliableContainerInventoryBaseline>(container));
    assert(decodeReliableContainerInventoryBaseline(containerBytes) == container);

    const GroundItemInterestMember ground{ stack, Position3(4, 5, 6), id<WorldItemRevision>(6) };
    auto groundBaseline = ReliableGroundItemBaseline::create(header(), cell, std::span(&ground, 1));
    assert(std::holds_alternative<ReliableGroundItemBaseline>(groundBaseline));
    const auto groundBytes = encodeReliableGroundItemBaseline(std::get<ReliableGroundItemBaseline>(groundBaseline));
    assert(decodeReliableGroundItemBaseline(groundBytes) == groundBaseline);

    PublicEquipmentMember member{ .player = id<PlayerId>(1) };
    member.slots[static_cast<std::size_t>(EquipmentSlot::CarriedRight)] = stack.prototypeId;
    auto equipment = LatestWinsEquipmentSnapshot::create(id<SessionId>(1), SessionGeneration::initial(),
        id<ServerTick>(2), id<CanonicalRevision>(3), std::span(&member, 1));
    assert(std::holds_alternative<LatestWinsEquipmentSnapshot>(equipment));
    const auto equipmentBytes = encodeLatestWinsEquipmentSnapshot(std::get<LatestWinsEquipmentSnapshot>(equipment));
    assert(decodeLatestWinsEquipmentSnapshot(equipmentBytes) == equipment);

    std::vector<CanonicalItemStack> maximumStacks;
    maximumStacks.reserve(MaximumInventoryBaselineChunkStacks);
    for (std::size_t index = 0; index < MaximumInventoryBaselineChunkStacks; ++index)
        maximumStacks.push_back({ id<ItemStackId>(1000 + index), stack.prototypeId, 1, 30, 40, std::nullopt });
    auto maximumPlayer = ReliablePlayerInventoryBaseline::create(
        header(), id<PlayerId>(1), id<InventoryRevision>(4), maximumStacks, {});
    assert(std::holds_alternative<ReliablePlayerInventoryBaseline>(maximumPlayer));
    assert(encodeReliablePlayerInventoryBaseline(std::get<ReliablePlayerInventoryBaseline>(maximumPlayer)).size()
        <= ReliableOperationMaximumPayloadBytes);

    std::vector<GroundItemInterestMember> maximumGround;
    maximumGround.reserve(MaximumGroundItemBaselineChunkItems);
    for (std::size_t index = 0; index < MaximumGroundItemBaselineChunkItems; ++index)
        maximumGround.push_back({ maximumStacks[index], Position3(4, 5, 6), id<WorldItemRevision>(6) });
    auto maximumGroundBaseline = ReliableGroundItemBaseline::create(header(), cell, maximumGround);
    assert(std::holds_alternative<ReliableGroundItemBaseline>(maximumGroundBaseline));
    assert(encodeReliableGroundItemBaseline(std::get<ReliableGroundItemBaseline>(maximumGroundBaseline)).size()
        <= ReliableOperationMaximumPayloadBytes);

    const ClientInventoryTransactionCommand command{ .sessionId = id<SessionId>(1),
        .sessionGeneration = SessionGeneration::initial(),
        .commandSequence = id<CommandSequence>(1),
        .commandId = id<CommandId>(2),
        .observedCanonicalRevision = id<CanonicalRevision>(3),
        .kind = InventoryTransactionKind::TakeFromContainer,
        .containerId = id<ContainerId>(2),
        .prototypeId = stack.prototypeId,
        .stackId = stack.stackId,
        .count = 1,
        .expectedInventoryRevision = id<InventoryRevision>(4),
        .expectedContainerRevision = id<ContainerRevision>(5),
        .interactionOrigin = Position3(1, 2, 3) };
    const auto commandBytes = encodeClientInventoryTransactionCommand(command);
    assert(decodeClientInventoryTransactionCommand(commandBytes) == InventoryTransactionCommandDecodeResult(command));

    for (std::size_t size = 0; size < playerBytes.size(); ++size)
        assert(std::holds_alternative<InventoryReplicationDecodeError>(
            decodeReliablePlayerInventoryBaseline(std::span(playerBytes).first(size))));
    assert(messageDescriptor(MessageKind::ReliablePlayerInventoryBaseline)->messageClass
        == MessageClass::ReliableOperation);
    assert(
        messageDescriptor(MessageKind::LatestWinsEquipmentSnapshot)->messageClass == MessageClass::LatestWinsSnapshot);
    assert(inventoryReplicationCapability().value() == InventoryReplicationCapabilityValue);
}
