#include <tes3mp/canonical_persistence.hpp>
#include <tes3mp/server_command_reducer.hpp>

#include <array>
#include <iostream>
#include <memory>
#include <variant>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return T::fromValue(value).value();
    }

    CanonicalPlayerEntityState player(std::int64_t position, std::uint64_t revision = 1)
    {
        const auto zero = Turn32::fromValue(0);
        return CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(10), id<AppearanceId>(20),
            Transform(CellId::interior(id<CellSpaceId>(30)), Position3(position, 2, 3), Orientation3(zero, zero, zero)),
            LinearVelocity3(0, 0, 0), id<EntityRevision>(revision), id<AuthorityEpoch>(1), id<ServerTick>(revision),
            LocomotionMode::Walk);
    }

    CanonicalPersistenceIdentity identity(std::uint64_t configuration = 1, std::uint32_t packageVersion = 1)
    {
        std::array<std::byte, 32> configurationBytes{};
        configurationBytes[0] = static_cast<std::byte>(configuration);
        const auto package = ServerScriptPackage::create(11, packageVersion, 0).value();
        const std::array scripts{ package };
        const std::array seeds{ PersistenceSeed{ 7, { 1, 2, 3, 4 } } };
        return CanonicalPersistenceIdentity::create(
            testContentManifestId(), ServerConfigurationId::fromBytes(configurationBytes).value(), scripts, seeds)
            .value();
    }

    DurableCommandOrder client(std::uint64_t tick, std::uint64_t ingress)
    {
        return { DurableCommandSource::Client, { tick, ingress, 1, 1, ingress, 100 + ingress, 0, 0, 0 }, 0 };
    }

    DurableCommandOrder script(std::uint64_t tick, std::uint64_t ordinal)
    {
        return { DurableCommandSource::Script, { tick, 1, 0, 0, 11, 1, ServerScriptApiVersion, 0, ordinal }, 0 };
    }

    CanonicalDurablePrefix prefix()
    {
        const std::array firstPlayers{ player(10) };
        const std::array firstCommands{ client(1, 1), script(1, 0) };
        auto first = CanonicalDurableTick::create(
            id<CanonicalStateVersion>(1), id<CanonicalRevision>(1), id<ServerTick>(1), firstPlayers, firstCommands)
                         .value();
        const std::array secondPlayers{ player(20, 2) };
        const std::array secondCommands{ client(2, 2) };
        auto second = CanonicalDurableTick::create(id<CanonicalStateVersion>(2), id<CanonicalRevision>(2),
            id<ServerTick>(2), secondPlayers, secondCommands, first.transactionChecksum())
                          .value();
        return CanonicalDurablePrefix::create(identity(), { std::move(first), std::move(second) }).value();
    }

    bool round_trip_preserves_identity_roots_versions_seeds_and_order()
    {
        const auto original = prefix();
        const auto bytes = encodeCanonicalDurablePrefixV1(original);
        const auto decoded = decodeCanonicalDurablePrefixV1(bytes, identity());
        const auto* restored = std::get_if<CanonicalDurablePrefix>(&decoded);
        return restored && *restored == original && restored->latest()->players().front() == player(20, 2)
            && restored->latest()->stateVersion().value() == 2 && restored->latest()->canonicalRevision().value() == 2
            && restored->latest()->checkpointTick().value() == 2
            && restored->transactions().front().commands().size() == 2
            && restored->transactions().front().commands()[1].source == DurableCommandSource::Script
            && restored->identity().seeds().front().words[3] == 4;
    }

    bool malformed_inputs_and_identity_mismatches_reject_atomically()
    {
        auto bytes = encodeCanonicalDurablePrefixV1(prefix());
        auto truncated = bytes;
        truncated.pop_back();
        if (!std::holds_alternative<CanonicalPersistenceDecodeError>(
                decodeCanonicalDurablePrefixV1(truncated, identity())))
            return false;
        auto corrupted = bytes;
        corrupted[corrupted.size() / 2] ^= std::byte{ 0x40 };
        if (std::get<CanonicalPersistenceDecodeError>(decodeCanonicalDurablePrefixV1(corrupted, identity()))
            != CanonicalPersistenceDecodeError::Corrupted)
            return false;
        return std::get<CanonicalPersistenceDecodeError>(decodeCanonicalDurablePrefixV1(bytes, identity(2)))
            == CanonicalPersistenceDecodeError::IdentityMismatch
            && std::get<CanonicalPersistenceDecodeError>(decodeCanonicalDurablePrefixV1(bytes, identity(1, 2)))
            == CanonicalPersistenceDecodeError::IdentityMismatch;
    }

    std::variant<CanonicalServerState, CanonicalChecksum> replayStep(
        const CanonicalServerState&, std::span<const DurableCommandOrder> commands, ServerTick)
    {
        if (commands.empty() || commands.front().source != DurableCommandSource::Client)
            return CanonicalChecksum(0);
        const auto ingress = commands.front().fields[1];
        const std::array players{ ingress == 1 ? player(10) : player(20, 2) };
        auto state = createCanonicalServerState(players, {});
        if (auto* value = std::get_if<CanonicalServerState>(&state))
            return std::move(*value);
        return CanonicalChecksum(0);
    }

    bool replayed_command_stream_reaches_each_recorded_checksum()
    {
        return replayCanonicalDurablePrefix(prefix(), &replayStep);
    }

    class ProbeDurability final : public CanonicalDurabilityPort
    {
    public:
        CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
            CanonicalRevision, std::span<const DurableCommandOrder>) noexcept override
        {
            called = true;
            sawCandidate = candidate && candidate->state().players().size() == 1;
            installedBeforeAcknowledgement = reducer && !reducer->state().players().empty();
            return result;
        }
        CanonicalCommandReducer* reducer = nullptr;
        CanonicalDurabilityResult result = CanonicalDurabilityResult::Rejected;
        bool called = false;
        bool sawCandidate = false;
        bool installedBeforeAcknowledgement = false;
    };

    bool durability_acknowledgement_precedes_installation_and_publication()
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        auto empty = std::get<CanonicalServerState>(createCanonicalServerState({}, {}));
        CanonicalCommandReducer reducer(std::move(empty), observability, testContentManifest());
        ProbeDurability durability;
        durability.reducer = &reducer;
        if (!reducer.configureDurability(durability))
            return false;
        CanonicalSessionProgress session(
            id<SessionId>(1), id<SessionGeneration>(1), id<PlayerId>(1), id<EntityId>(10), std::nullopt);
        auto rejected = reducer.prepareJoin(player(10), session, id<ServerTick>(1));
        const auto before = reducer.latestPublication();
        if (!rejected || reducer.commit(std::move(*rejected)) || !durability.called || !durability.sawCandidate
            || durability.installedBeforeAcknowledgement || reducer.latestPublication() != before
            || !reducer.state().players().empty())
            return false;
        durability.result = CanonicalDurabilityResult::Committed;
        auto accepted = reducer.prepareJoin(player(10), session, id<ServerTick>(1));
        return accepted && reducer.commit(std::move(*accepted)) && reducer.state().players().size() == 1
            && reducer.latestPublication() != before;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "round_trip_preserves_identity_roots_versions_seeds_and_order",
            &round_trip_preserves_identity_roots_versions_seeds_and_order },
        std::pair{ "malformed_inputs_and_identity_mismatches_reject_atomically",
            &malformed_inputs_and_identity_mismatches_reject_atomically },
        std::pair{ "replayed_command_stream_reaches_each_recorded_checksum",
            &replayed_command_stream_reaches_each_recorded_checksum },
        std::pair{ "durability_acknowledgement_precedes_installation_and_publication",
            &durability_acknowledgement_precedes_installation_and_publication },
    };
    for (const auto& [name, test] : tests)
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    return 0;
}
