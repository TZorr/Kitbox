//
//  SampleData.h
//  Kitbox
//
//  One loaded sample: the file exactly as it arrived, and the mono audio the
//  voices play.
//
//  Both are kept, and for different reasons. The mono buffer is what the
//  engine needs - summed once, here, so no voice ever carries a second
//  channel. The original bytes are what the user gave us, and three features
//  need them untouched: the kit container stores them, the DAW session embeds
//  them, and dragging a pad out onto the desktop hands back the original file,
//  stereo and all, not our mono fold of it.
//
//  Samples are shared between the pad that owns one and every voice still
//  playing it, and the last of those to let go may be a voice on the audio
//  thread. A delete there would be a free() in the render callback, so nothing
//  is ever deleted there: SamplePool holds one reference to every sample until
//  the message thread sees that it is the only holder left.
//

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

class SampleData : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    /** Longest sample a pad accepts. A drum pad is not a clip player, and the
        limit keeps an accidental drop of a whole song from embedding a hundred
        megabytes into the session. */
    static constexpr double maxSeconds = 60.0;

    /** Decodes a file's bytes. Returns null and says why in `error` if the
        bytes are not audio any registered format can read, or too long. */
    static Ptr decode (juce::MemoryBlock originalBytes,
                       const juce::String& fileName,
                       juce::AudioFormatManager& formats,
                       juce::String& error);

    /** Reads a file from disk and decodes it. Not for the audio thread. */
    static Ptr fromFile (const juce::File& file,
                         juce::AudioFormatManager& formats,
                         juce::String& error);

    /** A sample made directly from audio, for tests and the demo kit. The
        "original" is a WAV of the same audio, so every path that needs one
        still has one. */
    static Ptr fromAudio (const juce::AudioBuffer<float>& audio, double sampleRate,
                          const juce::String& name);

    //==============================================================================
    const juce::String& getName() const noexcept         { return name; }
    const juce::String& getFileName() const noexcept     { return fileName; }
    const juce::MemoryBlock& getOriginal() const noexcept { return original; }

    /** Gives the sample a new name; the file name follows it and keeps its
        extension, so a saved kit and a drag out of the pad carry the new
        name too. Message thread only - the audio thread never reads either. */
    void rename (const juce::String& newName);

    const float* getMono() const noexcept    { return mono.getReadPointer (0); }
    int getLength() const noexcept           { return mono.getNumSamples(); }
    double getSampleRate() const noexcept    { return sampleRate; }
    int getSourceChannels() const noexcept   { return sourceChannels; }
    double getSeconds() const noexcept       { return getLength() / sampleRate; }

    /** Peak min/max per column, for drawing. overviewSize columns. */
    static constexpr int overviewSize = 512;
    const std::vector<std::pair<float, float>>& getOverview() const noexcept { return overview; }

private:
    SampleData() = default;

    void buildOverview();

    juce::String name, fileName;
    juce::MemoryBlock original;
    juce::AudioBuffer<float> mono;
    double sampleRate = 44100.0;
    int sourceChannels = 1;
    std::vector<std::pair<float, float>> overview;
};

//==============================================================================
/**
    Keeps every sample alive until nothing but the pool refers to it.

    Message thread only. collect() is called from a timer; a sample is released
    once it has been retired for a while *and* its reference count shows the
    pool is its last holder. The delay covers the one gap reference counting
    cannot: the audio thread reads a pad's raw pointer and takes its reference a
    few instructions later.
*/
class SamplePool
{
public:
    /** Adds a sample, or un-retires it if the pool already holds it. */
    void add (SampleData::Ptr sample)
    {
        if (sample == nullptr)
            return;

        for (auto& entry : entries)
        {
            if (entry.sample == sample)
            {
                entry.retiredAt = 0.0;
                return;
            }
        }

        entries.push_back ({ std::move (sample), 0.0 });
    }

    /** Marks a sample as no longer owned by a pad. */
    void retire (const SampleData* sample)
    {
        for (auto& entry : entries)
            if (entry.sample.get() == sample && entry.retiredAt == 0.0)
                entry.retiredAt = juce::Time::getMillisecondCounterHiRes();
    }

    /** Frees retired samples no voice is playing any more. */
    void collect (double graceMs = 2000.0)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();

        std::erase_if (entries, [now, graceMs] (const Entry& entry)
        {
            return entry.retiredAt > 0.0
                && now - entry.retiredAt >= graceMs
                && entry.sample->getReferenceCount() == 1;
        });
    }

    size_t size() const noexcept { return entries.size(); }

private:
    struct Entry
    {
        SampleData::Ptr sample;
        double retiredAt;
    };

    std::vector<Entry> entries;
};
