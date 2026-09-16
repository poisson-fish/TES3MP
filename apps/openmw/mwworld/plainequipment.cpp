#include "plainequipment.hpp"

#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>

#include "class.hpp"
#include "esmstore.hpp"
#include "inventorystore.hpp"
#include "worldmodel.hpp"

#include <components/compiler/locals.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/statstate.hpp>

#include "../mwmechanics/autocalcspell.hpp"

namespace MWWorld
{
    namespace
    {
        bool sameReference(const ConstPtr& a, const ConstPtr& b)
        {
            return a == b && a.mCell == b.mCell && a.mContainerStore == b.mContainerStore
                && a.getReferenceLifetime() == b.getReferenceLifetime();
        }

        void registered(const ConstPtr& ptr, const WorldModel& world)
        {
            if (!ptr.hasLiveReference())
                throw std::invalid_argument("Equipment reference lifetime expired");
            if (ptr.mRef->mWorldModel != &world || !ptr.getCellRef().getRefNum().isSet()
                || !sameReference(ptr, world.getPtr(ptr.getCellRef().getRefNum())))
                throw std::invalid_argument("Equipment reference registry binding changed");
        }

        void npcContent(const ESM::NPC& npc, const ESMStore& content)
        {
            if (npc.mNpdtType == ESM::NPC::NPC_DEFAULT)
            {
                if (npc.mNpdt.mHealth == 0)
                    throw std::invalid_argument("Equipment NPC death initialization requires world time");
            }
            else if (npc.mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS)
            {
                const auto* race = content.get<ESM::Race>().search(npc.mRace);
                if (!race || !content.get<ESM::Class>().search(npc.mClass) || npc.mNpdt.mLevel < 1)
                    throw std::invalid_argument("Equipment auto NPC requires race/class and living level");
                // Bound the eligible context before any stock health setter can
                // enter death/time services. The shared formulas remain stock.
                for (int i = 0; i < ESM::Attribute::Length; ++i)
                {
                    const auto value = race->mData.getAttribute(ESM::Attribute::indexToRefId(i), npc.isMale());
                    if (value < 1 || value > 100)
                        throw std::invalid_argument("Equipment auto NPC requires positive TES3 race attributes");
                }
            }
            else
                throw std::invalid_argument("Unsupported equipment NPDT type");
            if (!npc.mScript.empty() || !npc.mFaction.empty())
                throw std::invalid_argument("Equipment NPC scripts/faction initialization is unsupported");
            if (!npc.mRace.empty() && !content.get<ESM::Race>().search(npc.mRace))
                throw std::invalid_argument("Equipment NPC race is unavailable");
        }

        const ESM::Spell& equipmentSpell(ESM::RefId id, const ESMStore& content, const ESM::NPC& npc)
        {
            const auto* spell = content.get<ESM::Spell>().search(id);
            if (!spell || (spell->mData.mType != ESM::Spell::ST_Spell && spell->mData.mType != ESM::Spell::ST_Power))
            {
                MWMechanics::abilityFortifyLuckMagnitude(content, id);
                const auto* race = content.get<ESM::Race>().search(npc.mRace);
                if (!race || std::find(race->mPowers.mList.begin(), race->mPowers.mList.end(), id) == race->mPowers.mList.end())
                    throw std::invalid_argument("Equipment passive ability must belong to the actor's race");
            }
            return *spell;
        }

        const ESM::NPC& equipmentNpc(const Ptr& actor, const ESMStore& content)
        {
            if (!actor.hasLiveReference() || !actor.getCellRef().getRefNum().isSet()
                || actor.getType() != ESM::NPC::sRecordId || actor.getRefData().getCustomData())
                throw std::invalid_argument("Equipment NPC requires a live owner without another stat writer");
            const auto* npc = actor.get<ESM::NPC>()->mBase;
            if (content.get<ESM::NPC>().search(actor.getCellRef().getRefId()) != npc)
                throw std::invalid_argument("Equipment NPC content binding changed");
            npcContent(*npc, content);
            if (content.get<ESM::Attribute>().getSize() != ESM::Attribute::Length
                || content.get<ESM::Skill>().getSize() != ESM::Skill::Length)
                throw std::invalid_argument("Equipment NPC requires complete TES3 stat definitions");
            return *npc;
        }

        float npcMagickaMultiplier(const ESMStore& content)
        {
            const float value = content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat();
            if (!std::isfinite(value) || value < 0 || value > 1000)
                throw std::invalid_argument("Invalid NPC magicka multiplier");
            return value;
        }

        auto cellValues(const ESM::CellRef& ref)
        {
            return std::tie(ref.mRefNum, ref.mRefID, ref.mScale, ref.mOwner, ref.mGlobalVariable, ref.mSoul,
                ref.mFaction, ref.mFactionRank, ref.mChargeInt, ref.mChargeIntRemainder, ref.mEnchantmentCharge,
                ref.mCount, ref.mTeleport, ref.mDoorDest, ref.mDestCell, ref.mLockLevel, ref.mIsLocked, ref.mKey,
                ref.mTrap, ref.mReferenceBlocked, ref.mPos);
        }

