#ifndef OPENMW_MWSCRIPT_REFERENCESTATE_H
#define OPENMW_MWSCRIPT_REFERENCESTATE_H

#include "executioncontext.hpp"
#include <array>

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
        virtual bool onDeath(const ExecutionContext&, const MWWorld::Ptr&)
        { throw std::invalid_argument("OnDeath lacks attributed life-event admission"); }
        virtual float health(const ExecutionContext&, const MWWorld::Ptr&, bool) const
        { throw std::invalid_argument("Health read lacks staged actor admission"); }
        virtual const ESM::Position& position(const ExecutionContext&, const MWWorld::Ptr&) const
        { throw std::invalid_argument("Position read lacks relocation admission"); }
        virtual void position(const ExecutionContext&, const MWWorld::Ptr&, const std::array<float, 3>&,
            float, const ESM::RefId&)
        { throw std::invalid_argument("Position lacks relocation admission"); }
    };
}
#endif
