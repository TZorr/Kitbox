//
//  DrumVoice.cpp
//  Kitbox
//

#include "DrumVoice.h"

#include <cmath>

namespace
{
    constexpr float minus60dB = 0.001f;
    constexpr float silence   = 1.0e-5f;   // -100 dB: below this the voice is done
}

void DrumVoice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    stop();
}

void DrumVoice::start (int padIndex, SampleData::Ptr newSample, float velocity01,
                       const PadParams& padParams, const HumanizeOffsets& humanize,
                       uint64_t newAge)
{
    sample = std::move (newSample);

    if (sample == nullptr)
        return;

    data   = sample->getMono();
    length = sample->getLength();
    params = &padParams;
    pad    = padIndex;
    age    = newAge;

    // Latched: a voice belongs to the group its pad was in when it was hit.
    chokeGroup = juce::jlimit (0, KitParams::numChokeGroups, (int) std::lround (padParams.choke->load()));
    human  = humanize;

    const auto sourceRate = sample->getSampleRate();

    // Pitch: the rate conversion from the file's rate to the session's, and the
    // tuning, in one increment. Resampling happens here, on the fly, because the
    // voice has to interpolate for tuning anyway - converting at load time as
    // well would interpolate twice.
    const auto semitones = (double) padParams.tune->load()
                         + padParams.fine->load() / 100.0
                         + humanize.pitchCents / 100.0;
    increment = sourceRate / sampleRate * std::exp2 (semitones / 12.0);

    // Start. Humanize may move it a fraction of a millisecond either way, and
    // "earlier than the start of the file" is allowed: readSample() returns
    // silence before sample 0, so a negative start is a tiny late hit - which
    // is exactly what an offset in time from a human drummer is.
    const auto startPercent = padParams.start->load();
    position = startPercent / 100.0 * (length - 1) + humanize.startMs * 0.001 * sourceRate;
    position = juce::jlimit (-0.001 * HumanizeOffsets::maxStartMs * sourceRate, (double) (length - 1), position);

    // Velocity: sensitivity 0 plays every hit at full level, 100 % makes the
    // gain the square of the velocity - half velocity is -12 dB, the curve most
    // hardware drum modules default to.
    const auto sensitivity = padParams.velocity->load() / 100.0f;
    velocityGain = std::pow (juce::jlimit (0.0f, 1.0f, velocity01), 2.0f * sensitivity);

    // A start inside the sample lands mid-waveform. One millisecond of attack
    // is inaudible on a drum and removes the click.
    auto attackMs = padParams.attack->load();
    if (startPercent > 0.0f)
        attackMs = juce::jmax (attackMs, 1.0f);

    attackSamples = (int) std::lround (attackMs * 0.001 * sampleRate);
    holdSamples   = (int) std::lround (padParams.hold->load() * 0.001 * sampleRate);

    const auto decayMs = padParams.decay->load();
    decayCoefficient = decayMs >= KitParams::decayMaxMs - 0.5f
        ? 1.0f
        : (float) std::exp (std::log (minus60dB) / (decayMs * 0.001 * sampleRate));

    envAttackSamples = (int) std::lround (padParams.envAttack->load() * 0.001 * sampleRate);
    envDecaySamples  = (float) juce::jmax (1.0, padParams.envDecay->load() * 0.001 * sampleRate);

    if (attackSamples > 0)
    {
        stage      = Stage::attack;
        envelope   = 0.0f;
        attackStep = 1.0f / (float) attackSamples;
    }
    else
    {
        stage    = Stage::hold;
        envelope = 1.0f;
    }

    holdRemaining    = holdSamples;
    elapsed          = 0;
    controlCountdown = 0;
    fadeRemaining    = 0;
    fadeGain         = 1.0f;
    firstBlock       = true;
    filter.reset();
}

void DrumVoice::fadeOut (double milliseconds) noexcept
{
    if (! isActive() || isFading())
        return;

    fadeRemaining = juce::jmax (1, (int) (milliseconds * 0.001 * sampleRate));
    fadeStep      = fadeGain / (float) fadeRemaining;
}

void DrumVoice::stop() noexcept
{
    sample = nullptr;
    data   = nullptr;
    length = 0;
    pad    = -1;
    chokeGroup = 0;
    fadeRemaining = 0;
}

