#pragma once
// Feedback Matrix: routes the outputs of the bus stages (Granular, Spectral, the tape
// delay and the final output) back into the inputs of Granular, Spectral and the delay.
// Every path has a DC blocker, a tone filter, a soft clip at the safety ceiling and its
// own limiter; while any path is up, the final output also passes a soft ceiling at 0 dBFS.
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "../DSP.h"

namespace tg
{

enum FbTap  { FT_Granular, FT_Spectral, FT_Delay, FT_Output, FT_COUNT };
enum FbDest { FD_Granular, FD_Spectral, FD_Delay, FD_COUNT };

struct FeedbackParams
{
    float amt[FT_COUNT][FD_COUNT] {};
    float timeMs = 120, toneHz = 6000, safety = 0.5f;
    bool active() const
    {
        for (auto& row : amt) for (float a : row) if (a > 1.0e-4f) return true;
        return false;
    }
};

class FeedbackMatrix
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        minDelay = std::max (64, maxBlock + 1);   // a path is always at least one block late (no zero-delay loops)
        len = juce::nextPowerOfTwo ((int) (sr * 1.1) + minDelay + 4);
        for (auto& t : tap) for (auto& ch : t) ch.assign ((size_t) len, 0.0f);
        relC = 1.0f - std::exp (-1.0f / (0.2f * (float) sr));
        atkC = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
        clear();
    }

    void clear()
    {
        for (auto& t : tap) for (auto& ch : t) std::fill (ch.begin(), ch.end(), 0.0f);
        for (auto& row : path) for (auto& p : row) p = Path();
        pos = 0;
    }

    // Called once per block before the stages, with this block's settings.
    bool begin (const FeedbackParams& p, int n)
    {
        params = p;
        blockLen = n;
        on = p.active();
        if (on && ! wasOn) clear();          // no stale audio from the last time it was used
        wasOn = on;
        if (! on) return false;
        delay = std::max ((double) minDelay, p.timeMs * 0.001 * sr);
        ceiling = 1.5f - 1.2f * juce::jlimit (0.0f, 1.0f, p.safety);
        lpC = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * juce::jlimit (50.0f, 20000.0f, p.toneHz) / (float) sr);
        dcR = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 15.0 / sr);
        return true;
    }

    // Adds the feedback arriving at destination d into L/R.
    void inject (int d, float* L, float* R)
    {
        if (! on) return;
        float* io[2] = { L, R };
        for (int s = 0; s < FT_COUNT; ++s)
        {
            const float a = params.amt[s][d];
            if (a <= 1.0e-4f) continue;
            Path& pt = path[s][d];
            for (int i = 0; i < blockLen; ++i)
            {
                double rp = (double) pos + i - delay;
                while (rp < 0) rp += len;
                const int i0 = (int) rp & (len - 1), i1 = (i0 + 1) & (len - 1);
                const float fr = (float) (rp - std::floor (rp));
                float y[2];
                for (int c = 0; c < 2; ++c)
                {
                    const auto& b = tap[s][c];
                    float x = b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
                    // DC blocker, tone, soft clip at the ceiling
                    const float dc = x - pt.dcX[c] + dcR * pt.dcY[c];
                    pt.dcX[c] = x; pt.dcY[c] = dc;
                    pt.lp[c] += (dc - pt.lp[c]) * lpC;
                    y[c] = ceiling * std::tanh (pt.lp[c] / ceiling);
                }
                // per-path limiter: holds the path at 70% of the ceiling
                const float pk = std::max (std::abs (y[0]), std::abs (y[1]));
                pt.env += (pk - pt.env) * (pk > pt.env ? atkC : relC);
                const float g = pt.env > 0.7f * ceiling ? 0.7f * ceiling / pt.env : 1.0f;
                for (int c = 0; c < 2; ++c)
                {
                    float v = y[c] * g * a * 0.95f;
                    if (! std::isfinite (v)) { v = 0.0f; pt = Path(); }
                    io[c][i] += v;
                }
            }
        }
    }

    // Records a stage's output for this block.
    void write (int t, const float* L, const float* R)
    {
        if (! on) return;
        for (int i = 0; i < blockLen; ++i)
        {
            const int w = (pos + i) & (len - 1);
            tap[t][0][(size_t) w] = std::isfinite (L[i]) ? L[i] : 0.0f;
            tap[t][1][(size_t) w] = std::isfinite (R[i]) ? R[i] : 0.0f;
        }
    }

    // Final soft ceiling (untouched below 0.9, never above 1.0) and the block advance.
    void end (float* L, float* R)
    {
        if (! on) return;
        for (int i = 0; i < blockLen; ++i) { L[i] = knee (L[i]); R[i] = knee (R[i]); }
        pos = (pos + blockLen) & (len - 1);
    }

    bool isOn() const { return on; }

private:
    static inline float knee (float x)
    {
        const float a = std::abs (x);
        if (! std::isfinite (x)) return 0.0f;
        if (a <= 0.9f) return x;
        return (x < 0 ? -1.0f : 1.0f) * (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f));
    }

    struct Path { float dcX[2] {}, dcY[2] {}, lp[2] {}; float env = 0; };
    double sr = 48000.0, delay = 1024;
    int len = 1 << 16, pos = 0, minDelay = 513, blockLen = 0;
    std::vector<float> tap[FT_COUNT][2];
    Path path[FT_COUNT][FD_COUNT];
    FeedbackParams params;
    float ceiling = 1.0f, lpC = 0.5f, dcR = 0.998f, relC = 0.0001f, atkC = 0.02f;
    bool on = false, wasOn = false;
};

} // namespace tg
