#ifndef OPENMW_TES3MP_DESKTOP_AUTOMATION_HPP
#define OPENMW_TES3MP_DESKTOP_AUTOMATION_HPP

#include "desktop_providers.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

namespace TES3MP::OpenMWAdapter
{
    enum class DesktopAutomationRole
    {
        FlowOne,
        FlowTwo,
        Reconnect,
        SoakOne,
        SoakTwo,
        CaptureOne,
        CaptureTwo,
        ActorReconnect,
        ActorAuth,
    };

    std::optional<DesktopAutomationRole> parseDesktopAutomationRole(std::string_view value) noexcept;

    class DesktopAutomation final : public SemanticInputProvider,
                                    public PresentationProvider,
                                    public ConnectionStatusProvider,
                                    public ConnectionControlProvider
    {
    public:
        static constexpr std::size_t MaximumEvidenceEvents = 128;

        DesktopAutomation(DesktopAutomationRole role, const std::filesystem::path& output,
            ContentManifest contentManifest, DesktopPresentation& presentation, ConnectionStatusProvider& status);

        bool valid() const noexcept { return mOutput.is_open(); }
        CellTransitionCapture captureCellTransition() noexcept override;
        std::optional<LocomotionIntent> sampleCurrentIntent() noexcept override;
        ProviderResult applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection, MonotonicInstant receivedAt,
            const std::optional<LocalLocomotionReconciliation>& localReconciliation = std::nullopt) noexcept override;
        ProviderResult applyActors(const LatestWinsActorSnapshot& snapshot,
            std::span<const ActorInterestMember> observedActors, MonotonicInstant receivedAt) noexcept override;
        ProviderResult advance(MonotonicInstant now) noexcept override;
        void clear() noexcept override;
        void report(ConnectionStatus status) noexcept override;
        bool disconnectRequested() noexcept override;
        std::optional<ResyncReason> resyncRequested() noexcept override;
        void resyncCompleted() noexcept override;

    private:
        const char* roleName() const noexcept;
        void writeStatus(ConnectionStatus status) noexcept;
        void writeActorSample(const ActorSpatialSnapshot& actor) noexcept;
        void finish(bool success) noexcept;

        DesktopAutomationRole mRole;
        CellId mInterior;
        CellId mExterior;
        std::ofstream mOutput;
        DesktopPresentation& mPresentation;
        ConnectionStatusProvider& mStatus;
        std::size_t mEvidenceEvents = 0;
        std::size_t mSnapshots = 0;
        std::size_t mResumes = 0;
        std::size_t mActorSnapshots = 0;
        std::size_t mActorEvidenceSamples = 0;
        std::size_t mActorResumeSnapshots = 0;
        std::optional<MonotonicInstant> mStartedAt;
        std::optional<MonotonicInstant> mNow;
        std::optional<MonotonicInstant> mNextDisconnect;
        std::optional<MonotonicInstant> mExteriorAt;
        std::optional<Position3> mInitialPosition;
        std::optional<CellId> mSelfCell;
        std::optional<PlayerId> mPlayerId;
        std::optional<EntityId> mPlayerEntityId;
        std::optional<ActorSpatialSnapshot> mFirstActor;
        std::optional<ActorSpatialSnapshot> mLastActor;
        bool mSawPeer = false;
        bool mSawLeave = false;
        bool mSawReturn = false;
        bool mMoved = false;
        bool mSentExterior = false;
        bool mSentInterior = false;
        bool mReadyToDisconnect = false;
        bool mAwaitingResumeSnapshot = false;
        bool mResumeSnapshot = false;
        bool mAwaitingActorResumeSnapshot = false;
        bool mActorStable = true;
        bool mActorMoved = false;
        bool mActorSawLeave = false;
        bool mActorSawReturn = false;
        bool mPlayerIdentityStable = true;
        bool mResyncReady = false;
        bool mResyncIssued = false;
        bool mResyncDone = false;
        bool mActorAppliedAfterResync = false;
        bool mFinished = false;
    };
}

#endif
