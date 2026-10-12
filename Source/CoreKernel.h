#pragma once
// The Core oscillator's Kernel engine: six-operator phase-modulation ("FM") synthesis with a free
// modulation matrix. Every operator can modulate every other one and itself (the diagonal is feedback);
// each has a ratio (or a fixed frequency), a waveform, a level and its own DAHDSR envelope. The whole
// kernel runs oversampled (Normal x2, High x4, Eco none) and comes back down through half-band filters,
// so the sidebands a high index makes above 20 kHz do not fold back as aliasing. Unison stacks up to 8
// copies of the kernel, detuned and spread across the stereo field.
//
// Modulation is in radians: an amount of 1 from an operator at full level moves the carrier's phase by
// up to 1 radian (the "modulation index"). Operators are worked out from 6 down to 1, so an operator
// modulating a lower-numbered one (the usual stacks) acts in the same sample; one modulating a higher
// numbered one acts a sample later (at the oversampled rate). Feedback uses the average of the
// operator's last two outputs, which keeps high feedback stable (a noisy buzz rather than a blow-up).
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "HalfBand.h"

namespace tg
{

struct CoreOpSettings
{
    int wave = 0;
    double ratio = 1.0, hz = 440.0, fine = 0.0;   // fine: cents
    bool fixed = false;
    float level = 0.0f, vel = 0.0f, key = 0.0f;
    float dly = 0.0f, att = 0.001f, hold = 0.0f, dec = 0.3f, sus = 1.0f, rel = 0.2f;
    float m[6] {};   // to operator j (m[self] = feedback), radians
    float out = 0.0f;
};

struct CoreSettings
{
    int uni = 1;                // 1..8
    float detune = 12.0f;       // cents (the outer voices are +/- this)
    float spread = 0.5f;
    float bright = 1.0f;        // scales every modulation amount
    float envSpd = 1.0f;        // envelope speed (times are divided by it)
    int quality = 1;            // Eco, Normal (x2), High (x4)
    CoreOpSettings op[6];
};

class CoreKernel
{
public:
    static constexpr int kMaxUni = 8, kOps = 6, kTable = 4096;

    // the waveforms, built once
    struct Tables
    {
        float t[8][kTable + 1];
        Tables()
        {
            const double pi = 3.14159265358979323846;
            for (int w = 0; w < 8; ++w)
            {
                double peak = 0.0;
                for (int i = 0; i <= kTable; ++i)
                {
                    const double x = 2.0 * pi * (double) (i % kTable) / kTable, s = std::sin (x), ph = (double) (i % kTable) / kTable;
                    double v = s;
                    switch (w)
                    {
                        case 1: v = s > 0.0 ? s : 0.0; break;                                     // half sine
                        case 2: v = std::abs (s); break;                                           // abs sine
                        case 3: v = (ph < 0.25 || (ph >= 0.5 && ph < 0.75)) ? std::abs (s) : 0.0; break;   // quarter sine
                        case 4: v = ph < 0.5 ? std::sin (2.0 * x) : 0.0; break;                    // alternating sine
                        case 5: v = ph < 0.5 ? std::abs (std::sin (2.0 * x)) : 0.0; break;         // camel sine
                        case 6: v = std::tanh (3.0 * s) / std::tanh (3.0); break;                  // soft square
                        case 7:                                                                    // soft saw: 16 harmonics, smoothed
                        {
                            v = 0.0;
                            for (int h = 1; h <= 16; ++h)
                            {
                                const double sig = h == 1 ? 1.0 : std::sin (pi * h / 17.0) / (pi * h / 17.0);
                                v += sig * std::sin (h * x) / h;
                            }
                            break;
                        }
                        default: break;
                    }
                    t[w][i] = (float) v;
                    peak = std::max (peak, std::abs (v));
                }
                if (peak > 0.0) for (int i = 0; i <= kTable; ++i) t[w][i] = (float) (t[w][i] / peak);
            }
        }
    };
    static const Tables& tables() { static const Tables tb; return tb; }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        tables();
        hbA.design (17); hbB.design (5);
        clear();
    }

    // a new note: envelopes from the start; phases per Retrigger (0 reset, 1 free, 2 random)
    void start (const CoreSettings& s, int retrig, uint32_t seed, int noteNum, float vel)
    {
        os = s.quality >= 2 ? 4 : (s.quality == 1 ? 2 : 1);
        note = noteNum; velocity = vel;
        uint32_t r = seed * 2654435761u + 12345u;
        for (int u = 0; u < kMaxUni; ++u)
            for (int k = 0; k < kOps; ++k)
            {
                if (retrig == 0) ph[k][u] = u == 0 ? 0u : hashPhase ((uint32_t) (u * 7 + k));
                else if (retrig == 2) { r = r * 1664525u + 1013904223u; ph[k][u] = r; }
                y[k][u] = yp[k][u] = 0.0f;
            }
        for (int k = 0; k < kOps; ++k) { env[k] = 0.0f; stage[k] = 0; stageT[k] = 0; }
        for (auto& d : dn) d.clear();
        released = false;
        started = true;
        first = true;
    }
    bool isStarted() const { return started; }
    void stop() { started = false; }
    void release()
    {
        if (released) return;
        released = true;
        for (int k = 0; k < kOps; ++k) stage[k] = 5;
    }

