//
//  DrumSynth.cpp
//  Kitbox
//
//  Line for line DrumSynth.swift and AttackNoise.swift; the comments there say
//  why each layer is built the way it is.
//

#include "DrumSynth.h"
#include "Dsp.h"

#include <algorithm>
#include <cmath>

namespace transmute
{
    namespace
    {
        using Kind = Biquad::Kind;

        Doubles voice (const DrumParams& p, double sampleRate, int n);
        Doubles modal (const DrumParams& p, double sr, int n);
        Doubles click (const DrumParams& p, double sampleRate, int n);
        Doubles noiseLayer (const DrumParams& p, double sampleRate, int n);
        Doubles wires (const DrumParams& p, double sr, int n);
        Doubles clap (const DrumParams& p, double sr, int n);
        Doubles hat (const DrumParams& p, double sr, int n);

        void applyFilter (const DrumParams& p, Doubles& x, double sampleRate)
        {
            std::vector<Biquad> sections;

            switch (p.filterType)
            {
                case FilterType::off:
                    return;
                case FilterType::lowPass:
                    sections = { Biquad (Kind::lowPass, p.filterCutoff, 0.5412, sampleRate),
                                 Biquad (Kind::lowPass, p.filterCutoff, p.filterQ * 1.3066 / 0.7071, sampleRate) };
                    break;
                case FilterType::highPass:
                    sections = { Biquad (Kind::highPass, p.filterCutoff, 0.5412, sampleRate),
                                 Biquad (Kind::highPass, p.filterCutoff, p.filterQ * 1.3066 / 0.7071, sampleRate) };
                    break;
                case FilterType::bandPass:
                    sections = { Biquad (Kind::bandPass, p.filterCutoff, p.filterQ, sampleRate),
                                 Biquad (Kind::bandPass, p.filterCutoff, p.filterQ, sampleRate) };
                    break;
            }

            for (auto section : sections)
                section.run (x);
        }

        std::optional<int> envelopeEndFrame (const DrumParams& p, double sampleRate)
        {
            if (! p.envelopeOn)
                return std::nullopt;
            return std::max ((int) std::round (p.envelopeEnd() * sampleRate), 1);
        }

        std::optional<int> applyEnvelope (const DrumParams& p, Doubles& x, double sampleRate)
        {
            const auto endFrame = envelopeEndFrame (p, sampleRate);
            if (! endFrame)
                return std::nullopt;

            const int end = *endFrame;
            const int hold = std::min (std::max ((int) std::round (p.envHold * sampleRate), 0), end - 1);
            const double span = (double) std::max (end - 1 - hold, 1);
            const int n = (int) x.size();

            for (int i = std::min (hold, n); i < std::min (end, n); ++i)
                x[(size_t) i] *= std::pow (std::max (1 - (double) (i - hold) / span, 0.0), p.envCurve);

            for (int i = end; i < n; ++i)
                x[(size_t) i] = 0;

            return end;
        }

