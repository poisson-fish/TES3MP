#include <tes3mp/authentication.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/authentication_accepted_generated.h"
#include "generated/authentication_rejected_generated.h"
#include "generated/authentication_request_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
    constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t MinimumIdentifiedFlatBufferBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;
    constexpr std::size_t MaximumVerifierDepth = 8;
    constexpr std::size_t MaximumVerifierTables = 4;

    constexpr TES3MP::AuthenticationCodecError error(TES3MP::AuthenticationCodecErrorStage stage,
        TES3MP::AuthenticationCodecErrorCode code, std::size_t observed = 0, std::size_t limit = 0) noexcept
    {
        return { stage, code, observed, limit };
    }

    std::optional<TES3MP::AuthenticationCodecError> validatePayloadPrefix(std::span<const std::byte> payload) noexcept
    {
        if (payload.size() < MinimumIdentifiedFlatBufferBytes)
        {
            return error(TES3MP::AuthenticationCodecErrorStage::SizePrefix,
                TES3MP::AuthenticationCodecErrorCode::PayloadTooSmall, payload.size(),
                MinimumIdentifiedFlatBufferBytes);
        }
        if (payload.size() > TES3MP::SessionControlMaximumPayloadBytes)
        {
            return error(TES3MP::AuthenticationCodecErrorStage::SizePrefix,
                TES3MP::AuthenticationCodecErrorCode::PayloadTooLarge, payload.size(),
                TES3MP::SessionControlMaximumPayloadBytes);
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const std::size_t declared = flatbuffers::GetSizePrefixedBufferLength(bytes);
        if (declared != payload.size())
        {
            return error(TES3MP::AuthenticationCodecErrorStage::SizePrefix,
                TES3MP::AuthenticationCodecErrorCode::PayloadLengthMismatch, payload.size(), declared);
        }
        return std::nullopt;
    }

    flatbuffers::Verifier makeVerifier(std::span<const std::byte> payload)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = MaximumVerifierDepth;
        options.max_tables = MaximumVerifierTables;
        options.max_size = TES3MP::SessionControlMaximumPayloadBytes + 1;
        options.check_alignment = true;
        options.check_nested_flatbuffers = false;
        return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    std::vector<std::byte> takeBuffer(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return std::vector<std::byte>(begin, begin + builder.GetSize());
    }

    template <class Vector>
    std::span<const std::byte> byteSpan(const Vector* value) noexcept
    {
        if (value == nullptr)
            return {};
        return { reinterpret_cast<const std::byte*>(value->data()), value->size() };
    }

    void clearBytes(std::span<std::byte> value) noexcept
    {
        volatile std::byte* destination = value.data();
        for (std::size_t index = 0; index < value.size(); ++index)
            destination[index] = std::byte{ 0 };
    }
}

