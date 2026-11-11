//
//  Knob.h
//  Kitbox
//
//  A rotary knob with its name and its value printed with it.
//
//  Since 0.10.0 (Variante B) a light metal cap sits inside a coloured ring:
//  the ring's track and value in the knob's zone colour, a dot where the
//  value starts from, a dark pointer on the cap.
//
//  Two layouts: stacked - the value over a 42-point knob and the name under
//  it, as in the control rack - and beside, a 38-point knob with name and
//  value to its right, as Humanize and Master in the header.
//
//  A stacked knob lights its value while the mouse is over it or turns it,
//  and for a second after. A change the host makes - automation - leaves it
//  dim, so a playing automation lane does not set every value flickering.
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

    /** Whether a stacked knob lights its value. */
    bool isShowingValue() const { return isMouseOverOrDragging() || isTimerRunning(); }

    /** Height of a stacked knob: value 14, 1, dial 42, 1, name 14. */
    static constexpr int stackedHeight = 72;

    /** How long the value stays after the mouse has left or let go. */
    static constexpr int valueHoldMs = 1000;

    /** Draws a dial of the mockup's geometry - 52 units, ring radius 22 -
        scaled into `area`. `big` is the header's: a heavier cap without the
        highlight and without the origin dot. */
    static void drawDial (juce::Graphics&, juce::Rectangle<float> area, float proportion, bool bipolar,
                          bool hover, juce::Colour value, juce::Colour track, bool big = false);

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
