#include <numbers>
#include "inventory_service.hpp"
#include "actor_inventory.hpp"
#include "actor_campaign.hpp"
#include "magic_runtime.hpp"
#include "ai_magic.hpp"
#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/crimeresult.hpp>
#include <apps/openmw/mwmechanics/werewolfstats.hpp>
#include "faction_scripts.hpp"
#include <apps/openmw/mwmechanics/airating.hpp>
#include <apps/openmw/mwmechanics/aitimer.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/inventoryitem.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containeradd.hpp>
#include <apps/openmw/mwclass/armor.hpp>
#include <apps/openmw/mwclass/creature.hpp>
#include <apps/openmw/mwclass/npcmovement.hpp>
#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/drawstate.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwmechanics/spellresistance.hpp>
#include <apps/openmw/mwmechanics/spelleffects.hpp>
#include <apps/openmw/mwmechanics/spells.hpp>
#include <apps/openmw/mwmechanics/activespells.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/statstate.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/constants.hpp>
#include <tes3mp/melee_combat.hpp>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <set>
#include <cstdio>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        const ESM::SpellList& actorSpells(const MWWorld::Ptr& actor)
        {
            return actor.getType() == ESM::NPC::sRecordId ? actor.get<ESM::NPC>()->mBase->mSpells
                : actor.get<ESM::Creature>()->mBase->mSpells;
        }

        constexpr uint64_t PhysicalAttackRetryTicks = 64;

        uint64_t spellRecordId(ESM::RefId id);
        const ESM::Enchantment* enchantmentBySource(const MWWorld::ESMStore& content, uint64_t source);

        bool rangedWeapon(const ESM::Weapon* weapon, bool extended)
        {
            return weapon && (weapon->mData.mType == ESM::Weapon::MarksmanBow
                || (extended && (weapon->mData.mType == ESM::Weapon::MarksmanCrossbow
                    || weapon->mData.mType == ESM::Weapon::MarksmanThrown)));
        }

        bool rangedSources(const ESM::Weapon& weapon, const ESM::Weapon& ammunition,
            const MWWorld::ESMStore& store, const InventoryServiceBinding& binding)
        {
            const auto* type = MWMechanics::getWeaponType(weapon.mData.mType);
            if (!(type->mWeaponClass == ESM::WeaponType::Thrown ? weapon.mId == ammunition.mId
                    : type->mWeaponClass == ESM::WeaponType::Ranged && type->mAmmoType == ammunition.mData.mType))
                return false;
            if (ammunition.mEnchant.empty()) return true;
            if (!binding.mExpandedEffects) return false;
            const auto* enchantment = store.get<ESM::Enchantment>().search(ammunition.mEnchant);
            const uint64_t source = spellRecordId(ammunition.mEnchant);
            const auto effects = enchantment && enchantment->mData.mType == ESM::Enchantment::WhenStrikes
                ? prepareInstantEffects(enchantment->mEffects, store, binding.mActorEffectLifecycle,
                    binding.mExpandedEffects, false, binding.mSpecialConditions, binding.mMovementEffects)
                : std::nullopt;
            return source && enchantmentBySource(store, source) == enchantment && effects;
        }

        const ESM::ObjectState* equippedAmmunition(const PlainEquipmentValues& values,
            const MWWorld::ESMStore& store, const ESM::Weapon& weapon, const InventoryServiceBinding& binding)
        {
            const bool thrown = MWMechanics::getWeaponType(weapon.mData.mType)->mWeaponClass == ESM::WeaponType::Thrown;
            const auto slot = values.mSlots[thrown ? MWWorld::InventoryStore::Slot_CarriedRight
                : MWWorld::InventoryStore::Slot_Ammunition];
            if (!slot.isSet()) return nullptr;
            const auto item = std::ranges::find(values.mObjects, slot,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == values.mObjects.end() || item->mRef.mCount <= 0
                || store.find(item->mRef.mRefID) != ESM::Weapon::sRecordId) return nullptr;
            const auto* record = store.get<ESM::Weapon>().find(item->mRef.mRefID);
            if (!rangedSources(weapon, *record, store, binding)) return nullptr;
            return &*item;
        }

        uint64_t spellRecordId(ESM::RefId id)
        {
            if (!id.is<ESM::StringRefId>()) return 0;
            uint64_t hash = 14695981039346656037ull;
            for (unsigned char c : id.getRefIdString())
            {
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c >= 128) return 0;
                hash = (hash ^ c) * 1099511628211ull;
            }
            return hash;
        }

        const ESM::Enchantment* enchantmentBySource(const MWWorld::ESMStore& content, uint64_t source)
        {
            const ESM::Enchantment* result = nullptr;
            const auto& records = content.get<ESM::Enchantment>();
            for (auto it = records.begin(); it != records.end(); ++it)
                if (spellRecordId(it->mId) == source)
                {
                    if (result) return nullptr;
                    result = &*it;
                }
            return result;
        }

        const ESM::Spell* spellBySource(const MWWorld::ESMStore& content, uint64_t source)
        {
            const ESM::Spell* result = nullptr;
            for (const auto& spell : content.get<ESM::Spell>())
                if (spellRecordId(spell.mId) == source)
                {
                    if (result) throw std::invalid_argument("Native spell identity ambiguous");
                    result = &spell;
                }
            return result;
        }

        class SealedCommitter final : public EquipmentSessionCommitter
        {
            EquipmentSessionCommitter& mSink;
            const EquipmentBytes& mImage;
        public:
            SealedCommitter(EquipmentSessionCommitter& sink, const EquipmentBytes& image) : mSink(sink), mImage(image) {}
            PersistenceResult commit(std::span<const char>) noexcept override { return mSink.commit(mImage); }
        };
        bool fatigueKnockout(const MWMechanics::CreatureStats& stats, bool stockBase)
        {
            return MWMechanics::isFatigueKnockout(stockBase ? stats.getFatigue().getBase() : 1.f,
                stats.getFatigue().getCurrent());
        }
        ActorCampaignCombat initialCombat(std::span<const ESM::RefId> actors,
            const MWWorld::ESMStore& content, uint32_t seed, bool stockBase,
            std::span<const uint64_t> placements = {})
        {
            static_assert(ActorCampaignCombat::StatCount == ESM::Attribute::Length + 3 + ESM::Skill::Length);
            if (actors.size() < 3 || actors.size() > 10
                || (!placements.empty() && placements.size() != actors.size() - 2))
                throw std::invalid_argument("Native combat actor domain invalid");
            ActorCampaignCombat result;
            result.npcPlacements.assign(placements.begin(), placements.end());
            result.actors.resize(actors.size());
            result.knockedDown.resize(actors.size());
            result.knockoutFrame.resize(actors.size());
            result.hitKnockdown.resize(actors.size());
            result.hitRecoveryTicks.resize(actors.size());
            result.bodyAction.resize(actors.size());
            result.hitGroup.resize(actors.size());
            Misc::Rng::Generator rng{seed};
            result.rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            const float magickaMultiplier = content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat();
            if (!std::isfinite(magickaMultiplier) || magickaMultiplier < 0 || magickaMultiplier > 1000)
                throw std::invalid_argument("Native combat magicka multiplier invalid");
            for (size_t actor = 0; actor < actors.size(); ++actor)
            {
                MWMechanics::NpcStats stats(content);
                if (const auto* creature = content.get<ESM::Creature>().search(actors[actor]))
                {
                    stats.initializeBaseStats(*creature, magickaMultiplier);
                    for (int i = 0; i < ESM::Skill::Length; ++i)
                    {
                        const auto skill = ESM::Skill::indexToRefId(i);
                        stats.getSkill(skill).setBase(MWClass::Creature::getSkill(*creature, skill, content));
                    }
                }
                else
                {
                    const auto& base = *content.get<ESM::NPC>().find(actors[actor]);
                    if (base.mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS)
                        stats.initializeAutoStats(base, content, magickaMultiplier);
                    else stats.initializeExplicitStats(base, magickaMultiplier);
                }
                size_t index = 0;
                const auto capture = [&](const auto& stat) {
                    ESM::StatState<float> value;
                    stat.writeState(value);
                    result.actors[actor][index++] = {value.mBase, value.mMod, value.mCurrent,
                        value.mDamage, value.mProgress};
                };
                for (int i = 0; i < ESM::Attribute::Length; ++i)
                    capture(stats.getAttribute(ESM::Attribute::indexToRefId(i)));
                for (int i = 0; i < 3; ++i) capture(stats.getDynamic(i));
                for (int i = 0; i < ESM::Skill::Length; ++i)
                    capture(stats.getSkill(ESM::Skill::indexToRefId(i)));
                if (index != result.actors[actor].size())
                    throw std::invalid_argument("Native combat stat shape invalid");
                result.knockedDown[actor] = stats.getHealth().getCurrent() > 0
                    && fatigueKnockout(stats, stockBase);
            }
            return result;
        }
        struct ActorDisposition
        {
            int fight = 0;
            int flee = 0;
            float charm = 0;
            bool calm = false;
            bool commanded = false;
            bool fightModified = false;
        };
        std::pair<bool, bool> aiActorKind(const MWWorld::Ptr& actor)
        {
            if (actor.getType() == ESM::NPC::sRecordId) return {true, false};
            if (actor.getType() != ESM::Creature::sRecordId)
                throw std::invalid_argument("Native AI actor type invalid");
            return {false, actor.get<ESM::Creature>()->mBase->mData.mType == ESM::Creature::Undead};
        }
        ActorDisposition committedDisposition(const MWWorld::ESMStore& content, ESM::RefId base,
            std::span<const ActorCampaignTimedEffect> effects, size_t actor)
        {
            const auto* npc = content.get<ESM::NPC>().search(base);
            const auto* creature = content.get<ESM::Creature>().search(base);
            if (!npc && !creature) throw std::invalid_argument("Native AI actor base missing");
            ActorDisposition result;
            result.fight = npc ? npc->mAiData.mFight : creature->mAiData.mFight;
            result.flee = npc ? npc->mAiData.mFlee : creature->mAiData.mFlee;
            const int level = npc ? npc->mNpdt.mLevel : creature->mData.mLevel;
            int fightModifier = 0, fleeModifier = 0;
            for (const auto& effect : effects)
            {
                if (effect.actor != actor || effect.magnitude <= 0) continue;
                const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
                if (!aiDispositionEffect(id)) continue;
                if (!MWMechanics::validAiEffectTarget(id, npc != nullptr, false,
                        creature && creature->mData.mType == ESM::Creature::Undead, true)) continue;
                if (const auto delta = MWMechanics::aiDispositionDelta(id, effect.magnitude))
                {
                    auto& modifier = delta->setting == MWMechanics::AiSetting::Fight
                        ? fightModifier : fleeModifier;
                    modifier = static_cast<int>(modifier + delta->modifier);
                    if (delta->setting == MWMechanics::AiSetting::Fight) result.fightModified = true;
                }
                if (id == ESM::MagicEffect::CalmHumanoid || id == ESM::MagicEffect::CalmCreature)
                    result.calm = true;
                if (id == ESM::MagicEffect::Charm) result.charm += effect.magnitude;
                if ((id == ESM::MagicEffect::CommandHumanoid || id == ESM::MagicEffect::CommandCreature)
                    && effect.magnitude >= level) result.commanded = true;
            }
            result.fight += fightModifier; result.flee += fleeModifier;
            return result;
        }
        ESM::StatState<float> combatStat(const std::array<float, 5>& fields)
        {
            ESM::StatState<float> value;
            value.mBase = fields[0]; value.mMod = fields[1]; value.mCurrent = fields[2];
            value.mDamage = fields[3]; value.mProgress = fields[4];
            return value;
        }
        bool absorbStat(ESM::RefId id)
        { return id == ESM::MagicEffect::AbsorbAttribute || id == ESM::MagicEffect::AbsorbSkill; }
        int drainDynamic(ESM::RefId id)
        {
            if (id == ESM::MagicEffect::DrainHealth) return 0;
            if (id == ESM::MagicEffect::DrainMagicka) return 1;
            if (id == ESM::MagicEffect::DrainFatigue) return 2;
            return -1;
        }
        int absorbDynamic(ESM::RefId id)
        {
            if (id == ESM::MagicEffect::AbsorbHealth) return 0;
            if (id == ESM::MagicEffect::AbsorbMagicka) return 1;
            if (id == ESM::MagicEffect::AbsorbFatigue) return 2;
            return -1;
        }
        bool hasParalysis(std::span<const ActorCampaignTimedEffect> effects, size_t actor)
        {
            return std::ranges::any_of(effects, [actor](const auto& effect) {
                return effect.actor == actor && effect.magnitude > 0
                    && effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::Paralyze));
            });
        }
        void applyEffectStats(MWMechanics::NpcStats& stats, const MWWorld::ESMStore& content,
            std::span<const ActorCampaignTimedEffect> effects, size_t actor, float direction)
        {
            for (const auto& effect : effects)
            {
                if (!effect.argument || permanentStatEffect(ESM::MagicEffect::indexToRefId(int(effect.effectIndex)))) continue;
                const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
                const float magnitude = effect.magnitude * direction;
                const float multiplier = content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat();
                const bool attribute = effect.argument <= 8;
                const auto argument = attribute ? ESM::Attribute::indexToRefId(int(effect.argument) - 1)
                    : ESM::Skill::indexToRefId(int(effect.argument) - 9);
                if (effect.actor == actor)
                {
                    if (id == ESM::MagicEffect::FortifyAttribute)
                        MWMechanics::modifyFortifyAttribute(stats, argument, magnitude, false, multiplier);
                    else if (id == ESM::MagicEffect::FortifySkill)
                        MWMechanics::modifyFortifySkill(stats, argument, magnitude);
                    else if (id == ESM::MagicEffect::DrainAttribute || id == ESM::MagicEffect::AbsorbAttribute)
                        MWMechanics::modifyAttributeDamage(stats, argument, magnitude, multiplier);
                    else if (id == ESM::MagicEffect::DrainSkill || id == ESM::MagicEffect::AbsorbSkill)
                        MWMechanics::modifySkillDamage(stats, argument, magnitude);
                    stats.getMagicEffects().add(MWMechanics::EffectKey(id, argument), MWMechanics::EffectParam(magnitude));
                }
                if (absorbStat(id) && effect.beneficiary == actor + 1)
                {
                    if (attribute) MWMechanics::modifyFortifyAttribute(stats, argument, magnitude, false, multiplier);
                    else MWMechanics::modifyFortifySkill(stats, argument, magnitude);
                }
            }
        }
        MWMechanics::NpcStats loadCombatStats(const MWWorld::ESMStore& content,
            const std::array<std::array<float, 5>, ActorCampaignCombat::StatCount>& fields,
            std::span<const ActorCampaignTimedEffect> effects = {}, size_t actor = 0)
        {
            MWMechanics::NpcStats stats(content);
            size_t index = 0;
            for (int i = 0; i < ESM::Attribute::Length; ++i)
            {
                MWMechanics::AttributeValue value;
                value.readState(combatStat(fields[index++]));
                stats.setAttribute(ESM::Attribute::indexToRefId(i), value, 0.f);
            }
            index += 3;
            for (int i = 0; i < ESM::Skill::Length; ++i)
            {
                MWMechanics::SkillValue value;
                value.readState(combatStat(fields[index++]));
                stats.setSkill(ESM::Skill::indexToRefId(i), value);
            }
            for (const auto& effect : effects)
                if (effect.actor == actor && effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::FortifyMaximumMagicka)))
                    stats.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::FortifyMaximumMagicka),
                        MWMechanics::EffectParam(effect.magnitude));
            applyEffectStats(stats, content, effects, actor, 1.f);
            // Saved resources already include the current equipment's derived maxima.
            // Loading overlays must not scale them again; equipment changes do that once.
            for (int i = 0; i < 3; ++i)
            {
                MWMechanics::DynamicStat<float> value;
                value.readState(combatStat(fields[ESM::Attribute::Length + i]));
                stats.setDynamic(i, value, MWWorld::TimeStamp{});
            }
            return stats;
        }
        bool timedDamage(ESM::RefId id)
        {
            return id == ESM::MagicEffect::DamageHealth || id == ESM::MagicEffect::DamageMagicka
                || id == ESM::MagicEffect::DamageFatigue || id == ESM::MagicEffect::FireDamage
                || id == ESM::MagicEffect::ShockDamage || id == ESM::MagicEffect::FrostDamage
                || id == ESM::MagicEffect::Poison || id == ESM::MagicEffect::SunDamage;
        }
        bool timedRestore(ESM::RefId id)
        {
            return id == ESM::MagicEffect::RestoreHealth
                || id == ESM::MagicEffect::RestoreMagicka
                || id == ESM::MagicEffect::RestoreFatigue;
        }

        struct CandidateCondition
        {
            EquipmentRuntime::EquippedWeaponCondition item;
            float remainder;
        };
        std::optional<CandidateCondition> equippedCandidateCondition(const PlainEquipmentValues& values,
            int slot, const MWWorld::ESMStore& store)
        {
            if (slot < 0 || slot >= MWWorld::InventoryStore::Slots || !values.mSlots[slot].isSet()) return {};
            const auto found = std::ranges::find(values.mObjects, values.mSlots[slot],
                [](const auto& object) { return object.mRef.mRefNum; });
            if (found == values.mObjects.end() || found->mRef.mCount <= 0)
                throw std::invalid_argument("Native equipped candidate item missing");
            const auto type = store.find(found->mRef.mRefID);
            int maximum = 0;
            if (slot == MWWorld::InventoryStore::Slot_CarriedRight && type == ESM::Weapon::sRecordId)
            {
                const auto& weapon = *store.get<ESM::Weapon>().find(found->mRef.mRefID);
                if (!(MWMechanics::getWeaponType(weapon.mData.mType)->mFlags & ESM::WeaponType::HasHealth)) return {};
                maximum = weapon.mData.mHealth;
            }
            else if (slot != MWWorld::InventoryStore::Slot_CarriedRight && type == ESM::Armor::sRecordId)
                maximum = store.get<ESM::Armor>().find(found->mRef.mRefID)->mData.mHealth;
            else return {};
            const int condition = found->mRef.mChargeInt == -1 ? maximum : found->mRef.mChargeInt;
            if (condition < 0 || condition > maximum || !std::isfinite(found->mRef.mChargeIntRemainder)
                || found->mRef.mChargeIntRemainder <= -1.f || found->mRef.mChargeIntRemainder >= 1.f)
                throw std::invalid_argument("Native equipped candidate condition invalid");
            return CandidateCondition{{found->mRef.mRefNum, condition}, found->mRef.mChargeIntRemainder};
        }
        uint64_t gameTimeMilliseconds(const CanonicalWorldState* world)
        {
            if (!world || !world->time().daysPassed
                || world->time().millisecondsSinceMidnight >= 86400000)
                throw std::invalid_argument("Native special condition requires a valid game clock");
            return uint64_t(*world->time().daysPassed) * 86400000
                + world->time().millisecondsSinceMidnight;
        }
        bool sameCondition(const ActorCampaignCombat::ConditionSource& condition,
            size_t actor, uint64_t source)
        { return condition.actor == actor && condition.source == source; }
        bool corprusStatOverlay(ESM::RefId id)
        {
            return id == ESM::MagicEffect::DrainAttribute || id == ESM::MagicEffect::DrainSkill
                || id == ESM::MagicEffect::FortifyAttribute || id == ESM::MagicEffect::FortifySkill;
        }
        void applyCorprusOnce(MWMechanics::NpcStats& target, const ActorCampaignTimedEffect& effect,
            const MWWorld::ESMStore& content, bool uncappedFatigue)
        {
            const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
            if (permanentStatEffect(id))
            {
                ESM::ENAMstruct entry{}; entry.mEffectID = id;
                if (effect.argument <= 8) entry.mAttribute = ESM::Attribute::indexToRefId(int(effect.argument) - 1);
                else entry.mSkill = ESM::Skill::indexToRefId(int(effect.argument) - 9);
                applyPermanentStatEffect(target, entry, effect.magnitude, content);
            }
            else if (timedDamage(id) || timedRestore(id))
            {
                const int stat = id == ESM::MagicEffect::DamageMagicka || id == ESM::MagicEffect::RestoreMagicka ? 1
                    : id == ESM::MagicEffect::DamageFatigue || id == ESM::MagicEffect::RestoreFatigue ? 2 : 0;
                const MWWorld::TimeStamp deathTime{};
                MWMechanics::adjustDynamicStatValue(target, stat,
                    timedRestore(id) ? effect.magnitude : -effect.magnitude,
                    stat == 2 && uncappedFatigue, false, &deathTime);
            }
        }
        bool supportedTimedStatus(ESM::RefId id)
        {
            return supportedCombatModifier(id) || id == ESM::MagicEffect::ResistMagicka
                || id == ESM::MagicEffect::ResistNormalWeapons
                || id == ESM::MagicEffect::WeaknessToNormalWeapons
                || id == ESM::MagicEffect::ResistFire
                || id == ESM::MagicEffect::ResistFrost
                || id == ESM::MagicEffect::ResistShock
                || id == ESM::MagicEffect::ResistPoison
                || id == ESM::MagicEffect::WeaknessToFire
                || id == ESM::MagicEffect::WeaknessToFrost
                || id == ESM::MagicEffect::WeaknessToShock
                || id == ESM::MagicEffect::WeaknessToPoison
                || id == ESM::MagicEffect::WeaknessToMagicka;
        }
        void addTimedResistance(MWMechanics::NpcStats& stats,
            std::span<const ActorCampaignTimedEffect> effects, size_t actor)
        {
            for (const auto& effect : effects)
                if (effect.actor == actor)
                {
                    if (effect.argument) continue; // Attribute/skill overlay owns these.
                    const auto id = effect.effectIndex || effect.source
                        ? ESM::MagicEffect::indexToRefId(int(effect.effectIndex))
                        : ESM::MagicEffect::ResistMagicka;
                    if (!timedDamage(id) && !timedRestore(id) && id != ESM::MagicEffect::FortifyMaximumMagicka
                        && id != ESM::MagicEffect::DisintegrateWeapon
                        && id != ESM::MagicEffect::DisintegrateArmor)
                        stats.getMagicEffects().add(MWMechanics::EffectKey(id),
                            MWMechanics::EffectParam(effect.magnitude));
                }
        }
        void stageTimedResistance(const PreparedInstantEffects& plan, int range, size_t actor,
            uint64_t tick, std::vector<ActorCampaignTimedEffect>& effects)
        {
            for (const auto& effect : plan.effects)
            {
                if (effect.mRange != range || effect.mEffectID != ESM::MagicEffect::ResistMagicka) continue;
                if (effects.size() >= MaximumActorTimedEffects
                    || tick > UINT64_MAX - uint64_t(effect.mDuration) * 30)
                    throw std::invalid_argument("Native timed effect capacity or deadline exhausted");
                effects.push_back({actor, float(effect.mMagnMin), tick + uint64_t(effect.mDuration) * 30});
            }
        }
        void stageActorEffects(const PreparedInstantEffects& plan, int range, size_t actor,
            MWMechanics::NpcStats& target, uint64_t tick, ActorCasterIdentity caster, uint64_t source,
            uint64_t sourceKind, Misc::Rng::Generator& rng, const MWWorld::ESMStore& content,
            std::vector<ActorCampaignTimedEffect>& effects, bool lifecycle, uint64_t ordinal = 0)
        {
            if (!lifecycle)
            {
                stageTimedResistance(plan, range, actor, tick, effects);
                return;
            }
            for (const auto& effect : plan.effects)
            {
                if (effect.mRange != range || effect.mDuration == 0) continue;
                if (effects.size() >= MaximumActorTimedEffects
                    || tick > UINT64_MAX - uint64_t(effect.mDuration) * 30)
                    throw std::invalid_argument("Native actor effect capacity or deadline exhausted");
                const auto* magic = content.get<ESM::MagicEffect>().search(effect.mEffectID);
                if (!magic || !source || !caster.id) throw std::invalid_argument("Native actor effect source invalid");
                const float magnitude = float(effect.mMagnMin + (effect.mMagnMin == effect.mMagnMax ? 0
                    : Misc::Rng::rollDice(effect.mMagnMax - effect.mMagnMin + 1, rng)));
                const float resistance = MWMechanics::getEffectResistance(effect.mEffectID, target,
                    100.f, target.getFatigueTerm(content),
                    bool(magic->mData.mFlags & ESM::MagicEffect::NoMagnitude), rng);
                const float applied = magnitude * (1.f - resistance / 100.f);
                if (!std::isfinite(applied) || applied < 0 || applied > 100000.f)
                    throw std::invalid_argument("Native actor effect magnitude invalid");
                if (applied == 0) continue;
                effects.push_back({actor, applied, tick + uint64_t(effect.mDuration) * 30,
                    uint64_t(ESM::MagicEffect::refIdToIndex(effect.mEffectID)), caster.id, source,
                    sourceKind, resistance, tick, uint64_t(effect.mDuration) * 30,
                    !effect.mAttribute.empty() ? uint64_t(ESM::Attribute::refIdToIndex(effect.mAttribute) + 1)
                        : !effect.mSkill.empty() ? uint64_t(ESM::Skill::refIdToIndex(effect.mSkill) + 9) : 0,
                    ordinal, caster.kind, caster.life});
                if (effects.back().argument) applyEffectStats(target, content, {&effects.back(), 1}, actor, 1.f);
                if (!effects.back().argument && !timedDamage(effect.mEffectID) && !timedRestore(effect.mEffectID))
                    target.getMagicEffects().add(MWMechanics::EffectKey(effect.mEffectID),
                        MWMechanics::EffectParam(applied));
            }
        }
        InstantSpellResult resolveActorEffects(const PreparedInstantEffects& plan, int range, size_t actor,
            MWMechanics::NpcStats& target, uint64_t tick, ActorCasterIdentity caster, uint64_t source,
            uint64_t sourceKind, Misc::Rng::Generator& rng, const MWWorld::ESMStore& content,
            std::vector<ActorCampaignTimedEffect>& effects, bool lifecycle, bool uncappedDamageFatigue,
            std::span<const uint64_t> ordinals = {})
        {
            if (!lifecycle)
            {
                auto result = applyInstantEffects(plan, range, target, &rng, &content, uncappedDamageFatigue);
                stageTimedResistance(plan, range, actor, tick, effects);
                return result;
            }
            const float beforeHealth = target.getHealth().getCurrent();
            const float beforeMagicka = target.getMagicka().getCurrent();
            const float beforeFatigue = target.getFatigue().getCurrent();
            for (size_t ordinal = 0; ordinal < plan.effects.size(); ++ordinal)
            {
                const auto& effect = plan.effects[ordinal];
                if (effect.mRange != range) continue;
                const PreparedInstantEffects single{{effect}};
                if (effect.mDuration)
                    stageActorEffects(single, range, actor, target, tick, caster, source,
                        sourceKind, rng, content, effects, true, ordinals.empty() ? ordinal : ordinals[ordinal]);
                else
                    applyInstantEffects(single, range, target, &rng, &content, uncappedDamageFatigue);
            }
            return {target.getHealth().getCurrent() - beforeHealth,
                target.getMagicka().getCurrent() - beforeMagicka,
                target.getFatigue().getCurrent() - beforeFatigue};
        }
        void saveCombatStats(std::array<std::array<float, 5>, ActorCampaignCombat::StatCount>& fields,
            const MWMechanics::NpcStats& stats,
            std::span<const ActorCampaignTimedEffect> effects = {}, size_t actor = 0)
        {
            size_t index = 0;
            const auto capture = [&](const auto& stat) {
                ESM::StatState<float> value;
                stat.writeState(value);
                const std::array<float, 5> captured{value.mBase, value.mMod, value.mCurrent, value.mDamage, value.mProgress};
                if (std::ranges::any_of(captured, [](float field) { return !std::isfinite(field) || std::abs(field) > 1'000'000; }))
                    throw std::invalid_argument("Native combat stat exceeds durable bounds");
                fields[index++] = captured;
            };
            for (int i = 0; i < ESM::Attribute::Length; ++i)
                capture(stats.getAttribute(ESM::Attribute::indexToRefId(i)));
            for (int i = 0; i < 3; ++i) capture(stats.getDynamic(i));
            for (int i = 0; i < ESM::Skill::Length; ++i)
                capture(stats.getSkill(ESM::Skill::indexToRefId(i)));
            for (const auto& effect : effects)
            {
                if (!effect.argument || permanentStatEffect(ESM::MagicEffect::indexToRefId(int(effect.effectIndex)))) continue;
                const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
                const size_t stat = effect.argument <= 8 ? size_t(effect.argument - 1) : size_t(effect.argument + 2);
                if (effect.actor == actor)
                {
                    const size_t field = id == ESM::MagicEffect::DrainAttribute || id == ESM::MagicEffect::DrainSkill
                        || absorbStat(id) ? 3 : 1;
                    fields[stat][field] -= effect.magnitude;
                }
                if (absorbStat(id) && effect.beneficiary == actor + 1) fields[stat][1] -= effect.magnitude;
            }
        }
        ItemStackId wireId(ESM::RefNum id);

        bool reconcileConstants(const MWWorld::PlainEquipmentValues& values, size_t actor, ActorCasterIdentity caster,
            uint64_t tick, const MWWorld::ESMStore& content, bool general,
            std::vector<ActorCampaignTimedEffect>& effects, Misc::Rng::Generator* rng,
            bool expanded, bool specialConditions, bool movementEffects, bool aiDecisions,
            bool npcActor, bool undeadActor, bool levitationEnabled = true)
        {
            std::vector<ActorCampaignTimedEffect> desired;
            bool changed = false;
            for (size_t slot = 0; slot < values.mSlots.size(); ++slot)
            {
                const auto source = values.mSlots[slot];
                if (!source.isSet()) continue;
                const auto item = std::ranges::find(values.mObjects, source,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == values.mObjects.end())
                    throw std::invalid_argument("Native constant source item missing");
                const auto record = MWWorld::inventoryItemRecord(content, item->mRef.mRefID);
                if (!record.mConstant) continue;
                PreparedInstantEffects plan;
                if (general)
                {
                    auto prepared = prepareConstantEffects(record.mEnchant, content, expanded, specialConditions,
                        movementEffects, aiDecisions);
                    if (!prepared) throw std::invalid_argument("Native constant effects unsupported");
                    plan = std::move(*prepared);
                }
                else
                {
                    if (slot != MWWorld::InventoryStore::Slot_Shirt)
                        throw std::invalid_argument("Legacy constant slot unsupported");
                    const auto magnitude = MWMechanics::constantFortifyLuckMagnitude(content, record.mEnchant);
                    plan.effects.push_back({ESM::MagicEffect::FortifyAttribute, {}, ESM::Attribute::Luck,
                        ESM::RT_Self, 0, 0, int(magnitude), int(magnitude)});
                }
                for (size_t ordinal = 0; ordinal < plan.effects.size(); ++ordinal)
                {
                    const auto& entry = plan.effects[ordinal];
                    if (!levitationEnabled && entry.mEffectID == ESM::MagicEffect::Levitate) continue;
                    if (aiDispositionEffect(entry.mEffectID)
                        && !MWMechanics::validAiEffectTarget(entry.mEffectID,
                            actor < 2 || npcActor, actor < 2, undeadActor, true)) continue;
                    const bool noMagnitude = (entry.mEffectID == ESM::MagicEffect::Invisibility
                        || entry.mEffectID == ESM::MagicEffect::WaterBreathing
                        || entry.mEffectID == ESM::MagicEffect::WaterWalking)
                        && (content.get<ESM::MagicEffect>().find(entry.mEffectID)->mData.mFlags
                            & ESM::MagicEffect::NoMagnitude);
                    const uint64_t argument = !entry.mAttribute.empty()
                        ? uint64_t(ESM::Attribute::refIdToIndex(entry.mAttribute) + 1)
                        : !entry.mSkill.empty() ? uint64_t(ESM::Skill::refIdToIndex(entry.mSkill) + 9) : 0;
                    const auto previous = std::ranges::find_if(effects, [&](const auto& effect) {
                        return effect.actor == actor && effect.sourceKind == 3
                            && effect.source == wireId(source).value() && effect.ordinal == ordinal;
                    });
                    ActorCampaignTimedEffect effect{actor, 0, UINT64_MAX,
                        uint64_t(ESM::MagicEffect::refIdToIndex(entry.mEffectID)), caster.id,
                        wireId(source).value(), 3, 0, tick, 0, argument, ordinal, caster.kind, caster.life};
                    if (previous != effects.end())
                    {
                        if (previous->effectIndex != effect.effectIndex || previous->argument != argument
                            || previous->caster != caster.id || previous->casterKind != caster.kind
                            || previous->casterLife != caster.life || previous->resistance != 0
                            || previous->durationTicks != 0 || previous->expiresTick != UINT64_MAX
                            || (noMagnitude ? (previous->magnitude != 1.f
                                    && !(entry.mEffectID == ESM::MagicEffect::Invisibility && previous->magnitude == 0.f))
                                : previous->magnitude < entry.mMagnMin || previous->magnitude > entry.mMagnMax)
                            || std::floor(previous->magnitude) != previous->magnitude)
                            throw std::invalid_argument("Native constant effect disagrees with source");
                        effect = *previous;
                    }
                    else if (rng)
                    {
                        changed = true;
                        effect.magnitude = noMagnitude ? 1.f : MWMechanics::rollEffectMagnitude(
                            float(entry.mMagnMin), float(entry.mMagnMax), *rng);
                    }
                    else if (tick) throw std::invalid_argument("Native saved constant effect missing");
                    else continue; // Initial image precedes the first composed tick.
                    desired.push_back(effect);
                }
            }
            if (!rng)
            {
                if (tick == 0 && !desired.empty())
                    throw std::invalid_argument("Native initial image contains constant effects");
                if (std::ranges::count_if(effects, [actor](const auto& effect) {
                        return effect.actor == actor && effect.sourceKind == 3;
                    }) != desired.size())
                    throw std::invalid_argument("Native saved constant source or ordinal invalid");
                return false;
            }
            changed |= std::ranges::count_if(effects, [actor](const auto& effect) {
                return effect.actor == actor && effect.sourceKind == 3;
            }) != desired.size();
            if (!changed) return false;
            std::erase_if(effects, [actor](const auto& effect) {
                return effect.actor == actor && effect.sourceKind == 3;
            });
            if (effects.size() + desired.size() > (general ? MaximumActorTimedEffects : 16))
                throw std::invalid_argument("Native constant effect capacity exhausted");
            effects.insert(effects.end(), desired.begin(), desired.end());
            return true;
        }

        bool reconcilePassiveActorEffects(const MWWorld::Ptr& ptr, size_t actor, ActorCasterIdentity caster,
            uint64_t tick, const MWWorld::ESMStore& content,
            std::vector<ActorCampaignTimedEffect>& effects, Misc::Rng::Generator* rng,
            bool movementEffects, bool aiEffects, bool npcActor, bool undeadActor,
            bool levitationEnabled = true)
        {
            if (!movementEffects && !aiEffects) return false;
            std::vector<ActorCampaignTimedEffect> desired;
            bool changed = false;
            for (const auto id : actorSpells(ptr).mList)
            {
                const auto* spell = content.get<ESM::Spell>().search(id);
                if (!spell || spell->mData.mType != ESM::Spell::ST_Ability) continue;
                const bool relevant = std::ranges::any_of(spell->mEffects.mList, [&](const auto& entry) {
                    return (aiEffects && aiDispositionEffect(entry.mData.mEffectID))
                        || (movementEffects && movementEffect(entry.mData.mEffectID));
                });
                if (!relevant) continue;
                const auto plan = preparePassiveActorEffects(*spell, content, movementEffects, aiEffects);
                const uint64_t source = spellRecordId(id);
                if (!plan || !source || spellBySource(content, source) != spell)
                    throw std::invalid_argument("Native passive AI source unsupported or ambiguous");
                for (size_t ordinal = 0; ordinal < plan->effects.size(); ++ordinal)
                {
                    const auto& entry = plan->effects[ordinal];
                    if (!levitationEnabled && entry.mEffectID == ESM::MagicEffect::Levitate) continue;
                    if (aiDispositionEffect(entry.mEffectID)
                        && !MWMechanics::validAiEffectTarget(entry.mEffectID,
                            actor < 2 || npcActor, actor < 2, undeadActor, true)) continue;
                    const bool noMagnitude = content.get<ESM::MagicEffect>().find(entry.mEffectID)->mData.mFlags
                        & ESM::MagicEffect::NoMagnitude;
                    ActorCampaignTimedEffect effect{actor, 0.f, UINT64_MAX,
                        uint64_t(ESM::MagicEffect::refIdToIndex(entry.mEffectID)), caster.id,
                        source, 5, 0.f, tick, 0, 0, ordinal, caster.kind, caster.life};
                    const auto previous = std::ranges::find_if(effects, [&](const auto& active) {
                        return active.actor == actor && active.sourceKind == 5
                            && active.source == source && active.ordinal == ordinal;
                    });
                    if (previous != effects.end())
                    {
                        if (previous->effectIndex != effect.effectIndex || previous->caster != caster.id
                            || previous->casterKind != caster.kind || previous->casterLife != caster.life
                            || previous->resistance != 0.f || previous->durationTicks != 0
                            || previous->expiresTick != UINT64_MAX || previous->argument
                            || (noMagnitude ? previous->magnitude != 1.f
                                : previous->magnitude < entry.mMagnMin || previous->magnitude > entry.mMagnMax)
                            || std::floor(previous->magnitude) != previous->magnitude)
                            throw std::invalid_argument("Native passive AI effect disagrees with source");
                        effect = *previous;
                    }
                    else if (rng)
                    {
                        changed = true;
                        effect.magnitude = noMagnitude ? 1.f : MWMechanics::rollEffectMagnitude(
                            float(entry.mMagnMin), float(entry.mMagnMax), *rng);
                    }
                    else if (tick) throw std::invalid_argument("Native saved passive AI effect missing");
                    else continue;
                    desired.push_back(effect);
                }
            }
            const auto existing = std::ranges::count_if(effects, [actor](const auto& effect) {
                return effect.actor == actor && effect.sourceKind == 5;
            });
            if (!rng)
            {
                if (existing != desired.size())
                    throw std::invalid_argument("Native saved passive AI membership invalid");
                return false;
            }
            changed |= existing != desired.size();
            if (!changed) return false;
            std::erase_if(effects, [actor](const auto& effect) {
                return effect.actor == actor && effect.sourceKind == 5;
            });
            if (effects.size() + desired.size() > MaximumActorTimedEffects)
                throw std::invalid_argument("Native passive AI effect capacity exhausted");
            effects.insert(effects.end(), desired.begin(), desired.end());
            return true;
        }

        std::optional<ActorCasterIdentity> updateEffectResources(ActorCampaignCombat& combat, size_t actor,
            const MWWorld::ESMStore& content, std::span<const ActorCampaignTimedEffect> previous,
            std::span<const ActorCampaignTimedEffect> current, bool retainKnockout)
        {
            auto stats = loadCombatStats(content, combat.actors[actor], previous, actor);
            const MWWorld::TimeStamp deathTime{};
            std::optional<ActorCasterIdentity> death;
            for (const auto& effect : previous)
                if ((effect.actor == actor || effect.beneficiary == actor + 1) && effect.argument
                    && std::ranges::find(current, effect) == current.end())
                    applyEffectStats(stats, content, {&effect, 1}, actor, -1.f);
            for (const auto& effect : current)
                if ((effect.actor == actor || effect.beneficiary == actor + 1) && effect.argument
                    && std::ranges::find(previous, effect) == previous.end())
                    applyEffectStats(stats, content, {&effect, 1}, actor, 1.f);
            for (const auto& effect : previous)
                if (effect.actor == actor && stats.getHealth().getCurrent() > 0 && std::ranges::find(current, effect) == current.end())
                {
                    if (const int stat = drainDynamic(ESM::MagicEffect::indexToRefId(int(effect.effectIndex))); stat >= 0)
                        MWMechanics::restoreDynamicStat(stats, stat, effect.magnitude);
                    if (const int stat = fortifyDynamicStat(ESM::MagicEffect::indexToRefId(int(effect.effectIndex))); stat >= 0)
                        MWMechanics::adjustDynamicStatValue(stats, stat, -effect.magnitude, true, false, &deathTime);
                    if (stats.getHealth().getCurrent() <= 0)
                        death = ActorCasterIdentity{effect.caster, effect.casterKind, effect.casterLife};
                }
            for (const auto& effect : current)
                if (effect.actor == actor && stats.getHealth().getCurrent() > 0 && std::ranges::find(previous, effect) == previous.end())
                    if (const int stat = fortifyDynamicStat(ESM::MagicEffect::indexToRefId(int(effect.effectIndex))); stat >= 0)
                        MWMechanics::adjustDynamicStatValue(stats, stat, effect.magnitude, false, true);
            const auto maximumMagicka = [actor](auto effects) {
                float sum = 0;
                for (const auto& effect : effects)
                    if (effect.actor == actor && effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::FortifyMaximumMagicka))) sum += effect.magnitude;
                return sum;
            };
            const float maximumDelta = maximumMagicka(current) - maximumMagicka(previous);
            if (maximumDelta != 0)
            {
                stats.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::FortifyMaximumMagicka), MWMechanics::EffectParam(maximumDelta));
                stats.recalculateMagicka(content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat());
            }
            saveCombatStats(combat.actors[actor], stats, current, actor);
            combat.knockedDown[actor] = stats.getHealth().getCurrent() > 0
                && (fatigueKnockout(stats, retainKnockout) || (retainKnockout && combat.knockedDown[actor]));
            return death;
        }

        struct ExpandedEffectResult
        {
            InstantSpellResult target;
            std::vector<std::optional<ActorCasterIdentity>> deaths;
        };
        ExpandedEffectResult resolveExpandedEffects(const PreparedInstantEffects& plan, int range, size_t targetIndex,
            MWMechanics::NpcStats& target, ActorCasterIdentity caster, uint64_t source, uint64_t sourceKind,
            uint64_t tick, ActorCampaignCombat& combat, std::vector<ActorCampaignTimedEffect>& effects,
            std::span<const ActorCasterIdentity> identities, const MWWorld::ESMStore& content,
            Misc::Rng::Generator& rng, bool uncappedFatigue, bool classicReflect,
            std::span<const std::pair<bool, bool>> actorKinds,
            std::span<const uint64_t> ordinals, MWMechanics::NpcStats* externalCaster = nullptr,
            bool ignoreResistance = false, const std::function<float(size_t)>& sunExposure = {},
            const std::function<void(size_t, ESM::RefId, float)>& disintegrate = {},
            const std::function<bool(size_t, ESM::RefId)>& castable = {})
        {
            if (identities.size() != combat.actors.size() || actorKinds.size() != identities.size()
                || targetIndex >= identities.size())
                throw std::invalid_argument("Native effect participant domain invalid");
            for (const auto& active : effects)
                if (active.actor >= identities.size() || active.beneficiary > identities.size())
                    throw std::invalid_argument("Native effect participant outside bound actors");
            std::vector<MWMechanics::NpcStats> owned;
            owned.reserve(combat.actors.size());
            for (size_t i = 0; i < combat.actors.size(); ++i)
                owned.push_back(loadCombatStats(content, combat.actors[i], effects, i));
            std::vector<MWMechanics::NpcStats*> states;
            states.reserve(owned.size());
            for (size_t i = 0; i < owned.size(); ++i)
            {
                states.push_back(&owned[i]);
                addTimedResistance(*states[i], effects, i);
            }
            const auto found = std::ranges::find(identities, caster);
            const size_t casterIndex = size_t(found - identities.begin());
            states[targetIndex] = &target;
            if (externalCaster && casterIndex < states.size()) states[casterIndex] = externalCaster;
            const auto prior = std::array{target.getHealth().getCurrent(), target.getMagicka().getCurrent(), target.getFatigue().getCurrent()};
            const ESM::Spell* spell = nullptr;
            const ESM::Enchantment* enchantment = nullptr;
            if (!sourceKind || sourceKind == 4)
            {
                for (const auto& record : content.get<ESM::Spell>())
                    if (source && spellRecordId(record.mId) == source) { spell = &record; break; }
            }
            else enchantment = enchantmentBySource(content, source);
            if (!spell && !enchantment) throw std::invalid_argument("Native expanded effect source missing");
            const int cost = spell ? MWMechanics::calcSpellCost(*spell, content)
                : MWMechanics::getEffectiveEnchantmentCastCost(MWMechanics::getEnchantmentCastCost(*enchantment, content),
                    casterIndex < states.size() ? states[casterIndex]->getSkill(ESM::Skill::Enchant).getModified() : 0.f);
            ExpandedEffectResult result{.deaths = std::vector<std::optional<ActorCasterIdentity>>(states.size())};
            const auto undo = [&](const ActorCampaignTimedEffect& effect) {
                for (size_t i = 0; i < states.size(); ++i) applyEffectStats(*states[i], content, {&effect, 1}, i, -1.f);
                const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
                auto& victim = *states[effect.actor];
                if (!effect.argument && !timedDamage(id) && !timedRestore(id)
                    && id != ESM::MagicEffect::DisintegrateWeapon
                    && id != ESM::MagicEffect::DisintegrateArmor)
                    victim.getMagicEffects().add(MWMechanics::EffectKey(id), MWMechanics::EffectParam(-effect.magnitude));
                if (const int stat = drainDynamic(id); stat >= 0 && victim.getHealth().getCurrent() > 0)
                    MWMechanics::restoreDynamicStat(victim, stat, effect.magnitude);
                const MWWorld::TimeStamp deathTime{};
                if (const int stat = fortifyDynamicStat(id); stat >= 0 && victim.getHealth().getCurrent() > 0)
                    MWMechanics::adjustDynamicStatValue(victim, stat, -effect.magnitude, true, false, &deathTime);
                if (id == ESM::MagicEffect::FortifyMaximumMagicka)
                    victim.recalculateMagicka(content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat());
                if (victim.getHealth().getCurrent() <= 0)
                    result.deaths[effect.actor] = ActorCasterIdentity{effect.caster, effect.casterKind, effect.casterLife};
            };
            const auto apply = [&](auto&& self, const ESM::ENAMstruct& effect, uint64_t ordinal,
                size_t recipient, size_t author, ActorCasterIdentity attribution, bool protections) -> void {
                auto& victim = *states[recipient];
                if (victim.getHealth().getCurrent() <= 0) return;
                if (castable && !castable(recipient, effect.mEffectID)) return;
                if (aiDispositionEffect(effect.mEffectID)
                    && !MWMechanics::validAiEffectTarget(effect.mEffectID,
                        actorKinds[recipient].first, recipient < 2, actorKinds[recipient].second,
                        author < states.size())) return;
                // Travel changes the owned player marker or canonical transform at
                // the cast release, not the actor's timed magic overlay.
                if (effect.mEffectID == ESM::MagicEffect::Mark
                    || effect.mEffectID == ESM::MagicEffect::Recall) return;
                const auto* magic = content.get<ESM::MagicEffect>().find(effect.mEffectID);
                if (protections && recipient != author)
                {
                    const bool reflect = author < states.size() && !(magic->mData.mFlags & ESM::MagicEffect::Unreflectable)
                        && victim.getMagicEffects().getOrDefault(ESM::MagicEffect::Reflect).getMagnitude() > 0;
                    const bool absorb = victim.getMagicEffects().getOrDefault(ESM::MagicEffect::SpellAbsorption).getMagnitude() > 0;
                    for (const auto& defense : effects)
                    {
                        if (defense.actor != recipient) continue;
                        const auto outcome = MWMechanics::rollEffectProtection(
                            ESM::MagicEffect::indexToRefId(int(defense.effectIndex)), defense.magnitude, reflect, absorb, rng);
                        if (outcome == MWMechanics::EffectProtection::Reflect)
                        {
                            self(self, effect, ordinal, author, classicReflect ? author : recipient,
                                classicReflect ? attribution : identities[recipient], false);
                            return;
                        }
                        if (outcome == MWMechanics::EffectProtection::Absorb)
                        { MWMechanics::restoreDynamicStat(victim, 1, float(cost)); return; }
                    }
                }
                const bool noMagnitude = magic->mData.mFlags & ESM::MagicEffect::NoMagnitude;
                const float chance = sourceKind != 4 && spell && author < states.size()
                    ? MWMechanics::getSpellSuccessChance(*spell, *states[author], content, false, false) : 100.f;
                const float resistance = ignoreResistance ? 0.f : MWMechanics::getEffectResistance(effect.mEffectID, victim,
                    chance, victim.getFatigueTerm(content), noMagnitude, rng);
                if (resistance >= 100.f) return;
                const float magnitude = noMagnitude ? 1.f : MWMechanics::rollEffectMagnitude(
                    float(effect.mMagnMin), float(effect.mMagnMax), rng) * (1.f - resistance / 100.f);
                if (!std::isfinite(magnitude) || magnitude < 0 || magnitude > 100000.f)
                    throw std::invalid_argument("Native expanded magnitude invalid");
                if (magnitude == 0) return;
                if (wholeSourceCure(effect.mEffectID))
                {
                    std::erase_if(combat.conditions, [&](const auto& condition) {
                        if (condition.actor != recipient) return false;
                        const auto* record = spellBySource(content, condition.source);
                        if (!record || !MWMechanics::Spells::isRemovedByCure(*record, effect.mEffectID)) return false;
                        std::erase_if(effects, [&](const auto& candidate) {
                            if (candidate.actor != recipient || candidate.sourceKind != 4
                                || candidate.source != condition.source) return false;
                            undo(candidate); return true;
                        });
                        return true;
                    });
                    return;
                }
                if (const auto cured = MWMechanics::curedEffect(effect.mEffectID); !cured.empty())
                {
                    if (cured == ESM::MagicEffect::Corprus)
                    {
                        for (auto& condition : combat.conditions)
                            if (condition.actor == recipient)
                            {
                                const auto* record = spellBySource(content, condition.source);
                                if (!record || !MWMechanics::Spells::hasCorprusEffect(record)) continue;
                                for (const auto& candidate : effects)
                                    if (candidate.actor == recipient && candidate.sourceKind == 4
                                        && candidate.source == condition.source && candidate.argument)
                                    {
                                        const auto id = ESM::MagicEffect::indexToRefId(int(candidate.effectIndex));
                                        const auto* magic = content.get<ESM::MagicEffect>().find(id);
                                        if (corprusStatOverlay(id) && (magic->mData.mFlags & ESM::MagicEffect::AppliedOnce)
                                            && (magic->mData.mFlags & ESM::MagicEffect::Harmful))
                                            applyEffectStats(*states[recipient], content, {&candidate, 1}, recipient,
                                                -float(condition.worsenings));
                                    }
                                std::erase_if(effects, [&](const auto& candidate) {
                                    if (candidate.actor != recipient || candidate.sourceKind != 4
                                        || candidate.source != condition.source) return false;
                                    undo(candidate); return true;
                                });
                                condition.nextWorseningMs = condition.lastObservedMs = condition.worsenings = 0;
                            }
                        return;
                    }
                    std::erase_if(effects, [&](const auto& candidate) {
                        if (candidate.actor != recipient
                            || candidate.effectIndex != uint64_t(ESM::MagicEffect::refIdToIndex(cured))) return false;
                        undo(candidate);
                        return true;
                    });
                    return;
                }
                if (effect.mEffectID == ESM::MagicEffect::Dispel)
                {
                    // One roll per source instance, never per effect; enchantments and passives survive.
                    const auto existing = effects;
                    std::set<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t>> visited;
                    for (const auto& active : existing)
                    {
                        if (active.actor != recipient || active.sourceKind != 0) continue;
                        const auto key = std::tuple(active.caster, active.casterKind, active.casterLife, active.source, active.startTick);
                        if (!visited.insert(key).second || !MWMechanics::rollDispel(magnitude, rng)) continue;
                        std::erase_if(effects, [&](const auto& candidate) {
                            if (candidate.actor != recipient || candidate.sourceKind != 0
                                || std::tuple(candidate.caster, candidate.casterKind, candidate.casterLife,
                                    candidate.source, candidate.startTick) != key) return false;
                            undo(candidate); return true;
                        });
                    }
                    return;
                }
                const int drain = drainDynamic(effect.mEffectID), absorb = absorbDynamic(effect.mEffectID);
                const bool disintegration = effect.mEffectID == ESM::MagicEffect::DisintegrateWeapon
                    || effect.mEffectID == ESM::MagicEffect::DisintegrateArmor;
                const bool instant = sourceKind != 4 && effect.mDuration == 0 && (timedDamage(effect.mEffectID)
                    || timedRestore(effect.mEffectID) || absorb >= 0 || permanentStatEffect(effect.mEffectID)
                    || disintegration);
                const auto deathTime = MWWorld::TimeStamp{};
                if (instant)
                {
                    if (disintegration)
                    {
                        if (!disintegrate) throw std::invalid_argument("Native disintegration candidate unavailable");
                        disintegrate(recipient, effect.mEffectID, magnitude);
                    }
                    else if (permanentStatEffect(effect.mEffectID))
                        applyPermanentStatEffect(victim, effect, magnitude, content);
                    else if (absorb >= 0)
                        MWMechanics::absorbDynamicStat(victim, author < states.size() && states[author]->getHealth().getCurrent() > 0
                            ? states[author] : nullptr, absorb, magnitude, &deathTime);
                    else
                    {
                        const int stat = effect.mEffectID == ESM::MagicEffect::DamageMagicka || effect.mEffectID == ESM::MagicEffect::RestoreMagicka ? 1
                            : effect.mEffectID == ESM::MagicEffect::DamageFatigue || effect.mEffectID == ESM::MagicEffect::RestoreFatigue ? 2 : 0;
                        const float scaled = effect.mEffectID == ESM::MagicEffect::SunDamage
                            ? magnitude * (sunExposure ? sunExposure(recipient) : 0.f) : magnitude;
                        MWMechanics::adjustDynamicStatValue(victim, stat, timedRestore(effect.mEffectID) ? scaled : -scaled,
                            stat == 2 && uncappedFatigue, false, &deathTime);
                    }
                }
                else
                {
                    const uint64_t duration = std::max(uint64_t(1), uint64_t(effect.mDuration) * 30);
                    if (effects.size() >= MaximumActorTimedEffects || tick > UINT64_MAX - duration)
                        throw std::invalid_argument("Native expanded effect capacity exhausted");
                    ActorCampaignTimedEffect active{recipient, magnitude, sourceKind == 4 ? UINT64_MAX : tick + duration,
                        uint64_t(ESM::MagicEffect::refIdToIndex(effect.mEffectID)), attribution.id, source, sourceKind,
                        resistance, tick, sourceKind == 4 ? 0 : duration, !effect.mAttribute.empty()
                            ? uint64_t(ESM::Attribute::refIdToIndex(effect.mAttribute) + 1)
                            : !effect.mSkill.empty() ? uint64_t(ESM::Skill::refIdToIndex(effect.mSkill) + 9) : 0,
                        ordinal, attribution.kind, attribution.life,
                        (absorb >= 0 || absorbStat(effect.mEffectID)) && author < states.size() ? author + 1 : 0};
                    effects.push_back(active);
                    for (size_t i = 0; i < states.size(); ++i) applyEffectStats(*states[i], content, {&active, 1}, i, 1.f);
                    if (sourceKind == 4 && spell && MWMechanics::Spells::hasCorprusEffect(spell)
                        && (magic->mData.mFlags & ESM::MagicEffect::AppliedOnce)
                        && effect.mEffectID != ESM::MagicEffect::Corprus)
                        applyCorprusOnce(victim, active, content, uncappedFatigue);
                    if (!active.argument && !timedDamage(effect.mEffectID) && !timedRestore(effect.mEffectID)
                        && !disintegration)
                        victim.getMagicEffects().add(MWMechanics::EffectKey(effect.mEffectID), MWMechanics::EffectParam(magnitude));
                    if (drain >= 0) MWMechanics::adjustDynamicStatValue(victim, drain, -magnitude, true, false, &deathTime);
                    if (const int stat = fortifyDynamicStat(effect.mEffectID); stat >= 0)
                        MWMechanics::adjustDynamicStatValue(victim, stat, magnitude, false, true, &deathTime);
                    if (effect.mEffectID == ESM::MagicEffect::FortifyMaximumMagicka)
                        victim.recalculateMagicka(content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat());
                }
                if (victim.getHealth().getCurrent() <= 0) result.deaths[recipient] = attribution;
            };
            for (size_t ordinal = 0; ordinal < plan.effects.size(); ++ordinal)
                if (plan.effects[ordinal].mRange == range)
                    apply(apply, plan.effects[ordinal], ordinals.empty() ? ordinal : ordinals[ordinal],
                        targetIndex, casterIndex, caster, true);
            for (size_t i = 0; i < states.size(); ++i) saveCombatStats(combat.actors[i], *states[i], effects, i);
            result.target = {target.getHealth().getCurrent() - prior[0], target.getMagicka().getCurrent() - prior[1],
                target.getFatigue().getCurrent() - prior[2]};
            return result;
        }

        std::string identity(const InventoryServiceBinding& binding, const MWWorld::ESMStore& content)
        {
            if (binding.mSecondWorldItems && (!binding.mWorldItems || (!binding.mStreamExteriors && !binding.mDoor)
                || binding.mSecondWorldItems->mCell == binding.mWorldItems->mCell))
                throw std::invalid_argument("Two-cell domain requires distinct cells and the first cell's door");
            const auto domains = binding.worldDomains();
            if (domains.size() > MaxEquipmentCells || (!binding.mAdditionalWorldItems.empty() && !binding.mSecondWorldItems))
                throw std::invalid_argument("Native cell domain exceeds its bound or has missing predecessors");
            std::set<CellId> cells;
            for (const auto* domain : domains)
                if (!cells.insert(domain->mCell).second)
                    throw std::invalid_argument("Duplicate native cell mapping");
            if (binding.mSecondWorldItems)
            {
                for (const auto& shared : binding.mContainers)
                    if (!cells.contains(shared.mCell))
                        throw std::invalid_argument("Shared inventory outside the two-cell domain");
            }
            if (binding.mDoor && (!binding.mWorldItems || !(binding.mDoorId >> 63)))
                throw std::invalid_argument("Native door requires a stable placement ID and world cell");
            if (binding.mTeleportDoors)
            {
                if (domains.empty() || binding.mTeleportDoors->size() > 32 * domains.size())
                    throw std::invalid_argument("Native teleport domain requires two cells and bounded doors");
                std::set<uint64_t> doors{binding.mDoorId};
                for (const auto& door : *binding.mTeleportDoors)
                    if (!(door.mId >> 63) || !doors.insert(door.mId).second
                        || !cells.contains(door.mCell) || !cells.contains(door.mDestination.cell())
                        || door.mDestination.cell() == door.mCell)
                        throw std::invalid_argument("Native teleport identity or cell mapping invalid");
            }
            if (binding.mPlayers[0] == binding.mPlayers[1] || (binding.mContainers.empty() && !binding.mWorldItems)
                || binding.mContainers.size() > (binding.mStreamExteriors ? MaxEquipmentContainers : 32)
                || binding.mActors[0].mBaseInventory != binding.mActors[1].mBaseInventory)
                throw std::invalid_argument("Native inventory requires distinct trusted players, bounded containers and one initialization mode");
            std::set<ContainerId> ids;
            for (const auto& container : binding.mContainers)
                if (container.mBase.empty() || !ids.insert(container.mId).second)
                    throw std::invalid_argument("Native container base or identity invalid");
            std::vector<ActorSpawnSelection> selections;
            for (const auto* domain : domains)
            {
                if (domain->mActorSpawns.size() > MaximumEquipmentSnapshotActors)
                    throw std::invalid_argument("Native cell spawn budget exceeded");
                for (const auto& spawn : domain->mActorSpawns)
                {
                    const auto owner = std::ranges::find_if(binding.mContainers,
                        [&](const auto& value) { return value.mId.value() == spawn.placement; });
                    if (spawn.record ? (owner == binding.mContainers.end() || owner->mCell != domain->mCell
                            || MWWorld::inventoryRecordId(owner->mBase) != spawn.record
                            || (content.find(owner->mBase) != ESM::NPC::sRecordId && content.find(owner->mBase) != ESM::Creature::sRecordId))
                        : owner != binding.mContainers.end())
                        throw std::invalid_argument("Native actor spawn owner mismatch");
                    selections.push_back({spawn.placement, spawn.record});
                }
            }
            std::ranges::sort(selections, {}, &ActorSpawnSelection::mPlacement);
            validateActorSpawns(selections);
            if (binding.mActorSelections ? (!binding.mStreamExteriors || *binding.mActorSelections != selections) : !selections.empty())
                throw std::invalid_argument("Native actor spawn persistence binding mismatch");
            if (binding.mActors[0].mBaseInventory)
            {
                if (binding.mShirt)
                    throw std::invalid_argument("Base actor inventories cannot override an item identity");
                return "native-inventory-2/" + std::to_string(binding.mPlayers[0].value()) + "/"
                    + std::to_string(binding.mPlayers[1].value()) + "/"
                    + std::to_string(binding.mContainers.empty() ? 0 : binding.mContainers.front().mId.value());
            }
            if (!binding.mShirt || binding.mActors[0].mShirt != binding.mActors[1].mShirt)
                throw std::invalid_argument("Legacy native inventory requires one seed shirt");
            const auto* shirt = content.get<ESM::Clothing>().find(binding.mActors[0].mShirt);
            if (!shirt->mScript.empty() || !shirt->mEnchant.empty())
                throw std::invalid_argument("Native inventory wire projection supports the plain shirt only");
            return "native-inventory-1/" + std::to_string(binding.mPlayers[0].value()) + "/"
                + std::to_string(binding.mPlayers[1].value()) + "/" + std::to_string(binding.mShirt->value())
                + "/" + std::to_string(binding.mContainers.empty() ? 0 : binding.mContainers.front().mId.value());
        }
        std::vector<EquipmentContainerBinding> containers(const InventoryServiceBinding& binding)
        {
            if (binding.mContainers.size() > MaxEquipmentContainers)
                throw std::invalid_argument("Native container budget exceeded");
            std::vector<EquipmentContainerBinding> result;
            for (const auto& shared : binding.mContainers) result.push_back({shared.mBase, shared.mPlacement});
            return result;
        }
        std::optional<std::vector<ESM::CellRef>> worldItems(const InventoryServiceBinding& binding)
        {
            if (!binding.mWorldItems) return {};
            const auto domains = binding.worldDomains();
            size_t count = 0;
            for (const auto* domain : domains) count += domain->mPlacements.size();
            if (count > PlainEquipmentValues::MaxWorldItems)
                throw std::invalid_argument("Native world placement budget exceeded");
            std::vector<ESM::CellRef> result;
            for (const auto* domain : domains)
            {
                if (!domain) continue;
                uint64_t previous = 0;
                for (const auto& [id, ref] : domain->mPlacements)
                {
                    if (!id || id <= previous) throw std::invalid_argument("Native world identities not sorted");
                    previous = id;
                    result.push_back(ref);
                }
            }
            return result;
        }
        std::optional<EquipmentSessionValues::WorldCells> worldCells(const InventoryServiceBinding& binding)
        {
            if (!binding.mSecondWorldItems && !binding.mStreamExteriors) return {};
            if (!binding.mWorldItems) throw std::invalid_argument("Second world cell requires the first");
            EquipmentSessionValues::WorldCells result;
            uint8_t index = 0;
            for (const auto* domain : binding.worldDomains())
            {
                for (const auto& [id, ref] : domain->mPlacements)
                    if (!result.emplace(ref.mRefNum, index).second)
                        throw std::invalid_argument("Duplicate world placement across cells");
                ++index;
            }
            return result;
        }
        Position3 worldPosition(const ESM::CellRef& ref)
        {
            return Position3(std::llround(double(ref.mPos.pos[0]) * 1024),
                std::llround(double(ref.mPos.pos[1]) * 1024), std::llround(double(ref.mPos.pos[2]) * 1024));
        }
        ItemStackId wireId(ESM::RefNum id)
        {
            return ItemStackId::fromValue((uint64_t(std::bit_cast<uint32_t>(id.mContentFile)) << 32) | id.mIndex).value();
        }
        ItemStackId worldId(ESM::RefNum ref, const InventoryServiceBinding::WorldItems& domain)
        {
            if (!ref.hasContentFile()) return wireId(ref);
            for (const auto& [id, placed] : domain.mPlacements)
                if (placed.mRefNum == ref) return ItemStackId::fromValue(id).value();
            throw std::invalid_argument("World reference outside the bound placement domain");
        }
        InventoryInstanceId nativeId(ItemStackId id)
        {
            return { uint32_t(id.value()), std::bit_cast<int32_t>(uint32_t(id.value() >> 32)) };
        }
        std::vector<CanonicalItemStack> stacks(const MWWorld::PlainEquipmentValues& values,
            const std::map<ESM::RefId, ItemPrototypeId>& items, const MWWorld::ESMStore& content,
            const InventoryServiceBinding::WorldItems* world = nullptr)
        {
            std::vector<CanonicalItemStack> result;
            result.reserve(values.mObjects.size());
            for (const auto& object : values.mObjects)
            {
                const auto count = object.mRef.mCount;
                if (count == 0) continue; // Dormant IDs remain engine-owned and durable.
                if (count == std::numeric_limits<int32_t>::min())
                    throw std::invalid_argument("Native inventory fields cannot be projected losslessly");
                const auto id = items.at(object.mRef.mRefID);
                MWWorld::ManualRef ref(content, object.mRef.mRefID);
                const auto& ptr = ref.getPtr();
                ptr.getCellRef() = MWWorld::CellRef(object.mRef);
                const auto& cls = ptr.getClass();
                const uint32_t condition = cls.hasItemHealth(ptr) ? uint32_t(cls.getItemHealth(ptr)) : 0;
                const auto charge = ptr.getCellRef().getEnchantmentCharge();
                std::optional<ActorPrototypeId> soul;
                if (!object.mRef.mSoul.empty())
                {
                    content.get<ESM::Creature>().find(object.mRef.mSoul);
                    soul = ActorPrototypeId::fromValue(MWWorld::inventoryRecordId(object.mRef.mSoul)).value();
                }
                // Native record IDs carry the exact engine charge bit patterns,
                // including the untouched -1 sentinel and fractional light time.
                const bool native = id.value() == MWWorld::inventoryRecordId(object.mRef.mRefID);
                result.push_back({ world ? worldId(object.mRef.mRefNum, *world) : wireId(object.mRef.mRefNum), id, uint32_t(std::abs(count)),
                    native ? std::bit_cast<uint32_t>(object.mRef.mChargeInt) : condition,
                    native ? std::bit_cast<uint32_t>(charge) : 0, soul });
            }
            std::ranges::sort(result, {}, &CanonicalItemStack::stackId);
            return result;
        }
    }

    InventoryService::InventoryService(MWWorld::ESMStore& content, ESM::ReadersCache& readers,
        InventoryServiceBinding binding, bool recovering)
        : mBinding(std::move(binding)), mWorld(content, readers, 1), mScripts(content),
          mCompilerContext(MWScript::CompilerContext::Type_Full),
          mScriptManager(content, mCompilerContext, 1),
          mRuntime(content, mWorld, mScripts, identity(mBinding, content), mBinding.mContent, mBinding.mActors,
              std::make_shared<const MWWorld::EquipmentScriptLocals>(mScriptManager), &mScriptManager,
              recovering ? std::optional<size_t>{ 2 } : std::nullopt, true, containers(mBinding),
              mBinding.mLootLevel, mBinding.mLootSeed, worldItems(mBinding), mBinding.mDoor,
              worldCells(mBinding), mBinding.worldDomains().size(),
              mBinding.mStreamExteriors ? PlainEquipmentValues::MaxWorldItems : 64, mBinding.mConstantEffects)
    {
        if (mBinding.mPersistentConditions && !mBinding.mPlayerCastLifecycle)
            throw std::invalid_argument("Persistent conditions require the current actor lifecycle");
        if (mBinding.mPlayerCastLifecycle)
        {
            if (!mBinding.mActorPresentation || !mBinding.mNpcCastLifecycle)
                throw std::invalid_argument("Player casting requires shared actor presentation");
            for (const auto& bound : mBinding.mPlayerCasts)
            {
                if (bound.resourceIdentity.empty() || bound.resourceIdentity.size() > 1024)
                    throw std::invalid_argument("Native player cast resources invalid");
                for (const auto timing : bound.ranges)
                    if (!timing.releaseTicks || timing.releaseTicks >= timing.stopTicks || timing.stopTicks > 1800)
                        throw std::invalid_argument("Native player cast timing invalid");
            }
        }
        if (mBinding.mWeaponMelee && !mBinding.mBoundHits)
            throw std::invalid_argument("Native weapon execution requires participant resource binding");
        if (mBinding.mGeneralAttackModes && !mBinding.mWeaponMelee)
            throw std::invalid_argument("Native attack selection requires weapon animation binding");
        if (bool(mBinding.mPlayerMelee[0]) != bool(mBinding.mPlayerMelee[1])
            || (mBinding.mPlayerMelee[0] && !mBinding.mGeneralAttackModes))
            throw std::invalid_argument("Native player swings require complete participant bindings");
        if (mBinding.mBowRelease && !mBinding.mPlayerMelee[0])
            throw std::invalid_argument("Native bow release requires player animation binding");
        if (mBinding.mRangedRelease && !mBinding.mBowRelease)
            throw std::invalid_argument("Native ranged release requires the ammunition layout");
        if (mBinding.mRangedFlight && !mBinding.mRangedRelease)
            throw std::invalid_argument("Native flight requires extended ranged release");
        if (mBinding.mBoundHits)
        {
            if (!mBinding.mNpcCastLifecycle || !mBinding.mMeleeDefenseRules)
                throw std::invalid_argument("Participant hit resources require the composed combat lifecycle");
            if (mBinding.mBoundHits->size() != (mBinding.mNeighborCombat ? 3u + mBinding.mNeighborMeleeSet.size()
                    : mBinding.mPlacementCombat ? 4u : 3u))
                throw std::invalid_argument("Participant hit resources differ from bound placements");
            for (const auto& hit : *mBinding.mBoundHits)
            {
                if (hit.resourceIdentity.empty() || hit.resourceIdentity.size() > MaximumHitResourceIdentity
                    || hit.animations.count > hit.animations.ticks.size())
                    throw std::invalid_argument("Native participant hit binding invalid");
                for (size_t i = 0; i < hit.animations.ticks.size(); ++i)
                    if (i < hit.animations.count ? (!hit.animations.ticks[i] || hit.animations.ticks[i] > 1800)
                                                : hit.animations.ticks[i] != 0)
                        throw std::invalid_argument("Native participant hit timing invalid");
            }
        }
        if (mBinding.mNpcCastLifecycle && (!mBinding.mNpcFullSelection || !mBinding.mBoundCasts))
            throw std::invalid_argument("NPC casting requires full selection and bound animation");
        if (mBinding.mNpcFullSelection && !mBinding.mNpcWeaponCompetition)
            throw std::invalid_argument("Full NPC selection requires weapon competition");
        if (mBinding.mNpcWeaponCompetition && !mBinding.mAutomaticNpcSpells)
            throw std::invalid_argument("NPC weapon competition requires automatic casting");
        if (mBinding.mAutomaticNpcSpells && (!mBinding.mDurableCasters || !mBinding.mMagicUse
                || !mBinding.mMagicPlayerTarget || !mBinding.mMagicProjectileCollection
                || !mBinding.mActorEffectLifecycle))
            throw std::invalid_argument("Automatic NPC spells require the shared durable cast lifecycle");
        if (mBinding.mDurableCasters && (!mBinding.mGeneralConstants || !mBinding.mNpcLifecycle))
            throw std::invalid_argument("Native durable casters require general constants and NPC lives");
        if (mBinding.mConstantEffects && (!mBinding.mActorEffectLifecycle || !mBinding.mCombatState))
            throw std::invalid_argument("Native constants require the composed actor effect campaign");
        if (mBinding.mKnockoutAnimation && (!mBinding.mBoundHits || !mBinding.mMeleeDefenseRules))
            throw std::invalid_argument("Native knockout animation requires participant resources and defense");
        if (mBinding.mMeleeDefenseRules && !mBinding.mKnockoutRules)
            throw std::invalid_argument("Native melee defense requires knockout campaign state");
        if (mBinding.mMeleeContact && !mBinding.mBoundMelee)
            throw std::invalid_argument("Native melee contact requires a bound animation");
        if (mBinding.mCombatState)
        {
            if (!mBinding.mMeleeContact || !mBinding.mNavigatingActor)
                throw std::invalid_argument("Native combat stats require both players and a selected melee NPC");
            const auto id = mBinding.mNavigatingActor->actorId();
            const auto owner = std::ranges::find_if(mBinding.mContainers,
                [id](const auto& value) { return value.mId.value() == id; });
            if (owner == mBinding.mContainers.end() || !actorInventory(mRuntime.ownerPtr(size_t(owner - mBinding.mContainers.begin()) + 2)))
                throw std::invalid_argument("Native combat stat owner must be the selected NPC");
            mCombatNpcOwner = size_t(owner - mBinding.mContainers.begin()) + 2;
            if (mBinding.mNeighborAi)
            {
                const auto neighbors = mBinding.mNavigatingActor->neighborSnapshots();
                if (neighbors.empty() || neighbors.size() > mBinding.mNeighborLimit)
                    throw std::invalid_argument("Native neighboring NPC count invalid");
                for (const auto& neighbor : neighbors)
                    if (std::ranges::none_of(mBinding.mContainers, [&](const auto& value) {
                        return value.mId.value() == neighbor.mActor && value.mPlacement.has_value()
                            && value.mCell == owner->mCell && value.mId != owner->mId;
                    })) throw std::invalid_argument("Native neighboring NPC has no authoritative placement");
            }
            const auto selected = mRuntime.ownerPtr(mCombatNpcOwner);
            const bool creatureSpellsSupported = std::ranges::all_of(actorSpells(selected).mList,
                [&](const ESM::RefId& id) {
                    if (!mBinding.mAiDecisions) return false;
                    const auto* spell = content.get<ESM::Spell>().search(id);
                    return spell && preparePassiveActorEffects(*spell, content,
                        mBinding.mMovementEffects, mBinding.mAiDecisions).has_value();
                });
            if (selected.getType() == ESM::Creature::sRecordId
                && (!mBinding.mActorPresentation || !creatureSpellsSupported
                    || mRuntime.equippedWeaponCondition(mCombatNpcOwner)))
                throw std::invalid_argument("Creature timeline requires an unarmed biped with supported passive spell sources");
            std::vector<ESM::RefId> combatBases{mBinding.mActors[0].mBase, mBinding.mActors[1].mBase,
                owner->mBase};
            std::vector<uint64_t> placements;
            if (mBinding.mPlacementCombat)
            {
                placements.push_back(owner->mId.value());
                for (const auto& neighbor : mBinding.mNavigatingActor->neighborSnapshots())
                {
                    const auto adjacent = std::ranges::find_if(mBinding.mContainers, [&](const auto& value) {
                        return value.mId.value() == neighbor.mActor;
                    });
                    if (adjacent == mBinding.mContainers.end())
                        throw std::invalid_argument("Native placement combat neighbor absent");
                    placements.push_back(adjacent->mId.value());
                    combatBases.push_back(adjacent->mBase);
                }
            }
            mCombat = initialCombat(combatBases, content, mBinding.mLootSeed,
                mBinding.mKnockoutAnimation, placements);
            mCombat->levitationEnabled = mBinding.mLevitationEnabled;
            if (mBinding.mNeighborCombat)
            {
                if (mBinding.mNeighborMeleeSet.size() != placements.size() - 1)
                    throw std::invalid_argument("Native neighbor melee resource count invalid");
                for (const auto& melee : mBinding.mNeighborMeleeSet)
                {
                    auto idle = melee(nullptr, "chop");
                    mCombat->neighborAttacks.push_back({idle.identity(), {}, idle.snapshot(), 0, 0, 0, 0, false});
                }
            }
            (void)mRuntime.equippedWeaponCondition(mCombatNpcOwner);
        }
        if (mBinding.mBoundMelee)
        {
            if (!mBinding.mNavigatingActor || mBinding.mBoundMelee->mResourceIdentity.empty()
                || mBinding.mBoundMelee->mResourceIdentity.size() > 512)
                throw std::invalid_argument("Native melee binding invalid");
            mMelee = mBinding.mBoundMelee->mAnimation;
            if (mBinding.mWeaponMelee) mMelee = mBinding.mWeaponMelee(nullptr, {});
            mIdleMelee = mMelee;
        }
        initializeAreaDoors();
        for (const auto& [id, record] : MWWorld::inventoryRecords(content))
            mItemIds.emplace(record, ItemPrototypeId::fromValue(id).value());
        // Synthetic service fixtures may retain their explicit shirt ID.
        if (mBinding.mShirt) mItemIds.insert_or_assign(mBinding.mActors[0].mShirt, *mBinding.mShirt);
        if (!recovering)
            mRuntime.encodeSession({{mRuntime.installedValues(0), mRuntime.installedValues(1)},
                mWorld.getPtrRegistryRevision()}, mImage);
        if (!recovering)
        {
            mCoreImage = mImage;
            mImage = sealInventory(mCoreImage);
        }
        if (!recovering && mBinding.mNavigatingActor)
        {
            const auto id = mBinding.mNavigatingActor->snapshot().mActor;
            if (!mBinding.mStreamExteriors || std::ranges::none_of(mBinding.mContainers,
                    [id](const auto& owner) { return owner.mId.value() == id && owner.mPlacement.has_value(); }))
                throw std::invalid_argument("Navigating NPC has no authoritative inventory owner");
            if (mBinding.mNpcLifecycle)
            {
                if (!mCombat || mCombat->actors[2][8][2] <= 0 || !mBinding.mNpcRespawnDelayTicks
                    || mBinding.mNpcRespawnDelayTicks > 30ull * 60 * 60 * 24)
                    throw std::invalid_argument("Native NPC lifecycle requires a living placed actor and bounded delay");
                mRespawnInventory = mRuntime.installedValues(mCombatNpcOwner);
                std::vector<ESM::RefId> ids;
                for (const auto& object : mRespawnInventory.mObjects)
                    for (auto ref : {object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul,
                             object.mRef.mFaction, object.mRef.mKey, object.mRef.mTrap})
                        if (!ref.empty()) ids.push_back(ref);
                const auto envelope = mRuntime.expectedEnvelope(mRespawnInventory.mActor);
                ActorCampaignLife life;
                life.spawnStats = mCombat->actors[2];
                life.spawnActor = mBinding.mNavigatingActor->selectedImage();
                encodeEquipment(mRespawnInventory, {envelope, mRuntime.mStore, ids, mRuntime.mScriptLocals, true},
                    life.spawnInventory);
                mLife = std::move(life);
                if (mBinding.mNeighborCombat)
                {
                    for (size_t i = 1; i < mCombat->npcPlacements.size(); ++i)
                    {
                        const auto id = mCombat->npcPlacements[i];
                        const auto found = std::ranges::find_if(mBinding.mContainers, [id](const auto& owner) {
                            return owner.mId.value() == id;
                        });
                        if (found == mBinding.mContainers.end())
                            throw std::invalid_argument("Neighbor life owner missing");
                        const size_t neighborOwner = size_t(found - mBinding.mContainers.begin()) + 2;
                        mNeighborRespawnInventory.push_back(mRuntime.installedValues(neighborOwner));
                        const auto& inventory = mNeighborRespawnInventory.back();
                        std::vector<ESM::RefId> neighborIds;
                        for (const auto& object : inventory.mObjects)
                            for (auto ref : {object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul,
                                     object.mRef.mFaction, object.mRef.mKey, object.mRef.mTrap})
                                if (!ref.empty()) neighborIds.push_back(ref);
                        ActorCampaignLife adjacent;
                        adjacent.spawnStats = mCombat->actors.at(i + 2);
                        adjacent.spawnActor = mBinding.mNavigatingActor->neighborImage(i - 1);
                        encodeEquipment(inventory, {mRuntime.expectedEnvelope(inventory.mActor),
                            mRuntime.mStore, neighborIds, mRuntime.mScriptLocals, true}, adjacent.spawnInventory);
                        mNeighborLives.push_back(std::move(adjacent));
                    }
                }
            }
            mActorImage = sealActor(mImage, mBinding.mNavigatingActor->image(), mActorTick, mActorVelocity,
                mMelee, mMeleeTarget, mMeleeContacted, mCombat, mLife, mNeighborLives, mProjectiles, mTimedEffects);
            installActorPosition();
        }
        if (mImage.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory image exceeds canonical record budget");
        if (mBinding.mMagicProjectile || mBinding.mRangedFlight)
        {
            const auto remember = [&](uint64_t kind, ESM::RefId id) {
                const auto source = spellRecordId(id);
                if (!source) return;
                const auto [entry, inserted] = mMagicVisualRecords.emplace(
                    std::pair(kind, source), std::string(id.getRefIdString()));
                if (!inserted) entry->second.clear(); // Ambiguous hashed source cannot drive a visual.
            };
            for (const auto& spell : content.get<ESM::Spell>())
                remember(uint64_t(MagicUseSourceKind::Spell), spell.mId);
            for (const auto& enchantment : content.get<ESM::Enchantment>())
                remember(uint64_t(MagicUseSourceKind::EnchantedItem), enchantment.mId);
        }
    }

    std::string InventoryService::magicVisualRecord(uint64_t sourceKind, uint64_t effectSource) const
    {
        const auto record = mMagicVisualRecords.find({sourceKind, effectSource});
        if (record == mMagicVisualRecords.end() || record->second.empty())
            throw std::invalid_argument("Native magic visual source missing or ambiguous");
        return record->second;
    }

    size_t InventoryService::actor(PlayerId player) const
    {
        for (size_t i = 0; i < mBinding.mPlayers.size(); ++i)
            if (mBinding.mPlayers[i] == player) return i;
        throw std::invalid_argument("Authenticated player has no native actor binding");
    }

    const InventoryServiceBinding::WorldItems* InventoryService::worldDomain(CellId cell) const
    {
        if (mBinding.mWorldItems && mBinding.mWorldItems->mCell == cell) return &*mBinding.mWorldItems;
        if (mBinding.mSecondWorldItems && mBinding.mSecondWorldItems->mCell == cell) return &*mBinding.mSecondWorldItems;
        for (const auto& domain : mBinding.mAdditionalWorldItems)
            if (domain.mCell == cell) return &domain;
        return nullptr;
    }

    uint8_t InventoryService::worldIndex(CellId cell) const
    {
        if (!worldDomain(cell)) throw std::invalid_argument("Cell outside native world domain");
        const auto domains = mBinding.worldDomains();
        for (size_t i = 0; i < domains.size(); ++i)
            if (domains[i]->mCell == cell) return static_cast<uint8_t>(i);
        throw std::invalid_argument("Cell outside native world domain");
    }

    bool InventoryService::allowsPlayerMovement(PlayerId player) const
    {
        return !mBinding.mExpandedEffects || !hasParalysis(mTimedEffects, actor(player));
    }

    std::optional<CellId> InventoryService::movementCell(CellId current, Position3 position) const
    {
        if (!mBinding.mStreamExteriors) return current;
        if (!worldDomain(current)) return {};
        const auto* exterior = current.asExterior();
        if (!exterior) return current;
        constexpr int64_t size = 8192 * 1024;
        const auto grid = [](int64_t value) { return value / size - (value % size < 0); };
        const auto x = grid(position.x()), y = grid(position.y());
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767
            || std::abs(x - exterior->gridX()) > 1 || std::abs(y - exterior->gridY()) > 1) return {};
        const auto cell = CellId::exterior(exterior->worldspace(), int32_t(x), int32_t(y));
        return worldDomain(cell) ? std::optional(cell) : std::nullopt;
    }

    bool InventoryService::allowsCellTransition(CellId current, CellId requested, Position3 position) const
    {
        return mBinding.mStreamExteriors && current.asExterior() && requested.asExterior()
            && movementCell(current, position) == requested;
    }

    PlainEquipmentValues InventoryService::cellWorldValues(CellId cell, const EquipmentRuntime::PreparedWorldTransfer* prepared) const
    {
        auto result = mRuntime.worldValues(prepared);
        const auto index = worldIndex(cell);
        std::erase_if(result.mObjects, [&](const auto& object) { return mRuntime.worldCell(object.mRef.mRefNum, prepared) != index; });
        return result;
    }

    void InventoryService::synchronizeCells(const CanonicalServerState& players)
    {
        if (!mBinding.mSecondWorldItems && !mBinding.mStreamExteriors) return;
        const auto domains = mBinding.worldDomains();
        std::vector<bool> active(domains.size());
        for (const auto& session : players.activeSessions())
        {
            const auto* player = players.findPlayer(session.playerId());
            if (player && std::ranges::find(mBinding.mPlayers, player->playerId()) != mBinding.mPlayers.end()
                && worldDomain(player->transform().cell()))
            {
                const auto cell = player->transform().cell();
                active[worldIndex(cell)] = true;
                if (mBinding.mStreamExteriors && cell.asExterior())
                    for (size_t i = 0; i < domains.size(); ++i)
                        if (const auto* other = domains[i]->mCell.asExterior(); other
                            && other->worldspace() == cell.asExterior()->worldspace()
                            && std::abs(int64_t(other->gridX()) - cell.asExterior()->gridX()) <= 1
                            && std::abs(int64_t(other->gridY()) - cell.asExterior()->gridY()) <= 1)
                            active[i] = true;
            }
        }
        if (mBinding.mRetainTraveler && mBinding.mNavigatingActor)
        {
            const auto index = worldIndex(actorCell(mBinding.mNavigatingActor->snapshot()));
            // Demand is a union, not another simulation loop. An unavailable
            // path remains travel demand; only stock path completion releases it.
            active[index] = active[index] || !mProjectiles.empty()
                || (mBinding.mRangedFlight && mCombat && std::ranges::any_of(mCombat->arrows,
                    [](const auto& flight) { return !flight.terminal; }))
                || (!mBinding.mNavigatingActor->arrived()
                    && (!mCombat || mCombat->actors[2][8][2] > 0));
            bool navigationActive = active[index];
            if (mBinding.mTravelerNeighborhood)
            {
                navigationActive = navigationActive || std::ranges::any_of(active, [](bool value) { return value; });
                // The declared neighborhood is one collision/navigation unit.
                // Union admission happens once below, never once per cell.
                if (navigationActive) std::fill(active.begin(), active.end(), true);
            }
            if (mBinding.mNavigationActivity) mBinding.mNavigationActivity(navigationActive);
        }
        if (mBinding.mAreaActivity) mBinding.mAreaActivity(active);
        const std::array legacy{bool(active[0]), active.size() > 1 && active[1]};
        if (mBinding.mCellActivity) mBinding.mCellActivity(legacy);
        for (size_t i = 0; i < mAreaDoors.size(); ++i)
            for (auto& report : mAreaDoors[i].reports)
                if (report)
                {
                    const auto* session = players.findActiveSession(report->session);
                    const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
                    if (!player || session->sessionGeneration() != report->generation
                        || player->transform().cell() != mBinding.mDoors[i].mCell) report.reset();
                }
        for (auto& report : mDoorReports)
            if (report)
            {
                const auto* session = players.findActiveSession(report->session);
                const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
                if (!player || session->sessionGeneration() != report->generation
                    || player->transform().cell() != mBinding.mWorldItems->mCell) report.reset();
            }
        mActiveCells = legacy;
        mActiveAreas = std::move(active);
    }

    size_t InventoryService::container(std::optional<ContainerId> id) const
    {
        for (size_t i = 0; i < mBinding.mContainers.size(); ++i)
            if (mBinding.mContainers[i].mId == id) return i;
        throw std::invalid_argument("Container is outside the native loaded domain");
    }

    void InventoryService::validate(const CanonicalServerState& players, const ServerApp::InventoryCommandBinding& bound) const
    {
        // Desktop positions are fixed-point quanta (1024 per OpenMW unit).
        // Preserve the bounded 384-unit interaction radius in that wire domain.
        constexpr std::uint32_t ReachQuanta = 384 * 1024;
        if (!bound.current(players)) throw std::invalid_argument("Inventory session binding is no longer current");
        const auto& command = bound.transaction();
        if (mBinding.mNavigatingActor && (command.kind == InventoryTransactionKind::PickupItem
                || command.kind == InventoryTransactionKind::DropItem))
            throw std::invalid_argument("Frozen NPC interior does not support changing collision placements");
        (void)actor(bound.player());
        if (command.placement && (command.kind != InventoryTransactionKind::DropItem
            || !validDropPlacementView(*command.placement)))
            throw std::invalid_argument("Invalid placement command shape");
        const bool pickup = command.kind == InventoryTransactionKind::PickupItem;
        const bool drop = command.kind == InventoryTransactionKind::DropItem;
        if (pickup || drop)
        {
            const auto* player = players.findPlayer(bound.player());
            const auto* domain = player ? worldDomain(player->transform().cell()) : nullptr;
            if (!domain || !command.stackId || command.containerId || command.slot
                || command.expectedContainerRevision || command.player != bound.player() || command.count == 0
                || command.count > MaximumTransferCount
                || command.expectedInventoryRevision.value() != mWorld.getPtrRegistryRevision()
                || !positionsWithinReach(player->transform().position(), command.interactionOrigin, ReachQuanta))
                throw std::invalid_argument("World item command shape, cell or revision invalid");
            if (pickup)
            {
                const auto state = cellWorldValues(player->transform().cell());
                const auto& items = state.mObjects;
                const auto object = std::ranges::find_if(items, [&](const auto& value) {
                    return worldId(value.mRef.mRefNum, *domain) == command.stackId;
                });
                if (object == items.end() || !command.expectedWorldItemRevision
                    || command.expectedWorldItemRevision->value() != mWorld.getPtrRegistryRevision()
                    || command.count != object->mRef.mCount || mItemIds.at(object->mRef.mRefID) != command.prototypeId
                    || !positionsWithinReach(player->transform().position(), worldPosition(object->mRef), ReachQuanta)
                    || !positionsWithinReach(command.interactionOrigin, worldPosition(object->mRef), ReachQuanta))
                    throw std::invalid_argument("World pickup missing, stale or out of reach");
            }
            else
            {
                if (mBinding.mStreamExteriors
                    && cellWorldValues(player->transform().cell()).mObjects.size() >= MaximumGroundItemBaselineChunkItems)
                    throw std::invalid_argument("Native cell ground-item budget exhausted");
                if (domain->mPlacement && !command.placement)
                    throw std::invalid_argument("Stock drop requires placement view input");
                const auto native = nativeId(*command.stackId);
                const auto item = mWorld.getPtr({native.mIndex, native.mContentFile});
                if (command.expectedWorldItemRevision || item.isEmpty()
                    || item.getContainerStore() != &mRuntime.storage(actor(bound.player()))
                    || mItemIds.at(item.getCellRef().getRefId()) != command.prototypeId)
                    throw std::invalid_argument("World drop ownership or prototype invalid");
            }
            return;
        }
        const auto item = command.stackId ? mWorld.getPtr({uint32_t(command.stackId->value()),
            std::bit_cast<int32_t>(uint32_t(command.stackId->value() >> 32))}) : MWWorld::Ptr{};
        const auto prototype = item.isEmpty() ? mItemIds.end() : mItemIds.find(item.getCellRef().getRefId());
        if (prototype == mItemIds.end() || prototype->second != command.prototypeId)
            throw std::invalid_argument("Native stack and item prototype identities disagree");
        if (command.kind == InventoryTransactionKind::EquipItem || command.kind == InventoryTransactionKind::UnequipItem)
        {
            if (command.player != bound.player() || !command.slot || command.count != 1
                || static_cast<unsigned>(*command.slot) >= static_cast<unsigned>(EquipmentSlot::Count)
                || command.containerId || command.expectedContainerRevision || command.expectedWorldItemRevision
                || command.expectedInventoryRevision.value() != mWorld.getPtrRegistryRevision()
                || item.getContainerStore() != &mRuntime.storage(actor(bound.player())))
                throw std::invalid_argument("Native equipment command shape, revision or ownership invalid");
            return;
        }
        const auto sharedIndex = container(command.containerId);
        const auto& shared = mBinding.mContainers[sharedIndex];
        if (mContainerLocks[sharedIndex].level)
            throw std::invalid_argument("Locked native container rejects inventory transfer");
        const auto sharedOwner = mRuntime.ownerPtr(sharedIndex + 2);
        const bool selectedCorpse = mCombat && sharedIndex + 2 == mCombatNpcOwner
            && mCombat->actors[2][8][2] <= 0;
        if (actorInventory(sharedOwner) && !initialCorpse(sharedOwner) && !selectedCorpse)
            throw std::invalid_argument("Living actor inventory access requires theft/companion services");
        const auto& player = *players.findPlayer(bound.player());
        const auto revision = mWorld.getPtrRegistryRevision();
        CellId sharedCell = shared.mCell;
        Position3 sharedPosition = shared.mPosition;
        if (selectedCorpse)
        {
            const auto state = mBinding.mNavigatingActor->snapshot();
            sharedCell = actorCell(state);
            sharedPosition = Position3(int64_t(std::llround(double(state.mPosition[0]) * 1024)),
                int64_t(std::llround(double(state.mPosition[1]) * 1024)),
                int64_t(std::llround(double(state.mPosition[2]) * 1024)));
        }
        if (command.player != bound.player()
            || !command.stackId || command.slot
            || command.expectedWorldItemRevision || command.count == 0 || command.count > MaximumTransferCount
            || (command.kind == InventoryTransactionKind::TakeAllFromContainer && command.count != 1)
            || command.expectedInventoryRevision.value() != revision || !command.expectedContainerRevision
            || command.expectedContainerRevision->value() != revision
            || (command.kind != InventoryTransactionKind::PutIntoContainer
                && command.kind != InventoryTransactionKind::TakeFromContainer
                && command.kind != InventoryTransactionKind::TakeAllFromContainer)
            || player.transform().cell() != sharedCell
            || !positionsWithinReach(player.transform().position(), sharedPosition, ReachQuanta)
            || !positionsWithinReach(command.interactionOrigin, sharedPosition, ReachQuanta)
            || !positionsWithinReach(player.transform().position(), command.interactionOrigin, ReachQuanta))
            throw std::invalid_argument("Native container command shape, revision, identity or reach invalid");
        if (command.kind == InventoryTransactionKind::PutIntoContainer && !actorInventory(sharedOwner))
        {
            const auto* base = mRuntime.ownerPtr(sharedIndex + 2).get<ESM::Container>()->mBase;
            if (MWWorld::checkContainerPut((base->mFlags & ESM::Container::Organic) != 0,
                    base->mWeight, mRuntime.storage(sharedIndex + 2).getWeight(), item.getClass().getWeight(item), int(command.count))
                != MWWorld::ContainerPutCheck::Allowed)
                throw std::invalid_argument("Native container rejects organic or over-capacity put");
        }
    }

    InventoryService::PreparedCommand InventoryService::prepare(const CanonicalServerState& players,
        const ServerApp::InventoryCommandBinding& bound)
    {
        validate(players, bound); // Bound all external fields before engine allocation.
        const auto index = actor(bound.player());
        const auto& input = bound.transaction();
        auto command = mRuntime.containerCommand(index, input.kind == InventoryTransactionKind::PutIntoContainer,
            nativeId(*input.stackId), int32_t(input.count), container(input.containerId));
        command.mTakeAll = input.kind == InventoryTransactionKind::TakeAllFromContainer;
        const auto openedCorpse = mCombat && mCombat->actors[2][8][2] <= 0
            ? std::optional(mCombatNpcOwner) : std::nullopt;
        return PreparedCommand(bound, mRuntime.prepare({ command.mInitiator }, command, openedCorpse));
    }

    PersistenceResult InventoryService::commit(const CanonicalServerState& players, PreparedCommand& command,
        EquipmentSessionCommitter& durability, std::unique_ptr<const InventoryTransferSuccess>& success,
        EquipmentBytes& bytes)
    {
        validate(players, command.mBinding);
        const auto image = command.image();
        if (image.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory image exceeds canonical record budget");
        auto retained = sealInventory(image);
        EquipmentBytes core(image.begin(), image.end());
        SealedCommitter sealed(durability, retained);
        const auto result = mRuntime.commit(command.mTransfer, sealed, success, bytes);
        if (result == PersistenceResult::Accepted)
        {
            mImage.swap(retained);
            mCoreImage.swap(core);
            retireCommittedEffects();
        }
        return result;
    }

    void InventoryService::retireCommittedEffects() noexcept
    {
        // This service publishes the committed image as baselines. The runtime's
        // diagnostic notifications have no further consumer here; retaining them
        // would turn the per-command bound into a lifetime transfer limit.
        for (size_t owner = 0; owner < mRuntime.ownerCount(); ++owner)
        {
            auto& effects = mRuntime.effects(owner);
            effects.mListener.mCalls = 0;
            effects.mListener.mRemovals.clear();
            effects.mInventoryUpdates = 0;
            effects.mNotifications.clear();
        }
    }

    FileReadResult InventoryService::recover(const std::filesystem::path& path, std::span<const ESM::RefId> references,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        if (mBinding.mStreamExteriors)
        {
            EquipmentBytes image;
            const auto read = readBoundedFile(path, MaximumNativeInventoryImageBytes, image, faults);
            if (read != FileReadResult::Read) return read;
            recover(std::as_bytes(std::span(image)), references);
            bytes.swap(image);
            return FileReadResult::Read;
        }
        std::unique_ptr<const EquipmentSessionValues> values;
        EquipmentBytes image;
        const auto result = readBoundedFile(path, MaximumNativeInventoryImageBytes, image, faults);
        if (result != FileReadResult::Read) return result;
        EquipmentBytes retained = image;
        mRuntime.restoreSession(std::move(image), references, values, bytes);
        mImage.swap(retained);
        return result;
    }

    class InventoryService::Transaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        CanonicalServerState players;
        PreparedCommand prepared;
        Transaction(InventoryService& owner, const CanonicalServerState& state, PreparedCommand command)
            : service(owner), players(state), prepared(std::move(command)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            struct Sink final : EquipmentSessionCommitter
            {
                const NativeInventoryCommit& persist;
                explicit Sink(const NativeInventoryCommit& value) : persist(value) {}
                PersistenceResult commit(std::span<const char> image) noexcept override
                {
                    const auto result = persist(std::as_bytes(image));
                    return result == CanonicalDurabilityResult::Committed ? PersistenceResult::Accepted
                        : result == CanonicalDurabilityResult::Rejected ? PersistenceResult::Rejected
                        : PersistenceResult::Uncertain;
                }
            } sink(persist);
            std::unique_ptr<const InventoryTransferSuccess> success;
            EquipmentBytes bytes;
            const auto result = service.commit(players, prepared, sink, success, bytes);
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected
                : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::EquipmentTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        CanonicalServerState players;
        std::optional<ServerApp::InventoryCommandBinding> binding;
        EquipmentRuntime::PreparedEquipment prepared;
        EquipmentTransaction(InventoryService& owner, const CanonicalServerState& state,
            std::optional<ServerApp::InventoryCommandBinding> bound, EquipmentRuntime::PreparedEquipment candidate)
            : service(owner), players(state), binding(std::move(bound)), prepared(std::move(candidate)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            if (binding) service.validate(players, *binding);
            auto retained = service.sealInventory(prepared.image());
            EquipmentBytes core(prepared.image().begin(), prepared.image().end());
            struct Sink final : EquipmentSessionCommitter
            {
                const NativeInventoryCommit& persist;
                explicit Sink(const NativeInventoryCommit& value) : persist(value) {}
                PersistenceResult commit(std::span<const char> image) noexcept override
                {
                    const auto result = persist(std::as_bytes(image));
                    return result == CanonicalDurabilityResult::Committed ? PersistenceResult::Accepted
                        : result == CanonicalDurabilityResult::Rejected ? PersistenceResult::Rejected : PersistenceResult::Uncertain;
                }
            } sink(persist);
            std::unique_ptr<const EquipmentSuccess> success;
            EquipmentBytes bytes;
            SealedCommitter sealed(sink, retained);
            const auto result = service.mRuntime.commit(prepared, sealed, success, bytes);
            if (result == PersistenceResult::Accepted)
            {
                service.mImage.swap(retained);
                service.mCoreImage.swap(core);
                service.retireCommittedEffects();
            }
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::WorldTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        CanonicalServerState players;
        ServerApp::InventoryCommandBinding binding;
        EquipmentRuntime::PreparedWorldTransfer prepared;
        WorldTransaction(InventoryService& owner, const CanonicalServerState& state,
            ServerApp::InventoryCommandBinding bound, EquipmentRuntime::PreparedWorldTransfer candidate)
            : service(owner), players(state), binding(std::move(bound)), prepared(std::move(candidate)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            service.validate(players, binding);
            auto retained = service.sealInventory(prepared.image());
            EquipmentBytes core(prepared.image().begin(), prepared.image().end());
            struct Sink final : EquipmentSessionCommitter
            {
                const NativeInventoryCommit& persist;
                explicit Sink(const NativeInventoryCommit& value) : persist(value) {}
                PersistenceResult commit(std::span<const char> image) noexcept override
                {
                    const auto result = persist(std::as_bytes(image));
                    return result == CanonicalDurabilityResult::Committed ? PersistenceResult::Accepted
                        : result == CanonicalDurabilityResult::Rejected ? PersistenceResult::Rejected : PersistenceResult::Uncertain;
                }
            } sink(persist);
            EquipmentBytes bytes;
            SealedCommitter sealed(sink, retained);
            const auto result = service.mRuntime.commit(prepared, sealed, bytes);
            if (result == PersistenceResult::Accepted)
            {
                service.mImage.swap(retained);
                service.mCoreImage.swap(core);
                service.retireCommittedEffects();
            }
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::DoorTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        EquipmentRuntime::PreparedDoor prepared;
        DoorTransaction(InventoryService& owner, EquipmentRuntime::PreparedDoor change)
            : service(owner), prepared(std::move(change)) {}
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        try
        {
            auto retained = service.sealInventory(prepared.image());
            EquipmentBytes core(prepared.image().begin(), prepared.image().end());
            struct Sink final : EquipmentSessionCommitter
            {
                const NativeInventoryCommit& persist;
                explicit Sink(const NativeInventoryCommit& value) : persist(value) {}
                PersistenceResult commit(std::span<const char> image) noexcept override
                {
                    const auto result = persist(std::as_bytes(image));
                    return result == CanonicalDurabilityResult::Committed ? PersistenceResult::Accepted
                        : result == CanonicalDurabilityResult::Rejected ? PersistenceResult::Rejected : PersistenceResult::Uncertain;
                }
            } sink(persist);
            EquipmentBytes bytes;
            SealedCommitter sealed(sink, retained);
            const auto result = service.mRuntime.commit(prepared, sealed, bytes);
            if (result == PersistenceResult::Accepted) { service.mImage.swap(retained); service.mCoreImage.swap(core); }
            return result == PersistenceResult::Accepted ? CanonicalDurabilityResult::Committed
                : result == PersistenceResult::Rejected ? CanonicalDurabilityResult::Rejected : CanonicalDurabilityResult::Failed;
        }
        catch (...) { return CanonicalDurabilityResult::Rejected; }
    };

    class InventoryService::TeleportTransaction final : public PreparedNativeInventory
    {
        Transform mDestination;
        EquipmentBytes mBefore;
        bool mConsumed = false;
    public:
        InventoryService& service;
        TeleportTransaction(InventoryService& owner, Transform destination)
            : mDestination(destination), mBefore(owner.mImage), service(owner) {}
        std::optional<Transform> playerDestination() const override { return mDestination; }
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        {
            if (mConsumed || service.inventoryImage().empty() || service.mImage != mBefore)
                return CanonicalDurabilityResult::Rejected;
            try
            {
                // ActorTransaction composes this inventory image with the staged
                // simulation image. Passing inventoryImage() would nest an actor
                // campaign inside another actor campaign in navigation runtimes.
                const auto result = persist(std::as_bytes(std::span(service.mImage)));
                if (result != CanonicalDurabilityResult::Rejected) mConsumed = true;
                if (result == CanonicalDurabilityResult::Failed) service.mRuntime.mFailedClosed = true;
                return result;
            }
            catch (...) { service.mRuntime.mFailedClosed = true; return CanonicalDurabilityResult::Failed; }
        }
    };

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareDoorActivation(
        const CanonicalServerState& players, const ServerCommandProposal& proposal)
    {
        constexpr uint32_t ReachQuanta = 384 * 1024;
        const auto* input = std::get_if<InteractiveObjectCommandProposal>(&proposal.payload());
        const auto* session = players.findActiveSession(proposal.sessionId());
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (input && player && mBinding.mStreamExteriors)
        {
            for (size_t i = 0; i < mBinding.mDoors.size(); ++i)
            {
                const auto& placed = mBinding.mDoors[i];
                if (placed.mId != input->objectId().value()) continue;
                if (session->sessionGeneration() != proposal.sessionGeneration()
                    || proposal.entityPrecondition().entityId() != player->entityId()
                    || proposal.entityPrecondition().expectedAuthorityEpoch() != player->authorityEpoch()
                    || input->kind() != ObjectInteractionKind::Activate || input->requestedKey() || input->requestedTool()
                    || input->expectedInventoryRevision() || input->expectedCombatRevision()
                    || input->expectedRevision().value() != mAreaDoors[i].motion
                    || player->transform().cell() != placed.mCell || input->cell() != placed.mCell
                    || !positionsWithinReach(player->transform().position(), worldPosition(placed.mRef), ReachQuanta)
                    || !positionsWithinReach(input->interactionOrigin(), worldPosition(placed.mRef), ReachQuanta)
                    || !positionsWithinReach(player->transform().position(), input->interactionOrigin(), ReachQuanta)) return {};
                try { (void)actor(player->playerId()); return prepareAreaDoor(i, true, players, ServerTick::initial(), 0); }
                catch (const std::invalid_argument&) { return {}; }
            }
        }
        if (mBinding.mTeleportDoors && input && player)
        {
            const auto found = std::ranges::find(*mBinding.mTeleportDoors, input->objectId().value(),
                &InventoryServiceBinding::TeleportDoor::mId);
            if (found != mBinding.mTeleportDoors->end())
            {
                if (inventoryImage().empty() || session->sessionGeneration() != proposal.sessionGeneration()
                    || proposal.entityPrecondition().entityId() != player->entityId()
                    || proposal.entityPrecondition().expectedAuthorityEpoch() != player->authorityEpoch()
                    || input->kind() != ObjectInteractionKind::Activate || input->requestedKey() || input->requestedTool()
                    || input->expectedInventoryRevision() || input->expectedCombatRevision()
                    || input->expectedRevision() != ObjectRevision::initial() || input->cell() != found->mCell
                    || player->transform().cell() != found->mCell
                    || !positionsWithinReach(player->transform().position(), found->mPosition, ReachQuanta)
                    || !positionsWithinReach(input->interactionOrigin(), found->mPosition, ReachQuanta)
                    || !positionsWithinReach(player->transform().position(), input->interactionOrigin(), ReachQuanta)) return {};
                try
                {
                    (void)actor(player->playerId());
                    return std::make_unique<TeleportTransaction>(*this, found->mDestination);
                }
                catch (const std::invalid_argument&) { return {}; }
            }
        }
        if (!mBinding.mDoor || !input || !player || session->sessionGeneration() != proposal.sessionGeneration()
            || input->objectId().value() != mBinding.mDoorId || input->kind() != ObjectInteractionKind::Activate
            || input->requestedKey() || input->requestedTool() || input->expectedInventoryRevision() || input->expectedCombatRevision()
            || input->expectedRevision().value() != mRuntime.mDoorMotion
            || player->transform().cell() != mBinding.mWorldItems->mCell || input->cell() != mBinding.mWorldItems->mCell
            || !positionsWithinReach(player->transform().position(), worldPosition(*mBinding.mDoor), ReachQuanta)
            || !positionsWithinReach(input->interactionOrigin(), worldPosition(*mBinding.mDoor), ReachQuanta)
            || !positionsWithinReach(player->transform().position(), input->interactionOrigin(), ReachQuanta)) return {};
        try
        {
            (void)actor(player->playerId());
            return std::make_unique<DoorTransaction>(*this, mRuntime.prepareDoor(true, 0, false));
        }
        catch (const std::invalid_argument&) { return {}; }
    }

    void InventoryService::reportDoorObstruction(
        const CanonicalServerState& players, const ClientDoorObstruction& report, ServerTick tick)
    {
        if (mBinding.mStreamExteriors)
        {
            const auto* session = players.findActiveSession(report.session);
            const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
            if (!player || session->sessionGeneration() != report.generation || !report.sequence
                || report.observedTick > tick || tick.value() - report.observedTick.value() >= DoorObstructionLifetimeTicks) return;
            const auto actorIndex = std::ranges::find(mBinding.mPlayers, player->playerId());
            if (actorIndex == mBinding.mPlayers.end()) return;
            for (size_t i = 0; i < mBinding.mDoors.size(); ++i)
            {
                if (mBinding.mDoors[i].mId != report.placement || mBinding.mDoors[i].mCell != player->transform().cell()
                    || mAreaDoors[i].motion != report.motion || !mAreaDoors[i].state->mDoorState) continue;
                auto& previous = mAreaDoors[i].reports[size_t(actorIndex - mBinding.mPlayers.begin())];
                if (previous && previous->session == report.session && previous->generation == report.generation
                    && previous->motion == report.motion && (report.sequence <= previous->sequence
                        || report.observedTick < previous->observedTick)) return;
                previous = report;
            }
            return;
        }
        const auto* session = players.findActiveSession(report.session);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!mBinding.mDoor || !player || session->sessionGeneration() != report.generation
            || report.placement != mBinding.mDoorId || report.motion != mRuntime.mDoorMotion
            || !mRuntime.mDoorState || !mRuntime.mDoorState->mDoorState || !report.sequence
            || player->transform().cell() != mBinding.mWorldItems->mCell
            || report.observedTick > tick || tick.value() - report.observedTick.value() >= DoorObstructionLifetimeTicks) return;
        const auto found = std::ranges::find(mBinding.mPlayers, player->playerId());
        if (found == mBinding.mPlayers.end()) return;
        auto& previous = mDoorReports[size_t(found - mBinding.mPlayers.begin())];
        if (previous && previous->session == report.session && previous->generation == report.generation
            && previous->motion == report.motion && (report.sequence <= previous->sequence
                || report.observedTick < previous->observedTick)) return;
        previous = report;
    }

    bool InventoryService::doorBlocked(const CanonicalServerState& players, ServerTick tick) const
    {
        for (const auto& report : mDoorReports)
        {
            if (!report || !report->blocked || report->motion != mRuntime.mDoorMotion || report->observedTick > tick
                || tick.value() - report->observedTick.value() >= DoorObstructionLifetimeTicks) continue;
            const auto* session = players.findActiveSession(report->session);
            const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
            if (player && session->sessionGeneration() == report->generation
                && player->transform().cell() == mBinding.mWorldItems->mCell) return true;
        }
        return false;
    }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareDoorStep(
        const CanonicalServerState& players, ServerTick tick, float seconds)
    {
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > OrdinaryDoor::MaxStepSeconds)
            throw std::invalid_argument("Native door tick outside bounds");
        mDoorStepSeconds = seconds;
        if (mBinding.mStreamExteriors) return prepareAreaDoor(0, false, players, tick, seconds);
        if (!mRuntime.mDoorState || !mRuntime.mDoorState->mDoorState) return {};
        if (mBinding.mSecondWorldItems && std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                const auto* player = players.findPlayer(session.playerId());
                return player && player->transform().cell() == mBinding.mWorldItems->mCell;
            })) return {}; // Empty interiors freeze; returning never catches up or resets the door.
        return std::make_unique<DoorTransaction>(*this, mRuntime.prepareDoor(false, seconds, doorBlocked(players, tick)));
    }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareInventory(
        const CanonicalServerState& players, const ServerCommandProposal& proposal)
    {
        const auto binding = ServerApp::InventoryCommandBinding::fromProposal(players, proposal);
        if (!binding) return {};
        try
        {
            const auto& input = binding->transaction();
            // A scripted projectile's recoverable locals live on its consumed
            // source stack until contact. Keep that state frozen while flying.
            if (mCombat && std::ranges::any_of(mCombat->arrows, [&](const auto& arrow) {
                    if (arrow.terminal || arrow.casterKind != 1
                        || arrow.caster != binding->player().value()) return false;
                    const auto* ammo = mRuntime.mStore.get<ESM::Weapon>().search(
                        ESM::RefId::stringRefId(arrow.ammoRecord));
                    return ammo && !ammo->mScript.empty()
                        && ((input.stackId && input.stackId->value() == arrow.ammunition)
                            || ((input.kind == InventoryTransactionKind::EquipItem
                                    || input.kind == InventoryTransactionKind::UnequipItem)
                                && input.slot && (*input.slot == EquipmentSlot::Ammunition
                                    || *input.slot == EquipmentSlot::CarriedRight)));
                })) return {};
            if (input.kind == InventoryTransactionKind::PickupItem || input.kind == InventoryTransactionKind::DropItem)
            {
                validate(players, *binding);
                const auto& origin = players.findPlayer(binding->player())->transform().position();
                ESM::Position position{};
                position.pos[0] = float(double(origin.x()) / 1024);
                position.pos[1] = float(double(origin.y()) / 1024);
                position.pos[2] = float(double(origin.z()) / 1024);
                std::function<ESM::Position(const ESM::ObjectState&)> placement;
                const auto cell = players.findPlayer(binding->player())->transform().cell();
                const auto& domain = *worldDomain(cell);
                const auto world = cellWorldValues(cell);
                if (input.kind == InventoryTransactionKind::DropItem && domain.mPlacement)
                {
                    const auto& transform = players.findPlayer(binding->player())->transform();
                    position.rot[2] = -float(double(transform.orientation().z().value()) / 4294967296.0 * 2 * std::numbers::pi);
                    placement = [&](const ESM::ObjectState& state) {
                        // Query the actual resulting world model (including stock gold piles),
                        // after detached inventory preparation and before image encoding.
                        MWWorld::ManualRef reference(mRuntime.mStore, state.mRef.mRefID);
                        auto ptr = reference.getPtr();
                        ptr.getCellRef() = MWWorld::CellRef(state.mRef);
                        return domain.mPlacement(position, ptr, *input.placement, world.mObjects);
                    };
                }
                auto item = nativeId(*input.stackId);
                if (input.kind == InventoryTransactionKind::PickupItem)
                    for (const auto& object : world.mObjects)
                        if (worldId(object.mRef.mRefNum, domain) == input.stackId)
                            item = EquipmentRuntime::ownedId(object.mRef.mRefNum);
                auto prepared = mRuntime.prepareWorldTransfer(actor(binding->player()), item,
                    int(input.count), input.kind == InventoryTransactionKind::PickupItem, position,
                    input.expectedInventoryRevision.value(), placement, worldIndex(cell));
                if (prepared.image().size() > MaximumNativeInventoryImageBytes)
                    throw std::invalid_argument("Native world image exceeds canonical budget");
                return std::make_unique<WorldTransaction>(*this, players, *binding, std::move(prepared));
            }
            if (input.kind == InventoryTransactionKind::EquipItem || input.kind == InventoryTransactionKind::UnequipItem)
            {
                validate(players, *binding);
                if (mBinding.mSocialLifecycle && mCombat->players[actor(binding->player())].werewolf)
                    return {};
                if (!mBinding.mConstantEffects)
                {
                    const auto values = mRuntime.installedValues(actor(binding->player()));
                    const auto item = std::ranges::find_if(values.mObjects, [&](const auto& value) {
                        return wireId(value.mRef.mRefNum) == *input.stackId;
                    });
                    if (item != values.mObjects.end())
                    {
                        const auto record = MWWorld::inventoryItemRecord(mRuntime.mStore, item->mRef.mRefID);
                        if (record.mConstant || (*input.slot == EquipmentSlot::Shirt && !record.mEnchant.empty()))
                            return {};
                    }
                }
                const auto owner = EquipmentRuntime::ownedId(mRuntime.ownerPtr(actor(binding->player())).getCellRef().getRefNum());
                EquipmentCommand command{owner, nativeId(*input.stackId), input.expectedInventoryRevision.value(),
                    input.kind == InventoryTransactionKind::EquipItem ? EquipmentRequestedState::Equipped : EquipmentRequestedState::Unequipped,
                    static_cast<int>(*input.slot)};
                auto prepared = mRuntime.prepare(EquipmentCaller{owner}, command);
                if (prepared.image().size() > MaximumNativeInventoryImageBytes)
                    throw std::invalid_argument("Native equipment image exceeds canonical record budget");
                return std::make_unique<EquipmentTransaction>(*this, players, *binding, std::move(prepared));
            }
            return std::make_unique<Transaction>(*this, players, prepare(players, *binding));
        }
        catch (const std::invalid_argument&) { return {}; }
    }

    class InventoryService::AttackTransaction final : public PreparedNativeInventory
    {
    public:
        ClientMeleeAttackCommand attack;
        PlayerId attacker;
        AttackTransaction(ClientMeleeAttackCommand input, PlayerId player)
            : attack(std::move(input)), attacker(player) {}
        bool changesInventory() const noexcept override { return false; }
        // The actor tick owns persistence. A bare attack is never a durability candidate.
        CanonicalDurabilityResult commit(const NativeInventoryCommit&) noexcept override
        { return CanonicalDurabilityResult::Rejected; }
    };

    InventoryService::MagicCasterContext InventoryService::magicCaster(size_t index) const
    {
        if (index < mBinding.mPlayers.size()) return {index, index, {mBinding.mPlayers[index].value(),
            mBinding.mDurableCasters ? 1u : 0u, mBinding.mDurableCasters ? 1u : 0u}};
        const auto owner = combatOwner(index);
        const auto life = index == 2 ? &*mLife : &mNeighborLives.at(index - 3);
        return {index, owner, {mCombat->npcPlacements.empty() ? mBinding.mNavigatingActor->actorId()
                    : mCombat->npcPlacements.at(index - 2),
            mBinding.mDurableCasters ? 2u : 0u, mBinding.mDurableCasters ? life->generation : 0u}};
    }

    size_t InventoryService::combatOwner(size_t index) const
    {
        if (index < 2) return index;
        if (!mCombat || index >= mCombat->actors.size() || !mBinding.mNavigatingActor)
            throw std::invalid_argument("Native combat owner index invalid");
        if (index == 2) return mCombatNpcOwner;
        const auto placement = mCombat->npcPlacements.at(index - 2);
        const auto found = std::ranges::find_if(mBinding.mContainers, [placement](const auto& owner) {
            return owner.mId.value() == placement;
        });
        if (found == mBinding.mContainers.end())
            throw std::invalid_argument("Native combat placement has no inventory owner");
        return size_t(found - mBinding.mContainers.begin()) + 2;
    }

    namespace
    {
        std::variant<PlayerId, ActorId> wireCaster(ActorCasterIdentity caster)
        {
            if (caster.kind == 2) return ActorId::fromValue(caster.id).value();
            return PlayerId::fromValue(caster.id).value();
        }
    }

    class InventoryService::SpellTransaction final : public PreparedNativeInventory
    {
    public:
        ClientMagicUseCommand use;
        PlayerId caster;
        PreparedInstantSpell spell;
        std::optional<ItemCharge> charge;
        uint64_t effectSource = 0;
        std::unique_ptr<SpellTransaction> concurrent;
        SpellTransaction(ClientMagicUseCommand input, PlayerId player, PreparedInstantSpell record,
            std::optional<ItemCharge> spent = {}, uint64_t effect = 0)
            : use(std::move(input)), caster(player), spell(std::move(record)),
              charge(std::move(spent)), effectSource(effect) {}
        bool changesInventory() const noexcept override { return false; }
        CanonicalDurabilityResult commit(const NativeInventoryCommit&) noexcept override
        { return CanonicalDurabilityResult::Rejected; }
    };

    namespace
    {
        bool playerTravelEffects(const PreparedInstantEffects& effects)
        {
            return effects.effects.size() == 1 && effects.effects.front().mRange == ESM::RT_Self
                && effects.effects.front().mArea == 0 && effects.effects.front().mDuration == 0
                && (effects.effects.front().mEffectID == ESM::MagicEffect::Mark
                    || effects.effects.front().mEffectID == ESM::MagicEffect::Recall);
        }
    }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareMagicUse(
        const CanonicalServerState& players, const ServerCommandProposal& proposal, ServerTick tick)
    {
        if (!mBinding.mMagicUse || !mCombat || !mBinding.mNavigatingActor) return {};
        const auto* input = std::get_if<MagicUseCommandProposal>(&proposal.payload());
        if (!input) return {};
        const auto& use = input->command();
        const auto* session = players.findActiveSession(proposal.sessionId());
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        size_t npcTarget = 0;
        if (use.targetKind == MagicUseTargetKind::Actor && mBinding.mNavigatingActor)
        {
            const auto found = std::ranges::find(mCombat->npcPlacements, use.targetId);
            if (found != mCombat->npcPlacements.end())
                npcTarget = size_t(found - mCombat->npcPlacements.begin()) + 2;
            else if (use.targetId == mBinding.mNavigatingActor->actorId()) npcTarget = 2;
        }
        const ActorCampaignLife* targetLife = npcTarget == 2 ? (mLife ? &*mLife : nullptr)
            : npcTarget >= 3 && npcTarget - 3 < mNeighborLives.size()
                ? &mNeighborLives[npcTarget - 3] : nullptr;
        const auto doorTarget = std::ranges::find(mBinding.mDoors, use.targetId,
            &InventoryServiceBinding::OrdinaryDoorPlacement::mId);
        const auto containerTarget = std::ranges::find_if(mBinding.mContainers, [&](const auto& value) {
            return value.mId.value() == use.targetId && value.mPlacement
                && mRuntime.mStore.get<ESM::Container>().search(value.mBase);
        });
        const bool objectTarget = use.targetKind == MagicUseTargetKind::Door
            || use.targetKind == MagicUseTargetKind::Container;
        const bool validDoor = use.targetKind == MagicUseTargetKind::Door && player
            && doorTarget != mBinding.mDoors.end() && mBinding.mStreamExteriors
            && player->transform().cell() == doorTarget->mCell
            && doorTarget->mRef.mOwner.empty() && doorTarget->mRef.mFaction.empty()
            && mActiveAreas.size() > worldIndex(doorTarget->mCell)
            && mActiveAreas[worldIndex(doorTarget->mCell)]
            && use.expectedTargetRevision.value()
                == doorContactRevision(*mAreaDoors[size_t(doorTarget - mBinding.mDoors.begin())].state)
            && [&] {
                const auto& position = player->transform().position();
                const std::array<float, 3> origin{float(double(position.x()) / 1024),
                    float(double(position.y()) / 1024), float(double(position.z()) / 1024) + 64.f};
                const auto& door = *mAreaDoors[size_t(doorTarget - mBinding.mDoors.begin())].state;
                const std::array<float, 3> endpoint{door.mPosition.pos[0], door.mPosition.pos[1],
                    door.mPosition.pos[2] + 64.f};
                const float reach = mRuntime.mStore.get<ESM::GameSetting>()
                    .find("fCombatDistance")->mValue.getFloat();
                float distance = 0;
                for (size_t axis = 0; axis < 3; ++axis)
                    distance += (origin[axis] - endpoint[axis]) * (origin[axis] - endpoint[axis]);
                return distance <= reach * reach
                    && mBinding.mNavigatingActor->lineOfSightToDoor(origin, endpoint, use.targetId);
            }();
        const bool validContainer = use.targetKind == MagicUseTargetKind::Container && mBinding.mContainerMagic && player
            && containerTarget != mBinding.mContainers.end() && mBinding.mStreamExteriors
            && player->transform().cell() == containerTarget->mCell
            && containerTarget->mPlacement->mOwner.empty() && containerTarget->mPlacement->mFaction.empty()
            && mActiveAreas.size() > worldIndex(containerTarget->mCell)
            && mActiveAreas[worldIndex(containerTarget->mCell)]
            && use.expectedTargetRevision.value()
                == mContainerLocks[size_t(containerTarget - mBinding.mContainers.begin())].revision
            && [&] {
                const auto& position = player->transform().position();
                const std::array<float, 3> origin{float(double(position.x()) / 1024),
                    float(double(position.y()) / 1024), float(double(position.z()) / 1024) + 64.f};
                const auto& ref = *containerTarget->mPlacement;
                const std::array<float, 3> endpoint{ref.mPos.pos[0], ref.mPos.pos[1], ref.mPos.pos[2] + 64.f};
                const float reach = mRuntime.mStore.get<ESM::GameSetting>()
                    .find("fCombatDistance")->mValue.getFloat();
                float distance = 0;
                for (size_t axis = 0; axis < 3; ++axis)
                    distance += (origin[axis] - endpoint[axis]) * (origin[axis] - endpoint[axis]);
                return distance <= reach * reach && mBinding.mNavigatingActor->lineOfSight(origin, endpoint);
            }();
        if (!player || session->sessionGeneration() != proposal.sessionGeneration()
            || use.sessionId != proposal.sessionId() || use.sessionGeneration != proposal.sessionGeneration()
            || std::ranges::find(mBinding.mPlayers, player->playerId()) == mBinding.mPlayers.end()
            || (mBinding.mSocialLifecycle && mCombat->players[actor(player->playerId())].werewolf)
            || (player->transform().cell() != actorCell(mBinding.mNavigatingActor->snapshot())
                && !worldDomain(player->transform().cell()))
            || (mBinding.mPlayerCastLifecycle && (mCombat->playerCasts[actor(player->playerId())]
                || (mCombat->swings[actor(player->playerId())] && mCombat->swings[actor(player->playerId())]->pending())))
            || hasParalysis(mTimedEffects, actor(player->playerId()))
            || (mBinding.mKnockoutRules && mCombat->knockedDown[actor(player->playerId())])
            || (mBinding.mKnockoutAnimation && mCombat->hitRecoveryTicks[actor(player->playerId())])
            || (use.sourceKind != MagicUseSourceKind::Spell
                && (use.sourceKind != MagicUseSourceKind::EnchantedItem || !mBinding.mMagicItemUse))
            || !use.sourceId || (mBinding.mMagicProjectileCollection && !use.commandId.value())
            || (mBinding.mMagicProjectileCollection
                ? ((use.targetKind != MagicUseTargetKind::Self && !objectTarget
                        && mProjectiles.size() >= MaximumActorProjectiles)
                    || std::ranges::any_of(mProjectiles, [&](const auto& pending) {
                        return pending.caster == player->playerId().value()
                            && (!mBinding.mDurableCasters || (pending.casterKind == 1 && pending.casterLife == 1))
                            && pending.commandId == use.commandId.value();
                    }))
                : !mProjectiles.empty())
            || (use.targetKind != MagicUseTargetKind::Self
                && (!mBinding.mMagicProjectile || !mLife
                    || (use.targetKind == MagicUseTargetKind::Actor
                        ? (!targetLife || targetLife->respawnTick || mCombat->actors[npcTarget][8][2] <= 0
                            || use.sourceServerTick.value() < targetLife->bornTick
                            || use.expectedTargetRevision.value() < targetLife->bornTick)
                        : (objectTarget ? !(validDoor || validContainer)
                            : (use.targetKind != MagicUseTargetKind::Player || !mBinding.mMagicPlayerTarget
                            || use.targetId == player->playerId().value()
                            || std::ranges::none_of(mBinding.mPlayers, [&](PlayerId id) {
                                const auto* target = players.findPlayer(id);
                                return id.value() == use.targetId && target
                                    && target->transform().cell() == actorCell(mBinding.mNavigatingActor->snapshot())
                                    && std::ranges::any_of(players.activeSessions(), [&](const auto& active) {
                                        return active.playerId() == id;
                                    }) && mCombat->actors[actor(id)][8][2] > 0;
                            }))))))
            || (use.targetKind == MagicUseTargetKind::Self && use.targetId)
            || use.sourceServerTick.value() > tick.value()
            || tick.value() - use.sourceServerTick.value() > 64
            || (use.targetKind == MagicUseTargetKind::Self
                && use.expectedCasterRevision != use.expectedTargetRevision)
            || use.expectedCasterRevision.value() > tick.value()
            || (!objectTarget
                && use.expectedTargetRevision.value() > tick.value())
            || mCombat->actors[actor(player->playerId())][8][2] <= 0)
            return {};
        if (use.sourceKind == MagicUseSourceKind::EnchantedItem
            && use.expectedInventoryRevision.value() != mWorld.getPtrRegistryRevision()) return {};
        auto prepared = preparePlayerMagicSource(player->playerId(), use, *mCombat, mTimedEffects);
        if (player->transform().cell() != actorCell(mBinding.mNavigatingActor->snapshot())
            && (!mBinding.mPlayerTravel || !prepared || !playerTravelEffects(prepared->spell.effects))) return {};
        return prepared;
    }

    std::unique_ptr<InventoryService::SpellTransaction> InventoryService::preparePlayerMagicSource(
        PlayerId player, const ClientMagicUseCommand& use, const ActorCampaignCombat& combat,
        std::span<const ActorCampaignTimedEffect> effectsState, const PreparedNativeInventory* inventory)
    {
        const bool objectTarget = use.targetKind == MagicUseTargetKind::Door
            || use.targetKind == MagicUseTargetKind::Container;
        const auto acceptsObjectTarget = [&](const PreparedInstantEffects& effects) {
            const auto objectEffect = [](const ESM::ENAMstruct& effect) {
                return effect.mEffectID == ESM::MagicEffect::Lock
                    || effect.mEffectID == ESM::MagicEffect::Open;
            };
            const auto travelEffect = [](const ESM::ENAMstruct& effect) {
                return effect.mEffectID == ESM::MagicEffect::Mark
                    || effect.mEffectID == ESM::MagicEffect::Recall;
            };
            if (use.targetKind == MagicUseTargetKind::Door || use.targetKind == MagicUseTargetKind::Container)
                return effects.effects.size() == 1 && objectEffect(effects.effects.front())
                    && effects.effects.front().mRange == ESM::RT_Touch
                    && effects.effects.front().mArea == 0 && effects.effects.front().mDuration == 0;
            if (std::ranges::any_of(effects.effects, travelEffect))
                return mBinding.mPlayerTravel && use.targetKind == MagicUseTargetKind::Self
                    && effects.effects.size() == 1 && effects.effects.front().mRange == ESM::RT_Self
                    && effects.effects.front().mArea == 0 && effects.effects.front().mDuration == 0;
            return std::ranges::none_of(effects.effects, objectEffect);
        };
        if (use.sourceKind == MagicUseSourceKind::EnchantedItem)
        {
            const auto context = magicCaster(actor(player));
            const auto values = combatEquipmentValues(context.combatIndex, inventory);
            const auto item = std::ranges::find_if(values.mObjects, [&](const auto& object) {
                return object.mRef.mCount > 0 && wireId(object.mRef.mRefNum).value() == use.sourceId;
            });
            if (item == values.mObjects.end()) return {};
            MWWorld::ManualRef reference(mRuntime.mStore, item->mRef.mRefID);
            auto ptr = reference.getPtr();
            const auto enchantId = ptr.getClass().getEnchantment(ptr);
            const auto* enchantment = enchantId.empty() ? nullptr : mRuntime.mStore.get<ESM::Enchantment>().search(enchantId);
            if (!enchantment || (enchantment->mData.mType != ESM::Enchantment::WhenUsed
                    && enchantment->mData.mType != ESM::Enchantment::CastOnce)) return {};
            const uint64_t effectSource = spellRecordId(enchantId);
            if (!effectSource || enchantmentBySource(mRuntime.mStore, effectSource) != enchantment) return {};
            const auto caster = loadCombatStats(mRuntime.mStore, combat.actors[context.combatIndex],
                effectsState, context.combatIndex);
            auto prepared = prepareEnchantmentCast(*enchantment, caster, item->mRef.mEnchantmentCharge,
                mRuntime.mStore, mBinding.mActorEffectLifecycle, mBinding.mExpandedEffects,
                mBinding.mSpecialConditions, mBinding.mMovementEffects,
                objectTarget, mBinding.mPlayerTravel);
            if (!prepared || !prepared->affordable || !acceptsObjectTarget(prepared->effects)) return {};
            const auto& effects = prepared->effects;
            if (std::ranges::any_of(effects.effects,
                    [&](const auto& effect) { return (!mBinding.mNpcCastLifecycle && (!effect.mAttribute.empty() || !effect.mSkill.empty()))
                        || (effect.mArea != 0 && (!mBinding.mMagicArea || (!mBinding.mNpcCastLifecycle && effect.mRange != ESM::RT_Target))); })
                || (!mBinding.mMagicTimed && std::ranges::any_of(effects.effects,
                    [](const auto& effect) { return effect.mDuration != 0; }))
                || (use.targetKind == MagicUseTargetKind::Self
                    ? !effects.onlyRange(ESM::RT_Self)
                    : (!effects.hasRange(ESM::RT_Target) && !(mBinding.mNpcCastLifecycle && effects.hasRange(ESM::RT_Touch)))
                        || (!mBinding.mNpcCastLifecycle && effects.hasRange(ESM::RT_Touch)))) return {};
            // CastOnce consumes one item; its pending effects retain record identity.
            if (prepared->consume && !ptr.getClass().getScript(ptr).empty()) return {};
            return std::make_unique<SpellTransaction>(use, player,
                PreparedInstantSpell{0, std::move(prepared->effects)},
                ItemCharge{context.inventoryOwner, item->mRef.mRefNum, item->mRef.mEnchantmentCharge,
                    prepared->chargeAfter, prepared->consume}, effectSource);
        }
        const auto& known = mRuntime.mStore.get<ESM::NPC>().find(mBinding.mActors[actor(player)].mBase)->mSpells.mList;
        const ESM::Spell* selected = nullptr;
        for (const auto& id : known)
            if (!id.empty() && spellRecordId(id) == use.sourceId)
            {
                if (selected) return {}; // Hash collision cannot grant another spell.
                selected = mRuntime.mStore.get<ESM::Spell>().search(id);
            }
        if (!selected) return {};
        auto prepared = prepareInstantSpell(*selected, mRuntime.mStore, mBinding.mActorEffectLifecycle,
            mBinding.mExpandedEffects, mBinding.mSpecialConditions, mBinding.mMovementEffects,
            objectTarget, mBinding.mPlayerTravel);
        if (!prepared || !acceptsObjectTarget(prepared->effects)
            || std::ranges::any_of(prepared->effects.effects,
                [&](const auto& effect) { return (!mBinding.mNpcCastLifecycle && (!effect.mAttribute.empty() || !effect.mSkill.empty()))
                    || (effect.mArea != 0 && (!mBinding.mMagicArea || (!mBinding.mNpcCastLifecycle && effect.mRange != ESM::RT_Target))); })
            || (!mBinding.mMagicTimed && std::ranges::any_of(prepared->effects.effects,
                [](const auto& effect) { return effect.mDuration != 0; }))
            || (use.targetKind == MagicUseTargetKind::Self
                ? !prepared->effects.onlyRange(ESM::RT_Self)
                : (!prepared->effects.hasRange(ESM::RT_Target) && !(mBinding.mNpcCastLifecycle && prepared->effects.hasRange(ESM::RT_Touch)))
                    || (!mBinding.mNpcCastLifecycle && prepared->effects.hasRange(ESM::RT_Touch)))
            || combat.actors[actor(player)][9][2] < prepared->cost) return {};
        return std::make_unique<SpellTransaction>(use, player, std::move(*prepared));
    }

    bool InventoryService::appendMagicUse(const CanonicalServerState& players,
        const ServerCommandProposal& proposal, ServerTick tick, PreparedNativeInventory& candidate)
    {
        auto* first = dynamic_cast<SpellTransaction*>(&candidate);
        if (!mBinding.mNpcCastLifecycle || !first || first->concurrent) return false;
        auto next = prepareMagicUse(players, proposal, tick);
        auto* second = dynamic_cast<SpellTransaction*>(next.get());
        if (!second || first->caster == second->caster
            || mProjectiles.size() + size_t(first->spell.effects.hasRange(ESM::RT_Target))
                + size_t(second->spell.effects.hasRange(ESM::RT_Target)) > MaximumActorProjectiles) return false;
        first->concurrent.reset(static_cast<SpellTransaction*>(next.release()));
        return true;
    }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareMeleeAttack(
        const CanonicalServerState& players, const ServerCommandProposal& proposal, ServerTick tick)
    try
    {
        if (!mBinding.mCombatResolution || !mCombat || !mBinding.mNavigatingActor) return {};
        const auto* input = std::get_if<MeleeAttackCommandProposal>(&proposal.payload());
        if (!input) return {};
        const auto& attack = input->command();
        const auto* session = players.findActiveSession(proposal.sessionId());
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player || session->sessionGeneration() != proposal.sessionGeneration()
            || attack.sessionId != proposal.sessionId() || attack.sessionGeneration != proposal.sessionGeneration()
            || std::ranges::find(mBinding.mPlayers, player->playerId()) == mBinding.mPlayers.end()
            || (!attack.targetActorId && !mBinding.mAuthoritativeAim)
            || (attack.attackType != MeleeAttackType::Chop && attack.attackType != MeleeAttackType::Slash
                && attack.attackType != MeleeAttackType::Thrust)
            || !std::isfinite(attack.attackStrength) || attack.attackStrength < 0 || attack.attackStrength > 1
            || (mBinding.mRangedFlight && tick.value() <= mActorTick)
            || attack.sourceServerTick.value() > tick.value()
            || tick.value() - attack.sourceServerTick.value() > PhysicalAttackRetryTicks)
            return {};
        size_t targetIndex = attack.targetActorId
            && attack.targetActorId->value() == mBinding.mNavigatingActor->actorId() ? 2 : 0;
        if (!targetIndex && attack.targetActorId && mBinding.mNeighborCombat)
        {
            const auto neighbors = mBinding.mNavigatingActor->neighborSnapshots();
            const auto found = std::ranges::find(neighbors, attack.targetActorId->value(),
                &ActorSceneSnapshot::mActor);
            if (found != neighbors.end()) targetIndex = size_t(found - neighbors.begin()) + 3;
        }
        if (attack.targetActorId && !targetIndex) return {};
        if (targetIndex && ([&] {
                const auto& life = targetIndex == 2 ? *mLife : mNeighborLives.at(targetIndex - 3);
                return attack.sourceServerTick.value() < life.bornTick
                    || attack.expectedTargetRevision.value() < life.bornTick || life.respawnTick;
            }()))
            return {};
        const size_t owner = actor(player->playerId());
        if (mBinding.mBowRelease && std::ranges::any_of(mCombat->arrows, [&](const auto& arrow) {
                return arrow.caster == player->playerId().value() && arrow.command == attack.commandId.value();
            })) return {};
        if (mBinding.mPlayerMelee[owner] && (mCombat->hitRecoveryTicks[owner]
                || (mBinding.mPlayerCastLifecycle && mCombat->playerCasts[owner])
                || (mCombat->swings[owner] && (mCombat->swings[owner]->pending()
                    || mCombat->swings[owner]->command == attack.commandId.value())))) return {};
        if (mCombat->actors[owner][8][2] <= 0 || (targetIndex && mCombat->actors[targetIndex][8][2] <= 0)
            || hasParalysis(mTimedEffects, owner)
            || (mBinding.mKnockoutRules && mCombat->knockedDown[owner]))
            return {};
        const auto held = mRuntime.equippedWeaponCondition(owner);
        if (player->transform().cell() != actorCell(targetIndex <= 2
                ? mBinding.mNavigatingActor->snapshot()
                : mBinding.mNavigatingActor->neighborSnapshots().at(targetIndex - 3)))
            return {};
        const ESM::Weapon* weapon = nullptr;
        if (held)
        {
            const auto values = mRuntime.installedValues(owner);
            const auto item = std::ranges::find(values.mObjects, held->mItem,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == values.mObjects.end() || mRuntime.mStore.find(item->mRef.mRefID) != ESM::Weapon::sRecordId)
                return {};
            weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
        }
        if (held && held->mCondition <= 0
            && (!mBinding.mRangedRelease
                || (MWMechanics::getWeaponType(weapon->mData.mType)->mFlags & ESM::WeaponType::HasHealth))) return {};
        const bool bow = mBinding.mBowRelease && rangedWeapon(weapon, mBinding.mRangedRelease);
        if (!targetIndex && !bow) return {};
        if (mBinding.mAuthoritativeAim && (bow != attack.aimPoint.has_value())) return {};
        if (bow && (!attack.commandId.value()
                || mCombat->arrows.size() >= MaximumActorProjectiles
                || !equippedAmmunition(mRuntime.installedValues(owner), mRuntime.mStore, *weapon,
                    mBinding))) return {};
        if (mBinding.mPlayerMelee[owner])
        {
            const std::array<std::string_view, 3> directions{"chop", "slash", "thrust"};
            (void)mBinding.mPlayerMelee[owner](weapon, directions[size_t(attack.attackType)]);
        }
        const auto& position = player->transform().position();
        const osg::Vec3f origin(float(double(position.x()) / 1024), float(double(position.y()) / 1024),
            float(double(position.z()) / 1024));
        const auto scene = targetIndex <= 2 ? mBinding.mNavigatingActor->snapshot()
            : mBinding.mNavigatingActor->neighborSnapshots().at(targetIndex - 3);
        const osg::Vec3f target(scene.mPosition[0], scene.mPosition[1], scene.mPosition[2]);
        if (attack.aimPoint)
        {
            const auto& aim = *attack.aimPoint;
            if (std::ranges::any_of(aim, [](float v) { return !std::isfinite(v) || std::abs(v) > 10'000'000.f; }))
                return {};
            const osg::Vec3f direction(aim[0] - origin.x(), aim[1] - origin.y(), aim[2] - origin.z() - 110.f);
            if (!std::isfinite(direction.length2()) || direction.length2() < 1.f
                || direction.length2() > 16384.f * 16384.f) return {};
        }
        if (!bow && !MWMechanics::isInMeleeReach(origin, target, 0, 0,
                MWMechanics::getMeleeWeaponReach(mRuntime.mStore, weapon, true)))
            return {};
        return std::make_unique<AttackTransaction>(attack, player->playerId());
    }
    catch (const std::invalid_argument&) { return {}; }

    std::span<const std::byte> InventoryService::inventoryImage() const noexcept
    {
        return mRuntime.mFailedClosed || mRuntime.mRestartActor ? std::span<const std::byte>{}
            : std::as_bytes(std::span(mBinding.mNavigatingActor ? mActorImage : mImage));
    }

    void InventoryService::recover(std::span<const std::byte> image, std::span<const ESM::RefId> references)
    {
        if (image.empty() || image.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native inventory recovery image bound invalid");
        if (mBinding.mNavigatingActor)
        {
            auto decoded = readActorCampaign({reinterpret_cast<const char*>(image.data()), image.size()});
            const auto [aiNpc, aiUndead] = aiActorKind(mRuntime.ownerPtr(mCombatNpcOwner));
            if (bool(decoded.melee) != bool(mMelee)
                || (decoded.melee && !mBinding.mWeaponMelee && decoded.melee->identity != mBinding.mBoundMelee->mResourceIdentity))
                throw std::invalid_argument("Native melee resource binding differs from campaign");
            if (bool(decoded.combat) != mBinding.mCombatState)
                throw std::invalid_argument("Native combat campaign version differs from binding");
            if (mBinding.mPlacementCombat)
            {
                const auto neighbors = mBinding.mNavigatingActor->neighborSnapshots();
                std::vector<uint64_t> placements{mBinding.mNavigatingActor->actorId()};
                for (const auto& neighbor : neighbors) placements.push_back(neighbor.mActor);
                if (!decoded.combat || decoded.combat->npcPlacements != placements)
                    throw std::invalid_argument("Native combat placements differ from bound actors");
            }
            if (decoded.combat && decoded.combat->fleeTarget
                && std::ranges::none_of(mBinding.mPlayers, [&](PlayerId player) {
                    return player.value() == decoded.combat->fleeTarget;
                }))
                throw std::invalid_argument("Native flee target differs from bound players");
            if (decoded.combat && mBinding.mBoundHits)
                for (size_t i = 0; i < mBinding.mBoundHits->size(); ++i)
                {
                    const auto& hit = (*mBinding.mBoundHits)[i].animations;
                    const unsigned maximum = hit.count ? *std::max_element(hit.ticks.begin(), hit.ticks.end()) : 1;
                    if (mBinding.mKnockoutAnimation
                        && decoded.combat->knockoutFrame[i] >= (decoded.combat->hitKnockdown[i]
                            ? (*mBinding.mBoundHits)[i].knockdown.stop : (*mBinding.mBoundHits)[i].knockout.stop))
                        throw std::invalid_argument("Native knockout exceeds participant resource");
                    if (decoded.combat->hitRecoveryTicks[i] > maximum)
                        throw std::invalid_argument("Native hit recovery exceeds participant resource");
                    if (mBinding.mActorPresentation)
                    {
                        const auto group = decoded.combat->hitGroup[i];
                        if (group > hit.count || (group && decoded.combat->hitRecoveryTicks[i] > hit.ticks[group - 1])
                            || ((decoded.combat->knockedDown[i] || (group && decoded.combat->hitRecoveryTicks[i]))
                                && !decoded.combat->bodyAction[i]))
                            throw std::invalid_argument("Native body recipe differs from participant resource");
                    }
                }
            if (bool(decoded.life) != mBinding.mNpcLifecycle)
                throw std::invalid_argument("Native NPC lifecycle campaign version differs from binding");
            size_t headerOffset = 0;
            const auto magic = getAreaWord({reinterpret_cast<const char*>(image.data()), image.size()}, headerOffset);
            if (mBinding.mMagicProjectile != (magic == ProjectileActorCampaignMagic
                    || magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
                    || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic || hasKnockoutState(magic))
                || (mBinding.mMagicItemUse && magic != EnchantedProjectileActorCampaignMagic
                    && magic != TimedActorCampaignMagic && magic != AreaActorCampaignMagic
                    && magic != PlayerTargetActorCampaignMagic && magic != MultipleProjectileActorCampaignMagic
                    && !hasKnockoutState(magic))
                || (mBinding.mMagicTimed != (magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
                    || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
                    || hasKnockoutState(magic)))
                || (mBinding.mMagicArea != (magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic || hasKnockoutState(magic)))
                || (mBinding.mMagicPlayerTarget != (magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic || hasKnockoutState(magic)))
                || (mBinding.mMagicProjectileCollection != (magic == MultipleProjectileActorCampaignMagic
                    || hasKnockoutState(magic)))
                || (mBinding.mKnockoutRules != hasKnockoutState(magic))
                || (mBinding.mMeleeDefenseRules != (magic == MeleeDefenseActorCampaignMagic
                    || magic == EffectActorCampaignMagic || hasConstantState(magic)))
                || (mBinding.mActorEffectLifecycle != (magic == EffectActorCampaignMagic
                    || hasConstantState(magic)))
                || (mBinding.mConstantEffects != hasConstantState(magic))
                || (mBinding.mGeneralConstants != hasGeneralConstantState(magic))
                || (mBinding.mDurableCasters != (hasCasterState(magic))))
                throw std::invalid_argument("Native projectile campaign version differs from binding");
            if (bool(mBinding.mWeaponMelee) != hasWeaponExecution(magic)
                || bool(mBinding.mPlayerMelee[0]) != hasPlayerSwings(magic)
                || mBinding.mBowRelease != hasRangedRelease(magic)
                || mBinding.mRangedRelease != (magic == RangedReleaseCampaignMagic || hasRangedFlight(magic))
                || mBinding.mRangedFlight != hasRangedFlight(magic)
                || mBinding.mAuthoritativeAim != hasAuthoritativeAim(magic)
                || mBinding.mScriptedMovementRules != hasMovementRules(magic)
                || mBinding.mPlayerTravel != hasPlayerTravel(magic)
                || mBinding.mScriptedTravelRules != hasTravelRules(magic)
                || mBinding.mKnockoutAnimation != hasKnockoutAnimation(magic)
                || mBinding.mExpandedEffects != hasExpandedEffects(magic)
                || mBinding.mActorPresentation != hasActorPresentation(magic)
                || mBinding.mPlayerCastLifecycle != hasPlayerCasts(magic)
                || mBinding.mPersistentConditions != (magic == PersistentConditionsCampaignMagic
                    || magic == SpecialConditionsCampaignMagic || hasMovementEffects(magic))
                || mBinding.mSpecialConditions != (magic == SpecialConditionsCampaignMagic
                    || hasMovementEffects(magic))
                || mBinding.mMovementEffects != hasMovementEffects(magic)
                || mBinding.mAiDecisions != hasAiDecisions(magic)
                || mBinding.mPlayerAi != hasPlayerAi(magic)
                || mBinding.mSocialLifecycle != hasSocialLifecycle(magic)
                || mBinding.mPlacementCombat != hasPlacementCombat(magic)
                || mBinding.mNeighborCombat != hasNeighborCombat(magic))
                throw std::invalid_argument("Native weapon execution campaign differs from binding");
            if (mBinding.mNpcCastLifecycle != hasCastLifecycle(magic)
                || (mBinding.mNpcCastLifecycle && decoded.castResource != mBinding.mBoundCasts->resourceIdentity))
                throw std::invalid_argument("Native cast timing resource differs from binding");
            if (mBinding.mPlayerCastLifecycle)
                for (size_t i = 0; i < 2; ++i)
                {
                    if (decoded.combat->playerCastResources[i] != mBinding.mPlayerCasts[i].resourceIdentity)
                        throw std::invalid_argument("Native player cast resources differ from binding");
                    const auto& cast = decoded.combat->playerCasts[i];
                    if (!cast) continue;
                    const auto timing = mBinding.mPlayerCasts[i].ranges[cast->range];
                    if (cast->actor != mBinding.mPlayers[i].value() || cast->elapsed >= timing.stopTicks
                        || (cast->phase < ActorCampaignCast::Released ? cast->elapsed >= timing.releaseTicks : cast->elapsed < timing.releaseTicks)
                        || decoded.combat->actors[i][8][2] <= 0 || decoded.combat->knockedDown[i]
                        || decoded.combat->hitRecoveryTicks[i] || hasParalysis(decoded.timedEffects, i)
                        || (cast->targetKind == 2 && (mBinding.mNeighborCombat
                            ? std::ranges::find(decoded.combat->npcPlacements, cast->target)
                                == decoded.combat->npcPlacements.end()
                            : cast->target != mBinding.mNavigatingActor->actorId()))
                        || (cast->targetKind == 1 && (cast->target == cast->actor || std::ranges::none_of(mBinding.mPlayers,
                            [&](auto player) { return player.value() == cast->target; }))))
                        throw std::invalid_argument("Native player cast timing or participant invalid");
                }
            if (decoded.casting)
            {
                const auto& cast = *decoded.casting;
                const auto timing = mBinding.mBoundCasts->ranges[cast.range];
                if (cast.actor != mBinding.mNavigatingActor->actorId()
                    || cast.elapsed >= timing.stopTicks
                    || (cast.phase < ActorCampaignCast::Released ? cast.elapsed >= timing.releaseTicks : cast.elapsed < timing.releaseTicks))
                    throw std::invalid_argument("Native cast stage is outside bound animation");
            }
            if (mBinding.mMeleeContact != (magic == ContactActorCampaignMagic || magic == CombatActorCampaignMagic
                    || magic == LifeActorCampaignMagic || magic == ProjectileActorCampaignMagic
                    || magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
                    || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic
                    || magic == MultipleProjectileActorCampaignMagic || hasKnockoutState(magic)))
                throw std::invalid_argument("Native melee contact campaign version differs from binding");
            PlainEquipmentValues baseline;
            std::vector<PlainEquipmentValues> neighborBaselines;
            if (decoded.life)
            {
                const auto envelope = mRuntime.expectedEnvelope(mRuntime.ownerPtr(mCombatNpcOwner).getCellRef().getRefNum());
                decodeEquipment(decoded.life->spawnInventory,
                    {envelope, mRuntime.mStore, references, mRuntime.mScriptLocals, true}, baseline);
                if (baseline.mObjects.size() > PlainEquipmentValues::MaxItems
                    || decoded.life->spawnStats[8][2] <= 0)
                    throw std::invalid_argument("Native NPC respawn baseline invalid");
                (void)mBinding.mNavigatingActor->prepareSelectedRestore(decoded.life->spawnActor, actorDoorFrames());
            }
            if (mBinding.mNeighborCombat)
            {
                const auto neighbors = mBinding.mNavigatingActor->neighborSnapshots();
                if (decoded.neighborLives.size() != neighbors.size()
                    || decoded.combat->neighborAttacks.size() != neighbors.size()
                    || mBinding.mNeighborMeleeSet.size() != neighbors.size())
                    throw std::invalid_argument("Native neighbor life/attack domain invalid");
                for (size_t i = 0; i < neighbors.size(); ++i)
                {
                    const auto found = std::ranges::find_if(mBinding.mContainers, [&](const auto& owner) {
                        return owner.mId.value() == neighbors[i].mActor;
                    });
                    if (found == mBinding.mContainers.end())
                        throw std::invalid_argument("Native neighbor owner absent on recovery");
                    const size_t owner = size_t(found - mBinding.mContainers.begin()) + 2;
                    PlainEquipmentValues inventory;
                    decodeEquipment(decoded.neighborLives[i].spawnInventory,
                        {mRuntime.expectedEnvelope(mRuntime.ownerPtr(owner).getCellRef().getRefNum()),
                            mRuntime.mStore, references, mRuntime.mScriptLocals, true}, inventory);
                    if (inventory.mObjects.size() > PlainEquipmentValues::MaxItems
                        || decoded.neighborLives[i].spawnStats[8][2] <= 0)
                        throw std::invalid_argument("Native neighbor respawn baseline invalid");
                    (void)mBinding.mNavigatingActor->prepareNeighborRestore(
                        i, decoded.neighborLives[i].spawnActor, actorDoorFrames());
                    const auto& attack = decoded.combat->neighborAttacks[i];
                    if (attack.target && std::ranges::none_of(mBinding.mPlayers,
                            [&](auto player) { return player.value() == attack.target; }))
                        throw std::invalid_argument("Native neighbor attack target invalid");
                    const ESM::Weapon* weapon = attack.weapon.empty() ? nullptr
                        : mRuntime.mStore.get<ESM::Weapon>().search(ESM::RefId::stringRefId(attack.weapon));
                    if (!attack.weapon.empty() && !weapon)
                        throw std::invalid_argument("Native neighbor attack weapon absent");
                    if (attack.source && std::ranges::none_of(inventory.mObjects, [&](const auto& item) {
                            return wireId(item.mRef.mRefNum).value() == attack.source
                                && item.mRef.mRefID == weapon->mId;
                        }) && !attack.state.mHit)
                        throw std::invalid_argument("Native neighbor attack source changed");
                    const std::array<std::string_view, 3> modes{"chop", "slash", "thrust"};
                    auto clip = mBinding.mNeighborMeleeSet[i](weapon, modes[attack.direction]);
                    if (attack.identity != clip.identity())
                        throw std::invalid_argument("Native neighbor attack animation differs from binding");
                    clip.restore(attack.state);
                    neighborBaselines.push_back(std::move(inventory));
                }
            }
            auto restoredMelee = mMelee;
            if (decoded.melee && !mBinding.mWeaponMelee) restoredMelee->restore(decoded.melee->state);
            if (decoded.melee && decoded.melee->target
                && std::ranges::none_of(mBinding.mPlayers,
                    [&](PlayerId player) { return player.value() == decoded.melee->target; }))
                throw std::invalid_argument("Native melee target outside bound players");
            const auto knownCaster = [&](ActorCasterIdentity caster) {
                if (caster.kind == 1)
                    return caster.life == 1 && std::ranges::any_of(mBinding.mPlayers,
                        [&](PlayerId player) { return player.value() == caster.id; });
                if (caster.kind != 2 || !caster.life) return false;
                if (caster.id == mBinding.mNavigatingActor->actorId())
                    return caster.life <= decoded.life->generation;
                for (size_t i = 0; i < decoded.neighborLives.size(); ++i)
                    if (caster.id == decoded.combat->npcPlacements[i + 1])
                        return caster.life <= decoded.neighborLives[i].generation;
                return false;
            };
            if (mBinding.mDurableCasters)
                for (const auto& death : decoded.life->deaths)
                    if (!knownCaster({death.killer, death.killerKind, death.killerLife}))
                        throw std::invalid_argument("Native death caster outside bound actors");
            if (mBinding.mNeighborCombat)
                for (const auto& adjacent : decoded.neighborLives)
                    for (const auto& death : adjacent.deaths)
                        if (!knownCaster({death.killer, death.killerKind, death.killerLife}))
                            throw std::invalid_argument("Native neighbor death caster outside bound actors");
            for (const auto& pending : decoded.projectiles)
            {
                if (mBinding.mDurableCasters && (!knownCaster({pending.caster, pending.casterKind, pending.casterLife})
                    || (pending.casterKind == 2 && (pending.casterLife != decoded.life->generation
                        || decoded.life->respawnTick || decoded.combat->actors[2][8][2] <= 0
                        || pending.expiresTick < 90 || pending.expiresTick - 90 < decoded.life->bornTick
                        || pending.targetKind != 1))
                    || (pending.targetKind == 1 && pending.generation != 1)))
                    throw std::invalid_argument("Native projectile caster or target life unsupported");
                const auto caster = std::ranges::find_if(mBinding.mPlayers,
                    [&](PlayerId player) { return player.value() == pending.caster; });
                if ((pending.casterKind != 2 && caster == mBinding.mPlayers.end())
                    || (pending.targetKind == uint64_t(MagicUseTargetKind::Actor)
                        ? (mBinding.mNeighborCombat
                            ? std::ranges::find(decoded.combat->npcPlacements, pending.target)
                                == decoded.combat->npcPlacements.end()
                            : pending.target != mBinding.mNavigatingActor->actorId())
                        : (!mBinding.mMagicPlayerTarget || std::ranges::none_of(mBinding.mPlayers,
                            [&](PlayerId id) { return id.value() == pending.target && (pending.casterKind == 2 || id.value() != pending.caster); }))))
                    throw std::invalid_argument("Native projectile identities outside bound actors");
                if (pending.sourceKind == uint64_t(MagicUseSourceKind::Spell))
                {
                    const auto& known = mRuntime.mStore.get<ESM::NPC>()
                        .find(pending.casterKind == 2 ? mBinding.mContainers[mCombatNpcOwner - 2].mBase
                            : mBinding.mActors[size_t(caster - mBinding.mPlayers.begin())].mBase)->mSpells.mList;
                    size_t matches = 0;
                    for (const auto& id : known)
                        if (!id.empty() && spellRecordId(id) == pending.source)
                        {
                            const auto* selected = mRuntime.mStore.get<ESM::Spell>().search(id);
                            const auto plan = selected ? prepareInstantSpell(*selected, mRuntime.mStore,
                                mBinding.mActorEffectLifecycle, mBinding.mExpandedEffects,
                                mBinding.mSpecialConditions, mBinding.mMovementEffects) : std::nullopt;
                            if (!plan || !plan->effects.hasRange(ESM::RT_Target)
                                || (!mBinding.mNpcCastLifecycle && plan->effects.hasRange(ESM::RT_Touch)))
                                throw std::invalid_argument("Native projectile spell source invalid");
                            if (!mBinding.mMagicArea && std::ranges::any_of(plan->effects.effects,
                                    [](const auto& effect) { return effect.mArea != 0; }))
                                throw std::invalid_argument("Native projectile area unsupported by campaign");
                            ++matches;
                        }
                    if (matches != 1) throw std::invalid_argument("Native projectile spell identity ambiguous");
                }
                else
                {
                    const auto* selected = enchantmentBySource(mRuntime.mStore, pending.effectSource);
                    const auto effects = selected && (selected->mData.mType == ESM::Enchantment::WhenUsed
                        || selected->mData.mType == ESM::Enchantment::CastOnce)
                        ? prepareInstantEffects(selected->mEffects, mRuntime.mStore,
                            mBinding.mActorEffectLifecycle, mBinding.mExpandedEffects,
                            false, mBinding.mSpecialConditions, mBinding.mMovementEffects) : std::nullopt;
                    if (!effects || !effects->hasRange(ESM::RT_Target) || (!mBinding.mNpcCastLifecycle && effects->hasRange(ESM::RT_Touch))
                        || (!mBinding.mMagicArea && std::ranges::any_of(effects->effects,
                            [](const auto& effect) { return effect.mArea != 0; })))
                        throw std::invalid_argument("Native projectile enchantment source invalid");
                }
            }
            if (mBinding.mPersistentConditions)
                for (const auto& condition : decoded.combat->conditions)
                {
                    const auto* spell = spellBySource(mRuntime.mStore, condition.source);
                    if (!decoded.tick || !spell || !preparePersistentEffects(*spell, mRuntime.mStore,
                            mBinding.mSpecialConditions))
                        throw std::invalid_argument("Native saved condition source unsupported");
                    if (mBinding.mSpecialConditions && condition.nextWorseningMs
                        && !MWMechanics::Spells::hasCorprusEffect(spell))
                        throw std::invalid_argument("Native non-Corprus condition carries a worsening clock");
                }
            std::set<std::tuple<uint64_t, uint64_t, uint64_t>> conditionEffects;
            const auto recoveredOwner = [&](size_t index) {
                if (index < 2) return index;
                const auto placement = decoded.combat->npcPlacements.empty()
                    ? mBinding.mNavigatingActor->actorId() : decoded.combat->npcPlacements.at(index - 2);
                const auto found = std::ranges::find_if(mBinding.mContainers,
                    [placement](const auto& owner) { return owner.mId.value() == placement; });
                if (found == mBinding.mContainers.end())
                    throw std::invalid_argument("Native effect owner placement missing");
                return size_t(found - mBinding.mContainers.begin()) + 2;
            };
            const auto recoveredLife = [&](size_t index) -> const ActorCampaignLife& {
                return index == 2 ? *decoded.life : decoded.neighborLives.at(index - 3);
            };
            const auto recoveredPlacement = [&](size_t index) {
                return decoded.combat->npcPlacements.empty() ? mBinding.mNavigatingActor->actorId()
                    : decoded.combat->npcPlacements.at(index - 2);
            };
            if (mBinding.mActorEffectLifecycle)
                for (const auto& effect : decoded.timedEffects)
                {
                    const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
                    if (aiDispositionEffect(id))
                    {
                        const auto selected = mRuntime.ownerPtr(recoveredOwner(effect.actor));
                        const bool npc = effect.actor < 2 || selected.getType() == ESM::NPC::sRecordId;
                        const bool undead = effect.actor >= 2 && !npc
                            && selected.get<ESM::Creature>()->mBase->mData.mType == ESM::Creature::Undead;
                        if (!MWMechanics::validAiEffectTarget(id, npc, effect.actor < 2, undead, true))
                            throw std::invalid_argument("Native saved AI effect target invalid");
                    }
                    if ((!mBinding.mMovementEffects && (movementEffect(id) || aiDispositionEffect(id)))
                        || (!mBinding.mSpecialConditions && (id == ESM::MagicEffect::SunDamage
                        || id == ESM::MagicEffect::ResistCorprusDisease
                        || id == ESM::MagicEffect::WeaknessToCorprusDisease
                        || id == ESM::MagicEffect::CureCorprusDisease)))
                        throw std::invalid_argument("Native saved special effect requires V55");
                    if (!timedDamage(id) && !timedRestore(id) && !supportedTimedStatus(id)
                        && !(mBinding.mExpandedEffects && expandedCombatEffect(id))
                        && !(mBinding.mMovementEffects && (movementEffect(id) || aiDispositionEffect(id)))
                        && !(mBinding.mSpecialConditions && (id == ESM::MagicEffect::Corprus
                            || id == ESM::MagicEffect::Vampirism))
                        && !((mBinding.mNpcCastLifecycle || (mBinding.mConstantEffects && effect.sourceKind == 3))
                            && (id == ESM::MagicEffect::FortifyAttribute || id == ESM::MagicEffect::FortifySkill))
                        && !(mBinding.mNpcCastLifecycle && (id == ESM::MagicEffect::DrainAttribute
                            || id == ESM::MagicEffect::DrainSkill)))
                        throw std::invalid_argument("Native saved actor effect unsupported: "
                            + std::string(id.getRefIdString()) + " source=" + std::to_string(effect.sourceKind)
                            + " duration=" + std::to_string(effect.durationTicks));
                    const bool casterKnown = mBinding.mDurableCasters
                        ? knownCaster({effect.caster, effect.casterKind, effect.casterLife})
                        : effect.caster == mBinding.mNavigatingActor->actorId()
                        || std::ranges::any_of(mBinding.mPlayers,
                            [&](const auto& player) { return player.value() == effect.caster; });
                    const auto matchesEffect = [&](const ESM::EffectList& list) {
                        for (size_t ordinal = 0; ordinal < list.mList.size(); ++ordinal)
                        {
                            const auto& entry = list.mList[ordinal].mData;
                            const auto argument = !entry.mAttribute.empty() ? uint64_t(ESM::Attribute::refIdToIndex(entry.mAttribute) + 1)
                                : !entry.mSkill.empty() ? uint64_t(ESM::Skill::refIdToIndex(entry.mSkill) + 9) : 0;
                            if (entry.mEffectID == id && (mBinding.mExpandedEffects ? std::max(uint64_t(1), uint64_t(entry.mDuration) * 30) : uint64_t(entry.mDuration) * 30) == effect.durationTicks
                                && (!mBinding.mNpcCastLifecycle || (effect.ordinal == ordinal && effect.argument == argument))) return true;
                        }
                        return false;
                    };
                    bool sourceKnown = false;
                    if (effect.sourceKind == 3)
                        sourceKnown = mBinding.mConstantEffects
                            && effect.durationTicks == 0 && effect.expiresTick == UINT64_MAX
                            && effect.resistance == 0;
                    else if (effect.sourceKind == 4)
                    {
                        const auto* spell = spellBySource(mRuntime.mStore, effect.source);
                        const auto plan = spell ? preparePersistentEffects(*spell, mRuntime.mStore,
                            mBinding.mSpecialConditions) : std::nullopt;
                        if (plan && effect.ordinal < plan->effects.size())
                        {
                            const auto& entry = plan->effects[effect.ordinal];
                            const auto argument = !entry.mAttribute.empty() ? uint64_t(ESM::Attribute::refIdToIndex(entry.mAttribute) + 1)
                                : !entry.mSkill.empty() ? uint64_t(ESM::Skill::refIdToIndex(entry.mSkill) + 9) : 0;
                            const bool noMagnitude = mRuntime.mStore.get<ESM::MagicEffect>().find(id)->mData.mFlags & ESM::MagicEffect::NoMagnitude;
                            const float scale = 1.f - effect.resistance / 100.f;
                            sourceKnown = mBinding.mPersistentConditions && entry.mEffectID == id
                                && entry.mRange == ESM::RT_Self && effect.argument == argument && effect.durationTicks == 0 && effect.expiresTick == UINT64_MAX
                                && (noMagnitude ? effect.magnitude == 1.f
                                    : effect.magnitude >= entry.mMagnMin * scale && effect.magnitude <= entry.mMagnMax * scale)
                                && std::ranges::any_of(decoded.combat->conditions, [&](const auto& condition) {
                                    return sameCondition(condition, effect.actor, effect.source);
                                })
                                && conditionEffects.emplace(effect.actor, effect.source, effect.ordinal).second;
                        }
                    }
                    else if (effect.sourceKind == 5)
                    {
                        const auto* spell = spellBySource(mRuntime.mStore, effect.source);
                        const auto plan = spell ? preparePassiveActorEffects(*spell, mRuntime.mStore,
                            mBinding.mMovementEffects, mBinding.mAiDecisions) : std::nullopt;
                        const auto ptr = mRuntime.ownerPtr(recoveredOwner(effect.actor));
                        if (plan && effect.ordinal < plan->effects.size())
                        {
                            const auto& entry = plan->effects[effect.ordinal];
                            const bool noMagnitude = mRuntime.mStore.get<ESM::MagicEffect>().find(id)->mData.mFlags
                                & ESM::MagicEffect::NoMagnitude;
                            sourceKnown = (mBinding.mAiDecisions || mBinding.mMovementEffects)
                                && entry.mEffectID == id
                                && std::ranges::find(actorSpells(ptr).mList, spell->mId)
                                    != actorSpells(ptr).mList.end()
                                && (!aiDispositionEffect(id) || MWMechanics::validAiEffectTarget(id,
                                    effect.actor < 2 || ptr.getType() == ESM::NPC::sRecordId, effect.actor < 2,
                                    effect.actor >= 2 && ptr.getType() == ESM::Creature::sRecordId
                                        && ptr.get<ESM::Creature>()->mBase->mData.mType == ESM::Creature::Undead, true))
                                && effect.caster == (effect.actor >= 2
                                    ? recoveredPlacement(effect.actor)
                                    : mBinding.mPlayers[size_t(effect.actor)].value())
                                && effect.resistance == 0.f && effect.argument == 0
                                && effect.durationTicks == 0 && effect.expiresTick == UINT64_MAX
                                && (noMagnitude ? effect.magnitude == 1.f
                                    : effect.magnitude >= entry.mMagnMin
                                        && effect.magnitude <= entry.mMagnMax);
                        }
                    }
                    else if (effect.sourceKind == 0)
                    {
                        for (const auto& spell : mRuntime.mStore.get<ESM::Spell>())
                            if (spell.mData.mType == ESM::Spell::ST_Spell
                                && spellRecordId(spell.mId) == effect.source)
                                sourceKnown |= matchesEffect(spell.mEffects);
                    }
                    else if (const auto* enchantment = enchantmentBySource(mRuntime.mStore, effect.source);
                        enchantment && (effect.sourceKind == 2
                            ? enchantment->mData.mType == ESM::Enchantment::WhenStrikes
                            : enchantment->mData.mType == ESM::Enchantment::WhenUsed
                                || enchantment->mData.mType == ESM::Enchantment::CastOnce))
                        sourceKnown |= matchesEffect(enchantment->mEffects);
                    const bool constantCaster = !mBinding.mDurableCasters || effect.sourceKind < 3
                        || (effect.actor >= 2
                            ? effect.casterKind == 2 && effect.caster == recoveredPlacement(effect.actor)
                                && effect.casterLife == recoveredLife(effect.actor).generation
                            : effect.casterKind == 1 && effect.caster == mBinding.mPlayers[effect.actor].value());
                    bool launchLifeKnown = true;
                    if (mBinding.mDurableCasters && casterKnown && effect.casterKind == 2)
                    {
                        size_t casterIndex = 0;
                        for (size_t i = 2; i < decoded.combat->actors.size(); ++i)
                            if (recoveredPlacement(i) == effect.caster) casterIndex = i;
                        if (!casterIndex) launchLifeKnown = false;
                        else
                        {
                            const auto& life = recoveredLife(casterIndex);
                            launchLifeKnown = effect.casterLife == life.generation
                                ? effect.startTick >= life.bornTick
                                : effect.casterLife < life.generation
                                    && effect.startTick <= life.deaths.at(size_t(effect.casterLife - 1)).tick;
                            if (effect.casterLife > 1 && effect.casterLife - 2 < life.deaths.size())
                                launchLifeKnown &= effect.startTick > life.deaths.at(size_t(effect.casterLife - 2)).tick;
                        }
                    }
                    if (mBinding.mExpandedEffects)
                    {
                        uint64_t expected = 0;
                        if (absorbStat(id) || absorbDynamic(id) >= 0)
                        {
                            if (effect.casterKind == 2)
                                for (size_t i = 2; i < decoded.combat->actors.size(); ++i)
                                    if (effect.caster == recoveredPlacement(i)
                                        && effect.casterLife == recoveredLife(i).generation)
                                        expected = i + 1;
                            else if (effect.casterKind == 1)
                                for (size_t i = 0; i < mBinding.mPlayers.size(); ++i)
                                    if (mBinding.mPlayers[i].value() == effect.caster) expected = i + 1;
                        }
                        if (effect.beneficiary != expected)
                            throw std::invalid_argument("Native absorb beneficiary life invalid");
                    }
                    if (!casterKnown || !sourceKnown || !constantCaster || !launchLifeKnown
                        || (!(mBinding.mScriptedMovementRules ? decoded.combat->levitationEnabled
                                : mBinding.mLevitationEnabled) && id == ESM::MagicEffect::Levitate))
                        throw std::invalid_argument("Native saved actor effect source invalid");
                }
            EquipmentBytes retained(reinterpret_cast<const char*>(image.data()), reinterpret_cast<const char*>(image.data()+image.size()));
            recoverAreas(std::as_bytes(decoded.inventory), references, decoded.actor,
                [&](const EquipmentSessionValues& session) {
                if (mBinding.mPlayerAi)
                    for (size_t i = 0; i < 2; ++i)
                    {
                        const auto& social = decoded.combat->players[i];
                        ESM::RefId previous;
                        for (const auto& faction : social.factions)
                        {
                            if (!mRuntime.mStore.get<ESM::Faction>().search(faction.id)
                                || (!previous.empty() && !(previous < faction.id)))
                                throw std::invalid_argument("Saved player faction differs from content");
                            previous = faction.id;
                        }
                        if (!social.selectedSpell.empty()
                            && (!mRuntime.mStore.get<ESM::Spell>().search(social.selectedSpell)
                                || std::ranges::find(mRuntime.mStore.get<ESM::NPC>()
                                        .find(mBinding.mActors[i].mBase)->mSpells.mList,
                                    social.selectedSpell) == mRuntime.mStore.get<ESM::NPC>()
                                        .find(mBinding.mActors[i].mBase)->mSpells.mList.end()))
                            throw std::invalid_argument("Saved player selected spell differs from content");
                        if (social.selectedEnchantedItem && std::ranges::none_of(session.mActors[i].mObjects,
                            [&](const auto& item) {
                                if (item.mRef.mCount <= 0
                                    || wireId(item.mRef.mRefNum).value() != social.selectedEnchantedItem) return false;
                                const auto record = MWWorld::inventoryItemRecord(mRuntime.mStore, item.mRef.mRefID);
                                return !record.mEnchant.empty()
                                    && mRuntime.mStore.get<ESM::Enchantment>().search(record.mEnchant);
                            }))
                            throw std::invalid_argument("Saved player selected item differs from inventory");
                        if (mBinding.mPlayerTravel && social.mark
                            && !worldDomain(social.mark->cell()))
                            throw std::invalid_argument("Saved player mark outside bound world");
                        if (mBinding.mSocialLifecycle)
                        {
                            if (social.werewolf != social.normalSkills.has_value()
                                || social.werewolf != social.normalAttributes.has_value())
                                throw std::invalid_argument("Saved werewolf stat state invalid");
                            const auto& inventory = session.mActors[i];
                            if (social.werewolf)
                            {
                                const auto robe = inventory.mSlots[MWWorld::InventoryStore::Slot_Robe];
                                const auto item = std::ranges::find(inventory.mObjects, robe,
                                    [](const auto& object) { return object.mRef.mRefNum; });
                                if (!robe.isSet() || item == inventory.mObjects.end()
                                    || item->mRef.mRefID != ESM::RefId::stringRefId("werewolfrobe")
                                    || std::ranges::any_of(inventory.mSlots, [robe](ESM::RefNum slot) {
                                        return slot.isSet() && slot != robe;
                                    })) throw std::invalid_argument("Saved werewolf equipment invalid");
                            }
                            for (const auto& engagement : social.engagements)
                                if (std::ranges::none_of(mBinding.mCrimeWitnesses, [&](const auto& witness) {
                                    return witness.placement == engagement.witness;
                                })) throw std::invalid_argument("Saved crime witness differs from content");
                        }
                    }
                if (decoded.combat) for (const auto& arrow : decoded.combat->arrows)
                {
                    const auto* weapon = mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(arrow.weapon));
                    const auto* ammo = mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(arrow.ammoRecord));
                    const auto caster = arrow.casterKind == 2
                        ? std::ranges::find(decoded.combat->npcPlacements, arrow.caster)
                            - decoded.combat->npcPlacements.begin() + 2 : 0;
                    if ((arrow.casterKind == 1
                            ? std::ranges::none_of(mBinding.mPlayers, [&](PlayerId id) { return id.value() == arrow.caster; })
                            : caster >= decoded.combat->actors.size()
                                || arrow.casterLife > (caster == 2 ? decoded.life->generation
                                    : decoded.neighborLives.at(caster - 3).generation))
                        || (mBinding.mAuthoritativeAim && arrow.casterKind == 1
                            && !arrow.target && !arrow.targetLife ? false : arrow.targetKind == 1
                            ? std::ranges::none_of(mBinding.mPlayers, [&](PlayerId id) { return id.value() == arrow.target; })
                            : mBinding.mNeighborCombat
                            ? std::ranges::find(decoded.combat->npcPlacements, arrow.target)
                                == decoded.combat->npcPlacements.end()
                            : arrow.target != mBinding.mNavigatingActor->actorId())
                        || (arrow.casterKind == 2 && !arrow.terminal
                            && (arrow.casterLife != (caster == 2 ? decoded.life->generation
                                    : decoded.neighborLives.at(caster - 3).generation)
                                || (caster == 2 ? decoded.life->respawnTick
                                    : decoded.neighborLives.at(caster - 3).respawnTick)
                                || decoded.combat->actors[caster][8][2] <= 0))
                        || (arrow.source == arrow.ammunition) != (weapon->mData.mType == ESM::Weapon::MarksmanThrown)
                        || !rangedWeapon(weapon, mBinding.mRangedRelease)
                        || !rangedSources(*weapon, *ammo, mRuntime.mStore, mBinding))
                        throw std::invalid_argument("Saved bow projectile source invalid");
                    if (mBinding.mRangedFlight)
                    {
                        const float speed = MWMechanics::projectileLaunchSpeed(mRuntime.mStore,
                            weapon->mData.mType == ESM::Weapon::MarksmanThrown, arrow.strength);
                        if (!std::isfinite(speed) || speed <= 0 || speed > 50000
                            || arrow.steps / 2 + arrow.steps % 2 > decoded.tick - arrow.releaseTick
                            || (arrow.terminal && !arrow.steps))
                            throw std::invalid_argument("Saved physical flight clock invalid");
                        osg::Vec3f velocity(arrow.direction[0] * speed, arrow.direction[1] * speed, arrow.direction[2] * speed);
                        for (uint64_t i = 0; i < arrow.steps; ++i)
                            velocity = MWMechanics::advanceProjectileVelocity(velocity, 1.f / 60);
                        for (size_t axis = 0; axis < 3; ++axis)
                            if (velocity[axis] != arrow.velocity[axis])
                                throw std::invalid_argument("Saved physical projectile velocity changed");
                    }
                }
                if (mBinding.mPlayerMelee[0])
                    for (size_t i = 0; i < decoded.combat->swings.size(); ++i)
                    {
                        if (!decoded.combat->swings[i]) continue;
                        const auto& swing = *decoded.combat->swings[i];
                        const auto* weapon = swing.weapon.empty() ? nullptr
                            : mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(swing.weapon));
                        const std::array<std::string_view, 3> directions{"chop", "slash", "thrust"};
                        auto clip = mBinding.mPlayerMelee[i](weapon, directions[swing.direction]);
                        if (clip.identity() != swing.identity
                            || (swing.state.mReleased && swing.state.mStrength != swing.strength))
                            throw std::invalid_argument("Saved player swing clip differs from bound source");
                        clip.restore(swing.state);
                        if (mBinding.mBowRelease)
                        {
                            const bool bow = rangedWeapon(weapon, mBinding.mRangedRelease);
                            if (bow != bool(swing.ammunition))
                                throw std::invalid_argument("Saved bow ammunition binding missing");
                            if (bow)
                            {
                                const auto* ammo = mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(swing.ammoRecord));
                                if (!rangedSources(*weapon, *ammo, mRuntime.mStore, mBinding)
                                    || (swing.source == swing.ammunition) != (weapon->mData.mType == ESM::Weapon::MarksmanThrown))
                                    throw std::invalid_argument("Saved bow source is unsupported");
                            }
                            const auto arrow = std::ranges::find_if(decoded.combat->arrows, [&](const auto& value) {
                                return value.caster == mBinding.mPlayers[i].value() && value.command == swing.command;
                            });
                            if ((bow && swing.state.mHit) != (arrow != decoded.combat->arrows.end())
                                || (arrow != decoded.combat->arrows.end()
                                    && (arrow->source != swing.source || arrow->ammunition != swing.ammunition
                                        || arrow->weapon != swing.weapon || arrow->ammoRecord != swing.ammoRecord
                                        || arrow->targetLife != swing.targetLife || arrow->strength != swing.strength)))
                                throw std::invalid_argument("Saved bow release and projectile disagree");
                        }
                        if (!swing.pending()) continue;
                        if (!swing.state.mHit)
                        {
                            const auto& values = session.mActors[i];
                            if (swing.ammunition)
                            {
                                const auto* ammo = equippedAmmunition(values, mRuntime.mStore, *weapon,
                                    mBinding);
                                if (!ammo || wireId(ammo->mRef.mRefNum).value() != swing.ammunition
                                    || ammo->mRef.mRefID != ESM::RefId::stringRefId(swing.ammoRecord))
                                    throw std::invalid_argument("Saved bow ammunition changed before release");
                            }
                            const auto slot = values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
                            if (swing.source != (slot.isSet() ? wireId(slot).value() : 0))
                                throw std::invalid_argument("Saved player swing source is no longer equipped");
                            if (swing.source && std::ranges::none_of(values.mObjects, [&](const auto& item) {
                                    return item.mRef.mRefNum == slot && item.mRef.mCount > 0
                                        && item.mRef.mRefID == weapon->mId;
                                })) throw std::invalid_argument("Saved player swing weapon identity changed");
                            const bool worldShot = mBinding.mAuthoritativeAim && swing.ammunition && !swing.target;
                            const auto victim = worldShot ? 0 : mBinding.mNeighborCombat
                                ? std::ranges::find(decoded.combat->npcPlacements, swing.target)
                                    - decoded.combat->npcPlacements.begin() + 2 : 2;
                            if (!worldShot && (victim >= decoded.combat->actors.size()
                                || swing.targetLife != (victim == 2 ? decoded.life->generation
                                    : decoded.neighborLives[victim - 3].generation)
                                || decoded.combat->actors[victim][8][2] <= 0))
                                throw std::invalid_argument("Saved player swing target life invalid");
                        }
                        if (decoded.combat->actors[i][8][2] <= 0 || decoded.combat->knockedDown[i]
                            || hasParalysis(decoded.timedEffects, i)
                            || decoded.combat->hitRecoveryTicks[i])
                            throw std::invalid_argument("Saved player swing is incapacitated");
                    }
                if (mBinding.mWeaponMelee)
                {
                    const auto& values = session.mContainers.at(mCombatNpcOwner - 2);
                    restoredMelee = mIdleMelee;
                    if (!decoded.melee->target && (decoded.melee->identity != restoredMelee->identity()
                            || decoded.melee->state != restoredMelee->snapshot()))
                        throw std::invalid_argument("Saved idle weapon swing is not at rest");
                    if (decoded.melee->target)
                    {
                        const auto match = [&](const ESM::Weapon* weapon) {
                            if (rangedWeapon(weapon, mBinding.mRangedRelease))
                            {
                                auto animation = mBinding.mWeaponMelee(weapon, "shoot");
                                if (animation.identity() != decoded.melee->identity) return false;
                                restoredMelee = std::move(animation); return true;
                            }
                            const std::array<std::string_view, 3> modes{"chop", "slash", "thrust"};
                            for (size_t i = 0; i < (mBinding.mGeneralAttackModes ? modes.size() : 1); ++i)
                            {
                                auto animation = mBinding.mWeaponMelee(weapon,
                                    mBinding.mGeneralAttackModes ? modes[i] : std::string_view{});
                                if (animation.identity() != decoded.melee->identity) continue;
                                restoredMelee = std::move(animation); return true;
                            }
                            return false;
                        };
                        bool found = (decoded.melee->state.mHit
                            || !values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight].isSet()) && match(nullptr);
                        for (const auto& item : values.mObjects)
                        {
                            if (found) break;
                            if (item.mRef.mCount <= 0 || mRuntime.mStore.find(item.mRef.mRefID) != ESM::Weapon::sRecordId
                                || (!decoded.melee->state.mHit && item.mRef.mRefNum != values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight])) continue;
                            const auto* weapon = mRuntime.mStore.get<ESM::Weapon>().find(item.mRef.mRefID);
                            if (MWMechanics::getWeaponType(weapon->mData.mType)->mWeaponClass != ESM::WeaponType::Melee
                                && !rangedWeapon(weapon, mBinding.mRangedRelease)) continue;
                            found = match(weapon);
                        }
                        if (!found) throw std::invalid_argument("Saved native swing has no bound weapon source");
                    }
                    restoredMelee->restore(decoded.melee->state);
                }
                if (mBinding.mConstantEffects)
                {
                    for (size_t actorIndex = 0; actorIndex < 3; ++actorIndex)
                    {
                        const auto& values = actorIndex == 2 ? session.mContainers.at(mCombatNpcOwner - 2) : session.mActors[actorIndex];
                        reconcileConstants(values, actorIndex, {actorIndex == 2
                            ? mBinding.mNavigatingActor->actorId() : mBinding.mPlayers[actorIndex].value(),
                            mBinding.mDurableCasters ? (actorIndex == 2 ? 2u : 1u) : 0u,
                            mBinding.mDurableCasters ? (actorIndex == 2 ? decoded.life->generation : 1u) : 0u},
                            decoded.tick, mRuntime.mStore, mBinding.mGeneralConstants, decoded.timedEffects, nullptr,
                            mBinding.mExpandedEffects, mBinding.mSpecialConditions, mBinding.mMovementEffects,
                            mBinding.mAiDecisions, actorIndex == 2 && aiNpc, actorIndex == 2 && aiUndead,
                            mBinding.mScriptedMovementRules ? decoded.combat->levitationEnabled
                                : mBinding.mLevitationEnabled);
                    }
                }
                if (mBinding.mAiDecisions || mBinding.mMovementEffects)
                    for (size_t actorIndex = 0; actorIndex < 3; ++actorIndex)
                        reconcilePassiveActorEffects(mRuntime.ownerPtr(actorIndex == 2 ? mCombatNpcOwner : actorIndex),
                            actorIndex, {actorIndex == 2 ? mBinding.mNavigatingActor->actorId()
                                    : mBinding.mPlayers[actorIndex].value(), actorIndex == 2 ? 2u : 1u,
                                actorIndex == 2 ? decoded.life->generation : 1u}, decoded.tick,
                            mRuntime.mStore, decoded.timedEffects, nullptr,
                            mBinding.mMovementEffects, mBinding.mAiDecisions,
                            actorIndex == 2 && aiNpc, actorIndex == 2 && aiUndead,
                            mBinding.mScriptedMovementRules ? decoded.combat->levitationEnabled
                                : mBinding.mLevitationEnabled);
                if (mBinding.mPlayerCastLifecycle)
                    for (size_t i = 0; i < 2; ++i)
                    {
                        const auto& pending = decoded.combat->playerCasts[i];
                        if (!pending) continue;
                        const auto& cast = *pending;
                        std::optional<PreparedInstantEffects> effects;
                        if (cast.sourceKind == uint64_t(MagicUseSourceKind::Spell))
                        {
                            for (const auto& id : mRuntime.mStore.get<ESM::NPC>().find(mBinding.mActors[i].mBase)->mSpells.mList)
                                if (spellRecordId(id) == cast.source)
                                {
                                    if (effects) throw std::invalid_argument("Saved player spell identity ambiguous");
                                    const auto* spell = mRuntime.mStore.get<ESM::Spell>().search(id);
                                    const auto plan = spell ? prepareInstantSpell(*spell, mRuntime.mStore, true,
                                        mBinding.mExpandedEffects, mBinding.mSpecialConditions,
                                        mBinding.mMovementEffects,
                                        cast.targetKind == uint64_t(MagicUseTargetKind::Door)
                                            || cast.targetKind == uint64_t(MagicUseTargetKind::Container),
                                        mBinding.mPlayerTravel) : std::nullopt;
                                    if (plan) effects = plan->effects;
                                }
                        }
                        else if (cast.phase < ActorCampaignCast::Released)
                        {
                            for (const auto& item : session.mActors[i].mObjects)
                                if (item.mRef.mCount > 0 && wireId(item.mRef.mRefNum).value() == cast.source)
                                {
                                    MWWorld::ManualRef reference(mRuntime.mStore, item.mRef.mRefID);
                                    const auto ptr = reference.getPtr();
                                    const auto* enchantment = mRuntime.mStore.get<ESM::Enchantment>().search(ptr.getClass().getEnchantment(ptr));
                                    if (enchantment && (enchantment->mData.mType == ESM::Enchantment::WhenUsed
                                            || (enchantment->mData.mType == ESM::Enchantment::CastOnce && ptr.getClass().getScript(ptr).empty())))
                                        effects = prepareInstantEffects(enchantment->mEffects, mRuntime.mStore, true,
                                            mBinding.mExpandedEffects, false, mBinding.mSpecialConditions,
                                            mBinding.mMovementEffects,
                                            cast.targetKind == uint64_t(MagicUseTargetKind::Door)
                                                || cast.targetKind == uint64_t(MagicUseTargetKind::Container),
                                            mBinding.mPlayerTravel);
                                }
                        }
                        else continue; // Released items may have been consumed or transferred.
                        if (!effects || uint64_t(effects->effects.front().mRange) != cast.range
                            || ((cast.targetKind == uint64_t(MagicUseTargetKind::Self)) != effects->onlyRange(ESM::RT_Self)))
                            throw std::invalid_argument("Saved player cast source/range invalid");
                        if ((cast.targetKind == uint64_t(MagicUseTargetKind::Door)
                                || cast.targetKind == uint64_t(MagicUseTargetKind::Container))
                            && ((cast.targetKind == uint64_t(MagicUseTargetKind::Door)
                                    ? std::ranges::none_of(mBinding.mDoors, [&](const auto& door) {
                                        return door.mId == cast.target;
                                    })
                                    : std::ranges::none_of(mBinding.mContainers, [&](const auto& container) {
                                        return container.mId.value() == cast.target && container.mPlacement
                                            && mRuntime.mStore.get<ESM::Container>().search(container.mBase);
                                    })) || effects->effects.size() != 1
                                || (effects->effects.front().mEffectID != ESM::MagicEffect::Lock
                                    && effects->effects.front().mEffectID != ESM::MagicEffect::Open)
                                || effects->effects.front().mRange != ESM::RT_Touch
                                || effects->effects.front().mArea || effects->effects.front().mDuration))
                            throw std::invalid_argument("Saved door cast source/target invalid");
                    }
                if (!decoded.casting) return;
                const auto& cast = *decoded.casting;
                if (decoded.life->respawnTick || decoded.combat->actors[2][8][2] <= 0
                    || hasParalysis(decoded.timedEffects, 2)
                    || decoded.combat->knockedDown[2] || decoded.combat->hitRecoveryTicks[2]
                    || (cast.targetKind == uint64_t(MagicUseTargetKind::Player)
                        && std::ranges::none_of(mBinding.mPlayers, [&](auto id) { return id.value() == cast.target; })))
                    throw std::invalid_argument("Native saved cast life/target invalid");
                std::optional<PreparedInstantEffects> effects;
                if (cast.sourceKind == uint64_t(MagicUseSourceKind::Spell))
                {
                    const auto& known = actorSpells(mRuntime.ownerPtr(mCombatNpcOwner)).mList;
                    for (const auto& id : known) if (spellRecordId(id) == cast.source)
                    {
                        if (effects) throw std::invalid_argument("Native saved cast source ambiguous");
                        const auto* source = mRuntime.mStore.get<ESM::Spell>().search(id);
                        const auto plan = source ? prepareInstantSpell(*source, mRuntime.mStore, true,
                            mBinding.mExpandedEffects, mBinding.mSpecialConditions,
                            mBinding.mMovementEffects) : std::nullopt;
                        if (plan) effects = plan->effects;
                    }
                }
                else
                {
                    const auto& values = session.mContainers.at(mCombatNpcOwner - 2);
                    for (const auto& item : values.mObjects)
                        if (item.mRef.mCount > 0 && wireId(item.mRef.mRefNum).value() == cast.source
                            && std::ranges::find(values.mSlots, item.mRef.mRefNum) != values.mSlots.end())
                        {
                            MWWorld::ManualRef reference(mRuntime.mStore, item.mRef.mRefID);
                            const auto ptr = reference.getPtr();
                            const auto* enchantment = mRuntime.mStore.get<ESM::Enchantment>().search(ptr.getClass().getEnchantment(ptr));
                            if (enchantment && enchantment->mData.mType == ESM::Enchantment::WhenUsed)
                                effects = prepareInstantEffects(enchantment->mEffects, mRuntime.mStore, true,
                                    mBinding.mExpandedEffects, false, mBinding.mSpecialConditions,
                                    mBinding.mMovementEffects);
                        }
                }
                if (!effects || uint64_t(effects->effects.front().mRange) != cast.range
                    || ((cast.targetKind == uint64_t(MagicUseTargetKind::Self)) != effects->onlyRange(ESM::RT_Self)))
                    throw std::invalid_argument("Native saved cast source/range invalid");
            });
            mMelee = std::move(restoredMelee);
            mMeleeTarget = decoded.melee ? decoded.melee->target : 0;
            mMeleeContacted = decoded.melee && decoded.melee->contact;
            mCombat = decoded.combat;
            mLife = decoded.life;
            mNeighborLives = std::move(decoded.neighborLives);
            mProjectiles = decoded.projectiles;
            mTimedEffects = std::move(decoded.timedEffects);
            mNpcCast = decoded.casting;
            mRespawnInventory.swap(baseline);
            mNeighborRespawnInventory.swap(neighborBaselines);
            mActorTick = decoded.tick; mActorVelocity = decoded.velocity; mActorImage.swap(retained);
            installActorPosition();
            return;
        }
        if (mBinding.mStreamExteriors) { recoverAreas(image, references); return; }
        EquipmentBytes accepted(reinterpret_cast<const char*>(image.data()), reinterpret_cast<const char*>(image.data()+image.size()));
        std::unique_ptr<const EquipmentSessionValues> values;
        EquipmentBytes output;
        mRuntime.restoreSession(std::move(accepted), references, values, output);
        mCoreImage = output;
        mImage.swap(output);
    }

    EquipmentBytes InventoryService::sealActor(std::span<const char> core, std::span<const char> actor,
        uint64_t tick, const std::array<float, 3>& velocity, const std::optional<MeleeAnimation>& melee,
        uint64_t target, bool contact, const std::optional<ActorCampaignCombat>& combat,
        const std::optional<ActorCampaignLife>& life,
        std::span<const ActorCampaignLife> neighborLives,
        std::span<const ActorCampaignProjectile> projectiles,
        std::span<const ActorCampaignTimedEffect> timedEffects,
        const std::optional<ActorCampaignCast>& casting) const
    {
        const size_t meleeSize = melee ? 8 + (mBinding.mWeaponMelee ? melee->identity() : mBinding.mBoundMelee->mResourceIdentity).size()
            + (mBinding.mMeleeContact ? 7 : 5) * 8 : 0;
        const size_t actorSlots = combat ? combat->actors.size() : 0;
        if (combat && ((mBinding.mPlacementCombat
                ? (actorSlots < 4 || actorSlots > 10 || combat->npcPlacements.size() != actorSlots - 2
                    || combat->npcPlacements.front() != mBinding.mNavigatingActor->actorId())
                : actorSlots != 3 || !combat->npcPlacements.empty())
            || combat->knockedDown.size() != actorSlots || combat->knockoutFrame.size() != actorSlots
            || combat->hitKnockdown.size() != actorSlots || combat->hitRecoveryTicks.size() != actorSlots
            || combat->bodyAction.size() != actorSlots || combat->hitGroup.size() != actorSlots
            || (mBinding.mNeighborCombat && combat->neighborAttacks.size() != actorSlots - 3)))
            throw std::invalid_argument("Native combat placement domain invalid");
        const size_t combatSize = combat ? 8 + (mBinding.mScriptedMovementRules ? 8 : 0)
            + (mBinding.mScriptedTravelRules ? 8 : 0)
            + (mBinding.mPlacementCombat ? (1 + combat->npcPlacements.size()) * 8 : 0)
            + actorSlots * ActorCampaignCombat::StatCount * 5 * 8
            + (mBinding.mKnockoutRules ? actorSlots * 8 : 0)
            + (mBinding.mKnockoutAnimation ? actorSlots * 2 * 8 : 0)
            + (mBinding.mMeleeDefenseRules ? actorSlots * 8 : 0)
            + (mBinding.mActorPresentation ? (1 + actorSlots * 2) * 8 : 0)
            + (mBinding.mAiDecisions ? 5 * 8 : 0) : 0;
        size_t playerAiSize = 0;
        if (mBinding.mPlayerAi && combat)
            for (const auto& player : combat->players)
            {
                if (player.factions.size() > 256 || player.bounty < 0 || player.drawState > 2)
                    throw std::invalid_argument("Native player AI state exceeds bounds");
                playerAiSize += 8 + 7 * 8 + player.selectedSpell.serializeText().size();
                if (mBinding.mPlayerTravel) playerAiSize += player.mark ? 11 * 8 : 8;
                if (mBinding.mSocialLifecycle && player.werewolf)
                    playerAiSize += (ESM::Skill::Length + ESM::Attribute::Length) * 8;
                if (mBinding.mSocialLifecycle)
                    playerAiSize += 8 + player.engagements.size() * 24;
                for (const auto& faction : player.factions)
                    playerAiSize += 24 + faction.id.serializeText().size();
            }
        size_t lifeSize = life ? (6 + ActorCampaignCombat::StatCount * 5 + (mBinding.mDurableCasters ? 5 : 3) * life->deaths.size()) * 8
            + life->spawnActor.size() + life->spawnInventory.size() : 0;
        size_t neighborAttackSize = 0;
        if (mBinding.mNeighborCombat)
        {
            if (neighborLives.size() != combat->npcPlacements.size() - 1)
                throw std::invalid_argument("Native neighbor life domain invalid");
            lifeSize += 8;
            for (const auto& adjacent : neighborLives)
                lifeSize += (7 + ActorCampaignCombat::StatCount * 5 + 5 * adjacent.deaths.size()) * 8
                    + adjacent.spawnActor.size() + adjacent.spawnInventory.size();
            for (const auto& attack : combat->neighborAttacks)
            {
                if (attack.identity.empty() || attack.identity.size() > 512)
                    throw std::invalid_argument("Native neighbor attack resource invalid");
                neighborAttackSize += 12 * 8 + attack.identity.size() + attack.weapon.size();
            }
        }
        if (mBinding.mNeighborCombat && (!mBinding.mPlacementCombat
            || !mBinding.mNeighborWeaponMelee || !mBinding.mNavigatingActor))
            throw std::invalid_argument("Neighbor combat requires placement, animation and scene bindings");
        const size_t projectileSize = mBinding.mMagicProjectile
            ? 8 + projectiles.size() * ((mBinding.mMagicItemUse ? 13 : 11) * 8
                + (mBinding.mMagicPlayerTarget ? 8 : 0)
                + (mBinding.mMagicProjectileCollection ? 8 : 0)
                + (mBinding.mDurableCasters ? 16 : 0)) : 0;
        size_t castSize = mBinding.mNpcCastLifecycle ? 16 + mBinding.mBoundCasts->resourceIdentity.size() + (casting ? 80 : 0) : 0;
        if (mBinding.mPlayerCastLifecycle)
            for (size_t i = 0; i < 2; ++i)
                castSize += 16 + mBinding.mPlayerCasts[i].resourceIdentity.size() + (combat->playerCasts[i] ? 88 : 0);
        if (mBinding.mPersistentConditions)
        {
            if (combat->conditions.size() > ActorCampaignCombat::MaximumConditionSources)
                throw std::invalid_argument("Native condition source capacity exhausted");
            castSize += 8 + combat->conditions.size() * (mBinding.mSpecialConditions ? 40 : 16);
        }
        const size_t timedSize = mBinding.mMagicTimed
            ? 8 + timedEffects.size() * (mBinding.mExpandedEffects ? 120 : mBinding.mDurableCasters ? 112 : mBinding.mGeneralConstants ? 96 : mBinding.mActorEffectLifecycle ? 80 : 24) : 0;
        size_t swingSize = 0;
        if (mBinding.mPlayerMelee[0])
            for (const auto& swing : combat->swings)
                swingSize += 8 + (swing ? (13 + size_t(mBinding.mNeighborCombat)
                    + (mBinding.mAuthoritativeAim ? 3 : 0)) * 8 + swing->weapon.size() + swing->identity.size()
                    + (mBinding.mBowRelease ? 16 + swing->ammoRecord.size() : 0) : 0);
        if (mBinding.mBowRelease)
        {
            if (combat->arrows.size() > MaximumActorProjectiles)
                throw std::invalid_argument("Native arrow capacity exceeded");
            swingSize += 8;
            for (const auto& arrow : combat->arrows)
                swingSize += (mBinding.mRangedFlight ? 22 + (mBinding.mNeighborCombat ? 3 : 0) : 16) * 8
                    + arrow.weapon.size() + arrow.ammoRecord.size();
        }
        if (core.empty() || actor.empty() || actor.size() > 65536
            || timedEffects.size() > (mBinding.mGeneralConstants ? MaximumActorTimedEffects : 16)
            || projectiles.size() > (mBinding.mMagicProjectileCollection ? MaximumActorProjectiles : 1)
            || 56 + meleeSize + combatSize + playerAiSize + lifeSize + neighborAttackSize + projectileSize + timedSize + castSize + swingSize + actor.size() > MaximumNativeInventoryImageBytes
            || core.size() > MaximumNativeInventoryImageBytes - 56 - meleeSize - combatSize - playerAiSize - lifeSize - neighborAttackSize - projectileSize - timedSize - castSize - swingSize - actor.size())
            throw std::invalid_argument("Native actor campaign exceeds bound");
        EquipmentBytes result;
        putAreaWord(result, mBinding.mScriptedTravelRules ? TravelRuleCampaignMagic
            : mBinding.mPlayerTravel ? PlayerTravelCampaignMagic
            : mBinding.mScriptedMovementRules ? MovementRuleCampaignMagic
            : mBinding.mAuthoritativeAim ? AuthoritativeAimCampaignMagic
            : mBinding.mNeighborCombat ? NpcRangedCampaignMagic
            : mBinding.mPlacementCombat ? PlacementCombatCampaignMagic
            : mBinding.mSocialLifecycle ? SocialLifecycleCampaignMagic
            : mBinding.mPlayerAi ? PlayerAiCampaignMagic
            : mBinding.mAiDecisions ? AiDecisionCampaignMagic
            : mBinding.mMovementEffects ? MovementEffectsCampaignMagic
            : mBinding.mSpecialConditions ? SpecialConditionsCampaignMagic
            : mBinding.mPersistentConditions ? PersistentConditionsCampaignMagic
            : mBinding.mPlayerCastLifecycle ? PlayerCastCampaignMagic
            : mBinding.mActorPresentation ? ActorPresentationCampaignMagic
            : mBinding.mExpandedEffects ? ExpandedEffectsCampaignMagic
            : mBinding.mKnockoutAnimation ? KnockoutAnimationCampaignMagic
            : mBinding.mRangedFlight ? RangedFlightCampaignMagic
            : mBinding.mRangedRelease ? RangedReleaseCampaignMagic : mBinding.mBowRelease ? BowReleaseCampaignMagic
            : mBinding.mPlayerMelee[0] ? PlayerSwingCampaignMagic
            : mBinding.mWeaponMelee ? WeaponExecutionCampaignMagic
            : mBinding.mNpcCastLifecycle ? CastLifecycleCampaignMagic
            : mBinding.mDurableCasters ? CasterActorCampaignMagic
            : mBinding.mGeneralConstants ? GeneralConstantActorCampaignMagic
            : mBinding.mConstantEffects ? ConstantActorCampaignMagic
            : mBinding.mActorEffectLifecycle ? EffectActorCampaignMagic
            : mBinding.mMeleeDefenseRules ? MeleeDefenseActorCampaignMagic
            : mBinding.mKnockoutRules ? KnockoutActorCampaignMagic
            : mBinding.mMagicProjectileCollection ? MultipleProjectileActorCampaignMagic
            : mBinding.mMagicPlayerTarget ? PlayerTargetActorCampaignMagic
            : mBinding.mMagicArea ? AreaActorCampaignMagic
            : mBinding.mMagicTimed ? TimedActorCampaignMagic
            : mBinding.mMagicItemUse ? EnchantedProjectileActorCampaignMagic
            : mBinding.mMagicProjectile ? ProjectileActorCampaignMagic
            : life ? LifeActorCampaignMagic : combat ? CombatActorCampaignMagic
            : melee ? (mBinding.mMeleeContact ? ContactActorCampaignMagic : MeleeActorCampaignMagic)
            : ActorCampaignMagic);
        putAreaWord(result, core.size()); putAreaWord(result, actor.size()); putAreaWord(result, tick);
        for (float value : velocity) putAreaWord(result, std::bit_cast<uint32_t>(value));
        if (melee)
        {
            const auto& identity = mBinding.mWeaponMelee ? melee->identity() : mBinding.mBoundMelee->mResourceIdentity;
            const auto& state = melee->snapshot();
            putAreaWord(result, identity.size()); result.insert(result.end(), identity.begin(), identity.end());
            putAreaWord(result, uint64_t(state.mPhase));
            putAreaWord(result, std::bit_cast<uint32_t>(state.mTime));
            putAreaWord(result, std::bit_cast<uint32_t>(state.mStrength));
            putAreaWord(result, state.mReleased); putAreaWord(result, state.mHit);
            if (mBinding.mMeleeContact) { putAreaWord(result, target); putAreaWord(result, contact); }
        }
        if (combat)
        {
            putAreaWord(result, combat->rng);
            if (mBinding.mScriptedMovementRules) putAreaWord(result, combat->levitationEnabled);
            if (mBinding.mScriptedTravelRules) putAreaWord(result, combat->teleportingEnabled);
            if (mBinding.mPlacementCombat)
            {
                putAreaWord(result, combat->npcPlacements.size());
                for (auto placement : combat->npcPlacements) putAreaWord(result, placement);
            }
            for (const auto& actorStats : combat->actors)
                for (const auto& stat : actorStats)
                    for (float value : stat) putAreaWord(result, std::bit_cast<uint32_t>(value));
            if (mBinding.mKnockoutRules)
                for (bool value : combat->knockedDown) putAreaWord(result, value);
            if (mBinding.mMeleeDefenseRules)
                for (uint32_t value : combat->hitRecoveryTicks) putAreaWord(result, value);
            if (mBinding.mKnockoutAnimation)
                for (uint32_t value : combat->knockoutFrame) putAreaWord(result, value);
            if (mBinding.mKnockoutAnimation)
                for (bool value : combat->hitKnockdown) putAreaWord(result, value);
        }
        if (combat && mBinding.mActorPresentation)
        {
            putAreaWord(result, combat->npcAction);
            for (auto v : combat->bodyAction) putAreaWord(result, v);
            for (auto v : combat->hitGroup) putAreaWord(result, v);
            if (mBinding.mNeighborCombat)
                for (const auto& attack : combat->neighborAttacks)
                {
                    putAreaWord(result, attack.identity.size());
                    result.insert(result.end(), attack.identity.begin(), attack.identity.end());
                    putAreaWord(result, attack.weapon.size());
                    result.insert(result.end(), attack.weapon.begin(), attack.weapon.end());
                    putAreaWord(result, uint64_t(attack.state.mPhase));
                    putAreaWord(result, std::bit_cast<uint32_t>(attack.state.mTime));
                    putAreaWord(result, std::bit_cast<uint32_t>(attack.state.mStrength));
                    putAreaWord(result, attack.state.mReleased); putAreaWord(result, attack.state.mHit);
                    putAreaWord(result, attack.target); putAreaWord(result, attack.contact);
                    putAreaWord(result, attack.action);
                    putAreaWord(result, attack.source); putAreaWord(result, attack.direction);
                }
        }
        if (combat && mBinding.mAiDecisions)
        {
            putAreaWord(result, combat->fleeTarget); putAreaWord(result, combat->fleeUntil);
            for (float value : combat->fleeDestination)
                putAreaWord(result, std::bit_cast<uint32_t>(value));
        }
        if (combat && mBinding.mPlayerAi)
            for (const auto& player : combat->players)
            {
                putAreaWord(result, player.factions.size());
                for (const auto& faction : player.factions)
                {
                    const auto id = faction.id.serializeText();
                    if (id.empty() || id.size() > 256 || faction.rank < 0 || faction.rank > 9)
                        throw std::invalid_argument("Native player faction state invalid");
                    putAreaWord(result, id.size()); result.insert(result.end(), id.begin(), id.end());
                    putAreaWord(result, faction.rank); putAreaWord(result, faction.expelled);
                }
                const auto spell = player.selectedSpell.serializeText();
                if (spell.size() > 256)
                    throw std::invalid_argument("Native player spell identity exceeds bound");
                putAreaWord(result, player.bounty);
                putAreaWord(result, std::bit_cast<uint32_t>(int32_t(player.crimeDisposition)));
                putAreaWord(result, player.drawState);
                putAreaWord(result, player.werewolf); putAreaWord(result, player.knownWerewolf);
                putAreaWord(result, spell.size()); result.insert(result.end(), spell.begin(), spell.end());
                putAreaWord(result, player.selectedEnchantedItem);
                if (mBinding.mPlayerTravel)
                {
                    putAreaWord(result, bool(player.mark));
                    if (player.mark)
                    {
                        const auto& value = *player.mark;
                        putAreaWord(result, uint64_t(value.cell().kind()));
                        putAreaWord(result, value.cell().asInterior()
                            ? value.cell().asInterior()->cellSpace().value()
                            : value.cell().asExterior()->worldspace().value());
                        putAreaWord(result, value.cell().asExterior()
                            ? uint32_t(value.cell().asExterior()->gridX()) : 0);
                        putAreaWord(result, value.cell().asExterior()
                            ? uint32_t(value.cell().asExterior()->gridY()) : 0);
                        for (const auto axis : {value.position().x(), value.position().y(), value.position().z()})
                            putAreaWord(result, std::bit_cast<uint64_t>(axis));
                        for (const auto axis : {value.orientation().x(), value.orientation().y(), value.orientation().z()})
                            putAreaWord(result, axis.value());
                    }
                }
                if (mBinding.mSocialLifecycle && player.werewolf)
                {
                    if (!player.normalSkills || !player.normalAttributes)
                        throw std::invalid_argument("Native werewolf missing saved normal stats");
                    for (float value : *player.normalSkills) putAreaWord(result, std::bit_cast<uint32_t>(value));
                    for (float value : *player.normalAttributes) putAreaWord(result, std::bit_cast<uint32_t>(value));
                }
                if (mBinding.mSocialLifecycle)
                {
                    if (player.engagements.size() > 128)
                        throw std::invalid_argument("Native crime engagement capacity exceeded");
                    putAreaWord(result, player.engagements.size());
                    for (const auto& entry : player.engagements)
                    { putAreaWord(result, entry.witness); putAreaWord(result, entry.tick); putAreaWord(result, entry.fight); }
                }
            }
        if (life)
        {
            putAreaWord(result, life->generation); putAreaWord(result, life->bornTick);
            putAreaWord(result, life->respawnTick);
            for (const auto& stat : life->spawnStats)
                for (float value : stat) putAreaWord(result, std::bit_cast<uint32_t>(value));
            putAreaWord(result, life->spawnActor.size()); putAreaWord(result, life->spawnInventory.size());
            result.insert(result.end(), life->spawnActor.begin(), life->spawnActor.end());
            result.insert(result.end(), life->spawnInventory.begin(), life->spawnInventory.end());
            putAreaWord(result, life->deaths.size());
            for (const auto& death : life->deaths)
            {
                putAreaWord(result, death.life); putAreaWord(result, death.tick); putAreaWord(result, death.killer);
                if (mBinding.mDurableCasters)
                { putAreaWord(result, death.killerKind); putAreaWord(result, death.killerLife); }
            }
        }
        if (mBinding.mNeighborCombat)
        {
            putAreaWord(result, neighborLives.size());
            for (size_t i = 0; i < neighborLives.size(); ++i)
            {
                const auto& adjacent = neighborLives[i];
                putAreaWord(result, combat->npcPlacements[i + 1]);
                putAreaWord(result, adjacent.generation); putAreaWord(result, adjacent.bornTick);
                putAreaWord(result, adjacent.respawnTick);
                for (const auto& stat : adjacent.spawnStats)
                    for (float value : stat) putAreaWord(result, std::bit_cast<uint32_t>(value));
                putAreaWord(result, adjacent.spawnActor.size());
                putAreaWord(result, adjacent.spawnInventory.size());
                result.insert(result.end(), adjacent.spawnActor.begin(), adjacent.spawnActor.end());
                result.insert(result.end(), adjacent.spawnInventory.begin(), adjacent.spawnInventory.end());
                putAreaWord(result, adjacent.deaths.size());
                for (const auto& death : adjacent.deaths)
                {
                    putAreaWord(result, death.life); putAreaWord(result, death.tick);
                    putAreaWord(result, death.killer); putAreaWord(result, death.killerKind);
                    putAreaWord(result, death.killerLife);
                }
            }
        }
        if (mBinding.mMagicProjectile)
        {
            putAreaWord(result, projectiles.size());
            for (const auto& projectile : projectiles)
            {
                putAreaWord(result, projectile.caster); putAreaWord(result, projectile.source);
                putAreaWord(result, projectile.target); putAreaWord(result, projectile.generation);
                putAreaWord(result, projectile.expiresTick);
                if (mBinding.mMagicItemUse)
                {
                    putAreaWord(result, projectile.sourceKind);
                    putAreaWord(result, projectile.effectSource);
                }
                if (mBinding.mMagicPlayerTarget) putAreaWord(result, projectile.targetKind);
                if (mBinding.mMagicProjectileCollection) putAreaWord(result, projectile.commandId);
                for (float value : projectile.position) putAreaWord(result, std::bit_cast<uint32_t>(value));
                for (float value : projectile.step) putAreaWord(result, std::bit_cast<uint32_t>(value));
                if (mBinding.mDurableCasters)
                { putAreaWord(result, projectile.casterKind); putAreaWord(result, projectile.casterLife); }
            }
        }
        if (mBinding.mMagicTimed)
        {
            putAreaWord(result, timedEffects.size());
            for (const auto& effect : timedEffects)
            {
                putAreaWord(result, effect.actor);
                putAreaWord(result, std::bit_cast<uint32_t>(effect.magnitude));
                putAreaWord(result, effect.expiresTick);
                if (mBinding.mActorEffectLifecycle)
                {
                    putAreaWord(result, effect.effectIndex);
                    putAreaWord(result, effect.caster);
                    putAreaWord(result, effect.source);
                    putAreaWord(result, effect.sourceKind);
                    putAreaWord(result, std::bit_cast<uint32_t>(effect.resistance));
                    putAreaWord(result, effect.startTick);
                    putAreaWord(result, effect.durationTicks);
                    if (mBinding.mGeneralConstants)
                    { putAreaWord(result, effect.argument); putAreaWord(result, effect.ordinal); }
                    if (mBinding.mDurableCasters)
                    { putAreaWord(result, effect.casterKind); putAreaWord(result, effect.casterLife); }
                    if (mBinding.mExpandedEffects) putAreaWord(result, effect.beneficiary);
                }
            }
        }
        if (mBinding.mNpcCastLifecycle)
        {
            const auto& identity = mBinding.mBoundCasts->resourceIdentity;
            putAreaWord(result, identity.size()); result.insert(result.end(), identity.begin(), identity.end());
            putAreaWord(result, casting ? 1 : 0);
            if (casting) for (const auto value : {casting->actor, casting->life, casting->cast, casting->sourceKind,
                    casting->source, casting->targetKind, casting->target, casting->range, casting->elapsed, casting->phase})
                putAreaWord(result, value);
        }
        if (mBinding.mPlayerCastLifecycle)
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& identity = mBinding.mPlayerCasts[i].resourceIdentity;
                putAreaWord(result, identity.size()); result.insert(result.end(), identity.begin(), identity.end());
                const auto& cast = combat->playerCasts[i];
                putAreaWord(result, bool(cast));
                if (cast) for (const auto value : {cast->actor, cast->life, cast->cast, cast->sourceKind, cast->source,
                        cast->targetKind, cast->target, cast->range, cast->elapsed, cast->phase, cast->targetLife})
                    putAreaWord(result, value);
            }
        if (mBinding.mPersistentConditions)
        {
            putAreaWord(result, combat->conditions.size());
            for (const auto& condition : combat->conditions)
            {
                putAreaWord(result, condition.actor); putAreaWord(result, condition.source);
                if (mBinding.mSpecialConditions)
                {
                    putAreaWord(result, condition.nextWorseningMs);
                    putAreaWord(result, condition.lastObservedMs);
                    putAreaWord(result, condition.worsenings);
                }
            }
        }
        if (mBinding.mPlayerMelee[0])
            for (const auto& swing : combat->swings)
            {
                putAreaWord(result, bool(swing));
                if (!swing) continue;
                for (const auto value : {swing->command, swing->source, swing->targetLife, swing->direction, swing->interruption})
                    putAreaWord(result, value);
                if (mBinding.mNeighborCombat) putAreaWord(result, swing->target);
                putAreaWord(result, std::bit_cast<uint32_t>(swing->strength));
                for (const auto* value : {&swing->weapon, &swing->identity})
                { putAreaWord(result, value->size()); result.insert(result.end(), value->begin(), value->end()); }
                putAreaWord(result, uint64_t(swing->state.mPhase));
                putAreaWord(result, std::bit_cast<uint32_t>(swing->state.mTime));
                putAreaWord(result, std::bit_cast<uint32_t>(swing->state.mStrength));
                putAreaWord(result, swing->state.mReleased); putAreaWord(result, swing->state.mHit);
                if (mBinding.mBowRelease)
                {
                    putAreaWord(result, swing->ammunition); putAreaWord(result, swing->ammoRecord.size());
                    result.insert(result.end(), swing->ammoRecord.begin(), swing->ammoRecord.end());
                }
                if (mBinding.mAuthoritativeAim)
                    for (float axis : swing->aim) putAreaWord(result, std::bit_cast<uint32_t>(axis));
            }
        if (mBinding.mBowRelease)
        {
            putAreaWord(result, combat->arrows.size());
            for (const auto& arrow : combat->arrows)
            {
                for (const auto v : {arrow.caster, arrow.command, arrow.source, arrow.ammunition,
                        arrow.target, arrow.targetLife, arrow.releaseTick}) putAreaWord(result, v);
                putAreaWord(result, std::bit_cast<uint32_t>(arrow.strength));
                for (const auto* text : {&arrow.weapon, &arrow.ammoRecord})
                { putAreaWord(result, text->size()); result.insert(result.end(), text->begin(), text->end()); }
                for (float v : arrow.position) putAreaWord(result, std::bit_cast<uint32_t>(v));
                for (float v : arrow.direction) putAreaWord(result, std::bit_cast<uint32_t>(v));
                if (mBinding.mRangedFlight)
                {
                    for (float v : arrow.velocity) putAreaWord(result, std::bit_cast<uint32_t>(v));
                    putAreaWord(result, std::bit_cast<uint32_t>(arrow.condition));
                    putAreaWord(result, arrow.steps);
                    putAreaWord(result, arrow.terminal);
                    if (mBinding.mNeighborCombat)
                    {
                        putAreaWord(result, arrow.casterKind);
                        putAreaWord(result, arrow.casterLife);
                        putAreaWord(result, arrow.targetKind);
                    }
                }
            }
        }
        result.insert(result.end(), core.begin(), core.end()); result.insert(result.end(), actor.begin(), actor.end());
        (void)readActorCampaign(result);
        return result;
    }

    void InventoryService::installActorPosition() noexcept
    {
        // Both simulated identities are existing engine inventory owners.
        const auto install = [&](const ActorSceneSnapshot& state) {
            for (size_t i = 0; i < mBinding.mContainers.size(); ++i)
                if (mBinding.mContainers[i].mId.value() == state.mActor)
                {
                    auto position = mRuntime.ownerPtr(i + 2).getRefData().getPosition();
                    std::copy_n(state.mPosition.begin(), 3, position.pos);
                    position.rot[2] = state.mYaw;
                    mRuntime.ownerPtr(i + 2).getRefData().setPosition(position);
                    return;
                }
            std::terminate();
        };
        install(mBinding.mNavigatingActor->snapshot());
        for (const auto& neighbor : mBinding.mNavigatingActor->neighborSnapshots()) install(neighbor);
    }

    CellId InventoryService::actorCell(const ActorSceneSnapshot& state) const
    {
        const auto owner = std::ranges::find_if(mBinding.mContainers,
            [&](const auto& value) { return value.mId.value() == state.mActor; });
        if (const auto* exterior = owner->mCell.asExterior(); mBinding.mTravelerNeighborhood && exterior)
        {
            const auto x = std::floor(double(state.mPosition[0]) / 8192);
            const auto y = std::floor(double(state.mPosition[1]) / 8192);
            if (x < -32768 || x > 32767 || y < -32768 || y > 32767)
                throw std::invalid_argument("Traveler position outside cell coordinate bounds");
            const auto cell = CellId::exterior(exterior->worldspace(), int32_t(x), int32_t(y));
            if (!worldDomain(cell)) throw std::invalid_argument("Traveler outside processing neighborhood");
            return cell;
        }
        return owner->mCell;
    }

    float InventoryService::meleeReach() const
    {
        const auto id = mBinding.mNavigatingActor->actorId();
        const auto owner = std::ranges::find_if(mBinding.mContainers,
            [id](const auto& value) { return value.mId.value() == id; });
        if (owner == mBinding.mContainers.end()) throw std::invalid_argument("Native melee actor has no inventory");
        const auto values = mRuntime.installedValues(size_t(owner - mBinding.mContainers.begin()) + 2);
        const auto equipped = values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
        const ESM::Weapon* weapon = nullptr;
        if (equipped.isSet())
        {
            const auto item = std::ranges::find(values.mObjects, equipped,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == values.mObjects.end()) throw std::invalid_argument("Native melee weapon identity missing");
            if (mRuntime.mStore.find(item->mRef.mRefID) == ESM::Weapon::sRecordId)
                weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
        }
        return MWMechanics::getMeleeWeaponReach(mRuntime.mStore, weapon,
            mRuntime.ownerPtr(size_t(owner - mBinding.mContainers.begin()) + 2).getType() == ESM::NPC::sRecordId);
    }

    uint64_t InventoryService::meleeContact(const CanonicalServerState& players,
        const ActorSceneSnapshot& actor, uint64_t requested, float reach) const
    {
        const auto cell = actorCell(actor);
        const osg::Vec3f origin(actor.mPosition[0], actor.mPosition[1], actor.mPosition[2]);
        uint64_t result = 0;
        float nearest = std::numeric_limits<float>::infinity();
        for (const auto& session : players.activeSessions())
        {
            const auto* player = players.findPlayer(session.playerId());
            if (!player || std::ranges::find(mBinding.mPlayers, player->playerId()) == mBinding.mPlayers.end()
                || (requested && player->playerId().value() != requested)
                || player->transform().cell() != cell) continue;
            const auto position = player->transform().position();
            const osg::Vec3f target(float(double(position.x()) / 1024),
                float(double(position.y()) / 1024), float(double(position.z()) / 1024));
            // The inherited player mover supplies a server position, but no
            // authoritative player hull yet. Center-to-center reach is
            // conservative until the M4 movement cutover supplies that hull.
            if (!MWMechanics::isInMeleeReach(origin, target, 0, 0, reach)) continue;
            const float distance = (target - origin).length2();
            if (distance < nearest || (distance == nearest && player->playerId().value() < result))
            { nearest = distance; result = player->playerId().value(); }
        }
        return result;
    }

    PlainEquipmentValues InventoryService::combatEquipmentValues(size_t owner, const PreparedNativeInventory* command) const
    {
        command = areaDoorCommand(command);
        if (const auto* transfer = dynamic_cast<const Transaction*>(command))
            return mRuntime.preparedValues(transfer->prepared.mTransfer, owner);
        if (const auto* equipment = dynamic_cast<const EquipmentTransaction*>(command))
            return mRuntime.preparedValues(equipment->prepared, owner);
        if (const auto* world = dynamic_cast<const WorldTransaction*>(command))
            return mRuntime.preparedValues(world->prepared, owner);
        return mRuntime.installedValues(owner);
    }

    EquipmentBytes InventoryService::stagedWeaponCore(std::span<const WeaponWear> wear,
        const PreparedNativeInventory* command, std::span<const ItemCharge> charges,
        const EquipmentRuntime::PreparedRespawn* respawn,
        std::span<const ProjectileRecovery> recoveries, EquipmentSessionValues* stagedValues) const
    {
        if (wear.size() > (mBinding.mRangedFlight ? 3 * MWWorld::InventoryStore::Slots : 4)
            || charges.size() > 2 + MaximumActorProjectiles || (wear.empty() && charges.empty() && recoveries.empty())
            || mWorld.getPtrRegistryRevision() >= std::numeric_limits<size_t>::max()
                - wear.size() - charges.size() - recoveries.size())
            throw std::invalid_argument("Native melee weapon wear candidate invalid");
        command = areaDoorCommand(command);
        const auto* transfer = dynamic_cast<const Transaction*>(command);
        const auto* equipment = dynamic_cast<const EquipmentTransaction*>(command);
        const auto* world = dynamic_cast<const WorldTransaction*>(command);
        const auto candidateValues = [&](size_t owner) {
            return respawn && respawn->owner() == owner ? respawn->values()
                : combatEquipmentValues(owner, command);
        };
        const uint64_t revision = transfer ? transfer->prepared.candidate().mRevision
            : equipment ? equipment->prepared.candidate().mRevision
            : world ? world->prepared.revision() : mWorld.getPtrRegistryRevision();
        if (revision >= std::numeric_limits<size_t>::max()
                - wear.size() - charges.size() - recoveries.size() - size_t(bool(respawn)))
            throw std::invalid_argument("Native melee inventory revision exhausted");
        EquipmentSessionValues values{{candidateValues(0), candidateValues(1)},
            revision + wear.size() + charges.size() + recoveries.size() + size_t(bool(respawn))};
        for (size_t i = 2; i < mRuntime.ownerCount(); ++i)
            values.mContainers.push_back(candidateValues(i));
        if (world)
        {
            values.mWorldItems = mRuntime.worldValues(&world->prepared);
            values.mWorldCells = mRuntime.preparedWorldCells(world->prepared);
        }
        for (size_t index = 0; index < wear.size(); ++index)
        {
            const auto& change = wear[index];
            if (change.owner >= mRuntime.ownerCount())
                throw std::invalid_argument("Native melee weapon wear owner invalid");
            if (respawn && change.owner == respawn->owner())
                throw std::invalid_argument("Respawning actor cannot wear a weapon in the same tick");
            const auto selected = equippedCandidateCondition(candidateValues(change.owner), change.slot, mRuntime.mStore);
            if (change.condition < 0
                || change.condition > change.before.mCondition
                || change.slot < 0 || change.slot >= MWWorld::InventoryStore::Slots
                || std::ranges::any_of(wear.first(index), [&](const auto& earlier) {
                    return earlier.owner == change.owner && earlier.slot == change.slot;
                })
                || !selected || selected->item != change.before
                || (change.remainder && (!std::isfinite(*change.remainder)
                    || *change.remainder <= -1.f || *change.remainder >= 1.f)))
                throw std::invalid_argument("Native melee weapon wear owner invalid");
            auto& owner = change.owner < 2 ? values.mActors[change.owner] : values.mContainers.at(change.owner - 2);
            if (owner.mSlots[change.slot] != change.before.mItem)
                throw std::invalid_argument("Native melee weapon slot changed");
            const auto item = std::ranges::find(owner.mObjects, change.before.mItem,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == owner.mObjects.end()) throw std::invalid_argument("Native melee weapon missing");
            item->mRef.mChargeInt = change.condition;
            if (change.remainder) item->mRef.mChargeIntRemainder = *change.remainder;
            if (change.condition == 0) owner.mSlots[change.slot] = {};
        }
        for (size_t index = 0; index < charges.size(); ++index)
        {
            const auto& charge = charges[index];
            if (std::ranges::any_of(charges.first(index), [&](const auto& earlier) {
                    return earlier.item == charge.item;
                })) throw std::invalid_argument("Duplicate native item charge source");
            if (charge.owner >= mRuntime.ownerCount()
                || (respawn && charge.owner == respawn->owner())
                || (!charge.consume && (!std::isfinite(charge.after) || charge.after < 0)))
                throw std::invalid_argument("Native item charge candidate invalid");
            auto& owner = charge.owner < 2 ? values.mActors[charge.owner]
                : values.mContainers.at(charge.owner - 2);
            const auto item = std::ranges::find(owner.mObjects, charge.item,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == owner.mObjects.end() || item->mRef.mEnchantmentCharge != charge.before)
                throw std::invalid_argument("Native item charge source changed");
            if (charge.consume)
            {
                if (item->mRef.mCount <= 0) throw std::invalid_argument("Native consumed item stack changed");
                --item->mRef.mCount;
                if (item->mRef.mCount == 0)
                    for (auto& slot : owner.mSlots) if (slot == charge.item) slot = {};
            }
            else item->mRef.mEnchantmentCharge = charge.after;
        }
        if (!recoveries.empty())
        {
            if (recoveries.size() > MaximumActorProjectiles)
                throw std::invalid_argument("Native projectile recovery capacity exceeded");
            auto counter = values.mActors[0].mLastGenerated;
            if (counter.mContentFile != -1 || recoveries.size() > UINT32_MAX - counter.mIndex)
                throw std::invalid_argument("Native projectile recovery identity exhausted");
            for (const auto& recovery : recoveries)
            {
                if (recovery.owner < 2 || recovery.owner >= mRuntime.ownerCount())
                    throw std::invalid_argument("Native projectile recovery victim invalid");
                const auto* record = mRuntime.mStore.get<ESM::Weapon>().search(recovery.record);
                if (!record || !record->mEnchant.empty())
                    throw std::invalid_argument("Native projectile recovery source invalid");
                auto& recipient = values.mContainers.at(recovery.owner - 2);
                if (recipient.mObjects.size() >= PlainEquipmentValues::MaxItems)
                    throw std::invalid_argument("Native projectile recovery inventory full");
                ESM::ObjectState item;
                if (record->mScript.empty())
                {
                    item.blank();
                    item.mRef.mRefID = recovery.record;
                    item.mEnabled = 1;
                    item.mHasCustomState = false;
                }
                else
                {
                    if (recovery.sourceOwner >= mRuntime.ownerCount() || !recovery.source.isSet())
                        throw std::invalid_argument("Native scripted projectile source owner invalid");
                    const auto& source = recovery.sourceOwner < 2 ? values.mActors[recovery.sourceOwner]
                        : values.mContainers.at(recovery.sourceOwner - 2);
                    const auto original = std::ranges::find(source.mObjects, recovery.source,
                        [](const auto& object) { return object.mRef.mRefNum; });
                    if (original == source.mObjects.end() || original->mRef.mRefID != recovery.record)
                        throw std::invalid_argument("Native scripted projectile source changed");
                    item = *original;
                }
                item.mRef.mRefNum = {++counter.mIndex, -1};
                item.mRef.mCount = 1;
                recipient.mObjects.push_back(std::move(item));
            }
            for (auto& actor : values.mActors) actor.mLastGenerated = counter;
            for (auto& recipient : values.mContainers) recipient.mLastGenerated = counter;
            if (values.mWorldItems) values.mWorldItems->mLastGenerated = counter;
        }
        EquipmentBytes core;
        if (stagedValues) *stagedValues = values;
        mRuntime.encodeSession(std::move(values), core);
        return core;
    }

    std::optional<ServerApp::NativeTravelDiagnostics> InventoryService::travelDiagnostics() const
    {
        if (!mBinding.mRetainTraveler) return {};
        auto result = mTravelDiagnostics;
        result.tick = mActorTick;
        result.position = mBinding.mNavigatingActor->snapshot().mPosition;
        result.completed = mBinding.mNavigatingActor->arrived();
        result.cellLimit = mBinding.mTravelerCellBudget;
        result.stepLimit = mBinding.mTravelerStepBudget;
        return result;
    }

    bool InventoryService::validPlayerAiState(size_t index, const ActorCampaignCombat::PlayerAi& state,
        const PreparedNativeInventory* inventory) const
    {
        if (state.factions.size() > 256 || state.bounty < 0 || state.bounty > 10'000'000
            || std::abs(int64_t(state.crimeDisposition)) > 1'000'000 || state.drawState > 2)
            return false;
        if (mBinding.mSocialLifecycle)
        {
            if (state.normalSkills.has_value() != state.werewolf
                || state.normalAttributes.has_value() != state.werewolf) return false;
            const auto validStats = [](const auto& values) {
                return std::ranges::all_of(values, [](float value) {
                    return std::isfinite(value) && std::abs(value) <= 1'000'000.f;
                });
            };
            if (state.werewolf && (!validStats(*state.normalSkills)
                || !validStats(*state.normalAttributes))) return false;
            if (state.werewolf && (state.drawState == uint64_t(MWMechanics::DrawState::Spell)
                || !state.selectedSpell.empty() || state.selectedEnchantedItem)) return false;
            if (state.engagements.size() > 128) return false;
            uint64_t previousWitness = 0;
            for (const auto& entry : state.engagements)
            {
                if (!entry.witness || entry.witness <= previousWitness || !entry.tick
                    || entry.tick > mActorTick || entry.fight < 0 || entry.fight > 100
                    || std::ranges::none_of(mBinding.mCrimeWitnesses, [&](const auto& witness) {
                        return witness.placement == entry.witness;
                    })) return false;
                previousWitness = entry.witness;
            }
        }
        ESM::RefId previous;
        for (const auto& faction : state.factions)
        {
            if (faction.id.empty() || faction.rank < 0 || faction.rank > 9
                || faction.id.serializeText().size() > 256
                || !mRuntime.mStore.get<ESM::Faction>().search(faction.id)
                || (!previous.empty() && !(previous < faction.id))) return false;
            previous = faction.id;
        }
        if (!state.selectedSpell.empty())
        {
            const auto& spells = mRuntime.mStore.get<ESM::NPC>().find(mBinding.mActors[index].mBase)->mSpells.mList;
            if (state.selectedSpell.serializeText().size() > 256
                || !mRuntime.mStore.get<ESM::Spell>().search(state.selectedSpell)
                || std::ranges::find(spells, state.selectedSpell) == spells.end()) return false;
        }
        if (state.selectedEnchantedItem && std::ranges::none_of(combatEquipmentValues(index, inventory).mObjects,
            [&](const auto& item) {
                if (item.mRef.mCount <= 0 || wireId(item.mRef.mRefNum).value() != state.selectedEnchantedItem)
                    return false;
                const auto record = MWWorld::inventoryItemRecord(mRuntime.mStore, item.mRef.mRefID);
                return !record.mEnchant.empty()
                    && mRuntime.mStore.get<ESM::Enchantment>().search(record.mEnchant);
            })) return false;
        return true;
    }

    class InventoryService::ActorTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        std::unique_ptr<PreparedNativeInventory> command;
        std::unique_ptr<InteriorActorScene::Prepared> actor;
        std::optional<MeleeAnimation> melee;
        std::optional<ActorCampaignCombat> combat;
        std::optional<ActorCampaignLife> life;
        std::vector<ActorCampaignLife> neighborLives;
        std::vector<ActorCampaignProjectile> projectiles;
        std::vector<ActorCampaignTimedEffect> timedEffects;
        std::optional<ActorCampaignCast> casting;
        std::unique_ptr<EquipmentRuntime::PreparedRespawn> respawn;
        std::unique_ptr<EquipmentRuntime::PreparedProjectileLoot> loot;
        std::vector<MeleeCombatEvent> playerHits;
        std::vector<ActorMeleeCombatEvent> actorHits;
        std::vector<MagicUseCombatEvent> spellCasts;
        std::vector<MagicImpactCue> magicImpactCues;
        std::vector<WeaponWear> wear;
        std::vector<ItemCharge> charges;
        std::vector<std::pair<PlayerId, Transform>> relocations;
        EquipmentBytes wornCore, wornInventory, respawnCore;
        uint64_t target;
        bool contact;
        EquipmentBytes before;
        uint64_t tick;
        std::array<float, 3> velocity;
        bool consumed = false;
        ServerApp::NativeTravelDiagnostics diagnostics;
        ActorTransaction(InventoryService& owner, std::unique_ptr<PreparedNativeInventory> input,
            std::unique_ptr<InteriorActorScene::Prepared> step, std::optional<MeleeAnimation> swing,
            uint64_t selected, bool contacted, std::optional<ActorCampaignCombat> stagedCombat,
            std::optional<ActorCampaignLife> stagedLife,
            std::vector<ActorCampaignLife> stagedNeighborLives,
            std::vector<ActorCampaignProjectile> stagedProjectiles,
            std::vector<ActorCampaignTimedEffect> stagedTimedEffects,
            std::optional<ActorCampaignCast> stagedCasting,
            std::unique_ptr<EquipmentRuntime::PreparedRespawn> stagedRespawn,
            std::unique_ptr<EquipmentRuntime::PreparedProjectileLoot> stagedLoot,
            std::vector<MeleeCombatEvent> stagedPlayerHits,
            std::vector<ActorMeleeCombatEvent> stagedActorHits,
            std::vector<MagicUseCombatEvent> stagedSpellCasts,
            std::vector<MagicImpactCue> stagedMagicImpactCues,
            std::vector<WeaponWear> stagedWear, std::vector<ItemCharge> stagedCharges, EquipmentBytes core,
            std::vector<std::pair<PlayerId, Transform>> stagedRelocations,
            uint64_t time, std::array<float,3> motion,
            ServerApp::NativeTravelDiagnostics report)
            : service(owner), command(std::move(input)), actor(std::move(step)), melee(std::move(swing)),
              combat(std::move(stagedCombat)), life(std::move(stagedLife)),
              neighborLives(std::move(stagedNeighborLives)),
              projectiles(std::move(stagedProjectiles)), timedEffects(std::move(stagedTimedEffects)), casting(stagedCasting),
              respawn(std::move(stagedRespawn)), loot(std::move(stagedLoot)),
              playerHits(std::move(stagedPlayerHits)),
              actorHits(std::move(stagedActorHits)), spellCasts(std::move(stagedSpellCasts)),
              magicImpactCues(std::move(stagedMagicImpactCues)),
              wear(std::move(stagedWear)), charges(std::move(stagedCharges)),
              relocations(std::move(stagedRelocations)),
              wornCore(std::move(core)), target(selected), contact(contacted), before(owner.mActorImage),
              tick(time), velocity(motion), diagnostics(report)
        { if (respawn) respawnCore.assign(respawn->image().begin(), respawn->image().end()); }
        bool changesInventory() const noexcept override
        { return bool(command) || !wear.empty() || !charges.empty() || bool(respawn) || bool(loot); }
        std::span<const std::pair<PlayerId, Transform>> playerRelocations() const override
        { return relocations; }
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        {
            if (consumed || service.inventoryImage().empty() || before != service.mActorImage
                || (actor && !service.mBinding.mNavigatingActor->canInstall(*actor))
                || (loot && loot->beforeRevision() != service.mWorld.getPtrRegistryRevision()))
                return CanonicalDurabilityResult::Rejected;
            try
            {
                for (const auto& change : wear)
                    if (const auto current = equippedCandidateCondition(
                            service.combatEquipmentValues(change.owner, command.get()), change.slot,
                            service.mRuntime.mStore); !current || current->item != change.before)
                        return CanonicalDurabilityResult::Rejected;
                for (const auto& charge : charges)
                {
                    const auto source = service.mWorld.getPtr(charge.item);
                    if (!source.hasLiveReference() || source.mContainerStore != &service.mRuntime.storage(charge.owner)
                        || source.getCellRef().getEnchantmentCharge() != charge.before
                        || (charge.consume && source.getCellRef().getCount() <= 0))
                        return CanonicalDurabilityResult::Rejected;
                }
                EquipmentBytes sealed;
                const auto compose = [&](std::span<const std::byte> inventory) {
                    const auto retained = readActorCampaign(before).actor;
                    const std::span<const char> candidate(reinterpret_cast<const char*>(inventory.data()), inventory.size());
                    if (!wornCore.empty()) wornInventory = service.replaceAreaCore(candidate, wornCore);
                    else if (respawn) wornInventory = service.replaceAreaCore(candidate, respawn->image());
                    const auto selected = !wornInventory.empty() ? std::span<const char>(wornInventory) : candidate;
                    sealed = service.sealActor(selected,
                        actor ? actor->image() : retained, tick, velocity, melee, target, contact, combat, life, neighborLives, projectiles,
                        timedEffects, casting);
                    return persist(std::as_bytes(std::span(sealed)));
                };
                const auto result = command ? command->commit(compose) : compose(std::as_bytes(std::span(service.mImage)));
                if (result == CanonicalDurabilityResult::Rejected) return result;
                consumed = true;
                if (result == CanonicalDurabilityResult::Failed) service.mRuntime.mFailedClosed = true;
                else
                {
                    if (loot) service.mRuntime.installProjectileLoot(*loot);
                    else if (respawn) service.mRuntime.installRespawn(*respawn);
                    if (!loot && (!wear.empty() || !charges.empty()))
                    {
                        for (const auto& change : wear)
                            if (change.slot == MWWorld::InventoryStore::Slot_CarriedRight)
                                service.mRuntime.installWeaponWear(change.owner, change.before.mItem, change.condition,
                                    change.remainder);
                            else service.mRuntime.installArmorWear(change.owner, change.slot, change.before.mItem,
                                change.condition, change.remainder);
                        for (const auto& charge : charges)
                        {
                            if (charge.consume) service.mRuntime.installConsumedMagicItem(charge.owner, charge.item);
                            else service.mRuntime.installEnchantmentCharge(charge.owner, charge.item, charge.after);
                        }
                    }
                    if (respawn || !wear.empty() || !charges.empty() || loot)
                    {
                        if (wornCore.empty()) service.mCoreImage.swap(respawnCore);
                        else service.mCoreImage.swap(wornCore);
                        service.mImage.swap(wornInventory);
                    }
                    if (actor) service.mBinding.mNavigatingActor->install(*actor);
                    service.mMelee = std::move(melee);
                    service.mMeleeTarget = target; service.mMeleeContacted = contact;
                    service.mCombat = std::move(combat);
                    service.mLife = std::move(life);
                    service.mNeighborLives = std::move(neighborLives);
                    service.mProjectiles = std::move(projectiles);
                    service.mTimedEffects = std::move(timedEffects);
                    service.mNpcCast = casting;
                    service.mActorTick = tick; service.mActorVelocity = velocity;
                    service.mActorImage.swap(sealed);
                    service.installActorPosition();
                    if (service.mBinding.mRetainTraveler)
                    {
                        const bool changed = diagnostics.status != service.mTravelDiagnostics.status;
                        service.mTravelDiagnostics = diagnostics;
                        if (changed || tick % 30 == 0)
                        {
                            const auto report = *service.travelDiagnostics();
                            const char* statuses[]{"idle", "running", "cell-capacity", "step-capacity", "boundary", "no-path"};
                            std::fprintf(stderr, "native travel committed: tick=%llu status=%s cells=%zu/%zu steps=%zu/2 completed=%d position=%.6f,%.6f,%.6f\n",
                                static_cast<unsigned long long>(tick), statuses[size_t(report.status)], report.demandedCells,
                                report.cellLimit, report.stepLimit, int(report.completed), report.position[0], report.position[1], report.position[2]);
                        }
                    }
                }
                return result;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native actor commit failed: %.240s\n", error.what());
                service.mRuntime.mFailedClosed = true;
                return CanonicalDurabilityResult::Failed;
            }
            catch (...) { service.mRuntime.mFailedClosed = true; return CanonicalDurabilityResult::Failed; }
        }
    };

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareNativeTick(const CanonicalServerState& players,
        ServerTick tick, float seconds, std::unique_ptr<PreparedNativeInventory> command,
        const CanonicalWorldState* world)
    { return prepareNativeTick(players, tick, seconds, std::move(command), {}, world); }

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareNativeTick(const CanonicalServerState& players,
        ServerTick tick, float seconds, std::unique_ptr<PreparedNativeInventory> command,
        std::optional<ActorMagicCast> actorCast, const CanonicalWorldState* world,
        std::span<const PlayerAiUpdate> playerAiUpdates,
        std::span<const PlayerSocialAction> socialActions,
        std::optional<FactionScriptRequest> factionScript,
        std::optional<MovementRuleScriptRequest> movementRuleScript)
    try
    {
        if (mRuntime.mFailedClosed || mRuntime.mRestartActor) return {};
        if (actorCast && !mBinding.mNavigatingActor)
            throw std::invalid_argument("Native actor cast requires a bound scene");
        if (!mBinding.mNavigatingActor) return command ? std::move(command) : prepareDoorStep(players, tick, seconds);
        if (!std::isfinite(seconds) || std::abs(seconds - 1.f/30.f) > 1e-6f || tick.value() <= mActorTick)
            throw std::invalid_argument("Native actor requires increasing 30 Hz durable ticks");
        const auto [aiNpc, aiUndead] = aiActorKind(mRuntime.ownerPtr(mCombatNpcOwner));
        const uint64_t gameNowMs = mBinding.mSpecialConditions ? gameTimeMilliseconds(world) : 0;
        std::optional<ClientMeleeAttackCommand> playerAttack;
        PlayerId playerAttacker = mBinding.mPlayers[0];
        std::unique_ptr<SpellTransaction> playerCasts;
        if (dynamic_cast<SpellTransaction*>(command.get()))
            playerCasts.reset(static_cast<SpellTransaction*>(command.release()));
        if (const auto* attack = dynamic_cast<const AttackTransaction*>(command.get()))
        {
            playerAttack = attack->attack;
            playerAttacker = attack->attacker;
            command.reset();
        }
        const bool dueRespawn = mLife && mLife->respawnTick && tick.value() >= mLife->respawnTick && !command;
        std::optional<size_t> dueNeighborRespawn;
        if (mBinding.mNeighborCombat && !dueRespawn && !command)
            for (size_t i = 0; i < mNeighborLives.size(); ++i)
                if (mNeighborLives[i].respawnTick && tick.value() >= mNeighborLives[i].respawnTick)
                { dueNeighborRespawn = i; break; }
        if (!dueRespawn && !dueNeighborRespawn)
        {
            if (!mBinding.mDoors.empty()) command = prepareAreaDoor(0, false, players, tick, seconds, std::move(command));
            else if (!command) command = prepareDoorStep(players, tick, seconds);
        }
        const auto before = mBinding.mNavigatingActor->snapshot();
        bool active = mBinding.mRetainTraveler && !mBinding.mNavigatingActor->arrived();
        for (const auto& session : players.activeSessions())
            if (const auto* player = players.findPlayer(session.playerId()); player
                && (mBinding.mTravelerNeighborhood ? sharesCellNeighborhood(player->transform().cell(), actorCell(before))
                                                  : player->transform().cell() == actorCell(before))) active = true;
        if (mCombat && std::ranges::none_of(std::span(mCombat->actors).subspan(2),
                [](const auto& stats) { return stats[8][2] > 0; })) active = false;
        using Diagnostics = ServerApp::NativeTravelDiagnostics;
        Diagnostics report;
        report.status = active ? Diagnostics::Status::Running : Diagnostics::Status::Idle;
        report.demandedCells = active ? (mBinding.mTravelerNeighborhood ? mBinding.worldDomains().size() : 1) : 0;
        if (mBinding.mTravelerNeighborhood && active)
        {
            if (report.demandedCells > mBinding.mTravelerCellBudget) report.status = Diagnostics::Status::CellCapacity;
            else if (mBinding.mTravelerStepBudget < 2) report.status = Diagnostics::Status::StepCapacity;
            active = report.status == Diagnostics::Status::Running;
        }
        const auto doors = actorDoorFrames(command.get());
        auto combat = mCombat;
        auto timedEffects = mTimedEffects;
        if (movementRuleScript)
        {
            if (!mBinding.mScriptedMovementRules || !combat) return {};
            const auto rules = runMovementRuleScript(mRuntime.mStore,
                movementRuleScript->script, mRuntime.ownerPtr(mCombatNpcOwner),
                {combat->levitationEnabled, combat->teleportingEnabled}, mBinding.mScriptedTravelRules);
            combat->levitationEnabled = rules.levitationEnabled;
            combat->teleportingEnabled = rules.teleportingEnabled;
            if (!combat->levitationEnabled)
                std::erase_if(timedEffects, [](const auto& effect) {
                    return effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(
                        ESM::MagicEffect::Levitate));
                });
        }
        const bool levitationEnabled = mBinding.mScriptedMovementRules
            ? combat->levitationEnabled : mBinding.mLevitationEnabled;
        const auto commandedDestination = [&](size_t index, const ActorSceneSnapshot& source,
            uint64_t generation) -> std::optional<std::array<float, 3>> {
            if (!combat || !generation || combat->actors[index][8][2] <= 0.f) return {};
            const auto actorPtr = mRuntime.ownerPtr(combatOwner(index));
            const bool npc = actorPtr.getType() == ESM::NPC::sRecordId;
            const int level = npc ? actorPtr.get<ESM::NPC>()->mBase->mNpdt.mLevel
                : actorPtr.get<ESM::Creature>()->mBase->mData.mLevel;
            const auto commandIndex = uint64_t(ESM::MagicEffect::refIdToIndex(npc
                ? ESM::MagicEffect::CommandHumanoid : ESM::MagicEffect::CommandCreature));
            const ActorCampaignTimedEffect* controlling = nullptr;
            for (const auto& effect : timedEffects)
                if (effect.actor == index && effect.effectIndex == commandIndex
                    && effect.magnitude >= level && effect.expiresTick > tick.value()
                    && (effect.casterKind == 1 || effect.casterKind == 2)
                    && (!controlling || effect.startTick > controlling->startTick
                        || (effect.startTick == controlling->startTick && effect.source > controlling->source)))
                    controlling = &effect;
            if (!controlling) return {};
            std::optional<std::array<float, 3>> follow;
            if (controlling->casterKind == 1 && controlling->casterLife == 1)
            {
                const auto casterId = PlayerId::fromValue(controlling->caster);
                const auto* caster = casterId ? players.findPlayer(*casterId) : nullptr;
                const auto casterBinding = casterId
                    ? std::ranges::find(mBinding.mPlayers, *casterId) : mBinding.mPlayers.end();
                if (!caster || caster->transform().cell() != actorCell(source)
                    || casterBinding == mBinding.mPlayers.end()
                    || combat->actors[size_t(casterBinding - mBinding.mPlayers.begin())][8][2] <= 0
                    || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                        return session.playerId() == *casterId;
                    })) return {};
                const auto& position = caster->transform().position();
                follow = {float(double(position.x()) / 1024),
                    float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
            }
            else if (controlling->casterKind == 2)
            {
                const auto placement = std::ranges::find(combat->npcPlacements, controlling->caster);
                if (placement == combat->npcPlacements.end()) return {};
                const size_t casterIndex = size_t(placement - combat->npcPlacements.begin()) + 2;
                if (casterIndex == index || combat->actors[casterIndex][8][2] <= 0) return {};
                const auto& casterLife = casterIndex == 2 ? *mLife : mNeighborLives.at(casterIndex - 3);
                if (casterLife.respawnTick || controlling->casterLife != casterLife.generation) return {};
                const auto casterBody = casterIndex == 2 ? mBinding.mNavigatingActor->snapshot()
                    : mBinding.mNavigatingActor->neighborSnapshots().at(casterIndex - 3);
                if (actorCell(casterBody) != actorCell(source)) return {};
                follow = casterBody.mPosition;
            }
            if (!follow) return {};
            if (!mBinding.mNavigatingActor->contains(*follow)) return {};
            return follow;
        };
        ActorMovement movement;
        auto navigationDestination = mBinding.mTravelDestination;
        std::optional<std::array<float, 3>> blindFleeEnemy;
        bool selectedFollowing = false;
        if (active && combat && mBinding.mMovementEffects)
        {
            if (const auto follow = commandedDestination(2, before,
                    mLife && !mLife->respawnTick ? mLife->generation : 0))
            {
                navigationDestination = *follow;
                selectedFollowing = true;
            }
            else if (mBinding.mAiDecisions && combat->fleeTarget
                && (combat->fleeDestination != std::array<float, 3>{}
                    || combat->fleeUntil > tick.value()))
            {
                const auto fleeingFrom = PlayerId::fromValue(combat->fleeTarget);
                const auto* player = fleeingFrom ? players.findPlayer(*fleeingFrom) : nullptr;
                if (player && player->transform().cell() == actorCell(before))
                {
                    if (combat->fleeDestination == std::array<float, 3>{})
                    {
                        const auto position = player->transform().position();
                        blindFleeEnemy = {float(double(position.x()) / 1024),
                            float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
                    }
                    else if (mBinding.mNavigatingActor->contains(combat->fleeDestination))
                        navigationDestination = combat->fleeDestination;
                }
            }
        }
        if (active && mBinding.mMovementEffects)
        {
            if (!combat) throw std::logic_error("NPC movement requires committed actor stats");
            const auto stats = loadCombatStats(mRuntime.mStore, combat->actors[2], timedEffects, 2);
            const auto magnitude = [&](ESM::RefId id) {
                if (id == ESM::MagicEffect::Levitate && !levitationEnabled) return 0.f;
                const auto index = uint64_t(ESM::MagicEffect::refIdToIndex(id));
                float result = 0.f;
                for (const auto& effect : timedEffects)
                    if (effect.actor == 2 && effect.effectIndex == index) result += effect.magnitude;
                return result;
            };
            const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
            const auto setting = [&](const char* name) { return settings.find(name)->mValue.getFloat(); };
            const float strength = stats.getAttribute(ESM::Attribute::Strength).getModified();
            const float speed = stats.getAttribute(ESM::Attribute::Speed).getModified();
            const float athletics = stats.getSkill(ESM::Skill::Athletics).getModified();
            const float acrobatics = stats.getSkill(ESM::Skill::Acrobatics).getModified();
            const float weight = std::max(0.f, mRuntime.storage(mCombatNpcOwner).getWeight()
                - magnitude(ESM::MagicEffect::Feather) + magnitude(ESM::MagicEffect::Burden));
            const float encumbrance = MWClass::normalizedEncumbrance(weight,
                strength * setting("fEncumbranceStrMult"));
            const float walk = MWClass::npcWalkSpeed(speed, encumbrance,
                setting("fMinWalkSpeed"), setting("fMaxWalkSpeed"), setting("fEncumberedMoveEffect"));
            const bool inert = encumbrance > 1.f || magnitude(ESM::MagicEffect::Paralyze) > 0.f
                || combat->knockedDown[2] || combat->actors[2][8][2] <= 0;
            movement.enabled = true;
            const bool fleeing = mBinding.mAiDecisions && combat->fleeTarget
                && (combat->fleeDestination != std::array<float, 3>{}
                    || combat->fleeUntil > tick.value())
                && (blindFleeEnemy || navigationDestination == combat->fleeDestination);
            movement.walkSpeed = inert ? 0.f : fleeing
                ? MWClass::npcRunSpeed(walk, athletics, setting("fAthleticsRunBonus"),
                    setting("fBaseRunMultiplier")) : walk;
            movement.swimSpeed = inert ? 0.f : MWClass::npcSwimSpeed(walk,
                magnitude(ESM::MagicEffect::SwiftSwim), athletics,
                setting("fSwimRunBase"), setting("fSwimRunAthleticsMult"));
            movement.flySpeed = inert ? 0.f : MWClass::npcFlySpeed(speed,
                magnitude(ESM::MagicEffect::Levitate), encumbrance,
                setting("fMinFlySpeed"), setting("fMaxFlySpeed"), setting("fEncumberedMoveEffect"));
            movement.jumpSpeed = inert ? 0.f : MWClass::npcJumpSpeed(encumbrance, acrobatics,
                magnitude(ESM::MagicEffect::Jump), stats.getFatigueTerm(mRuntime.mStore), false,
                setting("fJumpEncumbranceBase"), setting("fJumpEncumbranceMultiplier"),
                setting("fJumpAcrobaticsBase"), setting("fJumpAcroMultiplier"),
                setting("fJumpRunMultiplier"), Constants::GravityConst * Constants::UnitsPerMeter);
            movement.slowFall = MWClass::npcSlowFall(magnitude(ESM::MagicEffect::SlowFall));
            movement.levitating = magnitude(ESM::MagicEffect::Levitate) > 0.f;
            movement.waterWalking = magnitude(ESM::MagicEffect::WaterWalking) > 0.f;
            movement.waterBreathing = magnitude(ESM::MagicEffect::WaterBreathing) > 0.f;
            movement.unconscious = combat->knockedDown[2];
        }
        auto step = active ? (mBinding.mMovementEffects
            ? blindFleeEnemy ? mBinding.mNavigatingActor->prepareBlindRun(movement, doors, *blindFleeEnemy)
                : mBinding.mNavigatingActor->prepareNavigation(movement, doors, navigationDestination)
            : mBinding.mNavigatingActor->prepareNavigation(mBinding.mNavigationSpeed, doors)) : nullptr;
        if (step && mBinding.mTravelerNeighborhood && !mBinding.mNavigatingActor->contains(step->snapshot().mPosition))
        { step.reset(); report.status = Diagnostics::Status::Boundary; }
        if (active && !step) report.status = Diagnostics::Status::Boundary;
        else if (step && step->pathUnavailable()) report.status = Diagnostics::Status::NoPath;
        auto after = step ? step->snapshot() : before;
        const std::function<float(size_t)> sunExposure = [&](size_t index) {
            if (!mBinding.mSpecialConditions || !world)
                throw std::invalid_argument("Native SunDamage requires a world snapshot");
            const auto cell = index >= 2 ? actorCell(after)
                : players.findPlayer(mBinding.mPlayers[index])->transform().cell();
            const auto* domain = worldDomain(cell);
            if (!domain) throw std::invalid_argument("Native SunDamage cell is not bound");
            if (!domain->mSunExposed) return 0.f;
            if (!mSunDamageScale || domain->mSunRegion.empty())
                throw std::invalid_argument("Native SunDamage weather is not bound");
            return mSunDamageScale(*world, domain->mSunRegion);
        };
        auto melee = mMelee;
        std::optional<PlayerAiUpdate> scriptedFactionUpdate;
        if (factionScript)
        {
            if (!mBinding.mPlayerAi || !combat || !playerAiUpdates.empty() || !socialActions.empty())
                return {};
            const auto found = std::ranges::find(mBinding.mPlayers, factionScript->player);
            if (found == mBinding.mPlayers.end()) return {};
            const size_t owner = size_t(found - mBinding.mPlayers.begin());
            scriptedFactionUpdate.emplace(PlayerAiUpdate{factionScript->player,
                runFactionScript(mRuntime.mStore, factionScript->script,
                    mRuntime.ownerPtr(mCombatNpcOwner), mRuntime.ownerPtr(owner), combat->players[owner])});
            playerAiUpdates = std::span<const PlayerAiUpdate>(&*scriptedFactionUpdate, 1);
        }
        std::array<std::optional<bool>, 2> requestedWerewolf{};
        if (!socialActions.empty())
        {
            if (!mBinding.mPlayerAi || !combat || !playerAiUpdates.empty()
                || socialActions.size() > 32) return {};
            for (const auto& action : socialActions)
            {
                const auto found = std::ranges::find(mBinding.mPlayers, action.player);
                if (found == mBinding.mPlayers.end()) return {};
                const size_t index = size_t(found - mBinding.mPlayers.begin());
                auto& state = combat->players[index];
                auto faction = std::ranges::find(state.factions, action.faction,
                    &ActorCampaignCombat::PlayerAi::Faction::id);
                const bool factionAction = action.kind == PlayerSocialAction::Kind::JoinFaction
                    || action.kind == PlayerSocialAction::Kind::SetFactionRank
                    || action.kind == PlayerSocialAction::Kind::SetFactionExpelled;
                if (factionAction)
                {
                    if (action.faction.empty() || action.faction.serializeText().size() > 256
                        || !mRuntime.mStore.get<ESM::Faction>().search(action.faction)) return {};
                }
                else if (!action.faction.empty()) return {};
                switch (action.kind)
                {
                    case PlayerSocialAction::Kind::JoinFaction:
                        if (action.value != 0 || faction != state.factions.end()
                            || state.factions.size() >= 256) return {};
                        state.factions.insert(std::lower_bound(state.factions.begin(), state.factions.end(),
                            action.faction, [](const auto& entry, const ESM::RefId& id) {
                                return entry.id < id;
                            }), {action.faction, 0, false});
                        break;
                    case PlayerSocialAction::Kind::SetFactionRank:
                        if (faction == state.factions.end() || action.value < 0 || action.value > 9)
                            return {};
                        faction->rank = action.value;
                        break;
                    case PlayerSocialAction::Kind::SetFactionExpelled:
                        if (faction == state.factions.end() || (action.value != 0 && action.value != 1))
                            return {};
                        faction->expelled = action.value != 0;
                        break;
                    case PlayerSocialAction::Kind::ReportCrime:
                        if (action.value <= 0 || action.value > 10'000'000 - state.bounty) return {};
                        state.bounty += action.value;
                        break;
                    case PlayerSocialAction::Kind::ClearBounty:
                        if (action.value != 0) return {};
                        state.bounty = 0;
                        break;
                    case PlayerSocialAction::Kind::SetCrimeDisposition:
                        if (std::abs(int64_t(action.value)) > 1'000'000) return {};
                        state.crimeDisposition = action.value;
                        break;
                    case PlayerSocialAction::Kind::SetWerewolf:
                        if (action.value != 0 && action.value != 1) return {};
                        if (mBinding.mSocialLifecycle)
                        {
                            if (requestedWerewolf[index] || playerAttack || playerCasts)
                                return {};
                            requestedWerewolf[index] = action.value != 0;
                        }
                        else
                        {
                            state.werewolf = action.value != 0;
                            if (state.werewolf && state.drawState == uint64_t(MWMechanics::DrawState::Spell))
                                state.drawState = uint64_t(MWMechanics::DrawState::Nothing);
                        }
                        break;
                    case PlayerSocialAction::Kind::SetKnownWerewolf:
                        if (action.value != 0 && action.value != 1) return {};
                        state.knownWerewolf = action.value != 0;
                        break;
                    default: return {};
                }
            }
            for (size_t index = 0; index < 2; ++index)
                if (!validPlayerAiState(index, combat->players[index], command.get())) return {};
        }
        if (!playerAiUpdates.empty())
        {
            if (!mBinding.mPlayerAi || !combat || playerAiUpdates.size() > 2) return {};
            std::array<bool, 2> seen{};
            for (const auto& update : playerAiUpdates)
            {
                const auto found = std::ranges::find(mBinding.mPlayers, update.player);
                if (found == mBinding.mPlayers.end()) return {};
                const size_t index = size_t(found - mBinding.mPlayers.begin());
                if (seen[index] || !validPlayerAiState(index, update.state, command.get())
                    || (mBinding.mPlayerTravel && update.state.mark != combat->players[index].mark)
                    || (mBinding.mSocialLifecycle
                        && (update.state.werewolf != combat->players[index].werewolf
                            || update.state.normalSkills != combat->players[index].normalSkills
                            || update.state.normalAttributes != combat->players[index].normalAttributes
                            || update.state.engagements != combat->players[index].engagements))) return {};
                seen[index] = true;
                combat->players[index] = update.state;
            }
        }
        if (mBinding.mPlayerAi && combat)
        {
            // Accepted gameplay commands are the producer for draw and source state.
            // Keep them in the staged campaign so a failed write cannot publish them.
            if (playerAttack)
                combat->players[actor(playerAttacker)].drawState = uint64_t(MWMechanics::DrawState::Weapon);
            for (auto* cast = playerCasts.get(); cast; cast = cast->concurrent.get())
            {
                auto& selection = combat->players[actor(cast->caster)];
                selection.drawState = uint64_t(MWMechanics::DrawState::Spell);
                if (cast->use.sourceKind == MagicUseSourceKind::Spell)
                {
                    const auto& known = mRuntime.mStore.get<ESM::NPC>()
                        .find(mBinding.mActors[actor(cast->caster)].mBase)->mSpells.mList;
                    const auto found = std::ranges::find_if(known, [&](const ESM::RefId& id) {
                        return spellRecordId(id) == cast->use.sourceId;
                    });
                    if (found == known.end()) throw std::invalid_argument("Accepted player spell source disappeared");
                    selection.selectedSpell = *found;
                    selection.selectedEnchantedItem = 0;
                }
                else
                {
                    selection.selectedSpell = {};
                    selection.selectedEnchantedItem = cast->charge && cast->charge->consume
                        ? 0 : cast->use.sourceId;
                }
            }
            if (const auto* equipment = dynamic_cast<const EquipmentTransaction*>(areaDoorCommand(command.get())))
                if (equipment->binding)
                {
                    const size_t index = actor(equipment->binding->player());
                    const auto beforeRight = mRuntime.installedValues(index)
                        .mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
                    const auto afterRight = combatEquipmentValues(index, command.get())
                        .mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
                    if (beforeRight != afterRight)
                    {
                        auto& draw = combat->players[index].drawState;
                        if (afterRight.isSet()) draw = uint64_t(MWMechanics::DrawState::Weapon);
                        else if (draw == uint64_t(MWMechanics::DrawState::Weapon))
                            draw = uint64_t(MWMechanics::DrawState::Nothing);
                    }
                }
        }
        if (mBinding.mPlayerAi && combat)
            for (size_t index = 0; index < 2; ++index)
            {
                auto& selected = combat->players[index].selectedEnchantedItem;
                if (!selected) continue;
                const auto values = combatEquipmentValues(index, command.get());
                if (std::ranges::none_of(values.mObjects, [&](const auto& item) {
                    return item.mRef.mCount > 0 && wireId(item.mRef.mRefNum).value() == selected;
                })) selected = 0;
            }
        const auto fleeOutOfSight = [&] {
            if (!combat || !combat->fleeTarget || combat->fleeDestination == std::array<float, 3>{})
                return false;
            const auto id = PlayerId::fromValue(combat->fleeTarget);
            const auto* enemy = id ? players.findPlayer(*id) : nullptr;
            if (!enemy || enemy->transform().cell() != actorCell(before)) return false;
            const auto place = enemy->transform().position();
            const std::array<float, 3> from{before.mPosition[0], before.mPosition[1], before.mPosition[2] + 110.f};
            const std::array<float, 3> to{float(double(place.x()) / 1024),
                float(double(place.y()) / 1024), float(double(place.z()) / 1024) + 110.f};
            const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
            const float fleeDistance = mRuntime.mStore.get<ESM::GameSetting>()
                .find("fFleeDistance")->mValue.getFloat();
            if (!std::isfinite(fleeDistance) || fleeDistance < 0.f || fleeDistance > 1e7f)
                throw std::invalid_argument("Native flee distance outside bounds");
            return dx*dx + dy*dy + dz*dz > fleeDistance * fleeDistance
                && !mBinding.mNavigatingActor->lineOfSight(from, to);
        };
        if (combat && active && combat->fleeTarget && combat->fleeDestination != std::array<float, 3>{}
            && ((navigationDestination == combat->fleeDestination && step && step->pathCompleted())
                || fleeOutOfSight()))
        { combat->fleeTarget = combat->fleeUntil = 0; combat->fleeDestination = {}; }
        if (combat && mBinding.mAiDecisions && combat->fleeTarget
            && ((combat->fleeDestination == std::array<float, 3>{}
                    && combat->fleeUntil <= tick.value()) || combat->actors[2][8][2] <= 0
                || !std::ranges::any_of(players.activeSessions(), [&](const auto& session) {
                    return session.playerId().value() == combat->fleeTarget;
                })))
        { combat->fleeTarget = combat->fleeUntil = 0; combat->fleeDestination = {}; }
        if (combat && active && step && combat->fleeTarget
            && combat->fleeDestination != std::array<float, 3>{})
        {
            if (tick.value() > UINT64_MAX - 30)
                throw std::invalid_argument("Native flee deadline exhausted");
            combat->fleeUntil = tick.value() + 30;
        }
        if (mBinding.mRangedFlight && combat)
            std::erase_if(combat->arrows, [&](const auto& arrow) {
                // Retain a receipt throughout the accepted input window. Once
                // the release is older than that window, its original intent
                // is necessarily stale, including after restart. The campaign
                // tick and removal commit together; rejected writes keep both.
                if (!arrow.terminal || tick.value() <= arrow.releaseTick
                    || tick.value() - arrow.releaseTick <= PhysicalAttackRetryTicks) return false;
                for (size_t owner = 0; owner < combat->swings.size(); ++owner)
                    if (mBinding.mPlayers[owner].value() == arrow.caster && combat->swings[owner]
                        && combat->swings[owner]->command == arrow.command) return false;
                return true;
            });
        auto life = mLife;
        auto neighborLives = mNeighborLives;
        auto projectiles = mProjectiles;
        std::unique_ptr<EquipmentRuntime::PreparedRespawn> respawn;
        std::array<bool, 2> transformedNow{}, changingWerewolfEquipment{};
        auto casting = mNpcCast;
        std::vector<WeaponWear> wear;
        std::vector<ItemCharge> charges;
        std::vector<ProjectileRecovery> recoveries;
        std::vector<std::pair<PlayerId, Transform>> relocations;
        const auto stageDisintegrate = [&](size_t recipient, ESM::RefId effect, float magnitude) {
            if (recipient < 2 && changingWerewolfEquipment[recipient]) return;
            if (!std::isfinite(magnitude) || magnitude < 0.f || magnitude > 100000.f)
                throw std::invalid_argument("Native disintegration magnitude invalid");
            const size_t owner = combatOwner(recipient);
            const auto values = combatEquipmentValues(owner, command.get());
            const auto applySlot = [&](int slot) {
                const auto selected = equippedCandidateCondition(values, slot, mRuntime.mStore);
                if (!selected) return false;
                auto staged = std::ranges::find_if(wear, [&](const auto& change) {
                    return change.owner == owner && change.slot == slot;
                });
                const int beforeCondition = staged == wear.end() ? selected->item.mCondition : staged->condition;
                if (!beforeCondition) return false;
                const float remainder = staged != wear.end() && staged->remainder
                    ? *staged->remainder : selected->remainder;
                const auto afterCondition = MWMechanics::disintegrateCondition(beforeCondition, remainder, magnitude);
                if (staged == wear.end())
                    wear.push_back({owner, selected->item, afterCondition.condition, slot, afterCondition.remainder});
                else
                {
                    staged->condition = afterCondition.condition;
                    staged->remainder = afterCondition.remainder;
                }
                return true;
            };
            if (effect == ESM::MagicEffect::DisintegrateWeapon)
            { applySlot(MWWorld::InventoryStore::Slot_CarriedRight); return; }
            constexpr std::array priorities{
                MWWorld::InventoryStore::Slot_CarriedLeft, MWWorld::InventoryStore::Slot_Cuirass,
                MWWorld::InventoryStore::Slot_LeftPauldron, MWWorld::InventoryStore::Slot_RightPauldron,
                MWWorld::InventoryStore::Slot_LeftGauntlet, MWWorld::InventoryStore::Slot_RightGauntlet,
                MWWorld::InventoryStore::Slot_Helmet, MWWorld::InventoryStore::Slot_Greaves,
                MWWorld::InventoryStore::Slot_Boots};
            for (const int slot : priorities) if (applySlot(slot)) break;
        };
        uint64_t target = mMeleeTarget;
        bool contact = mMeleeContacted;
        std::vector<std::pair<bool, bool>> actorKinds;
        if (mBinding.mExpandedEffects && combat)
        {
            actorKinds.reserve(combat->actors.size());
            for (size_t i = 0; i < combat->actors.size(); ++i)
                actorKinds.push_back(aiActorKind(mRuntime.ownerPtr(combatOwner(i))));
        }
        const auto resolveEffects = [&](const PreparedInstantEffects& plan, int range, size_t index,
            MWMechanics::NpcStats& victim, uint64_t at, ActorCasterIdentity identity, uint64_t source,
            uint64_t kind, Misc::Rng::Generator& rng, const MWWorld::ESMStore& content,
            std::vector<ActorCampaignTimedEffect>& effects, bool lifecycle, bool uncapped,
            std::span<const uint64_t> ordinals = {}, MWMechanics::NpcStats* casterStats = nullptr) {
            if (!mBinding.mExpandedEffects)
                return resolveActorEffects(plan, range, index, victim, at, identity, source, kind, rng,
                    content, effects, lifecycle, uncapped, ordinals);
            std::vector<ActorCasterIdentity> identities{{mBinding.mPlayers[0].value(), 1, 1},
                {mBinding.mPlayers[1].value(), 1, 1},
                {mBinding.mNavigatingActor->actorId(), 2, life->generation}};
            for (size_t i = 0; i + 3 < combat->actors.size(); ++i)
                identities.push_back({combat->npcPlacements.at(i + 1), 2,
                    i < neighborLives.size() ? neighborLives[i].generation : 1});
            auto result = resolveExpandedEffects(plan, range, index, victim, identity, source, kind, at,
                *combat, effects, identities, content, rng, uncapped, mBinding.mClassicReflectedAbsorb,
                actorKinds, ordinals, casterStats, false, sunExposure, stageDisintegrate,
                [&](size_t recipient, ESM::RefId effect) {
                    if (effect == ESM::MagicEffect::Levitate) return levitationEnabled;
                    if (effect == ESM::MagicEffect::WaterWalking)
                    {
                        if (recipient >= 2)
                            return mBinding.mNavigatingActor->waterWalkingCastable(identities[recipient].id);
                        const auto* player = players.findPlayer(mBinding.mPlayers[recipient]);
                        if (!player) throw std::invalid_argument("WaterWalking player context missing");
                        const auto& position = player->transform().position();
                        const auto ptr = mRuntime.ownerPtr(recipient);
                        const auto* npc = ptr.get<ESM::NPC>();
                        return mBinding.mNavigatingActor->waterWalkingCastable(
                            {float(double(position.x()) / 1024), float(double(position.y()) / 1024),
                                float(double(position.z()) / 1024)}, npc->mBase->mRace,
                            ptr.getCellRef().getScale());
                    }
                    return true;
                });
            for (size_t i = 2; i < result.deaths.size(); ++i)
            {
                auto* victimLife = i == 2 ? &*life
                    : i - 3 < neighborLives.size() ? &neighborLives[i - 3] : nullptr;
                if (!victimLife) continue;
                if (!result.deaths[i] || victimLife->respawnTick) continue;
                const auto killer = *result.deaths[i];
                if (at > UINT64_MAX - mBinding.mNpcRespawnDelayTicks)
                    throw std::invalid_argument("Native effect death deadline exhausted");
                victimLife->deaths.push_back({victimLife->generation, at, killer.id, killer.kind, killer.life});
                victimLife->respawnTick = at + mBinding.mNpcRespawnDelayTicks;
                if (i == 2) { step.reset(); after = before; report.status = Diagnostics::Status::Idle; }
            }
            for (size_t i = 0; i < combat->actors.size(); ++i)
            {
                const auto stats = loadCombatStats(content, combat->actors[i], effects, i);
                combat->knockedDown[i] = stats.getHealth().getCurrent() > 0
                    && (fatigueKnockout(stats, true) || combat->knockedDown[i]);
            }
            return result.target;
        };
        const auto recordNpcDeath = [&](size_t index, ActorCasterIdentity killer) {
            auto* victimLife = index == 2 ? (life ? &*life : nullptr)
                : index - 3 < neighborLives.size() ? &neighborLives[index - 3] : nullptr;
            if (!victimLife || victimLife->respawnTick) return;
            if (tick.value() > UINT64_MAX - mBinding.mNpcRespawnDelayTicks)
                throw std::invalid_argument("Native effect death deadline exhausted");
            victimLife->deaths.push_back({victimLife->generation, tick.value(),
                killer.id, killer.kind, killer.life});
            victimLife->respawnTick = tick.value() + mBinding.mNpcRespawnDelayTicks;
            if (combat) std::erase_if(combat->arrows, [&](const auto& arrow) {
                return arrow.casterKind == 2 && arrow.caster == (index == 2 ? before.mActor
                    : combat->npcPlacements.at(index - 2)) && arrow.casterLife == victimLife->generation;
            });
            if (index == 2)
            {
                step.reset(); after = before; report.status = Diagnostics::Status::Idle;
                casting.reset();
            }
        };
        const auto recordEffectDeath = [&](ActorCasterIdentity killer) { recordNpcDeath(2, killer); };
        const auto updateResources = [&](size_t index, const auto& previous, const auto& current, bool retainKnockout) {
            const auto death = updateEffectResources(*combat, index, mRuntime.mStore, previous, current, retainKnockout);
            if (index >= 2 && death) recordNpcDeath(index, *death);
        };
        if (mBinding.mSocialLifecycle && combat)
            for (size_t index = 0; index < requestedWerewolf.size(); ++index)
            {
                if (!requestedWerewolf[index] || *requestedWerewolf[index] == combat->players[index].werewolf)
                    continue;
                if (respawn || dueRespawn || command && dynamic_cast<const EquipmentTransaction*>(areaDoorCommand(command.get())))
                    return {};
                auto& social = combat->players[index];
                const bool transformed = *requestedWerewolf[index];
                respawn = mRuntime.prepareWerewolfEquipment(index, transformed);
                changingWerewolfEquipment[index] = true;
                combat->swings[index].reset();
                const auto previous = timedEffects;
                std::erase_if(timedEffects, [index](const auto& effect) {
                    return effect.actor == index && effect.sourceKind <= 2;
                });
                updateResources(index, previous, timedEffects, mBinding.mKnockoutAnimation);
                if (mBinding.mConstantEffects)
                {
                    Misc::Rng::Generator rng{combat->rng};
                    const auto beforeEquipment = timedEffects;
                    if (reconcileConstants(respawn->values(), index,
                            {mBinding.mPlayers[index].value(), 1, 1}, tick.value(), mRuntime.mStore,
                            mBinding.mGeneralConstants, timedEffects, &rng, mBinding.mExpandedEffects,
                            mBinding.mSpecialConditions, mBinding.mMovementEffects,
                            mBinding.mAiDecisions, false, false, levitationEnabled))
                        updateResources(index, beforeEquipment, timedEffects, mBinding.mKnockoutAnimation);
                    combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                }
                auto stats = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
                if (transformed)
                {
                    const float multiplier = mRuntime.mStore.get<ESM::GameSetting>()
                        .find("fWereWolfHealth")->mValue.getFloat();
                    if (!std::isfinite(multiplier) || multiplier <= 0.f
                        || stats.getHealth().getBase() <= 0.f)
                        throw std::invalid_argument("Werewolf health setting or base invalid");
                    const auto saved = MWMechanics::saveWerewolfStats(stats);
                    social.normalSkills = saved.skills;
                    social.normalAttributes = saved.attributes;
                    MWMechanics::applyWerewolfStats(stats, mRuntime.mStore);
                    social.drawState = uint64_t(MWMechanics::DrawState::Nothing);
                    social.selectedSpell = {};
                    social.selectedEnchantedItem = 0;
                    combat->playerCasts[index].reset();
                }
                else
                {
                    if (!social.normalSkills || !social.normalAttributes)
                        throw std::invalid_argument("Werewolf normal stats absent");
                    MWMechanics::restoreWerewolfStats(stats, mRuntime.mStore,
                        {*social.normalSkills, *social.normalAttributes});
                    social.normalSkills.reset();
                    social.normalAttributes.reset();
                }
                social.werewolf = transformed;
                transformedNow[index] = transformed;
                saveCombatStats(combat->actors[index], stats, timedEffects, index);
            }
        const auto npcDetects = [&](size_t index) {
            auto victim = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
            addTimedResistance(victim, timedEffects, index);
            if (!MWMechanics::isTargetMagicallyHidden(victim)) return true;
            // Stock canFight rolls awareness for a magically hidden target. The
            // detached server actor has no presentation node or sneak stance,
            // leaving the shared magic term as its awareness threshold.
            Misc::Rng::Generator rng{combat->rng};
            const bool detected = Misc::Rng::roll0to99(rng) >= MWMechanics::magicConcealmentTarget(victim);
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            return detected;
        };
        const auto crimeWitnesses = [&](size_t index, bool includeSelectedOutsideRadius,
            const auto& observe) {
            const auto* player = players.findPlayer(mBinding.mPlayers[index]);
            if (!player) return;
            const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
            const float radius = settings.find("fAlarmRadius")->mValue.getFloat();
            if (!std::isfinite(radius) || radius < 0.f || radius > 1e7f)
                throw std::invalid_argument("Native crime alarm radius invalid");
            const auto position = player->transform().position();
            const std::array<float, 3> target{float(double(position.x()) / 1024),
                float(double(position.y()) / 1024), float(double(position.z()) / 1024) + 64.f};
            for (const auto& witness : mBinding.mCrimeWitnesses)
            {
                if (witness.cell != player->transform().cell()) continue;
                const bool selected = witness.placement == before.mActor;
                const auto neighbors = mBinding.mNeighborAi
                    ? mBinding.mNavigatingActor->neighborSnapshots() : std::vector<ActorSceneSnapshot>{};
                const auto neighbor = std::ranges::find(neighbors, witness.placement, &ActorSceneSnapshot::mActor);
                const bool simulatedNeighbor = neighbor != neighbors.end();
                if (simulatedNeighbor && mBinding.mNeighborCombat)
                {
                    const size_t adjacent = size_t(neighbor - neighbors.begin());
                    if (neighborLives[adjacent].respawnTick || combat->actors[adjacent + 3][8][2] <= 0.f)
                        continue;
                }
                if (selected && (life->respawnTick || combat->knockedDown[2]
                    || combat->actors[2][8][2] <= 0.f)) continue;
                const auto* npc = mRuntime.mStore.get<ESM::NPC>().search(witness.base);
                if (!npc) continue;
                const std::array<float, 3> from{selected ? before.mPosition[0]
                        : simulatedNeighbor ? neighbor->mPosition[0]
                        : float(double(witness.position.x()) / 1024),
                    selected ? before.mPosition[1] : simulatedNeighbor ? neighbor->mPosition[1]
                        : float(double(witness.position.y()) / 1024),
                    (selected ? before.mPosition[2]
                        : simulatedNeighbor ? neighbor->mPosition[2]
                        : float(double(witness.position.z()) / 1024)) + 64.f};
                float distance2 = 0.f;
                for (size_t axis = 0; axis < 3; ++axis)
                    distance2 += (target[axis] - from[axis]) * (target[axis] - from[axis]);
                if (distance2 > radius * radius && !(selected && includeSelectedOutsideRadius)) continue;
                if (!mBinding.mNavigatingActor->lineOfSight(from, target) || !npcDetects(index)) continue;
                observe(witness, *npc, selected, std::sqrt(distance2));
            }
        };
        if (mBinding.mSocialLifecycle && combat)
            for (size_t index = 0; index < transformedNow.size(); ++index)
            {
                if (!transformedNow[index]) continue;
                bool detected = false, reported = false;
                crimeWitnesses(index, false, [&](const auto&, const ESM::NPC& npc, bool, float) {
                    detected = true;
                    reported |= npc.mAiData.mAlarm > 0;
                });
                if (!detected) continue;
                auto& social = combat->players[index];
                social.knownWerewolf = true;
                if (reported)
                {
                    const int bounty = mRuntime.mStore.get<ESM::GameSetting>()
                        .find("iWereWolfBounty")->mValue.getInteger();
                    if (bounty < 0 || bounty > 10'000'000 - social.bounty)
                        throw std::invalid_argument("Native werewolf bounty outside bounds");
                    social.bounty += bounty;
                }
            }
        const auto breakInvisibility = [&](size_t index) {
            const auto previous = timedEffects;
            const auto invisibility = uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::Invisibility));
            std::erase_if(timedEffects, [&](const auto& effect) {
                return effect.actor == index && effect.sourceKind != 3 && effect.effectIndex == invisibility;
            });
            // Stock purges the applied equipment effect while retaining its source
            // spell. It cannot reapply until that item is unequipped and equipped.
            for (auto& effect : timedEffects)
                if (effect.actor == index && effect.sourceKind == 3 && effect.effectIndex == invisibility)
                    effect.magnitude = 0.f;
            if (timedEffects != previous)
                updateResources(index, previous, timedEffects, mBinding.mKnockoutAnimation);
        };
        const auto retaliate = [&](MWMechanics::NpcStats& attacker, const MWMechanics::NpcStats& victim,
            size_t attackerIndex, size_t victimIndex, Misc::Rng::Generator& rng) {
            if (!mBinding.mExpandedEffects) return;
            float destruction = attacker.getSkill(ESM::Skill::Destruction).getModified();
            if (attackerIndex >= 2)
            {
                const auto ptr = mRuntime.ownerPtr(combatOwner(attackerIndex));
                if (ptr.getType() == ESM::Creature::sRecordId) destruction = ptr.get<ESM::Creature>()->mBase->mData.mMagic;
            }
            const bool alive = attacker.getHealth().getCurrent() > 0;
            const MWWorld::TimeStamp deathTime{};
            for (const auto damage : MWMechanics::elementalShieldDamage(mRuntime.mStore, attacker, destruction, victim, rng))
                if (damage)
                {
                    if (!std::isfinite(*damage) || *damage < 0 || *damage > 1'000'000)
                        throw std::invalid_argument("Native elemental shield damage invalid");
                    MWMechanics::adjustDynamicStatValue(attacker, 0, -*damage, false, false, &deathTime);
                }
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            if (attackerIndex >= 2 && alive && attacker.getHealth().getCurrent() <= 0)
                recordNpcDeath(attackerIndex, magicCaster(victimIndex).identity);
        };
        if (combat && hasParalysis(timedEffects, 2))
        { step.reset(); after = before; melee = mIdleMelee; target = 0; contact = false; }
        if (combat && mBinding.mKnockoutRules)
        {
            for (size_t index = 0; index < combat->actors.size(); ++index)
            {
                const bool awake = index == 2 ? active : index > 2
                    ? std::ranges::any_of(players.activeSessions(), [&](const auto& session) {
                        const auto* player = players.findPlayer(session.playerId());
                        return player && player->transform().cell() == actorCell(before);
                    }) : std::ranges::any_of(players.activeSessions(), [&](const auto& session) {
                        return session.playerId() == mBinding.mPlayers[index];
                    });
                if (!awake || combat->actors[index][8][2] <= 0) continue;
                if (mBinding.mMeleeDefenseRules && combat->hitRecoveryTicks[index])
                    --combat->hitRecoveryTicks[index];
                auto stats = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
                MWMechanics::restoreCombatFatigue(stats, mRuntime.mStore, seconds);
                if (mBinding.mKnockoutAnimation && combat->knockedDown[index] && !hasParalysis(timedEffects, index))
                {
                    const auto& bound = (*mBinding.mBoundHits)[index];
                    const auto& clip = combat->hitKnockdown[index] ? bound.knockdown : bound.knockout;
                    auto& frame = combat->knockoutFrame[index];
                    frame = clip.advance(frame, !combat->hitKnockdown[index] && fatigueKnockout(stats, mBinding.mKnockoutAnimation));
                    if (frame == clip.stop)
                    { frame = 0; combat->knockedDown[index] = false; combat->hitKnockdown[index] = false; }
                }
                combat->knockedDown[index] = stats.getHealth().getCurrent() > 0
                    && (fatigueKnockout(stats, mBinding.mKnockoutAnimation)
                        || (mBinding.mKnockoutAnimation && combat->knockedDown[index]));
                saveCombatStats(combat->actors[index], stats, timedEffects, index);
            }
            if (combat->knockedDown[2] || hasParalysis(timedEffects, 2))
            {
                step.reset(); after = before; report.status = Diagnostics::Status::Idle;
                melee = mIdleMelee;
                target = 0; contact = false;
            }
        }
        const uint64_t elapsedTicks = tick.value() - mActorTick;
        for (auto& effect : timedEffects)
        {
            const auto id = effect.effectIndex || effect.source
                ? ESM::MagicEffect::indexToRefId(int(effect.effectIndex))
                : ESM::MagicEffect::ResistMagicka;
            if ((effect.sourceKind == 3 && id != ESM::MagicEffect::DisintegrateWeapon
                    && id != ESM::MagicEffect::DisintegrateArmor) || effect.sourceKind == 5)
                continue; // Equipped and authored passive sources are timeless overlays.
            bool paused = effect.actor >= 2 && !active;
            if (effect.actor < mBinding.mPlayers.size())
                paused = std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                    return session.playerId() == mBinding.mPlayers[size_t(effect.actor)];
                });
            if (paused)
            {
                if (effect.sourceKind == 3 || effect.sourceKind == 4) continue;
                if (effect.expiresTick > UINT64_MAX - elapsedTicks)
                    throw std::invalid_argument("Paused native timed effect deadline exhausted");
                effect.expiresTick += elapsedTicks;
            }
            else if (mBinding.mActorEffectLifecycle && combat && (effect.effectIndex || effect.source))
            {
                const auto* source = effect.sourceKind == 4 ? spellBySource(mRuntime.mStore, effect.source) : nullptr;
                const bool corprusOnce = mBinding.mSpecialConditions && source
                    && MWMechanics::Spells::hasCorprusEffect(source)
                    && (mRuntime.mStore.get<ESM::MagicEffect>().find(id)->mData.mFlags & ESM::MagicEffect::AppliedOnce);
                if (!corprusOnce && (timedDamage(id) || timedRestore(id) || absorbDynamic(id) >= 0
                    || permanentStatEffect(id) || id == ESM::MagicEffect::DisintegrateWeapon
                    || id == ESM::MagicEffect::DisintegrateArmor)
                    && combat->actors[size_t(effect.actor)][8][2] > 0)
                {
                    const uint64_t from = std::max(mActorTick, effect.startTick);
                    const uint64_t until = std::min(tick.value(), effect.expiresTick);
                    if (until > from)
                    {
                        auto victim = loadCombatStats(mRuntime.mStore, combat->actors[size_t(effect.actor)],
                            timedEffects, size_t(effect.actor));
                        const int stat = absorbDynamic(id) >= 0 ? absorbDynamic(id) : id == ESM::MagicEffect::DamageMagicka
                                || id == ESM::MagicEffect::RestoreMagicka ? 1
                            : id == ESM::MagicEffect::DamageFatigue
                                || id == ESM::MagicEffect::RestoreFatigue ? 2 : 0;
                        const float amount = effect.magnitude * float(until - from) / 30.f
                            * (id == ESM::MagicEffect::SunDamage ? sunExposure(size_t(effect.actor)) : 1.f);
                        if (id == ESM::MagicEffect::DisintegrateWeapon
                            || id == ESM::MagicEffect::DisintegrateArmor)
                            stageDisintegrate(size_t(effect.actor), id, amount);
                        else if (permanentStatEffect(id))
                        {
                            ESM::ENAMstruct entry{}; entry.mEffectID = id;
                            if (effect.argument <= 8) entry.mAttribute = ESM::Attribute::indexToRefId(int(effect.argument) - 1);
                            else entry.mSkill = ESM::Skill::indexToRefId(int(effect.argument) - 9);
                            applyPermanentStatEffect(victim, entry, amount, mRuntime.mStore);
                        }
                        else if (absorbDynamic(id) >= 0)
                        {
                            const auto recipient = effect.beneficiary ? size_t(effect.beneficiary - 1) : combat->actors.size();
                            const bool available = recipient < combat->actors.size() && combat->actors[recipient][8][2] > 0
                                && (recipient >= 2 ? active : std::ranges::any_of(players.activeSessions(),
                                    [&](const auto& session) { return session.playerId() == mBinding.mPlayers[recipient]; }));
                            const MWWorld::TimeStamp deathTime{};
                            if (available && recipient != effect.actor)
                            {
                                auto caster = loadCombatStats(mRuntime.mStore, combat->actors[recipient], timedEffects, recipient);
                                MWMechanics::absorbDynamicStat(victim, &caster, stat, amount, &deathTime);
                                saveCombatStats(combat->actors[recipient], caster, timedEffects, recipient);
                            }
                            else MWMechanics::absorbDynamicStat(victim, available ? &victim : nullptr, stat, amount, &deathTime);
                        }
                        else if (timedRestore(id))
                        {
                            if (stat == 0) MWMechanics::restoreHealth(victim, amount);
                            else MWMechanics::restoreDynamicStat(victim, stat, amount);
                        }
                        else
                        {
                            // The composed life image owns the authoritative death tick.
                            const MWWorld::TimeStamp deathTime{};
                            MWMechanics::adjustDynamicStatValue(victim, stat, -amount,
                                stat == 2 && mBinding.mUncappedDamageFatigue, false, &deathTime);
                        }
                        saveCombatStats(combat->actors[size_t(effect.actor)], victim, timedEffects,
                            size_t(effect.actor));
                        if (mBinding.mKnockoutRules)
                            combat->knockedDown[size_t(effect.actor)] = victim.getHealth().getCurrent() > 0
                                && (fatigueKnockout(victim, mBinding.mKnockoutAnimation)
                                    || (mBinding.mKnockoutAnimation && combat->knockedDown[size_t(effect.actor)]));
                        if (effect.actor >= 2 && victim.getHealth().getCurrent() <= 0)
                            recordNpcDeath(size_t(effect.actor),
                                {effect.caster, effect.casterKind, effect.casterLife});
                    }
                }
            }
        }
        const auto beforeExpiry = timedEffects;
        std::erase_if(timedEffects, [tick](const auto& effect) { return effect.expiresTick <= tick.value(); });
        if (combat && mBinding.mNpcCastLifecycle)
            for (size_t actor = 0; actor < (mBinding.mNeighborCombat ? combat->actors.size() : 3); ++actor)
                updateResources(actor, beforeExpiry, timedEffects, mBinding.mKnockoutAnimation);
        if (mBinding.mConstantEffects && combat)
        {
            for (size_t actorIndex = 0; actorIndex < (mBinding.mNeighborCombat ? combat->actors.size() : 3); ++actorIndex)
            {
                const size_t owner = combatOwner(actorIndex);
                auto values = combatEquipmentValues(owner, command.get());
                for (const auto& change : wear)
                    if (change.owner == owner && !change.condition)
                        values.mSlots[change.slot] = {};
                Misc::Rng::Generator rng{combat->rng};
                const auto previous = timedEffects;
                if (reconcileConstants(values, actorIndex, magicCaster(actorIndex).identity, tick.value(), mRuntime.mStore,
                        mBinding.mGeneralConstants, timedEffects, &rng, mBinding.mExpandedEffects,
                        mBinding.mSpecialConditions, mBinding.mMovementEffects,
                        mBinding.mAiDecisions, actorIndex >= 2 && (actorIndex == 2 ? aiNpc : true),
                        actorIndex == 2 && aiUndead, levitationEnabled))
                    updateResources(actorIndex, previous, timedEffects, mBinding.mKnockoutAnimation);
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
        }
        if ((mBinding.mAiDecisions || mBinding.mMovementEffects) && combat)
            for (size_t actorIndex = 0; actorIndex < (mBinding.mNeighborCombat ? combat->actors.size() : 3); ++actorIndex)
            {
                Misc::Rng::Generator rng{combat->rng};
                const auto previous = timedEffects;
                if (reconcilePassiveActorEffects(mRuntime.ownerPtr(combatOwner(actorIndex)),
                        actorIndex, magicCaster(actorIndex).identity, tick.value(), mRuntime.mStore,
                        timedEffects, &rng, mBinding.mMovementEffects, mBinding.mAiDecisions,
                        actorIndex >= 2 && (actorIndex == 2 ? aiNpc : true),
                        actorIndex == 2 && aiUndead, levitationEnabled))
                    updateResources(actorIndex, previous, timedEffects, mBinding.mKnockoutAnimation);
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
        const auto acquireCondition = [&](size_t index, const ESM::Spell& spell, Misc::Rng::Generator& rng,
            bool authored, MWMechanics::NpcStats* contactStats = nullptr) {
            ActorCampaignCombat::ConditionSource member{index, spellRecordId(spell.mId)};
            if (std::ranges::any_of(combat->conditions, [&](const auto& condition) {
                    return sameCondition(condition, index, member.source);
                })) return;
            const auto plan = preparePersistentEffects(spell, mRuntime.mStore, mBinding.mSpecialConditions);
            if (!plan || !member.source || spellBySource(mRuntime.mStore, member.source) != &spell)
                throw std::invalid_argument("Native persistent condition unsupported");
            if (combat->conditions.size() >= ActorCampaignCombat::MaximumConditionSources)
                throw std::invalid_argument("Native condition source capacity exhausted");
            if (mBinding.mSpecialConditions && MWMechanics::Spells::hasCorprusEffect(&spell))
            {
                if (gameNowMs > UINT64_MAX - 86400000)
                    throw std::invalid_argument("Native Corprus deadline exhausted");
                member.nextWorseningMs = gameNowMs + 86400000;
                member.lastObservedMs = gameNowMs;
            }
            auto stats = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
            addTimedResistance(stats, timedEffects, index);
            auto& victim = contactStats ? *contactStats : stats;
            std::vector<ActorCasterIdentity> identities{{mBinding.mPlayers[0].value(), 1, 1},
                {mBinding.mPlayers[1].value(), 1, 1},
                {mBinding.mNavigatingActor->actorId(), 2, life->generation}};
            for (size_t i = 0; i + 3 < combat->actors.size(); ++i)
                identities.push_back({combat->npcPlacements.at(i + 1), 2,
                    i < neighborLives.size() ? neighborLives[i].generation : 1});
            combat->conditions.push_back(member);
            const auto outcome = resolveExpandedEffects(*plan, ESM::RT_Self, index, victim, identities[index], member.source,
                4, tick.value(), *combat, timedEffects, identities, mRuntime.mStore, rng,
                mBinding.mUncappedDamageFatigue, mBinding.mClassicReflectedAbsorb,
                actorKinds, {}, nullptr, authored, sunExposure, stageDisintegrate);
            if (member.nextWorseningMs && std::ranges::none_of(timedEffects, [&](const auto& effect) {
                    return effect.actor == index && effect.sourceKind == 4 && effect.source == member.source
                        && effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::Corprus));
                }))
            {
                auto& stored = combat->conditions.back();
                stored.nextWorseningMs = stored.lastObservedMs = 0;
            }
            for (size_t i = 2; i < outcome.deaths.size(); ++i)
                if (outcome.deaths[i]) recordNpcDeath(i, *outcome.deaths[i]);
        };
        const auto initializeConditions = [&](size_t index) {
            if (!mBinding.mPersistentConditions) return;
            const auto ptr = mRuntime.ownerPtr(combatOwner(index));
            Misc::Rng::Generator rng{combat->rng};
            for (const auto id : actorSpells(ptr).mList)
            {
                const auto* spell = mRuntime.mStore.get<ESM::Spell>().find(id);
                if (spell->mData.mType == ESM::Spell::ST_Disease || spell->mData.mType == ESM::Spell::ST_Blight
                    || spell->mData.mType == ESM::Spell::ST_Curse)
                    acquireCondition(index, *spell, rng, true);
            }
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
        };
        if (mBinding.mPersistentConditions && !mActorTick)
            for (size_t index = 0; index < (mBinding.mNeighborCombat ? combat->actors.size() : 3); ++index)
                initializeConditions(index);
        const auto diseaseContact = [&](size_t index, MWMechanics::NpcStats& stats, Misc::Rng::Generator& rng) {
            if (!mBinding.mPersistentConditions || index == 2 || stats.getHealth().getCurrent() <= 0) return;
            const float chance = mRuntime.mStore.get<ESM::GameSetting>().find("fDiseaseXferChance")->mValue.getFloat();
            if (!std::isfinite(chance) || chance < 0 || chance > 100)
                throw std::invalid_argument("Native disease transfer chance invalid");
            const auto sources = combat->conditions;
            for (const auto& carrier : sources)
            {
                if (carrier.actor != 2 || std::ranges::any_of(combat->conditions, [&](const auto& condition) {
                        return sameCondition(condition, index, carrier.source);
                    })) continue;
                const auto* spell = spellBySource(mRuntime.mStore, carrier.source);
                const auto multiplier = MWMechanics::getDiseaseContactMultiplier(*spell, stats.getMagicEffects());
                if (!multiplier) continue;
                const float threshold = chance * 100.f * *multiplier;
                if (!std::isfinite(threshold) || threshold < float(INT_MIN) || threshold >= float(INT_MAX))
                    throw std::invalid_argument("Native disease transfer threshold invalid");
                if (Misc::Rng::rollDice(10000, rng) < int(threshold)) acquireCondition(index, *spell, rng, false, &stats);
            }
        };
        if (mBinding.mSpecialConditions)
            for (auto& condition : combat->conditions)
            {
                if (!condition.nextWorseningMs) continue; // Cured sources retain membership.
                const auto* source = spellBySource(mRuntime.mStore, condition.source);
                if (!source || !MWMechanics::Spells::hasCorprusEffect(source)
                    || gameNowMs < condition.lastObservedMs)
                    throw std::invalid_argument("Native Corprus clock or source invalid");
                const bool awake = condition.actor >= 2 ? active
                    : std::ranges::any_of(players.activeSessions(), [&](const auto& session) {
                        return session.playerId() == mBinding.mPlayers[size_t(condition.actor)];
                    });
                if (!awake)
                {
                    const uint64_t elapsed = gameNowMs - condition.lastObservedMs;
                    if (condition.nextWorseningMs > UINT64_MAX - elapsed)
                        throw std::invalid_argument("Paused Corprus deadline exhausted");
                    condition.nextWorseningMs += elapsed;
                }
                else
                {
                    const auto marker = std::ranges::find_if(timedEffects, [&](const auto& effect) {
                        return effect.actor == condition.actor && effect.sourceKind == 4
                            && effect.source == condition.source
                            && effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::Corprus));
                    });
                    if (marker == timedEffects.end())
                        throw std::invalid_argument("Active Corprus source lacks its marker");
                    uint64_t steps = 0;
                    while (gameNowMs >= condition.nextWorseningMs)
                    {
                        if (++steps > 32 || condition.worsenings >= 100000
                            || condition.nextWorseningMs > UINT64_MAX - 86400000)
                            throw std::invalid_argument("Native Corprus worsening bound exhausted");
                        auto victim = loadCombatStats(mRuntime.mStore,
                            combat->actors[size_t(condition.actor)], timedEffects, size_t(condition.actor));
                        for (const auto& effect : timedEffects)
                            if (effect.actor == condition.actor && effect.sourceKind == 4
                                && effect.source == condition.source
                                && effect.effectIndex != marker->effectIndex)
                            {
                                const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
                                const auto* magic = mRuntime.mStore.get<ESM::MagicEffect>().find(id);
                                if (magic->mData.mFlags & ESM::MagicEffect::AppliedOnce)
                                {
                                    if (corprusStatOverlay(id))
                                        applyEffectStats(victim, mRuntime.mStore, {&effect, 1},
                                            size_t(condition.actor), 1.f);
                                    applyCorprusOnce(victim, effect, mRuntime.mStore, mBinding.mUncappedDamageFatigue);
                                }
                            }
                        saveCombatStats(combat->actors[size_t(condition.actor)], victim, timedEffects,
                            size_t(condition.actor));
                        if (condition.actor == 2 && victim.getHealth().getCurrent() <= 0)
                            recordEffectDeath({marker->caster, marker->casterKind, marker->casterLife});
                        ++condition.worsenings;
                        condition.nextWorseningMs += 86400000;
                    }
                }
                condition.lastObservedMs = gameNowMs;
            }
        std::vector<MeleeCombatEvent> playerHits;
        std::vector<ActorMeleeCombatEvent> actorHits;
        std::vector<MagicUseCombatEvent> spellCasts;
        EquipmentBytes wornCore;
        std::vector<MagicImpactCue> magicImpactCues;
        const auto applyStrike = [&](MagicCasterContext context, const EquipmentRuntime::EquippedWeaponCondition& held,
            const ESM::Weapon& weapon, MWMechanics::NpcStats& attacker, MWMechanics::NpcStats& victim,
            size_t victimIndex, Misc::Rng::Generator& rng) {
            if (weapon.mEnchant.empty()) return;
            const auto* enchantment = mRuntime.mStore.get<ESM::Enchantment>().search(weapon.mEnchant);
            if (!enchantment || enchantment->mData.mType != ESM::Enchantment::WhenStrikes) return;
            const auto values = mRuntime.installedValues(context.inventoryOwner);
            const auto item = std::ranges::find(values.mObjects, held.mItem,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == values.mObjects.end())
                throw std::invalid_argument("Native strike weapon identity missing");
            const float beforeCharge = item->mRef.mEnchantmentCharge;
            const auto prepared = prepareEnchantmentCast(*enchantment, attacker, beforeCharge,
                mRuntime.mStore, mBinding.mActorEffectLifecycle, mBinding.mExpandedEffects,
                mBinding.mSpecialConditions, mBinding.mMovementEffects);
            if (!prepared || std::ranges::any_of(prepared->effects.effects,
                    [&](const auto& effect) { return effect.mArea != 0
                        || (!mBinding.mNpcCastLifecycle && (!effect.mAttribute.empty() || !effect.mSkill.empty())); }))
                throw std::invalid_argument("Native strike enchantment source invalid");
            if (!prepared->affordable) return;
            charges.push_back(ItemCharge{context.inventoryOwner, held.mItem, beforeCharge, prepared->chargeAfter});
            const uint64_t sourceId = spellRecordId(weapon.mEnchant);
            if (!sourceId || enchantmentBySource(mRuntime.mStore, sourceId) != enchantment)
                throw std::invalid_argument("Native strike enchantment source collision");
            resolveEffects(prepared->effects, ESM::RT_Self, context.combatIndex, attacker, tick.value(), context.identity,
                sourceId, 2, rng, mRuntime.mStore, timedEffects, mBinding.mActorEffectLifecycle,
                mBinding.mUncappedDamageFatigue, {}, &attacker);
            resolveEffects(prepared->effects, ESM::RT_Touch, victimIndex, victim, tick.value(), context.identity,
                sourceId, 2, rng, mRuntime.mStore, timedEffects, mBinding.mActorEffectLifecycle,
                mBinding.mUncappedDamageFatigue, {}, &attacker);
            resolveEffects(prepared->effects, ESM::RT_Target, victimIndex, victim, tick.value(), context.identity,
                sourceId, 2, rng, mRuntime.mStore, timedEffects, mBinding.mActorEffectLifecycle,
                mBinding.mUncappedDamageFatigue, {}, &attacker);
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
        };
        const auto victimLife = [&](size_t index) -> ActorCampaignLife& {
            return index == 2 ? *life : neighborLives.at(index - 3);
        };
        const auto victimScene = [&](size_t index) -> ActorSceneSnapshot {
            return index == 2 ? after : mBinding.mNavigatingActor->neighborSnapshots().at(index - 3);
        };
        const auto distanceSquared = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
            float distance = 0.f;
            for (size_t axis = 0; axis < 3; ++axis) distance += (a[axis] - b[axis]) * (a[axis] - b[axis]);
            return distance;
        };
        const auto applyProjectileStrike = [&](ActorCasterIdentity caster, uint64_t command, size_t casterIndex,
            const ESM::Weapon& ammunition, const std::array<float, 3>& impact,
            MWMechanics::NpcStats& attacker, MWMechanics::NpcStats& victim, size_t victimIndex,
            Misc::Rng::Generator& rng) {
            if (ammunition.mEnchant.empty()) return;
            const auto* enchantment = mRuntime.mStore.get<ESM::Enchantment>().search(ammunition.mEnchant);
            const auto effects = enchantment && enchantment->mData.mType == ESM::Enchantment::WhenStrikes
                ? prepareInstantEffects(enchantment->mEffects, mRuntime.mStore, mBinding.mActorEffectLifecycle,
                    mBinding.mExpandedEffects, false, mBinding.mSpecialConditions, mBinding.mMovementEffects)
                : std::nullopt;
            const uint64_t source = spellRecordId(ammunition.mEnchant);
            if (!effects || !source || enchantmentBySource(mRuntime.mStore, source) != enchantment)
                throw std::invalid_argument("Native projectile enchantment source invalid");
            for (const int range : {ESM::RT_Self, ESM::RT_Touch, ESM::RT_Target})
                if (std::ranges::any_of(effects->effects, [&](const auto& effect) {
                        return effect.mRange == range && (range == ESM::RT_Target || effect.mArea > 0);
                    }))
                {
                    if (magicImpactCues.size() >= MaximumReplicatedMagicImpactCues)
                        throw std::invalid_argument("Native magic visual cue capacity exhausted");
                    magicImpactCues.push_back({uint8_t(caster.kind == 2 ? 2 : 1),
                        uint8_t(MagicUseSourceKind::EnchantedItem), caster.id,
                        std::max<uint64_t>(1, caster.life), std::max<uint64_t>(1, command),
                        magicVisualRecord(uint64_t(MagicUseSourceKind::EnchantedItem), source),
                        impact, uint8_t(range)});
                }
            // OpenMW casts projectile Self effects on the impact victim. Projectile
            // WhenStrikes ignores charge, and the consumed item is never recovered.
            for (const int range : {ESM::RT_Self, ESM::RT_Touch, ESM::RT_Target})
                for (size_t index = 0; index < combat->actors.size(); ++index)
                {
                    if (index != victimIndex && combat->actors[index][8][2] <= 0) continue;
                    std::array<float, 3> position{};
                    if (index != victimIndex)
                    {
                        if (index < 2)
                        {
                            const auto* player = players.findPlayer(mBinding.mPlayers[index]);
                            if (!player || player->transform().cell() != actorCell(before)
                                || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                                    return session.playerId() == player->playerId();
                                })) continue;
                            const auto place = player->transform().position();
                            position = {float(double(place.x()) / 1024), float(double(place.y()) / 1024),
                                float(double(place.z()) / 1024) + 64.f};
                        }
                        else
                        {
                            if (victimLife(index).respawnTick) continue;
                            const auto scene = victimScene(index);
                            position = {scene.mPosition[0], scene.mPosition[1], scene.mPosition[2] + 55.f};
                        }
                    }
                    PreparedInstantEffects selected;
                    std::vector<uint64_t> ordinals;
                    for (size_t ordinal = 0; ordinal < effects->effects.size(); ++ordinal)
                    {
                        const auto& effect = effects->effects[ordinal];
                        if (effect.mRange != range || (index != victimIndex
                                && (!effect.mArea || distanceSquared(position, impact)
                                    > std::pow(effect.mArea * 22.f, 2)))) continue;
                        selected.effects.push_back(effect);
                        ordinals.push_back(ordinal);
                    }
                    if (selected.effects.empty()) continue;
                    auto other = index != victimIndex && index != casterIndex
                        ? std::optional(loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index))
                        : std::nullopt;
                    auto& targetStats = index == victimIndex ? victim
                        : index == casterIndex ? attacker : *other;
                    if (other) addTimedResistance(targetStats, timedEffects, index);
                    resolveEffects(selected, range, index, targetStats, tick.value(), caster,
                        source, 2, rng, mRuntime.mStore, timedEffects, mBinding.mActorEffectLifecycle,
                        mBinding.mUncappedDamageFatigue, ordinals, &attacker);
                    if (other) saveCombatStats(combat->actors[index], targetStats, timedEffects, index);
                    if (index >= 2 && targetStats.getHealth().getCurrent() <= 0)
                        recordNpcDeath(index, caster);
                }
        };
        const auto& gmst = mRuntime.mStore.get<ESM::GameSetting>();
        const auto armorCondition = [&](size_t owner, int slot) {
            if (owner < 2 && changingWerewolfEquipment[owner])
                return std::optional<EquipmentRuntime::EquippedWeaponCondition>{};
            auto result = mRuntime.equippedArmorCondition(owner, slot);
            if (result && mBinding.mPlayerMelee[0])
                for (const auto& change : wear)
                    if (change.owner == owner && change.slot == slot)
                    {
                        result->mCondition = change.condition;
                        if (!change.condition) result.reset();
                        break;
                    }
            return result;
        };
        const auto wearArmor = [&](size_t owner, int slot, int condition) {
            if (mBinding.mPlayerMelee[0])
                for (auto& change : wear)
                    if (change.owner == owner && change.slot == slot)
                    { change.condition = condition; return; }
            wear.push_back({owner, *mRuntime.equippedArmorCondition(owner, slot), condition, slot});
        };
        const auto armorRating = [&](size_t owner, const MWMechanics::NpcStats& stats) {
            if (mRuntime.ownerPtr(owner).getType() == ESM::Creature::sRecordId)
                return stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Shield).getMagnitude();
            const auto* inventory = mRuntime.inventoryStorage(owner);
            if (!inventory) throw std::invalid_argument("Native armor defender has no inventory");
            const float skill = stats.getSkill(ESM::Skill::Unarmored).getModified();
            const float unarmored = gmst.find("fUnarmoredBase1")->mValue.getFloat() * skill
                * gmst.find("fUnarmoredBase2")->mValue.getFloat() * skill;
            constexpr std::array slots{
                std::pair{MWWorld::InventoryStore::Slot_Cuirass, .30f},
                std::pair{MWWorld::InventoryStore::Slot_CarriedLeft, .10f},
                std::pair{MWWorld::InventoryStore::Slot_Helmet, .10f},
                std::pair{MWWorld::InventoryStore::Slot_Greaves, .10f},
                std::pair{MWWorld::InventoryStore::Slot_Boots, .10f},
                std::pair{MWWorld::InventoryStore::Slot_LeftPauldron, .10f},
                std::pair{MWWorld::InventoryStore::Slot_RightPauldron, .10f},
                std::pair{MWWorld::InventoryStore::Slot_LeftGauntlet, .05f},
                std::pair{MWWorld::InventoryStore::Slot_RightGauntlet, .05f}};
            float rating = stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Shield).getMagnitude();
            for (const auto& [slot, weight] : slots)
            {
                float part = unarmored;
                const auto selected = inventory->getSlot(slot);
                if (selected != inventory->end() && selected->getType() == ESM::Armor::sRecordId
                    && (!mBinding.mPlayerMelee[0] || armorCondition(owner, slot)))
                {
                    const auto item = *selected;
                    const auto& armor = static_cast<const MWClass::Armor&>(item.getClass());
                    part = armor.getSkillAdjustedArmorRating(item,
                        stats.getSkill(armor.getEquipmentSkill(item, mRuntime.mStore)).getModified(),
                        mRuntime.mStore);
                    if (item.getClass().hasItemHealth(item))
                        part *= mBinding.mPlayerMelee[0]
                            ? float(armorCondition(owner, slot)->mCondition) / std::max(1, item.getClass().getItemMaxHealth(item))
                            : item.getClass().getItemNormalizedHealth(item);
                }
                rating += part * weight;
            }
            if (!std::isfinite(rating) || rating < 0 || rating > 1'000'000)
                throw std::invalid_argument("Native armor rating invalid");
            return rating;
        };
        const auto defendHit = [&](size_t defender, MWMechanics::NpcStats& victim,
            const MWMechanics::NpcStats& attacker, const ESM::Weapon* weapon, float strength,
            float attackerWeight, float attackerSkill, const std::array<float, 3>& attackerPosition,
            const std::array<float, 3>& defenderPosition, float defenderYaw, bool movingForward,
            float& damage, Misc::Rng::Generator& rng, bool projectile = false) {
            if (!std::isfinite(damage) || damage < 0 || damage > 1'000'000)
                throw std::invalid_argument("Native melee damage invalid before defense");
            if (MWMechanics::isNormalWeapon(weapon, mBinding.mEnchantedWeaponsAreMagical))
                damage = MWMechanics::applyNormalWeaponResistance(victim, damage);
            if (!std::isfinite(damage) || damage < 0 || damage > 1'000'000)
                throw std::invalid_argument("Native resisted melee damage invalid");
            bool blocked = false;
            const auto owner = defender == 2 ? mCombatNpcOwner : defender;
            const auto shield = armorCondition(owner, MWWorld::InventoryStore::Slot_CarriedLeft);
            // CharacterController::isReadyToBlock requires carried-left to be
            // visible. The loaded actor has an animation; a two-handed weapon
            // hides the shield in OpenMW's NpcAnimation.
            bool readyToBlock = true;
            if (mBinding.mMeleeDefenseRules)
            {
                const auto* inventory = mRuntime.inventoryStorage(owner);
                if (!inventory) throw std::invalid_argument("Native block defender has no inventory");
                const auto right = inventory->getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                if (right != inventory->end() && right->getType() == ESM::Weapon::sRecordId)
                {
                    const auto& equipped = mRuntime.mStore.get<ESM::Weapon>().find(right->getCellRef().getRefId())->mData;
                    readyToBlock = carriedLeftVisibleForWeapon(equipped.mType);
                }
            }
            if (!projectile && shield && shield->mCondition > 0 && !victim.getKnockedDown()
                && readyToBlock && (!mBinding.mMeleeDefenseRules || !combat->hitRecoveryTicks[defender])
                && victim.getMagicEffects().getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() <= 0)
            {
                const float dx = attackerPosition[0] - defenderPosition[0];
                const float dy = attackerPosition[1] - defenderPosition[1];
                const float forwardX = -std::sin(defenderYaw), forwardY = std::cos(defenderYaw);
                const float angle = std::atan2(dx * forwardY - dy * forwardX,
                    dx * forwardX + dy * forwardY) * 180.f / std::numbers::pi_v<float>;
                if (angle >= gmst.find("fCombatBlockLeftAngle")->mValue.getFloat()
                    && angle <= gmst.find("fCombatBlockRightAngle")->mValue.getFloat())
                {
                    TES3MP::OpenMwMeleeSettings settings;
                    settings.swingBlockMultiplier = gmst.find("fSwingBlockMult")->mValue.getFloat();
                    settings.swingBlockBase = gmst.find("fSwingBlockBase")->mValue.getFloat();
                    settings.blockStillBonus = gmst.find("fBlockStillBonus")->mValue.getFloat();
                    settings.blockMinimumChance = float(gmst.find("iBlockMinChance")->mValue.getInteger());
                    settings.blockMaximumChance = float(gmst.find("iBlockMaxChance")->mValue.getInteger());
                    TES3MP::OpenMwMeleeAttacker blockerValues, attackerValues;
                    blockerValues.agility = victim.getAttribute(ESM::Attribute::Agility).getModified();
                    blockerValues.luck = victim.getAttribute(ESM::Attribute::Luck).getModified();
                    blockerValues.fatigueTerm = victim.getFatigueTerm(mRuntime.mStore);
                    attackerValues.agility = attacker.getAttribute(ESM::Attribute::Agility).getModified();
                    attackerValues.luck = attacker.getAttribute(ESM::Attribute::Luck).getModified();
                    attackerValues.weaponSkill = attackerSkill;
                    attackerValues.fatigueTerm = attacker.getFatigueTerm(mRuntime.mStore);
                    const float chance = TES3MP::openMwMeleeBlockChance(settings,
                        victim.getSkill(ESM::Skill::Block).getModified(), blockerValues, attackerValues,
                        strength, !movingForward);
                    blocked = Misc::Rng::roll0to99(rng) < chance;
                    if (blocked)
                    {
                        const int shieldLoss = std::min(shield->mCondition, int(damage));
                        if (shieldLoss)
                            wearArmor(owner, MWWorld::InventoryStore::Slot_CarriedLeft, shield->mCondition - shieldLoss);
                        const float capacity = victim.getAttribute(ESM::Attribute::Strength).getModified()
                            * gmst.find("fEncumbranceStrMult")->mValue.getFloat();
                        const float weight = std::max(0.f, mRuntime.storage(owner).getWeight());
                        settings.fatigueBlockBase = gmst.find("fFatigueBlockBase")->mValue.getFloat();
                        settings.fatigueBlockMultiplier = gmst.find("fFatigueBlockMult")->mValue.getFloat();
                        settings.weaponFatigueBlockMultiplier = gmst.find("fWeaponFatigueBlockMult")->mValue.getFloat();
                        std::optional<TES3MP::OpenMwMeleeWeapon> weaponValues;
                        if (weapon) { weaponValues.emplace(); weaponValues->weight = attackerWeight; }
                        const float cost = TES3MP::openMwMeleeBlockFatigueCost(settings,
                            weight == 0 ? 0.f : capacity == 0 ? 1.f : weight / capacity,
                            weaponValues, strength);
                        auto fatigue = victim.getFatigue();
                        fatigue.setCurrent(fatigue.getCurrent() - cost);
                        victim.setFatigue(fatigue);
                        victim.setBlock(true);
                        damage = 0;
                    }
                }
            }
            if (!blocked && damage > 0)
            {
                TES3MP::OpenMwMeleeSettings settings;
                settings.combatArmorMinimumMultiplier = gmst.find("fCombatArmorMinMult")->mValue.getFloat();
                const float original = damage;
                const float adjusted = TES3MP::openMwArmorAdjustedDamage(settings, damage, armorRating(owner, victim));
                damage = std::max(1.f, adjusted);
                const int roll = Misc::Rng::roll0to99(rng);
                int slot = MWWorld::InventoryStore::Slot_Cuirass;
                if (roll >= 90) slot = MWWorld::InventoryStore::Slot_CarriedLeft;
                else if (roll >= 85) slot = MWWorld::InventoryStore::Slot_RightGauntlet;
                else if (roll >= 80) slot = MWWorld::InventoryStore::Slot_LeftGauntlet;
                else if (roll >= 70) slot = MWWorld::InventoryStore::Slot_RightPauldron;
                else if (roll >= 60) slot = MWWorld::InventoryStore::Slot_LeftPauldron;
                else if (roll >= 50) slot = MWWorld::InventoryStore::Slot_Boots;
                else if (roll >= 40) slot = MWWorld::InventoryStore::Slot_Greaves;
                else if (roll >= 30) slot = MWWorld::InventoryStore::Slot_Helmet;
                if (slot == MWWorld::InventoryStore::Slot_CarriedLeft
                    && !armorCondition(owner, slot))
                    slot = roll >= 95 ? MWWorld::InventoryStore::Slot_Cuirass
                        : MWWorld::InventoryStore::Slot_LeftPauldron;
                if (const auto armor = armorCondition(owner, slot))
                {
                    const int loss = std::min(armor->mCondition, int(std::ceil(std::max(0.f, original - adjusted))));
                    if (loss) wearArmor(owner, slot, armor->mCondition - loss);
                }
            }
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            return blocked;
        };
        const auto startHitRecovery = [&](size_t defender, const MWMechanics::NpcStats& victim,
            const MWMechanics::HitDamageResult& applied, float damage, bool blocked, Misc::Rng::Generator& rng) {
            if (!mBinding.mMeleeDefenseRules || blocked || damage <= 0) return;
            if (mBinding.mKnockoutAnimation)
            {
                const bool knock = applied.mHasHealthDamage
                    && MWMechanics::rollHitKnockdown(mRuntime.mStore, victim, applied.mHealthDamage, rng);
                if (!combat->knockedDown[defender] && knock)
                {
                    combat->knockedDown[defender] = true;
                    combat->hitKnockdown[defender] = true;
                    combat->knockoutFrame[defender] = 0;
                }
                if (combat->knockedDown[defender])
                { combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng))); return; }
            }
            const auto* bound = mBinding.mBoundHits ? &(*mBinding.mBoundHits)[defender].animations : nullptr;
            const auto groups = bound ? bound->count : mBinding.mBoundMelee->mAnimation.hitRecoveryGroupCount();
            // With no hit clip, CharacterController clears recovery on its next
            // update. Otherwise it selects one of the consecutive hit groups.
            const auto selected = groups ? Misc::Rng::rollDice(int(groups), rng) : 0;
            if (mBinding.mActorPresentation)
            { combat->bodyAction[defender] = tick.value(); combat->hitGroup[defender] = groups ? selected + 1 : 0; }
            combat->hitRecoveryTicks[defender] = groups
                ? (bound ? bound->ticks[size_t(selected)] : mBinding.mBoundMelee->mAnimation.hitRecoveryTicks(unsigned(selected))) : 1;
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
        };
        if (dueRespawn)
        {
            if (life->generation == UINT32_MAX) throw std::invalid_argument("NPC life generation exhausted");
            respawn = mRuntime.prepareRespawn(mCombatNpcOwner, mRespawnInventory);
            step = mBinding.mNavigatingActor->prepareSelectedRestore(life->spawnActor, doors);
            after = step->snapshot();
            combat->actors[2] = life->spawnStats;
            combat->knockedDown[2] = life->spawnStats[8][2] > 0
                && MWMechanics::isFatigueKnockout(mBinding.mKnockoutAnimation
                    ? life->spawnStats[10][0] : 1.f, life->spawnStats[10][2]);
            combat->hitRecoveryTicks[2] = 0;
            combat->knockoutFrame[2] = 0;
            combat->hitKnockdown[2] = false;
            melee = mIdleMelee;
            target = 0; contact = false;
            std::erase_if(projectiles, [placement = before.mActor](const auto& pending) {
                return pending.targetKind == uint64_t(MagicUseTargetKind::Actor)
                    && pending.target == placement;
            });
            const auto previousBodyEffects = timedEffects;
            std::erase_if(timedEffects, [](const auto& effect) { return effect.actor == 2; });
            for (auto& effect : timedEffects) if (effect.beneficiary == 3) effect.beneficiary = 0;
            if (mBinding.mExpandedEffects)
                for (size_t i = 0; i < 2; ++i)
                    updateResources(i, previousBodyEffects, timedEffects, true);
            if (mBinding.mConstantEffects)
            {
                Misc::Rng::Generator rng{combat->rng};
                const auto previous = timedEffects;
                if (reconcileConstants(respawn->values(), 2, {before.mActor,
                        mBinding.mDurableCasters ? 2u : 0u, mBinding.mDurableCasters ? life->generation + 1 : 0u}, tick.value(), mRuntime.mStore,
                        mBinding.mGeneralConstants, timedEffects, &rng, mBinding.mExpandedEffects,
                        mBinding.mSpecialConditions, mBinding.mMovementEffects,
                        mBinding.mAiDecisions, aiNpc, aiUndead, levitationEnabled))
                    updateResources(2, previous, timedEffects, mBinding.mKnockoutAnimation);
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
            if (mBinding.mAiDecisions || mBinding.mMovementEffects)
            {
                Misc::Rng::Generator rng{combat->rng};
                const auto previous = timedEffects;
                if (reconcilePassiveActorEffects(mRuntime.ownerPtr(mCombatNpcOwner), 2,
                        {before.mActor, 2, life->generation + 1}, tick.value(), mRuntime.mStore,
                        timedEffects, &rng, mBinding.mMovementEffects, mBinding.mAiDecisions,
                        aiNpc, aiUndead, levitationEnabled))
                    updateResources(2, previous, timedEffects, mBinding.mKnockoutAnimation);
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
            ++life->generation;
            life->bornTick = tick.value();
            life->respawnTick = 0;
            std::erase_if(combat->conditions, [](const auto& condition) { return condition.actor == 2; });
            initializeConditions(2);
        }
        if (dueNeighborRespawn)
        {
            const size_t i = *dueNeighborRespawn, index = i + 3;
            auto& adjacentLife = neighborLives.at(i);
            if (adjacentLife.generation == UINT32_MAX)
                throw std::invalid_argument("Neighbor life generation exhausted");
            const size_t owner = combatOwner(index);
            respawn = mRuntime.prepareRespawn(owner, mNeighborRespawnInventory.at(i));
            step = mBinding.mNavigatingActor->prepareNeighborRestore(i, adjacentLife.spawnActor, doors);
            after = step->snapshot();
            combat->actors[index] = adjacentLife.spawnStats;
            combat->knockedDown[index] = adjacentLife.spawnStats[8][2] > 0
                && MWMechanics::isFatigueKnockout(adjacentLife.spawnStats[10][0],
                    adjacentLife.spawnStats[10][2]);
            combat->hitRecoveryTicks[index] = 0;
            combat->knockoutFrame[index] = 0;
            combat->hitKnockdown[index] = false;
            const auto idle = mBinding.mNeighborMeleeSet.at(i)(nullptr, "chop");
            combat->neighborAttacks[i] = {idle.identity(), {}, idle.snapshot(), 0, 0, 0, 0, false};
            std::erase_if(projectiles, [placement = combat->npcPlacements.at(i + 1)](const auto& pending) {
                return pending.targetKind == uint64_t(MagicUseTargetKind::Actor)
                    && pending.target == placement;
            });
            const auto previousBodyEffects = timedEffects;
            std::erase_if(timedEffects, [index](const auto& effect) { return effect.actor == index; });
            for (auto& effect : timedEffects)
                if (effect.beneficiary == index + 1) effect.beneficiary = 0;
            if (mBinding.mExpandedEffects)
                for (size_t playerIndex = 0; playerIndex < 2; ++playerIndex)
                    updateResources(playerIndex, previousBodyEffects, timedEffects, true);
            const auto [neighborNpc, neighborUndead] = aiActorKind(mRuntime.ownerPtr(owner));
            if (mBinding.mConstantEffects)
            {
                Misc::Rng::Generator rng{combat->rng};
                const auto previous = timedEffects;
                if (reconcileConstants(respawn->values(), index,
                        {combat->npcPlacements.at(i + 1), 2, adjacentLife.generation + 1},
                        tick.value(), mRuntime.mStore, mBinding.mGeneralConstants, timedEffects, &rng,
                        mBinding.mExpandedEffects, mBinding.mSpecialConditions, mBinding.mMovementEffects,
                        mBinding.mAiDecisions, neighborNpc, neighborUndead, levitationEnabled))
                    updateResources(index, previous, timedEffects, mBinding.mKnockoutAnimation);
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
            if (mBinding.mAiDecisions || mBinding.mMovementEffects)
            {
                Misc::Rng::Generator rng{combat->rng};
                const auto previous = timedEffects;
                if (reconcilePassiveActorEffects(mRuntime.ownerPtr(owner), index,
                        {combat->npcPlacements.at(i + 1), 2, adjacentLife.generation + 1},
                        tick.value(), mRuntime.mStore, timedEffects, &rng,
                        mBinding.mMovementEffects, mBinding.mAiDecisions,
                        neighborNpc, neighborUndead, levitationEnabled))
                    updateResources(index, previous, timedEffects, mBinding.mKnockoutAnimation);
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
            ++adjacentLife.generation;
            adjacentLife.bornTick = tick.value();
            adjacentLife.respawnTick = 0;
            std::erase_if(combat->conditions, [index](const auto& condition) { return condition.actor == index; });
            initializeConditions(index);
        }
        struct PlayerHitRequest
        {
            size_t owner, victim; MeleeAttackType direction; float strength; bool contact;
            std::optional<BowProjectile> projectile;
        };
        const auto npcVictim = [&](uint64_t placement) -> size_t {
            if (!mBinding.mNeighborCombat)
                return placement == before.mActor ? 2 : 0;
            const auto found = std::ranges::find(combat->npcPlacements, placement);
            return found == combat->npcPlacements.end() ? 0
                : size_t(found - combat->npcPlacements.begin()) + 2;
        };
        const auto swingVictim = [&](const PlayerSwing& swing) -> size_t {
            if (mBinding.mAuthoritativeAim && swing.ammunition && !swing.target) return 0;
            return mBinding.mNeighborCombat ? npcVictim(swing.target) : 2;
        };
        std::vector<PlayerHitRequest> playerContacts;
        std::vector<std::pair<BowProjectile, size_t>> npcProjectileContacts;
        if (mBinding.mPlayerMelee[0] && combat)
        {
            const std::array<std::string_view, 3> directions{"chop", "slash", "thrust"};
            if (playerAttack)
            {
                const auto owner = actor(playerAttacker);
                const auto held = mRuntime.equippedWeaponCondition(owner);
                const auto values = combatEquipmentValues(owner, command.get());
                const ESM::Weapon* weapon = nullptr;
                if (held)
                {
                    const auto item = std::ranges::find(values.mObjects, held->mItem,
                        [](const auto& item) { return item.mRef.mRefNum; });
                    if (item == values.mObjects.end()) throw std::invalid_argument("Player swing source missing");
                    weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
                }
                auto clip = mBinding.mPlayerMelee[owner](weapon, directions[size_t(playerAttack->attackType)]);
                const size_t victim = playerAttack->targetActorId
                    ? npcVictim(playerAttack->targetActorId->value()) : 0;
                if (playerAttack->targetActorId && !victim)
                    throw std::invalid_argument("Player swing target placement changed");
                combat->swings[owner] = PlayerSwing{playerAttack->commandId.value(),
                    held ? wireId(held->mItem).value() : 0, victim ? victimLife(victim).generation : 0,
                    uint64_t(playerAttack->attackType),
                    PlayerSwing::None, playerAttack->attackStrength,
                    weapon ? std::string(weapon->mId.getRefIdString()) : std::string{}, clip.identity(), clip.snapshot()};
                combat->swings[owner]->target = playerAttack->targetActorId
                    ? playerAttack->targetActorId->value() : 0;
                if (mBinding.mBowRelease && rangedWeapon(weapon, mBinding.mRangedRelease))
                {
                    const auto* ammo = equippedAmmunition(values, mRuntime.mStore, *weapon,
                        mBinding);
                    if (!ammo) throw std::invalid_argument("Bow release ammunition missing");
                    combat->swings[owner]->ammunition = wireId(ammo->mRef.mRefNum).value();
                    combat->swings[owner]->ammoRecord = std::string(ammo->mRef.mRefID.getRefIdString());
                    if (mBinding.mAuthoritativeAim)
                    {
                        const auto* shooter = players.findPlayer(playerAttacker);
                        const auto position = shooter->transform().position();
                        osg::Vec3f aim((*playerAttack->aimPoint)[0] - float(double(position.x()) / 1024),
                            (*playerAttack->aimPoint)[1] - float(double(position.y()) / 1024),
                            (*playerAttack->aimPoint)[2] - float(double(position.z()) / 1024) - 110.f);
                        if (aim.normalize() == 0) throw std::invalid_argument("Player aim direction invalid");
                        combat->swings[owner]->aim = {aim.x(), aim.y(), aim.z()};
                    }
                }
            }
            for (size_t owner = 0; owner < combat->swings.size(); ++owner)
            {
                auto& pending = combat->swings[owner];
                if (!pending || !pending->pending()) continue;
                auto& swing = *pending;
                const size_t victim = swingVictim(swing);
                const bool worldShot = mBinding.mAuthoritativeAim && swing.ammunition && !swing.target;
                if (!victim && !worldShot) { swing.interruption = PlayerSwing::TargetLost; continue; }
                const auto targetScene = victim ? victimScene(victim) : after;
                if (!mBinding.mNeighborCombat && victim) swing.target = targetScene.mActor;
                const auto* weapon = swing.weapon.empty() ? nullptr
                    : mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(swing.weapon));
                if (combat->actors[owner][8][2] <= 0 || combat->knockedDown[owner] || hasParalysis(timedEffects, owner) || combat->hitRecoveryTicks[owner])
                { swing.interruption = PlayerSwing::Incapacitated; continue; }
                // Equipment still commits when simulation is paused. Cancel in
                // that transaction so the saved wind-up keeps a valid source.
                if (!swing.state.mHit)
                {
                    const auto values = combatEquipmentValues(owner, command.get());
                    const auto slot = values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
                    const auto held = mRuntime.equippedWeaponCondition(owner);
                    if (swing.source != (slot.isSet() ? wireId(slot).value() : 0)
                        || (held && held->mCondition <= 0 && weapon
                            && (MWMechanics::getWeaponType(weapon->mData.mType)->mFlags & ESM::WeaponType::HasHealth)))
                    { swing.interruption = PlayerSwing::SourceChanged; continue; }
                    if (swing.ammunition)
                    {
                        const auto* ammo = equippedAmmunition(values, mRuntime.mStore, *weapon,
                            mBinding);
                        if (!ammo || wireId(ammo->mRef.mRefNum).value() != swing.ammunition
                            || ammo->mRef.mRefID != ESM::RefId::stringRefId(swing.ammoRecord))
                        { swing.interruption = PlayerSwing::SourceChanged; continue; }
                    }
                }
                // All-offline areas retain a restartable image. If a peer keeps
                // the encounter active, the departing player's swing cancels.
                const bool recoveryActive = victim && swing.state.mHit && combat->actors[victim][8][2] <= 0
                    && std::ranges::any_of(players.activeSessions(), [&](const auto& session) {
                        const auto* peer = players.findPlayer(session.playerId());
                        return peer && peer->transform().cell() == actorCell(targetScene);
                    });
                if (!active && !recoveryActive) continue;
                if (std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                        return session.playerId() == mBinding.mPlayers[owner];
                    })) { swing.interruption = PlayerSwing::Disconnected; continue; }
                const auto* player = players.findPlayer(mBinding.mPlayers[owner]);
                if (!swing.state.mHit)
                {
                    if (!player || player->transform().cell() != actorCell(targetScene)
                        || (victim && (victimLife(victim).generation != swing.targetLife
                            || combat->actors[victim][8][2] <= 0)))
                    { swing.interruption = PlayerSwing::TargetLost; continue; }
                    const auto position = player->transform().position();
                    if (!swing.ammunition && !mBinding.mNavigatingActor->lineOfSight(
                            {float(double(position.x()) / 1024), float(double(position.y()) / 1024),
                                float(double(position.z()) / 1024) + 110.f},
                            {targetScene.mPosition[0], targetScene.mPosition[1], targetScene.mPosition[2] + 110.f}))
                    { swing.interruption = PlayerSwing::TargetLost; continue; }
                }
                if (swing.ammunition && !swing.state.mHit && combat->arrows.size() >= MaximumActorProjectiles)
                    continue;
                auto clip = mBinding.mPlayerMelee[owner](weapon, directions[swing.direction]);
                clip.restore(swing.state);
                if (!swing.state.mReleased && (clip.windUp() >= swing.strength
                        || (clip.windUp() == -1.f && clip.phaseCompletion() == 1.f)))
                {
                    if (clip.windUp() == -1.f)
                    {
                        Misc::Rng::Generator rng;
                        Misc::Rng::deserialize(std::to_string(combat->rng), rng);
                        swing.strength = MWMechanics::resolveAttackStrength(-1.f, rng);
                        combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    }
                    clip.release(swing.strength);
                    breakInvisibility(owner);
                }
                const auto hit = clip.advance(seconds);
                swing.state = clip.snapshot();
                if (hit)
                {
                    const auto position = player->transform().position();
                    const osg::Vec3f origin(float(double(position.x()) / 1024), float(double(position.y()) / 1024),
                        float(double(position.z()) / 1024));
                    if (swing.ammunition)
                    {
                        const auto values = combatEquipmentValues(owner, command.get());
                        const auto* ammo = equippedAmmunition(values, mRuntime.mStore, *weapon,
                            mBinding);
                        if (!ammo) throw std::invalid_argument("Bow release lost ammunition");
                        // Temporary server body-center launch proxy, shared with
                        // spell targeting. Flight will replace this with bound geometry.
                        osg::Vec3f direction = mBinding.mAuthoritativeAim
                            ? osg::Vec3f(swing.aim[0], swing.aim[1], swing.aim[2])
                            : osg::Vec3f(targetScene.mPosition[0] - origin.x(),
                                targetScene.mPosition[1] - origin.y(), targetScene.mPosition[2] - origin.z());
                        if (direction.normalize() == 0) throw std::invalid_argument("Bow launch direction invalid");
                        combat->arrows.push_back({mBinding.mPlayers[owner].value(), swing.command, swing.source,
                            swing.ammunition, swing.target, swing.targetLife, tick.value(),
                            swing.strength, swing.weapon, swing.ammoRecord,
                            {origin.x(), origin.y(), origin.z() + 110.f},
                            {direction.x(), direction.y(), direction.z()}});
                        if (mBinding.mRangedFlight)
                        {
                            auto& arrow = combat->arrows.back();
                            const float speed = MWMechanics::projectileLaunchSpeed(mRuntime.mStore,
                                weapon->mData.mType == ESM::Weapon::MarksmanThrown, swing.strength);
                            if (!std::isfinite(speed) || speed <= 0 || speed > 50000)
                                throw std::invalid_argument("Native projectile launch speed invalid");
                            for (size_t axis = 0; axis < 3; ++axis) arrow.velocity[axis] = arrow.direction[axis] * speed;
                            const auto held = mRuntime.equippedWeaponCondition(owner);
                            arrow.condition = (MWMechanics::getWeaponType(weapon->mData.mType)->mFlags & ESM::WeaponType::HasHealth)
                                && weapon->mData.mHealth && held
                                ? std::clamp(float(held->mCondition) / weapon->mData.mHealth, 0.f, 1.f) : 1.f;
                        }
                        charges.push_back({owner, ammo->mRef.mRefNum, ammo->mRef.mEnchantmentCharge, 0, true});
                        auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[owner], timedEffects, owner);
                        const float capacity = attacker.getAttribute(ESM::Attribute::Strength).getModified()
                            * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
                        const float weight = std::max(0.f, mRuntime.storage(owner).getWeight());
                        MWMechanics::applyFatigueLoss(attacker, mRuntime.mStore, weapon->mData.mWeight, swing.strength,
                            weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity);
                        saveCombatStats(combat->actors[owner], attacker, timedEffects, owner);
                        continue;
                    }
                    const bool contact = MWMechanics::isInMeleeReach(origin,
                        osg::Vec3f(targetScene.mPosition[0], targetScene.mPosition[1], targetScene.mPosition[2]), 0, 0,
                        MWMechanics::getMeleeWeaponReach(mRuntime.mStore, weapon, true));
                    playerContacts.push_back({owner, victim, MeleeAttackType(swing.direction), swing.strength, contact});
                }
            }
        }
        else if (playerAttack && combat)
        {
            breakInvisibility(actor(playerAttacker));
            playerContacts.push_back({actor(playerAttacker), 2, playerAttack->attackType, playerAttack->attackStrength, true});
        }
        if (mBinding.mRangedFlight && combat
            && (!mBinding.mTravelerNeighborhood || (mBinding.worldDomains().size() <= mBinding.mTravelerCellBudget
                && mBinding.mTravelerStepBudget >= 2)))
            for (auto& arrow : combat->arrows)
            {
                if (arrow.terminal || arrow.releaseTick == tick.value()) continue;
                if (arrow.casterKind == 2)
                {
                    const size_t casterIndex = npcVictim(arrow.caster);
                    if (!casterIndex || victimLife(casterIndex).generation != arrow.casterLife
                        || victimLife(casterIndex).respawnTick || combat->actors[casterIndex][8][2] <= 0)
                        throw std::invalid_argument("NPC projectile caster life changed");
                }
                // The collision scene is retained by flight demand, including after disconnect.
                // Age counts simulated substeps, never wall-clock downtime.
                for (unsigned substep = 0; substep < 2 && !arrow.terminal; ++substep)
                {
                    const auto velocity = MWMechanics::advanceProjectileVelocity(
                        {arrow.velocity[0], arrow.velocity[1], arrow.velocity[2]}, 1.f / 60);
                    std::array<float, 3> endpoint;
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        arrow.velocity[axis] = velocity[axis];
                        endpoint[axis] = arrow.position[axis] + velocity[axis] / 60;
                    }
                    const auto hit = mBinding.mNavigatingActor->projectileContact(arrow.position, endpoint,
                        arrow.casterKind == 2 ? arrow.caster : 0);
                    std::optional<size_t> playerImpact;
                    std::array<float, 3> playerPosition{};
                    if (arrow.casterKind == 2)
                    {
                        const osg::Vec3f from(arrow.position[0], arrow.position[1], arrow.position[2]);
                        const osg::Vec3f to(endpoint[0], endpoint[1], endpoint[2]);
                        const auto segment = to - from;
                        const float length = segment.length2();
                        float first = hit ? (osg::Vec3f(hit->position[0], hit->position[1], hit->position[2]) - from).length2() : length;
                        if (length > 0)
                            for (size_t index = 0; index < mBinding.mPlayers.size(); ++index)
                            {
                                const auto* player = players.findPlayer(mBinding.mPlayers[index]);
                                if (!player || player->transform().cell() != actorCell(before)
                                    || combat->actors[index][8][2] <= 0
                                    || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                                        return session.playerId() == player->playerId();
                                    })) continue;
                                const auto position = player->transform().position();
                                const osg::Vec3f center(float(double(position.x()) / 1024),
                                    float(double(position.y()) / 1024), float(double(position.z()) / 1024) + 64.f);
                                const auto offset = from - center;
                                const float projection = -(offset * segment) / length;
                                const float closest = std::clamp(projection, 0.f, 1.f);
                                if ((offset + segment * closest).length2() > 32.f * 32.f) continue;
                                const float distance = std::max(0.f, closest -
                                    std::sqrt(std::max(0.f, 32.f * 32.f - (offset + segment * closest).length2()) / length));
                                if (distance * distance * length > first) continue;
                                first = distance * distance * length;
                                playerImpact = index;
                                const auto impact = from + segment * distance;
                                playerPosition = {impact.x(), impact.y(), impact.z()};
                            }
                    }
                    ++arrow.steps;
                    arrow.position = playerImpact ? playerPosition : hit ? hit->position : endpoint;
                    if (playerImpact)
                    {
                        arrow.terminal = 1;
                        npcProjectileContacts.emplace_back(arrow, *playerImpact);
                    }
                    else if (hit)
                    {
                        arrow.terminal = 1;
                        // The target in the launch intent fixes aim, not the impact victim.
                        // The first server-owned hull in the sweep owns a physical hit.
                        const size_t victim = npcVictim(hit->actor);
                        if (arrow.casterKind == 1 && victim && !victimLife(victim).respawnTick && combat->actors[victim][8][2] > 0
                            && (hit->actor != arrow.target
                                || arrow.targetLife == victimLife(victim).generation))
                            playerContacts.push_back({actor(PlayerId::fromValue(arrow.caster).value()), victim,
                                MeleeAttackType::Chop, arrow.strength, true, arrow});
                    }
                    else if (arrow.steps == 3600) arrow.terminal = 2;
                }
            }
        std::array<bool, 2> reportedAssaultContact{};
        for (const auto& request : playerContacts)
        {
            const size_t owner = request.owner;
            const size_t defender = request.victim;
            const auto targetScene = victimScene(defender);
            const auto playerAttacker = mBinding.mPlayers[owner];
            if (combat->actors[defender][8][2] <= 0)
            {
                if (combat->swings[owner]) combat->swings[owner]->interruption = PlayerSwing::TargetLost;
                continue;
            }
            auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[owner], timedEffects, owner);
            auto victim = loadCombatStats(mRuntime.mStore, combat->actors[defender], timedEffects, defender);
            if (mBinding.mKnockoutRules) victim.setKnockedDown(combat->knockedDown[defender]);
            addTimedResistance(attacker, timedEffects, owner);
            addTimedResistance(victim, timedEffects, defender);
            const auto held = mRuntime.equippedWeaponCondition(owner);
            if ((!request.projectile && held && held->mCondition <= 0) || victim.getHealth().getCurrent() <= 0)
                throw std::invalid_argument("Native player attack became stale before tick composition");
            const ESM::Weapon* weapon = nullptr;
            if (request.projectile)
                weapon = mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(request.projectile->weapon));
            else if (held)
            {
                const auto values = mRuntime.installedValues(owner);
                const auto item = std::ranges::find(values.mObjects, held->mItem,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == values.mObjects.end() || mRuntime.mStore.find(item->mRef.mRefID) != ESM::Weapon::sRecordId)
                    throw std::invalid_argument("Native player weapon identity missing");
                weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
            }
            const float strength = request.strength;
            const bool hasWeaponHealth = weapon && weapon->mData.mHealth
                && (!request.projectile || (MWMechanics::getWeaponType(weapon->mData.mType)->mFlags & ESM::WeaponType::HasHealth));
            const float capacity = attacker.getAttribute(ESM::Attribute::Strength).getModified()
                * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
            const float weight = std::max(0.f, mRuntime.storage(owner).getWeight());
            const float encumbrance = weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity;
            if (!request.projectile) MWMechanics::applyFatigueLoss(attacker, mRuntime.mStore,
                weapon ? weapon->mData.mWeight : 0.f, strength, encumbrance);
            const auto skill = weapon ? MWMechanics::getWeaponType(weapon->mData.mType)->mSkill
                : ESM::Skill::HandToHand;
            const bool paralyzed = victim.getMagicEffects()
                .getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() > 0;
            const float chance = MWMechanics::getHitChance(mRuntime.mStore, attacker, victim,
                int(attacker.getSkill(skill).getModified()), false, paralyzed);
            Misc::Rng::Generator rng;
            Misc::Rng::deserialize(std::to_string(combat->rng), rng);
            const bool success = request.contact && Misc::Rng::roll0to99(rng) < chance;
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            float damage = 0;
            bool blocked = false;
            const bool healthUnarmed = mBinding.mKnockoutRules
                && (victim.getKnockedDown() || paralyzed);
            const auto damagedStat = weapon || healthUnarmed ? MeleeDamageStat::Health : MeleeDamageStat::Fatigue;
            if (success && weapon)
            {
                const auto& range = request.direction == MeleeAttackType::Chop ? weapon->mData.mChop
                    : request.direction == MeleeAttackType::Slash ? weapon->mData.mSlash : weapon->mData.mThrust;
                damage = range[0] + (range[1] - range[0]) * strength;
                if (request.projectile) damage = MWMechanics::projectileBaseDamage(*weapon,
                    *mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(request.projectile->ammoRecord)), strength);
                TES3MP::OpenMwMeleeSettings settings;
                const auto& gmst = mRuntime.mStore.get<ESM::GameSetting>();
                settings.damageStrengthBase = gmst.find("fDamageStrengthBase")->mValue.getFloat();
                settings.damageStrengthMultiplier = gmst.find("fDamageStrengthMult")->mValue.getFloat();
                damage = TES3MP::openMwAdjustedWeaponDamage(settings,
                    attacker.getAttribute(ESM::Attribute::Strength).getModified(),
                    request.projectile ? request.projectile->condition
                        : weapon->mData.mHealth ? float(held->mCondition) / weapon->mData.mHealth : 1.f,
                    hasWeaponHealth, damage);
            }
            else if (success)
            {
                damage = healthUnarmed
                    ? MWMechanics::getUnarmedHealthDamage(mRuntime.mStore, attacker,
                        attacker.getSkill(ESM::Skill::HandToHand).getModified(), strength)
                    : MWMechanics::getUnarmedFatigueDamage(mRuntime.mStore, attacker,
                        attacker.getSkill(ESM::Skill::HandToHand).getModified(), strength);
            }
            const float weaponDamage = damage;
            if (success && request.projectile && victim.isWerewolf()
                && (mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(request.projectile->ammoRecord))
                    ->mData.mFlags & ESM::Weapon::Silver))
                damage *= gmst.find("fWereWolfSilverWeaponDamageMult")->mValue.getFloat();
            if (success && (!std::isfinite(damage) || damage < 0 || damage > 1'000'000))
                throw std::invalid_argument("Native player melee damage invalid");
            if (success && mBinding.mKnockoutRules)
                damage = MWMechanics::applyKnockoutDamageMultiplier(mRuntime.mStore, victim, damage);
            if (success && (!std::isfinite(damage) || damage < 0 || damage > 1'000'000))
                throw std::invalid_argument("Native player knockout damage invalid");
            if (success && !request.projectile && mBinding.mExpandedEffects)
            {
                if (weapon) applyStrike(magicCaster(owner), *held, *weapon, attacker, victim, defender, rng);
                retaliate(attacker, victim, owner, defender, rng);
            }
            if (success && damage > 0)
            {
                std::array<float, 3> attackerPosition;
                if (request.projectile) attackerPosition = request.projectile->position;
                else
                {
                    const auto* player = players.findPlayer(playerAttacker);
                    const auto position = player->transform().position();
                    attackerPosition = {float(double(position.x()) / 1024),
                        float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
                }
                const auto* resistanceWeapon = request.projectile
                    && (mBinding.mOnlyAppropriateAmmunitionBypassesResistance
                        || MWMechanics::isNormalWeapon(weapon, mBinding.mEnchantedWeaponsAreMagical))
                    ? mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(request.projectile->ammoRecord)) : weapon;
                blocked = defendHit(defender, victim, attacker, resistanceWeapon, strength,
                    weapon ? weapon->mData.mWeight : 0.f, attacker.getSkill(skill).getModified(),
                    attackerPosition, targetScene.mPosition, targetScene.mYaw,
                    defender == 2 && ((after.mPosition[0] - before.mPosition[0]) * -std::sin(after.mYaw)
                        + (after.mPosition[1] - before.mPosition[1]) * std::cos(after.mYaw) > 0),
                    damage, rng, bool(request.projectile));
                const auto applied = MWMechanics::applyHitDamage(victim, {{damagedStat == MeleeDamageStat::Health ? "health" : "fatigue",
                    damage}}, MWWorld::TimeStamp{});
                if (victim.getHealth().getCurrent() > 0) startHitRecovery(defender, victim, applied, damage, blocked, rng);
                else combat->hitRecoveryTicks[defender] = 0;
            }
            if (success && request.projectile)
                applyProjectileStrike(magicCaster(owner).identity, request.projectile->command, owner,
                    *mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(request.projectile->ammoRecord)),
                    request.projectile->position, attacker, victim, defender, rng);
            if (hasWeaponHealth && held
                && (!request.projectile || wireId(held->mItem).value() == request.projectile->source))
            {
                const float multiplier = mRuntime.mStore.get<ESM::GameSetting>()
                    .find("fWeaponDamageMult")->mValue.getFloat();
                const auto previous = std::ranges::find_if(wear, [&](const auto& entry) {
                    return entry.owner == owner && entry.slot == MWWorld::InventoryStore::Slot_CarriedRight;
                });
                const int condition = MWMechanics::weaponConditionAfterHit(
                    previous == wear.end() ? held->mCondition : previous->condition, weaponDamage, success, multiplier);
                if (previous == wear.end()) wear.push_back({owner, *held, condition});
                else previous->condition = condition;
            }
            if (success && weapon && !request.projectile && !mBinding.mExpandedEffects) applyStrike(magicCaster(owner), *held, *weapon, attacker, victim, defender, rng);
            if (success && request.projectile)
            {
                const auto record = ESM::RefId::stringRefId(request.projectile->ammoRecord);
                const auto* ammunition = mRuntime.mStore.get<ESM::Weapon>().find(record);
                if (ammunition->mEnchant.empty())
                {
                    const float storeChance = gmst.find("fProjectileThrownStoreChance")->mValue.getFloat();
                    if (!std::isfinite(storeChance) || std::abs(storeChance) > 10000.f)
                        throw std::invalid_argument("Native projectile recovery chance invalid");
                    if (Misc::Rng::rollProbability(rng) < storeChance / 100.f)
                        recoveries.push_back({combatOwner(defender), record, owner,
                            ESM::RefNum{uint32_t(request.projectile->ammunition),
                                std::bit_cast<int32_t>(uint32_t(request.projectile->ammunition >> 32))}});
                }
            }
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            saveCombatStats(combat->actors[owner], attacker, timedEffects, owner);
            saveCombatStats(combat->actors[defender], victim, timedEffects, defender);
            if (mBinding.mKnockoutRules)
            {
                combat->knockedDown[owner] = attacker.getHealth().getCurrent() > 0
                    && (fatigueKnockout(attacker, mBinding.mKnockoutAnimation)
                        || (mBinding.mKnockoutAnimation && combat->knockedDown[owner]));
                combat->knockedDown[defender] = victim.getHealth().getCurrent() > 0
                    && (fatigueKnockout(victim, mBinding.mKnockoutAnimation)
                        || (mBinding.mKnockoutAnimation && combat->knockedDown[defender]));
                if (defender == 2 && (combat->knockedDown[defender] || hasParalysis(timedEffects, defender)))
                {
                    step.reset(); after = before; report.status = Diagnostics::Status::Idle;
                    melee = mIdleMelee;
                    target = 0; contact = false;
                }
            }
            // The selected NPC is aware of an assault on itself. Stock crime
            // reporting requires an Alarm 100 witness; the victim qualifies
            // even outside the alarm radius. Resolve this only after a valid
            // authoritative contact and stage it with the hit and inventory.
            if (defender == 2 && mBinding.mPlayerAi && request.contact && (!request.projectile || success)
                && mCombat && life && !mCombat->npcAction && !mMeleeTarget
                && !mCombat->fleeTarget && !mCombat->knockedDown[2] && !life->respawnTick)
            {
                const auto npcPtr = mRuntime.ownerPtr(mCombatNpcOwner);
                const auto* npc = npcPtr.getType() == ESM::NPC::sRecordId
                    ? npcPtr.get<ESM::NPC>() : nullptr;
                if (npc && (mBinding.mSocialLifecycle || npc->mBase->mAiData.mAlarm >= 100)
                    && !victim.isWerewolf()
                    && victim.getMagicEffects().getOrDefault(ESM::MagicEffect::Vampirism).getMagnitude() <= 0)
                {
                    reportedAssaultContact[owner] = true;
                }
            }
            playerHits.push_back(MeleeCombatEvent{playerAttacker,
                ActorId::fromValue(targetScene.mActor).value(),
                CombatRevision::fromValue(tick.value()).value(),
                CombatRevision::fromValue(tick.value()).value(),
                damage, damagedStat, success, blocked,
                victim.getHealth().getCurrent() <= 0});
            if (victim.getHealth().getCurrent() <= 0)
            {
                recordNpcDeath(defender, {playerAttacker.value(), 1, 1});
                if (defender == 2)
                {
                    step.reset();
                    after = before;
                    report.status = Diagnostics::Status::Idle;
                }
                else
                {
                    auto& attack = combat->neighborAttacks.at(defender - 3);
                    const auto idle = mBinding.mNeighborMeleeSet.at(defender - 3)(nullptr, "chop");
                    attack.target = 0; attack.action = 0;
                    attack.state = idle.snapshot(); attack.identity = idle.identity();
                    attack.weapon.clear(); attack.source = 0; attack.contact = false;
                    combat->knockedDown[defender] = false;
                    combat->hitRecoveryTicks[defender] = 0;
                    combat->knockoutFrame[defender] = 0;
                    combat->hitKnockdown[defender] = false;
                }
            }
        }
        for (const auto& [flight, defender] : npcProjectileContacts)
        {
            const size_t attackerIndex = npcVictim(flight.caster);
            if (!attackerIndex || victimLife(attackerIndex).generation != flight.casterLife
                || victimLife(attackerIndex).respawnTick || combat->actors[attackerIndex][8][2] <= 0
                || combat->actors[defender][8][2] <= 0) continue;
            auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[attackerIndex], timedEffects, attackerIndex);
            auto victim = loadCombatStats(mRuntime.mStore, combat->actors[defender], timedEffects, defender);
            victim.setKnockedDown(combat->knockedDown[defender]);
            addTimedResistance(attacker, timedEffects, attackerIndex);
            addTimedResistance(victim, timedEffects, defender);
            const auto* weapon = mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(flight.weapon));
            const auto* ammunition = mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(flight.ammoRecord));
            const auto skill = MWMechanics::getWeaponType(weapon->mData.mType)->mSkill;
            const bool paralyzed = victim.getMagicEffects().getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() > 0;
            const float chance = MWMechanics::getHitChance(mRuntime.mStore, attacker, victim,
                int(attacker.getSkill(skill).getModified()), false, paralyzed);
            Misc::Rng::Generator rng{combat->rng};
            const bool success = Misc::Rng::roll0to99(rng) < chance;
            float damage = 0.f;
            bool blocked = false;
            const auto owner = combatOwner(attackerIndex);
            const auto held = mRuntime.equippedWeaponCondition(owner);
            if (success)
            {
                damage = MWMechanics::projectileBaseDamage(*weapon, *ammunition, flight.strength);
                TES3MP::OpenMwMeleeSettings settings;
                settings.damageStrengthBase = gmst.find("fDamageStrengthBase")->mValue.getFloat();
                settings.damageStrengthMultiplier = gmst.find("fDamageStrengthMult")->mValue.getFloat();
                const bool weaponHealth = MWMechanics::getWeaponType(weapon->mData.mType)->mFlags & ESM::WeaponType::HasHealth;
                damage = TES3MP::openMwAdjustedWeaponDamage(settings,
                    attacker.getAttribute(ESM::Attribute::Strength).getModified(), flight.condition,
                    weaponHealth && weapon->mData.mHealth, damage);
                if (victim.isWerewolf() && (ammunition->mData.mFlags & ESM::Weapon::Silver))
                    damage *= gmst.find("fWereWolfSilverWeaponDamageMult")->mValue.getFloat();
                if (!std::isfinite(damage) || damage < 0 || damage > 1'000'000)
                    throw std::invalid_argument("NPC projectile damage invalid");
                if (mBinding.mKnockoutRules)
                    damage = MWMechanics::applyKnockoutDamageMultiplier(mRuntime.mStore, victim, damage);
                if (damage > 0)
                {
                    const auto* player = players.findPlayer(mBinding.mPlayers[defender]);
                    const auto position = player->transform().position();
                    const std::array<float, 3> defenderPosition{float(double(position.x()) / 1024),
                        float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
                    const float yaw = float(double(player->transform().orientation().z().value())
                        * (2.0 * std::numbers::pi_v<double> / 4294967296.0));
                    const auto* resistanceWeapon = mBinding.mOnlyAppropriateAmmunitionBypassesResistance
                        || MWMechanics::isNormalWeapon(weapon, mBinding.mEnchantedWeaponsAreMagical)
                            ? ammunition : weapon;
                    blocked = defendHit(defender, victim, attacker, resistanceWeapon, flight.strength,
                        weapon->mData.mWeight, attacker.getSkill(skill).getModified(), flight.position,
                        defenderPosition, yaw, false, damage, rng, true);
                    const auto applied = MWMechanics::applyHitDamage(victim, {{"health", damage}}, MWWorld::TimeStamp{});
                    if (victim.getHealth().getCurrent() > 0)
                        startHitRecovery(defender, victim, applied, damage, blocked, rng);
                    else combat->hitRecoveryTicks[defender] = 0;
                }
                applyProjectileStrike({flight.caster, flight.casterKind, flight.casterLife}, flight.command, attackerIndex,
                    *ammunition, flight.position, attacker, victim, defender, rng);
                retaliate(attacker, victim, attackerIndex, defender, rng);
            }
            if (held && wireId(held->mItem).value() == flight.source && weapon->mData.mHealth
                && (MWMechanics::getWeaponType(weapon->mData.mType)->mFlags & ESM::WeaponType::HasHealth))
            {
                const auto prior = std::ranges::find_if(wear, [owner](const auto& entry) {
                    return entry.owner == owner && entry.slot == MWWorld::InventoryStore::Slot_CarriedRight;
                });
                const int condition = MWMechanics::weaponConditionAfterHit(
                    prior == wear.end() ? held->mCondition : prior->condition, damage, success,
                    gmst.find("fWeaponDamageMult")->mValue.getFloat());
                if (prior == wear.end()) wear.push_back({owner, *held, condition});
                else prior->condition = condition;
            }
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            saveCombatStats(combat->actors[attackerIndex], attacker, timedEffects, attackerIndex);
            saveCombatStats(combat->actors[defender], victim, timedEffects, defender);
            actorHits.push_back({ActorId::fromValue(flight.caster).value(), mBinding.mPlayers[defender],
                CombatRevision::fromValue(tick.value()).value(), CombatRevision::fromValue(tick.value()).value(),
                damage, MeleeDamageStat::Health, success, blocked, victim.getHealth().getCurrent() <= 0});
        }
        const size_t flyingCount = projectiles.size();
        struct Cast
        {
            MagicCasterContext context;
            MagicUseSourceKind sourceKind;
            uint64_t sourceId;
            MagicUseTargetKind targetKind;
            uint64_t targetId, commandId;
            PreparedInstantSpell record;
            std::optional<ItemCharge> charge;
            uint64_t effectSource;
        };
        std::vector<Cast> casts;
        if (combat) for (auto* use = playerCasts.get(); use; use = use->concurrent.get())
        {
            const size_t owner = actor(use->caster);
            if (mBinding.mPlayerCastLifecycle)
            {
                if (combat->playerCasts[owner]) throw std::invalid_argument("Player already casting");
                breakInvisibility(owner);
                const auto& input = use->use;
                combat->playerCasts[owner] = ActorCampaignCast{use->caster.value(), 1, input.commandId.value(),
                    uint64_t(input.sourceKind), input.sourceId, uint64_t(input.targetKind), input.targetId,
                    uint64_t(use->spell.effects.effects.front().mRange), 0, ActorCampaignCast::Selected,
                    input.targetKind == MagicUseTargetKind::Actor
                        ? victimLife(npcVictim(input.targetId)).generation
                        : input.targetKind == MagicUseTargetKind::Door
                            ? doorContactRevision(*mAreaDoors[size_t(std::ranges::find(mBinding.mDoors, input.targetId,
                                &InventoryServiceBinding::OrdinaryDoorPlacement::mId) - mBinding.mDoors.begin())].state)
                        : input.targetKind == MagicUseTargetKind::Container
                            ? mContainerLocks[container(ContainerId::fromValue(input.targetId))].revision : 1};
            }
            else
            {
                breakInvisibility(owner);
                casts.push_back({magicCaster(owner), use->use.sourceKind, use->use.sourceId,
                    use->use.targetKind, use->use.targetId, use->use.commandId.value(),
                    use->spell, use->charge, use->effectSource});
            }
        }
        const auto playerPosition = [&](size_t index, bool travel = false) -> std::optional<std::array<float, 3>> {
            const auto* player = players.findPlayer(mBinding.mPlayers[index]);
            if (!player || (travel ? !worldDomain(player->transform().cell())
                                  : player->transform().cell() != actorCell(before))
                || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                    return session.playerId() == player->playerId(); })) return {};
            const auto p = player->transform().position();
            return std::array<float, 3>{float(double(p.x()) / 1024), float(double(p.y()) / 1024), float(double(p.z()) / 1024) + 64.f};
        };
        const auto playerTargetValid = [&](const ActorCampaignCast& state, const PreparedInstantEffects& effects) {
            const auto origin = playerPosition(actor(PlayerId::fromValue(state.actor).value()),
                mBinding.mPlayerTravel && playerTravelEffects(effects));
            if (!origin) return false;
            if (!state.targetKind) return true;
            std::optional<std::array<float, 3>> endpoint;
            if (state.targetKind == uint64_t(MagicUseTargetKind::Actor))
            {
                const size_t victim = npcVictim(state.target);
                if (!victim || state.targetLife != victimLife(victim).generation
                    || victimLife(victim).respawnTick || combat->actors[victim][8][2] <= 0) return false;
                const auto scene = victim == 2 ? before : victimScene(victim);
                endpoint = {scene.mPosition[0], scene.mPosition[1], scene.mPosition[2] + 55.f};
            }
            else if (state.targetKind == uint64_t(MagicUseTargetKind::Door))
            {
                const auto found = std::ranges::find(mBinding.mDoors, state.target,
                    &InventoryServiceBinding::OrdinaryDoorPlacement::mId);
                if (found == mBinding.mDoors.end()
                    || found->mCell != players.findPlayer(PlayerId::fromValue(state.actor).value())->transform().cell())
                    return false;
                const auto& door = *mAreaDoors[size_t(found - mBinding.mDoors.begin())].state;
                if (state.targetLife != doorContactRevision(door)) return false;
                endpoint = {door.mPosition.pos[0], door.mPosition.pos[1], door.mPosition.pos[2] + 64.f};
            }
            else if (state.targetKind == uint64_t(MagicUseTargetKind::Container))
            {
                const auto index = container(ContainerId::fromValue(state.target));
                const auto& placed = mBinding.mContainers[index];
                if (!placed.mPlacement || state.targetLife != mContainerLocks[index].revision
                    || placed.mCell != players.findPlayer(PlayerId::fromValue(state.actor).value())->transform().cell())
                    return false;
                endpoint = {placed.mPlacement->mPos.pos[0], placed.mPlacement->mPos.pos[1],
                    placed.mPlacement->mPos.pos[2] + 64.f};
            }
            else
            {
                const auto index = actor(PlayerId::fromValue(state.target).value());
                if (combat->actors[index][8][2] <= 0) return false;
                endpoint = playerPosition(index);
            }
            if (!endpoint || !(state.targetKind == uint64_t(MagicUseTargetKind::Door)
                    ? mBinding.mNavigatingActor->lineOfSightToDoor(*origin, *endpoint, state.target)
                    : mBinding.mNavigatingActor->lineOfSight(*origin, *endpoint))) return false;
            float distance = 0;
            for (size_t axis = 0; axis < 3; ++axis) distance += ((*endpoint)[axis] - (*origin)[axis]) * ((*endpoint)[axis] - (*origin)[axis]);
            const float reach = effects.hasRange(ESM::RT_Target) ? 2048.f
                : mRuntime.mStore.get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat();
            return distance <= reach * reach && (!effects.hasRange(ESM::RT_Target) || distance >= 64.f);
        };
        if (combat && mBinding.mPlayerCastLifecycle)
            for (size_t owner = 0; owner < 2; ++owner)
            {
                auto& pending = combat->playerCasts[owner];
                if (!pending) continue;
                if (combat->actors[owner][8][2] <= 0 || combat->knockedDown[owner]
                    || hasParalysis(timedEffects, owner) || combat->hitRecoveryTicks[owner]
                    || (pending->phase < ActorCampaignCast::Released && pending->targetKind == 2
                        && (!npcVictim(pending->target)
                            || pending->targetLife != victimLife(npcVictim(pending->target)).generation)))
                { pending.reset(); continue; }
                const auto timing = mBinding.mPlayerCasts[owner].ranges[pending->range];
                // Preserve all-offline/restart and scheduling pauses. A peer keeping
                // the encounter active makes a departing caster cancel independently.
                std::unique_ptr<SpellTransaction> candidate;
                if (pending->phase < ActorCampaignCast::Released)
                {
                    ClientMagicUseCommand use{SessionId::fromValue(1).value(), SessionGeneration::initial(),
                        CommandSequence::initial(), CommandId::fromValue(pending->cast).value(), CanonicalRevision::initial(),
                        MagicUseSourceKind(pending->sourceKind), pending->source, MagicUseTargetKind(pending->targetKind),
                        pending->target, tick};
                    candidate = preparePlayerMagicSource(mBinding.mPlayers[owner], use, *combat, timedEffects, command.get());
                    if (!candidate || uint64_t(candidate->spell.effects.effects.front().mRange) != pending->range)
                    { pending.reset(); continue; }
                }
                // Self travel and already-paid recovery follow their online player,
                // independently of the selected NPC's processing cell.
                const bool travel = pending->phase >= ActorCampaignCast::Released
                    || (mBinding.mPlayerTravel && candidate && playerTravelEffects(candidate->spell.effects));
                const auto position = playerPosition(owner, travel);
                const bool castActive = (travel && position) || active || (combat->actors[2][8][2] <= 0
                    && (playerPosition(0) || playerPosition(1)));
                if (!castActive) continue;
                if (!position || (candidate && !playerTargetValid(*pending, candidate->spell.effects)))
                { pending.reset(); continue; }
                if (candidate && candidate->spell.effects.hasRange(ESM::RT_Target)
                    && projectiles.size() + casts.size() >= MaximumActorProjectiles) continue;
                if (advanceCast(*pending, timing))
                    casts.push_back({magicCaster(owner), candidate->use.sourceKind, candidate->use.sourceId,
                        candidate->use.targetKind, candidate->use.targetId, candidate->use.commandId.value(),
                        candidate->spell, candidate->charge, candidate->effectSource});
                if (pending->elapsed >= timing.stopTicks) pending.reset();
            }
        const auto prepareNpcCast = [&](const ActorMagicCast& use) -> std::optional<Cast>
        {
            if (!mBinding.mDurableCasters || !mBinding.mMagicUse || !mBinding.mMagicProjectile
                || !mBinding.mMagicPlayerTarget || !mBinding.mMagicProjectileCollection || !combat || !life
                || use.actorId != before.mActor || use.life != life->generation || life->respawnTick
                || !use.castId || !use.sourceId || !active || respawn || combat->actors[2][8][2] <= 0
                || combat->knockedDown[2] || hasParalysis(timedEffects, 2) || combat->hitRecoveryTicks[2]
                || (use.targetKind != MagicUseTargetKind::Self && use.targetKind != MagicUseTargetKind::Player)
                || ((use.targetKind == MagicUseTargetKind::Self) != (use.targetId == 0))
                || std::ranges::any_of(projectiles, [&](const auto& pending) {
                    return pending.casterKind == 2 && pending.caster == use.actorId
                        && pending.casterLife == use.life && pending.commandId == use.castId;
                })) return {};
            if (mBinding.mNpcCastLifecycle && use.targetKind == MagicUseTargetKind::Player)
            {
                const auto playerId = PlayerId::fromValue(use.targetId);
                const auto* victim = playerId ? players.findPlayer(*playerId) : nullptr;
                if (!victim || victim->transform().cell() != actorCell(before)
                    || combat->actors[actor(*playerId)][8][2] <= 0
                    || std::ranges::none_of(players.activeSessions(), [&](const auto& session) { return session.playerId() == *playerId; })) return {};
                const auto position = victim->transform().position();
                if (!mBinding.mNavigatingActor->lineOfSight(
                    {before.mPosition[0], before.mPosition[1], before.mPosition[2] + 110.f},
                    {float(double(position.x()) / 1024), float(double(position.y()) / 1024), float(double(position.z()) / 1024) + 110.f})) return {};
                if (!npcDetects(actor(*playerId))) return {};
            }
            const auto context = magicCaster(2);
            const auto caster = loadCombatStats(mRuntime.mStore, combat->actors[2], timedEffects, 2);
            std::optional<PreparedInstantSpell> prepared;
            std::optional<ItemCharge> payment;
            uint64_t effectSource = use.sourceId;
            if (use.sourceKind == MagicUseSourceKind::Spell)
            {
                const auto& known = actorSpells(mRuntime.ownerPtr(context.inventoryOwner)).mList;
                const ESM::Spell* selected = nullptr;
                for (const auto& id : known) if (!id.empty() && spellRecordId(id) == use.sourceId)
                {
                    if (selected) throw std::invalid_argument("Native actor spell identity ambiguous");
                    selected = mRuntime.mStore.get<ESM::Spell>().search(id);
                }
                if (selected) prepared = prepareInstantSpell(*selected, mRuntime.mStore, true,
                    mBinding.mExpandedEffects, mBinding.mSpecialConditions, mBinding.mMovementEffects);
                if (prepared && caster.getMagicka().getCurrent() < prepared->cost) prepared.reset();
            }
            else if (use.sourceKind == MagicUseSourceKind::EnchantedItem && mBinding.mMagicItemUse)
            {
                const auto values = mRuntime.installedValues(context.inventoryOwner);
                const auto item = std::ranges::find_if(values.mObjects, [&](const auto& object) {
                    return object.mRef.mCount > 0 && wireId(object.mRef.mRefNum).value() == use.sourceId;
                });
                if (item != values.mObjects.end() && (!mBinding.mNpcCastLifecycle
                    || std::ranges::find(values.mSlots, item->mRef.mRefNum) != values.mSlots.end()))
                {
                    MWWorld::ManualRef reference(mRuntime.mStore, item->mRef.mRefID);
                    const auto ptr = reference.getPtr();
                    const auto enchantId = ptr.getClass().getEnchantment(ptr);
                    const auto* enchantment = enchantId.empty() ? nullptr : mRuntime.mStore.get<ESM::Enchantment>().search(enchantId);
                    if (enchantment && enchantment->mData.mType == ESM::Enchantment::WhenUsed)
                    {
                        const auto plan = prepareEnchantmentCast(*enchantment, caster, item->mRef.mEnchantmentCharge,
                            mRuntime.mStore, true, mBinding.mExpandedEffects, mBinding.mSpecialConditions,
                            mBinding.mMovementEffects);
                        effectSource = spellRecordId(enchantId);
                        if (plan && plan->affordable && effectSource
                            && enchantmentBySource(mRuntime.mStore, effectSource) == enchantment)
                        {
                            prepared = PreparedInstantSpell{0, plan->effects};
                            payment = ItemCharge{context.inventoryOwner, item->mRef.mRefNum,
                                item->mRef.mEnchantmentCharge, plan->chargeAfter};
                        }
                    }
                }
            }
            if (!prepared || std::ranges::any_of(prepared->effects.effects, [&](const auto& effect) {
                    return (!mBinding.mNpcCastLifecycle && (!effect.mAttribute.empty() || !effect.mSkill.empty()))
                        || (effect.mArea && (!mBinding.mMagicArea || (!mBinding.mNpcCastLifecycle && effect.mRange != ESM::RT_Target)))
                        || (effect.mDuration && !mBinding.mMagicTimed);
                }) || (use.targetKind == MagicUseTargetKind::Self
                    ? !prepared->effects.onlyRange(ESM::RT_Self)
                    : (!prepared->effects.hasRange(ESM::RT_Target) && !(mBinding.mNpcCastLifecycle && prepared->effects.hasRange(ESM::RT_Touch)))
                        || (!mBinding.mNpcCastLifecycle && prepared->effects.hasRange(ESM::RT_Touch))))
                return {};
            if (mBinding.mNpcCastLifecycle && prepared->effects.hasRange(ESM::RT_Touch)
                && !prepared->effects.hasRange(ESM::RT_Target))
            {
                const auto* victim = players.findPlayer(PlayerId::fromValue(use.targetId).value());
                if (!victim) return {};
                const auto position = victim->transform().position();
                float distance = 0.f;
                const std::array<float, 3> endpoint{float(double(position.x()) / 1024), float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
                for (size_t axis = 0; axis < 3; ++axis) distance += std::pow(endpoint[axis] - before.mPosition[axis], 2);
                const float reach = mRuntime.mStore.get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat();
                if (distance > reach * reach) return {};
            }
            return Cast{context, use.sourceKind, use.sourceId, use.targetKind, use.targetId,
                use.castId, std::move(*prepared), payment, effectSource};
        };
        // Known spells compete with the equipped melee weapon in V40.
        // Admission uses the stock AI reaction interval rounded up to server ticks;
        // this is not cast animation timing. The committed tick anchors the cadence
        // across restart and rejected writes, without a second mutable clock.
        constexpr uint64_t reactionTicks = uint64_t(MWMechanics::AI_REACTION_TIME * 30.f + .999f);
        bool automaticCast = bool(casting);
        if (casting)
        {
            auto& state = *casting;
            const auto timing = mBinding.mBoundCasts->ranges[state.range];
            const ActorMagicCast use{state.actor, state.life, state.cast, MagicUseSourceKind(state.sourceKind),
                state.source, MagicUseTargetKind(state.targetKind), state.target};
            if (!life || state.life != life->generation || respawn || !combat || combat->actors[2][8][2] <= 0
                || combat->knockedDown[2] || hasParalysis(timedEffects, 2) || combat->hitRecoveryTicks[2]) casting.reset();
            else if (state.phase >= ActorCampaignCast::Released)
            {
                if (active)
                {
                    advanceCast(state, timing);
                    if (state.elapsed >= timing.stopTicks) casting.reset();
                }
            }
            else if (active)
            {
                // Inactive areas freeze the saved wind-up, including server
                // startup before players rejoin. Revalidate when activity resumes.
                // Rebuild read-only plans from bound content and current source,
                // target/life and visibility before any payment or RNG consumption.
                const auto candidate = prepareNpcCast(use);
                if (!candidate || uint64_t(candidate->record.effects.effects.front().mRange) != state.range)
                    casting.reset();
                else if (candidate->record.effects.hasRange(ESM::RT_Target)
                    && projectiles.size() + casts.size() >= MaximumActorProjectiles) {} // Hold before release; no payment.
                else if (advanceCast(state, timing)) casts.push_back(*candidate);
            }
        }
        struct AiActorContext
        {
            size_t index;
            ActorSceneSnapshot placement;
            uint64_t life;
        };
        const AiActorContext selectedContext{2, before, life ? life->generation : 0};
        const auto npcDisposition = mBinding.mMovementEffects && combat
            ? committedDisposition(mRuntime.mStore, mRuntime.ownerPtr(mCombatNpcOwner).getCellRef().getRefId(),
                timedEffects, 2) : ActorDisposition{};
        if (active && mBinding.mMovementEffects && life)
            selectedFollowing = commandedDestination(2, before,
                life->respawnTick ? 0 : life->generation).has_value();
        const bool npcMayAttack = !npcDisposition.calm && !selectedFollowing
            && (mBinding.mAiDecisions || (npcDisposition.flee < 100
                && (!npcDisposition.fightModified || npcDisposition.fight >= 100)));
        if (combat && mBinding.mAiDecisions && !npcMayAttack)
        { combat->fleeTarget = combat->fleeUntil = 0; combat->fleeDestination = {}; }
        const auto aggressiveAgainst = [&](const AiActorContext& source,
            const ActorDisposition& sourceDisposition, const CanonicalPlayerEntityState& player,
            uint64_t retainedTarget) {
            if (mBinding.mAiDecisions && (!source.life || combat->actors[source.index][8][2] <= 0.f
                    || (mBinding.mPlacementCombat
                        && combat->npcPlacements.at(source.index - 2) != source.placement.mActor)
                    || source.life != (source.index == 2 ? life->generation
                        : neighborLives.at(source.index - 3).generation)
                    || (source.index == 2 ? life->respawnTick != 0
                        : neighborLives.at(source.index - 3).respawnTick != 0))) return false;
            if (!mBinding.mAiDecisions || retainedTarget == player.playerId().value()) return true;
            const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
            const auto actorPtr = mRuntime.ownerPtr(combatOwner(source.index));
            const auto* npc = actorPtr.getType() == ESM::NPC::sRecordId
                ? actorPtr.get<ESM::NPC>() : nullptr;
            float disposition = 50.f;
            const auto& social = combat->players[actor(player.playerId())];
            if (mBinding.mSocialLifecycle
                && std::ranges::any_of(social.engagements, [&](const auto& entry) {
                    return entry.witness == source.placement.mActor;
                })) return true;
            if (npc)
            {
                const auto stats = loadCombatStats(mRuntime.mStore,
                    combat->actors[actor(player.playerId())], timedEffects, actor(player.playerId()));
                disposition = float(npc->mBase->mNpdt.mDisposition) + sourceDisposition.charm;
                const auto playerPtr = mRuntime.ownerPtr(actor(player.playerId()));
                const auto* playerNpc = playerPtr.getType() == ESM::NPC::sRecordId
                    ? playerPtr.get<ESM::NPC>() : nullptr;
                if (playerNpc && npc->mBase->mRace == playerNpc->mBase->mRace)
                    disposition += settings.find("fDispRaceMod")->mValue.getFloat();
                disposition += settings.find("fDispPersonalityMult")->mValue.getFloat()
                    * (stats.getAttribute(ESM::Attribute::Personality).getModified()
                        - settings.find("fDispPersonalityBase")->mValue.getFloat());
                if (mBinding.mPlayerAi && !npc->mBase->mFaction.empty())
                {
                    const auto factionReaction = [&](const ESM::RefId& other) {
                        const auto* faction = mRuntime.mStore.get<ESM::Faction>().find(npc->mBase->mFaction);
                        const auto found = faction->mReactions.find(other);
                        return found == faction->mReactions.end() ? 0 : found->second;
                    };
                    float reaction = 0.f;
                    int rank = 0;
                    const auto same = std::ranges::find_if(social.factions, [&](const auto& entry) {
                        return entry.id == npc->mBase->mFaction;
                    });
                    if (same != social.factions.end())
                    {
                        if (!same->expelled)
                        { reaction = float(factionReaction(same->id)); rank = same->rank; }
                    }
                    else
                        for (size_t i = 0; i < social.factions.size(); ++i)
                        {
                            const auto& entry = social.factions[i];
                            if (entry.expelled) continue;
                            const auto candidate = float(factionReaction(entry.id));
                            if (i == 0 || candidate < reaction)
                            { reaction = candidate; rank = entry.rank; }
                        }
                    disposition += MWMechanics::factionDisposition(rank, reaction,
                        settings.find("fDispFactionRankMult")->mValue.getFloat(),
                        settings.find("fDispFactionRankBase")->mValue.getFloat(),
                        settings.find("fDispFactionMod")->mValue.getFloat());
                }
                if (mBinding.mPlayerAi)
                {
                    disposition += MWMechanics::crimeDisposition(social.crimeDisposition,
                        social.bounty, settings.find("fDispCrimeMod")->mValue.getFloat());
                    disposition += MWMechanics::weaponDrawnDisposition(
                        social.drawState == uint64_t(MWMechanics::DrawState::Weapon),
                        settings.find("fDispWeaponDrawn")->mValue.getFloat());
                }
                if (std::ranges::any_of(combat->conditions, [&](const auto& condition) {
                    if (condition.actor != actor(player.playerId())) return false;
                    const auto* spell = spellBySource(mRuntime.mStore, condition.source);
                    return spell && (spell->mData.mType == ESM::Spell::ST_Disease
                        || spell->mData.mType == ESM::Spell::ST_Blight);
                })) disposition += settings.find("fDispDiseaseMod")->mValue.getFloat();
                disposition = float(MWMechanics::dispositionWithCharm(disposition, 0.f));
            }
            const auto position = player.transform().position();
            const float dx = float(double(position.x()) / 1024) - source.placement.mPosition[0];
            const float dy = float(double(position.y()) / 1024) - source.placement.mPosition[1];
            const float dz = float(double(position.z()) / 1024) - source.placement.mPosition[2];
            const float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
            const float distanceBias = MWMechanics::fightDistanceBias(distance,
                settings.find("iFightDistanceBase")->mValue.getInteger(),
                settings.find("fFightDistanceMultiplier")->mValue.getFloat());
            const float dispositionBias = MWMechanics::fightDispositionBias(disposition,
                settings.find("fFightDispMult")->mValue.getFloat());
            const int fight = sourceDisposition.fight + (mBinding.mPlayerAi && npc
                && (social.werewolf || social.knownWerewolf)
                ? MWMechanics::werewolfFight(social.werewolf, social.knownWerewolf,
                    settings.find("iWerewolfFightMod")->mValue.getInteger()) : 0);
            return MWMechanics::aggressiveAtDistance(fight, distanceBias, dispositionBias);
        };
        if (mBinding.mPlayerAi && combat)
            for (size_t owner = 0; owner < reportedAssaultContact.size(); ++owner)
            {
                if (!reportedAssaultContact[owner]) continue;
                const auto* player = players.findPlayer(mBinding.mPlayers[owner]);
                if (!player || aggressiveAgainst(selectedContext, npcDisposition, *player, target)) continue;
                bool reported = !mBinding.mSocialLifecycle;
                if (mBinding.mSocialLifecycle)
                {
                    const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
                    auto& engagements = combat->players[owner].engagements;
                    crimeWitnesses(owner, true, [&](const auto& witness, const ESM::NPC& npc,
                            bool victim, float distance) {
                        if (std::ranges::any_of(engagements, [&](const auto& entry) {
                            return entry.witness == witness.placement;
                        })) return;
                        const int alarm = std::clamp(int(npc.mAiData.mAlarm), 0, 100);
                        const int baseFight = std::clamp(int(npc.mAiData.mFight), 0, 100);
                        reported |= alarm >= 100;
                        const float disposition = float(npc.mNpdt.mDisposition)
                            + MWMechanics::crimeDisposition(combat->players[owner].crimeDisposition,
                                combat->players[owner].bounty,
                                settings.find("fDispCrimeMod")->mValue.getFloat());
                        const float distanceBias = MWMechanics::fightDistanceBias(distance,
                            settings.find("iFightDistanceBase")->mValue.getInteger(),
                            settings.find("fFightDistanceMultiplier")->mValue.getFloat());
                        const float dispositionBias = MWMechanics::fightDispositionBias(disposition,
                            settings.find("fFightDispMult")->mValue.getFloat());
                        const int provocation = settings.find(victim ? "iFightAttack" : "iFightAttacking")
                            ->mValue.getInteger();
                        const float rawTerm = (float(provocation) + distanceBias + dispositionBias)
                            * .01f * float(alarm);
                        if (!std::isfinite(rawTerm))
                            throw std::invalid_argument("Native crime engagement term invalid");
                        const float term = std::clamp(rawTerm, 0.f, float(100 - baseFight));
                        const int fight = baseFight + int(term);
                        if (fight < 100) return;
                        if (engagements.size() >= 128)
                            throw std::invalid_argument("Native crime engagement capacity exceeded");
                        engagements.insert(std::lower_bound(engagements.begin(), engagements.end(),
                            witness.placement, [](const auto& entry, uint64_t id) {
                                return entry.witness < id;
                            }), {witness.placement, tick.value(), fight});
                    });
                }
                if (!reported) continue;
                const int bounty = MWMechanics::reportedAssaultBounty(
                    mRuntime.mStore.get<ESM::GameSetting>());
                if (bounty < 0 || bounty > 10'000'000 - combat->players[owner].bounty)
                    throw std::invalid_argument("Native reported assault bounty out of bounds");
                combat->players[owner].bounty += bounty;
                const auto npc = mRuntime.ownerPtr(mCombatNpcOwner).get<ESM::NPC>();
                const auto faction = std::ranges::find(combat->players[owner].factions,
                    npc->mBase->mFaction, &ActorCampaignCombat::PlayerAi::Faction::id);
                if (faction != combat->players[owner].factions.end()) faction->expelled = true;
            }
        if (mBinding.mWeaponMelee && target && !npcMayAttack)
        { melee = mIdleMelee; target = 0; contact = false; }
        if (mBinding.mAutomaticNpcSpells && npcMayAttack && !automaticCast && !actorCast && step && active && !respawn && combat && life
            && !life->respawnTick && combat->actors[2][8][2] > 0
            && !combat->knockedDown[2] && !hasParalysis(timedEffects, 2) && !combat->hitRecoveryTicks[2]
            && tick.value() / reactionTicks != mActorTick / reactionTicks
            && (mBinding.mNpcWeaponCompetition || !mRuntime.equippedWeaponCondition(mCombatNpcOwner))
            && (!melee || (mBinding.mWeaponMelee ? !target : !melee->snapshot().mReleased)
                || melee->snapshot().mPhase == MeleeAnimation::Phase::Complete)
            && std::ranges::none_of(projectiles, [](const auto& flight) { return flight.casterKind == 2; }))
        {
            const CanonicalPlayerEntityState* enemy = nullptr;
            float nearest = std::numeric_limits<float>::infinity();
            for (const auto& playerId : mBinding.mPlayers)
            {
                const auto* player = players.findPlayer(playerId);
                if (!player || player->transform().cell() != actorCell(before)
                    || combat->actors[actor(playerId)][8][2] <= 0
                    || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                        return session.playerId() == playerId;
                    })) continue;
                const auto place = player->transform().position();
                const float dx = float(double(place.x()) / 1024) - before.mPosition[0];
                const float dy = float(double(place.y()) / 1024) - before.mPosition[1];
                const float dz = float(double(place.z()) / 1024) + 64.f - (before.mPosition[2] + 55.f);
                const float distance = dx*dx + dy*dy + dz*dz;
                if (distance < 8.f*8.f || distance > 2048.f*2048.f) continue;
                if (!aggressiveAgainst(selectedContext, npcDisposition, *player, target)) continue;
                if (mBinding.mNpcFullSelection && !mBinding.mNavigatingActor->lineOfSight(
                    {before.mPosition[0], before.mPosition[1], before.mPosition[2] + 110.f},
                    {float(double(place.x()) / 1024), float(double(place.y()) / 1024),
                        float(double(place.z()) / 1024) + 110.f})) continue;
                if (!npcDetects(actor(playerId))) continue;
                if (distance < nearest || (distance == nearest && enemy && playerId < enemy->playerId()))
                { enemy = player; nearest = distance; }
            }
            if (enemy)
            {
                auto caster = loadCombatStats(mRuntime.mStore, combat->actors[2], timedEffects, 2);
                auto victim = loadCombatStats(mRuntime.mStore, combat->actors[actor(enemy->playerId())],
                    timedEffects, actor(enemy->playerId()));
                addTimedResistance(caster, timedEffects, 2);
                addTimedResistance(victim, timedEffects, actor(enemy->playerId()));
                if (mBinding.mKnockoutRules) victim.setKnockedDown(combat->knockedDown[actor(enemy->playerId())]);
                // The native player casting lifecycle supplies the authoritative
                // spell activity used by stock Silence/Sound target priorities.
                if (mBinding.mPlayerCastLifecycle && combat->playerCasts[actor(enemy->playerId())])
                    victim.setDrawState(MWMechanics::DrawState::Spell);
                AiMagicContext selection{caster, &victim};
                selection.expandedEffects = mBinding.mExpandedEffects;
                selection.specialConditions = mBinding.mSpecialConditions;
                for (const auto& effect : timedEffects)
                {
                    if (effect.durationTicks <= 90) continue;
                    const auto id = ESM::MagicEffect::indexToRefId(int(effect.effectIndex));
                    const bool harmful = mRuntime.mStore.get<ESM::MagicEffect>().find(id)->mData.mFlags & ESM::MagicEffect::Harmful;
                    if (harmful && effect.actor == 2)
                    {
                        if (id == ESM::MagicEffect::Poison) ++selection.selfCures[0];
                        if (id == ESM::MagicEffect::Paralyze) ++selection.selfCures[1];
                    }
                    if (effect.sourceKind) continue;
                    if (effect.actor == 2) ++selection.selfDispel[harmful ? 1 : 0];
                    if (effect.actor == actor(enemy->playerId())) ++selection.enemyDispel[harmful ? 1 : 0];
                }
                selection.enemyWerewolf = victim.isWerewolf();
                std::optional<float> weaponRating = 0.f;
                const ESM::ObjectState* winningWeapon = nullptr;
                const auto values = mRuntime.installedValues(mCombatNpcOwner);
                float arrowRating = 0.f, boltRating = 0.f;
                if (mBinding.mNpcFullSelection)
                {
                    // Player contacts still use the bounded body proxy. Keep its
                    // reach basis consistent with authoritative melee validation.
                    float reach = mRuntime.mStore.get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat();
                    const auto enemyValues = mRuntime.installedValues(actor(enemy->playerId()));
                    const auto right = enemyValues.mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
                    const auto held = std::ranges::find(enemyValues.mObjects, right,
                        [](const auto& object) { return object.mRef.mRefNum; });
                    if (held != enemyValues.mObjects.end()
                        && mRuntime.mStore.find(held->mRef.mRefID) == ESM::Weapon::sRecordId)
                        reach *= mRuntime.mStore.get<ESM::Weapon>().find(held->mRef.mRefID)->mData.mReach;
                    selection.outsideEnemyReach = std::sqrt(nearest) - 64.f >= reach;
                    for (const auto& item : values.mObjects)
                    {
                        if (item.mRef.mCount <= 0 || mRuntime.mStore.find(item.mRef.mRefID) != ESM::Weapon::sRecordId) continue;
                        const auto* weapon = mRuntime.mStore.get<ESM::Weapon>().find(item.mRef.mRefID);
                        if (weapon->mData.mType != ESM::Weapon::Arrow && weapon->mData.mType != ESM::Weapon::Bolt) continue;
                        const auto rating = rateAiWeapon(selection, *weapon, 0, item.mRef.mEnchantmentCharge,
                            mBinding.mEnchantedWeaponsAreMagical, mRuntime.mStore);
                        if (!rating) { weaponRating.reset(); break; }
                        auto& best = weapon->mData.mType == ESM::Weapon::Arrow ? arrowRating : boltRating;
                        best = std::max(best, *rating);
                    }
                }
                for (const auto& item : values.mObjects)
                {
                    if (!weaponRating) break;
                    if (item.mRef.mCount <= 0 || mRuntime.mStore.find(item.mRef.mRefID) != ESM::Weapon::sRecordId
                        || (!mBinding.mNpcFullSelection && item.mRef.mRefNum != values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight])) continue;
                    const auto* weapon = mRuntime.mStore.get<ESM::Weapon>().find(item.mRef.mRefID);
                    const auto weaponClass = MWMechanics::getWeaponType(weapon->mData.mType)->mWeaponClass;
                    if (weaponClass == ESM::WeaponType::Ammo) continue;
                    if (!mBinding.mNpcFullSelection && weaponClass != ESM::WeaponType::Melee)
                    { weaponRating.reset(); break; }
                    const auto rating = rateAiWeapon(selection, *weapon,
                        item.mRef.mChargeInt < 0 ? weapon->mData.mHealth : item.mRef.mChargeInt,
                        item.mRef.mEnchantmentCharge, mBinding.mEnchantedWeaponsAreMagical, mRuntime.mStore,
                        arrowRating, boltRating);
                    if (!rating) { weaponRating.reset(); break; }
                    if (*rating > *weaponRating || (*rating > 0 && *rating == *weaponRating
                            && item.mRef.mRefNum == values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight]))
                    { weaponRating = *rating; winningWeapon = &item; }
                }
                // An unsupported weapon retains the existing combat path.
                // Do not pretend it has zero value and favor magic accidentally.
                selection.weaponRating = weaponRating.value_or(0.f);
                const auto ptr = mRuntime.ownerPtr(mCombatNpcOwner);
                const auto& known = actorSpells(ptr).mList;
                if (known.size() > MaximumAiMagicSources)
                    throw std::invalid_argument("Automatic NPC spell budget exceeded");
                const auto* race = ptr.getType() == ESM::NPC::sRecordId
                    ? mRuntime.mStore.get<ESM::Race>().find(ptr.get<ESM::NPC>()->mBase->mRace) : nullptr;
                std::vector<AiMagicSpell> sources;
                sources.reserve(known.size());
                for (const auto& id : known)
                {
                    const auto* spell = mRuntime.mStore.get<ESM::Spell>().search(id);
                    if (!spell) continue;
                    const auto plan = prepareInstantSpell(*spell, mRuntime.mStore, true,
                        mBinding.mExpandedEffects, mBinding.mSpecialConditions, mBinding.mMovementEffects);
                    // Filter whole sources before rating: an unsupported winner
                    // must not hide a lower-ranked executable spell.
                    if (!plan || (plan->effects.hasRange(ESM::RT_Touch) && (!mBinding.mNpcCastLifecycle
                        || (!plan->effects.hasRange(ESM::RT_Target) && nearest > std::pow(mRuntime.mStore.get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat(), 2))))
                        || std::ranges::any_of(plan->effects.effects, [&](const auto& effect) {
                            return (!mBinding.mNpcCastLifecycle && (!effect.mAttribute.empty() || !effect.mSkill.empty()))
                        || (effect.mArea && (!mBinding.mMagicArea || (!mBinding.mNpcCastLifecycle && effect.mRange != ESM::RT_Target)))
                                || (effect.mDuration && !mBinding.mMagicTimed);
                        }) || (plan->effects.hasRange(ESM::RT_Target)
                            && projectiles.size() + casts.size() >= MaximumActorProjectiles)) continue;
                    const auto activeOn = [&](size_t index) {
                        return std::ranges::any_of(timedEffects, [&](const auto& effect) {
                            return effect.actor == index && effect.sourceKind == uint64_t(MagicUseSourceKind::Spell)
                                && effect.source == spellRecordId(id) && effect.casterKind == 2
                                && effect.caster == before.mActor && effect.casterLife == life->generation;
                        });
                    };
                    sources.push_back({spell, race && race->mPowers.exists(id), activeOn(2), activeOn(actor(enemy->playerId()))});
                }
                std::vector<AiMagicItem> items;
                if (mBinding.mNpcFullSelection) for (const auto& item : values.mObjects)
                {
                    if (item.mRef.mCount <= 0 || std::ranges::find(values.mSlots, item.mRef.mRefNum) == values.mSlots.end()) continue;
                    const auto record = MWWorld::inventoryItemRecord(mRuntime.mStore, item.mRef.mRefID);
                    if (!record.mWhenUsed) continue;
                    const auto* enchantment = mRuntime.mStore.get<ESM::Enchantment>().find(record.mEnchant);
                    const auto plan = prepareEnchantmentCast(*enchantment, caster, item.mRef.mEnchantmentCharge,
                        mRuntime.mStore, true, mBinding.mExpandedEffects, mBinding.mSpecialConditions,
                        mBinding.mMovementEffects);
                    if (!plan || (!mBinding.mNpcCastLifecycle && std::ranges::any_of(plan->effects.effects,
                            [](const auto& effect) { return !effect.mAttribute.empty() || !effect.mSkill.empty(); })) || (plan->effects.hasRange(ESM::RT_Touch) && (!mBinding.mNpcCastLifecycle
                        || (!plan->effects.hasRange(ESM::RT_Target) && nearest > std::pow(mRuntime.mStore.get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat(), 2))))
                        || (plan->effects.hasRange(ESM::RT_Target) && projectiles.size() + casts.size() >= MaximumActorProjectiles)) continue;
                    bool activeSelf = false;
                    float enemyDuration = 0.f;
                    for (const auto& effect : timedEffects)
                        if (effect.sourceKind == uint64_t(MagicUseSourceKind::EnchantedItem)
                            && effect.source == spellRecordId(record.mEnchant) && effect.casterKind == 2
                            && effect.caster == before.mActor && effect.casterLife == life->generation)
                        {
                            activeSelf |= effect.actor == 2;
                            if (effect.actor == actor(enemy->playerId()))
                                enemyDuration = std::max(enemyDuration, float(effect.expiresTick - tick.value()) / 30.f);
                        }
                    items.push_back({item.mRef.mRefNum, record.mEnchant, item.mRef.mEnchantmentCharge, true, activeSelf, enemyDuration});
                }
                const auto selected = weaponRating
                    ? prepareAiMagicCast(selection, sources, items, mRuntime.mStore) : std::nullopt;
                bool flee = false;
                if (mBinding.mAiDecisions && weaponRating)
                {
                    const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
                    const float distanceBias = MWMechanics::fightDistanceBias(std::sqrt(nearest),
                        settings.find("iFightDistanceBase")->mValue.getInteger(),
                        settings.find("fFightDistanceMultiplier")->mValue.getFloat());
                    const float rating = MWMechanics::fleeRating(npcDisposition.flee,
                        caster.getHealth().getRatio(false),
                        settings.find("fAIFleeHealthMult")->mValue.getFloat(),
                        settings.find("fAIFleeFleeMult")->mValue.getFloat(), distanceBias);
                    flee = MWMechanics::fleeOverAttack(rating,
                        selected ? selected->rating : *weaponRating);
                }
                if (flee)
                {
                    const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
                    melee = mIdleMelee; target = 0; contact = false;
                    if (tick.value() > UINT64_MAX - 30)
                        throw std::invalid_argument("Native flee deadline exhausted");
                    const auto enemyIndex = actor(enemy->playerId());
                    const auto enemyValues = combatEquipmentValues(enemyIndex, command.get());
                    const auto right = enemyValues.mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
                    const auto held = std::ranges::find_if(enemyValues.mObjects,
                        [&](const auto& item) { return item.mRef.mRefNum == right; });
                    const auto* weapon = held == enemyValues.mObjects.end() ? nullptr
                        : mRuntime.mStore.get<ESM::Weapon>().search(held->mRef.mRefID);
                    float attackDistance = 1.f;
                    const auto targetSpeed = [&](const auto& effects) {
                        for (const auto& effect : effects.mList)
                            if (effect.mData.mRange == ESM::RT_Target)
                                return mRuntime.mStore.get<ESM::MagicEffect>()
                                    .find(effect.mData.mEffectID)->mData.mSpeed;
                        return 1.f;
                    };
                    const auto& selection = combat->players[enemyIndex];
                    if (mBinding.mPlayerAi && !weapon && !selection.selectedSpell.empty()
                        && selection.selectedEnchantedItem)
                        attackDistance = settings.find("fHandToHandReach")->mValue.getFloat();
                    else if (mBinding.mPlayerAi && selection.drawState == uint64_t(MWMechanics::DrawState::Spell))
                    {
                        if (!selection.selectedSpell.empty())
                            attackDistance = targetSpeed(mRuntime.mStore.get<ESM::Spell>()
                                .find(selection.selectedSpell)->mEffects);
                        else if (selection.selectedEnchantedItem)
                        {
                            const auto item = std::ranges::find_if(enemyValues.mObjects, [&](const auto& value) {
                                return wireId(value.mRef.mRefNum).value() == selection.selectedEnchantedItem;
                            });
                            if (item != enemyValues.mObjects.end())
                            {
                                const auto record = MWWorld::inventoryItemRecord(mRuntime.mStore, item->mRef.mRefID);
                                if (!record.mEnchant.empty())
                                    attackDistance = targetSpeed(mRuntime.mStore.get<ESM::Enchantment>()
                                        .find(record.mEnchant)->mEffects);
                            }
                        }
                        attackDistance *= std::max(1000.f,
                            settings.find("fTargetSpellMaxSpeed")->mValue.getFloat());
                    }
                    else if (!mBinding.mPlayerAi && combat->playerCasts[enemyIndex])
                    {
                        const auto& cast = combat->playerCasts[enemyIndex];
                        if (cast->sourceKind == uint64_t(MagicUseSourceKind::Spell))
                        {
                            if (const auto* spell = spellBySource(mRuntime.mStore, cast->source))
                                attackDistance = targetSpeed(spell->mEffects);
                        }
                        else if (cast->sourceKind == uint64_t(MagicUseSourceKind::EnchantedItem))
                        {
                            const auto item = std::ranges::find_if(enemyValues.mObjects,
                                [&](const auto& value) { return wireId(value.mRef.mRefNum).value() == cast->source; });
                            if (item != enemyValues.mObjects.end())
                            {
                                const auto record = MWWorld::inventoryItemRecord(mRuntime.mStore, item->mRef.mRefID);
                                if (!record.mEnchant.empty())
                                    if (const auto* enchantment = mRuntime.mStore.get<ESM::Enchantment>()
                                            .search(record.mEnchant))
                                        attackDistance = targetSpeed(enchantment->mEffects);
                            }
                        }
                        attackDistance *= std::max(1000.f,
                            settings.find("fTargetSpellMaxSpeed")->mValue.getFloat());
                    }
                    else if (weapon)
                    {
                        if (MWMechanics::getWeaponType(weapon->mData.mType)->mWeaponClass != ESM::WeaponType::Melee)
                        {
                            attackDistance = settings.find("fProjectileMaxSpeed")->mValue.getFloat();
                            const auto ammoSlot = enemyValues.mSlots[MWWorld::InventoryStore::Slot_Ammunition];
                            const auto ammo = std::ranges::find_if(enemyValues.mObjects,
                                [&](const auto& item) { return item.mRef.mRefNum == ammoSlot; });
                            if (ammo != enemyValues.mObjects.end())
                                if (const auto* record = mRuntime.mStore.get<ESM::Weapon>().search(ammo->mRef.mRefID))
                                    attackDistance *= record->mData.mSpeed;
                        }
                        else if (weapon->mData.mReach > 1) attackDistance = weapon->mData.mReach;
                    }
                    attackDistance = std::max(attackDistance, 1.f);
                    const float combatDistance = settings.find("fCombatDistance")->mValue.getFloat()
                        * ((mBinding.mPlayerAi ? selection.werewolf : victim.isWerewolf()) ? 1.f
                            + settings.find("fCombatDistanceWerewolfMod")->mValue.getFloat() : 1.f);
                    if (!std::isfinite(attackDistance) || attackDistance > 1e7f
                        || !std::isfinite(combatDistance) || combatDistance <= 0.f
                        || combatDistance > 1e7f)
                        throw std::invalid_argument("Native flee attack distance outside bounds");
                    if (attackDistance < combatDistance) attackDistance *= combatDistance;
                    const auto enemyPlace = enemy->transform().position();
                    const std::array<float, 3> enemyPosition{float(double(enemyPlace.x()) / 1024),
                        float(double(enemyPlace.y()) / 1024), float(double(enemyPlace.z()) / 1024)};
                    const float x = enemyPosition[0] - before.mPosition[0];
                    const float y = enemyPosition[1] - before.mPosition[1];
                    const float z = enemyPosition[2] - before.mPosition[2];
                    const auto enemyPtr = mRuntime.ownerPtr(enemyIndex);
                    const auto* enemyNpc = enemyPtr.get<ESM::NPC>();
                    const bool trigger = MWMechanics::fleeWithinAttackDistance(
                        mBinding.mNavigatingActor->lineOfSight(
                            {before.mPosition[0], before.mPosition[1], before.mPosition[2] + 110.f},
                            {enemyPosition[0], enemyPosition[1], enemyPosition[2] + 110.f}),
                        attackDistance, std::sqrt(x*x + y*y + z*z),
                        mBinding.mNavigatingActor->selectedActorHalfExtentY(),
                        mBinding.mNavigatingActor->npcHalfExtentY(enemyNpc->mBase->mRace,
                            enemyPtr.getCellRef().getScale()));
                    if (trigger && (combat->fleeTarget != enemy->playerId().value()
                        || combat->fleeUntil <= tick.value()))
                    {
                        std::array<float, 3> destination{};
                        const auto fleeingActor = mRuntime.ownerPtr(mCombatNpcOwner);
                        const auto points = fleeingActor.getClass().isPureWaterCreature(fleeingActor)
                            ? std::vector<std::array<float, 3>>{}
                            : mBinding.mNavigatingActor->fleePathgridDestinations();
                        if (!points.empty())
                        {
                            Misc::Rng::Generator rng{combat->rng};
                            destination = points[Misc::Rng::rollDice(points.size(), rng)];
                            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                        }
                        combat->fleeTarget = enemy->playerId().value();
                        combat->fleeUntil = tick.value() + 30;
                        combat->fleeDestination = destination;
                    }
                    else if (trigger && combat->fleeDestination != std::array<float, 3>{})
                        combat->fleeUntil = tick.value() + 30;
                }
                else if (selected)
                {
                    if (mBinding.mAiDecisions)
                    { combat->fleeTarget = combat->fleeUntil = 0; combat->fleeDestination = {}; }
                    const bool self = selected->spell.effects.onlyRange(ESM::RT_Self);
                    actorCast = ActorMagicCast{before.mActor, life->generation, tick.value(),
                        selected->payment ? MagicUseSourceKind::EnchantedItem : MagicUseSourceKind::Spell,
                        selected->payment ? wireId(selected->payment->instance).value() : spellRecordId(selected->effectSource),
                        self ? MagicUseTargetKind::Self : MagicUseTargetKind::Player,
                        self ? 0 : enemy->playerId().value()};
                    automaticCast = true;
                }
                else if (mBinding.mWeaponMelee && weaponRating)
                {
                    if (mBinding.mAiDecisions)
                    { combat->fleeTarget = combat->fleeUntil = 0; combat->fleeDestination = {}; }
                    // A completed swing returns to selection on the same durable
                    // reaction cadence as casting. Never execute the old weapon
                    // while the winning carried item waits for its equipment tick.
                    melee = mIdleMelee; target = 0; contact = false;
                    const auto* weapon = winningWeapon
                        ? mRuntime.mStore.get<ESM::Weapon>().find(winningWeapon->mRef.mRefID) : nullptr;
                    const bool ranged = rangedWeapon(weapon, mBinding.mRangedRelease);
                    if (ranged && !mBinding.mRangedFlight)
                        throw std::invalid_argument("Native NPC ranged selection has unsupported source");
                    if (winningWeapon && winningWeapon->mRef.mRefNum
                        != values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight])
                    {
                        if (!command)
                        {
                            // Validate the executable clip before preparing any equipment.
                            if (ranged)
                                (void)mBinding.mWeaponMelee(weapon, "shoot");
                            else if (mBinding.mGeneralAttackModes)
                                for (const auto mode : {"chop", "slash", "thrust"})
                                    (void)mBinding.mWeaponMelee(weapon, mode);
                            else (void)mBinding.mWeaponMelee(weapon, {});
                            auto prepared = mRuntime.prepareNpcEquipment(mCombatNpcOwner, winningWeapon->mRef.mRefNum);
                            const auto equipped = mRuntime.preparedValues(prepared, mCombatNpcOwner);
                            Misc::Rng::Generator rng{combat->rng};
                            const auto previous = timedEffects;
                            if (reconcileConstants(equipped, 2, magicCaster(2).identity, tick.value(), mRuntime.mStore,
                                    mBinding.mGeneralConstants, timedEffects, &rng, mBinding.mExpandedEffects,
                                    mBinding.mSpecialConditions, mBinding.mMovementEffects,
                                    mBinding.mAiDecisions, aiNpc, aiUndead, levitationEnabled))
                                updateResources(2, previous, timedEffects, mBinding.mKnockoutAnimation);
                            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                            command = std::make_unique<EquipmentTransaction>(*this, players, std::nullopt, std::move(prepared));
                        }
                    }
                    else if (ranged && !equippedAmmunition(values, mRuntime.mStore, *weapon,
                        mBinding)
                        && weapon->mData.mType != ESM::Weapon::MarksmanThrown)
                    {
                        const auto ammunition = std::ranges::find_if(values.mObjects, [&](const auto& item) {
                            if (item.mRef.mCount <= 0 || mRuntime.mStore.find(item.mRef.mRefID) != ESM::Weapon::sRecordId)
                                return false;
                            return rangedSources(*weapon, *mRuntime.mStore.get<ESM::Weapon>().find(item.mRef.mRefID),
                                mRuntime.mStore, mBinding);
                        });
                        if (ammunition != values.mObjects.end() && !command)
                        {
                            auto prepared = mRuntime.prepareNpcEquipment(mCombatNpcOwner, ammunition->mRef.mRefNum,
                                MWWorld::InventoryStore::Slot_Ammunition);
                            command = std::make_unique<EquipmentTransaction>(*this, players, std::nullopt, std::move(prepared));
                        }
                    }
                    else if (weapon || !values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight].isSet())
                    {
                        Misc::Rng::Generator rng{combat->rng};
                        const auto mode = ranged ? std::string_view("shoot") : mBinding.mGeneralAttackModes
                            ? MWMechanics::chooseMeleeAttack(weapon, rng) : std::string_view{};
                        melee = mBinding.mWeaponMelee(weapon, mode);
                        if (mBinding.mActorPresentation || ranged) combat->npcAction = tick.value();
                        combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                        target = enemy->playerId().value();
                    }
                }
            }
        }
        if (actorCast)
        {
            const auto candidate = prepareNpcCast(*actorCast);
            if (!candidate) throw std::invalid_argument("Native actor cast source or target became invalid");
            breakInvisibility(2);
            if (mBinding.mNpcCastLifecycle)
            {
                if (casting) throw std::invalid_argument("Native actor already casting");
                const auto& use = *actorCast;
                casting = ActorCampaignCast{use.actorId, use.life, use.castId, uint64_t(use.sourceKind), use.sourceId,
                    uint64_t(use.targetKind), use.targetId, uint64_t(candidate->record.effects.effects.front().mRange)};
                automaticCast = true;
            }
            else casts.push_back(*candidate);
        }
        if (mBinding.mWeaponMelee && automaticCast)
        { melee = mIdleMelee; target = 0; contact = false; }
        const auto applyContactEffects = [&](size_t index, const PreparedInstantEffects& effects, int range,
            ActorCasterIdentity identity, MagicUseSourceKind sourceKind, uint64_t source, uint64_t effectSource,
            Misc::Rng::Generator& rng, std::span<const uint64_t> ordinals = {}) {
            auto victim = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
            addTimedResistance(victim, timedEffects, index);
            const auto result = resolveEffects(effects, range,
                index, victim, tick.value(),
                identity, effectSource, uint64_t(sourceKind), rng,
                mRuntime.mStore, timedEffects, mBinding.mActorEffectLifecycle,
                mBinding.mUncappedDamageFatigue, ordinals);
            saveCombatStats(combat->actors[index], victim, timedEffects, index);
            if (mBinding.mKnockoutRules)
            {
                combat->knockedDown[index] = victim.getHealth().getCurrent() > 0
                    && (fatigueKnockout(victim, mBinding.mKnockoutAnimation)
                        || (mBinding.mKnockoutAnimation && combat->knockedDown[index]));
                if (index == 2 && (combat->knockedDown[2] || hasParalysis(timedEffects, 2)))
                {
                    step.reset(); after = before; report.status = Diagnostics::Status::Idle;
                    melee = mIdleMelee;
                    target = 0; contact = false;
                }
            }
            const bool died = victim.getHealth().getCurrent() <= 0;
            if (died && index >= 2) recordNpcDeath(index, identity);
            spellCasts.push_back(MagicUseCombatEvent{wireCaster({identity.id, identity.kind, identity.life}),
                sourceKind, source,
                index >= 2 ? MagicUseTargetKind::Actor : MagicUseTargetKind::Player,
                index >= 2 ? (index == 2 ? before.mActor : combat->npcPlacements.at(index - 2))
                    : mBinding.mPlayers[index].value(),
                CombatRevision::fromValue(tick.value()).value(),
                CombatRevision::fromValue(tick.value()).value(), true,
                0.f, 0.f, 0.f, result.health, result.fatigue, result.magicka, died, std::max(uint64_t(1), identity.life)});
        };
        const auto combatPosition = [&](size_t index, bool travel = false) -> std::optional<std::array<float, 3>> {
            if (index == 2)
                return std::array<float, 3>{before.mPosition[0], before.mPosition[1], before.mPosition[2] + 55.f};
            if (index > 2)
            {
                const auto scene = victimScene(index);
                return std::array<float, 3>{scene.mPosition[0], scene.mPosition[1], scene.mPosition[2] + 55.f};
            }
            const auto* player = players.findPlayer(mBinding.mPlayers[index]);
            if (!player || (travel ? !worldDomain(player->transform().cell())
                                  : player->transform().cell() != actorCell(before))
                || std::ranges::none_of(players.activeSessions(), [&](const auto& session) { return session.playerId() == player->playerId(); })) return {};
            const auto position = player->transform().position();
            return std::array<float, 3>{float(double(position.x()) / 1024), float(double(position.y()) / 1024), float(double(position.z()) / 1024) + 64.f};
        };
        for (auto cast : casts)
        {
            const auto& context = cast.context;
            if (mBinding.mPlayerCastLifecycle && context.combatIndex < 2)
            {
                ClientMagicUseCommand use{SessionId::fromValue(1).value(), SessionGeneration::initial(),
                    CommandSequence::initial(), CommandId::fromValue(cast.commandId).value(), CanonicalRevision::initial(),
                    cast.sourceKind, cast.sourceId, cast.targetKind, cast.targetId, tick};
                auto current = preparePlayerMagicSource(mBinding.mPlayers[context.combatIndex], use, *combat, timedEffects, command.get());
                if (!current) { combat->playerCasts[context.combatIndex].reset(); continue; }
                cast.record = std::move(current->spell); cast.charge = current->charge; cast.effectSource = current->effectSource;
            }
            const auto& spellRecord = cast.record;
            const auto& spellCharge = cast.charge;
            const auto spellEffectSource = cast.charge ? cast.effectSource : cast.sourceId;
            const size_t owner = context.combatIndex;
            if (combat->actors[owner][8][2] <= 0 || combat->knockedDown[owner] || hasParalysis(timedEffects, owner)
                || (mBinding.mKnockoutAnimation && combat->hitRecoveryTicks[owner])
                || (owner == 2 && cast.targetKind == MagicUseTargetKind::Player
                    && combat->actors[actor(PlayerId::fromValue(cast.targetId).value())][8][2] <= 0))
            {
                // Earlier effects or combat can incapacitate an admitted caster.
                // Cancel only this launch: rolling back the tick also erases its
                // cause and unrelated progress, so retries can stall simulation.
                if (owner == 2) casting.reset();
                else if (mBinding.mPlayerCastLifecycle) combat->playerCasts[owner].reset();
                spellCasts.push_back(MagicUseCombatEvent{wireCaster(context.identity), cast.sourceKind, cast.sourceId,
                    cast.targetKind, cast.targetId, CombatRevision::fromValue(tick.value()).value(),
                    CombatRevision::fromValue(tick.value()).value(), false,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false, std::max(uint64_t(1), context.identity.life)});
                continue;
            }
            if (mBinding.mPlayerCastLifecycle && owner < 2
                && (!combat->playerCasts[owner] || !playerTargetValid(*combat->playerCasts[owner], spellRecord.effects)
                    || (spellRecord.effects.hasRange(ESM::RT_Target) && projectiles.size() >= MaximumActorProjectiles)
                    || combat->actors[owner][9][2] < spellRecord.cost))
            { combat->playerCasts[owner].reset(); continue; }
            auto caster = loadCombatStats(mRuntime.mStore, combat->actors[owner], timedEffects, owner);
            addTimedResistance(caster, timedEffects, owner);
            Misc::Rng::Generator rng;
            Misc::Rng::deserialize(std::to_string(combat->rng), rng);
            auto launch = spellCharge
                ? InstantSpellLaunch{true, {}}
                : launchInstantSpell(spellRecord, caster, mRuntime.mStore, rng,
                    mBinding.mUncappedDamageFatigue, !mBinding.mActorEffectLifecycle);
            if (launch.succeeded)
            {
                if (!spellCharge && !mBinding.mActorEffectLifecycle)
                    stageTimedResistance(spellRecord.effects, ESM::RT_Self, owner,
                        tick.value(), timedEffects);
                else
                {
                    const auto applied = resolveEffects(spellRecord.effects, ESM::RT_Self,
                        owner, caster, tick.value(),
                        context.identity, spellCharge ? spellEffectSource : cast.sourceId,
                        uint64_t(cast.sourceKind), rng, mRuntime.mStore, timedEffects,
                        mBinding.mActorEffectLifecycle, mBinding.mUncappedDamageFatigue);
                    launch.result.health += applied.health;
                    launch.result.magicka += applied.magicka;
                    launch.result.fatigue += applied.fatigue;
                }
            }
            if (spellCharge) charges.push_back(*spellCharge);
            combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            saveCombatStats(combat->actors[owner], caster, timedEffects, owner);
            if (mBinding.mKnockoutRules)
                combat->knockedDown[owner] = caster.getHealth().getCurrent() > 0
                    && (fatigueKnockout(caster, mBinding.mKnockoutAnimation)
                        || (mBinding.mKnockoutAnimation && combat->knockedDown[owner]));
            if (owner == 2 && caster.getHealth().getCurrent() <= 0 && !life->respawnTick)
            {
                if (tick.value() > UINT64_MAX - mBinding.mNpcRespawnDelayTicks)
                    throw std::invalid_argument("NPC self-cast death deadline exhausted");
                life->deaths.push_back({life->generation, tick.value(), context.identity.id,
                    context.identity.kind, context.identity.life});
                life->respawnTick = tick.value() + mBinding.mNpcRespawnDelayTicks;
                step.reset(); after = before; report.status = Diagnostics::Status::Idle;
            }
            spellCasts.push_back(MagicUseCombatEvent{wireCaster(context.identity), cast.sourceKind, cast.sourceId,
                cast.targetKind, cast.targetId, CombatRevision::fromValue(tick.value()).value(),
                CombatRevision::fromValue(tick.value()).value(), launch.succeeded, launch.result.health,
                launch.result.fatigue, launch.result.magicka, 0.f, 0.f, 0.f, false, std::max(uint64_t(1), context.identity.life)});
            if (launch.succeeded && mBinding.mMagicProjectileCollection)
                if (const auto origin = combatPosition(owner, owner < 2 && mBinding.mPlayerTravel
                        && playerTravelEffects(spellRecord.effects)))
                {
                    if (magicImpactCues.size() >= MaximumReplicatedMagicImpactCues)
                        throw std::invalid_argument("Native magic visual cue capacity exhausted");
                    magicImpactCues.push_back({uint8_t(context.identity.kind == 2 ? 2 : 1),
                        uint8_t(cast.sourceKind), context.identity.id,
                        std::max<uint64_t>(1, context.identity.life), std::max<uint64_t>(1, cast.commandId),
                        magicVisualRecord(uint64_t(cast.sourceKind), spellEffectSource), *origin, 3});
                }
            if (launch.succeeded && (cast.targetKind == MagicUseTargetKind::Door
                    || cast.targetKind == MagicUseTargetKind::Container))
            {
                const auto& effect = spellRecord.effects.effects.front();
                const int magnitude = int(MWMechanics::rollEffectMagnitude(
                    float(effect.mMagnMin), float(effect.mMagnMax), rng));
                if (!(cast.targetKind == MagicUseTargetKind::Door
                        ? stageDoorSpell(command, cast.targetId, effect.mEffectID, magnitude)
                        : stageContainerSpell(command, cast.targetId, effect.mEffectID, magnitude)))
                    throw std::invalid_argument("Native door spell target changed during launch");
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
            if (launch.succeeded && owner < 2 && mBinding.mPlayerTravel && combat->teleportingEnabled
                && spellRecord.effects.effects.size() == 1)
            {
                const auto effect = spellRecord.effects.effects.front().mEffectID;
                if (effect == ESM::MagicEffect::Mark || effect == ESM::MagicEffect::Recall)
                {
                    const auto* traveler = players.findPlayer(mBinding.mPlayers[owner]);
                    if (!traveler) throw std::invalid_argument("Player travel caster disappeared");
                    if (effect == ESM::MagicEffect::Mark)
                        combat->players[owner].mark = traveler->transform();
                    else if (combat->players[owner].mark)
                        relocations.emplace_back(mBinding.mPlayers[owner], *combat->players[owner].mark);
                }
            }
            if (launch.succeeded && cast.targetKind != MagicUseTargetKind::Door
                && cast.targetKind != MagicUseTargetKind::Container
                && mBinding.mNpcCastLifecycle && caster.getHealth().getCurrent() > 0)
            {
                const auto origin = combatPosition(owner);
                const size_t victimIndex = cast.targetKind == MagicUseTargetKind::Self ? owner
                    : cast.targetKind == MagicUseTargetKind::Actor ? npcVictim(cast.targetId)
                    : actor(PlayerId::fromValue(cast.targetId).value());
                if (cast.targetKind == MagicUseTargetKind::Actor && !victimIndex)
                    throw std::invalid_argument("Spell target placement changed");
                const auto endpoint = combatPosition(victimIndex);
                const float reach = mRuntime.mStore.get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat();
                const bool touching = origin && endpoint && victimIndex != owner
                    && distanceSquared(*origin, *endpoint) <= reach * reach
                    && mBinding.mNavigatingActor->lineOfSight(*origin, *endpoint);
                for (const int range : {ESM::RT_Self, ESM::RT_Touch})
                {
                    if (range == ESM::RT_Touch && !touching) continue;
                    const auto center = range == ESM::RT_Self ? origin : endpoint;
                    if (!center) continue;
                    if (std::ranges::any_of(spellRecord.effects.effects, [&](const auto& effect) {
                            return effect.mRange == range;
                        }))
                    {
                        if (magicImpactCues.size() >= MaximumReplicatedMagicImpactCues)
                            throw std::invalid_argument("Native magic visual cue capacity exhausted");
                        magicImpactCues.push_back({uint8_t(context.identity.kind == 2 ? 2 : 1),
                            uint8_t(cast.sourceKind), context.identity.id,
                            std::max<uint64_t>(1, context.identity.life), std::max<uint64_t>(1, cast.commandId),
                            magicVisualRecord(uint64_t(cast.sourceKind), spellEffectSource),
                            *center, uint8_t(range)});
                    }
                    for (size_t index = 0; index < (mBinding.mNeighborCombat ? combat->actors.size() : 3); ++index)
                    {
                        if (index == owner || combat->actors[index][8][2] <= 0) continue;
                        const auto position = combatPosition(index);
                        if (!position) continue;
                        PreparedInstantEffects selected;
                        std::vector<uint64_t> ordinals;
                        for (size_t ordinal = 0; ordinal < spellRecord.effects.effects.size(); ++ordinal)
                        {
                            const auto& effect = spellRecord.effects.effects[ordinal];
                            if (effect.mRange != range) continue;
                            const bool direct = range == ESM::RT_Touch && index == victimIndex;
                            if (direct || (effect.mArea > 0 && distanceSquared(*position, *center) <= std::pow(effect.mArea * 22.f, 2)))
                            { selected.effects.push_back(effect); ordinals.push_back(ordinal); }
                        }
                        if (!selected.effects.empty()) applyContactEffects(index, selected, range,
                            context.identity, cast.sourceKind, cast.sourceId, spellEffectSource, rng, ordinals);
                    }
                }
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
            if (launch.succeeded && caster.getHealth().getCurrent() > 0 && cast.targetKind != MagicUseTargetKind::Self
                && spellRecord.effects.hasRange(ESM::RT_Target))
            {
                const auto* player = owner < 2 ? players.findPlayer(mBinding.mPlayers[owner]) : nullptr;
                if ((owner < 2 && !player) || !life || projectiles.size() >= MaximumActorProjectiles)
                    throw std::invalid_argument("Native target spell launch became stale");
                std::array<float, 3> origin{before.mPosition[0], before.mPosition[1], before.mPosition[2] + 55.f};
                if (player)
                {
                    const auto position = player->transform().position();
                    origin = {float(double(position.x()) / 1024), float(double(position.y()) / 1024),
                        float(double(position.z()) / 1024) + 64.f};
                }
                std::array<float, 3> destination{before.mPosition[0], before.mPosition[1],
                    before.mPosition[2] + 55.f};
                if (cast.targetKind == MagicUseTargetKind::Actor)
                {
                    const size_t victim = npcVictim(cast.targetId);
                    if (!victim || victimLife(victim).respawnTick)
                        throw std::invalid_argument("Native target NPC left before launch");
                    destination = *combatPosition(victim);
                }
                if (cast.targetKind == MagicUseTargetKind::Player)
                {
                    const auto* victim = players.findPlayer(PlayerId::fromValue(cast.targetId).value());
                    if (!victim || victim->transform().cell() != actorCell(before)
                        || combat->actors[actor(victim->playerId())][8][2] <= 0
                        || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                            return session.playerId() == victim->playerId();
                        })) throw std::invalid_argument("Native target player left before launch");
                    const auto place = victim->transform().position();
                    destination = {float(double(place.x()) / 1024), float(double(place.y()) / 1024),
                        float(double(place.z()) / 1024) + 64.f};
                }
                std::array<float, 3> direction{destination[0] - origin[0],
                    destination[1] - origin[1], destination[2] - origin[2]};
                const float distance = std::sqrt(direction[0]*direction[0]
                    + direction[1]*direction[1] + direction[2]*direction[2]);
                if (!std::isfinite(distance) || distance < 8.f || distance > 2048.f)
                    throw std::invalid_argument("Native target spell range invalid");
                float speed = 0;
                for (const auto& effect : spellRecord.effects.effects)
                    speed += mRuntime.mStore.get<ESM::MagicEffect>().find(effect.mEffectID)->mData.mSpeed;
                speed /= spellRecord.effects.effects.size();
                speed *= mRuntime.mStore.get<ESM::GameSetting>()
                    .find("fTargetSpellMaxSpeed")->mValue.getFloat() / 30.f;
                if (!std::isfinite(speed) || speed < 1.f || speed > 1000.f
                    || tick.value() > UINT64_MAX - 90)
                    throw std::invalid_argument("Native target spell speed invalid");
                for (float& axis : direction) axis *= speed / distance;
                projectiles.push_back(ActorCampaignProjectile{context.identity.id, cast.sourceId,
                    cast.targetId, cast.targetKind == MagicUseTargetKind::Actor
                        ? victimLife(npcVictim(cast.targetId)).generation : 1u, tick.value() + 90,
                    uint64_t(cast.sourceKind), spellCharge ? spellEffectSource : cast.sourceId,
                    origin, direction, uint64_t(cast.targetKind),
                    mBinding.mMagicProjectileCollection ? cast.commandId : 0,
                    context.identity.kind, context.identity.life});
            }
        }
        if (flyingCount && combat)
        {
            std::vector<ActorCampaignProjectile> remaining;
            remaining.reserve(projectiles.size());
            for (size_t flight = 0; flight < flyingCount; ++flight)
            {
                auto pending = projectiles[flight];
                const size_t eventCount = spellCasts.size();
                std::array<float, 3> endpoint;
                for (size_t i = 0; i < 3; ++i) endpoint[i] = pending.position[i] + pending.step[i];
                auto hit = mBinding.mNavigatingActor->projectileContact(pending.position, endpoint, pending.casterKind == 2 ? pending.caster : 0);
                size_t hitIndex = hit && hit->actor ? npcVictim(hit->actor) : combat->actors.size();
                if (mBinding.mMagicPlayerTarget)
                {
                    const float length2 = pending.step[0] * pending.step[0]
                        + pending.step[1] * pending.step[1] + pending.step[2] * pending.step[2];
                    float first = 1.f;
                    if (hit)
                    {
                        float projection = 0.f;
                        for (size_t axis = 0; axis < 3; ++axis)
                            projection += (hit->position[axis] - pending.position[axis]) * pending.step[axis];
                        first = std::clamp(projection / length2, 0.f, 1.f);
                    }
                    for (size_t index = 0; index < mBinding.mPlayers.size(); ++index)
                    {
                        const auto id = mBinding.mPlayers[index];
                        const auto* player = players.findPlayer(id);
                        if ((pending.casterKind != 2 && id.value() == pending.caster) || !player
                            || player->transform().cell() != actorCell(before)
                            || combat->actors[index][8][2] <= 0
                            || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                                return session.playerId() == id;
                            })) continue;
                        const auto place = player->transform().position();
                        const std::array<float, 3> center{float(double(place.x()) / 1024),
                            float(double(place.y()) / 1024), float(double(place.z()) / 1024) + 64.f};
                        float offset = 0.f, origin2 = 0.f;
                        for (size_t axis = 0; axis < 3; ++axis)
                        {
                            const float delta = pending.position[axis] - center[axis];
                            offset += delta * pending.step[axis]; origin2 += delta * delta;
                        }
                        constexpr float radius = 32.f;
                        const float discriminant = offset * offset - length2 * (origin2 - radius * radius);
                        if (discriminant < 0.f) continue;
                        const float fraction = origin2 <= radius * radius ? 0.f
                            : (-offset - std::sqrt(discriminant)) / length2;
                        if (fraction < 0.f || fraction > first) continue;
                        first = fraction;
                        std::array<float, 3> point{};
                        for (size_t axis = 0; axis < 3; ++axis)
                            point[axis] = pending.position[axis] + fraction * pending.step[axis];
                        hit = ActorProjectileContact{id.value(), point};
                        hitIndex = index;
                    }
                }
                const bool timedOut = tick.value() >= pending.expiresTick;
                const auto casterIndex = pending.casterKind == 2 ? size_t(2) : actor(PlayerId::fromValue(pending.caster).value());
                const size_t targetIndex = pending.targetKind == uint64_t(MagicUseTargetKind::Actor)
                    ? npcVictim(pending.target) : 0;
                const bool validCast = life && combat->actors[casterIndex][8][2] > 0
                    && (pending.casterKind != 2 || pending.casterLife == life->generation)
                    && (pending.targetKind == uint64_t(MagicUseTargetKind::Player)
                        || (targetIndex && victimLife(targetIndex).generation == pending.generation
                            && !victimLife(targetIndex).respawnTick
                            && combat->actors[targetIndex][8][2] > 0));
                if (!hit && !timedOut && validCast)
                {
                    pending.position = endpoint;
                    remaining.push_back(pending);
                }
                else
                {
                    const bool directHit = hit && validCast
                        && (pending.targetKind == uint64_t(MagicUseTargetKind::Actor)
                            ? hitIndex == targetIndex && hit->actor == pending.target
                            : hitIndex < 2 && mBinding.mPlayers[hitIndex].value() == pending.target);
                    if (hit && validCast && (directHit || mBinding.mMagicArea))
                    {
                        const auto& known = mRuntime.mStore.get<ESM::NPC>()
                            .find(mRuntime.ownerPtr(casterIndex == 2 ? mCombatNpcOwner : casterIndex).getCellRef().getRefId())->mSpells.mList;
                        std::optional<PreparedInstantEffects> effects;
                        if (pending.sourceKind == uint64_t(MagicUseSourceKind::Spell))
                        {
                            const ESM::Spell* selected = nullptr;
                            for (const auto& id : known)
                                if (!id.empty() && spellRecordId(id) == pending.effectSource)
                                {
                                    if (selected) throw std::invalid_argument("Native projectile spell identity collision");
                                    selected = mRuntime.mStore.get<ESM::Spell>().search(id);
                                }
                            if (!selected) throw std::invalid_argument("Native projectile spell source missing");
                            const auto plan = prepareInstantSpell(*selected, mRuntime.mStore,
                                mBinding.mActorEffectLifecycle, mBinding.mExpandedEffects,
                                mBinding.mSpecialConditions, mBinding.mMovementEffects);
                            if (plan) effects = plan->effects;
                        }
                        else if (const auto* selected = enchantmentBySource(mRuntime.mStore, pending.effectSource);
                            selected && (selected->mData.mType == ESM::Enchantment::WhenUsed
                                || selected->mData.mType == ESM::Enchantment::CastOnce))
                            effects = prepareInstantEffects(selected->mEffects, mRuntime.mStore,
                                mBinding.mActorEffectLifecycle, mBinding.mExpandedEffects,
                                false, mBinding.mSpecialConditions, mBinding.mMovementEffects);
                        if (!effects || !effects->hasRange(ESM::RT_Target))
                            throw std::invalid_argument("Native projectile effect plan changed");
                        std::vector<PreparedInstantEffects> selected(combat->actors.size());
                        std::vector<std::vector<uint64_t>> selectedOrdinals(combat->actors.size());
                        for (size_t ordinal = 0; ordinal < effects->effects.size(); ++ordinal)
                        {
                            const auto& effect = effects->effects[ordinal];
                            const auto select = [&](size_t index) {
                                selected[index].effects.push_back(effect);
                                selectedOrdinals[index].push_back(ordinal);
                            };
                            if (effect.mRange != ESM::RT_Target) continue;
                            if (directHit) select(hitIndex);
                            if (!mBinding.mMagicArea || !effect.mArea) continue;
                            const float radius = float(effect.mArea) * 22.f;
                            const auto nearby = [&](const std::array<float, 3>& position) {
                                float distance2 = 0.f;
                                for (size_t axis = 0; axis < 3; ++axis)
                                    distance2 += (position[axis] - hit->position[axis])
                                        * (position[axis] - hit->position[axis]);
                                return distance2 <= radius * radius;
                            };
                            // Stock explosion excludes the caster and applies the direct hit once.
                            for (size_t index = 0; index < mBinding.mPlayers.size(); ++index)
                            {
                                const auto playerId = mBinding.mPlayers[index];
                                const auto* player = players.findPlayer(playerId);
                                if ((pending.casterKind != 2 && playerId.value() == pending.caster) || (directHit && index == hitIndex) || !player
                                    || player->transform().cell() != actorCell(before)
                                    || combat->actors[index][8][2] <= 0
                                    || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                                        return session.playerId() == playerId;
                                    })) continue;
                                const auto position = player->transform().position();
                                if (nearby({float(double(position.x()) / 1024),
                                        float(double(position.y()) / 1024), float(double(position.z()) / 1024)}))
                                    select(index);
                            }
                            for (size_t index = 2; index < combat->actors.size(); ++index)
                            {
                                if ((pending.casterKind == 2 && pending.caster == (index == 2
                                        ? before.mActor : combat->npcPlacements.at(index - 2)))
                                    || (directHit && hitIndex == index)
                                    || combat->actors[index][8][2] <= 0
                                    || victimLife(index).respawnTick) continue;
                                if (nearby(victimScene(index).mPosition)) select(index);
                            }
                        }
                        Misc::Rng::Generator rng;
                        Misc::Rng::deserialize(std::to_string(combat->rng), rng);
                        const auto applySelected = [&](size_t index) {
                            if (selected[index].effects.empty()) return;
                            applyContactEffects(index, selected[index], ESM::RT_Target,
                                {pending.caster, pending.casterKind, pending.casterLife}, MagicUseSourceKind(pending.sourceKind),
                                pending.source, pending.effectSource, rng, selectedOrdinals[index]);
                        };
                        for (size_t index = 2; index < selected.size(); ++index) applySelected(index);
                        for (size_t index = 0; index < 2; ++index) applySelected(index);
                        combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    }
                    if (hit && validCast)
                    {
                        if (magicImpactCues.size() >= MaximumReplicatedMagicImpactCues)
                            throw std::invalid_argument("Native magic visual cue capacity exhausted");
                        magicImpactCues.push_back({uint8_t(pending.casterKind == 2 ? 2 : 1), uint8_t(pending.sourceKind),
                            pending.caster, std::max<uint64_t>(1, pending.casterLife),
                            std::max<uint64_t>(1, pending.commandId),
                            magicVisualRecord(pending.sourceKind, pending.effectSource), hit->position});
                    }
                    if (spellCasts.size() == eventCount)
                        spellCasts.push_back(MagicUseCombatEvent{wireCaster({pending.caster, pending.casterKind, pending.casterLife}),
                            MagicUseSourceKind(pending.sourceKind), pending.source,
                            MagicUseTargetKind(pending.targetKind), pending.target,
                            CombatRevision::fromValue(tick.value()).value(),
                            CombatRevision::fromValue(tick.value()).value(), false,
                            0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false, std::max(uint64_t(1), pending.casterLife)});
                }
            }
            remaining.insert(remaining.end(), projectiles.begin() + flyingCount, projectiles.end());
            projectiles = std::move(remaining);
        }
        const auto selectedNpcWeapon = [&]() -> const ESM::Weapon* {
            const auto held = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
            if (!held) return nullptr;
            const auto values = mRuntime.installedValues(mCombatNpcOwner);
            const auto item = std::ranges::find(values.mObjects, held->mItem,
                [](const auto& object) { return object.mRef.mRefNum; });
            if (item == values.mObjects.end()) throw std::invalid_argument("Native NPC weapon source missing");
            return mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
        };
        const bool npcRanged = target && rangedWeapon(selectedNpcWeapon(), mBinding.mRangedRelease);
        if (mBinding.mWeaponMelee && target && active)
        {
            const auto index = target == mBinding.mPlayers[0].value() ? 0u : 1u;
            const auto* victim = players.findPlayer(mBinding.mPlayers[index]);
            const auto visible = [&] {
                if (!victim) return false;
                const auto position = victim->transform().position();
                return mBinding.mNavigatingActor->lineOfSight(
                    {after.mPosition[0], after.mPosition[1], after.mPosition[2] + 110.f},
                    {float(double(position.x()) / 1024), float(double(position.y()) / 1024),
                        float(double(position.z()) / 1024) + 110.f}) && npcDetects(index);
            };
            if (combat->actors[2][8][2] <= 0 || combat->knockedDown[2] || hasParalysis(timedEffects, 2) || combat->hitRecoveryTicks[2]
                || combat->actors[index][8][2] <= 0 || !victim || victim->transform().cell() != actorCell(after)
                || !visible()
                // A retained target blocks the next AI selection. Give up a
                // fully wound, unreleased swing when that target leaves reach;
                // released swings keep their target and resolve at the hit key.
                || (!npcRanged && !melee->snapshot().mReleased && melee->phaseCompletion() == 1.f
                    && meleeContact(players, after, target, meleeReach()) != target)
                || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                    return session.playerId() == mBinding.mPlayers[index];
                }))
            { melee = mIdleMelee; target = 0; contact = false; }
        }
        if (mBinding.mActorPresentation && step && target && !respawn)
            if (const auto* victim = players.findPlayer(*PlayerId::fromValue(target)))
            {
                const auto position = victim->transform().position();
                const float dx = float(double(position.x()) / 1024) - after.mPosition[0];
                const float dy = float(double(position.y()) / 1024) - after.mPosition[1];
                if (dx != 0 || dy != 0)
                {
                    mBinding.mNavigatingActor->setFacing(*step, std::atan2(dx, dy));
                    after = step->snapshot();
                }
            }
        if (step && !respawn && !automaticCast && melee && (!mBinding.mWeaponMelee || target)
            && (!combat || (combat->actors[2][8][2] > 0
                && (!mBinding.mKnockoutRules || (!combat->knockedDown[2] && !hasParalysis(timedEffects, 2))))))
        {
            if (mBinding.mMeleeContact && !melee->snapshot().mReleased && melee->phaseCompletion() == 1.f)
            {
                const auto selected = npcRanged ? target
                    : meleeContact(players, after, mBinding.mWeaponMelee ? target : 0, meleeReach());
                if (selected)
                {
                    float strength = melee->windUp();
                    if (strength == -1.f)
                    {
                        if (!combat) throw std::invalid_argument("Fixed-strength NPC clips require durable combat RNG");
                        Misc::Rng::Generator rng;
                        Misc::Rng::deserialize(std::to_string(combat->rng), rng);
                        strength = MWMechanics::resolveAttackStrength(strength, rng);
                        combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    }
                    if (melee->release(strength))
                    {
                        target = selected;
                        breakInvisibility(2);
                    }
                }
            }
            const auto hit = melee->advance(seconds);
            if (mBinding.mMeleeContact && hit && target && !npcRanged)
                contact = meleeContact(players, after, target, meleeReach()) == target;
            if (hit && combat && mBinding.mCombatResolution && npcRanged)
            {
                if (combat->arrows.size() >= MaximumActorProjectiles) throw std::invalid_argument("NPC projectile capacity exceeded");
                const auto* weapon = selectedNpcWeapon();
                const auto values = combatEquipmentValues(mCombatNpcOwner, command.get());
                const auto* ammo = equippedAmmunition(values, mRuntime.mStore, *weapon,
                    mBinding);
                const auto held = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
                const auto* victim = players.findPlayer(*PlayerId::fromValue(target));
                if (!ammo || !held || !victim || life->respawnTick || combat->actors[2][8][2] <= 0)
                    throw std::invalid_argument("NPC projectile source or target changed before release");
                const auto position = victim->transform().position();
                const osg::Vec3f origin(after.mPosition[0], after.mPosition[1], after.mPosition[2] + 110.f);
                osg::Vec3f direction(float(double(position.x()) / 1024) - origin.x(),
                    float(double(position.y()) / 1024) - origin.y(),
                    float(double(position.z()) / 1024) + 64.f - origin.z());
                if (direction.normalize() == 0) throw std::invalid_argument("NPC projectile direction invalid");
                auto& arrow = combat->arrows.emplace_back();
                arrow.caster = before.mActor; arrow.casterKind = 2; arrow.casterLife = life->generation;
                arrow.command = combat->npcAction; arrow.source = wireId(held->mItem).value();
                arrow.ammunition = wireId(ammo->mRef.mRefNum).value();
                arrow.targetKind = 1; arrow.target = target; arrow.targetLife = 1;
                arrow.releaseTick = tick.value(); arrow.strength = melee->snapshot().mStrength;
                arrow.weapon = std::string(weapon->mId.getRefIdString());
                arrow.ammoRecord = std::string(ammo->mRef.mRefID.getRefIdString());
                arrow.position = {origin.x(), origin.y(), origin.z()};
                arrow.direction = {direction.x(), direction.y(), direction.z()};
                const float speed = MWMechanics::projectileLaunchSpeed(mRuntime.mStore,
                    weapon->mData.mType == ESM::Weapon::MarksmanThrown, arrow.strength);
                if (!std::isfinite(speed) || speed <= 0 || speed > 50000)
                    throw std::invalid_argument("NPC projectile launch speed invalid");
                for (size_t axis = 0; axis < 3; ++axis) arrow.velocity[axis] = arrow.direction[axis] * speed;
                arrow.condition = (MWMechanics::getWeaponType(weapon->mData.mType)->mFlags & ESM::WeaponType::HasHealth)
                    && weapon->mData.mHealth ? std::clamp(float(held->mCondition) / weapon->mData.mHealth, 0.f, 1.f) : 1.f;
                charges.push_back({mCombatNpcOwner, ammo->mRef.mRefNum, ammo->mRef.mEnchantmentCharge, 0, true});
                auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[2], timedEffects, 2);
                const float capacity = attacker.getAttribute(ESM::Attribute::Strength).getModified()
                    * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
                const float weight = std::max(0.f, mRuntime.storage(mCombatNpcOwner).getWeight());
                MWMechanics::applyFatigueLoss(attacker, mRuntime.mStore, weapon->mData.mWeight, arrow.strength,
                    weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity);
                saveCombatStats(combat->actors[2], attacker, timedEffects, 2);
            }
            if (hit && combat && mBinding.mCombatResolution && !npcRanged)
            {
                bool hitSuccess = false;
                bool hitBlocked = false;
                float hitDamage = 0;
                MeleeDamageStat hitStat = MeleeDamageStat::Health;
                bool targetDied = false;
                auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[2], timedEffects, 2);
                addTimedResistance(attacker, timedEffects, 2);
                const auto held = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
                const auto values = mRuntime.installedValues(mCombatNpcOwner);
                const ESM::Weapon* weapon = nullptr;
                if (held)
                {
                    const auto item = std::ranges::find(values.mObjects, held->mItem,
                        [](const auto& object) { return object.mRef.mRefNum; });
                    if (item == values.mObjects.end()) throw std::invalid_argument("Native melee weapon identity missing");
                    weapon = mRuntime.mStore.get<ESM::Weapon>().find(item->mRef.mRefID);
                }
                const float strength = melee->snapshot().mStrength;
                const float capacity = attacker.getAttribute(ESM::Attribute::Strength).getModified()
                    * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
                const float weight = std::max(0.f, mRuntime.storage(mCombatNpcOwner).getWeight());
                const float encumbrance = weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity;
                MWMechanics::applyFatigueLoss(attacker, mRuntime.mStore,
                    weapon ? weapon->mData.mWeight : 0.f, strength, encumbrance);
                if (contact)
                {
                    const size_t victimIndex = mBinding.mPlayers[0].value() == target ? 0 : 1;
                    auto victim = loadCombatStats(mRuntime.mStore, combat->actors[victimIndex],
                        timedEffects, victimIndex);
                    if (mBinding.mKnockoutRules)
                        victim.setKnockedDown(combat->knockedDown[victimIndex]);
                    addTimedResistance(victim, timedEffects, victimIndex);
                    const auto skill = weapon ? MWMechanics::getWeaponType(weapon->mData.mType)->mSkill
                        : ESM::Skill::HandToHand;
                    const auto ptr = mRuntime.ownerPtr(mCombatNpcOwner);
                    const int skillValue = ptr.getType() == ESM::Creature::sRecordId
                        ? ptr.get<ESM::Creature>()->mBase->mData.mCombat : int(attacker.getSkill(skill).getModified());
                    const bool paralyzed = victim.getMagicEffects()
                        .getOrDefault(ESM::MagicEffect::Paralyze).getMagnitude() > 0;
                    const float chance = MWMechanics::getHitChance(mRuntime.mStore, attacker, victim,
                        skillValue, false, paralyzed);
                    Misc::Rng::Generator rng;
                    Misc::Rng::deserialize(std::to_string(combat->rng), rng);
                    const bool success = Misc::Rng::roll0to99(rng) < chance;
                    hitSuccess = success;
                    combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    float damage = 0;
                    bool blocked = false;
                    const bool healthUnarmed = mBinding.mKnockoutRules
                        && (victim.getKnockedDown() || paralyzed);
                    const auto damagedStat = weapon || healthUnarmed ? MeleeDamageStat::Health : MeleeDamageStat::Fatigue;
                    if (success && weapon)
                    {
                        const auto& range = *hit == ESM::Weapon::AT_Chop ? weapon->mData.mChop
                            : *hit == ESM::Weapon::AT_Slash ? weapon->mData.mSlash : weapon->mData.mThrust;
                        damage = range[0] + (range[1] - range[0]) * strength;
                        TES3MP::OpenMwMeleeSettings settings;
                        const auto& gmst = mRuntime.mStore.get<ESM::GameSetting>();
                        settings.damageStrengthBase = gmst.find("fDamageStrengthBase")->mValue.getFloat();
                        settings.damageStrengthMultiplier = gmst.find("fDamageStrengthMult")->mValue.getFloat();
                        damage = TES3MP::openMwAdjustedWeaponDamage(settings,
                            attacker.getAttribute(ESM::Attribute::Strength).getModified(),
                            weapon->mData.mHealth ? float(held->mCondition) / weapon->mData.mHealth : 1.f,
                            weapon->mData.mHealth != 0, damage);
                    }
                    else if (success)
                    {
                        damage = healthUnarmed
                            ? MWMechanics::getUnarmedHealthDamage(mRuntime.mStore, attacker,
                                attacker.getSkill(ESM::Skill::HandToHand).getModified(), strength)
                            : MWMechanics::getUnarmedFatigueDamage(mRuntime.mStore, attacker,
                                attacker.getSkill(ESM::Skill::HandToHand).getModified(), strength);
                    }
                    const float weaponDamage = damage;
                    if (success && (!std::isfinite(damage) || damage < 0 || damage > 1'000'000))
                        throw std::invalid_argument("Native NPC melee damage invalid");
                    if (success && mBinding.mKnockoutRules)
                        damage = MWMechanics::applyKnockoutDamageMultiplier(mRuntime.mStore, victim, damage);
                    if (success && (!std::isfinite(damage) || damage < 0 || damage > 1'000'000))
                        throw std::invalid_argument("Native NPC knockout damage invalid");
                    if (success && mBinding.mExpandedEffects)
                    {
                        if (weapon) applyStrike(magicCaster(2), *held, *weapon, attacker, victim, victimIndex, rng);
                        retaliate(attacker, victim, 2, victimIndex, rng);
                        diseaseContact(victimIndex, victim, rng);
                    }
                    if (success && damage > 0)
                    {
                        const auto* player = players.findPlayer(mBinding.mPlayers[victimIndex]);
                        const auto position = player->transform().position();
                        const std::array<float, 3> defenderPosition{float(double(position.x()) / 1024),
                            float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
                        const float defenderYaw = float(double(player->transform().orientation().z().value())
                            * (2.0 * std::numbers::pi_v<double> / 4294967296.0));
                        blocked = defendHit(victimIndex, victim, attacker, weapon, strength,
                            weapon ? weapon->mData.mWeight : 0.f, attacker.getSkill(skill).getModified(),
                            after.mPosition, defenderPosition, defenderYaw,
                            double(player->linearVelocity().x()) * -std::sin(defenderYaw)
                                + double(player->linearVelocity().y()) * std::cos(defenderYaw) > 0,
                            damage, rng);
                        const auto applied = MWMechanics::applyHitDamage(victim,
                            {{damagedStat == MeleeDamageStat::Health ? "health" : "fatigue", damage}},
                            MWWorld::TimeStamp{});
                        if (victim.getHealth().getCurrent() > 0)
                            startHitRecovery(victimIndex, victim, applied, damage, blocked, rng);
                        else combat->hitRecoveryTicks[victimIndex] = 0;
                    }
                    if (weapon && weapon->mData.mHealth)
                    {
                        const float multiplier = mRuntime.mStore.get<ESM::GameSetting>()
                            .find("fWeaponDamageMult")->mValue.getFloat();
                        wear.push_back({mCombatNpcOwner, *held,
                            MWMechanics::weaponConditionAfterHit(held->mCondition, weaponDamage, success, multiplier)});
                    }
                    if (success && weapon && !mBinding.mExpandedEffects)
                        applyStrike(magicCaster(2), *held, *weapon, attacker, victim, victimIndex, rng);
                    saveCombatStats(combat->actors[victimIndex], victim, timedEffects, victimIndex);
                    if (mBinding.mKnockoutRules)
                        combat->knockedDown[victimIndex] = victim.getHealth().getCurrent() > 0
                            && (fatigueKnockout(victim, mBinding.mKnockoutAnimation)
                                || (mBinding.mKnockoutAnimation && combat->knockedDown[victimIndex]));
                    hitDamage = damage;
                    hitStat = damagedStat;
                    hitBlocked = blocked;
                    targetDied = victim.getHealth().getCurrent() <= 0;
                }
                saveCombatStats(combat->actors[2], attacker, timedEffects, 2);
                if (mBinding.mKnockoutRules)
                    combat->knockedDown[2] = attacker.getHealth().getCurrent() > 0
                        && (fatigueKnockout(attacker, mBinding.mKnockoutAnimation)
                            || (mBinding.mKnockoutAnimation && combat->knockedDown[2]));
                if (target)
                    actorHits.push_back(ActorMeleeCombatEvent{ActorId::fromValue(before.mActor).value(),
                        PlayerId::fromValue(target).value(),
                        CombatRevision::fromValue(tick.value()).value(),
                        CombatRevision::fromValue(tick.value()).value(),
                        hitDamage, hitStat, contact && hitSuccess, hitBlocked, targetDied});
            }
        }
        const auto waterBreathingIndex
            = uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::WaterBreathing));
        if (combat && step && step->snapshot().mDrowning && combat->actors[2][8][2] > 0
            && std::ranges::none_of(timedEffects, [waterBreathingIndex](const auto& effect) {
                return effect.actor == 2 && effect.effectIndex == waterBreathingIndex && effect.magnitude > 0;
            }))
        {
            auto victim = loadCombatStats(mRuntime.mStore, combat->actors[2], timedEffects, 2);
            auto health = victim.getHealth();
            health.setCurrent(health.getCurrent() - mRuntime.mStore.get<ESM::GameSetting>()
                .find("fSuffocationDamage")->mValue.getFloat() * seconds);
            victim.setHealth(health);
            saveCombatStats(combat->actors[2], victim, timedEffects, 2);
            if (victim.getHealth().getCurrent() <= 0)
                recordEffectDeath({before.mActor, 2, life->generation});
        }
        if (combat && mBinding.mPlayerCastLifecycle)
            for (size_t owner = 0; owner < 2; ++owner)
                if (combat->playerCasts[owner] && (combat->actors[owner][8][2] <= 0 || combat->knockedDown[owner]
                    || hasParalysis(timedEffects, owner) || combat->hitRecoveryTicks[owner]
                    || (combat->playerCasts[owner]->targetKind == 2
                        && combat->playerCasts[owner]->phase < ActorCampaignCast::Released
                        && (!npcVictim(combat->playerCasts[owner]->target)
                            || combat->playerCasts[owner]->targetLife
                                != victimLife(npcVictim(combat->playerCasts[owner]->target)).generation
                            || victimLife(npcVictim(combat->playerCasts[owner]->target)).respawnTick))))
                    combat->playerCasts[owner].reset();
        if (casting && (!combat || !life || life->generation != casting->life
            || combat->actors[2][8][2] <= 0 || combat->knockedDown[2] || hasParalysis(timedEffects, 2) || combat->hitRecoveryTicks[2])) casting.reset();
        if (combat && life)
            std::erase_if(projectiles, [&](const auto& pending) {
                if (pending.casterKind != 2 || (pending.casterLife == life->generation && combat->actors[2][8][2] > 0))
                    return false;
                spellCasts.push_back(MagicUseCombatEvent{wireCaster({pending.caster, pending.casterKind, pending.casterLife}),
                    MagicUseSourceKind(pending.sourceKind), pending.source, MagicUseTargetKind(pending.targetKind), pending.target,
                    CombatRevision::fromValue(tick.value()).value(), CombatRevision::fromValue(tick.value()).value(),
                    false, 0, 0, 0, 0, 0, 0, false, pending.casterLife});
                return true;
            });
        if (combat && mBinding.mMeleeDefenseRules)
            for (size_t index = 0; index < combat->actors.size(); ++index)
                if (combat->actors[index][8][2] <= 0) combat->hitRecoveryTicks[index] = 0;
        std::array<float,3> velocity;
        for (size_t i=0; i<3; ++i)
        {
            velocity[i] = respawn && respawn->owner() == mCombatNpcOwner
                ? 0 : (after.mPosition[i]-before.mPosition[i])*30;
            // WaterWalking may lift an actor onto the water plane in one frame.
            // Keep its committed position exact while bounding presentation speed.
            if (mBinding.mMovementEffects) velocity[i] = std::clamp(velocity[i], -4096.f, 4096.f);
        }
        if (!mBinding.mNpcCastLifecycle)
            for (auto& effect : timedEffects) if (effect.sourceKind != 3)
            {
                if (effect.argument) throw std::invalid_argument("Timed stat arguments require cast lifecycle campaign");
                effect.ordinal = 0;
            }
        if (mBinding.mWeaponMelee && combat)
            for (size_t index = 0; index < 3; ++index)
            {
                const size_t owner = index == 2 ? mCombatNpcOwner : index;
                if (std::ranges::none_of(wear, [owner](const auto& change) {
                        return change.owner == owner && change.condition == 0;
                    })) continue;
                auto values = combatEquipmentValues(owner, command.get());
                for (const auto& change : wear)
                    if (change.owner == owner && change.condition == 0) values.mSlots[change.slot] = {};
                const auto previous = timedEffects;
                Misc::Rng::Generator rng{combat->rng};
                if (reconcileConstants(values, index, magicCaster(index).identity, tick.value(), mRuntime.mStore,
                        mBinding.mGeneralConstants, timedEffects, &rng, mBinding.mExpandedEffects,
                        mBinding.mSpecialConditions, mBinding.mMovementEffects,
                        mBinding.mAiDecisions, index == 2 && aiNpc, index == 2 && aiUndead,
                        levitationEnabled))
                    updateResources(index, previous, timedEffects, mBinding.mKnockoutAnimation);
                combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
            }
        if (combat && mBinding.mKnockoutAnimation)
            for (size_t index = 0; index < combat->actors.size(); ++index)
            {
                if (mBinding.mActorPresentation && combat->knockedDown[index]
                    && (!mCombat->knockedDown[index] || combat->hitKnockdown[index] != mCombat->hitKnockdown[index]))
                    combat->bodyAction[index] = tick.value();
                if (combat->actors[index][8][2] <= 0) combat->knockedDown[index] = false;
                if (!combat->knockedDown[index])
                { combat->knockoutFrame[index] = 0; combat->hitKnockdown[index] = false; }
                else combat->hitRecoveryTicks[index] = 0;
            }
        if (mBinding.mPlayerMelee[0] && combat)
            for (size_t i = 0; i < combat->swings.size(); ++i)
            {
                auto& swing = combat->swings[i];
                if (!swing || !swing->pending()) continue;
                if (combat->actors[i][8][2] <= 0 || combat->knockedDown[i] || hasParalysis(timedEffects, i) || combat->hitRecoveryTicks[i])
                    swing->interruption = PlayerSwing::Incapacitated;
                else if (!swing->state.mHit
                    && !(mBinding.mAuthoritativeAim && swing->ammunition && !swing->target))
                {
                    const auto victim = mBinding.mNeighborCombat
                        ? std::ranges::find(combat->npcPlacements, swing->target)
                            - combat->npcPlacements.begin() + 2 : 2;
                    if (victim >= combat->actors.size() || combat->actors[victim][8][2] <= 0
                        || swing->targetLife != (victim == 2 ? life->generation
                            : neighborLives[victim - 3].generation))
                        swing->interruption = PlayerSwing::TargetLost;
                }
            }
        if (std::ranges::any_of(wear, [&](const auto& change) {
                return change.owner < 2 && changingWerewolfEquipment[change.owner];
        }) || std::ranges::any_of(charges, [&](const auto& change) {
            return change.owner < 2 && changingWerewolfEquipment[change.owner];
        })) throw std::invalid_argument("Werewolf equipment changed during a concurrent item effect");
        std::vector<uint64_t> neighborTargets(mBinding.mNeighborAi
            ? mBinding.mNavigatingActor->neighborSnapshots().size() : 0);
        std::vector<bool> neighborFleeing(neighborTargets.size());
        std::vector<bool> neighborFollowing(neighborTargets.size());
        if (mBinding.mNeighborAi && combat && !dueNeighborRespawn)
        {
            const auto neighbors = mBinding.mNavigatingActor->neighborSnapshots();
            if (neighbors.empty() || (mBinding.mNeighborCombat
                    && neighbors.size() != combat->actors.size() - 3))
                throw std::logic_error("Bound neighboring NPC body set absent");
            std::vector<ActorMovement> movements(neighbors.size());
            std::vector<std::optional<std::array<float, 3>>> destinations(neighbors.size());
            std::vector<std::optional<std::array<float, 3>>> fleeEnemies(neighbors.size());
            bool movingNeighbor = false;
            for (size_t i = 0; i < neighbors.size(); ++i)
            {
                const auto& neighbor = neighbors[i];
                const size_t index = i + 3;
                const AiActorContext source{index, neighbor,
                    mBinding.mNeighborCombat ? neighborLives[i].generation : 1};
                const auto disposition = mBinding.mNeighborCombat
                    ? committedDisposition(mRuntime.mStore,
                        mRuntime.ownerPtr(combatOwner(index)).getCellRef().getRefId(),
                        timedEffects, index) : ActorDisposition{};
                if (mBinding.mNeighborCombat && disposition.commanded)
                {
                    if (const auto follow = commandedDestination(index, neighbor,
                            neighborLives[i].respawnTick ? 0 : neighborLives[i].generation))
                    {
                        destinations[i] = *follow;
                        neighborFollowing[i] = true;
                        movingNeighbor = true;
                    }
                }
                const CanonicalPlayerEntityState* pursued = nullptr;
                float nearest = std::numeric_limits<float>::infinity();
                for (const auto playerId : mBinding.mPlayers)
                {
                    const auto* player = players.findPlayer(playerId);
                    if (!player || player->transform().cell() != actorCell(neighbor)
                        || combat->actors[actor(playerId)][8][2] <= 0
                        || std::ranges::none_of(players.activeSessions(), [&](const auto& session) {
                            return session.playerId() == playerId;
                        })) continue;
                    const bool engaged = std::ranges::any_of(combat->players[actor(playerId)].engagements,
                        [&](const auto& entry) { return entry.witness == neighbor.mActor && entry.fight >= 100; });
                    const auto& position = player->transform().position();
                    const float dx = float(double(position.x()) / 1024) - neighbor.mPosition[0];
                    const float dy = float(double(position.y()) / 1024) - neighbor.mPosition[1];
                    const float dz = float(double(position.z()) / 1024) - neighbor.mPosition[2];
                    const float distance = dx * dx + dy * dy + dz * dz;
                    if (distance > 2048.f * 2048.f || distance >= nearest) continue;
                    if (disposition.calm || neighborFollowing[i]) continue;
                    if (!engaged && !mBinding.mNeighborCombat) continue;
                    if (!aggressiveAgainst(source, disposition, *player,
                            engaged ? playerId.value() : 0)) continue;
                    if (!mBinding.mNavigatingActor->lineOfSight(
                            {neighbor.mPosition[0], neighbor.mPosition[1], neighbor.mPosition[2] + 110.f},
                            {float(double(position.x()) / 1024), float(double(position.y()) / 1024),
                                float(double(position.z()) / 1024) + 110.f})) continue;
                    if (mBinding.mNeighborCombat && !npcDetects(actor(playerId))) continue;
                    pursued = player;
                    nearest = distance;
                }
                if (pursued)
                {
                    const auto& position = pursued->transform().position();
                    const std::array<float, 3> enemy{float(double(position.x()) / 1024),
                        float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
                    if (!mBinding.mNeighborCombat)
                    { destinations[i] = enemy; neighborTargets[i] = pursued->playerId().value(); }
                    else
                    {
                        const auto stats = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
                        const auto victim = loadCombatStats(mRuntime.mStore,
                            combat->actors[actor(pursued->playerId())], timedEffects, actor(pursued->playerId()));
                        const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
                        const float rating = MWMechanics::fleeRating(disposition.flee,
                            stats.getHealth().getRatio(false),
                            settings.find("fAIFleeHealthMult")->mValue.getFloat(),
                            settings.find("fAIFleeFleeMult")->mValue.getFloat(),
                            MWMechanics::fightDistanceBias(std::sqrt(nearest),
                                settings.find("iFightDistanceBase")->mValue.getInteger(),
                                settings.find("fFightDistanceMultiplier")->mValue.getFloat()));
                        float attackRating = 0.f;
                        if (const auto held = mRuntime.equippedWeaponCondition(combatOwner(index)))
                        {
                            const auto values = mRuntime.installedValues(combatOwner(index));
                            const auto item = std::ranges::find(values.mObjects, held->mItem,
                                [](const auto& value) { return value.mRef.mRefNum; });
                            if (item == values.mObjects.end())
                                throw std::invalid_argument("Native neighbor flee weapon source missing");
                            if (const auto* weapon = mRuntime.mStore.get<ESM::Weapon>().search(item->mRef.mRefID))
                            {
                                AiMagicContext context{stats, &victim};
                                context.expandedEffects = mBinding.mExpandedEffects;
                                const auto scored = rateAiWeapon(context, *weapon, held->mCondition,
                                    item->mRef.mEnchantmentCharge,
                                    mBinding.mEnchantedWeaponsAreMagical, mRuntime.mStore);
                                if (scored) attackRating = *scored;
                            }
                        }
                        neighborFleeing[i] = MWMechanics::fleeOverAttack(rating, attackRating);
                        if (neighborFleeing[i])
                        {
                            fleeEnemies[i] = enemy;
                            const auto neighborPtr = mRuntime.ownerPtr(combatOwner(index));
                            const auto points = neighborPtr.getClass().isPureWaterCreature(neighborPtr)
                                ? std::vector<std::array<float, 3>>{}
                                : mBinding.mNavigatingActor->neighborFleePathgridDestinations(i);
                            float nearestEscape = std::numeric_limits<float>::infinity();
                            const float fromEnemy = std::pow(neighbor.mPosition[0] - enemy[0], 2)
                                + std::pow(neighbor.mPosition[1] - enemy[1], 2)
                                + std::pow(neighbor.mPosition[2] - enemy[2], 2);
                            for (const auto& point : points)
                            {
                                const float dx = point[0] - enemy[0], dy = point[1] - enemy[1], dz = point[2] - enemy[2];
                                if (dx*dx + dy*dy + dz*dz <= fromEnemy) continue;
                                const float x = point[0] - neighbor.mPosition[0];
                                const float y = point[1] - neighbor.mPosition[1];
                                const float z = point[2] - neighbor.mPosition[2];
                                const float distance = x*x + y*y + z*z;
                                if (distance < nearestEscape)
                                { destinations[i] = point; nearestEscape = distance; }
                            }
                        }
                        else { destinations[i] = enemy; neighborTargets[i] = pursued->playerId().value(); }
                    }
                    movingNeighbor = true;
                }
                auto& adjacentMovement = movements[i];
                const bool traveling = (destinations[i] || fleeEnemies[i])
                    && (!mBinding.mNeighborCombat || !neighborLives[i].respawnTick);
                adjacentMovement.walkSpeed = traveling ? mBinding.mNavigationSpeed : 0.f;
                if (mBinding.mMovementEffects && mBinding.mNeighborCombat)
                {
                    const auto stats = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
                    const auto magnitude = [&](ESM::RefId id) {
                        if (id == ESM::MagicEffect::Levitate && !levitationEnabled) return 0.f;
                        const auto effectIndex = uint64_t(ESM::MagicEffect::refIdToIndex(id));
                        float result = 0.f;
                        for (const auto& effect : timedEffects)
                            if (effect.actor == index && effect.effectIndex == effectIndex)
                                result += effect.magnitude;
                        return result;
                    };
                    const auto& settings = mRuntime.mStore.get<ESM::GameSetting>();
                    const auto setting = [&](const char* name) { return settings.find(name)->mValue.getFloat(); };
                    const float strength = stats.getAttribute(ESM::Attribute::Strength).getModified();
                    const float speed = stats.getAttribute(ESM::Attribute::Speed).getModified();
                    const float athletics = stats.getSkill(ESM::Skill::Athletics).getModified();
                    const float acrobatics = stats.getSkill(ESM::Skill::Acrobatics).getModified();
                    const float weight = std::max(0.f, mRuntime.storage(combatOwner(index)).getWeight()
                        - magnitude(ESM::MagicEffect::Feather) + magnitude(ESM::MagicEffect::Burden));
                    const float encumbrance = MWClass::normalizedEncumbrance(weight,
                        strength * setting("fEncumbranceStrMult"));
                    const float walk = MWClass::npcWalkSpeed(speed, encumbrance,
                        setting("fMinWalkSpeed"), setting("fMaxWalkSpeed"), setting("fEncumberedMoveEffect"));
                    const bool inert = encumbrance > 1.f || magnitude(ESM::MagicEffect::Paralyze) > 0.f
                        || combat->knockedDown[index] || combat->actors[index][8][2] <= 0
                        || (mBinding.mNeighborCombat && neighborLives[i].respawnTick);
                    adjacentMovement.enabled = true;
                    adjacentMovement.walkSpeed = traveling && !inert ? walk : 0.f;
                    adjacentMovement.swimSpeed = traveling && !inert ? MWClass::npcSwimSpeed(walk,
                        magnitude(ESM::MagicEffect::SwiftSwim), athletics,
                        setting("fSwimRunBase"), setting("fSwimRunAthleticsMult")) : 0.f;
                    adjacentMovement.flySpeed = traveling && !inert ? MWClass::npcFlySpeed(speed,
                        magnitude(ESM::MagicEffect::Levitate), encumbrance,
                        setting("fMinFlySpeed"), setting("fMaxFlySpeed"), setting("fEncumberedMoveEffect")) : 0.f;
                    adjacentMovement.jumpSpeed = !inert ? MWClass::npcJumpSpeed(encumbrance, acrobatics,
                        magnitude(ESM::MagicEffect::Jump), stats.getFatigueTerm(mRuntime.mStore), false,
                        setting("fJumpEncumbranceBase"), setting("fJumpEncumbranceMultiplier"),
                        setting("fJumpAcrobaticsBase"), setting("fJumpAcroMultiplier"),
                        setting("fJumpRunMultiplier"), Constants::GravityConst * Constants::UnitsPerMeter) : 0.f;
                    adjacentMovement.slowFall = MWClass::npcSlowFall(magnitude(ESM::MagicEffect::SlowFall));
                    adjacentMovement.levitating = magnitude(ESM::MagicEffect::Levitate) > 0.f;
                    adjacentMovement.waterWalking = magnitude(ESM::MagicEffect::WaterWalking) > 0.f;
                    adjacentMovement.waterBreathing = magnitude(ESM::MagicEffect::WaterBreathing) > 0.f;
                    adjacentMovement.unconscious = combat->knockedDown[index];
                }
            }
            if (movingNeighbor || step)
            {
                if (!step) step = mBinding.mNavigatingActor->prepareSelectedRestore(
                    mBinding.mNavigatingActor->selectedImage(), doors);
                mBinding.mNavigatingActor->prepareNeighborNavigation(*step, movements, doors, destinations, fleeEnemies);
            }
        }
        if (mBinding.mNeighborCombat && mBinding.mMovementEffects && combat && step)
        {
            const auto adjacent = step->neighborSnapshots();
            for (size_t i = 0; i < adjacent.size(); ++i)
            {
                const size_t index = i + 3;
                if (!adjacent[i].mDrowning || combat->actors[index][8][2] <= 0
                    || neighborLives[i].respawnTick
                    || std::ranges::any_of(timedEffects, [&](const auto& effect) {
                        return effect.actor == index && effect.effectIndex == waterBreathingIndex
                            && effect.magnitude > 0;
                    })) continue;
                auto victim = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
                auto health = victim.getHealth();
                health.setCurrent(health.getCurrent() - mRuntime.mStore.get<ESM::GameSetting>()
                    .find("fSuffocationDamage")->mValue.getFloat() * seconds);
                victim.setHealth(health);
                saveCombatStats(combat->actors[index], victim, timedEffects, index);
                if (victim.getHealth().getCurrent() <= 0)
                    recordNpcDeath(index, {adjacent[i].mActor, 2, neighborLives[i].generation});
            }
        }
        if (mBinding.mNeighborCombat && combat && step)
        {
            const auto adjacentSet = step->neighborSnapshots();
            if (adjacentSet.size() != combat->neighborAttacks.size())
                throw std::invalid_argument("Native neighbor attack set changed");
            for (size_t i = 0; i < adjacentSet.size(); ++i)
            {
            const size_t index = i + 3;
            auto& attack = combat->neighborAttacks[i];
            const auto* adjacent = &adjacentSet[i];
            if (adjacent->mActor != combat->npcPlacements.at(i + 1))
                throw std::invalid_argument("Native neighbor attack placement changed");
            const size_t owner = combatOwner(index);
            const auto values = combatEquipmentValues(owner, command.get());
            const auto held = mRuntime.equippedWeaponCondition(owner);
            const ESM::Weapon* weapon = nullptr;
            if (held)
            {
                const auto item = std::ranges::find(values.mObjects, held->mItem,
                    [](const auto& value) { return value.mRef.mRefNum; });
                if (item == values.mObjects.end())
                    throw std::invalid_argument("Native neighbor weapon source missing");
                weapon = mRuntime.mStore.get<ESM::Weapon>().search(item->mRef.mRefID);
            }
            const bool meleeWeapon = !weapon
                || MWMechanics::getWeaponType(weapon->mData.mType)->mWeaponClass == ESM::WeaponType::Melee;
            const auto reach = MWMechanics::getMeleeWeaponReach(mRuntime.mStore, weapon, true);
            const auto disposition = committedDisposition(mRuntime.mStore,
                mRuntime.ownerPtr(owner).getCellRef().getRefId(), timedEffects, index);
            const bool able = !(dueNeighborRespawn && *dueNeighborRespawn == i)
                && !neighborLives[i].respawnTick && combat->actors[index][8][2] > 0
                && !combat->knockedDown[index] && !hasParalysis(timedEffects, index)
                && !combat->hitRecoveryTicks[index] && !disposition.calm && !neighborFollowing[i]
                && !neighborFleeing[i] && neighborTargets[i]
                && meleeWeapon && (!held || held->mCondition > 0);
            const auto contactAt = [&](uint64_t requested) {
                return meleeContact(players, *adjacent, requested, reach);
            };
            const auto sourceSlot = values.mSlots[MWWorld::InventoryStore::Slot_CarriedRight];
            const bool sourceChanged = attack.target && !attack.state.mHit
                && (attack.source != (sourceSlot.isSet() ? wireId(sourceSlot).value() : 0)
                    || attack.weapon != (weapon ? weapon->mId.getRefIdString() : std::string{}));
            if (attack.target && (!able || sourceChanged || !PlayerId::fromValue(attack.target)
                || attack.target != neighborTargets[i]
                || combat->actors[actor(*PlayerId::fromValue(attack.target))][8][2] <= 0
                || (!attack.state.mReleased && contactAt(attack.target) != attack.target)))
            {
                auto idle = mBinding.mNeighborMeleeSet[i](nullptr, "chop");
                attack = {idle.identity(), {}, idle.snapshot(), 0, 0, 0, 0, false};
            }
            if (!attack.target && able && tick.value() / reactionTicks != mActorTick / reactionTicks)
            {
                const auto selected = contactAt(neighborTargets[i]);
                const auto id = PlayerId::fromValue(selected);
                if (id && selected == neighborTargets[i] && combat->actors[actor(*id)][8][2] > 0)
                {
                    Misc::Rng::Generator rng{combat->rng};
                    const auto mode = MWMechanics::chooseMeleeAttack(weapon, rng);
                    const std::array<std::string_view, 3> modes{"chop", "slash", "thrust"};
                    const auto direction = size_t(std::ranges::find(modes, mode) - modes.begin());
                    if (direction >= modes.size()) throw std::invalid_argument("Native neighbor attack mode invalid");
                    auto clip = mBinding.mNeighborMeleeSet[i](weapon, mode);
                    attack = {clip.identity(), weapon ? weapon->mId.getRefIdString() : std::string{},
                        clip.snapshot(), selected, tick.value(),
                        held ? wireId(held->mItem).value() : 0, direction, false};
                    combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                }
            }
            if (attack.target)
            {
                const std::array<std::string_view, 3> modes{"chop", "slash", "thrust"};
                const auto* attackWeapon = attack.weapon.empty() ? nullptr
                    : mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(attack.weapon));
                auto clip = mBinding.mNeighborMeleeSet[i](attackWeapon, modes.at(attack.direction));
                if (clip.identity() != attack.identity)
                    throw std::invalid_argument("Native neighbor attack clip changed");
                clip.restore(attack.state);
                if (!clip.snapshot().mReleased && clip.phaseCompletion() == 1.f)
                {
                    Misc::Rng::Generator rng{combat->rng};
                    const float strength = MWMechanics::resolveAttackStrength(clip.windUp(), rng);
                    combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    if (contactAt(attack.target) == attack.target && clip.release(strength))
                        breakInvisibility(index);
                }
                const auto hit = clip.advance(seconds);
                attack.state = clip.snapshot();
                if (hit)
                {
                    const bool contact = contactAt(attack.target) == attack.target;
                    attack.contact = contact;
                    auto attacker = loadCombatStats(mRuntime.mStore, combat->actors[index], timedEffects, index);
                    auto victimIndex = actor(*PlayerId::fromValue(attack.target));
                    auto victim = loadCombatStats(mRuntime.mStore, combat->actors[victimIndex], timedEffects, victimIndex);
                    addTimedResistance(attacker, timedEffects, index);
                    addTimedResistance(victim, timedEffects, victimIndex);
                    victim.setKnockedDown(combat->knockedDown[victimIndex]);
                    const float capacity = attacker.getAttribute(ESM::Attribute::Strength).getModified()
                        * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
                    const float weight = std::max(0.f, mRuntime.storage(owner).getWeight());
                    MWMechanics::applyFatigueLoss(attacker, mRuntime.mStore,
                        attackWeapon ? attackWeapon->mData.mWeight : 0.f,
                        clip.snapshot().mStrength,
                        weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity);
                    Misc::Rng::Generator rng{combat->rng};
                    const auto skill = attackWeapon
                        ? MWMechanics::getWeaponType(attackWeapon->mData.mType)->mSkill
                        : ESM::Skill::HandToHand;
                    const bool paralyzed = hasParalysis(timedEffects, victimIndex);
                    const auto chance = MWMechanics::getHitChance(mRuntime.mStore, attacker, victim,
                        int(attacker.getSkill(skill).getModified()), false, paralyzed);
                    const bool success = contact && Misc::Rng::roll0to99(rng) < chance;
                    float damage = 0.f;
                    bool blocked = false;
                    const bool healthUnarmed = victim.getKnockedDown() || paralyzed;
                    const auto stat = attackWeapon || healthUnarmed
                        ? MeleeDamageStat::Health : MeleeDamageStat::Fatigue;
                    if (success && attackWeapon)
                    {
                        const auto& range = *hit == ESM::Weapon::AT_Chop ? attackWeapon->mData.mChop
                            : *hit == ESM::Weapon::AT_Slash ? attackWeapon->mData.mSlash
                            : attackWeapon->mData.mThrust;
                        damage = range[0] + (range[1] - range[0]) * clip.snapshot().mStrength;
                        const auto& gmst = mRuntime.mStore.get<ESM::GameSetting>();
                        TES3MP::OpenMwMeleeSettings settings;
                        settings.damageStrengthBase = gmst.find("fDamageStrengthBase")->mValue.getFloat();
                        settings.damageStrengthMultiplier = gmst.find("fDamageStrengthMult")->mValue.getFloat();
                        damage = TES3MP::openMwAdjustedWeaponDamage(settings,
                            attacker.getAttribute(ESM::Attribute::Strength).getModified(),
                            attackWeapon->mData.mHealth ? float(held->mCondition) / attackWeapon->mData.mHealth : 1.f,
                            attackWeapon->mData.mHealth != 0, damage);
                    }
                    else if (success)
                        damage = healthUnarmed
                            ? MWMechanics::getUnarmedHealthDamage(mRuntime.mStore, attacker,
                                attacker.getSkill(ESM::Skill::HandToHand).getModified(), clip.snapshot().mStrength)
                            : MWMechanics::getUnarmedFatigueDamage(mRuntime.mStore, attacker,
                                attacker.getSkill(ESM::Skill::HandToHand).getModified(), clip.snapshot().mStrength);
                    const float weaponDamage = damage;
                    if (success) damage = MWMechanics::applyKnockoutDamageMultiplier(mRuntime.mStore, victim, damage);
                    if (!std::isfinite(damage) || damage < 0 || damage > 1'000'000)
                        throw std::invalid_argument("Native neighbor attack damage invalid");
                    if (success && mBinding.mExpandedEffects)
                    {
                        if (attackWeapon) applyStrike(magicCaster(index), *held, *attackWeapon,
                            attacker, victim, victimIndex, rng);
                        retaliate(attacker, victim, index, victimIndex, rng);
                        diseaseContact(victimIndex, victim, rng);
                    }
                    if (success && damage > 0)
                    {
                        const auto* player = players.findPlayer(mBinding.mPlayers[victimIndex]);
                        const auto position = player->transform().position();
                        const std::array<float, 3> defenderPosition{float(double(position.x()) / 1024),
                            float(double(position.y()) / 1024), float(double(position.z()) / 1024)};
                        const float defenderYaw = float(double(player->transform().orientation().z().value())
                            * (2.0 * std::numbers::pi_v<double> / 4294967296.0));
                        blocked = defendHit(victimIndex, victim, attacker, attackWeapon,
                            clip.snapshot().mStrength, attackWeapon ? attackWeapon->mData.mWeight : 0.f,
                            attacker.getSkill(skill).getModified(), adjacent->mPosition,
                            defenderPosition, defenderYaw, false, damage, rng);
                        const auto applied = MWMechanics::applyHitDamage(victim,
                            {{stat == MeleeDamageStat::Health ? "health" : "fatigue", damage}}, MWWorld::TimeStamp{});
                        if (victim.getHealth().getCurrent() > 0)
                            startHitRecovery(victimIndex, victim, applied, damage, blocked, rng);
                        else combat->hitRecoveryTicks[victimIndex] = 0;
                    }
                    if (attackWeapon && attackWeapon->mData.mHealth)
                    {
                        const float multiplier = mRuntime.mStore.get<ESM::GameSetting>()
                            .find("fWeaponDamageMult")->mValue.getFloat();
                        wear.push_back({owner, *held,
                            MWMechanics::weaponConditionAfterHit(held->mCondition, weaponDamage, success, multiplier)});
                    }
                    if (success && attackWeapon && !mBinding.mExpandedEffects)
                        applyStrike(magicCaster(index), *held, *attackWeapon,
                            attacker, victim, victimIndex, rng);
                    saveCombatStats(combat->actors[victimIndex], victim, timedEffects, victimIndex);
                    saveCombatStats(combat->actors[index], attacker, timedEffects, index);
                    combat->knockedDown[victimIndex] = victim.getHealth().getCurrent() > 0
                        && (fatigueKnockout(victim, mBinding.mKnockoutAnimation)
                            || combat->knockedDown[victimIndex]);
                    combat->knockedDown[index] = attacker.getHealth().getCurrent() > 0
                        && (fatigueKnockout(attacker, mBinding.mKnockoutAnimation)
                            || combat->knockedDown[index]);
                    for (const size_t body : {victimIndex, index})
                    {
                        if (combat->knockedDown[body] && !mCombat->knockedDown[body])
                            combat->bodyAction[body] = tick.value();
                        if (combat->knockedDown[body]) combat->hitRecoveryTicks[body] = 0;
                        else
                        {
                            combat->knockoutFrame[body] = 0;
                            combat->hitKnockdown[body] = false;
                        }
                    }
                    combat->rng = uint32_t(std::stoul(Misc::Rng::serialize(rng)));
                    actorHits.push_back({ActorId::fromValue(adjacent->mActor).value(),
                        PlayerId::fromValue(attack.target).value(),
                        CombatRevision::fromValue(tick.value()).value(),
                        CombatRevision::fromValue(tick.value()).value(), damage, stat,
                        contact && success, blocked, victim.getHealth().getCurrent() <= 0});
                }
                if (clip.snapshot().mPhase == MeleeAnimation::Phase::Complete)
                {
                    auto idle = mBinding.mNeighborMeleeSet[i](nullptr, "chop");
                    attack = {idle.identity(), {}, idle.snapshot(), 0, 0, 0, 0, false};
                }
            }
            }
        }
        std::unique_ptr<EquipmentRuntime::PreparedProjectileLoot> loot;
        if (!wear.empty() || !charges.empty() || !recoveries.empty())
        {
            EquipmentSessionValues stagedValues;
            wornCore = stagedWeaponCore(wear, command.get(), charges, respawn.get(), recoveries,
                recoveries.empty() ? nullptr : &stagedValues);
            if (!recoveries.empty())
            {
                if (dynamic_cast<const WorldTransaction*>(command.get()))
                    throw std::invalid_argument("Projectile recovery cannot share a world transfer tick");
                std::vector<size_t> owners;
                owners.reserve(mRuntime.ownerCount());
                for (size_t owner = 0; owner < mRuntime.ownerCount(); ++owner) owners.push_back(owner);
                loot = mRuntime.prepareProjectileLoot(stagedValues, owners);
            }
        }
        return std::make_unique<ActorTransaction>(*this, std::move(command), std::move(step),
            std::move(melee), target, contact, std::move(combat), std::move(life),
            std::move(neighborLives),
            std::move(projectiles), std::move(timedEffects), casting, std::move(respawn), std::move(loot),
            std::move(playerHits), std::move(actorHits), std::move(spellCasts),
            std::move(magicImpactCues),
            std::move(wear), std::move(charges), std::move(wornCore), std::move(relocations),
            tick.value(), velocity, report);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "native travel preparation failed: tick=%llu previous=%llu reason=%s\n",
            static_cast<unsigned long long>(tick.value()), static_cast<unsigned long long>(mActorTick), error.what());
        throw;
    }

    bool InventoryService::stageWaitRestRecovery(PreparedNativeInventory& candidate,
        const CanonicalServerState& players, const CanonicalWorldState& world,
        std::uint8_t hours, WaitRestMode mode)
    try
    {
        auto* staged = dynamic_cast<ActorTransaction*>(&candidate);
        if (!staged || &staged->service != this || staged->consumed || staged->before != mActorImage
            || !staged->combat || !hours || hours > MaximumWaitRestHours
            || (mode != WaitRestMode::Wait && mode != WaitRestMode::Rest)) return false;
        if (staged->casting || !staged->projectiles.empty()
            || std::ranges::any_of(staged->combat->swings, [](const auto& swing) { return swing && swing->pending(); })
            || std::ranges::any_of(staged->combat->playerCasts, [](const auto& cast) { return bool(cast); })
            || std::ranges::any_of(staged->combat->arrows, [](const auto& arrow) { return !arrow.terminal; })) return false;
        const auto& npc = staged->actor ? staged->actor->snapshot() : mBinding.mNavigatingActor->snapshot();
        if (staged->combat->actors[2][8][2] > 0 && staged->target)
            for (const auto& session : players.activeSessions())
            {
                const auto* player = players.findPlayer(session.playerId());
                if (player && session.playerId().value() == staged->target
                    && player->transform().cell() == actorCell(npc)) return false;
            }
        auto recovered = *staged->combat;
        auto effects = staged->timedEffects;
        const auto stuntedIndex = uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::StuntedMagicka));
        const double timeScale = world.time().timeScale() > 0 ? world.time().timeScale() : 1.0;
        const double skippedTicks = double(hours) * 3600.0 * 30.0 / timeScale;
        if (!std::isfinite(skippedTicks) || skippedTicks < 0 || skippedTicks > double(UINT64_MAX)) return false;
        for (const auto& session : players.activeSessions())
        {
            const auto* player = players.findPlayer(session.playerId());
            if (!player) return false;
            const size_t index = actor(player->playerId());
            auto stats = loadCombatStats(mRuntime.mStore, recovered.actors[index], effects, index);
            if (stats.getHealth().getCurrent() <= 0) return false;
            double magickaHours = hours;
            if (mode == WaitRestMode::Rest)
                for (const auto& effect : effects)
                {
                    if (effect.actor != index || effect.effectIndex != stuntedIndex || effect.magnitude <= 0) continue;
                    if (effect.sourceKind >= 3) { magickaHours = 0; break; }
                    if (effect.expiresTick > staged->tick)
                        magickaHours = std::min(magickaHours, std::max(0.0,
                            double(hours) - double(effect.expiresTick - staged->tick) * timeScale / (30.0 * 3600.0)));
                }
            const float capacity = stats.getAttribute(ESM::Attribute::Strength).getModified()
                * mRuntime.mStore.get<ESM::GameSetting>().find("fEncumbranceStrMult")->mValue.getFloat();
            const float weight = std::max(0.f, mRuntime.storage(index).getWeight());
            const float encumbrance = weight == 0 ? 0.f : capacity == 0 ? 1.f + 1e-6f : weight / capacity;
            MWMechanics::restoreWaitRestStats(stats, mRuntime.mStore, hours, mode == WaitRestMode::Rest,
                encumbrance, magickaHours);
            saveCombatStats(recovered.actors[index], stats, effects, index);
        }
        const auto advance = static_cast<uint64_t>(skippedTicks);
        for (auto& effect : effects)
            if (effect.effectIndex == stuntedIndex && effect.sourceKind < 3)
            {
                const auto remaining = effect.expiresTick - staged->tick;
                effect.expiresTick = remaining <= advance ? staged->tick : effect.expiresTick - advance;
            }
        std::erase_if(effects, [&](const auto& effect) { return effect.expiresTick <= staged->tick; });
        staged->combat = std::move(recovered);
        staged->timedEffects = std::move(effects);
        return true;
    }
    catch (...) { return false; }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::projectInventory(
        const CanonicalServerState& players, SessionId target, ServerTick tick, CanonicalRevision revision,
        const PreparedNativeInventory* candidate) const
    {
        const auto* moving = dynamic_cast<const ActorTransaction*>(candidate);
        if (moving && (&moving->service != this || moving->consumed || moving->before != mActorImage
            || (moving->actor && !mBinding.mNavigatingActor->canInstall(*moving->actor)))) return {};
        if (moving) candidate = moving->command.get();
        const auto* areaDoors = candidate;
        if (ownsAreaDoorCandidate(candidate)) candidate = areaDoorCommand(candidate);
        const auto* transaction = dynamic_cast<const Transaction*>(candidate);
        const auto* equipment = dynamic_cast<const EquipmentTransaction*>(candidate);
        const auto* world = dynamic_cast<const WorldTransaction*>(candidate);
        const auto* door = dynamic_cast<const DoorTransaction*>(candidate);
        const auto* teleport = dynamic_cast<const TeleportTransaction*>(candidate);
        if (candidate && ((!transaction && !equipment && !world && !door && !teleport && !ownsAreaDoorCandidate(candidate))
                || (teleport && &teleport->service != this) || (door && &door->service != this)
                || (world && &world->service != this) || (transaction && &transaction->service != this)
                || (equipment && &equipment->service != this))) return std::nullopt;
        const auto actorState = mBinding.mNavigatingActor ? std::optional(moving && moving->actor
            ? moving->actor->snapshot() : mBinding.mNavigatingActor->snapshot()) : std::nullopt;
        auto result = project(players, target, tick, revision, transaction ? &transaction->prepared : nullptr,
            equipment ? &equipment->prepared : nullptr, world ? &world->prepared : nullptr, door ? &door->prepared : nullptr,
            {}, actorState ? &*actorState : nullptr, moving ? std::span<const WeaponWear>(moving->wear) : std::span<const WeaponWear>{},
            moving ? std::span<const ItemCharge>(moving->charges) : std::span<const ItemCharge>{},
            moving && moving->combat ? &*moving->combat : nullptr,
            moving ? moving->respawn.get() : nullptr);
        if (result)
        {
            for (auto& baseline : result->containers)
            {
                const auto index = container(baseline.container);
                const auto lock = areaContainerLock(index, areaDoors);
                baseline.lockLevel = lock.level;
                baseline.contactRevision = lock.revision;
            }
            for (auto& ground : result->groundItems)
            {
                ground.doors = areaDoorSnapshots(ground.cell, areaDoors);
                for (auto& neighbor : ground.neighbors) neighbor.doors = areaDoorSnapshots(neighbor.cell, areaDoors);
            }
        }
        if (result && result->equipment && mBinding.mNavigatingActor)
        {
            const auto state = moving && moving->actor ? moving->actor->snapshot() : mBinding.mNavigatingActor->snapshot();
            if (std::ranges::any_of(result->equipment->actors, [&](const auto& owner) { return owner.actor.value() == state.mActor; }))
                result->equipment->motions.push_back({state.mActor, moving ? moving->tick : std::max<uint64_t>(1, mActorTick),
                    state.mPosition, moving ? moving->velocity : mActorVelocity, state.mYaw});
            if (mBinding.mNeighborAi)
            {
                const auto neighbors = moving && moving->actor ? moving->actor->neighborSnapshots()
                    : mBinding.mNavigatingActor->neighborSnapshots();
                const auto previous = mBinding.mNavigatingActor->neighborSnapshots();
                for (size_t i = 0; i < neighbors.size(); ++i)
                {
                    const auto& neighbor = neighbors[i];
                    if (std::ranges::none_of(result->equipment->actors, [&](const auto& owner) {
                        return owner.actor.value() == neighbor.mActor;
                    })) continue;
                    std::array<float, 3> velocity{};
                    if (moving && moving->actor)
                    {
                        for (size_t axis = 0; axis < 3; ++axis)
                            velocity[axis] = (neighbor.mPosition[axis] - previous.at(i).mPosition[axis]) * 30.f;
                    }
                    result->equipment->motions.push_back({neighbor.mActor,
                        moving ? moving->tick : std::max<uint64_t>(1, mActorTick),
                        neighbor.mPosition, velocity, neighbor.mYaw});
                }
            }
        }
        return result;
    }

    std::optional<LatestWinsCombatSnapshot> InventoryService::projectCombat(
        const CanonicalServerState& players, SessionId target, ServerTick tick, CanonicalRevision revision,
        const PreparedNativeInventory* candidate) const
    try
    {
        if (!mBinding.mCombatResolution || !mCombat || mRuntime.mFailedClosed) return {};
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player) return {};
        const auto* moving = dynamic_cast<const ActorTransaction*>(candidate);
        if (candidate && (!moving || &moving->service != this || moving->consumed || moving->before != mActorImage))
            return {};
        const auto& combat = moving && moving->combat ? *moving->combat : *mCombat;
        const std::span<const ActorCampaignTimedEffect> effects = moving ? moving->timedEffects : mTimedEffects;
        const auto combatRevision = CombatRevision::fromValue(std::max<uint64_t>(1,
            moving ? moving->tick : mActorTick)).value();
        const size_t selfIndex = actor(player->playerId());
        const auto self = loadCombatStats(mRuntime.mStore, combat.actors[selfIndex], effects, selfIndex);
        const auto knockout = [&](size_t index) -> KnockoutSnapshot {
            if (!mBinding.mKnockoutAnimation) return {};
            return {uint8_t(combat.knockedDown[index] ? (combat.hitKnockdown[index] ? 3 : 2) : 1),
                uint16_t(combat.knockoutFrame[index]),
                combat.actors[index][8][2] > 0 && hasParalysis(effects, index)};
        };
        const auto snapshot = [&](const auto& stats, auto id) {
            return PlayerCombatSnapshot{id, combatRevision, stats.getHealth().getCurrent(),
                stats.getHealth().getModified(), stats.getFatigue().getCurrent(),
                stats.getFatigue().getModified(), stats.getMagicka().getCurrent(),
                stats.getMagicka().getModified(), stats.getHealth().getCurrent() <= 0, knockout(actor(id))};
        };
        std::vector<PlayerCombatSnapshot> others;
        for (size_t index = 0; index < 2; ++index)
            if (index != selfIndex)
            {
                const auto* other = players.findPlayer(mBinding.mPlayers[index]);
                if (other && other->transform().cell() == player->transform().cell())
                    others.push_back(snapshot(loadCombatStats(mRuntime.mStore, combat.actors[index],
                        effects, index), other->playerId()));
            }
        std::vector<PlayerSwingSnapshot> swings;
        if (mBinding.mPlayerMelee[0])
            for (size_t index = 0; index < 2; ++index)
            {
                const auto owner = mBinding.mPlayers[index];
                if (index != selfIndex && std::ranges::none_of(others,
                        [&](const auto& other) { return other.playerId == owner; })) continue;
                PlayerSwingSnapshot projected{owner};
                if (const auto& swing = combat.swings[index])
                {
                    const auto* weapon = swing->weapon.empty() ? nullptr
                        : mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(swing->weapon));
                    const std::array<std::string_view, 3> directions{"chop", "slash", "thrust"};
                    auto clip = mBinding.mPlayerMelee[index](weapon, directions[swing->direction]);
                    clip.restore(swing->state);
                    // Presentation requires a nonzero life; world shots carry no
                    // target life in the durable swing and never use this value.
                    projected = {owner, swing->command, swing->source,
                        swing->targetLife ? swing->targetLife : 1,
                        uint8_t(swing->direction), uint8_t(unsigned(swing->state.mPhase) + 1),
                        uint8_t(swing->interruption), swing->strength, clip.phaseCompletion(), clip.group()};
                }
                swings.push_back(std::move(projected));
            }
        std::ranges::sort(swings, {}, &PlayerSwingSnapshot::playerId);
        std::vector<ActorCombatSnapshot> visible;
        const auto scene = moving && moving->actor ? moving->actor->snapshot() : mBinding.mNavigatingActor->snapshot();
        if (actorCell(scene) == player->transform().cell())
        {
            const auto npc = loadCombatStats(mRuntime.mStore, combat.actors[2], effects, 2);
            visible.push_back({ActorId::fromValue(scene.mActor).value(), combatRevision,
                npc.getHealth().getCurrent(), npc.getHealth().getModified(),
                npc.getFatigue().getCurrent(), npc.getFatigue().getModified(),
                npc.getMagicka().getCurrent(), npc.getMagicka().getModified(),
                npc.getHealth().getCurrent() <= 0});
            visible.back().knockout = knockout(2);
            const auto& casting = moving ? moving->casting : mNpcCast;
            if (casting)
            {
                const auto timing = mBinding.mBoundCasts->ranges[casting->range];
                auto& projected = visible.back();
                projected.castId = casting->cast; projected.castPhase = uint8_t(casting->phase);
                projected.castRange = uint8_t(casting->range); projected.castElapsed = uint16_t(casting->elapsed);
                projected.castRelease = uint16_t(timing.releaseTicks); projected.castStop = uint16_t(timing.stopTicks);
            }
            if (combat.actors.size() > 3)
            {
                const auto neighbors = moving && moving->actor ? moving->actor->neighborSnapshots()
                    : mBinding.mNavigatingActor->neighborSnapshots();
                if (neighbors.size() + 1 != combat.npcPlacements.size())
                    throw std::invalid_argument("Native projected combat neighbor differs from placement");
                for (size_t i = 0; i < neighbors.size(); ++i)
                {
                    if (combat.npcPlacements[i + 1] != neighbors[i].mActor)
                        throw std::invalid_argument("Native projected neighbor placement changed");
                    const auto stats = loadCombatStats(mRuntime.mStore, combat.actors[i + 3], effects, i + 3);
                    visible.push_back({ActorId::fromValue(neighbors[i].mActor).value(), combatRevision,
                        stats.getHealth().getCurrent(), stats.getHealth().getModified(),
                        stats.getFatigue().getCurrent(), stats.getFatigue().getModified(),
                        stats.getMagicka().getCurrent(), stats.getMagicka().getModified(),
                        stats.getHealth().getCurrent() <= 0});
                    visible.back().knockout = knockout(i + 3);
                }
            }
        }
        std::ranges::sort(visible, {}, &ActorCombatSnapshot::actorId);
        std::vector<ActorPresentationSnapshot> presentation;
        if (mBinding.mActorPresentation)
        {
            for (size_t index = 0; index < combat.actors.size(); ++index)
            {
                if (index < 2 && index != selfIndex && std::ranges::none_of(others,
                        [&](const auto& other) { return other.playerId == mBinding.mPlayers[index]; })) continue;
                if (index >= 2 && std::ranges::none_of(visible, [&](const auto& value) {
                    const auto placement = combat.npcPlacements.empty() ? scene.mActor
                        : combat.npcPlacements[index - 2];
                    return value.actorId.value() == placement;
                })) continue;
                ActorPresentationSnapshot p;
                p.id = index < 2 ? mBinding.mPlayers[index].value()
                    : combat.npcPlacements.empty() ? scene.mActor : combat.npcPlacements[index - 2];
                p.kind = index < 2 ? 1 : 2;
                p.life = index < 2 || (index > 2 && !mBinding.mNeighborCombat) ? 1 : index == 2
                    ? (moving && moving->life ? moving->life->generation : mLife->generation)
                    : (moving ? moving->neighborLives : mNeighborLives).at(index - 3).generation;
                p.dead = combat.actors[index][8][2] <= 0;
                p.movementOwned = mBinding.mMovementEffects;
                const std::array visibleEffects{ESM::MagicEffect::Invisibility, ESM::MagicEffect::Chameleon,
                    ESM::MagicEffect::Light, ESM::MagicEffect::NightEye, ESM::MagicEffect::DetectAnimal,
                    ESM::MagicEffect::DetectEnchantment, ESM::MagicEffect::DetectKey,
                    ESM::MagicEffect::Charm};
                const std::array movementEffects{ESM::MagicEffect::WaterBreathing, ESM::MagicEffect::SwiftSwim,
                    ESM::MagicEffect::WaterWalking, ESM::MagicEffect::Burden, ESM::MagicEffect::Feather,
                    ESM::MagicEffect::Jump, ESM::MagicEffect::Levitate, ESM::MagicEffect::SlowFall};
                for (const auto& effect : effects)
                    if (effect.actor == index)
                    {
                        if (!p.dead && effect.effectIndex < ESM::MagicEffect::Length)
                        {
                            const auto* visual = mRuntime.mStore.get<ESM::MagicEffect>().search(
                                ESM::MagicEffect::indexToRefId(int(effect.effectIndex)));
                            if (visual && (visual->mData.mFlags & ESM::MagicEffect::ContinuousVfx))
                                p.visualEffects.push_back(uint16_t(effect.effectIndex));
                        }
                        for (size_t i = 0; i < visibleEffects.size(); ++i)
                            if (effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(visibleEffects[i])))
                                p.visibility[i] += effect.magnitude;
                        if (mBinding.mMovementEffects)
                            for (size_t i = 0; i < movementEffects.size(); ++i)
                                if (effect.effectIndex == uint64_t(ESM::MagicEffect::refIdToIndex(movementEffects[i])))
                                    p.movement[i] += effect.magnitude;
                    }
                std::ranges::sort(p.visualEffects);
                p.visualEffects.erase(std::unique(p.visualEffects.begin(), p.visualEffects.end()), p.visualEffects.end());
                const auto setSwing = [&](const MeleeAnimation& clip, uint64_t action) {
                    p.action = action; p.group = clip.group(); p.phase = uint8_t(unsigned(clip.snapshot().mPhase) + 1);
                    p.direction = uint8_t(clip.direction()); p.strength = clip.snapshot().mStrength;
                    p.completion = clip.phaseCompletion(); p.rate = clip.phaseRate();
                };
                if (!p.dead && index < 2 && combat.swings[index] && !combat.swings[index]->interruption)
                {
                    const auto& swing = *combat.swings[index];
                    const auto* weapon = swing.weapon.empty() ? nullptr
                        : mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(swing.weapon));
                    const std::array<std::string_view, 3> directions{"chop", "slash", "thrust"};
                    auto clip = mBinding.mPlayerMelee[index](weapon, directions[swing.direction]);
                    clip.restore(swing.state); setSwing(clip, swing.command);
                    p.strength = swing.strength;
                }
                if (!p.dead && index == 2)
                {
                    const auto& clip = moving ? moving->melee : mMelee;
                    const auto target = moving ? moving->target : mMeleeTarget;
                    if (clip && target) setSwing(*clip, combat.npcAction);
                }
                if (!p.dead && index >= 3 && mBinding.mNeighborCombat)
                {
                    const auto& attack = combat.neighborAttacks.at(index - 3);
                    if (attack.target)
                    {
                        const auto* weapon = attack.weapon.empty() ? nullptr
                            : mRuntime.mStore.get<ESM::Weapon>().find(ESM::RefId::stringRefId(attack.weapon));
                        const std::array<std::string_view, 3> directions{"chop", "slash", "thrust"};
                        auto clip = mBinding.mNeighborMeleeSet.at(index - 3)(weapon, directions.at(attack.direction));
                        clip.restore(attack.state); setSwing(clip, attack.action);
                    }
                }
                p.bodyAction = combat.bodyAction[index];
                if (!p.dead && combat.knockedDown[index])
                {
                    p.bodyState = combat.hitKnockdown[index] ? 3 : 2;
                    const auto& bound = (*mBinding.mBoundHits)[index];
                    const auto timing = combat.hitKnockdown[index] ? bound.knockdown : bound.knockout;
                    p.bodyFrame = float(combat.knockoutFrame[index]); p.bodyStop = uint16_t(timing.stop);
                    p.loopStart = uint16_t(timing.loopStart); p.loopStop = uint16_t(timing.loopStop);
                }
                else if (!p.dead && combat.hitRecoveryTicks[index] && combat.hitGroup[index])
                {
                    p.bodyState = 4; p.hitGroup = uint8_t(combat.hitGroup[index]);
                    p.bodyStop = uint16_t((*mBinding.mBoundHits)[index].animations.ticks[p.hitGroup - 1]);
                    p.bodyFrame = float(p.bodyStop - combat.hitRecoveryTicks[index]);
                }
                const std::optional<ActorCampaignCast> noCast;
                const auto& pending = index < 2 ? combat.playerCasts[index]
                    : index == 2 ? (moving ? moving->casting : mNpcCast) : noCast;
                if (pending && !p.dead && p.bodyState == 1)
                {
                    const auto timing = (index < 2 ? mBinding.mPlayerCasts[index] : *mBinding.mBoundCasts).ranges[pending->range];
                    p.cast = pending->cast; p.castPhase = uint8_t(pending->phase); p.castRange = uint8_t(pending->range);
                    p.castElapsed = uint16_t(pending->elapsed); p.castRelease = uint16_t(timing.releaseTicks);
                    p.castStop = uint16_t(timing.stopTicks);
                }
                presentation.push_back(std::move(p));
            }
            std::ranges::sort(presentation, [](const auto& a, const auto& b) {
                return std::pair(a.kind, a.id) < std::pair(b.kind, b.id); });
        }
        const std::array skillIds{ESM::Skill::Block, ESM::Skill::ShortBlade, ESM::Skill::LongBlade,
            ESM::Skill::BluntWeapon, ESM::Skill::Axe, ESM::Skill::Spear, ESM::Skill::HandToHand,
            ESM::Skill::LightArmor, ESM::Skill::MediumArmor, ESM::Skill::HeavyArmor,
            ESM::Skill::Unarmored, ESM::Skill::Security, ESM::Skill::Alteration,
            ESM::Skill::Conjuration, ESM::Skill::Destruction, ESM::Skill::Illusion,
            ESM::Skill::Mysticism, ESM::Skill::Restoration, ESM::Skill::Enchant};
        std::array<CombatSkillSnapshot, ReplicatedCombatSkillCount> skills{};
        for (size_t index = 0; index < skills.size(); ++index)
        {
            const auto& stat = combat.actors[selfIndex][11 + ESM::Skill::refIdToIndex(skillIds[index])];
            skills[index] = {static_cast<ReplicatedCombatSkill>(index),
                self.getSkill(skillIds[index]).getModified(), stat[4]};
        }
        std::vector<PhysicalProjectileSnapshot> projectiles;
        std::vector<MagicProjectileSnapshot> magicProjectiles;
        if (mBinding.mRangedFlight && actorCell(scene) == player->transform().cell())
        {
            projectiles.reserve(combat.arrows.size());
            for (const auto& arrow : combat.arrows)
                projectiles.push_back({uint8_t(arrow.casterKind), uint8_t(arrow.terminal),
                    arrow.caster, arrow.casterLife, arrow.command, arrow.releaseTick, arrow.ammoRecord,
                    arrow.position, arrow.velocity});
            std::ranges::sort(projectiles, [](const auto& a, const auto& b) {
                return std::tuple(a.casterKind, a.caster, a.casterLife, a.command)
                    < std::tuple(b.casterKind, b.caster, b.casterLife, b.command);
            });
        }
        if (mBinding.mMagicProjectileCollection && actorCell(scene) == player->transform().cell())
        {
            const auto& flights = moving ? moving->projectiles : mProjectiles;
            magicProjectiles.reserve(flights.size());
            for (const auto& flight : flights)
                magicProjectiles.push_back({uint8_t(flight.casterKind == 2 ? 2 : 1), uint8_t(flight.sourceKind),
                    flight.caster, std::max<uint64_t>(1, flight.casterLife), flight.commandId,
                    magicVisualRecord(flight.sourceKind, flight.effectSource),
                    flight.position, {flight.step[0] * 30.f, flight.step[1] * 30.f, flight.step[2] * 30.f}});
            std::ranges::sort(magicProjectiles, [](const auto& a, const auto& b) {
                return std::tuple(a.casterKind, a.caster, a.casterLife, a.command)
                    < std::tuple(b.casterKind, b.caster, b.casterLife, b.command);
            });
        }
        auto created = LatestWinsCombatSnapshot::create(target, session->sessionGeneration(), tick, revision,
            player->playerId(), combatRevision, self.getHealth().getCurrent(), self.getHealth().getModified(),
            self.getFatigue().getCurrent(), self.getFatigue().getModified(), self.getMagicka().getCurrent(),
            self.getMagicka().getModified(), self.getHealth().getCurrent() <= 0,
            visible, skills, others, {}, swings, knockout(selfIndex), presentation, projectiles, magicProjectiles);
        auto* value = std::get_if<LatestWinsCombatSnapshot>(&created);
        return value ? std::optional<LatestWinsCombatSnapshot>(std::move(*value)) : std::nullopt;
    }
    catch (...) { return {}; }

    std::optional<ReliableCombatEventBatch> InventoryService::projectCombatEvents(
        const CanonicalServerState& players, SessionId target, ServerTick tick, CanonicalRevision revision,
        const PreparedNativeInventory* candidate) const
    try
    {
        if (!mBinding.mCombatResolution || !candidate) return {};
        const auto* staged = dynamic_cast<const ActorTransaction*>(candidate);
        const auto* session = players.findActiveSession(target);
        if (!staged || &staged->service != this || staged->consumed || staged->before != mActorImage
            || !session || staged->tick != tick.value()
            || (staged->playerHits.empty() && staged->actorHits.empty() && staged->spellCasts.empty()
                && staged->magicImpactCues.empty())) return {};
        const auto* observer = players.findPlayer(session->playerId());
        const auto scene = staged->actor ? staged->actor->snapshot() : mBinding.mNavigatingActor->snapshot();
        if (!observer) return {};
        const bool actorVisible = observer->transform().cell() == actorCell(scene);
        if (!actorVisible && !mBinding.mPlayerTravel) return {};
        const auto playerVisible = [&](uint64_t id) {
            const auto player = PlayerId::fromValue(id);
            const auto* caster = player ? players.findPlayer(*player) : nullptr;
            return caster && (caster->playerId() == observer->playerId()
                || caster->transform().cell() == observer->transform().cell());
        };
        auto actorEvents = staged->actorHits;
        auto playerHits = staged->playerHits;
        auto magicEvents = staged->spellCasts;
        auto cues = staged->magicImpactCues;
        if (mBinding.mPlayerTravel)
        {
            if (!actorVisible) { actorEvents.clear(); playerHits.clear(); }
            std::erase_if(magicEvents, [&](const auto& event) {
                return event.actorCaster() ? !actorVisible : !playerVisible(event.casterId());
            });
            std::erase_if(cues, [&](const auto& cue) {
                return cue.casterKind == 2 ? !actorVisible : !playerVisible(cue.caster);
            });
        }
        if (playerHits.empty() && actorEvents.empty() && magicEvents.empty() && cues.empty()) return {};
        auto created = ReliableCombatEventBatch::create(target, session->sessionGeneration(), tick,
            revision, playerHits, actorEvents, magicEvents, {}, cues);
        auto* value = std::get_if<ReliableCombatEventBatch>(&created);
        return value ? std::optional<ReliableCombatEventBatch>(std::move(*value)) : std::nullopt;
    }
    catch (...) { return {}; }

    std::optional<ServerApp::InventoryInterestDelivery> InventoryService::project(const CanonicalServerState& players,
        SessionId target, ServerTick tick, CanonicalRevision revision, const PreparedCommand* candidate,
        const EquipmentRuntime::PreparedEquipment* equipped, const EquipmentRuntime::PreparedWorldTransfer* world,
        const EquipmentRuntime::PreparedDoor* door, std::optional<CellId> area,
        const ActorSceneSnapshot* moving, std::span<const WeaponWear> wear,
        std::span<const ItemCharge> charges,
        const ActorCampaignCombat* stagedCombat,
        const EquipmentRuntime::PreparedRespawn* respawn) const
    try
    {
        if (mRuntime.mRestartActor || mRuntime.mFailedClosed) return std::nullopt;
        const auto* session = players.findActiveSession(target);
        const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
        if (!player) return std::nullopt;
        const auto visibleCell = area.value_or(player->transform().cell());
        const auto index = actor(player->playerId());
        const auto values = [&](size_t owner) {
            auto state = respawn && respawn->owner() == owner ? respawn->values()
                : world ? mRuntime.preparedValues(*world, owner)
                : equipped ? mRuntime.preparedValues(*equipped, owner)
                : candidate ? mRuntime.preparedValues(candidate->mTransfer, owner) : mRuntime.installedValues(owner);
            for (const auto& change : wear) if (owner == change.owner)
            {
                const auto item = std::ranges::find(state.mObjects, change.before.mItem,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == state.mObjects.end()) throw std::invalid_argument("Native melee projection lost weapon identity");
                item->mRef.mChargeInt = change.condition;
                if (change.condition == 0) state.mSlots[change.slot] = {};
            }
            for (const auto& charge : charges) if (owner == charge.owner)
            {
                const auto item = std::ranges::find(state.mObjects, charge.item,
                    [](const auto& object) { return object.mRef.mRefNum; });
                if (item == state.mObjects.end() || item->mRef.mEnchantmentCharge != charge.before)
                    throw std::invalid_argument("Native magic projection lost item identity");
                if (charge.consume)
                {
                    if (item->mRef.mCount <= 0)
                        throw std::invalid_argument("Native magic projection lost consumable count");
                    --item->mRef.mCount;
                    if (item->mRef.mCount == 0)
                        for (auto& slot : state.mSlots) if (slot == charge.item) slot = {};
                }
                else item->mRef.mEnchantmentCharge = charge.after;
            }
            return state;
        };
        const auto installed = values(index);
        const auto items = stacks(installed, mItemIds, mRuntime.mStore);
        std::vector<EquipmentBinding> equipment;
        static_assert(static_cast<int>(EquipmentSlot::Count) == InventoryStore::Slots);
        for (int slot = 0; slot < InventoryStore::Slots; ++slot)
            if (installed.mSlots[slot].isSet())
                equipment.push_back({static_cast<EquipmentSlot>(slot), wireId(installed.mSlots[slot])});
        const auto commandVersion = respawn ? respawn->revision()
            : world ? world->revision() : equipped ? equipped->candidate().mRevision
            : candidate ? candidate->candidate().mRevision : mWorld.getPtrRegistryRevision();
        const auto version = commandVersion + wear.size() + charges.size();
        const InventoryBaselineHeader header{ target, session->sessionGeneration(), tick, revision, 0, 1 };
        ServerApp::InventoryInterestDelivery result{ .targetSession = target };
        auto inventory = ReliablePlayerInventoryBaseline::create(header, player->playerId(),
            InventoryRevision::fromValue(version).value(), items, equipment);
        if (!std::holds_alternative<ReliablePlayerInventoryBaseline>(inventory)) return std::nullopt;
        result.playerInventory.push_back(std::get<ReliablePlayerInventoryBaseline>(std::move(inventory)));
        const auto publicSlots = [&](const PlainEquipmentValues& state) {
            std::array<std::optional<ItemPrototypeId>, static_cast<size_t>(EquipmentSlot::Count)> slots{};
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (state.mSlots[slot].isSet())
                {
                    const auto item = std::ranges::find(state.mObjects, state.mSlots[slot],
                        [](const auto& object) { return object.mRef.mRefNum; });
                    if (item == state.mObjects.end()) throw std::logic_error("Public equipment item missing");
                    slots[slot] = mItemIds.at(item->mRef.mRefID);
                }
            return slots;
        };
        std::vector<PublicActorEquipmentMember> actors;
        for (size_t i = 0; i < mBinding.mContainers.size(); ++i)
        {
            const auto& shared = mBinding.mContainers[i];
            const auto* life = stagedCombat ? stagedCombat : mCombat ? &*mCombat : nullptr;
            const bool selectedCorpse = life && i + 2 == mCombatNpcOwner && life->actors[2][8][2] <= 0;
            const auto selectedPosition = selectedCorpse
                ? std::optional(moving ? *moving : mBinding.mNavigatingActor->snapshot()) : std::nullopt;
            const auto sharedCell = selectedPosition ? actorCell(*selectedPosition) : shared.mCell;
            const auto sharedPosition = selectedPosition
                ? Position3(int64_t(std::llround(double(selectedPosition->mPosition[0]) * 1024)),
                    int64_t(std::llround(double(selectedPosition->mPosition[1]) * 1024)),
                    int64_t(std::llround(double(selectedPosition->mPosition[2]) * 1024))) : shared.mPosition;
            // Keep the authored placement controlled while its origin is visible,
            // too: otherwise a late observer could render its local frozen copy.
            // V20's fixed neighborhood always retains that origin cell.
            if (visibleCell != sharedCell
                && !(moving && shared.mId.value() == moving->mActor && visibleCell == actorCell(*moving))) continue;
            const auto owner = mRuntime.ownerPtr(i + 2);
            const auto sharedValues = values(i + 2);
            if (selectedCorpse)
                actors.push_back({shared.mId, publicSlots(sharedValues)});
            if (actorInventory(owner) && !initialCorpse(owner) && !selectedCorpse)
            {
                // Appearance only: no private stacks, counts or transfer revision.
                // Empty slots also suppress any locally selected starting gear.
                actors.push_back({shared.mId, publicSlots(sharedValues)});
                continue;
            }
            std::vector<EquipmentBinding> slots;
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (sharedValues.mSlots[slot].isSet())
                    slots.push_back({static_cast<EquipmentSlot>(slot), wireId(sharedValues.mSlots[slot])});
            auto baseline = ReliableContainerInventoryBaseline::create(header, shared.mId, sharedCell,
                sharedPosition, ContainerRevision::fromValue(version).value(), 0,
                stacks(sharedValues, mItemIds, mRuntime.mStore), slots);
            if (!std::holds_alternative<ReliableContainerInventoryBaseline>(baseline)) return std::nullopt;
            result.containers.push_back(std::get<ReliableContainerInventoryBaseline>(std::move(baseline)));
        }
        std::vector<GroundItemInterestMember> groundItems;
        std::vector<uint64_t> placements;
        std::vector<GroundItemPresentation> presentation;
        const auto* domain = worldDomain(visibleCell);
        if (domain)
        {
            for (const auto& [id, ref] : domain->mPlacements) placements.push_back(id);
            const auto state = cellWorldValues(visibleCell, world);
            for (const auto& stack : stacks(state, mItemIds, mRuntime.mStore, domain))
            {
                const auto& ref = std::ranges::find_if(state.mObjects, [&](const auto& value) {
                    return worldId(value.mRef.mRefNum, *domain) == stack.stackId;
                })->mRef;
                groundItems.push_back({stack, worldPosition(ref), WorldItemRevision::fromValue(version).value()});
                presentation.push_back({stack.stackId, {ref.mPos.rot[0], ref.mPos.rot[1], ref.mPos.rot[2]}, ref.mScale});
            }
        }
        std::optional<NativeDoorSnapshot> doorView;
        if (mBinding.mDoor && visibleCell == mBinding.mWorldItems->mCell)
        {
            const auto& state = door ? door->state() : *mRuntime.mDoorState;
            doorView = NativeDoorSnapshot{mBinding.mDoorId, door ? door->motion() : mRuntime.mDoorMotion,
                state.mPosition.rot[2], mDoorStepSeconds, uint8_t(state.mDoorState), door ? door->blocked() : mRuntime.mDoorBlocked};
        }
        std::vector<uint64_t> teleports;
        if (mBinding.mTeleportDoors)
            for (const auto& teleport : *mBinding.mTeleportDoors)
                if (teleport.mCell == visibleCell) teleports.push_back(teleport.mId);
        std::ranges::sort(teleports);
        auto ground = ReliableGroundItemBaseline::create(header, visibleCell, groundItems, placements, presentation,
            domain != nullptr, doorView, teleports, areaDoorSnapshots(visibleCell, nullptr), {},
            domain ? std::span<const NativeActorSpawn>(domain->mActorSpawns) : std::span<const NativeActorSpawn>{});
        if (!std::holds_alternative<ReliableGroundItemBaseline>(ground)) return std::nullopt;
        result.groundItems.push_back(std::get<ReliableGroundItemBaseline>(std::move(ground)));
        std::vector<PublicEquipmentMember> visible;
        for (size_t i = 0; i < 2; ++i)
            if (const auto* other = players.findPlayer(mBinding.mPlayers[i]);
                other && other->transform().cell() == visibleCell)
            {
                visible.push_back({other->playerId(), publicSlots(values(i))});
            }
        if (mBinding.mStreamExteriors && !area && visibleCell.asExterior())
        {
            for (const auto* domain : mBinding.worldDomains())
            {
                const auto* exterior = domain->mCell.asExterior();
                if (!exterior || domain->mCell == visibleCell || exterior->worldspace() != visibleCell.asExterior()->worldspace()
                    || std::abs(int64_t(exterior->gridX()) - visibleCell.asExterior()->gridX()) > 1
                    || std::abs(int64_t(exterior->gridY()) - visibleCell.asExterior()->gridY()) > 1) continue;
                auto neighbor = project(players, target, tick, revision, candidate, equipped, world, door,
                    domain->mCell, moving, wear, charges, stagedCombat);
                if (!neighbor || neighbor->groundItems.size() != 1 || !neighbor->equipment) return std::nullopt;
                result.groundItems.front().neighbors.push_back(std::move(neighbor->groundItems.front()));
                actors.insert(actors.end(), neighbor->equipment->actors.begin(), neighbor->equipment->actors.end());
                visible.insert(visible.end(), neighbor->equipment->members.begin(), neighbor->equipment->members.end());
            }
            std::ranges::sort(result.groundItems.front().neighbors, {}, &ReliableGroundItemBaseline::cell);
        }
        std::ranges::sort(visible, {}, &PublicEquipmentMember::player);
        std::ranges::sort(actors, {}, &PublicActorEquipmentMember::actor);
        actors.erase(std::unique(actors.begin(), actors.end(), [](const auto& a, const auto& b) {
            return a.actor == b.actor;
        }), actors.end());
        auto publicEquipment = LatestWinsEquipmentSnapshot::create(target, session->sessionGeneration(), tick, revision, visible, actors);
        if (!std::holds_alternative<LatestWinsEquipmentSnapshot>(publicEquipment)) return std::nullopt;
        result.equipment = std::get<LatestWinsEquipmentSnapshot>(std::move(publicEquipment));
        return result;
    }
    catch (...) { return std::nullopt; }
}
