#pragma once
// The Granular Synth: a playable, per-voice granular oscillator over a loaded sample (Oscillators >
// Granular Synth). Separate from the Granular bus stage (Bus Mutation), which grains the whole mix.
// Each voice runs its own cloud: grains start every 1 / Density seconds at the playhead (Position,
// moving at Scan speed) plus a random Spray, play at the note's pitch (key tracked from Root Note),
// with an envelope of the chosen shape. Every window reaches zero at both ends, so grains don't click.
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>
#include "EngineTypes.h"

namespace tg
{

class GranVoice
{
public:
    struct Params
    {
        float position = 0.25f, scan = 0.0f, sprayMs = 20.0f, sizeMs = 80.0f, density = 20.0f;
        int shape = 0;
        float semis = 0.0f, pitchRand = 0.0f, reverse = 0.0f, spread = 0.3f;
        double ratio = 1.0;    // the note's pitch against the root (key tracking, bend, pitch envelope)
        int maxGrains = 24;    // Quality: Eco 8, Normal 24, High 48
    };
    static constexpr int kMaxGrains = 48;

    void prepare (double sampleRate) { sr = sampleRate; }
    void start (uint64_t seed)
    {
        rng = (uint32_t) (seed * 2654435761u) ^ 0x9E3779B9u;
        if (rng == 0) rng = 1;
        for (auto& g : grains) g.on = false;
        untilNext = 0.0;
        scanPos = -1.0; scanOff = 0.0;
        started = true;
    }
    void stop() { started = false; for (auto& g : grains) g.on = false; }
    int activeGrains() const { int n = 0; for (auto& g : grains) n += g.on ? 1 : 0; return n; }
    void burstNow() { untilNext = 0.0; }   // (Break React: a grain right away)
    void limitWait (double samples) { if (untilNext > samples) untilNext = samples; }   // (... and the next ones as dense as the burst asks)
    double playhead() const { return scanPos; }   // in sample frames (for the display)

    // Adds one stereo sample of the cloud (unscaled by the level)
    inline void tick (const WaveSample& w, const Params& p, float& outL, float& outR)
    {
        outL = outR = 0.0f;
        if (! started || w.length < 2) return;
        const double len = (double) w.length;
        const double fileToOut = w.sampleRate / sr;
        // the playhead: Position plus how far Scan has moved it since the note started
        scanOff += (double) p.scan * fileToOut;
        if (scanOff >= len) scanOff -= len; else if (scanOff <= -len) scanOff += len;
        scanPos = clampd (p.position, 0.0, 1.0) * (len - 1.0) + scanOff;
        if (scanPos >= len) scanPos -= len; else if (scanPos < 0.0) scanPos += len;

        if (untilNext <= 0.0)
        {
            spawn (w, p);
            const double d = std::max (0.5, (double) p.density);
            untilNext += sr / d;
        }
        untilNext -= 1.0;

        const float* l = w.ch[0].data();
        const float* r = w.ch[w.numChannels > 1 ? 1 : 0].data();
        for (auto& g : grains)
        {
            if (! g.on) continue;
            const float env = windowFast (g.shape, (float) (g.age * g.invLen));
            const double pos = g.pos;
            const int i0 = std::min (w.length - 1, (int) pos);
            const float fr = (float) (pos - i0);
            const int i1 = i0 + 1 < w.length ? i0 + 1 : 0;
            const float a = l[i0] + (l[i1] - l[i0]) * fr;
            const float b = r[i0] + (r[i1] - r[i0]) * fr;
            const float e = env * g.gain;
            // spread: each grain sits somewhere across the stereo field
            outL += (a * g.pl + b * (1.0f - g.pl) * g.cross) * e;
            outR += (b * g.pr + a * (1.0f - g.pr) * g.cross) * e;
            g.pos += g.inc;
            if (g.pos >= len) g.pos -= len; else if (g.pos < 0.0) g.pos += len;
            if (++g.age >= g.len) g.on = false;
        }
    }

