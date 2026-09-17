#include "canonical_persistence_file.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::ServerApp;

    CanonicalPersistenceFileError mapError(CanonicalPersistenceDecodeError error) noexcept
    {
        switch (error)
        {
            case CanonicalPersistenceDecodeError::Truncated:
                return CanonicalPersistenceFileError::Truncated;
            case CanonicalPersistenceDecodeError::TooLarge:
                return CanonicalPersistenceFileError::TooLarge;
            case CanonicalPersistenceDecodeError::UnsupportedVersion:
                return CanonicalPersistenceFileError::UnsupportedVersion;
            case CanonicalPersistenceDecodeError::Corrupted:
                return CanonicalPersistenceFileError::Corrupted;
            case CanonicalPersistenceDecodeError::Malformed:
                return CanonicalPersistenceFileError::Malformed;
            case CanonicalPersistenceDecodeError::IdentityMismatch:
                return CanonicalPersistenceFileError::IdentityMismatch;
        }
        return CanonicalPersistenceFileError::Malformed;
    }

    bool flushFile(const std::filesystem::path& path) noexcept
    {
#ifdef _WIN32
        const HANDLE handle = CreateFileW(
            path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;
        const bool result = FlushFileBuffers(handle) != 0;
        CloseHandle(handle);
        return result;
#else
        const int descriptor = ::open(path.c_str(), O_WRONLY);
        if (descriptor < 0)
            return false;
        const bool result = ::fsync(descriptor) == 0;
        ::close(descriptor);
        return result;
#endif
    }

    bool replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept
    {
#ifdef _WIN32
        constexpr unsigned MaximumAttempts = 6;
        for (unsigned attempt = 0; attempt < MaximumAttempts; ++attempt)
        {
            if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
                return true;
            const auto error = GetLastError();
            if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
                return false;
            if (attempt + 1 < MaximumAttempts)
                Sleep(1u << attempt);
        }
        return false;
#else
        if (std::rename(temporary.c_str(), target.c_str()) != 0)
            return false;
        const auto parent = target.parent_path().empty() ? std::filesystem::path(".") : target.parent_path();
        const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0)
            return false;
        const bool result = ::fsync(directory) == 0;
        ::close(directory);
        return result;
#endif
    }

    bool replaceDurably(const std::filesystem::path& target, std::span<const std::byte> bytes) noexcept
    try
    {
        auto temporary = target;
        temporary += ".tmp";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                return false;
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            stream.flush();
            if (!stream)
                return false;
        }
        if (!flushFile(temporary) || !replaceFile(temporary, target))
        {
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }

}

