//
//  SampleDisplay.h
//  Kitbox
//
//  The card at the top of the pad editor: which pad is selected, what is on
//  it, and its waveform.
//
//  Left: the pad's number in its colour, the file, its note and routing, its
//  rate and length - or, while it fits, how far the fit has got. Under them
//  a play button and the pad's SYN switch.
//
//  Right: the waveform as bars in the pad's colour, the amp envelope (Attack,
//  Hold, Decay) drawn over it in orange from the start point, the start point
//  itself marked "S", and a time scale along the bottom. While the pad fits,
//  a bar crosses the waveform from left to right. Click it to audition the
//  pad at full velocity.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class KitboxProcessor;

class SampleDisplay : public juce::Component
{
public:
    explicit SampleDisplay (KitboxProcessor& processorToUse);

    void setPad (int pad);

    /** The SYN switch was clicked: the pad's own switch does the work. */
    std::function<void()> onToggleSynth;

    /** 30 Hz, from the editor. Repaints only when something shown changed. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String routingText() const;
    juce::String statusText() const;
    juce::String stateKey() const;

    juce::Rectangle<float> infoBounds() const;
    juce::Rectangle<float> waveBounds() const;
    juce::Rectangle<float> playBounds() const;
    juce::Rectangle<float> synBounds() const;
    bool synVisible() const;

    void paintWave (juce::Graphics&, juce::Rectangle<float> well);

    KitboxProcessor& processor;
    int pad = 0;
    juce::String shownKey;
};
