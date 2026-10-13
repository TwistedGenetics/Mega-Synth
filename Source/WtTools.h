#pragma once
// v0.1.74: tools for the wavetable oscillators' sampler: zero-crossing snap, root note detection and an auto-loop finder.
// They work on a loaded sample (message thread), never on the audio thread.
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "EngineTypes.h"

namespace tg
{
namespace wttools
{
    inline float mono (const WaveSample& w, int i)
    {
        i = juce::jlimit (0, w.length - 1, i);
        return w.numChannels > 1 ? 0.5f * (w.ch[0][(size_t) i] + w.ch[1][(size_t) i]) : w.ch[0][(size_t) i];
    }

    // the nearest upward zero crossing to pos (in frames), within maxDist; pos itself when there's none
    inline double snapToZero (const WaveSample& w, double pos, int maxDist)
    {
        const int p = (int) std::lround (pos);
        for (int d = 0; d <= maxDist; ++d)
            for (int s : { p + d, p - d })
                if (s > 0 && s < w.length && mono (w, s - 1) < 0.0f && mono (w, s) >= 0.0f) return (double) s;
        return pos;
    }

    // the pitch of the sample around its middle (or from..from+len), by YIN; 0 when none is clear
    inline double detectPitchHz (const WaveSample& w, double* clarity = nullptr)
    {
        const double sr = w.sampleRate;
        const int win = (int) (0.05 * sr), minLag = (int) (sr / 2000.0), maxLag = (int) (sr / 30.0);
        if (w.length < win + maxLag + 4) { if (clarity) *clarity = 0.0; return 0.0; }
        double bestHz = 0.0, bestC = 1.0;
        // three windows (a quarter, half and three quarters in), the clearest wins
        for (double where : { 0.25, 0.5, 0.75 })
        {
            const int s0 = juce::jlimit (0, w.length - win - maxLag - 2, (int) (where * w.length) - win / 2);
            std::vector<double> d ((size_t) maxLag + 2, 0.0), cm ((size_t) maxLag + 2, 1.0);
            for (int lag = 1; lag <= maxLag + 1; ++lag)
            {
                double s = 0.0;
                for (int i = 0; i < win; ++i) { const double e = mono (w, s0 + i) - mono (w, s0 + i + lag); s += e * e; }
                d[(size_t) lag] = s;
            }
            double run = 0.0;
            for (int lag = 1; lag <= maxLag + 1; ++lag) { run += d[(size_t) lag]; cm[(size_t) lag] = d[(size_t) lag] * lag / std::max (1.0e-20, run); }
            int best = -1;
            for (int lag = minLag; lag <= maxLag; ++lag)
                if (cm[(size_t) lag] < 0.15 && cm[(size_t) lag] <= cm[(size_t) lag - 1] && cm[(size_t) lag] <= cm[(size_t) lag + 1]) { best = lag; break; }
            if (best < 0) continue;
            const double a = cm[(size_t) best - 1], c = cm[(size_t) best], e = cm[(size_t) best + 1];
            const double off = (a - 2.0 * c + e) != 0.0 ? 0.5 * (a - e) / (a - 2.0 * c + e) : 0.0;
            if (c < bestC) { bestC = c; bestHz = sr / (best + off); }
        }
        if (clarity) *clarity = bestHz > 0.0 ? 1.0 - bestC : 0.0;
        return bestHz;
    }

    // a loop end near `le` whose sound flows back into the sound at `ls`: the candidates are upward zero crossings within
    // 10% of the loop's length (at most 0.2 s); each is scored by how alike the stretch before it is to the stretch before
    // ls (so the loop continues as the sound did). match: 0-1 (1: the same).
    struct LoopFind { double end = 0.0; double match = 0.0; };
    inline LoopFind findLoopEnd (const WaveSample& w, double ls, double le)
    {
        LoopFind best { le, -1.0 };
        const double span = le - ls;
        if (span < 64.0) return { le, 0.0 };
        const int W = (int) juce::jlimit (16.0, 2048.0, span * 0.25);
        const int reach = (int) std::min (span * 0.1, 0.2 * w.sampleRate);
        const int lsI = (int) std::lround (ls);
        auto score = [&] (int end)
        {
            // the W samples leading into ls, against the W leading into end (the loop plays end then ls)
            double ab = 0.0, aa = 0.0, bb = 0.0;
            for (int i = 1; i <= W; ++i)
            {
                const double a = mono (w, lsI - i), b = mono (w, end - i);
                ab += a * b; aa += a * a; bb += b * b;
            }
            return ab / std::sqrt (std::max (1.0e-20, aa * bb));
        };
        for (int c = (int) le - reach; c <= (int) le + reach; ++c)
        {
            if (c <= lsI + 32 || c >= w.length - 1) continue;
            if (! (mono (w, c - 1) < 0.0f && mono (w, c) >= 0.0f)) continue;
            const double sc = score (c);
            if (sc > best.match) best = { (double) c, sc };
        }
        if (best.match < 0.0) return { le, 0.0 };
        best.match = juce::jlimit (0.0, 1.0, best.match);
        return best;
    }
} // namespace wttools
} // namespace tg
