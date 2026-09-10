#include "profiledialog.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_TextBox.h>

#include "../tes3mp/player_profile_manager.hpp"

namespace MWGui
{
    ProfileDialog::ProfileDialog(TES3MP::OpenMWAdapter::PlayerProfileManager& manager, std::string profilePath)
        : WindowModal("openmw_profile_dialog.layout")
        , mManager(manager)
        , mProfilePath(std::move(profilePath))
    {
        getWidget(mProfileSelect, "ProfileSelect");
        getWidget(mUsername, "Username");
        getWidget(mPassword, "Password");
        getWidget(mStatusMessage, "StatusMessage");
        getWidget(mAdd, "Add");
        getWidget(mDelete, "Delete");
        getWidget(mClose, "Close");

        mPassword->setEditPassword(true);

        mProfileSelect->eventComboChangePosition += MyGUI::newDelegate(this, &ProfileDialog::onProfileSelected);
        mAdd->eventMouseButtonClick += MyGUI::newDelegate(this, &ProfileDialog::onAdd);
        mDelete->eventMouseButtonClick += MyGUI::newDelegate(this, &ProfileDialog::onDelete);
        mClose->eventMouseButtonClick += MyGUI::newDelegate(this, &ProfileDialog::onClose);

        mControllerButtons.mA = "#{Interface:OK}";
        mControllerButtons.mB = "#{Interface:Cancel}";

        refreshProfiles();
        center();
    }

    MyGUI::Widget* ProfileDialog::getDefaultKeyFocus()
    {
        return mUsername;
    }

    bool ProfileDialog::exit()
    {
        setVisible(false);
        return true;
    }

    void ProfileDialog::refreshProfiles()
    {
        mProfileSelect->removeAllItems();
        const auto& profiles = mManager.profiles();
        std::size_t activeIndex = MyGUI::ITEM_NONE;

        for (std::size_t index = 0; index < profiles.size(); ++index)
        {
            mProfileSelect->addItem(profiles[index].username);
            if (profiles[index].username == mManager.activeUsername())
                activeIndex = index;
        }

        if (activeIndex != MyGUI::ITEM_NONE)
        {
            mProfileSelect->setIndexSelected(activeIndex);
            const auto& active = profiles[activeIndex];
            mUsername->setCaption(active.username);
            mPassword->setCaption("");
            mDelete->setEnabled(true);
        }
        else if (!profiles.empty())
        {
            mProfileSelect->setIndexSelected(0);
            mManager.setActive(profiles[0].username);
            mUsername->setCaption(profiles[0].username);
            mPassword->setCaption("");
            mDelete->setEnabled(true);
        }
        else
        {
            mUsername->setCaption("");
            mPassword->setCaption("");
            mDelete->setEnabled(false);
        }
    }

    void ProfileDialog::onProfileSelected(MyGUI::ComboBox* sender, std::size_t index)
    {
        if (index == MyGUI::ITEM_NONE)
            return;

        const auto username = sender->getItemNameAt(index);
        const auto previous = mManager;
        mManager.setActive(username);
        if (const auto profile = mManager.activeProfile())
        {
            mUsername->setCaption(profile->username);
            mPassword->setCaption("");
            mStatusMessage->setCaption("Selected profile: " + profile->username);
            mDelete->setEnabled(true);
            if (!mManager.save(mProfilePath))
            {
                mManager = previous;
                refreshProfiles();
                mStatusMessage->setCaption("Error: Profile selection could not be saved");
                return;
            }
            eventProfileChanged();
        }
    }

    void ProfileDialog::onAdd(MyGUI::Widget*)
    {
        const std::string username = mUsername->getCaption();
        const std::string password = mPassword->getCaption();

        if (!TES3MP::OpenMWAdapter::PlayerProfileManager::isValidUsername(username))
        {
            mStatusMessage->setCaption("Error: Username must be 3-32 chars (letters, digits, _)");
            return;
        }

        if (!TES3MP::OpenMWAdapter::PlayerProfileManager::isValidPassword(password))
        {
            mStatusMessage->setCaption("Error: Password must be 1-256 bytes");
            return;
        }

        const auto previous = mManager;
        if (!mManager.addProfile(username, password))
        {
            mStatusMessage->setCaption("Error: Profile already exists with that username");
            return;
        }

        if (!mManager.save(mProfilePath))
        {
            mManager = previous;
            mStatusMessage->setCaption("Error: Profile could not be saved");
            return;
        }
        refreshProfiles();
        mStatusMessage->setCaption("Profile created: " + username);
        eventProfileChanged();
    }

    void ProfileDialog::onDelete(MyGUI::Widget*)
    {
        const std::string username = mUsername->getCaption();
        if (username.empty())
            return;

        const auto previous = mManager;
        if (mManager.deleteProfile(username))
        {
            if (!mManager.save(mProfilePath))
            {
                mManager = previous;
                mStatusMessage->setCaption("Error: Profile deletion could not be saved");
                return;
            }
            refreshProfiles();
            mStatusMessage->setCaption("Profile deleted: " + username);
            eventProfileChanged();
        }
    }

    void ProfileDialog::onClose(MyGUI::Widget*)
    {
        exit();
    }
}
