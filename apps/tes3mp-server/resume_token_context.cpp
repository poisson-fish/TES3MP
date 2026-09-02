#include "resume_token_context.hpp"

#include <cstddef>
#include <span>

namespace TES3MP::ServerApp
{
    std::optional<ResumeTokenContext> makePhase7ResumeTokenContext(
        const ServerHello& negotiated, CredentialCrypto& crypto) noexcept
    {
        try
        {
            const auto protocolBytes = encodeServerHello(negotiated);
            const auto contentBytes
                = std::as_bytes(std::span(Phase7FixtureContentId.data(), Phase7FixtureContentId.size()));
            ResumeTokenContext context;
            if (!crypto.sha256(protocolBytes, context.protocol)
                || !crypto.sha256(contentBytes, context.content))
                return std::nullopt;
            return context;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}
