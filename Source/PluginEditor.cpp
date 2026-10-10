//
//  PluginEditor.cpp
//  Kitbox
//

#include "PluginEditor.h"
#include "UI/Palette.h"
#include "TextUtf8.h"

namespace
{
    constexpr int margin = 16;
    constexpr int sectionGap = 10;
    constexpr int sectionPadding = 8;
    constexpr int titleHeight = 20;
}

//==============================================================================
void KitboxEditor::FlatButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();

    const auto lit = down || active;
    g.setColour (lit ? Palette::accent : (highlighted ? Palette::buttonHover : Palette::button));
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (lit ? Palette::pad : Palette::ink);
    g.setFont (Palette::label (10.5f));
    g.drawText (getButtonText().toUpperCase(), bounds, juce::Justification::centred);
}

//==============================================================================
KitboxEditor::KitboxEditor (KitboxProcessor& p)
    : AudioProcessorEditor (&p), kitbox (p), display (p)
{
    sectionSample = { "Sample",     true,  { &tune, &fine, &start, &velocity }, {} };
    sectionAmp    = { "Amp",        true,  { &attack, &hold, &decay }, {} };
    sectionFilter = { "Filter",     true,  { &filterType, &cutoff, &resonance, &drive }, {} };
    sectionEnv    = { "Filter Env", true,  { &envAttack, &envDecay, &envAmount }, {} };
    sectionRouting = { "Routing",   true,  { &note, &choke, &output }, {} };
    sectionMix    = { "Mix",        true,  { &level, &pan, &sendReverb, &sendDelay, &sendPhaser }, {} };
    Section* fxSections[] { &sectionReverb, &sectionDelay, &sectionMod };

    for (int slot = 0; slot < KitParams::Fx::numSlots; ++slot)
    {
        auto& k = fx[(size_t) slot];
        *fxSections[slot] = { KitParams::Fx::titles[slot], false, { &k.type, &k.a, &k.b, &k.level }, {} };
    }

    for (auto* section : { &sectionSample, &sectionAmp, &sectionFilter, &sectionEnv, &sectionRouting, &sectionMix,
                           &sectionReverb, &sectionDelay, &sectionMod })
        for (auto* knob : section->knobs)
            addAndMakeVisible (*knob);

    addAndMakeVisible (humanize);
    addAndMakeVisible (master);

    auto& state = kitbox.parameters;
    humanize.attach (state, KitParams::humanize);
    master.attach (state, KitParams::masterLevel);
    for (int slot = 0; slot < KitParams::Fx::numSlots; ++slot)
    {
        const auto& ids = KitParams::Fx::ids[slot];
        auto& k = fx[(size_t) slot];

        k.type.attach (state, ids.type);
        k.a.attach (state, ids.a);
        k.b.attach (state, ids.b);
        k.level.attach (state, ids.level);

        // Set after attach(): the attachment installs its own text functions,
        // which only know "0..1". These print what the effect loaded means by
        // it - Hz, ms, bits - and the delay's time as the note value it plays.
        k.a.textFromValueFunction = [this, slot] (double v)
        {
            const auto unit = slot == KitParams::Fx::delay ? EffectCatalog::Unit::Division : fxInfo (slot).unitA;
            return EffectCatalog::format (unit, (float) v);
        };

        k.b.textFromValueFunction = [this, slot] (double v)
        {
            return EffectCatalog::format (fxInfo (slot).unitB, (float) v);
        };
    }

    // The delay always follows the tempo, so its time knob steps through the
    // fourteen note values instead of sliding between them.
    fx[KitParams::Fx::delay].a.setRange (0.0, 1.0, 1.0 / (Dsp::Sync::numDivisions - 1));

    refreshFxLabels();

    addAndMakeVisible (display);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (learnButton);
    loadButton.onClick = [this] { loadKit(); };
    saveButton.onClick = [this] { saveKit(); };
    learnButton.onClick = [this]
    {
        if (kitbox.getLearnPad() >= 0)
            kitbox.cancelLearn();
        else
            kitbox.startLearn (selectedPad);
    };

    for (int pad = 0; pad < KitParams::numPads; ++pad)
    {
        auto button = std::make_unique<PadButton> (kitbox, pad);
        button->onSelect = [this] (int index) { selectPad (index); };
        addAndMakeVisible (*button);
        pads.push_back (std::move (button));
    }

    kitbox.addChangeListener (this);

    setSize (editorWidth, editorHeight);
    selectPad (kitbox.getSelectedPad());

    startTimerHz (30);
}