namespace TES3MP
{
    std::optional<AuthenticationMaterial> AuthenticationMaterial::create(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() > MaximumAuthenticationMaterialBytes)
            return std::nullopt;
        AuthenticationMaterial result;
        std::copy(bytes.begin(), bytes.end(), result.mBytes.begin());
        result.mSize = bytes.size();
        return result;
    }

    AuthenticationMaterial::AuthenticationMaterial(AuthenticationMaterial&& other) noexcept
        : mBytes(other.mBytes)
        , mSize(other.mSize)
    {
        other.clear();
    }

    AuthenticationMaterial& AuthenticationMaterial::operator=(AuthenticationMaterial&& other) noexcept
    {
        if (this == &other)
            return *this;
        clear();
        mBytes = other.mBytes;
        mSize = other.mSize;
        other.clear();
        return *this;
    }

    AuthenticationMaterial::~AuthenticationMaterial()
    {
        clear();
    }

    void AuthenticationMaterial::clear() noexcept
    {
        clearBytes(mBytes);
        mSize = 0;
    }

    std::optional<ResumeToken> ResumeToken::create(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() != ResumeTokenBytes)
            return std::nullopt;
        ResumeToken result;
        std::copy(bytes.begin(), bytes.end(), result.mBytes.begin());
        return result;
    }

    ResumeToken::ResumeToken(ResumeToken&& other) noexcept
        : mBytes(other.mBytes)
    {
        other.clear();
    }

    ResumeToken& ResumeToken::operator=(ResumeToken&& other) noexcept
    {
        if (this == &other)
            return *this;
        clear();
        mBytes = other.mBytes;
        other.clear();
        return *this;
    }

    ResumeToken::~ResumeToken()
    {
        clear();
    }

    void ResumeToken::clear() noexcept
    {
        clearBytes(mBytes);
    }

    AuthenticationRequest AuthenticationRequest::join(AuthenticationMaterial material) noexcept
    {
        return AuthenticationRequest(AuthenticationCredentialKind::JoinPassword, std::move(material));
    }

    AuthenticationRequest AuthenticationRequest::resume(ResumeToken token) noexcept
    {
        auto material = AuthenticationMaterial::create(token.secretBytes());
        return AuthenticationRequest(AuthenticationCredentialKind::ResumeToken, std::move(*material));
    }

    std::optional<AuthenticationAcceptedMessage> AuthenticationAcceptedMessage::create(
        ResumeToken token, std::uint64_t lifetimeMilliseconds) noexcept
    {
        if (lifetimeMilliseconds < MinimumResumeTokenLifetimeMilliseconds
            || lifetimeMilliseconds > MaximumResumeTokenLifetimeMilliseconds)
            return std::nullopt;
        return AuthenticationAcceptedMessage(std::move(token), lifetimeMilliseconds);
    }

    std::vector<std::byte> encodeAuthenticationRequest(const AuthenticationRequest& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto material = value.materialBytes();
        const auto encodedMaterial
            = builder.CreateVector(reinterpret_cast<const std::uint8_t*>(material.data()), material.size());
        const auto root = Protocol::Schema::CreateAuthenticationRequest(
            builder, static_cast<Protocol::Schema::AuthenticationCredentialKind>(value.kind()), encodedMaterial);
        Protocol::Schema::FinishSizePrefixedAuthenticationRequestBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeAuthenticationAccepted(const AuthenticationAcceptedMessage& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto token = value.mToken.secretBytes();
        const auto encodedToken
            = builder.CreateVector(reinterpret_cast<const std::uint8_t*>(token.data()), token.size());
        const auto root
            = Protocol::Schema::CreateAuthenticationAccepted(builder, encodedToken, value.lifetimeMilliseconds());
        Protocol::Schema::FinishSizePrefixedAuthenticationAcceptedBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeAuthenticationRejected(AuthenticationRejectedMessage value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto root = Protocol::Schema::CreateAuthenticationRejected(
            builder, static_cast<Protocol::Schema::AuthenticationPublicRejection>(value.reason));
        Protocol::Schema::FinishSizePrefixedAuthenticationRejectedBuffer(builder, root);
        return takeBuffer(builder);
    }

    AuthenticationRequestDecodeResult decodeAuthenticationRequest(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Protocol::Schema::SizePrefixedAuthenticationRequestBufferHasIdentifier(bytes))
        {
            return error(AuthenticationCodecErrorStage::Identifier, AuthenticationCodecErrorCode::InvalidIdentifier);
        }
        auto verifier = makeVerifier(payload);
        if (!Protocol::Schema::VerifySizePrefixedAuthenticationRequestBuffer(verifier))
        {
            return error(AuthenticationCodecErrorStage::Verification, AuthenticationCodecErrorCode::VerificationFailed);
        }

        const auto* value = Protocol::Schema::GetSizePrefixedAuthenticationRequest(bytes);
        const auto materialBytes = byteSpan(value->material());
        AuthenticationCredentialKind kind;
        switch (value->kind())
        {
            case Protocol::Schema::AuthenticationCredentialKind::JoinPassword:
                kind = AuthenticationCredentialKind::JoinPassword;
                if (materialBytes.size() > MaximumAuthenticationMaterialBytes)
                {
                    return error(AuthenticationCodecErrorStage::SemanticValidation,
                        AuthenticationCodecErrorCode::InvalidCredentialSize, materialBytes.size(),
                        MaximumAuthenticationMaterialBytes);
                }
                break;
            case Protocol::Schema::AuthenticationCredentialKind::ResumeToken:
                kind = AuthenticationCredentialKind::ResumeToken;
                if (materialBytes.size() != ResumeTokenBytes)
                {
                    return error(AuthenticationCodecErrorStage::SemanticValidation,
                        AuthenticationCodecErrorCode::InvalidResumeTokenSize, materialBytes.size(), ResumeTokenBytes);
                }
                break;
            default:
                return error(AuthenticationCodecErrorStage::SemanticValidation,
                    AuthenticationCodecErrorCode::UnknownCredentialKind, static_cast<std::size_t>(value->kind()));
        }

        auto material = AuthenticationMaterial::create(materialBytes);
        return AuthenticationRequest(kind, std::move(*material));
    }

    AuthenticationAcceptedDecodeResult decodeAuthenticationAccepted(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Protocol::Schema::SizePrefixedAuthenticationAcceptedBufferHasIdentifier(bytes))
        {
            return error(AuthenticationCodecErrorStage::Identifier, AuthenticationCodecErrorCode::InvalidIdentifier);
        }
        auto verifier = makeVerifier(payload);
        if (!Protocol::Schema::VerifySizePrefixedAuthenticationAcceptedBuffer(verifier))
        {
            return error(AuthenticationCodecErrorStage::Verification, AuthenticationCodecErrorCode::VerificationFailed);
        }

        const auto* value = Protocol::Schema::GetSizePrefixedAuthenticationAccepted(bytes);
        const auto tokenBytes = byteSpan(value->resume_token());
        auto token = ResumeToken::create(tokenBytes);
        if (!token)
        {
            return error(AuthenticationCodecErrorStage::SemanticValidation,
                AuthenticationCodecErrorCode::InvalidResumeTokenSize, tokenBytes.size(), ResumeTokenBytes);
        }
        auto accepted = AuthenticationAcceptedMessage::create(std::move(*token), value->lifetime_milliseconds());
        if (!accepted)
        {
            return error(AuthenticationCodecErrorStage::SemanticValidation,
                AuthenticationCodecErrorCode::InvalidLifetime, value->lifetime_milliseconds(),
                MaximumResumeTokenLifetimeMilliseconds);
        }
        return std::move(*accepted);
    }

    AuthenticationRejectedDecodeResult decodeAuthenticationRejected(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Protocol::Schema::SizePrefixedAuthenticationRejectedBufferHasIdentifier(bytes))
        {
            return error(AuthenticationCodecErrorStage::Identifier, AuthenticationCodecErrorCode::InvalidIdentifier);
        }
        auto verifier = makeVerifier(payload);
        if (!Protocol::Schema::VerifySizePrefixedAuthenticationRejectedBuffer(verifier))
        {
            return error(AuthenticationCodecErrorStage::Verification, AuthenticationCodecErrorCode::VerificationFailed);
        }

        const auto* value = Protocol::Schema::GetSizePrefixedAuthenticationRejected(bytes);
        switch (value->reason())
        {
            case Protocol::Schema::AuthenticationPublicRejection::Denied:
                return AuthenticationRejectedMessage{ AuthenticationPublicRejection::Denied };
            case Protocol::Schema::AuthenticationPublicRejection::TemporarilyUnavailable:
                return AuthenticationRejectedMessage{ AuthenticationPublicRejection::TemporarilyUnavailable };
            default:
                return error(AuthenticationCodecErrorStage::SemanticValidation,
                    AuthenticationCodecErrorCode::UnknownRejectionReason, static_cast<std::size_t>(value->reason()));
        }
    }
}
