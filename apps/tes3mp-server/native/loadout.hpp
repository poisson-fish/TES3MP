#ifndef TES3MP_NATIVE_LOADOUT_H
#define TES3MP_NATIVE_LOADOUT_H

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
    };

    LoadoutOptions readLoadoutOptions(int argc, const char* const argv[]);

    // Owns actual engine records and the readers needed for deferred cell data.
    // This is an offline TES3 content probe, not a gameplay or network catalog.
    class Loadout
    {
    public:
        explicit Loadout(LoadoutOptions options);
        const MWWorld::ESMStore& store() const { return mStore; }
        void enumerate(std::ostream& output) const;

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
