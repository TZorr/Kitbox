//
//  PadTransmuter.h
//  Kitbox
//
//  Turns a pad's sample into Transmute's synthesised version of it, off the
//  message thread: decode the pad's original file, bring it to 48 kHz the way
//  Transmute does, analyse it (as a chosen model, or as the one it suggests),
//  fit, and render the result at the sample's own rate (as Transmute's export
//  does) into a SampleData the engine plays like any other.
//
//  A fit takes one to four seconds on one core plus whatever its grids borrow.
//  Several pads fit at once - as many as the machine has cores less two, at
//  low priority, because the host is playing while it happens.
//
//  Results come back through `resultsReady` (from a worker thread) and
//  takeResults() (on the message thread). Each request carries the pad's
//  generation; the processor drops a result whose pad has moved on since -
//  cleared, reloaded, swapped. Starting a pad again, or cancelling it, also
//  stops its running fit at the next 20 evaluations.
//

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "SampleData.h"
#include "../ParameterIds.h"
#include "../Transmute/DrumParams.h"

class PadTransmuter
{
public:
    /** Longest sample Transmute models: a single hit (its Decoder's limit). */
    static constexpr double maxSeconds = 10.0;

    struct Request
    {
        int pad = 0;
        int generation = 0;
        SampleData::Ptr original;
        std::optional<transmute::DrumModel> model;   // empty: the model the hit suggests
    };

    struct Result
    {
        int pad = 0;
        int generation = 0;
        SampleData::Ptr synth;                        // null on failure or cancel
        std::optional<transmute::DrumParams> params;
        transmute::DrumModel suggested = transmute::DrumModel::kick;
        juce::String error;
        bool cancelled = false;
    };

    explicit PadTransmuter (std::function<void()> resultsReady);
    ~PadTransmuter();

    //==============================================================================
    //  Message thread
    void start (Request toStart);
    void cancel (int pad);
    void cancelAll();
    /** Cancels everything and waits for the workers: nothing calls back after it. */
    void shutdown();
    std::vector<Result> takeResults();

    /** 0...1 while the pad's latest request is queued or fitting; -1 when idle. */
    float getProgress (int pad) const;
    bool isBusy (int pad) const { return jobs[(size_t) pad] != nullptr; }

    /** Marks the pad idle once its result has been taken. */
    void finished (int pad, int generation);

    //==============================================================================
    static bool canTransmute (const SampleData* sample) noexcept
    {
        return sample != nullptr && sample->getSeconds() <= maxSeconds;
    }

    /** The rate the synth is rendered at: the sample's own, inside 22.05-192 kHz. */
    static double renderRate (double sourceRate) noexcept
    {
        return sourceRate >= 22050.0 && sourceRate <= 192000.0 ? sourceRate : 48000.0;
    }

    /** The whole job, synchronously. Public for the tests. */
    static Result run (const Request& request, const std::atomic<bool>& cancel,
                       const std::function<void (double)>& progress);

private:
    struct Job
    {
        int generation = 0;
        std::atomic<bool> cancel { false };
        std::atomic<float> progress { 0.0f };
    };

    std::function<void()> resultsReady;
    std::array<std::shared_ptr<Job>, KitParams::numPads> jobs;   // message thread only

    juce::CriticalSection lock;
    std::vector<Result> results;

    juce::ThreadPool pool;

    JUCE_DECLARE_NON_COPYABLE (PadTransmuter)
};
