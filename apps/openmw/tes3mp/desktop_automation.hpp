#ifndef OPENMW_TES3MP_DESKTOP_AUTOMATION_HPP
#define OPENMW_TES3MP_DESKTOP_AUTOMATION_HPP

#include "desktop_providers.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace TES3MP::OpenMWAdapter
{
    class EngineCoordinator;

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
        WeatherOne,
        WeatherTwo,
        WeatherReconnect,
        WeatherSlow,
        WaitOne,
        WaitTwo,
        WaitAnchor,
        WaitReconnect,
        WaitSlowAnchor,
        WaitSlow,
        SecurityPick,
        SecurityProbe,
        MagicItem,
        MagicSpellCaster,
        MagicSpellTarget,
        NativePut,
        NativeTake,
        NativeRecoverOne,
        NativeRecoverTwo,
        NativeTraversal,
    };

    std::optional<DesktopAutomationRole> parseDesktopAutomationRole(std::string_view value) noexcept;

    class DesktopAutomation final : public SemanticInputProvider,
                                    public PresentationProvider,
                                    public ConnectionStatusProvider,
                                    public ConnectionControlProvider
    {
    public:
        static constexpr std::size_t MaximumEvidenceEvents = 2048;

        DesktopAutomation(DesktopAutomationRole role, const std::filesystem::path& output,
            ContentManifest contentManifest, DesktopPresentation& presentation, ConnectionStatusProvider& status);

        bool valid() const noexcept { return mOutput.is_open(); }
        CellTransitionCapture captureCellTransition() noexcept override;
        std::optional<LocomotionIntent> sampleCurrentIntent() noexcept override;
        std::optional<ObjectInteractionCapture> captureObjectInteraction() noexcept override;
        std::optional<MagicUseCapture> captureMagicUse() noexcept override;
        std::optional<InventoryTransactionCapture> captureInventoryTransaction() noexcept override;
        void clearSessionState() noexcept override;
        void setDesktopInput(DesktopSemanticInput* input) noexcept { mDesktopInput = input; }
        void setCoordinator(EngineCoordinator* coordinator) noexcept { mCoordinator = coordinator; }
        ProviderResult applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection, MonotonicInstant receivedAt,
            const std::optional<LocalLocomotionReconciliation>& localReconciliation = std::nullopt) noexcept override;
        ProviderResult applyActors(const LatestWinsActorSnapshot& snapshot,
            std::span<const ActorInterestMember> observedActors, MonotonicInstant receivedAt) noexcept override;
        ProviderResult applyInteractiveObjects(
            const ReliableInteractiveObjectInterestBaseline& baseline, MonotonicInstant receivedAt) noexcept override;
        ProviderResult applyInventory(const ReliablePlayerInventoryBaseline& player,
            std::span<const ReliableContainerInventoryBaseline> containers,
            const ReliableGroundItemBaseline& groundItems, const LatestWinsEquipmentSnapshot& equipment,
            MonotonicInstant receivedAt) noexcept override;
        ProviderResult applyNativeDoors(const ReliableGroundItemBaseline& groundItems,
            MonotonicInstant receivedAt) noexcept override;
        ProviderResult applyCombat(const LatestWinsCombatSnapshot& snapshot,
            std::span<const ReliableCombatEventBatch> events, MonotonicInstant receivedAt) noexcept override;
        ProviderResult applyWeather(std::span<const WeatherRegionSnapshot> regions, ServerTick serverTick,
            MonotonicInstant receivedAt) noexcept override;
        ProviderResult applyWorldTime(
            const ReliableWorldTimeState& state, MonotonicInstant receivedAt) noexcept override;
        std::optional<ObjectRevision> observedObjectRevision(InteractiveObjectId id) const noexcept override;
        std::optional<bool> nativeDoorObstruction(const NativeDoorSnapshot& door) const noexcept override
        { return mPresentation.nativeDoorObstruction(door); }
        ProviderResult advance(MonotonicInstant now) noexcept override;
        void clear() noexcept override;
        void report(ConnectionStatus status) noexcept override;
        bool disconnectRequested() noexcept override;
        std::uint64_t reconnectDelayNanoseconds() noexcept override;
        std::optional<ResyncReason> resyncRequested() noexcept override;
        void resyncCompleted() noexcept override;

    private:
        const char* roleName() const noexcept;
        void writeStatus(ConnectionStatus status) noexcept;
        void writeActorSample(const ActorSpatialSnapshot& actor) noexcept;
        void writeWeatherSample(std::span<const WeatherRegionSnapshot> regions, ServerTick serverTick) noexcept;
        void writeWaitRestSample(const ReliableWorldTimeState& state) noexcept;
        void finish(bool success) noexcept;
        bool nativeInventoryRole() const noexcept;
        void advanceNativeInventory(MonotonicInstant now);
        void writeNativeInventory(std::string_view event);
        void advanceNativeTraversal(MonotonicInstant now);
        void writeNativeTraversal(std::string_view event);

        DesktopAutomationRole mRole;
        CellId mInterior;
        CellId mExterior;
        std::ofstream mOutput;
        DesktopPresentation& mPresentation;
        ConnectionStatusProvider& mStatus;
        EngineCoordinator* mCoordinator = nullptr;
        DesktopSemanticInput* mDesktopInput = nullptr;
        std::filesystem::path mTraversalControl;
        std::uint64_t mTraversalSequence = 0;
        bool mTraversalDisconnectOnly = false;
        std::optional<ReliableGroundItemBaseline> mTraversalGround;
        std::vector<NativeDoorSnapshot> mPresentedNativeDoors;
        std::optional<std::uint32_t> mNativePlayerCount;
        std::optional<std::uint32_t> mNativeContainerCount;
        std::optional<ContainerId> mNativeContainerId;
        std::uint64_t mNativeRevision = 0;
        std::vector<CanonicalItemStack> mNativePlayerStacks, mNativeContainerStacks, mNativeExpectedPlayer;
        std::optional<CanonicalItemStack> mNativeSelected;
        std::uint32_t mNativeInitialCount = 0;
        std::uint64_t mNativeSubmittedRevision = 0;
        unsigned mNativeStage = 0;
        bool mNativeInventoryAfterResume = false;
        std::optional<MonotonicInstant> mNativeStageAt;
        std::size_t mEvidenceEvents = 0;
        std::size_t mSnapshots = 0;
        std::size_t mResumes = 0;
        std::size_t mActorSnapshots = 0;
        std::size_t mActorEvidenceSamples = 0;
        std::size_t mActorResumeSnapshots = 0;
        std::size_t mWeatherPresentations = 0;
        std::size_t mWeatherDuplicates = 0;
        std::size_t mWeatherEvidenceSamples = 0;
        std::size_t mWorldTimePresentations = 0;
        std::size_t mWorldTimeDuplicates = 0;
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
        std::vector<WeatherRegionSnapshot> mLastWeather;
        std::optional<ServerTick> mLastWeatherTick;
        std::optional<WeatherRevision> mWeatherRevisionBeforeDisconnect;
        std::optional<ServerTick> mWeatherTickBeforeDisconnect;
        std::optional<ReliableWorldTimeState> mLastWorldTime;
        std::optional<CanonicalWorldTimeState> mInitialWorldTime;
        std::optional<WorldTimeRevision> mWorldTimeRevisionBeforeDisconnect;
        std::optional<InteractiveObjectId> mSecurityObject;
        std::optional<ObjectRevision> mInitialSecurityObjectRevision;
        std::optional<ObjectRevision> mSecurityObjectRevision;
        std::optional<ItemStackId> mSecurityTool;
        std::optional<std::uint32_t> mInitialSecurityToolCondition;
        std::optional<std::uint32_t> mSecurityToolCondition;
        std::optional<InventoryRevision> mSecurityInventoryRevision;
        std::optional<CombatRevision> mSecurityCombatRevision;
        std::optional<float> mInitialSecurityProgress;
        std::optional<float> mSecurityProgress;
        std::optional<ActorId> mMagicActor;
        std::optional<PlayerId> mMagicPlayer;
        std::optional<ActiveMagicEffectId> mMagicEffect;
        std::optional<ItemStackId> mMagicItem;
        std::optional<InventoryRevision> mMagicInventoryRevision;
        std::optional<CombatRevision> mMagicCasterRevision;
        std::optional<CombatRevision> mMagicTargetRevision;
        std::optional<ServerTick> mMagicSourceTick;
        std::optional<std::uint32_t> mInitialMagicCharge;
        std::optional<std::uint32_t> mMagicCharge;
        std::optional<float> mInitialMagicTargetFatigue;
        std::optional<float> mMagicTargetFatigue;
        std::optional<float> mMinimumMagicTargetFatigue;
        std::optional<float> mInitialEnchantProgress;
        std::optional<float> mEnchantProgress;
        float mMagicAppliedDelta = 0.f;
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
        bool mSawWeatherTransition = false;
        bool mSawWeatherCompletion = false;
        bool mWeatherConvergedAfterResume = false;
        bool mSlowPeerStalled = false;
        bool mSlowPeerRecovered = false;
        bool mWaitRestSubmitted = false;
        bool mWaitRestApplied = false;
        bool mWorldTimeConvergedAfterResume = false;
        bool mSecuritySubmitted = false;
        bool mSecurityUnlocked = false;
        bool mSecurityDisarmed = false;
        bool mSecurityResumeRequested = false;
        bool mSecurityObjectAfterResume = false;
        bool mSecurityInventoryAfterResume = false;
        bool mSecurityCombatAfterResume = false;
        bool mMagicSubmitted = false;
        bool mMagicEventPresented = false;
        bool mMagicResumeRequested = false;
        bool mMagicInventoryAfterResume = false;
        bool mMagicCombatAfterResume = false;
        bool mMagicEffectStarted = false;
        bool mMagicEffectUpdated = false;
        bool mMagicEffectEnded = false;
        bool mMagicEffectActiveAfterResume = false;
        bool mFinished = false;
    };
}

#endif
