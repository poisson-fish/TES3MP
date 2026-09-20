#include "adapter.hpp"
#include "client_connection.hpp"
#include "movement_mapping.hpp"
#include "player_profile_manager.hpp"
#include "providers.hpp"
#include "remote_motion.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <vector>

namespace
{
    void require(bool value, int line)
    {
        if (!value)
        {
            std::cerr << "adapter_tests failure at line " << line << std::endl;
            std::abort();
        }
    }

    template <class T>
    T value(std::uint64_t raw)
    {
        return T::fromValue(raw).value();
    }

    TES3MP::SpatialEntitySnapshot remoteSample(std::uint64_t tick, std::uint64_t revision, std::int64_t x,
        std::int64_t velocity = 4096, std::uint64_t epoch = 1,
        TES3MP::LocomotionMode mode = TES3MP::LocomotionMode::Walk)
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        return TES3MP::SpatialEntitySnapshot(value<TES3MP::ServerTick>(tick), value<TES3MP::PlayerId>(1),
            value<TES3MP::EntityId>(2), value<TES3MP::AppearanceId>(1), value<TES3MP::EntityRevision>(revision),
            value<TES3MP::AuthorityEpoch>(epoch),
            TES3MP::Transform(TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)), TES3MP::Position3(x, 0, 0),
                TES3MP::Orientation3(zero, zero, zero)),
            TES3MP::LinearVelocity3(velocity, 0, 0), mode);
    }

    TES3MP::ServerHello serverHello(bool pose = false, bool actors = false, bool interactiveObjects = false,
        bool inventory = false, bool combat = false, std::uint16_t minor = 2, bool dialogueChoices = false,
        bool weather = false, bool worldTime = false, bool waitRest = false, bool instantMagic = false)
    {
        auto versions = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, minor, minor));
        std::vector<TES3MP::CapabilityId> capabilities;
        if (pose)
            capabilities.push_back(TES3MP::vrPoseCapability());
        if (actors)
            capabilities.push_back(TES3MP::actorReplicationCapability());
        if (interactiveObjects)
            capabilities.push_back(TES3MP::interactiveObjectReplicationCapability());
        if (inventory)
            capabilities.push_back(TES3MP::inventoryReplicationCapability());
        if (combat)
            capabilities.push_back(TES3MP::combatReplicationCapability());
        if (dialogueChoices)
            capabilities.push_back(TES3MP::dialogueChoiceCapability());
        if (weather)
            capabilities.push_back(TES3MP::weatherReplicationCapability());
        if (worldTime)
            capabilities.push_back(TES3MP::worldTimeReplicationCapability());
        if (waitRest)
            capabilities.push_back(TES3MP::authoritativeWaitRestCapability());
        if (instantMagic)
        {
            capabilities.push_back(TES3MP::authoritativeInstantMagicCapability());
            capabilities.push_back(TES3MP::authoritativeTimedAreaMagicCapability());
        }
        auto client = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(versions, capabilities, {}));
        auto server
            = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(std::move(versions), capabilities, {}));
        auto negotiated = TES3MP::negotiateClientHello(TES3MP::ClientHello::fromOffer(std::move(client)), server);
        return std::get<TES3MP::ServerHello>(std::move(negotiated));
    }

    TES3MP::ReliableInteractiveObjectInterestBaseline interactiveObjectBaseline(TES3MP::SessionGeneration generation,
        std::uint64_t tick, std::uint64_t revision, std::optional<std::uint64_t> objectRevision = std::nullopt)
    {
        const std::array members{ TES3MP::InteractiveObjectInterestMember{
            *TES3MP::InteractiveObjectId::fromValue(101),
            *TES3MP::ObjectRevision::fromValue(objectRevision.value_or(revision)),
            TES3MP::DoorState::Open,
            TES3MP::LockState::Unlocked,
            TES3MP::TrapState::Disarmed,
        } };
        auto created = TES3MP::ReliableInteractiveObjectInterestBaseline::create(value<TES3MP::SessionId>(1),
            generation, value<TES3MP::ServerTick>(tick), value<TES3MP::CanonicalRevision>(revision), members);
        return std::get<TES3MP::ReliableInteractiveObjectInterestBaseline>(std::move(created));
    }

    TES3MP::InventoryBaselineHeader inventoryHeader(
        TES3MP::SessionGeneration generation, std::uint64_t tick, std::uint64_t revision)
    {
        return { value<TES3MP::SessionId>(1), generation, value<TES3MP::ServerTick>(tick),
            value<TES3MP::CanonicalRevision>(revision), 0, 1 };
    }

    TES3MP::ReliablePlayerInventoryBaseline playerInventoryBaseline(TES3MP::SessionGeneration generation,
        std::uint64_t tick, std::uint64_t revision, std::uint32_t chunkIndex = 0, std::uint32_t chunkCount = 1)
    {
        const std::array stacks{ TES3MP::CanonicalItemStack{
            value<TES3MP::ItemStackId>(10 + chunkIndex), value<TES3MP::ItemPrototypeId>(20), 2, 0, 0, std::nullopt } };
        auto header = inventoryHeader(generation, tick, revision);
        header.chunkIndex = chunkIndex;
        header.chunkCount = chunkCount;
        auto created = TES3MP::ReliablePlayerInventoryBaseline::create(
            header, value<TES3MP::PlayerId>(1), value<TES3MP::InventoryRevision>(revision), stacks, {});
        return std::get<TES3MP::ReliablePlayerInventoryBaseline>(std::move(created));
    }

    TES3MP::ReliableContainerInventoryBaseline containerInventoryBaseline(
        TES3MP::SessionGeneration generation, std::uint64_t tick, std::uint64_t revision)
    {
        const std::array stacks{ TES3MP::CanonicalItemStack{
            value<TES3MP::ItemStackId>(12), value<TES3MP::ItemPrototypeId>(20), 1, 0, 0, std::nullopt } };
        auto created = TES3MP::ReliableContainerInventoryBaseline::create(inventoryHeader(generation, tick, revision),
            value<TES3MP::ContainerId>(30), TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)),
            TES3MP::Position3(10, 0, 0), value<TES3MP::ContainerRevision>(revision), 100, stacks);
        return std::get<TES3MP::ReliableContainerInventoryBaseline>(std::move(created));
    }

    TES3MP::ReliableGroundItemBaseline groundItemBaseline(
        TES3MP::SessionGeneration generation, std::uint64_t tick, std::uint64_t revision)
    {
        auto created = TES3MP::ReliableGroundItemBaseline::create(
            inventoryHeader(generation, tick, revision), TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)), {});
        return std::get<TES3MP::ReliableGroundItemBaseline>(std::move(created));
    }

    TES3MP::LatestWinsEquipmentSnapshot equipmentSnapshot(
        TES3MP::SessionGeneration generation, std::uint64_t tick, std::uint64_t revision)
    {
        const std::array members{ TES3MP::PublicEquipmentMember{ value<TES3MP::PlayerId>(1) } };
        auto created = TES3MP::LatestWinsEquipmentSnapshot::create(value<TES3MP::SessionId>(1), generation,
            value<TES3MP::ServerTick>(tick), value<TES3MP::CanonicalRevision>(revision), members);
        return std::get<TES3MP::LatestWinsEquipmentSnapshot>(std::move(created));
    }

    TES3MP::LatestWinsCombatSnapshot combatSnapshot(
        TES3MP::SessionGeneration generation, std::uint64_t tick, std::uint64_t revision)
    {
        const std::array actors{ TES3MP::ActorCombatSnapshot{
            value<TES3MP::ActorId>(1), TES3MP::CombatRevision::initial(), 25.f, 40.f, 30.f, 50.f, false } };
        std::array<TES3MP::CombatSkillSnapshot, TES3MP::ReplicatedCombatSkillCount> skills{};
        for (std::size_t index = 0; index < skills.size(); ++index)
            skills[index] = { static_cast<TES3MP::ReplicatedCombatSkill>(index), 20.f + index, 0.25f };
        auto created = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1), generation,
            value<TES3MP::ServerTick>(tick), value<TES3MP::CanonicalRevision>(revision), value<TES3MP::PlayerId>(1),
            TES3MP::CombatRevision::initial(), 100.f, 120.f, 80.f, 100.f, 60.f, 75.f, false, actors, skills);
        return std::get<TES3MP::LatestWinsCombatSnapshot>(std::move(created));
    }

    TES3MP::ReliableWeatherState weatherState(TES3MP::SessionGeneration generation, std::uint64_t tick,
        std::uint64_t canonicalRevision, std::uint64_t weatherRevision, bool baseline, std::uint64_t target = 2)
    {
        const std::array regions{ TES3MP::WeatherRegionSnapshot{ value<TES3MP::WeatherRegionId>(1),
            value<TES3MP::WeatherId>(1), value<TES3MP::WeatherId>(target), value<TES3MP::ServerTick>(5),
            value<TES3MP::ServerTick>(15), value<TES3MP::ServerTick>(25),
            value<TES3MP::WeatherRevision>(weatherRevision) } };
        const TES3MP::WeatherStateHeader header{ value<TES3MP::SessionId>(1), generation,
            value<TES3MP::ServerTick>(tick), value<TES3MP::CanonicalRevision>(canonicalRevision), 0, 1, baseline };
        return std::get<TES3MP::ReliableWeatherState>(TES3MP::ReliableWeatherState::create(header, regions));
    }

    TES3MP::ReliableWorldTimeState worldTimeState(
        TES3MP::SessionGeneration generation, std::uint64_t tick, bool baseline)
    {
        TES3MP::CanonicalWorldTimeState time;
        time.day = 30;
        time.month = 1;
        time.year = 427;
        time.millisecondsSinceMidnight = 45'000'000;
        time.timeScaleUnits = 30 * TES3MP::WorldTimeScaleUnitsPerOne;
        time.revision = value<TES3MP::WorldTimeRevision>(tick);
        time.lastChangeTick = value<TES3MP::ServerTick>(tick);
        time.lastAdvanceTick = value<TES3MP::ServerTick>(tick);
        return { value<TES3MP::SessionId>(1), generation, value<TES3MP::ServerTick>(tick),
            value<TES3MP::CanonicalRevision>(tick), time, baseline };
    }

    std::vector<std::byte> frame(
        TES3MP::MessageClass messageClass, TES3MP::MessageKind messageKind, std::span<const std::byte> payload)
    {
        return std::get<std::vector<std::byte>>(TES3MP::encodeProtocolFrame(messageClass, messageKind, payload));
    }

    TES3MP::AuthenticationAcceptedMessage accepted(std::byte marker,
        std::uint64_t lifetime = TES3MP::MinimumResumeTokenLifetimeMilliseconds, bool includePlayerCredential = false,
        TES3MP::CharacterLifecycle lifecycle = TES3MP::CharacterLifecycle::EstablishedCharacter)
    {
        std::array<std::byte, TES3MP::ResumeTokenBytes> bytes{};
        bytes.fill(marker);
        auto token = TES3MP::ResumeToken::create(bytes);
        std::optional<TES3MP::PlayerCredential> playerCredential;
        if (includePlayerCredential)
            playerCredential = TES3MP::PlayerCredential::create(bytes);
        return std::move(*TES3MP::AuthenticationAcceptedMessage::create(
            std::move(*token), lifetime, std::move(playerCredential), lifecycle));
    }

    TES3MP::CharacterProfile establishedCharacterProfile()
    {
        TES3MP::CharacterDerivedState derived;
        derived.attributes.fill(40);
        derived.skills.fill(5);
        auto profile = TES3MP::CharacterProfile::restore(TES3MP::CharacterLifecycle::EstablishedCharacter,
            TES3MP::CharacterCreationPhase::Complete, "Nerevar",
            TES3MP::CharacterAppearance{ value<TES3MP::RaceRecordId>(1), value<TES3MP::HeadRecordId>(2),
                value<TES3MP::HairRecordId>(3), TES3MP::CharacterSex::Male },
            TES3MP::CharacterClass{ value<TES3MP::ClassRecordId>(4) }, value<TES3MP::BirthsignRecordId>(5),
            std::move(derived), {}, value<TES3MP::CharacterProfileRevision>(6));
        return std::move(*profile);
    }

    class RecordingPlayerCredentialPersistence final : public TES3MP::OpenMWAdapter::PlayerCredentialPersistence
    {
    public:
        bool store(TES3MP::PlayerCredential credential) noexcept override
        {
            ++calls;
            return credential.copyTo(bytes);
        }
        std::size_t calls = 0;
        std::array<std::byte, TES3MP::PlayerCredentialBytes> bytes{};
    };

    TES3MP::LatestWinsSnapshot selfSnapshot(TES3MP::SessionGeneration generation, bool includeRemote = false,
        std::uint64_t canonicalRevision = 1, std::uint64_t serverTick = 1,
        std::optional<std::uint64_t> acknowledgedCommand = std::nullopt)
    {
        const auto session = value<TES3MP::SessionId>(1);
        const auto player = value<TES3MP::PlayerId>(1);
        const auto entity = value<TES3MP::EntityId>(1);
        const auto zero = TES3MP::Turn32::fromValue(0);
        std::vector entries{ TES3MP::SpatialEntitySnapshot(value<TES3MP::ServerTick>(serverTick), player, entity,
            value<TES3MP::AppearanceId>(1), TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(),
            TES3MP::Transform(TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)), TES3MP::Position3(0, 0, 0),
                TES3MP::Orientation3(zero, zero, zero)),
            TES3MP::LinearVelocity3(0, 0, 0)) };
        if (includeRemote)
            entries.push_back(TES3MP::SpatialEntitySnapshot(value<TES3MP::ServerTick>(serverTick),
                value<TES3MP::PlayerId>(2), value<TES3MP::EntityId>(2), value<TES3MP::AppearanceId>(1),
                TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(),
                TES3MP::Transform(TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)), TES3MP::Position3(0, 0, 0),
                    TES3MP::Orientation3(zero, zero, zero)),
                TES3MP::LinearVelocity3(0, 0, 0)));
        auto view = std::get<TES3MP::SpatialWorldView>(TES3MP::SpatialWorldView::create(entries));
        const auto acknowledgement
            = acknowledgedCommand ? std::optional(value<TES3MP::CommandSequence>(*acknowledgedCommand)) : std::nullopt;
        return TES3MP::LatestWinsSnapshot(TES3MP::LatestWinsSnapshotHeader(session, generation, player, entity,
                                              value<TES3MP::CanonicalRevision>(canonicalRevision), acknowledgement),
            std::move(view));
    }

    TES3MP::ReliableInterestBaseline selfBaseline(TES3MP::SessionGeneration generation, bool includeRemote = false,
        std::uint64_t canonicalRevision = 1, std::uint64_t serverTick = 1)
    {
        std::vector members{ TES3MP::InterestMember{ value<TES3MP::PlayerId>(1), value<TES3MP::EntityId>(1) } };
        if (includeRemote)
            members.push_back({ value<TES3MP::PlayerId>(2), value<TES3MP::EntityId>(2) });
        return std::get<TES3MP::ReliableInterestBaseline>(TES3MP::ReliableInterestBaseline::create(
            value<TES3MP::SessionId>(1), generation, value<TES3MP::CanonicalRevision>(canonicalRevision),
            value<TES3MP::CanonicalStateVersion>(canonicalRevision), value<TES3MP::ServerTick>(serverTick), members));
    }

    TES3MP::LatestWinsActorSnapshot actorSnapshot(
        TES3MP::SessionGeneration generation, std::uint64_t canonicalRevision, std::uint64_t serverTick)
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        const std::array entries{ TES3MP::ActorSpatialSnapshot(value<TES3MP::ServerTick>(serverTick),
            value<TES3MP::ActorId>(1), value<TES3MP::EntityId>(9), value<TES3MP::ActorPrototypeId>(1),
            value<TES3MP::EntityRevision>(canonicalRevision), TES3MP::AuthorityEpoch::initial(),
            TES3MP::Transform(TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)),
                TES3MP::Position3(static_cast<std::int64_t>(canonicalRevision), 0, 0),
                TES3MP::Orientation3(zero, zero, zero)),
            TES3MP::LinearVelocity3(1, 0, 0), TES3MP::ActorActivity::Wander) };
        auto view = std::get<TES3MP::ActorWorldView>(TES3MP::ActorWorldView::create(entries));
        return TES3MP::LatestWinsActorSnapshot(value<TES3MP::SessionId>(1), generation,
            value<TES3MP::ServerTick>(serverTick), value<TES3MP::CanonicalRevision>(canonicalRevision),
            std::move(view));
    }

    TES3MP::ReliableActorInterestBaseline actorBaseline(
        TES3MP::SessionGeneration generation, std::uint64_t canonicalRevision, std::uint64_t serverTick)
    {
        const std::array members{ TES3MP::ActorInterestMember{
            value<TES3MP::ActorId>(1), value<TES3MP::EntityId>(9), value<TES3MP::ActorPrototypeId>(1) } };
        return std::get<TES3MP::ReliableActorInterestBaseline>(
            TES3MP::ReliableActorInterestBaseline::create(value<TES3MP::SessionId>(1), generation,
                value<TES3MP::ServerTick>(serverTick), value<TES3MP::CanonicalRevision>(canonicalRevision), members));
    }

    class MotionMetrics final : public TES3MP::OpenMWAdapter::RemoteMotionMetricSink
    {
    public:
        TES3MP::ObservationResult tryRecord(TES3MP::OpenMWAdapter::RemoteMotionMetric metric) noexcept override
        {
            if (size == values.size())
            {
                ++dropped;
                return TES3MP::ObservationResult::Dropped;
            }
            values[size++] = metric;
            return TES3MP::ObservationResult::Accepted;
        }

        bool has(TES3MP::OpenMWAdapter::RemoteMotionMetricKey key) const
        {
            for (std::size_t index = 0; index < size; ++index)
                if (values[index]->key == key)
                    return true;
            return false;
        }

        std::size_t count(TES3MP::OpenMWAdapter::RemoteMotionMetricKey key) const
        {
            std::size_t result = 0;
            for (std::size_t index = 0; index < size; ++index)
                if (values[index]->key == key)
                    ++result;
            return result;
        }

        std::uint64_t maximum(TES3MP::OpenMWAdapter::RemoteMotionMetricKey key) const
        {
            std::uint64_t result = 0;
            for (std::size_t index = 0; index < size; ++index)
                if (values[index]->key == key)
                    result = std::max(result, values[index]->value);
            return result;
        }

        std::array<std::optional<TES3MP::OpenMWAdapter::RemoteMotionMetric>, 128> values{};
        std::size_t size = 0;
        std::size_t dropped = 0;
    };

    class DroppingMotionMetrics final : public TES3MP::OpenMWAdapter::RemoteMotionMetricSink
    {
    public:
        TES3MP::ObservationResult tryRecord(TES3MP::OpenMWAdapter::RemoteMotionMetric) noexcept override
        {
            return TES3MP::ObservationResult::Dropped;
        }
    };

    class Clock final : public TES3MP::MonotonicClock
    {
    public:
        TES3MP::MonotonicInstant now() const noexcept override
        {
            return TES3MP::MonotonicInstant::fromNanoseconds(nanoseconds);
        }

        std::uint64_t nanoseconds = 0;
    };

    class IdleTransport final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override
        {
            return { TES3MP::TransportResult::NotReady, std::nullopt };
        }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override { return TES3MP::TransportResult::NotReady; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        {
            if (acceptConnections)
            {
                pendingAttempt = value<TES3MP::ConnectAttemptId>(nextConnection);
                return { TES3MP::TransportResult::Accepted, pendingAttempt };
            }
            return { TES3MP::TransportResult::NotReady, std::nullopt };
        }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override
        {
            return TES3MP::TransportResult::NotReady;
        }
        TES3MP::TransportResult send(
            TES3MP::TransportConnectionId, TES3MP::TransportChannel channel, std::span<const std::byte> bytes) override
        {
            sentChannel = channel;
            sent.assign(bytes.begin(), bytes.end());
            sentFrames.emplace_back(sent);
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportReceiveResult receive(
            TES3MP::TransportConnectionId, std::span<TES3MP::TransportMessage> output) override
        {
            const auto count = std::min(output.size(), inbound.size());
            for (std::size_t index = 0; index < count; ++index)
                output[index] = std::move(inbound[index]);
            inbound.erase(inbound.begin(), inbound.begin() + static_cast<std::ptrdiff_t>(count));
            return { acceptConnections ? TES3MP::TransportResult::Accepted : TES3MP::TransportResult::NotReady, count };
        }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode) override
        {
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent> output) override
        {
            if (failPoll)
                return { TES3MP::TransportResult::RuntimeFailed, 0 };
            if (pendingAttempt && !output.empty())
            {
                const auto connection = value<TES3MP::TransportConnectionId>(nextConnection++);
                output[0] = { TES3MP::TransportEventKind::ConnectSucceeded, TES3MP::TransportFailure::None,
                    std::nullopt, pendingAttempt, connection };
                pendingAttempt.reset();
                return { TES3MP::TransportResult::Accepted, 1 };
            }
            return { TES3MP::TransportResult::Accepted, 0 };
        }
        TES3MP::TransportResult shutdown() override { return TES3MP::TransportResult::Accepted; }

        bool failPoll = false;
        bool acceptConnections = false;
        std::uint64_t nextConnection = 1;
        std::optional<TES3MP::ConnectAttemptId> pendingAttempt;
        std::vector<TES3MP::TransportMessage> inbound;
        std::vector<std::byte> sent;
        std::vector<std::vector<std::byte>> sentFrames;
        std::optional<TES3MP::TransportChannel> sentChannel;

        void enqueue(TES3MP::MessageClass messageClass, TES3MP::MessageKind messageKind,
            std::span<const std::byte> payload, TES3MP::TransportChannel channel)
        {
            inbound.push_back({ channel, frame(messageClass, messageKind, payload) });
        }
    };

    class Input final : public TES3MP::OpenMWAdapter::SemanticInputProvider
    {
    public:
        TES3MP::OpenMWAdapter::CellTransitionCapture captureCellTransition() noexcept override
        {
            if (nextTransition)
            {
                auto val = *nextTransition;
                nextTransition.reset();
                return val;
            }
            return {};
        }
        std::optional<TES3MP::LocomotionIntent> sampleCurrentIntent() noexcept override
        {
            ++calls;
            return TES3MP::LocomotionIntent(
                TES3MP::LocomotionMode::Walk, TES3MP::Turn32::fromValue(0), TES3MP::LinearVelocity3(1, 2, 3));
        }
        std::optional<TES3MP::OpenMWAdapter::ObjectInteractionCapture> captureObjectInteraction() noexcept override
        {
            ++interactionCalls;
            if (nextInteraction)
            {
                auto val = std::move(nextInteraction);
                nextInteraction.reset();
                return val;
            }
            return std::nullopt;
        }
        std::optional<TES3MP::OpenMWAdapter::InventoryTransactionCapture>
        captureInventoryTransaction() noexcept override
        {
            ++inventoryCalls;
            if (!nextInventory)
                return std::nullopt;
            auto value = std::move(nextInventory);
            nextInventory.reset();
            return value;
        }
        std::optional<TES3MP::OpenMWAdapter::MeleeAttackCapture> captureMeleeAttack() noexcept override
        {
            ++meleeCalls;
            auto value = std::move(nextMelee);
            nextMelee.reset();
            return value;
        }
        std::optional<TES3MP::OpenMWAdapter::MagicUseCapture> captureMagicUse() noexcept override
        {
            ++magicCalls;
            auto value = std::move(nextMagic);
            nextMagic.reset();
            return value;
        }
        std::optional<TES3MP::DialogueChoiceId> mapDialogueChoice(int localChoice) const noexcept override
        {
            return localChoice == 7 ? TES3MP::DialogueChoiceId::fromValue(40) : std::nullopt;
        }
        void clearSessionState() noexcept override
        {
            ++clearCalls;
            nextInteraction.reset();
            nextInventory.reset();
            nextMelee.reset();
            nextMagic.reset();
        }
        unsigned calls = 0;
        unsigned interactionCalls = 0;
        unsigned inventoryCalls = 0;
        unsigned meleeCalls = 0;
        unsigned magicCalls = 0;
        unsigned clearCalls = 0;
        std::optional<TES3MP::OpenMWAdapter::CellTransitionCapture> nextTransition;
        std::optional<TES3MP::OpenMWAdapter::ObjectInteractionCapture> nextInteraction;
        std::optional<TES3MP::OpenMWAdapter::InventoryTransactionCapture> nextInventory;
        std::optional<TES3MP::OpenMWAdapter::MeleeAttackCapture> nextMelee;
        std::optional<TES3MP::OpenMWAdapter::MagicUseCapture> nextMagic;
    };

    class Presentation final : public TES3MP::OpenMWAdapter::PresentationProvider
    {
    public:
        TES3MP::OpenMWAdapter::ProviderResult applyAuthoritative(const TES3MP::LatestWinsSnapshot&,
            std::span<const TES3MP::ObservedPlayer> observed, bool allowLocalCellCorrection, TES3MP::MonotonicInstant,
            const std::optional<TES3MP::LocalLocomotionReconciliation>&) noexcept override
        {
            ++calls;
            lastObservedPlayers = observed.size();
            lastAllowLocalCellCorrection = allowLocalCellCorrection;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult advance(TES3MP::MonotonicInstant) noexcept override
        {
            ++advances;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyVrPose(
            const TES3MP::ServerVrPoseSnapshot&, TES3MP::MonotonicInstant) noexcept override
        {
            ++poses;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyActors(const TES3MP::LatestWinsActorSnapshot&,
            std::span<const TES3MP::ActorInterestMember>, TES3MP::MonotonicInstant) noexcept override
        {
            ++actors;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyInteractiveObjects(
            const TES3MP::ReliableInteractiveObjectInterestBaseline& baseline,
            TES3MP::MonotonicInstant) noexcept override
        {
            ++interactiveObjects;
            for (const auto& member : baseline.members())
                objectRevisions.insert_or_assign(member.objectId, member.revision);
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyInventory(const TES3MP::ReliablePlayerInventoryBaseline& player,
            std::span<const TES3MP::ReliableContainerInventoryBaseline> containers,
            const TES3MP::ReliableGroundItemBaseline& ground, const TES3MP::LatestWinsEquipmentSnapshot& equipment,
            TES3MP::MonotonicInstant) noexcept override
        {
            ++inventories;
            lastInventoryRevision = player.revision;
            lastContainerCount = containers.size();
            lastGroundCount = ground.items.size();
            lastGroundCell = ground.cell;
            lastEquipmentCount = equipment.members.size();
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyNativeDoors(const TES3MP::ReliableGroundItemBaseline& ground,
            TES3MP::MonotonicInstant) noexcept override
        {
            if (!ground.doors.empty()) doorAngles.push_back(ground.doors.front().angle);
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyCombat(const TES3MP::LatestWinsCombatSnapshot& snapshot,
            std::span<const TES3MP::ReliableCombatEventBatch> events, TES3MP::MonotonicInstant) noexcept override
        {
            ++combats;
            lastCombatFatigue = snapshot.selfFatigue();
            lastCombatEvents = events.size();
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyWeather(std::span<const TES3MP::WeatherRegionSnapshot> regions,
            TES3MP::ServerTick tick, TES3MP::MonotonicInstant) noexcept override
        {
            ++weathers;
            lastWeatherTick = tick;
            lastWeatherRegions.assign(regions.begin(), regions.end());
            return weatherResult;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyWorldTime(
            const TES3MP::ReliableWorldTimeState& state, TES3MP::MonotonicInstant) noexcept override
        {
            ++worldTimes;
            lastWorldTime = state;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyQuestJournal(const TES3MP::QuestJournalCatalog& catalog,
            const TES3MP::CanonicalPlayerQuestJournalState& state, TES3MP::MonotonicInstant) noexcept override
        {
            ++questJournals;
            lastQuestCatalogSize = catalog.quests().size();
            lastQuestJournal = state;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        std::optional<TES3MP::ObjectRevision> observedObjectRevision(
            TES3MP::InteractiveObjectId id) const noexcept override
        {
            auto it = objectRevisions.find(id);
            if (it != objectRevisions.end())
                return it->second;
            return std::nullopt;
        }
        TES3MP::OpenMWAdapter::ProviderResult applyVrPoseWeight(
            TES3MP::EntityId, TES3MP::AuthorityEpoch, double weight) noexcept override
        {
            ++poseFallbacks;
            lastPoseWeight = weight;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        void clear() noexcept override
        {
            ++clears;
            objectRevisions.clear();
        }
        unsigned calls = 0;
        std::size_t lastObservedPlayers = 0;
        bool lastAllowLocalCellCorrection = false;
        unsigned advances = 0;
        unsigned clears = 0;
        unsigned poses = 0;
        unsigned actors = 0;
        unsigned interactiveObjects = 0;
        unsigned inventories = 0;
        std::vector<float> doorAngles;
        std::optional<TES3MP::CellId> lastGroundCell;
        unsigned combats = 0;
        unsigned questJournals = 0;
        unsigned weathers = 0;
        unsigned poseFallbacks = 0;
        double lastPoseWeight = 0.0;
        std::map<TES3MP::InteractiveObjectId, TES3MP::ObjectRevision> objectRevisions;
        std::optional<TES3MP::InventoryRevision> lastInventoryRevision;
        std::size_t lastContainerCount = 0;
        std::size_t lastGroundCount = 0;
        std::size_t lastEquipmentCount = 0;
        float lastCombatFatigue = 0.f;
        std::size_t lastCombatEvents = 0;
        std::size_t lastQuestCatalogSize = 0;
        std::optional<TES3MP::CanonicalPlayerQuestJournalState> lastQuestJournal;
        TES3MP::OpenMWAdapter::ProviderResult weatherResult = TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        std::optional<TES3MP::ServerTick> lastWeatherTick;
        std::vector<TES3MP::WeatherRegionSnapshot> lastWeatherRegions;
        std::size_t worldTimes = 0;
        std::optional<TES3MP::ReliableWorldTimeState> lastWorldTime;
    };

    class PoseInput final : public TES3MP::OpenMWAdapter::VrPoseInputProvider
    {
    public:
        std::optional<TES3MP::OpenMWAdapter::LocalVrPose> sampleVrPose() noexcept override
        {
            ++calls;
            const auto offset = *TES3MP::VrPoseOffset3::create(1, 2, 3);
            const auto zero = TES3MP::Turn32::fromValue(0);
            return TES3MP::OpenMWAdapter::LocalVrPose{
                TES3MP::VrTrackedTransform(offset, TES3MP::Orientation3(zero, zero, zero)), std::nullopt, std::nullopt
            };
        }
        unsigned calls = 0;
    };

    class Status final : public TES3MP::OpenMWAdapter::ConnectionStatusProvider
    {
    public:
        void report(TES3MP::OpenMWAdapter::ConnectionStatus value) noexcept override
        {
            last = value;
            values.push_back(value);
        }
        std::optional<TES3MP::OpenMWAdapter::ConnectionStatus> last;
        std::vector<TES3MP::OpenMWAdapter::ConnectionStatus> values;
    };

    class DisconnectOnce final : public TES3MP::OpenMWAdapter::ConnectionControlProvider
    {
    public:
        bool disconnectRequested() noexcept override
        {
            if (!pending)
                return false;
            pending = false;
            return true;
        }
        std::optional<TES3MP::ResyncReason> resyncRequested() noexcept override
        {
            if (!resyncPending)
                return std::nullopt;
            resyncPending = false;
            return TES3MP::ResyncReason::LocalFeedGap;
        }
        void resyncCompleted() noexcept override { ++resyncCompletions; }
        bool pending = true;
        bool resyncPending = false;
        unsigned resyncCompletions = 0;
    };

    class Coordinator final : public TES3MP::OpenMWAdapter::EngineCoordinator
    {
    public:
        void frame(float duration) noexcept override
        {
            ++calls;
            lastDuration = duration;
        }
        unsigned calls = 0;
        float lastDuration = 0;
    };
}

#define require(value) require(static_cast<bool>(value), __LINE__)

void teleportPresentation(bool pickupBarrier = false, bool doorProgress = false)
{
    using namespace TES3MP;
    using namespace TES3MP::OpenMWAdapter;
    Input input; Presentation presentation; Status status;
    auto transport = std::make_unique<IdleTransport>();
    auto* wire = transport.get(); wire->acceptConnections = true;
    auto clock = std::make_unique<Clock>();
    const auto endpoint = *ConnectionEndpoint::create("127.0.0.1", 25560);
    const auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
    const auto outbound = *OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
    const ReconnectConfiguration reconnect{endpoint, timeouts, outbound, testContentManifestId()};
    auto runtime = std::get<std::unique_ptr<ClientSessionRuntime>>(
        ClientSessionRuntime::create(*transport, *clock, timeouts, SessionGeneration::initial(), outbound));
    const std::array capabilities{inventoryReplicationCapability()};
    auto offer = std::get<CapabilityOffer>(CapabilityOffer::create(
        std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1,2,2)), capabilities, {}));
    const std::array passwordBytes{std::byte{1}};
    require(runtime->start(endpoint, ClientHello::fromOffer(std::move(offer)),
        AuthenticationRequest::join(*AuthenticationMaterial::create(passwordBytes))) == HeadlessClientResult::Accepted);
    auto coordinator = makeCoordinator(std::move(transport), std::move(clock), std::move(runtime),
        reconnect, input, presentation, status);
    coordinator->frame(.01f);
    wire->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello(false,false,false,true)), TransportChannel::ReliableOrdered);
    coordinator->frame(.01f);
    wire->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(accepted(std::byte{7})), TransportChannel::ReliableOrdered);
    const auto generation = SessionGeneration::initial();
    const auto first = CellId::interior(value<CellSpaceId>(7)), second = CellId::interior(value<CellSpaceId>(8));
    const auto sendSpatial = [&](uint64_t revision, CellId cell) {
        wire->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
            encodeReliableInterestBaseline(selfBaseline(generation, false, revision, revision)), TransportChannel::ReliableOrdered);
        const auto original = selfSnapshot(generation, false, revision, revision);
        const auto& self = original.view().entries()[0];
        const std::array entries{SpatialEntitySnapshot(self.serverTick(), self.playerId(), self.entityId(), self.appearanceId(),
            value<EntityRevision>(revision), value<AuthorityEpoch>(pickupBarrier ? 1 : revision), Transform(cell, Position3(32 * 1024, 0, 0),
                self.transform().orientation()), LinearVelocity3(0,0,0))};
        wire->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
            encodeLatestWinsSnapshot(LatestWinsSnapshot(original.header(), std::get<SpatialWorldView>(SpatialWorldView::create(entries)))),
            TransportChannel::LatestWins);
    };
    const auto sendInventory = [&](uint64_t revision, CellId cell) {
        wire->enqueue(MessageClass::ReliableOperation, MessageKind::ReliablePlayerInventoryBaseline,
            encodeReliablePlayerInventoryBaseline(playerInventoryBaseline(generation, revision, revision)), TransportChannel::ReliableOrdered);
        auto ground = groundItemBaseline(generation, revision, revision);
        ground.cell = cell; ground.nativeWorld = true; ground.teleportDoors = {(uint64_t(1) << 63) | revision};
        wire->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableGroundItemBaseline,
            encodeReliableGroundItemBaseline(ground), TransportChannel::ReliableOrdered);
        wire->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsEquipmentSnapshot,
            encodeLatestWinsEquipmentSnapshot(equipmentSnapshot(generation, revision, revision)), TransportChannel::LatestWins);
    };
    sendSpatial(1, first); sendInventory(1, first); coordinator->frame(.01f);
    require(presentation.inventories == 1 && presentation.lastGroundCell == first);
    if (doorProgress)
    {
        const auto sendDoor = [&](uint64_t revision, CellId cell, float angle, uint8_t direction, bool blocked) {
            auto ground = groundItemBaseline(generation, revision, revision);
            ground.cell = cell;
            ground.nativeWorld = true;
            ground.doors = {{(uint64_t(1) << 63) | 50, 2, angle, 1.f / 30.f, direction, blocked}};
            wire->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableGroundItemBaseline,
                encodeReliableGroundItemBaseline(ground), TransportChannel::ReliableOrdered);
        };
        // Each committed angle arrives while the inventory/equipment and
        // movement lanes are still on an older revision. Never wait until idle.
        for (uint64_t revision = 2; revision <= 5; ++revision)
        {
            const float angle = float(revision - 1) * .1f;
            sendDoor(revision, first, angle, 1, false);
            coordinator->frame(.01f);
            require(!presentation.doorAngles.empty() && presentation.doorAngles.back() == angle);
            require(presentation.inventories == 1);
        }
        sendDoor(6, first, .4f, 1, true); coordinator->frame(.01f);
        sendDoor(7, first, .3f, 2, false); coordinator->frame(.01f);
        require(presentation.doorAngles.back() == .3f);
        const auto beforeTransition = presentation.doorAngles.size();
        sendDoor(8, second, .2f, 2, false); coordinator->frame(.01f);
        require(presentation.doorAngles.size() == beforeTransition);
        sendSpatial(8, second); coordinator->frame(.01f);
        require(presentation.doorAngles.back() == .2f);
        // An old cell cannot overwrite the destination's door presentation.
        const auto inDestination = presentation.doorAngles.size();
        sendDoor(9, first, .1f, 2, false); coordinator->frame(.01f);
        require(presentation.doorAngles.size() == inDestination);
        require(coordinator->multiplayerState() == MultiplayerState::Ready);
        std::cout << "PASS native-door-presentation: moving/blocked/reversed angles without inventory lane agreement; cell transitions guarded\n";
        return;
    }
    if (pickupBarrier)
    {
        // Reliable inventory and ground updates can span frames. Picking up
        // during that gap must retain the proposal instead of closing transport.
        wire->enqueue(MessageClass::ReliableOperation, MessageKind::ReliablePlayerInventoryBaseline,
            encodeReliablePlayerInventoryBaseline(playerInventoryBaseline(generation, 2, 2)),
            TransportChannel::ReliableOrdered);
        input.nextInventory = InventoryTransactionCapture{ .kind = InventoryTransactionKind::PickupItem,
            .prototypeId = value<ItemPrototypeId>(20), .stackId = value<ItemStackId>(12), .count = 1,
            .expectedInventoryRevision = value<InventoryRevision>(1), .interactionOrigin = Position3(0, 0, 0),
            .expectedWorldItemRevision = value<WorldItemRevision>(1) };
        const auto sentBeforePickup = wire->sentFrames.size();
        coordinator->frame(.01f);
        require(input.nextInventory && status.last != ConnectionStatus::TransportFailed);
        require(presentation.inventories == 1);
        sendSpatial(2, first); sendInventory(2, first); coordinator->frame(.01f);
        require(!input.nextInventory && status.last != ConnectionStatus::TransportFailed);
        require(presentation.inventories == 2);
        unsigned pickups = 0;
        for (size_t i = sentBeforePickup; i < wire->sentFrames.size(); ++i)
        {
            const auto frame = std::get<DecodedFrame>(decodeProtocolFrame(wire->sentFrames[i]));
            if (frame.messageKind() != MessageKind::ClientInventoryTransactionCommand) continue;
            const auto command = std::get<ClientInventoryTransactionCommand>(decodeClientInventoryTransactionCommand(frame.payload()));
            require(command.kind == InventoryTransactionKind::PickupItem
                && command.expectedInventoryRevision == value<InventoryRevision>(1)
                && command.expectedWorldItemRevision == value<WorldItemRevision>(1));
            ++pickups;
        }
        require(pickups == 1);
        std::cout << "PASS inventory-pickup-barrier: partial updates defer one revision-bound pickup\n";
        return;
    }
    // Spatial correction wins the race. The previous cell's complete inventory
    // must not be reinstalled while the destination baseline is still in flight.
    sendSpatial(2, second); coordinator->frame(.01f);
    require(presentation.inventories == 1 && presentation.lastAllowLocalCellCorrection && input.clearCalls == 1);
    const auto sent = wire->sentFrames.size();
    input.nextTransition = CellTransitionCapture{ProviderResult::Accepted, CellTransition(second)};
    sendInventory(2, second); coordinator->frame(.01f);
    require(presentation.inventories == 2 && presentation.lastGroundCell == second);
    for (size_t i = sent; i < wire->sentFrames.size(); ++i)
    {
        const auto decoded = std::get<DecodedFrame>(decodeProtocolFrame(wire->sentFrames[i]));
        if (decoded.messageKind() != MessageKind::ReliableOperation) continue;
        const auto command = std::get<ReliableOperation>(decodeReliableOperation(decoded.payload()));
        require(!std::holds_alternative<CellTransition>(command.body()));
    }
    // Reverse order on return: the baseline cannot install before its committed
    // spatial snapshot. Once both arrive the old activators are replaced.
    sendInventory(3, first); coordinator->frame(.01f);
    require(presentation.inventories == 2);
    sendSpatial(3, first); coordinator->frame(.01f);
    require(presentation.inventories == 3 && presentation.lastGroundCell == first && input.clearCalls == 2
        && coordinator->multiplayerState() == MultiplayerState::Ready);
    std::cout << "PASS teleport-presentation: delayed baselines and correction echo\n";
}

int main(int argc, char** argv)
{
    using namespace TES3MP;
    using namespace TES3MP::OpenMWAdapter;
    if (argc == 2 && (std::string_view(argv[1]) == "teleport-presentation"
        || std::string_view(argv[1]) == "inventory-pickup-barrier"
        || std::string_view(argv[1]) == "native-door-presentation"))
    {
        teleportPresentation(std::string_view(argv[1]) == "inventory-pickup-barrier",
            std::string_view(argv[1]) == "native-door-presentation");
        return 0;
    }

    const auto credentialDirectory = std::filesystem::temp_directory_path() / "tes3mp-openmw-player-credential-test";
    std::filesystem::remove_all(credentialDirectory);
    std::filesystem::create_directory(credentialDirectory);
    const auto credentialPath = credentialDirectory / "player.bin";
    std::array<std::byte, PlayerCredentialBytes> credentialBytes{};
    credentialBytes.fill(std::byte{ 0x5a });
    auto credentialStore = makeFilePlayerCredentialPersistence(credentialPath);
    require(credentialStore->store(std::move(*PlayerCredential::create(credentialBytes))));
    std::array<std::byte, PlayerCredentialBytes> storedCredential{};
    {
        std::ifstream stream(credentialPath, std::ios::binary);
        stream.read(reinterpret_cast<char*>(storedCredential.data()), storedCredential.size());
        require(stream && stream.peek() == std::char_traits<char>::eof());
    }
    require(storedCredential == credentialBytes);
#ifndef _WIN32
    const auto publicPermissions = std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    require(
        (std::filesystem::status(credentialPath).permissions() & publicPermissions) == std::filesystem::perms::none);
#endif
    std::filesystem::remove(credentialPath);
    std::filesystem::create_directory(credentialPath);
    require(!credentialStore->store(std::move(*PlayerCredential::create(credentialBytes))));
    auto credentialTemporaryPath = credentialPath;
    credentialTemporaryPath += ".tmp";
    require(!std::filesystem::exists(credentialTemporaryPath));
    std::filesystem::remove_all(credentialDirectory);

    const auto endpoint = *ConnectionEndpoint::create("127.0.0.1", 25560);
    const auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
    const auto outbound = *OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
    const ReconnectConfiguration reconnect{ endpoint, timeouts, outbound, testContentManifestId() };

    require(mapPlanarMovement(0, 0, 0).desiredVelocity() == LinearVelocity3(0, 0, 0));
    require(mapPlanarMovement(1, 0, 0).desiredVelocity() == LinearVelocity3(DesktopFixtureSpeedQuantaPerTick, 0, 0));
    require(mapPlanarMovement(0, 1, 0).desiredVelocity() == LinearVelocity3(0, DesktopFixtureSpeedQuantaPerTick, 0));
    require(mapPlanarMovement(1, 1, 0).desiredVelocity() == LinearVelocity3(2896, 2896, 0));
    require(mapPlanarMovement(0, 1, std::numbers::pi / 2).desiredVelocity()
        == LinearVelocity3(DesktopFixtureSpeedQuantaPerTick, 0, 0));
    require(
        mapPlanarMovement(0.5 / DesktopFixtureSpeedQuantaPerTick, 0, 0).desiredVelocity() == LinearVelocity3(0, 0, 0));
    require(
        mapPlanarMovement(1.5 / DesktopFixtureSpeedQuantaPerTick, 0, 0).desiredVelocity() == LinearVelocity3(2, 0, 0));
    require(
        mapPlanarMovement(std::numeric_limits<double>::infinity(), 1, 0).desiredVelocity() == LinearVelocity3(0, 0, 0));

    MotionMetrics motionMetrics;
    RemoteMotionBuffer remote(motionMetrics);
    require(remote.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(remote.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(33'333'333)));
    require(remote.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(66'666'667)));
    auto pose = remote.advance(MonotonicInstant::fromNanoseconds(83'333'334));
    require(pose && std::abs(pose->x - 2048.0) < 1.0);
    pose = remote.advance(MonotonicInstant::fromNanoseconds(100'000'000));
    require(pose && std::abs(pose->x - 4096.0) < 1.0);
    require(remote.observe(remoteSample(3, 4, 12'288), MonotonicInstant::fromNanoseconds(100'000'000)));
    require(remote.observe(remoteSample(4, 5, 16'384), MonotonicInstant::fromNanoseconds(133'333'334)));
    require(remote.sampleCount() == MaximumRemoteMotionSamples);

    RemoteMotionBuffer adaptive(motionMetrics);
    require(adaptive.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(adaptive.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(33'333'333)));
    require(adaptive.playbackDelayTicks() == RemotePlaybackDelayFloorTicks);
    require(adaptive.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(100'000'000)));
    require(adaptive.playbackDelayTicks() == RemotePlaybackDelayCeilingTicks);
    for (std::uint64_t tick = 3; tick < 7; ++tick)
        require(adaptive.observe(remoteSample(tick, tick + 2, static_cast<std::int64_t>(tick * 4096)),
            MonotonicInstant::fromNanoseconds(100'000'000 + (tick - 2) * 33'333'333)));
    require(adaptive.playbackDelayTicks() == RemotePlaybackDelayFloorTicks);

    auto locomotionPose = *remote.advance(MonotonicInstant::fromNanoseconds(166'666'667));
    require(remoteLocomotionAnimation(locomotionPose) == RemoteLocomotionAnimation::WalkRight);
    locomotionPose.velocity = LinearVelocity3(0, 0, 0);
    locomotionPose.locomotionMode = LocomotionMode::Sneak;
    require(remoteLocomotionAnimation(locomotionPose) == RemoteLocomotionAnimation::SneakIdle);
    locomotionPose.velocity = LinearVelocity3(0, 4096, 0);
    locomotionPose.locomotionMode = LocomotionMode::Run;
    require(remoteLocomotionAnimation(locomotionPose) == RemoteLocomotionAnimation::RunForward);
    locomotionPose.locomotionMode = LocomotionMode::Jump;
    require(remoteLocomotionAnimation(locomotionPose) == RemoteLocomotionAnimation::Jump);
    require(remote.observe(remoteSample(5, 6, 20'480), MonotonicInstant::fromNanoseconds(166'666'667)));
    require(remote.sampleCount() == MaximumRemoteMotionSamples);

    MotionMetrics extrapolationMetrics;
    RemoteMotionBuffer extrapolation(extrapolationMetrics);
    require(extrapolation.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(extrapolation.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(0)));
    require(extrapolation.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(0)));
    pose = extrapolation.advance(MonotonicInstant::fromNanoseconds(166'666'667));
    require(pose && std::abs(pose->x - 20'480.0) < 1.0);
    const auto held = extrapolation.advance(MonotonicInstant::fromNanoseconds(500'000'000));
    require(held && std::abs(held->x - pose->x) < 1.0);
    require(extrapolationMetrics.has(RemoteMotionMetricKey::SnapshotAgeNanoseconds));
    require(extrapolationMetrics.has(RemoteMotionMetricKey::BufferDepth));
    require(extrapolationMetrics.has(RemoteMotionMetricKey::ExtrapolationNanoseconds));

    MotionMetrics correctionMetrics;
    RemoteMotionBuffer correction(correctionMetrics);
    require(correction.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(correction.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(0)));
    require(correction.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(0)));
    pose = correction.advance(MonotonicInstant::fromNanoseconds(100'000'000));
    require(pose && std::abs(pose->x - 12'288.0) < 1.0);
    require(correction.observe(remoteSample(2, 4, 9216), MonotonicInstant::fromNanoseconds(100'000'000)));
    const auto continuous = correction.advance(MonotonicInstant::fromNanoseconds(100'000'000));
    require(continuous && std::abs(continuous->x - pose->x) < 1.0);
    const auto corrected = correction.advance(MonotonicInstant::fromNanoseconds(166'666'667));
    require(corrected && std::abs(corrected->x - 21'504.0) < 1.0);
    require(correctionMetrics.has(RemoteMotionMetricKey::CorrectionDistanceQuanta));

    const auto snapsBefore = correctionMetrics.count(RemoteMotionMetricKey::HardSnaps);
    require(correction.observe(remoteSample(2, 5, 40'000), MonotonicInstant::fromNanoseconds(166'666'667)));
    const auto snapped = correction.advance(MonotonicInstant::fromNanoseconds(166'666'667));
    require(snapped && snapped->x > corrected->x + RemoteHardSnapDistanceQuanta);
    require(correctionMetrics.count(RemoteMotionMetricKey::HardSnaps) == snapsBefore + 1);
    require(correction.observe(remoteSample(3, 6, 50'000, 0, 2), MonotonicInstant::fromNanoseconds(200'000'000)));
    require(correction.sampleCount() == 1);
    const auto discontinuity = correction.advance(MonotonicInstant::fromNanoseconds(200'000'000));
    require(discontinuity && discontinuity->x == 50'000.0);
    correction.clear();
    require(correction.sampleCount() == 0 && !correction.advance(MonotonicInstant::fromNanoseconds(300'000'000)));

    MotionMetrics clockMetrics;
    RemoteMotionBuffer clockRegression(clockMetrics);
    require(clockRegression.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(clockRegression.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(0)));
    require(clockRegression.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(0)));
    const auto beforeRegression = clockRegression.advance(MonotonicInstant::fromNanoseconds(33'333'334));
    const auto afterRegression = clockRegression.advance(MonotonicInstant::fromNanoseconds(20'000'000));
    require(beforeRegression && afterRegression && std::abs(beforeRegression->x - afterRegression->x) < 1.0);

    DroppingMotionMetrics droppingMetrics;
    RemoteMotionBuffer dropsDoNotChangePresentation(droppingMetrics);
    require(dropsDoNotChangePresentation.observe(remoteSample(0, 1, 7), MonotonicInstant::fromNanoseconds(0)));
    require(dropsDoNotChangePresentation.advance(MonotonicInstant::fromNanoseconds(0))->x == 7.0);

    require(movementCorrectionDistanceQuanta(Position3(3, 4, 0), 0, 0, 0) == 5);
    require(movementCorrectionDistanceQuanta(Position3(0, 0, 0), std::numeric_limits<double>::infinity(), 0, 0)
        == std::numeric_limits<std::uint64_t>::max());

    MotionMetrics trackerMetrics;
    MotionIntentTracker motion(&trackerMetrics);
    const auto moving = LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(10, 0, 0));
    const auto stopped = LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(0, 0, 0));
    motion.sample(moving, MonotonicInstant::fromNanoseconds(90));
    require(motion.next(LinearVelocity3(0, 0, 0)).has_value());
    require(motion.markQueued(CommandSequence::initial(), moving, MonotonicInstant::fromNanoseconds(100))
        && motion.pending());
    motion.sample(stopped, MonotonicInstant::fromNanoseconds(125));
    require(!motion.next(LinearVelocity3(0, 0, 0)));
    motion.observeAcknowledgement(CommandSequence::initial(), MonotonicInstant::fromNanoseconds(175));
    require(!motion.pending());
    require(trackerMetrics.has(MovementMetricKey::CommandAcknowledgementNanoseconds));
    require(motion.next(LinearVelocity3(10, 0, 0))->desiredVelocity() == LinearVelocity3(0, 0, 0));
    require(motion.markQueued(*CommandSequence::initial().next(), stopped, MonotonicInstant::fromNanoseconds(200)));
    motion.observeAcknowledgement(*CommandSequence::initial().next(), MonotonicInstant::fromNanoseconds(300));
    require(!motion.next(LinearVelocity3(0, 0, 0)));
    require(trackerMetrics.has(MovementMetricKey::StopAcknowledgementNanoseconds));
    require(trackerMetrics.maximum(MovementMetricKey::StopAcknowledgementNanoseconds) == 175);

    PoseEvidenceTracker poseEvidence(&trackerMetrics);
    poseEvidence.observe(value<EntityId>(9), AuthorityEpoch::initial(), PoseSampleSequence::initial(),
        MonotonicInstant::fromNanoseconds(400));
    require(poseEvidence.poseWeight(value<EntityId>(9), AuthorityEpoch::initial(),
                MonotonicInstant::fromNanoseconds(400 + RemotePoseFreshNanoseconds))
        == 1.0);
    const double blendingPoseWeight = poseEvidence.poseWeight(value<EntityId>(9), AuthorityEpoch::initial(),
        MonotonicInstant::fromNanoseconds(400 + RemotePoseFreshNanoseconds + RemotePoseFallbackBlendNanoseconds / 2));
    require(blendingPoseWeight > 0.49 && blendingPoseWeight < 0.51);
    require(
        poseEvidence.poseWeight(value<EntityId>(9), AuthorityEpoch::initial(),
            MonotonicInstant::fromNanoseconds(400 + RemotePoseFreshNanoseconds + RemotePoseFallbackBlendNanoseconds))
        == 0.0);
    require(
        poseEvidence.poseWeight(value<EntityId>(10), AuthorityEpoch::initial(), MonotonicInstant::fromNanoseconds(400))
        == 0.0);
    poseEvidence.advance(MonotonicInstant::fromNanoseconds(425));
    poseEvidence.observe(value<EntityId>(9), AuthorityEpoch::initial(), value<PoseSampleSequence>(3),
        MonotonicInstant::fromNanoseconds(450));
    require(trackerMetrics.has(MovementMetricKey::PoseAgeNanoseconds));
    require(trackerMetrics.has(MovementMetricKey::PoseLostSamples));
    const auto poseAges = trackerMetrics.count(MovementMetricKey::PoseAgeNanoseconds);
    poseEvidence.retain({});
    poseEvidence.advance(MonotonicInstant::fromNanoseconds(500));
    require(trackerMetrics.count(MovementMetricKey::PoseAgeNanoseconds) == poseAges);
    poseEvidence.clear();

    BoundedMovementMetricSink bounded(1);
    require(bounded.tryRecord({ MovementMetricKey::BufferDepth, 3 }) == ObservationResult::Accepted);
    require(bounded.tryRecord({ MovementMetricKey::BufferDepth, 4 }) == ObservationResult::Dropped);
    require((bounded.summary(MovementMetricKey::BufferDepth) == MovementMetricSummary{ 1, 3, 3, 3 }));
    require(bounded.acceptedCount() == 1 && bounded.droppedCount() == 1
        && std::string_view(movementMetricName(MovementMetricKey::BufferDepth)) == "remote_buffer_depth");

    {
        const std::array<GlobalVariableCatalogEntry, 0> globals{};
        const auto globalCatalog = GlobalVariableCatalog::create(globals).value();
        const std::array quests{ QuestCatalogEntry{
            value<QuestId>(10), value<QuestStage>(0), { value<QuestStage>(0), value<QuestStage>(20) } } };
        const std::array journal{ JournalCatalogEntry{
            value<JournalEntryId>(100), value<QuestId>(10), value<QuestStage>(20) } };
        const auto catalog = QuestJournalCatalog::create(testContentManifestId(), quests, journal).value();
        CanonicalWorldTimeState time;
        auto committed = CanonicalWorldState::initial(time, globalCatalog, catalog).value();
        auto quest = setCanonicalQuestStage(committed, value<PlayerId>(1), value<QuestId>(10), QuestRevision::initial(),
            value<QuestStage>(20), value<ServerTick>(3));
        committed = std::get<CanonicalWorldState>(std::move(quest));
        auto entry = addCanonicalJournalEntry(committed, value<PlayerId>(1), value<QuestId>(10),
            JournalRevision::initial(), value<JournalEntryId>(100), value<ServerTick>(3));
        committed = std::get<CanonicalWorldState>(std::move(entry));
        Presentation questPresentation;
        require(applyCommittedQuestJournal(
                    questPresentation, committed, value<PlayerId>(1), MonotonicInstant::fromNanoseconds(1))
            == ProviderResult::Accepted);
        require(questPresentation.questJournals == 1 && questPresentation.lastQuestCatalogSize == 1
            && questPresentation.lastQuestJournal && questPresentation.lastQuestJournal->quests.size() == 1
            && questPresentation.lastQuestJournal->journal.size() == 1);
    }

    Input input;
    auto intent = input.sampleCurrentIntent();
    require(input.calls == 1 && intent && intent->desiredVelocity() == TES3MP::LinearVelocity3(1, 2, 3));
    Presentation presentation;
    Status status;
    Coordinator coordinator;
    coordinator.frame(0.25f);
    require(coordinator.calls == 1 && coordinator.lastDuration == 0.25f);
    require(!TES3MP::OpenMWAdapter::makeCoordinator({}, {}, {}, reconnect, input, presentation, status));

    auto transport = std::make_unique<IdleTransport>();
    auto* transportObserver = transport.get();
    auto clock = std::make_unique<Clock>();
    auto created = ClientSessionRuntime::create(*transport, *clock, timeouts, SessionGeneration::initial(), outbound);
    auto runtime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(created));
    auto liveCoordinator = makeCoordinator(
        std::move(transport), std::move(clock), std::move(runtime), reconnect, input, presentation, status);
    require(static_cast<bool>(liveCoordinator));
    liveCoordinator->frame(0.01f);
    require(presentation.advances == 0);
    transportObserver->failPoll = true;
    liveCoordinator->frame(0.01f);
    require(presentation.clears == 1 && status.last == ConnectionStatus::Disconnected);
    require(liveCoordinator->confirmedCharacterProfile() == nullptr);
    liveCoordinator.reset();
    require(presentation.clears == 2);

    Input rejectedInput;
    Presentation rejectedPresentation;
    Status rejectedStatus;
    auto rejectedTransport = std::make_unique<IdleTransport>();
    auto* rejectedTransportObserver = rejectedTransport.get();
    rejectedTransportObserver->acceptConnections = true;
    auto rejectedClock = std::make_unique<Clock>();
    auto rejectedCreated = ClientSessionRuntime::create(
        *rejectedTransport, *rejectedClock, timeouts, SessionGeneration::initial(), outbound);
    auto rejectedRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(rejectedCreated));
    auto rejectedVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
    auto rejectedClientOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(rejectedVersions, {}, {}, testContentManifestId()));
    auto rejectedMaterial = AuthenticationMaterial::create({});
    require(rejectedRuntime->start(endpoint, ClientHello::fromOffer(std::move(rejectedClientOffer)),
                AuthenticationRequest::join(std::move(*rejectedMaterial)))
        == HeadlessClientResult::Accepted);
    auto rejectedCoordinator = makeCoordinator(std::move(rejectedTransport), std::move(rejectedClock),
        std::move(rejectedRuntime), reconnect, rejectedInput, rejectedPresentation, rejectedStatus);
    rejectedCoordinator->frame(0.01f);
    std::array<std::byte, ContentManifestIdBytes> mismatchedManifestBytes{};
    mismatchedManifestBytes.fill(std::byte{ 1 });
    const auto mismatchedManifest = *ContentManifestId::fromBytes(mismatchedManifestBytes);
    auto serverVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
    auto serverOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(serverVersions), {}, {}, mismatchedManifest));
    auto clientVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
    auto clientOffer = std::get<CapabilityOffer>(
        CapabilityOffer::create(std::move(clientVersions), {}, {}, testContentManifestId()));
    const auto negotiation = negotiateClientHello(ClientHello::fromOffer(std::move(clientOffer)), serverOffer);
    require(std::holds_alternative<SessionRejected>(negotiation));
    auto rejectionFrame = encodeProtocolFrame(MessageClass::SessionControl, MessageKind::SessionRejected,
        encodeSessionRejected(std::get<SessionRejected>(negotiation)));
    rejectedTransportObserver->inbound.push_back(
        { TransportChannel::ReliableOrdered, std::get<std::vector<std::byte>>(std::move(rejectionFrame)) });
    rejectedCoordinator->frame(0.01f);
    require(rejectedStatus.last == ConnectionStatus::ContentManifestMismatch
        && rejectedCoordinator->multiplayerState() == MultiplayerState::Failed);

    Input authRejectedInput;
    Presentation authRejectedPresentation;
    Status authRejectedStatus;
    auto authRejectedTransport = std::make_unique<IdleTransport>();
    auto* authRejectedTransportObserver = authRejectedTransport.get();
    authRejectedTransportObserver->acceptConnections = true;
    auto authRejectedClock = std::make_unique<Clock>();
    auto authRejectedCreated = ClientSessionRuntime::create(
        *authRejectedTransport, *authRejectedClock, timeouts, SessionGeneration::initial(), outbound);
    auto authRejectedRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(authRejectedCreated));
    auto authRejectedVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
    auto authRejectedClientOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(authRejectedVersions, {}, {}, testContentManifestId()));
    const std::array dummyPasswordBytes{ std::byte{ 1 } };
    auto authRejectedMaterial = AuthenticationMaterial::create(dummyPasswordBytes);
    require(authRejectedMaterial
        && authRejectedRuntime->start(endpoint, ClientHello::fromOffer(std::move(authRejectedClientOffer)),
               AuthenticationRequest::join(std::move(*authRejectedMaterial)))
            == HeadlessClientResult::Accepted);
    auto authRejectedCoordinator = makeCoordinator(std::move(authRejectedTransport), std::move(authRejectedClock),
        std::move(authRejectedRuntime), reconnect, authRejectedInput, authRejectedPresentation, authRejectedStatus);
    authRejectedCoordinator->frame(0.01f);
    authRejectedTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello()), TransportChannel::ReliableOrdered);
    authRejectedCoordinator->frame(0.01f);
    authRejectedTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationRejected,
        encodeAuthenticationRejected({ AuthenticationPublicRejection::Denied }), TransportChannel::ReliableOrdered);
    authRejectedTransportObserver->failPoll = true;
    authRejectedCoordinator->frame(0.01f);
    require(authRejectedStatus.last == ConnectionStatus::AuthenticationRejected
        && authRejectedCoordinator->multiplayerState() == MultiplayerState::Failed);

    Input concurrentProtocolRejectedInput;
    Presentation concurrentProtocolRejectedPresentation;
    Status concurrentProtocolRejectedStatus;
    auto concurrentProtocolRejectedTransport = std::make_unique<IdleTransport>();
    auto* concurrentProtocolRejectedTransportObserver = concurrentProtocolRejectedTransport.get();
    concurrentProtocolRejectedTransportObserver->acceptConnections = true;
    auto concurrentProtocolRejectedClock = std::make_unique<Clock>();
    auto concurrentProtocolRejectedCreated = ClientSessionRuntime::create(*concurrentProtocolRejectedTransport,
        *concurrentProtocolRejectedClock, timeouts, SessionGeneration::initial(), outbound);
    auto concurrentProtocolRejectedRuntime
        = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(concurrentProtocolRejectedCreated));
    auto concurrentProtocolRejectedVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
    auto concurrentProtocolRejectedOffer = std::get<CapabilityOffer>(
        CapabilityOffer::create(std::move(concurrentProtocolRejectedVersions), {}, {}, testContentManifestId()));
    auto concurrentProtocolRejectedPassword = AuthenticationMaterial::create(dummyPasswordBytes);
    require(concurrentProtocolRejectedPassword
        && concurrentProtocolRejectedRuntime->start(endpoint,
               ClientHello::fromOffer(std::move(concurrentProtocolRejectedOffer)),
               AuthenticationRequest::join(std::move(*concurrentProtocolRejectedPassword)))
            == HeadlessClientResult::Accepted);
    auto concurrentProtocolRejectedCoordinator = makeCoordinator(std::move(concurrentProtocolRejectedTransport),
        std::move(concurrentProtocolRejectedClock), std::move(concurrentProtocolRejectedRuntime), reconnect,
        concurrentProtocolRejectedInput, concurrentProtocolRejectedPresentation, concurrentProtocolRejectedStatus);
    concurrentProtocolRejectedCoordinator->frame(0.01f);
    auto concurrentRejectionFrame = encodeProtocolFrame(MessageClass::SessionControl, MessageKind::SessionRejected,
        encodeSessionRejected(std::get<SessionRejected>(negotiation)));
    concurrentProtocolRejectedTransportObserver->inbound.push_back(
        { TransportChannel::ReliableOrdered, std::get<std::vector<std::byte>>(std::move(concurrentRejectionFrame)) });
    concurrentProtocolRejectedTransportObserver->failPoll = true;
    concurrentProtocolRejectedCoordinator->frame(0.01f);
    require(concurrentProtocolRejectedStatus.last == ConnectionStatus::ContentManifestMismatch
        && concurrentProtocolRejectedCoordinator->multiplayerState() == MultiplayerState::Failed);

    Input reconnectInput;
    Presentation reconnectPresentation;
    Status reconnectStatus;
    DisconnectOnce disconnect;
    PoseInput poseInput;
    auto reconnectTransport = std::make_unique<IdleTransport>();
    auto* reconnectTransportObserver = reconnectTransport.get();
    reconnectTransportObserver->acceptConnections = true;
    auto reconnectClock = std::make_unique<Clock>();
    auto* reconnectClockObserver = reconnectClock.get();
    auto reconnectCreated = ClientSessionRuntime::create(
        *reconnectTransport, *reconnectClock, timeouts, SessionGeneration::initial(), outbound);
    auto reconnectRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(reconnectCreated));
    auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 2));
    const std::array poseCapabilities{ vrPoseCapability(), actorReplicationCapability() };
    auto offer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(versions), poseCapabilities, {}));
    const std::array passwordBytes{ std::byte{ 1 } };
    auto password = AuthenticationMaterial::create(passwordBytes);
    require(password
        && reconnectRuntime->start(
               endpoint, ClientHello::fromOffer(std::move(offer)), AuthenticationRequest::join(std::move(*password)))
            == HeadlessClientResult::Accepted);
    auto credentialPersistence = std::make_unique<RecordingPlayerCredentialPersistence>();
    auto* credentialPersistenceObserver = credentialPersistence.get();
    auto reconnectCoordinator = makeCoordinator(std::move(reconnectTransport), std::move(reconnectClock),
        std::move(reconnectRuntime), reconnect, reconnectInput, reconnectPresentation, reconnectStatus, &disconnect,
        &poseInput, std::move(credentialPersistence), &trackerMetrics);
    reconnectCoordinator->frame(0.01f);
    auto helloPayload = encodeServerHello(serverHello(true, true));
    reconnectTransportObserver->enqueue(
        MessageClass::SessionControl, MessageKind::ServerHello, helloPayload, TransportChannel::ReliableOrdered);
    reconnectCoordinator->frame(0.01f);
    auto initialAccepted = accepted(std::byte{ 2 }, 2 * MinimumResumeTokenLifetimeMilliseconds, true);
    auto initialAcceptedPayload = encodeAuthenticationAccepted(initialAccepted);
    reconnectTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        initialAcceptedPayload, TransportChannel::ReliableOrdered);
    reconnectTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial(), true)),
        TransportChannel::ReliableOrdered);
    auto initialSnapshot = selfSnapshot(SessionGeneration::initial(), true);
    auto initialSnapshotPayload = encodeLatestWinsSnapshot(initialSnapshot);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        initialSnapshotPayload, TransportChannel::LatestWins);
    reconnectTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableActorInterestBaseline,
        encodeReliableActorInterestBaseline(actorBaseline(SessionGeneration::initial(), 1, 1)),
        TransportChannel::ReliableOrdered);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsActorSnapshot,
        encodeLatestWinsActorSnapshot(actorSnapshot(SessionGeneration::initial(), 1, 1)), TransportChannel::LatestWins);
    const auto zero = Turn32::fromValue(0);
    const auto head = VrTrackedTransform(*VrPoseOffset3::create(4, 5, 6), Orientation3(zero, zero, zero));
    const auto remotePose = ServerVrPoseSnapshot(value<SessionId>(1), SessionGeneration::initial(), value<PlayerId>(2),
        value<SessionId>(1), SessionGeneration::initial(), value<EntityId>(2), AuthorityEpoch::initial(),
        PoseSampleSequence::initial(), head, std::nullopt, std::nullopt);
    reconnectTransportObserver->enqueue(MessageClass::PresentationSample, MessageKind::ServerVrPoseSnapshot,
        encodeServerVrPoseSnapshot(remotePose), TransportChannel::PresentationLatest);
    reconnectCoordinator->frame(0.01f);
    require(reconnectPresentation.calls == 1 && reconnectPresentation.actors == 1 && reconnectPresentation.poses == 1
        && reconnectPresentation.poseFallbacks == 1 && reconnectPresentation.lastPoseWeight == 1.0
        && poseInput.calls == 1 && credentialPersistenceObserver->calls == 1
        && credentialPersistenceObserver->bytes.front() == std::byte{ 2 });
    require(trackerMetrics.has(MovementMetricKey::PoseAgeNanoseconds));
    require(reconnectTransportObserver->sentChannel == TransportChannel::PresentationLatest);
    const auto sentPoseFrame = decodeProtocolFrame(reconnectTransportObserver->sent);
    require(std::holds_alternative<DecodedFrame>(sentPoseFrame)
        && std::get<DecodedFrame>(sentPoseFrame).messageKind() == MessageKind::ClientVrPoseSample);

    reconnectTransportObserver->acceptConnections = false;
    reconnectCoordinator->frame(0.01f);
    require(reconnectPresentation.clears == 1 && reconnectStatus.last == ConnectionStatus::Reconnecting);
    reconnectTransportObserver->acceptConnections = true;
    reconnectClockObserver->nanoseconds = 999'999'999;
    reconnectCoordinator->frame(0.01f);
    require(!reconnectTransportObserver->pendingAttempt && reconnectTransportObserver->nextConnection == 2);
    reconnectClockObserver->nanoseconds = 1'000'000'000;
    reconnectCoordinator->frame(0.01f);
    require(reconnectTransportObserver->pendingAttempt && reconnectTransportObserver->nextConnection == 2);
    reconnectCoordinator->frame(0.01f);
    reconnectTransportObserver->enqueue(
        MessageClass::SessionControl, MessageKind::ServerHello, helloPayload, TransportChannel::ReliableOrdered);
    reconnectCoordinator->frame(0.01f);
    auto sentResumeFrame = decodeProtocolFrame(reconnectTransportObserver->sent);
    require(std::holds_alternative<DecodedFrame>(sentResumeFrame));
    auto sentResume = decodeAuthenticationRequest(std::get<DecodedFrame>(sentResumeFrame).payload());
    require(std::holds_alternative<AuthenticationRequest>(sentResume)
        && std::get<AuthenticationRequest>(std::move(sentResume)).kind() == AuthenticationCredentialKind::ResumeToken);
    auto rotatedAccepted = accepted(std::byte{ 3 });
    auto rotatedAcceptedPayload = encodeAuthenticationAccepted(rotatedAccepted);
    reconnectTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        rotatedAcceptedPayload, TransportChannel::ReliableOrdered);
    reconnectTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(*SessionGeneration::initial().next(), true)),
        TransportChannel::ReliableOrdered);
    auto resumedSnapshot = selfSnapshot(*SessionGeneration::initial().next(), true);
    auto resumedSnapshotPayload = encodeLatestWinsSnapshot(resumedSnapshot);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        resumedSnapshotPayload, TransportChannel::LatestWins);
    reconnectTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableActorInterestBaseline,
        encodeReliableActorInterestBaseline(actorBaseline(*SessionGeneration::initial().next(), 1, 1)),
        TransportChannel::ReliableOrdered);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsActorSnapshot,
        encodeLatestWinsActorSnapshot(actorSnapshot(*SessionGeneration::initial().next(), 1, 1)),
        TransportChannel::LatestWins);
    reconnectCoordinator->frame(0.01f);
    require(reconnectPresentation.calls == 2 && reconnectPresentation.actors == 2
        && reconnectPresentation.poseFallbacks == 2 && reconnectPresentation.lastPoseWeight == 0.0
        && reconnectStatus.last == ConnectionStatus::Resumed);

    disconnect.resyncPending = true;
    const auto sentBeforeResync = reconnectTransportObserver->sentFrames.size();
    reconnectCoordinator->frame(0.01f);
    bool sentResync = false;
    for (std::size_t index = sentBeforeResync; index < reconnectTransportObserver->sentFrames.size(); ++index)
    {
        const auto decoded = decodeProtocolFrame(reconnectTransportObserver->sentFrames[index]);
        sentResync = sentResync
            || (std::holds_alternative<DecodedFrame>(decoded)
                && std::get<DecodedFrame>(decoded).messageKind() == MessageKind::SessionResyncRequest);
    }
    require(sentResync && disconnect.resyncCompletions == 0);
    reconnectTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableActorInterestBaseline,
        encodeReliableActorInterestBaseline(actorBaseline(*SessionGeneration::initial().next(), 1, 2)),
        TransportChannel::ReliableOrdered);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsActorSnapshot,
        encodeLatestWinsActorSnapshot(actorSnapshot(*SessionGeneration::initial().next(), 1, 2)),
        TransportChannel::LatestWins);
    reconnectCoordinator->frame(0.01f);
    require(disconnect.resyncCompletions == 0 && reconnectPresentation.calls == 2 && reconnectPresentation.actors == 3);
    reconnectTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(*SessionGeneration::initial().next(), true, 1, 1)),
        TransportChannel::ReliableOrdered);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(*SessionGeneration::initial().next(), true, 1, 1)),
        TransportChannel::LatestWins);
    reconnectCoordinator->frame(0.01f);
    require(disconnect.resyncCompletions == 1 && reconnectPresentation.calls == 3 && reconnectPresentation.actors == 4);

    // Verify unnegotiated interactive object capability does not queue commands or capture interactions
    const auto sentBeforeUnneg = reconnectTransportObserver->sentFrames.size();
    const auto unnegInteractionsBefore = reconnectInput.interactionCalls;
    reconnectInput.nextInteraction = ObjectInteractionCapture{ *InteractiveObjectId::fromValue(101),
        CellId::interior(value<CellSpaceId>(7)), Position3(100, 200, 300), *ObjectRevision::fromValue(1) };
    reconnectCoordinator->frame(0.01f);
    require(reconnectInput.interactionCalls == unnegInteractionsBefore);
    for (std::size_t index = sentBeforeUnneg; index < reconnectTransportObserver->sentFrames.size(); ++index)
    {
        const auto decoded = decodeProtocolFrame(reconnectTransportObserver->sentFrames[index]);
        if (std::holds_alternative<DecodedFrame>(decoded))
        {
            require(std::get<DecodedFrame>(decoded).messageKind() != MessageKind::ClientInteractObjectCommand);
        }
    }

    Input objInput;
    Presentation objPresentation;
    Status objStatus;
    DisconnectOnce objDisconnect;
    objDisconnect.pending = false;
    auto objTransport = std::make_unique<IdleTransport>();
    auto* objTransportObserver = objTransport.get();
    objTransportObserver->acceptConnections = true;
    auto objClock = std::make_unique<Clock>();
    auto objCreated
        = ClientSessionRuntime::create(*objTransport, *objClock, timeouts, SessionGeneration::initial(), outbound);
    auto objRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(objCreated));
    auto objVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 2));
    const std::array objCapabilities{ interactiveObjectReplicationCapability() };
    auto objOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(objVersions), objCapabilities, {}));
    auto objPassword = AuthenticationMaterial::create(passwordBytes);
    require(objPassword
        && objRuntime->start(endpoint, ClientHello::fromOffer(std::move(objOffer)),
               AuthenticationRequest::join(std::move(*objPassword)))
            == HeadlessClientResult::Accepted);
    auto objCoordinator = makeCoordinator(std::move(objTransport), std::move(objClock), std::move(objRuntime),
        reconnect, objInput, objPresentation, objStatus, &objDisconnect);
    require(static_cast<bool>(objCoordinator));
    auto objHello = encodeServerHello(serverHello(false, false, true));
    objTransportObserver->enqueue(
        MessageClass::SessionControl, MessageKind::ServerHello, objHello, TransportChannel::ReliableOrdered);
    objCoordinator->frame(0.01f);
    auto objAccepted = accepted(std::byte{ 5 });
    objTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(objAccepted), TransportChannel::ReliableOrdered);
    objTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial(), true, 1, 1)),
        TransportChannel::ReliableOrdered);
    objTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial(), true, 1, 1)), TransportChannel::LatestWins);
    objTransportObserver->enqueue(MessageClass::ReliableOperation,
        MessageKind::ReliableInteractiveObjectInterestBaseline,
        encodeReliableInteractiveObjectInterestBaseline(interactiveObjectBaseline(SessionGeneration::initial(), 1, 1)),
        TransportChannel::ReliableOrdered);
    objCoordinator->frame(0.01f);
    require(objPresentation.calls == 1 && objPresentation.interactiveObjects == 1);
    require(
        objPresentation.observedObjectRevision(*InteractiveObjectId::fromValue(101)) == *ObjectRevision::fromValue(1));

    // Test queueing object activation when capability is negotiated
    objInput.nextInteraction = ObjectInteractionCapture{ *InteractiveObjectId::fromValue(101),
        CellId::interior(value<CellSpaceId>(7)), Position3(100, 200, 300), *ObjectRevision::fromValue(1) };
    const auto sentBeforeInteract = objTransportObserver->sentFrames.size();
    objCoordinator->frame(0.01f);
    require(objInput.interactionCalls >= 1);
    require(objTransportObserver->sentFrames.size() > sentBeforeInteract);

    bool foundInteractCommand = false;
    for (std::size_t index = sentBeforeInteract; index < objTransportObserver->sentFrames.size(); ++index)
    {
        const auto decoded = decodeProtocolFrame(objTransportObserver->sentFrames[index]);
        if (std::holds_alternative<DecodedFrame>(decoded))
        {
            const auto& frame = std::get<DecodedFrame>(decoded);
            if (frame.messageKind() == MessageKind::ClientInteractObjectCommand)
            {
                auto decodedCmd = decodeClientInteractObjectCommand(frame.payload());
                require(std::holds_alternative<ClientInteractObjectCommand>(decodedCmd));
                const auto& cmd = std::get<ClientInteractObjectCommand>(decodedCmd);
                require(cmd.objectId == *InteractiveObjectId::fromValue(101));
                require(cmd.targetCell == CellId::interior(value<CellSpaceId>(7)));
                require(cmd.interactionOrigin == Position3(100, 200, 300));
                require(cmd.expectedRevision == *ObjectRevision::fromValue(1));
                require(cmd.kind == ObjectInteractionKind::Activate);
                foundInteractCommand = true;
            }
        }
    }
    require(foundInteractCommand);

    objDisconnect.resyncPending = true;
    objCoordinator->frame(0.01f);
    require(objDisconnect.resyncCompletions == 0);
    objTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial(), true, 2, 2)),
        TransportChannel::ReliableOrdered);
    objTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial(), true, 2, 2)), TransportChannel::LatestWins);
    objCoordinator->frame(0.01f);
    require(objDisconnect.resyncCompletions == 0);
    require(objPresentation.calls == 2);
    require(objPresentation.interactiveObjects == 1);
    objTransportObserver->enqueue(MessageClass::ReliableOperation,
        MessageKind::ReliableInteractiveObjectInterestBaseline,
        encodeReliableInteractiveObjectInterestBaseline(interactiveObjectBaseline(SessionGeneration::initial(), 2, 2)),
        TransportChannel::ReliableOrdered);
    objCoordinator->frame(0.01f);
    require(objDisconnect.resyncCompletions == 1 && objPresentation.interactiveObjects == 2);
    require(
        objPresentation.observedObjectRevision(*InteractiveObjectId::fromValue(101)) == *ObjectRevision::fromValue(2));

    // Verify revision gating: object baseline with revision > snapshot revision is not applied to presentation
    objTransportObserver->enqueue(MessageClass::ReliableOperation,
        MessageKind::ReliableInteractiveObjectInterestBaseline,
        encodeReliableInteractiveObjectInterestBaseline(interactiveObjectBaseline(SessionGeneration::initial(), 3, 5)),
        TransportChannel::ReliableOrdered);
    objCoordinator->frame(0.01f);
    require(objPresentation.interactiveObjects == 2);

    // A newer baseline may not regress an individual object's revision.
    objTransportObserver->enqueue(MessageClass::ReliableOperation,
        MessageKind::ReliableInteractiveObjectInterestBaseline,
        encodeReliableInteractiveObjectInterestBaseline(
            interactiveObjectBaseline(SessionGeneration::initial(), 4, 6, 4)),
        TransportChannel::ReliableOrdered);
    objCoordinator->frame(0.01f);
    require(objStatus.last == ConnectionStatus::Reconnecting && objPresentation.interactiveObjects == 2);
    require(objInput.clearCalls == 1);

    // Inventory views apply only after the complete private/container/ground/equipment set, then UI proposals
    // leave as one authenticated transaction command.
    Input inventoryInput;
    Presentation inventoryPresentation;
    Status inventoryStatus;
    DisconnectOnce inventoryControl;
    inventoryControl.pending = false;
    auto inventoryTransport = std::make_unique<IdleTransport>();
    auto* inventoryTransportObserver = inventoryTransport.get();
    inventoryTransportObserver->acceptConnections = true;
    auto inventoryClock = std::make_unique<Clock>();
    auto inventoryCreated = ClientSessionRuntime::create(
        *inventoryTransport, *inventoryClock, timeouts, SessionGeneration::initial(), outbound);
    auto inventoryRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(inventoryCreated));
    auto inventoryVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 2));
    const std::array inventoryCapabilities{ inventoryReplicationCapability() };
    auto inventoryOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(inventoryVersions), inventoryCapabilities, {}));
    auto inventoryPassword = AuthenticationMaterial::create(passwordBytes);
    require(inventoryPassword
        && inventoryRuntime->start(endpoint, ClientHello::fromOffer(std::move(inventoryOffer)),
               AuthenticationRequest::join(std::move(*inventoryPassword)))
            == HeadlessClientResult::Accepted);
    auto inventoryCoordinator
        = makeCoordinator(std::move(inventoryTransport), std::move(inventoryClock), std::move(inventoryRuntime),
            reconnect, inventoryInput, inventoryPresentation, inventoryStatus, &inventoryControl);
    inventoryCoordinator->frame(0.01f);
    inventoryTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello(false, false, false, true)), TransportChannel::ReliableOrdered);
    inventoryCoordinator->frame(0.01f);
    auto inventoryAccepted = accepted(std::byte{ 7 });
    inventoryTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(inventoryAccepted), TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial(), true)),
        TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial(), true)), TransportChannel::LatestWins);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliablePlayerInventoryBaseline,
        encodeReliablePlayerInventoryBaseline(playerInventoryBaseline(SessionGeneration::initial(), 1, 1, 0, 2)),
        TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation,
        MessageKind::ReliableContainerInventoryBaseline,
        encodeReliableContainerInventoryBaseline(containerInventoryBaseline(SessionGeneration::initial(), 1, 1)),
        TransportChannel::ReliableOrdered);
    inventoryCoordinator->frame(0.01f);
    require(inventoryPresentation.inventories == 0);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliablePlayerInventoryBaseline,
        encodeReliablePlayerInventoryBaseline(playerInventoryBaseline(SessionGeneration::initial(), 1, 1, 1, 2)),
        TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableGroundItemBaseline,
        encodeReliableGroundItemBaseline(groundItemBaseline(SessionGeneration::initial(), 1, 1)),
        TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsEquipmentSnapshot,
        encodeLatestWinsEquipmentSnapshot(equipmentSnapshot(SessionGeneration::initial(), 1, 1)),
        TransportChannel::LatestWins);
    inventoryCoordinator->frame(0.01f);
    require(inventoryPresentation.inventories == 1 && inventoryPresentation.lastContainerCount == 1
        && inventoryPresentation.lastGroundCount == 0 && inventoryPresentation.lastEquipmentCount == 1);

    inventoryInput.nextInventory = InventoryTransactionCapture{ .kind = InventoryTransactionKind::TakeFromContainer,
        .prototypeId = value<ItemPrototypeId>(20),
        .stackId = value<ItemStackId>(12),
        .count = 1,
        .expectedInventoryRevision = value<InventoryRevision>(1),
        .interactionOrigin = Position3(0, 0, 0),
        .containerId = value<ContainerId>(30),
        .expectedContainerRevision = value<ContainerRevision>(1) };
    const auto sentBeforeInventory = inventoryTransportObserver->sentFrames.size();
    inventoryCoordinator->frame(0.01f);
    bool foundInventoryCommand = false;
    for (std::size_t index = sentBeforeInventory; index < inventoryTransportObserver->sentFrames.size(); ++index)
    {
        auto decoded = decodeProtocolFrame(inventoryTransportObserver->sentFrames[index]);
        auto* commandFrame = std::get_if<DecodedFrame>(&decoded);
        if (!commandFrame || commandFrame->messageKind() != MessageKind::ClientInventoryTransactionCommand)
            continue;
        auto decodedCommand = decodeClientInventoryTransactionCommand(commandFrame->payload());
        auto* command = std::get_if<ClientInventoryTransactionCommand>(&decodedCommand);
        foundInventoryCommand = command && command->kind == InventoryTransactionKind::TakeFromContainer
            && command->containerId == value<ContainerId>(30) && command->stackId == value<ItemStackId>(12)
            && command->expectedInventoryRevision == value<InventoryRevision>(1)
            && command->expectedContainerRevision == value<ContainerRevision>(1);
    }
    require(foundInventoryCommand);

    inventoryControl.resyncPending = true;
    inventoryCoordinator->frame(0.01f);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial(), true)),
        TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliablePlayerInventoryBaseline,
        encodeReliablePlayerInventoryBaseline(playerInventoryBaseline(SessionGeneration::initial(), 1, 1, 0, 2)),
        TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableGroundItemBaseline,
        encodeReliableGroundItemBaseline(groundItemBaseline(SessionGeneration::initial(), 1, 1)),
        TransportChannel::ReliableOrdered);
    inventoryTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsEquipmentSnapshot,
        encodeLatestWinsEquipmentSnapshot(equipmentSnapshot(SessionGeneration::initial(), 1, 1)),
        TransportChannel::LatestWins);
    inventoryCoordinator->frame(0.01f);
    require(inventoryControl.resyncCompletions == 0 && inventoryPresentation.inventories == 1);
    inventoryTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliablePlayerInventoryBaseline,
        encodeReliablePlayerInventoryBaseline(playerInventoryBaseline(SessionGeneration::initial(), 1, 1, 1, 2)),
        TransportChannel::ReliableOrdered);
    inventoryCoordinator->frame(0.01f);
    require(inventoryControl.resyncCompletions == 1 && inventoryPresentation.inventories == 2);

    Input combatInput;
    Presentation combatPresentation;
    Status combatStatus;
    auto combatTransport = std::make_unique<IdleTransport>();
    auto* combatTransportObserver = combatTransport.get();
    combatTransportObserver->acceptConnections = true;
    auto combatClock = std::make_unique<Clock>();
    auto combatCreated = ClientSessionRuntime::create(
        *combatTransport, *combatClock, timeouts, SessionGeneration::initial(), outbound);
    auto combatRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(combatCreated));
    auto combatVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 10, 10));
    const std::array combatCapabilities{ combatReplicationCapability(), authoritativeInstantMagicCapability(),
        authoritativeTimedAreaMagicCapability() };
    auto combatOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(combatVersions), combatCapabilities, {}));
    auto combatPassword = AuthenticationMaterial::create(passwordBytes);
    require(combatPassword
        && combatRuntime->start(endpoint, ClientHello::fromOffer(std::move(combatOffer)),
               AuthenticationRequest::join(std::move(*combatPassword)))
            == HeadlessClientResult::Accepted);
    auto combatCoordinator = makeCoordinator(std::move(combatTransport), std::move(combatClock),
        std::move(combatRuntime), reconnect, combatInput, combatPresentation, combatStatus);
    combatCoordinator->frame(0.01f);
    combatTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello(false, false, false, false, true, 10, false, false, false, false, true)),
        TransportChannel::ReliableOrdered);
    combatCoordinator->frame(0.01f);
    combatTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(accepted(std::byte{ 8 })), TransportChannel::ReliableOrdered);
    combatTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial())), TransportChannel::ReliableOrdered);
    combatTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial())), TransportChannel::LatestWins);
    combatTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsCombatSnapshot,
        encodeLatestWinsCombatSnapshot(combatSnapshot(SessionGeneration::initial(), 1, 1)),
        TransportChannel::LatestWins);
    combatCoordinator->frame(0.01f);
    require(combatPresentation.combats == 1 && combatPresentation.lastCombatFatigue == 80.f);
    combatInput.nextMelee = MeleeAttackCapture{ value<ActorId>(1), value<ServerTick>(1), CombatRevision::initial(),
        CombatRevision::initial(), MeleeAttackType::Chop, 0.75f };
    const auto sentBeforeCombat = combatTransportObserver->sentFrames.size();
    combatCoordinator->frame(0.01f);
    bool foundCombatCommand = false;
    for (std::size_t index = sentBeforeCombat; index < combatTransportObserver->sentFrames.size(); ++index)
    {
        auto decoded = decodeProtocolFrame(combatTransportObserver->sentFrames[index]);
        auto* commandFrame = std::get_if<DecodedFrame>(&decoded);
        if (!commandFrame || commandFrame->messageKind() != MessageKind::ClientMeleeAttackCommand)
            continue;
        auto decodedCommand = decodeClientMeleeAttackCommand(commandFrame->payload());
        auto* command = std::get_if<ClientMeleeAttackCommand>(&decodedCommand);
        foundCombatCommand = command && command->targetActorId == value<ActorId>(1)
            && command->expectedAttackerRevision == CombatRevision::initial()
            && command->attackType == MeleeAttackType::Chop && command->attackStrength == 0.75f;
    }
    require(foundCombatCommand);
    combatInput.nextMagic = MagicUseCapture{ MagicUseSourceKind::Spell, 22, MagicUseTargetKind::Actor, 1,
        value<ServerTick>(1), CombatRevision::initial(), CombatRevision::initial(), InventoryRevision::initial() };
    const auto sentBeforeMagic = combatTransportObserver->sentFrames.size();
    combatCoordinator->frame(0.01f);
    bool foundMagicCommand = false;
    for (std::size_t index = sentBeforeMagic; index < combatTransportObserver->sentFrames.size(); ++index)
    {
        auto decoded = decodeProtocolFrame(combatTransportObserver->sentFrames[index]);
        auto* commandFrame = std::get_if<DecodedFrame>(&decoded);
        if (!commandFrame || commandFrame->messageKind() != MessageKind::ClientMagicUseCommand)
            continue;
        auto decodedCommand = decodeClientMagicUseCommand(commandFrame->payload());
        auto* command = std::get_if<ClientMagicUseCommand>(&decodedCommand);
        foundMagicCommand = command && command->sourceKind == MagicUseSourceKind::Spell && command->sourceId == 22
            && command->targetKind == MagicUseTargetKind::Actor && command->targetId == 1
            && command->expectedCasterRevision == CombatRevision::initial();
    }
    require(foundMagicCommand);

    Input weatherInput;
    Presentation weatherPresentation;
    Status weatherStatus;
    auto weatherTransport = std::make_unique<IdleTransport>();
    auto* weatherTransportObserver = weatherTransport.get();
    weatherTransportObserver->acceptConnections = true;
    auto weatherClock = std::make_unique<Clock>();
    auto weatherCreated = ClientSessionRuntime::create(
        *weatherTransport, *weatherClock, timeouts, SessionGeneration::initial(), outbound);
    auto weatherRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(weatherCreated));
    auto weatherVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 7, 7));
    const std::array weatherCapabilities{ weatherReplicationCapability() };
    auto weatherOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(weatherVersions), weatherCapabilities, {}));
    auto weatherPassword = AuthenticationMaterial::create(passwordBytes);
    require(weatherPassword
        && weatherRuntime->start(endpoint, ClientHello::fromOffer(std::move(weatherOffer)),
               AuthenticationRequest::join(std::move(*weatherPassword)))
            == HeadlessClientResult::Accepted);
    auto weatherCoordinator = makeCoordinator(std::move(weatherTransport), std::move(weatherClock),
        std::move(weatherRuntime), reconnect, weatherInput, weatherPresentation, weatherStatus);
    weatherCoordinator->frame(0.01f);
    weatherTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello(false, false, false, false, false, 7, false, true)),
        TransportChannel::ReliableOrdered);
    weatherCoordinator->frame(0.01f);
    weatherTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(accepted(std::byte{ 14 })), TransportChannel::ReliableOrdered);
    weatherTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial())), TransportChannel::ReliableOrdered);
    weatherTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial())), TransportChannel::LatestWins);
    weatherCoordinator->frame(0.01f);
    require(weatherCoordinator->multiplayerState() != MultiplayerState::Ready && weatherPresentation.weathers == 0);
    const auto weatherBaseline = weatherState(SessionGeneration::initial(), 5, 1, 1, true);
    weatherTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableWeatherState,
        encodeReliableWeatherState(weatherBaseline), TransportChannel::ReliableOrdered);
    weatherCoordinator->frame(0.01f);
    require(weatherCoordinator->multiplayerState() == MultiplayerState::Ready && weatherPresentation.weathers == 1
        && weatherPresentation.lastWeatherTick == value<ServerTick>(5)
        && weatherPresentation.lastWeatherRegions.size() == 1
        && weatherPresentation.lastWeatherRegions[0].targetWeather == value<WeatherId>(2));
    weatherTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableWeatherState,
        encodeReliableWeatherState(weatherBaseline), TransportChannel::ReliableOrdered);
    weatherCoordinator->frame(0.01f);
    require(weatherPresentation.weathers == 1);
    weatherPresentation.weatherResult = ProviderResult::ContentMappingFailed;
    weatherTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableWeatherState,
        encodeReliableWeatherState(weatherState(SessionGeneration::initial(), 6, 2, 2, false, 3)),
        TransportChannel::ReliableOrdered);
    weatherCoordinator->frame(0.01f);
    require(weatherPresentation.weathers == 2 && weatherStatus.last == ConnectionStatus::ContentMappingFailed);

    Input timeInput;
    Presentation timePresentation;
    Status timeStatus;
    auto timeTransport = std::make_unique<IdleTransport>();
    auto* timeTransportObserver = timeTransport.get();
    timeTransportObserver->acceptConnections = true;
    auto timeClock = std::make_unique<Clock>();
    auto timeCreated
        = ClientSessionRuntime::create(*timeTransport, *timeClock, timeouts, SessionGeneration::initial(), outbound);
    auto timeRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(timeCreated));
    auto timeVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 8, 8));
    const std::array timeCapabilities{ combatReplicationCapability(), worldTimeReplicationCapability(),
        authoritativeWaitRestCapability() };
    auto timeOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(timeVersions), timeCapabilities, {}));
    auto timePassword = AuthenticationMaterial::create(passwordBytes);
    require(timePassword
        && timeRuntime->start(endpoint, ClientHello::fromOffer(std::move(timeOffer)),
               AuthenticationRequest::join(std::move(*timePassword)))
            == HeadlessClientResult::Accepted);
    auto timeCoordinator = makeCoordinator(std::move(timeTransport), std::move(timeClock), std::move(timeRuntime),
        reconnect, timeInput, timePresentation, timeStatus);
    timeCoordinator->frame(0.01f);
    timeTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello(false, false, false, false, true, 8, false, false, true, true)),
        TransportChannel::ReliableOrdered);
    timeCoordinator->frame(0.01f);
    timeTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(accepted(std::byte{ 15 })), TransportChannel::ReliableOrdered);
    timeTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial())), TransportChannel::ReliableOrdered);
    timeTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial())), TransportChannel::LatestWins);
    timeTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsCombatSnapshot,
        encodeLatestWinsCombatSnapshot(combatSnapshot(SessionGeneration::initial(), 1, 1)),
        TransportChannel::LatestWins);
    timeCoordinator->frame(0.01f);
    require(timeCoordinator->multiplayerState() != MultiplayerState::Ready && timePresentation.worldTimes == 0);
    const auto timeBaseline = worldTimeState(SessionGeneration::initial(), 5, true);
    timeTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableWorldTimeState,
        encodeReliableWorldTimeState(timeBaseline), TransportChannel::ReliableOrdered);
    timeCoordinator->frame(0.01f);
    require(timeCoordinator->multiplayerState() == MultiplayerState::Ready && timePresentation.worldTimes == 1
        && timePresentation.lastWorldTime && timePresentation.lastWorldTime->time.day == 30
        && timePresentation.lastWorldTime->time.month == 1);
    timeTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableWorldTimeState,
        encodeReliableWorldTimeState(timeBaseline), TransportChannel::ReliableOrdered);
    timeCoordinator->frame(0.01f);
    require(timePresentation.worldTimes == 1);
    const auto sentBeforeWait = timeTransportObserver->sentFrames.size();
    timeCoordinator->setGameRunning(true);
    require(timeCoordinator->submitWaitRest(2, WaitRestMode::Rest));
    timeCoordinator->frame(0.01f);
    bool sentWaitRest = false;
    for (std::size_t index = sentBeforeWait; index < timeTransportObserver->sentFrames.size(); ++index)
    {
        const auto frame = decodeProtocolFrame(timeTransportObserver->sentFrames[index]);
        const auto* decoded = std::get_if<DecodedFrame>(&frame);
        if (!decoded || decoded->messageKind() != MessageKind::ReliableOperation)
            continue;
        const auto operation = decodeReliableOperation(decoded->payload());
        const auto* value = std::get_if<ReliableOperation>(&operation);
        const auto* request = value ? std::get_if<WaitRestRequest>(&value->body()) : nullptr;
        sentWaitRest = request && request->hours() == 2 && request->mode() == WaitRestMode::Rest;
    }
    require(sentWaitRest && !timeCoordinator->submitWaitRest(0, WaitRestMode::Rest));

    Input dialogueInput;
    Presentation dialoguePresentation;
    Status dialogueStatus;
    DisconnectOnce dialogueDisconnect;
    dialogueDisconnect.pending = false;
    auto dialogueTransport = std::make_unique<IdleTransport>();
    auto* dialogueTransportObserver = dialogueTransport.get();
    dialogueTransportObserver->acceptConnections = true;
    auto dialogueClock = std::make_unique<Clock>();
    auto dialogueCreated = ClientSessionRuntime::create(
        *dialogueTransport, *dialogueClock, timeouts, SessionGeneration::initial(), outbound);
    auto dialogueRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(dialogueCreated));
    auto dialogueVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 6, 6));
    const std::array dialogueCapabilities{ dialogueChoiceCapability() };
    auto dialogueOffer = std::get<CapabilityOffer>(
        CapabilityOffer::create(std::move(dialogueVersions), dialogueCapabilities, {}, testContentManifestId()));
    const std::array dialoguePasswordBytes{ std::byte{ 12 } };
    auto dialoguePassword = AuthenticationMaterial::create(dialoguePasswordBytes);
    require(dialoguePassword
        && dialogueRuntime->start(endpoint, ClientHello::fromOffer(std::move(dialogueOffer)),
               AuthenticationRequest::join(std::move(*dialoguePassword)))
            == HeadlessClientResult::Accepted);
    auto dialogueCoordinator
        = makeCoordinator(std::move(dialogueTransport), std::move(dialogueClock), std::move(dialogueRuntime), reconnect,
            dialogueInput, dialoguePresentation, dialogueStatus, &dialogueDisconnect);
    dialogueCoordinator->frame(0.01f);
    const auto dialogueHello = encodeServerHello(serverHello(false, false, false, false, false, 6, true));
    dialogueTransportObserver->enqueue(
        MessageClass::SessionControl, MessageKind::ServerHello, dialogueHello, TransportChannel::ReliableOrdered);
    dialogueCoordinator->frame(0.01f);
    dialogueTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(accepted(std::byte{ 12 }, 2 * MinimumResumeTokenLifetimeMilliseconds)),
        TransportChannel::ReliableOrdered);
    dialogueTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial())), TransportChannel::ReliableOrdered);
    dialogueTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial())), TransportChannel::LatestWins);
    dialogueCoordinator->frame(0.01f);
    require(dialogueCoordinator->multiplayerState() == MultiplayerState::Ready);
    require(dialogueCoordinator->submitDialogueChoice(8) == DialogueChoiceSubmissionResult::Unmapped);
    require(dialogueCoordinator->submitDialogueChoice(7) == DialogueChoiceSubmissionResult::Pending);
    require(dialogueCoordinator->submitDialogueChoice(7) == DialogueChoiceSubmissionResult::Unavailable);
    require(!dialogueCoordinator->takeDialogueChoiceResolution());
    dialogueCoordinator->frame(0.01f);

    std::vector<ClientDialogueChoiceCommand> sentDialogueChoices;
    const auto collectDialogueChoices = [&] {
        sentDialogueChoices.clear();
        for (const auto& bytes : dialogueTransportObserver->sentFrames)
        {
            const auto decoded = decodeProtocolFrame(bytes);
            const auto* decodedFrame = std::get_if<DecodedFrame>(&decoded);
            if (!decodedFrame || decodedFrame->messageKind() != MessageKind::ClientDialogueChoiceCommand)
                continue;
            const auto command = decodeClientDialogueChoiceCommand(decodedFrame->payload());
            if (const auto* value = std::get_if<ClientDialogueChoiceCommand>(&command))
                sentDialogueChoices.push_back(*value);
        }
    };
    collectDialogueChoices();
    require(sentDialogueChoices.size() == 1 && sentDialogueChoices.front().choiceId == value<DialogueChoiceId>(40));
    const auto retainedDialogueCommandId = sentDialogueChoices.front().commandId;

    dialogueDisconnect.pending = true;
    dialogueCoordinator->frame(0.01f);
    require(dialogueStatus.last == ConnectionStatus::Reconnecting);
    dialogueCoordinator->frame(0.01f);
    dialogueTransportObserver->enqueue(
        MessageClass::SessionControl, MessageKind::ServerHello, dialogueHello, TransportChannel::ReliableOrdered);
    dialogueCoordinator->frame(0.01f);
    const auto resumedGeneration = *SessionGeneration::initial().next();
    dialogueTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(accepted(std::byte{ 13 })), TransportChannel::ReliableOrdered);
    dialogueTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(resumedGeneration)), TransportChannel::ReliableOrdered);
    dialogueTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(resumedGeneration)), TransportChannel::LatestWins);
    dialogueCoordinator->frame(0.01f);
    dialogueCoordinator->frame(0.01f);
    collectDialogueChoices();
    require(sentDialogueChoices.size() == 2 && sentDialogueChoices.back().commandId == retainedDialogueCommandId
        && sentDialogueChoices.back().choiceId == value<DialogueChoiceId>(40)
        && dialogueStatus.last == ConnectionStatus::Resumed);
    const auto& retriedDialogue = sentDialogueChoices.back();
    dialogueTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableDialogueChoiceResult,
        encodeReliableDialogueChoiceResult(
            { value<SessionId>(1), resumedGeneration, retriedDialogue.commandSequence, retainedDialogueCommandId,
                value<DialogueChoiceId>(40), DialogueChoiceDisposition::Committed, true, value<CanonicalRevision>(2) }),
        TransportChannel::ReliableOrdered);
    dialogueCoordinator->frame(0.01f);
    const auto dialogueResolution = dialogueCoordinator->takeDialogueChoiceResolution();
    require(dialogueResolution && dialogueResolution->localChoice == 7 && dialogueResolution->committed()
        && dialogueResolution->duplicate && !dialogueCoordinator->takeDialogueChoiceResolution());

    Input deferredInput;
    Presentation deferredPresentation;
    Status deferredStatus;
    auto deferredTransport = std::make_unique<IdleTransport>();
    auto* deferredTransportObserver = deferredTransport.get();
    deferredTransportObserver->acceptConnections = true;
    auto deferredClock = std::make_unique<Clock>();
    auto deferredCreated = ClientSessionRuntime::create(
        *deferredTransport, *deferredClock, timeouts, SessionGeneration::initial(), outbound);
    auto deferredRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(deferredCreated));
    auto deferredVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 2));
    auto deferredOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(deferredVersions), {}, {}));
    auto deferredPassword = AuthenticationMaterial::create(passwordBytes);
    require(deferredPassword
        && deferredRuntime->start(endpoint, ClientHello::fromOffer(std::move(deferredOffer)),
               AuthenticationRequest::join(std::move(*deferredPassword)))
            == HeadlessClientResult::Accepted);
    auto deferredCoordinator = makeCoordinator(std::move(deferredTransport), std::move(deferredClock),
        std::move(deferredRuntime), reconnect, deferredInput, deferredPresentation, deferredStatus);
    deferredCoordinator->setGameRunning(false);
    deferredCoordinator->frame(0.01f);
    deferredTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello()), TransportChannel::ReliableOrdered);
    deferredCoordinator->frame(0.01f);
    deferredTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(accepted(std::byte{ 9 })), TransportChannel::ReliableOrdered);
    deferredTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial())), TransportChannel::ReliableOrdered);
    deferredTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial())), TransportChannel::LatestWins);
    deferredCoordinator->frame(0.01f);
    require(deferredCoordinator->multiplayerState() == MultiplayerState::Ready
        && deferredCoordinator->gameStartRequested() && deferredPresentation.calls == 0
        && deferredPresentation.advances == 0 && deferredInput.calls == 0);
    deferredCoordinator->setGameRunning(true);
    deferredCoordinator->frame(0.01f);
    require(!deferredCoordinator->gameStartRequested() && deferredPresentation.calls == 1
        && deferredPresentation.advances == 1 && deferredInput.calls == 1);

    Input chargenInput;
    Presentation chargenPresentation;
    Status chargenStatus;
    auto chargenTransport = std::make_unique<IdleTransport>();
    auto* chargenTransportObserver = chargenTransport.get();
    chargenTransportObserver->acceptConnections = true;
    auto chargenClock = std::make_unique<Clock>();
    auto* chargenClockObserver = chargenClock.get();
    auto chargenCreated = ClientSessionRuntime::create(
        *chargenTransport, *chargenClock, timeouts, SessionGeneration::initial(), outbound);
    auto chargenRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(chargenCreated));
    auto chargenVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 3, 3));
    auto chargenOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(chargenVersions), {}, {}));
    auto chargenPassword = AuthenticationMaterial::create(passwordBytes);
    require(chargenPassword
        && chargenRuntime->start(endpoint, ClientHello::fromOffer(std::move(chargenOffer)),
               AuthenticationRequest::join(std::move(*chargenPassword)))
            == HeadlessClientResult::Accepted);
    auto chargenCoordinator = makeCoordinator(std::move(chargenTransport), std::move(chargenClock),
        std::move(chargenRuntime), reconnect, chargenInput, chargenPresentation, chargenStatus);
    chargenCoordinator->frame(0.01f);
    chargenTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(serverHello(false, false, false, false, false, 3)), TransportChannel::ReliableOrdered);
    chargenCoordinator->frame(0.01f);
    chargenTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        encodeAuthenticationAccepted(
            accepted(std::byte{ 10 }, MinimumResumeTokenLifetimeMilliseconds, false, CharacterLifecycle::NewCharacter)),
        TransportChannel::ReliableOrdered);
    chargenTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
        encodeReliableInterestBaseline(selfBaseline(SessionGeneration::initial(), true)),
        TransportChannel::ReliableOrdered);
    chargenTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial(), true)), TransportChannel::LatestWins);
    chargenCoordinator->frame(0.01f);
    for (std::uint64_t sequence = 1; sequence <= MaximumRetainedLocomotionInputs + 1; ++sequence)
    {
        chargenClockObserver->nanoseconds = sequence * 250'000'000;
        chargenTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
            encodeLatestWinsSnapshot(
                selfSnapshot(SessionGeneration::initial(), true, sequence + 1, sequence + 1, sequence)),
            TransportChannel::LatestWins);
        chargenCoordinator->frame(0.01f);
    }
    require(chargenCoordinator->multiplayerState() == MultiplayerState::Ready);
    require(chargenCoordinator->characterLifecycle() == CharacterLifecycle::NewCharacter);
    require(chargenPresentation.calls == 0);
    require(chargenInput.calls == 0);
    require(chargenCoordinator->submitCharacterCreation(CompleteCharacterCreation{}));
    const auto safePointRevision = MaximumRetainedLocomotionInputs + 3;
    chargenTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(
            SessionGeneration::initial(), true, safePointRevision, safePointRevision, safePointRevision - 1)),
        TransportChannel::LatestWins);
    chargenCoordinator->frame(0.01f);
    require(chargenCoordinator->characterLifecycle() == CharacterLifecycle::NewCharacter
        && chargenPresentation.calls == 0 && chargenInput.calls == 0);
    chargenTransportObserver->enqueue(MessageClass::ReliableOperation, MessageKind::ReliableCharacterProfile,
        encodeReliableCharacterProfile({ value<SessionId>(1), SessionGeneration::initial(), value<PlayerId>(1),
            CharacterConfirmationResult::Confirmed, establishedCharacterProfile() }),
        TransportChannel::ReliableOrdered);
    chargenCoordinator->frame(0.01f);
    require(chargenCoordinator->characterLifecycle() == CharacterLifecycle::EstablishedCharacter
        && chargenPresentation.calls == 1 && chargenPresentation.lastObservedPlayers == 2 && chargenInput.calls == 1);

    const auto transientStartupCell = CellId::exterior(value<CellSpaceId>(4), 0, 0);
    chargenInput.nextTransition
        = CellTransitionCapture{ ProviderResult::Accepted, CellTransition(transientStartupCell) };
    chargenPresentation.lastAllowLocalCellCorrection = false;
    const auto sentBeforeTransientTransition = chargenTransportObserver->sentFrames.size();
    chargenCoordinator->frame(0.01f);
    std::optional<CommandSequence> transientTransitionSequence;
    for (std::size_t index = sentBeforeTransientTransition; index < chargenTransportObserver->sentFrames.size();
        ++index)
    {
        auto decoded = decodeProtocolFrame(chargenTransportObserver->sentFrames[index]);
        const auto* commandFrame = std::get_if<DecodedFrame>(&decoded);
        if (!commandFrame || commandFrame->messageKind() != MessageKind::ReliableOperation)
            continue;
        auto operation = decodeReliableOperation(commandFrame->payload());
        const auto* reliable = std::get_if<ReliableOperation>(&operation);
        const auto* transition = reliable ? std::get_if<CellTransition>(&reliable->body()) : nullptr;
        if (transition && transition->requestedCell() == transientStartupCell)
            transientTransitionSequence = reliable->header().commandHeader().commandSequence();
    }
    require(transientTransitionSequence && chargenCoordinator->multiplayerState() == MultiplayerState::Ready
        && !chargenPresentation.lastAllowLocalCellCorrection);

    chargenTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        encodeLatestWinsSnapshot(selfSnapshot(SessionGeneration::initial(), true, safePointRevision + 1,
            safePointRevision + 1, transientTransitionSequence->value())),
        TransportChannel::LatestWins);
    chargenCoordinator->frame(0.01f);
    require(chargenCoordinator->multiplayerState() == MultiplayerState::Ready
        && chargenPresentation.lastAllowLocalCellCorrection);

    require(
        std::get<TES3MP::OpenMWAdapter::ClientCompositionFailure>(TES3MP::OpenMWAdapter::makeClientCoordinator("", 0, 0,
            "unreachable/credential", {}, TES3MP::testContentManifestId(), TES3MP::OpenMWAdapter::ClientProviders{}))
        == TES3MP::OpenMWAdapter::ClientCompositionFailure::ProvidersUnavailable);
    const TES3MP::OpenMWAdapter::ClientProviders providers{ &input, &presentation, &status, nullptr };
    require(std::get<TES3MP::OpenMWAdapter::ClientCompositionFailure>(TES3MP::OpenMWAdapter::makeClientCoordinator(
                "", 25560, 1000, {}, {}, TES3MP::testContentManifestId(), providers))
        == TES3MP::OpenMWAdapter::ClientCompositionFailure::InvalidEndpoint);
    require(std::get<TES3MP::OpenMWAdapter::ClientCompositionFailure>(TES3MP::OpenMWAdapter::makeClientCoordinator(
                "127.0.0.1", 25560, 0, {}, {}, TES3MP::testContentManifestId(), providers))
        == TES3MP::OpenMWAdapter::ClientCompositionFailure::InvalidTimeout);

    const auto hostOnly = TES3MP::OpenMWAdapter::parseServerAddress("example.org", 25565);
    require(hostOnly && hostOnly->host() == "example.org" && hostOnly->port() == 25565);
    const auto uri = TES3MP::OpenMWAdapter::parseServerAddress("tes3mp://127.0.0.1:25570", 25565);
    require(uri && uri->host() == "127.0.0.1" && uri->port() == 25570);
    require(TES3MP::OpenMWAdapter::parseServerAddress("TES3MP://example.org", 25565));
    const auto ipv6 = TES3MP::OpenMWAdapter::parseServerAddress("tes3mp://[::1]:25571", 25565);
    require(ipv6 && ipv6->host() == "::1" && ipv6->port() == 25571);
    require(!TES3MP::OpenMWAdapter::parseServerAddress("http://example.org", 25565));
    require(!TES3MP::OpenMWAdapter::parseServerAddress("tes3mp://example.org/path", 25565));
    require(!TES3MP::OpenMWAdapter::parseServerAddress("::1", 25565));
    require(!TES3MP::OpenMWAdapter::parseServerAddress("example.org:0", 25565));

    // PlayerProfileManager and credential hashing tests
    {
        using namespace TES3MP::OpenMWAdapter;
        require(PlayerProfileManager::isValidUsername("Jiub"));
        require(PlayerProfileManager::isValidUsername("Player_1"));
        require(!PlayerProfileManager::isValidUsername("ab"));
        require(!PlayerProfileManager::isValidUsername("user with spaces"));
        require(!PlayerProfileManager::isValidUsername("invalid!char"));

        const auto hash1 = computeCredentialHash("Jiub", "mypassword");
        const auto hash2 = computeCredentialHash("jiub", "mypassword");
        require(hash1 == hash2);
        const auto hash3 = computeCredentialHash("Jiub", "wrongpassword");
        require(hash1 != hash3);
        const auto endpointHash1 = deriveEndpointCredential(hash1, "example.org", 25565);
        const auto endpointHash2 = deriveEndpointCredential(hash1, "EXAMPLE.ORG", 25565);
        require(endpointHash1 == endpointHash2);
        require(endpointHash1 != deriveEndpointCredential(hash1, "example.org", 25566));
        require(endpointHash1 != deriveEndpointCredential(hash1, "other.example", 25565));

        const auto tempProfileFile = std::filesystem::temp_directory_path() / "tes3mp_test_profiles.json";
        std::filesystem::remove(tempProfileFile);

        PlayerProfileManager manager;
        require(!manager.load(tempProfileFile));
        require(!manager.hasProfiles());

        require(manager.addProfile("Jiub", "pass1"));
        require(manager.hasProfiles());
        require(manager.activeUsername() == "Jiub");
        require(manager.addProfile("Fargoth", "pass2"));
        require(manager.profiles().size() == 2);
        require(manager.setActive("Fargoth"));
        require(manager.activeUsername() == "Fargoth");
        require(manager.save(tempProfileFile));
        {
            std::ifstream saved(tempProfileFile);
            const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
            require(text.find("\"password\"") == std::string::npos);
            require(text.find("\"credential\"") != std::string::npos);
            require(text.find("pass1") == std::string::npos && text.find("pass2") == std::string::npos);
        }

        PlayerProfileManager loaded;
        require(loaded.load(tempProfileFile));
        require(loaded.profiles().size() == 2);
        require(loaded.activeUsername() == "Fargoth");
        const auto* loadedJiub = loaded.getProfile("Jiub");
        require(loadedJiub && loadedJiub->credential == computeCredentialHash("Jiub", "pass1"));
        const auto* loadedFargoth = loaded.getProfile("Fargoth");
        require(loadedFargoth && loadedFargoth->credential == computeCredentialHash("Fargoth", "pass2"));

        require(loaded.deleteProfile("Jiub"));
        require(loaded.profiles().size() == 1);
        require(loaded.getProfile("Jiub") == nullptr);

        std::filesystem::remove(tempProfileFile);
        {
            std::ofstream legacy(tempProfileFile);
            legacy << "{\n  \"active\": \"Jiub\",\n  \"profiles\": [\n"
                      "    { \"username\": \"Jiub\", \"password\": \"oldpass\" }\n  ]\n}\n";
        }
        PlayerProfileManager migrated;
        require(migrated.load(tempProfileFile));
        require(migrated.getProfile("Jiub")
            && migrated.getProfile("Jiub")->credential == computeCredentialHash("Jiub", "oldpass"));
        require(migrated.save(tempProfileFile));
        {
            std::ifstream saved(tempProfileFile);
            const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
            require(text.find("oldpass") == std::string::npos && text.find("\"password\"") == std::string::npos);
        }
        std::filesystem::remove(tempProfileFile);
    }

#ifdef TES3MP_ADAPTER_TEST_HAS_GNS
    auto launcher = TES3MP::OpenMWAdapter::makeClientLauncher({ 25565, 1'000, {},
        std::filesystem::temp_directory_path(), {}, {}, TES3MP::testContentManifestId(), providers });
    require(static_cast<bool>(launcher));
    const auto profileCredential = TES3MP::OpenMWAdapter::computeCredentialHash("Jiub", "testpass");
    launcher->setPlayerProfile("Jiub", profileCredential);
    require(launcher->activePlayerUsername() == "Jiub");
    launcher->setJoinPassword("retry-test");
    require(launcher->connect("127.0.0.1:9"));
    launcher->confirmGameStart(false);
    require(launcher->multiplayerState() == TES3MP::OpenMWAdapter::MultiplayerState::Failed);
    require(launcher->connect("127.0.0.1:9"));
    require(launcher->failure().empty());
    launcher->confirmGameStart(false);
#endif
}
