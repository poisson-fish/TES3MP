#include "actor_scene.hpp"
#include "loadout.hpp"
#include "ordinary_door.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

int main(int argc, const char* const argv[])
{
    try
    {
        if (argc < 6)
            throw std::invalid_argument("Usage: actor_probe <interior> <npc-record|--list> <baseanim> <beastanim> <loadout options>");
        std::vector<const char*> arguments{argv[0]};
        const bool avoidance = argc >= 10 && std::string_view(argv[5]) == "--avoid-door";
        const bool navigation = argc >= 10 && (std::string_view(argv[5]) == "--navigate" || avoidance);
        arguments.insert(arguments.end(), argv + (navigation ? 10 : 5), argv + argc);
        TES3MP::Native::Loadout loadout(TES3MP::Native::readLoadoutOptions(
            static_cast<int>(arguments.size()), arguments.data()));
        const auto actors = loadout.placedActors(std::string_view(argv[1]));
        if (std::string_view(argv[2]) == "--list")
        {
            for (const auto& actor : actors)
                std::cout << actor.mRef.mRefID.toDebugString() << " placement=" << actor.mIdentity
                    << " scripted=" << actor.mScripted << " leveled=" << actor.mLeveled
                    << " position=" << actor.mRef.mPos.pos[0] << ',' << actor.mRef.mPos.pos[1] << ',' << actor.mRef.mPos.pos[2] << '\n';
            for (const auto& door : loadout.ordinaryDoors(ESM::RefId::stringRefId(argv[1]), 128))
                std::cout << "door=" << door.mRef.mRefID << " placement=" << door.mIdentity
                    << " position=" << door.mRef.mPos.pos[0] << ',' << door.mRef.mPos.pos[1] << ',' << door.mRef.mPos.pos[2]
                    << " yaw=" << door.mRef.mPos.rot[2] << '\n';
            return 0;
        }
        const auto record = ESM::RefId::stringRefId(argv[2]);
        const auto count = std::count_if(actors.begin(), actors.end(), [&](const auto& actor) { return actor.mRef.mRefID == record; });
        if (count != 1) throw std::invalid_argument("NPC record must select exactly one placement in the interior");
        const auto actor = std::find_if(actors.begin(), actors.end(), [&](const auto& value) { return value.mRef.mRefID == record; });
        TES3MP::Native::InteriorActorScene scene(loadout, argv[1], actor->mIdentity, argv[3], argv[4]);
        const auto original = scene.snapshot();
        if (navigation)
        {
            const auto doors = avoidance ? loadout.ordinaryDoors(ESM::RefId::stringRefId(argv[1]), 128)
                                         : std::vector<TES3MP::Native::Loadout::PlacedDoor>{};
            if (avoidance)
            {
                std::vector<uint64_t> ids;
                for (const auto& door : doors) ids.push_back(door.mIdentity);
                scene.bindDoors(ids, true);
            }
            scene.enableNavigation(argv[6]);
            for (int i = 0; i < 120; ++i) scene.step({0,0,0});
            const std::array destination{std::stof(argv[7]), std::stof(argv[8]), std::stof(argv[9])};
            const auto path = scene.pathTo(destination);
            scene.travelTo(destination);
            size_t steps = 0;
            while (!scene.arrived() && steps < 3600) { scene.navigate(120); ++steps; }
            const auto end = scene.snapshot();
            const auto dx = end.mPosition[0] - destination[0], dy = end.mPosition[1] - destination[1];
            std::cout << "navigation points=" << path.size() << " steps=" << steps
                << " end=" << end.mPosition[0] << ',' << end.mPosition[1] << ',' << end.mPosition[2]
                << " arrived=" << scene.arrived() << '\n';
            if (avoidance)
            {
                std::vector<TES3MP::Native::ActorSceneDoor> frames;
                for (const auto& door : doors) frames.push_back({door.mIdentity, door.mRef.mPos.rot[2]});
                const auto& selected = doors.at(0);
                TES3MP::Native::OrdinaryDoor door(*loadout.store().get<ESM::Door>().find(selected.mRef.mRefID), selected.mRef);
                auto state = door.activate(door.initialState()).mState;
                size_t blocked = 0;
                float excursion = 0;
                double maxTickMs = 0;
                size_t overruns = 0;
                for (int tick = 0; tick < 300; ++tick)
                {
                    const auto started = std::chrono::steady_clock::now();
                    bool avoid = false;
                    auto next = door.advance(state, 1.f / 30, [&](const auto& position, float delta) {
                        const auto contact = scene.doorContact(selected.mIdentity, position.rot[2], delta);
                        avoid = contact.mSelectedActor;
                        blocked += contact.mBlocked;
                        return contact.mBlocked;
                    });
                    state = std::move(next.mState);
                    frames[0] = {selected.mIdentity, state.mPosition.rot[2], state.mDoorState != 0, avoid};
                    auto pending = scene.prepareNavigation(120, frames); scene.install(*pending);
                    const auto now = scene.snapshot();
                    excursion = std::max(excursion, std::hypot(now.mPosition[0]-end.mPosition[0], now.mPosition[1]-end.mPosition[1]));
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count();
                    maxTickMs = std::max(maxTickMs, ms); overruns += ms > 1000. / 30;
                }
                const auto final = scene.snapshot();
                std::cout << "avoidance blocked=" << blocked << " excursion=" << excursion << " direction=" << state.mDoorState
                    << " final=" << final.mPosition[0] << ',' << final.mPosition[1] << ',' << final.mPosition[2]
                    << " arrived=" << scene.arrived() << " max-tick-ms=" << maxTickMs << " overruns=" << overruns << '\n';
                if (!blocked || excursion < 10 || state.mDoorState != 0 || !scene.arrived())
                    throw std::runtime_error("Real door avoidance did not clear and resume destination");
                std::cout << "PASS real-interior-door-avoidance\n";
                return 0;
            }
            if (!scene.arrived() || dx*dx + dy*dy > 32*32 || steps < 60)
                throw std::runtime_error("Interior navigation failed to reach a distinct destination");
            std::cout << "PASS native-interior-navigation\n" << scene.fingerprint();
            return 0;
        }
        std::set<uint64_t> contacts;
        size_t grounded = 0;
        float distance = 0.f;
        auto previous = original;
        // Diagnostic local movement, deliberately independent of AI/navigation.
        for (const std::array<float, 3> velocity : {std::array<float, 3>{0,0,0}, {0,120,0}, {120,0,0}, {0,-120,0}, {-120,0,0}})
            for (int step = 0; step < 180; ++step)
            {
                const auto state = scene.step(velocity);
                grounded += state.mGrounded;
                contacts.insert(state.mContacts.begin(), state.mContacts.end());
                for (int axis = 0; axis < 2; ++axis) distance += std::abs(state.mPosition[axis] - previous.mPosition[axis]);
                previous = state;
            }
        const auto beforeReject = scene.snapshot();
        bool rejected = false;
        try { scene.step({std::numeric_limits<float>::quiet_NaN(),0,0}); }
        catch (const std::invalid_argument&) { rejected = true; }
        const auto afterReject = scene.snapshot();
        if (!rejected || beforeReject.mPosition != afterReject.mPosition || beforeReject.mContacts != afterReject.mContacts)
            throw std::runtime_error("Invalid input changed native collision state");
        if (grounded < 600 || distance < 10 || contacts.empty() || scene.bodyCount() < 2)
            throw std::runtime_error("Interior proof did not establish grounded movement and object contact");
        std::cout << "PASS native-interior-npc cell=" << argv[1] << " record=" << argv[2]
            << " placement=" << original.mActor << " bodies=" << scene.bodyCount()
            << " steps=900 grounded=" << grounded << " horizontal-distance=" << distance
            << " contacted-objects=" << contacts.size() << " invalid-input=atomic\n"
            << "start=" << original.mPosition[0] << ',' << original.mPosition[1] << ',' << original.mPosition[2]
            << " end=" << previous.mPosition[0] << ',' << previous.mPosition[1] << ',' << previous.mPosition[2] << '\n'
            << "Frozen content collision scene; explicit movement only; no AI, scripts, inventory mutation or live replication.\n"
            << scene.fingerprint();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL native-interior-npc: " << error.what() << '\n';
        return 1;
    }
}
