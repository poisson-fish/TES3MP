#ifndef TES3MP_NATIVE_WORLD_SCRIPT_HPP
#define TES3MP_NATIVE_WORLD_SCRIPT_HPP

#include <apps/openmw/mwscript/worldscript.hpp>
#include <components/interpreter/interpreter.hpp>
#include <tes3mp/server_authentication.hpp>

#include <functional>
#include <span>

namespace TES3MP::Native
{
    // Admission/transaction seam only. The host must compose this image with its
    // existing gameplay commit, never persist scripts in a parallel file.
    class SharedWorldScript
    {
    public:
        static constexpr std::size_t MaximumImageBytes = 32768;
        static constexpr std::size_t MaximumDiagnosticBytes = 256;
        struct State
        {
            uint64_t tick = 0;
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

        SharedWorldScript(const ESM::Script& script, std::vector<ESM::Global> globals,
            std::string_view campaignContent, CredentialCrypto& crypto);
        State initial() const;
        std::vector<char> encode(const State& state) const;
        State restore(std::span<const char> image) const;
        Result transact(State& committed, uint64_t tick, const DurableSink& persist) const;

    private:
        MWScript::WorldScript mRuntime;
        CredentialDigest mIdentity;
    };
}
#endif
