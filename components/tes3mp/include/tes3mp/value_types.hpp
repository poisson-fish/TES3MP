#ifndef TES3MP_VALUE_TYPES_HPP
#define TES3MP_VALUE_TYPES_HPP

#include "strong_value.hpp"

namespace TES3MP::Detail
{
    struct EntityIdTag
    {
        static constexpr std::string_view name = "EntityId";
    };

    struct PlayerIdTag
    {
        static constexpr std::string_view name = "PlayerId";
    };

    struct PrincipalIdTag
    {
        static constexpr std::string_view name = "PrincipalId";
    };

    struct SessionIdTag
    {
        static constexpr std::string_view name = "SessionId";
    };

    struct SessionGenerationTag
    {
        static constexpr std::string_view name = "SessionGeneration";
    };

    struct AuthenticationAttemptIdTag
    {
        static constexpr std::string_view name = "AuthenticationAttemptId";
    };

    struct ServerTickTag
    {
        static constexpr std::string_view name = "ServerTick";
    };

    struct CanonicalRevisionTag
    {
        static constexpr std::string_view name = "CanonicalRevision";
    };

    struct CanonicalStateVersionTag
    {
        static constexpr std::string_view name = "CanonicalStateVersion";
    };

    struct CommandSequenceTag
    {
        static constexpr std::string_view name = "CommandSequence";
    };

    struct EntityRevisionTag
    {
        static constexpr std::string_view name = "EntityRevision";
    };

    struct CommandIdTag
    {
        static constexpr std::string_view name = "CommandId";
    };

    struct AuthorityEpochTag
    {
        static constexpr std::string_view name = "AuthorityEpoch";
    };

    struct PoseSampleSequenceTag
    {
        static constexpr std::string_view name = "PoseSampleSequence";
    };

    struct IngressOrdinalTag
    {
        static constexpr std::string_view name = "IngressOrdinal";
    };

    struct CellSpaceIdTag
    {
        static constexpr std::string_view name = "CellSpaceId";
    };

    struct AppearanceIdTag
    {
        static constexpr std::string_view name = "AppearanceId";
    };

    struct ActorIdTag
    {
        static constexpr std::string_view name = "ActorId";
    };

    struct ActorPrototypeIdTag
    {
        static constexpr std::string_view name = "ActorPrototypeId";
    };

    struct InteractiveObjectIdTag
    {
        static constexpr std::string_view name = "InteractiveObjectId";
    };

    struct KeyPrototypeIdTag
    {
        static constexpr std::string_view name = "KeyPrototypeId";
    };

    struct TrapPrototypeIdTag
    {
        static constexpr std::string_view name = "TrapPrototypeId";
    };

    struct ObjectRevisionTag
    {
        static constexpr std::string_view name = "ObjectRevision";
    };

    struct ItemPrototypeIdTag
    {
        static constexpr std::string_view name = "ItemPrototypeId";
    };

    struct ItemStackIdTag
    {
        static constexpr std::string_view name = "ItemStackId";
    };

    struct InventoryRevisionTag
    {
        static constexpr std::string_view name = "InventoryRevision";
    };

    struct ContainerRevisionTag
    {
        static constexpr std::string_view name = "ContainerRevision";
    };

    struct WorldItemRevisionTag
    {
        static constexpr std::string_view name = "WorldItemRevision";
    };

    struct ContainerIdTag
    {
        static constexpr std::string_view name = "ContainerId";
    };

    struct CombatRevisionTag
    {
        static constexpr std::string_view name = "CombatRevision";
    };
}

