#ifndef TES3MP_CANONICAL_RESYNC_HPP
#define TES3MP_CANONICAL_RESYNC_HPP

#include "canonical_publication.hpp"
#include "protocol_exchange.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace TES3MP
{
    using CanonicalResyncReason = ResyncReason;
    using CanonicalResyncRequest = SessionResyncRequest;

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
