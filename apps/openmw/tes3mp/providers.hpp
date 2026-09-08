#ifndef OPENMW_TES3MP_PROVIDERS_HPP
#define OPENMW_TES3MP_PROVIDERS_HPP

#include <tes3mp/actor_replication.hpp>
#include <tes3mp/client_locomotion.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/interactive_object_replication.hpp>
#include <tes3mp/protocol_exchange.hpp>
#include <tes3mp/protocol_pose.hpp>

#include <optional>
#include <span>

namespace TES3MP::OpenMWAdapter
{
    enum class ConnectionStatus
    {
        ProtocolRejected,
        AuthenticationRejected,
        TimedOut,
        TransportFailed,
        Disconnected,
        Reconnecting,
        Resumed,
        ResumeFailed,
        ContentMappingFailed,
        PresentationFailed,
    };

    enum class ProviderResult
    {
        Accepted,
        ContentMappingFailed,
        PresentationFailed,
    };

    struct CellTransitionCapture
    {
        ProviderResult result = ProviderResult::Accepted;
        std::optional<CellTransition> transition;
    };

    struct LocalVrPose
    {
        VrTrackedTransform head;
        std::optional<VrTrackedTransform> leftHand;
        std::optional<VrTrackedTransform> rightHand;
    };

    class ConnectionStatusProvider
    {
    public:
        virtual ~ConnectionStatusProvider() = default;
        virtual void report(ConnectionStatus status) noexcept = 0;
    };

    class ConnectionControlProvider
    {
    public:
        virtual ~ConnectionControlProvider() = default;
        virtual bool disconnectRequested() noexcept = 0;
        virtual std::optional<ResyncReason> resyncRequested() noexcept { return std::nullopt; }
        virtual void resyncCompleted() noexcept {}
    };

    class SemanticInputProvider
    {
    public:
        virtual ~SemanticInputProvider() = default;
        virtual CellTransitionCapture captureCellTransition() noexcept = 0;
        virtual std::optional<LocomotionIntent> sampleCurrentIntent() noexcept = 0;
    };

    class VrPoseInputProvider
    {
    public:
        virtual ~VrPoseInputProvider() = default;
        virtual std::optional<LocalVrPose> sampleVrPose() noexcept = 0;
    };

    class PresentationProvider
    {
    public:
        virtual ~PresentationProvider() = default;
        virtual ProviderResult applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection, MonotonicInstant receivedAt,
            const std::optional<LocalLocomotionReconciliation>& localReconciliation = std::nullopt) noexcept = 0;
        virtual ProviderResult advance(MonotonicInstant now) noexcept = 0;
        virtual ProviderResult applyActors(
            const LatestWinsActorSnapshot&, std::span<const ActorInterestMember>, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyInteractiveObjects(
            const ReliableInteractiveObjectInterestBaseline&, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyVrPose(const ServerVrPoseSnapshot&, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyVrPoseWeight(EntityId, AuthorityEpoch, double) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual void clear() noexcept = 0;
    };
}

#endif
