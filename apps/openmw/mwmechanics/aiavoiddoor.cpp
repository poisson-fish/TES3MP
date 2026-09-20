#include "aiavoiddoor.hpp"

#include <components/misc/rng.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"

#include "actorutil.hpp"
#include "creaturestats.hpp"
#include "movement.hpp"
#include "steering.hpp"

MWMechanics::AiAvoidDoor::AiAvoidDoor(const MWWorld::ConstPtr& doorPtr)
    : mDoorPtr(doorPtr)
{
}

bool MWMechanics::AiAvoidDoor::execute(
    const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
{

    ESM::Position pos = actor.getRefData().getPosition();
    const auto angle = mAvoidance.update(pos.asVec3(), mDoorPtr.getRefData().getPosition().asVec3(),
        mDoorPtr.getClass().getDoorState(mDoorPtr) != MWWorld::DoorState::Idle, duration,
        MWBase::Environment::get().getWorld()->getPrng());
    if (!angle)
        return true;

    actor.getClass().getCreatureStats(actor).setMovementFlag(CreatureStats::Flag_Run, true);

    // Turn away from the door and move when turn completed
    if (zTurn(actor, *angle, osg::DegreesToRadians(5.f)))
        actor.getClass().getMovementSettings(actor).mPosition[1] = 1;
    else
        actor.getClass().getMovementSettings(actor).mPosition[1] = 0;
    actor.getClass().getMovementSettings(actor).mPosition[0] = 0;

    // Make all nearby actors also avoid the door
    std::vector<MWWorld::Ptr> actors;
    MWBase::Environment::get().getMechanicsManager()->getActorsInRange(pos.asVec3(), 100, actors);
    for (auto& neighbor : actors)
    {
        if (neighbor == getPlayer())
            continue;

        MWMechanics::AiSequence& seq = neighbor.getClass().getCreatureStats(neighbor).getAiSequence();
        if (seq.getTypeId() != MWMechanics::AiPackageTypeId::AvoidDoor)
            seq.stack(MWMechanics::AiAvoidDoor(mDoorPtr), neighbor);
    }

    return false;
}
