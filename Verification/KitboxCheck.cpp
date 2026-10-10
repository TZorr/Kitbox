//
//  KitboxCheck.cpp
//  Kitbox
//
//  Measures the engine against answers known by arithmetic, not against its
//  own earlier output: a Butterworth filter is 3.01 dB down at its cutoff, an
//  octave is twice the frequency, a decay set to 200 ms reaches -60 dB at
//  200 ms. A test that compares the engine with a recording of itself would
//  pass a filter that was wrong from the first day.
//
//  Links the engine sources the plugin runs, without the plugin machinery.
//

#include <juce_audio_formats/juce_audio_formats.h>

#include "Engine/DrumEngine.h"
#include "Engine/FxMigration.h"
#include "Engine/AuPreset.h"
#include "Engine/KitFile.h"
#include "Engine/TptFilter.h"

#include <cstdio>
#include <numeric>

namespace
{
    int failures = 0, checks = 0;

    void check (bool condition, const juce::String& what, const juce::String& detail = {})
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::printf ("FAIL: %s  %s\n", what.toRawUTF8(), detail.toRawUTF8());
        }
    }

    void near (double value, double expected, double tolerance, const juce::String& what)
    {
        check (std::abs (value - expected) <= tolerance, what,
               "(got " + juce::String (value, 4) + ", expected " + juce::String (expected, 4)
                   + " +- " + juce::String (tolerance, 4) + ")");
    }

    double toDb (double gain) { return 20.0 * std::log10 (juce::jmax (1.0e-12, gain)); }

    double rms (const float* data, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += (double) data[i] * data[i];
        return std::sqrt (sum / juce::jmax (1, n));
    }

    constexpr double rate = 48000.0;

    //==============================================================================
    /** Parameter storage the engine can point at, at the plugin's defaults. */
    struct Knobs
    {
        std::array<std::array<std::atomic<float>, PadParams::count>, KitParams::numPads> pad;
        std::array<std::atomic<float>, 2 + 4 * KitParams::Fx::numSlots> global;
        std::array<PadParams, KitParams::numPads> padParams {};
        GlobalParams globalParams;

        Knobs()
        {
            for (int p = 0; p < KitParams::numPads; ++p)
                for (int i = 0; i < PadParams::count; ++i)
                    *padParams[(size_t) p].slot (i) = &pad[(size_t) p][(size_t) i];

            globalParams.humanize    = &global[0];
            globalParams.masterLevel = &global[1];

            for (size_t slot = 0; slot < globalParams.fx.size(); ++slot)
                globalParams.fx[slot] = { &global[2 + slot * 4], &global[3 + slot * 4], &global[4 + slot * 4], &global[5 + slot * 4] };

            defaults();
        }

        void defaults()
        {
            for (size_t index = 0; index < padParams.size(); ++index)
            {
                auto& p = padParams[index];
                p.note->store ((float) (KitParams::firstNote + (int) index));
                p.choke->store (0.0f);
                p.output->store (0.0f);
                p.level->store (0.0f);  p.pan->store (0.0f);   p.tune->store (0.0f);  p.fine->store (0.0f);
                p.start->store (0.0f);  p.velocity->store (100.0f);
                p.attack->store (0.0f); p.hold->store (0.0f);  p.decay->store (KitParams::decayMaxMs);
                p.filterType->store (0.0f); p.cutoff->store (20000.0f); p.resonance->store (0.0f); p.drive->store (0.0f);
                p.envAttack->store (0.0f); p.envDecay->store (200.0f); p.envAmount->store (0.0f);
                p.sendReverb->store (0.0f); p.sendDelay->store (0.0f); p.sendPhaser->store (0.0f);
            }

            auto& g = globalParams;
            g.humanize->store (0.0f); g.masterLevel->store (0.0f);
            for (size_t slot = 0; slot < g.fx.size(); ++slot)
            {
                g.fx[slot].type->store ((float) KitParams::Fx::defaultTypes[slot]);
                g.fx[slot].a->store (KitParams::Fx::defaultA[slot]);
                g.fx[slot].b->store (KitParams::Fx::defaultB[slot]);
                g.fx[slot].level->store (0.0f);
            }
        }
    };

    juce::AudioBuffer<float> sine (double frequency, double seconds, double sampleRate, float amplitude = 0.5f)
    {
        juce::AudioBuffer<float> buffer (1, (int) (seconds * sampleRate));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate));
        return buffer;
    }

    juce::AudioBuffer<float> constant (float value, double seconds, double sampleRate)
    {
        juce::AudioBuffer<float> buffer (1, (int) (seconds * sampleRate));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, value);
        return buffer;
    }

    /** Frequency from rising zero crossings, linearly interpolated. */
    double measureFrequency (const float* data, int n, double sampleRate)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;

        for (int i = 1; i < n; ++i)
        {
            if (data[i - 1] < 0.0f && data[i] >= 0.0f)
            {
                const auto t = (double) (i - 1) + (double) data[i - 1] / ((double) data[i - 1] - (double) data[i]);
                if (first < 0.0) first = t;
                last = t;
                ++crossings;
            }
        }

        return crossings > 1 ? (crossings - 1) * sampleRate / (last - first) : 0.0;
    }

    struct Rendered
    {
        std::vector<float> left, right;
    };

    Rendered renderHit (DrumEngine& engine, int pad, float velocity, int samples)
    {
        Rendered out { std::vector<float> ((size_t) samples), std::vector<float> ((size_t) samples) };
        engine.trigger (pad, velocity);
        engine.render (out.left.data(), out.right.data(), samples);
        return out;
    }

    //==============================================================================
    double filterGainDb (KitParams::FilterType type, float cutoff, float resonance, double frequency,
                         float amplitude = 0.05f)
    {
        TptFilter filter;
        filter.set (type, cutoff, resonance, 0.0f, rate);

        const auto input = sine (frequency, 1.0, rate, amplitude);
        std::vector<float> output ((size_t) input.getNumSamples());

        for (int i = 0; i < input.getNumSamples(); ++i)
            output[(size_t) i] = filter.process (input.getSample (0, i));

        const auto skip = 24000;
        return toDb (rms (output.data() + skip, (int) output.size() - skip)
                     / rms (input.getReadPointer (0) + skip, input.getNumSamples() - skip));
    }

    void testFilter()
    {
        using T = KitParams::FilterType;

        near (filterGainDb (T::lp12, 1000.0f, 0.0f, 1000.0), -3.01, 0.1, "LP 12 is 3 dB down at its cutoff");
        near (filterGainDb (T::lp12, 1000.0f, 0.0f, 100.0), 0.0, 0.05, "LP 12 passes an octave-plus below");
        // Away from the cutoff the expected value is the Butterworth magnitude
        // at the *warped* frequency: the TPT filter is a bilinear transform,
        // exact at its cutoff and progressively steeper towards Nyquist. The
        // analogue formula would expect -36.1 dB at 8 kHz; the right answer at
        // 48 kHz is -37.8.
        const auto butterworth = [] (double f, double fc, int order)
        {
            const auto w = std::tan (juce::MathConstants<double>::pi * f / rate)
                         / std::tan (juce::MathConstants<double>::pi * fc / rate);
            return -10.0 * std::log10 (1.0 + std::pow (w, 2 * order));
        };

        near (filterGainDb (T::lp12, 1000.0f, 0.0f, 8000.0), butterworth (8000.0, 1000.0, 2), 0.05, "LP 12 is a second-order Butterworth");
        near (filterGainDb (T::lp24, 1000.0f, 0.0f, 1000.0), -3.01, 0.1, "LP 24 is 3 dB down at its cutoff");
        near (filterGainDb (T::lp24, 1000.0f, 0.0f, 4000.0), butterworth (4000.0, 1000.0, 4), 0.05, "LP 24 is a fourth-order Butterworth");
        near (filterGainDb (T::hp12, 1000.0f, 0.0f, 1000.0), -3.01, 0.1, "HP 12 is 3 dB down at its cutoff");
        near (filterGainDb (T::hp12, 1000.0f, 0.0f, 10000.0), 0.0, 0.1, "HP 12 passes well above");
        near (filterGainDb (T::hp24, 1000.0f, 0.0f, 1000.0), -3.01, 0.1, "HP 24 is 3 dB down at its cutoff");
        near (filterGainDb (T::bp, 1000.0f, 0.0f, 1000.0), 0.0, 0.1, "BP is unity at its centre");
        near (filterGainDb (T::off, 1000.0f, 0.0f, 5000.0), 0.0, 0.001, "Off passes everything");

        const auto peak = filterGainDb (T::lp12, 1000.0f, 1.0f, 1000.0, 0.005f);
        check (peak > 20.0, "full resonance peaks by more than 20 dB", "(got " + juce::String (peak, 1) + " dB)");

        // The saturating integrator: at full resonance a loud input must peak
        // clearly less than a quiet one - the compression is the analogue part.
        const auto loudPeak = filterGainDb (T::lp12, 1000.0f, 1.0f, 1000.0, 0.5f);
        check (loudPeak < peak - 6.0, "resonance compresses at high level",
               "(quiet " + juce::String (peak, 1) + " dB, loud " + juce::String (loudPeak, 1) + " dB)");

        // A full-resonance filter swept hard must stay finite.
        TptFilter filter;
        juce::Random random (7);
        bool finite = true;
        float biggest = 0.0f;

        for (int i = 0; i < 96000; ++i)
        {
            if (i % 16 == 0)
                filter.set (T::lp24, 20.0f + 19000.0f * (0.5f + 0.5f * std::sin ((float) i * 0.003f)), 1.0f, 1.0f, rate);
            const auto y = filter.process (random.nextFloat() * 2.0f - 1.0f);
            finite = finite && std::isfinite (y);
            biggest = juce::jmax (biggest, std::abs (y));
        }

        check (finite && biggest < 20.0f, "a fast full-resonance sweep stays bounded", "(peak " + juce::String (biggest, 2) + ")");
    }

    //==============================================================================
    void testVoice()
    {
        Knobs knobs;
        DrumEngine engine;
        engine.setParameters (knobs.padParams, knobs.globalParams);
        engine.prepare (rate, 512);

        auto& pad = knobs.padParams[0];
        auto tone = SampleData::fromAudio (sine (440.0, 1.0, 44100.0), 44100.0, "Sine");
        engine.setPadSample (0, tone.get());

        // Rate conversion alone: a 44.1 kHz file played at 48 kHz keeps its pitch.
        {
            const auto hit = renderHit (engine, 0, 1.0f, 24000);
            near (measureFrequency (hit.left.data() + 100, 20000, rate), 440.0, 0.05, "44.1 kHz sample plays at pitch at 48 kHz");
        }

        engine.reset();
        pad.tune->store (12.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 24000);
            near (measureFrequency (hit.left.data() + 100, 20000, rate), 880.0, 0.1, "Tune +12 st is an octave");
        }

        engine.reset();
        pad.tune->store (-7.0f);
        pad.fine->store (-50.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 24000);
            near (measureFrequency (hit.left.data() + 100, 20000, rate), 440.0 * std::exp2 (-7.5 / 12.0), 0.1,
                  "Tune -7 st and Fine -50 ct add up");
        }

        pad.tune->store (0.0f);
        pad.fine->store (0.0f);

        // Envelope, measured on a constant so only the envelope shapes it.
        auto flat = SampleData::fromAudio (constant (0.5f, 3.0, rate), rate, "Flat");
        engine.setPadSample (0, flat.get());
        constexpr double centreGain = 0.70710678;

        const auto levelAt = [] (const Rendered& r, double ms)
        {
            return r.left[(size_t) (ms * 0.001 * rate)] / (0.5 * centreGain);
        };

        engine.reset();
        {
            const auto hit = renderHit (engine, 0, 1.0f, 4800);
            near (hit.left[10], 0.5 * centreGain, 1.0e-4, "a centred pad is -3 dB per side");
            near (hit.right[10], hit.left[10], 1.0e-7, "a centred pad is equal in both sides");
        }

        engine.reset();
        pad.decay->store (200.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 24000);
            near (toDb (levelAt (hit, 200.0)), -60.0, 0.2, "Decay 200 ms reaches -60 dB at 200 ms");
            near (toDb (levelAt (hit, 100.0)), -30.0, 0.2, "and -30 dB half way");
        }

        engine.reset();
        pad.hold->store (100.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 24000);
            near (toDb (levelAt (hit, 99.0)), 0.0, 0.01, "Hold keeps full level for its length");
            near (toDb (levelAt (hit, 300.0)), -60.0, 0.3, "and the decay starts after it");
        }

        engine.reset();
        pad.hold->store (0.0f);
        pad.decay->store (KitParams::decayMaxMs);
        pad.attack->store (10.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 4800);
            near (levelAt (hit, 5.0), 0.5, 0.01, "Attack 10 ms is half way at 5 ms");
            near (levelAt (hit, 20.0), 1.0, 1.0e-4, "and complete after it");
        }

        pad.attack->store (0.0f);

        // Velocity and pan
        engine.reset();
        {
            const auto hit = renderHit (engine, 0, 0.5f, 480);
            near (toDb (levelAt (hit, 5.0)), -12.04, 0.05, "velocity 64 at full sensitivity is -12 dB");
        }

        engine.reset();
        pad.velocity->store (0.0f);
        {
            const auto hit = renderHit (engine, 0, 0.2f, 480);
            near (toDb (levelAt (hit, 5.0)), 0.0, 0.01, "sensitivity 0 ignores velocity");
        }

        pad.velocity->store (100.0f);
        engine.reset();
        pad.pan->store (-1.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 480);
            near (hit.right[100], 0.0, 1.0e-7, "pan hard left is silent on the right");
            near (hit.left[100], 0.5, 1.0e-4, "and full on the left");
        }

        pad.pan->store (0.0f);

        // Level knob at the bottom is silence.
        engine.reset();
        pad.level->store (KitParams::levelFloorDb);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 480);
            near (rms (hit.left.data(), 480), 0.0, 1.0e-9, "Level at -inf is silent");
        }

        pad.level->store (0.0f);

        // Start: on a ramp, 50 % starts half way up.
        juce::AudioBuffer<float> rampAudio (1, 48000);
        for (int i = 0; i < 48000; ++i)
            rampAudio.setSample (0, i, (float) i / 48000.0f);
        auto ramp = SampleData::fromAudio (rampAudio, rate, "Ramp");
        engine.setPadSample (0, ramp.get());

        engine.reset();
        pad.start->store (50.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 480);
            // The 1 ms click guard scales the first 48 samples; after it the
            // ramp continues from half way.
            near (hit.left[48] / centreGain, 0.5 + 48.0 / 48000.0, 1.0e-3, "Start 50 % plays from the middle");
        }

        pad.start->store (0.0f);

        // The end of the sample ends the voice.
        engine.reset();
        {
            auto shortHit = SampleData::fromAudio (constant (0.5f, 0.01, rate), rate, "Short");
            engine.setPadSample (0, shortHit.get());
            renderHit (engine, 0, 1.0f, 2000);
            check (engine.getActiveVoiceCount() == 0, "a voice frees itself at the end of its sample");
        }

        // Filter envelope: LP at 100 Hz with +48 st of envelope is open at the
        // hit and closed a second later.
        engine.reset();
        engine.setPadSample (0, tone.get());
        pad.filterType->store ((float) KitParams::FilterType::lp24);
        pad.cutoff->store (100.0f);
        pad.envAmount->store (48.0f);
        pad.envDecay->store (100.0f);
        {
            const auto hit = renderHit (engine, 0, 1.0f, 48000);
            const auto early = rms (hit.left.data() + 200, 480);
            const auto late  = rms (hit.left.data() + 40000, 480);
            check (toDb (late / early) < -25.0, "the filter envelope closes the filter",
                   "(" + juce::String (toDb (late / early), 1) + " dB)");
        }

        knobs.defaults();
    }

    //==============================================================================
    void testHumanize()
    {
        std::mt19937 generator (1);
        std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);
        const auto random = [&] { return uniform (generator); };

        float pitch = 0.0f, filter = 0.0f, level = 0.0f, start = 0.0f, pan = 0.0f;

        for (int i = 0; i < 5000; ++i)
        {
            const auto h = HumanizeOffsets::draw (1.0f, random);
            pitch  = juce::jmax (pitch, std::abs (h.pitchCents));
            filter = juce::jmax (filter, std::abs (h.filterFactor - 1.0f));
            level  = juce::jmax (level, std::abs (h.levelDb));
            start  = juce::jmax (start, std::abs (h.startMs));
            pan    = juce::jmax (pan, std::abs (h.pan));
        }

        check (pitch  <= 6.0f  && pitch  > 5.8f,   "humanize 100 % pitch spans +-6 ct",    juce::String (pitch));
        check (filter <= 0.10f && filter > 0.096f, "humanize 100 % filter spans +-10 %",   juce::String (filter));
        check (level  <= 2.0f  && level  > 1.94f,  "humanize 100 % level spans +-2 dB",    juce::String (level));
        check (start  <= 1.0f  && start  > 0.96f,  "humanize 100 % start spans +-1 ms",    juce::String (start));
        check (pan    <= 0.04f && pan    > 0.038f, "humanize 100 % pan spans +-4 %",       juce::String (pan));

        // 50 % is what 100 % was before 0.6.0: +-3 ct, +-5 %, +-1 dB, +-0.5 ms, +-2 %.
        {
            float p = 0.0f, f = 0.0f, l = 0.0f, s = 0.0f, n = 0.0f;
            for (int i = 0; i < 5000; ++i)
            {
                const auto h = HumanizeOffsets::draw (0.5f, random);
                p = juce::jmax (p, std::abs (h.pitchCents));
                f = juce::jmax (f, std::abs (h.filterFactor - 1.0f));
                l = juce::jmax (l, std::abs (h.levelDb));
                s = juce::jmax (s, std::abs (h.startMs));
                n = juce::jmax (n, std::abs (h.pan));
            }

            check (p <= 3.0f && p > 2.9f && f <= 0.05f && f > 0.048f && l <= 1.0f && l > 0.97f
                       && s <= 0.5f && s > 0.48f && n <= 0.02f && n > 0.019f,
                   "humanize 50 % spans what 100 % did before (+-3 ct, 5 %, 1 dB, 0.5 ms, 2 %)");
        }

        const auto none = HumanizeOffsets::draw (0.0f, random);
        check (none.pitchCents == 0.0f && none.filterFactor == 1.0f && none.levelDb == 0.0f
                   && none.startMs == 0.0f && none.pan == 0.0f,
               "humanize 0 changes nothing");

        // Through the engine: at 0 two hits are identical, at 100 they are not.
        Knobs knobs;
        DrumEngine engine;
        engine.setParameters (knobs.padParams, knobs.globalParams);
        engine.prepare (rate, 512);
        auto tone = SampleData::fromAudio (sine (220.0, 0.5, rate), rate, "Tone");
        engine.setPadSample (0, tone.get());

        const auto a = renderHit (engine, 0, 1.0f, 4800);
        engine.reset();
        const auto b = renderHit (engine, 0, 1.0f, 4800);
        check (a.left == b.left, "hits are bit-identical with humanize off");

        knobs.globalParams.humanize->store (100.0f);
        engine.reset();
        const auto c = renderHit (engine, 0, 1.0f, 4800);
        engine.reset();
        const auto d = renderHit (engine, 0, 1.0f, 4800);
        check (c.left != d.left, "hits differ with humanize on");
    }

    //==============================================================================
    void testPool()
    {
        Knobs knobs;
        DrumEngine engine;
        engine.setParameters (knobs.padParams, knobs.globalParams);
        engine.prepare (rate, 512);
        auto tone = SampleData::fromAudio (sine (220.0, 2.0, rate), rate, "Tone");
        engine.setPadSample (0, tone.get());

        std::vector<float> l (512), r (512);

        for (int i = 0; i < 200; ++i)
            engine.trigger (0, 1.0f);

        check (engine.getActiveVoiceCount() <= DrumEngine::numVoices, "never more than 64 voices");

        engine.render (l.data(), r.data(), 512);
        check (engine.getActiveVoiceCount() <= DrumEngine::maxPolyphony, "stolen voices fade within a block",
               "(" + juce::String (engine.getActiveVoiceCount()) + " active)");

        // A steal must not click: 60 staggered hits, then the output's largest
        // sample-to-sample step stays within what the sine itself produces.
        engine.reset();
        std::vector<float> all;
        for (int block = 0; block < 60; ++block)
        {
            engine.trigger (0, 0.2f);
            engine.render (l.data(), r.data(), 512);
            all.insert (all.end(), l.begin(), l.end());
        }

        float biggestStep = 0.0f, biggest = 0.0f;
        for (size_t i = 1; i < all.size(); ++i)
        {
            biggestStep = juce::jmax (biggestStep, std::abs (all[i] - all[i - 1]));
            biggest = juce::jmax (biggest, std::abs (all[i]));
        }

        check (biggestStep < biggest * 0.1f, "voice stealing is click-free",
               "(step " + juce::String (biggestStep, 4) + " of peak " + juce::String (biggest, 3) + ")");

        // SamplePool: frees only what nobody plays.
        SamplePool pool;
        auto sample = SampleData::fromAudio (constant (0.1f, 0.1, rate), rate, "Pool");
        auto* raw = sample.get();
        pool.add (sample);
        SampleData::Ptr voiceHold (raw);
        sample = nullptr;
        pool.retire (raw);
        pool.collect (0.0);
        check (pool.size() == 1, "the pool keeps a sample a voice still plays");
        voiceHold = nullptr;
        pool.collect (0.0);
        check (pool.size() == 0, "and frees it once nothing does");
    }

    //==============================================================================
    void testNotesChokeOutputs()
    {
        Knobs knobs;
        DrumEngine engine;
        engine.setParameters (knobs.padParams, knobs.globalParams);
        engine.prepare (rate, 512);

        auto tone = SampleData::fromAudio (sine (220.0, 2.0, rate), rate, "Tone");
        for (int pad = 0; pad < 3; ++pad)
            engine.setPadSample (pad, tone.get());

        std::vector<float> l (4800), r (4800);
        const auto hits = [&engine] (int pad) { return engine.getHitCount (pad); };

        // Notes
        engine.handleNoteOn (36, 1.0f);
        check (hits (0) == 1 && engine.getActiveVoiceCount() == 1, "note 36 plays pad 1 by default");

        engine.reset();
        knobs.padParams[0].note->store (60.0f);
        engine.handleNoteOn (36, 1.0f);
        check (hits (0) == 1 && engine.getActiveVoiceCount() == 0, "a pad moved to another note no longer answers its old one");
        engine.handleNoteOn (60, 1.0f);
        check (hits (0) == 2, "and answers the new one");

        engine.reset();
        knobs.padParams[1].note->store (60.0f);
        engine.handleNoteOn (60, 1.0f);
        check (engine.getActiveVoiceCount() == 2 && hits (1) == 1, "two pads on one note play together");
        knobs.defaults();

        // Choke
        engine.reset();
        knobs.padParams[0].choke->store (1.0f);
        knobs.padParams[1].choke->store (1.0f);
        engine.trigger (0, 1.0f);
        engine.render (l.data(), r.data(), 480);
        engine.trigger (1, 1.0f);
        engine.render (l.data(), r.data(), 48);
        check (engine.getActiveVoiceCount() == 2, "a choked voice fades rather than stopping dead");
        engine.render (l.data(), r.data(), 480);
        check (engine.getActiveVoiceCount() == 1, "and is gone 5 ms after the choking hit");

        engine.reset();
        engine.trigger (0, 1.0f);
        engine.render (l.data(), r.data(), 480);
        engine.trigger (0, 1.0f);
        engine.render (l.data(), r.data(), 480);
        check (engine.getActiveVoiceCount() == 1, "a pad in a group chokes itself");

        engine.reset();
        knobs.padParams[2].choke->store (2.0f);
        engine.trigger (0, 1.0f);
        engine.trigger (2, 1.0f);
        engine.render (l.data(), r.data(), 480);
        check (engine.getActiveVoiceCount() == 2, "different groups leave each other alone");

        engine.reset();
        knobs.padParams[0].note->store (70.0f);
        knobs.padParams[1].note->store (70.0f);
        engine.handleNoteOn (70, 1.0f);
        engine.render (l.data(), r.data(), 480);
        check (engine.getActiveVoiceCount() == 2, "layered pads in one group do not choke each other");
        engine.handleNoteOn (70, 1.0f);
        engine.render (l.data(), r.data(), 480);
        check (engine.getActiveVoiceCount() == 2, "but the next hit on that note chokes both");

        // The choke fade must not click: its steepest step stays within the
        // tone's own slope scaled by the fade, far below a hard cut.
        engine.reset();
        knobs.defaults();
        knobs.padParams[0].choke->store (1.0f);
        knobs.padParams[1].choke->store (1.0f);
        engine.setPadSample (1, nullptr);   // pad 2 silent: only the fade is heard
        engine.trigger (0, 1.0f);
        engine.render (l.data(), r.data(), 1000);
        const auto before = l[999];
        engine.trigger (1, 1.0f);
        engine.render (l.data(), r.data(), 480);
        float step = std::abs (l[0] - before);
        for (int i = 1; i < 480; ++i)
            step = juce::jmax (step, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
        check (step < 0.03f, "the choke fade is click-free", "(largest step " + juce::String (step, 4) + ")");
        engine.setPadSample (1, tone.get());
        knobs.defaults();

        // Outputs
        std::vector<std::vector<float>> buffers (8, std::vector<float> (4800));
        DrumEngine::Outputs outputs;
        outputs.left[0] = buffers[0].data(); outputs.right[0] = buffers[1].data();
        outputs.left[3] = buffers[2].data(); outputs.right[3] = buffers[3].data();

        engine.reset();
        knobs.padParams[0].output->store (3.0f);
        engine.trigger (0, 1.0f);
        engine.render (outputs, 4800);
        check (rms (buffers[2].data(), 4800) > 0.1 && rms (buffers[0].data(), 4800) < 1.0e-9,
               "a pad routed to Out 3 plays there and not on Main");

        engine.reset();
        knobs.padParams[0].output->store (5.0f);
        engine.trigger (0, 1.0f);
        engine.render (outputs, 4800);
        check (rms (buffers[0].data(), 4800) > 0.1 && rms (buffers[2].data(), 4800) < 1.0e-9,
               "a pad routed to an output the host has not enabled plays on Main");

        engine.reset();
        knobs.padParams[0].output->store (3.0f);
        knobs.padParams[0].sendReverb->store (100.0f);
        engine.trigger (0, 1.0f);
        engine.render (outputs, 4800);
        check (rms (buffers[0].data(), 4800) > 1.0e-4, "its reverb send still returns on Main");

        // Master scales the aux outputs too.
        engine.reset();
        knobs.defaults();
        knobs.padParams[0].output->store (3.0f);
        engine.trigger (0, 1.0f);
        engine.render (outputs, 4800);
        const auto full = rms (buffers[2].data() + 2400, 2400);
        engine.reset();
        knobs.globalParams.masterLevel->store (-6.0f);
        engine.render (outputs, 512);   // let the master ramp settle
        engine.trigger (0, 1.0f);
        engine.render (outputs, 4800);
        near (toDb (rms (buffers[2].data() + 2400, 2400) / full), -6.0, 0.05, "Master applies to the aux outputs");
        knobs.defaults();
    }

    //==============================================================================
    void testEffects()
    {
        namespace F = KitParams::Fx;

        Knobs knobs;
        DrumEngine engine;
        engine.setParameters (knobs.padParams, knobs.globalParams);
        engine.prepare (rate, 512);

        juce::AudioBuffer<float> clickAudio (1, 480);
        clickAudio.clear();
        clickAudio.setSample (0, 0, 1.0f);
        auto click = SampleData::fromAudio (clickAudio, rate, "Click");
        auto tone  = SampleData::fromAudio (sine (220.0, 4.0, rate), rate, "Tone");

        auto& pad = knobs.padParams[0];
        auto& g = knobs.globalParams;

        const auto run = [&engine] (int samples)
        {
            Rendered out { std::vector<float> ((size_t) samples), std::vector<float> ((size_t) samples) };
            engine.render (out.left.data(), out.right.data(), samples);
            return out;
        };

        const auto finite = [] (const Rendered& r)
        {
            for (size_t i = 0; i < r.left.size(); ++i)
                if (! std::isfinite (r.left[i]) || ! std::isfinite (r.right[i]))
                    return false;
            return true;
        };

        // Reverbs: an impulse into the send comes back finite, in stereo, and dies away.
        engine.setPadSample (0, click.get());
        pad.sendReverb->store (100.0f);

        for (int type = 0; type < EffectCatalog::numSubtypes (F::categories[F::reverb]); ++type)
        {
            const auto name = juce::String (EffectCatalog::info (F::categories[F::reverb], type).name);
            engine.reset();
            g.fx[F::reverb].type->store ((float) type);
            engine.trigger (0, 1.0f);
            const auto out = run ((int) (6.0 * rate));
            const auto early = rms (out.left.data() + 2400, 24000);
            const auto late  = rms (out.left.data() + (int) (5.5 * rate), 24000);

            check (finite (out), name + " reverb is finite");
            check (early > 1.0e-4 && toDb (late / early) < -30.0, name + " reverb returns and decays",
                   "(" + juce::String (toDb (late / early), 1) + " dB)");
            check (out.left != out.right, name + " reverb returns stereo from a mono send");
        }

        pad.sendReverb->store (0.0f);

        // Delays: a quarter note at 120 BPM is 24000 samples at 48 kHz - the
        // slot is always tempo-synced. "1/4" is step 8 of Dsp::Sync's fourteen.
        pad.sendDelay->store (100.0f);
        g.fx[F::delay].a->store (8.0f / 13.0f);
        g.fx[F::delay].b->store (0.5f);
        engine.setTempo (120.0);

        for (int type = 0; type < EffectCatalog::numSubtypes (F::categories[F::delay]); ++type)
        {
            const auto name = juce::String (EffectCatalog::info (F::categories[F::delay], type).name);
            engine.reset();
            g.fx[F::delay].type->store ((float) type);
            engine.trigger (0, 1.0f);
            const auto out = run ((int) (1.2 * rate));

            int loudest = 0;
            float peak = 0.0f;
            for (int i = 1000; i < 36000; ++i)
            {
                const auto level = std::abs (out.left[(size_t) i]) + std::abs (out.right[(size_t) i]);
                if (level > peak) { peak = level; loudest = i; }
            }

            // The tape delay's transport wobbles (wow, +-0.35 ms = +-17 samples)
            // and its filters smear the click, so it is held to "within half a
            // millisecond, about the pad's level"; the others are exact.
            const auto tape = type == 1;
            check (finite (out), name + " delay is finite");
            check (std::abs (loudest - 24000) <= (tape ? 24 : 2), name + " delay echoes a quarter note at 120 BPM",
                   "(at sample " + juce::String (loudest) + ")");

            const auto echo = juce::jmax (std::abs (out.left[(size_t) loudest]), std::abs (out.right[(size_t) loudest]));
            if (type == 0)
                near (echo, 0.70710678, 0.01, name + " delay's echo is the pad's centred level");
            else
                check (echo > 0.2f && echo < 1.0f, name + " delay's echo is near the pad's level", juce::String (echo, 3));
        }

        // Ping-pong: the first echo on one side, the second on the other.
        {
            engine.reset();
            g.fx[F::delay].type->store (2.0f);
            engine.trigger (0, 1.0f);
            const auto out = run ((int) (1.2 * rate));
            const auto side = [&out] (int at) { return std::abs (out.left[(size_t) at]) > std::abs (out.right[(size_t) at]) ? 0 : 1; };
            const auto firstPeak  = std::abs (out.left[24000]) + std::abs (out.right[24000]);
            const auto secondPeak = std::abs (out.left[48000]) + std::abs (out.right[48000]);
            check (firstPeak > 0.1f && secondPeak > 0.05f && side (24000) != side (48000), "ping-pong alternates sides");
        }

        pad.sendDelay->store (0.0f);

        // Modulation: a held tone through each type comes back finite and changed.
        // The pad itself goes to Out 3, so Main carries only the return (sends
        // are taken after the pad's level, so muting the pad would mute them too).
        engine.setPadSample (0, tone.get());
        pad.sendPhaser->store (100.0f);
        pad.output->store (3.0f);

        std::vector<float> auxLeft ((size_t) rate), auxRight ((size_t) rate);
        const auto runReturn = [&engine, &auxLeft, &auxRight] (int samples)
        {
            Rendered out { std::vector<float> ((size_t) samples), std::vector<float> ((size_t) samples) };
            DrumEngine::Outputs outputs;
            outputs.left[0] = out.left.data();  outputs.right[0] = out.right.data();
            outputs.left[3] = auxLeft.data();   outputs.right[3] = auxRight.data();
            engine.render (outputs, samples);
            return out;
        };

        std::vector<float> reference;

        for (int type = 0; type < EffectCatalog::numSubtypes (F::categories[F::mod]); ++type)
        {
            const auto name = juce::String (EffectCatalog::info (F::categories[F::mod], type).name);
            engine.reset();
            g.fx[F::mod].type->store ((float) type);
            engine.trigger (0, 1.0f);
            const auto out = runReturn ((int) rate);

            check (finite (out) && rms (out.left.data() + 4800, 38400) > 0.05, name + " returns the tone");

            if (type > 0)
                check (out.left != reference, name + " sounds different from the chorus");
            else
                reference = out.left;
        }

        // Switching the type while the tone plays crossfades: no sample step
        // around the switch is larger than the steadiest part of the signal's own.
        {
            engine.reset();
            g.fx[F::mod].type->store (3.0f);   // Tremolo
            engine.trigger (0, 1.0f);
            const auto before = runReturn (24000);
            g.fx[F::mod].type->store (1.0f);   // Phaser
            const auto after = runReturn (24000);

            float steady = 0.0f, across = 0.0f;
            for (size_t i = 12001; i < 24000; ++i)
                steady = juce::jmax (steady, std::abs (before.left[i] - before.left[i - 1]));

            across = std::abs (after.left[0] - before.left[23999]);
            for (size_t i = 1; i < 4800; ++i)
                across = juce::jmax (across, std::abs (after.left[i] - after.left[i - 1]));

            check (across < steady * 2.0f, "changing the Mod type does not click",
                   "(step " + juce::String (across, 4) + " vs steady " + juce::String (steady, 4) + ")");
        }

        knobs.defaults();
    }

    //==============================================================================
    void testMigration()
    {
        namespace L = KitParams::Legacy;
        namespace F = KitParams::Fx;

        const auto oldTree = [] (std::initializer_list<std::pair<const char*, float>> values)
        {
            juce::ValueTree state (KitParams::stateTreeType);
            for (const auto& [id, value] : values)
            {
                juce::ValueTree param ("PARAM");
                param.setProperty ("id", juce::String (id), nullptr);
                param.setProperty ("value", value, nullptr);
                state.appendChild (param, nullptr);
            }
            return state;
        };

        const auto value = [] (const juce::ValueTree& state, const char* id)
        {
            return (float) state.getChildWithProperty ("id", juce::String (id)).getProperty ("value", -1.0f);
        };

        auto state = oldTree ({ { L::reverbSize, 70.0f }, { L::reverbDamping, 20.0f }, { L::reverbPreDelay, 30.0f },
                                { L::delayTime, 12.0f }, { L::delayFeedback, 57.0f }, { L::delayTone, 10.0f },
                                { L::phaserRate, 1.0f }, { L::phaserDepth, 30.0f }, { L::phaserFeedback, 60.0f },
                                { "rev_level", -6.0f } });

        check (FxMigration::apply (state), "an old kit is translated");
        near (value (state, F::ids[F::reverb].a), 0.70, 1.0e-6, "reverb size becomes A");
        near (value (state, F::ids[F::reverb].b), 0.20, 1.0e-6, "reverb damping becomes B");
        near (value (state, F::ids[F::delay].a), 1.0, 1.0e-6, "the old 1/1 is the new 1/1");
        near (value (state, F::ids[F::delay].b), 0.6, 1.0e-6, "feedback 57 % is B 0.6 (x 0.95)");
        near (value (state, F::ids[F::mod].a), std::log (20.0) / std::log (200.0), 1.0e-5, "1 Hz on the 0.05-10 Hz map");
        near (value (state, F::ids[F::mod].b), 0.3, 1.0e-6, "phaser depth becomes B");
        near (value (state, F::ids[F::delay].type), 1.0, 0.0, "the old delay becomes Tape");
        near (value (state, F::ids[F::mod].type), 1.0, 0.0, "the old phaser becomes the Phaser (index 1, after Chorus)");
        near (value (state, "rev_level"), -6.0, 0.0, "return levels are kept as they were");
        check (! state.getChildWithProperty ("id", juce::String (L::reverbSize)).isValid()
                   && ! state.getChildWithProperty ("id", juce::String (L::phaserFeedback)).isValid(),
               "and the old knobs are gone");
        check (! FxMigration::apply (state), "a translated kit is left alone");

        // The old defaults translate to the new defaults, so an untouched old
        // kit and a fresh Kitbox agree.
        auto defaults = oldTree ({ { L::reverbSize, 50.0f }, { L::reverbDamping, 40.0f }, { L::delayTime, 5.0f },
                                   { L::delayFeedback, 35.0f }, { L::phaserRate, 0.4f }, { L::phaserDepth, 70.0f } });
        FxMigration::apply (defaults);
        for (int slot = 0; slot < F::numSlots; ++slot)
        {
            near (value (defaults, F::ids[slot].a), F::defaultA[slot], 0.005, juce::String (F::titles[slot]) + " A default matches the old one");
            near (value (defaults, F::ids[slot].b), F::defaultB[slot], 0.005, juce::String (F::titles[slot]) + " B default matches the old one");
        }
    }

    //==============================================================================
    void testSamplesAndKitFile()
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        // Stereo folds to the average.
        juce::AudioBuffer<float> stereo (2, 4800);
        for (int i = 0; i < 4800; ++i)
        {
            stereo.setSample (0, i, 0.5f);
            stereo.setSample (1, i, 0.25f);
        }

        auto sample = SampleData::fromAudio (stereo, 44100.0, "Stereo Kick");
        check (sample != nullptr, "a stereo WAV decodes");
        near (sample->getMono()[100], 0.375, 1.0e-5, "stereo folds to mono by averaging");
        check (sample->getSourceChannels() == 2, "the source channel count is kept");
        check (sample->getName() == "Stereo Kick", "the name comes from the file name", sample->getName());
        check ((int) sample->getOverview().size() == SampleData::overviewSize, "the overview is built");

        juce::String error;
        juce::MemoryBlock junk ("not audio at all", 16);
        check (SampleData::decode (junk, "junk.wav", formats, error) == nullptr && error.isNotEmpty(),
               "garbage is refused with a reason", error);

        // Kit file round trip, bytes exact.
        juce::ValueTree params (KitParams::stateTreeType);
        params.setProperty ("selectedPad", 5, nullptr);
        juce::ValueTree child ("PARAM");
        child.setProperty ("id", "pad01_level", nullptr);
        child.setProperty ("value", -6.5, nullptr);
        params.appendChild (child, nullptr);

        std::array<SampleData::Ptr, KitParams::numPads> samples;
        samples[0]  = sample;
        samples[15] = SampleData::fromAudio (sine (100.0, 0.2, rate), rate, "Last Pad");

        const auto block = KitFile::write (params, samples);
        const auto kit = KitFile::read (block.getData(), block.getSize());

        check (kit.ok(), "a written kit reads back", kit.error);
        check (kit.params.isEquivalentTo (params), "the settings come back unchanged");
        check (kit.samples[0].bytes == sample->getOriginal(), "pad 1's file comes back byte for byte");
        check (kit.samples[15].bytes == samples[15]->getOriginal(), "pad 16's file comes back byte for byte");
        check (kit.samples[0].fileName == "Stereo Kick.wav", "and under its name", kit.samples[0].fileName);
        check (kit.samples[3].bytes.getSize() == 0, "empty pads stay empty");

        check (kit.version == KitFile::formatVersion && std::memcmp (block.getData(), "PK\3\4", 4) == 0,
               "a kit is written as a ZIP (version 2)");

        // With a synth beside a sample: the WAV and its parameters come back.
        {
            KitFile::Synths synths;
            transmute::DrumParams drum;
            drum.model = transmute::DrumModel::snare;
            drum.fundamental = 187.123456789;
            synths[15] = { SampleData::fromAudio (sine (200.0, 0.1, rate), rate, "Last Pad"), drum };

            const auto withSynth = KitFile::read (KitFile::write (params, samples, synths).getData(),
                                                  KitFile::write (params, samples, synths).getSize());
            check (withSynth.ok() && withSynth.synths[15].file.bytes == synths[15].sample->getOriginal()
                       && withSynth.synths[15].file.fileName == "Last Pad.wav"
                       && withSynth.synths[15].params == drum.clamped()
                       && withSynth.synths[0].file.bytes.getSize() == 0,
                   "a pad's synth and its parameters come back exactly");
            check (KitFile::entryName (2, "Kick: 1/2.wav") == "03 Kick- 1-2.wav", "file names are made safe in the archive",
                   KitFile::entryName (2, "Kick: 1/2.wav"));
        }

        // Version 1, as Kitbox wrote it up to 0.6 and Transmute's Export Kitbox Kit still does.
        {
            const auto file = juce::File (KITBOX_SOURCE_DIR).getChildFile ("Kits/Transmute Kit.aupreset");
            const auto preset = AuPreset::read (file.loadFileAsString());
            const auto old = KitFile::read (preset.pluginState.getData(), preset.pluginState.getSize());
            int filled = 0;
            for (const auto& stored : old.samples)
                filled += stored.bytes.getSize() > 0 ? 1 : 0;

            check (preset.ok() && old.ok() && old.version == 1 && filled > 0 && old.params.isValid(),
                   "a version 1 kit (Transmute's export) still reads", old.error + " " + juce::String (filled) + " samples");
        }

        auto damaged = block;
        damaged[0] = 'X';
        check (! KitFile::read (damaged.getData(), damaged.getSize()).ok(), "a file that is not a kit is refused");
        check (! KitFile::read (block.getData(), 20).ok(), "a truncated kit is refused");

        // .aupreset round trip
        const auto text = AuPreset::write (block, "Test");
        const auto preset = AuPreset::read (text);
        check (preset.ok(), "an .aupreset reads back", preset.error);
        check (preset.pluginState == block, "and carries the kit byte for byte");
        check (preset.name == "Test", "under its name, no extension", preset.name);
        check (text.contains ("<integer>" + juce::String (AuPreset::subtype) + "</integer>")
                   && juce::String (AuPreset::subtype) == "1265918584" && juce::String (AuPreset::manufacturer) == "1417310066",
               "with Kitbox's codes (Ktbx / Tzor)");

        const auto foreign = text.replace (juce::String (AuPreset::subtype), "1685677425");   // Gullfoss's subtype
        check (! AuPreset::read (foreign).ok(), "another plugin's preset is refused");
        check (! AuPreset::read ("not a plist").ok(), "a file that is not a preset is refused");

        // One Logic itself wrote, if it is still on this machine.
        const auto logicFile = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                   .getChildFile ("Downloads/KITBOX 808.kitbox.aupreset");
        if (logicFile.existsAsFile())
        {
            const auto fromLogic = AuPreset::read (logicFile.loadFileAsString());
            check (fromLogic.ok() && KitFile::read (fromLogic.pluginState.getData(), fromLogic.pluginState.getSize()).ok(),
                   "a preset Logic saved reads as a kit", fromLogic.error);
        }
    }
}

