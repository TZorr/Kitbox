//
//  Comparison.cpp
//  Kitbox
//

#include "Comparison.h"
#include "DrumSynth.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace transmute
{
    Comparison::Comparison (const Floats& target, double rate, std::vector<PitchPoint> track)
        : sampleRate (rate),
          frameCount ((int) target.size()),
          pitchTrack (std::move (track)),
          envelopeHop (std::max ((int) (0.002 * rate), 1))
    {
        int envelopeSize = 2;
        while (envelopeSize < 2 * frameCount)
            envelopeSize <<= 1;
        envelopeFFT = std::make_unique<RealFFT> (envelopeSize);

        for (const int size : { 64, 256, 1024, 4096 })
        {
            Resolution r;
            r.size = size;
            r.hop = size / 2;
            r.fft = std::make_unique<RealFFT> (size);
            r.window.resize ((size_t) size);
            vDSP_hann_window (r.window.data(), (vDSP_Length) size, vDSP_HANN_NORM);
            r.bands = bands (size, sampleRate);
            r.frameLimit = size == 64 ? (int) (0.05 * sampleRate) / (size / 2) + 1 : INT_MAX;
            r.target = spectrogram (target, r);
            r.floors = floors (r.target, (int) r.bands.size());
            resolutions.push_back (std::move (r));
        }

        targetEnvelope = envelope (target);
        envelopeFloor = floors (targetEnvelope, 1)[0];
    }

    MatchScore Comparison::score (const Floats& candidate) const
    {
        const int count = (int) resolutions.size();
        Doubles parts ((size_t) count + 1);

        parallel (count + 1, [&] (int i)
        {
            if (i == count)
            {
                parts[(size_t) i] = distance (envelope (candidate), targetEnvelope, { envelopeFloor }, 1);
                return;
            }
            const auto& r = resolutions[(size_t) i];
            parts[(size_t) i] = distance (spectrogram (candidate, r), r.target, r.floors, (int) r.bands.size());
        });

        double spectral = 0.0;
        for (int i = 0; i < count; ++i)
            spectral += parts[(size_t) i];
        spectral /= (double) count;

        return { spectral, parts[(size_t) count], 0 };
    }

    MatchScore Comparison::score (const DrumParams& params) const
    {
        auto result = score (DrumSynth::render (params, sampleRate, frameCount));
        result.pitchSemitones = pitchError (params);
        return result;
    }

    double Comparison::pitchError (const DrumParams& p) const
    {
        if (pitchTrack.empty())
            return 0;

        double sum = 0.0, weights = 0.0;
        for (const auto& point : pitchTrack)
        {
            const double model = p.pitch (point.time);
            sum += point.weight * std::abs (12 * std::log2 (std::max (model, 1.0) / point.hz));
            weights += point.weight;
        }
        return weights > 0 ? sum / weights : 0;
    }

    //==============================================================================
    Floats Comparison::spectrogram (const Floats& x, const Resolution& r) const
    {
        const int half = r.size / 2;
        const int frames = std::min (frameCount / r.hop + 1, r.frameLimit);
        const int bandCount = (int) r.bands.size();
        Floats out ((size_t) (frames * bandCount), 0.0f);
        Floats frame ((size_t) r.size), real ((size_t) half), imag ((size_t) half), power ((size_t) half);
        const int length = (int) x.size();

        for (int f = 0; f < frames; ++f)
        {
            const int start = f * r.hop - half;
            for (int i = 0; i < r.size; ++i)
            {
                const int j = start + i;
                frame[(size_t) i] = j >= 0 && j < length ? x[(size_t) j] * r.window[(size_t) i] : 0.0f;
            }

            r.fft->forward (frame.data(), real.data(), imag.data());
            DSPSplitComplex split { real.data(), imag.data() };
            vDSP_zvmags (&split, 1, power.data(), 1, (vDSP_Length) half);
            power[0] = 0;   // DC and Nyquist packed; neither is a band

            for (int b = 0; b < bandCount; ++b)
            {
                const auto& band = r.bands[(size_t) b];
                float sum = 0;
                vDSP_sve (power.data() + band.lower, 1, &sum, (vDSP_Length) (band.upper - band.lower));
                out[(size_t) (f * bandCount + b)] = 10 * std::log10 (sum + 1e-20f);
            }
        }

        return out;
    }

    Floats Comparison::envelope (const Floats& x) const
    {
        const int n = std::min ((int) x.size(), frameCount);
        const int size = envelopeFFT->size();
        const int half = envelopeFFT->half();
        Floats input ((size_t) size, 0.0f);
        std::copy (x.begin(), x.begin() + n, input.begin());

        Floats real ((size_t) half), imag ((size_t) half), quadrature ((size_t) size);
        envelopeFFT->forward (input.data(), real.data(), imag.data());

        // -i on the positive frequencies.
        std::swap (real, imag);
        float minusOne = -1;
        vDSP_vsmul (imag.data(), 1, &minusOne, imag.data(), 1, (vDSP_Length) half);
        real[0] = 0;
        imag[0] = 0;
        envelopeFFT->inverse (real.data(), imag.data(), quadrature.data());

        Floats power ((size_t) std::max (n, 0));
        vDSP_vmul (input.data(), 1, input.data(), 1, power.data(), 1, (vDSP_Length) n);
        vDSP_vma (quadrature.data(), 1, quadrature.data(), 1, power.data(), 1, power.data(), 1, (vDSP_Length) n);

        const int frames = std::max (frameCount / envelopeHop, 1);
        Floats out ((size_t) frames);

        for (int f = 0; f < frames; ++f)
        {
            const int count = std::min (envelopeHop, n - f * envelopeHop);
            float mean = 0;
            if (count > 0)
                vDSP_meanv (power.data() + f * envelopeHop, 1, &mean, (vDSP_Length) count);
            out[(size_t) f] = 10 * std::log10 (mean + 1e-20f);
        }

        return out;
    }

    double Comparison::distance (const Floats& a, const Floats& b,
                                 const Floats& floors, int bands)
    {
        const size_t count = std::min (a.size(), b.size());
        const float maxA = a.empty() ? 0.0f : *std::max_element (a.begin(), a.end());
        const float maxB = b.empty() ? 0.0f : *std::max_element (b.begin(), b.end());
        const double loudest = (double) std::max (maxA, maxB);
        double sum = 0.0, weights = 0.0;

        for (size_t i = 0; i < count; ++i)
        {
            const float floor = floors[i % (size_t) bands];
            const float x = std::max (a[i], floor), y = std::max (b[i], floor);

            if (x > floor || y > floor)
            {
                const double weight = std::pow (10.0, ((double) std::max (x, y) - loudest) / 20);
                sum += weight * (double) std::abs (x - y);
                weights += weight;
            }
        }

        return weights > 0 ? sum / weights : 0;
    }

    Floats Comparison::floors (const Floats& levels, int bands)
    {
        const float loudest = levels.empty() ? 0.0f : *std::max_element (levels.begin(), levels.end());
        Floats out;

        for (int b = 0; b < bands; ++b)
        {
            Floats column;
            for (size_t i = (size_t) b; i < levels.size(); i += (size_t) bands)
                column.push_back (levels[i]);
            std::sort (column.begin(), column.end());

            const float quiet = column.empty() ? loudest : column[column.size() / 10];
            out.push_back (std::max (quiet + 3, loudest - 80));
        }

        return out;
    }

    std::vector<Comparison::Band> Comparison::bands (int size, double sampleRate)
    {
        const double binHz = sampleRate / (double) size;
        const int nyquistBin = size / 2;
        std::vector<Band> ranges;
        double edge = std::max (20.0, 2 * binHz);
        int low = std::max ((int) std::ceil (edge / binHz), 2);

        while (low < nyquistBin)
        {
            edge *= std::pow (2.0, 1.0 / 6);
            const int high = std::min ((int) std::ceil (edge / binHz), nyquistBin);
            if (high > low)
            {
                ranges.push_back ({ low, high });
                low = high;
            }
        }

        return ranges;
    }
}
