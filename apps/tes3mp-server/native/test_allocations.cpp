#include "test_allocations.hpp"

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
    thread_local Phase sPhase = Phase::Outside;

    void observeAllocation()
    {
        if (!sTrace)
            return;
        ++sTrace->mAllocations[static_cast<std::size_t>(sPhase)];
        if (++sTrace->mTotal == sFailAt)
        {
            ++sTrace->mFailures;
            sTrace->mFailedPhase = sPhase;
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
                return result;
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
        if (sTrace || sPhase != Phase::Outside)
            std::abort(); // Nested instrumentation would hide allocations.
        trace = {};
        sTrace = &trace;
        sFailAt = failAt;
    }
    Observe::~Observe()
    {
        sTrace = nullptr;
        sFailAt = 0;
    }
    InPhase::InPhase(Phase phase) noexcept
        : mPrevious(sPhase)
    {
        set(phase);
    }
    InPhase::~InPhase()
    {
        sPhase = mPrevious;
    }
    void InPhase::set(Phase phase) noexcept
    {
        sPhase = phase;
        if (sTrace)
            ++sTrace->mVisits[static_cast<std::size_t>(phase)];
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
