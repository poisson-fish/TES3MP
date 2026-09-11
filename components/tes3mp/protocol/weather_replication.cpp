#include <tes3mp/weather_replication.hpp>

#include "generated/reliable_weather_state_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <array>
#include <cstring>
#include <optional>
#include <type_traits>

namespace
{
    namespace Schema = TES3MP::Protocol::Schema::WeatherReplication;
    using Error = TES3MP::WeatherReplicationDecodeError;
    using Code = TES3MP::WeatherReplicationDecodeErrorCode;

    constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t MinimumBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;

    constexpr Error error(Code code, std::size_t observed = 0, std::size_t limit = 0,
        std::size_t index = 0) noexcept
    {
        return { code, observed, limit, index };
    }

    std::optional<Error> validatePrefix(std::span<const std::byte> payload) noexcept
    {
        if (payload.size() < MinimumBytes)
            return error(Code::PayloadTooSmall, payload.size(), MinimumBytes);
        if (payload.size() > TES3MP::ReliableOperationMaximumPayloadBytes)
            return error(Code::PayloadTooLarge, payload.size(), TES3MP::ReliableOperationMaximumPayloadBytes);
        const auto declared
            = flatbuffers::GetSizePrefixedBufferLength(reinterpret_cast<const std::uint8_t*>(payload.data()));
        if (declared != payload.size())
            return error(Code::PayloadLengthMismatch, payload.size(), declared);
        return std::nullopt;
    }

    flatbuffers::Verifier verifier(std::span<const std::byte> payload)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = 8;
        options.max_tables = 8;
        options.max_size = TES3MP::ReliableOperationMaximumPayloadBytes + 1;
        options.check_alignment = true;
        options.check_nested_flatbuffers = false;
        return flatbuffers::Verifier(
            reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    std::vector<std::byte> take(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return { begin, begin + builder.GetSize() };
    }

    template <class Value>
    std::variant<Value, Error> strong(std::uint64_t raw, std::size_t index = 0)
    {
        auto value = Value::fromValue(raw);
        return value ? std::variant<Value, Error>(*value)
                     : std::variant<Value, Error>(error(Code::InvalidStrongValue, raw, 0, index));
    }

    template <class Value>
    const Value* value(const std::variant<Value, Error>& decoded)
    {
        return std::get_if<Value>(&decoded);
    }

    template <class Struct>
    Struct copyStruct(const flatbuffers::Vector<const Struct*>* values, std::size_t index) noexcept
    {
        static_assert(std::is_trivially_copyable_v<Struct>);
        Struct result{};
        std::memcpy(&result, values->Data() + index * sizeof(Struct), sizeof(Struct));
        return result;
    }
}

namespace TES3MP
{
    std::variant<ReliableWeatherState, WeatherReplicationDecodeError> ReliableWeatherState::create(
        WeatherStateHeader header, std::span<const WeatherRegionSnapshot> regions)
    {
        if (header.chunkCount == 0 || header.chunkCount > MaximumWeatherReplicationChunks
            || header.chunkIndex >= header.chunkCount)
            return error(Code::InvalidChunkSequence, header.chunkIndex, header.chunkCount);
        if (regions.empty())
            return error(Code::EmptyEntries);
        if (regions.size() > MaximumWeatherReplicationEntriesPerChunk)
            return error(Code::TooManyEntries, regions.size(), MaximumWeatherReplicationEntriesPerChunk);
        for (std::size_t index = 0; index < regions.size(); ++index)
        {
            const auto& region = regions[index];
            if (region.transitionStartTick > region.transitionEndTick
                || region.nextSelectionTick < region.transitionEndTick
                || (region.currentWeather == region.targetWeather
                    && region.transitionStartTick != region.transitionEndTick))
                return error(Code::InvalidTransitionTicks, region.transitionEndTick.value(),
                    region.transitionStartTick.value(), index);
            if (index != 0 && regions[index - 1].region >= region.region)
                return error(Code::EntriesNotStrictlySorted, region.region.value(),
                    regions[index - 1].region.value(), index);
        }
        return ReliableWeatherState(header, std::vector<WeatherRegionSnapshot>(regions.begin(), regions.end()));
    }

