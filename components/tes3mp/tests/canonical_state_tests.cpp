#include <tes3mp/canonical_state.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    TES3MP::PlayerId playerId(std::uint64_t value)
    {
        return TES3MP::PlayerId::fromValue(value).value();
    }
    TES3MP::EntityId entityId(std::uint64_t value)
    {
        return TES3MP::EntityId::fromValue(value).value();
    }
    TES3MP::SessionId sessionId(std::uint64_t value)
    {
        return TES3MP::SessionId::fromValue(value).value();
    }

    TES3MP::Transform transform(std::uint64_t cell, std::int64_t position)
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        return TES3MP::Transform(TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(cell).value()),
            TES3MP::Position3(position, position + 1, position + 2), TES3MP::Orientation3(zero, zero, zero));
    }

    TES3MP::CanonicalPlayerEntityState player(std::uint64_t player, std::uint64_t entity, std::uint64_t revision = 1,
        std::uint64_t tick = 0, std::uint64_t epoch = 1)
    {
        return TES3MP::CanonicalPlayerEntityState(playerId(player), entityId(entity), transform(player, player * 10),
            TES3MP::LinearVelocity3(static_cast<std::int64_t>(player), 0, 0),
            TES3MP::EntityRevision::fromValue(revision).value(), TES3MP::AuthorityEpoch::fromValue(epoch).value(),
            TES3MP::ServerTick::fromValue(tick).value());
    }

    TES3MP::CanonicalSessionProgress session(std::uint64_t session, std::uint64_t player, std::uint64_t entity,
        std::optional<TES3MP::CommandSequence> acknowledgement = std::nullopt)
    {
        return TES3MP::CanonicalSessionProgress(sessionId(session), TES3MP::SessionGeneration::initial(),
            playerId(player), entityId(entity), acknowledgement);
    }

    bool hasStateError(const TES3MP::CanonicalStateCreationResult& result, TES3MP::CanonicalStateErrorCode code)
    {
        const auto error = std::get_if<TES3MP::CanonicalStateError>(&result);
        return error != nullptr && error->code == code;
    }

    bool player_entity_and_active_session_state_have_distinct_scope_and_lifetime()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(7, 1, 101) };
        const auto result = TES3MP::createCanonicalServerState(players, sessions);
        const auto state = std::get_if<TES3MP::CanonicalServerState>(&result);
        return state != nullptr && state->players().size() == 1 && state->activeSessions().size() == 1
            && state->players().front().entityId() == state->activeSessions().front().entityId()
            && state->players().front().entityRevision().value() == 1
            && state->activeSessions().front().sessionGeneration().value() == 1;
    }

    bool player_may_exist_without_session_but_session_requires_matching_player_entity()
    {
        const std::array players{ player(1, 101) };
        const auto playerOnly = TES3MP::createCanonicalServerState(players, {});
        const std::array missing{ session(1, 2, 202) };
        const auto missingResult = TES3MP::createCanonicalServerState(players, missing);
        const std::array mismatch{ session(1, 1, 202) };
        const auto mismatchResult = TES3MP::createCanonicalServerState(players, mismatch);
        return std::holds_alternative<TES3MP::CanonicalServerState>(playerOnly)
            && hasStateError(missingResult, TES3MP::CanonicalStateErrorCode::SessionPlayerMissing)
            && hasStateError(mismatchResult, TES3MP::CanonicalStateErrorCode::SessionEntityMismatch);
    }

    bool two_sessions_bind_explicitly_to_two_distinct_player_entity_pairs()
    {
        const std::array players{ player(1, 101), player(2, 202), player(3, 303) };
        const std::array sessions{ session(10, 1, 101), session(20, 2, 202) };
        const auto result = TES3MP::createCanonicalServerState(players, sessions);
        const auto state = std::get_if<TES3MP::CanonicalServerState>(&result);
        return state != nullptr && state->findPlayer(playerId(3)) != nullptr
            && state->findActiveSession(sessionId(10))->playerId() == playerId(1)
            && state->findActiveSession(sessionId(20))->entityId() == entityId(202)
            && state->findActiveSession(sessionId(30)) == nullptr;
    }

    bool player_and_session_inputs_require_strict_stable_identity_order()
    {
        const std::array unorderedPlayers{ player(2, 202), player(1, 101) };
        const std::array players{ player(1, 101), player(2, 202) };
        const std::array unorderedSessions{ session(20, 2, 202), session(10, 1, 101) };
        return hasStateError(TES3MP::createCanonicalServerState(unorderedPlayers, {}),
                   TES3MP::CanonicalStateErrorCode::PlayerIdsNotStrictlyOrdered)
            && hasStateError(TES3MP::createCanonicalServerState(players, unorderedSessions),
                TES3MP::CanonicalStateErrorCode::SessionIdsNotStrictlyOrdered);
    }

    bool duplicate_player_entity_session_or_active_binding_fails_without_partial_state()
    {
        const std::array duplicatePlayers{ player(1, 101), player(1, 102) };
        const std::array duplicateEntities{ player(1, 101), player(2, 101) };
        const std::array players{ player(1, 101), player(2, 202) };
        const std::array duplicateSessions{ session(10, 1, 101), session(10, 2, 202) };
        const std::array duplicateBinding{ session(10, 1, 101), session(20, 1, 101) };
        return hasStateError(TES3MP::createCanonicalServerState(duplicatePlayers, {}),
                   TES3MP::CanonicalStateErrorCode::PlayerIdsNotStrictlyOrdered)
            && hasStateError(TES3MP::createCanonicalServerState(duplicateEntities, {}),
                TES3MP::CanonicalStateErrorCode::DuplicateEntityId)
            && hasStateError(TES3MP::createCanonicalServerState(players, duplicateSessions),
                TES3MP::CanonicalStateErrorCode::SessionIdsNotStrictlyOrdered)
            && hasStateError(TES3MP::createCanonicalServerState(players, duplicateBinding),
                TES3MP::CanonicalStateErrorCode::DuplicateActiveBinding);
    }

    bool player_and_session_limits_accept_256_and_reject_257_before_owned_copy()
    {
        std::vector<TES3MP::CanonicalPlayerEntityState> players;
        std::vector<TES3MP::CanonicalSessionProgress> sessions;
        players.reserve(TES3MP::MaximumCanonicalPlayerEntities + 1);
        sessions.reserve(TES3MP::MaximumCanonicalActiveSessions + 1);
        for (std::size_t index = 0; index < TES3MP::MaximumCanonicalPlayerEntities; ++index)
        {
            const auto value = static_cast<std::uint64_t>(index + 1);
            players.push_back(player(value, value + 1000));
            sessions.push_back(session(value, value, value + 1000));
        }
        const auto boundary = TES3MP::createCanonicalServerState(players, sessions);
        players.push_back(player(257, 1257));
        const auto tooManyPlayers = TES3MP::createCanonicalServerState(players, sessions);
        players.pop_back();
        sessions.push_back(session(257, 1, 1001));
        const auto tooManySessions = TES3MP::createCanonicalServerState(players, sessions);
        return std::holds_alternative<TES3MP::CanonicalServerState>(boundary)
            && hasStateError(tooManyPlayers, TES3MP::CanonicalStateErrorCode::PlayerLimitExceeded)
            && hasStateError(tooManySessions, TES3MP::CanonicalStateErrorCode::ActiveSessionLimitExceeded);
    }

    bool spatial_advance_atomically_replaces_cell_root_velocity_and_increments_revision_once()
    {
        const auto original = player(1, 101, 7, 10, 3);
        const auto replacementTransform = transform(9, 900);
        const TES3MP::LinearVelocity3 replacementVelocity(4, 5, 6);
        const auto result = TES3MP::advanceCanonicalSpatialState(
            original, TES3MP::ServerTick::fromValue(11).value(), replacementTransform, replacementVelocity);
        const auto advanced = std::get_if<TES3MP::CanonicalPlayerEntityState>(&result);
        return advanced != nullptr && advanced->transform() == replacementTransform
            && advanced->linearVelocity() == replacementVelocity && advanced->entityRevision().value() == 8
            && advanced->lastSpatialChangeTick().value() == 11 && original.entityRevision().value() == 7
            && original.transform() != replacementTransform;
    }

    bool two_spatial_advances_in_one_tick_produce_two_monotonic_revisions()
    {
        const auto original = player(1, 101, 7, 10);
        const auto firstResult = TES3MP::advanceCanonicalSpatialState(
            original, TES3MP::ServerTick::fromValue(11).value(), transform(2, 20), TES3MP::LinearVelocity3(1, 2, 3));
        const auto first = std::get_if<TES3MP::CanonicalPlayerEntityState>(&firstResult);
        if (first == nullptr)
            return false;
        const auto secondResult = TES3MP::advanceCanonicalSpatialState(
            *first, TES3MP::ServerTick::fromValue(11).value(), transform(3, 30), TES3MP::LinearVelocity3(4, 5, 6));
        const auto second = std::get_if<TES3MP::CanonicalPlayerEntityState>(&secondResult);
        return second != nullptr && first->entityRevision().value() == 8 && second->entityRevision().value() == 9
            && first->lastSpatialChangeTick() == second->lastSpatialChangeTick();
    }

    bool tick_regression_and_revision_exhaustion_preserve_the_original_value()
    {
        const auto original = player(1, 101, 7, 10);
        const auto regressed = TES3MP::advanceCanonicalSpatialState(
            original, TES3MP::ServerTick::fromValue(9).value(), transform(2, 20), TES3MP::LinearVelocity3(1, 2, 3));
        const auto maximum = player(1, 101, std::numeric_limits<std::uint64_t>::max(), 10);
        const auto exhausted = TES3MP::advanceCanonicalSpatialState(
            maximum, TES3MP::ServerTick::fromValue(10).value(), transform(2, 20), TES3MP::LinearVelocity3(1, 2, 3));
        const auto regressionError = std::get_if<TES3MP::SpatialAdvanceError>(&regressed);
        const auto exhaustionError = std::get_if<TES3MP::SpatialAdvanceError>(&exhausted);
        return regressionError != nullptr && exhaustionError != nullptr
            && regressionError->code == TES3MP::SpatialAdvanceErrorCode::TickRegression
            && exhaustionError->code == TES3MP::SpatialAdvanceErrorCode::RevisionExhausted
            && original == player(1, 101, 7, 10)
            && maximum.entityRevision().value() == std::numeric_limits<std::uint64_t>::max();
    }

    bool spatial_advance_cannot_change_identity_or_authority_epoch()
    {
        const auto original = player(7, 707, 3, 4, 9);
        const auto result = TES3MP::advanceCanonicalSpatialState(
            original, TES3MP::ServerTick::fromValue(5).value(), transform(8, 80), TES3MP::LinearVelocity3(1, 1, 1));
        const auto advanced = std::get_if<TES3MP::CanonicalPlayerEntityState>(&result);
        return advanced != nullptr && advanced->playerId() == original.playerId()
            && advanced->entityId() == original.entityId() && advanced->authorityEpoch() == original.authorityEpoch();
    }

    bool session_ack_is_optional_disposition_progress_not_success_or_entity_revision()
    {
        const std::array players{ player(1, 101, 12, 8) };
        const std::array withoutAck{ session(7, 1, 101) };
        const std::array withAck{ session(7, 1, 101, TES3MP::CommandSequence::fromValue(19).value()) };
        const auto beforeResult = TES3MP::createCanonicalServerState(players, withoutAck);
        const auto afterResult = TES3MP::createCanonicalServerState(players, withAck);
        const auto before = std::get_if<TES3MP::CanonicalServerState>(&beforeResult);
        const auto after = std::get_if<TES3MP::CanonicalServerState>(&afterResult);
        return before != nullptr && after != nullptr
            && !before->activeSessions().front().highestContiguousFinalizedCommand()
            && after->activeSessions().front().highestContiguousFinalizedCommand()->value() == 19
            && std::equal(before->players().begin(), before->players().end(), after->players().begin())
            && after->players().front().entityRevision().value() == 12;
    }

    template <class Type>
    concept HasMutablePlayers = requires(Type& value) {
        { value.players() } -> std::same_as<std::span<TES3MP::CanonicalPlayerEntityState>>;
    };

    template <class Type>
    concept HasInsert = requires(Type& value, TES3MP::CanonicalPlayerEntityState player) { value.insert(player); };

    bool state_exposes_const_ordered_values_without_wire_engine_or_bypass_mutation_surface()
    {
        static_assert(!HasMutablePlayers<TES3MP::CanonicalServerState>);
        static_assert(!HasInsert<TES3MP::CanonicalServerState>);
        static_assert(std::is_same_v<decltype(std::declval<const TES3MP::CanonicalServerState&>().players()),
            std::span<const TES3MP::CanonicalPlayerEntityState>>);
        const std::array players{ player(1, 101), player(2, 202) };
        const auto result = TES3MP::createCanonicalServerState(players, {});
        const auto state = std::get_if<TES3MP::CanonicalServerState>(&result);
        return state != nullptr && state->players()[0].playerId() < state->players()[1].playerId();
    }
}

int main()
{
    return player_entity_and_active_session_state_have_distinct_scope_and_lifetime()
            && player_may_exist_without_session_but_session_requires_matching_player_entity()
            && two_sessions_bind_explicitly_to_two_distinct_player_entity_pairs()
            && player_and_session_inputs_require_strict_stable_identity_order()
            && duplicate_player_entity_session_or_active_binding_fails_without_partial_state()
            && player_and_session_limits_accept_256_and_reject_257_before_owned_copy()
            && spatial_advance_atomically_replaces_cell_root_velocity_and_increments_revision_once()
            && two_spatial_advances_in_one_tick_produce_two_monotonic_revisions()
            && tick_regression_and_revision_exhaustion_preserve_the_original_value()
            && spatial_advance_cannot_change_identity_or_authority_epoch()
            && session_ack_is_optional_disposition_progress_not_success_or_entity_revision()
            && state_exposes_const_ordered_values_without_wire_engine_or_bypass_mutation_surface()
        ? 0
        : 1;
}
