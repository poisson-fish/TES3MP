#include "loadout.hpp"
#include "equipment_runtime.hpp"

#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <components/files/hash.hpp>
#include <components/files/openfile.hpp>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace TES3MP::Native
{
    void Loadout::writeEquipmentProbe(std::ostream& output)
    {
        const auto id = [](std::string_view value) {
            if (value.empty() || value.size() > 256)
                throw std::invalid_argument("Equipment probe expects 1..256 byte record IDs");
            for (unsigned char c : value)
                if (c < 32 || c == 127)
                    throw std::invalid_argument("Equipment probe record ID contains a control character");
            return ESM::RefId::stringRefId(value);
        };
        if (mOptions.mEquipmentActors.size() != 2 || mOptions.mEquipmentSaveDirectory.empty())
            throw std::invalid_argument("Equipment probe requires explicit actors and save directory");
        const auto shirtId = id(mOptions.mEquipment);
        const auto* shirt = mStore.get<ESM::Clothing>().find(shirtId);
        const bool stats = !shirt->mEnchant.empty() || !shirt->mScript.empty();
        const auto container = mOptions.mEquipmentContainer.empty() ? ESM::RefId{} : id(mOptions.mEquipmentContainer);
        if (!container.empty() && stats)
            throw std::invalid_argument("Container probe starts with a plain shirt");
        const std::array<EquipmentActorBinding, 2> actors{{
            { id(mOptions.mEquipmentActors[0]), shirtId, shirt->mScript.empty() ? 3 : 1, stats },
            { id(mOptions.mEquipmentActors[1]), shirtId, shirt->mScript.empty() ? -5 : -1, stats }
        }};

        // Reuse OpenMW's file fingerprint for this local diagnostic composition.
        // Bind ordered bytes, encoding and startup actor/item roles. This is not
        // an authenticated multiplayer pack manifest. Content stays immutable.
        std::ostringstream identity;
        identity << "equipment-probe-3\n" << mOptions.mEncoding << '\n';
        identity << container << '\n';
        for (const auto& actor : actors)
            identity << actor.mBase << '\n' << actor.mShirt << '\n' << actor.mCount << '\n';
        for (size_t i = 0; i < mFiles.size(); ++i)
        {
            auto stream = Files::openBinaryInputFileStream(mFiles[i]);
            const auto hash = Files::getHash(mOptions.mContent[i], *stream);
            identity << mOptions.mContent[i].size() << ':' << mOptions.mContent[i] << '\n'
                     << hash[0] << ':' << hash[1] << '\n';
        }
        std::istringstream identityStream(identity.str());
        const auto hash = Files::getHash("equipment-loadout", identityStream);
        std::array<unsigned char, 32> contentIdentity{};
        for (size_t i = 0; i < 16; ++i)
            contentIdentity[i] = static_cast<unsigned char>(hash[i / 8] >> ((i % 8) * 8));

        MWScript::CompilerContext compiler(MWScript::CompilerContext::Type_Full);
        MWScript::ScriptManager declarations(mStore, compiler, 1);
        std::shared_ptr<const MWWorld::EquipmentScriptLocals> locals;
        if (!shirt->mScript.empty())
            locals = std::make_shared<const MWWorld::EquipmentScriptLocals>(mStore, shirt->mScript, declarations);

        if (!std::filesystem::create_directory(mOptions.mEquipmentSaveDirectory))
            throw std::invalid_argument("Equipment save directory must be new (existing files are never overwritten)");
        std::ostringstream report;
        report << "native-equipment-runtime\t3\ncontent-fingerprint\t";
        for (auto byte : contentIdentity)
            report << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(byte);
        report << std::dec << "\nshirt\t" << shirtId << "\nnpc-stats\t" << stats << '\n';
        if (!container.empty()) report << "container\t" << container << "\tdiagnostic-start=empty\n";
        const auto path = mOptions.mEquipmentSaveDirectory / "session.equipment";
        EquipmentBytes bytes;
        std::unique_ptr<const EquipmentSuccess> success;
        FileFaults faults;
        InventoryInstanceId sharedItem;
        std::vector<ESM::RefId> ids{ shirtId, actors[0].mBase, actors[1].mBase };
        {
            MWWorld::WorldModel world(mStore, mReaders, 1);
            MWWorld::LocalScripts scripts(mStore);
            EquipmentRuntime runtime(mStore, world, scripts, "native-equipment-probe-3", contentIdentity,
                actors, locals, &declarations, {}, true, container);
            EquipmentFileSink file(path, true);
            InventoryInstanceId received;
            if (!stats)
            {
                auto command = runtime.transferCommand(0, runtime.command(0, true).mItem, 2);
                std::unique_ptr<const InventoryTransferSuccess> transferred;
                if (!container.empty())
                {
                    command = runtime.containerCommand(0, true, command.mItem, 2);
                    if (runtime.execute({ command.mInitiator }, command, file, transferred, bytes, faults)
                        != PersistenceResult::Accepted || transferred->mSourceCount != 1 || transferred->mDestinationCount != 2)
                        throw std::runtime_error("Connected drop was not durably accepted");
                    sharedItem = transferred->mDestinationItem;
                    report << "drop\t0->container\tquantity=2\tsource=1\tcontainer=2\titem=" << sharedItem.mIndex << '\n';
                    // Leave one shirt in the shared container for fresh recovery.
                    command = runtime.containerCommand(1, false, sharedItem, 1);
                }
                if (runtime.execute({ command.mInitiator }, command, file, transferred, bytes, faults)
                    != PersistenceResult::Accepted)
                    throw std::runtime_error("Connected transfer was not durably accepted");
                received = transferred->mDestinationItem;
                report << (container.empty() ? "transfer\t0->1\tquantity=2" : "take\tcontainer->1\tquantity=1")
                       << "\tsource=" << transferred->mSourceCount
                       << "\tdestination=" << transferred->mDestinationCount << "\titem=" << received.mIndex << '\n';
            }
            for (size_t actor : { size_t(1), size_t(0) })
            {
                auto command = runtime.command(actor, true);
                if (actor == 1 && !stats) command.mItem = received;
                if (runtime.execute({ command.mActor }, command, file, success, bytes, faults) != PersistenceResult::Accepted)
                    throw std::runtime_error("Connected equipment was not durably accepted");
                if (stats)
                {
                    MWWorld::ManualRef base(mStore, actors[actor].mBase);
                    base.getPtr().getCellRef().setRefNum({ command.mActor.mIndex, command.mActor.mContentFile });
                    const MWWorld::EquipmentNpcStats initial(base.getPtr(), mStore);
                    for (auto spell : initial.values().mSpells) if (!spell.empty()) ids.push_back(spell);
                }
                report << "actor\t" << actor << "\tbase=" << actors[actor].mBase
                       << "\tequipped=" << success->mShirt.mIndex << "\trevision=" << success->mRevision << '\n';
            }
        }
        // All owners and their registry have died. Restore the one accepted
        // session, including the shared container, before either actor continues.
        MWWorld::WorldModel world(mStore, mReaders, 1);
        MWWorld::LocalScripts scripts(mStore);
        EquipmentRuntime fresh(mStore, world, scripts, "native-equipment-probe-3", contentIdentity,
            actors, locals, &declarations, 2, true, container);
        std::unique_ptr<const EquipmentSessionValues> restored;
        EquipmentBytes accepted;
        if (fresh.restartSession(path, ids, restored, accepted, faults) != FileReadResult::Read || accepted != bytes)
            throw std::runtime_error("Connected fresh recovery mismatch");
        EquipmentFileSink file(path, true);
        for (size_t actor = 0; actor < 2; ++actor)
        {
            const auto command = fresh.command(actor, false);
            if (fresh.execute({ command.mActor }, command, file, success, bytes, faults) != PersistenceResult::Accepted
                || success->mShirt.mIndex != 0)
                throw std::runtime_error("Connected recovered continuation failed");
            report << "restored\t" << actor << "\tcontinued-unequipped\tbytes=" << bytes.size() << '\n';
        }
        if (!stats)
        {
            const auto command = container.empty() ? fresh.transferCommand(1, fresh.command(1, true).mItem, 1)
                : fresh.containerCommand(0, false, sharedItem, 1);
            std::unique_ptr<const InventoryTransferSuccess> transferred;
            if (fresh.execute({ command.mInitiator }, command, file, transferred, bytes, faults) != PersistenceResult::Accepted)
                throw std::runtime_error("Connected recovered return transfer failed");
            report << (container.empty() ? "return-transfer\t1->0" : "recovered-take\tcontainer->0")
                   << "\tquantity=1\tsource=" << transferred->mSourceCount
                   << "\tdestination=" << transferred->mDestinationCount << '\n';
        }
        if (!container.empty())
        {
            if (!restored->mContainer || restored->mContainer->mObjects.size() != 1
                || restored->mContainer->mObjects[0].mRef.mCount != 1)
                throw std::runtime_error("Shared container recovery lost its remaining shirt");
            for (size_t actor = 0; actor < 2; ++actor)
            {
                const auto equip = fresh.command(actor, true);
                if (fresh.execute({ equip.mActor }, equip, file, success, bytes, faults) != PersistenceResult::Accepted)
                    throw std::runtime_error("Recovered container actors could not equip again");
                report << "continued-equip\t" << actor << "\tshirt=" << success->mShirt.mIndex << '\n';
            }
        }
        report << "complete\n";
        output << report.str();
        if (!output) throw std::runtime_error("Equipment probe report write failed");
    }
}
