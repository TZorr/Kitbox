//
//  Controls.h
//  Kitbox
//
//  The panel's controls besides the knob, all from the 0.9 mockup:
//
//  - ChoiceBox: a drop-down for a parameter with a handful of settings -
//    filter and effect type, note, choke group, output. Click for the list,
//    or turn the scroll wheel over it. Its name sits under it where a knob's
//    would, so it lines up in a row of knobs.
//  - PanelButton: the header's rounded buttons - an icon, a word, or both -
//    and the kit field, whose text is the kit's name.
//  - Pill: the small rounded switch - an effect's ON / OFF, and SYN ALL.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Palette.h"

class ChoiceBox : public juce::Component
{
public:
    explicit ChoiceBox (const juce::String& labelText);
    ~ChoiceBox() override;

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId);

    /** The colour zone the box sits in: its edge takes the zone's. */
    void setZone (Palette::ZoneId zoneToUse, bool inRack = false) { zone = zoneToUse; rack = inRack; repaint(); }

    /** The text shown for a value; the parameter's own text when not set. */
    std::function<juce::String (int)> textForValue;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    juce::Rectangle<float> boxBounds() const;
    juce::String textFor (int value) const;
    void set (int value);
    int count() const;

    juce::String label;
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int current = 0;
    Palette::ZoneId zone = Palette::ZoneId::none;
    bool rack = false;
    float wheelAccumulator = 0.0f;
};

//==============================================================================
class PanelButton : public juce::Button
{
public:
    enum class Style { plain, field };

    /** `iconPath` is SVG path data in a 16-unit box, stroked; `iconDots`,
        filled. Either may be empty. */
    PanelButton (const juce::String& text, const juce::String& iconPath = {}, const juce::String& iconDots = {},
                 Style styleToUse = Style::plain);

    void setIcon (const juce::String& iconPath, const juce::String& iconDots = {});

    /** Lit in the accent colour: Learn while it waits for a note. */
    bool active = false;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Path stroke, fill;
    Style style;
};

//==============================================================================
class Pill : public juce::Button
{
public:
    /** An ON / OFF switch when `isSwitch`, a pressable word otherwise. */
    Pill (const juce::String& text, bool isSwitch);

    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool isSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
