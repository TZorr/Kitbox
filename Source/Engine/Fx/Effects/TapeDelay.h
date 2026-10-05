//
//  TapeDelay.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  The Digital delay with a tape machine's faults added on purpose.
//
//  - Every repeat is darker: a low-pass (4.5 kHz) and a soft saturator sit in
//    the feedback path, so a long trail recedes instead of piling up bright
//    copies. The saturator is on the recirculated part only, so the first
//    echo is clean and the tenth is warm.
//  - The transport wobbles: a slow "wow" (0.6 Hz, +-0.35 ms) and a fast
//    "flutter" (6.3 Hz, +-0.05 ms) modulate the read position, and the two
//    channels use different phases so the echoes drift apart a little.
//  - The head is slow: Time glides for 150 ms, which on a tape delay is the
//    pitch-dive everybody knows.
//
//  Time and Feedback mean what they mean on the Digital one.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class TapeDelay : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            const auto target = delaySamples (nextA(), maxSeconds);
            if (fresh)
            {
                time = target;   // the first sample after a reset starts where the knob is, tempo included
                fresh = false;
            }

            time += glide * (target - time);
            const auto feedback = 0.95f * nextB();

            for (int c = 0; c < 2; ++c)
            {
                const auto wobble = 0.00035f * (float) sampleRate * wow[c].next()
                                  + 0.00005f * (float) sampleRate * flutter[c].next();

                const auto delayed = tone[c].process (line[c].read (juce::jmax (1.0f, time - 1.0f + wobble)));
                line[c].push (channels[c][i] + feedback * Dsp::softClip (1.4f * delayed) / 1.4f);
                channels[c][i] = delayed;
            }
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) ((maxSeconds + 0.05) * sampleRate));

        for (auto& t : tone)
            t.setCutoff (4500.0f, sampleRate);

        for (int c = 0; c < 2; ++c)
        {
            wow[c].setRate (0.6f, sampleRate);
            flutter[c].setRate (6.3f, sampleRate);
        }

        glide = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            line[c].clear();
            tone[c].reset();
            wow[c].setPhase (0.5f * (float) c);
            flutter[c].setPhase (0.33f * (float) c);
        }

        fresh = true;
    }

private:
    Dsp::DelayLine line[2];
    Dsp::OnePole tone[2];
    Dsp::Lfo wow[2], flutter[2];
    static constexpr float maxSeconds = 4.0f;   // a whole note at 60 BPM
    float time = 1000.0f, glide = 0.001f;
    bool fresh = true;
};
