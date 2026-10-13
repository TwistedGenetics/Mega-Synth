#include "SpaceFx.h"
#include "../Registry.h"

namespace tg
{

double syncSeconds (const ChoiceList& list, int idx, double tempo, double fallback);   // Engine.cpp

namespace
{
    constexpr float kDubOut = 1.585f;   // (v0.1.74: the Dub Delay's repeats 4 dB louder; the loop itself is unchanged)
    inline float softLimit (float x, float ceiling) { return ceiling * std::tanh (x / ceiling); }
    inline uint32_t xorshift (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float noise (uint32_t& s) { return (float) (xorshift (s) & 0xffffff) / 8388608.0f - 1.0f; }
    inline float tri (double ph) { const double p = ph - std::floor (ph); return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); }
}

//==============================================================================
// Dub Delay
void DubDelay::prepare (double s) { sr = s; for (auto& d : dl) d.prepare ((int) (sr * 2.2)); reset(); }
void DubDelay::reset()
{
    for (auto& d : dl) d.reset();
    for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset();
    timeS = -1.0f; sendS = 0.0f; latched = false; lastHeld = false;
}

void DubDelay::process (float* L, float* R, int n, const float* v, const FxContext& ctx, bool throwHeld, const float* duck)
{
    const float t = (float) clampv (syncSeconds (kListDelaySync, (int) std::lround (v[P_sxDubSync]), ctx.tempo, v[P_sxDubTime]), 0.01, 2.0);
    if (timeS < 0.0f) timeS = t;
    const float fb = clampv (v[P_sxDubFb], 0.0f, 1.1f);
    const float fHp = clampv (v[P_sxDubHP], 20.0f, 2000.0f), fLp = clampv (v[P_sxDubLP], 500.0f, 18000.0f), res = clampv (v[P_sxDubRes], 0.0f, 1.0f);
    if (fHp != lastHp || fLp != lastLp || res != lastRes)
    {
        lastHp = fHp; lastLp = fLp; lastRes = res;
        const double q = 0.707 + 5.0 * res;
        for (int c = 0; c < 2; ++c) { hp[c].set (fHp, 0.707, sr); lp[c].set (fLp, q, sr); }
        // (the resonance's peak taken back to unity: the loop's gain never goes above Feedback, so below 100% it always dies away)
        lpComp = (float) (1.0 / std::max (1.0, q / std::sqrt (std::max (1.0e-6, 1.0 - 1.0 / (4.0 * q * q)))));
    }
    const float drive = clampv (v[P_sxDubDrive], 0.0f, 1.0f), dk = 1.0f + 5.0f * drive;
    const float w = clampv (v[P_sxDubWidth], 0.0f, 1.0f), wa = 0.5f + 0.5f * w, wb = 0.5f - 0.5f * w;
    const int mode = clampv ((int) std::lround (v[P_sxDubMode]), 0, 2);
    if (mode == 1 && throwHeld && ! lastHeld) latched = ! latched;
    lastHeld = throwHeld;
    const float sendT = mode == 2 ? 1.0f : (mode == 0 ? (throwHeld ? 1.0f : 0.0f) : (latched ? 1.0f : 0.0f));
    const float cs = smoothCoef (1.0 / sr, 0.004), ts = smoothCoef (1.0 / sr, 0.06);
    auto fbPath = [&] (float y, int c)
    {
        float l, b, h;
        hp[c].tick (y, l, b, h); float x = h;
        lp[c].tick (x, l, b, h); x = l * lpComp;
        if (drive > 0.0f) x += drive * (std::tanh (dk * x) / dk - x);   // (saturation: small signals pass unchanged)
        return softLimit (x, 1.2f);   // (above 100% the repeats build up, and stay bounded)
    };
    for (int i = 0; i < n; ++i)
    {
        sendS += (sendT - sendS) * cs;
        timeS += (t - timeS) * ts;
        const double d = std::max (1.0, (double) timeS * sr - 1.0);
        const float yL = dl[0].read (d), yR = dl[1].read (d);
        const float in = 0.5f * (L[i] + R[i]) * sendS;
        const float fL = fbPath (yL, 0) * fb, fR = fbPath (yR, 1) * fb;
        dl[0].push (in + fR); dl[1].push (fL);   // (ping-pong: in on the left, then across and back)
        const float g = duck != nullptr ? duck[i] : 1.0f;
        L[i] += (yL * wa + yR * wb) * g * kDubOut;
        R[i] += (yR * wa + yL * wb) * g * kDubOut;
    }
}

//==============================================================================
// Spring Reverb
void SpringReverb::prepare (double s)
{
    sr = s;
    static const double base[kSprings] = { 0.0371, 0.0437, 0.0533 };
    for (int c = 0; c < 2; ++c)
        for (int j = 0; j < kSprings; ++j)
        {
            spr[c][j].baseDelay = std::round (base[j] * (c == 0 ? 1.0 : 1.071) * sr);   // (whole samples: a frozen loop doesn't lose its highs)
            spr[c][j].loop.prepare ((int) (spr[c][j].baseDelay + 0.01 * sr));
        }
    reset();
}

void SpringReverb::reset()
{
    for (auto& ch : spr) for (auto& p : ch)
    {
        p.loop.reset(); p.apPos = 0; p.lpS = 0.0f; p.dcX = p.dcY = 0.0f;
        for (auto& a : p.apX) std::fill (std::begin (a), std::end (a), 0.0f);
        for (auto& a : p.apY) std::fill (std::begin (a), std::end (a), 0.0f);
    }
    lfoPh = 0.0; freezeS = 0.0f; inS = 1.0f; lastKick = false; kickLeft = 0; rng = 0x5eed5u; thumpPh = 0.0f;
    hpX[0] = hpX[1] = hpY[0] = hpY[1] = 0.0f;
}

void SpringReverb::process (float* L, float* R, int n, const float* v, const FxContext&, const float* duck)
{
    const float tension = clampv (v[P_sxSprTension], 0.0f, 1.0f);
    const float a = 0.45f + 0.42f * tension;                 // the all-passes' coefficient: more = a longer, wetter drip
    const int K = tension < 0.5f ? 2 : 3;                    // (stretched by 2 or 3 samples)
    const float decay = clampv (v[P_sxSprDecay], 0.3f, 6.0f);
    const float toneC = 1.0f - (float) std::exp (-2.0 * kPi * clampv (v[P_sxSprTone], 500.0f, 12000.0f) / sr);
    const int ns = clampv ((int) std::lround (v[P_sxSprCount]), 0, 1) + 2;
    const bool freeze = v[P_sxSprFreeze] > 0.5f;
    const bool kick = v[P_sxSprKick] > 0.5f;
    if (kick && ! lastKick) { kickLeft = (int) (0.05 * sr); thumpPh = 0.0f; }
    lastKick = kick;
    const float fc = smoothCoef (1.0 / sr, 0.03);
    const float hpC = (float) std::exp (-2.0 * kPi * 90.0 / sr);
    const double lfoInc = 0.7 / sr;
    float g[kSprings];
    for (int j = 0; j < ns; ++j) g[j] = (float) std::pow (10.0, -3.0 * (spr[0][j].baseDelay / sr) / decay);
    const float outScale = 2.22f / std::sqrt ((float) ns);   // (v0.1.72: about 8 dB up; v0.1.74: 4 dB more)
    for (int i = 0; i < n; ++i)
    {
        freezeS += ((freeze ? 1.0f : 0.0f) - freezeS) * fc;
        const float inGain = 0.35f * (1.0f - freezeS);
        // Kick the tank: a knock of noise and a low thump, straight into the springs
        float knock = 0.0f;
        if (kickLeft > 0)
        {
            const float e = (float) kickLeft / (float) (0.05 * sr);
            knock = 0.9f * noise (rng) * e * e + 1.2f * std::sin (thumpPh) * e;
            thumpPh += (float) (2.0 * kPi * 52.0 / sr);
            --kickLeft;
        }
        lfoPh += lfoInc; if (lfoPh >= 1.0) lfoPh -= 1.0;
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            const float x = io[c][i] * inGain + knock;
            float sum = 0.0f;
            for (int j = 0; j < ns; ++j)
            {
                auto& p = spr[c][j];
                const double mod = (1.0f - freezeS) * 0.0003 * sr * std::sin (2.0 * kPi * (lfoPh + 0.31 * j + 0.17 * c));
                const float y = p.loop.read (p.baseDelay + mod);
                sum += y;
                // the chirp: stretched all-passes (each H = (a + z^-K) / (1 + a z^-K))
                float z = y;
                const int rp = (p.apPos + 4 - K) & 3;
                for (int q = 0; q < kAp; ++q)
                {
                    const float out = a * z + p.apX[q][rp] - a * p.apY[q][rp];
                    p.apX[q][p.apPos] = z; p.apY[q][p.apPos] = out;
                    z = out;
                }
                // tone in the loop (transparent when frozen), then the loop's loss (none when frozen)
                const float tc = toneC + (1.0f - toneC) * freezeS;
                p.lpS += (z - p.lpS) * tc;
                const float gl = g[j] + (0.9998f - g[j]) * freezeS;
                p.loop.push (softLimit (x + p.lpS * gl, 2.0f));
            }
            for (int j = 0; j < ns; ++j) { auto& p = spr[c][j]; p.apPos = (p.apPos + 1) & 3; }
            // (springs are thin: the lows high-passed out)
            const float hx = sum * outScale;
            const float hy = hpC * (hpY[c] + hx - hpX[c]);
            hpX[c] = hx; hpY[c] = hy;
            io[c][i] += hy * (duck != nullptr ? duck[i] : 1.0f);
        }
    }
}

