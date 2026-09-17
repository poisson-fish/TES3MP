#ifndef TES3MP_NATIVE_BOUNDED_FILE_H
#define TES3MP_NATIVE_BOUNDED_FILE_H

#include "persistence.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace TES3MP::Native
{
    // App-local OS fault seams. Short successful reads/writes are real
    // I/O; failures occur at the named boundary without replacing the protocol.
    enum class FileFault
    {
        None,
        Create,
        Write,
        Flush,
        Close,
        Replace,
        ReplaceError,
        AfterReplace,
        Barrier,
        ReadOpen,
        ReadSize,
        Read,
        ReadEof,
        ReadClose
    };
    struct FileFaults
    {
        FileFault mFail = FileFault::None;
        size_t mChunk = 4096;
        size_t mWrites = 0, mReads = 0;
        FileFault mReached = FileFault::None;
        uint32_t mReplaceError = 0;
    };

    enum class FileReadResult
    {
        Read,
        Unavailable,
        TooLarge
    };

    // Query regular-file size on the opened handle before allocating the byte
    // buffer; bounded exact reads plus EOF check; publish only by noexcept swap.
    // Allocation failure propagates and preserves output, including its storage.
    FileReadResult readBoundedFile(
        const std::filesystem::path& path, size_t maximum, std::vector<char>& output, FileFaults& faults);

    // One writer, an existing private scratch directory and serialized access.
    // Exclusive sibling .tmp creation never deletes another writer's staging.
    // Caller encodes/validates trusted bindings before write(). This leaf knows
    // only bounded bytes, not the legacy canonical gameplay schema.
    class BoundedFileSink
    {
        std::filesystem::path mPath, mTemporary, mParent;
        const size_t mMaximum;
        bool mFailedClosed = false;

    public:
        BoundedFileSink(const std::filesystem::path& path, size_t maximum);
        BoundedFileSink(const BoundedFileSink&) = delete;
        BoundedFileSink& operator=(const BoundedFileSink&) = delete;

        // No allocation or exceptions, including cleanup and post-replacement
        // verification. Uncertainty is sticky; recovery needs a new composition
        // after validated restart, never an in-place retry of this runtime.
        PersistenceResult write(std::span<const char> bytes, FileFaults& faults) noexcept;
        bool failedClosed() const noexcept { return mFailedClosed; }
    };
}

namespace MWWorld::Testing
{
    using TES3MP::Native::FileFault;
    using TES3MP::Native::FileFaults;
    using TES3MP::Native::FileReadResult;
    using TES3MP::Native::readBoundedFile;
    using TES3MP::Native::BoundedFileSink;
}
#endif
