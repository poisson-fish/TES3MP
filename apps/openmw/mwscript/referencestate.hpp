#ifndef OPENMW_MWSCRIPT_REFERENCESTATE_H
#define OPENMW_MWSCRIPT_REFERENCESTATE_H

#include "executioncontext.hpp"

namespace MWScript
{
    // A transaction-owned physical writer. Missing services must never fall
    // through to World. The authority validates placement, life and scope.
    class ReferenceState
    {
    public:
        virtual ~ReferenceState() = default;
        virtual void enable(const ExecutionContext&, const MWWorld::Ptr&) = 0;
        virtual void disable(const ExecutionContext&, const MWWorld::Ptr&) = 0;
        virtual bool disabled(const ExecutionContext&, const MWWorld::Ptr&) const = 0;
    };
}
#endif
