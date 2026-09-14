#include "loadout.hpp"

#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include <components/esm/records.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/files/conversion.hpp>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template <class T>
    T record(std::string_view id)
    {
        T result;
        result.blank();
        result.mId = ESM::RefId::stringRefId(id);
        return result;
    }

    template <class T>
    void write(ESM::ESMWriter& writer, const T& value, bool deleted = false)
    {
        writer.startRecord(T::sRecordId, value.mRecordFlags);
        value.save(writer, deleted);
        writer.endRecord(T::sRecordId);
    }

    void plugin(const std::filesystem::path& path, bool master, const std::function<void(ESM::ESMWriter&)>& body)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        ToUTF8::Utf8Encoder encoder(ToUTF8::calculateEncoding("win1251"));
        ESM::ESMWriter writer;
        writer.setVersion();
        writer.setType(master ? 1 : 0);
        writer.setFormatVersion(ESM::DefaultFormatVersion);
        writer.setEncoder(&encoder);
        writer.setAuthor("TES3MP synthetic test");
        writer.setDescription("");
        writer.setRecordCount(0);
        if (!master)
            writer.addMaster("bAsE.eSm", 0);
        writer.save(stream);
        body(writer);
        writer.close();
    }

    void config(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream stream(path);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << text;
    }

    void fixture(const std::filesystem::path& root)
    {
        for (const char* dir : { "low", "high", "local", "extra", "userdata" })
            std::filesystem::create_directories(root / dir);
        config(root / "openmw.cfg",
            "replace=data\nreplace=content\ndata=low\ncontent=Base.esm\n"
            "config=extra\nuser-data=userdata\n");
        config(root / "extra/openmw.cfg",
            "data=../high\ndata-local=../local\nencoding=win1251\n"
            "content=empty.omwscripts\ncontent=Patch.esp\n");
        config(root / "local/empty.omwscripts", "# Synthetic; stored, never executed\n");
        plugin(root / "low/Base.esm", true, [](ESM::ESMWriter& writer) {
            write(writer, record<ESM::Class>("test_class"));
            write(writer, record<ESM::Race>("test_race"));
            auto item = record<ESM::Miscellaneous>("MiXeD_Item");
            item.mName = "base";
            item.mData.mValue = 10;
            write(writer, item);
            item.mId = ESM::RefId::stringRefId("ignored_override");
            write(writer, item);
            item.mId = ESM::RefId::stringRefId("deleted_item");
            write(writer, item);
            auto gmst = record<ESM::GameSetting>("fTestSetting");
            gmst.mValue = ESM::Variant(1.0f);
            write(writer, gmst);

            auto effect = record<ESM::MagicEffect>("RestoreHealth");
            write(writer, effect);
            auto spell = record<ESM::Spell>("normalized_spell");
            ESM::ENAMstruct valid{};
            valid.mEffectID = ESM::MagicEffect::RestoreHealth;
            valid.mSkill = ESM::Skill::Block;
            valid.mAttribute = ESM::Attribute::Strength;
            valid.mRange = 2;
            valid.mArea = 7;
            valid.mDuration = 13;
            valid.mMagnMin = 5;
            valid.mMagnMax = 9;
            ESM::ENAMstruct missing{};
            missing.mEffectID = ESM::MagicEffect::FireDamage;
            spell.mEffects.populate({ missing, valid });
            write(writer, spell);
            auto enchantment = record<ESM::Enchantment>("normalized_enchantment");
            enchantment.mEffects = spell.mEffects;
            write(writer, enchantment);
        });
        for (const auto& [directory, value] : { std::pair{ "low", 20 }, { "high", 30 }, { "local", 40 } })
        {
            plugin(root / directory / "Patch.esp", false, [value](ESM::ESMWriter& writer) {
                auto item = record<ESM::Miscellaneous>("mixed_ITEM");
                item.mName = "\xd0\x9c\xd0\xb5\xd1\x87"; // UTF-8, written in win1251 by OpenMW.
                item.mData.mValue = value;
                write(writer, item);
                item.mId = ESM::RefId::stringRefId("ignored_override");
                item.mRecordFlags = ESM::FLAG_Ignored;
                write(writer, item);
                item.mId = ESM::RefId::stringRefId("deleted_item");
                item.mRecordFlags = 0;
                write(writer, item, true);
                auto gmst = record<ESM::GameSetting>("FTESTSETTING");
                gmst.mValue = ESM::Variant(2.5f);
                write(writer, gmst);
            });
        }
    }

    void sampleFixture(const std::filesystem::path& root)
    {
        plugin(root / "local/Sample.esm", true, [](ESM::ESMWriter& writer) {
            const auto item = [&]<class T> {
                auto value = record<T>("sample_item");
                value.mName = "line\t\"quoted\"\\\n";
                write(writer, value);
            };
            item.operator()<ESM::Potion>();
            item.operator()<ESM::Apparatus>();
            item.operator()<ESM::Armor>();
            item.operator()<ESM::Book>();
            item.operator()<ESM::Clothing>();
            item.operator()<ESM::Ingredient>();
            item.operator()<ESM::Light>();
            item.operator()<ESM::Lockpick>();
            item.operator()<ESM::Probe>();
            item.operator()<ESM::Repair>();
            item.operator()<ESM::Weapon>();
            // More winners than the sample cap, deliberately in reverse ID order.
            for (int i = 5; i >= 0; --i)
                write(writer, record<ESM::Miscellaneous>("z_sample_" + std::to_string(i)));
            auto setting = record<ESM::GameSetting>("iTest");
            setting.mValue = ESM::Variant(std::int32_t(-7));
            setting.mValue.setType(ESM::VT_Int);
            write(writer, setting);
            setting.mId = ESM::RefId::stringRefId("sTest");
            setting.mValue = ESM::Variant(std::string("line\n\"quoted\"\\"));
            write(writer, setting);
            write(writer, record<ESM::GameSetting>("xEmpty"));
        });
        config(root / "extra/openmw.cfg",
            "data=../high\ndata-local=../local\nencoding=win1251\n"
            "content=empty.omwscripts\ncontent=Patch.esp\ncontent=Sample.esm\n");
    }

    const TES3MP::Native::DiagnosticRecord& findSample(
        const TES3MP::Native::DiagnosticSample& sample, std::string_view type, std::string_view id)
    {
        for (const auto& value : sample.mRecords)
            if (value.mType == type && value.mId == id)
                return value;
        throw std::runtime_error("Expected sampled record missing");
    }

    void rejectSample(const TES3MP::Native::Loadout& loadout, const TES3MP::Native::DiagnosticLimits& limits,
        std::string_view expected)
    {
        std::ostringstream output;
        output << "previous publication\n";
        bool failed = false;
        try
        {
            loadout.writeSample(output, limits);
        }
        catch (const std::runtime_error& error)
        {
            failed = true;
            require(std::string_view(error.what()).find(expected) != std::string_view::npos,
                "unexpected diagnostic rejection reason");
        }
        require(failed, "invalid sample accepted");
        require(output.str() == "previous publication\n", "rejected sample changed publication");
    }

    void checkSample(const std::filesystem::path& root, int argc, const char* const argv[], const std::string& filter)
    {
        using namespace TES3MP::Native;
        sampleFixture(root);
        const auto options = readLoadoutOptions(argc, argv);
        if (filter == "sample-owned")
        {
            DiagnosticSample owned;
            {
                Loadout loadout(options);
                owned = loadout.sample();
                require(owned == loadout.sample(), "sample order or values changed on repeat");
                require(
                    loadout.store().get<ESM::Miscellaneous>().getSize() == 8, "sampling truncated the engine store");
                require(loadout.store().get<ESM::Miscellaneous>().search(ESM::RefId::stringRefId("z_sample_5")),
                    "unsampled engine record unavailable");
            }
            // All projected strings, variants and effects remain usable after the
            // engine store/encoder/readers have been destroyed.
            require(owned.mRecords.size() == 21, "sample category/count mismatch");
            std::set<std::string> categories;
            std::vector<std::string> miscIds;
            for (const auto& value : owned.mRecords)
            {
                categories.insert(value.mType);
                if (value.mType == "Miscellaneous")
                    miscIds.push_back(value.mId);
            }
            require(categories.size() == 15, "sample omitted a supported record category");
            require(
                miscIds == std::vector<std::string>({ "ignored_override", "mixed_item", "z_sample_0", "z_sample_1" }),
                "sample did not select first winning IDs in engine order");
            const auto& item = findSample(owned, "Miscellaneous", "mixed_item");
            require(item.mValue == 40 && item.mName == "\xd0\x9c\xd0\xb5\xd1\x87",
                "owned override/encoding projection mismatch");
            require(
                findSample(owned, "Miscellaneous", "ignored_override").mValue == 10, "owned ignored override mismatch");
            for (const auto& [type, id] :
                { std::pair{ "Spell", "normalized_spell" }, std::pair{ "Enchantment", "normalized_enchantment" } })
            {
                const auto& effects = findSample(owned, type, id).mEffects;
                const DiagnosticEffect expected{ "restorehealth", "", "", 1, 2, 7, 13, 5, 9 };
                require(effects.size() == 1 && effects.front() == expected,
                    "owned effects did not preserve engine normalization");
            }
            require(std::get<float>(*findSample(owned, "GameSetting", "ftestsetting").mSetting) == 2.5f,
                "owned float GMST mismatch");
            require(std::get<std::int32_t>(*findSample(owned, "GameSetting", "itest").mSetting) == -7,
                "owned integer GMST mismatch");
            require(std::get<std::string>(*findSample(owned, "GameSetting", "stest").mSetting) == "line\n\"quoted\"\\",
                "owned string GMST mismatch");
            require(std::holds_alternative<std::monostate>(*findSample(owned, "GameSetting", "xempty").mSetting),
                "empty engine GMST was not preserved");
            require(owned.mReport.find("line\\x09\\\"quoted\\\"\\\\\\x0a") != std::string::npos,
                "sample TSV did not escape control characters and quotes");
            require(owned.mReport.ends_with("complete\n"), "staged sample completion missing");
            return;
        }
        if (filter == "sample-limits")
        {
            Loadout loadout(options);
            const auto expected = loadout.sample();
            DiagnosticLimits limits;
            limits.mMaxRecords = expected.mRecords.size();
            limits.mMaxBytes = expected.mReport.size();
            require(loadout.sample(limits) == expected, "exact record/report ceilings rejected");
            --limits.mMaxRecords;
            rejectSample(loadout, limits, "record count limit");
            limits.mMaxRecords = expected.mRecords.size();
            --limits.mMaxBytes;
            rejectSample(loadout, limits, "report byte limit");
            limits = {};
            limits.mMaxStringBytes = 22; // normalized_enchantment is the longest field.
            require(loadout.sample(limits) == expected, "exact string ceiling rejected");
            --limits.mMaxStringBytes;
            rejectSample(loadout, limits, "string byte limit");
            limits = {};
            limits.mMaxEffectsPerRecord = 1;
            require(loadout.sample(limits) == expected, "exact effect ceiling rejected");
            limits.mMaxEffectsPerRecord = 0;
            rejectSample(loadout, limits, "effect count limit");
            limits = {};
            limits.mMaxBytes = 100;
            rejectSample(loadout, limits, "aggregate string byte limit");
            for (auto member : { &DiagnosticLimits::mRecordsPerType, &DiagnosticLimits::mMaxRecords,
                     &DiagnosticLimits::mMaxStringBytes, &DiagnosticLimits::mMaxEffectsPerRecord,
                     &DiagnosticLimits::mMaxBytes })
            {
                limits = {};
                limits.*member = std::numeric_limits<std::size_t>::max();
                rejectSample(loadout, limits, "Invalid diagnostic sample limits");
            }
            limits = {};
            limits.mRecordsPerType = 0;
            rejectSample(loadout, limits, "Invalid diagnostic sample limits");
            limits.mRecordsPerType = 1;
            require(loadout.sample(limits).mRecords.size() == 15, "reduced sampling limit not applied");
            require(loadout.sample() == expected, "rejection changed the loaded engine records");
            return;
        }
        require(filter == "sample-malformed", "unknown sample filter");
        for (const std::string problem : { "weight", "setting", "range", "string", "effects" })
        {
            plugin(root / "local/Invalid.esm", true, [&](ESM::ESMWriter& writer) {
                if (problem == "weight" || problem == "string")
                {
                    auto value = record<ESM::Miscellaneous>("mixed_item");
                    if (problem == "weight")
                        value.mData.mWeight = std::numeric_limits<float>::quiet_NaN();
                    else
                        value.mName.assign(4097, 'x');
                    write(writer, value);
                }
                else if (problem == "setting")
                {
                    auto value = record<ESM::GameSetting>("fTestSetting");
                    value.mValue = ESM::Variant(std::numeric_limits<float>::infinity());
                    write(writer, value);
                }
                else
                {
                    auto value = record<ESM::Spell>("normalized_spell");
                    ESM::ENAMstruct effect{};
                    effect.mEffectID = ESM::MagicEffect::RestoreHealth;
                    effect.mRange = problem == "range" ? 3 : 0;
                    value.mEffects.populate(std::vector<ESM::ENAMstruct>(problem == "effects" ? 33 : 1, effect));
                    write(writer, value);
                }
            });
            auto invalid = options;
            invalid.mContent.push_back("Invalid.esm");
            Loadout loadout(std::move(invalid)); // Malformed projected fields survive engine loading.
            std::ostringstream before;
            loadout.enumerate(before);
            const auto reason = problem == "string" ? "string byte limit"
                : problem == "effects"              ? "effect count limit"
                : problem == "range"                ? "invalid effect range"
                                                    : "non-finite numeric field";
            rejectSample(loadout, {}, reason);
            std::ostringstream after;
            loadout.enumerate(after);
            require(before.str() == after.str(), "projection rejection mutated engine data");
        }
    }

    void check(const std::filesystem::path& root, const std::string& filter)
    {
        fixture(root);
        const std::string directory = Files::pathToUnicodeString(root);
        const char* args[] = { "native-test", "--config", directory.c_str(), "--replace", "config" };
        if (filter.starts_with("sample-"))
        {
            checkSample(root, 5, args, filter);
            return;
        }
        if (filter == "layered")
        {
            const auto options = TES3MP::Native::readLoadoutOptions(5, args);
            require(options.mEncoding == "win1251", "nested config encoding not applied");
            require(options.mContent == std::vector<std::string>({ "Base.esm", "empty.omwscripts", "Patch.esp" }),
                "OpenMW composing content order changed");
            TES3MP::Native::Loadout loadout(options);
            const auto& store = loadout.store();
            const auto& items = store.get<ESM::Miscellaneous>();
            require(items.getSize() == 2, "winning/deleted/ignored item count mismatch");
            const auto* item = items.find(ESM::RefId::stringRefId("mixed_item"));
            require(item->mData.mValue == 40, "data-local or override priority mismatch");
            require(item->mName == "\xd0\x9c\xd0\xb5\xd1\x87", "engine encoding mismatch");
            require(items.find(ESM::RefId::stringRefId("ignored_override"))->mData.mValue == 10,
                "ignored record replaced its predecessor");
            require(items.search(ESM::RefId::stringRefId("deleted_item")) == nullptr, "deleted record survived");
            require(store.get<ESM::GameSetting>().find("ftestsetting")->mValue.getFloat() == 2.5f,
                "case-insensitive GMST override mismatch");
            for (const auto* effects :
                { &store.get<ESM::Spell>().find(ESM::RefId::stringRefId("normalized_spell"))->mEffects,
                    &store.get<ESM::Enchantment>().find(ESM::RefId::stringRefId("normalized_enchantment"))->mEffects })
            {
                require(effects->mList.size() == 1, "engine did not remove missing magic effect");
                require(effects->mList.front().mData.mSkill.empty() && effects->mList.front().mData.mAttribute.empty(),
                    "engine did not normalize effect arguments");
            }
            std::ostringstream output;
            loadout.enumerate(output);
            require(
                output.str().find("record\tSpell\t\"normalized_spell\"\tname=\"\"\teffects=1\n") != std::string::npos,
                "report did not enumerate normalized spell");
            require(output.str().ends_with("complete\n"), "report completion marker missing");
            return;
        }

        std::string expected;
        if (filter == "missing-content")
        {
            config(root / "extra/openmw.cfg", "replace=content\ncontent=missing.esm\n");
            expected = "not found";
        }
        else if (filter == "master-order")
        {
            config(root / "extra/openmw.cfg", "replace=content\ncontent=Patch.esp\ncontent=Base.esm\n");
            expected = "not available or has been loaded in the wrong order";
        }
        else if (filter == "truncated")
        {
            const auto file = root / "local/Patch.esp";
            std::filesystem::resize_file(file, std::filesystem::file_size(file) - 2);
            expected = ""; // Engine diagnostic text is intentionally not duplicated here.
        }
        else if (filter == "tes4")
        {
            config(root / "local/Patch.esp", "TES4");
            expected = "supports TES3 only";
        }
        else
            throw std::runtime_error("Unknown filter: " + filter);

        std::ostringstream output;
        bool failed = false;
        try
        {
            TES3MP::Native::probe(5, args, output);
        }
        catch (const std::exception& error)
        {
            failed = true;
            require(std::string(error.what()).find(expected) != std::string::npos, "unexpected load failure reason");
        }
        require(failed, "invalid loadout accepted");
        require(output.str().empty(), "failed load published a partial record report");
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 3, "Usage: tes3mp_native_loadout_tests FILTER SCRATCH_DIRECTORY");
        check(std::filesystem::absolute(std::filesystem::u8path(argv[2])), argv[1]);
        std::cout << "PASS " << argv[1] << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