namespace TES3MP::ServerApp
{
    std::variant<std::unique_ptr<CanonicalPersistenceFile>, CanonicalPersistenceFileError>
    CanonicalPersistenceFile::open(std::filesystem::path path, CanonicalPersistenceIdentity identity) noexcept
    try
    {
        if (path.empty())
            return CanonicalPersistenceFileError::Unavailable;
        auto empty = CanonicalDurablePrefix::create(identity, {});
        if (!empty)
            return CanonicalPersistenceFileError::Malformed;
        auto temporary = path;
        temporary += ".tmp";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        if (!std::filesystem::exists(path))
            return std::unique_ptr<CanonicalPersistenceFile>(
                new CanonicalPersistenceFile(std::move(path), std::move(*empty)));
        if (!std::filesystem::is_regular_file(path))
            return CanonicalPersistenceFileError::Unavailable;
        const auto size = std::filesystem::file_size(path);
        if (size > MaximumPersistenceFileBytes)
            return CanonicalPersistenceFileError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return CanonicalPersistenceFileError::Unavailable;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        if (!bytes.empty())
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream || stream.peek() != std::ifstream::traits_type::eof())
            return CanonicalPersistenceFileError::Unavailable;
        auto decoded = decodeCanonicalDurablePrefix(bytes, identity);
        if (const auto* error = std::get_if<CanonicalPersistenceDecodeError>(&decoded))
            return mapError(*error);
        return std::unique_ptr<CanonicalPersistenceFile>(
            new CanonicalPersistenceFile(std::move(path), std::get<CanonicalDurablePrefix>(std::move(decoded))));
    }
    catch (...)
    {
        return CanonicalPersistenceFileError::Unavailable;
    }

    std::optional<CanonicalServerState> CanonicalPersistenceFile::restoredState() const noexcept
    try
    {
        const auto* latest = mPrefix.latest();
        if (!latest)
            return std::nullopt;
        auto state = createCanonicalServerState(latest->players(), {});
        const auto* value = std::get_if<CanonicalServerState>(&state);
        return value ? std::optional<CanonicalServerState>(*value) : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalStateVersion> CanonicalPersistenceFile::restoredStateVersion() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest ? std::optional(latest->stateVersion()) : std::nullopt;
    }

    std::optional<CanonicalRevision> CanonicalPersistenceFile::restoredCanonicalRevision() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest ? std::optional(latest->canonicalRevision()) : std::nullopt;
    }

    std::optional<ServerTick> CanonicalPersistenceFile::restoredCheckpointTick() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest ? std::optional(latest->checkpointTick()) : std::nullopt;
    }

    const CanonicalDurableInventoryState* CanonicalPersistenceFile::restoredInventory() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest && latest->inventory() ? &*latest->inventory() : nullptr;
    }

    const CanonicalDurableCombatState* CanonicalPersistenceFile::restoredCombat() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest && latest->combat() ? &*latest->combat() : nullptr;
    }

    const CanonicalDurableInteractiveObjectState* CanonicalPersistenceFile::restoredObjects() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest && latest->objects() ? &*latest->objects() : nullptr;
    }

    const CanonicalDurableActorState* CanonicalPersistenceFile::restoredActors() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest && latest->actors() ? &*latest->actors() : nullptr;
    }

    const CanonicalWorldState* CanonicalPersistenceFile::restoredWorld() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest && latest->world() ? &*latest->world() : nullptr;
    }

    const CanonicalScriptState* CanonicalPersistenceFile::restoredScriptState() const noexcept
    {
        const auto* latest = mPrefix.latest();
        return latest && latest->scriptState() ? &*latest->scriptState() : nullptr;
    }

    bool CanonicalPersistenceFile::bindPlayerIdentities(const PlayerIdentityRegistry& identities) noexcept
    {
        if (mPlayerIdentities)
            return false;
        mPlayerIdentities = &identities;
        return true;
    }

    CanonicalDurabilityResult CanonicalPersistenceFile::commit(
        const std::shared_ptr<const CanonicalStatePublication>& candidate, CanonicalRevision canonicalRevision,
        std::span<const DurableCommandOrder> commands, const CanonicalInventoryWorld* inventory,
        const CanonicalCombatWorld* combat, const CanonicalInteractiveObjectWorld* objects,
        const CanonicalActorWorld* actors, const CanonicalWorldState* world,
        const CanonicalScriptState* scriptState, std::span<const std::byte> nativeInventory) noexcept
    try
    {
        if (!candidate)
            return CanonicalDurabilityResult::Rejected;
        if (nativeInventory.size() > MaximumNativeInventoryImageBytes || (inventory && !nativeInventory.empty())
            || (mPrefix.latest() && !mPrefix.latest()->nativeInventory().empty() && nativeInventory.empty())
            || (mPrefix.latest() && mPrefix.latest()->inventory() && !nativeInventory.empty()))
            return CanonicalDurabilityResult::Rejected;
        if (!scriptState
            || !CanonicalScriptState::restore(mPrefix.identity().scriptStateCatalog(), scriptState->variables()))
            return CanonicalDurabilityResult::Rejected;
        const auto prior = mPrefix.latest() ? mPrefix.latest()->transactionChecksum() : CanonicalChecksum(0);
        std::vector<CanonicalPlayerEntityState> durablePlayers;
        durablePlayers.reserve(candidate->state().players().size());
        for (const auto& player : candidate->state().players())
        {
            const auto* profile = mPlayerIdentities ? mPlayerIdentities->characterProfile(player.playerId()) : nullptr;
            if (!mPlayerIdentities || (profile && profile->lifecycle() == CharacterLifecycle::EstablishedCharacter))
                durablePlayers.push_back(player);
        }
        if (mPlayerIdentities && mPrefix.latest())
            for (const auto& player : mPrefix.latest()->players())
            {
                const auto* profile = mPlayerIdentities->characterProfile(player.playerId());
                if (profile && profile->lifecycle() == CharacterLifecycle::EstablishedCharacter
                    && std::ranges::none_of(
                        durablePlayers, [&](const auto& value) { return value.playerId() == player.playerId(); }))
                    durablePlayers.push_back(player);
            }
        std::ranges::sort(durablePlayers, {}, &CanonicalPlayerEntityState::playerId);
        auto transaction
            = CanonicalDurableTick::create(candidate->stateVersion(), canonicalRevision, candidate->checkpointTick(),
                durablePlayers, commands, prior, inventory, combat, objects, actors, world, scriptState, nativeInventory);
        if (!transaction)
            return CanonicalDurabilityResult::Rejected;
        std::vector<CanonicalDurableTick> transactions(mPrefix.transactions().begin(), mPrefix.transactions().end());
        transactions.push_back(std::move(*transaction));
        if (transactions.size() > MaximumPersistenceTransactions)
        {
            const auto& previous = transactions[transactions.size() - 2];
            auto checkpoint = CanonicalDurableTick::create(previous.stateVersion(), previous.canonicalRevision(),
                previous.checkpointTick(), previous.players(), {}, CanonicalChecksum(0), previous.inventory(),
                previous.combat(), previous.objects(), previous.actors(), previous.world(), previous.scriptState(), previous.nativeInventory());
            auto newest = CanonicalDurableTick::create(candidate->stateVersion(), canonicalRevision,
                candidate->checkpointTick(), durablePlayers, commands,
                checkpoint ? checkpoint->transactionChecksum() : CanonicalChecksum(0), inventory, combat, objects,
                actors, world, scriptState, nativeInventory);
            if (!checkpoint || !newest)
                return CanonicalDurabilityResult::Rejected;
            transactions.clear();
            transactions.push_back(std::move(*checkpoint));
            transactions.push_back(std::move(*newest));
        }
        auto next = CanonicalDurablePrefix::create(mPrefix.identity(), std::move(transactions));
        if (!next)
            return CanonicalDurabilityResult::Rejected;
        auto bytes = encodeCanonicalDurablePrefixV2(*next);
        if (bytes.size() > MaximumPersistenceFileBytes || !replaceDurably(mPath, bytes))
            return CanonicalDurabilityResult::Failed;
        mPrefix = std::move(*next);
        return CanonicalDurabilityResult::Committed;
    }
    catch (...)
    {
        return CanonicalDurabilityResult::Failed;
    }
}