        //==============================================================================
        Doubles voice (const DrumParams& p, double sampleRate, int n)
        {
            if (n <= 0)
                return {};

            const auto count = (vDSP_Length) n;
            Doubles t ((size_t) n);
            double start = 0.0, step = 1 / sampleRate;
            vDSP_vrampD (&start, &step, t.data(), 1, count);
            int size = n;

            // Phase, closed form.
            const double tau = p.pitchDecay;
            const double delta = p.pitchStart - p.fundamental;
            Doubles decay ((size_t) n);
            double scale = -1 / tau;
            vDSP_vsmulD (t.data(), 1, &scale, decay.data(), 1, count);
            vvexp (decay.data(), decay.data(), &size);
            const double phase0 = p.startPhase * M_PI / 180;
            Doubles phase ((size_t) n);
            for (int i = 0; i < n; ++i)
                phase[(size_t) i] = 2 * M_PI * (p.fundamental * t[(size_t) i] + delta * tau * (1 - decay[(size_t) i])) + phase0;

            Doubles wave ((size_t) n);
            vvsin (wave.data(), phase.data(), &size);

            // Envelope: a half-cosine rise over the attack, then exp(-(u/tau)^k).
            const double attack = p.ampAttack;
            Doubles u ((size_t) n);
            double shift = -attack;
            vDSP_vsaddD (t.data(), 1, &shift, u.data(), 1, count);
            double zero = 0.0, inverse = 1 / p.ampDecay;
            vDSP_vthrD (u.data(), 1, &zero, u.data(), 1, count);
            vDSP_vsmulD (u.data(), 1, &inverse, u.data(), 1, count);
            vvlog (u.data(), u.data(), &size);
            double k = p.ampShape;
            vDSP_vsmulD (u.data(), 1, &k, u.data(), 1, count);
            vvexp (u.data(), u.data(), &size);
            double minusOne = -1.0;
            vDSP_vsmulD (u.data(), 1, &minusOne, u.data(), 1, count);
            vvexp (u.data(), u.data(), &size);

            const int attackFrames = std::min (n, (int) (attack * sampleRate));
            for (int i = 0; i < attackFrames; ++i)
            {
                const double x = std::sin (0.5 * M_PI * t[(size_t) i] / attack);
                u[(size_t) i] = x * x;
            }
            vDSP_vmulD (wave.data(), 1, u.data(), 1, wave.data(), 1, count);

            if (p.drive > 1e-4)
            {
                const double d = p.drive, norm = 1 / std::tanh (d);
                for (int i = 0; i < n; ++i)
                    wave[(size_t) i] = std::tanh (d * wave[(size_t) i]) * norm;
            }

            // A snare's second mode, after the drive.
            if (p.isSnare() && p.mode2Level > 0)
            {
                const double ratio = p.mode2Ratio, rate = -1 / p.mode2Decay;
                Doubles mode ((size_t) n);
                for (int i = 0; i < n; ++i)
                    mode[(size_t) i] = ratio * (phase[(size_t) i] - phase0) + phase0;
                vvsin (mode.data(), mode.data(), &size);

                for (int i = 0; i < n; ++i)
                {
                    double rise;
                    if (i < attackFrames)
                    {
                        const double x = std::sin (0.5 * M_PI * t[(size_t) i] / attack);
                        rise = x * x;
                    }
                    else
                    {
                        rise = std::exp (std::max (t[(size_t) i] - attack, 0.0) * rate);
                    }
                    wave[(size_t) i] += p.mode2Level * rise * mode[(size_t) i];
                }
            }

            return wave;
        }

        //==============================================================================
        Doubles click (const DrumParams& p, double sampleRate, int n)
        {
            const int m = std::min (n, std::max ((int) (12 * p.clickDecay * sampleRate), 16));
            Xorshift random (0xC11C0001ULL);
            Biquad filter (Kind::highPass, p.clickTone, 0.7071, sampleRate);
            Doubles burst ((size_t) m);

            for (int i = 0; i < m; ++i)
            {
                const double env = std::exp (-(double) i / (p.clickDecay * sampleRate));
                burst[(size_t) i] = filter.process (random.next() * env);
            }

            double peak = 0.0;
            vDSP_maxmgvD (burst.data(), 1, &peak, (vDSP_Length) m);
            if (peak > 0)
            {
                double s = 1 / peak;
                vDSP_vsmulD (burst.data(), 1, &s, burst.data(), 1, (vDSP_Length) m);
            }
            return burst;
        }

        Doubles noiseLayer (const DrumParams& p, double sampleRate, int n)
        {
            if (p.isSnare())
                return wires (p, sampleRate, n);

            const int m = std::min (n, std::max ((int) (12 * p.noiseDecay * sampleRate), 2048));
            Xorshift random (0x5E110002ULL);
            Biquad filter (Kind::bandPass, p.noiseTone, 0.8, sampleRate);
            Doubles band ((size_t) m);

            for (int i = 0; i < m; ++i)
                band[(size_t) i] = filter.process (random.next());

            double rms = 0.0;
            vDSP_rmsqvD (band.data(), 1, &rms, (vDSP_Length) m);
            const double norm = rms > 0 ? 1 / rms : 0;
            const double rate = 1 / (p.noiseDecay * sampleRate);

            for (int i = 0; i < m; ++i)
                band[(size_t) i] *= norm * std::exp (-(double) i * rate);

            return band;
        }

