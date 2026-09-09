#include <tes3mp/character_creation_protocol.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace
{
    using namespace TES3MP;

    template <class T> T id(std::uint64_t value) { return *T::fromValue(value); }

    ClientCharacterCreationCommand command(CharacterCreationChoice choice)
    {
        return { id<SessionId>(11), id<SessionGeneration>(2), id<CommandSequence>(3), id<CommandId>(4),
            id<CanonicalRevision>(5), { id<CharacterProfileRevision>(6), std::move(choice) } };
    }

    CharacterProfile completeProfile()
    {
        CharacterDerivedState derived;
        for (std::size_t i = 0; i < derived.attributes.size(); ++i) derived.attributes[i] = 30 + i;
        for (std::size_t i = 0; i < derived.skills.size(); ++i) derived.skills[i] = 5 + i;
        derived.startingSpells = { id<SpellRecordId>(51), id<SpellRecordId>(52) };
        CustomClassDefinition custom{ "Wayfarer", "A careful custom class", ClassSpecialization::Stealth,
            { 3, 6 }, { 1, 2, 3, 4, 5 }, { 20, 21, 22, 23, 24 } };
        return *CharacterProfile::restore(CharacterLifecycle::EstablishedCharacter, CharacterCreationPhase::Complete,
            "Nerevar", CharacterAppearance{ id<RaceRecordId>(10), id<HeadRecordId>(20), id<HairRecordId>(30),
                           CharacterSex::Male },
            CharacterClass{ std::move(custom) }, id<BirthsignRecordId>(40), std::move(derived),
            std::vector<StartingItem>{ { id<ItemPrototypeId>(60), 1, 3 } }, id<CharacterProfileRevision>(9));
    }

    bool commandRoundTrips()
    {
        CustomClassDefinition custom{ "Scout", "Road class", ClassSpecialization::Stealth,
            { 3, 4 }, { 1, 2, 3, 4, 5 }, { 6, 7, 8, 9, 10 } };
        const std::array<CharacterCreationChoice, 6> choices{
            SetCharacterName{ "Jiub" },
            SetCharacterAppearance{ { id<RaceRecordId>(1), id<HeadRecordId>(2), id<HairRecordId>(3),
                CharacterSex::Male } },
            SetCharacterClass{ id<ClassRecordId>(4) }, SetCharacterClass{ custom },
            SetCharacterBirthsign{ id<BirthsignRecordId>(5) }, CompleteCharacterCreation{} };
        for (const auto& choice : choices)
        {
            const auto value = command(choice);
            const auto encoded = encodeClientCharacterCreationCommand(value);
            const auto decoded = decodeClientCharacterCreationCommand(encoded);
            if (!std::holds_alternative<ClientCharacterCreationCommand>(decoded)
                || std::get<ClientCharacterCreationCommand>(decoded) != value)
                return false;
        }
        return true;
    }

    bool profileRoundTrips()
    {
        ReliableCharacterProfile value{ id<SessionId>(11), id<SessionGeneration>(2), id<PlayerId>(7),
            CharacterConfirmationResult::Confirmed, completeProfile() };
        const auto encoded = encodeReliableCharacterProfile(value);
        const auto decoded = decodeReliableCharacterProfile(encoded);
        return std::holds_alternative<ReliableCharacterProfile>(decoded)
            && std::get<ReliableCharacterProfile>(decoded) == value
            && encodeReliableCharacterProfile(std::get<ReliableCharacterProfile>(decoded)) == encoded;
    }

    bool confirmationResultNamesAreStable()
    {
        return characterConfirmationResultName(CharacterConfirmationResult::Confirmed) == "Confirmed"
            && characterConfirmationResultName(CharacterConfirmationResult::InvalidAppearance) == "InvalidAppearance"
            && characterConfirmationResultName(static_cast<CharacterConfirmationResult>(255))
                == "UnknownCharacterConfirmationResult";
    }

    bool verified_unaligned_scalar_vectors_are_copied_before_access()
    {
        const ReliableCharacterProfile profile{ id<SessionId>(11), id<SessionGeneration>(2), id<PlayerId>(7),
            CharacterConfirmationResult::Confirmed, completeProfile() };
        auto payload = encodeReliableCharacterProfile(profile);
        if (payload.size() != 440)
            return false;
        constexpr std::array<std::pair<std::size_t, std::uint8_t>, 17> mutations{ {
            { 14, 1 }, { 68, 40 }, { 92, 176 }, { 171, 32 }, { 218, 9 },
            { 220, 0 }, { 224, 0 }, { 228, 0 }, { 232, 6 }, { 233, 3 }, { 234, 7 }, { 236, 0 }, { 240, 0 },
            { 242, 2 }, { 313, 128 }, { 380, 0 }, { 382, 3 }
        } };
        for (const auto [offset, byte] : mutations)
            payload[offset] = static_cast<std::byte>(byte);
        const auto decoded = decodeReliableCharacterProfile(payload);
        const auto* value = std::get_if<ReliableCharacterProfile>(&decoded);
        return value != nullptr
            && std::holds_alternative<ReliableCharacterProfile>(
                decodeReliableCharacterProfile(encodeReliableCharacterProfile(*value)));
    }

    bool malformedInputIsRejected()
    {
        auto encoded = encodeClientCharacterCreationCommand(command(SetCharacterName{ "Jiub" }));
        for (std::size_t size = 0; size < encoded.size(); ++size)
            if (std::holds_alternative<ClientCharacterCreationCommand>(
                    decodeClientCharacterCreationCommand(std::span(encoded).first(size))))
                return false;
        encoded[8] ^= std::byte{ 0xff };
        return std::holds_alternative<CharacterProtocolError>(decodeClientCharacterCreationCommand(encoded));
    }

    bool goldenVectorIsStable()
    {
        constexpr std::string_view expected
            = "6c0000002000000054334343000000001400400034002c0024001c0014000c000b000400140000003c000000"
              "0000000106000000000000000500000000000000040000000000000003000000000000000200000000000000"
              "0b0000000000000000000000040000004a69756200000000";
        std::string actual;
        constexpr char digits[] = "0123456789abcdef";
        for (const auto byte : encodeClientCharacterCreationCommand(command(SetCharacterName{ "Jiub" })))
        {
            const auto value = std::to_integer<unsigned char>(byte);
            actual.push_back(digits[value >> 4]);
            actual.push_back(digits[value & 15]);
        }
        return actual == expected;
    }

    bool writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    }

    std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) return std::nullopt;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return stream && stream.peek() == std::ifstream::traits_type::eof()
            ? std::optional<std::vector<std::byte>>(std::move(bytes)) : std::nullopt;
    }

    bool writeCorpus(const std::filesystem::path& directory)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) return false;
        const ReliableCharacterProfile profile{ id<SessionId>(11), id<SessionGeneration>(2), id<PlayerId>(7),
            CharacterConfirmationResult::Confirmed, completeProfile() };
        return writeFile(directory / "valid-character-command",
                   encodeClientCharacterCreationCommand(command(SetCharacterName{ "Jiub" })))
            && writeFile(directory / "valid-character-profile", encodeReliableCharacterProfile(profile));
    }

    bool verifyCorpus(const std::filesystem::path& directory)
    {
        const auto commandBytes = readFile(directory / "valid-character-command");
        const auto profileBytes = readFile(directory / "valid-character-profile");
        return commandBytes && std::holds_alternative<ClientCharacterCreationCommand>(
                                   decodeClientCharacterCreationCommand(*commandBytes))
            && profileBytes && std::holds_alternative<ReliableCharacterProfile>(
                                   decodeReliableCharacterProfile(*profileBytes));
    }
}

