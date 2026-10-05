//
//  Phaser.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  Six first-order allpass stages swept by a sine LFO, with 50 % feedback.
//
//  Ported from Kitbox's send phaser, which was mono; here each channel has its
//  own chain and the two LFOs run a quarter cycle apart. The notches come from
//  adding the allpassed signal to the dry one, and the output does that itself
//  (half and half), so the effect does not depend on what the block's Mix knob
//  is set to. Six stages give three notches; the sweep is exponential around
//  800 Hz and Depth sets how many octaves it covers (up to five). Coefficients
//  are recomputed every eight samples - an LFO of a few hertz moves nowhere
//  near far enough in that time to be heard stepping.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Phaser : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };
        constexpr float feedback = 0.5f;

        if (phaseLocked())
        {
            const auto base = syncPhase(), amount = lockAmount (n);

            for (int c = 0; c < 2; ++c)
                side[c].lfo.pull (base + 0.25f * (float) c, amount, n);
        }

        for (int i = 0; i < n; ++i)
        {
            const auto rate = lfoRateHz (nextA());
            const auto depth = nextB();

            for (int c = 0; c < 2; ++c)
            {
                auto& s = side[c];
                s.lfo.setRate (rate, sampleRate);
                const auto sweep = s.lfo.next();

                if (--s.countdown < 0)
                {
                    s.countdown = 7;
                    const auto corner = 800.0f * std::exp2 (sweep * depth * 2.5f);
                    const auto t = std::tan (Dsp::pi * std::fmin (corner, (float) (0.45 * sampleRate)) / (float) sampleRate);
                    s.coefficient = (t - 1.0f) / (t + 1.0f);
                }

                const auto in = channels[c][i];
                auto x = in + Dsp::softClip (feedback * s.last);

                for (auto& z : s.state)
                {
                    const auto y = s.coefficient * x + z;   // transposed direct form II allpass
                    z = x - s.coefficient * y;
                    x = y;
                }

                s.last = x;
                channels[c][i] = 0.5f * (in + x);
            }
        }
    }

protected:
    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            side[c] = {};
            side[c].lfo.setPhase (0.25f * (float) c);
        }
    }

private:
    struct Side
    {
        Dsp::Lfo lfo;
        float state[6] {};
        float coefficient = 0.0f, last = 0.0f;
        int countdown = 0;
    };

    Side side[2];
};
