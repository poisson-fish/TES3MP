#include <tes3mp/server_scripting.hpp>

#include <algorithm>
#include <limits>
#include <tuple>

namespace TES3MP
{
    CanonicalSinkDeliveryResult DeterministicServerScriptRuntime::terminate(CanonicalSinkDeliveryResult result) noexcept
    {
        mPending.clear();
        mTerminated = true;
        return result;
    }

    std::optional<ServerScriptPackage> ServerScriptPackage::create(std::uint64_t packageId,
        std::uint32_t packageVersion, std::uint32_t loadOrder, std::uint32_t apiVersion) noexcept
    {
        if (packageId == 0 || packageVersion == 0 || apiVersion != ServerScriptApiVersion)
            return std::nullopt;
        return ServerScriptPackage(packageId, packageVersion, loadOrder, apiVersion);
    }

    ServerScriptEmitResult ServerScriptCommandEmitter::enqueue(ServerScriptPlayerSafePointCommand command) noexcept
    {
        return enqueuePayload(ServerScriptCommandPayload(std::move(command)));
    }

    ServerScriptEmitResult ServerScriptCommandEmitter::enqueue(ServerScriptSetGlobalCommand command) noexcept
    {
        return enqueuePayload(ServerScriptCommandPayload(std::move(command)));
    }

    ServerScriptEmitResult ServerScriptCommandEmitter::enqueue(ServerScriptSetWorldTimeCommand command) noexcept
    {
        return enqueuePayload(ServerScriptCommandPayload(std::move(command)));
    }

    ServerScriptEmitResult ServerScriptCommandEmitter::enqueuePayload(ServerScriptCommandPayload command) noexcept
    try
    {
        if (mEmitted == MaximumServerScriptCommandsPerCallback)
            return mResult = ServerScriptEmitResult::PerCallbackLimit;
        if (mCommands.size() == MaximumServerScriptCommandsPerPublication)
            return mResult = ServerScriptEmitResult::PerPublicationLimit;
        ++mEmitted;
        mCommands.push_back(QueuedServerScriptCommand(
            ServerScriptCommandOrder(mEligibleTick, mPublicationOrdinal, mEventOrdinal, mPackage.loadOrder(),
                mPackage.packageId(), mPackage.packageVersion(), mPackage.apiVersion(), mCallbackOrder, mEmitted),
            std::move(command)));
        return ServerScriptEmitResult::Accepted;
    }
    catch (...)
    {
        return mResult = ServerScriptEmitResult::PerPublicationLimit;
    }

    ServerScriptRegistrationResult DeterministicServerScriptRuntime::registerCallback(ServerScriptPackage package,
        std::uint32_t callbackOrder, ServerScriptEventKind eventKind, ServerScriptCallback& callback) noexcept
    try
    {
        if (mStarted)
            return ServerScriptRegistrationResult::RuntimeStarted;
        if (mCallbacks.size() == MaximumServerScriptCallbacks)
            return ServerScriptRegistrationResult::CallbackLimit;
        if (std::ranges::any_of(mCallbacks, [&](const Registration& value) {
                return value.package.packageId() == package.packageId() && value.package != package;
            }))
            return ServerScriptRegistrationResult::PackageConflict;
        const Registration registration{ package, callbackOrder, eventKind, &callback };
        const auto key = [](const Registration& value) {
            return std::tuple(value.package.loadOrder(), value.package.packageId(), value.callbackOrder,
                static_cast<std::uint8_t>(value.eventKind));
        };
        if (std::ranges::any_of(mCallbacks, [&](const Registration& value) { return key(value) == key(registration); }))
            return ServerScriptRegistrationResult::DuplicateRegistration;
        mCallbacks.push_back(registration);
        std::ranges::sort(
            mCallbacks, [&](const Registration& left, const Registration& right) { return key(left) < key(right); });
        return ServerScriptRegistrationResult::Accepted;
    }
    catch (...)
    {
        return ServerScriptRegistrationResult::CallbackLimit;
    }

