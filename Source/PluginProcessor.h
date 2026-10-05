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
//  - Everything else - installing a decoded sample, swapping pads, saving and
//    loading kits - happens on the message thread.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Engine/DrumEngine.h"
#include "Engine/AuPreset.h"
#include "Engine/KitFile.h"
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

    int getSelectedPad() const;
    void setSelectedPad (int pad);

    juce::String getAudioFileWildcard() const { return formats.getWildcardForAllFormats(); }
    bool isAudioFile (const juce::File& file) const;

    /** Installs an already decoded sample. Used by loads and by the tests. */
    void setPadSample (int pad, SampleData::Ptr sample);

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

    std::array<SampleData::Ptr, KitParams::numPads> padSamples;
    std::array<int, KitParams::numPads> loadingCount {};
    std::array<juce::String, KitParams::numPads> padErrors;

    std::array<std::atomic<float>, KitParams::numPads> pendingHits;
    std::atomic<int> learnPad { -1 };
    std::atomic<int> learnedAssignment { -1 };   // (pad << 8) | note

    Loader loader { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitboxProcessor)
};
