#include "adapter.hpp"
#include "providers.hpp"

#include <cstdlib>
#include <optional>

namespace
{
    void require(bool value)
    {
        if (!value)
            std::abort();
    }

    class Input final : public TES3MP::OpenMWAdapter::SemanticInputProvider
    {
    public:
        std::optional<TES3MP::PlayerMotionIntent> sampleCurrentIntent() noexcept override
        {
            ++calls;
            return TES3MP::PlayerMotionIntent(TES3MP::LinearVelocity3(1, 2, 3));
        }
        unsigned calls = 0;
    };

    class Presentation final : public TES3MP::OpenMWAdapter::PresentationProvider
    {
    public:
        void applyAuthoritative(
            const TES3MP::LatestWinsSnapshot&, std::span<const TES3MP::ObservedPlayer>) noexcept override
        {
            ++calls;
        }
        unsigned calls = 0;
    };

    class Coordinator final : public TES3MP::OpenMWAdapter::EngineCoordinator
    {
    public:
        void frame(float duration) noexcept override
        {
            ++calls;
            lastDuration = duration;
        }
        unsigned calls = 0;
        float lastDuration = 0;
    };
}

int main()
{
    Input input;
    auto intent = input.sampleCurrentIntent();
    require(input.calls == 1 && intent && intent->desiredVelocity() == TES3MP::LinearVelocity3(1, 2, 3));
    Presentation presentation;
    Coordinator coordinator;
    coordinator.frame(0.25f);
    require(coordinator.calls == 1 && coordinator.lastDuration == 0.25f);
    require(!TES3MP::OpenMWAdapter::makeCoordinator({}, {}, {}, input, presentation));
}
