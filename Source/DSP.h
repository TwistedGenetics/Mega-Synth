#pragma once
// Low-level DSP building blocks that reproduce the Web Audio nodes the
// browser synth was built from (OscillatorNode/PeriodicWave, BiquadFilterNode,
// WaveShaperNode, setTargetAtTime smoothing, linear ADSR ramps).

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

namespace tg
{

constexpr double kPi = 3.14159265358979323846;

template <typename T> inline T clampv (T v, T lo, T hi) { return std::max (lo, std::min (hi, v)); }

inline double midiToFreq (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

// Rational tanh approximation (max error ~1e-6 over the clamped range).
inline float fastTanh (float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)))
             / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}

// WaveShaperNode with makeDriveCurve(k): the curve maps input -1..1, input is clamped.
inline float driveShape (float x, float k) { return fastTanh (k * clampv (x, -1.0f, 1.0f)); }

// Web Audio setTargetAtTime approximated per control block.
inline float smoothCoef (double dt, double tau) { return (float) (1.0 - std::exp (-dt / std::max (1.0e-5, tau))); }

//==============================================================================
// Band-limited single-cycle tables for the 10 oscillator shapes, with one
// table per octave so high notes don't alias (Web Audio oscillators are
// band-limited too).
enum WaveId { W_SAW, W_SQUARE, W_TRI, W_SINE, W_PULSE25, W_PULSE12, W_TRAP, W_ORGAN, W_ODDSINE, W_SINEFOLD, W_COUNT };

// Choice index (as listed in the UI) -> WaveId
inline const int kWaveChoiceToId[10]  = { W_SAW, W_SQUARE, W_TRI, W_SINE, W_PULSE25, W_PULSE12, W_TRAP, W_ORGAN, W_ODDSINE, W_SINEFOLD };
inline const int kWaveBChoiceToId[10] = { W_SINE, W_SAW, W_SQUARE, W_TRI, W_PULSE25, W_PULSE12, W_TRAP, W_ORGAN, W_ODDSINE, W_SINEFOLD };
inline const int kSubChoiceToId[5]    = { W_SQUARE, W_SINE, W_TRI, W_PULSE25, W_SAW };

class WaveBank
{
public:
    static constexpr int N = 2048;
    static constexpr int kLevels = 11; // max harmonics 1023, 512, 256 ... 1

    static const WaveBank& get()
    {
        static const WaveBank bank;
        return bank;
    }

    // phase in [0,1), freqOverSr = |f| / sampleRate
    inline float sample (int wave, double phase, float freqOverSr) const
    {
        // pick the richest table whose top harmonic stays below Nyquist:
        // level = ceil(log2(2048 * f / sr)), via the float exponent
        int e = 0;
        std::frexp (2048.0f * freqOverSr, &e);
        const int level = e < 0 ? 0 : (e > kLevels - 1 ? kLevels - 1 : e);
        const float* t = tables[(size_t) wave][(size_t) level].data();
        double idx = phase * N;
        int i0 = (int) idx;
        float frac = (float) (idx - i0);
        i0 &= (N - 1);
        return t[i0] + (t[i0 + 1] - t[i0]) * frac;
    }

private:
    static int maxHarm (int level) { return level == 0 ? 1023 : (1024 >> level); }

