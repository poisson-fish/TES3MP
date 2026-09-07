#include <tes3mp/interactive_object_world.hpp>

#include <algorithm>

namespace TES3MP
{
    namespace
    {
        std::uint64_t orderedCoordinate(std::int64_t value) noexcept
        {
            constexpr std::uint64_t SignBit = std::uint64_t{ 1 } << 63u;
            return static_cast<std::uint64_t>(value) ^ SignBit;
        }

        std::uint64_t coordinateDistance(std::int64_t left, std::int64_t right) noexcept
        {
            const auto orderedLeft = orderedCoordinate(left);
            const auto orderedRight = orderedCoordinate(right);
            return orderedLeft >= orderedRight ? orderedLeft - orderedRight : orderedRight - orderedLeft;
        }

        bool withinReach(Position3 left, Position3 right, std::uint32_t maxReach) noexcept
        {
            const std::uint64_t limit = maxReach;
            const auto dx = coordinateDistance(left.x(), right.x());
            const auto dy = coordinateDistance(left.y(), right.y());
            const auto dz = coordinateDistance(left.z(), right.z());
            if (dx > limit || dy > limit || dz > limit)
                return false;

            std::uint64_t remainingSquared = limit * limit;
            const auto dxSquared = dx * dx;
            if (dxSquared > remainingSquared)
                return false;
            remainingSquared -= dxSquared;

            const auto dySquared = dy * dy;
            if (dySquared > remainingSquared)
                return false;
            remainingSquared -= dySquared;
            return dz * dz <= remainingSquared;
        }

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

    ObjectInteractionResult applyObjectInteraction(const CanonicalInteractiveObjectWorld& current,
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
            return { outcome, std::nullopt };
        }

        if (playerState->transform().cell() != command.cell)
        {
            outcome.code = ObjectInteractionResultCode::CellMismatch;
            return { outcome, std::nullopt };
        }

        const auto* object = current.find(command.objectId);
        const auto* catalogEntry = catalog.find(command.objectId);
        if (!object || !catalogEntry)
        {
            outcome.code = ObjectInteractionResultCode::ObjectNotFound;
            return { outcome, std::nullopt };
        }

        if (!stateMatchesCatalog(*object, *catalogEntry))
        {
            outcome.code = ObjectInteractionResultCode::CatalogMismatch;
            return { outcome, std::nullopt };
        }

        if (object->cell() != command.cell)
        {
            outcome.code = ObjectInteractionResultCode::CellMismatch;
            return { outcome, std::nullopt };
        }

        const auto playerPosition = playerState->transform().position();
        const auto objectPosition = catalogEntry->transform.position();
        if (!withinReach(playerPosition, objectPosition, validation.maxReach)
            || !withinReach(playerPosition, command.interactionOrigin, validation.maxReach)
            || !withinReach(command.interactionOrigin, objectPosition, validation.maxReach))
        {
            outcome.code = ObjectInteractionResultCode::PlayerOutOfReach;
            return { outcome, std::nullopt };
        }

        if (command.expectedRevision != object->revision())
        {
            outcome.code = ObjectInteractionResultCode::StaleRevision;
            return { outcome, std::nullopt };
        }

        if (currentTick < object->lastChangeTick())
        {
            outcome.code = ObjectInteractionResultCode::TickRegression;
            return { outcome, std::nullopt };
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
                        return { outcome, std::nullopt };
                    }
                    const auto nextRev = *nextRevOpt;
                    outcome.code = ObjectInteractionResultCode::Success;
                    outcome.newRevision = nextRev;
                    outcome.newDoorState = object->doorState();
                    outcome.newLockState = LockState::Unlocked;
                    outcome.newTrapState = TrapState::Disarmed;

