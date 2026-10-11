#pragma once
// Break Sequencer pattern tools: Original, Reverse, Shuffle, Jungle edit, Clear. Shuffle and Jungle
// use a seed, so the same press gives the same result (and tests can check it).
#include <algorithm>
#include <vector>
#include "BreakStore.h"

namespace tg
{
namespace breaktools
{

struct Rng
{
    uint32_t s;
    explicit Rng (int seed) : s ((uint32_t) seed * 2654435761u + 0x2545F491u) { if (s == 0) s = 1; }
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 16777216.0f; }
    int below (int n) { return std::min (n - 1, (int) (next() * (float) n)); }
};

// the break as it was: the slices in order across the pattern, each where it falls in time
inline void original (BreakData& d, int pat, int steps)
{
    auto* st = d.step[pat];
    for (int i = 0; i < BreakData::kSteps; ++i) st[i] = BreakStep();
    const int n = std::max (1, d.nSlices);
    const int len = std::max (1, d.start[d.nSlices] - d.start[0]);
    if (d.nSlices <= 0)
    {
        for (int i = 0; i < steps; i += 2) { st[i].on = true; st[i].slice = (i / 2) % 16; }
        return;
    }
    // a slice goes on the step nearest to where it starts in the break (the break spans the pattern)
    for (int k = 0; k < n; ++k)
    {
        const double frac = (double) (d.start[k] - d.start[0]) / (double) len;
        const int i = std::min (steps - 1, (int) std::lround (frac * steps));
        if (st[i].on) continue;
        st[i].on = true; st[i].slice = k; st[i].vel = 0.8f;
    }
}

inline void reverseOrder (BreakData& d, int pat, int steps)
{
    std::reverse (d.step[pat], d.step[pat] + steps);
}

inline void shuffle (BreakData& d, int pat, int steps, int seed)
{
    Rng r (seed);
    std::vector<int> on;
    for (int i = 0; i < steps; ++i) if (d.step[pat][i].on) on.push_back (i);
    std::vector<int> slices;
    for (int i : on) slices.push_back (d.step[pat][i].slice);
    for (int i = (int) slices.size() - 1; i > 0; --i) std::swap (slices[(size_t) i], slices[(size_t) r.below (i + 1)]);
    for (size_t k = 0; k < on.size(); ++k) d.step[pat][on[k]].slice = slices[k];
}

inline void clear (BreakData& d, int pat) { for (auto& s : d.step[pat]) s = BreakStep(); }

// A jungle edit of the original: snare ghosts and swaps late in each bar, a kick double, a reversed
// hit, a cut-up of two 1/8s, and a rising snare roll into the end of the pattern.
inline void jungle (BreakData& d, int pat, int steps, int seed)
{
    original (d, pat, steps);
    Rng r (seed);
    const int n = std::max (1, d.nSlices);
    // the slices that sound most like snares and kicks (from the analysis; else the usual places)
    std::vector<int> idx (n);
    for (int k = 0; k < n; ++k) idx[(size_t) k] = k;
    auto top = [&] (const float* score, int fallback)
    {
        if (d.nSlices <= 0) return std::vector<int> { fallback };
        auto v = idx;
        std::sort (v.begin(), v.end(), [&] (int a, int b) { return score[a] > score[b]; });
        v.resize ((size_t) std::min (3, n));
        // only the clear ones (at least half the best score)
        const float best = score[v.front()];
        v.erase (std::remove_if (v.begin() + 1, v.end(), [&] (int k) { return score[k] < 0.5f * best; }), v.end());
        return v;
    };
    const auto snares = top (d.snare, 2 % n), kicks = top (d.kick, 0);
    auto* st = d.step[pat];
    auto snare = [&] { return snares[(size_t) r.below ((int) snares.size())]; };
    for (int bar = 0; bar < steps / 16; ++bar)
    {
        const int b0 = bar * 16;
        // ghost snares and swaps in the second half of the bar
        for (int i = b0 + 8; i < b0 + 16; ++i)
            if (r.next() < 0.3f) { st[i].on = true; st[i].slice = snare(); st[i].vel = st[i].on && i % 4 == 0 ? 0.85f : 0.5f; }
        // a kick double just before the second snare
        st[b0 + 10].on = true; st[b0 + 10].slice = kicks[0]; st[b0 + 10].vel = 0.9f;
        // a reversed hit
        const int rv = b0 + 2 + 2 * r.below (6);
        if (st[rv].on) st[rv].reverse = true;
    }
    // cut-up: two 1/8s swap places in the first bar
    {
        const int a = 2 * r.below (4), b = 8 + 2 * r.below (4);
        std::swap (st[a].slice, st[b].slice);
    }
    // the roll: the last four steps, faster and rising
    const int e = steps - 4;
    const int sn = snare();
    for (int k = 0; k < 4; ++k)
    {
        auto& s = st[e + k];
        s.on = true; s.slice = sn; s.roll = k < 2 ? 2 : (k == 2 ? 3 : 4); s.pitch = (float) k; s.vel = 0.6f + 0.1f * k; s.accent = k == 3;
    }
}

} // namespace breaktools
} // namespace tg
