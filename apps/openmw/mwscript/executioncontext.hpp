#ifndef OPENMW_MWSCRIPT_EXECUTIONCONTEXT_H
#define OPENMW_MWSCRIPT_EXECUTIONCONTEXT_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "../mwworld/ptr.hpp"

namespace MWScript
{
    // Stable authority identities, never sessions or pointers. The caller owns
    // their persistence and supplies the same identities to nested invocations.
    struct ExecutionContext
    {
        static constexpr std::size_t MaximumReferenceNameBytes = 128;
        enum class Ownership { Shared, Character };
        Ownership ownership = Ownership::Shared;
        uint64_t campaign = 0;
        uint64_t story = 0;
        uint64_t physicalScope = 0;
        uint64_t character = 0;
        uint64_t sourceReference = 0;
        uint64_t sourceLife = 0;
        uint64_t invocation = 0;
        uint64_t causalEvent = 0; // Zero for invocations without an external event.
        bool operator==(const ExecutionContext&) const = default;

        void validate() const
        {
            if (!campaign || !physicalScope || !invocation
                || (sourceReference == 0) != (sourceLife == 0)
                || (ownership != Ownership::Shared && ownership != Ownership::Character)
                || (ownership == Ownership::Shared && (character || story))
                || (ownership == Ownership::Character && (!character || !story)))
                throw std::invalid_argument("Incomplete script execution context");
        }
    };

    class ScriptContextUnavailable : public std::runtime_error
    {
    public:
        ScriptContextUnavailable() : std::runtime_error("Script context dependency unavailable") {}
    };

    // A temporary, life-bound source dependency. It is not a death event or
    // quest receipt; the containing authority must persist it after rollback.
    class ScriptSourceWait : public ScriptContextUnavailable
    {
    public:
        uint64_t reference, life, respawnTick;
        ScriptSourceWait(uint64_t reference_, uint64_t life_, uint64_t respawnTick_)
            : reference(reference_), life(life_), respawnTick(respawnTick_) {}
    };

    class ReferenceResolver
    {
    public:
        virtual ~ReferenceResolver() = default;
        // Resolve in this physical scope and revalidate source identity/life.
        // Empty id means the invocation source; "player" means its character.
        // Return empty only for actual absence. Temporary unavailability throws
        // ScriptContextUnavailable, including optional lookups, so callers defer.
        virtual MWWorld::Ptr resolve(const ExecutionContext& execution,
            const ESM::RefId& id, bool activeOnly) const = 0;
    };
}

#endif
