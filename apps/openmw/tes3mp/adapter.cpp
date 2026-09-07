#include "adapter.hpp"
#include "movement_mapping.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        constexpr std::uint64_t RetryIntervalNanoseconds = 1'000'000'000;
        constexpr std::uint64_t PoseSampleIntervalNanoseconds = 50'000'000;

        ClientHello makeClientHello(ContentManifestId contentManifest)
        {
            auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
            const std::array optional{ vrPoseCapability() };
            auto offer = std::get<CapabilityOffer>(
                CapabilityOffer::create(std::move(versions), optional, {}, contentManifest));
            return ClientHello::fromOffer(std::move(offer));
        }

        bool poseNegotiated(const ClientSessionRuntime& runtime) noexcept
        {
            const auto& hello = runtime.session().stateMachine().negotiatedHello();
            return hello && std::binary_search(
                hello->negotiatedCapabilities().begin(), hello->negotiatedCapabilities().end(), vrPoseCapability());
        }

        struct ResumeContinuity
        {
            SessionId session;
            PlayerId player;
            EntityId entity;
            EntityRevision revision;
            std::optional<CommandSequence> acknowledged;
        };

        const SpatialEntitySnapshot* selfEntry(const LatestWinsSnapshot& snapshot)
        {
            const auto self = std::ranges::find_if(snapshot.view().entries(), [&](const auto& entry) {
                return entry.playerId() == snapshot.header().targetPlayerId()
                    && entry.entityId() == snapshot.header().targetEntityId();
            });
            return self == snapshot.view().entries().end() ? nullptr : &*self;
        }

        std::optional<ResumeContinuity> continuity(const LatestWinsSnapshot& snapshot)
        {
            const auto* self = selfEntry(snapshot);
            if (!self)
                return std::nullopt;
            return ResumeContinuity{ snapshot.header().targetSessionId(), snapshot.header().targetPlayerId(),
                snapshot.header().targetEntityId(), self->entityRevision(),
                snapshot.header().acknowledgedCommandSequence() };
        }

        bool preserves(const LatestWinsSnapshot& snapshot, SessionGeneration generation, const ResumeContinuity& prior)
        {
            const auto current = continuity(snapshot);
            return current && snapshot.header().targetSessionGeneration() == generation
                && current->session == prior.session && current->player == prior.player
                && current->entity == prior.entity && current->revision == prior.revision
                && current->acknowledged == prior.acknowledged;
        }

        class Coordinator final : public EngineCoordinator
        {
        public:
            Coordinator(std::unique_ptr<TransportRuntime> transport, std::unique_ptr<MonotonicClock> clock,
                std::unique_ptr<ClientSessionRuntime> runtime, ReconnectConfiguration reconnect,
                SemanticInputProvider& input, PresentationProvider& presentation, ConnectionStatusProvider& status,
                ConnectionControlProvider* control, VrPoseInputProvider* poseInput,
                std::unique_ptr<PlayerCredentialPersistence> playerCredentials,
                MovementMetricSink* movementMetrics) noexcept
                : mTransport(std::move(transport))
                , mClock(std::move(clock))
                , mRuntime(std::move(runtime))
                , mReconnect(std::move(reconnect))
                , mInput(input)
                , mPresentation(presentation)
                , mStatus(status)
                , mControl(control)
                , mPoseInput(poseInput)
                , mPlayerCredentials(std::move(playerCredentials))
                , mMovementMetrics(movementMetrics)
                , mMotion(movementMetrics)
                , mPoseEvidence(movementMetrics)
            {
            }

            ~Coordinator() override
            {
                mPresentation.clear();
                if (mRuntime)
                    mRuntime->close();
            }

            void frame(float) noexcept override
            {
                if (mClosed)
                    return;
                const auto now = mClock->now();
                if (!mRuntime)
                {
                    if (mResuming && mNextAttempt && now >= *mNextAttempt)
                        startResumeAttempt(now);
                    return;
                }
                if (mResuming && mResumeDeadline && now >= *mResumeDeadline)
                {
                    closeTerminal(ConnectionStatus::ResumeFailed);
                    return;
                }
                if (!mResuming && mReady && mControl && mControl->disconnectRequested())
                {
                    beginResume(now);
                    return;
                }

                const bool hadSnapshot = mRuntime->session().stateMachine().interestBaselineComplete();
                CellTransitionCapture captured;
                if (hadSnapshot && !mResuming)
                {
                    captured = mInput.captureCellTransition();
                    if (captured.result != ProviderResult::Accepted)
                    {
                        closeForProviderFailure(captured.result);
                        return;
                    }
                }
                const auto advanced = mRuntime->advance();
                if (advanced.result != ClientRuntimeResult::Accepted)
                {
                    handleRuntimeFailure(advanced.result, advanced.action, now);
                    return;
                }
                if (advanced.authenticationAccepted)
                {
                    if (auto playerCredential = mRuntime->takePlayerCredential())
                    {
                        if (!mPlayerCredentials || !mPlayerCredentials->store(std::move(*playerCredential)))
                        {
                            closeTerminal(ConnectionStatus::TransportFailed);
                            return;
                        }
                    }
                    auto token = mRuntime->takeResumeToken();
                    const auto lifetime = mRuntime->resumeLifetimeMilliseconds();
                    if (!token || lifetime < MinimumResumeTokenLifetimeMilliseconds
                        || lifetime > MaximumResumeTokenLifetimeMilliseconds)
                    {
                        closeTerminal(mResuming ? ConnectionStatus::ResumeFailed : ConnectionStatus::TransportFailed);
                        return;
                    }
                    const auto deadline = lifetime <= std::numeric_limits<std::uint64_t>::max() / 1'000'000
                        ? sessionDeadline(now, lifetime * 1'000'000)
                        : std::nullopt;
                    if (!deadline)
                    {
                        closeTerminal(mResuming ? ConnectionStatus::ResumeFailed : ConnectionStatus::TransportFailed);
                        return;
                    }
                    mResumeToken = std::move(token);
                    mTokenDeadline = *deadline;
                    if (mResuming)
                        mResumeDeadline.reset();
                }

                const auto& snapshot = mRuntime->session().stateMachine().confirmedSnapshot();
                if (snapshot)
                    mMotion.observeAcknowledgement(snapshot->header().acknowledgedCommandSequence(), now);
                bool finalizedCellTransition = false;
                if (snapshot && mPendingCellTransition && snapshot->header().acknowledgedCommandSequence()
                    && *snapshot->header().acknowledgedCommandSequence() >= *mPendingCellTransition)
                {
                    mPendingCellTransition.reset();
                    finalizedCellTransition = true;
                }
                if (mResuming && advanced.baselineCompleted)
                {
                    if (!mAttemptGeneration || !mContinuity || !preserves(*snapshot, *mAttemptGeneration, *mContinuity))
                    {
                        closeTerminal(ConnectionStatus::ResumeFailed);
                        return;
                    }
                    mResuming = false;
                    mReady = true;
                    mStatus.report(ConnectionStatus::Resumed);
                }
                if ((advanced.baselineCompleted || advanced.snapshotApplied || advanced.observationApplied)
                    && snapshot && mRuntime->session().stateMachine().interestBaselineComplete())
                {
                    const auto localReconciliation
                        = mRuntime->reconcileLocalPresentation(advanced.baselineCompleted && !mReady);
                    const auto applied
                        = mPresentation.applyAuthoritative(*snapshot, mRuntime->session().observedPlayers(),
                            !mPendingCellTransition && !mDeferredCellTransition && !captured.transition, now,
                            localReconciliation);
                    if (applied != ProviderResult::Accepted)
                    {
                        closeForProviderFailure(applied);
                        return;
                    }
                    if (advanced.baselineCompleted)
                    {
                        auto current = continuity(*snapshot);
                        if (!current)
                        {
                            closeTerminal(ConnectionStatus::TransportFailed);
                            return;
                        }
                        mContinuity = std::move(current);
                        mAttemptGeneration = snapshot->header().targetSessionGeneration();
                        mReady = true;
                    }
                }
                if (snapshot)
                {
                    mPoseEvidence.retain(snapshot->view().entries());
                    for (const auto& pose : advanced.poseSnapshots)
                    {
                        const auto observed = std::ranges::find_if(snapshot->view().entries(), [&](const auto& entry) {
                            return entry.playerId() == pose.sourcePlayerId()
                                && entry.entityId() == pose.rootEntityId()
                                && entry.authorityEpoch() == pose.rootAuthorityEpoch();
                        });
                        if (observed == snapshot->view().entries().end()
                            || mPresentation.applyVrPose(pose, now) != ProviderResult::Accepted)
                        {
                            closeForProviderFailure(ProviderResult::PresentationFailed);
                            return;
                        }
                        mPoseEvidence.observe(
                            pose.rootEntityId(), pose.rootAuthorityEpoch(), pose.sampleSequence(), now);
                    }
                }
                mPoseEvidence.advance(now);
                if (mPresentation.advance(now) != ProviderResult::Accepted)
                {
                    closeForProviderFailure(ProviderResult::PresentationFailed);
                    return;
                }
                if (mResuming || !mReady)
                {
                    if (mRuntime->flushOutbound() != ClientRuntimeResult::Accepted)
                        handleRuntimeFailure(
                            ClientRuntimeResult::TransportFailed, ClientSessionAction::SessionClosed, now);
                    return;
                }
                if (captured.transition)
                {
                    if (mPendingCellTransition)
                        mDeferredCellTransition = std::move(captured.transition);
                    else
                        queueCellTransition(std::move(*captured.transition));
                }
                else if (finalizedCellTransition && mDeferredCellTransition)
                {
                    auto deferred = std::move(*mDeferredCellTransition);
                    mDeferredCellTransition.reset();
                    queueCellTransition(std::move(deferred));
                }
                if (mClosed)
                    return;
                if (snapshot)
                {
                    if (auto intent = mInput.sampleCurrentIntent())
                        mMotion.sample(std::move(*intent), now);
                    const auto* self = selfEntry(*snapshot);
                    if (!self)
                    {
                        closeTerminal(ConnectionStatus::TransportFailed);
                        return;
                    }
                    if (auto intent = mMotion.next(self->linearVelocity()))
                    {
                        const LocomotionIntent queuedIntent = *intent;
                        const auto& hello = mRuntime->session().stateMachine().negotiatedHello();
                        const auto queued = hello && hello->selectedVersion().minor >= 3
                            ? mRuntime->queueLocomotionIntent(std::move(*intent))
                            : mRuntime->queueMotionIntent(PlayerMotionIntent(intent->desiredVelocity()));
                        if (queued.result != ClientRuntimeResult::Accepted || !queued.sequence
                            || !mMotion.markQueued(*queued.sequence, queuedIntent, now))
                        {
                            closeTerminal(ConnectionStatus::TransportFailed);
                            return;
                        }
                    }
                    if (mPoseInput && poseNegotiated(*mRuntime)
                        && (!mNextPoseSample || now >= *mNextPoseSample))
                    {
                        if (auto pose = mPoseInput->sampleVrPose())
                        {
                            const auto sequence = mPoseSequence ? mPoseSequence->next()
                                                                : std::optional(PoseSampleSequence::initial());
                            if (!sequence
                                || mRuntime->queuePoseSample(ClientVrPoseSample(
                                       snapshot->header().targetSessionId(),
                                       snapshot->header().targetSessionGeneration(), self->entityId(),
                                       self->authorityEpoch(), *sequence, pose->head, pose->leftHand,
                                       pose->rightHand))
                                    != ClientRuntimeResult::Accepted)
                            {
                                closeTerminal(ConnectionStatus::TransportFailed);
                                return;
                            }
                            mPoseSequence = *sequence;
                        }
                        if (now.nanoseconds()
                            <= std::numeric_limits<std::uint64_t>::max() - PoseSampleIntervalNanoseconds)
                            mNextPoseSample = MonotonicInstant::fromNanoseconds(
                                now.nanoseconds() + PoseSampleIntervalNanoseconds);
                        else
                        {
                            closeTerminal(ConnectionStatus::TransportFailed);
                            return;
                        }
                    }
                }
                if (mRuntime->flushOutbound() != ClientRuntimeResult::Accepted)
                    handleRuntimeFailure(ClientRuntimeResult::TransportFailed, ClientSessionAction::SessionClosed, now);
            }

        private:
            void handleRuntimeFailure(ClientRuntimeResult result, ClientSessionAction action, MonotonicInstant now)
            {
                if (mResuming)
                {
                    if (mResumeToken && mContinuity && mAttemptGeneration)
                    {
                        beginResume(now);
                        return;
                    }
                    auto reusable = mRuntime->takeUnsubmittedResumeToken();
                    mRuntime->close();
                    mRuntime.reset();
                    if (reusable && mResumeDeadline && now < *mResumeDeadline)
                    {
                        mResumeToken = std::move(reusable);
                        if (now.nanoseconds() <= std::numeric_limits<std::uint64_t>::max() - RetryIntervalNanoseconds)
                            mNextAttempt
                                = MonotonicInstant::fromNanoseconds(now.nanoseconds() + RetryIntervalNanoseconds);
                        else
                            closeTerminal(ConnectionStatus::ResumeFailed);
                        return;
                    }
                    closeTerminal(ConnectionStatus::ResumeFailed);
                    return;
                }
                if (mReady && mResumeToken && mContinuity && mAttemptGeneration)
                {
                    beginResume(now);
                    return;
                }
                reportFailure(result, action);
                mClosed = true;
            }

            void beginResume(MonotonicInstant now)
            {
                const auto nextGeneration = mAttemptGeneration ? mAttemptGeneration->next() : std::nullopt;
                if (!mResumeToken || !mContinuity || !nextGeneration || !mTokenDeadline || now >= *mTokenDeadline)
                {
                    closeTerminal(ConnectionStatus::ResumeFailed);
                    return;
                }
                mPresentation.clear();
                mMotion = MotionIntentTracker(mMovementMetrics);
                mPoseEvidence.clear();
                mPoseSequence.reset();
                mNextPoseSample.reset();
                mPendingCellTransition.reset();
                mDeferredCellTransition.reset();
                mReady = false;
                mResuming = true;
                mAttemptGeneration = *nextGeneration;
                mResumeDeadline = *mTokenDeadline;
                mNextAttempt = now;
                mRuntime->close();
                mRuntime.reset();
                mStatus.report(ConnectionStatus::Reconnecting);
                startResumeAttempt(now);
            }

            void startResumeAttempt(MonotonicInstant now)
            {
                if (!mResuming || !mResumeToken || !mAttemptGeneration || !mResumeDeadline || now >= *mResumeDeadline)
                {
                    closeTerminal(ConnectionStatus::ResumeFailed);
                    return;
                }
                auto created = ClientSessionRuntime::create(
                    *mTransport, *mClock, mReconnect.timeouts, *mAttemptGeneration, mReconnect.outbound);
                auto* runtime = std::get_if<std::unique_ptr<ClientSessionRuntime>>(&created);
                if (!runtime || !*runtime)
                {
                    closeTerminal(ConnectionStatus::ResumeFailed);
                    return;
                }
                auto attempt = std::move(*runtime);
                auto token = std::move(*mResumeToken);
                mResumeToken.reset();
                if (attempt->start(
                        mReconnect.endpoint, makeClientHello(mReconnect.contentManifest),
                        AuthenticationRequest::resume(std::move(token)))
                    != HeadlessClientResult::Accepted)
                {
                    mRuntime = std::move(attempt);
                    handleRuntimeFailure(ClientRuntimeResult::TransportFailed, ClientSessionAction::SessionClosed, now);
                    return;
                }
                mRuntime = std::move(attempt);
                mNextAttempt.reset();
            }

            void reportFailure(ClientRuntimeResult result, ClientSessionAction action) noexcept
            {
                mPresentation.clear();
                if (mRuntime)
                    mRuntime->close();
                if (action == ClientSessionAction::SessionRejected)
                {
                    const auto& state = mRuntime->session().stateMachine();
                    mStatus.report(state.authenticationRejection() ? ConnectionStatus::AuthenticationRejected
                                                                   : ConnectionStatus::ProtocolRejected);
                }
                else if (action == ClientSessionAction::SessionTimedOut)
                    mStatus.report(ConnectionStatus::TimedOut);
                else if (action == ClientSessionAction::SessionClosed)
                    mStatus.report(ConnectionStatus::Disconnected);
                else if (result == ClientRuntimeResult::ProtocolRejected)
                    mStatus.report(ConnectionStatus::ProtocolRejected);
                else
                    mStatus.report(ConnectionStatus::TransportFailed);
            }

            void queueCellTransition(CellTransition transition) noexcept
            {
                const auto queued = mRuntime->queueCellTransition(std::move(transition));
                if (queued.result != ClientRuntimeResult::Accepted || !queued.sequence)
                {
                    closeForProviderFailure(ProviderResult::PresentationFailed);
                    return;
                }
                mPendingCellTransition = queued.sequence;
            }

            void closeForProviderFailure(ProviderResult result) noexcept
            {
                closeTerminal(result == ProviderResult::ContentMappingFailed ? ConnectionStatus::ContentMappingFailed
                                                                             : ConnectionStatus::PresentationFailed);
            }

            void closeTerminal(ConnectionStatus status) noexcept
            {
                mPresentation.clear();
                if (mRuntime)
                    mRuntime->close();
                mRuntime.reset();
                mResumeToken.reset();
                mTokenDeadline.reset();
                mStatus.report(status);
                mClosed = true;
            }

            std::unique_ptr<TransportRuntime> mTransport;
            std::unique_ptr<MonotonicClock> mClock;
            std::unique_ptr<ClientSessionRuntime> mRuntime;
            ReconnectConfiguration mReconnect;
            SemanticInputProvider& mInput;
            PresentationProvider& mPresentation;
            ConnectionStatusProvider& mStatus;
            ConnectionControlProvider* mControl = nullptr;
            VrPoseInputProvider* mPoseInput = nullptr;
            std::unique_ptr<PlayerCredentialPersistence> mPlayerCredentials;
            MovementMetricSink* mMovementMetrics = nullptr;
            MotionIntentTracker mMotion;
            PoseEvidenceTracker mPoseEvidence;
            bool mClosed = false;
            bool mReady = false;
            bool mResuming = false;
            std::optional<CommandSequence> mPendingCellTransition;
            std::optional<CellTransition> mDeferredCellTransition;
            std::optional<PoseSampleSequence> mPoseSequence;
            std::optional<MonotonicInstant> mNextPoseSample;
            std::optional<ResumeToken> mResumeToken;
            std::optional<MonotonicInstant> mTokenDeadline;
            std::optional<SessionGeneration> mAttemptGeneration;
            std::optional<ResumeContinuity> mContinuity;
            std::optional<MonotonicInstant> mResumeDeadline;
            std::optional<MonotonicInstant> mNextAttempt;
        };
    }

    std::unique_ptr<EngineCoordinator> makeCoordinator(std::unique_ptr<TransportRuntime> transport,
        std::unique_ptr<MonotonicClock> clock, std::unique_ptr<ClientSessionRuntime> runtime,
        ReconnectConfiguration reconnect, SemanticInputProvider& input, PresentationProvider& presentation,
        ConnectionStatusProvider& status, ConnectionControlProvider* control, VrPoseInputProvider* poseInput,
        std::unique_ptr<PlayerCredentialPersistence> playerCredentials, MovementMetricSink* movementMetrics) noexcept
    {
        if (!transport || !clock || !runtime)
            return {};
        return std::make_unique<Coordinator>(std::move(transport), std::move(clock), std::move(runtime),
            std::move(reconnect), input, presentation, status, control, poseInput, std::move(playerCredentials),
            movementMetrics);
    }
}
