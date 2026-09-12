#include <tes3mp/combat_world.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <ranges>

namespace
{
    bool finite(float value) noexcept
    {
        return std::isfinite(value);
    }

    constexpr std::array<std::size_t, static_cast<std::size_t>(TES3MP::CombatProgressionSkill::Count)>
        CharacterSkillIndexes{ 0, 20, 5, 4, 6, 7, 26, 21, 2, 3, 17 };

    float combatSkillValue(
        const TES3MP::CanonicalPlayerCombatState& player, TES3MP::CombatProgressionSkill skill) noexcept
    {
        using Skill = TES3MP::CombatProgressionSkill;
        switch (skill)
        {
            case Skill::Block:
                return player.blockSkill;
            case Skill::ShortBlade:
                return player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::ShortBlade)];
            case Skill::LongBlade:
                return player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::LongBlade)];
            case Skill::BluntWeapon:
                return player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::BluntWeapon)];
            case Skill::Axe:
                return player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::Axe)];
            case Skill::Spear:
                return player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::Spear)];
            case Skill::HandToHand:
                return player.stats.handToHandSkill;
            case Skill::LightArmor:
                return player.armorSkills[0];
            case Skill::MediumArmor:
                return player.armorSkills[1];
            case Skill::HeavyArmor:
                return player.armorSkills[2];
            case Skill::Unarmored:
                return player.armorSkills[3];
            case Skill::Count:
                break;
        }
        return 0.f;
    }

    void setCombatSkillValue(
        TES3MP::CanonicalPlayerCombatState& player, TES3MP::CombatProgressionSkill skill, float value) noexcept
    {
        using Skill = TES3MP::CombatProgressionSkill;
        switch (skill)
        {
            case Skill::Block:
                player.blockSkill = value;
                break;
            case Skill::ShortBlade:
                player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::ShortBlade)] = value;
                break;
            case Skill::LongBlade:
                player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::LongBlade)] = value;
                break;
            case Skill::BluntWeapon:
                player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::BluntWeapon)] = value;
                break;
            case Skill::Axe:
                player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::Axe)] = value;
                break;
            case Skill::Spear:
                player.weaponSkills[static_cast<std::size_t>(TES3MP::MeleeWeaponSkill::Spear)] = value;
                break;
            case Skill::HandToHand:
                player.stats.handToHandSkill = value;
                break;
            case Skill::LightArmor:
                player.armorSkills[0] = value;
                break;
            case Skill::MediumArmor:
                player.armorSkills[1] = value;
                break;
            case Skill::HeavyArmor:
                player.armorSkills[2] = value;
                break;
            case Skill::Unarmored:
                player.armorSkills[3] = value;
                break;
            case Skill::Count:
                break;
        }
    }

    TES3MP::CombatProgressionSkill progressionSkill(TES3MP::MeleeWeaponSkill skill) noexcept
    {
        using Progression = TES3MP::CombatProgressionSkill;
        switch (skill)
        {
            case TES3MP::MeleeWeaponSkill::ShortBlade:
                return Progression::ShortBlade;
            case TES3MP::MeleeWeaponSkill::LongBlade:
                return Progression::LongBlade;
            case TES3MP::MeleeWeaponSkill::BluntWeapon:
                return Progression::BluntWeapon;
            case TES3MP::MeleeWeaponSkill::Axe:
                return Progression::Axe;
            case TES3MP::MeleeWeaponSkill::Spear:
                return Progression::Spear;
            case TES3MP::MeleeWeaponSkill::Count:
                break;
        }
        return Progression::HandToHand;
    }

    bool advanceCombatSkill(TES3MP::CanonicalPlayerCombatState& player, TES3MP::CombatProgressionSkill skill) noexcept
    {
        const auto index = static_cast<std::size_t>(skill);
        if (index >= player.skillRules.size())
            return false;
        const auto& rule = player.skillRules[index];
        auto& state = player.skillProgression[index];
        const float value = combatSkillValue(player, skill);
        if (!finite(value) || value >= 100.f || !finite(rule.useGain) || rule.useGain <= 0.f || !finite(state.progress)
            || state.progress < 0.f || !finite(state.requirementFactor) || state.requirementFactor <= 0.f)
            return false;
        const float requirement = (value + 1.f) * state.requirementFactor;
        const float gained = rule.useGain / requirement;
        if (!finite(gained) || gained <= 0.f)
            return false;
        state.progress += gained;
        if (state.progress >= 1.f)
        {
            setCombatSkillValue(player, skill, std::min(100.f, value + 1.f));
            state.progress = 0.f;
        }
        return true;
    }

    float normalizedEncumbrance(std::uint64_t weight, std::uint64_t maximum) noexcept
    {
        return maximum == 0 ? 0.f : static_cast<float>(static_cast<double>(weight) / static_cast<double>(maximum));
    }

    bool validProfile(const TES3MP::MeleeWeaponProfile& value) noexcept
    {
        const auto skill = static_cast<std::uint8_t>(value.skill);
        return skill < static_cast<std::uint8_t>(TES3MP::MeleeWeaponSkill::Count) && finite(value.chopMinimum)
            && finite(value.chopMaximum) && value.chopMinimum >= 0.f && value.chopMinimum <= value.chopMaximum
            && finite(value.slashMinimum) && finite(value.slashMaximum) && value.slashMinimum >= 0.f
            && value.slashMinimum <= value.slashMaximum && finite(value.thrustMinimum) && finite(value.thrustMaximum)
            && value.thrustMinimum >= 0.f && value.thrustMinimum <= value.thrustMaximum && finite(value.weight)
            && value.weight >= 0.f && finite(value.reach) && value.reach > 0.f;
    }

    bool validProfile(const TES3MP::MeleeArmorProfile& value) noexcept
    {
        return static_cast<std::uint8_t>(value.skill) <= static_cast<std::uint8_t>(TES3MP::ArmorSkill::HeavyArmor)
            && finite(value.baseArmor) && value.baseArmor >= 0.f;
    }

    bool valid(const TES3MP::CanonicalPlayerCombatState& value) noexcept
    {
        const auto& s = value.stats;
        const auto validVictim = [](const TES3MP::OpenMwMeleeVictim& victim) {
            return finite(victim.health) && finite(victim.fatigue) && finite(victim.evasion) && finite(victim.chameleon)
                && finite(victim.invisibility) && finite(victim.normalWeaponResistance)
                && finite(victim.normalWeaponWeakness);
        };
        if (!(finite(s.agility) && finite(s.luck) && finite(s.strength) && finite(s.fatigueTerm)
                && finite(s.normalizedEncumbrance) && finite(s.fortifyAttack) && finite(s.blind)
                && finite(s.weaponSkill) && finite(s.handToHandSkill) && finite(s.fatigue) && finite(s.endurance))
            || value.maximumEncumbranceWeightUnits == 0 || !finite(value.blockSkill)
            || !std::ranges::all_of(value.weaponSkills, [](float skill) { return finite(skill); })
            || !std::ranges::all_of(value.armorSkills, [](float skill) { return finite(skill); })
            || !validVictim(value.victim) || !validVictim(value.respawnVictim) || !finite(value.maximumHealth)
            || value.maximumHealth <= 0.f || !finite(value.maximumFatigue) || value.maximumFatigue < 0.f
            || !finite(value.magicka) || !finite(value.maximumMagicka) || value.maximumMagicka < 0.f
            || value.magicka < 0.f || value.magicka > value.maximumMagicka || !finite(value.healthRecoveryPerSecond)
            || value.healthRecoveryPerSecond < 0.f || !finite(value.magickaRecoveryPerSecond)
            || value.magickaRecoveryPerSecond < 0.f || !TES3MP::validDirectMagicDefense(value.magicDefense)
            || value.contractedDiseases.size() > TES3MP::MaximumContractedDiseasesPerPlayer
            || !std::ranges::is_sorted(value.contractedDiseases)
            || std::ranges::adjacent_find(value.contractedDiseases) != value.contractedDiseases.end())
            return false;
        for (std::size_t index = 0; index < value.skillRules.size(); ++index)
        {
            const auto specialization = static_cast<std::uint8_t>(value.skillRules[index].specialization);
            if (specialization > static_cast<std::uint8_t>(TES3MP::ClassSpecialization::Stealth)
                || !finite(value.skillRules[index].useGain) || value.skillRules[index].useGain < 0.f
                || !finite(value.skillProgression[index].progress) || value.skillProgression[index].progress < 0.f
                || value.skillProgression[index].progress >= 1.f
                || !finite(value.skillProgression[index].requirementFactor)
                || value.skillProgression[index].requirementFactor <= 0.f)
                return false;
        }
        return true;
    }

    bool valid(const TES3MP::CanonicalActorCombatState& value) noexcept
    {
        const auto& s = value.stats;
        const auto& spawn = value.respawnStats;
        const auto& attacker = value.attacker;
        const bool victimValid = finite(s.health) && finite(s.fatigue) && finite(s.evasion) && finite(s.chameleon)
            && finite(s.invisibility) && finite(s.normalWeaponResistance) && finite(s.normalWeaponWeakness);
        const bool spawnValid = finite(spawn.health) && finite(spawn.fatigue) && finite(spawn.evasion)
            && finite(spawn.chameleon) && finite(spawn.invisibility) && finite(spawn.normalWeaponResistance)
            && finite(spawn.normalWeaponWeakness);
        const bool attackerValid = finite(attacker.agility) && finite(attacker.luck) && finite(attacker.strength)
            && finite(attacker.fatigueTerm) && finite(attacker.normalizedEncumbrance) && finite(attacker.fortifyAttack)
            && finite(attacker.blind) && finite(attacker.weaponSkill) && finite(attacker.handToHandSkill)
            && finite(attacker.fatigue) && finite(attacker.endurance);
        return victimValid && spawnValid && attackerValid && TES3MP::validDirectMagicDefense(value.magicDefense)
            && finite(value.maximumHealth) && value.maximumHealth >= 0.f && finite(value.maximumFatigue)
            && value.maximumFatigue >= 0.f && (value.stats.dead || value.maximumHealth > 0.f)
            && (!value.naturalWeapon
                || validProfile(TES3MP::MeleeWeaponProfile{ *TES3MP::ItemPrototypeId::fromValue(1),
                    TES3MP::MeleeWeaponSkill::ShortBlade, value.naturalWeapon->chopMinimum,
                    value.naturalWeapon->chopMaximum, value.naturalWeapon->slashMinimum,
                    value.naturalWeapon->slashMaximum, value.naturalWeapon->thrustMinimum,
                    value.naturalWeapon->thrustMaximum, value.naturalWeapon->weight, 1.f,
                    value.naturalWeapon->normalWeapon }))
            && value.attackReachQuanta <= (1u << 30);
    }

    std::uint64_t distanceSquared(
        const TES3MP::Position3& lhs, const TES3MP::Position3& rhs, std::uint32_t maximum) noexcept
    {
        const auto delta = [](std::int64_t first, std::int64_t second) -> std::optional<std::uint64_t> {
            if ((first < 0) == (second < 0))
                return first < second ? static_cast<std::uint64_t>(second - first)
                                      : static_cast<std::uint64_t>(first - second);
            const auto magnitude = [](std::int64_t value) {
                return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1 : static_cast<std::uint64_t>(value);
            };
            const auto a = magnitude(first);
            const auto b = magnitude(second);
            if (a > std::numeric_limits<std::uint64_t>::max() - b)
                return std::nullopt;
            return a + b;
        };
        const auto x = delta(lhs.x(), rhs.x());
        const auto y = delta(lhs.y(), rhs.y());
        const auto z = delta(lhs.z(), rhs.z());
        if (!x || !y || !z || *x > maximum || *y > maximum || *z > maximum)
            return std::numeric_limits<std::uint64_t>::max();
        return *x * *x + *y * *y + *z * *z;
    }

    bool insideBlockArc(const TES3MP::Transform& blocker, const TES3MP::Position3& attacker,
        const TES3MP::OpenMwMeleeSettings& settings) noexcept
    {
        const auto origin = blocker.position();
        const double x = static_cast<double>(attacker.x()) - static_cast<double>(origin.x());
        const double y = static_cast<double>(attacker.y()) - static_cast<double>(origin.y());
        if (x == 0.0 && y == 0.0)
            return true;
        constexpr double TurnScale = 2.0 * std::numbers::pi / 4294967296.0;
        const double yaw = static_cast<double>(blocker.orientation().z().value()) * TurnScale;
        const double forwardX = std::sin(yaw);
        const double forwardY = std::cos(yaw);
        const double angle
            = std::atan2(x * forwardY - y * forwardX, forwardX * x + forwardY * y) * 180.0 / std::numbers::pi;
        return angle >= settings.combatBlockLeftAngle && angle <= settings.combatBlockRightAngle;
    }

    bool receivesBlockStillBonus(const TES3MP::Transform& blocker, const TES3MP::LinearVelocity3& velocity) noexcept
    {
        constexpr double TurnScale = 2.0 * std::numbers::pi / 4294967296.0;
        const double yaw = static_cast<double>(blocker.orientation().z().value()) * TurnScale;
        const double forwardSpeed
            = static_cast<double>(velocity.x()) * std::sin(yaw) + static_cast<double>(velocity.y()) * std::cos(yaw);
        return forwardSpeed <= 0.0;
    }

    constexpr std::uint32_t ArmorSlotMask = TES3MP::slotToMask(TES3MP::EquipmentSlot::Helmet)
        | TES3MP::slotToMask(TES3MP::EquipmentSlot::Cuirass) | TES3MP::slotToMask(TES3MP::EquipmentSlot::Greaves)
        | TES3MP::slotToMask(TES3MP::EquipmentSlot::LeftPauldron)
        | TES3MP::slotToMask(TES3MP::EquipmentSlot::RightPauldron)
        | TES3MP::slotToMask(TES3MP::EquipmentSlot::LeftGauntlet)
        | TES3MP::slotToMask(TES3MP::EquipmentSlot::RightGauntlet) | TES3MP::slotToMask(TES3MP::EquipmentSlot::Boots)
        | TES3MP::slotToMask(TES3MP::EquipmentSlot::CarriedLeft);

    TES3MP::CombatProgressionSkill progressionSkill(TES3MP::ArmorSkill skill) noexcept
    {
        switch (skill)
        {
            case TES3MP::ArmorSkill::LightArmor:
                return TES3MP::CombatProgressionSkill::LightArmor;
            case TES3MP::ArmorSkill::MediumArmor:
                return TES3MP::CombatProgressionSkill::MediumArmor;
            case TES3MP::ArmorSkill::HeavyArmor:
                return TES3MP::CombatProgressionSkill::HeavyArmor;
        }
        return TES3MP::CombatProgressionSkill::Unarmored;
    }

    const TES3MP::CanonicalItemStack* equippedArmor(const TES3MP::CanonicalPlayerInventoryState* inventory,
        TES3MP::EquipmentSlot slot, const TES3MP::ItemPrototypeCatalog& items,
        const TES3MP::MeleeWeaponCatalog& weapons) noexcept
    {
        if (!inventory)
            return nullptr;
        const auto stackId = inventory->equipment[static_cast<std::size_t>(slot)];
        const auto* stack = stackId ? inventory->findStack(*stackId) : nullptr;
        const auto* item = stack ? items.find(stack->prototypeId) : nullptr;
        return stack && item && item->category == TES3MP::ItemCategory::Armor && stack->condition > 0
                && weapons.findArmor(stack->prototypeId)
            ? stack
            : nullptr;
    }

    float armorRating(const TES3MP::CanonicalPlayerCombatState& player,
        const TES3MP::CanonicalPlayerInventoryState* inventory, const TES3MP::ItemPrototypeCatalog& items,
        const TES3MP::MeleeWeaponCatalog& weapons, const TES3MP::OpenMwMeleeSettings& settings) noexcept
    {
        constexpr std::array slots{
            std::pair{ TES3MP::EquipmentSlot::Cuirass, 0.30f },
            std::pair{ TES3MP::EquipmentSlot::CarriedLeft, 0.10f },
            std::pair{ TES3MP::EquipmentSlot::Helmet, 0.10f },
            std::pair{ TES3MP::EquipmentSlot::Greaves, 0.10f },
            std::pair{ TES3MP::EquipmentSlot::Boots, 0.10f },
            std::pair{ TES3MP::EquipmentSlot::LeftPauldron, 0.10f },
            std::pair{ TES3MP::EquipmentSlot::RightPauldron, 0.10f },
            std::pair{ TES3MP::EquipmentSlot::LeftGauntlet, 0.05f },
            std::pair{ TES3MP::EquipmentSlot::RightGauntlet, 0.05f },
        };
        const float unarmored = TES3MP::openMwUnarmoredRating(settings, player.armorSkills[3]);
        float total = 0.f;
        for (const auto& [slot, weight] : slots)
        {
            float rating = unarmored;
            if (const auto* stack = equippedArmor(inventory, slot, items, weapons))
            {
                const auto* item = items.find(stack->prototypeId);
                const auto* armor = weapons.findArmor(stack->prototypeId);
                const float condition = item->maxCondition == 0
                    ? 1.f
                    : static_cast<float>(stack->condition) / static_cast<float>(item->maxCondition);
                rating = item->weightUnits == 0
                    ? armor->baseArmor * condition
                    : TES3MP::openMwSkillAdjustedArmorRating(settings, armor->baseArmor,
                        player.armorSkills[static_cast<std::size_t>(armor->skill)], condition);
            }
            total += rating * weight;
        }
        return total;
    }

    TES3MP::EquipmentSlot armorHitSlot(std::uint8_t roll, bool redistributeShield) noexcept
    {
        if (roll >= 90)
        {
            if (redistributeShield)
                return roll >= 95 ? TES3MP::EquipmentSlot::Cuirass : TES3MP::EquipmentSlot::LeftPauldron;
            return TES3MP::EquipmentSlot::CarriedLeft;
        }
        if (roll >= 85)
            return TES3MP::EquipmentSlot::RightGauntlet;
        if (roll >= 80)
            return TES3MP::EquipmentSlot::LeftGauntlet;
        if (roll >= 70)
            return TES3MP::EquipmentSlot::RightPauldron;
        if (roll >= 60)
            return TES3MP::EquipmentSlot::LeftPauldron;
        if (roll >= 50)
            return TES3MP::EquipmentSlot::Boots;
        if (roll >= 40)
            return TES3MP::EquipmentSlot::Greaves;
        if (roll >= 30)
            return TES3MP::EquipmentSlot::Helmet;
        return TES3MP::EquipmentSlot::Cuirass;
    }

    void applyDirectMagic(TES3MP::CanonicalPlayerCombatState& target, TES3MP::DirectMagicResolution resolution,
        TES3MP::ServerTick tick) noexcept
    {
        target.victim.health = std::max(0.f, target.victim.health - resolution.healthDamage);
        target.victim.fatigue -= resolution.fatigueDamage;
        target.victim.fatigueNonNegative = target.victim.fatigue >= 0.f;
        target.stats.fatigue = target.victim.fatigue;
        if (target.victim.health < 1.f)
        {
            target.victim.health = 0.f;
            target.victim.dead = true;
            if (!target.deathTick)
                target.deathTick = tick;
        }
    }

    void applyDirectMagic(TES3MP::CanonicalActorCombatState& target, TES3MP::DirectMagicResolution resolution,
        TES3MP::ServerTick tick) noexcept
    {
        target.stats.health = std::max(0.f, target.stats.health - resolution.healthDamage);
        target.stats.fatigue -= resolution.fatigueDamage;
        target.stats.fatigueNonNegative = target.stats.fatigue >= 0.f;
        target.attacker.fatigue = target.stats.fatigue;
        if (target.stats.health < 1.f)
        {
            target.stats.health = 0.f;
            target.stats.dead = true;
            if (!target.deathTick)
                target.deathTick = tick;
        }
    }

    bool addContractedDisease(TES3MP::CanonicalPlayerCombatState& target, TES3MP::SpellRecordId disease)
    {
        const auto found = std::ranges::lower_bound(target.contractedDiseases, disease);
        if (found != target.contractedDiseases.end() && *found == disease)
            return true;
        if (target.contractedDiseases.size() >= TES3MP::MaximumContractedDiseasesPerPlayer)
            return false;
        target.contractedDiseases.insert(found, disease);
        return true;
    }
}
namespace TES3MP
{
    std::optional<CanonicalPlayerCombatTemplate> deriveCharacterCombatTemplate(const CharacterProfile& profile,
        const CanonicalPlayerCombatTemplate& base, const CharacterContentCatalog* characterContent) noexcept
    {
        if (profile.lifecycle() != CharacterLifecycle::EstablishedCharacter || !finite(base.stats.strength)
            || base.stats.strength <= 0.f || !finite(base.stats.endurance)
            || (base.healthRecoveryPerSecond > 0.f && base.stats.endurance <= 0.f) || !finite(base.intelligence)
            || ((base.maximumMagicka > 0.f || base.magickaRecoveryPerSecond > 0.f) && base.intelligence <= 0.f)
            || base.maximumEncumbranceWeightUnits == 0 || !profile.characterClass())
            return std::nullopt;
        std::optional<CharacterClassDefinition> ownedClass;
        const CharacterClassDefinition* selectedClass = nullptr;
        if (const auto* predefined = std::get_if<ClassRecordId>(&*profile.characterClass()))
        {
            if (!characterContent)
                return std::nullopt;
            selectedClass = characterContent->find(*predefined);
        }
        else if (const auto* custom = std::get_if<CustomClassDefinition>(&*profile.characterClass()))
        {
            ownedClass = CharacterClassDefinition{ *ClassRecordId::fromValue(1), custom->specialization,
                custom->favoredAttributes, custom->minorSkills, custom->majorSkills };
            selectedClass = &*ownedClass;
        }
        if (!selectedClass)
            return std::nullopt;
        const auto& attributes = profile.derived().attributes;
        const auto& skills = profile.derived().skills;
        CanonicalPlayerCombatTemplate result = base;
        result.stats.strength = static_cast<float>(attributes[0]);
        result.stats.agility = static_cast<float>(attributes[3]);
        result.stats.luck = static_cast<float>(attributes[7]);
        result.stats.endurance = static_cast<float>(attributes[5]);
        result.stats.fatigueTerm = 1.f;
        result.stats.handToHandSkill = static_cast<float>(skills[26]);
        result.blockSkill = static_cast<float>(skills[0]);
        result.stats.fatigue = static_cast<float>(attributes[0]) + static_cast<float>(attributes[2])
            + static_cast<float>(attributes[3]) + static_cast<float>(attributes[5]);
        result.victim.health = (static_cast<float>(attributes[0]) + static_cast<float>(attributes[5])) * 0.5f;
        result.victim.fatigue = result.stats.fatigue;
        result.victim.evasion = (result.stats.agility / 5.f + result.stats.luck / 10.f) * result.stats.fatigueTerm;
        result.victim.dead = false;
        result.maximumHealth = result.victim.health;
        result.maximumFatigue = result.victim.fatigue;
        result.maximumMagicka = base.maximumMagicka > 0.f
            ? base.maximumMagicka * static_cast<float>(attributes[1]) / base.intelligence
            : 0.f;
        result.magicka = result.maximumMagicka;
        result.healthRecoveryPerSecond = base.healthRecoveryPerSecond > 0.f
            ? base.healthRecoveryPerSecond * result.stats.endurance / base.stats.endurance
            : 0.f;
        result.magickaRecoveryPerSecond = base.magickaRecoveryPerSecond > 0.f
            ? base.magickaRecoveryPerSecond * static_cast<float>(attributes[1]) / base.intelligence
            : 0.f;
        result.intelligence = static_cast<float>(attributes[1]);
        result.magicDefense.willpower = static_cast<float>(attributes[2]);
        result.magicDefense.destructionSkill = static_cast<float>(skills[10]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::ShortBlade)] = static_cast<float>(skills[20]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::LongBlade)] = static_cast<float>(skills[5]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::BluntWeapon)] = static_cast<float>(skills[4]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::Axe)] = static_cast<float>(skills[6]);
        result.weaponSkills[static_cast<std::size_t>(MeleeWeaponSkill::Spear)] = static_cast<float>(skills[7]);
        result.armorSkills = { static_cast<float>(skills[21]), static_cast<float>(skills[2]),
            static_cast<float>(skills[3]), static_cast<float>(skills[17]) };
        for (std::size_t index = 0; index < result.skillProgression.size(); ++index)
        {
            const auto characterSkill = static_cast<std::uint8_t>(CharacterSkillIndexes[index]);
            float factor = result.skillSettings.miscellaneousFactor;
            if (std::ranges::find(selectedClass->minorSkills, characterSkill) != selectedClass->minorSkills.end())
                factor = result.skillSettings.minorFactor;
            else if (std::ranges::find(selectedClass->majorSkills, characterSkill) != selectedClass->majorSkills.end())
                factor = result.skillSettings.majorFactor;
            if (result.skillRules[index].specialization == selectedClass->specialization)
                factor *= result.skillSettings.specializationFactor;
            if (!finite(factor) || factor <= 0.f)
                return std::nullopt;
            result.skillProgression[index] = { 0.f, factor };
        }
        const auto scaledEncumbrance = static_cast<long double>(base.maximumEncumbranceWeightUnits)
            * static_cast<long double>(attributes[0]) / static_cast<long double>(base.stats.strength);
        if (scaledEncumbrance < 1.L
            || scaledEncumbrance > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
            return std::nullopt;
        result.maximumEncumbranceWeightUnits = static_cast<std::uint64_t>(scaledEncumbrance);
        return result;
    }

    std::optional<MeleeWeaponCatalog> MeleeWeaponCatalog::create(const ItemPrototypeCatalog& items,
        std::span<const MeleeWeaponProfile> profiles, std::span<const MeleeArmorProfile> armor) noexcept
    try
    {
        if (profiles.size() > MaximumItemPrototypes)
            return std::nullopt;
        std::vector<MeleeWeaponProfile> values(profiles.begin(), profiles.end());
        std::ranges::sort(values, {}, &MeleeWeaponProfile::prototypeId);
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            const auto* item = items.find(values[index].prototypeId);
            if (!validProfile(values[index]) || !item || item->category != ItemCategory::Weapon || item->stackable
                || (item->slotMask & slotToMask(EquipmentSlot::CarriedRight)) == 0
                || item->maxCondition > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                || (index != 0 && values[index - 1].prototypeId == values[index].prototypeId))
                return std::nullopt;
        }
        if (armor.size() > MaximumItemPrototypes)
            return std::nullopt;
        std::vector<MeleeArmorProfile> armorValues(armor.begin(), armor.end());
        std::ranges::sort(armorValues, {}, &MeleeArmorProfile::prototypeId);
        for (std::size_t index = 0; index < armorValues.size(); ++index)
        {
            const auto* item = items.find(armorValues[index].prototypeId);
            if (!validProfile(armorValues[index]) || !item || item->category != ItemCategory::Armor || item->stackable
                || item->maxCondition == 0
                || item->maxCondition > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                || (item->slotMask & ArmorSlotMask) == 0
                || (index != 0 && armorValues[index - 1].prototypeId == armorValues[index].prototypeId))
                return std::nullopt;
        }
        for (const auto& item : items.declarations())
        {
            if (item.category != ItemCategory::Armor)
                continue;
            const auto found = std::ranges::lower_bound(armorValues, item.id, {}, &MeleeArmorProfile::prototypeId);
            if (found == armorValues.end() || found->prototypeId != item.id)
                return std::nullopt;
        }
        return MeleeWeaponCatalog(items.contentManifestId(), std::move(values), std::move(armorValues));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const MeleeWeaponProfile* MeleeWeaponCatalog::find(ItemPrototypeId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mProfiles, id, {}, &MeleeWeaponProfile::prototypeId);
        return found != mProfiles.end() && found->prototypeId == id ? &*found : nullptr;
    }

    const MeleeArmorProfile* MeleeWeaponCatalog::findArmor(ItemPrototypeId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mArmor, id, {}, &MeleeArmorProfile::prototypeId);
        return found != mArmor.end() && found->prototypeId == id ? &*found : nullptr;
    }

    const CanonicalPlayerCombatState* CanonicalCombatWorld::findPlayer(PlayerId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        return found != mPlayers.end() && found->playerId == id ? &*found : nullptr;
    }

    const CanonicalActorCombatState* CanonicalCombatWorld::findActor(ActorId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mActors, id, {}, &CanonicalActorCombatState::actorId);
        return found != mActors.end() && found->actorId == id ? &*found : nullptr;
    }

    bool CanonicalCombatWorld::ensurePlayer(
        PlayerId id, const CanonicalPlayerCombatTemplate& source, std::uint64_t inventoryWeightUnits) noexcept
    try
    {
        if (findPlayer(id))
            return true;
        if (mPlayers.size() >= MaximumPlayerCombatants || source.maximumEncumbranceWeightUnits == 0
            || !finite(source.maximumHealth) || source.maximumHealth <= 0.f || !finite(source.maximumFatigue)
            || source.maximumFatigue < 0.f || !finite(source.magicka) || !finite(source.maximumMagicka)
            || source.maximumMagicka < 0.f || source.magicka < 0.f || source.magicka > source.maximumMagicka)
            return false;
        CanonicalPlayerCombatState value{ id, CombatRevision::initial(), source.stats, source.weaponSkills,
            source.blockSkill, source.maximumEncumbranceWeightUnits, std::nullopt, std::nullopt, source.victim,
            source.victim, std::nullopt, source.maximumHealth, source.maximumFatigue };
        value.magicka = source.magicka;
        value.maximumMagicka = source.maximumMagicka;
        value.healthRecoveryPerSecond = source.healthRecoveryPerSecond;
        value.magickaRecoveryPerSecond = source.magickaRecoveryPerSecond;
        value.skillRules = source.skillRules;
        value.skillProgression = source.skillProgression;
        value.armorSkills = source.armorSkills;
        value.magicDefense = source.magicDefense;
        value.stats.weaponSkill = 0.f;
        value.stats.normalizedEncumbrance
            = normalizedEncumbrance(inventoryWeightUnits, source.maximumEncumbranceWeightUnits);
        if (!valid(value))
            return false;
        const auto position = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        mPlayers.insert(position, std::move(value));
        return true;
    }
    catch (...)
    {
        return false;
    }

    bool CanonicalCombatWorld::initializePlayerFromCharacter(PlayerId id, const CanonicalPlayerCombatTemplate& source,
        std::uint64_t inventoryWeightUnits, CharacterProfileRevision profileRevision) noexcept
    try
    {
        auto found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        if (found == mPlayers.end() || found->playerId != id)
        {
            if (!ensurePlayer(id, source, inventoryWeightUnits))
                return false;
            found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
            found->initializedCharacterProfile = profileRevision;
            return true;
        }
        if (found->initializedCharacterProfile)
            return *found->initializedCharacterProfile == profileRevision;
        const auto revision = found->revision.next();
        if (!revision || source.maximumEncumbranceWeightUnits == 0)
            return false;
        CanonicalPlayerCombatState value{ id, *revision, source.stats, source.weaponSkills, source.blockSkill,
            source.maximumEncumbranceWeightUnits, std::nullopt, profileRevision, source.victim, source.victim,
            std::nullopt, source.maximumHealth, source.maximumFatigue };
        value.magicka = source.magicka;
        value.maximumMagicka = source.maximumMagicka;
        value.healthRecoveryPerSecond = source.healthRecoveryPerSecond;
        value.magickaRecoveryPerSecond = source.magickaRecoveryPerSecond;
        value.skillRules = source.skillRules;
        value.skillProgression = source.skillProgression;
        value.armorSkills = source.armorSkills;
        value.magicDefense = source.magicDefense;
        value.stats.weaponSkill = 0.f;
        value.stats.normalizedEncumbrance
            = normalizedEncumbrance(inventoryWeightUnits, source.maximumEncumbranceWeightUnits);
        if (!valid(value))
            return false;
        *found = std::move(value);
        return true;
    }
    catch (...)
    {
        return false;
    }

    bool CanonicalCombatWorld::advancePlayerInventoryBinding(PlayerId id, std::uint64_t inventoryWeightUnits) noexcept
    {
        auto found = std::ranges::lower_bound(mPlayers, id, {}, &CanonicalPlayerCombatState::playerId);
        if (found == mPlayers.end() || found->playerId != id)
            return false;
        const auto revision = found->revision.next();
        if (!revision)
            return false;
        found->stats.normalizedEncumbrance
            = normalizedEncumbrance(inventoryWeightUnits, found->maximumEncumbranceWeightUnits);
        found->revision = *revision;
        return valid(*found);
    }

    std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
        std::span<const CanonicalPlayerCombatState> players, std::span<const CanonicalActorCombatState> actors,
        RandomStateV1 randomState, std::optional<ServerTick> lastSimulationTick)
    {
        if (players.size() > MaximumPlayerCombatants)
            return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::PlayerLimitExceeded };
        if (actors.size() > MaximumActorCombatants)
            return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::ActorLimitExceeded };
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            if (!valid(players[i]))
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::InvalidStat, i };
            if (i && players[i - 1].playerId >= players[i].playerId)
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::PlayersNotStrictlySorted, i };
        }
        for (std::size_t i = 0; i < actors.size(); ++i)
        {
            if (!valid(actors[i]))
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::InvalidStat, i };
            if (i && actors[i - 1].actorId >= actors[i].actorId)
                return CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::ActorsNotStrictlySorted, i };
        }
        return CanonicalCombatWorld(std::vector(players.begin(), players.end()),
            std::vector(actors.begin(), actors.end()), randomState, lastSimulationTick);
    }

    PreparedMeleeAttack prepareAuthoritativeMeleeAttack(const CanonicalCombatWorld& combat,
        const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& items, const MeleeWeaponCatalog& weapons,
        const CanonicalServerState& players, const CanonicalActorWorld& actors, const OpenMwMeleeSettings& settings,
        MeleeAuthorityPolicy policy, ServerMeleeContactQuery& contact, ServerTick serverTick,
        const AuthoritativeMeleeAttack& attack, const DirectMagicCatalog* magic) noexcept
    try
    {
        PreparedMeleeAttack out;
        const auto* attackerCombat = combat.findPlayer(attack.attacker);
        if (!attackerCombat)
        {
            out.disposition = AuthoritativeMeleeDisposition::UnknownAttacker;
            return out;
        }
        if (attackerCombat->revision != attack.expectedAttackerRevision)
        {
            out.disposition = AuthoritativeMeleeDisposition::StaleAttackerRevision;
            return out;
        }
        if (attackerCombat->victim.dead)
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        const auto* targetCombat = attack.target ? combat.findActor(*attack.target) : nullptr;
        if (attack.target && !targetCombat)
        {
            out.disposition = AuthoritativeMeleeDisposition::UnknownTarget;
            return out;
        }
        if (targetCombat && targetCombat->revision != attack.expectedTargetRevision)
        {
            out.disposition = AuthoritativeMeleeDisposition::StaleTargetRevision;
            return out;
        }
        const auto* playerSpatial = players.findPlayer(attack.attacker);
        const auto* actorSpatial = attack.target ? actors.find(*attack.target) : nullptr;
        if (!playerSpatial || (attack.target && !actorSpatial))
        {
            out.disposition = AuthoritativeMeleeDisposition::SpatialIdentityMismatch;
            return out;
        }
        if (actorSpatial && playerSpatial->transform().cell() != actorSpatial->root().cell())
        {
            out.disposition = AuthoritativeMeleeDisposition::DifferentCell;
            return out;
        }
        if (attack.sourceTick > serverTick)
        {
            out.disposition = AuthoritativeMeleeDisposition::FutureSourceTick;
            return out;
        }
        if (serverTick.value() - attack.sourceTick.value() > policy.maximumRewindTicks)
        {
            out.disposition = AuthoritativeMeleeDisposition::RewindWindowExceeded;
            return out;
        }
        if (attackerCombat->lastAttackTick
            && serverTick.value() - attackerCombat->lastAttackTick->value() < policy.minimumAttackIntervalTicks)
        {
            out.disposition = AuthoritativeMeleeDisposition::RateLimited;
            return out;
        }
        const auto* attackerInventory = inventory.findPlayer(attack.attacker);
        if (!attackerInventory || inventory.contentManifestId() != items.contentManifestId()
            || inventory.contentManifestId() != weapons.contentManifestId()
            || (magic && inventory.contentManifestId() != magic->contentManifestId()))
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        std::optional<ItemStackId> equippedStackId;
        std::optional<ItemPrototypeId> equippedPrototypeId;
        std::optional<OpenMwMeleeWeapon> equippedWeapon;
        std::optional<float> equippedWeaponReach;
        CombatProgressionSkill usedSkill = CombatProgressionSkill::HandToHand;
        OpenMwMeleeAttacker resolvedAttacker = attackerCombat->stats;
        resolvedAttacker.fatigueTerm
            = openMwFatigueTerm(settings, attackerCombat->stats.fatigue, attackerCombat->maximumFatigue);
        const auto carriedRight = static_cast<std::size_t>(EquipmentSlot::CarriedRight);
        if (attackerInventory->equipment[carriedRight])
        {
            equippedStackId = attackerInventory->equipment[carriedRight];
            const auto* stack = attackerInventory->findStack(*equippedStackId);
            const auto* item = stack ? items.find(stack->prototypeId) : nullptr;
            const auto* profile = stack ? weapons.find(stack->prototypeId) : nullptr;
            if (!stack || !item || !profile || item->category != ItemCategory::Weapon
                || stack->condition > item->maxCondition || (item->maxCondition != 0 && stack->condition == 0))
            {
                out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
                return out;
            }
            OpenMwMeleeWeapon weapon{ profile->chopMinimum, profile->chopMaximum, profile->slashMinimum,
                profile->slashMaximum, profile->thrustMinimum, profile->thrustMaximum, profile->weight, 1.f,
                static_cast<std::int32_t>(stack->condition), item->maxCondition != 0, profile->normalWeapon };
            if (weapon.hasCondition)
                weapon.normalizedCondition
                    = static_cast<float>(stack->condition) / static_cast<float>(item->maxCondition);
            equippedWeapon = weapon;
            equippedPrototypeId = stack->prototypeId;
            equippedWeaponReach = profile->reach;
            resolvedAttacker.weaponSkill = attackerCombat->weaponSkills[static_cast<std::size_t>(profile->skill)];
            usedSkill = progressionSkill(profile->skill);
        }
        else
            resolvedAttacker.weaponSkill = 0.f;
        if (!finite(attack.attackStrength) || attack.attackStrength < 0.f || attack.attackStrength > 1.f
            || !finite(attack.werewolfClawMultiplier))
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        if (actorSpatial)
        {
            const auto contactResult
                = contact.validate(ServerMeleeContactRequest{ attack.attacker, *attack.target, attack.sourceTick,
                                       attack.attackType, equippedWeapon, equippedWeaponReach },
                *playerSpatial, *actorSpatial);
            if (contactResult != MeleeContactValidation::Accepted)
            {
                out.disposition = contactResult == MeleeContactValidation::NoContact
                    ? AuthoritativeMeleeDisposition::NoContact
                    : AuthoritativeMeleeDisposition::HistoryUnavailable;
                return out;
            }
        }

        auto attackerRevision = attackerCombat->revision.next();
        auto targetRevision = targetCombat ? targetCombat->revision.next() : std::optional<CombatRevision>{};
        if (!attackerRevision || (targetCombat && !targetRevision))
        {
            out.disposition = AuthoritativeMeleeDisposition::RevisionExhausted;
            return out;
        }
        std::vector<CanonicalPlayerCombatState> playerStates(combat.players().begin(), combat.players().end());
        std::vector<CanonicalActorCombatState> actorStates(combat.actors().begin(), combat.actors().end());
        CanonicalInventoryWorld inventoryCandidate = inventory;
        auto& mutableAttacker
            = *std::ranges::lower_bound(playerStates, attack.attacker, {}, &CanonicalPlayerCombatState::playerId);
        auto* mutableTarget = targetCombat
            ? &*std::ranges::lower_bound(actorStates, *attack.target, {}, &CanonicalActorCombatState::actorId)
            : nullptr;
        auto random = Xoshiro256StarStar::restore(combat.randomState());
        std::uint8_t roll = 0;
        if (mutableTarget)
        {
            const auto generated = random.uniformBelow(100);
            if (!generated)
            {
                out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
                return out;
            }
            roll = static_cast<std::uint8_t>(*generated);
        }
        OpenMwMeleeAttempt attempt{ .type = attack.attackType,
            .attackStrength = attack.attackStrength,
            .hitRoll0To99 = roll,
            .contact = mutableTarget != nullptr,
            .blocked = false,
            .strengthInfluencesHandToHand = attack.strengthInfluencesHandToHand,
            .werewolfClawMultiplier = attack.werewolfClawMultiplier,
            .weapon = equippedWeapon };
        const OpenMwMeleeVictim emptyVictim{};
        auto resolution = resolveOpenMwMelee(
            settings, resolvedAttacker, mutableTarget ? mutableTarget->stats : emptyVictim, attempt);
        if (resolution.code == OpenMwMeleeResolutionCode::InvalidInput)
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        if (mutableTarget && resolution.hit && !resolution.blocked)
        {
            resolution.damage = openMwDifficultyScaledDamage(settings, resolution.damage, policy.difficulty, false);
            if (resolution.damagedStat == MeleeDamageStat::Health)
            {
                const float remaining = mutableTarget->stats.health - resolution.damage;
                resolution.victimDied = remaining < 1.f;
                resolution.victimHealth = resolution.victimDied ? 0.f : remaining;
            }
            else
                resolution.victimFatigue = mutableTarget->stats.fatigue - resolution.damage;
        }
        DirectMagicResolution attackerMagic;
        DirectMagicResolution targetMagic;
        std::optional<std::uint32_t> remainingEnchantmentCharge;
        if (magic && mutableTarget && resolution.hit)
        {
            const auto attackerDefense
                = combinedDirectMagicDefense(mutableAttacker.magicDefense, attackerInventory, *magic);
            const auto targetDefense = mutableTarget->magicDefense;
            if (equippedPrototypeId)
            {
                const auto* enchantment = magic->findEnchantment(*equippedPrototypeId);
                const auto* stack = equippedStackId ? attackerInventory->findStack(*equippedStackId) : nullptr;
                if (enchantment && stack && stack->enchantmentCharge >= enchantment->chargeCost)
                {
                    const auto self = resolveDirectMagicEffects(
                        enchantment->effects, DirectMagicTarget::Self, attackerDefense, random);
                    const auto other = resolveDirectMagicEffects(
                        enchantment->effects, DirectMagicTarget::Other, targetDefense, random);
                    if (!self || !other)
                    {
                        out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
                        return out;
                    }
                    attackerMagic = *self;
                    targetMagic = *other;
                    remainingEnchantmentCharge = stack->enchantmentCharge - enchantment->chargeCost;
                }
            }
            const auto shieldDamage
                = resolveElementalShieldDamage(targetDefense, attackerDefense, mutableAttacker.stats.luck,
                    mutableAttacker.stats.fatigue, mutableAttacker.maximumFatigue, magic->settings(), random);
            if (!shieldDamage)
            {
                out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
                return out;
            }
            attackerMagic.healthDamage
                = openMwDifficultyScaledDamage(settings, attackerMagic.healthDamage, policy.difficulty, true)
                + openMwDifficultyScaledDamage(settings, *shieldDamage, policy.difficulty, true);
            targetMagic.healthDamage
                = openMwDifficultyScaledDamage(settings, targetMagic.healthDamage, policy.difficulty, false);
        }
        mutableAttacker.stats.fatigue = resolution.attackerFatigue;
        mutableAttacker.victim.fatigue = resolution.attackerFatigue;
        mutableAttacker.victim.fatigueNonNegative = resolution.attackerFatigue >= 0.f;
        mutableAttacker.lastAttackTick = serverTick;
        if (mutableTarget && resolution.hit)
            (void)advanceCombatSkill(mutableAttacker, usedSkill);
        mutableAttacker.revision = *attackerRevision;
        applyDirectMagic(mutableAttacker, attackerMagic, serverTick);
        if (remainingEnchantmentCharge && equippedStackId
            && inventoryCandidate.setEquippedItemEnchantmentCharge(attack.attacker, EquipmentSlot::CarriedRight,
                   *equippedStackId, *remainingEnchantmentCharge, serverTick)
                != EquippedConditionResult::Applied)
        {
            out.disposition = AuthoritativeMeleeDisposition::RevisionExhausted;
            return out;
        }
        if (equippedStackId && equippedWeapon && resolution.weaponCondition != equippedWeapon->condition)
        {
            const auto condition = static_cast<std::uint32_t>(resolution.weaponCondition);
            if (inventoryCandidate.setEquippedItemCondition(attack.attacker, EquipmentSlot::CarriedRight,
                    *equippedStackId, condition, resolution.weaponBroken, serverTick)
                != EquippedConditionResult::Applied)
            {
                out.disposition = AuthoritativeMeleeDisposition::RevisionExhausted;
                return out;
            }
        }
        if (mutableTarget && resolution.code == OpenMwMeleeResolutionCode::Resolved)
        {
            mutableTarget->stats.health = resolution.victimHealth;
            mutableTarget->stats.fatigue = resolution.victimFatigue;
            mutableTarget->stats.fatigueNonNegative = resolution.victimFatigue >= 0.f;
            mutableTarget->attacker.fatigue = resolution.victimFatigue;
            mutableTarget->stats.dead = resolution.victimDied || mutableTarget->stats.dead;
            applyDirectMagic(*mutableTarget, targetMagic, serverTick);
            resolution.victimHealth = mutableTarget->stats.health;
            resolution.victimFatigue = mutableTarget->stats.fatigue;
            resolution.victimDied = mutableTarget->stats.dead;
            mutableTarget->aggressionTarget = attack.attacker;
            if (mutableTarget->stats.dead && !mutableTarget->deathTick)
                mutableTarget->deathTick = serverTick;
            mutableTarget->revision = *targetRevision;
        }
        auto created
            = createCanonicalCombatWorld(playerStates, actorStates, random.snapshot(), combat.lastSimulationTick());
        auto* candidate = std::get_if<CanonicalCombatWorld>(&created);
        if (!candidate)
        {
            out.disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
            return out;
        }
        out.disposition = AuthoritativeMeleeDisposition::Applied;
        if (mutableTarget && resolution.code == OpenMwMeleeResolutionCode::Resolved)
            out.event = AuthoritativeMeleeEvent{ serverTick, attack.attacker, *attack.target, *attackerRevision,
                *targetRevision, resolution };
        out.candidate = std::move(*candidate);
        if (inventoryCandidate != inventory)
            out.candidateInventory = std::move(inventoryCandidate);
        return out;
    }
    catch (...)
    {
        return PreparedMeleeAttack{};
    }

    std::variant<CombatSimulationStep, CombatSimulationError> advanceAuthoritativeCombat(
        const CanonicalCombatWorld& combat, const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& items,
        const MeleeWeaponCatalog& weapons, const CanonicalServerState& players, const CanonicalActorWorld& actors,
        const OpenMwMeleeSettings& settings, CombatSimulationPolicy policy, ServerTick tick,
        const DirectMagicCatalog* magic) noexcept
    try
    {
        if (policy.minimumActorAttackIntervalTicks == 0 || policy.respawnDelayTicks == 0
            || !finite(policy.secondsPerTick) || policy.secondsPerTick <= 0.f || policy.difficulty < -100
            || policy.difficulty > 100 || inventory.contentManifestId() != items.contentManifestId()
            || inventory.contentManifestId() != weapons.contentManifestId()
            || (magic && inventory.contentManifestId() != magic->contentManifestId()))
            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
        if (combat.lastSimulationTick() && tick < *combat.lastSimulationTick())
            return CombatSimulationError{ CombatSimulationErrorCode::TickRegression };
        const std::uint64_t elapsedTicks
            = combat.lastSimulationTick() ? tick.value() - combat.lastSimulationTick()->value() : 1;
        std::vector<CanonicalPlayerCombatState> playerStates(combat.players().begin(), combat.players().end());
        std::vector<CanonicalActorCombatState> actorStates(combat.actors().begin(), combat.actors().end());
        std::vector<AuthoritativeActorMeleeEvent> events;
        CanonicalInventoryWorld inventoryCandidate = inventory;
        auto random = Xoshiro256StarStar::restore(combat.randomState());

        const auto activePlayer = [&](PlayerId id) {
            return std::ranges::any_of(players.activeSessions(),
                [&](const CanonicalSessionProgress& session) { return session.playerId() == id; });
        };
        for (std::size_t index = 0; index < actorStates.size(); ++index)
        {
            auto& actor = actorStates[index];
            const auto* spatialActor = actors.find(actor.actorId);
            if (!spatialActor)
                return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
            if (actor.stats.dead)
            {
                if (!actor.deathTick)
                    continue;
                if (tick < *actor.deathTick)
                    return CombatSimulationError{ CombatSimulationErrorCode::TickRegression, index };
                if (actor.respawnStats.health > 0.f
                    && tick.value() - actor.deathTick->value() >= policy.respawnDelayTicks)
                {
                    const auto revision = actor.revision.next();
                    if (!revision)
                        return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
                    actor.stats = actor.respawnStats;
                    actor.attacker.fatigue = actor.respawnStats.fatigue;
                    actor.aggressionTarget.reset();
                    actor.lastAttackTick.reset();
                    actor.deathTick.reset();
                    actor.revision = *revision;
                }
                continue;
            }
            if (!actor.aggressionTarget)
                continue;
            auto target = std::ranges::lower_bound(
                playerStates, *actor.aggressionTarget, {}, &CanonicalPlayerCombatState::playerId);
            const auto* spatialPlayer = players.findPlayer(*actor.aggressionTarget);
            const bool validTarget = target != playerStates.end() && target->playerId == *actor.aggressionTarget
                && spatialPlayer && activePlayer(target->playerId) && !target->victim.dead
                && spatialPlayer->transform().cell() == spatialActor->root().cell();
            if (!validTarget)
            {
                const auto revision = actor.revision.next();
                if (!revision)
                    return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
                actor.aggressionTarget.reset();
                actor.revision = *revision;
                continue;
            }
            if (actor.attackReachQuanta == 0
                || (actor.lastAttackTick && tick >= *actor.lastAttackTick
                    && tick.value() - actor.lastAttackTick->value() < policy.minimumActorAttackIntervalTicks))
                continue;
            if (actor.lastAttackTick && tick < *actor.lastAttackTick)
                return CombatSimulationError{ CombatSimulationErrorCode::TickRegression, index };
            const auto rangeSquared = static_cast<std::uint64_t>(actor.attackReachQuanta) * actor.attackReachQuanta;
            if (distanceSquared(
                    spatialActor->root().position(), spatialPlayer->transform().position(), actor.attackReachQuanta)
                > rangeSquared)
                continue;
            const auto roll = random.uniformBelow(100);
            if (!roll)
                return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
            auto resolvedActor = actor.attacker;
            resolvedActor.fatigueTerm = openMwFatigueTerm(settings, actor.attacker.fatigue, actor.maximumFatigue);
            auto resolution = resolveOpenMwMelee(settings, resolvedActor, target->victim,
                OpenMwMeleeAttempt{ .type = MeleeAttackType::Chop,
                    .attackStrength = 1.f,
                    .hitRoll0To99 = static_cast<std::uint8_t>(*roll),
                    .contact = true,
                    .weapon = actor.naturalWeapon });
            if (resolution.code == OpenMwMeleeResolutionCode::InvalidInput)
                return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
            if (resolution.code == OpenMwMeleeResolutionCode::DeadVictim)
                continue;
            if (resolution.hit)
            {
                const auto* playerInventory = inventoryCandidate.findPlayer(target->playerId);
                const auto left = static_cast<std::size_t>(EquipmentSlot::CarriedLeft);
                const auto shieldId = playerInventory ? playerInventory->equipment[left] : std::nullopt;
                const auto* shieldStack = shieldId && playerInventory ? playerInventory->findStack(*shieldId) : nullptr;
                const auto* shieldItem = shieldStack ? items.find(shieldStack->prototypeId) : nullptr;
                const auto* shield = shieldStack ? weapons.findArmor(shieldStack->prototypeId) : nullptr;
                const bool canBlock = shieldId && shieldStack && shieldItem && shield
                    && shieldItem->category == ItemCategory::Armor && shieldStack->condition > 0
                    && !target->victim.knockedDown && !target->victim.paralyzed
                    && insideBlockArc(spatialPlayer->transform(), spatialActor->root().position(), settings);
                if (canBlock)
                {
                    const auto blockRoll = random.uniformBelow(100);
                    if (!blockRoll)
                        return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
                    auto blocker = target->stats;
                    blocker.fatigueTerm = openMwFatigueTerm(settings, target->stats.fatigue, target->maximumFatigue);
                    const bool receivesStillBonus
                        = receivesBlockStillBonus(spatialPlayer->transform(), spatialPlayer->linearVelocity());
                    const float chance = openMwMeleeBlockChance(
                        settings, target->blockSkill, blocker, resolvedActor, 1.f, receivesStillBonus);
                    if (static_cast<float>(*blockRoll) < chance)
                    {
                        const auto conditionLoss = std::min<std::uint32_t>(shieldStack->condition,
                            static_cast<std::uint32_t>(std::min<float>(
                                resolution.damage, static_cast<float>(std::numeric_limits<std::uint32_t>::max()))));
                        const auto condition = shieldStack->condition - conditionLoss;
                        if (conditionLoss != 0
                            && inventoryCandidate.setEquippedItemCondition(target->playerId, EquipmentSlot::CarriedLeft,
                                   *shieldId, condition, condition == 0, tick)
                                != EquippedConditionResult::Applied)
                            return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
                        const float fatigueCost = openMwMeleeBlockFatigueCost(
                            settings, target->stats.normalizedEncumbrance, actor.naturalWeapon, 1.f);
                        resolution.blocked = true;
                        resolution.damage = 0.f;
                        resolution.victimHealth = target->victim.health;
                        resolution.victimFatigue = target->victim.fatigue - fatigueCost;
                        resolution.victimDied = false;
                        (void)advanceCombatSkill(*target, CombatProgressionSkill::Block);
                    }
                }
                if (!resolution.blocked && resolution.damagedStat == MeleeDamageStat::Health)
                {
                    const float originalDamage = resolution.damage;
                    const float armorAdjustedDamage = openMwArmorAdjustedDamage(
                        settings, originalDamage, armorRating(*target, playerInventory, items, weapons, settings));
                    resolution.damage = std::max(armorAdjustedDamage, 1.f);
                    const auto armorRoll = random.uniformBelow(100);
                    if (!armorRoll)
                        return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
                    const bool hasShield
                        = equippedArmor(playerInventory, EquipmentSlot::CarriedLeft, items, weapons) != nullptr;
                    const auto slot = armorHitSlot(static_cast<std::uint8_t>(*armorRoll),
                        settings.redistributeShieldHitsWhenNotWearingShield && !hasShield);
                    const auto* hitStack = equippedArmor(playerInventory, slot, items, weapons);
                    const auto* armor = hitStack ? weapons.findArmor(hitStack->prototypeId) : nullptr;
                    (void)advanceCombatSkill(
                        *target, armor ? progressionSkill(armor->skill) : CombatProgressionSkill::Unarmored);
                    if (hitStack && (!actor.creature || settings.unarmedCreatureAttacksDamageArmor))
                    {
                        const auto lossValue = std::ceil(std::max(0.f, originalDamage - armorAdjustedDamage));
                        const auto loss = static_cast<std::uint32_t>(
                            std::min<float>(lossValue, static_cast<float>(hitStack->condition)));
                        const auto condition = hitStack->condition - loss;
                        if (loss != 0
                            && inventoryCandidate.setEquippedItemCondition(
                                   target->playerId, slot, hitStack->stackId, condition, condition == 0, tick)
                                != EquippedConditionResult::Applied)
                            return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
                    }
                }
                if (!resolution.blocked)
                    resolution.damage
                        = openMwDifficultyScaledDamage(settings, resolution.damage, policy.difficulty, true);
                if (resolution.damagedStat == MeleeDamageStat::Health)
                {
                    const float remaining = target->victim.health - resolution.damage;
                    resolution.victimDied = remaining < 1.f;
                    resolution.victimHealth = resolution.victimDied ? 0.f : remaining;
                }
                else
                    resolution.victimFatigue = target->victim.fatigue - resolution.damage;
            }
            DirectMagicResolution actorMagic;
            DirectMagicResolution targetMagic;
            if (magic && resolution.hit)
            {
                const auto* playerInventory = inventoryCandidate.findPlayer(target->playerId);
                const auto targetDefense = combinedDirectMagicDefense(target->magicDefense, playerInventory, *magic);
                const auto shieldDamage = resolveElementalShieldDamage(targetDefense, actor.magicDefense,
                    actor.attacker.luck, actor.attacker.fatigue, actor.maximumFatigue, magic->settings(), random);
                if (!shieldDamage)
                    return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
                actorMagic.healthDamage
                    = openMwDifficultyScaledDamage(settings, *shieldDamage, policy.difficulty, false);
                if (const auto* carrier = magic->findActor(actor.actorId))
                {
                    for (const auto& disease : carrier->diseases)
                    {
                        if (std::ranges::binary_search(target->contractedDiseases, disease.spellId))
                            continue;
                        const auto contracted
                            = resolveDiseaseTransfer(disease.kind, targetDefense, magic->settings(), random);
                        if (!contracted)
                            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
                        if (!*contracted)
                            continue;
                        const auto effects = resolveDirectMagicEffects(
                            disease.effects, DirectMagicTarget::Other, targetDefense, random);
                        if (!effects || !addContractedDisease(*target, disease.spellId))
                            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld, index };
                        targetMagic.healthDamage += effects->healthDamage;
                        targetMagic.fatigueDamage += effects->fatigueDamage;
                    }
                }
                targetMagic.healthDamage
                    = openMwDifficultyScaledDamage(settings, targetMagic.healthDamage, policy.difficulty, true);
            }
            const auto actorRevision = actor.revision.next();
            const auto targetRevision = target->revision.next();
            if (!actorRevision || !targetRevision)
                return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
            actor.attacker.fatigue = resolution.attackerFatigue;
            actor.stats.fatigue = resolution.attackerFatigue;
            actor.stats.fatigueNonNegative = resolution.attackerFatigue >= 0.f;
            actor.lastAttackTick = tick;
            actor.revision = *actorRevision;
            applyDirectMagic(actor, actorMagic, tick);
            target->victim.health = resolution.victimHealth;
            target->victim.fatigue = resolution.victimFatigue;
            target->victim.fatigueNonNegative = resolution.victimFatigue >= 0.f;
            target->victim.dead = resolution.victimDied || target->victim.dead;
            target->stats.fatigue = resolution.victimFatigue;
            if (target->victim.dead && !target->deathTick)
                target->deathTick = tick;
            applyDirectMagic(*target, targetMagic, tick);
            resolution.victimHealth = target->victim.health;
            resolution.victimFatigue = target->victim.fatigue;
            resolution.victimDied = target->victim.dead;
            target->revision = *targetRevision;
            events.push_back({ tick, actor.actorId, target->playerId, *actorRevision, *targetRevision, resolution });
            if (events.size() > MaximumAuthoritativeActorMeleeEventsPerTick)
                return CombatSimulationError{ CombatSimulationErrorCode::EventLimitExceeded, index };
        }

        for (std::size_t index = 0; index < playerStates.size(); ++index)
        {
            auto& player = playerStates[index];
            if (!player.victim.dead)
                continue;
            if (!player.deathTick)
                continue;
            if (tick < *player.deathTick)
                return CombatSimulationError{ CombatSimulationErrorCode::TickRegression, index };
            if (player.respawnVictim.health <= 0.f
                || tick.value() - player.deathTick->value() < policy.respawnDelayTicks)
                continue;
            const auto revision = player.revision.next();
            if (!revision)
                return CombatSimulationError{ CombatSimulationErrorCode::RevisionExhausted, index };
            player.victim = player.respawnVictim;
            player.stats.fatigue = player.respawnVictim.fatigue;
            player.magicka = player.maximumMagicka;
            player.lastAttackTick.reset();
            player.deathTick.reset();
            player.revision = *revision;
        }

        const float elapsedSeconds = static_cast<float>(elapsedTicks) * policy.secondsPerTick;
        if (!finite(elapsedSeconds))
            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
        const auto recoverFatigue = [&](float current, float maximum, const OpenMwMeleeAttacker& attacker) {
            if (current >= maximum)
                return current;
            const float rate
                = openMwFatigueRecoveryPerSecond(settings, attacker.endurance, attacker.normalizedEncumbrance);
            if (!finite(rate) || rate <= 0.f)
                return current;
            return std::min(maximum, current + rate * elapsedSeconds);
        };
        // Latest-wins snapshots order passive recovery by server tick. Keeping the
        // action revision stable prevents normal network delay from making every
        // attack stale while fatigue is recovering.
        for (std::size_t index = 0; index < playerStates.size(); ++index)
        {
            auto& player = playerStates[index];
            if (player.victim.dead || !activePlayer(player.playerId))
                continue;
            const float recovered = recoverFatigue(player.stats.fatigue, player.maximumFatigue, player.stats);
            if (recovered != player.stats.fatigue)
            {
                player.stats.fatigue = recovered;
                player.victim.fatigue = recovered;
                player.victim.fatigueNonNegative = recovered >= 0.f;
            }
            const auto* spatialPlayer = players.findPlayer(player.playerId);
            const bool engaged
                = spatialPlayer && std::ranges::any_of(actorStates, [&](const CanonicalActorCombatState& actor) {
                    if (actor.stats.dead || actor.aggressionTarget != player.playerId)
                        return false;
                    const auto* spatialActor = actors.find(actor.actorId);
                    return spatialActor && spatialActor->root().cell() == spatialPlayer->transform().cell();
                });
            if (engaged)
                continue;
            if (player.victim.health < player.maximumHealth && player.healthRecoveryPerSecond > 0.f)
            {
                player.victim.health = std::min(
                    player.maximumHealth, player.victim.health + player.healthRecoveryPerSecond * elapsedSeconds);
            }
            if (player.magicka < player.maximumMagicka && player.magickaRecoveryPerSecond > 0.f)
            {
                player.magicka = std::min(
                    player.maximumMagicka, player.magicka + player.magickaRecoveryPerSecond * elapsedSeconds);
            }
        }
        for (std::size_t index = 0; index < actorStates.size(); ++index)
        {
            auto& actor = actorStates[index];
            const auto* spatialActor = actors.find(actor.actorId);
            const bool activeCell = spatialActor
                && std::ranges::any_of(players.activeSessions(), [&](const CanonicalSessionProgress& session) {
                    const auto* player = players.findPlayer(session.playerId());
                    return player && player->transform().cell() == spatialActor->root().cell();
                });
            if (actor.stats.dead || !activeCell)
                continue;
            const float recovered = recoverFatigue(actor.attacker.fatigue, actor.maximumFatigue, actor.attacker);
            if (recovered == actor.attacker.fatigue)
                continue;
            actor.attacker.fatigue = recovered;
            actor.stats.fatigue = recovered;
            actor.stats.fatigueNonNegative = recovered >= 0.f;
        }

        auto created = createCanonicalCombatWorld(playerStates, actorStates, random.snapshot(), tick);
        auto* candidate = std::get_if<CanonicalCombatWorld>(&created);
        if (!candidate)
            return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
        std::optional<CanonicalInventoryWorld> changedInventory;
        if (inventoryCandidate != inventory)
            changedInventory.emplace(std::move(inventoryCandidate));
        return CombatSimulationStep{ std::move(*candidate), std::move(changedInventory), std::move(events) };
    }
    catch (...)
    {
        return CombatSimulationError{ CombatSimulationErrorCode::InvalidWorld };
    }

    WaitRestRecoveryResult applyAuthoritativeWaitRestRecovery(const CanonicalCombatWorld& combat,
        const CanonicalServerState& players, const CanonicalActorWorld& actors,
        const OpenMwMeleeSettings& settings, std::uint8_t hours, WaitRestMode mode) noexcept
    try
    {
        if (hours == 0 || hours > MaximumWaitRestHours
            || (mode != WaitRestMode::Wait && mode != WaitRestMode::Rest))
            return WaitRestRecoveryError::InvalidRequest;

        for (const auto& active : players.activeSessions())
        {
            const auto* spatialPlayer = players.findPlayer(active.playerId());
            const auto* combatPlayer = combat.findPlayer(active.playerId());
            if (!spatialPlayer || !combatPlayer)
                return WaitRestRecoveryError::InvalidWorld;
            if (combatPlayer->victim.dead)
                return WaitRestRecoveryError::DeadPlayer;
            const bool engaged = std::ranges::any_of(combat.actors(), [&](const CanonicalActorCombatState& actor) {
                if (actor.stats.dead || actor.aggressionTarget != active.playerId())
                    return false;
                const auto* spatialActor = actors.find(actor.actorId);
                return spatialActor && spatialActor->root().cell() == spatialPlayer->transform().cell();
            });
            if (engaged)
                return WaitRestRecoveryError::ActiveCombat;
        }

        std::vector<CanonicalPlayerCombatState> playerStates(combat.players().begin(), combat.players().end());
        constexpr float RestRecoverySecondsPerHour = 120.f;
        const float restSeconds = static_cast<float>(hours) * RestRecoverySecondsPerHour;
        const float fatigueSeconds = static_cast<float>(hours) * 3600.f;
        if (!finite(restSeconds) || !finite(fatigueSeconds))
            return WaitRestRecoveryError::InvalidRequest;

        for (const auto& active : players.activeSessions())
        {
            auto found = std::ranges::lower_bound(
                playerStates, active.playerId(), {}, &CanonicalPlayerCombatState::playerId);
            if (found == playerStates.end() || found->playerId != active.playerId())
                return WaitRestRecoveryError::InvalidWorld;
            auto next = *found;
            if (next.stats.fatigue < next.maximumFatigue)
            {
                const float rate = openMwFatigueRecoveryPerSecond(
                    settings, next.stats.endurance, next.stats.normalizedEncumbrance);
                if (!finite(rate) || rate < 0.f)
                    return WaitRestRecoveryError::InvalidWorld;
                next.stats.fatigue = std::min(next.maximumFatigue, next.stats.fatigue + rate * fatigueSeconds);
                next.victim.fatigue = next.stats.fatigue;
                next.victim.fatigueNonNegative = next.victim.fatigue >= 0.f;
            }
            if (mode == WaitRestMode::Rest)
            {
                if (next.victim.health < next.maximumHealth)
                    next.victim.health = std::min(next.maximumHealth,
                        next.victim.health + next.healthRecoveryPerSecond * restSeconds);
                if (next.magicka < next.maximumMagicka)
                    next.magicka = std::min(next.maximumMagicka,
                        next.magicka + next.magickaRecoveryPerSecond * restSeconds);
            }
            if (next != *found)
            {
                const auto revision = found->revision.next();
                if (!revision)
                    return WaitRestRecoveryError::RevisionExhausted;
                next.revision = *revision;
                *found = std::move(next);
            }
        }

        auto created = createCanonicalCombatWorld(
            playerStates, combat.actors(), combat.randomState(), combat.lastSimulationTick());
        auto* candidate = std::get_if<CanonicalCombatWorld>(&created);
        return candidate ? WaitRestRecoveryResult(std::move(*candidate))
                         : WaitRestRecoveryResult(WaitRestRecoveryError::InvalidWorld);
    }
    catch (...)
    {
        return WaitRestRecoveryError::InvalidWorld;
    }
}
