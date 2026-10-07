#include <tes3mp/ambient_replication.hpp>
#include <array>

namespace TES3MP
{
    bool validAmbientBatch(const ReliableAmbientBatch& batch) noexcept
    {
        if (!batch.invocation || batch.script.empty() || batch.script.size() > ReliableAmbientBatch::MaximumScriptBytes
            || batch.script.find('\0') != std::string::npos || batch.messages.empty()
            || batch.messages.size() > ReliableAmbientBatch::MaximumMessages) return false;
        for (const auto& message : batch.messages)
            if (message.size() > ReliableAmbientBatch::MaximumMessageBytes || message.find('\0') != std::string::npos)
                return false;
        return true;
    }

    std::vector<std::byte> encodeReliableAmbientBatch(const ReliableAmbientBatch& batch)
    {
        if (!validAmbientBatch(batch)) return {};
        std::vector<std::byte> bytes{std::byte{1}};
        for (uint64_t word : {batch.targetSessionId.value(), batch.targetSessionGeneration.value(),
                batch.serverTick.value(), batch.canonicalRevision.value(), batch.invocation})
            for (unsigned i = 0; i < 8; ++i) bytes.push_back(std::byte((word >> (8 * i)) & 255));
        const auto string = [&](const std::string& text) {
            bytes.push_back(std::byte(text.size() & 255)); bytes.push_back(std::byte(text.size() >> 8));
            for (unsigned char c : text) bytes.push_back(std::byte(c));
        };
        string(batch.script); bytes.push_back(std::byte(batch.messages.size()));
        for (const auto& message : batch.messages) string(message);
        return bytes;
    }

    std::optional<ReliableAmbientBatch> decodeReliableAmbientBatch(std::span<const std::byte> bytes)
    {
        if (bytes.size() < 47 || bytes.size() > ReliableAmbientBatch::MaximumPayloadBytes || bytes[0] != std::byte{1}) return {};
        std::array<uint64_t, 5> words{};
        for (size_t i = 0; i < words.size(); ++i)
            for (unsigned j = 0; j < 8; ++j)
                words[i] |= uint64_t(std::to_integer<unsigned char>(bytes[1 + i * 8 + j])) << (8 * j);
        const auto session = SessionId::fromValue(words[0]);
        const auto generation = SessionGeneration::fromValue(words[1]);
        const auto tick = ServerTick::fromValue(words[2]);
        const auto revision = CanonicalRevision::fromValue(words[3]);
        if (!session || !generation || !tick || !revision || !words[4]) return {};
        size_t offset = 41;
        const auto string = [&](std::string& text, size_t limit) {
            if (bytes.size() - offset < 2) return false;
            const size_t length = std::to_integer<unsigned char>(bytes[offset])
                | (size_t(std::to_integer<unsigned char>(bytes[offset + 1])) << 8);
            offset += 2;
            if (length > limit || length > bytes.size() - offset) return false;
            const auto data = bytes.subspan(offset, length);
            for (auto c : data) if (c == std::byte{0}) return false;
            text.assign(reinterpret_cast<const char*>(data.data()), length); offset += length;
            return true;
        };
        ReliableAmbientBatch batch{*session, *generation, *tick, *revision, words[4], {}, {}};
        if (!string(batch.script, ReliableAmbientBatch::MaximumScriptBytes) || batch.script.empty() || offset == bytes.size()) return {};
        const size_t count = std::to_integer<unsigned char>(bytes[offset++]);
        if (!count || count > ReliableAmbientBatch::MaximumMessages) return {};
        batch.messages.resize(count);
        for (auto& message : batch.messages) if (!string(message, ReliableAmbientBatch::MaximumMessageBytes)) return {};
        if (offset != bytes.size()) return {};
        return batch;
    }
}
