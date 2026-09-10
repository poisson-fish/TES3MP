#include <tes3mp/direct_magic.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace
{
    bool finite(float value) noexcept
    {
        return std::isfinite(value);
    }

    bool validEffect(const TES3MP::DirectMagicEffectProfile& effect) noexcept
    {
        return static_cast<std::uint8_t>(effect.target) <= static_cast<std::uint8_t>(TES3MP::DirectMagicTarget::Other)
            && static_cast<std::uint8_t>(effect.kind)
            <= static_cast<std::uint8_t>(TES3MP::DirectMagicEffectKind::DamageFatigue)
            && finite(effect.minimumMagnitude) && finite(effect.maximumMagnitude) && effect.minimumMagnitude >= 0.f
            && effect.minimumMagnitude <= effect.maximumMagnitude && effect.maximumMagnitude <= 1'000'000.f;
    }

    float resistance(const TES3MP::DirectMagicDefense& defense, TES3MP::DirectMagicEffectKind kind) noexcept
    {
        using Kind = TES3MP::DirectMagicEffectKind;
        switch (kind)
        {
            case Kind::FireDamage:
                return defense.fireResistance + defense.fireShield;
            case Kind::ShockDamage:
                return defense.shockResistance + defense.shockShield;
            case Kind::FrostDamage:
                return defense.frostResistance + defense.frostShield;
            case Kind::PoisonDamage:
                return defense.poisonResistance;
            case Kind::DamageHealth:
            case Kind::DamageFatigue:
                return 0.f;
        }
        return 0.f;
    }

    float addBounded(float lhs, float rhs) noexcept
    {
        const double value = static_cast<double>(lhs) + static_cast<double>(rhs);
        return static_cast<float>(std::clamp(value, -1'000'000.0, 1'000'000.0));
    }
}

namespace TES3MP
{
    bool validDirectMagicDefense(const DirectMagicDefense& value) noexcept
    {
        const std::array values{ value.willpower, value.destructionSkill, value.fireResistance, value.shockResistance,
            value.frostResistance, value.poisonResistance, value.commonDiseaseResistance, value.blightDiseaseResistance,
            value.fireShield, value.shockShield, value.frostShield };
        return std::ranges::all_of(
            values, [](float current) { return finite(current) && current >= -1'000.f && current <= 1'000.f; });
    }

