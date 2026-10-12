#pragma once
// Half-band low-pass filters for 2x / 4x oversampling (Kaiser-windowed; every other tap is zero and the
// centre is 1/2). The same design as the Reese engine's drive (kept separate so the Reese is untouched).
#include <algorithm>
#include <cmath>

namespace tg
{
namespace hb
{

struct HalfBand
{
    float side[48] {};   // the non-zero taps, h[2j]
    int L = 0, m = 0;
    void design (int mm)
    {
        constexpr double pi = 3.14159265358979323846;
        m = mm; const int N = 4 * m + 3; L = (N + 1) / 2;
        const double c = (N - 1) * 0.5, beta = 8.96;
        auto i0 = [] (double x) { double s = 1, t = 1; for (int j = 1; j < 30; ++j) { t *= (x / (2 * j)) * (x / (2 * j)); s += t; } return s; };
        double sum = 0;
        for (int j = 0; j < L; ++j)
        {
            const double k = 2.0 * j - c, r = k / c;
            const double h = std::sin (0.5 * pi * k) / (pi * k) * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
            side[j] = (float) h; sum += h;
        }
        for (int j = 0; j < L; ++j) side[j] = (float) (side[j] * 0.5 / sum);   // (DC gain 1, with the centre's 1/2)
        for (int j = L; j < 48; ++j) side[j] = 0.0f;
        L = (L + 7) & ~7;   // (padded with zero taps to a multiple of 8, for the vector loop)
    }
};

// one channel's history for going down (two samples in, one out)
struct Down
{
    float x[96] {}, f[96] {};   // (doubled rings, so a run of taps is always contiguous)
    int pos = 0;
    void clear() { std::fill (std::begin (x), std::end (x), 0.0f); std::fill (std::begin (f), std::end (f), 0.0f); pos = 0; }
    inline float dot (const HalfBand& h, const float* in) const
    {
        float s[8] {};
        for (int j = 0; j < h.L; j += 8) for (int q = 0; q < 8; ++q) s[q] += h.side[j + q] * in[j + q];
        return ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
    }
    inline float down (const HalfBand& h, float first, float second)
    {
        pos = (pos == 0 ? h.L : pos) - 1; x[pos] = x[pos + h.L] = second; f[pos] = f[pos + h.L] = first;
        return dot (h, x + pos) + 0.5f * f[pos + h.m];
    }
};

} // namespace hb
} // namespace tg
