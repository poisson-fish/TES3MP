#include <tes3mp/interactive_object_world.hpp>

#include <algorithm>

namespace TES3MP
{
    namespace
    {
        bool stateMatchesCatalog(
            const CanonicalInteractiveObjectState& state, const InteractiveObjectCatalogEntry& entry) noexcept
        {
            return state.objectId() == entry.objectId && state.cell() == entry.cell
                && state.lockLevel() == entry.lock.lockLevel && state.keyId() == entry.lock.keyId
                && state.trapId() == entry.trap.trapId;
        }
    }

    const CanonicalInteractiveObjectState* CanonicalInteractiveObjectWorld::find(InteractiveObjectId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mObjects, id, {}, &CanonicalInteractiveObjectState::objectId);
        return found != mObjects.end() && found->objectId() == id ? &*found : nullptr;
    }

    CanonicalInteractiveObjectWorldResult createCanonicalInteractiveObjectWorld(
        std::span<const CanonicalInteractiveObjectState> objects)
    try
    {
        if (objects.size() > MaximumInteractiveObjectCatalogEntries)
            return CanonicalInteractiveObjectWorldError{ CanonicalInteractiveObjectWorldErrorCode::LimitExceeded,
                objects.size(), 0 };

        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            if (index != 0 && objects[index - 1].objectId() >= objects[index].objectId())
                return CanonicalInteractiveObjectWorldError{
                    CanonicalInteractiveObjectWorldErrorCode::ObjectIdsNotStrictlyOrdered, index,
                    objects[index].objectId().value(), objects[index - 1].objectId().value()
                };
        }

        return CanonicalInteractiveObjectWorld(
            std::vector<CanonicalInteractiveObjectState>(objects.begin(), objects.end()));
    }
    catch (...)
    {
        return CanonicalInteractiveObjectWorldError{ CanonicalInteractiveObjectWorldErrorCode::AllocationFailure, 0,
            0 };
    }

    CanonicalInteractiveObjectWorldResult createInitialCanonicalInteractiveObjectWorld(
        const InteractiveObjectCatalog& catalog)
    try
    {
        std::vector<CanonicalInteractiveObjectState> states;
        states.reserve(catalog.entries().size());

        for (const auto& entry : catalog.entries())
        {
            const auto doorState = DoorState::Closed;
            const auto lockState = entry.lock.lockedByDefault ? LockState::Locked : LockState::Unlocked;
            const auto trapState = entry.trap.trappedByDefault ? TrapState::Armed : TrapState::Disarmed;

            states.emplace_back(entry.objectId, entry.cell, doorState, lockState, entry.lock.lockLevel,
                entry.lock.keyId, trapState, entry.trap.trapId, ObjectRevision::initial(), ServerTick::initial());
        }

        return createCanonicalInteractiveObjectWorld(states);
    }
    catch (...)
    {
        return CanonicalInteractiveObjectWorldError{ CanonicalInteractiveObjectWorldErrorCode::AllocationFailure, 0,
            0 };
    }

    StagedObjectInteractionResult applyObjectInteractionToCandidate(CanonicalInteractiveObjectWorld& current,
        const InteractiveObjectCatalog& catalog, const CanonicalServerState& players,
        const InteractObjectCommand& command, ServerTick currentTick,
        ObjectInteractionValidationContext validation) noexcept
    try
    {
        ObjectInteractionOutcome outcome;
        outcome.objectId = command.objectId;

        const auto* playerState = players.findPlayer(command.player);
        if (!playerState)
        {
            outcome.code = ObjectInteractionResultCode::PlayerNotFound;
            return { outcome, false };
        }

        if (playerState->transform().cell() != command.cell)
        {
            outcome.code = ObjectInteractionResultCode::CellMismatch;
            return { outcome, false };
        }

        const auto* object = current.find(command.objectId);
        const auto* catalogEntry = catalog.find(command.objectId);
        if (!object || !catalogEntry)
        {
            outcome.code = ObjectInteractionResultCode::ObjectNotFound;
            return { outcome, false };
        }

        if (!stateMatchesCatalog(*object, *catalogEntry))
        {
            outcome.code = ObjectInteractionResultCode::CatalogMismatch;
            return { outcome, false };
        }

        if (object->cell() != command.cell)
        {
            outcome.code = ObjectInteractionResultCode::CellMismatch;
            return { outcome, false };
        }

        const auto playerPosition = playerState->transform().position();
        const auto objectPosition = catalogEntry->transform.position();
        if (!positionsWithinReach(playerPosition, objectPosition, validation.maxReach)
            || !positionsWithinReach(playerPosition, command.interactionOrigin, validation.maxReach)
            || !positionsWithinReach(command.interactionOrigin, objectPosition, validation.maxReach))
        {
            outcome.code = ObjectInteractionResultCode::PlayerOutOfReach;
            return { outcome, false };
        }

        if (command.expectedRevision != object->revision())
        {
            outcome.code = ObjectInteractionResultCode::StaleRevision;
            return { outcome, false };
        }

        if (currentTick < object->lastChangeTick())
        {
            outcome.code = ObjectInteractionResultCode::TickRegression;
            return { outcome, false };
        }

        if (command.kind == ObjectInteractionKind::UnlockWithKey)
        {
            if (object->lockState() == LockState::Locked)
            {
                if (object->keyId().has_value() && command.requestedKey.has_value()
                    && *object->keyId() == *command.requestedKey
                    && std::ranges::find(validation.verifiedPlayerKeys, *command.requestedKey)
                        != validation.verifiedPlayerKeys.end())
                {
                    const auto nextRevOpt = object->revision().next();
                    if (!nextRevOpt)
                    {
                        outcome.code = ObjectInteractionResultCode::RevisionExhausted;
                        return { outcome, false };
                    }
                    const auto nextRev = *nextRevOpt;
                    outcome.code = ObjectInteractionResultCode::Success;
                    outcome.newRevision = nextRev;
                    outcome.newDoorState = object->doorState();
                    outcome.newLockState = LockState::Unlocked;
                    outcome.newTrapState = TrapState::Disarmed;

                    auto found = std::ranges::lower_bound(
                        current.mObjects, command.objectId, {}, &CanonicalInteractiveObjectState::objectId);
                    const auto previous = *found;
                    *found = CanonicalInteractiveObjectState(found->objectId(), found->cell(), found->doorState(),
                        LockState::Unlocked, found->lockLevel(), found->keyId(), TrapState::Disarmed, found->trapId(),
                        nextRev, currentTick);
                    return { outcome, true, previous };
                }
                else
                {
                    outcome.code = ObjectInteractionResultCode::Locked;
                    return { outcome, false };
                }
            }
            else
            {
                outcome.code = ObjectInteractionResultCode::Success;
                outcome.newRevision = object->revision();
                outcome.newDoorState = object->doorState();
                outcome.newLockState = object->lockState();
                outcome.newTrapState = object->trapState();
                return { outcome, false };
            }
        }

        if (object->lockState() == LockState::Locked)
        {
            outcome.code = ObjectInteractionResultCode::Locked;
            return { outcome, false };
        }

        if (object->trapState() == TrapState::Armed)
        {
            const auto nextRevOpt = object->revision().next();
            if (!nextRevOpt)
            {
                outcome.code = ObjectInteractionResultCode::RevisionExhausted;
                return { outcome, false };
            }
            const auto nextRev = *nextRevOpt;
            outcome.code = ObjectInteractionResultCode::TrapSprung;
            outcome.newRevision = nextRev;
            outcome.newDoorState = object->doorState();
            outcome.newLockState = object->lockState();
            outcome.newTrapState = TrapState::Disarmed;
            outcome.sprungTrap = object->trapId();

            auto found = std::ranges::lower_bound(
                current.mObjects, command.objectId, {}, &CanonicalInteractiveObjectState::objectId);
            const auto previous = *found;
            *found = CanonicalInteractiveObjectState(found->objectId(), found->cell(), found->doorState(),
                found->lockState(), found->lockLevel(), found->keyId(), TrapState::Disarmed, found->trapId(), nextRev,
                currentTick);
            return { outcome, true, previous };
        }

        if (catalogEntry->kind == InteractiveObjectKind::TeleportDoor)
        {
            const auto nextRevOpt = object->revision().next();
            if (!nextRevOpt)
            {
                outcome.code = ObjectInteractionResultCode::RevisionExhausted;
                return { outcome, false };
            }
            const auto nextRev = *nextRevOpt;
            outcome.code = ObjectInteractionResultCode::Success;
            outcome.newRevision = nextRev;
            outcome.newDoorState = object->doorState();
            outcome.newLockState = object->lockState();
            outcome.newTrapState = object->trapState();
            outcome.playerTeleport = catalogEntry->destination;

            auto found = std::ranges::lower_bound(
                current.mObjects, command.objectId, {}, &CanonicalInteractiveObjectState::objectId);
            const auto previous = *found;
            *found = CanonicalInteractiveObjectState(found->objectId(), found->cell(), found->doorState(),
                found->lockState(), found->lockLevel(), found->keyId(), found->trapState(), found->trapId(), nextRev,
                currentTick);
            return { outcome, true, previous };
        }

        const auto nextRevOpt = object->revision().next();
        if (!nextRevOpt)
        {
            outcome.code = ObjectInteractionResultCode::RevisionExhausted;
            return { outcome, false };
        }
        const auto nextRev = *nextRevOpt;
        const auto nextDoorState = (object->doorState() == DoorState::Closed) ? DoorState::Open : DoorState::Closed;
        outcome.code = ObjectInteractionResultCode::Success;
        outcome.newRevision = nextRev;
        outcome.newDoorState = nextDoorState;
        outcome.newLockState = object->lockState();
        outcome.newTrapState = object->trapState();

        auto found = std::ranges::lower_bound(
            current.mObjects, command.objectId, {}, &CanonicalInteractiveObjectState::objectId);
        const auto previous = *found;
        *found = CanonicalInteractiveObjectState(found->objectId(), found->cell(), nextDoorState, found->lockState(),
            found->lockLevel(), found->keyId(), found->trapState(), found->trapId(), nextRev, currentTick);
        return { outcome, true, previous };
    }
    catch (...)
    {
        ObjectInteractionOutcome outcome;
        outcome.code = ObjectInteractionResultCode::InternalError;
        outcome.objectId = command.objectId;
        return { outcome, false };
    }

    bool restoreInteractiveObjectCandidate(
        CanonicalInteractiveObjectWorld& candidate, CanonicalInteractiveObjectState previous) noexcept
    {
        const auto found = std::ranges::lower_bound(
            candidate.mObjects, previous.objectId(), {}, &CanonicalInteractiveObjectState::objectId);
        if (found == candidate.mObjects.end() || found->objectId() != previous.objectId())
            return false;
        *found = std::move(previous);
        return true;
    }

    ObjectInteractionResult applyObjectInteraction(const CanonicalInteractiveObjectWorld& current,
        const InteractiveObjectCatalog& catalog, const CanonicalServerState& players,
        const InteractObjectCommand& command, ServerTick currentTick,
        ObjectInteractionValidationContext validation) noexcept
    try
    {
        CanonicalInteractiveObjectWorld candidate = current;
        auto staged = applyObjectInteractionToCandidate(candidate, catalog, players, command, currentTick, validation);
        return { std::move(staged.outcome),
            staged.worldChanged ? std::optional<CanonicalInteractiveObjectWorld>(std::move(candidate)) : std::nullopt };
    }
    catch (...)
    {
        ObjectInteractionOutcome outcome;
        outcome.code = ObjectInteractionResultCode::InternalError;
        outcome.objectId = command.objectId;
        return { outcome, std::nullopt };
    }
}
