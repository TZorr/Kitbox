//
//  PingPongDelay.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  The echoes bounce between the channels: the mono sum of the input goes
//  into the left line, the left line feeds the right one, and the right one
//  feeds back into the left. So the first echo is on the left, the second on
//  the right, and each repeat takes two delay times to return to a side.
//
//  Time is the interval between bounces (10 ms to 1 s), Feedback the share that
//  keeps bouncing. Time glides for 60 ms, as in the Digital delay.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class PingPongDelay : public Effect
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

            const auto l = lineLeft.read (time - 1.0f);
            const auto r = lineRight.read (time - 1.0f);

            lineLeft.push (0.5f * (left[i] + right[i]) + feedback * r);
            lineRight.push (l);

            left[i] = l;
            right[i] = r;
        }
    }

protected:
    void onPrepare() override
    {
        lineLeft.allocate ((int) ((maxSeconds + 0.05) * sampleRate));
        lineRight.allocate ((int) ((maxSeconds + 0.05) * sampleRate));
        glide = 1.0f - std::exp (-1.0f / (0.06f * (float) sampleRate));
    }

    void onReset() override
    {
        lineLeft.clear();
        lineRight.clear();
        fresh = true;
    }

private:
    Dsp::DelayLine lineLeft, lineRight;
    static constexpr float maxSeconds = 4.0f;   // a whole note at 60 BPM
    float time = 1000.0f, glide = 0.001f;
    bool fresh = true;
};
