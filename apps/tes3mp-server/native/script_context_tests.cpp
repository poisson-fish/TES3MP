#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/locals.hpp>
#include <apps/openmw/mwscript/miscextensions.hpp>
#include <apps/openmw/mwscript/ref.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwscript/statsextensions.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/opcodes.hpp>
#include <components/compiler/scanner.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/locals.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/globalscript.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/localopcodes.hpp>
#include <components/interpreter/runtime.hpp>

#include <array>
#include <iostream>
#include <limits>
#include <sstream>

namespace
{
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template<class F> void rejects(F&& operation, const char* message)
    {
        try { operation(); } catch (const std::exception&) { return; }
        throw std::runtime_error(message);
    }
    ESM::RefId id(std::string_view name) { return ESM::RefId::stringRefId(name); }
    MWScript::ExecutionContext execution(uint64_t character)
    { return {MWScript::ExecutionContext::Ownership::Character, 7, character + 100, character + 200,
        character, 23, 2, 41, 53}; }

    struct Content
    {
        MWWorld::ESMStore store;
        Content()
        {
            ESM::Race race; race.blank(); race.mId = id("fixture_race"); race.mName = "Race";
            store.insertStatic(race);
            ESM::Class characterClass; characterClass.blank(); characterClass.mId = id("fixture_class");
            characterClass.mName = "Class"; store.insertStatic(characterClass);
            ESM::Faction faction; faction.blank(); faction.mId = id("fixture_faction"); faction.mName = "Faction";
            for (int i = 0; i < 10; ++i) faction.mRanks[i] = "Rank " + std::to_string(i);
            store.insertStatic(faction);
            for (const char* name : {"player_a", "player_b", "fixture_actor"})
            {
                ESM::NPC npc; npc.blank(); npc.mId = id(name); npc.mName = name;
                npc.mRace = race.mId; npc.mClass = characterClass.mId; npc.mFaction = faction.mId;
                store.insertStatic(npc);
            }
        }
    };

    struct Resolver final : MWScript::ReferenceResolver
    {
        MWScript::ExecutionContext expected;
        MWWorld::Ptr player, source;
        bool unavailable = false;
        mutable bool lastActiveOnly = false;
        mutable unsigned calls = 0;
        Resolver(MWScript::ExecutionContext context, MWWorld::Ptr character, MWWorld::Ptr actor)
            : expected(context), player(character), source(actor) {}
        MWWorld::Ptr resolve(const MWScript::ExecutionContext& context, const ESM::RefId& reference,
            bool activeOnly) const override
        {
            ++calls; lastActiveOnly = activeOnly;
            require(context == expected, "Lookup lost campaign/story/scope/character/source/life/invocation/event");
            if (unavailable) throw MWScript::ScriptContextUnavailable();
            if (reference == "player") return player;
            if (reference.empty() || reference == "fixture_actor") return source;
            return {};
        }
    };

