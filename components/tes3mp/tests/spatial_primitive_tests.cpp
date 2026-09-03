#include <tes3mp/command_primitives.hpp>
#include <tes3mp/test_support/spatial_round_trip.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

namespace
{
    template <class Type>
    concept HasTransform = requires(const Type& value) { value.transform(); };

    template <class Type>
    concept HasAdmissionOrder = requires(const Type& value) {
        value.eligibleServerTick();
        value.ingressOrdinal();
    };

    static_assert(!std::is_default_constructible_v<TES3MP::CellSpaceId>);
    static_assert(!std::is_default_constructible_v<TES3MP::CellId>);
    static_assert(!std::is_default_constructible_v<TES3MP::Position3>);
    static_assert(!std::is_default_constructible_v<TES3MP::Turn32>);
    static_assert(!std::is_default_constructible_v<TES3MP::Orientation3>);
    static_assert(!std::is_default_constructible_v<TES3MP::Transform>);
    static_assert(!std::is_default_constructible_v<TES3MP::LinearVelocity3>);
    static_assert(!std::is_default_constructible_v<TES3MP::ClientCommandHeader>);
    static_assert(!std::is_default_constructible_v<TES3MP::EntityPrecondition>);
    static_assert(!std::is_default_constructible_v<TES3MP::WriterAdmissionStamp>);
    static_assert(!std::is_default_constructible_v<TES3MP::SpatialEntitySnapshot>);
    static_assert(sizeof(TES3MP::Position3) == sizeof(std::int64_t) * 3);
    static_assert(sizeof(TES3MP::Turn32) == sizeof(std::uint32_t));
    static_assert(sizeof(TES3MP::Orientation3) == sizeof(std::uint32_t) * 3);
    static_assert(sizeof(TES3MP::LinearVelocity3) == sizeof(std::int64_t) * 3);
    static_assert(std::is_trivially_copyable_v<TES3MP::Position3>);
    static_assert(std::is_trivially_copyable_v<TES3MP::Turn32>);
    static_assert(std::is_trivially_copyable_v<TES3MP::Orientation3>);
    static_assert(std::is_trivially_copyable_v<TES3MP::LinearVelocity3>);
    static_assert(!HasTransform<TES3MP::ClientCommandHeader>);
    static_assert(!HasAdmissionOrder<TES3MP::ClientCommandHeader>);
    static_assert(HasAdmissionOrder<TES3MP::WriterAdmissionStamp>);

    constexpr std::uint32_t QuarterTurn = std::uint32_t{ 1 } << 30;

    struct BasisVector
    {
        int x;
        int y;
        int z;

        friend constexpr bool operator==(BasisVector, BasisVector) noexcept = default;
    };

    BasisVector rotateXQuarter(BasisVector value)
    {
        return { value.x, -value.z, value.y };
    }

    BasisVector rotateYQuarter(BasisVector value)
    {
        return { value.z, value.y, -value.x };
    }

    BasisVector rotateZQuarter(BasisVector value)
    {
        return { -value.y, value.x, value.z };
    }

    template <class Rotate>
    BasisVector rotateQuarterTurns(BasisVector value, TES3MP::Turn32 angle, Rotate rotate)
    {
        if (angle.value() % QuarterTurn != 0)
            return {};
        const std::uint32_t count = (angle.value() / QuarterTurn) % 4;
        for (std::uint32_t index = 0; index < count; ++index)
            value = rotate(value);
        return value;
    }

    BasisVector applyCanonicalOrientation(BasisVector value, TES3MP::Orientation3 orientation)
    {
        value = rotateQuarterTurns(value, orientation.x(), rotateXQuarter);
        value = rotateQuarterTurns(value, orientation.y(), rotateYQuarter);
        return rotateQuarterTurns(value, orientation.z(), rotateZQuarter);
    }

    TES3MP::SpatialEntitySnapshot makeSnapshot(TES3MP::CellId cell)
    {
        return TES3MP::SpatialEntitySnapshot(TES3MP::ServerTick::fromValue(47).value(),
            TES3MP::PlayerId::fromValue(5).value(),
            TES3MP::EntityId::fromValue(11).value(), TES3MP::EntityRevision::fromValue(9).value(),
            TES3MP::AuthorityEpoch::fromValue(3).value(),
            TES3MP::Transform(cell,
                TES3MP::Position3(std::numeric_limits<std::int64_t>::min(), 8'388'608,
                    std::numeric_limits<std::int64_t>::max()),
                TES3MP::Orientation3(TES3MP::Turn32::fromValue(0),
                    TES3MP::Turn32::fromValue(QuarterTurn),
                    TES3MP::Turn32::fromValue(std::numeric_limits<std::uint32_t>::max()))),
            TES3MP::LinearVelocity3(-1024, 0, std::numeric_limits<std::int64_t>::max()));
    }

