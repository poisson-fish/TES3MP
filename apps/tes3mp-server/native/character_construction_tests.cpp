#include "character_construction.hpp"
#include "../player_identity_file.hpp"
#include <tes3mp/authenticated_join.hpp>
#include <apps/openmw/mwmechanics/playerconstruction.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadbsgn.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadmgef.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace
{
    using namespace TES3MP;
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template<class T> T id(uint64_t raw) { return T::fromValue(raw).value(); }
    template<class T> T recordId(std::string_view name) { return id<T>(*characterRecordId(name)); }
    ESM::RefId ref(std::string_view name) { return ESM::RefId::stringRefId(name); }
    Transform spawn(int64_t x)
    {
        const auto zero = Turn32::fromValue(0);
        return { CellId::interior(id<CellSpaceId>(7)), Position3(x, 20, 30), Orientation3(zero, zero, zero) };
    }
    struct Content
    {
        MWWorld::ESMStore store;
        ESM::Race race;
        ESM::Class characterClass;
        ESM::BirthSign sign;
        CharacterAppearance female{ recordId<RaceRecordId>("race"), recordId<HeadRecordId>("female_head"),
            recordId<HairRecordId>("female_hair"), CharacterSex::Female };
        CharacterAppearance male{ recordId<RaceRecordId>("race"), recordId<HeadRecordId>("male_head"),
            recordId<HairRecordId>("male_hair"), CharacterSex::Male };
        void setting(const char* name, ESM::Variant value)
        {
            ESM::GameSetting setting; setting.mId = ref(name); setting.mValue = value;
            if (store.get<ESM::GameSetting>().search(setting.mId)) store.overrideRecord(setting);
            else store.insertStatic(setting);
        }
        void spell(const char* name, int type, int cost, int flags)
        {
            ESM::Spell spell; spell.blank(); spell.mId = ref(name);
            spell.mData = { type, cost, flags };
            if (type == ESM::Spell::ST_Spell)
                spell.mEffects.mList.push_back({{ESM::MagicEffect::RestoreHealth, {}, ESM::Attribute::Luck,
                    ESM::RT_Self, 0, 1, 5, 5}, 0});
            store.insertStatic(spell);
        }
        void exceedSpellBudget()
        {
            for (size_t i = 0; i < 65; ++i)
                spell(("budget_spell_" + std::to_string(i)).c_str(), ESM::Spell::ST_Spell, 1, ESM::Spell::F_PCStart);
            setting("iAutoPCSpellMax", ESM::Variant(65));
        }
        Content()
        {
            for (int i = 0; i < ESM::Attribute::Length; ++i)
            {
                ESM::Attribute attribute;
                attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
                store.insertStatic(attribute);
            }
            for (int i = 0; i < ESM::Skill::Length; ++i)
            {
                ESM::Skill skill; skill.blank();
                skill.mId = *ESM::Skill::indexToRefId(i).getIf<ESM::StringRefId>();
                // Content specialization deliberately differs from contiguous index groups.
                skill.mData.mSpecialization = (i == 0 || i == 20) ? ESM::Class::Combat : ESM::Class::Magic;
                store.insertStatic(skill);
            }
            setting("fPCbaseMagickaMult", ESM::Variant(1.f));
            setting("fAutoPCSpellChance", ESM::Variant(0.f));
            setting("iAutoPCSpellMax", ESM::Variant(1));
            setting("iAutoSpellAttSkillMin", ESM::Variant(0));
            setting("fEffectCostMult", ESM::Variant(1.f));
            ESM::MagicEffect effect; effect.blank(); effect.mId = ESM::MagicEffect::RestoreHealth;
            effect.mData.mSchool = ESM::Skill::Destruction; effect.mData.mBaseCost = 1.f;
            effect.mData.mFlags = ESM::MagicEffect::TargetAttribute;
            store.insertStatic(effect);
            spell("race_power", ESM::Spell::ST_Power, 0, 0);
            spell("sign_ability", ESM::Spell::ST_Ability, 0, 0);
            spell("cheap", ESM::Spell::ST_Spell, 1, ESM::Spell::F_PCStart | ESM::Spell::F_Autocalc);
            spell("strong", ESM::Spell::ST_Spell, 10, ESM::Spell::F_PCStart);
            spell("unaffordable", ESM::Spell::ST_Spell, 100, ESM::Spell::F_PCStart);
            race.blank(); race.mId = ref("race"); race.mData.mFlags = ESM::Race::Playable;
            for (int i = 0; i < ESM::Attribute::Length; ++i)
            {
                race.mData.setAttribute(ESM::Attribute::indexToRefId(i), false, 30);
                race.mData.setAttribute(ESM::Attribute::indexToRefId(i), true, 40);
            }
            race.mData.mBonus[0] = {3, 5}; race.mPowers.mList = { ref("race_power") }; store.insertStatic(race);
            characterClass.blank(); characterClass.mId = ref("class"); characterClass.mData.mIsPlayable = 1;
            characterClass.mData.mAttribute = {0, 1}; characterClass.mData.mSpecialization = ESM::Class::Combat;
            for (int i = 0; i < 5; ++i) characterClass.mData.mSkills[i] = {i + 2, i + 7};
            store.insertStatic(characterClass);
            sign.blank(); sign.mId = ref("sign"); sign.mPowers.mList = {ref("race_power"), ref("sign_ability")};
            store.insertStatic(sign);
            for (const auto& [name, part, isFemale] : {
                std::tuple{"female_head", ESM::BodyPart::MP_Head, true},
                std::tuple{"female_hair", ESM::BodyPart::MP_Hair, true},
                std::tuple{"male_head", ESM::BodyPart::MP_Head, false},
                std::tuple{"male_hair", ESM::BodyPart::MP_Hair, false}})
            {
                ESM::BodyPart body; body.blank(); body.mId = ref(name); body.mRace = race.mId;
                body.mData.mPart = static_cast<unsigned char>(part); body.mData.mType = ESM::BodyPart::MT_Skin;
                body.mData.mFlags = isFemale ? ESM::BodyPart::BPF_Female : 0; store.insertStatic(body);
            }
        }
        CharacterContentCatalog catalog()
        {
            // Deliberately incorrect scaffold stats prove the native provider owns derivation.
            CharacterRaceDefinition declared{ female.race, { female, male } };
            declared.femaleAttributes.fill(1); declared.maleAttributes.fill(2);
            CharacterClassDefinition cls{recordId<ClassRecordId>("class"), ClassSpecialization::Combat,
                {0, 1}, {2, 3, 4, 5, 6}, {7, 8, 9, 10, 11}};
            CharacterBirthsignDefinition birth{recordId<BirthsignRecordId>("sign"), {}};
            auto selections = CharacterContentCatalog::create(testContentManifestId(), spawn(10), spawn(100),
                std::span(&declared, 1), std::span(&cls, 1), std::span(&birth, 1), {}).value();
            return Native::withNativeCharacterConstruction(selections, store);
        }
    };
    CharacterProfile apply(const CharacterProfile& profile, const CharacterContentCatalog& catalog,
        CharacterCreationChoice choice)
    {
        auto result = applyCharacterCreation(profile, catalog, {profile.revision(), std::move(choice)});
        require(std::holds_alternative<CharacterProfile>(result), "character choice rejected");
        return std::get<CharacterProfile>(std::move(result));
    }
    CharacterProfile review(Content& content, const CharacterContentCatalog& catalog, bool male = false)
    {
        auto profile = apply(CharacterProfile::fresh(), catalog, SetCharacterName{"Nerevar"});
        profile = apply(profile, catalog, SetCharacterAppearance{male ? content.male : content.female});
        profile = apply(profile, catalog, SetCharacterClass{recordId<ClassRecordId>("class")});
        return apply(profile, catalog, SetCharacterBirthsign{recordId<BirthsignRecordId>("sign")});
    }
    void construction()
    {
        Content content; const auto catalog = content.catalog();
        ESM::NPC templateBody; templateBody.blank();
        templateBody.mFlags = ESM::NPC::Essential | ESM::NPC::Respawn;
        templateBody.mClass = ref("template_class"); templateBody.mFaction = ref("template_faction");
        templateBody.mScript = ref("template_script"); templateBody.mModel = "template_model";
        templateBody.mInventory.mList.push_back({2, ref("template_item")});
        templateBody.mSpells.mList.push_back(ref("template_spell"));
        const auto original = templateBody;
        for (const bool male : {false, true})
        {
            const auto head = ref(male ? "male_head" : "female_head");
            const auto hair = ref(male ? "male_hair" : "female_hair");
            require(MWMechanics::isPlayablePlayerAppearance(&content.race,
                content.store.get<ESM::BodyPart>().find(head), content.store.get<ESM::BodyPart>().find(hair), male),
                "shared appearance rejected valid selected parts");
            auto stock = templateBody;
            MWMechanics::setPlayerAppearance(stock, content.race.mId, male, head, hair);
            require(stock.isMale() == male && (stock.mFlags & ESM::NPC::Essential)
                && stock.mInventory.mList.size() == 1 && stock.mScript == original.mScript,
                "stock appearance selection discarded gameplay fields");
            const auto detached = MWMechanics::buildPlayerBody(templateBody, content.race.mId, male, head, hair);
            require(detached.isMale() == male && detached.mRace == stock.mRace
                && detached.mHead == stock.mHead && detached.mHair == stock.mHair
                && detached.mFlags == (male ? 0 : ESM::NPC::Female) && detached.mClass.empty()
                && detached.mFaction.empty() && detached.mScript.empty() && detached.mModel.empty()
                && detached.mInventory.mList.empty() && detached.mSpells.mList.empty(),
                "detached appearance inherited template gameplay or changed selected parts");
        }
        require(templateBody.mFlags == original.mFlags && templateBody.mInventory.mList.size() == 1,
            "shared body assembly mutated winning template");
        const auto female = review(content, catalog);
        const auto male = review(content, catalog, true);
        require(female.derived().attributes[0] == 40 && male.derived().attributes[0] == 50,
            "native sex/favored attributes did not replace scaffold values");
        require(female.derived().skills[3] == 20 && female.derived().skills[7] == 30
            && female.derived().skills[20] == 10, "winning SKIL specialization not used");
        auto expected = std::vector{recordId<SpellRecordId>("race_power"), recordId<SpellRecordId>("sign_ability"),
            recordId<SpellRecordId>("strong")};
        std::ranges::sort(expected);
        require(female.derived().startingSpells == expected, "racial/sign/strongest affordable starting spell mismatch");
        const CustomClassDefinition custom{"Scholar", {}, ClassSpecialization::Magic,
            {0, 1}, {2, 3, 4, 5, 6}, {7, 8, 9, 10, 11}};
        const auto edited = apply(female, catalog, SetCharacterClass{custom});
        require(edited.derived().skills[3] == 25 && female.derived().skills[3] == 20,
            "custom class construction mutated another candidate");
        content.setting("fPCbaseMagickaMult", ESM::Variant(.1f));
        const auto changed = review(content, catalog);
        require(std::ranges::find(changed.derived().startingSpells, recordId<SpellRecordId>("cheap"))
                != changed.derived().startingSpells.end()
            && std::ranges::find(changed.derived().startingSpells, recordId<SpellRecordId>("strong"))
                == changed.derived().startingSpells.end(), "starting spell settings leaked across explicit stores/calls");
        content.exceedSpellBudget();
        const auto failure = applyCharacterCreation(female, catalog, {female.revision(), CompleteCharacterCreation{}});
        require(std::get<CharacterProfileError>(failure) == CharacterProfileError::InvalidInitialState,
            "native spell allocation budget did not reject");
        content.setting("iAutoPCSpellMax", ESM::Variant(1));
        auto invalid = content.female; invalid.hair = recordId<HairRecordId>("male_hair");
        require(!catalog.construction()->derive(invalid, recordId<ClassRecordId>("class"), recordId<BirthsignRecordId>("sign")),
            "sex-incompatible native appearance admitted");
        content.race.mData.setAttribute(ESM::Attribute::Strength, false, 65536);
        content.store.overrideRecord(content.race);
        require(!catalog.construction()->derive(content.female, recordId<ClassRecordId>("class"), recordId<BirthsignRecordId>("sign")),
            "native stat overflow truncated");
        // Stock partial selection semantics remain available without an Environment.
        ESM::NPC base; base.blank(); base.mNpdt.mSkills.fill(17); base.mNpdt.mAttributes.fill(31);
        const auto unselected = MWMechanics::buildPlayerStats(base, nullptr, nullptr, content.store);
        require(unselected.mAttributes.at(ESM::Attribute::Strength).getBase() == 17,
            "stock pre-race fallback changed");
    }
    void discovery()
    {
        Content content;
        const auto discover = [&] {
            return Native::discoverNativeCharacterContent(testContentManifestId(), spawn(10), spawn(100), {}, content.store);
        };
        auto catalog = discover();
        require(catalog && catalog->construction(), "native selections not discovered");
        require(catalog->find(content.female.race)->appearances == std::vector{content.female, content.male},
            "sex-compatible appearances not discovered deterministically");
        require(review(content, *catalog).derived() == review(content, content.catalog()).derived(),
            "discovered native choices changed construction");
        // Winning overrides, including playable flags and body-part sex/type, own discovery.
        auto head = *content.store.get<ESM::BodyPart>().find(ref("female_head"));
        head.mId = ref("extra_head"); content.store.insertStatic(head);
        auto hidden = head; hidden.mId = ref("hidden_head"); hidden.mData.mFlags |= ESM::BodyPart::BPF_NotPlayable;
        content.store.insertStatic(hidden);
        auto firstPerson = head; firstPerson.mId = ref("first_person_head.1st"); content.store.insertStatic(firstPerson);
        auto otherRace = content.race; otherRace.mId = ref("nonplayable"); otherRace.mData.mFlags = 0;
        content.store.insertStatic(otherRace);
        auto cls = content.characterClass; cls.mId = ref("hidden_class"); cls.mData.mIsPlayable = 0;
        content.store.insertStatic(cls);
        catalog = discover();
        require(catalog && catalog->find(content.female.race)->appearances.size() == 3
            && !catalog->find(recordId<RaceRecordId>("nonplayable"))
            && !catalog->find(recordId<ClassRecordId>("hidden_class")), "nonplayable native records offered");
        head.mData.mFlags |= ESM::BodyPart::BPF_NotPlayable; content.store.overrideRecord(head);
        content.characterClass.mData.mSpecialization = ESM::Class::Magic;
        content.store.overrideRecord(content.characterClass);
        catalog = discover();
        require(catalog && catalog->find(content.female.race)->appearances.size() == 2
            && catalog->find(recordId<ClassRecordId>("class"))->specialization == ClassSpecialization::Magic,
            "discovery used overridden records");
        content.characterClass.mData.mSkills[0][0] = 256;
        content.store.overrideRecord(content.characterClass);
        require(!discover(), "invalid class index truncated during discovery");
        content.characterClass.mData.mSkills[0][0] = 2; content.store.overrideRecord(content.characterClass);
        auto hair = *content.store.get<ESM::BodyPart>().find(ref("female_hair"));
        for (int i = 0; i < 32; ++i)
        {
            head.mId = ESM::RefId::stringRefId("head_" + std::to_string(i)); head.mData.mFlags = ESM::BodyPart::BPF_Female;
            hair.mId = ESM::RefId::stringRefId("hair_" + std::to_string(i));
            content.store.insertStatic(head); content.store.insertStatic(hair);
        }
        require(!discover(), "appearance Cartesian-product budget not enforced");
        // Overflow publishes no partial selection catalog; another store is unaffected.
        Content independent;
        require(Native::discoverNativeCharacterContent(testContentManifestId(), spawn(10), spawn(100), {}, independent.store)
            .has_value(), "failed discovery changed another content store");
    }
    class FakeCrypto final : public CredentialCrypto
    {
        uint8_t mSeed = 1;
    public:
        bool randomBytes(std::span<std::byte> out) noexcept override
        { std::ranges::fill(out, std::byte(mSeed++)); return true; }
        bool sha256(std::span<const std::byte> source, CredentialDigest& out) noexcept override
        {
            out.bytes.fill(std::byte{});
            for (size_t i = 0; i < source.size(); ++i) out.bytes[i % out.bytes.size()] ^= source[i];
            return true;
        }
        bool constantTimeEqual(std::span<const std::byte> a, std::span<const std::byte> b) noexcept override
        { return std::ranges::equal(a, b); }
    };
    struct Persistence final : PlayerIdentityPersistence
    {
        ServerApp::PlayerIdentityFile& file;
        bool reject = false;
        explicit Persistence(ServerApp::PlayerIdentityFile& value) : file(value) {}
        bool replace(std::span<const PersistedPlayerIdentity> records) noexcept override
        { return !reject && file.replace(records); }
    };
    auto open(const std::filesystem::path& path)
    {
        auto result = ServerApp::PlayerIdentityFile::open(path);
        require(std::holds_alternative<std::unique_ptr<ServerApp::PlayerIdentityFile>>(result), "identity file open failed");
        return std::get<std::unique_ptr<ServerApp::PlayerIdentityFile>>(std::move(result));
    }
    // Synthetic engine seam: exercises the real profile/reducer durability
    // composition, without claiming native inventory or animation coverage.
    struct SyntheticNative final : NativeInventoryAuthority
    {
        std::vector<std::byte> image{std::byte{1}, std::byte{0}, std::byte{0}};
        std::array<std::shared_ptr<const CharacterProfile>, 2> profiles;
        bool rejectResources = false;
        unsigned preparations = 0;
        struct Candidate final : PreparedNativeInventory
        {
            SyntheticNative& owner;
            size_t index;
            std::vector<std::byte> before, after;
            std::shared_ptr<const CharacterProfile> profile;
            bool consumed = false;
            Candidate(SyntheticNative& value, size_t slot, const CharacterProfile& constructed)
                : owner(value), index(slot), before(value.image), after(value.image),
                  profile(std::make_shared<const CharacterProfile>(constructed))
            { after[index + 1] = std::byte(constructed.derived().attributes[0]); }
            CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
            {
                if (consumed || before != owner.image) return CanonicalDurabilityResult::Rejected;
                const auto result = persist(after);
                if (result == CanonicalDurabilityResult::Committed)
                {
                    owner.image.swap(after); owner.profiles[index].swap(profile); consumed = true;
                }
                return result;
            }
        };
        std::unique_ptr<PreparedNativeInventory> prepareInventory(
            const CanonicalServerState&, const ServerCommandProposal&) override { return {}; }
        std::span<const std::byte> inventoryImage() const noexcept override { return image; }
        std::unique_ptr<PreparedNativeInventory> prepareCharacterCompletion(const CanonicalServerState& players,
            PlayerId player, const CharacterProfile& profile, ServerTick) override
        {
            ++preparations;
            if (rejectResources || player.value() > profiles.size() || profiles[player.value() - 1]) return {};
            require(players.findPlayer(player)->transform() == spawn(100), "native completion did not stage safe point");
            return std::make_unique<Candidate>(*this, size_t(player.value() - 1), profile);
        }
    };
    struct CampaignPersistence final : CanonicalDurabilityPort
    {
        bool reject = false;
        std::vector<std::byte> nativeImage;
        std::shared_ptr<const CanonicalStatePublication> publication;
        CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
            CanonicalRevision, std::span<const DurableCommandOrder>, const CanonicalInventoryWorld*,
            const CanonicalCombatWorld*, const CanonicalInteractiveObjectWorld*, const CanonicalActorWorld*,
            const CanonicalWorldState*, const CanonicalScriptState*, std::span<const std::byte> image) noexcept override
        try
        {
            if (reject) return CanonicalDurabilityResult::Rejected;
            nativeImage.assign(image.begin(), image.end()); publication = candidate;
            return CanonicalDurabilityResult::Committed;
        }
        catch (...) { return CanonicalDurabilityResult::Failed; }
    };
    void completion(const std::filesystem::path& path, bool native = false)
    {
        require(!std::filesystem::exists(path), "completion scratch file must be fresh");
        Content content; const auto catalog = content.catalog();
        FakeCrypto crypto; auto file = open(path); Persistence persistence(*file);
        auto registry = std::get<std::unique_ptr<PlayerIdentityRegistry>>(PlayerIdentityRegistry::create(crypto, persistence, {}));
        NullMetricSink metrics; NullStructuredEventSink events; Observability observation{metrics, events};
        CanonicalCommandReducer reducer(std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observation,
            testContentManifest());
        SyntheticNative engine; CampaignPersistence campaign;
        if (native) require(reducer.configureDurability(campaign, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &engine),
            "native completion durability configuration failed");
        auto joins = AuthenticatedJoinCoordinator::create(spawn(10), testContentManifest(), id<SessionId>(1), *registry, reducer).value();
        const auto first = std::get<AuthenticatedJoinResult>(joins.join(id<PrincipalId>(1), SessionGeneration::initial(), id<ServerTick>(1)));
        const auto second = std::get<AuthenticatedJoinResult>(joins.join(id<PrincipalId>(2), SessionGeneration::initial(), id<ServerTick>(2)));
        auto choice = [&](PlayerId player, CharacterCreationChoice value) {
            const auto current = *registry->characterProfile(player);
            const auto result = joins.applyCharacterCreation(player, catalog, {current.revision(), std::move(value)},
                id<ServerTick>(std::max<uint64_t>(3, reducer.checkpointTick().value())));
            require(std::holds_alternative<CharacterProfile>(result), "atomic native profile choice failed");
        };
        for (auto player : {first.player, second.player})
        {
            choice(player, SetCharacterName{"Nerevar"});
            choice(player, SetCharacterAppearance{player == first.player ? content.female : content.male});
            choice(player, SetCharacterClass{recordId<ClassRecordId>("class")});
            choice(player, SetCharacterBirthsign{recordId<BirthsignRecordId>("sign")});
        }
        require(open(path)->records()[0].characterProfile == CharacterProfile::fresh(), "review leaked to durable identity");
        const auto reviewFirst = *registry->characterProfile(first.player);
        const auto reviewSecond = *registry->characterProfile(second.player);
        content.exceedSpellBudget();
        const auto invalid = joins.prepareCharacterCreation(first.player, catalog,
            {reviewFirst.revision(), CompleteCharacterCreation{}}, id<ServerTick>(4));
        require(std::get<CharacterProfileError>(invalid) == CharacterProfileError::InvalidInitialState
            && *registry->characterProfile(first.player) == reviewFirst
            && reducer.state().findPlayer(first.player)->transform() == spawn(10),
            "failed native construction reserved or installed a completion");
        content.setting("iAutoPCSpellMax", ESM::Variant(1));
        if (native)
        {
            engine.rejectResources = true;
            const auto unavailable = joins.prepareCharacterCreation(first.player, catalog,
                {reviewFirst.revision(), CompleteCharacterCreation{}}, id<ServerTick>(4));
            require(std::get<CharacterProfileError>(unavailable) == CharacterProfileError::InvalidInitialState
                && !engine.profiles[0] && *registry->characterProfile(first.player) == reviewFirst,
                "missing native resources completed a character");
            engine.rejectResources = false;
            auto cancelled = joins.prepareCharacterCreation(first.player, catalog,
                {reviewFirst.revision(), CompleteCharacterCreation{}}, id<ServerTick>(4));
            auto* abandoned = std::get_if<PreparedCharacterCreation>(&cancelled);
            require(abandoned && joins.cancelCharacterCreation(std::move(*abandoned)) && !engine.profiles[0],
                "cancelled native completion installed state or retained identity reservation");
            auto stale = joins.prepareCharacterCreation(first.player, catalog,
                {reviewFirst.revision(), CompleteCharacterCreation{}}, id<ServerTick>(4));
            auto* old = std::get_if<PreparedCharacterCreation>(&stale);
            auto concurrent = reducer.preparePlayerSafePoint(second.player, spawn(10), id<ServerTick>(4));
            require(old && concurrent && reducer.commit(std::move(*concurrent)), "concurrent lifecycle could not advance");
            const auto currentPublication = reducer.latestPublication();
            require(!joins.commitCharacterCreation(std::move(*old)) && !engine.profiles[0]
                && *registry->characterProfile(first.player) == reviewFirst
                && reducer.latestPublication() == currentPublication
                && open(path)->records()[0].characterProfile == CharacterProfile::fresh(),
                "stale native completion installed a profile or candidate");
            const auto before = engine.image;
            const auto publication = reducer.latestPublication();
            campaign.reject = true;
            auto staged = joins.prepareCharacterCreation(first.player, catalog,
                {reviewFirst.revision(), CompleteCharacterCreation{}}, id<ServerTick>(4));
            auto* candidate = std::get_if<PreparedCharacterCreation>(&staged);
            require(candidate && candidate->candidateNativeInventory() && !engine.profiles[0],
                "native preparation installed state before durability");
            require(!joins.commitCharacterCreation(std::move(*candidate)) && engine.image == before
                && !engine.profiles[0] && *registry->characterProfile(first.player) == reviewFirst
                && *registry->characterProfile(second.player) == reviewSecond
                && reducer.latestPublication() == publication
                && reducer.state().findPlayer(first.player)->transform() == spawn(10)
                && open(path)->records()[0].characterProfile == CharacterProfile::fresh(),
                "campaign rejection leaked native state, profile or safe point");
            campaign.reject = false;
        }
        persistence.reject = true;
        auto failed = joins.prepareCharacterCreation(first.player, catalog,
            {reviewFirst.revision(), CompleteCharacterCreation{}}, id<ServerTick>(4));
        auto* prepared = std::get_if<PreparedCharacterCreation>(&failed);
        require(prepared && !joins.commitCharacterCreation(std::move(*prepared)), "rejected storage completed profile");
        require(*registry->characterProfile(first.player) == reviewFirst
            && *registry->characterProfile(second.player) == reviewSecond
            && reducer.state().findPlayer(first.player)->transform() == spawn(10)
            && open(path)->records()[0].characterProfile == CharacterProfile::fresh(), "failed completion leaked state");
        persistence.reject = false;
        choice(first.player, CompleteCharacterCreation{});
        require(*registry->characterProfile(second.player) == reviewSecond
            && reducer.state().findPlayer(second.player)->transform() == spawn(10), "first completion changed second player");
        if (native) require(engine.profiles[0] && !engine.profiles[1] && campaign.nativeImage == engine.image
            && engine.image[1] == std::byte{40} && engine.image[2] == std::byte{0},
            "first native completion changed another owner or wrote installed image");
        choice(second.player, CompleteCharacterCreation{});
        const auto completedFirst = *registry->characterProfile(first.player);
        const auto completedSecond = *registry->characterProfile(second.player);
        require(completedFirst.derived().attributes[0] == 40 && completedSecond.derived().attributes[0] == 50
            && reducer.state().findPlayer(first.player)->transform() == spawn(100), "completed native result/safe point mismatch");
        // Repeated completion rejects before construction/persistence.
        const auto preparations = engine.preparations;
        const auto duplicate = joins.applyCharacterCreation(first.player, catalog,
            {completedFirst.revision(), CompleteCharacterCreation{}}, id<ServerTick>(5));
        require(std::get<CharacterProfileError>(duplicate) == CharacterProfileError::AlreadyEstablished, "duplicate completion admitted");
        if (native) require(engine.preparations == preparations
            && engine.profiles[0] && engine.profiles[1] && campaign.nativeImage == engine.image
            && *engine.profiles[0] == completedFirst && *engine.profiles[1] == completedSecond
            && engine.image[2] == std::byte{50}, "native completions did not durably install owned results");
        auto restartedFile = open(path); Persistence restartedPersistence(*restartedFile);
        auto restarted = std::get<std::unique_ptr<PlayerIdentityRegistry>>(
            PlayerIdentityRegistry::create(crypto, restartedPersistence, restartedFile->records()));
        require(*restarted->characterProfile(first.player) == completedFirst
            && *restarted->characterProfile(second.player) == completedSecond, "native profile restart changed stats/spells/revisions");
        CanonicalCommandReducer restartedReducer(std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observation,
            testContentManifest());
        auto restartedJoins = AuthenticatedJoinCoordinator::create(spawn(10), testContentManifest(), id<SessionId>(10),
            *restarted, restartedReducer).value();
        for (const auto& record : restartedFile->records())
        {
            auto reattach = restartedJoins.prepareReattach(id<PrincipalId>(record.claim.player.value() + 10), record.claim,
                SessionGeneration::initial(), id<ServerTick>(6));
            auto* pending = std::get_if<AuthenticatedJoinPreparation>(&reattach);
            require(pending && std::holds_alternative<AuthenticatedJoinResult>(restartedJoins.commit(pending->id)),
                "native established profile could not reattach");
            require(restartedReducer.state().findPlayer(record.claim.player)->transform() == spawn(100),
                "reattach lost completion safe point");
        }
    }
}

int main(int argc, char** argv)
try
{
    require(argc >= 2, "Usage: tes3mp_native_character_tests construction | discovery | completion NEW_IDENTITY_FILE");
    if (std::string_view(argv[1]) == "construction" && argc == 2) construction();
    else if (std::string_view(argv[1]) == "discovery" && argc == 2) discovery();
    else if (std::string_view(argv[1]) == "completion" && argc == 3) completion(std::filesystem::absolute(argv[2]));
    else if (std::string_view(argv[1]) == "completion-native" && argc == 3) completion(std::filesystem::absolute(argv[2]), true);
    else throw std::runtime_error("unknown character test filter");
    std::cout << "PASS native character " << argv[1] << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
