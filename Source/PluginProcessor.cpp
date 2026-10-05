//
//  PluginProcessor.cpp
//  Kitbox
//

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/FxMigration.h"

//==============================================================================
//  Parameter text
//
//  Whole numbers go through roundToInt, never String (value, 0): juce::String's
//  zero-decimals constructor means "as many as it takes", and a knob would read
//  "1365.33 Hz".
namespace
{
    juce::String dbText (float v, int)
    {
        if (v <= KitParams::levelFloorDb)
            return "-inf dB";
        return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
    }

    juce::String panText (float v, int)
    {
        const auto amount = juce::roundToInt (std::abs (v) * 100.0f);
        if (amount == 0)
            return "C";
        return (v < 0.0f ? "L" : "R") + juce::String (amount);
    }

    juce::String semitoneText (float v, int)
    {
        const auto st = juce::roundToInt (v);
        return (st > 0 ? "+" : "") + juce::String (st) + " st";
    }

    juce::String centText (float v, int)
    {
        const auto ct = juce::roundToInt (v);
        return (ct > 0 ? "+" : "") + juce::String (ct) + " ct";
    }

    juce::String percentText (float v, int)
    {
        return juce::String (juce::roundToInt (v)) + " %";
    }

    juce::String startText (float v, int)
    {
        return juce::String (v, 1) + " %";
    }

    juce::String msText (float v, int)
    {
        if (v >= 1000.0f)
            return juce::String (v / 1000.0f, 2) + " s";
        if (v < 10.0f)
            return juce::String (v, 1) + " ms";
        return juce::String (juce::roundToInt (v)) + " ms";
    }

    juce::String decayText (float v, int maxLength)
    {
        if (v >= KitParams::decayMaxMs - 0.5f)
            return "Full";
        return msText (v, maxLength);
    }

