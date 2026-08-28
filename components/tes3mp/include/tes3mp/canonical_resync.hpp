#ifndef TES3MP_CANONICAL_RESYNC_HPP
#define TES3MP_CANONICAL_RESYNC_HPP

#include "canonical_publication.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace TES3MP
{
    enum class CanonicalResyncReason : std::uint8_t
    {
        LocalFeedGap,
        EntityRevisionMismatch,
        ChecksumMismatch,
    };

    class CanonicalResyncRequest
    {
    public:
        constexpr CanonicalResyncRequest(SessionId sessionId, SessionGeneration sessionGeneration,
            CanonicalResyncReason reason, CanonicalStateVersion lastObservedStateVersion) noexcept
            : mSessionId(sessionId)
            , mSessionGeneration(sessionGeneration)
            , mReason(reason)
            , mLastObservedStateVersion(lastObservedStateVersion)
        {
        }

        constexpr SessionId sessionId() const noexcept { return mSessionId; }
        constexpr SessionGeneration sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr CanonicalResyncReason reason() const noexcept { return mReason; }
        constexpr CanonicalStateVersion lastObservedStateVersion() const noexcept { return mLastObservedStateVersion; }

        friend constexpr bool operator==(CanonicalResyncRequest, CanonicalResyncRequest) noexcept = default;

    private:
        SessionId mSessionId;
        SessionGeneration mSessionGeneration;
        CanonicalResyncReason mReason;
        CanonicalStateVersion mLastObservedStateVersion;
    };

    enum class CanonicalResyncDisposition : std::uint8_t
    {
        SnapshotRequired,
        UnknownSession,
        SessionGenerationMismatch,
    };

    class CanonicalResyncResult
    {
    public:
        constexpr CanonicalResyncDisposition disposition() const noexcept { return mDisposition; }
        const std::shared_ptr<const CanonicalStatePublication>& publication() const noexcept { return mPublication; }

    private:
        friend CanonicalResyncResult resolveCanonicalResync(
            const CanonicalResyncRequest&, std::shared_ptr<const CanonicalStatePublication>);

        CanonicalResyncResult(CanonicalResyncDisposition disposition,
            std::shared_ptr<const CanonicalStatePublication> publication) noexcept
            : mDisposition(disposition)
            , mPublication(std::move(publication))
        {
        }

        CanonicalResyncDisposition mDisposition;
        std::shared_ptr<const CanonicalStatePublication> mPublication;
    };

    CanonicalResyncResult resolveCanonicalResync(
        const CanonicalResyncRequest& request, std::shared_ptr<const CanonicalStatePublication> latestPublication);
}

#endif
