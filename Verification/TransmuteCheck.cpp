//
//  TransmuteCheck.cpp
//  Kitbox
//
//  Holds the C++ port of Transmute's engine (Source/Transmute) against the
//  Swift original: Verification/transmute_golden.sh runs Transmute's own
//  analysis, fit and synth on its test samples and writes what they gave;
//  this reads those files and asks the port for the same.
//
//  The port is meant to be exact, not close: the same arithmetic in the same
//  order, no fused multiply-adds (-ffp-contract=off), the same vDSP and
//  vForce calls. A fit is thousands of steps that each depend on the last, so
//  "close" in the first render can end in a different drum - which is why the
//  fit is compared, not only the synth. Where it is not bit for bit, the
//  tolerances below say how far it may be.
//
//  Skipped, with a note, when the golden folder is not there (the test
//  samples are not part of either repository).
//

#include <juce_core/juce_core.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Engine/PadTransmuter.h"
#include "Transmute/AnalysisInput.h"
#include "Transmute/Analyzer.h"
#include "Transmute/DrumSynth.h"
#include "Transmute/Fitter.h"

#include <cstdio>
#include <functional>

namespace TransmuteCheck
{
    using Check = std::function<void (bool, const juce::String&, const juce::String&)>;

    void run (const juce::File& golden, const Check& check);

    namespace
    {
        using namespace transmute;

        Floats readFloats (const juce::File& file)
        {
            juce::MemoryBlock block;
            file.loadFileAsData (block);
            Floats out (block.getSize() / sizeof (float));
            std::memcpy (out.data(), block.getData(), out.size() * sizeof (float));
            return out;
        }

        std::optional<DrumParams> readParams (const juce::File& file)
        {
            return DrumParams::fromJson (file.loadFileAsString().toStdString());
        }

        /** The largest difference over every parameter, relative where the value is not tiny. */
        double paramsDifference (const DrumParams& a, const DrumParams& b, juce::String& worst)
        {
            double largest = 0;
            for (const auto& s : allSpecs())
            {
                const double x = a.*s.member, y = b.*s.member;
                const double d = std::abs (x - y) / std::max (1.0, std::max (std::abs (x), std::abs (y)));
                if (d > largest)
                {
                    largest = d;
                    worst = juce::String (s.id) + " " + juce::String (x, 9) + " vs " + juce::String (y, 9);
                }
            }
            if (a.model != b.model)
            {
                largest = 1;
                worst = "model";
            }
            if (a.attack.has_value() != b.attack.has_value())
            {
                largest = 1;
                worst = "attack table present in one only";
            }
            return largest;
        }

        double peakDifferenceDB (const Floats& a, const Floats& b)
        {
            if (a.size() != b.size())
                return 1000;
            double largest = 0;
            for (size_t i = 0; i < a.size(); ++i)
                largest = std::max (largest, (double) std::abs (a[i] - b[i]));
            return 20 * std::log10 (std::max (largest, 1e-30));
        }
    }