    // per control block: frequencies, the matrix and the envelope rates (n samples follow)
    void configure (const CoreSettings& s, double noteHz, int n)
    {
        if (! first) snapRamps();   // (the last block's ramps end exactly on their targets)
        nu = std::clamp (s.uni, 1, kMaxUni);
        lanes = nu == 1 ? 1 : (nu == 2 ? 2 : (nu <= 4 ? 4 : 8));
        const double osr = sr * os, toInc = 4294967296.0 / osr, nyq = osr * 0.45;
        for (int u = 0; u < nu; ++u)
        {
            const double off = nu > 1 ? s.detune * (2.0 * u / (nu - 1) - 1.0) : 0.0;
            for (int k = 0; k < kOps; ++k)
            {
                const auto& o = s.op[k];
                double f = o.fixed ? o.hz * std::exp2 (o.fine / 1200.0) : noteHz * o.ratio * std::exp2 ((o.fine + off) / 1200.0);
                f = std::clamp (f, 0.0, nyq);
                inc[k][u] = (uint32_t) (int64_t) std::llround (f * toInc);
            }
        }
        if (nu != panN || s.spread != panSpread)
        {
            panN = nu; panSpread = s.spread;
            const float norm = nu > 1 ? 1.0f / std::sqrt ((float) nu) : 1.0f;
            for (int u = nu; u < kMaxUni; ++u) gL[u] = gR[u] = 0.0f;
            for (int u = 0; u < nu; ++u)
            {
                const float p = nu > 1 ? std::clamp (s.spread * (2.0f * u / (nu - 1) - 1.0f), -1.0f, 1.0f) : 0.0f;
                const float a = (p + 1.0f) * 0.78539816f;
                gL[u] = norm * 1.41421356f * std::cos (a); gR[u] = norm * 1.41421356f * std::sin (a);
            }
        }
        isStereo = nu > 1 && s.spread > 0.0005f;

        // which operators are heard or feed one that is (and have a level)
        bool need[kOps] {};
        for (int k = 0; k < kOps; ++k) need[k] = s.op[k].level > 0.0f && s.op[k].out > 0.0f;
        for (int pass = 0; pass < kOps; ++pass)
            for (int j = 0; j < kOps; ++j)
                if (! need[j] && s.op[j].level > 0.0f)
                    for (int k = 0; k < kOps; ++k)
                        if (k != j && need[k] && s.op[j].m[k] > 0.0f) { need[j] = true; break; }
        // operator gains (level x velocity x key scaling) and the matrix, ramped across the block
        const float invN = 1.0f / (float) std::max (1, n);
        nAct = 0;
        for (int k = kOps - 1; k >= 0; --k)
        {
            const auto& o = s.op[k];
            const float vg = 1.0f - o.vel + o.vel * velocity;
            const float kg = std::clamp (std::exp2 (o.key * (float) (note - 60) / 12.0f), 0.0f, 4.0f);
            const float g = need[k] ? o.level * vg * kg : 0.0f;
            const float outT = need[k] ? o.out : 0.0f;
            if (first) { gain[k] = g; outG[k] = outT; }
            gainStep[k] = (g - gain[k]) * invN; gainTarget[k] = g;
            outStep[k] = (outT - outG[k]) * invN; outTarget[k] = outT;
            for (int j = 0; j < kOps; ++j)
            {
                const float mt = need[k] && need[j] ? o.m[j] * s.bright : 0.0f;   // k modulates j
                if (first) mat[k][j] = mt;
                matStep[k][j] = (mt - mat[k][j]) * invN; matTarget[k][j] = mt;
            }
            if (need[k] || gain[k] > 0.0f) act[nAct++] = k;   // (descending: 6 first)
            // the envelope's rates
            const float spd = std::max (0.05f, s.envSpd);
            const double fs = sr / spd;
            dlyN[k] = (int) std::lround (o.dly * fs);
            attInc[k] = o.att > 0.0f ? (float) (1.0 / (o.att * fs)) : 2.0f;
            holdN[k] = (int) std::lround (o.hold * fs);
            decK[k] = (float) std::exp (-6.907755 / std::max (1.0, o.dec * fs));
            relK[k] = (float) std::exp (-6.907755 / std::max (1.0, o.rel * fs));
            sus[k] = std::clamp (o.sus, 0.0f, 1.0f);
            wave[k] = std::clamp (o.wave, 0, 7);
        }
        ramping = false;
        for (int k = 0; k < kOps; ++k)
        {
            ramping |= gainStep[k] != 0.0f || outStep[k] != 0.0f;
            for (int j = 0; j < kOps; ++j) ramping |= matStep[k][j] != 0.0f;
        }
        // the modulators of each operator (with a non-zero amount, now or by the block's end)
        for (int k = 0; k < kOps; ++k)
        {
            nMods[k] = 0;
            for (int j = 0; j < kOps; ++j)
                if (j != k && (mat[j][k] != 0.0f || matTarget[j][k] != 0.0f)) mods[k][nMods[k]++] = j;
        }
        first = false;
    }

