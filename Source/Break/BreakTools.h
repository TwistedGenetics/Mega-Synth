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

//==============================================================================
// The pattern tools (Stage 4). Each is seeded (the same seed gives the same result), changes the
// pattern by Amount (0..1), and draws its slices from one break or from all the loaded breaks.
// Kicks, snares and hats come from the analysis; with none, from where they usually fall.

enum Kind { Kick, Snare, Hat };

// a slice of a kind, from the chosen break or (all) from a loaded break picked at random
struct Picker
{
    const BreakData& d;
    int brk;            // 0-2: the break the pattern plays (the Active Break)
    bool all;
    Rng& r;

    static bool hasAnalysis (const BreakSlot& b)
    {
        for (int k = 0; k < b.nSlices; ++k) if (b.kick[k] > 1.0e-6f || b.snare[k] > 1.0e-6f || b.hat[k] > 1.0e-6f) return true;
        return false;
    }
    // the slice playing at a 16th of a bar-long break (for breaks without an analysis)
    static int sliceAtStep (const BreakSlot& b, int s16, int steps = 16)
    {
        if (b.nSlices <= 0) return s16 % 16;
        const int len = std::max (1, b.start[b.nSlices] - b.start[0]);
        const double at = (double) s16 / steps * len + b.start[0];
        for (int k = b.nSlices - 1; k >= 0; --k) if (b.start[k] <= at + 1.0) return k;
        return 0;
    }
    static std::vector<int> slicesOf (const BreakSlot& b, Kind kind)
    {
        if (hasAnalysis (b)) return topSlices (b, kind == Kick ? b.kick : (kind == Snare ? b.snare : b.hat), 0);
        const int bars = std::max (1, b.bars);
        const int steps = 16 * bars;
        switch (kind)   // kick on 1, snare on 2, hat on the off-beat 8th
        {
            case Kick:  return { sliceAtStep (b, 0, steps) };
            case Snare: return { sliceAtStep (b, 4, steps) };
            default:    return { sliceAtStep (b, 2, steps) };
        }
    }
    // the kind a slice of break k sounds most like
    static Kind kindOf (const BreakSlot& b, int slice)
    {
        if (b.nSlices <= 0) return Hat;
        const int k = std::clamp (slice, 0, b.nSlices - 1);
        if (! hasAnalysis (b))
        {
            const int steps = 16 * std::max (1, b.bars);
            const int len = std::max (1, b.start[b.nSlices] - b.start[0]);
            const int s16 = (int) std::lround ((double) (b.start[k] - b.start[0]) / len * steps) % 16;
            return s16 == 0 || s16 == 10 ? Kick : (s16 == 4 || s16 == 12 ? Snare : Hat);
        }
        return b.kick[k] >= b.snare[k] && b.kick[k] >= b.hat[k] ? Kick : (b.snare[k] >= b.hat[k] ? Snare : Hat);
    }
    // sets a step to a hit of this kind
    void put (BreakStep& s, Kind kind)
    {
        int from = brk;
        if (all)
        {
            std::vector<int> loaded;
            for (int k = 0; k < BreakData::kBreaks; ++k) if (d.brk[k].nSlices > 0) loaded.push_back (k);
            if (! loaded.empty()) from = loaded[(size_t) r.below ((int) loaded.size())];
        }
        const auto v = slicesOf (d.brk[from], kind);
        s.on = true; s.tie = false;
        s.slice = v[(size_t) r.below ((int) v.size())];
        s.brk = all ? from + 1 : 0;
    }
};

inline Kind stepKind (const BreakData& d, const BreakStep& s, int activeBrk)
{
    return Picker::kindOf (d.brk[s.brk > 0 ? std::clamp (s.brk - 1, 0, 2) : activeBrk], s.slice);
}

// Rollers: 16th ghost snares at low velocity around the backbeat
inline void rollers (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    for (int b0 = 0; b0 < steps; b0 += 16)
        for (int o : { 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 })
        {
            auto& s = st[b0 + o];
            if (s.on || s.tie || r.next() >= amount) continue;
            pk.put (s, Snare);
            s.vel = 0.22f + 0.18f * r.next();
            s.accent = false; s.reverse = false; s.roll = 1; s.pitch = 0.0f;
        }
}

