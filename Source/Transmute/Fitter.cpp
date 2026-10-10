//
//  Fitter.cpp
//  Kitbox
//
//  Line for line Fitter.swift and NelderMead.swift. The grids run in parallel
//  and pick their winner in the order the loops had, first on a tie, so a fit
//  comes out the same at any core count; the simplex searches are sequential.
//

#include "Fitter.h"
#include "DrumSynth.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <numeric>
#include <string>

namespace transmute
{
    namespace
    {
        using Strings = std::vector<std::string>;
        using Score = std::function<double (const DrumParams&)>;
        using Stop = std::function<bool()>;

        struct Cancelled {};

        //==============================================================================
        //  Nelder-Mead, with Gao and Han's dimension-dependent coefficients.
        struct Minimum { Doubles point; double value; int evaluations; };

        Minimum minimize (const std::function<double (const Doubles&)>& f,
                          const Doubles& start, const Doubles& steps,
                          int maxEvaluations, double tolerance, const Stop& shouldStop)
        {
            const int n = (int) start.size();
            const double dimension = (double) n;
            const double alpha = 1.0;
            const double beta = 1 + 2 / dimension;
            const double gamma = 0.75 - 1 / (2 * dimension);
            const double delta = 1 - 1 / dimension;

            int evaluations = 0;
            const auto value = [&] (const Doubles& x)
            {
                evaluations += 1;
                const double v = f (x);
                return std::isfinite (v) ? v : DBL_MAX;
            };

            std::vector<Doubles> simplex { start };
            for (int i = 0; i < n; ++i)
            {
                auto vertex = start;
                vertex[(size_t) i] += steps[(size_t) i];
                simplex.push_back (vertex);
            }

            Doubles values;
            for (const auto& vertex : simplex)
                values.push_back (value (vertex));

            while (evaluations < maxEvaluations && ! shouldStop())
            {
                std::vector<size_t> order (values.size());
                std::iota (order.begin(), order.end(), 0);
                std::stable_sort (order.begin(), order.end(), [&values] (size_t a, size_t b) { return values[a] < values[b]; });

                std::vector<Doubles> sortedSimplex;
                Doubles sortedValues;
                for (const auto i : order)
                {
                    sortedSimplex.push_back (simplex[i]);
                    sortedValues.push_back (values[i]);
                }
                simplex = std::move (sortedSimplex);
                values = std::move (sortedValues);

                if (values[(size_t) n] - values[0] < tolerance)
                    break;

                Doubles centroid ((size_t) n, 0.0);
                for (int v = 0; v < n; ++v)
                    for (int i = 0; i < n; ++i)
                        centroid[(size_t) i] += simplex[(size_t) v][(size_t) i] / dimension;

                const auto along = [&] (double t)
                {
                    Doubles x ((size_t) n);
                    for (int i = 0; i < n; ++i)
                        x[(size_t) i] = centroid[(size_t) i] + t * (simplex[(size_t) n][(size_t) i] - centroid[(size_t) i]);
                    return x;
                };

                const auto reflected = along (-alpha);
                const double r = value (reflected);

                if (r < values[0])
                {
                    const auto expanded = along (-alpha * beta);
                    const double e = value (expanded);
                    if (e < r) { simplex[(size_t) n] = expanded; values[(size_t) n] = e; }
                    else       { simplex[(size_t) n] = reflected; values[(size_t) n] = r; }
                }
                else if (r < values[(size_t) n - 1])
                {
                    simplex[(size_t) n] = reflected;
                    values[(size_t) n] = r;
                }
                else
                {
                    const bool outside = r < values[(size_t) n];
                    const auto contracted = along (outside ? -alpha * gamma : gamma);
                    const double c = value (contracted);

                    if (c < (outside ? r : values[(size_t) n]))
                    {
                        simplex[(size_t) n] = contracted;
                        values[(size_t) n] = c;
                    }
                    else
                    {
                        // Shrink everything towards the best vertex.
                        for (int j = 1; j <= n; ++j)
                        {
                            Doubles x ((size_t) n);
                            for (int i = 0; i < n; ++i)
                                x[(size_t) i] = simplex[0][(size_t) i] + delta * (simplex[(size_t) j][(size_t) i] - simplex[0][(size_t) i]);
                            simplex[(size_t) j] = x;
                            values[(size_t) j] = value (simplex[(size_t) j]);
                        }
                    }
                }
            }

            size_t best = 0;
            for (size_t i = 1; i < values.size(); ++i)
                if (values[i] < values[best])
                    best = i;

            return { simplex[best], values[best], evaluations };
        }

