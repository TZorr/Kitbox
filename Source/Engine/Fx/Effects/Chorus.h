//
//  Chorus.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  Two detuned copies of the signal per channel, each read from a delay line
//  whose length is swept by its own sine LFO. Rate is the sweep speed, Depth is
//  how far the delay travels (up to 6 ms around a 14 ms centre).
//
//  The copies are what make it a chorus: the sweep changes the delay, a changing
//  delay is a pitch shift, and the ear hears two slightly different pitches
//  beating against the dry signal. Left and right sweep a quarter cycle apart,
//  so the image widens as well as thickens. (Free-running, the second copy sweeps 13 % faster
//  so the two never lock into a pattern; synced, both run at the grid's rate and stay a half
//  cycle apart.) The output already contains half of
//  the dry signal - a chorus of nothing but the shifted copies is a vibrato.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Chorus : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        if (phaseLocked())
        {
            const auto base = syncPhase(), amount = lockAmount (n);

            for (int c = 0; c < 2; ++c)
            {
                lfo[c][0].pull (base + 0.25f * (float) c, amount, n);
                lfo[c][1].pull (base + 0.5f + 0.25f * (float) c, amount, n);
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const auto rate = lfoRateHz (nextA());
            const auto excursion = nextB() * 0.006f * (float) sampleRate;
            const auto centre = 0.014f * (float) sampleRate;

            for (int c = 0; c < 2; ++c)
            {
                lfo[c][0].setRate (rate, sampleRate);
                lfo[c][1].setRate (rate * (syncOn ? 1.0f : 1.13f), sampleRate);   // synced, both sweeps must keep the grid

                const auto x = channels[c][i];
                line[c].push (x);

                const auto v0 = line[c].read (centre + excursion * lfo[c][0].next());
                const auto v1 = line[c].read (centre * 1.4f + excursion * lfo[c][1].next());
                channels[c][i] = 0.5f * x + 0.25f * (v0 + v1);
            }
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) (0.05 * sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            line[c].clear();
            lfo[c][0].setPhase (0.25f * (float) c);
            lfo[c][1].setPhase (0.5f + 0.25f * (float) c);
        }
    }

private:
    Dsp::DelayLine line[2];
    Dsp::Lfo lfo[2][2];
};
