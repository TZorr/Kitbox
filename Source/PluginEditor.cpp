//
//  PluginEditor.cpp
//  Kitbox
//

#include "PluginEditor.h"
#include "UI/Palette.h"
#include "TextUtf8.h"

namespace
{
    /** The panel's theme and the window's size are the user's, not the kit's:
        they live in ~/Library/Application Support/Kitbox/Kitbox.settings,
        shared by every instance and every project, and never travel with a
        kit or session. */
    std::unique_ptr<juce::PropertiesFile> openSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = JucePlugin_Name;
        options.filenameSuffix      = ".settings";
        options.folderName          = JucePlugin_Name;
        options.osxLibrarySubFolder = "Application Support";
        options.getDefaultFile().getParentDirectory().createDirectory();
        return std::make_unique<juce::PropertiesFile> (options);
    }

    constexpr const char* darkModeKey = "darkMode";
    constexpr const char* scaleKey    = "windowScale";

    // The mockup's measures.
    constexpr int margin = 24;          // around the body, the FX bus, the header's ends
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 36;
    constexpr int padColumn = 640;
    constexpr int columnGap = 20;
    constexpr int titleRow = 20;        // PADS, PAD SETTINGS, FX BUS
    constexpr int titleGap = 12;
    constexpr int padHeight = 118;
    constexpr int padGap = 6;
    constexpr int displayHeight = 210;
    constexpr int cardHeight = 128;
    constexpr int cardGap = 12;

    const juce::String moonIcon = "M13.2 9.6A5.6 5.6 0 1 1 6.4 2.8a4.5 4.5 0 0 0 6.8 6.8z";
    const juce::String sunIcon  = "M11 8a3 3 0 1 1-6 0a3 3 0 1 1 6 0M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6"
                                  "M3.4 3.4l1.1 1.1M11.5 11.5l1.1 1.1M3.4 12.6l1.1-1.1M11.5 4.5l1.1-1.1";

    /** The meter's scale: -48 dB at the bottom, +3 at the top. */
    float meterFraction (float gain)
    {
        const auto db = juce::Decibels::gainToDecibels (gain, -100.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -48.0f, 3.0f, 0.0f, 1.0f));
    }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }
}

