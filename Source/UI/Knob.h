//
//  Knob.h
//  Kitbox
//
//  A flat rotary knob with its name and its value printed with it.
//
//  Two layouts: stacked - the name over a 52-point knob, as in the pad editor
//  and the FX bus - and beside, a 38-point knob with name and value to its
//  right, as Humanize and Master in the header.
//
//  A stacked knob shows its value in place of its name, as Logic's own
//  plug-ins do: while the mouse is over it or turns it, and for a second
//  after. That saves the value's row under every knob. A change the host
//  makes - automation - leaves the name standing, so a playing automation
//  lane does not set every label flickering.
//
//  Bipolar knobs - pan, tune, fine, filter envelope amount - draw their arc
//  from twelve o'clock, so "nothing" looks like nothing. Double-click returns a
//  knob to its default.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Palette.h"

class Knob : public juce::Slider,
             private juce::Timer
{
public:
    enum class Layout { stacked, beside };

    Knob (const juce::String& labelText, bool isBipolar = false, Layout layoutToUse = Layout::stacked);
    ~Knob() override;

    /** Binds to a parameter, replacing any previous binding. */
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId);

    /** The name printed with the knob; an effect's A and B are renamed by the effect loaded. */
    void setLabel (const juce::String& newLabel);

    /** The colour zone the knob sits in: its arc takes the zone's colour.
        A send knob also takes the zone's track and name colour, which ties
        it to its effect in the FX rack. */
    void setZone (Palette::ZoneId zoneToUse, bool tintTrackAndName = false, bool inRack = false);

    void paint (juce::Graphics&) override;
    void valueChanged() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Whether a stacked knob shows its value where its name goes. */
    bool isShowingValue() const { return isMouseOverOrDragging() || isTimerRunning(); }

    /** Height of a stacked knob: name or value, 6, dial. */
    static constexpr int stackedHeight = 74;

    /** How long the value stays after the mouse has left or let go. */
    static constexpr int valueHoldMs = 1000;

    /** Draws a dial of the mockup's geometry - 52 units, ring radius 21 -
        scaled into `area`. Shared with nothing else yet, but kept apart so the
        two layouts cannot drift. */
    static void drawDial (juce::Graphics&, juce::Rectangle<float> area, float proportion, bool bipolar,
                          bool hover, juce::Colour value, juce::Colour track, float strokeScale = 1.0f);

private:
    void timerCallback() override;
    void holdValue();

    juce::String label;
    bool bipolar;
    Layout layout;
    Palette::ZoneId zone = Palette::ZoneId::none;
    bool tinted = false, rack = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
