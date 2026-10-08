#ifndef TES3MP_NATIVE_WORLD_SCRIPT_HPP
#define TES3MP_NATIVE_WORLD_SCRIPT_HPP

#include <apps/openmw/mwscript/worldscript.hpp>
#include <components/interpreter/interpreter.hpp>
#include <tes3mp/server_authentication.hpp>

#include <functional>
#include <span>

namespace TES3MP::Native
{
    // Prepared engine state/output joins the host's existing gameplay commit;
    // scripts never persist in a parallel file.
    class SharedWorldScript
    {
    public:
        static constexpr std::size_t MaximumImageBytes = 32768;
        static constexpr std::size_t MaximumDiagnosticBytes = 256;
        struct State
        {
            uint64_t tick = 0; // Consecutive invocation ordinal; world ticks may skip.
            MWScript::WorldScript::State runtime;
        };
        enum class Outcome { Committed, Deferred, Rejected };
        struct Result
        {
            Outcome outcome = Outcome::Rejected;
            bool executed = false;
            ESM::RefId script;
            uint64_t invocationTick = 0;
            Interpreter::ExecutionTrace trace;
            std::vector<std::string> presentation;
            std::string diagnostic;
        };
        using DurableSink = std::function<bool(std::span<const char>)>;
        struct Prepared
        {
            State state;
            std::vector<char> image;
            Result result;
        };

        SharedWorldScript(const ESM::Script& script, std::vector<ESM::Global> globals,
            std::string_view campaignContent, CredentialCrypto& crypto, std::span<const ESM::Script> bank = {});
        const Compiler::Locals& declarations() const { return mRuntime.declarations(); }
        bool bankCapable() const { return mRuntime.bankCapable(); }
        Interpreter::Program compileResult(std::string_view source, bool physical = false,
            const ESM::RefId& reference = {}, bool restoration = false) const
        { return mRuntime.compileResult(source, physical, reference, restoration); }
        MWScript::WorldScript::Invocation executeResult(const State& state, const MWScript::ExecutionContext& execution,
            const MWWorld::ESMStore& content, const MWScript::ReferenceResolver& resolver, MWScript::ScriptState& bank,
            MWScript::GlobalScriptDesc& caller, MWScript::DialogueState& dialogue, const Interpreter::Program& program,
            Interpreter::ExecutionTrace& trace, MWMechanics::NpcStats* sourceNpcStats = nullptr,
            MWScript::ReferenceState* referenceState = nullptr) const
        { return mRuntime.execute(state.runtime, execution, content, resolver, &trace, &bank, &caller, &dialogue, &program, sourceNpcStats, referenceState); }
        State initial() const;
        std::vector<char> encode(const State& state) const;
        State restore(std::span<const char> image) const;
        Prepared prepare(const State& committed, uint64_t tick) const;
        Prepared prepare(const State& committed, const MWScript::ExecutionContext& execution,
            const MWWorld::ESMStore& content, const MWScript::ReferenceResolver& resolver) const;
        Prepared prepareBank(const State&, const MWScript::ExecutionContext&, const MWWorld::ESMStore&,
            const MWScript::ReferenceResolver&, MWScript::ScriptState&, MWScript::GlobalScriptDesc&) const;
        Result transact(State& committed, uint64_t tick, const DurableSink& persist) const;

    private:
        Prepared prepareImpl(const State&, uint64_t, const MWScript::ExecutionContext*,
            const MWWorld::ESMStore*, const MWScript::ReferenceResolver*, MWScript::ScriptState* = nullptr,
            MWScript::GlobalScriptDesc* = nullptr) const;
        MWScript::WorldScript mRuntime;
        CredentialDigest mIdentity;
    };
}
#endif