    std::optional<DirectMagicCatalog> DirectMagicCatalog::create(ContentManifestId manifest,
        const ItemPrototypeCatalog& items, DirectMagicSettings settings,
        std::span<const DirectEnchantmentProfile> enchantments, std::span<const DirectEquipmentMagicProfile> equipment,
        std::span<const DirectActorMagicProfile> actors) noexcept
    try
    {
        if (manifest != items.contentManifestId() || !finite(settings.elementalShieldMultiplier)
            || settings.elementalShieldMultiplier < 0.f || settings.elementalShieldMultiplier > 1'000.f
            || !finite(settings.diseaseTransferChance) || settings.diseaseTransferChance < 0.f
            || settings.diseaseTransferChance > 100.f || enchantments.size() > MaximumItemPrototypes
            || equipment.size() > MaximumItemPrototypes || actors.size() > MaximumActorCatalogEntries)
            return std::nullopt;

        std::vector<DirectEnchantmentProfile> enchantmentValues(enchantments.begin(), enchantments.end());
        std::ranges::sort(enchantmentValues, {}, &DirectEnchantmentProfile::prototypeId);
        for (std::size_t index = 0; index < enchantmentValues.size(); ++index)
        {
            const auto& profile = enchantmentValues[index];
            const auto* item = items.find(profile.prototypeId);
            if (!item || item->category != ItemCategory::Weapon
                || (item->slotMask & slotToMask(EquipmentSlot::CarriedRight)) == 0
                || profile.kind != DirectMagicEnchantmentKind::OnStrike || profile.chargeCost == 0
                || profile.chargeCost > item->maxEnchantmentCharge || profile.effects.empty()
                || profile.effects.size() > MaximumDirectMagicEffectsPerSource
                || !std::ranges::all_of(profile.effects, validEffect)
                || (index && enchantmentValues[index - 1].prototypeId == profile.prototypeId))
                return std::nullopt;
        }

        std::vector<DirectEquipmentMagicProfile> equipmentValues(equipment.begin(), equipment.end());
        std::ranges::sort(equipmentValues, {}, &DirectEquipmentMagicProfile::prototypeId);
        for (std::size_t index = 0; index < equipmentValues.size(); ++index)
        {
            const auto& profile = equipmentValues[index];
            const auto* item = items.find(profile.prototypeId);
            if (!item || item->slotMask == 0 || !validDirectMagicDefense(profile.defense)
                || profile.defense.willpower != 0.f || profile.defense.destructionSkill != 0.f
                || (index && equipmentValues[index - 1].prototypeId == profile.prototypeId))
                return std::nullopt;
        }

        std::vector<DirectActorMagicProfile> actorValues(actors.begin(), actors.end());
        std::ranges::sort(actorValues, {}, &DirectActorMagicProfile::actorId);
        for (std::size_t index = 0; index < actorValues.size(); ++index)
        {
            const auto& profile = actorValues[index];
            if (!validDirectMagicDefense(profile.defense)
                || profile.diseases.size() > MaximumDirectMagicDiseasesPerActor
                || (index && actorValues[index - 1].actorId == profile.actorId))
                return std::nullopt;
            std::vector<SpellRecordId> diseaseIds;
            diseaseIds.reserve(profile.diseases.size());
            for (const auto& disease : profile.diseases)
            {
                if (disease.effects.empty() || disease.effects.size() > MaximumDirectMagicEffectsPerSource
                    || !std::ranges::all_of(disease.effects, [](const DirectMagicEffectProfile& effect) {
                           return validEffect(effect) && effect.target == DirectMagicTarget::Other;
                       }))
                    return std::nullopt;
                diseaseIds.push_back(disease.spellId);
            }
            std::ranges::sort(diseaseIds);
            if (std::ranges::adjacent_find(diseaseIds) != diseaseIds.end())
                return std::nullopt;
        }
        return DirectMagicCatalog(
            manifest, settings, std::move(enchantmentValues), std::move(equipmentValues), std::move(actorValues));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const DirectEnchantmentProfile* DirectMagicCatalog::findEnchantment(ItemPrototypeId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mEnchantments, id, {}, &DirectEnchantmentProfile::prototypeId);
        return found != mEnchantments.end() && found->prototypeId == id ? &*found : nullptr;
    }

