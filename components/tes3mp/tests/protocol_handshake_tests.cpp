#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/protocol_handshake.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    using TES3MP::CapabilityId;
    using TES3MP::CapabilityOffer;
    using TES3MP::ClientHello;
    using TES3MP::HandshakeError;
    using TES3MP::HandshakeErrorCode;
    using TES3MP::ProtocolVersionRange;
    using TES3MP::ServerHello;
    using TES3MP::SessionRejected;

    static_assert(sizeof(CapabilityId) == sizeof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<CapabilityId>);
    static_assert(std::is_trivially_copyable_v<HandshakeError>);
    static_assert(!std::is_default_constructible_v<ClientHello>);
    static_assert(!std::is_default_constructible_v<ServerHello>);
    static_assert(!std::is_default_constructible_v<SessionRejected>);

    CapabilityId capability(std::uint32_t value)
    {
        const auto result = CapabilityId::fromValue(value);
        return result ? *result : *CapabilityId::fromValue(1);
    }

    ProtocolVersionRange versionRange(std::uint16_t major, std::uint16_t minimum, std::uint16_t maximum)
    {
        auto result = ProtocolVersionRange::create(major, minimum, maximum);
        return std::get<ProtocolVersionRange>(result);
    }

    CapabilityOffer offer(ProtocolVersionRange versions, std::initializer_list<std::uint32_t> optional,
        std::initializer_list<std::uint32_t> required)
    {
        std::vector<CapabilityId> optionalValues;
        std::vector<CapabilityId> requiredValues;
        for (const auto value : optional)
            optionalValues.push_back(capability(value));
        for (const auto value : required)
            requiredValues.push_back(capability(value));
        auto result = CapabilityOffer::create(versions, optionalValues, requiredValues);
        return std::get<CapabilityOffer>(std::move(result));
    }

    bool hasError(const auto& result, HandshakeErrorCode code)
    {
        const auto* value = std::get_if<HandshakeError>(&result);
        return value != nullptr && value->code == code;
    }

    bool capabilitiesEqual(std::span<const CapabilityId> actual, std::initializer_list<std::uint32_t> expected)
    {
        if (actual.size() != expected.size())
            return false;
        return std::equal(actual.begin(), actual.end(), expected.begin(),
            [](CapabilityId left, std::uint32_t right) { return left.value() == right; });
    }

    bool bytesEqual(std::span<const std::byte> actual, std::initializer_list<std::uint8_t> expected)
    {
        return actual.size() == expected.size()
            && std::equal(actual.begin(), actual.end(), expected.begin(),
                [](std::byte left, std::uint8_t right) { return std::to_integer<std::uint8_t>(left) == right; });
    }

    std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values)
    {
        std::vector<std::byte> result;
        result.reserve(values.size());
        for (const auto value : values)
            result.push_back(static_cast<std::byte>(value));
        return result;
    }

    bool replaceFirst(
        std::vector<std::byte>& value, std::span<const std::byte> before, std::span<const std::byte> after)
    {
        if (before.size() != after.size())
            return false;
        const auto position = std::search(value.begin(), value.end(), before.begin(), before.end());
        if (position == value.end())
            return false;
        std::copy(after.begin(), after.end(), position);
        return true;
    }

    struct Fixture
    {
        ClientHello client = ClientHello::fromOffer(offer(versionRange(1, 0, 1), { 1, 3 }, { 5 }));
        CapabilityOffer server = offer(versionRange(1, 0, 1), { 1, 4, 5 }, { 3 });
        ServerHello accepted = std::get<ServerHello>(TES3MP::negotiateClientHello(client, server));
        SessionRejected rejected = std::get<SessionRejected>(
            TES3MP::negotiateClientHello(ClientHello::fromOffer(offer(versionRange(2, 0, 0), {}, {})), server));
    };

    void printBytes(std::span<const std::byte> bytes)
    {
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            if (index != 0)
                std::cout << ',';
            std::cout << static_cast<unsigned>(std::to_integer<std::uint8_t>(bytes[index]));
        }
        std::cout << '\n';
    }

    bool value_factories_reject_invalid_ranges_and_capability_sets()
    {
        const auto invalidRange = ProtocolVersionRange::create(1, 2, 1);
        const auto overbroadRange = ProtocolVersionRange::create(1, 0, 2);
        if (!hasError(invalidRange, HandshakeErrorCode::InvalidVersionRange)
            || !hasError(overbroadRange, HandshakeErrorCode::InvalidVersionRange) || CapabilityId::fromValue(0))
            return false;

        const auto versions = versionRange(1, 0, 1);
        const std::array unsorted{ capability(2), capability(1) };
        const std::array duplicate{ capability(1), capability(1) };
        const std::array one{ capability(1) };
        std::vector<CapabilityId> tooMany;
        tooMany.reserve(TES3MP::MaximumOptionalCapabilityCount + 1);
        for (std::size_t index = 0; index < TES3MP::MaximumOptionalCapabilityCount + 1; ++index)
            tooMany.push_back(capability(static_cast<std::uint32_t>(index + 1)));

        return hasError(
                   CapabilityOffer::create(versions, unsorted, {}), HandshakeErrorCode::CapabilitiesNotStrictlySorted)
            && hasError(
                CapabilityOffer::create(versions, duplicate, {}), HandshakeErrorCode::CapabilitiesNotStrictlySorted)
            && hasError(CapabilityOffer::create(versions, one, one), HandshakeErrorCode::CapabilityInBothSets)
            && hasError(CapabilityOffer::create(versions, tooMany, {}), HandshakeErrorCode::TooManyCapabilities);
    }

    bool current_and_previous_minor_select_highest_overlap()
    {
        const auto server = offer(versionRange(1, 0, 1), {}, {});
        const auto current = ClientHello::fromOffer(offer(versionRange(1, 1, 1), {}, {}));
        const auto previous = ClientHello::fromOffer(offer(versionRange(1, 0, 0), {}, {}));
        const auto currentResult = TES3MP::negotiateClientHello(current, server);
        const auto previousResult = TES3MP::negotiateClientHello(previous, server);
        const auto* currentHello = std::get_if<ServerHello>(&currentResult);
        const auto* previousHello = std::get_if<ServerHello>(&previousResult);
        return currentHello != nullptr && currentHello->selectedVersion() == TES3MP::ProtocolVersion{ 1, 1 }
        && previousHello != nullptr && previousHello->selectedVersion() == TES3MP::ProtocolVersion{ 1, 0 };
    }

    bool optional_capabilities_intersect_without_enabling_unknown_ids()
    {
        const auto client = ClientHello::fromOffer(offer(versionRange(1, 0, 1), { 1, 3, 99 }, { 5 }));
        const auto server = offer(versionRange(1, 0, 1), { 1, 4, 5 }, { 3 });
        const auto result = TES3MP::negotiateClientHello(client, server);
        const auto* accepted = std::get_if<ServerHello>(&result);
        return accepted != nullptr && capabilitiesEqual(accepted->negotiatedCapabilities(), { 1, 3, 5 });
    }

    bool version_and_required_capability_failures_are_stable()
    {
        const auto server = offer(versionRange(1, 0, 1), {}, { 5 });
        const auto major
            = TES3MP::negotiateClientHello(ClientHello::fromOffer(offer(versionRange(2, 0, 1), {}, {})), server);
        const auto minor
            = TES3MP::negotiateClientHello(ClientHello::fromOffer(offer(versionRange(1, 2, 3), {}, { 7 })), server);
        const auto capabilityFailure
            = TES3MP::negotiateClientHello(ClientHello::fromOffer(offer(versionRange(1, 0, 1), {}, { 7 })), server);
        const auto* majorRejected = std::get_if<SessionRejected>(&major);
        const auto* minorRejected = std::get_if<SessionRejected>(&minor);
        const auto* capabilityRejected = std::get_if<SessionRejected>(&capabilityFailure);
        if (capabilityRejected == nullptr)
            return false;
        const auto capabilityPayload = TES3MP::encodeSessionRejected(*capabilityRejected);
        const auto decodedCapabilityFailure = TES3MP::decodeSessionRejected(capabilityPayload);
        const auto* decodedCapabilityRejected = std::get_if<SessionRejected>(&decodedCapabilityFailure);
        return majorRejected != nullptr
            && majorRejected->reason() == TES3MP::SessionRejectionReason::ProtocolMajorMismatch
            && minorRejected != nullptr && minorRejected->reason() == TES3MP::SessionRejectionReason::NoCompatibleMinor
            && capabilityRejected->reason() == TES3MP::SessionRejectionReason::UnsupportedRequiredCapability
            && capabilityRejected->unsupportedCapability() && capabilityRejected->unsupportedCapability()->value() == 5
            && decodedCapabilityRejected != nullptr
            && decodedCapabilityRejected->reason() == TES3MP::SessionRejectionReason::UnsupportedRequiredCapability
            && decodedCapabilityRejected->unsupportedCapability()
            && decodedCapabilityRejected->unsupportedCapability()->value() == 5;
    }

    bool all_payloads_round_trip_as_owned_values()
    {
        const Fixture fixture;
        auto clientBytes = TES3MP::encodeClientHello(fixture.client);
        auto serverBytes = TES3MP::encodeServerHello(fixture.accepted);
        auto rejectedBytes = TES3MP::encodeSessionRejected(fixture.rejected);
        auto decodedClient = TES3MP::decodeClientHello(clientBytes);
        auto decodedServer = TES3MP::decodeServerHello(serverBytes);
        auto decodedRejected = TES3MP::decodeSessionRejected(rejectedBytes);
        std::fill(clientBytes.begin(), clientBytes.end(), std::byte{ 0 });
        std::fill(serverBytes.begin(), serverBytes.end(), std::byte{ 0 });
        std::fill(rejectedBytes.begin(), rejectedBytes.end(), std::byte{ 0 });

        const auto* client = std::get_if<ClientHello>(&decodedClient);
        const auto* server = std::get_if<ServerHello>(&decodedServer);
        const auto* rejected = std::get_if<SessionRejected>(&decodedRejected);
        return client != nullptr && client->versions() == versionRange(1, 0, 1)
            && capabilitiesEqual(client->optionalCapabilities(), { 1, 3 })
            && capabilitiesEqual(client->requiredCapabilities(), { 5 }) && server != nullptr
            && server->selectedVersion() == TES3MP::ProtocolVersion{ 1, 1 }
        && capabilitiesEqual(server->negotiatedCapabilities(), { 1, 3, 5 }) && rejected != nullptr
            && rejected->reason() == TES3MP::SessionRejectionReason::ProtocolMajorMismatch
            && rejected->serverVersions() == versionRange(1, 0, 1) && !rejected->unsupportedCapability();
    }

    bool every_truncation_wrong_identifier_and_trailing_byte_fail_without_partial_value()
    {
        const Fixture fixture;
        const std::array payloads{ TES3MP::encodeClientHello(fixture.client),
            TES3MP::encodeServerHello(fixture.accepted), TES3MP::encodeSessionRejected(fixture.rejected) };
        for (std::size_t payloadIndex = 0; payloadIndex < payloads.size(); ++payloadIndex)
        {
            const auto decode = [payloadIndex](std::span<const std::byte> bytes) {
                if (payloadIndex == 0)
                    return std::holds_alternative<HandshakeError>(TES3MP::decodeClientHello(bytes));
                if (payloadIndex == 1)
                    return std::holds_alternative<HandshakeError>(TES3MP::decodeServerHello(bytes));
                return std::holds_alternative<HandshakeError>(TES3MP::decodeSessionRejected(bytes));
            };
            for (std::size_t size = 0; size < payloads[payloadIndex].size(); ++size)
            {
                if (!decode(std::span(payloads[payloadIndex]).first(size)))
                    return false;
            }
            auto wrongIdentifier = payloads[payloadIndex];
            wrongIdentifier[8] ^= std::byte{ 0xff };
            if (!decode(wrongIdentifier))
                return false;
            auto trailing = payloads[payloadIndex];
            trailing.push_back(std::byte{ 0 });
            if (!decode(trailing))
                return false;
        }
        return true;
    }

    bool hostile_capability_vectors_reject_before_negotiation()
    {
        const Fixture fixture;
        const auto optionalVector = bytes({ 2, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0 });
        const auto duplicateVector = bytes({ 2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 });
        const auto zeroVector = bytes({ 2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0 });
        const auto requiredVector = bytes({ 1, 0, 0, 0, 5, 0, 0, 0 });
        const auto overlappingRequired = bytes({ 1, 0, 0, 0, 1, 0, 0, 0 });

        auto duplicate = TES3MP::encodeClientHello(fixture.client);
        auto zero = duplicate;
        auto overlap = duplicate;
        auto oversized = duplicate;
        if (!replaceFirst(duplicate, optionalVector, duplicateVector) || !replaceFirst(zero, optionalVector, zeroVector)
            || !replaceFirst(overlap, requiredVector, overlappingRequired))
            return false;

        const auto optionalPosition
            = std::search(oversized.begin(), oversized.end(), optionalVector.begin(), optionalVector.end());
        if (optionalPosition == oversized.end())
            return false;
        optionalPosition[0] = std::byte{ 33 };
        for (std::uint32_t value = 4; value <= 34; ++value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8)
                oversized.push_back(static_cast<std::byte>((value >> shift) & 0xff));
        }
        const auto declaredSize = static_cast<std::uint32_t>(oversized.size() - sizeof(std::uint32_t));
        for (unsigned shift = 0; shift < 32; shift += 8)
            oversized[shift / 8] = static_cast<std::byte>((declaredSize >> shift) & 0xff);
        const std::vector<std::byte> overBudget(TES3MP::SessionControlMaximumPayloadBytes + 1);

        return hasError(TES3MP::decodeClientHello(duplicate), HandshakeErrorCode::CapabilitiesNotStrictlySorted)
            && hasError(TES3MP::decodeClientHello(zero), HandshakeErrorCode::ZeroCapability)
            && hasError(TES3MP::decodeClientHello(overlap), HandshakeErrorCode::CapabilityInBothSets)
            && hasError(TES3MP::decodeClientHello(oversized), HandshakeErrorCode::TooManyCapabilities)
            && hasError(TES3MP::decodeClientHello(overBudget), HandshakeErrorCode::PayloadTooLarge);
    }

    bool preamble_classification_never_replies_to_legacy_or_unknown_bytes()
    {
        const std::array shortInput{ std::byte{ 'T' }, std::byte{ '3' }, std::byte{ 'M' } };
        const std::array vnext{ std::byte{ 'T' }, std::byte{ '3' }, std::byte{ 'M' }, std::byte{ 'P' } };
        const std::array legacy{ std::byte{ 'T' }, std::byte{ 'E' }, std::byte{ 'S' }, std::byte{ '3' } };
        return TES3MP::classifyInitialPeerProtocol(shortInput) == TES3MP::InitialPeerProtocol::NeedMoreBytes
            && TES3MP::classifyInitialPeerProtocol(vnext) == TES3MP::InitialPeerProtocol::Vnext
            && TES3MP::classifyInitialPeerProtocol(legacy) == TES3MP::InitialPeerProtocol::LegacyOrUnknown;
    }

    bool golden_payloads_are_exact()
    {
        const Fixture fixture;
        const auto client = TES3MP::encodeClientHello(fixture.client);
        const auto server = TES3MP::encodeServerHello(fixture.accepted);
        const auto rejected = TES3MP::encodeSessionRejected(fixture.rejected);
        return bytesEqual(client,
                   { 60, 0, 0, 0, 24, 0, 0, 0, 84, 51, 67, 72, 0, 0, 14, 0, 16, 0, 4, 0, 0, 0, 6, 0, 8, 0, 12, 0, 14, 0,
                       0, 0, 1, 0, 1, 0, 16, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 3, 0,
                       0, 0 })
            && bytesEqual(server,
                { 48, 0, 0, 0, 20, 0, 0, 0, 84, 51, 83, 72, 0, 0, 10, 0, 12, 0, 4, 0, 6, 0, 8, 0, 10, 0, 0, 0, 1, 0, 1,
                    0, 4, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0 })
            && bytesEqual(rejected,
                { 32, 0, 0, 0, 20, 0, 0, 0, 84, 51, 82, 74, 12, 0, 12, 0, 7, 0, 8, 0, 0, 0, 10, 0, 12, 0, 0, 0, 0, 0, 0,
                    1, 1, 0, 1, 0 });
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--print-golden")
    {
        const Fixture fixture;
        printBytes(TES3MP::encodeClientHello(fixture.client));
        printBytes(TES3MP::encodeServerHello(fixture.accepted));
        printBytes(TES3MP::encodeSessionRejected(fixture.rejected));
        return 0;
    }

    return value_factories_reject_invalid_ranges_and_capability_sets()
            && current_and_previous_minor_select_highest_overlap()
            && optional_capabilities_intersect_without_enabling_unknown_ids()
            && version_and_required_capability_failures_are_stable() && all_payloads_round_trip_as_owned_values()
            && every_truncation_wrong_identifier_and_trailing_byte_fail_without_partial_value()
            && hostile_capability_vectors_reject_before_negotiation()
            && preamble_classification_never_replies_to_legacy_or_unknown_bytes() && golden_payloads_are_exact()
        ? 0
        : 1;
}