        Doubles wires (const DrumParams& p, double sr, int n)
        {
            const double k = p.noiseShape;
            const int m = std::min (n, std::max ((int) (p.noiseDecay * std::pow (12.0, 1 / k) * sr), 2048));
            Xorshift random (0x5E110003ULL);
            Doubles band ((size_t) m);
            for (auto& v : band)
                v = random.next();

            band = DrumSynth::bandPass (band, p, sr);
            const double step = 1 / (p.noiseDecay * sr);

            for (int i = 0; i < m; ++i)
                band[(size_t) i] *= std::exp (-std::pow ((double) i * step, k));

            return band;
        }

        //==============================================================================
        Doubles modal (const DrumParams& p, double sr, int n)
        {
            if (n <= 0)
                return {};

            Doubles out ((size_t) n, 0.0);
            const int attackFrames = std::min (n, (int) (p.ampAttack * sr));
            const double k = p.ampShape;

            for (const auto& mode : p.modes())
            {
                if (! (mode.level > 0 && mode.hz < 0.49 * sr))
                    continue;

                const int m = std::min (n, attackFrames + (int) (mode.decay * std::pow (12.0, 1 / k) * sr) + 1);
                int size = m;
                double zero = 0.0, radians = 2 * M_PI * mode.hz / sr;
                Doubles wave ((size_t) m);
                vDSP_vrampD (&zero, &radians, wave.data(), 1, (vDSP_Length) m);
                vvsin (wave.data(), wave.data(), &size);

                Doubles envelope ((size_t) m, 0.0);
                for (int i = 0; i < std::min (attackFrames, m); ++i)
                {
                    const double x = std::sin (0.5 * M_PI * (double) i / (double) attackFrames);
                    envelope[(size_t) i] = x * x;
                }

                const int rest = m - std::min (attackFrames, m);
                if (rest > 0)
                {
                    double* tail = envelope.data() + (m - rest);
                    double start = 0.0, step = 1 / (mode.decay * sr);
                    vDSP_vrampD (&start, &step, tail, 1, (vDSP_Length) rest);
                    int count = rest;
                    if (k != 1)
                    {
                        double exponent = k;
                        vvpows (tail, &exponent, tail, &count);
                    }
                    double minusOne = -1.0;
                    vDSP_vsmulD (tail, 1, &minusOne, tail, 1, (vDSP_Length) rest);
                    vvexp (tail, tail, &count);
                }

                vDSP_vmulD (envelope.data(), 1, wave.data(), 1, wave.data(), 1, (vDSP_Length) m);
                double level = mode.level;
                vDSP_vsmaD (wave.data(), 1, &level, out.data(), 1, out.data(), 1, (vDSP_Length) m);
            }

            return out;
        }

        //==============================================================================
        Doubles randomBlock (uint64_t seed, int count)
        {
            Xorshift random (seed);
            Doubles x ((size_t) std::max (count, 0));
            for (auto& v : x)
                v = random.next();
            return x;
        }

        Doubles clap (const DrumParams& p, double sr, int n)
        {
            const double k = p.noiseShape;
            const double tailLength = p.noise > 0 ? p.noiseDecay * std::pow (12.0, 1 / k) : 0;
            const int m = std::min (n, std::max ((int) ((p.tailStart() + std::max (tailLength, 12 * p.clapBurstDecay)) * sr) + 1, 2048));

            if (m <= 0)
                return {};

            const auto bursts = DrumSynth::bandPass (randomBlock (0x5E110005ULL, m), p, sr);

            std::vector<int> starts;
            for (int i = 0; i < p.burstCount(); ++i)
                starts.push_back ((int) std::round ((double) i * p.clapSpacing * sr));

            const int last = starts.empty() ? 0 : starts.back();
            Doubles tail ((size_t) m, 0.0);

            if (p.noise > 0 && last < m)
            {
                auto band = p;
                band.noiseTone = p.clapTailTone;
                band.noiseWidth = p.clapTailWidth;
                const auto stream = DrumSynth::bandPass (randomBlock (0x5E110006ULL, m - last), band, sr);
                for (size_t i = 0; i < stream.size(); ++i)
                    tail[(size_t) last + i] = stream[i];
            }

            const double burstStep = 1 / (p.clapBurstDecay * sr), tailStep = 1 / (p.noiseDecay * sr);
            Doubles out ((size_t) m);

            for (int i = 0; i < m; ++i)
            {
                double envelope = 0.0;
                for (const auto s : starts)
                    if (i >= s)
                        envelope += std::exp (-(double) (i - s) * burstStep);

                out[(size_t) i] = envelope * bursts[(size_t) i];

                if (i >= last)
                    out[(size_t) i] += p.noise * std::exp (-std::pow ((double) (i - last) * tailStep, k)) * tail[(size_t) i];
            }

            return out;
        }

