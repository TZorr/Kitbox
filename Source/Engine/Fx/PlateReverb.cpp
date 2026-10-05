//
//  PlateReverb.cpp
//  Kitbox (copied from Rackbox - fix bugs in both)
//

#include "PlateReverb.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double referenceRate = 29761.0;   // the rate Dattorro's lengths are given at
    constexpr double maxPreDelayMs = 250.0;
    constexpr float  inputDiffusion1 = 0.75f;
    constexpr float  inputDiffusion2 = 0.625f;
    constexpr float  decayDiffusion1 = 0.70f;
    constexpr float  outputScale     = 0.6f;
}

//==============================================================================
void PlateReverb::Line::allocate (int maxDelay)
{
    int size = 1;
    while (size < maxDelay + 4)
        size <<= 1;

    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;
    pos  = 0;
}

void PlateReverb::Line::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
}

float PlateReverb::Line::tapFractional (float d) const noexcept
{
    const auto whole = (int) d;
    const auto frac  = d - (float) whole;
    const auto a = tap (whole);
    const auto b = tap (whole + 1);
    return a + (b - a) * frac;
}

//==============================================================================
int PlateReverb::scaled (int samplesAt29761) const noexcept
{
    return std::max (1, (int) std::lround (samplesAt29761 * sampleRate / referenceRate));
}

void PlateReverb::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    preDelay.allocate ((int) (maxPreDelayMs * 0.001 * sampleRate) + 1);

    const int inputLengths[4] = { 142, 107, 379, 277 };

    for (int i = 0; i < 4; ++i)
    {
        input[i].delay = scaled (inputLengths[i]);
        input[i].line.allocate (input[i].delay);
    }

    excursion = (float) (16.0 * sampleRate / referenceRate);

    tankModLeft.delay  = (float) scaled (672);
    tankModRight.delay = (float) scaled (908);
    tankModLeft.line.allocate  ((int) (tankModLeft.delay + excursion) + 2);
    tankModRight.line.allocate ((int) (tankModRight.delay + excursion) + 2);

    const auto setDelay = [this] (Delay& d, int length) { d.delay = scaled (length); d.line.allocate (d.delay); };
    const auto setAllpass = [this] (Allpass& a, int length) { a.delay = scaled (length); a.line.allocate (a.delay); };

    setDelay (delayLeft1, 4453);
    setAllpass (tankLeft2, 1800);
    setDelay (delayLeft2, 3720);
    setDelay (delayRight1, 4217);
    setAllpass (tankRight2, 2656);
    setDelay (delayRight2, 3163);

    // Output taps, table 2 of the paper: left reads mostly the right half of
    // the tank and right reads the left, which is where the width comes from.
    const int left[7]  = { 266, 2974, 1913, 1996, 1990, 187, 1066 };
    const int right[7] = { 353, 3627, 1228, 2673, 2111, 335, 121 };

    for (int i = 0; i < 7; ++i)
    {
        tapsLeft[i]  = scaled (left[i]);
        tapsRight[i] = scaled (right[i]);
    }

    // The input band limit: Dattorro's bandwidth filter, set as a corner
    // frequency so it means the same at every session rate. 10 kHz keeps the
    // tail from hissing on hi-hats, which feed the reverb far more high end
    // than the vocals and snares the algorithm was voiced on.
    bandwidthCoefficient = (float) (1.0 - std::exp (-2.0 * 3.141592653589793 * 10000.0 / sampleRate));

    lfoIncrement = 2.0 * 3.141592653589793 * 1.0 / sampleRate;   // 1 Hz

    reset();
}

void PlateReverb::reset()
{
    preDelay.clear();

    for (auto& a : input)
        a.line.clear();

    tankModLeft.line.clear();  tankModRight.line.clear();
    delayLeft1.line.clear();   delayLeft2.line.clear();
    delayRight1.line.clear();  delayRight2.line.clear();
    tankLeft2.line.clear();    tankRight2.line.clear();

    dampLeft = dampRight = bandwidthState = 0.0f;
    lfoPhase = 0.0;
}

void PlateReverb::setParameters (float size, float dampingAmount, float preDelayMs) noexcept
{
    // The tank's gain per round trip. 0.98 at the top is a very long plate
    // but still strictly below one, so the tank cannot run away.
    decay   = 0.2f + 0.78f * std::clamp (size, 0.0f, 1.0f);
    damping = 0.9f * std::clamp (dampingAmount, 0.0f, 1.0f);
    decayDiffusion2 = std::clamp (decay + 0.15f, 0.25f, 0.5f);

    preDelaySamples = std::clamp ((int) std::lround (preDelayMs * 0.001 * sampleRate),
                                  0, (int) (maxPreDelayMs * 0.001 * sampleRate));
}

void PlateReverb::process (const float* in, float* left, float* right, int numSamples, float gain) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        preDelay.push (in[i]);
        auto x = preDelay.tap (preDelaySamples);

        bandwidthState += bandwidthCoefficient * (x - bandwidthState);
        x = bandwidthState;

        x = input[0].process (x, inputDiffusion1);
        x = input[1].process (x, inputDiffusion1);
        x = input[2].process (x, inputDiffusion2);
        x = input[3].process (x, inputDiffusion2);

        lfoPhase += lfoIncrement;
        if (lfoPhase > 6.283185307179586)
            lfoPhase -= 6.283185307179586;

        const auto modLeft  = excursion * (float) std::sin (lfoPhase);
        const auto modRight = excursion * (float) std::cos (lfoPhase);

        // The figure eight: each half is fed by the input plus the other half's
        // output from the previous trip round.
        const auto leftIn  = x + decay * delayRight2.output();
        const auto rightIn = x + decay * delayLeft2.output();

        // Left half. The tank's first allpass runs with the sign inverted, as
        // the paper specifies.
        {
            const auto a  = tankModLeft.process (leftIn, -decayDiffusion1, modLeft);
            const auto d1 = delayLeft1.output();
            delayLeft1.line.push (a);
            dampLeft += (1.0f - damping) * (d1 - dampLeft);
            const auto c = tankLeft2.process (dampLeft * decay, decayDiffusion2);
            delayLeft2.line.push (c);
        }

        // Right half
        {
            const auto a  = tankModRight.process (rightIn, -decayDiffusion1, modRight);
            const auto d1 = delayRight1.output();
            delayRight1.line.push (a);
            dampRight += (1.0f - damping) * (d1 - dampRight);
            const auto c = tankRight2.process (dampRight * decay, decayDiffusion2);
            delayRight2.line.push (c);
        }

        const auto yL = delayRight1.line.tap (tapsLeft[0])
                      + delayRight1.line.tap (tapsLeft[1])
                      - tankRight2.line.tap (tapsLeft[2])
                      + delayRight2.line.tap (tapsLeft[3])
                      - delayLeft1.line.tap (tapsLeft[4])
                      - tankLeft2.line.tap (tapsLeft[5])
                      - delayLeft2.line.tap (tapsLeft[6]);

        const auto yR = delayLeft1.line.tap (tapsRight[0])
                      + delayLeft1.line.tap (tapsRight[1])
                      - tankLeft2.line.tap (tapsRight[2])
                      + delayLeft2.line.tap (tapsRight[3])
                      - delayRight1.line.tap (tapsRight[4])
                      - tankRight2.line.tap (tapsRight[5])
                      - delayRight2.line.tap (tapsRight[6]);

        left[i]  += gain * outputScale * yL;
        right[i] += gain * outputScale * yR;
    }
}
