#include <tes3mp/server_authentication.hpp>

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <utility>

namespace TES3MP
{
    std::optional<AuthenticationRateLimitPolicy> AuthenticationRateLimitPolicy::create(std::size_t sourceBurst,
        std::size_t globalBurst, std::uint64_t sourceRefillMilliseconds,
        std::uint64_t globalRefillMilliseconds) noexcept
    {
        if (sourceBurst < MinimumSourceAuthenticationBurst || sourceBurst > MaximumSourceAuthenticationBurst
            || globalBurst < MinimumGlobalAuthenticationBurst || globalBurst > MaximumGlobalAuthenticationBurst
            || sourceRefillMilliseconds < MinimumAuthenticationRefillMilliseconds
            || sourceRefillMilliseconds > MaximumAuthenticationRefillMilliseconds
            || globalRefillMilliseconds < MinimumAuthenticationRefillMilliseconds
            || globalRefillMilliseconds > MaximumAuthenticationRefillMilliseconds)
            return std::nullopt;
        return AuthenticationRateLimitPolicy(
            sourceBurst, globalBurst, sourceRefillMilliseconds, globalRefillMilliseconds);
    }

    std::unique_ptr<AuthenticationRateLimiter> AuthenticationRateLimiter::create(
        AuthenticationRateLimitPolicy policy, MonotonicInstant now) noexcept
    {
        return std::unique_ptr<AuthenticationRateLimiter>(new (std::nothrow) AuthenticationRateLimiter(policy, now));
    }

    void AuthenticationRateLimiter::refill(
        Bucket& bucket, std::size_t capacity, std::uint64_t intervalNanoseconds, MonotonicInstant now) noexcept
    {
        const auto elapsed = now.nanoseconds() - bucket.lastRefill.nanoseconds();
        const auto additions = elapsed / intervalNanoseconds;
        if (additions == 0)
            return;
        const auto missing = capacity - bucket.tokens;
        if (additions >= missing)
        {
            bucket.tokens = capacity;
            bucket.lastRefill = now;
            return;
        }
        bucket.tokens += static_cast<std::size_t>(additions);
        bucket.lastRefill
            = MonotonicInstant::fromNanoseconds(bucket.lastRefill.nanoseconds() + additions * intervalNanoseconds);
    }

