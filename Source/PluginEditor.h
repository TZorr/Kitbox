//
//  PluginEditor.h
//  Kitbox
//
//  The panel, top to bottom:
//
//      KITBOX   [ display: pad, sample, waveform ]   Load/Save/Learn   Humanize  Master
//      SAMPLE  | AMP     | FILTER       | FILTER ENV   | ROUTING      <- selected pad
//      MIX     | REVERB       | DELAY             | MOD               <- pad mix, then global FX
//      [ 1][ 2][ 3][ 4][ 5][ 6][ 7][ 8]
//      [ 9][10][11][12][13][14][15][16]
//
//  The knobs in SAMPLE, AMP, FILTER, FILTER ENV, ROUTING and MIX belong to the selected
//  pad and are rebound when another pad is selected; their section titles
//  carry the pad's number and colour so that is visible. REVERB, DELAY and
//  MOD are the shared send effects: Type, the effect's two knobs (named by the
//  effect loaded) and the return level.
//
//  The pads sit a clear 30 points above the bottom edge: Logic swallows clicks
//  in roughly the last 20 points of a plugin window, and a pad there would
//  sometimes simply not play.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/Knob.h"
#include "UI/PadButton.h"
#include "UI/SampleDisplay.h"

class KitboxEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit KitboxEditor (KitboxProcessor&);
    ~KitboxEditor() override;

    static constexpr int editorWidth  = 1080;
    static constexpr int editorHeight = 548;

    void paint (juce::Graphics&) override;
    void resized() override;

    void selectPad (int pad);
    int getSelectedPad() const noexcept { return selectedPad; }

protected:
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                               juce::StringArray& files, bool& canMoveFiles) override;

private:
    struct Section
    {
        juce::String title;
        bool perPad;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    class FlatButton : public juce::Button
    {
    public:
        explicit FlatButton (const juce::String& text) : juce::Button (text) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

        /** Lit in the accent colour, for Learn while it waits for a note. */
        bool active = false;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void bindPadKnobs();

    /** The effect loaded in a send slot, and its knobs renamed to match. */
    const EffectCatalog::Info& fxInfo (int slot) const;
    void refreshFxLabels();
    void layoutRow (std::vector<Section*> row, juce::Rectangle<int> area);
    void loadKit();
    void saveKit();

    KitboxProcessor& kitbox;
    int selectedPad = 0;

    // Per pad
    Knob tune { "Tune", true }, fine { "Fine", true }, start { "Start" }, velocity { "Velocity" };
    Knob attack { "Attack" }, hold { "Hold" }, decay { "Decay" };
    Knob filterType { "Type" }, cutoff { "Cutoff" }, resonance { "Reso" }, drive { "Drive" };
    Knob envAttack { "Attack" }, envDecay { "Decay" }, envAmount { "Amount", true };
    Knob level { "Level" }, pan { "Pan", true }, sendReverb { "Reverb" }, sendDelay { "Delay" }, sendPhaser { "Mod" };
    Knob note { "Note" }, choke { "Choke" }, output { "Output" };

    // Global
    struct FxKnobs
    {
        Knob type { "Type" }, a { "A" }, b { "B" }, level { "Level" };
        int shownType = -1;
    };

    std::array<FxKnobs, KitParams::Fx::numSlots> fx;
    Knob humanize { "Humanize" }, master { "Master" };

    Section sectionSample, sectionAmp, sectionFilter, sectionEnv, sectionRouting, sectionMix;
    Section sectionReverb, sectionDelay, sectionMod;

    SampleDisplay display;
    FlatButton loadButton { "Load Kit" }, saveButton { "Save Kit" }, learnButton { "Learn Note" };
    juce::Rectangle<int> wordmarkArea, globalArea, footerArea;

    std::vector<std::unique_ptr<PadButton>> pads;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitboxEditor)
};
