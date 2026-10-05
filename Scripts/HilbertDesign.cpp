//
//  HilbertDesign.cpp
//  Kitbox (copied from Rackbox - fix bugs in both)
//
//  Where the coefficients of Dsp::QuadraturePair come from. Not part of the
//  build; run it by hand:
//
//      clang++ -std=c++20 -O2 -o /tmp/hilbert_design Scripts/HilbertDesign.cpp
//      /tmp/hilbert_design 7 10      (sections per chain, lower band edge in Hz at 48 kHz)
//
//  The structure is two chains of second-order allpasses (c + z^-2)/(1 + c z^-2),
//  the second with one extra sample of delay. Their phase difference should be
//  90 degrees from the lower edge to the same distance below Nyquist (the
//  structure is symmetric about a quarter of the sample rate, so only the lower
//  half needs fitting). Starting from pole magnitudes spread between the band
//  edge and the middle, alternating between the chains (the delayed chain takes
//  the one nearest 1), a Levenberg-Marquardt least-squares fit is pushed towards
//  minimax by reweighting each frequency by its error (Lawson's method).
//
//  For 7 sections and 10 Hz it prints a worst error of 0.053 degrees: an
//  unwanted sideband of tan(0.053/2 degrees), 66.7 dB down.
//

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>
using V = std::vector<double>;

double sectionPhase (double c, double w)
{
    // arg(c + e^{-2jw}) - arg(1 + c e^{-2jw})
    const double nr = c + std::cos (2 * w), ni = -std::sin (2 * w);
    const double dr = 1 + c * std::cos (2 * w), di = -c * std::sin (2 * w);
    return std::atan2 (ni, nr) - std::atan2 (di, dr);
}

double wrapPi (double x) { while (x > M_PI) x -= 2 * M_PI; while (x < -M_PI) x += 2 * M_PI; return x; }

int N;            // sections per branch
V grid;
double target = M_PI / 2;

double diff (const V& p, double w)
{
    double a = 0, b = -w;
    for (int i = 0; i < N; ++i) { a += sectionPhase (p[i], w); b += sectionPhase (p[N + i], w); }
    return wrapPi (a - b);
}

V residuals (const V& p, const V& weight)
{
    V r (grid.size());
    for (size_t j = 0; j < grid.size(); ++j) r[j] = weight[j] * wrapPi (diff (p, grid[j]) - target);
    return r;
}

double maxErrDeg (const V& p, double wl)
{
    double m = 0;
    for (int j = 0; j <= 20000; ++j)
    {
        const double w = wl + (M_PI - 2 * wl) * j / 20000.0;
        m = std::max (m, std::abs (wrapPi (diff (p, w) - target)));
    }
    return m * 180 / M_PI;
}

// Solve (A) x = b, small dense system, Gaussian elimination with pivoting.
V solve (std::vector<V> A, V b)
{
    const int n = (int) b.size();
    for (int i = 0; i < n; ++i)
    {
        int piv = i;
        for (int k = i + 1; k < n; ++k) if (std::abs (A[k][i]) > std::abs (A[piv][i])) piv = k;
        std::swap (A[i], A[piv]); std::swap (b[i], b[piv]);
        for (int k = i + 1; k < n; ++k)
        {
            const double f = A[k][i] / A[i][i];
            for (int c = i; c < n; ++c) A[k][c] -= f * A[i][c];
            b[k] -= f * b[i];
        }
    }
    V x (n);
    for (int i = n - 1; i >= 0; --i)
    {
        double s = b[i];
        for (int c = i + 1; c < n; ++c) s -= A[i][c] * x[c];
        x[i] = s / A[i][i];
    }
    return x;
}

