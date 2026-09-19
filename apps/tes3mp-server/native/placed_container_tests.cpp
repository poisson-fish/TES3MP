#include "inventory_service_tests.hpp"
#include "loadout.hpp"
#include "inventory_service.hpp"
#include "../canonical_persistence_file.hpp"
#include "../native_environment_service.hpp"
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadrace.hpp>
#include <fstream>
#include <stdexcept>
#include <limits>

namespace TES3MP::ServerApp::Testing
{
    void nativeInventoryApplication(NativeInventoryService&, CanonicalPersistenceFile&, NativeEnvironmentService* = nullptr);
}

namespace TES3MP::Native::Testing
{
    namespace
    {
        void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
        template<class T> void write(ESM::ESMWriter& out, const T& value)
        {
            out.startRecord(T::sRecordId, 0); value.save(out); out.endRecord(T::sRecordId);
        }
        void plugin(const std::filesystem::path& path, bool patch, bool invalidTeleport = false)
        {
            std::ofstream stream(path, std::ios::binary);
            ESM::ESMWriter out;
            out.setVersion(); out.setFormatVersion(ESM::DefaultFormatVersion); out.setType(patch ? 0 : 1);
            if (patch) out.addMaster("Base.esm", 0);
            out.save(stream);
            if (!patch)
            {
                ESM::Class klass; klass.blank(); klass.mId = ESM::RefId::stringRefId("class"); write(out, klass);
                ESM::Race race; race.blank(); race.mId = ESM::RefId::stringRefId("race"); write(out, race);
                for (const auto* name : {"barrel", "chest"})
                {
                    ESM::Container base; base.blank(); base.mId = ESM::RefId::stringRefId(name); base.mWeight = 1000; write(out, base);
                }
                ESM::NPC npc; npc.blank(); npc.mId = ESM::RefId::stringRefId("actor");
                npc.mClass = klass.mId; npc.mRace = race.mId; write(out, npc);
                ESM::Script script; script.blank(); script.mId = ESM::RefId::stringRefId("actor_script");
                script.mScriptText = "begin actor_script\nend actor_script\n"; write(out, script);
                npc.mId = ESM::RefId::stringRefId("scripted_actor"); npc.mScript = script.mId; write(out, npc);
                ESM::Creature creature; creature.blank(); creature.mId = ESM::RefId::stringRefId("creature"); write(out, creature);
                ESM::CreatureLevList leveled; leveled.blank(); leveled.mId = ESM::RefId::stringRefId("leveled_actor"); write(out, leveled);
                ESM::Clothing shirt; shirt.blank(); shirt.mId = ESM::RefId::stringRefId("shirt");
                shirt.mData.mType = ESM::Clothing::Shirt; write(out, shirt);
                shirt.mId = ESM::RefId::stringRefId("scripted_item"); shirt.mScript = script.mId; write(out, shirt);
                ESM::Light fixed; fixed.blank(); fixed.mId = ESM::RefId::stringRefId("fixed_light"); write(out, fixed);
                ESM::Door door; door.blank(); door.mId = ESM::RefId::stringRefId("door"); write(out, door);
                ESM::ItemLevList items; items.blank(); items.mId = ESM::RefId::stringRefId("leveled_item"); write(out, items);
            }
            ESM::Cell cell; cell.blank(); cell.mName = "Placed test"; cell.mData.mFlags = ESM::Cell::Interior;
            cell.updateId();
            out.startRecord(ESM::REC_CELL, 0); cell.save(out);
            for (uint32_t i = 1; i <= 4; ++i)
            {
                ESM::CellRef ref; ref.blank(); ref.mRefNum = {i, patch ? 1 : 0};
                ref.mRefID = ESM::RefId::stringRefId(i == 1 ? "barrel" : "chest");
                ref.mPos.pos[0] = patch ? float(i * 2) : float(i);
                if (i == 4) { ref.mIsLocked = true; ref.mLockLevel = 0; }
                ref.save(out, false, false, patch && i == 3);
            }
            out.endRecord(ESM::REC_CELL);
            cell.mName = "Shared test"; cell.updateId();
            out.startRecord(ESM::REC_CELL, 0); cell.save(out);
            for (uint32_t i : {7, 6, 8})
            {
                ESM::CellRef ref; ref.blank(); ref.mRefNum = {i, patch ? 1 : 0};
                ref.mRefID = ESM::RefId::stringRefId(i == 6 ? "barrel" : "chest");
                ref.mPos.pos[0] = patch ? float(i * 2) : float(i);
                ref.save(out, false, false, patch && i == 8);
            }
            out.endRecord(ESM::REC_CELL);
            for (const auto* name : {"World items", "Scripted world items", "Leveled world items"})
            {
                cell.mName = name; cell.updateId();
                out.startRecord(ESM::REC_CELL, 0); cell.save(out);
                for (uint32_t i : {204, 203, 201, 202})
                {
                    ESM::CellRef ref; ref.blank(); ref.mRefNum = {i, patch ? 1 : 0};
                    ref.mRefID = ESM::RefId::stringRefId(std::string_view(name) == "Scripted world items" ? "scripted_item"
                        : std::string_view(name) == "Leveled world items" ? "leveled_item" : "shirt");
                    ref.mPos.pos[0] = patch ? float(i * 2) : float(i);
                    ref.mCount = 2;
                    ref.save(out, false, false, patch && i == 204);
                }
                ESM::CellRef fixed; fixed.blank(); fixed.mRefNum = {205, patch ? 1 : 0};
                fixed.mRefID = ESM::RefId::stringRefId("fixed_light"); fixed.save(out);
                out.endRecord(ESM::REC_CELL);
            }
            for (const auto* name : {"Actor test", "Scripted actor test", "Leveled actor test"})
            {
                cell.mName = name; cell.updateId();
                out.startRecord(ESM::REC_CELL, 0); cell.save(out);
                for (uint32_t i : {103, 101, 104, 102})
                {
                    ESM::CellRef ref; ref.blank(); ref.mRefNum = {i, patch ? 1 : 0};
                    ref.mRefID = ESM::RefId::stringRefId(std::string_view(name) == "Scripted actor test" ? "scripted_actor"
                        : std::string_view(name) == "Leveled actor test" ? "leveled_actor" : i == 103 ? "creature" : "actor");
                    ref.mPos.pos[0] = patch ? float(i * 2) : float(i);
                    ref.save(out, false, false, patch && i == 104);
                }
                out.endRecord(ESM::REC_CELL);
            }
            cell.mName = "Exterior references"; cell.mData.mFlags = 0;
            cell.mData.mX = -2; cell.mData.mY = -3; cell.updateId();
            out.startRecord(ESM::REC_CELL, 0); cell.save(out);
            for (uint32_t i = 301; i <= 308; ++i)
            {
                ESM::CellRef ref; ref.blank(); ref.mRefNum = {i, patch ? 1 : 0};
                ref.mRefID = ESM::RefId::stringRefId(i < 305 ? "chest" : i == 305 ? "actor" : i == 306 ? "shirt" : "door");
                ref.mPos.pos[0] = -16384 + (patch ? 64.f : 32.f); ref.mPos.pos[1] = -24576 + 64.f;
                if (i == 308)
                {
                    ref.mTeleport = true; ref.mDestCell = "sHaReD tEsT";
                    if (invalidTeleport)
                    {
                        ref.mDestCell.clear(); ref.mDoorDest.pos[0] = std::numeric_limits<float>::infinity();
                    }
                }
                if (patch && i == 304)
                {
                    out.writeHNT("MVRF", uint32_t(0x01000000 | i));
                    const int32_t target[2]{-1, -3}; out.writeHNT("CNDT", target);
                    ref.mPos.pos[0] += 8192;
                }
                ref.save(out, false, false, patch && i == 303);
            }
            out.endRecord(ESM::REC_CELL);
            cell.mData.mX = -1; cell.updateId();
            out.startRecord(ESM::REC_CELL, 0); cell.save(out); out.endRecord(ESM::REC_CELL);
            out.close();
        }
    }

