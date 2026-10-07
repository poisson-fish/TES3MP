#ifndef TES3MP_NATIVE_PERSONAL_SCRIPT_HPP
#define TES3MP_NATIVE_PERSONAL_SCRIPT_HPP

#include "world_script.hpp"
#include <apps/openmw/mwscript/executioncontext.hpp>
#include <memory>

namespace TES3MP::Native
{
    // One explicitly admitted core script per character/story. Cross-script
    // state and reference effects remain closed. Scheduled policies suspend
    // offline characters and execute once per admitted host tick without catch-up.
    class PersonalScripts
    {
    public:
        static constexpr size_t MaximumInstances = 16;
        static constexpr size_t MaximumInvocationsPerTransaction = 16;
        static constexpr size_t MaximumImageBytes = 528384;
        struct Definition
        {
            uint64_t character = 0, story = 0;
            std::shared_ptr<const SharedWorldScript> script;
        };
        struct Instance
        {
            MWScript::ExecutionContext last;
            uint64_t eventCursor = 0;
            SharedWorldScript::State runtime;
            uint64_t scheduledTick = 0;
        };
        using State = std::vector<Instance>;
        struct Prepared
        {
            State state;
            std::vector<char> image;
            SharedWorldScript::Result result;
            size_t invocations = 0;
        };
        PersonalScripts(uint64_t campaign, std::vector<Definition>, CredentialCrypto&, bool scheduled = false);
        uint64_t campaign() const { return mCampaign; }
        bool scheduled() const { return mScheduled; }
        State initial() const;
        std::vector<char> encode(const State&) const;
        State restore(std::span<const char>) const;
        Prepared prepare(const State&, const MWScript::ExecutionContext&, const MWWorld::ESMStore&,
            const MWScript::ReferenceResolver&) const;
        Prepared prepareScheduled(const State&, uint64_t worldTick, std::span<const uint64_t> activeCharacters,
            const MWWorld::ESMStore&, const MWScript::ReferenceResolver&) const;

    private:
        uint64_t mCampaign;
        std::vector<Definition> mDefinitions;
        CredentialDigest mIdentity;
        bool mScheduled;
        void validate(const Instance&, size_t) const;
    };
}
#endif