// Two-Step: kick on 1 and the "and" of 2, snares on 2 and 4, the rest from the pattern (its other
// kicks thinned out by Amount)
inline void twoStep (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    for (int b0 = 0; b0 < steps; b0 += 16)
    {
        for (int o = 0; o < 16; ++o)
        {
            auto& s = st[b0 + o];
            if (o == 0 || o == 4 || o == 6 || o == 12) continue;
            if (s.on && stepKind (d, s, brk) == Kick && r.next() < amount) s = BreakStep();
        }
        for (int o : { 0, 6 }) { auto& s = st[b0 + o]; s = BreakStep(); pk.put (s, Kick); s.vel = o == 0 ? 0.9f : 0.8f; }
        for (int o : { 4, 12 }) { auto& s = st[b0 + o]; s = BreakStep(); pk.put (s, Snare); s.vel = 0.9f; }
    }
}

// Halftime: the snare on 3 only, kicks spread out (1 and the "and" of 3), hats from the break
// (Amount: how many 8th hats)
inline void halftime (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    for (int i = 0; i < steps; ++i) st[i] = BreakStep();
    for (int b0 = 0; b0 < steps; b0 += 16)
    {
        pk.put (st[b0], Kick); st[b0].vel = 0.95f;
        pk.put (st[b0 + 8], Snare); st[b0 + 8].vel = 0.95f; st[b0 + 8].accent = true;
        pk.put (st[b0 + 11], Kick); st[b0 + 11].vel = 0.75f;
        for (int o : { 2, 4, 6, 10, 12, 14 })
            if (r.next() < 0.25f + 0.75f * amount) { pk.put (st[b0 + o], Hat); st[b0 + o].vel = 0.55f + 0.15f * r.next(); }
    }
}

// Double-time: each half bar squeezed into a quarter and played twice (Amount: the share of half bars)
inline void doubleTime (BreakData& d, int pat, int steps, int seed, float amount)
{
    Rng r (seed);
    auto* st = d.step[pat];
    for (int h0 = 0; h0 < steps; h0 += 8)
    {
        if (r.next() >= std::max (amount, 0.001f)) continue;
        BreakStep q[4];
        for (int j = 0; j < 4; ++j) q[j] = st[h0 + 2 * j].on ? st[h0 + 2 * j] : st[h0 + 2 * j + 1];
        for (int j = 0; j < 8; ++j) st[h0 + j] = q[j % 4];
    }
}

// Snare Build: a snare roll that speeds up through the last bar (x2, then x3, then x4), its pitch
// rising (Amount: how far it rises, up to an octave, and how much of the bar it covers)
inline void snareBuild (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    const int len = std::clamp ((int) std::lround (16.0f * (0.5f + 0.5f * amount)), 4, 16), e0 = steps - len;
    BreakStep proto; pk.put (proto, Snare);
    for (int j = 0; j < len; ++j)
    {
        auto& s = st[e0 + j];
        const float f = (float) j / (float) std::max (1, len - 1);
        s = proto;
        s.roll = f < 1.0f / 3.0f ? 2 : (f < 2.0f / 3.0f ? 3 : 4);
        s.pitch = std::round (12.0f * amount * f);
        s.vel = 0.45f + 0.5f * f;
        s.accent = j == len - 1;
    }
}

// Stutter: a 1/16 or 1/32 of a hit repeated 2-4 times at random places (Amount: how many)
inline void stutter (BreakData& d, int pat, int steps, int seed, float amount)
{
    Rng r (seed);
    auto* st = d.step[pat];
    const int places = std::max (1, (int) std::lround (amount * (float) steps / 8.0f));
    for (int n = 0; n < places; ++n)
    {
        std::vector<int> on;
        for (int i = 1; i < steps - 1; ++i) if (st[i].on && ! st[i].tie) on.push_back (i);
        if (on.empty()) return;
        const int i = on[(size_t) r.below ((int) on.size())];
        const int reps = 2 + r.below (3);
        if (r.next() < 0.5f)
            for (int j = 1; j < reps && i + j < steps; ++j) { st[i + j] = st[i]; st[i + j].roll = 1; st[i + j].vel = st[i].vel * (1.0f - 0.08f * j); }   // 1/16s
        else
        {
            const int n32 = (reps + 1) / 2;
            for (int j = 0; j < n32 && i + j < steps; ++j) { st[i + j] = st[i]; st[i + j].roll = 2; }   // 1/32s
        }
    }
}

