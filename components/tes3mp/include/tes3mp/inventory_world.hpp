#ifndef TES3MP_INVENTORY_WORLD_HPP
#define TES3MP_INVENTORY_WORLD_HPP

#include "canonical_state.hpp"
#include "item_catalog.hpp"
#include "spatial_types.hpp"
#include "value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumPlayerInventoryStacks = 1024;
    inline constexpr std::size_t MaximumContainerStacks = 512;
    inline constexpr std::size_t MaximumInventoryPlayers = 256;
    inline constexpr std::size_t MaximumInventoryContainers = 65536;
    inline constexpr std::size_t MaximumWorldItemStacks = 65536;
    inline constexpr std::uint32_t MaximumTransferCount = 1000000;

    struct CanonicalItemStack
    {
        ItemStackId stackId;
        ItemPrototypeId prototypeId;
        std::uint32_t count = 1;
        std::uint32_t condition = 0;
        std::uint32_t enchantmentCharge = 0;
        std::optional<ActorPrototypeId> soulPrototype = std::nullopt;

        bool canStackWith(const CanonicalItemStack& other, const ItemPrototypeCatalog& catalog) const noexcept;

        friend bool operator==(const CanonicalItemStack&, const CanonicalItemStack&) noexcept = default;
    };

    struct CanonicalPlayerInventoryState
    {
        PlayerId player;
        InventoryRevision revision = InventoryRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();
        std::vector<CanonicalItemStack> stacks;
        std::array<std::optional<ItemStackId>, static_cast<std::size_t>(EquipmentSlot::Count)> equipment{};

        const CanonicalItemStack* findStack(ItemStackId id) const noexcept;
        CanonicalItemStack* findStack(ItemStackId id) noexcept;
        std::uint64_t totalWeight(const ItemPrototypeCatalog& catalog) const noexcept;
        std::vector<KeyPrototypeId> collectKeys(const ItemPrototypeCatalog& catalog) const noexcept;

        friend bool operator==(const CanonicalPlayerInventoryState&, const CanonicalPlayerInventoryState&) noexcept
            = default;
    };

    struct CanonicalContainerInventoryState
    {
        ContainerId containerId;
        CellId cell;
        Position3 position;
        ContainerRevision revision = ContainerRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();
        std::uint32_t capacityWeight = 0; // 0 = unbounded
        std::vector<CanonicalItemStack> stacks;

        const CanonicalItemStack* findStack(ItemStackId id) const noexcept;
        CanonicalItemStack* findStack(ItemStackId id) noexcept;
        std::uint64_t totalWeight(const ItemPrototypeCatalog& catalog) const noexcept;

        friend bool operator==(
            const CanonicalContainerInventoryState&, const CanonicalContainerInventoryState&) noexcept
            = default;
    };

    struct CanonicalWorldItemState
    {
        CanonicalItemStack stack;
        CellId cell;
        Position3 position;
        WorldItemRevision revision = WorldItemRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();

        friend bool operator==(const CanonicalWorldItemState&, const CanonicalWorldItemState&) noexcept = default;
    };

    enum class InventoryTransactionKind : std::uint8_t
    {
        TakeFromContainer,
        PutIntoContainer,
        EquipItem,
        UnequipItem,
        DropItem,
        PickupItem,
    };

    struct InventoryTransactionCommand
    {
        PlayerId player;
        InventoryTransactionKind kind = InventoryTransactionKind::TakeFromContainer;
        std::optional<ContainerId> containerId = std::nullopt;
        ItemPrototypeId prototypeId;
        std::optional<ItemStackId> stackId = std::nullopt;
        std::uint32_t count = 1;
        std::optional<EquipmentSlot> slot = std::nullopt;
        InventoryRevision expectedInventoryRevision = InventoryRevision::initial();
        std::optional<ContainerRevision> expectedContainerRevision = std::nullopt;
        std::optional<WorldItemRevision> expectedWorldItemRevision = std::nullopt;
        Position3 interactionOrigin;

        friend bool operator==(const InventoryTransactionCommand&, const InventoryTransactionCommand&) noexcept
            = default;
    };

    struct InventoryValidationContext
    {
        std::uint32_t maxReach = 384;
    };

    enum class InventoryTransactionResultCode : std::uint8_t
    {
        Success,
        PlayerNotFound,
        ContainerNotFound,
        WorldItemNotFound,
        MissingExpectedRevision,
        CellMismatch,
        PlayerOutOfReach,
        OriginOutOfReach,
        ItemNotFound,
        InsufficientCount,
        StaleInventoryRevision,
        StaleContainerRevision,
        StaleWorldItemRevision,
        SlotNotCompatible,
        EquipmentQuantityExceeded,
        ItemEquipped,
        PlayerInventoryFull,
        ContainerCapacityExceeded,
        WorldItemCapacityExceeded,
        ArithmeticOverflow,
        PrototypeNotFound,
        TickRegression,
        RevisionExhausted,
        StackIdExhausted,
        InternalError,
    };

    struct InventoryTransactionOutcome
    {
        InventoryTransactionResultCode code = InventoryTransactionResultCode::InternalError;
        PlayerId player;
        InventoryRevision newInventoryRevision = InventoryRevision::initial();
        std::optional<ContainerId> containerId = std::nullopt;
        std::optional<ContainerRevision> newContainerRevision = std::nullopt;
        std::optional<WorldItemRevision> newWorldItemRevision = std::nullopt;
        std::optional<ItemStackId> affectedStackId = std::nullopt;

        friend bool operator==(const InventoryTransactionOutcome&, const InventoryTransactionOutcome&) noexcept
            = default;
    };

    class CanonicalInventoryWorld
    {
    public:
        static std::optional<CanonicalInventoryWorld> create(const ContentManifest& manifest,
            const ItemPrototypeCatalog& catalog, std::span<const CanonicalPlayerInventoryState> players,
            std::span<const CanonicalContainerInventoryState> containers,
            std::span<const CanonicalWorldItemState> worldItems = {},
            std::optional<ItemStackId> nextItemStackId = std::nullopt) noexcept;

        constexpr ContentManifestId contentManifestId() const noexcept { return mManifest.id(); }
        std::span<const CanonicalPlayerInventoryState> players() const noexcept { return mPlayers; }
        std::span<const CanonicalContainerInventoryState> containers() const noexcept { return mContainers; }
        std::span<const CanonicalWorldItemState> worldItems() const noexcept { return mWorldItems; }

        const CanonicalPlayerInventoryState* findPlayer(PlayerId player) const noexcept;

        const CanonicalContainerInventoryState* findContainer(ContainerId container) const noexcept;

        const CanonicalWorldItemState* findWorldItem(ItemStackId stack) const noexcept;

        std::vector<KeyPrototypeId> collectVerifiedKeys(PlayerId player) const noexcept;

        bool ensurePlayer(PlayerId player) noexcept;
        bool ensureContainer(
            ContainerId container, CellId cell, Position3 position, std::uint32_t capacityWeight = 0) noexcept;

        friend bool operator==(const CanonicalInventoryWorld&, const CanonicalInventoryWorld&) noexcept = default;

    private:
        CanonicalInventoryWorld(ContentManifest manifest, ItemPrototypeCatalog catalog,
            std::vector<CanonicalPlayerInventoryState> players,
            std::vector<CanonicalContainerInventoryState> containers, std::vector<CanonicalWorldItemState> worldItems,
            std::optional<ItemStackId> nextItemStackId) noexcept
            : mManifest(std::move(manifest))
            , mCatalog(std::move(catalog))
            , mPlayers(std::move(players))
            , mContainers(std::move(containers))
            , mWorldItems(std::move(worldItems))
            , mNextItemStackId(nextItemStackId)
        {
        }

        std::optional<ItemStackId> allocateNextStackId() noexcept;
        CanonicalPlayerInventoryState* findMutablePlayer(PlayerId player) noexcept;
        CanonicalContainerInventoryState* findMutableContainer(ContainerId container) noexcept;

        ContentManifest mManifest;
        ItemPrototypeCatalog mCatalog;
        std::vector<CanonicalPlayerInventoryState> mPlayers;
        std::vector<CanonicalContainerInventoryState> mContainers;
        std::vector<CanonicalWorldItemState> mWorldItems;
        std::optional<ItemStackId> mNextItemStackId;

        friend InventoryTransactionOutcome applyInventoryTransaction(CanonicalInventoryWorld&,
            const CanonicalServerState&, const InventoryTransactionCommand&, const InventoryValidationContext&,
            ServerTick) noexcept;
    };

    InventoryTransactionOutcome applyInventoryTransaction(CanonicalInventoryWorld& world,
        const CanonicalServerState& players, const InventoryTransactionCommand& command,
        const InventoryValidationContext& context, ServerTick tick) noexcept;
}

#endif
