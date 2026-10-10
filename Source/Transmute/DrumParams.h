//
//  DrumParams.h
//  Kitbox
//
//  Transmute's drum voice, ported from Swift (Transmute/Engine/DrumParams.swift):
//  everything the synth needs to play one hit, and what a .drumparams file
//  holds. The reasoning behind every field is in the Swift original; this port
//  keeps its names, defaults, ranges and arithmetic so that a .drumparams file
//  moves between the two unchanged and a fit here lands where Transmute's does.
//
//  Five models: Kick / Tom (a swept sine, click, shell noise), Snare (adds a
//  second head mode and wires), Hi-Hat (noise and six square waves through one
//  band), Modal (up to six damped sines) and Clap (bursts and a tail).
//

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "SwiftVector.h"

namespace transmute
{
    enum class DrumModel { kick, snare, hat, modal, clap };
    enum class FilterType { off, lowPass, bandPass, highPass };

    constexpr DrumModel allModels[] = { DrumModel::kick, DrumModel::snare, DrumModel::hat,
                                        DrumModel::modal, DrumModel::clap };

    /** "kick", "snare", ... - the raw values a .drumparams file uses. */
    const char* modelId (DrumModel);
    /** "Kick / Tom", "Snare", ... - as Transmute shows them. */
    const char* modelTitle (DrumModel);
    std::optional<DrumModel> modelFromId (std::string_view);

    /** The measured attack noise (see AttackNoise): 1/3-octave band levels in
        1 ms frames, relative to the body's peak. */
    struct AttackTable
    {
        Doubles bands;
        double hop = 0.001;
        std::vector<std::vector<float>> levels;   // levels[band][frame]

        double duration() const { return hop * (double) (levels.empty() ? 0 : levels.front().size()); }
        /** Exact, field by field (defined where -Wfloat-equal is off: see DrumParams.cpp). */
        bool operator== (const AttackTable&) const;
    };

    struct DrumParams
    {
        DrumModel model = DrumModel::kick;
        double delay = 0;               // s of silence before the voice starts
        double fundamental = 55;        // Hz, where the pitch settles
        double pitchStart = 160;        // Hz at the start of the hit
        double pitchDecay = 0.040;      // s, time constant of the sweep
        double ampAttack = 0.0005;      // s, rise of the body
        double ampDecay = 0.250;        // s, time constant of the body
        double ampShape = 1;            // stretch of the decay, 1 = exponential
        double startPhase = 0;          // degrees
        double drive = 0;               // tanh drive on the body
        double transient = 0.2;         // click peak relative to the body's
        double clickTone = 3000;        // Hz
        double clickDecay = 0.0015;     // s
        double noise = 0.05;            // noise RMS relative to the body's peak
        double noiseTone = 2500;        // Hz
        double noiseDecay = 0.060;      // s
        double gainDB = -1;             // dBFS of the body's peak
        double length = 1.0;            // s, when not automatic
        bool autoLength = true;
        std::optional<AttackTable> attack;
        double attackLevelDB = 0;
        FilterType filterType = FilterType::off;
        double filterCutoff = 2000;
        double filterQ = 0.707;
        bool envelopeOn = false;
        double envHold = 0.100;
        double envRelease = 0.300;
        double envCurve = 3;
        // Snare only.
        double mode2Level = 0.3;
        double mode2Ratio = 1.7;
        double mode2Decay = 0.020;
        double noiseWidth = 3;
        double noiseShape = 1;
        // Hi-hat only.
        double metal = 0.5;
        double metalTone = 205.3;
        // Modal only.
        double modalTone = 800;
        double modalRatio2 = 1.5, modalLevel2 = 0, modalDecay2 = 0.05;
        double modalRatio3 = 2,   modalLevel3 = 0, modalDecay3 = 0.05;
        double modalRatio4 = 2.5, modalLevel4 = 0, modalDecay4 = 0.05;
        double modalRatio5 = 3,   modalLevel5 = 0, modalDecay5 = 0.05;
        double modalRatio6 = 4,   modalLevel6 = 0, modalDecay6 = 0.05;
        // Clap only.
        double clapBursts = 4;
        double clapSpacing = 0.010;
        double clapBurstDecay = 0.003;
        double clapTailTone = 1200;
        double clapTailWidth = 3;

        bool isSnare() const { return model == DrumModel::snare; }
        bool isHat() const   { return model == DrumModel::hat; }
        bool isModal() const { return model == DrumModel::modal; }
        bool isClap() const  { return model == DrumModel::clap; }

        int burstCount() const;
        double tailStart() const;

        struct ModalMembers { double DrumParams::* ratio; double DrumParams::* level; double DrumParams::* decay; };
        static ModalMembers modalMembers (int k);   // k = 2...6

        struct Mode { double hz, level, decay; };
        std::vector<Mode> modes() const;

        DrumParams clamped() const;

        double fadeSeconds() const;
        double tailSeconds() const;
        double envelopeEnd() const { return envHold + envRelease; }
        double renderLength() const;
        double pitch (double t) const;

        /** Exact, field by field: the tests hold the port to Transmute bit for bit. */
        bool operator== (const DrumParams&) const;

        /** The .drumparams file: JSON with Transmute's keys, sorted, so the file
            opens in Transmute. Doubles are written shortest-round-trip. */
        std::string toJson() const;

        /** Forgiving like Transmute's decoder: a missing key keeps its
            default, and the result is clamped. Empty on a file that is not
            JSON at all. */
        static std::optional<DrumParams> fromJson (const std::string& text);

    private:
        double hatTailSeconds() const;
        double modalTailSeconds() const;
        double clapTailSeconds() const;
    };

    /** One parameter's range, as Transmute's sliders and fitter use it. */
    struct ParamSpec
    {
        const char* id;
        double DrumParams::* member;
        double lower, upper;
        bool logarithmic;
    };

    const std::vector<ParamSpec>& allSpecs();
    const ParamSpec& spec (std::string_view id);
}
