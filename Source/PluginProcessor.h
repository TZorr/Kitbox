//
//  PluginProcessor.h
//  Kitbox
//
//  The plugin: parameters, samples, state, and the engine they drive.
//
//  Threading, which is most of what this class is about:
//
//  - The audio thread runs processBlock and touches only the engine. Samples
//    reach it as raw pointers the engine reads atomically; see SamplePool for
//    why none is ever freed there.
//  - The loader thread reads and decodes dropped files, so a slow disk costs a
//    moment of "loading" on the pad and never a dropout.
//  - The Transmute workers (PadTransmuter) analyse, fit and render a pad's
//    synth; the result is installed on the message thread like a loaded file.
//  - Everything else - installing a decoded sample, swapping pads, saving and
//    loading kits - happens on the message thread.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Engine/DrumEngine.h"
#include "Engine/AuPreset.h"
#include "Engine/KitFile.h"
#include "Engine/PadSource.h"
#include "Engine/PadTransmuter.h"
#include "ParameterIds.h"

class KitboxProcessor : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster,
                        private juce::Timer,
                        private juce::AsyncUpdater
{
public:
    KitboxProcessor();
    ~KitboxProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    /** The loaded kit's name - what Logic shows in its settings box. */
    const juce::String getProgramName (int) override { return getKitName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    //  Message thread

    /** Starts loading files onto consecutive pads from `pad`, in name order.
        Files beyond pad 16 are dropped. Returns immediately. */
    void loadFiles (int pad, juce::Array<juce::File> files);

    void clearPad (int pad);

    /** Renames the pad's sample; an empty pad or an empty name is ignored. */
    void renamePad (int pad, const juce::String& newName);

    /** Exchanges two pads: samples and every per-pad knob. */
    void swapPads (int a, int b);

    /** What the pad plays: its sample, or its synth when the pad is set to one. */
    SampleData::Ptr getPadSample (int pad) const { return padSamples[(size_t) pad]; }
    bool isPadLoading (int pad) const noexcept   { return loadingCount[(size_t) pad] > 0; }
    const juce::String& getPadError (int pad) const noexcept { return padErrors[(size_t) pad]; }

    /** Plays one pad - not its note, which may be shared. Safe from the
        message thread; the hit lands at the start of the next block. */
    void playPad (int pad, float velocity);

    int getPadNote (int pad) const;

    /** MIDI Learn: the next note-on the plugin receives becomes this pad's
        note. The note is taken on the audio thread and written to the
        parameter on the message thread by the timer. */
    void startLearn (int pad);
    void cancelLearn();
    int getLearnPad() const noexcept { return learnPad.load(); }

    /** Message thread. Applies a note the audio thread has learnt. Called by
        the timer; public so tests need not wait for it. */
    void applyLearnedNote();

    /** Kits are .aupreset files (see AuPreset.h); loadKit also reads the
        older bare .kitbox container. */
    juce::Result saveKit (const juce::File& file);
    juce::Result loadKit (const juce::File& file);

    juce::String getKitName() const;
    void setKitName (const juce::String& name);

    /** The file a drag out of a pad hands over: the pad's original file,
        written to a temporary folder under its own name. */
    juce::File writeSampleForDrag (int pad);

    /** Export Samples: every pad's sample into folder/Original, every synth
        and its .drumparams into folder/Synth, named as inside the kit
        container ("01 Kick.wav", see KitFile.h). Files already there under
        those names are replaced; nothing else in the folder is touched. */
    juce::Result exportSamples (const juce::File& folder);
    bool hasAnySample() const;

    int getSelectedPad() const;
    void setSelectedPad (int pad);

    juce::String getAudioFileWildcard() const { return formats.getWildcardForAllFormats(); }
    bool isAudioFile (const juce::File& file) const;

    /** Installs an already decoded sample. Used by loads and by the tests.
        The pad's synth goes with the old sample; a pad set to Auto or a model
        starts fitting the new one. */
    void setPadSample (int pad, SampleData::Ptr sample);

    //==============================================================================
    //  Transmute (message thread)

    PadSource getPadSource (int pad) const noexcept { return padSource[(size_t) pad]; }

    /** Sample, Auto or a model, from the pad's menu. A synth already made for
        that choice plays at once; otherwise the pad plays its sample until the
        fit is done. */
    void setPadSource (int pad, PadSource source);

    /** SYN: every pad holding a sample Transmute can model goes to Auto. */
    void transmuteAll();

    /** A sample of at most 10 s is on the pad. */
    bool canTransmute (int pad) const noexcept { return PadTransmuter::canTransmute (padOriginals[(size_t) pad].get()); }
    bool isPadSynth (int pad) const noexcept;
    /** 0...1 while the pad fits, -1 otherwise. */
    float getFitProgress (int pad) const { return transmuter.getProgress (pad); }

    SampleData::Ptr getPadOriginal (int pad) const { return padOriginals[(size_t) pad]; }
    SampleData::Ptr getPadSynth (int pad) const    { return padSynths[(size_t) pad]; }
    const std::optional<transmute::DrumParams>& getPadSynthParams (int pad) const { return padSynthParams[(size_t) pad]; }
    /** The model the pad's sample suggests, once it has been analysed. */
    const std::optional<transmute::DrumModel>& getSuggestedModel (int pad) const { return padSuggested[(size_t) pad]; }
    const juce::String& getSynthError (int pad) const noexcept { return synthErrors[(size_t) pad]; }

    /** SYN's pass: pads done of the pads it started; total 0 when none runs. */
    struct SynProgress { int done = 0, total = 0; };
    SynProgress getSynProgress() const;

    /** Installs finished fits now rather than on the async update. For tests. */
    void takeTransmuteResults();

    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;

    const DrumEngine& getEngine() const noexcept { return engine; }

    /** Releases retired samples now rather than on the timer. For tests. */
    void collectSamples (double graceMs) { pool.collect (graceMs); }
    size_t getPoolSize() const noexcept  { return pool.size(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    static BusesProperties makeBuses();

    juce::Result applyKit (const KitFile::Contents& kit);
    KitFile::Synths synthsForSaving() const;

    /** Puts `sample` on the engine for the pad, keeping the pool's books. */
    void play (int pad, SampleData::Ptr sample);
    /** Plays the synth if it is the pad's choice and made for it, else the sample. */
    void activate (int pad);
    bool synthMatches (int pad) const;
    void requestFit (int pad);
    void storePadSource (int pad);

    void timerCallback() override;
    void handleAsyncUpdate() override;

    //==============================================================================
    /** Reads and decodes files off the message thread. */
    class Loader : public juce::Thread
    {
    public:
        explicit Loader (KitboxProcessor& ownerToUse);
        ~Loader() override;

        void add (int pad, const juce::File& file);
        void stop();

        struct Result
        {
            int pad;
            SampleData::Ptr sample;
            juce::String error;
        };

        std::vector<Result> takeResults();

    private:
        void run() override;

        KitboxProcessor& owner;
        juce::CriticalSection lock;
        std::vector<std::pair<int, juce::File>> jobs;
        std::vector<Result> results;
        juce::WaitableEvent wake;
    };

    //==============================================================================
    DrumEngine engine;
    juce::AudioFormatManager formats;
    SamplePool pool;

    std::array<SampleData::Ptr, KitParams::numPads> padSamples;     // what plays
    std::array<SampleData::Ptr, KitParams::numPads> padOriginals;   // the sample as loaded
    std::array<SampleData::Ptr, KitParams::numPads> padSynths;      // its Transmute synth
    std::array<std::optional<transmute::DrumParams>, KitParams::numPads> padSynthParams;
    std::array<std::optional<transmute::DrumModel>, KitParams::numPads> padSuggested;
    std::array<PadSource, KitParams::numPads> padSource {};
    std::array<int, KitParams::numPads> padGeneration {};
    std::array<juce::String, KitParams::numPads> synthErrors;
    std::array<bool, KitParams::numPads> synBatch {};
    int synTotal = 0;
    std::array<int, KitParams::numPads> loadingCount {};
    std::array<juce::String, KitParams::numPads> padErrors;

    std::array<std::atomic<float>, KitParams::numPads> pendingHits;
    std::atomic<int> learnPad { -1 };
    std::atomic<int> learnedAssignment { -1 };   // (pad << 8) | note

    Loader loader { *this };
    PadTransmuter transmuter { [this] { triggerAsyncUpdate(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitboxProcessor)
};
