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
        virtual void aiPackage(const ExecutionContext& context, const MWWorld::Ptr& ptr, int packageType,
            const std::array<float, 3>& destination, bool repeat, const ESM::RefId& targetActor, float duration,
            std::string_view cellName = {})
        {
            if ((packageType == 1 || packageType == 2) && cellName.empty())
            {
                aiTravel(context, ptr, destination, repeat, targetActor, duration);
                return;
            }
            throw std::invalid_argument("AI package lacks staged activity admission");
        }
        virtual bool aiPackageDone(const ExecutionContext&, const MWWorld::Ptr&) const
        { throw std::invalid_argument("GetAiPackageDone lacks staged activity admission"); }
        virtual int currentAiPackage(const ExecutionContext&, const MWWorld::Ptr&) const
        { throw std::invalid_argument("GetCurrentAIPackage lacks staged activity admission"); }
        virtual bool pcCell(const ExecutionContext&, std::string_view) const
        { throw std::invalid_argument("GetPCCell lacks staged cell admission"); }
        virtual float distance(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&) const
        { throw std::invalid_argument("GetDistance lacks staged position admission"); }
        virtual void forceGreeting(const ExecutionContext&, const MWWorld::Ptr&)
        { throw std::invalid_argument("ForceGreeting lacks staged dialogue admission"); }
        virtual void goodbye(const ExecutionContext&)
        { throw std::invalid_argument("Goodbye lacks staged dialogue admission"); }
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
        virtual bool hasSpell(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&) const
        { throw std::invalid_argument("GetSpell lacks staged spellbook admission"); }
        virtual void addSpell(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&)
        { throw std::invalid_argument("AddSpell lacks staged spellbook admission"); }
        virtual void removeSpell(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&)
        { throw std::invalid_argument("RemoveSpell lacks staged spellbook admission"); }
        virtual void startCombat(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&)
        { throw std::invalid_argument("StartCombat lacks staged combat admission"); }
        virtual void stopCombat(const ExecutionContext&, const MWWorld::Ptr&)
        { throw std::invalid_argument("StopCombat lacks staged combat admission"); }
        virtual void setAiSetting(const ExecutionContext&, const MWWorld::Ptr&, int, int)
        { throw std::invalid_argument("AI setting write lacks staged combat admission"); }
        virtual void cast(const ExecutionContext&, const MWWorld::Ptr&, const ESM::RefId&, const ESM::RefId&)
        { throw std::invalid_argument("Cast lacks staged spellcast admission"); }
    };
}
#endif