    std::optional<std::size_t> AuthenticationRateLimiter::find(const AdmissionScopeId& scope) const noexcept
    {
        for (std::size_t index = 0; index < mSources.size(); ++index)
        {
            if (mSources[index] && mSources[index]->scope == scope)
                return index;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> AuthenticationRateLimiter::emptySlot() const noexcept
    {
        for (std::size_t index = 0; index < mSources.size(); ++index)
        {
            if (!mSources[index])
                return index;
        }
        return std::nullopt;
    }

    AuthenticationRateLimitResult AuthenticationRateLimiter::allow(
        const AdmissionScopeId& scope, MonotonicInstant now) noexcept
    {
        constexpr std::uint64_t NanosecondsPerMillisecond = 1'000'000;
        const std::scoped_lock lock(mMutex);
        if (now < mLastAttempt)
            return AuthenticationRateLimitResult::ClockRegressed;
        mLastAttempt = now;

        refill(mGlobal, mPolicy.globalBurst(), mPolicy.globalRefillMilliseconds() * NanosecondsPerMillisecond, now);
        if (mGlobal.tokens == 0)
            return AuthenticationRateLimitResult::GlobalExhausted;

        if (const auto existing = find(scope))
        {
            auto& source = mSources[*existing]->bucket;
            refill(source, mPolicy.sourceBurst(), mPolicy.sourceRefillMilliseconds() * NanosecondsPerMillisecond, now);
            if (source.tokens == 0)
                return AuthenticationRateLimitResult::SourceExhausted;
            --source.tokens;
            --mGlobal.tokens;
            return AuthenticationRateLimitResult::Allowed;
        }

        const auto slot = emptySlot();
        if (!slot)
            return AuthenticationRateLimitResult::SourceTableFull;
        mSources[*slot] = SourceBucket{ scope, Bucket{ mPolicy.sourceBurst() - 1, now } };
        ++mTrackedScopes;
        --mGlobal.tokens;
        return AuthenticationRateLimitResult::Allowed;
    }

    std::size_t AuthenticationRateLimiter::trackedScopes() const noexcept
    {
        const std::scoped_lock lock(mMutex);
        return mTrackedScopes;
    }

    namespace
    {
        constexpr std::size_t ComparedPasswordBytes = MaximumAuthenticationMaterialBytes + sizeof(std::uint16_t);

        void clearBytes(std::span<std::byte> bytes) noexcept
        {
            volatile std::byte* destination = bytes.data();
            for (std::size_t index = 0; index < bytes.size(); ++index)
                destination[index] = std::byte{ 0 };
        }

        bool passwordsMatch(
            CredentialCrypto& crypto, std::span<const std::byte> expected, std::span<const std::byte> supplied) noexcept
        {
            std::array<std::byte, ComparedPasswordBytes> expectedBlock{};
            std::array<std::byte, ComparedPasswordBytes> suppliedBlock{};
            const auto encode = [](std::span<const std::byte> source, auto& destination) {
                const auto size = static_cast<std::uint16_t>(source.size());
                destination[0] = static_cast<std::byte>(size & 0xffu);
                destination[1] = static_cast<std::byte>((size >> 8u) & 0xffu);
                std::copy(source.begin(), source.end(), destination.begin() + sizeof(size));
            };
            encode(expected, expectedBlock);
            encode(supplied, suppliedBlock);
            const bool matched = crypto.constantTimeEqual(expectedBlock, suppliedBlock);
            clearBytes(expectedBlock);
            clearBytes(suppliedBlock);
            return matched;
        }

        std::optional<PrincipalId> allocatePrincipal() noexcept
        {
            static std::atomic<std::uint64_t> next{ 1 };
            auto candidate = next.load(std::memory_order_relaxed);
            for (;;)
            {
                if (candidate == 0)
                    return std::nullopt;
                const auto following = candidate == std::numeric_limits<std::uint64_t>::max() ? 0 : candidate + 1;
                if (next.compare_exchange_weak(
                        candidate, following, std::memory_order_relaxed, std::memory_order_relaxed))
                    return PrincipalId::fromValue(candidate);
            }
        }

        enum class PasswordOperationState : std::uint8_t
        {
            Ready,
            Cancelled,
            Completed,
        };

        class JoinPasswordAuthenticationOperation final : public AuthenticationOperation
        {
        public:
            JoinPasswordAuthenticationOperation(AuthenticationAttempt attempt, bool matched) noexcept
                : mAttempt(attempt)
                , mMatched(matched)
            {
            }

            AuthenticationPollResult poll() noexcept override
            {
                if (mState == PasswordOperationState::Completed)
                    return AuthenticationPending{};

                AuthenticationResult result = AuthenticationRejected{ AuthenticationRejectionReason::Cancelled };
                if (mState != PasswordOperationState::Cancelled)
                {
                    if (!mMatched)
                        result = AuthenticationRejected{ AuthenticationRejectionReason::Denied };
                    else if (auto principal = allocatePrincipal())
                        result = AuthenticatedAdmission::initial(*principal);
                    else
                        result = AuthenticationRejected{ AuthenticationRejectionReason::ProviderUnavailable };
                }
                mState = PasswordOperationState::Completed;
                return AuthenticationCompletion{ mAttempt, std::move(result) };
            }

            void cancel() noexcept override
            {
                if (mState == PasswordOperationState::Ready)
                    mState = PasswordOperationState::Cancelled;
            }

        private:
            AuthenticationAttempt mAttempt;
            bool mMatched;
            PasswordOperationState mState = PasswordOperationState::Ready;
        };
    }

    std::unique_ptr<JoinPasswordAuthenticationProvider> JoinPasswordAuthenticationProvider::create(
        CredentialCrypto& crypto, AuthenticationMaterial expectedPassword) noexcept
    {
        return std::unique_ptr<JoinPasswordAuthenticationProvider>(
            new (std::nothrow) JoinPasswordAuthenticationProvider(crypto, std::move(expectedPassword)));
    }

    std::unique_ptr<AuthenticationOperation> JoinPasswordAuthenticationProvider::begin(
        AuthenticationAttempt attempt, AuthenticationMaterial material) noexcept
    {
        const bool matched = passwordsMatch(mCrypto, materialBytes(mExpectedPassword), materialBytes(material));
        return std::unique_ptr<AuthenticationOperation>(
            new (std::nothrow) JoinPasswordAuthenticationOperation(attempt, matched));
    }

    std::unique_ptr<ResumeTokenStore> ResumeTokenStore::create(
        CredentialCrypto& crypto, std::uint64_t lifetimeMilliseconds) noexcept
    {
        if (lifetimeMilliseconds < MinimumResumeTokenLifetimeMilliseconds
            || lifetimeMilliseconds > MaximumResumeTokenLifetimeMilliseconds)
            return {};
        return std::unique_ptr<ResumeTokenStore>(new (std::nothrow) ResumeTokenStore(crypto, lifetimeMilliseconds));
    }

    std::optional<MonotonicInstant> ResumeTokenStore::expiryFrom(MonotonicInstant now) const noexcept
    {
        constexpr std::uint64_t NanosecondsPerMillisecond = 1'000'000;
        if (mLifetimeMilliseconds > std::numeric_limits<std::uint64_t>::max() / NanosecondsPerMillisecond)
            return std::nullopt;
        const auto lifetime = mLifetimeMilliseconds * NanosecondsPerMillisecond;
        if (now.nanoseconds() > std::numeric_limits<std::uint64_t>::max() - lifetime)
            return std::nullopt;
        return MonotonicInstant::fromNanoseconds(now.nanoseconds() + lifetime);
    }

    std::optional<std::size_t> ResumeTokenStore::find(const CredentialDigest& digest) const noexcept
    {
        for (std::size_t index = 0; index < mRecords.size(); ++index)
        {
            if (mRecords[index] && mCrypto.constantTimeEqual(mRecords[index]->digest.bytes, digest.bytes))
                return index;
        }
        return std::nullopt;
    }

    bool ResumeTokenStore::digestToken(const ResumeToken& token, CredentialDigest& digest) noexcept
    {
        return mCrypto.sha256(token.secretBytes(), digest);
    }

    std::variant<std::pair<ResumeToken, CredentialDigest>, ResumeTokenStoreError> ResumeTokenStore::makeToken() noexcept
    {
        std::array<std::byte, ResumeTokenBytes> bytes{};
        if (!mCrypto.randomBytes(bytes))
            return ResumeTokenStoreError::RandomUnavailable;
        auto token = ResumeToken::create(bytes);
        CredentialDigest digest;
        if (!token || !digestToken(*token, digest))
            return ResumeTokenStoreError::DigestUnavailable;
        if (find(digest))
            return ResumeTokenStoreError::DigestCollision;
        return std::pair{ std::move(*token), digest };
    }

    std::optional<std::size_t> ResumeTokenStore::emptySlot() const noexcept
    {
        for (std::size_t index = 0; index < mRecords.size(); ++index)
        {
            if (!mRecords[index])
                return index;
        }
        return std::nullopt;
    }

    void ResumeTokenStore::purgeExpired(MonotonicInstant now) noexcept
    {
        for (auto& record : mRecords)
        {
            if (record && now >= record->expiresAt)
            {
                record.reset();
                --mSize;
            }
        }
    }

    ResumeTokenIssueResult ResumeTokenStore::issue(PrincipalId principal, SessionId session,
        SessionGeneration generation, ResumeTokenContext context, MonotonicInstant now) noexcept
    {
        const std::scoped_lock lock(mMutex);
        purgeExpired(now);
        const auto slot = emptySlot();
        if (!slot)
            return ResumeTokenStoreError::Full;
        const auto expiresAt = expiryFrom(now);
        if (!expiresAt)
            return ResumeTokenStoreError::DeadlineOverflow;
        auto generated = makeToken();
        if (const auto* error = std::get_if<ResumeTokenStoreError>(&generated))
            return *error;
        auto [token, digest] = std::get<std::pair<ResumeToken, CredentialDigest>>(std::move(generated));
        mRecords[*slot] = Record{ digest, principal, session, generation, context, *expiresAt };
        ++mSize;
        return *AuthenticationAcceptedMessage::create(std::move(token), mLifetimeMilliseconds);
    }

    ResumeTokenConsumeResult ResumeTokenStore::consume(
        const ResumeToken& token, ResumeTokenContext context, MonotonicInstant now) noexcept
    {
        const std::scoped_lock lock(mMutex);
        CredentialDigest digest;
        if (!digestToken(token, digest))
            return ResumeTokenStoreError::DigestUnavailable;
        const auto slot = find(digest);
        if (!slot)
            return ResumeTokenStoreError::Denied;
        const Record& current = *mRecords[*slot];
        if (now >= current.expiresAt)
        {
            mRecords[*slot].reset();
            --mSize;
            return ResumeTokenStoreError::Denied;
        }
        if (current.context != context)
            return ResumeTokenStoreError::Denied;
        const auto nextGeneration = current.generation.next();
        if (!nextGeneration)
            return ResumeTokenStoreError::GenerationOverflow;
        const auto expiresAt = expiryFrom(now);
        if (!expiresAt)
            return ResumeTokenStoreError::DeadlineOverflow;
        auto generated = makeToken();
        if (const auto* error = std::get_if<ResumeTokenStoreError>(&generated))
            return *error;
        auto [replacement, replacementDigest]
            = std::get<std::pair<ResumeToken, CredentialDigest>>(std::move(generated));
        auto response = AuthenticationAcceptedMessage::create(std::move(replacement), mLifetimeMilliseconds);
        if (!response)
            return ResumeTokenStoreError::DeadlineOverflow;

        const auto principal = current.principal;
        const auto session = current.session;
        const auto priorGeneration = current.generation;
        mRecords[*slot] = Record{ replacementDigest, principal, session, *nextGeneration, context, *expiresAt };
        ResumeAdmissionGrant grant(session, priorGeneration, *nextGeneration, std::move(*response));
        return AuthenticatedAdmission(principal, std::optional<ResumeAdmissionGrant>{ std::move(grant) });
    }

    std::size_t ResumeTokenStore::size() const noexcept
    {
        const std::scoped_lock lock(mMutex);
        return mSize;
    }

    namespace
    {
        class ImmediateAuthenticationOperation final : public AuthenticationOperation
        {
        public:
            ImmediateAuthenticationOperation(AuthenticationAttempt attempt, AuthenticationResult result) noexcept
                : mAttempt(attempt)
                , mResult(std::move(result))
            {
            }

            AuthenticationPollResult poll() noexcept override
            {
                if (!mResult)
                    return AuthenticationPending{};
                auto result = std::move(*mResult);
                mResult.reset();
                return AuthenticationCompletion{ mAttempt, std::move(result) };
            }

            void cancel() noexcept override { mResult.reset(); }

        private:
            AuthenticationAttempt mAttempt;
            std::optional<AuthenticationResult> mResult;
        };

        class ResumeAuthenticationOperation final : public AuthenticationOperation
        {
        public:
            ResumeAuthenticationOperation(AuthenticationAttempt attempt, ResumeTokenStore& store, ResumeToken token,
                ResumeTokenContext context, MonotonicClock& clock) noexcept
                : mAttempt(attempt)
                , mStore(store)
                , mToken(std::move(token))
                , mContext(context)
                , mClock(clock)
            {
            }

            AuthenticationPollResult poll() noexcept override
            {
                if (!mToken)
                    return AuthenticationPending{};
                auto consumed = mStore.consume(*mToken, mContext, mClock.now());
                mToken.reset();
                if (auto* admission = std::get_if<AuthenticatedAdmission>(&consumed))
                    return AuthenticationCompletion{ mAttempt, std::move(*admission) };
                const auto error = std::get<ResumeTokenStoreError>(consumed);
                return AuthenticationCompletion{ mAttempt,
                    AuthenticationRejected{ error == ResumeTokenStoreError::Denied
                            ? AuthenticationRejectionReason::Denied
                            : AuthenticationRejectionReason::ProviderUnavailable } };
            }

            void cancel() noexcept override { mToken.reset(); }

        private:
            AuthenticationAttempt mAttempt;
            ResumeTokenStore& mStore;
            std::optional<ResumeToken> mToken;
            ResumeTokenContext mContext;
            MonotonicClock& mClock;
        };

        std::unique_ptr<AuthenticationOperation> immediate(
            AuthenticationAttempt attempt, AuthenticationResult result) noexcept
        {
            return std::unique_ptr<AuthenticationOperation>(
                new (std::nothrow) ImmediateAuthenticationOperation(attempt, std::move(result)));
        }
    }

    std::unique_ptr<AuthenticationOperation> SharedServerAuthenticationService::begin(
        AuthenticationAttempt attempt, ServerAuthenticationSubmission submission) noexcept
    {
        if (mLimiter.allow(submission.mScope, mClock.now()) != AuthenticationRateLimitResult::Allowed)
            return immediate(attempt, AuthenticationRejected{ AuthenticationRejectionReason::ProviderUnavailable });

        if (submission.mRequest.kind() == AuthenticationCredentialKind::JoinPassword)
            return mJoinProvider.begin(attempt, submission.mRequest.takeMaterial());

        auto token = submission.mRequest.takeResumeToken();
        if (!token)
            return immediate(attempt, AuthenticationRejected{ AuthenticationRejectionReason::Denied });
        return std::unique_ptr<AuthenticationOperation>(new (std::nothrow) ResumeAuthenticationOperation(
            attempt, mResumeStore, std::move(*token), submission.mContext, mClock));
    }

    ResumeTokenIssueResult SharedServerAuthenticationService::issueInitial(PrincipalId principal, SessionId session,
        SessionGeneration generation, ResumeTokenContext context) noexcept
    {
        return mResumeStore.issue(principal, session, generation, context, mClock.now());
    }
}
