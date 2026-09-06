#include <tes3mp/player_identity.hpp>

#include <algorithm>
#include <limits>

namespace
{
    template <class Value>
    std::optional<Value> advance(Value value) noexcept
    {
        if (value.value() == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        return Value::fromValue(value.value() + 1);
    }
}

namespace TES3MP
{
    PlayerIdentityRegistry::PlayerIdentityRegistry(CredentialCrypto& crypto,
        PlayerIdentityPersistence& persistence, std::vector<PersistedPlayerIdentity> records,
        PlayerId nextPlayer, EntityId nextEntity) noexcept
        : mCrypto(crypto), mPersistence(persistence), mRecords(std::move(records)),
          mNextPlayer(nextPlayer), mNextEntity(nextEntity)
    {
    }

    std::variant<std::unique_ptr<PlayerIdentityRegistry>, PlayerIdentityError> PlayerIdentityRegistry::create(
        CredentialCrypto& crypto, PlayerIdentityPersistence& persistence,
        std::span<const PersistedPlayerIdentity> initialRecords) noexcept
    try
    {
        if (initialRecords.size() > MaximumPlayerIdentityRecords)
            return PlayerIdentityError::InvalidInitialState;
        std::vector<PersistedPlayerIdentity> records(initialRecords.begin(), initialRecords.end());
        std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
            return left.claim.player < right.claim.player;
        });
        std::uint64_t maximumPlayer = 0;
        std::uint64_t maximumEntity = 0;
        for (std::size_t index = 0; index < records.size(); ++index)
        {
            const auto& record = records[index];
            if ((index != 0 && records[index - 1].claim.player == record.claim.player)
                || std::any_of(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(index),
                    [&](const auto& previous) { return previous.claim.entity == record.claim.entity
                        || previous.credentialDigest == record.credentialDigest; }))
                return PlayerIdentityError::InvalidInitialState;
            maximumPlayer = std::max(maximumPlayer, record.claim.player.value());
            maximumEntity = std::max(maximumEntity, record.claim.entity.value());
        }
        if (maximumPlayer == std::numeric_limits<std::uint64_t>::max()
            || maximumEntity == std::numeric_limits<std::uint64_t>::max())
            return PlayerIdentityError::IdentityExhausted;
        const auto nextPlayer = PlayerId::fromValue(maximumPlayer == 0 ? 1 : maximumPlayer + 1);
        const auto nextEntity = EntityId::fromValue(maximumEntity == 0 ? 1 : maximumEntity + 1);
        if (!nextPlayer || !nextEntity)
            return PlayerIdentityError::IdentityExhausted;
        return std::unique_ptr<PlayerIdentityRegistry>(new PlayerIdentityRegistry(
            crypto, persistence, std::move(records), *nextPlayer, *nextEntity));
    }
    catch (...)
    {
        return PlayerIdentityError::InvalidInitialState;
    }

    bool PlayerIdentityRegistry::digest(
        const PlayerCredential& credential, CredentialDigest& destination) noexcept
    {
        return mCrypto.sha256(credential.secretBytes(), destination);
    }

    PlayerIdentityPrepareResult PlayerIdentityRegistry::prepareCreate(ContentManifest contentManifest) noexcept
    try
    {
        if (mPending || mCommitted)
            return PlayerIdentityError::PreparationPending;
        if (mRecords.size() >= MaximumPlayerIdentityRecords)
            return PlayerIdentityError::Full;
        if (mIdentityExhausted || mNextPreparationId == 0)
            return PlayerIdentityError::IdentityExhausted;
        std::array<std::byte, PlayerCredentialBytes> bytes{};
        if (!mCrypto.randomBytes(bytes))
            return PlayerIdentityError::RandomUnavailable;
        auto credential = PlayerCredential::create(bytes);
        std::fill(bytes.begin(), bytes.end(), std::byte{});
        CredentialDigest digestValue;
        if (!credential || !digest(*credential, digestValue))
            return PlayerIdentityError::DigestUnavailable;
        if (std::any_of(mRecords.begin(), mRecords.end(), [&](const auto& record) {
                return record.credentialDigest == digestValue;
            }))
            return PlayerIdentityError::DigestCollision;
        const AuthenticatedAdmission::PlayerClaim claim{ mNextPlayer, mNextEntity,
            contentManifest.defaultAppearance(), contentManifest.id() };
        const auto id = mNextPreparationId++;
        mPending.emplace(Pending{ id, { claim, digestValue }, std::move(*credential) });
        return PreparedPlayerIdentity{ id, claim };
    }
    catch (...)
    {
        return PlayerIdentityError::Full;
    }

    std::optional<PlayerCredential> PlayerIdentityRegistry::copyPreparedCredential(
        std::uint64_t preparationId) const noexcept
    {
        if (!mPending || mPending->id != preparationId)
            return std::nullopt;
        return PlayerCredential::create(mPending->credential.secretBytes());
    }

    bool PlayerIdentityRegistry::commit(std::uint64_t preparationId) noexcept
    try
    {
        if (!mPending || mPending->id != preparationId || mCommitted)
            return false;
        std::vector<PersistedPlayerIdentity> candidate = mRecords;
        candidate.push_back(mPending->record);
        std::sort(candidate.begin(), candidate.end(), [](const auto& left, const auto& right) {
            return left.claim.player < right.claim.player;
        });
        Committed committed{
            preparationId, mRecords, mNextPlayer, mNextEntity, mIdentityExhausted };
        if (!mPersistence.replace(candidate))
            return false;
        mCommitted.emplace(std::move(committed));
        mRecords = std::move(candidate);
        const auto nextPlayer = advance(mNextPlayer);
        const auto nextEntity = advance(mNextEntity);
        if (nextPlayer && nextEntity)
        {
            mNextPlayer = *nextPlayer;
            mNextEntity = *nextEntity;
        }
        else
            mIdentityExhausted = true;
        mPending.reset();
        return true;
    }
    catch (...)
    {
        return false;
    }

    bool PlayerIdentityRegistry::finalize(std::uint64_t preparationId) noexcept
    {
        if (!mCommitted || mCommitted->id != preparationId)
            return false;
        mCommitted.reset();
        return true;
    }

    bool PlayerIdentityRegistry::rollback(std::uint64_t preparationId) noexcept
    {
        if (!mCommitted || mCommitted->id != preparationId || !mPersistence.replace(mCommitted->prior))
            return false;
        mRecords = std::move(mCommitted->prior);
        mNextPlayer = mCommitted->priorNextPlayer;
        mNextEntity = mCommitted->priorNextEntity;
        mIdentityExhausted = mCommitted->priorIdentityExhausted;
        mCommitted.reset();
        return true;
    }

    bool PlayerIdentityRegistry::cancel(std::uint64_t preparationId) noexcept
    {
        if (!mPending || mPending->id != preparationId)
            return false;
        mPending.reset();
        return true;
    }

    std::optional<AuthenticatedAdmission::PlayerClaim> PlayerIdentityRegistry::authenticate(
        const PlayerCredential& credential, ContentManifestId contentManifest) noexcept
    {
        CredentialDigest digestValue;
        if (!digest(credential, digestValue))
            return std::nullopt;
        for (const auto& record : mRecords)
            if (record.claim.contentManifest == contentManifest
                && mCrypto.constantTimeEqual(record.credentialDigest.bytes, digestValue.bytes))
                return record.claim;
        return std::nullopt;
    }
}
