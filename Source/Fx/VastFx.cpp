#include "VastFx.h"

namespace tg
{

double syncSeconds (const ChoiceList& list, int idx, double tempo, double fallback);   // Engine.cpp

namespace
{
    struct VsMode
    {
        double scale[VastReverb::kLines];
        float warpMul, fbMin, lpMul, modMul, sat;
        bool shimmer, bloom;
    };
    const VsMode kModes[8] = {
        { { 0.62, 0.71, 0.79, 0.87, 0.94, 1.03, 1.13, 1.27 }, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, false, false },     // Vast Hall
        { { 1.0, 1.01, 0.99, 1.02, 0.98, 1.015, 0.985, 1.005 }, 0.35f, 0.0f, 0.8f, 0.6f, 0.0f, false, false },  // Echo Cascade
        { { 0.74, 0.85, 0.95, 1.04, 1.13, 1.24, 1.36, 1.52 }, 1.0f, 0.0f, 1.2f, 1.0f, 0.0f, true, false },      // Shimmer Swell
        { { 0.99, 1.14, 1.26, 1.39, 1.5, 1.65, 1.81, 2.03 }, 1.0f, 0.0f, 0.25f, 1.5f, 0.0f, false, false },     // Dark Cloud
        { { 0.68, 0.78, 0.87, 0.96, 1.03, 1.13, 1.24, 1.4 }, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, false, true },       // Gated Bloom
        { { 1.12, 1.28, 1.42, 1.57, 1.69, 1.85, 2.03, 2.29 }, 1.0f, 0.985f, 0.9f, 1.2f, 0.0f, false, false },   // Endless
        { { 0.05, 0.063, 0.071, 0.083, 0.097, 0.11, 0.127, 0.139 }, 0.3f, 0.0f, 1.0f, 0.2f, 0.0f, false, false }, // Comb Galaxy
        { { 0.66, 0.74, 0.83, 0.91, 0.99, 1.08, 1.19, 1.31 }, 1.0f, 0.0f, 0.6f, 4.0f, 0.5f, false, false } };   // Tape Nebula
    inline float softSat (float x, float amt) { return amt > 0.0f ? std::tanh (x * (1.0f + 2.0f * amt)) / (1.0f + 2.0f * amt) : x; }   // (unity for quiet sounds: tape squash on loud ones)
    // the loop's bound: untouched below 1 (an endless hold keeps its level), a soft knee above it (never past 2)
    inline float bound (float x)
    {
        const float a = std::abs (x);
        if (a <= 1.0f) return x;
        const float e = a - 1.0f, y = 1.0f + e / (1.0f + e);
        return x < 0.0f ? -y : y;
    }
    constexpr int kSub = 16;
}

void VastReverb::prepare (double s)
{
    sr = s;
    for (auto& l : line) l.prepare ((int) (2.45 * sr) + 64);
    static const double dl[4] = { 0.00296, 0.00223, 0.00789, 0.00577 };
    for (int k = 0; k < 4; ++k) { diffLen[k] = std::round (dl[k] * sr); diff[k].prepare ((int) diffLen[k] + 8); }
    shim.prepare ((int) (0.2 * sr));
    reset();
}

void VastReverb::reset()
{
    for (auto& l : line) l.reset();
    for (auto& d : diff) d.reset();
    shim.reset();
    for (int k = 0; k < kLines; ++k) { lp[k] = hp[k] = 0.0f; lenS[k] = -1.0; lfoPh[k] = k / (double) kLines; modPrev[k] = 0.0; }
    freezeS = 0.0f; warpS = fbS = densS = widthS = -1.0f;
    bloomEnv = bloomGain = inEnv = 0.0f; curMode = -1; shimPh = 0.0; wowPh = 0.0f;
}

void VastReverb::process (float* L, float* R, int n, const float* v, const FxContext& ctx, const float* duck)
{
    const int mode = clampv ((int) std::lround (v[P_vsMode]), 0, 7);
    const VsMode& M = kModes[mode];
    const double baseS = clampv (syncSeconds (kListDelaySync, (int) std::lround (v[P_vsSync]), ctx.tempo, clampv (v[P_vsDelay], 10.0f, 1000.0f) * 0.001), 0.01, 1.0);
    const float warp = clampv (v[P_vsWarp], 0.0f, 1.0f) * M.warpMul;
    const float fbT = std::max (M.fbMin, clampv (v[P_vsFeedback], 0.0f, 1.0f));
    const float dens = clampv (v[P_vsDensity], 0.0f, 1.0f), width = clampv (v[P_vsWidth], 0.0f, 1.0f);
    const bool freeze = v[P_vsFreeze] > 0.5f;
    const float lpC = 1.0f - (float) std::exp (-2.0 * kPi * clampv (v[P_vsHighCut] * M.lpMul, 200.0f, 20000.0f) / sr);
    const float hpC = 1.0f - (float) std::exp (-2.0 * kPi * clampv (v[P_vsLowCut], 20.0f, 1000.0f) / sr);
    const double modRate = clampv (v[P_vsModRate], 0.01f, 5.0f);
    const double modDepth = clampv (v[P_vsModDepth], 0.0f, 1.0f) * M.modMul * 0.004 * sr;
    if (mode != curMode)
    {
        // a new mode: its lines start at their new lengths (the network keeps ringing; it doesn't restart)
        curMode = mode;
        for (int k = 0; k < kLines; ++k) lenS[k] = -1.0;
    }
    if (warpS < 0.0f) { warpS = warp; fbS = fbT; densS = dens; widthS = width; }
    const double maxLen = 2.4 * sr;
    double target[kLines];
    for (int k = 0; k < kLines; ++k)
    {
        target[k] = std::round (clampv (baseS * M.scale[k] * sr, 16.0, maxLen - modDepth - 8.0));   // (whole samples: a held tail isn't dulled by in-between reads)
        if (lenS[k] < 0.0) lenS[k] = target[k];
    }
    const float fzC = smoothCoef (1.0 / sr, 0.03), bUp = smoothCoef (1.0 / sr, 0.6), bDown = smoothCoef (1.0 / sr, 0.12);
    // (Warp turns each pair of lines into each other - 0: none, a ring of echoes; 1: all into all, a dense wash - as a rotation, so it keeps the energy)
    for (int i0 = 0; i0 < n; i0 += kSub)
    {
        const int m = std::min (kSub, n - i0);
        const float cs = smoothCoef ((double) m / sr, 0.05), cl = smoothCoef ((double) m / sr, 0.25);
        warpS += (warp - warpS) * cs; fbS += (fbT - fbS) * cs; densS += (dens - densS) * cs; widthS += (width - widthS) * cs;
        double modNext[kLines], len0[kLines];
        wowPh += (float) (0.7 * m / sr); if (wowPh > 1.0f) wowPh -= 1.0f;
        for (int k = 0; k < kLines; ++k)
        {
            len0[k] = lenS[k];
            lenS[k] += (target[k] - lenS[k]) * cl;   // (a new delay time glides, like tape: no clicks)
            lfoPh[k] += modRate * (1.0 + 0.13 * k) * m / sr; lfoPh[k] -= std::floor (lfoPh[k]);
            modNext[k] = (1.0 - freezeS) * (modDepth * (0.5 + 0.5 * std::sin (2.0 * kPi * lfoPh[k]))
                       + (M.sat > 0.0f ? modDepth * 0.5 * std::sin (2.0 * kPi * (wowPh + 0.1 * k)) : 0.0));   // (a frozen tail holds still)
        }
        const float g = freeze ? 1.0f : std::min (0.9995f, 0.15f + 0.85f * std::sqrt (fbS));
        const float c = std::cos (warpS * 0.78539816f), sn = std::sin (warpS * 0.78539816f);
        const float ap = 0.45f + 0.3f * densS;
        for (int j = 0; j < m; ++j)
        {
            const int i = i0 + j;
            const double u = (double) (j + 1) / m;
            freezeS += ((freeze ? 1.0f : 0.0f) - freezeS) * fzC;
            const float inL = L[i] * (1.0f - freezeS), inR = R[i] * (1.0f - freezeS);
            // the input: diffused (Density) and split over the lines
            float x = 0.5f * (inL + inR);
            for (int d = 0; d < 4; ++d)
            {
                const float z = diff[d].read (diffLen[d] - 1.0);
                const float w = x + ap * z;
                diff[d].push (w);
                x = z - ap * w;
            }
            const float side = 0.5f * (inL - inR);
            float y[kLines];
            for (int k = 0; k < kLines; ++k)
                y[k] = line[k].read (len0[k] + (lenS[k] - len0[k]) * u + modPrev[k] + (modNext[k] - modPrev[k]) * u - 1.0);
            // three stages of rotations between pairs (at Warp 1, a Hadamard: all into all), then each line into the next
            float h[kLines];
            for (int k = 0; k < kLines; ++k) h[k] = y[k];
            for (int len = 1; len < kLines; len <<= 1)
                for (int a = 0; a < kLines; a += 2 * len)
                    for (int b = a; b < a + len; ++b) { const float p = h[b], q = h[b + len]; h[b] = c * p + sn * q; h[b + len] = sn * p - c * q; }
            float shimIn = 0.0f;
            if (M.shimmer)
            {
                // an octave up: two read heads sweeping a 60 ms window at twice the speed, crossfaded
                shim.push (0.25f * (y[0] + y[3] + y[5] + y[6]));
                const double W = 0.06 * sr;
                shimPh += 1.0 / W; if (shimPh >= 1.0) shimPh -= 1.0;
                const double p1 = shimPh, p2 = shimPh + 0.5 - std::floor (shimPh + 0.5);
                const float w1 = (float) std::sin (kPi * p1), w2 = (float) std::sin (kPi * p2);
                shimIn = (shim.read (W * (1.0 - p1)) * w1 * w1 + shim.read (W * (1.0 - p2)) * w2 * w2) * 0.3f;
            }
            for (int k = 0; k < kLines; ++k)
            {
                const float mixed = h[(k + kLines - 1) % kLines];
                lp[k] += (mixed - lp[k]) * lpC;
                hp[k] += (lp[k] - hp[k]) * hpC;
                // (frozen: the filters step aside, so the held sound keeps its level and colour)
                float o = ((lp[k] - hp[k]) + (mixed - (lp[k] - hp[k])) * freezeS) * g;
                if (M.sat > 0.0f) o = softSat (o, M.sat * (1.0f - freezeS));
                const float inj = (k < 4 ? x + side : x - side) * ((k & 1) ? -0.35f : 0.35f) * (warpS < 0.5f && k > 0 ? 0.6f : 1.0f);
                line[k].push (bound (o) + inj + (k == 0 || k == 5 ? shimIn * (1.0f - freezeS) : 0.0f));   // (bounded: an endless hold stays below clipping)
            }
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kLines; ++k) { if (k & 1) wr += y[k] * ((k & 2) ? -1.0f : 1.0f); else wl += y[k] * ((k & 2) ? -1.0f : 1.0f); }
            wl *= 1.3f; wr *= 1.3f;
            const float mid = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * widthS;
            wl = mid + sd; wr = mid - sd;
            if (M.bloom)
            {
                // Gated Bloom: the wet swells in while you play and closes quickly when you stop
                const float a = std::max (std::abs (inL), std::abs (inR));
                inEnv += (a - inEnv) * (a > inEnv ? 0.01f : 0.0005f);
                const float tgt = inEnv > 0.003f ? 1.0f : 0.0f;
                bloomGain += (tgt - bloomGain) * (tgt > bloomGain ? bUp : bDown);
                wl *= bloomGain; wr *= bloomGain;
            }
            const float dk = duck != nullptr ? duck[i] : 1.0f;
            L[i] += wl * dk; R[i] += wr * dk;
        }
        for (int k = 0; k < kLines; ++k) modPrev[k] = modNext[k];
    }
}

} // namespace tg
