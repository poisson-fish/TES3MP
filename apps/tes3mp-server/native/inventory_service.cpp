#include "inventory_service.hpp"
#include <apps/openmw/mwworld/esmstore.hpp>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        std::string identity(const InventoryServiceBinding& binding, const MWWorld::ESMStore& content)
        {
            if (binding.mPlayers[0] == binding.mPlayers[1] || binding.mContainerBase.empty()
                || binding.mActors[0].mShirt != binding.mActors[1].mShirt)
                throw std::invalid_argument("Native inventory requires distinct trusted players and one shared shirt/container");
            const auto* shirt = content.get<ESM::Clothing>().find(binding.mActors[0].mShirt);
            if (!shirt->mScript.empty() || !shirt->mEnchant.empty())
                throw std::invalid_argument("Native inventory wire projection supports the plain shirt only");
            return "native-inventory-1/" + std::to_string(binding.mPlayers[0].value()) + "/"
                + std::to_string(binding.mPlayers[1].value()) + "/" + std::to_string(binding.mShirt.value())
                + "/" + std::to_string(binding.mContainer.value());
        }
        ItemStackId wireId(ESM::RefNum id)
        {
            return ItemStackId::fromValue((uint64_t(std::bit_cast<uint32_t>(id.mContentFile)) << 32) | id.mIndex).value();
        }
        InventoryInstanceId nativeId(ItemStackId id)
        {
            return { uint32_t(id.value()), std::bit_cast<int32_t>(uint32_t(id.value() >> 32)) };
        }
        std::vector<CanonicalItemStack> stacks(const MWWorld::PlainEquipmentValues& values, ItemPrototypeId shirt)
        {
            std::vector<CanonicalItemStack> result;
            result.reserve(values.mObjects.size());
            for (const auto& object : values.mObjects)
            {
                const auto count = object.mRef.mCount;
                if (count == 0) continue; // Dormant IDs remain engine-owned and durable.
                if (count == std::numeric_limits<int32_t>::min() || !object.mRef.mSoul.empty())
                    throw std::invalid_argument("Native inventory fields cannot be projected losslessly");
                result.push_back({ wireId(object.mRef.mRefNum), shirt, uint32_t(std::abs(count)) });
            }
            std::ranges::sort(result, {}, &CanonicalItemStack::stackId);
            return result;
        }
    }

    InventoryService::InventoryService(MWWorld::ESMStore& content, ESM::ReadersCache& readers,
        InventoryServiceBinding binding, bool recovering)
        : mBinding(std::move(binding)), mWorld(content, readers, 1), mScripts(content),
          mRuntime(content, mWorld, mScripts, identity(mBinding, content), mBinding.mContent, mBinding.mActors,
              {}, nullptr, recovering ? std::optional<size_t>{ 2 } : std::nullopt, true, mBinding.mContainerBase)
    {
    }

    size_t InventoryService::actor(PlayerId player) const
    {
        for (size_t i = 0; i < mBinding.mPlayers.size(); ++i)
            if (mBinding.mPlayers[i] == player) return i;
        throw std::invalid_argument("Authenticated player has no native actor binding");
    }

    void InventoryService::validate(const CanonicalServerState& players, const ServerApp::InventoryCommandBinding& bound) const
    {
        if (!bound.current(players)) throw std::invalid_argument("Inventory session binding is no longer current");
        (void)actor(bound.player());
        const auto& command = bound.transaction();
        const auto& player = *players.findPlayer(bound.player());
        const auto revision = mWorld.getPtrRegistryRevision();
        if (command.player != bound.player() || command.containerId != mBinding.mContainer
            || command.prototypeId != mBinding.mShirt || !command.stackId || command.slot
            || command.expectedWorldItemRevision || command.count == 0 || command.count > MaximumTransferCount
            || command.expectedInventoryRevision.value() != revision || !command.expectedContainerRevision
            || command.expectedContainerRevision->value() != revision
            || (command.kind != InventoryTransactionKind::PutIntoContainer
                && command.kind != InventoryTransactionKind::TakeFromContainer)
            || player.transform().cell() != mBinding.mCell
            || !positionsWithinReach(player.transform().position(), mBinding.mPosition, 384)
            || !positionsWithinReach(command.interactionOrigin, mBinding.mPosition, 384)
            || !positionsWithinReach(player.transform().position(), command.interactionOrigin, 384))
            throw std::invalid_argument("Native container command shape, revision, identity or reach invalid");
    }

    InventoryService::PreparedCommand InventoryService::prepare(const CanonicalServerState& players,
        const ServerApp::InventoryCommandBinding& bound)
    {
        validate(players, bound); // Bound all external fields before engine allocation.
        const auto index = actor(bound.player());
        const auto& input = bound.transaction();
        auto command = mRuntime.containerCommand(index, input.kind == InventoryTransactionKind::PutIntoContainer,
            nativeId(*input.stackId), int32_t(input.count));
        return PreparedCommand(bound, mRuntime.prepare({ command.mInitiator }, command));
    }

    PersistenceResult InventoryService::commit(const CanonicalServerState& players, PreparedCommand& command,
        EquipmentSessionCommitter& durability, std::unique_ptr<const InventoryTransferSuccess>& success,
        EquipmentBytes& bytes)
    {
        validate(players, command.mBinding);
        return mRuntime.commit(command.mTransfer, durability, success, bytes);
    }

    FileReadResult InventoryService::recover(const std::filesystem::path& path, std::span<const ESM::RefId> references,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        std::unique_ptr<const EquipmentSessionValues> values;
        return mRuntime.restartSession(path, references, values, bytes, faults);
    }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::project(const CanonicalServerState& players,
        SessionId target, ServerTick tick, CanonicalRevision revision) const
    try
    {
        if (mRuntime.mRestartActor || mRuntime.mFailedClosed) return std::nullopt;
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player) return std::nullopt;
        const auto index = actor(player->playerId());
        const auto installed = mRuntime.installedValues(index);
        const auto items = stacks(installed, mBinding.mShirt);
        std::vector<EquipmentBinding> equipment;
        if (installed.mShirt.isSet()) equipment.push_back({ EquipmentSlot::Shirt, wireId(installed.mShirt) });
        const auto version = mWorld.getPtrRegistryRevision();
        const InventoryBaselineHeader header{ target, session->sessionGeneration(), tick, revision, 0, 1 };
        ServerApp::InventoryInterestDelivery result{ .targetSession = target };
        auto inventory = ReliablePlayerInventoryBaseline::create(header, player->playerId(),
            InventoryRevision::fromValue(version).value(), items, equipment);
        if (!std::holds_alternative<ReliablePlayerInventoryBaseline>(inventory)) return std::nullopt;
        result.playerInventory.push_back(std::get<ReliablePlayerInventoryBaseline>(std::move(inventory)));
        if (player->transform().cell() == mBinding.mCell)
        {
            auto shared = ReliableContainerInventoryBaseline::create(header, mBinding.mContainer, mBinding.mCell,
                mBinding.mPosition, ContainerRevision::fromValue(version).value(), 0,
                stacks(mRuntime.installedValues(2), mBinding.mShirt));
            if (!std::holds_alternative<ReliableContainerInventoryBaseline>(shared)) return std::nullopt;
            result.containers.push_back(std::get<ReliableContainerInventoryBaseline>(std::move(shared)));
        }
        auto ground = ReliableGroundItemBaseline::create(header, player->transform().cell(), {});
        if (!std::holds_alternative<ReliableGroundItemBaseline>(ground)) return std::nullopt;
        result.groundItems.push_back(std::get<ReliableGroundItemBaseline>(std::move(ground)));
        std::vector<PublicEquipmentMember> visible;
        for (size_t i = 0; i < 2; ++i)
            if (const auto* other = players.findPlayer(mBinding.mPlayers[i]);
                other && other->transform().cell() == player->transform().cell())
            {
                PublicEquipmentMember member{ .player = other->playerId() };
                if (mRuntime.installedValues(i).mShirt.isSet())
                    member.slots[static_cast<size_t>(EquipmentSlot::Shirt)] = mBinding.mShirt;
                visible.push_back(member);
            }
        std::ranges::sort(visible, {}, &PublicEquipmentMember::player);
        auto publicEquipment = LatestWinsEquipmentSnapshot::create(target, session->sessionGeneration(), tick, revision, visible);
        if (!std::holds_alternative<LatestWinsEquipmentSnapshot>(publicEquipment)) return std::nullopt;
        result.equipment = std::get<LatestWinsEquipmentSnapshot>(std::move(publicEquipment));
        return result;
    }
    catch (...) { return std::nullopt; }
}
