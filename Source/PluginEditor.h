//
//  PluginEditor.h
//  Kitbox
//
//  The panel, since 0.10.0 "Variante B - Kompakt, Farbringe":
//
//      KITBOX  [<][>][ kit  FOLDER  v] LOAD SAVE LEARN   (o) Humanize (o) Master ||  [moon][settings]
//      PADS  C1 - D#2  SYN ALL   . Kick . Snare . Hat . Perc   [ display: pad, file, facts | waveform ]
//      [ 1][ 2][ 3][ 4]                                        +------------------------------------+
//      [ 5][ 6][ 7][ 8]                                        |S| SAMPLE      |F| FILTER   |F| ENV |
//      [ 9][10][11][12]                                        |A| AMP  |M| MIX         |R| ROUTE   |
//      [13][14][15][16]                                        |R| REVERB  |D| DELAY    |M| MOD    |
//                                                              +------------------------------------+
//      KITBOX 0.10.0 | hints                                                  MIDI  Voices 3 / 64
//
//  The rack is one panel: three rows split by hairlines, each row's sections
//  by uprights, every section named down its left edge, and the colour of
//  what it works on in its knobs' rings. The first two rows belong to the
//  selected pad and are rebound when another pad is selected. The third is
//  the FX bus, the three shared send effects, a step darker: each with its
//  ON / OFF as the upright name, Type, the effect's two knobs (named by the
//  effect loaded) and the return level.
//
//  The panel is laid out once at the mockup's 1640 x 588 and scaled as a
//  whole: the window resizes freely at that aspect ratio, opens at 85 % the
//  first time, and remembers the size per user. The pads sit far from the
//  bottom edge, where Logic swallows clicks in roughly the last 20 points.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/Controls.h"
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

    /** The panel's own size; the window shows it scaled. */
    static constexpr int panelWidth  = 1640;
    static constexpr int panelHeight = 588;
    static constexpr double defaultScale = 0.85, minScale = 0.6, maxScale = 1.4;

    void paint (juce::Graphics&) override;
    void resized() override;

    void selectPad (int pad);
    int getSelectedPad() const noexcept { return selectedPad; }

    /** The kit the browser's arrows go to from `current`: the next one
        (delta 1) or the one before (-1) in the same folder, round from its
        end to its start. Without a current kit, the first or last kit in
        `fallbackFolder`, or in its first folder holding any. None when there
        is no kit at all. Static, for the tests. */
    static juce::File neighbourKit (const juce::File& current, const juce::File& fallbackFolder, int delta);

    /** The sixteen pads, for the tests. */
    PadButton& getPad (int pad) { return *pads[(size_t) pad]; }

protected:
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                               juce::StringArray& files, bool& canMoveFiles) override;

private:
    /** The 1640 x 588 panel: everything on it is its child, and it is drawn
        through the editor's paintPanel. */
    struct Panel : public juce::Component
    {
        explicit Panel (KitboxEditor& ownerToUse) : owner (ownerToUse) { setOpaque (true); }
        void paint (juce::Graphics& g) override { owner.paintPanel (g); }
        KitboxEditor& owner;
    };

    /** A section of the rack: its upright title, its controls, one column each. */
    struct Section
    {
        juce::String title;
        std::vector<juce::Component*> controls;
        juce::Rectangle<int> bounds;
        bool fx = false;
        Palette::ZoneId zone = Palette::ZoneId::none;
        RackPower* power = nullptr;   // an effect's ON / OFF, which is its title too
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void paintPanel (juce::Graphics&);
    void layoutPanel();
    void layoutRow (std::vector<Section*> row, juce::Rectangle<int> area);
    void refreshDimming();
    void showSettingsMenu();

    void bindPadKnobs();

    /** The effect loaded in a send slot, and its knobs renamed to match. */
    const EffectCatalog::Info& fxInfo (int slot) const;
    void refreshFxLabels();

    void loadKit();
    void saveKit();
    void loadKitFile (const juce::File&);
    void showKitMenu();
    void stepKit (int delta);
    juce::Array<juce::File> kitFiles() const;
    juce::File currentKit() const;
    void refreshKitName();

    void setDarkMode (bool shouldBeDark);
    void refreshThemeIcon();

    KitboxProcessor& kitbox;
    Panel panel { *this };
    int selectedPad = 0;

    // Per pad
    Knob tune { "Tune", true }, fine { "Fine", true }, start { "Start" }, velocity { "Velocity" };
    Knob attack { "Attack" }, hold { "Hold" }, decay { "Decay" };
    ChoiceBox filterType { "Type" };
    Knob cutoff { "Cutoff" }, resonance { "Reso" }, drive { "Drive" };
    Knob envAttack { "Attack" }, envDecay { "Decay" }, envAmount { "Amount", true };
    Knob level { "Level" }, pan { "Pan", true }, sendReverb { "Rev" }, sendDelay { "Dly" }, sendPhaser { "Mod" };
    ChoiceBox note { "Note" }, choke { "Choke" }, output { "Out" };

    // Global
    struct FxControls
    {
        ChoiceBox type { "Type" };
        Knob a { "A" }, b { "B" }, level { "Level" };
        std::unique_ptr<RackPower> power;
        int shownType = -1;
    };

    std::array<FxControls, KitParams::Fx::numSlots> fx;
    Knob humanize { "Humanize", false, Knob::Layout::beside }, master { "Master", false, Knob::Layout::beside };

    Section sectionSample, sectionAmp, sectionFilter, sectionEnv, sectionRouting, sectionMix;
    Section sectionReverb, sectionDelay, sectionMod;

    SampleDisplay display;
    PanelButton previousKit { {}, "M10 3.5 5.5 8l4.5 4.5" }, nextKit { {}, "M6 3.5 10.5 8 6 12.5" };
    PanelButton kitField { {}, {}, {}, PanelButton::Style::field };
    PanelButton loadButton { "Load", "M2 4.5A1.5 1.5 0 0 1 3.5 3H6l1.5 1.5h5A1.5 1.5 0 0 1 14 6v5.5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 11.5z" };
    PanelButton saveButton { "Save", "M8 2.5v7.5M4.8 7 8 10.2 11.2 7M3 13.5h10" };
    PanelButton learnButton { "Learn", "M13.5 8a5.5 5.5 0 1 1-11 0a5.5 5.5 0 1 1 11 0", "M9.8 8a1.8 1.8 0 1 1-3.6 0a1.8 1.8 0 1 1 3.6 0" };
    PanelButton themeButton { {} };
    PanelButton settingsButton { {}, "M2.5 5h6.5M12 5h1.5M2.5 11h1.5M7 11h6.5"
                                     "M12 5a1.5 1.5 0 1 1-3 0a1.5 1.5 0 1 1 3 0M7 11a1.5 1.5 0 1 1-3 0a1.5 1.5 0 1 1 3 0" };
    Pill synAll { "SYN ALL", false };
    bool shownDark = false;

    // Header meter, status bar.
    std::array<float, 2> meterLevels {};
    int shownMidiCount = 0, shownVoices = -1;
    double midiLitUntil = 0.0;

    juce::Rectangle<int> headerArea, wordmarkArea, metersArea, padsTitleArea, legendArea, padWellArea,
                         rackArea, footerArea;
    std::array<juce::Rectangle<int>, 3> rackRows;
    std::vector<std::vector<Section*>> rackLayout;
    std::array<juce::Rectangle<int>, 2> headerDividers;

    std::vector<std::unique_ptr<PadButton>> pads;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::PropertiesFile> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitboxEditor)
};
