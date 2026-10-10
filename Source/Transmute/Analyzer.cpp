//
//  Analyzer.cpp
//  Kitbox
//
//  Line for line Analyzer.swift. In order: onset and end, where the pitch
//  settles, the sweep from zero crossings, the decay, the start phase, click
//  and noise from the residual - or, for a hat, a modal drum or a clap, their
//  own measurements.
//

#include "Analyzer.h"
#include "Dsp.h"
#include "DrumSynth.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace transmute
{
    namespace
    {
        using Kind = Biquad::Kind;
        using Vec = Doubles;

        constexpr double snareThresholdDB = -17.5;
        constexpr double hatThresholdDB = -12.0;
        constexpr double modalLow = 200.0, modalHigh = 5000.0;

        Vec slice (const Vec& x, int begin, int end)
        {
            begin = std::clamp (begin, 0, (int) x.size());
            end = std::clamp (end, begin, (int) x.size());
            return Vec (x.begin() + begin, x.begin() + end);
        }

        Vec prefix (const Vec& x, int count) { return slice (x, 0, count); }
        Vec dropFirst (const Vec& x, int count) { return slice (x, count, (int) x.size()); }

        double maxValue (const Vec& x, int begin, int end, double fallback = 0)
        {
            if (end <= begin)
                return fallback;
            return *std::max_element (x.begin() + begin, x.begin() + end);
        }

        double maxValue (const Vec& x) { return maxValue (x, 0, (int) x.size()); }

        double maxAbs (const Vec& x, int begin, int end)
        {
            double top = 0;
            bool any = false;
            for (int i = begin; i < end; ++i)
            {
                const auto v = std::abs (x[(size_t) i]);
                if (! any || top < v) { top = v; any = true; }
            }
            return top;
        }

        int firstMaxAbsIndex (const Vec& x, int begin, int end)
        {
            int best = begin;
            for (int i = begin + 1; i < end; ++i)
                if (std::abs (x[(size_t) best]) < std::abs (x[(size_t) i]))
                    best = i;
            return best;
        }

        double sum (const Vec& x)
        {
            double s = 0;
            for (const auto v : x)
                s += v;
            return s;
        }

        double sortedAt (Vec x, size_t index)
        {
            std::sort (x.begin(), x.end());
            return x[index];
        }

        /** Swift's stride(from: 0, to: count, by: step) picked out of x. */
        Vec strided (const Vec& x, int step)
        {
            Vec out;
            for (size_t i = 0; i < x.size(); i += (size_t) step)
                out.push_back (x[i]);
            return out;
        }

        /** The running RMS over `window` samples, centred. */
        Vec movingRms (const Vec& x, int window)
        {
            Vec sums (x.size() + 1, 0.0);
            for (size_t i = 0; i < x.size(); ++i)
                sums[i + 1] = sums[i] + x[i] * x[i];

            Vec out (x.size());
            const int count = (int) x.size();
            for (int i = 0; i < count; ++i)
            {
                const int lo = std::max (i - window / 2, 0), hi = std::min (i + window / 2, count);
                out[(size_t) i] = std::sqrt ((sums[(size_t) hi] - sums[(size_t) lo]) / (double) std::max (hi - lo, 1));
            }
            return out;
        }

        double sumOfSquares (const Vec& x)
        {
            double total = 0;
            vDSP_svesqD (x.data(), 1, &total, (vDSP_Length) x.size());
            return total;
        }

        //==============================================================================
        /** Hann-windowed magnitude spectrum, size / 2 bins (bin 0 left 0). */
        Vec spectrum (const Vec& segment, int size)
        {
            RealFFT fft (size);
            const int half = size / 2;
            const int n = std::min ((int) segment.size(), size);
            Floats input ((size_t) size, 0.0f), real ((size_t) half), imag ((size_t) half);

            for (int i = 0; i < n; ++i)
            {
                const double w = n > 1 ? 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) (n - 1)) : 1;
                input[(size_t) i] = (float) (segment[(size_t) i] * w);
            }

            fft.forward (input.data(), real.data(), imag.data());

            Vec magnitudes ((size_t) half, 0.0);
            for (int k = 1; k < half; ++k)
                magnitudes[(size_t) k] = (double) std::sqrt (real[(size_t) k] * real[(size_t) k] + imag[(size_t) k] * imag[(size_t) k]);
            return magnitudes;
        }

        /** Parabolic interpolation on log magnitudes around bin k, in bins. */
        double interpolatedPeak (const Vec& m, int k)
        {
            if (! (k > 0 && k + 1 < (int) m.size()))
                return (double) k;

            const double a = std::log (std::max (m[(size_t) k - 1], 1e-20));
            const double b = std::log (std::max (m[(size_t) k], 1e-20));
            const double c = std::log (std::max (m[(size_t) k + 1], 1e-20));
            const double denominator = a - 2 * b + c;

            if (! (std::abs (denominator) > 1e-12))
                return (double) k;

            return (double) k + 0.5 * (a - c) / denominator;
        }

        double logCentroid (const Vec& magnitudes, double binHz)
        {
            double power = 0.0, moment = 0.0;
            for (size_t k = 1; k < magnitudes.size(); ++k)
            {
                const double e = magnitudes[k] * magnitudes[k];
                power += e;
                moment += e * std::log ((double) k * binHz);
            }
            return power > 0 ? std::exp (moment / power) : 0;
        }

        struct Line { double slope, intercept; };

        Line line (const Vec& xs, const Vec& ys)
        {
            const double n = (double) xs.size();
            const double mx = sum (xs) / n, my = sum (ys) / n;
            double sxy = 0.0, sxx = 0.0;
            for (size_t j = 0; j < xs.size(); ++j)
            {
                sxy += (xs[j] - mx) * (ys[j] - my);
                sxx += (xs[j] - mx) * (xs[j] - mx);
            }
            const double slope = sxx > 0 ? sxy / sxx : 0;
            return { slope, my - slope * mx };
        }

        double strongestPeak (const Vec& x, double sr)
        {
            const int size = 1 << 15;
            const auto m = spectrum (x, size);
            const double binHz = sr / (double) size;
            const int low = std::max ((int) (30 / binHz), 1);

            if (! ((int) m.size() > low + 2))
                return 0;

            const int k = firstMaxIndex (m, low, (int) m.size() - 1);
            return interpolatedPeak (m, k) * binHz;
        }

        //==============================================================================
        //  1. Bounds
        struct Bounds { int onset, end; double floorRatio; };

        Bounds bounds (const Vec& x, double sr)
        {
            const int count = (int) x.size();
            const int hop = std::max ((int) (0.001 * sr), 1);
            const int hops = std::max (count / hop, 1);
            Vec peaks ((size_t) hops, 0.0), rms ((size_t) hops, 0.0);

            for (int h = 0; h < hops; ++h)
            {
                const int c = std::min (hop, count - h * hop);
                if (c <= 0)
                    continue;
                vDSP_maxmgvD (x.data() + h * hop, 1, &peaks[(size_t) h], (vDSP_Length) c);
                vDSP_rmsqvD (x.data() + h * hop, 1, &rms[(size_t) h], (vDSP_Length) c);
            }

            const double top = maxValue (peaks);
            const int peakHop = (int) (std::find (peaks.begin(), peaks.end(), top) - peaks.begin());

            const double loudest = maxValue (rms);
            int firstLoud = 0;
            for (int h = 0; h < hops; ++h)
                if (rms[(size_t) h] >= 0.1 * loudest) { firstLoud = h; break; }

            const int leadEnd = std::max (firstLoud * hop - (int) (0.005 * sr), 0);
            const int leadStart = std::max (leadEnd - (int) (0.05 * sr), 0);
            double leadSigma = 0.0;

            if (leadEnd - leadStart > hop)
            {
                Vec magnitudes;
                for (int i = leadStart; i < leadEnd; ++i)
                    magnitudes.push_back (std::abs (x[(size_t) i]));
                std::sort (magnitudes.begin(), magnitudes.end());
                leadSigma = 1.4826 * magnitudes[magnitudes.size() / 2];
            }

            const double threshold = std::max (4 * leadSigma, 0.01 * top);

            const int window = std::max ((int) (0.00025 * sr), 1);
            const int from = std::min (firstLoud * hop, count - 1);
            int onset = from;

            while (onset > window && from - onset < (int) (0.005 * sr))
            {
                bool loud = false;
                for (int j = onset - window; j < onset; ++j)
                    if (std::abs (x[(size_t) j]) >= threshold) { loud = true; break; }
                if (! loud)
                    break;
                onset -= 1;
            }

            int back = onset;
            while (back > 0 && onset - back < hop && x[(size_t) back - 1] * x[(size_t) back] > 0)
                back -= 1;
            if (back > 0 && onset - back < hop)
                onset = back;

            const int tailCount = std::min (std::max (hops / 5, 50), hops - peakHop);
            double floor = top * 1e-5;

            if (tailCount >= 4)
            {
                const Vec tail (peaks.begin() + (hops - tailCount), peaks.end());
                const auto meanDB = [] (const Vec& values, int begin, int end)
                {
                    double s = 0;
                    for (int i = begin; i < end; ++i)
                        s += 20 * std::log10 (std::max (values[(size_t) i], 1e-12));
                    return s / (double) (end - begin);
                };
                const double falling = meanDB (tail, 0, tailCount / 2) - meanDB (tail, tailCount / 2, tailCount);
                if (falling < 1)
                    floor = std::max (floor, sortedAt (tail, (size_t) (tailCount / 2)));
            }

            const double level = std::max (2 * floor, top * 1e-3);
            int lastHop = peakHop;
            for (int h = hops - 1; h >= peakHop; --h)
                if (peaks[(size_t) h] > level) { lastHop = h; break; }

            const int end = std::min ((lastHop + 1) * hop + (int) (0.01 * sr), count);
            return { onset, std::max (end, std::min (onset + 1, count)), floor / std::max (top, 1e-12) };
        }

        //==============================================================================
        //  2. Tail
        double tailFrequency (const Vec& y, double sr)
        {
            const int n = (int) y.size();
            const int peakIndex = firstMaxAbsIndex (y, 0, n);
            int start = std::min (peakIndex + (int) (0.02 * sr), n - 1);
            if (n - start < (int) (0.05 * sr))
                start = std::min (peakIndex, n - 1);

            const auto segment = dropFirst (y, start);
            int size = 1 << 16;
            while (size < 4 * (int) segment.size())
                size <<= 1;

            const auto magnitudes = spectrum (segment, size);
            const double binHz = sr / (double) size;
            const int low = std::max ((int) (20 / binHz), 1), high = std::min ((int) (1000 / binHz), (int) magnitudes.size() - 2);

            if (! (high > low))
                return 55;

            int best = low;
            for (int k = low; k <= high; ++k)
                if (magnitudes[(size_t) k] > magnitudes[(size_t) best])
                    best = k;

            return interpolatedPeak (magnitudes, best) * binHz;
        }

        //==============================================================================
        //  3. Sweep
        std::vector<PitchPoint> pitchTrack (const Vec& body, const Vec& envelope, double floorRatio, double sr)
        {
            Vec up, down;
            for (size_t i = 1; i < body.size(); ++i)
            {
                const double a = body[i - 1], b = body[i];
                if (a < 0 && b >= 0) up.push_back (((double) (i - 1) + a / (a - b)) / sr);
                if (a > 0 && b <= 0) down.push_back (((double) (i - 1) + a / (a - b)) / sr);
            }

            const double top = maxValue (envelope);
            const double gate = top * std::max (4 * floorRatio, 0.003);
            std::vector<PitchPoint> points;

            for (const auto* crossings : { &up, &down })
            {
                if (crossings->size() <= 1)
                    continue;

                for (size_t j = 1; j < crossings->size(); ++j)
                {
                    const double period = (*crossings)[j] - (*crossings)[j - 1];
                    if (! (period > 0))
                        continue;

                    const double t = 0.5 * ((*crossings)[j] + (*crossings)[j - 1]);
                    const int index = std::min ((int) (t * sr), (int) envelope.size() - 1);
                    const double hz = 1 / period;

                    if (! (hz > 15 && hz < 5000 && envelope[(size_t) index] > gate))
                        continue;

                    points.push_back ({ t, hz, envelope[(size_t) index] / std::max (top, 1e-12) });
                }
            }

            std::stable_sort (points.begin(), points.end(), [] (const PitchPoint& a, const PitchPoint& b) { return a.time < b.time; });

            if (points.size() <= 4)
                return points;

            std::vector<PitchPoint> kept;
            const int count = (int) points.size();

            for (int i = 0; i < count; ++i)
            {
                const int lo = std::max (0, i - 3), hi = std::min (count - 1, i + 3);
                Vec neighbours;
                for (int j = lo; j <= hi; ++j)
                    neighbours.push_back (points[(size_t) j].hz);
                std::sort (neighbours.begin(), neighbours.end());
                const double median = neighbours[(size_t) ((hi - lo) / 2)];

                if (std::abs (points[(size_t) i].hz / median - 1) < 0.3)
                    kept.push_back (points[(size_t) i]);
            }

            return kept;
        }

        struct Sweep { double start, end, tau; };

        Sweep fitSweep (const std::vector<PitchPoint>& track, double fallbackHz)
        {
            if (track.size() < 4)
                return { fallbackHz * 2.5, fallbackHz, 0.03 };

            const auto solve = [&track] (double tau) -> std::pair<double, Sweep>
            {
                double sw = 0.0, sg = 0.0, sgg = 0.0, sf = 0.0, sgf = 0.0;
                for (const auto& point : track)
                {
                    const double g = std::exp (-point.time / tau), w = point.weight;
                    sw += w; sg += w * g; sgg += w * g * g; sf += w * point.hz; sgf += w * g * point.hz;
                }

                const double det = sw * sgg - sg * sg;
                if (! (std::abs (det) > 1e-12))
                {
                    const double mean = sf / std::max (sw, 1e-12);
                    return { HUGE_VAL, { mean, mean, tau } };
                }

                const double end = (sgg * sf - sg * sgf) / det;
                const double delta = (sw * sgf - sg * sf) / det;
                double error = 0.0;
                for (const auto& point : track)
                {
                    const double r = point.hz - end - delta * std::exp (-point.time / tau);
                    error += point.weight * r * r;
                }
                return { error, { end + delta, end, tau } };
            };

            Vec grid;
            for (int i = 0; i < 80; ++i)
                grid.push_back (0.001 * std::pow (1000.0, (double) i / 79));

            int bestIndex = 0;
            double bestError = HUGE_VAL;
            for (int i = 0; i < 80; ++i)
            {
                const double error = solve (grid[(size_t) i]).first;
                if (error < bestError) { bestError = error; bestIndex = i; }
            }

            double a = std::log (grid[(size_t) std::max (bestIndex - 1, 0)]);
            double b = std::log (grid[(size_t) std::min (bestIndex + 1, (int) grid.size() - 1)]);
            const double ratio = (std::sqrt (5.0) - 1) / 2;

            for (int i = 0; i < 40; ++i)
            {
                const double c = b - ratio * (b - a), d = a + ratio * (b - a);
                if (solve (std::exp (c)).first < solve (std::exp (d)).first) b = d; else a = c;
            }

            auto sweep = solve (std::exp (0.5 * (a + b))).second;
            sweep.end = std::min (std::max (sweep.end, 20.0), 1000.0);
            sweep.start = std::min (std::max (sweep.start, 20.0), 4000.0);
            return sweep;
        }

        //==============================================================================
        //  4. Envelope
        struct Shape { double tau, k, peak; };

        Shape fitDecay (const Vec& envelope, double startSeconds, double floorRatio, double sr)
        {
            const double top = maxValue (envelope);
            const int hop = std::max ((int) (0.001 * sr), 1);
            const double lowest = top * std::max (3 * floorRatio, 1e-3);
            const int stop = (int) envelope.size() - (int) (0.02 * sr);
            Vec ts, ls;

            int i = std::min ((int) (startSeconds * sr), std::max ((int) envelope.size() - 1, 0));
            while (i < stop)
            {
                const double e = envelope[(size_t) i];
                if (e < lowest)
                    break;
                ts.push_back ((double) i / sr);
                ls.push_back (std::log (e));
                i += hop;
            }

            if (ts.size() < 5)
                return { std::max ((double) envelope.size() / sr / 3, 0.005), 1, top };

            const double last = ts.back();

            const auto solve = [&] (double k) -> std::pair<double, Shape>
            {
                double n = 0.0, su = 0.0, suu = 0.0, sl = 0.0, sul = 0.0;
                for (size_t j = 0; j < ts.size(); ++j)
                {
                    const double u = std::pow (ts[j], k);
                    n += 1; su += u; suu += u * u; sl += ls[j]; sul += u * ls[j];
                }

                const double det = n * suu - su * su;
                if (! (std::abs (det) > 1e-30))
                    return { HUGE_VAL, { 1, k, top } };

                const double a = (suu * sl - su * sul) / det;
                const double c = -(n * sul - su * sl) / det;
                if (! (c > 0))
                    return { HUGE_VAL, { 1, k, top } };

                double error = 0.0;
                for (size_t j = 0; j < ts.size(); ++j)
                {
                    const double r = ls[j] - a + c * std::pow (ts[j], k);
                    error += r * r;
                }
                return { error, { std::pow (c, -1 / k), k, std::exp (a) } };
            };

            auto best = solve (1);
            for (int step = 0; step <= 60; ++step)
            {
                const double k = 0.4 * std::pow (10.0, (double) step / 60);
                const auto candidate = solve (k);
                if (candidate.first < best.first)
                    best = candidate;
            }

            if ((ls[0] - ls[ls.size() - 1]) * 20 / std::log (10.0) < 6)
            {
                auto plain = solve (1).second;
                plain.tau = std::max (plain.tau, last);
                return plain;
            }

            return best.second;
        }

        //==============================================================================
        //  6. Residual
        void measureClick (const Vec& residual, double gain, double floor, double sr, DrumParams& p)
        {
            const int n = std::min ((int) residual.size(), (int) (0.005 * sr));
            if (! (n > 8 && gain > 0))
                return;

            const auto head = prefix (residual, n);
            const double top = maxAbs (head, 0, n);
            p.transient = std::min (std::max (top - floor, 0.0) / gain, 2.0);
            if (! (top > floor))
                return;

            const double centre = logCentroid (spectrum (head, 1024), sr / 1024);
            if (centre > 0)
                p.clickTone = centre * centre / (0.5 * sr);

            const int hop = std::max ((int) (0.00025 * sr), 1);
            const int peakIndex = firstMaxAbsIndex (head, 0, n);
            const auto span = slice (residual, peakIndex, std::min ((int) residual.size(), peakIndex + (int) (0.02 * sr)));

            int i = 0;
            while (i + hop <= (int) span.size())
            {
                const double local = maxAbs (span, i, i + hop);
                if (local < floor + (top - floor) / M_E)
                    break;
                i += hop;
            }

            p.clickDecay = std::max ((double) i / sr, 0.0002);
        }

        void measureNoise (const Vec& residual, double gain, double aboveHz, double sr, DrumParams& p)
        {
            const int skip = (int) (0.005 * sr);
            if (! ((int) residual.size() > skip + (int) (0.02 * sr) && gain > 0))
                return;

            const auto high = Filters::highPass (residual, aboveHz, sr);
            const int window = std::max ((int) (0.005 * sr), 1);
            Vec times, powers;

            for (int i = skip; i + window <= (int) high.size(); i += window)
            {
                double ms = 0.0;
                vDSP_measqvD (high.data() + i, 1, &ms, (vDSP_Length) window);
                times.push_back (((double) i + 0.5 * (double) window) / sr);
                powers.push_back (ms);
            }

            if (powers.size() < 4)
                return;

            const double floor = sortedAt (powers, powers.size() / 10);
            const int first = firstMaxIndex (powers, 0, (int) powers.size());
            const double lowest = std::max (2 * floor, powers[(size_t) first] * 1e-4);
            Vec xs, ys;

            for (size_t j = (size_t) first; j < powers.size(); ++j)
            {
                if (! (powers[j] > lowest))
                    break;
                xs.push_back (times[j]);
                ys.push_back (0.5 * std::log (powers[j] - floor));
            }

            if (xs.size() < 3)
                return;

            const double n = (double) xs.size();
            const double mx = sum (xs) / n, my = sum (ys) / n;
            double sxy = 0.0, sxx = 0.0;
            for (size_t j = 0; j < xs.size(); ++j)
            {
                sxy += (xs[j] - mx) * (ys[j] - my);
                sxx += (xs[j] - mx) * (xs[j] - mx);
            }
            const double slope = sxx > 0 ? sxy / sxx : -20;
            if (! (slope < 0))
                return;

            p.noiseDecay = -1 / slope;
            p.noise = std::min (std::exp (my - slope * mx) / gain, 1.0);

            const auto segment = slice (high, skip, std::min ((int) high.size(), skip + (int) (0.05 * sr)));
            const double centre = logCentroid (spectrum (segment, 4096), sr / 4096);
            if (centre > 0)
                p.noiseTone = centre;
        }

        void measureMode2 (const Vec& residual, double gain, double sr, DrumParams& p)
        {
            const int n = std::min ((int) residual.size(), (int) (0.04 * sr));
            if (! (n > (int) (0.01 * sr) && gain > 0))
                return;

            const int size = 16384;
            const auto magnitudes = spectrum (prefix (residual, n), size);
            const double binHz = sr / (double) size;

            // stride(from: 0.0, to: n / sr, by: 0.001): start + i * step.
            Vec pitch;
            const double until = (double) n / sr;
            for (int i = 0;; ++i)
            {
                const double t = 0.0 + (double) i * 0.001;
                if (! (t < until))
                    break;
                pitch.push_back (p.pitch (p.delay + t));
            }
            std::sort (pitch.begin(), pitch.end());
            const double body = pitch[pitch.size() / 2];

            const int low = std::max ((int) (1.2 * body / binHz), 1);
            const int high = std::min ((int) (4 * body / binHz), (int) magnitudes.size() - 2);
            if (! (high > low + 2))
                return;

            int best = -1;
            for (int k = low + 1; k < high; ++k)
            {
                if (magnitudes[(size_t) k] > magnitudes[(size_t) k - 1] && magnitudes[(size_t) k] >= magnitudes[(size_t) k + 1])
                    if (best < 0 || magnitudes[(size_t) k] > magnitudes[(size_t) best])
                        best = k;
            }
            if (best < 0)
                return;

            const double hz = interpolatedPeak (magnitudes, best) * binHz;
            p.mode2Ratio = hz / body;

            const double q = 4.0;
            const auto band = Filters::zeroPhase (residual, { Biquad (Kind::bandPass, hz, q, sr), Biquad (Kind::bandPass, hz, q, sr) });
            const auto envelope = Filters::envelope (band);
            const int search = std::min ((int) envelope.size(), (int) (0.03 * sr));
            const int peakIndex = search > 0 ? firstMaxIndex (envelope, 0, search) : 0;
            const double top = envelope[(size_t) peakIndex];
            if (! (top > 0))
                return;

            const int hop = std::max ((int) (0.001 * sr), 1);
            Vec xs, ys;
            int i = peakIndex;
            while (i < std::min ((int) envelope.size(), peakIndex + (int) (0.15 * sr)) && envelope[(size_t) i] > top * 0.03)
            {
                xs.push_back ((double) i / sr);
                ys.push_back (std::log (envelope[(size_t) i]));
                i += hop;
            }

            if (xs.size() < 3)
                return;

            const auto fit = line (xs, ys);
            if (! (fit.slope < 0))
                return;

            p.mode2Decay = -1 / fit.slope;
            p.mode2Level = std::min (std::exp (fit.intercept) / gain, 2.0);
        }

        struct BandShape { double tone, width; };

        std::optional<BandShape> measureBand (const Vec& segment, double aboveHz, double sr)
        {
            const auto magnitudes = spectrum (segment, 4096);
            const double binHz = sr / 4096;
            Vec power (magnitudes.size());
            for (size_t k = 0; k < magnitudes.size(); ++k)
                power[k] = magnitudes[k] * magnitudes[k];

            const double total = sum (power);
            if (! (total > 0))
                return std::nullopt;

            double running = 0.0, f10 = 0.0, f90 = 0.0;
            for (size_t k = 0; k < power.size(); ++k)
            {
                running += power[k];
                if (f10 == 0 && running >= 0.1 * total) f10 = (double) k * binHz;
                if (running >= 0.9 * total) { f90 = (double) k * binHz; break; }
            }

            const double a = std::max (std::max ((9 * f10 - f90) / 8, 0.7 * aboveHz), 20.0);
            const double b = std::min (std::max ((9 * f90 - f10) / 8, 1.5 * a), 0.45 * sr);
            return BandShape { std::sqrt (a * b), std::log2 (b / a) };
        }

        void measureWires (const Vec& residual, double gain, double aboveHz, double sr, DrumParams& p)
        {
            const int window = std::max ((int) (0.005 * sr), 1);
            if (! ((int) residual.size() > 4 * window && gain > 0))
                return;

            const auto high = Filters::highPass (residual, aboveHz, sr);
            const auto envelope = movingRms (high, window);
            const double top = maxValue (envelope);
            if (! (top > 0))
                return;

            const auto floor = strided (envelope, window);
            const double floorRatio = sortedAt (floor, floor.size() / 10) / top;
            const auto shape = fitDecay (envelope, 0.005, floorRatio, sr);
            p.noise = std::min (shape.peak / gain, 1.0);
            p.noiseDecay = shape.tau;
            p.noiseShape = shape.k;

            const int skip = std::min ((int) (0.005 * sr), (int) high.size() - 1);
            const auto segment = slice (high, skip, std::min ((int) high.size(), skip + (int) (0.05 * sr)));
            if (const auto band = measureBand (segment, aboveHz, sr))
            {
                p.noiseTone = band->tone;
                p.noiseWidth = band->width;
            }
        }

        //==============================================================================
        //  Clap
        struct Burst { double time, rms; };

        std::vector<Burst> bursts (const Vec& y, double sr)
        {
            const auto x = Filters::highPass (prefix (y, (int) (0.07 * sr)), 1000, sr);
            const int w = std::max ((int) (0.001 * sr), 1), hop = std::max ((int) (0.00025 * sr), 1);
            const int n = std::min ((int) x.size(), (int) (0.06 * sr));
            if (! (n > w))
                return {};

            Vec levels;
            for (int i = 0; i < n - w; i += hop)
            {
                double ms = 0.0;
                vDSP_measqvD (x.data() + i, 1, &ms, (vDSP_Length) w);
                levels.push_back (10 * std::log10 (ms + 1e-20));
            }

            const double top = maxValue (levels);
            std::vector<Burst> out;
            bool armed = true;
            double candidate = -HUGE_VAL, trough = HUGE_VAL;
            int index = 0;

            for (int i = 0; i < (int) levels.size(); ++i)
            {
                const double v = levels[(size_t) i];
                if (armed)
                {
                    if (v > candidate) { candidate = v; index = i; }
                    if (candidate - v >= 8 || i == (int) levels.size() - 1)
                    {
                        if (candidate > top - 12)
                            out.push_back ({ (double) (index * hop) / sr, std::pow (10.0, candidate / 20) });
                        armed = false;
                        trough = v;
                    }
                }
                else
                {
                    trough = std::min (trough, v);
                    if (v - trough >= 8) { armed = true; candidate = v; index = i; }
                }
            }

            return out;
        }

        bool isClap (const Vec& y, double sr)
        {
            Vec times;
            for (const auto& burst : bursts (y, sr))
                times.push_back (burst.time);

            if (times.size() < 4)
                return false;

            for (size_t start = 0; start + 4 <= times.size(); ++start)
            {
                Vec gaps;
                for (size_t j = start; j < start + 3; ++j)
                    gaps.push_back (times[j + 1] - times[j]);

                const double median = sortedAt (gaps, 1);
                if (std::all_of (gaps.begin(), gaps.end(), [median] (double gap)
                                 { return gap >= 0.006 && gap <= 0.016 && std::abs (gap / median - 1) <= 0.2; }))
                    return true;
            }

            return false;
        }

        void analyzeClap (const Vec& y, double sr, DrumParams& p)
        {
            auto found = bursts (y, sr);
            if (found.empty())
            {
                double s = 0;
                for (const auto v : y)
                    s += v * v;
                found.push_back ({ 0, std::sqrt (s / (double) std::max ((int) y.size(), 1)) });
            }

            Vec times;
            for (const auto& burst : found)
                times.push_back (burst.time);

            p.clapBursts = (double) found.size();
            if (times.size() > 1)
            {
                Vec gaps;
                for (size_t j = 1; j < times.size(); ++j)
                    gaps.push_back (times[j] - times[j - 1]);
                p.clapSpacing = sortedAt (gaps, gaps.size() / 2);
            }

            p.delay = std::max (times[0] - 0.0005, 0.0);
            double level = 0;
            for (const auto& burst : found)
                level += burst.rms;
            level /= (double) found.size();
            p.gainDB = 20 * std::log10 (std::max (level, 1e-6));
            p.transient = 0;

            // Burst decays, on the same 1 ms RMS (every 0.25 ms), full band.
            const int w = std::max ((int) (0.001 * sr), 1), hop = std::max ((int) (0.00025 * sr), 1);
            const auto rmsAt = [&] (int i)
            {
                const int lo = std::max (i, 0), hi = std::min (i + w, (int) y.size());
                if (! (hi > lo))
                    return 1e-10;
                double ms = 0.0;
                vDSP_measqvD (y.data() + lo, 1, &ms, (vDSP_Length) (hi - lo));
                return std::max (std::sqrt (ms), 1e-10);
            };

            Vec decays;
            for (int j = 0; j < std::max ((int) times.size() - 1, 0); ++j)
            {
                const int a = (int) (times[(size_t) j] * sr), b = (int) (times[(size_t) j + 1] * sr);
                Vec xs, ys;
                for (int i = a; i < b; i += hop)
                {
                    xs.push_back ((double) i / sr);
                    ys.push_back (std::log (rmsAt (i)));
                }
                if (ys.empty())
                    continue;

                int low = 0;
                for (int i = 1; i < (int) ys.size(); ++i)
                    if (ys[(size_t) i] < ys[(size_t) low])
                        low = i;
                if (low < 3)
                    continue;

                const auto fit = line (prefix (xs, low + 1), prefix (ys, low + 1));
                if (fit.slope < 0)
                    decays.push_back (-1 / fit.slope);
            }

            if (! decays.empty())
                p.clapBurstDecay = sortedAt (decays, decays.size() / 2);

            // The tail, from the last burst on.
            const int last = (int) (times.back() * sr);
            const int window = std::max ((int) (0.005 * sr), 1);
            const auto rest = dropFirst (y, last);
            const auto envelope = movingRms (rest, window);

            if (! envelope.empty())
            {
                const double top = maxValue (envelope);
                if (top > 0)
                {
                    const auto floor = strided (envelope, window);
                    const auto shape = fitDecay (envelope, std::max (3 * p.clapBurstDecay, 0.005),
                                                 sortedAt (floor, floor.size() / 10) / top, sr);
                    p.noise = std::min (shape.peak / std::max (level, 1e-9), 1.5);
                    p.noiseDecay = shape.tau;
                    p.noiseShape = shape.k;
                }
            }

            if (const auto band = measureBand (prefix (y, std::min ((int) y.size(), last + (int) (0.01 * sr))), 100, sr))
            {
                p.noiseTone = band->tone;
                p.noiseWidth = band->width;
            }

            const int tailFrom = std::min (last + (int) (0.01 * sr), (int) y.size() - 1);
            if (const auto band = measureBand (slice (y, tailFrom, std::min ((int) y.size(), tailFrom + (int) (0.15 * sr))), 100, sr))
            {
                p.clapTailTone = band->tone;
                p.clapTailWidth = band->width;
            }
        }

        //==============================================================================
        //  Modal
        void analyzeModal (const Vec& y, double sr, DrumParams& p)
        {
            p.delay = 0;
            p.ampAttack = 0.0005;
            p.ampShape = 1;

            const auto head = prefix (y, std::min ((int) y.size(), (int) (0.06 * sr)));
            const int size = 1 << 15;
            const auto m = spectrum (head, size);
            const double binHz = sr / (double) size;
            const int span = std::max ((int) (40 / binHz), 1);
            const double top = maxValue (m);
            if (! (top > 0))
                return;

            std::vector<int> peaks;
            const int from = std::max (span, (int) (60 / binHz));
            const int to = std::min ((int) m.size() - span, (int) (0.45 * sr / binHz));

            for (int k = from; k < to; ++k)
                if (m[(size_t) k] > top * 0.0316 && maxValue (m, k - span, k + span + 1) == m[(size_t) k])
                    peaks.push_back (k);

            std::stable_sort (peaks.begin(), peaks.end(), [&m] (int a, int b) { return m[(size_t) a] > m[(size_t) b]; });
            if (peaks.empty())
                return;

            const double tone = interpolatedPeak (m, peaks.front()) * binHz;
            Vec chosen;
            for (const int k : peaks)
            {
                const double hz = interpolatedPeak (m, k) * binHz;
                if (hz / tone >= 0.25 && hz / tone <= 8)
                    chosen.push_back (hz);
                if (chosen.size() == 6)
                    break;
            }

            struct Measured { double level, decay; };

            const auto measure = [&] (double hz) -> Measured
            {
                const double q = std::max (hz / 60, 4.0);
                const auto band = Filters::zeroPhase (y, { Biquad (Kind::bandPass, hz, q, sr), Biquad (Kind::bandPass, hz, q, sr) });
                const auto envelope = Filters::envelope (band);
                const int search = std::min ((int) envelope.size(), (int) (0.03 * sr));
                const int peakIndex = search > 0 ? firstMaxIndex (envelope, 0, search) : 0;
                const double peak = envelope[(size_t) peakIndex];
                if (! (peak > 0))
                    return { 0, 0.02 };

                const int hop = std::max ((int) (0.001 * sr), 1);
                Vec xs, ys;
                int i = peakIndex;
                while (i < std::min ((int) envelope.size() - (int) (0.01 * sr), peakIndex + (int) (0.4 * sr))
                       && envelope[(size_t) i] > peak * 0.0316)
                {
                    xs.push_back ((double) i / sr);
                    ys.push_back (std::log (envelope[(size_t) i]));
                    i += hop;
                }

                if (xs.size() < 3)
                    return { peak, 0.005 };

                const auto fit = line (xs, ys);
                return fit.slope < 0 ? Measured { std::exp (fit.intercept), -1 / fit.slope } : Measured { peak, 1 };
            };

            const auto one = measure (tone);
            if (! (one.level > 0))
                return;

            p.modalTone = tone;
            p.gainDB = 20 * std::log10 (one.level);
            p.ampDecay = one.decay;

            for (int k = 2; k <= 6; ++k)
                p.*DrumParams::modalMembers (k).level = 0;

            for (size_t index = 1; index < chosen.size(); ++index)
            {
                const auto members = DrumParams::modalMembers ((int) index + 1);
                const double hz = chosen[index];
                const auto mode = measure (hz);
                p.*members.ratio = hz / tone;
                p.*members.level = std::min (mode.level / one.level, 2.0);
                p.*members.decay = mode.decay;
            }

            p = p.clamped();

            auto modes = p;
            modes.transient = 0;
            modes.noise = 0;
            const double gain = std::pow (10.0, p.gainDB / 20);
            const auto voice = DrumSynth::body (modes, sr, (int) y.size());
            Vec residual (y.size());
            for (size_t i = 0; i < y.size(); ++i)
                residual[i] = y[i] - gain * voice[i];

            p.transient = 0;
            p.noise = 0;
            measureClick (residual, gain, 0, sr, p);
            measureNoise (residual, gain, 150, sr, p);
        }

        //==============================================================================
        //  Hi-hat
        struct FineStructure { Vec fine, level; };

        FineStructure fineStructure (const Vec& x, int size, double sr)
        {
            const auto magnitudes = spectrum (x, size);
            Vec levels (magnitudes.size());
            for (size_t i = 0; i < magnitudes.size(); ++i)
                levels[i] = 20 * std::log10 (magnitudes[i] + 1e-12);

            const int span = std::max ((int) (300 / (sr / (double) size)), 1);
            Vec sums (levels.size() + 1, 0.0);
            for (size_t i = 0; i < levels.size(); ++i)
                sums[i + 1] = sums[i] + levels[i];

            FineStructure out { Vec (levels.size()), Vec (levels.size()) };
            const int count = (int) levels.size();
            for (int i = 0; i < count; ++i)
            {
                const int lo = std::max (i - span, 0), hi = std::min (i + span + 1, count);
                out.level[(size_t) i] = (sums[(size_t) hi] - sums[(size_t) lo]) / (double) (hi - lo);
                out.fine[(size_t) i] = levels[(size_t) i] - out.level[(size_t) i];
            }
            return out;
        }

        struct Metal { double tone, metal; };

        Metal measureMetal (const Vec& y, const DrumParams& p, double sr)
        {
            const int n = std::min ((int) y.size(), 4096);
            if (n < 1024)
                return { p.metalTone, 0.5 };

            const int size = 8192;
            const double binHz = sr / (double) size;
            const auto target = fineStructure (prefix (y, n), size, sr);
            const double loudest = maxValue (target.level);

            std::vector<int> bins;
            for (int i = 0; i < (int) target.level.size(); ++i)
                if (target.level[(size_t) i] > loudest - 20 && (double) i * binHz > 500 && (double) i * binHz < 0.45 * sr)
                    bins.push_back (i);

            if (bins.size() <= 16)
                return { p.metalTone, 0.5 };

            const auto envelope = DrumSynth::hatEnvelope (p, sr, n);
            const auto shaped = [&] (const Vec& source)
            {
                const auto band = DrumSynth::bandPass (source, p, sr);
                Vec out ((size_t) n);
                for (int i = 0; i < n; ++i)
                    out[(size_t) i] = band[(size_t) i] * envelope[(size_t) i];
                return out;
            };

            const auto blurred = [] (const Vec& x, int radius)
            {
                if (radius <= 0)
                    return x;
                Vec sums (x.size() + 1, 0.0);
                for (size_t i = 0; i < x.size(); ++i)
                    sums[i + 1] = sums[i] + x[i];
                Vec out (x.size());
                const int count = (int) x.size();
                for (int i = 0; i < count; ++i)
                {
                    const int lo = std::max (i - radius, 0), hi = std::min (i + radius + 1, count);
                    out[(size_t) i] = (sums[(size_t) hi] - sums[(size_t) lo]) / (double) (hi - lo);
                }
                return out;
            };

            const auto correlation = [&bins] (const Vec& a, const Vec& b)
            {
                Vec xs, ys;
                for (const int bin : bins)
                {
                    xs.push_back (a[(size_t) bin]);
                    ys.push_back (b[(size_t) bin]);
                }
                const double mx = sum (xs) / (double) xs.size(), my = sum (ys) / (double) ys.size();
                double sxy = 0.0, sxx = 0.0, syy = 0.0;
                for (size_t j = 0; j < xs.size(); ++j)
                {
                    sxy += (xs[j] - mx) * (ys[j] - my);
                    sxx += (xs[j] - mx) * (xs[j] - mx);
                    syy += (ys[j] - my) * (ys[j] - my);
                }
                return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
            };

            const auto metalFine = [&] (double tone)
            {
                return fineStructure (shaped (DrumSynth::metalSource (tone, sr, n)), size, sr).fine;
            };

            const int coarseRadius = (int) (50 / binHz);
            const auto coarseTarget = blurred (target.fine, coarseRadius);
            double bestTone = p.metalTone, bestValue = -HUGE_VAL;

            for (int step = 0; step < 200; ++step)
            {
                const double tone = 100 * std::pow (10.0, (double) step / 199);
                const double value = correlation (coarseTarget, blurred (metalFine (tone), coarseRadius));
                if (value > bestValue) { bestValue = value; bestTone = tone; }
            }

            const auto fineTarget = blurred (target.fine, 1);
            const double centre = bestTone;
            bestValue = -HUGE_VAL;

            for (int step = 0; step <= 60; ++step)
            {
                const double tone = centre * std::pow (1.012, (double) (step - 30) / 30);
                const double value = correlation (fineTarget, blurred (metalFine (tone), 1));
                if (value > bestValue) { bestValue = value; bestTone = tone; }
            }

            const auto spread = [&bins] (const Vec& fine)
            {
                Vec xs;
                for (const int bin : bins)
                    xs.push_back (fine[(size_t) bin]);
                const double mean = sum (xs) / (double) xs.size();
                double s = 0;
                for (const auto v : xs)
                    s += (v - mean) * (v - mean);
                return std::sqrt (s / (double) xs.size());
            };

            const double wanted = spread (target.fine);
            Xorshift random (0x5E110004ULL);
            Vec white ((size_t) n);
            for (auto& v : white)
                v = random.next();

            const auto noise = shaped (white);
            const auto metal = shaped (DrumSynth::metalSource (bestTone, sr, n));
            double bestShare = 0.5, bestGap = HUGE_VAL;

            for (int step = 0; step <= 10; ++step)
            {
                const double share = (double) step / 10;
                const double a = std::sqrt (1 - share), b = std::sqrt (share);
                Vec mix ((size_t) n);
                for (int i = 0; i < n; ++i)
                    mix[(size_t) i] = a * noise[(size_t) i] + b * metal[(size_t) i];
                const double gap = std::abs (spread (fineStructure (mix, size, sr).fine) - wanted);
                if (gap < bestGap) { bestGap = gap; bestShare = share; }
            }

            return { bestTone, bestShare };
        }

        void analyzeHat (const Vec& y, double sr, DrumParams& p)
        {
            p.delay = 0;
            const int window = std::max ((int) (0.002 * sr), 1);
            const auto envelope = movingRms (y, window);
            const int peakIndex = firstMaxIndex (envelope, 0, (int) envelope.size());
            const int rise = peakIndex > (int) (0.003 * sr) ? peakIndex : 0;
            p.ampAttack = rise > 0 ? (double) rise / sr : 0.0005;
            const double top = envelope[(size_t) peakIndex];
            if (! (top > 0))
                return;

            const auto floor = strided (envelope, window);
            const auto shape = fitDecay (dropFirst (envelope, rise), 0.001, sortedAt (floor, floor.size() / 10) / top, sr);
            p.noiseDecay = shape.tau;
            p.noiseShape = shape.k;
            p.gainDB = 20 * std::log10 (std::max (shape.peak, 1e-6));

            if (const auto band = measureBand (prefix (y, (int) (0.05 * sr)), 200, sr))
            {
                p.noiseTone = band->tone;
                p.noiseWidth = band->width;
            }

            const auto share = measureMetal (y, p, sr);
            p.metalTone = share.tone;
            p.noise = std::sqrt (1 - share.metal);
            p.metal = std::sqrt (share.metal);
            const double gain = std::pow (10.0, p.gainDB / 20);
            measureClick (y, gain, 3 * gain, sr, p);
        }
    }

    //==============================================================================
    DrumModel Analyzer::suggestModel (const Vec& y, double sr)
    {
        const auto head = prefix (y, (int) (0.2 * sr));
        const double total = sumOfSquares (head);
        if (! (total > 0))
            return DrumModel::kick;

        const double low = sumOfSquares (Filters::lowPass (head, 1000, sr));

        if (isClap (y, sr))
            return DrumModel::clap;

        const double peak = strongestPeak (prefix (y, (int) (0.1 * sr)), sr);

        if (10 * std::log10 (std::max (low / total, 1e-20)) < hatThresholdDB && peak >= modalHigh)
            return DrumModel::hat;

        if (peak >= modalLow && peak < modalHigh)
            return DrumModel::modal;

        const double high = sumOfSquares (Filters::highPass (head, 2000, sr));
        return 10 * std::log10 (std::max (high / total, 1e-20)) > snareThresholdDB ? DrumModel::snare : DrumModel::kick;
    }

    double Analyzer::bodyCutoff (DrumModel model, double hz)
    {
        if (model == DrumModel::snare)
            return std::min (std::max (1.25 * hz, 60.0), 2500.0);
        return std::min (std::max (6 * hz, 250.0), 2500.0);
    }

    double Analyzer::startPhase (const DrumParams& p, const Vec& body, double sr)
    {
        auto q = p;
        q.drive = 0;
        q.startPhase = 0;
        const int n = std::min ((int) body.size(), std::max ((int) (3 / p.fundamental * sr), (int) (0.03 * sr)));
        const auto s = DrumSynth::body (q, sr, n);
        q.startPhase = 90;
        const auto c = DrumSynth::body (q, sr, n);

        double ss = 0.0, cc = 0.0, sc = 0.0, sb = 0.0, cb = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto si = s[(size_t) i], ci = c[(size_t) i], bi = body[(size_t) i];
            ss += si * si; cc += ci * ci; sc += si * ci;
            sb += si * bi; cb += ci * bi;
        }

        const double det = ss * cc - sc * sc;
        if (! (std::abs (det) > 1e-12))
            return 0;

        const double alpha = (cc * sb - sc * cb) / det;
        const double beta = (ss * cb - sc * sb) / det;
        return std::atan2 (beta, alpha) * 180 / M_PI;
    }

    std::optional<Analysis> Analyzer::analyze (const Floats& audio, double sr,
                                               std::optional<DrumModel> model, std::string& error)
    {
        float audioPeak = 0;
        vDSP_maxmgv (audio.data(), 1, &audioPeak, (vDSP_Length) audio.size());
        if (! (audioPeak > 1e-5f))
        {
            error = "The sample is silent.";
            return std::nullopt;
        }

        // Rumble and DC off first.
        const auto full = Filters::highPass (Vec (audio.begin(), audio.end()), 15, sr);

        // 1. Onset and end.
        const auto [onset, end, floorRatio] = bounds (full, sr);
        const auto y = slice (full, onset, end);

        if ((int) y.size() < (int) (0.02 * sr))
        {
            error = "The hit is shorter than 20 ms.";
            return std::nullopt;
        }

        Analysis analysis;
        analysis.sampleRate = sr;
        analysis.suggested = suggestModel (y, sr);
        analysis.onsetSeconds = (double) onset / sr;
        analysis.noiseFloorDB = 20 * std::log10 (std::max (floorRatio, 1e-6));
        analysis.hit.assign (audio.begin() + onset, audio.begin() + end);

        DrumParams p;
        p.model = model.value_or (analysis.suggested);
        p.length = (double) y.size() / sr;

        if (p.isHat() || p.isModal() || p.isClap())
        {
            if (p.isHat())        analyzeHat (y, sr, p);
            else if (p.isModal()) analyzeModal (y, sr, p);
            else                  analyzeClap (y, sr, p);

            analysis.initial = p.clamped();
            return analysis;
        }

        p.transient = 0;
        p.noise = 0;

        // 2. Where the pitch settles.
        const double tailHz = tailFrequency (y, sr);

        // 3. The sweep, on the low-passed body.
        const auto body = Filters::lowPass (y, bodyCutoff (p.model, tailHz), sr);
        const auto envelope = Filters::envelope (body);
        const auto track = pitchTrack (body, envelope, floorRatio, sr);
        const auto sweep = fitSweep (track, tailHz);

        // 3b. Where the body really starts: its envelope at half its peak.
        const double envelopeTop = maxValue (envelope);
        int start = 0;
        for (int i = 0; i < (int) envelope.size(); ++i)
            if (envelope[(size_t) i] >= 0.5 * envelopeTop) { start = i; break; }

        p.delay = std::min ((double) start / sr, 0.03);
        const int d = (int) std::round (p.delay * sr);
        p.fundamental = sweep.end;
        p.pitchStart = sweep.end + (sweep.start - sweep.end) * std::exp (-p.delay / sweep.tau);
        p.pitchDecay = sweep.tau;

        // 4. Decay, shape, level.
        const auto shape = fitDecay (dropFirst (envelope, d), std::max (1.5 / p.fundamental, 0.005), floorRatio, sr);
        p.ampAttack = 0.0005;
        p.ampDecay = shape.tau;
        p.ampShape = shape.k;
        p.gainDB = 20 * std::log10 (std::max (shape.peak, 1e-6));
        p = p.clamped();

        // 5. Phase.
        p.startPhase = startPhase (p, body, sr);

        // 6. Click and noise, from the residual.
        auto modelled = p;
        modelled.transient = 0;
        modelled.noise = 0;
        modelled.mode2Level = 0;
        const double gain = std::pow (10.0, p.gainDB / 20);
        const auto voice = DrumSynth::body (modelled, sr, (int) y.size());
        Vec whole (y.size());
        for (size_t i = 0; i < y.size(); ++i)
            whole[i] = y[i] - gain * voice[i];
        const auto residual = dropFirst (whole, d);

        if (p.isSnare())
        {
            measureMode2 (residual, gain, sr, p);
            measureWires (residual, gain, std::max (4.5 * p.fundamental, 600.0), sr, p);
            measureClick (residual, gain, 3 * p.noise * gain, sr, p);
        }
        else
        {
            measureClick (residual, gain, 0, sr, p);
            measureNoise (residual, gain, std::max (3 * p.fundamental, 150.0), sr, p);
        }

        analysis.pitchTrack = track;
        analysis.initial = p.clamped();
        return analysis;
    }
}
