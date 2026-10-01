#ifndef TES3MP_NATIVE_DYNAMIC_ACTOR_OWNERSHIP_HPP
#define TES3MP_NATIVE_DYNAMIC_ACTOR_OWNERSHIP_HPP

#include "actor_campaign.hpp"
#include <apps/openmw/mwmechanics/summoning.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace TES3MP::Native
{
    // Ownership is independent of combat-slot and inventory-owner indices.
    // Removing a dynamic actor must never turn its slot into a spawn point.
    // This image must join body/inventory installation in the composed actor
    // transaction; it cannot independently authorize a live spawn.
    struct SummonSourceIdentity
    {
        ActorCasterIdentity owner, caster;
        uint64_t source = 0, sourceKind = 0, startTick = 0, ordinal = 0, effect = 0;
        bool operator==(const SummonSourceIdentity&) const = default;
    };

    struct DynamicActorOwnership
    {
        static constexpr uint64_t IdentityTag = uint64_t(3) << 62;
        static constexpr uint64_t CounterLimit = (uint64_t(1) << 62) - 1;
        static constexpr size_t MaximumActors = 32;
        static constexpr size_t MaximumSources = MaximumActorTimedEffects;
        static constexpr uint64_t ImageMagic = 0x31594e4454335354; // TS3TDNY1

        struct Entry
        {
            SummonSourceIdentity source;
            uint64_t actor = 0; // Zero remembers a failed attempt, as stock does.
            ESM::RefId record;
            bool operator==(const Entry&) const = default;
        };
        uint64_t lastActor = 0;
        std::vector<Entry> entries;
        bool operator==(const DynamicActorOwnership&) const = default;

        static bool dynamic(uint64_t actor) { return (actor & IdentityTag) == IdentityTag; }
        static void validateIdentity(ActorCasterIdentity identity)
        {
            if (!identity.id || !identity.life || (identity.kind != 1 && identity.kind != 2)
                || (identity.kind == 1 && (identity.life != 1 || dynamic(identity.id)))
                || (dynamic(identity.id) && identity.life != 1))
                throw std::invalid_argument("Dynamic actor owner/caster life invalid");
        }
        static auto key(const SummonSourceIdentity& source)
        {
            return std::tuple(source.owner.kind, source.owner.id, source.owner.life,
                source.caster.kind, source.caster.id, source.caster.life,
                source.sourceKind, source.source, source.startTick, source.ordinal, source.effect);
        }
        static void validateSource(const SummonSourceIdentity& source)
        {
            validateIdentity(source.owner);
            validateIdentity(source.caster);
            if (!source.source || source.sourceKind > 5 || !source.startTick || source.ordinal >= 64
                || source.effect > 142 || !MWMechanics::isSummoningEffect(
                    ESM::MagicEffect::indexToRefId(int(source.effect))))
                throw std::invalid_argument("Dynamic actor summon source invalid");
        }
        void validate() const
        {
            if (lastActor > CounterLimit || entries.size() > MaximumSources)
                throw std::invalid_argument("Dynamic actor ownership capacity exceeded");
            std::set<decltype(key(SummonSourceIdentity{}))> sources;
            std::map<uint64_t, const Entry*> actors;
            for (const auto& entry : entries)
            {
                validateSource(entry.source);
                const auto text = entry.record.serializeText();
                if (entry.record.empty() || text.size() > 256 || text.find('\0') != std::string::npos
                    || !sources.insert(key(entry.source)).second)
                    throw std::invalid_argument("Dynamic actor record/source invalid");
                if (!entry.actor) continue;
                const uint64_t counter = entry.actor & CounterLimit;
                if (!dynamic(entry.actor) || !counter || counter > lastActor
                    || !actors.emplace(entry.actor, &entry).second)
                    throw std::invalid_argument("Dynamic actor identity invalid");
            }
            if (actors.size() > MaximumActors)
                throw std::invalid_argument("Dynamic actor body capacity exceeded");
            for (const auto& entry : entries)
            {
                // Parents predate their children. This proves an acyclic forest
                // without recursion over attacker-controlled recovery input.
                if (!dynamic(entry.source.owner.id)) continue;
                const auto parent = actors.find(entry.source.owner.id);
                if (parent == actors.end() || (entry.actor && entry.source.owner.id >= entry.actor))
                    throw std::invalid_argument("Dynamic actor ownership parent invalid");
            }
        }
        const Entry* find(const SummonSourceIdentity& source) const
        {
            const auto found = std::ranges::find(entries, source, &Entry::source);
            return found == entries.end() ? nullptr : &*found;
        }

        // The caller must establish active source membership and content-bound
        // selection before staging. Removed sources are not replay receipts.
        // The factory returns a detached body. It must never install collision,
        // inventory, registry entries or presentation before durability.
        // Copy first so a factory exception/capacity failure preserves this image.
        template<class Spawn>
        uint64_t stageSpawn(const SummonSourceIdentity& source, ESM::RefId record, Spawn&& spawn)
        {
            validate();
            validateSource(source);
            if (const auto* existing = find(source))
            {
                if (existing->record != record)
                    throw std::invalid_argument("Dynamic actor source selection changed");
                return existing->actor;
            }
            if (entries.size() == MaximumSources || lastActor == CounterLimit
                || std::ranges::count_if(entries, [](const auto& entry) { return entry.actor != 0; }) >= MaximumActors)
                throw std::length_error("Dynamic actor spawn capacity exhausted");
            auto candidate = *this;
            const uint64_t actor = IdentityTag | (lastActor + 1);
            candidate.entries.push_back({source, actor, record});
            candidate.lastActor = lastActor + 1;
            candidate.validate(); // Invalid ownership never calls the factory.
            if (!spawn(actor, record, source.owner))
            {
                candidate.entries.back().actor = 0;
                candidate.lastActor = lastActor; // Failed placement allocated no body.
            }
            candidate.validate();
            swap(candidate);
            return entries.back().actor;
        }
        // Returns the complete removed body domain in children-first order.
        // A missing identity is an idempotent removal, never a respawn request.
        std::vector<uint64_t> stageRemove(uint64_t actor)
        {
            validate();
            auto candidate = *this;
            std::set<uint64_t> removed;
            if (std::ranges::any_of(entries, [&](const auto& entry) { return entry.actor == actor && actor; }))
            {
                MWMechanics::removeSummonTree(actor, [&](uint64_t parent) {
                    std::vector<uint64_t> children;
                    for (const auto& entry : entries)
                        if (entry.actor && entry.source.owner.id == parent) children.push_back(entry.actor);
                    return children;
                }, [](uint64_t) {}, [&](uint64_t id) { removed.insert(id); });
            }
            // Allocate the result before installing any membership change.
            std::vector<uint64_t> result(removed.rbegin(), removed.rend());
            std::erase_if(candidate.entries, [&](const auto& entry) {
                return (entry.actor && removed.contains(entry.actor)) || removed.contains(entry.source.owner.id);
            });
            candidate.validate();
            swap(candidate);
            return result;
        }
        template<class ActiveSource, class LivingOwner, class FinishedDeath>
        std::vector<uint64_t> stageCleanup(ActiveSource&& activeSource, LivingOwner&& livingOwner,
            FinishedDeath&& finishedDeath)
        {
            validate();
            auto candidate = *this;
            std::vector<uint64_t> removed;
            const auto previous = candidate.entries;
            for (const auto& entry : previous)
            {
                if (!candidate.find(entry.source)) continue;
                if (activeSource(entry.source) && livingOwner(entry.source.owner)
                    && (!entry.actor || !finishedDeath(entry.actor))) continue;
                if (entry.actor)
                {
                    auto children = candidate.stageRemove(entry.actor);
                    removed.insert(removed.end(), children.begin(), children.end());
                }
                else std::erase_if(candidate.entries, [&](const auto& value) { return value.source == entry.source; });
            }
            candidate.validate();
            swap(candidate);
            return removed;
        }
        void swap(DynamicActorOwnership& other) noexcept
        {
            std::swap(lastActor, other.lastActor);
            entries.swap(other.entries);
        }
        std::vector<char> image() const
        {
            validate();
            std::vector<char> bytes;
            putAreaWord(bytes, ImageMagic);
            putAreaWord(bytes, lastActor);
            putAreaWord(bytes, entries.size());
            for (const auto& entry : entries)
            {
                for (auto identity : {entry.source.owner, entry.source.caster})
                    for (auto value : {identity.id, identity.kind, identity.life}) putAreaWord(bytes, value);
                for (auto value : {entry.source.source, entry.source.sourceKind, entry.source.startTick,
                        entry.source.ordinal, entry.source.effect, entry.actor}) putAreaWord(bytes, value);
                const auto record = entry.record.serializeText();
                putAreaWord(bytes, record.size());
                bytes.insert(bytes.end(), record.begin(), record.end());
            }
            return bytes;
        }
        static DynamicActorOwnership restore(std::span<const char> bytes)
        {
            constexpr size_t MaximumBytes = 24 + MaximumSources * (13 * 8 + 256);
            if (bytes.size() > MaximumBytes) throw std::invalid_argument("Dynamic actor image capacity exceeded");
            size_t offset = 0;
            if (getAreaWord(bytes, offset) != ImageMagic)
                throw std::invalid_argument("Dynamic actor image version invalid");
            DynamicActorOwnership candidate;
            candidate.lastActor = getAreaWord(bytes, offset);
            const auto count = getAreaWord(bytes, offset);
            if (count > MaximumSources || count > (bytes.size() - offset) / (13 * 8))
                throw std::invalid_argument("Dynamic actor image count invalid");
            candidate.entries.reserve(size_t(count));
            for (size_t i = 0; i < count; ++i)
            {
                Entry entry;
                for (auto* identity : {&entry.source.owner, &entry.source.caster})
                {
                    identity->id = getAreaWord(bytes, offset);
                    identity->kind = getAreaWord(bytes, offset);
                    identity->life = getAreaWord(bytes, offset);
                }
                for (auto* value : {&entry.source.source, &entry.source.sourceKind, &entry.source.startTick,
                        &entry.source.ordinal, &entry.source.effect, &entry.actor})
                    *value = getAreaWord(bytes, offset);
                const auto size = getAreaWord(bytes, offset);
                if (!size || size > 256 || size > bytes.size() - offset)
                    throw std::invalid_argument("Dynamic actor record length invalid");
                const std::string_view text(bytes.data() + offset, size_t(size));
                if (text.find('\0') != std::string_view::npos)
                    throw std::invalid_argument("Dynamic actor record contains null");
                entry.record = ESM::RefId::deserializeText(text);
                offset += size_t(size);
                candidate.entries.push_back(std::move(entry));
            }
            if (offset != bytes.size()) throw std::invalid_argument("Dynamic actor image trailing data");
            candidate.validate();
            return candidate;
        }
    };
}
#endif