    void lookup()
    {
        Content content;
        MWWorld::ManualRef a(content.store, id("player_a")), b(content.store, id("player_b"));
        // Identical record names can denote different physical instances.
        MWWorld::ManualRef actorA(content.store, id("fixture_actor")), actorB(content.store, id("fixture_actor"));
        Resolver resolverA(execution(1), a.getPtr(), actorA.getPtr()), resolverB(execution(2), b.getPtr(), actorB.getPtr());
        MWMechanics::NpcStats statsA(content.store), statsB(content.store);
        statsA.setBounty(11); statsB.setBounty(29);
        MWScript::InterpreterContext contextA(nullptr, execution(1), content.store, resolverA, &statsA);
        MWScript::InterpreterContext contextB(nullptr, execution(2), content.store, resolverB, &statsB);
        Interpreter::Program program; program.mStrings = {"player", "fixture_actor", "absent", "", std::string(129, 'x')};
        Interpreter::Runtime runtime;
        for (auto* context : {&contextA, &contextB})
        {
            const bool first = context == &contextA;
            runtime.configure(program, *context);
            runtime.push(0);
            require(MWScript::ExplicitRef()(runtime) == (first ? a.getPtr() : b.getPtr()), "Explicit player crossed characters");
            runtime.push(1);
            require(MWScript::ExplicitRef()(runtime, true, true) == (first ? actorA.getPtr() : actorB.getPtr()),
                "Explicit source crossed physical scopes");
            require((first ? resolverA : resolverB).lastActiveOnly, "Explicit lookup lost active-only policy");
            require(MWScript::ImplicitRef()(runtime, true, false) == (first ? actorA.getPtr() : actorB.getPtr())
                && !(first ? resolverA : resolverB).lastActiveOnly, "Implicit lookup bypassed scoped resolver");
            runtime.push(2); require(MWScript::ExplicitRef()(runtime, false).isEmpty(), "Optional absence fabricated a reference");
            runtime.push(2); rejects([&] { MWScript::ExplicitRef()(runtime); }, "Required absent reference accepted");
            runtime.push(3); require(MWScript::ExplicitRef()(runtime, false).isEmpty(), "Empty explicit name became implicit source");
            runtime.push(4); rejects([&] { MWScript::ExplicitRef()(runtime); }, "Oversized literal lookup admitted");
        }
        require(contextA.getPCName() == "player_a" && contextB.getPCName() == "player_b"
            && contextA.getPCRace() == "Race" && contextB.getPCClass() == "Class"
            && contextA.getPCBounty() == 11 && contextB.getPCBounty() == 29, "Personal text used the stock protagonist");
        statsA.joinFaction(id("fixture_faction")); statsA.setFactionRank(id("fixture_faction"), 3, content.store);
        require(contextA.getPCRank() == "Rank 3" && contextB.getPCRank() == "Rank 0"
            && contextA.getPCNextRank() == "Rank 4" && contextB.getPCNextRank() == "Rank 0",
            "Personal rank text ignored staged stats or stock defaults");
        rejects([&] { contextA.getGlobalLong("foreign"); }, "Scoped globals fell back to world state");
        rejects([&] { contextA.setGlobalLong("foreign", 9); }, "Scoped globals wrote world state");
        rejects([&] { contextA.getMemberLong(id("foreign"), "value", true); }, "Member locals fell back to global scripts");
        rejects([&] { contextA.setMemberLong(id("fixture_actor"), "value", 9, false); }, "Member locals wrote unstaged references");
        rejects([&] { contextA.messageBox("unstaged", {}); }, "Scoped output bypassed transaction");
        rejects([&] { contextA.executeActivation(actorA.getPtr(), a.getPtr()); }, "Scoped activation bypassed transaction");
        resolverA.unavailable = true;
        bool deferred = false;
        try { contextA.resolveReference(id("absent"), false); }
        catch (const MWScript::ScriptContextUnavailable&) { deferred = true; }
        require(deferred, "Unavailable optional lookup became absence");
        resolverA.unavailable = false;
        auto staleLife = execution(1); ++staleLife.sourceLife;
        MWScript::InterpreterContext stale(nullptr, staleLife, content.store, resolverA);
        rejects([&] { stale.getReference(); }, "Stale source life accepted");
        rejects([&] { contextA.resolveReference(id(std::string(129, 'x'))); }, "Oversized reference lookup admitted");
        {
            MWWorld::ManualRef temporary(content.store, id("fixture_actor"));
            resolverA.source = temporary.getPtr();
        }
        rejects([&] { contextA.getReference(false); }, "Destroyed scoped source returned to caller");

        auto shared = execution(1); shared.ownership = MWScript::ExecutionContext::Ownership::Shared;
        shared.character = shared.story = 0;
        Resolver sharedResolver(shared, {}, actorA.getPtr());
        MWScript::InterpreterContext sharedContext(nullptr, shared, content.store, sharedResolver);
        rejects([&] { sharedContext.getPlayer(); }, "Shared script acquired a character");
        rejects([&] { sharedContext.getPlayerStats(); }, "Shared script acquired player stats");
        rejects([&] { sharedContext.getPCRank(); }, "Shared script acquired personal rank text");
        rejects([&] { MWScript::InterpreterContext invalid(nullptr, shared, content.store, sharedResolver, &statsA); },
            "Shared script admitted personal stats");
        require(sharedResolver.calls == 0, "Shared player rejection reached the resolver");
        auto global = execution(1); global.sourceReference = global.sourceLife = 0;
        Resolver globalResolver(global, a.getPtr(), {});
        MWScript::InterpreterContext globalContext(nullptr, global, content.store, globalResolver);
        require(globalContext.getReference(false).isEmpty() && globalContext.getPlayer() == a.getPtr(),
            "Personal global script lost source-free character binding");
        rejects([&] { globalContext.getPlayerStats(); }, "Scoped stats borrowed a live character writer");
        for (int field = 0; field < 6; ++field)
        {
            auto invalid = execution(1);
            if (field == 0) invalid.campaign = 0;
            if (field == 1) invalid.story = 0;
            if (field == 2) invalid.physicalScope = 0;
            if (field == 3) invalid.character = 0;
            if (field == 4) invalid.sourceLife = 0;
            if (field == 5) invalid.invocation = 0;
            rejects([&] { MWScript::InterpreterContext bad(nullptr, invalid, content.store, resolverA); },
                "Incomplete context admitted");
        }
    }

