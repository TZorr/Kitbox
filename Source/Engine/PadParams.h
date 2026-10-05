//
//  PadParams.h
//  Kitbox
//
//  Where the engine reads a pad's knobs from.
//
//  The processor points these at the APVTS's raw values; the harness points
//  them at plain atomics it owns. The engine cannot tell the difference, which
//  is the point - it links without the plugin machinery, so it can be tested
//  without it.
//

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <iterator>

#include "ParameterIds.h"

struct PadParams
{
    std::atomic<float>* level      = nullptr;   // dB
    std::atomic<float>* pan        = nullptr;   // -1..1
    std::atomic<float>* tune       = nullptr;   // semitones
    std::atomic<float>* fine       = nullptr;   // cents
    std::atomic<float>* start      = nullptr;   // percent of the sample
    std::atomic<float>* velocity   = nullptr;   // percent sensitivity
    std::atomic<float>* attack     = nullptr;   // ms
    std::atomic<float>* hold       = nullptr;   // ms
    std::atomic<float>* decay      = nullptr;   // ms, decayMaxMs = none
    std::atomic<float>* filterType = nullptr;   // FilterType index
    std::atomic<float>* cutoff     = nullptr;   // Hz
    std::atomic<float>* resonance  = nullptr;   // percent
    std::atomic<float>* drive      = nullptr;   // percent
    std::atomic<float>* envAttack  = nullptr;   // ms
    std::atomic<float>* envDecay   = nullptr;   // ms
    std::atomic<float>* envAmount  = nullptr;   // semitones
    std::atomic<float>* sendReverb = nullptr;   // percent
    std::atomic<float>* sendDelay  = nullptr;   // percent
    std::atomic<float>* sendPhaser = nullptr;   // percent
    std::atomic<float>* note       = nullptr;   // MIDI note 0..127
    std::atomic<float>* choke      = nullptr;   // 0 = none, 1..8
    std::atomic<float>* output     = nullptr;   // 0 = Main, 1..16 = Out n

    /** The same order as KitParams::Pad::all. */
    std::atomic<float>** slot (int index) noexcept
    {
        std::atomic<float>** slots[] = {
            &level, &pan, &tune, &fine, &start, &velocity, &attack, &hold, &decay,
            &filterType, &cutoff, &resonance, &drive, &envAttack, &envDecay, &envAmount,
            &sendReverb, &sendDelay, &sendPhaser, &note, &choke, &output
        };
        return slots[index];
    }

    static constexpr int count = (int) std::size (KitParams::Pad::all);
};

struct GlobalParams
{
    std::atomic<float>* humanize       = nullptr;   // percent
    std::atomic<float>* masterLevel    = nullptr;   // dB

    /** One send effect: its type (index within the slot's category), its two
        knobs 0..1, and its return level in dB. Indexed by KitParams::Fx::Slot. */
    struct FxSlot
    {
        std::atomic<float>* type  = nullptr;
        std::atomic<float>* a     = nullptr;
        std::atomic<float>* b     = nullptr;
        std::atomic<float>* level = nullptr;
    };

    std::array<FxSlot, KitParams::Fx::numSlots> fx {};
};

/** dB to gain, with the bottom of the knob meaning silence. */
inline float levelToGain (float db) noexcept
{
    return db <= KitParams::levelFloorDb ? 0.0f : std::pow (10.0f, db / 20.0f);
}
