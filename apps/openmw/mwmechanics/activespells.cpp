#include "activespells.hpp"

#include <optional>

#include <components/debug/debuglog.hpp>

#include <components/misc/resourcehelpers.hpp>

#include <components/misc/strings/algorithm.hpp>

#include <components/esm/generatedrefid.hpp>
#include <components/esm3/actoridconverter.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadstat.hpp>

#include <components/settings/values.hpp>

#include "actorutil.hpp"
#include "creaturestats.hpp"
#include "spellcasting.hpp"
#include "spelleffects.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/worldmodel.hpp"

namespace
{
    bool merge(std::vector<ESM::ActiveEffect>& present, const std::vector<ESM::ActiveEffect>& queued)
    {
        // Can't merge if we already have an effect with the same effect index
        auto problem = std::find_if(queued.begin(), queued.end(), [&](const auto& qEffect) {
            return std::find_if(present.begin(), present.end(), [&](const auto& pEffect) {
                return pEffect.mEffectIndex == qEffect.mEffectIndex;
            }) != present.end();
        });
        if (problem != queued.end())
            return false;
        present.insert(present.end(), queued.begin(), queued.end());
        return true;
    }

    void addEffects(
        std::vector<ESM::ActiveEffect>& effects, const ESM::EffectList& list, bool ignoreResistances = false)
    {
        for (const auto& enam : list.mList)
        {
            if (enam.mData.mRange != ESM::RT_Self)
                continue;
            ESM::ActiveEffect effect;
            effect.mEffectId = enam.mData.mEffectID;
            effect.mArg = MWMechanics::EffectKey(enam.mData).mArg;
            effect.mMagnitude = 0.f;
            effect.mMinMagnitude = static_cast<float>(enam.mData.mMagnMin);
            effect.mMaxMagnitude = static_cast<float>(enam.mData.mMagnMax);
            effect.mEffectIndex = static_cast<int32_t>(enam.mIndex);
            effect.mFlags = ESM::ActiveEffect::Flag_None;
            if (ignoreResistances)
                effect.mFlags |= ESM::ActiveEffect::Flag_Ignore_Resistances;
            effect.mDuration = -1;
            effect.mTimeLeft = -1;
            effects.emplace_back(effect);
        }
    }
}

namespace MWMechanics
{
    struct ActiveSpells::UpdateContext
    {
        bool mUpdatedEnemy = false;
        bool mUpdatedHitOverlay = false;
        bool mUpdateSpellWindow = false;
        bool mPlayNonLooping = false;
        bool mEraseRemoved = false;
        bool mUpdate;

        UpdateContext(bool update)
            : mUpdate(update)
        {
        }
    };

    ActiveSpells::IterationGuard::IterationGuard(ActiveSpells& spells)
        : mActiveSpells(spells)
    {
        mActiveSpells.mIterating = true;
    }

    ActiveSpells::IterationGuard::~IterationGuard()
    {
        mActiveSpells.mIterating = false;
    }

    ActiveSpells::ActiveSpellParams::ActiveSpellParams(
        const MWWorld::Ptr& caster, const ESM::RefId& id, std::string_view sourceName, ESM::RefNum item)
        : mSourceSpellId(id)
        , mDisplayName(sourceName)
        , mItem(item)
        , mFlags()
        , mWorsenings(-1)
    {
        if (!caster.isEmpty() && caster.getClass().isActor())
            mCaster = caster.getCellRef().getRefNum();
    }

    ActiveSpells::ActiveSpellParams::ActiveSpellParams(
        const ESM::Spell* spell, const MWWorld::Ptr& actor, bool ignoreResistances)
        : mSourceSpellId(spell->mId)
        , mDisplayName(spell->mName)
        , mCaster(actor.getCellRef().getRefNum())
        , mFlags()
        , mWorsenings(-1)
    {
        assert(spell->mData.mType != ESM::Spell::ST_Spell && spell->mData.mType != ESM::Spell::ST_Power);
        setFlag(ESM::ActiveSpells::Flag_SpellStore);
        if (spell->mData.mType == ESM::Spell::ST_Ability)
            setFlag(ESM::ActiveSpells::Flag_AffectsBaseValues);
        addEffects(mEffects, spell->mEffects, ignoreResistances);
    }

    ActiveSpells::ActiveSpellParams::ActiveSpellParams(
        const MWWorld::ConstPtr& item, const ESM::Enchantment* enchantment, const MWWorld::Ptr& actor)
        : mSourceSpellId(item.getCellRef().getRefId())
        , mDisplayName(item.getClass().getName(item))
        , mCaster(actor.getCellRef().getRefNum())
        , mItem(item.getCellRef().getRefNum())
        , mFlags()
        , mWorsenings(-1)
    {
        assert(enchantment->mData.mType == ESM::Enchantment::ConstantEffect);
        addEffects(mEffects, enchantment->mEffects);
        setFlag(ESM::ActiveSpells::Flag_Equipment);
    }

