#ifndef TES3MP_NATIVE_DYNAMIC_ACTOR_SET_HPP
#define TES3MP_NATIVE_DYNAMIC_ACTOR_SET_HPP
#include "dynamic_actor_ownership.hpp"
#include "actor_scene.hpp"
#include <components/files/hash.hpp>
#include <set>
#include <sstream>

namespace TES3MP::Native
{
    struct DynamicActorSet
    {
        struct Body
        {
            DynamicActorBody collision;
            ESM::RefNum reference;
            ActorCasterIdentity enemy;
        };
        DynamicActorOwnership ownership;
        std::vector<Body> bodies;
        std::array<uint64_t, 2> collisionResources{};
        static std::array<uint64_t, 2> resources(const InteriorActorScene& scene)
        {
            std::istringstream bytes(scene.fingerprint());
            return Files::getHash("native-summon-collision", bytes);
        }
        void validate() const
        {
            ownership.validate();
            if (bodies.size() > DynamicActorOwnership::MaximumActors)
                throw std::invalid_argument("Dynamic actor set capacity exceeded");
            if (!bodies.empty() && collisionResources == std::array<uint64_t, 2>{})
                throw std::invalid_argument("Dynamic actor collision resources absent");
            std::set<uint64_t> actors;
            std::set<ESM::RefNum> references;
            for (const auto& body : bodies)
            {
                const auto& value = body.collision;
                const auto entry = std::ranges::find(ownership.entries, value.actor,
                    &DynamicActorOwnership::Entry::actor);
                if (!value.actor || entry == ownership.entries.end() || entry->record != value.record
                    || !actors.insert(value.actor).second || !references.insert(body.reference).second
                    || !body.reference.isSet() || body.reference.mContentFile != -1
                    || !std::isfinite(value.yaw) || std::abs(value.yaw) > 1e4f
                    || std::ranges::any_of(value.position, [](float p) { return !std::isfinite(p) || std::abs(p) > 1e7f; }))
                    throw std::invalid_argument("Dynamic actor body identity/transform invalid");
                if (body.enemy.id) DynamicActorOwnership::validateIdentity(body.enemy);
                else if (body.enemy.kind || body.enemy.life)
                    throw std::invalid_argument("Dynamic actor empty combat target invalid");
            }
            for (const auto& entry : ownership.entries)
                if (entry.actor && !actors.contains(entry.actor))
                    throw std::invalid_argument("Dynamic actor ownership has no body");
        }
        std::vector<char> image() const
        {
            validate();
            auto owners = ownership.image();
            std::vector<char> bytes;
            putAreaWord(bytes, owners.size());
            bytes.insert(bytes.end(), owners.begin(), owners.end());
            for (auto word : collisionResources) putAreaWord(bytes, word);
            putAreaWord(bytes, bodies.size());
            for (const auto& body : bodies)
            {
                putAreaWord(bytes, body.collision.actor);
                putAreaWord(bytes, body.reference.mIndex);
                for (float value : body.collision.position) putAreaWord(bytes, std::bit_cast<uint32_t>(value));
                putAreaWord(bytes, std::bit_cast<uint32_t>(body.collision.yaw));
                for (auto value : {body.enemy.id, body.enemy.kind, body.enemy.life}) putAreaWord(bytes, value);
            }
            return bytes;
        }
        static DynamicActorSet restore(std::span<const char> bytes)
        {
            size_t offset = 0;
            const auto size = getAreaWord(bytes, offset);
            if (size > bytes.size() - offset) throw std::invalid_argument("Dynamic actor ownership length invalid");
            DynamicActorSet result;
            result.ownership = DynamicActorOwnership::restore(bytes.subspan(offset, size_t(size)));
            offset += size_t(size);
            for (auto& word : result.collisionResources) word = getAreaWord(bytes, offset);
            const auto count = getAreaWord(bytes, offset);
            if (count > DynamicActorOwnership::MaximumActors || count > (bytes.size() - offset) / 72)
                throw std::invalid_argument("Dynamic actor body count invalid");
            for (uint64_t i = 0; i < count; ++i)
            {
                Body body;
                body.collision.actor = getAreaWord(bytes, offset);
                const auto reference = getAreaWord(bytes, offset);
                if (!reference || reference > UINT32_MAX) throw std::invalid_argument("Dynamic actor reference invalid");
                body.reference = {uint32_t(reference), -1};
                const auto entry = std::ranges::find(result.ownership.entries, body.collision.actor,
                    &DynamicActorOwnership::Entry::actor);
                if (entry == result.ownership.entries.end()) throw std::invalid_argument("Dynamic actor body unowned");
                body.collision.record = entry->record;
                const auto number = [&] {
                    const auto word = getAreaWord(bytes, offset);
                    if (word > UINT32_MAX) throw std::invalid_argument("Dynamic actor float encoding invalid");
                    return std::bit_cast<float>(uint32_t(word));
                };
                for (auto& p : body.collision.position) p = number();
                body.collision.yaw = number();
                body.enemy = {getAreaWord(bytes, offset), getAreaWord(bytes, offset), getAreaWord(bytes, offset)};
                result.bodies.push_back(body);
            }
            if (offset != bytes.size()) throw std::invalid_argument("Dynamic actor image trailing data");
            result.validate();
            return result;
        }
    };
}
#endif
