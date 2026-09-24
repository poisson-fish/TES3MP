#include <numbers>
#include "inventory_service.hpp"
#include "actor_inventory.hpp"
#include "actor_campaign.hpp"
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containeradd.hpp>
#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/statstate.hpp>
#include <components/misc/rng.hpp>
#include <tes3mp/melee_combat.hpp>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <set>
#include <cstdio>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        class SealedCommitter final : public EquipmentSessionCommitter
        {
            EquipmentSessionCommitter& mSink;
            const EquipmentBytes& mImage;
        public:
            SealedCommitter(EquipmentSessionCommitter& sink, const EquipmentBytes& image) : mSink(sink), mImage(image) {}
            PersistenceResult commit(std::span<const char>) noexcept override { return mSink.commit(mImage); }
        };
        ActorCampaignCombat initialCombat(const std::array<ESM::RefId, 3>& actors,
            const MWWorld::ESMStore& content, uint32_t seed)
        {
            static_assert(ActorCampaignCombat::StatCount == ESM::Attribute::Length + 3 + ESM::Skill::Length);
            ActorCampaignCombat result;
            Misc::Rng::Generator rng{seed};
            result.rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            const float magickaMultiplier = content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat();
            if (!std::isfinite(magickaMultiplier) || magickaMultiplier < 0 || magickaMultiplier > 1000)
                throw std::invalid_argument("Native combat magicka multiplier invalid");
            for (size_t actor = 0; actor < actors.size(); ++actor)
            {
                const auto& base = *content.get<ESM::NPC>().find(actors[actor]);
                MWMechanics::NpcStats stats(content);
                if (base.mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS)
                    stats.initializeAutoStats(base, content, magickaMultiplier);
                else stats.initializeExplicitStats(base, magickaMultiplier);
                size_t index = 0;
                const auto capture = [&](const auto& stat) {
                    ESM::StatState<float> value;
                    stat.writeState(value);
                    result.actors[actor][index++] = {value.mBase, value.mMod, value.mCurrent,
                        value.mDamage, value.mProgress};
                };
                for (int i = 0; i < ESM::Attribute::Length; ++i)
                    capture(stats.getAttribute(ESM::Attribute::indexToRefId(i)));
                for (int i = 0; i < 3; ++i) capture(stats.getDynamic(i));
                for (int i = 0; i < ESM::Skill::Length; ++i)
                    capture(stats.getSkill(ESM::Skill::indexToRefId(i)));
                if (index != result.actors[actor].size())
                    throw std::invalid_argument("Native combat stat shape invalid");
            }
            return result;
        }
        ESM::StatState<float> combatStat(const std::array<float, 5>& fields)
        {
            ESM::StatState<float> value;
            value.mBase = fields[0]; value.mMod = fields[1]; value.mCurrent = fields[2];
            value.mDamage = fields[3]; value.mProgress = fields[4];
            return value;
        }
        MWMechanics::NpcStats loadCombatStats(const MWWorld::ESMStore& content,
            const std::array<std::array<float, 5>, ActorCampaignCombat::StatCount>& fields)
        {
            MWMechanics::NpcStats stats(content);
            size_t index = 0;
            for (int i = 0; i < ESM::Attribute::Length; ++i)
            {
                MWMechanics::AttributeValue value;
                value.readState(combatStat(fields[index++]));
                stats.setAttribute(ESM::Attribute::indexToRefId(i), value, 0.f);
            }
            for (int i = 0; i < 3; ++i)
            {
                MWMechanics::DynamicStat<float> value;
                value.readState(combatStat(fields[index++]));
                stats.setDynamic(i, value, MWWorld::TimeStamp{});
            }
            for (int i = 0; i < ESM::Skill::Length; ++i)
            {
                MWMechanics::SkillValue value;
                value.readState(combatStat(fields[index++]));
                stats.setSkill(ESM::Skill::indexToRefId(i), value);
            }
            return stats;
        }
        void saveCombatStats(std::array<std::array<float, 5>, ActorCampaignCombat::StatCount>& fields,
            const MWMechanics::NpcStats& stats)
        {
            size_t index = 0;
            const auto capture = [&](const auto& stat) {
                ESM::StatState<float> value;
                stat.writeState(value);
                fields[index++] = {value.mBase, value.mMod, value.mCurrent, value.mDamage, value.mProgress};
            };
            for (int i = 0; i < ESM::Attribute::Length; ++i)
                capture(stats.getAttribute(ESM::Attribute::indexToRefId(i)));
            for (int i = 0; i < 3; ++i) capture(stats.getDynamic(i));
            for (int i = 0; i < ESM::Skill::Length; ++i)
                capture(stats.getSkill(ESM::Skill::indexToRefId(i)));
        }
        std::string identity(const InventoryServiceBinding& binding, const MWWorld::ESMStore& content)
        {
            if (binding.mSecondWorldItems && (!binding.mWorldItems || (!binding.mStreamExteriors && !binding.mDoor)
                || binding.mSecondWorldItems->mCell == binding.mWorldItems->mCell))
                throw std::invalid_argument("Two-cell domain requires distinct cells and the first cell's door");
            const auto domains = binding.worldDomains();
            if (domains.size() > MaxEquipmentCells || (!binding.mAdditionalWorldItems.empty() && !binding.mSecondWorldItems))
                throw std::invalid_argument("Native cell domain exceeds its bound or has missing predecessors");
            std::set<CellId> cells;
            for (const auto* domain : domains)
                if (!cells.insert(domain->mCell).second)
                    throw std::invalid_argument("Duplicate native cell mapping");
            if (binding.mSecondWorldItems)
            {
                for (const auto& shared : binding.mContainers)
                    if (!cells.contains(shared.mCell))
                        throw std::invalid_argument("Shared inventory outside the two-cell domain");
            }
            if (binding.mDoor && (!binding.mWorldItems || !(binding.mDoorId >> 63)))
                throw std::invalid_argument("Native door requires a stable placement ID and world cell");
            if (binding.mTeleportDoors)
            {
                if (domains.empty() || binding.mTeleportDoors->size() > 32 * domains.size())
                    throw std::invalid_argument("Native teleport domain requires two cells and bounded doors");
                std::set<uint64_t> doors{binding.mDoorId};
                for (const auto& door : *binding.mTeleportDoors)
                    if (!(door.mId >> 63) || !doors.insert(door.mId).second
                        || !cells.contains(door.mCell) || !cells.contains(door.mDestination.cell())
                        || door.mDestination.cell() == door.mCell)
                        throw std::invalid_argument("Native teleport identity or cell mapping invalid");
            }
            if (binding.mPlayers[0] == binding.mPlayers[1] || (binding.mContainers.empty() && !binding.mWorldItems)
                || binding.mContainers.size() > (binding.mStreamExteriors ? MaxEquipmentContainers : 32)
                || binding.mActors[0].mBaseInventory != binding.mActors[1].mBaseInventory)
                throw std::invalid_argument("Native inventory requires distinct trusted players, bounded containers and one initialization mode");
            std::set<ContainerId> ids;
            for (const auto& container : binding.mContainers)
                if (container.mBase.empty() || !ids.insert(container.mId).second)
                    throw std::invalid_argument("Native container base or identity invalid");
            std::vector<ActorSpawnSelection> selections;
            for (const auto* domain : domains)
            {
                if (domain->mActorSpawns.size() > MaximumEquipmentSnapshotActors)
                    throw std::invalid_argument("Native cell spawn budget exceeded");
                for (const auto& spawn : domain->mActorSpawns)
                {
                    const auto owner = std::ranges::find_if(binding.mContainers,
                        [&](const auto& value) { return value.mId.value() == spawn.placement; });
                    if (spawn.record ? (owner == binding.mContainers.end() || owner->mCell != domain->mCell
                            || MWWorld::inventoryRecordId(owner->mBase) != spawn.record
                            || (content.find(owner->mBase) != ESM::NPC::sRecordId && content.find(owner->mBase) != ESM::Creature::sRecordId))
                        : owner != binding.mContainers.end())
                        throw std::invalid_argument("Native actor spawn owner mismatch");
                    selections.push_back({spawn.placement, spawn.record});
                }
            }
            std::ranges::sort(selections, {}, &ActorSpawnSelection::mPlacement);
            validateActorSpawns(selections);
            if (binding.mActorSelections ? (!binding.mStreamExteriors || *binding.mActorSelections != selections) : !selections.empty())
                throw std::invalid_argument("Native actor spawn persistence binding mismatch");
            if (binding.mActors[0].mBaseInventory)
            {
                if (binding.mShirt)
                    throw std::invalid_argument("Base actor inventories cannot override an item identity");
                return "native-inventory-2/" + std::to_string(binding.mPlayers[0].value()) + "/"
                    + std::to_string(binding.mPlayers[1].value()) + "/"
                    + std::to_string(binding.mContainers.empty() ? 0 : binding.mContainers.front().mId.value());
            }
            if (!binding.mShirt || binding.mActors[0].mShirt != binding.mActors[1].mShirt)
                throw std::invalid_argument("Legacy native inventory requires one seed shirt");
            const auto* shirt = content.get<ESM::Clothing>().find(binding.mActors[0].mShirt);
            if (!shirt->mScript.empty() || !shirt->mEnchant.empty())
                throw std::invalid_argument("Native inventory wire projection supports the plain shirt only");
            return "native-inventory-1/" + std::to_string(binding.mPlayers[0].value()) + "/"
                + std::to_string(binding.mPlayers[1].value()) + "/" + std::to_string(binding.mShirt->value())
                + "/" + std::to_string(binding.mContainers.empty() ? 0 : binding.mContainers.front().mId.value());
        }
        std::vector<EquipmentContainerBinding> containers(const InventoryServiceBinding& binding)
        {
            if (binding.mContainers.size() > MaxEquipmentContainers)
                throw std::invalid_argument("Native container budget exceeded");
            std::vector<EquipmentContainerBinding> result;
            for (const auto& shared : binding.mContainers) result.push_back({shared.mBase, shared.mPlacement});
            return result;
        }
        std::optional<std::vector<ESM::CellRef>> worldItems(const InventoryServiceBinding& binding)
        {
            if (!binding.mWorldItems) return {};
            const auto domains = binding.worldDomains();
            size_t count = 0;
            for (const auto* domain : domains) count += domain->mPlacements.size();
            if (count > PlainEquipmentValues::MaxWorldItems)
                throw std::invalid_argument("Native world placement budget exceeded");
            std::vector<ESM::CellRef> result;
            for (const auto* domain : domains)
            {
                if (!domain) continue;
                uint64_t previous = 0;
                for (const auto& [id, ref] : domain->mPlacements)
                {
                    if (!id || id <= previous) throw std::invalid_argument("Native world identities not sorted");
                    previous = id;
                    result.push_back(ref);
                }
            }
            return result;
        }
        std::optional<EquipmentSessionValues::WorldCells> worldCells(const InventoryServiceBinding& binding)
        {
            if (!binding.mSecondWorldItems && !binding.mStreamExteriors) return {};
            if (!binding.mWorldItems) throw std::invalid_argument("Second world cell requires the first");
            EquipmentSessionValues::WorldCells result;
            uint8_t index = 0;
            for (const auto* domain : binding.worldDomains())
            {
                for (const auto& [id, ref] : domain->mPlacements)
                    if (!result.emplace(ref.mRefNum, index).second)
                        throw std::invalid_argument("Duplicate world placement across cells");
                ++index;
            }
            return result;
        }
        Position3 worldPosition(const ESM::CellRef& ref)
        {
            return Position3(std::llround(double(ref.mPos.pos[0]) * 1024),
                std::llround(double(ref.mPos.pos[1]) * 1024), std::llround(double(ref.mPos.pos[2]) * 1024));
        }
        ItemStackId wireId(ESM::RefNum id)
        {
            return ItemStackId::fromValue((uint64_t(std::bit_cast<uint32_t>(id.mContentFile)) << 32) | id.mIndex).value();
        }
        ItemStackId worldId(ESM::RefNum ref, const InventoryServiceBinding::WorldItems& domain)
        {
            if (!ref.hasContentFile()) return wireId(ref);
            for (const auto& [id, placed] : domain.mPlacements)
                if (placed.mRefNum == ref) return ItemStackId::fromValue(id).value();
            throw std::invalid_argument("World reference outside the bound placement domain");
        }
        InventoryInstanceId nativeId(ItemStackId id)
        {
            return { uint32_t(id.value()), std::bit_cast<int32_t>(uint32_t(id.value() >> 32)) };
        }
        std::vector<CanonicalItemStack> stacks(const MWWorld::PlainEquipmentValues& values,
            const std::map<ESM::RefId, ItemPrototypeId>& items, const MWWorld::ESMStore& content,
            const InventoryServiceBinding::WorldItems* world = nullptr)
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
                result.push_back({ world ? worldId(object.mRef.mRefNum, *world) : wireId(object.mRef.mRefNum), id, uint32_t(std::abs(count)),
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
              {}, nullptr, recovering ? std::optional<size_t>{ 2 } : std::nullopt, true, containers(mBinding), mBinding.mLootLevel, mBinding.mLootSeed, worldItems(mBinding), mBinding.mDoor, worldCells(mBinding), mBinding.worldDomains().size(), mBinding.mStreamExteriors ? PlainEquipmentValues::MaxWorldItems : 64)
    {
        if (mBinding.mMeleeContact && !mBinding.mBoundMelee)
            throw std::invalid_argument("Native melee contact requires a bound animation");
        if (mBinding.mCombatState)
        {
            if (!mBinding.mMeleeContact || !mBinding.mNavigatingActor)
                throw std::invalid_argument("Native combat stats require both players and a selected melee NPC");
            const auto id = mBinding.mNavigatingActor->actorId();
            const auto owner = std::ranges::find_if(mBinding.mContainers,
                [id](const auto& value) { return value.mId.value() == id; });
            if (owner == mBinding.mContainers.end() || mRuntime.ownerPtr(size_t(owner - mBinding.mContainers.begin()) + 2).getType() != ESM::NPC::sRecordId)
                throw std::invalid_argument("Native combat stat owner must be the selected NPC");
            mCombatNpcOwner = size_t(owner - mBinding.mContainers.begin()) + 2;
            mCombat = initialCombat({mBinding.mActors[0].mBase, mBinding.mActors[1].mBase, owner->mBase},
                content, mBinding.mLootSeed);
            (void)mRuntime.equippedWeaponCondition(mCombatNpcOwner);
        }
        if (mBinding.mBoundMelee)
        {
            if (!mBinding.mNavigatingActor || mBinding.mBoundMelee->mResourceIdentity.empty()
                || mBinding.mBoundMelee->mResourceIdentity.size() > 512)
                throw std::invalid_argument("Native melee binding invalid");
            mMelee = mBinding.mBoundMelee->mAnimation;
        }
        initializeAreaDoors();
        for (const auto& [id, record] : MWWorld::inventoryRecords(content))
            mItemIds.emplace(record, ItemPrototypeId::fromValue(id).value());
        // Synthetic service fixtures may retain their explicit shirt ID.
        if (mBinding.mShirt) mItemIds.insert_or_assign(mBinding.mActors[0].mShirt, *mBinding.mShirt);
        if (!recovering)
            mRuntime.encodeSession({{mRuntime.installedValues(0), mRuntime.installedValues(1)},
                mWorld.getPtrRegistryRevision()}, mImage);
        if (!recovering)
        {
            mCoreImage = mImage;
            mImage = sealInventory(mCoreImage);
        }
        if (!recovering && mBinding.mNavigatingActor)
        {
            const auto id = mBinding.mNavigatingActor->snapshot().mActor;
            if (!mBinding.mStreamExteriors || std::ranges::none_of(mBinding.mContainers,
                    [id](const auto& owner) { return owner.mId.value() == id && owner.mPlacement.has_value(); }))
                throw std::invalid_argument("Navigating NPC has no authoritative inventory owner");
            mActorImage = sealActor(mImage, mBinding.mNavigatingActor->image(), mActorTick, mActorVelocity,
                mMelee, mMeleeTarget, mMeleeContacted, mCombat);
            installActorPosition();
        }
        if (mImage.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory image exceeds canonical record budget");
    }

    size_t InventoryService::actor(PlayerId player) const
    {
        for (size_t i = 0; i < mBinding.mPlayers.size(); ++i)
            if (mBinding.mPlayers[i] == player) return i;
        throw std::invalid_argument("Authenticated player has no native actor binding");
    }

    const InventoryServiceBinding::WorldItems* InventoryService::worldDomain(CellId cell) const
    {
        if (mBinding.mWorldItems && mBinding.mWorldItems->mCell == cell) return &*mBinding.mWorldItems;
        if (mBinding.mSecondWorldItems && mBinding.mSecondWorldItems->mCell == cell) return &*mBinding.mSecondWorldItems;
        for (const auto& domain : mBinding.mAdditionalWorldItems)
            if (domain.mCell == cell) return &domain;
        return nullptr;
    }

    uint8_t InventoryService::worldIndex(CellId cell) const
    {
        if (!worldDomain(cell)) throw std::invalid_argument("Cell outside native world domain");
        const auto domains = mBinding.worldDomains();
        for (size_t i = 0; i < domains.size(); ++i)
            if (domains[i]->mCell == cell) return static_cast<uint8_t>(i);
        throw std::invalid_argument("Cell outside native world domain");
    }

    std::optional<CellId> InventoryService::movementCell(CellId current, Position3 position) const
    {
        if (!mBinding.mStreamExteriors) return current;
        if (!worldDomain(current)) return {};
        const auto* exterior = current.asExterior();
        if (!exterior) return current;
        constexpr int64_t size = 8192 * 1024;
        const auto grid = [](int64_t value) { return value / size - (value % size < 0); };
        const auto x = grid(position.x()), y = grid(position.y());
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767
            || std::abs(x - exterior->gridX()) > 1 || std::abs(y - exterior->gridY()) > 1) return {};
        const auto cell = CellId::exterior(exterior->worldspace(), int32_t(x), int32_t(y));
        return worldDomain(cell) ? std::optional(cell) : std::nullopt;
    }

    bool InventoryService::allowsCellTransition(CellId current, CellId requested, Position3 position) const
    {
        return mBinding.mStreamExteriors && current.asExterior() && requested.asExterior()
            && movementCell(current, position) == requested;
    }

    PlainEquipmentValues InventoryService::cellWorldValues(CellId cell, const EquipmentRuntime::PreparedWorldTransfer* prepared) const
    {
        auto result = mRuntime.worldValues(prepared);
        const auto index = worldIndex(cell);
        std::erase_if(result.mObjects, [&](const auto& object) { return mRuntime.worldCell(object.mRef.mRefNum, prepared) != index; });
        return result;
    }

    void InventoryService::synchronizeCells(const CanonicalServerState& players)
    {
        if (!mBinding.mSecondWorldItems && !mBinding.mStreamExteriors) return;
        const auto domains = mBinding.worldDomains();
        std::vector<bool> active(domains.size());
        for (const auto& session : players.activeSessions())
        {
            const auto* player = players.findPlayer(session.playerId());
            if (player && std::ranges::find(mBinding.mPlayers, player->playerId()) != mBinding.mPlayers.end()
                && worldDomain(player->transform().cell()))
            {
                const auto cell = player->transform().cell();
                active[worldIndex(cell)] = true;
                if (mBinding.mStreamExteriors && cell.asExterior())
                    for (size_t i = 0; i < domains.size(); ++i)
                        if (const auto* other = domains[i]->mCell.asExterior(); other
                            && other->worldspace() == cell.asExterior()->worldspace()
                            && std::abs(int64_t(other->gridX()) - cell.asExterior()->gridX()) <= 1
                            && std::abs(int64_t(other->gridY()) - cell.asExterior()->gridY()) <= 1)
                            active[i] = true;
            }
        }
        if (mBinding.mRetainTraveler && mBinding.mNavigatingActor)
        {
            const auto index = worldIndex(actorCell(mBinding.mNavigatingActor->snapshot()));
            // Demand is a union, not another simulation loop. An unavailable
            // path remains travel demand; only stock path completion releases it.
            active[index] = active[index] || !mBinding.mNavigatingActor->arrived();
            bool navigationActive = active[index];
            if (mBinding.mTravelerNeighborhood)
            {
                navigationActive = navigationActive || std::ranges::any_of(active, [](bool value) { return value; });
                // The declared neighborhood is one collision/navigation unit.
                // Union admission happens once below, never once per cell.
                if (navigationActive) std::fill(active.begin(), active.end(), true);
            }
            if (mBinding.mNavigationActivity) mBinding.mNavigationActivity(navigationActive);
        }
        if (mBinding.mAreaActivity) mBinding.mAreaActivity(active);
        const std::array legacy{bool(active[0]), active.size() > 1 && active[1]};
        if (mBinding.mCellActivity) mBinding.mCellActivity(legacy);
        for (size_t i = 0; i < mAreaDoors.size(); ++i)
            for (auto& report : mAreaDoors[i].reports)
                if (report)
                {
                    const auto* session = players.findActiveSession(report->session);
                    const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
                    if (!player || session->sessionGeneration() != report->generation
                        || player->transform().cell() != mBinding.mDoors[i].mCell) report.reset();
                }
        for (auto& report : mDoorReports)
            if (report)
            {
                const auto* session = players.findActiveSession(report->session);
                const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
                if (!player || session->sessionGeneration() != report->generation
                    || player->transform().cell() != mBinding.mWorldItems->mCell) report.reset();
            }
        mActiveCells = legacy;
        mActiveAreas = std::move(active);
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
        const auto& command = bound.transaction();
        if (mBinding.mNavigatingActor && (command.kind == InventoryTransactionKind::PickupItem
                || command.kind == InventoryTransactionKind::DropItem))
            throw std::invalid_argument("Frozen NPC interior does not support changing collision placements");
        (void)actor(bound.player());
        if (command.placement && (command.kind != InventoryTransactionKind::DropItem
            || !validDropPlacementView(*command.placement)))
            throw std::invalid_argument("Invalid placement command shape");
        const bool pickup = command.kind == InventoryTransactionKind::PickupItem;
        const bool drop = command.kind == InventoryTransactionKind::DropItem;
        if (pickup || drop)
        {
            const auto* player = players.findPlayer(bound.player());
            const auto* domain = player ? worldDomain(player->transform().cell()) : nullptr;
            if (!domain || !command.stackId || command.containerId || command.slot
                || command.expectedContainerRevision || command.player != bound.player() || command.count == 0
                || command.count > MaximumTransferCount
                || command.expectedInventoryRevision.value() != mWorld.getPtrRegistryRevision()
                || !positionsWithinReach(player->transform().position(), command.interactionOrigin, ReachQuanta))
                throw std::invalid_argument("World item command shape, cell or revision invalid");
            if (pickup)
            {
                const auto state = cellWorldValues(player->transform().cell());
                const auto& items = state.mObjects;
                const auto object = std::ranges::find_if(items, [&](const auto& value) {
                    return worldId(value.mRef.mRefNum, *domain) == command.stackId;
                });
                if (object == items.end() || !command.expectedWorldItemRevision
                    || command.expectedWorldItemRevision->value() != mWorld.getPtrRegistryRevision()
                    || command.count != object->mRef.mCount || mItemIds.at(object->mRef.mRefID) != command.prototypeId
                    || !positionsWithinReach(player->transform().position(), worldPosition(object->mRef), ReachQuanta)
                    || !positionsWithinReach(command.interactionOrigin, worldPosition(object->mRef), ReachQuanta))
                    throw std::invalid_argument("World pickup missing, stale or out of reach");
            }
            else
            {
                if (mBinding.mStreamExteriors
                    && cellWorldValues(player->transform().cell()).mObjects.size() >= MaximumGroundItemBaselineChunkItems)
                    throw std::invalid_argument("Native cell ground-item budget exhausted");
                if (domain->mPlacement && !command.placement)
                    throw std::invalid_argument("Stock drop requires placement view input");
                const auto native = nativeId(*command.stackId);
                const auto item = mWorld.getPtr({native.mIndex, native.mContentFile});
                if (command.expectedWorldItemRevision || item.isEmpty()
                    || item.getContainerStore() != &mRuntime.storage(actor(bound.player()))
                    || mItemIds.at(item.getCellRef().getRefId()) != command.prototypeId)
                    throw std::invalid_argument("World drop ownership or prototype invalid");
            }
            return;
        }
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
        auto retained = sealInventory(image);
        EquipmentBytes core(image.begin(), image.end());
        SealedCommitter sealed(durability, retained);
        const auto result = mRuntime.commit(command.mTransfer, sealed, success, bytes);
        if (result == PersistenceResult::Accepted)
        {
            mImage.swap(retained);
            mCoreImage.swap(core);
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
        if (mBinding.mStreamExteriors)
        {
            EquipmentBytes image;
            const auto read = readBoundedFile(path, MaximumNativeInventoryImageBytes, image, faults);
            if (read != FileReadResult::Read) return read;
            recover(std::as_bytes(std::span(image)), references);
            bytes.swap(image);
            return FileReadResult::Read;
        }
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
            auto retained = service.sealInventory(prepared.image());
            EquipmentBytes core(prepared.image().begin(), prepared.image().end());
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
            SealedCommitter sealed(sink, retained);
            const auto result = service.mRuntime.commit(prepared, sealed, success, bytes);
            if (result == PersistenceResult::Accepted)
            {
                service.mImage.swap(retained);
                service.mCoreImage.swap(core);
                service.retireCommittedEffects();
            }
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::WorldTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        CanonicalServerState players;
        ServerApp::InventoryCommandBinding binding;
        EquipmentRuntime::PreparedWorldTransfer prepared;
        WorldTransaction(InventoryService& owner, const CanonicalServerState& state,
            ServerApp::InventoryCommandBinding bound, EquipmentRuntime::PreparedWorldTransfer candidate)
            : service(owner), players(state), binding(std::move(bound)), prepared(std::move(candidate)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            service.validate(players, binding);
            auto retained = service.sealInventory(prepared.image());
            EquipmentBytes core(prepared.image().begin(), prepared.image().end());
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
            EquipmentBytes bytes;
            SealedCommitter sealed(sink, retained);
            const auto result = service.mRuntime.commit(prepared, sealed, bytes);
            if (result == PersistenceResult::Accepted)
            {
                service.mImage.swap(retained);
                service.mCoreImage.swap(core);
                service.retireCommittedEffects();
            }
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::DoorTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        EquipmentRuntime::PreparedDoor prepared;
        DoorTransaction(InventoryService& owner, EquipmentRuntime::PreparedDoor change)
            : service(owner), prepared(std::move(change)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            auto retained = service.sealInventory(prepared.image());
            EquipmentBytes core(prepared.image().begin(), prepared.image().end());
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
            EquipmentBytes bytes;
            SealedCommitter sealed(sink, retained);
            const auto result = service.mRuntime.commit(prepared, sealed, bytes);
            if (result == PersistenceResult::Accepted) { service.mImage.swap(retained); service.mCoreImage.swap(core); }
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::TeleportTransaction final : public PreparedNativeInventory
    {
        Transform mDestination;
        EquipmentBytes mBefore;
        bool mConsumed = false;
    public:
        InventoryService& service;
        TeleportTransaction(InventoryService& owner, Transform destination)
            : mDestination(destination), mBefore(owner.mImage), service(owner) {}
        std::optional<Transform> playerDestination() const override { return mDestination; }
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        {
            if (mConsumed || service.inventoryImage().empty() || service.mImage != mBefore)
                return CanonicalDurabilityResult::Rejected;
            try
            {
                const auto result = persist(service.inventoryImage());
                if (result != CanonicalDurabilityResult::Rejected) mConsumed = true;
                if (result == CanonicalDurabilityResult::Failed) service.mRuntime.mFailedClosed = true;
                return result;
            }
            catch (...) { service.mRuntime.mFailedClosed = true; return CanonicalDurabilityResult::Failed; }
        }
    };

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareDoorActivation(
        const CanonicalServerState& players, const ServerCommandProposal& proposal)
    {
        constexpr uint32_t ReachQuanta = 384 * 1024;
        const auto* input = std::get_if<InteractiveObjectCommandProposal>(&proposal.payload());
        const auto* session = players.findActiveSession(proposal.sessionId());
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (input && player && mBinding.mStreamExteriors)
        {
            for (size_t i = 0; i < mBinding.mDoors.size(); ++i)
            {
                const auto& placed = mBinding.mDoors[i];
                if (placed.mId != input->objectId().value()) continue;
                if (session->sessionGeneration() != proposal.sessionGeneration()
                    || proposal.entityPrecondition().entityId() != player->entityId()
                    || proposal.entityPrecondition().expectedAuthorityEpoch() != player->authorityEpoch()
                    || input->kind() != ObjectInteractionKind::Activate || input->requestedKey() || input->requestedTool()
                    || input->expectedInventoryRevision() || input->expectedCombatRevision()
                    || input->expectedRevision().value() != mAreaDoors[i].motion
                    || player->transform().cell() != placed.mCell || input->cell() != placed.mCell
                    || !positionsWithinReach(player->transform().position(), worldPosition(placed.mRef), ReachQuanta)
                    || !positionsWithinReach(input->interactionOrigin(), worldPosition(placed.mRef), ReachQuanta)
                    || !positionsWithinReach(player->transform().position(), input->interactionOrigin(), ReachQuanta)) return {};
                try { (void)actor(player->playerId()); return prepareAreaDoor(i, true, players, ServerTick::initial(), 0); }
                catch (const std::invalid_argument&) { return {}; }
            }
        }
        if (mBinding.mTeleportDoors && input && player)
        {
            const auto found = std::ranges::find(*mBinding.mTeleportDoors, input->objectId().value(),
                &InventoryServiceBinding::TeleportDoor::mId);
            if (found != mBinding.mTeleportDoors->end())
            {
                if (inventoryImage().empty() || session->sessionGeneration() != proposal.sessionGeneration()
                    || proposal.entityPrecondition().entityId() != player->entityId()
                    || proposal.entityPrecondition().expectedAuthorityEpoch() != player->authorityEpoch()
                    || input->kind() != ObjectInteractionKind::Activate || input->requestedKey() || input->requestedTool()
                    || input->expectedInventoryRevision() || input->expectedCombatRevision()
                    || input->expectedRevision() != ObjectRevision::initial() || input->cell() != found->mCell
                    || player->transform().cell() != found->mCell
                    || !positionsWithinReach(player->transform().position(), found->mPosition, ReachQuanta)
                    || !positionsWithinReach(input->interactionOrigin(), found->mPosition, ReachQuanta)
                    || !positionsWithinReach(player->transform().position(), input->interactionOrigin(), ReachQuanta)) return {};
                try
                {
                    (void)actor(player->playerId());
                    return std::make_unique<TeleportTransaction>(*this, found->mDestination);
                }
                catch (const std::invalid_argument&) { return {}; }
            }
        }
        if (!mBinding.mDoor || !input || !player || session->sessionGeneration() != proposal.sessionGeneration()
            || input->objectId().value() != mBinding.mDoorId || input->kind() != ObjectInteractionKind::Activate
            || input->requestedKey() || input->requestedTool() || input->expectedInventoryRevision() || input->expectedCombatRevision()
            || input->expectedRevision().value() != mRuntime.mDoorMotion
            || player->transform().cell() != mBinding.mWorldItems->mCell || input->cell() != mBinding.mWorldItems->mCell
            || !positionsWithinReach(player->transform().position(), worldPosition(*mBinding.mDoor), ReachQuanta)
            || !positionsWithinReach(input->interactionOrigin(), worldPosition(*mBinding.mDoor), ReachQuanta)
            || !positionsWithinReach(player->transform().position(), input->interactionOrigin(), ReachQuanta)) return {};
        try
        {
            (void)actor(player->playerId());
            return std::make_unique<DoorTransaction>(*this, mRuntime.prepareDoor(true, 0, false));
        }
        catch (const std::invalid_argument&) { return {}; }
    }

    void InventoryService::reportDoorObstruction(
        const CanonicalServerState& players, const ClientDoorObstruction& report, ServerTick tick)
    {
        if (mBinding.mStreamExteriors)
        {
            const auto* session = players.findActiveSession(report.session);
            const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
            if (!player || session->sessionGeneration() != report.generation || !report.sequence
                || report.observedTick > tick || tick.value() - report.observedTick.value() >= DoorObstructionLifetimeTicks) return;
            const auto actorIndex = std::ranges::find(mBinding.mPlayers, player->playerId());
            if (actorIndex == mBinding.mPlayers.end()) return;
            for (size_t i = 0; i < mBinding.mDoors.size(); ++i)
            {
                if (mBinding.mDoors[i].mId != report.placement || mBinding.mDoors[i].mCell != player->transform().cell()
                    || mAreaDoors[i].motion != report.motion || !mAreaDoors[i].state->mDoorState) continue;
                auto& previous = mAreaDoors[i].reports[size_t(actorIndex - mBinding.mPlayers.begin())];
                if (previous && previous->session == report.session && previous->generation == report.generation
                    && previous->motion == report.motion && (report.sequence <= previous->sequence
                        || report.observedTick < previous->observedTick)) return;
                previous = report;
            }
            return;
        }
        const auto* session = players.findActiveSession(report.session);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!mBinding.mDoor || !player || session->sessionGeneration() != report.generation
            || report.placement != mBinding.mDoorId || report.motion != mRuntime.mDoorMotion
            || !mRuntime.mDoorState || !mRuntime.mDoorState->mDoorState || !report.sequence
            || player->transform().cell() != mBinding.mWorldItems->mCell
            || report.observedTick > tick || tick.value() - report.observedTick.value() >= DoorObstructionLifetimeTicks) return;
        const auto found = std::ranges::find(mBinding.mPlayers, player->playerId());
        if (found == mBinding.mPlayers.end()) return;
        auto& previous = mDoorReports[size_t(found - mBinding.mPlayers.begin())];
        if (previous && previous->session == report.session && previous->generation == report.generation
            && previous->motion == report.motion && (report.sequence <= previous->sequence
                || report.observedTick < previous->observedTick)) return;
        previous = report;
    }

    bool InventoryService::doorBlocked(const CanonicalServerState& players, ServerTick tick) const
    {
        for (const auto& report : mDoorReports)
        {
            if (!report || !report->blocked || report->motion != mRuntime.mDoorMotion || report->observedTick > tick
                || tick.value() - report->observedTick.value() >= DoorObstructionLifetimeTicks) continue;
            const auto* session = players.findActiveSession(report->session);
            const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
            if (player && session->sessionGeneration() == report->generation
                && player->transform().cell() == mBinding.mWorldItems->mCell) return true;
        }
        return false;
    }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareDoorStep(
        const CanonicalServerState& players, ServerTick tick, float seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > OrdinaryDoor::MaxStepSeconds)
            throw std::invalid_argument("Native door tick outside bounds");
        mDoorStepSeconds = seconds;
        if (mBinding.mStreamExteriors) return prepareAreaDoor(0, false, players, tick, seconds);
        if (!mRuntime.mDoorState || !mRuntime.mDoorState->mDoorState) return {};
        if (mBinding.mSecondWorldItems && std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                const auto* player = players.findPlayer(session.playerId());
                return player && player->transform().cell() == mBinding.mWorldItems->mCell;
            })) return {}; // Empty interiors freeze; returning never catches up or resets the door.
        return std::make_unique<DoorTransaction>(*this, mRuntime.prepareDoor(false, seconds, doorBlocked(players, tick)));
    }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareInventory(
        const CanonicalServerState& players, const ServerCommandProposal& proposal)
    {
        const auto binding = ServerApp::InventoryCommandBinding::fromProposal(players, proposal);
        if (!binding) return {};
        try
        {
            const auto& input = binding->transaction();
            if (input.kind == InventoryTransactionKind::PickupItem || input.kind == InventoryTransactionKind::DropItem)
            {
                validate(players, *binding);
                const auto& origin = players.findPlayer(binding->player())->transform().position();
                ESM::Position position{};
                position.pos[0] = float(double(origin.x()) / 1024);
                position.pos[1] = float(double(origin.y()) / 1024);
                position.pos[2] = float(double(origin.z()) / 1024);
                std::function<ESM::Position(const ESM::ObjectState&)> placement;
                const auto cell = players.findPlayer(binding->player())->transform().cell();
                const auto& domain = *worldDomain(cell);
                const auto world = cellWorldValues(cell);
                if (input.kind == InventoryTransactionKind::DropItem && domain.mPlacement)
                {
                    const auto& transform = players.findPlayer(binding->player())->transform();
                    position.rot[2] = -float(double(transform.orientation().z().value()) / 4294967296.0 * 2 * std::numbers::pi);
                    placement = [&](const ESM::ObjectState& state) {
                        // Query the actual resulting world model (including stock gold piles),
                        // after detached inventory preparation and before image encoding.
                        MWWorld::ManualRef reference(mRuntime.mStore, state.mRef.mRefID);
                        auto ptr = reference.getPtr();
                        ptr.getCellRef() = MWWorld::CellRef(state.mRef);
                        return domain.mPlacement(position, ptr, *input.placement, world.mObjects);
                    };
                }
                auto item = nativeId(*input.stackId);
                if (input.kind == InventoryTransactionKind::PickupItem)
                    for (const auto& object : world.mObjects)
                        if (worldId(object.mRef.mRefNum, domain) == input.stackId)
                            item = EquipmentRuntime::ownedId(object.mRef.mRefNum);
                auto prepared = mRuntime.prepareWorldTransfer(actor(binding->player()), item,
                    int(input.count), input.kind == InventoryTransactionKind::PickupItem, position,
                    input.expectedInventoryRevision.value(), placement, worldIndex(cell));
                if (prepared.image().size() > MaximumNativeInventoryImageBytes)
                    throw std::invalid_argument("Native world image exceeds canonical budget");
                return std::make_unique<WorldTransaction>(*this, players, *binding, std::move(prepared));
            }
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
            : std::as_bytes(std::span(mBinding.mNavigatingActor ? mActorImage : mImage));
    }

    void InventoryService::recover(std::span<const std::byte> image, std::span<const ESM::RefId> references)
    {
        if (image.empty() || image.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory recovery image bound invalid");
        if (mBinding.mNavigatingActor)
        {
            const auto decoded = readActorCampaign({reinterpret_cast<const char*>(image.data()), image.size()});
            if (bool(decoded.melee) != bool(mMelee)
                || (decoded.melee && decoded.melee->identity != mBinding.mBoundMelee->mResourceIdentity))
                throw std::invalid_argument("Native melee resource binding differs from campaign");
            if (bool(decoded.combat) != mBinding.mCombatState)
                throw std::invalid_argument("Native combat campaign version differs from binding");
            size_t headerOffset = 0;
            const auto magic = getAreaWord({reinterpret_cast<const char*>(image.data()), image.size()}, headerOffset);
            if (mBinding.mMeleeContact != (magic == ContactActorCampaignMagic || magic == CombatActorCampaignMagic))
                throw std::invalid_argument("Native melee contact campaign version differs from binding");
            auto restoredMelee = mMelee;
            if (decoded.melee) restoredMelee->restore(decoded.melee->state);
            if (decoded.melee && decoded.melee->target
                && std::ranges::none_of(mBinding.mPlayers,
                    [&](PlayerId player) { return player.value() == decoded.melee->target; }))
                throw std::invalid_argument("Native melee target outside bound players");
            EquipmentBytes retained(reinterpret_cast<const char*>(image.data()), reinterpret_cast<const char*>(image.data()+image.size()));
            recoverAreas(std::as_bytes(decoded.inventory), references, decoded.actor);
            mMelee = std::move(restoredMelee);
            mMeleeTarget = decoded.melee ? decoded.melee->target : 0;
            mMeleeContacted = decoded.melee && decoded.melee->contact;
            mCombat = decoded.combat;
            mActorTick = decoded.tick; mActorVelocity = decoded.velocity; mActorImage.swap(retained);
            installActorPosition();
            return;
        }
        if (mBinding.mStreamExteriors) { recoverAreas(image, references); return; }
        EquipmentBytes accepted(reinterpret_cast<const char*>(image.data()), reinterpret_cast<const char*>(image.data()+image.size()));
        std::unique_ptr<const EquipmentSessionValues> values;
        EquipmentBytes output;
        mRuntime.restoreSession(std::move(accepted), references, values, output);
        mCoreImage = output;
        mImage.swap(output);
    }

    EquipmentBytes InventoryService::sealActor(std::span<const char> core, std::span<const char> actor,
        uint64_t tick, const std::array<float, 3>& velocity, const std::optional<MeleeAnimation>& melee,
        uint64_t target, bool contact, const std::optional<ActorCampaignCombat>& combat) const
    {
        const size_t meleeSize = melee ? 8 + mBinding.mBoundMelee->mResourceIdentity.size()
            + (mBinding.mMeleeContact ? 7 : 5) * 8 : 0;
        const size_t combatSize = combat ? 8 + 3 * ActorCampaignCombat::StatCount * 5 * 8 : 0;
        if (core.empty() || actor.empty() || actor.size() > 65536
            || 56 + meleeSize + combatSize + actor.size() > MaximumNativeInventoryImageBytes
            || core.size() > MaximumNativeInventoryImageBytes - 56 - meleeSize - combatSize - actor.size())
            throw std::invalid_argument("Native actor campaign exceeds bound");
        EquipmentBytes result;
        putAreaWord(result, combat ? CombatActorCampaignMagic
            : melee ? (mBinding.mMeleeContact ? ContactActorCampaignMagic : MeleeActorCampaignMagic)
            : ActorCampaignMagic);
        putAreaWord(result, core.size()); putAreaWord(result, actor.size()); putAreaWord(result, tick);
        for (float value : velocity) putAreaWord(result, std::bit_cast<uint32_t>(value));
        if (melee)
        {
            const auto& identity = mBinding.mBoundMelee->mResourceIdentity;
            const auto& state = melee->snapshot();
            putAreaWord(result, identity.size()); result.insert(result.end(), identity.begin(), identity.end());
            putAreaWord(result, uint64_t(state.mPhase));
            putAreaWord(result, std::bit_cast<uint32_t>(state.mTime));
            putAreaWord(result, std::bit_cast<uint32_t>(state.mStrength));
            putAreaWord(result, state.mReleased); putAreaWord(result, state.mHit);
            if (mBinding.mMeleeContact) { putAreaWord(result, target); putAreaWord(result, contact); }
        }
        if (combat)
        {
            putAreaWord(result, combat->rng);
            for (const auto& actorStats : combat->actors)
                for (const auto& stat : actorStats)
                    for (float value : stat) putAreaWord(result, std::bit_cast<uint32_t>(value));
        }
        result.insert(result.end(), core.begin(), core.end()); result.insert(result.end(), actor.begin(), actor.end());
        (void)readActorCampaign(result);
        return result;
    }

    void InventoryService::installActorPosition() noexcept
    {
        // The physics identity is the existing engine inventory owner. No second
        // NPC inventory or loot stream is created by the collision scene.
        const auto state = mBinding.mNavigatingActor->transform();
        const auto id = mBinding.mNavigatingActor->actorId();
        for (size_t i=0; i<mBinding.mContainers.size(); ++i)
            if (mBinding.mContainers[i].mId.value() == id)
            {
                auto position = mRuntime.ownerPtr(i+2).getRefData().getPosition();
                std::copy_n(state.begin(), 3, position.pos);
                position.rot[2] = state[3];
                mRuntime.ownerPtr(i+2).getRefData().setPosition(position);
                return;
            }
        std::terminate();
    }

    CellId InventoryService::actorCell(const ActorSceneSnapshot& state) const
    {
        const auto owner = std::ranges::find_if(mBinding.mContainers,
            [&](const auto& value) { return value.mId.value() == state.mActor; });
        if (const auto* exterior = owner->mCell.asExterior(); mBinding.mTravelerNeighborhood && exterior)
        {
            const auto x = std::floor(double(state.mPosition[0]) / 8192);
            const auto y = std::floor(double(state.mPosition[1]) / 8192);
            if (x < -32768 || x > 32767 || y < -32768 || y > 32767)
                throw std::invalid_argument("Traveler position outside cell coordinate bounds");
            const auto cell = CellId::exterior(exterior->worldspace(), int32_t(x), int32_t(y));
            if (!worldDomain(cell)) throw std::invalid_argument("Traveler outside processing neighborhood");
            return cell;
        }
        return owner->mCell;
    }

    float InventoryService::meleeReach() const
    {
        const auto id = mBinding.mNavigatingActor->actorId();
        const auto owner = std::ranges::find_if(mBinding.mContainers,
            [id](const auto& value) { return value.mId.value() == id; });
        if (owner == mBinding.mContainers.end()) throw std::invalid_argument("Native melee actor has no inventory");
        const auto values = mRuntime.installedValues(size_t(owner - mBinding.mContainers.begin()) + 2);
        const auto equipped = values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
        const ESM::Weapon* weapon = nullptr;
        if (equipped.isSet())
        {
            const auto item = std::ranges::find(values.mObjects, equipped,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == values.mObjects.end()) throw std::invalid_argument("Native melee weapon identity missing");
            if (mRuntime.mStore.find(item->mRef.mRefID) == ESM::Weapon::sRecordId)
                weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
        }
        return MWMechanics::getMeleeWeaponReach(mRuntime.mStore, weapon, true);
    }

    uint64_t InventoryService::meleeContact(const CanonicalServerState& players,
        const ActorSceneSnapshot& actor, uint64_t requested, float reach) const
    {
        const auto cell = actorCell(actor);
        const osg::Vec3f origin(actor.mPosition[0], actor.mPosition[1], actor.mPosition[2]);
        uint64_t result = 0;
        float nearest = std::numeric_limits<float>::infinity();
        for (const auto& session : players.activeSessions())
        {
            const auto* player = players.findPlayer(session.playerId());
            if (!player || std::ranges::find(mBinding.mPlayers, player->playerId()) == mBinding.mPlayers.end()
                || (requested && player->playerId().value() != requested)
                || player->transform().cell() != cell) continue;
            const auto position = player->transform().position();
            const osg::Vec3f target(float(double(position.x()) / 1024),
                float(double(position.y()) / 1024), float(double(position.z()) / 1024));
            // The inherited player mover supplies a server position, but no
            // authoritative player hull yet. Center-to-center reach is
            // conservative until the M4 movement cutover supplies that hull.
            if (!MWMechanics::isInMeleeReach(origin, target, 0, 0, reach)) continue;
            const float distance = (target - origin).length2();
            if (distance < nearest || (distance == nearest && player->playerId().value() < result))
            { nearest = distance; result = player->playerId().value(); }
        }
        return result;
    }

    EquipmentBytes InventoryService::stagedWeaponCore(int condition) const
    {
        const auto equipped = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
        if (!equipped || condition < 0 || condition > equipped->mCondition
            || mWorld.getPtrRegistryRevision() >= std::numeric_limits<size_t>::max() - 1)
            throw std::invalid_argument("Native melee weapon wear candidate invalid");
        EquipmentSessionValues values{{mRuntime.installedValues(0), mRuntime.installedValues(1)},
            mWorld.getPtrRegistryRevision() + 1};
        for (size_t i = 2; i < mRuntime.ownerCount(); ++i)
            values.mContainers.push_back(mRuntime.installedValues(i));
        auto& owner = values.mContainers.at(mCombatNpcOwner - 2);
        if (owner.mSlots[MWWorld::InventoryStore::Slot_CarriedRight] != equipped->mItem)
            throw std::invalid_argument("Native melee weapon slot changed");
        const auto item = std::ranges::find(owner.mObjects, equipped->mItem,
            [](const auto& object) { return object.mRef.mRefNum; });
        if (item == owner.mObjects.end()) throw std::invalid_argument("Native melee weapon missing");
        item->mRef.mChargeInt = condition;
        if (condition == 0) owner.mSlots[MWWorld::InventoryStore::Slot_CarriedRight] = {};
        EquipmentBytes core;
        mRuntime.encodeSession(std::move(values), core);
        return core;
    }

    std::optional<ServerApp::NativeTravelDiagnostics> InventoryService::travelDiagnostics() const
    {
        if (!mBinding.mRetainTraveler) return {};
        auto result = mTravelDiagnostics;
        result.tick = mActorTick;
        result.position = mBinding.mNavigatingActor->snapshot().mPosition;
        result.completed = mBinding.mNavigatingActor->arrived();
        result.cellLimit = mBinding.mTravelerCellBudget;
        result.stepLimit = mBinding.mTravelerStepBudget;
        return result;
    }

    class InventoryService::ActorTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        std::unique_ptr<PreparedNativeInventory> command;
        std::unique_ptr<InteriorActorScene::Prepared> actor;
        std::optional<MeleeAnimation> melee;
        std::optional<ActorCampaignCombat> combat;
        std::optional<EquipmentRuntime::EquippedWeaponCondition> weapon;
        std::optional<int> wornCondition;
        EquipmentBytes wornCore, wornInventory;
        uint64_t target;
        bool contact;
        EquipmentBytes before;
        uint64_t tick;
        std::array<float, 3> velocity;
        bool consumed = false;
        ServerApp::NativeTravelDiagnostics diagnostics;
        ActorTransaction(InventoryService& owner, std::unique_ptr<PreparedNativeInventory> input,
            std::unique_ptr<InteriorActorScene::Prepared> step, std::optional<MeleeAnimation> swing,
            uint64_t selected, bool contacted, std::optional<ActorCampaignCombat> stagedCombat,
            std::optional<EquipmentRuntime::EquippedWeaponCondition> stagedWeapon,
            std::optional<int> condition, EquipmentBytes core,
            uint64_t time, std::array<float,3> motion,
            ServerApp::NativeTravelDiagnostics report)
            : service(owner), command(std::move(input)), actor(std::move(step)), melee(std::move(swing)),
              combat(std::move(stagedCombat)), weapon(std::move(stagedWeapon)),
              wornCondition(condition), wornCore(std::move(core)), target(selected), contact(contacted), before(owner.mActorImage),
              tick(time), velocity(motion), diagnostics(report) {}
        bool changesInventory() const noexcept override { return bool(command) || wornCondition.has_value(); }
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        {
            if (consumed || service.inventoryImage().empty() || before != service.mActorImage
                || (actor && !service.mBinding.mNavigatingActor->canInstall(*actor))) return CanonicalDurabilityResult::Rejected;
            try
            {
                if (combat && weapon != service.mRuntime.equippedWeaponCondition(service.mCombatNpcOwner))
                    return CanonicalDurabilityResult::Rejected;
                EquipmentBytes sealed;
                const auto compose = [&](std::span<const std::byte> inventory) {
                    const auto retained = readActorCampaign(before).actor;
                    const std::span<const char> candidate(reinterpret_cast<const char*>(inventory.data()), inventory.size());
                    if (wornCondition) wornInventory = service.replaceAreaCore(candidate, wornCore);
                    const auto selected = wornCondition ? std::span<const char>(wornInventory) : candidate;
                    sealed = service.sealActor(selected,
                        actor ? actor->image() : retained, tick, velocity, melee, target, contact, combat);
                    return persist(std::as_bytes(std::span(sealed)));
                };
                const auto result = command ? command->commit(compose) : compose(std::as_bytes(std::span(service.mImage)));
                if (result == CanonicalDurabilityResult::Rejected) return result;
                consumed = true;
                if (result == CanonicalDurabilityResult::Failed) service.mRuntime.mFailedClosed = true;
                else
                {
                    if (wornCondition)
                    {
                        service.mRuntime.installWeaponWear(service.mCombatNpcOwner, weapon->mItem, *wornCondition);
                        service.mCoreImage.swap(wornCore);
                        service.mImage.swap(wornInventory);
                    }
                    if (actor) service.mBinding.mNavigatingActor->install(*actor);
                    service.mMelee = std::move(melee);
                    service.mMeleeTarget = target; service.mMeleeContacted = contact;
                    service.mCombat = std::move(combat);
                    service.mActorTick = tick; service.mActorVelocity = velocity;
                    service.mActorImage.swap(sealed);
                    service.installActorPosition();
                    if (service.mBinding.mRetainTraveler)
                    {
                        const bool changed = diagnostics.status != service.mTravelDiagnostics.status;
                        service.mTravelDiagnostics = diagnostics;
                        if (changed || tick % 30 == 0)
                        {
                            const auto report = *service.travelDiagnostics();
                            const char* statuses[]{"idle", "running", "cell-capacity", "step-capacity", "boundary", "no-path"};
                            std::fprintf(stderr, "native travel committed: tick=%llu status=%s cells=%zu/%zu steps=%zu/2 completed=%d position=%.6f,%.6f,%.6f\n",
                                static_cast<unsigned long long>(tick), statuses[size_t(report.status)], report.demandedCells,
                                report.cellLimit, report.stepLimit, int(report.completed), report.position[0], report.position[1], report.position[2]);
                        }
                    }
                }
                return result;
            }
            catch (...) { service.mRuntime.mFailedClosed = true; return CanonicalDurabilityResult::Failed; }
        }
    };

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareNativeTick(const CanonicalServerState& players,
        ServerTick tick, float seconds, std::unique_ptr<PreparedNativeInventory> command)
    try
    {
        if (!mBinding.mNavigatingActor) return command ? std::move(command) : prepareDoorStep(players, tick, seconds);
        if (!std::isfinite(seconds) || std::abs(seconds - 1.f/30.f) > 1e-6f || tick.value() <= mActorTick)
            throw std::invalid_argument("Native actor requires increasing 30 Hz durable ticks");
        if (!mBinding.mDoors.empty()) command = prepareAreaDoor(0, false, players, tick, seconds, std::move(command));
        else if (!command) command = prepareDoorStep(players, tick, seconds);
        const auto before = mBinding.mNavigatingActor->snapshot();
        bool active = mBinding.mRetainTraveler && !mBinding.mNavigatingActor->arrived();
        for (const auto& session : players.activeSessions())
            if (const auto* player = players.findPlayer(session.playerId()); player
                && (mBinding.mTravelerNeighborhood ? sharesCellNeighborhood(player->transform().cell(), actorCell(before))
                                                  : player->transform().cell() == actorCell(before))) active = true;
        using Diagnostics = ServerApp::NativeTravelDiagnostics;
        Diagnostics report;
        report.status = active ? Diagnostics::Status::Running : Diagnostics::Status::Idle;
        report.demandedCells = active ? (mBinding.mTravelerNeighborhood ? mBinding.worldDomains().size() : 1) : 0;
        if (mBinding.mTravelerNeighborhood && active)
        {
            if (report.demandedCells > mBinding.mTravelerCellBudget) report.status = Diagnostics::Status::CellCapacity;
            else if (mBinding.mTravelerStepBudget < 2) report.status = Diagnostics::Status::StepCapacity;
            active = report.status == Diagnostics::Status::Running;
        }
        const auto doors = actorDoorFrames(command.get());
        auto step = active ? mBinding.mNavigatingActor->prepareNavigation(mBinding.mNavigationSpeed, doors)
            : nullptr;
        if (step && mBinding.mTravelerNeighborhood && !mBinding.mNavigatingActor->contains(step->snapshot().mPosition))
        { step.reset(); report.status = Diagnostics::Status::Boundary; }
        if (active && !step) report.status = Diagnostics::Status::Boundary;
        else if (step && step->pathUnavailable()) report.status = Diagnostics::Status::NoPath;
        const auto after = step ? step->snapshot() : before;
        auto melee = mMelee;
        auto combat = mCombat;
        std::optional<int> wornCondition;
        EquipmentBytes wornCore;
        uint64_t target = mMeleeTarget;
        bool contact = mMeleeContacted;
        if (step && melee)
        {
            if (mBinding.mMeleeContact && !melee->snapshot().mReleased && melee->windUp() >= 1.f)
            {
                const auto selected = meleeContact(players, after, 0, meleeReach());
                if (selected && melee->release(std::clamp(melee->windUp(), 0.f, 1.f))) target = selected;
            }
            const auto hit = melee->advance(seconds);
            if (mBinding.mMeleeContact && hit && target)
                contact = meleeContact(players, after, target, meleeReach()) == target;
            if (hit && combat && mBinding.mCombatResolution)
            {
                const auto* input = areaDoorCommand(command.get());
                if (dynamic_cast<const Transaction*>(input) || dynamic_cast<const EquipmentTransaction*>(input)
                    || dynamic_cast<const WorldTransaction*>(input))
                    throw std::invalid_argument("Native melee hit conflicts with an inventory command");
                auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[2]);
                const auto held = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
                const auto values = mRuntime.installedValues(mCombatNpcOwner);
                const ESM::Weapon* weapon = nullptr;
                if (held)
                {
                    const auto item = std::ranges::find(values.mObjects, held->mItem,
                        [](const auto& object) { return object.mRef.mRefNum; });
                    if (item == values.mObjects.end()) throw std::invalid_argument("Native melee weapon identity missing");
                    weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
                }
                const float strength = melee->snapshot().mStrength;
                const float capacity = attacker.getAttribute(ESM::Attribute::Strength).getModified()
                    * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
                const float weight = std::max(0.f, mRuntime.storage(mCombatNpcOwner).getWeight());
                const float encumbrance = weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity;
                MWMechanics::applyFatigueLoss(attacker, mRuntime.mStore,
                    weapon ? weapon->mData.mWeight : 0.f, strength, encumbrance);
                if (contact)
                {
                    const size_t victimIndex = mBinding.mPlayers[0].value() == target ? 0 : 1;
                    auto victim = loadCombatStats(mRuntime.mStore, combat->actors[victimIndex]);
                    const auto skill = weapon ? MWMechanics::getWeaponType(weapon->mData.mType)->mSkill
                        : ESM::Skill::HandToHand;
                    const int skillValue = int(attacker.getSkill(skill).getModified());
                    const bool paralyzed = victim.getMagicEffects()
                        .getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() > 0;
                    const float chance = MWMechanics::getHitChance(mRuntime.mStore, attacker, victim,
                        skillValue, false, paralyzed);
                    Misc::Rng::Generator rng;
                    Misc::Rng::deserialize(std::to_string(combat->rng), rng);
                    const bool success = Misc::Rng::roll0to99(rng) < chance;
                    combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    float damage = 0;
                    if (success && weapon)
                    {
                        const auto& range = *hit == ESM::Weapon::AT_Chop ? weapon->mData.mChop
                            : *hit == ESM::Weapon::AT_Slash ? weapon->mData.mSlash : weapon->mData.mThrust;
                        damage = range[0] + (range[1] - range[0]) * strength;
                        TES3MP::OpenMwMeleeSettings settings;
                        const auto& gmst = mRuntime.mStore.get<ESM::GameSetting>();
                        settings.damageStrengthBase = gmst.find("fDamageStrengthBase")->mValue.getFloat();
                        settings.damageStrengthMultiplier = gmst.find("fDamageStrengthMult")->mValue.getFloat();
                        damage = TES3MP::openMwAdjustedWeaponDamage(settings,
                            attacker.getAttribute(ESM::Attribute::Strength).getModified(),
                            weapon->mData.mHealth ? float(held->mCondition) / weapon->mData.mHealth : 1.f,
                            weapon->mData.mHealth != 0, damage);
                        MWMechanics::applyHitDamage(victim, {{"health", damage}}, MWWorld::TimeStamp{});
                    }
                    if (weapon && weapon->mData.mHealth)
                    {
                        const float multiplier = mRuntime.mStore.get<ESM::GameSetting>()
                            .find("fWeaponDamageMult")->mValue.getFloat();
                        wornCondition = MWMechanics::weaponConditionAfterHit(held->mCondition, damage, success, multiplier);
                        wornCore = stagedWeaponCore(*wornCondition);
                    }
                    saveCombatStats(combat->actors[victimIndex], victim);
                }
                saveCombatStats(combat->actors[2], attacker);
            }
        }
        std::array<float,3> velocity;
        for (size_t i=0; i<3; ++i) velocity[i]=(after.mPosition[i]-before.mPosition[i])*30;
        return std::make_unique<ActorTransaction>(*this, std::move(command), std::move(step),
            std::move(melee), target, contact, std::move(combat),
            mCombat ? mRuntime.equippedWeaponCondition(mCombatNpcOwner) : std::nullopt,
            wornCondition, std::move(wornCore),
            tick.value(), velocity, report);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "native travel preparation failed: tick=%llu previous=%llu reason=%s\n",
            static_cast<unsigned long long>(tick.value()), static_cast<unsigned long long>(mActorTick), error.what());
        throw;
    }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::projectInventory(
        const CanonicalServerState& players, SessionId target, ServerTick tick, CanonicalRevision revision,
        const PreparedNativeInventory* candidate) const
    {
        const auto* moving = dynamic_cast<const ActorTransaction*>(candidate);
        if (moving && (&moving->service != this || moving->consumed || moving->before != mActorImage
            || (moving->actor && !mBinding.mNavigatingActor->canInstall(*moving->actor)))) return {};
        if (moving) candidate = moving->command.get();
        const auto* areaDoors = candidate;
        if (ownsAreaDoorCandidate(candidate)) candidate = areaDoorCommand(candidate);
        const auto* transaction = dynamic_cast<const Transaction*>(candidate);
        const auto* equipment = dynamic_cast<const EquipmentTransaction*>(candidate);
        const auto* world = dynamic_cast<const WorldTransaction*>(candidate);
        const auto* door = dynamic_cast<const DoorTransaction*>(candidate);
        const auto* teleport = dynamic_cast<const TeleportTransaction*>(candidate);
        if (candidate && ((!transaction && !equipment && !world && !door && !teleport && !ownsAreaDoorCandidate(candidate))
                || (teleport && &teleport->service != this) || (door && &door->service != this)
                || (world && &world->service != this) || (transaction && &transaction->service != this)
                || (equipment && &equipment->service != this))) return std::nullopt;
        const auto actorState = mBinding.mNavigatingActor ? std::optional(moving && moving->actor
            ? moving->actor->snapshot() : mBinding.mNavigatingActor->snapshot()) : std::nullopt;
        auto result = project(players, target, tick, revision, transaction ? &transaction->prepared : nullptr,
            equipment ? &equipment->prepared : nullptr, world ? &world->prepared : nullptr, door ? &door->prepared : nullptr,
            {}, actorState ? &*actorState : nullptr, moving ? moving->wornCondition : std::nullopt);
        if (result)
            for (auto& ground : result->groundItems)
            {
                ground.doors = areaDoorSnapshots(ground.cell, areaDoors);
                for (auto& neighbor : ground.neighbors) neighbor.doors = areaDoorSnapshots(neighbor.cell, areaDoors);
            }
        if (result && result->equipment && mBinding.mNavigatingActor)
        {
            const auto state = moving && moving->actor ? moving->actor->snapshot() : mBinding.mNavigatingActor->snapshot();
            if (std::ranges::any_of(result->equipment->actors, [&](const auto& owner) { return owner.actor.value() == state.mActor; }))
                result->equipment->motions.push_back({state.mActor, moving ? moving->tick : std::max<uint64_t>(1, mActorTick),
                    state.mPosition, moving ? moving->velocity : mActorVelocity, state.mYaw});
        }
        return result;
    }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::project(const CanonicalServerState& players,
        SessionId target, ServerTick tick, CanonicalRevision revision, const PreparedCommand* candidate,
        const EquipmentRuntime::PreparedEquipment* equipped, const EquipmentRuntime::PreparedWorldTransfer* world,
        const EquipmentRuntime::PreparedDoor* door, std::optional<CellId> area,
        const ActorSceneSnapshot* moving, std::optional<int> wornCondition) const
    try
    {
        if (mRuntime.mRestartActor || mRuntime.mFailedClosed) return std::nullopt;
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player) return std::nullopt;
        const auto visibleCell = area.value_or(player->transform().cell());
        const auto index = actor(player->playerId());
        const auto values = [&](size_t owner) {
            auto state = world ? mRuntime.preparedValues(*world, owner)
                : equipped ? mRuntime.preparedValues(*equipped, owner)
                : candidate ? mRuntime.preparedValues(candidate->mTransfer, owner) : mRuntime.installedValues(owner);
            if (wornCondition && owner == mCombatNpcOwner)
            {
                const auto weapon = mRuntime.equippedWeaponCondition(owner);
                if (!weapon) throw std::invalid_argument("Native melee projection lost equipped weapon");
                const auto item = std::ranges::find(state.mObjects, weapon->mItem,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == state.mObjects.end()) throw std::invalid_argument("Native melee projection lost weapon identity");
                item->mRef.mChargeInt = *wornCondition;
                if (*wornCondition == 0) state.mSlots[MWWorld::InventoryStore::Slot_CarriedRight] = {};
            }
            return state;
        };
        const auto installed = values(index);
        const auto items = stacks(installed, mItemIds, mRuntime.mStore);
        std::vector<EquipmentBinding> equipment;
        static_assert(static_cast<int>(EquipmentSlot::Count) == InventoryStore::Slots);
        for (int slot = 0; slot < InventoryStore::Slots; ++slot)
            if (installed.mSlots[slot].isSet())
                equipment.push_back({static_cast<EquipmentSlot>(slot), wireId(installed.mSlots[slot])});
        const auto version = wornCondition ? mWorld.getPtrRegistryRevision() + 1
            : world ? world->revision() : equipped ? equipped->candidate().mRevision
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
            // Keep the authored placement controlled while its origin is visible,
            // too: otherwise a late observer could render its local frozen copy.
            // V20's fixed neighborhood always retains that origin cell.
            if (visibleCell != shared.mCell
                && !(moving && shared.mId.value() == moving->mActor && visibleCell == actorCell(*moving))) continue;
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
        std::vector<GroundItemInterestMember> groundItems;
        std::vector<uint64_t> placements;
        std::vector<GroundItemPresentation> presentation;
        const auto* domain = worldDomain(visibleCell);
        if (domain)
        {
            for (const auto& [id, ref] : domain->mPlacements) placements.push_back(id);
            const auto state = cellWorldValues(visibleCell, world);
            for (const auto& stack : stacks(state, mItemIds, mRuntime.mStore, domain))
            {
                const auto& ref = std::ranges::find_if(state.mObjects, [&](const auto& value) {
                    return worldId(value.mRef.mRefNum, *domain) == stack.stackId;
                })->mRef;
                groundItems.push_back({stack, worldPosition(ref), WorldItemRevision::fromValue(version).value()});
                presentation.push_back({stack.stackId, {ref.mPos.rot[0], ref.mPos.rot[1], ref.mPos.rot[2]}, ref.mScale});
            }
        }
        std::optional<NativeDoorSnapshot> doorView;
        if (mBinding.mDoor && visibleCell == mBinding.mWorldItems->mCell)
        {
            const auto& state = door ? door->state() : *mRuntime.mDoorState;
            doorView = NativeDoorSnapshot{mBinding.mDoorId, door ? door->motion() : mRuntime.mDoorMotion,
                state.mPosition.rot[2], mDoorStepSeconds, uint8_t(state.mDoorState), door ? door->blocked() : mRuntime.mDoorBlocked};
        }
        std::vector<uint64_t> teleports;
        if (mBinding.mTeleportDoors)
            for (const auto& teleport : *mBinding.mTeleportDoors)
                if (teleport.mCell == visibleCell) teleports.push_back(teleport.mId);
        std::ranges::sort(teleports);
        auto ground = ReliableGroundItemBaseline::create(header, visibleCell, groundItems, placements, presentation,
            domain != nullptr, doorView, teleports, areaDoorSnapshots(visibleCell, nullptr), {},
            domain ? std::span<const NativeActorSpawn>(domain->mActorSpawns) : std::span<const NativeActorSpawn>{});
        if (!std::holds_alternative<ReliableGroundItemBaseline>(ground)) return std::nullopt;
        result.groundItems.push_back(std::get<ReliableGroundItemBaseline>(std::move(ground)));
        std::vector<PublicEquipmentMember> visible;
        for (size_t i = 0; i < 2; ++i)
            if (const auto* other = players.findPlayer(mBinding.mPlayers[i]);
                other && other->transform().cell() == visibleCell)
            {
                visible.push_back({other->playerId(), publicSlots(values(i))});
            }
        if (mBinding.mStreamExteriors && !area && visibleCell.asExterior())
        {
            for (const auto* domain : mBinding.worldDomains())
            {
                const auto* exterior = domain->mCell.asExterior();
                if (!exterior || domain->mCell == visibleCell || exterior->worldspace() != visibleCell.asExterior()->worldspace()
                    || std::abs(int64_t(exterior->gridX()) - visibleCell.asExterior()->gridX()) > 1
                    || std::abs(int64_t(exterior->gridY()) - visibleCell.asExterior()->gridY()) > 1) continue;
                auto neighbor = project(players, target, tick, revision, candidate, equipped, world, door, domain->mCell, moving);
                if (!neighbor || neighbor->groundItems.size() != 1 || !neighbor->equipment) return std::nullopt;
                result.groundItems.front().neighbors.push_back(std::move(neighbor->groundItems.front()));
                actors.insert(actors.end(), neighbor->equipment->actors.begin(), neighbor->equipment->actors.end());
                visible.insert(visible.end(), neighbor->equipment->members.begin(), neighbor->equipment->members.end());
            }
            std::ranges::sort(result.groundItems.front().neighbors, {}, &ReliableGroundItemBaseline::cell);
        }
        std::ranges::sort(visible, {}, &PublicEquipmentMember::player);
        std::ranges::sort(actors, {}, &PublicActorEquipmentMember::actor);
        actors.erase(std::unique(actors.begin(), actors.end(), [](const auto& a, const auto& b) {
            return a.actor == b.actor;
        }), actors.end());
        auto publicEquipment = LatestWinsEquipmentSnapshot::create(target, session->sessionGeneration(), tick, revision, visible, actors);
        if (!std::holds_alternative<LatestWinsEquipmentSnapshot>(publicEquipment)) return std::nullopt;
        result.equipment = std::get<LatestWinsEquipmentSnapshot>(std::move(publicEquipment));
        return result;
    }
    catch (...) { return std::nullopt; }
}
