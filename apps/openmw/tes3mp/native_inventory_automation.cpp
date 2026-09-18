#include "desktop_automation.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/scene.hpp"
#include "../mwgui/container.hpp"
#include "../mwgui/countdialog.hpp"
#include "../mwgui/inventorywindow.hpp"
#include "../mwgui/itemview.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/placedrefid.hpp"
#include "../mwworld/inventoryrecordid.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/manualref.hpp"
#include <cmath>
#include <iomanip>
#include <bit>
#include <map>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loaddoor.hpp>

#include <stdexcept>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        // The capture resolves the server's placed container (barrel or chest),
        // real GUI models and their click/count/drop delegates. No inventory command
        // or local gameplay mutation is synthesized by the evidence driver.
        MWWorld::Ptr placedContainer(ContainerId id)
        {
            MWWorld::Ptr result;
            const auto expected = MWWorld::localPlacedRef(id.value(),
                MWBase::Environment::get().getWorld()->getContentFiles());
            if (!expected) return result;
            auto* cell = MWBase::Environment::get().getWorldScene()->getCurrentCell();
            if (cell)
                cell->forEachType<ESM::Container>([&](const MWWorld::Ptr& ptr) {
                    const auto ref = ptr.getCellRef().getRefNum();
                    if (ref == *expected)
                        result = ptr;
                    return result.isEmpty();
                });
            return result;
        }

        MWGui::ContainerWindow* containerWindow()
        {
            for (auto* window : MWBase::Environment::get().getWindowManager()->getGuiModeWindows(MWGui::GM_Container))
                if (auto* container = dynamic_cast<MWGui::ContainerWindow*>(window))
                    return container;
            throw std::runtime_error("Native evidence container window missing");
        }

        using ItemKey = std::tuple<uint64_t, uint32_t, uint32_t, uint64_t>;
        ItemKey key(const CanonicalItemStack& item)
        {
            return {item.prototypeId.value(), item.condition, item.enchantmentCharge,
                item.soulPrototype ? item.soulPrototype->value() : 0};
        }
        ItemKey key(const MWWorld::Ptr& item)
        {
            const auto& ref = item.getCellRef();
            return {MWWorld::inventoryRecordId(ref.getRefId()), std::bit_cast<uint32_t>(ref.getCharge()),
                std::bit_cast<uint32_t>(ref.getEnchantmentCharge()),
                ref.getSoul().empty() ? 0 : MWWorld::inventoryRecordId(ref.getSoul())};
        }
        bool matches(MWGui::ItemView& view, const std::vector<CanonicalItemStack>& expected)
        {
            view.update();
            std::map<ItemKey, uint64_t> observed, wanted;
            auto* model = view.getModel();
            for (size_t i = 0; i < model->getItemCount(); ++i)
            {
                const auto& item = model->getItem(int(i));
                observed[key(item.mBase)] += item.mCount;
            }
            for (const auto& item : expected) wanted[key(item)] += item.count;
            if (observed != wanted)
            {
                Log(Debug::Error) << "Native GUI mismatch: observed groups=" << observed.size() << " expected=" << wanted.size();
                for (const auto& [item, count] : observed)
                    Log(Debug::Error) << "GUI item=" << std::get<0>(item) << " count=" << count
                        << " condition=" << std::get<1>(item) << " charge=" << std::get<2>(item);
                for (const auto& [item, count] : wanted)
                    Log(Debug::Error) << "Expected item=" << std::get<0>(item) << " count=" << count
                        << " condition=" << std::get<1>(item) << " charge=" << std::get<2>(item);
            }
            return observed == wanted;
        }

        void clickItem(MWGui::ItemView& view, const CanonicalItemStack& stack)
        {
            view.update();
            auto* model = view.getModel();
            for (std::size_t i = 0; i < model->getItemCount(); ++i)
            {
                const auto item = model->getItem(static_cast<int>(i));
                if (key(item.mBase) != key(stack))
                    continue;
                view.eventItemClicked(static_cast<int>(i));
                if (item.mCount > 1)
                {
                    auto* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
                    if (!dialog->isVisible())
                        throw std::runtime_error("Native evidence count dialog missing");
                    dialog->setCount(int(stack.count));
                    auto* ok = dialog->getWidget("OkButton");
                    ok->eventMouseButtonClick(ok);
                }
                return;
            }
            throw std::runtime_error("Native evidence item missing from GUI");
        }
    }

    bool DesktopAutomation::nativeInventoryRole() const noexcept
    {
        return mRole == DesktopAutomationRole::NativePut || mRole == DesktopAutomationRole::NativeTake
            || mRole == DesktopAutomationRole::NativeTraversal
            || mRole == DesktopAutomationRole::NativeRecoverOne || mRole == DesktopAutomationRole::NativeRecoverTwo;
    }

    std::optional<InventoryTransactionCapture> DesktopAutomation::captureInventoryTransaction() noexcept
    {
        auto captured = nativeInventoryRole() && mDesktopInput ? mDesktopInput->captureInventoryTransaction() : std::nullopt;
        if (captured)
            writeNativeInventory(captured->kind == InventoryTransactionKind::PutIntoContainer
                ? "native_put_intent_captured" : "native_take_intent_captured");
        return captured;
    }

    void DesktopAutomation::clearSessionState() noexcept
    {
        if (nativeInventoryRole() && mDesktopInput)
            mDesktopInput->clearSessionState();
    }

    void DesktopAutomation::writeNativeInventory(std::string_view event)
    {
        if (!mOutput || mEvidenceEvents >= MaximumEvidenceEvents)
            throw std::runtime_error("Native evidence event budget exhausted");
        mOutput << "{\"event\":\"" << event << "\",\"role\":\"" << roleName()
                << "\",\"player_count\":" << mNativePlayerCount.value_or(0)
                << ",\"container_count\":" << mNativeContainerCount.value_or(0)
                << ",\"container_id\":" << (mNativeContainerId ? mNativeContainerId->value() : 0)
                << ",\"inventory_revision\":" << mNativeRevision << ",\"resumes\":" << mResumes;
        const auto write = [&](const char* name, const auto& stacks) {
            mOutput << ",\"" << name << "\":[";
            bool comma = false;
            for (const auto& item : stacks)
            {
                if (comma) mOutput << ',';
                comma = true;
                mOutput << "{\"stack\":" << item.stackId.value() << ",\"item\":" << item.prototypeId.value()
                    << ",\"count\":" << item.count << ",\"condition\":" << item.condition
                    << ",\"charge\":" << item.enchantmentCharge << ",\"soul\":"
                    << (item.soulPrototype ? item.soulPrototype->value() : 0) << '}';
            }
            mOutput << ']';
        };
        write("player", mNativePlayerStacks); write("container", mNativeContainerStacks);
        mOutput << "}\n";
        mOutput.flush();
        ++mEvidenceEvents;
    }

    void DesktopAutomation::writeNativeTraversal(std::string_view event)
    {
        if (!mOutput || mEvidenceEvents >= MaximumEvidenceEvents || !mTraversalGround
            || !mTraversalGround->cell.asInterior())
            throw std::runtime_error("Traversal evidence unavailable or exhausted");
        auto world = MWBase::Environment::get().getWorld();
        const auto player = world->getPlayerPtr();
        auto* cell = player.getCell();
        const auto focus = world->getFocusObject();
        std::map<uint64_t, uint64_t> visible, expected;
        const auto add = [](auto& counts, const MWWorld::Ptr& ptr) {
            // Stock gold copies become coin counts in the rendered world.
            const bool gold = ptr.getClass().isGold(ptr);
            const auto id = gold ? ESM::RefId::stringRefId("gold_001") : ptr.getCellRef().getRefId();
            counts[MWWorld::inventoryRecordId(id)] += uint64_t(ptr.getCellRef().getCount())
                * (gold ? ptr.getClass().getValue(ptr) : 1);
        };
        size_t originals = 0;
        cell->forEach([&](const MWWorld::Ptr& ptr) {
            if (!ptr.getRefData().getBaseNode() || !ptr.getRefData().isEnabled()
                || ptr.getCellRef().getCount() <= 0 || !MWWorld::ContainerStore::isStorableType(ptr.getType()))
                return true;
            add(visible, ptr);
            const auto placed = MWWorld::placedRefId(ptr.getCellRef().getRefNum(), world->getContentFiles());
            if (placed && std::ranges::binary_search(mTraversalGround->nativePlacements, *placed)) ++originals;
            return true;
        });
        if (!mTraversalGround->items.empty())
        {
            const auto records = MWWorld::inventoryRecords(*MWBase::Environment::get().getESMStore());
            for (const auto& item : mTraversalGround->items)
            {
                MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(),
                    records.at(item.stack.prototypeId.value()), static_cast<int>(item.stack.count));
                add(expected, ref.getPtr());
            }
        }
        const auto& pos = player.getRefData().getPosition();
        mOutput << "{\"event\":\"traversal_" << event << "\",\"sequence\":" << mTraversalSequence
                << ",\"cell\":" << std::quoted(cell->getCell()->getId().toString())
                << ",\"authority_cell\":" << (mSelfCell && mSelfCell->asInterior()
                    ? mSelfCell->asInterior()->cellSpace().value() : 0)
                << ",\"baseline_cell\":" << mTraversalGround->cell.asInterior()->cellSpace().value()
                << ",\"resumes\":" << mResumes << ",\"position\":[" << pos.pos[0] << ',' << pos.pos[1] << ',' << pos.pos[2]
                << "],\"focus\":" << std::quoted(focus.isEmpty() ? std::string{} : focus.getCellRef().getRefId().toString())
                << ",\"ground_matches\":" << (visible == expected ? "true" : "false")
                << ",\"visible_originals\":" << originals << ",\"ground\":[";
        bool comma = false;
        for (const auto& item : mTraversalGround->items)
        {
            if (comma) mOutput << ',';
            comma = true;
            mOutput << "{\"stack\":" << item.stack.stackId.value() << ",\"item\":"
                    << item.stack.prototypeId.value() << ",\"count\":" << item.stack.count << '}';
        }
        mOutput << "],\"teleports\":" << mTraversalGround->teleportDoors.size();
        if (mTraversalGround->door)
        {
            mOutput << ",\"door_angle\":" << mTraversalGround->door->angle
                    << ",\"door_direction\":" << unsigned(mTraversalGround->door->direction)
                    << ",\"door_blocked\":" << (mTraversalGround->door->blocked ? "true" : "false");
            const auto ref = MWWorld::localPlacedRef(mTraversalGround->door->placement, world->getContentFiles());
            cell->forEachType<ESM::Door>([&](const MWWorld::Ptr& ptr) {
                if (ref && ptr.getCellRef().getRefNum() == *ref)
                    mOutput << ",\"rendered_door_angle\":" << ptr.getRefData().getPosition().rot[2];
                return true;
            });
        }
        mOutput << "}\n";
        mOutput.flush();
        ++mEvidenceEvents;
        writeNativeInventory("traversal_inventory");
        if (event == "observe" && (visible != expected || originals != 0))
            throw std::runtime_error("Traversal rendered ground differs from committed baseline");
    }

    void DesktopAutomation::advanceNativeTraversal(MonotonicInstant now)
    {
        // Test-build-only, bounded local control. Gameplay actions use the same
        // Player activation and GUI delegates as a desktop user. Pose is explicit
        // same-cell test setup; it never changes cell or mutates server state.
        if (!mTraversalGround || !mNativePlayerCount || !mSnapshots || !mDesktopInput) return;
        std::error_code error;
        const auto size = std::filesystem::file_size(mTraversalControl, error);
        if (error) return;
        if (size > 256) throw std::runtime_error("Traversal control exceeds 256 bytes");
        std::ifstream file(mTraversalControl);
        std::uint64_t sequence;
        std::string action;
        if (!(file >> sequence >> action) || sequence <= mTraversalSequence) return;
        if (sequence != mTraversalSequence + 1 || action.size() > 16)
            throw std::runtime_error("Traversal control sequence/action invalid");
        float x = 0, y = 0, z = 0, pitch = 0, yaw = 0;
        std::string record, trailing;
        if (action == "pose")
        {
            if (!(file >> x >> y >> z >> pitch >> yaw) || !std::isfinite(x) || !std::isfinite(y)
                || !std::isfinite(z) || !std::isfinite(pitch) || !std::isfinite(yaw)
                || std::abs(x) > 4096 || std::abs(y) > 4096 || std::abs(z) > 4096
                || std::abs(pitch) > 1.5f || std::abs(yaw) > 6.3f)
                throw std::runtime_error("Traversal setup pose invalid");
        }
        else if (action == "activate" || action == "put")
        {
            if (!(file >> std::quoted(record)) || record.size() > 64)
                throw std::runtime_error("Traversal record argument invalid");
        }
        if (file >> trailing) throw std::runtime_error("Trailing traversal control input");
        auto world = MWBase::Environment::get().getWorld();
        auto wm = MWBase::Environment::get().getWindowManager();
        if (action == "pose")
        {
            world->toggleVanityMode(false);
            world->moveObject(world->getPlayerPtr(), osg::Vec3f(x,y,z));
            world->rotateObject(world->getPlayerPtr(), osg::Vec3f(pitch,0,yaw));
        }
        else if (action == "activate")
        {
            if (wm->isGuiMode())
                throw std::runtime_error("Traversal activation requires game focus");
            const auto focus = world->getFocusObject();
            if (focus.isEmpty() || focus.getCellRef().getRefId() != ESM::RefId::stringRefId(record))
                throw std::runtime_error("Traversal activation focus differs from requested record");
            world->getPlayer().activate();
        }
        else if (action == "open")
        {
            if (!mNativeContainerId) throw std::runtime_error("Traversal shared container not bound");
            const auto ptr = placedContainer(*mNativeContainerId);
            if (ptr.isEmpty()) throw std::runtime_error("Traversal shared container missing");
            wm->pushGuiMode(MWGui::GM_Container, ptr);
        }
        else if (action == "takeall")
        {
            if (!wm->containsMode(MWGui::GM_Container)) throw std::runtime_error("Traversal loot window not open");
            auto* button = containerWindow()->getWidget("TakeButton");
            button->eventMouseButtonClick(button);
        }
        else if (action == "put")
        {
            if (!wm->containsMode(MWGui::GM_Container))
                throw std::runtime_error("Traversal put requires loot window and record");
            const auto id = MWWorld::inventoryRecordId(ESM::RefId::stringRefId(record));
            const auto stack = std::ranges::find_if(mNativePlayerStacks, [&](const auto& s) { return s.prototypeId.value() == id; });
            if (stack == mNativePlayerStacks.end()) throw std::runtime_error("Traversal put item missing");
            MWGui::ItemView* view = nullptr;
            wm->getInventoryWindow()->getWidget(view, "ItemView");
            clickItem(*view, *stack);
            containerWindow()->getItemView()->eventBackgroundClicked();
        }
        else if (action == "close")
        {
            if (wm->containsMode(MWGui::GM_Container)) wm->removeGuiMode(MWGui::GM_Container);
            if (wm->containsMode(MWGui::GM_Inventory)) wm->removeGuiMode(MWGui::GM_Inventory);
        }
        else if (action == "reconnect")
        {
            mReadyToDisconnect = true;
            mNextDisconnect = now;
        }
        else if (action == "observe")
        {
            if (wm->containsMode(MWGui::GM_Container))
            {
                MWGui::ItemView* view = nullptr;
                wm->getInventoryWindow()->getWidget(view, "ItemView");
                if (!matches(*view, mNativePlayerStacks)
                    || !matches(*containerWindow()->getItemView(), mNativeContainerStacks))
                    throw std::runtime_error("Traversal GUI differs from committed inventory");
            }
        }
        else throw std::runtime_error("Unknown traversal control action");
        mTraversalSequence = sequence;
        writeNativeTraversal(action);
    }

    void DesktopAutomation::advanceNativeInventory(MonotonicInstant now)
    {
        constexpr uint64_t Second = 1'000'000'000;
        if (now.nanoseconds() - mStartedAt->nanoseconds() > 60 * Second)
        { writeNativeInventory("native_timeout"); finish(false); return; }
        if (!mNativePlayerCount || !mNativeContainerCount || !mNativeContainerId || mSnapshots == 0) return;
        const auto ptr = placedContainer(*mNativeContainerId);
        if (ptr.isEmpty()) throw std::runtime_error("Native evidence placed container missing");
        auto wm = MWBase::Environment::get().getWindowManager();
        if (!wm->containsMode(MWGui::GM_Container)) wm->pushGuiMode(MWGui::GM_Container, ptr);
        auto* container = containerWindow();
        auto* inventory = wm->getInventoryWindow();
        MWGui::ItemView* playerView = nullptr;
        inventory->getWidget(playerView, "ItemView");
        auto* containerView = container->getItemView();
        if (!container->isVisible() || !inventory->isVisible()
            || !matches(*playerView, mNativePlayerStacks) || !matches(*containerView, mNativeContainerStacks))
            throw std::runtime_error("Native GUI items/counts/charge/souls differ from committed baseline");
        const bool first = mRole == DesktopAutomationRole::NativePut || mRole == DesktopAutomationRole::NativeRecoverOne;
        const bool recovery = mRole == DesktopAutomationRole::NativeRecoverOne || mRole == DesktopAutomationRole::NativeRecoverTwo;
        const auto changeStage = [&](unsigned value) { mNativeStage = value; mNativeStageAt = now; };
        const auto submit = [&](bool put, CanonicalItemStack stack) {
            mNativeSelected = stack;
            mNativeSubmittedRevision = mNativeRevision;
            clickItem(put ? *playerView : *containerView, stack);
            if (put) containerView->eventBackgroundClicked();
            writeNativeInventory(put ? "native_gui_put_submitted" : "native_gui_take_submitted");
        };
        const auto held = [&]() {
            const auto found = std::ranges::find(mNativePlayerStacks, mNativeSelected->prototypeId, &CanonicalItemStack::prototypeId);
            if (found == mNativePlayerStacks.end()) throw std::runtime_error("Taken native item missing");
            auto stack = *found; stack.count = mNativeSelected->count; return stack;
        };
        if (mNativeStage == 0)
        {
            mNativeInitialCount = *mNativeContainerCount;
            if (recovery && mNativeInitialCount != 0) throw std::runtime_error("Restart refilled the chest");
            if (!recovery && first && mNativeInitialCount == 0) throw std::runtime_error("Chest has no initial loot");
            writeNativeInventory(recovery ? "native_gui_recovered" : first ? "native_gui_initial" : "native_gui_late_join");
            changeStage(1); return;
        }
        const auto age = now.nanoseconds() - mNativeStageAt->nanoseconds();
        if (!recovery)
        {
            if (first && mNativeStage == 1 && age >= Second)
            { submit(false, mNativeContainerStacks.front()); changeStage(2); return; }
            if (first && mNativeStage == 2 && mNativeRevision > mNativeSubmittedRevision)
            { writeNativeInventory("native_gui_first_take_observed"); changeStage(3); return; }
            if (first && mNativeStage == 3 && mSawPeer && age >= 3 * Second)
            { submit(true, held()); changeStage(4); return; }
            if (first && mNativeStage == 4 && mNativeRevision > mNativeSubmittedRevision)
            { writeNativeInventory("native_gui_put_observed"); changeStage(5); return; }
            if (!first && mNativeStage == 1 && *mNativeContainerCount > mNativeInitialCount)
            { writeNativeInventory("native_gui_put_observed"); changeStage(5); return; }
            if (!first && mNativeStage == 5 && age >= Second && !mNativeContainerStacks.empty())
            { submit(false, mNativeContainerStacks.front()); changeStage(6); return; }
            if (!first && mNativeStage == 6 && mNativeRevision > mNativeSubmittedRevision)
            { writeNativeInventory("native_gui_take_observed"); changeStage(5); return; }
            if (mNativeStage == 5 && mNativeContainerStacks.empty())
            {
                mNativeExpectedPlayer = mNativePlayerStacks;
                writeNativeInventory("native_gui_emptied"); changeStage(9);
                mReadyToDisconnect = true;
                mNextDisconnect = MonotonicInstant::fromNanoseconds(now.nanoseconds() + Second);
                return;
            }
            if (mNativeStage == 9 && mResumes == 1 && mNativeInventoryAfterResume && age >= 3 * Second)
            {
                if (mNativePlayerStacks != mNativeExpectedPlayer || !mNativeContainerStacks.empty())
                    throw std::runtime_error("Reconnect changed item identities or refilled loot");
                writeNativeInventory("native_gui_resumed"); finish(mPlayerIdentityStable && mSawPeer);
            }
        }
        else
        {
            if (!first && mNativeStage == 1 && mSawPeer && age >= 2 * Second)
            {
                const auto shirt = MWWorld::inventoryRecordId(ESM::RefId::stringRefId("common_shirt_01"));
                const auto item = std::ranges::find_if(mNativePlayerStacks, [&](const auto& s) { return s.prototypeId.value() != shirt; });
                if (item == mNativePlayerStacks.end()) throw std::runtime_error("Recovered loot missing");
                submit(true, *item); changeStage(2); return;
            }
            if (first && mNativeStage == 1 && !mNativeContainerStacks.empty())
            { writeNativeInventory("native_gui_put_observed"); changeStage(2); return; }
            if (!first && mNativeStage == 2 && mNativeRevision > mNativeSubmittedRevision)
            { writeNativeInventory("native_gui_put_observed"); changeStage(3); return; }
            if (first && mNativeStage == 2 && age >= Second)
            { submit(false, mNativeContainerStacks.front()); changeStage(3); return; }
            if (mNativeStage == 3 && mNativeContainerStacks.empty() && mNativeRevision > mNativeSubmittedRevision)
            { writeNativeInventory("native_gui_take_observed"); changeStage(9); return; }
            if (mNativeStage == 9 && age >= 2 * Second)
            { writeNativeInventory("native_gui_restart_continuation"); finish(mPlayerIdentityStable && mSawPeer); }
        }
    }
}
