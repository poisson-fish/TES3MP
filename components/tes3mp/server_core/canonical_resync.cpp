#include <tes3mp/canonical_resync.hpp>

namespace TES3MP
{
    CanonicalResyncResult resolveCanonicalResync(
        const CanonicalResyncRequest& request, std::shared_ptr<const CanonicalStatePublication> latestPublication)
    {
        if (!latestPublication)
            return { CanonicalResyncDisposition::UnknownSession, {} };
        const CanonicalSessionProgress* session = latestPublication->state().findActiveSession(request.sessionId());
        if (session == nullptr)
            return { CanonicalResyncDisposition::UnknownSession, {} };
        if (session->sessionGeneration() != request.sessionGeneration())
            return { CanonicalResyncDisposition::SessionGenerationMismatch, {} };
        return { CanonicalResyncDisposition::SnapshotRequired, std::move(latestPublication) };
    }
}
