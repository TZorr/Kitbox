//
//  Flanger.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  One short delay, swept between about 0.5 and 5.5 ms, fed back on itself.
//
//  The comb filter a flanger makes has its notches spaced 1/delay apart, so
//  sweeping the delay sweeps the notches - and feedback (fixed at 55 % here)
//  turns the notches into a ring. Rate is the sweep speed, Depth the travel.
//  Stereo comes from starting the two channels a quarter cycle apart.
//
//  Feedback is taken from the delayed signal, not from the output, so the dry
//  half added at the end never re-enters the loop.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Flanger : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };
        constexpr float feedback = 0.55f;

        if (phaseLocked())
        {
            const auto base = syncPhase(), amount = lockAmount (n);

            for (int c = 0; c < 2; ++c)
                lfo[c].pull (base + 0.25f * (float) c, amount, n);
        }

        for (int i = 0; i < n; ++i)
        {
            const auto rate = lfoRateHz (nextA());
            const auto depth = nextB();
            const auto ms = 0.001f * (float) sampleRate;

            for (int c = 0; c < 2; ++c)
            {
                lfo[c].setRate (rate, sampleRate);

                const auto delay = ms * (3.0f + 2.5f * depth * lfo[c].next());
                const auto delayed = line[c].read (delay);
                const auto x = channels[c][i];

                line[c].push (x + feedback * Dsp::softClip (delayed));
                channels[c][i] = 0.5f * (x + delayed);
            }
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) (0.01 * sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            line[c].clear();
            lfo[c].setPhase (0.25f * (float) c);
        }
    }

private:
    Dsp::DelayLine line[2];
    Dsp::Lfo lfo[2];
};
