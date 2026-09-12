#include "combat_content.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_COMBAT_V8";
        constexpr std::size_t MaximumFields = 48;

        struct ActorAttackDeclaration
        {
            ActorId actorId;
            OpenMwMeleeAttacker attacker;
            std::optional<OpenMwMeleeWeapon> weapon;
            std::uint32_t reachQuanta = 0;
        };

        CombatContentError error(CombatContentErrorCode code, std::size_t line = 0) noexcept
        {
            return { code, line };
        }

        template <class Value>
        std::optional<Value> number(std::string_view text) noexcept
        {
            Value value{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
                ? std::optional<Value>(value)
                : std::nullopt;
        }

        std::optional<float> finiteFloat(std::string_view text) noexcept
        {
            const auto value = number<float>(text);
            return value && std::isfinite(*value) ? value : std::nullopt;
        }

        std::optional<bool> boolean(std::string_view text) noexcept
        {
            if (text == "0")
                return false;
            if (text == "1")
                return true;
            return std::nullopt;
        }

        std::optional<DirectMagicTarget> magicTarget(std::string_view text) noexcept
        {
            if (text == "self")
                return DirectMagicTarget::Self;
            if (text == "other")
                return DirectMagicTarget::Other;
            return std::nullopt;
        }

        std::optional<DirectMagicEffectKind> magicEffect(std::string_view text) noexcept
        {
            if (text == "fire")
                return DirectMagicEffectKind::FireDamage;
            if (text == "shock")
                return DirectMagicEffectKind::ShockDamage;
            if (text == "frost")
                return DirectMagicEffectKind::FrostDamage;
            if (text == "poison")
                return DirectMagicEffectKind::PoisonDamage;
            if (text == "health")
                return DirectMagicEffectKind::DamageHealth;
            if (text == "fatigue")
                return DirectMagicEffectKind::DamageFatigue;
            return std::nullopt;
        }

        std::optional<DirectMagicDefense> magicDefense(std::span<const std::string_view> values) noexcept
        {
            if (values.size() != 11)
                return std::nullopt;
            std::array<float, 11> parsed{};
            for (std::size_t index = 0; index < parsed.size(); ++index)
            {
                const auto value = finiteFloat(values[index]);
                if (!value)
                    return std::nullopt;
                parsed[index] = *value;
            }
            DirectMagicDefense result{ parsed[0], parsed[1], parsed[2], parsed[3], parsed[4], parsed[5], parsed[6],
                parsed[7], parsed[8], parsed[9], parsed[10] };
            return validDirectMagicDefense(result) ? std::optional<DirectMagicDefense>(result) : std::nullopt;
        }

        std::optional<std::vector<DirectMagicEffectProfile>> magicEffects(
            std::span<const std::string_view> values, std::size_t count)
        {
            if (count == 0 || count > MaximumDirectMagicEffectsPerSource || values.size() != count * 4)
                return std::nullopt;
            std::vector<DirectMagicEffectProfile> result;
            result.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto target = magicTarget(values[index * 4]);
                const auto effect = magicEffect(values[index * 4 + 1]);
                const auto minimum = finiteFloat(values[index * 4 + 2]);
                const auto maximum = finiteFloat(values[index * 4 + 3]);
                if (!target || !effect || !minimum || !maximum || *minimum < 0.f || *minimum > *maximum)
                    return std::nullopt;
                result.push_back({ *target, *effect, *minimum, *maximum });
            }
            return result;
        }

        std::optional<std::size_t> tokenize(
            std::string_view line, std::array<std::string_view, MaximumFields>& output) noexcept
        {
            std::size_t count = 0;
            for (std::size_t begin = 0; begin < line.size();)
            {
                while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t'))
                    ++begin;
                if (begin == line.size())
                    break;
                if (count == output.size())
                    return std::nullopt;
                auto end = begin;
                while (end < line.size() && line[end] != ' ' && line[end] != '\t')
                    ++end;
                output[count++] = line.substr(begin, end - begin);
                begin = end;
            }
            return count;
        }
    }

    CombatContentLoadResult loadCombatContent(const std::filesystem::path& path, const ContentManifest& manifest,
        const ActorCatalog& actors, const ItemPrototypeCatalog& items) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path))
            return error(CombatContentErrorCode::Unavailable);
        if (std::filesystem::file_size(path) > MaximumCombatContentBytes)
            return error(CombatContentErrorCode::TooLarge);
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return error(CombatContentErrorCode::Unavailable);
        std::string text;
        text.reserve(MaximumCombatContentBytes + 1);
        char byte = 0;
        while (stream.get(byte))
        {
            text.push_back(byte);
            if (text.size() > MaximumCombatContentBytes)
                return error(CombatContentErrorCode::TooLarge);
        }
        if (!stream.eof())
            return error(CombatContentErrorCode::Unavailable);

        std::optional<ContentManifestId> declaredManifest;
        std::optional<OpenMwMeleeSettings> settings;
        std::optional<OpenMwSecuritySettings> securitySettings;
        std::optional<CanonicalPlayerCombatTemplate> playerTemplate;
        std::optional<CombatSkillProgressionSettings> skillSettings;
        std::optional<std::array<CombatSkillProgressionRule, static_cast<std::size_t>(CombatProgressionSkill::Count)>>
            skillRules;
        std::optional<std::uint64_t> randomSeed;
        std::optional<DirectMagicSettings> magicSettings;
        std::optional<DirectMagicDefense> playerMagic;
        std::vector<CanonicalActorCombatState> actorStates;
        std::vector<ActorAttackDeclaration> actorAttacks;
        std::vector<MeleeWeaponProfile> weaponProfiles;
        std::vector<MeleeArmorProfile> armorProfiles;
        std::vector<DirectEnchantmentProfile> enchantmentProfiles;
        std::vector<DirectEquipmentMagicProfile> equipmentMagicProfiles;
        std::vector<DirectTrapMagicProfile> trapMagicProfiles;
        std::map<ActorId, DirectActorMagicProfile> actorMagicProfiles;
        std::vector<ActorId> declaredActorMagic;
        std::size_t lineNumber = 0;
        for (std::size_t begin = 0; begin <= text.size();)
        {
            ++lineNumber;
            const auto end = text.find('\n', begin);
            const auto length = (end == std::string::npos ? text.size() : end) - begin;
            auto line = std::string_view(text).substr(begin, length);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (lineNumber == 1)
            {
                if (line != Header)
                    return error(CombatContentErrorCode::Malformed, lineNumber);
            }
            else if (!line.empty())
            {
                std::array<std::string_view, MaximumFields> fields{};
                const auto count = tokenize(line, fields);
                if (!count || *count == 0)
                    return error(CombatContentErrorCode::Malformed, lineNumber);
                const auto values = std::span(fields).first(*count);
                if (values[0] == "manifest")
                {
                    if (values.size() != 2 || declaredManifest)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    declaredManifest = ContentManifestId::fromHex(values[1]);
                    if (!declaredManifest)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                }
                else if (values[0] == "seed")
                {
                    if (values.size() != 2 || randomSeed)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    randomSeed = number<std::uint64_t>(values[1]);
                    if (!randomSeed)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                }
                else if (values[0] == "settings")
                {
                    if (values.size() != 35 || settings)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    std::array<float, 32> parsed{};
                    for (std::size_t index = 0; index < parsed.size(); ++index)
                    {
                        const auto value = finiteFloat(values[index + 1]);
                        if (!value)
                            return error(CombatContentErrorCode::InvalidSettings, lineNumber);
                        parsed[index] = *value;
                    }
                    const auto unarmedCreatureWear = boolean(values[33]);
                    const auto redistributeShieldHits = boolean(values[34]);
                    if (!unarmedCreatureWear || !redistributeShieldHits)
                        return error(CombatContentErrorCode::InvalidSettings, lineNumber);
                    settings = OpenMwMeleeSettings{ parsed[0], parsed[1], parsed[2], parsed[3], parsed[4], parsed[5],
                        parsed[6], parsed[7], parsed[8], parsed[9], parsed[10], parsed[11], parsed[12], parsed[13],
                        parsed[14], parsed[15], parsed[16], parsed[17], parsed[18], parsed[19], parsed[20], parsed[21],
                        parsed[22], parsed[23], parsed[24], parsed[25], parsed[26], parsed[27], parsed[28], parsed[29],
                        parsed[30], parsed[31], *unarmedCreatureWear, *redistributeShieldHits };
                    if (settings->minimumHandToHandMultiplier > settings->maximumHandToHandMultiplier
                        || settings->fatigueBase < 0.f || settings->fatigueMultiplier < 0.f
                        || settings->fatigueReturnBase < 0.f || settings->fatigueReturnMultiplier < 0.f
                        || settings->enduranceFatigueMultiplier < 0.f || settings->difficultyMultiplier <= 0.f
                        || settings->combatBlockLeftAngle < -180.f || settings->combatBlockLeftAngle > 0.f
                        || settings->combatBlockRightAngle < 0.f || settings->combatBlockRightAngle > 180.f
                        || settings->swingBlockMultiplier < 0.f || settings->swingBlockBase < 0.f
                        || settings->blockStillBonus < 0.f || settings->blockMinimumChance < 0.f
                        || settings->blockMinimumChance > settings->blockMaximumChance
                        || settings->blockMaximumChance > 100.f || settings->fatigueBlockBase < 0.f
                        || settings->fatigueBlockMultiplier < 0.f || settings->weaponFatigueBlockMultiplier < 0.f
                        || settings->baseArmorSkill <= 0.f || settings->unarmoredBase1 < 0.f
                        || settings->unarmoredBase2 < 0.f || settings->combatArmorMinimumMultiplier < 0.f
                        || settings->combatArmorMinimumMultiplier > 1.f)
                        return error(CombatContentErrorCode::InvalidSettings, lineNumber);
                }
                else if (values[0] == "magic_settings")
                {
                    if (values.size() != 3 || magicSettings)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto shieldMultiplier = finiteFloat(values[1]);
                    const auto diseaseChance = finiteFloat(values[2]);
                    if (!shieldMultiplier || *shieldMultiplier < 0.f || *shieldMultiplier > 1'000.f || !diseaseChance
                        || *diseaseChance < 0.f || *diseaseChance > 100.f)
                        return error(CombatContentErrorCode::InvalidMagicCatalog, lineNumber);
                    magicSettings = DirectMagicSettings{ *shieldMultiplier, *diseaseChance };
                }
                else if (values[0] == "security_settings")
                {
                    if (values.size() != 4 || securitySettings)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto pick = finiteFloat(values[1]);
                    const auto trap = finiteFloat(values[2]);
                    const auto disarmGain = finiteFloat(values[3]);
                    if (!pick || !trap || !disarmGain || *disarmGain <= 0.f)
                        return error(CombatContentErrorCode::InvalidSettings, lineNumber);
                    securitySettings = OpenMwSecuritySettings{ *pick, *trap, *disarmGain };
                }
                else if (values[0] == "player_magic")
                {
                    if (playerMagic || values.size() != 12)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    playerMagic = magicDefense(values.subspan(1));
                    if (!playerMagic)
                        return error(CombatContentErrorCode::InvalidMagicCatalog, lineNumber);
                }
                else if (values[0] == "actor_magic")
                {
                    if (values.size() != 13)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawActor = number<std::uint64_t>(values[1]);
                    const auto actor = rawActor ? ActorId::fromValue(*rawActor) : std::nullopt;
                    const auto defense = magicDefense(values.subspan(2));
                    if (!actor || !defense || std::ranges::find(declaredActorMagic, *actor) != declaredActorMagic.end())
                        return error(CombatContentErrorCode::InvalidMagicCatalog, lineNumber);
                    if (!actorMagicProfiles.contains(*actor) && actorMagicProfiles.size() >= MaximumActorCatalogEntries)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                    auto [position, inserted]
                        = actorMagicProfiles.try_emplace(*actor, DirectActorMagicProfile{ *actor, {}, {} });
                    (void)inserted;
                    position->second.defense = *defense;
                    declaredActorMagic.push_back(*actor);
                }
                else if (values[0] == "enchantment")
                {
                    if (values.size() < 8)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawPrototype = number<std::uint64_t>(values[1]);
                    const auto prototype = rawPrototype ? ItemPrototypeId::fromValue(*rawPrototype) : std::nullopt;
                    const auto chargeCost = number<std::uint32_t>(values[2]);
                    const auto countEffects = number<std::size_t>(values[3]);
                    const auto effects = countEffects ? magicEffects(values.subspan(4), *countEffects) : std::nullopt;
                    if (!prototype || !chargeCost || *chargeCost == 0 || !effects)
                        return error(CombatContentErrorCode::InvalidMagicCatalog, lineNumber);
                    if (enchantmentProfiles.size() >= MaximumItemPrototypes)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                    enchantmentProfiles.push_back(
                        { *prototype, DirectMagicEnchantmentKind::OnStrike, *chargeCost, std::move(*effects) });
                }
                else if (values[0] == "equipment_magic")
                {
                    if (values.size() != 13)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawPrototype = number<std::uint64_t>(values[1]);
                    const auto prototype = rawPrototype ? ItemPrototypeId::fromValue(*rawPrototype) : std::nullopt;
                    const auto defense = magicDefense(values.subspan(2));
                    if (!prototype || !defense)
                        return error(CombatContentErrorCode::InvalidMagicCatalog, lineNumber);
                    if (equipmentMagicProfiles.size() >= MaximumItemPrototypes)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                    equipmentMagicProfiles.push_back({ *prototype, *defense });
                }
                else if (values[0] == "disease")
                {
                    if (values.size() < 9)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawActor = number<std::uint64_t>(values[1]);
                    const auto actor = rawActor ? ActorId::fromValue(*rawActor) : std::nullopt;
                    const auto rawSpell = number<std::uint64_t>(values[2]);
                    const auto spell = rawSpell ? SpellRecordId::fromValue(*rawSpell) : std::nullopt;
                    const auto diseaseKind = values[3] == "common" ? std::optional(DirectDiseaseKind::Common)
                        : values[3] == "blight"                    ? std::optional(DirectDiseaseKind::Blight)
                                                                   : std::nullopt;
                    const auto countEffects = number<std::size_t>(values[4]);
                    const auto effects = countEffects ? magicEffects(values.subspan(5), *countEffects) : std::nullopt;
                    if (!actor || !spell || !diseaseKind || !effects)
                        return error(CombatContentErrorCode::InvalidMagicCatalog, lineNumber);
                    if (!actorMagicProfiles.contains(*actor) && actorMagicProfiles.size() >= MaximumActorCatalogEntries)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                    auto [position, inserted]
                        = actorMagicProfiles.try_emplace(*actor, DirectActorMagicProfile{ *actor, {}, {} });
                    (void)inserted;
                    if (position->second.diseases.size() >= MaximumDirectMagicDiseasesPerActor)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                    position->second.diseases.push_back({ *spell, *diseaseKind, std::move(*effects) });
                }
                else if (values[0] == "trap")
                {
                    if (values.size() < 7)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawTrap = number<std::uint64_t>(values[1]);
                    const auto trap = rawTrap ? TrapPrototypeId::fromValue(*rawTrap) : std::nullopt;
                    const auto countEffects = number<std::size_t>(values[2]);
                    const auto effects = countEffects ? magicEffects(values.subspan(3), *countEffects) : std::nullopt;
                    if (!trap || !effects
                        || !std::ranges::all_of(
                            *effects, [](const auto& effect) { return effect.target == DirectMagicTarget::Other; }))
                        return error(CombatContentErrorCode::InvalidMagicCatalog, lineNumber);
                    if (trapMagicProfiles.size() >= MaximumDirectMagicTraps)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                    trapMagicProfiles.push_back({ *trap, std::move(*effects) });
                }
                else if (values[0] == "player")
                {
                    if (values.size() != 27 || playerTemplate)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    std::array<float, 24> parsed{};
                    for (std::size_t index = 0; index < parsed.size(); ++index)
                    {
                        const auto value = finiteFloat(values[index + 1]);
                        if (!value)
                            return error(CombatContentErrorCode::InvalidPlayerTemplate, lineNumber);
                        parsed[index] = *value;
                    }
                    const auto maximumWeight = number<std::uint64_t>(values[25]);
                    const auto werewolf = boolean(values[26]);
                    if (!maximumWeight || *maximumWeight == 0 || !werewolf || parsed[13] < 0.f || parsed[14] < 0.f
                        || parsed[15] <= 0.f || parsed[16] < 0.f || parsed[17] < 0.f || parsed[18] < 0.f)
                        return error(CombatContentErrorCode::InvalidPlayerTemplate, lineNumber);
                    OpenMwMeleeAttacker attacker;
                    attacker.agility = parsed[0];
                    attacker.luck = parsed[1];
                    attacker.strength = parsed[2];
                    attacker.fatigueTerm = parsed[3];
                    attacker.fortifyAttack = parsed[4];
                    attacker.blind = parsed[5];
                    attacker.handToHandSkill = parsed[11];
                    attacker.fatigue = parsed[12];
                    attacker.endurance = parsed[13];
                    attacker.werewolf = *werewolf;
                    attacker.godMode = false;
                    OpenMwMeleeVictim victim;
                    victim.health = attacker.strength;
                    victim.fatigue = attacker.fatigue;
                    victim.evasion = (attacker.agility / 5.f + attacker.luck / 10.f) * attacker.fatigueTerm;
                    playerTemplate = CanonicalPlayerCombatTemplate{ attacker,
                        { parsed[6], parsed[7], parsed[8], parsed[9], parsed[10] }, parsed[14], *maximumWeight, victim,
                        victim.health, victim.fatigue };
                    playerTemplate->intelligence = parsed[15];
                    playerTemplate->magicka = parsed[16];
                    playerTemplate->maximumMagicka = parsed[16];
                    playerTemplate->healthRecoveryPerSecond = parsed[17];
                    playerTemplate->magickaRecoveryPerSecond = parsed[18];
                    playerTemplate->armorSkills = { parsed[19], parsed[20], parsed[21], parsed[22] };
                    playerTemplate->securitySkill = parsed[23];
                }
                else if (values[0] == "progression")
                {
                    constexpr std::size_t SkillCount = static_cast<std::size_t>(CombatProgressionSkill::Count);
                    if (values.size() != 5 + SkillCount * 2 || skillSettings || skillRules)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    std::array<float, 4> factors{};
                    for (std::size_t index = 0; index < factors.size(); ++index)
                    {
                        const auto parsed = finiteFloat(values[index + 1]);
                        if (!parsed || *parsed <= 0.f)
                            return error(CombatContentErrorCode::InvalidPlayerTemplate, lineNumber);
                        factors[index] = *parsed;
                    }
                    std::array<CombatSkillProgressionRule, SkillCount> rules{};
                    for (std::size_t index = 0; index < SkillCount; ++index)
                    {
                        const auto specialization = number<std::uint8_t>(values[5 + index * 2]);
                        const auto gain = finiteFloat(values[6 + index * 2]);
                        if (!specialization || *specialization > static_cast<std::uint8_t>(ClassSpecialization::Stealth)
                            || !gain || *gain < 0.f
                            || (index == static_cast<std::size_t>(CombatProgressionSkill::Security)
                                && *gain <= 0.f))
                            return error(CombatContentErrorCode::InvalidPlayerTemplate, lineNumber);
                        rules[index] = { static_cast<ClassSpecialization>(*specialization), *gain };
                    }
                    skillSettings = CombatSkillProgressionSettings{ factors[0], factors[1], factors[2], factors[3] };
                    skillRules = rules;
                }
                else if (values[0] == "actor")
                {
                    if (values.size() != 14)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawActor = number<std::uint64_t>(values[1]);
                    const auto actor = rawActor ? ActorId::fromValue(*rawActor) : std::nullopt;
                    std::array<float, 7> parsed{};
                    for (std::size_t index = 0; index < parsed.size(); ++index)
                    {
                        const auto value = finiteFloat(values[index + 2]);
                        if (!value)
                            return error(CombatContentErrorCode::InvalidActorSet, lineNumber);
                        parsed[index] = *value;
                    }
                    const auto knockedDown = boolean(values[9]);
                    const auto paralyzed = boolean(values[10]);
                    const auto unaware = boolean(values[11]);
                    const auto dead = boolean(values[12]);
                    const auto creature = boolean(values[13]);
                    if (!actor || !knockedDown || !paralyzed || !unaware || !dead || !creature)
                        return error(CombatContentErrorCode::InvalidActorSet, lineNumber);
                    if (parsed[0] < 0.f || (*dead && parsed[0] != 0.f) || (!*dead && parsed[0] < 1.f))
                        return error(CombatContentErrorCode::InvalidActorSet, lineNumber);
                    OpenMwMeleeVictim victim;
                    victim.health = parsed[0];
                    victim.fatigue = parsed[1];
                    victim.evasion = parsed[2];
                    victim.chameleon = parsed[3];
                    victim.invisibility = parsed[4];
                    victim.normalWeaponResistance = parsed[5];
                    victim.normalWeaponWeakness = parsed[6];
                    victim.fatigueNonNegative = victim.fatigue >= 0.f;
                    victim.knockedDown = *knockedDown;
                    victim.paralyzed = *paralyzed;
                    victim.unaware = *unaware;
                    victim.dead = *dead;
                    actorStates.push_back({ *actor, CombatRevision::initial(), victim, victim, {}, std::nullopt, 0,
                        std::nullopt, std::nullopt, std::nullopt, victim.health, victim.fatigue, *creature });
                    if (actorStates.size() > MaximumActorCombatants)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                }
                else if (values[0] == "actor_attack")
                {
                    if (values.size() != 16)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawActor = number<std::uint64_t>(values[1]);
                    const auto actor = rawActor ? ActorId::fromValue(*rawActor) : std::nullopt;
                    std::array<float, 14> parsed{};
                    for (std::size_t index = 0; index < parsed.size(); ++index)
                    {
                        const auto value = finiteFloat(values[index + 2]);
                        if (!value)
                            return error(CombatContentErrorCode::InvalidActorSet, lineNumber);
                        parsed[index] = *value;
                    }
                    if (!actor || parsed[3] <= 0.f || parsed[6] < 0.f || parsed[6] > parsed[7] || parsed[8] < 0.f
                        || parsed[8] > parsed[9] || parsed[10] < 0.f || parsed[10] > parsed[11] || parsed[12] <= 0.f
                        || parsed[12] > 8192.f || parsed[13] < 0.f)
                        return error(CombatContentErrorCode::InvalidActorSet, lineNumber);
                    OpenMwMeleeAttacker attacker;
                    attacker.agility = parsed[0];
                    attacker.luck = parsed[1];
                    attacker.strength = parsed[2];
                    attacker.fatigueTerm = parsed[3];
                    attacker.weaponSkill = parsed[4];
                    attacker.handToHandSkill = parsed[4];
                    attacker.fatigue = parsed[5];
                    attacker.endurance = parsed[13];
                    std::optional<OpenMwMeleeWeapon> weapon;
                    if (parsed[6] != 0.f || parsed[7] != 0.f || parsed[8] != 0.f || parsed[9] != 0.f
                        || parsed[10] != 0.f || parsed[11] != 0.f)
                        weapon = OpenMwMeleeWeapon{ parsed[6], parsed[7], parsed[8], parsed[9], parsed[10], parsed[11],
                            0.f, 1.f, 0, false, false };
                    const auto reach = static_cast<double>(parsed[12]) * 128.0 * 1024.0;
                    if (reach < 1.0 || reach > static_cast<double>(1u << 30))
                        return error(CombatContentErrorCode::InvalidActorSet, lineNumber);
                    actorAttacks.push_back({ *actor, attacker, weapon, static_cast<std::uint32_t>(std::round(reach)) });
                }
                else if (values[0] == "weapon")
                {
                    if (values.size() != 12)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawPrototype = number<std::uint64_t>(values[1]);
                    const auto prototype = rawPrototype ? ItemPrototypeId::fromValue(*rawPrototype) : std::nullopt;
                    const auto rawSkill = number<std::uint8_t>(values[2]);
                    std::array<float, 8> parsed{};
                    for (std::size_t index = 0; index < parsed.size(); ++index)
                    {
                        const auto value = finiteFloat(values[index + 3]);
                        if (!value)
                            return error(CombatContentErrorCode::InvalidWeaponCatalog, lineNumber);
                        parsed[index] = *value;
                    }
                    const auto normal = boolean(values[11]);
                    if (!prototype || !rawSkill || *rawSkill >= static_cast<std::uint8_t>(MeleeWeaponSkill::Count)
                        || !normal)
                        return error(CombatContentErrorCode::InvalidWeaponCatalog, lineNumber);
                    if (parsed[0] < 0.f || parsed[0] > parsed[1] || parsed[2] < 0.f || parsed[2] > parsed[3]
                        || parsed[4] < 0.f || parsed[4] > parsed[5] || parsed[6] < 0.f || parsed[7] <= 0.f)
                        return error(CombatContentErrorCode::InvalidWeaponCatalog, lineNumber);
                    weaponProfiles.push_back({ *prototype, static_cast<MeleeWeaponSkill>(*rawSkill), parsed[0],
                        parsed[1], parsed[2], parsed[3], parsed[4], parsed[5], parsed[6], parsed[7], *normal });
                    if (weaponProfiles.size() > MaximumItemPrototypes)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                }
                else if (values[0] == "armor")
                {
                    if (values.size() != 4)
                        return error(CombatContentErrorCode::Malformed, lineNumber);
                    const auto rawPrototype = number<std::uint64_t>(values[1]);
                    const auto prototype = rawPrototype ? ItemPrototypeId::fromValue(*rawPrototype) : std::nullopt;
                    const auto rawSkill = number<std::uint8_t>(values[2]);
                    const auto baseArmor = finiteFloat(values[3]);
                    if (!prototype || !rawSkill || *rawSkill > static_cast<std::uint8_t>(ArmorSkill::HeavyArmor)
                        || !baseArmor || *baseArmor < 0.f)
                        return error(CombatContentErrorCode::InvalidWeaponCatalog, lineNumber);
                    armorProfiles.push_back({ *prototype, static_cast<ArmorSkill>(*rawSkill), *baseArmor });
                    if (armorProfiles.size() > MaximumItemPrototypes)
                        return error(CombatContentErrorCode::TooLarge, lineNumber);
                }
                else
                    return error(CombatContentErrorCode::Malformed, lineNumber);
            }
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }

        if (!declaredManifest || !settings || !securitySettings || !playerTemplate || !skillSettings || !skillRules
            || !randomSeed
            || !magicSettings || !playerMagic)
            return error(CombatContentErrorCode::Malformed);
        playerTemplate->skillSettings = *skillSettings;
        playerTemplate->skillRules = *skillRules;
        playerTemplate->magicDefense = *playerMagic;
        for (auto& state : playerTemplate->skillProgression)
            state.requirementFactor = skillSettings->miscellaneousFactor;
        if (*declaredManifest != manifest.id() || items.contentManifestId() != manifest.id()
            || actors.contentManifestId() != manifest.id())
            return error(CombatContentErrorCode::ManifestMismatch);
        std::ranges::sort(actorStates, {}, &CanonicalActorCombatState::actorId);
        std::ranges::sort(actorAttacks, {}, &ActorAttackDeclaration::actorId);
        if (actorStates.size() != actors.entries().size())
            return error(CombatContentErrorCode::InvalidActorSet);
        for (std::size_t index = 0; index < actorStates.size(); ++index)
            if (actorStates[index].actorId != actors.entries()[index].actorId)
                return error(CombatContentErrorCode::InvalidActorSet);
        if (!actorAttacks.empty() && actorAttacks.size() != actorStates.size())
            return error(CombatContentErrorCode::InvalidActorSet);
        for (std::size_t index = 0; index < actorAttacks.size(); ++index)
        {
            if (actorAttacks[index].actorId != actorStates[index].actorId
                || (index != 0 && actorAttacks[index - 1].actorId == actorAttacks[index].actorId))
                return error(CombatContentErrorCode::InvalidActorSet);
            actorStates[index].attacker = actorAttacks[index].attacker;
            actorStates[index].naturalWeapon = actorAttacks[index].weapon;
            actorStates[index].attackReachQuanta = actorAttacks[index].reachQuanta;
        }
        std::vector<DirectActorMagicProfile> actorMagic;
        actorMagic.reserve(actorMagicProfiles.size());
        for (auto& [actorId, profile] : actorMagicProfiles)
        {
            auto state = std::ranges::lower_bound(actorStates, actorId, {}, &CanonicalActorCombatState::actorId);
            if (state == actorStates.end() || state->actorId != actorId)
                return error(CombatContentErrorCode::InvalidMagicCatalog);
            state->magicDefense = profile.defense;
            actorMagic.push_back(std::move(profile));
        }
        auto weapons = MeleeWeaponCatalog::create(items, weaponProfiles, armorProfiles);
        if (!weapons)
            return error(CombatContentErrorCode::InvalidWeaponCatalog);
        auto magic = DirectMagicCatalog::create(
            manifest.id(), items, *magicSettings, enchantmentProfiles, equipmentMagicProfiles, actorMagic,
            trapMagicProfiles);
        if (!magic)
            return error(CombatContentErrorCode::InvalidMagicCatalog);
        const auto streamKey = RandomStreamKey::fromValues(5, 0);
        if (!streamKey)
            return error(CombatContentErrorCode::InvalidWorld);
        auto world = createCanonicalCombatWorld(
            {}, actorStates, Xoshiro256StarStar::fromWorldSeed(*randomSeed, *streamKey).snapshot());
        auto* created = std::get_if<CanonicalCombatWorld>(&world);
        if (!created)
            return error(CombatContentErrorCode::InvalidWorld);
        CanonicalCombatWorld validation = *created;
        if (!validation.ensurePlayer(*PlayerId::fromValue(1), *playerTemplate, 0))
            return error(CombatContentErrorCode::InvalidPlayerTemplate);
        return CombatContent{ *settings, *securitySettings, *playerTemplate, std::move(*weapons),
            std::move(*magic), std::move(*created) };
    }
    catch (...)
    {
        return error(CombatContentErrorCode::Unavailable);
    }

    std::string describeCombatContentError(CombatContentError value)
    {
        constexpr std::array names{ "unavailable", "too large", "malformed", "manifest mismatch", "invalid settings",
            "invalid player template", "invalid actor set", "invalid weapon catalog", "invalid magic catalog",
            "invalid world" };
        std::string result = names[static_cast<std::size_t>(value.code)];
        if (value.line != 0)
            result += " at line " + std::to_string(value.line);
        return result;
    }
}
