//
//  PlateVerb.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  The Plate: Kitbox's Dattorro plate (PlateReverb) behind the Effect
//  interface. Size is the tank's decay, Damp the high-frequency loss in it.
//
//  PlateReverb takes one mono input and adds a stereo return into the buffers
//  it is given, so the wrapper sums the input to mono first, clears the
//  buffers, and lets the reverb fill them.
//
//  The Dattorro tank gets much louder as its decay lengthens - measured on noise,
//  7.5 dB from Size 0 to Size 1 - so the return is scaled by a curve fitted to
//  those measurements, which leaves the level within about 2 dB of -4 dB re input
//  over the whole knob. Without it, turning Size up is also turning the reverb
//  up, and the two want to be separate knobs. A short fixed pre-delay (12 ms) keeps
//  the onset from smearing over the attack of what it is reverberating.
//

#pragma once

#include "../Effect.h"
#include "../PlateReverb.h"

class PlateVerb : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        const auto preTarget = preDelaySamples();

        for (int i = 0; i < n; ++i)
        {
            mono[(size_t) i] = preDelay.process (0.5f * (left[i] + right[i]), preTarget);
            left[i] = right[i] = 0.0f;
        }

        const auto size = blockA (n);
        reverb.setParameters (size, blockB (n), 0.0f);
        reverb.process (mono.data(), left, right, n, makeUp (size));
    }

protected:
    void onPrepare() override
    {
        mono.assign ((size_t) maxBlock, 0.0f);
        reverb.prepare (sampleRate);
        preDelay.prepare (sampleRate, Dsp::Sync::maxPreDelaySeconds);
    }

    void onReset() override
    {
        reverb.reset();
        reverb.setParameters (targetA(), targetB(), 0.0f);
        preDelay.reset();
    }

private:
    /** Gain that flattens the measured level (-2.7, -1.3, +4.8 dB at Size 0, 0.5, 1) to about -4 dB. */
    static float makeUp (float size) noexcept
    {
        const auto measured = -2.7f - 1.9f * size + 9.4f * size * size;
        return std::pow (10.0f, (-4.0f - measured) / 20.0f);
    }

    PlateReverb reverb;
    Dsp::PreDelay preDelay;
    std::vector<float> mono;
};
