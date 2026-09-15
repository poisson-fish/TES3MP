#ifndef TES3MP_NATIVE_TRANSFER_SAVE_CODEC_H
#define TES3MP_NATIVE_TRANSFER_SAVE_CODEC_H

#include "transfer_rehearsal.hpp"

#include <array>
#include <span>

namespace MWWorld::Testing
{
    struct RestoreContent
    {
        std::span<const ESM::Miscellaneous* const> mBases;
        const ESM::Script& mScript;
        const Compiler::Locals& mDeclarations;
    };

    const ESM::Miscellaneous& suppliedBase(const ESM::RefId& id, const RestoreContent& content);
    void validateRestore(const SerializedPair& input, const RestoreContent& content);

    // Test-target-only, little-endian, development format. The caller supplies
    // the trusted runtime/content identity and already interned content IDs.
    // These synthetic identities are not a production loadout fingerprint.
    struct SaveEnvelope
    {
        std::string mRuntime;
        std::array<unsigned char, 32> mContent{};
        ESM::RefNum mSourceOwner, mDestinationOwner, mInitiator;
        bool operator==(const SaveEnvelope&) const = default;
    };

    struct SaveBindings
    {
        const SaveEnvelope& mEnvelope;
        const RestoreContent& mContent;
        // All RefIds used by the save, including semantic owner/soul/key fields.
        // Preflight checks these without interning names from untrusted input.
        std::span<const ESM::RefId> mReferenceIds;
    };

    inline constexpr size_t MaxTransferSaveBytes = 8 * 1024 * 1024;
    inline constexpr size_t MaxTransferObjectBytes = 256 * 1024;
    using TransferSaveBytes = std::vector<char>;

    // All bounds/relationships are checked before publishing either output.
    // No services, pointers, registry, selections or iterators enter the format.
    void encodeTransferSave(const SerializedPair& input, const SaveBindings& bindings, TransferSaveBytes& output);
    void decodeTransferSave(std::span<const char> bytes, const SaveBindings& bindings, SerializedPair& output);
}

#endif
