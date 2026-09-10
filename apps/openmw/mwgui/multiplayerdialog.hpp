#ifndef OPENMW_MWGUI_MULTIPLAYERDIALOG_HPP
#define OPENMW_MWGUI_MULTIPLAYERDIALOG_HPP

#include "windowbase.hpp"

#include <string>

namespace MyGUI
{
    class Button;
    class EditBox;
    class TextBox;
}

namespace TES3MP::OpenMWAdapter
{
    class PlayerProfileManager;
}

namespace MWGui
{
    class MultiplayerDialog final : public WindowModal
    {
    public:
        explicit MultiplayerDialog(std::string defaultAddress,
            TES3MP::OpenMWAdapter::PlayerProfileManager* profileManager = nullptr);
        bool exit() override;
        MyGUI::Widget* getDefaultKeyFocus() override;
        void updateProfileDisplay();

    private:
        void onConnect(MyGUI::Widget* sender);
        void onAddressAccepted(MyGUI::EditBox* sender);
        void onHost(MyGUI::Widget* sender);
        void onCancel(MyGUI::Widget* sender);
        bool start(bool host);

        TES3MP::OpenMWAdapter::PlayerProfileManager* mProfileManager = nullptr;

        MyGUI::EditBox* mAddress = nullptr;
        MyGUI::EditBox* mPassword = nullptr;
        MyGUI::TextBox* mProfileLabel = nullptr;
        MyGUI::Button* mConnect = nullptr;
        MyGUI::Button* mHost = nullptr;
        MyGUI::Button* mCancel = nullptr;
    };
}

#endif
