//
//  ParameterIds.h
//  Kitbox
//
//  Every parameter the host can see, named in one place.
//
//  A pad's parameters are the same twenty-two suffixes sixteen times over, with
//  the pad number in front: "pad06_cutoff". The ids are the identity a saved
//  session is restored by, so once a session exists none of these strings may
//  change - rename the display name instead.
//
//  Two groups of parameters behave differently when a pad is hit, and the split
//  is the one a hardware drum machine makes. Tune, Start and the envelope times
//  are read once, at the hit: turning them changes the next hit, not the one
//  still ringing. Level, pan, the filter and the sends are read every block, so
//  sweeping the cutoff over a long tail does what it looks like it does.
//

#pragma once

#include <juce_core/juce_core.h>

namespace KitParams
{
    constexpr int stateVersion = 1;

    constexpr int numPads   = 16;
    constexpr int firstNote = 36;   // the default: C1 on pad 1, the MPC / GM drum layout

    /** Choke groups 1..8; 0 is "no group". */
    constexpr int numChokeGroups = 8;

    /** Aux stereo outputs, "Out 1".."Out 16", besides the main one. An output
        index of 0 on a pad means Main. */
    constexpr int numAuxOutputs = 16;
    constexpr int numOutputBuses = numAuxOutputs + 1;

    //==============================================================================
    //  Per pad
    namespace Pad
    {
        inline constexpr const char* level      = "level";
        inline constexpr const char* pan        = "pan";
        inline constexpr const char* tune       = "tune";
        inline constexpr const char* fine       = "fine";
        inline constexpr const char* start      = "start";
        inline constexpr const char* velocity   = "velocity";
        inline constexpr const char* attack     = "attack";
        inline constexpr const char* hold       = "hold";
        inline constexpr const char* decay      = "decay";
        inline constexpr const char* filterType = "ftype";
        inline constexpr const char* cutoff     = "cutoff";
        inline constexpr const char* resonance  = "reso";
        inline constexpr const char* drive      = "drive";
        inline constexpr const char* envAttack  = "fattack";
        inline constexpr const char* envDecay   = "fdecay";
        inline constexpr const char* envAmount  = "famount";
        inline constexpr const char* sendReverb = "rev";
        inline constexpr const char* sendDelay  = "dly";
        inline constexpr const char* sendPhaser = "phs";
        inline constexpr const char* note       = "note";
        inline constexpr const char* choke      = "choke";
        inline constexpr const char* output     = "output";

        /** Every suffix, in the order PadParams::slot() lists them. */
        inline constexpr const char* all[] = {
            level, pan, tune, fine, start, velocity, attack, hold, decay,
            filterType, cutoff, resonance, drive, envAttack, envDecay, envAmount,
            sendReverb, sendDelay, sendPhaser, note, choke, output
        };
    }

    /** "pad06_cutoff" for pad index 5. */
    inline juce::String padId (int pad, const char* suffix)
    {
        return "pad" + juce::String (pad + 1).paddedLeft ('0', 2) + "_" + suffix;
    }

    //==============================================================================
    //  Global
    inline constexpr const char* humanize    = "humanize";
    inline constexpr const char* masterLevel = "master";

    //  The three send effects. Each slot holds one category of Rackbox's
    //  catalog, and Type picks the effect within it; A and B are that effect's
    //  two knobs, 0..1, named and printed by EffectCatalog. The Level ids are
    //  the ones Kitbox always had, so a return level survives old kits as is -
    //  "phs_level" included, although the slot is Mod now.
    namespace Fx
    {
        constexpr int numSlots = 3;
        enum Slot { reverb, delay, mod };

        struct Ids { const char* type; const char* a; const char* b; const char* level; };

        inline constexpr Ids ids[numSlots] {
            { "rev_type", "rev_a", "rev_b", "rev_level" },
            { "dly_type", "dly_a", "dly_b", "dly_level" },
            { "mod_type", "mod_a", "mod_b", "phs_level" }
        };

