#include "script_module.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_SCRIPT_MODULE_V1";

        struct IncrementInteger
        {
            ScriptVariableId id;
            std::int64_t delta;
        };

        struct ConsumeBudget
        {
            std::uint32_t units;
        };

        using Instruction = std::variant<IncrementInteger, ConsumeBudget>;

        struct ParsedCallback
        {
            std::uint32_t order = 0;
            ServerScriptEventKind eventKind = ServerScriptEventKind::CommandFinalized;
            std::vector<Instruction> instructions;
        };

        class ModuleCallback final : public ServerScriptCallback
        {
        public:
            ModuleCallback(std::uint64_t packageId, std::uint32_t budget, std::vector<Instruction> instructions)
                : mPackageId(packageId)
                , mBudget(budget)
                , mInstructions(std::move(instructions))
            {
            }

            ServerScriptCallbackResult onEvent(
                const ServerScriptCallbackInput& input, ServerScriptCommandEmitter& output) noexcept override
            try
            {
                std::uint32_t consumed = 0;
                for (const auto& instruction : mInstructions)
                {
                    const auto cost = std::holds_alternative<ConsumeBudget>(instruction)
                        ? std::get<ConsumeBudget>(instruction).units
                        : 1;
                    if (cost > mBudget - consumed)
                        return ServerScriptCallbackResult::Failed;
                    consumed += cost;
                    if (const auto* increment = std::get_if<IncrementInteger>(&instruction))
                    {
                        const auto found = std::ranges::find_if(input.persistentState(), [&](const auto& value) {
                            return value.packageId == mPackageId && value.id == increment->id;
                        });
                        if (found == input.persistentState().end())
                            return ServerScriptCallbackResult::Failed;
                        const auto* current = std::get_if<std::int64_t>(&found->value);
                        if (!current
                            || (increment->delta > 0
                                && *current > std::numeric_limits<std::int64_t>::max() - increment->delta)
                            || (increment->delta < 0
                                && *current < std::numeric_limits<std::int64_t>::min() - increment->delta))
                            return ServerScriptCallbackResult::Failed;
                        if (output.enqueue(ServerScriptCompareAndSetPersistentCommand(
                                increment->id, found->revision, *current + increment->delta))
                            != ServerScriptEmitResult::Accepted)
                            return ServerScriptCallbackResult::Failed;
                    }
                }
                return ServerScriptCallbackResult::Accepted;
            }
            catch (...)
            {
                return ServerScriptCallbackResult::Failed;
            }

        private:
            std::uint64_t mPackageId;
            std::uint32_t mBudget;
            std::vector<Instruction> mInstructions;
        };

        template <class Value>
        std::optional<Value> number(std::string_view text) noexcept
        {
            Value value{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        std::optional<ServerScriptEventKind> eventKind(std::string_view text) noexcept
        {
            if (text == "command_finalized")
                return ServerScriptEventKind::CommandFinalized;
            if (text == "session_joined")
                return ServerScriptEventKind::SessionJoined;
            if (text == "spatial_state_changed")
                return ServerScriptEventKind::SpatialStateChanged;
            if (text == "session_lifecycle")
                return ServerScriptEventKind::SessionLifecycle;
            return std::nullopt;
        }

        std::optional<std::vector<std::string_view>> fields(std::string_view line) noexcept
        try
        {
            std::vector<std::string_view> result;
            for (std::size_t begin = 0; begin < line.size();)
            {
                while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t'))
                    ++begin;
                if (begin == line.size())
                    break;
                if (result.size() == 3)
                    return std::nullopt;
                auto end = begin;
                while (end < line.size() && line[end] != ' ' && line[end] != '\t')
                    ++end;
                result.push_back(line.substr(begin, end - begin));
                begin = end;
            }
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }

        constexpr std::array<std::uint32_t, 64> ShaConstants{
            0x428a2f98,
            0x71374491,
            0xb5c0fbcf,
            0xe9b5dba5,
            0x3956c25b,
            0x59f111f1,
            0x923f82a4,
            0xab1c5ed5,
            0xd807aa98,
            0x12835b01,
            0x243185be,
            0x550c7dc3,
            0x72be5d74,
            0x80deb1fe,
            0x9bdc06a7,
            0xc19bf174,
            0xe49b69c1,
            0xefbe4786,
            0x0fc19dc6,
            0x240ca1cc,
            0x2de92c6f,
            0x4a7484aa,
            0x5cb0a9dc,
            0x76f988da,
            0x983e5152,
            0xa831c66d,
            0xb00327c8,
            0xbf597fc7,
            0xc6e00bf3,
            0xd5a79147,
            0x06ca6351,
            0x14292967,
            0x27b70a85,
            0x2e1b2138,
            0x4d2c6dfc,
            0x53380d13,
            0x650a7354,
            0x766a0abb,
            0x81c2c92e,
            0x92722c85,
            0xa2bfe8a1,
            0xa81a664b,
            0xc24b8b70,
            0xc76c51a3,
            0xd192e819,
            0xd6990624,
            0xf40e3585,
            0x106aa070,
            0x19a4c116,
            0x1e376c08,
            0x2748774c,
            0x34b0bcb5,
            0x391c0cb3,
            0x4ed8aa4a,
            0x5b9cca4f,
            0x682e6ff3,
            0x748f82ee,
            0x78a5636f,
            0x84c87814,
            0x8cc70208,
            0x90befffa,
            0xa4506ceb,
            0xbef9a3f7,
            0xc67178f2,
        };

        std::array<std::byte, ScriptModuleHashBytes> sha256(std::span<const std::byte> source)
        {
            std::vector<std::byte> padded(source.begin(), source.end());
            padded.push_back(std::byte{ 0x80 });
            while (padded.size() % 64 != 56)
                padded.push_back(std::byte{});
            const auto bitLength = static_cast<std::uint64_t>(source.size()) * 8;
            for (int shift = 56; shift >= 0; shift -= 8)
                padded.push_back(static_cast<std::byte>((bitLength >> shift) & 0xff));

            std::array<std::uint32_t, 8> state{ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
                0x1f83d9ab, 0x5be0cd19 };
            for (std::size_t offset = 0; offset < padded.size(); offset += 64)
            {
                std::array<std::uint32_t, 64> words{};
                for (std::size_t index = 0; index < 16; ++index)
                    words[index] = (std::to_integer<std::uint32_t>(padded[offset + index * 4]) << 24)
                        | (std::to_integer<std::uint32_t>(padded[offset + index * 4 + 1]) << 16)
                        | (std::to_integer<std::uint32_t>(padded[offset + index * 4 + 2]) << 8)
                        | std::to_integer<std::uint32_t>(padded[offset + index * 4 + 3]);
                for (std::size_t index = 16; index < words.size(); ++index)
                {
                    const auto s0
                        = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
                    const auto s1
                        = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
                    words[index] = words[index - 16] + s0 + words[index - 7] + s1;
                }
                auto [a, b, c, d, e, f, g, h] = state;
                for (std::size_t index = 0; index < words.size(); ++index)
                {
                    const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
                    const auto choice = (e & f) ^ (~e & g);
                    const auto temporary1 = h + s1 + choice + ShaConstants[index] + words[index];
                    const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
                    const auto majority = (a & b) ^ (a & c) ^ (b & c);
                    const auto temporary2 = s0 + majority;
                    h = g;
                    g = f;
                    f = e;
                    e = d + temporary1;
                    d = c;
                    c = b;
                    b = a;
                    a = temporary1 + temporary2;
                }
                const std::array next{ a, b, c, d, e, f, g, h };
                for (std::size_t index = 0; index < state.size(); ++index)
                    state[index] += next[index];
            }
            std::array<std::byte, ScriptModuleHashBytes> result{};
            for (std::size_t index = 0; index < state.size(); ++index)
                for (std::size_t byte = 0; byte < 4; ++byte)
                    result[index * 4 + byte] = static_cast<std::byte>((state[index] >> (24 - byte * 8)) & 0xff);
            return result;
        }

        struct ParsedModule
        {
            std::uint32_t abi = 0;
            std::uint32_t api = 0;
            std::string entrypoint;
            std::vector<ParsedCallback> callbacks;
        };

        std::variant<ParsedModule, ExecutableScriptModuleError> parseModule(std::string_view text,
            const ScriptModuleBinding& binding, const ServerScriptStateCatalog& stateCatalog) noexcept
        try
        {
            ParsedModule module;
            std::optional<ParsedCallback> callback;
            bool sawHeader = false;
            bool sawAbi = false;
            bool sawApi = false;
            for (std::size_t begin = 0, lineNumber = 1; begin <= text.size(); ++lineNumber)
            {
                const auto lineEnd = text.find('\n', begin);
                const auto length = (lineEnd == std::string_view::npos ? text.size() : lineEnd) - begin;
                auto line = text.substr(begin, length);
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
                if (lineNumber == 1)
                {
                    if (line != Header)
                        return ExecutableScriptModuleError::Malformed;
                    sawHeader = true;
                }
                else
                {
                    const auto first = line.find_first_not_of(" \t");
                    if (first == std::string_view::npos || line[first] == '#')
                    {
                        if (lineEnd == std::string_view::npos)
                            break;
                        begin = lineEnd + 1;
                        continue;
                    }
                    const auto tokens = fields(line);
                    if (!tokens || tokens->empty())
                        return ExecutableScriptModuleError::Malformed;
                    if ((*tokens)[0] == "abi")
                    {
                        const auto value = tokens->size() == 2 ? number<std::uint32_t>((*tokens)[1]) : std::nullopt;
                        if (!value || sawAbi)
                            return ExecutableScriptModuleError::Malformed;
                        module.abi = *value;
                        sawAbi = true;
                    }
                    else if ((*tokens)[0] == "api")
                    {
                        const auto value = tokens->size() == 2 ? number<std::uint32_t>((*tokens)[1]) : std::nullopt;
                        if (!value || sawApi)
                            return ExecutableScriptModuleError::Malformed;
                        module.api = *value;
                        sawApi = true;
                    }
                    else if ((*tokens)[0] == "entry")
                    {
                        if (tokens->size() != 2 || !module.entrypoint.empty() || callback)
                            return ExecutableScriptModuleError::Malformed;
                        module.entrypoint = std::string((*tokens)[1]);
                    }
                    else if ((*tokens)[0] == "callback")
                    {
                        const auto order = tokens->size() == 3 ? number<std::uint32_t>((*tokens)[1]) : std::nullopt;
                        const auto kind = tokens->size() == 3 ? eventKind((*tokens)[2]) : std::nullopt;
                        if (!order || !kind || callback || module.entrypoint.empty()
                            || module.callbacks.size() == MaximumScriptModuleCallbacks)
                            return module.callbacks.size() == MaximumScriptModuleCallbacks
                                ? ExecutableScriptModuleError::InvalidResourceBounds
                                : ExecutableScriptModuleError::Malformed;
                        callback = ParsedCallback{ *order, *kind, {} };
                    }
                    else if ((*tokens)[0] == "increment_integer")
                    {
                        const auto rawId = tokens->size() == 3 ? number<std::uint64_t>((*tokens)[1]) : std::nullopt;
                        const auto delta = tokens->size() == 3 ? number<std::int64_t>((*tokens)[2]) : std::nullopt;
                        const auto id = rawId ? ScriptVariableId::fromValue(*rawId) : std::nullopt;
                        if (!callback || !id || !delta || *delta == 0)
                            return ExecutableScriptModuleError::Malformed;
                        const auto* catalog = stateCatalog.find(binding.package.packageId(), *id);
                        if (!catalog || catalog->type() != ScriptVariableType::Integer)
                            return ExecutableScriptModuleError::InvalidStateCatalog;
                        if (callback->instructions.size() == MaximumScriptModuleInstructionsPerCallback)
                            return ExecutableScriptModuleError::InvalidResourceBounds;
                        callback->instructions.emplace_back(IncrementInteger{ *id, *delta });
                    }
                    else if ((*tokens)[0] == "consume")
                    {
                        const auto units = tokens->size() == 2 ? number<std::uint32_t>((*tokens)[1]) : std::nullopt;
                        if (!callback || !units || *units == 0 || *units > MaximumScriptModuleExecutionBudget)
                            return ExecutableScriptModuleError::Malformed;
                        if (callback->instructions.size() == MaximumScriptModuleInstructionsPerCallback)
                            return ExecutableScriptModuleError::InvalidResourceBounds;
                        callback->instructions.emplace_back(ConsumeBudget{ *units });
                    }
                    else if ((*tokens)[0] == "end")
                    {
                        if (tokens->size() != 1 || !callback || callback->instructions.empty())
                            return ExecutableScriptModuleError::Malformed;
                        if (std::ranges::any_of(module.callbacks, [&](const auto& existing) {
                                return existing.order == callback->order && existing.eventKind == callback->eventKind;
                            }))
                            return ExecutableScriptModuleError::Malformed;
                        module.callbacks.push_back(std::move(*callback));
                        callback.reset();
                    }
                    else
                        return ExecutableScriptModuleError::Malformed;
                }
                if (lineEnd == std::string_view::npos)
                    break;
                begin = lineEnd + 1;
            }
            if (!sawHeader || !sawAbi || !sawApi || callback || module.callbacks.empty())
                return ExecutableScriptModuleError::Malformed;
            if (module.abi != ServerScriptModuleAbiVersion)
                return ExecutableScriptModuleError::AbiMismatch;
            if (module.api != ServerScriptApiVersion || module.api != binding.package.apiVersion())
                return ExecutableScriptModuleError::ApiMismatch;
            if (module.entrypoint != binding.entrypoint)
                return ExecutableScriptModuleError::EntrypointMissing;
            return module;
        }
        catch (...)
        {
            return ExecutableScriptModuleError::Malformed;
        }
    }

    ExecutableScriptModuleLoadResult loadExecutableScriptModules(const std::filesystem::path& packageContentPath,
        const ScriptPackageContent& content, DeterministicServerScriptRuntime& runtime) noexcept
    try
    {
        if (content.modules.size() != content.packages.size())
            return ExecutableScriptModuleError::Malformed;
        ExecutableScriptModules result;
        struct Registration
        {
            ServerScriptPackage package;
            std::uint32_t order;
            ServerScriptEventKind kind;
            std::unique_ptr<ServerScriptCallback> callback;
        };
        std::vector<Registration> registrations;
        for (const auto& binding : content.modules)
        {
            if (binding.executionBudget == 0 || binding.executionBudget > MaximumScriptModuleExecutionBudget
                || std::ranges::find(content.packages, binding.package) == content.packages.end())
                return ExecutableScriptModuleError::InvalidResourceBounds;
            const auto path = packageContentPath.parent_path() / binding.artifact;
            if (!std::filesystem::is_regular_file(path))
                return ExecutableScriptModuleError::Unavailable;
            if (std::filesystem::file_size(path) > MaximumScriptModuleBytes)
                return ExecutableScriptModuleError::TooLarge;
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                return ExecutableScriptModuleError::Unavailable;
            std::vector<std::byte> bytes;
            bytes.reserve(MaximumScriptModuleBytes + 1);
            char value = 0;
            while (stream.get(value))
            {
                if (static_cast<unsigned char>(value) > 0x7f)
                    return ExecutableScriptModuleError::Malformed;
                bytes.push_back(static_cast<std::byte>(value));
                if (bytes.size() > MaximumScriptModuleBytes)
                    return ExecutableScriptModuleError::TooLarge;
            }
            if (!stream.eof())
                return ExecutableScriptModuleError::Unavailable;
            if (sha256(bytes) != binding.sha256)
                return ExecutableScriptModuleError::HashMismatch;
            const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            auto parsed = parseModule(text, binding, content.stateCatalog);
            const auto* module = std::get_if<ParsedModule>(&parsed);
            if (!module)
                return std::get<ExecutableScriptModuleError>(parsed);
            if (module->callbacks.size() > MaximumServerScriptCallbacks - registrations.size())
                return ExecutableScriptModuleError::InvalidResourceBounds;
            for (const auto& callback : module->callbacks)
                registrations.push_back({ binding.package, callback.order, callback.eventKind,
                    std::make_unique<ModuleCallback>(
                        binding.package.packageId(), binding.executionBudget, callback.instructions) });
        }
        std::ranges::sort(registrations, [](const auto& left, const auto& right) {
            return std::tuple(left.package.loadOrder(), left.package.packageId(), left.order,
                       static_cast<std::uint8_t>(left.kind))
                < std::tuple(right.package.loadOrder(), right.package.packageId(), right.order,
                    static_cast<std::uint8_t>(right.kind));
        });
        for (auto& registration : registrations)
        {
            if (runtime.registerCallback(
                    registration.package, registration.order, registration.kind, *registration.callback)
                != ServerScriptRegistrationResult::Accepted)
                return ExecutableScriptModuleError::RegistrationFailed;
            result.mCallbacks.push_back(std::move(registration.callback));
        }
        return result;
    }
    catch (...)
    {
        return ExecutableScriptModuleError::Unavailable;
    }
}