    ActiveSpells::ActiveSpellParams::ActiveSpellParams(const ESM::ActiveSpells::ActiveSpellParams& params)
        : mActiveSpellId(params.mActiveSpellId)
        , mSourceSpellId(params.mSourceSpellId)
        , mEffects(params.mEffects)
        , mDisplayName(params.mDisplayName)
        , mCaster(params.mCaster)
        , mItem(params.mItem)
        , mFlags(params.mFlags)
        , mWorsenings(params.mWorsenings)
        , mNextWorsening({ params.mNextWorsening })
    {
    }

    ActiveSpells::ActiveSpellParams::ActiveSpellParams(const ActiveSpellParams& params, const MWWorld::Ptr& actor)
        : mSourceSpellId(params.mSourceSpellId)
        , mDisplayName(params.mDisplayName)
        , mCaster(actor.getCellRef().getRefNum())
        , mItem(params.mItem)
        , mFlags(params.mFlags)
        , mWorsenings(-1)
    {
    }

    ESM::ActiveSpells::ActiveSpellParams ActiveSpells::ActiveSpellParams::toEsm() const
    {
        ESM::ActiveSpells::ActiveSpellParams params;
        params.mActiveSpellId = mActiveSpellId;
        params.mSourceSpellId = mSourceSpellId;
        params.mEffects = mEffects;
        params.mDisplayName = mDisplayName;
        params.mCaster = mCaster;
        params.mItem = mItem;
        params.mFlags = mFlags;
        params.mWorsenings = mWorsenings;
        params.mNextWorsening = mNextWorsening.toEsm();
        return params;
    }

    void ActiveSpells::ActiveSpellParams::setFlag(ESM::ActiveSpells::Flags flag)
    {
        mFlags = static_cast<ESM::ActiveSpells::Flags>(mFlags | flag);
    }

    void ActiveSpells::ActiveSpellParams::worsen()
    {
        ++mWorsenings;
        if (!mWorsenings)
            mNextWorsening = MWBase::Environment::get().getWorld()->getTimeStamp();
        mNextWorsening += CorprusStats::sWorseningPeriod;
    }

    bool ActiveSpells::ActiveSpellParams::shouldWorsen() const
    {
        return mWorsenings >= 0 && MWBase::Environment::get().getWorld()->getTimeStamp() >= mNextWorsening;
    }

    void ActiveSpells::ActiveSpellParams::resetWorsenings()
    {
        mWorsenings = -1;
    }

    ESM::RefId ActiveSpells::ActiveSpellParams::getEnchantment() const
    {
        // Enchantment id is not stored directly. Instead the enchanted item is stored.
        const auto& store = MWBase::Environment::get().getESMStore();
        switch (store->find(mSourceSpellId))
        {
            case ESM::REC_ARMO:
                return store->get<ESM::Armor>().find(mSourceSpellId)->mEnchant;
            case ESM::REC_BOOK:
                return store->get<ESM::Book>().find(mSourceSpellId)->mEnchant;
            case ESM::REC_CLOT:
                return store->get<ESM::Clothing>().find(mSourceSpellId)->mEnchant;
            case ESM::REC_WEAP:
                return store->get<ESM::Weapon>().find(mSourceSpellId)->mEnchant;
            default:
                return {};
        }
    }

    const ESM::Spell* ActiveSpells::ActiveSpellParams::getSpell() const
    {
        return MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(getSourceSpellId());
    }

    bool ActiveSpells::ActiveSpellParams::hasFlag(ESM::ActiveSpells::Flags flags) const
    {
        return static_cast<ESM::ActiveSpells::Flags>(mFlags & flags) == flags;
    }

    namespace
    {
        float fixedFortifyLuckMagnitude(const MWWorld::ESMStore& store, const ESM::EffectList& effects)
        {
            if (effects.mList.size() != 1)
                throw std::invalid_argument("Expected one fixed Fortify Luck effect");
            const auto& effect = effects.mList.front().mData;
            const auto* magic = store.get<ESM::MagicEffect>().search(effect.mEffectID);
            if (effect.mEffectID != ESM::MagicEffect::FortifyAttribute || effect.mAttribute != ESM::Attribute::Luck
                || !effect.mSkill.empty() || effect.mRange != ESM::RT_Self || effect.mArea != 0
                || effect.mMagnMin <= 0 || effect.mMagnMin > 1000 || effect.mMagnMin != effect.mMagnMax
                || !magic || !(magic->mData.mFlags & ESM::MagicEffect::AppliedOnce)
                || (magic->mData.mFlags & (ESM::MagicEffect::Harmful | ESM::MagicEffect::NoMagnitude
                    | ESM::MagicEffect::CasterLinked | ESM::MagicEffect::NonRecastable))
                || !ESM::MagicEffect::getResistanceEffect(effect.mEffectID).empty())
                throw std::invalid_argument("Unsupported constant Fortify Luck equipment effect");
            return static_cast<float>(effect.mMagnMin);
        }
    }

