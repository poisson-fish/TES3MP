#include "desktop_automation.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"

#include <limits>
#include <ranges>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        constexpr std::uint64_t Second = 1'000'000'000;
        constexpr std::uint64_t ReconnectCadence = 1'050'000'000;
        constexpr std::uint64_t FlowDuration = 8 * Second;
        constexpr std::uint64_t SoakDuration = 60 * Second;
        constexpr std::int64_t AutomationSpeed = 4096;
        constexpr CellId Interior = CellId::interior(CellSpaceId::fromValue(7).value());
        constexpr CellId Exterior = CellId::exterior(CellSpaceId::fromValue(8).value(), 0, 0);

        std::optional<MonotonicInstant> add(MonotonicInstant value, std::uint64_t duration) noexcept
        {
            if (duration > std::numeric_limits<std::uint64_t>::max() - value.nanoseconds())
                return std::nullopt;
            return MonotonicInstant::fromNanoseconds(value.nanoseconds() + duration);
        }

        const char* statusName(ConnectionStatus status) noexcept
        {
            switch (status)
            {
                case ConnectionStatus::ProtocolRejected:
                    return "protocol_rejected";
                case ConnectionStatus::AuthenticationRejected:
                    return "authentication_rejected";
                case ConnectionStatus::TimedOut:
                    return "timed_out";
                case ConnectionStatus::TransportFailed:
                    return "transport_failed";
                case ConnectionStatus::Disconnected:
                    return "disconnected";
                case ConnectionStatus::Reconnecting:
                    return "reconnecting";
                case ConnectionStatus::Resumed:
                    return "resumed";
                case ConnectionStatus::ResumeFailed:
                    return "resume_failed";
                case ConnectionStatus::ContentMappingFailed:
                    return "content_mapping_failed";
                case ConnectionStatus::PresentationFailed:
                    return "presentation_failed";
            }
            return "unknown";
        }
    }

    std::optional<DesktopAutomationRole> parseDesktopAutomationRole(std::string_view value) noexcept
    {
        if (value == "flow-one")
            return DesktopAutomationRole::FlowOne;
        if (value == "flow-two")
            return DesktopAutomationRole::FlowTwo;
        if (value == "reconnect")
            return DesktopAutomationRole::Reconnect;
        if (value == "soak-one")
            return DesktopAutomationRole::SoakOne;
        if (value == "soak-two")
            return DesktopAutomationRole::SoakTwo;
        return std::nullopt;
    }

    DesktopAutomation::DesktopAutomation(DesktopAutomationRole role, const std::filesystem::path& output,
        DesktopPresentation& presentation, ConnectionStatusProvider& status)
        : mRole(role)
        , mOutput(output, std::ios::out | std::ios::trunc)
        , mPresentation(presentation)
        , mStatus(status)
    {
        if (mOutput)
        {
            mOutput << "{\"event\":\"phase8_desktop_started\",\"role\":\"" << roleName() << "\"}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
    }

    CellTransitionCapture DesktopAutomation::captureCellTransition() noexcept
    {
        if (mRole != DesktopAutomationRole::FlowOne || !mNow || !mStartedAt || !mSelfCell)
            return {};
        const auto elapsed = mNow->nanoseconds() - mStartedAt->nanoseconds();
        if (!mSentExterior && mSawPeer && elapsed >= Second)
        {
            mSentExterior = true;
            return { ProviderResult::Accepted, FixtureCellTransition(Exterior) };
        }
        if (mSentExterior && !mSentInterior && *mSelfCell == Exterior && mExteriorAt
            && mNow->nanoseconds() - mExteriorAt->nanoseconds() >= Second)
        {
            mSentInterior = true;
            return { ProviderResult::Accepted, FixtureCellTransition(Interior) };
        }
        return {};
    }

    std::optional<PlayerMotionIntent> DesktopAutomation::sampleCurrentIntent() noexcept
    {
        if (!mStartedAt || !mNow || mRole == DesktopAutomationRole::Reconnect)
            return PlayerMotionIntent(LinearVelocity3(0, 0, 0));
        const auto moving = mNow->nanoseconds() - mStartedAt->nanoseconds() < 2 * Second;
        const auto x = (mRole == DesktopAutomationRole::FlowOne || mRole == DesktopAutomationRole::SoakOne) && moving
            ? AutomationSpeed
            : 0;
        const auto y = (mRole == DesktopAutomationRole::FlowTwo || mRole == DesktopAutomationRole::SoakTwo) && moving
            ? AutomationSpeed
            : 0;
        return PlayerMotionIntent(LinearVelocity3(x, y, 0));
    }

    ProviderResult DesktopAutomation::applyAuthoritative(const LatestWinsSnapshot& snapshot,
        std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection,
        MonotonicInstant receivedAt) noexcept
    {
        const auto applied
            = mPresentation.applyAuthoritative(snapshot, observedPlayers, allowLocalCellCorrection, receivedAt);
        if (applied != ProviderResult::Accepted)
            return applied;
        ++mSnapshots;
        const auto self = std::ranges::find_if(snapshot.view().entries(), [&](const auto& entry) {
            return entry.playerId() == snapshot.header().targetPlayerId()
                && entry.entityId() == snapshot.header().targetEntityId();
        });
        if (self == snapshot.view().entries().end())
            return ProviderResult::PresentationFailed;
        if (!mInitialPosition)
            mInitialPosition = self->transform().position();
        else if (self->transform().position() != *mInitialPosition)
            mMoved = true;
        mSelfCell = self->transform().cell();
        if (*mSelfCell == Exterior && !mExteriorAt)
            mExteriorAt = receivedAt;
        const bool hasPeer = std::ranges::any_of(observedPlayers, [&](const ObservedPlayer& observed) {
            return observed.playerId != snapshot.header().targetPlayerId()
                || observed.entityId != snapshot.header().targetEntityId();
        });
        if (hasPeer)
        {
            if (mSawLeave)
                mSawReturn = true;
            mSawPeer = true;
        }
        else if (mSawPeer)
            mSawLeave = true;
        if (mAwaitingResumeSnapshot)
        {
            mAwaitingResumeSnapshot = false;
            mResumeSnapshot = true;
            mReadyToDisconnect = true;
        }
        else if (mRole == DesktopAutomationRole::Reconnect && mSnapshots == 1)
            mReadyToDisconnect = true;
        return ProviderResult::Accepted;
    }

    ProviderResult DesktopAutomation::advance(MonotonicInstant now) noexcept
    {
        mNow = now;
        if (!mStartedAt)
            mStartedAt = now;
        const auto applied = mPresentation.advance(now);
        if (applied != ProviderResult::Accepted)
            return applied;
        if (mFinished)
            return ProviderResult::Accepted;
        const auto elapsed = now.nanoseconds() - mStartedAt->nanoseconds();
        if (mRole == DesktopAutomationRole::Reconnect)
        {
            if (mReadyToDisconnect && !mNextDisconnect)
                mNextDisconnect = add(now, ReconnectCadence);
            if (mResumes == 32 && mResumeSnapshot)
                finish(true);
        }
        else if ((mRole == DesktopAutomationRole::SoakOne || mRole == DesktopAutomationRole::SoakTwo)
            && elapsed >= SoakDuration)
            finish(mSawPeer && mMoved);
        else if ((mRole == DesktopAutomationRole::FlowOne || mRole == DesktopAutomationRole::FlowTwo)
            && elapsed >= FlowDuration)
        {
            const bool cellFlow = mRole == DesktopAutomationRole::FlowOne
                ? mSentExterior && mSentInterior && mSawLeave && mSawReturn
                : mSawLeave && mSawReturn;
            finish(mMoved && mSawPeer && cellFlow);
        }
        return ProviderResult::Accepted;
    }

    void DesktopAutomation::clear() noexcept
    {
        mPresentation.clear();
    }

    void DesktopAutomation::report(ConnectionStatus status) noexcept
    {
        writeStatus(status);
        if (status == ConnectionStatus::Resumed)
        {
            ++mResumes;
            mAwaitingResumeSnapshot = true;
            mResumeSnapshot = false;
            mNextDisconnect.reset();
        }
        else if (status != ConnectionStatus::Reconnecting)
            finish(false);
        mStatus.report(status);
    }

    bool DesktopAutomation::disconnectRequested() noexcept
    {
        if (mRole != DesktopAutomationRole::Reconnect || !mReadyToDisconnect || !mNow || !mNextDisconnect
            || *mNow < *mNextDisconnect || mResumes >= 32)
            return false;
        mReadyToDisconnect = false;
        mNextDisconnect.reset();
        return true;
    }

    const char* DesktopAutomation::roleName() const noexcept
    {
        switch (mRole)
        {
            case DesktopAutomationRole::FlowOne:
                return "flow-one";
            case DesktopAutomationRole::FlowTwo:
                return "flow-two";
            case DesktopAutomationRole::Reconnect:
                return "reconnect";
            case DesktopAutomationRole::SoakOne:
                return "soak-one";
            case DesktopAutomationRole::SoakTwo:
                return "soak-two";
        }
        return "unknown";
    }

    void DesktopAutomation::writeStatus(ConnectionStatus status) noexcept
    {
        if (!mOutput || mEvidenceEvents >= MaximumEvidenceEvents)
            return;
        mOutput << "{\"event\":\"phase8_desktop_status\",\"role\":\"" << roleName() << "\",\"status\":\""
                << statusName(status) << "\"}\n";
        mOutput.flush();
        ++mEvidenceEvents;
    }

    void DesktopAutomation::finish(bool success) noexcept
    {
        if (mFinished)
            return;
        mFinished = true;
        if (mOutput && mEvidenceEvents < MaximumEvidenceEvents)
        {
            mOutput << "{\"event\":\"phase8_desktop_complete\",\"role\":\"" << roleName()
                    << "\",\"success\":" << (success ? "true" : "false") << ",\"snapshots\":" << mSnapshots
                    << ",\"resumes\":" << mResumes << ",\"saw_peer\":" << (mSawPeer ? "true" : "false")
                    << ",\"saw_leave\":" << (mSawLeave ? "true" : "false")
                    << ",\"saw_return\":" << (mSawReturn ? "true" : "false")
                    << ",\"moved\":" << (mMoved ? "true" : "false") << "}\n";
            mOutput.flush();
            ++mEvidenceEvents;
        }
        try
        {
            MWBase::Environment::get().getStateManager()->requestQuit();
        }
        catch (...)
        {
        }
    }
}