        bool sameCell(const CellRef& a, const CellRef& b)
        {
            ESM::ObjectState x, y;
            a.writeState(x);
            b.writeState(y);
            return a.hasChanged() == b.hasChanged() && cellValues(x.mRef) == cellValues(y.mRef);
        }

        LiveCellRef<ESM::Clothing> detached(const LiveCellRef<ESM::Clothing>& ref)
        {
            LiveCellRef<ESM::Clothing> value(ESM::makeBlankCellRef(), ref.mBase);
            value.mRef = ref.mRef;
            value.mData = ref.mData.copyForContainerTransfer();
            return value;
        }

        void serialize(const CellRefList<ESM::Clothing>::List& nodes, PlainEquipmentValues& output)
        {
            const Compiler::Locals declarations;
            output.mObjects.reserve(nodes.size());
            for (const auto& node : nodes)
            {
                if (node.mWorldModel || node.mData.getBaseNode() || node.mData.getLuaScripts()
                    || node.mData.getCustomData() || node.mData.isDeletedByContentFile()
                    || node.mData.mPhysicsPostponed)
                    throw std::invalid_argument("Unsupported equipment runtime state for export");
                auto& object = output.mObjects.emplace_back();
                object.blank();
                node.mRef.writeState(object);
                node.mData.write(object, declarations);
                object.mHasCustomState = false;
            }
        }

        void textValue(std::string_view value, bool required = false)
        {
            if ((required && value.empty()) || value.size() > PlainEquipmentValues::MaxText
                || value.find('\0') != std::string_view::npos)
                throw std::invalid_argument("Invalid equipment text value");
        }

        void recordId(const ESM::RefId& id, bool required = false)
        {
            if (!required && id.empty())
                return;
            if (!id.is<ESM::StringRefId>())
                throw std::invalid_argument("Equipment values require TES3 string IDs");
            textValue(id.getRefIdString(), true);
        }

        void counterCovers(ESM::RefNum counter, ESM::RefNum id)
        {
            // Stock generation rolls index overflow into a more negative file.
            if (!id.isSet()
                || (id.mContentFile < 0
                    && (id.mContentFile < counter.mContentFile
                        || (id.mContentFile == counter.mContentFile && id.mIndex > counter.mIndex))))
                throw std::invalid_argument("Equipment identity exceeds saved counter or is unset");
        }

