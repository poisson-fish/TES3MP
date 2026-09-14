#include "loadout.hpp"

#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/misc/strings/lower.hpp>

namespace TES3MP::Native
{
    void Loadout::writeInventoryProbe(std::ostream& output, std::string_view itemId)
    {
        // Fixed counts, no externally supplied live Ptrs, and only MISC in this
        // slice. Other item classes can invoke further global gameplay services.
        if (itemId.empty() || itemId.size() > 256)
            throw std::runtime_error("Inventory probe expects a 1..256 byte item ID");
        for (unsigned char ch : itemId)
            if (ch < 32 || ch == 127)
                throw std::runtime_error("Inventory probe item ID contains a control character");
        const auto id = ESM::RefId::stringRefId(itemId);
        mStore.get<ESM::Miscellaneous>().find(id); // Reject other item classes before constructing a reference.

        MWClass::registerClasses();
        // The real registry and its lifetime bookkeeping, with a bounded cache.
        // No settings singleton, World, Environment, cell loading or scene setup.
        MWWorld::WorldModel worldModel(mStore, mReaders, 1);
        ESM::RefNum assigned;
        int notifications = 0;
        float weight = 0;
        std::string winningId;
        {
            // Explicit identity only: no NPC custom data or InventoryStore is
            // initialized. This is a disposable base store, not live actor state.
            MWWorld::ManualRef player(mStore, ESM::RefId::stringRefId("player"));
            MWWorld::ManualRef source(mStore, id, 2);
            const auto effectiveId
                = source.getPtr().getClass().isGold(source.getPtr()) ? MWWorld::ContainerStore::sGoldId : id;
            const auto* base = mStore.get<ESM::Miscellaneous>().find(effectiveId);
            if (!std::isfinite(base->mData.mWeight) || base->mData.mWeight < 0
                || base->mData.mWeight > std::numeric_limits<float>::max() / 3)
                throw std::runtime_error("Inventory probe item weight is invalid");
            MWWorld::ContainerStore container;
            const MWWorld::ContainerStoreAddContext context{ mStore, worldModel, player.getPtr(), player.getPtr(),
                nullptr, [&](const MWWorld::Ptr& owner) {
                    if (owner != player.getPtr())
                        throw std::runtime_error("Inventory presentation request has the wrong owner");
                    ++notifications;
                } };
            const auto first = container.add(source.getPtr(), 2, context);
            assigned = first->getCellRef().getRefNum();
            const auto second = container.add(source.getPtr(), 1, context);
            if (first != second || first->getCellRef().getCount() != 3
                || std::distance(container.begin(), container.end()) != 1 || worldModel.getPtr(assigned) != *first
                || first->getContainerStore() != &container || source.getPtr().getCellRef().getCount() != 2
                || notifications != 2)
                throw std::runtime_error("Native inventory insertion/stacking/registration invariant failed");
            weight = container.getWeight();
            if (!std::isfinite(weight))
                throw std::runtime_error("Native inventory total weight is not finite");
            const auto refId = first->getCellRef().getRefId().getRefIdString();
            if (refId.size() > 256)
                throw std::runtime_error("Inventory winning ID exceeds diagnostic limit");
            winningId = Misc::StringUtils::lowerCase(refId);
        }
        if (!worldModel.getPtr(assigned).isEmpty())
            throw std::runtime_error("Native inventory destruction left a registered reference");

        // Staged owned output only; fixed fields plus one bounded quoted ID.
        // Device write failure itself cannot be rolled back.
        std::ostringstream report;
        report.imbue(std::locale::classic());
        report << std::setprecision(std::numeric_limits<float>::max_digits10) << "native-inventory\t1\nitem\t"
               << std::quoted(winningId) << "\ncount\t3\nstacks\t1\nweight\t" << weight << "\npresentation-requests\t"
               << notifications << "\nregistered\t1\nderegistered\t1\nscript-executed\t0\ncomplete\n";
        const auto text = report.str();
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output)
            throw std::runtime_error("Failed writing native inventory report");
    }
}
