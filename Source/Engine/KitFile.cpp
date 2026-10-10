//
//  KitFile.cpp
//  Kitbox
//

#include "KitFile.h"

namespace
{
    const juce::Identifier fileType    { "KITBOX_FILE" };
    const juce::Identifier samplesType { "SAMPLES" };
    const juce::Identifier sampleType  { "SAMPLE" };
    const juce::Identifier propPad     { "pad" };
    const juce::Identifier propFile    { "file" };
    const juce::Identifier propData    { "data" };
    const juce::Identifier propVersion { "formatVersion" };

    constexpr char legacyMagic[4] = { 'K', 'T', 'B', 'X' };
    constexpr char zipMagic[4]    = { 'P', 'K', 3, 4 };

    const juce::String kitEntry     { "kit.xml" };
    const juce::String originalPath { "Original/" };
    const juce::String synthPath    { "Synth/" };
    const juce::String paramsSuffix { ".drumparams" };

    void addEntry (juce::ZipFile::Builder& zip, const void* data, size_t size, int compression, const juce::String& path)
    {
        // The builder reads every stream when it writes: each entry gets its own copy.
        zip.addEntry (std::make_unique<juce::MemoryInputStream> (data, size, true), compression, path,
                      juce::Time::getCurrentTime());
    }

    /** "01 Kick.wav" -> pad 0 and "Kick.wav"; -1 for anything else. */
    int padOf (const juce::String& entry, juce::String& fileName)
    {
        if (entry.length() < 4 || ! juce::CharacterFunctions::isDigit (entry[0])
            || ! juce::CharacterFunctions::isDigit (entry[1]) || entry[2] != ' ')
            return -1;

        fileName = entry.substring (3);
        const auto pad = entry.substring (0, 2).getIntValue() - 1;
        return pad >= 0 && pad < KitParams::numPads ? pad : -1;
    }

    //==============================================================================
    KitFile::Contents readLegacy (const void* data, size_t size)
    {
        KitFile::Contents contents;
        contents.version = 1;

        juce::MemoryInputStream in (data, size, false);
        in.skipNextBytes (sizeof (legacyMagic));

        if (in.readInt() > 1)
        {
            contents.error = "This kit was saved by a newer Kitbox.";
            return contents;
        }

        juce::GZIPDecompressorInputStream unzipped (in);
        const auto root = juce::ValueTree::readFromStream (unzipped);

        if (! root.hasType (fileType))
        {
            contents.error = "The kit file is damaged.";
            return contents;
        }

        contents.params = root.getChildWithName (KitParams::stateTreeType).createCopy();

        for (const auto& entry : root.getChildWithName (samplesType))
        {
            const auto pad = (int) entry.getProperty (propPad, -1);

            if (pad < 0 || pad >= KitParams::numPads)
                continue;

            if (const auto* bytes = entry.getProperty (propData).getBinaryData())
            {
                auto& slot = contents.samples[(size_t) pad];
                slot.fileName = entry.getProperty (propFile).toString();
                slot.bytes = *bytes;
            }
        }

        return contents;
    }

    KitFile::Contents readZip (const void* data, size_t size)
    {
        KitFile::Contents contents;
        contents.version = 2;

        juce::ZipFile zip (std::make_unique<juce::MemoryInputStream> (data, size, false));

        const auto readEntry = [&zip] (int index, juce::MemoryBlock& out)
        {
            std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (index));
            if (stream == nullptr)
                return false;
            out.reset();
            juce::MemoryOutputStream sink (out, false);
            sink.writeFromInputStream (*stream, -1);
            return true;
        };

        const auto kitIndex = zip.getIndexOfFileName (kitEntry);
        juce::MemoryBlock kitBytes;

        if (kitIndex < 0 || ! readEntry (kitIndex, kitBytes))
        {
            contents.error = "The kit file is damaged.";
            return contents;
        }

        const auto root = juce::ValueTree::fromXml (kitBytes.toString());

