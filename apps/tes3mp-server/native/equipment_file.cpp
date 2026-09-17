#include "equipment_file.hpp"
#include <stdexcept>

namespace TES3MP::Native
{
    using namespace MWWorld;
    FileReadResult readEquipmentFile(const std::filesystem::path& path, EquipmentBytes& output, FileFaults& faults)
    {
        return readBoundedFile(path, MaxEquipmentBytes, output, faults);
    }

    FileReadResult restartEquipmentFile(const std::filesystem::path& path, const EquipmentBindings& bindings,
        std::unique_ptr<const RestoredPlainEquipment>& output, FileFaults& faults, EquipmentBytes* accepted)
    {
        EquipmentBytes bytes;
        const auto result = readEquipmentFile(path, bytes, faults);
        if (result != FileReadResult::Read)
            return result;
        PlainEquipmentValues values;
        decodeEquipment(bytes, bindings, values);
        auto staged = std::make_unique<RestoredPlainEquipment>(
            RestoredPlainEquipment::restore(values, bindings.mContent, bindings.mEnvelope.mActor, bindings.mScriptLocals));
        static_assert(noexcept(output = std::move(staged)));
        output = std::move(staged);
        if (accepted)
            accepted->swap(bytes);
        return FileReadResult::Read;
    }

    EquipmentFileSink::EquipmentFileSink(const std::filesystem::path& path, bool session)
        : mFile(path, session ? MaxEquipmentSessionBytes : MaxEquipmentBytes), mSession(session)
    {
    }

    PersistenceResult EquipmentFileSink::write(const PlainEquipmentValues& values,
        const EquipmentBindings& bindings, EquipmentBytes& output, FileFaults& faults)
    {
        if (mFile.failedClosed())
            return PersistenceResult::Uncertain;
        if (mSession) throw std::invalid_argument("Session sink requires both actors");
        EquipmentBytes staged;
        encodeEquipment(values, bindings, staged);
        const auto result = mFile.write(staged, faults);
        if (result == PersistenceResult::Accepted)
            output.swap(staged);
        return result;
    }
    PersistenceResult EquipmentFileSink::writeSession(const EquipmentSessionValues& values,
        const std::array<EquipmentBindings, 2>& bindings, EquipmentBytes& output, FileFaults& faults,
        const EquipmentBindings* container)
    {
        if (mFile.failedClosed()) return PersistenceResult::Uncertain;
        if (!mSession) throw std::invalid_argument("Actor sink cannot commit a session");
        EquipmentBytes staged;
        encodeEquipmentSession(values, bindings, staged, container);
        const auto result = mFile.write(staged, faults);
        if (result == PersistenceResult::Accepted) output.swap(staged);
        return result;
    }

}
