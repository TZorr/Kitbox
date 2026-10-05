//
//  Fuzz.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  A hard, lopsided clipper. Fuzz is the gain into it (20 to 50 dB), Tone a
//  low-pass after it.
//
//  Two things separate it from the Overdrive. The signal is biased before the
//  shaper, so the positive and negative halves clip at different points: that
//  asymmetry adds even harmonics, the octave-up growl. And the shaper is a
//  soft clip followed by a hard one, so the top of every wave is flat.
//
//  The bias puts DC on the output, which a DC blocker removes; without it a
//  loud note would push the following blocks off centre. The output is held
//  down (x0.35) because at these gains every signal is at full scale.
//
//  At 20 to 50 dB of gain nearly every wave is clipped flat, and a flat top
//  has harmonics far past Nyquist. The whole curve - bias, soft clip, hard
//  clip - is therefore one shape for Dsp::Adaa, which needs its integral:
//  between the two corners of the hard clip that is softClip's integral less
//  the bias offset times the input, outside them a straight line at the clip
//  level. The bias lives inside the shape, so silence is the shape's zero and
//  a reset shaper starts from rest instead of from a step to the bias. The top
//  end the averaging softens is lifted back (Dsp::AdaaCompensation).
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

/** The Fuzz curve as an Adaa shape: in -> clip (1.5 (softClip (in + bias) - softClip (bias)), +-0.8). */
struct FuzzShape
{
    static constexpr double bias = 0.3, gain = 1.5, limit = 0.8;
    static constexpr double offset = Dsp::softClipCurve (bias);

    // The hard clip's corners, as inputs to softClip (bias included).
    static constexpr double cornerLow  = Dsp::softClipInverse (offset - limit / gain);
    static constexpr double cornerHigh = Dsp::softClipInverse (offset + limit / gain);

    static double value (double in) noexcept
    {
        const auto u = juce::jlimit (-3.0, 3.0, in + bias);
        return juce::jlimit (-limit, limit, gain * (Dsp::softClipCurve (u) - offset));
    }

    static double integral (double in) noexcept
    {
        const auto u = in + bias;

        if (u > cornerHigh) return between (cornerHigh) + limit * (u - cornerHigh);
        if (u < cornerLow)  return between (cornerLow) - limit * (u - cornerLow);
        return between (u);
    }

private:
    static double between (double u) noexcept   { return gain * (Dsp::softClipIntegral (u) - offset * u); }
};

class Fuzz : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            const auto gain = std::pow (10.0f, (20.0f + 30.0f * nextA()) / 20.0f);
            const auto cutoff = Dsp::Map::exponential (nextB(), 800.0f, 14000.0f);

            for (int c = 0; c < 2; ++c)
            {
                tone[c].setCutoff (cutoff, sampleRate);

                const auto y = lift[c].process (shaper[c].process (gain * channels[c][i]));
                channels[c][i] = tone[c].process (dc[c].process (y)) * 0.35f;
            }
        }
    }

protected:
    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            tone[c].reset();
            dc[c].reset();
            shaper[c].reset();
            lift[c].reset();
        }
    }

private:
    Dsp::OnePole tone[2];
    Dsp::DcBlocker dc[2];
    Dsp::Adaa<FuzzShape> shaper[2];
    Dsp::AdaaCompensation lift[2];
};