//==============================================================================
KitboxEditor::KitboxEditor (KitboxProcessor& p)
    : AudioProcessorEditor (&p), kitbox (p), display (p), settings (openSettings())
{
    addAndMakeVisible (panel);

    sectionSample  = { "Sample",     {},  4, 4.0f, { &tune, &fine, &start, &velocity }, {} };
    sectionFilter  = { "Filter",     {},  4, 4.0f, { &filterType, &cutoff, &resonance, &drive }, {} };
    sectionEnv     = { "Filter Env", {},  3, 3.0f, { &envAttack, &envDecay, &envAmount }, {} };
    sectionAmp     = { "Amp",        {},  3, 3.0f, { &attack, &hold, &decay }, {} };
    sectionMix     = { "Mix", utf8 ("Sends \xe2\x86\x92 FX bus"), 5, 5.0f, { &level, &pan, &sendReverb, &sendDelay, &sendPhaser }, {} };
    sectionRouting = { "Routing",    {},  3, 3.4f, { &note, &choke, &output }, {} };
    Section* fxSections[] { &sectionReverb, &sectionDelay, &sectionMod };

    for (int slot = 0; slot < KitParams::Fx::numSlots; ++slot)
    {
        auto& k = fx[(size_t) slot];
        *fxSections[slot] = { KitParams::Fx::titles[slot], {}, 4, 1.0f, { &k.type, &k.a, &k.b, &k.level }, {} };
        fxSections[slot]->fx = true;
        panel.addAndMakeVisible (k.power);
    }

    for (auto* section : { &sectionSample, &sectionAmp, &sectionFilter, &sectionEnv, &sectionRouting, &sectionMix,
                           &sectionReverb, &sectionDelay, &sectionMod })
        for (auto* control : section->controls)
            panel.addAndMakeVisible (*control);

    // The colour zones: what a card works on, said by its colour.
    {
        using Z = Palette::ZoneId;
        sectionSample.zone  = Z::source;
        sectionAmp.zone     = Z::source;
        sectionFilter.zone  = Z::filter;
        sectionEnv.zone     = Z::filter;
        sectionMix.zone     = Z::mix;
        sectionRouting.zone = Z::routing;
        sectionReverb.zone  = Z::reverb;
        sectionDelay.zone   = Z::delay;
        sectionMod.zone     = Z::mod;

        for (auto* section : { &sectionSample, &sectionAmp, &sectionFilter, &sectionEnv, &sectionRouting, &sectionMix,
                               &sectionReverb, &sectionDelay, &sectionMod })
        {
            for (auto* control : section->controls)
            {
                if (auto* knob = dynamic_cast<Knob*> (control))
                    knob->setZone (section->zone, false, section->fx);
                else if (auto* box = dynamic_cast<ChoiceBox*> (control))
                    box->setZone (section->zone, section->fx);
            }
        }

        // Each send in the colour of the effect it feeds.
        sendReverb.setZone (Z::reverb, true, true);
        sendDelay.setZone (Z::delay, true, true);
        sendPhaser.setZone (Z::mod, true, true);

        // The header's two knobs act on everything: neutral, not orange.
        humanize.setZone (Z::neutral);
        master.setZone (Z::neutral);
    }

    panel.addAndMakeVisible (humanize);
    panel.addAndMakeVisible (master);

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
        k.power.attach (state, KitParams::Fx::onIds[slot]);
        k.power.setTooltip (juce::String (KitParams::Fx::titles[slot]) + " return on / off");

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

    // The note box reads "C1 . 36", as the mockup has it.
    note.textForValue = [] (int value) { return KitParams::noteName (value) + utf8 (" \xc2\xb7 ") + juce::String (value); };

    refreshFxLabels();

    // The saved theme first, so the first paint is already the right one.
    // Dark is the mockup's, and so the default.
    Palette::setDark (settings->getBoolValue (darkModeKey, true));
    shownDark = Palette::dark;
    themeButton.onClick = [this] { setDarkMode (! Palette::dark); };
    refreshThemeIcon();
    panel.addAndMakeVisible (themeButton);

    panel.addAndMakeVisible (display);
    display.onToggleSynth = [this] { pads[(size_t) selectedPad]->toggleSource(); };

    for (auto* button : { &previousKit, &nextKit, &kitField, &loadButton, &saveButton, &learnButton })
        panel.addAndMakeVisible (*button);

    previousKit.setTooltip ("Previous kit in the presets folder");
    nextKit.setTooltip ("Next kit in the presets folder");
    kitField.setTooltip ("The kits in the presets folder");
    previousKit.onClick = [this] { stepKit (-1); };
    nextKit.onClick = [this] { stepKit (1); };
    kitField.onClick = [this] { showKitMenu(); };
    loadButton.onClick = [this] { loadKit(); };
    saveButton.onClick = [this] { saveKit(); };
    learnButton.setTooltip ("Learn: pick a pad, play a key");
    learnButton.onClick = [this]
    {
        if (kitbox.getLearnPad() >= 0)
            kitbox.cancelLearn();
        else
            kitbox.startLearn (selectedPad);
    };

    synAll.setTooltip ("Every pad holding a sample plays Transmute's synth of it (Auto)");
    synAll.onClick = [this] { kitbox.transmuteAll(); };
    panel.addAndMakeVisible (synAll);

    for (int pad = 0; pad < KitParams::numPads; ++pad)
    {
        auto button = std::make_unique<PadButton> (kitbox, pad);
        button->onSelect = [this] (int index) { selectPad (index); };
        panel.addAndMakeVisible (*button);
        pads.push_back (std::move (button));
    }

    refreshKitName();
    kitbox.addChangeListener (this);

    panel.setSize (panelWidth, panelHeight);
    layoutPanel();

    // Any size at the panel's proportions; 75 % the first time.
    const auto scale = juce::jlimit (minScale, maxScale, settings->getDoubleValue (scaleKey, defaultScale));
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (panelWidth * minScale), juce::roundToInt (panelHeight * minScale),
                     juce::roundToInt (panelWidth * maxScale), juce::roundToInt (panelHeight * maxScale));
    getConstrainer()->setFixedAspectRatio ((double) panelWidth / (double) panelHeight);
    setSize (juce::roundToInt (panelWidth * scale), juce::roundToInt (panelHeight * scale));

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
    panel.repaint();
}

void KitboxEditor::setDarkMode (bool shouldBeDark)
{
    Palette::setDark (shouldBeDark);
    settings->setValue (darkModeKey, shouldBeDark);
    settings->saveIfNeeded();

    shownDark = shouldBeDark;
    refreshThemeIcon();
    panel.repaint();
}

