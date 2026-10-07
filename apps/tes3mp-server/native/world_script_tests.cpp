#include "world_script.hpp"
#include "personal_script.hpp"
#include "loadout.hpp"

#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadnpc.hpp>

#include <algorithm>
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
            }
            require(accepted == (std::string_view(name) == "fargoth"), "Stock script admission classification changed");
        }
        require(admitted == 1 && rejected == 3, "Excluded stock script coverage differs");
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
        else if (filter == "shared" && argc == 2) shared(*crypto);
        else if (filter == "rollback") rollback(*crypto);
        else if (filter == "recovery") recovery(*crypto);
        else if (filter == "admission") admission(*crypto);
        else if (filter == "personal") personal(*crypto);
        else if (filter == "personal-scheduling") personalScheduling(*crypto);
        else throw std::invalid_argument("Unknown filter");
        std::cout << "PASS shared-world-script " << filter << '\n'; return 0;
    }
    catch (const std::exception& error)
    { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
