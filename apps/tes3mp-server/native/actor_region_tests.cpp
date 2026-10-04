#include "actor_scene.hpp"
#include "actor_processing_regions.hpp"
#include "loadout.hpp"
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadnpc.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }

    template <class F> void rejects(F&& operation)
    {
        bool rejected = false;
        try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid processing domain was accepted");
    }

    void check(const std::filesystem::path& scratch, const std::filesystem::path& config,
        const std::filesystem::path& settings)
    {
        using namespace TES3MP::Native;
        const auto cell = [](int x) { return ESM::RefId::esm3ExteriorCell(x, 100); };
        const auto interior = ESM::RefId::stringRefId("Region Interior");
        std::vector<ESM::RefId> cells;
        for (int x = -2; x <= 9; ++x) cells.push_back(cell(x));
        cells.push_back(cell(20)); cells.push_back(interior);
        const auto regions = actorProcessingRegions(cells);
        require(regions.size() == 3 && regions[0].size() == 12 && regions[1] == std::vector{cell(20)}
            && regions[2] == std::vector{interior}, "Connected cells or separate spaces were partitioned incorrectly");
        rejects([&] { actorProcessingRegions(std::vector{cell(0), cell(0)}); });
        rejects([&] { actorProcessingRegions(std::vector{cell(32768)}); });
        std::vector<ESM::RefId> tooMany;
        for (int x = 0; x <= 256; ++x) tooMany.push_back(cell(x));
        rejects([&] { actorProcessingRegions(tooMany); });
        require(actorProcessingRegions(std::span<const ESM::RefId>(tooMany).first(256))[0].size() == 256,
            "Maximum bound region was rejected");
        const auto legacy = actorProcessingRegions(std::vector{cell(-1), cell(0), cell(1)});
        require(legacy[0] == std::vector{cell(0), cell(-1), cell(1)}, "Legacy neighborhood anchor changed");

        require(std::filesystem::create_directory(scratch), "Region scratch already exists");
        std::filesystem::create_directory(scratch / "openmw");
        std::filesystem::copy_file(config / "openmw.cfg", scratch / "openmw/openmw.cfg");
        constexpr float y = 100 * 8192.f + 1000, boundary = 3 * 8192.f;
        const auto record = ESM::RefId::stringRefId("region_actor");
        {
            const auto directory = config.string();
            const char* args[]{"actor-regions", "--config", directory.c_str()};
            Loadout base(readLoadoutOptions(3, args));
            auto npc = *base.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("player"));
            npc.mId = record; npc.mScript = {}; npc.mInventory.mList.clear();
            std::ofstream stream(scratch / "Regions.esp", std::ios::binary);
            ESM::ESMWriter out; out.setVersion(); out.setFormatVersion(ESM::DefaultFormatVersion); out.setType(0);
            out.addMaster("Morrowind.esm", 0); out.save(stream);
            out.startRecord(ESM::NPC::sRecordId, 0); npc.save(out); out.endRecord(ESM::NPC::sRecordId);
            for (auto id : cells)
            {
                ESM::Cell value; value.blank();
                const auto* exterior = id.getIf<ESM::ESM3ExteriorCellRefId>();
                if (exterior) { value.mData.mX = exterior->getX(); value.mData.mY = exterior->getY(); }
                else { value.mName = "Region Interior"; value.mData.mFlags = ESM::Cell::Interior; }
                value.updateId();
                out.startRecord(ESM::Cell::sRecordId, 0); value.save(out);
                const int x = exterior ? exterior->getX() : -999;
                if (x == 2 || x == 3 || x == 20)
                {
                    ESM::CellRef ref; ref.blank(); ref.mRefNum = {unsigned(x), 0}; ref.mRefID = record;
                    ref.mPos = {{x == 2 ? boundary - 100 : x == 3 ? boundary + 100 : 20 * 8192.f + 100,
                        y, 257}, {0, 0, 0}};
                    ref.save(out);
                }
                out.endRecord(ESM::Cell::sRecordId);
                if (!exterior) continue;
                ESM::Land land; land.blank(); land.mX = x; land.mY = 100;
                land.mFlags = ESM::Land::Flag_HeightsNormals; land.add(ESM::Land::DATA_VHGT);
                land.mLandData->mHeights.fill(256);
                land.mLandData->mMinHeight = land.mLandData->mMaxHeight = 256;
                out.startRecord(ESM::Land::sRecordId, 0); land.save(out); out.endRecord(ESM::Land::sRecordId);
            }
            out.close();
            std::ofstream cfg(scratch / "openmw/openmw.cfg", std::ios::app);
            cfg << "\ndata=" << std::quoted(scratch.generic_string()) << "\ncontent=Regions.esp\n";
        }
        const auto directory = (scratch / "openmw").string();
        const char* args[]{"actor-regions", "--config", directory.c_str()};
        Loadout loadout(readLoadoutOptions(3, args));
        const auto a = loadout.placedActors(cell(2)).at(0).mIdentity;
        const auto b = loadout.placedActors(cell(3)).at(0).mIdentity;
        const auto c = loadout.placedActors(cell(20)).at(0).mIdentity;
        using Domains = std::vector<std::pair<uint64_t, std::vector<ESM::RefId>>>;
        const Domains domains{{a, {cell(1), cell(2), cell(3)}}, {b, {cell(3), cell(4), cell(5)}}, {c, {cell(20)}}};
        const auto create = [&] { return std::make_unique<InteriorActorScene>(loadout, domains,
            "meshes/base_anim.nif", "meshes/base_animkna.nif", std::span<const DynamicActorBody>{}, cells); };
        auto scene = create();
        require(scene->neighborSnapshots().size() == 2 && scene->forCell(cell(9)).actorId() == a
            && scene->forCell(cell(20)).actorId() == c && scene->forCell(interior).actorId() == 0,
            "Region ownership lost, duplicated or mixed actors/scenery");
        require(scene->sharesCollisionWorld(scene->forActor(b)) && !scene->sharesCollisionWorld(scene->forActor(c))
            && scene->bodyCount() == 2 && scene->forActor(b).bodyCount() == 2 && scene->forActor(c).bodyCount() == 1,
            "Overlapping regions duplicated collision bodies");
        const auto hit = scene->projectileContact({boundary - 80, y, 300}, {boundary + 150, y, 300}, a);
        require(hit && hit->actor == b && scene->forActor(c).lineOfSight(
            {boundary - 80, y, 300}, {boundary + 150, y, 300}), "Cross-boundary actor collision or separate-region isolation failed");
        rejects([&] { InteriorActorScene invalid(loadout, Domains{{a, {cell(0)}}, {a, {cell(1)}}},
            "meshes/base_anim.nif", "meshes/base_animkna.nif"); });
        rejects([&] { InteriorActorScene invalid(loadout, Domains{{a, {cell(2), interior}}},
            "meshes/base_anim.nif", "meshes/base_animkna.nif"); });
        rejects([&] { InteriorActorScene invalid(loadout, Domains{{a, {cell(2)}}},
            "meshes/base_anim.nif", "meshes/base_animkna.nif", {}, tooMany); });

        scene->enableNavigation(settings.string());
        const auto initial = scene->image();
        const auto isolated = scene->neighborImage(1);
        const std::array movements{ActorMovement{.walkSpeed = 0}, ActorMovement{.walkSpeed = 0}};
        const std::array<std::optional<std::array<float, 3>>, 2> destinations{};
        const std::array<uint8_t, 2> active{1, 0};
        bool crossed = false, blocked = false;
        for (int tick = 0; tick < 100; ++tick)
        {
            const auto before = scene->image();
            const auto previousPosition = scene->snapshot().mPosition;
            auto step = scene->prepareNavigation(ActorMovement{.walkSpeed = 120}, {},
                std::array{boundary + 200, y, 257.f});
            scene->prepareNeighborNavigation(*step, movements, {}, destinations, {}, active);
            scene->prepareSceneryDoors(*step, {});
            require(scene->canInstall(*step), "Region candidate is incomplete");
            require(scene->image() == before, "Rejected region preparation mutated committed frames");
            if (tick == 25)
            {
                auto retry = scene->prepareNavigation(ActorMovement{.walkSpeed = 120}, {},
                    std::array{boundary + 200, y, 257.f});
                scene->prepareNeighborNavigation(*retry, movements, {}, destinations, {}, active);
                scene->prepareSceneryDoors(*retry, {});
                require(std::ranges::equal(step->image(), retry->image()), "Retry changed region simulation");
            }
            scene->install(*step);
            const auto frame = scene->snapshot();
            crossed |= frame.mPosition[0] >= boundary;
            blocked |= crossed && std::abs(frame.mPosition[0] - previousPosition[0]) < .1f;
            require(frame.mPosition[2] > 255 && frame.mPosition[2] < 260
                && scene->neighborImage(1) == isolated, "Crossing lost terrain or stepped the inactive region");
        }
        if (!crossed || !blocked || scene->snapshot().mPosition[0] >= boundary + 100)
            throw std::runtime_error("Crossing/contact failed: x=" + std::to_string(scene->snapshot().mPosition[0])
                + " crossed=" + std::to_string(crossed) + " blocked=" + std::to_string(blocked)
                + " pathUnavailable=" + std::to_string(scene->pathUnavailable()));
        const float destination = 6 * 8192.f + 200;
        bool traversed = false;
        for (int tick = 0; tick < 240; ++tick)
        {
            const auto position = scene->forActor(b).snapshot().mPosition;
            const float speed = std::min(4096.f, std::max(60.f, std::abs(destination - position[0]) * 15.f));
            const std::array traveling{ActorMovement{.walkSpeed = speed}, ActorMovement{.walkSpeed = 0}};
            const std::array<std::optional<std::array<float, 3>>, 2> targets{
                std::array{std::min(destination, position[0] + 512), y, 257.f}, std::nullopt};
            auto step = scene->prepareNavigation(ActorMovement{.walkSpeed = 0});
            scene->prepareNeighborNavigation(*step, traveling, {}, targets, {}, active);
            scene->prepareSceneryDoors(*step, {});
            require(scene->canInstall(*step), "Traveling region candidate is incomplete");
            scene->install(*step);
            require(scene->neighborImage(1) == isolated, "Travel demand stepped a disconnected region");
            if (std::abs(scene->forActor(b).snapshot().mPosition[0] - destination) < 16)
            { traversed = true; break; }
        }
        if (!traversed || scene->forActor(b).snapshot().mPosition[2] <= 255)
            throw std::runtime_error("Region traversal failed: x="
                + std::to_string(scene->forActor(b).snapshot().mPosition[0])
                + " y=" + std::to_string(scene->forActor(b).snapshot().mPosition[1])
                + " z=" + std::to_string(scene->forActor(b).snapshot().mPosition[2])
                + " noPath=" + std::to_string(scene->forActor(b).pathUnavailable()));
        const auto committed = scene->image();
        auto fresh = create(); fresh->enableNavigation(settings.string());
        require(fresh->fingerprint() == scene->fingerprint(), "Region reconstruction changed binding");
        auto restored = fresh->prepareRestore(committed); fresh->install(*restored);
        require(fresh->image() == committed, "Region restart changed frames");
        const std::array<std::optional<std::array<float, 3>>, 2> resumedTargets{
            std::array{destination + 200, y, 257.f}, std::nullopt};
        const std::array resumedMovements{ActorMovement{.walkSpeed = 120}, ActorMovement{.walkSpeed = 0}};
        const auto continuation = [&](InteriorActorScene& owner) {
            auto next = owner.prepareNavigation(ActorMovement{.walkSpeed = 0});
            owner.prepareNeighborNavigation(*next, resumedMovements, {}, resumedTargets, {}, active);
            owner.prepareSceneryDoors(*next, {});
            return next;
        };
        auto uninterrupted = continuation(*scene), recovered = continuation(*fresh);
        require(std::ranges::equal(uninterrupted->image(), recovered->image()) && scene->image() == committed,
            "Recovered region navigation diverged or leaked a candidate");
        scene->unload();
        require(!scene->loaded(), "Region collision resources did not unload");
        scene->reload(*fresh);
        require(scene->image() == committed && scene->bodyCount() == 2
            && scene->sharesCollisionWorld(scene->forActor(b)), "Region reentry changed frames or duplicated bodies");
        auto reset = scene->prepareRestore(initial); scene->install(*reset);
        require(scene->image() == initial, "Region rollback failed to restore all actors");
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4) throw std::invalid_argument("Usage: actor_region_tests NEW_SCRATCH CONFIG SETTINGS");
        check(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
        std::cout << "PASS actor-regions (synthetic terrain, shared stock physics/navigation, crossing/rollback/restart/reentry)\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL actor-regions: " << error.what() << '\n'; return 1; }
}
