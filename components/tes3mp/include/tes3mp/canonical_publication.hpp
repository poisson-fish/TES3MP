#ifndef TES3MP_CANONICAL_PUBLICATION_HPP
#define TES3MP_CANONICAL_PUBLICATION_HPP

#include "canonical_checksum.hpp"
#include "canonical_state.hpp"
#include "server_command_intake.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace TES3MP
{
    struct CanonicalSessionJoinedRecord
    {
        CanonicalStateVersion stateVersion;
        ServerTick commitTick;
        CanonicalSessionProgress session;
        CanonicalPlayerEntityState player;
        friend bool operator==(const CanonicalSessionJoinedRecord&, const CanonicalSessionJoinedRecord&) noexcept = default;
    };

    struct CanonicalSpatialTickRecord
    {
        CanonicalStateVersion stateVersion;
        ServerTick commitTick;
        CanonicalPlayerEntityState player;
        friend bool operator==(const CanonicalSpatialTickRecord&, const CanonicalSpatialTickRecord&) noexcept = default;
    };

    class CanonicalStateChangeRecord
    {
    public:
        CanonicalStateChangeRecord(const CanonicalStateChangeRecord&) noexcept = default;
        CanonicalStateChangeRecord& operator=(const CanonicalStateChangeRecord&) noexcept = default;
        CanonicalStateChangeRecord(CanonicalStateChangeRecord&&) noexcept = default;
        CanonicalStateChangeRecord& operator=(CanonicalStateChangeRecord&&) noexcept = default;

        constexpr CanonicalStateVersion stateVersion() const noexcept { return mStateVersion; }
        constexpr WriterAdmissionStamp stamp() const noexcept { return mStamp; }
        constexpr ServerTick commitTick() const noexcept { return mStamp.eligibleServerTick(); }
        constexpr SessionId sessionId() const noexcept { return mSessionId; }
        constexpr SessionGeneration sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr CommandSequence commandSequence() const noexcept { return mCommandSequence; }
        constexpr CommandId commandId() const noexcept { return mCommandId; }
        constexpr CommandDisposition disposition() const noexcept { return mDisposition; }
        constexpr const CanonicalSessionProgress& sessionReplacement() const noexcept { return mSessionReplacement; }
        constexpr const std::optional<CanonicalPlayerEntityState>& playerReplacement() const noexcept
        {
            return mPlayerReplacement;
        }

        friend bool operator==(const CanonicalStateChangeRecord&, const CanonicalStateChangeRecord&) noexcept = default;

    private:
        friend class CanonicalCommandReducer;

        CanonicalStateChangeRecord(CanonicalStateVersion stateVersion, WriterAdmissionStamp stamp, SessionId sessionId,
            SessionGeneration sessionGeneration, CommandSequence commandSequence, CommandId commandId,
            CommandDisposition disposition, CanonicalSessionProgress sessionReplacement,
            std::optional<CanonicalPlayerEntityState> playerReplacement) noexcept
            : mStateVersion(stateVersion)
            , mStamp(stamp)
            , mSessionId(sessionId)
            , mSessionGeneration(sessionGeneration)
            , mCommandSequence(commandSequence)
            , mCommandId(commandId)
            , mDisposition(disposition)
            , mSessionReplacement(sessionReplacement)
            , mPlayerReplacement(playerReplacement)
        {
        }
        CanonicalStateVersion mStateVersion;
        WriterAdmissionStamp mStamp;
        SessionId mSessionId;
        SessionGeneration mSessionGeneration;
        CommandSequence mCommandSequence;
        CommandId mCommandId;
        CommandDisposition mDisposition;
        CanonicalSessionProgress mSessionReplacement;
        std::optional<CanonicalPlayerEntityState> mPlayerReplacement;
    };

    class CanonicalStatePublication
    {
    public:
        constexpr CanonicalStateVersion stateVersion() const noexcept { return mStateVersion; }
        constexpr ServerTick checkpointTick() const noexcept { return mCheckpointTick; }
        constexpr CanonicalChecksum checksum() const noexcept { return mChecksum; }
        const CanonicalServerState& state() const noexcept { return *mState; }
        std::span<const CanonicalStateChangeRecord> changes() const noexcept { return mChanges; }
        std::span<const CanonicalSessionJoinedRecord> joinedSessions() const noexcept { return mJoinedSessions; }
        std::span<const CanonicalSpatialTickRecord> spatialTicks() const noexcept { return mSpatialTicks; }

        friend bool operator==(const CanonicalStatePublication&, const CanonicalStatePublication&) noexcept;

    private:
        friend class CanonicalCommandReducer;

        CanonicalStatePublication(CanonicalStateVersion stateVersion, ServerTick checkpointTick,
            std::shared_ptr<const CanonicalServerState> state, std::vector<CanonicalStateChangeRecord> changes);

        CanonicalStateVersion mStateVersion;
        ServerTick mCheckpointTick;
        CanonicalChecksum mChecksum;
        std::shared_ptr<const CanonicalServerState> mState;
        std::vector<CanonicalStateChangeRecord> mChanges;
        std::vector<CanonicalSessionJoinedRecord> mJoinedSessions;
        std::vector<CanonicalSpatialTickRecord> mSpatialTicks;
    };

    enum class CanonicalPublicationReadAction : std::uint8_t
    {
        NoChange,
        ApplyContiguousChanges,
        ReplaceFromSnapshot,
        OlderPublication,
    };

    CanonicalPublicationReadAction classifyCanonicalPublication(
        CanonicalStateVersion lastConsumedVersion, const CanonicalStatePublication& publication) noexcept;

    bool canReserveCanonicalStateVersions(CanonicalStateVersion currentVersion, std::size_t maximumChanges) noexcept;
}

#endif
