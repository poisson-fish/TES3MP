#include "loadout.hpp"
#include <components/fallback/validate.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <type_traits>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <apps/openmw/mwworld/esmloader.hpp>
#include <components/esm/format.hpp>
#include <components/esm/records.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/files/openfile.hpp>
#include <components/files/hash.hpp>
#include <sstream>
#include <components/misc/strings/lower.hpp>

namespace TES3MP::Native
{
    namespace
    {
        template <class Visitor>
        void visitRecordTypes(Visitor&& visitor)
        {
            visitor.template operator()<ESM::Potion>();
            visitor.template operator()<ESM::Apparatus>();
            visitor.template operator()<ESM::Armor>();
            visitor.template operator()<ESM::Book>();
            visitor.template operator()<ESM::Clothing>();
            visitor.template operator()<ESM::Ingredient>();
            visitor.template operator()<ESM::Light>();
            visitor.template operator()<ESM::Lockpick>();
            visitor.template operator()<ESM::Miscellaneous>();
            visitor.template operator()<ESM::Probe>();
            visitor.template operator()<ESM::Repair>();
            visitor.template operator()<ESM::Weapon>();
            visitor.template operator()<ESM::Spell>();
            visitor.template operator()<ESM::Enchantment>();
            visitor.template operator()<ESM::GameSetting>();
        }

        void validateLimits(const DiagnosticLimits& limits)
        {
            constexpr DiagnosticLimits ceiling;
            if (limits.mRecordsPerType == 0 || limits.mRecordsPerType > ceiling.mRecordsPerType
                || limits.mMaxRecords > ceiling.mMaxRecords || limits.mMaxStringBytes > ceiling.mMaxStringBytes
                || limits.mMaxEffectsPerRecord > ceiling.mMaxEffectsPerRecord || limits.mMaxBytes > ceiling.mMaxBytes)
                throw std::runtime_error("Invalid diagnostic sample limits");
        }

        class SampleStrings
        {
        public:
            explicit SampleStrings(const DiagnosticLimits& limits)
                : mLimits(limits)
            {
            }

            std::string copy(std::string_view value)
            {
                if (value.size() > mLimits.mMaxStringBytes)
                    throw std::runtime_error("Diagnostic string byte limit exceeded");
                if (value.size() > mLimits.mMaxBytes - mBytes)
                    throw std::runtime_error("Diagnostic aggregate string byte limit exceeded");
                mBytes += value.size();
                return std::string(value);
            }

            std::string id(const ESM::RefId& value, bool optional = false)
            {
                if (optional && value.empty())
                    return {};
                if (!value.is<ESM::StringRefId>() || value.getRefIdString().empty())
                    throw std::runtime_error("Diagnostic expected a nonempty TES3 string ID");
                auto result = copy(value.getRefIdString());
                // RefId interns the first spelling seen process-wide. Use the
                // engine's ASCII fold so diagnostic identity is independent of it.
                Misc::StringUtils::lowerCaseInPlace(result);
                return result;
            }

        private:
            const DiagnosticLimits& mLimits;
            std::size_t mBytes = 0;
        };

        float finite(float value)
        {
            if (!std::isfinite(value))
                throw std::runtime_error("Diagnostic non-finite numeric field");
            return value;
        }

