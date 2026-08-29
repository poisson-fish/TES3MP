#ifndef TES3MP_SERVER_AUTHENTICATION_HPP
#define TES3MP_SERVER_AUTHENTICATION_HPP

#include "authentication.hpp"
#include "monotonic_clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace TES3MP
{
    inline constexpr std::size_t CredentialDigestBytes = 32;
    inline constexpr std::size_t MaximumResumeTokenRecords = 256;

    struct CredentialDigest
    {
        std::array<std::byte, CredentialDigestBytes> bytes{};

        friend constexpr bool operator==(CredentialDigest, CredentialDigest) noexcept = default;
    };

    struct ResumeTokenContext
    {
        CredentialDigest protocol;
        CredentialDigest content;

        friend constexpr bool operator==(ResumeTokenContext, ResumeTokenContext) noexcept = default;
    };

    class CredentialCrypto
    {
    public:
        virtual ~CredentialCrypto() = default;
        virtual bool randomBytes(std::span<std::byte> destination) noexcept = 0;
        virtual bool sha256(std::span<const std::byte> source, CredentialDigest& destination) noexcept = 0;
        virtual bool constantTimeEqual(std::span<const std::byte> left, std::span<const std::byte> right) noexcept = 0;
    };

    class JoinPasswordAuthenticationProvider final : public AuthenticationProvider
    {
    public:
        static std::unique_ptr<JoinPasswordAuthenticationProvider> create(
            CredentialCrypto& crypto, AuthenticationMaterial expectedPassword) noexcept;

        JoinPasswordAuthenticationProvider(const JoinPasswordAuthenticationProvider&) = delete;
        JoinPasswordAuthenticationProvider& operator=(const JoinPasswordAuthenticationProvider&) = delete;
        JoinPasswordAuthenticationProvider(JoinPasswordAuthenticationProvider&&) = delete;
        JoinPasswordAuthenticationProvider& operator=(JoinPasswordAuthenticationProvider&&) = delete;

        std::unique_ptr<AuthenticationOperation> begin(
            AuthenticationAttempt attempt, AuthenticationMaterial material) noexcept override;

    private:
        JoinPasswordAuthenticationProvider(CredentialCrypto& crypto, AuthenticationMaterial expectedPassword) noexcept
            : mCrypto(crypto)
            , mExpectedPassword(std::move(expectedPassword))
        {
        }

        CredentialCrypto& mCrypto;
        AuthenticationMaterial mExpectedPassword;
    };

    enum class ResumeTokenStoreError : std::uint8_t
    {
        Denied,
        Full,
        RandomUnavailable,
        DigestUnavailable,
        DigestCollision,
        DeadlineOverflow,
        GenerationOverflow,
    };

    using ResumeTokenIssueResult = std::variant<AuthenticationAcceptedMessage, ResumeTokenStoreError>;
    using ResumeTokenConsumeResult = std::variant<AuthenticatedAdmission, ResumeTokenStoreError>;

    class ResumeTokenStore
    {
    public:
        static std::unique_ptr<ResumeTokenStore> create(
            CredentialCrypto& crypto, std::uint64_t lifetimeMilliseconds) noexcept;

        ResumeTokenStore(const ResumeTokenStore&) = delete;
        ResumeTokenStore& operator=(const ResumeTokenStore&) = delete;
        ResumeTokenStore(ResumeTokenStore&&) = delete;
        ResumeTokenStore& operator=(ResumeTokenStore&&) = delete;

        ResumeTokenIssueResult issue(PrincipalId principal, SessionId session, SessionGeneration generation,
            ResumeTokenContext context, MonotonicInstant now) noexcept;
        ResumeTokenConsumeResult consume(
            const ResumeToken& token, ResumeTokenContext context, MonotonicInstant now) noexcept;

        std::size_t size() const noexcept;

    private:
        struct Record
        {
            CredentialDigest digest;
            PrincipalId principal;
            SessionId session;
            SessionGeneration generation;
            ResumeTokenContext context;
            MonotonicInstant expiresAt;
        };

        ResumeTokenStore(CredentialCrypto& crypto, std::uint64_t lifetimeMilliseconds) noexcept
            : mCrypto(crypto)
            , mLifetimeMilliseconds(lifetimeMilliseconds)
        {
        }

        std::optional<MonotonicInstant> expiryFrom(MonotonicInstant now) const noexcept;
        std::optional<std::size_t> find(const CredentialDigest& digest) const noexcept;
        bool digestToken(const ResumeToken& token, CredentialDigest& digest) noexcept;
        std::variant<std::pair<ResumeToken, CredentialDigest>, ResumeTokenStoreError> makeToken() noexcept;
        std::optional<std::size_t> emptySlot() const noexcept;
        void purgeExpired(MonotonicInstant now) noexcept;

        CredentialCrypto& mCrypto;
        std::uint64_t mLifetimeMilliseconds;
        mutable std::mutex mMutex;
        std::array<std::optional<Record>, MaximumResumeTokenRecords> mRecords{};
        std::size_t mSize = 0;
    };
}

#endif
