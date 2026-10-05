//
//  EffectCatalog.cpp
//  Kitbox (copied from Rackbox - fix bugs in both)
//

#include "EffectCatalog.h"

#include "Dsp.h"
#include "Effects/Bitcrusher.h"
#include "Effects/Chorus.h"
#include "Effects/DigitalDelay.h"
#include "Effects/Flanger.h"
#include "Effects/FreqShifter.h"
#include "Effects/Fuzz.h"
#include "Effects/HallVerb.h"
#include "Effects/Overdrive.h"
#include "Effects/Phaser.h"
#include "Effects/PingPongDelay.h"
#include "Effects/PlateVerb.h"
#include "Effects/RoomVerb.h"
#include "Effects/TapeDelay.h"
#include "Effects/Tremolo.h"

namespace EffectCatalog
{
    namespace
    {
        using U = Unit;

        struct Entry
        {
            Info info;
            std::unique_ptr<Effect> (*make)();
        };

        template <typename T>
        std::unique_ptr<Effect> make() { return std::make_unique<T>(); }

        /** A number with a fixed count of decimals. Not juce::String (v, 0): there 0 means "as many
            as it takes", and 23.4567 Hz is what a knob then prints. */
        juce::String fixed (float v, int decimals)
        {
            return decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        }

        struct Category
        {
            const char* name;
            int count;
            Entry entries[maxSubtypes];
        };

        const Category categories[numCategories] =
        {
            { "Modulation", 5, {
                { { "Chorus",  "Rate", U::RateHz, "Depth", U::Percent }, make<Chorus> },
                { { "Phaser",  "Rate", U::RateHz, "Depth", U::Percent }, make<Phaser> },
                { { "Flanger", "Rate", U::RateHz, "Depth", U::Percent }, make<Flanger> },
                { { "Tremolo", "Rate", U::RateHz, "Depth", U::Percent }, make<Tremolo> },
                { { "Shifter", "Shift", U::ShiftHz, "Feedback", U::Percent }, make<FreqShifter> } } },

            { "Reverb", 3, {
                { { "Plate", "Size", U::Percent, "Damp", U::Percent }, make<PlateVerb> },
                { { "Room",  "Size", U::Percent, "Damp", U::Percent }, make<RoomVerb> },
                { { "Hall",  "Size", U::Percent, "Damp", U::Percent }, make<HallVerb> } } },

            { "Delay", 3, {
                { { "Digital",   "Time", U::TimeMs, "Feedback", U::Percent }, make<DigitalDelay> },
                { { "Tape",      "Time", U::TimeMs, "Feedback", U::Percent }, make<TapeDelay> },
                { { "Ping-Pong", "Time", U::TimeMs, "Feedback", U::Percent }, make<PingPongDelay> } } },

            { "Distortion", 3, {
                { { "Overdrive", "Drive", U::Percent, "Tone", U::Percent }, make<Overdrive> },
                { { "Fuzz",      "Fuzz",  U::Percent, "Tone", U::Percent }, make<Fuzz> },
                { { "Bitcrusher", "Bits", U::Bits,    "Rate", U::Factor  }, make<Bitcrusher> } } }
        };

        int firstIndex (int category)
        {
            int first = 0;
            for (int c = 0; c < category; ++c)
                first += categories[c].count;
            return first;
        }
    }

    const char* categoryName (int category)
    {
        return categories[juce::jlimit (0, numCategories - 1, category)].name;
    }

    int numSubtypes (int category)
    {
        return categories[juce::jlimit (0, numCategories - 1, category)].count;
    }

    int clampSubtype (int category, int subtype)
    {
        return juce::jlimit (0, numSubtypes (category) - 1, subtype);
    }

    int index (int category, int subtype)
    {
        category = juce::jlimit (0, numCategories - 1, category);
        return firstIndex (category) + clampSubtype (category, subtype);
    }

    const Info& info (int category, int subtype)
    {
        category = juce::jlimit (0, numCategories - 1, category);
        return categories[category].entries[clampSubtype (category, subtype)].info;
    }

    std::unique_ptr<Effect> create (int effectIndex)
    {
        for (int c = 0; c < numCategories; ++c)
        {
            if (effectIndex < categories[c].count)
                return categories[c].entries[effectIndex].make();

            effectIndex -= categories[c].count;
        }

        jassertfalse;
        return {};
    }

    bool hasPreDelay (int category)
    {
        return category == 1;   // Reverb: the second entry of the table above
    }

    bool canSync (int category, int subtype)
    {
        const auto unit = info (category, subtype).unitA;
        return unit == Unit::TimeMs || unit == Unit::RateHz;
    }

    juce::String format (Unit unit, float x)
    {
        switch (unit)
        {
            case Unit::Percent: return juce::String (juce::roundToInt (100.0f * x)) + " %";
            case Unit::RateHz:  return juce::String (Dsp::Map::rateHz (x), 2) + " Hz";
            case Unit::Bits:    return juce::String (Dsp::Map::bits (x), 1) + " bit";
            case Unit::Division: return Dsp::Sync::names[Dsp::Sync::index (x)];
            case Unit::Factor:  return "x" + juce::String (Dsp::Map::downsample (x), 1);
            case Unit::ShiftHz:
            {
                const auto hz = Dsp::Map::shiftHz (x);
                const auto size = std::abs (hz);
                const auto digits = size < 9.995f ? 2 : (size < 99.95f ? 1 : 0);
                const auto text = fixed (size, digits) + " Hz";
                return hz > 0.0f ? "+" + text : (hz < 0.0f ? "-" + text : text);
            }
            case Unit::TimeMs:
            {
                const auto ms = Dsp::Map::timeMs (x);
                return ms < 100.0f ? juce::String (ms, 1) + " ms"
                                   : (ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms"
                                                   : juce::String (ms / 1000.0f, 2) + " s");
            }
        }

        return {};
    }
}
