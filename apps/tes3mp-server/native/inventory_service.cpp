#include "inventory_service.hpp"
#include "actor_inventory.hpp"
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containeradd.hpp>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <limits>
#include <set>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        std::string identity(const InventoryServiceBinding& binding, const MWWorld::ESMStore& content)
        {
            if (binding.mPlayers[0] == binding.mPlayers[1] || binding.mContainers.empty()
                || binding.mContainers.size() > MaxEquipmentContainers
                || binding.mActors[0].mBaseInventory != binding.mActors[1].mBaseInventory)
                throw std::invalid_argument("Native inventory requires distinct trusted players, bounded containers and one initialization mode");
            std::set<ContainerId> ids;
            for (const auto& container : binding.mContainers)
                if (container.mBase.empty() || !ids.insert(container.mId).second)
                    throw std::invalid_argument("Native container base or identity invalid");
            if (binding.mActors[0].mBaseInventory)
            {
                if (binding.mShirt)
                    throw std::invalid_argument("Base actor inventories cannot override an item identity");
                return "native-inventory-2/" + std::to_string(binding.mPlayers[0].value()) + "/"
                    + std::to_string(binding.mPlayers[1].value()) + "/"
                    + std::to_string(binding.mContainers.front().mId.value());
            }
            if (!binding.mShirt || binding.mActors[0].mShirt != binding.mActors[1].mShirt)
                throw std::invalid_argument("Legacy native inventory requires one seed shirt");
            const auto* shirt = content.get<ESM::Clothing>().find(binding.mActors[0].mShirt);
            if (!shirt->mScript.empty() || !shirt->mEnchant.empty())
                throw std::invalid_argument("Native inventory wire projection supports the plain shirt only");
            return "native-inventory-1/" + std::to_string(binding.mPlayers[0].value()) + "/"
                + std::to_string(binding.mPlayers[1].value()) + "/" + std::to_string(binding.mShirt->value())
                + "/" + std::to_string(binding.mContainers.front().mId.value());
        }
        std::vector<EquipmentContainerBinding> containers(const InventoryServiceBinding& binding)
        {
            if (binding.mContainers.size() > MaxEquipmentContainers)
                throw std::invalid_argument("Native container budget exceeded");
            std::vector<EquipmentContainerBinding> result;
            for (const auto& shared : binding.mContainers) result.push_back({shared.mBase, shared.mPlacement});
            return result;
        }
        ItemStackId wireId(ESM::RefNum id)
        {
            return ItemStackId::fromValue((uint64_t(std::bit_cast<uint32_t>(id.mContentFile)) << 32) | id.mIndex).value();
        }
        InventoryInstanceId nativeId(ItemStackId id)
        {
            return { uint32_t(id.value()), std::bit_cast<int32_t>(uint32_t(id.value() >> 32)) };
        }
        std::vector<CanonicalItemStack> stacks(const MWWorld::PlainEquipmentValues& values,
            const std::map<ESM::RefId, ItemPrototypeId>& items, const MWWorld::ESMStore& content)
        {
            std::vector<CanonicalItemStack> result;
            result.reserve(values.mObjects.size());
            for (const auto& object : values.mObjects)
            {
                const auto count = object.mRef.mCount;
                if (count == 0) continue; // Dormant IDs remain engine-owned and durable.
                if (count == std::numeric_limits<int32_t>::min())
                    throw std::invalid_argument("Native inventory fields cannot be projected losslessly");
                const auto id = items.at(object.mRef.mRefID);
                MWWorld::ManualRef ref(content, object.mRef.mRefID);
                const auto& ptr = ref.getPtr();
                ptr.getCellRef() = MWWorld::CellRef(object.mRef);
                const auto& cls = ptr.getClass();
                const uint32_t condition = cls.hasItemHealth(ptr) ? uint32_t(cls.getItemHealth(ptr)) : 0;
                const auto charge = ptr.getCellRef().getEnchantmentCharge();
                std::optional<ActorPrototypeId> soul;
                if (!object.mRef.mSoul.empty())
                {
                    content.get<ESM::Creature>().find(object.mRef.mSoul);
                    soul = ActorPrototypeId::fromValue(MWWorld::inventoryRecordId(object.mRef.mSoul)).value();
                }
                // Native record IDs carry the exact engine charge bit patterns,
                // including the untouched -1 sentinel and fractional light time.
                const bool native = id.value() == MWWorld::inventoryRecordId(object.mRef.mRefID);
                result.push_back({ wireId(object.mRef.mRefNum), id, uint32_t(std::abs(count)),
                    native ? std::bit_cast<uint32_t>(object.mRef.mChargeInt) : condition,
                    native ? std::bit_cast<uint32_t>(charge) : 0, soul });
            }
            std::ranges::sort(result, {}, &CanonicalItemStack::stackId);
            return result;
        }
    }

    InventoryService::InventoryService(MWWorld::ESMStore& content, ESM::ReadersCache& readers,
        InventoryServiceBinding binding, bool recovering)
        : mBinding(std::move(binding)), mWorld(content, readers, 1), mScripts(content),
          mRuntime(content, mWorld, mScripts, identity(mBinding, content), mBinding.mContent, mBinding.mActors,
              {}, nullptr, recovering ? std::optional<size_t>{ 2 } : std::nullopt, true, containers(mBinding), mBinding.mLootLevel, mBinding.mLootSeed)
    {
        for (const auto& [id, record] : MWWorld::inventoryRecords(content))
            mItemIds.emplace(record, ItemPrototypeId::fromValue(id).value());
        // Synthetic service fixtures may retain their explicit shirt ID.
        if (mBinding.mShirt) mItemIds.insert_or_assign(mBinding.mActors[0].mShirt, *mBinding.mShirt);
        if (!recovering)
            mRuntime.encodeSession({{mRuntime.installedValues(0), mRuntime.installedValues(1)},
                mWorld.getPtrRegistryRevision()}, mImage);
        if (mImage.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory image exceeds canonical record budget");
    }

    size_t InventoryService::actor(PlayerId player) const
    {
        for (size_t i = 0; i < mBinding.mPlayers.size(); ++i)
            if (mBinding.mPlayers[i] == player) return i;
        throw std::invalid_argument("Authenticated player has no native actor binding");
    }

    size_t InventoryService::container(std::optional<ContainerId> id) const
    {
        for (size_t i = 0; i < mBinding.mContainers.size(); ++i)
            if (mBinding.mContainers[i].mId == id) return i;
        throw std::invalid_argument("Container is outside the native loaded domain");
    }

    void InventoryService::validate(const CanonicalServerState& players, const ServerApp::InventoryCommandBinding& bound) const
    {
        // Desktop positions are fixed-point quanta (1024 per OpenMW unit).
        // Preserve the bounded 384-unit interaction radius in that wire domain.
        constexpr std::uint32_t ReachQuanta = 384 * 1024;
        if (!bound.current(players)) throw std::invalid_argument("Inventory session binding is no longer current");
        (void)actor(bound.player());
        const auto& command = bound.transaction();
        const auto item = command.stackId ? mWorld.getPtr({uint32_t(command.stackId->value()),
            std::bit_cast<int32_t>(uint32_t(command.stackId->value() >> 32))}) : MWWorld::Ptr{};
        const auto prototype = item.isEmpty() ? mItemIds.end() : mItemIds.find(item.getCellRef().getRefId());
        if (prototype == mItemIds.end() || prototype->second != command.prototypeId)
            throw std::invalid_argument("Native stack and item prototype identities disagree");
        if (command.kind == InventoryTransactionKind::EquipItem || command.kind == InventoryTransactionKind::UnequipItem)
        {
            if (command.player != bound.player() || !command.slot || command.count != 1
                || static_cast<unsigned>(*command.slot) >= static_cast<unsigned>(EquipmentSlot::Count)
                || command.containerId || command.expectedContainerRevision || command.expectedWorldItemRevision
                || command.expectedInventoryRevision.value() != mWorld.getPtrRegistryRevision()
                || item.getContainerStore() != &mRuntime.storage(actor(bound.player())))
                throw std::invalid_argument("Native equipment command shape, revision or ownership invalid");
            return;
        }
        const auto sharedIndex = container(command.containerId);
        const auto& shared = mBinding.mContainers[sharedIndex];
        const auto sharedOwner = mRuntime.ownerPtr(sharedIndex + 2);
        if (actorInventory(sharedOwner) && !initialCorpse(sharedOwner))
            throw std::invalid_argument("Living actor inventory access requires theft/companion services");
        const auto& player = *players.findPlayer(bound.player());
        const auto revision = mWorld.getPtrRegistryRevision();
        if (command.player != bound.player()
            || !command.stackId || command.slot
            || command.expectedWorldItemRevision || command.count == 0 || command.count > MaximumTransferCount
            || (command.kind == InventoryTransactionKind::TakeAllFromContainer && command.count != 1)
            || command.expectedInventoryRevision.value() != revision || !command.expectedContainerRevision
            || command.expectedContainerRevision->value() != revision
            || (command.kind != InventoryTransactionKind::PutIntoContainer
                && command.kind != InventoryTransactionKind::TakeFromContainer
                && command.kind != InventoryTransactionKind::TakeAllFromContainer)
            || player.transform().cell() != shared.mCell
            || !positionsWithinReach(player.transform().position(), shared.mPosition, ReachQuanta)
            || !positionsWithinReach(command.interactionOrigin, shared.mPosition, ReachQuanta)
            || !positionsWithinReach(player.transform().position(), command.interactionOrigin, ReachQuanta))
            throw std::invalid_argument("Native container command shape, revision, identity or reach invalid");
        if (command.kind == InventoryTransactionKind::PutIntoContainer && !actorInventory(sharedOwner))
        {
            const auto* base = mRuntime.ownerPtr(sharedIndex + 2).get<ESM::Container>()->mBase;
            if (MWWorld::checkContainerPut((base->mFlags & ESM::Container::Organic) != 0,
                    base->mWeight, mRuntime.storage(sharedIndex + 2).getWeight(), item.getClass().getWeight(item), int(command.count))
                != MWWorld::ContainerPutCheck::Allowed)
                throw std::invalid_argument("Native container rejects organic or over-capacity put");
        }
    }

    InventoryService::PreparedCommand InventoryService::prepare(const CanonicalServerState& players,
        const ServerApp::InventoryCommandBinding& bound)
    {
        validate(players, bound); // Bound all external fields before engine allocation.
        const auto index = actor(bound.player());
        const auto& input = bound.transaction();
        auto command = mRuntime.containerCommand(index, input.kind == InventoryTransactionKind::PutIntoContainer,
            nativeId(*input.stackId), int32_t(input.count), container(input.containerId));
        command.mTakeAll = input.kind == InventoryTransactionKind::TakeAllFromContainer;
        return PreparedCommand(bound, mRuntime.prepare({ command.mInitiator }, command));
    }

    PersistenceResult InventoryService::commit(const CanonicalServerState& players, PreparedCommand& command,
        EquipmentSessionCommitter& durability, std::unique_ptr<const InventoryTransferSuccess>& success,
        EquipmentBytes& bytes)
    {
        validate(players, command.mBinding);
        const auto image = command.image();
        if (image.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory image exceeds canonical record budget");
        EquipmentBytes retained(image.begin(), image.end());
        const auto result = mRuntime.commit(command.mTransfer, durability, success, bytes);
        if (result == PersistenceResult::Accepted)
        {
            mImage.swap(retained);
            retireCommittedEffects();
        }
        return result;
    }

    void InventoryService::retireCommittedEffects() noexcept
    {
        // This service publishes the committed image as baselines. The runtime's
        // diagnostic notifications have no further consumer here; retaining them
        // would turn the per-command bound into a lifetime transfer limit.
        for (size_t owner = 0; owner < mRuntime.ownerCount(); ++owner)
        {
            auto& effects = mRuntime.effects(owner);
            effects.mListener.mCalls = 0;
            effects.mListener.mRemovals.clear();
            effects.mInventoryUpdates = 0;
            effects.mNotifications.clear();
        }
    }

    FileReadResult InventoryService::recover(const std::filesystem::path& path, std::span<const ESM::RefId> references,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        std::unique_ptr<const EquipmentSessionValues> values;
        EquipmentBytes image;
        const auto result = readBoundedFile(path, MaximumNativeInventoryImageBytes, image, faults);
        if (result != FileReadResult::Read) return result;
        EquipmentBytes retained = image;
        mRuntime.restoreSession(std::move(image), references, values, bytes);
        mImage.swap(retained);
        return result;
    }

    class InventoryService::Transaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        CanonicalServerState players;
        PreparedCommand prepared;
        Transaction(InventoryService& owner, const CanonicalServerState& state, PreparedCommand command)
            : service(owner), players(state), prepared(std::move(command)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            struct Sink final : EquipmentSessionCommitter
            {
                const NativeInventoryCommit& persist;
                explicit Sink(const NativeInventoryCommit& value) : persist(value) {}
                PersistenceResult commit(std::span<const char> image) noexcept override
                {
                    const auto result = persist(std::as_bytes(image));
                    return result == CanonicalDurabilityResult::Committed ? PersistenceResult::Accepted
                        : result == CanonicalDurabilityResult::Rejected ? PersistenceResult::Rejected
                        : PersistenceResult::Uncertain;
                }
            } sink(persist);
            std::unique_ptr<const InventoryTransferSuccess> success;
            EquipmentBytes bytes;
            const auto result = service.commit(players, prepared, sink, success, bytes);
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected
                : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::EquipmentTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        CanonicalServerState players;
        ServerApp::InventoryCommandBinding binding;
        EquipmentRuntime::PreparedEquipment prepared;
        EquipmentTransaction(InventoryService& owner, const CanonicalServerState& state,
            ServerApp::InventoryCommandBinding bound, EquipmentRuntime::PreparedEquipment candidate)
            : service(owner), players(state), binding(std::move(bound)), prepared(std::move(candidate)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            service.validate(players, binding);
            EquipmentBytes retained(prepared.image().begin(), prepared.image().end());
            struct Sink final : EquipmentSessionCommitter
            {
                const NativeInventoryCommit& persist;
                explicit Sink(const NativeInventoryCommit& value) : persist(value) {}
                PersistenceResult commit(std::span<const char> image) noexcept override
                {
                    const auto result = persist(std::as_bytes(image));
                    return result == CanonicalDurabilityResult::Committed ? PersistenceResult::Accepted
                        : result == CanonicalDurabilityResult::Rejected ? PersistenceResult::Rejected : PersistenceResult::Uncertain;
                }
            } sink(persist);
            std::unique_ptr<const EquipmentSuccess> success;
            EquipmentBytes bytes;
            const auto result = service.mRuntime.commit(prepared, sink, success, bytes);
            if (result == PersistenceResult::Accepted)
            {
                service.mImage.swap(retained);
                service.retireCommittedEffects();
            }
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareInventory(
        const CanonicalServerState& players, const ServerCommandProposal& proposal)
    {
        const auto binding = ServerApp::InventoryCommandBinding::fromProposal(players, proposal);
        if (!binding) return {};
        try
        {
            const auto& input = binding->transaction();
            if (input.kind == InventoryTransactionKind::EquipItem || input.kind == InventoryTransactionKind::UnequipItem)
            {
                validate(players, *binding);
                const auto owner = EquipmentRuntime::ownedId(mRuntime.ownerPtr(actor(binding->player())).getCellRef().getRefNum());
                EquipmentCommand command{owner, nativeId(*input.stackId), input.expectedInventoryRevision.value(),
                    input.kind == InventoryTransactionKind::EquipItem ? EquipmentRequestedState::Equipped : EquipmentRequestedState::Unequipped,
                    static_cast<int>(*input.slot)};
                auto prepared = mRuntime.prepare(EquipmentCaller{owner}, command);
                if (prepared.image().size() > MaximumNativeInventoryImageBytes)
                    throw std::invalid_argument("Native equipment image exceeds canonical record budget");
                return std::make_unique<EquipmentTransaction>(*this, players, *binding, std::move(prepared));
            }
            return std::make_unique<Transaction>(*this, players, prepare(players, *binding));
        }
        catch (const std::invalid_argument&) { return {}; }
    }

    std::span<const std::byte> InventoryService::inventoryImage() const noexcept
    {
        return mRuntime.mFailedClosed || mRuntime.mRestartActor ? std::span<const std::byte>{}
            : std::as_bytes(std::span(mImage));
    }

    void InventoryService::recover(std::span<const std::byte> image, std::span<const ESM::RefId> references)
    {
        if (image.empty() || image.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory recovery image bound invalid");
        EquipmentBytes accepted(reinterpret_cast<const char*>(image.data()), reinterpret_cast<const char*>(image.data()+image.size()));
        std::unique_ptr<const EquipmentSessionValues> values;
        EquipmentBytes output;
        mRuntime.restoreSession(std::move(accepted), references, values, output);
        mImage.swap(output);
    }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::projectInventory(
        const CanonicalServerState& players, SessionId target, ServerTick tick, CanonicalRevision revision,
        const PreparedNativeInventory* candidate) const
    {
        const auto* transaction = dynamic_cast<const Transaction*>(candidate);
        const auto* equipment = dynamic_cast<const EquipmentTransaction*>(candidate);
        if (candidate && ((!transaction && !equipment) || (transaction && &transaction->service != this)
                || (equipment && &equipment->service != this))) return std::nullopt;
        return project(players, target, tick, revision, transaction ? &transaction->prepared : nullptr,
            equipment ? &equipment->prepared : nullptr);
    }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::project(const CanonicalServerState& players,
        SessionId target, ServerTick tick, CanonicalRevision revision, const PreparedCommand* candidate,
        const EquipmentRuntime::PreparedEquipment* equipped) const
    try
    {
        if (mRuntime.mRestartActor || mRuntime.mFailedClosed) return std::nullopt;
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player) return std::nullopt;
        const auto index = actor(player->playerId());
        const auto values = [&](size_t owner) {
            if (equipped) return mRuntime.preparedValues(*equipped, owner);
            return candidate ? mRuntime.preparedValues(candidate->mTransfer, owner) : mRuntime.installedValues(owner);
        };
        const auto installed = values(index);
        const auto items = stacks(installed, mItemIds, mRuntime.mStore);
        std::vector<EquipmentBinding> equipment;
        static_assert(static_cast<int>(EquipmentSlot::Count) == InventoryStore::Slots);
        for (int slot = 0; slot < InventoryStore::Slots; ++slot)
            if (installed.mSlots[slot].isSet())
                equipment.push_back({static_cast<EquipmentSlot>(slot), wireId(installed.mSlots[slot])});
        const auto version = equipped ? equipped->candidate().mRevision
            : candidate ? candidate->candidate().mRevision : mWorld.getPtrRegistryRevision();
        const InventoryBaselineHeader header{ target, session->sessionGeneration(), tick, revision, 0, 1 };
        ServerApp::InventoryInterestDelivery result{ .targetSession = target };
        auto inventory = ReliablePlayerInventoryBaseline::create(header, player->playerId(),
            InventoryRevision::fromValue(version).value(), items, equipment);
        if (!std::holds_alternative<ReliablePlayerInventoryBaseline>(inventory)) return std::nullopt;
        result.playerInventory.push_back(std::get<ReliablePlayerInventoryBaseline>(std::move(inventory)));
        const auto publicSlots = [&](const PlainEquipmentValues& state) {
            std::array<std::optional<ItemPrototypeId>, static_cast<size_t>(EquipmentSlot::Count)> slots{};
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (state.mSlots[slot].isSet())
                {
                    const auto item = std::ranges::find(state.mObjects, state.mSlots[slot],
                        [](const auto& object) { return object.mRef.mRefNum; });
                    if (item == state.mObjects.end()) throw std::logic_error("Public equipment item missing");
                    slots[slot] = mItemIds.at(item->mRef.mRefID);
                }
            return slots;
        };
        std::vector<PublicActorEquipmentMember> actors;
        for (size_t i = 0; i < mBinding.mContainers.size(); ++i)
        {
            const auto& shared = mBinding.mContainers[i];
            if (player->transform().cell() != shared.mCell) continue;
            const auto owner = mRuntime.ownerPtr(i + 2);
            const auto sharedValues = values(i + 2);
            if (actorInventory(owner) && !initialCorpse(owner))
            {
                // Appearance only: no private stacks, counts or transfer revision.
                // Empty slots also suppress any locally selected starting gear.
                actors.push_back({shared.mId, publicSlots(sharedValues)});
                continue;
            }
            std::vector<EquipmentBinding> slots;
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (sharedValues.mSlots[slot].isSet())
                    slots.push_back({static_cast<EquipmentSlot>(slot), wireId(sharedValues.mSlots[slot])});
            auto baseline = ReliableContainerInventoryBaseline::create(header, shared.mId, shared.mCell,
                shared.mPosition, ContainerRevision::fromValue(version).value(), 0,
                stacks(sharedValues, mItemIds, mRuntime.mStore), slots);
            if (!std::holds_alternative<ReliableContainerInventoryBaseline>(baseline)) return std::nullopt;
            result.containers.push_back(std::get<ReliableContainerInventoryBaseline>(std::move(baseline)));
        }
        auto ground = ReliableGroundItemBaseline::create(header, player->transform().cell(), {});
        if (!std::holds_alternative<ReliableGroundItemBaseline>(ground)) return std::nullopt;
        result.groundItems.push_back(std::get<ReliableGroundItemBaseline>(std::move(ground)));
        std::vector<PublicEquipmentMember> visible;
        for (size_t i = 0; i < 2; ++i)
            if (const auto* other = players.findPlayer(mBinding.mPlayers[i]);
                other && other->transform().cell() == player->transform().cell())
            {
                visible.push_back({other->playerId(), publicSlots(values(i))});
            }
        std::ranges::sort(visible, {}, &PublicEquipmentMember::player);
        std::ranges::sort(actors, {}, &PublicActorEquipmentMember::actor);
        auto publicEquipment = LatestWinsEquipmentSnapshot::create(target, session->sessionGeneration(), tick, revision, visible, actors);
        if (!std::holds_alternative<LatestWinsEquipmentSnapshot>(publicEquipment)) return std::nullopt;
        result.equipment = std::get<LatestWinsEquipmentSnapshot>(std::move(publicEquipment));
        return result;
    }
    catch (...) { return std::nullopt; }
}
