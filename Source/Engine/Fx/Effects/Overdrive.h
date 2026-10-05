//
//  Overdrive.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  A soft clipper. Drive is the gain into it (0 to 36 dB), Tone a low-pass
//  after it (800 Hz to 14 kHz) that takes the fizz off.
//
//  The shaper is symmetric, so it adds odd harmonics only and no DC. The
//  output is pulled down by drive^0.4: a saturator gets louder as it is
//  pushed, but by much less than its gain, and this keeps the block roughly
//  level-matched across the Drive knob without making the low settings quiet.
//
//  The shaper is not oversampled but anti-aliased (Dsp::Adaa): it returns the
//  curve's average between two samples rather than its value at each, which
//  keeps most of the harmonics above Nyquist from folding back as grit - with
//  no latency, for half a sample of delay. The top end that averaging softens
//  is lifted back (Dsp::AdaaCompensation), so the Tone knob sounds as before.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Overdrive : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            const auto gain = std::pow (10.0f, 36.0f * nextA() / 20.0f);
            const auto cutoff = Dsp::Map::exponential (nextB(), 800.0f, 14000.0f);
            const auto makeUp = std::pow (gain, -0.4f);

            for (int c = 0; c < 2; ++c)
            {
                tone[c].setCutoff (cutoff, sampleRate);
                channels[c][i] = tone[c].process (lift[c].process (shaper[c].process (gain * channels[c][i]))) * makeUp;
            }
        }
    }

protected:
    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            tone[c].reset();
            shaper[c].reset();
            lift[c].reset();
        }
    }

private:
    Dsp::OnePole tone[2];
    Dsp::Adaa<Dsp::SoftClipShape> shaper[2];
    Dsp::AdaaCompensation lift[2];
};
