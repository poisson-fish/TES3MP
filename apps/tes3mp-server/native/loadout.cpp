#include "loadout.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <type_traits>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <apps/openmw/mwworld/esmloader.hpp>
#include <components/esm/format.hpp>
#include <components/esm/records.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/files/openfile.hpp>
#include <components/misc/strings/lower.hpp>

namespace TES3MP::Native
{
    namespace
    {
        // Human-readable TSV: keep each external string on a single line.
        void field(std::ostream& out, std::string_view value)
        {
            constexpr char hex[] = "0123456789abcdef";
            out << '"';
            for (unsigned char ch : value)
            {
                if (ch == '\\' || ch == '"')
                    out << '\\' << ch;
                else if (ch < 32 || ch == 127)
                    out << "\\x" << hex[ch >> 4] << hex[ch & 15];
                else
                    out << ch;
            }
            out << '"';
        }

        template <class T>
        void enumerateStore(const MWWorld::ESMStore& store, std::ostream& out)
        {
            const auto& records = store.get<T>();
            std::vector<const T*> sorted;
            sorted.reserve(records.getSize());
            for (const auto& record : records)
                sorted.push_back(&record);
            std::sort(sorted.begin(), sorted.end(), [](const T* a, const T* b) { return a->mId < b->mId; });
            out << "count\t" << T::getRecordType() << '\t' << sorted.size() << '\n';
            for (const T* record : sorted)
            {
                out << "record\t" << T::getRecordType() << '\t';
                field(out, record->mId.getRefIdString());
                if constexpr (requires { record->mName; })
                {
                    out << "\tname=";
                    field(out, record->mName);
                }
                if constexpr (requires { record->mEffects; })
                    out << "\teffects=" << record->mEffects.mList.size();
                if constexpr (requires { record->mData.mWeight; record->mData.mValue; })
                    out << "\tweight=" << record->mData.mWeight << "\tvalue=" << record->mData.mValue;
                if constexpr (std::is_same_v<T, ESM::GameSetting>)
                {
                    const auto& value = record->mValue;
                    out << "\ttype=" << value.getType() << "\tvalue=";
                    if (value.getType() == ESM::VT_String)
                        field(out, value.getString());
                    else
                        value.write(out);
                }
                out << '\n';
            }
        }
    }

