//
//  AuPreset.h
//  Kitbox
//
//  Kits on disk: an Audio Unit preset (.aupreset) with the .kitbox container
//  inside.
//
//  Why this format rather than a file type of our own. An .aupreset is what
//  Logic itself writes when it saves an AU setting, and what plugins such as
//  Gullfoss write from their own preset buttons: a property list naming the
//  plugin (type, subtype, manufacturer), the preset's name, and the plugin's
//  state. Kept in ~/Library/Audio/Presets/<company>/<plugin>/, it is found by
//  Logic's settings menu, which shows the name without the extension - and a
//  kit saved by Load/Save Kit and a setting saved by Logic are one and the
//  same kind of file.
//
//  The state goes under the key JUCE's AU wrapper uses, "jucePluginState", so
//  when Logic loads one of these files the wrapper hands those bytes straight
//  to setStateInformation. What the wrapper checks before that
//  (AUBase::RestoreState): version 0, and subtype and manufacturer matching the
//  plugin. The "data" key of a Logic-written preset holds AU-level parameter
//  values the wrapper discards; it is not written here.
//
//  The XML is written by hand with juce::XmlElement rather than through
//  CoreFoundation, so the engine harness can test it without the AU machinery.
//

#pragma once

#include <juce_core/juce_core.h>

namespace AuPreset
{
    inline constexpr const char* extension = ".aupreset";

    /** The plugin's identity, as the four-character codes in CMakeLists.txt
        (AU_MAIN_TYPE, PLUGIN_CODE, PLUGIN_MANUFACTURER_CODE). */
    constexpr int fourCC (const char (&code)[5])
    {
        return (int) (((unsigned) (unsigned char) code[0] << 24) | ((unsigned) (unsigned char) code[1] << 16)
                      | ((unsigned) (unsigned char) code[2] << 8) | (unsigned) (unsigned char) code[3]);
    }

    constexpr int type         = fourCC ("aumu");
    constexpr int subtype      = fourCC ("Ktbx");
    constexpr int manufacturer = fourCC ("Tzor");

    /** The whole file, ready to write. */
    juce::String write (const juce::MemoryBlock& pluginState, const juce::String& presetName);

    struct Contents
    {
        juce::MemoryBlock pluginState;
        juce::String name;
        juce::String error;   // empty on success

        bool ok() const noexcept { return error.isEmpty(); }
    };

    Contents read (const juce::String& fileText);
}
