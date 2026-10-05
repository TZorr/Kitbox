//
//  EffectBlock.h
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  One of the three slots: holds every effect there is, runs one of them, and
//  crossfades when told to run another.
//
//  Why every effect is built up front. Switching means having the next effect
//  ready, and building one allocates (delay lines, reverb tanks). On the audio
//  thread that is forbidden; on another thread it is a hand-off with its own
//  ways to go wrong. Fourteen instances per block cost a few megabytes in all
//  and remove the question: prepare() builds them, and after that a switch is
//  a pointer that was already valid becoming the one that gets called. Only the
//  effect in use (and, for the length of a fade, the one leaving) runs.
//
//  The fade. Each of fifteen slots - the fourteen effects and "dry", which is
//  what bypass is - has a gain that ramps towards 1 for the target and towards
//  0 for the rest, linearly over 30 ms. The output is the gain-weighted sum
//  divided by the sum of the gains, so it stays correctly scaled if the target
//  changes mid-fade (the ramps then no longer add up to one by themselves).
//  Changing again while fading just re-aims the ramps from where they are.
//  A slot that reaches zero is reset, so the next time it is chosen it starts
//  from silence rather than from whatever it was ringing with.
//
//  Outside a fade the block runs the one effect straight through, and a
//  bypassed block touches nothing at all: its output is its input, bit for bit.
//
//  Mix crossfades the block's input with the result of the above. Effects are
//  written to return the finished signal (see Effect.h), so this is the only
//  place that blends dry back in.
//

#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Effect.h"
#include "EffectCatalog.h"

class EffectBlock
{
public:
    struct Settings
    {
        int category = 0, subtype = 0;
        bool bypass = false;
        float a = 0.5f, b = 0.5f, mix = 1.0f;
        bool sync = false;      // the effect's time knob follows the host tempo (where it has one)
        double bpm = 120.0;
        bool hasPosition = false;   // the host is playing and reported where (ppq, in quarter notes)
        double ppq = 0.0;           // ... at the start of the block
        int preDelay = 0;           // reverbs: a step of Dsp::Sync::preDelayNames
        double beatsPerBar = 4.0;   // ... and how long "1 BAR" is
    };

    static constexpr int drySlot = EffectCatalog::numEffects;
    static constexpr int numSlots = EffectCatalog::numEffects + 1;
    static constexpr double fadeSeconds = 0.03;

    /** Allocates. Not for the audio thread. */
    void prepare (double newSampleRate, int newMaxBlock);

    void reset();

    /** In place, left and right. Any n; longer than the prepared block is chunked. */
    void process (float* left, float* right, int n, const Settings& settings) noexcept;

    /** The slot the block is heading for (or at): 0..13 an effect, 14 bypass. Test hook. */
    int getTarget() const noexcept      { return target; }

    bool isFading() const noexcept      { return fading; }

private:
    void processChunk (float* left, float* right, int n, const Settings& settings) noexcept;
    void activate (int slot, const Settings& settings) noexcept;
    void deactivate (int slot) noexcept;

    float* scratch (int slot, int channel) noexcept
    {
        return slotBuffers.data() + (size_t) ((slot * 2 + channel) * maxBlock);
    }

    std::array<std::unique_ptr<Effect>, EffectCatalog::numEffects> effects;
    std::array<bool, numSlots> active {};
    std::array<float, numSlots> gain {};

    std::vector<float> slotBuffers, dry;   // dry: left then right of the block's input
    juce::SmoothedValue<float> mix { 1.0f };

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int target = drySlot;
    bool initialised = false, fading = false;
};
