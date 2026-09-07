#include <tes3mp/client_locomotion.hpp>

#include <algorithm>
#include <limits>

namespace TES3MP
{
    namespace
    {
        std::optional<std::int64_t> checkedAdd(std::int64_t left, std::int64_t right) noexcept
        {
            if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
                || (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
                return std::nullopt;
            return left + right;
        }
    }

    ClientLocomotionHistory::ClientLocomotionHistory()
    {
        mInputs.reserve(MaximumRetainedLocomotionInputs);
    }

    bool ClientLocomotionHistory::retain(
        CommandSequence commandSequence, PlayerLocomotionInput input) noexcept
    {
        if (mInputs.size() == MaximumRetainedLocomotionInputs
            || (!mInputs.empty()
                && (commandSequence <= mInputs.back().commandSequence
                    || input.inputSequence() <= mInputs.back().input.inputSequence()
                    || input.inputTick() < mInputs.back().input.inputTick())))
            return false;
        mInputs.push_back({ commandSequence, input });
        return true;
    }

    LocalLocomotionReconciliation ClientLocomotionHistory::reconcile(
        const SpatialEntitySnapshot& authoritative, std::optional<CommandSequence> acknowledgedCommand,
        bool hardDiscontinuity) noexcept
    {
        if (mBaseline
            && (mBaseline->player != authoritative.playerId() || mBaseline->entity != authoritative.entityId()
                || mBaseline->authorityEpoch != authoritative.authorityEpoch()
                || mBaseline->cell != authoritative.transform().cell()
                || authoritative.serverTick() < mBaseline->tick))
            hardDiscontinuity = true;

        mBaseline = BaselineIdentity{ authoritative.playerId(), authoritative.entityId(),
            authoritative.authorityEpoch(), authoritative.transform().cell(), authoritative.serverTick() };
        if (hardDiscontinuity)
            mInputs.clear();
        else if (acknowledgedCommand)
            mInputs.erase(mInputs.begin(), std::find_if(mInputs.begin(), mInputs.end(), [&](const auto& retained) {
                return retained.commandSequence > *acknowledgedCommand;
            }));

        Transform replayed = authoritative.transform();
        LinearVelocity3 velocity = authoritative.linearVelocity();
        std::size_t replayedCount = 0;
        for (const auto& retained : mInputs)
        {
            const auto desired = retained.input.intent().desiredVelocity();
            const auto position = replayed.position();
            const auto x = checkedAdd(position.x(), desired.x());
            const auto y = checkedAdd(position.y(), desired.y());
            const auto z = checkedAdd(position.z(), desired.z());
            if (!x || !y || !z)
            {
                mInputs.clear();
                return { authoritative.transform(), authoritative.linearVelocity(), 0, true };
            }
            const auto orientation = replayed.orientation();
            replayed = Transform(replayed.cell(), Position3(*x, *y, *z),
                Orientation3(orientation.x(), orientation.y(), retained.input.intent().rootFacing()));
            velocity = desired;
            ++replayedCount;
        }
        return { replayed, velocity, replayedCount, hardDiscontinuity };
    }

    void ClientLocomotionHistory::clear() noexcept
    {
        mInputs.clear();
        mBaseline.reset();
    }
}
