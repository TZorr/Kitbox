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

    constexpr char magic[4] = { 'K', 'T', 'B', 'X' };
}

juce::MemoryBlock KitFile::write (const juce::ValueTree& params,
                                  const std::array<SampleData::Ptr, KitParams::numPads>& samples)
{
    juce::ValueTree root (fileType);
    root.appendChild (params.createCopy(), nullptr);

    juce::ValueTree stored (samplesType);

    for (int pad = 0; pad < KitParams::numPads; ++pad)
    {
        const auto& sample = samples[(size_t) pad];

        if (sample == nullptr)
            continue;

        juce::ValueTree entry (sampleType);
        entry.setProperty (propPad, pad, nullptr);
        entry.setProperty (propFile, sample->getFileName(), nullptr);
        entry.setProperty (propData, juce::var (sample->getOriginal()), nullptr);
        stored.appendChild (entry, nullptr);
    }

    root.appendChild (stored, nullptr);

    juce::MemoryBlock block;

    {
        juce::MemoryOutputStream out (block, false);
        out.write (magic, sizeof (magic));
        out.writeInt (formatVersion);

        juce::GZIPCompressorOutputStream zipped (out, 6);
        root.writeToStream (zipped);
        zipped.flush();
    }

    return block;
}

KitFile::Contents KitFile::read (const void* data, size_t size)
{
    Contents contents;

    if (data == nullptr || size < 8 || std::memcmp (data, magic, sizeof (magic)) != 0)
    {
        contents.error = "Not a Kitbox kit.";
        return contents;
    }

    juce::MemoryInputStream in (data, size, false);
    in.skipNextBytes (sizeof (magic));

    const auto version = in.readInt();

    if (version > formatVersion)
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

    if (! contents.params.isValid())
    {
        contents.error = "The kit file has no settings in it.";
        return contents;
    }

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
