#ifndef TES3MP_AUTHENTICATION_HPP
#define TES3MP_AUTHENTICATION_HPP

#include "session_types.hpp"
#include "value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumAuthenticationMaterialBytes = 256;
    inline constexpr std::size_t ResumeTokenBytes = 32;
    inline constexpr std::uint64_t MinimumResumeTokenLifetimeMilliseconds = 1'000;
    inline constexpr std::uint64_t MaximumResumeTokenLifetimeMilliseconds = 120'000;

    class AuthenticationRequest;
    class AuthenticationAcceptedMessage;
    struct AuthenticationCodecError;

    class AuthenticationMaterial
    {
    public:
        static std::optional<AuthenticationMaterial> create(std::span<const std::byte> bytes) noexcept;

        AuthenticationMaterial(const AuthenticationMaterial&) = delete;
        AuthenticationMaterial& operator=(const AuthenticationMaterial&) = delete;
        AuthenticationMaterial(AuthenticationMaterial&& other) noexcept;
        AuthenticationMaterial& operator=(AuthenticationMaterial&& other) noexcept;
        ~AuthenticationMaterial();

        std::size_t size() const noexcept { return mSize; }
        bool empty() const noexcept { return mSize == 0; }

    private:
        friend class AuthenticationProvider;
        friend class AuthenticationRequest;

        AuthenticationMaterial() noexcept = default;
        std::span<const std::byte> secretBytes() const noexcept { return { mBytes.data(), mSize }; }
        void clear() noexcept;

        std::array<std::byte, MaximumAuthenticationMaterialBytes> mBytes{};
        std::size_t mSize = 0;
    };

    class ResumeToken
    {
    public:
        static std::optional<ResumeToken> create(std::span<const std::byte> bytes) noexcept;

        ResumeToken(const ResumeToken&) = delete;
        ResumeToken& operator=(const ResumeToken&) = delete;
        ResumeToken(ResumeToken&& other) noexcept;
        ResumeToken& operator=(ResumeToken&& other) noexcept;
        ~ResumeToken();

        constexpr std::size_t size() const noexcept { return ResumeTokenBytes; }

    private:
        friend class AuthenticationRequest;
        friend class AuthenticationAcceptedMessage;
        friend std::vector<std::byte> encodeAuthenticationRequest(const AuthenticationRequest& value);
        friend std::vector<std::byte> encodeAuthenticationAccepted(const AuthenticationAcceptedMessage& value);

        ResumeToken() noexcept = default;
        std::span<const std::byte> secretBytes() const noexcept { return mBytes; }
        void clear() noexcept;

        std::array<std::byte, ResumeTokenBytes> mBytes{};
    };

    enum class AuthenticationCredentialKind : std::uint8_t
    {
        JoinPassword = 1,
        ResumeToken = 2,
    };

    class AuthenticationRequest
    {
    public:
        static AuthenticationRequest join(AuthenticationMaterial material) noexcept;
        static AuthenticationRequest resume(ResumeToken token) noexcept;

        AuthenticationRequest(const AuthenticationRequest&) = delete;
        AuthenticationRequest& operator=(const AuthenticationRequest&) = delete;
        AuthenticationRequest(AuthenticationRequest&&) noexcept = default;
        AuthenticationRequest& operator=(AuthenticationRequest&&) noexcept = default;

        AuthenticationCredentialKind kind() const noexcept { return mKind; }
        std::size_t materialSize() const noexcept { return mMaterial.size(); }

    private:
        friend std::vector<std::byte> encodeAuthenticationRequest(const AuthenticationRequest& value);
        friend std::variant<AuthenticationRequest, struct AuthenticationCodecError> decodeAuthenticationRequest(
            std::span<const std::byte> payload);

        std::span<const std::byte> materialBytes() const noexcept { return mMaterial.secretBytes(); }

        AuthenticationRequest(AuthenticationCredentialKind kind, AuthenticationMaterial material) noexcept
            : mKind(kind)
            , mMaterial(std::move(material))
        {
        }

        AuthenticationCredentialKind mKind;
        AuthenticationMaterial mMaterial;
    };

    class AuthenticationAcceptedMessage
    {
    public:
        static std::optional<AuthenticationAcceptedMessage> create(
            ResumeToken token, std::uint64_t lifetimeMilliseconds) noexcept;

        AuthenticationAcceptedMessage(const AuthenticationAcceptedMessage&) = delete;
        AuthenticationAcceptedMessage& operator=(const AuthenticationAcceptedMessage&) = delete;
        AuthenticationAcceptedMessage(AuthenticationAcceptedMessage&&) noexcept = default;
        AuthenticationAcceptedMessage& operator=(AuthenticationAcceptedMessage&&) noexcept = default;

        std::uint64_t lifetimeMilliseconds() const noexcept { return mLifetimeMilliseconds; }
        ResumeToken takeToken() noexcept { return std::move(mToken); }

    private:
        friend std::vector<std::byte> encodeAuthenticationAccepted(const AuthenticationAcceptedMessage& value);
        friend std::variant<AuthenticationAcceptedMessage, struct AuthenticationCodecError>
        decodeAuthenticationAccepted(std::span<const std::byte> payload);

        AuthenticationAcceptedMessage(ResumeToken token, std::uint64_t lifetimeMilliseconds) noexcept
            : mToken(std::move(token))
            , mLifetimeMilliseconds(lifetimeMilliseconds)
        {
        }

        ResumeToken mToken;
        std::uint64_t mLifetimeMilliseconds;
    };

    enum class AuthenticationPublicRejection : std::uint8_t
    {
        Denied = 1,
        TemporarilyUnavailable = 2,
    };

    struct AuthenticationRejectedMessage
    {
        AuthenticationPublicRejection reason;

        friend constexpr bool operator==(AuthenticationRejectedMessage, AuthenticationRejectedMessage) noexcept
            = default;
    };

    enum class AuthenticationCodecErrorStage : std::uint8_t
    {
        SizePrefix,
        Identifier,
        Verification,
        SemanticValidation,
    };

    enum class AuthenticationCodecErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        UnknownCredentialKind,
        InvalidCredentialSize,
        InvalidResumeTokenSize,
        InvalidLifetime,
        UnknownRejectionReason,
    };

    struct AuthenticationCodecError
    {
        AuthenticationCodecErrorStage stage;
        AuthenticationCodecErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;

        friend constexpr bool operator==(AuthenticationCodecError, AuthenticationCodecError) noexcept = default;
    };

    using AuthenticationRequestDecodeResult = std::variant<AuthenticationRequest, AuthenticationCodecError>;
    using AuthenticationAcceptedDecodeResult = std::variant<AuthenticationAcceptedMessage, AuthenticationCodecError>;
    using AuthenticationRejectedDecodeResult = std::variant<AuthenticationRejectedMessage, AuthenticationCodecError>;

    std::vector<std::byte> encodeAuthenticationRequest(const AuthenticationRequest& value);
    std::vector<std::byte> encodeAuthenticationAccepted(const AuthenticationAcceptedMessage& value);
    std::vector<std::byte> encodeAuthenticationRejected(AuthenticationRejectedMessage value);

    AuthenticationRequestDecodeResult decodeAuthenticationRequest(std::span<const std::byte> payload);
    AuthenticationAcceptedDecodeResult decodeAuthenticationAccepted(std::span<const std::byte> payload);
    AuthenticationRejectedDecodeResult decodeAuthenticationRejected(std::span<const std::byte> payload);

    struct AuthenticationAttempt
    {
        AuthenticationAttemptId id;
        SessionGeneration generation;

        friend constexpr bool operator==(AuthenticationAttempt, AuthenticationAttempt) noexcept = default;
    };

    struct AuthenticatedPrincipal
    {
        PrincipalId id;

        friend constexpr bool operator==(AuthenticatedPrincipal, AuthenticatedPrincipal) noexcept = default;
    };

    struct AuthenticationRejected
    {
        AuthenticationRejectionReason reason;

        friend constexpr bool operator==(AuthenticationRejected, AuthenticationRejected) noexcept = default;
    };

    using AuthenticationResult = std::variant<AuthenticatedPrincipal, AuthenticationRejected>;

    struct AuthenticationPending
    {
        friend constexpr bool operator==(AuthenticationPending, AuthenticationPending) noexcept = default;
    };

    struct AuthenticationCompletion
    {
        AuthenticationAttempt attempt;
        AuthenticationResult result;

        friend constexpr bool operator==(const AuthenticationCompletion&, const AuthenticationCompletion&) noexcept
            = default;
    };

    using AuthenticationPollResult = std::variant<AuthenticationPending, AuthenticationCompletion>;

    class AuthenticationOperation
    {
    public:
        virtual ~AuthenticationOperation() = default;
        virtual AuthenticationPollResult poll() noexcept = 0;
        virtual void cancel() noexcept = 0;
    };

    class AuthenticationProvider
    {
    public:
        virtual ~AuthenticationProvider() = default;
        virtual std::unique_ptr<AuthenticationOperation> begin(
            AuthenticationAttempt attempt, AuthenticationMaterial material) noexcept
            = 0;

    protected:
        static std::span<const std::byte> materialBytes(const AuthenticationMaterial& material) noexcept
        {
            return material.secretBytes();
        }
    };
}

#endif