    float constantFortifyLuckMagnitude(const MWWorld::ESMStore& store, ESM::RefId id)
    {
        if (id.empty())
            return 0;
        const auto* enchantment = store.get<ESM::Enchantment>().search(id);
        if (!enchantment || enchantment->mData.mType != ESM::Enchantment::ConstantEffect)
            throw std::invalid_argument("Equipment requires a constant enchantment");
        return fixedFortifyLuckMagnitude(store, enchantment->mEffects);
    }

    float abilityFortifyLuckMagnitude(const MWWorld::ESMStore& store, ESM::RefId id)
    {
        const auto* spell = store.get<ESM::Spell>().search(id);
        if (!spell || spell->mData.mType != ESM::Spell::ST_Ability)
            throw std::invalid_argument("Expected a passive Fortify Luck ability");
        return fixedFortifyLuckMagnitude(store, spell->mEffects);
    }

    void ActiveSpells::visitNewSpells(const MWWorld::Ptr& actor, const CreatureStats& stats,
        const std::function<void(const ActiveSpellParams&)>& add)
    {
        if (stats.isDead())
            return;
        for (const ESM::Spell* spell : stats.getSpells())
            if (spell->mData.mType != ESM::Spell::ST_Spell && spell->mData.mType != ESM::Spell::ST_Power
                && !isSpellActive(spell->mId))
                add(ActiveSpellParams{ spell, actor, true });
    }

    void ActiveSpells::applyFixedFortifyLuck(const ActiveSpellParams& params, CreatureStats& stats)
    {
        // Actor-scoped source IDs suffice for this bounded ability/item pair.
        auto& spell = *initParams(params, params.mSourceSpellId);
        auto& effect = spell.mEffects.front();
        effect.mMagnitude = effect.mMinMagnitude; // Fixed stock roll consumes no RNG.
        modifyFortifyAttribute(stats, effect.getSkillOrAttribute(), effect.mMagnitude,
            spell.hasFlag(ESM::ActiveSpells::Flag_AffectsBaseValues));
        stats.getMagicEffects().add(EffectKey(effect.mEffectId, effect.getSkillOrAttribute()), EffectParam(effect.mMagnitude));
        effect.mFlags |= ESM::ActiveEffect::Flag_Applied;
    }

    void ActiveSpells::validateFortifyLuckState(const MWWorld::Ptr& actor,
        const MWWorld::ESMStore& content, const CreatureStats& stats, bool allowInactiveAbility) const
    {
        if (&stats.getActiveSpells() != this || !actor.hasLiveReference()
            || !actor.getCellRef().getRefNum().isSet() || stats.isDead()
            || mIterating || !mQueue.empty() || !mPurges.empty())
            throw std::invalid_argument("Invalid bounded actor/effect state");
        const ESM::Spell* ability = nullptr;
        for (const auto* spell : stats.getSpells())
        {
            if (content.get<ESM::Spell>().search(spell->mId) != spell)
                throw std::invalid_argument("Passive spell content binding changed");
            if (spell->mData.mType == ESM::Spell::ST_Spell || spell->mData.mType == ESM::Spell::ST_Power)
                continue;
            if (ability)
                throw std::invalid_argument("Only one passive Fortify Luck ability is supported");
            abilityFortifyLuckMagnitude(content, spell->mId);
            ability = spell;
        }
        bool hasAbility = false, hasItem = false;
        float abilityMagnitude = 0, itemMagnitude = 0;
        const EffectKey key(ESM::MagicEffect::FortifyAttribute, ESM::Attribute::Luck);
        for (const auto& spell : mSpells)
        {
            const bool passive = spell.mFlags == ESM::Compatibility::ActiveSpells::Type_Ability_Flags;
            float magnitude;
            int32_t index;
            if (passive)
            {
                if (!ability || hasAbility || spell.mSourceSpellId != ability->mId || spell.mItem.isSet())
                    throw std::invalid_argument("Passive ability ownership changed");
                hasAbility = true;
                magnitude = abilityMagnitude = abilityFortifyLuckMagnitude(content, ability->mId);
                index = static_cast<int32_t>(ability->mEffects.mList.front().mIndex);
            }
            else
            {
                const auto* item = content.get<ESM::Clothing>().search(spell.mSourceSpellId);
                if (hasItem || spell.mFlags != ESM::ActiveSpells::Flag_Equipment || !spell.mItem.isSet()
                    || !item || item->mData.mType != ESM::Clothing::Shirt || !item->mScript.empty())
                    throw std::invalid_argument("Constant equipment effect ownership changed");
                hasItem = true;
                magnitude = itemMagnitude = constantFortifyLuckMagnitude(content, item->mEnchant);
                if (!magnitude)
                    throw std::invalid_argument("Active equipment effect has no enchantment");
                index = static_cast<int32_t>(content.get<ESM::Enchantment>().find(item->mEnchant)->mEffects.mList.front().mIndex);
            }
            if (spell.mCaster != actor.getCellRef().getRefNum() || spell.mActiveSpellId != spell.mSourceSpellId
                || spell.mEffects.size() != 1 || !spell.mSource.isEmpty() || spell.mWorsenings != -1)
                throw std::invalid_argument("Canonical effect identity changed");
            const auto& effect = spell.mEffects.front();
            const int flags = ESM::ActiveEffect::Flag_Applied
                | (passive ? ESM::ActiveEffect::Flag_Ignore_Resistances : 0);
            if (effect.mEffectId != key.mId || effect.getSkillOrAttribute() != key.mArg
                || effect.mMagnitude != magnitude || effect.mMinMagnitude != magnitude || effect.mMaxMagnitude != magnitude
                || effect.mFlags != flags || effect.mDuration != -1 || effect.mTimeLeft != -1 || effect.mEffectIndex != index)
                throw std::invalid_argument("Canonical applied effect changed");
        }
        const float total = abilityMagnitude + itemMagnitude;
        if ((!allowInactiveAbility && ability && !hasAbility)
            || stats.getAttribute(ESM::Attribute::Luck).getModifier() != itemMagnitude
            || stats.getMagicEffects().getOrDefault(key).getMagnitude() != total)
            throw std::invalid_argument("Canonical Luck/effect state changed");
        for (const auto& [effect, value] : stats.getMagicEffects())
            if (!(effect == key) || value.getBase() != 0 || value.getModifier() != total)
                throw std::invalid_argument("Unsupported magic effects");
    }