        template <class T>
        DiagnosticRecord project(const T& record, SampleStrings& strings, const DiagnosticLimits& limits)
        {
            DiagnosticRecord result;
            result.mType = strings.copy(T::getRecordType());
            result.mId = strings.id(record.mId);
            if constexpr (requires { record.mName; })
                result.mName = strings.copy(record.mName);
            if constexpr (requires {
                              record.mData.mWeight;
                              record.mData.mValue;
                          })
            {
                result.mWeight = finite(record.mData.mWeight);
                result.mValue = record.mData.mValue;
            }
            if constexpr (requires { record.mEffects; })
            {
                if (record.mEffects.mList.size() > limits.mMaxEffectsPerRecord)
                    throw std::runtime_error("Diagnostic effect count limit exceeded");
                result.mEffects.reserve(record.mEffects.mList.size());
                for (const auto& effect : record.mEffects.mList)
                {
                    const auto& data = effect.mData;
                    if (data.mRange < 0 || data.mRange > 2)
                        throw std::runtime_error("Diagnostic invalid effect range");
                    result.mEffects.push_back(
                        { strings.id(data.mEffectID), strings.id(data.mSkill, true), strings.id(data.mAttribute, true),
                            effect.mIndex, data.mRange, data.mArea, data.mDuration, data.mMagnMin, data.mMagnMax });
                }
            }
            if constexpr (std::is_same_v<T, ESM::GameSetting>)
            {
                result.mSetting.emplace();
                switch (record.mValue.getType())
                {
                    case ESM::VT_None:
                        break;
                    case ESM::VT_Int:
                    case ESM::VT_Long:
                    case ESM::VT_Short:
                        *result.mSetting = record.mValue.getInteger();
                        break;
                    case ESM::VT_Float:
                        *result.mSetting = finite(record.mValue.getFloat());
                        break;
                    case ESM::VT_String:
                        *result.mSetting = strings.copy(record.mValue.getString());
                        break;
                    default:
                        throw std::runtime_error("Diagnostic unsupported GMST value type");
                }
            }
            return result;
        }

        // No unbounded report stream followed by a post-allocation length check.
        // This caps the escaped representation as it grows, including metadata.
        class SampleBuffer final : public std::streambuf
        {
        public:
            explicit SampleBuffer(std::size_t limit)
                : mLimit(limit)
            {
            }
            std::string take() { return std::move(mText); }

        private:
            std::streamsize xsputn(const char* text, std::streamsize size) override
            {
                if (size < 0 || static_cast<std::size_t>(size) > mLimit - mText.size())
                    throw std::runtime_error("Diagnostic report byte limit exceeded");
                mText.append(text, static_cast<std::size_t>(size));
                return size;
            }

            int_type overflow(int_type ch) override
            {
                if (traits_type::eq_int_type(ch, traits_type::eof()))
                    return traits_type::not_eof(ch);
                const char value = traits_type::to_char_type(ch);
                xsputn(&value, 1);
                return ch;
            }

