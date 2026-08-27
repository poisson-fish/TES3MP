#include <tes3mp/value_types.hpp>

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>

namespace
{
    template <class Left, class Right>
    concept EqualityComparable = requires(Left left, Right right) { left == right; };

    template <class Type>
    concept HasInitial = requires { Type::initial(); };

    template <class Type>
    concept HasNext = requires(Type value) { value.next(); };

    template <class Type>
    concept HasPreIncrement = requires(Type value) { ++value; };

    template <class Type>
    concept HasAddition = requires(Type value) { value + 1; };

    static_assert(!std::is_default_constructible_v<TES3MP::EntityId>);
    static_assert(!std::is_default_constructible_v<TES3MP::ServerTick>);
    static_assert(sizeof(TES3MP::EntityId) == sizeof(std::uint64_t));
    static_assert(alignof(TES3MP::EntityId) == alignof(std::uint64_t));
    static_assert(std::is_trivially_copyable_v<TES3MP::EntityId>);
    static_assert(!std::is_convertible_v<std::uint64_t, TES3MP::EntityId>);
    static_assert(!std::is_convertible_v<TES3MP::EntityId, std::uint64_t>);
    static_assert(!std::is_constructible_v<TES3MP::EntityId, TES3MP::PlayerId>);
    static_assert(!std::is_constructible_v<TES3MP::PrincipalId, TES3MP::PlayerId>);
    static_assert(!std::is_constructible_v<TES3MP::AuthenticationAttemptId, TES3MP::SessionGeneration>);
    static_assert(!EqualityComparable<TES3MP::EntityId, TES3MP::PlayerId>);
    static_assert(!HasInitial<TES3MP::EntityId>);
    static_assert(!HasNext<TES3MP::EntityId>);
    static_assert(!HasNext<TES3MP::CommandId>);
    static_assert(HasInitial<TES3MP::ServerTick>);
    static_assert(HasNext<TES3MP::ServerTick>);
    static_assert(!HasPreIncrement<TES3MP::ServerTick>);
    static_assert(!HasAddition<TES3MP::ServerTick>);

    bool check(bool condition)
    {
        return condition;
    }

    bool semantic_types_are_not_convertible_or_comparable_to_each_other()
    {
        return !std::is_constructible_v<TES3MP::EntityId, TES3MP::PlayerId>
            && !EqualityComparable<TES3MP::EntityId, TES3MP::PlayerId>;
    }

    bool semantic_types_are_not_implicitly_convertible_to_or_from_u64()
    {
        return !std::is_convertible_v<std::uint64_t, TES3MP::EntityId>
            && !std::is_convertible_v<TES3MP::EntityId, std::uint64_t>;
    }

    bool nonzero_types_reject_zero_and_have_no_default_constructor()
    {
        return !TES3MP::EntityId::fromValue(0).has_value() && !TES3MP::CommandId::fromValue(0).has_value()
            && !TES3MP::EntityRevision::fromValue(0).has_value() && !std::is_default_constructible_v<TES3MP::EntityId>;
    }

    bool server_tick_accepts_zero_as_its_initial_value()
    {
        const auto decoded = TES3MP::ServerTick::fromValue(0);
        return decoded.has_value() && decoded->value() == 0 && TES3MP::ServerTick::initial().value() == 0;
    }

    bool one_based_counters_expose_value_one_as_initial()
    {
        return TES3MP::SessionGeneration::initial().value() == 1 && TES3MP::CommandSequence::initial().value() == 1
            && TES3MP::AuthenticationAttemptId::initial().value() == 1 && TES3MP::EntityRevision::initial().value() == 1
            && TES3MP::AuthorityEpoch::initial().value() == 1 && TES3MP::IngressOrdinal::initial().value() == 1;
    }

    bool same_type_equality_and_total_order_use_unsigned_value_order()
    {
        const auto lower = TES3MP::EntityId::fromValue(7).value();
        const auto equal = TES3MP::EntityId::fromValue(7).value();
        const auto higher = TES3MP::EntityId::fromValue(8).value();
        return lower == equal && lower < higher && higher > equal;
    }

