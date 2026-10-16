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
        virtual void aiTravel(const ExecutionContext&, const MWWorld::Ptr&, const std::array<float, 3>&, bool,
            const ESM::RefId&, float)
        { throw std::invalid_argument("AI package lacks staged activity admission"); }
        virtual bool onDeath(const ExecutionContext&, const MWWorld::Ptr&)
        { throw std::invalid_argument("OnDeath lacks attributed life-event admission"); }
        virtual float health(const ExecutionContext&, const MWWorld::Ptr&, bool) const
        { throw std::invalid_argument("Health read lacks staged actor admission"); }
        virtual const ESM::Position& position(const ExecutionContext&, const MWWorld::Ptr&) const
        { throw std::invalid_argument("Position read lacks relocation admission"); }
        virtual void position(const ExecutionContext&, const MWWorld::Ptr&, const std::array<float, 3>&,
            float, const ESM::RefId&)
        { throw std::invalid_argument("Position lacks relocation admission"); }
        virtual bool cellChanged(const ExecutionContext&) const
        { throw std::invalid_argument("CellChanged lacks staged cell admission"); }
        virtual bool menuMode(const ExecutionContext&) const
        { throw std::invalid_argument("MenuMode lacks staged source admission"); }
        virtual bool playerSleeping(const ExecutionContext&) const
        { throw std::invalid_argument("GetPCSleep lacks staged source admission"); }
        virtual bool onActivate(const ExecutionContext&, const MWWorld::Ptr&)
        { throw std::invalid_argument("OnActivate lacks staged activation admission"); }
        virtual void activate(const ExecutionContext&, const MWWorld::Ptr&)
        { throw std::invalid_argument("Activate lacks staged activation admission"); }
        virtual int itemCount(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&) const
        { throw std::invalid_argument("GetItemCount lacks staged inventory admission"); }
        virtual void addItem(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&, int)
        { throw std::invalid_argument("AddItem lacks staged inventory admission"); }
        virtual void removeItem(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&, int)
        { throw std::invalid_argument("RemoveItem lacks staged inventory admission"); }
    };
}
#endif