        //==============================================================================
        const Strings bodyKeys { "delay", "fundamental", "pitchStart", "pitchDecay", "ampAttack", "ampDecay", "ampShape", "gainDB" };
        const Strings moreKeys { "drive", "transient", "clickTone", "clickDecay", "noise", "noiseTone", "noiseDecay" };

        constexpr double maxDrive = 0.5;
        const Doubles driveTrials { 0.0, 0.25, 0.5 };
        const Doubles clickToneTrials { 300.0, 700, 1500, 3000, 6000, 12000 };
        const Doubles clickDecayTrials { 0.0005, 0.0015, 0.004 };
        const Doubles wireToneTrials { 0.5, 0.71, 1, 1.41, 2 };
        const Doubles wireWidthTrials { 1.0, 2, 3, 4.5, 6.5 };
        constexpr int wireLevelBudget = 12;

        constexpr int bodyBudget = 400;
        constexpr int driveBudget = 200;
        constexpr int snareDriveBudget = 300;
        constexpr int fullBudget = 1000;
        constexpr int restartBudget = 600;
        constexpr int attackBudget = 400;
        const Strings attackKeysBase { "attackLevelDB", "gainDB", "delay", "ampDecay", "ampShape", "drive",
                                       "transient", "noise", "noiseTone", "noiseDecay" };

        const Strings hatKeys { "gainDB", "delay", "ampAttack", "transient", "clickTone", "clickDecay",
                                "noiseTone", "noiseWidth", "noiseDecay", "noiseShape" };
        const Strings hatAttackKeys { "attackLevelDB", "gainDB", "delay", "transient", "noiseDecay", "noiseShape" };
        const Strings clapKeys { "gainDB", "delay", "clapSpacing", "clapBurstDecay", "noise", "noiseTone", "noiseWidth",
                                 "noiseDecay", "noiseShape", "clapTailTone", "clapTailWidth",
                                 "transient", "clickTone", "clickDecay" };
        const Strings clapAttackKeys { "attackLevelDB", "gainDB", "clapBurstDecay", "noise", "noiseDecay", "noiseShape" };
        const Strings modalKeys { "gainDB", "delay", "ampAttack", "ampDecay", "ampShape",
                                  "transient", "clickTone", "clickDecay", "noise", "noiseTone", "noiseDecay" };
        const Strings modalAttackKeys { "attackLevelDB", "gainDB", "delay", "ampDecay", "ampShape",
                                        "transient", "noise", "noiseTone", "noiseDecay" };

        Strings concat (Strings a, const Strings& b)
        {
            a.insert (a.end(), b.begin(), b.end());
            return a;
        }

        enum class Stage { body, all, attack, drive };

        Strings keys (Stage stage, DrumModel model)
        {
            const bool snare = model == DrumModel::snare;
            switch (stage)
            {
                case Stage::body:   return concat (bodyKeys, snare ? Strings { "mode2Level", "mode2Decay" } : Strings {});
                case Stage::all:    return concat (concat (keys (Stage::body, model), moreKeys),
                                                   snare ? Strings { "mode2Ratio", "noiseWidth", "noiseShape" } : Strings {});
                case Stage::attack: return concat (attackKeysBase, snare ? Strings { "noiseShape" } : Strings {});
                case Stage::drive:  return concat (bodyKeys, snare ? Strings { "transient", "noise" } : Strings {});
            }
            return {};
        }

        double stepFor (const std::string& id)
        {
            if (id == "gainDB")    return 1;
            if (id == "delay")     return 0.001;
            if (id == "drive")     return 0.5;
            if (id == "transient") return 0.15;
            if (id == "noise")     return 0.04;
            return 0.1;
        }

