//
//  DrumSynth.h
//  Kitbox
//
//  Transmute's synth, ported from DrumSynth.swift and AttackNoise.swift:
//  DrumParams in, samples out, a pure function - the same parameters at the
//  same rate give the same samples every time, which is what the fitter
//  stands on (it renders a drum a thousand times and compares each render
//  with the original). Noise layers use fixed seeds.
//

#pragma once

#include <optional>
#include <vector>

#include "DrumParams.h"

namespace transmute
{
    namespace DrumSynth
    {
        /** The drum at `sampleRate`, renderLength() long - or, with `frames`,
            exactly that many frames of it: cut off (no fade) or padded. */
        Floats render (const DrumParams& p, double sampleRate, std::optional<int> frames = std::nullopt);

        int delayFrames (const DrumParams& p, double sampleRate, int frames);

        /** The swept sine (or a modal drum's modes) after `delay`, peak 1. */
        Doubles body (const DrumParams& p, double sampleRate, int frames);

        /** The wires' and the hat's band: high- and low-pass around noiseTone, then RMS 1. */
        Doubles bandPass (const Doubles& x, const DrumParams& p, double sampleRate);

        Doubles metalSource (double tone, double sampleRate, int frames);
        Doubles hatEnvelope (const DrumParams& p, double sampleRate, int frames);
    }

    namespace AttackNoise
    {
        /** What the original has in its first 50 ms that `synth` lacks. */
        AttackTable measure (const Doubles& original, const Doubles& synth,
                             double gain, double floorRatio, double sampleRate);

        Doubles render (const AttackTable& table, double sampleRate, int frames);
    }
}
