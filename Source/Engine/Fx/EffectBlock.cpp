//
//  EffectBlock.cpp
//  Kitbox (copied from Rackbox - fix bugs in both)
//

#include "EffectBlock.h"

void EffectBlock::prepare (double newSampleRate, int newMaxBlock)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (1, newMaxBlock);

    for (int i = 0; i < EffectCatalog::numEffects; ++i)
    {
        effects[(size_t) i] = EffectCatalog::create (i);
        effects[(size_t) i]->prepare (sampleRate, maxBlock);
    }

    slotBuffers.assign ((size_t) (EffectCatalog::numEffects * 2 * maxBlock), 0.0f);
    dry.assign ((size_t) (2 * maxBlock), 0.0f);
    mix.reset (sampleRate, 0.02);

    reset();
}

void EffectBlock::reset()
{
    for (auto& e : effects)
        if (e != nullptr)
            e->reset();

    active.fill (false);
    gain.fill (0.0f);
    initialised = false;
    fading = false;
    mix.setCurrentAndTargetValue (mix.getTargetValue());
}

void EffectBlock::activate (int slot, const Settings& settings) noexcept
{
    if (slot == drySlot || active[(size_t) slot])
        return;

    // A deactivated effect was reset when it went quiet, so only its parameters
    // need to jump to where the knobs are now.
    effects[(size_t) slot]->setParameters (settings.a, settings.b);
    effects[(size_t) slot]->setTiming (settings.sync, settings.bpm, settings.hasPosition, settings.ppq);
    effects[(size_t) slot]->setPreDelay (settings.preDelay, settings.beatsPerBar);
    effects[(size_t) slot]->snapParameters();
    active[(size_t) slot] = true;
}

void EffectBlock::deactivate (int slot) noexcept
{
    if (slot == drySlot || ! active[(size_t) slot])
        return;

    effects[(size_t) slot]->reset();
    active[(size_t) slot] = false;
}

void EffectBlock::process (float* left, float* right, int n, const Settings& settings) noexcept
{
    auto current = settings;

    while (n > 0)
    {
        const auto chunk = juce::jmin (n, maxBlock);
        processChunk (left, right, chunk, current);

        // The next piece starts later in the song.
        current.ppq += (double) chunk * juce::jlimit (20.0, 999.0, settings.bpm) / (60.0 * sampleRate);
        left += chunk;
        right += chunk;
        n -= chunk;
    }
}

void EffectBlock::processChunk (float* left, float* right, int n, const Settings& settings) noexcept
{
    const auto wanted = settings.bypass ? drySlot : EffectCatalog::index (settings.category, settings.subtype);

    mix.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.mix));

    if (! initialised)
    {
        // The first block after prepare or reset takes its state without a fade;
        // there is nothing to fade from.
        active.fill (false);
        gain.fill (0.0f);
        target = wanted;
        gain[(size_t) target] = 1.0f;
        activate (target, settings);
        mix.setCurrentAndTargetValue (mix.getTargetValue());
        initialised = true;
        fading = false;
    }
    else if (wanted != target)
    {
        target = wanted;
        activate (target, settings);
        fading = true;
    }

    for (int slot = 0; slot < EffectCatalog::numEffects; ++slot)
        if (active[(size_t) slot])
        {
            // Only the effect being faded to follows the knobs. One fading out keeps the knobs
            // it had: A and B mean something else in the next effect (a delay's Time is a
            // reverb's Size), and handing them over would bend or jump it during the fade.
            if (slot == target)
                effects[(size_t) slot]->setParameters (settings.a, settings.b);

            effects[(size_t) slot]->setTiming (settings.sync, settings.bpm, settings.hasPosition, settings.ppq);
            effects[(size_t) slot]->setPreDelay (settings.preDelay, settings.beatsPerBar);
        }

    if (! fading && target == drySlot)
    {
        mix.skip (n);
        return;
    }

    float* dryLeft = dry.data();
    float* dryRight = dry.data() + maxBlock;

    juce::FloatVectorOperations::copy (dryLeft, left, n);
    juce::FloatVectorOperations::copy (dryRight, right, n);

    if (! fading)
    {
        effects[(size_t) target]->process (left, right, n);

        for (int i = 0; i < n; ++i)
        {
            const auto m = mix.getNextValue();

            if (m < 1.0f)
            {
                left[i]  = dryLeft[i]  + m * (left[i]  - dryLeft[i]);
                right[i] = dryRight[i] + m * (right[i] - dryRight[i]);
            }
        }

        return;
    }

    // Fading: every active effect works on its own copy of the input.
    for (int slot = 0; slot < EffectCatalog::numEffects; ++slot)
    {
        if (! active[(size_t) slot])
            continue;

        juce::FloatVectorOperations::copy (scratch (slot, 0), dryLeft, n);
        juce::FloatVectorOperations::copy (scratch (slot, 1), dryRight, n);
        effects[(size_t) slot]->process (scratch (slot, 0), scratch (slot, 1), n);
    }

    const auto step = (float) (1.0 / (fadeSeconds * sampleRate));

    for (int i = 0; i < n; ++i)
    {
        float sumLeft = 0.0f, sumRight = 0.0f, total = 0.0f;

        for (int slot = 0; slot < numSlots; ++slot)
        {
            auto& g = gain[(size_t) slot];
            g = slot == target ? juce::jmin (1.0f, g + step) : juce::jmax (0.0f, g - step);

            if (g <= 0.0f)
                continue;

            total += g;

            if (slot == drySlot)
            {
                sumLeft  += g * dryLeft[i];
                sumRight += g * dryRight[i];
            }
            else
            {
                sumLeft  += g * scratch (slot, 0)[i];
                sumRight += g * scratch (slot, 1)[i];
            }
        }

        const auto yLeft  = sumLeft / total;
        const auto yRight = sumRight / total;
        const auto m = mix.getNextValue();

        left[i]  = m < 1.0f ? dryLeft[i]  + m * (yLeft  - dryLeft[i])  : yLeft;
        right[i] = m < 1.0f ? dryRight[i] + m * (yRight - dryRight[i]) : yRight;
    }

    // Retire what has faded out; the fade is over when only the target is left.
    bool othersLeft = false;

    for (int slot = 0; slot < numSlots; ++slot)
    {
        if (slot == target)
            continue;

        if (gain[(size_t) slot] <= 0.0f)
            deactivate (slot);
        else
            othersLeft = true;
    }

    if (! othersLeft && gain[(size_t) target] >= 1.0f)
        fading = false;
}
