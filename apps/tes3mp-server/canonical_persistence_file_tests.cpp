#include "canonical_persistence_file.hpp"

#include <tes3mp/observability.hpp>
#include <tes3mp/server_command_reducer.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::ServerApp;

    template <class T>
    T id(std::uint64_t value)
    {
        return T::fromValue(value).value();
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            mPath = std::filesystem::temp_directory_path() / ("tes3mp-persistence-" + std::to_string(suffix));
            std::filesystem::create_directory(mPath);
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

    CanonicalPersistenceIdentity identity(std::uint64_t configuration = 1)
    {
        std::array<std::byte, 32> configurationBytes{};
        configurationBytes[0] = static_cast<std::byte>(configuration);
        const std::array seeds{ PersistenceSeed{ 1, { 4, 3, 2, 1 } } };
        return CanonicalPersistenceIdentity::create(
            testContentManifestId(), ServerConfigurationId::fromBytes(configurationBytes).value(), {}, seeds)
            .value();
    }

    CanonicalPlayerEntityState player()
    {
        const auto zero = Turn32::fromValue(0);
        return CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(2), id<AppearanceId>(3),
            Transform(CellId::interior(id<CellSpaceId>(1)), Position3(10, 20, 30), Orientation3(zero, zero, zero)),
            LinearVelocity3(0, 0, 0), id<EntityRevision>(1), id<AuthorityEpoch>(1), ServerTick::initial());
    }

    bool commitJoin(CanonicalPersistenceFile& file)
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        auto empty = std::get<CanonicalServerState>(createCanonicalServerState({}, {}));
        CanonicalCommandReducer reducer(std::move(empty), observability, testContentManifest());
        if (!reducer.configureDurability(file))
            return false;
        CanonicalSessionProgress session(
            id<SessionId>(1), id<SessionGeneration>(1), id<PlayerId>(1), id<EntityId>(2), std::nullopt);
        auto prepared = reducer.prepareJoin(player(), session, id<ServerTick>(1));
        return prepared && reducer.commit(std::move(*prepared));
    }

    bool file_round_trip_restores_one_verified_session_independent_prefix()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file || !commitJoin(**file) || !std::filesystem::exists(path))
            return false;
        file->reset();
        auto reopened = CanonicalPersistenceFile::open(path, identity());
        auto* restoredFile = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&reopened);
        if (!restoredFile)
            return false;
        const auto restored = (*restoredFile)->restoredState();
        return restored && restored->players().size() == 1 && restored->activeSessions().empty()
            && restored->players().front() == player()
            && (*restoredFile)->restoredStateVersion() == id<CanonicalStateVersion>(1)
            && (*restoredFile)->restoredCanonicalRevision() == id<CanonicalRevision>(1)
            && (*restoredFile)->restoredCheckpointTick() == id<ServerTick>(1)
            && (*restoredFile)->prefix().transactions().size() == 1;
    }

    std::vector<char> read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    void write(const std::filesystem::path& path, std::span<const char> bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    bool corruption_truncation_and_identity_mismatch_reject_without_restore()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file || !commitJoin(**file))
            return false;
        file->reset();
        const auto valid = read(path);
        auto corrupted = valid;
        corrupted[corrupted.size() / 2] ^= 0x20;
        write(path, corrupted);
        auto corruptResult = CanonicalPersistenceFile::open(path, identity());
        if (!std::holds_alternative<CanonicalPersistenceFileError>(corruptResult))
            return false;
        write(path, std::span(valid).first(valid.size() - 3));
        auto truncatedResult = CanonicalPersistenceFile::open(path, identity());
        if (!std::holds_alternative<CanonicalPersistenceFileError>(truncatedResult))
            return false;
        write(path, valid);
        auto mismatchResult = CanonicalPersistenceFile::open(path, identity(2));
        return std::get<CanonicalPersistenceFileError>(mismatchResult)
            == CanonicalPersistenceFileError::IdentityMismatch;
    }

    bool stale_uncommitted_temporary_file_never_replaces_the_committed_prefix()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file || !commitJoin(**file))
            return false;
        file->reset();
        auto temporary = path;
        temporary += ".tmp";
        const std::array junk{ 'u', 'n', 'c', 'o', 'm', 'm', 'i', 't', 't', 'e', 'd' };
        write(temporary, junk);
        auto reopened = CanonicalPersistenceFile::open(path, identity());
        auto* restored = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&reopened);
        return restored && (*restored)->prefix().transactions().size() == 1 && !std::filesystem::exists(temporary);
    }
}

int main()
{
    const std::array tests{
        std::pair{ "file_round_trip_restores_one_verified_session_independent_prefix",
            &file_round_trip_restores_one_verified_session_independent_prefix },
        std::pair{ "corruption_truncation_and_identity_mismatch_reject_without_restore",
            &corruption_truncation_and_identity_mismatch_reject_without_restore },
        std::pair{ "stale_uncommitted_temporary_file_never_replaces_the_committed_prefix",
            &stale_uncommitted_temporary_file_never_replaces_the_committed_prefix },
    };
    for (const auto& [name, test] : tests)
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    return 0;
}