// Reverse Swell: a reversed snare on the step before each backbeat (Amount: how many backbeats)
inline void reverseSwell (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    for (int b0 = 0; b0 < steps; b0 += 16)
        for (int bb : { 4, 12 })
        {
            if (r.next() >= std::max (amount, 0.001f)) continue;
            auto& s = st[b0 + bb - 1];
            s = BreakStep(); pk.put (s, Snare);
            s.reverse = true; s.vel = 0.7f;
        }
}

// Cut-up: 1/8s swapped at random, keeping the kick on 1 and the snares on 2 and 4 (Amount: how many swaps)
inline void cutUp (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    for (int b0 = 0; b0 < steps; b0 += 16)
    {
        const int free8[] = { 1, 3, 4, 5, 7 };   // the 1/8s that move (0, 2 and 6 hold the kick and the snares)
        const int swaps = std::max (1, (int) std::lround (amount * 4.0f));
        for (int n = 0; n < swaps; ++n)
        {
            const int a = free8[r.below (5)], b = free8[r.below (5)];
            if (a == b) continue;
            std::swap (st[b0 + 2 * a], st[b0 + 2 * b]); std::swap (st[b0 + 2 * a + 1], st[b0 + 2 * b + 1]);
        }
        if (! st[b0].on || stepKind (d, st[b0], brk) != Kick) { st[b0] = BreakStep(); pk.put (st[b0], Kick); st[b0].vel = 0.9f; }
        for (int o : { 4, 12 }) if (! st[b0 + o].on || stepKind (d, st[b0 + o], brk) != Snare) { st[b0 + o] = BreakStep(); pk.put (st[b0 + o], Snare); st[b0 + o].vel = 0.9f; }
    }
}

// Ghosts: quiet hits on empty steps (Amount: how many)
inline void ghosts (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    for (int i = 0; i < steps; ++i)
    {
        auto& s = st[i];
        if (s.on || s.tie || r.next() >= 0.6f * amount) continue;
        s = BreakStep(); pk.put (s, r.next() < 0.6f ? Snare : Hat);
        s.vel = 0.18f + 0.17f * r.next();
    }
}

// Hat Shuffle: only the hat-like hits change places (Amount: the share of them that move)
inline void hatShuffle (BreakData& d, int pat, int steps, int seed, float amount, int brk)
{
    Rng r (seed);
    auto* st = d.step[pat];
    std::vector<int> hats;
    for (int i = 0; i < steps; ++i) if (st[i].on && ! st[i].tie && stepKind (d, st[i], brk) == Hat && r.next() < std::max (amount, 0.001f)) hats.push_back (i);
    std::vector<BreakStep> v; for (int i : hats) v.push_back (st[i]);
    for (int i = (int) v.size() - 1; i > 0; --i) std::swap (v[(size_t) i], v[(size_t) r.below (i + 1)]);
    for (size_t k = 0; k < hats.size(); ++k) { const float vel = st[hats[k]].vel; st[hats[k]] = v[k]; st[hats[k]].vel = vel; }   // (the groove's dynamics stay)
}

// Thin Out: hits removed or made less likely, for breakdowns (the kick on 1 and the backbeats stay)
inline void thinOut (BreakData& d, int pat, int steps, int seed, float amount)
{
    Rng r (seed);
    auto* st = d.step[pat];
    for (int i = 0; i < steps; ++i)
    {
        auto& s = st[i];
        const int o = i % 16;
        if (! s.on || o == 0 || o == 4 || o == 12 || r.next() >= amount) continue;
        if (r.next() < 0.5f) s = BreakStep();
        else s.prob = std::max (0.1f, s.prob * 0.5f);
    }
}

// Humanise: small changes to velocity, and hits played a little late, within the swing (Amount: how much)
inline void humanise (BreakData& d, int pat, int steps, int seed, float amount)
{
    Rng r (seed);
    auto* st = d.step[pat];
    for (int i = 0; i < steps; ++i)
    {
        auto& s = st[i];
        if (! s.on) continue;
        s.vel = std::clamp (s.vel + (r.next() - 0.5f) * 0.3f * amount, 0.05f, 1.0f);
        s.nudge = (i % 4 == 0) ? 0.0f : std::round (r.next() * 0.16f * amount * 100.0f) / 100.0f;   // (the beats stay on the grid)
    }
}