    class CompilerContext final : public Compiler::Context
    {
        const Compiler::Locals* mMembers;
    public:
        explicit CompilerContext(const Compiler::Locals* members = nullptr) : mMembers(members) {}
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& script) const override
        {
            return {mMembers && (script == "ledger_x73" || script == "relay_q19") ? mMembers->getType(name) : ' ', false};
        }
        bool isId(const ESM::RefId& name) const override
        {
            return name == "player" || name == "fixture_actor" || name == "absent"
                || (mMembers && (name == "ledger_x73" || name == "relay_q19"));
        }
    };
    class Errors final : public Compiler::ErrorHandler
    {
        void report(const std::string&, const Compiler::TokenLoc&, Type) override {}
        void report(const std::string&, Type) override {}
    };
    struct Compiled
    {
        Interpreter::Program program;
        Compiler::Locals declarations;
    };
    Compiled compile(std::string_view body, const Compiler::Locals* members = nullptr)
    {
        Compiler::Extensions extensions;
        extensions.registerInstruction("pcjoinfaction", "/S", Compiler::Stats::opcodePCJoinFaction,
            Compiler::Stats::opcodePCJoinFactionExplicit);
        extensions.registerInstruction("pcraiserank", "/S", Compiler::Stats::opcodePCRaiseRank,
            Compiler::Stats::opcodePCRaiseRankExplicit);
        extensions.registerFunction("getpcrank", 'l', "/S", Compiler::Stats::opcodeGetPCRank,
            Compiler::Stats::opcodeGetPCRankExplicit);
        extensions.registerFunction("scriptrunning", 'l', "c", Compiler::Misc::opcodeScriptRunning);
        extensions.registerInstruction("startscript", "c", Compiler::Misc::opcodeStartScript,
            Compiler::Misc::opcodeStartScriptExplicit);
        extensions.registerInstruction("stopscript", "c", Compiler::Misc::opcodeStopScript);
        CompilerContext context(members); context.setExtensions(&extensions);
        Errors errors; errors.setWarningsMode(2);
        Compiler::FileParser parser(errors, context);
        std::istringstream input("Begin arbitrary_context\nshort phase\n" + std::string(body) + "\nEnd arbitrary_context\n");
        Compiler::Scanner scanner(errors, input, &extensions); scanner.scan(parser);
        require(errors.isGood() && errors.countWarnings() == 0, "Fixture script did not compile strictly");
        return {parser.getProgram(), parser.getLocals()};
    }
    struct StagedContext final : MWScript::InterpreterContext
    {
        std::vector<std::string>& output;
        StagedContext(MWScript::Locals& locals, MWScript::ExecutionContext execution, const MWWorld::ESMStore& content,
            const Resolver& resolver, MWMechanics::NpcStats& stats, std::vector<std::string>& messages)
            : InterpreterContext(&locals, execution, content, resolver, &stats), output(messages) {}
        void messageBox(std::string_view message, const std::vector<std::string>& buttons) override
        { require(buttons.empty() && output.size() < 8 && message.size() <= 256, "Fixture output exceeded bounds"); output.emplace_back(message); }
    };

    void rollback()
    {
        Content content;
        MWWorld::ManualRef a(content.store, id("player_a")), b(content.store, id("player_b")), actor(content.store, id("fixture_actor"));
        Resolver resolver(execution(1), a.getPtr(), actor.getPtr());
        auto committedA = std::make_unique<MWMechanics::NpcStats>(content.store);
        MWMechanics::NpcStats committedB(content.store);
        std::vector<std::string> published;
        const auto good = compile("set phase to phase + 1\nplayer->PCJoinFaction \"fixture_faction\"\n"
            "fixture_actor->PCRaiseRank \"fixture_faction\"\nMessageBox \"committed\"");
        const auto bad = compile("set phase to phase + 1\nPCJoinFaction \"fixture_faction\"\n"
            "MessageBox \"discarded\"\nset phase to absent->GetPCRank");
        ESM::Variant zero; zero.setType(ESM::VT_Int); zero.setInteger(0);
        ESM::Locals initial; initial.mVariables.emplace_back("phase", zero);
        auto committedLocals = MWScript::Locals::restore(initial, id("arbitrary_context"), good.declarations);
        enum class Outcome { Committed, Deferred, Rejected };
        auto transact = [&](const Compiled& script, bool durable) {
            auto locals = committedLocals;
            auto stats = std::make_unique<MWMechanics::NpcStats>(*committedA);
            std::vector<std::string> messages;
            StagedContext context(locals, execution(1), content.store, resolver, *stats, messages);
            Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
            MWScript::Stats::installOpcodes(interpreter);
            try { interpreter.run(script.program, context, 256); }
            catch (const MWScript::ScriptContextUnavailable&) { return Outcome::Deferred; }
            catch (const std::exception&) { return Outcome::Rejected; }
            if (!durable) return Outcome::Deferred;
            committedLocals = std::move(locals); committedA.swap(stats); published.swap(messages);
            return Outcome::Committed;
        };
        require(transact(bad, true) == Outcome::Rejected && committedLocals.mShorts[0] == 0
            && committedA->getFactionRanks().empty() && published.empty(), "Rejected stock caller leaked staged changes/output");
        require(transact(good, false) == Outcome::Deferred && committedLocals.mShorts[0] == 0
            && committedA->getFactionRanks().empty() && published.empty(), "Failed durability leaked staged changes/output");
        resolver.unavailable = true;
        require(transact(good, true) == Outcome::Deferred && committedLocals.mShorts[0] == 0
            && published.empty(), "Dependency wait leaked staged locals");
        resolver.unavailable = false;
        require(transact(good, true) == Outcome::Committed && committedLocals.mShorts[0] == 1
            && committedA->getFactionRank(id("fixture_faction")) == 1 && published == std::vector<std::string>{"committed"}
            && committedB.getFactionRanks().empty(), "Retry did not publish one isolated stock result");
        ESM::Locals saved;
        require(committedLocals.write(saved, good.declarations), "Committed locals did not serialize with stock declarations");
        auto recovered = MWScript::Locals::restore(saved, id("arbitrary_context"), good.declarations);
        require(recovered.mShorts == committedLocals.mShorts, "Scoped locals failed stock recovery");
    }

    void stock()
    {
        Content content;
        MWWorld::ManualRef actor(content.store, id("fixture_actor")), player(content.store, id("player_a"));
        const auto script = compile("set phase to phase + 1");
        MWScript::Locals locals; locals.mShorts = {4};
        MWScript::InterpreterContext context(&locals, actor.getPtr());
        Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
        interpreter.run(script.program, context); // Stock unlimited execution remains supported.
        require(!context.getExecutionContext() && !context.hasExplicitPlayer() && locals.mShorts[0] == 5
            && context.getReference() == actor.getPtr() && context.getTarget() == id("fixture_actor"), "Stock local caller changed");
        MWMechanics::NpcStats stats(content.store);
        MWScript::InterpreterContext detached(&locals, actor.getPtr(), player.getPtr(), content.store, &stats);
        require(detached.resolveReference(id("player")) == player.getPtr() && detached.getReference() == actor.getPtr(),
            "Detached migration lost player/source binding");
        rejects([&] { detached.resolveReference(id("fixture_actor")); }, "Detached lookup reached an unscoped world");
        MWScript::InterpreterContext empty(nullptr, {});
        require(empty.getReference(false).isEmpty() && empty.getTarget().empty(), "Stock optional implicit reference changed");
        rejects([&] { empty.getReference(); }, "Stock missing implicit reference accepted");
    }

    MWScript::ExecutionContext sourceFree(uint64_t character)
    {
        auto result = execution(character); result.sourceReference = result.sourceLife = 0;
        return result;
    }
    struct Bank final : MWScript::ScriptState
    {
        MWScript::ExecutionContext expected;
        Compiler::Locals declarations;
        std::array<MWScript::GlobalScriptDesc, 2> scripts;
        bool unavailable = false;
        unsigned calls = 0;
        explicit Bank(MWScript::ExecutionContext context) : expected(context)
        {
            declarations.declare('s', "token"); declarations.declare('l', "calls"); declarations.declare('f', "phase");
            for (size_t i = 0; i < scripts.size(); ++i)
            {
                ESM::Locals values;
                for (const char type : {'s', 'l', 'f'})
                    for (const auto& name : std::as_const(declarations).get(type))
                    {
                        ESM::Variant zero; zero.setType(type == 'f' ? ESM::VT_Float : ESM::VT_Int);
                        values.mVariables.emplace_back(name, zero);
                    }
                scripts[i].mLocals = MWScript::Locals::restore(values, id(i ? "relay_q19" : "ledger_x73"), declarations);
            }
        }
        Binding resolve(const MWScript::ExecutionContext& context, const ESM::RefId& script) override
        {
            ++calls;
            require(context == expected, "Script state lost an execution identity or crossed ownership");
            if (unavailable) throw MWScript::ScriptContextUnavailable();
            require(script == "ledger_x73" || script == "relay_q19", "Script lacks explicit bank ownership");
            return {scripts[script == "relay_q19" ? 1 : 0], declarations};
        }
        std::string save() const
        {
            std::ostringstream output(std::ios::binary);
            ESM::ESMWriter writer; writer.setVersion(ESM::VER_130); writer.setType(0);
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion); writer.setRecordCount(2); writer.save(output);
            for (const auto& entry : scripts)
            {
                ESM::GlobalScript value; value.mId = entry.mLocals.getScriptId(); value.mRunning = entry.mRunning;
                require(entry.mLocals.write(value.mLocals, declarations), "Bank locals failed stock serialization");
                writer.startRecord("GSCR"); value.save(writer); writer.endRecord("GSCR");
            }
            writer.close(); return output.str();
        }
        void restore(const std::string& saved)
        {
            ESM::ESMReader reader; reader.open(std::make_unique<std::istringstream>(saved, std::ios::binary), {});
            auto staged = scripts;
            for (auto& entry : staged)
            {
                require(reader.getRecName() == ESM::fourCC("GSCR"), "Missing stock bank record"); reader.getRecHeader();
                ESM::GlobalScript value; value.load(reader);
                require(value.mId == entry.mLocals.getScriptId() && value.mTargetId.empty() && !value.mTargetRef.isSet(),
                    "Bank recovery lost identity or acquired a physical target");
                entry.mLocals = MWScript::Locals::restore(value.mLocals, value.mId, declarations);
                entry.mRunning = value.mRunning;
            }
            require(!reader.hasMoreRecs(), "Unexpected bank recovery record"); scripts.swap(staged);
        }
    };
    struct BankContext final : MWScript::InterpreterContext
    {
        std::vector<std::string> messages;
        BankContext(MWScript::Locals* locals, Bank& bank, const Content& content, const Resolver& resolver)
            : InterpreterContext(locals, bank.expected, content.store, resolver, nullptr, &bank) {}
        void messageBox(std::string_view message, const std::vector<std::string>& buttons) override
        { require(buttons.empty() && messages.size() < 8 && message.size() <= 256, "Bank output exceeded bounds"); messages.emplace_back(message); }
    };
    void run(const Compiled& script, MWScript::InterpreterContext& context)
    {
        Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
        MWScript::Misc::installOpcodes(interpreter); interpreter.run(script.program, context, 256);
    }
    constexpr std::string_view BankCalls = "set phase to phase + 1\n"
        "set ledger_x73.token to ledger_x73.token + 2\nset ledger_x73.calls to ledger_x73.calls + 5\n"
        "set ledger_x73.phase to ledger_x73.phase + 0.25\nStartScript ledger_x73\n"
        "set phase to ScriptRunning ledger_x73\nStopScript ledger_x73\nStartScript relay_q19\n"
        "set phase to phase + ScriptRunning relay_q19\nMessageBox \"committed\"";

    void crossScript()
    {
        Content content;
        Bank alice(sourceFree(1)), bob(sourceFree(2)), otherStory(sourceFree(1)); ++otherStory.expected.story;
        Resolver resolver(alice.expected, {}, {});
        MWScript::Locals caller; caller.mShorts = {0};
        BankContext context(&caller, alice, content, resolver), peer(nullptr, bob, content, resolver),
            story(nullptr, otherStory, content, resolver);
        const auto peerBefore = bob.save(), storyBefore = otherStory.save();
        const auto script = compile(BankCalls, &alice.declarations); run(script, context);
        require(caller.mShorts[0] == 2 && context.getMemberShort(id("ledger_x73"), "token", true) == 2
            && context.getMemberLong(id("ledger_x73"), "calls", true) == 5
            && context.getMemberFloat(id("ledger_x73"), "phase", true) == .25f
            && !context.scriptRunning("ledger_x73") && context.scriptRunning("relay_q19")
            && !peer.scriptRunning("relay_q19") && !story.scriptRunning("relay_q19")
            && bob.save() == peerBefore && otherStory.save() == storyBefore && resolver.calls == 0,
            "Stock cross-script calls executed inline, resolved physics, or crossed character/story ownership");
        context.startScript("ledger_x73", {}); context.startScript("ledger_x73", {});
        context.stopScript("ledger_x73"); context.stopScript("ledger_x73");
        require(context.getMemberLong(id("ledger_x73"), "calls", true) == 5, "Start/stop reset script locals");
        context.setMemberShort(id("ledger_x73"), "token", 65537, true);
        require(context.getMemberShort(id("ledger_x73"), "token", true) == 1, "Member short lost stock conversion");
        const auto before = alice.save();
        for (const std::string name : {std::string(), std::string(129, 'x'), std::string("bad\0name", 8), std::string("foreign")})
        {
            const auto calls = alice.calls;
            rejects([&] { context.scriptRunning(name); }, "Unbounded/foreign running query accepted");
            rejects([&] { context.startScript(name, {}); }, "Unbounded/foreign start accepted");
            rejects([&] { context.stopScript(name); }, "Unbounded/foreign stop accepted");
            rejects([&] { context.getMemberLong(id("ledger_x73"), name, true); }, "Unbounded/unknown member accepted");
            if (name != "foreign") require(alice.calls == calls, "Unbounded name reached the bank resolver");
        }
        rejects([&] { context.getMemberLong(id("ledger_x73"), "token", true); }, "Mistyped member accepted");
        rejects([&] { context.setMemberFloat(id("ledger_x73"), "phase", std::numeric_limits<float>::infinity(), true); },
            "Nonfinite member accepted");
        rejects([&] { context.setMemberLong(id("ledger_x73"), "calls", 9, false); }, "Physical member operation accepted");
        for (int field = 0; field < 9; ++field)
        {
            auto foreign = alice.expected;
            if (field == 0) ++foreign.campaign;
            if (field == 1) ++foreign.story;
            if (field == 2) ++foreign.physicalScope;
            if (field == 3) ++foreign.character;
            if (field == 4) foreign.sourceReference = foreign.sourceLife = 9;
            if (field == 5) ++foreign.invocation;
            if (field == 6) ++foreign.causalEvent;
            if (field == 7) { foreign.ownership = MWScript::ExecutionContext::Ownership::Shared; foreign.character = foreign.story = 0; }
            if (field == 8) { foreign.sourceReference = 9; foreign.sourceLife = 10; }
            MWScript::InterpreterContext invalid(nullptr, foreign, content.store, resolver, nullptr, &alice);
            rejects([&] { invalid.setMemberLong(id("ledger_x73"), "calls", 9, true); }, "Foreign member write accepted");
            rejects([&] { invalid.startScript("ledger_x73", {}); }, "Foreign lifecycle write accepted");
        }
        require(alice.save() == before, "Rejected cross-script operation changed the bank");
        // Exercise core literal conversion, rather than interning malformed IDs
        // in the fixture before calling the scoped context.
        Interpreter::Program literals; literals.mStrings = {"calls", std::string(129, 'x')};
        Interpreter::Runtime runtime; const auto calls = alice.calls;
        runtime.configure(literals, context); runtime.push(0); runtime.push(1);
        rejects([&] { Interpreter::OpFetchMemberLong<true>().execute(runtime); }, "Oversized fetch literal accepted");
        runtime.configure(literals, context); runtime.push(0); runtime.push(1); runtime.push(9);
        rejects([&] { Interpreter::OpStoreMemberLong<true>().execute(runtime); }, "Oversized store literal accepted");
        require(alice.calls == calls && alice.save() == before, "Unbounded core literal reached script bank");
        Bank maximum(alice.expected);
        for (size_t i = 3; i < MWScript::ScriptState::MaximumLocals; ++i)
        { maximum.declarations.declare('l', "extra" + std::to_string(i)); maximum.scripts[0].mLocals.mLongs.push_back(0); }
        BankContext bounded(nullptr, maximum, content, resolver);
        const std::string maximumName(MWScript::ScriptState::MaximumNameBytes, 'x');
        require(bounded.getMemberId(maximumName).getRefIdString() == maximumName, "Maximum script name rejected");
        bounded.setMemberLong(id("ledger_x73"), "extra63", 9, true);
        require(bounded.getMemberLong(id("ledger_x73"), "extra63", true) == 9, "Maximum local count rejected");
        for (int fault = 0; fault < 5; ++fault)
        {
            Bank malformed = alice;
            if (fault == 0) malformed.scripts[0].mLocals = malformed.scripts[1].mLocals;
            if (fault == 1) malformed.scripts[0].mLocals.mLongs.clear();
            if (fault == 2)
                for (size_t i = 1; i <= MWScript::ScriptState::MaximumLocals; ++i)
                { malformed.declarations.declare('l', "extra" + std::to_string(i)); malformed.scripts[0].mLocals.mLongs.push_back(0); }
            if (fault == 3) malformed.scripts[0].mTarget = std::pair<ESM::RefNum, ESM::RefId>{};
            if (fault == 4) malformed.scripts[0].mLocals.mFloats[0] = std::numeric_limits<float>::quiet_NaN();
            BankContext invalid(nullptr, malformed, content, resolver);
            rejects([&] { invalid.startScript("ledger_x73", {}); }, "Malformed staged script binding accepted");
        }
        alice.unavailable = true;
        bool deferred = false;
        try { context.scriptRunning("ledger_x73"); } catch (const MWScript::ScriptContextUnavailable&) { deferred = true; }
        require(deferred, "Waiting script became a stopped script");
        alice.unavailable = false;
        MWScript::InterpreterContext closed(nullptr, alice.expected, content.store, resolver);
        rejects([&] { closed.scriptRunning("ledger_x73"); }, "Unprovided bank fell back to stock scripts");
        rejects([&] { closed.startScript("ledger_x73", {}); }, "Unprovided bank started a stock script");
        rejects([&] { closed.stopScript("ledger_x73"); }, "Unprovided bank stopped a stock script");
        std::cout << "cross-script: stock typed members/lifecycle, character/story isolation, all identities, bounds, waits, closed services\n";
    }

    void crossScriptRollback()
    {
        Content content; MWWorld::ManualRef actor(content.store, id("fixture_actor"));
        Bank committed(sourceFree(1)); Resolver resolver(committed.expected, {}, actor.getPtr());
        const auto initial = committed.save(); const auto good = compile(BankCalls, &committed.declarations);
        const auto bad = compile(std::string(BankCalls) + "\nStartScript foreign", &committed.declarations);
        const auto targeted = compile(std::string(BankCalls) + "\nfixture_actor->StartScript ledger_x73", &committed.declarations);
        MWScript::Locals caller; caller.mShorts = {0}; std::vector<std::string> published;
        enum class Outcome { Committed, Deferred, Rejected };
        auto transact = [&](const Compiled& script, bool durable) {
            auto staged = committed; auto locals = caller;
            BankContext context(&locals, staged, content, resolver);
            try { run(script, context); }
            catch (const MWScript::ScriptContextUnavailable&) { return Outcome::Deferred; }
            catch (const std::exception&) { return Outcome::Rejected; }
            if (!durable) return Outcome::Deferred;
            committed = std::move(staged); caller = std::move(locals); published = std::move(context.messages);
            return Outcome::Committed;
        };
        for (const auto* script : {&bad, &targeted})
            require(transact(*script, true) == Outcome::Rejected && committed.save() == initial
                && caller.mShorts[0] == 0 && published.empty(), "Late failure leaked cross-script locals/lifecycle/output");
        require(transact(good, false) == Outcome::Deferred && committed.save() == initial
            && caller.mShorts[0] == 0 && published.empty(), "Failed durability leaked script bank");
        committed.unavailable = true;
        require(transact(good, true) == Outcome::Deferred && committed.save() == initial && caller.mShorts[0] == 0,
            "Dependency wait leaked caller locals");
        committed.unavailable = false;
        require(transact(good, true) == Outcome::Committed && committed.scripts[0].mLocals.mLongs[0] == 5
            && !committed.scripts[0].mRunning && committed.scripts[1].mRunning
            && caller.mShorts[0] == 2 && published == std::vector<std::string>{"committed"}, "Retry lost or duplicated script effects");
        const auto saved = committed.save(); Bank recovered(committed.expected); recovered.restore(saved);
        require(recovered.save() == saved && recovered.scripts[0].mLocals.mLongs[0] == 5
            && !recovered.scripts[0].mRunning && recovered.scripts[1].mRunning, "Stock GSCR recovery lost member state/lifecycle");
        std::cout << "cross-script rollback: late/targeted rejection, durability/wait rollback, retry, stock GSCR locals/running recovery\n";
    }

    void crossScriptStock()
    {
        Content content; MWBase::Environment environment; environment.setESMStore(content.store);
        Compiler::Locals declarations; declarations.declare('s', "token"); declarations.declare('l', "calls");
        declarations.declare('f', "phase");
        for (const char* name : {"ledger_x73", "relay_q19"})
        {
            ESM::Script value; value.blank(); value.mId = id(name);
            value.mScriptText = "Begin " + std::string(name) + "\nshort token\nlong calls\nfloat phase\nEnd " + name + "\n";
            content.store.insertStatic(value);
        }
        CompilerContext compiler(&declarations); Compiler::Extensions extensions; compiler.setExtensions(&extensions);
        MWScript::ScriptManager manager(content.store, compiler, 2); environment.setScriptManager(manager);
        MWScript::Locals caller; caller.mShorts = {0};
        MWScript::InterpreterContext context(&caller, {});
        run(compile(BankCalls.substr(0, BankCalls.find("\nMessageBox")), &declarations), context);
        require(caller.mShorts[0] == 2 && context.getMemberShort(id("ledger_x73"), "token", true) == 2
            && context.getMemberLong(id("ledger_x73"), "calls", true) == 5
            && context.getMemberFloat(id("ledger_x73"), "phase", true) == .25f
            && !context.scriptRunning("ledger_x73") && context.scriptRunning("relay_q19")
            && !context.scriptRunning("foreign"), "Stock global-script members/lifecycle changed");
        context.startScript("foreign", {}); context.stopScript("foreign");
        require(manager.getGlobalScripts().getScripts().size() == 2, "Stock missing script created an instance");
        MWWorld::ManualRef target(content.store, id("fixture_actor"));
        context.startScript("ledger_x73", target.getPtr());
        const auto* entry = manager.getGlobalScripts().getScriptIfPresent(id("ledger_x73"));
        require(entry && entry->mRunning && entry->getPtrIfPresent() && *entry->getPtrIfPresent() == target.getPtr()
            && entry->mLocals.mLongs[0] == 5, "Stock targeted restart lost target or reset locals");
        std::cout << "cross-script stock: unchanged compiled member calls, running flags, absent scripts, targeted restart/local retention\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: tes3mp_native_script_context_tests lookup|rollback|stock|cross-script|cross-script-rollback|cross-script-stock");
        MWClass::registerClasses();
        const std::string_view filter = argv[1];
        if (filter == "lookup") lookup();
        else if (filter == "rollback") rollback();
        else if (filter == "stock") stock();
        else if (filter == "cross-script") crossScript();
        else if (filter == "cross-script-rollback") crossScriptRollback();
        else if (filter == "cross-script-stock") crossScriptStock();
        else throw std::invalid_argument("Unknown script context filter");
        std::cout << "PASS script-context " << filter << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
