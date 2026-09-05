#include "resume_token_context.hpp"

#include <cstddef>
#include <span>

namespace TES3MP::ServerApp
{
    std::optional<ResumeTokenContext> makeResumeTokenContext(
        const ServerHello& negotiated, CredentialCrypto& crypto) noexcept
    {
        try
        {
            const auto protocolBytes = encodeServerHello(negotiated);
            const auto contentBytes = negotiated.contentManifest().bytes();
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