// Pitch Dive: the pitch falls over the last beat (Amount: down to an octave)
inline void pitchDive (BreakData& d, int pat, int steps, float amount)
{
    auto* st = d.step[pat];
    int last = -1;
    for (int i = 0; i < steps - 4; ++i) if (st[i].on && ! st[i].tie) last = i;
    for (int j = 0; j < 4; ++j)
    {
        auto& s = st[steps - 4 + j];
        if (! s.on && last >= 0) { s = st[last]; s.vel *= 0.9f; }   // (an empty step: the last hit again)
        if (s.on) s.pitch = std::round (-12.0f * amount * (float) (j + 1) / 4.0f);
        if (s.on) last = steps - 4 + j;
    }
}

// Variation: small random changes to the pattern as it is (its own Amount)
inline void variation (BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    Rng r (seed); Picker pk { d, brk, all, r };
    auto* st = d.step[pat];
    for (int i = 1; i < steps; ++i)
    {
        auto& s = st[i];
        if (! s.on || s.tie || r.next() >= 0.5f * amount) continue;
        switch (r.below (4))
        {
            case 0: { const Kind k = stepKind (d, s, brk); const float v = s.vel; pk.put (s, k); s.vel = v; break; }   // another slice of the same kind
            case 1: s.vel = std::clamp (s.vel + (r.next() - 0.5f) * 0.4f, 0.1f, 1.0f); break;
            case 2: if (i % 4 != 0) s.roll = s.roll > 1 ? 1 : 2; break;
            default: if (i + 1 < steps && ! st[i + 1].on && i % 4 != 0) { st[i + 1] = s; s = BreakStep(); } break;   // a hit moves a 16th later
        }
    }
}

// The tools by number (the Break Sequencer page's buttons, the fills): 0 Rollers ... 13 Variation
enum Tool { ToolRollers, ToolTwoStep, ToolHalftime, ToolDoubleTime, ToolSnareBuild, ToolStutter, ToolReverseSwell, ToolCutUp,
            ToolGhosts, ToolHatShuffle, ToolThinOut, ToolHumanise, ToolPitchDive, ToolVariation, kNumTools };
inline const char* toolName (int t)
{
    static const char* n[] = { "Rollers", "Two-Step", "Halftime", "Double", "Snare Build", "Stutter", "Rev Swell", "Cut-up",
                               "Ghosts", "Hat Shuffle", "Thin Out", "Humanise", "Pitch Dive", "Variation" };
    return n[std::clamp (t, 0, (int) kNumTools - 1)];
}
inline void applyTool (int tool, BreakData& d, int pat, int steps, int seed, float amount, int brk, bool all)
{
    amount = std::clamp (amount, 0.0f, 1.0f);
    brk = std::clamp (brk, 0, BreakData::kBreaks - 1);
    // an empty pattern: start from the break as it was
    bool any = false;
    for (int i = 0; i < steps; ++i) any = any || d.step[pat][i].on;
    if (! any && tool != ToolHalftime) original (d, pat, steps, brk);
    switch (tool)
    {
        case ToolRollers:      rollers (d, pat, steps, seed, amount, brk, all); break;
        case ToolTwoStep:      twoStep (d, pat, steps, seed, amount, brk, all); break;
        case ToolHalftime:     halftime (d, pat, steps, seed, amount, brk, all); break;
        case ToolDoubleTime:   doubleTime (d, pat, steps, seed, amount); break;
        case ToolSnareBuild:   snareBuild (d, pat, steps, seed, amount, brk, all); break;
        case ToolStutter:      stutter (d, pat, steps, seed, amount); break;
        case ToolReverseSwell: reverseSwell (d, pat, steps, seed, amount, brk, all); break;
        case ToolCutUp:        cutUp (d, pat, steps, seed, amount, brk, all); break;
        case ToolGhosts:       ghosts (d, pat, steps, seed, amount, brk, all); break;
        case ToolHatShuffle:   hatShuffle (d, pat, steps, seed, amount, brk); break;
        case ToolThinOut:      thinOut (d, pat, steps, seed, amount); break;
        case ToolHumanise:     humanise (d, pat, steps, seed, amount); break;
        case ToolPitchDive:    pitchDive (d, pat, steps, amount); break;
        case ToolVariation:    variation (d, pat, steps, seed, amount, brk, all); break;
        default: break;
    }
}

} // namespace breaktools
} // namespace tg
