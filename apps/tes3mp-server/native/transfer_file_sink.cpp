#include "transfer_file_sink.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace MWWorld::Testing
{
    namespace
    {
        // Same OS flush/replace primitives as canonical_persistence_file.cpp,
        // with exclusive staging, checked close, and explicit uncertainty after
        // replacement. Keep the existing migration adapter/schema independent.
        class File
        {
#ifdef _WIN32
            HANDLE mHandle = INVALID_HANDLE_VALUE;
#else
            int mHandle = -1;
#endif
        public:
            File() = default;
            File(const File&) = delete;
            File& operator=(const File&) = delete;
            ~File() { close(); }
            bool open(const std::filesystem::path& path, bool create = false) noexcept
            {
#ifdef _WIN32
                mHandle = CreateFileW(path.c_str(), create ? GENERIC_WRITE : GENERIC_READ, create ? 0 : FILE_SHARE_READ,
                    nullptr, create ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                return mHandle != INVALID_HANDLE_VALUE;
#else
                mHandle = ::open(path.c_str(), create ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY | O_NONBLOCK, 0600);
                return mHandle >= 0;
#endif
            }
            bool size(uint64_t& result) noexcept
            {
#ifdef _WIN32
                BY_HANDLE_FILE_INFORMATION info;
                LARGE_INTEGER length;
                if (GetFileType(mHandle) != FILE_TYPE_DISK || !GetFileInformationByHandle(mHandle, &info)
                    || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !GetFileSizeEx(mHandle, &length)
                    || length.QuadPart < 0)
                    return false;
                result = static_cast<uint64_t>(length.QuadPart);
#else
                struct stat info;
                if (::fstat(mHandle, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0)
                    return false;
                result = static_cast<uint64_t>(info.st_size);
#endif
                return true;
            }
            bool read(char* bytes, size_t size, size_t& count) noexcept
            {
#ifdef _WIN32
                DWORD transferred = 0;
                if (!ReadFile(mHandle, bytes, static_cast<DWORD>(size), &transferred, nullptr))
                    return false;
#else
                ssize_t transferred;
                do
                    transferred = ::read(mHandle, bytes, size);
                while (transferred < 0 && errno == EINTR);
                if (transferred < 0)
                    return false;
#endif
                count = static_cast<size_t>(transferred);
                return true;
            }
            bool write(const char* bytes, size_t size, size_t& count) noexcept
            {
#ifdef _WIN32
                DWORD transferred = 0;
                if (!WriteFile(mHandle, bytes, static_cast<DWORD>(size), &transferred, nullptr))
                    return false;
#else
                ssize_t transferred;
                do
                    transferred = ::write(mHandle, bytes, size);
                while (transferred < 0 && errno == EINTR);
                if (transferred < 0)
                    return false;
#endif
                count = static_cast<size_t>(transferred);
                return true;
            }
            bool flush() noexcept
            {
#ifdef _WIN32
                return FlushFileBuffers(mHandle) != 0;
#else
                return ::fsync(mHandle) == 0;
#endif
            }
            bool close() noexcept
            {
#ifdef _WIN32
                if (mHandle == INVALID_HANDLE_VALUE)
                    return true;
                const auto handle = mHandle;
                mHandle = INVALID_HANDLE_VALUE;
                return CloseHandle(handle) != 0;
#else
                if (mHandle < 0)
                    return true;
                const auto handle = mHandle;
                mHandle = -1;
                return ::close(handle) == 0;
#endif
            }
        };

        void removeTemporary(const std::filesystem::path& path) noexcept
        {
#ifdef _WIN32
            DeleteFileW(path.c_str());
#else
            ::unlink(path.c_str());
#endif
        }

        bool replace(
            const std::filesystem::path& temporary, const std::filesystem::path& path, FileFaults& faults) noexcept
        {
#ifdef _WIN32
            // Reuse the migration adapter's bounded lock/share retries. Each
            // attempt names the same immutable staging file. An ambiguous move
            // that consumed it cannot produce a later spurious acceptance.
            constexpr unsigned MaximumAttempts = 6;
            for (unsigned attempt = 0; attempt < MaximumAttempts; ++attempt)
            {
                if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    return true;
                faults.mReplaceError = GetLastError();
                if (faults.mReplaceError != ERROR_ACCESS_DENIED && faults.mReplaceError != ERROR_SHARING_VIOLATION
                    && faults.mReplaceError != ERROR_LOCK_VIOLATION)
                    return false;
                if (attempt + 1 < MaximumAttempts)
                    Sleep(1u << attempt);
            }
            return false;
#else
            if (std::rename(temporary.c_str(), path.c_str()) == 0)
                return true;
            faults.mReplaceError = static_cast<uint32_t>(errno);
            return false;
#endif
        }

        bool barrier(const std::filesystem::path& parent) noexcept
        {
#ifdef _WIN32
            // WRITE_THROUGH is requested on replacement above. No separate
            // Windows directory-fsync or power-loss guarantee is claimed here.
            (void)parent;
            return true;
#else
            const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
            if (directory < 0)
                return false;
            const bool flushed = ::fsync(directory) == 0;
            const bool closed = ::close(directory) == 0;
            return flushed && closed;
#endif
        }

        FileReadResult openRead(
            File& file, const std::filesystem::path& path, uint64_t& size, FileFaults& faults) noexcept
        {
            if (faults.mFail == FileFault::ReadOpen || !file.open(path) || faults.mFail == FileFault::ReadSize
                || !file.size(size))
                return FileReadResult::Unavailable;
            return size > MaxTransferSaveBytes ? FileReadResult::TooLarge : FileReadResult::Read;
        }

        bool readExact(File& file, std::span<char> bytes, FileFaults& faults) noexcept
        {
            size_t offset = 0;
            while (offset < bytes.size())
            {
                if (faults.mFail == FileFault::Read && faults.mReads != 0)
                    return false;
                size_t count = 0;
                ++faults.mReads;
                if (!file.read(bytes.data() + offset,
                        std::min(bytes.size() - offset, std::clamp(faults.mChunk, size_t{ 1 }, size_t{ 4096 })), count)
                    || count == 0)
                    return false;
                offset += count;
            }
            return true;
        }

        bool finishRead(File& file, FileFaults& faults) noexcept
        {
            char extra;
            size_t count = 0;
            const bool eof = file.read(&extra, 1, count) && count == 0 && faults.mFail != FileFault::ReadEof;
            const bool closed = file.close();
            return eof && closed && faults.mFail != FileFault::ReadClose;
        }

        bool verifyFile(const std::filesystem::path& path, std::span<const char> bytes, FileFaults& faults) noexcept
        {
            File file;
            uint64_t size = 0;
            if (openRead(file, path, size, faults) != FileReadResult::Read || size != bytes.size())
                return false;
            std::array<char, 4096> buffer;
            for (size_t offset = 0; offset < bytes.size();)
            {
                const size_t count = std::min(buffer.size(), bytes.size() - offset);
                if (!readExact(file, std::span(buffer).first(count), faults)
                    || !std::equal(buffer.begin(), buffer.begin() + count, bytes.begin() + offset))
                    return false;
                offset += count;
            }
            return finishRead(file, faults);
        }
    }

    FileReadResult readTransferFile(const std::filesystem::path& path, TransferSaveBytes& output, FileFaults& faults)
    {
        File file;
        uint64_t size = 0;
        const auto result = openRead(file, path, size, faults);
        if (result != FileReadResult::Read)
            return result;
        TransferSaveBytes staged(static_cast<size_t>(size));
        if (!readExact(file, staged, faults) || !finishRead(file, faults))
            return FileReadResult::Unavailable;
        output.swap(staged);
        return FileReadResult::Read;
    }

    TransferFileSink::TransferFileSink(const std::filesystem::path& path)
        : mPath(path)
        , mTemporary(path)
        , mParent(path.parent_path())
    {
        if (path.empty() || mParent.empty())
            throw std::invalid_argument("Transfer file sink requires a scratch directory");
        mTemporary += ".tmp";
    }

    TestPersistenceResult TransferFileSink::write(std::span<const char> bytes, FileFaults& faults) noexcept
    {
        if (mFailedClosed)
            return TestPersistenceResult::Uncertain;
        if (bytes.empty() || bytes.size() > MaxTransferSaveBytes)
            return TestPersistenceResult::Rejected;
        File file;
        faults.mReached = FileFault::Create;
        if (faults.mFail == FileFault::Create || !file.open(mTemporary, true))
            return TestPersistenceResult::Rejected;
        struct Cleanup
        {
            File& mFile;
            const std::filesystem::path& mPath;
            ~Cleanup()
            {
                mFile.close();
                removeTemporary(mPath);
            }
        } cleanup{ file, mTemporary };
        faults.mReached = FileFault::Write;
        for (size_t offset = 0; offset < bytes.size();)
        {
            if (faults.mFail == FileFault::Write && faults.mWrites != 0)
                return TestPersistenceResult::Rejected;
            size_t count = 0;
            ++faults.mWrites;
            if (!file.write(bytes.data() + offset,
                    std::min(bytes.size() - offset, std::clamp(faults.mChunk, size_t{ 1 }, size_t{ 4096 })), count)
                || count == 0)
                return TestPersistenceResult::Rejected;
            offset += count;
        }
        faults.mReached = FileFault::Flush;
        if (faults.mFail == FileFault::Flush || !file.flush())
            return TestPersistenceResult::Rejected;
        faults.mReached = FileFault::Close;
        const bool closed = file.close();
        if (!closed || faults.mFail == FileFault::Close || faults.mFail == FileFault::Replace)
            return TestPersistenceResult::Rejected;

        // Conservative even on an OS replace error: do not infer from a failure
        // return that the namespace was untouched. No fallible C++ work follows.
        mFailedClosed = true;
        faults.mReached = FileFault::Replace;
        if (faults.mFail == FileFault::ReplaceError || !replace(mTemporary, mPath, faults)
            || faults.mFail == FileFault::AfterReplace)
            return TestPersistenceResult::Uncertain;
        faults.mReached = FileFault::Barrier;
        if (faults.mFail == FileFault::Barrier || !barrier(mParent))
            return TestPersistenceResult::Uncertain;
        faults.mReached = FileFault::Read;
        if (!verifyFile(mPath, bytes, faults))
            return TestPersistenceResult::Uncertain;
        mFailedClosed = false;
        return TestPersistenceResult::Accepted;
    }
}
