//
//  EffectCatalog.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  The list of what a block can hold: four categories, fourteen effects.
//
//  This is the one place that knows the names. The editor prints them, the
//  display prints them, the block asks it for an instance - and the effects
//  themselves know nothing about their own name or what their two knobs are
//  called. That is on purpose: a block's two effect knobs are fixed parameters
//  whose meaning depends on what is loaded, and the table is where "the first
//  knob" turns into Rate, Size or Time.
//
//  Categories are chosen by one parameter and the subtype by another, and the
//  subtype range is the longest list (five, since Modulation gained the
//  frequency Shifter in 0.4.0) for every category. A shorter category clamps: a stored
//  subtype of 3 or 4 in Reverb means Hall, so a saved session never asks for an
//  effect that does not exist.
//

#pragma once

#include <memory>

#include <juce_core/juce_core.h>

#include "Effect.h"

namespace EffectCatalog
{
    constexpr int numCategories = 4;
    constexpr int maxSubtypes = 5;
    constexpr int numEffects = 14;

    enum class Unit { Percent, RateHz, TimeMs, Bits, Factor, Division, ShiftHz };

    struct Info
    {
        const char* name;       // "Chorus"
        const char* labelA;     // "Rate"
        Unit unitA;
        const char* labelB;
        Unit unitB;
    };

    const char* categoryName (int category);
    int numSubtypes (int category);

    /** The subtype limited to what the category has. */
    int clampSubtype (int category, int subtype);

    /** Category and subtype to a running index 0..numEffects-1. */
    int index (int category, int subtype);

    const Info& info (int category, int subtype);

    /** A fresh instance. Allocates - never call from the audio thread. */
    std::unique_ptr<Effect> create (int effectIndex);

    /** Reverbs have a pre-delay button where the others have Sync. */
    bool hasPreDelay (int category);

    /** Whether the effect's first knob is a time or a rate that can follow the host tempo. */
    bool canSync (int category, int subtype);

    /** A knob position 0..1 printed in the unit the effect uses for it. */
    juce::String format (Unit unit, float normalised);
}
