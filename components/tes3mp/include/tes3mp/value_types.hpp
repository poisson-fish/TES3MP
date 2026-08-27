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

    struct SessionIdTag
    {
        static constexpr std::string_view name = "SessionId";
    };

    struct SessionGenerationTag
    {
        static constexpr std::string_view name = "SessionGeneration";
    };

    struct ServerTickTag
    {
        static constexpr std::string_view name = "ServerTick";
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

    struct IngressOrdinalTag
    {
        static constexpr std::string_view name = "IngressOrdinal";
    };

    struct CellSpaceIdTag
    {
        static constexpr std::string_view name = "CellSpaceId";
    };
}

namespace TES3MP
{
    using EntityId = Detail::StrongValue<Detail::EntityIdTag, Detail::StrongValuePolicy::Identity>;
    using PlayerId = Detail::StrongValue<Detail::PlayerIdTag, Detail::StrongValuePolicy::Identity>;
    using SessionId = Detail::StrongValue<Detail::SessionIdTag, Detail::StrongValuePolicy::Identity>;
    using SessionGeneration
        = Detail::StrongValue<Detail::SessionGenerationTag, Detail::StrongValuePolicy::CounterFromOne>;
    using ServerTick = Detail::StrongValue<Detail::ServerTickTag, Detail::StrongValuePolicy::CounterFromZero>;
    using CommandSequence
        = Detail::StrongValue<Detail::CommandSequenceTag, Detail::StrongValuePolicy::CounterFromOne>;
    using EntityRevision
        = Detail::StrongValue<Detail::EntityRevisionTag, Detail::StrongValuePolicy::CounterFromOne>;
    using CommandId = Detail::StrongValue<Detail::CommandIdTag, Detail::StrongValuePolicy::Identity>;
    using AuthorityEpoch
        = Detail::StrongValue<Detail::AuthorityEpochTag, Detail::StrongValuePolicy::CounterFromOne>;
    using IngressOrdinal
        = Detail::StrongValue<Detail::IngressOrdinalTag, Detail::StrongValuePolicy::CounterFromOne>;
    using CellSpaceId = Detail::StrongValue<Detail::CellSpaceIdTag, Detail::StrongValuePolicy::Identity>;
}

#endif
