#include "inventory_service_tests.hpp"
#include "loadout.hpp"
#include "inventory_service.hpp"
#include "../canonical_persistence_file.hpp"
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadrace.hpp>
#include <fstream>
#include <stdexcept>

namespace TES3MP::ServerApp::Testing
{
    void nativeInventoryApplication(NativeInventoryService&, CanonicalPersistenceFile&);
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
        void plugin(const std::filesystem::path& path, bool patch)
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
                ESM::Clothing shirt; shirt.blank(); shirt.mId = ESM::RefId::stringRefId("shirt");
                shirt.mData.mType = ESM::Clothing::Shirt; write(out, shirt);
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
            out.endRecord(ESM::REC_CELL); out.close();
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
}
