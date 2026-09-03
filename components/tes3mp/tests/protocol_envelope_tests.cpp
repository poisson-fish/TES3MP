#include <tes3mp/protocol_envelope.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    template <class Type>
    concept HasWriterAdmission = requires(const Type& value) {
        value.eligibleServerTick();
        value.ingressOrdinal();
    };

    template <class Type>
    concept HasCanonicalResult = requires(const Type& value) { value.canonicalResult(); };

    template <class Type>
    concept HasGenericPayload = requires(const Type& value) { value.payload(); };

    template <class Type>
    concept HasAcceptanceFlag = requires(const Type& value) { value.accepted(); };

    template <class Type>
    concept HasSnapshotSequence = requires(const Type& value) { value.snapshotSequence(); };

    template <class Type>
    concept HasEntityRevision = requires(const Type& value) { value.entityRevision(); };

    static_assert(!std::is_default_constructible_v<TES3MP::ReliableOperationHeader>);
    static_assert(!std::is_default_constructible_v<TES3MP::LatestWinsSnapshotHeader>);
    static_assert(!HasWriterAdmission<TES3MP::ReliableOperationHeader>);
    static_assert(!HasCanonicalResult<TES3MP::ReliableOperationHeader>);
    static_assert(!HasGenericPayload<TES3MP::ReliableOperationHeader>);
    static_assert(!HasGenericPayload<TES3MP::LatestWinsSnapshotHeader>);
    static_assert(!HasAcceptanceFlag<TES3MP::LatestWinsSnapshotHeader>);
    static_assert(!HasSnapshotSequence<TES3MP::LatestWinsSnapshotHeader>);
    static_assert(!HasEntityRevision<TES3MP::LatestWinsSnapshotHeader>);

    TES3MP::ClientCommandHeader makeCommandHeader()
    {
        return TES3MP::ClientCommandHeader(TES3MP::SessionId::fromValue(41).value(),
            TES3MP::SessionGeneration::fromValue(3).value(), TES3MP::CommandSequence::fromValue(7).value(),
            TES3MP::CommandId::fromValue(91).value(), TES3MP::CanonicalRevision::fromValue(29).value());
    }

    TES3MP::EntityPrecondition makePrecondition()
    {
        return TES3MP::EntityPrecondition(TES3MP::EntityId::fromValue(17).value(),
            TES3MP::EntityRevision::fromValue(5).value(), TES3MP::AuthorityEpoch::fromValue(2).value());
    }

    bool reliable_header_retains_session_generation_sequence_id_and_observed_tick()
    {
        const TES3MP::ReliableOperationHeader value(makeCommandHeader(), std::nullopt);
        const auto& header = value.commandHeader();
        return header.sessionId().value() == 41 && header.sessionGeneration().value() == 3
            && header.commandSequence().value() == 7 && header.commandId().value() == 91
            && header.observedCanonicalRevision().value() == 29;
    }

    bool reliable_precondition_is_explicitly_optional()
    {
        const TES3MP::ReliableOperationHeader absent(makeCommandHeader(), std::nullopt);
        const TES3MP::ReliableOperationHeader present(makeCommandHeader(), makePrecondition());
        return !absent.entityPrecondition().has_value() && present.entityPrecondition().has_value()
            && present.entityPrecondition()->entityId().value() == 17
            && present.entityPrecondition()->expectedRevision().value() == 5
            && present.entityPrecondition()->expectedAuthorityEpoch().value() == 2;
    }

    bool one_reliable_envelope_is_one_apply_once_unit()
    {
        const TES3MP::ReliableOperationHeader value(makeCommandHeader(), makePrecondition());
        return std::is_same_v<decltype(value.commandHeader()), const TES3MP::ClientCommandHeader&>
            && value.commandHeader().commandId().value() == 91 && value.commandHeader().commandSequence().value() == 7;
    }

    bool reliable_envelope_exposes_no_writer_admission_or_canonical_result()
    {
        return !HasWriterAdmission<TES3MP::ReliableOperationHeader>
            && !HasCanonicalResult<TES3MP::ReliableOperationHeader>;
    }

    bool snapshot_header_binds_target_session_generation_and_server_tick()
    {
        const TES3MP::LatestWinsSnapshotHeader value(TES3MP::SessionId::fromValue(41).value(),
            TES3MP::SessionGeneration::fromValue(3).value(), TES3MP::PlayerId::fromValue(51).value(),
            TES3MP::EntityId::fromValue(61).value(), TES3MP::CanonicalRevision::fromValue(30).value(), std::nullopt);
        return value.targetSessionId().value() == 41 && value.targetSessionGeneration().value() == 3
            && value.targetPlayerId().value() == 51 && value.targetEntityId().value() == 61
            && value.canonicalRevision().value() == 30;
    }

    bool snapshot_ack_is_absent_before_any_finalized_command()
    {
        const TES3MP::LatestWinsSnapshotHeader value(TES3MP::SessionId::fromValue(41).value(),
            TES3MP::SessionGeneration::fromValue(3).value(), TES3MP::PlayerId::fromValue(51).value(),
            TES3MP::EntityId::fromValue(61).value(), TES3MP::CanonicalRevision::fromValue(30).value(), std::nullopt);
        return !value.acknowledgedCommandSequence().has_value();
    }

    bool snapshot_ack_means_contiguous_finalized_progress_not_acceptance()
    {
        const TES3MP::LatestWinsSnapshotHeader value(TES3MP::SessionId::fromValue(41).value(),
            TES3MP::SessionGeneration::fromValue(3).value(), TES3MP::PlayerId::fromValue(51).value(),
            TES3MP::EntityId::fromValue(61).value(), TES3MP::CanonicalRevision::fromValue(31).value(),
            TES3MP::CommandSequence::fromValue(7).value());
        return value.acknowledgedCommandSequence()->value() == 7
            && !HasAcceptanceFlag<TES3MP::LatestWinsSnapshotHeader>;
    }

    bool snapshot_recency_uses_server_tick_without_snapshot_sequence()
    {
        const TES3MP::LatestWinsSnapshotHeader older(TES3MP::SessionId::fromValue(41).value(),
            TES3MP::SessionGeneration::fromValue(3).value(), TES3MP::PlayerId::fromValue(51).value(),
            TES3MP::EntityId::fromValue(61).value(), TES3MP::CanonicalRevision::fromValue(30).value(), std::nullopt);
        const TES3MP::LatestWinsSnapshotHeader newer(TES3MP::SessionId::fromValue(41).value(),
            TES3MP::SessionGeneration::fromValue(3).value(), TES3MP::PlayerId::fromValue(51).value(),
            TES3MP::EntityId::fromValue(61).value(), TES3MP::CanonicalRevision::fromValue(31).value(), std::nullopt);
        return older.canonicalRevision() < newer.canonicalRevision() && !HasSnapshotSequence<TES3MP::LatestWinsSnapshotHeader>;
    }

    bool entity_revision_and_authority_epoch_remain_entry_scoped()
    {
        const auto precondition = makePrecondition();
        return !HasEntityRevision<TES3MP::LatestWinsSnapshotHeader> && precondition.expectedRevision().value() == 5
            && precondition.expectedAuthorityEpoch().value() == 2;
    }

    bool envelope_headers_have_no_generic_bytes_strings_maps_or_generated_views()
    {
        return !HasGenericPayload<TES3MP::ReliableOperationHeader>
            && !HasGenericPayload<TES3MP::LatestWinsSnapshotHeader>
            && !std::is_constructible_v<TES3MP::ReliableOperationHeader, std::span<const std::byte>>
            && !std::is_constructible_v<TES3MP::LatestWinsSnapshotHeader, std::vector<std::byte>>
            && !std::is_constructible_v<TES3MP::ReliableOperationHeader, std::string>;
    }

    bool envelope_headers_compile_without_openmw_transport_flatbuffers_or_test_support()
    {
        const TES3MP::ReliableOperationHeader reliable(makeCommandHeader(), std::nullopt);
        const TES3MP::LatestWinsSnapshotHeader snapshot(reliable.commandHeader().sessionId(),
            reliable.commandHeader().sessionGeneration(), TES3MP::PlayerId::fromValue(51).value(),
            TES3MP::EntityId::fromValue(61).value(), TES3MP::CanonicalRevision::fromValue(30).value(), std::nullopt);
        return snapshot.targetSessionId() == reliable.commandHeader().sessionId();
    }

    bool identical_metadata_inputs_produce_identical_owned_values()
    {
        const TES3MP::ReliableOperationHeader reliableLeft(makeCommandHeader(), makePrecondition());
        const TES3MP::ReliableOperationHeader reliableRight(makeCommandHeader(), makePrecondition());
        const TES3MP::LatestWinsSnapshotHeader snapshotLeft(TES3MP::SessionId::fromValue(41).value(),
            TES3MP::SessionGeneration::fromValue(3).value(), TES3MP::PlayerId::fromValue(51).value(),
            TES3MP::EntityId::fromValue(61).value(), TES3MP::CanonicalRevision::fromValue(31).value(),
            TES3MP::CommandSequence::fromValue(7).value());
        const TES3MP::LatestWinsSnapshotHeader snapshotRight = snapshotLeft;
        return reliableLeft == reliableRight && snapshotLeft == snapshotRight;
    }
}

int main()
{
    return reliable_header_retains_session_generation_sequence_id_and_observed_tick()
            && reliable_precondition_is_explicitly_optional() && one_reliable_envelope_is_one_apply_once_unit()
            && reliable_envelope_exposes_no_writer_admission_or_canonical_result()
            && snapshot_header_binds_target_session_generation_and_server_tick()
            && snapshot_ack_is_absent_before_any_finalized_command()
            && snapshot_ack_means_contiguous_finalized_progress_not_acceptance()
            && snapshot_recency_uses_server_tick_without_snapshot_sequence()
            && entity_revision_and_authority_epoch_remain_entry_scoped()
            && envelope_headers_have_no_generic_bytes_strings_maps_or_generated_views()
            && envelope_headers_compile_without_openmw_transport_flatbuffers_or_test_support()
            && identical_metadata_inputs_produce_identical_owned_values()
        ? 0
        : 1;
}
