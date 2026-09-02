#ifndef TES3MP_SERVER_RESUME_TOKEN_CONTEXT_HPP
#define TES3MP_SERVER_RESUME_TOKEN_CONTEXT_HPP

#include "tes3mp/server_authentication.hpp"
#include "tes3mp/protocol_handshake.hpp"

#include <optional>

namespace TES3MP::ServerApp
{
    inline constexpr std::string_view Phase7FixtureContentId = "tes3mp-vnext-phase7-fixture-v1";

    std::optional<ResumeTokenContext> makePhase7ResumeTokenContext(
        const ServerHello& negotiated, CredentialCrypto& crypto) noexcept;
}

#endif