        if (! root.hasType (fileType))
        {
            contents.error = "The kit file is damaged.";
            return contents;
        }

        if ((int) root.getProperty (propVersion, KitFile::formatVersion) > KitFile::formatVersion)
        {
            contents.error = "This kit was saved by a newer Kitbox.";
            return contents;
        }

        contents.params = root.getChildWithName (KitParams::stateTreeType).createCopy();

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto path = zip.getEntry (i)->filename;
            const auto inOriginals = path.startsWith (originalPath);
            const auto inSynths = path.startsWith (synthPath);

            if (! inOriginals && ! inSynths)
                continue;

            juce::String fileName;
            const auto pad = padOf (path.fromFirstOccurrenceOf ("/", false, false), fileName);

            if (pad < 0)
                continue;

            juce::MemoryBlock bytes;
            if (! readEntry (i, bytes))
                continue;

            if (inOriginals)
            {
                contents.samples[(size_t) pad] = { fileName, std::move (bytes) };
            }
            else if (fileName.endsWithIgnoreCase (paramsSuffix))
            {
                contents.synths[(size_t) pad].params = transmute::DrumParams::fromJson (bytes.toString().toStdString());
            }
            else
            {
                contents.synths[(size_t) pad].file = { fileName, std::move (bytes) };
            }
        }

        return contents;
    }
}

//==============================================================================
juce::String KitFile::entryName (int pad, const juce::String& fileName)
{
    return juce::String (pad + 1).paddedLeft ('0', 2) + " "
         + fileName.replaceCharacters ("/:\\", "---").trim();
}

juce::MemoryBlock KitFile::write (const juce::ValueTree& params,
                                  const std::array<SampleData::Ptr, KitParams::numPads>& samples,
                                  const Synths& synths)
{
    juce::ValueTree root (fileType);
    root.setProperty (propVersion, formatVersion, nullptr);
    root.appendChild (params.createCopy(), nullptr);

    juce::ZipFile::Builder zip;

    const auto xml = root.toXmlString();
    addEntry (zip, xml.toRawUTF8(), xml.getNumBytesAsUTF8(), 6, kitEntry);

    for (int pad = 0; pad < KitParams::numPads; ++pad)
    {
        if (const auto& sample = samples[(size_t) pad])
        {
            const auto& bytes = sample->getOriginal();
            addEntry (zip, bytes.getData(), bytes.getSize(), 0, originalPath + entryName (pad, sample->getFileName()));
        }

        const auto& synth = synths[(size_t) pad];

        if (synth.sample != nullptr)
        {
            const auto& bytes = synth.sample->getOriginal();
            const auto name = entryName (pad, synth.sample->getFileName());
            addEntry (zip, bytes.getData(), bytes.getSize(), 0, synthPath + name);

            if (synth.params)
            {
                const auto json = synth.params->toJson();
                addEntry (zip, json.data(), json.size(), 6,
                          synthPath + juce::File::createFileWithoutCheckingPath ("/" + name).getFileNameWithoutExtension() + paramsSuffix);
            }
        }
    }

    juce::MemoryBlock block;

    {
        juce::MemoryOutputStream out (block, false);
        zip.writeToStream (out, nullptr);
    }

    return block;
}

KitFile::Contents KitFile::read (const void* data, size_t size)
{
    Contents contents;

    if (data == nullptr || size < 8)
    {
        contents.error = "Not a Kitbox kit.";
        return contents;
    }

    if (std::memcmp (data, legacyMagic, sizeof (legacyMagic)) == 0)
        contents = readLegacy (data, size);
    else if (std::memcmp (data, zipMagic, sizeof (zipMagic)) == 0)
        contents = readZip (data, size);
    else
        contents.error = "Not a Kitbox kit.";

    if (contents.ok() && ! contents.params.isValid())
        contents.error = "The kit file has no settings in it.";

    return contents;
}
