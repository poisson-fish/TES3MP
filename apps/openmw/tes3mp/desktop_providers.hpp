#ifndef OPENMW_TES3MP_DESKTOP_PROVIDERS_HPP
#define OPENMW_TES3MP_DESKTOP_PROVIDERS_HPP

#include "providers.hpp"
#include "remote_motion.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TES3MP::OpenMWAdapter
{
    struct DesktopCellSpaceMapping
    {
        CellSpaceId id;
        CellSpaceKind kind;
        std::string record;
    };

    struct DesktopContentMapping
    {
        static std::optional<DesktopContentMapping> create(ContentManifest manifest,
            std::span<const DesktopCellSpaceMapping> cellSpaces, AppearanceId appearanceId,
            std::string avatarNpc);

        ContentManifest manifest;
        std::vector<DesktopCellSpaceMapping> cellSpaces;
        AppearanceId appearanceId;
        std::string avatarNpc;
    };

    class DesktopSemanticInput final : public SemanticInputProvider
    {
    public:
        DesktopSemanticInput();
        ~DesktopSemanticInput() override;
        void configure(DesktopContentMapping mapping);
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
        void configure(DesktopContentMapping mapping);
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