void KitboxEditor::refreshThemeIcon()
{
    themeButton.setIcon (Palette::dark ? moonIcon : sunIcon);
    themeButton.setTooltip (Palette::dark ? "Switch to the light panel" : "Switch to the dark panel");
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

        // A type change - from the box, automation or a loaded kit - renames
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

    refreshKitName();
    display.refresh();
    panel.repaint (settingsTitleArea);
}

void KitboxEditor::timerCallback()
{
    for (auto& pad : pads)
        pad->updateFlash();

    display.refresh();
    refreshFxLabels();

    // Another Kitbox window may have switched the theme: it is process-wide.
    if (shownDark != Palette::dark)
    {
        shownDark = Palette::dark;
        refreshThemeIcon();
        panel.repaint();
    }

    const auto learning = kitbox.getLearnPad() >= 0;
    if (learnButton.active != learning)
    {
        learnButton.active = learning;
        learnButton.repaint();
    }

    // SYN ALL: its count while it runs, dim when there is nothing to model.
    {
        const auto progress = kitbox.getSynProgress();
        const auto text = progress.total > 0 ? "SYN " + juce::String (progress.done) + "/" + juce::String (progress.total)
                                             : juce::String ("SYN ALL");
        bool anything = false;
        for (int p = 0; p < KitParams::numPads && ! anything; ++p)
            anything = kitbox.canTransmute (p);

        if (synAll.getButtonText() != text)
            synAll.setButtonText (text);
        if (synAll.isEnabled() != anything)
            synAll.setEnabled (anything);
    }

    // The meter: peaks held since the last tick, falling back at about 20 dB a second.
    {
        bool moved = false;
        for (int c = 0; c < 2; ++c)
        {
            const auto peak = kitbox.takeMeterPeak (c);
            const auto next = juce::jmax (peak, meterLevels[(size_t) c] * 0.86f);
            const auto shown = next < 1.0e-4f ? 0.0f : next;
            moved = moved || std::abs (meterFraction (shown) - meterFraction (meterLevels[(size_t) c])) > 0.002f;
            meterLevels[(size_t) c] = shown;
        }
        if (moved)
            panel.repaint (metersArea);
    }

    // The status bar: the MIDI light, lit for a moment by each note, and the voices.
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto count = kitbox.getMidiNoteCount();
        const auto voices = kitbox.getEngine().getSoundingVoices();
        const auto wasLit = now < midiLitUntil;

        if (count != shownMidiCount)
        {
            shownMidiCount = count;
            midiLitUntil = now + 150.0;
        }

        if (voices != shownVoices || wasLit != (now < midiLitUntil))
        {
            shownVoices = voices;
            panel.repaint (footerArea);
        }
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
/** Where kits are saved and loaded by default:
    ~/Library/Audio/Presets/T'Zorr/Kitbox, the user's own presets folder,
    sorted by maker like other plugins do (his choice, 2026-10-28).

    It lives in the user's Library, so the plugin creates it itself. Should
    that ever fail, the dialogs open in Logic's own settings folder for Kitbox
    instead, which is always writable - a Save Kit must never fail because of
    a default. */
static juce::File presetFolder()
{
    const auto user = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                          .getChildFile ("Library/Audio/Presets")
                          .getChildFile (JucePlugin_Manufacturer)
                          .getChildFile (JucePlugin_Name);
    user.createDirectory();

    if (user.isDirectory() && user.hasWriteAccess())
        return user;

    const auto fallback = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                              .getChildFile ("Audio Music Apps/Plug-In Settings/" JucePlugin_Name);
    fallback.createDirectory();
    return fallback;
}

/** The kits directly in a folder, in the Finder's name order. */
static juce::Array<juce::File> kitsIn (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + KitFile::extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& x, const juce::File& y)
    {
        return x.getFileName().compareNatural (y.getFileName()) < 0;
    });
    return files;
}

/** The folders directly in a folder, in the Finder's name order, hidden ones left out. */
static juce::Array<juce::File> foldersIn (const juce::File& folder)
{
    auto folders = folder.findChildFiles (juce::File::findDirectories | juce::File::ignoreHiddenFiles, false);
    std::sort (folders.begin(), folders.end(), [] (const juce::File& x, const juce::File& y)
    {
        return x.getFileName().compareNatural (y.getFileName()) < 0;
    });
    return folders;
}

