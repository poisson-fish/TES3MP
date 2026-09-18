#include "desktop_automation.hpp"

#include "engine_coordinator.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"

#include <components/debug/debuglog.hpp>

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
        constexpr std::uint64_t SecurityDuration = 12 * Second;
        constexpr std::uint64_t MagicSpellDuration = 20 * Second;
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
        if (value == "native-traversal")
            return DesktopAutomationRole::NativeTraversal;
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
        if (value == "security-pick")
            return DesktopAutomationRole::SecurityPick;
        if (value == "security-probe")
            return DesktopAutomationRole::SecurityProbe;
        if (value == "magic-item")
            return DesktopAutomationRole::MagicItem;
        if (value == "magic-spell-caster")
            return DesktopAutomationRole::MagicSpellCaster;
        if (value == "magic-spell-target")
            return DesktopAutomationRole::MagicSpellTarget;
        if (value == "native-put")
            return DesktopAutomationRole::NativePut;
        if (value == "native-take")
            return DesktopAutomationRole::NativeTake;
        if (value == "native-recover-one")
            return DesktopAutomationRole::NativeRecoverOne;
        if (value == "native-recover-two")
            return DesktopAutomationRole::NativeRecoverTwo;
        return std::nullopt;
    }

    DesktopAutomation::DesktopAutomation(DesktopAutomationRole role, const std::filesystem::path& output,
        ContentManifest contentManifest, DesktopPresentation& presentation, ConnectionStatusProvider& status)
        : mRole(role)
        , mInterior(firstCell(contentManifest, CellId::Kind::Interior))
        , mExterior(role == DesktopAutomationRole::NativeTraversal ? mInterior
                                                                 : firstCell(contentManifest, CellId::Kind::Exterior))
        , mOutput(output, std::ios::out | std::ios::trunc)
        , mPresentation(presentation)
        , mStatus(status)
    {
        if (mRole == DesktopAutomationRole::NativeTraversal)
            mTraversalControl = output.string() + ".control";
        if (mOutput)
        {
            mOutput << "{\"event\":\"phase8_desktop_started\",\"role\":\"" << roleName() << "\"}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
    }

    CellTransitionCapture DesktopAutomation::captureCellTransition() noexcept
    {
        if (mRole == DesktopAutomationRole::NativeTraversal && mDesktopInput)
            return mDesktopInput->captureCellTransition();
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
        if (mRole == DesktopAutomationRole::NativeTraversal && mDesktopInput)
            return mDesktopInput->sampleCurrentIntent();
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

    std::optional<ObjectInteractionCapture> DesktopAutomation::captureObjectInteraction() noexcept
    {
        if (mRole == DesktopAutomationRole::NativeTraversal && mDesktopInput)
            return mDesktopInput->captureObjectInteraction();
        if ((mRole != DesktopAutomationRole::SecurityPick && mRole != DesktopAutomationRole::SecurityProbe)
            || mSecuritySubmitted || !mSecurityObject || !mSecurityObjectRevision || !mSecurityTool
            || !mSecurityInventoryRevision || !mSecurityCombatRevision || !mSelfCell || !mInitialPosition)
            return std::nullopt;
        mSecuritySubmitted = true;
        if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
        {
            mOutput << "{\"event\":\"security_" << (mRole == DesktopAutomationRole::SecurityPick ? "pick" : "probe")
                    << "_submitted\",\"role\":\"" << roleName() << "\",\"object_id\":" << mSecurityObject->value()
                    << ",\"tool_stack_id\":" << mSecurityTool->value() << "}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
        return ObjectInteractionCapture{ .objectId = *mSecurityObject,
            .targetCell = *mSelfCell,
            .interactionOrigin = *mInitialPosition,
            .expectedRevision = *mSecurityObjectRevision,
            .kind = mRole == DesktopAutomationRole::SecurityPick ? ObjectInteractionKind::PickLock
                                                                 : ObjectInteractionKind::DisarmTrap,
            .requestedTool = *mSecurityTool,
            .expectedInventoryRevision = *mSecurityInventoryRevision,
            .expectedCombatRevision = *mSecurityCombatRevision };
    }

    std::optional<MagicUseCapture> DesktopAutomation::captureMagicUse() noexcept
    {
        if (mRole == DesktopAutomationRole::MagicSpellCaster && !mMagicSubmitted && mMagicPlayer
            && mMagicInventoryRevision && mMagicCasterRevision && mMagicTargetRevision && mMagicSourceTick
            && mStartedAt && mNow && mNow->nanoseconds() - mStartedAt->nanoseconds() >= 3 * Second)
        {
            constexpr std::uint64_t CurseFatigueSpell = 13128897029866312148ull;
            mMagicSubmitted = true;
            if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
            {
                mOutput << "{\"event\":\"magic_spell_submitted\",\"role\":\"" << roleName()
                        << "\",\"target_player_id\":" << mMagicPlayer->value() << ",\"spell_id\":"
                        << CurseFatigueSpell << "}\n";
                mOutput.flush();
                ++mEvidenceEvents;
            }
            return MagicUseCapture{ MagicUseSourceKind::Spell, CurseFatigueSpell, MagicUseTargetKind::Player,
                mMagicPlayer->value(), *mMagicSourceTick, *mMagicCasterRevision, *mMagicTargetRevision,
                *mMagicInventoryRevision };
        }
        if (mRole != DesktopAutomationRole::MagicItem || mMagicSubmitted || !mMagicActor || !mMagicItem
            || !mMagicInventoryRevision || !mMagicCasterRevision || !mMagicTargetRevision || !mMagicSourceTick)
            return std::nullopt;
        mMagicSubmitted = true;
        if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
        {
            mOutput << "{\"event\":\"magic_item_submitted\",\"role\":\"" << roleName()
                    << "\",\"actor_id\":" << mMagicActor->value() << ",\"item_stack_id\":" << mMagicItem->value()
                    << "}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
        return MagicUseCapture{ MagicUseSourceKind::EnchantedItem, mMagicItem->value(), MagicUseTargetKind::Actor,
            mMagicActor->value(), *mMagicSourceTick, *mMagicCasterRevision, *mMagicTargetRevision,
            *mMagicInventoryRevision };
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
        if (mRole == DesktopAutomationRole::MagicSpellCaster && !mMagicPlayer)
        {
            const auto peer = std::ranges::find_if(observedPlayers, [&](const ObservedPlayer& observed) {
                return observed.playerId != snapshot.header().targetPlayerId();
            });
            if (peer != observedPlayers.end())
                mMagicPlayer = peer->playerId;
        }
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
        if (mRole == DesktopAutomationRole::MagicItem)
            mMagicActor = actor.actorId();
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
        if (nativeInventoryRole())
        {
            try
            {
                if (mRole == DesktopAutomationRole::NativeTraversal)
                    advanceNativeTraversal(now);
                else
                    advanceNativeInventory(now);
            }
            catch (const std::exception& error)
            {
                Log(Debug::Error) << "Native desktop evidence failed: " << error.what();
                finish(false);
            }
            return ProviderResult::Accepted;
        }
        const bool submitsWaitRest = mRole == DesktopAutomationRole::WaitOne || mRole == DesktopAutomationRole::WaitTwo
            || mRole == DesktopAutomationRole::WaitAnchor || mRole == DesktopAutomationRole::WaitSlowAnchor
            || mRole == DesktopAutomationRole::WaitSlow;
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
        else if ((mRole == DesktopAutomationRole::SecurityPick || mRole == DesktopAutomationRole::SecurityProbe)
            && (mRole == DesktopAutomationRole::SecurityPick ? mSecurityUnlocked : mSecurityDisarmed)
            && mInitialSecurityToolCondition && mSecurityToolCondition
            && *mSecurityToolCondition + 1 == *mInitialSecurityToolCondition && mInitialSecurityProgress
            && mSecurityProgress && *mSecurityProgress > *mInitialSecurityProgress)
        {
            if (mResumes == 0 && !mSecurityResumeRequested && !mReadyToDisconnect && !mNextDisconnect)
            {
                mSecurityResumeRequested = true;
                mReadyToDisconnect = true;
                mNextDisconnect = now;
                if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
                {
                    mOutput << "{\"event\":\"security_resume_scheduled\",\"role\":\"" << roleName() << "\"}\n";
                    mOutput.flush();
                    ++mEvidenceEvents;
                }
            }
            else if (mResumes == 1 && mSecurityObjectAfterResume && mSecurityInventoryAfterResume
                && mSecurityCombatAfterResume && elapsed >= 6 * Second)
                finish(true);
            else if (elapsed >= SecurityDuration)
                finish(false);
        }
        else if ((mRole == DesktopAutomationRole::SecurityPick || mRole == DesktopAutomationRole::SecurityProbe)
            && elapsed >= SecurityDuration)
            finish(false);
        else if (mRole == DesktopAutomationRole::MagicItem && mResumes == 1 && mMagicInventoryAfterResume
            && mMagicCombatAfterResume && elapsed >= 6 * Second)
            finish(true);
        else if (mRole == DesktopAutomationRole::MagicItem && mMagicEventPresented && mInitialMagicCharge
            && mMagicCharge && *mMagicCharge < *mInitialMagicCharge && mInitialMagicTargetFatigue && mMagicTargetFatigue
            && *mMagicTargetFatigue < *mInitialMagicTargetFatigue && mInitialEnchantProgress && mEnchantProgress
            && *mEnchantProgress > *mInitialEnchantProgress)
        {
            if (mResumes == 0 && !mMagicResumeRequested && !mReadyToDisconnect && !mNextDisconnect)
            {
                mMagicResumeRequested = true;
                mReadyToDisconnect = true;
                mNextDisconnect = now;
                if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
                {
                    mOutput << "{\"event\":\"magic_resume_scheduled\",\"role\":\"" << roleName() << "\"}\n";
                    mOutput.flush();
                    ++mEvidenceEvents;
                }
            }
            else if (elapsed >= SecurityDuration)
                finish(false);
        }
        else if (mRole == DesktopAutomationRole::MagicItem && elapsed >= SecurityDuration)
            finish(false);
        else if (mRole == DesktopAutomationRole::MagicSpellCaster && mResumes == 1 && mMagicEventPresented
            && mMagicEffectStarted && mMagicEffectUpdated && mMagicEffectEnded && mMagicEffectActiveAfterResume
            && mMagicAppliedDelta < 0.f && elapsed >= 6 * Second)
            finish(true);
        else if (mRole == DesktopAutomationRole::MagicSpellTarget && mMagicEventPresented && mMagicEffectStarted
            && mMagicEffectUpdated && mMagicEffectEnded && mMagicAppliedDelta < 0.f && elapsed >= 6 * Second)
            finish(true);
        else if ((mRole == DesktopAutomationRole::MagicSpellCaster
                     || mRole == DesktopAutomationRole::MagicSpellTarget)
            && elapsed >= MagicSpellDuration)
            finish(false);
        return ProviderResult::Accepted;
    }

    ProviderResult DesktopAutomation::applyInteractiveObjects(
        const ReliableInteractiveObjectInterestBaseline& baseline, MonotonicInstant receivedAt) noexcept
    {
        const auto applied = mPresentation.applyInteractiveObjects(baseline, receivedAt);
        if (applied != ProviderResult::Accepted
            || (mRole != DesktopAutomationRole::SecurityPick && mRole != DesktopAutomationRole::SecurityProbe)
            || baseline.members().empty())
            return applied;
        const auto& member = baseline.members().front();
        mSecurityObject = member.objectId;
        mSecurityObjectRevision = member.revision;
        if (!mInitialSecurityObjectRevision)
            mInitialSecurityObjectRevision = member.revision;
        if (mSecuritySubmitted && member.revision > *mInitialSecurityObjectRevision
            && member.lockState == LockState::Unlocked)
            mSecurityUnlocked = true;
        if (mSecuritySubmitted && member.revision > *mInitialSecurityObjectRevision
            && member.trapState == TrapState::Disarmed)
            mSecurityDisarmed = true;
        if (mResumes != 0 && (mRole == DesktopAutomationRole::SecurityPick ? mSecurityUnlocked : mSecurityDisarmed))
            mSecurityObjectAfterResume = true;
        return applied;
    }

    ProviderResult DesktopAutomation::applyInventory(const ReliablePlayerInventoryBaseline& player,
        std::span<const ReliableContainerInventoryBaseline> containers, const ReliableGroundItemBaseline& groundItems,
        const LatestWinsEquipmentSnapshot& equipment, MonotonicInstant receivedAt) noexcept
    {
        const auto applied = mPresentation.applyInventory(player, containers, groundItems, equipment, receivedAt);
        if (applied != ProviderResult::Accepted)
            return applied;
        if (nativeInventoryRole())
        {
            if (mRole == DesktopAutomationRole::NativeTraversal)
            {
                mTraversalGround = groundItems;
                mNativeContainerCount.reset();
                mNativeContainerId.reset();
                mNativeContainerStacks.clear();
            }
            const auto count = [](const auto& stacks) {
                std::uint32_t total = 0;
                for (const auto& stack : stacks)
                    total += stack.count;
                return total;
            };
            mNativePlayerCount = count(player.stacks);
            mNativePlayerStacks = player.stacks;
            if (containers.size() == 1)
            {
                mNativeContainerCount = count(containers.front().stacks);
                mNativeContainerStacks = containers.front().stacks;
                mNativeContainerId = containers.front().container;
            }
            mNativeRevision = player.revision.value();
            // Resume baselines can be presented before the final readiness lane
            // reports Resumed. The wire generation is the continuity witness.
            if (player.header.targetSessionGeneration > SessionGeneration::initial())
                mNativeInventoryAfterResume = true;
            return applied;
        }
        if (mRole == DesktopAutomationRole::MagicSpellCaster)
        {
            mMagicInventoryRevision = player.revision;
            return applied;
        }
        if (mRole == DesktopAutomationRole::MagicItem)
        {
            constexpr std::uint64_t RingOfFleabitePrototype = 13568541167308910850ull;
            const auto prototype = ItemPrototypeId::fromValue(RingOfFleabitePrototype);
            const auto item = prototype ? std::ranges::find(player.stacks, *prototype, &CanonicalItemStack::prototypeId)
                                        : player.stacks.end();
            if (item != player.stacks.end())
            {
                mMagicItem = item->stackId;
                mMagicCharge = item->enchantmentCharge;
                if (!mInitialMagicCharge)
                    mInitialMagicCharge = item->enchantmentCharge;
            }
            mMagicInventoryRevision = player.revision;
            if (mResumes != 0 && mInitialMagicCharge && mMagicCharge && *mMagicCharge < *mInitialMagicCharge)
                mMagicInventoryAfterResume = true;
            return applied;
        }
        if (mRole != DesktopAutomationRole::SecurityPick && mRole != DesktopAutomationRole::SecurityProbe)
            return applied;
        constexpr std::uint64_t ApprenticeLockpickPrototype = 12936841098047256804ull;
        constexpr std::uint64_t ApprenticeProbePrototype = 16269827911551786073ull;
        const auto prototype = ItemPrototypeId::fromValue(
            mRole == DesktopAutomationRole::SecurityPick ? ApprenticeLockpickPrototype : ApprenticeProbePrototype);
        const auto tool = prototype ? std::ranges::find(player.stacks, *prototype, &CanonicalItemStack::prototypeId)
                                    : player.stacks.end();
        if (tool != player.stacks.end())
        {
            mSecurityTool = tool->stackId;
            mSecurityToolCondition = tool->condition;
            if (!mInitialSecurityToolCondition)
                mInitialSecurityToolCondition = tool->condition;
        }
        mSecurityInventoryRevision = player.revision;
        if (mResumes != 0 && mInitialSecurityToolCondition && mSecurityToolCondition
            && *mSecurityToolCondition + 1 == *mInitialSecurityToolCondition)
            mSecurityInventoryAfterResume = true;
        return applied;
    }

    ProviderResult DesktopAutomation::applyCombat(const LatestWinsCombatSnapshot& snapshot,
        std::span<const ReliableCombatEventBatch> events, MonotonicInstant receivedAt) noexcept
    {
        const auto applied = mPresentation.applyCombat(snapshot, events, receivedAt);
        if (applied != ProviderResult::Accepted)
            return applied;
        if (mRole == DesktopAutomationRole::MagicSpellCaster || mRole == DesktopAutomationRole::MagicSpellTarget)
        {
            constexpr std::uint64_t CurseFatigueSpell = 13128897029866312148ull;
            mMagicCasterRevision = snapshot.selfCombatRevision();
            mMagicSourceTick = snapshot.serverTick();
            if (mRole == DesktopAutomationRole::MagicSpellCaster && mMagicPlayer)
            {
                const auto target
                    = std::ranges::find(snapshot.players(), *mMagicPlayer, &PlayerCombatSnapshot::playerId);
                if (target != snapshot.players().end())
                {
                    mMagicTargetRevision = target->combatRevision;
                    mMagicTargetFatigue = target->fatigue;
                }
            }
            else if (mRole == DesktopAutomationRole::MagicSpellTarget)
                mMagicTargetFatigue = snapshot.selfFatigue();
            if (mMagicTargetFatigue)
            {
                if (!mInitialMagicTargetFatigue)
                    mInitialMagicTargetFatigue = *mMagicTargetFatigue;
                if (!mMinimumMagicTargetFatigue || *mMagicTargetFatigue < *mMinimumMagicTargetFatigue)
                    mMinimumMagicTargetFatigue = *mMagicTargetFatigue;
            }
            for (const auto& batch : events)
            {
                if (std::ranges::any_of(batch.magicEvents(), [&](const MagicUseCombatEvent& event) {
                        const bool targetMatches = event.targetKind == MagicUseTargetKind::Player
                            && (mRole == DesktopAutomationRole::MagicSpellTarget
                                    ? event.targetId == snapshot.selfPlayerId().value()
                                    : mMagicPlayer && event.targetId == mMagicPlayer->value());
                        return event.sourceKind == MagicUseSourceKind::Spell && event.sourceId == CurseFatigueSpell
                            && event.castSucceeded && targetMatches;
                    }))
                    mMagicEventPresented = true;
                for (const auto& event : batch.magicEffectEvents())
                {
                    const bool targetMatches = event.targetKind == MagicUseTargetKind::Player
                        && (mRole == DesktopAutomationRole::MagicSpellTarget
                                ? event.targetId == snapshot.selfPlayerId().value()
                                : mMagicPlayer && event.targetId == mMagicPlayer->value());
                    if (!targetMatches || (mMagicEffect && event.instanceId != *mMagicEffect))
                        continue;
                    if (event.eventKind == MagicEffectCombatEventKind::Started)
                    {
                        mMagicEffect = event.instanceId;
                        mMagicEffectStarted = true;
                    }
                    else if (event.eventKind == MagicEffectCombatEventKind::Updated)
                    {
                        mMagicEffectUpdated = mMagicEffectUpdated || event.appliedDelta < 0.f;
                        mMagicAppliedDelta += event.appliedDelta;
                        if (mRole == DesktopAutomationRole::MagicSpellCaster && mResumes == 0 && !mNextDisconnect)
                        {
                            mReadyToDisconnect = true;
                            mNextDisconnect = receivedAt;
                        }
                    }
                    else if (event.eventKind == MagicEffectCombatEventKind::Ended)
                        mMagicEffectEnded = true;
                }
            }
            if (mRole == DesktopAutomationRole::MagicSpellCaster && mResumes != 0 && mMagicEffect
                && std::ranges::any_of(snapshot.activeEffects(), [&](const ActiveMagicEffectSnapshot& effect) {
                       return effect.instanceId == *mMagicEffect && effect.targetKind == MagicUseTargetKind::Player
                           && mMagicPlayer && effect.targetId == mMagicPlayer->value();
                   }))
                mMagicEffectActiveAfterResume = true;
            if (mResumes != 0 && mMagicEffectActiveAfterResume && mMagicEffectUpdated)
                mMagicCombatAfterResume = true;
            return applied;
        }
        if (mRole == DesktopAutomationRole::MagicItem)
        {
            mMagicCasterRevision = snapshot.selfCombatRevision();
            mMagicSourceTick = snapshot.serverTick();
            if (mMagicActor)
            {
                const auto actor = std::ranges::find(snapshot.actors(), *mMagicActor, &ActorCombatSnapshot::actorId);
                if (actor != snapshot.actors().end())
                {
                    mMagicTargetRevision = actor->combatRevision;
                    mMagicTargetFatigue = actor->fatigue;
                    if (!mInitialMagicTargetFatigue)
                        mInitialMagicTargetFatigue = actor->fatigue;
                    if (!mMinimumMagicTargetFatigue || actor->fatigue < *mMinimumMagicTargetFatigue)
                        mMinimumMagicTargetFatigue = actor->fatigue;
                }
            }
            const auto skill
                = std::ranges::find(snapshot.selfSkills(), ReplicatedCombatSkill::Enchant, &CombatSkillSnapshot::skill);
            if (skill != snapshot.selfSkills().end())
            {
                mEnchantProgress = skill->progress;
                if (!mInitialEnchantProgress)
                    mInitialEnchantProgress = skill->progress;
            }
            for (const auto& batch : events)
                if (std::ranges::any_of(batch.magicEvents(), [&](const MagicUseCombatEvent& event) {
                        return event.casterPlayerId == snapshot.selfPlayerId()
                            && event.sourceKind == MagicUseSourceKind::EnchantedItem
                            && event.targetKind == MagicUseTargetKind::Actor && mMagicActor
                            && event.targetId == mMagicActor->value() && event.castSucceeded;
                    }))
                    mMagicEventPresented = true;
            if (mResumes != 0 && mInitialMagicTargetFatigue && mMagicTargetFatigue
                && *mMagicTargetFatigue < *mInitialMagicTargetFatigue && mInitialEnchantProgress && mEnchantProgress
                && *mEnchantProgress > *mInitialEnchantProgress)
                mMagicCombatAfterResume = true;
            return applied;
        }
        if (mRole != DesktopAutomationRole::SecurityPick && mRole != DesktopAutomationRole::SecurityProbe)
            return applied;
        mSecurityCombatRevision = snapshot.selfCombatRevision();
        const auto skill
            = std::ranges::find(snapshot.selfSkills(), ReplicatedCombatSkill::Security, &CombatSkillSnapshot::skill);
        if (skill != snapshot.selfSkills().end())
        {
            mSecurityProgress = skill->progress;
            if (!mInitialSecurityProgress)
                mInitialSecurityProgress = skill->progress;
        }
        if (mResumes != 0 && mInitialSecurityProgress && mSecurityProgress
            && *mSecurityProgress > *mInitialSecurityProgress)
            mSecurityCombatAfterResume = true;
        return applied;
    }

    ProviderResult DesktopAutomation::applyWeather(
        std::span<const WeatherRegionSnapshot> regions, ServerTick serverTick, MonotonicInstant receivedAt) noexcept
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
        const bool completed = std::ranges::all_of(
            regions, [](const auto& region) { return region.currentWeather == region.targetWeather; });
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
            && serverTick > *mWeatherTickBeforeDisconnect && std::ranges::all_of(regions, [&](const auto& region) {
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

        if (mRole == DesktopAutomationRole::WaitSlow && mLastWorldTime && !mSlowPeerStalled && mWaitRestApplied)
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
        mTraversalGround.reset();
        mNativePlayerCount.reset();
        mNativeContainerCount.reset();
        mNativeContainerId.reset();
        mNativeInventoryAfterResume = false;
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
            : mRole == DesktopAutomationRole::SecurityPick                    ? 1u
            : mRole == DesktopAutomationRole::SecurityProbe                   ? 1u
            : mRole == DesktopAutomationRole::MagicItem                       ? 1u
            : mRole == DesktopAutomationRole::MagicSpellCaster                ? 1u
            : mRole == DesktopAutomationRole::NativePut                       ? 1u
            : mRole == DesktopAutomationRole::NativeTake                      ? 1u
            : mRole == DesktopAutomationRole::NativeTraversal                 ? 8u
                                                                              : 0u;
        if (maximumResumes == 0 || !mReadyToDisconnect || !mNow || !mNextDisconnect || *mNow < *mNextDisconnect
            || mResumes >= maximumResumes)
            return false;
        mReadyToDisconnect = false;
        mNextDisconnect.reset();
        if ((mRole == DesktopAutomationRole::SecurityPick || mRole == DesktopAutomationRole::SecurityProbe
                || mRole == DesktopAutomationRole::MagicItem || mRole == DesktopAutomationRole::MagicSpellCaster)
            && mOutput && mEvidenceEvents < MaximumEvidenceEvents)
        {
            const bool magicRole
                = mRole == DesktopAutomationRole::MagicItem || mRole == DesktopAutomationRole::MagicSpellCaster;
            mOutput << "{\"event\":\"" << (magicRole ? "magic" : "security")
                    << "_disconnect_requested\",\"role\":\"" << roleName() << "\"}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
        return true;
    }

    std::uint64_t DesktopAutomation::reconnectDelayNanoseconds() noexcept
    {
        if (mRole == DesktopAutomationRole::WaitReconnect)
            return 8'000'000'000ull;
        return mRole == DesktopAutomationRole::WeatherReconnect ? 4'000'000'000ull : 0;
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
        if (mRole == DesktopAutomationRole::NativeTraversal)
            return "native-traversal";
        switch (mRole)
        {
            case DesktopAutomationRole::NativePut:
                return "native-put";
            case DesktopAutomationRole::NativeTake:
                return "native-take";
            case DesktopAutomationRole::NativeRecoverOne:
                return "native-recover-one";
            case DesktopAutomationRole::NativeRecoverTwo:
                return "native-recover-two";
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
            case DesktopAutomationRole::SecurityPick:
                return "security-pick";
            case DesktopAutomationRole::SecurityProbe:
                return "security-probe";
            case DesktopAutomationRole::MagicItem:
                return "magic-item";
            case DesktopAutomationRole::MagicSpellCaster:
                return "magic-spell-caster";
            case DesktopAutomationRole::MagicSpellTarget:
                return "magic-spell-target";
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
            mOutput << "{\"region\":" << region.region.value() << ",\"current\":" << region.currentWeather.value()
                    << ",\"target\":" << region.targetWeather.value() << ",\"revision\":" << region.revision.value()
                    << ",\"transition_start\":" << region.transitionStartTick.value()
                    << ",\"transition_end\":" << region.transitionEndTick.value()
                    << ",\"next_selection\":" << region.nextSelectionTick.value() << '}';
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
                << "\",\"revision\":" << state.time.revision.value() << ",\"tick\":" << state.serverTick.value()
                << ",\"day\":" << static_cast<unsigned>(state.time.day)
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
                    << ",\"world_time_resumed_converged\":" << (mWorldTimeConvergedAfterResume ? "true" : "false")
                    << ",\"security_submitted\":" << (mSecuritySubmitted ? "true" : "false")
                    << ",\"security_unlocked\":" << (mSecurityUnlocked ? "true" : "false")
                    << ",\"security_disarmed\":" << (mSecurityDisarmed ? "true" : "false")
                    << ",\"security_resumed_converged\":"
                    << (mSecurityObjectAfterResume && mSecurityInventoryAfterResume && mSecurityCombatAfterResume
                               ? "true"
                               : "false")
                    << ",\"security_initial_tool_condition\":" << mInitialSecurityToolCondition.value_or(0)
                    << ",\"security_tool_condition\":" << mSecurityToolCondition.value_or(0)
                    << ",\"security_initial_progress\":" << mInitialSecurityProgress.value_or(0.f)
                    << ",\"security_progress\":" << mSecurityProgress.value_or(0.f)
                    << ",\"magic_submitted\":" << (mMagicSubmitted ? "true" : "false")
                    << ",\"magic_event_presented\":" << (mMagicEventPresented ? "true" : "false")
                    << ",\"magic_resumed_converged\":"
                    << (mMagicInventoryAfterResume && mMagicCombatAfterResume ? "true" : "false")
                    << ",\"magic_initial_charge\":" << mInitialMagicCharge.value_or(0)
                    << ",\"magic_charge\":" << mMagicCharge.value_or(0)
                    << ",\"magic_initial_target_fatigue\":" << mInitialMagicTargetFatigue.value_or(0.f)
                    << ",\"magic_target_fatigue\":" << mMagicTargetFatigue.value_or(0.f)
                    << ",\"magic_minimum_target_fatigue\":" << mMinimumMagicTargetFatigue.value_or(0.f)
                    << ",\"magic_initial_enchant_progress\":" << mInitialEnchantProgress.value_or(0.f)
                    << ",\"magic_enchant_progress\":" << mEnchantProgress.value_or(0.f)
                    << ",\"magic_effect_started\":" << (mMagicEffectStarted ? "true" : "false")
                    << ",\"magic_effect_updated\":" << (mMagicEffectUpdated ? "true" : "false")
                    << ",\"magic_effect_ended\":" << (mMagicEffectEnded ? "true" : "false")
                    << ",\"magic_effect_active_after_resume\":"
                    << (mMagicEffectActiveAfterResume ? "true" : "false")
                    << ",\"magic_applied_delta\":" << mMagicAppliedDelta << "}\n";
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