    static double coef (int wave, int n)
    {
        const double pi = kPi;
        switch (wave)
        {
            case W_SAW:     return 2.0 / (n * pi) * ((n & 1) ? 1.0 : -1.0);
            case W_SQUARE:  return (n & 1) ? 4.0 / (n * pi) : 0.0;
            case W_TRI:     return (n & 1) ? 8.0 / (n * n * pi * pi) * (((n & 3) == 1) ? 1.0 : -1.0) : 0.0;
            case W_SINE:    return n == 1 ? 1.0 : 0.0;
            // custom periodic waves from the browser synth (64-point tables => harmonics 1..63)
            case W_PULSE25: return n < 64 ? (4.0 / (n * pi)) * std::sin (n * pi * 0.25) : 0.0;
            case W_PULSE12: return n < 64 ? (4.0 / (n * pi)) * std::sin (n * pi * 0.125) : 0.0;
            case W_TRAP:    return n < 64 ? (((n % 2) ? 1.0 : 0.35) / n) * std::exp (-n / 12.0) : 0.0;
            case W_ORGAN:
            {
                static const int hs[] = { 1, 2, 3, 4, 5, 6, 8 };
                static const double as[] = { 1.0, 0.7, 0.45, 0.28, 0.18, 0.12, 0.08 };
                for (int k = 0; k < 7; ++k) if (hs[k] == n) return as[k];
                return 0.0;
            }
            case W_ODDSINE: return (n < 64 && (n & 1)) ? 1.0 / (n * n * 0.8) : 0.0;
            case W_SINEFOLD:
            {
                static const int hs[] = { 1, 2, 3, 5, 7, 9 };
                static const double as[] = { 1.0, 0.55, 0.32, 0.16, 0.1, 0.06 };
                for (int k = 0; k < 6; ++k) if (hs[k] == n) return as[k];
                return 0.0;
            }
            default: return 0.0;
        }
    }

    WaveBank()
    {
        std::vector<double> sinTab (N);
        for (int i = 0; i < N; ++i) sinTab[(size_t) i] = std::sin (2.0 * kPi * i / N);

        for (int w = 0; w < W_COUNT; ++w)
        {
            std::vector<double> acc (N);
            double norm = 1.0;
            for (int l = 0; l < kLevels; ++l)
            {
                std::fill (acc.begin(), acc.end(), 0.0);
                const int H = maxHarm (l);
                for (int n = 1; n <= H; ++n)
                {
                    const double c = coef (w, n);
                    if (std::abs (c) < 1.0e-30) continue;
                    for (int i = 0; i < N; ++i)
                        acc[(size_t) i] += c * sinTab[(size_t) ((int64_t) n * i & (N - 1))];
                }
                if (l == 0)
                {
                    // Web Audio normalises periodic waves so the full-band peak is 1
                    double mx = 0.0;
                    for (double v : acc) mx = std::max (mx, std::abs (v));
                    norm = mx > 0.0 ? 1.0 / mx : 1.0;
                }
                auto& t = tables[(size_t) w][(size_t) l];
                t.resize (N + 1);
                for (int i = 0; i < N; ++i) t[(size_t) i] = (float) (acc[(size_t) i] * norm);
                t[N] = t[0];
            }
        }
    }

    std::array<std::array<std::vector<float>, kLevels>, W_COUNT> tables;
};

//==============================================================================
// BiquadFilterNode with Web Audio's coefficient formulas (lowpass/highpass Q in dB).
struct Biquad
{
    enum Type { LP, HP, BP, NOTCH };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = { 0, 0 }, z2[2] = { 0, 0 };

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void copyState (int from, int to) { z1[to] = z1[from]; z2[to] = z2[from]; }

    void set (Type type, double freq, double q, double sampleRate)
    {
        const double nyq = sampleRate * 0.5;
        const double fn = clampv (freq / nyq, 0.0, 1.0);
        if (type == LP || type == HP)
        {
            if (fn >= 1.0) { if (type == LP) setPass(); else setZero(); return; }
            if (fn <= 0.0) { if (type == LP) setZero(); else setPass(); return; }
            const double w0 = kPi * fn;
            const double g = std::pow (10.0, q / 20.0);
            const double alpha = std::sin (w0) / (2.0 * g);
            const double c = std::cos (w0);
            const double ia0 = 1.0 / (1.0 + alpha);
            if (type == LP) { b0 = (1 - c) * 0.5 * ia0; b1 = (1 - c) * ia0; b2 = b0; }
            else            { b0 = (1 + c) * 0.5 * ia0; b1 = -(1 + c) * ia0; b2 = b0; }
            a1 = -2.0 * c * ia0; a2 = (1.0 - alpha) * ia0;
            return;
        }
        if (fn <= 0.0 || fn >= 1.0) { if (type == NOTCH) setPass(); else setZero(); return; }
        if (q <= 0.0) { if (type == NOTCH) setZero(); else setPass(); return; }
        const double w0 = kPi * fn;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double c = std::cos (w0);
        const double ia0 = 1.0 / (1.0 + alpha);
        if (type == BP) { b0 = alpha * ia0; b1 = 0; b2 = -alpha * ia0; }
        else            { b0 = ia0; b1 = -2.0 * c * ia0; b2 = ia0; }
        a1 = -2.0 * c * ia0; a2 = (1.0 - alpha) * ia0;
    }

