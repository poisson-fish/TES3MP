#include "plainequipment.hpp"

#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>

#include "class.hpp"
#include "containeradd.hpp"
#include "esmstore.hpp"
#include "inventorystore.hpp"
#include "inventoryitem.hpp"
#include "manualref.hpp"
#include "worldmodel.hpp"

#include <components/compiler/locals.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/statstate.hpp>

#include "../mwmechanics/autocalcspell.hpp"
#include "../mwclass/clothing.hpp"
#include "../mwclass/armor.hpp"
#include "../mwclass/weapon.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwscript/itemlocals.hpp"

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

        template<class T> LiveCellRef<T> detached(const LiveCellRef<T>& ref)
        {
            LiveCellRef<T> value(ESM::makeBlankCellRef(), ref.mBase);
            value.mRef = ref.mRef;
            value.mData = ref.mData.copyForContainerTransfer();
            return value;
        }

        template<class T> void serializeNode(const T& node, PlainEquipmentValues& output,
            const ESMStore& content, const EquipmentScriptLocals* scripts)
        {
            if (node.mWorldModel || node.mData.getBaseNode() || node.mData.getLuaScripts()
                || node.mData.getCustomData() || node.mData.isDeletedByContentFile()
                || node.mData.mPhysicsPostponed)
                throw std::invalid_argument("Unsupported equipment runtime state for export");
            auto& object = output.mObjects.emplace_back();
            object.blank();
            node.mRef.writeState(object);
            node.mData.write(object, equipmentDeclarations(content, node.mData.getLocals().getScriptId(), scripts));
            object.mHasCustomState = false;
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

        void validateValues(const PlainEquipmentValues& input, const ESMStore& content, ESM::RefNum expectedActor,
            const EquipmentScriptLocals* scripts)
        {
            if (input.mObjects.size() > PlainEquipmentValues::MaxItems || input.mActor != expectedActor
                || !input.mActor.isSet() || input.mLastGenerated.mContentFile >= 0)
                throw std::invalid_argument("Invalid equipment owner, membership or counter");
            counterCovers(input.mLastGenerated, input.mActor);
            int64_t total = 0;
            float magnitude = 0;
            bool shirtFound = input.mSlots[InventoryStore::Slot_Shirt] == ESM::RefNum{};
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
                const auto record = inventoryItemRecord(content, ref.mRefID);
                const auto* base = &record;
                validateEquipmentItemSlots(record, ref.mRefNum, ref.mCount, input.mSlots, input.mNpcStats.has_value());
                if (!base->mScript.empty() && !input.mNpcStats)
                    throw std::invalid_argument("Inventory script services or equipment type unsupported");
                const auto effect = ref.mRefNum == input.mSlots[InventoryStore::Slot_Shirt]
                    ? MWMechanics::constantFortifyLuckMagnitude(content, base->mEnchant) : 0.f;
                if (ref.mRefNum == input.mSlots[InventoryStore::Slot_Shirt])
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
                if (!base->mScript.empty() && (std::abs(static_cast<int64_t>(ref.mCount)) > 1 || base->mEnchant.empty()))
                    throw std::invalid_argument("Scripted equipment requires single constant shirts");
                const auto& declarations = equipmentDeclarations(content, base->mScript, scripts);
                RefData::validateRestore(object, base->mScript, declarations);
                // The equipment format preserves the stock field writer's order.
                // Reject noncanonical owned input as well as noncanonical bytes.
                size_t localIndex = 0;
                for (char type : { 's', 'l', 'f' })
                    for (const auto& name : declarations.get(type))
                        if (object.mLocals.mVariables[localIndex++].first != name)
                            throw std::invalid_argument("Noncanonical equipment local order");
                if (ref.mRefNum == input.mSlots[InventoryStore::Slot_Shirt])
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
            for (const auto slot : input.mSlots)
                if (slot.isSet() && std::none_of(input.mObjects.begin(), input.mObjects.end(),
                        [&](const auto& object) { return object.mRef.mRefNum == slot; }))
                    throw std::invalid_argument("Foreign equipment slot identity");
            if (!shirtFound || !selectedFound)
                throw std::invalid_argument("Foreign equipment shirt or selection identity");
        }
    }

    EquipmentScriptLocals::EquipmentScriptLocals(
        const ESMStore& content, ESM::RefId script, MWBase::ScriptManager& scripts)
        : mRecord(content.get<ESM::Script>().find(script))
        , mId(script)
        , mText(mRecord->mScriptText)
        , mDeclarations(scripts.getLocals(script))
    {
        size_t total = 0;
        for (char type : { 's', 'l', 'f' })
        {
            const auto& names = mDeclarations.get(type);
            if (names.size() > MaxVariables - total)
                throw std::invalid_argument("Equipment script declaration limit exceeded");
            total += names.size();
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i].empty() || names[i].size() > MaxName || names[i].find('\0') != std::string::npos
                    || mDeclarations.getType(names[i]) != type || mDeclarations.getIndex(names[i]) != static_cast<int>(i))
                    throw std::invalid_argument("Invalid equipment script declaration");
        }
        for (const auto name : { "onpcequip", "pcskipequip" })
            if (mDeclarations.getType(name) != 's' && mDeclarations.getType(name) != 'l')
                throw std::invalid_argument("Equipment script requires integer equip/skip locals");
    }

    const Compiler::Locals& EquipmentScriptLocals::declarations(const ESMStore& content, ESM::RefId script) const
    {
        if (script.empty() || script != mId || content.get<ESM::Script>().search(script) != mRecord
            || mRecord->mScriptText != mText)
            throw std::invalid_argument("Equipment script content/declaration binding changed");
        return mDeclarations;
    }

    void EquipmentScriptLocals::validate(
        const MWScript::Locals& locals, const ESMStore& content, ESM::RefId script) const
    {
        const auto& d = declarations(content, script);
        if (locals.getScriptId() != script || locals.mShorts.size() != d.get('s').size()
            || locals.mLongs.size() != d.get('l').size() || locals.mFloats.size() != d.get('f').size()
            || std::any_of(locals.mFloats.begin(), locals.mFloats.end(), [](float v) { return !std::isfinite(v); }))
            throw std::invalid_argument("Equipment script local identity/shape/value changed");
    }

    const Compiler::Locals& equipmentDeclarations(
        const ESMStore& content, ESM::RefId script, const EquipmentScriptLocals* binding)
    {
        static const Compiler::Locals empty;
        if (script.empty())
            return empty;
        if (!binding)
            throw std::invalid_argument("Equipment script requires explicit declarations");
        return binding->declarations(content, script);
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

    void PlainEquipmentValues::validate(const ESMStore& content, ESM::RefNum expectedActor,
        const EquipmentScriptLocals* scripts) const
    {
        validateValues(*this, content, expectedActor, scripts);
    }

    void PlainEquipmentValues::swap(PlainEquipmentValues& other) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<ESM::RefNum>);
        std::swap(mActor, other.mActor);
        mSlots.swap(other.mSlots);
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
            CellRef mRef;
            RefData mData;
        };
        ContainerStoreResolution mResolution;
        PlainEquipmentContext mContext;
        std::weak_ptr<const void> mScriptsLifetime;
        PtrRegistry::Snapshot mRegistry;
        LocalScripts::List mScripts;
        std::vector<Node> mBefore;
        std::array<ESM::RefNum, InventoryStore::Slots> mSlots{};
        ESM::RefNum mSelected;
        InventoryStoreListener* mEquipmentListener;
        ContainerStoreListener* mContainerListener;
        // Never copy an InventoryStore: it would retain live services, owner,
        // RefData aliases and iterators. Construct fresh storage and relocate slots.
        std::unique_ptr<ContainerStore> mStorage;
        ContainerStore& candidate() { return *mStorage; }
        const ContainerStore& candidate() const { return *mStorage; }
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

        static const ContainerStore& storage(
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
            const auto* inventory = typeid(base) == typeid(InventoryStore) ? static_cast<const InventoryStore*>(&base) : nullptr;
            const bool container = typeid(base) == typeid(ContainerStore);
            const auto owner = resolution.mOwner;
            const bool placedActor = owner.getCellRef().getRefNum().hasContentFile() && owner.getClass().isActor()
                && sameReference(owner, context.mActor) && !sameReference(owner, context.mPlayer);
            if (placedActor && (context.mNpcStats || !owner.getClass().getScript(owner).empty()
                || owner.getRefData().getCustomData()
                || (owner.getType() == ESM::NPC::sRecordId
                    ? context.mStore.get<ESM::NPC>().search(owner.getCellRef().getRefId()) != owner.get<ESM::NPC>()->mBase
                    : owner.getType() != ESM::Creature::sRecordId
                        || context.mStore.get<ESM::Creature>().search(owner.getCellRef().getRefId()) != owner.get<ESM::Creature>()->mBase)))
                throw std::invalid_argument("Placed actor inventory requires its original unscripted content and detached context");
            if ((!inventory && !container) || !base.mResolved || !context.mPlayer.getClass().isNpc()
                || (!placedActor && (!context.mActor.getClass().isNpc() || !sameReference(context.mActor, context.mPlayer)))
                || !sameReference(base.getPtr(context.mWorldModel), resolution.mOwner)
                || (inventory && !sameReference(resolution.mOwner, context.mActor))
                || (container && !placedActor && (resolution.mOwner.getType() != ESM::Container::sRecordId
                    || context.mNpcStats || !resolution.mOwner.getClass().getScript(resolution.mOwner).empty()
                    || context.mStore.get<ESM::Container>().search(resolution.mOwner.getCellRef().getRefId())
                        != resolution.mOwner.get<ESM::Container>()->mBase
                    || resolution.mOwner.getRefData().getCustomData())))
                throw std::invalid_argument("Equipment actor/player/resolved inventory mismatch");
            if (!context.mLocalScripts.usesStore(context.mStore))
                throw std::invalid_argument("Equipment script content service mismatch");
            if (base.storedSize() > MaxItems || (inventory && !inventory->mUpdatesEnabled))
                throw std::invalid_argument("Inventory preparation item budget or update mode invalid");
            return base;
        }

        static ESM::RefNum position(const InventoryStore& store, const ContainerStoreIterator& selection)
        {
            if (selection == store.end())
                return {};
            // Compare against current raw members. Never follow a saved iterator,
            // including a dormant, foreign, erased or replaced selection.
            ESM::RefNum result;
            store.forEachStored([&](const auto& node, auto it) {
                if (it == selection) result = node.mRef.getRefNum();
            });
            if (!result.isSet()) throw std::invalid_argument("Equipment iterator is not a current inventory member");
            return result;
        }

        template<class T> static void plain(
            const LiveCellRef<T>& node, const ContainerStore& store, const PlainEquipmentContext& context)
        {
            ConstPtr item(&node, nullptr);
            item.mContainerStore = &store;
            registered(item, context.mWorldModel);
            const auto record = inventoryItemRecord(context.mStore, node.mRef.getRefId());
            if (record.mBase != node.mBase
                || (!context.mNpcStats && !record.mScript.empty())
                || node.mData.getLuaScripts() || node.mData.getCustomData()
                || node.mData.isDeletedByContentFile() || node.mRef.getCount(false) == std::numeric_limits<int>::min()
                || context.mLocalScripts.prepareRemove(&node.mRef).hasRegistration())
                throw std::invalid_argument("Inventory preparation requires bounded items without executing scripts");
            const auto script = node.mBase->mScript;
            if (script.empty())
            {
                if (!node.mData.getLocals().getScriptId().empty() || !node.mData.getLocals().isEmpty())
                    throw std::invalid_argument("Unexpected equipment locals");
            }
            else
            {
                equipmentDeclarations(context.mStore, script, context.mScriptLocals.get());
                context.mScriptLocals->validate(node.mData.getLocals(), context.mStore, script);
                if (std::abs(static_cast<int64_t>(node.mRef.getCount(false))) > 1 || record.mEnchant.empty())
                    throw std::invalid_argument("Scripted equipment requires single constant shirts");
            }
            if (context.mNpcStats && record.mSlots.contains(InventoryStore::Slot_Shirt))
                MWMechanics::constantFortifyLuckMagnitude(context.mStore, record.mEnchant);
        }

        void capture(const ContainerStore& source, bool retireEmpty = false)
        {
            mRegistry = mContext.mWorldModel.snapshotPtrRegistry();
            mScripts = mContext.mLocalScripts.snapshot();
            mContainerListener = source.mListener;
            const auto* inventory = dynamic_cast<const InventoryStore*>(&source);
            if (inventory)
            {
                mStorage = std::make_unique<InventoryStore>();
                mEquipmentListener = inventory->mInventoryListener;
                for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                    mSlots[slot] = position(*inventory, inventory->mSlots[slot]);
                mSelected = position(*inventory, inventory->mSelectedEnchantItem);

            }
            else mStorage = std::make_unique<ContainerStore>();
            auto& mCandidate = candidate();
            int64_t total = 0;
            source.forEachStored([&](const auto& ref, auto) {
                plain(ref, source, mContext);
                total += std::abs(static_cast<int64_t>(ref.mRef.getCount(false)));
                if (total > std::numeric_limits<int>::max())
                    throw std::invalid_argument("Equipment restack count bound exceeded");
                ConstPtr live(&ref, nullptr);
                live.mContainerStore = &source;
                mBefore.push_back({ live, ref.mRef, ref.mData.copyForContainerTransfer() });
                // Keep every live node as a revalidation witness, but retired
                // ordinary stacks need not occupy the next committed image.
                // Never recycle their identities or discard selected/scripted nodes.
                if (retireEmpty && ref.mRef.getCount(false) == 0 && ref.mRef.getRefNum() != mSelected
                    && std::ranges::find(mSlots, ref.mRef.getRefNum()) == mSlots.end()
                    && live.getClass().getScript(live).empty())
                    return;
                auto copy = detached(ref);
                auto it = mCandidate.addNewStack(ConstPtr(&copy), ref.mRef.getCount(false));
                auto& node = *it->getBase();
                // Stock insertion copies RefData and clears transient activation
                // flags. A detached transaction must retain the complete source.
                node.mData = ref.mData.copyForContainerTransfer();
                validateEquipmentItemSlots(inventoryItemRecord(mContext.mStore, node.mRef.getRefId()),
                    node.mRef.getRefNum(), node.mRef.getCount(false), mSlots, bool(mContext.mNpcStats));
                for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                    if (node.mRef.getRefNum() == mSlots[slot])
                        static_cast<InventoryStore&>(mCandidate).mSlots[slot] = it;
                if (node.mRef.getRefNum() == mSelected)
                    static_cast<InventoryStore&>(mCandidate).mSelectedEnchantItem = it;
            });
            mCandidate.mResolved = true;
            if (mContext.mNpcStats)
            {
                mContext.mNpcStats->validate(mContext.mActor, mContext.mStore);
                const auto& stats = mContext.mNpcStats->stats();
                stats.getActiveSpells().validateConstantFortifyLuck(mContext.mActor, *inventory, mContext.mStore, stats);
                mBeforeStats = mContext.mNpcStats->values();
                mNpcStats = std::make_shared<EquipmentNpcStats>(mContext.mActor, mContext.mStore);
                mNpcStats->restore(mBeforeStats, static_cast<InventoryStore&>(mCandidate));
            }
            mResult.mActor = mResolution.mOwner.getCellRef().getRefNum();
            mResult.mLastGenerated = mRegistry.mLastGenerated;
        }

        void check(const PlainEquipmentContext& context) const
        {
            if (mScriptsLifetime.expired() || &context.mStore != &mContext.mStore
                || &context.mWorldModel != &mContext.mWorldModel || &context.mLocalScripts != &mContext.mLocalScripts
                || context.mScriptLocals != mContext.mScriptLocals
                || context.mNpcStats != mContext.mNpcStats
                || (context.mNpcStats && context.mNpcStats->values() != mBeforeStats)
                || !sameReference(context.mActor, mContext.mActor) || !sameReference(context.mPlayer, mContext.mPlayer))
                throw std::invalid_argument("Equipment preparation context/service changed");
            const auto& source = storage(mResolution, context);
            const auto* inventory = dynamic_cast<const InventoryStore*>(&source);
            if (context.mNpcStats)
            {
                context.mNpcStats->validate(context.mActor, context.mStore);
                const auto& stats = context.mNpcStats->stats();
                stats.getActiveSpells().validateConstantFortifyLuck(context.mActor, *inventory, context.mStore, stats);
            }
            if (context.mWorldModel.snapshotPtrRegistry() != mRegistry || context.mLocalScripts.snapshot() != mScripts
                || source.mListener != mContainerListener
                || (inventory && (inventory->mInventoryListener != mEquipmentListener
                    || position(*inventory, inventory->mSelectedEnchantItem) != mSelected))
                || source.storedSize() != mBefore.size())
                throw std::invalid_argument("Equipment preparation source state changed");
            if (inventory)
                for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                    if (position(*inventory, inventory->mSlots[slot]) != mSlots[slot])
                        throw std::invalid_argument("Equipment preparation other slot changed");
            size_t index = 0;
            source.forEachStored([&](const auto& ref, auto) {
                const auto& before = mBefore[index++];
                ConstPtr current(&ref, nullptr);
                current.mContainerStore = &source;
                if (!before.mLive.hasLiveReference() || !sameReference(current, before.mLive))
                    throw std::invalid_argument("Equipment preparation item lifetime/membership changed");
                plain(ref, source, context);
                if (!sameCell(ref.mRef, before.mRef)
                    || !ref.mData.matchesContainerTransferState(before.mData))
                    throw std::invalid_argument("Equipment preparation item values changed");
            });
        }

        void effect(PlainEquipmentResult::EffectKind kind, const Ptr& item = {}, int count = 0)
        {
            mResult.mEffects.push_back(
                { kind, mResult.mActor, item.isEmpty() ? ESM::RefNum() : item.getCellRef().getRefNum(), count });
        }

        void run(ESM::RefNum identity, bool equip, int slot)
        {
            auto& mCandidate = static_cast<InventoryStore&>(candidate());
            auto item = mCandidate.begin();
            while (item != mCandidate.end() && item->getCellRef().getRefNum() != identity)
                ++item;
            if (slot < 0 || slot >= InventoryStore::Slots || item == mCandidate.end()
                || (!equip && mSlots[slot] != identity))
                throw std::invalid_argument("Equipment item is stale, dormant or slot is invalid");
            const auto record = inventoryItemRecord(mContext.mStore, item->getCellRef().getRefId());
            if (!record.mSlots.contains(slot)
                || ((!record.mEnchant.empty() || !record.mScript.empty())
                    && (slot != InventoryStore::Slot_Shirt || !mContext.mNpcStats)))
                throw std::invalid_argument("Equipment type or required effect services unsupported");
            if (equip)
            {
                // These stock checks now accept the explicit owning actor, content and inventory.
                const int allowed = record.mType == ESM::Clothing::sRecordId
                    ? static_cast<const MWClass::Clothing&>(item->getClass()).canBeEquipped(
                        *item, mContext.mActor, mContext.mStore).first
                    : record.mType == ESM::Armor::sRecordId
                        ? static_cast<const MWClass::Armor&>(item->getClass()).canBeEquipped(
                            *item, mContext.mActor, mContext.mStore, mCandidate).first
                        // These bounded actors have no scene or attack simulation. The
                        // stock weapon check uses content/condition without UI in this context.
                        : static_cast<const MWClass::Weapon&>(item->getClass()).canBeEquipped(*item, mContext.mActor).first;
                if (allowed == 0)
                    throw std::invalid_argument("Equipment rejected by actor, condition or occupied hand rules");
            }
            using Kind = PlainEquipmentResult::EffectKind;
            const MWScript::ItemLocalsContext scriptContext{ mContext.mActor, mContext.mPlayer,
                [this](const Ptr& item, ESM::RefId script) -> const Compiler::Locals& {
                    const auto& declarations = equipmentDeclarations(mContext.mStore, script, mContext.mScriptLocals.get());
                    mContext.mScriptLocals->validate(item.getRefData().getLocals(), mContext.mStore, script);
                    return declarations;
                } };
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
                        candidate().flagAsModified();
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
                [&](const Ptr& item, const ESM::RefId&) { MWScript::unequipItemLocals(item, scriptContext); },
                [this](const Ptr&) { effect(Kind::EquipmentChanged); }
            };
            if (equip)
            {
                mResult.mSkipped = !MWScript::beginItemUse(*item, scriptContext);
                if (!mResult.mSkipped)
                {
                    if (std::find(mSlots.begin(), mSlots.end(), identity) != mSlots.end())
                        throw std::invalid_argument("Equipment item already occupies a slot");
                    MWScript::finishItemUse(*item, true, scriptContext);
                    mCandidate.equip(slot, item, context);
                }
            }
            else
                mCandidate.unequipSlot(slot, context);
            if (mNpcStats)
                mNpcStats->mStats.getActiveSpells().updateConstantFortifyLuck(
                    mContext.mActor, mCandidate, mContext.mStore, mNpcStats->mStats);
            if (mNpcStats)
            {
                const auto& luck = mNpcStats->mStats.getAttribute(ESM::Attribute::Luck);
                mResult.mLuck = { luck.getBase(), luck.getModifier(), luck.getDamage() };
            }
            finish();
        }

        void finish()
        {
            mResult.mItems.clear();
            const auto& mCandidate = candidate();
            if (const auto* inventory = dynamic_cast<const InventoryStore*>(&mCandidate))
            {
                for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                    mResult.mSlots[slot] = position(*inventory, inventory->mSlots[slot]);
                mResult.mSelected = position(*inventory, inventory->mSelectedEnchantItem);
            }
            mCandidate.forEachStored([&](const auto& ref, auto) {
                if (ref.mWorldModel || ref.mData.getBaseNode() || ref.mData.getLuaScripts()
                    || ref.mData.getCustomData())
                    throw std::logic_error("Equipment candidate retained a live service or mutable alias");
                mResult.mItems.push_back({ ref.mRef.getRefNum(), ref.mRef.getRefId(), ref.mRef.getCount(false) });
            });
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
        const PlainEquipmentContext& context, int slot)
    {
        const auto& source = State::storage(inventory, context);
        if (typeid(source) != typeid(InventoryStore))
            throw std::invalid_argument("Equipment requires an actor inventory");
        if (context.mWorldModel.getPtrRegistryRevision() != expectedRegistryRevision)
            throw std::invalid_argument("Equipment request registry revision changed");
        registered(item, context.mWorldModel);
        if (item.mContainerStore != &source || item.getCellRef().getRefNum() != expectedIdentity)
            throw std::invalid_argument("Equipment request item owner/identity mismatch");
        bool found = false;
        source.forEachStored([&](const auto& node, auto) { found = found || &node == item.mRef; });
        if (!found)
            throw std::invalid_argument("Equipment request item is foreign");
        auto state = std::make_unique<State>(inventory, context);
        state->capture(source);
        state->run(expectedIdentity, equip, slot);
        state->check(context);
        return PreparedPlainEquipment(std::move(state));
    }

    std::array<PreparedPlainEquipment, 2> PreparedPlainEquipment::prepareTransfer(
        const std::array<ContainerStoreResolution, 2>& inventories, const ConstPtr& item,
        ESM::RefNum expectedIdentity, size_t expectedRevision, int count,
        const std::array<PlainEquipmentContext, 2>& contexts, bool allowEquippedSource, bool takeAll)
    {
        const auto& source = State::storage(inventories[0], contexts[0]);
        const auto& destination = State::storage(inventories[1], contexts[1]);
        const auto& world = contexts[0].mWorldModel;
        registered(item, world);
        if (&source == &destination || sameReference(inventories[0].mOwner, inventories[1].mOwner)
            || &world != &contexts[1].mWorldModel || &contexts[0].mStore != &contexts[1].mStore
            || &contexts[0].mLocalScripts != &contexts[1].mLocalScripts
            || expectedRevision != world.getPtrRegistryRevision()
            || item.getContainerStore() != &source || item.getCellRef().getRefNum() != expectedIdentity
            || !ContainerStore::isStorableType(item.getType()) || count <= 0
            || count > std::abs(static_cast<int64_t>(item.getCellRef().getCount(false)))
            || (!allowEquippedSource && dynamic_cast<const InventoryStore*>(&source)
                && std::any_of(static_cast<const InventoryStore&>(source).mSlots.begin(),
                    static_cast<const InventoryStore&>(source).mSlots.end(), [&](const auto& slot) {
                        return State::position(static_cast<const InventoryStore&>(source), slot) == expectedIdentity;
                    }))
            || !item.getClass().getScript(item).empty())
            throw std::invalid_argument("Transfer requires a current unequipped unscripted item and distinct owners");
        const auto generated = world.getLastGeneratedRefNum();
        if (expectedRevision >= std::numeric_limits<size_t>::max() - 1
            || generated.mContentFile != -1 || generated.mIndex == std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("Inventory transfer registry counter exhausted");
        // Bound the complete request before stock arithmetic or candidate allocation.
        int64_t total = takeAll ? 0 : count;
        size_t transferCount = takeAll ? 0 : 1;
        if (takeAll)
            for (auto it = source.begin(); it != source.end(); ++it)
            {
                if (!it->getClass().getScript(*it).empty()
                    || (!allowEquippedSource && dynamic_cast<const InventoryStore*>(&source)
                        && std::any_of(static_cast<const InventoryStore&>(source).mSlots.begin(),
                            static_cast<const InventoryStore&>(source).mSlots.end(), [&](const auto& slot) {
                                return State::position(static_cast<const InventoryStore&>(source), slot)
                                    == it->getCellRef().getRefNum();
                            })))
                    throw std::invalid_argument("Take All contains an unsupported source item");
                total += std::abs(static_cast<int64_t>(it->getCellRef().getCount(false)));
                ++transferCount;
            }
        if (transferCount == 0 || transferCount > MaxItems
            || expectedRevision > std::numeric_limits<size_t>::max() - transferCount - 1
            || generated.mIndex > std::numeric_limits<uint32_t>::max() - transferCount)
            throw std::invalid_argument("Transfer batch or registry counter bound exceeded");
        destination.forEachStored([&](const auto& node, auto) {
            total += std::abs(static_cast<int64_t>(node.mRef.getCount(false)));
        });
        if (total > std::numeric_limits<int>::max())
            throw std::invalid_argument("Transfer destination count bound exceeded");
        std::array states{ std::make_unique<State>(inventories[0], contexts[0]),
            std::make_unique<State>(inventories[1], contexts[1]) };
        states[0]->capture(source, true);
        states[1]->capture(destination, true);
        auto& from = *states[0];
        auto& to = *states[1];
        auto counter = world.getLastGeneratedRefNum();
        const auto transfer = [&](const ContainerStoreIterator& origin, int quantity) {
            const auto sourceId = origin->getCellRef().getRefNum();
            ManualRef detachedItem(contexts[0].mStore, origin->getCellRef().getRefId());
            auto& incoming = *detachedItem.getPtr().getBase();
            incoming.mRef = origin->getCellRef();
            incoming.mData = origin->getRefData().copyForContainerTransfer();
            incoming.mRef.unsetRefNum();
            incoming.mRef.setCount(quantity);
            // Exactly the stock add selection, signed count arithmetic and equipped
            // stack exclusion. The protected copy owns RefData and no live service.
            auto added = to.candidate().addImp(ConstPtr(&incoming), quantity, contexts[0].mStore);
            if (!added->getCellRef().getRefNum().isSet())
                added->getRefData() = incoming.mData.copyForContainerTransfer();
            const bool newIdentity = !added->getCellRef().getRefNum().isSet();
            const auto destinationId = added->getCellRef().getOrAssignRefNum(counter);
            if (newIdentity && from.mRegistry.mEntries.contains(destinationId))
                throw std::invalid_argument("Transfer generated identity collision");
            normalizeContainerAddReference(added->getCellRef());
            const auto removal = ContainerStore::prepareRemoveCount(origin->getCellRef(), quantity);
            origin->getCellRef() = origin->getCellRef().copyWithCount(removal.mRemainingCount);
            if (auto* inventory = dynamic_cast<InventoryStore*>(&from.candidate()); inventory && allowEquippedSource)
            {
                // Stock removal only clears equipment when the entire stack is gone.
                // Zero-count unequip cannot restack, split or execute item locals.
                const InventoryStoreEquipmentContext context{{contexts[0].mStore, {}, {}, {}},
                    contexts[0].mActor, contexts[0].mPlayer,
                    [](const Ptr&, const ESM::RefId&) { throw std::logic_error("Removed item unexpectedly invoked script locals"); },
                    [&](const Ptr&) { from.effect(PlainEquipmentResult::EffectKind::EquipmentChanged); }};
                inventory->unequipRemovedItem(*origin, context);
            }
            if (auto* inventory = dynamic_cast<InventoryStore*>(&from.candidate());
                inventory && removal.mFullRemoval && inventory->mSelectedEnchantItem == origin)
                inventory->mSelectedEnchantItem = inventory->end();
            if (sourceId == expectedIdentity)
            {
                from.mResult.mTransferred = sourceId;
                to.mResult.mTransferred = destinationId;
            }
            if (takeAll) from.mResult.mBulkTransfers.push_back({sourceId, destinationId, quantity});
            using Kind = PlainEquipmentResult::EffectKind;
            from.effect(Kind::InventoryUpdated);
            if (from.mContainerListener) from.effect(Kind::ItemRemoved, *origin, quantity);
            from.effect(Kind::InventoryUpdated);
            // Stock add registers even an existing destination stack once.
            to.effect(Kind::RegisterSplit, *added);
            to.effect(Kind::InventoryUpdated);
            if (to.mContainerListener) to.effect(Kind::ItemAdded, *added, quantity);
            to.effect(Kind::InventoryUpdated);
        };
        for (auto origin = from.candidate().begin(); origin != from.candidate().end(); ++origin)
        {
            if (!takeAll && origin->getCellRef().getRefNum() != expectedIdentity) continue;
            const int quantity = takeAll ? origin->getCellRef().getCount() : count;
            transfer(origin, quantity);
            if (!takeAll) break;
        }
        if (!from.mResult.mTransferred.isSet())
            throw std::invalid_argument("Transfer source witness is dormant or foreign");
        // Capacity is checked after stock stacking: a full inventory may still
        // accept matching stacks. Failure discards both detached candidates.
        if (to.candidate().storedSize() > MaxItems)
            throw std::invalid_argument("Transfer destination node bound exceeded");
        from.mResult.mLastGenerated = to.mResult.mLastGenerated = counter;
        for (auto& state : states)
        {
            state->finish();
            state->check(state->mContext);
        }
        return { PreparedPlainEquipment(std::move(states[0])), PreparedPlainEquipment(std::move(states[1])) };
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

    ContainerStore& PreparedPlainEquipment::installationCandidate(
        const PlainEquipmentContext& context, const ContainerStore& target)
    {
        validate(context);
        if (mState->mResolution.mStore != &target)
            throw std::invalid_argument("Equipment installation target changed");
        return mState->candidate();
    }

    std::shared_ptr<EquipmentNpcStats>& PreparedPlainEquipment::installationNpcStats()
    {
        return mState->mNpcStats;
    }

    void PreparedPlainEquipment::exportValues(const PlainEquipmentContext& context, PlainEquipmentValues& output) const
    {
        validate(context);
        const auto& result = mState->mResult;
        PlainEquipmentValues staged{ result.mActor, result.mSlots, result.mSelected, result.mLastGenerated, {} };
        if (mState->mNpcStats)
            staged.mNpcStats = mState->mNpcStats->values();
        mState->candidate().forEachStored([&](const auto& node, auto) {
            serializeNode(node, staged, context.mStore, context.mScriptLocals.get());
        });
        validateValues(staged, context.mStore, result.mActor, context.mScriptLocals.get());
        validate(context);
        output.swap(staged);
    }

    struct RestoredPlainEquipment::State
    {
        std::unique_ptr<ContainerStore> mStorage;
        ESM::RefNum mActor, mLastGenerated;
        const ESMStore* mContent = nullptr;
        std::shared_ptr<const EquipmentScriptLocals> mScriptLocals;
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
        const PlainEquipmentValues& input, const ESMStore& content, ESM::RefNum expectedActor,
        std::shared_ptr<const EquipmentScriptLocals> scripts, bool container)
    {
        if (container && (std::any_of(input.mSlots.begin(), input.mSlots.end(), [](auto id) { return id.isSet(); }) || input.mSelected.isSet() || input.mNpcStats))
            throw std::invalid_argument("Container restore cannot contain equipment or stats");
        validateValues(input, content, expectedActor, scripts.get());
        auto staged = std::make_unique<State>();
        staged->mActor = input.mActor;
        staged->mLastGenerated = input.mLastGenerated;
        staged->mContent = &content;
        staged->mScriptLocals = std::move(scripts);
        staged->mNpcStats = input.mNpcStats;
        if (container) staged->mStorage = std::make_unique<ContainerStore>();
        else staged->mStorage = std::make_unique<InventoryStore>();
        auto& inventory = *staged->mStorage;
        for (const auto& object : input.mObjects)
        {
            ManualRef reference(content, object.mRef.mRefID);
            const auto script = reference.getPtr().getClass().getScript(reference.getPtr());
            reference.getPtr().getCellRef() = CellRef(object.mRef);
            reference.getPtr().getRefData() = RefData::restore(object, script,
                equipmentDeclarations(content, script, staged->mScriptLocals.get()));
            const auto it = inventory.addNewStack(reference.getPtr(), object.mRef.mCount);
            auto& node = *it->getBase();
            node.mData = reference.getPtr().getRefData().copyForContainerTransfer();
            // Capture before publishing the detached owner. Later relocation
            // must not lazily mutate retained input on an allocation rejection.
            ConstPtr witness(&node, nullptr);
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (object.mRef.mRefNum == input.mSlots[slot])
                    static_cast<InventoryStore&>(inventory).mSlots[slot] = it;
            if (object.mRef.mRefNum == input.mSelected)
                static_cast<InventoryStore&>(inventory).mSelectedEnchantItem = it;
        }
        inventory.mResolved = true;
        return RestoredPlainEquipment(std::move(staged));
    }

    InventoryStore& RestoredPlainEquipment::installationCandidate(
        const ESMStore& content, ESM::RefNum actor, ESM::RefNum counter) const
    {
        return dynamic_cast<InventoryStore&>(installationStorage(content, actor, counter));
    }

    ContainerStore& RestoredPlainEquipment::installationStorage(
        const ESMStore& content, ESM::RefNum actor, ESM::RefNum counter) const
    {
        if (!mState || mState->mContent != &content || mState->mActor != actor || mState->mLastGenerated != counter)
            throw std::invalid_argument("Restored equipment content, actor or saved counter binding changed");
        mState->mStorage->forEachStored([&](const auto& node, auto) {
            if (inventoryItemRecord(content, node.mRef.getRefId()).mBase != node.mBase)
                throw std::invalid_argument("Restored equipment base record binding changed");
        });
        return *mState->mStorage;
    }

    void RestoredPlainEquipment::exportValues(PlainEquipmentValues& output) const
    {
        if (!mState)
            throw std::invalid_argument("Restored equipment was moved");
        const auto& inventory = *mState->mStorage;
        const auto identity = [&](const ContainerStoreIterator& selection) {
            if (selection == inventory.end()) return ESM::RefNum{};
            // Inspect current raw membership without allocating a Ptr witness.
            ESM::RefNum result;
            inventory.forEachStored([&](const auto& node, auto it) {
                if (it == selection) result = node.mRef.getRefNum();
            });
            if (!result.isSet()) throw std::logic_error("Restored equipment selection lost its member");
            return result;
        };
        PlainEquipmentValues staged{ mState->mActor, {}, {}, mState->mLastGenerated, {} };
        if (const auto* actor = dynamic_cast<const InventoryStore*>(&inventory))
        {
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                staged.mSlots[slot] = identity(actor->mSlots[slot]);
            staged.mSelected = identity(actor->mSelectedEnchantItem);
        }
        staged.mNpcStats = mState->mNpcStats;
        inventory.forEachStored([&](const auto& node, auto) {
            serializeNode(node, staged, *mState->mContent, mState->mScriptLocals.get());
        });
        output.swap(staged);
    }
}