namespace TES3MP
{
    using EntityId = Detail::StrongValue<Detail::EntityIdTag, Detail::StrongValuePolicy::Identity>;
    using PlayerId = Detail::StrongValue<Detail::PlayerIdTag, Detail::StrongValuePolicy::Identity>;
    using PrincipalId = Detail::StrongValue<Detail::PrincipalIdTag, Detail::StrongValuePolicy::Identity>;
    using SessionId = Detail::StrongValue<Detail::SessionIdTag, Detail::StrongValuePolicy::Identity>;
    using SessionGeneration
        = Detail::StrongValue<Detail::SessionGenerationTag, Detail::StrongValuePolicy::CounterFromOne>;
    using AuthenticationAttemptId
        = Detail::StrongValue<Detail::AuthenticationAttemptIdTag, Detail::StrongValuePolicy::CounterFromOne>;
    using ServerTick = Detail::StrongValue<Detail::ServerTickTag, Detail::StrongValuePolicy::CounterFromZero>;
    using CanonicalRevision
        = Detail::StrongValue<Detail::CanonicalRevisionTag, Detail::StrongValuePolicy::CounterFromZero>;
    using CanonicalStateVersion
        = Detail::StrongValue<Detail::CanonicalStateVersionTag, Detail::StrongValuePolicy::CounterFromZero>;
    using CommandSequence = Detail::StrongValue<Detail::CommandSequenceTag, Detail::StrongValuePolicy::CounterFromOne>;
    using EntityRevision = Detail::StrongValue<Detail::EntityRevisionTag, Detail::StrongValuePolicy::CounterFromOne>;
    using CommandId = Detail::StrongValue<Detail::CommandIdTag, Detail::StrongValuePolicy::Identity>;
    using AuthorityEpoch = Detail::StrongValue<Detail::AuthorityEpochTag, Detail::StrongValuePolicy::CounterFromOne>;
    using PoseSampleSequence
        = Detail::StrongValue<Detail::PoseSampleSequenceTag, Detail::StrongValuePolicy::CounterFromOne>;
    using IngressOrdinal = Detail::StrongValue<Detail::IngressOrdinalTag, Detail::StrongValuePolicy::CounterFromOne>;
    using CellSpaceId = Detail::StrongValue<Detail::CellSpaceIdTag, Detail::StrongValuePolicy::Identity>;
    using AppearanceId = Detail::StrongValue<Detail::AppearanceIdTag, Detail::StrongValuePolicy::Identity>;
    using ActorId = Detail::StrongValue<Detail::ActorIdTag, Detail::StrongValuePolicy::Identity>;
    using ActorPrototypeId = Detail::StrongValue<Detail::ActorPrototypeIdTag, Detail::StrongValuePolicy::Identity>;
    using InteractiveObjectId
        = Detail::StrongValue<Detail::InteractiveObjectIdTag, Detail::StrongValuePolicy::Identity>;
    using KeyPrototypeId = Detail::StrongValue<Detail::KeyPrototypeIdTag, Detail::StrongValuePolicy::Identity>;
    using TrapPrototypeId = Detail::StrongValue<Detail::TrapPrototypeIdTag, Detail::StrongValuePolicy::Identity>;
    using ObjectRevision = Detail::StrongValue<Detail::ObjectRevisionTag, Detail::StrongValuePolicy::CounterFromOne>;
    using ItemPrototypeId = Detail::StrongValue<Detail::ItemPrototypeIdTag, Detail::StrongValuePolicy::Identity>;
    using ItemStackId = Detail::StrongValue<Detail::ItemStackIdTag, Detail::StrongValuePolicy::CounterFromOne>;
    using InventoryRevision
        = Detail::StrongValue<Detail::InventoryRevisionTag, Detail::StrongValuePolicy::CounterFromOne>;
    using ContainerRevision
        = Detail::StrongValue<Detail::ContainerRevisionTag, Detail::StrongValuePolicy::CounterFromOne>;
    using WorldItemRevision
        = Detail::StrongValue<Detail::WorldItemRevisionTag, Detail::StrongValuePolicy::CounterFromOne>;
    using ContainerId = Detail::StrongValue<Detail::ContainerIdTag, Detail::StrongValuePolicy::Identity>;
    using CombatRevision = Detail::StrongValue<Detail::CombatRevisionTag, Detail::StrongValuePolicy::CounterFromOne>;
}

#endif
