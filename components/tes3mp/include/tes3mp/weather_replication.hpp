#ifndef TES3MP_WEATHER_REPLICATION_HPP
#define TES3MP_WEATHER_REPLICATION_HPP

#include "protocol_frame.hpp"
#include "value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumWeatherReplicationEntriesPerChunk = 224;
    inline constexpr std::uint32_t MaximumWeatherReplicationChunks = 19;

    enum class WeatherReplicationDecodeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        MissingHeader,
        InvalidStrongValue,
        InvalidChunkSequence,
        EmptyEntries,
        TooManyEntries,
        EntriesNotStrictlySorted,
        InvalidTransitionTicks,
    };

    struct WeatherReplicationDecodeError
    {
        WeatherReplicationDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::size_t index = 0;

        friend constexpr bool operator==(
            WeatherReplicationDecodeError, WeatherReplicationDecodeError) noexcept = default;
    };

    struct WeatherRegionSnapshot
    {
        WeatherRegionId region;
        WeatherId currentWeather;
        WeatherId targetWeather;
        ServerTick transitionStartTick;
        ServerTick transitionEndTick;
        ServerTick nextSelectionTick;
        WeatherRevision revision;

        friend constexpr bool operator==(WeatherRegionSnapshot, WeatherRegionSnapshot) noexcept = default;
        friend constexpr auto operator<=>(WeatherRegionSnapshot, WeatherRegionSnapshot) noexcept = default;
    };

    struct WeatherStateHeader
    {
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        ServerTick serverTick;
        CanonicalRevision canonicalRevision;
        std::uint32_t chunkIndex = 0;
        std::uint32_t chunkCount = 1;
        bool completeBaseline = false;

        friend constexpr bool operator==(WeatherStateHeader, WeatherStateHeader) noexcept = default;
    };

    class ReliableWeatherState
    {
    public:
        static std::variant<ReliableWeatherState, WeatherReplicationDecodeError> create(
            WeatherStateHeader header, std::span<const WeatherRegionSnapshot> regions);

        constexpr const WeatherStateHeader& header() const noexcept { return mHeader; }
        std::span<const WeatherRegionSnapshot> regions() const noexcept { return mRegions; }

        friend bool operator==(const ReliableWeatherState&, const ReliableWeatherState&) noexcept = default;

    private:
        ReliableWeatherState(WeatherStateHeader header, std::vector<WeatherRegionSnapshot> regions)
            : mHeader(header), mRegions(std::move(regions)) {}

        WeatherStateHeader mHeader;
        std::vector<WeatherRegionSnapshot> mRegions;
    };

    using ReliableWeatherStateDecodeResult = std::variant<ReliableWeatherState, WeatherReplicationDecodeError>;

    std::vector<std::byte> encodeReliableWeatherState(const ReliableWeatherState& value);
    ReliableWeatherStateDecodeResult decodeReliableWeatherState(std::span<const std::byte> payload);
}

#endif
