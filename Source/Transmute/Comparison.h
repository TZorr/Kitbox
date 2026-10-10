//
//  Comparison.h
//  Kitbox
//
//  How far a render is from the original, as one number in dB - what the
//  fitter minimises. Ported from Comparison.swift: sixth-octave band levels at
//  four frame sizes, the analytic envelope over 2 ms, and the pitch sweep
//  against the measured periods, each cell weighted by its amplitude.
//

#pragma once

#include <memory>
#include <vector>

#include "DrumParams.h"
#include "Dsp.h"

namespace transmute
{
    /** One period measured from the zero crossings. */
    struct PitchPoint
    {
        double time;     // s from the onset
        double hz;
        double weight;   // the body's envelope there
    };

    struct MatchScore
    {
        double spectralDB = 0, envelopeDB = 0, pitchSemitones = 0;
        double total() const { return spectralDB + envelopeDB + pitchSemitones; }
    };

    class Comparison
    {
    public:
        Comparison (const Floats& target, double sampleRate, std::vector<PitchPoint> pitchTrack);

        MatchScore score (const Floats& candidate) const;
        MatchScore score (const DrumParams& params) const;
        double pitchError (const DrumParams& p) const;

        const double sampleRate;
        const int frameCount;
        const std::vector<PitchPoint> pitchTrack;

    private:
        struct Band { int lower, upper; };

        struct Resolution
        {
            int size, hop;
            std::unique_ptr<RealFFT> fft;
            Floats window;
            std::vector<Band> bands;
            int frameLimit;   // frames from the onset on; INT_MAX for all
            Floats target, floors;
        };

        Floats spectrogram (const Floats& x, const Resolution& r) const;
        Floats envelope (const Floats& x) const;

        static double distance (const Floats& a, const Floats& b,
                                const Floats& floors, int bands);
        static Floats floors (const Floats& levels, int bands);
        static std::vector<Band> bands (int size, double sampleRate);

        std::vector<Resolution> resolutions;
        int envelopeHop;
        std::unique_ptr<RealFFT> envelopeFFT;
        Floats targetEnvelope;
        float envelopeFloor = -200;
    };
}
