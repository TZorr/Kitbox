//
//  FdnReverb.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  What Room and Hall share: a feedback delay network of eight lines.
//
//  Why a network and not Kitbox's plate for these too. A plate has no early
//  reflections and a dense onset, which is right for it and wrong for a room:
//  the sense of a space is mostly the first few hundred milliseconds. A
//  feedback delay network gets there cheaply - eight delay lines of unequal
//  length, their outputs mixed by a Hadamard matrix (orthonormal, so the
//  mixing loses no energy) and fed back into their inputs. Every line ends up
//  in every other, so the echo density grows exponentially with time, which is
//  what a room does.
//
//  Decay. Each line is given the gain that makes its own trip around the loop
//  drop by 60 dB in the chosen RT60: g = 10^(-3 * delay / (RT60 * fs)). A
//  one-pole low-pass in each line makes the highs die faster (Damp), as air and
//  soft surfaces do.
//
//  Level. A network's steady-state loudness grows with its decay time, so the
//  input is scaled down as the loop gets longer: by (1 - g_mean^2)^0.2. The
//  textbook exponent is 0.5 (it holds the energy in the loop constant), but
//  measured on noise it over-corrects here - the damping filters and the
//  decorrelating output taps take level out as well - and a Hall dropped 5 dB
//  across Size. 0.2 is the exponent that leaves the level within about 2 dB
//  across the whole knob (RackboxCheck prints the numbers).
//
//  Modulation. The read positions wander a few samples on slow sine LFOs, one
//  per line. Fixed delays ring at their own resonances on a long tail; a
//  wandering read turns the ringing into shimmer.
//
//  The pre-delay (Dsp::PreDelay, shared with the Plate) comes first, so the whole onset -
//  diffusion and early reflections included - moves later as one.
//
//  Two Schroeder allpasses in front of the network smear the input, so a click
//  does not arrive as eight discrete echoes.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class FdnReverb : public Effect
{
public:
    struct Tuning
    {
        float minDelayMs, maxDelayMs;   // the spread of the eight lines
        float minRt60, maxRt60;         // seconds, at Size 0 and Size 1
    };

    explicit FdnReverb (Tuning t) : tuning (t) {}

    void process (float* left, float* right, int n) noexcept override
    {
        updateFeedback (blockA (n), blockB (n));
        const auto preTarget = preDelaySamples();

        for (int i = 0; i < n; ++i)
        {
            auto x = preDelay.process (0.5f * (left[i] + right[i]), preTarget);
            x = diffuse (allpass[0], x, 0.6f);
            x = diffuse (allpass[1], x, 0.6f);

            float out[numLines];
            for (int l = 0; l < numLines; ++l)
            {
                const auto wobble = 6.0f * (float) scale * modulation[l].next();
                out[l] = lines[l].read (delaySamples[l] + wobble);
            }

            // Hadamard, in place: three butterfly stages, then 1/sqrt(8).
            float mixed[numLines];
            for (int l = 0; l < numLines; ++l)
                mixed[l] = out[l];

            for (int span = 1; span < numLines; span <<= 1)
                for (int base = 0; base < numLines; base += span * 2)
                    for (int k = base; k < base + span; ++k)
                    {
                        const auto p = mixed[k], q = mixed[k + span];
                        mixed[k] = p + q;
                        mixed[k + span] = p - q;
                    }

            for (int l = 0; l < numLines; ++l)
            {
                const auto fed = damping[l].process (mixed[l] * 0.35355339f) * gains[l];
                lines[l].push (fed + inputSigns[l] * inputGain * x);
            }

            // Even lines to the left, odd to the right, alternating signs so
            // the two outputs are decorrelated.
            left[i]  = 0.5f * (out[0] - out[2] + out[4] - out[6]);
            right[i] = 0.5f * (out[1] - out[3] + out[5] - out[7]);
        }
    }

protected:
    void onPrepare() override
    {
        scale = sampleRate / 48000.0;
        preDelay.prepare (sampleRate, Dsp::Sync::maxPreDelaySeconds);

        for (int l = 0; l < numLines; ++l)
        {
            // Geometric spread between the two limits, then nudged to a nearby prime
            // so no two lines share a factor.
            const auto fraction = (float) l / (float) (numLines - 1);
            const auto ms = tuning.minDelayMs * std::pow (tuning.maxDelayMs / tuning.minDelayMs, fraction);
            delayLength[l] = nearestPrime (juce::jmax (8, (int) (ms * 0.001 * sampleRate)));
            delaySamples[l] = (float) delayLength[l];
            lines[l].allocate (delayLength[l] + (int) (16.0 * scale) + 8);
            modulation[l].setRate (0.11f + 0.037f * (float) l, sampleRate);
        }

        allpass[0].line.allocate ((int) (0.0053 * sampleRate) + 4);
        allpass[0].delay = (int) (0.0053 * sampleRate);
        allpass[1].line.allocate ((int) (0.0077 * sampleRate) + 4);
        allpass[1].delay = (int) (0.0077 * sampleRate);
    }

    void onReset() override
    {
        for (int l = 0; l < numLines; ++l)
        {
            lines[l].clear();
            damping[l].reset();
            modulation[l].setPhase ((float) l / (float) numLines);
        }

        for (auto& a : allpass)
            a.line.clear();

        preDelay.reset();

        updateFeedback (targetA(), targetB());
    }

private:
    static constexpr int numLines = 8;

    struct Allpass
    {
        Dsp::DelayLine line;
        int delay = 1;
    };

    static float diffuse (Allpass& a, float x, float g) noexcept
    {
        const auto delayed = a.line.read ((float) a.delay);
        const auto v = x - g * delayed;
        a.line.push (v);
        return g * v + delayed;
    }

    static int nearestPrime (int n) noexcept
    {
        const auto isPrime = [] (int v)
        {
            if (v < 2) return false;
            for (int d = 2; d * d <= v; ++d)
                if (v % d == 0) return false;
            return true;
        };

        for (int offset = 0;; ++offset)
        {
            if (isPrime (n + offset)) return n + offset;
            if (n - offset > 1 && isPrime (n - offset)) return n - offset;
        }
    }

    void updateFeedback (float size, float damp) noexcept
    {
        const auto rt60 = tuning.minRt60 * std::pow (tuning.maxRt60 / tuning.minRt60, juce::jlimit (0.0f, 1.0f, size));
        const auto cutoff = Dsp::Map::exponential (1.0f - juce::jlimit (0.0f, 1.0f, damp), 1500.0f, 14000.0f);

        float sum = 0.0f;
        for (int l = 0; l < numLines; ++l)
        {
            gains[l] = std::pow (10.0f, -3.0f * (float) delayLength[l] / (rt60 * (float) sampleRate));
            damping[l].setCutoff (cutoff, sampleRate);
            sum += gains[l];
        }

        const auto mean = sum / (float) numLines;
        inputGain = 0.7f * std::pow (juce::jmax (1.0e-4f, 1.0f - mean * mean), 0.2f);
    }

    Tuning tuning;
    double scale = 1.0;

    Dsp::DelayLine lines[numLines];
    Dsp::OnePole damping[numLines];
    Dsp::Lfo modulation[numLines];
    Allpass allpass[2];
    Dsp::PreDelay preDelay;

    int delayLength[numLines] {};
    float delaySamples[numLines] {};
    float gains[numLines] {};
    float inputGain = 0.3f;

    static constexpr float inputSigns[numLines] { 1, -1, 1, 1, -1, 1, -1, -1 };
};
