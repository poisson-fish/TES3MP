#include "transfer_file_sink.hpp"

namespace MWWorld::Testing
{
    FileReadResult readTransferFile(const std::filesystem::path& path, TransferSaveBytes& output, FileFaults& faults)
    {
        return readBoundedFile(path, MaxTransferSaveBytes, output, faults);
    }

    TransferFileSink::TransferFileSink(const std::filesystem::path& path)
        : mFile(path, MaxTransferSaveBytes)
    {
    }

    TestPersistenceResult TransferFileSink::write(std::span<const char> bytes, FileFaults& faults) noexcept
    {
        return mFile.write(bytes, faults);
    }
}