    juce::String hzText (float v, int)
    {
        if (v >= 1000.0f)
            return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::NormalisableRange<float> skewed (float min, float max, float interval, float centre)
    {
        juce::NormalisableRange<float> range (min, max, interval);
        range.setSkewForCentre (centre);
        return range;
    }

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                          juce::NormalisableRange<float> range, float defaultValue,
                                                          juce::String (*text) (float, int))
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, KitParams::stateVersion }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (text));
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const juce::String& id, const juce::String& name,
                                                            const juce::StringArray& choices, int defaultIndex,
                                                            bool automatable = true)
    {
        return std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id, KitParams::stateVersion }, name, choices, defaultIndex,
            juce::AudioParameterChoiceAttributes().withAutomatable (automatable));
    }

    /** Routing is set up, not performed: note, choke group and output are kept
        out of the host's automation lists, where they would only be clutter. */
    std::unique_ptr<juce::AudioParameterInt> makeNote (const juce::String& id, const juce::String& name, int defaultNote)
    {
        return std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id, KitParams::stateVersion }, name, 0, 127, defaultNote,
            juce::AudioParameterIntAttributes()
                .withAutomatable (false)
                .withStringFromValueFunction ([] (int note, int) { return KitParams::noteName (note) + " " + juce::String (note); })
                .withValueFromStringFunction ([] (const juce::String& text) { return text.getTrailingIntValue(); }));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout KitboxProcessor::makeParameterLayout()
{
    namespace P = KitParams::Pad;
    using KitParams::padId;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Global first, so a host's generic parameter list opens on the controls
    // that affect everything.
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", " | ");

        group->addChild (makeFloat (KitParams::humanize,    "Humanize", { 0.0f, 100.0f, 1.0f }, 50.0f, percentText));
        group->addChild (makeFloat (KitParams::masterLevel, "Master",   skewed (-60.0f, 6.0f, 0.1f, -12.0f), 0.0f, dbText));

        // The send effects: Type, the effect's two knobs, the return level.
        // A and B mean whatever the loaded effect calls them (EffectCatalog),
        // so here they are plain 0..1 - the panel prints them in the effect's
        // own units. Version hint 2: added after the first release of the
        // parameter list, which hosts use to keep the order stable.
        for (int slot = 0; slot < KitParams::Fx::numSlots; ++slot)
        {
            const auto& ids = KitParams::Fx::ids[slot];
            const auto title = juce::String (KitParams::Fx::titles[slot]);
            const auto category = KitParams::Fx::categories[slot];

            juce::StringArray types;
            for (int t = 0; t < EffectCatalog::numSubtypes (category); ++t)
                types.add (EffectCatalog::info (category, t).name);

            group->addChild (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { ids.type, 2 }, title + " Type", types, KitParams::Fx::defaultTypes[slot]));

            for (const auto& [id, knob, value] : { std::tuple { ids.a, " A", KitParams::Fx::defaultA[slot] },
                                                   std::tuple { ids.b, " B", KitParams::Fx::defaultB[slot] } })
                group->addChild (std::make_unique<juce::AudioParameterFloat> (
                    juce::ParameterID { id, 2 }, title + knob, juce::NormalisableRange<float> (0.0f, 1.0f), value,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                    {
                        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
                    })));

            group->addChild (makeFloat (ids.level, title + " Level", skewed (-60.0f, 6.0f, 0.1f, -12.0f), 0.0f, dbText));
        }

        layout.add (std::move (group));
    }

    for (int pad = 0; pad < KitParams::numPads; ++pad)
    {
        const auto prefix = "Pad " + juce::String (pad + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "pad" + juce::String (pad + 1).paddedLeft ('0', 2), "Pad " + juce::String (pad + 1), " | ");

        group->addChild (makeFloat (padId (pad, P::level),    prefix + "Level",    skewed (-60.0f, 6.0f, 0.1f, -12.0f), 0.0f, dbText));
        group->addChild (makeFloat (padId (pad, P::pan),      prefix + "Pan",      { -1.0f, 1.0f, 0.01f }, 0.0f, panText));
        group->addChild (makeFloat (padId (pad, P::tune),     prefix + "Tune",     { -24.0f, 24.0f, 1.0f }, 0.0f, semitoneText));
        group->addChild (makeFloat (padId (pad, P::fine),     prefix + "Fine",     { -100.0f, 100.0f, 1.0f }, 0.0f, centText));
        group->addChild (makeFloat (padId (pad, P::start),    prefix + "Start",    skewed (0.0f, 100.0f, 0.1f, 10.0f), 0.0f, startText));
        group->addChild (makeFloat (padId (pad, P::velocity), prefix + "Velocity", { 0.0f, 100.0f, 1.0f }, 100.0f, percentText));

        group->addChild (makeFloat (padId (pad, P::attack), prefix + "Attack", skewed (0.0f, 200.0f, 0.1f, 20.0f), 0.0f, msText));
        group->addChild (makeFloat (padId (pad, P::hold),   prefix + "Hold",   skewed (0.0f, 2000.0f, 1.0f, 200.0f), 0.0f, msText));
        group->addChild (makeFloat (padId (pad, P::decay),  prefix + "Decay",  skewed (10.0f, KitParams::decayMaxMs, 1.0f, 500.0f), KitParams::decayMaxMs, decayText));

        group->addChild (makeChoice (padId (pad, P::filterType), prefix + "Filter", KitParams::filterTypeNames, 0));
        group->addChild (makeFloat (padId (pad, P::cutoff),    prefix + "Cutoff",    skewed (20.0f, 20000.0f, 1.0f, 632.0f), 20000.0f, hzText));
        group->addChild (makeFloat (padId (pad, P::resonance), prefix + "Resonance", { 0.0f, 100.0f, 1.0f }, 0.0f, percentText));
        group->addChild (makeFloat (padId (pad, P::drive),     prefix + "Drive",     { 0.0f, 100.0f, 1.0f }, 0.0f, percentText));

        group->addChild (makeFloat (padId (pad, P::envAttack), prefix + "Filter Env Attack", skewed (0.0f, 200.0f, 0.1f, 20.0f), 0.0f, msText));
        group->addChild (makeFloat (padId (pad, P::envDecay),  prefix + "Filter Env Decay",  skewed (10.0f, 5000.0f, 1.0f, 300.0f), 200.0f, msText));
        group->addChild (makeFloat (padId (pad, P::envAmount), prefix + "Filter Env Amount", { -48.0f, 48.0f, 1.0f }, 0.0f, semitoneText));

        group->addChild (makeFloat (padId (pad, P::sendReverb), prefix + "Reverb Send", { 0.0f, 100.0f, 1.0f }, 0.0f, percentText));
        group->addChild (makeFloat (padId (pad, P::sendDelay),  prefix + "Delay Send",  { 0.0f, 100.0f, 1.0f }, 0.0f, percentText));
        group->addChild (makeFloat (padId (pad, P::sendPhaser), prefix + "Mod Send", { 0.0f, 100.0f, 1.0f }, 0.0f, percentText));

        group->addChild (makeNote (padId (pad, P::note), prefix + "Note", KitParams::firstNote + pad));
        group->addChild (makeChoice (padId (pad, P::choke), prefix + "Choke Group", KitParams::chokeNames(), 0, false));
        group->addChild (makeChoice (padId (pad, P::output), prefix + "Output", KitParams::outputNames(), 0, false));

        layout.add (std::move (group));
    }

    return layout;
}