        /** Nelder-Mead over the parameters named in `keys`, the rest held. */
        DrumParams search (const Strings& names, const DrumParams& start, int budget, double stepScale,
                           const Score& score, const Stop& stop)
        {
            std::vector<const ParamSpec*> specs;
            for (const auto& name : names)
                specs.push_back (&spec (name));

            Doubles encoded, steps;
            for (const auto* s : specs)
            {
                const double v = start.*s->member;
                encoded.push_back (s->logarithmic ? std::log (v) : v);
                steps.push_back (stepScale * (s->logarithmic ? 0.2 : stepFor (s->id)));
            }

            const auto decode = [&] (const Doubles& x)
            {
                auto p = start;
                for (size_t i = 0; i < specs.size(); ++i)
                {
                    const auto* s = specs[i];
                    const double value = s->logarithmic ? std::exp (x[i]) : x[i];
                    const double upper = std::string_view (s->id) == "drive" ? std::min (s->upper, maxDrive) : s->upper;
                    p.*s->member = std::min (std::max (value, s->lower), upper);
                }
                return p;
            };

            const auto result = minimize ([&] (const Doubles& x) { return score (decode (x)); },
                                          encoded, steps, budget, 1e-4, stop);
            return decode (result.point);
        }

        /** The best of `candidates` if it beats `start`, in order, first on a tie. */
        std::pair<DrumParams, double> pick (const DrumParams& start, double startValue,
                                            const std::vector<DrumParams>& candidates, const Doubles& values)
        {
            auto best = start;
            double bestValue = startValue;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                if (values[i] < bestValue)
                {
                    best = candidates[i];
                    bestValue = values[i];
                }
            }
            return { best, bestValue };
        }

        Doubles scoreAll (const std::vector<DrumParams>& cells, const Score& score)
        {
            Doubles values (cells.size());
            parallel ((int) cells.size(), [&] (int i) { values[(size_t) i] = score (cells[(size_t) i]); });
            return values;
        }

        DrumParams bandGrid (const DrumParams& start, const std::string& level, bool tail,
                             const Score& score, const Stop& stop)
        {
            const auto tone = tail ? &DrumParams::clapTailTone : &DrumParams::noiseTone;
            const auto width = tail ? &DrumParams::clapTailWidth : &DrumParams::noiseWidth;
            const double startValue = score (start);

            std::vector<std::pair<double, double>> cells;
            for (const auto factor : wireToneTrials)
                for (const auto w : wireWidthTrials)
                    cells.emplace_back (factor, w);

            std::vector<DrumParams> fitted (cells.size());
            Doubles values (cells.size());

            parallel ((int) cells.size(), [&] (int i)
            {
                auto trial = start;
                trial.*tone = std::min (std::max (start.*tone * cells[(size_t) i].first, 100.0), 16000.0);
                trial.*width = cells[(size_t) i].second;
                trial = search ({ level }, trial, wireLevelBudget, 1, score, stop);
                fitted[(size_t) i] = trial;
                values[(size_t) i] = score (trial);
            });

            return pick (start, startValue, fitted, values).first;
        }

        std::pair<DrumParams, double> clickGrid (const DrumParams& start, double startValue, const Score& score)
        {
            std::vector<DrumParams> cells;
            for (const auto tone : clickToneTrials)
            {
                for (const auto decay : clickDecayTrials)
                {
                    auto trial = start;
                    trial.clickTone = tone;
                    trial.clickDecay = decay;
                    cells.push_back (trial);
                }
            }
            return pick (start, startValue, cells, scoreAll (cells, score));
        }

