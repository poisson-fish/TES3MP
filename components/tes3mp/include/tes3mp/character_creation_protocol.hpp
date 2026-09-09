#ifndef TES3MP_CHARACTER_CREATION_PROTOCOL_HPP
#define TES3MP_CHARACTER_CREATION_PROTOCOL_HPP

#include "character_profile.hpp"
#include "command_primitives.hpp"
#include "session_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace TES3MP
{
    struct ClientCharacterCreationCommand
    {
        SessionId sessionId;
        SessionGeneration sessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        CanonicalRevision observedCanonicalRevision;
        CharacterCreationCommand command;
        friend bool operator==(const ClientCharacterCreationCommand&,
            const ClientCharacterCreationCommand&) noexcept = default;
    };

    enum class CharacterConfirmationResult : std::uint8_t
    {
        Confirmed = 0,
        StaleRevision = 1,
        AlreadyEstablished = 2,
        WrongPhase = 3,
        InvalidName = 4,
        UnknownRace = 5,
        InvalidAppearance = 6,
        UnknownClass = 7,
        InvalidCustomClass = 8,
        UnknownBirthsign = 9,
        Incomplete = 10,
        InternalError = 11,
    };

    struct ReliableCharacterProfile
    {
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        PlayerId playerId;
        CharacterConfirmationResult result = CharacterConfirmationResult::Confirmed;
        CharacterProfile profile = CharacterProfile::fresh();
        friend bool operator==(const ReliableCharacterProfile&, const ReliableCharacterProfile&) noexcept = default;
    };

    enum class CharacterProtocolErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        InvalidStrongValue,
        UnknownCommandKind,
        UnexpectedField,
        InvalidText,
        InvalidAppearance,
        InvalidCustomClass,
        UnknownResult,
        UnknownLifecycle,
        UnknownPhase,
        InvalidProfile,
        InvalidVectorSize,
        InvalidInventory,
    };

    struct CharacterProtocolError
    {
        CharacterProtocolErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        friend constexpr bool operator==(CharacterProtocolError, CharacterProtocolError) noexcept = default;
    };

    using ClientCharacterCreationCommandDecodeResult
        = std::variant<ClientCharacterCreationCommand, CharacterProtocolError>;
    using ReliableCharacterProfileDecodeResult = std::variant<ReliableCharacterProfile, CharacterProtocolError>;

    std::vector<std::byte> encodeClientCharacterCreationCommand(const ClientCharacterCreationCommand& value);
    ClientCharacterCreationCommandDecodeResult decodeClientCharacterCreationCommand(std::span<const std::byte> payload);
    std::vector<std::byte> encodeReliableCharacterProfile(const ReliableCharacterProfile& value);
    ReliableCharacterProfileDecodeResult decodeReliableCharacterProfile(std::span<const std::byte> payload);
    CharacterConfirmationResult characterConfirmationResult(CharacterProfileError error) noexcept;
    std::string_view characterConfirmationResultName(CharacterConfirmationResult result) noexcept;
}

#endif
