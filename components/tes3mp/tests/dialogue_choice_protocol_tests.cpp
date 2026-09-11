#include <tes3mp/dialogue_choice_protocol.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <variant>

namespace
{
    using namespace TES3MP;

    template <class T> T id(std::uint64_t value) { return *T::fromValue(value); }

    bool commandRoundTrips()
    {
        const ClientDialogueChoiceCommand value{ id<SessionId>(11), id<SessionGeneration>(2),
            id<CommandSequence>(3), id<CommandId>(4), id<CanonicalRevision>(5), id<DialogueChoiceId>(6) };
        const auto decoded = decodeClientDialogueChoiceCommand(encodeClientDialogueChoiceCommand(value));
        return std::holds_alternative<ClientDialogueChoiceCommand>(decoded)
            && std::get<ClientDialogueChoiceCommand>(decoded) == value;
    }

    bool resultsRoundTrip()
    {
        constexpr std::array dispositions{ DialogueChoiceDisposition::Committed,
            DialogueChoiceDisposition::UnknownChoice, DialogueChoiceDisposition::Ineligible,
            DialogueChoiceDisposition::Rejected };
        for (const auto disposition : dispositions)
        {
            const ReliableDialogueChoiceResult value{ id<SessionId>(11), id<SessionGeneration>(2),
                id<CommandSequence>(3), id<CommandId>(4), id<DialogueChoiceId>(6), disposition,
                disposition == DialogueChoiceDisposition::Committed, id<CanonicalRevision>(7) };
            const auto decoded = decodeReliableDialogueChoiceResult(encodeReliableDialogueChoiceResult(value));
            if (!std::holds_alternative<ReliableDialogueChoiceResult>(decoded)
                || std::get<ReliableDialogueChoiceResult>(decoded) != value)
                return false;
        }
        return true;
    }

    bool malformedInputIsRejected()
    {
        const ClientDialogueChoiceCommand value{ id<SessionId>(11), id<SessionGeneration>(2),
            id<CommandSequence>(3), id<CommandId>(4), id<CanonicalRevision>(5), id<DialogueChoiceId>(6) };
        auto encoded = encodeClientDialogueChoiceCommand(value);
        for (std::size_t size = 0; size < encoded.size(); ++size)
            if (std::holds_alternative<ClientDialogueChoiceCommand>(
                    decodeClientDialogueChoiceCommand(std::span(encoded).first(size))))
                return false;
        encoded[8] ^= std::byte{ 0xff };
        return std::holds_alternative<DialogueChoiceProtocolError>(decodeClientDialogueChoiceCommand(encoded));
    }
}

int main()
{
    static_assert(static_cast<std::uint8_t>(DialogueChoiceDisposition::Rejected) == 3);
    if (!commandRoundTrips() || !resultsRoundTrip() || !malformedInputIsRejected())
        return 1;
    std::cout << "dialogue choice protocol contracts passed\n";
}
