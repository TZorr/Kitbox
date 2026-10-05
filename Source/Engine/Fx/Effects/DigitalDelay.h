//
//  DigitalDelay.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  A clean stereo delay: one line per channel, feedback straight back in.
//
//  Time is 10 ms to 1 s on an exponential knob (the ear hears time in ratios), or - with
//  Sync on - one of fourteen note values at the host tempo, up to four seconds (a whole
//  note at 60 BPM; slower tempi are held at four).
//  Feedback is the fraction returned, up to 95 %. What comes out is the echoes
//  only - the block's Mix knob puts the dry signal back.
//
//  Turning Time glides the read position (about 60 ms) instead of jumping it.
//  A jump lands on a different part of every repeat still in the line and is a
//  click on each; a glide bends their pitch briefly, the lesser fault.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class DigitalDelay : public Effect
{
public:
    void process (float* left, float* right, int n) noexcept override
    {
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

            const auto l = line[0].read (time - 1.0f);
            const auto r = line[1].read (time - 1.0f);

            line[0].push (left[i]  + feedback * l);
            line[1].push (right[i] + feedback * r);

            left[i] = l;
            right[i] = r;
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) ((maxSeconds + 0.05) * sampleRate));

        glide = 1.0f - std::exp (-1.0f / (0.06f * (float) sampleRate));
    }

    void onReset() override
    {
        for (auto& l : line)
            l.clear();

        fresh = true;
    }

private:
    Dsp::DelayLine line[2];
    static constexpr float maxSeconds = 4.0f;   // a whole note at 60 BPM
    float time = 1000.0f, glide = 0.001f;
    bool fresh = true;
};
