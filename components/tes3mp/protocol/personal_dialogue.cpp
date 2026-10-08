#include <algorithm>
#include <bit>
#include <stdexcept>
#include <tes3mp/personal_dialogue.hpp>

namespace TES3MP
{
    namespace
    {
        bool textValid(std::string_view value, size_t bound, bool empty = true)
        {
            return (empty || !value.empty()) && value.size() <= bound && value.find('\0') == value.npos;
        }
        struct Writer
        {
            std::vector<std::byte> bytes{ std::byte{ 1 } };
            void word(uint64_t value)
            {
                for (unsigned i = 0; i < 8; ++i)
                    bytes.push_back(std::byte(value >> (8 * i)));
            }
            void string(const std::string& value)
            {
                word(value.size());
                for (unsigned char c : value)
                    bytes.push_back(std::byte(c));
            }
        };
        struct Reader
        {
            std::span<const std::byte> bytes;
            explicit Reader(std::span<const std::byte> input, size_t bound)
                : bytes(input)
            {
                if (bytes.empty() || bytes.size() > bound || bytes[0] != std::byte{ 1 })
                    throw std::invalid_argument("dialogue frame");
                bytes = bytes.subspan(1);
            }
            uint64_t word()
            {
                if (bytes.size() < 8)
                    throw std::invalid_argument("dialogue word");
                uint64_t value = 0;
                for (unsigned i = 0; i < 8; ++i)
                    value |= uint64_t(std::to_integer<unsigned char>(bytes[i])) << (8 * i);
                bytes = bytes.subspan(8);
                return value;
            }
            size_t count(size_t bound)
            {
                auto n = word();
                if (n > bound)
                    throw std::invalid_argument("dialogue bound");
                return size_t(n);
            }
            int32_t integer()
            {
                auto value = word();
                if (value > UINT32_MAX)
                    throw std::invalid_argument("dialogue integer");
                return std::bit_cast<int32_t>(uint32_t(value));
            }
            std::string string(size_t bound)
            {
                auto n = count(bound);
                if (n > bytes.size())
                    throw std::invalid_argument("dialogue text");
                std::string value(reinterpret_cast<const char*>(bytes.data()), n);
                bytes = bytes.subspan(n);
                if (!textValid(value, bound))
                    throw std::invalid_argument("dialogue text");
                return value;
            }
            template <class T>
            T strong()
            {
                auto value = T::fromValue(word());
                if (!value)
                    throw std::invalid_argument("dialogue identity");
                return *value;
            }
        };
        void intent(Writer& w, const DialogueIntent& v)
        {
            w.word(v.story);
            w.word(v.speaker);
            w.word(v.life);
            w.word(v.revision);
            w.string(v.topic);
            w.word(std::bit_cast<uint32_t>(v.choice));
        }
    }
    bool validDialogueIntent(const DialogueIntent& value) noexcept
    {
        return value.story && value.speaker && value.life && value.revision != UINT64_MAX && value.choice >= -1
            && textValid(value.topic, 128, false);
    }
    bool validPersonalDialogue(const ReliablePersonalDialogue& v) noexcept
    {
        if (!v.story || bool(v.speaker) != bool(v.life) || bool(v.revision) != bool(v.command)
            || !textValid(v.topic, 128) || !textValid(v.response, 4096) || v.choices.size() > 8 || v.quests.size() > 16
            || v.entries.size() > 64 || v.topics.size() > 16 || v.messages.size() > 8)
            return false;
        for (size_t i = 0; i < v.choices.size(); ++i)
            if (v.choices[i].id < 0 || !textValid(v.choices[i].text, 256, false)
                || std::ranges::any_of(
                    std::span(v.choices).first(i), [&](const auto& c) { return c.id == v.choices[i].id; }))
                return false;
        if (v.revision && (!v.speaker || v.topic.empty()))
            return false;
        if (!v.revision
            && (v.speaker || !v.topic.empty() || !v.choices.empty() || !v.response.empty() || !v.messages.empty()))
            return false;
        for (size_t i = 0; i < v.quests.size(); ++i)
            if (!textValid(v.quests[i].topic, 128, false)
                || std::ranges::any_of(
                    std::span(v.quests).first(i), [&](const auto& q) { return q.topic == v.quests[i].topic; }))
                return false;
        for (size_t i = 0; i < v.entries.size(); ++i)
            if (!textValid(v.entries[i].topic, 128, false) || !textValid(v.entries[i].info, 128, false)
                || std::ranges::find(std::span(v.entries).first(i), v.entries[i]) != std::span(v.entries).first(i).end()
                || std::ranges::none_of(v.quests, [&](const auto& q) { return q.topic == v.entries[i].topic; }))
                return false;
        for (size_t i = 0; i < v.topics.size(); ++i)
            if (!textValid(v.topics[i], 128, false)
                || std::ranges::find(std::span(v.topics).first(i), v.topics[i]) != std::span(v.topics).first(i).end())
                return false;
        for (const auto& m : v.messages)
            if (!textValid(m, 256))
                return false;
        return true;
    }
    std::vector<std::byte> encodeClientDialogueIntent(const ClientDialogueIntent& v)
    {
        if (!validDialogueIntent(v.intent))
            return {};
        Writer w;
        for (auto n : { v.sessionId.value(), v.sessionGeneration.value(), v.commandSequence.value(),
                 v.commandId.value(), v.observedCanonicalRevision.value() })
            w.word(n);
        intent(w, v.intent);
        return std::move(w.bytes);
    }
    std::optional<ClientDialogueIntent> decodeClientDialogueIntent(std::span<const std::byte> bytes)
    try
    {
        Reader r(bytes, 256);
        ClientDialogueIntent v{ r.strong<SessionId>(), r.strong<SessionGeneration>(), r.strong<CommandSequence>(),
            r.strong<CommandId>(), r.strong<CanonicalRevision>(), {} };
        v.intent = { r.word(), r.word(), r.word(), r.word(), r.string(128), r.integer() };
        if (!r.bytes.empty() || !validDialogueIntent(v.intent))
            return {};
        return v;
    }
    catch (...)
    {
        return {};
    }
    std::vector<std::byte> encodeReliablePersonalDialogue(const ReliablePersonalDialogue& v)
    {
        if (!validPersonalDialogue(v))
            return {};
        Writer w;
        for (auto n : { v.targetSessionId.value(), v.targetSessionGeneration.value(), v.serverTick.value(),
                 v.canonicalRevision.value(), v.story, v.revision, v.speaker, v.life, v.command })
            w.word(n);
        w.string(v.topic);
        w.string(v.response);
        w.word(v.choices.size());
        for (const auto& c : v.choices)
        {
            w.string(c.text);
            w.word(std::bit_cast<uint32_t>(c.id));
        }
        w.word(v.quests.size());
        for (const auto& q : v.quests)
        {
            w.string(q.topic);
            w.word(std::bit_cast<uint32_t>(q.index));
            w.word(q.finished);
        }
        w.word(v.entries.size());
        for (const auto& e : v.entries)
        {
            w.string(e.topic);
            w.string(e.info);
        }
        w.word(v.topics.size());
        for (const auto& t : v.topics)
            w.string(t);
        w.word(v.messages.size());
        for (const auto& m : v.messages)
            w.string(m);
        if (w.bytes.size() > MaximumPersonalDialogueBytes)
            return {};
        return std::move(w.bytes);
    }
    std::optional<ReliablePersonalDialogue> decodeReliablePersonalDialogue(std::span<const std::byte> bytes)
    try
    {
        Reader r(bytes, MaximumPersonalDialogueBytes);
        ReliablePersonalDialogue v{ r.strong<SessionId>(), r.strong<SessionGeneration>(), r.strong<ServerTick>(),
            r.strong<CanonicalRevision>() };
        v.story = r.word();
        v.revision = r.word();
        v.speaker = r.word();
        v.life = r.word();
        v.command = r.word();
        v.topic = r.string(128);
        v.response = r.string(4096);
        for (size_t n = r.count(8); n--;)
            v.choices.push_back({ r.string(256), r.integer() });
        for (size_t n = r.count(16); n--;)
        {
            auto t = r.string(128);
            auto i = r.integer();
            auto b = r.count(1);
            v.quests.push_back({ std::move(t), i, bool(b) });
        }
        for (size_t n = r.count(64); n--;)
            v.entries.push_back({ r.string(128), r.string(128) });
        for (size_t n = r.count(16); n--;)
            v.topics.push_back(r.string(128));
        for (size_t n = r.count(8); n--;)
            v.messages.push_back(r.string(256));
        if (!r.bytes.empty() || !validPersonalDialogue(v))
            return {};
        return v;
    }
    catch (...)
    {
        return {};
    }
}
