#ifndef TES3MP_SERVER_AUTHENTICATION_HPP
#define TES3MP_SERVER_AUTHENTICATION_HPP

#include "admission_scope.hpp"
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
    inline constexpr std::size_t MaximumAuthenticationAdmissionScopes = 256;
    inline constexpr std::size_t MinimumSourceAuthenticationBurst = 1;
    inline constexpr std::size_t MaximumSourceAuthenticationBurst = 16;
    inline constexpr std::size_t MinimumGlobalAuthenticationBurst = 1;
    inline constexpr std::size_t MaximumGlobalAuthenticationBurst = 256;
    inline constexpr std::uint64_t MinimumAuthenticationRefillMilliseconds = 100;
    inline constexpr std::uint64_t MaximumAuthenticationRefillMilliseconds = 120'000;

    class AuthenticationRateLimitPolicy
    {
    public:
        static std::optional<AuthenticationRateLimitPolicy> create(std::size_t sourceBurst, std::size_t globalBurst,
            std::uint64_t sourceRefillMilliseconds, std::uint64_t globalRefillMilliseconds) noexcept;

        std::size_t sourceBurst() const noexcept { return mSourceBurst; }
        std::size_t globalBurst() const noexcept { return mGlobalBurst; }
        std::uint64_t sourceRefillMilliseconds() const noexcept { return mSourceRefillMilliseconds; }
        std::uint64_t globalRefillMilliseconds() const noexcept { return mGlobalRefillMilliseconds; }

    private:
        AuthenticationRateLimitPolicy(std::size_t sourceBurst, std::size_t globalBurst,
            std::uint64_t sourceRefillMilliseconds, std::uint64_t globalRefillMilliseconds) noexcept
            : mSourceBurst(sourceBurst)
            , mGlobalBurst(globalBurst)
            , mSourceRefillMilliseconds(sourceRefillMilliseconds)
            , mGlobalRefillMilliseconds(globalRefillMilliseconds)
        {
        }

        std::size_t mSourceBurst;
        std::size_t mGlobalBurst;
        std::uint64_t mSourceRefillMilliseconds;
        std::uint64_t mGlobalRefillMilliseconds;
    };

    enum class AuthenticationRateLimitResult : std::uint8_t
    {
        Allowed,
        GlobalExhausted,
        SourceExhausted,
        SourceTableFull,
        ClockRegressed,
    };

    class AuthenticationRateLimiter
    {
    public:
        static std::unique_ptr<AuthenticationRateLimiter> create(
            AuthenticationRateLimitPolicy policy, MonotonicInstant now) noexcept;

        AuthenticationRateLimiter(const AuthenticationRateLimiter&) = delete;
        AuthenticationRateLimiter& operator=(const AuthenticationRateLimiter&) = delete;
        AuthenticationRateLimiter(AuthenticationRateLimiter&&) = delete;
        AuthenticationRateLimiter& operator=(AuthenticationRateLimiter&&) = delete;

        AuthenticationRateLimitResult allow(const AdmissionScopeId& scope, MonotonicInstant now) noexcept;
        std::size_t trackedScopes() const noexcept;

    private:
        struct Bucket
        {
            std::size_t tokens;
            MonotonicInstant lastRefill;
        };

        struct SourceBucket
        {
            AdmissionScopeId scope;
            Bucket bucket;
        };

        AuthenticationRateLimiter(AuthenticationRateLimitPolicy policy, MonotonicInstant now) noexcept
            : mPolicy(policy)
            , mGlobal{ policy.globalBurst(), now }
            , mLastAttempt(now)
        {
        }

        static void refill(
            Bucket& bucket, std::size_t capacity, std::uint64_t intervalNanoseconds, MonotonicInstant now) noexcept;
        std::optional<std::size_t> find(const AdmissionScopeId& scope) const noexcept;
        std::optional<std::size_t> emptySlot() const noexcept;

        AuthenticationRateLimitPolicy mPolicy;
        Bucket mGlobal;
        MonotonicInstant mLastAttempt;
        mutable std::mutex mMutex;
        std::array<std::optional<SourceBucket>, MaximumAuthenticationAdmissionScopes> mSources{};
        std::size_t mTrackedScopes = 0;
    };

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

    std::unique_ptr<CredentialCrypto> makeProductionCredentialCrypto() noexcept;

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