juce::Array<juce::File> KitboxEditor::kitFiles() const
{
    // Every kit in the presets folder and the folders inside it, in the
    // Finder's name order, folders included in the name ("Live/Kit 2").
    const auto folder = presetFolder();
    auto files = folder.findChildFiles (juce::File::findFiles, true, juce::String ("*") + KitFile::extension);

    std::sort (files.begin(), files.end(), [&folder] (const juce::File& x, const juce::File& y)
    {
        return x.getRelativePathFrom (folder).compareNatural (y.getRelativePathFrom (folder)) < 0;
    });

    return files;
}

juce::File KitboxEditor::currentKit() const
{
    // The file the kit came from; for a session from before 0.9.0, which
    // kept only the name, the first kit of that name in the presets.
    if (const auto file = kitbox.getKitFile(); file.existsAsFile())
        return file;

    const auto name = kitbox.getKitName();
    for (const auto& file : kitFiles())
        if (file.getFileNameWithoutExtension() == name)
            return file;

    return {};
}

juce::File KitboxEditor::neighbourKit (const juce::File& current, const juce::File& fallbackFolder, int delta)
{
    // Within the kit's own folder, round from its end to its start. A kit
    // not found there - none loaded, or moved away - starts at an end.
    auto folder = current.existsAsFile() ? current.getParentDirectory() : fallbackFolder;
    auto files = kitsIn (folder);

    // Nothing at the top of the presets: the first folder that holds kits.
    if (files.isEmpty() && ! current.existsAsFile())
        for (const auto& sub : foldersIn (fallbackFolder))
            if (files = kitsIn (sub); ! files.isEmpty())
                break;

    if (files.isEmpty())
        return {};

    const auto index = files.indexOf (current);
    const auto next = index < 0 ? (delta > 0 ? 0 : files.size() - 1)
                                : ((index + delta) % files.size() + files.size()) % files.size();
    return files[next];
}

void KitboxEditor::refreshKitName()
{
    // "808 > 808 Clean Kit 01": the folder the arrows step through, then the kit.
    const auto name = kitbox.getKitName();
    auto text = name.isNotEmpty() ? name : juce::String ("Untitled Kit");

    const auto file = kitbox.getKitFile();
    const auto presets = presetFolder();

    if (name.isNotEmpty() && file.isAChildOf (presets) && file.getParentDirectory() != presets)
    {
        const auto folders = juce::StringArray::fromTokens (file.getParentDirectory().getRelativePathFrom (presets), "/", {});
        text = folders.joinIntoString (utf8 (" \xe2\x80\xba ")) + utf8 (" \xe2\x80\xba ") + name;
    }

    if (kitField.getButtonText() != text)
        kitField.setButtonText (text);
}

void KitboxEditor::loadKitFile (const juce::File& file)
{
    const auto result = kitbox.loadKit (file);

    if (result.failed())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load Kit", result.getErrorMessage());
}

void KitboxEditor::stepKit (int delta)
{
    const auto next = neighbourKit (currentKit(), presetFolder(), delta);

    if (next.existsAsFile())
        loadKitFile (next);
    else
        showKitMenu();
}

void KitboxEditor::showKitMenu()
{
    const auto presets = presetFolder();
    const auto current = currentKit();
    juce::Array<juce::File> files;   // item id - 1 -> kit

    // The presets folder as it is on disk: a submenu per folder, the kits in
    // it below them; the loaded kit ticked, and the folders on its way.
    std::function<bool (juce::PopupMenu&, const juce::File&)> addFolder;
    addFolder = [&] (juce::PopupMenu& menu, const juce::File& folder)
    {
        bool any = false;

        for (const auto& sub : foldersIn (folder))
        {
            juce::PopupMenu inner;
            if (addFolder (inner, sub))
            {
                menu.addSubMenu (sub.getFileName(), inner, true, nullptr, current.isAChildOf (sub));
                any = true;
            }
        }

        for (const auto& kit : kitsIn (folder))
        {
            files.add (kit);
            menu.addItem (files.size(), kit.getFileNameWithoutExtension(), true, kit == current);
            any = true;
        }

        return any;
    };

    juce::PopupMenu menu;

    if (! addFolder (menu, presets))
        menu.addItem (-1, "No kits in " + presets.getFullPathName(), false);

    constexpr int loadItem = 100000, revealItem = 100001;
    menu.addSeparator();
    menu.addItem (loadItem, utf8 ("Load Kit\xe2\x80\xa6"));
    menu.addItem (revealItem, "Show Presets Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitField).withMinimumWidth (kitField.getWidth()),
                        [safe = juce::Component::SafePointer<KitboxEditor> (this), files, presets] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == loadItem)
            safe->loadKit();
        else if (result == revealItem)
            presets.revealToUser();
        else if (result > 0 && result <= files.size())
            safe->loadKitFile (files[result - 1]);
    });
}

