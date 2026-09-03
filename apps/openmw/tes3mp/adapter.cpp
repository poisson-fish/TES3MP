#include "adapter.hpp"
#include "movement_mapping.hpp"

#include <ranges>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        class Coordinator final : public EngineCoordinator
        {
        public:
            Coordinator(std::unique_ptr<TransportRuntime> transport, std::unique_ptr<MonotonicClock> clock,
                std::unique_ptr<ClientSessionRuntime> runtime, SemanticInputProvider& input,
                PresentationProvider& presentation, ConnectionStatusProvider& status) noexcept
                : mTransport(std::move(transport))
                , mClock(std::move(clock))
                , mRuntime(std::move(runtime))
                , mInput(input)
                , mPresentation(presentation)
                , mStatus(status)
            {
            }

            ~Coordinator() override
            {
                mPresentation.clear();
                mRuntime->close();
            }

            void frame(float) noexcept override
            {
                if (mClosed)
                    return;
                const bool hadSnapshot = mRuntime->session().stateMachine().confirmedSnapshot().has_value();
                CellTransitionCapture captured;
                if (hadSnapshot)
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
                    reportFailure(advanced.result, advanced.action);
                    mClosed = true;
                    return;
                }
                const auto& snapshot = mRuntime->session().stateMachine().confirmedSnapshot();
                const auto now = mClock->now();
                if (snapshot)
                    mMotion.observeAcknowledgement(snapshot->header().acknowledgedCommandSequence());
                bool finalizedCellTransition = false;
                if (snapshot && mPendingCellTransition && snapshot->header().acknowledgedCommandSequence()
                    && *snapshot->header().acknowledgedCommandSequence() >= *mPendingCellTransition)
                {
                    mPendingCellTransition.reset();
                    finalizedCellTransition = true;
                }
                if ((advanced.snapshotApplied || advanced.observationApplied) && snapshot)
                {
                    const auto applied
                        = mPresentation.applyAuthoritative(*snapshot, mRuntime->session().observedPlayers(),
                            !mPendingCellTransition && !mDeferredCellTransition && !captured.transition, now);
                    if (applied != ProviderResult::Accepted)
                    {
                        closeForProviderFailure(applied);
                        return;
                    }
                }
                if (mPresentation.advance(now) != ProviderResult::Accepted)
                {
                    closeForProviderFailure(ProviderResult::PresentationFailed);
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
                        mMotion.sample(std::move(*intent));
                    const auto self = std::ranges::find_if(snapshot->view().entries(), [&](const auto& entry) {
                        return entry.playerId() == snapshot->header().targetPlayerId()
                            && entry.entityId() == snapshot->header().targetEntityId();
                    });
                    if (self == snapshot->view().entries().end())
                    {
                        mPresentation.clear();
                        mRuntime->close();
                        mStatus.report(ConnectionStatus::TransportFailed);
                        mClosed = true;
                        return;
                    }
                    if (auto intent = mMotion.next(self->linearVelocity()))
                    {
                        const auto queued = mRuntime->queueMotionIntent(std::move(*intent));
                        if (queued.result != ClientRuntimeResult::Accepted || !queued.sequence
                            || !mMotion.markQueued(*queued.sequence))
                        {
                            mPresentation.clear();
                            mRuntime->close();
                            mStatus.report(ConnectionStatus::TransportFailed);
                            mClosed = true;
                            return;
                        }
                    }
                }
                if (mRuntime->flushOutbound() != ClientRuntimeResult::Accepted)
                {
                    mPresentation.clear();
                    mStatus.report(ConnectionStatus::TransportFailed);
                    mClosed = true;
                }
            }

            void reportFailure(ClientRuntimeResult result, ClientSessionAction action) noexcept
            {
                mPresentation.clear();
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

            void queueCellTransition(FixtureCellTransition transition) noexcept
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
                mPresentation.clear();
                mRuntime->close();
                mStatus.report(result == ProviderResult::ContentMappingFailed ? ConnectionStatus::ContentMappingFailed
                                                                              : ConnectionStatus::PresentationFailed);
                mClosed = true;
            }

        private:
            std::unique_ptr<TransportRuntime> mTransport;
            std::unique_ptr<MonotonicClock> mClock;
            std::unique_ptr<ClientSessionRuntime> mRuntime;
            SemanticInputProvider& mInput;
            PresentationProvider& mPresentation;
            ConnectionStatusProvider& mStatus;
            bool mClosed = false;
            std::optional<CommandSequence> mPendingCellTransition;
            std::optional<FixtureCellTransition> mDeferredCellTransition;
            MotionIntentTracker mMotion;
        };
    }

    std::unique_ptr<EngineCoordinator> makeCoordinator(std::unique_ptr<TransportRuntime> transport,
        std::unique_ptr<MonotonicClock> clock, std::unique_ptr<ClientSessionRuntime> runtime,
        SemanticInputProvider& input, PresentationProvider& presentation, ConnectionStatusProvider& status) noexcept
    {
        if (!transport || !clock || !runtime)
            return {};
        return std::make_unique<Coordinator>(
            std::move(transport), std::move(clock), std::move(runtime), input, presentation, status);
    }
}
