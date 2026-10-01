#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/creaturestats.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/constants.hpp>
#include <components/sceneutil/animationkeys.hpp>

#include "melee_animation.hpp"
#include "hit_animation.hpp"
#include "actor_campaign.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    void near(float actual, float expected, const char* message)
    {
        require(std::isfinite(actual) && std::abs(actual - expected) < .0001f, message);
    }
    void setting(MWWorld::ESMStore& store, const char* name, float value)
    {
        ESM::GameSetting setting;
        setting.blank();
        setting.mId = ESM::RefId::stringRefId(name);
        setting.mValue.setType(ESM::VT_Float);
        setting.mValue.setFloat(value);
        store.insertStatic(setting);
    }
    void content(MWWorld::ESMStore& store, float fatigueBase = 1.25f)
    {
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            ESM::Attribute attribute;
            attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
            store.insertStatic(attribute);
        }
        setting(store, "fFatigueBase", fatigueBase);
        setting(store, "fFatigueMult", .5f);
        setting(store, "fFatigueAttackBase", 2.f);
        setting(store, "fFatigueAttackMult", 4.f);
        setting(store, "fWeaponFatigueMult", .5f);
        setting(store, "fCombatInvisoMult", 1.f);
        setting(store, "fCombatDistance", 100.f);
        setting(store, "fHandToHandReach", .75f);
        setting(store, "fCombatKODamageMult", 2.f);
    }
    void initialize(MWMechanics::CreatureStats& actor, float health = 20.f)
    {
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            MWMechanics::AttributeValue attribute;
            attribute.setBase(40.f);
            actor.setAttribute(ESM::Attribute::indexToRefId(i), attribute, 1.f);
        }
        actor.setHealth(MWMechanics::DynamicStat<float>(health));
        actor.setMagicka(MWMechanics::DynamicStat<float>(30.f));
        actor.setFatigue(MWMechanics::DynamicStat<float>(100.f));
    }

    void projectileMechanics()
    {
        MWWorld::ESMStore store;
        setting(store, "fProjectileMinSpeed", 600);
        setting(store, "fProjectileMaxSpeed", 1400);
        setting(store, "fThrownWeaponMinSpeed", 300);
        setting(store, "fThrownWeaponMaxSpeed", 1100);
        near(MWMechanics::projectileLaunchSpeed(store, false, .25f), 800, "Bow speed lost content/strength");
        near(MWMechanics::projectileLaunchSpeed(store, true, .75f), 900, "Thrown speed used bow settings");
        ESM::Weapon bow, arrow; bow.blank(); arrow.blank();
        bow.mData.mChop[0] = 5; bow.mData.mChop[1] = 25;
        arrow.mData.mChop[0] = 2; arrow.mData.mChop[1] = 6;
        near(MWMechanics::projectileBaseDamage(bow, arrow, .5f), 19, "Arrow damage omitted");
        near(MWMechanics::projectileBaseDamage(arrow, arrow, .5f), 8, "Thrown damage must include both stock terms");
        const osg::Vec3f initial(800, -90, 60);
        const auto velocity = MWMechanics::advanceProjectileVelocity(initial, 1.f / 60);
        near(velocity.x(), initial.x(), "Projectile drag changed stock motion");
        near(velocity.y(), initial.y(), "Projectile lateral velocity changed");
        near(velocity.z(), 60 - Constants::GravityConst * Constants::UnitsPerMeter * .1f / 60,
            "Projectile gravity differs from stock units");
    }

    void mechanics()
    {
        // No Environment, World, player singleton or presentation service. These
        // fixtures exercise the same primitives called by stock combat/Npc.
        MWWorld::ESMStore store, otherStore;
        content(store);
        content(otherStore, 2.f);
        MWMechanics::CreatureStats first(store), second(store), target(store);
        initialize(first);
        initialize(second);
        initialize(target);
        near(first.getFatigueTerm(store), 1.25f, "Stock full fatigue term changed");
        near(first.getFatigueTerm(otherStore), 2.f, "Content context reused another store's static settings");
        near(MWMechanics::getHitChance(store, first, target, 40, false, false), 50.f,
            "Explicit full-fatigue hit chance differs from stock arithmetic");
        near(MWMechanics::applyKnockoutDamageMultiplier(store, target, 7.f), 7.f,
            "Standing target received knockout damage multiplier");
        target.setKnockedDown(true);
        near(MWMechanics::applyKnockoutDamageMultiplier(store, target, 7.f), 14.f,
            "Knocked target missed stock damage multiplier");
        target.setKnockedDown(false);
        ESM::Weapon ordinary;
        ordinary.blank();
        require(MWMechanics::isNormalWeapon(&ordinary, false), "Ordinary weapon lost resistance classification");
        ordinary.mData.mFlags = ESM::Weapon::Silver;
        require(!MWMechanics::isNormalWeapon(&ordinary, false), "Silver weapon used normal resistance");
        ordinary.mData.mFlags = ESM::Weapon::Magical;
        require(!MWMechanics::isNormalWeapon(&ordinary, false), "Magical weapon used normal resistance");
        ordinary.mData.mFlags = 0;
        ordinary.mEnchant = ESM::RefId::stringRefId("bound-test");
        require(MWMechanics::isNormalWeapon(&ordinary, false)
            && !MWMechanics::isNormalWeapon(&ordinary, true),
            "Enchanted weapon ignored bound gameplay setting");
        target.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::ResistNormalWeapons),
            MWMechanics::EffectParam(80.f));
        target.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::WeaknessToNormalWeapons),
            MWMechanics::EffectParam(30.f));
        near(MWMechanics::applyNormalWeaponResistance(target, 10.f), 5.f,
            "Normal weapon resistance and weakness did not compose");
        near(MWMechanics::getHitChance(store, first, target, 40, true, false), 65.f,
            "Unaware target retained ordinary evasion");
        near(MWMechanics::getHitChance(store, first, target, 40, false, true), 65.f,
            "Paralyzed target retained ordinary evasion");

        target.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Chameleon), MWMechanics::EffectParam(20.f));
        near(MWMechanics::getHitChance(store, first, target, 40, false, false), 30.f,
            "Target effects did not participate in hit chance");
        require(MWMechanics::getHitChance(store, first, target, 0, false, false) < 0,
            "Low skill did not produce an unavoidable miss");
        auto fatigue = target.getFatigue();
        fatigue.setCurrent(-1.f, true);
        target.setFatigue(fatigue);
        near(MWMechanics::getHitChance(store, first, target, 40, false, false), 65.f,
            "Negative fatigue failed to suppress target defense");

        // CharacterController zeroes strength on a failed accuracy roll. An
        // ordinary miss still spends base/encumbrance fatigue and one condition.
        MWMechanics::applyFatigueLoss(first, store, 10.f, 0.f, .5f);
        near(first.getFatigue().getCurrent(), 96.f, "Miss fatigue cost changed");
        near(second.getFatigue().getCurrent(), 100.f, "First actor's cost leaked into second actor");
        require(MWMechanics::weaponConditionAfterHit(100, 50.f, false, .1f) == 99,
            "Accuracy miss did not wear exactly one weapon condition");
        MWMechanics::applyFatigueLoss(second, store, 10.f, 1.f, .5f);
        near(second.getFatigue().getCurrent(), 91.f, "Full-strength fatigue cost changed");
        require(MWMechanics::weaponConditionAfterHit(100, 25.f, true, .1f) == 98,
            "Weapon wear did not retain integer truncation");
        require(MWMechanics::weaponConditionAfterHit(2, 50.f, true, .1f) == 0,
            "Weapon break did not clamp condition to zero");
        require(MWMechanics::weaponConditionAfterHit(0, 0.f, false, .1f) == 0,
            "Broken weapon condition underflowed");
        MWMechanics::applyFatigueLoss(second, store, 1000.f, 1.f, .5f);
        near(second.getFatigue().getCurrent(), 0.f, "Attack cost bypassed stock nonnegative fatigue clamp");

        ESM::Weapon weapon;
        weapon.blank();
        weapon.mData.mReach = 1.5f;
        near(MWMechanics::getMeleeWeaponReach(store, &weapon, true), 150.f, "Weapon reach ignored content");
        near(MWMechanics::getMeleeWeaponReach(store, nullptr, true), 75.f, "NPC hand-to-hand reach changed");
        near(MWMechanics::getMeleeWeaponReach(store, nullptr, false), 100.f, "Creature reach changed");
        const osg::Vec3f origin(0, 0, 0);
        require(MWMechanics::isInMeleeReach(origin, {131.99f, 0, 0}, 16, 16, 100),
            "Bounds-adjusted in-range contact rejected");
        require(!MWMechanics::isInMeleeReach(origin, {132, 0, 0}, 16, 16, 100),
            "Reach equality must be out of range");
        require(!MWMechanics::isInMeleeReach(origin, {0, 0, 100}, 50, 50, 100)
            && !MWMechanics::isInMeleeReach(origin, {0, 0, -100}, 50, 50, 100),
            "Large actor hull bypassed strict vertical reach");

        const MWWorld::TimeStamp deathTime(8.5f, 3);
        auto result = MWMechanics::applyHitDamage(target, {{"health", 19.f}}, deathTime);
        require(result.mHasDamage && result.mHasHealthDamage && !target.isDead(),
            "One remaining health must stay alive");
        near(target.getHealth().getCurrent(), 1.f, "Nonlethal health update changed");
        result = MWMechanics::applyHitDamage(target, {{"health", .0005f}}, deathTime);
        require(!result.mHasDamage && target.getHealth().getCurrent() == 1.f,
            "Subthreshold damage was applied");
        result = MWMechanics::applyHitDamage(target, {{"health", .25f}}, deathTime);
        require(target.isDead() && target.getHealth().getCurrent() == 0.f
            && target.getTimeOfDeath() == deathTime, "Lethal damage lost stock sub-one-health death/time semantics");
        require(!target.hasDied(), "Resource mutation emitted an unowned death notification");
        MWMechanics::applyHitDamage(target, {{"health", 100.f}}, MWWorld::TimeStamp(10.f, 5));
        require(target.getTimeOfDeath() == deathTime, "Repeated lethal damage reset death time");
        // Stock wrapper must also avoid a new world-clock lookup once dead.
        MWMechanics::applyHitDamage(target, {{"health", 1.f}});
        require(target.getTimeOfDeath() == deathTime, "Stock repeat changed the first death timestamp");
        near(first.getHealth().getCurrent(), 20.f, "Target death leaked into the attacking player");
        near(second.getHealth().getCurrent(), 20.f, "Target death leaked into the second player");
        ESM::StatState<float> savedHealth;
        target.getHealth().writeState(savedHealth);
        const auto savedTime = target.getTimeOfDeath().toEsm();
        require(savedHealth.mCurrent == 0.f && savedTime.mHour == 8.5f && savedTime.mDay == 3,
            "Stock field writers lost lethal health/time");

        MWMechanics::applyHitDamage(first, {{"fatigue", 120.f}, {"magicka", 50.f}}, deathTime);
        near(first.getFatigue().getCurrent(), -24.f, "Hit fatigue damage lost below-zero behavior");
        near(first.getMagicka().getCurrent(), 0.f, "Hit magicka damage lost its zero clamp");
        const auto before = first.getHealth();
        bool rejected = false;
        try { first.setDynamic(3, MWMechanics::DynamicStat<float>(0.f), deathTime); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected && first.getHealth() == before && !first.isDead(),
            "Invalid dynamic stat index mutated actor state");
    }

    void timing()
    {
        using namespace MWMechanics;
        near(attackWindUp(.5f, 1.f, 2.f), 0.f, "Early release did not clamp to zero");
        near(attackWindUp(1.25f, 1.f, 2.f), .25f, "Animation-relative wind-up changed");
        near(attackWindUp(3.f, 1.f, 2.f), 1.f, "Late release did not clamp to one");
        require(attackWindUp(1.f, -1.f, 2.f) == -1.f && attackWindUp(1.f, 2.f, 2.f) == -1.f
            && attackWindUp(1.f, 3.f, 2.f) == -1.f, "Missing/reversed wind-up keys lost stock fallback");
        for (const auto group : {"attack1", "swimattack1"})
            require(meleeHitType(group, "hit") == ESM::Weapon::AT_Chop, "Generic chop key changed");
        for (const auto group : {"attack2", "swimattack2"})
            require(meleeHitType(group, "hit") == ESM::Weapon::AT_Slash, "Generic slash key changed");
        for (const auto group : {"attack3", "swimattack3"})
            require(meleeHitType(group, "hit") == ESM::Weapon::AT_Thrust, "Generic thrust key changed");
        require(meleeHitType("weapononehand", "chop hit") == ESM::Weapon::AT_Chop
            && meleeHitType("weapononehand", "slash hit") == ESM::Weapon::AT_Slash
            && meleeHitType("weapononehand", "thrust hit") == ESM::Weapon::AT_Thrust,
            "Directional weapon hit keys changed");
        require(meleeHitType("spellcast", "hit") == -1 && meleeHitType("attack1", "stop") == -1,
            "Unrelated text key became a melee hit");
        SceneUtil::TextKeyMap keys;
        keys.emplace(0.f, "attack1: start");
        keys.emplace(.1f, "attack10: hit");
        keys.emplace(.2f, "attack1: stop");
        keys.emplace(.3f, "attack1: hit");
        require(!hasMeleeHitKey("attack1", keys.begin(), keys),
            "A foreign or post-stop hit key suppressed hit-at-start fallback");
        keys.emplace(.15f, "attack1: hit");
        require(hasMeleeHitKey("attack1", keys.begin(), keys), "Authored hit key was not discovered");
        SceneUtil::TextKeyMap empty;
        require(!hasMeleeHitKey("attack1", empty.begin(), empty), "Empty key map reported a hit");
    }

    SceneUtil::TextKeyMap directionalKeys(bool minimumHit = true)
    {
        SceneUtil::TextKeyMap keys;
        keys.emplace(0.f, "weapononehand: chop start");
        keys.emplace(.125f, "weapononehand: chop min attack");
        keys.emplace(.5f, "weapononehand: chop max attack");
        if (minimumHit) keys.emplace(.625f, "weapononehand: chop min hit");
        keys.emplace(.75f, "weapononehand: chop hit");
        keys.emplace(.75f, "weapononehand: chop hit"); // One swing, two authored keys.
        keys.emplace(.6f, "weapononehandextra: chop hit");
        for (const auto& [name, start] : {std::pair{"small", 1.f}, {"medium", 2.f}, {"large", 3.f}})
        {
            keys.emplace(start, "weapononehand: chop " + std::string(name) + " follow start");
            keys.emplace(start + .125f, "weapononehand: chop hit"); // Must not hit in follow-through.
            keys.emplace(start + .25f, "weapononehand: chop " + std::string(name) + " follow stop.");
        }
        return keys;
    }

    void hitResources()
    {
        using TES3MP::Native::readHitAnimations;
        SceneUtil::TextKeyMap base, female, custom, empty;
        base.emplace(0.f, "hit1: start"); base.emplace(.5f, "hit1: stop");
        base.emplace(1.f, "hit2: start"); base.emplace(2.f, "hit2: stop");
        female.emplace(3.f, "hit1: start"); female.emplace(3.25f, "hit1: stop");
        custom.emplace(4.f, "hit2: start"); custom.emplace(4.125f, "hit2: stop");
        const auto first = readHitAnimations(std::array<const SceneUtil::TextKeyMap*, 1>{&base});
        const auto second = readHitAnimations(std::array<const SceneUtil::TextKeyMap*, 2>{&base, &female});
        const auto npc = readHitAnimations(std::array<const SceneUtil::TextKeyMap*, 3>{&base, &female, &custom});
        require(first.count == 2 && first.ticks[0] == 15 && first.ticks[1] == 30
            && second.count == 2 && second.ticks[0] == 8 && second.ticks[1] == 30
            && npc.count == 2 && npc.ticks[0] == 8 && npc.ticks[1] == 4,
            "Participant resources lost group-specific source priority or rounded timing");
        require(readHitAnimations({}).count == 0
            && readHitAnimations(std::array<const SceneUtil::TextKeyMap*, 2>{&base, &empty}) == first,
            "Missing hit groups hid lower layers or fabricated recovery");
        SceneUtil::TextKeyMap gap;
        gap.emplace(1.f, "hit3: start"); gap.emplace(2.f, "hit3: stop");
        require(readHitAnimations(std::array<const SceneUtil::TextKeyMap*, 1>{&gap}).count == 0,
            "Nonconsecutive recovery groups entered stock random selection");
        const auto rejects = [&](const SceneUtil::TextKeyMap& keys) {
            bool rejected = false;
            try { (void)readHitAnimations(std::array<const SceneUtil::TextKeyMap*, 2>{&base, &keys}); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Invalid winning hit resource silently fell back");
        };
        SceneUtil::TextKeyMap missingStop;
        missingStop.emplace(3.f, "hit1: start"); rejects(missingStop);
        SceneUtil::TextKeyMap reversed;
        reversed.emplace(2.f, "hit1: start"); reversed.emplace(1.f, "hit1: stop"); rejects(reversed);
        SceneUtil::TextKeyMap tooLong;
        tooLong.emplace(1.f, "hit1: start"); tooLong.emplace(62.f, "hit1: stop"); rejects(tooLong);
        auto oversized = base; oversized.emplace(3.f, std::string(257, 'x')); rejects(oversized);
        SceneUtil::TextKeyMap many;
        for (unsigned i = 1; i <= 17; ++i)
        {
            many.emplace(float(i * 2), "hit" + std::to_string(i) + ": start");
            many.emplace(float(i * 2 + 1), "hit" + std::to_string(i) + ": stop");
        }
        rejects(many);
    }

    void knockoutTiming()
    {
        require(MWMechanics::isFatigueKnockout(0.f, 0.f)
            && MWMechanics::isFatigueKnockout(0.f, 100.f)
            && MWMechanics::isFatigueKnockout(100.f, -.001f), "Stock exhaustion condition lost zero-base or negative fatigue");
        require(!MWMechanics::isFatigueKnockout(100.f, 0.f)
            && !MWMechanics::isFatigueKnockout(100.f, 100.f), "Nonnegative fatigue with positive base caused knockout");
        using namespace TES3MP::Native;
        SceneUtil::TextKeyMap base, override;
        base.emplace(1.f, "knockout: start");
        base.emplace(1.5f, "knockout: loop start");
        base.emplace(2.f, "knockout: loop stop");
        base.emplace(3.f, "knockout: stop");
        const auto clip = readKnockoutAnimation(std::array<const SceneUtil::TextKeyMap*, 1>{&base});
        require(clip.stop == 60 && clip.loopStart == 15 && clip.loopStop == 30,
            "Knockout keys lost authored timing");
        unsigned frame = 0;
        for (unsigned i = 0; i < 30; ++i) frame = clip.advance(frame, true);
        require(frame == 15, "Exhausted knockout failed to loop");
        for (unsigned i = 0; i < 44; ++i) frame = clip.advance(frame, false);
        require(frame == 59, "Restored fatigue skipped authored get-up tail");
        require(clip.advance(frame, false) == clip.stop, "Get-up failed to finish");
        override.emplace(4.f, "knockout: start"); override.emplace(5.f, "knockout: stop");
        const auto layered = readKnockoutAnimation(std::array<const SceneUtil::TextKeyMap*, 2>{&base, &override});
        require(layered.stop == 30 && !layered.loopStop, "Knockout merged keys from different layers");
        require(layered.advance(29, true) == 29 && layered.advance(29, false) == 30,
            "Nonlooping knockout released an exhausted actor");
        require(readKnockoutAnimation({}).advance(0, true) == 0
            && readKnockoutAnimation({}).advance(0, false) == 1, "Missing knockout fallback invalid");
        SceneUtil::TextKeyMap held;
        held.emplace(1.f, "knockout: start"); held.emplace(2.f, "knockout: loop start");
        held.emplace(2.f, "knockout: loop stop"); held.emplace(3.f, "knockout: stop");
        const auto frozen = readKnockoutAnimation(std::array<const SceneUtil::TextKeyMap*, 1>{&held});
        require(frozen.loopStart == 30 && frozen.loopStop == 31 && frozen.advance(30, true) == 30
            && frozen.advance(30, false) == 31, "Stock creature exhaustion hold lost recovery tail");
        override.emplace(6.f, "knockout: loop stop");
        bool rejected = false;
        try { (void)readKnockoutAnimation(std::array<const SceneUtil::TextKeyMap*, 2>{&base, &override}); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Knockout loop outside its clip accepted");
    }

    void hitKnockdown()
    {
        MWWorld::ESMStore store; content(store);
        setting(store, "fKnockDownMult", .5f);
        for (const auto& [name, value] : {std::pair{"iKnockDownOddsMult", 100}, {"iKnockDownOddsBase", 10}})
        {
            ESM::GameSetting record; record.blank(); record.mId = ESM::RefId::stringRefId(name);
            record.mValue.setType(ESM::VT_Int); record.mValue.setInteger(value); store.insertStatic(record);
        }
        MWMechanics::CreatureStats victim(store); initialize(victim);
        Misc::Rng::Generator actual{17}, expected{17};
        require(!MWMechanics::rollHitKnockdown(store, victim, 0, actual)
            && !MWMechanics::rollHitKnockdown(store, victim, 19, actual)
            && Misc::Rng::serialize(actual) == Misc::Rng::serialize(expected),
            "Non-health or subthreshold hit consumed knockdown RNG");
        for (unsigned i = 0; i < 100; ++i)
            require(MWMechanics::rollHitKnockdown(store, victim, 20, actual)
                == (50 <= Misc::Rng::roll0to99(expected)), "Knockdown changed stock threshold/odds");
        require(Misc::Rng::serialize(actual) == Misc::Rng::serialize(expected), "Knockdown RNG stream differs");
    }

    void attackModes()
    {
        ESM::Weapon weapon;
        weapon.blank();
        weapon.mData.mSlash[0] = 1; weapon.mData.mSlash[1] = 4;
        weapon.mData.mChop[0] = 5; weapon.mData.mChop[1] = 8;
        weapon.mData.mThrust[0] = 9; weapon.mData.mThrust[1] = 12;
        Misc::Rng::Generator actual{713}, stock{713};
        std::array<unsigned, 3> counts{};
        for (unsigned i = 0; i < 1024; ++i)
        {
            // Original AiCombat arithmetic: integer averages 2, 6 and 10.
            const float roll = Misc::Rng::rollClosedProbability(stock) * 18;
            const auto expected = roll <= 2 ? "slash" : roll <= 12 ? "thrust" : "chop";
            const auto mode = MWMechanics::chooseMeleeAttack(&weapon, actual);
            require(mode == expected && actual == stock, "Weighted attack selection or RNG consumption differs from stock");
            ++counts[mode == "chop" ? 0 : mode == "slash" ? 1 : 2];
        }
        require(counts[2] > counts[0] && counts[0] > counts[1] && counts[1] > 0,
            "Weighted selection did not exercise all directions");
        weapon.blank();
        require(MWMechanics::chooseMeleeAttack(&weapon, actual) == "slash",
            "Zero-damage weapon lost stock slash tie");
        Misc::Rng::rollClosedProbability(stock);
        require(actual == stock, "Zero-damage weapon stopped consuming its stock roll");
        counts = {};
        for (unsigned i = 0; i < 1024; ++i)
        {
            const float roll = Misc::Rng::rollProbability(stock);
            const auto expected = roll >= 2 / 3.f ? "thrust" : roll >= 1 / 3.f ? "slash" : "chop";
            const auto mode = MWMechanics::chooseMeleeAttack(nullptr, actual);
            require(mode == expected && actual == stock, "Unarmed selection differs from CharacterController");
            ++counts[mode == "chop" ? 0 : mode == "slash" ? 1 : 2];
        }
        require(counts[0] && counts[1] && counts[2], "Unarmed selection did not exercise all directions");
        for (const auto mode : {"chop", "slash", "thrust"})
        {
            SceneUtil::TextKeyMap keys;
            for (const auto& [time, key] : directionalKeys())
            {
                if (time >= 1.f && key == "weapononehand: chop hit") continue;
                std::string renamed(key);
                const auto at = renamed.find("chop");
                if (at != std::string::npos) renamed.replace(at, 4, mode);
                keys.emplace(time, std::move(renamed));
            }
            TES3MP::Native::MeleeAnimation swing(keys, "weapononehand", mode, 1.f);
            unsigned hits = 0;
            for (unsigned tick = 0; tick < 240; ++tick)
            {
                // Recovery must retain every intermediate state without replaying a hit.
                TES3MP::Native::MeleeAnimation recovered(keys, "weapononehand", mode, 1.f);
                recovered.restore(swing.snapshot());
                if (swing.windUp() == 1.f)
                { swing.release(1.f); recovered.release(1.f); }
                const auto hit = swing.advance(1.f / 30);
                require(recovered.advance(1.f / 30) == hit && recovered.snapshot() == swing.snapshot(),
                    "Directional animation changed at restart");
                if (hit)
                {
                    ++hits;
                    require(*hit == MWMechanics::meleeHitType("weapononehand", std::string(mode) + " hit"),
                        "Directional hit emitted the wrong damage mode");
                }
            }
            require(hits == 1 && swing.snapshot().mPhase == TES3MP::Native::MeleeAnimation::Phase::Complete,
                "Directional animation did not finish with exactly one hit");
        }
    }

    void scheduling()
    {
        using TES3MP::Native::MeleeAnimation;
        require(TES3MP::Native::carriedLeftVisibleForWeapon(ESM::Weapon::ShortBladeOneHand)
            && TES3MP::Native::carriedLeftVisibleForWeapon(ESM::Weapon::None)
            && !TES3MP::Native::carriedLeftVisibleForWeapon(ESM::Weapon::LongBladeTwoHand)
            && !TES3MP::Native::carriedLeftVisibleForWeapon(ESM::Weapon::MarksmanBow),
            "OpenMW carried-left block readiness changed with weapon type");
        using Phase = MeleeAnimation::Phase;
        using namespace MWMechanics;
        near(attackReleaseStartPoint(.5f, .125f, .5f, .625f, .75f), .25f,
            "Release lost the strength-dependent min-hit skip");
        near(attackReleaseStartPoint(0.f, .125f, .5f, -1.f, .75f), 1.f,
            "Missing min-hit key lost stock full pre-hit skip");
        near(attackReleaseStartPoint(.5f, -1.f, .5f, .625f, .75f), 0.f,
            "Missing wind-up keys must not skip pre-hit time");
        require(attackFollowStrength(.329f) == "small" && attackFollowStrength(.33f) == "medium"
            && attackFollowStrength(.659f) == "medium" && attackFollowStrength(.66f) == "large",
            "Follow-through thresholds changed");

        SceneUtil::TextKeyMap sections;
        sections.emplace(0.f, "walk: start");
        sections.emplace(1.f, "walk: stop");
        sections.emplace(2.f, "walk: start");
        sections.emplace(3.f, "walk: stop.");
        sections.emplace(4.f, "walker: start");
        SceneUtil::AnimationKeys range;
        require(SceneUtil::findAnimationKeys(sections, "walk", "loop start", "stop", range)
            && range.mStart->first == 2.f && range.mStop->first == 3.f,
            "Shared section lookup lost last-group selection, loop fallback or stop suffix tolerance");
        require(!SceneUtil::findAnimationKeys(sections, "absent", "start", "stop", range),
            "Missing animation section was fabricated");
        require(!SceneUtil::findAnimationKeys(sections, "walk", "stop.", "start", range),
            "Reversed animation section was admitted");

        // Authored follow sections also contain hit keys, so the last generic
        // hit would be selected by stock reverse section lookup. Reject that
        // ambiguous layout before admitting it to the bounded scheduler.
        bool rejected = false;
        try { MeleeAnimation invalid(directionalKeys(), "weapononehand", "chop", 1.f); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Ambiguous directional clip was silently scheduled");

        SceneUtil::TextKeyMap keys;
        const auto authored = directionalKeys();
        for (const auto& [time, key] : authored)
            if (time < 1.f || key != "weapononehand: chop hit") keys.emplace(time, std::string(key));
        // A different attack key in follow-through must not generate a hit.
        keys.emplace(3.125f, "weapononehand: slash hit");
        keys.emplace(4.f, "hit1: start");
        keys.emplace(4.25f, "hit1: stop");
        keys.emplace(5.f, "hit2: start");
        keys.emplace(5.5f, "hit2: stop.");
        MeleeAnimation committed(keys, "weapononehand", "chop", 1.f);
        require(committed.hitRecoveryGroupCount() == 2
            && committed.hitRecoveryTicks(0) == 8 && committed.hitRecoveryTicks(1) == 15,
            "Stock hit clip timing did not bind to CPU recovery frames");
        const auto original = committed.snapshot();
        auto rejectedTick = committed;
        require(rejectedTick.release(1.f), "Initial release rejected");
        require(!rejectedTick.release(0.f) && rejectedTick.snapshot().mStrength == 1.f,
            "Repeated release changed the chosen swing");
        int hits = 0;
        for (int i = 0; i < 40; ++i)
            if (rejectedTick.advance(1.f / 60)) ++hits;
        require(hits == 1 && rejectedTick.snapshot().mHit && committed.snapshot() == original,
            "Detached tick leaked state or dispatched duplicate hit keys");
        auto retriedTick = committed;
        retriedTick.release(1.f);
        int retryHits = 0;
        for (int i = 0; i < 40; ++i)
            if (retriedTick.advance(1.f / 60)) ++retryHits;
        require(retryHits == 1 && retriedTick.snapshot() == rejectedTick.snapshot(),
            "Discard and retry changed the staged schedule");
        committed = retriedTick; // Future host installation follows durability.
        {
            using namespace TES3MP::Native;
            const std::string identity = "native-melee-resource-1\nmeshes/test.kf:1:2\n";
            const auto saved = committed.snapshot();
            std::vector<char> image;
            putAreaWord(image, MeleeActorCampaignMagic);
            putAreaWord(image, 1); putAreaWord(image, 1); putAreaWord(image, 17);
            for (unsigned i = 0; i < 3; ++i) putAreaWord(image, 0);
            putAreaWord(image, identity.size()); image.insert(image.end(), identity.begin(), identity.end());
            putAreaWord(image, uint64_t(saved.mPhase));
            putAreaWord(image, std::bit_cast<uint32_t>(saved.mTime));
            putAreaWord(image, std::bit_cast<uint32_t>(saved.mStrength));
            putAreaWord(image, saved.mReleased); putAreaWord(image, saved.mHit);
            image.push_back('I'); image.push_back('A');
            const auto decoded = readActorCampaign(image);
            require(decoded.tick == 17 && decoded.inventory[0] == 'I' && decoded.actor[0] == 'A'
                && decoded.melee && decoded.melee->identity == identity && decoded.melee->state == saved,
                "Durable actor campaign lost bound animation identity or swing state");
            MeleeAnimation resumed(keys, "weapononehand", "chop", 1.f);
            resumed.restore(decoded.melee->state);
            require(resumed.snapshot() == saved && !resumed.advance(1.f / 30),
                "Recovered committed swing replayed its hit proposal");
            auto corrupt = image;
            corrupt[56 + 8 + identity.size()] = char(255); // Phase word.
            bool invalid = false;
            try { (void)readActorCampaign(corrupt); }
            catch (const std::invalid_argument&) { invalid = true; }
            require(invalid, "Invalid saved swing phase was accepted");
            auto wrong = saved; wrong.mHit = false;
            invalid = false;
            try { resumed.restore(wrong); }
            catch (const std::invalid_argument&) { invalid = true; }
            require(invalid && resumed.snapshot() == saved, "Invalid saved swing changed the live schedule");
        }
        for (int i = 0; i < 30; ++i)
            require(!committed.advance(1.f / 60), "Committed hit replayed during follow-through/completion");
        require(committed.snapshot().mPhase == Phase::Complete, "Follow-through did not finish");

        MeleeAnimation early(keys, "weapononehand", "chop", 1.f);
        near(early.windUp(), 0.f, "Initial wind-up was nonzero");
        early.release(0.f);
        require(!early.advance(1.f / 32) && early.snapshot().mPhase == Phase::WindUp,
            "Early release skipped the required pre-wind-up section");
        for (int i = 0; i < 3; ++i) early.advance(1.f / 32);
        near(early.snapshot().mTime, .125f, "Early-release wind-up did not reach the authored minimum");
        require(!early.advance(0.f) && early.snapshot().mPhase == Phase::Release,
            "Early release failed to enter its authored release section");
        near(early.snapshot().mTime, .625f, "Miss strength did not select the authored min-hit time");
        for (int i = 0; i < 3; ++i) require(!early.advance(1.f / 32), "Hit dispatched before its key");
        require(early.advance(1.f / 32) == ESM::Weapon::AT_Chop, "Exact hit-time boundary lost the event");
        require(!early.advance(0.f) && early.snapshot().mTime == 1.f,
            "Zero-strength swing did not select small follow-through");

        MeleeAnimation held(keys, "weapononehand", "chop", 2.f);
        for (int i = 0; i < 20; ++i) require(!held.advance(1.f / 32), "Held swing hit before release");
        near(held.windUp(), 1.f, "Held wind-up did not saturate at full strength");
        near(held.snapshot().mTime, .5f, "Held swing advanced past max attack");
        held.release(.5f);
        held.advance(0.f);
        near(held.snapshot().mTime, .5625f, "Half-strength release start changed");
        for (int i = 0; i < 2; ++i) require(!held.advance(1.f / 32), "Weapon speed hit too early");
        require(held.advance(1.f / 32).has_value(), "Weapon speed was not applied to authored time");
        held.advance(0.f);
        near(held.snapshot().mTime, 2.f, "Half-strength swing did not select medium follow-through");

        SceneUtil::TextKeyMap fixedWindup;
        for (const auto& [time, key] : keys)
            fixedWindup.emplace(key == "weapononehand: chop min attack" ? .5f : time, std::string(key));
        MeleeAnimation fixed(fixedWindup, "weapononehand", "chop", 1.f);
        for (int i = 0; i < 16; ++i) fixed.advance(1.f / 32);
        near(fixed.windUp(), -1.f, "Custom fixed wind-up lost stock random-strength fallback");
        require(fixed.phaseCompletion() == 1.f && fixed.release(.5f), "Custom fixed wind-up failed to release");
        MeleeAnimation resumed(fixedWindup, "weapononehand", "chop", 1.f);
        resumed.restore(fixed.snapshot());
        unsigned fixedHits = 0;
        for (unsigned i = 0; i < 96; ++i)
        {
            const auto hit = fixed.advance(1.f / 32);
            require(resumed.advance(1.f / 32) == hit && resumed.snapshot() == fixed.snapshot(),
                "Custom fixed wind-up restart changed its clock or hit");
            fixedHits += hit.has_value();
        }
        require(fixedHits == 1 && fixed.snapshot().mPhase == Phase::Complete,
            "Custom fixed wind-up duplicated its hit or failed to complete");

        SceneUtil::TextKeyMap withoutMinimumHit;
        for (const auto& [time, key] : keys)
            if (key != "weapononehand: chop min hit") withoutMinimumHit.emplace(time, std::string(key));
        MeleeAnimation skip(withoutMinimumHit, "weapononehand", "chop", 1.f);
        for (int i = 0; i < 16; ++i) skip.advance(1.f / 32);
        skip.release(0.f);
        require(skip.advance(0.f) == ESM::Weapon::AT_Chop,
            "Hit at release start was skipped without a min-hit key");
        require(skip.snapshot().mPhase == Phase::Follow && skip.snapshot().mTime == 1.f,
            "Held endpoint release delayed stock synchronous follow-through");
        require(!skip.advance(0.f), "Hit at release start replayed");

        const auto before = held.snapshot();
        for (const float invalid : {-1.f, 1.f, std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::quiet_NaN()})
        {
            rejected = false;
            try { held.advance(invalid); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && held.snapshot() == before, "Invalid step changed native schedule");
        }
        rejected = false;
        try { held.release(std::numeric_limits<float>::quiet_NaN()); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && held.snapshot() == before, "Invalid strength changed native schedule");
        SceneUtil::TextKeyMap incomplete;
        incomplete.emplace(0.f, "weapononehand: chop start");
        rejected = false;
        try { MeleeAnimation invalid(incomplete, "weapononehand", "chop", 1.f); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Incomplete clip silently acquired attack timers");
        SceneUtil::TextKeyMap badHit;
        for (const auto& [time, key] : keys)
            badHit.emplace(time, key == "weapononehand: chop hit" ? key + '.' : std::string(key));
        rejected = false;
        try { MeleeAnimation invalid(badHit, "weapononehand", "chop", 1.f); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Tolerated section-stop suffix silently removed the gameplay hit");
        auto oversized = keys;
        oversized.emplace(5.f, std::string(257, 'x'));
        rejected = false;
        try { MeleeAnimation invalid(oversized, "weapononehand", "chop", 1.f); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Unbounded text key was copied into native schedule");
        auto tooMany = keys;
        for (int i = 0; i < 4096; ++i) tooMany.emplace(5.f, "sound: unrelated");
        rejected = false;
        try { MeleeAnimation invalid(tooMany, "weapononehand", "chop", 1.f); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Unbounded text-key count was copied into native schedule");
        for (const float invalidSpeed : {0.f, -1.f, 101.f, std::numeric_limits<float>::infinity()})
        {
            rejected = false;
            try { MeleeAnimation invalid(keys, "weapononehand", "chop", invalidSpeed); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Invalid weapon animation speed was admitted");
        }
    }
}

namespace
{
    void creatureScheduling()
    {
        using TES3MP::Native::MeleeAnimation;
        for (unsigned direction = 0; direction < 3; ++direction)
            for (bool hitKey : {false, true})
            {
                const auto group = "attack" + std::to_string(direction + 1);
                SceneUtil::TextKeyMap keys;
                keys.emplace(1.f, group + ": start"); keys.emplace(2.f, group + ": stop");
                if (hitKey) keys.emplace(1.4f, group + ": hit");
                const std::array<std::string, 3> modes{"chop", "slash", "thrust"};
                MeleeAnimation clip(keys, group, modes[direction], 1.f);
                require(clip.windUp() == -1.f && clip.release(.5f), "Creature attack lost stock random strength");
                unsigned hits = 0;
                for (unsigned frame = 0; frame < 70; ++frame)
                {
                    MeleeAnimation restored(keys, group, modes[direction], 1.f);
                    restored.restore(clip.snapshot());
                    const auto actual = clip.advance(1.f / 30);
                    require(restored.advance(1.f / 30) == actual && restored.snapshot() == clip.snapshot(),
                        "Creature attack restart changed clock or duplicated hit");
                    if (actual) { ++hits; require(*actual == int(direction), "Creature hit chose the wrong damage range"); }
                }
                require(hits == 1 && clip.snapshot().mPhase == MeleeAnimation::Phase::Complete,
                    "Creature hit key/start fallback did not complete exactly once");
            }
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Select melee-context, melee-timing or melee-scheduling");
        const std::string_view filter = argv[1];
        if (filter == "melee-context") mechanics();
        else if (filter == "melee-timing") timing();
        else if (filter == "melee-scheduling") scheduling();
        else if (filter == "hit-resources") hitResources();
        else if (filter == "knockout-timing") knockoutTiming();
        else if (filter == "hit-knockdown") hitKnockdown();
        else if (filter == "attack-modes") attackModes();
        else if (filter == "projectile-mechanics") projectileMechanics();
        else if (filter == "creature-scheduling") creatureScheduling();
        else throw std::invalid_argument("Unknown melee filter");
        std::cout << "PASS " << filter << " (synthetic content, shared stock primitives, no Environment)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL native melee: " << error.what() << '\n';
        return 1;
    }
}