    void ActiveSpells::activateFortifyLuckAbility(const MWWorld::Ptr& actor,
        const MWWorld::ESMStore& content, CreatureStats& stats)
    {
        validateFortifyLuckState(actor, content, stats, true);
        visitNewSpells(actor, stats, [&](const ActiveSpellParams& params) { applyFixedFortifyLuck(params, stats); });
    }

    bool ActiveSpells::stillEquipped(const ActiveSpellParams& spell, const MWWorld::InventoryStore& inventory)
    {
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
        {
            const auto item = inventory.getSlot(slot);
            if (item != inventory.end() && item->getCellRef().getRefNum().isSet()
                && item->getCellRef().getRefNum() == spell.mItem)
                return true;
        }
        return false;
    }

    void ActiveSpells::visitNewEquipment(const MWWorld::Ptr& actor, const MWWorld::InventoryStore& inventory,
        const MWWorld::ESMStore& content, const std::function<void(const ActiveSpellParams&)>& add)
    {
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
        {
            const auto item = inventory.getSlot(slot);
            if (item == inventory.end())
                continue;
            const auto id = item->getClass().getEnchantment(*item);
            const auto* enchantment = id.empty() ? nullptr : content.get<ESM::Enchantment>().search(id);
            if (!enchantment || enchantment->mData.mType != ESM::Enchantment::ConstantEffect)
                continue;
            if (std::find_if(mSpells.begin(), mSpells.end(), [&](const ActiveSpellParams& spell) {
                    return spell.mItem == item->getCellRef().getRefNum()
                        && spell.hasFlag(ESM::ActiveSpells::Flag_Equipment)
                        && spell.mSourceSpellId == item->getCellRef().getRefId();
                }) == mSpells.end())
                add(ActiveSpellParams{ *item, enchantment, actor });
        }
    }

    void ActiveSpells::validateConstantFortifyLuck(const MWWorld::Ptr& actor,
        const MWWorld::InventoryStore& inventory, const MWWorld::ESMStore& content, const CreatureStats& stats) const
    {
        validateFortifyLuckState(actor, content, stats, false);
        const auto item = inventory.getSlot(MWWorld::InventoryStore::Slot_Shirt);
        const float magnitude = item == inventory.end() ? 0
            : constantFortifyLuckMagnitude(content, item->getClass().getEnchantment(*item));
        if (stats.getAttribute(ESM::Attribute::Luck).getModifier() != magnitude)
            throw std::invalid_argument("Equipment canonical Luck disagrees with shirt");
        for (const auto& spell : mSpells)
            if (spell.hasFlag(ESM::ActiveSpells::Flag_Equipment)
                && (item == inventory.end() || spell.mItem != item->getCellRef().getRefNum()
                    || spell.mSourceSpellId != item->getCellRef().getRefId()))
                throw std::invalid_argument("Equipment canonical item ownership changed");
    }