//==============================================================================
// Gated Reverb
void GatedReverb::prepare (double s)
{
    sr = s;
    for (auto& l : line) l.prepare ((int) (0.09 * 1.3 * sr) + 8);
    for (auto& p : pre) p.prepare ((int) (0.11 * sr));
    reset();
}

void GatedReverb::reset()
{
    for (auto& l : line) l.reset();
    for (auto& p : pre) p.reset();
    std::fill (std::begin (lp), std::end (lp), 0.0f);
    gate = 0.0f; levelEnv = 0.0f; openLeft = 0; armed = true; freezeS = 0.0f;
}

void GatedReverb::process (float* L, float* R, int n, const float* v, const FxContext& ctx, uint32_t hits, const bool* scHit, const float* duck)
{
    static const double lens[kLines] = { 0.0297, 0.0371, 0.0411, 0.0437, 0.0533, 0.0599, 0.0671, 0.0793 };
    const float size = clampv (v[P_sxGateSize], 0.0f, 1.0f);
    const double scaleL = 0.35 + 0.9 * size;
    const double t60 = 1.2 + 2.0 * size;
    const float toneC = 1.0f - (float) std::exp (-2.0 * kPi * clampv (v[P_sxGateTone], 1000.0f, 18000.0f) / sr);
    const double preS = clampv (v[P_sxGatePre], 0.0f, 100.0f) * 0.001 * sr;
    const int syncIdx = clampv ((int) std::lround (v[P_sxGateSync]), 0, 5);
    const double lenSec = syncIdx > 0 ? kSxGateSyncBeats[syncIdx] * 60.0 / std::max (20.0, ctx.tempo) : clampv (v[P_sxGateLen], 50.0f, 1000.0f) * 0.001;
    const int lenN = std::max (1, (int) (lenSec * sr));
    const bool curved = v[P_sxGateShape] > 0.5f;
    const int trig = clampv ((int) std::lround (v[P_sxGateTrig]), 0, 8);
    const float thr = std::pow (10.0f, clampv (v[P_sxGateThr], -60.0f, 0.0f) / 20.0f);
    const bool freeze = v[P_sxGateFreeze] > 0.5f;
    static const uint32_t trigBit[9] = { 0, SH_Kick, SH_Snare, SH_Hits, SH_Main, SH_Layer, SH_Sc, SH_Notes, SH_Note };
    const bool blockHit = trig > 0 && trig != 6 && (hits & trigBit[trig]) != 0;
    float fbG[kLines]; double dl[kLines];
    for (int k = 0; k < kLines; ++k) { dl[k] = std::round (lens[k] * scaleL * sr); fbG[k] = (float) std::pow (10.0, -3.0 * lens[k] * scaleL / t60); }
    const float fc = smoothCoef (1.0 / sr, 0.03);
    const float envA = smoothCoef (1.0 / sr, 0.0005), envR = smoothCoef (1.0 / sr, 0.03);
    const float gOpen = smoothCoef (1.0 / sr, 0.0003), gClose = smoothCoef (1.0 / sr, curved ? 0.006 : 0.002);
    for (int i = 0; i < n; ++i)
    {
        freezeS += ((freeze ? 1.0f : 0.0f) - freezeS) * fc;
        // the trigger
        bool fire = false;
        if (trig == 0)
        {
            const float a = std::max (std::abs (L[i]), std::abs (R[i]));
            levelEnv += (a - levelEnv) * (a > levelEnv ? envA : envR);
            if (armed && levelEnv > thr) { fire = true; armed = false; }
            else if (! armed && levelEnv < thr * 0.5f) armed = true;
        }
        else if (trig == 6) fire = scHit != nullptr && scHit[i];
        else fire = blockHit && i == 0;
        if (fire) { openLeft = lenN; openLen = lenN; }
        // the network
        float in[2] = { L[i] * (1.0f - freezeS), R[i] * (1.0f - freezeS) };
        float pd[2];
        for (int c = 0; c < 2; ++c) { pre[c].push (in[c]); pd[c] = pre[c].read (std::max (0.0, preS)); }
        float y[kLines];
        for (int k = 0; k < kLines; ++k) y[k] = line[k].read (dl[k] - 1.0);
        // (Hadamard mixing, 8 x 8, normalised)
        float h[kLines];
        for (int k = 0; k < kLines; ++k) h[k] = y[k];
        for (int len = 1; len < kLines; len <<= 1)
            for (int a = 0; a < kLines; a += 2 * len)
                for (int b = a; b < a + len; ++b) { const float u = h[b], w = h[b + len]; h[b] = u + w; h[b + len] = u - w; }
        const float nrm = 0.35355339f;
        const float tc = toneC + (1.0f - toneC) * freezeS;
        for (int k = 0; k < kLines; ++k)
        {
            lp[k] += (h[k] * nrm - lp[k]) * tc;
            const float gk = fbG[k] + (0.99995f - fbG[k]) * freezeS;
            line[k].push (softLimit (lp[k] * gk + pd[k & 1] * 0.5f, 2.0f));
        }
        const float outL = (y[0] - y[2] + y[4] - y[6]) * 0.5f, outR = (y[1] - y[3] + y[5] - y[7]) * 0.5f;
        // the gate: open for Length after each trigger (Curved: falls away over it); always open when frozen
        float target;
        if (openLeft > 0) { const float p = (float) openLeft / (float) openLen; target = curved ? p * p : 1.0f; --openLeft; }
        else target = 0.0f;
        target = std::max (target, freezeS);
        gate += (target - gate) * (target > gate ? gOpen : gClose);
        const float g = gate * (duck != nullptr ? duck[i] : 1.0f);
        L[i] += outL * g; R[i] += outR * g;
    }
}

