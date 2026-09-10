#include "multiplayerdialog.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_TextBox.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../tes3mp/engine_coordinator.hpp"
#include "../tes3mp/player_profile_manager.hpp"

namespace MWGui
{
    MultiplayerDialog::MultiplayerDialog(std::string defaultAddress,
        TES3MP::OpenMWAdapter::PlayerProfileManager* profileManager)
        : WindowModal("openmw_multiplayer_dialog.layout")
        , mProfileManager(profileManager)
    {
        getWidget(mAddress, "Address");
        getWidget(mPassword, "Password");
        getWidget(mProfileLabel, "ProfileLabel");
        getWidget(mConnect, "Connect");
        getWidget(mHost, "Host");
        getWidget(mCancel, "Cancel");
        mAddress->setCaption(std::move(defaultAddress));
        mPassword->setCaption("tes3mp");
        mPassword->setEditPassword(true);
        mAddress->eventEditSelectAccept += MyGUI::newDelegate(this, &MultiplayerDialog::onAddressAccepted);
        mConnect->eventMouseButtonClick += MyGUI::newDelegate(this, &MultiplayerDialog::onConnect);
        mHost->eventMouseButtonClick += MyGUI::newDelegate(this, &MultiplayerDialog::onHost);
        mCancel->eventMouseButtonClick += MyGUI::newDelegate(this, &MultiplayerDialog::onCancel);
        mControllerButtons.mA = "#{Interface:OK}";
        mControllerButtons.mB = "#{Interface:Cancel}";
        updateProfileDisplay();
        center();
    }

    MyGUI::Widget* MultiplayerDialog::getDefaultKeyFocus()
    {
        return mAddress;
    }

    bool MultiplayerDialog::exit()
    {
        setVisible(false);
        return true;
    }

    void MultiplayerDialog::onConnect(MyGUI::Widget*)
    {
        (void)start(false);
    }

    void MultiplayerDialog::onAddressAccepted(MyGUI::EditBox*)
    {
        (void)start(false);
    }

    void MultiplayerDialog::onHost(MyGUI::Widget*)
    {
        (void)start(true);
    }

    void MultiplayerDialog::onCancel(MyGUI::Widget*)
    {
        (void)exit();
    }

    void MultiplayerDialog::updateProfileDisplay()
    {
        if (!mProfileLabel)
            return;

        if (mProfileManager && mProfileManager->hasProfiles())
        {
            if (const auto profile = mProfileManager->activeProfile())
            {
                mProfileLabel->setCaption("Active Profile: " + profile->username);
                mConnect->setEnabled(true);
                mHost->setEnabled(true);
                return;
            }
        }

        mProfileLabel->setCaption("No profile selected. Please create one in Profiles.");
        mConnect->setEnabled(false);
        mHost->setEnabled(false);
    }

    bool MultiplayerDialog::start(bool shouldHost)
    {
        auto* multiplayer = MWBase::Environment::get().getMultiplayerCoordinator();
        if (!multiplayer)
            return false;

        if (mProfileManager)
        {
            const auto profile = mProfileManager->activeProfile();
            if (!profile)
            {
                MWBase::Environment::get().getWindowManager()->messageBox(
                    "Please create a player profile before connecting.");
                return false;
            }
            multiplayer->setPlayerProfile(profile->username, profile->credential);
        }

        const std::string address = mAddress->getCaption();
        multiplayer->setJoinPassword(mPassword->getCaption());
        const bool accepted = shouldHost ? multiplayer->host(address) : multiplayer->connect(address);
        if (!accepted)
        {
            const std::string message = multiplayer->failure().empty()
                ? "The multiplayer request could not be started."
                : std::string(multiplayer->failure());
            MWBase::Environment::get().getWindowManager()->messageBox(message);
            return false;
        }
        setVisible(false);
        MWBase::Environment::get().getWindowManager()->messageBox(
            shouldHost ? "Starting the server and connecting..." : "Connecting to the server...");
        return true;
    }
}