    void ActiveSpells::updateConstantFortifyLuck(const MWWorld::Ptr& actor,
        const MWWorld::InventoryStore& inventory, const MWWorld::ESMStore& content, CreatureStats& stats)
    {
        validateFortifyLuckState(actor, content, stats, false);
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
        {
            const auto item = inventory.getSlot(slot);
            if (item == inventory.end())
                continue;
            if (slot != MWWorld::InventoryStore::Slot_Shirt || item->getType() != ESM::Clothing::sRecordId
                || !item->getClass().getScript(*item).empty())
                throw std::invalid_argument("Constant effect context requires a non-scripted shirt");
            constantFortifyLuckMagnitude(content, item->getClass().getEnchantment(*item));
        }
        const EffectKey key(ESM::MagicEffect::FortifyAttribute, ESM::Attribute::Luck);
        // Removal uses the saved applied magnitude, never a freshly rolled value.
        for (auto it = mSpells.begin(); it != mSpells.end();)
        {
            if (!it->hasFlag(ESM::ActiveSpells::Flag_Equipment) || stillEquipped(*it, inventory))
                ++it;
            else
            {
                const auto magnitude = it->mEffects.front().mMagnitude;
                stats.getMagicEffects().add(key, EffectParam(-magnitude));
                modifyFortifyAttribute(stats, ESM::Attribute::Luck, -magnitude);
                it = mSpells.erase(it);
            }
        }
        visitNewEquipment(actor, inventory, content,
            [&](const ActiveSpellParams& params) { applyFixedFortifyLuck(params, stats); });
    }

    void ActiveSpells::update(const MWWorld::Ptr& ptr, float duration)
    {
        if (mIterating)
            return;
        auto& creatureStats = ptr.getClass().getCreatureStats(ptr);
        assert(&creatureStats.getActiveSpells() == this);
        IterationGuard guard{ *this };
        // Erase no longer active spells and effects
        for (auto spellIt = mSpells.begin(); spellIt != mSpells.end();)
        {
            if (spellIt->hasFlag(ESM::ActiveSpells::Flag_SpellStore))
            {
                const ESM::Spell* spell
                    = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spellIt->mSourceSpellId);
                if (spell && ptr.getClass().getCreatureStats(ptr).getSpells().hasSpell(spell))
                    ++spellIt;
                else
                {
                    if (spell == nullptr)
                        Log(Debug::Error) << "Dropping non-existent active effect: " << spellIt->mSourceSpellId;
                    auto params = *spellIt;
                    spellIt = mSpells.erase(spellIt);
                    for (const auto& effect : params.mEffects)
                        onMagicEffectRemoved(ptr, params, effect);
                    applyPurges(ptr, &spellIt);
                }
                continue;
            }
            else if (!spellIt->hasFlag(ESM::ActiveSpells::Flag_Temporary))
            {
                ++spellIt;
                continue;
            }
            bool removedSpell = false;
            for (auto effectIt = spellIt->mEffects.begin(); effectIt != spellIt->mEffects.end();)
            {
                if (effectIt->mFlags & ESM::ActiveEffect::Flag_Remove && effectIt->mTimeLeft <= 0.f)
                {
                    auto effect = *effectIt;
                    effectIt = spellIt->mEffects.erase(effectIt);
                    onMagicEffectRemoved(ptr, *spellIt, effect);
                    removedSpell = applyPurges(ptr, &spellIt, &effectIt);
                    if (removedSpell)
                        break;
                }
                else
                {
                    ++effectIt;
                }
            }
            if (removedSpell)
                continue;
            if (spellIt->mEffects.empty())
                spellIt = mSpells.erase(spellIt);
            else
                ++spellIt;
        }

        UpdateContext context(duration > 0.f);
        for (const auto& spell : mQueue)
            addToSpells(ptr, spell, context);
        mQueue.clear();

        // Vanilla only does this on cell change I think.
        visitNewSpells(ptr, creatureStats,
            [&](const ActiveSpellParams& params) { initParams(ptr, params, context); });

        if (ptr.getClass().hasInventoryStore(ptr)
            && !(creatureStats.isDead() && creatureStats.isDeathAnimationFinished()))
        {
            auto& store = ptr.getClass().getInventoryStore(ptr);
            if (store.getInvListener() != nullptr)
            {
                context.mPlayNonLooping = !store.isFirstEquip();
                visitNewEquipment(ptr, store, *MWBase::Environment::get().getESMStore(),
                    [&](const ActiveSpellParams& params) {
                        // breakInvisibility calls update recursively; purge directly.
                        purgeEffect(ptr, ESM::MagicEffect::Invisibility);
                        applyPurges(ptr);
                        if (initParams(ptr, params, context))
                            context.mUpdateSpellWindow = true;
                    });
            }
        }

