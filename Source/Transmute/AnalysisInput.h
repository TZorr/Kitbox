//
//  AnalysisInput.h
//  Kitbox
//
//  A pad's sample as Transmute's analysis takes it: one channel at 48 kHz.
//  Made the way Transmute's Decoder makes it, so a sample transmuted here fits
//  to what it fits to in Transmute: every channel through Core Audio's
//  mastering-quality resampler (AVAudioConverter there, AudioConverter here,
//  the same converter underneath), then the channels averaged - not summed
//  first, which rounds differently, and not the converter's own downmix.
//

#pragma once

#include <vector>

#include "SwiftVector.h"

namespace transmute
{
    /** `channels` (each `frames` long) at `sourceRate`, folded to mono at
        analysisRate. Empty if Core Audio refuses the conversion. */
    Floats toAnalysisRate (const std::vector<const float*>& channels, int frames, double sourceRate);
}
