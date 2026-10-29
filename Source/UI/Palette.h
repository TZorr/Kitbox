//
//  Palette.h
//  Kitbox
//
//  Every colour on the panel, and the fonts.
//
//  Since 0.9.0 the panel follows the "Kitbox 0.9 - Vorschlag" mockup: a
//  near-black body, cards a step lighter with a hairline edge, one orange
//  accent, and pads filled with their colour - coral kicks, teal snares,
//  amber hats, mint percussion, every other pad a shade apart. Flat
//  throughout: depth is said with greys and hairlines, not with light.
//
//  The mockup is the dark panel. The light one is derived from it: the same
//  layout, accent and pad colours, the greys turned round. The choice is kept
//  per user, not per kit (see PluginEditor.cpp).
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    // The colours below are the dark panel, the mockup's. They are not
    // const: setDark() swaps the whole set, and since every component reads
    // them while it paints, a repaint is all a switch needs.
    inline juce::Colour body        { 0xff111113 };   // the window
    inline juce::Colour header      { 0xff161619 };   // header bar
    inline juce::Colour footer      { 0xff151518 };   // status bar
    inline juce::Colour line        { 0xff25252a };   // header and card edges
    inline juce::Colour rule        { 0xff232328 };   // rules after a section title
    inline juce::Colour divider     { 0xff2a2a2f };   // the short uprights in the header

    inline juce::Colour card        { 0xff19191c };   // pad editor cards
    inline juce::Colour fxCard      { 0xff151518 };   // FX bus cards
    inline juce::Colour fxCardLine  { 0xff24242a };
    inline juce::Colour padWell     { 0xff0b0b0d };   // behind the pads
    inline juce::Colour rack        { 0xff0c0c0e };   // the FX bus's frame, a step darker
    inline juce::Colour rackLine    { 0xff1c1c20 };

    inline juce::Colour button      { 0xff1f1f23 };
    inline juce::Colour buttonLine  { 0xff2e2e34 };
    inline juce::Colour buttonText  { 0xffd7d5d2 };
    inline juce::Colour icon        { 0xffc9c7c3 };

    inline juce::Colour field       { 0xff222226 };   // drop-downs, small buttons
    inline juce::Colour fieldLine   { 0xff33333a };
    inline juce::Colour pill        { 0xff1c1c20 };
    inline juce::Colour pillLine    { 0xff2a2a30 };

    inline juce::Colour title       { 0xfff2f0ed };   // the brightest type
    inline juce::Colour ink         { 0xffeceae6 };   // values
    inline juce::Colour heading     { 0xffb4b2ae };   // section titles
    inline juce::Colour inkSoft     { 0xffa3a3aa };
    inline juce::Colour inkDim      { 0xff9a9aa1 };   // labels
    inline juce::Colour inkFaint    { 0xff8e8e95 };   // hints, meta

    inline juce::Colour accent      { 0xffff8a3d };
    inline juce::Colour signal      { 0xff4fc1a6 };   // meter, MIDI light
    inline juce::Colour warning     { 0xffe04a4f };

    inline juce::Colour knobTrack   { 0xff2d2d33 };
    inline juce::Colour knobFace    { 0xff27272c };
    inline juce::Colour knobEdge    { 0xff36363d };
    inline juce::Colour knobPointer { 0xfff2f0ed };
    inline juce::Colour meterTrack  { 0xff26262b };

    inline juce::Colour wave        { 0xff121215 };   // the waveform's well
    inline juce::Colour waveLine    { 0xff232328 };
    inline juce::Colour waveGrid    { 0xff24242a };

    inline juce::Colour padEmpty    { 0xff18181b };
    inline juce::Colour padEmptyLine{ 0xff3a3a42 };
    inline juce::Colour padRing     { 0xfff7f5f2 };   // inside the selected pad's orange ring

    inline bool dark = true;

    /** Dark (the mockup) or light. Process-wide, so every open Kitbox window
        follows (each repaints when it notices). */
    inline void setDark (bool shouldBeDark)
    {
        dark = shouldBeDark;

        const auto set = [] (juce::Colour& c, juce::uint32 darkValue, juce::uint32 light)
        {
            c = juce::Colour (dark ? darkValue : light);
        };

        set (body,         0xff111113, 0xffe9e7e3);
        set (header,       0xff161619, 0xfff4f3f0);
        set (footer,       0xff151518, 0xfff1f0ed);
        set (line,         0xff25252a, 0xffd6d3cd);
        set (rule,         0xff232328, 0xffd6d3cd);
        set (divider,      0xff2a2a2f, 0xffcfccc6);

        set (card,         0xff19191c, 0xfff7f6f4);
        set (fxCard,       0xff151518, 0xfff2f1ee);
        set (fxCardLine,   0xff24242a, 0xffd9d6d0);
        set (padWell,      0xff0b0b0d, 0xffd5d2cc);
        set (rack,         0xff0c0c0e, 0xffdfdcd7);
        set (rackLine,     0xff1c1c20, 0xffd0ccc6);

        set (button,       0xff1f1f23, 0xfffbfaf8);
        set (buttonLine,   0xff2e2e34, 0xffd0cdc7);
        set (buttonText,   0xffd7d5d2, 0xff34322f);
        set (icon,         0xffc9c7c3, 0xff4a4744);

        set (field,        0xff222226, 0xffffffff);
        set (fieldLine,    0xff33333a, 0xffcfccc6);
        set (pill,         0xff1c1c20, 0xffffffff);
        set (pillLine,     0xff2a2a30, 0xffd6d3cd);

        set (title,        0xfff2f0ed, 0xff161514);
        set (ink,          0xffeceae6, 0xff1f1e1c);
        set (heading,      0xffb4b2ae, 0xff57544f);
        set (inkSoft,      0xffa3a3aa, 0xff5f5c57);
        set (inkDim,       0xff9a9aa1, 0xff6e6b65);
        set (inkFaint,     0xff8e8e95, 0xff86837c);

        set (accent,       0xffff8a3d, 0xfff57a28);
        set (signal,       0xff4fc1a6, 0xff2fa486);
        set (warning,      0xffe04a4f, 0xffd63a40);

        set (knobTrack,    0xff2d2d33, 0xffdcd9d3);
        set (knobFace,     0xff27272c, 0xffffffff);
        set (knobEdge,     0xff36363d, 0xffc9c5be);
        set (knobPointer,  0xfff2f0ed, 0xff2a2826);
        set (meterTrack,   0xff26262b, 0xffdcd9d3);

        set (wave,         0xff121215, 0xfffdfcfa);
        set (waveLine,     0xff232328, 0xffdcd9d3);
        set (waveGrid,     0xff24242a, 0xffe6e3de);

        set (padEmpty,     0xff18181b, 0xffefeeeb);
        set (padEmptyLine, 0xff3a3a42, 0xffb9b5ae);
        set (padRing,      0xfff7f5f2, 0xffffffff);
    }

    //==============================================================================
    //  Colour zones (the "Farbzonen" mockup)
    //
    //  Each card says by colour what it works on: amber for the sound itself
    //  (Sample, Amp), blue for the filter, grey for the mix and routing, and
    //  one colour per send effect - the same on its card in the FX rack and on
    //  its send knob in Mix, so a send and its return read as a pair.
    //
    //  A zone is one colour, desaturated to 70 %, and what is mixed from it:
    //  the card's faint tint (7 % into the card, 1.5 % for the grey zones),
    //  its edge (24 %), its title (a third towards white - towards black on
    //  the light panel) and, for send knobs, their track. Computed while
    //  painting, so a theme switch needs no more than a repaint.

    enum class ZoneId { none, source, filter, mix, routing, reverb, delay, mod, neutral };

    struct Zone
    {
        juce::Colour line, tint, border, label, track;
    };

    inline juce::Colour zoneBase (ZoneId id)
    {
        switch (id)
        {
            case ZoneId::source:  return juce::Colour (0xffe9a23b);
            case ZoneId::filter:  return juce::Colour (0xff4c9eeb);
            case ZoneId::reverb:  return juce::Colour (0xff8b7cf6);
            case ZoneId::delay:   return juce::Colour (0xff3fc4a8);
            case ZoneId::mod:     return juce::Colour (0xffe06ab0);
            case ZoneId::routing: return juce::Colour (0xff8a8f98);
            case ZoneId::mix:
            case ZoneId::neutral: return dark ? juce::Colour (0xffc4c4cc) : juce::Colour (0xff7d7a74);
            case ZoneId::none:    break;
        }
        return accent;
    }

    inline bool isRackZone (ZoneId id)
    {
        return id == ZoneId::reverb || id == ZoneId::delay || id == ZoneId::mod;
    }

    inline Zone zone (ZoneId id, bool inRack = false)
    {
        if (id == ZoneId::none)
            return { accent, card, line, heading, knobTrack };

        const auto raw = zoneBase (id);
        const auto grey = 0.3f * raw.getFloatRed() + 0.59f * raw.getFloatGreen() + 0.11f * raw.getFloatBlue();
        const auto desaturate = [grey] (float v) { return grey + (v - grey) * 0.7f; };
        const auto colour = juce::Colour::fromFloatRGBA (desaturate (raw.getFloatRed()), desaturate (raw.getFloatGreen()),
                                                         desaturate (raw.getFloatBlue()), 1.0f);

        const auto base = inRack ? fxCard : card;
        const auto amount = id == ZoneId::mix || id == ZoneId::routing || id == ZoneId::neutral ? 0.015f
                          : dark ? 0.07f : 0.09f;

        return { colour,
                 base.interpolatedWith (colour, amount),
                 base.interpolatedWith (colour, dark ? 0.24f : 0.32f),
                 colour.interpolatedWith (dark ? juce::Colours::white : juce::Colours::black, 0.35f),
                 knobTrack.interpolatedWith (colour, 0.32f) };
    }

    //==============================================================================
    //  Pads

    /** The four groups of four, as in the mockup: a pair of shades per group,
        alternating, so neighbours stay apart. Pads 1-4 kicks, 5-8 snares,
        9-12 hats, 13-16 percussion. */
    inline juce::Colour padColour (int padIndex)
    {
        static const juce::uint32 shades[4][2] = {
            { 0xffd9625f, 0xffe07870 },   // kick
            { 0xff3a9da9, 0xff35919d },   // snare
            { 0xffe8b34a, 0xffd99b3e },   // hat
            { 0xffa6d4c3, 0xffe6dbbe }    // perc
        };
        return juce::Colour (shades[(padIndex / 4) & 3][padIndex & 1]);
    }

    /** The pad's own colour if it has one, its group's otherwise. */
    inline juce::Colour padColour (int padIndex, const std::optional<juce::Colour>& chosen)
    {
        return chosen ? *chosen : padColour (padIndex);
    }

    /** What the pad menu's Colour offers: the eight group shades, then four
        to tell more voices apart. */
    struct NamedColour { const char* name; juce::uint32 argb; };

    inline const std::array<NamedColour, 12> padChoices { {
        { "Coral",    0xffd9625f }, { "Salmon",   0xffe07870 },
        { "Teal",     0xff3a9da9 }, { "Lagoon",   0xff35919d },
        { "Amber",    0xffe8b34a }, { "Ochre",    0xffd99b3e },
        { "Mint",     0xffa6d4c3 }, { "Sand",     0xffe6dbbe },
        { "Violet",   0xff9b86e8 }, { "Sky",      0xff6aa6ee },
        { "Rose",     0xffe58ab9 }, { "Lime",     0xffa9cf62 }
    } };

    /** Type on a pad: near-black on light fills, near-white on dark ones. */
    inline juce::Colour padInk (juce::Colour fill)
    {
        const auto luminance = 0.2126f * fill.getFloatRed() + 0.7152f * fill.getFloatGreen() + 0.0722f * fill.getFloatBlue();
        return luminance > 0.42f ? juce::Colour (0xff141416) : juce::Colour (0xfff4f2ef);
    }

    //==============================================================================
    //  Type
    //
    //  The mockup sets Inter with system-ui behind it; the panel uses the
    //  system face, which is what a Mac without Inter shows the mockup in.
    //  600 and up is bold, the rest plain.

    inline juce::Font font (float height, bool bold = false, float tracking = 0.0f)
    {
        auto f = juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
        return tracking != 0.0f ? f.withExtraKerningFactor (tracking) : f;
    }

    /** Uppercase labels: 0.04 em apart, bold. */
    inline juce::Font label (float height = 11.0f)
    {
        return font (height, true, 0.04f);
    }
}
