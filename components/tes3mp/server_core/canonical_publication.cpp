#include <tes3mp/canonical_publication.hpp>

#include <limits>
#include <utility>

namespace TES3MP
{
    CanonicalStatePublication::CanonicalStatePublication(CanonicalStateVersion stateVersion,
        std::shared_ptr<const CanonicalServerState> state, std::vector<CanonicalStateChangeRecord> changes) noexcept
        : mStateVersion(stateVersion)
        , mState(std::move(state))
        , mChanges(std::move(changes))
    {
    }

    bool operator==(const CanonicalStatePublication& left, const CanonicalStatePublication& right) noexcept
    {
        return left.mStateVersion == right.mStateVersion && *left.mState == *right.mState
            && left.mChanges == right.mChanges;
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
        return CanonicalPublicationReadAction::ReplaceFromSnapshot;
    }

    bool canReserveCanonicalStateVersions(CanonicalStateVersion currentVersion, std::size_t maximumChanges) noexcept
    {
        constexpr std::uint64_t MaximumVersion = std::numeric_limits<std::uint64_t>::max();
        return maximumChanges <= MaximumVersion - currentVersion.value();
    }
}