    CanonicalSinkDeliveryResult DeterministicServerScriptRuntime::tryConsume(
        const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept
    try
    {
        mStarted = true;
        if (mTerminated)
            return CanonicalSinkDeliveryResult::Failed;
        if (!publication || mNextPublicationOrdinal == std::numeric_limits<std::uint64_t>::max())
            return terminate(CanonicalSinkDeliveryResult::Failed);

        std::size_t eventCount = 0;
        const auto addEvents = [&](std::size_t count) {
            if (count > MaximumServerScriptEventsPerPublication - eventCount)
                return false;
            eventCount += count;
            return true;
        };
        if (!addEvents(publication->changes().size()) || !addEvents(publication->joinedSessions().size())
            || !addEvents(publication->spatialTicks().size()) || !addEvents(publication->sessionLifecycle().size()))
            return terminate(CanonicalSinkDeliveryResult::Backpressured);
        const std::uint64_t publicationOrdinal = mNextPublicationOrdinal++;
        if (eventCount == 0 || mCallbacks.empty())
            return CanonicalSinkDeliveryResult::Accepted;
        const auto eligibleTick = publication->checkpointTick().next();
        if (!eligibleTick || (mLastPumpedTick && *eligibleTick <= *mLastPumpedTick))
            return terminate(CanonicalSinkDeliveryResult::Failed);

        std::vector<ServerScriptEvent> events;
        events.reserve(eventCount);
        for (const auto& change : publication->changes())
        {
            ServerScriptEvent event;
            event.mKind = ServerScriptEventKind::CommandFinalized;
            event.mStateVersion = change.stateVersion();
            event.mCommitTick = change.commitTick();
            event.mWriterStamp = change.stamp();
            event.mSessionId = change.sessionId();
            event.mSessionGeneration = change.sessionGeneration();
            event.mPlayerId = change.sessionReplacement().playerId();
            event.mCommandSequence = change.commandSequence();
            event.mCommandId = change.commandId();
            event.mCommandDisposition = change.disposition();
            event.mObjectInteractionOutcome = change.objectInteractionOutcome();
            event.mPlayerState = change.playerReplacement();
            events.push_back(std::move(event));
        }
        for (const auto& joined : publication->joinedSessions())
        {
            ServerScriptEvent event;
            event.mKind = ServerScriptEventKind::SessionJoined;
            event.mStateVersion = joined.stateVersion;
            event.mCommitTick = joined.commitTick;
            event.mSessionId = joined.session.sessionId();
            event.mSessionGeneration = joined.session.sessionGeneration();
            event.mPlayerId = joined.player.playerId();
            event.mPlayerState = joined.player;
            events.push_back(std::move(event));
        }
        for (const auto& spatial : publication->spatialTicks())
        {
            ServerScriptEvent event;
            event.mKind = ServerScriptEventKind::SpatialStateChanged;
            event.mStateVersion = spatial.stateVersion;
            event.mCommitTick = spatial.commitTick;
            event.mPlayerId = spatial.player.playerId();
            event.mPlayerState = spatial.player;
            events.push_back(std::move(event));
        }
        for (const auto& lifecycle : publication->sessionLifecycle())
        {
            ServerScriptEvent event;
            event.mKind = ServerScriptEventKind::SessionLifecycle;
            event.mStateVersion = lifecycle.stateVersion;
            event.mCommitTick = lifecycle.commitTick;
            event.mSessionId = lifecycle.session;
            event.mSessionGeneration = lifecycle.generation;
            event.mPlayerId = lifecycle.player;
            event.mLifecycleKind = lifecycle.kind;
            events.push_back(std::move(event));
        }

        std::vector<QueuedServerScriptCommand> staged;
        staged.reserve(std::min(MaximumServerScriptCommandsPerPublication,
            events.size() * std::min(mCallbacks.size(), MaximumServerScriptCommandsPerCallback)));
        for (std::size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex)
        {
            const auto ordinal = static_cast<std::uint32_t>(eventIndex + 1);
            const ServerScriptCallbackInput input(publicationOrdinal, ordinal, events[eventIndex]);
            for (const Registration& registration : mCallbacks)
            {
                if (registration.eventKind != input.event().kind())
                    continue;
                ServerScriptCommandEmitter emitter(staged, *eligibleTick, publicationOrdinal, ordinal,
                    registration.package, registration.callbackOrder);
                if (registration.callback->onEvent(input, emitter) != ServerScriptCallbackResult::Accepted)
                    return terminate(CanonicalSinkDeliveryResult::Failed);
                if (emitter.mResult != ServerScriptEmitResult::Accepted)
                    return terminate(CanonicalSinkDeliveryResult::Backpressured);
            }
        }
        if (staged.size() > MaximumPendingServerScriptCommands - mPending.size())
            return terminate(CanonicalSinkDeliveryResult::Backpressured);
        mPending.insert(mPending.end(), std::make_move_iterator(staged.begin()), std::make_move_iterator(staged.end()));
        std::ranges::sort(mPending, [](const QueuedServerScriptCommand& left, const QueuedServerScriptCommand& right) {
            return left.order() < right.order();
        });
        return CanonicalSinkDeliveryResult::Accepted;
    }
    catch (...)
    {
        return terminate(CanonicalSinkDeliveryResult::Failed);
    }

    ServerScriptPumpResult DeterministicServerScriptRuntime::pump(ServerTick tick) noexcept
    try
    {
        ServerScriptPumpResult result;
        if (mTerminated)
        {
            result.mError = ServerScriptPumpError::RuntimeTerminated;
            return result;
        }
        if (mLastPumpedTick && tick <= *mLastPumpedTick)
        {
            result.mError = ServerScriptPumpError::TickNotStrictlyIncreasing;
            return result;
        }
        if (!mPending.empty() && mPending.front().order().eligibleTick() < tick)
        {
            result.mError = ServerScriptPumpError::MissedEligibleTick;
            return result;
        }
        const auto end = std::ranges::upper_bound(
            mPending, tick, {}, [](const QueuedServerScriptCommand& value) { return value.order().eligibleTick(); });
        const auto count = static_cast<std::size_t>(end - mPending.begin());
        if (count > MaximumServerScriptCommandsPerTick)
        {
            result.mError = ServerScriptPumpError::MissedEligibleTick;
            return result;
        }
        result.mCommands.insert(
            result.mCommands.end(), std::make_move_iterator(mPending.begin()), std::make_move_iterator(end));
        mPending.erase(mPending.begin(), end);
        mLastPumpedTick = tick;
        return result;
    }
    catch (...)
    {
        ServerScriptPumpResult result;
        result.mError = ServerScriptPumpError::MissedEligibleTick;
        return result;
    }
}
