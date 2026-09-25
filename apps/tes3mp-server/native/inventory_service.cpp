#include <numbers>
#include "inventory_service.hpp"
#include "actor_inventory.hpp"
#include "actor_campaign.hpp"
#include "magic_runtime.hpp"
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containeradd.hpp>
#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
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
        uint64_t spellRecordId(ESM::RefId id)
        {
            if (!id.is<ESM::StringRefId>()) return 0;
            uint64_t hash = 14695981039346656037ull;
            for (unsigned char c : id.getRefIdString())
            {
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c >= 128) return 0;
                hash = (hash ^ c) * 1099511628211ull;
            }
            return hash;
        }

        const ESM::Enchantment* enchantmentBySource(const MWWorld::ESMStore& content, uint64_t source)
        {
            const ESM::Enchantment* result = nullptr;
            const auto& records = content.get<ESM::Enchantment>();
            for (auto it = records.begin(); it != records.end(); ++it)
                if (spellRecordId(it->mId) == source)
                {
                    if (result) return nullptr;
                    result = &*it;
                }
            return result;
        }

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
        void addTimedResistance(MWMechanics::NpcStats& stats,
            std::span<const ActorCampaignTimedEffect> effects, size_t actor)
        {
            for (const auto& effect : effects)
                if (effect.actor == actor)
                    stats.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::ResistMagicka),
                        MWMechanics::EffectParam(effect.magnitude));
        }
        void stageTimedResistance(const PreparedInstantEffects& plan, int range, size_t actor,
            uint64_t tick, std::vector<ActorCampaignTimedEffect>& effects)
        {
            for (const auto& effect : plan.effects)
            {
                if (effect.mRange != range || effect.mEffectID != ESM::MagicEffect::ResistMagicka) continue;
                if (effects.size() >= MaximumActorTimedEffects
                    || tick > UINT64_MAX - uint64_t(effect.mDuration) * 30)
                    throw std::invalid_argument("Native timed effect capacity or deadline exhausted");
                effects.push_back({actor, float(effect.mMagnMin), tick + uint64_t(effect.mDuration) * 30});
            }
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
            if (mBinding.mNpcLifecycle)
            {
                if (!mCombat || mCombat->actors[2][8][2] <= 0 || !mBinding.mNpcRespawnDelayTicks
                    || mBinding.mNpcRespawnDelayTicks > 30ull * 60 * 60 * 24)
                    throw std::invalid_argument("Native NPC lifecycle requires a living placed actor and bounded delay");
                mRespawnInventory = mRuntime.installedValues(mCombatNpcOwner);
                std::vector<ESM::RefId> ids;
                for (const auto& object : mRespawnInventory.mObjects)
                    for (auto ref : {object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul,
                             object.mRef.mFaction, object.mRef.mKey, object.mRef.mTrap})
                        if (!ref.empty()) ids.push_back(ref);
                const auto envelope = mRuntime.expectedEnvelope(mRespawnInventory.mActor);
                ActorCampaignLife life;
                life.spawnStats = mCombat->actors[2];
                life.spawnActor = mBinding.mNavigatingActor->image();
                encodeEquipment(mRespawnInventory, {envelope, mRuntime.mStore, ids, mRuntime.mScriptLocals, true},
                    life.spawnInventory);
                mLife = std::move(life);
            }
            mActorImage = sealActor(mImage, mBinding.mNavigatingActor->image(), mActorTick, mActorVelocity,
                mMelee, mMeleeTarget, mMeleeContacted, mCombat, mLife, mProjectiles, mTimedEffects);
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
            active[index] = active[index] || !mProjectiles.empty()
                || (!mBinding.mNavigatingActor->arrived()
                    && (!mCombat || mCombat->actors[2][8][2] > 0));
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
        const bool selectedCorpse = mCombat && sharedIndex + 2 == mCombatNpcOwner
            && mCombat->actors[2][8][2] <= 0;
        if (actorInventory(sharedOwner) && !initialCorpse(sharedOwner) && !selectedCorpse)
            throw std::invalid_argument("Living actor inventory access requires theft/companion services");
        const auto& player = *players.findPlayer(bound.player());
        const auto revision = mWorld.getPtrRegistryRevision();
        CellId sharedCell = shared.mCell;
        Position3 sharedPosition = shared.mPosition;
        if (selectedCorpse)
        {
            const auto state = mBinding.mNavigatingActor->snapshot();
            sharedCell = actorCell(state);
            sharedPosition = Position3(int64_t(std::llround(double(state.mPosition[0]) * 1024)),
                int64_t(std::llround(double(state.mPosition[1]) * 1024)),
                int64_t(std::llround(double(state.mPosition[2]) * 1024)));
        }
        if (command.player != bound.player()
            || !command.stackId || command.slot
            || command.expectedWorldItemRevision || command.count == 0 || command.count > MaximumTransferCount
            || (command.kind == InventoryTransactionKind::TakeAllFromContainer && command.count != 1)
            || command.expectedInventoryRevision.value() != revision || !command.expectedContainerRevision
            || command.expectedContainerRevision->value() != revision
            || (command.kind != InventoryTransactionKind::PutIntoContainer
                && command.kind != InventoryTransactionKind::TakeFromContainer
                && command.kind != InventoryTransactionKind::TakeAllFromContainer)
            || player.transform().cell() != sharedCell
            || !positionsWithinReach(player.transform().position(), sharedPosition, ReachQuanta)
            || !positionsWithinReach(command.interactionOrigin, sharedPosition, ReachQuanta)
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
        const auto openedCorpse = mCombat && mCombat->actors[2][8][2] <= 0
            ? std::optional(mCombatNpcOwner) : std::nullopt;
        return PreparedCommand(bound, mRuntime.prepare({ command.mInitiator }, command, openedCorpse));
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

    class InventoryService::AttackTransaction final : public PreparedNativeInventory
    {
    public:
        ClientMeleeAttackCommand attack;
        PlayerId attacker;
        AttackTransaction(ClientMeleeAttackCommand input, PlayerId player)
            : attack(std::move(input)), attacker(player) {}
        bool changesInventory() const noexcept override { return false; }
        // The actor tick owns persistence. A bare attack is never a durability candidate.
        CanonicalDurabilityResult commit(const NativeInventoryCommit&) noexcept override
        { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::SpellTransaction final : public PreparedNativeInventory
    {
    public:
        ClientMagicUseCommand use;
        PlayerId caster;
        PreparedInstantSpell spell;
        std::optional<ItemCharge> charge;
        uint64_t effectSource = 0;
        SpellTransaction(ClientMagicUseCommand input, PlayerId player, PreparedInstantSpell record,
            std::optional<ItemCharge> spent = {}, uint64_t effect = 0)
            : use(std::move(input)), caster(player), spell(std::move(record)),
              charge(std::move(spent)), effectSource(effect) {}
        bool changesInventory() const noexcept override { return false; }
        CanonicalDurabilityResult commit(const NativeInventoryCommit&) noexcept override
        { return CanonicalDurabilityResult::Rejected; }
    };

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareMagicUse(
        const CanonicalServerState& players, const ServerCommandProposal& proposal, ServerTick tick)
    {
        if (!mBinding.mMagicUse || !mCombat || !mBinding.mNavigatingActor) return {};
        const auto* input = std::get_if<MagicUseCommandProposal>(&proposal.payload());
        if (!input) return {};
        const auto& use = input->command();
        const auto* session = players.findActiveSession(proposal.sessionId());
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player || session->sessionGeneration() != proposal.sessionGeneration()
            || use.sessionId != proposal.sessionId() || use.sessionGeneration != proposal.sessionGeneration()
            || std::ranges::find(mBinding.mPlayers, player->playerId()) == mBinding.mPlayers.end()
            || player->transform().cell() != actorCell(mBinding.mNavigatingActor->snapshot())
            || (use.sourceKind != MagicUseSourceKind::Spell
                && (use.sourceKind != MagicUseSourceKind::EnchantedItem || !mBinding.mMagicItemUse))
            || !use.sourceId || (mBinding.mMagicProjectileCollection && !use.commandId.value())
            || (mBinding.mMagicProjectileCollection
                ? ((use.targetKind != MagicUseTargetKind::Self
                        && mProjectiles.size() >= MaximumActorProjectiles)
                    || std::ranges::any_of(mProjectiles, [&](const auto& pending) {
                        return pending.caster == player->playerId().value()
                            && pending.commandId == use.commandId.value();
                    }))
                : !mProjectiles.empty())
            || (use.targetKind != MagicUseTargetKind::Self
                && (!mBinding.mMagicProjectile || !mLife
                    || (use.targetKind == MagicUseTargetKind::Actor
                        ? (use.targetId != mBinding.mNavigatingActor->actorId()
                            || mLife->respawnTick || use.sourceServerTick.value() < mLife->bornTick
                            || use.expectedTargetRevision.value() < mLife->bornTick)
                        : (use.targetKind != MagicUseTargetKind::Player || !mBinding.mMagicPlayerTarget
                            || use.targetId == player->playerId().value()
                            || std::ranges::none_of(mBinding.mPlayers, [&](PlayerId id) {
                                const auto* target = players.findPlayer(id);
                                return id.value() == use.targetId && target
                                    && target->transform().cell() == actorCell(mBinding.mNavigatingActor->snapshot())
                                    && std::ranges::any_of(players.activeSessions(), [&](const auto& active) {
                                        return active.playerId() == id;
                                    }) && mCombat->actors[actor(id)][8][2] > 0;
                            })))))
            || (use.targetKind == MagicUseTargetKind::Self && use.targetId)
            || use.sourceServerTick.value() > tick.value()
            || tick.value() - use.sourceServerTick.value() > 64
            || (use.targetKind == MagicUseTargetKind::Self
                && use.expectedCasterRevision != use.expectedTargetRevision)
            || use.expectedCasterRevision.value() > tick.value()
            || use.expectedTargetRevision.value() > tick.value()
            || mCombat->actors[actor(player->playerId())][8][2] <= 0)
            return {};
        if (use.sourceKind == MagicUseSourceKind::EnchantedItem)
        {
            if (use.expectedInventoryRevision.value() != mWorld.getPtrRegistryRevision()) return {};
            const size_t owner = actor(player->playerId());
            const auto values = mRuntime.installedValues(owner);
            const auto item = std::ranges::find_if(values.mObjects, [&](const auto& object) {
                return object.mRef.mCount > 0 && wireId(object.mRef.mRefNum).value() == use.sourceId;
            });
            if (item == values.mObjects.end()) return {};
            MWWorld::ManualRef reference(mRuntime.mStore, item->mRef.mRefID);
            auto ptr = reference.getPtr();
            const auto enchantId = ptr.getClass().getEnchantment(ptr);
            const auto* enchantment = enchantId.empty() ? nullptr : mRuntime.mStore.get<ESM::Enchantment>().search(enchantId);
            if (!enchantment || (enchantment->mData.mType != ESM::Enchantment::WhenUsed
                    && enchantment->mData.mType != ESM::Enchantment::CastOnce)) return {};
            const uint64_t effectSource = spellRecordId(enchantId);
            if (!effectSource || enchantmentBySource(mRuntime.mStore, effectSource) != enchantment) return {};
            auto effects = prepareInstantEffects(enchantment->mEffects, mRuntime.mStore);
            if (!effects || std::ranges::any_of(effects->effects,
                    [&](const auto& effect) { return effect.mArea != 0
                        && (!mBinding.mMagicArea || effect.mRange != ESM::RT_Target); })
                || (!mBinding.mMagicTimed && std::ranges::any_of(effects->effects,
                    [](const auto& effect) { return effect.mDuration != 0; }))
                || (use.targetKind == MagicUseTargetKind::Self
                    ? !effects->onlyRange(ESM::RT_Self)
                    : !effects->hasRange(ESM::RT_Target) || effects->hasRange(ESM::RT_Touch))) return {};
            if (enchantment->mData.mType == ESM::Enchantment::CastOnce)
            {
                // Consume one item at launch. The remaining stack keeps its identity;
                // the pending effect retains the enchantment record independently.
                if (!ptr.getClass().getScript(ptr).empty()) return {};
                PreparedInstantSpell prepared{0, std::move(*effects)};
                return std::make_unique<SpellTransaction>(use, player->playerId(), std::move(prepared),
                    ItemCharge{owner, item->mRef.mRefNum, item->mRef.mEnchantmentCharge,
                        item->mRef.mEnchantmentCharge, true}, effectSource);
            }
            const auto caster = loadCombatStats(mRuntime.mStore, mCombat->actors[owner]);
            const float baseCost = MWMechanics::getEnchantmentCastCost(*enchantment, mRuntime.mStore);
            if (!std::isfinite(baseCost) || baseCost < 0 || baseCost > 1'000'000) return {};
            const int cost = MWMechanics::getEffectiveEnchantmentCastCost(baseCost,
                caster.getSkill(ESM::Skill::Enchant).getModified());
            const int maximum = MWMechanics::getEnchantmentCharge(*enchantment, mRuntime.mStore);
            const float before = item->mRef.mEnchantmentCharge;
            const float available = before == -1.f ? float(maximum) : before;
            if (cost < 1 || maximum < 1 || maximum > 1'000'000 || !std::isfinite(available)
                || available < cost || available > maximum) return {};
            PreparedInstantSpell prepared{0, std::move(*effects)};
            return std::make_unique<SpellTransaction>(use, player->playerId(), std::move(prepared),
                ItemCharge{owner, item->mRef.mRefNum, before, available - cost}, effectSource);
        }
        const auto& known = mRuntime.mStore.get<ESM::NPC>().find(mBinding.mActors[actor(player->playerId())].mBase)->mSpells.mList;
        const ESM::Spell* selected = nullptr;
        for (const auto& id : known)
            if (!id.empty() && spellRecordId(id) == use.sourceId)
            {
                if (selected) return {}; // Hash collision cannot grant another spell.
                selected = mRuntime.mStore.get<ESM::Spell>().search(id);
            }
        if (!selected) return {};
        auto prepared = prepareInstantSpell(*selected, mRuntime.mStore);
        if (!prepared || std::ranges::any_of(prepared->effects.effects,
                [&](const auto& effect) { return effect.mArea != 0
                    && (!mBinding.mMagicArea || effect.mRange != ESM::RT_Target); })
            || (!mBinding.mMagicTimed && std::ranges::any_of(prepared->effects.effects,
                [](const auto& effect) { return effect.mDuration != 0; }))
            || (use.targetKind == MagicUseTargetKind::Self
                ? !prepared->effects.onlyRange(ESM::RT_Self)
                : !prepared->effects.hasRange(ESM::RT_Target)
                    || prepared->effects.hasRange(ESM::RT_Touch))
            || mCombat->actors[actor(player->playerId())][9][2] < prepared->cost) return {};
        return std::make_unique<SpellTransaction>(use, player->playerId(), std::move(*prepared));
    }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareMeleeAttack(
        const CanonicalServerState& players, const ServerCommandProposal& proposal, ServerTick tick)
    {
        if (!mBinding.mCombatResolution || !mCombat || !mBinding.mNavigatingActor) return {};
        const auto* input = std::get_if<MeleeAttackCommandProposal>(&proposal.payload());
        if (!input) return {};
        const auto& attack = input->command();
        const auto* session = players.findActiveSession(proposal.sessionId());
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player || session->sessionGeneration() != proposal.sessionGeneration()
            || attack.sessionId != proposal.sessionId() || attack.sessionGeneration != proposal.sessionGeneration()
            || std::ranges::find(mBinding.mPlayers, player->playerId()) == mBinding.mPlayers.end()
            || !attack.targetActorId || attack.targetActorId->value() != mBinding.mNavigatingActor->actorId()
            || (attack.attackType != MeleeAttackType::Chop && attack.attackType != MeleeAttackType::Slash
                && attack.attackType != MeleeAttackType::Thrust)
            || !std::isfinite(attack.attackStrength) || attack.attackStrength < 0 || attack.attackStrength > 1
            || attack.sourceServerTick.value() > tick.value() || tick.value() - attack.sourceServerTick.value() > 64
            || (mLife && (attack.sourceServerTick.value() < mLife->bornTick
                || attack.expectedTargetRevision.value() < mLife->bornTick)))
            return {};
        const size_t owner = actor(player->playerId());
        if (mCombat->actors[owner][8][2] <= 0 || mCombat->actors[2][8][2] <= 0)
            return {};
        const auto held = mRuntime.equippedWeaponCondition(owner);
        if ((held && held->mCondition <= 0)
            || player->transform().cell() != actorCell(mBinding.mNavigatingActor->snapshot()))
            return {};
        const ESM::Weapon* weapon = nullptr;
        if (held)
        {
            const auto values = mRuntime.installedValues(owner);
            const auto item = std::ranges::find(values.mObjects, held->mItem,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == values.mObjects.end() || mRuntime.mStore.find(item->mRef.mRefID) != ESM::Weapon::sRecordId)
                return {};
            weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
        }
        const auto& position = player->transform().position();
        const osg::Vec3f origin(float(double(position.x()) / 1024), float(double(position.y()) / 1024),
            float(double(position.z()) / 1024));
        const auto scene = mBinding.mNavigatingActor->snapshot();
        const osg::Vec3f target(scene.mPosition[0], scene.mPosition[1], scene.mPosition[2]);
        if (!MWMechanics::isInMeleeReach(origin, target, 0, 0,
                MWMechanics::getMeleeWeaponReach(mRuntime.mStore, weapon, true)))
            return {};
        return std::make_unique<AttackTransaction>(attack, player->playerId());
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
            if (bool(decoded.life) != mBinding.mNpcLifecycle)
                throw std::invalid_argument("Native NPC lifecycle campaign version differs from binding");
            size_t headerOffset = 0;
            const auto magic = getAreaWord({reinterpret_cast<const char*>(image.data()), image.size()}, headerOffset);
            if (mBinding.mMagicProjectile != (magic == ProjectileActorCampaignMagic
                    || magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
                    || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic)
                || (mBinding.mMagicItemUse && magic != EnchantedProjectileActorCampaignMagic
                    && magic != TimedActorCampaignMagic && magic != AreaActorCampaignMagic
                    && magic != PlayerTargetActorCampaignMagic && magic != MultipleProjectileActorCampaignMagic)
                || (mBinding.mMagicTimed != (magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
                    || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic))
                || (mBinding.mMagicArea != (magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic))
                || (mBinding.mMagicPlayerTarget != (magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic))
                || (mBinding.mMagicProjectileCollection != (magic == MultipleProjectileActorCampaignMagic)))
                throw std::invalid_argument("Native projectile campaign version differs from binding");
            if (mBinding.mMeleeContact != (magic == ContactActorCampaignMagic || magic == CombatActorCampaignMagic
                    || magic == LifeActorCampaignMagic || magic == ProjectileActorCampaignMagic
                    || magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
                    || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic))
                throw std::invalid_argument("Native melee contact campaign version differs from binding");
            PlainEquipmentValues baseline;
            if (decoded.life)
            {
                const auto envelope = mRuntime.expectedEnvelope(mRuntime.ownerPtr(mCombatNpcOwner).getCellRef().getRefNum());
                decodeEquipment(decoded.life->spawnInventory,
                    {envelope, mRuntime.mStore, references, mRuntime.mScriptLocals, true}, baseline);
                if (baseline.mObjects.size() > PlainEquipmentValues::MaxItems
                    || decoded.life->spawnStats[8][2] <= 0)
                    throw std::invalid_argument("Native NPC respawn baseline invalid");
                (void)mBinding.mNavigatingActor->prepareRestore(decoded.life->spawnActor, actorDoorFrames());
            }
            auto restoredMelee = mMelee;
            if (decoded.melee) restoredMelee->restore(decoded.melee->state);
            if (decoded.melee && decoded.melee->target
                && std::ranges::none_of(mBinding.mPlayers,
                    [&](PlayerId player) { return player.value() == decoded.melee->target; }))
                throw std::invalid_argument("Native melee target outside bound players");
            for (const auto& pending : decoded.projectiles)
            {
                const auto caster = std::ranges::find_if(mBinding.mPlayers,
                    [&](PlayerId player) { return player.value() == pending.caster; });
                if (caster == mBinding.mPlayers.end()
                    || (pending.targetKind == uint64_t(MagicUseTargetKind::Actor)
                        ? pending.target != mBinding.mNavigatingActor->actorId()
                        : (!mBinding.mMagicPlayerTarget || std::ranges::none_of(mBinding.mPlayers,
                            [&](PlayerId id) { return id.value() == pending.target && id.value() != pending.caster; }))))
                    throw std::invalid_argument("Native projectile identities outside bound actors");
                if (pending.sourceKind == uint64_t(MagicUseSourceKind::Spell))
                {
                    const auto& known = mRuntime.mStore.get<ESM::NPC>()
                        .find(mBinding.mActors[size_t(caster - mBinding.mPlayers.begin())].mBase)->mSpells.mList;
                    size_t matches = 0;
                    for (const auto& id : known)
                        if (!id.empty() && spellRecordId(id) == pending.source)
                        {
                            const auto* selected = mRuntime.mStore.get<ESM::Spell>().search(id);
                            const auto plan = selected ? prepareInstantSpell(*selected, mRuntime.mStore) : std::nullopt;
                            if (!plan || !plan->effects.hasRange(ESM::RT_Target)
                                || plan->effects.hasRange(ESM::RT_Touch))
                                throw std::invalid_argument("Native projectile spell source invalid");
                            if (!mBinding.mMagicArea && std::ranges::any_of(plan->effects.effects,
                                    [](const auto& effect) { return effect.mArea != 0; }))
                                throw std::invalid_argument("Native projectile area unsupported by campaign");
                            ++matches;
                        }
                    if (matches != 1) throw std::invalid_argument("Native projectile spell identity ambiguous");
                }
                else
                {
                    const auto* selected = enchantmentBySource(mRuntime.mStore, pending.effectSource);
                    const auto effects = selected && (selected->mData.mType == ESM::Enchantment::WhenUsed
                        || selected->mData.mType == ESM::Enchantment::CastOnce)
                        ? prepareInstantEffects(selected->mEffects, mRuntime.mStore) : std::nullopt;
                    if (!effects || !effects->hasRange(ESM::RT_Target) || effects->hasRange(ESM::RT_Touch)
                        || (!mBinding.mMagicArea && std::ranges::any_of(effects->effects,
                            [](const auto& effect) { return effect.mArea != 0; })))
                        throw std::invalid_argument("Native projectile enchantment source invalid");
                }
            }
            EquipmentBytes retained(reinterpret_cast<const char*>(image.data()), reinterpret_cast<const char*>(image.data()+image.size()));
            recoverAreas(std::as_bytes(decoded.inventory), references, decoded.actor);
            mMelee = std::move(restoredMelee);
            mMeleeTarget = decoded.melee ? decoded.melee->target : 0;
            mMeleeContacted = decoded.melee && decoded.melee->contact;
            mCombat = decoded.combat;
            mLife = decoded.life;
            mProjectiles = decoded.projectiles;
            mTimedEffects = std::move(decoded.timedEffects);
            mRespawnInventory.swap(baseline);
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
        uint64_t target, bool contact, const std::optional<ActorCampaignCombat>& combat,
        const std::optional<ActorCampaignLife>& life,
        std::span<const ActorCampaignProjectile> projectiles,
        std::span<const ActorCampaignTimedEffect> timedEffects) const
    {
        const size_t meleeSize = melee ? 8 + mBinding.mBoundMelee->mResourceIdentity.size()
            + (mBinding.mMeleeContact ? 7 : 5) * 8 : 0;
        const size_t combatSize = combat ? 8 + 3 * ActorCampaignCombat::StatCount * 5 * 8 : 0;
        const size_t lifeSize = life ? (6 + ActorCampaignCombat::StatCount * 5 + 3 * life->deaths.size()) * 8
            + life->spawnActor.size() + life->spawnInventory.size() : 0;
        const size_t projectileSize = mBinding.mMagicProjectile
            ? 8 + projectiles.size() * ((mBinding.mMagicItemUse ? 13 : 11) * 8
                + (mBinding.mMagicPlayerTarget ? 8 : 0)
                + (mBinding.mMagicProjectileCollection ? 8 : 0)) : 0;
        const size_t timedSize = mBinding.mMagicTimed ? 8 + timedEffects.size() * 24 : 0;
        if (core.empty() || actor.empty() || actor.size() > 65536
            || timedEffects.size() > MaximumActorTimedEffects
            || projectiles.size() > (mBinding.mMagicProjectileCollection ? MaximumActorProjectiles : 1)
            || 56 + meleeSize + combatSize + lifeSize + projectileSize + timedSize + actor.size() > MaximumNativeInventoryImageBytes
            || core.size() > MaximumNativeInventoryImageBytes - 56 - meleeSize - combatSize - lifeSize - projectileSize - timedSize - actor.size())
            throw std::invalid_argument("Native actor campaign exceeds bound");
        EquipmentBytes result;
        putAreaWord(result, mBinding.mMagicProjectileCollection ? MultipleProjectileActorCampaignMagic
            : mBinding.mMagicPlayerTarget ? PlayerTargetActorCampaignMagic
            : mBinding.mMagicArea ? AreaActorCampaignMagic
            : mBinding.mMagicTimed ? TimedActorCampaignMagic
            : mBinding.mMagicItemUse ? EnchantedProjectileActorCampaignMagic
            : mBinding.mMagicProjectile ? ProjectileActorCampaignMagic
            : life ? LifeActorCampaignMagic : combat ? CombatActorCampaignMagic
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
        if (life)
        {
            putAreaWord(result, life->generation); putAreaWord(result, life->bornTick);
            putAreaWord(result, life->respawnTick);
            for (const auto& stat : life->spawnStats)
                for (float value : stat) putAreaWord(result, std::bit_cast<uint32_t>(value));
            putAreaWord(result, life->spawnActor.size()); putAreaWord(result, life->spawnInventory.size());
            result.insert(result.end(), life->spawnActor.begin(), life->spawnActor.end());
            result.insert(result.end(), life->spawnInventory.begin(), life->spawnInventory.end());
            putAreaWord(result, life->deaths.size());
            for (const auto& death : life->deaths)
            { putAreaWord(result, death.life); putAreaWord(result, death.tick); putAreaWord(result, death.killer); }
        }
        if (mBinding.mMagicProjectile)
        {
            putAreaWord(result, projectiles.size());
            for (const auto& projectile : projectiles)
            {
                putAreaWord(result, projectile.caster); putAreaWord(result, projectile.source);
                putAreaWord(result, projectile.target); putAreaWord(result, projectile.generation);
                putAreaWord(result, projectile.expiresTick);
                if (mBinding.mMagicItemUse)
                {
                    putAreaWord(result, projectile.sourceKind);
                    putAreaWord(result, projectile.effectSource);
                }
                if (mBinding.mMagicPlayerTarget) putAreaWord(result, projectile.targetKind);
                if (mBinding.mMagicProjectileCollection) putAreaWord(result, projectile.commandId);
                for (float value : projectile.position) putAreaWord(result, std::bit_cast<uint32_t>(value));
                for (float value : projectile.step) putAreaWord(result, std::bit_cast<uint32_t>(value));
            }
        }
        if (mBinding.mMagicTimed)
        {
            putAreaWord(result, timedEffects.size());
            for (const auto& effect : timedEffects)
            {
                putAreaWord(result, effect.actor);
                putAreaWord(result, std::bit_cast<uint32_t>(effect.magnitude));
                putAreaWord(result, effect.expiresTick);
            }
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

    EquipmentBytes InventoryService::stagedWeaponCore(std::span<const WeaponWear> wear,
        const PreparedNativeInventory* command, const std::optional<ItemCharge>& charge) const
    {
        if (wear.size() > 2 || (wear.empty() && !charge)
            || mWorld.getPtrRegistryRevision() >= std::numeric_limits<size_t>::max() - wear.size() - size_t(charge.has_value()))
            throw std::invalid_argument("Native melee weapon wear candidate invalid");
        command = areaDoorCommand(command);
        const auto* transfer = dynamic_cast<const Transaction*>(command);
        const auto* equipment = dynamic_cast<const EquipmentTransaction*>(command);
        const auto* world = dynamic_cast<const WorldTransaction*>(command);
        const auto candidateValues = [&](size_t owner) {
            return transfer ? mRuntime.preparedValues(transfer->prepared.mTransfer, owner)
                : equipment ? mRuntime.preparedValues(equipment->prepared, owner)
                : world ? mRuntime.preparedValues(world->prepared, owner)
                : mRuntime.installedValues(owner);
        };
        const uint64_t revision = transfer ? transfer->prepared.candidate().mRevision
            : equipment ? equipment->prepared.candidate().mRevision
            : world ? world->prepared.revision() : mWorld.getPtrRegistryRevision();
        if (revision >= std::numeric_limits<size_t>::max() - wear.size() - size_t(charge.has_value()))
            throw std::invalid_argument("Native melee inventory revision exhausted");
        EquipmentSessionValues values{{candidateValues(0), candidateValues(1)},
            revision + wear.size() + size_t(charge.has_value())};
        for (size_t i = 2; i < mRuntime.ownerCount(); ++i)
            values.mContainers.push_back(candidateValues(i));
        if (world)
        {
            values.mWorldItems = mRuntime.worldValues(&world->prepared);
            values.mWorldCells = mRuntime.preparedWorldCells(world->prepared);
        }
        for (size_t index = 0; index < wear.size(); ++index)
        {
            const auto& change = wear[index];
            if (change.owner >= mRuntime.ownerCount() || change.condition < 0
                || change.condition > change.before.mCondition
                || std::ranges::any_of(wear.first(index), [&](const auto& earlier) { return earlier.owner == change.owner; })
                || mRuntime.equippedWeaponCondition(change.owner) != change.before)
                throw std::invalid_argument("Native melee weapon wear owner invalid");
            auto& owner = change.owner < 2 ? values.mActors[change.owner] : values.mContainers.at(change.owner - 2);
            if (owner.mSlots[MWWorld::InventoryStore::Slot_CarriedRight] != change.before.mItem)
                throw std::invalid_argument("Native melee weapon slot changed");
            const auto item = std::ranges::find(owner.mObjects, change.before.mItem,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == owner.mObjects.end()) throw std::invalid_argument("Native melee weapon missing");
            item->mRef.mChargeInt = change.condition;
            if (change.condition == 0) owner.mSlots[MWWorld::InventoryStore::Slot_CarriedRight] = {};
        }
        if (charge)
        {
            if (charge->owner >= 2 || (!charge->consume && (!std::isfinite(charge->after) || charge->after < 0)))
                throw std::invalid_argument("Native item charge candidate invalid");
            auto& owner = values.mActors[charge->owner];
            const auto item = std::ranges::find(owner.mObjects, charge->item,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == owner.mObjects.end() || item->mRef.mEnchantmentCharge != charge->before)
                throw std::invalid_argument("Native item charge source changed");
            if (charge->consume)
            {
                if (item->mRef.mCount <= 0) throw std::invalid_argument("Native consumed item stack changed");
                --item->mRef.mCount;
                if (item->mRef.mCount == 0)
                    for (auto& slot : owner.mSlots) if (slot == charge->item) slot = {};
            }
            else item->mRef.mEnchantmentCharge = charge->after;
        }
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
        std::optional<ActorCampaignLife> life;
        std::vector<ActorCampaignProjectile> projectiles;
        std::vector<ActorCampaignTimedEffect> timedEffects;
        std::unique_ptr<EquipmentRuntime::PreparedRespawn> respawn;
        std::optional<MeleeCombatEvent> playerHit;
        std::optional<ActorMeleeCombatEvent> actorHit;
        std::vector<MagicUseCombatEvent> spellCasts;
        std::vector<WeaponWear> wear;
        std::optional<ItemCharge> charge;
        EquipmentBytes wornCore, wornInventory, respawnCore;
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
            std::optional<ActorCampaignLife> stagedLife,
            std::vector<ActorCampaignProjectile> stagedProjectiles,
            std::vector<ActorCampaignTimedEffect> stagedTimedEffects,
            std::unique_ptr<EquipmentRuntime::PreparedRespawn> stagedRespawn,
            std::optional<MeleeCombatEvent> stagedPlayerHit,
            std::optional<ActorMeleeCombatEvent> stagedActorHit,
            std::vector<MagicUseCombatEvent> stagedSpellCasts,
            std::vector<WeaponWear> stagedWear, std::optional<ItemCharge> stagedCharge, EquipmentBytes core,
            uint64_t time, std::array<float,3> motion,
            ServerApp::NativeTravelDiagnostics report)
            : service(owner), command(std::move(input)), actor(std::move(step)), melee(std::move(swing)),
              combat(std::move(stagedCombat)), life(std::move(stagedLife)),
              projectiles(std::move(stagedProjectiles)), timedEffects(std::move(stagedTimedEffects)),
              respawn(std::move(stagedRespawn)),
              playerHit(std::move(stagedPlayerHit)),
              actorHit(std::move(stagedActorHit)), spellCasts(std::move(stagedSpellCasts)),
              wear(std::move(stagedWear)), charge(std::move(stagedCharge)),
              wornCore(std::move(core)), target(selected), contact(contacted), before(owner.mActorImage),
              tick(time), velocity(motion), diagnostics(report)
        { if (respawn) respawnCore.assign(respawn->image().begin(), respawn->image().end()); }
        bool changesInventory() const noexcept override { return bool(command) || !wear.empty() || bool(charge) || bool(respawn); }
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        {
            if (consumed || service.inventoryImage().empty() || before != service.mActorImage
                || (actor && !service.mBinding.mNavigatingActor->canInstall(*actor))) return CanonicalDurabilityResult::Rejected;
            try
            {
                for (const auto& change : wear)
                    if (service.mRuntime.equippedWeaponCondition(change.owner) != change.before)
                        return CanonicalDurabilityResult::Rejected;
                if (charge)
                {
                    const auto source = service.mWorld.getPtr(charge->item);
                    if (!source.hasLiveReference() || source.mContainerStore != &service.mRuntime.storage(charge->owner)
                        || source.getCellRef().getEnchantmentCharge() != charge->before
                        || (charge->consume && source.getCellRef().getCount() <= 0))
                        return CanonicalDurabilityResult::Rejected;
                }
                EquipmentBytes sealed;
                const auto compose = [&](std::span<const std::byte> inventory) {
                    const auto retained = readActorCampaign(before).actor;
                    const std::span<const char> candidate(reinterpret_cast<const char*>(inventory.data()), inventory.size());
                    if (!wornCore.empty()) wornInventory = service.replaceAreaCore(candidate, wornCore);
                    if (respawn) wornInventory = service.replaceAreaCore(candidate, respawn->image());
                    const auto selected = !wornInventory.empty() ? std::span<const char>(wornInventory) : candidate;
                    sealed = service.sealActor(selected,
                        actor ? actor->image() : retained, tick, velocity, melee, target, contact, combat, life, projectiles,
                        timedEffects);
                    return persist(std::as_bytes(std::span(sealed)));
                };
                const auto result = command ? command->commit(compose) : compose(std::as_bytes(std::span(service.mImage)));
                if (result == CanonicalDurabilityResult::Rejected) return result;
                consumed = true;
                if (result == CanonicalDurabilityResult::Failed) service.mRuntime.mFailedClosed = true;
                else
                {
                    if (!wear.empty() || charge)
                    {
                        for (const auto& change : wear)
                            service.mRuntime.installWeaponWear(change.owner, change.before.mItem, change.condition);
                        if (charge)
                        {
                            if (charge->consume) service.mRuntime.installConsumedMagicItem(charge->owner, charge->item);
                            else service.mRuntime.installEnchantmentCharge(charge->owner, charge->item, charge->after);
                        }
                        service.mCoreImage.swap(wornCore);
                        service.mImage.swap(wornInventory);
                    }
                    if (respawn)
                    {
                        service.mRuntime.installRespawn(*respawn);
                        service.mCoreImage.swap(respawnCore);
                        service.mImage.swap(wornInventory);
                    }
                    if (actor) service.mBinding.mNavigatingActor->install(*actor);
                    service.mMelee = std::move(melee);
                    service.mMeleeTarget = target; service.mMeleeContacted = contact;
                    service.mCombat = std::move(combat);
                    service.mLife = std::move(life);
                    service.mProjectiles = std::move(projectiles);
                    service.mTimedEffects = std::move(timedEffects);
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
        std::optional<ClientMeleeAttackCommand> playerAttack;
        PlayerId playerAttacker = mBinding.mPlayers[0];
        const SpellTransaction* spellUse = dynamic_cast<const SpellTransaction*>(command.get());
        std::optional<ClientMagicUseCommand> playerSpell;
        PlayerId spellCaster = mBinding.mPlayers[0];
        std::optional<PreparedInstantSpell> spellRecord;
        std::optional<ItemCharge> spellCharge;
        uint64_t spellEffectSource = 0;
        if (spellUse)
        {
            playerSpell = spellUse->use;
            spellCaster = spellUse->caster;
            spellRecord = spellUse->spell;
            spellCharge = spellUse->charge;
            spellEffectSource = spellUse->effectSource;
            command.reset();
        }
        if (const auto* attack = dynamic_cast<const AttackTransaction*>(command.get()))
        {
            playerAttack = attack->attack;
            playerAttacker = attack->attacker;
            command.reset();
        }
        const bool dueRespawn = mLife && mLife->respawnTick && tick.value() >= mLife->respawnTick && !command;
        if (!dueRespawn)
        {
            if (!mBinding.mDoors.empty()) command = prepareAreaDoor(0, false, players, tick, seconds, std::move(command));
            else if (!command) command = prepareDoorStep(players, tick, seconds);
        }
        const auto before = mBinding.mNavigatingActor->snapshot();
        bool active = mBinding.mRetainTraveler && !mBinding.mNavigatingActor->arrived();
        for (const auto& session : players.activeSessions())
            if (const auto* player = players.findPlayer(session.playerId()); player
                && (mBinding.mTravelerNeighborhood ? sharesCellNeighborhood(player->transform().cell(), actorCell(before))
                                                  : player->transform().cell() == actorCell(before))) active = true;
        if (mCombat && mCombat->actors[2][8][2] <= 0) active = false;
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
        auto after = step ? step->snapshot() : before;
        auto melee = mMelee;
        auto combat = mCombat;
        auto life = mLife;
        auto projectiles = mProjectiles;
        auto timedEffects = mTimedEffects;
        const uint64_t elapsedTicks = tick.value() - mActorTick;
        for (auto& effect : timedEffects)
        {
            bool paused = effect.actor == 2 && !active;
            if (effect.actor < mBinding.mPlayers.size())
                paused = std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                    return session.playerId() == mBinding.mPlayers[size_t(effect.actor)];
                });
            if (paused)
            {
                if (effect.expiresTick > UINT64_MAX - elapsedTicks)
                    throw std::invalid_argument("Paused native timed effect deadline exhausted");
                effect.expiresTick += elapsedTicks;
            }
        }
        std::erase_if(timedEffects, [tick](const auto& effect) { return effect.expiresTick <= tick.value(); });
        std::unique_ptr<EquipmentRuntime::PreparedRespawn> respawn;
        std::optional<MeleeCombatEvent> playerHit;
        std::optional<ActorMeleeCombatEvent> actorHit;
        std::vector<MagicUseCombatEvent> spellCasts;
        std::vector<WeaponWear> wear;
        std::optional<ItemCharge> charge;
        EquipmentBytes wornCore;
        uint64_t target = mMeleeTarget;
        bool contact = mMeleeContacted;
        if (dueRespawn)
        {
            if (life->generation == UINT32_MAX) throw std::invalid_argument("NPC life generation exhausted");
            respawn = mRuntime.prepareRespawn(mCombatNpcOwner, mRespawnInventory);
            step = mBinding.mNavigatingActor->prepareRestore(life->spawnActor, doors);
            after = step->snapshot();
            combat->actors[2] = life->spawnStats;
            melee = mBinding.mBoundMelee->mAnimation;
            target = 0; contact = false;
            std::erase_if(projectiles, [](const auto& pending) {
                return pending.targetKind == uint64_t(MagicUseTargetKind::Actor);
            });
            std::erase_if(timedEffects, [](const auto& effect) { return effect.actor == 2; });
            ++life->generation;
            life->bornTick = tick.value();
            life->respawnTick = 0;
        }
        if (playerAttack && combat)
        {
            const size_t owner = actor(playerAttacker);
            auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[owner]);
            auto victim = loadCombatStats(mRuntime.mStore, combat->actors[2]);
            addTimedResistance(attacker, timedEffects, owner);
            addTimedResistance(victim, timedEffects, 2);
            const auto held = mRuntime.equippedWeaponCondition(owner);
            if ((held && held->mCondition <= 0) || victim.getHealth().getCurrent() <= 0)
                throw std::invalid_argument("Native player attack became stale before tick composition");
            const ESM::Weapon* weapon = nullptr;
            if (held)
            {
                const auto values = mRuntime.installedValues(owner);
                const auto item = std::ranges::find(values.mObjects, held->mItem,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == values.mObjects.end() || mRuntime.mStore.find(item->mRef.mRefID) != ESM::Weapon::sRecordId)
                    throw std::invalid_argument("Native player weapon identity missing");
                weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
            }
            const float strength = playerAttack->attackStrength;
            const float capacity = attacker.getAttribute(ESM::Attribute::Strength).getModified()
                * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
            const float weight = std::max(0.f, mRuntime.storage(owner).getWeight());
            const float encumbrance = weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity;
            MWMechanics::applyFatigueLoss(attacker, mRuntime.mStore,
                weapon ? weapon->mData.mWeight : 0.f, strength, encumbrance);
            const auto skill = weapon ? MWMechanics::getWeaponType(weapon->mData.mType)->mSkill
                : ESM::Skill::HandToHand;
            const bool paralyzed = victim.getMagicEffects()
                .getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() > 0;
            const float chance = MWMechanics::getHitChance(mRuntime.mStore, attacker, victim,
                int(attacker.getSkill(skill).getModified()), false, paralyzed);
            Misc::Rng::Generator rng;
            Misc::Rng::deserialize(std::to_string(combat->rng), rng);
            const bool success = Misc::Rng::roll0to99(rng) < chance;
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            float damage = 0;
            const auto damagedStat = weapon ? MeleeDamageStat::Health : MeleeDamageStat::Fatigue;
            if (success && weapon)
            {
                const auto& range = playerAttack->attackType == MeleeAttackType::Chop ? weapon->mData.mChop
                    : playerAttack->attackType == MeleeAttackType::Slash ? weapon->mData.mSlash : weapon->mData.mThrust;
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
            else if (success)
            {
                damage = MWMechanics::getUnarmedFatigueDamage(mRuntime.mStore, attacker,
                    attacker.getSkill(ESM::Skill::HandToHand).getModified(), strength);
                MWMechanics::applyHitDamage(victim, {{"fatigue", damage}}, MWWorld::TimeStamp{});
            }
            if (weapon && weapon->mData.mHealth)
            {
                const float multiplier = mRuntime.mStore.get<ESM::GameSetting>()
                    .find("fWeaponDamageMult")->mValue.getFloat();
                wear.push_back({owner, *held,
                    MWMechanics::weaponConditionAfterHit(held->mCondition, damage, success, multiplier)});
            }
            saveCombatStats(combat->actors[owner], attacker);
            saveCombatStats(combat->actors[2], victim);
            playerHit = MeleeCombatEvent{playerAttacker,
                ActorId::fromValue(before.mActor).value(),
                CombatRevision::fromValue(tick.value()).value(),
                CombatRevision::fromValue(tick.value()).value(),
                damage, damagedStat, success, false,
                victim.getHealth().getCurrent() <= 0};
            if (victim.getHealth().getCurrent() <= 0)
            {
                if (life)
                {
                    if (life->deaths.size() >= ActorCampaignLife::MaximumDeaths
                        || tick.value() > UINT64_MAX - mBinding.mNpcRespawnDelayTicks)
                        throw std::invalid_argument("NPC death history or deadline exhausted");
                    life->deaths.push_back({life->generation, tick.value(), playerAttacker.value()});
                    life->respawnTick = tick.value() + mBinding.mNpcRespawnDelayTicks;
                }
                step.reset();
                after = before;
                report.status = Diagnostics::Status::Idle;
            }
        }
        const size_t flyingCount = projectiles.size();
        if (playerSpell && combat)
        {
            const size_t owner = actor(spellCaster);
            auto caster = loadCombatStats(mRuntime.mStore, combat->actors[owner]);
            addTimedResistance(caster, timedEffects, owner);
            Misc::Rng::Generator rng;
            Misc::Rng::deserialize(std::to_string(combat->rng), rng);
            const auto launch = spellCharge
                ? InstantSpellLaunch{true, applyInstantEffects(spellRecord->effects,
                    ESM::RT_Self, caster, &rng, &mRuntime.mStore)}
                : launchInstantSpell(*spellRecord, caster, mRuntime.mStore, rng);
            if (launch.succeeded)
                stageTimedResistance(spellRecord->effects, ESM::RT_Self, owner, tick.value(), timedEffects);
            charge = spellCharge;
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            saveCombatStats(combat->actors[owner], caster);
            spellCasts.push_back(MagicUseCombatEvent{spellCaster, playerSpell->sourceKind, playerSpell->sourceId,
                playerSpell->targetKind, playerSpell->targetId, CombatRevision::fromValue(tick.value()).value(),
                CombatRevision::fromValue(tick.value()).value(), launch.succeeded, launch.result.health,
                launch.result.fatigue, launch.result.magicka, 0.f, 0.f, 0.f, false});
            if (launch.succeeded && playerSpell->targetKind != MagicUseTargetKind::Self)
            {
                const auto* player = players.findPlayer(spellCaster);
                if (!player || !life || projectiles.size() >= MaximumActorProjectiles)
                    throw std::invalid_argument("Native target spell launch became stale");
                const auto position = player->transform().position();
                std::array<float, 3> origin{float(double(position.x()) / 1024),
                    float(double(position.y()) / 1024), float(double(position.z()) / 1024) + 64.f};
                std::array<float, 3> destination{before.mPosition[0], before.mPosition[1],
                    before.mPosition[2] + 55.f};
                if (playerSpell->targetKind == MagicUseTargetKind::Player)
                {
                    const auto* victim = players.findPlayer(PlayerId::fromValue(playerSpell->targetId).value());
                    if (!victim || victim->transform().cell() != actorCell(before)
                        || combat->actors[actor(victim->playerId())][8][2] <= 0
                        || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                            return session.playerId() == victim->playerId();
                        })) throw std::invalid_argument("Native target player left before launch");
                    const auto place = victim->transform().position();
                    destination = {float(double(place.x()) / 1024), float(double(place.y()) / 1024),
                        float(double(place.z()) / 1024) + 64.f};
                }
                std::array<float, 3> direction{destination[0] - origin[0],
                    destination[1] - origin[1], destination[2] - origin[2]};
                const float distance = std::sqrt(direction[0]*direction[0]
                    + direction[1]*direction[1] + direction[2]*direction[2]);
                if (!std::isfinite(distance) || distance < 8.f || distance > 2048.f)
                    throw std::invalid_argument("Native target spell range invalid");
                float speed = 0;
                for (const auto& effect : spellRecord->effects.effects)
                    speed += mRuntime.mStore.get<ESM::MagicEffect>().find(effect.mEffectID)->mData.mSpeed;
                speed /= spellRecord->effects.effects.size();
                speed *= mRuntime.mStore.get<ESM::GameSetting>()
                    .find("fTargetSpellMaxSpeed")->mValue.getFloat() / 30.f;
                if (!std::isfinite(speed) || speed < 1.f || speed > 1000.f
                    || tick.value() > UINT64_MAX - 90)
                    throw std::invalid_argument("Native target spell speed invalid");
                for (float& axis : direction) axis *= speed / distance;
                projectiles.push_back(ActorCampaignProjectile{spellCaster.value(), playerSpell->sourceId,
                    playerSpell->targetId, life->generation, tick.value() + 90,
                    uint64_t(playerSpell->sourceKind), spellCharge ? spellEffectSource : playerSpell->sourceId,
                    origin, direction, uint64_t(playerSpell->targetKind),
                    mBinding.mMagicProjectileCollection ? playerSpell->commandId.value() : 0});
            }
        }
        if (flyingCount && combat)
        {
            std::vector<ActorCampaignProjectile> remaining;
            remaining.reserve(projectiles.size());
            for (size_t flight = 0; flight < flyingCount; ++flight)
            {
                auto pending = projectiles[flight];
                const size_t eventCount = spellCasts.size();
                std::array<float, 3> endpoint;
                for (size_t i = 0; i < 3; ++i) endpoint[i] = pending.position[i] + pending.step[i];
                auto hit = mBinding.mNavigatingActor->projectileContact(pending.position, endpoint);
                size_t hitIndex = hit && hit->actor ? 2 : 3; // Three denotes world geometry.
                if (mBinding.mMagicPlayerTarget)
                {
                    const float length2 = pending.step[0] * pending.step[0]
                        + pending.step[1] * pending.step[1] + pending.step[2] * pending.step[2];
                    float first = 1.f;
                    if (hit)
                    {
                        float projection = 0.f;
                        for (size_t axis = 0; axis < 3; ++axis)
                            projection += (hit->position[axis] - pending.position[axis]) * pending.step[axis];
                        first = std::clamp(projection / length2, 0.f, 1.f);
                    }
                    for (size_t index = 0; index < mBinding.mPlayers.size(); ++index)
                    {
                        const auto id = mBinding.mPlayers[index];
                        const auto* player = players.findPlayer(id);
                        if (id.value() == pending.caster || !player
                            || player->transform().cell() != actorCell(before)
                            || combat->actors[index][8][2] <= 0
                            || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                                return session.playerId() == id;
                            })) continue;
                        const auto place = player->transform().position();
                        const std::array<float, 3> center{float(double(place.x()) / 1024),
                            float(double(place.y()) / 1024), float(double(place.z()) / 1024) + 64.f};
                        float offset = 0.f, origin2 = 0.f;
                        for (size_t axis = 0; axis < 3; ++axis)
                        {
                            const float delta = pending.position[axis] - center[axis];
                            offset += delta * pending.step[axis]; origin2 += delta * delta;
                        }
                        constexpr float radius = 32.f;
                        const float discriminant = offset * offset - length2 * (origin2 - radius * radius);
                        if (discriminant < 0.f) continue;
                        const float fraction = origin2 <= radius * radius ? 0.f
                            : (-offset - std::sqrt(discriminant)) / length2;
                        if (fraction < 0.f || fraction > first) continue;
                        first = fraction;
                        std::array<float, 3> point;
                        for (size_t axis = 0; axis < 3; ++axis)
                            point[axis] = pending.position[axis] + fraction * pending.step[axis];
                        hit = ActorProjectileContact{id.value(), point};
                        hitIndex = index;
                    }
                }
                const bool timedOut = tick.value() >= pending.expiresTick;
                const auto casterIndex = actor(PlayerId::fromValue(pending.caster).value());
                const bool validCast = life && combat->actors[casterIndex][8][2] > 0
                    && (pending.targetKind == uint64_t(MagicUseTargetKind::Player)
                        || (life->generation == pending.generation && !life->respawnTick
                            && combat->actors[2][8][2] > 0));
                if (!hit && !timedOut && validCast)
                {
                    pending.position = endpoint;
                    remaining.push_back(pending);
                }
                else
                {
                    const bool directHit = hit && validCast
                        && (pending.targetKind == uint64_t(MagicUseTargetKind::Actor)
                            ? hitIndex == 2 && hit->actor == pending.target
                            : hitIndex < 2 && mBinding.mPlayers[hitIndex].value() == pending.target);
                    if (hit && validCast && (directHit || mBinding.mMagicArea))
                    {
                        const auto& known = mRuntime.mStore.get<ESM::NPC>()
                            .find(mBinding.mActors[casterIndex].mBase)->mSpells.mList;
                        std::optional<PreparedInstantEffects> effects;
                        if (pending.sourceKind == uint64_t(MagicUseSourceKind::Spell))
                        {
                            const ESM::Spell* selected = nullptr;
                            for (const auto& id : known)
                                if (!id.empty() && spellRecordId(id) == pending.effectSource)
                                {
                                    if (selected) throw std::invalid_argument("Native projectile spell identity collision");
                                    selected = mRuntime.mStore.get<ESM::Spell>().search(id);
                                }
                            if (!selected) throw std::invalid_argument("Native projectile spell source missing");
                            const auto plan = prepareInstantSpell(*selected, mRuntime.mStore);
                            if (plan) effects = plan->effects;
                        }
                        else if (const auto* selected = enchantmentBySource(mRuntime.mStore, pending.effectSource);
                            selected && (selected->mData.mType == ESM::Enchantment::WhenUsed
                                || selected->mData.mType == ESM::Enchantment::CastOnce))
                            effects = prepareInstantEffects(selected->mEffects, mRuntime.mStore);
                        if (!effects || !effects->hasRange(ESM::RT_Target))
                            throw std::invalid_argument("Native projectile effect plan changed");
                        std::array<PreparedInstantEffects, 3> selected;
                        for (const auto& effect : effects->effects)
                        {
                            if (effect.mRange != ESM::RT_Target) continue;
                            if (directHit) selected[hitIndex].effects.push_back(effect);
                            if (!mBinding.mMagicArea || !effect.mArea) continue;
                            const float radius = float(effect.mArea) * 22.f;
                            const auto nearby = [&](const std::array<float, 3>& position) {
                                float distance2 = 0.f;
                                for (size_t axis = 0; axis < 3; ++axis)
                                    distance2 += (position[axis] - hit->position[axis])
                                        * (position[axis] - hit->position[axis]);
                                return distance2 <= radius * radius;
                            };
                            // Stock explosion excludes the caster and applies the direct hit once.
                            for (size_t index = 0; index < mBinding.mPlayers.size(); ++index)
                            {
                                const auto playerId = mBinding.mPlayers[index];
                                const auto* player = players.findPlayer(playerId);
                                if (playerId.value() == pending.caster || (directHit && index == hitIndex) || !player
                                    || player->transform().cell() != actorCell(before)
                                    || combat->actors[index][8][2] <= 0
                                    || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                                        return session.playerId() == playerId;
                                    })) continue;
                                const auto position = player->transform().position();
                                if (nearby({float(double(position.x()) / 1024),
                                        float(double(position.y()) / 1024), float(double(position.z()) / 1024)}))
                                    selected[index].effects.push_back(effect);
                            }
                            if (!(directHit && hitIndex == 2) && combat->actors[2][8][2] > 0
                                && nearby(before.mPosition)) selected[2].effects.push_back(effect);
                        }
                        Misc::Rng::Generator rng;
                        Misc::Rng::deserialize(std::to_string(combat->rng), rng);
                        for (const size_t index : {size_t(2), size_t(0), size_t(1)})
                        {
                            if (selected[index].effects.empty()) continue;
                            auto victim = loadCombatStats(mRuntime.mStore, combat->actors[index]);
                            addTimedResistance(victim, timedEffects, index);
                            const auto result = applyInstantEffects(selected[index], ESM::RT_Target,
                                victim, &rng, &mRuntime.mStore);
                            stageTimedResistance(selected[index], ESM::RT_Target, index, tick.value(), timedEffects);
                            saveCombatStats(combat->actors[index], victim);
                            const bool died = victim.getHealth().getCurrent() <= 0;
                            if (died && index == 2 && life)
                            {
                                if (life->deaths.size() >= ActorCampaignLife::MaximumDeaths
                                    || tick.value() > UINT64_MAX - mBinding.mNpcRespawnDelayTicks)
                                    throw std::invalid_argument("NPC spell death history or deadline exhausted");
                                life->deaths.push_back({life->generation, tick.value(), pending.caster});
                                life->respawnTick = tick.value() + mBinding.mNpcRespawnDelayTicks;
                                step.reset(); after = before; report.status = Diagnostics::Status::Idle;
                            }
                            spellCasts.push_back(MagicUseCombatEvent{PlayerId::fromValue(pending.caster).value(),
                                MagicUseSourceKind(pending.sourceKind), pending.source,
                                index == 2 ? MagicUseTargetKind::Actor : MagicUseTargetKind::Player,
                                index == 2 ? mBinding.mNavigatingActor->actorId() : mBinding.mPlayers[index].value(),
                                CombatRevision::fromValue(tick.value()).value(),
                                CombatRevision::fromValue(tick.value()).value(), true,
                                0.f, 0.f, 0.f, result.health, result.fatigue, result.magicka, died});
                        }
                        combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    }
                    if (spellCasts.size() == eventCount)
                        spellCasts.push_back(MagicUseCombatEvent{PlayerId::fromValue(pending.caster).value(),
                            MagicUseSourceKind(pending.sourceKind), pending.source,
                            MagicUseTargetKind(pending.targetKind), pending.target,
                            CombatRevision::fromValue(tick.value()).value(),
                            CombatRevision::fromValue(tick.value()).value(), false,
                            0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false});
                }
            }
            if (projectiles.size() > flyingCount) remaining.push_back(projectiles.back());
            projectiles = std::move(remaining);
        }
        if (step && !respawn && melee && (!combat || combat->actors[2][8][2] > 0))
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
                bool hitSuccess = false;
                float hitDamage = 0;
                MeleeDamageStat hitStat = MeleeDamageStat::Health;
                bool targetDied = false;
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
                    hitSuccess = success;
                    combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    float damage = 0;
                    const auto damagedStat = weapon ? MeleeDamageStat::Health : MeleeDamageStat::Fatigue;
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
                    else if (success)
                    {
                        damage = MWMechanics::getUnarmedFatigueDamage(mRuntime.mStore, attacker,
                            attacker.getSkill(ESM::Skill::HandToHand).getModified(), strength);
                        MWMechanics::applyHitDamage(victim, {{"fatigue", damage}}, MWWorld::TimeStamp{});
                    }
                    if (weapon && weapon->mData.mHealth)
                    {
                        const float multiplier = mRuntime.mStore.get<ESM::GameSetting>()
                            .find("fWeaponDamageMult")->mValue.getFloat();
                        wear.push_back({mCombatNpcOwner, *held,
                            MWMechanics::weaponConditionAfterHit(held->mCondition, damage, success, multiplier)});
                    }
                    saveCombatStats(combat->actors[victimIndex], victim);
                    hitDamage = damage;
                    hitStat = damagedStat;
                    targetDied = victim.getHealth().getCurrent() <= 0;
                }
                saveCombatStats(combat->actors[2], attacker);
                if (target)
                    actorHit = ActorMeleeCombatEvent{ActorId::fromValue(before.mActor).value(),
                        PlayerId::fromValue(target).value(),
                        CombatRevision::fromValue(tick.value()).value(),
                        CombatRevision::fromValue(tick.value()).value(),
                        hitDamage, hitStat, contact && hitSuccess, false, targetDied};
            }
        }
        std::array<float,3> velocity;
        for (size_t i=0; i<3; ++i) velocity[i]=respawn ? 0 : (after.mPosition[i]-before.mPosition[i])*30;
        if (!wear.empty() || charge) wornCore = stagedWeaponCore(wear, command.get(), charge);
        return std::make_unique<ActorTransaction>(*this, std::move(command), std::move(step),
            std::move(melee), target, contact, std::move(combat), std::move(life),
            std::move(projectiles), std::move(timedEffects), std::move(respawn),
            std::move(playerHit), std::move(actorHit), std::move(spellCasts),
            std::move(wear), std::move(charge), std::move(wornCore),
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
            {}, actorState ? &*actorState : nullptr, moving ? std::span<const WeaponWear>(moving->wear) : std::span<const WeaponWear>{},
            moving ? moving->charge : std::optional<ItemCharge>{},
            moving && moving->combat ? &*moving->combat : nullptr,
            moving ? moving->respawn.get() : nullptr);
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

    std::optional<LatestWinsCombatSnapshot> InventoryService::projectCombat(
        const CanonicalServerState& players, SessionId target, ServerTick tick, CanonicalRevision revision,
        const PreparedNativeInventory* candidate) const
    try
    {
        if (!mBinding.mCombatResolution || !mCombat || mRuntime.mFailedClosed) return {};
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player) return {};
        const auto* moving = dynamic_cast<const ActorTransaction*>(candidate);
        if (candidate && (!moving || &moving->service != this || moving->consumed || moving->before != mActorImage))
            return {};
        const auto& combat = moving && moving->combat ? *moving->combat : *mCombat;
        const auto combatRevision = CombatRevision::fromValue(std::max<uint64_t>(1,
            moving ? moving->tick : mActorTick)).value();
        const size_t selfIndex = actor(player->playerId());
        const auto self = loadCombatStats(mRuntime.mStore, combat.actors[selfIndex]);
        const auto snapshot = [&](const auto& stats, auto id) {
            return PlayerCombatSnapshot{id, combatRevision, stats.getHealth().getCurrent(),
                stats.getHealth().getModified(), stats.getFatigue().getCurrent(),
                stats.getFatigue().getModified(), stats.getMagicka().getCurrent(),
                stats.getMagicka().getModified(), stats.getHealth().getCurrent() <= 0};
        };
        std::vector<PlayerCombatSnapshot> others;
        for (size_t index = 0; index < 2; ++index)
            if (index != selfIndex)
            {
                const auto* other = players.findPlayer(mBinding.mPlayers[index]);
                if (other && other->transform().cell() == player->transform().cell())
                    others.push_back(snapshot(loadCombatStats(mRuntime.mStore, combat.actors[index]), other->playerId()));
            }
        std::vector<ActorCombatSnapshot> visible;
        const auto scene = moving && moving->actor ? moving->actor->snapshot() : mBinding.mNavigatingActor->snapshot();
        if (actorCell(scene) == player->transform().cell())
        {
            const auto npc = loadCombatStats(mRuntime.mStore, combat.actors[2]);
            visible.push_back({ActorId::fromValue(scene.mActor).value(), combatRevision,
                npc.getHealth().getCurrent(), npc.getHealth().getModified(),
                npc.getFatigue().getCurrent(), npc.getFatigue().getModified(),
                npc.getMagicka().getCurrent(), npc.getMagicka().getModified(),
                npc.getHealth().getCurrent() <= 0});
        }
        const std::array skillIds{ESM::Skill::Block, ESM::Skill::ShortBlade, ESM::Skill::LongBlade,
            ESM::Skill::BluntWeapon, ESM::Skill::Axe, ESM::Skill::Spear, ESM::Skill::HandToHand,
            ESM::Skill::LightArmor, ESM::Skill::MediumArmor, ESM::Skill::HeavyArmor,
            ESM::Skill::Unarmored, ESM::Skill::Security, ESM::Skill::Alteration,
            ESM::Skill::Conjuration, ESM::Skill::Destruction, ESM::Skill::Illusion,
            ESM::Skill::Mysticism, ESM::Skill::Restoration, ESM::Skill::Enchant};
        std::array<CombatSkillSnapshot, ReplicatedCombatSkillCount> skills{};
        for (size_t index = 0; index < skills.size(); ++index)
        {
            const auto& stat = combat.actors[selfIndex][11 + ESM::Skill::refIdToIndex(skillIds[index])];
            skills[index] = {static_cast<ReplicatedCombatSkill>(index),
                self.getSkill(skillIds[index]).getModified(), stat[4]};
        }
        auto created = LatestWinsCombatSnapshot::create(target, session->sessionGeneration(), tick, revision,
            player->playerId(), combatRevision, self.getHealth().getCurrent(), self.getHealth().getModified(),
            self.getFatigue().getCurrent(), self.getFatigue().getModified(), self.getMagicka().getCurrent(),
            self.getMagicka().getModified(), self.getHealth().getCurrent() <= 0,
            visible, skills, others);
        auto* value = std::get_if<LatestWinsCombatSnapshot>(&created);
        return value ? std::optional<LatestWinsCombatSnapshot>(std::move(*value)) : std::nullopt;
    }
    catch (...) { return {}; }

    std::optional<ReliableCombatEventBatch> InventoryService::projectCombatEvents(
        const CanonicalServerState& players, SessionId target, ServerTick tick, CanonicalRevision revision,
        const PreparedNativeInventory* candidate) const
    try
    {
        if (!mBinding.mCombatResolution || !candidate) return {};
        const auto* staged = dynamic_cast<const ActorTransaction*>(candidate);
        const auto* session = players.findActiveSession(target);
        if (!staged || &staged->service != this || staged->consumed || staged->before != mActorImage
            || !session || staged->tick != tick.value()
            || (!staged->playerHit && !staged->actorHit && staged->spellCasts.empty())) return {};
        const auto* observer = players.findPlayer(session->playerId());
        const auto scene = staged->actor ? staged->actor->snapshot() : mBinding.mNavigatingActor->snapshot();
        if (!observer || observer->transform().cell() != actorCell(scene)) return {};
        const std::vector<MeleeCombatEvent> playerEvents = staged->playerHit
            ? std::vector<MeleeCombatEvent>{*staged->playerHit} : std::vector<MeleeCombatEvent>{};
        const std::vector<ActorMeleeCombatEvent> actorEvents = staged->actorHit
            ? std::vector<ActorMeleeCombatEvent>{*staged->actorHit} : std::vector<ActorMeleeCombatEvent>{};
        const auto& magicEvents = staged->spellCasts;
        auto created = ReliableCombatEventBatch::create(target, session->sessionGeneration(), tick,
            revision, playerEvents, actorEvents, magicEvents);
        auto* value = std::get_if<ReliableCombatEventBatch>(&created);
        return value ? std::optional<ReliableCombatEventBatch>(std::move(*value)) : std::nullopt;
    }
    catch (...) { return {}; }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::project(const CanonicalServerState& players,
        SessionId target, ServerTick tick, CanonicalRevision revision, const PreparedCommand* candidate,
        const EquipmentRuntime::PreparedEquipment* equipped, const EquipmentRuntime::PreparedWorldTransfer* world,
        const EquipmentRuntime::PreparedDoor* door, std::optional<CellId> area,
        const ActorSceneSnapshot* moving, std::span<const WeaponWear> wear,
        const std::optional<ItemCharge>& charge,
        const ActorCampaignCombat* stagedCombat,
        const EquipmentRuntime::PreparedRespawn* respawn) const
    try
    {
        if (mRuntime.mRestartActor || mRuntime.mFailedClosed) return std::nullopt;
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player) return std::nullopt;
        const auto visibleCell = area.value_or(player->transform().cell());
        const auto index = actor(player->playerId());
        const auto values = [&](size_t owner) {
            auto state = respawn && respawn->owner() == owner ? respawn->values()
                : world ? mRuntime.preparedValues(*world, owner)
                : equipped ? mRuntime.preparedValues(*equipped, owner)
                : candidate ? mRuntime.preparedValues(candidate->mTransfer, owner) : mRuntime.installedValues(owner);
            for (const auto& change : wear) if (owner == change.owner)
            {
                const auto item = std::ranges::find(state.mObjects, change.before.mItem,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == state.mObjects.end()) throw std::invalid_argument("Native melee projection lost weapon identity");
                item->mRef.mChargeInt = change.condition;
                if (change.condition == 0) state.mSlots[MWWorld::InventoryStore::Slot_CarriedRight] = {};
            }
            if (charge && owner == charge->owner)
            {
                const auto item = std::ranges::find(state.mObjects, charge->item,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == state.mObjects.end() || item->mRef.mEnchantmentCharge != charge->before)
                    throw std::invalid_argument("Native magic projection lost item identity");
                if (charge->consume)
                {
                    if (item->mRef.mCount <= 0)
                        throw std::invalid_argument("Native magic projection lost consumable count");
                    --item->mRef.mCount;
                    if (item->mRef.mCount == 0)
                        for (auto& slot : state.mSlots) if (slot == charge->item) slot = {};
                }
                else item->mRef.mEnchantmentCharge = charge->after;
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
        const auto commandVersion = respawn ? respawn->revision()
            : world ? world->revision() : equipped ? equipped->candidate().mRevision
            : candidate ? candidate->candidate().mRevision : mWorld.getPtrRegistryRevision();
        const auto version = commandVersion + wear.size() + size_t(charge.has_value());
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
            const auto* life = stagedCombat ? stagedCombat : mCombat ? &*mCombat : nullptr;
            const bool selectedCorpse = life && i + 2 == mCombatNpcOwner && life->actors[2][8][2] <= 0;
            const auto selectedPosition = selectedCorpse
                ? std::optional(moving ? *moving : mBinding.mNavigatingActor->snapshot()) : std::nullopt;
            const auto sharedCell = selectedPosition ? actorCell(*selectedPosition) : shared.mCell;
            const auto sharedPosition = selectedPosition
                ? Position3(int64_t(std::llround(double(selectedPosition->mPosition[0]) * 1024)),
                    int64_t(std::llround(double(selectedPosition->mPosition[1]) * 1024)),
                    int64_t(std::llround(double(selectedPosition->mPosition[2]) * 1024))) : shared.mPosition;
            // Keep the authored placement controlled while its origin is visible,
            // too: otherwise a late observer could render its local frozen copy.
            // V20's fixed neighborhood always retains that origin cell.
            if (visibleCell != sharedCell
                && !(moving && shared.mId.value() == moving->mActor && visibleCell == actorCell(*moving))) continue;
            const auto owner = mRuntime.ownerPtr(i + 2);
            const auto sharedValues = values(i + 2);
            if (selectedCorpse)
                actors.push_back({shared.mId, publicSlots(sharedValues)});
            if (actorInventory(owner) && !initialCorpse(owner) && !selectedCorpse)
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
            auto baseline = ReliableContainerInventoryBaseline::create(header, shared.mId, sharedCell,
                sharedPosition, ContainerRevision::fromValue(version).value(), 0,
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
                auto neighbor = project(players, target, tick, revision, candidate, equipped, world, door,
                    domain->mCell, moving, wear, charge, stagedCombat);
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
