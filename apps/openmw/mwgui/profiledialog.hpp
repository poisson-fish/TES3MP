#ifndef OPENMW_MWGUI_PROFILEDIALOG_HPP
#define OPENMW_MWGUI_PROFILEDIALOG_HPP

#include "windowbase.hpp"

#include <string>

namespace MyGUI
{
    class Button;
    class ComboBox;
    class EditBox;
    class TextBox;
}

namespace TES3MP::OpenMWAdapter
{
    class PlayerProfileManager;
}

namespace MWGui
{
    class ProfileDialog final : public WindowModal
    {
    public:
        explicit ProfileDialog(TES3MP::OpenMWAdapter::PlayerProfileManager& manager, std::string profilePath);
        bool exit() override;
        MyGUI::Widget* getDefaultKeyFocus() override;

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_ProfileChanged;
        EventHandle_ProfileChanged eventProfileChanged;

    private:
        void refreshProfiles();
        void onProfileSelected(MyGUI::ComboBox* sender, std::size_t index);
        void onAdd(MyGUI::Widget* sender);
        void onDelete(MyGUI::Widget* sender);
        void onClose(MyGUI::Widget* sender);

        TES3MP::OpenMWAdapter::PlayerProfileManager& mManager;
        std::string mProfilePath;

        MyGUI::ComboBox* mProfileSelect = nullptr;
        MyGUI::EditBox* mUsername = nullptr;
        MyGUI::EditBox* mPassword = nullptr;
        MyGUI::TextBox* mStatusMessage = nullptr;
        MyGUI::Button* mAdd = nullptr;
        MyGUI::Button* mDelete = nullptr;
        MyGUI::Button* mClose = nullptr;
    };
}

#endif
