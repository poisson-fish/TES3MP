#include "world_script.hpp"
#include "personal_script.hpp"
#include "loadout.hpp"

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadnpc.hpp>

#include <algorithm>
#include <charconv>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::Native;
    void require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }
    template<class Operation> void rejects(Operation&& operation, const char* message)
    {
        try { operation(); }
        catch (const std::exception&) { return; }
        throw std::runtime_error(message);
    }
    ESM::Script script(std::string body)
    {
        ESM::Script value; value.blank();
        value.mId = ESM::RefId::stringRefId("ambient_pulse_74");
        value.mScriptText = "begin ambient_pulse_74\nshort announced\nlong calls\nfloat phase\n"
            + body + "\nend ambient_pulse_74\n";
        return value;
    }
    std::vector<ESM::Global> globals()
    {
        ESM::Global counter; counter.blank(); counter.mId = ESM::RefId::stringRefId("sharedpulse");
        counter.mValue.setType(ESM::VT_Long); counter.mValue.setInteger(0);
        ESM::Global phase; phase.blank(); phase.mId = ESM::RefId::stringRefId("ambientphase");
        phase.mValue.setType(ESM::VT_Float); phase.mValue.setFloat(0);
        return {counter, phase};
    }
    ESM::Script pulse()
    {
        return script("set calls to calls + 1\nset phase to phase + 0.25\n"
            "set sharedpulse to sharedpulse + 1\nset ambientphase to phase\n"
            "if ( announced == 0 )\nMessageBox \"The bell rings.\"\nset announced to 1\nendif");
    }
    double local(const SharedWorldScript::State& state, std::string_view id)
    {
        for (const auto& [name, value] : state.runtime.script.mLocals.mVariables)
            if (name == id) return value.getFloat();
        throw std::runtime_error("Missing fixture local");
    }
    void personal(CredentialCrypto& crypto)
    {
        auto core = std::make_shared<const SharedWorldScript>(pulse(), globals(), "campaign-one", crypto);
        const std::vector<PersonalScripts::Definition> policy{{11, 31, core}, {22, 31, core}, {11, 32, core}};
        PersonalScripts runtime(7, policy, crypto);
        auto state = runtime.initial(); const auto initial = runtime.encode(state);
        MWWorld::ESMStore content;
        struct Resolver final : MWScript::ReferenceResolver
        {
            MWWorld::Ptr resolve(const MWScript::ExecutionContext&, const ESM::RefId&, bool) const override
            { throw std::runtime_error("Core personal invocation unexpectedly resolved a physical reference"); }
        } resolver;
        auto invocation = state[0].last; invocation.invocation = 1; invocation.causalEvent = 53;
        auto first = runtime.prepare(state, invocation, content, resolver);
        require(first.result.executed && first.result.outcome == SharedWorldScript::Outcome::Committed
            && first.result.presentation == std::vector<std::string>{"The bell rings."} && runtime.encode(state) == initial,
            "Personal preparation leaked or lost staged output");
        state.swap(first.state);
        require(state[0].runtime.tick == 1 && local(state[0].runtime, "calls") == 1
            && state[0].runtime.runtime.globals.front().mValue.getInteger() == 1
            && state[1].runtime.tick == 0 && state[2].runtime.tick == 0, "Characters/stories shared a writer");
        const auto committed = runtime.encode(state);
        auto replay = runtime.prepare(state, invocation, content, resolver);
        require(replay.result.outcome == SharedWorldScript::Outcome::Committed && !replay.result.executed
            && replay.result.presentation.empty() && replay.image.empty(), "Exact personal replay executed twice");
        for (int field = 0; field < 8; ++field)
        {
            auto invalid = invocation;
            if (field == 0) ++invalid.campaign;
            if (field == 1) ++invalid.physicalScope;
            if (field == 2) invalid.character = 99;
            if (field == 3) invalid.story = 99;
            if (field == 4) invalid.sourceReference = invalid.sourceLife = 9;
            if (field == 5) invalid.invocation = 3;
            if (field == 6) ++invalid.causalEvent; // Conflicting replay of the same invocation.
            if (field == 7) { invalid.invocation = 2; } // Already consumed event under a new invocation.
            auto rejected = runtime.prepare(state, invalid, content, resolver);
            require(rejected.result.outcome == SharedWorldScript::Outcome::Rejected && !rejected.result.executed
                && rejected.result.presentation.empty() && runtime.encode(state) == committed, "Invalid personal identity mutated state");
        }
        invocation.invocation = 2; invocation.causalEvent = 0;
        auto next = runtime.prepare(state, invocation, content, resolver);
        require(next.result.executed && next.state[0].eventCursor == 53, "Event-free call forgot consumed event");
        state.swap(next.state); invocation.invocation = 3; invocation.causalEvent = 53;
        require(runtime.prepare(state, invocation, content, resolver).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Old causal event became reusable after event-free call");
        invocation.causalEvent = 54;
        state = runtime.prepare(state, invocation, content, resolver).state;
        auto other = state[1].last; other.invocation = 1; other.causalEvent = 53;
        state = runtime.prepare(state, other, content, resolver).state;
        const auto saved = runtime.encode(state);
        PersonalScripts restarted(7, policy, crypto);
        require(restarted.encode(restarted.restore(saved)) == saved && state[1].eventCursor == 53
            && state[2].eventCursor == 0, "Personal cursor/recovery crossed ownership");
        for (int fault = 0; fault < 5; ++fault)
        {
            auto corrupt = saved;
            if (fault == 0) corrupt.resize(47);
            if (fault == 1) corrupt.push_back('x');
            if (fault == 2) corrupt[8] ^= 1;
            if (fault == 3) std::fill_n(corrupt.begin() + 120, 8, char(-1));
            if (fault == 4) corrupt[48 + 24] ^= 1;
            rejects([&] { runtime.restore(corrupt); }, "Malformed personal image accepted");
        }
        PersonalScripts foreign(8, policy, crypto);
        rejects([&] { foreign.restore(saved); }, "Foreign personal campaign restored");
        auto changed = policy; changed[0].story = 71;
        PersonalScripts rebound(7, changed, crypto);
        rejects([&] { rebound.restore(saved); }, "Changed story policy restored");
        auto duplicate = policy; duplicate.push_back(policy.front());
        rejects([&] { PersonalScripts invalid(7, duplicate, crypto); }, "Duplicate personal writer admitted");
        duplicate.assign(PersonalScripts::MaximumInstances + 1, policy.front());
        rejects([&] { PersonalScripts invalid(7, duplicate, crypto); }, "Personal allocation budget exceeded");
        std::cout << "personal: isolated stock globals/locals, exact/conflicting replay, event cursor, policy/bounds/recovery\n";
    }
    void personalScheduling(CredentialCrypto& crypto)
    {
        auto core = std::make_shared<const SharedWorldScript>(pulse(), globals(), "campaign-one", crypto);
        const std::vector<PersonalScripts::Definition> policy{{11, 31, core}, {22, 31, core}, {11, 32, core}};
        PersonalScripts runtime(7, policy, crypto, true);
        auto state = runtime.initial(); const auto initial = runtime.encode(state);
        MWWorld::ESMStore content;
        struct Resolver final : MWScript::ReferenceResolver
        {
            MWWorld::Ptr resolve(const MWScript::ExecutionContext&, const ESM::RefId&, bool) const override
            { throw std::runtime_error("Scheduled core script resolved a physical reference"); }
        } resolver;
        const std::array<uint64_t, 1> alice{11}, bob{22};
        auto first = runtime.prepareScheduled(state, 3, alice, content, resolver);
        require(first.result.outcome == SharedWorldScript::Outcome::Committed && first.invocations == 2
            && first.result.presentation.empty() && runtime.encode(state) == initial,
            "Scheduled preparation leaked state/output or counted inactive instances");
        state.swap(first.state);
        require(state[0].runtime.tick == 1 && state[0].scheduledTick == 3 && state[1].runtime.tick == 0
            && state[2].runtime.tick == 1 && local(state[2].runtime, "calls") == 1,
            "Scheduled state crossed characters/stories or caught up skipped ticks");
        const auto saved = runtime.encode(state);
        const auto replay = runtime.prepareScheduled(state, 3, alice, content, resolver);
        require(replay.result.outcome == SharedWorldScript::Outcome::Committed && !replay.result.executed
            && replay.image.empty(), "Same-tick schedule replayed");
        auto other = runtime.prepareScheduled(state, 3, bob, content, resolver);
        require(other.invocations == 1 && other.state[1].scheduledTick == 3, "Same-tick activation lost another owner");
        const auto offline = runtime.prepareScheduled(state, 8, {}, content, resolver);
        require(offline.result.outcome == SharedWorldScript::Outcome::Committed && !offline.result.executed
            && offline.image.empty() && runtime.encode(state) == saved, "Offline scheduling mutated state");
        PersonalScripts restarted(7, policy, crypto, true);
        state = restarted.restore(saved);
        auto next = restarted.prepareScheduled(state, 20, bob, content, resolver);
        require(next.invocations == 1 && next.state[1].runtime.tick == 1 && next.state[1].scheduledTick == 20
            && next.state[0].runtime.tick == 1, "Restart/offline catch-up added executions");
        auto call = next.state[0].last; call.invocation = 2; call.causalEvent = 41;
        auto trusted = restarted.prepare(next.state, call, content, resolver);
        require(trusted.invocations == 1 && trusted.state[0].scheduledTick == 3,
            "Trusted call changed the scheduling cursor");
        auto continued = restarted.prepareScheduled(trusted.state, 21, alice, content, resolver);
        require(continued.state[0].runtime.tick == 3 && continued.state[0].eventCursor == 41
            && continued.state[0].last.causalEvent == 0, "Scheduled call forgot a trusted event receipt");
        for (const std::vector<uint64_t> invalid : {std::vector<uint64_t>{11, 11}, {99},
                std::vector<uint64_t>(PersonalScripts::MaximumInstances + 1, 11)})
            require(runtime.prepareScheduled(state, 21, invalid, content, resolver).result.outcome
                == SharedWorldScript::Outcome::Rejected, "Invalid active-character set admitted");
        require(runtime.prepareScheduled(state, 2, alice, content, resolver).result.outcome
            == SharedWorldScript::Outcome::Rejected, "Schedule accepted a backwards tick");
        PersonalScripts legacy(7, policy, crypto);
        rejects([&] { legacy.restore(saved); }, "Scheduled state silently downgraded");
        rejects([&] { runtime.restore(legacy.encode(legacy.initial())); }, "Trusted state silently upgraded");
        auto malformed = saved; std::fill_n(malformed.begin() + 120, 8, char(-1));
        rejects([&] { runtime.restore(malformed); }, "Malformed scheduled script image admitted");
        auto broken = std::make_shared<const SharedWorldScript>(script("set calls to 9\nwhile ( 1 )\nendwhile"),
            globals(), "campaign-one", crypto);
        PersonalScripts stalled(7, {{11, 31, core}, {11, 32, broken}}, crypto, true);
        const auto before = stalled.initial(); const auto beforeImage = stalled.encode(before);
        auto rejected = stalled.prepareScheduled(before, 1, alice, content, resolver);
        require(rejected.result.outcome == SharedWorldScript::Outcome::Rejected && !rejected.result.executed
            && rejected.state.empty() && rejected.image.empty() && rejected.invocations == 0
            && rejected.result.presentation.empty() && stalled.encode(before) == beforeImage
            && rejected.result.diagnostic.starts_with("character=11 story=32 scope=1 source=0 life=0 ")
            && rejected.result.diagnostic.size() <= SharedWorldScript::MaximumDiagnosticBytes,
            "Late scheduled failure retained an earlier instance's effects");
        std::vector<PersonalScripts::Definition> maximum;
        for (uint64_t story = 1; story <= PersonalScripts::MaximumInstances; ++story) maximum.push_back({11, story, core});
        PersonalScripts full(7, maximum, crypto, true);
        const auto bounded = full.prepareScheduled(full.initial(), 1, alice, content, resolver);
        require(bounded.invocations == PersonalScripts::MaximumInvocationsPerTransaction
            && bounded.image.size() <= PersonalScripts::MaximumImageBytes, "Full scheduled policy exceeded its budget");
        std::cout << "personal scheduling: active ownership, silent replay, offline/skipped ticks, event cursors, "
            "late failure rollback, bounded policy and exact/version-bound recovery\n";
    }
    void scriptBanks(CredentialCrypto& crypto)
    {
        using Definition = PersonalScripts::Definition;
        const auto id = [](std::string_view name) { return ESM::RefId::stringRefId(name); };
        const auto records = [&](std::string suffix = "") {
            std::vector<ESM::Script> scripts;
            for (const auto& [name, body] : std::vector<std::pair<std::string, std::string>>{
                {"ledger_x73", "set ledger_x73.calls to calls + 1\nset token to token + 1\n"
                    "set relay_q19.calls to relay_q19.calls + 5\nset relay_q19.phase to relay_q19.phase + 0.25\n"
                    "StartScript relay_q19\nStopScript ledger_x73\nset sharedpulse to sharedpulse + 1"},
                {"relay_q19", "set calls to calls + 1\nset ledger_x73.token to ledger_x73.token + 2\n"
                    "set sharedpulse to sharedpulse + 10\nStopScript relay_q19"},
                {"clock_b41", "set calls to calls + 1\nif ( calls == 2 )\nStartScript ledger_x73\nendif\n" + suffix}})
            {
                ESM::Script record; record.blank(); record.mId = id(name);
                record.mScriptText = "begin " + name + "\nshort token\nlong calls\nfloat phase\n" + body + "\nend " + name;
                scripts.push_back(std::move(record));
            }
            return scripts;
        };
        const auto policy = [&](const std::vector<ESM::Script>& scripts) {
            std::vector<std::shared_ptr<const SharedWorldScript>> programs;
            for (const auto& record : scripts)
                programs.push_back(std::make_shared<const SharedWorldScript>(record, globals(), "campaign-one", crypto, scripts));
            Definition entry{11, 31, programs[0], {{programs[1], false}, {programs[2], true}}};
            if (programs.size() == 4) entry.members.push_back({programs[3], true});
            auto bob = entry; bob.character = 22;
            auto other = entry; other.story = 32;
            return std::vector{entry, bob, other};
        };
        const auto definitions = policy(records());
        PersonalScripts runtime(7, definitions, crypto, true, true);
        MWWorld::ESMStore content;
        struct Resolver final : MWScript::ReferenceResolver
        {
            MWWorld::Ptr resolve(const MWScript::ExecutionContext&, const ESM::RefId&, bool) const override
            { throw MWScript::ScriptContextUnavailable(); }
        } resolver;
        auto state = runtime.initial(); const auto initial = runtime.encode(state);
        const std::array<uint64_t, 1> alice{11}, bob{22};
        auto first = runtime.prepareScheduled(state, 3, alice, content, resolver);
        require(first.result.outcome == SharedWorldScript::Outcome::Committed && first.invocations == 6
            && first.result.presentation.empty() && runtime.encode(state) == initial, "Bank preparation leaked state");
        state.swap(first.state);
        require(local(state[0].runtime, "calls") == 1 && local(state[0].runtime, "token") == 3
            && local(state[0].members[0], "calls") == 6 && local(state[0].members[0], "phase") == .25
            && !state[0].runtime.runtime.script.mRunning && !state[0].members[0].runtime.script.mRunning
            && state[0].members[1].runtime.script.mRunning && state[0].runtime.runtime.globals[0].mValue.getInteger() == 11
            && state[0].members[0].runtime.globals.empty() && state[1].last.invocation == 0
            && local(state[2].runtime, "calls") == 1, "Bank locals/flags/globals crossed writers or lost self aliases");
        const auto saved = runtime.encode(state);
        require(!runtime.prepareScheduled(state, 3, alice, content, resolver).result.executed,
            "Same-tick bank replayed");
        PersonalScripts restarted(7, definitions, crypto, true, true);
        state = restarted.restore(saved);
        require(restarted.encode(state) == saved, "Bank recovery changed stock locals/running flags");
        auto second = restarted.prepareScheduled(state, 20, alice, content, resolver);
        require(second.invocations == 2 && second.state[0].runtime.tick == 1
            && second.state[0].runtime.runtime.script.mRunning && second.state[0].scheduledTick == 20,
            "Stopped bank member ran or restart caught up ticks");
        auto third = restarted.prepareScheduled(second.state, 21, alice, content, resolver);
        require(third.invocations == 6 && local(third.state[0].runtime, "calls") == 2
            && local(third.state[0].members[0], "calls") == 12, "Restart reset locals or ran a member twice");
        auto other = runtime.prepareScheduled(state, 4, bob, content, resolver);
        require(other.invocations == 3 && other.state[0].last.invocation == 1
            && other.state[1].last.invocation == 1, "Active bank ownership lost isolation");
        require(!runtime.prepareScheduled(state, 10, {}, content, resolver).result.executed, "Offline banks ran");
        auto stopped = definitions; stopped.resize(1); stopped[0].running = false; stopped[0].members.clear();
        PersonalScripts dormant(7, stopped, crypto, true, true);
        auto marker = dormant.prepareScheduled(dormant.initial(), 8, alice, content, resolver);
        require(marker.result.executed && marker.invocations == 1 && marker.state[0].scheduledTick == 8
            && !dormant.restore(marker.image)[0].runtime.runtime.script.mRunning
            && marker.state[0].runtime.tick == 0, "Stopped bank failed to commit its bounded marker");
        auto call = marker.state[0].last; ++call.invocation;
        require(dormant.prepare(marker.state, call, content, resolver, 0).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Stopped bank bypassed the invocation budget");
        auto receipt = dormant.prepare(marker.state, call, content, resolver, 1);
        require(receipt.result.executed && receipt.invocations == 1 && receipt.state[0].runtime.tick == 0
            && !dormant.prepare(receipt.state, call, content, resolver, 0).result.executed,
            "Stopped receipt executed a script or charged an exact replay");
        auto limited = runtime.initial()[0].last; limited.invocation = 1;
        const auto saturated = runtime.prepare(runtime.initial(), limited, content, resolver, 2);
        require(saturated.result.outcome == SharedWorldScript::Outcome::Rejected && saturated.state.empty()
            && saturated.image.empty() && !saturated.result.executed, "Bank exceeded the remaining host budget");
        for (const std::string suffix : {"StartScript foreign", "ledger_x73->StartScript relay_q19"})
        {
            PersonalScripts invalid(7, policy(records(suffix)), crypto, true, true);
            const auto before = invalid.initial(); const auto bytes = invalid.encode(before);
            auto failed = invalid.prepareScheduled(before, 1, alice, content, resolver);
            require(failed.result.outcome == (suffix.starts_with("ledger") ? SharedWorldScript::Outcome::Deferred
                    : SharedWorldScript::Outcome::Rejected)
                && !failed.result.executed && failed.state.empty() && failed.image.empty() && failed.invocations == 0
                && invalid.encode(before) == bytes, "Late bank rejection/wait retained locals/flags/globals");
        }
        auto malformed = state; malformed[0].members[0].tick = 99;
        rejects([&] { runtime.encode(malformed); }, "Future member ordinal admitted");
        for (size_t size : {size_t(0), saved.size() - 1, saved.size() + 1})
        { auto bytes = saved; bytes.resize(size); rejects([&] { runtime.restore(bytes); }, "Malformed bank envelope admitted"); }
        auto changed = definitions; changed[0].members[0].running = true;
        PersonalScripts rebound(7, changed, crypto, true, true);
        rejects([&] { rebound.restore(saved); }, "Bank running policy changed without a fresh campaign");
        auto legacyProgram = std::make_shared<const SharedWorldScript>(pulse(), globals(), "campaign-one", crypto);
        PersonalScripts legacy(7, {{11, 31, legacyProgram}}, crypto, true);
        rejects([&] { legacy.restore(saved); }, "Bank state downgraded");
        rejects([&] { runtime.restore(legacy.encode(legacy.initial())); }, "Single-script state upgraded");
        auto duplicate = definitions; duplicate[0].members.push_back({duplicate[0].script, false});
        rejects([&] { PersonalScripts invalid(7, duplicate, crypto, true, true); }, "Duplicate bank writer admitted");
        auto maximumRecords = records();
        ESM::Script meter; meter.blank(); meter.mId = id("meter_l93");
        meter.mScriptText = "begin meter_l93\nlong calls\nset calls to calls + 1\nend meter_l93";
        maximumRecords.push_back(std::move(meter));
        auto full = policy(maximumRecords); full.push_back(full[0]); full.back().story = 33;
        PersonalScripts bounded(7, full, crypto, true, true);
        const auto maximum = bounded.prepareScheduled(bounded.initial(), 1, std::array<uint64_t, 2>{11, 22}, content, resolver);
        require(maximum.invocations == PersonalScripts::MaximumInvocationsPerTransaction
            && maximum.image.size() <= PersonalScripts::MaximumImageBytes,
            "Maximum admitted bank exceeded transaction/image bounds");
        full.push_back(full[0]); full.back().story = 35;
        rejects([&] { PersonalScripts invalid(7, full, crypto, true, true); }, "Campaign bank budget exceeded");
        std::cout << "script banks: typed/self locals, lifecycle/order, isolated globals, offline/skipped/replayed ticks, "
            "late rejection/wait rollback, stopped markers, stock/version-bound recovery and budgets\n";
    }

    void shared(CredentialCrypto& crypto)
    {
        SharedWorldScript runtime(pulse(), globals(), "campaign-one", crypto);
        auto state = runtime.initial();
        std::vector<char> durable;
        std::size_t writes = 0;
        const auto sink = [&](std::span<const char> bytes) { durable.assign(bytes.begin(), bytes.end()); ++writes; return true; };
        auto first = runtime.transact(state, 1, sink);
        require(first.outcome == SharedWorldScript::Outcome::Committed && first.executed && writes == 1
            && local(state, "calls") == 1 && local(state, "phase") == .25
            && state.runtime.globals[0].mValue.getInteger() == 1
            && state.runtime.globals[1].mValue.getFloat() == .25f, "Stock script mutation differs");
        require(first.presentation == std::vector<std::string>{"The bell rings."}, "Literal presentation missing");
        const auto observerA = state, observerB = state;
        require(runtime.encode(observerA) == durable && runtime.encode(observerB) == durable,
            "Observer views differ from the single committed state");
        const auto replay = runtime.transact(state, 1, sink);
        require(replay.outcome == SharedWorldScript::Outcome::Committed && !replay.executed
            && replay.presentation.empty() && writes == 1 && runtime.encode(state) == durable,
            "Second observer/retry executed the shared occurrence twice");
        const auto second = runtime.transact(state, 2, sink);
        require(second.executed && second.presentation.empty() && local(state, "calls") == 2
            && state.runtime.globals[0].mValue.getInteger() == 2, "Distinct tick was suppressed");
        require(first.trace.executed > 0 && first.trace.instruction >= 0, "Execution trace missing");
        const auto before = durable;
        require(runtime.transact(state, 4, sink).outcome == SharedWorldScript::Outcome::Rejected
            && runtime.transact(state, 0, sink).outcome == SharedWorldScript::Outcome::Rejected
            && runtime.encode(state) == before && writes == 2, "Invalid schedule consumed state");
    }
    void rollback(CredentialCrypto& crypto)
    {
        SharedWorldScript runtime(pulse(), globals(), "campaign-one", crypto);
        auto state = runtime.initial(); const auto before = runtime.encode(state);
        std::size_t attempts = 0;
        auto failed = runtime.transact(state, 1, [&](auto bytes) {
            ++attempts; require(!bytes.empty(), "Missing prepared image"); return false;
        });
        require(failed.outcome == SharedWorldScript::Outcome::Deferred && failed.presentation.empty()
            && !failed.executed && attempts == 1 && runtime.encode(state) == before, "Failed durability leaked state/output");
        failed = runtime.transact(state, 1, [](auto) -> bool { throw std::runtime_error("injected persistence failure"); });
        require(failed.outcome == SharedWorldScript::Outcome::Deferred && runtime.encode(state) == before
            && failed.presentation.empty(), "Thrown persistence failure leaked state/output");
        const auto retry = runtime.transact(state, 1, [](auto) { return true; });
        require(retry.executed && local(state, "calls") == 1 && retry.presentation.size() == 1,
            "Deferred retry consumed locals or presentation twice");

        for (const auto& body : {std::string("set calls to 5\nset sharedpulse to 7\nMessageBox \"staged\"\nwhile ( 1 )\nendwhile"),
            std::string("set calls to 5\nset sharedpulse to 7\nMessageBox \"choose\", \"Yes\", \"No\"")})
        {
            SharedWorldScript invalid(script(body), globals(), "campaign-one", crypto);
            auto unchanged = invalid.initial(); const auto image = invalid.encode(unchanged);
            std::size_t writes = 0;
            const auto rejected = invalid.transact(unchanged, 1, [&](auto) { ++writes; return true; });
            require(rejected.outcome == SharedWorldScript::Outcome::Rejected && rejected.presentation.empty()
                && !writes && invalid.encode(unchanged) == image && !rejected.diagnostic.empty()
                && rejected.diagnostic.size() <= SharedWorldScript::MaximumDiagnosticBytes,
                "Runtime rejection leaked locals/globals/presentation or deactivated script");
            if (body.find("while") != std::string::npos)
                require(rejected.trace.executed == MWScript::WorldScript::MaximumExecutedInstructions,
                    "Infinite script did not stop at the numeric instruction budget");
            require(invalid.transact(unchanged, 1, [](auto) { return true; }).outcome == SharedWorldScript::Outcome::Rejected,
                "Rejected occurrence was silently consumed");
        }
    }
    void recovery(CredentialCrypto& crypto)
    {
        SharedWorldScript runtime(pulse(), globals(), "campaign-one", crypto);
        auto state = runtime.initial();
        std::vector<char> saved;
        require(runtime.transact(state, 1, [&](auto bytes) { saved.assign(bytes.begin(), bytes.end()); return true; }).executed,
            "Fixture commit failed");
        SharedWorldScript restarted(pulse(), globals(), "campaign-one", crypto);
        auto recovered = restarted.restore(saved);
        require(restarted.encode(recovered) == saved && local(recovered, "announced") == 1,
            "Stock GSCR/GLOB recovery changed committed state");
        std::size_t writes = 0;
        const auto replay = restarted.transact(recovered, 1, [&](auto) { ++writes; return true; });
        require(!replay.executed && replay.presentation.empty() && !writes, "Restart replay duplicated output");
        const auto next = restarted.transact(recovered, 2, [](auto) { return true; });
        require(next.executed && next.presentation.empty() && local(recovered, "calls") == 2,
            "Reentry/restart reset script locals or invocation cursor");
        auto changed = pulse(); changed.mScriptText += "\n; changed winning source\n";
        SharedWorldScript otherSource(changed, globals(), "campaign-one", crypto);
        SharedWorldScript otherCampaign(pulse(), globals(), "campaign-two", crypto);
        auto otherGlobals = globals(); otherGlobals[0].mValue.setInteger(3);
        SharedWorldScript otherDefaults(pulse(), otherGlobals, "campaign-one", crypto);
        rejects([&] { otherSource.restore(saved); }, "Changed source accepted old image");
        rejects([&] { otherCampaign.restore(saved); }, "Changed campaign accepted old image");
        rejects([&] { otherDefaults.restore(saved); }, "Changed ownership/default accepted old image");
        for (std::size_t size : {std::size_t(0), std::size_t(15), saved.size() - 1})
            rejects([&] { restarted.restore(std::span(saved).first(size)); }, "Truncated image accepted");
        auto extra = saved; extra.push_back(0);
        rejects([&] { restarted.restore(extra); }, "Trailing bytes accepted");
        auto length = saved; std::fill_n(length.begin() + 4, 4, char(-1));
        rejects([&] { restarted.restore(length); }, "Oversized record accepted");
        auto badLocal = state; badLocal.runtime.script.mLocals.mVariables.front().first = "unknown";
        rejects([&] { runtime.encode(badLocal); }, "Unknown local silently accepted");
        auto target = state; target.runtime.script.mTargetId = ESM::RefId::stringRefId("player");
        rejects([&] { runtime.encode(target); }, "Targeted script crossed the world context boundary");
        require(runtime.encode(state) == saved, "Recovery failure mutated committed source state");
    }
    void admission(CredentialCrypto& crypto)
    {
        SharedWorldScript noOp(script(""), {}, "campaign-one", crypto);
        auto declarationState = noOp.initial();
        const auto emptyInvocation = noOp.transact(declarationState, 1, [](auto) { return true; });
        require(emptyInvocation.executed && emptyInvocation.trace.executed == 0 && emptyInvocation.presentation.empty()
            && local(declarationState, "calls") == 0
            && noOp.encode(noOp.restore(noOp.encode(declarationState))) == noOp.encode(declarationState),
            "Declaration-only stock script cannot commit/recover its locals");
        for (const auto& body : {"Journal \"quest\" 10", "player->AddItem \"gold_001\" 1",
            "set unowned to 1", "MessageBox \"%999999999d\", 1", "MessageBox \"^PCName\""})
            rejects([&] { SharedWorldScript runtime(script(body), globals(), "campaign-one", crypto); },
                "Unsupported/unowned/personal operation admitted");
        auto duplicate = globals(); duplicate.push_back(duplicate.front());
        rejects([&] { SharedWorldScript runtime(pulse(), duplicate, "campaign-one", crypto); }, "Duplicate global ownership admitted");
        auto lossy = globals(); lossy[0].mValue.setInteger(16777217);
        rejects([&] { SharedWorldScript runtime(pulse(), lossy, "campaign-one", crypto); }, "Lossy stock global admitted");
        SharedWorldScript overflow(script("set sharedpulse to 16777217"), globals(), "campaign-one", crypto);
        auto precise = overflow.initial(); const auto preciseImage = overflow.encode(precise);
        require(overflow.transact(precise, 1, [](auto) { return true; }).outcome == SharedWorldScript::Outcome::Rejected
            && overflow.encode(precise) == preciseImage, "Lossy runtime global committed before recovery");
        auto oversized = pulse(); oversized.mScriptText.resize(MWScript::WorldScript::MaximumSourceBytes + 1, ' ');
        rejects([&] { SharedWorldScript runtime(oversized, globals(), "campaign-one", crypto); }, "Oversized source admitted");
        std::string declarations;
        for (std::size_t i = 0; i <= MWScript::WorldScript::MaximumLocals; ++i)
            declarations += "long local" + std::to_string(i) + "\n";
        rejects([&] { SharedWorldScript runtime(script(declarations), globals(), "campaign-one", crypto); },
            "Local allocation budget exceeded");
        SharedWorldScript runtime(script("set phase to 1000000000000000000000000000000.0 * 1000000000000000000000000000000.0"),
            globals(), "campaign-one", crypto);
        auto state = runtime.initial(); const auto image = runtime.encode(state);
        require(runtime.transact(state, 1, [](auto) { return true; }).outcome == SharedWorldScript::Outcome::Rejected
            && runtime.encode(state) == image, "Nonfinite script result committed");
        std::string messages;
        for (std::size_t i = 0; i <= MWScript::WorldScript::MaximumMessages; ++i) messages += "MessageBox \"bounded\"\n";
        SharedWorldScript noisy(script(messages), globals(), "campaign-one", crypto);
        auto quiet = noisy.initial(); const auto quietImage = noisy.encode(quiet);
        const auto rejected = noisy.transact(quiet, 1, [](auto) { return true; });
        require(rejected.outcome == SharedWorldScript::Outcome::Rejected && rejected.presentation.empty()
            && noisy.encode(quiet) == quietImage, "Presentation budget leaked output/state");
    }
    void content(CredentialCrypto& crypto, const char* config)
    {
        const char* args[]{"script-content-test", "--config", config};
        Loadout loadout(readLoadoutOptions(3, args));
        std::size_t admitted = 0, rejected = 0;
        for (const auto* name : {"fargoth", "vodunius nuccius", "chargen dock guard", "chargen boat guard 1"})
        {
            const auto* npc = loadout.store().get<ESM::NPC>().find(ESM::RefId::stringRefId(name));
            require(!npc->mScript.empty(), "Expected excluded stock NPC script missing");
            const auto* record = loadout.store().get<ESM::Script>().find(npc->mScript);
            bool accepted = false;
            try
            {
                SharedWorldScript declarations(*record, {}, loadout.contentFingerprint(), crypto);
                auto state = declarations.initial();
                const auto invocation = declarations.transact(state, 1, [](auto) { return true; });
                require(invocation.executed && invocation.trace.executed == 0 && invocation.presentation.empty(),
                    "Stock declaration script unexpectedly executes gameplay");
                accepted = true;
                ++admitted;
                std::cout << "excluded actor=" << name << " script=" << npc->mScript
                    << " declarations-only; reference/dialogue ownership still unavailable\n";
            }
            catch (const std::exception& error)
            {
                ++rejected;
                std::cout << "excluded actor=" << name << " script=" << npc->mScript << " reason="
                    << std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes) << '\n';
                const std::string_view diagnostic(error.what());
                const auto at = diagnostic.find(": line ");
                if (at != std::string_view::npos)
                {
                    unsigned line = 0;
                    const auto parsed = std::from_chars(diagnostic.data() + at + 7, diagnostic.data() + diagnostic.size(), line);
                    if (parsed.ec == std::errc{} && line && line <= MWScript::WorldScript::MaximumSourceBytes)
                    {
                        std::istringstream source(record->mScriptText); std::string instruction;
                        for (unsigned i = 0; i < line && std::getline(source, instruction); ++i) {}
                        std::cout << "first-rejected-source-line=" << line << " instruction="
                            << std::string_view(instruction).substr(0, SharedWorldScript::MaximumDiagnosticBytes) << '\n';
                    }
                }
            }
            require(accepted == (std::string_view(name) == "fargoth"), "Stock script admission classification changed");
        }
        require(admitted == 1 && rejected == 3, "Excluded stock script coverage differs");
    }

    // Verify unchanged Seyda Neen and Tamriel Rebuilt quests against the real
    // content store: admission, two-character isolation, concurrent dialogue,
    // failed-write/unstaged rollback, waiting/reserved restart, and recovery.
    void questContent(CredentialCrypto& crypto, const char* config, bool tr)
    {
        const char* args[]{"quest-content-test", "--config", config};
        Loadout loadout(readLoadoutOptions(3, args));
        const auto content = loadout.contentFingerprint();
        const auto id = [](std::string_view name) { return ESM::RefId::stringRefId(name); };
        struct NoFallback final : MWScript::ReferenceResolver
        {
            MWWorld::Ptr resolve(const MWScript::ExecutionContext&, const ESM::RefId&, bool) const override
            { throw std::runtime_error("Quest content test unexpectedly fell through to unscoped resolver"); }
        } noFallback;
        struct IntentResolver final : MWScript::ReferenceResolver
        {
            MWWorld::Ptr actor;
            MWWorld::Ptr player;
            MWWorld::Ptr resolve(const MWScript::ExecutionContext&, const ESM::RefId& refId, bool) const override
            {
                if (refId == "player") return player;
                if (refId.empty() || refId == actor.getCellRef().getRefId()) return actor;
                throw std::runtime_error("Unexpected reference lookup in quest content test");
            }
        };
        MWMechanics::NpcStats publicStats(loadout.store()), privateStats(loadout.store());
        publicStats.setHealth(MWMechanics::DynamicStat<float>(50, 50, 50), MWWorld::TimeStamp{});
        privateStats.setHealth(MWMechanics::DynamicStat<float>(50, 50, 50), MWWorld::TimeStamp{});
        ESM::Position pose{};
        const std::array<uint64_t, 2> both{11, 22};

        if (tr)
        {
            const auto* npc = loadout.store().get<ESM::NPC>().find(id("TR_m1_T_Eldale"));
            require(npc->mScript == id("TR_m1_T_EldaleSc"), "TR riddles owning NPC script changed");
            const auto* record = loadout.store().get<ESM::Script>().find(npc->mScript);
            const auto* hortDeath = loadout.store().get<ESM::Script>().find(id("TR_m0_TelvanniHortDeathSc"));
            const auto* unseatingDralSc = loadout.store().get<ESM::Script>().find(id("TR_m1_HT_UnseatingDralSc"));
            const std::vector<ESM::Script> bank{*record, *hortDeath, *unseatingDralSc};
            const std::vector<ESM::Global> owned{*loadout.store().get<ESM::Global>().find(id("TR_m1_HT_EldaleDead"))};
            const auto* quest = loadout.store().get<ESM::Dialogue>().find(id("TR_M1_GS_MQ_4"));
            const auto* unseatingDral = loadout.store().get<ESM::Dialogue>().find(id("TR_m1_HT_UnseatingDral"));
            const auto* unseatingDralEldale = loadout.store().get<ESM::Dialogue>().find(id("TR_m1_HT_UnseatingDral_Eldale"));
            const auto* topic = loadout.store().get<ESM::Dialogue>().find(id("puzzling riddles"));
            require(quest->mType == ESM::Dialogue::Journal && topic->mType == ESM::Dialogue::Topic
                && std::ranges::any_of(quest->mInfo, [](const auto& info) {
                    return info.mData.mJournalIndex == 50 && info.mQuestStatus == ESM::DialInfo::QS_Finished;
                }) && std::ranges::any_of(topic->mInfo, [&](const auto& info) {
                    return info.mActor == npc->mId && info.mResultScript.find("TR_M1_GS_MQ_4 50") != std::string::npos;
                }), "Unchanged TR riddles completion branch missing");

            auto eldalePrimary = std::make_shared<const SharedWorldScript>(
                *record, owned, content, crypto, bank, npc->mId, true);
            auto hortDeathMember = std::make_shared<const SharedWorldScript>(
                *hortDeath, owned, content, crypto, bank, npc->mId, true);
            require(eldalePrimary->encode(eldalePrimary->restore(eldalePrimary->encode(eldalePrimary->initial())))
                    == eldalePrimary->encode(eldalePrimary->initial())
                && hortDeathMember->encode(hortDeathMember->restore(hortDeathMember->encode(hortDeathMember->initial())))
                    == hortDeathMember->encode(hortDeathMember->initial()),
                "Unchanged TR Duel of Riddles source bank failed admission or round-trip");
            for (const auto& info : topic->mInfo)
                if (!info.mResultScript.empty())
                    require(!eldalePrimary->compileResult(info.mResultScript, true, npc->mId).mInstructions.empty(),
                        "Unchanged TR puzzling riddles result script failed admission");

            const std::vector<ESM::Dialogue> dialogues{*quest, *unseatingDral, *unseatingDralEldale, *topic};
            const PersonalScripts::EncounterBinding binding{401, npc->mId, id("Eldale")};
            std::vector<PersonalScripts::Definition> defs;
            for (const uint64_t ch : both)
                defs.push_back({ch, 601, eldalePrimary, {{hortDeathMember, false}}, true, dialogues, binding});
            PersonalScripts policy(95, defs, crypto, true, true, true, true, true, true, true, true, true, true, true);
            auto state = policy.initial();
            const std::array<PersonalScripts::PlayerInventory, 2> emptyInvs{{{11, {}}, {22, {}}}};
            std::vector<PersonalScripts::LifeEvent> events;
            PersonalScripts::Source liveSource{401, 1, 0, npc->mId, true, 0, 0, events, &publicStats, pose,
                &privateStats, false, false, 0, 0, false, 0, emptyInvs};

            // 1. Scheduled source-bank tick runs unchanged TR_m1_T_EldaleSc (sets TR_Map = 1).
            auto tick1 = policy.prepareScheduled(state, 1, both, loadout.store(), noFallback, &liveSource);
            require(tick1.result.executed && local(tick1.state[0].runtime, "tr_map") == 1
                && local(tick1.state[1].runtime, "tr_map") == 1
                && tick1.state[0].runtime.runtime.globals.front().mValue.getInteger() == 0
                && policy.encode(policy.restore(tick1.image)) == tick1.image,
                "TR_m1_T_EldaleSc scheduled tick failed");
            state.swap(tick1.state);

            // 2. Concurrent dialogue: TR_M1_GS_MQ_4 starts at index 5 (from Ultern); A (11) and B (22) open "puzzling riddles" (index 10) and A finishes Duel of Riddles (index 50, finished=1).
            state[0].dialogue.quests.push_back({id("TR_M1_GS_MQ_4"), 5, 0});
            state[1].dialogue.quests.push_back({id("TR_M1_GS_MQ_4"), 5, 0});
            MWWorld::ManualRef eldaleRef(loadout.store(), npc->mId);
            MWWorld::ManualRef playerRef(loadout.store(), npc->mId);
            IntentResolver intentResolver;
            intentResolver.actor = eldaleRef.getPtr();
            intentResolver.player = playerRef.getPtr();
            MWMechanics::NpcStats playerStats(loadout.store()), speakerStats(loadout.store());
            speakerStats.setBaseDisposition(npc->mNpdt.mDisposition);
            const MWScript::DialogueState::Stamp stamp{0, 7, 16};
            uint64_t cmd = 1;
            const auto talk = [&](PersonalScripts::State& s, uint64_t ch, int choice, uint64_t life = 1) {
                const size_t idx = ch == 11 ? 0 : 1;
                DialogueIntent req{601, 401, life, s[idx].dialogueRevision, "puzzling riddles", choice};
                return policy.prepareIntent(s, ch, cmd++, req, "Eldale", loadout.store(), intentResolver, 4,
                    &playerStats, {}, &speakerStats, stamp);
            };
            auto aStart = talk(state, 11, -1);
            require(aStart.result.executed && aStart.state[0].dialogue.quests.size() == 1
                && aStart.state[0].dialogue.quests.front().mState == 5
                && aStart.state[0].choices.size() == 5 && aStart.state[1].dialogue.quests.front().mState == 5,
                ("TR riddles initial greeting failed or leaked to B: " + aStart.result.diagnostic).c_str());
            state.swap(aStart.state);

            auto bStart = talk(state, 22, -1);
            require(bStart.result.executed && bStart.state[1].dialogue.quests.size() == 1
                && bStart.state[1].dialogue.quests.front().mState == 5
                && bStart.state[1].choices.size() == 5,
                ("Concurrent B greeting on puzzling riddles failed: " + bStart.result.diagnostic).c_str());
            state.swap(bStart.state);

            const auto rightChoiceForStage = [&](int stage) {
                for (const auto& info : topic->mInfo)
                {
                    if (info.mActor != npc->mId || info.mSelects.size() < 2) continue;
                    const auto& jSel = info.mSelects[0];
                    const auto& cSel = info.mSelects[1];
                    if (jSel.mFunction == ESM::DialogueCondition::Function_Journal
                        && std::holds_alternative<int32_t>(jSel.mValue) && std::get<int32_t>(jSel.mValue) == stage
                        && cSel.mFunction == ESM::DialogueCondition::Function_Choice
                        && cSel.mComparison == ESM::DialogueCondition::Comp_Eq
                        && std::holds_alternative<int32_t>(cSel.mValue))
                        return int(std::get<int32_t>(cSel.mValue));
                }
                throw std::runtime_error("Missing Comp_Eq choice for TR_M1_GS_MQ_4 stage " + std::to_string(stage));
            };
            const std::array<std::pair<int, int>, 5> riddleSteps{{
                {rightChoiceForStage(5), 10},
                {rightChoiceForStage(10), 20},
                {rightChoiceForStage(20), 30},
                {rightChoiceForStage(30), 40},
                {rightChoiceForStage(40), 50}}};

            // Wrong answer branch in unchanged TR_Mainland.esm executes Goodbye -> rejected atomically without mutating state.
            const auto beforeWrong = policy.encode(state);
            const int wrongChoice = riddleSteps[0].first == 1 ? 2 : 1;
            auto wrongStep = talk(state, 11, wrongChoice);
            require(wrongStep.result.outcome == SharedWorldScript::Outcome::Rejected && wrongStep.state.empty()
                && policy.encode(state) == beforeWrong,
                "Wrong riddle choice (Goodbye branch) did not roll back atomically");

            // A steps through all 5 stages to completion (index 50, finished=1):
            // Stage 5 was opened by aStart; each subsequent stage opens "puzzling riddles" (-1) then submits the answer choice.
            for (size_t sIdx = 0; sIdx < riddleSteps.size(); ++sIdx)
            {
                if (sIdx > 0)
                {
                    auto ask = talk(state, 11, -1);
                    require(ask.result.executed && !ask.state[0].choices.empty(),
                        ("A failed to ask next TR_M1_GS_MQ_4 riddle: " + ask.result.diagnostic).c_str());
                    state.swap(ask.state);
                }
                const auto [choice, expectedIndex] = riddleSteps[sIdx];
                auto step = talk(state, 11, choice);
                require(step.result.executed && step.state[0].dialogue.quests.front().mState == expectedIndex,
                    ("A failed to advance TR_M1_GS_MQ_4 riddle chain: " + step.result.diagnostic).c_str());
                state.swap(step.state);
            }
            require(state[0].dialogue.quests.front().mFinished == 1 && state[0].choices.empty()
                && state[0].npcs.size() == 1 && state[0].npcs.front().disposition == npc->mNpdt.mDisposition + 45
                && state[1].dialogue.quests.front().mState == 5 && state[1].dialogue.quests.front().mFinished == 0,
                "A did not finish TR_M1_GS_MQ_4 with +45 disposition or mutated B");

            // 3. Waiting/reserved restart: foreign killer 99 kills life 1 while B is in progress -> B waits and reserves life 2.
            const std::array<PersonalScripts::LifeEvent, 1> foreignKill{{{1, 2, 99, 1, 1, 99, 1, false}}};
            MWMechanics::NpcStats deadStats(loadout.store());
            deadStats.setHealth(MWMechanics::DynamicStat<float>(50, 50, 0), MWWorld::TimeStamp{});
            PersonalScripts::Source foreignDeadSource{401, 1, 15, npc->mId, false, 99, 1, foreignKill, &deadStats, pose,
                &privateStats, false, false, 0, 0, false, 0, emptyInvs};
            const std::array<uint64_t, 1> onlyBob{22};
            auto bobWait = policy.prepareScheduled(state, 2, onlyBob, loadout.store(), noFallback, &foreignDeadSource);
            require(bobWait.result.executed && bobWait.state[1].sourceWait && bobWait.state[1].reservedLife == 2
                && policy.encode(policy.restore(bobWait.image)) == bobWait.image,
                ("Foreign kill on TR_m1_T_Eldale did not defer B or reserve life 2: " + bobWait.result.diagnostic).c_str());
            state = policy.restore(bobWait.image);

            // Life 2 respawns -> B wakes, reopens puzzling riddles on life 2, and completes TR_M1_GS_MQ_4.
            PersonalScripts::Source respawnedSource{401, 2, 0, npc->mId, true, 0, 0, foreignKill, &publicStats, pose,
                &privateStats, false, false, 0, 0, false, 0, emptyInvs};
            auto bobWake = policy.prepareScheduled(state, 15, onlyBob, loadout.store(), noFallback, &respawnedSource);
            require(bobWake.result.executed && !bobWake.state[1].sourceWait, "B failed to wake on respawned life 2");
            state.swap(bobWake.state);
            for (const auto [choice, expectedIndex] : riddleSteps)
            {
                auto ask = talk(state, 22, -1, 2);
                require(ask.result.executed && !ask.state[1].choices.empty(),
                    ("B failed to open puzzling riddles stage on life 2: " + ask.result.diagnostic).c_str());
                state.swap(ask.state);
                auto step = talk(state, 22, choice, 2);
                require(step.result.executed && step.state[1].dialogue.quests.front().mState == expectedIndex,
                    ("B failed to complete TR_M1_GS_MQ_4 after waiting/reserved restart: " + step.result.diagnostic).c_str());
                state.swap(step.state);
            }
            require(state[1].dialogue.quests.front().mFinished == 1 && state[1].choices.empty(),
                "B did not finish TR_M1_GS_MQ_4 after restart");

            // 4. Fair-opportunity protection + attributed OnDeath on TR_m1_T_EldaleSc:
            // A (11) cannot steal B (22)'s reserved life 2; B (22) claims OnDeath on life 2, setting TR_m1_HT_EldaleDead=1
            // and running/stopping TR_m0_TelvanniHortDeathSc for B only while clearing B's reservation.
            const std::array<PersonalScripts::LifeEvent, 2> stolenKill{{
                {1, 2, 99, 1, 1, 99, 1, false},
                {2, 20, 11, 1, 1, 11, 1, false}}};
            PersonalScripts::Source stolenDeadSource{401, 2, 30, npc->mId, false, 11, 1, stolenKill, &deadStats, pose,
                &privateStats, false, false, 0, 0, false, 0, emptyInvs};
            const std::array<uint64_t, 1> onlyAlice{11};
            auto rejectedSteal = policy.prepareScheduled(state, 20, onlyAlice, loadout.store(), noFallback, &stolenDeadSource);
            require(rejectedSteal.result.outcome == SharedWorldScript::Outcome::Rejected && rejectedSteal.state.empty(),
                "Unreserved character A was allowed to claim B's reserved life 2");

            const std::array<PersonalScripts::LifeEvent, 2> ownKill{{
                {1, 2, 99, 1, 1, 99, 1, false},
                {2, 20, 22, 1, 1, 22, 1, false}}};
            PersonalScripts::Source ownDeadSource{401, 2, 30, npc->mId, false, 22, 1, ownKill, &deadStats, pose,
                &privateStats, false, false, 0, 0, false, 0, emptyInvs};
            auto bobDeath = policy.prepareScheduled(state, 20, onlyBob, loadout.store(), noFallback, &ownDeadSource);
            require(bobDeath.result.executed && bobDeath.state[1].deathCursor == 2
                && bobDeath.state[1].reservedLife == 0
                && bobDeath.state[1].runtime.runtime.globals.front().mValue.getInteger() == 1
                && !bobDeath.state[1].members.front().runtime.script.mRunning
                && bobDeath.state[0].runtime.runtime.globals.front().mValue.getInteger() == 0,
                ("TR_m1_T_EldaleSc OnDeath failed: exec=" + std::to_string(bobDeath.result.executed)
                    + " diag=" + bobDeath.result.diagnostic).c_str());
            state.swap(bobDeath.state);

            // 5. Unstaged OnPCHitMe==1 combat/cast branch rolls back atomically without mutating committed state.
            auto hitBranch = state;
            for (auto& [varName, varVal] : hitBranch[0].runtime.runtime.script.mLocals.mVariables)
                if (varName == "onpchitme") varVal.setInteger(1);
            const auto committedBytes = policy.encode(state);
            auto rejectedHit = policy.prepareScheduled(hitBranch, 21, onlyAlice, loadout.store(), noFallback, &respawnedSource);
            require(rejectedHit.result.outcome == SharedWorldScript::Outcome::Rejected && rejectedHit.state.empty()
                && policy.encode(state) == committedBytes,
                "Unstaged OnPCHitMe combat branch in TR_m1_T_EldaleSc did not roll back atomically");
            std::cout << "TR quest content: unchanged TR_m1_T_EldaleSc + TR_m0_TelvanniHortDeathSc + TR_M1_GS_MQ_4 "
                "(Duel of Riddles) admitted and verified for A/B with waiting/reserved restart and OnDeath/rollback\n";
        }
        else
        {
            // 1. MS_FargothRing: unchanged fargoth (LocalState), arrille, MS_FargothRing, and "ring".
            const auto* fargoth = loadout.store().get<ESM::NPC>().find(id("fargoth"));
            const auto* arrille = loadout.store().get<ESM::NPC>().find(id("arrille"));
            const auto* fargothScript = loadout.store().get<ESM::Script>().find(fargoth->mScript);
            const std::vector<ESM::Script> fargothBank{*fargothScript};
            auto fargothProg = std::make_shared<const SharedWorldScript>(
                *fargothScript, std::vector<ESM::Global>{}, content, crypto, fargothBank, fargoth->mId, true);
            const auto* ringQuest = loadout.store().get<ESM::Dialogue>().find(id("MS_FargothRing"));
            const auto* ringTopicFull = loadout.store().get<ESM::Dialogue>().find(id("ring"));
            require(ringQuest->mType == ESM::Dialogue::Journal && ringTopicFull->mType == ESM::Dialogue::Topic,
                "Unchanged Seyda Neen ring records missing");
            ESM::Dialogue ringTopic = *ringTopicFull;
            std::erase_if(ringTopic.mInfo, [&](const auto& info) { return info.mActor != fargoth->mId; });
            require(!ringTopic.mInfo.empty(), "Fargoth ring topic entries missing");
            for (const auto& info : ringTopic.mInfo)
                if (!info.mResultScript.empty())
                    require(!fargothProg->compileResult(info.mResultScript, true, fargoth->mId).mInstructions.empty(),
                        "Seyda Neen ring result script failed admission");

            const PersonalScripts::EncounterBinding fargothBind{101, fargoth->mId, ESM::RefId::esm3ExteriorCell(-2, -9)};
            std::vector<PersonalScripts::Definition> ringDefs;
            for (const uint64_t ch : both)
                ringDefs.push_back({ch, 501, fargothProg, {}, true, {*ringQuest, ringTopic}, fargothBind});
            PersonalScripts ringPolicy(95, ringDefs, crypto, true, true, true, true, true, true, true, true, true, true, true);
            auto ringState = ringPolicy.initial();
            MWWorld::ManualRef fargothRef(loadout.store(), fargoth->mId);
            MWWorld::ManualRef playerRef(loadout.store(), fargoth->mId);
            IntentResolver ringResolver;
            ringResolver.actor = fargothRef.getPtr();
            ringResolver.player = playerRef.getPtr();
            MWMechanics::NpcStats playerStats(loadout.store()), fargothStats(loadout.store());
            fargothStats.setBaseDisposition(fargoth->mNpdt.mDisposition);
            const MWScript::DialogueState::Stamp stamp{0, 7, 16};
            const std::array<PersonalScripts::ItemBalance, 1> hasRing{{{id("ring_keley"), 1}}};
            const std::array<PersonalScripts::ItemBalance, 0> noRing{};

            auto aRingGreeting = ringPolicy.prepareIntent(ringState, 11, 1,
                {501, 101, 1, 0, "ring", -1}, "Seyda Neen", loadout.store(), ringResolver, 4,
                &playerStats, {}, &fargothStats, stamp, std::span<const PersonalScripts::ItemBalance>(hasRing));
            require(aRingGreeting.result.executed && aRingGreeting.state[0].choices.size() == 2,
                ("Fargoth ring greeting did not offer yes/no choices when holding ring_keley: " + aRingGreeting.result.diagnostic).c_str());
            ringState.swap(aRingGreeting.state);

            // Missing-inventory check: attempting choice 2 ("Yes") without ring_keley matches Morrowind.esm's
            // Item ring_keley == 0 refusal branch without completing MS_FargothRing or transferring items, and
            // an invalid choice (9) rejects atomically without mutating state.
            const auto ringBeforeGive = ringPolicy.encode(ringState);
            auto rejectedChoice = ringPolicy.prepareIntent(ringState, 11, 2,
                {501, 101, 1, ringState[0].dialogueRevision, "ring", 9}, "Seyda Neen", loadout.store(), ringResolver, 4,
                &playerStats, {}, &fargothStats, stamp, std::span<const PersonalScripts::ItemBalance>(noRing));
            require(rejectedChoice.result.outcome == SharedWorldScript::Outcome::Rejected && rejectedChoice.state.empty()
                && ringPolicy.encode(ringState) == ringBeforeGive,
                "Invalid Fargoth ring choice did not roll back atomically");
            auto missingRingAttempt = ringPolicy.prepareIntent(ringState, 11, 2,
                {501, 101, 1, ringState[0].dialogueRevision, "ring", 2}, "Seyda Neen", loadout.store(), ringResolver, 4,
                &playerStats, {}, &fargothStats, stamp, std::span<const PersonalScripts::ItemBalance>(noRing));
            require(missingRingAttempt.result.executed
                && missingRingAttempt.state[0].dialogue.quests.size() == 1
                && missingRingAttempt.state[0].dialogue.quests.front().mState == 10
                && missingRingAttempt.state[0].dialogue.quests.front().mFinished == 0
                && missingRingAttempt.state[0].itemReceipts.empty(),
                "Fargoth ring choice 2 without ring_keley did not take Morrowind.esm's no-ring branch (index 10, no transfer)");

            auto aRingFinish = ringPolicy.prepareIntent(ringState, 11, 3,
                {501, 101, 1, ringState[0].dialogueRevision, "ring", 2}, "Seyda Neen", loadout.store(), ringResolver, 4,
                &playerStats, {}, &fargothStats, stamp, std::span<const PersonalScripts::ItemBalance>(hasRing));
            require(aRingFinish.result.executed
                && aRingFinish.state[0].dialogue.quests.size() == 1
                && aRingFinish.state[0].dialogue.quests.front().mState == 100
                && aRingFinish.state[0].dialogue.quests.front().mFinished == 1
                && aRingFinish.state[0].playerItems.empty()
                && aRingFinish.state[0].itemReceipts.size() == 2
                && aRingFinish.state[0].npcs.size() == 2
                && aRingFinish.state[1].dialogue.quests.empty(),
                ("Fargoth ring completion did not finish MS_FargothRing 100, transfer ring_keley, and update fargoth/arrille dispositions: " + aRingFinish.result.diagnostic).c_str());
            const auto arrilleDisp = std::ranges::find(aRingFinish.state[0].npcs, arrille->mId, &PersonalScripts::Instance::Npc::record);
            require(arrilleDisp != aRingFinish.state[0].npcs.end()
                && arrilleDisp->disposition == arrille->mNpdt.mDisposition + 40,
                "Fargoth ring completion did not stage +40 foreign disposition on arrille");
            ringState = ringPolicy.restore(aRingFinish.image);

            // B completes MS_FargothRing independently after restart.
            auto bRingGreeting = ringPolicy.prepareIntent(ringState, 22, 4,
                {501, 101, 1, 0, "ring", -1}, "Seyda Neen", loadout.store(), ringResolver, 4,
                &playerStats, {}, &fargothStats, stamp, std::span<const PersonalScripts::ItemBalance>(hasRing));
            auto bRingFinish = ringPolicy.prepareIntent(bRingGreeting.state, 22, 5,
                {501, 101, 1, bRingGreeting.state[1].dialogueRevision, "ring", 2}, "Seyda Neen", loadout.store(),
                ringResolver, 4, &playerStats, {}, &fargothStats, stamp,
                std::span<const PersonalScripts::ItemBalance>(hasRing));
            require(bRingFinish.result.executed && bRingFinish.state[1].dialogue.quests.front().mState == 100
                && bRingFinish.state[1].dialogue.quests.front().mFinished == 1,
                ("B failed to complete MS_FargothRing independently after restart: " + bRingFinish.result.diagnostic).c_str());

            // 2. MS_Nuccius: unchanged vodunius nuccius (voduniusScript) departure on CellChanged when MS_Nuccius >= 100.
            const auto* murderTopicFull = loadout.store().get<ESM::Dialogue>().find(id("murder of Processus Vitellius"));
            require(murderTopicFull->mType == ESM::Dialogue::Topic, "Unchanged murder of Processus Vitellius topic missing");
            ESM::Dialogue murderTopic = *murderTopicFull;
            murderTopic.mInfo.clear();

            const auto* departure = loadout.store().get<ESM::NPC>().find(id("vodunius nuccius"));
            const auto* departureScript = loadout.store().get<ESM::Script>().find(departure->mScript);
            const std::vector<ESM::Script> departureBank{*departureScript};
            auto nucciusProg = std::make_shared<const SharedWorldScript>(
                *departureScript, std::vector<ESM::Global>{}, content, crypto, departureBank, departure->mId, true);
            const auto* nucciusQuest = loadout.store().get<ESM::Dialogue>().find(id("MS_Nuccius"));
            const PersonalScripts::EncounterBinding nucciusBind{102, departure->mId, ESM::RefId::esm3ExteriorCell(-2, -9)};
            std::vector<PersonalScripts::Definition> nucciusDefs;
            for (const uint64_t ch : both)
                nucciusDefs.push_back({ch, 502, nucciusProg, {}, true, {*nucciusQuest, murderTopic}, nucciusBind});
            PersonalScripts nucciusPolicy(95, nucciusDefs, crypto, true, true, true, true, true, true, true, true, true, true, true);
            auto nucciusState = nucciusPolicy.initial();
            nucciusState[0].dialogue.quests.push_back({id("MS_Nuccius"), 100, 1});
            const std::array<PersonalScripts::PlayerInventory, 2> emptyInvs{{{11, {}}, {22, {}}}};
            std::vector<PersonalScripts::LifeEvent> events;
            PersonalScripts::Source nucciusSource{102, 1, 0, departure->mId, true, 0, 0, events, &publicStats, pose,
                &privateStats, true, false, 0, 11, false, 0, emptyInvs};
            auto nucciusTick = nucciusPolicy.prepareScheduled(nucciusState, 1, both, loadout.store(), noFallback, &nucciusSource);
            require(nucciusTick.result.executed && nucciusTick.state[0].encounterLife == 1
                && PersonalScripts::physicalScope(nucciusTick.state, 11) == 2
                && nucciusTick.state[1].encounterLife == 0
                && PersonalScripts::physicalScope(nucciusTick.state, 22) == 1
                && nucciusPolicy.encode(nucciusPolicy.restore(nucciusTick.image)) == nucciusTick.image,
                "Unchanged voduniusScript did not Disable vodunius nuccius for A only on CellChanged");

            // 3. MS_Lookout: unchanged flora_treestump_unique (treestumpScript) activation advances MS_Lookout to 40.
            const auto* stump = loadout.store().get<ESM::Container>().find(id("flora_treestump_unique"));
            const auto* stumpScript = loadout.store().get<ESM::Script>().find(stump->mScript);
            const std::vector<ESM::Script> stumpBank{*stumpScript};
            const std::vector<ESM::Global> stumpGlobals{*loadout.store().get<ESM::Global>().find(id("fargothwalk"))};
            auto stumpProg = std::make_shared<const SharedWorldScript>(
                *stumpScript, stumpGlobals, content, crypto, stumpBank, stump->mId, true);
            const auto* lookoutQuest = loadout.store().get<ESM::Dialogue>().find(id("MS_Lookout"));
            const PersonalScripts::EncounterBinding stumpBind{103, stump->mId, ESM::RefId::esm3ExteriorCell(-2, -9)};
            std::vector<PersonalScripts::Definition> stumpDefs;
            for (const uint64_t ch : both)
                stumpDefs.push_back({ch, 503, stumpProg, {}, true, {*lookoutQuest, murderTopic}, stumpBind});
            PersonalScripts stumpPolicy(95, stumpDefs, crypto, true, true, true, true, true, true, true, true, true, true, true);
            auto stumpState = stumpPolicy.initial();
            stumpState[0].runtime.runtime.globals.front().mValue.setInteger(2);
            stumpState[1].runtime.runtime.globals.front().mValue.setInteger(2);
            stumpState[0].dialogue.quests.push_back({id("MS_Lookout"), 20, 0});
            stumpState[1].dialogue.quests.push_back({id("MS_Lookout"), 20, 0});
            PersonalScripts::Source stumpSource{103, 1, 0, stump->mId, true, 0, 0, events, &publicStats, pose,
                &privateStats, false, false, 0, 0, true, 11, emptyInvs};
            auto stumpTickA = stumpPolicy.prepareScheduled(stumpState, 1, both, loadout.store(), noFallback, &stumpSource);
            require(stumpTickA.result.executed
                && stumpTickA.state[0].dialogue.quests.front().mState == 40
                && !stumpTickA.state[0].dialogue.entries.empty()
                && stumpTickA.state[1].dialogue.quests.front().mState == 20
                && stumpTickA.state[1].dialogue.entries.empty()
                && stumpPolicy.encode(stumpPolicy.restore(stumpTickA.image)) == stumpTickA.image,
                ("Unchanged treestumpScript did not advance MS_Lookout to 40 for A only on activation: " + stumpTickA.result.diagnostic).c_str());

            // 4. MV_DeadTaxman: unchanged processus vitellius (processusScript) activation writes MV_DeadTaxman 10 and AddTopic.
            const auto* processus = loadout.store().get<ESM::NPC>().find(id("processus vitellius"));
            const auto* processusScript = loadout.store().get<ESM::Script>().find(processus->mScript);
            const std::vector<ESM::Script> processusBank{*processusScript};
            auto processusProg = std::make_shared<const SharedWorldScript>(
                *processusScript, std::vector<ESM::Global>{}, content, crypto, processusBank, processus->mId, true);
            const auto* taxmanQuest = loadout.store().get<ESM::Dialogue>().find(id("MV_DeadTaxman"));
            require(taxmanQuest->mType == ESM::Dialogue::Journal, "Unchanged MV_DeadTaxman journal missing");
            ESM::Dialogue rootTopic = *ringTopicFull;
            rootTopic.mInfo.clear();
            const PersonalScripts::EncounterBinding processusBind{104, processus->mId, ESM::RefId::esm3ExteriorCell(-2, -9)};
            std::vector<PersonalScripts::Definition> taxmanDefs;
            for (const uint64_t ch : both)
                taxmanDefs.push_back({ch, 504, processusProg, {}, true, {*taxmanQuest, rootTopic, murderTopic}, processusBind});
            PersonalScripts taxmanPolicy(95, taxmanDefs, crypto, true, true, true, true, true, true, true, true, true, true, true);
            auto taxmanState = taxmanPolicy.initial();
            PersonalScripts::Source processusSource{104, 1, 0, processus->mId, true, 0, 0, events, &publicStats, pose,
                &privateStats, false, false, 0, 0, true, 11, emptyInvs};
            auto taxmanTickA = taxmanPolicy.prepareScheduled(taxmanState, 1, both, loadout.store(), noFallback, &processusSource);
            require(taxmanTickA.result.executed
                && taxmanTickA.state[0].dialogue.quests.size() == 1
                && taxmanTickA.state[0].dialogue.quests.front().mState == 10
                && std::ranges::find(taxmanTickA.state[0].dialogue.topics, id("murder of Processus Vitellius"))
                    != taxmanTickA.state[0].dialogue.topics.end()
                && taxmanTickA.state[1].dialogue.quests.empty()
                && std::ranges::find(taxmanTickA.state[1].dialogue.topics, id("murder of Processus Vitellius"))
                    == taxmanTickA.state[1].dialogue.topics.end()
                && taxmanPolicy.encode(taxmanPolicy.restore(taxmanTickA.image)) == taxmanTickA.image,
                "Unchanged processusScript did not write MV_DeadTaxman 10 and AddTopic for A only");
            taxmanState = taxmanPolicy.restore(taxmanTickA.image);
            processusSource.activatedCharacter = 22;
            auto taxmanTickB = taxmanPolicy.prepareScheduled(taxmanState, 2, both, loadout.store(), noFallback, &processusSource);
            require(taxmanTickB.result.executed
                && taxmanTickB.state[1].dialogue.quests.size() == 1
                && taxmanTickB.state[1].dialogue.quests.front().mState == 10
                && std::ranges::find(taxmanTickB.state[1].dialogue.topics, id("murder of Processus Vitellius"))
                    != taxmanTickB.state[1].dialogue.topics.end(),
                "B failed to activate processus vitellius independently after restart");
            std::cout << "Seyda Neen quest content: unchanged MS_FargothRing, MS_Nuccius (voduniusScript), "
                "MS_Lookout (treestumpScript), and MV_DeadTaxman (processusScript) admitted and verified for A/B\n";
        }
        std::cout << "Quest content verification passed; unchanged content records executed with A/B isolation and recovery\n";
    }

    void questAudit(CredentialCrypto& crypto)
    {
        const auto id = [](std::string_view name) { return ESM::RefId::stringRefId(name); };
        ESM::Global eldaleDead; eldaleDead.blank(); eldaleDead.mId = id("TR_m1_HT_EldaleDead");
        eldaleDead.mValue.setType(ESM::VT_Short); eldaleDead.mValue.setInteger(0);
        ESM::Script fargoth; fargoth.blank(); fargoth.mId = id("fargothScript");
        fargoth.mScriptText = "begin fargothScript\nshort ring\nend fargothScript\n";
        ESM::Script hortDeath; hortDeath.blank(); hortDeath.mId = id("TR_m0_TelvanniHortDeathSc");
        hortDeath.mScriptText = "begin TR_m0_TelvanniHortDeathSc\nshort stop\nset stop to 1\nStopScript TR_m0_TelvanniHortDeathSc\nend\n";
        ESM::Script eldale; eldale.blank(); eldale.mId = id("TR_m1_T_EldaleSc");
        eldale.mScriptText = "begin TR_m1_T_EldaleSc\nshort doOnce\nfloat timer\nfloat health\nfloat ratio\nfloat px\nfloat yaw\n"
            "if ( MenuMode == 1 )\nreturn\nendif\n"
            "if ( GetPCSleep == 1 )\nreturn\nendif\n"
            "if ( CellChanged == 1 )\nset doOnce to 1\nendif\n"
            "if ( OnActivate == 1 )\nActivate\nendif\n"
            "if ( OnDeath == 1 )\nset TR_m1_HT_EldaleDead to 1\nStartScript TR_m0_TelvanniHortDeathSc\nendif\n"
            "if ( GetJournalIndex \"TR_M1_GS_MQ_4\" >= 10 )\nset doOnce to 2\nendif\n"
            "set timer to timer + GetSecondsPassed\nset health to GetHealth\nset ratio to GetHealthGetRatio\n"
            "set px to GetPos x\nset yaw to GetAngle z\n"
            "if ( GetDisabled == 1 )\nset doOnce to 3\nendif\nend TR_m1_T_EldaleSc\n";
        const std::vector<ESM::Script> bank{eldale, hortDeath, fargoth};
        SharedWorldScript admittedBank(eldale, {eldaleDead}, "quest-audit-v95", crypto, bank, id("TR_m1_T_Eldale"), true);
        ESM::Script nuccius; nuccius.blank(); nuccius.mId = id("nucciusScript");
        nuccius.mScriptText = "begin nucciusScript\nshort state\n"
            "if ( CellChanged == 0 )\nreturn\nendif\n"
            "if ( GetJournalIndex \"MS_Nuccius\" >= 100 )\nDisable\nset state to 1\nendif\nend nucciusScript\n";
        SharedWorldScript admittedDeparture(nuccius, {}, "quest-audit-v95", crypto, std::vector<ESM::Script>{nuccius},
            id("vodunius nuccius"));
        (void)admittedBank.compileResult(
            "if ( GetJournalIndex \"TR_M1_GS_MQ_4\" >= 10 )\nSetJournalIndex \"TR_M1_GS_MQ_4\" 20\nendif\n"
            "Journal TR_M1_GS_MQ_4 10\nAddTopic \"puzzling riddles\"\nChoice \"Answer one\" 1 \"Answer two\" 2\n"
            "ModDisposition 10\n\"arrille\"->ModDisposition 40\n"
            "TR_m1_T_Eldale->PositionCell 200 -32 1 0 \"Eldale\"\nTR_m1_T_Eldale->AiTravel 200 -32 1\n"
            "TR_m1_T_Eldale->AiEscort player 0 200 -32 1\nTR_m1_T_Eldale->Disable\n",
            true, id("TR_m1_T_Eldale"), false, true, true);
        (void)admittedBank.compileResult("Enable", true, id("TR_m1_T_Eldale"), true);

        const auto makeBankScript = [&](std::string_view body) {
            ESM::Script candidate = eldale;
            candidate.mScriptText = "begin TR_m1_T_EldaleSc\nshort state\nfloat timer\n" + std::string(body) + "\nend\n";
            return candidate;
        };
        for (const auto* query : {
            "if ( player->OnActivate == 1 )\nplayer->Activate\nendif",
            "player->Disable",
            "player->Equip \"ring_keley\""})
            rejects([&] {
                auto candidate = makeBankScript(query);
                SharedWorldScript blocked(candidate, {eldaleDead}, "quest-audit-v95", crypto,
                    std::vector<ESM::Script>{candidate, hortDeath}, id("TR_m1_T_Eldale"), true);
            }, "Unadmitted foreign activation/disable or Equip opcode compiled in source bank");
        for (const auto* query : {"Journal \"TR_M1_GS_MQ_4\" 10", "SetJournalIndex \"TR_M1_GS_MQ_4\" 10",
                 "AddTopic \"puzzling riddles\""})
            rejects([&] {
                auto candidate = makeBankScript(query);
                SharedWorldScript blocked(candidate, {eldaleDead}, "quest-audit-v95", crypto,
                    std::vector<ESM::Script>{candidate, hortDeath});
            }, "Source-only journal/topic write compiled in source-free bank");
        auto eldaleWithItems = makeBankScript(
            "if ( player->GetItemCount \"ring_keley\" >= 1 )\n"
            "set state to GetItemCount \"gold_001\"\n"
            "AddItem \"gold_001\" 10\n"
            "RemoveItem \"gold_001\" 5\n"
            "player->AddItem \"gold_001\" 25\n"
            "player->RemoveItem \"ring_keley\" 1\n"
            "Journal \"TR_M1_GS_MQ_4\" 10\n"
            "SetJournalIndex \"TR_M1_GS_MQ_4\" 20\n"
            "AddTopic \"puzzling riddles\"\n"
            "AddSpell \"TR_m1_Telvanni_Levitate\"\n"
            "RemoveSpell \"TR_m1_Telvanni_Levitate\"\n"
            "if ( GetSpell \"TR_m1_Telvanni_Levitate\" == 1 )\n"
            "TR_m1_Ultern->StartCombat player\n"
            "Cast \"TR_m1_Eldale_Shield\" TR_m1_T_Eldale\n"
            "endif\n"
            "endif");
        SharedWorldScript admittedItems(eldaleWithItems, {eldaleDead}, "quest-audit-items", crypto,
            std::vector<ESM::Script>{eldaleWithItems, hortDeath}, id("TR_m1_T_Eldale"), true);
        require(admittedItems.encode(admittedItems.restore(admittedItems.encode(admittedItems.initial())))
                == admittedItems.encode(admittedItems.initial()),
            "Batch-3/4 source-bank opcodes failed admission or round-trip");
        for (const auto* resultOp : {
            "Journal MS_FargothRing 100\n\"arrille\"->ModDisposition 40\nModDisposition 50\nplayer->RemoveItem \"ring_keley\" 1",
            "Journal MS_Nuccius 20\nplayer->AddItem \"ring_nuccius\" 1\nplayer->RemoveItem \"gold_001\" 100",
            "if ( player->GetItemCount \"tax record\" >= 1 )\nplayer->RemoveItem \"tax record\" 1\n"
            "player->RemoveItem \"extravagant_ring_processus\" 1\nplayer->AddItem \"gold_001\" 500\nendif\n"
            "if ( GetItemCount \"gold_001\" >= 50 )\nRemoveItem \"gold_001\" 50\nplayer->AddItem \"gold_001\" 50\nendif",
            "Journal \"MV_DeadTaxman\" 85\nSetFight 100\nAddTopic \"Processus' Ring\"\nGoodbye"})
            require(!admittedBank.compileResult(resultOp, true, id("TR_m1_T_Eldale"), false, true, true).mInstructions.empty(),
                "Batch-3/4 opcode failed admission in dialogue result");
        rejects([&] { (void)admittedBank.compileResult("player->Equip \"ring_keley\"", true, id("TR_m1_T_Eldale"), false, true, true); },
            "Unadmitted Container Equip opcode compiled in dialogue result");
        std::cout << "quest audit: V95+Batch-1/2/3/4 admits source-bank GetJournalIndex/Journal/SetJournalIndex/AddTopic/"
            "CellChanged/MenuMode/GetPCSleep/OnActivate/Activate/Disable/OnDeath/StartScript/StopScript/GetSecondsPassed/"
            "GetDisabled/GetPos/GetAngle/GetHealth/GetItemCount/AddItem/RemoveItem/GetSpell/AddSpell/RemoveSpell/"
            "StartCombat/StopCombat/SetFight/Cast and result Journal/SetJournalIndex/GetJournalIndex/AddTopic/Choice/"
            "ModDisposition/foreign-ModDisposition/Disable/Enable/Position/PositionCell/AiTravel/AiEscort/GetItemCount/"
            "AddItem/RemoveItem/GetSpell/AddSpell/RemoveSpell/SetFight\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2 || argc == 3, "Usage: tes3mp_native_world_script_tests FILTER [CONTENT_CONFIG]");
        MWClass::registerClasses();
        auto crypto = TES3MP::makeProductionCredentialCrypto(); require(bool(crypto), "Crypto unavailable");
        const std::string_view filter = argv[1];
        if (filter == "content" && argc == 3) content(*crypto, argv[2]);
        else if (filter == "quest-content" && argc == 3) questContent(*crypto, argv[2], false);
        else if (filter == "tr-quest-content" && argc == 3) questContent(*crypto, argv[2], true);
        else if (filter == "quest-audit" && argc == 2) questAudit(*crypto);
        else if (filter == "shared" && argc == 2) shared(*crypto);
        else if (filter == "rollback") rollback(*crypto);
        else if (filter == "recovery") recovery(*crypto);
        else if (filter == "admission") admission(*crypto);
        else if (filter == "personal") personal(*crypto);
        else if (filter == "personal-scheduling") personalScheduling(*crypto);
        else if (filter == "script-banks") scriptBanks(*crypto);
        else throw std::invalid_argument("Unknown filter");
        std::cout << "PASS shared-world-script " << filter << '\n'; return 0;
    }
    catch (const std::exception& error)
    { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