    LoadoutOptions readLoadoutOptions(int argc, const char* const argv[])
    {
        namespace bpo = boost::program_options;
        bpo::options_description description("Native TES3 loadout probe");
        Files::ConfigurationManager::addCommonOptions(description);
        description.add_options()("data",
            bpo::value<Files::MaybeQuotedPathContainer>()->default_value({}, "")->multitoken()->composing())(
            "data-local", bpo::value<Files::MaybeQuotedPath>()->default_value({}, ""))(
            "content", bpo::value<std::vector<std::string>>()->default_value({}, "")->multitoken()->composing())(
            "encoding", bpo::value<std::string>()->default_value("win1252"));
        bpo::variables_map variables;
        Files::parseArgs(argc, argv, variables, description);
        Files::ConfigurationManager config(true);
        config.processPaths(variables, std::filesystem::current_path());
        config.readConfiguration(variables, description, true);
        bpo::notify(variables);

        LoadoutOptions result;
        result.mConfigPaths = config.getActiveConfigPaths();
        result.mDataPaths = Files::asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>());
        const auto& local = variables["data-local"].as<Files::MaybeQuotedPath>();
        if (!local.empty())
            result.mDataPaths.push_back(local);
        config.filterOutNonExistingPaths(result.mDataPaths);
        result.mContent = variables["content"].as<std::vector<std::string>>();
        result.mEncoding = variables["encoding"].as<std::string>();
        return result;
    }

    Loadout::Loadout(LoadoutOptions options)
        : mOptions(std::move(options))
        , mEncoder(ToUTF8::calculateEncoding(mOptions.mEncoding))
    {
        // Diagnostic limits, not a claim that engine file parsing is a hardened
        // network-input boundary. Content remains local, operator-selected input.
        if (mOptions.mContent.empty() || mOptions.mContent.size() > 1024)
            throw std::runtime_error("Expected 1..1024 configured content files");
        mVersions.resize(mOptions.mContent.size(), -1);
        Files::Collections collections(mOptions.mDataPaths);
        MWWorld::EsmLoader loader(mStore, mReaders, &mEncoder, mVersions);
        for (std::size_t i = 0; i < mOptions.mContent.size(); ++i)
        {
            const auto& name = mOptions.mContent[i];
            const auto path = collections.getPath(name);
            const auto extension = Misc::StringUtils::lowerCase(Files::pathToUnicodeString(path.extension()));
            if (extension == ".omwscripts")
            {
                // Same store registration as OMWScriptsLoader; no Lua execution.
                mStore.addOMWScripts(path);
            }
            else
            {
                if (extension != ".esm" && extension != ".esp" && extension != ".omwgame"
                    && extension != ".omwaddon" && extension != ".project")
                    throw std::runtime_error("Unsupported content extension: " + extension);
                auto stream = Files::openBinaryInputFileStream(path);
                if (ESM::readFormat(*stream) != ESM::Format::Tes3)
                    throw std::runtime_error("Native loadout probe supports TES3 only: " + name);
                // Reuse the game's master resolution, ignored/deleted record
                // handling, and override loading. Never enter EsmLoader's TES4
                // branch, which requires Environment's ResourceSystem/VFS.
                int index = static_cast<int>(i);
                loader.load(path, index, nullptr);
            }
            mFiles.push_back(path);
        }
        mStore.setUp();
        mStore.validateRecords(mReaders);
        // World::ensureNeededRecords only supplies globals; movePlayerRecord
        // creates dynamic player state. Neither is part of these static stores.
    }

    void Loadout::enumerate(std::ostream& output) const
    {
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        output << "native-loadout\t1\nencoding\t" << mOptions.mEncoding << '\n';
        for (const auto& path : mOptions.mConfigPaths)
        {
            output << "config\t";
            field(output, Files::pathToUnicodeString(path / "openmw.cfg"));
            output << '\n';
        }
        for (const auto& path : mOptions.mDataPaths)
        {
            output << "data\t";
            field(output, Files::pathToUnicodeString(path));
            output << '\n';
        }
        for (std::size_t i = 0; i < mFiles.size(); ++i)
        {
            output << "content\t" << i << '\t';
            field(output, Files::pathToUnicodeString(mFiles[i]));
            output << "\tversion=" << mVersions[i] << '\n';
        }
        enumerateStore<ESM::Potion>(mStore, output);
        enumerateStore<ESM::Apparatus>(mStore, output);
        enumerateStore<ESM::Armor>(mStore, output);
        enumerateStore<ESM::Book>(mStore, output);
        enumerateStore<ESM::Clothing>(mStore, output);
        enumerateStore<ESM::Ingredient>(mStore, output);
        enumerateStore<ESM::Light>(mStore, output);
        enumerateStore<ESM::Lockpick>(mStore, output);
        enumerateStore<ESM::Miscellaneous>(mStore, output);
        enumerateStore<ESM::Probe>(mStore, output);
        enumerateStore<ESM::Repair>(mStore, output);
        enumerateStore<ESM::Weapon>(mStore, output);
        enumerateStore<ESM::Spell>(mStore, output);
        enumerateStore<ESM::Enchantment>(mStore, output);
        enumerateStore<ESM::GameSetting>(mStore, output);
        output << "complete\n";
        if (!output)
            throw std::runtime_error("Failed writing native loadout report");
    }

    void probe(int argc, const char* const argv[], std::ostream& output)
    {
        Loadout loadout(readLoadoutOptions(argc, argv));
        loadout.enumerate(output);
    }
}