    std::vector<std::byte> encodeReliableWeatherState(const ReliableWeatherState& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto& header = input.header();
        const auto encodedHeader = Schema::CreateWeatherStateHeader(builder, header.targetSessionId.value(),
            header.targetSessionGeneration.value(), header.serverTick.value(), header.canonicalRevision.value(),
            header.chunkIndex, header.chunkCount, header.completeBaseline);
        std::vector<Schema::WeatherRegionState> regions;
        regions.reserve(input.regions().size());
        for (const auto& region : input.regions())
            regions.emplace_back(region.region.value(), region.currentWeather.value(), region.targetWeather.value(),
                region.transitionStartTick.value(), region.transitionEndTick.value(), region.nextSelectionTick.value(),
                region.revision.value());
        const auto encodedRegions = builder.CreateVectorOfStructs(regions);
        const auto root = Schema::CreateReliableWeatherState(builder, encodedHeader, encodedRegions);
        Schema::FinishSizePrefixedReliableWeatherStateBuffer(builder, root);
        return take(builder);
    }

    ReliableWeatherStateDecodeResult decodeReliableWeatherState(std::span<const std::byte> payload)
    {
        if (const auto failure = validatePrefix(payload))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Schema::SizePrefixedReliableWeatherStateBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        auto checked = verifier(payload);
        if (!Schema::VerifySizePrefixedReliableWeatherStateBuffer(checked))
            return error(Code::VerificationFailed);
        const auto* root = Schema::GetSizePrefixedReliableWeatherState(bytes);
        if (!root->header())
            return error(Code::MissingHeader);
        const auto* rawHeader = root->header();
        auto session = strong<SessionId>(rawHeader->target_session_id());
        auto generation = strong<SessionGeneration>(rawHeader->target_session_generation());
        auto tick = strong<ServerTick>(rawHeader->server_tick());
        auto canonicalRevision = strong<CanonicalRevision>(rawHeader->canonical_revision());
        if (const auto* failure = std::get_if<Error>(&session)) return *failure;
        if (const auto* failure = std::get_if<Error>(&generation)) return *failure;
        if (const auto* failure = std::get_if<Error>(&tick)) return *failure;
        if (const auto* failure = std::get_if<Error>(&canonicalRevision)) return *failure;
        const auto* encoded = root->regions();
        const std::size_t count = encoded ? encoded->size() : 0;
        if (count > MaximumWeatherReplicationEntriesPerChunk)
            return error(Code::TooManyEntries, count, MaximumWeatherReplicationEntriesPerChunk);
        std::vector<WeatherRegionSnapshot> regions;
        regions.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto raw = copyStruct(encoded, index);
            auto region = strong<WeatherRegionId>(raw.region_id(), index);
            auto current = strong<WeatherId>(raw.current_weather_id(), index);
            auto target = strong<WeatherId>(raw.target_weather_id(), index);
            auto transitionStart = strong<ServerTick>(raw.transition_start_tick(), index);
            auto transitionEnd = strong<ServerTick>(raw.transition_end_tick(), index);
            auto nextSelection = strong<ServerTick>(raw.next_selection_tick(), index);
            auto revision = strong<WeatherRevision>(raw.weather_revision(), index);
            const std::array failures{ std::get_if<Error>(&region), std::get_if<Error>(&current),
                std::get_if<Error>(&target), std::get_if<Error>(&transitionStart),
                std::get_if<Error>(&transitionEnd), std::get_if<Error>(&nextSelection),
                std::get_if<Error>(&revision) };
            for (const auto* failure : failures)
                if (failure) return *failure;
            regions.push_back({ *value(region), *value(current), *value(target), *value(transitionStart),
                *value(transitionEnd), *value(nextSelection), *value(revision) });
        }
        WeatherStateHeader header{ *value(session), *value(generation), *value(tick), *value(canonicalRevision),
            rawHeader->chunk_index(), rawHeader->chunk_count(), rawHeader->complete_baseline() };
        return ReliableWeatherState::create(header, regions);
    }
}
