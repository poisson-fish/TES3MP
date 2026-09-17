#include "test_allocations.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <new>

#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace
{
    using namespace MWWorld::Testing::Allocations;
    thread_local Trace* sTrace = nullptr;
    thread_local std::size_t sFailAt = 0;
    using TES3MP::Native::Allocations::currentPhase;
    using TES3MP::Native::Allocations::visitPhase;
    thread_local std::array<void*, 8192> sBlocks{};

    void trackAllocation(void* value) noexcept
    {
        if (!sTrace)
            return;
        if (sTrace->mOutstanding == sBlocks.size())
        {
            ++sTrace->mTrackingOverflow;
            return;
        }
        sBlocks[sTrace->mOutstanding++] = value;
        sTrace->mPeakOutstanding = std::max(sTrace->mPeakOutstanding, sTrace->mOutstanding);
    }

    void trackFree(void* value) noexcept
    {
        if (!sTrace || !value)
            return;
        const auto end = sBlocks.begin() + sTrace->mOutstanding;
        const auto found = std::find(sBlocks.begin(), end, value);
        if (found != end)
            *found = sBlocks[--sTrace->mOutstanding];
    }

    void observeAllocation()
    {
        if (!sTrace)
            return;
        ++sTrace->mAllocations[static_cast<std::size_t>(currentPhase)];
        if (++sTrace->mTotal == sFailAt)
        {
            ++sTrace->mFailures;
            sTrace->mFailedPhase = currentPhase;
            throw std::bad_alloc();
        }
    }

    void* allocate(std::size_t size, std::size_t alignment = 0)
    {
        observeAllocation();
        if (size == 0)
            size = 1;
        for (;;)
        {
            void* result = nullptr;
            if (alignment == 0)
                result = std::malloc(size);
            else
            {
#ifdef _MSC_VER
                result = _aligned_malloc(size, alignment);
#else
                if (size <= std::numeric_limits<std::size_t>::max() - (alignment - 1))
                    result = std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
            }
            if (result)
            {
                trackAllocation(result);
                return result;
            }
            const auto handler = std::get_new_handler();
            if (!handler)
                throw std::bad_alloc();
            handler();
        }
    }

    void freeAligned(void* value) noexcept
    {
#ifdef _MSC_VER
        _aligned_free(value);
#else
        std::free(value);
#endif
    }
}

namespace MWWorld::Testing::Allocations
{
    Observe::Observe(Trace& trace, std::size_t failAt) noexcept
    {
        if (sTrace || currentPhase != Phase::Outside)
            std::abort(); // Nested instrumentation would hide allocations.
        trace = {};
        sTrace = &trace;
        visitPhase = [](Phase phase) noexcept { ++sTrace->mVisits[static_cast<std::size_t>(phase)]; };
        sFailAt = failAt;
    }
    Observe::~Observe()
    {
        visitPhase = nullptr;
        sTrace = nullptr;
        sFailAt = 0;
    }
}

// Cover scalar/array, aligned, sized-delete and nothrow paths. Delegate variants
// to one observed allocation, with matching CRT allocation/deallocation families.
void* operator new(std::size_t size)
{
    return allocate(size);
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new[](size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}
void operator delete(void* value) noexcept
{
    trackFree(value);
    std::free(value);
}
void operator delete[](void* value) noexcept
{
    ::operator delete(value);
}
void operator delete(void* value, std::size_t) noexcept
{
    ::operator delete(value);
}
void operator delete[](void* value, std::size_t) noexcept
{
    ::operator delete[](value);
}
void operator delete(void* value, const std::nothrow_t&) noexcept
{
    ::operator delete(value);
}
void operator delete[](void* value, const std::nothrow_t&) noexcept
{
    ::operator delete[](value);
}
void operator delete(void* value, std::align_val_t) noexcept
{
    trackFree(value);
    freeAligned(value);
}
void operator delete[](void* value, std::align_val_t alignment) noexcept
{
    ::operator delete(value, alignment);
}
void operator delete(void* value, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(value, alignment);
}
void operator delete[](void* value, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete[](value, alignment);
}
void operator delete(void* value, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    ::operator delete(value, alignment);
}
void operator delete[](void* value, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    ::operator delete[](value, alignment);
}
