#include "desktop_automation.hpp"

#include "engine_coordinator.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"

#include <limits>
#include <ranges>
#include <thread>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        CellId firstCell(const ContentManifest& manifest, CellId::Kind kind)
        {
            return *std::ranges::find_if(manifest.cells(), [&](const CellId& cell) { return cell.kind() == kind; });
        }
    }
    namespace
    {
        constexpr std::uint64_t Second = 1'000'000'000;
        constexpr std::uint64_t ReconnectCadence = 1'050'000'000;
        constexpr std::uint64_t FlowDuration = 8 * Second;
        constexpr std::uint64_t SoakDuration = 60 * Second;
        constexpr std::uint64_t CaptureStopAt = 4 * Second;
        constexpr std::uint64_t CaptureDuration = 5 * Second;
        constexpr std::uint64_t ActorAuthDuration = 8 * Second;
        constexpr std::uint64_t SlowPeerEvidenceDuration = 20 * Second;
        constexpr std::int64_t AutomationSpeed = 4096;
        std::optional<MonotonicInstant> add(MonotonicInstant value, std::uint64_t duration) noexcept
        {
            if (duration > std::numeric_limits<std::uint64_t>::max() - value.nanoseconds())
                return std::nullopt;
            return MonotonicInstant::fromNanoseconds(value.nanoseconds() + duration);
        }

        const char* statusName(ConnectionStatus status) noexcept
        {
            switch (status)
            {
                case ConnectionStatus::ProtocolRejected:
                    return "protocol_rejected";
                case ConnectionStatus::ProtocolVersionMismatch:
                    return "protocol_version_mismatch";
                case ConnectionStatus::RequiredCapabilityMissing:
                    return "required_capability_missing";
                case ConnectionStatus::ContentManifestMismatch:
                    return "content_manifest_mismatch";
                case ConnectionStatus::AuthenticationRejected:
                    return "authentication_rejected";
                case ConnectionStatus::AuthenticationUnavailable:
                    return "authentication_unavailable";
                case ConnectionStatus::TimedOut:
                    return "timed_out";
                case ConnectionStatus::TransportFailed:
                    return "transport_failed";
                case ConnectionStatus::Disconnected:
                    return "disconnected";
                case ConnectionStatus::Reconnecting:
                    return "reconnecting";
                case ConnectionStatus::Resumed:
                    return "resumed";
                case ConnectionStatus::ResumeFailed:
                    return "resume_failed";
                case ConnectionStatus::ContentMappingFailed:
                    return "content_mapping_failed";
                case ConnectionStatus::PresentationFailed:
                    return "presentation_failed";
            }
            return "unknown";
        }
    }

    std::optional<DesktopAutomationRole> parseDesktopAutomationRole(std::string_view value) noexcept
    {
        if (value == "flow-one")
            return DesktopAutomationRole::FlowOne;
        if (value == "flow-two")
            return DesktopAutomationRole::FlowTwo;
        if (value == "reconnect")
            return DesktopAutomationRole::Reconnect;
        if (value == "soak-one")
            return DesktopAutomationRole::SoakOne;
        if (value == "soak-two")
            return DesktopAutomationRole::SoakTwo;
        if (value == "capture-one")
            return DesktopAutomationRole::CaptureOne;
        if (value == "capture-two")
            return DesktopAutomationRole::CaptureTwo;
        if (value == "actor-reconnect")
            return DesktopAutomationRole::ActorReconnect;
        if (value == "actor-auth")
            return DesktopAutomationRole::ActorAuth;
        if (value == "weather-one")
            return DesktopAutomationRole::WeatherOne;
        if (value == "weather-two")
            return DesktopAutomationRole::WeatherTwo;
        if (value == "weather-reconnect")
            return DesktopAutomationRole::WeatherReconnect;
        if (value == "weather-slow")
            return DesktopAutomationRole::WeatherSlow;
        if (value == "wait-one")
            return DesktopAutomationRole::WaitOne;
        if (value == "wait-two")
            return DesktopAutomationRole::WaitTwo;
        if (value == "wait-anchor")
            return DesktopAutomationRole::WaitAnchor;
        if (value == "wait-reconnect")
            return DesktopAutomationRole::WaitReconnect;
        if (value == "wait-slow-anchor")
            return DesktopAutomationRole::WaitSlowAnchor;
        if (value == "wait-slow")
            return DesktopAutomationRole::WaitSlow;
        return std::nullopt;
    }

    DesktopAutomation::DesktopAutomation(DesktopAutomationRole role, const std::filesystem::path& output,
        ContentManifest contentManifest, DesktopPresentation& presentation, ConnectionStatusProvider& status)
        : mRole(role)
        , mInterior(firstCell(contentManifest, CellId::Kind::Interior))
        , mExterior(firstCell(contentManifest, CellId::Kind::Exterior))
        , mOutput(output, std::ios::out | std::ios::trunc)
        , mPresentation(presentation)
        , mStatus(status)
    {
        if (mOutput)
        {
            mOutput << "{\"event\":\"phase8_desktop_started\",\"role\":\"" << roleName() << "\"}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
    }

    CellTransitionCapture DesktopAutomation::captureCellTransition() noexcept
    {
        if (mRole != DesktopAutomationRole::FlowOne || !mNow || !mStartedAt || !mSelfCell)
            return {};
        const auto elapsed = mNow->nanoseconds() - mStartedAt->nanoseconds();
        if (!mSentExterior && mSawPeer && elapsed >= Second)
        {
            mSentExterior = true;
            return { ProviderResult::Accepted, CellTransition(mExterior) };
        }
        if (mSentExterior && !mSentInterior && *mSelfCell == mExterior && mExteriorAt
            && mNow->nanoseconds() - mExteriorAt->nanoseconds() >= Second)
        {
            mSentInterior = true;
            return { ProviderResult::Accepted, CellTransition(mInterior) };
        }
        return {};
    }

    std::optional<LocomotionIntent> DesktopAutomation::sampleCurrentIntent() noexcept
    {
        if (!mStartedAt || !mNow || mRole == DesktopAutomationRole::Reconnect
            || mRole == DesktopAutomationRole::ActorReconnect || mRole == DesktopAutomationRole::ActorAuth)
            return LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(0, 0, 0));
        const auto elapsed = mNow->nanoseconds() - mStartedAt->nanoseconds();
        if ((mRole == DesktopAutomationRole::CaptureOne || mRole == DesktopAutomationRole::CaptureTwo)
            && elapsed < CaptureStopAt)
        {
            const bool turned = elapsed >= 2 * Second;
            if (mRole == DesktopAutomationRole::CaptureOne)
                return LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(turned ? 0x40000000u : 0),
                    LinearVelocity3(turned ? 0 : AutomationSpeed, turned ? AutomationSpeed : 0, 0));
            return LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(turned ? 0xc0000000u : 0),
                LinearVelocity3(turned ? -AutomationSpeed : 0, turned ? 0 : AutomationSpeed, 0));
        }
        const auto moving = elapsed < 2 * Second;
        const auto x = (mRole == DesktopAutomationRole::FlowOne || mRole == DesktopAutomationRole::SoakOne) && moving
            ? AutomationSpeed
            : 0;
        const auto y = (mRole == DesktopAutomationRole::FlowTwo || mRole == DesktopAutomationRole::SoakTwo) && moving
            ? AutomationSpeed
            : 0;
        return LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(x, y, 0));
    }

    ProviderResult DesktopAutomation::applyAuthoritative(const LatestWinsSnapshot& snapshot,
        std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection, MonotonicInstant receivedAt,
        const std::optional<LocalLocomotionReconciliation>& localReconciliation) noexcept
    {
        const auto applied = mPresentation.applyAuthoritative(
            snapshot, observedPlayers, allowLocalCellCorrection, receivedAt, localReconciliation);
        if (applied != ProviderResult::Accepted)
            return applied;
        ++mSnapshots;
        const auto self = std::ranges::find_if(snapshot.view().entries(), [&](const auto& entry) {
            return entry.playerId() == snapshot.header().targetPlayerId()
                && entry.entityId() == snapshot.header().targetEntityId();
        });
        if (self == snapshot.view().entries().end())
            return ProviderResult::PresentationFailed;
        if (!mPlayerId)
        {
            mPlayerId = snapshot.header().targetPlayerId();
            mPlayerEntityId = snapshot.header().targetEntityId();
        }
        else if (*mPlayerId != snapshot.header().targetPlayerId()
            || *mPlayerEntityId != snapshot.header().targetEntityId())
            mPlayerIdentityStable = false;
        if (!mInitialPosition)
            mInitialPosition = self->transform().position();
        else if (self->transform().position() != *mInitialPosition)
            mMoved = true;
        mSelfCell = self->transform().cell();
        if (*mSelfCell == mExterior && !mExteriorAt)
            mExteriorAt = receivedAt;
        const bool hasPeer = std::ranges::any_of(observedPlayers, [&](const ObservedPlayer& observed) {
            return observed.playerId != snapshot.header().targetPlayerId()
                || observed.entityId != snapshot.header().targetEntityId();
        });
        if (hasPeer)
        {
            if (mSawLeave)
                mSawReturn = true;
            mSawPeer = true;
        }
        else if (mSawPeer)
            mSawLeave = true;
        if (mRole == DesktopAutomationRole::WaitReconnect && mWorldTimeRevisionBeforeDisconnect && mSawPeer
            && mResumes == 0 && !mNextDisconnect)
        {
            mReadyToDisconnect = true;
            mNextDisconnect = receivedAt;
        }
        if (mAwaitingResumeSnapshot)
        {
            mAwaitingResumeSnapshot = false;
            mResumeSnapshot = true;
            mReadyToDisconnect = true;
        }
        else if ((mRole == DesktopAutomationRole::Reconnect || mRole == DesktopAutomationRole::ActorReconnect)
            && mSnapshots == 1)
            mReadyToDisconnect = true;
        return ProviderResult::Accepted;
    }

    ProviderResult DesktopAutomation::applyActors(const LatestWinsActorSnapshot& snapshot,
        std::span<const ActorInterestMember> observedActors, MonotonicInstant receivedAt) noexcept
    {
        const auto applied = mPresentation.applyActors(snapshot, observedActors, receivedAt);
        if (applied != ProviderResult::Accepted)
            return applied;
        ++mActorSnapshots;
        if (observedActors.empty())
        {
            if (mLastActor)
                mActorSawLeave = true;
            return ProviderResult::Accepted;
        }
        if (observedActors.size() != 1 || snapshot.view().entries().size() != 1)
            return ProviderResult::PresentationFailed;
        const auto& actor = snapshot.view().entries().front();
        const auto& observed = observedActors.front();
        if (actor.actorId() != observed.actorId || actor.entityId() != observed.entityId
            || actor.prototypeId() != observed.prototypeId)
            return ProviderResult::PresentationFailed;
        if (!mFirstActor)
            mFirstActor = actor;
        else if (actor.actorId() != mFirstActor->actorId() || actor.entityId() != mFirstActor->entityId()
            || actor.prototypeId() != mFirstActor->prototypeId()
            || actor.authorityEpoch() != mFirstActor->authorityEpoch())
            mActorStable = false;
        if (mLastActor)
        {
            if (actor.entityRevision() < mLastActor->entityRevision()
                || (actor.entityRevision() == mLastActor->entityRevision() && actor != *mLastActor))
                mActorStable = false;
            if (actor.transform().position() != mLastActor->transform().position())
                mActorMoved = true;
            if (mActorSawLeave)
                mActorSawReturn = true;
        }
        if (!mLastActor || actor.entityRevision() != mLastActor->entityRevision())
            writeActorSample(actor);
        mLastActor = actor;
        if (mAwaitingActorResumeSnapshot)
        {
            mAwaitingActorResumeSnapshot = false;
            ++mActorResumeSnapshots;
        }
        if (mResyncIssued)
            mActorAppliedAfterResync = true;
        if (mRole == DesktopAutomationRole::ActorAuth && mSnapshots != 0)
            mResyncReady = true;
        return ProviderResult::Accepted;
    }

    ProviderResult DesktopAutomation::advance(MonotonicInstant now) noexcept
    {
        mNow = now;
        if (!mStartedAt)
            mStartedAt = now;
        const auto applied = mPresentation.advance(now);
        if (applied != ProviderResult::Accepted)
            return applied;
        if (mFinished)
            return ProviderResult::Accepted;
        const auto elapsed = now.nanoseconds() - mStartedAt->nanoseconds();
        const bool submitsWaitRest = mRole == DesktopAutomationRole::WaitOne
            || mRole == DesktopAutomationRole::WaitTwo || mRole == DesktopAutomationRole::WaitAnchor
            || mRole == DesktopAutomationRole::WaitSlowAnchor || mRole == DesktopAutomationRole::WaitSlow;
        if (submitsWaitRest && !mWaitRestSubmitted && mLastWorldTime && elapsed >= 5 * Second)
        {
            const bool accepted = mCoordinator && mCoordinator->submitWaitRest(2, WaitRestMode::Rest);
            if (accepted)
            {
                mWaitRestSubmitted = true;
                if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
                {
                    mOutput << "{\"event\":\"wait_rest_submitted\",\"role\":\"" << roleName()
                            << "\",\"hours\":2,\"mode\":\"rest\",\"accepted\":true}\n";
                    mOutput.flush();
                    ++mEvidenceEvents;
                }
            }
        }
        if (mRole == DesktopAutomationRole::Reconnect)
        {
            if (mReadyToDisconnect && !mNextDisconnect)
                mNextDisconnect = add(now, ReconnectCadence);
            if (mResumes == 32 && mResumeSnapshot)
                finish(true);
        }
        else if (mRole == DesktopAutomationRole::ActorReconnect)
        {
            if (mReadyToDisconnect && !mNextDisconnect)
                mNextDisconnect = add(now, ReconnectCadence);
            if (mResumes == 4 && mResumeSnapshot && mActorResumeSnapshots == 4)
                mResyncReady = true;
            if (mResyncDone)
                finish(mPlayerIdentityStable && mActorStable && mLastActor && mActorAppliedAfterResync);
        }
        else if (mRole == DesktopAutomationRole::ActorAuth)
        {
            if (mResyncDone)
                finish(mPlayerIdentityStable && mActorStable && mLastActor && mActorAppliedAfterResync);
            else if (elapsed >= ActorAuthDuration)
                finish(false);
        }
        else if ((mRole == DesktopAutomationRole::SoakOne || mRole == DesktopAutomationRole::SoakTwo)
            && elapsed >= SoakDuration)
            finish(mSawPeer && mMoved);
        else if ((mRole == DesktopAutomationRole::FlowOne || mRole == DesktopAutomationRole::FlowTwo)
            && elapsed >= FlowDuration)
        {
            const bool cellFlow = mRole == DesktopAutomationRole::FlowOne
                ? mSentExterior && mSentInterior && mSawLeave && mSawReturn
                : mSawLeave && mSawReturn;
            finish(mMoved && mSawPeer && cellFlow);
        }
        else if ((mRole == DesktopAutomationRole::CaptureOne || mRole == DesktopAutomationRole::CaptureTwo)
            && elapsed >= CaptureDuration)
            finish(mMoved && mSawPeer);
        else if ((mRole == DesktopAutomationRole::WeatherOne || mRole == DesktopAutomationRole::WeatherTwo)
            && mSawWeatherTransition && mSawWeatherCompletion)
            finish(mWeatherPresentations >= 2 && mWeatherDuplicates == 0);
        else if (mRole == DesktopAutomationRole::WeatherReconnect && mWeatherConvergedAfterResume)
            finish(mWeatherDuplicates == 0);
        else if (mRole == DesktopAutomationRole::WeatherSlow && mSlowPeerRecovered)
            finish(mWeatherDuplicates == 0);
        else if ((mRole == DesktopAutomationRole::WaitOne || mRole == DesktopAutomationRole::WaitTwo
                     || mRole == DesktopAutomationRole::WaitAnchor)
            && mWaitRestApplied)
            finish(mWorldTimeDuplicates == 0);
        else if (mRole == DesktopAutomationRole::WaitReconnect && mWorldTimeConvergedAfterResume)
            finish(mWorldTimeDuplicates == 0);
        else if (mRole == DesktopAutomationRole::WaitSlowAnchor && mWaitRestApplied
            && elapsed >= SlowPeerEvidenceDuration)
            finish(mWorldTimeDuplicates == 0);
        else if (mRole == DesktopAutomationRole::WaitSlow && mSlowPeerRecovered && mWaitRestApplied
            && elapsed >= SlowPeerEvidenceDuration)
            finish(mWorldTimeDuplicates == 0);
        return ProviderResult::Accepted;
    }

    ProviderResult DesktopAutomation::applyInteractiveObjects(
        const ReliableInteractiveObjectInterestBaseline& baseline, MonotonicInstant receivedAt) noexcept
    {
        return mPresentation.applyInteractiveObjects(baseline, receivedAt);
    }

    ProviderResult DesktopAutomation::applyInventory(const ReliablePlayerInventoryBaseline& player,
        std::span<const ReliableContainerInventoryBaseline> containers, const ReliableGroundItemBaseline& groundItems,
        const LatestWinsEquipmentSnapshot& equipment, MonotonicInstant receivedAt) noexcept
    {
        return mPresentation.applyInventory(player, containers, groundItems, equipment, receivedAt);
    }

    ProviderResult DesktopAutomation::applyWeather(std::span<const WeatherRegionSnapshot> regions,
        ServerTick serverTick, MonotonicInstant receivedAt) noexcept
    {
        const auto applied = mPresentation.applyWeather(regions, serverTick, receivedAt);
        if (applied != ProviderResult::Accepted)
            return applied;
        ++mWeatherPresentations;
        if (mLastWeatherTick && *mLastWeatherTick == serverTick && std::ranges::equal(mLastWeather, regions))
            ++mWeatherDuplicates;
        else
            writeWeatherSample(regions, serverTick);
        mLastWeather.assign(regions.begin(), regions.end());
        mLastWeatherTick = serverTick;

        const bool transitioning = std::ranges::any_of(regions, [serverTick](const auto& region) {
            return region.currentWeather != region.targetWeather && serverTick >= region.transitionStartTick
                && serverTick < region.transitionEndTick;
        });
        const bool completed = std::ranges::all_of(regions, [](const auto& region) {
            return region.currentWeather == region.targetWeather;
        });
        mSawWeatherTransition = mSawWeatherTransition || transitioning;
        mSawWeatherCompletion = mSawWeatherCompletion || (mSawWeatherTransition && completed);

        if (mRole == DesktopAutomationRole::WeatherReconnect && transitioning && !mWeatherTickBeforeDisconnect)
        {
            mWeatherTickBeforeDisconnect = serverTick;
            mWeatherRevisionBeforeDisconnect = regions.front().revision;
            mReadyToDisconnect = true;
            mNextDisconnect = receivedAt;
        }
        if (mRole == DesktopAutomationRole::WeatherReconnect && mResumes != 0 && mWeatherTickBeforeDisconnect
            && serverTick > *mWeatherTickBeforeDisconnect
            && std::ranges::all_of(regions, [&](const auto& region) {
                   return !mWeatherRevisionBeforeDisconnect || region.revision > *mWeatherRevisionBeforeDisconnect;
               }))
            mWeatherConvergedAfterResume = true;

        if (mRole == DesktopAutomationRole::WeatherSlow && transitioning && !mSlowPeerStalled)
        {
            mSlowPeerStalled = true;
            if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
            {
                mOutput << "{\"event\":\"weather_slow_peer_stall_started\",\"role\":\"" << roleName()
                        << "\",\"server_tick\":" << serverTick.value() << "}\n";
                mOutput.flush();
                ++mEvidenceEvents;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
        else if (mRole == DesktopAutomationRole::WeatherSlow && mSlowPeerStalled && completed)
        {
            mSlowPeerRecovered = true;
            if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
            {
                mOutput << "{\"event\":\"weather_slow_peer_recovered\",\"role\":\"" << roleName()
                        << "\",\"server_tick\":" << serverTick.value() << "}\n";
                mOutput.flush();
                ++mEvidenceEvents;
            }
        }
        return ProviderResult::Accepted;
    }

    ProviderResult DesktopAutomation::applyWorldTime(
        const ReliableWorldTimeState& state, MonotonicInstant receivedAt) noexcept
    {
        const auto applied = mPresentation.applyWorldTime(state, receivedAt);
        if (applied != ProviderResult::Accepted)
            return applied;
        ++mWorldTimePresentations;
        if (mLastWorldTime && *mLastWorldTime == state)
            ++mWorldTimeDuplicates;
        else
            writeWaitRestSample(state);
        if (!mInitialWorldTime)
            mInitialWorldTime = state.time;
        if (mInitialWorldTime
            && (state.time.year != mInitialWorldTime->year || state.time.month != mInitialWorldTime->month
                || state.time.day != mInitialWorldTime->day))
            mWaitRestApplied = true;

        if (mRole == DesktopAutomationRole::WaitReconnect && !mWorldTimeRevisionBeforeDisconnect && mResumes == 0)
            mWorldTimeRevisionBeforeDisconnect = state.time.revision;
        if (mRole == DesktopAutomationRole::WaitReconnect && mResumes != 0 && mWorldTimeRevisionBeforeDisconnect
            && state.time.revision > *mWorldTimeRevisionBeforeDisconnect && mWaitRestApplied)
            mWorldTimeConvergedAfterResume = true;

        if (mRole == DesktopAutomationRole::WaitSlow && mLastWorldTime && !mSlowPeerStalled
            && mWaitRestApplied)
        {
            mSlowPeerStalled = true;
            if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
            {
                mOutput << "{\"event\":\"wait_slow_peer_stall_started\",\"role\":\"" << roleName()
                        << "\",\"revision\":" << state.time.revision.value() << "}\n";
                mOutput.flush();
                ++mEvidenceEvents;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            mSlowPeerRecovered = true;
            if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
            {
                mOutput << "{\"event\":\"wait_slow_peer_recovered\",\"role\":\"" << roleName()
                        << "\",\"revision\":" << state.time.revision.value() << "}\n";
                mOutput.flush();
                ++mEvidenceEvents;
            }
        }
        mLastWorldTime = state;
        return ProviderResult::Accepted;
    }

    std::optional<ObjectRevision> DesktopAutomation::observedObjectRevision(InteractiveObjectId id) const noexcept
    {
        return mPresentation.observedObjectRevision(id);
    }

    void DesktopAutomation::clear() noexcept
    {
        mPresentation.clear();
    }

    void DesktopAutomation::report(ConnectionStatus status) noexcept
    {
        writeStatus(status);
        if (status == ConnectionStatus::Resumed)
        {
            ++mResumes;
            mAwaitingResumeSnapshot = true;
            mAwaitingActorResumeSnapshot = true;
            mResumeSnapshot = false;
            mNextDisconnect.reset();
        }
        else if (status != ConnectionStatus::Reconnecting)
            finish(false);
        mStatus.report(status);
    }

    bool DesktopAutomation::disconnectRequested() noexcept
    {
        const auto maximumResumes = mRole == DesktopAutomationRole::Reconnect ? 32u
            : mRole == DesktopAutomationRole::ActorReconnect                  ? 4u
            : mRole == DesktopAutomationRole::WeatherReconnect                ? 1u
            : mRole == DesktopAutomationRole::WaitReconnect                   ? 1u
                                                                              : 0u;
        if (maximumResumes == 0 || !mReadyToDisconnect || !mNow || !mNextDisconnect || *mNow < *mNextDisconnect
            || mResumes >= maximumResumes)
            return false;
        mReadyToDisconnect = false;
        mNextDisconnect.reset();
        return true;
    }

    std::uint64_t DesktopAutomation::reconnectDelayNanoseconds() noexcept
    {
        if (mRole == DesktopAutomationRole::WaitReconnect)
            return 8'000'000'000ull;
        return mRole == DesktopAutomationRole::WeatherReconnect ? 4'000'000'000ull
            : 0;
    }

    std::optional<ResyncReason> DesktopAutomation::resyncRequested() noexcept
    {
        if (!mResyncReady || mResyncIssued)
            return std::nullopt;
        mResyncIssued = true;
        mActorAppliedAfterResync = false;
        if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
        {
            mOutput << "{\"event\":\"phase13_resync_requested\",\"role\":\"" << roleName() << "\"}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
        return ResyncReason::LocalFeedGap;
    }

    void DesktopAutomation::resyncCompleted() noexcept
    {
        mResyncDone = true;
        if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
        {
            mOutput << "{\"event\":\"phase13_resync_completed\",\"role\":\"" << roleName() << "\"}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
    }

    const char* DesktopAutomation::roleName() const noexcept
    {
        switch (mRole)
        {
            case DesktopAutomationRole::FlowOne:
                return "flow-one";
            case DesktopAutomationRole::FlowTwo:
                return "flow-two";
            case DesktopAutomationRole::Reconnect:
                return "reconnect";
            case DesktopAutomationRole::SoakOne:
                return "soak-one";
            case DesktopAutomationRole::SoakTwo:
                return "soak-two";
            case DesktopAutomationRole::CaptureOne:
                return "capture-one";
            case DesktopAutomationRole::CaptureTwo:
                return "capture-two";
            case DesktopAutomationRole::ActorReconnect:
                return "actor-reconnect";
            case DesktopAutomationRole::ActorAuth:
                return "actor-auth";
            case DesktopAutomationRole::WeatherOne:
                return "weather-one";
            case DesktopAutomationRole::WeatherTwo:
                return "weather-two";
            case DesktopAutomationRole::WeatherReconnect:
                return "weather-reconnect";
            case DesktopAutomationRole::WeatherSlow:
                return "weather-slow";
            case DesktopAutomationRole::WaitOne:
                return "wait-one";
            case DesktopAutomationRole::WaitTwo:
                return "wait-two";
            case DesktopAutomationRole::WaitAnchor:
                return "wait-anchor";
            case DesktopAutomationRole::WaitReconnect:
                return "wait-reconnect";
            case DesktopAutomationRole::WaitSlowAnchor:
                return "wait-slow-anchor";
            case DesktopAutomationRole::WaitSlow:
                return "wait-slow";
        }
        return "unknown";
    }

    void DesktopAutomation::writeActorSample(const ActorSpatialSnapshot& actor) noexcept
    {
        const std::size_t limit
            = mRole == DesktopAutomationRole::FlowOne || mRole == DesktopAutomationRole::FlowTwo ? 64 : 24;
        if (!mOutput || mEvidenceEvents >= MaximumEvidenceEvents || mActorEvidenceSamples >= limit)
            return;
        const auto& position = actor.transform().position();
        mOutput << "{\"event\":\"phase13_actor_sample\",\"role\":\"" << roleName()
                << "\",\"actor_id\":" << actor.actorId().value() << ",\"entity_id\":" << actor.entityId().value()
                << ",\"prototype_id\":" << actor.prototypeId().value()
                << ",\"revision\":" << actor.entityRevision().value() << ",\"tick\":" << actor.serverTick().value()
                << ",\"x\":" << position.x() << ",\"y\":" << position.y() << ",\"z\":" << position.z() << "}\n";
        mOutput.flush();
        ++mEvidenceEvents;
        ++mActorEvidenceSamples;
    }

    void DesktopAutomation::writeWeatherSample(
        std::span<const WeatherRegionSnapshot> regions, ServerTick serverTick) noexcept
    {
        if (!mOutput || mEvidenceEvents >= MaximumEvidenceEvents || mWeatherEvidenceSamples >= 24)
            return;
        mOutput << "{\"event\":\"weather_sample\",\"role\":\"" << roleName()
                << "\",\"server_tick\":" << serverTick.value() << ",\"regions\":[";
        for (std::size_t index = 0; index < regions.size(); ++index)
        {
            const auto& region = regions[index];
            if (index != 0)
                mOutput << ',';
            mOutput << "{\"region\":" << region.region.value() << ",\"current\":"
                    << region.currentWeather.value() << ",\"target\":" << region.targetWeather.value()
                    << ",\"revision\":" << region.revision.value() << ",\"transition_start\":"
                    << region.transitionStartTick.value() << ",\"transition_end\":"
                    << region.transitionEndTick.value() << ",\"next_selection\":"
                    << region.nextSelectionTick.value() << '}';
        }
        mOutput << "]}\n";
        mOutput.flush();
        ++mEvidenceEvents;
        ++mWeatherEvidenceSamples;
    }

    void DesktopAutomation::writeWaitRestSample(const ReliableWorldTimeState& state) noexcept
    {
        if (!mOutput || mEvidenceEvents >= MaximumEvidenceEvents)
            return;
        mOutput << "{\"event\":\"wait_rest_time_sample\",\"role\":\"" << roleName()
                << "\",\"revision\":" << state.time.revision.value() << ",\"tick\":"
                << state.serverTick.value() << ",\"day\":" << static_cast<unsigned>(state.time.day)
                << ",\"month\":" << static_cast<unsigned>(state.time.month) << ",\"year\":" << state.time.year
                << ",\"milliseconds_since_midnight\":" << state.time.millisecondsSinceMidnight << "}\n";
        mOutput.flush();
        ++mEvidenceEvents;
    }

    void DesktopAutomation::writeStatus(ConnectionStatus status) noexcept
    {
        if (!mOutput || mEvidenceEvents >= MaximumEvidenceEvents)
            return;
        mOutput << "{\"event\":\"phase8_desktop_status\",\"role\":\"" << roleName() << "\",\"status\":\""
                << statusName(status) << "\"}\n";
        mOutput.flush();
        ++mEvidenceEvents;
    }

    void DesktopAutomation::finish(bool success) noexcept
    {
        if (mFinished)
            return;
        mFinished = true;
        if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
        {
            mOutput << "{\"event\":\"phase8_desktop_complete\",\"role\":\"" << roleName()
                    << "\",\"success\":" << (success ? "true" : "false") << ",\"snapshots\":" << mSnapshots
                    << ",\"resumes\":" << mResumes << ",\"saw_peer\":" << (mSawPeer ? "true" : "false")
                    << ",\"saw_leave\":" << (mSawLeave ? "true" : "false")
                    << ",\"saw_return\":" << (mSawReturn ? "true" : "false")
                    << ",\"moved\":" << (mMoved ? "true" : "false")
                    << ",\"player_id\":" << (mPlayerId ? mPlayerId->value() : 0)
                    << ",\"player_entity_id\":" << (mPlayerEntityId ? mPlayerEntityId->value() : 0)
                    << ",\"player_identity_stable\":" << (mPlayerIdentityStable ? "true" : "false")
                    << ",\"actor_snapshots\":" << mActorSnapshots
                    << ",\"actor_resume_snapshots\":" << mActorResumeSnapshots
                    << ",\"actor_id\":" << (mFirstActor ? mFirstActor->actorId().value() : 0)
                    << ",\"actor_entity_id\":" << (mFirstActor ? mFirstActor->entityId().value() : 0)
                    << ",\"actor_prototype_id\":" << (mFirstActor ? mFirstActor->prototypeId().value() : 0)
                    << ",\"actor_first_revision\":" << (mFirstActor ? mFirstActor->entityRevision().value() : 0)
                    << ",\"actor_last_revision\":" << (mLastActor ? mLastActor->entityRevision().value() : 0)
                    << ",\"actor_stable\":" << (mActorStable ? "true" : "false")
                    << ",\"actor_moved\":" << (mActorMoved ? "true" : "false")
                    << ",\"actor_saw_leave\":" << (mActorSawLeave ? "true" : "false")
                    << ",\"actor_saw_return\":" << (mActorSawReturn ? "true" : "false")
                    << ",\"resync_completed\":" << (mResyncDone ? "true" : "false")
                    << ",\"actor_after_resync\":" << (mActorAppliedAfterResync ? "true" : "false")
                    << ",\"weather_presentations\":" << mWeatherPresentations
                    << ",\"weather_duplicate_presentations\":" << mWeatherDuplicates
                    << ",\"weather_regions\":" << mLastWeather.size()
                    << ",\"weather_transition\":" << (mSawWeatherTransition ? "true" : "false")
                    << ",\"weather_completion\":" << (mSawWeatherCompletion ? "true" : "false")
                    << ",\"weather_resumed_converged\":" << (mWeatherConvergedAfterResume ? "true" : "false")
                    << ",\"slow_peer_stalled\":" << (mSlowPeerStalled ? "true" : "false")
                    << ",\"slow_peer_recovered\":" << (mSlowPeerRecovered ? "true" : "false")
                    << ",\"world_time_presentations\":" << mWorldTimePresentations
                    << ",\"world_time_duplicate_presentations\":" << mWorldTimeDuplicates
                    << ",\"wait_rest_submitted\":" << (mWaitRestSubmitted ? "true" : "false")
                    << ",\"wait_rest_applied\":" << (mWaitRestApplied ? "true" : "false")
                    << ",\"world_time_resumed_converged\":"
                    << (mWorldTimeConvergedAfterResume ? "true" : "false") << "}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
        try
        {
            MWBase::Environment::get().getStateManager()->requestQuit();
        }
        catch (...)
        {
        }
    }
}