KitboxEditor::~KitboxEditor()
{
    stopTimer();
    kitbox.removeChangeListener (this);
}

//==============================================================================
void KitboxEditor::selectPad (int pad)
{
    selectedPad = juce::jlimit (0, KitParams::numPads - 1, pad);
    kitbox.setSelectedPad (selectedPad);

    // Learn follows the selection: pick the pad, then play the key.
    if (kitbox.getLearnPad() >= 0 && kitbox.getLearnPad() != selectedPad)
        kitbox.startLearn (selectedPad);

    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSelected (i == selectedPad);

    display.setPad (selectedPad);
    bindPadKnobs();
    repaint();
}

void KitboxEditor::bindPadKnobs()
{
    namespace P = KitParams::Pad;
    auto& state = kitbox.parameters;
    const auto id = [this] (const char* suffix) { return KitParams::padId (selectedPad, suffix); };

    tune.attach (state, id (P::tune));
    fine.attach (state, id (P::fine));
    start.attach (state, id (P::start));
    velocity.attach (state, id (P::velocity));
    attack.attach (state, id (P::attack));
    hold.attach (state, id (P::hold));
    decay.attach (state, id (P::decay));
    filterType.attach (state, id (P::filterType));
    cutoff.attach (state, id (P::cutoff));
    resonance.attach (state, id (P::resonance));
    drive.attach (state, id (P::drive));
    envAttack.attach (state, id (P::envAttack));
    envDecay.attach (state, id (P::envDecay));
    envAmount.attach (state, id (P::envAmount));
    level.attach (state, id (P::level));
    pan.attach (state, id (P::pan));
    sendReverb.attach (state, id (P::sendReverb));
    sendDelay.attach (state, id (P::sendDelay));
    sendPhaser.attach (state, id (P::sendPhaser));
    note.attach (state, id (P::note));
    choke.attach (state, id (P::choke));
    output.attach (state, id (P::output));
}

const EffectCatalog::Info& KitboxEditor::fxInfo (int slot) const
{
    const auto category = KitParams::Fx::categories[slot];
    const auto type = (int) std::lround (kitbox.parameters.getRawParameterValue (KitParams::Fx::ids[slot].type)->load());
    return EffectCatalog::info (category, EffectCatalog::clampSubtype (category, type));
}

void KitboxEditor::refreshFxLabels()
{
    for (int slot = 0; slot < KitParams::Fx::numSlots; ++slot)
    {
        auto& k = fx[(size_t) slot];
        const auto type = (int) std::lround (kitbox.parameters.getRawParameterValue (KitParams::Fx::ids[slot].type)->load());

        if (type == k.shownType)
            continue;

        // A type change - from the knob, automation or a loaded kit - renames
        // the two effect knobs and reprints their values in the new units.
        k.shownType = type;
        const auto& info = fxInfo (slot);
        k.a.setLabel (info.labelA);
        k.b.setLabel (info.labelB);
    }
}

void KitboxEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A loaded kit may have brought its own selected pad with it.
    if (kitbox.getSelectedPad() != selectedPad)
        selectPad (kitbox.getSelectedPad());

    for (auto& pad : pads)
        pad->repaint();

    display.setPad (selectedPad);
}

void KitboxEditor::timerCallback()
{
    for (auto& pad : pads)
        pad->updateFlash();

    display.refresh();
    refreshFxLabels();

    const auto learning = kitbox.getLearnPad() >= 0;
    if (learnButton.active != learning)
    {
        learnButton.active = learning;
        learnButton.repaint();
    }
}

bool KitboxEditor::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& details,
                                                         juce::StringArray& files, bool& canMoveFiles)
{
    int pad = -1;

    if (! PadButton::parseDragDescription (details.description, pad))
        return false;

    const auto file = kitbox.writeSampleForDrag (pad);

    if (! file.existsAsFile())
        return false;

    files.add (file.getFullPathName());
    canMoveFiles = false;
    return true;
}

