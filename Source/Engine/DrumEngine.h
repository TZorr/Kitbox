//
//  DrumEngine.h
//  Kitbox
//
//  Sixteen pads, a pool of voices, three send buses, the effects, the master.
//
//      MIDI note -> every pad set to that note -> free voice
//      voice -> its pad's output (Main or Out 1..16) + reverb/delay/mod sends
//      sends -> three effect slots (stereo returns) -> Main
//      every output -> master level
//
//  Notes are per pad and may repeat: two pads on one note play together, which
//  is how a kick is layered with a sub.
//
//  Choke groups: a hit on a pad in group n first fades out every voice already
//  sounding in group n - the same pad's included, so an open hat also chokes
//  itself - over 5 ms. That is long enough not to click on a low tom and short
//  enough to read as a hand closing the hat. When one note plays several pads,
//  all the chokes happen before any of the new voices start, so layered pads in
//  the same group do not silence each other.
//
//  Outputs: a pad routed to an aux output the host has not enabled plays on
//  Main instead. A stereo instance of a multi-output plugin must not lose a
//  drum because of a setting made in a multi-output one. The effect returns
//  always go to Main - the sends are shared, so their returns belong to the
//  mix, not to any one pad.
//
//  The pool holds 64 voices and allows 48 to sound. The other sixteen are the
//  headroom that makes stealing click-free: when all 48 are busy, the oldest
//  is faded over 2 ms and the new hit takes a spare voice, instead of cutting
//  the old one off mid-waveform. Only if a whole spare set is also fading - 64
//  hits inside two milliseconds - is a voice restarted in place.
//
//  Audio thread only, except setPadSample() and prepare(), which the processor
//  calls from the message thread with the documented guarantees.
//

#pragma once

#include <array>
#include <random>

#include "DrumVoice.h"
#include "Fx/EffectBlock.h"

class DrumEngine
{
public:
    static constexpr int numVoices    = 64;
    static constexpr int maxPolyphony = 48;
    static constexpr double chokeFadeMs = 5.0;

    /** Where the engine writes. Index 0 is Main and must be set; an aux pair
        left null is a bus the host has not enabled. */
    struct Outputs
    {
        std::array<float*, KitParams::numOutputBuses> left {}, right {};
    };

    DrumEngine();

    void setParameters (const std::array<PadParams, KitParams::numPads>& pads, const GlobalParams& global);

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Message thread. The pointer is only borrowed: the caller keeps the
        sample alive (the processor, through SamplePool) for as long as the
        engine might still read it. */
    void setPadSample (int pad, SampleData* sample) noexcept;

    /** Audio thread. One pad, velocity 0..1: its choke group, then its voice. */
    void trigger (int pad, float velocity);

    /** Audio thread. Plays every pad whose note this is. */
    void handleNoteOn (int note, float velocity);

    /** Audio thread. Overwrites every output it is given. */
    void render (const Outputs& outputs, int numSamples);

    /** Main only - for tests and a stereo host. */
    void render (float* left, float* right, int numSamples);

    void setTempo (double bpm) noexcept { tempo = bpm > 0.0 ? bpm : 120.0; }

    /** For the pads' flash: counts hits, from any thread. */
    int getHitCount (int pad) const noexcept { return hitCount[(size_t) pad].load (std::memory_order_relaxed); }
    float getLastVelocity (int pad) const noexcept { return lastVelocity[(size_t) pad].load (std::memory_order_relaxed); }

    int getActiveVoiceCount() const noexcept;

    /** For tests: a fixed seed makes humanize repeatable. */
    void seedRandom (uint32_t seed) { random.seed (seed); }

private:
    void renderChunk (const Outputs& outputs, int numSamples);
    DrumVoice* findVoice();
    int chokeGroupOf (int pad) const noexcept;
    void choke (int group) noexcept;
    void startVoice (int pad, float velocity);

    std::array<DrumVoice, numVoices> voices;
    std::array<std::atomic<SampleData*>, KitParams::numPads> padSamples;
    std::array<std::atomic<int>, KitParams::numPads> hitCount;
    std::array<std::atomic<float>, KitParams::numPads> lastVelocity;

    std::array<PadParams, KitParams::numPads> padParams {};
    GlobalParams globalParams {};

    //  The send effects: one Rackbox EffectBlock per slot, its category fixed
    //  (KitParams::Fx::categories), Mix at 100 %, never bypassed. The block
    //  builds every effect of the catalog up front and crossfades 30 ms when
    //  the type changes, so switching the reverb while it rings does not click.
    std::array<EffectBlock, KitParams::Fx::numSlots> fxBlocks;
    std::array<float, KitParams::Fx::numSlots> fxGain {};

    std::vector<float> busReverb, busDelay, busPhaser, fxLeft, fxRight;
    int maxBlock = 0;
    double sampleRate = 48000.0, tempo = 120.0;
    uint64_t voiceAge = 0;
    float masterGain = 1.0f;

    std::mt19937 random { 0x4b697462u };
    std::uniform_real_distribution<float> uniform { -1.0f, 1.0f };
};
