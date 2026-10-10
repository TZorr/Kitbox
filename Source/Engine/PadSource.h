//
//  PadSource.h
//  Kitbox
//
//  What a pad plays: its sample, or Transmute's synth of it - the model the
//  hit suggests (Auto), or one of the five chosen by hand. Chosen per pad from
//  the pad's right-click menu; SYN sets every pad that can be modelled to Auto.
//
//  Kept as a property on the state tree ("padSource1" ... "padSource16"), not
//  as a parameter: it is not something to automate, and a new parameter would
//  change the plugin's parameter list in every host.
//

#pragma once

#include <array>
#include <optional>

#include <juce_core/juce_core.h>

#include "../Transmute/DrumParams.h"

enum class PadSource { sample, automatic, kick, snare, hat, modal, clap };

namespace PadSources
{
    inline constexpr std::array<PadSource, 7> all { PadSource::sample, PadSource::automatic, PadSource::kick,
                                                    PadSource::snare, PadSource::hat, PadSource::modal, PadSource::clap };

    /** The model a source asks for; empty for the sample and for Auto. */
    inline std::optional<transmute::DrumModel> model (PadSource source)
    {
        switch (source)
        {
            case PadSource::kick:  return transmute::DrumModel::kick;
            case PadSource::snare: return transmute::DrumModel::snare;
            case PadSource::hat:   return transmute::DrumModel::hat;
            case PadSource::modal: return transmute::DrumModel::modal;
            case PadSource::clap:  return transmute::DrumModel::clap;
            case PadSource::sample:
            case PadSource::automatic: break;
        }
        return std::nullopt;
    }

    /** "sample", "auto", then Transmute's own model ids. */
    inline juce::String id (PadSource source)
    {
        if (source == PadSource::sample)    return "sample";
        if (source == PadSource::automatic) return "auto";
        return transmute::modelId (*model (source));
    }

    inline PadSource fromId (const juce::String& text)
    {
        for (auto source : all)
            if (id (source) == text)
                return source;
        return PadSource::sample;
    }

    inline juce::Identifier sourceProperty (int pad)    { return juce::Identifier ("padSource" + juce::String (pad + 1)); }
    inline juce::Identifier suggestedProperty (int pad) { return juce::Identifier ("padSuggested" + juce::String (pad + 1)); }
}
