//
//  Knob.h
//  Kitbox
//
//  The only control on the panel: a flat rotary knob with its name and its
//  value printed underneath.
//
//  The value is always shown, not only while dragging. Two rows of knobs whose
//  settings can only be read by touching them is a panel that cannot be read
//  from a screenshot, or at a glance mid-take.
//
//  Bipolar knobs - pan, tune, fine, filter envelope amount - draw their arc
//  from twelve o'clock, so "nothing" looks like nothing. Double-click returns a
//  knob to its default.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class Knob : public juce::Slider
{
public:
    Knob (const juce::String& labelText, bool isBipolar = false);
    ~Knob() override;

    /** Binds to a parameter, replacing any previous binding. */
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId);

    /** The name printed under the knob; an effect's A and B are renamed by the effect loaded. */
    void setLabel (const juce::String& newLabel);

    void paint (juce::Graphics&) override;

    static constexpr int labelHeight = 26;   // name + value under the knob

private:
    juce::String label;
    bool bipolar;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
