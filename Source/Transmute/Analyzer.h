//
//  Analyzer.h
//  Kitbox
//
//  A recorded drum hit in, a first guess at its DrumParams out - measured off
//  the waveform one parameter at a time, for the Fitter to improve. Ported
//  from Analyzer.swift; the reasoning behind every threshold is there.
//

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Comparison.h"
#include "DrumParams.h"

namespace transmute
{
    /** The rate every analysis runs at (MonoAudio.analysisRate). */
    constexpr double analysisRate = 48000.0;

    struct Analysis
    {
        /** The hit, onset to end, at the analysis rate. */
        Floats hit;
        double sampleRate = analysisRate;
        double onsetSeconds = 0;
        std::vector<PitchPoint> pitchTrack;
        /** The recording's noise floor below the hit's peak, in dB (<= 0). */
        double noiseFloorDB = 0;
        /** The measured first guess, of the model it was analysed as. */
        DrumParams initial;
        /** The model the hit itself suggests. */
        DrumModel suggested = DrumModel::kick;
    };

    namespace Analyzer
    {
        /** Analyses `audio` as `model`, or as the model it suggests. Empty, with
            `error` set, for a silent file or a hit under 20 ms. */
        std::optional<Analysis> analyze (const Floats& audio, double sampleRate,
                                         std::optional<DrumModel> model, std::string& error);

        DrumModel suggestModel (const Doubles& y, double sampleRate);
        double bodyCutoff (DrumModel model, double hz);
        double startPhase (const DrumParams& p, const Doubles& body, double sampleRate);
    }
}