        void validateValues(const PlainEquipmentValues& input, const ESMStore& content, ESM::RefNum expectedActor)
        {
            if (input.mObjects.size() > PlainEquipmentValues::MaxItems || input.mActor != expectedActor
                || !input.mActor.isSet() || input.mLastGenerated.mContentFile >= 0)
                throw std::invalid_argument("Invalid equipment owner, membership or counter");
            counterCovers(input.mLastGenerated, input.mActor);
            const Compiler::Locals declarations;
            int64_t total = 0;
            float magnitude = 0;
            bool shirtFound = input.mShirt == ESM::RefNum{};
            bool selectedFound = input.mSelected == ESM::RefNum{};
            for (size_t i = 0; i < input.mObjects.size(); ++i)
            {
                const auto& object = input.mObjects[i];
                const auto& ref = object.mRef;
                counterCovers(input.mLastGenerated, ref.mRefNum);
                if (ref.mRefNum == input.mActor)
                    throw std::invalid_argument("Equipment item aliases owner identity");
                for (size_t j = 0; j < i; ++j)
                    if (input.mObjects[j].mRef.mRefNum == ref.mRefNum)
                        throw std::invalid_argument("Duplicate equipment identity");
                recordId(ref.mRefID, true);
                const auto* base = content.get<ESM::Clothing>().search(ref.mRefID);
                if (!base || base->mData.mType != ESM::Clothing::Shirt || !base->mScript.empty()
                    || (!input.mNpcStats && !base->mEnchant.empty()))
                    throw std::invalid_argument("Equipment values require supplied plain shirt content");
                const auto effect = MWMechanics::constantFortifyLuckMagnitude(content, base->mEnchant);
                if (ref.mRefNum == input.mShirt)
                    magnitude = effect;
                for (const auto& id : { ref.mOwner, ref.mSoul, ref.mFaction, ref.mKey, ref.mTrap })
                    recordId(id);
                textValue(ref.mGlobalVariable);
                textValue(ref.mDestCell);
                if (ref.mCount == std::numeric_limits<int>::min() || !std::isfinite(ref.mScale) || ref.mScale <= 0
                    || ref.mChargeInt < -1 || !std::isfinite(ref.mChargeIntRemainder)
                    || !std::isfinite(ref.mEnchantmentCharge) || ref.mEnchantmentCharge < -1)
                    throw std::invalid_argument("Invalid equipment CellRef numeric value");
                total += std::abs(static_cast<int64_t>(ref.mCount));
                if (total > std::numeric_limits<int>::max())
                    throw std::invalid_argument("Equipment total count bound exceeded");
                for (const auto* position : { &ref.mPos, &ref.mDoorDest })
                    for (int axis = 0; axis < 3; ++axis)
                        if (!std::isfinite(position->pos[axis]) || !std::isfinite(position->rot[axis]))
                            throw std::invalid_argument("Nonfinite equipment CellRef position");
                if (object.mAnimationState.mScriptedAnims.size() > PlainEquipmentValues::MaxAnimations)
                    throw std::invalid_argument("Oversized equipment animation state");
                for (const auto& animation : object.mAnimationState.mScriptedAnims)
                    textValue(animation.mGroup, true);
                RefData::validateRestore(object, {}, declarations);
                if (ref.mRefNum == input.mShirt)
                {
                    if (std::abs(ref.mCount) != 1)
                        throw std::invalid_argument("Equipment shirt slot requires one active item");
                    shirtFound = true;
                }
                if (ref.mRefNum == input.mSelected)
                    selectedFound = true; // Stock selections may retain dormant members.
            }
            if (input.mNpcStats)
            {
                input.mNpcStats->validate(content);
                if (input.mNpcStats->mAttributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)][1] != magnitude)
                    throw std::invalid_argument("Equipment NPC Luck disagrees with constant effect");
            }
            if (!shirtFound || !selectedFound)
                throw std::invalid_argument("Foreign equipment shirt or selection identity");
        }
    }

    void EquipmentNpcStatsValues::validate(const ESMStore& content) const
    {
        const auto* npc = content.get<ESM::NPC>().search(mBase);
        if (!npc)
            throw std::invalid_argument("Equipment stats require known NPC content");
        npcContent(*npc, content);
        bool ended = false;
        float abilityMagnitude = 0;
        for (size_t i = 0; i < mSpells.size(); ++i)
        {
            if (mSpells[i].empty())
            {
                ended = true;
                continue;
            }
            recordId(mSpells[i], true);
            if (ended || std::find(mSpells.begin(), mSpells.begin() + i, mSpells[i]) != mSpells.begin() + i)
                throw std::invalid_argument("Invalid equipment NPC initialized spell list");
            const auto& spell = equipmentSpell(mSpells[i], content, *npc);
            if (spell.mData.mType == ESM::Spell::ST_Ability)
            {
                if (abilityMagnitude)
                    throw std::invalid_argument("Equipment supports only one race ability");
                abilityMagnitude = MWMechanics::abilityFortifyLuckMagnitude(content, spell.mId);
            }
        }
        if (const auto* race = content.get<ESM::Race>().search(npc->mRace))
            for (const auto id : race->mPowers.mList)
                if (equipmentSpell(id, content, *npc).mData.mType == ESM::Spell::ST_Ability
                    && std::find(mSpells.begin(), mSpells.end(), id) == mSpells.end())
                    throw std::invalid_argument("Equipment save omitted its race ability");
        if (mAbilityMagnitude != abilityMagnitude)
            throw std::invalid_argument("Equipment save passive contribution disagrees with content");
        for (const auto& value : mAttributes)
            if (!std::isfinite(value[0]) || value[0] < 0 || value[0] > 1000
                || !std::isfinite(value[1]) || std::abs(value[1]) > 1000
                || !std::isfinite(value[2]) || value[2] < 0 || value[2] > 2000)
                throw std::invalid_argument("Invalid equipment NPC attribute values");
        for (const auto& value : mDynamic)
            if (!std::isfinite(value[0]) || value[0] < 0 || value[0] > 1000000
                || !std::isfinite(value[1]) || std::abs(value[1]) > 1000000
                || !std::isfinite(value[2]) || std::abs(value[2]) > 1000000)
                throw std::invalid_argument("Invalid equipment NPC dynamic values");
        // Death needs world time and further actor state; it is outside this operation.
        if (mDynamic[0][2] < 1)
            throw std::invalid_argument("Equipment NPC death state is unsupported");
    }

    EquipmentNpcStats::EquipmentNpcStats(const Ptr& actor, const ESMStore& content)
        : mActor(actor)
        , mIdentity(actor.hasLiveReference() ? actor.getCellRef().getRefNum() : ESM::RefNum{})
        , mContent(content)
        , mBase(&equipmentNpc(actor, content))
        , mNpdtType(mBase->mNpdtType)
        , mMagickaMultiplier(npcMagickaMultiplier(content))
        , mStats(content)
    {
        if (mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS)
            mStats.initializeAutoStats(*mBase, content, mMagickaMultiplier);
        else
            mStats.initializeExplicitStats(*mBase, mMagickaMultiplier);
        // Stock startup order: base spells, generated instance spells, race powers.
        // Use owned instance membership, never the shared base-record SpellList.
        const auto add = [&](const std::vector<ESM::RefId>& ids) {
            for (const auto id : ids)
            {
                const auto* spell = &equipmentSpell(id, content, *mBase);
                if (!mStats.getSpells().hasSpell(spell))
                {
                    if (mStats.getSpells().count() == EquipmentNpcStatsValues::MaxSpells)
                        throw std::invalid_argument("Equipment NPC initialized spell limit exceeded");
                    mStats.getSpells().add(spell, false);
                }
            }
        };
        add(mBase->mSpells.mList);
        const auto* race = content.get<ESM::Race>().search(mBase->mRace);
        if (mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS)
            add(MWMechanics::autoCalcNpcSpells(mStats.getSkills(), mStats.getAttributes(), race, content));
        if (race)
            add(race->mPowers.mList);
        if (mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS)
            mStats.recalculateMagicka(mMagickaMultiplier);
        // Stock activation follows spell generation; abilities must not change
        // the attributes used for auto-NPDT spell eligibility.
        mStats.getActiveSpells().activateFortifyLuckAbility(mActor, mContent, mStats);
        mInitialSpells = values().mSpells;
    }

    void EquipmentNpcStats::validate(const Ptr& actor, const ESMStore& content) const
    {
        if (&content != &mContent || !mActor.hasLiveReference() || !sameReference(actor, mActor)
            || &equipmentNpc(actor, content) != mBase || actor.getCellRef().getRefNum() != mIdentity
            || mBase->mNpdtType != mNpdtType || npcMagickaMultiplier(content) != mMagickaMultiplier)
            throw std::invalid_argument("Equipment NPC stat actor/content binding changed");
        const auto saved = values();
        saved.validate(content);
        if (saved.mSpells != mInitialSpells || !mStats.getSpells().getSelectedSpell().empty())
            throw std::invalid_argument("Equipment NPC initialized spell membership changed");
        for (const auto* spell : mStats.getSpells())
            if (&equipmentSpell(spell->mId, content, *mBase) != spell)
                throw std::invalid_argument("Equipment NPC spell content binding changed");
    }

    EquipmentNpcStatsValues EquipmentNpcStats::values() const
    {
        EquipmentNpcStatsValues result;
        result.mBase = mBase->mId;
        const auto& spells = mStats.getSpells();
        if (spells.count() > result.mSpells.size())
            throw std::invalid_argument("Equipment NPC spell limit exceeded");
        for (size_t i = 0; i < spells.count(); ++i)
            result.mSpells[i] = spells.at(i)->mId;
        for (const auto& spell : mStats.getActiveSpells())
            if (spell.hasFlag(ESM::ActiveSpells::Flag_AffectsBaseValues))
                for (const auto& effect : spell.getEffects())
                    result.mAbilityMagnitude += effect.mMagnitude;
        for (size_t i = 0; i < result.mAttributes.size(); ++i)
        {
            ESM::StatState<float> value;
            mStats.getAttribute(ESM::Attribute::indexToRefId(static_cast<int>(i))).writeState(value);
            result.mAttributes[i] = { value.mBase, value.mMod, value.mDamage };
        }
        for (size_t i = 0; i < result.mDynamic.size(); ++i)
        {
            ESM::StatState<float> value;
            mStats.getDynamic(static_cast<int>(i)).writeState(value);
            result.mDynamic[i] = { value.mBase, value.mMod, value.mCurrent };
        }
        return result;
    }

    void EquipmentNpcStats::restore(const EquipmentNpcStatsValues& values, const InventoryStore& inventory)
    {
        values.validate(mContent);
        if (values.mBase != mBase->mId || values.mSpells != mInitialSpells)
            throw std::invalid_argument("Equipment NPC save base/initialized spells differ from bound actor");
        // Fresh construction has already activated the validated race ability.
        // Saved base values include it; retain that active owner while replacing
        // the base values, then reconstruct only the shirt's modifier below.
        for (size_t i = 0; i < values.mAttributes.size(); ++i)
        {
            const auto id = ESM::Attribute::indexToRefId(static_cast<int>(i));
            const auto& value = values.mAttributes[i];
            ESM::StatState<float> state;
            state.mBase = value[0];
            state.mMod = id == ESM::Attribute::Luck ? 0.f : value[1];
            state.mDamage = value[2];
            MWMechanics::AttributeValue attribute;
            attribute.readState(state);
            mStats.setAttribute(id, attribute, mMagickaMultiplier);
        }
        // Attribute changes recalculate magicka/fatigue. Saved dynamic fields
        // are installed afterwards using their stock field readers.
        for (size_t i = 0; i < values.mDynamic.size(); ++i)
        {
            const auto& value = values.mDynamic[i];
            ESM::StatState<float> state;
            state.mBase = value[0];
            state.mMod = value[1];
            state.mCurrent = value[2];
            MWMechanics::DynamicStat<float> dynamic;
            dynamic.readState(state);
            mStats.setDynamic(static_cast<int>(i), dynamic);
        }
        mStats.getActiveSpells().updateConstantFortifyLuck(mActor, inventory, mContent, mStats);
        if (this->values() != values)
            throw std::invalid_argument("Equipment NPC source stats disagree with equipment consequence");
    }

    void PlainEquipmentValues::validate(const ESMStore& content, ESM::RefNum expectedActor) const
    {
        validateValues(*this, content, expectedActor);
    }

    void PlainEquipmentValues::swap(PlainEquipmentValues& other) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<ESM::RefNum>);
        std::swap(mActor, other.mActor);
        std::swap(mShirt, other.mShirt);
        std::swap(mSelected, other.mSelected);
        std::swap(mLastGenerated, other.mLastGenerated);
        mObjects.swap(other.mObjects);
        mNpcStats.swap(other.mNpcStats);
    }

    struct PreparedPlainEquipment::State
    {
        struct Node
        {
            ConstPtr mLive;
            LiveCellRef<ESM::Clothing> mValue;
        };
        ContainerStoreResolution mResolution;
        PlainEquipmentContext mContext;
        std::weak_ptr<const void> mScriptsLifetime;
        PtrRegistry::Snapshot mRegistry;
        LocalScripts::List mScripts;
        std::vector<Node> mBefore;
        ESM::RefNum mShirt, mSelected;
        InventoryStoreListener* mEquipmentListener;
        ContainerStoreListener* mContainerListener;
        // Never copy an InventoryStore: it would retain live services, owner,
        // RefData aliases and iterators. Construct fresh storage and relocate slots.
        InventoryStore mCandidate;
        PlainEquipmentResult mResult;
        std::shared_ptr<EquipmentNpcStats> mNpcStats;
        EquipmentNpcStatsValues mBeforeStats;

        State(const ContainerStoreResolution& resolution, const PlainEquipmentContext& context)
            : mResolution(resolution)
            , mContext(context)
            , mScriptsLifetime(context.mLocalScripts.lifetimeWitness())
            , mEquipmentListener(nullptr)
            , mContainerListener(nullptr)
        {
        }

        static const InventoryStore& inventory(
            const ContainerStoreResolution& resolution, const PlainEquipmentContext& context)
        {
            const auto lifetime = resolution.mLifetime.lock();
            if (!lifetime || lifetime->mStore != resolution.mStore)
                throw std::invalid_argument("Equipment inventory lifetime expired");
            const auto& base = *resolution.mStore;
            if (base.mStorageIdentity != resolution.mStorage || base.mResolutionLifetime != lifetime)
                throw std::invalid_argument("Equipment inventory storage changed");
            registered(resolution.mOwner, context.mWorldModel);
            registered(context.mActor, context.mWorldModel);
            registered(context.mPlayer, context.mWorldModel);
            if (typeid(base) != typeid(InventoryStore) || !base.mResolved || !context.mActor.getClass().isNpc()
                || !sameReference(context.mActor, context.mPlayer) || !sameReference(resolution.mOwner, context.mActor)
                || !sameReference(base.getPtr(context.mWorldModel), context.mActor))
                throw std::invalid_argument("Equipment actor/player/resolved inventory mismatch");
            if (!context.mLocalScripts.usesStore(context.mStore))
                throw std::invalid_argument("Equipment script content service mismatch");
            const auto& inventory = static_cast<const InventoryStore&>(base);
            const auto& lists = base.mLists;
            if (!lists.mPotions.mList.empty() || !lists.mAppas.mList.empty() || !lists.mArmors.mList.empty()
                || !lists.mBooks.mList.empty() || !lists.mIngreds.mList.empty() || !lists.mLights.mList.empty()
                || !lists.mLockpicks.mList.empty() || !lists.mMiscItems.mList.empty() || !lists.mProbes.mList.empty()
                || !lists.mRepairs.mList.empty() || !lists.mWeapons.mList.empty()
                || lists.mClothes.mList.size() > MaxItems || !inventory.mUpdatesEnabled)
                throw std::invalid_argument(
                    "Equipment preparation requires bounded plain clothing storage and updates");
            return inventory;
        }

        static ESM::RefNum position(const InventoryStore& store, const ContainerStoreIterator& selection)
        {
            if (selection == store.end())
                return {};
            // Compare against current raw members. Never follow a saved iterator,
            // including a dormant, foreign, erased or replaced selection.
            for (auto it = store.mLists.mClothes.mList.begin(); it != store.mLists.mClothes.mList.end(); ++it)
                if (ConstContainerStoreIterator(&store, it) == selection)
                    return it->mRef.getRefNum();
            throw std::invalid_argument("Equipment iterator is not a current inventory member");
        }

        static void plain(
            const LiveCellRef<ESM::Clothing>& node, const InventoryStore& store, const PlainEquipmentContext& context)
        {
            ConstPtr item(&node, nullptr);
            item.mContainerStore = &store;
            registered(item, context.mWorldModel);
            if (context.mStore.get<ESM::Clothing>().search(node.mRef.getRefId()) != node.mBase
                || !node.mBase->mScript.empty() || (!context.mNpcStats && !node.mBase->mEnchant.empty())
                || node.mBase->mData.mType != ESM::Clothing::Shirt || !node.mData.getLocals().getScriptId().empty()
                || !node.mData.getLocals().isEmpty() || node.mData.getLuaScripts() || node.mData.getCustomData()
                || node.mData.isDeletedByContentFile() || node.mRef.getCount(false) == std::numeric_limits<int>::min()
                || context.mLocalScripts.prepareRemove(&node.mRef).hasRegistration())
                throw std::invalid_argument("Equipment preparation supports only plain non-scripted shirts");
            MWMechanics::constantFortifyLuckMagnitude(context.mStore, node.mBase->mEnchant);
        }

        void capture(const InventoryStore& source)
        {
            mRegistry = mContext.mWorldModel.snapshotPtrRegistry();
            mScripts = mContext.mLocalScripts.snapshot();
            mEquipmentListener = source.mInventoryListener;
            mContainerListener = source.mListener;
            mShirt = position(source, source.mSlots[InventoryStore::Slot_Shirt]);
            mSelected = position(source, source.mSelectedEnchantItem);
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (slot != InventoryStore::Slot_Shirt && source.mSlots[slot] != source.end())
                    throw std::invalid_argument("Equipment preparation supports only the shirt slot");
            int64_t total = 0;
            for (const auto& ref : source.mLists.mClothes.mList)
            {
                plain(ref, source, mContext);
                total += std::abs(static_cast<int64_t>(ref.mRef.getCount(false)));
                if (total > std::numeric_limits<int>::max())
                    throw std::invalid_argument("Equipment restack count bound exceeded");
                ConstPtr live(&ref, nullptr);
                live.mContainerStore = &source;
                mBefore.push_back({ live, detached(ref) });
                auto& node = mCandidate.mLists.mClothes.mList.emplace_back(detached(ref));
                auto it = ContainerStoreIterator(&mCandidate, std::prev(mCandidate.mLists.mClothes.mList.end()));
                if (node.mRef.getRefNum() == mShirt)
                {
                    if (node.mRef.getCount() != 1)
                        throw std::invalid_argument("Equipment shirt slot must contain one item");
                    mCandidate.mSlots[InventoryStore::Slot_Shirt] = it;
                }
                if (node.mRef.getRefNum() == mSelected)
                    mCandidate.mSelectedEnchantItem = it;
            }
            mCandidate.mResolved = true;
            if (mContext.mNpcStats)
            {
                mContext.mNpcStats->validate(mContext.mActor, mContext.mStore);
                const auto& stats = mContext.mNpcStats->stats();
                stats.getActiveSpells().validateConstantFortifyLuck(mContext.mActor, source, mContext.mStore, stats);
                mBeforeStats = mContext.mNpcStats->values();
                mNpcStats = std::make_shared<EquipmentNpcStats>(mContext.mActor, mContext.mStore);
                mNpcStats->restore(mBeforeStats, mCandidate);
            }
            mResult.mActor = mContext.mActor.getCellRef().getRefNum();
            mResult.mLastGenerated = mRegistry.mLastGenerated;
        }

        void check(const PlainEquipmentContext& context) const
        {
            if (mScriptsLifetime.expired() || &context.mStore != &mContext.mStore
                || &context.mWorldModel != &mContext.mWorldModel || &context.mLocalScripts != &mContext.mLocalScripts
                || context.mNpcStats != mContext.mNpcStats
                || (context.mNpcStats && context.mNpcStats->values() != mBeforeStats)
                || !sameReference(context.mActor, mContext.mActor) || !sameReference(context.mPlayer, mContext.mPlayer))
                throw std::invalid_argument("Equipment preparation context/service changed");
            const auto& source = inventory(mResolution, context);
            if (context.mNpcStats)
            {
                context.mNpcStats->validate(context.mActor, context.mStore);
                const auto& stats = context.mNpcStats->stats();
                stats.getActiveSpells().validateConstantFortifyLuck(context.mActor, source, context.mStore, stats);
            }
            if (context.mWorldModel.snapshotPtrRegistry() != mRegistry || context.mLocalScripts.snapshot() != mScripts
                || source.mInventoryListener != mEquipmentListener || source.mListener != mContainerListener
                || position(source, source.mSlots[InventoryStore::Slot_Shirt]) != mShirt
                || position(source, source.mSelectedEnchantItem) != mSelected
                || source.mLists.mClothes.mList.size() != mBefore.size())
                throw std::invalid_argument("Equipment preparation source state changed");
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (slot != InventoryStore::Slot_Shirt && source.mSlots[slot] != source.end())
                    throw std::invalid_argument("Equipment preparation other slot changed");
            size_t index = 0;
            for (const auto& ref : source.mLists.mClothes.mList)
            {
                const auto& before = mBefore[index++];
                ConstPtr current(&ref, nullptr);
                current.mContainerStore = &source;
                if (!before.mLive.hasLiveReference() || !sameReference(current, before.mLive))
                    throw std::invalid_argument("Equipment preparation item lifetime/membership changed");
                plain(ref, source, context);
                if (!sameCell(ref.mRef, before.mValue.mRef)
                    || !ref.mData.matchesContainerTransferState(before.mValue.mData))
                    throw std::invalid_argument("Equipment preparation item values changed");
            }
        }

        void effect(PlainEquipmentResult::EffectKind kind, const Ptr& item = {}, int count = 0)
        {
            mResult.mEffects.push_back(
                { kind, mResult.mActor, item.isEmpty() ? ESM::RefNum() : item.getCellRef().getRefNum(), count });
        }

        void run(ESM::RefNum identity, bool equip)
        {
            auto item = mCandidate.begin();
            while (item != mCandidate.end() && item->getCellRef().getRefNum() != identity)
                ++item;
            if (item == mCandidate.end() || (equip && mShirt == identity) || (!equip && mShirt != identity))
                throw std::invalid_argument("Equipment item is stale, dormant or already in requested state");
            using Kind = PlainEquipmentResult::EffectKind;
            InventoryStoreEquipmentContext context{
                { mContext.mStore,
                    [this](const Ptr& split) {
                        auto& counter = mResult.mLastGenerated;
                        if (counter.mContentFile >= 0
                            || (counter.mIndex == std::numeric_limits<uint32_t>::max()
                                && counter.mContentFile == std::numeric_limits<int32_t>::min()))
                            throw std::invalid_argument("Equipment identity counter exhausted or invalid");
                        const auto id = split.getCellRef().getOrAssignRefNum(counter);
                        if (mRegistry.mEntries.contains(id))
                            throw std::invalid_argument("Equipment split identity collision");
                        effect(Kind::RegisterSplit, split);
                    },
                    [this](const Ptr& original, int count) {
                        const auto removal = ContainerStore::prepareRemoveCount(original.getCellRef(), count);
                        if (removal.mFullRemoval)
                            throw std::logic_error("Equipment split unexpectedly removed a whole stack");
                        original.getCellRef() = original.getCellRef().copyWithCount(removal.mRemainingCount);
                        mCandidate.flagAsModified();
                        effect(Kind::InventoryUpdated);
                        if (mContainerListener)
                            effect(Kind::ItemRemoved, original, removal.mRemoved);
                        effect(Kind::InventoryUpdated);
                    },
                    [this](const Ptr& original) {
                        original.getCellRef() = original.getCellRef().copyWithCount(0);
                        effect(Kind::DeleteStackScript, original);
                    } },
                mContext.mActor, mContext.mPlayer,
                [](const Ptr&, const ESM::RefId&) { throw std::logic_error("Unexpected equipment script effect"); },
                [this](const Ptr&) { effect(Kind::EquipmentChanged); }
            };
            if (equip)
                mCandidate.equip(InventoryStore::Slot_Shirt, item, context);
            else
                mCandidate.unequipSlot(InventoryStore::Slot_Shirt, context);
            if (mNpcStats)
                mNpcStats->mStats.getActiveSpells().updateConstantFortifyLuck(
                    mContext.mActor, mCandidate, mContext.mStore, mNpcStats->mStats);
            if (mNpcStats)
            {
                const auto& luck = mNpcStats->mStats.getAttribute(ESM::Attribute::Luck);
                mResult.mLuck = { luck.getBase(), luck.getModifier(), luck.getDamage() };
            }
            mResult.mShirt = position(mCandidate, mCandidate.mSlots[InventoryStore::Slot_Shirt]);
            mResult.mSelected = position(mCandidate, mCandidate.mSelectedEnchantItem);
            for (const auto& ref : mCandidate.mLists.mClothes.mList)
            {
                if (ref.mWorldModel || ref.mData.getBaseNode() || ref.mData.getLuaScripts()
                    || ref.mData.getCustomData())
                    throw std::logic_error("Equipment candidate retained a live service or mutable alias");
                mResult.mItems.push_back({ ref.mRef.getRefNum(), ref.mRef.getRefId(), ref.mRef.getCount(false) });
            }
        }
    };

    PreparedPlainEquipment::PreparedPlainEquipment(std::unique_ptr<State> state)
        : mState(std::move(state))
    {
    }
    PreparedPlainEquipment::PreparedPlainEquipment(PreparedPlainEquipment&&) noexcept = default;
    PreparedPlainEquipment& PreparedPlainEquipment::operator=(PreparedPlainEquipment&&) noexcept = default;
    PreparedPlainEquipment::~PreparedPlainEquipment() = default;

    PreparedPlainEquipment PreparedPlainEquipment::prepare(const ContainerStoreResolution& inventory,
        const ConstPtr& item, ESM::RefNum expectedIdentity, size_t expectedRegistryRevision, bool equip,
        const PlainEquipmentContext& context)
    {
        const auto& source = State::inventory(inventory, context);
        if (context.mWorldModel.getPtrRegistryRevision() != expectedRegistryRevision)
            throw std::invalid_argument("Equipment request registry revision changed");
        registered(item, context.mWorldModel);
        if (item.mContainerStore != &source || item.getCellRef().getRefNum() != expectedIdentity)
            throw std::invalid_argument("Equipment request item owner/identity mismatch");
        bool found = false;
        for (const auto& node : source.mLists.mClothes.mList)
            found = found || &node == item.mRef;
        if (!found)
            throw std::invalid_argument("Equipment request item is foreign");
        auto state = std::make_unique<State>(inventory, context);
        state->capture(source);
        state->run(expectedIdentity, equip);
        state->check(context);
        return PreparedPlainEquipment(std::move(state));
    }

    void PreparedPlainEquipment::validate(const PlainEquipmentContext& context) const
    {
        if (!mState)
            throw std::invalid_argument("Equipment preparation was moved");
        mState->check(context);
    }

    const PlainEquipmentResult& PreparedPlainEquipment::result() const
    {
        if (!mState)
            throw std::invalid_argument("Equipment preparation was moved");
        return mState->mResult;
    }

    InventoryStore& PreparedPlainEquipment::installationCandidate(
        const PlainEquipmentContext& context, const InventoryStore& target)
    {
        validate(context);
        if (mState->mResolution.mStore != &target)
            throw std::invalid_argument("Equipment installation target changed");
        return mState->mCandidate;
    }

    std::shared_ptr<EquipmentNpcStats>& PreparedPlainEquipment::installationNpcStats()
    {
        return mState->mNpcStats;
    }

    void PreparedPlainEquipment::exportValues(const PlainEquipmentContext& context, PlainEquipmentValues& output) const
    {
        validate(context);
        const auto& result = mState->mResult;
        PlainEquipmentValues staged{ result.mActor, result.mShirt, result.mSelected, result.mLastGenerated, {} };
        if (mState->mNpcStats)
            staged.mNpcStats = mState->mNpcStats->values();
        serialize(mState->mCandidate.mLists.mClothes.mList, staged);
        validateValues(staged, context.mStore, result.mActor);
        validate(context);
        output.swap(staged);
    }

    struct RestoredPlainEquipment::State
    {
        InventoryStore mInventory;
        ESM::RefNum mActor, mLastGenerated;
        const ESMStore* mContent = nullptr;
        std::optional<EquipmentNpcStatsValues> mNpcStats;
    };

    RestoredPlainEquipment::RestoredPlainEquipment(std::unique_ptr<State> state)
        : mState(std::move(state))
    {
    }
    RestoredPlainEquipment::RestoredPlainEquipment(RestoredPlainEquipment&&) noexcept = default;
    RestoredPlainEquipment& RestoredPlainEquipment::operator=(RestoredPlainEquipment&&) noexcept = default;
    RestoredPlainEquipment::~RestoredPlainEquipment() = default;

    RestoredPlainEquipment RestoredPlainEquipment::restore(
        const PlainEquipmentValues& input, const ESMStore& content, ESM::RefNum expectedActor)
    {
        validateValues(input, content, expectedActor);
        auto staged = std::make_unique<State>();
        staged->mActor = input.mActor;
        staged->mLastGenerated = input.mLastGenerated;
        staged->mContent = &content;
        staged->mNpcStats = input.mNpcStats;
        auto& inventory = staged->mInventory;
        const Compiler::Locals declarations;
        for (const auto& object : input.mObjects)
        {
            auto& node = inventory.mLists.mClothes.mList.emplace_back(
                object.mRef, content.get<ESM::Clothing>().search(object.mRef.mRefID));
            node.mData = RefData::restore(object, {}, declarations);
            // Capture before publishing the detached owner. Later relocation
            // must not lazily mutate retained input on an allocation rejection.
            ConstPtr witness(&node, nullptr);
            const auto it = ContainerStoreIterator(&inventory, std::prev(inventory.mLists.mClothes.mList.end()));
            if (object.mRef.mRefNum == input.mShirt)
                inventory.mSlots[InventoryStore::Slot_Shirt] = it;
            if (object.mRef.mRefNum == input.mSelected)
                inventory.mSelectedEnchantItem = it;
        }
        inventory.mResolved = true;
        return RestoredPlainEquipment(std::move(staged));
    }

    InventoryStore& RestoredPlainEquipment::installationCandidate(
        const ESMStore& content, ESM::RefNum actor, ESM::RefNum counter) const
    {
        if (!mState || mState->mContent != &content || mState->mActor != actor || mState->mLastGenerated != counter)
            throw std::invalid_argument("Restored equipment content, actor or saved counter binding changed");
        for (const auto& node : mState->mInventory.mLists.mClothes.mList)
            if (content.get<ESM::Clothing>().search(node.mRef.getRefId()) != node.mBase)
                throw std::invalid_argument("Restored equipment base record binding changed");
        return mState->mInventory;
    }

    void RestoredPlainEquipment::exportValues(PlainEquipmentValues& output) const
    {
        if (!mState)
            throw std::invalid_argument("Restored equipment was moved");
        const auto& inventory = mState->mInventory;
        const auto identity = [&](const ContainerStoreIterator& selection) {
            if (selection == inventory.end())
                return ESM::RefNum{};
            // Dereferencing an iterator constructs Ptr and can allocate a lazy
            // lifetime token in the node. Export must not mutate retained storage.
            const auto& nodes = inventory.mLists.mClothes.mList;
            for (auto it = nodes.begin(); it != nodes.end(); ++it)
                if (ConstContainerStoreIterator(&inventory, it) == selection)
                    return it->mRef.getRefNum();
            throw std::logic_error("Restored equipment selection lost its member");
        };
        PlainEquipmentValues staged{ mState->mActor, identity(inventory.mSlots[InventoryStore::Slot_Shirt]),
            identity(inventory.mSelectedEnchantItem), mState->mLastGenerated, {} };
        staged.mNpcStats = mState->mNpcStats;
        serialize(inventory.mLists.mClothes.mList, staged);
        output.swap(staged);
    }
}
