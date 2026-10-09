#pragma once
// Audio-rate transform: ring modulation, AM and frequency shifting of the voice, driven
// by a selectable modulator (an internal sine at a ratio of the note, one of the voice's
// own oscillators, or noise). The same modulator can also frequency-modulate the
// oscillators (that part runs inside the voice's oscillator loop).
#include <JuceHeader.h>
#include <cmath>

namespace tg
{

struct AudioRateParams
{
    int mod = 0;            // 0 sine (ratio), 1 Osc 1, 2 Osc 2, 3 Osc 3, 4 Sub, 5 noise
    float ratio = 1, offsetHz = 0, fm = 0, am = 0, ring = 0, shiftHz = 0, shiftMix = 0;
    int fmTarget = 0;       // 0 Osc 1-3 + Sub, 1 Osc 1, 2 Osc 2, 3 Osc 3, 4 every oscillator
    bool fxActive() const { return am > 1.0e-4f || ring > 1.0e-4f || shiftMix > 1.0e-4f; }
    bool fmActive() const { return fm > 1.0e-4f; }
};

// 90-degree phase-difference network (two cascades of four 2nd-order allpasses,
// O. Niemitalo's coefficients), good from about 20 Hz to 0.97 * Nyquist at 44.1 kHz.
struct Hilbert
{
    static constexpr float a1[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    static constexpr float a2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    float x1[4][2] {}, y1[4][2] {}, x2[4][2] {}, y2[4][2] {};
    float delay = 0.0f;

    void reset() { *this = Hilbert(); }

    static inline float stage (float in, float a, float (&x)[2], float (&y)[2])
    {
        const float out = a * a * (in + y[1]) - x[1];
        x[1] = x[0]; x[0] = in;
        y[1] = y[0]; y[0] = out;
        return out;
    }

    // returns the in-phase (I) and quadrature (Q) parts
    inline void process (float in, float& i, float& q)
    {
        float a = in, b = in;
        for (int k = 0; k < 4; ++k) a = stage (a, a1[k], x1[k], y1[k]);
        for (int k = 0; k < 4; ++k) b = stage (b, a2[k], x2[k], y2[k]);
        i = delay; delay = a;     // path 1 is delayed one sample
        q = b;
    }
};

class AudioRateFx
{
public:
    void reset() { for (auto& h : hil) h.reset(); shiftRe = 1.0; shiftIm = 0.0; }

    // ring / AM / frequency shift of one stereo sample; m is the modulator's sample
    inline void process (float& l, float& r, float m, bool stereo, const AudioRateParams& p, double rotCos, double rotSin)
    {
        auto one = [&] (float x, int c) -> float
        {
            if (p.ring > 0.0f) x += (x * m * 2.0f - x) * p.ring;
            if (p.am > 0.0f) x *= 1.0f + p.am * (m - 1.0f) * 0.5f;      // full depth: (1 + m) / 2
            if (p.shiftMix > 0.0f)
            {
                float i, q;
                hil[c].process (x, i, q);
                const float s = (float) (i * shiftRe + q * shiftIm);   // upper sideband for a positive shift
                x += (s - x) * p.shiftMix;
            }
            return x;
        };
        l = one (l, 0);
        r = stereo ? one (r, 1) : l;
        if (p.shiftMix > 0.0f)
        {
            // rotate the shift oscillator (complex phasor), renormalised now and then
            const double re = shiftRe * rotCos - shiftIm * rotSin;
            shiftIm = shiftRe * rotSin + shiftIm * rotCos; shiftRe = re;
            if (++renorm >= 256) { renorm = 0; const double mag = std::sqrt (shiftRe * shiftRe + shiftIm * shiftIm); shiftRe /= mag; shiftIm /= mag; }
        }
    }

private:
    Hilbert hil[2];
    double shiftRe = 1.0, shiftIm = 0.0;
    int renorm = 0;
};

} // namespace tg