    // one output sample (stereo when unison is spread)
    inline void tick (float& outL, float& outR)
    {
        // the envelopes and ramps, once per sample
        float a[kOps];
        for (int q = 0; q < nAct; ++q)
        {
            const int k = act[q];
            a[k] = stepEnv (k) * gain[k];
        }
        float accL[4] {}, accR[4] {};
        switch (lanes)
        {
            case 1:  run<1> (a, accL, accR); break;
            case 2:  run<2> (a, accL, accR); break;
            case 4:  run<4> (a, accL, accR); break;
            default: run<8> (a, accL, accR); break;
        }
        // ramps to the block's targets (only while something is moving)
        if (ramping) for (int q = 0; q < nAct; ++q)
        {
            const int k = act[q];
            gain[k] += gainStep[k]; outG[k] += outStep[k];
            for (int i = 0; i < nMods[k]; ++i) { const int j = mods[k][i]; mat[j][k] += matStep[j][k]; }
            mat[k][k] += matStep[k][k];
        }
        // back down to the real rate
        if (os == 1) { outL = accL[0]; outR = accR[0]; }
        else if (os == 2)
        {
            outL = dn[0].down (hbA, accL[0], accL[1]);
            outR = isStereo || nu > 1 ? dn[1].down (hbA, accR[0], accR[1]) : outL;
        }
        else
        {
            const float l0 = dn[2].down (hbB, accL[0], accL[1]), l1 = dn[2].down (hbB, accL[2], accL[3]);
            outL = dn[0].down (hbA, l0, l1);
            if (isStereo || nu > 1)
            {
                const float r0 = dn[3].down (hbB, accR[0], accR[1]), r1 = dn[3].down (hbB, accR[2], accR[3]);
                outR = dn[1].down (hbA, r0, r1);
            }
            else outR = outL;
        }
    }

    float envelope (int k) const { return env[k]; }
    // the loudest envelope among the operators that are heard (0 once they have all finished)
    float heardLevel() const
    {
        if (! started) return 0.0f;
        float m = 0.0f;
        for (int q = 0; q < nAct; ++q) { const int k = act[q]; if (outTarget[k] > 0.0f || outG[k] > 0.0f) m = std::max (m, env[k] * std::max (gain[k], gainTarget[k])); }
        return m;
    }
    bool stereo() const { return isStereo; }
    int oversampling() const { return os; }
    int activeOperators() const { return nAct; }

private:
    // the oversampled samples of one output sample, N unison lanes at a time (op-major arrays, the lanes
    // innermost and fixed in number, so the arithmetic runs as vector instructions; lanes past the unison
    // count have no pan gain)
    template <int N>
    inline void run (const float* a, float* accL, float* accR)
    {
        const auto& tb = tables();
        constexpr float inv2pi = 0.15915494309189535f, toTurns = 2.3283064365386963e-10f;   // 1 / (2 pi), 2^-32
        for (int s = 0; s < os; ++s)
        {
            float o[N] {};
            for (int q = 0; q < nAct; ++q)
            {
                const int k = act[q];
                float x[N];
                const float fb = 0.5f * mat[k][k];
                float* __restrict yk = y[k];
                float* __restrict ypk = yp[k];
                for (int u = 0; u < N; ++u) x[u] = fb * (yk[u] + ypk[u]);
                for (int i = 0; i < nMods[k]; ++i)
                {
                    const int j = mods[k][i];
                    const float m = mat[j][k];
                    const float* __restrict yj = y[j];
                    for (int u = 0; u < N; ++u) x[u] += m * yj[u];
                }
                // the phase in turns, -0.5..0.5 (the accumulator stays exact; the modulation is added in float)
                uint32_t* __restrict pk = ph[k];
                const uint32_t* __restrict ik = inc[k];
                for (int u = 0; u < N; ++u)
                {
                    float v = (float) (int32_t) pk[u] * toTurns + x[u] * inv2pi;
                    v -= (float) (int32_t) (v + (v >= 0.0f ? 0.5f : -0.5f));
                    x[u] = v;
                    pk[u] += ik[u];
                }
                const float ak = a[k], ok = outG[k];
                float w[N];
                if (wave[k] == 0)
                {
                    for (int u = 0; u < N; ++u)
                    {
                        // sin (2 pi v): folded into a quarter cycle, then the series to the 11th power (error below 1e-7)
                        const float v = x[u];
                        const float h = v > 0.25f ? 0.5f - v : (v < -0.25f ? -0.5f - v : v);
                        const float z = h * 6.2831853071795865f, z2 = z * z;
                        w[u] = z * (1.0f + z2 * (-1.6666667e-1f + z2 * (8.3333333e-3f + z2 * (-1.9841270e-4f + z2 * (2.7557319e-6f + z2 * -2.5052108e-8f)))));
                    }
                }
                else
                {
                    const float* tw = tb.t[wave[k]];
                    for (int u = 0; u < N; ++u)
                    {
                        const float p01 = (x[u] < 0.0f ? x[u] + 1.0f : x[u]) * (float) kTable;
                        const int idx = std::min ((int) p01, kTable - 1);
                        const float fr = p01 - (float) idx;
                        w[u] = tw[idx] + (tw[idx + 1] - tw[idx]) * fr;
                    }
                }
                for (int u = 0; u < N; ++u) { ypk[u] = yk[u]; yk[u] = w[u] * ak; o[u] += ok * yk[u]; }
            }
            float sL = 0.0f, sR = 0.0f;
            for (int u = 0; u < N; ++u) { sL += o[u] * gL[u]; sR += o[u] * gR[u]; }
            accL[s] = sL; accR[s] = sR;
        }
    }

