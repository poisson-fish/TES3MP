#include <tes3mp/server_authentication.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace TES3MP
{
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
}