//==============================================================================
//  Main plus sixteen stereo aux outputs, the aux ones off by default. A host
//  that offers multi-output instruments (Logic's "Multi Output" variant, or
//  enabling outputs in Live, Reaper, Cubase) turns them on; everywhere else
//  Kitbox is a plain stereo instrument.
juce::AudioProcessor::BusesProperties KitboxProcessor::makeBuses()
{
    auto buses = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);

    for (int o = 1; o <= KitParams::numAuxOutputs; ++o)
        buses = buses.withOutput ("Out " + juce::String (o), juce::AudioChannelSet::stereo(), false);

    return buses;
}

KitboxProcessor::KitboxProcessor()
    : AudioProcessor (makeBuses()),
      parameters (*this, nullptr, KitParams::stateTreeType, makeParameterLayout())
{
    formats.registerBasicFormats();

    std::array<PadParams, KitParams::numPads> pads {};

    for (int pad = 0; pad < KitParams::numPads; ++pad)
        for (int i = 0; i < PadParams::count; ++i)
            *pads[(size_t) pad].slot (i) = parameters.getRawParameterValue (KitParams::padId (pad, KitParams::Pad::all[i]));

    GlobalParams global;
    global.humanize       = parameters.getRawParameterValue (KitParams::humanize);
    global.masterLevel    = parameters.getRawParameterValue (KitParams::masterLevel);
    for (size_t slot = 0; slot < global.fx.size(); ++slot)
    {
        const auto& ids = KitParams::Fx::ids[slot];
        global.fx[slot] = { parameters.getRawParameterValue (ids.type), parameters.getRawParameterValue (ids.a),
                            parameters.getRawParameterValue (ids.b),    parameters.getRawParameterValue (ids.level) };
    }

    engine.setParameters (pads, global);

    parameters.state.setProperty (KitParams::propStateVersion, KitParams::stateVersion, nullptr);
    parameters.state.setProperty (KitParams::propHumanizeRange, KitParams::humanizeRange, nullptr);
    parameters.state.setProperty (KitParams::propSelectedPad, 0, nullptr);

    for (auto& hit : pendingHits)
        hit.store (0.0f);

    loader.startThread();
    startTimerHz (20);
}

KitboxProcessor::~KitboxProcessor()
{
    // The loader first: it posts results back to this object, and must not be
    // able to while the rest of it is being torn down.
    loader.stop();
    cancelPendingUpdate();
    stopTimer();
}

//==============================================================================
void KitboxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

void KitboxProcessor::releaseResources()
{
    engine.reset();
}

bool KitboxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
    {
        const auto& set = layouts.outputBuses.getReference (bus);

        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return layouts.inputBuses.isEmpty();
}

void KitboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm())
                engine.setTempo (*bpm);

    // Where each output bus lives in this block's buffer. A bus the host has
    // not enabled stays null, and the engine sends its pads to Main.
    DrumEngine::Outputs outputs;

    for (int b = 0; b < juce::jmin (KitParams::numOutputBuses, getBusCount (false)); ++b)
    {
        const auto* bus = getBus (false, b);

        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() != 2)
            continue;

        const auto first = bus->getChannelIndexInProcessBlockBuffer (0);

        if (first + 1 < buffer.getNumChannels())
        {
            outputs.left[(size_t) b]  = buffer.getWritePointer (first);
            outputs.right[(size_t) b] = buffer.getWritePointer (first + 1);
        }
    }

    if (outputs.left[0] == nullptr)
    {
        buffer.clear();
        return;
    }

    // Pads clicked on the panel, at the start of the block. They go straight
    // to their pad rather than through a MIDI note, because a note may play
    // several pads and a click means this one.
    for (int pad = 0; pad < KitParams::numPads; ++pad)
        if (const auto velocity = pendingHits[(size_t) pad].exchange (0.0f); velocity > 0.0f)
            engine.trigger (pad, velocity);

    const auto advance = [&outputs] (int samples)
    {
        auto moved = outputs;
        for (size_t o = 0; o < moved.left.size(); ++o)
            if (moved.left[o] != nullptr)
            {
                moved.left[o]  += samples;
                moved.right[o] += samples;
            }
        return moved;
    };

    // Rendered in pieces between MIDI events, so every hit starts on the
    // sample the host put it on. A drum machine that quantises its own input
    // to the block size flams at any buffer setting above 64.
    int rendered = 0;

    for (const auto metadata : midi)
    {
        const auto position = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (position > rendered)
        {
            engine.render (advance (rendered), position - rendered);
            rendered = position;
        }

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            // Learn takes the note for the pad that asked, and the note still
            // plays - hearing the key you pressed is the confirmation.
            if (const auto learning = learnPad.exchange (-1); learning >= 0)
                learnedAssignment.store ((learning << 8) | message.getNoteNumber());

            engine.handleNoteOn (message.getNoteNumber(), message.getFloatVelocity());
        }
    }

    if (rendered < numSamples)
        engine.render (advance (rendered), numSamples - rendered);
}

