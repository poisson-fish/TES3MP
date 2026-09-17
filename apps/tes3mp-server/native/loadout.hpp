#ifndef TES3MP_NATIVE_LOADOUT_H
#define TES3MP_NATIVE_LOADOUT_H

#include "diagnostic.hpp"

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/toutf8/toutf8.hpp>

namespace TES3MP::Native
{
    struct LoadoutOptions
    {
        std::vector<std::filesystem::path> mConfigPaths;
        std::vector<std::filesystem::path> mDataPaths;
        std::vector<std::string> mContent;
        std::string mEncoding;
        bool mSample = false;
        std::string mInventoryItem;
        std::string mEnchantment;
        std::string mEquipment;
        std::string mEquipmentContainer;
        std::vector<std::string> mEquipmentActors;
        std::filesystem::path mEquipmentSaveDirectory;
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
        std::string contentFingerprint() const;
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