    const DirectEquipmentMagicProfile* DirectMagicCatalog::findEquipment(ItemPrototypeId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mEquipment, id, {}, &DirectEquipmentMagicProfile::prototypeId);
        return found != mEquipment.end() && found->prototypeId == id ? &*found : nullptr;
    }

    const DirectActorMagicProfile* DirectMagicCatalog::findActor(ActorId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mActors, id, {}, &DirectActorMagicProfile::actorId);
        return found != mActors.end() && found->actorId == id ? &*found : nullptr;
    }

    DirectMagicDefense combinedDirectMagicDefense(const DirectMagicDefense& base,
        const CanonicalPlayerInventoryState* inventory, const DirectMagicCatalog& catalog) noexcept
    {
        DirectMagicDefense result = base;
        if (!inventory)
            return result;
        for (const auto& equipped : inventory->equipment)
        {
            const auto* stack = equipped ? inventory->findStack(*equipped) : nullptr;
            const auto* profile = stack ? catalog.findEquipment(stack->prototypeId) : nullptr;
            if (!profile)
                continue;
            result.fireResistance = addBounded(result.fireResistance, profile->defense.fireResistance);
            result.shockResistance = addBounded(result.shockResistance, profile->defense.shockResistance);
            result.frostResistance = addBounded(result.frostResistance, profile->defense.frostResistance);
            result.poisonResistance = addBounded(result.poisonResistance, profile->defense.poisonResistance);
            result.commonDiseaseResistance
                = addBounded(result.commonDiseaseResistance, profile->defense.commonDiseaseResistance);
            result.blightDiseaseResistance
                = addBounded(result.blightDiseaseResistance, profile->defense.blightDiseaseResistance);
            result.fireShield = addBounded(result.fireShield, profile->defense.fireShield);
            result.shockShield = addBounded(result.shockShield, profile->defense.shockShield);
            result.frostShield = addBounded(result.frostShield, profile->defense.frostShield);
        }
        return result;
    }

    std::optional<DirectMagicResolution> resolveDirectMagicEffects(std::span<const DirectMagicEffectProfile> effects,
        DirectMagicTarget target, const DirectMagicDefense& defense, Xoshiro256StarStar& random) noexcept
    {
        if (effects.size() > MaximumDirectMagicEffectsPerSource || !validDirectMagicDefense(defense))
            return std::nullopt;
        DirectMagicResolution result;
        for (const auto& effect : effects)
        {
            if (!validEffect(effect))
                return std::nullopt;
            if (effect.target != target)
                continue;
            float magnitude = effect.minimumMagnitude;
            if (effect.maximumMagnitude != effect.minimumMagnitude)
            {
                const auto roll = random.uniformBelow(10'001);
                if (!roll)
                    return std::nullopt;
                magnitude += (effect.maximumMagnitude - effect.minimumMagnitude) * static_cast<float>(*roll) / 10'000.f;
            }
            const float multiplier = std::max(0.f, 1.f - resistance(defense, effect.kind) * 0.01f);
            const float damage = magnitude * multiplier;
            if (!finite(damage))
                return std::nullopt;
            if (effect.kind == DirectMagicEffectKind::DamageFatigue)
                result.fatigueDamage = addBounded(result.fatigueDamage, damage);
            else
                result.healthDamage = addBounded(result.healthDamage, damage);
        }
        return result;
    }

    std::optional<float> resolveElementalShieldDamage(const DirectMagicDefense& shieldOwner,
        const DirectMagicDefense& attackerDefense, float attackerLuck, float attackerFatigue,
        float attackerMaximumFatigue, DirectMagicSettings settings, Xoshiro256StarStar& random) noexcept
    {
        if (!validDirectMagicDefense(shieldOwner) || !validDirectMagicDefense(attackerDefense) || !finite(attackerLuck)
            || !finite(attackerFatigue) || !finite(attackerMaximumFatigue) || attackerMaximumFatigue < 0.f
            || !finite(settings.elementalShieldMultiplier) || settings.elementalShieldMultiplier < 0.f)
            return std::nullopt;
        const std::array shields{ shieldOwner.fireShield, shieldOwner.shockShield, shieldOwner.frostShield };
        const std::array resistances{ attackerDefense.fireResistance, attackerDefense.shockResistance,
            attackerDefense.frostResistance };
        float damage = 0.f;
        for (std::size_t index = 0; index < shields.size(); ++index)
        {
            if (shields[index] <= 0.f)
                continue;
            const auto roll = random.uniformBelow(100);
            if (!roll)
                return std::nullopt;
            const float normalizedFatigue = std::floor(attackerMaximumFatigue) == 0.f
                ? 1.f
                : std::max(0.f, attackerFatigue / attackerMaximumFatigue);
            float save = (attackerDefense.destructionSkill + 0.2f * attackerDefense.willpower + 0.1f * attackerLuck)
                    * 1.25f * normalizedFatigue
                - static_cast<float>(*roll);
            save = std::min(100.f, std::max(0.f, save) + resistances[index]);
            damage = addBounded(damage, settings.elementalShieldMultiplier * shields[index] * (1.f - 0.01f * save));
        }
        return finite(damage) ? std::optional<float>(damage) : std::nullopt;
    }

    std::optional<bool> resolveDiseaseTransfer(DirectDiseaseKind kind, const DirectMagicDefense& target,
        DirectMagicSettings settings, Xoshiro256StarStar& random) noexcept
    {
        if (!validDirectMagicDefense(target) || !finite(settings.diseaseTransferChance)
            || settings.diseaseTransferChance < 0.f || settings.diseaseTransferChance > 100.f)
            return std::nullopt;
        const float resistance
            = kind == DirectDiseaseKind::Common ? target.commonDiseaseResistance : target.blightDiseaseResistance;
        const float threshold
            = std::clamp(settings.diseaseTransferChance * 100.f * (1.f - 0.01f * resistance), 0.f, 10'000.f);
        const auto roll = random.uniformBelow(10'000);
        return roll ? std::optional<bool>(static_cast<float>(*roll) < threshold) : std::nullopt;
    }
}