        const MWWorld::Ptr player = MWMechanics::getPlayer();
        // Update effects
        context.mEraseRemoved = true;
        for (auto spellIt = mSpells.begin(); spellIt != mSpells.end();)
        {
            updateActiveSpell(ptr, duration, spellIt, context);
        }

        if (Settings::game().mClassicCalmSpellsBehavior)
        {
            ESM::RefId effect
                = ptr.getClass().isNpc() ? ESM::MagicEffect::CalmHumanoid : ESM::MagicEffect::CalmCreature;
            if (creatureStats.getMagicEffects().getOrDefault(effect).getMagnitude() > 0.f)
                creatureStats.getAiSequence().stopCombat();
        }

        if (ptr == player && context.mUpdateSpellWindow)
        {
            // Something happened with the spell list -- possibly while the game is paused,
            // so we want to make the spell window get the memo.
            // We don't normally want to do this, so this targets constant enchantments.
            MWBase::Environment::get().getWindowManager()->updateSpellWindow();
        }
    }

    bool ActiveSpells::updateActiveSpell(
        const MWWorld::Ptr& ptr, float duration, Collection::iterator& spellIt, UpdateContext& context)
    {
        const auto caster = MWBase::Environment::get().getWorldModel()->getPtr(spellIt->mCaster);
        bool removedSpell = false;
        std::optional<ActiveSpellParams> reflected;
        for (auto it = spellIt->mEffects.begin(); it != spellIt->mEffects.end();)
        {
            if (it->mFlags & ESM::ActiveEffect::Flag_Remove && it->mTimeLeft <= 0.f
                && spellIt->hasFlag(ESM::ActiveSpells::Flag_Temporary))
            {
                ++it;
                continue;
            }
            auto result = applyMagicEffect(ptr, caster, *spellIt, *it, duration, context.mPlayNonLooping);
            if (result.mType == MagicApplicationResult::Type::REFLECTED)
            {
                if (!reflected)
                {
                    if (Settings::game().mClassicReflectedAbsorbSpellsBehavior)
                        reflected = { *spellIt, caster };
                    else
                        reflected = { *spellIt, ptr };
                }
                auto& reflectedEffect = reflected->mEffects.emplace_back(*it);
                reflectedEffect.mFlags
                    = ESM::ActiveEffect::Flag_Ignore_Reflect | ESM::ActiveEffect::Flag_Ignore_SpellAbsorption;
                it = spellIt->mEffects.erase(it);
            }
            else if (result.mType == MagicApplicationResult::Type::REMOVED)
                it = spellIt->mEffects.erase(it);
            else
            {
                const MWWorld::Ptr player = MWMechanics::getPlayer();
                ++it;
                if (!context.mUpdatedEnemy && result.mShowHealth && caster == player && ptr != player)
                {
                    MWBase::Environment::get().getWindowManager()->setEnemy(ptr);
                    context.mUpdatedEnemy = true;
                }
                if (!context.mUpdatedHitOverlay && result.mShowHit && ptr == player)
                {
                    MWBase::Environment::get().getWindowManager()->activateHitOverlay(false);
                    context.mUpdatedHitOverlay = true;
                }
            }
            removedSpell = applyPurges(ptr, &spellIt, &it);
            if (removedSpell)
                break;
        }
        if (reflected)
        {
            const ESM::Static* reflectStatic = MWBase::Environment::get().getESMStore()->get<ESM::Static>().find(
                ESM::RefId::stringRefId("VFX_Reflect"));
            MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
            if (animation && !reflectStatic->mModel.empty())
            {
                const VFS::Path::Normalized reflectStaticModel
                    = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(reflectStatic->mModel));
                animation->addEffect(reflectStaticModel, ESM::MagicEffect::Reflect.getValue(), false);
            }
            caster.getClass().getCreatureStats(caster).getActiveSpells().addSpell(*reflected);
        }
        if (removedSpell)
            return true;

        if (context.mEraseRemoved)
        {
            bool remove = false;
            if (spellIt->hasFlag(ESM::ActiveSpells::Flag_Equipment))
            {
                // Remove effects tied to equipment that has been unequipped
                const auto& store = ptr.getClass().getInventoryStore(ptr);
                remove = !stillEquipped(*spellIt, store);
            }
            if (remove)
            {
                auto params = *spellIt;
                spellIt = mSpells.erase(spellIt);
                for (const auto& effect : params.mEffects)
                    onMagicEffectRemoved(ptr, params, effect);
                applyPurges(ptr, &spellIt);
                context.mUpdateSpellWindow = true;
                return true;
            }
        }
        ++spellIt;
        return false;
    }

    ActiveSpells::Collection::iterator ActiveSpells::initParams(const ActiveSpellParams& params, ESM::RefId activeId)
    {
        auto it = mSpells.emplace(mSpells.end(), params);
        it->setActiveSpellId(activeId);
        return it;
    }

    bool ActiveSpells::initParams(const MWWorld::Ptr& ptr, const ActiveSpellParams& params, UpdateContext& context)
    {
        auto it = initParams(params, {});
        it->setActiveSpellId(MWBase::Environment::get().getESMStore()->generateId());
        // We instantly apply the effect with a duration of 0 so continuous effects can be purged before truly applying
        if (context.mUpdate && updateActiveSpell(ptr, 0.f, it, context))
            return false;
        return true;
    }

    void ActiveSpells::addToSpells(const MWWorld::Ptr& ptr, const ActiveSpellParams& spell, UpdateContext& context)
    {
        if (!spell.hasFlag(ESM::ActiveSpells::Flag_Stackable))
        {
            auto found = std::find_if(mSpells.begin(), mSpells.end(), [&](const auto& existing) {
                return spell.mSourceSpellId == existing.mSourceSpellId && spell.mCaster == existing.mCaster
                    && spell.mItem == existing.mItem;
            });
            if (found != mSpells.end())
            {
                if (!spell.hasFlag(ESM::ActiveSpells::Flag_Temporary))
                    return;
                if (merge(found->mEffects, spell.mEffects))
                    return;
                for (auto& effect : found->mEffects)
                    effect.mTimeLeft = 0.f;
            }
        }
        initParams(ptr, spell, context);
    }

    ActiveSpells::ActiveSpells()
        : mIterating(false)
    {
    }

    ActiveSpells::TIterator ActiveSpells::begin() const
    {
        return mSpells.begin();
    }

    ActiveSpells::TIterator ActiveSpells::end() const
    {
        return mSpells.end();
    }

    ActiveSpells::TIterator ActiveSpells::getActiveSpellById(const ESM::RefId& id)
    {
        for (TIterator it = begin(); it != end(); it++)
            if (it->getActiveSpellId() == id)
                return it;
        return end();
    }

    bool ActiveSpells::isSpellActive(const ESM::RefId& id) const
    {
        return std::find_if(mSpells.begin(), mSpells.end(), [&](const auto& spell) {
            return spell.mSourceSpellId == id;
        }) != mSpells.end();
    }

    bool ActiveSpells::isEnchantmentActive(const ESM::RefId& id) const
    {
        const auto& store = MWBase::Environment::get().getESMStore();
        if (store->get<ESM::Enchantment>().search(id) == nullptr)
            return false;

        return std::find_if(mSpells.begin(), mSpells.end(), [&](const auto& spell) {
            return spell.getEnchantment() == id;
        }) != mSpells.end();
    }

    void ActiveSpells::addSpell(const ActiveSpellParams& params)
    {
        mQueue.emplace_back(params);
    }

    void ActiveSpells::addSpell(const ESM::Spell* spell, const MWWorld::Ptr& actor, bool ignoreResistances)
    {
        mQueue.emplace_back(ActiveSpellParams{ spell, actor, ignoreResistances });
    }

    void ActiveSpells::purge(ParamsPredicate predicate, const MWWorld::Ptr& ptr)
    {
        assert(&ptr.getClass().getCreatureStats(ptr).getActiveSpells() == this);
        mPurges.emplace(predicate);
        if (!mIterating)
        {
            IterationGuard guard{ *this };
            applyPurges(ptr);
        }
    }

    void ActiveSpells::purge(EffectPredicate predicate, const MWWorld::Ptr& ptr)
    {
        assert(&ptr.getClass().getCreatureStats(ptr).getActiveSpells() == this);
        mPurges.emplace(predicate);
        if (!mIterating)
        {
            IterationGuard guard{ *this };
            applyPurges(ptr);
        }
    }

    bool ActiveSpells::applyPurges(const MWWorld::Ptr& ptr, std::list<ActiveSpellParams>::iterator* currentSpell,
        std::vector<ActiveEffect>::iterator* currentEffect)
    {
        bool removedCurrentSpell = false;
        while (!mPurges.empty())
        {
            auto predicate = mPurges.front();
            mPurges.pop();
            for (auto spellIt = mSpells.begin(); spellIt != mSpells.end();)
            {
                bool isCurrentSpell = currentSpell && *currentSpell == spellIt;
                std::visit(
                    [&](auto&& variant) {
                        using T = std::decay_t<decltype(variant)>;
                        if constexpr (std::is_same_v<T, ParamsPredicate>)
                        {
                            if (variant(*spellIt))
                            {
                                auto params = *spellIt;
                                spellIt = mSpells.erase(spellIt);
                                if (isCurrentSpell)
                                {
                                    *currentSpell = spellIt;
                                    removedCurrentSpell = true;
                                }
                                for (const auto& effect : params.mEffects)
                                    onMagicEffectRemoved(ptr, params, effect);
                            }
                            else
                                ++spellIt;
                        }
                        else
                        {
                            static_assert(std::is_same_v<T, EffectPredicate>, "Non-exhaustive visitor");
                            for (auto effectIt = spellIt->mEffects.begin(); effectIt != spellIt->mEffects.end();)
                            {
                                if (variant(*spellIt, *effectIt))
                                {
                                    auto effect = *effectIt;
                                    if (isCurrentSpell && currentEffect)
                                    {
                                        auto distance = std::distance(spellIt->mEffects.begin(), *currentEffect);
                                        if (effectIt <= *currentEffect)
                                            distance--;
                                        effectIt = spellIt->mEffects.erase(effectIt);
                                        *currentEffect = spellIt->mEffects.begin() + distance;
                                    }
                                    else
                                        effectIt = spellIt->mEffects.erase(effectIt);
                                    onMagicEffectRemoved(ptr, *spellIt, effect);
                                }
                                else
                                    ++effectIt;
                            }
                            ++spellIt;
                        }
                    },
                    predicate);
            }
        }
        return removedCurrentSpell;
    }

    void ActiveSpells::removeEffectsBySourceSpellId(const MWWorld::Ptr& ptr, const ESM::RefId& id)
    {
        purge([=](const ActiveSpellParams& params) { return params.mSourceSpellId == id; }, ptr);
    }

    void ActiveSpells::removeEffectsByActiveSpellId(const MWWorld::Ptr& ptr, const ESM::RefId& id)
    {
        purge([=](const ActiveSpellParams& params) { return params.mActiveSpellId == id; }, ptr);
    }

    void ActiveSpells::purgeEffect(const MWWorld::Ptr& ptr, ESM::RefId effectId, ESM::RefId effectArg)
    {
        purge(
            [=](const ActiveSpellParams&, const ESM::ActiveEffect& effect) {
                if (!(effect.mFlags & ESM::ActiveEffect::Flag_Applied))
                    return false;
                if (effectArg.empty())
                    return effect.mEffectId == effectId;
                return effect.mEffectId == effectId && effect.getSkillOrAttribute() == effectArg;
            },
            ptr);
    }

    void ActiveSpells::purge(const MWWorld::Ptr& ptr, ESM::RefNum actor)
    {
        purge([=](const ActiveSpellParams& params) { return params.mCaster == actor; }, ptr);
    }

    void ActiveSpells::clear(const MWWorld::Ptr& ptr)
    {
        mQueue.clear();
        purge([](const ActiveSpellParams& params) { return true; }, ptr);
    }

    void ActiveSpells::skipWorsenings(double hours)
    {
        for (auto& spell : mSpells)
        {
            if (spell.mWorsenings >= 0)
                spell.mNextWorsening += hours;
        }
    }

    void ActiveSpells::writeState(ESM::ActiveSpells& state) const
    {
        for (const auto& spell : mSpells)
            state.mSpells.emplace_back(spell.toEsm());
        for (const auto& spell : mQueue)
            state.mQueue.emplace_back(spell.toEsm());
    }

    void ActiveSpells::readState(const ESM::ActiveSpells& state)
    {
        for (const ESM::ActiveSpells::ActiveSpellParams& spell : state.mSpells)
        {
            mSpells.emplace_back(ActiveSpellParams{ spell });
            // Generate ID for older saves that didn't have any.
            if (mSpells.back().getActiveSpellId().empty())
                mSpells.back().setActiveSpellId(MWBase::Environment::get().getESMStore()->generateId());
        }
        for (const ESM::ActiveSpells::ActiveSpellParams& spell : state.mQueue)
            mQueue.emplace_back(ActiveSpellParams{ spell });
        if (state.mActorIdConverter)
        {
            const auto convertSummons = [converter = state.mActorIdConverter](auto& collection) {
                for (ActiveSpellParams& params : collection)
                {
                    converter->convert(params.mCaster, params.mCaster.mIndex);
                    for (ESM::ActiveEffect& effect : params.mEffects)
                    {
                        if (ESM::RefNum* refNum = std::get_if<ESM::RefNum>(&effect.mArg))
                            converter->convert(*refNum, refNum->mIndex);
                    }
                }
            };
            convertSummons(mSpells);
            convertSummons(mQueue);
        }
    }

    void ActiveSpells::unloadActor(const MWWorld::Ptr& ptr)
    {
        purge([](const auto& spell) { return spell.hasFlag(ESM::ActiveSpells::Flag_Temporary); }, ptr);
        mQueue.clear();
    }
}