    void checkExteriorReferences(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Exterior reference scratch already exists");
        plugin(scratch / "Base.esm", false); plugin(scratch / "Patch.esp", true);
        { std::ofstream out(scratch / "empty.omwscripts"); out << "# no scripts\n"; }
        LoadoutOptions options; options.mDataPaths = {scratch};
        options.mContent = {"Base.esm", "empty.omwscripts", "Patch.esp"}; options.mEncoding = "win1252";
        Loadout loadout(options);
        const auto source = ESM::RefId::esm3ExteriorCell(-2, -3), target = ESM::RefId::esm3ExteriorCell(-1, -3);
        const auto containers = loadout.resolveContainers(source, 32);
        const auto moved = loadout.resolveContainers(target, 32);
        require(containers.size() == 2 && containers[0].mRef.mRefNum == ESM::RefNum{301, 0}
            && containers[1].mRef.mRefNum == ESM::RefNum{302, 0} && containers[0].mRef.mPos.pos[0] == -16320
            && moved.size() == 1 && moved[0].mRef.mRefNum == ESM::RefNum{304, 0}
            && moved[0].mRef.mPos.pos[0] == -8128, "Exterior override/deletion/moved-reference resolution failed");
        require(loadout.resolveActors(source, 32).size() == 1 && loadout.placedItems(source, 64).size() == 1,
            "Exterior actor or item reference discovery failed");
        const auto ordinary = loadout.resolveDoor(source, "BASE.ESM", 307);
        const auto teleports = loadout.teleportDoors(source, interiorCell("Shared test"));
        require(ordinary.mRef.mRefNum == ESM::RefNum{307, 0} && teleports.size() == 1
            && teleports[0].mRef.mRefNum == ESM::RefNum{308, 0}
            && loadout.teleportDoors(source, target).empty(), "Exterior ordinary/teleport door resolution failed");
        const std::vector<std::string> desktop{"builtin.omwscripts", "Base.esm", "empty.omwscripts", "Patch.esp"};
        require(MWWorld::localPlacedRef(moved[0].mIdentity, desktop) == ESM::RefNum{304, 1},
            "Exterior reference identity changed with script reader slots");
        for (auto invalid : {ESM::RefId{}, ESM::RefId::generated(1), ESM::RefId::esm3ExteriorCell(INT32_MAX, 0)})
        {
            bool rejected = false;
            try { loadout.placedContainers(invalid); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Unbounded or invalid native cell accepted");
        }
        bool rejected = false;
        try { loadout.resolveContainers(source, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && loadout.resolveContainers(source, 32).size() == 2,
            "Exterior over-budget discovery mutated the next result");
        plugin(scratch / "Bad.esp", true, true); options.mContent.push_back("Bad.esp");
        Loadout bad(options);
        rejected = false;
        try { bad.teleportDoors(source, target); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Nonfinite exterior destination reached engine coordinate conversion");
    }

    void checkPlacedItems(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Placed item scratch already exists");
        plugin(scratch / "Base.esm", false); plugin(scratch / "Patch.esp", true);
        LoadoutOptions options; options.mDataPaths = {scratch};
        options.mContent = {"Base.esm", "Patch.esp"}; options.mEncoding = "win1252";
        Loadout loadout(options);
        const auto items = loadout.placedItems("World items", 64);
        require(items.size() == 3 && items[0].mRef.mRefNum.mIndex == 201 && items[2].mRef.mRefNum.mIndex == 203
            && items[0].mRef.mPos.pos[0] == 402 && items[0].mRef.mCount == 2,
            "World discovery lost winning overrides, deletion, sorting or repeated bases");
        for (const auto name : {"Scripted world items", "Leveled world items", "World items"})
        {
            bool rejected = false;
            try { loadout.placedItems(name, std::string_view(name) == "World items" ? 2 : 64); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "World discovery silently skipped scripts/spawns or exceeded capacity");
        }
    }

    void checkPlacedContainers(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Placed test scratch already exists");
        plugin(scratch / "Base.esm", false); plugin(scratch / "Patch.esp", true);
        { std::ofstream file(scratch / "empty.omwscripts"); file << "# synthetic script list\n"; }
        LoadoutOptions options; options.mDataPaths = {scratch};
        options.mContent = {"Base.esm", "empty.omwscripts", "Patch.esp"}; options.mEncoding = "win1252";
        Loadout loadout(options);
        const auto refs = loadout.placedContainers("Placed test");
        require(refs.size() == 3, "Deleted placed chest was included");
        const auto barrel = loadout.resolveContainer("Placed test", "BASE.ESM", 1);
        const auto chest = loadout.resolveContainer("Placed test", "Base.esm", 2);
        require(barrel.mRef.mPos.pos[0] == 2 && chest.mRef.mPos.pos[0] == 4,
            "Winning placed override position not used");
        require(chest.mRef.mRefID == ESM::RefId::stringRefId("chest") && chest.mIdentity != barrel.mIdentity,
            "Chest and barrel reference identity collided");
        const auto shared = loadout.resolveContainers("Shared test", MaxEquipmentContainers);
        require(shared.size() == 2 && shared[0].mRef.mRefNum.mIndex == 6 && shared[1].mRef.mRefNum.mIndex == 7
            && shared[0].mRef.mPos.pos[0] == 12 && shared[1].mRef.mPos.pos[0] == 14,
            "Whole-cell discovery lost deterministic ordering, override or deletion semantics");
        for (bool locked : {false, true})
        {
            bool rejected = false;
            try { loadout.resolveContainers(locked ? "Placed test" : "Shared test", locked ? MaxEquipmentContainers : 1); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Whole-cell bootstrap silently skipped a lock or exceeded its budget");
        }
        for (uint32_t bad : {3, 4, 999})
        {
            bool rejected = false;
            try { loadout.resolveContainer("Placed test", "Base.esm", bad); }
            catch (const std::exception&) { rejected = true; }
            require(rejected, "Deleted, locked or missing container accepted");
        }
        const std::vector<std::string> desktop{"builtin.omwscripts", "Base.esm", "empty.omwscripts", "Patch.esp"};
        const auto local = MWWorld::localPlacedRef(chest.mIdentity, desktop);
        require(local == ESM::RefNum{2, 1} && MWWorld::placedRefId(*local, desktop) == chest.mIdentity,
            "Desktop script-list reader offset changed reference identity");
        require(!MWWorld::localPlacedRef(90, desktop) && !MWWorld::placedRefId({1, -1}, desktop)
            && !MWWorld::localPlacedRef(MWWorld::PlacedRefTag | (uint64_t{9} << 32) | 1, desktop),
            "Legacy, dynamic or missing plugin accepted as placed identity");

        const auto player = [](uint64_t value) { return PlayerId::fromValue(value).value(); };
        for (const auto& placed : {barrel, chest})
        {
            const auto actor = ESM::RefId::stringRefId("actor"), shirt = ESM::RefId::stringRefId("shirt");
            InventoryServiceBinding binding{{player(1), player(2)}, ItemPrototypeId::fromValue(70).value(),
                {{{actor, shirt, 3}, {actor, shirt, 5}}}, {1, 2, 3},
                {{ContainerId::fromValue(placed.mIdentity).value(), CellId::interior(CellSpaceId::fromValue(7).value()),
                    Position3(int64_t(placed.mRef.mPos.pos[0] * 1024), 0, 0), placed.mRef.mRefID, placed.mRef}}};
            InventoryService original(loadout.store(), loadout.readers(), binding);
            std::array<std::byte, 32> configuration{}; configuration[0] = std::byte{1};
            const auto identity = CanonicalPersistenceIdentity::create(testContentManifestId(),
                ServerConfigurationId::fromBytes(configuration).value(), {}, {}).value();
            const auto path = scratch / (std::to_string(placed.mRef.mRefNum.mIndex) + ".world");
            auto file = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                ServerApp::CanonicalPersistenceFile::open(path, identity));
            ServerApp::Testing::nativeInventoryApplication(original, *file);
            InventoryService restored(loadout.store(), loadout.readers(), binding, true);
            const std::array records{actor, shirt};
            restored.recover(original.inventoryImage(), records);
            require(std::ranges::equal(original.inventoryImage(), restored.inventoryImage()),
                "Placed barrel/chest coherent recovery changed image");
            ++binding.mContainers[0].mPlacement->mRefNum.mIndex;
            InventoryService wrong(loadout.store(), loadout.readers(), binding, true);
            bool rejected = false;
            try { wrong.recover(original.inventoryImage(), records); }
            catch (const std::exception&) { rejected = true; }
            require(rejected && wrong.inventoryImage().empty(), "Wrong engine ref recovered or partially installed");
            auto reopened = std::get<std::unique_ptr<ServerApp::CanonicalPersistenceFile>>(
                ServerApp::CanonicalPersistenceFile::open(path, identity));
            ServerApp::Testing::nativeInventoryApplication(restored, *reopened);
        }
    }

    void checkPlacedActors(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Actor discovery scratch already exists");
        plugin(scratch / "Base.esm", false); plugin(scratch / "Patch.esp", true);
        { std::ofstream out(scratch / "empty.omwscripts"); out << "# no script execution\n"; }
        LoadoutOptions options; options.mDataPaths = {scratch};
        options.mContent = {"Base.esm", "empty.omwscripts", "Patch.esp"}; options.mEncoding = "win1252";
        Loadout loadout(options);
        const auto actors = loadout.resolveActors("Actor test", 3);
        require(actors.size() == 3 && actors[0].mRef.mRefNum == ESM::RefNum{101, 0}
            && actors[1].mRef.mRefNum == ESM::RefNum{102, 0} && actors[2].mRef.mRefNum == ESM::RefNum{103, 0}
            && actors[0].mRef.mPos.pos[0] == 202 && actors[1].mRef.mPos.pos[0] == 204
            && actors[0].mIdentity != actors[1].mIdentity && actors[0].mRef.mRefID == actors[1].mRef.mRefID
            && actors[2].mRef.mRefID == ESM::RefId::stringRefId("creature"),
            "Actor discovery lost winning overrides, deletions, placement order or repeated-base identities");
        require(loadout.resolveActors("Shared test", 1).empty(), "Empty actor domain is invalid");
        for (const auto* cell : {"Actor test", "Scripted actor test", "Leveled actor test"})
        {
            bool rejected = false;
            try { loadout.resolveActors(cell, std::string_view(cell) == "Actor test" ? 2 : 32); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Actor discovery silently skipped a script, spawn or over-budget placement");
        }
    }
}
