#include <tes3mp/canonical_publication.hpp>

#include <limits>
#include <utility>

namespace TES3MP
{
    CanonicalStatePublication::CanonicalStatePublication(CanonicalStateVersion stateVersion, ServerTick checkpointTick,
        std::shared_ptr<const CanonicalServerState> state, std::vector<CanonicalStateChangeRecord> changes)
        : mStateVersion(stateVersion)
        , mCheckpointTick(checkpointTick)
        , mChecksum(canonicalStateChecksumV1(stateVersion, checkpointTick, *state))
        , mState(std::move(state))
        , mChanges(std::move(changes))
    {
    }

    bool operator==(const CanonicalStatePublication& left, const CanonicalStatePublication& right) noexcept
    {
        return left.mStateVersion == right.mStateVersion && left.mCheckpointTick == right.mCheckpointTick
            && left.mChecksum == right.mChecksum && *left.mState == *right.mState && left.mChanges == right.mChanges
            && left.mJoinedSessions == right.mJoinedSessions && left.mSpatialTicks == right.mSpatialTicks
            && left.mSessionLifecycle == right.mSessionLifecycle;
    }

    CanonicalPublicationReadAction classifyCanonicalPublication(
        CanonicalStateVersion lastConsumedVersion, const CanonicalStatePublication& publication) noexcept
    {
        if (publication.stateVersion() == lastConsumedVersion)
            return CanonicalPublicationReadAction::NoChange;
        if (publication.stateVersion() < lastConsumedVersion)
            return CanonicalPublicationReadAction::OlderPublication;

        const auto changes = publication.changes();
        const auto expectedVersion = lastConsumedVersion.next();
        if (expectedVersion && !changes.empty() && changes.front().stateVersion() == *expectedVersion
            && changes.back().stateVersion() == publication.stateVersion())
            return CanonicalPublicationReadAction::ApplyContiguousChanges;
        const auto joins = publication.joinedSessions();
        if (expectedVersion && joins.size() == 1 && joins.front().stateVersion == *expectedVersion
            && joins.front().stateVersion == publication.stateVersion())
            return CanonicalPublicationReadAction::ApplyContiguousChanges;
        const auto spatialTicks = publication.spatialTicks();
        if (expectedVersion && !spatialTicks.empty() && spatialTicks.front().stateVersion == *expectedVersion
            && spatialTicks.back().stateVersion == publication.stateVersion())
            return CanonicalPublicationReadAction::ApplyContiguousChanges;
        const auto lifecycle = publication.sessionLifecycle();
        if (expectedVersion && lifecycle.size() == 1 && lifecycle.front().stateVersion == *expectedVersion
            && lifecycle.front().stateVersion == publication.stateVersion())
            return CanonicalPublicationReadAction::ApplyContiguousChanges;
        return CanonicalPublicationReadAction::ReplaceFromSnapshot;
    }

    bool canReserveCanonicalStateVersions(CanonicalStateVersion currentVersion, std::size_t maximumChanges) noexcept
    {
        constexpr std::uint64_t MaximumVersion = std::numeric_limits<std::uint64_t>::max();
        return maximumChanges <= MaximumVersion - currentVersion.value();
    }
}
