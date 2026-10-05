//
//  AuPreset.cpp
//  Kitbox
//

#include "AuPreset.h"

namespace
{
    const juce::String stateKey = "jucePluginState";

    void addEntry (juce::XmlElement& dict, const juce::String& key, const juce::String& valueTag, const juce::String& value)
    {
        dict.createNewChildElement ("key")->addTextElement (key);
        dict.createNewChildElement (valueTag)->addTextElement (value);
    }
}

juce::String AuPreset::write (const juce::MemoryBlock& pluginState, const juce::String& presetName)
{
    juce::XmlElement plist ("plist");
    plist.setAttribute ("version", "1.0");
    auto* dict = plist.createNewChildElement ("dict");

    // Keys in the order Logic writes them.
    addEntry (*dict, stateKey,       "data",    juce::Base64::toBase64 (pluginState.getData(), pluginState.getSize()));
    addEntry (*dict, "manufacturer", "integer", juce::String (manufacturer));
    addEntry (*dict, "name",         "string",  presetName);
    addEntry (*dict, "subtype",      "integer", juce::String (subtype));
    addEntry (*dict, "type",         "integer", juce::String (type));
    addEntry (*dict, "version",      "integer", "0");

    juce::XmlElement::TextFormat format;
    format.dtd = "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                 "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">";

    return plist.toString (format);
}

AuPreset::Contents AuPreset::read (const juce::String& fileText)
{
    Contents contents;

    const auto plist = juce::XmlDocument::parse (fileText);
    const auto* dict = plist != nullptr && plist->hasTagName ("plist") ? plist->getChildByName ("dict") : nullptr;

    if (dict == nullptr)
    {
        contents.error = "Not an Audio Unit preset.";
        return contents;
    }

    juce::String stateText;
    bool hasState = false;
    juce::int64 foundSubtype = -1, foundManufacturer = -1;

    // A plist dict is a flat run of <key> elements, each followed by its value.
    for (auto* key = dict->getFirstChildElement(); key != nullptr; key = key->getNextElement())
    {
        auto* value = key->getNextElement();

        if (! key->hasTagName ("key") || value == nullptr)
            continue;

        const auto name = key->getAllSubText();

        if (name == stateKey)          { stateText = value->getAllSubText(); hasState = true; }
        else if (name == "name")         contents.name = value->getAllSubText();
        else if (name == "subtype")      foundSubtype = value->getAllSubText().trim().getLargeIntValue();
        else if (name == "manufacturer") foundManufacturer = value->getAllSubText().trim().getLargeIntValue();

        key = value;
    }

    if (foundSubtype != subtype || foundManufacturer != manufacturer)
    {
        contents.error = "This preset belongs to another plugin.";
        return contents;
    }

    if (! hasState)
    {
        contents.error = "The preset holds no Kitbox settings.";
        return contents;
    }

    // Logic wraps base64 over many indented lines; the decoder wants it bare.
    const auto base64 = stateText.removeCharacters (" \t\r\n");
    bool decodedOk = false;

    {
        // Scoped: the stream sets the block's final size when it is destroyed.
        juce::MemoryOutputStream decoded (contents.pluginState, false);
        decodedOk = juce::Base64::convertFromBase64 (decoded, base64);
    }

    if (! decodedOk)
        contents.error = "The preset is damaged.";

    return contents;
}