    void run (const juce::File& golden, const Check& check)
    {
        // The JSON round trip, on its own: every field, an attack table, bits exact.
        {
            DrumParams p;
            p.model = DrumModel::modal;
            p.fundamental = 57.123456789012345;
            p.clickDecay = 1.0e-5 * 3.3;
            p.filterType = FilterType::bandPass;
            p.envelopeOn = true;
            p.autoLength = false;
            p.modalLevel4 = 0.333333333333333;
            p.attack = AttackTable { { 80, 100.79 }, 0.001, { { 0.1f, 0.0f, 1.0e-7f }, { 0.25f, 0.5f, 0.75f } } };
            const auto back = DrumParams::fromJson (p.toJson());
            check (back.has_value() && *back == p.clamped(), "a .drumparams file round-trips bit for bit", {});
            check (! DrumParams::fromJson ("not json").has_value(), "a file that is not JSON is refused", {});

            // Swift writes small values in plain decimals; juce::JSON read this one 7 ulp off.
            const auto small = DrumParams::fromJson ("{ \"delay\" : 0.0005045075527284687 }");
            check (small.has_value() && juce::exactlyEqual (small->delay, 0x1.0881db8434c43p-11), "a long decimal reads to the nearest double", {});
            check (DrumParams::fromJson ("{}").has_value() && *DrumParams::fromJson ("{}") == DrumParams().clamped(),
                   "a file with no keys opens at the defaults", {});
        }

        if (! golden.isDirectory())
        {
            std::printf ("  skipped the comparison with Transmute: no %s (run Verification/transmute_golden.sh)\n",
                         golden.getFullPathName().toRawUTF8());
            return;
        }

        const auto inputs = golden.findChildFiles (juce::File::findFiles, false, "*.in.f32");
        check (! inputs.isEmpty(), "the golden folder holds test cases", golden.getFullPathName());

        int exactFits = 0;

        for (const auto& input : inputs)
        {
            const auto base = input.getFullPathName().dropLastCharacters (7);   // ".in.f32"
            const auto name = juce::File (base).getFileName();
            const auto file = [&base] (const char* suffix) { return juce::File (base + suffix); };

            const auto audio = readFloats (input);
            const auto swiftInitial = readParams (file (".initial.drumparams"));
            const auto swiftFit = readParams (file (".fit.drumparams"));
            const auto swiftShaped = readParams (file (".shaped.drumparams"));
            const auto swiftModel = modelFromId (file (".suggested.txt").loadFileAsString().trim().toStdString());
            const auto swiftScore = file (".score.txt").loadFileAsString().getDoubleValue();

            if (! (swiftInitial && swiftFit && swiftShaped && swiftModel))
            {
                check (false, name + ": the golden files read", {});
                continue;
            }

            // The way in: the file decoded, resampled and folded as Transmute's Decoder does.
            const juce::File original (file (".file.txt").loadFileAsString().trim());
            if (original.existsAsFile())
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (original));
                if (reader != nullptr)
                {
                    juce::AudioBuffer<float> source ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&source, 0, source.getNumSamples(), 0, true, true);
                    std::vector<const float*> channels;
                    for (int c = 0; c < source.getNumChannels(); ++c)
                        channels.push_back (source.getReadPointer (c));
                    const auto converted = toAnalysisRate (channels, source.getNumSamples(), reader->sampleRate);
                    check (converted == audio, name + ": reaches the analysis sample for sample as in Transmute",
                           juce::String (reader->sampleRate) + " Hz, " + juce::String (reader->numChannels) + " ch, "
                               + juce::String ((int) converted.size()) + " vs " + juce::String ((int) audio.size()) + " frames, "
                               + juce::String (peakDifferenceDB (converted, audio), 1) + " dB");
                }
                else
                {
                    check (false, name + ": the original file reads", original.getFullPathName());
                }
            }

            // The synth alone: the Swift fit's parameters, rendered here.
            check (DrumSynth::render (*swiftFit, 48000) == readFloats (file (".render48.f32")),
                   name + ": renders sample for sample as Transmute does at 48 kHz",
                   juce::String (peakDifferenceDB (DrumSynth::render (*swiftFit, 48000), readFloats (file (".render48.f32"))), 1) + " dB");
            check (DrumSynth::render (*swiftShaped, 44100) == readFloats (file (".render44.f32")),
                   name + ": renders sample for sample as Transmute does at 44.1 kHz, with filter, envelope and a set length",
                   juce::String (peakDifferenceDB (DrumSynth::render (*swiftShaped, 44100), readFloats (file (".render44.f32"))), 1) + " dB");

            // The analysis.
            std::string error;
            const auto analysis = Analyzer::analyze (audio, analysisRate, std::nullopt, error);
            if (! analysis)
            {
                check (false, name + ": analyses", error);
                continue;
            }

            check (analysis->suggested == *swiftModel, name + ": suggests the model Transmute does",
                   juce::String (modelId (analysis->suggested)) + " vs " + modelId (*swiftModel));

            juce::String worst;
            const auto initialDifference = paramsDifference (analysis->initial, *swiftInitial, worst);
            check (juce::exactlyEqual (initialDifference, 0.0) && analysis->initial == *swiftInitial, name + ": first guess bit for bit as Transmute's", worst);

            // The fit.
            const auto report = Fitter::fit (*analysis);
            if (! report)
            {
                check (false, name + ": fits", {});
                continue;
            }

            const auto fitDifference = paramsDifference (report->params, *swiftFit, worst);
            const auto scoreDifference = std::abs (report->score.total() - swiftScore);
            if (juce::exactlyEqual (fitDifference, 0.0))
                ++exactFits;

            check (juce::exactlyEqual (fitDifference, 0.0) && report->params == *swiftFit, name + ": fits bit for bit as Transmute does", worst);
            check (scoreDifference < 1e-12, name + ": fits to Transmute's Match",
                   juce::String (report->score.total(), 4) + " vs " + juce::String (swiftScore, 4) + " (" + worst + ")");

            std::printf ("  %-40s %-6s match %7.3f dB (Transmute %7.3f)  params %s  %.1f s\n",
                         name.toRawUTF8(), modelId (analysis->suggested), report->score.total(), swiftScore,
                         juce::exactlyEqual (fitDifference, 0.0) ? "exact" : (juce::String (fitDifference) + " " + worst).toRawUTF8(), report->seconds);
        }

        std::printf ("  %d of %d fits bit for bit as Transmute's\n", exactFits, inputs.size());
        check (exactFits == inputs.size(), "every fit bit for bit as Transmute's", {});
    }
}