//==============================================================================
/** Where kits are saved and loaded by default: /Library/Audio/Presets/Kitbox,
    beside the system-wide presets of other plugins (his choice, 2026-09-28).

    That folder belongs to root, so the plugin cannot create it; it is made
    once by hand (see the README). Until it exists and is writable, the dialogs
    open in Logic's own settings folder for Kitbox instead, which is the user's
    and always writable - a Save Kit must never fail because of a default. */
static juce::File presetFolder()
{
    const juce::File system ("/Library/Audio/Presets/" JucePlugin_Name);

    if (system.isDirectory() && system.hasWriteAccess())
        return system;

    const auto fallback = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                              .getChildFile ("Audio Music Apps/Plug-In Settings/" JucePlugin_Name);
    fallback.createDirectory();
    return fallback;
}

void KitboxEditor::loadKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Kit", presetFolder(),
                                                   juce::String ("*") + AuPreset::extension + ";*" + KitFile::extension);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<KitboxEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        const auto result = safe->kitbox.loadKit (fc.getResult());

        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Load Kit", result.getErrorMessage());
    });
}

void KitboxEditor::saveKit()
{
    // The name field gets a plain name, never one with a dot in it: the macOS
    // save panel selects only the part before the last dot, so a suggested
    // "Kit.kitbox" would survive every rename as "<new name>.kitbox".
    const auto current = kitbox.getKitName();
    const auto suggested = current.isNotEmpty() ? juce::File::createLegalFileName (current) : juce::String ("Kit");

    // .aupreset unless the name is given .kitbox: then the bare container, a
    // ZIP of the kit's samples and synths (see KitFile.h).
    chooser = std::make_unique<juce::FileChooser> ("Save Kit", presetFolder().getChildFile (suggested),
                                                   juce::String ("*") + AuPreset::extension + ";*" + KitFile::extension);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<KitboxEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        const auto chosen = fc.getResult();
        const auto file = chosen.hasFileExtension (KitFile::extension) ? chosen : chosen.withFileExtension (AuPreset::extension);
        const auto result = safe->kitbox.saveKit (file);

        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Save Kit", result.getErrorMessage());
    });
}

//==============================================================================
void KitboxEditor::layoutRow (std::vector<Section*> row, juce::Rectangle<int> area)
{
    int knobCount = 0;
    for (auto* section : row)
        knobCount += (int) section->knobs.size();

    const auto available = area.getWidth() - sectionGap * ((int) row.size() - 1)
                         - 2 * sectionPadding * (int) row.size();
    const auto cell = available / knobCount;

    for (size_t s = 0; s < row.size(); ++s)
    {
        auto* section = row[s];
        const auto width = s + 1 == row.size() ? area.getWidth()
                                               : cell * (int) section->knobs.size() + 2 * sectionPadding;

        section->bounds = area.removeFromLeft (width);
        area.removeFromLeft (sectionGap);

        auto knobs = section->bounds.reduced (sectionPadding, 0);
        knobs.removeFromTop (titleHeight);
        knobs.removeFromBottom (6);

        const auto knobWidth = knobs.getWidth() / (int) section->knobs.size();

        for (auto* knob : section->knobs)
            knob->setBounds (knobs.removeFromLeft (knobWidth));
    }
}

