#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/locals.hpp>
#include <apps/openmw/mwscript/ref.hpp>
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
#include <components/esm3/locals.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/runtime.hpp>

#include <iostream>
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
    public:
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string&, const ESM::RefId&) const override { return {' ', false}; }
        bool isId(const ESM::RefId& name) const override { return name == "player" || name == "fixture_actor" || name == "absent"; }
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
    Compiled compile(std::string_view body)
    {
        Compiler::Extensions extensions;
        extensions.registerInstruction("pcjoinfaction", "/S", Compiler::Stats::opcodePCJoinFaction,
            Compiler::Stats::opcodePCJoinFactionExplicit);
        extensions.registerInstruction("pcraiserank", "/S", Compiler::Stats::opcodePCRaiseRank,
            Compiler::Stats::opcodePCRaiseRankExplicit);
        extensions.registerFunction("getpcrank", 'l', "/S", Compiler::Stats::opcodeGetPCRank,
            Compiler::Stats::opcodeGetPCRankExplicit);
        CompilerContext context; context.setExtensions(&extensions);
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
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: tes3mp_native_script_context_tests lookup|rollback|stock");
        MWClass::registerClasses();
        const std::string_view filter = argv[1];
        if (filter == "lookup") lookup();
        else if (filter == "rollback") rollback();
        else if (filter == "stock") stock();
        else throw std::invalid_argument("Unknown script context filter");
        std::cout << "PASS script-context " << filter << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
