//
//  SampleDisplay.h
//  Kitbox
//
//  The dark screen in the header: which pad is selected, what is on it, and
//  its waveform with the start point marked.
//
//  Click it to audition the pad at full velocity.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class KitboxProcessor;

class SampleDisplay : public juce::Component
{
public:
    explicit SampleDisplay (KitboxProcessor& processorToUse);

    void setPad (int pad);

    /** 30 Hz, from the editor. Repaints only when something shown changed. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String routingText() const;

    KitboxProcessor& processor;
    int pad = 0;

    const void* shownSample = nullptr;
    float shownStart = -1.0f;
    bool shownLoading = false;
    juce::String shownRouting;
};