//==============================================================================
// Plate Reverb (Dattorro, "Effect Design Part 1", 1997; his delays are for 29761 Hz)
void PlateReverb::prepare (double s)
{
    sr = s; scale = sr / 29761.0;
    pre.prepare ((int) (0.21 * sr));
    static const int inLen[4] = { 142, 107, 379, 277 };
    for (int k = 0; k < 4; ++k) { inAp[k].len = std::round (inLen[k] * scale); inAp[k].d.prepare ((int) inAp[k].len + 4); }
    const int d1[2] = { 672, 908 }, d2[2] = { 1800, 2656 }, t1[2] = { 4453, 4217 }, t2[2] = { 3720, 3163 };
    for (int c = 0; c < 2; ++c)
    {
        decAp1[c].len = std::round (d1[c] * scale); decAp1[c].d.prepare ((int) (decAp1[c].len + 40 * scale) + 4);
        decAp2[c].len = std::round (d2[c] * scale); decAp2[c].d.prepare ((int) decAp2[c].len + 4);
        tankDl1[c].prepare ((int) (t1[c] * scale) + 4); tankDl2[c].prepare ((int) (t2[c] * scale) + 4);
    }
    reset();
}

void PlateReverb::reset()
{
    pre.reset();
    for (auto& a : inAp) a.d.reset();
    for (int c = 0; c < 2; ++c) { decAp1[c].d.reset(); decAp2[c].d.reset(); tankDl1[c].reset(); tankDl2[c].reset(); damp[c] = 0.0f; tankFb[c] = 0.0f; }
    bw = 0.0f; lfoPh = 0.0; freezeS = 0.0f;
}

