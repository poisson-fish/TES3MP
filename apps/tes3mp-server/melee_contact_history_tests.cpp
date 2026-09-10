#include "melee_contact_history.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    TES3MP::ContentManifest manifest()
    {
        return TES3MP::testContentManifest();
    }

    std::filesystem::path collisionFile(bool blocked)
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        auto path = std::filesystem::temp_directory_path()
            / ("tes3mp-melee-contact-" + std::to_string(nonce) + (blocked ? "-blocked" : "-clear") + ".txt");
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << "TES3MP_COLLISION_V1\n"
                  "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
                  "cell interior 7\n"
                  "cell exterior 8 0 0\n";
        if (blocked)
            stream << "solid interior 7 40 -10 -10 60 10 10\n";
        return path;
    }

    TES3MP::Transform root(std::int64_t x)
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        return { TES3MP::CellId::interior(id<TES3MP::CellSpaceId>(7)), TES3MP::Position3(x, 0, 0),
            TES3MP::Orientation3(zero, zero, zero) };
    }

    TES3MP::CanonicalServerState players(std::int64_t x = 0)
    {
        const std::array values{ TES3MP::CanonicalPlayerEntityState(id<TES3MP::PlayerId>(1),
            id<TES3MP::EntityId>(10), id<TES3MP::AppearanceId>(1), root(x), TES3MP::LinearVelocity3(0, 0, 0),
            TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial()) };
        return std::get<TES3MP::CanonicalServerState>(TES3MP::createCanonicalServerState(values, {}));
    }

    TES3MP::CanonicalActorWorld actors(std::int64_t x)
    {
        const std::array values{ TES3MP::CanonicalActorEntityState(id<TES3MP::ActorId>(2), id<TES3MP::EntityId>(20),
            id<TES3MP::ActorPrototypeId>(3), root(x), TES3MP::LinearVelocity3(0, 0, 0),
            TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(), TES3MP::ServerTick::initial(),
            TES3MP::ActorActivity::Idle, 0) };
        return std::get<TES3MP::CanonicalActorWorld>(TES3MP::createCanonicalActorWorld(values));
    }

    TES3MP::ServerMeleeContactRequest request(std::uint64_t tick, std::optional<float> reach = std::nullopt)
    {
        return { id<TES3MP::PlayerId>(1), id<TES3MP::ActorId>(2), id<TES3MP::ServerTick>(tick),
            TES3MP::MeleeAttackType::Chop, std::nullopt, reach };
    }

    bool historical_roots_decide_reach_instead_of_current_roots()
    {
        const auto path = collisionFile(false);
        auto loaded = TES3MP::ServerApp::ContentCollisionProvider::load(path, manifest());
        std::filesystem::remove(path);
        auto* collision = std::get_if<std::unique_ptr<TES3MP::ServerApp::ContentCollisionProvider>>(&loaded);
        if (!collision)
            return false;
        auto history = TES3MP::ServerApp::MeleeContactHistory::create({ 1, 8, 100 }, **collision);
        if (!history || !history->capture(id<TES3MP::ServerTick>(5), players(0), actors(90)))
            return false;
        const auto currentPlayer = players(1000);
        const auto currentActors = actors(1090);
        return history->validate(request(5), currentPlayer.players().front(), currentActors.actors().front())
            == TES3MP::MeleeContactValidation::Accepted;
    }

    bool missing_history_range_and_occlusion_fail_closed()
    {
        const auto clearPath = collisionFile(false);
        const auto blockedPath = collisionFile(true);
        auto clearLoaded = TES3MP::ServerApp::ContentCollisionProvider::load(clearPath, manifest());
        auto blockedLoaded = TES3MP::ServerApp::ContentCollisionProvider::load(blockedPath, manifest());
        std::filesystem::remove(clearPath);
        std::filesystem::remove(blockedPath);
        auto* clear = std::get_if<std::unique_ptr<TES3MP::ServerApp::ContentCollisionProvider>>(&clearLoaded);
        auto* blocked = std::get_if<std::unique_ptr<TES3MP::ServerApp::ContentCollisionProvider>>(&blockedLoaded);
        if (!clear || !blocked)
            return false;
        auto ranged = TES3MP::ServerApp::MeleeContactHistory::create({ 1, 8, 100 }, **clear);
        auto occluded = TES3MP::ServerApp::MeleeContactHistory::create({ 1, 8, 200 }, **blocked);
        if (!ranged || !occluded || !ranged->capture(id<TES3MP::ServerTick>(5), players(), actors(101))
            || !occluded->capture(id<TES3MP::ServerTick>(5), players(), actors(100)))
            return false;
        const auto spatialPlayers = players();
        const auto rangedActors = actors(101);
        const auto blockedActors = actors(100);
        return ranged->validate(request(4), spatialPlayers.players().front(), rangedActors.actors().front())
                == TES3MP::MeleeContactValidation::HistoryUnavailable
            && ranged->validate(request(5), spatialPlayers.players().front(), rangedActors.actors().front())
                == TES3MP::MeleeContactValidation::NoContact
            && ranged->validate(request(5, 1.01f), spatialPlayers.players().front(), rangedActors.actors().front())
                == TES3MP::MeleeContactValidation::Accepted
            && occluded->validate(request(5), spatialPlayers.players().front(), blockedActors.actors().front())
                == TES3MP::MeleeContactValidation::NoContact;
    }

    bool history_is_bounded_and_tick_regression_is_atomic()
    {
        const auto path = collisionFile(false);
        auto loaded = TES3MP::ServerApp::ContentCollisionProvider::load(path, manifest());
        std::filesystem::remove(path);
        auto* collision = std::get_if<std::unique_ptr<TES3MP::ServerApp::ContentCollisionProvider>>(&loaded);
        if (!collision)
            return false;
        if (TES3MP::ServerApp::MeleeContactHistory::create(
                { 1, 2, TES3MP::ServerApp::MaximumAuthoritativeMeleeReachQuanta + 1 }, **collision))
            return false;
        auto history = TES3MP::ServerApp::MeleeContactHistory::create({ 1, 2, 100 }, **collision);
        if (!history)
            return false;
        for (std::uint64_t tick = 1; tick <= 4; ++tick)
            if (!history->capture(id<TES3MP::ServerTick>(tick), players(), actors(10)))
                return false;
        const auto count = history->frameCount();
        return count == 3 && !history->capture(id<TES3MP::ServerTick>(3), players(), actors(10))
            && history->frameCount() == count
            && history->validate(request(1), players().players().front(), actors(10).actors().front())
                == TES3MP::MeleeContactValidation::HistoryUnavailable
            && history->validate(request(2), players().players().front(), actors(10).actors().front())
                == TES3MP::MeleeContactValidation::Accepted
            && history->validate(request(2, static_cast<float>(
                    TES3MP::ServerApp::MaximumAuthoritativeMeleeReachQuanta)),
                    players().players().front(), actors(10).actors().front())
                == TES3MP::MeleeContactValidation::NoContact;
    }
}

int main()
{
    if (!historical_roots_decide_reach_instead_of_current_roots()) return 1;
    if (!missing_history_range_and_occlusion_fail_closed()) return 1;
    return history_is_bounded_and_tick_regression_is_atomic() ? 0 : 1;
}