float DrumVoice::readSample() const noexcept
{
    // Four-point, third-order Hermite. Linear interpolation dulls a hi-hat
    // audibly when it is tuned; this does not, and costs a handful of
    // multiplies.
    const auto base = std::floor (position);
    const auto i    = (int) base;
    const auto f    = (float) (position - base);

    float xm1, x0, x1, x2;

    if (i >= 1 && i + 2 < length)
    {
        xm1 = data[i - 1]; x0 = data[i]; x1 = data[i + 1]; x2 = data[i + 2];
    }
    else
    {
        const auto at = [this] (int k) { return k >= 0 && k < length ? data[k] : 0.0f; };
        xm1 = at (i - 1); x0 = at (i); x1 = at (i + 1); x2 = at (i + 2);
    }

    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

    return ((c3 * f + c2) * f + c1) * f + x0;
}

float DrumVoice::filterEnvelopeAt (int64_t t) const noexcept
{
    if (t < envAttackSamples)
        return (float) t / (float) envAttackSamples;

    // The same -60 dB-at-the-decay-time definition as the amp envelope, so the
    // two Decay knobs mean the same thing.
    return std::exp (-6.9077553f * (float) (t - envAttackSamples) / envDecaySamples);
}

void DrumVoice::updateControl() noexcept
{
    const auto type = (KitParams::FilterType) juce::jlimit (0, 5, (int) params->filterType->load());
    filterOn = type != KitParams::FilterType::off;

    if (! filterOn)
        return;

    const auto sweep  = params->envAmount->load() * filterEnvelopeAt (elapsed);
    const auto cutoff = params->cutoff->load() * std::exp2 (sweep / 12.0f) * human.filterFactor;

    filter.set (type, cutoff, params->resonance->load() / 100.0f, params->drive->load() / 100.0f, sampleRate);
}

void DrumVoice::render (float* left, float* right,
                        float* sendReverb, float* sendDelay, float* sendPhaser,
                        int numSamples) noexcept
{
    if (! isActive() || numSamples <= 0)
        return;

    // Level, pan and sends are read live, once per block, and ramped across it.
    const auto level = levelToGain (params->level->load() + human.levelDb) * velocityGain;
    const auto pan   = juce::jlimit (-1.0f, 1.0f, params->pan->load() + human.pan);

    // Equal power: a centred pad sits at -3 dB in each side, and its loudness
    // does not change as it is panned.
    const auto angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

    const float targetLeft   = level * std::cos (angle);
    const float targetRight  = level * std::sin (angle);
    const float targetReverb = level * params->sendReverb->load() / 100.0f;
    const float targetDelay  = level * params->sendDelay->load() / 100.0f;
    const float targetPhaser = level * params->sendPhaser->load() / 100.0f;

    if (firstBlock)
    {
        gainLeft = targetLeft; gainRight = targetRight;
        gainReverb = targetReverb; gainDelay = targetDelay; gainPhaser = targetPhaser;
        firstBlock = false;
    }

    const auto inverse = 1.0f / (float) numSamples;
    const auto stepLeft   = (targetLeft   - gainLeft)   * inverse;
    const auto stepRight  = (targetRight  - gainRight)  * inverse;
    const auto stepReverb = (targetReverb - gainReverb) * inverse;
    const auto stepDelay  = (targetDelay  - gainDelay)  * inverse;
    const auto stepPhaser = (targetPhaser - gainPhaser) * inverse;

    bool finished = false;

    for (int i = 0; i < numSamples; ++i)
    {
        if (--controlCountdown < 0)
        {
            updateControl();
            controlCountdown = controlInterval - 1;
        }

        auto s = readSample();
        position += increment;

        if (filterOn)
            s = filter.process (s);

        switch (stage)
        {
            case Stage::attack:
                envelope += attackStep;
                if (envelope >= 1.0f)
                {
                    envelope = 1.0f;
                    stage = Stage::hold;
                }
                break;

            case Stage::hold:
                if (--holdRemaining < 0)
                    stage = Stage::decay;
                break;

            case Stage::decay:
                envelope *= decayCoefficient;
                break;
        }

        if (fadeRemaining > 0)
        {
            fadeGain -= fadeStep;
            if (--fadeRemaining == 0)
                finished = true;
        }

        const auto v = s * envelope * fadeGain;

        gainLeft += stepLeft; gainRight += stepRight;
        gainReverb += stepReverb; gainDelay += stepDelay; gainPhaser += stepPhaser;

        left[i]       += v * gainLeft;
        right[i]      += v * gainRight;
        sendReverb[i] += v * gainReverb;
        sendDelay[i]  += v * gainDelay;
        sendPhaser[i] += v * gainPhaser;

        ++elapsed;

        // Two samples past the end, so the interpolator's tail is played out.
        if (finished
            || position >= (double) (length + 2)
            || (stage == Stage::decay && envelope < silence))
        {
            finished = true;
            break;
        }
    }

    if (finished)
        stop();
}