void PlateReverb::process (float* L, float* R, int n, const float* v, const FxContext&, const float* duck)
{
    const float t60 = clampv (v[P_sxPlDecay], 0.2f, 10.0f);
    const float decayBase = clampv ((float) std::pow (10.0, -3.0 * 0.18 / t60), 0.0f, 0.985f);
    const float dampBase = clampv (v[P_sxPlDamp], 0.0f, 1.0f) * 0.85f;
    const double preS = clampv (v[P_sxPlPre], 0.0f, 200.0f) * 0.001 * sr;
    const float width = clampv (v[P_sxPlWidth], 0.0f, 1.0f);
    const double exc = 16.0 * scale * clampv (v[P_sxPlMod], 0.0f, 1.0f) * 1.5;
    const bool freeze = v[P_sxPlFreeze] > 0.5f;
    const float fc = smoothCoef (1.0 / sr, 0.03);
    const double lfoInc = 0.8 / sr;
    const int t1[2] = { 4453, 4217 }, t2[2] = { 3720, 3163 };
    auto tapT1 = [&] (int c, int at) { return tankDl1[c].read (at * scale); };
    auto tapT2 = [&] (int c, int at) { return tankDl2[c].read (at * scale); };
    auto tapA2 = [&] (int c, int at) { return decAp2[c].d.read (at * scale); };
    for (int i = 0; i < n; ++i)
    {
        freezeS += ((freeze ? 1.0f : 0.0f) - freezeS) * fc;
        const float decay = decayBase + (0.99995f - decayBase) * freezeS;
        const float dampC = dampBase * (1.0f - freezeS);
        lfoPh += lfoInc; if (lfoPh >= 1.0) lfoPh -= 1.0;
        // the input: mono, pre-delayed, band-limited, diffused
        pre.push (0.5f * (L[i] + R[i]) * (1.0f - freezeS));
        float x = pre.read (std::max (0.0, preS));
        bw += (x - bw) * 0.9995f; x = bw;
        x = inAp[0].tick (x, 0.75f, inAp[0].len); x = inAp[1].tick (x, 0.75f, inAp[1].len);
        x = inAp[2].tick (x, 0.625f, inAp[2].len); x = inAp[3].tick (x, 0.625f, inAp[3].len);
        // the tank: two branches, each feeding the other
        float outBranch[2];
        for (int c = 0; c < 2; ++c)
        {
            const double m = exc * (1.0f - freezeS) * std::sin (2.0 * kPi * (lfoPh + 0.25 * c));
            float y = x + tankFb[1 - c];
            y = decAp1[c].tick (y, -0.7f, decAp1[c].len + m);
            tankDl1[c].push (y);
            float z = tankDl1[c].read (std::round (t1[c] * scale) - 1.0);
            damp[c] = z * (1.0f - dampC) + damp[c] * dampC;
            z = damp[c] * decay;
            z = decAp2[c].tick (z, 0.5f, decAp2[c].len);
            tankDl2[c].push (z);
            outBranch[c] = softLimit (tankDl2[c].read (std::round (t2[c] * scale) - 1.0) * decay, 2.0f);
        }
        tankFb[0] = outBranch[0]; tankFb[1] = outBranch[1];
        // the output taps (Dattorro's table)
        const float yl = tapT1 (1, 266) + tapT1 (1, 2974) - tapA2 (1, 1913) + tapT2 (1, 1996) - tapT1 (0, 1990) - tapA2 (0, 187) - tapT2 (0, 1066);
        const float yr = tapT1 (0, 353) + tapT1 (0, 3627) - tapA2 (0, 1228) + tapT2 (0, 2673) - tapT1 (1, 2111) - tapA2 (1, 335) - tapT2 (1, 121);
        const float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * width;
        const float g = 0.6f * (duck != nullptr ? duck[i] : 1.0f);
        L[i] += (mid + side) * g; R[i] += (mid - side) * g;
    }
}

