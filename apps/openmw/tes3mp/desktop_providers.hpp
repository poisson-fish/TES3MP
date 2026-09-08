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

    struct DesktopActorPrototypeMapping
    {
        ActorPrototypeId id;
        std::string record;
    };

    struct DesktopInteractiveObjectMapping
    {
        InteractiveObjectId id;
        std::uint32_t refNumIndex = 0;
        std::int32_t refNumContentFile = -1;
    };

    struct DesktopContentMapping
    {
        static std::optional<DesktopContentMapping> create(ContentManifest manifest,
            std::span<const DesktopCellSpaceMapping> cellSpaces, AppearanceId appearanceId,
            std::string avatarNpc, std::span<const DesktopActorPrototypeMapping> actorPrototypes = {},
            std::span<const DesktopInteractiveObjectMapping> interactiveObjects = {});

        ContentManifest manifest;
        std::vector<DesktopCellSpaceMapping> cellSpaces;
        AppearanceId appearanceId;
        std::string avatarNpc;
        std::vector<DesktopActorPrototypeMapping> actorPrototypes;
        std::vector<DesktopInteractiveObjectMapping> interactiveObjects;
    };

}

namespace MWWorld
{
    class Ptr;
}

namespace TES3MP::OpenMWAdapter
{
    class DesktopSemanticInput final : public SemanticInputProvider
    {
    public:
        DesktopSemanticInput();
        ~DesktopSemanticInput() override;
        void configure(DesktopContentMapping mapping, const PresentationProvider* presentation = nullptr);
        CellTransitionCapture captureCellTransition() noexcept override;
        std::optional<LocomotionIntent> sampleCurrentIntent() noexcept override;
        std::optional<ObjectInteractionCapture> captureObjectInteraction() noexcept override;

        bool handleActivation(const MWWorld::Ptr& toActivate, const MWWorld::Ptr& player) noexcept;
        bool queueObjectActivation(const MWWorld::Ptr& doorPtr) noexcept;
        void clearSessionState() noexcept override;

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
            MonotonicInstant receivedAt,
            const std::optional<LocalLocomotionReconciliation>& localReconciliation = std::nullopt) noexcept override;
        ProviderResult advance(MonotonicInstant now) noexcept override;
        ProviderResult applyActors(const LatestWinsActorSnapshot& snapshot,
            std::span<const ActorInterestMember> observedActors, MonotonicInstant receivedAt) noexcept override;
        ProviderResult applyInteractiveObjects(
            const ReliableInteractiveObjectInterestBaseline& baseline, MonotonicInstant receivedAt) noexcept override;
        std::optional<ObjectRevision> observedObjectRevision(InteractiveObjectId id) const noexcept override;
        void clear() noexcept override;

    private:
        class Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
