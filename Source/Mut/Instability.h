#pragma once
// Cell Instability: a bank of smooth per-voice noise sources, wired straight onto a set of
// targets (pitch, cutoff, levels...) as built-in modulation, plus one more that the Mod
// Matrix can use as a source. Each source glides between random points with a cosine
// curve, so its value and its slope are continuous: it wanders, it never steps.
#include <cmath>
#include <cstdint>

namespace tg
{

enum CiTarget { CI_Pitch, CI_Cutoff, CI_Res, CI_Level, CI_Fold, CI_Scan, CI_Fm, CI_Dna, CI_Env, CI_Reso, CI_Source, CI_COUNT };

class Instability
{
public:
    void seed (uint32_t s)
    {
        rng = s ? s : 1u;
        for (int k = 0; k < CI_COUNT; ++k)
        {
            g[k].a = rnd(); g[k].b = rnd(); g[k].ph = 0.5 * (rnd() + 1.0);
            g[k].speed = 0.7 + 0.3 * (rnd() + 1.0);   // each target wanders at its own speed (0.7x - 1.3x)
        }
    }

    // advance by dt seconds at 'rateHz' new points per second (per target, scaled by its speed)
    void advance (double dt, double rateHz)
    {
        for (auto& x : g)
        {
            x.ph += dt * rateHz * x.speed;
            while (x.ph >= 1.0) { x.ph -= 1.0; x.a = x.b; x.b = rnd(); }
        }
    }

    // -1..1
    float value (int k) const
    {
        const auto& x = g[k];
        const double w = 0.5 - 0.5 * std::cos (3.14159265358979 * x.ph);
        return (float) (x.a + (x.b - x.a) * w);
    }

private:
    struct Gen { double a = 0, b = 0, ph = 0, speed = 1; };
    Gen g[CI_COUNT];
    uint32_t rng = 1;
    double rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 8388608.0 - 1.0; }
};

} // namespace tg