void KitboxEditor::loadKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Kit", presetFolder(),
                                                   juce::String ("*") + KitFile::extension);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<KitboxEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult() != juce::File())
            safe->loadKitFile (fc.getResult());
    });
}

void KitboxEditor::saveKit()
{
    // The name field gets a plain name, never one with a dot in it: the macOS
    // save panel selects only the part before the last dot, so a suggested
    // "Kit.kitbox" would survive every rename as "<new name>.kitbox".
    const auto current = kitbox.getKitName();
    const auto suggested = current.isNotEmpty() ? juce::File::createLegalFileName (current) : juce::String ("Kit");

    // A .kitbox: a ZIP of the kit's samples and synths (see KitFile.h).
    chooser = std::make_unique<juce::FileChooser> ("Save Kit", presetFolder().getChildFile (suggested),
                                                   juce::String ("*") + KitFile::extension);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<KitboxEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        const auto file = fc.getResult().withFileExtension (KitFile::extension);
        const auto result = safe->kitbox.saveKit (file);

        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Save Kit", result.getErrorMessage());
    });
}

//==============================================================================
void KitboxEditor::paint (juce::Graphics& g)
{
    // Only where the scaled panel leaves a sliver at the edge.
    g.fillAll (Palette::body);
}

void KitboxEditor::resized()
{
    const auto scale = (float) getWidth() / (float) panelWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));

    // Remembered once the window is up: the constructor's own setSize is the
    // saved size, not a choice.
    if (isTimerRunning() && std::abs (settings->getDoubleValue (scaleKey, defaultScale) - scale) > 0.001)
        settings->setValue (scaleKey, (double) scale);
}

void KitboxEditor::layoutRow (std::vector<Section*> row, juce::Rectangle<int> area)
{
    float flexTotal = 0.0f;
    for (auto* section : row)
        flexTotal += section->flex;

    const auto available = (float) (area.getWidth() - cardGap * ((int) row.size() - 1));

    for (size_t s = 0; s < row.size(); ++s)
    {
        auto* section = row[s];
        const auto width = s + 1 == row.size() ? area.getWidth()
                                               : juce::roundToInt (available * section->flex / flexTotal);

        section->bounds = area.removeFromLeft (width);
        area.removeFromLeft (cardGap);

        // The zone's 2-point line and 13 in, the title row, 12, then the
        // controls on a grid with 6 between.
        auto grid = section->bounds.withTrimmedLeft (14).withTrimmedRight (14).withTrimmedTop (15).withTrimmedBottom (11);
        grid.removeFromTop (16 + 12);

        const auto columns = section->columns;
        const auto cell = (float) (grid.getWidth() - padGap * (columns - 1)) / (float) columns;

        for (int c = 0; c < (int) section->controls.size(); ++c)
        {
            const auto x = grid.getX() + juce::roundToInt ((cell + (float) padGap) * (float) c);
            section->controls[(size_t) c]->setBounds (x, grid.getY(), juce::roundToInt (cell), Knob::stackedHeight);
        }
    }
}

