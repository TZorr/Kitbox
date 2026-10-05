//
//  SampleData.cpp
//  Kitbox
//

#include "SampleData.h"

SampleData::Ptr SampleData::decode (juce::MemoryBlock originalBytes,
                                    const juce::String& fileName,
                                    juce::AudioFormatManager& formats,
                                    juce::String& error)
{
    if (originalBytes.getSize() == 0)
    {
        error = "The file is empty.";
        return nullptr;
    }

    // The stream does not own the bytes: they stay in originalBytes, which
    // outlives the reader and then moves into the sample.
    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (originalBytes, false)));

    if (reader == nullptr)
    {
        error = "Not an audio file Kitbox can read.";
        return nullptr;
    }

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        error = "The file holds no audio.";
        return nullptr;
    }

    if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
    {
        error = "Longer than " + juce::String ((int) maxSeconds) + " seconds - a pad plays one-shots.";
        return nullptr;
    }

    const auto length   = (int) reader->lengthInSamples;
    const auto channels = (int) reader->numChannels;

    juce::AudioBuffer<float> source (channels, length);

    if (! reader->read (&source, 0, length, 0, true, true))
    {
        error = "The file could not be read to the end.";
        return nullptr;
    }

    Ptr sample (new SampleData());
    sample->fileName       = fileName;
    sample->name           = juce::File::createFileWithoutCheckingPath ("/" + fileName).getFileNameWithoutExtension().trim();
    sample->sampleRate     = reader->sampleRate;
    sample->sourceChannels = channels;
    sample->mono.setSize (1, length);

    // The mono fold is the average, not the sum: a stereo file whose sides are
    // identical comes out at the level it went in, which is what somebody
    // auditioning the file in the Finder heard.
    sample->mono.clear();

    for (int channel = 0; channel < channels; ++channel)
        sample->mono.addFrom (0, 0, source, channel, 0, length, 1.0f / (float) channels);

    sample->original = std::move (originalBytes);
    sample->buildOverview();

    return sample;
}

void SampleData::rename (const juce::String& newName)
{
    const auto trimmed = newName.trim();

    if (trimmed.isEmpty())
        return;

    auto extension = juce::File::createFileWithoutCheckingPath ("/" + fileName).getFileExtension();
    if (extension.isEmpty())
        extension = ".wav";

    name = trimmed;
    fileName = trimmed + extension;
}

SampleData::Ptr SampleData::fromFile (const juce::File& file,
                                      juce::AudioFormatManager& formats,
                                      juce::String& error)
{
    // A size check before reading: a 60 s stereo 32-bit float WAV at 192 kHz is
    // about 92 MB, so anything well past that cannot be a pad sample.
    if (file.getSize() > 256 * 1024 * 1024)
    {
        error = "Too large for a pad sample.";
        return nullptr;
    }

    juce::MemoryBlock bytes;

    if (! file.loadFileAsData (bytes))
    {
        error = "The file could not be opened.";
        return nullptr;
    }

    return decode (std::move (bytes), file.getFileName(), formats, error);
}

SampleData::Ptr SampleData::fromAudio (const juce::AudioBuffer<float>& audio, double rate,
                                       const juce::String& sampleName)
{
    juce::MemoryBlock wavBytes;

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (wavBytes, false);

        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (rate)
                                                       .withNumChannels (audio.getNumChannels())
                                                       .withBitsPerSample (24));
        jassert (writer != nullptr);

        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }   // the writer finishes the header when it goes out of scope

    juce::AudioFormatManager formats;
    formats.registerFormat (new juce::WavAudioFormat(), true);

    juce::String error;
    auto sample = decode (std::move (wavBytes), sampleName + ".wav", formats, error);
    jassert (sample != nullptr);
    return sample;
}

void SampleData::buildOverview()
{
    overview.assign (overviewSize, { 0.0f, 0.0f });

    const auto length = getLength();
    const auto* data  = getMono();

    for (int column = 0; column < overviewSize; ++column)
    {
        const auto begin = (int) ((int64_t) column * length / overviewSize);
        const auto end   = juce::jmax (begin + 1, (int) ((int64_t) (column + 1) * length / overviewSize));

        float low = 0.0f, high = 0.0f;

        for (int i = begin; i < juce::jmin (end, length); ++i)
        {
            low  = juce::jmin (low, data[i]);
            high = juce::jmax (high, data[i]);
        }

        overview[(size_t) column] = { low, high };
    }
}
