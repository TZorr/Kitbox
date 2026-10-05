//
//  TptFilter.h
//  Kitbox
//
//  The pad filter: a topology-preserving-transform state-variable filter
//  (Zavalishin, "The Art of VA Filter Design", ch. 4), with two analogue
//  habits added where they cost almost nothing.
//
//  Why this structure. A drum filter is swept - by its envelope on every hit,
//  and by hand over a ringing tail - and a biquad computed from the cookbook
//  formulas misbehaves under fast coefficient changes: its states belong to
//  one set of coefficients and mean something else under the next, so a fast
//  sweep clicks and a resonant one can blow up. The TPT SVF's states are the
//  two integrators of the analogue circuit it models, and they stay meaningful
//  whatever the cutoff does. It also gives low, band and high pass from the
//  same two states, so one object serves every type on the panel.
//
//  The two analogue habits:
//
//  - Drive. The input goes through a soft saturator before the filter, the
//    way an overdriven input stage in front of a transistor or OTA filter
//    colours a drum. At zero drive it is bypassed exactly, so a clean pad is
//    bit-for-bit linear.
//  - Resonance that saturates. The band-pass integrator is soft-limited, so
//    turning resonance up makes the peak compress and bloom instead of growing
//    without bound - which is what the integrators in a real circuit do when
//    they run out of headroom. At low resonance the state never gets near the
//    knee and the filter is linear to within a fraction of a dB.
//
//  24 dB modes are two stages in cascade, tuned as a fourth-order Butterworth
//  (Q 0.54 and 1.31) with the resonance on the second stage only.
//

#pragma once

#include <cmath>

#include "ParameterIds.h"

namespace KitDsp
{
    /** A Pade tanh: exact enough in the audible range, a third of the cost,
        and it saturates at +-1 because the input is clamped to +-3. */
    inline float softClip (float x) noexcept
    {
        x = x < -3.0f ? -3.0f : (x > 3.0f ? 3.0f : x);
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
}

class SvfStage
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** g = tan(pi * fc / fs), k = 1 / Q. */
    void setCoefficients (float g, float kIn) noexcept
    {
        k  = kIn;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Outputs { float low, band, high; };

    Outputs process (float v0) noexcept
    {
        const auto v3 = v0 - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;

        // The saturating integrator. The knee sits at a state of about 4, far
        // above anything a filter at moderate resonance produces from a
        // full-scale drum, and reached at high resonance - which is the point.
        ic1 = 4.0f * KitDsp::softClip (0.25f * (2.0f * v1 - ic1));
        ic2 = 2.0f * v2 - ic2;

        return { v2, v1, v0 - k * v1 - v2 };
    }

    float getK() const noexcept { return k; }

private:
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 1.4142f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
};

class TptFilter
{
public:
    using Type = KitParams::FilterType;

    void reset() noexcept
    {
        first.reset();
        second.reset();
    }

    /** cutoff in Hz, resonance and drive 0..1. Call as often as the cutoff
        moves - the voice does it every 16 samples. */
    void set (Type newType, float cutoffHz, float resonance, float drive, double sampleRate) noexcept
    {
        type = newType;

        const auto nyquistSafe = (float) (0.45 * sampleRate);
        const auto fc = cutoffHz < 20.0f ? 20.0f : (cutoffHz > nyquistSafe ? nyquistSafe : cutoffHz);
        const auto g  = std::tan (3.14159265f * fc / (float) sampleRate);

        // Resonance maps onto 1/Q exponentially: equal knob travel, equal
        // musical steps in peak height, from no peak up to Q of about 20.
        const auto resonantK = [resonance] (float baseK) { return baseK * std::pow (0.035f, resonance); };

        if (type == Type::lp24 || type == Type::hp24)
        {
            first.setCoefficients (g, 1.8478f);
            second.setCoefficients (g, resonantK (0.7654f));
        }
        else
        {
            first.setCoefficients (g, resonantK (1.4142f));
        }

        driveAmount = drive;
        driveGain   = std::pow (10.0f, drive * 24.0f / 20.0f);   // up to +24 dB into the saturator
        makeUp      = 1.0f / std::sqrt (driveGain);
    }

    float process (float x) noexcept
    {
        if (driveAmount > 0.0f)
        {
            // Blended in over the first tenth of the knob so there is no jump
            // between "off" and "a little".
            const auto blend = driveAmount >= 0.1f ? 1.0f : driveAmount * 10.0f;
            x += blend * (KitDsp::softClip (x * driveGain) * makeUp - x);
        }

        switch (type)
        {
            case Type::lp12: return first.process (x).low;
            case Type::hp12: return first.process (x).high;
            case Type::bp:   return first.process (x).band * first.getK();   // unity at the centre
            case Type::lp24: return second.process (first.process (x).low).low;
            case Type::hp24: return second.process (first.process (x).high).high;
            case Type::off:  break;
        }

        return x;
    }

private:
    SvfStage first, second;
    Type type = Type::off;
    float driveAmount = 0.0f, driveGain = 1.0f, makeUp = 1.0f;
};
