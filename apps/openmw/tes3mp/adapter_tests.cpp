#include "adapter.hpp"
#include "desktop_connection.hpp"
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
        TES3MP::OpenMWAdapter::CellTransitionCapture captureCellTransition() noexcept override { return {}; }
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
        TES3MP::OpenMWAdapter::ProviderResult applyAuthoritative(const TES3MP::LatestWinsSnapshot&,
            std::span<const TES3MP::ObservedPlayer>, bool) noexcept override
        {
            ++calls;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        void clear() noexcept override { ++clears; }
        unsigned calls = 0;
        unsigned clears = 0;
    };

    class Status final : public TES3MP::OpenMWAdapter::ConnectionStatusProvider
    {
    public:
        void report(TES3MP::OpenMWAdapter::ConnectionStatus value) noexcept override { last = value; }
        std::optional<TES3MP::OpenMWAdapter::ConnectionStatus> last;
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
    Status status;
    Coordinator coordinator;
    coordinator.frame(0.25f);
    require(coordinator.calls == 1 && coordinator.lastDuration == 0.25f);
    require(!TES3MP::OpenMWAdapter::makeCoordinator({}, {}, {}, input, presentation, status));
    require(std::get<TES3MP::OpenMWAdapter::DesktopConnectionFailure>(
                TES3MP::OpenMWAdapter::makeDesktopCoordinator(
                    "", 25560, 1000, {}, input, presentation, status))
        == TES3MP::OpenMWAdapter::DesktopConnectionFailure::InvalidEndpoint);
    require(std::get<TES3MP::OpenMWAdapter::DesktopConnectionFailure>(
                TES3MP::OpenMWAdapter::makeDesktopCoordinator(
                    "127.0.0.1", 25560, 0, {}, input, presentation, status))
        == TES3MP::OpenMWAdapter::DesktopConnectionFailure::InvalidTimeout);
}
