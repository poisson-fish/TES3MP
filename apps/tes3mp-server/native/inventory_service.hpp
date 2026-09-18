#ifndef TES3MP_NATIVE_INVENTORY_SERVICE_HPP
#define TES3MP_NATIVE_INVENTORY_SERVICE_HPP

#include "equipment_runtime.hpp"
#include "../inventory_command_binding.hpp"
#include "../inventory_interest_projection.hpp"
#include "../native_inventory_service.hpp"

namespace TES3MP::Native
{
    // Trusted composition for the loaded placed inventory domain. Bindings
    // come from server startup, never first arrival/client fields.
    // Production resolves placements from OpenMW; synthetic tests may use
    // a base-only container. Item identities derive from the loaded records.
    struct InventoryContainerBinding
    {
        ContainerId mId;
        CellId mCell;
        Position3 mPosition;
        ESM::RefId mBase;
        std::optional<ESM::CellRef> mPlacement;
    };
    struct InventoryServiceBinding
    {
        std::array<PlayerId, 2> mPlayers;
        std::optional<ItemPrototypeId> mShirt; // Legacy seed/projection override only.
        std::array<EquipmentActorBinding, 2> mActors;
        std::array<unsigned char, 32> mContent;
        std::vector<InventoryContainerBinding> mContainers;
        int mLootLevel = 1;
        uint32_t mLootSeed = 0;
        struct WorldItems
        {
            CellId mCell;
            std::vector<std::pair<uint64_t, ESM::CellRef>> mPlacements;
            // Trusted composition. The query reads the committed world and never mutates it.
            std::function<ESM::Position(const ESM::Position&, const MWWorld::Ptr&,
                const DropPlacementView&, std::span<const ESM::ObjectState>)> mPlacement;
        };
        std::optional<WorldItems> mWorldItems;
    };

    // One long-lived engine service group for all shared inventories. Loaded
    // content/readers outlive this object; registry/scripts precede the runtime
    // and die after it. There is no CanonicalInventoryWorld or shadow writer.
    class InventoryService final : public ServerApp::NativeInventoryService
    {
        const InventoryServiceBinding mBinding;
        std::map<ESM::RefId, ItemPrototypeId> mItemIds;
        MWWorld::WorldModel mWorld;
        MWWorld::LocalScripts mScripts;
        EquipmentRuntime mRuntime;
        EquipmentBytes mImage;
        class Transaction;
        class EquipmentTransaction;
        class WorldTransaction;
        size_t actor(PlayerId player) const;
        size_t container(std::optional<ContainerId> id) const;
        void validate(const CanonicalServerState& players, const ServerApp::InventoryCommandBinding& command) const;
        void retireCommittedEffects() noexcept;

    public:
        class PreparedCommand
        {
            friend class InventoryService;
            ServerApp::InventoryCommandBinding mBinding;
            EquipmentRuntime::PreparedTransfer mTransfer;
            PreparedCommand(ServerApp::InventoryCommandBinding binding, EquipmentRuntime::PreparedTransfer transfer)
                : mBinding(std::move(binding)), mTransfer(std::move(transfer)) {}
        public:
            const InventoryTransferSuccess& candidate() const { return mTransfer.candidate(); }
            std::span<const char> image() const { return mTransfer.image(); }
        };
        InventoryService(MWWorld::ESMStore& content, ESM::ReadersCache& readers,
            InventoryServiceBinding binding, bool recovering = false);
        InventoryService(const InventoryService&) = delete;
        InventoryService& operator=(const InventoryService&) = delete;
        PreparedCommand prepare(const CanonicalServerState& players, const ServerApp::InventoryCommandBinding& command);
        PersistenceResult commit(const CanonicalServerState& players, PreparedCommand& command,
            EquipmentSessionCommitter& durability, std::unique_ptr<const InventoryTransferSuccess>& success,
            EquipmentBytes& bytes);
        FileReadResult recover(const std::filesystem::path& path, std::span<const ESM::RefId> references,
            EquipmentBytes& bytes, FileFaults& faults);
        void recover(std::span<const std::byte> image, std::span<const ESM::RefId> references);
        std::unique_ptr<PreparedNativeInventory> prepareInventory(
            const CanonicalServerState& players, const ServerCommandProposal& command) override;
        std::span<const std::byte> inventoryImage() const noexcept override;
        std::optional<ServerApp::InventoryInterestDelivery> projectInventory(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedNativeInventory* candidate = nullptr) const override;
        // Direct projection to the existing owned wire values. No retained or
        // writable mirror. Candidate baselines stay staged until durable commit.
        std::optional<ServerApp::InventoryInterestDelivery> project(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedCommand* candidate = nullptr,
            const EquipmentRuntime::PreparedEquipment* equipment = nullptr,
            const EquipmentRuntime::PreparedWorldTransfer* world = nullptr) const;
    };
}
#endif
