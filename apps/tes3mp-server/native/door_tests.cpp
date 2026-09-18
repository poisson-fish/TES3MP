#include "door_tests.hpp"
#include "loadout.hpp"
#include "ordinary_door.hpp"
#include "equipment_session.hpp"
#include "test_allocations.hpp"

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/doormotion.hpp>
#include <apps/openmw/mwworld/livecellref.hpp>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/compiler/locals.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadrace.hpp>

#include <osg/Math>

#include <cmath>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace TES3MP::Native::Testing
{
    namespace
    {
        void require(bool value, const char* message)
        {
            if (!value) throw std::runtime_error(message);
        }

        void near(float value, float expected, const char* message)
        {
            require(std::abs(value - expected) < .00001f, message);
        }

        template<class F> void rejects(F&& operation, const char* message)
        {
            bool rejected = false;
            try { operation(); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, message);
        }

        ESM::Door baseDoor()
        {
            ESM::Door result;
            result.blank();
            result.mId = ESM::RefId::stringRefId("ordinary_door");
            result.mOpenSound = ESM::RefId::stringRefId("door_open");
            result.mCloseSound = ESM::RefId::stringRefId("door_close");
            return result;
        }

        ESM::CellRef placement()
        {
            ESM::CellRef result;
            result.blank();
            result.mRefID = baseDoor().mId;
            result.mRefNum = {17, 0};
            result.mPos = {{30, 40, 50}, {.1f, -.2f, 5.f}};
            result.mScale = 1.25f;
            result.mOwner = ESM::RefId::stringRefId("door_owner");
            result.mFaction = ESM::RefId::stringRefId("door_faction");
            result.mFactionRank = 2;
            result.mGlobalVariable = "door_access";
            return result;
        }

        // Raw stock serialization used independently of the bounded codec.
        std::string save(const ESM::DoorState& state)
        {
            std::ostringstream stream(std::ios::binary);
            ESM::ESMWriter out;
            out.setVersion();
            out.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
            out.setType(0);
            out.setRecordCount(1);
            out.save(stream);
            out.startRecord(ESM::REC_DOOR, 0);
            state.save(out);
            out.endRecord(ESM::REC_DOOR);
            out.close();
            return stream.str();
        }

        ESM::DoorState load(const std::string& bytes)
        {
            ESM::ESMReader in;
            in.open(std::make_unique<std::istringstream>(bytes, std::ios::binary), {});
            require(in.getRecName() == ESM::REC_DOOR, "Door save record missing");
            in.getRecHeader();
            ESM::DoorState result;
            result.blank();
            result.mRef.loadId(in, true);
            result.load(in);
            require(!in.hasMoreSubs() && !in.hasMoreRecs(), "Door save has trailing data");
            return result;
        }

        // Stock live-door field producers/consumers with explicit empty locals.
        // LiveCellRef::load itself also calls LuaManager, which is intentionally
        // not constructed by these headless checks.
        ESM::DoorState stockFields(const ESM::Door& base, const ESM::DoorState& state)
        {
            MWWorld::LiveCellRef<ESM::Door> live(state.mRef, &base);
            live.mData = MWWorld::RefData(state, false);
            MWWorld::Ptr ptr(&live);
            ptr.getClass().readAdditionalState(ptr, state);
            require(ptr.getClass().getDoorState(ptr) == static_cast<MWWorld::DoorState>(state.mDoorState),
                "Stock door custom-state restoration changed motion");
            ESM::DoorState result;
            result.blank();
            ptr.getCellRef().writeState(result);
            ptr.getRefData().write(result, Compiler::Locals{});
            ptr.getClass().writeAdditionalState(ptr, result);
            return result;
        }

        void writePlugin(const std::filesystem::path& path, bool patch)
        {
            std::ofstream stream(path, std::ios::binary);
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            ESM::ESMWriter out;
            out.setVersion();
            out.setFormatVersion(ESM::DefaultFormatVersion);
            out.setType(patch ? 0 : 1);
            if (patch) out.addMaster("Base.esm", 0);
            out.save(stream);
            if (!patch)
            {
                // ESMStore's normal player-record validation requires these,
                // even though this fixture has no placed actors or inventories.
                ESM::Class klass;
                klass.blank();
                klass.mId = ESM::RefId::stringRefId("door_test_class");
                out.startRecord(ESM::REC_CLAS, 0);
                klass.save(out);
                out.endRecord(ESM::REC_CLAS);
                ESM::Race race;
                race.blank();
                race.mId = ESM::RefId::stringRefId("door_test_race");
                out.startRecord(ESM::REC_RACE, 0);
                race.save(out);
                out.endRecord(ESM::REC_RACE);
            }
            auto base = baseDoor();
            if (patch) base.mOpenSound = ESM::RefId::stringRefId("override_open");
            out.startRecord(ESM::REC_DOOR, 0);
            base.save(out);
            out.endRecord(ESM::REC_DOOR);
            ESM::Cell cell;
            cell.blank();
            cell.mName = "Door test";
            cell.mData.mFlags = ESM::Cell::Interior;
            cell.updateId();
            out.startRecord(ESM::REC_CELL, 0);
            cell.save(out);
            for (uint32_t i : {17, 18, 19, 20})
            {
                auto ref = placement();
                ref.mRefNum = {i, patch ? 1 : 0};
                ref.mPos.pos[0] = patch ? 60.f : 30.f;
                if (i == 19) ref.mTeleport = true;
                if (i == 20) ref.mIsLocked = true; // Zero-level lock is still locked.
                ref.save(out, false, false, patch && i == 18);
            }
            out.endRecord(ESM::REC_CELL);
            out.close();
        }
    }

    void checkDoorCodec()
    {
        auto ref = placement();
        ref.mGlobalVariable = std::string(256, 'g'); // Exercise non-SSO bound text.
        DoorBinding door(baseDoor(), ref);
        MWWorld::ESMStore store;
        const std::array<EquipmentEnvelope, 2> envelopes{{{"door-codec", {1, 2, 3}, {1, -1}},
            {"door-codec", {1, 2, 3}, {2, -1}}}};
        const std::array<EquipmentBindings, 2> bindings{{{envelopes[0], store, {}}, {envelopes[1], store, {}}}};
        EquipmentSessionValues values;
        values.mRevision = 7;
        for (size_t i = 0; i < 2; ++i)
        {
            values.mActors[i].mActor = envelopes[i].mActor;
            values.mActors[i].mLastGenerated = {2, -1};
        }
        values.mWorldItems = values.mActors[0];
        EquipmentBytes legacy;
        encodeEquipmentSession(values, bindings, legacy, {}, &bindings[0]);
        require(legacy[7] == '4', "Legacy world session version changed");
        const auto initial = door.door().initialState();
        const auto moving = door.door().advance(door.door().activate(initial).mState, .3f,
            [](const auto&, float) { return false; }).mState;
        for (int direction = 0; direction <= 2; ++direction)
        {
            auto state = moving;
            state.mDoorState = direction;
            values.mDoor = std::make_shared<const ESM::DoorState>(state);
            EquipmentBytes image;
            encodeEquipmentSession(values, bindings, image, {}, &bindings[0], &door);
            EquipmentSessionValues decoded;
            decodeEquipmentSession(image, bindings, decoded, {}, &bindings[0], &door);
            require(decoded.mDoor && save(*decoded.mDoor) == save(state) && decoded.mRevision == 7,
                "Door session lost partial position or motion direction");
            EquipmentBytes roundTrip;
            encodeEquipmentSession(decoded, bindings, roundTrip, {}, &bindings[0], &door);
            require(roundTrip == image, "Door session round trip changed bytes");
        }
        for (auto state : {initial, door.door().advance(door.door().activate(initial).mState, 1.f,
                 [](const auto&, float) { return false; }).mState})
        {
            EquipmentBytes bytes;
            encodeDoor(state, door, bytes);
            require(save(*decodeDoor(bytes, door)) == save(state), "Door endpoint/default state changed");
        }
        values.mDoor = std::make_shared<const ESM::DoorState>(moving);
        EquipmentBytes image, bytes;
        encodeEquipmentSession(values, bindings, image, {}, &bindings[0], &door);
        encodeDoor(moving, door, bytes);
        EquipmentSessionValues retained = values;
        const auto* previous = retained.mDoor.get();
        const auto reject = [&](std::span<const char> bad) {
            MWWorld::Testing::Allocations::Trace trace;
            bool failed = false;
            {
                MWWorld::Testing::Allocations::Observe observe(trace);
                try { decodeEquipmentSession(bad, bindings, retained, {}, &bindings[0], &door); }
                catch (const std::invalid_argument&) { failed = true; }
            }
            require(failed && retained.mDoor.get() == previous && retained.mRevision == 7,
                "Malformed door session changed retained output");
            require(trace.mTotal <= 1, "Door preflight allocated before rejection");
        };
        for (size_t end = 0; end < image.size(); ++end) reject(std::span(image).first(end));
        auto bad = image; bad.push_back(0); reject(bad);
        for (size_t offset : {size_t(0), size_t(16), size_t(48)})
        {
            bad = image; bad[offset] = char(255); reject(bad);
        }
        const auto start = image.size() - bytes.size();
        const auto field = [&](std::string_view tag) {
            const auto it = std::search(bytes.begin(), bytes.end(), tag.begin(), tag.end());
            require(it != bytes.end(), "Door codec fixture field missing");
            return size_t(it - bytes.begin());
        };
        const auto put32 = [](auto& target, size_t offset, uint32_t number) {
            for (size_t i = 0; i < 4; ++i) target[offset + i] = char(number >> (8 * i));
        };
        for (auto tag : {"FORM", "HEDR", "DOOR", "FRMR", "NAME", "XSCL", "ANAM", "BNAM", "CNAM", "DATA", "POS_", "ANIM"})
        {
            bad = image; bad[start + field(tag)] ^= 1; reject(bad);
            bad = image; put32(bad, start + field(tag) + 4, UINT32_MAX); reject(bad);
        }
        bad = image; bad[start + field("NAME") + 9] ^= 1; reject(bad);
        bad = image; bad[start + field("FRMR") + 8] ^= 1; reject(bad);
        for (uint32_t direction : {0u, 3u, UINT32_MAX})
        {
            bad = image; put32(bad, start + field("ANIM") + 8, direction); reject(bad);
        }
        for (size_t component = 0; component < 6; ++component)
        {
            bad = image;
            put32(bad, start + field("POS_") + 8 + 4 * component, std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN()));
            reject(bad);
        }
        for (float angle : {ref.mPos.rot[2] - .1f, ref.mPos.rot[2] + 2.f})
        {
            bad = image; put32(bad, start + field("POS_") + 28, std::bit_cast<uint32_t>(angle)); reject(bad);
        }
        // Stock can encode unsupported locals/flags; our bounded domain must not.
        for (int kind = 0; kind < 4; ++kind)
        {
            auto state = moving;
            if (kind == 0) state.mFlags = 1;
            if (kind == 1) state.mEnabled = 0;
            if (kind == 2) state.mHasCustomState = false;
            if (kind == 3) state.mRef.mRefNum.mIndex++;
            const auto raw = save(state);
            bad.assign(image.begin(), image.begin() + start);
            bad.insert(bad.end(), raw.begin(), raw.end());
            put32(bad, 48, uint32_t(raw.size()));
            reject(bad);
        }
        auto changed = ref; changed.mRefNum.mIndex++;
        DoorBinding other(baseDoor(), changed);
        rejects([&] { decodeEquipmentSession(image, bindings, retained, {}, &bindings[0], &other); },
            "Different door placement accepted saved state");
        rejects([&] { decodeEquipmentSession(image, bindings, retained, {}, &bindings[0]); }, "V8 accepted V9 state");
        rejects([&] { decodeEquipmentSession(legacy, bindings, retained, {}, &bindings[0], &door); }, "V9 silently upgraded V8");
        auto wrongEnvelopes = envelopes;
        wrongEnvelopes[0].mContent[0]++;
        const std::array<EquipmentBindings, 2> wrongBindings{{{wrongEnvelopes[0], store, {}}, {wrongEnvelopes[1], store, {}}}};
        rejects([&] { decodeEquipmentSession(image, wrongBindings, retained, {}, &wrongBindings[0], &door); },
            "Door session accepted mismatched content");
        require(retained.mDoor.get() == previous, "Binding rejection changed output");
        decodeEquipmentSession(legacy, bindings, retained, {}, &bindings[0]);
        EquipmentBytes legacyAgain;
        encodeEquipmentSession(retained, bindings, legacyAgain, {}, &bindings[0]);
        require(legacyAgain == legacy && !retained.mDoor, "Legacy campaign bytes changed");
        std::cout << "door codec: stock fields, endpoints/partial/idle motion, session v5, bounded allocation-free rejection, content binding, unchanged v4\n";
    }

    void checkOrdinaryDoor()
    {
        MWClass::registerClasses();
        using Motion = MWWorld::DoorState;
        const float quarterTurn = osg::DegreesToRadians(90.f);
        const auto base = baseDoor();
        const auto placed = placement();
        const OrdinaryDoor door(base, placed);
        const auto closed = door.initialState();
        const auto closedBytes = save(closed);
        const auto clear = [](const ESM::Position&, float) { return false; };
        require(MWWorld::activatedDoorState(Motion::Idle, 0, 0) == Motion::Opening
                && MWWorld::activatedDoorState(Motion::Idle, 0, .1f) == Motion::Closing
                && MWWorld::activatedDoorState(Motion::Opening, 0, .1f) == Motion::Closing
                && MWWorld::activatedDoorState(Motion::Closing, 0, .1f) == Motion::Opening,
            "Door activation lost closed/open/partial/reversal semantics");

        const auto opening = door.activate(closed);
        require(opening.mState.mDoorState == 1 && opening.mPlaySound == base.mOpenSound
                && opening.mFadeSound == base.mCloseSound && opening.mSoundOffset == 0,
            "Closed activation lost opening sound/state");
        const auto quarter = door.advance(opening.mState, .25f, clear);
        near(quarter.mState.mPosition.rot[2], 5.f + quarterTurn * .25f, "Door did not turn 90 degrees/second");
        require(quarter.mState.mDoorState == 1 && quarter.mState.mPosition.pos[0] == 30
                && quarter.mState.mPosition.rot[0] == .1f && quarter.mState.mPosition.rot[1] == -.2f,
            "Door movement changed its authored transform");
        const auto closing = door.activate(quarter.mState);
        require(closing.mState.mDoorState == 2 && closing.mPlaySound == base.mCloseSound
                && closing.mFadeSound == base.mOpenSound, "Partial activation did not reverse");
        near(closing.mSoundOffset, .75f, "Closing sound offset lost partial progress");
        const auto reopening = door.activate(closing.mState);
        require(reopening.mState.mDoorState == 1, "Closing door did not reverse to opening");
        near(reopening.mSoundOffset, .25f, "Opening sound offset lost partial progress");

        bool queried = false;
        const auto blocked = door.advance(closing.mState, .1f, [&](const ESM::Position& candidate, float delta) {
            queried = true;
            near(candidate.rot[2], 5.f + quarterTurn * .15f, "Collision query saw the old door position");
            near(delta, -quarterTurn * .1f, "Collision query lost stock signed motion delta");
            return true;
        });
        require(queried && save(blocked.mState) == save(closing.mState) && blocked.mStopSound == base.mCloseSound,
            "Actor collision did not preserve the complete previous transform/motion and stop sound");
        const auto blockedOpen = door.advance(opening.mState, 1, [](const auto&, float) { return true; });
        require(save(blockedOpen.mState) == save(opening.mState) && blockedOpen.mStopSound == base.mOpenSound,
            "Collision at open endpoint incorrectly completed motion");
        const auto fullyClosed = door.advance(blocked.mState, 1, clear);
        require(fullyClosed.mState.mDoorState == 0 && fullyClosed.mState.mPosition == placed.mPos,
            "Clear closing step did not clamp/finish at authored angle");
        const auto fullyOpen = door.advance(quarter.mState, 1, clear);
        near(fullyOpen.mState.mPosition.rot[2], 5.f + quarterTurn, "Open endpoint wrapped or overshot");
        require(fullyOpen.mState.mDoorState == 0, "Open endpoint must be idle");
        require(save(door.advance(fullyOpen.mState, 1, {}).mState) == save(fullyOpen.mState),
            "Idle open door was incorrectly advanced/closed");
        require(door.activate(fullyOpen.mState).mState.mDoorState == 2, "Idle open door did not close on activation");
        auto partialIdle = quarter.mState;
        partialIdle.mDoorState = 0;
        require(door.activate(partialIdle).mState.mDoorState == 2, "Idle partial door did not close");
        near(MWWorld::doorMotion(Motion::Idle, 5, 5 + quarterTurn, 1).mTargetAngle, 5,
            "Explicit stock Idle close/snap behavior changed");
        near(MWWorld::doorMotion(Motion::Opening, -2, -2, 4).mTargetAngle, -2 + quarterTurn,
            "Stock long-frame clamp changed");
        near(MWWorld::doorSoundOffset(Motion::Closing, 0, quarterTurn + .1f), 0,
            "Stock closing sound offset clamp changed");
        require(MWWorld::doorContactBlocks(.25f, {}, {-1,0,0}, {0,1,0})
                && !MWWorld::doorContactBlocks(.25f, {}, {-1,0,0}, {0,-1,0})
                && !MWWorld::doorContactBlocks(-.25f, {}, {-1,0,0}, {0,1,0})
                && MWWorld::doorContactBlocks(.25f, {}, {-1,0,0}, {0,0,1}),
            "Stock actor-contact direction rule changed");

        for (const auto& state : {closed, opening.mState, quarter.mState, closing.mState,
                 partialIdle, fullyOpen.mState, fullyClosed.mState})
        {
            const auto bytes = save(state);
            const auto restored = load(bytes);
            door.validate(restored);
            const auto nativeFields = stockFields(base, restored);
            door.validate(nativeFields);
            require(save(nativeFields) == bytes, "Stock door position/custom-state field round trip changed bytes");
            require(save(door.advance(restored, .2f, clear).mState)
                    == save(door.advance(state, .2f, clear).mState),
                "Restored moving door did not continue at the same angle/direction");
        }

        // No prepared operation mutates its input; discard and query failure
        // leave the same source bytes. This is not a durability-commit test.
        rejects([&] { door.advance(opening.mState, .1f, {}); }, "Absent collision query accepted");
        for (float bad : {-1.f, 1.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            rejects([&] { door.advance(opening.mState, bad, clear); }, "Invalid door duration accepted");
        rejects([&] {
            door.advance(opening.mState, .1f, [](const auto&, float) -> bool {
                throw std::invalid_argument("synthetic collision query failure");
            });
        }, "Collision query failure swallowed");
        require(save(closed) == closedBytes && opening.mState.mPosition == placed.mPos,
            "Door preparation mutated its source");

        const std::function<void(ESM::DoorState&)> corruptions[] = {
            [](auto& s) { s.mDoorState = -1; }, [](auto& s) { s.mDoorState = 3; },
            [](auto& s) { s.mHasCustomState = false; }, [](auto& s) { s.mEnabled = 0; },
            [](auto& s) { s.mFlags = 1; }, [](auto& s) { s.mHasLocals = 1; },
            [](auto& s) { s.mLuaScripts.mScripts.emplace_back(); },
            [](auto& s) { s.mAnimationState.mScriptedAnims.emplace_back(); },
            [](auto& s) { s.mVersion = 12345; }, [](auto& s) { s.mRef.mRefNum.mIndex++; },
            [](auto& s) { s.mRef.mScale = .75f; }, [](auto& s) { s.mRef.mIsLocked = true; },
            [](auto& s) { s.mRef.mPos.rot[2] += .1f; }, [](auto& s) { s.mPosition.pos[0]++; },
            [](auto& s) { s.mPosition.rot[0]++; }, [](auto& s) { s.mPosition.rot[2] = 4.9f; },
            [](auto& s) { s.mPosition.rot[2] = 7.f; },
            [](auto& s) { s.mPosition.rot[2] = std::numeric_limits<float>::quiet_NaN(); }
        };
        for (const auto& corrupt : corruptions)
        {
            auto bad = opening.mState;
            corrupt(bad);
            bool called = false;
            rejects([&] { door.activate(bad); }, "Invalid saved door activated");
            rejects([&] { door.advance(bad, .1f, [&](const auto&, float) { called = true; return false; }); },
                "Invalid saved door advanced");
            require(!called, "Invalid saved door reached a collision query");
        }
        // ESM's loader logs an invalid ANIM value but retains it. The native
        // boundary must reject it rather than cast it into a live motion state.
        auto corruptBytes = save(opening.mState);
        const auto anim = corruptBytes.rfind("ANIM");
        require(anim != std::string::npos && anim + 12 == corruptBytes.size(), "Door ANIM field missing");
        corruptBytes[anim + 8] = 3;
        const auto badLoaded = load(corruptBytes);
        rejects([&] { door.validate(badLoaded); }, "Invalid serialized ANIM accepted by native boundary");

        for (int fault = 0; fault < 9; ++fault)
        {
            auto invalidBase = base;
            auto invalidRef = placed;
            switch (fault)
            {
                case 0: invalidBase.mScript = ESM::RefId::stringRefId("script"); break;
                case 1: invalidRef.mTeleport = true; break;
                case 2: invalidRef.mIsLocked = true; break;
                case 3: invalidRef.mTrap = ESM::RefId::stringRefId("trap"); break;
                case 4: invalidRef.mKey = ESM::RefId::stringRefId("key"); break;
                case 5: invalidRef.mRefNum.mContentFile = -1; break;
                case 6: invalidRef.mCount = 2; break;
                case 7: invalidRef.mPos.rot[2] = std::numeric_limits<float>::infinity(); break;
                case 8: invalidRef.mGlobalVariable.assign(257, 'x'); break;
            }
            rejects([&] { OrdinaryDoor unsupported(invalidBase, invalidRef); }, "Unsupported door placement accepted");
        }
    }

    void checkPlacedDoor(const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "Door test scratch already exists");
        writePlugin(scratch / "Base.esm", false);
        writePlugin(scratch / "Patch.esp", true);
        { std::ofstream file(scratch / "empty.omwscripts"); file << "# synthetic empty script list\n"; }
        LoadoutOptions options;
        options.mDataPaths = {scratch};
        options.mContent = {"Base.esm", "empty.omwscripts", "Patch.esp"};
        options.mEncoding = "win1252";
        Loadout loadout(options);
        const auto selected = loadout.resolveDoor("Door test", "BASE.ESM", 17);
        require(selected.mRef.mPos.pos[0] == 60 && selected.mRef.mRefNum == ESM::RefNum{17, 0}
                && selected.mIdentity == (MWWorld::PlacedRefTag | 17),
            "Door discovery lost the winning placement or stable originating identity");
        const auto& base = *loadout.store().get<ESM::Door>().find(selected.mRef.mRefID);
        const OrdinaryDoor door(base, selected.mRef);
        require(door.activate(door.initialState()).mPlaySound == ESM::RefId::stringRefId("override_open"),
            "Door discovery ignored the winning base record");
        for (uint32_t id : {18, 19, 20, 999})
            rejects([&] { loadout.resolveDoor("Door test", "Base.esm", id); },
                "Deleted, teleport, zero-level locked or missing door accepted");
        rejects([&] { loadout.resolveDoor("Door test", "Patch.esp", 17); }, "Origin plugin mismatch accepted");
        rejects([&] { loadout.resolveDoor(std::string(257, 'x'), "Base.esm", 17); }, "Oversized cell name accepted");
        const std::vector<std::string> desktopFiles{"builtin.omwscripts", "Base.esm", "empty.omwscripts", "Patch.esp"};
        require(MWWorld::placedRefId({17, 1}, desktopFiles) == selected.mIdentity,
            "Client builtin script slot changed door wire identity");
        { std::ofstream file(scratch / "empty.omwscripts"); file << "GLOBAL: door.lua\n"; }
        rejects([&] { loadout.resolveDoor("Door test", "Base.esm", 17); }, "Uncomposed Lua door services accepted");
    }

    void checkDoorLoadout(const std::filesystem::path& configDirectory, std::string_view cell)
    {
        const auto config = configDirectory.string();
        const char* args[]{"door-test", "--config", config.c_str()};
        Loadout loadout(readLoadoutOptions(3, args));
        MWClass::registerClasses();
        std::optional<ESM::CellRef> candidate;
        {
            MWWorld::WorldModel world(loadout.store(), loadout.readers(), 1);
            world.getInterior(cell).forEachType<ESM::Door>([&](const MWWorld::Ptr& ptr) {
                if (candidate || !ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
                ESM::DoorState state;
                ptr.getCellRef().writeState(state);
                try { OrdinaryDoor supported(*ptr.get<ESM::Door>()->mBase, state.mRef); }
                catch (const std::invalid_argument&) { return true; }
                candidate = state.mRef;
                return true;
            });
        }
        require(candidate.has_value(), "Real interior has no supported ordinary door");
        const auto selected = loadout.resolveDoor(cell,
            loadout.options().mContent.at(candidate->mRefNum.mContentFile), candidate->mRefNum.mIndex);
        const auto& base = *loadout.store().get<ESM::Door>().find(selected.mRef.mRefID);
        const OrdinaryDoor door(base, selected.mRef);
        const auto clear = [](const ESM::Position&, float) { return false; };
        const auto initial = door.initialState();
        const auto moving = door.advance(door.activate(initial).mState, .375f, clear);
        const auto restored = load(save(stockFields(base, moving.mState)));
        door.validate(restored);
        const auto opened = door.advance(restored, 1.f, clear);
        require(opened.mState.mDoorState == 0 && opened.mState.mPosition.rot[2] > initial.mPosition.rot[2],
            "Real-content restored door failed to finish opening");
        const auto closed = door.advance(door.activate(opened.mState).mState, 1.f, clear);
        require(closed.mState.mDoorState == 0 && closed.mState.mPosition == initial.mPosition,
            "Real-content door failed to close to its authored transform");
        require(save(door.advance(restored, .25f, clear).mState)
                == save(door.advance(moving.mState, .25f, clear).mState),
            "Real-content door save changed resumed motion");
        std::cout << "door: " << selected.mPlugin << ':' << selected.mRef.mRefNum.mIndex
                  << " record=" << base.mId << " cell=" << cell << " identity=" << selected.mIdentity
                  << "\nReal content + stock ESM fields; synthetic clear collision query; no live clients\n";
    }
}
