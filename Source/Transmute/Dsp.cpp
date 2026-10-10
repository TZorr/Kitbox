//
//  Dsp.cpp
//  Kitbox
//

#include "Dsp.h"

#include <algorithm>
#include <cmath>

#include <dispatch/dispatch.h>

namespace transmute
{
    Biquad::Biquad (Kind kind, double frequency, double q, double sampleRate)
    {
        const double f = std::min (frequency, 0.49 * sampleRate);
        const double w = 2 * M_PI * f / sampleRate;
        const double alpha = std::sin (w) / (2 * q);
        const double cosw = std::cos (w);
        const double a0 = 1 + alpha;

        switch (kind)
        {
            case Kind::lowPass:
                b0 = (1 - cosw) / 2; b1 = 1 - cosw; b2 = (1 - cosw) / 2;
                break;
            case Kind::highPass:
                b0 = (1 + cosw) / 2; b1 = -(1 + cosw); b2 = (1 + cosw) / 2;
                break;
            case Kind::bandPass:
                // Constant 0 dB peak gain.
                b0 = alpha; b1 = 0; b2 = -alpha;
                break;
        }

        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 = -2 * cosw / a0;
        a2 = (1 - alpha) / a0;
    }

    //==============================================================================
    RealFFT::RealFFT (int size)
        : n (size),
          log2n ((vDSP_Length) std::log2 ((double) size)),
          setup (vDSP_create_fftsetup (log2n, kFFTRadix2))
    {
    }

    RealFFT::~RealFFT()
    {
        vDSP_destroy_fftsetup (setup);
    }

    void RealFFT::forward (const float* input, float* real, float* imag) const
    {
        DSPSplitComplex split { real, imag };
        vDSP_ctoz (reinterpret_cast<const DSPComplex*> (input), 2, &split, 1, (vDSP_Length) half());
        vDSP_fft_zrip (setup, &split, 1, log2n, kFFTDirection_Forward);
    }

    void RealFFT::inverse (float* real, float* imag, float* output) const
    {
        DSPSplitComplex split { real, imag };
        vDSP_fft_zrip (setup, &split, 1, log2n, kFFTDirection_Inverse);
        vDSP_ztoc (&split, 1, reinterpret_cast<DSPComplex*> (output), 2, (vDSP_Length) half());
        float scale = 1 / (float) (2 * n);
        vDSP_vsmul (output, 1, &scale, output, 1, (vDSP_Length) n);
    }

    //==============================================================================
    Doubles Filters::zeroPhase (const Doubles& signal, std::vector<Biquad> sections)
    {
        auto y = signal;

        for (auto section : sections)
            section.run (y);

        std::reverse (y.begin(), y.end());

        for (auto section : sections)
        {
            section.reset();
            section.run (y);
        }

        std::reverse (y.begin(), y.end());
        return y;
    }

    Doubles Filters::lowPass (const Doubles& signal, double cutoff, double sampleRate)
    {
        return zeroPhase (signal, { Biquad (Biquad::Kind::lowPass, cutoff, 0.5412, sampleRate),
                                    Biquad (Biquad::Kind::lowPass, cutoff, 1.3066, sampleRate) });
    }

    Doubles Filters::highPass (const Doubles& signal, double cutoff, double sampleRate)
    {
        return zeroPhase (signal, { Biquad (Biquad::Kind::highPass, cutoff, 0.5412, sampleRate),
                                    Biquad (Biquad::Kind::highPass, cutoff, 1.3066, sampleRate) });
    }

    Doubles Filters::envelope (const Doubles& signal)
    {
        const auto n = (int) signal.size();

        if (n <= 1)
        {
            Doubles out (signal.size());
            for (size_t i = 0; i < signal.size(); ++i)
                out[i] = std::abs (signal[i]);
            return out;
        }

        int size = 2;
        while (size < 2 * n)
            size <<= 1;

        RealFFT fft (size);
        const auto half = size / 2;
        Floats input ((size_t) size, 0.0f), real ((size_t) half), imag ((size_t) half), quadrature ((size_t) size);

        for (int i = 0; i < n; ++i)
            input[(size_t) i] = (float) signal[(size_t) i];

        fft.forward (input.data(), real.data(), imag.data());

        // H multiplies positive frequencies by -i: (re, im) -> (im, -re).
        for (int k = 1; k < half; ++k)
        {
            const auto re = real[(size_t) k];
            real[(size_t) k] = imag[(size_t) k];
            imag[(size_t) k] = -re;
        }

        real[0] = 0;
        imag[0] = 0;
        fft.inverse (real.data(), imag.data(), quadrature.data());

        Doubles out ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto q = (double) quadrature[(size_t) i];
            out[(size_t) i] = std::sqrt (signal[(size_t) i] * signal[(size_t) i] + q * q);
        }
        return out;
    }

    //==============================================================================
    void parallel (int count, const std::function<void (int)>& work)
    {
        if (count <= 1)
        {
            for (int i = 0; i < count; ++i)
                work (i);
            return;
        }

        // DISPATCH_APPLY_AUTO, as Swift's concurrentPerform; its definition
        // carries a nullability qualifier -Wpedantic objects to.
       #pragma clang diagnostic push
       #pragma clang diagnostic ignored "-Wnullability-extension"
        dispatch_apply_f ((size_t) count, DISPATCH_APPLY_AUTO, const_cast<std::function<void (int)>*> (&work),
                          [] (void* context, size_t i)
                          {
                              (*static_cast<const std::function<void (int)>*> (context)) ((int) i);
                          });
       #pragma clang diagnostic pop
    }
}
