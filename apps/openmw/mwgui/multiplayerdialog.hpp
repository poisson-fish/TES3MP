#ifndef OPENMW_MWGUI_MULTIPLAYERDIALOG_HPP
#define OPENMW_MWGUI_MULTIPLAYERDIALOG_HPP

#include "windowbase.hpp"

#include <string>

namespace MyGUI
{
    class Button;
    class EditBox;
}

namespace MWGui
{
    class MultiplayerDialog final : public WindowModal
    {
    public:
        explicit MultiplayerDialog(std::string defaultAddress);
        bool exit() override;
        MyGUI::Widget* getDefaultKeyFocus() override;

    private:
        void onConnect(MyGUI::Widget* sender);
        void onAddressAccepted(MyGUI::EditBox* sender);
        void onHost(MyGUI::Widget* sender);
        void onCancel(MyGUI::Widget* sender);
        bool start(bool host);

        MyGUI::EditBox* mAddress = nullptr;
        MyGUI::EditBox* mPassword = nullptr;
        MyGUI::Button* mConnect = nullptr;
        MyGUI::Button* mHost = nullptr;
        MyGUI::Button* mCancel = nullptr;
    };
}

#endif
