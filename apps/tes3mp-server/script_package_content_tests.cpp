#include "canonical_persistence_file.hpp"
#include "script_package_content.hpp"

#include <tes3mp/canonical_persistence.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <variant>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::ServerApp;

    template <class Value>
    Value id(std::uint64_t value)
    {
        return Value::fromValue(value).value();
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            mPath = std::filesystem::temp_directory_path() / ("tes3mp-script-packages-" + std::to_string(suffix));
            std::filesystem::create_directories(mPath);
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(mPath, ignored);
        }
        const std::filesystem::path& path() const noexcept { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    ContentManifest manifest()
    {
        const auto manifestId
            = ContentManifestId::fromHex("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
        const std::array spaces{ CellSpaceDeclaration{ id<CellSpaceId>(1), CellSpaceKind::Interior } };
        const std::array cells{ CellId::interior(id<CellSpaceId>(1)) };
        const auto movement = MovementProfile::create(1, 2, 3, 4);
        return ContentManifest::create(*manifestId, spaces, cells, id<AppearanceId>(2), *movement).value();
    }

    std::string packageText(std::uint32_t firstVersion = 1, std::string_view firstInitial = "7")
    {
        return "TES3MP_SCRIPT_PACKAGES_V1\n"
               "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
               "package 22 1 20 3\n"
               "package 11 "
            + std::to_string(firstVersion) + " 10 3\n"
              "variable 22 4 string_hex 68656c6c6f\n"
              "variable 11 2 boolean true\n"
              "variable 11 1 integer "
            + std::string(firstInitial) + "\n"
              "variable 22 3 float 1.5\n";
    }

    void write(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    void write(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    bool deterministic_load_order_and_typed_catalog()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(path, packageText());
        const auto expectedManifest = manifest();
        auto loaded = loadScriptPackageContent(path, expectedManifest);
        const auto* content = std::get_if<ScriptPackageContent>(&loaded);
        if (!content || content->packages.size() != 2 || content->stateCatalog.entries().size() != 4)
            return false;
        DeterministicServerScriptRuntime runtime;
        const auto state = CanonicalScriptState::initial(content->stateCatalog);
        if (!state || !runtime.configurePackages(content->packages, content->stateCatalog)
            || !runtime.bindPersistentState(*state))
            return false;
        const auto& entries = content->stateCatalog.entries();
        return runtime.packages().size() == 2 && content->packages[0].packageId() == 11
            && content->packages[1].packageId() == 22 && entries[0].packageId == 11
            && entries[0].id == id<ScriptVariableId>(1) && std::get<std::int64_t>(entries[0].initialValue) == 7
            && entries[1].id == id<ScriptVariableId>(2) && std::get<bool>(entries[1].initialValue)
            && entries[2].packageId == 22 && entries[2].id == id<ScriptVariableId>(3)
            && std::get<double>(entries[2].initialValue) == 1.5
            && std::get<std::string>(entries[3].initialValue) == "hello";
    }

    CanonicalPersistenceIdentity identity(const ScriptPackageContent& content)
    {
        std::array<std::byte, 32> configuration{};
        configuration[0] = std::byte{ 1 };
        return CanonicalPersistenceIdentity::create(manifest().id(),
            ServerConfigurationId::fromBytes(configuration).value(), content.packages, content.stateCatalog, {})
            .value();
    }

    bool restart_restores_state_and_upgrade_or_catalog_mismatch_rejects()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(path, packageText());
        auto loaded = loadScriptPackageContent(path, manifest());
        auto* content = std::get_if<ScriptPackageContent>(&loaded);
        if (!content)
            return false;
        auto state = CanonicalScriptState::initial(content->stateCatalog).value();
        auto changed = compareAndSetCanonicalScriptVariable(state, content->stateCatalog, 11, id<ScriptVariableId>(1),
            ScriptStateRevision::initial(), std::int64_t{ 19 }, id<ServerTick>(1));
        auto* restoredState = std::get_if<CanonicalScriptState>(&changed);
        if (!restoredState)
            return false;
        const auto tick
            = CanonicalDurableTick::create(id<CanonicalStateVersion>(1), id<CanonicalRevision>(1), id<ServerTick>(1),
                {}, {}, CanonicalChecksum(0), nullptr, nullptr, nullptr, nullptr, nullptr, restoredState);
        const auto prefix = tick ? CanonicalDurablePrefix::create(identity(*content), { *tick }) : std::nullopt;
        if (!prefix)
            return false;
        const auto bytes = encodeCanonicalDurablePrefixV2(*prefix);
        const auto persistencePath = directory.path() / "world-v2.t3p";
        write(persistencePath, bytes);
        auto reopened = CanonicalPersistenceFile::open(persistencePath, identity(*content));
        auto* same = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&reopened);
        if (!same || !*same || !(*same)->restoredScriptState())
            return false;
        const auto* value = (*same)->restoredScriptState()->find(11, id<ScriptVariableId>(1));
        if (!value || std::get<std::int64_t>(value->value) != 19 || value->revision.value() != 2)
            return false;

        write(path, packageText(2));
        auto upgraded = loadScriptPackageContent(path, manifest());
        const auto* upgradedContent = std::get_if<ScriptPackageContent>(&upgraded);
        if (!upgradedContent)
            return false;
        auto upgradeResult = CanonicalPersistenceFile::open(persistencePath, identity(*upgradedContent));
        write(path, packageText(1, "8"));
        auto changedCatalog = loadScriptPackageContent(path, manifest());
        const auto* changedContent = std::get_if<ScriptPackageContent>(&changedCatalog);
        if (!changedContent)
            return false;
        auto catalogResult = CanonicalPersistenceFile::open(persistencePath, identity(*changedContent));
        return std::get_if<CanonicalPersistenceFileError>(&upgradeResult)
            && std::get<CanonicalPersistenceFileError>(upgradeResult) == CanonicalPersistenceFileError::IdentityMismatch
            && std::get_if<CanonicalPersistenceFileError>(&catalogResult)
            && std::get<CanonicalPersistenceFileError>(catalogResult)
            == CanonicalPersistenceFileError::IdentityMismatch;
    }

    bool malformed_bounds_and_manifest_mismatch_fail_closed()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(path,
            "TES3MP_SCRIPT_PACKAGES_V1\nmanifest "
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\npackage 1 1 0 3\n");
        auto mismatched = loadScriptPackageContent(path, manifest());
        if (!std::get_if<ScriptPackageContentError>(&mismatched)
            || std::get<ScriptPackageContentError>(mismatched) != ScriptPackageContentError::ManifestMismatch)
            return false;
        write(path,
            "TES3MP_SCRIPT_PACKAGES_V1\nmanifest "
            "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
            "package 1 1 0 4\n");
        auto wrongApi = loadScriptPackageContent(path, manifest());
        if (std::get<ScriptPackageContentError>(wrongApi) != ScriptPackageContentError::Malformed)
            return false;
        write(path,
            "TES3MP_SCRIPT_PACKAGES_V1\nmanifest "
            "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
            "package 1 1 0 3\nvariable 2 1 integer 0\n");
        auto unknownPackage = loadScriptPackageContent(path, manifest());
        if (std::get<ScriptPackageContentError>(unknownPackage) != ScriptPackageContentError::InvalidCatalog)
            return false;
        std::string tooMany
            = "TES3MP_SCRIPT_PACKAGES_V1\nmanifest "
              "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n";
        for (std::uint64_t value = 1; value <= MaximumServerScriptPackages + 1; ++value)
            tooMany += "package " + std::to_string(value) + " 1 " + std::to_string(value) + " 3\n";
        write(path, tooMany);
        auto oversized = loadScriptPackageContent(path, manifest());
        return std::get<ScriptPackageContentError>(oversized) == ScriptPackageContentError::TooLarge;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "deterministic_load_order_and_typed_catalog", &deterministic_load_order_and_typed_catalog },
        std::pair{ "restart_restores_state_and_upgrade_or_catalog_mismatch_rejects",
            &restart_restores_state_and_upgrade_or_catalog_mismatch_rejects },
        std::pair{
            "malformed_bounds_and_manifest_mismatch_fail_closed", &malformed_bounds_and_manifest_mismatch_fail_closed },
    };
    bool passed = true;
    for (const auto& [name, test] : tests)
    {
        const bool result = test();
        std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
        passed = passed && result;
    }
    return passed ? 0 : 1;
}
