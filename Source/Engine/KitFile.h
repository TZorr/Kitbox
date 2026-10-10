//
//  KitFile.h
//  Kitbox
//
//  The .kitbox container: the plugin's session state, and the payload of a kit
//  saved to disk (an .aupreset carries it under "jucePluginState" - see
//  AuPreset.h), and a kit file of its own when Save Kit is given the .kitbox
//  extension.
//
//  One format for all three on purpose. A kit saved to disk and a kit saved
//  inside a Logic project hold the same things - every pad's sample, every
//  knob, the effects - and a second format would be a second place for the
//  two to drift apart. So getStateInformation writes a .kitbox into the host's
//  memory block, and Load Kit reads a session's state from disk.
//
//  Version 2 (0.7.0) is a plain ZIP archive, so the samples in it can be
//  reached without Kitbox: rename the file to .zip and open it.
//
//      kit.xml                        every knob, the pads' Transmute choices
//      Original/01 Kick 01.wav        each pad's sample, the file as it came
//      Synth/01 Kick 01.wav           its Transmute synth, if it has one
//      Synth/01 Kick 01.drumparams    and the synth's parameters - a file
//                                     Transmute opens, to work on the drum
//
//  The two digits are the pad. Samples are stored as the files they were, not
//  as decoded audio: a 16-bit WAV stays 16-bit, an MP3 stays an MP3, and
//  dragging a pad out of a restored session hands back the same file that went
//  in. Audio is stored uncompressed (deflate saves little on audio and costs
//  time on every save), kit.xml and the parameters deflated.
//
//  Version 1 - the four bytes "KTBX", a format version, then a gzip-compressed
//  binary ValueTree holding the parameters and the samples - still loads:
//  every kit and session from before, and the kits Transmute's "Export Kitbox
//  Kit" writes.
//

#pragma once

#include <array>
#include <optional>

#include <juce_data_structures/juce_data_structures.h>

#include "SampleData.h"
#include "../ParameterIds.h"
#include "../Transmute/DrumParams.h"

namespace KitFile
{
    constexpr int formatVersion = 2;
    inline constexpr const char* extension = ".kitbox";

    /** A pad's Transmute synth: the rendered sample and what it was rendered from. */
    struct Synth
    {
        SampleData::Ptr sample;
        std::optional<transmute::DrumParams> params;
    };

    using Synths = std::array<Synth, KitParams::numPads>;

    /** params must come from copyState() - see the note in PluginProcessor. */
    juce::MemoryBlock write (const juce::ValueTree& params,
                             const std::array<SampleData::Ptr, KitParams::numPads>& samples,
                             const Synths& synths = {});

    struct StoredSample
    {
        juce::String fileName;
        juce::MemoryBlock bytes;
    };

    struct StoredSynth
    {
        StoredSample file;
        std::optional<transmute::DrumParams> params;
    };

    struct Contents
    {
        juce::ValueTree params;
        std::array<StoredSample, KitParams::numPads> samples;
        std::array<StoredSynth, KitParams::numPads> synths;
        int version = 0;
        juce::String error;   // empty on success

        bool ok() const noexcept { return error.isEmpty(); }
    };

    Contents read (const void* data, size_t size);

    /** A pad's file inside the archive: "01 Kick 01.wav", the name made safe
        for every file system. */
    juce::String entryName (int pad, const juce::String& fileName);
}
