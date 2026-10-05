//
//  PlateReverb.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  The reverb: Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997), mono
//  in, stereo out.
//
//  Chosen for the brief - good and cheap - and because its shape is exactly the
//  one the send bus needs. The algorithm takes one input and builds its stereo
//  image from taps spread around a figure-eight tank, so a mono send produces a
//  wide, decorrelated return without a second channel of processing. Per
//  sample it is four input diffusers, two tank halves of two allpasses and two
//  delays each, and fourteen output taps: well under one percent of a core.
//
//  Plates suit drums. The dense, immediately diffuse onset has none of the
//  discrete early echoes a room model spends its budget on, and those echoes
//  are what makes a snare in a cheap algorithmic room sound like a flam.
//
//  The published delay lengths are for 29761 Hz and are scaled to the session
//  rate. The two tank input allpasses are modulated, as in the paper, to keep
//  the tail from ringing at the tank's own resonances.
//

#pragma once

#include <vector>

class PlateReverb
{
public:
    void prepare (double sampleRate);
    void reset();

    /** size and damping 0..1, preDelay in ms. */
    void setParameters (float size, float damping, float preDelayMs) noexcept;

    /** Adds the wet signal, times gain, into left and right. */
    void process (const float* input, float* left, float* right, int numSamples, float gain) noexcept;

private:
    class Line
    {
    public:
        void allocate (int maxDelay);
        void clear();
        void push (float x) noexcept           { pos = (pos + 1) & mask; buffer[(size_t) pos] = x; }
        /** tap(0) is the last value pushed. */
        float tap (int d) const noexcept        { return buffer[(size_t) ((pos - d) & mask)]; }
        float tapFractional (float d) const noexcept;

    private:
        std::vector<float> buffer;
        int mask = 0, pos = 0;
    };

    struct Allpass
    {
        Line line;
        int delay = 1;

        float process (float x, float g) noexcept
        {
            const auto delayed = line.tap (delay - 1);
            const auto v = x - g * delayed;
            line.push (v);
            return g * v + delayed;
        }
    };

    struct ModulatedAllpass
    {
        Line line;
        float delay = 1.0f;

        float process (float x, float g, float modulation) noexcept
        {
            const auto delayed = line.tapFractional (delay - 1.0f + modulation);
            const auto v = x - g * delayed;
            line.push (v);
            return g * v + delayed;
        }
    };

    struct Delay
    {
        Line line;
        int delay = 1;

        float output() const noexcept { return line.tap (delay - 1); }
    };

    int scaled (int samplesAt29761) const noexcept;

    double sampleRate = 48000.0;

    Line preDelay;
    int preDelaySamples = 0;
    float bandwidthState = 0.0f, bandwidthCoefficient = 0.5f;

    Allpass input[4];
    ModulatedAllpass tankModLeft, tankModRight;
    Delay delayLeft1, delayLeft2, delayRight1, delayRight2;
    Allpass tankLeft2, tankRight2;
    float dampLeft = 0.0f, dampRight = 0.0f;

    float decay = 0.5f, damping = 0.1f, decayDiffusion2 = 0.5f;

    double lfoPhase = 0.0, lfoIncrement = 0.0;
    float excursion = 16.0f;

    int tapsLeft[7] {}, tapsRight[7] {};
};