void KitboxEditor::resized()
{
    auto area = getLocalBounds().reduced (margin, 0);
    area.removeFromTop (margin);

    // Header
    auto header = area.removeFromTop (86);
    wordmarkArea = header.removeFromLeft (180);
    header.removeFromLeft (10);

    globalArea = header.removeFromRight (196);
    header.removeFromRight (12);

    auto buttons = header.removeFromRight (104);
    header.removeFromRight (12);
    display.setBounds (header);

    loadButton.setBounds (buttons.removeFromTop (26));
    buttons.removeFromTop (4);
    saveButton.setBounds (buttons.removeFromTop (26));
    buttons.removeFromTop (4);
    learnButton.setBounds (buttons.removeFromTop (26));

    {
        auto knobs = globalArea.reduced (sectionPadding, 0);
        knobs.removeFromTop (titleHeight);
        knobs.removeFromBottom (4);
        humanize.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
        master.setBounds (knobs);
    }

    area.removeFromTop (12);
    layoutRow ({ &sectionSample, &sectionAmp, &sectionFilter, &sectionEnv, &sectionRouting }, area.removeFromTop (104));
    area.removeFromTop (10);
    layoutRow ({ &sectionMix, &sectionReverb, &sectionDelay, &sectionMod }, area.removeFromTop (104));
    area.removeFromTop (16);

    // Pads: two rows of eight, 1-8 on top as in the sketch.
    constexpr int padGap = 8;
    constexpr int padHeight = 78;
    const auto padWidth = (area.getWidth() - 7 * padGap) / 8;

    for (int row = 0; row < 2; ++row)
    {
        auto line = area.removeFromTop (padHeight);

        for (int column = 0; column < 8; ++column)
        {
            const auto width = column == 7 ? line.getWidth() : padWidth;
            pads[(size_t) (row * 8 + column)]->setBounds (line.removeFromLeft (width));
            line.removeFromLeft (padGap);
        }

        area.removeFromTop (padGap);
    }

    footerArea = area.withTrimmedTop (2);
}

void KitboxEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::body);

    // Wordmark: the two-tone name from the reference, orange then ink.
    {
        auto area = wordmarkArea.toFloat();
        const auto font = juce::Font (juce::FontOptions (30.0f, juce::Font::bold)).withExtraKerningFactor (0.16f);
        g.setFont (font);

        const auto kitWidth = juce::GlyphArrangement::getStringWidth (font, "KIT");
        auto nameRow = area.removeFromTop (52.0f).withTrimmedTop (10.0f);

        g.setColour (Palette::accent);
        g.drawText ("KIT", nameRow, juce::Justification::centredLeft);
        g.setColour (Palette::ink);
        g.drawText ("BOX", nameRow.withTrimmedLeft (kitWidth + 1.0f), juce::Justification::centredLeft);

        g.setColour (Palette::inkDim);
        g.setFont (Palette::label (9.5f));
        g.drawText ("16-PAD DRUM SAMPLER", area.removeFromTop (14.0f), juce::Justification::centredLeft);
    }

    const auto padColour = Palette::padColour (selectedPad);
    const auto padLabel  = "PAD " + juce::String (selectedPad + 1).paddedLeft ('0', 2);

    const auto drawSection = [&] (juce::Rectangle<int> bounds, const juce::String& title, bool perPad)
    {
        g.setColour (Palette::section);
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

        auto titleRow = bounds.reduced (sectionPadding + 2, 0).removeFromTop (titleHeight).toFloat().withTrimmedTop (3.0f);

        g.setColour (Palette::ink);
        g.setFont (Palette::label (10.0f));
        g.drawText (title.toUpperCase(), titleRow, juce::Justification::centredLeft);

        if (perPad)
        {
            g.setColour (Palette::inkDim);
            g.setFont (Palette::mono (10.0f, true));
            g.drawText (padLabel, titleRow, juce::Justification::centredRight);

            const auto labelWidth = juce::GlyphArrangement::getStringWidth (Palette::mono (10.0f, true), padLabel);
            g.setColour (padColour);
            g.fillEllipse (titleRow.getRight() - labelWidth - 11.0f, titleRow.getCentreY() - 3.0f, 6.0f, 6.0f);
        }
    };

    drawSection (globalArea, "Global", false);

    for (auto* section : { &sectionSample, &sectionAmp, &sectionFilter, &sectionEnv, &sectionRouting, &sectionMix,
                           &sectionReverb, &sectionDelay, &sectionMod })
        drawSection (section->bounds, section->title, section->perPad);

    g.setColour (Palette::inkDim);
    g.setFont (Palette::label (9.0f));
    g.drawText (utf8 ("KITBOX " JucePlugin_VersionString "   \xc2\xb7   LEARN NOTE: PICK A PAD, PLAY A KEY"
                      "   \xc2\xb7   DROP FILES ON A PAD   \xc2\xb7   DRAG PAD ONTO PAD TO SWAP, OUT OF THE WINDOW TO EXPORT"),
                footerArea, juce::Justification::topLeft);
}
