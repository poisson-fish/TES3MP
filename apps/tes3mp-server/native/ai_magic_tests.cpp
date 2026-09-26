#include "ai_magic.hpp"
#include "loadout.hpp"
#include <apps/openmw/mwmechanics/weaponpriority.hpp>
#include <components/esm3/loadweap.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/spellpriority.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace TES3MP::Native;
    void require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }
    void near(float value, float expected, const char* message)
    { require(std::isfinite(value) && std::abs(value - expected) < .0001f, message); }
    ESM::RefId id(std::string_view name) { return ESM::RefId::stringRefId(name); }
    void setting(MWWorld::ESMStore& store, const char* name, float value)
    {
        ESM::GameSetting record;
        record.blank(); record.mId = id(name); record.mValue.setType(ESM::VT_Float);
        record.mValue.setFloat(value);
        if (store.get<ESM::GameSetting>().search(record.mId)) store.overrideRecord(record);
        else store.insertStatic(record);
    }
    void content(MWWorld::ESMStore& store)
    {
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            ESM::Attribute attribute;
            attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
            store.insertStatic(attribute);
        }
        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            ESM::Skill skill;
            skill.blank(); skill.mId = *ESM::Skill::indexToRefId(i).getIf<ESM::StringRefId>();
            store.insertStatic(skill);
        }
        setting(store, "fFatigueBase", 1.25f); setting(store, "fFatigueMult", .5f);
        setting(store, "fEffectCostMult", 1.f);
        setting(store, "fAIMagicSpellMult", 2.f); setting(store, "fAIRangeMagicSpellMult", 3.f);
        for (const auto effect : {ESM::MagicEffect::DamageHealth, ESM::MagicEffect::RestoreHealth,
                ESM::MagicEffect::RestoreMagicka, ESM::MagicEffect::RestoreFatigue,
                ESM::MagicEffect::FireDamage, ESM::MagicEffect::FrostDamage,
                ESM::MagicEffect::ResistMagicka, ESM::MagicEffect::WeaknessToFire})
        {
            ESM::MagicEffect record;
            record.blank(); record.mId = effect; record.mData.mSchool = ESM::Skill::Destruction;
            record.mData.mBaseCost = 10.f;
            record.mData.mFlags = effect == ESM::MagicEffect::DamageHealth
                    || effect == ESM::MagicEffect::FireDamage || effect == ESM::MagicEffect::FrostDamage
                    || effect == ESM::MagicEffect::WeaknessToFire ? ESM::MagicEffect::Harmful : 0;
            store.insertStatic(record);
        }
    }
    void initialize(MWMechanics::NpcStats& stats)
    {
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            MWMechanics::AttributeValue value; value.setBase(40.f);
            stats.setAttribute(ESM::Attribute::indexToRefId(i), value, 1.f);
        }
        for (int i = 0; i < ESM::Skill::Length; ++i)
            stats.getSkill(ESM::Skill::indexToRefId(i)).setBase(100.f);
        stats.setHealth(MWMechanics::DynamicStat<float>(100.f));
        stats.setMagicka(MWMechanics::DynamicStat<float>(100.f));
        stats.setFatigue(MWMechanics::DynamicStat<float>(100.f));
    }
    ESM::Spell spell(std::string_view name, ESM::RefId effect, int range, int magnitude = 10)
    {
        ESM::Spell result; result.blank(); result.mId = id(name);
        result.mData.mType = ESM::Spell::ST_Spell; result.mData.mCost = 5;
        result.mData.mFlags = ESM::Spell::F_Always;
        ESM::IndexedENAMstruct entry{};
        entry.mData.mEffectID = effect; entry.mData.mRange = range;
        entry.mData.mDuration = 1; entry.mData.mMagnMin = entry.mData.mMagnMax = magnitude;
        result.mEffects.mList.push_back(entry);
        return result;
    }
    void weapons()
    {
        MWWorld::ESMStore store; content(store);
        setting(store, "fDamageStrengthBase", .5f); setting(store, "fDamageStrengthMult", .01f);
        setting(store, "fAIMeleeWeaponMult", 1.f); setting(store, "fCombatInvisoMult", 1.f);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        ESM::Weapon weapon; weapon.blank(); weapon.mId = id("synthetic sword");
        weapon.mData.mType = ESM::Weapon::ShortBladeOneHand;
        weapon.mData.mHealth = 100; weapon.mData.mSpeed = 1.f;
        for (auto* range : {&weapon.mData.mChop, &weapon.mData.mSlash, &weapon.mData.mThrust})
            (*range)[0] = (*range)[1] = 10;
        near(MWMechanics::weaponRatingBaseDamage(weapon), 10.f, "Shared melee rating arithmetic changed");
        near(MWMechanics::weaponRatingScore(weapon, 10.f, 50.f, 2.f), 10.f, "Shared hit/speed scaling changed");
        AiMagicContext context{caster, &enemy};
        const auto healthy = rateAiMeleeWeapon(context, weapon, 100, -1.f, true, store);
        require(healthy && *healthy > 0.f, "Equipped weapon not rated");
        const auto worn = rateAiMeleeWeapon(context, weapon, 50, -1.f, true, store);
        require(worn && *worn < *healthy, "AI ignored weapon condition");
        near(*rateAiMeleeWeapon(context, weapon, 0, -1.f, true, store), 0.f, "Broken weapon competed");
        enemy.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::ResistNormalWeapons), MWMechanics::EffectParam(100.f));
        near(*rateAiMeleeWeapon(context, weapon, 100, -1.f, true, store), 0.f, "AI ignored normal resistance");
        enemy.getMagicEffects() = {};
        caster.getSkill(ESM::Skill::ShortBlade).setBase(0.f);
        near(*rateAiMeleeWeapon(context, weapon, 100, -1.f, true, store), 0.f, "Zero-skill weapon competed");
        caster.getSkill(ESM::Skill::ShortBlade).setBase(100.f);
        auto fire = spell("competing fire", ESM::MagicEffect::FireDamage, ESM::RT_Target);
        std::array<AiMagicSpell, 1> sources{{{&fire}}};
        const auto cast = prepareAiMagicCast(context, sources, {}, store);
        require(bool(cast), "Competition fixture has no cast");
        context.weaponRating = cast->rating;
        require(!prepareAiMagicCast(context, sources, {}, store), "Spell won a weapon tie");
        context.weaponRating -= .01f;
        require(bool(prepareAiMagicCast(context, sources, {}, store)), "Stronger spell lost to weapon");
        context.weaponRating = cast->rating + .01f;
        require(!prepareAiMagicCast(context, sources, {}, store), "Stronger weapon lost to spell");
        ESM::Enchantment enchantment; enchantment.blank(); enchantment.mId = id("competing item");
        enchantment.mData.mType = ESM::Enchantment::WhenUsed;
        enchantment.mData.mCost = 10; enchantment.mData.mCharge = 100; enchantment.mEffects = fire.mEffects;
        store.insertStatic(enchantment);
        std::array<AiMagicItem, 1> items{{{{41, -1}, enchantment.mId, -1.f, true}}};
        context.weaponRating = 0.f;
        const auto item = prepareAiMagicCast(context, sources, items, store);
        require(item && item->payment, "Item did not compete");
        context.weaponRating = item->rating;
        require(prepareAiMagicCast(context, sources, items, store)->payment.has_value(), "Weapon won an item tie");
        context.weaponRating += .01f;
        require(!prepareAiMagicCast(context, sources, items, store), "Weaker item beat weapon");
        enchantment.mData.mType = ESM::Enchantment::WhenStrikes; store.overrideRecord(enchantment);
        weapon.mEnchant = enchantment.mId;
        const auto charged = rateAiMeleeWeapon(context, weapon, 100, -1.f, true, store);
        const auto depleted = rateAiMeleeWeapon(context, weapon, 100, 0.f, true, store);
        require(charged && depleted && *charged > *depleted, "AI ignored usable strike charge");
        enchantment.mEffects.mList.back().mData.mEffectID = ESM::MagicEffect::Levitate;
        store.overrideRecord(enchantment);
        require(!rateAiMeleeWeapon(context, weapon, 100, -1.f, true, store), "Unsupported strike rated partially");
        weapon.mEnchant = {};
        weapon.mData.mType = ESM::Weapon::MarksmanBow;
        require(!rateAiMeleeWeapon(context, weapon, 100, -1.f, true, store), "Ranged weapon silently treated as melee");
        weapon.mData.mType = ESM::Weapon::ShortBladeOneHand;
        for (auto* range : {&weapon.mData.mChop, &weapon.mData.mSlash, &weapon.mData.mThrust}) (*range)[0] = (*range)[1] = 0;
        near(MWMechanics::weaponRatingBaseDamage(weapon), 0.f, "Zero-damage weapon rating is not finite");
        setting(store, "fAIMeleeWeaponMult", 2.f);
        for (auto* range : {&weapon.mData.mChop, &weapon.mData.mSlash, &weapon.mData.mThrust}) (*range)[0] = (*range)[1] = 10;
        near(*rateAiMeleeWeapon(context, weapon, 100, -1.f, true, store), *healthy * 2.f, "AI cached another content rating");
        for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            context.weaponRating = invalid;
            bool rejected = false;
            try { prepareAiMagicCast(context, sources, items, store); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Malformed competition rating accepted");
        }
        near(caster.getMagicka().getCurrent(), 100.f, "Competition spent magicka");
        near(items[0].charge, -1.f, "Competition spent charge");
    }

    void selection()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto fire = spell("synthetic fire", ESM::MagicEffect::FireDamage, ESM::RT_Target);
        auto frost = spell("synthetic frost", ESM::MagicEffect::FrostDamage, ESM::RT_Target);
        auto heal = spell("synthetic heal", ESM::MagicEffect::RestoreHealth, ESM::RT_Self);
        auto ward = spell("synthetic ward", ESM::MagicEffect::ResistMagicka, ESM::RT_Self);
        std::array<AiMagicSpell, 4> spells{{{&fire}, {&frost}, {&heal}, {&ward}}};
        AiMagicContext context{caster, &enemy};
        auto selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == fire.mId, "Stable spell ordering changed");
        near(selected->rating, 30.f, "Target effect cost or AI GMST multiplier changed");
        MWMechanics::MagicEffects resistance;
        resistance.add(MWMechanics::EffectKey(ESM::MagicEffect::ResistFire), MWMechanics::EffectParam(100.f));
        enemy.getMagicEffects() = resistance;
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == frost.mId, "AI ignored target elemental resistance");
        spells[1].racialPower = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI selected a racial spell or useless ward/heal");
        spells[1].racialPower = false; spells[1].activeOnEnemy = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI refreshed an active target spell");
        spells[1].activeOnEnemy = false;
        context.casterUnderwater = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI launched Target magic underwater");
        context.casterUnderwater = false; context.enemyUnderwater = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI targeted an underwater enemy");
        context.enemyUnderwater = false;
        auto health = caster.getHealth(); health.setCurrent(10.f); caster.setHealth(health);
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == heal.mId, "Injured AI did not prefer restoration");
        spells[2].activeOnSelf = true;
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == frost.mId, "Active self restoration did not suppress recast");
        spells[2].activeOnSelf = false;
        context.enemy = nullptr;
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == heal.mId, "Self restoration required an enemy");
        caster.setKnockedDown(true);
        require(!prepareAiMagicCast(context, spells, {}, store), "Knocked AI selected a cast");
        caster.setKnockedDown(false);

        ESM::Enchantment enchantment; enchantment.blank(); enchantment.mId = id("synthetic used");
        enchantment.mData.mType = ESM::Enchantment::WhenUsed;
        enchantment.mData.mCost = 10; enchantment.mData.mCharge = 100;
        enchantment.mEffects = heal.mEffects; store.insertStatic(enchantment);
        std::array<AiMagicItem, 2> items{{{{41, -1}, enchantment.mId, -1.f, true},
            {{42, -1}, enchantment.mId, -1.f, true}}};
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && selected->payment && selected->payment->instance == items[0].instance,
            "AI lost item preference, stable ordering or source instance");
        const auto payment = prepareEnchantmentCast(enchantment, caster, -1.f, store, true);
        near(selected->payment->after, payment->chargeAfter, "AI charge diverged from player preparation");
        near(items[0].charge, -1.f, "Selection mutated item charge");
        near(caster.getHealth().getCurrent(), 10.f, "Selection applied effects");
        near(caster.getMagicka().getCurrent(), 100.f, "Selection spent magicka");
        items[0].equipped = false; items[1].charge = 0.f;
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && !selected->payment, "Unequipped or depleted WhenUsed item selected");
        items[0].equipped = true; items[0].activeOnSelf = true;
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && !selected->payment, "AI refreshed active item self effects");
        items[0].activeOnSelf = false;
        MWMechanics::MagicEffects silence;
        silence.add(MWMechanics::EffectKey(ESM::MagicEffect::Silence), MWMechanics::EffectParam(1.f)); caster.getMagicEffects() = silence;
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && selected->payment, "Silence incorrectly blocked enchanted item use");
        require(!prepareAiMagicCast(context, spells, {}, store), "Silenced AI selected a spell");
        caster.getMagicEffects() = {};

        // Changing content must change ratings; no function-static GMST cache.
        context.enemy = &enemy;
        spells[2].activeOnSelf = true;
        setting(store, "fAIRangeMagicSpellMult", 7.f);
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == frost.mId, "Changed content lost viable source");
        near(selected->rating, 70.f, "AI retained another content context's GMST");
    }
    void rejection()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto valid = spell("valid", ESM::MagicEffect::DamageHealth, ESM::RT_Target);
        auto malformed = valid; malformed.mId = id("mixed unsupported");
        auto unsupported = valid.mEffects.mList.front(); unsupported.mData.mEffectID = ESM::MagicEffect::Mark;
        malformed.mEffects.mList.push_back(unsupported);
        std::array<AiMagicSpell, 2> spells{{{&malformed}, {&valid}}};
        const AiMagicContext context{caster, &enemy};
        auto selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == valid.mId && selected->spell.effects.effects.size() == 1,
            "Unsupported mixed record was partially selected");
        malformed.mEffects = valid.mEffects;
        malformed.mData.mFlags = ESM::Spell::F_Autocalc;
        malformed.mEffects.mList.front().mData.mMagnMin = std::numeric_limits<int>::max();
        malformed.mEffects.mList.front().mData.mMagnMax = std::numeric_limits<int>::max();
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == valid.mId,
            "Malformed autocalc source bypassed effect bounds during restoration rating");
        auto magicka = caster.getMagicka(); magicka.setCurrent(4.f); caster.setMagicka(magicka);
        require(!prepareAiMagicCast(context, spells, {}, store), "Unaffordable spell selected");
        magicka.setCurrent(100.f); caster.setMagicka(magicka);
        const auto rejects = [&](auto candidates, auto items) {
            bool rejected = false;
            try { (void)prepareAiMagicCast(context, candidates, items, store); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Malformed AI source domain accepted");
        };
        std::array<AiMagicItem, 2> duplicate{{{{1, -1}, id("unused")}, {{1, -1}, id("unused")}}};
        rejects(std::span(spells), std::span(duplicate));
        duplicate[1].instance = {2, -1}; duplicate[1].charge = std::numeric_limits<float>::quiet_NaN();
        rejects(std::span(spells), std::span(duplicate));
        spells[1].record = nullptr;
        rejects(std::span(spells), std::span<const AiMagicItem>{});
        std::vector<AiMagicSpell> oversized(MaximumAiMagicSources + 1, AiMagicSpell{&valid});
        rejects(std::span(oversized), std::span<const AiMagicItem>{});
        near(caster.getMagicka().getCurrent(), 100.f, "Rejected selection mutated caster");
    }
    void launch()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto source = spell("launch source", ESM::MagicEffect::DamageHealth, ESM::RT_Target);
        const std::array spells{AiMagicSpell{&source}};
        const auto selected = prepareAiMagicCast({caster, &enemy}, spells, {}, store);
        require(bool(selected), "AI launch plan absent");
        Misc::Rng::Generator rng{17};
        const auto before = Misc::Rng::serialize(rng);
        const auto first = launchInstantSpell(selected->spell, caster, store, rng, false, false);
        require(first.succeeded, "Always-success AI source failed shared launch");
        near(caster.getMagicka().getCurrent(), 95.f, "AI launch did not spend player spell cost");
        near(enemy.getHealth().getCurrent(), 100.f, "AI launch applied Target effects before contact");
        require(Misc::Rng::serialize(rng) != before, "AI launch did not use explicit RNG");
        source.mData.mFlags = 0;
        caster.getSkill(ESM::Skill::Destruction).setBase(0.f);
        source.mData.mCost = 100;
        auto magicka = caster.getMagicka(); magicka.setCurrent(100.f); caster.setMagicka(magicka);
        const auto prepared = prepareInstantSpell(source, store, true);
        const auto failed = launchInstantSpell(*prepared, caster, store, rng, false, false);
        require(!failed.succeeded, "Impossible spell chance unexpectedly succeeded");
        near(caster.getMagicka().getCurrent(), 0.f, "Failed cast did not retain shared prescribed cost");
    }

    void items()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto attack = spell("mixed source", ESM::MagicEffect::FireDamage, ESM::RT_Target);
        auto weakness = spell("weakness", ESM::MagicEffect::WeaknessToFire, ESM::RT_Target);
        attack.mEffects.mList.insert(attack.mEffects.mList.begin(), weakness.mEffects.mList.front());
        ESM::Enchantment enchantment; enchantment.blank(); enchantment.mId = id("ordered enchanted source");
        enchantment.mData.mType = ESM::Enchantment::WhenUsed;
        enchantment.mData.mCost = 10; enchantment.mData.mCharge = 100; enchantment.mEffects = attack.mEffects;
        store.insertStatic(enchantment);
        std::array sources{AiMagicSpell{&attack}};
        std::array items{AiMagicItem{{7, -1}, enchantment.mId, -1.f, true}};
        const AiMagicContext context{caster, &enemy};
        auto selected = prepareAiMagicCast(context, sources, items, store);
        require(selected && selected->payment, "Target WhenUsed did not beat the equivalent spell");
        require(selected->spell.effects.effects.size() == 2
                && selected->spell.effects.effects[0].mEffectID == ESM::MagicEffect::WeaknessToFire
                && selected->spell.effects.effects[1].mEffectID == ESM::MagicEffect::FireDamage,
            "AI preparation reordered or merged weakness and damage");
        items[0].enemyDuration = 3.01f;
        selected = prepareAiMagicCast(context, sources, items, store);
        require(selected && !selected->payment, "AI refreshed a long-running item target effect");
        items[0].enemyDuration = 3.f;
        selected = prepareAiMagicCast(context, sources, items, store);
        require(selected && selected->payment, "AI lost stock three-second item refresh boundary");
        sources[0].activeOnEnemy = true;
        items[0].enemyDuration = 4.f;
        require(!prepareAiMagicCast(context, sources, items, store), "Both active sources remained selectable");
        sources[0].activeOnEnemy = false; items[0].enemyDuration = 0.f;
        caster.getSkill(ESM::Skill::Enchant).setBase(0.f);
        const auto unskilled = prepareAiMagicCast(context, {}, items, store);
        caster.getSkill(ESM::Skill::Enchant).setBase(100.f);
        const auto skilled = prepareAiMagicCast(context, {}, items, store);
        require(unskilled && skilled && unskilled->payment->after < skilled->payment->after,
            "Item preparation ignored the selected NPC's Enchant skill");
        // Charge is a proposed payment. Re-preparation cannot spend it, including
        // when a caller discards a candidate after a failed persistence attempt.
        const auto retry = prepareAiMagicCast(context, {}, items, store);
        require(retry && retry->payment->before == skilled->payment->before
                && retry->payment->after == skilled->payment->after,
            "Discard/retry changed the selected item's proposed payment");
        auto strike = enchantment; strike.mData.mType = ESM::Enchantment::WhenStrikes;
        store.overrideRecord(strike);
        require(!prepareAiMagicCast(context, {}, items, store), "WhenStrikes item entered AI use selection");
        auto once = enchantment; once.mData.mType = ESM::Enchantment::CastOnce;
        store.overrideRecord(once);
        require(!prepareAiMagicCast(context, {}, items, store), "Unimplemented AI CastOnce selection was enabled");
    }

    void records(const char* directory)
    {
        const std::string config = std::string("--config=") + directory;
        const char* args[]{"native-ai-records", config.c_str()};
        Loadout loadout(readLoadoutOptions(2, args));
        const auto& store = loadout.store();
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto health = caster.getHealth(); health.setCurrent(10.f); caster.setHealth(health);
        caster.setMagicka(MWMechanics::DynamicStat<float>(100000.f));
        const AiMagicContext context{caster, &enemy};
        size_t spells = 0, items = 0, trSpells = 0, trItems = 0, mixed = 0;
        for (const auto& source : store.get<ESM::Spell>())
        {
            const std::array candidates{AiMagicSpell{&source}};
            const auto selected = prepareAiMagicCast(context, candidates, {}, store);
            if (!selected) continue;
            require(!selected->payment && selected->spell.source == &source
                    && selected->spell.effects.effects.size() == source.mEffects.mList.size(),
                "Real spell selection lost source or effects");
            near(float(selected->spell.cost), float(MWMechanics::calcSpellCost(source, store)),
                "Real spell preparation diverged from engine cost");
            ++spells;
            if (source.mId.is<ESM::StringRefId>() && source.mId.getRefIdString().starts_with("T_")) ++trSpells;
            if (source.mEffects.mList.size() > 1) ++mixed;
        }
        for (const auto& source : store.get<ESM::Enchantment>())
        {
            if (source.mData.mType != ESM::Enchantment::WhenUsed) continue;
            const std::array candidates{AiMagicItem{{1, -1}, source.mId, -1.f, true}};
            const auto selected = prepareAiMagicCast(context, {}, candidates, store);
            if (!selected) continue;
            const auto payment = prepareEnchantmentCast(source, caster, -1.f, store, true);
            require(payment && selected->payment && selected->effectSource == source.mId
                    && selected->spell.effects.effects.size() == source.mEffects.mList.size(),
                "Real WhenUsed preparation lost source or effects");
            near(selected->payment->after, payment->chargeAfter, "Real item preparation diverged from player charge");
            ++items;
            if (source.mId.is<ESM::StringRefId>() && source.mId.getRefIdString().starts_with("T_")) ++trItems;
            if (source.mEffects.mList.size() > 1) ++mixed;
        }
        require(spells && items && mixed, "Real loadout lacks varied supported AI selections");
        std::cout << "Real records; synthetic caster/equipment/activity: spells=" << spells << " WhenUsed=" << items
            << " TR spells=" << trSpells << " TR WhenUsed=" << trItems << " mixed=" << mixed
            << "; selection/preparation only, no live network outcomes\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "records")
        { records(argv[2]); std::cout << "PASS records\n"; return 0; }
        if (argc != 2) throw std::invalid_argument("Select weapons, selection, rejection, launch, items or records <config>");
        const std::string_view filter = argv[1];
        if (filter == "weapons") weapons();
        else if (filter == "selection") selection();
        else if (filter == "rejection") rejection();
        else if (filter == "launch") launch();
        else if (filter == "items") items();
        else throw std::invalid_argument("Unknown AI magic filter");
        std::cout << "PASS " << filter << " (synthetic records; shared OpenMW rules; no Environment)\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << "FAIL native AI magic: " << error.what() << '\n'; return 1; }
}
