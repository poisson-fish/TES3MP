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

#include <stdexcept>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        // This capture deliberately uses the mapped Morrowind prison-ship barrel,
        // real GUI models and their click/count/drop delegates. No inventory command
        // or local gameplay mutation is synthesized by the evidence driver.
        MWWorld::Ptr barrel()
        {
            MWWorld::Ptr result;
            auto* cell = MWBase::Environment::get().getWorldScene()->getCurrentCell();
            if (cell)
                cell->forEachType<ESM::Container>([&](const MWWorld::Ptr& ptr) {
                    const auto ref = ptr.getCellRef().getRefNum();
                    if (ref.mIndex == 299164 && ref.mContentFile == 1
                        && ptr.getCellRef().getRefId() == ESM::RefId::stringRefId("barrel_01"))
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

        std::size_t shirtCount(MWGui::ItemView& view)
        {
            view.update();
            std::size_t total = 0;
            auto* model = view.getModel();
            for (std::size_t i = 0; i < model->getItemCount(); ++i)
            {
                const auto& item = model->getItem(static_cast<int>(i));
                if (item.mBase.getCellRef().getRefId() == ESM::RefId::stringRefId("common_shirt_01"))
                    total += item.mCount;
            }
            return total;
        }

        void clickShirt(MWGui::ItemView& view, int count)
        {
            view.update();
            auto* model = view.getModel();
            for (std::size_t i = 0; i < model->getItemCount(); ++i)
            {
                const auto item = model->getItem(static_cast<int>(i));
                if (item.mBase.getCellRef().getRefId() != ESM::RefId::stringRefId("common_shirt_01"))
                    continue;
                view.eventItemClicked(static_cast<int>(i));
                if (item.mCount > 1)
                {
                    auto* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
                    if (!dialog->isVisible())
                        throw std::runtime_error("Native evidence count dialog missing");
                    dialog->setCount(count);
                    auto* ok = dialog->getWidget("OkButton");
                    ok->eventMouseButtonClick(ok);
                }
                return;
            }
            throw std::runtime_error("Native evidence shirt missing from GUI");
        }
    }

    bool DesktopAutomation::nativeInventoryRole() const noexcept
    {
        return mRole == DesktopAutomationRole::NativePut || mRole == DesktopAutomationRole::NativeTake
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
                << "\",\"player_shirts\":" << mNativePlayerCount.value_or(0)
                << ",\"container_shirts\":" << mNativeContainerCount.value_or(0)
                << ",\"inventory_revision\":" << mNativeRevision << ",\"resumes\":" << mResumes << "}\n";
        mOutput.flush();
        ++mEvidenceEvents;
    }

    void DesktopAutomation::advanceNativeInventory(MonotonicInstant now)
    {
        constexpr std::uint64_t Second = 1'000'000'000;
        if (now.nanoseconds() - mStartedAt->nanoseconds() > 25 * Second)
        {
            writeNativeInventory("native_timeout");
            finish(false);
            return;
        }
        if (!mNativePlayerCount || !mNativeContainerCount || !mSawPeer || mSnapshots < 2)
            return;
        const auto ptr = barrel();
        if (ptr.isEmpty())
            throw std::runtime_error("Native evidence placed barrel missing");
        auto wm = MWBase::Environment::get().getWindowManager();
        if (!wm->containsMode(MWGui::GM_Container))
            wm->pushGuiMode(MWGui::GM_Container, ptr);
        auto* container = containerWindow();
        auto* inventory = wm->getInventoryWindow();
        MWGui::ItemView* playerView = nullptr;
        inventory->getWidget(playerView, "ItemView");
        auto* containerView = container->getItemView();
        if (!container->isVisible() || !inventory->isVisible())
            throw std::runtime_error("Native evidence GUI not visible");
        if (shirtCount(*playerView) != *mNativePlayerCount || shirtCount(*containerView) != *mNativeContainerCount)
            throw std::runtime_error("Native evidence GUI differs from committed baseline");

        const bool first = mRole == DesktopAutomationRole::NativePut || mRole == DesktopAutomationRole::NativeRecoverOne;
        const bool recovery = mRole == DesktopAutomationRole::NativeRecoverOne || mRole == DesktopAutomationRole::NativeRecoverTwo;
        if (mNativeStage == 0)
        {
            if (*mNativePlayerCount != (recovery ? (first ? 1u : 6u) : (first ? 3u : 5u))
                || *mNativeContainerCount != (recovery ? 1u : 0u))
                throw std::runtime_error("Native evidence initial inventory mismatch");
            writeNativeInventory(recovery ? "native_gui_recovered" : "native_gui_initial");
            mNativeStage = 1;
            mNativeStageAt = now;
            return;
        }
        const auto stageAge = now.nanoseconds() - mNativeStageAt->nanoseconds();
        if (mNativeStage == 1 && stageAge >= 2 * Second)
        {
            if (!recovery && first)
            {
                clickShirt(*playerView, 2);
                containerView->eventBackgroundClicked();
                writeNativeInventory("native_gui_put_submitted");
                mNativeStage = 2;
            }
            else if (!recovery && !first && *mNativeContainerCount == 2)
            {
                writeNativeInventory("native_gui_put_observed");
                mNativeStage = 3;
                mNativeStageAt = now;
                return;
            }
            else if (recovery && !first)
            {
                writeNativeInventory(recovery ? "native_gui_restart_take_ready" : "native_gui_put_observed");
                clickShirt(*containerView, 1);
                writeNativeInventory("native_gui_take_submitted");
                mNativeStage = 2;
            }
        }
        if (!recovery && !first && mNativeStage == 3 && stageAge >= Second)
        {
            clickShirt(*containerView, 1);
            writeNativeInventory("native_gui_take_submitted");
            mNativeStage = 2;
        }
        if (!recovery && first && mNativeStage == 2 && *mNativePlayerCount == 1 && *mNativeContainerCount == 2)
        {
            writeNativeInventory("native_gui_put_observed");
            mNativeStage = 3;
        }
        if (*mNativePlayerCount == (first ? 1u : (recovery ? 7u : 6u))
            && *mNativeContainerCount == (recovery ? 0u : 1u) && mNativeStage < 4)
        {
            writeNativeInventory("native_gui_take_observed");
            mNativeStage = 4;
            mNativeStageAt = now;
            if (!recovery)
            {
                mReadyToDisconnect = true;
                mNextDisconnect = MonotonicInstant::fromNanoseconds(now.nanoseconds() + Second);
            }
        }
        if (mNativeStage == 4 && (recovery || (mResumes == 1 && mNativeInventoryAfterResume))
            && *mNativePlayerCount == (first ? 1u : (recovery ? 7u : 6u))
            && *mNativeContainerCount == (recovery ? 0u : 1u)
            && now.nanoseconds() - mNativeStageAt->nanoseconds() >= 3 * Second)
        {
            writeNativeInventory(recovery ? "native_gui_restart_continuation" : "native_gui_resumed");
            finish(mPlayerIdentityStable);
        }
    }
}
