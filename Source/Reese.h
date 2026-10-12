#pragma once
// The Reese engine: 2, 3 or 4 detuned band-limited saws (or squares) per note, built for jungle and
// drum & bass basses.
//
//  - Detune in Cents (as usual), in Hz (the same beating on every note) or in Beats: the beating is a
//    note value (1/16 .. 4 bars) at the song's tempo, worked out per note, so the wobble lands on the
//    beat at any pitch. Voices 3 and 4 beat at x3 or x5 that rate (still locked: every difference
//    between voices is a whole multiple of it) or at the golden ratio (free).
//  - Phase Twist: each voice's phase is pushed back and forth (slow: a swirl, fast: a growl).
//  - Width spreads the voices; Rotation moves them round the stereo field; Mono Below keeps the low end
//    in the middle (the side signal is high-passed there).
//  - A moving notch (synced to the beating, or free), a phase-locked sine sub an octave down, and a
//    soft drive with a tone control.
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <iterator>
#include "DSP.h"
#include "Params.h"

namespace tg
{

struct ReeseSettings
{
    int voices = 2, wave = 0, mode = 2, beat = 3, ratio = 0, retrig = 0;
    float cents = 14.0f, hz = 1.5f, twist = 0.15f, twistRate = 0.6f, width = 0.6f, rotate = 0.0f, rotRate = 0.15f, monoHz = 120.0f;
    float notch = 0.0f, notchFreq = 900.0f, notchMove = 0.5f, notchRate = 0.5f, sub = 0.0f, drive = 0.0f, tone = 1.0f;
    bool notchSync = true;
    int quality = 1;   // the drive is oversampled: Eco none, Normal x2, High x4
};

class ReeseOsc
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate; isr = 1.0 / sr;
        // the drive's oversampling filters: two half-band stages (48 -> 96 kHz: flat to 20 kHz, -90 dB from 28 kHz;
        // 96 -> 192 kHz: -90 dB from 76 kHz), used both ways
        hbA.design (17); hbB.design (5);
    }

    // a new note: the phases start free (as they were), together, or at random
    void start (int retrig, uint32_t seed)
    {
        if (retrig == 1) { for (auto& p : ph) p = 0.0; acc0 = 0.0; }
        else if (retrig == 2)
        {
            uint32_t r = seed * 2654435761u + 0x9E3779B9u;
            for (auto& p : ph) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; p = (double) (r & 0xffffff) / 16777216.0; }
            acc0 = ph[0];
        }
        for (auto& s : side) s = {};
        for (auto& s : notchSt) s = {};
        toneLp[0] = toneLp[1] = 0.0f;
        notchCount = 0;
        for (auto& x : upA) x.clear(); for (auto& x : upB) x.clear(); for (auto& x : dnA) x.clear(); for (auto& x : dnB) x.clear(); adaPrev[0] = adaPrev[1] = 0.0f; adaF[0] = adaF[1] = 1.0f; driveLatched = false; xfN = 0; pitchMul = 1.0; notchJump = 0.0f; panCount = 0;
    }

    // the voices' frequencies for this block: f = the note, tempo in BPM
    void setFrequency (double f, double tempo, const ReeseSettings& s)
    {
        st = s;
        n = std::clamp (s.voices + 2, 2, 4);
        static const double ratios[3] = { 3.0, 5.0, 1.6180339887 };
        const double r = ratios[std::clamp (s.ratio, 0, 2)];
        const double u[4] = { -1.0, 1.0, n == 3 ? r : -r, r };   // offsets, in half the beat rate
        double delta;   // the beating between voices 1 and 2, in Hz
        if (s.mode == 0)
        {
            const double c = std::max (0.0f, s.cents) / 1200.0;
            for (int k = 0; k < n; ++k) freq[k] = f * std::pow (2.0, u[k] * c * 0.5);
            delta = freq[1] - freq[0];
        }
        else
        {
            delta = s.mode == 1 ? (double) std::max (0.0f, s.hz)
                                : std::max (20.0, tempo) / 60.0 / kRsBeatBeats[std::clamp (s.beat, 0, 12)];
            for (int k = 0; k < n; ++k) freq[k] = std::max (0.0, f + u[k] * delta * 0.5);
        }
        beatHz = delta;
        // (the coefficients that change slowly)
        if (s.monoHz > 1.0f) sideG = (float) std::tan (kPi * std::min ((double) s.monoHz, sr * 0.45) * isr);
        toneC = s.tone >= 0.999f ? 1.0f : 1.0f - (float) std::exp (-2.0 * kPi * std::min (20000.0, 800.0 * std::pow (2.0, 4.6 * (double) s.tone)) * isr);
        driveK = 1.0f + 9.0f * std::clamp (s.drive, 0.0f, 1.0f);
        driveN = std::sqrt (1.0f + driveK * driveK) / driveK;   // (full scale in, full scale out)
        driveMix = std::min (1.0f, 5.0f * std::max (0.0f, s.drive));
    }

    // one stereo sample
    inline void tick (float& outL, float& outR)
    {
        const float tw = (float) std::sin (2.0 * kPi * twPh);
        const float tw2 = (float) std::cos (2.0 * kPi * twPh);
        if ((panCount++ & 7) == 0)
        {
            // the voices' pans (spread by Width, moved round by Rotation; equal power), every 8 samples
            static const float basePan[3][4] = { { -1.0f, 1.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, -0.33f, 0.33f } };
            const float rot = st.rotate > 0.0f ? (float) (2.0 * kPi * rotPh) : 0.0f;
            const float norm = n == 2 ? 0.62f : (n == 3 ? 0.52f : 0.45f);
            for (int k = 0; k < n; ++k)
            {
                const float pan = std::clamp (st.width * basePan[n - 2][k] + (rot != 0.0f ? st.rotate * std::sin (rot + (float) k * 6.2831853f / (float) n) : 0.0f), -1.0f, 1.0f);
                const float a = (pan + 1.0f) * 0.7853982f;
                gL[k] = norm * std::cos (a); gR[k] = norm * std::sin (a);
            }
        }
        float l, r;
        // (once a note has used the drive it stays on the oversampled path, so moving the drive to and from
        //  zero does not jump between paths; the drive fades in from zero)
        if (st.drive > 0.0f) driveLatched = true;
        const int os = driveLatched ? (st.quality >= 2 ? 4 : (st.quality == 1 ? 2 : 1)) : 1;
        if (os == 1)
        {
            voicesAndSub (l, r, tw, tw2, 1.0);
            if (st.drive > 0.0f) { l += (shape (l) - l) * driveMix; r += (shape (r) - r) * driveMix; }
        }
        else
        {
            // the drive oversampled (x2 or x4, by half-band stages) and anti-aliased (the antiderivative method):
            // the voices are made at the real rate (they have nothing above it), taken up, driven, and the drive's
            // new harmonics filtered off above 20 kHz on the way back down, so they do not fold back as aliasing
            voicesAndSub (l, r, tw, tw2, 1.0);
            float a2[2], b2[2];
            upA[0].up (hbA, l, a2); upA[1].up (hbA, r, b2);
            float da[2], db[2];
            for (int j = 0; j < 2; ++j)
            {
                if (os == 4)
                {
                    float a4[2], b4[2];
                    upB[0].up (hbB, a2[j], a4); upB[1].up (hbB, b2[j], b4);
                    for (int q = 0; q < 2; ++q) { a4[q] += (adaa (a4[q], 0) - a4[q]) * driveMix; b4[q] += (adaa (b4[q], 1) - b4[q]) * driveMix; }
                    da[j] = dnB[0].down (hbB, a4[0], a4[1]); db[j] = dnB[1].down (hbB, b4[0], b4[1]);
                }
                else { da[j] = a2[j] + (adaa (a2[j], 0) - a2[j]) * driveMix; db[j] = b2[j] + (adaa (b2[j], 1) - b2[j]) * driveMix; }
            }
            l = dnA[0].down (hbA, da[0], da[1]); r = dnA[1].down (hbA, db[0], db[1]);
        }
        // tone
        if (toneC < 1.0f) { toneLp[0] += (l - toneLp[0]) * toneC; toneLp[1] += (r - toneLp[1]) * toneC; l = toneLp[0]; r = toneLp[1]; }
        // the moving notch
        if (st.notch > 0.0f)
        {
            if ((notchCount++ & 15) == 0)
            {
                const double mp = st.notchSync ? beatPh : notchPh;
                const double fc = std::clamp ((double) st.notchFreq * std::pow (2.0, 2.0 * st.notchMove * std::sin (2.0 * kPi * mp) + (double) notchJump), 30.0, sr * 0.45);
                notchG = (float) std::tan (kPi * fc * isr);
            }
            const float k = 0.8f;   // (fairly wide)
            float* io[2] = { &l, &r };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = notchSt[c];
                const float a1 = 1.0f / (1.0f + notchG * (notchG + k)), a2 = notchG * a1, a3 = notchG * a2;
                const float x = *io[c];
                const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
                const float notched = x - k * v1;
                *io[c] = x + (notched - x) * st.notch;
            }
        }
        // mono below: the side signal high-passed (4-pole Butterworth), so the low end is the same both sides
        if (st.monoHz > 1.0f)
        {
            const float mid = 0.5f * (l + r);
            float sd = 0.5f * (l - r);
            static const float ks[2] = { 1.8477591f, 0.7653669f };
            for (int j = 0; j < 2; ++j)
            {
                auto& s = side[j];
                const float k = ks[j], a1 = 1.0f / (1.0f + sideG * (sideG + k)), a2 = sideG * a1, a3 = sideG * a2;
                const float v3 = sd - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
                sd = sd - k * v1 - v2;
            }
            l = mid + sd; r = mid - sd;
        }
        // the slow clocks
        beatPh += beatHz * isr; beatPh -= std::floor (beatPh);
        twPh += std::max (0.0f, st.twistRate) * isr; twPh -= std::floor (twPh);
        rotPh += std::max (0.0f, st.rotRate) * isr; rotPh -= std::floor (rotPh);
        notchPh += std::max (0.0f, st.notchRate) * isr; notchPh -= std::floor (notchPh);
        outL = l; outR = r;
    }

    // ---- Break-Reactive mode
    // every voice (and the sub) back to the start of its cycle now, the old phases faded out over xfade samples
    void resetPhases (int xfade)
    {
        std::copy (std::begin (ph), std::end (ph), std::begin (oldPh)); oldAcc = acc0;
        for (auto& p : ph) p = 0.0;
        acc0 = 0.0; beatPh = 0.0;
        xfLen = std::max (1, xfade); xfN = xfLen;
    }
    void setPitchMul (double m) { pitchMul = m; }          // a pitch kick (1 = none)
    void setNotchJump (float octaves) { notchJump = octaves; }   // the notch moved up (read every 16 samples)
    double beatPhase() const { return beatPh; }     // where the beat cycle is (0..1): the "Reese Phase" Mod Matrix source
    double beatRate() const { return beatHz; }      // the beating between voices 1 and 2, in Hz
    double voiceFrequency (int k) const { return freq[std::clamp (k, 0, 3)]; }
    int voiceCount() const { return n; }

