//
//  DrumEngine.cpp
//  Kitbox
//

#include "DrumEngine.h"

DrumEngine::DrumEngine()
{
    for (auto& p : padSamples)   p.store (nullptr);
    for (auto& h : hitCount)     h.store (0);
    for (auto& v : lastVelocity) v.store (0.0f);
}

void DrumEngine::setParameters (const std::array<PadParams, KitParams::numPads>& pads, const GlobalParams& global)
{
    padParams    = pads;
    globalParams = global;
}

void DrumEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock   = juce::jmax (1, maxBlockSize);

    busReverb.assign ((size_t) maxBlock, 0.0f);
    busDelay.assign  ((size_t) maxBlock, 0.0f);
    busPhaser.assign ((size_t) maxBlock, 0.0f);
    fxLeft.assign    ((size_t) maxBlock, 0.0f);
    fxRight.assign   ((size_t) maxBlock, 0.0f);

    for (auto& v : voices)
        v.prepare (sampleRate);

    for (size_t slot = 0; slot < fxBlocks.size(); ++slot)
    {
        fxBlocks[slot].prepare (sampleRate, maxBlock);
        fxGain[slot] = levelToGain (globalParams.fx[slot].level->load());
    }

    masterGain = levelToGain (globalParams.masterLevel->load());
}

void DrumEngine::reset()
{
    for (auto& v : voices)
        v.stop();

    for (auto& block : fxBlocks)
        block.reset();
}

void DrumEngine::setPadSample (int pad, SampleData* sample) noexcept
{
    if (pad >= 0 && pad < KitParams::numPads)
        padSamples[(size_t) pad].store (sample, std::memory_order_release);
}

int DrumEngine::getActiveVoiceCount() const noexcept
{
    int count = 0;

    for (auto& v : voices)
        if (v.isActive())
            ++count;

    return count;
}

DrumVoice* DrumEngine::findVoice()
{
    DrumVoice* free = nullptr;
    DrumVoice* oldestSounding = nullptr;
    DrumVoice* oldestAny = nullptr;
    int sounding = 0;

    for (auto& v : voices)
    {
        if (! v.isActive())
        {
            if (free == nullptr)
                free = &v;
            continue;
        }

        if (oldestAny == nullptr || v.getAge() < oldestAny->getAge())
            oldestAny = &v;

        if (! v.isFading())
        {
            ++sounding;
            if (oldestSounding == nullptr || v.getAge() < oldestSounding->getAge())
                oldestSounding = &v;
        }
    }

    if (sounding >= maxPolyphony && oldestSounding != nullptr)
        oldestSounding->fadeOut();

    if (free != nullptr)
        return free;

    // Every one of the 64 is busy. Restart the oldest in place; this is the
    // one case that can click, and it takes 64 hits within 2 ms to reach it.
    oldestAny->stop();
    return oldestAny;
}

int DrumEngine::chokeGroupOf (int pad) const noexcept
{
    return juce::jlimit (0, KitParams::numChokeGroups, (int) std::lround (padParams[(size_t) pad].choke->load()));
}

void DrumEngine::choke (int group) noexcept
{
    if (group <= 0)
        return;

    for (auto& v : voices)
        if (v.isActive() && v.getChokeGroup() == group)
            v.fadeOut (chokeFadeMs);
}

