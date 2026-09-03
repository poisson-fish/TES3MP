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
            DesktopPresentation& presentation, ConnectionStatusProvider& status);

        bool valid() const noexcept { return mOutput.is_open(); }
        CellTransitionCapture captureCellTransition() noexcept override;
        std::optional<PlayerMotionIntent> sampleCurrentIntent() noexcept override;
        ProviderResult applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection,
            MonotonicInstant receivedAt) noexcept override;
        ProviderResult advance(MonotonicInstant now) noexcept override;
        void clear() noexcept override;
        void report(ConnectionStatus status) noexcept override;
        bool disconnectRequested() noexcept override;

    private:
        const char* roleName() const noexcept;
        void writeStatus(ConnectionStatus status) noexcept;
        void finish(bool success) noexcept;

        DesktopAutomationRole mRole;
        std::ofstream mOutput;
        DesktopPresentation& mPresentation;
        ConnectionStatusProvider& mStatus;
        std::size_t mEvidenceEvents = 0;
        std::size_t mSnapshots = 0;
        std::size_t mResumes = 0;
        std::optional<MonotonicInstant> mStartedAt;
        std::optional<MonotonicInstant> mNow;
        std::optional<MonotonicInstant> mNextDisconnect;
        std::optional<MonotonicInstant> mExteriorAt;
        std::optional<Position3> mInitialPosition;
        std::optional<CellId> mSelfCell;
        bool mSawPeer = false;
        bool mSawLeave = false;
        bool mSawReturn = false;
        bool mMoved = false;
        bool mSentExterior = false;
        bool mSentInterior = false;
        bool mReadyToDisconnect = false;
        bool mAwaitingResumeSnapshot = false;
        bool mResumeSnapshot = false;
        bool mFinished = false;
    };
}

#endif