    // RBJ shelving EQ (slope 1)
    void setShelf (bool low, double freq, double gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * clampv (freq, 10.0, sampleRate * 0.45) / sampleRate;
        const double c = std::cos (w0), sn = std::sin (w0);
        const double alpha = sn / 2.0 * std::sqrt (2.0);
        const double sq = 2.0 * std::sqrt (A) * alpha;
        double a0;
        if (low)
        {
            b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq);
            a0 = (A + 1) + (A - 1) * c + sq;        a1 = -2 * ((A - 1) + (A + 1) * c);    a2 = (A + 1) + (A - 1) * c - sq;
        }
        else
        {
            b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
            a0 = (A + 1) - (A - 1) * c + sq;        a1 = 2 * ((A - 1) - (A + 1) * c);      a2 = (A + 1) - (A - 1) * c - sq;
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }

    inline float process (float x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

private:
    void setPass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void setZero() { b0 = b1 = b2 = a1 = a2 = 0; }
};

//==============================================================================
// Linear ADSR, identical to evalEnvState() in the browser synth.
struct EnvState
{
    double a = 0.01, d = 0.2, s = 0.75, r = 0.45;
    double releaseTime = -1.0;
    double releaseLevel = 0.0;

    void init (double A, double D, double S, double R)
    {
        a = std::max (0.004, A);
        d = std::max (0.002, D);
        s = clampv (S, 0.0, 1.0);
        r = std::max (0.012, R);
        releaseTime = -1.0;
    }

    double held (double t) const
    {
        if (t < a) return t / std::max (0.0001, a);
        if (t < a + d) return 1.0 + (s - 1.0) * ((t - a) / std::max (0.0001, d));
        return s;
    }

    double eval (double t) const
    {
        if (releaseTime < 0.0 || t <= releaseTime) return clampv (held (std::max (0.0, t)), 0.0, 1.0);
        const double rx = (t - releaseTime) / std::max (0.0001, r);
        return clampv (releaseLevel * (1.0 - rx), 0.0, 1.0);
    }

    void release (double t)
    {
        if (releaseTime >= 0.0) return;
        releaseLevel = eval (t);
        releaseTime = t;
    }

    bool finished (double t) const { return releaseTime >= 0.0 && t > releaseTime + r; }
};

//==============================================================================
// Simple fractional delay line (linear interpolation), one channel.
struct DelayLine
{
    std::vector<float> buf;
    int w = 0;
    void prepare (int maxSamples) { buf.assign ((size_t) std::max (4, maxSamples + 4), 0.0f); w = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
    // delay in samples (>= 1 when called after push for the same sample => 0 means newest)
    inline float read (double delaySamples) const
    {
        const int size = (int) buf.size();
        delaySamples = clampv (delaySamples, 0.0, (double) (size - 3));
        double rp = (double) w - 1.0 - delaySamples;
        while (rp < 0) rp += size;
        int i0 = (int) rp;
        float f = (float) (rp - i0);
        int i1 = i0 + 1; if (i1 >= size) i1 = 0;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }
};

} // namespace tg