            std::size_t mLimit;
            std::string mText;
        };

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
                // The engine retains the first interned spelling process-wide.
                // Export the same identity as sample(), without changing records.
                field(out, Misc::StringUtils::lowerCase(record->mId.getRefIdString()));
                if constexpr (requires { record->mName; })
                {
                    out << "\tname=";
                    field(out, record->mName);
                }
                if constexpr (requires { record->mEffects; })
                    out << "\teffects=" << record->mEffects.mList.size();
                if constexpr (requires {
                                  record->mData.mWeight;
                                  record->mData.mValue;
                              })
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
        description.add_options()(
            "data", bpo::value<Files::MaybeQuotedPathContainer>()->default_value({}, "")->multitoken()->composing())(
            "data-local", bpo::value<Files::MaybeQuotedPath>()->default_value({}, ""))(
            "content", bpo::value<std::vector<std::string>>()->default_value({}, "")->multitoken()->composing())(
            "fallback-archive", bpo::value<std::vector<std::string>>()->default_value({}, "")->multitoken()->composing())(
            "fallback", bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing())(
            "encoding", bpo::value<std::string>()->default_value("win1252"))(
            "sample", bpo::bool_switch(), "Stage a bounded owned diagnostic sample")(
            "containers", bpo::value<std::string>(), "List placed containers in one interior using OpenMW")(
            "inventory", bpo::value<std::string>(), "Probe adding a MISC item to an engine ContainerStore")(
            "enchantment", bpo::value<std::string>(), "Probe an enchantment's engine cast cost and charge")(
            "equipment", bpo::value<std::string>(), "Run durable shirt equipment and fresh restart")(
            "equipment-actors", bpo::value<std::vector<std::string>>()->multitoken(), "Two trusted NPC base IDs")(
            "equipment-container", bpo::value<std::string>(), "Shared empty diagnostic container base ID")(
            "equipment-save-dir", bpo::value<std::string>(), "New private directory for equipment saves");
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
        result.mArchives = variables["fallback-archive"].as<std::vector<std::string>>();
        result.mFallbacks = variables["fallback"].as<Fallback::FallbackMap>().mMap;
        result.mSample = variables["sample"].as<bool>();
        if (variables.count("containers")) result.mContainerCell = variables["containers"].as<std::string>();
        if (variables.count("inventory"))
        {
            result.mInventoryItem = variables["inventory"].as<std::string>();
            if (result.mSample || result.mInventoryItem.empty())
                throw std::runtime_error("--inventory requires an item ID and cannot be combined with --sample");
        }
        if (variables.count("enchantment"))
        {
            result.mEnchantment = variables["enchantment"].as<std::string>();
            if (result.mSample || !result.mInventoryItem.empty() || result.mEnchantment.empty())
                throw std::runtime_error(
                    "--enchantment requires an ID and cannot be combined with --sample/--inventory");
        }
        if (variables.count("equipment"))
        {
            result.mEquipment = variables["equipment"].as<std::string>();
            if (result.mSample || !result.mInventoryItem.empty() || !result.mEnchantment.empty()
                || result.mEquipment.empty() || !variables.count("equipment-actors")
                || !variables.count("equipment-save-dir"))
                throw std::runtime_error("--equipment requires --equipment-actors A B and --equipment-save-dir, alone");
            result.mEquipmentActors = variables["equipment-actors"].as<std::vector<std::string>>();
            result.mEquipmentSaveDirectory = variables["equipment-save-dir"].as<std::string>();
            if (variables.count("equipment-container"))
                result.mEquipmentContainer = variables["equipment-container"].as<std::string>();
            if (result.mEquipmentActors.size() != 2 || result.mEquipmentSaveDirectory.empty())
                throw std::runtime_error("Equipment requires exactly two NPC bases and a save directory");
        }
        else if (variables.count("equipment-actors") || variables.count("equipment-save-dir") || variables.count("equipment-container"))
            throw std::runtime_error("Equipment bindings require --equipment");
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
                if (extension != ".esm" && extension != ".esp" && extension != ".omwgame" && extension != ".omwaddon"
                    && extension != ".project")
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

    std::string Loadout::contentFingerprint() const
    {
        // Same engine file fingerprints as the equipment probe, ordered by the
        // actual resolved loadout. No ESM reinterpretation or second catalog.
        std::ostringstream result;
        result << mOptions.mEncoding << '\n';
        for (size_t i = 0; i < mFiles.size(); ++i)
        {
            auto stream = Files::openBinaryInputFileStream(mFiles[i]);
            const auto hash = Files::getHash(mOptions.mContent[i], *stream);
            result << mOptions.mContent[i].size() << ':' << mOptions.mContent[i] << '\n'
                << hash[0] << ':' << hash[1] << '\n';
        }
        return result.str();
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
        visitRecordTypes([&]<class T> { enumerateStore<T>(mStore, output); });
        output << "complete\n";
        if (!output)
            throw std::runtime_error("Failed writing native loadout report");
    }

