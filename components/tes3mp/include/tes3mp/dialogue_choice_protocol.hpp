#ifndef TES3MP_DIALOGUE_CHOICE_PROTOCOL_HPP
#define TES3MP_DIALOGUE_CHOICE_PROTOCOL_HPP

#include "command_primitives.hpp"
#include "session_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    struct ClientDialogueChoiceCommand
    {
        SessionId sessionId;
        SessionGeneration sessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        CanonicalRevision observedCanonicalRevision;
        DialogueChoiceId choiceId;

        friend constexpr bool operator==(ClientDialogueChoiceCommand, ClientDialogueChoiceCommand) noexcept = default;
    };

    enum class DialogueChoiceDisposition : std::uint8_t
    {
        Committed = 0,
        UnknownChoice = 1,
        Ineligible = 2,
        Rejected = 3,
    };

    struct ReliableDialogueChoiceResult
    {
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        DialogueChoiceId choiceId;
        DialogueChoiceDisposition disposition = DialogueChoiceDisposition::Rejected;
        bool duplicate = false;
        CanonicalRevision committedCanonicalRevision = CanonicalRevision::initial();

        constexpr bool committed() const noexcept { return disposition == DialogueChoiceDisposition::Committed; }
        friend constexpr bool operator==(ReliableDialogueChoiceResult, ReliableDialogueChoiceResult) noexcept
            = default;
    };

    enum class DialogueChoiceProtocolErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        InvalidStrongValue,
        UnknownDisposition,
    };

    struct DialogueChoiceProtocolError
    {
        DialogueChoiceProtocolErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;

        friend constexpr bool operator==(DialogueChoiceProtocolError, DialogueChoiceProtocolError) noexcept
            = default;
    };

    using ClientDialogueChoiceCommandDecodeResult
        = std::variant<ClientDialogueChoiceCommand, DialogueChoiceProtocolError>;
    using ReliableDialogueChoiceResultDecodeResult
        = std::variant<ReliableDialogueChoiceResult, DialogueChoiceProtocolError>;

    std::vector<std::byte> encodeClientDialogueChoiceCommand(const ClientDialogueChoiceCommand& value);
    ClientDialogueChoiceCommandDecodeResult decodeClientDialogueChoiceCommand(std::span<const std::byte> payload);
    std::vector<std::byte> encodeReliableDialogueChoiceResult(const ReliableDialogueChoiceResult& value);
    ReliableDialogueChoiceResultDecodeResult decodeReliableDialogueChoiceResult(std::span<const std::byte> payload);
}

#endif
