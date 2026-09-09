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

    std::vector<TES3MP::PersistedPlayerIdentity> durableRecords(
        const std::vector<TES3MP::PersistedPlayerIdentity>& live)
    {
        auto durable = live;
        for (auto& record : durable)
        {
            if (record.characterProfile.lifecycle() == TES3MP::CharacterLifecycle::EstablishedCharacter)
                continue;
            record.characterProfile = TES3MP::CharacterProfile::fresh();
            record.savedPlayer.reset();
        }
        return durable;
    }
}

namespace TES3MP
{
    PlayerIdentityRegistry::PlayerIdentityRegistry(CredentialCrypto& crypto,
        PlayerIdentityPersistence& persistence, std::vector<PersistedPlayerIdentity> records,
        std::vector<EntityId> reservedEntityIds, PlayerId nextPlayer, EntityId nextEntity) noexcept
        : mCrypto(crypto), mPersistence(persistence), mRecords(std::move(records)),
          mReservedEntityIds(std::move(reservedEntityIds)), mNextPlayer(nextPlayer), mNextEntity(nextEntity)
    {
    }

    std::variant<std::unique_ptr<PlayerIdentityRegistry>, PlayerIdentityError> PlayerIdentityRegistry::create(
        CredentialCrypto& crypto, PlayerIdentityPersistence& persistence,
        std::span<const PersistedPlayerIdentity> initialRecords,
        std::span<const EntityId> reservedEntityIds) noexcept
    try
    {
        if (initialRecords.size() > MaximumPlayerIdentityRecords)
            return PlayerIdentityError::InvalidInitialState;
        std::vector<PersistedPlayerIdentity> records(initialRecords.begin(), initialRecords.end());
        std::vector<EntityId> reserved(reservedEntityIds.begin(), reservedEntityIds.end());
        std::ranges::sort(reserved);
        if (std::ranges::adjacent_find(reserved) != reserved.end())
            return PlayerIdentityError::InvalidInitialState;
        std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
            return left.claim.player < right.claim.player;
        });
        std::uint64_t maximumPlayer = 0;
        std::uint64_t maximumEntity = 0;
        for (std::size_t index = 0; index < records.size(); ++index)
        {
            const auto& record = records[index];
            const bool established
                = record.characterProfile.lifecycle() == CharacterLifecycle::EstablishedCharacter;
            if ((!established && record.characterProfile != CharacterProfile::fresh())
                || established != record.savedPlayer.has_value()
                || (record.savedPlayer
                && (record.savedPlayer->playerId() != record.claim.player
                    || record.savedPlayer->entityId() != record.claim.entity
                    || record.savedPlayer->appearanceId() != record.claim.appearance)))
                return PlayerIdentityError::InvalidInitialState;
            if ((index != 0 && records[index - 1].claim.player == record.claim.player)
                || std::any_of(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(index),
                    [&](const auto& previous) { return previous.claim.entity == record.claim.entity
                        || previous.credentialDigest == record.credentialDigest; }))
                return PlayerIdentityError::InvalidInitialState;
            if (std::ranges::binary_search(reserved, record.claim.entity))
                return PlayerIdentityError::InvalidInitialState;
            maximumPlayer = std::max(maximumPlayer, record.claim.player.value());
            maximumEntity = std::max(maximumEntity, record.claim.entity.value());
        }
        if (maximumPlayer == std::numeric_limits<std::uint64_t>::max()
            || maximumEntity == std::numeric_limits<std::uint64_t>::max())
            return PlayerIdentityError::IdentityExhausted;
        const auto nextPlayer = PlayerId::fromValue(maximumPlayer == 0 ? 1 : maximumPlayer + 1);
        auto nextEntity = EntityId::fromValue(maximumEntity == 0 ? 1 : maximumEntity + 1);
        if (!nextPlayer || !nextEntity)
            return PlayerIdentityError::IdentityExhausted;
        while (std::ranges::binary_search(reserved, *nextEntity))
        {
            nextEntity = advance(*nextEntity);
            if (!nextEntity) return PlayerIdentityError::IdentityExhausted;
        }
        return std::unique_ptr<PlayerIdentityRegistry>(new PlayerIdentityRegistry(
            crypto, persistence, std::move(records), std::move(reserved), *nextPlayer, *nextEntity));
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

    bool PlayerIdentityRegistry::advanceIdentities() noexcept
    {
        auto nextPlayer = advance(mNextPlayer);
        auto nextEntity = advance(mNextEntity);
        while (nextEntity && std::ranges::binary_search(mReservedEntityIds, *nextEntity))
            nextEntity = advance(*nextEntity);
        if (!nextPlayer || !nextEntity)
        {
            mIdentityExhausted = true;
            return false;
        }
        mNextPlayer = *nextPlayer;
        mNextEntity = *nextEntity;
        return true;
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
        if (!mPersistence.replace(durableRecords(candidate)))
            return false;
        mCommitted.emplace(std::move(committed));
        mRecords = std::move(candidate);
        (void)advanceIdentities();
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
        if (!mCommitted || mCommitted->id != preparationId
            || !mPersistence.replace(durableRecords(mCommitted->prior)))
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

    const CanonicalPlayerEntityState* PlayerIdentityRegistry::savedPlayer(PlayerId player) const noexcept
    {
        const auto found = std::find_if(mRecords.begin(), mRecords.end(),
            [player](const auto& record) { return record.claim.player == player; });
        return found == mRecords.end() || !found->savedPlayer ? nullptr : &*found->savedPlayer;
    }

    bool PlayerIdentityRegistry::savePlayer(const CanonicalPlayerEntityState& player) noexcept
    {
        return savePlayers(std::span<const CanonicalPlayerEntityState>(&player, 1));
    }

    bool PlayerIdentityRegistry::savePlayers(std::span<const CanonicalPlayerEntityState> players) noexcept
    try
    {
        if (mPending || mCommitted || players.empty())
            return false;
        auto candidate = mRecords;
        std::vector<PlayerId> seen;
        seen.reserve(players.size());
        bool changed = false;
        for (const auto& player : players)
        {
            if (std::ranges::find(seen, player.playerId()) != seen.end())
                return false;
            seen.push_back(player.playerId());
            const auto found = std::find_if(candidate.begin(), candidate.end(), [&](const auto& record) {
                return record.claim.player == player.playerId() && record.claim.entity == player.entityId()
                    && record.claim.appearance == player.appearanceId();
            });
            if (found == candidate.end())
                return false;
            if (found->characterProfile.lifecycle() != CharacterLifecycle::EstablishedCharacter)
                continue;
            found->savedPlayer = player;
            changed = true;
        }
        if (!changed)
            return true;
        if (!mPersistence.replace(durableRecords(candidate)))
            return false;
        mRecords = std::move(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }

    const CharacterProfile* PlayerIdentityRegistry::characterProfile(PlayerId player) const noexcept
    {
        const auto found = std::find_if(mRecords.begin(), mRecords.end(),
            [player](const auto& record) { return record.claim.player == player; });
        return found == mRecords.end() ? nullptr : &found->characterProfile;
    }

    bool PlayerIdentityRegistry::restartIncompleteCharacter(PlayerId player) noexcept
    {
        const auto found = std::find_if(mRecords.begin(), mRecords.end(),
            [player](const auto& record) { return record.claim.player == player; });
        if (found == mRecords.end())
            return false;
        if (found->characterProfile.lifecycle() == CharacterLifecycle::EstablishedCharacter)
            return true;
        found->characterProfile = CharacterProfile::fresh();
        found->savedPlayer.reset();
        return true;
    }

    CharacterProfileApplyResult PlayerIdentityRegistry::applyCharacterCreation(PlayerId player,
        const CharacterContentCatalog& catalog, const CharacterCreationCommand& command,
        const CanonicalPlayerEntityState* completionCheckpoint) noexcept
    try
    {
        if (mPending || mCommitted)
            return CharacterProfileError::PersistenceFailed;
        auto candidate = mRecords;
        const auto found = std::find_if(candidate.begin(), candidate.end(),
            [player](const auto& record) { return record.claim.player == player; });
        if (found == candidate.end() || found->claim.contentManifest != catalog.manifest())
            return CharacterProfileError::InvalidInitialState;
        auto applied = TES3MP::applyCharacterCreation(found->characterProfile, catalog, command);
        auto* profile = std::get_if<CharacterProfile>(&applied);
        if (!profile)
            return std::get<CharacterProfileError>(applied);
        found->characterProfile = *profile;
        if (profile->lifecycle() == CharacterLifecycle::EstablishedCharacter)
        {
            if (!completionCheckpoint || completionCheckpoint->playerId() != found->claim.player
                || completionCheckpoint->entityId() != found->claim.entity
                || completionCheckpoint->appearanceId() != found->claim.appearance)
                return CharacterProfileError::InvalidInitialState;
            found->savedPlayer = *completionCheckpoint;
            if (!mPersistence.replace(durableRecords(candidate)))
                return CharacterProfileError::PersistenceFailed;
        }
        mRecords = std::move(candidate);
        return *profile;
    }
    catch (...)
    {
        return CharacterProfileError::AllocationFailure;
    }
}
