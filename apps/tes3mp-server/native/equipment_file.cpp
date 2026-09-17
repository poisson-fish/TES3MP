#include "equipment_file.hpp"

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

    EquipmentFileSink::EquipmentFileSink(const std::filesystem::path& path)
        : mFile(path, MaxEquipmentBytes)
    {
    }

    PersistenceResult EquipmentFileSink::write(const PlainEquipmentValues& values,
        const EquipmentBindings& bindings, EquipmentBytes& output, FileFaults& faults)
    {
        if (mFile.failedClosed())
            return PersistenceResult::Uncertain;
        EquipmentBytes staged;
        encodeEquipment(values, bindings, staged);
        const auto result = mFile.write(staged, faults);
        if (result == PersistenceResult::Accepted)
            output.swap(staged);
        return result;
    }
}