juce::AudioProcessorEditor* KitboxProcessor::createEditor()
{
    return new KitboxEditor (*this);
}

//==============================================================================
//  State
//
//  copyState(), never state.createXml(): a knob's live value reaches the tree
//  only when the APVTS flushes on its timer, and serialising the tree directly
//  saves whatever the last flush left there. copyState() flushes first.
void KitboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    destData = KitFile::write (parameters.copyState(), padSamples);
}

void KitboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto kit = KitFile::read (data, (size_t) juce::jmax (0, sizeInBytes));

    if (kit.ok())
        applyKit (kit);
}

juce::Result KitboxProcessor::applyKit (const KitFile::Contents& kit)
{
    // Decoded before anything is replaced, so a kit with one unreadable sample
    // still loads everything else - and says which pad it could not fill.
    std::array<SampleData::Ptr, KitParams::numPads> decoded;
    juce::StringArray problems;

    for (int pad = 0; pad < KitParams::numPads; ++pad)
    {
        const auto& stored = kit.samples[(size_t) pad];
        padErrors[(size_t) pad].clear();

        if (stored.bytes.getSize() == 0)
            continue;

        juce::String error;
        decoded[(size_t) pad] = SampleData::decode (stored.bytes, stored.fileName, formats, error);

        if (decoded[(size_t) pad] == nullptr)
        {
            padErrors[(size_t) pad] = error;
            problems.add ("Pad " + juce::String (pad + 1) + ": " + error);
        }
    }

    // Kits and sessions from before the effect slots name the old effect
    // knobs; translated here, or they would come back at the defaults.
    auto params = kit.params.createCopy();
    FxMigration::apply (params);

    // Kits and sessions from before 0.6.0 used half the Humanize range: their value is halved, so
    // a kit saved at 100 % comes back at 50 % and sounds exactly as it did.
    if ((int) params.getProperty (KitParams::propHumanizeRange, 1) < KitParams::humanizeRange)
    {
        auto humanize = params.getChildWithProperty ("id", juce::String (KitParams::humanize));

        if (humanize.isValid())
            humanize.setProperty ("value", 0.5 * (double) humanize.getProperty ("value"), nullptr);

        params.setProperty (KitParams::propHumanizeRange, KitParams::humanizeRange, nullptr);
    }

    parameters.replaceState (params);

    for (int pad = 0; pad < KitParams::numPads; ++pad)
        setPadSample (pad, decoded[(size_t) pad]);

    sendChangeMessage();

    return problems.isEmpty() ? juce::Result::ok() : juce::Result::fail (problems.joinIntoString ("\n"));
}

