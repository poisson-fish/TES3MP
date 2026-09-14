#include "loadout.hpp"

#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/localscripts.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
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

    void checkInventory(
        const std::filesystem::path& root, int argc, const char* const argv[], const std::string& filter)
    {
        plugin(root / "local/Inventory.esm", true, [](ESM::ESMWriter& writer) {
            write(writer, record<ESM::Class>("native_class"));
            write(writer, record<ESM::Race>("native_race"));
            auto player = record<ESM::NPC>("player");
            player.mClass = ESM::RefId::stringRefId("native_class");
            player.mRace = ESM::RefId::stringRefId("native_race");
            write(writer, player);
            auto script = record<ESM::Script>("native_script");
            script.mScriptText
                = "begin native_script\nshort OnPCAdd\nlong counter\nfloat ratio\nif ( OnPCAdd == 1 )\n"
                  "set OnPCAdd to 0\nendif\nend native_script\n";
            write(writer, script);
            auto item = record<ESM::Miscellaneous>("native_plain");
            item.mData.mWeight = 2.5f;
            write(writer, item);
            item.mId = ESM::RefId::stringRefId("native_scripted");
            item.mScript = script.mId;
            write(writer, item);
            item = record<ESM::Miscellaneous>("gold_001");
            item.mData.mWeight = 0.25f;
            write(writer, item);
            write(writer, record<ESM::Miscellaneous>("gold_100"));
        });
        auto options = TES3MP::Native::readLoadoutOptions(argc, argv);
        options.mContent.push_back("Inventory.esm");
        TES3MP::Native::Loadout loadout(options);
        if (filter == "inventory-plain")
        {
            std::ostringstream before;
            loadout.enumerate(before);
            std::ostringstream output;
            loadout.writeInventoryProbe(output, "NATIVE_PLAIN");
            require(output.str() == "native-inventory\t2\nitem\t\"native_plain\"\ncount\t3\nstacks\t1\nweight\t7.5\n"
                                    "presentation-requests\t2\nregistered\t1\nderegistered\t1\n"
                                    "local-shorts\t0\nlocal-longs\t0\nlocal-floats\t0\nscripts-registered\t0\n"
                                    "scripts-removed\t0\nonpcadd-assigned\t0\n"
                                    "script-executed\t0\ncomplete\n",
                "native inventory diagnostic mismatch");
            std::ostringstream goldOutput;
            loadout.writeInventoryProbe(goldOutput, "gold_100");
            require(
                goldOutput.str().find("item\t\"gold_001\"\ncount\t3\nstacks\t1\nweight\t0.75\n") != std::string::npos,
                "native inventory did not use OpenMW gold normalization and canonical weight");
            std::ostringstream after;
            loadout.enumerate(after);
            require(before.str() == after.str(), "inventory probe changed retained base records");
            return;
        }
        if (filter == "inventory-scripted")
        {
            const auto scriptId = ESM::RefId::stringRefId("native_script");
            const auto* script = loadout.store().get<ESM::Script>().find(scriptId);
            require(script->mNumShorts == 0 && script->mNumLongs == 0 && script->mNumFloats == 0
                    && script->mVarNames.empty(),
                "fixture must have no precompiled local declarations");
            std::ostringstream output;
            loadout.writeInventoryProbe(output, "native_scripted");
            require(output.str() == "native-inventory\t2\nitem\t\"native_scripted\"\ncount\t3\nstacks\t2\nweight\t7.5\n"
                                    "presentation-requests\t2\nregistered\t2\nderegistered\t2\n"
                                    "local-shorts\t1\nlocal-longs\t1\nlocal-floats\t1\nscripts-registered\t2\n"
                                    "scripts-removed\t2\nonpcadd-assigned\t2\nscript-executed\t0\ncomplete\n",
                "scripted inventory diagnostic mismatch");

            plugin(root / "local/ScriptedGold.esm", true, [&](ESM::ESMWriter& writer) {
                auto gold = record<ESM::Miscellaneous>("gold_001");
                gold.mScript = scriptId;
                gold.mData.mWeight = 0.25f;
                write(writer, gold);
            });
            auto goldOptions = options;
            goldOptions.mContent.push_back("ScriptedGold.esm");
            TES3MP::Native::Loadout scriptedGold(goldOptions);
            std::ostringstream goldOutput;
            scriptedGold.writeInventoryProbe(goldOutput, "gold_100");
            require(
                goldOutput.str().find("item\t\"gold_001\"\ncount\t3\nstacks\t1\nweight\t0.75\n") != std::string::npos
                    && goldOutput.str().find("scripts-registered\t1\nscripts-removed\t1\nonpcadd-assigned\t1\n")
                        != std::string::npos,
                "normalized scripted gold did not retain engine stacking/locals/registration behavior");

            // WorldModel owns mutable registry/content services; copy only the
            // already engine-loaded bases needed by this isolated operation.
            MWWorld::ESMStore store;
            store.insertStatic(*script);
            store.insertStatic(*loadout.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("player")));
            store.insertStatic(
                *loadout.store().get<ESM::Miscellaneous>().find(ESM::RefId::stringRefId("native_scripted")));
            Compiler::Extensions extensions;
            Compiler::registerExtensions(extensions);
            MWScript::CompilerContext compilerContext(MWScript::CompilerContext::Type_Full);
            compilerContext.setExtensions(&extensions);
            MWScript::ScriptManager scripts(store, compilerContext, 1);
            ESM::ReadersCache readers;
            MWWorld::WorldModel worldModel(store, readers, 1);
            MWWorld::ManualRef player(store, ESM::RefId::stringRefId("player"));
            MWWorld::ManualRef owner(store, ESM::RefId::stringRefId("player"));
            ESM::Cell cell;
            cell.blank();
            MWWorld::CellStore ownerCell(MWWorld::Cell(cell), store, readers);
            auto ownerPtr = owner.getPtr();
            ownerPtr.mCell = &ownerCell;
            MWWorld::ManualRef source(store, ESM::RefId::stringRefId("native_scripted"));
            MWWorld::ContainerStore container;
            MWWorld::LocalScripts localScripts(store);
            int notifications = 0;
            MWWorld::ContainerStoreAddContext context{ store, worldModel, player.getPtr(), ownerPtr, &localScripts,
                &scripts, [&](const MWWorld::Ptr& ptr) {
                    require(ptr == ownerPtr, "wrong explicit inventory owner");
                    ++notifications;
                } };
            const auto first = container.add(source.getPtr(), 1, context);
            auto& locals = first->getRefData().getLocals();
            require(locals.mShorts == std::vector<Interpreter::Type_Short>{ 0 }
                    && locals.mLongs == std::vector<Interpreter::Type_Integer>{ 0 }
                    && locals.mFloats == std::vector<Interpreter::Type_Float>{ 0 }
                    && localScripts.isRunning(scriptId, *first) && notifications == 1,
                "non-player initialization incorrectly assigned OnPCAdd or lost registration");
            require(!locals.setVar(*script, "absent", 9, scripts) && locals.mShorts[0] == 0,
                "undeclared local assignment changed state");

            // Preserve OpenMW's unusual rule: new local instances inherit existing
            // global-script locals, including stopped globals, instead of zeroing.
            scripts.getGlobalScripts().addScript(scriptId, scripts);
            auto& global = scripts.getGlobalScripts().getScripts().at(scriptId)->mLocals;
            require(global.setVar(*script, "onpcadd", 7, scripts) && global.setVar(*script, "counter", 42, scripts)
                    && global.setVar(*script, "ratio", 1.25, scripts),
                "explicit global local initialization failed");
            scripts.getGlobalScripts().removeScript(scriptId);
            const auto inherited = container.add(source.getPtr(), 1, context);
            auto& inheritedLocals = inherited->getRefData().getLocals();
            require(inheritedLocals.mShorts[0] == 7 && inheritedLocals.mLongs[0] == 42
                    && inheritedLocals.mFloats[0] == 1.25f && locals.mShorts[0] == 0,
                "global-script locals were not copied into only the new instance");
            context.mPlayer = owner.getPtr();
            const auto playerItem = container.add(source.getPtr(), 1, context);
            require(playerItem->getRefData().getLocals().mShorts[0] == 1
                    && playerItem->getRefData().getLocals().mLongs[0] == 42 && global.mShorts[0] == 7,
                "explicit player OnPCAdd assignment did not preserve other/inherited locals");
            global.setVar(*script, "counter", 99, scripts);
            localScripts.add(scriptId, *inherited, scripts);
            require(inheritedLocals.mLongs[0] == 42, "repeat registration reset initialized locals");
            localScripts.startIteration();
            std::pair<ESM::RefId, MWWorld::Ptr> entry;
            int registered = 0;
            while (localScripts.getNext(entry))
                ++registered;
            require(registered == 3 && notifications == 3 && source.getPtr().getRefData().getLocals().isEmpty(),
                "duplicate registration or source mutation");
            for (const auto& item : container)
                localScripts.remove(item);
            require(!localScripts.isRunning(scriptId, *inherited), "script registration survived removal");
            return;
        }
        require(filter == "inventory-rejection", "unknown inventory filter");
        const auto scriptedId = ESM::RefId::stringRefId("native_scripted");
        require(!loadout.store().get<ESM::Miscellaneous>().find(scriptedId)->mScript.empty(),
            "OpenMW removed fixture script before probing");
        for (const bool oversizedText : { true, false })
        {
            plugin(root / "local/ScriptLimit.esm", true, [&](ESM::ESMWriter& writer) {
                auto script = record<ESM::Script>("native_script");
                if (oversizedText)
                    script.mScriptText.assign(64 * 1024 + 1, ' ');
                else
                {
                    script.mScriptText = "begin native_script\n";
                    for (int i = 0; i < 257; ++i)
                        script.mScriptText += "short var" + std::to_string(i) + "\n";
                    script.mScriptText += "end native_script\n";
                }
                write(writer, script);
            });
            auto invalidOptions = options;
            invalidOptions.mContent.push_back("ScriptLimit.esm");
            TES3MP::Native::Loadout invalid(invalidOptions);
            std::ostringstream output;
            output << "previous publication\n";
            bool rejected = false;
            try
            {
                invalid.writeInventoryProbe(output, "native_scripted");
            }
            catch (const std::runtime_error& error)
            {
                rejected = true;
                require(std::string_view(error.what()).find(oversizedText ? "64 KiB" : "256 locals")
                        != std::string_view::npos,
                    "unexpected script limit rejection");
            }
            require(rejected && output.str() == "previous publication\n", "script limit published a partial report");
        }

        // Exercise preflight against a nonempty engine store and observe every
        // possible effect. Records come from the OpenMW-written/loaded fixture.
        MWWorld::ESMStore store;
        for (const auto* id : { "native_plain", "native_scripted" })
            store.insertStatic(*loadout.store().get<ESM::Miscellaneous>().find(ESM::RefId::stringRefId(id)));
        store.insertStatic(*loadout.store().get<ESM::NPC>().find(ESM::RefId::stringRefId("player")));
        auto gold = record<ESM::Miscellaneous>("gold_001");
        gold.mScript = ESM::RefId::stringRefId("native_script");
        store.insertStatic(gold);
        store.insertStatic(record<ESM::Miscellaneous>("gold_100"));
        ESM::ReadersCache readers;
        MWWorld::WorldModel worldModel(store, readers, 1);
        MWWorld::ManualRef player(store, ESM::RefId::stringRefId("player"));
        MWWorld::ManualRef plain(store, ESM::RefId::stringRefId("native_plain"), 2);
        MWWorld::ManualRef scripted(store, scriptedId, 2);
        MWWorld::ManualRef goldPile(store, ESM::RefId::stringRefId("gold_100"), 2);
        plain.getPtr().getCellRef().setOwner(ESM::RefId::stringRefId("old_owner"));
        plain.getPtr().getCellRef().setFaction(ESM::RefId::stringRefId("old_faction"));
        plain.getPtr().getCellRef().setFactionRank(4);
        ESM::Position position{};
        position.pos[0] = 7;
        position.rot[1] = 2;
        plain.getPtr().getCellRef().setPosition(position);
        MWWorld::ContainerStore container;
        struct Listener final : MWWorld::ContainerStoreListener
        {
            int mAdded = 0;
            void itemAdded(const MWWorld::ConstPtr&, int) override { ++mAdded; }
        } listener;
        container.setContListener(&listener);
        int notifications = 0;
        MWWorld::ContainerStoreAddContext context{ store, worldModel, player.getPtr(), player.getPtr(), nullptr,
            nullptr, [&](const MWWorld::Ptr&) { ++notifications; } };
        const auto first = container.add(plain.getPtr(), 2, context);
        require(first->getCellRef().getOwner().empty() && first->getCellRef().getFaction().empty()
                && first->getCellRef().getFactionRank() == -2 && first->getCellRef().getPosition().pos[0] == 0
                && first->getCellRef().getPosition().rot[1] == 0
                && plain.getPtr().getCellRef().getOwner() == "old_owner"
                && plain.getPtr().getCellRef().getPosition().pos[0] == 7,
            "engine add did not scrub copied world metadata while preserving the source");
        const auto revision = worldModel.getPtrRegistryRevision();
        const auto reject = [&](const auto& operation, std::string_view reason) {
            bool rejected = false;
            try
            {
                operation();
            }
            catch (const std::exception& error)
            {
                rejected = true;
                require(std::string_view(error.what()).find(reason) != std::string_view::npos,
                    "unexpected inventory rejection reason");
            }
            require(rejected, "unsupported inventory operation accepted");
            require(first->getCellRef().getCount() == 2 && std::distance(container.begin(), container.end()) == 1
                    && container.getWeight() == 5 && notifications == 1 && listener.mAdded == 1
                    && worldModel.getPtrRegistryRevision() == revision && scripted.getPtr().getCellRef().getCount() == 2
                    && !scripted.getPtr().getCellRef().getRefNum().isSet(),
                "rejected add changed items, source, registry, listener or presentation");
        };
        reject([&] { container.add(scripted.getPtr(), 1, context); }, "OnPCAdd");
        reject([&] { container.add(goldPile.getPtr(), 1, context); }, "OnPCAdd");
        MWWorld::LocalScripts localScripts(store);
        context.mLocalScripts = &localScripts;
        reject([&] { container.add(scripted.getPtr(), 1, context); }, "ScriptManager");
        context.mLocalScripts = nullptr;
        MWScript::CompilerContext compilerContext(MWScript::CompilerContext::Type_Full);
        MWScript::ScriptManager scripts(store, compilerContext, 1);
        context.mScriptManager = &scripts;
        reject([&] { container.add(scripted.getPtr(), 1, context); }, "LocalScripts");
        MWWorld::InventoryStore equipment;
        reject([&] { static_cast<MWWorld::ContainerStore&>(equipment).add(plain.getPtr(), 1, context); },
            "InventoryStore");
        context.mInventoryUpdated = {};
        reject([&] { container.add(plain.getPtr(), 1, context); }, "presentation consumer");
    }

    void check(const std::filesystem::path& root, const std::string& filter)
    {
        fixture(root);
        const std::string directory = Files::pathToUnicodeString(root);
        const char* args[] = { "native-test", "--config", directory.c_str(), "--replace", "config" };
        if (filter.starts_with("inventory-"))
        {
            checkInventory(root, 5, args, filter);
            return;
        }
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