void KitboxEditor::layoutPanel()
{
    auto area = juce::Rectangle<int> (0, 0, panelWidth, panelHeight);

    // Header: from the left, the wordmark, the kit, Load / Save / Learn;
    // from the right, the theme, Master and its meter, Humanize.
    headerArea = area.removeFromTop (headerHeight);
    {
        auto row = headerArea.reduced (margin, 0);
        const auto buttonRow = [] (juce::Rectangle<int> r) { return r.withSizeKeepingCentre (r.getWidth(), 36); };

        const auto wordmark = textWidth (Palette::font (22.0f, true, 0.16f), "KITBOX");
        const auto subtitle = textWidth (Palette::label (12.0f), "16-PAD DRUM SAMPLER");
        wordmarkArea = row.removeFromLeft (wordmark + 14 + subtitle);
        row.removeFromLeft (20);
        headerDividers[0] = row.removeFromLeft (1).withSizeKeepingCentre (1, 28);
        row.removeFromLeft (20);

        previousKit.setBounds (buttonRow (row.removeFromLeft (36)));
        row.removeFromLeft (6);
        nextKit.setBounds (buttonRow (row.removeFromLeft (36)));
        row.removeFromLeft (6);
        kitField.setBounds (buttonRow (row.removeFromLeft (320)));
        row.removeFromLeft (20);

        for (auto* button : { &loadButton, &saveButton, &learnButton })
        {
            const auto width = 14 + 16 + 8 + textWidth (Palette::label (12.0f), button->getButtonText().toUpperCase()) + 14;
            button->setBounds (buttonRow (row.removeFromLeft (width)));
            row.removeFromLeft (6);
        }

        themeButton.setBounds (buttonRow (row.removeFromRight (36)));
        row.removeFromRight (20);
        headerDividers[1] = row.removeFromRight (1).withSizeKeepingCentre (1, 28);
        row.removeFromRight (20);

        metersArea = row.removeFromRight (13).withSizeKeepingCentre (13, 34);
        row.removeFromRight (10);
        master.setBounds (row.removeFromRight (38 + 10 + 64).withSizeKeepingCentre (38 + 10 + 64, 38));
        row.removeFromRight (20);
        humanize.setBounds (row.removeFromRight (38 + 10 + 70).withSizeKeepingCentre (38 + 10 + 70, 38));
    }

    footerArea = area.removeFromBottom (footerHeight);

    // Body: the pads on the left, the pad editor on the right.
    auto body = area.withTrimmedTop (margin).reduced (margin, 0);
    auto padsColumn = body.removeFromLeft (padColumn);
    body.removeFromLeft (columnGap);
    auto editor = body;

    padsTitleArea = padsColumn.removeFromTop (titleRow);
    synAll.setBounds (padsTitleArea.removeFromRight (84));
    padsTitleArea = padsTitleArea.withRight (synAll.getX());
    padsColumn.removeFromTop (titleGap);

    padWellArea = padsColumn.removeFromTop (4 * padHeight + 3 * padGap + 2 * padGap);
    {
        const auto grid = padWellArea.reduced (padGap);
        const auto width = (float) (grid.getWidth() - 3 * padGap) / 4.0f;

        // 1-4 along the top, as in the mockup.
        for (int pad = 0; pad < KitParams::numPads; ++pad)
        {
            const auto column = pad % 4, row = pad / 4;
            const auto x = grid.getX() + juce::roundToInt ((width + (float) padGap) * (float) column);
            const auto cell = juce::Rectangle<int> (x, grid.getY() + row * (padHeight + padGap), juce::roundToInt (width), padHeight);
            pads[(size_t) pad]->setBounds (cell.expanded (PadButton::ringMargin));
        }
    }

    display.setBounds (editor.removeFromTop (displayHeight));
    editor.removeFromTop (titleGap);
    settingsTitleArea = editor.removeFromTop (titleRow);
    editor.removeFromTop (titleGap);
    layoutRow ({ &sectionSample, &sectionFilter, &sectionEnv }, editor.removeFromTop (cardHeight));
    editor.removeFromTop (cardGap);
    layoutRow ({ &sectionAmp, &sectionMix, &sectionRouting }, editor.removeFromTop (cardHeight));

    // The FX bus, under both columns: a rack a step darker, 20 below the
    // pads, its title and cards 12 inside its edge.
    rackArea = juce::Rectangle<int> (margin, padWellArea.getBottom() + 20,
                                     panelWidth - 2 * margin, 2 * 13 + titleRow + titleGap + cardHeight);
    auto fxArea = rackArea.reduced (13);
    fxTitleArea = fxArea.removeFromTop (titleRow).reduced (2, 0);
    fxArea.removeFromTop (titleGap);
    layoutRow ({ &sectionReverb, &sectionDelay, &sectionMod }, fxArea);

    for (int slot = 0; slot < KitParams::Fx::numSlots; ++slot)
    {
        const Section* section[] { &sectionReverb, &sectionDelay, &sectionMod };
        const auto title = section[slot]->bounds.reduced (14, 0).withTrimmedTop (15).withHeight (16);
        fx[(size_t) slot].power.setBounds (title.withLeft (title.getRight() - 48).withSizeKeepingCentre (48, 20));
    }
}

