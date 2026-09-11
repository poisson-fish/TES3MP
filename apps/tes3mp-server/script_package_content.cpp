#include "script_package_content.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_SCRIPT_PACKAGES_V2";
        constexpr std::size_t MaximumFields = 10;

        template <class Value>
        std::optional<Value> number(std::string_view text) noexcept
        {
            Value value{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        std::optional<std::size_t> tokenize(
            std::string_view line, std::array<std::string_view, MaximumFields>& output) noexcept
        {
            std::size_t count = 0;
            for (std::size_t begin = 0; begin < line.size();)
            {
                while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t'))
                    ++begin;
                if (begin == line.size())
                    break;
                if (count == output.size())
                    return std::nullopt;
                auto end = begin;
                while (end < line.size() && line[end] != ' ' && line[end] != '\t')
                    ++end;
                output[count++] = line.substr(begin, end - begin);
                begin = end;
            }
            return count;
        }

        std::optional<std::string> hexString(std::string_view text) noexcept
        try
        {
            if (text == "-")
                return std::string{};
            if (text.empty() || text.size() % 2 != 0 || text.size() / 2 > MaximumScriptStringBytes)
                return std::nullopt;
            const auto nibble = [](char value) -> std::optional<unsigned char> {
                if (value >= '0' && value <= '9')
                    return static_cast<unsigned char>(value - '0');
                if (value >= 'a' && value <= 'f')
                    return static_cast<unsigned char>(value - 'a' + 10);
                return std::nullopt;
            };
            std::string result;
            result.reserve(text.size() / 2);
            for (std::size_t index = 0; index < text.size(); index += 2)
            {
                const auto high = nibble(text[index]);
                const auto low = nibble(text[index + 1]);
                if (!high || !low)
                    return std::nullopt;
                result.push_back(static_cast<char>((*high << 4) | *low));
            }
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }

        std::optional<std::array<std::byte, ScriptModuleHashBytes>> moduleHash(std::string_view text) noexcept
        {
            if (text.size() != ScriptModuleHashBytes * 2)
                return std::nullopt;
            const auto nibble = [](char value) -> std::optional<unsigned char> {
                if (value >= '0' && value <= '9')
                    return static_cast<unsigned char>(value - '0');
                if (value >= 'a' && value <= 'f')
                    return static_cast<unsigned char>(value - 'a' + 10);
                return std::nullopt;
            };
            std::array<std::byte, ScriptModuleHashBytes> result{};
            for (std::size_t index = 0; index < result.size(); ++index)
            {
                const auto high = nibble(text[index * 2]);
                const auto low = nibble(text[index * 2 + 1]);
                if (!high || !low)
                    return std::nullopt;
                result[index] = static_cast<std::byte>((*high << 4) | *low);
            }
            return result;
        }

        bool identifier(std::string_view value, std::size_t maximum) noexcept
        {
            if (value.empty() || value.size() > maximum)
                return false;
            return std::ranges::all_of(value, [](char byte) {
                return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')
                    || byte == '_' || byte == '-';
            });
        }

        bool artifactName(std::string_view value) noexcept
        {
            if (value.empty() || value.size() > MaximumScriptModuleNameBytes || value.front() == '.')
                return false;
            return std::ranges::all_of(value, [](char byte) {
                return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')
                    || byte == '_' || byte == '-' || byte == '.';
            });
        }
    }

    ScriptPackageContentLoadResult loadScriptPackageContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path))
            return ScriptPackageContentError::Unavailable;
        if (std::filesystem::file_size(path) > MaximumScriptPackageContentBytes)
            return ScriptPackageContentError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return ScriptPackageContentError::Unavailable;
        std::string text;
        text.reserve(MaximumScriptPackageContentBytes + 1);
        char byte = 0;
        while (stream.get(byte))
        {
            if (static_cast<unsigned char>(byte) > 0x7f)
                return ScriptPackageContentError::Malformed;
            text.push_back(byte);
            if (text.size() > MaximumScriptPackageContentBytes)
                return ScriptPackageContentError::TooLarge;
        }
        if (!stream.eof())
            return ScriptPackageContentError::Unavailable;

        std::optional<ContentManifestId> declaredManifest;
        std::vector<ServerScriptPackage> packages;
        std::vector<ScriptModuleBinding> modules;
        std::vector<ServerScriptVariableCatalogEntry> variables;
        std::size_t lineNumber = 0;
        for (std::size_t begin = 0; begin <= text.size();)
        {
            ++lineNumber;
            const auto lineEnd = text.find('\n', begin);
            const auto length = (lineEnd == std::string::npos ? text.size() : lineEnd) - begin;
            auto line = std::string_view(text).substr(begin, length);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (lineNumber == 1)
            {
                if (line != Header)
                    return ScriptPackageContentError::Malformed;
            }
            else if (!line.empty() && line.front() != '#')
            {
                std::array<std::string_view, MaximumFields> fields{};
                const auto count = tokenize(line, fields);
                if (!count || *count == 0)
                    return ScriptPackageContentError::Malformed;
                const auto values = std::span(fields).first(*count);
                if (values[0] == "manifest")
                {
                    if (values.size() != 2 || declaredManifest)
                        return ScriptPackageContentError::Malformed;
                    declaredManifest = ContentManifestId::fromHex(values[1]);
                    if (!declaredManifest)
                        return ScriptPackageContentError::Malformed;
                }
                else if (values[0] == "package")
                {
                    if (values.size() != 10 || packages.size() == MaximumServerScriptPackages)
                        return packages.size() == MaximumServerScriptPackages ? ScriptPackageContentError::TooLarge
                                                                              : ScriptPackageContentError::Malformed;
                    const auto packageId = number<std::uint64_t>(values[1]);
                    const auto version = number<std::uint32_t>(values[2]);
                    const auto loadOrder = number<std::uint32_t>(values[3]);
                    const auto api = number<std::uint32_t>(values[4]);
                    const auto abi = number<std::uint32_t>(values[5]);
                    const auto hash = moduleHash(values[7]);
                    const auto budget = number<std::uint32_t>(values[9]);
                    const auto package = packageId && version && loadOrder && api
                        ? ServerScriptPackage::create(*packageId, *version, *loadOrder, *api)
                        : std::nullopt;
                    const std::filesystem::path artifact = std::filesystem::u8path(values[6]);
                    if (!package || !abi || *abi != ServerScriptModuleAbiVersion || !hash || !budget || *budget == 0
                        || *budget > MaximumScriptModuleExecutionBudget || !artifactName(values[6])
                        || artifact.filename() != artifact || !identifier(values[8], MaximumScriptEntrypointBytes)
                        || std::ranges::any_of(
                            packages, [&](const auto value) { return value.packageId() == *packageId; }))
                        return ScriptPackageContentError::Malformed;
                    packages.push_back(*package);
                    modules.push_back({ *package, artifact, *hash, std::string(values[8]), *budget });
                }
                else if (values[0] == "variable")
                {
                    if (values.size() != 5 || variables.size() == MaximumScriptVariables)
                        return variables.size() == MaximumScriptVariables ? ScriptPackageContentError::TooLarge
                                                                          : ScriptPackageContentError::Malformed;
                    const auto packageId = number<std::uint64_t>(values[1]);
                    const auto rawId = number<std::uint64_t>(values[2]);
                    const auto id = rawId ? ScriptVariableId::fromValue(*rawId) : std::nullopt;
                    if (!packageId || *packageId == 0 || !id)
                        return ScriptPackageContentError::Malformed;
                    ScriptVariableValue value;
                    if (values[3] == "boolean")
                    {
                        if (values[4] == "true")
                            value = true;
                        else if (values[4] == "false")
                            value = false;
                        else
                            return ScriptPackageContentError::Malformed;
                    }
                    else if (values[3] == "integer")
                    {
                        const auto parsed = number<std::int64_t>(values[4]);
                        if (!parsed)
                            return ScriptPackageContentError::Malformed;
                        value = *parsed;
                    }
                    else if (values[3] == "float")
                    {
                        const auto parsed = number<double>(values[4]);
                        if (!parsed || !std::isfinite(*parsed))
                            return ScriptPackageContentError::Malformed;
                        value = *parsed;
                    }
                    else if (values[3] == "string_hex")
                    {
                        auto parsed = hexString(values[4]);
                        if (!parsed)
                            return ScriptPackageContentError::Malformed;
                        value = std::move(*parsed);
                    }
                    else
                        return ScriptPackageContentError::Malformed;
                    variables.push_back({ *packageId, *id, std::move(value) });
                }
                else
                    return ScriptPackageContentError::Malformed;
            }
            if (lineEnd == std::string::npos)
                break;
            begin = lineEnd + 1;
        }
        if (!declaredManifest)
            return ScriptPackageContentError::Malformed;
        if (*declaredManifest != manifest.id())
            return ScriptPackageContentError::ManifestMismatch;
        std::ranges::sort(packages, [](const auto left, const auto right) {
            return std::tuple(left.loadOrder(), left.packageId(), left.packageVersion(), left.apiVersion())
                < std::tuple(right.loadOrder(), right.packageId(), right.packageVersion(), right.apiVersion());
        });
        std::ranges::sort(modules, [](const auto& left, const auto& right) {
            return std::tuple(left.package.loadOrder(), left.package.packageId(), left.package.packageVersion(),
                       left.package.apiVersion())
                < std::tuple(right.package.loadOrder(), right.package.packageId(), right.package.packageVersion(),
                    right.package.apiVersion());
        });
        std::ranges::sort(variables, {}, [](const auto& value) { return std::pair(value.packageId, value.id); });
        if (std::ranges::any_of(variables, [&](const auto& variable) {
                return std::ranges::none_of(
                    packages, [&](const auto package) { return package.packageId() == variable.packageId; });
            }))
            return ScriptPackageContentError::InvalidCatalog;
        auto catalog = ServerScriptStateCatalog::create(variables);
        if (!catalog)
            return ScriptPackageContentError::InvalidCatalog;
        return ScriptPackageContent{ std::move(packages), std::move(modules), std::move(*catalog) };
    }
    catch (...)
    {
        return ScriptPackageContentError::Unavailable;
    }
}
