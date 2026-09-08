#include <tes3mp/interactive_object_replication.hpp>
#include <tes3mp/protocol_frame.hpp>

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
    using namespace TES3MP;

    template <class Value>
    Value id(std::uint64_t value)
    {
        return Value::fromValue(value).value();
    }

    void require(bool condition, int line)
    {
        if (!condition)
        {
            std::cerr << "interactive_object_replication_tests assertion failed at line " << line << '\n';
            std::abort();
        }
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition), __LINE__)

    bool baseline_create_and_validation()
    {
        const auto session = id<SessionId>(1);
        const auto generation = SessionGeneration::initial();
        const auto tick = id<ServerTick>(10);
        const auto revision = id<CanonicalRevision>(5);

        std::vector<InteractiveObjectInterestMember> members{
            { id<InteractiveObjectId>(1), ObjectRevision::initial(), DoorState::Closed, LockState::Locked, TrapState::Armed },
            { id<InteractiveObjectId>(2), ObjectRevision::initial(), DoorState::Open, LockState::Unlocked, TrapState::Disarmed },
        };

        auto baseline = ReliableInteractiveObjectInterestBaseline::create(session, generation, tick, revision, members);
        assert(std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(baseline));
        const auto& b = std::get<ReliableInteractiveObjectInterestBaseline>(baseline);
        assert(b.targetSessionId() == session);
        assert(b.targetSessionGeneration() == generation);
        assert(b.serverTick() == tick);
        assert(b.canonicalRevision() == revision);
        assert(b.members().size() == 2);

        // Unsorted members rejected
        std::vector<InteractiveObjectInterestMember> unsorted{
            { id<InteractiveObjectId>(2), ObjectRevision::initial(), DoorState::Open, LockState::Unlocked, TrapState::Disarmed },
            { id<InteractiveObjectId>(1), ObjectRevision::initial(), DoorState::Closed, LockState::Locked, TrapState::Armed },
        };
        auto unsortedResult = ReliableInteractiveObjectInterestBaseline::create(session, generation, tick, revision, unsorted);
        assert(std::holds_alternative<InteractiveObjectReplicationDecodeError>(unsortedResult));
        assert(std::get<InteractiveObjectReplicationDecodeError>(unsortedResult).code
            == InteractiveObjectReplicationDecodeErrorCode::EntriesNotStrictlySorted);

        // Duplicate object ID rejected
        std::vector<InteractiveObjectInterestMember> duplicate{
            { id<InteractiveObjectId>(1), ObjectRevision::initial(), DoorState::Closed, LockState::Unlocked, TrapState::Disarmed },
            { id<InteractiveObjectId>(1), ObjectRevision::initial(), DoorState::Open, LockState::Unlocked, TrapState::Disarmed },
        };
        auto dupResult = ReliableInteractiveObjectInterestBaseline::create(session, generation, tick, revision, duplicate);
        assert(std::holds_alternative<InteractiveObjectReplicationDecodeError>(dupResult));

        std::vector<InteractiveObjectInterestMember> tooMany;
        tooMany.reserve(MaximumInteractiveObjectInterestMembers + 1);
        for (std::size_t index = 0; index <= MaximumInteractiveObjectInterestMembers; ++index)
            tooMany.push_back({ id<InteractiveObjectId>(index + 1), ObjectRevision::initial(), DoorState::Closed,
                LockState::Unlocked, TrapState::Disarmed });
        const auto tooManyResult
            = ReliableInteractiveObjectInterestBaseline::create(session, generation, tick, revision, tooMany);
        assert(std::holds_alternative<InteractiveObjectReplicationDecodeError>(tooManyResult));
        assert(std::get<InteractiveObjectReplicationDecodeError>(tooManyResult).code
            == InteractiveObjectReplicationDecodeErrorCode::TooManyEntries);

        return true;
    }

    bool baseline_encode_decode_roundtrip()
    {
        const auto session = id<SessionId>(42);
        const auto generation = SessionGeneration::initial();
        const auto tick = id<ServerTick>(100);
        const auto revision = id<CanonicalRevision>(7);

        std::vector<InteractiveObjectInterestMember> members{
            { id<InteractiveObjectId>(10), ObjectRevision::initial(), DoorState::Closed, LockState::Locked, TrapState::Armed },
            { id<InteractiveObjectId>(20), ObjectRevision::initial().next().value(), DoorState::Open, LockState::Unlocked, TrapState::Disarmed },
            { id<InteractiveObjectId>(30), ObjectRevision::initial(), DoorState::Closed, LockState::Unlocked, TrapState::Disarmed },
        };

        auto baseline = std::get<ReliableInteractiveObjectInterestBaseline>(
            ReliableInteractiveObjectInterestBaseline::create(session, generation, tick, revision, members));

        auto encoded = encodeReliableInteractiveObjectInterestBaseline(baseline);
        assert(!encoded.empty());

        auto decoded = decodeReliableInteractiveObjectInterestBaseline(encoded);
        assert(std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(decoded));
        const auto& result = std::get<ReliableInteractiveObjectInterestBaseline>(decoded);

        assert(result.targetSessionId() == session);
        assert(result.targetSessionGeneration() == generation);
        assert(result.serverTick() == tick);
        assert(result.canonicalRevision() == revision);
        assert(result.members().size() == 3);
        assert(result.members()[0] == members[0]);
        assert(result.members()[1] == members[1]);
        assert(result.members()[2] == members[2]);

        for (std::size_t size = 0; size < encoded.size(); ++size)
            assert(std::holds_alternative<InteractiveObjectReplicationDecodeError>(
                decodeReliableInteractiveObjectInterestBaseline(std::span(encoded).first(size))));

        // Corrupted identifier rejected
        auto corrupted = encoded;
        corrupted[4] = std::byte{ 'X' };
        assert(std::holds_alternative<InteractiveObjectReplicationDecodeError>(
            decodeReliableInteractiveObjectInterestBaseline(corrupted)));

        return true;
    }

    bool command_encode_decode_roundtrip()
    {
        const auto session = id<SessionId>(1);
        const auto generation = SessionGeneration::initial();
        const auto seq = id<CommandSequence>(1);
        const auto cmdId = id<CommandId>(10);
        const auto observedRev = id<CanonicalRevision>(2);
        const auto objId = id<InteractiveObjectId>(99);
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        const Position3 origin(100, 200, 300);
        const auto expectedRev = ObjectRevision::initial();
        const auto key = id<KeyPrototypeId>(55);

        ClientInteractObjectCommand cmd{
            .sessionId = session,
            .sessionGeneration = generation,
            .commandSequence = seq,
            .commandId = cmdId,
            .observedCanonicalRevision = observedRev,
            .objectId = objId,
            .targetCell = cell,
            .interactionOrigin = origin,
            .expectedRevision = expectedRev,
            .kind = ObjectInteractionKind::UnlockWithKey,
            .requestedKey = key
        };

        auto encoded = encodeClientInteractObjectCommand(cmd);
        assert(!encoded.empty());

        auto decoded = decodeClientInteractObjectCommand(encoded);
        assert(std::holds_alternative<ClientInteractObjectCommand>(decoded));
        const auto& result = std::get<ClientInteractObjectCommand>(decoded);

        assert(result.sessionId == session);
        assert(result.sessionGeneration == generation);
        assert(result.commandSequence == seq);
        assert(result.commandId == cmdId);
        assert(result.observedCanonicalRevision == observedRev);
        assert(result.objectId == objId);
        assert(result.targetCell == cell);
        assert(result.interactionOrigin == origin);
        assert(result.expectedRevision == expectedRev);
        assert(result.kind == ObjectInteractionKind::UnlockWithKey);
        assert(result.requestedKey == key);

        // Command without key in exterior cell
        const auto extCell = CellId::exterior(id<CellSpaceId>(8), 2, -3);
        ClientInteractObjectCommand cmdNoKey{
            .sessionId = session,
            .sessionGeneration = generation,
            .commandSequence = seq,
            .commandId = cmdId,
            .observedCanonicalRevision = observedRev,
            .objectId = objId,
            .targetCell = extCell,
            .interactionOrigin = origin,
            .expectedRevision = expectedRev,
            .kind = ObjectInteractionKind::Activate,
            .requestedKey = std::nullopt
        };

        auto encodedNoKey = encodeClientInteractObjectCommand(cmdNoKey);
        auto decodedNoKey = decodeClientInteractObjectCommand(encodedNoKey);
        assert(std::holds_alternative<ClientInteractObjectCommand>(decodedNoKey));
        const auto& resultNoKey = std::get<ClientInteractObjectCommand>(decodedNoKey);
        assert(resultNoKey.targetCell == extCell);
        assert(!resultNoKey.requestedKey.has_value());
        assert(resultNoKey.kind == ObjectInteractionKind::Activate);

        for (std::size_t size = 0; size < encoded.size(); ++size)
            assert(std::holds_alternative<InteractiveObjectReplicationDecodeError>(
                decodeClientInteractObjectCommand(std::span(encoded).first(size))));

        auto oversized = encoded;
        oversized.resize(ReliableOperationMaximumPayloadBytes + 1);
        assert(std::holds_alternative<InteractiveObjectReplicationDecodeError>(
            decodeClientInteractObjectCommand(oversized)));

        assert(messageDescriptor(MessageKind::ReliableInteractiveObjectInterestBaseline)->messageClass
            == MessageClass::ReliableOperation);
        assert(messageDescriptor(MessageKind::ClientInteractObjectCommand)->messageClass
            == MessageClass::ReliableOperation);

        return true;
    }
}

int main()
{
    return baseline_create_and_validation()
        && baseline_encode_decode_roundtrip()
        && command_encode_decode_roundtrip()
        ? 0
        : 1;
}