juce::Result KitboxProcessor::saveKit (const juce::File& file)
{
    // Named before writing, so the saved state carries its own name and a
    // session that restores it shows the same name again.
    setKitName (file.getFileNameWithoutExtension());

    const auto state = KitFile::write (parameters.copyState(), padSamples);
    const auto text  = AuPreset::write (state, getKitName());

    // replaceWithData writes a temporary file and moves it into place. Opening
    // the file with createOutputStream would append to an existing kit.
    if (! file.getParentDirectory().createDirectory()
        || ! file.replaceWithText (text, false, false, "\n"))
        return juce::Result::fail ("Could not write " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result KitboxProcessor::loadKit (const juce::File& file)
{
    juce::MemoryBlock block;

    if (! file.loadFileAsData (block))
        return juce::Result::fail ("Could not read " + file.getFullPathName());

    auto name = file.getFileNameWithoutExtension();

    // Kits are .aupreset files; the .kitbox container is what they carry, and
    // a bare .kitbox from the first days of the plugin still loads as it is.
    if (file.hasFileExtension (AuPreset::extension))
    {
        const auto preset = AuPreset::read (block.toString());

        if (! preset.ok())
            return juce::Result::fail (preset.error);

        block = preset.pluginState;
        if (preset.name.isNotEmpty())
            name = preset.name;
    }

    const auto kit = KitFile::read (block.getData(), block.getSize());

    if (! kit.ok())
        return juce::Result::fail (kit.error);

    const auto result = applyKit (kit);
    setKitName (name);
    return result;
}

juce::String KitboxProcessor::getKitName() const
{
    return parameters.state.getProperty (KitParams::propKitName).toString();
}

void KitboxProcessor::setKitName (const juce::String& newName)
{
    // Settings saved before kits were .aupreset files can carry the old
    // extension inside their name ("KITBOX 808.kitbox"); it is not part of it.
    auto name = newName.trim();
    if (name.endsWithIgnoreCase (KitFile::extension))
        name = name.dropLastCharacters ((int) std::strlen (KitFile::extension)).trim();

    parameters.state.setProperty (KitParams::propKitName, name, nullptr);

    // Tells the host the "program" changed, so JUCE's AU wrapper hands Logic
    // the new present-preset name - the kit's name, without an extension.
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    sendChangeMessage();
}

//==============================================================================
//  Pads
void KitboxProcessor::setPadSample (int pad, SampleData::Ptr sample)
{
    if (pad < 0 || pad >= KitParams::numPads)
        return;

    auto previous = padSamples[(size_t) pad];

    pool.add (sample);
    engine.setPadSample (pad, sample.get());
    padSamples[(size_t) pad] = sample;

    // Retired only if no other pad still holds it - a sample can sit on two
    // pads for a moment during a swap.
    if (previous != nullptr && std::find (padSamples.begin(), padSamples.end(), previous) == padSamples.end())
        pool.retire (previous.get());
}

void KitboxProcessor::clearPad (int pad)
{
    setPadSample (pad, nullptr);
    padErrors[(size_t) pad].clear();
    sendChangeMessage();
}

void KitboxProcessor::renamePad (int pad, const juce::String& newName)
{
    if (pad < 0 || pad >= KitParams::numPads)
        return;

    const auto sample = getPadSample (pad);

    if (sample == nullptr || newName.trim().isEmpty() || newName.trim() == sample->getName())
        return;

    sample->rename (newName);

    // Not a parameter, so the host would not know the session changed.
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    sendChangeMessage();
}

void KitboxProcessor::swapPads (int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= KitParams::numPads || b >= KitParams::numPads)
        return;

    auto sampleA = padSamples[(size_t) a];
    auto sampleB = padSamples[(size_t) b];
    setPadSample (a, sampleB);
    setPadSample (b, sampleA);
    std::swap (padErrors[(size_t) a], padErrors[(size_t) b]);

    for (const auto* suffix : KitParams::Pad::all)
    {
        // The note stays where it is: it belongs to the pad's place under the
        // player's fingers, not to the sound. Choke and output move with it.
        if (juce::String (suffix) == KitParams::Pad::note)
            continue;

        auto* pa = parameters.getParameter (KitParams::padId (a, suffix));
        auto* pb = parameters.getParameter (KitParams::padId (b, suffix));
        const auto va = pa->getValue();
        const auto vb = pb->getValue();

        pa->beginChangeGesture(); pa->setValueNotifyingHost (vb); pa->endChangeGesture();
        pb->beginChangeGesture(); pb->setValueNotifyingHost (va); pb->endChangeGesture();
    }

    sendChangeMessage();
}

void KitboxProcessor::loadFiles (int pad, juce::Array<juce::File> files)
{
    // Name order, the way the Finder sorts: "Kick 2" before "Kick 10".
    std::sort (files.begin(), files.end(), [] (const juce::File& x, const juce::File& y)
    {
        return x.getFileName().compareNatural (y.getFileName()) < 0;
    });

    for (int i = 0; i < files.size() && pad + i < KitParams::numPads; ++i)
    {
        ++loadingCount[(size_t) (pad + i)];
        padErrors[(size_t) (pad + i)].clear();
        loader.add (pad + i, files[i]);
    }

    sendChangeMessage();
}

void KitboxProcessor::playPad (int pad, float velocity)
{
    if (pad < 0 || pad >= KitParams::numPads)
        return;

    // Two clicks inside one block count once, at the louder velocity.
    auto& slot = pendingHits[(size_t) pad];
    auto current = slot.load();
    while (velocity > current && ! slot.compare_exchange_weak (current, velocity)) {}
}

int KitboxProcessor::getPadNote (int pad) const
{
    return (int) std::lround (parameters.getRawParameterValue (KitParams::padId (pad, KitParams::Pad::note))->load());
}

void KitboxProcessor::startLearn (int pad)
{
    learnPad.store (juce::jlimit (0, KitParams::numPads - 1, pad));
    sendChangeMessage();
}

void KitboxProcessor::cancelLearn()
{
    learnPad.store (-1);
    sendChangeMessage();
}

void KitboxProcessor::applyLearnedNote()
{
    const auto learned = learnedAssignment.exchange (-1);

    if (learned < 0)
        return;

    const auto pad = learned >> 8, note = learned & 0x7f;
    auto* parameter = parameters.getParameter (KitParams::padId (pad, KitParams::Pad::note));

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) note));
    parameter->endChangeGesture();

    sendChangeMessage();
}