        DrumParams attackStage (const DrumParams& start, const Strings& attackKeys, const Analysis& analysis,
                                const Comparison& comparison, const Score& score, const Stop& stop)
        {
            const double bestValue = score (start);
            auto voice = start;
            voice.attack.reset();
            const Doubles original (analysis.hit.begin(), analysis.hit.end());
            const double floorRatio = std::pow (10.0, analysis.noiseFloorDB / 20);
            auto withAttack = voice;

            for (int round = 0; round < 2; ++round)
            {
                auto plain = withAttack;
                plain.attack.reset();
                const auto synth = DrumSynth::render (plain, comparison.sampleRate, comparison.frameCount);
                withAttack.attack = AttackNoise::measure (original, Doubles (synth.begin(), synth.end()),
                                                          std::pow (10.0, plain.gainDB / 20), floorRatio, comparison.sampleRate);
                withAttack.attackLevelDB = 0;
                withAttack = search (attackKeys, withAttack, attackBudget, 1, score, stop);
                if (stop())
                    throw Cancelled();
            }

            return score (withAttack) < bestValue - 0.05 ? withAttack : start;
        }

        //==============================================================================
        /** The evaluation count and cancellation, safe from the grids' threads. */
        struct Meter
        {
            double total;
            const Fitter::Progress& progress;
            const std::atomic<bool>* cancel;
            std::atomic<int> used { 0 };
            std::atomic<bool> stopped { false };

            void count()
            {
                const int n = ++used;
                if (n % 20 == 0)
                {
                    if (progress)
                        progress (std::min ((double) n / total, 1.0));
                    if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                        stopped.store (true, std::memory_order_relaxed);
                }
            }

            bool cancelled() const { return stopped.load (std::memory_order_relaxed); }
        };

        double secondsSince (std::chrono::steady_clock::time_point started)
        {
            return std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
        }

        FitReport fitDirect (const Analysis& analysis, const Strings& fitKeys, const Strings& attackKeys,
                             const std::optional<std::string>& bandLevel, const Fitter::Progress& progress,
                             const std::atomic<bool>* cancel)
        {
            const auto started = std::chrono::steady_clock::now();
            const Comparison comparison (analysis.hit, analysis.sampleRate, analysis.pitchTrack);
            const int oneGrid = (int) (wireToneTrials.size() * wireWidthTrials.size()) * (wireLevelBudget + 1) + 1;
            const int bandGridBudget = (bandLevel ? oneGrid : 0) + (analysis.initial.isClap() ? oneGrid : 0);
            Meter meter { (double) (fullBudget + (int) (clickToneTrials.size() * clickDecayTrials.size()) + bandGridBudget
                                    + restartBudget + 2 * (attackBudget + 1) + 2),
                          progress, cancel };

            const Score score = [&] (const DrumParams& p) { meter.count(); return comparison.score (p).total(); };
            const Stop stop = [&] { return meter.cancelled(); };

            const auto& initial = analysis.initial;
            auto best = initial;

            best = search (fitKeys, best, fullBudget, 1, score, stop);
            if (stop()) throw Cancelled();

            if (best.transient > 0.01)
                best = clickGrid (best, score (best), score).first;

            if (bandLevel)
            {
                best = bandGrid (best, *bandLevel, false, score, stop);
                if (stop()) throw Cancelled();
            }

            if (best.isClap() && best.noise > 0.01)
            {
                best = bandGrid (best, "noise", true, score, stop);
                if (stop()) throw Cancelled();
            }

            best = search (fitKeys, best, restartBudget, 0.3, score, stop);
            if (stop()) throw Cancelled();

            if (! attackKeys.empty())
                best = attackStage (best, attackKeys, analysis, comparison, score, stop);

            if (progress)
                progress (1);

            return { best, comparison.score (best), comparison.score (initial), meter.used.load(), secondsSince (started) };
        }