                    std::vector<CanonicalInteractiveObjectState> updated(
                        current.objects().begin(), current.objects().end());
                    for (auto& obj : updated)
                    {
                        if (obj.objectId() == command.objectId)
                        {
                            obj = CanonicalInteractiveObjectState(obj.objectId(), obj.cell(), obj.doorState(),
                                LockState::Unlocked, obj.lockLevel(), obj.keyId(), TrapState::Disarmed, obj.trapId(),
                                nextRev, currentTick);
                            break;
                        }
                    }
                    auto rebuilt = createCanonicalInteractiveObjectWorld(updated);
                    if (auto* updatedWorld = std::get_if<CanonicalInteractiveObjectWorld>(&rebuilt))
                        return { outcome, std::move(*updatedWorld) };
                    outcome.code = ObjectInteractionResultCode::InternalError;
                    return { outcome, std::nullopt };
                }
                else
                {
                    outcome.code = ObjectInteractionResultCode::Locked;
                    return { outcome, std::nullopt };
                }
            }
            else
            {
                outcome.code = ObjectInteractionResultCode::Success;
                outcome.newRevision = object->revision();
                outcome.newDoorState = object->doorState();
                outcome.newLockState = object->lockState();
                outcome.newTrapState = object->trapState();
                return { outcome, std::nullopt };
            }
        }

        if (object->lockState() == LockState::Locked)
        {
            outcome.code = ObjectInteractionResultCode::Locked;
            return { outcome, std::nullopt };
        }

        if (object->trapState() == TrapState::Armed)
        {
            const auto nextRevOpt = object->revision().next();
            if (!nextRevOpt)
            {
                outcome.code = ObjectInteractionResultCode::RevisionExhausted;
                return { outcome, std::nullopt };
            }
            const auto nextRev = *nextRevOpt;
            outcome.code = ObjectInteractionResultCode::TrapSprung;
            outcome.newRevision = nextRev;
            outcome.newDoorState = object->doorState();
            outcome.newLockState = object->lockState();
            outcome.newTrapState = TrapState::Disarmed;
            outcome.sprungTrap = object->trapId();

            std::vector<CanonicalInteractiveObjectState> updated(current.objects().begin(), current.objects().end());
            for (auto& obj : updated)
            {
                if (obj.objectId() == command.objectId)
                {
                    obj = CanonicalInteractiveObjectState(obj.objectId(), obj.cell(), obj.doorState(), obj.lockState(),
                        obj.lockLevel(), obj.keyId(), TrapState::Disarmed, obj.trapId(), nextRev, currentTick);
                    break;
                }
            }
            auto rebuilt = createCanonicalInteractiveObjectWorld(updated);
            if (auto* updatedWorld = std::get_if<CanonicalInteractiveObjectWorld>(&rebuilt))
                return { outcome, std::move(*updatedWorld) };
            outcome.code = ObjectInteractionResultCode::InternalError;
            return { outcome, std::nullopt };
        }

        if (catalogEntry->kind == InteractiveObjectKind::TeleportDoor)
        {
            const auto nextRevOpt = object->revision().next();
            if (!nextRevOpt)
            {
                outcome.code = ObjectInteractionResultCode::RevisionExhausted;
                return { outcome, std::nullopt };
            }
            const auto nextRev = *nextRevOpt;
            outcome.code = ObjectInteractionResultCode::Success;
            outcome.newRevision = nextRev;
            outcome.newDoorState = object->doorState();
            outcome.newLockState = object->lockState();
            outcome.newTrapState = object->trapState();
            outcome.playerTeleport = catalogEntry->destination;

            std::vector<CanonicalInteractiveObjectState> updated(current.objects().begin(), current.objects().end());
            for (auto& obj : updated)
            {
                if (obj.objectId() == command.objectId)
                {
                    obj = CanonicalInteractiveObjectState(obj.objectId(), obj.cell(), obj.doorState(), obj.lockState(),
                        obj.lockLevel(), obj.keyId(), obj.trapState(), obj.trapId(), nextRev, currentTick);
                    break;
                }
            }
            auto rebuilt = createCanonicalInteractiveObjectWorld(updated);
            if (auto* updatedWorld = std::get_if<CanonicalInteractiveObjectWorld>(&rebuilt))
                return { outcome, std::move(*updatedWorld) };
            outcome.code = ObjectInteractionResultCode::InternalError;
            return { outcome, std::nullopt };
        }

        const auto nextRevOpt = object->revision().next();
        if (!nextRevOpt)
        {
            outcome.code = ObjectInteractionResultCode::RevisionExhausted;
            return { outcome, std::nullopt };
        }
        const auto nextRev = *nextRevOpt;
        const auto nextDoorState = (object->doorState() == DoorState::Closed) ? DoorState::Open : DoorState::Closed;
        outcome.code = ObjectInteractionResultCode::Success;
        outcome.newRevision = nextRev;
        outcome.newDoorState = nextDoorState;
        outcome.newLockState = object->lockState();
        outcome.newTrapState = object->trapState();

        std::vector<CanonicalInteractiveObjectState> updated(current.objects().begin(), current.objects().end());
        for (auto& obj : updated)
        {
            if (obj.objectId() == command.objectId)
            {
                obj = CanonicalInteractiveObjectState(obj.objectId(), obj.cell(), nextDoorState, obj.lockState(),
                    obj.lockLevel(), obj.keyId(), obj.trapState(), obj.trapId(), nextRev, currentTick);
                break;
            }
        }
        auto rebuilt = createCanonicalInteractiveObjectWorld(updated);
        if (auto* updatedWorld = std::get_if<CanonicalInteractiveObjectWorld>(&rebuilt))
            return { outcome, std::move(*updatedWorld) };
        outcome.code = ObjectInteractionResultCode::InternalError;
        return { outcome, std::nullopt };
    }
    catch (...)
    {
        ObjectInteractionOutcome outcome;
        outcome.code = ObjectInteractionResultCode::InternalError;
        outcome.objectId = command.objectId;
        return { outcome, std::nullopt };
    }
}
