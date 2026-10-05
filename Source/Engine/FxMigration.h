//
//  FxMigration.h
//  Kitbox
//
//  Old kits and sessions, from before the send effects became slots.
//
//  They name the old knobs - rev_size, dly_time, phs_rate and the rest - which
//  no longer exist, and JUCE would simply ignore them: the kit would come back
//  with every effect at its default. So before the parameter tree is handed to
//  the APVTS it is rewritten here, old knobs into the new slot knobs:
//
//      Reverb  Plate,  A = size,  B = damping                  (both 0..100 %)
//      Delay   Tape,   A = the same note value,  B = feedback / 95 %
//                      (Rackbox's delays feed back 0.95 x B)
//      Mod     Phaser, A = the rate on Rackbox's 0.05-10 Hz exponential map,
//                      B = depth
//
//  Pre-delay, delay tone and phaser feedback have nothing to go to. An old kit
//  therefore sounds close to what it did, not identical - the effects behind
//  the knobs are Rackbox's now.
//
//  The APVTS keeps each parameter as <PARAM id=".." value=".."/> with the value
//  in its own units (dB, %, a choice index), which is what is read and written.
//

#pragma once

#include <cmath>

#include <juce_data_structures/juce_data_structures.h>

#include "ParameterIds.h"

namespace FxMigration
{
    /** Rewrites an old parameter tree in place. Returns false, and leaves the
        tree alone, when there is nothing to translate. */
    inline bool apply (juce::ValueTree& state)
    {
        namespace L = KitParams::Legacy;
        namespace F = KitParams::Fx;

        const auto find = [&state] (const char* id) { return state.getChildWithProperty ("id", juce::String (id)); };

        const auto hasOld = find (L::reverbSize).isValid() || find (L::delayTime).isValid() || find (L::phaserRate).isValid();
        const auto hasNew = find (F::ids[F::reverb].a).isValid();

        if (! hasOld || hasNew)
            return false;

        const auto get = [&find] (const char* id, float fallback)
        {
            const auto param = find (id);
            return param.isValid() ? (float) param.getProperty ("value") : fallback;
        };

        const auto set = [&state] (const char* id, float value)
        {
            juce::ValueTree param ("PARAM");
            param.setProperty ("id", juce::String (id), nullptr);
            param.setProperty ("value", value, nullptr);
            state.appendChild (param, nullptr);
        };

        set (F::ids[F::reverb].type, 0.0f);   // Plate
        set (F::ids[F::reverb].a, juce::jlimit (0.0f, 1.0f, get (L::reverbSize, 50.0f) / 100.0f));
        set (F::ids[F::reverb].b, juce::jlimit (0.0f, 1.0f, get (L::reverbDamping, 40.0f) / 100.0f));

        // The old list is Dsp::Sync's without "1/2 D": indices match up to 1/2,
        // and the old last step (1/1) is the new last.
        const auto division = juce::jlimit (0, L::numDelayDivisions - 1, juce::roundToInt (get (L::delayTime, 5.0f)));
        const auto syncIndex = division < L::numDelayDivisions - 1 ? division : 13;
        set (F::ids[F::delay].type, 1.0f);    // Tape: the old delay darkened its repeats too
        set (F::ids[F::delay].a, (float) syncIndex / 13.0f);
        set (F::ids[F::delay].b, juce::jlimit (0.0f, 1.0f, get (L::delayFeedback, 35.0f) / 95.0f));

        const auto rate = juce::jlimit (0.05f, 10.0f, get (L::phaserRate, 0.4f));
        set (F::ids[F::mod].type, 1.0f);      // Phaser (the catalog lists Chorus first)
        set (F::ids[F::mod].a, std::log (rate / 0.05f) / std::log (200.0f));
        set (F::ids[F::mod].b, juce::jlimit (0.0f, 1.0f, get (L::phaserDepth, 70.0f) / 100.0f));

        for (const auto* id : { L::reverbSize, L::reverbDamping, L::reverbPreDelay, L::delayTime, L::delayFeedback,
                                L::delayTone, L::phaserRate, L::phaserDepth, L::phaserFeedback })
            if (auto old = find (id); old.isValid())
                state.removeChild (old, nullptr);

        return true;
    }
}