    void snapRamps()
    {
        for (int k = 0; k < kOps; ++k)
        {
            gain[k] = gainTarget[k]; outG[k] = outTarget[k];
            for (int j = 0; j < kOps; ++j) mat[k][j] = matTarget[k][j];
        }
    }

    static uint32_t hashPhase (uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
        return x;
    }
    void clear()
    {
        for (auto& d : dn) d.clear();
        for (int u = 0; u < kMaxUni; ++u) for (int k = 0; k < kOps; ++k) { ph[k][u] = 0; y[k][u] = yp[k][u] = 0.0f; }
        for (int k = 0; k < kOps; ++k) { env[k] = 0.0f; stage[k] = 0; stageT[k] = 0; gain[k] = outG[k] = 0.0f; }
        started = false;
    }
    // stages: 0 delay, 1 attack, 2 hold, 3 decay / sustain, 5 release
    inline float stepEnv (int k)
    {
        float& e = env[k];
        switch (stage[k])
        {
            case 0:
                if (stageT[k]++ >= dlyN[k]) { stage[k] = 1; stageT[k] = 0; }
                else return e;
                [[fallthrough]];
            case 1:
                e += attInc[k];
                if (e >= 1.0f) { e = 1.0f; stage[k] = 2; stageT[k] = 0; }
                return e;
            case 2:
                if (stageT[k]++ >= holdN[k]) { stage[k] = 3; stageT[k] = 0; }
                return e;
            case 3:
                e = sus[k] + (e - sus[k]) * decK[k];
                return e;
            default:
                e *= relK[k];
                if (e < 1.0e-5f) e = 0.0f;
                return e;
        }
    }

    double sr = 48000.0;
    int os = 2, nu = 1, lanes = 1, note = 60;
    float velocity = 1.0f;
    bool released = false, started = false, first = true, isStereo = false, ramping = false;
    alignas (32) uint32_t ph[kOps][kMaxUni] {};
    alignas (32) uint32_t inc[kOps][kMaxUni] {};
    alignas (32) float y[kOps][kMaxUni] {};
    alignas (32) float yp[kOps][kMaxUni] {};
    alignas (32) float gL[kMaxUni] {};
    alignas (32) float gR[kMaxUni] {};
    int panN = -1; float panSpread = -1.0f;
    float env[kOps] {}, attInc[kOps] {}, decK[kOps] {}, relK[kOps] {}, sus[kOps] {};
    int stage[kOps] {}, stageT[kOps] {}, dlyN[kOps] {}, holdN[kOps] {}, wave[kOps] {};
    float gain[kOps] {}, gainStep[kOps] {}, gainTarget[kOps] {}, outG[kOps] {}, outStep[kOps] {}, outTarget[kOps] {};
    float mat[kOps][kOps] {}, matStep[kOps][kOps] {}, matTarget[kOps][kOps] {};
    int act[kOps] {}, nAct = 0, mods[kOps][kOps] {}, nMods[kOps] {};
    hb::HalfBand hbA, hbB;
    hb::Down dn[4];
};

} // namespace tg
