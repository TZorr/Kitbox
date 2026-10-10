//
//  Dsp.h
//  Kitbox
//
//  The small pieces Transmute's synth, analysis and comparison share, ported
//  from Filters.swift, RealFFT.swift and DrumSynth.swift's Xorshift: RBJ
//  biquads in Direct Form I, a zero-phase way to run them, the Hilbert
//  envelope, vDSP's packed real FFT, the seeded noise - and Fitter.parallel,
//  which is libdispatch's concurrentPerform here as there.
//
//  The arithmetic follows the Swift line by line, and every file of this port
//  is compiled with -ffp-contract=off (see CMakeLists.txt): Swift does not fuse
//  a multiply and an add, clang would, and a fused one rounds differently. A
//  fit takes thousands of steps that each depend on the last, so a difference
//  in the last bit at the start can end in a different fit.
//

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "SwiftVector.h"

#include <Accelerate/Accelerate.h>

namespace transmute
{
    /** One RBJ biquad section (Audio EQ Cookbook), Direct Form I in double. */
    struct Biquad
    {
        enum class Kind { lowPass, highPass, bandPass };

        Biquad (Kind kind, double frequency, double q, double sampleRate);

        inline double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }

        /** Filters `signal` in place, front to back. */
        void run (Doubles& signal) noexcept
        {
            for (auto& v : signal)
                v = process (v);
        }

        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    private:
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };

    /** White noise in [-1, 1), the same sequence for the same seed. */
    struct Xorshift
    {
        explicit Xorshift (uint64_t seed) : state (seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

        inline double next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return (double) (state >> 11) / (double) (1ULL << 52) - 1;
        }

    private:
        uint64_t state;
    };

    /** vDSP's real FFT in its packed form: bins 1 ..< N/2, with DC in real[0]
        and Nyquist in imag[0]. inverse(forward(x)) == x. */
    class RealFFT
    {
    public:
        explicit RealFFT (int size);
        ~RealFFT();

        RealFFT (const RealFFT&) = delete;
        RealFFT& operator= (const RealFFT&) = delete;

        int size() const noexcept { return n; }
        int half() const noexcept { return n / 2; }

        void forward (const float* input, float* real, float* imag) const;
        /** Overwrites real / imag. */
        void inverse (float* real, float* imag, float* output) const;

    private:
        int n;
        vDSP_Length log2n;
        FFTSetup setup;
    };

    namespace Filters
    {
        /** A cascade run forward, then backward: no delay, magnitude squared. */
        Doubles zeroPhase (const Doubles& signal, std::vector<Biquad> sections);

        /** Fourth-order Butterworth (two sections), run zero-phase. */
        Doubles lowPass (const Doubles& signal, double cutoff, double sampleRate);
        Doubles highPass (const Doubles& signal, double cutoff, double sampleRate);

        /** |x + iH{x}|, the analytic magnitude. */
        Doubles envelope (const Doubles& signal);
    }

    /** `work` for 0 ..< count at once, one per core (DispatchQueue.concurrentPerform). */
    void parallel (int count, const std::function<void (int)>& work);

    /** Swift's `max(by:)` / `min(by:)`: the first index of the largest / smallest. */
    template <typename Container>
    int firstMaxIndex (const Container& x, int begin, int end)
    {
        int best = begin;
        for (int i = begin + 1; i < end; ++i)
            if (x[(size_t) best] < x[(size_t) i])
                best = i;
        return best;
    }
}