private:
    // the voices (and the sub) for one sample at the rate sr / step
    inline void voicesAndSub (float& outL, float& outR, float tw, float tw2, double step)
    {
        sumVoices (ph, acc0, outL, outR, tw, tw2, step);
        if (xfN > 0)
        {
            // just after a phase reset (Break-Reactive mode): the old phases fade out over a moment, so it doesn't click
            float a, b; sumVoices (oldPh, oldAcc, a, b, tw, tw2, step);
            const float w = (float) xfN / (float) xfLen;
            outL += (a - outL) * w; outR += (b - outR) * w;
            --xfN;
        }
    }
    inline void sumVoices (double* phs, double& acc, float& outL, float& outR, float tw, float tw2, double step)
    {
        const WaveBank& bank = WaveBank::get();
        const float twistAmt = 0.25f * std::clamp (st.twist, 0.0f, 1.0f);
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            const double f = freq[k] * pitchMul;
            const float fos = (float) (f * isr);   // (the table for the real rate: nothing above its Nyquist)
            // phase twist: voices pushed in alternate directions (k even: sine, odd: cosine)
            const float off = twistAmt * ((k & 1) ? tw2 : tw) * (k < 2 ? 1.0f : -1.0f);
            double p = phs[k] + off; p -= std::floor (p);
            float x;
            switch (st.wave)
            {
                case 1:  x = bank.sample (W_SQUARE, p, fos); break;
                case 2:  x = 0.5f * (bank.sample (W_SAW, p, fos) + bank.sample (W_SQUARE, p, fos)); break;
                default: x = bank.sample (W_SAW, p, fos); break;
            }
            l += x * gL[k]; r += x * gR[k];
            phs[k] += f * isr * step; if (phs[k] >= 1.0) phs[k] -= std::floor (phs[k]);
        }
        // the sub: a sine an octave down, locked to voice 1 (its phase runs over two of voice 1's cycles)
        acc += freq[0] * pitchMul * isr * step; if (acc >= 2.0) acc -= 2.0;
        if (st.sub > 0.0f) { const float sb = st.sub * 0.8f * (float) std::sin (kPi * acc); l += sb; r += sb; }
        outL = l; outR = r;
    }
    double oldPh[4] {}, oldAcc = 0.0;
    int xfN = 0, xfLen = 1;
    double pitchMul = 1.0;
    float notchJump = 0.0f;
    // the drive's curve: k x / sqrt (1 + (k x)^2), a smooth saturation like tanh whose antiderivative
    // (sqrt (1 + (k x)^2) / k) is cheap; anti-aliased: the mean of the curve between this sample and the last
    inline float shape (float x) const { const float z = driveK * x; return z / std::sqrt (1.0f + z * z) * driveN; }
    inline float adaa (float x, int c)
    {
        const float z = driveK * x, F = std::sqrt (1.0f + z * z);
        const float xp = adaPrev[c], d = x - xp;
        float y;
        if (std::abs (d) > 1.0e-3f) y = (F - adaF[c]) / (driveK * d) * driveN;
        else y = shape (0.5f * (x + xp));
        adaPrev[c] = x; adaF[c] = F;
        return y;
    }
    float adaF[2] { 1.0f, 1.0f };
    float driveMix = 0.0f;
    bool driveLatched = false;
    float adaPrev[2] {};
    float gL[4] {}, gR[4] {};
    int panCount = 0;
    // a half-band low-pass (length 4m + 3: every other tap is zero, the centre is 1/2), Kaiser-windowed
    struct HalfBand
    {
        float side[48] {};   // the non-zero taps, h[2j]
        int L = 0, m = 0;
        void design (int mm)
        {
            m = mm; const int N = 4 * m + 3; L = (N + 1) / 2;
            const double c = (N - 1) * 0.5, beta = 8.96;
            auto i0 = [] (double x) { double s = 1, t = 1; for (int j = 1; j < 30; ++j) { t *= (x / (2 * j)) * (x / (2 * j)); s += t; } return s; };
            double sum = 0;
            for (int j = 0; j < L; ++j)
            {
                const double k = 2.0 * j - c, r = k / c;
                const double h = std::sin (0.5 * kPi * k) / (kPi * k) * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
                side[j] = (float) h; sum += h;
            }
            for (int j = 0; j < L; ++j) side[j] = (float) (side[j] * 0.5 / sum);   // (DC gain 1, with the centre's 1/2)
            L = (L + 7) & ~7;   // (padded with zero taps to a multiple of 8, for the vector loop)
        }
    };
    // one channel's history for going up (one sample in, two out) or down (two in, one out)
    struct HbState
    {
        float x[96] {}, f[96] {};   // (doubled rings, so a run of taps is always contiguous)
        int pos = 0;
        void clear() { std::fill (std::begin (x), std::end (x), 0.0f); std::fill (std::begin (f), std::end (f), 0.0f); pos = 0; }
        inline float dot (const HalfBand& hb, const float* h) const
        {
            float s[8] {};   // (8 running sums: vector instructions)
            for (int j = 0; j < hb.L; j += 8) for (int q = 0; q < 8; ++q) s[q] += hb.side[j + q] * h[j + q];
            return ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
        }
        inline void up (const HalfBand& hb, float in, float* out)
        {
            pos = (pos == 0 ? hb.L : pos) - 1; x[pos] = x[pos + hb.L] = in;   // x[pos + j] = the input j samples ago
            out[0] = 2.0f * dot (hb, x + pos);
            out[1] = x[pos + hb.m];                                           // (the centre tap, delayed)
        }
        inline float down (const HalfBand& hb, float first, float second)
        {
            pos = (pos == 0 ? hb.L : pos) - 1; x[pos] = x[pos + hb.L] = second; f[pos] = f[pos + hb.L] = first;
            return dot (hb, x + pos) + 0.5f * f[pos + hb.m];
        }
    };
    HalfBand hbA, hbB;
    HbState upA[2], upB[2], dnA[2], dnB[2];
    struct Svf { float ic1 = 0.0f, ic2 = 0.0f; };
    double sr = 48000.0, isr = 1.0 / 48000.0;
    ReeseSettings st;
    int n = 2;
    double ph[4] {}, freq[4] { 110.0, 110.0, 110.0, 110.0 }, acc0 = 0.0;
    double beatPh = 0.0, beatHz = 0.0, twPh = 0.0, rotPh = 0.0, notchPh = 0.0;
    Svf side[2], notchSt[2];
    float sideG = 0.01f, notchG = 0.05f, toneC = 1.0f, toneLp[2] {}, driveK = 1.0f, driveN = 1.0f;
    int notchCount = 0;
};

} // namespace tg
