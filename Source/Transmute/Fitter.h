//
//  Fitter.h
//  Kitbox
//
//  From the Analyzer's first guess to the parameters that sound most like the
//  original: render, compare, adjust, a few thousand times. Ported from
//  Fitter.swift and NelderMead.swift - the same stages, budgets, grids and
//  drive cap; the stages' reasons are in the Swift file's header.
//

#pragma once

#include <atomic>
#include <functional>
#include <optional>

#include "Analyzer.h"
#include "Comparison.h"

namespace transmute
{
    struct FitReport
    {
        DrumParams params;
        MatchScore score;
        MatchScore initialScore;
        int evaluations = 0;
        double seconds = 0;
    };

    namespace Fitter
    {
        /** Progress 0...1, every 20 evaluations, from any thread. */
        using Progress = std::function<void (double)>;

        /** Fits `analysis` from its first guess. Empty if `cancel` was set. */
        std::optional<FitReport> fit (const Analysis& analysis, const Progress& progress = {},
                                      const std::atomic<bool>* cancel = nullptr);
    }
}
