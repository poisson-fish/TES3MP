#ifndef OPENMW_MWMECHANICS_MELEESTATE_H
#define OPENMW_MWMECHANICS_MELEESTATE_H

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <components/sceneutil/textkeymap.hpp>
#include <osg/Vec3f>

#include "../mwworld/timestamp.hpp"

namespace ESM { struct Weapon; }
namespace MWWorld { class ESMStore; }

namespace MWMechanics
{
    class CreatureStats;

    // Shared stock mechanics with explicit actor/content context. These are
    // simulation operations, not command authorization: the caller owns timing,
    // collision selection, staging, RNG and publication. No player/UI lookup.
    float getHitChance(const MWWorld::ESMStore& store, const CreatureStats& attacker,
        const CreatureStats& victim, int skillValue, bool unaware, bool paralyzed);
    void applyFatigueLoss(CreatureStats& attacker, const MWWorld::ESMStore& store,
        float weaponWeight, float attackStrength, float normalizedEncumbrance);
    // The ordinary NPC hand-to-hand hit against a standing target damages
    // fatigue. This detached-stats form shares the stock damage calculation.
    float getUnarmedFatigueDamage(const MWWorld::ESMStore& store, const CreatureStats& attacker,
        float handToHandSkill, float attackStrength);
    float getUnarmedHealthDamage(const MWWorld::ESMStore& store, const CreatureStats& attacker,
        float handToHandSkill, float attackStrength);
    float applyKnockoutDamageMultiplier(const MWWorld::ESMStore& store,
        const CreatureStats& victim, float damage);
    bool isNormalWeapon(const ESM::Weapon* weapon, bool enchantedWeaponsAreMagical);
    float applyNormalWeaponResistance(const CreatureStats& victim, float damage);
    void restoreCombatFatigue(CreatureStats& actor, const MWWorld::ESMStore& store, float seconds);
    int weaponConditionAfterHit(int condition, float damage, bool hit, float damageMultiplier);
    float getMeleeWeaponReach(const MWWorld::ESMStore& store, const ESM::Weapon* weapon, bool npc);
    bool isInMeleeReach(const osg::Vec3f& attacker, const osg::Vec3f& target,
        float attackerHalfExtentY, float targetHalfExtentY, float reach);

    struct HitDamageResult
    {
        bool mHasDamage = false;
        bool mHasHealthDamage = false;
        float mHealthDamage = 0;
    };
    // A supplied clock permits lethal damage on detached stats. The stock
    // caller omits it and retains CreatureStats' lazy world-clock lookup.
    // Death notification/attribution is still the enclosing caller's job.
    HitDamageResult applyHitDamage(CreatureStats& victim, const std::map<std::string, float>& damages,
        const std::optional<MWWorld::TimeStamp>& time = {});

    // Gameplay text-key interpretation shared with CharacterController. Missing
    // wind-up keys retain the stock random-strength fallback (-1); missing hit
    // keys for attack1..3 retain the stock hit-at-start behavior.
    float attackWindUp(float currentTime, float minimumTime, float maximumTime);
    float attackReleaseStartPoint(float strength, float minimumAttackTime, float maximumAttackTime,
        float minimumHitTime, float hitTime);
    std::string_view attackFollowStrength(float strength);
    int meleeHitType(std::string_view group, std::string_view action);
    bool hasMeleeHitKey(std::string_view group, SceneUtil::TextKeyMap::ConstIterator start,
        const SceneUtil::TextKeyMap& keys);
}

#endif
