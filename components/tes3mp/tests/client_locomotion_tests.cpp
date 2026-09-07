#include <tes3mp/client_locomotion.hpp>

#include <cstdlib>

namespace
{
    using namespace TES3MP;

    template <class Value>
    Value value(std::uint64_t raw) { return *Value::fromValue(raw); }

    void require(bool condition)
    {
        if (!condition)
            std::abort();
    }

    SpatialEntitySnapshot snapshot(CellId cell, std::uint64_t tick, Position3 position)
    {
        return SpatialEntitySnapshot(value<ServerTick>(tick), value<PlayerId>(1), value<EntityId>(2),
            value<AppearanceId>(3), value<EntityRevision>(4), value<AuthorityEpoch>(5),
            Transform(cell, position, Orientation3(Turn32::fromValue(0), Turn32::fromValue(0), Turn32::fromValue(0))),
            LinearVelocity3(0, 0, 0));
    }

    PlayerLocomotionInput input(std::uint64_t ordinal, LinearVelocity3 velocity, std::uint32_t facing = 0)
    {
        return PlayerLocomotionInput(*LocomotionInputTick::fromValue(ordinal),
            *LocomotionInputSequence::fromValue(ordinal),
            LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(facing), velocity));
    }
}

int main()
{
    const auto interior = CellId::interior(value<CellSpaceId>(1));
    ClientLocomotionHistory history;
    require(history.retain(value<CommandSequence>(1), input(1, LinearVelocity3(10, 0, 0), 7)));
    require(history.retain(value<CommandSequence>(2), input(2, LinearVelocity3(0, 20, 0), 8)));

    const auto baseline = snapshot(interior, 10, Position3(100, 200, 300));
    const auto replayed = history.reconcile(baseline, value<CommandSequence>(1));
    require(replayed.root.position() == Position3(100, 220, 300));
    require(replayed.root.orientation().z() == Turn32::fromValue(8));
    require(replayed.replayedInputCount == 1 && history.size() == 1);

    const auto repeated = history.reconcile(baseline, value<CommandSequence>(1));
    require(repeated == replayed && history.size() == 1);

    const auto exterior = CellId::exterior(value<CellSpaceId>(2), 0, 0);
    const auto discontinuity = history.reconcile(snapshot(exterior, 11, Position3(5, 6, 7)), std::nullopt);
    require(discontinuity.hardDiscontinuity && discontinuity.replayedInputCount == 0 && history.size() == 0);

    history.clear();
    for (std::size_t index = 0; index < MaximumRetainedLocomotionInputs; ++index)
    {
        const auto ordinal = static_cast<std::uint64_t>(index + 1);
        require(history.retain(value<CommandSequence>(ordinal), input(ordinal, LinearVelocity3(1, 0, 0))));
    }
    require(!history.retain(value<CommandSequence>(MaximumRetainedLocomotionInputs + 1),
        input(MaximumRetainedLocomotionInputs + 1, LinearVelocity3(1, 0, 0))));
    return 0;
}
