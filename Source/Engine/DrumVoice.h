//
//  DrumVoice.h
//  Kitbox
//
//  One hit, from the first sample to silence.
//
//      sample -> pitch -> filter (+ filter envelope) -> amp envelope
//             -> level / pan -> dry L/R, and the three mono sends
//
//  Everything before the pan is one channel. The voice writes its dry signal
//  into the stereo mix and the same mono signal, post-level and pre-pan, into
//  each send bus - so a pad panned hard left still reaches the reverb at full
//  strength, which is what a send on a mixing desk does.
//
//  Hits are one-shots: a note-off is ignored, the envelope and the end of the
//  sample decide when a voice stops. That is how drum machines behave and what
//  a drummer playing short notes from a pad controller expects.
//

#pragma once

#include "SampleData.h"
#include "PadParams.h"
#include "TptFilter.h"

/** The per-hit deviations of the "analog" engine, drawn once per trigger.
    At Humanize 100 % each is uniform within the range the panel promises:
    pitch +-6 ct, filter +-10 %, level +-2 dB, start +-1 ms, pan +-4 %.
    50 % (the start) is what 100 % was before 0.6.0; old kits are halved on
    load so they sound as they did (see KitboxProcessor::applyKit). */
struct HumanizeOffsets
{
    float pitchCents   = 0.0f;
    float filterFactor = 1.0f;
    float levelDb      = 0.0f;
    float startMs      = 0.0f;
    float pan          = 0.0f;

    static constexpr float maxPitchCents  = 6.0f;
    static constexpr float maxFilter      = 0.10f;
    static constexpr float maxLevelDb     = 2.0f;
    static constexpr float maxStartMs     = 1.0f;
    static constexpr float maxPan         = 0.04f;

    /** amount 0..1; random() returns uniform -1..1. */
    template <typename Random>
    static HumanizeOffsets draw (float amount, Random&& random)
    {
        HumanizeOffsets h;

        if (amount <= 0.0f)
            return h;

        h.pitchCents   = amount * maxPitchCents * random();
        h.filterFactor = 1.0f + amount * maxFilter * random();
        h.levelDb      = amount * maxLevelDb * random();
        h.startMs      = amount * maxStartMs * random();
        h.pan          = amount * maxPan * random();
        return h;
    }
};

class DrumVoice
{
public:
    void prepare (double sampleRate);

    /** Audio thread. Latches everything read at the hit. */
    void start (int padIndex, SampleData::Ptr sample, float velocity01,
                const PadParams& params, const HumanizeOffsets& humanize,
                uint64_t age);

    /** A short fade to silence: 2 ms for a voice being stolen, 5 ms for one
        being choked. */
    void fadeOut (double milliseconds = 2.0) noexcept;

    /** Adds this voice into the outputs. Clears itself when it has finished. */
    void render (float* left, float* right,
                 float* sendReverb, float* sendDelay, float* sendPhaser,
                 int numSamples) noexcept;

    bool isActive() const noexcept     { return sample != nullptr; }
    bool isFading() const noexcept     { return fadeRemaining > 0; }
    int getPad() const noexcept        { return pad; }
    int getChokeGroup() const noexcept { return chokeGroup; }
    uint64_t getAge() const noexcept   { return age; }

    /** Frees the sample reference. Called by the engine outside the render
        loop's hot path; the pool guarantees this never deletes. */
    void stop() noexcept;

private:
    static constexpr int controlInterval = 16;   // samples between filter updates

    float readSample() const noexcept;
    float filterEnvelopeAt (int64_t sampleIndex) const noexcept;
    void  updateControl() noexcept;

    double sampleRate = 48000.0;

    SampleData::Ptr sample;
    const float* data = nullptr;
    int length = 0;
    const PadParams* params = nullptr;
    int pad = -1;
    int chokeGroup = 0;
    uint64_t age = 0;

    // Latched at the hit
    double position = 0.0, increment = 1.0;
    float velocityGain = 1.0f;
    HumanizeOffsets human;
    int attackSamples = 0, holdSamples = 0;
    float decayCoefficient = 1.0f;
    int envAttackSamples = 0;
    float envDecaySamples = 1.0f;

    // Running
    enum class Stage { attack, hold, decay };
    Stage stage = Stage::attack;
    float envelope = 0.0f, attackStep = 1.0f;
    int holdRemaining = 0;
    int64_t elapsed = 0;
    int controlCountdown = 0;

    int fadeRemaining = 0;
    float fadeStep = 0.0f, fadeGain = 1.0f;

    TptFilter filter;
    bool filterOn = false;

    // Per-block gains, ramped from the previous block's values
    float gainLeft = 0.0f, gainRight = 0.0f;
    float gainReverb = 0.0f, gainDelay = 0.0f, gainPhaser = 0.0f;
    bool firstBlock = true;
};
