#include "adapter.hpp"

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        class Coordinator final : public EngineCoordinator
        {
        public:
            Coordinator(std::unique_ptr<TransportRuntime> transport, std::unique_ptr<MonotonicClock> clock,
                std::unique_ptr<ClientSessionRuntime> runtime, SemanticInputProvider& input,
                PresentationProvider& presentation) noexcept
                : mTransport(std::move(transport))
                , mClock(std::move(clock))
                , mRuntime(std::move(runtime))
                , mInput(input)
                , mPresentation(presentation)
            {
            }

            ~Coordinator() override { mRuntime->close(); }

            void frame(float) noexcept override
            {
                if (mClosed)
                    return;
                const auto advanced = mRuntime->advance();
                if (advanced.result != ClientRuntimeResult::Accepted)
                {
                    mClosed = true;
                    return;
                }
                const auto& snapshot = mRuntime->session().stateMachine().confirmedSnapshot();
                if ((advanced.snapshotApplied || advanced.observationApplied) && snapshot)
                    mPresentation.applyAuthoritative(*snapshot, mRuntime->session().observedPlayers());
                if (snapshot)
                {
                    if (auto intent = mInput.sampleCurrentIntent();
                        intent && mRuntime->queueMotionIntent(std::move(*intent)) != ClientRuntimeResult::Accepted)
                    {
                        mRuntime->close();
                        mClosed = true;
                        return;
                    }
                }
                if (mRuntime->flushOutbound() != ClientRuntimeResult::Accepted)
                    mClosed = true;
            }

        private:
            std::unique_ptr<TransportRuntime> mTransport;
            std::unique_ptr<MonotonicClock> mClock;
            std::unique_ptr<ClientSessionRuntime> mRuntime;
            SemanticInputProvider& mInput;
            PresentationProvider& mPresentation;
            bool mClosed = false;
        };
    }

    std::unique_ptr<EngineCoordinator> makeCoordinator(std::unique_ptr<TransportRuntime> transport,
        std::unique_ptr<MonotonicClock> clock, std::unique_ptr<ClientSessionRuntime> runtime,
        SemanticInputProvider& input, PresentationProvider& presentation) noexcept
    {
        if (!transport || !clock || !runtime)
            return {};
        return std::make_unique<Coordinator>(
            std::move(transport), std::move(clock), std::move(runtime), input, presentation);
    }
}
