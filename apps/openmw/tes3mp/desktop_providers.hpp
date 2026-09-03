#ifndef OPENMW_TES3MP_DESKTOP_PROVIDERS_HPP
#define OPENMW_TES3MP_DESKTOP_PROVIDERS_HPP

#include "providers.hpp"
#include "remote_motion.hpp"

#include <memory>
#include <string>

namespace TES3MP::OpenMWAdapter
{
    struct DesktopFixtureMapping
    {
        std::string interiorCell;
        std::string exteriorWorldspace;
        std::string avatarNpc;
    };

    class DesktopSemanticInput final : public SemanticInputProvider
    {
    public:
        DesktopSemanticInput();
        ~DesktopSemanticInput() override;
        void configure(DesktopFixtureMapping mapping);
        CellTransitionCapture captureCellTransition() noexcept override;
        std::optional<PlayerMotionIntent> sampleCurrentIntent() noexcept override;

    private:
        class Impl;
        std::unique_ptr<Impl> mImpl;
    };

    class DesktopPresentation final : public PresentationProvider
    {
    public:
        explicit DesktopPresentation(RemoteMotionMetricSink& metrics);
        ~DesktopPresentation() override;
        void configure(DesktopFixtureMapping mapping);
        ProviderResult applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection,
            MonotonicInstant receivedAt) noexcept override;
        ProviderResult advance(MonotonicInstant now) noexcept override;
        void clear() noexcept override;

    private:
        class Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
