#ifndef TES3MP_INVENTORY_REPLICATION_HPP
#define TES3MP_INVENTORY_REPLICATION_HPP

#include "native_door.hpp"

#include "inventory_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumInventoryBaselineChunkStacks = 256;
    inline constexpr std::size_t MaximumGroundItemBaselineChunkItems = 192;
    inline constexpr std::size_t MaximumEquipmentSnapshotPlayers = 256;
    inline constexpr std::size_t MaximumEquipmentSnapshotActors = 128;

    enum class InventoryReplicationDecodeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        MissingRequiredField,
        InvalidStrongValue,
        TooManyEntries,
        EntriesNotStrictlySorted,
        InvalidChunk,
        InvalidCellKind,
        InvalidInteriorGrid,
        InvalidEquipmentSlot,
        InvalidTransactionKind,
        InvalidCommandShape,
    };

    struct InventoryReplicationDecodeError
    {
        InventoryReplicationDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::size_t index = 0;
        friend constexpr bool operator==(InventoryReplicationDecodeError, InventoryReplicationDecodeError) noexcept
            = default;
    };

    struct InventoryBaselineHeader
    {
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        ServerTick serverTick;
        CanonicalRevision canonicalRevision;
        std::uint32_t chunkIndex = 0;
        std::uint32_t chunkCount = 1;
        friend constexpr bool operator==(InventoryBaselineHeader, InventoryBaselineHeader) noexcept = default;
    };

    struct EquipmentBinding
    {
        EquipmentSlot slot = EquipmentSlot::Helmet;
        ItemStackId stackId;
        friend constexpr bool operator==(EquipmentBinding, EquipmentBinding) noexcept = default;
    };

    struct ReliablePlayerInventoryBaseline
    {
        InventoryBaselineHeader header;
        PlayerId player;
        InventoryRevision revision;
        std::vector<CanonicalItemStack> stacks;
        std::vector<EquipmentBinding> equipment;

        static std::variant<ReliablePlayerInventoryBaseline, InventoryReplicationDecodeError> create(
            InventoryBaselineHeader header, PlayerId player, InventoryRevision revision,
            std::span<const CanonicalItemStack> stacks, std::span<const EquipmentBinding> equipment);
        friend bool operator==(const ReliablePlayerInventoryBaseline&, const ReliablePlayerInventoryBaseline&) noexcept
            = default;
    };

    struct ReliableContainerInventoryBaseline
    {
        InventoryBaselineHeader header;
        ContainerId container;
        CellId cell;
        Position3 position;
        ContainerRevision revision;
        std::uint32_t capacityWeight = 0;
        std::vector<CanonicalItemStack> stacks;
        // Optional equipment for actor-owned shared storage. Ordinary containers
        // retain an empty table and their original wire representation.
        std::vector<EquipmentBinding> equipment;

        static std::variant<ReliableContainerInventoryBaseline, InventoryReplicationDecodeError> create(
            InventoryBaselineHeader header, ContainerId container, CellId cell, Position3 position,
            ContainerRevision revision, std::uint32_t capacityWeight, std::span<const CanonicalItemStack> stacks,
            std::span<const EquipmentBinding> equipment = {});
        friend bool operator==(
            const ReliableContainerInventoryBaseline&, const ReliableContainerInventoryBaseline&) noexcept = default;
    };

    struct GroundItemInterestMember
    {
        CanonicalItemStack stack;
        Position3 position;
        WorldItemRevision revision;
        friend bool operator==(const GroundItemInterestMember&, const GroundItemInterestMember&) noexcept = default;
    };

    struct GroundItemPresentation
    {
        ItemStackId stack;
        std::array<float, 3> rotation{};
        float scale = 1;
        friend bool operator==(const GroundItemPresentation&, const GroundItemPresentation&) noexcept = default;
    };
    struct NativeActorSpawn
    {
        uint64_t placement = 0, record = 0; // Zero record is an authoritative chance-none outcome.
        friend bool operator==(const NativeActorSpawn&, const NativeActorSpawn&) noexcept = default;
    };
    struct ReliableGroundItemBaseline
    {
        InventoryBaselineHeader header;
        CellId cell;
        std::vector<GroundItemInterestMember> items;
        // Native world domain: suppress these content placements even when the
        // active item list is empty. Bounded, complete, single-chunk snapshots.
        std::vector<uint64_t> nativePlacements;
        std::vector<GroundItemPresentation> presentation;
        bool nativeWorld = false;
        std::optional<NativeDoorSnapshot> door;
        // Immutable teleport activators in this cell; destinations stay server-side.
        std::vector<uint64_t> teleportDoors;
        std::vector<NativeDoorSnapshot> doors;
        // One coherent exterior neighborhood; children are single-cell leaves.
        std::vector<ReliableGroundItemBaseline> neighbors;
        std::vector<NativeActorSpawn> actorSpawns;

        static std::variant<ReliableGroundItemBaseline, InventoryReplicationDecodeError> create(
            InventoryBaselineHeader header, CellId cell, std::span<const GroundItemInterestMember> items,
            std::span<const uint64_t> nativePlacements = {}, std::span<const GroundItemPresentation> presentation = {},
            bool nativeWorld = false, std::optional<NativeDoorSnapshot> door = {},
            std::span<const uint64_t> teleportDoors = {}, std::span<const NativeDoorSnapshot> doors = {},
            std::span<const ReliableGroundItemBaseline> neighbors = {}, std::span<const NativeActorSpawn> actorSpawns = {});
        friend bool operator==(const ReliableGroundItemBaseline&, const ReliableGroundItemBaseline&) noexcept = default;
    };

    struct PublicEquipmentMember
    {
        PlayerId player;
        std::array<std::optional<ItemPrototypeId>, static_cast<std::size_t>(EquipmentSlot::Count)> slots{};
        friend constexpr bool operator==(const PublicEquipmentMember&, const PublicEquipmentMember&) noexcept = default;
    };

    // Placed actor identity shares the inventory-owner namespace, but this
    // appearance-only entry grants no access and contains no stack identities.
    struct PublicActorEquipmentMember
    {
        ContainerId actor;
        std::array<std::optional<ItemPrototypeId>, static_cast<std::size_t>(EquipmentSlot::Count)> slots{};
        friend constexpr bool operator==(const PublicActorEquipmentMember&, const PublicActorEquipmentMember&) noexcept = default;
    };

    struct LatestWinsEquipmentSnapshot
    {
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        ServerTick serverTick;
        CanonicalRevision canonicalRevision;
        std::vector<PublicEquipmentMember> members;
        std::vector<PublicActorEquipmentMember> actors;

        static std::variant<LatestWinsEquipmentSnapshot, InventoryReplicationDecodeError> create(SessionId target,
            SessionGeneration generation, ServerTick tick, CanonicalRevision revision,
            std::span<const PublicEquipmentMember> members, std::span<const PublicActorEquipmentMember> actors = {});
        friend bool operator==(const LatestWinsEquipmentSnapshot&, const LatestWinsEquipmentSnapshot&) noexcept
            = default;
    };

    struct ClientInventoryTransactionCommand
    {
        SessionId sessionId;
        SessionGeneration sessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        CanonicalRevision observedCanonicalRevision;
        InventoryTransactionKind kind = InventoryTransactionKind::TakeFromContainer;
        std::optional<ContainerId> containerId;
        ItemPrototypeId prototypeId;
        std::optional<ItemStackId> stackId;
        std::uint32_t count = 1;
        std::optional<EquipmentSlot> slot;
        InventoryRevision expectedInventoryRevision;
        std::optional<ContainerRevision> expectedContainerRevision;
        std::optional<WorldItemRevision> expectedWorldItemRevision;
        Position3 interactionOrigin;
        std::optional<DropPlacementView> placement;
        friend bool operator==(
            const ClientInventoryTransactionCommand&, const ClientInventoryTransactionCommand&) noexcept = default;
    };

    using PlayerInventoryBaselineDecodeResult
        = std::variant<ReliablePlayerInventoryBaseline, InventoryReplicationDecodeError>;
    using ContainerInventoryBaselineDecodeResult
        = std::variant<ReliableContainerInventoryBaseline, InventoryReplicationDecodeError>;
    using GroundItemBaselineDecodeResult = std::variant<ReliableGroundItemBaseline, InventoryReplicationDecodeError>;
    using EquipmentSnapshotDecodeResult = std::variant<LatestWinsEquipmentSnapshot, InventoryReplicationDecodeError>;
    using InventoryTransactionCommandDecodeResult
        = std::variant<ClientInventoryTransactionCommand, InventoryReplicationDecodeError>;

    std::vector<std::byte> encodeReliablePlayerInventoryBaseline(const ReliablePlayerInventoryBaseline& value);
    std::vector<std::byte> encodeReliableContainerInventoryBaseline(const ReliableContainerInventoryBaseline& value);
    std::vector<std::byte> encodeReliableGroundItemBaseline(const ReliableGroundItemBaseline& value);
    std::vector<std::byte> encodeLatestWinsEquipmentSnapshot(const LatestWinsEquipmentSnapshot& value);
    std::vector<std::byte> encodeClientInventoryTransactionCommand(const ClientInventoryTransactionCommand& value);

    PlayerInventoryBaselineDecodeResult decodeReliablePlayerInventoryBaseline(std::span<const std::byte> payload);
    ContainerInventoryBaselineDecodeResult decodeReliableContainerInventoryBaseline(std::span<const std::byte> payload);
    GroundItemBaselineDecodeResult decodeReliableGroundItemBaseline(std::span<const std::byte> payload);
    EquipmentSnapshotDecodeResult decodeLatestWinsEquipmentSnapshot(std::span<const std::byte> payload);
    InventoryTransactionCommandDecodeResult decodeClientInventoryTransactionCommand(std::span<const std::byte> payload);
}

#endif