        inline constexpr const char* titles[numSlots] { "Reverb", "Delay", "Mod" };

        /** Where each slot sits in EffectCatalog (Modulation 0, Reverb 1, Delay 2).
            Type indices follow the catalog's order: Plate/Room/Hall,
            Digital/Tape/Ping-Pong, Chorus/Phaser/Flanger/Tremolo/Shifter. */
        inline constexpr int categories[numSlots] { 1, 2, 0 };

        /** Types when nothing says otherwise: the plate, the tape delay (the
            closest to Kitbox's old darkening delay) and the phaser. A and B
            are today's defaults of the old knobs, translated. */
        inline constexpr int   defaultTypes[numSlots] { 0, 1, 1 };                     // Plate, Tape, Phaser
        inline constexpr float defaultA[numSlots]     { 0.5f, 5.0f / 13.0f, 0.392f };   // 50 %, 1/8, 0.4 Hz
        inline constexpr float defaultB[numSlots]     { 0.4f, 0.37f, 0.7f };            // 40 %, 35 % feedback, 70 %
    }

    //  The effect parameters before the slots, read only to translate old kits
    //  and sessions (FxMigration.h). Never registered as parameters.
    namespace Legacy
    {
        inline constexpr const char* reverbSize     = "rev_size";
        inline constexpr const char* reverbDamping  = "rev_damp";
        inline constexpr const char* reverbPreDelay = "rev_predelay";
        inline constexpr const char* delayTime      = "dly_time";      // index into the 13 divisions below
        inline constexpr const char* delayFeedback  = "dly_feedback";  // 0..95 %
        inline constexpr const char* delayTone      = "dly_tone";
        inline constexpr const char* phaserRate     = "phs_rate";      // Hz
        inline constexpr const char* phaserDepth    = "phs_depth";
        inline constexpr const char* phaserFeedback = "phs_feedback";

        /** The old division list was Dsp::Sync's without "1/2 D": the same up
            to 1/2, then 1/1. */
        constexpr int numDelayDivisions = 13;
    }

    //==============================================================================
    //  Ranges and choices the engine needs as well as the layout

    /** The level knobs bottom out here, and the bottom means silence. */
    constexpr float levelFloorDb = -60.0f;

    /** Decay at the top of its range means "no decay": the sample plays out. */
    constexpr float decayMaxMs = 10000.0f;

    enum class FilterType { off, lp12, lp24, bp, hp12, hp24 };

    inline juce::StringArray chokeNames()
    {
        juce::StringArray names { "Off" };
        for (int g = 1; g <= numChokeGroups; ++g)
            names.add (juce::String (g));
        return names;
    }

    inline juce::StringArray outputNames()
    {
        juce::StringArray names { "Main" };
        for (int o = 1; o <= numAuxOutputs; ++o)
            names.add ("Out " + juce::String (o));
        return names;
    }

    /** "C1" for 36 - Logic's convention, middle C = C3. */
    inline juce::String noteName (int note)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
    }

    inline const juce::StringArray filterTypeNames { "Off", "LP 12", "LP 24", "BP", "HP 12", "HP 24" };


    //==============================================================================
    //  Non-parameter state, kept as properties on the parameter tree
    inline const juce::Identifier stateTreeType     { "KITBOX" };
    inline const juce::Identifier propSelectedPad   { "selectedPad" };
    inline const juce::Identifier propKitName       { "kitName" };
    inline const juce::Identifier propStateVersion  { "stateVersion" };

    /** Which Humanize scale a saved tree uses. Missing: before 0.6.0, when 100 % was what 50 %
        is now; such a tree's Humanize is halved on load. 2: the doubled range of 0.6.0. A
        property of its own rather than a new stateVersion, which is also every parameter's
        version hint and must not move. */
    inline const juce::Identifier propHumanizeRange { "humanizeRange" };
    constexpr int humanizeRange = 2;
}