//==============================================================================
// Micro-pitch Widener: a pitch shifter each side (two taps on a delay line, sweeping, crossfaded with Hann windows)
void MicroWidener::prepare (double s) { sr = s; for (auto& d : dl) d.prepare ((int) (0.08 * sr)); reset(); }
void MicroWidener::reset() { for (auto& d : dl) d.reset(); ph[0] = 0.0; ph[1] = 0.5; hp.reset(); hp2.reset(); lastCut = -1.0f; }

void MicroWidener::process (float* L, float* R, int n, const float* v, const FxContext&)
{
    const double cents = clampv (v[P_sxWidCents], 0.0f, 25.0f);
    const double r = std::pow (2.0, cents / 1200.0);
    const double W = 0.035 * sr;
    const double base = clampv (v[P_sxWidDelay], 0.0f, 30.0f) * 0.001 * sr + 2.0;
    const float cut = clampv (v[P_sxWidLowCut], 50.0f, 1000.0f);
    if (cut != lastCut) { lastCut = cut; hp.set (Biquad::HP, cut, -3.0103, sr); hp2.set (Biquad::HP, cut, -3.0103, sr); }   // (24 dB / octave: the lows stay mono)
    const float width = clampv (v[P_sxWidWidth], 0.0f, 1.0f) * 0.75f;
    const double incUp = (r - 1.0) / W, incDn = (1.0 - 1.0 / r) / W;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (L[i] + R[i]);
        dl[0].push (x);
        // up: the delay shrinks (W -> 0); down: it grows (0 -> W); two taps half a sweep apart
        float wet[2];
        for (int s = 0; s < 2; ++s)
        {
            const double p = ph[s];
            const double pA = p, pB = p + 0.5 - std::floor (p + 0.5);
            const double dA = s == 0 ? W * (1.0 - pA) : W * pA, dB = s == 0 ? W * (1.0 - pB) : W * pB;
            const float wA = (float) (std::sin (kPi * pA) * std::sin (kPi * pA)), wB = 1.0f - wA;
            wet[s] = dl[0].read (base + dA) * wA + dl[0].read (base + dB) * wB;
            ph[s] += s == 0 ? incUp : incDn; if (ph[s] >= 1.0) ph[s] -= 1.0;
        }
        const float a = (float) hp2.process (hp.process (wet[0], 0), 0), b = (float) hp2.process (hp.process (wet[1], 1), 1);
        L[i] += a * width; R[i] += b * width;
    }
}