    bool interior_and_exterior_cell_ids_are_distinct_and_totally_ordered()
    {
        const auto space = TES3MP::CellSpaceId::fromValue(7).value();
        const auto interior = TES3MP::CellId::interior(space);
        const auto exterior = TES3MP::CellId::exterior(space, -1, 2);
        const auto laterExterior = TES3MP::CellId::exterior(space, -1, 3);
        return interior.kind() == TES3MP::CellId::Kind::Interior && interior.asInterior() != nullptr
            && interior.asExterior() == nullptr && exterior.kind() == TES3MP::CellId::Kind::Exterior
            && exterior.asExterior() != nullptr && exterior.asInterior() == nullptr && interior != exterior
            && interior < exterior && exterior < laterExterior;
    }

    bool cell_id_has_no_openmw_string_path_or_transport_dependency()
    {
        const auto cell = TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(1).value());
        return cell.asInterior()->cellSpace().value() == 1 && !std::is_convertible_v<const char*, TES3MP::CellId>;
    }

    bool same_cell_space_value_in_different_content_contexts_is_not_interchangeable()
    {
        struct ContentScopedCell
        {
            std::uint64_t contentContext;
            TES3MP::CellId cell;
        };

        const auto cell = TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(5).value());
        const ContentScopedCell left{ 1, cell };
        const ContentScopedCell right{ 2, cell };
        return left.contentContext != right.contentContext && left.cell == right.cell;
    }

    bool position_uses_signed_i64_one_over_1024_unit_components()
    {
        const TES3MP::Position3 position(
            std::numeric_limits<std::int64_t>::min(), -1024, std::numeric_limits<std::int64_t>::max());
        return position.x() == std::numeric_limits<std::int64_t>::min() && position.y() == -1024
            && position.z() == std::numeric_limits<std::int64_t>::max();
    }

    bool exterior_boundary_crossing_does_not_renormalize_world_position()
    {
        const auto worldspace = TES3MP::CellSpaceId::fromValue(9).value();
        const TES3MP::Position3 boundaryPosition(8192LL * 1024, -8192LL * 1024, 16 * 1024);
        const TES3MP::Transform before(TES3MP::CellId::exterior(worldspace, 0, -1), boundaryPosition,
            TES3MP::Orientation3(
                TES3MP::Turn32::fromValue(0), TES3MP::Turn32::fromValue(0), TES3MP::Turn32::fromValue(0)));
        const TES3MP::Transform after(TES3MP::CellId::exterior(worldspace, 1, -1), boundaryPosition,
            before.orientation());
        return before.cell() != after.cell() && before.position() == after.position();
    }

    bool turn32_covers_exactly_one_modular_turn()
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        const auto maximum = TES3MP::Turn32::fromValue(std::numeric_limits<std::uint32_t>::max());
        const auto fullTurn = TES3MP::Turn32::fromUnnormalized(std::uint64_t{ 1 } << 32);
        const auto fullTurnPlusOne = TES3MP::Turn32::fromUnnormalized((std::uint64_t{ 1 } << 32) + 1);
        return zero != maximum && fullTurn == zero && fullTurnPlusOne.value() == 1;
    }

    bool orientation_composition_and_axis_sign_match_canonical_vectors()
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        const auto quarter = TES3MP::Turn32::fromValue(QuarterTurn);
        const auto yaw = TES3MP::Orientation3(zero, zero, quarter);
        const auto xyz = TES3MP::Orientation3(quarter, quarter, quarter);
        return applyCanonicalOrientation({ 1, 0, 0 }, yaw) == BasisVector{ 0, 1, 0 }
            && applyCanonicalOrientation({ 0, 0, 1 }, xyz) == BasisVector{ 1, 0, 0 };
    }

    bool transform_contains_only_cell_position_and_orientation_values()
    {
        const auto cell = TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(3).value());
        const TES3MP::Position3 position(1, 2, 3);
        const TES3MP::Orientation3 orientation(
            TES3MP::Turn32::fromValue(4), TES3MP::Turn32::fromValue(5), TES3MP::Turn32::fromValue(6));
        const TES3MP::Transform transform(cell, position, orientation);
        return transform.cell() == cell && transform.position() == position && transform.orientation() == orientation;
    }

    bool linear_velocity_uses_signed_position_quanta_per_server_tick()
    {
        const TES3MP::LinearVelocity3 velocity(
            std::numeric_limits<std::int64_t>::min(), -1, std::numeric_limits<std::int64_t>::max());
        return velocity.x() == std::numeric_limits<std::int64_t>::min() && velocity.y() == -1
            && velocity.z() == std::numeric_limits<std::int64_t>::max();
    }

    bool spatial_values_round_trip_through_owned_test_encoding()
    {
        const auto interior = makeSnapshot(
            TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(15).value()));
        const auto exterior = makeSnapshot(
            TES3MP::CellId::exterior(TES3MP::CellSpaceId::fromValue(21).value(),
                std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
        const auto interiorBytes = TES3MP::TestSupport::encodeSpatialEntitySnapshot(interior);
        const auto exteriorBytes = TES3MP::TestSupport::encodeSpatialEntitySnapshot(exterior);
        const auto decodedInterior = TES3MP::TestSupport::decodeSpatialEntitySnapshot(interiorBytes);
        const auto decodedExterior = TES3MP::TestSupport::decodeSpatialEntitySnapshot(exteriorBytes);
        return interiorBytes.size() == 109 && exteriorBytes.size() == 117 && decodedInterior == interior
            && decodedExterior == exterior;
    }

    bool client_command_header_contains_no_canonical_snapshot_payload()
    {
        const TES3MP::ClientCommandHeader header(TES3MP::SessionId::fromValue(4).value(),
            TES3MP::SessionGeneration::fromValue(2).value(), TES3MP::CommandSequence::fromValue(8).value(),
            TES3MP::CommandId::fromValue(12).value(), TES3MP::CanonicalRevision::fromValue(33).value());
        return !HasTransform<TES3MP::ClientCommandHeader> && header.sessionId().value() == 4
            && header.sessionGeneration().value() == 2 && header.commandSequence().value() == 8
            && header.commandId().value() == 12 && header.observedCanonicalRevision().value() == 33;
    }

    bool writer_admission_stamp_is_separate_from_client_observation()
    {
        const TES3MP::WriterAdmissionStamp stamp(
            TES3MP::ServerTick::fromValue(34).value(), TES3MP::IngressOrdinal::fromValue(19).value());
        return !std::is_constructible_v<TES3MP::WriterAdmissionStamp, TES3MP::ClientCommandHeader>
            && stamp.eligibleServerTick().value() == 34 && stamp.ingressOrdinal().value() == 19;
    }

    bool entity_precondition_is_explicit_and_optional_at_the_command_boundary()
    {
        const std::optional<TES3MP::EntityPrecondition> absent;
        const std::optional<TES3MP::EntityPrecondition> present(TES3MP::EntityPrecondition(
            TES3MP::EntityId::fromValue(7).value(), TES3MP::EntityRevision::fromValue(5).value(),
            TES3MP::AuthorityEpoch::fromValue(2).value()));
        return !absent && present && present->entityId().value() == 7 && present->expectedRevision().value() == 5
            && present->expectedAuthorityEpoch().value() == 2;
    }

    bool spatial_entity_snapshot_is_value_only_and_fully_initialized()
    {
        const auto snapshot = makeSnapshot(
            TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(17).value()));
        return !std::is_default_constructible_v<TES3MP::SpatialEntitySnapshot> && snapshot.serverTick().value() == 47
            && snapshot.playerId().value() == 5 && snapshot.entityId().value() == 11 && snapshot.entityRevision().value() == 9
            && snapshot.authorityEpoch().value() == 3 && snapshot.linearVelocity().x() == -1024;
    }

    bool primitives_compile_without_openmw_osg_bullet_flatbuffers_transport_or_vr_headers()
    {
        const auto snapshot = makeSnapshot(
            TES3MP::CellId::exterior(TES3MP::CellSpaceId::fromValue(1).value(), -1, 0));
        auto bytes = TES3MP::TestSupport::encodeSpatialEntitySnapshot(snapshot);
        const auto valid = TES3MP::TestSupport::decodeSpatialEntitySnapshot(bytes);

        bytes.pop_back();
        const auto truncated = TES3MP::TestSupport::decodeSpatialEntitySnapshot(bytes);
        auto invalidTag = TES3MP::TestSupport::encodeSpatialEntitySnapshot(snapshot);
        invalidTag[40] = std::byte{ 2 };
        const auto rejectedTag = TES3MP::TestSupport::decodeSpatialEntitySnapshot(invalidTag);
        return valid == snapshot && !truncated && !rejectedTag;
    }
}

int main()
{
    return interior_and_exterior_cell_ids_are_distinct_and_totally_ordered()
            && cell_id_has_no_openmw_string_path_or_transport_dependency()
            && same_cell_space_value_in_different_content_contexts_is_not_interchangeable()
            && position_uses_signed_i64_one_over_1024_unit_components()
            && exterior_boundary_crossing_does_not_renormalize_world_position()
            && turn32_covers_exactly_one_modular_turn()
            && orientation_composition_and_axis_sign_match_canonical_vectors()
            && transform_contains_only_cell_position_and_orientation_values()
            && linear_velocity_uses_signed_position_quanta_per_server_tick()
            && spatial_values_round_trip_through_owned_test_encoding()
            && client_command_header_contains_no_canonical_snapshot_payload()
            && writer_admission_stamp_is_separate_from_client_observation()
            && entity_precondition_is_explicit_and_optional_at_the_command_boundary()
            && spatial_entity_snapshot_is_value_only_and_fully_initialized()
            && primitives_compile_without_openmw_osg_bullet_flatbuffers_transport_or_vr_headers()
        ? 0
        : 1;
}
