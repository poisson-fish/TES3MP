#ifndef OPENMW_TES3MP_ENGINE_COORDINATOR_HPP
#define OPENMW_TES3MP_ENGINE_COORDINATOR_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include <tes3mp/character_profile.hpp>
#include <tes3mp/character_creation_protocol.hpp>
#include <tes3mp/dialogue_choice_protocol.hpp>

namespace TES3MP::OpenMWAdapter
{
    enum class MultiplayerState
    {
        Idle,
        Connecting,
        Ready,
        Failed,
    };

    enum class DialogueChoiceSubmissionResult
    {
        Pending,
        Unavailable,
        Unmapped,
        Backpressured,
    };

    struct DialogueChoiceResolution
    {
        int localChoice = 0;
        DialogueChoiceDisposition disposition = DialogueChoiceDisposition::Rejected;
        bool duplicate = false;

        constexpr bool committed() const noexcept { return disposition == DialogueChoiceDisposition::Committed; }
    };

    class EngineCoordinator
    {
    public:
        virtual ~EngineCoordinator() = default;
        virtual void frame(float frameDurationSeconds) noexcept = 0;

        virtual MultiplayerState multiplayerState() const noexcept { return MultiplayerState::Ready; }
        virtual bool connect(std::string_view) noexcept { return false; }
        virtual bool host(std::string_view) noexcept { return false; }
        virtual void setJoinPassword(std::string_view) noexcept {}
        virtual void setPlayerProfile(std::string_view, std::span<const std::byte>) noexcept {}
        virtual std::string_view activePlayerUsername() const noexcept { return {}; }
        virtual std::string_view failure() const noexcept { return {}; }
        virtual bool gameStartRequested() const noexcept { return false; }
        virtual CharacterLifecycle characterLifecycle() const noexcept
        { return CharacterLifecycle::NewCharacter; }
        virtual CharacterProfileRevision characterProfileRevision() const noexcept
        { return CharacterProfileRevision::initial(); }
        virtual const ReliableCharacterProfile* confirmedCharacterProfile() const noexcept { return nullptr; }
        virtual bool submitCharacterCreation(CharacterCreationChoice) noexcept { return false; }
        virtual DialogueChoiceSubmissionResult submitDialogueChoice(int) noexcept
        {
            return DialogueChoiceSubmissionResult::Unavailable;
        }
        virtual std::optional<DialogueChoiceResolution> takeDialogueChoiceResolution() noexcept
        {
            return std::nullopt;
        }
        virtual void setGameRunning(bool) noexcept {}
        virtual void confirmGameStart(bool running) noexcept { setGameRunning(running); }
    };
}

#endif
