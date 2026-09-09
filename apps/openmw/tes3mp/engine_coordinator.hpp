#ifndef OPENMW_TES3MP_ENGINE_COORDINATOR_HPP
#define OPENMW_TES3MP_ENGINE_COORDINATOR_HPP

#include <string_view>

#include <tes3mp/character_profile.hpp>
#include <tes3mp/character_creation_protocol.hpp>

namespace TES3MP::OpenMWAdapter
{
    enum class MultiplayerState
    {
        Idle,
        Connecting,
        Ready,
        Failed,
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
        virtual std::string_view failure() const noexcept { return {}; }
        virtual bool gameStartRequested() const noexcept { return false; }
        virtual CharacterLifecycle characterLifecycle() const noexcept
        { return CharacterLifecycle::NewCharacter; }
        virtual CharacterProfileRevision characterProfileRevision() const noexcept
        { return CharacterProfileRevision::initial(); }
        virtual const ReliableCharacterProfile* confirmedCharacterProfile() const noexcept { return nullptr; }
        virtual bool submitCharacterCreation(CharacterCreationChoice) noexcept { return false; }
        virtual void setGameRunning(bool) noexcept {}
        virtual void confirmGameStart(bool running) noexcept { setGameRunning(running); }
    };
}

#endif
