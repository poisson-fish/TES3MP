#include <tes3mp/world_time_replication.hpp>

#include "generated/reliable_world_time_state_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <array>
#include <optional>

namespace
{
    namespace Schema = TES3MP::Protocol::Schema::WorldTimeReplication;
    using Error = TES3MP::WorldTimeReplicationDecodeError;
    using Code = TES3MP::WorldTimeReplicationDecodeErrorCode;
    constexpr std::size_t Prefix = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t Minimum = Prefix + sizeof(flatbuffers::uoffset_t) + 4;

    constexpr Error error(Code code, std::size_t observed = 0, std::size_t limit = 0) noexcept
    {
        return { code, observed, limit };
    }

    template <class Value>
    std::variant<Value, Error> strong(std::uint64_t raw)
    {
        auto value = Value::fromValue(raw);
        return value ? std::variant<Value, Error>(*value)
                     : std::variant<Value, Error>(error(Code::InvalidStrongValue, raw));
    }

    template <class Value>
    const Value* value(const std::variant<Value, Error>& decoded)
    {
        return std::get_if<Value>(&decoded);
    }
}

namespace TES3MP
{
    std::vector<std::byte> encodeReliableWorldTimeState(const ReliableWorldTimeState& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto& time = input.time;
        const auto root = Schema::CreateReliableWorldTimeState(builder, input.targetSessionId.value(),
            input.targetSessionGeneration.value(), input.serverTick.value(), input.canonicalRevision.value(),
            time.day, time.month, time.year, time.millisecondsSinceMidnight, time.timeScaleUnits,
            time.subMillisecondRemainder, time.revision.value(), time.lastChangeTick.value(),
            time.lastAdvanceTick.value(), input.completeBaseline);
        Schema::FinishSizePrefixedReliableWorldTimeStateBuffer(builder, root);
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return { begin, begin + builder.GetSize() };
    }

    ReliableWorldTimeStateDecodeResult decodeReliableWorldTimeState(std::span<const std::byte> payload)
    {
        if (payload.size() < Minimum)
            return error(Code::PayloadTooSmall, payload.size(), Minimum);
        if (payload.size() > ReliableOperationMaximumPayloadBytes)
            return error(Code::PayloadTooLarge, payload.size(), ReliableOperationMaximumPayloadBytes);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const auto declared = flatbuffers::GetSizePrefixedBufferLength(bytes);
        if (declared != payload.size())
            return error(Code::PayloadLengthMismatch, payload.size(), declared);
        if (!Schema::SizePrefixedReliableWorldTimeStateBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        flatbuffers::Verifier::Options options;
        options.max_depth = 4;
        options.max_tables = 4;
        options.max_size = ReliableOperationMaximumPayloadBytes + 1;
        flatbuffers::Verifier verifier(bytes, payload.size(), options);
        if (!Schema::VerifySizePrefixedReliableWorldTimeStateBuffer(verifier))
            return error(Code::VerificationFailed);
        const auto* root = Schema::GetSizePrefixedReliableWorldTimeState(bytes);
        auto session = strong<SessionId>(root->target_session_id());
        auto generation = strong<SessionGeneration>(root->target_session_generation());
        auto tick = strong<ServerTick>(root->server_tick());
        auto canonicalRevision = strong<CanonicalRevision>(root->canonical_revision());
        auto revision = strong<WorldTimeRevision>(root->world_time_revision());
        auto lastChange = strong<ServerTick>(root->last_change_tick());
        auto lastAdvance = strong<ServerTick>(root->last_advance_tick());
        const std::array failures{ std::get_if<Error>(&session), std::get_if<Error>(&generation),
            std::get_if<Error>(&tick), std::get_if<Error>(&canonicalRevision), std::get_if<Error>(&revision),
            std::get_if<Error>(&lastChange), std::get_if<Error>(&lastAdvance) };
        for (const auto* failure : failures)
            if (failure)
                return *failure;
        CanonicalWorldTimeState time{ root->day(), root->month(), root->year(), root->milliseconds_since_midnight(),
            root->time_scale_units(), root->sub_millisecond_remainder(), *value(revision), *value(lastChange),
            *value(lastAdvance) };
        if (time.day < 1 || time.day > 30 || time.month >= 12
            || time.millisecondsSinceMidnight >= WorldMillisecondsPerDay
            || time.timeScaleUnits > MaximumWorldTimeScaleUnits
            || time.subMillisecondRemainder >= WorldTimeScaleUnitsPerOne
            || time.lastChangeTick > time.lastAdvanceTick)
            return error(Code::InvalidWorldTime);
        return ReliableWorldTimeState{ *value(session), *value(generation), *value(tick), *value(canonicalRevision),
            time, root->complete_baseline() };
    }
}
