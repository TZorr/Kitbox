//
//  AnalysisInput.cpp
//  Kitbox
//

#include "AnalysisInput.h"
#include "Analyzer.h"

#include <AudioToolbox/AudioToolbox.h>
#include <Accelerate/Accelerate.h>

#include <algorithm>
#include <cmath>

namespace transmute
{
    namespace
    {
        AudioStreamBasicDescription floatFormat (double rate, int channels)
        {
            AudioStreamBasicDescription format {};
            format.mSampleRate       = rate;
            format.mFormatID         = kAudioFormatLinearPCM;
            format.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
            format.mBytesPerPacket   = sizeof (float);
            format.mFramesPerPacket  = 1;
            format.mBytesPerFrame    = sizeof (float);
            format.mChannelsPerFrame = (UInt32) channels;
            format.mBitsPerChannel   = 32;
            return format;
        }

        struct Feed
        {
            const std::vector<const float*>* channels;
            int frames;
            int position = 0;
            std::vector<float> silence;
        };

        OSStatus supply (AudioConverterRef, UInt32* packets, AudioBufferList* data,
                         AudioStreamPacketDescription**, void* context)
        {
            auto& feed = *static_cast<Feed*> (context);
            const auto remaining = feed.frames - feed.position;

            if (remaining <= 0)
            {
                // End of stream: no frames, and buffers that point somewhere.
                *packets = 0;
                for (UInt32 c = 0; c < data->mNumberBuffers; ++c)
                {
                    data->mBuffers[c].mData = feed.silence.data();
                    data->mBuffers[c].mDataByteSize = 0;
                }
                return noErr;
            }

            const auto count = std::min ((int) *packets, remaining);

            for (UInt32 c = 0; c < data->mNumberBuffers; ++c)
            {
                data->mBuffers[c].mData = const_cast<float*> ((*feed.channels)[c] + feed.position);
                data->mBuffers[c].mDataByteSize = (UInt32) count * sizeof (float);
                data->mBuffers[c].mNumberChannels = 1;
            }

            feed.position += count;
            *packets = (UInt32) count;
            return noErr;
        }

        /** Every channel at analysisRate, through one converter as AVAudioConverter does. */
        std::vector<std::vector<float>> resample (const std::vector<const float*>& channels, int frames, double sourceRate)
        {
            const auto count = (int) channels.size();
            const auto in = floatFormat (sourceRate, count);
            const auto out = floatFormat (analysisRate, count);

            AudioConverterRef converter = nullptr;
            if (AudioConverterNew (&in, &out, &converter) != noErr)
                return {};

            UInt32 complexity = kAudioConverterSampleRateConverterComplexity_Mastering;
            UInt32 quality = kAudioConverterQuality_Max;
            AudioConverterSetProperty (converter, kAudioConverterSampleRateConverterComplexity, sizeof (complexity), &complexity);
            AudioConverterSetProperty (converter, kAudioConverterSampleRateConverterQuality, sizeof (quality), &quality);

            Feed feed { &channels, frames, 0, std::vector<float> (16, 0.0f) };
            std::vector<std::vector<float>> result ((size_t) count);
            const UInt32 chunk = 32768;
            std::vector<std::vector<float>> block ((size_t) count, std::vector<float> (chunk));

            std::vector<unsigned char> listMemory (offsetof (AudioBufferList, mBuffers) + sizeof (AudioBuffer) * (size_t) count);
            auto* list = reinterpret_cast<AudioBufferList*> (listMemory.data());

            for (;;)
            {
                list->mNumberBuffers = (UInt32) count;
                for (int c = 0; c < count; ++c)
                {
                    list->mBuffers[c].mNumberChannels = 1;
                    list->mBuffers[c].mData = block[(size_t) c].data();
                    list->mBuffers[c].mDataByteSize = chunk * sizeof (float);
                }

                UInt32 produced = chunk;
                const auto status = AudioConverterFillComplexBuffer (converter, supply, &feed, &produced, list, nullptr);

                for (int c = 0; c < count; ++c)
                    result[(size_t) c].insert (result[(size_t) c].end(), block[(size_t) c].begin(), block[(size_t) c].begin() + produced);

                if (status != noErr || produced == 0)
                    break;
            }

            AudioConverterDispose (converter);
            return result;
        }
    }

    Floats toAnalysisRate (const std::vector<const float*>& channels, int frames, double sourceRate)
    {
        if (channels.empty() || frames <= 0)
            return {};

        std::vector<std::vector<float>> converted;

        if (std::abs (sourceRate - analysisRate) < 1.0e-9)
        {
            for (const auto* channel : channels)
                converted.emplace_back (channel, channel + frames);
        }
        else
        {
            converted = resample (channels, frames, sourceRate);
            if (converted.empty() || converted.front().empty())
                return {};
        }

        // The average, as Transmute's Decoder folds: the first channel, the
        // others added one by one, then scaled.
        Floats mono (converted.front().begin(), converted.front().end());

        if (converted.size() > 1)
        {
            const auto n = (vDSP_Length) mono.size();
            for (size_t c = 1; c < converted.size(); ++c)
                vDSP_vadd (mono.data(), 1, converted[c].data(), 1, mono.data(), 1, n);

            float scale = 1 / (float) converted.size();
            vDSP_vsmul (mono.data(), 1, &scale, mono.data(), 1, n);
        }

        return mono;
    }
}