    // the grain envelope, x in 0..1; zero at both ends
    static inline float window (int shape, float x)
    {
        x = std::min (1.0f, std::max (0.0f, x));
        switch (shape)
        {
            case 1: return 1.0f - std::abs (2.0f * x - 1.0f);                                   // triangle
            case 2: { const float e = 0.15f; if (x < e) return 0.5f - 0.5f * std::cos (3.14159265f * x / e);   // flat top (Tukey)
                      if (x > 1.0f - e) return 0.5f - 0.5f * std::cos (3.14159265f * (1.0f - x) / e); return 1.0f; }
            case 3: { const float a = 0.03f; if (x < a) return x / a;                          // percussive: fast in, exponential out
                      const float d = std::exp (-5.0f * (x - a) / (1.0f - a)); return (d - 0.0067f) / (1.0f - 0.0067f) * std::min (1.0f, (1.0f - x) / 0.02f); }
            case 4: { const float u = (x - 0.5f) * 6.0f; const float floor = std::exp (-0.5f * 9.0f); // Gaussian, shifted to reach zero
                      return (std::exp (-0.5f * u * u) - floor) / (1.0f - floor); }
            default: return 0.5f - 0.5f * std::cos (6.2831853f * x);                            // Hann
        }
    }

    // the same windows from a table (4096 points, interpolated)
    static inline float windowFast (int shape, float x)
    {
        static const std::vector<float> table = []
        {
            std::vector<float> t ((size_t) 5 * (kWin + 2));
            for (int sh = 0; sh < 5; ++sh)
                for (int i = 0; i <= kWin + 1; ++i) t[(size_t) (sh * (kWin + 2) + i)] = window (sh, (float) i / (float) kWin);
            return t;
        }();
        const float p = std::min (1.0f, std::max (0.0f, x)) * (float) kWin;
        const int i = (int) p;
        const float* r = table.data() + (size_t) (std::min (4, std::max (0, shape)) * (kWin + 2) + i);
        return r[0] + (r[1] - r[0]) * (p - (float) i);
    }

private:
    static constexpr int kWin = 4096;
    struct Grain { bool on = false; double pos = 0.0, inc = 1.0; int age = 0, len = 1; double invLen = 1.0; int shape = 0; float gain = 1.0f, pl = 1.0f, pr = 1.0f, cross = 0.0f; };

    static double clampd (double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f; }

    void spawn (const WaveSample& w, const Params& p)
    {
        int slot = -1;
        const int cap = std::max (1, std::min (kMaxGrains, p.maxGrains));
        int live = 0;
        for (int k = 0; k < kMaxGrains; ++k) { if (grains[k].on) ++live; else if (slot < 0 && k < cap) slot = k; }
        if (slot < 0 || live >= cap) return;   // the Quality limit: this grain is skipped
        auto& g = grains[slot];
        const double len = (double) w.length;
        const double spray = (double) p.sprayMs * 0.001 * w.sampleRate * (2.0 * rand01() - 1.0);
        double pos = scanPos + spray;
        pos = std::fmod (pos, len); if (pos < 0.0) pos += len;
        const double semis = p.semis + (p.pitchRand > 0.0f ? p.pitchRand * (2.0f * rand01() - 1.0f) : 0.0f);
        double inc = std::pow (2.0, semis / 12.0) * p.ratio * w.sampleRate / sr;
        if (p.reverse > 0.0f && rand01() < p.reverse) inc = -inc;
        g.on = true; g.pos = pos; g.inc = inc; g.age = 0;
        g.len = std::max (16, (int) (p.sizeMs * 0.001 * sr));
        g.invLen = 1.0 / (double) g.len;
        g.shape = p.shape;
        // level: keeps roughly the same loudness however many grains overlap
        const float overlap = std::max (1.0f, p.density * p.sizeMs * 0.001f);
        g.gain = 1.0f / std::sqrt (overlap) / std::max (0.05f, w.peak);
        const float pan = std::min (1.0f, std::max (0.0f, p.spread)) * (2.0f * rand01() - 1.0f);
        g.pl = std::min (1.0f, 1.0f - pan); g.pr = std::min (1.0f, 1.0f + pan);
        g.cross = std::abs (pan) * 0.5f;
    }

    double sr = 48000.0;
    Grain grains[kMaxGrains];
    double untilNext = 0.0, scanPos = -1.0, scanOff = 0.0;
    uint32_t rng = 1;
    bool started = false;
};

} // namespace tg
