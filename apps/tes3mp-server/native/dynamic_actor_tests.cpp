#include "dynamic_actor_ownership.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace TES3MP::Native;
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    template<class Operation> void rejects(Operation&& operation, const char* message)
    {
        try { operation(); }
        catch (const std::exception&) { return; }
        throw std::runtime_error(message);
    }
    SummonSourceIdentity source(uint64_t ordinal = 0, ActorCasterIdentity owner = {10, 1, 1})
    {
        return {owner, {10, 1, 1}, 100, 0, 1, ordinal,
            uint64_t(ESM::MagicEffect::refIdToIndex(ESM::MagicEffect::SummonScamp))};
    }
    const auto record = ESM::RefId::stringRefId("test_summon");
    bool spawn(uint64_t, ESM::RefId, ActorCasterIdentity) { return true; }

    void ownership()
    {
        DynamicActorOwnership live;
        auto staged = live;
        const auto first = staged.stageSpawn(source(), record, spawn);
        require(live.entries.empty() && live.lastActor == 0, "Preparation mutated the live domain");
        size_t calls = 0;
        require(staged.stageSpawn(source(), record, [&](auto, auto, auto) { ++calls; return true; }) == first
            && calls == 0, "Source replay created another actor");
        const auto otherOwner = staged.stageSpawn(source(0, {11, 1, 1}), record, spawn);
        require(otherOwner != first, "Two owners alias summon identity");
        const auto otherLife = staged.stageSpawn(source(0, {500, 2, 2}), record, spawn);
        require(otherLife != first, "A new owner life aliases an old source");
        const auto childSource = source(1, {first, 2, 1});
        const auto child = staged.stageSpawn(childSource, record, spawn);
        const auto grandchild = staged.stageSpawn(source(2, {child, 2, 1}), record, spawn);
        const auto failed = source(3, {child, 2, 1});
        staged.stageSpawn(failed, record, [](auto, auto, auto) { return false; });
        require(staged.find(failed) && !staged.find(failed)->actor, "Failed source was not remembered");
        live.swap(staged); // The enclosing actor commit installs only after durability.
        auto removed = live.stageRemove(first);
        require(removed == std::vector<uint64_t>{grandchild, child, first}, "Nested removal order/domain differs");
        require(!live.find(failed) && live.entries.size() == 2, "Removal left descendant sources or removed another owner");
        require(live.stageRemove(first).empty(), "Repeated removal was not idempotent");
        const auto next = live.stageSpawn(source(4), record, spawn);
        require(next > grandchild, "Removed identity was reused");
        const auto image = live.image();
        auto recovered = DynamicActorOwnership::restore(image);
        calls = 0;
        require(recovered.stageSpawn(source(4), record, [&](auto, auto, auto) { ++calls; return true; }) == next
            && !calls && recovered.image() == image, "Recovery reran creation");
        const auto clean = recovered.stageCleanup([](const auto&) { return false; },
            [](auto) { return true; }, [](auto) { return false; });
        require(clean.size() == 3 && recovered.entries.empty(), "Expiry/dispel did not remove owned actors");
        require(DynamicActorOwnership::restore(recovered.image()).entries.empty(), "Restart resurrected removed summons");
        auto failedLive = DynamicActorOwnership{};
        failedLive.stageSpawn(source(), record, [](auto, auto, auto) { return false; });
        const auto failedImage = failedLive.image();
        auto failedRecovery = DynamicActorOwnership::restore(failedImage);
        failedRecovery.stageSpawn(source(), record, [&](auto, auto, auto) { ++calls; return true; });
        require(!calls && failedRecovery.image() == failedImage, "Failed recovery retried every frame");
        failedRecovery.stageCleanup([](const auto&) { return true; }, [](auto) { return false; }, [](auto) { return false; });
        require(failedRecovery.entries.empty(), "Owner death retained a dormant source");
    }

    void capacityRollback()
    {
        DynamicActorOwnership live;
        for (size_t i = 0; i < DynamicActorOwnership::MaximumActors; ++i)
            live.stageSpawn(source(i), record, spawn);
        const auto before = live.image();
        size_t calls = 0;
        rejects([&] { live.stageSpawn(source(33), record, [&](auto, auto, auto) { ++calls; return true; }); },
            "Body capacity admitted another actor");
        require(!calls && live.image() == before, "Capacity rejection leaked creation or identity");
        live.stageRemove(live.entries.front().actor);
        const auto next = live.stageSpawn(source(34), record, spawn);
        require(next == DynamicActorOwnership::IdentityTag + 33, "Capacity was not reclaimed without reusing identity");
        live.stageRemove(live.entries.front().actor);
        const auto valid = live.image();
        auto invalid = source(35, {DynamicActorOwnership::IdentityTag + 100, 2, 1});
        rejects([&] { live.stageSpawn(invalid, record, spawn); }, "Missing dynamic owner accepted");
        require(live.image() == valid, "Invalid owner leaked mutation");
        live.stageRemove(live.entries.front().actor);
        const auto previous = live.image();
        rejects([&] { live.stageSpawn(source(36), record, [](auto, auto, auto) -> bool {
            throw std::runtime_error("detached construction failed");
        }); }, "Factory exception accepted");
        require(live.image() == previous, "Construction failure consumed identity or source");
        auto failed = DynamicActorOwnership{};
        for (size_t i = 0; i < DynamicActorOwnership::MaximumSources; ++i)
        {
            auto key = source(i % 64);
            key.source += i;
            failed.stageSpawn(key, record, [](auto, auto, auto) { return false; });
        }
        const auto failedBefore = failed.image();
        rejects([&] { failed.stageSpawn(source(1, {99, 1, 1}), record, spawn); }, "Dormant source capacity ignored");
        require(failed.image() == failedBefore, "Source capacity rejection changed image");
        DynamicActorOwnership exhausted;
        exhausted.lastActor = DynamicActorOwnership::CounterLimit;
        rejects([&] { exhausted.stageSpawn(source(), record, spawn); }, "Identity overflow accepted");
    }

    void recovery()
    {
        DynamicActorOwnership live;
        const auto first = live.stageSpawn(source(), record, spawn);
        live.stageSpawn(source(1, {first, 2, 1}), record, spawn);
        const auto bytes = live.image();
        require(DynamicActorOwnership::restore(bytes) == live, "Ownership recovery changed state");
        for (size_t size = 0; size < bytes.size(); ++size)
            rejects([&] { (void)DynamicActorOwnership::restore(std::span(bytes).first(size)); }, "Truncated ownership image accepted");
        auto trailing = bytes;
        trailing.push_back(0);
        rejects([&] { (void)DynamicActorOwnership::restore(trailing); }, "Trailing ownership data accepted");
        auto damaged = live;
        damaged.entries[1].actor = first;
        rejects([&] { (void)damaged.image(); }, "Duplicate actor identity accepted");
        damaged = live;
        damaged.entries[0].source.owner = {live.entries[1].actor, 2, 1};
        rejects([&] { (void)damaged.image(); }, "Ownership cycle accepted");
        damaged = live;
        damaged.entries[1].source.owner.life = 2;
        rejects([&] { (void)damaged.image(); }, "Dynamic permanent respawn life accepted");
        damaged = live;
        damaged.entries[1].source = damaged.entries[0].source;
        rejects([&] { (void)damaged.image(); }, "Duplicate source accepted");
        damaged = live;
        damaged.lastActor = 0;
        rejects([&] { (void)damaged.image(); }, "Future actor identity accepted");
        // The active-source result short-circuits owner lookup; exercise a later
        // query failure after an earlier candidate subtree was removed.
        auto cleanup = live;
        auto other = source(2, {11, 1, 1});
        cleanup.stageSpawn(other, record, spawn);
        const auto before = cleanup.image();
        rejects([&] { cleanup.stageCleanup([&](const auto& value) { return value == other; },
            [](auto) -> bool { throw std::runtime_error("owner query failed"); }, [](auto) { return false; }); },
            "Cleanup query failure accepted");
        require(cleanup.image() == before, "Cleanup exception leaked partial removal");
        const auto removed = live.stageCleanup([](const auto&) { return true; }, [](auto) { return true; },
            [&](auto id) { return id == first; });
        require(removed.size() == 2 && live.entries.empty(), "Finished death did not cascade");
    }

    void stockSeams()
    {
        DynamicActorOwnership family;
        uint64_t ordinal = 0;
        // Parameterize the family once; stock review establishes individual
        // behavior. The seam test proves each effect routes through one lookup.
        for (const auto& [effect, setting] : MWMechanics::summonSettings())
        {
            size_t reads = 0;
            require(MWMechanics::isSummoningEffect(effect), "Summon family member missing");
            const auto selected = MWMechanics::selectSummonedCreature(effect, [&](auto name) {
                ++reads;
                require(name == setting, "Wrong stock GMST selected");
                return std::string("overridden_") + std::string(name);
            });
            require(reads == 1 && !selected.empty(), "Selection cached or ignored the store");
            require(MWMechanics::selectSummonedCreature(effect, [](auto) { return std::string("another_loadout"); })
                == ESM::RefId::stringRefId("another_loadout"), "Different loadout retained cached selection");
            auto effectSource = source(ordinal++);
            effectSource.effect = uint64_t(ESM::MagicEffect::refIdToIndex(effect));
            family.stageSpawn(effectSource, selected, spawn);
        }
        require(family.entries.size() == 22 && DynamicActorOwnership::restore(family.image()) == family,
            "Summon family did not share ownership/recovery");
        require(!MWMechanics::isSummoningEffect(ESM::MagicEffect::BoundDagger), "Equipment classified as summon");
        const auto unsupported = MWMechanics::selectSummonedCreature(ESM::MagicEffect::BoundDagger,
            [](auto) -> std::string { throw std::runtime_error("must not read GMST"); });
        require(unsupported.empty(), "Unknown effect returned a summon");
        size_t tries = 0;
        const auto point = MWMechanics::summonSpawnPoint({}, 0, 0, 120, true,
            [&](const auto& candidate, const auto& origin) {
                require(origin == osg::Vec3f(0, 0, 20) && candidate.z() == 30, "Stock slope placement changed");
                return ++tries == 2;
            });
        require(point == osg::Vec3f(120, 0, 30) && tries == 2, "Stock fallback direction changed");
        tries = 0;
        const auto fallback = MWMechanics::summonSpawnPoint({}, 0, 0, 120, true,
            [&](const auto&, const auto&) { ++tries; return false; });
        require(tries == 4 && fallback == osg::Vec3f(0, -120, 30), "Blocked stock placement lost its final fallback");
        tries = 0;
        MWMechanics::summonSpawnPoint({}, 0, 0, 120, false,
            [&](const auto&, const auto&) { ++tries; return false; });
        require(!tries, "Non-actor queried collision");
        std::vector<uint64_t> events;
        uint64_t remembered = 0;
        auto identity = MWMechanics::createSummon<uint64_t>(record,
            [&](auto, auto& id) { events.push_back(1); id = 7; },
            [&](auto id) { require(id == 7, "Follow lost created identity"); events.push_back(2); },
            [&](auto id) { remembered = id; events.push_back(3); }, [](const auto&) {});
        require(identity == 7 && remembered == 7 && events == std::vector<uint64_t>{1, 2, 3}, "Stock creation order changed");
        identity = MWMechanics::createSummon<uint64_t>(record,
            [](auto, auto&) { throw std::runtime_error("placement failed"); },
            [](auto) { throw std::runtime_error("follow must not execute"); },
            [&](auto id) { remembered = id; }, [](const auto&) {});
        require(!identity && !remembered, "Failed placement was not dormant");
        identity = MWMechanics::createSummon<uint64_t>(record,
            [](auto, auto& id) { id = 8; },
            [](auto) { throw std::runtime_error("follow setup failed"); },
            [&](auto id) { remembered = id; }, [](const auto&) {});
        require(identity == 8 && remembered == 8, "Failure after placement lost cleanup ownership");
        events.clear();
        MWMechanics::removeSummonTree(uint64_t(1), [](auto id) {
            return id == 1 ? std::vector<uint64_t>{2} : std::vector<uint64_t>{};
        }, [&](auto id) { events.push_back(id); }, [&](auto id) { events.push_back(10 + id); });
        require(events == std::vector<uint64_t>{1, 2, 12, 11}, "Stock nested delete/purge order changed");
    }
}

int main(int argc, char** argv)
try
{
    if (argc != 2) throw std::invalid_argument("Select ownership, capacity, recovery or stock-seams");
    const std::string_view filter(argv[1]);
    if (filter == "ownership") ownership();
    else if (filter == "capacity") capacityRollback();
    else if (filter == "recovery") recovery();
    else if (filter == "stock-seams") stockSeams();
    else throw std::invalid_argument("Unknown dynamic actor test filter");
    std::cout << "PASS dynamic-actors " << filter << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL dynamic-actors: " << error.what() << '\n';
    return 1;
}
