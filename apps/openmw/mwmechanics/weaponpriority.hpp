#ifndef OPENMW_WEAPON_PRIORITY_H
#define OPENMW_WEAPON_PRIORITY_H

namespace ESM { struct Weapon; }

namespace MWWorld
{
    class Ptr;
    class ESMStore;
}

namespace MWMechanics
{
    // Shared rating arithmetic. Callers supply actor/target adjustments between
    // base damage and scoring; neither helper queries world or presentation.
    float weaponRatingBaseDamage(const ESM::Weapon& weapon);
    float weaponRatingAdjustedDamage(const ESM::Weapon& weapon, float strength, float normalizedHealth,
        bool hasHealth, const MWWorld::ESMStore& content);
    float weaponRatingScore(const ESM::Weapon& weapon, float adjustedDamage, float hitChance, float multiplier);

    float rateWeapon(const MWWorld::Ptr& item, const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy, int type = -1,
        float arrowRating = 0.f, float boltRating = 0.f);

    float rateAmmo(const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy, MWWorld::Ptr& bestAmmo, int ammoType);
    float rateAmmo(const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy, int ammoType);

    float vanillaRateWeaponAndAmmo(
        const MWWorld::Ptr& weapon, const MWWorld::Ptr& ammo, const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy);
}

#endif