        //==============================================================================
        double blep (double t, double dt)
        {
            if (t < dt)
            {
                const double x = t / dt;
                return x + x - x * x - 1;
            }
            if (t > 1 - dt)
            {
                const double x = (t - 1) / dt;
                return x * x + x + x + 1;
            }
            return 0;
        }

        Doubles hat (const DrumParams& p, double sr, int n)
        {
            const int m = std::min (n, std::max ((int) ((p.ampAttack + p.noiseDecay * std::pow (12.0, 1 / p.noiseShape)) * sr), 2048));

            if (m <= 0)
                return {};

            Doubles out ((size_t) m, 0.0);

            if (p.noise > 0)
            {
                const auto band = DrumSynth::bandPass (randomBlock (0x5E110004ULL, m), p, sr);
                for (int i = 0; i < m; ++i)
                    out[(size_t) i] += p.noise * band[(size_t) i];
            }

            if (p.metal > 0)
            {
                const auto band = DrumSynth::bandPass (DrumSynth::metalSource (p.metalTone, sr, m), p, sr);
                for (int i = 0; i < m; ++i)
                    out[(size_t) i] += p.metal * band[(size_t) i];
            }

            const auto envelope = DrumSynth::hatEnvelope (p, sr, m);
            for (int i = 0; i < m; ++i)
                out[(size_t) i] *= envelope[(size_t) i];

            return out;
        }
    }

    //==============================================================================
    int DrumSynth::delayFrames (const DrumParams& p, double sampleRate, int frames)
    {
        return std::min (std::max ((int) std::round (p.delay * sampleRate), 0), frames);
    }

    Doubles DrumSynth::body (const DrumParams& p, double sampleRate, int frames)
    {
        if (p.isHat() || p.isClap())
            return Doubles ((size_t) frames, 0.0);

        const int d = delayFrames (p, sampleRate, frames);
        auto make = p.isModal() ? modal : voice;

        if (d <= 0)
            return make (p, sampleRate, frames);

        Doubles out ((size_t) d, 0.0);
        const auto rest = make (p, sampleRate, frames - d);
        out.insert (out.end(), rest.begin(), rest.end());
        return out;
    }

    Doubles DrumSynth::bandPass (const Doubles& x, const DrumParams& p, double sr)
    {
        const double half = std::pow (2.0, 0.5 * p.noiseWidth);
        const double low = std::min (std::max (p.noiseTone / half, 20.0), 0.45 * sr);
        const double high = std::min (p.noiseTone * half, 0.45 * sr);

        std::vector<Biquad> sections;
        if (p.isHat())
            sections = { Biquad (Kind::highPass, low, 0.5412, sr), Biquad (Kind::highPass, low, 1.3066, sr),
                         Biquad (Kind::lowPass, high, 0.5412, sr), Biquad (Kind::lowPass, high, 1.3066, sr) };
        else
            sections = { Biquad (Kind::highPass, low, 0.7071, sr), Biquad (Kind::lowPass, high, 0.7071, sr) };

        Doubles band (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            double y = x[i];
            for (auto& section : sections)
                y = section.process (y);
            band[i] = y;
        }

        double rms = 0.0;
        vDSP_rmsqvD (band.data(), 1, &rms, (vDSP_Length) band.size());
        if (rms > 0)
        {
            double norm = 1 / rms;
            vDSP_vsmulD (band.data(), 1, &norm, band.data(), 1, (vDSP_Length) band.size());
        }
        return band;
    }

