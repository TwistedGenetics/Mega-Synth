#pragma once
// Break Sequencer pattern tools: Original, Reverse, Shuffle, Jungle edit, Clear. Shuffle and Jungle
// use a seed, so the same press gives the same result (and tests can check it).
#include <algorithm>
#include <vector>
#include <algorithm>
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

// the break as it was: the slices in order across the pattern, each where it falls in time.
// brk: which break's slicing to follow (0-2); pin: the steps always play that break (otherwise the
// Active Break)
inline void original (BreakData& d, int pat, int steps, int brk = 0, bool pin = false)
{
    const BreakSlot& b = d.brk[std::clamp (brk, 0, BreakData::kBreaks - 1)];
    auto* st = d.step[pat];
    for (int i = 0; i < BreakData::kSteps; ++i) { st[i] = BreakStep(); if (pin) st[i].brk = brk + 1; }
    const int n = std::max (1, b.nSlices);
    const int len = std::max (1, b.start[b.nSlices] - b.start[0]);
    if (b.nSlices <= 0)
    {
        for (int i = 0; i < steps; i += 2) { st[i].on = true; st[i].slice = (i / 2) % 16; }
        return;
    }
    // a slice goes on the step nearest to where it starts in the break (the break spans the pattern)
    for (int k = 0; k < n; ++k)
    {
        const double frac = (double) (b.start[k] - b.start[0]) / (double) len;
        const int i = std::min (steps - 1, (int) std::lround (frac * steps));
        if (st[i].on) continue;
        st[i].on = true; st[i].slice = k; st[i].vel = 0.8f;
    }
}

// the slices of one break that sound most like a kind of hit (from the analysis): up to three, only
// the clear ones (at least half the best score); fallback when there's no analysis
inline std::vector<int> topSlices (const BreakSlot& b, const float* score, int fallback)
{
    const int n = std::max (1, b.nSlices);
    if (b.nSlices <= 0) return { fallback % n };
    std::vector<int> v ((size_t) n);
    for (int k = 0; k < n; ++k) v[(size_t) k] = k;
    std::sort (v.begin(), v.end(), [&] (int x, int y) { return score[x] > score[y]; });
    v.resize ((size_t) std::min (3, n));
    const float best = score[v.front()];
    v.erase (std::remove_if (v.begin() + 1, v.end(), [&] (int k) { return score[k] < 0.5f * best; }), v.end());
    return v;
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
inline void jungle (BreakData& d, int pat, int steps, int seed, int brk = 0)
{
    original (d, pat, steps, brk);
    Rng r (seed);
    const BreakSlot& b = d.brk[std::clamp (brk, 0, BreakData::kBreaks - 1)];
    const int n = std::max (1, b.nSlices);
    // the slices that sound most like snares and kicks (from the analysis; else the usual places)
    const auto snares = topSlices (b, b.snare, 2 % n), kicks = topSlices (b, b.kick, 0);
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

// Frankenbreak: the groove of one break, with its kicks, snares and hats each taken from a different
// loaded break (which gets which turns with the seed). Amount: the share of hits replaced.
inline void frankenbreak (BreakData& d, int pat, int steps, int seed, float amount, int groove)
{
    groove = std::clamp (groove, 0, BreakData::kBreaks - 1);
    original (d, pat, steps, groove, true);
    std::vector<int> loaded;
    for (int k = 0; k < BreakData::kBreaks; ++k) if (d.brk[(groove + k) % BreakData::kBreaks].nSlices > 0) loaded.push_back ((groove + k) % BreakData::kBreaks);
    if (loaded.size() < 2) return;   // one break: nothing to swap in
    Rng r (seed);
    const int nl = (int) loaded.size(), rot = r.below (nl);
    const int from[3] = { loaded[(size_t) ((1 + rot) % nl)], loaded[(size_t) ((2 + rot) % nl)], loaded[(size_t) ((0 + rot) % nl)] };   // kick, snare, hat
    const BreakSlot& g = d.brk[groove];
    for (int i = 0; i < steps; ++i)
    {
        auto& s = d.step[pat][i];
        if (! s.on || g.nSlices <= 0) continue;
        const int k = std::clamp (s.slice, 0, g.nSlices - 1);
        const int kind = g.kick[k] >= g.snare[k] && g.kick[k] >= g.hat[k] ? 0 : (g.snare[k] >= g.hat[k] ? 1 : 2);
        if (r.next() >= amount) continue;
        const BreakSlot& src = d.brk[from[kind]];
        const auto pick = topSlices (src, kind == 0 ? src.kick : (kind == 1 ? src.snare : src.hat), 0);
        s.slice = pick[(size_t) r.below ((int) pick.size())];
        s.brk = from[kind] + 1;
    }
}

// Swap Break: the same pattern played with another break's slices at the same positions
inline void swapBreak (BreakData& d, int pat, int steps, int target)
{
    target = std::clamp (target, 0, BreakData::kBreaks - 1);
    const int n = d.brk[target].nSlices;
    for (int i = 0; i < steps; ++i)
    {
        auto& s = d.step[pat][i];
        if (! s.on && ! s.tie) continue;
        s.brk = target + 1;
        if (n > 0) s.slice = s.slice % n;
    }
}

} // namespace breaktools
} // namespace tg
