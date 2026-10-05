//
//  Tremolo.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  Amplitude modulation by a sine: gain runs between 1 and 1 - Depth.
//
//  Both channels share one LFO on purpose. A tremolo whose sides are out of
//  phase is an auto-pan, which is a different effect and would make the
//  subtype list lie.
//
//  With Sync on and the host playing, the sweep sits on the beat grid: the gain is at
//  its top on the downbeat of the note value chosen, so a 1/8 tremolo chops in time.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Tremolo : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        if (phaseLocked())
            lfo.pull (syncPhase(), lockAmount (n), n);

        for (int i = 0; i < n; ++i)
        {
            lfo.setRate (lfoRateHz (nextA()), sampleRate);
            const auto gain = 1.0f - nextB() * 0.5f * (1.0f + lfo.next());
            left[i] *= gain;
            right[i] *= gain;
        }
    }

protected:
    void onReset() override     { lfo.setPhase (0.0f); }

private:
    Dsp::Lfo lfo;
};
