//
//  EditorShot.cpp
//  Kitbox
//
//  Builds the plugin headlessly, loads a synthesised demo kit, checks the
//  things that need the whole processor, and paints the panel into a PNG.
//
//  The checks are the ones KitboxCheck cannot make because they live between
//  the engine and the host:
//
//  - the session round trip: every parameter, the selected pad and every
//    sample's bytes, through getStateInformation into a fresh processor - with
//    the knobs moved immediately before saving, which is when a state saved
//    from the tree instead of copyState() comes back stale;
//  - hits land on the sample the MIDI event names, not on the block boundary;
//  - swapping two pads swaps samples and knobs;
//  - dragging a pad out hands over the original file, byte for byte;
//  - files dropped on a pad load on the loader thread, fill the following
//    pads, and do so in the Finder's name order.
//
//  The render is the other half. A panel is arithmetic on rectangles, which is
//  obviously right while reading it and obviously wrong once drawn - look at
//  the PNG after every layout change.
//
//  Usage:  EditorShot <output-directory>
//

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>

namespace
{
    int failures = 0, checks = 0;

    void check (bool condition, const juce::String& what, const juce::String& detail = {})
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::printf ("FAIL: %s  %s\n", what.toRawUTF8(), detail.toRawUTF8());
        }
    }

    constexpr double rate = 44100.0;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    //==============================================================================
    //  The demo kit. Synthesised rather than shipped, so the target needs no
    //  asset somebody has to remember to keep.
    juce::AudioBuffer<float> synth (double seconds, const std::function<float (double t, juce::Random&)>& f)
    {
        juce::Random random (42);
        juce::AudioBuffer<float> buffer (1, (int) (seconds * rate));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, f (i / rate, random));

        return buffer;
    }

    SampleData::Ptr kick()
    {
        double phase = 0.0;
        return SampleData::fromAudio (synth (0.5, [&] (double t, juce::Random&)
        {
            phase += twoPi * (48.0 + 110.0 * std::exp (-t * 30.0)) / rate;
            return (float) (0.95 * std::sin (phase) * std::exp (-t * 7.0));
        }), rate, "Kick 909");
    }

    SampleData::Ptr snare()
    {
        return SampleData::fromAudio (synth (0.35, [] (double t, juce::Random& r)
        {
            const auto body  = 0.5 * std::sin (twoPi * 185.0 * t) * std::exp (-t * 25.0);
            const auto noise = 0.45 * (r.nextFloat() * 2.0 - 1.0) * std::exp (-t * 14.0);
            return (float) (body + noise);
        }), rate, "Snare Tight");
    }

    SampleData::Ptr hat (double decay, const juce::String& name)
    {
        float last = 0.0f;
        return SampleData::fromAudio (synth (decay * 5.0, [&, decay] (double t, juce::Random& r)
        {
            const auto n = r.nextFloat() * 2.0f - 1.0f;
            const auto high = n - last;   // a first difference: crude, bright
            last = n;
            return (float) (0.5 * high * std::exp (-t / decay));
        }), rate, name);
    }

    SampleData::Ptr clap()
    {
        return SampleData::fromAudio (synth (0.3, [] (double t, juce::Random& r)
        {
            const auto burst = std::fmod (t, 0.011) < 0.004 && t < 0.045 ? 1.0 : 0.0;
            const auto env = t < 0.045 ? burst : std::exp (-(t - 0.045) * 18.0);
            return (float) (0.6 * (r.nextFloat() * 2.0 - 1.0) * env);
        }), rate, "Clap");
    }

    SampleData::Ptr tone (double frequency, double decay, double seconds, const juce::String& name)
    {
        return SampleData::fromAudio (synth (seconds, [=] (double t, juce::Random&)
        {
            return (float) (0.8 * std::sin (twoPi * frequency * t) * std::exp (-t / decay));
        }), rate, name);
    }

    void loadDemoKit (KitboxProcessor& processor)
    {
        processor.setPadSample (0, kick());
        processor.setPadSample (1, snare());
        processor.setPadSample (2, hat (0.03, "Hat Closed"));
        processor.setPadSample (3, hat (0.25, "Hat Open"));
        processor.setPadSample (4, clap());
        processor.setPadSample (5, tone (98.0, 0.18, 0.8, "Tom Low"));
        processor.setPadSample (6, tone (1700.0, 0.01, 0.08, "Rim"));
        processor.setPadSample (7, tone (560.0, 0.12, 0.5, "Cowbell"));
        processor.setPadSample (8, tone (45.0, 0.5, 2.0, "Sub Boom"));
        processor.setPadSample (9, hat (0.08, "Shaker"));
    }

    void setParam (KitboxProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.parameters.getParameter (id);
        jassert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void renderBlocks (KitboxProcessor& processor, int blocks, std::function<void (int, juce::MidiBuffer&)> midiFor = nullptr)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (midiFor) midiFor (b, midi);
            processor.processBlock (buffer, midi);
        }
    }

    bool writePng (const juce::Image& image, const juce::File& file)
    {
        // Deleted first: createOutputStream appends to an existing file, and a
        // PNG decoder stops at the first image - the old one.
        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        juce::PNGImageFormat png;
        return stream != nullptr && png.writeImageToStream (image, *stream);
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    //==============================================================================
    void checkStateRoundTrip()
    {
        KitboxProcessor original;
        original.prepareToPlay (48000.0, 512);
        loadDemoKit (original);
        original.setSelectedPad (5);

        // Moved immediately before saving, on purpose: see copyState() in
        // PluginProcessor.cpp.
        setParam (original, KitParams::padId (1, KitParams::Pad::cutoff), 2500.0f);
        setParam (original, KitParams::padId (1, KitParams::Pad::filterType), 2.0f);
        setParam (original, KitParams::padId (7, KitParams::Pad::tune), -5.0f);
        setParam (original, KitParams::padId (15, KitParams::Pad::sendDelay), 42.0f);
        setParam (original, KitParams::Fx::ids[KitParams::Fx::reverb].a, 0.83f);
        setParam (original, KitParams::Fx::ids[KitParams::Fx::delay].type, 2.0f);   // Ping-Pong
        setParam (original, KitParams::Fx::ids[KitParams::Fx::mod].type, 2.0f);     // Flanger
        setParam (original, KitParams::humanize, 25.0f);
        setParam (original, KitParams::padId (6, KitParams::Pad::note), 60.0f);
        setParam (original, KitParams::padId (2, KitParams::Pad::choke), 1.0f);
        setParam (original, KitParams::padId (4, KitParams::Pad::output), 3.0f);

        juce::MemoryBlock state;
        original.getStateInformation (state);

        KitboxProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());

        int mismatched = 0;
        for (auto* parameter : original.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            auto* other = restored.parameters.getParameter (withId->paramID);

            if (other == nullptr || std::abs (other->getValue() - parameter->getValue()) > 1.0e-6f)
            {
                ++mismatched;
                std::printf ("  parameter %s did not come back\n", withId->paramID.toRawUTF8());
            }
        }

        check (mismatched == 0, "every parameter survives the session round trip");
        check (restored.getSelectedPad() == 5, "the selected pad survives it");

        for (int pad = 0; pad < KitParams::numPads; ++pad)
        {
            const auto a = original.getPadSample (pad);
            const auto b = restored.getPadSample (pad);

            if (a == nullptr)
            {
                check (b == nullptr, "pad " + juce::String (pad + 1) + " stays empty");
                continue;
            }

            check (b != nullptr && b->getOriginal() == a->getOriginal() && b->getFileName() == a->getFileName(),
                   "pad " + juce::String (pad + 1) + "'s sample survives byte for byte");
        }

        // The same bytes through a kit on disk - an .aupreset, as Logic writes.
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto kitFile = temp.getChildFile ("EditorShot Kit.aupreset");
        check (original.saveKit (kitFile).wasOk(), "Save Kit writes");
        check (original.getProgramName (0) == "EditorShot Kit", "saving names the kit after the file, without extension",
               original.getProgramName (0));

        const auto preset = AuPreset::read (kitFile.loadFileAsString());
        check (preset.ok() && preset.name == "EditorShot Kit", "the file is an Audio Unit preset named without extension",
               preset.name + " " + preset.error);

        KitboxProcessor fromDisk;
        check (fromDisk.loadKit (kitFile).wasOk(), "Load Kit reads it back");
        check (fromDisk.getPadSample (9) != nullptr && fromDisk.getPadSample (9)->getName() == "Shaker", "the kit's samples load");
        check (std::abs (fromDisk.parameters.getRawParameterValue (KitParams::Fx::ids[KitParams::Fx::reverb].a)->load() - 0.83f) < 0.001f
                   && (int) fromDisk.parameters.getRawParameterValue (KitParams::Fx::ids[KitParams::Fx::delay].type)->load() == 2,
               "the kit's knobs and effect types load");
        check (fromDisk.getProgramName (0) == "EditorShot Kit", "and its name", fromDisk.getProgramName (0));

        // What Logic hands over when it loads that preset from its own menu:
        // the jucePluginState bytes, straight into setStateInformation.
        KitboxProcessor viaHost;
        viaHost.setStateInformation (preset.pluginState.getData(), (int) preset.pluginState.getSize());
        check (viaHost.getPadSample (0) != nullptr && viaHost.getProgramName (0) == "EditorShot Kit",
               "a preset loaded by the host restores kit and name");

        // A bare .kitbox from before still loads, and an old ".kitbox" inside a
        // name is dropped.
        const auto legacy = temp.getChildFile ("Old.kitbox");
        juce::MemoryBlock legacyBytes;
        original.getStateInformation (legacyBytes);
        legacy.replaceWithData (legacyBytes.getData(), legacyBytes.getSize());
        KitboxProcessor fromLegacy;
        check (fromLegacy.loadKit (legacy).wasOk() && fromLegacy.getPadSample (9) != nullptr, "a legacy .kitbox still loads");
        fromLegacy.setKitName ("KITBOX 808.kitbox");
        check (fromLegacy.getProgramName (0) == "KITBOX 808", "an old .kitbox in a name is dropped", fromLegacy.getProgramName (0));

        // A kit from before the effect slots: its state names the old effect
        // knobs. Built here from a current state by swapping the slot knobs for
        // the old ones, then loaded the way Load Kit does.
        {
            juce::MemoryBlock current;
            original.getStateInformation (current);
            auto contents = KitFile::read (current.getData(), current.getSize());
            auto params = contents.params;

            for (const auto& ids : KitParams::Fx::ids)
                for (const auto* id : { ids.type, ids.a, ids.b })
                    params.removeChild (params.getChildWithProperty ("id", juce::String (id)), nullptr);

            for (const auto& [id, value] : { std::pair { KitParams::Legacy::reverbSize, 64.0f },
                                             std::pair { KitParams::Legacy::delayTime, 8.0f },
                                             std::pair { KitParams::Legacy::phaserRate, 2.0f } })
            {
                juce::ValueTree param ("PARAM");
                param.setProperty ("id", juce::String (id), nullptr);
                param.setProperty ("value", value, nullptr);
                params.appendChild (param, nullptr);
            }

            std::array<SampleData::Ptr, KitParams::numPads> samples;
            for (int p = 0; p < KitParams::numPads; ++p)
                samples[(size_t) p] = original.getPadSample (p);

            const auto oldKit = temp.getChildFile ("Before Slots.kitbox");
            const auto bytes = KitFile::write (params, samples);
            oldKit.replaceWithData (bytes.getData(), bytes.getSize());

            KitboxProcessor fromOld;
            const auto value = [&fromOld] (const char* id) { return fromOld.parameters.getRawParameterValue (id)->load(); };
            check (fromOld.loadKit (oldKit).wasOk() && fromOld.getPadSample (0) != nullptr, "a kit from before the slots loads");
            check (std::abs (value (KitParams::Fx::ids[KitParams::Fx::reverb].a) - 0.64f) < 0.001f
                       && std::abs (value (KitParams::Fx::ids[KitParams::Fx::delay].a) - 8.0f / 13.0f) < 0.001f
                       && (int) value (KitParams::Fx::ids[KitParams::Fx::delay].type) == 1
                       && (int) value (KitParams::Fx::ids[KitParams::Fx::mod].type) == 1,
                   "and its effect settings arrive in the slots");
            oldKit.deleteFile();
        }

        // Humanize doubled its range in 0.6.0. A session from before (no humanizeRange on its tree)
        // at 100 % comes back at 50 %, which sounds as 100 % did; a current one keeps its value.
        {
            juce::MemoryBlock current;
            original.getStateInformation (current);
            auto contents = KitFile::read (current.getData(), current.getSize());

            std::array<SampleData::Ptr, KitParams::numPads> samples;
            for (int p = 0; p < KitParams::numPads; ++p)
                samples[(size_t) p] = original.getPadSample (p);

            const auto loadWith = [&] (bool old)
            {
                auto params = contents.params.createCopy();
                params.getChildWithProperty ("id", juce::String (KitParams::humanize)).setProperty ("value", 100.0f, nullptr);
                if (old)
                    params.removeProperty (KitParams::propHumanizeRange, nullptr);

                const auto bytes = KitFile::write (params, samples);
                KitboxProcessor loaded;
                loaded.setStateInformation (bytes.getData(), (int) bytes.getSize());
                return loaded.parameters.getRawParameterValue (KitParams::humanize)->load();
            };

            check (std::abs (loadWith (true) - 50.0f) < 0.01f, "a session from before 0.6.0 at Humanize 100 % loads at 50 %",
                   juce::String (loadWith (true), 1));
            check (std::abs (loadWith (false) - 100.0f) < 0.01f, "a current session keeps its Humanize", juce::String (loadWith (false), 1));

            KitboxProcessor fresh;
            check (std::abs (fresh.parameters.getRawParameterValue (KitParams::humanize)->load() - 50.0f) < 0.01f,
                   "a new Kitbox starts at Humanize 50 %");
        }

        kitFile.deleteFile();
        legacy.deleteFile();

        // Swap
        const auto kickBefore  = restored.getPadSample (0);
        const auto snareBefore = restored.getPadSample (1);
        restored.swapPads (0, 1);
        check (restored.getPadSample (0) == snareBefore && restored.getPadSample (1) == kickBefore, "a swap exchanges samples");
        check ((int) restored.parameters.getRawParameterValue (KitParams::padId (0, KitParams::Pad::filterType))->load() == 2
                   && (int) restored.parameters.getRawParameterValue (KitParams::padId (1, KitParams::Pad::filterType))->load() == 0,
               "and knobs");
        check (restored.getPadNote (0) == 36 && restored.getPadNote (1) == 37, "but each pad keeps its note");

        // Drag out
        const auto dragged = restored.writeSampleForDrag (1);
        juce::MemoryBlock draggedBytes;
        dragged.loadFileAsData (draggedBytes);
        check (dragged.getFileName() == "Kick 909.wav" && draggedBytes == kickBefore->getOriginal(),
               "dragging a pad out hands over its original file", dragged.getFullPathName());

        // Rename
        restored.renamePad (1, "  Big Kick ");
        check (restored.getPadSample (1)->getName() == "Big Kick" && restored.getPadSample (1)->getFileName() == "Big Kick.wav",
               "renaming a pad renames its sample and keeps the extension", restored.getPadSample (1)->getFileName());
        restored.renamePad (1, "   ");
        check (restored.getPadSample (1)->getName() == "Big Kick", "an empty name changes nothing");
        restored.clearPad (15);
        restored.renamePad (15, "Nothing here");
        check (restored.getPadSample (15) == nullptr, "an empty pad cannot be renamed");

        {
            juce::MemoryBlock renamedState;
            restored.getStateInformation (renamedState);
            KitboxProcessor reloaded;
            reloaded.setStateInformation (renamedState.getData(), (int) renamedState.getSize());
            const auto sample = reloaded.getPadSample (1);
            check (sample != nullptr && sample->getName() == "Big Kick" && sample->getOriginal() == kickBefore->getOriginal(),
                   "the new name survives the session round trip");
        }

        restored.swapPads (0, 1);
        check (restored.getPadSample (0)->getName() == "Big Kick", "the name moves with a swap");
        check (restored.writeSampleForDrag (0).getFileName() == "Big Kick.wav", "and a drag out carries it");
    }

    void checkSampleAccurateHits()
    {
        KitboxProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> dc (1, 4800);
        for (int i = 0; i < dc.getNumSamples(); ++i)
            dc.setSample (0, i, 0.5f);
        processor.setPadSample (0, SampleData::fromAudio (dc, 48000.0, "DC"));
        setParam (processor, KitParams::humanize, 0.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, KitParams::firstNote, (juce::uint8) 127), 100);
        processor.processBlock (buffer, midi);

        check (buffer.getSample (0, 99) == 0.0f && buffer.getSample (0, 100) > 0.3f,
               "a hit starts on the sample its MIDI event names");
    }

    /** A buffer laid out the way a host lays out every bus enabled: Main in
        channels 0-1, Out n in channels 2n and 2n+1. */
    void checkMultiOutAndLearn()
    {
        KitboxProcessor processor;

        auto layout = processor.getBusesLayout();
        for (auto& set : layout.outputBuses)
            set = juce::AudioChannelSet::stereo();
        check (processor.setBusesLayout (layout), "all sixteen aux outputs can be enabled");
        check (processor.getTotalNumOutputChannels() == 34, "giving 34 output channels",
               juce::String (processor.getTotalNumOutputChannels()));

        processor.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> dc (1, 48000);
        for (int i = 0; i < dc.getNumSamples(); ++i)
            dc.setSample (0, i, 0.5f);
        processor.setPadSample (0, SampleData::fromAudio (dc, 48000.0, "DC"));
        processor.setPadSample (1, SampleData::fromAudio (dc, 48000.0, "DC 2"));
        setParam (processor, KitParams::humanize, 0.0f);
        setParam (processor, KitParams::padId (0, KitParams::Pad::output), 2.0f);

        juce::AudioBuffer<float> buffer (34, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
        processor.processBlock (buffer, midi);

        check (buffer.getMagnitude (4, 0, 512) > 0.3f && buffer.getMagnitude (0, 0, 512) < 1.0e-9f
                   && buffer.getMagnitude (2, 0, 512) < 1.0e-9f,
               "a pad on Out 2 comes out of channels 5-6 only");

        // Back to plain stereo: the same setting now plays on Main.
        KitboxProcessor stereo;
        stereo.prepareToPlay (48000.0, 512);
        stereo.setPadSample (0, SampleData::fromAudio (dc, 48000.0, "DC"));
        setParam (stereo, KitParams::padId (0, KitParams::Pad::output), 2.0f);
        juce::AudioBuffer<float> two (2, 512);
        juce::MidiBuffer note;
        note.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
        stereo.processBlock (two, note);
        check (two.getMagnitude (0, 0, 512) > 0.3f, "in a stereo instance a pad on Out 2 plays on Main");

        // Learn
        processor.startLearn (1);
        juce::MidiBuffer key;
        key.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 10);
        processor.processBlock (buffer, key);
        processor.applyLearnedNote();
        check (processor.getPadNote (1) == 72 && processor.getLearnPad() < 0, "Learn gives the pad the next note played",
               juce::String (processor.getPadNote (1)));

        // A click plays that pad, even when its note is shared.
        setParam (processor, KitParams::padId (0, KitParams::Pad::note), 72.0f);
        processor.releaseResources();
        processor.prepareToPlay (48000.0, 512);
        const auto hitsBefore0 = processor.getEngine().getHitCount (0);
        const auto hitsBefore1 = processor.getEngine().getHitCount (1);
        processor.playPad (1, 1.0f);
        juce::MidiBuffer none;
        processor.processBlock (buffer, none);
        check (processor.getEngine().getHitCount (1) == hitsBefore1 + 1
                   && processor.getEngine().getHitCount (0) == hitsBefore0,
               "clicking a pad plays that pad only, not every pad on its note");
    }

    void checkDroppedFiles()
    {
        KitboxProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("EditorShot drop");
        folder.deleteRecursively();
        folder.createDirectory();

        juce::Array<juce::File> files;

        for (const auto* name : { "Snare 10.wav", "Snare 2.wav", "Snare 1.wav", "Snare 3.wav", "Snare 4.wav" })
        {
            const auto file = folder.getChildFile (name);
            const auto sample = tone (200.0, 0.1, 0.3, "x");
            file.replaceWithData (sample->getOriginal().getData(), sample->getOriginal().getSize());
            files.add (file);
        }

        processor.loadFiles (12, files);

        for (int wait = 0; wait < 100 && processor.isPadLoading (12); ++wait)
            pump (50);
        for (int wait = 0; wait < 100 && processor.isPadLoading (15); ++wait)
            pump (50);

        const auto nameOf = [&] (int pad) { auto s = processor.getPadSample (pad); return s != nullptr ? s->getName() : juce::String ("(empty)"); };

        check (nameOf (12) == "Snare 1" && nameOf (13) == "Snare 2" && nameOf (14) == "Snare 3" && nameOf (15) == "Snare 4",
               "dropped files fill the pads from the drop, in name order",
               nameOf (12) + ", " + nameOf (13) + ", " + nameOf (14) + ", " + nameOf (15));
        check (processor.getPadSample (11) == nullptr, "and leave the pads before it alone");

        folder.deleteRecursively();
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File outputDirectory = argc > 1 ? juce::File (juce::String (argv[1]))
                                                : juce::File::getCurrentWorkingDirectory();

    if (! outputDirectory.createDirectory())
    {
        std::printf ("FAIL: cannot create %s\n", outputDirectory.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("Session round trip, swap, drag out...\n");
    checkStateRoundTrip();
    std::printf ("Sample-accurate hits...\n");
    checkSampleAccurateHits();
    std::printf ("Multi-output, learn...\n");
    checkMultiOutAndLearn();
    std::printf ("Dropped files...\n");
    checkDroppedFiles();

    // The panel, with a kit on it and a few pads mid-flash.
    {
        KitboxProcessor processor;
        processor.prepareToPlay (48000.0, 512);
        loadDemoKit (processor);

        setParam (processor, KitParams::padId (1, KitParams::Pad::filterType), 2.0f);
        setParam (processor, KitParams::padId (1, KitParams::Pad::cutoff), 3200.0f);
        setParam (processor, KitParams::padId (1, KitParams::Pad::resonance), 30.0f);
        setParam (processor, KitParams::padId (1, KitParams::Pad::envAmount), 24.0f);
        setParam (processor, KitParams::padId (1, KitParams::Pad::decay), 420.0f);
        setParam (processor, KitParams::padId (1, KitParams::Pad::start), 3.0f);
        setParam (processor, KitParams::padId (1, KitParams::Pad::sendReverb), 35.0f);
        setParam (processor, KitParams::Fx::ids[KitParams::Fx::reverb].type, 2.0f);   // Hall
        setParam (processor, KitParams::Fx::ids[KitParams::Fx::mod].type, 0.0f);      // Chorus
        setParam (processor, KitParams::padId (1, KitParams::Pad::pan), -0.15f);
        setParam (processor, KitParams::padId (2, KitParams::Pad::choke), 1.0f);   // closed and open hat
        setParam (processor, KitParams::padId (3, KitParams::Pad::choke), 1.0f);
        processor.setSelectedPad (1);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        pump (50);

        renderBlocks (processor, 4, [] (int block, juce::MidiBuffer& midi)
        {
            if (block == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, KitParams::firstNote + 0, (juce::uint8) 120), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, KitParams::firstNote + 2, (juce::uint8) 70), 10);
                midi.addEvent (juce::MidiMessage::noteOn (1, KitParams::firstNote + 9, (juce::uint8) 100), 20);
            }
        });

        pump (60);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = outputDirectory.getChildFile ("kitbox.png");
        check (writePng (image, file), "the panel renders to " + file.getFullPathName());

        // A kit left on disk for build.sh to hand to plutil: macOS's own
        // parser, not ours, decides whether the file is a valid preset.
        check (processor.saveKit (outputDirectory.getChildFile ("Demo Kit.aupreset")).wasOk(), "the demo kit saves");

        processor.editorBeingDeleted (editor.get());
        editor.reset();
    }

    std::printf ("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
