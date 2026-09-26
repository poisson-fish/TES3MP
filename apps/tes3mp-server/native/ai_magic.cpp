#include "ai_magic.hpp"

#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/spellpriority.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <apps/openmw/mwmechanics/weaponpriority.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadweap.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace TES3MP::Native
{
    std::optional<float> rateAiMeleeWeapon(const AiMagicContext& context, const ESM::Weapon& weapon,
        int condition, float charge, bool enchantedWeaponsAreMagical, const MWWorld::ESMStore& content)
    {
        if (condition < 0 || !std::isfinite(charge) || (charge < 0.f && charge != -1.f))
            throw std::invalid_argument("Native AI weapon state invalid");
        const auto* type = MWMechanics::getWeaponType(weapon.mData.mType);
        if (type->mWeaponClass != ESM::WeaponType::Melee) return std::nullopt;
        if (!context.enemy || !condition) return 0.f;
        const int skill = int(context.caster.getSkill(type->mSkill).getModified());
        if (skill <= 0) return 0.f;
        float damage = MWMechanics::weaponRatingAdjustedDamage(weapon,
            context.caster.getAttribute(ESM::Attribute::Strength).getModified(),
            weapon.mData.mHealth ? float(condition) / weapon.mData.mHealth : 1.f,
            weapon.mData.mHealth != 0, content);
        if (MWMechanics::isNormalWeapon(&weapon, enchantedWeaponsAreMagical))
            damage = MWMechanics::applyNormalWeaponResistance(*context.enemy, damage);
        if (context.enemyWerewolf && (weapon.mData.mFlags & ESM::Weapon::Silver))
            damage *= content.get<ESM::GameSetting>().find("fWereWolfSilverWeaponDamageMult")->mValue.getFloat();
        if (!weapon.mEnchant.empty())
        {
            const auto* enchantment = content.get<ESM::Enchantment>().search(weapon.mEnchant);
            if (!enchantment) return std::nullopt;
            if (enchantment->mData.mType == ESM::Enchantment::WhenStrikes)
            {
                const auto plan = prepareEnchantmentCast(*enchantment, context.caster, charge, content, true);
                if (!plan) return std::nullopt;
                if (plan->affordable) for (const auto& effect : plan->effects.effects)
                {
                    // This priority needs the complete known-spell domain.
                    if (effect.mEffectID == ESM::MagicEffect::RestoreMagicka && effect.mRange == ESM::RT_Self)
                        return std::nullopt;
                    const auto priority = MWMechanics::rateCommonEffect(effect, context.caster, 0.f);
                    if (!priority) return std::nullopt;
                    // Stock on-strike rating omits the spell range multiplier.
                    damage += MWMechanics::adjustEffectRating(effect, *priority, context.caster,
                        context.enemy, content, context.casterUnderwater, context.enemyUnderwater);
                }
            }
        }
        const float chance = MWMechanics::getHitChance(content, context.caster, *context.enemy, skill, false,
            context.enemy->getMagicEffects().getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() > 0.f);
        const float rating = MWMechanics::weaponRatingScore(weapon, damage, chance,
            content.get<ESM::GameSetting>().find("fAIMeleeWeaponMult")->mValue.getFloat());
        if (!std::isfinite(rating) || rating < 0.f) return std::nullopt;
        return rating;
    }

    std::optional<PreparedAiMagicCast> prepareAiMagicCast(const AiMagicContext& context,
        std::span<const AiMagicSpell> spells, std::span<const AiMagicItem> items,
        const MWWorld::ESMStore& content)
    {
        if (!std::isfinite(context.weaponRating) || context.weaponRating < 0.f)
            throw std::invalid_argument("Native AI weapon rating invalid");
        if (spells.size() > MaximumAiMagicSources || items.size() > MaximumAiMagicSources
            || spells.size() + items.size() > MaximumAiMagicSources)
            throw std::invalid_argument("Native AI magic source budget exceeded");
        // Validate the complete caller-owned domain before producing any plan.
        for (const auto& source : spells)
            if (!source.record || source.record->mId.empty())
                throw std::invalid_argument("Native AI spell identity invalid");
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            if (!item.instance.isSet() || item.instance.mContentFile < -1
                || item.enchantment.empty() || !std::isfinite(item.charge)
                || (item.charge < 0.f && item.charge != -1.f)
                || !std::isfinite(item.enemyDuration) || item.enemyDuration < 0.f
                || std::any_of(items.begin(), items.begin() + i,
                    [&](const auto& other) { return other.instance == item.instance; }))
                throw std::invalid_argument("Native AI magic item identity or state invalid");
        }
        if (context.caster.getHealth().getCurrent() <= 0.f || context.caster.getKnockedDown()
            || context.caster.getMagicEffects().getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() > 0.f)
            return std::nullopt;

        float magickaPriority = 0.f;
        for (const auto& source : spells)
            // Validate effects before autocalc; unsupported sources cannot be
            // made usable by restoring magicka in this bounded runtime.
            if (const auto prepared = prepareInstantSpell(*source.record, content, true))
            {
                const int cost = prepared->cost;
                if (cost > context.caster.getMagicka().getCurrent()
                    && cost < context.caster.getMagicka().getModified())
                { magickaPriority = 2.f; break; }
            }
        const auto rate = [&](const PreparedInstantEffects& effects) -> std::optional<float> {
            float result = 0.f;
            for (const auto& effect : effects.effects)
            {
                const auto priority = MWMechanics::rateCommonEffect(effect, context.caster, magickaPriority);
                if (!priority) return std::nullopt;
                result += MWMechanics::adjustEffectRating(effect, *priority, context.caster,
                    context.enemy, content, context.casterUnderwater, context.enemyUnderwater)
                    * MWMechanics::effectRatingMultiplier(effect.mRange, content);
            }
            return std::isfinite(result) ? std::optional(result) : std::nullopt;
        };
        std::optional<PreparedAiMagicCast> best;
        const auto better = [&](float rating) { return rating > (best ? best->rating : 0.f); };
        for (const auto& item : items)
        {
            const auto* enchantment = content.get<ESM::Enchantment>().search(item.enchantment);
            if (!enchantment || enchantment->mData.mType != ESM::Enchantment::WhenUsed) continue;
            auto prepared = prepareEnchantmentCast(*enchantment, context.caster, item.charge, content, true);
            if (!prepared || !prepared->affordable) continue;
            const int cost = MWMechanics::getEffectiveEnchantmentCastCost(
                MWMechanics::getEnchantmentCastCost(*enchantment, content),
                context.caster.getSkill(ESM::Skill::Enchant).getModified());
            const float multiplier = MWMechanics::magicItemRatingMultiplier(*enchantment, true,
                item.equipped, item.charge, cost, item.activeOnSelf, item.enemyDuration);
            if (multiplier <= 0.f) continue;
            const auto rating = rate(prepared->effects);
            if (rating && *rating * multiplier >= context.weaponRating && better(*rating * multiplier))
                best = PreparedAiMagicCast{PreparedInstantSpell{0, std::move(prepared->effects)},
                    enchantment->mId, AiMagicPayment{item.instance, item.charge, prepared->chargeAfter},
                    *rating * multiplier};
        }
        for (const auto& source : spells)
        {
            auto prepared = prepareInstantSpell(*source.record, content, true);
            if (!prepared) continue;
            const float multiplier = MWMechanics::spellRatingMultiplier(*source.record,
                MWMechanics::getSpellSuccessChance(*source.record, context.caster, content),
                source.racialPower, source.activeOnSelf, source.activeOnEnemy);
            if (multiplier <= 0.f) continue;
            const auto rating = rate(prepared->effects);
            if (rating && *rating * multiplier > context.weaponRating && better(*rating * multiplier))
                best = PreparedAiMagicCast{std::move(*prepared), source.record->mId, {}, *rating * multiplier};
        }
        return best;
    }
}
