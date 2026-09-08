#include "adapter.hpp"
#include "client_connection.hpp"
#include "movement_mapping.hpp"
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

    TES3MP::ServerHello serverHello(
        bool pose = false, bool actors = false, bool interactiveObjects = false, bool inventory = false)
    {
        auto versions = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 2, 2));
        std::vector<TES3MP::CapabilityId> capabilities;
        if (pose)
            capabilities.push_back(TES3MP::vrPoseCapability());
        if (actors)
            capabilities.push_back(TES3MP::actorReplicationCapability());
        if (interactiveObjects)
            capabilities.push_back(TES3MP::interactiveObjectReplicationCapability());
        if (inventory)
            capabilities.push_back(TES3MP::inventoryReplicationCapability());
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

    std::vector<std::byte> frame(
        TES3MP::MessageClass messageClass, TES3MP::MessageKind messageKind, std::span<const std::byte> payload)
    {
        return std::get<std::vector<std::byte>>(TES3MP::encodeProtocolFrame(messageClass, messageKind, payload));
    }

    TES3MP::AuthenticationAcceptedMessage accepted(std::byte marker,
        std::uint64_t lifetime = TES3MP::MinimumResumeTokenLifetimeMilliseconds, bool includePlayerCredential = false)
    {
        std::array<std::byte, TES3MP::ResumeTokenBytes> bytes{};
        bytes.fill(marker);
        auto token = TES3MP::ResumeToken::create(bytes);
        std::optional<TES3MP::PlayerCredential> playerCredential;
        if (includePlayerCredential)
            playerCredential = TES3MP::PlayerCredential::create(bytes);
        return std::move(
            *TES3MP::AuthenticationAcceptedMessage::create(std::move(*token), lifetime, std::move(playerCredential)));
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
        std::uint64_t canonicalRevision = 1, std::uint64_t serverTick = 1)
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
        return TES3MP::LatestWinsSnapshot(TES3MP::LatestWinsSnapshotHeader(session, generation, player, entity,
                                              value<TES3MP::CanonicalRevision>(canonicalRevision), std::nullopt),
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
        void clearSessionState() noexcept override
        {
            ++clearCalls;
            nextInteraction.reset();
            nextInventory.reset();
        }
        unsigned calls = 0;
        unsigned interactionCalls = 0;
        unsigned inventoryCalls = 0;
        unsigned clearCalls = 0;
        std::optional<TES3MP::OpenMWAdapter::CellTransitionCapture> nextTransition;
        std::optional<TES3MP::OpenMWAdapter::ObjectInteractionCapture> nextInteraction;
        std::optional<TES3MP::OpenMWAdapter::InventoryTransactionCapture> nextInventory;
    };

    class Presentation final : public TES3MP::OpenMWAdapter::PresentationProvider
    {
    public:
        TES3MP::OpenMWAdapter::ProviderResult applyAuthoritative(const TES3MP::LatestWinsSnapshot&,
            std::span<const TES3MP::ObservedPlayer>, bool, TES3MP::MonotonicInstant,
            const std::optional<TES3MP::LocalLocomotionReconciliation>&) noexcept override
        {
            ++calls;
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
            lastEquipmentCount = equipment.members.size();
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
        unsigned advances = 0;
        unsigned clears = 0;
        unsigned poses = 0;
        unsigned actors = 0;
        unsigned interactiveObjects = 0;
        unsigned inventories = 0;
        unsigned poseFallbacks = 0;
        double lastPoseWeight = 0.0;
        std::map<TES3MP::InteractiveObjectId, TES3MP::ObjectRevision> objectRevisions;
        std::optional<TES3MP::InventoryRevision> lastInventoryRevision;
        std::size_t lastContainerCount = 0;
        std::size_t lastGroundCount = 0;
        std::size_t lastEquipmentCount = 0;
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

int main()
{
    using namespace TES3MP;
    using namespace TES3MP::OpenMWAdapter;

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
    require(presentation.advances == 1);
    transportObserver->failPoll = true;
    liveCoordinator->frame(0.01f);
    require(presentation.clears == 1 && status.last == ConnectionStatus::Disconnected);
    liveCoordinator.reset();
    require(presentation.clears == 2);

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
}