int main(int argc, char** argv)
{
    static_assert(static_cast<std::uint8_t>(CharacterConfirmationResult::InvalidAppearance) == 6);
    static_assert(static_cast<std::uint8_t>(CharacterSex::Female) == 0
        && static_cast<std::uint8_t>(CharacterSex::Male) == 1);
    if (argc == 3 && std::string_view(argv[1]) == "--write-corpus")
        return writeCorpus(argv[2]) ? 0 : 1;
    if (argc == 3 && std::string_view(argv[1]) == "--verify-corpus")
        return verifyCorpus(argv[2]) ? 0 : 1;
    if (argc == 2)
    {
        constexpr char digits[] = "0123456789abcdef";
        for (const auto byte : encodeClientCharacterCreationCommand(command(SetCharacterName{ "Jiub" })))
        {
            const auto value = std::to_integer<unsigned char>(byte);
            std::cout << digits[value >> 4] << digits[value & 15];
        }
        std::cout << '\n';
        return 0;
    }
    if (!commandRoundTrips() || !profileRoundTrips() || !confirmationResultNamesAreStable()
        || !verified_unaligned_scalar_vectors_are_copied_before_access() || !malformedInputIsRejected()
        || !goldenVectorIsStable())
        return 1;
    std::cout << "character creation protocol contracts passed\n";
}
