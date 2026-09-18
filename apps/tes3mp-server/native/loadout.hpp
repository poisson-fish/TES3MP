#ifndef TES3MP_NATIVE_LOADOUT_H
#define TES3MP_NATIVE_LOADOUT_H

#include "diagnostic.hpp"

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm3/cellref.hpp>
#include <components/toutf8/toutf8.hpp>

namespace TES3MP::Native
{
    struct LoadoutOptions
    {
        std::vector<std::filesystem::path> mConfigPaths;
        std::vector<std::filesystem::path> mDataPaths;
        std::vector<std::string> mContent;
        std::vector<std::string> mArchives;
        std::string mEncoding;
        bool mSample = false;
        std::string mInventoryItem;
        std::string mEnchantment;
        std::string mEquipment;
        std::string mEquipmentContainer;
        std::vector<std::string> mEquipmentActors;
        std::filesystem::path mEquipmentSaveDirectory;
        std::string mContainerCell;
    };

    LoadoutOptions readLoadoutOptions(int argc, const char* const argv[]);

    // Owns actual engine records and the readers needed for deferred cell data.
    // This is an offline TES3 content probe, not a gameplay or network catalog.
    class Loadout
    {
    public:
        explicit Loadout(LoadoutOptions options);
        const MWWorld::ESMStore& store() const { return mStore; }
        // Retained engine services for the app-owned multiplayer runtime.
        MWWorld::ESMStore& store() { return mStore; }
        ESM::ReadersCache& readers() { return mReaders; }
        const LoadoutOptions& options() const { return mOptions; }
        const ToUTF8::StatelessUtf8Encoder& encoder() const { return mEncoder.getStatelessEncoder(); }
        std::string contentFingerprint() const;
        struct PlacedInventory
        {
            ESM::CellRef mRef;
            uint64_t mIdentity;
            std::string mPlugin;
            bool mEmptyBase, mScripted;
        };
        // One interior's winning engine references, including barrels and chests.
        // No gameplay or base-inventory execution occurs during discovery.
        std::vector<PlacedInventory> placedContainers(std::string_view cell);
        PlacedInventory resolveContainer(std::string_view cell, std::string_view plugin, uint32_t index);
        std::vector<PlacedInventory> resolveContainers(std::string_view cell, size_t limit);
        // Actor discovery never initializes custom data, AI, scripts or loot.
        // NPC and creature placements use the same stable reference namespace.
        std::vector<PlacedInventory> placedActors(std::string_view cell);
        std::vector<PlacedInventory> resolveActors(std::string_view cell, size_t limit);
        std::vector<PlacedInventory> placedItems(std::string_view cell, size_t limit);
        void writeContainers(std::ostream& output, std::string_view cell);
        void enumerate(std::ostream& output) const;
        DiagnosticSample sample(const DiagnosticLimits& limits = {}) const;
        // Prepare completely before touching output. Stream/device write failure
        // itself cannot be rolled back; this is not a durable publication API.
        void writeSample(std::ostream& output, const DiagnosticLimits& limits = {}) const;
        // Disposable base ContainerStore operation, including script local initialization.
        // Does not initialize a live player InventoryStore or execute scripts.
        void writeInventoryProbe(std::ostream& output, std::string_view itemId);
        // Bounded, staged cost/charge diagnostics using the retained engine store.
        void writeEnchantmentProbe(std::ostream& output, std::string_view enchantmentId) const;
        // Durable equipment command, fresh owner recovery and continuation.
        // Retains bounded per-actor saves in a new private directory.
        void writeEquipmentProbe(std::ostream& output);

    private:
        LoadoutOptions mOptions;
        ToUTF8::Utf8Encoder mEncoder;
        ESM::ReadersCache mReaders;
        MWWorld::ESMStore mStore;
        std::vector<int> mVersions;
        std::vector<std::filesystem::path> mFiles;
    };

    // No record report is written until loading and engine normalization finish.
    void probe(int argc, const char* const argv[], std::ostream& output);
}

#endif
