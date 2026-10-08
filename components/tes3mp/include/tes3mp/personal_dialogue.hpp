#ifndef TES3MP_PERSONAL_DIALOGUE_HPP
#define TES3MP_PERSONAL_DIALOGUE_HPP
#include "command_primitives.hpp"
#include "session_types.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace TES3MP
{
    struct DialogueIntent
    {
        uint64_t story = 0, speaker = 0, life = 0, revision = 0;
        std::string topic;
        int32_t choice = -1;
        bool operator==(const DialogueIntent&) const = default;
    };
    struct ClientDialogueIntent
    {
        SessionId sessionId;
        SessionGeneration sessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        CanonicalRevision observedCanonicalRevision;
        DialogueIntent intent;
        bool operator==(const ClientDialogueIntent&) const = default;
    };
    struct DialogueChoice
    {
        std::string text;
        int32_t id;
        bool operator==(const DialogueChoice&) const = default;
    };
    struct DialogueQuest
    {
        std::string topic;
        int32_t index;
        bool finished;
        bool operator==(const DialogueQuest&) const = default;
    };
    struct DialogueEntry
    {
        std::string topic, info;
        bool operator==(const DialogueEntry&) const = default;
    };
    // Full personal progression is recoverable. Response/messages are transient:
    // only the transaction that executes an occurrence may publish them.
    struct ReliablePersonalDialogue
    {
        SessionId targetSessionId;
        SessionGeneration targetSessionGeneration;
        ServerTick serverTick;
        CanonicalRevision canonicalRevision;
        uint64_t story = 0, revision = 0, speaker = 0, life = 0, command = 0;
        std::string topic, response;
        std::vector<DialogueChoice> choices;
        std::vector<DialogueQuest> quests;
        std::vector<DialogueEntry> entries;
        std::vector<std::string> topics, messages;
        bool operator==(const ReliablePersonalDialogue&) const = default;
    };
    inline constexpr size_t MaximumPersonalDialogueBytes = 32768;
    bool validDialogueIntent(const DialogueIntent&) noexcept;
    bool validPersonalDialogue(const ReliablePersonalDialogue&) noexcept;
    std::vector<std::byte> encodeClientDialogueIntent(const ClientDialogueIntent&);
    std::optional<ClientDialogueIntent> decodeClientDialogueIntent(std::span<const std::byte>);
    std::vector<std::byte> encodeReliablePersonalDialogue(const ReliablePersonalDialogue&);
    std::optional<ReliablePersonalDialogue> decodeReliablePersonalDialogue(std::span<const std::byte>);
}
#endif
