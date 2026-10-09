#pragma once
// Granular: runs once on the summed voices, before the effects. A 4-second stereo
// buffer records the synth; up to 64 grains read it back with their own position,
// pitch, direction and pan. Freeze stops recording so the grains play the held sound.
#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace tg
{

struct GranularParams
{
    float mix = 0, sizeMs = 80, density = 20, position = 0.25f, jitter = 0.2f, pitch = 0, pitchRand = 0,
          reverse = 0, spread = 0.5f, feedback = 0;
    bool freeze = false;
    bool active() const { return mix > 1.0e-4f; }
};

class Granular
{
public:
    static constexpr int kMaxGrains = 64;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        len = juce::nextPowerOfTwo ((int) (sr * 4.0) + 4);
        for (auto& b : buf) b.assign ((size_t) len, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        wpos = 0; written = 0; spawnPh = 1.0; lastL = lastR = 0.0f;
        for (auto& g : grains) g.on = false;
        wasActive = false;
    }

    int activeGrains() const { int n = 0; for (auto& g : grains) n += g.on ? 1 : 0; return n; }

    void process (float* L, float* R, int n, const GranularParams& p)
    {
        if (! p.active()) { if (wasActive) reset(); return; }
        wasActive = true;

        const double grainLen = juce::jlimit (0.002, 1.0, p.sizeMs * 0.001) * sr;
        const double spawnInc = juce::jlimit (0.1f, 400.0f, p.density) / sr;
        const double posSamples = juce::jlimit (0.0, 3.0, (double) p.position) * sr;
        // overlapping grains add up: scale by the expected overlap
        const float overlap = (float) std::max (1.0, grainLen * spawnInc);
        const float norm = 1.0f / std::sqrt (overlap);
        const float fb = juce::jlimit (0.0f, 0.95f, p.feedback);

        for (int i = 0; i < n; ++i)
        {
            // record (unless frozen); feedback writes the grains back into the buffer
            if (! p.freeze)
            {
                buf[0][(size_t) wpos] = L[i] + fb * std::tanh (lastL);
                buf[1][(size_t) wpos] = R[i] + fb * std::tanh (lastR);
                wpos = (wpos + 1) & (len - 1);
                written = std::min (written + 1, len);
            }

            spawnPh += spawnInc;
            if (spawnPh >= 1.0)
            {
                spawnPh -= std::floor (spawnPh);
                spawn (p, grainLen, posSamples);
            }

            float wl = 0.0f, wr = 0.0f;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float ph = (float) (g.age / g.length);
                const float win = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * ph);   // Hann
                const double rp = g.pos;
                int i0 = (int) std::floor (rp);
                const float fr = (float) (rp - i0);
                i0 &= (len - 1);
                const int i1 = (i0 + 1) & (len - 1);
                const float sl = buf[0][(size_t) i0] + (buf[0][(size_t) i1] - buf[0][(size_t) i0]) * fr;
                const float sr2 = buf[1][(size_t) i0] + (buf[1][(size_t) i1] - buf[1][(size_t) i0]) * fr;
                // Spread: each grain moves from the source's own stereo towards a mono grain panned at random
                const float m = 0.5f * (sl + sr2), a = std::abs (g.pan);
                wl += win * (sl + (m * g.gl - sl) * a);
                wr += win * (sr2 + (m * g.gr - sr2) * a);
                g.pos += g.rate;
                if (g.pos < 0) g.pos += len;
                if (g.pos >= len) g.pos -= len;
                g.age += 1.0;
                if (g.age >= g.length) g.on = false;
            }
            wl *= norm; wr *= norm;
            if (! std::isfinite (wl) || ! std::isfinite (wr)) { reset(); wl = wr = 0.0f; }
            lastL = wl; lastR = wr;
            L[i] += (wl - L[i]) * p.mix;
            R[i] += (wr - R[i]) * p.mix;
        }
    }

private:
    struct Grain { bool on = false; double pos = 0, rate = 1, age = 0, length = 1; float pan = 0, gl = 1, gr = 1; };

    inline float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }   // 0..1

    void spawn (const GranularParams& p, double grainLen, double posSamples)
    {
        Grain* g = nullptr;
        for (auto& x : grains) if (! x.on) { g = &x; break; }
        if (g == nullptr) return;   // all 64 busy: skip this one
        const double semis = p.pitch + (rnd() * 2.0f - 1.0f) * 12.0f * p.pitchRand;
        double rate = std::exp2 (semis / 12.0);
        const bool rev = rnd() < p.reverse;
        // position: how far back from "now", spread by Jitter (up to +/- one second)
        double back = posSamples + (rnd() * 2.0f - 1.0f) * p.jitter * sr;
        // a grain must never overtake the write head, or read further back than has been recorded
        const double minBack = 64.0 + (rev ? 0.0 : (p.freeze ? grainLen * rate : std::max (0.0, grainLen * (rate - 1.0))));
        const double maxBack = (double) written - 4.0 - (rev ? grainLen * rate : 0.0);
        if (maxBack < minBack) return;   // not enough audio recorded yet
        back = juce::jlimit (minBack, maxBack, back);
        g->on = true;
        g->rate = rev ? -rate : rate;
        g->pos = (double) wpos - back;
        while (g->pos < 0) g->pos += len;
        g->age = 0.0;
        g->length = grainLen;
        g->pan = (rnd() * 2.0f - 1.0f) * p.spread;
        const float th = (g->pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        g->gl = std::cos (th) * 1.41421356f; g->gr = std::sin (th) * 1.41421356f;
    }

    double sr = 48000.0, spawnPh = 1.0;
    std::vector<float> buf[2];
    int len = 1 << 18, wpos = 0, written = 0;
    Grain grains[kMaxGrains];
    float lastL = 0, lastR = 0;
    uint32_t rng = 0x2545F491u;
    bool wasActive = false;
};

} // namespace tg
