//
//  SwiftVector.h
//  Kitbox
//
//  Vectors laid out in memory the way Swift lays out an Array's elements: 32
//  bytes into a malloc'd block (Swift puts the array's header there).
//
//  Not a curiosity. vDSP's real FFT takes another code path when the real and
//  the imaginary half of a split spectrum both start on a 64-byte boundary (at
//  512 points and up, measured 2026-10-09), and that path rounds differently in
//  the last bit. A Swift array's elements never start there - malloc's blocks
//  from 1 KB up are 512-byte aligned, plus Swift's 32 - while a large
//  std::vector's always do (page-aligned). With plain vectors the port's
//  envelopes and spectra came out 1e-7 off Transmute's, and the fits, which
//  follow every such bit through thousands of steps, ended up elsewhere on
//  five of the 24 test samples.
//

#pragma once

#include <cstdlib>
#include <new>
#include <vector>

namespace transmute
{
    template <typename T>
    struct SwiftAllocator
    {
        using value_type = T;

        /** Swift's array header, in front of the elements. */
        static constexpr size_t headerBytes = 32;

        SwiftAllocator() noexcept = default;
        template <typename U> SwiftAllocator (const SwiftAllocator<U>&) noexcept {}

        T* allocate (size_t n)
        {
            auto* block = static_cast<unsigned char*> (std::malloc (headerBytes + n * sizeof (T)));
            if (block == nullptr)
                throw std::bad_alloc();
            return reinterpret_cast<T*> (static_cast<void*> (block + headerBytes));
        }

        void deallocate (T* p, size_t) noexcept
        {
            std::free (static_cast<unsigned char*> (static_cast<void*> (p)) - headerBytes);
        }

        template <typename U> bool operator== (const SwiftAllocator<U>&) const noexcept { return true; }
    };

    using Doubles = std::vector<double, SwiftAllocator<double>>;
    using Floats  = std::vector<float, SwiftAllocator<float>>;
}
