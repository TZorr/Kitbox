//
//  KitFile.h
//  Kitbox
//
//  The .kitbox container: the plugin's session state, and the payload of a kit
//  saved to disk (an .aupreset carries it under "jucePluginState" - see
//  AuPreset.h). Bare .kitbox files from before that still load.
//
//  One format for both on purpose. A kit saved to disk and a kit saved inside a
//  Logic project hold the same things - every pad's sample, every knob, the
//  effects - and a second format would be a second place for the two to drift
//  apart. So getStateInformation writes a .kitbox file into the host's memory
//  block, and Load Kit reads a session's state from disk.
//
//  Layout: the four bytes "KTBX", a little-endian format version, then a
//  gzip-compressed binary ValueTree:
//
//      KITBOX_FILE
//        <parameter tree>          the APVTS state, via copyState()
//        SAMPLES
//          SAMPLE pad=0 file="Kick 01.wav" data=<the original file's bytes>
//          ...
//
//  Samples are stored as the files they were, not as decoded audio: a 16-bit
//  WAV stays 16-bit, an MP3 stays an MP3, and dragging a pad out of a restored
//  session hands back the same file that went in.
//

#pragma once

#include <array>

#include <juce_data_structures/juce_data_structures.h>

#include "SampleData.h"
#include "ParameterIds.h"

namespace KitFile
{
    constexpr int formatVersion = 1;
    inline constexpr const char* extension = ".kitbox";

    /** params must come from copyState() - see the note in PluginProcessor. */
    juce::MemoryBlock write (const juce::ValueTree& params,
                             const std::array<SampleData::Ptr, KitParams::numPads>& samples);

    struct StoredSample
    {
        juce::String fileName;
        juce::MemoryBlock bytes;
    };

    struct Contents
    {
        juce::ValueTree params;
        std::array<StoredSample, KitParams::numPads> samples;
        juce::String error;   // empty on success

        bool ok() const noexcept { return error.isEmpty(); }
    };

    Contents read (const void* data, size_t size);
}