//==============================================================================
void KitboxEditor::paintPanel (juce::Graphics& g)
{
    g.fillAll (Palette::body);

    const auto heading = [&g] (juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour (Palette::heading);
        g.setFont (Palette::label (12.0f));
        g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft);
        return textWidth (Palette::label (12.0f), text.toUpperCase());
    };

    const auto rule = [&g] (juce::Rectangle<int> area)
    {
        g.setColour (Palette::rule);
        g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1));
    };

    // Header.
    g.setColour (Palette::header);
    g.fillRect (headerArea);
    g.setColour (Palette::line);
    g.fillRect (headerArea.removeFromBottom (1));

    {
        auto area = wordmarkArea;
        const auto font = Palette::font (22.0f, true, 0.16f);
        const auto kitWidth = textWidth (font, "KIT");
        const auto boxWidth = textWidth (font, "KITBOX");
        g.setFont (font);
        g.setColour (Palette::accent);
        g.drawText ("KIT", area, juce::Justification::centredLeft);
        g.setColour (Palette::title);
        g.drawText ("BOX", area.withTrimmedLeft (kitWidth), juce::Justification::centredLeft);

        g.setColour (Palette::inkFaint);
        g.setFont (Palette::label (12.0f));
        g.drawText ("16-PAD DRUM SAMPLER", area.withTrimmedLeft (boxWidth + 14).translated (0, 2), juce::Justification::centredLeft);
    }

    g.setColour (Palette::divider);
    for (const auto& divider : headerDividers)
        g.fillRect (divider);

    // The meter beside Master: two bars, left and right.
    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> ((float) metersArea.getX() + (float) c * 8.0f, (float) metersArea.getY(),
                                                 5.0f, (float) metersArea.getHeight());
        g.setColour (Palette::meterTrack);
        g.fillRoundedRectangle (bar, 2.0f);

        const auto peak = meterLevels[(size_t) c];
        const auto fraction = meterFraction (peak);
        if (fraction > 0.0f)
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (bar, 2.0f);
            g.reduceClipRegion (clip);
            g.setColour (peak >= 1.0f ? Palette::warning : peak >= 0.7f ? Palette::accent : Palette::signal);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * fraction));
        }
    }

    // PADS  C1 - D#2
    {
        auto area = padsTitleArea;
        area.removeFromLeft (heading (area, "Pads") + 10);
        g.setColour (Palette::inkFaint);
        g.setFont (Palette::font (11.0f));
        g.drawText (KitParams::noteName (kitbox.getPadNote (0)) + utf8 (" \xe2\x80\x93 ")
                        + KitParams::noteName (kitbox.getPadNote (KitParams::numPads - 1)),
                    area, juce::Justification::centredLeft);
    }

    g.setColour (Palette::padWell);
    g.fillRoundedRectangle (padWellArea.toFloat(), 6.0f);

    // PAD SETTINGS  (o PAD 01 . name)  ----------  Click a pad to edit it
    {
        auto area = settingsTitleArea;
        area.removeFromLeft (heading (area, "Pad settings") + 12);

        const auto sample = kitbox.getPadSample (selectedPad);
        const auto pillText = "PAD " + juce::String (selectedPad + 1).paddedLeft ('0', 2) + utf8 (" \xc2\xb7 ")
                            + (sample != nullptr ? sample->getName() : juce::String ("Empty"));
        const auto pillFont = Palette::font (11.0f);
        const auto pill = area.removeFromLeft (juce::jmin (360, 8 + 8 + 8 + textWidth (pillFont, pillText) + 10)).toFloat();

        g.setColour (Palette::pill);
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (Palette::pillLine);
        g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
        g.setColour (Palette::padColour (selectedPad, kitbox.getPadColour (selectedPad)));
        g.fillEllipse (pill.getX() + 8.0f, pill.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (Palette::ink);
        g.setFont (pillFont);
        g.drawText (pillText, pill.withTrimmedLeft (24.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);

        const auto hint = juce::String ("Click a pad to edit it");
        const auto hintArea = area.removeFromRight (textWidth (Palette::font (11.0f), hint));
        g.setColour (Palette::inkFaint);
        g.drawText (hint, hintArea, juce::Justification::centredRight);
        rule (area.reduced (12, 0));
    }

    // The FX rack's frame.
    g.setColour (Palette::rack);
    g.fillRoundedRectangle (rackArea.toFloat(), 12.0f);
    g.setColour (Palette::rackLine);
    g.drawRoundedRectangle (rackArea.toFloat().reduced (0.5f), 12.0f, 1.0f);

    // FX BUS  Shared returns . fed by the Mix sends of every pad  ----------
    {
        auto area = fxTitleArea;
        area.removeFromLeft (heading (area, "FX Bus") + 12);
        const auto meta = utf8 ("Shared returns \xc2\xb7 fed by the Mix sends of every pad");
        g.setColour (Palette::inkFaint);
        g.setFont (Palette::font (11.0f));
        g.drawText (meta, area.removeFromLeft (textWidth (Palette::font (11.0f), meta)), juce::Justification::centredLeft);
        rule (area.withTrimmedLeft (12));
    }

    // The cards, each in its zone's colours: a faint tint, the edge, a
    // 2-point line along the top, and a dot before the title.
    for (auto* section : { &sectionSample, &sectionAmp, &sectionFilter, &sectionEnv, &sectionRouting, &sectionMix,
                           &sectionReverb, &sectionDelay, &sectionMod })
    {
        const auto zone = Palette::zone (section->zone, section->fx);
        const auto bounds = section->bounds.toFloat();
        constexpr float radius = 10.0f;

        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path outline;
            outline.addRoundedRectangle (bounds, radius);
            g.reduceClipRegion (outline);

            g.setColour (zone.border);
            g.fillRect (bounds);
            g.setColour (zone.line);
            g.fillRect (bounds.withHeight (2.0f));
            g.setColour (zone.tint);
            g.fillRoundedRectangle (bounds.withTrimmedLeft (1.0f).withTrimmedRight (1.0f)
                                          .withTrimmedTop (2.0f).withTrimmedBottom (1.0f), radius - 1.0f);
        }

        auto title = section->bounds.reduced (14, 0).withTrimmedTop (15).withHeight (16);

        if (section->meta.isNotEmpty())
        {
            g.setColour (Palette::inkFaint);
            g.setFont (Palette::font (11.0f));
            g.drawText (section->meta, title, juce::Justification::centredRight);
        }

        g.setColour (zone.line);
        g.fillEllipse (title.removeFromLeft (7).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
        title.removeFromLeft (8);
        g.setColour (zone.label);
        g.setFont (Palette::label (12.0f));
        g.drawText (section->title.toUpperCase(), title, juce::Justification::centredLeft);
    }

    // Status bar.
    {
        auto area = footerArea;
        g.setColour (Palette::footer);
        g.fillRect (area);
        g.setColour (Palette::line.interpolatedWith (Palette::footer, 0.2f));
        g.fillRect (area.removeFromTop (1));
        area = area.reduced (margin, 0);

        const auto font = Palette::font (11.0f, false, 0.06f);
        g.setFont (font);

        const auto version = juce::String ("KITBOX " JucePlugin_VersionString);
        g.setColour (Palette::inkSoft);
        g.drawText (version, area.removeFromLeft (textWidth (font, version)), juce::Justification::centredLeft);
        area.removeFromLeft (16);
        g.setColour (Palette::divider);
        g.fillRect (area.removeFromLeft (1).withSizeKeepingCentre (1, 14));
        area.removeFromLeft (16);

        // Right: the MIDI light and the voices sounding.
        const auto voices = "Voices " + juce::String (juce::jmax (0, shownVoices)) + " / " + juce::String (DrumEngine::numVoices);
        g.setColour (Palette::inkFaint);
        g.drawText (voices, area.removeFromRight (textWidth (font, voices) + 2), juce::Justification::centredRight);
        area.removeFromRight (16);

        const auto midiWidth = 6 + 6 + textWidth (font, "MIDI");
        auto midi = area.removeFromRight (midiWidth);
        const auto lit = juce::Time::getMillisecondCounterHiRes() < midiLitUntil;
        g.setColour (lit ? Palette::signal : Palette::padEmptyLine);
        g.fillEllipse (midi.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
        midi.removeFromLeft (6);
        g.setColour (Palette::inkFaint);
        g.drawText ("MIDI", midi, juce::Justification::centredLeft);
        area.removeFromRight (16);

        const auto hints = juce::StringArray { "Drop files on a pad", "Drag pad onto pad to swap",
                                               "Drag out of the window to export", "Learn: pick a pad, play a key",
                                               "Right-click a pad for more" };
        g.drawText (hints.joinIntoString (utf8 ("   \xc2\xb7   ")), area, juce::Justification::centredLeft, true);
    }
}