    bool hash_matches_equal_values_without_defining_canonical_order()
    {
        const auto left = TES3MP::PlayerId::fromValue(42).value();
        const auto right = TES3MP::PlayerId::fromValue(42).value();
        return std::hash<TES3MP::PlayerId>{}(left) == std::hash<TES3MP::PlayerId>{}(right);
    }

    bool debug_text_is_stable_type_qualified_ascii_decimal()
    {
        const auto entity = TES3MP::EntityId::fromValue(42).value();
        const auto maximumTick = TES3MP::ServerTick::fromValue(std::numeric_limits<std::uint64_t>::max()).value();
        std::ostringstream stream;
        stream << entity;
        return entity.toString() == "EntityId{42}" && stream.str() == "EntityId{42}"
            && maximumTick.toString() == "ServerTick{18446744073709551615}";
    }

    bool counter_next_advances_without_mutating_source()
    {
        const auto source = TES3MP::EntityRevision::fromValue(7).value();
        const auto advanced = source.next();
        return source.value() == 7 && advanced.has_value() && advanced->value() == 8;
    }

    template <class Counter>
    bool rejectsMaximumWithoutWrap()
    {
        const auto maximum = Counter::fromValue(std::numeric_limits<std::uint64_t>::max()).value();
        return !maximum.next().has_value() && maximum.value() == std::numeric_limits<std::uint64_t>::max();
    }

    bool counter_next_at_u64_max_returns_empty_without_wrap()
    {
        return rejectsMaximumWithoutWrap<TES3MP::SessionGeneration>()
            && rejectsMaximumWithoutWrap<TES3MP::AuthenticationAttemptId>()
            && rejectsMaximumWithoutWrap<TES3MP::ServerTick>() && rejectsMaximumWithoutWrap<TES3MP::CommandSequence>()
            && rejectsMaximumWithoutWrap<TES3MP::EntityRevision>()
            && rejectsMaximumWithoutWrap<TES3MP::AuthorityEpoch>()
            && rejectsMaximumWithoutWrap<TES3MP::IngressOrdinal>();
    }

    bool identity_types_expose_no_increment_or_arithmetic_operators()
    {
        return !HasNext<TES3MP::EntityId> && !HasPreIncrement<TES3MP::EntityId> && !HasAddition<TES3MP::EntityId>;
    }

    bool types_compile_without_openmw_flatbuffers_transport_fmt_or_platform_headers()
    {
        return TES3MP::SessionId::fromValue(1).has_value() && TES3MP::ServerTick::initial().value() == 0;
    }

    bool test_support_can_construct_boundary_and_exhaustion_values_explicitly()
    {
        const auto first = TES3MP::IngressOrdinal::initial();
        const auto maximum = TES3MP::IngressOrdinal::fromValue(std::numeric_limits<std::uint64_t>::max()).value();
        return first.value() == 1 && !maximum.next().has_value();
    }
}

int main()
{
    return check(semantic_types_are_not_convertible_or_comparable_to_each_other())
            && check(semantic_types_are_not_implicitly_convertible_to_or_from_u64())
            && check(nonzero_types_reject_zero_and_have_no_default_constructor())
            && check(server_tick_accepts_zero_as_its_initial_value())
            && check(one_based_counters_expose_value_one_as_initial())
            && check(same_type_equality_and_total_order_use_unsigned_value_order())
            && check(hash_matches_equal_values_without_defining_canonical_order())
            && check(debug_text_is_stable_type_qualified_ascii_decimal())
            && check(counter_next_advances_without_mutating_source())
            && check(counter_next_at_u64_max_returns_empty_without_wrap())
            && check(identity_types_expose_no_increment_or_arithmetic_operators())
            && check(types_compile_without_openmw_flatbuffers_transport_fmt_or_platform_headers())
            && check(test_support_can_construct_boundary_and_exhaustion_values_explicitly())
        ? 0
        : 1;
}