    DiagnosticSample Loadout::sample(const DiagnosticLimits& limits) const
    {
        validateLimits(limits);
        DiagnosticSample result;
        SampleStrings strings(limits);
        result.mRecords.reserve(limits.mMaxRecords);
        visitRecordTypes([&]<class T> {
            // A bounded heap selects the first IDs without copying/sorting the
            // whole store. Neither selection nor projection mutates engine data.
            const auto& records = mStore.get<T>();
            std::vector<const T*> selected;
            selected.reserve(std::min(records.getSize(), limits.mRecordsPerType));
            const auto less = [](const T* a, const T* b) { return a->mId < b->mId; };
            for (const auto& record : records)
            {
                if (selected.size() == limits.mRecordsPerType)
                {
                    if (!less(&record, selected.front()))
                        continue;
                    std::pop_heap(selected.begin(), selected.end(), less);
                    selected.pop_back();
                }
                selected.push_back(&record);
                std::push_heap(selected.begin(), selected.end(), less);
            }
            if (selected.size() > limits.mMaxRecords - result.mRecords.size())
                throw std::runtime_error("Diagnostic record count limit exceeded");
            std::sort_heap(selected.begin(), selected.end(), less);
            for (const T* record : selected)
                result.mRecords.push_back(project(*record, strings, limits));
        });

        SampleBuffer buffer(limits.mMaxBytes);
        std::ostream output(&buffer);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        output << "native-loadout-sample\t1\nfirst-ids-per-type\t" << limits.mRecordsPerType << "\ncount\t"
               << result.mRecords.size() << '\n';
        for (const auto& record : result.mRecords)
        {
            output << "record\t" << record.mType << '\t';
            field(output, record.mId);
            if (record.mName)
            {
                output << "\tname=";
                field(output, *record.mName);
            }
            if (record.mWeight)
                output << "\tweight=" << *record.mWeight;
            if (record.mValue)
                output << "\tvalue=" << *record.mValue;
            output << "\teffects=" << record.mEffects.size();
            if (record.mSetting)
                std::visit(
                    [&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, std::monostate>)
                            output << "\tsetting=none";
                        else if constexpr (std::is_same_v<T, std::string>)
                        {
                            output << "\tsetting=string\tvalue=";
                            field(output, value);
                        }
                        else
                            output << "\tsetting=" << (std::is_same_v<T, float> ? "float" : "integer")
                                   << "\tvalue=" << value;
                    },
                    *record.mSetting);
            output << '\n';
            for (const auto& effect : record.mEffects)
            {
                output << "effect\t" << effect.mIndex << '\t';
                field(output, effect.mId);
                output << "\tskill=";
                field(output, effect.mSkill);
                output << "\tattribute=";
                field(output, effect.mAttribute);
                output << "\trange=" << effect.mRange << "\tarea=" << effect.mArea << "\tduration=" << effect.mDuration
                       << "\tmin=" << effect.mMagnitudeMin << "\tmax=" << effect.mMagnitudeMax << '\n';
            }
        }
        output << "complete\n";
        result.mReport = buffer.take();
        return result;
    }

    void Loadout::writeSample(std::ostream& output, const DiagnosticLimits& limits) const
    {
        const auto prepared = sample(limits);
        output.write(prepared.mReport.data(), static_cast<std::streamsize>(prepared.mReport.size()));
        if (!output)
            throw std::runtime_error("Failed writing native loadout sample");
    }

    void probe(int argc, const char* const argv[], std::ostream& output)
    {
        auto options = readLoadoutOptions(argc, argv);
        const bool sample = options.mSample;
        const std::string inventoryItem = options.mInventoryItem;
        const std::string enchantment = options.mEnchantment;
        const bool equipment = !options.mEquipment.empty();
        const std::string containerCell = options.mContainerCell;
        Loadout loadout(std::move(options));
        if (!containerCell.empty())
            loadout.writeContainers(output, containerCell);
        else if (equipment)
            loadout.writeEquipmentProbe(output);
        else if (!enchantment.empty())
            loadout.writeEnchantmentProbe(output, enchantment);
        else if (!inventoryItem.empty())
            loadout.writeInventoryProbe(output, inventoryItem);
        else if (sample)
            loadout.writeSample(output);
        else
            loadout.enumerate(output);
    }
}