//==============================================================================
// Dimension chorus (after the Roland SDD-320: two short delays swept in opposite directions, cross-mixed)
void DimensionChorus::prepare (double s) { sr = s; for (auto& d : dl) d.prepare ((int) (0.012 * sr)); bbd.set (Biquad::LP, 9000.0, -3.0103, sr); reset(); }
void DimensionChorus::reset() { for (auto& d : dl) d.reset(); bbd.reset(); ph = 0.0; depthS = -1.0f; }

void DimensionChorus::process (float* L, float* R, int n, const float* v, const FxContext&)
{
    static const float depthMs[4] = { 0.55f, 1.0f, 1.0f, 1.6f }, rateHz[4] = { 0.25f, 0.25f, 0.5f, 0.5f };
    float depth = 0.0f, rate = 0.25f; int on = 0;
    for (int m = 0; m < 4; ++m) if (v[P_sxDim1 + m] > 0.5f) { depth += depthMs[m]; rate = std::max (rate, rateHz[m]); ++on; }
    depth = std::min (depth, 2.4f);
    if (depthS < 0.0f) depthS = depth;
    const float cs = smoothCoef (1.0 / sr, 0.05);
    const double centre = 0.0035 * sr;
    for (int i = 0; i < n; ++i)
    {
        depthS += (depth - depthS) * cs;
        rateS += (rate - rateS) * cs;
        const float t = tri (ph);
        ph += rateS / sr; if (ph >= 1.0) ph -= 1.0;
        const double d1 = centre + depthS * 0.001 * sr * t, d2 = centre - depthS * 0.001 * sr * t;
        dl[0].push (L[i]); dl[1].push (R[i]);
        const float w1 = (float) bbd.process (dl[0].read (d1), 0), w2 = (float) bbd.process (dl[1].read (d2), 1);
        if (on == 0) continue;
        L[i] += 0.6f * w1 - 0.3f * w2;
        R[i] += 0.6f * w2 - 0.3f * w1;
    }
}

