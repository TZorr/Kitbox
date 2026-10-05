//
//  FreqShifter.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  A frequency shifter (single-sideband, after Harald Bode's). Shift moves
//  every frequency in the signal by the same number of Hz, -1000 to +1000,
//  0 at the knob's centre. Feedback sends the shifted signal round again.
//
//  Shifting is not pitch-shifting. A pitch shift multiplies frequencies and
//  keeps a note's harmonics in tune with each other; a shift adds the same Hz
//  to all of them, so 100, 200 and 300 Hz shifted up by 50 become 150, 250
//  and 350 - no longer a harmonic series. A few Hz is a slow beating, like a
//  phaser that never turns round; tens of Hz is metallic; hundreds is a voice
//  from a different planet.
//
//  How. Dsp::QuadraturePair splits the signal into two parts 90 degrees apart,
//  which together describe each frequency as a turning pointer. Turning every
//  pointer further by a steady rate - multiplying by a cosine and a sine of the
//  shift and taking the difference - moves every frequency by that rate.
//  Multiplying by a cosine alone (a ring modulator) would make two copies, one
//  moved up and one down; the 90 degree part is what cancels the second one.
//  Below zero, frequencies go through 0 Hz and come back mirrored.
//
//  Feedback. The output goes back into the input after 8 ms, so every pass is
//  shifted once more. That is the barber-pole: the 8 ms loop is a comb filter
//  whose teeth move by the shift on every pass, so with a few Hz of shift the
//  comb climbs (or falls) for ever without ever arriving. At larger shifts it
//  smears notes into clangorous clusters. The loop has a DC blocker (a shifted
//  signal near 0 Hz is no longer split cleanly) and a soft clip, so at the top
//  of the Feedback knob it rings loud but never runs away; the output is
//  pulled down as Feedback rises, which keeps it roughly as loud as it went in.
//
//  The quadrature pair is an allpass, so even at a shift of 0 Hz the output
//  is the input with its phases turned, not the input itself. On its own that
//  is inaudible; blended with the dry signal on the block's Mix it colours
//  like a phaser standing still.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class FreqShifter : public Effect
{
public:
    static constexpr float loopSeconds = 0.008f;
    static constexpr float maxFeedback = 0.9f;

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            const auto shift = Dsp::Map::shiftHz (nextA());
            const auto feedback = maxFeedback * nextB();
            const auto makeUp = 1.0f - 0.5f * feedback;

            const auto turn = (float) (2.0 * juce::MathConstants<double>::pi * phase);
            const auto cosine = std::cos (turn), sine = std::sin (turn);
            phase += (double) shift / sampleRate;
            phase -= std::floor (phase);

            for (int c = 0; c < 2; ++c)
            {
                const auto back = Dsp::softClip (dc[c].process (loop[c].read (loopDelay)));

                float inPhase, quadrature;
                split[c].process (channels[c][i] + feedback * back, inPhase, quadrature);

                const auto shifted = inPhase * cosine - quadrature * sine;
                loop[c].push (shifted);
                channels[c][i] = shifted * makeUp;
            }
        }
    }

protected:
    void onPrepare() override
    {
        // Read before this sample is pushed, so "0 samples ago" is the previous one.
        loopDelay = juce::jmax (0.0f, std::round (loopSeconds * (float) sampleRate) - 1.0f);

        for (auto& l : loop)
            l.allocate ((int) loopDelay + 2);
    }

    void onReset() override
    {
        phase = 0.0;

        for (int c = 0; c < 2; ++c)
        {
            split[c].reset();
            loop[c].clear();
            dc[c].reset();
        }
    }

private:
    Dsp::QuadraturePair split[2];
    Dsp::DelayLine loop[2];
    Dsp::DcBlocker dc[2];
    double phase = 0.0;
    float loopDelay = 0.0f;
};
