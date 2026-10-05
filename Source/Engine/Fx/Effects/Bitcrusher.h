//
//  Bitcrusher.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  Bit depth and sample rate, both lowered on purpose.
//
//  Bits runs from 16 down to 2: the signal is rounded to that many levels,
//  which is quantisation noise that follows the signal. Rate holds each sample
//  for 1 to 32 samples, so everything above the reduced Nyquist folds back as
//  aliasing. Neither is filtered - the alias products are the effect.
//
//  Bits is continuous, not stepped, so the knob sweeps smoothly through the
//  in-between depths; the level count is 2^(bits-1) per polarity.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Bitcrusher : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto levels = std::exp2 (Dsp::Map::bits (nextA()) - 1.0f);
            const auto period = Dsp::Map::downsample (nextB());

            counter += 1.0f;
            if (counter >= period)
            {
                counter -= period;
                heldLeft  = std::round (left[i]  * levels) / levels;
                heldRight = std::round (right[i] * levels) / levels;
            }

            left[i] = heldLeft;
            right[i] = heldRight;
        }
    }

protected:
    void onReset() override
    {
        counter = 0.0f;
        heldLeft = heldRight = 0.0f;
    }

private:
    float counter = 0.0f, heldLeft = 0.0f, heldRight = 0.0f;
};