V levenbergMarquardt (V p, const V& weight, int iterations)
{
    double lambda = 1e-3;
    auto cost = [&] (const V& q) { double s = 0; for (double r : residuals (q, weight)) s += r * r; return s; };
    double c0 = cost (p);
    const int n = (int) p.size();

    for (int it = 0; it < iterations; ++it)
    {
        const V r = residuals (p, weight);
        std::vector<V> J (r.size(), V (n));
        for (int k = 0; k < n; ++k)
        {
            V q = p; const double h = 1e-7; q[k] += h;
            const V rq = residuals (q, weight);
            for (size_t j = 0; j < r.size(); ++j) J[j][k] = (rq[j] - r[j]) / h;
        }
        std::vector<V> JtJ (n, V (n, 0)); V Jtr (n, 0);
        for (size_t j = 0; j < r.size(); ++j)
            for (int a = 0; a < n; ++a) { Jtr[a] += J[j][a] * r[j]; for (int b = 0; b < n; ++b) JtJ[a][b] += J[j][a] * J[j][b]; }

        for (int tries = 0; tries < 20; ++tries)
        {
            auto A = JtJ; for (int a = 0; a < n; ++a) A[a][a] *= (1 + lambda);
            V negJtr (n); for (int a = 0; a < n; ++a) negJtr[a] = -Jtr[a];
            const V step = solve (A, negJtr);
            V q = p; bool ok = true;
            for (int a = 0; a < n; ++a) { q[a] += step[a]; if (std::abs (q[a]) >= 0.99999999) ok = false; }
            const double c1 = ok ? cost (q) : 1e300;
            if (c1 < c0) { p = q; c0 = c1; lambda = std::max (1e-12, lambda * 0.3); break; }
            lambda *= 10;
        }
    }
    return p;
}

int main (int argc, char** argv)
{
    N = argc > 1 ? std::atoi (argv[1]) : 6;
    const double lowHz = argc > 2 ? std::atof (argv[2]) : 10.0, fs = 48000;
    const double wl = 2 * M_PI * lowHz / fs;

    // Grid on [wl, pi/2], dense near the low edge (log spacing), where the hard part is.
    for (int j = 0; j < 600; ++j) grid.push_back (wl * std::pow ((M_PI / 2) / wl, j / 599.0));

    // Initial guess: 2N pole magnitudes spread from near 1 (low edge) to small, alternating branches.
    V p (2 * N);
    for (int k = 0; k < 2 * N; ++k)
    {
        const double t = (k + 0.5) / (2 * N);                 // 0..1
        const double oneMinus = std::exp (std::log (wl) * (1 - t) + std::log (0.9) * t);
        const double m = 1 - oneMinus;
        p[((k + 1) % 2) * N + k / 2] = -m;   // the branch with the extra z^-1 takes the pole nearest 1
    }

    // Decide the sign of the target from the start.
    double s = 0; for (double w : grid) s += diff (p, w);
    target = s >= 0 ? M_PI / 2 : -M_PI / 2;

    V weight (grid.size(), 1.0);
    p = levenbergMarquardt (p, weight, 200);
    std::printf ("N=%d  least squares: max error %.4f deg\n", N, maxErrDeg (p, wl));

    // Lawson-style reweighting towards minimax.
    for (int round = 0; round < 60; ++round)
    {
        const V r = residuals (p, V (grid.size(), 1.0));
        double sum = 0;
        for (size_t j = 0; j < grid.size(); ++j) { weight[j] *= std::sqrt (std::abs (r[j]) + 1e-12); sum += weight[j]; }
        for (auto& w : weight) w *= grid.size() / sum;
        p = levenbergMarquardt (p, weight, 30);
    }

    std::printf ("N=%d  minimax:       max error %.4f deg (sideband %.1f dB), target %+.0f deg\n", N, maxErrDeg (p, wl),
                 20 * std::log10 (std::tan (maxErrDeg (p, wl) * M_PI / 360)), target * 180 / M_PI);
    V a (p.begin(), p.begin() + N), b (p.begin() + N, p.end());
    std::sort (a.begin(), a.end()); std::sort (b.begin(), b.end());
    std::printf ("branch I:"); for (double c : a) std::printf (" %.12f", c); std::printf ("\n");
    std::printf ("branch Q:"); for (double c : b) std::printf (" %.12f", c); std::printf ("\n");
}
