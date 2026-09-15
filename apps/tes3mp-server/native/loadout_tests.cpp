#include "loadout.hpp"

#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include <osg/observer_ptr>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
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
#include <components/esm3/loadcont.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/files/conversion.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void bindEmptyStore(MWWorld::ContainerStore& container, const MWWorld::Ptr& owner, MWWorld::WorldModel& worldModel)
    {
        worldModel.registerPtr(owner);
        container.setPtr(owner, worldModel);
        Misc::Rng::Generator prng{ 0 };
        container.fill({}, ESM::RefId(), prng);
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

    void checkInventoryPreparation(MWWorld::ContainerStore& a, MWWorld::ContainerStore& b,
        const MWWorld::ContainerStoreAddContext& addA, const MWWorld::ContainerStoreAddContext& addB,
        MWWorld::LocalScripts& localScripts, const std::function<std::pair<size_t, size_t>()>& notificationCounts)
    {
        const auto& store = addA.mStore;
        auto& worldModel = addA.mWorldModel;
        const auto scriptId = ESM::RefId::stringRefId("native_script");
        Compiler::Extensions extensions;
        Compiler::registerExtensions(extensions);
        MWScript::CompilerContext compilerContext(MWScript::CompilerContext::Type_Full);
        compilerContext.setExtensions(&extensions);
        struct ObservedScripts final : MWScript::ScriptManager
        {
            using MWScript::ScriptManager::ScriptManager;
            std::function<void()> mBeforeLocals;
            int mRuns = 0;
            const Compiler::Locals& getLocals(const ESM::RefId& id) override
            {
                if (mBeforeLocals)
                    mBeforeLocals();
                return MWScript::ScriptManager::getLocals(id);
            }
            bool run(const ESM::RefId&, Interpreter::Context&) override
            {
                ++mRuns;
                throw std::runtime_error("preparation must not execute scripts");
            }
        } scripts(store, compilerContext, 1);
        auto scriptedAddA = addA;
        auto scriptedAddB = addB;
        for (auto* context : { &scriptedAddA, &scriptedAddB })
        {
            context->mLocalScripts = &localScripts;
            context->mScriptManager = &scripts;
        }
        // Exercise a stock nonplayer registration with a real owner cell as well
        // as the player registration whose null cell survives owner-cell unload.
        scriptedAddB.mPlayer = addA.mPlayer;
        MWWorld::ManualRef plain(store, ESM::RefId::stringRefId("native_plain"));
        MWWorld::ManualRef scripted(store, ESM::RefId::stringRefId("native_scripted"));
        const auto plainA = a.add(plain.getPtr(), 4, addA);
        const auto plainB = b.add(plain.getPtr(), 7, addB);
        const auto scriptA = a.add(scripted.getPtr(), 3, scriptedAddA);
        const auto scriptB = b.add(scripted.getPtr(), 2, scriptedAddB);
        require(scriptA->getRefData().getLocals().mShorts.at(0) == 1
                && scriptB->getRefData().getLocals().mShorts.at(0) == 0,
            "shared stock add lost player/nonplayer OnPCAdd behavior");
        plainA->getCellRef().setCount(-4); // Preparation must preserve the live restocking count.
        a.setSelectedEnchantItem(scriptA);
        b.setSelectedEnchantItem(scriptB);
        const std::vector<MWWorld::Ptr> live{ *plainA, *plainB, *scriptA, *scriptB };
        for (size_t i = 0; i < live.size(); ++i)
        {
            auto& ref = live[i].getCellRef();
            ref.setSoul(ESM::RefId::stringRefId("test_soul_" + std::to_string(i)));
            ref.setCharge(51 + static_cast<int>(i));
            ref.setChargeIntRemainder(0.25f);
            ref.setEnchantmentCharge(12.5f + i);
            ref.setOwner(ESM::RefId::stringRefId("test_owner"));
            ref.setFaction(ESM::RefId::stringRefId("test_faction"));
            ref.setFactionRank(4);
            ref.setScale(0.75f);
            ESM::Position position{ { 1.f + static_cast<float>(i), 2.f, 3.f }, { .1f, .2f, .3f } };
            ref.setPosition(position);
            auto& data = live[i].getRefData();
            position.pos[0] += 20;
            data.setPosition(position);
            data.disable();
            data.mPhysicsPostponed = true;
            data.getAnimationState().mScriptedAnims.emplace_back();
            auto& animation = data.getAnimationState().mScriptedAnims.back();
            animation.mGroup = "owned animation group with heap storage";
            animation.mTime = 3.5f;
            animation.mAbsolute = true;
            animation.mLoopCount = 7;
            // The source may carry a scene node; preparation must never share it.
            data.setBaseNode(new SceneUtil::PositionAttitudeTransform);
            data.onActivate();
            require(!data.activate(), "fixture did not buffer activation");
        }
        for (auto item : { *scriptA, *scriptB })
        {
            auto& locals = item.getRefData().getLocals();
            require(locals.mShorts.size() == 1 && locals.mLongs.size() == 1 && locals.mFloats.size() == 1,
                "preparation fixture script locals were not initialized");
            locals.mShorts[0] = item == *scriptA ? 0 : 7;
            locals.mLongs[0] = item == *scriptA ? 123 : 456;
            locals.mFloats[0] = item == *scriptA ? 1.25f : 2.5f;
        }
        const auto values = [](const MWWorld::ConstPtr& item) {
            const auto& ref = item.getCellRef();
            const auto& data = item.getRefData();
            const auto& locals = data.getLocals();
            std::vector<std::tuple<std::string, float, bool, uint64_t>> animations;
            for (const auto& animation : data.getAnimationState().mScriptedAnims)
                animations.emplace_back(animation.mGroup, animation.mTime, animation.mAbsolute, animation.mLoopCount);
            return std::tuple{ ref.getRefId(), ref.getSoul(), ref.getCharge(), ref.getChargeIntRemainder(),
                ref.getEnchantmentCharge(), ref.getOwner(), ref.getGlobalVariable(), ref.getFaction(),
                ref.getFactionRank(), ref.getScale(), ref.getPosition(), ref.hasChanged(), data.getPosition(),
                data.isEnabled(), data.isDeletedByContentFile(), data.mPhysicsPostponed, data.hasChanged(),
                locals.getScriptId(), locals.mShorts, locals.mLongs, locals.mFloats, animations };
        };
        const auto refDataState = [](const MWWorld::RefData& data) {
            auto detached = data.copyForContainerTransfer();
            // Serializing initialized locals needs Environment. They are already
            // compared separately; read flags through stock write on an owned copy.
            detached.getLocals() = {};
            ESM::ObjectState state;
            detached.write(state);
            return state;
        };
        const auto flags = [&](const MWWorld::RefData& data) { return refDataState(data).mFlags; };
        const auto setFlags = [&](MWWorld::RefData& data, unsigned int bits) {
            auto state = refDataState(data);
            state.mFlags = bits;
            MWWorld::RefData changed(state, data.isDeletedByContentFile());
            // Save loading clears suppression. Restore runtime-reachable states
            // through activation calls; OnActivate-only states come from saves.
            if (bits & 1)
            {
                changed.onActivate();
                if (bits & 2)
                    require(!changed.activate(), "flag fixture did not suppress activation");
            }
            changed.getLocals() = data.getLocals();
            changed.mPhysicsPostponed = data.mPhysicsPostponed;
            changed.setBaseNode(data.getBaseNode());
            data = std::move(changed);
            require(flags(data) == bits, "flag fixture does not match requested activation state");
        };
        // All runtime/save-load reachable combinations, plus unrelated high bits
        // which stock copying must retain. Destination states vary independently.
        const std::vector<std::pair<unsigned int, unsigned int>> activationCases{ { 0, 7 }, { 1, 6 }, { 2, 5 },
            { 4, 4 }, { 5, 2 }, { 6, 1 }, { 7, 0 }, { 15, 8 } };
        const auto itemSnapshot = [&](const MWWorld::Ptr& item) {
            const auto& data = item.getRefData();
            const auto& locals = data.getLocals();
            return std::tuple{ values(item), item.mRef, item.mCell, item.mContainerStore, item.mRef->mWorldModel,
                item.getType() == ESM::Miscellaneous::sRecordId ? item.get<ESM::Miscellaneous>()->mBase : nullptr,
                item.getCellRef().getRefNum(), item.getCellRef().getCount(false), data.getBaseNode(),
                data.getBaseNode() ? data.getBaseNode()->referenceCount() : 0, data.getLuaScripts(),
                data.getCustomData(),
                data.getLuaScripts() || data.getCustomData() ? std::optional<unsigned int>() : flags(data),
                locals.mShorts.data(), locals.mLongs.data(), locals.mFloats.data(),
                data.getAnimationState().mScriptedAnims.data() };
        };
        std::vector<MWWorld::ContainerStore*> observedStores{ &a, &b };
        std::vector<MWWorld::Ptr> observedDormant;
        const auto snapshot = [&] {
            using Items = std::vector<decltype(itemSnapshot(live.front()))>;
            std::vector<std::tuple<Items, float, MWWorld::Ptr, bool, MWWorld::ContainerStoreIterator,
                MWWorld::ContainerStoreListener*>>
                inventories;
            for (auto* container : observedStores)
            {
                Items items;
                for (auto item : *container)
                    items.push_back(itemSnapshot(item));
                inventories.emplace_back(std::move(items), container->getWeight(), container->getPtr(worldModel),
                    container->isResolved(), container->getSelectedEnchantItem(), container->getContListener());
            }
            std::map<ESM::RefNum, std::tuple<MWWorld::LiveCellRefBase*, MWWorld::CellStore*, MWWorld::ContainerStore*>>
                registry;
            for (const auto& [id, ptr] : worldModel.getPtrRegistryView())
                registry.emplace(id, std::tuple{ ptr.mRef, ptr.mCell, ptr.mContainerStore });
            Items dormant;
            for (const auto& item : observedDormant)
                dormant.push_back(itemSnapshot(item));
            return std::tuple{ inventories, registry, worldModel.getPtrRegistryRevision(),
                worldModel.getLastGeneratedRefNum(), notificationCounts(), dormant };
        };
        using ScriptEntry
            = std::tuple<ESM::RefId, MWWorld::LiveCellRefBase*, MWWorld::CellStore*, MWWorld::ContainerStore*>;
        const auto remainingScripts = [&] {
            std::vector<ScriptEntry> result;
            std::pair<ESM::RefId, MWWorld::Ptr> entry;
            while (localScripts.getNext(entry))
                result.emplace_back(entry.first, entry.second.mRef, entry.second.mCell, entry.second.mContainerStore);
            return result;
        };
        std::vector<ScriptEntry> expectedScripts;
        const auto startScripts = [&] {
            localScripts.startIteration();
            expectedScripts = remainingScripts();
            localScripts.startIteration();
            std::pair<ESM::RefId, MWWorld::Ptr> entry;
            if (!expectedScripts.empty())
                require(localScripts.getNext(entry), "script iteration fixture lost first entry");
        };
        const auto unchangedScripts = [&] {
            const std::vector<ScriptEntry> tail(
                expectedScripts.begin() + !expectedScripts.empty(), expectedScripts.end());
            require(remainingScripts() == tail, "preparation changed live script iteration cursor");
            localScripts.startIteration();
            require(remainingScripts() == expectedScripts, "preparation changed live script membership or bindings");
        };
        const auto prepare = [&](const MWWorld::Ptr& item) {
            const bool fromA = item.mContainerStore == &a;
            return (fromA ? a : b)
                .prepareTransferItem(item, 2, fromA ? b : a, fromA ? addA.mContainer : addB.mContainer,
                    fromA ? addB.mContainer : addA.mContainer, worldModel);
        };
        // This marker is owned only by the prepared RefData. Its destructor proves
        // the entire temporary reference reaches cleanup during unwinding.
        struct Lifetime final : MWWorld::CustomData
        {
            int& mAlive;
            explicit Lifetime(int& alive)
                : mAlive(alive)
            {
                ++mAlive;
            }
            ~Lifetime() override { --mAlive; }
            std::unique_ptr<MWWorld::CustomData> clone() const override
            {
                throw std::logic_error("preparation must never clone arbitrary custom state");
            }
        };
        struct PreparationFailure
        {
        };
        for (const auto& item : live)
        {
            for (bool fail : { false, true })
            {
                const auto before = snapshot();
                startScripts();
                int alive = 0;
                osg::observer_ptr<SceneUtil::PositionAttitudeTransform> temporaryNode;
                bool caught = false;
                try
                {
                    auto prepared = prepare(item);
                    const MWWorld::Ptr temporary(prepared.get());
                    require(temporary.mRef != item.mRef && temporary.mRef->mWorldModel == nullptr
                            && !temporary.getCellRef().getRefNum().isSet() && temporary.mCell == nullptr
                            && temporary.mContainerStore == nullptr && temporary.getRefData().getBaseNode() == nullptr
                            && temporary.getRefData().getCustomData() == nullptr
                            && temporary.getRefData().getLuaScripts() == nullptr
                            && temporary.get<ESM::Miscellaneous>()->mBase == item.get<ESM::Miscellaneous>()->mBase
                            && temporary.getCellRef().getCount(false) == 2 && values(temporary) == values(item),
                        "prepared item lost value state or retained live links");
                    require(!localScripts.isRunning(scriptId, temporary), "preparation registered a temporary script");
                    auto& locals = temporary.getRefData().getLocals();
                    const auto& sourceLocals = item.getRefData().getLocals();
                    if (!locals.isEmpty())
                    {
                        require(locals.mShorts.data() != sourceLocals.mShorts.data()
                                && locals.mLongs.data() != sourceLocals.mLongs.data()
                                && locals.mFloats.data() != sourceLocals.mFloats.data(),
                            "prepared locals alias live buffers");
                        locals.mShorts[0] = 1;
                        locals.mLongs[0] += 99;
                        locals.mFloats[0] += 10;
                        require(locals.setVar(*store.get<ESM::Script>().find(scriptId), "counter", 999, scripts),
                            "prepared locals lost compiler identity");
                    }
                    auto& data = temporary.getRefData();
                    require(data.activateByScript() && data.activate(), "preparation lost buffered activation flags");
                    require(data.getAnimationState().mScriptedAnims.data()
                            != item.getRefData().getAnimationState().mScriptedAnims.data(),
                        "prepared animation aliases live state");
                    data.getAnimationState().mScriptedAnims[0].mGroup = "temporary animation";
                    data.getAnimationState().mScriptedAnims[0].mTime = 100;
                    data.enable();
                    data.mPhysicsPostponed = false;
                    data.setPosition({});
                    temporary.getCellRef().setSoul(ESM::RefId());
                    temporary.getCellRef().setOwner(ESM::RefId());
                    temporary.getCellRef().setCharge(1);
                    temporary.getCellRef().setChargeIntRemainder(0);
                    temporary.getCellRef().setEnchantmentCharge(0);
                    temporary.getCellRef().setPosition({});
                    // Also exercise cleanup of a zero-count temporary, using a private
                    // LocalScripts collection rather than the live script registry.
                    MWWorld::LocalScripts temporaryScripts(store);
                    temporary.getCellRef().setCount(0, temporaryScripts);
                    data.setCustomData(std::make_unique<Lifetime>(alive));
                    data.setBaseNode(new SceneUtil::PositionAttitudeTransform);
                    temporaryNode = data.getBaseNode();
                    require(alive == 1 && temporaryNode.valid() && snapshot() == before,
                        "mutating prepared item changed live inventory, registry or notifications");
                    // Inject a failure in the remaining preparation work, while the
                    // owned item and its mutated locals still exist. No install is attempted.
                    if (fail)
                        throw PreparationFailure{};
                }
                catch (const PreparationFailure&)
                {
                    caught = true;
                }
                require(caught == fail && alive == 0 && !temporaryNode.valid(),
                    "failed or discarded preparation leaked temporary state");
                require(snapshot() == before, "preparation cleanup changed live state or emitted success");
                unchangedScripts();
            }
        }

        // Copying the notification consumer is real, fallible effect preparation.
        // Its owned member is constructed before the injected throw, so both normal
        // discard and a partially constructed intent must release that ownership.
        struct IntentLifetime
        {
            int& mAlive;
            explicit IntentLifetime(int& alive)
                : mAlive(alive)
            {
                ++mAlive;
            }
            IntentLifetime(const IntentLifetime& other)
                : IntentLifetime(other.mAlive)
            {
            }
            ~IntentLifetime() { --mAlive; }
        };
        struct NotificationIntent
        {
            IntentLifetime mLifetime;
            std::function<void()>& mOnCopy;
            int& mEmitted;
            NotificationIntent(int& alive, std::function<void()>& onCopy, int& emitted)
                : mLifetime(alive)
                , mOnCopy(onCopy)
                , mEmitted(emitted)
            {
            }
            NotificationIntent(const NotificationIntent& other)
                : mLifetime(other.mLifetime)
                , mOnCopy(other.mOnCopy)
                , mEmitted(other.mEmitted)
            {
                if (mOnCopy)
                    mOnCopy();
            }
            void operator()(const MWWorld::Ptr&) const { ++mEmitted; }
        };
        for (const auto& item : live)
        {
            const bool fromA = item.mContainerStore == &a;
            auto& destination = fromA ? b : a;
            // Each owner can be the explicit player, the other player's container,
            // or a nonplayer destination with no selected player context.
            for (int playerMode : { 0, 1, 2 })
            {
                for (bool fail : { false, true })
                {
                    const auto before = snapshot();
                    startScripts();
                    int itemAlive = 0, intentAlive = 0, emitted = 0, copies = 0;
                    bool caught = false;
                    osg::observer_ptr<SceneUtil::PositionAttitudeTransform> temporaryNode;
                    std::function<void()> onCopy;
                    {
                        auto context = fromA ? scriptedAddB : scriptedAddA;
                        context.mPlayer = playerMode == 0 ? context.mContainer
                            : playerMode == 1             ? (fromA ? addA.mContainer : addB.mContainer)
                                                          : MWWorld::Ptr();
                        context.mInventoryUpdated = NotificationIntent(intentAlive, onCopy, emitted);
                        require(intentAlive == 1, "notification fixture retained a temporary copy");
                        try
                        {
                            auto detached = prepare(item);
                            const MWWorld::Ptr temporary(detached.get());
                            auto expected = prepare(item);
                            expected->mRef.setPosition({});
                            expected->mRef.setOwner({});
                            expected->mRef.resetGlobalVariable();
                            expected->mRef.setFaction({});
                            expected->mRef.setFactionRank(-2);
                            const bool hasScript = !item.getClass().getScript(item).empty();
                            if (hasScript && playerMode == 0)
                                expected->mData.getLocals().mShorts[0] = 1;
                            onCopy = [&] {
                                ++copies;
                                require(intentAlive == 2 && values(temporary) == values(MWWorld::Ptr(expected.get()))
                                        && temporary.mRef->mWorldModel == nullptr
                                        && !temporary.getCellRef().getRefNum().isSet() && temporary.mCell == nullptr
                                        && temporary.mContainerStore == nullptr
                                        && !localScripts.isRunning(scriptId, temporary) && snapshot() == before,
                                    "effect preparation changed live state or prepared wrong destination locals");
                                // Installed only in temporary test state, after the production
                                // custom-state exclusion. Unwinding must destroy the whole item.
                                temporary.getRefData().setCustomData(std::make_unique<Lifetime>(itemAlive));
                                temporary.getRefData().setBaseNode(new SceneUtil::PositionAttitudeTransform);
                                temporaryNode = temporary.getRefData().getBaseNode();
                                if (fail)
                                    throw PreparationFailure{};
                            };
                            auto prepared = destination.prepareTransferAdd(std::move(detached), context);
                            onCopy = {};
                            require(!detached && prepared.mItem.get() == temporary.mRef
                                    && prepared.mOwner == context.mContainer && prepared.mCount == 2
                                    && !prepared.getStackTarget().isSet() && prepared.getStackCount() == 2
                                    && prepared.mNotifyItemAdded && prepared.mInventoryUpdated
                                    && prepared.mScript.has_value() == hasScript && intentAlive == 2 && itemAlive == 1,
                                "prepared effects lost ownership or destination notification intent");
                            if (hasScript)
                                require(prepared.mScript->mScript == scriptId
                                        && prepared.mScript->mCell
                                            == (playerMode == 0 ? nullptr : context.mContainer.mCell),
                                    "prepared script intent has wrong script or owner cell");
                        }
                        catch (const PreparationFailure&)
                        {
                            caught = true;
                        }
                        onCopy = {};
                        require(caught == fail && copies == 1 && intentAlive == 1 && itemAlive == 0
                                && !temporaryNode.valid() && emitted == 0 && snapshot() == before,
                            "effect preparation failure/discard leaked state, intents or success");
                    }
                    require(intentAlive == 0, "notification intent survived its operation");
                    unchangedScripts();
                }
            }
        }
        const auto reject = [&](const auto& operation, std::string_view reason) {
            const auto before = snapshot();
            startScripts();
            bool caught = false;
            try
            {
                operation();
            }
            catch (const std::exception& error)
            {
                caught = true;
                require(std::string_view(error.what()).find(reason) != std::string_view::npos,
                    "unexpected preparation rejection reason");
            }
            require(caught, "preparation accepted a stale or unsupported decision");
            require(snapshot() == before, "preparation rejection changed live state");
            unchangedScripts();
        };
        const auto checkRemoval = [&](const MWWorld::Ptr& item, int count, int remaining,
                                      MWWorld::ContainerStore& source, const MWWorld::Ptr& owner) {
            const auto before = snapshot();
            startScripts();
            auto prepared = source.prepareTransferRemove(item, count, owner, worldModel, &localScripts);
            require(prepared.getItemIdentity() == item.getCellRef().getRefNum() && prepared.getCount() == count
                    && prepared.getRemainingCount() == remaining,
                "source preparation chose the wrong identity/quantity/signed remainder");
            const auto registration = localScripts.prepareRemove(&item.getCellRef());
            const auto* intent = prepared.getScriptRemoval();
            require((intent != nullptr) == (remaining == 0 && registration.hasRegistration()),
                "source preparation failed to preserve partial registration or defer full deregistration");
            if (intent)
                require(intent->getScript() == item.getClass().getScript(item)
                        && intent->getCell() == registration.getCell() && intent->getContainer() == &source,
                    "source deregistration intent lost script/cell/store binding");
            source.validateTransferRemoval(prepared, owner, worldModel, &localScripts);
            require(snapshot() == before, "source preparation/validation changed live state");
            unchangedScripts();
            return prepared;
        };
        for (const auto& item : live)
        {
            const bool fromA = item.mContainerStore == &a;
            auto& source = fromA ? a : b;
            auto& destination = fromA ? b : a;
            const auto& sourceContext = fromA ? scriptedAddA : scriptedAddB;
            const auto& destinationContext = fromA ? scriptedAddB : scriptedAddA;
            const int originalCount = item.getCellRef().getCount(false);
            const auto originalRegistration = localScripts.prepareRemove(&item.getCellRef());
            const auto restoreRegistration = [&] {
                if (originalRegistration.hasRegistration() && !localScripts.isRunning(scriptId, item))
                {
                    auto registered = item;
                    registered.mCell = originalRegistration.getCell();
                    localScripts.add(scriptId, registered, scripts);
                }
            };
            for (int signedCount : { 4, -4, std::numeric_limits<int>::max(), -std::numeric_limits<int>::max() })
            {
                item.getCellRef().setCount(signedCount);
                for (int quantity : { 2, std::abs(signedCount) })
                {
                    const int remaining = signedCount < 0 ? signedCount + quantity : signedCount - quantity;
                    auto removal = checkRemoval(item, quantity, remaining, source, sourceContext.mContainer);
                    // Compare both branches to actual stock removal in a disposable
                    // base store, outside the preparation-only snapshot.
                    MWWorld::ContainerStore stock;
                    bindEmptyStore(stock, sourceContext.mContainer, worldModel);
                    MWWorld::LocalScripts stockScripts(store);
                    auto stockAdd = sourceContext;
                    stockAdd.mLocalScripts = &stockScripts;
                    const bool hasScript = !item.getClass().getScript(item).empty();
                    auto stockItem
                        = stock.add(hasScript ? scripted.getPtr() : plain.getPtr(), std::abs(signedCount), stockAdd);
                    stockScripts.startIteration();
                    stockItem->getCellRef().setCount(signedCount);
                    stock.setSelectedEnchantItem(stockItem);
                    MWWorld::ContainerStoreRemoveContext removeContext{ worldModel, sourceContext.mContainer,
                        stockScripts, sourceContext.mInventoryUpdated };
                    require(stock.remove(*stockItem, quantity, removeContext) == removal.getCount()
                            && stockItem->getCellRef().getCount(false) == removal.getRemainingCount()
                            && (stock.getSelectedEnchantItem() == stock.end()) == (remaining == 0),
                        "prepared removal differs from stock signed counts/full-removal selection cleanup");
                    std::pair<ESM::RefId, MWWorld::Ptr> stockEntry;
                    require(stockScripts.isRunning(scriptId, *stockItem) == (hasScript && remaining != 0)
                            && stockScripts.getNext(stockEntry) == (hasScript && remaining != 0)
                            && (remaining == 0 || !hasScript || stockEntry.second == *stockItem),
                        "stock removal lost registration preservation or next-iterator repair");
                    if (std::abs(signedCount) != 4)
                        continue;
                    const auto before = snapshot();
                    startScripts();
                    {
                        auto detached = source.prepareTransferItem(item, quantity, destination,
                            sourceContext.mContainer, destinationContext.mContainer, worldModel);
                        auto addition = destination.prepareTransferAdd(std::move(detached), destinationContext);
                        source.validateTransferRemoval(removal, sourceContext.mContainer, worldModel, &localScripts);
                        destination.validateTransferStacking(addition, destinationContext);
                    }
                    require(snapshot() == before, "two-owner partial/full preparation or discard changed live state");
                    unchangedScripts();
                }
            }
            item.getCellRef().setCount(originalCount);
            for (int quantity : { 0, -1, std::numeric_limits<int>::min(), std::abs(originalCount) + 1 })
                reject(
                    [&] {
                        source.prepareTransferRemove(
                            item, quantity, sourceContext.mContainer, worldModel, &localScripts);
                    },
                    "count");
            for (int invalidCount : { 0, std::numeric_limits<int>::min() })
            {
                item.getCellRef().setCount(invalidCount, localScripts);
                reject(
                    [&] { source.prepareTransferRemove(item, 1, sourceContext.mContainer, worldModel, &localScripts); },
                    invalidCount == 0 ? "ownership mismatch" : "count is invalid");
                item.getCellRef().setCount(originalCount);
                restoreRegistration();
            }

            const auto staleSource = [&](const auto& change, const auto& restore) {
                auto removal = checkRemoval(item, 2, originalCount < 0 ? originalCount + 2 : originalCount - 2, source,
                    sourceContext.mContainer);
                change();
                reject(
                    [&] {
                        source.validateTransferRemoval(removal, sourceContext.mContainer, worldModel, &localScripts);
                    },
                    "changed");
                restore();
                restoreRegistration();
            };
            for (int changedCount : { -originalCount, originalCount + 1, 0, std::numeric_limits<int>::min() })
                staleSource([&] { item.getCellRef().setCount(changedCount, localScripts); },
                    [&] { item.getCellRef().setCount(originalCount); });
            // Each copied CellRef value must still describe the prepared source.
            const auto savedRef = item.getCellRef();
            for (const auto& change :
                std::vector<std::function<void(MWWorld::CellRef&)>>{ [](auto& ref) { ref.setSoul({}); },
                    [](auto& ref) { ref.setOwner({}); }, [](auto& ref) { ref.setFaction({}); },
                    [](auto& ref) { ref.setFactionRank(0); }, [](auto& ref) { ref.setCharge(1); },
                    [](auto& ref) { ref.setChargeIntRemainder(0); }, [](auto& ref) { ref.setEnchantmentCharge(1); },
                    [](auto& ref) { ref.setScale(1); }, [](auto& ref) { ref.setPosition({}); } })
                staleSource([&] { change(item.getCellRef()); }, [&] { item.getCellRef() = savedRef; });
            const auto savedData = item.getRefData().copyForContainerTransfer();
            const osg::ref_ptr<SceneUtil::PositionAttitudeTransform> savedNode = item.getRefData().getBaseNode();
            for (const auto& change : std::vector<std::function<void(MWWorld::RefData&)>>{
                     [](auto& data) { data.setDeletedByContentFile(true); }, [](auto& data) { data.enable(); },
                     [](auto& data) { data.setPosition({}); }, [](auto& data) { data.mPhysicsPostponed = false; },
                     [](auto& data) { data.getAnimationState().mScriptedAnims[0].mTime += 1; },
                     [](auto& data) { data.activateByScript(); },
                     [](auto& data) { data.getLocals().mLongs.push_back(7); } })
                staleSource([&] { change(item.getRefData()); },
                    [&] {
                        item.getRefData() = savedData.copyForContainerTransfer();
                        item.getRefData().setBaseNode(savedNode);
                    });
            const auto base = item.get<ESM::Miscellaneous>()->mBase;
            auto replacementBase = *base;
            staleSource([&] { item.get<ESM::Miscellaneous>()->mBase = &replacementBase; },
                [&] { item.get<ESM::Miscellaneous>()->mBase = base; });
            replacementBase.mScript = scriptId;
            staleSource([&] { item.get<ESM::Miscellaneous>()->mBase = &replacementBase; },
                [&] { item.get<ESM::Miscellaneous>()->mBase = base; });
            staleSource([&] { item.getCellRef().unsetRefNum(); }, [&] { item.getCellRef() = savedRef; });
            staleSource([&] { worldModel.deregisterLiveCellRef(*item.mRef); }, [&] { worldModel.registerPtr(item); });
            staleSource(
                [&] {
                    auto relocated = item;
                    relocated.mContainerStore = &destination;
                    worldModel.registerPtr(relocated);
                },
                [&] { worldModel.registerPtr(item); });
            int alive = 0;
            staleSource([&] { item.getRefData().setCustomData(std::make_unique<Lifetime>(alive)); },
                [&] { item.getRefData().setCustomData(nullptr); });
            require(alive == 0, "stale source validation cloned or retained custom data");
            auto removal = checkRemoval(
                item, 2, originalCount < 0 ? originalCount + 2 : originalCount - 2, source, sourceContext.mContainer);
            reject(
                [&] {
                    destination.validateTransferRemoval(
                        removal, destinationContext.mContainer, worldModel, &localScripts);
                },
                "context changed");
            auto movedOwner = sourceContext.mContainer;
            movedOwner.mCell = destinationContext.mContainer.mCell;
            reject([&] { source.validateTransferRemoval(removal, movedOwner, worldModel, &localScripts); },
                "context changed");
            worldModel.registerPtr(movedOwner);
            reject(
                [&] { source.validateTransferRemoval(removal, sourceContext.mContainer, worldModel, &localScripts); },
                "context changed");
            reject([&] { source.prepareTransferRemove(item, 2, sourceContext.mContainer, worldModel, &localScripts); },
                "owner cell mismatch");
            worldModel.registerPtr(sourceContext.mContainer);
        }
        for (const auto& item : { *scriptA, *scriptB })
        {
            const bool fromA = item.mContainerStore == &a;
            auto& source = fromA ? a : b;
            const auto& context = fromA ? scriptedAddA : scriptedAddB;
            const auto registration = localScripts.prepareRemove(&item.getCellRef());
            auto registered = item;
            registered.mCell = registration.getCell();
            const int count = item.getCellRef().getCount();
            for (int quantity : { 1, count })
            {
                const auto prepareRemoval
                    = [&] { return checkRemoval(item, quantity, count - quantity, source, context.mContainer); };
                const auto validate = [&](const auto& removal) {
                    source.validateTransferRemoval(removal, context.mContainer, worldModel, &localScripts);
                };
                auto removal = prepareRemoval();
                reject([&] { source.validateTransferRemoval(removal, context.mContainer, worldModel); },
                    "context changed");
                MWWorld::LocalScripts otherScripts(store);
                reject([&] { source.validateTransferRemoval(removal, context.mContainer, worldModel, &otherScripts); },
                    "context changed");
                reject([&] { source.prepareTransferRemove(item, quantity, context.mContainer, worldModel); },
                    "requires LocalScripts");

                // Erased registration/iterator, then an identical registration:
                // neither absence nor reusing the same Ptr/script/cell may validate.
                localScripts.remove(item);
                reject([&] { validate(removal); }, "registration changed");
                auto absent = prepareRemoval();
                require(!absent.getScriptRemoval(), "absent stock registration produced a deregistration effect");
                localScripts.add(scriptId, registered, scripts);
                reject([&] { validate(removal); }, "registration changed");
                reject([&] { validate(absent); }, "registration changed");

                for (int change : { 0, 1, 2 })
                {
                    auto current = prepareRemoval();
                    localScripts.remove(item);
                    auto changed = registered;
                    if (change == 0)
                        changed.mCell = fromA ? addB.mContainer.mCell : nullptr;
                    else if (change == 1)
                        changed.mContainerStore = fromA ? &b : &a;
                    localScripts.add(
                        change == 2 ? ESM::RefId::stringRefId("alternate_script") : scriptId, changed, scripts);
                    reject([&] { validate(current); }, "registration changed");
                    if (change != 0)
                        reject(
                            [&] {
                                source.prepareTransferRemove(
                                    item, quantity, context.mContainer, worldModel, &localScripts);
                            },
                            "registration mismatch");
                    localScripts.remove(item);
                    localScripts.add(scriptId, registered, scripts);
                }

                // Initialized script identity and each owned locals buffer are
                // independent stale inputs. No compiler or script execution is needed.
                const auto saved = item.getRefData().copyForContainerTransfer();
                const osg::ref_ptr<SceneUtil::PositionAttitudeTransform> node = item.getRefData().getBaseNode();
                for (int change : { 0, 1, 2, 3 })
                {
                    auto current = prepareRemoval();
                    auto& locals = item.getRefData().getLocals();
                    if (change == 0)
                        locals = MWScript::Locals();
                    else if (change == 1)
                        ++locals.mShorts[0];
                    else if (change == 2)
                        ++locals.mLongs[0];
                    else
                        locals.mFloats[0] += 1;
                    reject([&] { validate(current); }, "state changed");
                    if (change == 0)
                        reject(
                            [&] {
                                source.prepareTransferRemove(
                                    item, quantity, context.mContainer, worldModel, &localScripts);
                            },
                            "matching initialized");
                    item.getRefData() = saved.copyForContainerTransfer();
                    item.getRefData().setBaseNode(node);
                }
            }
        }

        // Fallible destination notification preparation runs after both source
        // state/deregistration and destination item/add intents are owned.
        for (const auto& item : { *scriptA, *scriptB })
        {
            const bool fromA = item.mContainerStore == &a;
            auto& source = fromA ? a : b;
            auto& destination = fromA ? b : a;
            const auto& owner = fromA ? addA.mContainer : addB.mContainer;
            for (int quantity : { 1, item.getCellRef().getCount() })
                for (bool fail : { false, true })
                {
                    int itemAlive = 0, intentAlive = 0, emitted = 0, copies = 0;
                    osg::observer_ptr<SceneUtil::PositionAttitudeTransform> node;
                    std::function<void()> onCopy;
                    {
                        auto context = fromA ? scriptedAddB : scriptedAddA;
                        context.mInventoryUpdated = NotificationIntent(intentAlive, onCopy, emitted);
                        const auto before = snapshot();
                        startScripts();
                        bool caught = false;
                        try
                        {
                            auto initial
                                = source.prepareTransferRemove(item, quantity, owner, worldModel, &localScripts);
                            auto removal = std::move(initial);
                            require(
                                (removal.getScriptRemoval() != nullptr) == (quantity == item.getCellRef().getCount()),
                                "late failure source lost owned deregistration intent");
                            auto detached = source.prepareTransferItem(
                                item, quantity, destination, owner, context.mContainer, worldModel);
                            const MWWorld::Ptr temporary(detached.get());
                            onCopy = [&] {
                                source.validateTransferRemoval(removal, owner, worldModel, &localScripts);
                                ++copies;
                                temporary.getRefData().setCustomData(std::make_unique<Lifetime>(itemAlive));
                                temporary.getRefData().setBaseNode(new SceneUtil::PositionAttitudeTransform);
                                node = temporary.getRefData().getBaseNode();
                                require(intentAlive == 2 && temporary.getRefData().getLocals().getScriptId() == scriptId
                                        && snapshot() == before,
                                    "late scripted failure preceded owned temporary/effect preparation");
                                if (fail)
                                    throw PreparationFailure{};
                            };
                            auto addition = destination.prepareTransferAdd(std::move(detached), context);
                            require(addition.mScript && addition.mScript->mScript == scriptId
                                    && !addition.getStackTarget().isSet(),
                                "late failure destination lost deferred registration or stacked a script");
                        }
                        catch (const PreparationFailure&)
                        {
                            caught = true;
                        }
                        onCopy = {};
                        require(caught == fail && copies == 1 && itemAlive == 0 && intentAlive == 1 && !node.valid()
                                && emitted == 0 && snapshot() == before,
                            "scripted removal/add failure or discard leaked state, registrations or success");
                        unchangedScripts();
                    }
                    require(intentAlive == 0, "scripted operation retained a notification intent");
                }
        }

        // Stock callers still repair the next iterator for both removal overloads,
        // cell cleanup and clear, including erasing already-visited entries.
        for (int consumed : { 0, 1, 2 })
            for (int cleanup : { 0, 1, 2, 3 })
            {
                const auto before = snapshot();
                MWWorld::LocalScripts stockScripts(store);
                auto first = *scriptA;
                auto second = *scriptB;
                first.mCell = nullptr;
                second.mCell = addB.mContainer.mCell;
                stockScripts.add(scriptId, first, scripts);
                stockScripts.add(scriptId, second, scripts);
                auto old = stockScripts.prepareRemove(&first.getCellRef());
                stockScripts.startIteration();
                std::pair<ESM::RefId, MWWorld::Ptr> entry;
                for (int i = 0; i < consumed; ++i)
                    require(stockScripts.getNext(entry), "stock script iteration fixture ended early");
                if (cleanup == 0)
                    stockScripts.remove(&first.getCellRef());
                else if (cleanup == 1)
                    stockScripts.remove(first);
                else if (cleanup == 2)
                    stockScripts.clearCell(nullptr);
                else
                    stockScripts.clear();
                const bool expectSecond = cleanup != 3 && consumed < 2;
                require(stockScripts.getNext(entry) == expectSecond && (!expectSecond || entry.second == second)
                        && !stockScripts.getNext(entry),
                    "stock cleanup changed next-script repair");
                stockScripts.add(scriptId, first, scripts);
                reject([&] { stockScripts.validateRemoval(old, &first.getCellRef()); }, "registration changed");
                stockScripts.remove(&first.getCellRef()); // Also safe after clear/re-add without startIteration.
                require(snapshot() == before, "stock script-only fixture changed inventory or notifications");
            }

        // Destroy source nodes while decisions survive; validation and preparation
        // from the stale Ptr must reject without dereferencing it or old iterators.
        for (bool scriptedSource : { false, true })
        {
            MWWorld::ContainerStore source;
            bindEmptyStore(source, addA.mContainer, worldModel);
            observedStores.push_back(&source);
            const auto& sourceContext = scriptedSource ? scriptedAddA : addA;
            const auto& sourceItem = scriptedSource ? scripted.getPtr() : plain.getPtr();
            const auto oldItem = *source.add(sourceItem, 4, sourceContext);
            const auto* oldCellRef = &oldItem.getCellRef();
            const auto identity = oldItem.getCellRef().getRefNum();
            auto partial = checkRemoval(oldItem, 2, 2, source, addA.mContainer);
            auto full = checkRemoval(oldItem, 4, 0, source, addA.mContainer);
            MWWorld::ContainerStore replacement;
            bindEmptyStore(replacement, addA.mContainer, worldModel);
            const auto newItem = *replacement.add(sourceItem, 4, sourceContext);
            source = std::move(replacement);
            for (const auto* decision : { &partial, &full })
                reject([&] { source.validateTransferRemoval(*decision, addA.mContainer, worldModel, &localScripts); },
                    "changed");
            reject([&] { source.prepareTransferRemove(oldItem, 2, addA.mContainer, worldModel, &localScripts); },
                "ownership mismatch");
            require(worldModel.getPtr(identity).isEmpty(), "destroyed source retained its registration");
            worldModel.deregisterLiveCellRef(*newItem.mRef);
            newItem.getCellRef().setRefNum(identity);
            auto registeredReplacement = newItem;
            registeredReplacement.mContainerStore = &source;
            worldModel.registerPtr(registeredReplacement);
            for (const auto* decision : { &partial, &full })
                reject([&] { source.validateTransferRemoval(*decision, addA.mContainer, worldModel, &localScripts); },
                    "changed");
            // A stale script Ptr still exists here. Lookup for another item must
            // skip it without reading freed memory. Stock cleanup can also use its
            // saved address key, repairing iteration without dereferencing it.
            const auto beforeLookup = snapshot();
            startScripts();
            const auto replacementIntent = localScripts.prepareRemove(&newItem.getCellRef());
            localScripts.validateRemoval(replacementIntent, &newItem.getCellRef());
            require(replacementIntent.hasRegistration() == scriptedSource && snapshot() == beforeLookup,
                "lookup past a destroyed source changed state or lost the replacement registration");
            unchangedScripts();
            localScripts.remove(oldCellRef);
            localScripts.remove(newItem);
            observedStores.pop_back();
        }
        const auto checkDecision
            = [&](const MWWorld::Ptr& item, MWWorld::ContainerStore& destination,
                  const MWWorld::ContainerStoreAddContext& context, ESM::RefNum target, int count) {
                  const auto before = snapshot();
                  startScripts();
                  auto detached = (item.mContainerStore == &a ? a : b)
                                      .prepareTransferItem(item, 2, destination,
                                          item.mContainerStore == &a ? addA.mContainer : addB.mContainer,
                                          context.mContainer, worldModel);
                  auto prepared = destination.prepareTransferAdd(std::move(detached), context);
                  require(prepared.getStackTarget() == target && prepared.getStackCount() == count
                          && prepared.mItem->mRef.getCount(false) == 2 && !prepared.mItem->mRef.getRefNum().isSet()
                          && prepared.mItem->mWorldModel == nullptr,
                      "destination preparation chose the wrong stack/count or attached the temporary");
                  destination.validateTransferStacking(prepared, context);
                  require(snapshot() == before, "stacking preparation/validation changed live state");
                  unchangedScripts();
                  return prepared;
              };
        for (const auto& item : { *plainA, *plainB })
        {
            const bool fromA = item.mContainerStore == &a;
            auto& destination = fromA ? b : a;
            const auto& context = fromA ? addB : addA;
            const auto target = fromA ? *plainB : *plainA;
            checkDecision(item, destination, context, {}, 2); // Different souls cannot stack.
            const auto soul = target.getCellRef().getSoul();
            const auto originalCount = target.getCellRef().getCount(false);
            target.getCellRef().setSoul(item.getCellRef().getSoul());
            for (int signedCount :
                { 7, -7, std::numeric_limits<int>::max() - 2, -(std::numeric_limits<int>::max() - 2) })
            {
                target.getCellRef().setCount(signedCount);
                const int expected = signedCount < 0 ? signedCount - 2 : signedCount + 2;
                auto prepared = checkDecision(item, destination, context, target.getCellRef().getRefNum(), expected);
                // Compare to actual stock addImp/addItems on a separate disposable
                // base store. Stock mutations are outside the preparation snapshot.
                MWWorld::ContainerStore stock;
                bindEmptyStore(stock, context.mContainer, worldModel);
                const MWWorld::ConstPtr value(prepared.mItem.get());
                auto existing = stock.add(value, std::abs(signedCount), context);
                existing->getCellRef().setCount(signedCount);
                auto result = stock.add(value, 2, context);
                require(result == existing && result->getCellRef().getCount(false) == prepared.getStackCount(),
                    "prepared signed arithmetic differs from stock add");
            }
            target.getCellRef().setCount(originalCount);
            target.getCellRef().setSoul(soul);
        }
        // Scripts remain separate even when all non-script stacking inputs match.
        const auto scriptedSoul = scriptB->getCellRef().getSoul();
        scriptB->getCellRef().setSoul(scriptA->getCellRef().getSoul());
        checkDecision(*scriptA, b, scriptedAddB, {}, 2);
        checkDecision(*scriptB, a, scriptedAddA, {}, 2);
        scriptB->getCellRef().setSoul(scriptedSoul);

        const auto destinationSoul = plainB->getCellRef().getSoul();
        plainB->getCellRef().setSoul(plainA->getCellRef().getSoul());
        // Capture a decision, make exactly one intentional change, then snapshot
        // that new state. Rejection itself must have no additional side effects.
        const auto stale = [&](const auto& change, const auto& restore) {
            auto prepared = checkDecision(*plainA, b, addB, plainB->getCellRef().getRefNum(), 9);
            change();
            reject([&] { b.validateTransferStacking(prepared, addB); }, "changed");
            restore();
        };
        stale([&] { plainB->getCellRef().setCount(-7); }, [&] { plainB->getCellRef().setCount(7); });
        stale([&] { plainB->getCellRef().setCount(8); }, [&] { plainB->getCellRef().setCount(7); });
        stale([&] { plainB->getCellRef().setCount(0, localScripts); }, [&] { plainB->getCellRef().setCount(7); });
        stale([&] { plainB->getCellRef().setSoul(destinationSoul); },
            [&] { plainB->getCellRef().setSoul(plainA->getCellRef().getSoul()); });
        stale([&] { plainB->getRefData().setDeletedByContentFile(true); },
            [&] { plainB->getRefData().setDeletedByContentFile(false); });
        // A different immutable base, even with the same ID/soul, invalidates the witness.
        const auto originalBase = plainB->get<ESM::Miscellaneous>()->mBase;
        auto replacementBase = *originalBase;
        stale([&] { plainB->get<ESM::Miscellaneous>()->mBase = &replacementBase; },
            [&] { plainB->get<ESM::Miscellaneous>()->mBase = originalBase; });
        replacementBase.mScript = scriptId;
        stale(
            [&] {
                plainB->get<ESM::Miscellaneous>()->mBase = &replacementBase;
                plainB->getRefData().getLocals() = scriptB->getRefData().getLocals();
            },
            [&] {
                plainB->get<ESM::Miscellaneous>()->mBase = originalBase;
                plainB->getRefData().getLocals() = {};
            });
        stale([&] { worldModel.deregisterLiveCellRef(*plainB->mRef); }, [&] { worldModel.registerPtr(*plainB); });
        stale(
            [&] {
                auto relocated = *plainB;
                relocated.mContainerStore = &a;
                worldModel.registerPtr(relocated);
            },
            [&] { worldModel.registerPtr(*plainB); });
        {
            auto prepared = checkDecision(*plainA, b, addB, plainB->getCellRef().getRefNum(), 9);
            prepared.mItem->mRef.setSoul(destinationSoul);
            reject([&] { b.validateTransferStacking(prepared, addB); }, "item changed");
        }
        {
            auto prepared = checkDecision(*plainA, b, addB, plainB->getCellRef().getRefNum(), 9);
            reject([&] { a.validateTransferStacking(prepared, addA); }, "context changed");
            auto movedOwner = addB;
            movedOwner.mContainer.mCell = addA.mContainer.mCell;
            reject([&] { b.validateTransferStacking(prepared, movedOwner); }, "context changed");
            worldModel.registerPtr(movedOwner.mContainer);
            reject([&] { b.validateTransferStacking(prepared, addB); }, "context changed");
            reject([&] { b.prepareTransferAdd(prepare(*plainA), addB); }, "owner cell mismatch");
            worldModel.registerPtr(addB.mContainer);
            b.setPtr(addA.mContainer, worldModel);
            reject([&] { b.validateTransferStacking(prepared, addB); }, "owner mismatch");
            b.setPtr(addB.mContainer, worldModel);
        }
        // Recheck the count at destination preparation, not only at detachment.
        // INT_MIN must reject before inventory iteration or stock abs(int) arithmetic.
        for (int count :
            { std::numeric_limits<int>::max(), -std::numeric_limits<int>::max(), std::numeric_limits<int>::min() })
        {
            auto detached = prepare(*plainA);
            plainB->getCellRef().setCount(count);
            reject([&] { b.prepareTransferAdd(std::move(detached), addB); }, "overflow");
            plainB->getCellRef().setCount(7);
        }
        // Fail after the existing-stack decision and owned temporary are prepared.
        for (bool fail : { false, true })
        {
            int itemAlive = 0, intentAlive = 0, emitted = 0, copies = 0;
            osg::observer_ptr<SceneUtil::PositionAttitudeTransform> node;
            std::function<void()> onCopy;
            auto context = addB;
            context.mInventoryUpdated = NotificationIntent(intentAlive, onCopy, emitted);
            const auto before = snapshot();
            startScripts();
            bool caught = false;
            try
            {
                auto removal = a.prepareTransferRemove(*plainA, 4, addA.mContainer, worldModel);
                require(removal.getRemainingCount() == 0, "late failure fixture did not prepare full source removal");
                auto detached = a.prepareTransferItem(
                    *plainA, removal.getCount(), b, addA.mContainer, addB.mContainer, worldModel);
                const MWWorld::Ptr temporary(detached.get());
                onCopy = [&] {
                    a.validateTransferRemoval(removal, addA.mContainer, worldModel);
                    ++copies;
                    temporary.getRefData().setCustomData(std::make_unique<Lifetime>(itemAlive));
                    temporary.getRefData().setBaseNode(new SceneUtil::PositionAttitudeTransform);
                    node = temporary.getRefData().getBaseNode();
                    if (fail)
                        throw PreparationFailure{};
                };
                auto prepared = b.prepareTransferAdd(std::move(detached), context);
                require(prepared.getStackTarget() == plainB->getCellRef().getRefNum() && prepared.getStackCount() == 11
                        && itemAlive == 1 && intentAlive == 2,
                    "late-failure fixture did not prepare an owned existing-stack decision");
            }
            catch (const PreparationFailure&)
            {
                caught = true;
            }
            onCopy = {};
            require(caught == fail && copies == 1 && itemAlive == 0 && intentAlive == 1 && !node.valid() && emitted == 0
                    && snapshot() == before,
                "existing-stack failure/discard leaked temporary state or emitted success");
            unchangedScripts();
        }
        plainB->getCellRef().setSoul(destinationSoul);

        // An empty destination, multiple candidates, and destruction/replacement of
        // list nodes use the same two owners without disturbing their scripted lists.
        {
            MWWorld::ContainerStore destination;
            bindEmptyStore(destination, addB.mContainer, worldModel);
            observedStores.push_back(&destination);
            auto empty = checkDecision(*plainA, destination, addB, {}, 2);
            auto incompatible = destination.add(plain.getPtr(), 3, addB);
            reject([&] { destination.validateTransferStacking(empty, addB); }, "changed");
            auto separate = checkDecision(*plainA, destination, addB, {}, 2);
            auto value = prepare(*plainA);
            auto compatible = destination.add(MWWorld::ConstPtr(value.get()), 5, addB);
            reject([&] { destination.validateTransferStacking(separate, addB); }, "changed");
            auto selected = checkDecision(*plainA, destination, addB, compatible->getCellRef().getRefNum(), 7);
            incompatible->getCellRef().setSoul(plainA->getCellRef().getSoul());
            reject([&] { destination.validateTransferStacking(selected, addB); }, "changed");
            auto first = checkDecision(*plainA, destination, addB, incompatible->getCellRef().getRefNum(), 5);
            compatible->getCellRef().setCount(std::numeric_limits<int>::max() - 3);
            reject([&] { destination.prepareTransferAdd(prepare(*plainA), addB); }, "overflow");
            compatible->getCellRef().setCount(5);
            MWWorld::ContainerStore replacement;
            bindEmptyStore(replacement, addB.mContainer, worldModel);
            destination = std::move(replacement); // Destroys the old nodes, unlike clear's zero-count tombstones.
            reject([&] { destination.validateTransferStacking(first, addB); }, "membership changed");
            destination.add(MWWorld::ConstPtr(value.get()), 3, addB);
            reject([&] { destination.validateTransferStacking(first, addB); }, "changed");
            observedStores.pop_back();
        }
        for (const auto& item : { *scriptA, *scriptB })
        {
            const bool fromA = item.mContainerStore == &a;
            auto& destination = fromA ? b : a;
            auto context = fromA ? scriptedAddB : scriptedAddA;
            context.mPlayer = context.mContainer;
            int alive = 0, calls = 0;
            reject(
                [&] {
                    auto detached = prepare(item);
                    const MWWorld::Ptr temporary(detached.get());
                    scripts.mBeforeLocals = [&] {
                        ++calls;
                        require(!localScripts.isRunning(scriptId, temporary) && flags(temporary.getRefData()) == 0
                                && temporary.getRefData().getLocals().mShorts == item.getRefData().getLocals().mShorts,
                            "OnPCAdd lookup lost new-stack flag clearing or ran after live registration/assignment");
                        temporary.getRefData().setCustomData(std::make_unique<Lifetime>(alive));
                        throw std::runtime_error("injected OnPCAdd preparation failure");
                    };
                    destination.prepareTransferAdd(std::move(detached), context);
                },
                "injected OnPCAdd preparation failure");
            scripts.mBeforeLocals = {};
            require(calls == 1 && alive == 0, "OnPCAdd preparation swallowed failure or leaked its item");
            context.mScriptManager = nullptr;
            reject([&] { destination.prepareTransferAdd(prepare(item), context); }, "requires LocalScripts");
            context.mScriptManager = &scripts;
            context.mInventoryUpdated = {};
            reject([&] { destination.prepareTransferAdd(prepare(item), context); }, "presentation consumer");
        }
        const auto attempt = [&](const MWWorld::ConstPtr& item, int count, const MWWorld::ContainerStore& from,
                                 const MWWorld::ContainerStore& to) {
            return from.prepareTransferItem(item, count, to, addA.mContainer, addB.mContainer, worldModel);
        };
        for (int count : { 0, -1, std::numeric_limits<int>::min(), 5 })
            reject([&] { attempt(*plainA, count, a, b); }, "count");
        plainA->getCellRef().setCount(std::numeric_limits<int>::min());
        reject([&] { prepare(*plainA); }, "count is invalid");
        plainA->getCellRef().setCount(-4);
        reject([&] { attempt({}, 1, a, b); }, "ownership mismatch");
        auto forged = *plainB;
        forged.mContainerStore = &a;
        reject([&] { attempt(forged, 1, a, b); }, "ownership mismatch");
        reject([&] { a.prepareTransferItem(*plainA, 1, b, addB.mContainer, addA.mContainer, worldModel); },
            "owner mismatch");
        reject([&] { a.prepareTransferItem(*plainA, 1, a, addA.mContainer, addA.mContainer, worldModel); },
            "distinct owners");
        MWWorld::ContainerStore unresolved;
        unresolved.setPtr(addB.mContainer, worldModel);
        reject([&] { attempt(*plainA, 1, a, unresolved); }, "unresolved");
        unresolved.setPtr(addA.mContainer, worldModel);
        reject([&] { attempt(*plainA, 1, unresolved, b); }, "unresolved");
        MWWorld::InventoryStore equipment;
        reject([&] { attempt(*plainA, 1, a, equipment); }, "InventoryStore");
        reject([&] { attempt(*plainA, 1, equipment, b); }, "InventoryStore");
        require(
            !unresolved.isResolved() && unresolved.begin() == unresolved.end() && equipment.begin() == equipment.end(),
            "preparation touched an excluded store");
        plainB->getCellRef().setCount(std::numeric_limits<int>::max());
        reject([&] { prepare(*plainA); }, "overflow");
        plainB->getCellRef().setCount(7);
        worldModel.deregisterLiveCellRef(*plainA->mRef);
        reject([&] { prepare(*plainA); }, "must be registered");
        worldModel.registerPtr(*plainA);
        auto savedLocals = scriptA->getRefData().getLocals();
        scriptA->getRefData().getLocals() = {};
        reject([&] { prepare(*scriptA); }, "initialized script locals");
        scriptA->getRefData().getLocals() = std::move(savedLocals);
        int customAlive = 0;
        plainA->getRefData().setCustomData(std::make_unique<Lifetime>(customAlive));
        reject([&] { prepare(*plainA); }, "Lua or custom state");
        require(customAlive == 1, "rejection cloned or destroyed live custom state");
        plainA->getRefData().setCustomData(nullptr);

        using Pair = MWWorld::PreparedContainerTransfer;
        static_assert(!std::is_copy_constructible_v<Pair> && !std::is_copy_assignable_v<Pair>);
        static_assert(std::is_nothrow_move_constructible_v<Pair> && std::is_nothrow_move_assignable_v<Pair>);
        static_assert(std::is_same_v<decltype(std::declval<const Pair&>().getSourceItem().getRefData()),
            const MWWorld::RefData&>);
        static_assert(std::is_same_v<decltype(std::declval<const Pair&>().getSourceItem().getCellRef()),
            const MWWorld::CellRef&>);
        static_assert(std::is_same_v<decltype(std::declval<const Pair&>().getSourceInventory()[0].mItem.getRefData()),
            const MWWorld::RefData&>);
        static_assert(std::is_same_v<decltype(std::declval<const Pair&>().getSourceInventory()[0].mItem.getCellRef()),
            const MWWorld::CellRef&>);
        static_assert(
            std::is_same_v<decltype(std::declval<const Pair&>().getDestinationInventory()[0].mItem.getRefData()),
                const MWWorld::RefData&>);
        static_assert(
            std::is_same_v<decltype(std::declval<const Pair&>().getDestinationInventory()[0].mItem.getCellRef()),
                const MWWorld::CellRef&>);
        static_assert(
            std::is_same_v<decltype(std::declval<const Pair&>().getItem().getRefData()), const MWWorld::RefData&>);
        static_assert(std::is_same_v<decltype(std::declval<const Pair&>().getDestinationItem().getRefData()),
            const MWWorld::RefData&>);
        static_assert(std::is_same_v<decltype(std::declval<const Pair&>().getDestinationItem().getCellRef()),
            const MWWorld::CellRef&>);
        static_assert(std::is_same_v<decltype(std::declval<const Pair&>().getRemoval()),
            const MWWorld::PreparedContainerRemove&>);
        const auto copyValues = [](const MWWorld::Ptr& to, const MWWorld::ConstPtr& from) {
            const auto identity = to.getCellRef().getRefNum();
            to.getCellRef() = from.getCellRef();
            to.getCellRef().setRefNum(identity);
            to.getRefData() = from.getRefData().copyForContainerTransfer();
        };
        const auto compareStock = [&](const Pair& paired, const MWWorld::Ptr& item,
                                      const MWWorld::ContainerStore& liveDestination,
                                      const MWWorld::ContainerStoreAddContext& sourceContext,
                                      const MWWorld::ContainerStoreAddContext& destinationContext,
                                      const std::vector<MWWorld::Ptr>& rawDestination = {},
                                      const std::vector<MWWorld::Ptr>& rawSource = {}) {
            // Separate stores, owners, registry and script list: real stock mutation
            // must not disturb the live preparation snapshots, even their ID counter.
            MWWorld::ESMStore registryStore;
            ESM::ReadersCache readers;
            MWWorld::WorldModel model(registryStore, readers, 1);
            MWWorld::ManualRef sourceOwner(store, sourceContext.mContainer.getCellRef().getRefId());
            MWWorld::ManualRef destinationOwner(store, destinationContext.mContainer.getCellRef().getRefId());
            auto sourcePtr = sourceOwner.getPtr();
            auto destinationPtr = destinationOwner.getPtr();
            sourcePtr.mCell = sourceContext.mContainer.mCell;
            destinationPtr.mCell = destinationContext.mContainer.mCell;
            MWWorld::ContainerStore source, destination;
            bindEmptyStore(source, sourcePtr, model);
            bindEmptyStore(destination, destinationPtr, model);
            MWWorld::LocalScripts stockScripts(store);
            int notifications = 0;
            MWWorld::ContainerStoreAddContext sourceAdd{ store, model,
                sourceContext.mPlayer.isEmpty()                         ? MWWorld::Ptr()
                    : sourceContext.mPlayer == sourceContext.mContainer ? sourcePtr
                                                                        : destinationPtr,
                sourcePtr, &stockScripts, &scripts, [&](const MWWorld::Ptr&) { ++notifications; } };
            MWWorld::ContainerStoreAddContext destinationAdd{ store, model,
                destinationContext.mPlayer.isEmpty()                              ? MWWorld::Ptr()
                    : destinationContext.mPlayer == destinationContext.mContainer ? destinationPtr
                                                                                  : sourcePtr,
                destinationPtr, &stockScripts, &scripts, sourceAdd.mInventoryUpdated };
            const bool scriptedItem = !item.getClass().getScript(item).empty();
            auto stockSource = source.end();
            std::map<ESM::RefNum, ESM::RefNum> sourceIdentities, destinationIdentities;
            std::vector<MWWorld::Ptr> stockDormant;
            const auto clone
                = [&](const MWWorld::ContainerStore& original, MWWorld::ContainerStore& copiedStore,
                      const MWWorld::ContainerStoreAddContext& add, std::map<ESM::RefNum, ESM::RefNum>& identities,
                      const std::vector<MWWorld::Ptr>& raw) {
                      const auto append = [&](const MWWorld::ConstPtr& member) {
                          // Distinct seed souls preserve separate compatible and dormant nodes.
                          MWWorld::ManualRef seed(store, member.getCellRef().getRefId());
                          seed.getPtr().getCellRef().setSoul(
                              ESM::RefId::stringRefId("stock_clone_" + std::to_string(identities.size())));
                          const auto copied = copiedStore.add(seed.getPtr(), 1, add);
                          copyValues(*copied, member);
                          if (member.getRefData().getBaseNode())
                              copied->getRefData().setBaseNode(new SceneUtil::PositionAttitudeTransform);
                          identities.emplace(copied->getCellRef().getRefNum(), member.getCellRef().getRefNum());
                          if (member == item)
                              stockSource = copied;
                          if (original.getSelectedEnchantItem() != original.end()
                              && *original.getSelectedEnchantItem() == member)
                              copiedStore.setSelectedEnchantItem(copied);
                          if (!member.getCellRef().getCount(false))
                              stockDormant.push_back(*copied);
                          // Clone the actual registration presence/cell, independent of
                          // the context chosen for this next addition's player semantics.
                          const auto registration = localScripts.prepareRemove(&member.getCellRef());
                          stockScripts.remove(*copied);
                          if (registration.hasRegistration())
                          {
                              auto ptr = *copied;
                              ptr.mCell = registration.getCell();
                              stockScripts.add(registration.getScript(), ptr, scripts);
                          }
                      };
                      if (raw.empty())
                          for (const auto& member : original)
                              append(member);
                      else
                          for (const auto& member : raw)
                              append(member);
                  };
            clone(*item.mContainerStore, source, sourceAdd, sourceIdentities, rawSource);
            clone(liveDestination, destination, destinationAdd, destinationIdentities, rawDestination);
            require(stockSource != source.end(), "disposable source clone lost transfer item");
            MWWorld::ManualRef incoming(store, item.getCellRef().getRefId());
            copyValues(incoming.getPtr(), item);
            MWWorld::ContainerStoreRemoveContext removal{ model, sourcePtr, stockScripts, sourceAdd.mInventoryUpdated };
            const auto removed = source.remove(*stockSource, paired.getRemoval().getCount(), removal);
            int onPCAddLookups = 0;
            scripts.mBeforeLocals = [&] {
                ++onPCAddLookups;
                // search() resolves through global World even for a resolved
                // store. Observe current members of this disposable store only.
                const auto added = std::find_if(destination.begin(), destination.end(), [&](const auto& candidate) {
                    return !destinationIdentities.contains(candidate.getCellRef().getRefNum());
                });
                require(added != destination.end() && stockScripts.isRunning(scriptId, *added)
                        && flags(added->getRefData()) == (flags(item.getRefData()) & ~7u)
                        && added->getRefData().getLocals().mShorts == item.getRefData().getLocals().mShorts,
                    "stock new-stack copying/registration did not precede OnPCAdd");
            };
            const auto stockResult = destination.add(incoming.getPtr(), removed, destinationAdd);
            scripts.mBeforeLocals = {};
            require(onPCAddLookups == (scriptedItem && destinationAdd.mPlayer == destinationPtr ? 1 : 0),
                "disposable stock made unexpected OnPCAdd lookups");
            const auto result = paired.getDestinationItem();
            const auto sourceResult = paired.getSourceItem();
            require(removed == paired.getRemoval().getCount()
                    && stockSource->getCellRef().getCount(false) == paired.getRemoval().getRemainingCount()
                    && stockSource->getCellRef().getCount(false) == sourceResult.getCellRef().getCount(false)
                    && values(*stockSource) == values(sourceResult)
                    && stockSource->getRefData().matchesContainerTransferState(sourceResult.getRefData())
                    && (destinationIdentities.contains(stockResult->getCellRef().getRefNum())
                               ? destinationIdentities.at(stockResult->getCellRef().getRefNum())
                               : ESM::RefNum())
                        == paired.getStackTarget()
                    && values(*stockResult) == values(result)
                    && stockResult->getRefData().matchesContainerTransferState(result.getRefData())
                    && stockResult->getCellRef().getCount(false) == result.getCellRef().getCount(false)
                    && !stockResult->getRefData().getBaseNode()
                    && notifications == static_cast<int>(sourceIdentities.size() + destinationIdentities.size()) + 2,
                "paired source/destination values differ from stock add/remove in disposable stores");
            const auto inventory = paired.getSourceInventory();
            size_t index = 0;
            for (const auto& member : source)
            {
                require(index < inventory.size(), "prepared source inventory omitted a stock member");
                const auto& view = inventory[index++];
                require(view.mIdentity == sourceIdentities.at(member.getCellRef().getRefNum())
                        && values(view.mItem) == values(member)
                        && view.mItem.getCellRef().getCount(false) == member.getCellRef().getCount(false)
                        && view.mItem.getRefData().matchesContainerTransferState(member.getRefData()),
                    "prepared source inventory order, identity, count or RefData differs from stock removal");
                const auto current = worldModel.getPtr(view.mIdentity);
                const auto originalRegistration = localScripts.prepareRemove(&current.getCellRef());
                const auto stockRegistration = stockScripts.prepareRemove(&member.getCellRef());
                require(stockRegistration.hasRegistration() == originalRegistration.hasRegistration()
                        && (!stockRegistration.hasRegistration()
                            || (stockRegistration.getScript() == originalRegistration.getScript()
                                && stockRegistration.getCell() == originalRegistration.getCell()
                                && stockRegistration.getContainer() == &source)),
                    "stock removal changed a remaining source registration");
            }
            const auto selected = source.getSelectedEnchantItem();
            require(index == inventory.size()
                    && paired.getSourceSelection()
                        == (selected == source.end() ? ESM::RefNum()
                                                     : sourceIdentities.at(selected->getCellRef().getRefNum())),
                "prepared source membership or selection differs from stock removal");
            const auto destinationInventory = paired.getDestinationInventory();
            index = 0;
            for (const auto& member : destination)
            {
                require(index < destinationInventory.size(), "prepared destination omitted a stock member");
                const auto& view = destinationInventory[index++];
                const auto identity = destinationIdentities.find(member.getCellRef().getRefNum());
                require(view.mIdentity == (identity == destinationIdentities.end() ? ESM::RefNum() : identity->second)
                        && values(view.mItem) == values(member)
                        && view.mItem.getCellRef().getCount(false) == member.getCellRef().getCount(false)
                        && view.mItem.getRefData().matchesContainerTransferState(member.getRefData())
                        && (view.mItem == result) == (member == *stockResult),
                    "prepared destination order, identity, values or result association differs from stock add");
                if (member != *stockResult)
                {
                    const auto original = worldModel.getPtr(view.mIdentity);
                    const auto registration = localScripts.prepareRemove(&original.getCellRef());
                    const auto stockRegistration = stockScripts.prepareRemove(&member.getCellRef());
                    require(stockRegistration.hasRegistration() == registration.hasRegistration()
                            && bool(member.getRefData().getBaseNode()) == bool(original.getRefData().getBaseNode())
                            && (!registration.hasRegistration()
                                || (stockRegistration.getScript() == registration.getScript()
                                    && stockRegistration.getCell() == registration.getCell()
                                    && stockRegistration.getContainer() == &destination)),
                        "stock addition changed a remaining destination registration");
                }
            }
            const auto destinationSelected = destination.getSelectedEnchantItem();
            require(index == destinationInventory.size()
                    && paired.getDestinationSelection()
                        == (destinationSelected == destination.end()
                                ? ESM::RefNum()
                                : destinationIdentities.at(destinationSelected->getCellRef().getRefNum())),
                "prepared destination membership or selection differs from stock addition");
            for (const auto& member : stockDormant)
            {
                const auto& identities = member.mContainerStore == &source ? sourceIdentities : destinationIdentities;
                const auto original = worldModel.getPtr(identities.at(member.getCellRef().getRefNum()));
                const auto registration = localScripts.prepareRemove(&original.getCellRef());
                const auto stockRegistration = stockScripts.prepareRemove(&member.getCellRef());
                require(!member.getCellRef().getCount(false) && values(member) == values(original)
                        && member.getRefData().matchesContainerTransferState(original.getRefData())
                        && registration.hasRegistration() == stockRegistration.hasRegistration()
                        && (!registration.hasRegistration()
                            || (registration.getScript() == stockRegistration.getScript()
                                && registration.getCell() == stockRegistration.getCell()
                                && stockRegistration.getContainer() == member.mContainerStore)),
                    "stock transfer changed a dormant node or registration");
            }
            require(stockScripts.isRunning(scriptId, *stockResult) == paired.getScriptAddition().has_value()
                    && (!paired.getScriptAddition()
                        || stockScripts.prepareRemove(&stockResult->getCellRef()).getCell()
                            == paired.getScriptAddition()->mCell)
                    && stockScripts.isRunning(scriptId, *stockSource)
                        == (scriptedItem && paired.getRemoval().getRemainingCount() != 0),
                "paired deferred script intents differ from disposable stock behavior");
        };
        for (const auto& item : live)
        {
            const bool fromA = item.mContainerStore == &a;
            const bool hasScript = !item.getClass().getScript(item).empty();
            auto& source = fromA ? a : b;
            auto& destination = fromA ? b : a;
            const auto& sourceAdd = fromA ? scriptedAddA : scriptedAddB;
            auto context = fromA ? scriptedAddB : scriptedAddA;
            MWWorld::ContainerStoreRemoveContext removalContext{ worldModel, sourceAdd.mContainer, localScripts,
                sourceAdd.mInventoryUpdated };
            const auto target = fromA ? *plainB : *plainA;
            const auto originalSoul = target.getCellRef().getSoul();
            const auto originalCount = item.getCellRef().getCount(false);
            const auto originalSelection = source.getSelectedEnchantItem();
            const auto originalDestinationSelection = destination.getSelectedEnchantItem();
            const auto originalTargetCount = target.getCellRef().getCount(false);
            const auto originalData = item.getRefData().copyForContainerTransfer();
            const auto originalTargetData = target.getRefData().copyForContainerTransfer();
            const osg::ref_ptr<SceneUtil::PositionAttitudeTransform> originalNode = item.getRefData().getBaseNode();
            const osg::ref_ptr<SceneUtil::PositionAttitudeTransform> originalTargetNode
                = target.getRefData().getBaseNode();
            for (const auto& [sourceFlags, destinationFlags] : activationCases)
                for (bool compatible : { false, true })
                    for (int playerMode : { 0, 1, 2 })
                        for (int signedCount : { 4, -4 })
                            for (int destinationCount : { 7, -7 })
                                for (int quantity : { 1, 4 })
                                    for (int selectionMode : { 0, 1, 2 })
                                    {
                                        const auto selectionOther = fromA ? (hasScript ? *plainA : *scriptA)
                                                                          : (hasScript ? *plainB : *scriptB);
                                        source.setSelectedEnchantItem(selectionMode == 0
                                                ? source.end()
                                                : std::find(source.begin(), source.end(),
                                                      selectionMode == 1 ? item : selectionOther));
                                        destination.setSelectedEnchantItem(selectionMode == 0
                                                ? destination.end()
                                                : std::find(destination.begin(), destination.end(),
                                                      selectionMode == 1 ? target : (fromA ? *scriptB : *scriptA)));
                                        item.getCellRef().setCount(signedCount);
                                        target.getCellRef().setCount(destinationCount);
                                        target.getCellRef().setSoul(
                                            compatible ? item.getCellRef().getSoul() : originalSoul);
                                        context.mPlayer = playerMode == 0 ? context.mContainer
                                            : playerMode == 1             ? removalContext.mContainer
                                                                          : MWWorld::Ptr();
                                        setFlags(item.getRefData(), sourceFlags);
                                        setFlags(target.getRefData(), destinationFlags);
                                        const auto before = snapshot();
                                        startScripts();
                                        {
                                            auto initial = source.prepareTransfer(
                                                item, quantity, destination, removalContext, context);
                                            auto paired = std::move(initial);
                                            const auto& removal = paired.getRemoval();
                                            const auto temporary = paired.getItem();
                                            const auto sourceResult = paired.getSourceItem();
                                            auto inventory = paired.getSourceInventory();
                                            require(inventory.size() == static_cast<size_t>(quantity == 4 ? 1 : 2)
                                                    && paired.getSourceSelection()
                                                        == (selectionMode == 0 || (selectionMode == 1 && quantity == 4)
                                                                ? ESM::RefNum()
                                                                : (selectionMode == 1 ? item : selectionOther)
                                                                      .getCellRef()
                                                                      .getRefNum()),
                                                "source result lost full-removal membership or selected/other/no "
                                                "selection");
                                            for (const auto& view : inventory)
                                            {
                                                const auto current = worldModel.getPtr(view.mIdentity);
                                                require(view.mItem.mRef != current.mRef && !view.mItem.mRef->mWorldModel
                                                        && !view.mItem.getCellRef().getRefNum().isSet()
                                                        && !view.mItem.mCell && !view.mItem.mContainerStore
                                                        && !view.mItem.getRefData().getBaseNode()
                                                        && view.mItem.getRefData().matchesContainerTransferState(
                                                            current.getRefData())
                                                        && view.mItem.getRefData()
                                                                .getAnimationState()
                                                                .mScriptedAnims.data()
                                                            != current.getRefData()
                                                                .getAnimationState()
                                                                .mScriptedAnims.data()
                                                        && (!view.mItem.getRefData().getLocals().mShorts.size()
                                                            || (view.mItem.getRefData().getLocals().mShorts.data()
                                                                    != current.getRefData().getLocals().mShorts.data()
                                                                && view.mItem.getRefData().getLocals().mLongs.data()
                                                                    != current.getRefData().getLocals().mLongs.data()
                                                                && view.mItem.getRefData().getLocals().mFloats.data()
                                                                    != current.getRefData()
                                                                        .getLocals()
                                                                        .mFloats.data())),
                                                    "source inventory result retained live identity, scene or buffers");
                                                if (current == item)
                                                    require(view.mItem == sourceResult,
                                                        "source membership copied a second removal value");
                                            }
                                            inventory.clear(); // Returned views cannot edit membership.
                                            const auto expectedRemainder
                                                = signedCount < 0 ? signedCount + quantity : signedCount - quantity;
                                            require(removal.getCount() == quantity
                                                    && removal.getRemainingCount() == expectedRemainder
                                                    && removal.getItemIdentity() == item.getCellRef().getRefNum()
                                                    && (removal.getScriptRemoval() != nullptr)
                                                        == (hasScript && quantity == 4)
                                                    && paired.getStackTarget()
                                                        == (compatible && !hasScript ? target.getCellRef().getRefNum()
                                                                                     : ESM::RefNum())
                                                    && paired.getStackCount()
                                                        == (compatible && !hasScript
                                                                ? (target.getCellRef().getCount(false) < 0
                                                                          ? target.getCellRef().getCount(false)
                                                                              - quantity
                                                                          : target.getCellRef().getCount(false)
                                                                              + quantity)
                                                                : quantity)
                                                    && paired.hasRemovalNotification()
                                                    && paired.hasAdditionNotification(),
                                                "paired preparation lost counts, stack selection or deferred "
                                                "notifications");
                                            require(sourceResult.getCellRef().getCount(false) == expectedRemainder
                                                    && values(sourceResult) == values(item)
                                                    && sourceResult.getRefData().matchesContainerTransferState(
                                                        item.getRefData())
                                                    && sourceResult.mRef != item.mRef && sourceResult != temporary
                                                    && !sourceResult.getCellRef().getRefNum().isSet()
                                                    && !sourceResult.mRef->mWorldModel && !sourceResult.mCell
                                                    && !sourceResult.mContainerStore
                                                    && !sourceResult.getRefData().getBaseNode()
                                                    && !localScripts.prepareRemove(&sourceResult.getCellRef())
                                                        .hasRegistration(),
                                                "paired source result lost count, source values or detachment");
                                            for (const auto& other : { MWWorld::ConstPtr(item), temporary })
                                            {
                                                const auto& data = sourceResult.getRefData();
                                                const auto& otherData = other.getRefData();
                                                require(data.getAnimationState().mScriptedAnims.data()
                                                            != otherData.getAnimationState().mScriptedAnims.data()
                                                        && (!hasScript
                                                            || (data.getLocals().mShorts.data()
                                                                    != otherData.getLocals().mShorts.data()
                                                                && data.getLocals().mLongs.data()
                                                                    != otherData.getLocals().mLongs.data()
                                                                && data.getLocals().mFloats.data()
                                                                    != otherData.getLocals().mFloats.data())),
                                                    "paired source result aliases live or incoming buffers");
                                            }
                                            auto expected = source.prepareTransferItem(item, quantity, destination,
                                                removalContext.mContainer, context.mContainer, worldModel);
                                            const MWWorld::Ptr independentItem(expected.get());
                                            int onPCAddLookups = 0;
                                            scripts.mBeforeLocals = [&] {
                                                ++onPCAddLookups;
                                                require(flags(independentItem.getRefData()) == (sourceFlags & ~7u)
                                                        && independentItem.getRefData().getLocals().mShorts
                                                            == item.getRefData().getLocals().mShorts
                                                        && !localScripts.isRunning(scriptId, independentItem)
                                                        && snapshot() == before,
                                                    "new-stack flags/registration preparation did not precede OnPCAdd");
                                            };
                                            auto independent
                                                = destination.prepareTransferAdd(std::move(expected), context);
                                            scripts.mBeforeLocals = {};
                                            require(onPCAddLookups == (hasScript && playerMode == 0 ? 1 : 0),
                                                "preparation performed unexpected script service calls");
                                            require(
                                                values(temporary) == values(MWWorld::ConstPtr(independent.mItem.get()))
                                                    && temporary.getCellRef().getCount(false) == quantity
                                                    && !temporary.getCellRef().getRefNum().isSet()
                                                    && !temporary.mRef->mWorldModel && !temporary.mCell
                                                    && !temporary.mContainerStore
                                                    && !temporary.getRefData().getBaseNode()
                                                    && !localScripts.isRunning(
                                                        scriptId, MWWorld::Ptr(independent.mItem.get())),
                                                "paired value differs from shared detached/stock normalization rules");
                                            const auto result = paired.getDestinationItem();
                                            const bool stacked = compatible && !hasScript;
                                            auto destinationInventory = paired.getDestinationInventory();
                                            require(destinationInventory.size() == (stacked ? 2u : 3u)
                                                    && paired.getDestinationSelection()
                                                        == (selectionMode == 0
                                                                ? ESM::RefNum()
                                                                : (selectionMode == 1 ? target
                                                                                      : (fromA ? *scriptB : *scriptA))
                                                                      .getCellRef()
                                                                      .getRefNum()),
                                                "destination result lost existing/new membership or original "
                                                "selection");
                                            for (const auto& view : destinationInventory)
                                            {
                                                const auto original
                                                    = view.mIdentity.isSet() ? worldModel.getPtr(view.mIdentity) : item;
                                                const auto& data = view.mItem.getRefData();
                                                const auto& liveData = original.getRefData();
                                                require(view.mItem.mRef != original.mRef
                                                        && !view.mItem.mRef->mWorldModel
                                                        && !view.mItem.getCellRef().getRefNum().isSet()
                                                        && !view.mItem.mCell && !view.mItem.mContainerStore
                                                        && !data.getBaseNode() && !data.getLuaScripts()
                                                        && !data.getCustomData()
                                                        && !localScripts.prepareRemove(&view.mItem.getCellRef())
                                                            .hasRegistration()
                                                        && data.getAnimationState().mScriptedAnims.data()
                                                            != liveData.getAnimationState().mScriptedAnims.data()
                                                        && (data.getLocals().mShorts.empty()
                                                            || (data.getLocals().mShorts.data()
                                                                    != liveData.getLocals().mShorts.data()
                                                                && data.getLocals().mLongs.data()
                                                                    != liveData.getLocals().mLongs.data()
                                                                && data.getLocals().mFloats.data()
                                                                    != liveData.getLocals().mFloats.data())),
                                                    "destination inventory retained live identity, registration, scene "
                                                    "or buffers");
                                                if (!view.mIdentity.isSet()
                                                    || view.mIdentity == paired.getStackTarget())
                                                    require(view.mItem == result,
                                                        "destination membership duplicated the added value");
                                            }
                                            destinationInventory.clear();
                                            require(flags(sourceResult.getRefData()) == sourceFlags
                                                    && flags(temporary.getRefData())
                                                        == (stacked ? sourceFlags : sourceFlags & ~7u)
                                                    && flags(result.getRefData())
                                                        == (stacked ? destinationFlags : sourceFlags & ~7u),
                                                "paired source/incoming/destination activation flags differ from "
                                                "stock");
                                            require(result.getCellRef().getCount(false) == paired.getStackCount()
                                                    && !result.getCellRef().getRefNum().isSet()
                                                    && !result.mRef->mWorldModel && !result.mCell
                                                    && !result.mContainerStore && !result.getRefData().getBaseNode()
                                                    && (result == temporary) == !stacked,
                                                "paired destination result lost owned/detached state or new-stack "
                                                "identity");
                                            if (stacked)
                                                require(result.mRef != target.mRef
                                                        && result.getRefData().matchesContainerTransferState(
                                                            target.getRefData())
                                                        && !result.getRefData().matchesContainerTransferState(
                                                            item.getRefData())
                                                        && result.getRefData().getAnimationState().mScriptedAnims.data()
                                                            != target.getRefData()
                                                                .getAnimationState()
                                                                .mScriptedAnims.data()
                                                        && result.getRefData().getAnimationState().mScriptedAnims.data()
                                                            != temporary.getRefData()
                                                                .getAnimationState()
                                                                .mScriptedAnims.data(),
                                                    "compatible stack lost distinct destination RefData or aliases "
                                                    "owned "
                                                    "buffers");
                                            compareStock(paired, item, destination, sourceAdd, context);
                                            auto registration = paired.getScriptAddition();
                                            require(registration.has_value() == hasScript
                                                    && (!registration
                                                        || (registration->mScript == scriptId
                                                            && registration->mCell
                                                                == (playerMode == 0 ? nullptr
                                                                                    : context.mContainer.mCell))),
                                                "paired destination script/OnPCAdd context was lost");
                                            if (registration)
                                                registration->mScript
                                                    = {}; // A returned value cannot change the owned intent.
                                            scripts.mBeforeLocals
                                                = [] { throw std::runtime_error("validation called script services"); };
                                            source.validateTransfer(paired, destination, removalContext, context);
                                            scripts.mBeforeLocals = {};
                                            if (!stacked)
                                            {
                                                auto& proposed = const_cast<MWWorld::RefData&>(result.getRefData());
                                                const auto saved = proposed.copyForContainerTransfer();
                                                for (unsigned int bit : { 1u, 2u, 4u })
                                                {
                                                    setFlags(proposed, (sourceFlags & ~7u) | bit);
                                                    bool rejected = false;
                                                    try
                                                    {
                                                        source.validateTransfer(
                                                            paired, destination, removalContext, context);
                                                    }
                                                    catch (const std::invalid_argument& error)
                                                    {
                                                        rejected
                                                            = std::string_view(error.what()).find("item values changed")
                                                            != std::string_view::npos;
                                                    }
                                                    require(rejected, "corrupted new-stack activation flag validated");
                                                    proposed = saved.copyForContainerTransfer();
                                                }
                                            }
                                            bool movedRejected = false;
                                            try
                                            {
                                                source.validateTransfer(initial, destination, removalContext, context);
                                            }
                                            catch (const std::invalid_argument&)
                                            {
                                                movedRejected = true;
                                            }
                                            require(movedRejected, "moved-from pair validated");
                                            // Move assignment discards the previous entire decision too.
                                            auto assigned = source.prepareTransfer(
                                                item, quantity, destination, removalContext, context);
                                            int replacedSourceAlive = 0;
                                            int replacedRemainingAlive = 0;
                                            int replacedDestinationAlive = 0;
                                            const_cast<MWWorld::RefData&>(assigned.getSourceItem().getRefData())
                                                .setCustomData(std::make_unique<Lifetime>(replacedSourceAlive));
                                            for (const auto& view : assigned.getSourceInventory())
                                                if (view.mIdentity != item.getCellRef().getRefNum())
                                                    const_cast<MWWorld::RefData&>(view.mItem.getRefData())
                                                        .setCustomData(
                                                            std::make_unique<Lifetime>(replacedRemainingAlive));
                                            for (const auto& view : assigned.getDestinationInventory())
                                                const_cast<MWWorld::RefData&>(view.mItem.getRefData())
                                                    .setCustomData(
                                                        std::make_unique<Lifetime>(replacedDestinationAlive));
                                            assigned = std::move(paired);
                                            require(replacedSourceAlive == 0 && replacedRemainingAlive == 0
                                                    && replacedDestinationAlive == 0
                                                    && assigned.getSourceItem() == sourceResult
                                                    && assigned.getDestinationItem() == result,
                                                "move assignment leaked a prior inventory result or lost the new one");
                                            source.validateTransfer(assigned, destination, removalContext, context);
                                            for (const auto& access :
                                                std::vector<std::function<void()>>{ [&] { paired.getSourceItem(); },
                                                    [&] { paired.getSourceInventory(); },
                                                    [&] { paired.getSourceSelection(); },
                                                    [&] { paired.getDestinationInventory(); },
                                                    [&] { paired.getDestinationSelection(); },
                                                    [&] { paired.getDestinationItem(); } })
                                            {
                                                movedRejected = false;
                                                try
                                                {
                                                    access();
                                                }
                                                catch (const std::invalid_argument&)
                                                {
                                                    movedRejected = true;
                                                }
                                                require(movedRejected, "moved-from pair exposed an inventory result");
                                            }
                                        }
                                        // Failure after all values and add intents exist must
                                        // discard flag normalization together with the pair.
                                        auto failingRemoval = removalContext;
                                        int intentAlive = 0, emitted = 0, copies = 0;
                                        std::function<void()> onCopy;
                                        failingRemoval.mInventoryUpdated
                                            = NotificationIntent(intentAlive, onCopy, emitted);
                                        onCopy = [&] {
                                            ++copies;
                                            require(snapshot() == before,
                                                "late preparation consumed live activation flags");
                                            throw PreparationFailure{};
                                        };
                                        bool failed = false;
                                        try
                                        {
                                            source.prepareTransfer(
                                                item, quantity, destination, failingRemoval, context);
                                        }
                                        catch (const PreparationFailure&)
                                        {
                                            failed = true;
                                        }
                                        onCopy = {};
                                        failingRemoval.mInventoryUpdated = {};
                                        require(failed && copies == 1 && intentAlive == 0 && emitted == 0,
                                            "activation preparation failure leaked an intent or emitted success");
                                        require(snapshot() == before,
                                            "paired preparation/validation/move/discard changed live state");
                                        unchangedScripts();
                                    }
            source.setSelectedEnchantItem(originalSelection);
            destination.setSelectedEnchantItem(originalDestinationSelection);
            item.getRefData() = originalData.copyForContainerTransfer();
            item.getRefData().setBaseNode(originalNode);
            target.getRefData() = originalTargetData.copyForContainerTransfer();
            target.getRefData().setBaseNode(originalTargetNode);
            item.getCellRef().setCount(originalCount);
            target.getCellRef().setSoul(originalSoul);
            target.getCellRef().setCount(originalTargetCount);
            context.mPlayer = (fromA ? scriptedAddB : scriptedAddA).mPlayer;
            if (!hasScript)
                target.getCellRef().setSoul(item.getCellRef().getSoul());
            const auto makePair = [&] { return source.prepareTransfer(item, 1, destination, removalContext, context); };
            const auto validatePair
                = [&](const Pair& paired) { source.validateTransfer(paired, destination, removalContext, context); };
            for (const auto& [sourceFlags, destinationFlags] : activationCases)
            {
                setFlags(item.getRefData(), sourceFlags);
                auto decision = makePair();
                const unsigned int changedFlags = sourceFlags & 7u ? sourceFlags & ~7u : sourceFlags | 1u;
                auto& sourceResult = const_cast<MWWorld::RefData&>(decision.getSourceItem().getRefData());
                setFlags(sourceResult, changedFlags);
                reject([&] { validatePair(decision); }, "source item values changed");
                setFlags(sourceResult, sourceFlags);
                validatePair(decision);
                // Even changing the live source to new-stack flag semantics must
                // reject against the original witness, not the proposed value.
                setFlags(item.getRefData(), changedFlags);
                reject([&] { validatePair(decision); }, "source state changed");
                setFlags(item.getRefData(), sourceFlags);
                validatePair(decision);
            }
            item.getRefData() = originalData.copyForContainerTransfer();
            item.getRefData().setBaseNode(originalNode);
            for (int signedCount : { 4, -4 })
                for (int quantity : { 1, 4 })
                {
                    item.getCellRef().setCount(signedCount);
                    auto decision = source.prepareTransfer(item, quantity, destination, removalContext, context);
                    auto* result = const_cast<MWWorld::LiveCellRef<ESM::Miscellaneous>*>(
                        decision.getSourceItem().get<ESM::Miscellaneous>());
                    const auto savedRef = result->mRef;
                    const auto savedData = result->mData.copyForContainerTransfer();
                    for (const auto& change : std::vector<std::function<void(MWWorld::CellRef&)>>{
                             [=](auto& ref) { ref.setCount(signedCount); },
                             [=](auto& ref) { ref.setCount(-signedCount); }, [](auto& ref) { ref.setCharge(999); },
                             [](auto& ref) { ref.setOwner({}); }, [](auto& ref) { ref.setPosition({}); },
                             [&](auto& ref) { ref.setRefNum(item.getCellRef().getRefNum()); } })
                    {
                        change(result->mRef);
                        reject([&] { validatePair(decision); }, "source item values changed");
                        result->mRef = savedRef;
                    }
                    for (const auto& change :
                        std::vector<std::function<void(MWWorld::RefData&)>>{ [](auto& data) { data.setPosition({}); },
                            [](auto& data) { data.enable(); }, [](auto& data) { data.mPhysicsPostponed = false; },
                            [](auto& data) { data.getAnimationState().mScriptedAnims[0].mTime += 1; },
                            [](auto& data) { data.activateByScript(); },
                            [](auto& data) { data.getLocals().mLongs.push_back(42); },
                            [](auto& data) { data.setBaseNode(new SceneUtil::PositionAttitudeTransform); } })
                    {
                        change(result->mData);
                        reject([&] { validatePair(decision); }, "source item values changed");
                        result->mData = savedData.copyForContainerTransfer();
                    }
                    const auto* base = result->mBase;
                    result->mBase = (hasScript ? plain : scripted).getPtr().get<ESM::Miscellaneous>()->mBase;
                    reject([&] { validatePair(decision); }, "source item values changed");
                    result->mBase = base;
                    result->mWorldModel = &worldModel;
                    reject([&] { validatePair(decision); }, "source item values changed");
                    result->mWorldModel = nullptr;
                    int alive = 0;
                    result->mData.setCustomData(std::make_unique<Lifetime>(alive));
                    reject([&] { validatePair(decision); }, "source item values changed");
                    result->mData.setCustomData(nullptr);
                    require(alive == 0, "source corruption marker leaked");
                    validatePair(decision);

                    // A live source changed to the proposed value must still fail:
                    // the original witness cannot double as the post-removal result.
                    const auto liveRef = item.getCellRef();
                    item.getCellRef() = savedRef;
                    item.getCellRef().setRefNum(liveRef.getRefNum());
                    reject([&] { validatePair(decision); }, "source state changed");
                    item.getCellRef() = liveRef;
                    for (int staleCount : { -signedCount, signedCount + (signedCount > 0 ? 1 : -1) })
                    {
                        item.getCellRef().setCount(staleCount);
                        reject([&] { validatePair(decision); }, "source state changed");
                        item.getCellRef() = liveRef;
                    }
                    MWWorld::ManualRef replacement(store, item.getCellRef().getRefId());
                    replacement.getPtr().getCellRef().setRefNum(item.getCellRef().getRefNum());
                    auto replacementPtr = replacement.getPtr();
                    replacementPtr.mContainerStore = &source;
                    worldModel.registerPtr(replacementPtr);
                    reject([&] { validatePair(decision); }, "source state changed");
                    worldModel.registerPtr(item);
                    validatePair(decision);
                }
            item.getCellRef().setCount(originalCount);
            const auto remainingItem = fromA ? (hasScript ? *plainA : *scriptA) : (hasScript ? *plainB : *scriptB);
            for (int quantity : { 1, std::abs(originalCount) })
            {
                for (const auto& selected : { source.end(), std::find(source.begin(), source.end(), item),
                         std::find(source.begin(), source.end(), remainingItem) })
                {
                    source.setSelectedEnchantItem(selected);
                    auto decision = source.prepareTransfer(item, quantity, destination, removalContext, context);
                    for (const auto& changed : { source.end(), std::find(source.begin(), source.end(), item),
                             std::find(source.begin(), source.end(), remainingItem) })
                        if (changed != selected)
                        {
                            source.setSelectedEnchantItem(changed);
                            reject([&] { validatePair(decision); }, "selection changed");
                            source.setSelectedEnchantItem(selected);
                            validatePair(decision);
                        }
                }
                source.setSelectedEnchantItem(originalSelection);
                auto decision = source.prepareTransfer(item, quantity, destination, removalContext, context);
                auto views = decision.getSourceInventory();
                const auto remaining = std::find_if(views.begin(), views.end(),
                    [&](const auto& view) { return view.mIdentity == remainingItem.getCellRef().getRefNum(); });
                require(remaining != views.end(), "source result omitted unchanged member");
                auto* result
                    = const_cast<MWWorld::LiveCellRef<ESM::Miscellaneous>*>(remaining->mItem.get<ESM::Miscellaneous>());
                remaining->mIdentity = {}; // Association is returned by value.
                remaining->mItem = {};
                validatePair(decision);
                const auto ref = result->mRef;
                for (const auto& change : std::vector<std::function<void(MWWorld::CellRef&)>>{
                         [](auto& value) { value = value.copyWithCount(0); },
                         [](auto& value) { value.setCount(-value.getCount(false)); },
                         [](auto& value) { value.setCharge(999); }, [](auto& value) { value.setOwner({}); },
                         [](auto& value) { value.setPosition({}); },
                         [&](auto& value) { value.setRefNum(remainingItem.getCellRef().getRefNum()); } })
                {
                    change(result->mRef);
                    reject([&] { validatePair(decision); }, "source inventory result changed");
                    result->mRef = ref;
                }
                const auto data = result->mData.copyForContainerTransfer();
                for (const auto& change : std::vector<std::function<void(MWWorld::RefData&)>>{
                         [](auto& value) { value.getAnimationState().mScriptedAnims[0].mTime += 1; },
                         [](auto& value) { value.getLocals().mLongs.push_back(42); },
                         [](auto& value) { value.enable(); }, [](auto& value) { value.setPosition({}); },
                         [&](auto& value) {
                             const auto bits = flags(value);
                             setFlags(value, bits & 7u ? bits & ~7u : bits | 1u);
                         },
                         [](auto& value) { value.setBaseNode(new SceneUtil::PositionAttitudeTransform); } })
                {
                    change(result->mData);
                    reject([&] { validatePair(decision); }, "source inventory result changed");
                    result->mData = data.copyForContainerTransfer();
                }
                result->mWorldModel = &worldModel;
                reject([&] { validatePair(decision); }, "source inventory result changed");
                result->mWorldModel = nullptr;
                validatePair(decision);
            }
            for (const auto& selected : { destination.end(), std::find(destination.begin(), destination.end(), target),
                     std::find(destination.begin(), destination.end(), fromA ? *scriptB : *scriptA) })
            {
                destination.setSelectedEnchantItem(selected);
                auto decision = makePair();
                for (const auto& changed :
                    { destination.end(), std::find(destination.begin(), destination.end(), target),
                        std::find(destination.begin(), destination.end(), fromA ? *scriptB : *scriptA) })
                    if (changed != selected)
                    {
                        destination.setSelectedEnchantItem(changed);
                        reject([&] { validatePair(decision); }, "selection changed");
                        destination.setSelectedEnchantItem(selected);
                        validatePair(decision);
                    }
            }
            destination.setSelectedEnchantItem(originalDestinationSelection);
            for (int quantity : { 1, std::abs(originalCount) })
            {
                auto decision = source.prepareTransfer(item, quantity, destination, removalContext, context);
                for (auto view : decision.getDestinationInventory())
                {
                    if (view.mItem == decision.getDestinationItem())
                        continue; // Addition result corruption is covered separately.
                    auto* result
                        = const_cast<MWWorld::LiveCellRef<ESM::Miscellaneous>*>(view.mItem.get<ESM::Miscellaneous>());
                    const auto original = worldModel.getPtr(view.mIdentity);
                    view.mIdentity = {};
                    view.mItem = {}; // Returned membership/associations cannot edit the decision.
                    validatePair(decision);
                    const auto ref = result->mRef;
                    const auto data = result->mData.copyForContainerTransfer();
                    for (const auto& change : std::vector<std::function<void(MWWorld::CellRef&)>>{
                             [](auto& value) { value = value.copyWithCount(0); },
                             [](auto& value) { value.setCount(-value.getCount(false)); },
                             [](auto& value) { value.setCharge(999); }, [](auto& value) { value.setOwner({}); },
                             [](auto& value) { value.setPosition({}); },
                             [&](auto& value) { value.setRefNum(original.getCellRef().getRefNum()); } })
                    {
                        change(result->mRef);
                        reject([&] { validatePair(decision); }, "destination inventory result changed");
                        result->mRef = ref;
                    }
                    for (const auto& change : std::vector<std::function<void(MWWorld::RefData&)>>{
                             [](auto& value) { value.getAnimationState().mScriptedAnims[0].mTime += 1; },
                             [](auto& value) { value.getLocals().mLongs.push_back(42); },
                             [](auto& value) { value.enable(); }, [](auto& value) { value.setPosition({}); },
                             [&](auto& value) {
                                 const auto bits = flags(value);
                                 setFlags(value, bits & 7u ? bits & ~7u : bits | 1u);
                             },
                             [](auto& value) { value.setBaseNode(new SceneUtil::PositionAttitudeTransform); } })
                    {
                        change(result->mData);
                        reject([&] { validatePair(decision); }, "destination inventory result changed");
                        result->mData = data.copyForContainerTransfer();
                    }
                    result->mWorldModel = &worldModel;
                    reject([&] { validatePair(decision); }, "destination inventory result changed");
                    result->mWorldModel = nullptr;
                    int alive = 0;
                    result->mData.setCustomData(std::make_unique<Lifetime>(alive));
                    reject([&] { validatePair(decision); }, "destination inventory result changed");
                    result->mData.setCustomData(nullptr);
                    require(alive == 0, "remaining destination corruption marker leaked");
                    validatePair(decision);
                }
            }
            for (const auto& changedItem : { item, remainingItem, target, fromA ? *scriptB : *scriptA })
            {
                auto paired = makePair();
                const auto ref = changedItem.getCellRef();
                changedItem.getCellRef().setCharge(9);
                reject([&] { validatePair(paired); }, "changed");
                changedItem.getCellRef() = ref;
                changedItem.getCellRef().setOwner({});
                reject([&] { validatePair(paired); }, "changed");
                changedItem.getCellRef() = ref;
                auto& locals = changedItem.getRefData().getLocals();
                locals.mLongs.push_back(99);
                reject([&] { validatePair(paired); }, "changed");
                locals.mLongs.pop_back();
                auto wrongStore = changedItem;
                wrongStore.mContainerStore = changedItem.mContainerStore == &a ? &b : &a;
                worldModel.registerPtr(wrongStore);
                reject([&] { validatePair(paired); }, "changed");
                worldModel.registerPtr(changedItem);
                if (!changedItem.getClass().getScript(changedItem).empty())
                {
                    auto registered = changedItem;
                    registered.mCell = localScripts.prepareRemove(&changedItem.getCellRef()).getCell();
                    localScripts.remove(changedItem);
                    reject([&] { validatePair(paired); }, "registration changed");
                    auto absent = makePair();
                    localScripts.add(scriptId, registered, scripts);
                    reject([&] { validatePair(paired); }, "registration changed");
                    reject([&] { validatePair(absent); }, "registration changed");
                }
            }
            auto paired = makePair();
            const auto remainingRef = remainingItem.getCellRef();
            remainingItem.getCellRef() = remainingRef.copyWithCount(0);
            reject([&] { validatePair(paired); }, "changed");
            remainingItem.getCellRef() = remainingRef;
            int remainingAlive = 0;
            remainingItem.getRefData().setCustomData(std::make_unique<Lifetime>(remainingAlive));
            reject([&] { makePair(); }, "Lua or custom state");
            require(remainingAlive == 1, "failed source inventory preparation cloned/destroyed remaining custom state");
            remainingItem.getRefData().setCustomData(nullptr);
            validatePair(paired);
            for (int change : { 0, 1, 2, 3 })
            {
                auto staleContext = context;
                if (change == 0)
                    staleContext.mPlayer = context.mPlayer.isEmpty() ? context.mContainer : MWWorld::Ptr();
                if (change == 1)
                    staleContext.mContainer.mCell = removalContext.mContainer.mCell;
                if (change == 2)
                    staleContext.mLocalScripts = nullptr;
                if (change == 3)
                    staleContext.mScriptManager = nullptr;
                reject([&] { source.validateTransfer(paired, destination, removalContext, staleContext); }, "changed");
            }
            MWWorld::LocalScripts otherScripts(store);
            auto staleRemoval = removalContext;
            staleRemoval.mContainer = context.mContainer;
            reject([&] { source.validateTransfer(paired, destination, staleRemoval, context); }, "changed");
            MWWorld::ContainerStoreRemoveContext otherRemoval{ worldModel, removalContext.mContainer, otherScripts,
                removalContext.mInventoryUpdated };
            reject([&] { source.validateTransfer(paired, destination, otherRemoval, context); }, "changed");
            for (auto* container : { &source, &destination })
            {
                const auto owner = container->getPtr(worldModel);
                container->setPtr(
                    owner == removalContext.mContainer ? context.mContainer : removalContext.mContainer, worldModel);
                reject([&] { validatePair(paired); }, "changed");
                container->setPtr(owner, worldModel);
                auto movedOwner = owner;
                movedOwner.mCell = nullptr;
                worldModel.registerPtr(movedOwner);
                reject([&] { validatePair(paired); }, "changed");
                worldModel.registerPtr(owner);
                MWWorld::ManualRef replacementOwner(store, owner.getCellRef().getRefId());
                replacementOwner.getPtr().getCellRef().setRefNum(owner.getCellRef().getRefNum());
                worldModel.registerPtr(replacementOwner.getPtr());
                reject([&] { validatePair(paired); }, "changed");
                worldModel.registerPtr(owner);
                auto* listener = container->getContListener();
                container->setContListener(nullptr);
                reject([&] { validatePair(paired); }, "changed");
                container->setContListener(listener);
            }
            // Deliberate internal-corruption fault injection; the public view is
            // const. Full prepared values, not just stack inputs, must be checked.
            auto* temporary
                = const_cast<MWWorld::LiveCellRef<ESM::Miscellaneous>*>(paired.getItem().get<ESM::Miscellaneous>());
            const auto preparedRef = temporary->mRef;
            temporary->mRef.setCharge(999);
            reject([&] { validatePair(paired); }, "item values changed");
            temporary->mRef = preparedRef;
            temporary->mData.getLocals().mFloats.push_back(42);
            reject([&] { validatePair(paired); }, "item values changed");
            temporary->mData.getLocals().mFloats.pop_back();
            validatePair(paired);
            if (!hasScript)
            {
                require(paired.getStackTarget() == target.getCellRef().getRefNum(),
                    "destination fault fixture did not select the existing stack");
                auto* result = const_cast<MWWorld::LiveCellRef<ESM::Miscellaneous>*>(
                    paired.getDestinationItem().get<ESM::Miscellaneous>());
                const auto savedResultRef = result->mRef;
                const auto savedResultData = result->mData.copyForContainerTransfer();
                for (const auto& change : std::vector<std::function<void(MWWorld::CellRef&)>>{
                         [](auto& ref) { ref.setCount(-999); }, [](auto& ref) { ref.setCharge(999); },
                         [](auto& ref) { ref.setOwner(ESM::RefId::stringRefId("corrupt_owner")); },
                         [&](auto& ref) { ref.setRefNum(target.getCellRef().getRefNum()); } })
                {
                    change(result->mRef);
                    reject([&] { validatePair(paired); }, "destination item values changed");
                    result->mRef = savedResultRef;
                }
                for (const auto& change :
                    std::vector<std::function<void(MWWorld::RefData&)>>{ [](auto& data) { data.setPosition({}); },
                        [](auto& data) { data.enable(); }, [](auto& data) { data.mPhysicsPostponed = false; },
                        [](auto& data) { data.getAnimationState().mScriptedAnims[0].mTime += 1; },
                        [](auto& data) { data.activateByScript(); },
                        [](auto& data) { data.getLocals().mLongs.push_back(42); },
                        [](auto& data) { data.setBaseNode(new SceneUtil::PositionAttitudeTransform); } })
                {
                    change(result->mData);
                    reject([&] { validatePair(paired); }, "destination item values changed");
                    result->mData = savedResultData.copyForContainerTransfer();
                }
                // Replacing the result with the normalized incoming state would
                // preserve compatibility but lose the destination's applicable state.
                result->mData = temporary->mData.copyForContainerTransfer();
                reject([&] { validatePair(paired); }, "destination item values changed");
                result->mData = savedResultData.copyForContainerTransfer();
                validatePair(paired);

                const auto targetRef = target.getCellRef();
                for (int staleCount : { 0, -originalTargetCount, originalTargetCount + 1 })
                {
                    target.getCellRef().setCount(staleCount, localScripts);
                    reject([&] { validatePair(paired); }, "changed");
                    target.getCellRef() = targetRef;
                }
                const auto targetData = target.getRefData().copyForContainerTransfer();
                target.getRefData().getAnimationState().mScriptedAnims[0].mTime += 1;
                reject([&] { validatePair(paired); }, "destination values changed");
                target.getRefData().getAnimationState().mScriptedAnims[0].mTime
                    = targetData.getAnimationState().mScriptedAnims[0].mTime;
                MWWorld::ManualRef replacement(store, target.getCellRef().getRefId());
                replacement.getPtr().getCellRef().setRefNum(target.getCellRef().getRefNum());
                auto replacementPtr = replacement.getPtr();
                replacementPtr.mContainerStore = &destination;
                worldModel.registerPtr(replacementPtr);
                reject([&] { validatePair(paired); }, "changed");
                worldModel.registerPtr(target);
                int alive = 0;
                target.getRefData().setCustomData(std::make_unique<Lifetime>(alive));
                reject([&] { makePair(); }, "Lua or custom state");
                require(alive == 1, "failed destination copying cloned or destroyed live custom data");
                target.getRefData().setCustomData(nullptr);
                target.getCellRef().setCount(std::numeric_limits<int>::max());
                reject([&] { makePair(); }, "overflow");
                target.getCellRef() = targetRef;
                validatePair(paired);
            }
            auto full = source.prepareTransfer(item, std::abs(originalCount), destination, removalContext, context);
            const auto selection = source.getSelectedEnchantItem();
            source.setSelectedEnchantItem(hasScript ? source.end() : (fromA ? plainA : plainB));
            reject([&] { validatePair(full); }, "selection changed");
            source.setSelectedEnchantItem(selection);
            for (int quantity : { 0, -1, std::abs(originalCount) + 1 })
                reject([&] { source.prepareTransfer(item, quantity, destination, removalContext, context); }, "count");
            MWWorld::ESMStore otherStore;
            ESM::ReadersCache otherReaders;
            MWWorld::WorldModel otherWorld(otherStore, otherReaders, 1);
            MWWorld::ContainerStoreAddContext otherContent{ otherStore, worldModel, context.mPlayer, context.mContainer,
                context.mLocalScripts, context.mScriptManager, context.mInventoryUpdated };
            reject(
                [&] { source.validateTransfer(paired, destination, removalContext, otherContent); }, "context changed");
            reject(
                [&] { source.prepareTransfer(item, 1, destination, removalContext, otherContent); }, "content store");
            MWWorld::ContainerStoreRemoveContext otherModel{ otherWorld, removalContext.mContainer, localScripts,
                removalContext.mInventoryUpdated };
            reject([&] { source.validateTransfer(paired, destination, otherModel, context); }, "context changed");
            reject(
                [&] { source.prepareTransfer(item, 1, destination, otherModel, context); }, "world context mismatch");

            // Validation uses the captured consumers, so replacing callables in
            // caller contexts cannot replace or execute the owned deferred effects.
            auto changedConsumers = context;
            changedConsumers.mInventoryUpdated = [](const MWWorld::Ptr&) { throw PreparationFailure{}; };
            auto changedSourceConsumer = removalContext;
            changedSourceConsumer.mInventoryUpdated = {};
            source.validateTransfer(paired, destination, changedSourceConsumer, changedConsumers);

            // Last consumer copy runs with source/incoming/destination values,
            // registration/OnPCAdd and its notification already prepared.
            for (bool compatible : { false, true })
                for (int quantity : { 1, std::abs(originalCount) })
                    for (bool fail : { false, true })
                    {
                        target.getCellRef().setSoul(compatible ? item.getCellRef().getSoul() : originalSoul);
                        int sourceAlive = 0, destinationAlive = 0, emitted = 0, copies = 0, itemAlive = 0,
                            resultAlive = 0, sourceResultAlive = 0, remainingResultAlive = 0,
                            destinationRemainingAlive = 0;
                        osg::observer_ptr<SceneUtil::PositionAttitudeTransform> node, resultNode, sourceNode,
                            remainingNode;
                        std::vector<osg::observer_ptr<SceneUtil::PositionAttitudeTransform>> destinationNodes;
                        std::function<void()> sourceCopy, destinationCopy;
                        {
                            auto addition = context;
                            addition.mInventoryUpdated = NotificationIntent(destinationAlive, destinationCopy, emitted);
                            MWWorld::ContainerStoreRemoveContext removal{ worldModel, removalContext.mContainer,
                                localScripts, NotificationIntent(sourceAlive, sourceCopy, emitted) };
                            const auto before = snapshot();
                            startScripts();
                            sourceCopy = [&] {
                                ++copies;
                                require(sourceAlive == 2 && destinationAlive == 2 && snapshot() == before,
                                    "paired late failure ran before both owned notification intents");
                                if (fail)
                                    throw PreparationFailure{};
                            };
                            bool caught = false;
                            try
                            {
                                auto decision = source.prepareTransfer(item, quantity, destination, removal, addition);
                                require(decision.getScriptAddition().has_value() == hasScript
                                        && (decision.getRemoval().getScriptRemoval() != nullptr)
                                            == (hasScript && quantity == std::abs(originalCount)),
                                    "paired late failure/discard lost script intents");
                                source.validateTransfer(decision, destination, removal, addition);
                                auto& sourceData = const_cast<MWWorld::RefData&>(decision.getSourceItem().getRefData());
                                sourceData.setCustomData(std::make_unique<Lifetime>(sourceResultAlive));
                                sourceData.setBaseNode(new SceneUtil::PositionAttitudeTransform);
                                sourceNode = sourceData.getBaseNode();
                                require(sourceResultAlive == 1 && sourceNode.valid(),
                                    "paired discard did not own the source result");
                                for (const auto& view : decision.getSourceInventory())
                                    if (view.mIdentity == remainingItem.getCellRef().getRefNum())
                                    {
                                        auto& remaining = const_cast<MWWorld::RefData&>(view.mItem.getRefData());
                                        remaining.setCustomData(std::make_unique<Lifetime>(remainingResultAlive));
                                        remaining.setBaseNode(new SceneUtil::PositionAttitudeTransform);
                                        remainingNode = remaining.getBaseNode();
                                    }
                                require(remainingResultAlive == 1 && remainingNode.valid(),
                                    "paired discard did not own the remaining source values");
                                for (const auto& view : decision.getDestinationInventory())
                                    if (view.mItem != decision.getDestinationItem())
                                    {
                                        auto& remaining = const_cast<MWWorld::RefData&>(view.mItem.getRefData());
                                        remaining.setCustomData(std::make_unique<Lifetime>(destinationRemainingAlive));
                                        remaining.setBaseNode(new SceneUtil::PositionAttitudeTransform);
                                        destinationNodes.emplace_back(remaining.getBaseNode());
                                    }
                                require(destinationRemainingAlive == (decision.getStackTarget().isSet() ? 1 : 2),
                                    "paired discard did not own all remaining destination values");
                                // Observe destruction of the privately owned item after
                                // successful preparation; never install these test markers live.
                                auto& data = const_cast<MWWorld::RefData&>(decision.getItem().getRefData());
                                data.setCustomData(std::make_unique<Lifetime>(itemAlive));
                                data.setBaseNode(new SceneUtil::PositionAttitudeTransform);
                                node = data.getBaseNode();
                                if (decision.getStackTarget().isSet())
                                {
                                    auto& result
                                        = const_cast<MWWorld::RefData&>(decision.getDestinationItem().getRefData());
                                    result.setCustomData(std::make_unique<Lifetime>(resultAlive));
                                    result.setBaseNode(new SceneUtil::PositionAttitudeTransform);
                                    resultNode = result.getBaseNode();
                                    require(itemAlive == 1 && resultAlive == 1 && node.valid() && resultNode.valid(),
                                        "paired discard did not own two separate item values");
                                }
                            }
                            catch (const PreparationFailure&)
                            {
                                caught = true;
                            }
                            sourceCopy = {};
                            require(caught == fail && copies == 1 && sourceAlive == 1 && destinationAlive == 1
                                    && emitted == 0 && itemAlive == 0 && resultAlive == 0 && !node.valid()
                                    && sourceResultAlive == 0 && !sourceNode.valid() && !resultNode.valid()
                                    && remainingResultAlive == 0 && !remainingNode.valid()
                                    && destinationRemainingAlive == 0
                                    && std::none_of(destinationNodes.begin(), destinationNodes.end(),
                                        [](const auto& observed) { return observed.valid(); })
                                    && snapshot() == before,
                                "paired late failure/discard leaked owned state or changed live state/notifications");
                            unchangedScripts();
                        }
                        require(sourceAlive == 0 && destinationAlive == 0,
                            "paired notification intents survived their contexts");
                    }
            target.getCellRef().setSoul(originalSoul);
        }

        // Dormant raw nodes are absent from stock iteration but still witnessed.
        // Also compare multiple signed remaining items and an empty full-removal
        // result, with separate stores that can be discarded after stale writes.
        for (bool fromA : { false, true })
            for (bool hasScript : { false, true })
                for (int signedCount : { 4, -4 })
                    for (int quantity : { 1, 4 })
                        // Destination modes: empty membership, new stack, existing stack.
                        for (const auto& [keepRemaining, destinationMode] : std::vector<std::pair<bool, int>>{
                                 { false, 0 }, { false, 1 }, { false, 2 }, { true, 0 }, { true, 1 }, { true, 2 } })
                        {
                            auto sourceAdd = fromA ? scriptedAddA : scriptedAddB;
                            auto destinationAdd = fromA ? scriptedAddB : scriptedAddA;
                            MWWorld::ContainerStore source, destination;
                            bindEmptyStore(source, sourceAdd.mContainer, worldModel);
                            bindEmptyStore(destination, destinationAdd.mContainer, worldModel);
                            observedStores.push_back(&source);
                            observedStores.push_back(&destination);
                            std::vector<MWWorld::Ptr> rawSource;
                            const auto append = [&](const MWWorld::Ptr& original, int count) {
                                MWWorld::ManualRef seed(store, original.getCellRef().getRefId());
                                seed.getPtr().getCellRef().setSoul(
                                    ESM::RefId::stringRefId("source_clone_" + std::to_string(rawSource.size())));
                                auto added = *source.add(seed.getPtr(), 1, sourceAdd);
                                copyValues(added, original);
                                added.getCellRef() = added.getCellRef().copyWithCount(count);
                                rawSource.push_back(added);
                                return added;
                            };
                            const auto dormant = append(fromA ? *scriptA : *scriptB, 0);
                            observedDormant.push_back(dormant);
                            const auto item
                                = append(fromA ? (hasScript ? *scriptA : *plainA) : (hasScript ? *scriptB : *plainB),
                                    signedCount);
                            if (keepRemaining)
                            {
                                append(fromA ? *plainA : *plainB, -3);
                                append(fromA ? *scriptA : *scriptB, -2);
                            }
                            std::vector<MWWorld::Ptr> rawDestination;
                            const auto appendDestination = [&](const MWWorld::Ptr& original, int count) {
                                MWWorld::ManualRef seed(store, original.getCellRef().getRefId());
                                seed.getPtr().getCellRef().setSoul(ESM::RefId::stringRefId(
                                    "destination_clone_" + std::to_string(rawDestination.size())));
                                auto added = *destination.add(seed.getPtr(), 1, destinationAdd);
                                copyValues(added, original);
                                added.getCellRef() = added.getCellRef().copyWithCount(count);
                                rawDestination.push_back(added);
                                return added;
                            };
                            const auto destinationPlain = fromA ? *plainB : *plainA;
                            const auto destinationScript = fromA ? *scriptB : *scriptA;
                            // Dormant compatible nodes must neither stack nor enter membership.
                            const auto dormantTarget = appendDestination(destinationPlain, 0);
                            dormantTarget.getCellRef().setSoul(item.getCellRef().getSoul());
                            const auto dormantScript = appendDestination(destinationScript, 0);
                            observedDormant.push_back(dormantTarget);
                            observedDormant.push_back(dormantScript);
                            auto selectedTarget = destination.end();
                            if (destinationMode)
                            {
                                const auto first = appendDestination(destinationPlain, signedCount > 0 ? 7 : -7);
                                if (destinationMode == 2)
                                    first.getCellRef().setSoul(item.getCellRef().getSoul());
                                // A second compatible stack must survive with its original values.
                                const auto second = appendDestination(destinationPlain, -3);
                                second.getCellRef().setSoul(first.getCellRef().getSoul());
                                const auto other = appendDestination(destinationScript, -2);
                                selectedTarget = std::find(destination.begin(), destination.end(), first);
                                if (keepRemaining)
                                    destination.setSelectedEnchantItem(quantity == 1
                                            ? selectedTarget
                                            : std::find(destination.begin(), destination.end(), other));
                            }
                            source.setSelectedEnchantItem(std::find(source.begin(), source.end(), item));
                            MWWorld::ContainerStoreRemoveContext removal{ worldModel, sourceAdd.mContainer,
                                localScripts, sourceAdd.mInventoryUpdated };
                            const auto before = snapshot();
                            startScripts();
                            {
                                auto decision
                                    = source.prepareTransfer(item, quantity, destination, removal, destinationAdd);
                                compareStock(
                                    decision, item, destination, sourceAdd, destinationAdd, rawDestination, rawSource);
                                const auto views = decision.getSourceInventory();
                                require(views.size() == static_cast<size_t>((keepRemaining ? 2 : 0) + (quantity != 4))
                                        && std::none_of(views.begin(), views.end(),
                                            [&](const auto& view) {
                                                return view.mIdentity == dormant.getCellRef().getRefNum();
                                            }),
                                    "dormant or fully removed node remained in source membership");
                                source.validateTransfer(decision, destination, removal, destinationAdd);
                                const auto destinationViews = decision.getDestinationInventory();
                                const bool stacked = destinationMode == 2 && !hasScript;
                                require(destinationViews.size() == (destinationMode ? (stacked ? 3u : 4u) : 1u)
                                        && std::none_of(destinationViews.begin(), destinationViews.end(),
                                            [&](const auto& view) {
                                                return view.mIdentity == dormantTarget.getCellRef().getRefNum()
                                                    || view.mIdentity == dormantScript.getCellRef().getRefNum();
                                            })
                                        && decision.getStackTarget()
                                            == (stacked ? selectedTarget->getCellRef().getRefNum() : ESM::RefNum()),
                                    "destination preparation revived a dormant node or missed first-stack membership");
                                require(
                                    snapshot() == before, "owned source inventory preparation changed raw/live values");
                                unchangedScripts();
                                const auto dormantRef = dormant.getCellRef();
                                for (int count : { 1, -1 })
                                {
                                    dormant.getCellRef().setCount(count);
                                    reject(
                                        [&] {
                                            source.validateTransfer(decision, destination, removal, destinationAdd);
                                        },
                                        "source inventory membership changed");
                                    dormant.getCellRef() = dormantRef;
                                }
                                const auto dormantFlags = flags(dormant.getRefData());
                                setFlags(
                                    dormant.getRefData(), dormantFlags & 7u ? dormantFlags & ~7u : dormantFlags | 1u);
                                reject([&] { source.validateTransfer(decision, destination, removal, destinationAdd); },
                                    "source inventory values changed");
                                setFlags(dormant.getRefData(), dormantFlags);
                                dormant.getRefData().getLocals().mLongs[0] += 1;
                                reject([&] { source.validateTransfer(decision, destination, removal, destinationAdd); },
                                    "source inventory values changed");
                                dormant.getRefData().getLocals().mLongs[0] -= 1;
                                source.validateTransfer(decision, destination, removal, destinationAdd);
                                for (const auto& dormantDestination : { dormantTarget, dormantScript })
                                {
                                    const auto ref = dormantDestination.getCellRef();
                                    for (int count : { 1, -1 })
                                    {
                                        dormantDestination.getCellRef().setCount(count);
                                        reject(
                                            [&] {
                                                source.validateTransfer(decision, destination, removal, destinationAdd);
                                            },
                                            "destination state changed");
                                        // Capture a current iterator before restoring dormancy.
                                        const auto invalidSelection
                                            = std::find(destination.begin(), destination.end(), dormantDestination);
                                        dormantDestination.getCellRef() = ref;
                                        const auto selection = destination.getSelectedEnchantItem();
                                        destination.setSelectedEnchantItem(invalidSelection);
                                        reject(
                                            [&] {
                                                source.validateTransfer(decision, destination, removal, destinationAdd);
                                            },
                                            "selection changed or unsupported");
                                        reject(
                                            [&] {
                                                source.prepareTransfer(
                                                    item, quantity, destination, removal, destinationAdd);
                                            },
                                            "selection changed or unsupported");
                                        destination.setSelectedEnchantItem(selection);
                                    }
                                    const auto data = dormantDestination.getRefData().copyForContainerTransfer();
                                    dormantDestination.getRefData().getAnimationState().mScriptedAnims[0].mTime += 1;
                                    reject(
                                        [&] {
                                            source.validateTransfer(decision, destination, removal, destinationAdd);
                                        },
                                        "destination values changed");
                                    dormantDestination.getRefData().getAnimationState().mScriptedAnims[0].mTime -= 1;
                                    const auto bits = flags(data);
                                    setFlags(dormantDestination.getRefData(), bits & 7u ? bits & ~7u : bits | 1u);
                                    reject(
                                        [&] {
                                            source.validateTransfer(decision, destination, removal, destinationAdd);
                                        },
                                        "destination values changed");
                                    setFlags(dormantDestination.getRefData(), bits);
                                    int alive = 0;
                                    dormantDestination.getRefData().setCustomData(std::make_unique<Lifetime>(alive));
                                    reject(
                                        [&] {
                                            source.prepareTransfer(
                                                item, quantity, destination, removal, destinationAdd);
                                        },
                                        "Lua or custom state");
                                    require(alive == 1, "preparation touched dormant destination custom state");
                                    dormantDestination.getRefData().setCustomData(nullptr);
                                }
                                dormantScript.getRefData().getLocals().mLongs[0] += 1;
                                reject([&] { source.validateTransfer(decision, destination, removal, destinationAdd); },
                                    "destination values changed");
                                dormantScript.getRefData().getLocals().mLongs[0] -= 1;
                                const auto destinationSelection = destination.getSelectedEnchantItem();
                                destination.setSelectedEnchantItem(source.begin());
                                reject(
                                    [&] {
                                        source.prepareTransfer(item, quantity, destination, removal, destinationAdd);
                                    },
                                    "selection changed or unsupported");
                                reject([&] { source.validateTransfer(decision, destination, removal, destinationAdd); },
                                    "selection changed or unsupported");
                                destination.setSelectedEnchantItem(destinationSelection);
                                // An unrelated destination count still has to be representable.
                                const auto dormantRefData = dormantScript.getCellRef();
                                dormantScript.getCellRef().setCount(std::numeric_limits<int>::min());
                                reject(
                                    [&] {
                                        source.prepareTransfer(item, quantity, destination, removal, destinationAdd);
                                    },
                                    "count");
                                dormantScript.getCellRef() = dormantRefData;
                                source.validateTransfer(decision, destination, removal, destinationAdd);
                                appendDestination(destinationScript, 1);
                                reject([&] { source.validateTransfer(decision, destination, removal, destinationAdd); },
                                    "destination state changed");
                                auto withAppendedDestination
                                    = source.prepareTransfer(item, quantity, destination, removal, destinationAdd);
                                // Appending a member rejects even when it cannot stack
                                // with the incoming item and the removal value is unchanged.
                                append(fromA ? *scriptA : *scriptB, 1);
                                reject(
                                    [&] {
                                        source.validateTransfer(
                                            withAppendedDestination, destination, removal, destinationAdd);
                                    },
                                    "source inventory membership changed");
                                // A non-MISC selection cannot be represented by this projection.
                                MWWorld::ManualRef book(store, ESM::RefId::stringRefId("native_book"));
                                const auto bookMember = destination.add(book.getPtr(), 1, destinationAdd);
                                auto withBook
                                    = source.prepareTransfer(item, quantity, destination, removal, destinationAdd);
                                destination.setSelectedEnchantItem(bookMember);
                                reject(
                                    [&] {
                                        source.prepareTransfer(item, quantity, destination, removal, destinationAdd);
                                    },
                                    "selection changed or unsupported");
                                reject([&] { source.validateTransfer(withBook, destination, removal, destinationAdd); },
                                    "selection changed or unsupported");
                                destination.setSelectedEnchantItem(destinationSelection);
                                MWWorld::ManualRef gold(store, MWWorld::ContainerStore::sGoldId);
                                appendDestination(gold.getPtr(), 1);
                                reject(
                                    [&] {
                                        source.prepareTransfer(item, quantity, destination, removal, destinationAdd);
                                    },
                                    "destination inventory excludes gold");
                            }
                            for (const auto& member : rawSource)
                                localScripts.remove(member);
                            for (const auto& member : rawDestination)
                                localScripts.remove(member);
                            observedDormant.pop_back();
                            observedDormant.pop_back();
                            observedDormant.pop_back();
                            observedStores.pop_back();
                            observedStores.pop_back();
                        }

        // Store replacement can reuse the same node address and ID (copy assignment
        // may reuse list nodes). Reject by storage identity before inspecting nodes.
        for (bool replaceSource : { false, true })
            for (bool scriptedItem : { false, true })
            {
                MWWorld::ContainerStore source, destination;
                bindEmptyStore(source, addA.mContainer, worldModel);
                bindEmptyStore(destination, addB.mContainer, worldModel);
                observedStores.push_back(&source);
                observedStores.push_back(&destination);
                const auto& seed = scriptedItem ? scripted.getPtr() : plain.getPtr();
                auto item = *source.add(seed, 4, scriptedAddA);
                destination.add(seed, 5, scriptedAddB);
                MWWorld::ContainerStoreRemoveContext removal{ worldModel, addA.mContainer, localScripts,
                    addA.mInventoryUpdated };
                auto paired = source.prepareTransfer(item, 2, destination, removal, scriptedAddB);
                auto& replaced = replaceSource ? source : destination;
                std::vector<const MWWorld::CellRef*> oldScriptKeys;
                for (const auto& ptr : replaced)
                    oldScriptKeys.push_back(&ptr.getCellRef());
                MWWorld::ContainerStore copy(replaced);
                replaced = copy;
                for (auto ptr : replaced)
                    worldModel.registerPtr(ptr);
                reject([&] { source.validateTransfer(paired, destination, removal, scriptedAddB); }, "storage changed");
                for (auto key : oldScriptKeys)
                    localScripts.remove(key);
                // A move to empty storage actually destroys all old nodes.
                MWWorld::ContainerStore empty;
                bindEmptyStore(empty, replaced.getPtr(worldModel), worldModel);
                replaced = std::move(empty);
                reject([&] { source.validateTransfer(paired, destination, removal, scriptedAddB); }, "storage changed");
                if (replaceSource)
                    reject([&] { source.prepareTransfer(item, 2, destination, removal, scriptedAddB); },
                        "ownership mismatch");
                for (auto* container : { &source, &destination })
                    for (auto ptr : *container)
                        localScripts.remove(ptr);
                observedStores.pop_back();
                observedStores.pop_back();
            }
        // Stock LocalScripts retains its catch boundary, duplicate replacement and
        // iterator repair. A separate real list keeps these checks off both owners.
        MWWorld::LocalScripts stockScripts(store);
        MWWorld::ManualRef stockFirst(store, scripted.getPtr().getCellRef().getRefId());
        MWWorld::ManualRef stockSecond(store, scripted.getPtr().getCellRef().getRefId());
        scripts.mBeforeLocals = [] { throw std::runtime_error("injected stock registration failure"); };
        stockScripts.add(scriptId, stockFirst.getPtr(), scripts); // Must log and swallow.
        scripts.mBeforeLocals = {};
        require(!stockScripts.isRunning(scriptId, stockFirst.getPtr()), "failed stock registration inserted a script");
        auto firstScript = stockFirst.getPtr();
        auto secondScript = stockSecond.getPtr();
        firstScript.mCell = addA.mContainer.mCell;
        secondScript.mCell = addB.mContainer.mCell;
        stockScripts.add(scriptId, firstScript, scripts);
        stockScripts.add(scriptId, secondScript, scripts);
        firstScript.getRefData().getLocals().mLongs.at(0) = 99;
        stockScripts.startIteration();
        stockScripts.add(scriptId, firstScript, scripts); // Replace the pending first entry.
        stockScripts.add(ESM::RefId::stringRefId("missing_script"), firstScript, scripts);
        std::pair<ESM::RefId, MWWorld::Ptr> stockEntry;
        require(stockScripts.getNext(stockEntry) && stockEntry.second == secondScript
                && stockEntry.second.mCell == secondScript.mCell && stockScripts.getNext(stockEntry)
                && stockEntry.second == firstScript && stockEntry.second.mCell == firstScript.mCell
                && !stockScripts.getNext(stockEntry) && firstScript.getRefData().getLocals().mLongs.at(0) == 99,
            "stock duplicate/missing registration changed locals, cell ownership or iteration");
        stockScripts.startIteration();
        stockScripts.clearCell(secondScript.mCell);
        require(
            stockScripts.getNext(stockEntry) && stockEntry.second == firstScript && !stockScripts.getNext(stockEntry),
            "stock cell removal invalidated script iteration");
        require(scripts.mRuns == 0, "preparation executed a script");
        for (auto item : live)
            require(item.getRefData().onActivate(), "preparation disturbed live activation flags");
    }

    void checkInventoryOwners(const TES3MP::Native::Loadout& loadout, bool preparation = false)
    {
        MWClass::registerClasses();
        MWWorld::ESMStore store;
        const auto id = ESM::RefId::stringRefId("native_plain");
        const auto playerId = ESM::RefId::stringRefId("player");
        store.insertStatic(*loadout.store().get<ESM::Miscellaneous>().find(id));
        store.insertStatic(*loadout.store().get<ESM::NPC>().find(playerId));
        const auto scriptId = ESM::RefId::stringRefId("native_script");
        const auto scriptedId = ESM::RefId::stringRefId("native_scripted");
        store.insertStatic(*loadout.store().get<ESM::Script>().find(scriptId));
        if (preparation)
        {
            auto alternate = record<ESM::Script>("alternate_script");
            alternate.mScriptText = "begin alternate_script\nend alternate_script\n";
            store.insertStatic(alternate);
            store.insertStatic(record<ESM::Book>("native_book"));
            store.insertStatic(*loadout.store().get<ESM::Miscellaneous>().find(MWWorld::ContainerStore::sGoldId));
        }
        store.insertStatic(*loadout.store().get<ESM::Miscellaneous>().find(scriptedId));
        ESM::ReadersCache readers;
        MWWorld::WorldModel worldModel(store, readers, 1);
        // Two separate NPC references, with disposable base stores. No actor
        // custom data, equipment, Environment, or WindowManager is constructed.
        MWWorld::ManualRef ownerA(store, playerId);
        MWWorld::ManualRef ownerB(store, playerId);
        ESM::Cell cellRecordA;
        ESM::Cell cellRecordB;
        cellRecordA.blank();
        cellRecordB.blank();
        MWWorld::CellStore cellA(MWWorld::Cell(cellRecordA), store, readers);
        MWWorld::CellStore cellB(MWWorld::Cell(cellRecordB), store, readers);
        auto ownerPtrA = ownerA.getPtr();
        auto ownerPtrB = ownerB.getPtr();
        ownerPtrA.mCell = &cellA;
        ownerPtrB.mCell = &cellB;
        MWWorld::ManualRef source(store, id, 5);
        MWWorld::ContainerStore a;
        MWWorld::ContainerStore b;
        MWWorld::LocalScripts localScripts(store);
        bindEmptyStore(a, ownerPtrA, worldModel);
        bindEmptyStore(b, ownerPtrB, worldModel);
        require(ownerA.getPtr() != ownerB.getPtr()
                && ownerA.getPtr().getCellRef().getRefNum() != ownerB.getPtr().getCellRef().getRefNum(),
            "two-owner fixture did not create distinct registered owners");

        struct Event
        {
            MWWorld::Ptr mOwner;
            MWWorld::ConstPtr mItem;
            int mCount;
            bool mAdded;
        };
        std::vector<Event> events;
        struct Listener final : MWWorld::ContainerStoreListener
        {
            MWWorld::ContainerStore& mStore;
            const MWWorld::WorldModel& mWorldModel;
            std::vector<Event>& mEvents;
            Listener(MWWorld::ContainerStore& store, const MWWorld::WorldModel& worldModel, std::vector<Event>& events)
                : mStore(store)
                , mWorldModel(worldModel)
                , mEvents(events)
            {
            }
            void itemAdded(const MWWorld::ConstPtr& item, int count) override
            {
                mEvents.push_back({ mStore.getPtr(mWorldModel), item, count, true });
            }
            void itemRemoved(const MWWorld::ConstPtr& item, int count) override
            {
                mEvents.push_back({ mStore.getPtr(mWorldModel), item, count, false });
            }
        } listenerA(a, worldModel, events), listenerB(b, worldModel, events);
        a.setContListener(&listenerA);
        b.setContListener(&listenerB);
        std::vector<MWWorld::Ptr> notifications;
        const auto updated = [&](const MWWorld::Ptr& owner) { notifications.push_back(owner); };
        const MWWorld::ContainerStoreAddContext addA{ store, worldModel, ownerPtrA, ownerPtrA, nullptr, nullptr,
            updated };
        const MWWorld::ContainerStoreAddContext addB{ store, worldModel, ownerPtrB, ownerPtrB, nullptr, nullptr,
            updated };
        const MWWorld::ContainerStoreRemoveContext removeA{ worldModel, ownerA.getPtr(), localScripts, updated };
        const MWWorld::ContainerStoreRemoveContext removeB{ worldModel, ownerB.getPtr(), localScripts, updated };
        if (preparation)
        {
            checkInventoryPreparation(
                a, b, addA, addB, localScripts, [&] { return std::pair(events.size(), notifications.size()); });
            return;
        }
        const auto success = [&](const MWWorld::Ptr& owner, const MWWorld::Ptr& item, int count, bool added) {
            require(events.size() == notifications.size() && !events.empty() && events.back().mOwner == owner
                    && events.back().mItem == item && events.back().mCount == count && events.back().mAdded == added
                    && notifications.back() == owner,
                "success listener/presentation owner, item, count or multiplicity mismatch");
        };
        const auto first = a.add(source.getPtr(), 3, addA);
        require(a.count(id) == 3 && b.count(id) == 0 && events.size() == 1, "add A changed wrong inventory");
        success(ownerA.getPtr(), *first, 3, true);
        const auto second = b.add(source.getPtr(), 5, addB);
        require(a.count(id) == 3 && b.count(id) == 5 && events.size() == 2, "add B changed wrong inventory");
        success(ownerB.getPtr(), *second, 5, true);
        require(a.add(source.getPtr(), 1, addA) == first && a.count(id) == 4 && b.count(id) == 5,
            "add A did not stack locally");
        success(ownerA.getPtr(), *first, 1, true);
        a.setSelectedEnchantItem(first);
        b.setSelectedEnchantItem(second);

        const auto snapshot = [&] {
            return std::tuple{ a.count(id), b.count(id), a.getWeight(), b.getWeight(),
                std::distance(a.begin(), a.end()), std::distance(b.begin(), b.end()), a.getSelectedEnchantItem(),
                b.getSelectedEnchantItem(), worldModel.getPtrRegistryRevision(), events.size(), notifications.size(),
                source.getPtr().getCellRef().getCount(false), source.getPtr().getCellRef().getRefNum() };
        };
        const auto reject = [&](const auto& operation, std::string_view reason) {
            const auto before = snapshot();
            bool rejected = false;
            try
            {
                operation();
            }
            catch (const std::exception& error)
            {
                rejected = true;
                require(std::string_view(error.what()).find(reason) != std::string_view::npos,
                    "unexpected two-owner rejection reason");
            }
            require(
                rejected && snapshot() == before, "rejection changed inventory, selection, registry or notifications");
        };
        for (int count : { 0, -1, std::numeric_limits<int>::min() })
        {
            reject([&] { a.add(source.getPtr(), count, addA); }, "count");
            reject([&] { b.remove(*second, count, removeB); }, "count");
        }
        reject([&] { a.add(source.getPtr(), std::numeric_limits<int>::max(), addA); }, "overflow");
        reject([&] { a.add(source.getPtr(), 1, addB); }, "owner mismatch");
        reject([&] { b.remove(*second, 1, removeA); }, "owner mismatch");
        reject([&] { a.remove(*second, 1, removeA); }, "ownership mismatch");
        auto forged = *second;
        forged.mContainerStore = &a;
        reject([&] { a.remove(forged, 1, removeA); }, "ownership mismatch");
        reject([&] { a.remove(source.getPtr(), 1, removeA); }, "ownership mismatch");
        reject([&] { a.remove(MWWorld::Ptr(), 1, removeA); }, "ownership mismatch");
        auto noAddPresentation = addA;
        noAddPresentation.mInventoryUpdated = {};
        reject([&] { a.add(source.getPtr(), 1, noAddPresentation); }, "presentation consumer");
        auto noRemovePresentation = removeA;
        noRemovePresentation.mInventoryUpdated = {};
        reject([&] { a.remove(*first, 1, noRemovePresentation); }, "presentation consumer");
        MWWorld::ContainerStore unresolved;
        unresolved.setPtr(ownerA.getPtr(), worldModel);
        reject([&] { unresolved.add(source.getPtr(), 1, addA); }, "unresolved");
        reject([&] { unresolved.remove(*first, 1, removeA); }, "unresolved");
        require(!unresolved.isResolved() && unresolved.begin() == unresolved.end(), "rejection resolved a store");
        MWWorld::InventoryStore equipment;
        auto& baseEquipment = static_cast<MWWorld::ContainerStore&>(equipment);
        reject([&] { baseEquipment.add(source.getPtr(), 1, addA); }, "InventoryStore");
        reject([&] { baseEquipment.remove(*first, 1, removeA); }, "InventoryStore");
        require(equipment.begin() == equipment.end(), "explicit operation mutated equipment store");

        require(a.remove(*first, 1, removeA) == 1 && a.count(id) == 3 && b.count(id) == 5 && a.getWeight() == 7.5f
                && a.getSelectedEnchantItem() == first && events.size() == 4,
            "partial removal changed wrong inventory, weight or selection");
        success(ownerA.getPtr(), *first, 1, false);
        // Preserve stock negative (restocking) stack arithmetic and clamp oversize requests.
        second->getCellRef().setCount(-5);
        require(b.remove(*second, 2, removeB) == 2 && second->getCellRef().getCount(false) == -3
                && b.getWeight() == 7.5f && a.count(id) == 3 && events.size() == 5,
            "removal lost stock negative-stack semantics or changed the other inventory");
        success(ownerB.getPtr(), *second, 2, false);
        require(b.remove(*second, 99, removeB) == 3 && b.count(id) == 0 && b.getWeight() == 0
                && b.getSelectedEnchantItem() == b.end() && a.getSelectedEnchantItem() == first && events.size() == 6,
            "full removal did not clamp count or clear only its owner's selection");
        success(ownerB.getPtr(), *second, 3, false);
        reject([&] { b.remove(*second, 1, removeB); }, "ownership mismatch");
        require(a.remove(*first, 3, removeA) == 3 && a.count(id) == 0 && a.getWeight() == 0
                && a.getSelectedEnchantItem() == a.end() && events.size() == 7,
            "full removal did not empty A");
        success(ownerA.getPtr(), *first, 3, false);
        require(source.getPtr().getCellRef().getCount() == 5 && !source.getPtr().getCellRef().getRefNum().isSet(),
            "add/remove changed source reference");

        Compiler::Extensions extensions;
        Compiler::registerExtensions(extensions);
        MWScript::CompilerContext compilerContext(MWScript::CompilerContext::Type_Full);
        compilerContext.setExtensions(&extensions);
        MWScript::ScriptManager scripts(store, compilerContext, 1);
        MWWorld::ManualRef scripted(store, scriptedId);
        auto scriptedAddA = addA;
        scriptedAddA.mLocalScripts = &localScripts;
        scriptedAddA.mScriptManager = &scripts;
        auto scriptedAddB = addB;
        scriptedAddB.mLocalScripts = &localScripts;
        scriptedAddB.mScriptManager = &scripts;
        const auto scriptA = a.add(scripted.getPtr(), 1, scriptedAddA);
        success(ownerA.getPtr(), *scriptA, 1, true);
        const auto scriptB = b.add(scripted.getPtr(), 1, scriptedAddB);
        success(ownerB.getPtr(), *scriptB, 1, true);
        require(localScripts.isRunning(scriptId, *scriptA) && localScripts.isRunning(scriptId, *scriptB),
            "two-owner script registration failed");
        reject([&] { a.remove(*scriptA, 1, removeB); }, "owner mismatch");
        require(a.count(scriptedId) == 1 && b.count(scriptedId) == 1 && localScripts.isRunning(scriptId, *scriptA)
                && localScripts.isRunning(scriptId, *scriptB),
            "rejected removal changed a scripted stack or registration");
        require(a.remove(*scriptA, 1, removeA) == 1 && !localScripts.isRunning(scriptId, *scriptA)
                && localScripts.isRunning(scriptId, *scriptB) && b.count(scriptedId) == 1,
            "full removal did not unregister only the intended owner's script");
        success(ownerA.getPtr(), *scriptA, 1, false);
        require(b.remove(*scriptB, 1, removeB) == 1 && !localScripts.isRunning(scriptId, *scriptB)
                && a.count(scriptedId) == 0 && b.count(scriptedId) == 0 && events.size() == 11,
            "full removal left a script registration or emitted duplicate success");
        success(ownerB.getPtr(), *scriptB, 1, false);
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
        if (filter == "inventory-two-owners" || filter == "inventory-transfer-preparation")
        {
            checkInventoryOwners(loadout, filter == "inventory-transfer-preparation");
            return;
        }
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
            bindEmptyStore(container, ownerPtr, worldModel);
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
        bindEmptyStore(container, player.getPtr(), worldModel);
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

    void enchantmentFixture(
        const std::filesystem::path& root, float multiplier = 2, int chargeScale = 1, std::string_view invalid = {})
    {
        plugin(root / "local/Calculation.esp", false, [&](ESM::ESMWriter& writer) {
            auto setting = record<ESM::GameSetting>("fEffectCostMult");
            setting.mValue = ESM::Variant(multiplier);
            if (invalid == "setting-type")
                setting.mValue = ESM::Variant(std::string("invalid"));
            write(writer, setting);
            setting.mId = ESM::RefId::stringRefId("iAlchemyMod");
            setting.mValue = ESM::Variant(std::int32_t(5));
            setting.mValue.setType(ESM::VT_Int);
            write(writer, setting);
            for (const auto& [id, value] : { std::pair{ "iMagicItemChargeOnce", 3 }, { "iMagicItemChargeStrike", 5 },
                     { "iMagicItemChargeUse", 7 }, { "iMagicItemChargeConst", 11 } })
            {
                setting.mId = ESM::RefId::stringRefId(id);
                setting.mValue = ESM::Variant(std::int32_t(invalid == "charge-overflow" ? 1000000000
                        : invalid == "negative-charge"                                  ? -1
                                                                                        : value * chargeScale));
                setting.mValue.setType(ESM::VT_Int);
                write(writer, setting);
            }
            auto magicEffect = record<ESM::MagicEffect>("RestoreHealth");
            magicEffect.mData.mBaseCost = invalid == "base-cost" ? std::numeric_limits<float>::infinity() : 4;
            write(writer, magicEffect);
            ESM::ENAMstruct effect{};
            effect.mEffectID = ESM::MagicEffect::RestoreHealth;
            effect.mRange = ESM::RT_Self;
            effect.mArea = 2;
            effect.mDuration = 3;
            effect.mMagnMin = 2;
            effect.mMagnMax = 4;
            ESM::ENAMstruct target = effect;
            target.mRange = ESM::RT_Target;
            target.mArea = 0;
            target.mDuration = 1;
            target.mMagnMin = 1;
            target.mMagnMax = 2;
            if (invalid == "effect-fields")
                effect.mMagnMax = std::numeric_limits<int>::max();
            if (invalid == "range")
                effect.mRange = 3;
            if (invalid == "cost-overflow")
                effect.mArea = effect.mDuration = effect.mMagnMin = effect.mMagnMax = 1000000;
            for (int type = 0; type < 4; ++type)
            {
                auto enchantment = record<ESM::Enchantment>("calc_" + std::to_string(type));
                enchantment.mData = { invalid == "type" ? 4 : type, 77, 99, ESM::Enchantment::Autocalc };
                enchantment.mEffects.populate({ effect, target });
                if (invalid == "effects")
                    enchantment.mEffects.mList.resize(33, enchantment.mEffects.mList.front());
                write(writer, enchantment);
                enchantment.mId = ESM::RefId::stringRefId("manual_" + std::to_string(type));
                enchantment.mData.mFlags = 0;
                write(writer, enchantment);
            }
        });
        config(root / "extra/openmw.cfg",
            "data=../high\ndata-local=../local\nencoding=win1251\ncontent=Patch.esp\ncontent=Calculation.esp\n");
    }

    void checkEnchantment(
        const std::filesystem::path& root, int argc, const char* const argv[], const std::string& filter)
    {
        using namespace TES3MP::Native;
        if (filter == "enchantment-cost")
        {
            // Engine-written plugins and independently specified expected values.
            // This tests adapter inputs/rounding, not parity with a running client.
            for (int pass = 0; pass < 2; ++pass)
            {
                enchantmentFixture(root, pass == 0 ? 2.f : 3.f, pass + 1);
                Loadout loadout(readLoadoutOptions(argc, argv));
                const int charges[] = { 3, 5, 7, 11 };
                for (int type = 0; type < 4; ++type)
                {
                    for (const bool manual : { false, true })
                    {
                        const std::string id = (manual ? "manual_" : "calc_") + std::to_string(type);
                        const auto& value = *loadout.store().get<ESM::Enchantment>().find(ESM::RefId::stringRefId(id));
                        const float expectedCost = manual ? 77.f : pass == 0 ? 9.8f : 14.7f;
                        const int expectedCharge = manual ? 99 : (pass == 0 ? 10 : 15) * charges[type] * (pass + 1);
                        require(std::abs(MWMechanics::getEnchantmentCastCost(value, loadout.store()) - expectedCost)
                                < 0.00001f,
                            "enchantment cost, target surcharge or per-store setting mismatch");
                        require(MWMechanics::getEnchantmentCharge(value, loadout.store()) == expectedCharge,
                            "enchantment charge rounding, type multiplier or per-store setting mismatch");
                        std::ostringstream report;
                        loadout.writeEnchantmentProbe(report, id);
                        require(report.str().find("maximum-charge\t" + std::to_string(expectedCharge) + '\n')
                                    != std::string::npos
                                && report.str().ends_with("complete\n") && report.str().size() < 2048,
                            "enchantment report charge, completion or bound mismatch");
                        require(value.mData.mCost == 77 && value.mData.mCharge == 99,
                            "calculation changed retained enchantment record");
                    }
                }
                const auto& store = loadout.store();
                const auto& enchantment = *store.get<ESM::Enchantment>().find(ESM::RefId::stringRefId("calc_0"));
                auto effect = enchantment.mEffects.mList.front().mData;
                const float factor = pass == 0 ? 2.f : 3.f;
                require(std::abs(MWMechanics::calcEffectCost(
                                     effect, store, nullptr, MWMechanics::EffectCostMethod::PlayerSpell)
                            - 5.2f * factor)
                        < 0.00001f,
                    "player spell duration offset changed");
                require(std::abs(MWMechanics::calcEffectCost(
                                     effect, store, nullptr, MWMechanics::EffectCostMethod::GamePotion)
                            - 20.f)
                        < 0.00001f,
                    "potion used spell multiplier");
                effect.mMagnMin = effect.mMagnMax = effect.mDuration = effect.mArea = 0;
                auto magicEffect = *store.get<ESM::MagicEffect>().find(effect.mEffectID);
                magicEffect.mData.mFlags = 0;
                require(std::abs(MWMechanics::calcEffectCost(effect, store, &magicEffect) - 0.4f * factor) < 0.00001f,
                    "game spell magnitude/duration floor changed");
                magicEffect.mData.mFlags = ESM::MagicEffect::NoMagnitude | ESM::MagicEffect::NoDuration;
                require(std::abs(MWMechanics::calcEffectCost(
                                     effect, store, &magicEffect, MWMechanics::EffectCostMethod::GameEnchantment)
                            - 0.4f * factor)
                        < 0.00001f,
                    "magic effect flags changed");
                magicEffect.mData.mFlags = ESM::MagicEffect::AppliedOnce;
                require(MWMechanics::calcEffectCost(effect, store, &magicEffect) == 0, "applied-once duration changed");
                // Exercise actual CLI dispatch and normalized request identity.
                std::vector<const char*> args(argv, argv + argc);
                args.insert(args.end(), { "--enchantment", "CaLc_0" });
                std::ostringstream report;
                probe(static_cast<int>(args.size()), args.data(), report);
                require(report.str().starts_with("native-enchantment-probe\t1\nid\t\"calc_0\"\n"),
                    "enchantment CLI dispatch or normalization failed");
            }
            return;
        }
        require(filter == "enchantment-rejection", "unknown enchantment filter");
        for (const auto& [invalid, reason] : { std::pair{ "effects", "32 effects" }, { "type", "enchantment type" },
                 { "effect-fields", "effect fields" }, { "range", "effect fields" }, { "base-cost", "base cost" },
                 { "negative-charge", "charge multiplier" }, { "charge-overflow", "integer range" },
                 { "cost-overflow", "integer range" }, { "setting-type", "" }, { "multiplier", "fEffectCostMult" },
                 { "id-size", "256 byte ID" }, { "id-control", "control character" }, { "missing", "" } })
        {
            enchantmentFixture(root,
                std::string_view(invalid) == "multiplier" ? std::numeric_limits<float>::quiet_NaN() : 2, 1, invalid);
            Loadout loadout(readLoadoutOptions(argc, argv));
            std::string id = "calc_0";
            if (std::string_view(invalid) == "id-size")
                id.assign(257, 'x');
            else if (std::string_view(invalid) == "id-control")
                id = "calc_0\n";
            else if (std::string_view(invalid) == "missing")
                id = "missing";
            std::ostringstream report;
            report << "previous publication\n";
            bool failed = false;
            try
            {
                loadout.writeEnchantmentProbe(report, id);
            }
            catch (const std::exception& error)
            {
                failed = true;
                require(std::string_view(error.what()).find(reason) != std::string_view::npos,
                    "unexpected enchantment rejection reason");
            }
            require(failed, "invalid enchantment probe accepted");
            require(report.str() == "previous publication\n", "rejected enchantment published partial report");
        }
        for (const char* mode : { "--sample", "--inventory" })
        {
            std::vector<const char*> args(argv, argv + argc);
            args.insert(args.end(), { "--enchantment", "calc_0", mode });
            if (std::string_view(mode) == "--inventory")
                args.push_back("mixed_item");
            bool failed = false;
            try
            {
                readLoadoutOptions(static_cast<int>(args.size()), args.data());
            }
            catch (const std::runtime_error&)
            {
                failed = true;
            }
            require(failed, "conflicting enchantment CLI mode accepted");
        }
    }

    void check(const std::filesystem::path& root, const std::string& filter)
    {
        if (filter == "enumeration-normalized")
        {
            // Simulate records interned by an earlier load in this process,
            // before the writer/reader sees the fixture's lowercase spelling.
            for (const char* id : { "SaMpLe_ItEm", "NoRmAlIzEd_SpElL", "NoRmAlIzEd_EnChAnTmEnT" })
                ESM::RefId::stringRefId(id);
        }
        fixture(root);
        const std::string directory = Files::pathToUnicodeString(root);
        const char* args[] = { "native-test", "--config", directory.c_str(), "--replace", "config" };
        if (filter.starts_with("enchantment-"))
        {
            checkEnchantment(root, 5, args, filter);
            return;
        }
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
        if (filter == "enumeration-normalized")
        {
            sampleFixture(root);
            const auto options = TES3MP::Native::readLoadoutOptions(5, args);
            std::string previous;
            for (int pass = 0; pass < 2; ++pass)
            {
                TES3MP::Native::Loadout loadout(options);
                const auto& item
                    = *loadout.store().get<ESM::Miscellaneous>().find(ESM::RefId::stringRefId("mixed_item"));
                require(item.mId.getRefIdString() == "MiXeD_Item", "fixture did not preserve interned spelling");
                require(loadout.store()
                            .get<ESM::Spell>()
                            .find(ESM::RefId::stringRefId("normalized_spell"))
                            ->mId.getRefIdString()
                        == "NoRmAlIzEd_SpElL",
                    "fixture did not preserve earlier-load spelling");
                const auto sample = loadout.sample();
                std::ostringstream output;
                loadout.enumerate(output);
                const auto report = output.str();
                for (const auto& value : sample.mRecords)
                {
                    const std::string row = "record\t" + value.mType + "\t\"" + value.mId + "\"\t";
                    const auto position = report.find(row);
                    require(position != std::string::npos, "enumeration/sample winning identity mismatch");
                    require(report.find(row, position + row.size()) == std::string::npos,
                        "enumeration duplicated a winning identity");
                }
                require(report.find("\"deleted_item\"") == std::string::npos, "enumeration included a deleted item");
                require(report.find("\"z_sample_5\"") != std::string::npos, "enumeration was limited to the sample");
                require(report.find("name=\"line\\x09\\\"quoted\\\"\\\\\\x0a\"") != std::string::npos,
                    "enumeration changed display text escaping");
                require(report.find("record\tSpell\t\"normalized_spell\"\tname=\"\"\teffects=1\n") != std::string::npos,
                    "enumeration lost engine effect normalization");
                require(item.mId.getRefIdString() == "MiXeD_Item" && item.mData.mValue == 40
                        && item.mName == "\xd0\x9c\xd0\xb5\xd1\x87" && loadout.sample() == sample,
                    "enumeration changed the winning engine records");
                require(report.ends_with("complete\n"), "enumeration completion marker missing");
                if (pass != 0)
                    require(report == previous, "enumeration changed across loads in the same process");
                previous = report;
            }
            std::ostringstream output;
            TES3MP::Native::probe(5, args, output);
            require(output.str() == previous, "default CLI enumeration differs from retained store enumeration");
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
