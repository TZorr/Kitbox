//
//  PadTransmuter.cpp
//  Kitbox
//

#include "PadTransmuter.h"

#include "../Transmute/AnalysisInput.h"
#include "../Transmute/Analyzer.h"
#include "../Transmute/DrumSynth.h"
#include "../Transmute/Fitter.h"

namespace
{
    int fitThreads()
    {
        return juce::jmax (1, juce::SystemStats::getNumCpus() - 2);
    }
}

PadTransmuter::PadTransmuter (std::function<void()> ready)
    : resultsReady (std::move (ready)),
      pool (juce::ThreadPoolOptions{}
                .withThreadName ("Kitbox Transmute")
                .withNumberOfThreads (fitThreads())
                .withDesiredThreadPriority (juce::Thread::Priority::low))
{
}

PadTransmuter::~PadTransmuter()
{
    shutdown();
}

void PadTransmuter::shutdown()
{
    cancelAll();
    pool.removeAllJobs (true, 30000);
}

void PadTransmuter::start (Request toStart)
{
    const auto pad = toStart.pad;
    cancel (pad);

    auto job = std::make_shared<Job>();
    job->generation = toStart.generation;
    jobs[(size_t) pad] = job;

    pool.addJob ([this, job, request = std::move (toStart)]
    {
        auto result = run (request, job->cancel, [&job] (double fraction) { job->progress.store ((float) fraction); });

        {
            const juce::ScopedLock sl (lock);
            results.push_back (std::move (result));
        }

        if (resultsReady)
            resultsReady();

        return juce::ThreadPoolJob::jobHasFinished;
    });
}

void PadTransmuter::cancel (int pad)
{
    if (auto& job = jobs[(size_t) pad])
    {
        job->cancel.store (true);
        job.reset();
    }
}

void PadTransmuter::cancelAll()
{
    for (int pad = 0; pad < KitParams::numPads; ++pad)
        cancel (pad);
}

std::vector<PadTransmuter::Result> PadTransmuter::takeResults()
{
    const juce::ScopedLock sl (lock);
    return std::exchange (results, {});
}

float PadTransmuter::getProgress (int pad) const
{
    const auto& job = jobs[(size_t) pad];
    return job != nullptr ? job->progress.load() : -1.0f;
}

void PadTransmuter::finished (int pad, int generation)
{
    if (jobs[(size_t) pad] != nullptr && jobs[(size_t) pad]->generation == generation)
        jobs[(size_t) pad].reset();
}

//==============================================================================
PadTransmuter::Result PadTransmuter::run (const Request& request, const std::atomic<bool>& cancel,
                                          const std::function<void (double)>& progress)
{
    Result result;
    result.pad = request.pad;
    result.generation = request.generation;

    const auto fail = [&result] (const juce::String& error)
    {
        result.error = error;
        return result;
    };

    if (request.original == nullptr)
        return fail ("The pad is empty.");

    // The original file again, every channel: Transmute averages after
    // resampling, and the mono the pad plays was averaged before.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (request.original->getOriginal(), false)));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0)
        return fail ("The sample could not be read again.");

    if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
        return fail ("Longer than 10 s - Transmute models a single hit.");

    const auto length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> source ((int) reader->numChannels, length);

    if (! reader->read (&source, 0, length, 0, true, true))
        return fail ("The sample could not be read to the end.");

    std::vector<const float*> channels;
    for (int c = 0; c < source.getNumChannels(); ++c)
        channels.push_back (source.getReadPointer (c));

    const auto audio = transmute::toAnalysisRate (channels, length, reader->sampleRate);

    if (audio.empty())
        return fail ("The sample could not be resampled for the analysis.");

    if (cancel.load())
    {
        result.cancelled = true;
        return result;
    }

    std::string error;
    const auto analysis = transmute::Analyzer::analyze (audio, transmute::analysisRate, request.model, error);

    if (! analysis)
        return fail (error);

    result.suggested = analysis->suggested;

    const auto report = transmute::Fitter::fit (*analysis, progress, &cancel);

    if (! report)
    {
        result.cancelled = true;
        return result;
    }

    const auto rate = renderRate (reader->sampleRate);
    const auto rendered = transmute::DrumSynth::render (report->params, rate);

    juce::AudioBuffer<float> buffer (1, (int) rendered.size());
    std::copy (rendered.begin(), rendered.end(), buffer.getWritePointer (0));

    // 24-bit like every sample Kitbox makes, unless the synth peaks over full
    // scale - then float, which keeps it, rather than a clipped file.
    const auto bits = buffer.getMagnitude (0, buffer.getNumSamples()) > 1.0f ? 32 : 24;

    result.synth = SampleData::fromAudio (buffer, rate, request.original->getName(), bits);
    result.params = report->params;

    if (result.synth == nullptr)
        return fail ("The synth could not be stored.");

    return result;
}
