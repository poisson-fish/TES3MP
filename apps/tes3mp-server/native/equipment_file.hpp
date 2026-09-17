#ifndef TES3MP_NATIVE_EQUIPMENT_FILE_H
#define TES3MP_NATIVE_EQUIPMENT_FILE_H

#include "bounded_file.hpp"
#include "equipment_session.hpp"

namespace TES3MP::Native
{
    using namespace MWWorld;
    // Size is checked on the opened regular-file handle before allocating input
    // bytes. Short reads, EOF and close are checked before nonthrowing swap.
    FileReadResult readEquipmentFile(const std::filesystem::path& path, EquipmentBytes& output, FileFaults& faults);

    // Read/decode/restore are staged together. Unavailable/oversized input,
    // invalid_argument and allocation failure preserve the prior owned object
    // and all its storage/value. Only a complete detached result is published.
    // Caller content must outlive it. No owner, services, listeners or effects
    // are installed, and saved generation counters are never inferred.
    // When requested, the exact bytes from the same read are published with the
    // detached owner, avoiding a second read for validated installation.
    FileReadResult restartEquipmentFile(const std::filesystem::path& path, const EquipmentBindings& bindings,
        std::unique_ptr<const RestoredPlainEquipment>& output, FileFaults& faults, EquipmentBytes* accepted = nullptr);

    // App-local equipment formats. The caller supplies trusted runtime,
    // content and actor bindings on every write; no borrowed bindings are kept.
    // One serialized writer and an existing private scratch directory are required.
    class EquipmentFileSink
    {
        BoundedFileSink mFile;
        const bool mSession;

    public:
        explicit EquipmentFileSink(const std::filesystem::path& path, bool session = false);
        bool session() const noexcept { return mSession; }
        PersistenceResult writeSession(const EquipmentSessionValues& values,
            const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output, FileFaults& faults);
        // Encode and validate before any I/O. Accepted publishes complete owned
        // bytes by nonthrowing swap. Rejection/exception/uncertainty preserves
        // prior output storage/value. Uncertainty blocks further writes, even
        // encoding; recovery requires a new composition after validated restart.
        // This is persistence only: no live installation, success notification
        // or gameplay effect execution is authorized by Accepted.
        PersistenceResult write(const PlainEquipmentValues& values, const EquipmentBindings& bindings,
            EquipmentBytes& output, FileFaults& faults);
        bool failedClosed() const noexcept { return mFile.failedClosed(); }
    };
}

#endif