juce::File KitboxProcessor::writeSampleForDrag (int pad)
{
    const auto sample = getPadSample (pad);

    if (sample == nullptr)
        return {};

    auto name = sample->getFileName();
    if (name.isEmpty())
        name = sample->getName() + ".wav";

    // One folder per pad, emptied each time, so a drag always hands over the
    // file under its own name and the temporary folder never accumulates.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("Kitbox Drag")
                            .getChildFile ("Pad " + juce::String (pad + 1));
    folder.deleteRecursively();
    folder.createDirectory();

    const auto file = folder.getChildFile (juce::File::createLegalFileName (name));
    const auto& bytes = sample->getOriginal();

    if (! file.replaceWithData (bytes.getData(), bytes.getSize()))
        return {};

    return file;
}

int KitboxProcessor::getSelectedPad() const
{
    return juce::jlimit (0, KitParams::numPads - 1, (int) parameters.state.getProperty (KitParams::propSelectedPad, 0));
}

void KitboxProcessor::setSelectedPad (int pad)
{
    parameters.state.setProperty (KitParams::propSelectedPad, juce::jlimit (0, KitParams::numPads - 1, pad), nullptr);
}

bool KitboxProcessor::isAudioFile (const juce::File& file) const
{
    return file.existsAsFile()
        && const_cast<juce::AudioFormatManager&> (formats).findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

//==============================================================================
void KitboxProcessor::timerCallback()
{
    applyLearnedNote();
    pool.collect();
}

void KitboxProcessor::handleAsyncUpdate()
{
    for (auto& result : loader.takeResults())
    {
        auto& count = loadingCount[(size_t) result.pad];
        count = juce::jmax (0, count - 1);

        if (result.sample != nullptr)
        {
            setPadSample (result.pad, result.sample);
            padErrors[(size_t) result.pad].clear();
        }
        else
        {
            padErrors[(size_t) result.pad] = result.error;
        }
    }

    sendChangeMessage();
}

//==============================================================================
KitboxProcessor::Loader::Loader (KitboxProcessor& ownerToUse)
    : juce::Thread ("Kitbox sample loader"), owner (ownerToUse)
{
}

KitboxProcessor::Loader::~Loader()
{
    stop();
}

void KitboxProcessor::Loader::stop()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

void KitboxProcessor::Loader::add (int pad, const juce::File& file)
{
    {
        const juce::ScopedLock sl (lock);
        jobs.emplace_back (pad, file);
    }

    wake.signal();
}

std::vector<KitboxProcessor::Loader::Result> KitboxProcessor::Loader::takeResults()
{
    const juce::ScopedLock sl (lock);
    return std::exchange (results, {});
}

void KitboxProcessor::Loader::run()
{
    while (! threadShouldExit())
    {
        std::pair<int, juce::File> job { -1, {} };

        {
            const juce::ScopedLock sl (lock);

            if (! jobs.empty())
            {
                job = jobs.front();
                jobs.erase (jobs.begin());
            }
        }

        if (job.first < 0)
        {
            wake.wait (500);
            continue;
        }

        Result result { job.first, nullptr, {} };
        result.sample = SampleData::fromFile (job.second, owner.formats, result.error);

        {
            const juce::ScopedLock sl (lock);
            results.push_back (std::move (result));
        }

        owner.triggerAsyncUpdate();
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KitboxProcessor();
}