    Doubles DrumSynth::metalSource (double tone, double sr, int n)
    {
        static const double hz[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        static const double phases[] = { 0, 0.37, 0.71, 0.13, 0.52, 0.89 };

        Doubles out ((size_t) std::max (n, 0), 0.0);

        for (int j = 0; j < 6; ++j)
        {
            const double ratio = hz[j] / 205.3;
            const double dt = std::min (tone * ratio / sr, 0.49);
            double phase = phases[j];

            for (int i = 0; i < n; ++i)
            {
                double v = phase < 0.5 ? 1.0 : -1.0;
                v += blep (phase, dt);
                double other = phase + 0.5;
                if (other >= 1) other -= 1;
                v -= blep (other, dt);
                out[(size_t) i] += v;
                phase += dt;
                if (phase >= 1) phase -= 1;
            }
        }

        return out;
    }

    Doubles DrumSynth::hatEnvelope (const DrumParams& p, double sr, int n)
    {
        const int attackFrames = std::min (n, (int) (p.ampAttack * sr));
        const double step = 1 / (p.noiseDecay * sr), k = p.noiseShape;
        Doubles out ((size_t) std::max (n, 0));

        for (int i = 0; i < n; ++i)
        {
            if (i < attackFrames)
            {
                const double x = std::sin (0.5 * M_PI * (double) i / (double) std::max (attackFrames, 1));
                out[(size_t) i] = x * x;
            }
            else
            {
                out[(size_t) i] = std::exp (-std::pow ((double) (i - attackFrames) * step, k));
            }
        }

        return out;
    }

    Floats DrumSynth::render (const DrumParams& p, double sampleRate, std::optional<int> frames)
    {
        const int total = std::max ((int) std::round (p.renderLength() * sampleRate), 1);
        const int n = std::min (frames.value_or (total), total);
        auto out = body (p, sampleRate, n);

        const int d = delayFrames (p, sampleRate, n);

        if (p.transient > 0 && n > d)
        {
            const auto burst = click (p, sampleRate, n - d);
            for (size_t i = 0; i < burst.size(); ++i)
                out[(size_t) d + i] += p.transient * burst[i];
        }

        if (p.isHat())
        {
            if (p.noise + p.metal > 0 && n > d)
            {
                const auto layer = hat (p, sampleRate, n - d);
                for (size_t i = 0; i < layer.size(); ++i)
                    out[(size_t) d + i] += layer[i];
            }
        }
        else if (p.isClap())
        {
            if (n > d)
            {
                const auto layer = clap (p, sampleRate, n - d);
                for (size_t i = 0; i < layer.size(); ++i)
                    out[(size_t) d + i] += layer[i];
            }
        }
        else if (p.noise > 0 && n > d)
        {
            const auto layer = noiseLayer (p, sampleRate, n - d);
            for (size_t i = 0; i < layer.size(); ++i)
                out[(size_t) d + i] += p.noise * layer[i];
        }

        if (p.attack)
        {
            const auto layer = AttackNoise::render (*p.attack, sampleRate, n);
            const double level = std::pow (10.0, p.attackLevelDB / 20);
            for (size_t i = 0; i < layer.size(); ++i)
                out[i] += level * layer[i];
        }

        applyFilter (p, out, sampleRate);
        const auto envelopeEnd = applyEnvelope (p, out, sampleRate);

        double gain = std::pow (10.0, p.gainDB / 20);
        vDSP_vsmulD (out.data(), 1, &gain, out.data(), 1, (vDSP_Length) n);

        // The fade, where the drum really ends, not after the master envelope.
        if (n == total && ! (envelopeEnd && *envelopeEnd <= n))
        {
            const int fade = std::min ((int) (p.fadeSeconds() * sampleRate), (int) (0.3 * (double) n));
            for (int i = 0; i < fade; ++i)
                out[(size_t) (n - 1 - i)] *= 0.5 - 0.5 * std::cos (M_PI * (double) i / (double) fade);
        }

        Floats result ((size_t) frames.value_or (n), 0.0f);
        vDSP_vdpsp (out.data(), 1, result.data(), 1, (vDSP_Length) n);
        return result;
    }

    //==============================================================================
    //  AttackNoise
    namespace
    {
        constexpr double bandQ = 4.32;
        constexpr double attackHop = 0.001;
        constexpr double attackWindow = 0.05;

        const Doubles& bandCentres()
        {
            static const Doubles centres = []
            {
                Doubles c;
                for (int i = 0; i < 24; ++i)
                    c.push_back (80 * std::pow (2.0, (double) i / 3));
                return c;
            }();
            return centres;
        }

        std::vector<Doubles> bandLevels (const Doubles& x, double sr)
        {
            const int frames = (int) (attackWindow / attackHop);
            const int step = std::max ((int) (attackHop * sr), 1);
            const int n = std::min ((int) x.size(), (int) ((attackWindow + 0.03) * sr));
            const Doubles segment (x.begin(), x.begin() + n);

            std::vector<Doubles> out;

            for (const auto centre : bandCentres())
            {
                if (! (centre < 0.45 * sr))
                {
                    out.emplace_back ((size_t) frames, 0.0);
                    continue;
                }

                const auto band = Filters::zeroPhase (segment, { Biquad (Kind::bandPass, centre, bandQ, sr),
                                                                 Biquad (Kind::bandPass, centre, bandQ, sr) });
                Doubles levels ((size_t) frames);

                for (int f = 0; f < frames; ++f)
                {
                    const int centreSample = f * step;
                    const int lo = std::max (centreSample - step, 0), hi = std::min (centreSample + step, (int) band.size());
                    if (hi <= lo)
                    {
                        levels[(size_t) f] = 0;
                        continue;
                    }
                    double ms = 0.0;
                    vDSP_measqvD (band.data() + lo, 1, &ms, (vDSP_Length) (hi - lo));
                    levels[(size_t) f] = std::sqrt (ms);
                }

                out.push_back (std::move (levels));
            }

            return out;
        }
    }

    AttackTable AttackNoise::measure (const Doubles& original, const Doubles& synth,
                                      double gain, double floorRatio, double sr)
    {
        const auto o = bandLevels (original, sr);
        const auto s = bandLevels (synth, sr);

        double peak = 0;
        for (const auto v : original)
            peak = std::max (peak, std::abs (v));

        const double floorPower = std::pow (peak * floorRatio, 2.0) / (double) bandCentres().size();

        AttackTable table;
        table.bands = bandCentres();
        table.hop = attackHop;

        for (size_t b = 0; b < bandCentres().size(); ++b)
        {
            std::vector<float> levels (o[b].size());
            for (size_t f = 0; f < o[b].size(); ++f)
            {
                const double missing = o[b][f] * o[b][f] - s[b][f] * s[b][f] - floorPower;
                levels[f] = missing > 0 ? (float) (std::sqrt (missing) / std::max (gain, 1e-9)) : 0.0f;
            }
            table.levels.push_back (std::move (levels));
        }

        return table;
    }

    Doubles AttackNoise::render (const AttackTable& table, double sr, int n)
    {
        Doubles out ((size_t) n, 0.0);
        const int frames = table.levels.empty() ? 0 : (int) table.levels.front().size();

        if (frames <= 0)
            return out;

        const int m = std::min (n, (int) (table.hop * (double) frames * sr));
        const int length = std::max (m, 2048);

        for (size_t b = 0; b < table.bands.size(); ++b)
        {
            const double centre = table.bands[b];
            if (! (centre < 0.45 * sr))
                continue;

            const auto& levels = table.levels[b];
            if (std::none_of (levels.begin(), levels.end(), [] (float v) { return v > 0; }))
                continue;

            Xorshift random (0xA77AC000ULL + (uint64_t) b);
            Biquad f1 (Kind::bandPass, centre, bandQ, sr);
            Biquad f2 (Kind::bandPass, centre, bandQ, sr);

            const int settle = (int) (4 * sr / centre) + 64;
            for (int i = 0; i < settle; ++i)
                (void) f2.process (f1.process (random.next()));

            Doubles band ((size_t) length);
            for (int i = 0; i < length; ++i)
                band[(size_t) i] = f2.process (f1.process (random.next()));

            double rms = 0.0;
            vDSP_rmsqvD (band.data(), 1, &rms, (vDSP_Length) length);
            if (! (rms > 0))
                continue;

            for (int i = 0; i < m; ++i)
            {
                const double position = (double) i / sr / table.hop;
                const int f = (int) position;
                const double frac = position - (double) f;
                const double a = f < frames ? (double) levels[(size_t) f] : 0;
                const double c = f + 1 < frames ? (double) levels[(size_t) f + 1] : 0;
                out[(size_t) i] += band[(size_t) i] / rms * (a + (c - a) * frac);
            }
        }

        return out;
    }
}