namespace
{
    void testFormatting()
    {
        // The Mod slot's Shifter: never more than two decimals, at any of 1001 knob positions.
        juce::String worst;
        for (int i = 0; i <= 1000; ++i)
        {
            const auto number = EffectCatalog::format (EffectCatalog::Unit::ShiftHz, (float) i / 1000.0f)
                                    .upToFirstOccurrenceOf (" ", false, false);
            if (number.contains (".") && number.fromFirstOccurrenceOf (".", false, false).length() > 2)
                worst = number;
        }

        check (worst.isEmpty(), "the Shifter's Shift never prints more than two decimals", worst);
    }
}

namespace TransmuteCheck
{
    void run (const juce::File& golden, const std::function<void (bool, const juce::String&, const juce::String&)>& check);
}

int main()
{
    std::printf ("Filter...\n");         testFilter();
    std::printf ("Voice...\n");          testVoice();
    std::printf ("Humanize...\n");       testHumanize();
    std::printf ("Voice pool...\n");     testPool();
    std::printf ("Notes, choke, outputs...\n"); testNotesChokeOutputs();
    std::printf ("Effects...\n");        testEffects();
    std::printf ("Formatting...\n");     testFormatting();
    std::printf ("Old kits...\n");       testMigration();
    std::printf ("Samples, kit file...\n"); testSamplesAndKitFile();
    std::printf ("Transmute port...\n");
    TransmuteCheck::run (juce::File (KITBOX_SOURCE_DIR).getChildFile ("build/transmute-golden"),
                         [] (bool condition, const juce::String& what, const juce::String& detail) { check (condition, what, detail); });

    std::printf ("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
