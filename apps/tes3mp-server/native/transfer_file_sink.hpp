#ifndef TES3MP_NATIVE_TRANSFER_FILE_SINK_H
#define TES3MP_NATIVE_TRANSFER_FILE_SINK_H

#include "bounded_file.hpp"
#include "transfer_save_codec.hpp"

namespace MWWorld::Testing
{
    FileReadResult readTransferFile(const std::filesystem::path& path, TransferSaveBytes& output, FileFaults& faults);

    // Transfer v4 keeps its own byte bound and trusted caller encoding contract.
    // Shared test-only I/O owns exclusive staging and sticky uncertainty.
    class TransferFileSink
    {
        BoundedFileSink mFile;

    public:
        explicit TransferFileSink(const std::filesystem::path& path);
        TransferFileSink(const TransferFileSink&) = delete;
        TransferFileSink& operator=(const TransferFileSink&) = delete;
        TestPersistenceResult write(std::span<const char> bytes, FileFaults& faults) noexcept;
        bool failedClosed() const noexcept { return mFile.failedClosed(); }
    };
}

#endif