        FitReport fitVoice (const Analysis& analysis, const Fitter::Progress& progress, const std::atomic<bool>* cancel)
        {
            const auto model = analysis.initial.model;

            if (model == DrumModel::hat)
                return fitDirect (analysis, hatKeys, hatAttackKeys, std::string ("gainDB"), progress, cancel);
            if (model == DrumModel::clap)
                return fitDirect (analysis, clapKeys, clapAttackKeys, std::string ("gainDB"), progress, cancel);
            if (model == DrumModel::modal)
                return fitDirect (analysis, modalKeys, modalAttackKeys, std::nullopt, progress, cancel);

            const auto started = std::chrono::steady_clock::now();
            const Comparison comparison (analysis.hit, analysis.sampleRate, analysis.pitchTrack);
            const bool snare = analysis.initial.isSnare();
            const int wireGrid = snare ? (int) (wireToneTrials.size() * wireWidthTrials.size()) * (wireLevelBudget + 1) + 1 : 0;
            const int driveTrialBudget = snare ? snareDriveBudget : driveBudget;

            Meter meter { (double) (bodyBudget + (int) driveTrials.size() * (driveTrialBudget + 2)
                                    + (int) (clickToneTrials.size() * clickDecayTrials.size())
                                    + fullBudget + wireGrid + restartBudget + 2 * (attackBudget + 1)),
                          progress, cancel };

            const Score score = [&] (const DrumParams& p) { meter.count(); return comparison.score (p).total(); };
            const Stop stop = [&] { return meter.cancelled(); };

            const auto& initial = analysis.initial;
            auto best = initial;
            const auto bodyFit = keys (Stage::body, best.model), allFit = keys (Stage::all, best.model);
            const auto attackFit = keys (Stage::attack, best.model), driveFit = keys (Stage::drive, best.model);

            // 1. Body.
            best = search (bodyFit, best, bodyBudget, 1, score, stop);
            if (stop()) throw Cancelled();

            // 1b. A first, quick look at drive.
            double bestValue = score (best);
            {
                const auto plainBody = best;
                std::vector<DrumParams> driven;
                for (const auto drive : driveTrials)
                {
                    if (drive == plainBody.drive)
                        continue;
                    auto trial = plainBody;
                    trial.drive = drive;
                    driven.push_back (trial);
                }
                std::tie (best, bestValue) = pick (best, bestValue, driven, scoreAll (driven, score));
            }

            // 2. Click.
            if (best.transient > 0.01)
                std::tie (best, bestValue) = clickGrid (best, bestValue, score);

            // 3. Everything.
            best = search (allFit, best, fullBudget, 1, score, stop);
            if (stop()) throw Cancelled();

            // 3b. A snare's wires on a grid.
            if (best.isSnare() && best.noise > 0.01)
            {
                best = bandGrid (best, "noise", false, score, stop);
                if (stop()) throw Cancelled();
            }

            // 4. Drive, each trial with its own body.
            bestValue = score (best);
            {
                const auto fitted = best;
                Doubles trialDrives;
                for (const auto drive : driveTrials)
                    if (std::abs (drive - fitted.drive) > 0.2)
                        trialDrives.push_back (drive);

                std::vector<DrumParams> bodies (trialDrives.size());
                Doubles values (trialDrives.size());

                parallel ((int) trialDrives.size(), [&] (int i)
                {
                    auto trial = fitted;
                    trial.drive = trialDrives[(size_t) i];
                    trial = search (driveFit, trial, driveTrialBudget, 1, score, stop);
                    bodies[(size_t) i] = trial;
                    values[(size_t) i] = score (trial);
                });

                std::tie (best, bestValue) = pick (best, bestValue, bodies, values);
                if (stop()) throw Cancelled();
            }

            // 5. Everything again.
            best = search (allFit, best, restartBudget, 0.3, score, stop);
            if (stop()) throw Cancelled();

            // 6. Attack noise.
            best = attackStage (best, attackFit, analysis, comparison, score, stop);

            // The phase, against the fitted sweep.
            const double cutoff = Analyzer::bodyCutoff (best.model, best.fundamental);
            const auto body = Filters::lowPass (Doubles (analysis.hit.begin(), analysis.hit.end()), cutoff, comparison.sampleRate);
            best.startPhase = Analyzer::startPhase (best, body, comparison.sampleRate);

            if (progress)
                progress (1);

            return { best, comparison.score (best), comparison.score (initial), meter.used.load(), secondsSince (started) };
        }
    }

    std::optional<FitReport> Fitter::fit (const Analysis& analysis, const Progress& progress, const std::atomic<bool>* cancel)
    {
        try
        {
            return fitVoice (analysis, progress, cancel);
        }
        catch (const Cancelled&)
        {
            return std::nullopt;
        }
    }
}