void DrumEngine::startVoice (int pad, float velocity)
{
    hitCount[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
    lastVelocity[(size_t) pad].store (velocity, std::memory_order_relaxed);

    auto* sample = padSamples[(size_t) pad].load (std::memory_order_acquire);

    if (sample == nullptr)
        return;

    const auto amount = globalParams.humanize->load() / 100.0f;
    const auto humanize = HumanizeOffsets::draw (amount, [this] { return uniform (random); });

    if (auto* voice = findVoice())
        voice->start (pad, SampleData::Ptr (sample), velocity, padParams[(size_t) pad], humanize, ++voiceAge);
}

void DrumEngine::trigger (int pad, float velocity)
{
    if (pad < 0 || pad >= KitParams::numPads || velocity <= 0.0f)
        return;

    choke (chokeGroupOf (pad));
    startVoice (pad, velocity);
}

void DrumEngine::handleNoteOn (int note, float velocity)
{
    if (velocity <= 0.0f)
        return;

    std::array<bool, KitParams::numPads> matches {};
    bool any = false;

    for (int pad = 0; pad < KitParams::numPads; ++pad)
    {
        matches[(size_t) pad] = (int) std::lround (padParams[(size_t) pad].note->load()) == note;
        any = any || matches[(size_t) pad];
    }

    if (! any)
        return;

    // Every choke first, then every start - see the header.
    for (int pad = 0; pad < KitParams::numPads; ++pad)
        if (matches[(size_t) pad])
            choke (chokeGroupOf (pad));

    for (int pad = 0; pad < KitParams::numPads; ++pad)
        if (matches[(size_t) pad])
            startVoice (pad, velocity);
}

void DrumEngine::render (float* left, float* right, int numSamples)
{
    Outputs outputs;
    outputs.left[0]  = left;
    outputs.right[0] = right;
    render (outputs, numSamples);
}

void DrumEngine::render (const Outputs& outputs, int numSamples)
{
    auto cursor = outputs;
    jassert (cursor.left[0] != nullptr && cursor.right[0] != nullptr);

    while (numSamples > 0)
    {
        const auto chunk = juce::jmin (numSamples, maxBlock);
        renderChunk (cursor, chunk);

        for (size_t o = 0; o < cursor.left.size(); ++o)
        {
            if (cursor.left[o] != nullptr)
            {
                cursor.left[o]  += chunk;
                cursor.right[o] += chunk;
            }
        }

        numSamples -= chunk;
    }
}

void DrumEngine::renderChunk (const Outputs& outputs, int n)
{
    for (size_t o = 0; o < outputs.left.size(); ++o)
    {
        if (outputs.left[o] != nullptr)
        {
            std::fill (outputs.left[o],  outputs.left[o]  + n, 0.0f);
            std::fill (outputs.right[o], outputs.right[o] + n, 0.0f);
        }
    }

    std::fill (busReverb.begin(), busReverb.begin() + n, 0.0f);
    std::fill (busDelay.begin(),  busDelay.begin()  + n, 0.0f);
    std::fill (busPhaser.begin(), busPhaser.begin() + n, 0.0f);

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;

        // Read live, so re-routing a ringing pad moves its tail too.
        auto out = (size_t) juce::jlimit (0, KitParams::numAuxOutputs,
                                          (int) std::lround (padParams[(size_t) v.getPad()].output->load()));
        if (outputs.left[out] == nullptr)
            out = 0;

        v.render (outputs.left[out], outputs.right[out], busReverb.data(), busDelay.data(), busPhaser.data(), n);
    }

    auto* left  = outputs.left[0];
    auto* right = outputs.right[0];
    const auto& g = globalParams;

    // The send effects. A send is mono; it enters its effect as a centred
    // stereo signal - -3 dB per side, the same as a centred pad - so an echo of
    // a centred pad comes back at the pad's own level, and effects that need
    // two sides (chorus, ping-pong, the reverbs' width) get them.
    constexpr float centre = 0.70710678f;
    const float* buses[KitParams::Fx::numSlots] { busReverb.data(), busDelay.data(), busPhaser.data() };

    for (int slot = 0; slot < KitParams::Fx::numSlots; ++slot)
    {
        const auto& p = g.fx[(size_t) slot];

        EffectBlock::Settings settings;
        settings.category = KitParams::Fx::categories[slot];
        settings.subtype  = EffectCatalog::clampSubtype (settings.category, (int) std::lround (p.type->load()));
        settings.a        = p.a->load();
        settings.b        = p.b->load();
        settings.mix      = 1.0f;
        settings.sync     = slot == KitParams::Fx::delay;   // Kitbox's delay has always followed the tempo
        settings.bpm      = tempo;

        for (int i = 0; i < n; ++i)
            fxLeft[(size_t) i] = fxRight[(size_t) i] = buses[slot][i] * centre;

        fxBlocks[(size_t) slot].process (fxLeft.data(), fxRight.data(), n, settings);

        // The return level ramps across the chunk, like Master.
        const auto start = fxGain[(size_t) slot];
        const auto end   = levelToGain (p.level->load());
        const auto step  = (end - start) / (float) n;
        auto gain = start;

        for (int i = 0; i < n; ++i)
        {
            gain += step;
            left[i]  += gain * fxLeft[(size_t) i];
            right[i] += gain * fxRight[(size_t) i];
        }

        fxGain[(size_t) slot] = end;
    }

    // Master scales every output alike, ramped across the chunk.
    const auto start  = masterGain;
    const auto target = levelToGain (g.masterLevel->load());
    const auto step   = (target - start) / (float) n;

    for (size_t o = 0; o < outputs.left.size(); ++o)
    {
        if (outputs.left[o] == nullptr)
            continue;

        auto gain = start;

        for (int i = 0; i < n; ++i)
        {
            gain += step;
            outputs.left[o][i]  *= gain;
            outputs.right[o][i] *= gain;
        }
    }

    masterGain = target;
}