//==============================================================================
// IR Reverb
void IrReverb::prepare (double s, int maxBlock)
{
    sr = s;
    buf.setSize (2, std::max (32, maxBlock));
    conv.prepare ({ sr, (juce::uint32) std::max (32, maxBlock), 2 });
    for (auto& p : pre) p.prepare ((int) (0.21 * sr));
    reset();
}
void IrReverb::reset() { conv.reset(); for (auto& p : pre) p.reset(); lp.reset(); hp.reset(); lastLp = lastHp = -1.0f; }

void IrReverb::load (juce::AudioBuffer<float>&& ir, double irRate)
{
    conv.loadImpulseResponse (std::move (ir), irRate, juce::dsp::Convolution::Stereo::yes,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
}

void IrReverb::process (float* L, float* R, int n, const float* v, const FxContext&, const float* duck)
{
    if (! ready())
    {
        // (no impulse yet: nothing added; the convolution still runs on silence, which is where it picks up a new impulse)
        buf.clear();
        juce::dsp::AudioBlock<float> blk (buf.getArrayOfWritePointers(), 2, (size_t) std::min (n, buf.getNumSamples()));
        conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
        return;
    }
    const double preS = clampv (v[P_sxIrPre], 0.0f, 200.0f) * 0.001 * sr;
    const float fl = clampv (v[P_sxIrLP], 1000.0f, 20000.0f), fh = clampv (v[P_sxIrHP], 20.0f, 1000.0f);
    if (fl != lastLp) { lastLp = fl; lp.set (Biquad::LP, fl, -3.0103, sr); }
    if (fh != lastHp) { lastHp = fh; hp.set (Biquad::HP, fh, -3.0103, sr); }
    const float width = clampv (v[P_sxIrWidth], 0.0f, 1.0f);
    for (int start = 0; start < n; start += buf.getNumSamples())
    {
        const int m = std::min (buf.getNumSamples(), n - start);
        float* a = buf.getWritePointer (0); float* b = buf.getWritePointer (1);
        for (int i = 0; i < m; ++i)
        {
            pre[0].push (L[start + i]); pre[1].push (R[start + i]);
            a[i] = pre[0].read (preS); b[i] = pre[1].read (preS);
        }
        juce::dsp::AudioBlock<float> blk (buf.getArrayOfWritePointers(), 2, (size_t) m);
        conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
        for (int i = 0; i < m; ++i)
        {
            const float x = (float) hp.process (lp.process (a[i], 0), 0), y = (float) hp.process (lp.process (b[i], 1), 1);
            const float mid = 0.5f * (x + y), side = 0.5f * (x - y) * width;
            const float g = duck != nullptr ? duck[start + i] : 1.0f;
            L[start + i] += (mid + side) * g; R[start + i] += (mid - side) * g;
        }
    }
}

//==============================================================================
// The built-in impulses: original, synthesised (Room and Hall from noise; Plate and Spring from this file's own reverbs)
juce::AudioBuffer<float> makeSpaceImpulse (int type, double sr, double maxSeconds)
{
    juce::AudioBuffer<float> ir;
    if (type == 3 || type == 4)
    {
        const double secs = std::min (maxSeconds, type == 3 ? 3.0 : 2.5);
        const int len = std::max (16, (int) (secs * sr));
        ir.setSize (2, len); ir.clear();
        float v[P_COUNT];
        for (int i = 0; i < P_COUNT; ++i) v[i] = meta (i).def;
        FxContext ctx; ctx.sr = sr;
        if (type == 3)
        {
            PlateReverb pl; v[P_sxPlDecay] = 2.2f; v[P_sxPlDamp] = 0.25f; v[P_sxPlPre] = 0.0f; v[P_sxPlMod] = 0.0f;
            pl.prepare (sr);
            ir.setSample (0, 0, 1.0f); ir.setSample (1, 0, 1.0f);
            pl.process (ir.getWritePointer (0), ir.getWritePointer (1), len, v, ctx, nullptr);
        }
        else
        {
            SpringReverb sp; v[P_sxSprDecay] = 1.8f; v[P_sxSprTension] = 0.65f;
            sp.prepare (sr);
            ir.setSample (0, 0, 1.0f); ir.setSample (1, 0, 1.0f);
            sp.process (ir.getWritePointer (0), ir.getWritePointer (1), len, v, ctx, nullptr);
        }
        // (the direct sound out: only the reverb)
        ir.setSample (0, 0, ir.getSample (0, 0) - 1.0f); ir.setSample (1, 0, ir.getSample (1, 0) - 1.0f);
    }
    else
    {
        const double secs = std::min (maxSeconds, type == 1 ? 0.7 : 3.2);
        const int len = std::max (16, (int) (secs * sr));
        ir.setSize (2, len);
        uint32_t rng = type == 1 ? 0xA11CEu : 0xBA11u;
        for (int c = 0; c < 2; ++c)
        {
            float* d = ir.getWritePointer (c);
            float lp = 0.0f;
            for (int i = 0; i < len; ++i)
            {
                const double t = (double) i / sr;
                const double env = std::exp (-6.9 * t / secs);
                // the highs die away faster: a low-pass closing over the tail
                const float cut = (float) (1.0 - std::exp (-2.0 * kPi * (9000.0 * std::exp (-2.5 * t / secs) + 600.0) / sr));
                lp += (noise (rng) - lp) * cut;
                float s = (float) (lp * env);
                if (t < 0.004) s *= (float) (t / 0.004);
                d[i] = s;
            }
            // early reflections
            const double er[6] = { 0.0071, 0.0113, 0.0167, 0.0219, 0.0289, 0.0347 };
            for (int k = 0; k < 6; ++k)
            {
                const int at = (int) (er[k] * (type == 1 ? 1.0 : 2.2) * sr * (c == 0 ? 1.0 : 1.09));
                if (at < len) d[at] += (k & 1 ? -0.5f : 0.6f) * (1.0f - 0.1f * k);
            }
        }
    }
    // the energy normalised (about -6 dB of wet for a steady input)
    double e = 0.0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < ir.getNumSamples(); ++i) e += (double) ir.getSample (c, i) * ir.getSample (c, i);
    if (e > 1.0e-12) ir.applyGain ((float) (0.5 / std::sqrt (e * 0.5)));
    return ir;
}

} // namespace tg
