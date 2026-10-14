#include "world_script.hpp"
#include "personal_script.hpp"
#include "loadout.hpp"

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

    // These are admission preflights, not completed quests. Read winning,
    // unchanged content and assert the first unavailable transaction service;
    // never rewrite a quest or silently skip its owning NPC script.
    void questContent(CredentialCrypto& crypto, const char* config, bool tr)
    {
        const char* args[]{"quest-content-test", "--config", config};
        Loadout loadout(readLoadoutOptions(3, args));
        const auto content = loadout.contentFingerprint();
        const auto id = [](std::string_view name) { return ESM::RefId::stringRefId(name); };
        const auto blocked = [](auto&& operation, std::string_view expected, std::string_view context)
        {
            std::string diagnostic;
            try { operation(); }
            catch (const std::invalid_argument& error) { diagnostic = error.what(); }
            std::cout << "BLOCKED " << context << " reason="
                << std::string_view(diagnostic).substr(0, SharedWorldScript::MaximumDiagnosticBytes) << '\n';
            require(!diagnostic.empty() && diagnostic.find(expected) != std::string::npos,
                "Unchanged quest admission boundary changed; inspect its owning source");
        };
        if (tr)
        {
            const auto* npc = loadout.store().get<ESM::NPC>().find(id("TR_m1_T_Eldale"));
            require(npc->mScript == id("TR_m1_T_EldaleSc"), "TR riddles owning NPC script changed");
            const auto* record = loadout.store().get<ESM::Script>().find(npc->mScript);
            const std::vector<ESM::Script> bank{*record,
                *loadout.store().get<ESM::Script>().find(id("TR_m0_TelvanniHortDeathSc")),
                *loadout.store().get<ESM::Script>().find(id("TR_m1_HT_UnseatingDralSc"))};
            const std::vector<ESM::Global> owned{*loadout.store().get<ESM::Global>().find(id("TR_m1_HT_EldaleDead"))};
            const auto* quest = loadout.store().get<ESM::Dialogue>().find(id("TR_M1_GS_MQ_4"));
            const auto* topic = loadout.store().get<ESM::Dialogue>().find(id("puzzling riddles"));
            require(quest->mType == ESM::Dialogue::Journal && topic->mType == ESM::Dialogue::Topic
                && std::ranges::any_of(quest->mInfo, [](const auto& info) {
                    return info.mData.mJournalIndex == 50 && info.mQuestStatus == ESM::DialInfo::QS_Finished;
                }) && std::ranges::any_of(topic->mInfo, [&](const auto& info) {
                    return info.mActor == npc->mId && info.mResultScript.find("TR_M1_GS_MQ_4 50") != std::string::npos;
                }), "Unchanged TR riddles completion branch missing");
            blocked([&] { SharedWorldScript runtime(*record, owned, content, crypto, bank, npc->mId, true); },
                "line 18", "TR Duel of Riddles source-bank (GetJournalIndex)");
        }
        else
        {
            const auto* npc = loadout.store().get<ESM::NPC>().find(id("fargoth"));
            const auto* record = loadout.store().get<ESM::Script>().find(npc->mScript);
            const std::vector<ESM::Script> bank{*record};
            SharedWorldScript runtime(*record, {}, content, crypto, bank);
            const auto* quest = loadout.store().get<ESM::Dialogue>().find(id("MS_FargothRing"));
            const auto* topic = loadout.store().get<ESM::Dialogue>().find(id("ring"));
            require(quest->mType == ESM::Dialogue::Journal && topic->mType == ESM::Dialogue::Topic,
                "Unchanged Seyda Neen ring records missing");
            const auto finish = std::ranges::find_if(topic->mInfo, [&](const auto& info) {
                return info.mActor == npc->mId && info.mResultScript.find("MS_FargothRing 100") != std::string::npos;
            });
            require(finish != topic->mInfo.end(), "Unchanged ring completion branch missing");
            const auto unchanged = runtime.encode(runtime.initial());
            blocked([&] { (void)runtime.compileResult(finish->mResultScript, true, npc->mId); },
                "line 4", "Seyda Neen ring completion (AddItem/RemoveItem)");
            require(runtime.encode(runtime.initial()) == unchanged, "Rejected stock quest changed script state");
            const auto* departure = loadout.store().get<ESM::NPC>().find(id("vodunius nuccius"));
            const auto* departureScript = loadout.store().get<ESM::Script>().find(departure->mScript);
            const std::vector<ESM::Script> departureBank{*departureScript};
            blocked([&] { SharedWorldScript source(*departureScript, {}, content, crypto, departureBank,
                departure->mId); }, "line 2", "Seyda Neen departure source-bank (CellChanged)");
        }
        std::cout << "Quest completion remains open; no journal/reward/source life was executed or copied\n";
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
            "if ( OnDeath == 1 )\nset TR_m1_HT_EldaleDead to 1\nStartScript TR_m0_TelvanniHortDeathSc\nendif\n"
            "set timer to timer + GetSecondsPassed\nset health to GetHealth\nset ratio to GetHealthGetRatio\n"
            "set px to GetPos x\nset yaw to GetAngle z\n"
            "if ( GetDisabled == 1 )\nset doOnce to 1\nendif\nend TR_m1_T_EldaleSc\n";
        const std::vector<ESM::Script> bank{eldale, hortDeath, fargoth};
        SharedWorldScript admittedBank(eldale, {eldaleDead}, "quest-audit-v95", crypto, bank, id("TR_m1_T_Eldale"), true);
        (void)admittedBank.compileResult(
            "if ( GetJournalIndex \"TR_M1_GS_MQ_4\" >= 10 )\nSetJournalIndex \"TR_M1_GS_MQ_4\" 20\nendif\n"
            "Journal TR_M1_GS_MQ_4 10\nAddTopic \"puzzling riddles\"\nChoice \"Answer one\" 1 \"Answer two\" 2\nModDisposition 10\n"
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
            "if ( GetJournalIndex \"TR_M1_GS_MQ_4\" >= 10 )\nset state to 1\nendif",
            "if ( CellChanged == 0 )\nreturn\nendif",
            "if ( MenuMode == 1 )\nreturn\nendif",
            "if ( GetPCSleep == 1 )\nreturn\nendif",
            "if ( OnActivate == 1 )\nActivate\nendif"})
            rejects([&] {
                auto candidate = makeBankScript(query);
                SharedWorldScript blocked(candidate, {eldaleDead}, "quest-audit-v95", crypto,
                    std::vector<ESM::Script>{candidate, hortDeath}, id("TR_m1_T_Eldale"), true);
            }, "Unadmitted Batch-1/2 state, cell, menu, or activation opcode compiled in source bank");
        for (const auto* resultOp : {
            "\"arrille\"->ModDisposition 40",
            "if ( player->GetItemCount \"ring_keley\" >= 1 )\nset doOnce to 1\nendif",
            "player->AddItem \"gold_001\" 100",
            "player->RemoveItem \"ring_keley\" 1"})
            rejects([&] { (void)admittedBank.compileResult(resultOp, true, id("TR_m1_T_Eldale"), false, true, true); },
                "Unadmitted Batch-2/3 foreign-NPC or provenance item opcode compiled in dialogue result");
        std::cout << "quest audit: V95 admits source-bank OnDeath/StartScript/StopScript/GetSecondsPassed/GetDisabled/GetPos/GetAngle/GetHealth "
            "and result Journal/SetJournalIndex/GetJournalIndex/AddTopic/Choice/ModDisposition/Disable/Enable/Position/PositionCell/AiTravel/AiEscort; "
            "remaining quest batches isolated to (1) bank GetJournalIndex/CellChanged/MenuMode/GetPCSleep, "
            "(2) OnActivate/Activate/foreign-ModDisposition, "
            "(3) provenance GetItemCount/AddItem/RemoveItem\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2 || argc == 3, "Usage: tes3mp_native_world_script_tests FILTER [CONTENT_CONFIG]");
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
