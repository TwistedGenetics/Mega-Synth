#include "RackFx.h"

namespace tg
{

double syncRate (const ChoiceList& list, int idx, double tempo, double fallback);   // Engine.cpp

static inline float dbToGain (float db) { return std::exp (db * 0.11512925f); }
static inline float coefFor (double seconds, double sr) { return (float) (1.0 - std::exp (-1.0 / std::max (1.0, seconds * sr))); }

//==============================================================================
// Multiband compressor (OTT style). The bands are split subtractively (low = LR4 low pass,
// the rest = input - low, and so on), so with no compression the bands add back to exactly
// the input: no phase smearing at the crossovers when the effect is neutral.
void MultibandComp::prepare (double s) { sr = s; reset(); }
void MultibandComp::reset()
{
    x1.reset(); x2.reset(); x1.f = x2.f = -1;
    for (int b = 0; b < 3; ++b) { env[b] = 0.0f; gainDb[b] = 0.0f; shownDb[b] = 0.0f; meter[b].store (0.0f); }
}

void MultibandComp::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const float fLow = clampv (v[P_mbcXLow], 40.0f, 1000.0f);
    const float fHigh = clampv (std::max (v[P_mbcXHigh], fLow * 1.5f), 1000.0f, (float) std::min (12000.0, sr * 0.45));
    x1.set (fLow, sr); x2.set (fHigh, sr);
    const float depth = clampv (v[P_mbcDepth], 0.0f, 1.0f);
    const double tscale = std::pow (2.0, (clampv (v[P_mbcTime], 0.0f, 1.0f) - 0.5) * 4.0);   // 0.25x .. 4x
    static const double baseA[3] = { 0.010, 0.005, 0.002 }, baseR[3] = { 0.15, 0.08, 0.05 };
    float att[3], rel[3], inG[3], up[3], down[3], outDb[3];
    for (int b = 0; b < 3; ++b)
    {
        att[b] = coefFor (baseA[b] * tscale, sr); rel[b] = coefFor (baseR[b] * tscale, sr);
        up[b] = clampv (v[P_mbcUpL + 4 * b], 0.0f, 1.0f) * depth;
        down[b] = clampv (v[P_mbcDownL + 4 * b], 0.0f, 1.0f) * depth;
        inG[b] = dbToGain (v[P_mbcInL + 4 * b]);
        outDb[b] = v[P_mbcOutL + 4 * b];
    }
    const float outG = dbToGain (v[P_mbcGain]);
    const int step = ctx.quality == 0 ? 8 : 1;   // Eco: gains every 8 samples
    float g[3];
    for (int b = 0; b < 3; ++b) g[b] = dbToGain (gainDb[b] + outDb[b]);
    for (int i = 0; i < n; ++i)
    {
        float band[3][2];
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            const float x = io[c][i];
            const float lo = x1.process (x, c), rest = x - lo, mid = x2.process (rest, c);
            band[0][c] = lo * inG[0]; band[1][c] = mid * inG[1]; band[2][c] = (rest - mid) * inG[2];
        }
        for (int b = 0; b < 3; ++b)
        {
            const float a = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
            env[b] += (a - env[b]) * (a > env[b] ? att[b] : rel[b]);
        }
        if (i % step == 0)
            for (int b = 0; b < 3; ++b)
            {
                const float lv = 20.0f * std::log10 (env[b] + 1.0e-9f);
                float gd = 0.0f;
                const float Td = -18.0f, Tu = -36.0f;
                if (lv > Td) gd -= (lv - Td) * (1.0f - 1.0f / (1.0f + 19.0f * down[b]));                 // up to 20:1 above -18 dB
                if (lv < Tu && lv > -80.0f)
                    gd += std::min (24.0f, (Tu - lv) * (1.0f - 1.0f / (1.0f + 3.0f * up[b]))) * clampv ((lv + 80.0f) / 10.0f, 0.0f, 1.0f);   // up to 4:1 below -36 dB
                gainDb[b] = gd;
                g[b] = dbToGain (gd + outDb[b]);
            }
        for (int c = 0; c < 2; ++c)
            io[c][i] = (band[0][c] * g[0] + band[1][c] * g[1] + band[2][c] * g[2]) * outG;
    }
    for (int b = 0; b < 3; ++b) meter[b].store (gainDb[b], std::memory_order_relaxed);
}

//==============================================================================
// Vintage Sampler: input clip, anti-alias filter (or none), sample-and-hold at the machine's
// rate, bit reduction, hiss and an output filter.
void VintageSampler::prepare (double s) { sr = s; reset(); }
void VintageSampler::reset()
{
    for (auto& b : aa) b.reset();
    out1.reset(); ph = 0.0;
    held[0] = held[1] = lastIn[0] = lastIn[1] = 0.0f;
    lastModel = -1; lastRate = lastCut = lastRes = -1;
}

void VintageSampler::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const int model = clampv ((int) std::lround (v[P_smpModel]), 0, 3);
    float rate = 26040.0f, bits = 12.0f;
    int aaStages = 0; int outMode = 0;   // 0 none, 1 fixed 12 kHz, 2 the resonant Filter knob
    switch (model)
    {
        case 0: rate = 26040.0f; aaStages = 0; outMode = 0; break;                          // SP-1200: no anti-alias filter
        case 1: rate = clampv (v[P_smpRate], 2000.0f, 48000.0f); aaStages = 4; outMode = 2; break;   // S950: steep anti-alias filter
        case 2: rate = 27778.0f; aaStages = 1; outMode = 1; break;                          // E-mu
        default: rate = clampv (v[P_smpRate], 2000.0f, 48000.0f); bits = clampv (v[P_smpBits], 1.0f, 16.0f);
                 aaStages = v[P_smpAA] > 0.5f ? 4 : 0; outMode = 2; break;
    }
    if (outMode == 2 && v[P_smpCutoff] >= 19900.0f) outMode = 0;   // Filter fully open = no output filter
    rate = std::min (rate, (float) sr);
    const float cut = clampv (v[P_smpCutoff], 500.0f, (float) (sr * 0.45)), res = clampv (v[P_smpRes], 0.0f, 1.0f);
    if (model != lastModel || rate != lastRate || cut != lastCut || res != lastRes)
    {
        const double aaF = std::min (rate * (aaStages >= 4 ? 0.42 : 0.45), sr * 0.45);
        for (auto& b : aa) b.set (Biquad::LP, aaF, -3.0103, sr);
        if (outMode == 1) out1.set (Biquad::LP, std::min (12000.0, sr * 0.45), -3.0103, sr);
        else if (outMode == 2) out1.set (Biquad::LP, cut, 20.0 * std::log10 (0.707 + res * 7.0), sr);
        if (model != lastModel) { for (auto& b : aa) b.reset(); out1.reset(); }
        lastModel = model; lastRate = rate; lastCut = cut; lastRes = res;
    }
    const float drive = 1.0f + 3.0f * clampv (v[P_smpDrive], 0.0f, 1.0f), dNorm = 1.0f / std::sqrt (drive);
    const float q = std::pow (2.0f, bits - 1.0f), iq = 1.0f / q;
    const float noise = clampv (v[P_smpNoise], 0.0f, 1.0f) * 0.004f;
    const double inc = rate / sr;
    const bool os = ctx.quality >= 2;   // High: the input clip runs at twice the rate
    auto clip = [&] (float x) { return fastTanh (drive * x) * dNorm; };
    for (int i = 0; i < n; ++i)
    {
        float* io[2] = { L, R };
        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = io[c][i];
            float s = os ? 0.5f * (clip (0.5f * (x + lastIn[c])) + clip (x)) : clip (x);
            lastIn[c] = x;
            for (int k = 0; k < aaStages; ++k) s = aa[k].process (s, c);
            y[c] = s;
        }
        ph += inc;
        if (ph >= 1.0)
        {
            ph -= std::floor (ph);
            for (int c = 0; c < 2; ++c) held[c] = std::round (clampv (y[c], -1.0f, 1.0f) * q) * iq;
        }
        for (int c = 0; c < 2; ++c)
        {
            rng = rng * 1664525u + 1013904223u;
            float o = held[c] + noise * ((float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f);
            if (outMode != 0) o = out1.process (o, c);
            io[c][i] = o;
        }
    }
}

//==============================================================================
// Beat Repeat: keeps the last 4 seconds; a trigger plays the last 1/4 .. 1/32 over and over.
void BeatRepeat::prepare (double s)
{
    sr = s;
    for (auto& b : buf) b.assign ((size_t) (sr * 4.0) + 8, 0.0f);
    reset();
}
void BeatRepeat::reset()
{
    for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
    w = 0; active = false; wasHeld = false; mixEnv = 0.0f; repeatNo = 0; activity.store (0); prevGate = 0.0f;
}

void BeatRepeat::start (double len, double duration)
{
    segLen = curLen = std::max (16.0, std::min (len, (double) buf[0].size() - 8.0));
    capStart = (double) w - segLen;
    pos = 0.0; rate = 1.0; repeatNo = 1;
    remaining = duration;
    active = true;
}

void BeatRepeat::process (float* L, float* R, int n, const float* v, const FxContext& ctx, bool held)
{
    static const double lens[] = { 1.0, 0.5, 0.25, 0.125 }, durs[] = { 0.5, 1.0, 2.0, 4.0 };
    const double spb = 60.0 / std::max (20.0, ctx.tempo) * sr;
    const double len = lens[clampv ((int) std::lround (v[P_rptLength]), 0, 3)] * spb;
    const double dur = durs[clampv ((int) std::lround (v[P_rptDuration]), 0, 3)] * spb;
    const double shrink = 1.0 - 0.5 * clampv (v[P_rptShrink], 0.0f, 1.0f);
    const double pitchMul = std::pow (2.0, -clampv (v[P_rptPitch], 0.0f, 12.0f) / 12.0);
    const bool reverse = v[P_rptReverse] > 0.5f;
    const float gate = clampv (v[P_rptGate], 0.1f, 1.0f);
    const float chance = clampv (v[P_rptChance], 0.0f, 1.0f);
    const double minLen = spb / 16.0;
    const float ramp = coefFor (0.003, sr);
    const int size = (int) buf[0].size();

    // DNA Step Gate: a new step (rising pulse) starts a repeat
    const bool dnaHit = v[P_rptDna] > 0.5f && ctx.dnaGate > 0.6f && prevGate < 0.3f;
    prevGate = ctx.dnaGate;
    if (held && ! wasHeld) start (len, -1.0);
    else if (! held && wasHeld && active && remaining < 0.0) remaining = 0.0;   // released: stop
    wasHeld = held;
    if (dnaHit && ! active) start (len, dur);

    for (int i = 0; i < n; ++i)
    {
        // chance, decided once per beat at the beat line
        if (chance > 0.0f && ! active && ctx.beatInc > 0.0)
        {
            const double b0 = ctx.beat + (i - 1) * ctx.beatInc, b1 = ctx.beat + i * ctx.beatInc;
            if (std::floor (b1) != std::floor (b0))
            {
                rng = rng * 1664525u + 1013904223u;
                if ((rng >> 8) * (1.0f / 16777216.0f) < chance) start (len, dur);
            }
        }
        const float in[2] = { L[i], R[i] };
        buf[0][(size_t) w] = in[0]; buf[1][(size_t) w] = in[1];
        if (++w >= size) w = 0;

        mixEnv += ((active ? 1.0f : 0.0f) - mixEnv) * ramp;
        if (mixEnv < 1.0e-4f && ! active) { mixEnv = 0.0f; continue; }

        // the repeat: short fades at its edges and at the gate
        const double gateEnd = gate * curLen;
        const double fade = std::min (64.0, curLen * 0.1);
        float e = (float) std::min ({ 1.0, pos / fade, std::max (0.0, (gateEnd - pos) / fade) });
        const double rp = reverse ? curLen - 1.0 - pos : pos;
        for (int c = 0; c < 2; ++c)
        {
            const float r = readBuf (c, capStart + rp) * e;
            (c == 0 ? L : R)[i] = in[c] + (r - in[c]) * mixEnv;
        }
        pos += rate;
        if (pos >= curLen)
        {
            pos -= curLen;
            ++repeatNo;
            curLen = std::max (minLen, curLen * shrink);
            rate *= pitchMul;
            if (pos >= curLen) pos = 0.0;
        }
        if (active && remaining >= 0.0)
        {
            remaining -= 1.0;
            if (remaining <= 0.0) active = false;
        }
    }
    activity.store (active ? repeatNo : 0, std::memory_order_relaxed);
}

//==============================================================================
// Flanger (optionally through-zero) and phaser, swept by an LFO or by the input level.
void FlangerPhaser::prepare (double s)
{
    sr = s;
    for (int c = 0; c < 2; ++c) { dl[c].prepare ((int) (sr * 0.03)); dry[c].prepare ((int) (sr * 0.03)); }
    reset();
}
void FlangerPhaser::reset()
{
    for (int c = 0; c < 2; ++c) { dl[c].reset(); dry[c].reset(); fb[c] = 0.0f; apFb[c] = 0.0f; for (auto& z : apZ[c]) z = 0.0f; }
    env = 0.0f; ph = 0.0; dS = -1.0f;
}

void FlangerPhaser::process (float* L, float* R, int n, const float* v, const FxContext& ctx)
{
    const bool phaser = std::lround (v[P_flpMode]) == 1;
    const double rate = syncRate (kListModSync, (int) std::lround (v[P_flpSync]), ctx.tempo, clampv (v[P_flpRate], 0.01f, 10.0f));
    const double inc = rate / sr;
    const float depth = clampv (v[P_flpDepth], 0.0f, 1.0f), fbAmt = clampv (v[P_flpFeedback], -0.95f, 0.95f);
    const float manual = clampv (v[P_flpManual], 0.0f, 1.0f), spread = clampv (v[P_flpSpread], 0.0f, 1.0f) * 0.5f;
    const bool envMode = v[P_flpEnv] > 0.5f, tz = v[P_flpTZ] > 0.5f;
    const float sens = clampv (v[P_flpEnvSens], 0.0f, 1.0f);
    const float eA = coefFor (0.005, sr), eR = coefFor (0.15, sr);
    const float centerT = (float) ((0.3 + manual * 9.7) * 0.001 * sr);
    if (dS < 0.0f) dS = centerT;
    const float cs = coefFor (0.05, sr);
    static const int stagesFor[] = { 4, 8, 12 };
    const int stages = stagesFor[clampv ((int) std::lround (v[P_flpStages]), 0, 2)];
    const int coefEvery = ctx.quality == 0 ? 32 : 8;
    for (int i = 0; i < n; ++i)
    {
        float* io[2] = { L, R };
        const float a = std::max (std::abs (L[i]), std::abs (R[i]));
        env += (a - env) * (a > env ? eA : eR);
        dS += (centerT - dS) * cs;
        for (int c = 0; c < 2; ++c)
        {
            const float x = io[c][i];
            const float lfo = envMode ? clampv (env * (1.0f + sens * 30.0f) * 2.0f - 1.0f, -1.0f, 1.0f)
                                      : (float) std::sin (2.0 * kPi * (ph + c * spread));
            if (! phaser)
            {
                float d, dryPath = x;
                if (tz)
                {
                    dry[c].push (x);
                    dryPath = dry[c].read (dS);
                    d = dS * (1.0f + depth * lfo);                    // sweeps through the dry path's delay
                }
                else d = dS * (1.0f + depth * 0.95f * lfo);
                dl[c].push (x + fbAmt * fb[c]);
                const float wet = dl[c].read (std::max (0.0f, d));
                fb[c] = wet;
                io[c][i] = (dryPath + wet) * 0.6f;
            }
            else
            {
                if (i % coefEvery == 0)
                {
                    const double fc = clampv (80.0 * std::pow (2.0, manual * 5.5 + depth * lfo * 2.0), 20.0, sr * 0.45);
                    const double t = std::tan (kPi * fc / sr);
                    apA[c] = (float) ((t - 1.0) / (t + 1.0));
                }
                float y = x + fbAmt * apFb[c];
                for (int k = 0; k < stages; ++k)
                {
                    const float o = apA[c] * y + apZ[c][k];
                    apZ[c][k] = y - apA[c] * o;
                    y = o;
                }
                apFb[c] = y;
                io[c][i] = 0.5f * (x + y);
            }
        }
        ph += inc; if (ph >= 1.0) ph -= 1.0;
    }
}

//==============================================================================
// Volume Shaper: the drawn curve over 1/4, 1/2 or 1 bar, locked to the beat or restarted.
void VolumeShaper::process (float* L, float* R, int n, const float* v, const FxContext& ctx, const FxRackStore& st)
{
    static const double periods[] = { 1.0, 2.0, 4.0 };
    const double P = periods[clampv ((int) std::lround (v[P_vshRate]), 0, 2)];
    const float depth = clampv (v[P_vshDepth], 0.0f, 1.0f);
    const float k = coefFor (0.0003 + clampv (v[P_vshSmooth], 0.0f, 1.0f) * 0.02, ctx.sr);
    const int trig = clampv ((int) std::lround (v[P_vshTrig]), 0, 2);
    if (trig == 0) anchor = 0.0;
    else if (trig == 1 && pendingNote) anchor = noteBeat;
    else if (trig == 2 && ctx.dnaGate > 0.6f && prevGate < 0.3f) anchor = ctx.beat;
    pendingNote = false;
    prevGate = ctx.dnaGate;
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase = (ctx.beat + i * ctx.beatInc - anchor) / P;
        const float target = 1.0f - depth * (1.0f - st.curveAt (phase));
        g += (target - g) * k;
        L[i] *= g; R[i] *= g;
    }
    phaseShown.store ((float) (phase - std::floor (phase)), std::memory_order_relaxed);
}

//==============================================================================
// Stereo Tools in mid/side: the side signal loses its lows (Bass Mono), is scaled (Width),
// and gets a delayed copy of the mid's top end (Haas). The mid is never changed, so a mono
// sum (L + R) is exactly the same as without the effect.
void StereoTools::prepare (double s)
{
    sr = s;
    haas.prepare ((int) (sr * 0.035));
    reset();
}
void StereoTools::reset() { sideHigh.type = Biquad::HP; sideHigh.reset(); midLow.reset(); sideHigh.f = midLow.f = -1; haas.reset(); haasS = 0.0f; haasAmt = 0.0f; widthS = 1.0f; }

void StereoTools::process (float* L, float* R, int n, const float* v, const FxContext&)
{
    const bool mono = v[P_sttMono] > 0.5f;
    const float f = clampv (v[P_sttMonoFreq], 50.0f, 300.0f);
    sideHigh.set (f, sr); midLow.set (std::max (f, 200.0f), sr);
    const float widthT = clampv (v[P_sttWidth], 0.0f, 2.0f);
    const float haasT = clampv (v[P_sttHaas], 0.0f, 30.0f) * 0.001f * (float) sr;
    const float amtT = v[P_sttHaas] > 0.05f ? 0.5f : 0.0f;
    const float cs = coefFor (0.02, sr), cSlow = coefFor (0.08, sr);
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        float s = 0.5f * (L[i] - R[i]);
        if (mono) s = sideHigh.process (s, 0);   // the side keeps only what's above the crossover
        widthS += (widthT - widthS) * cs;
        s *= widthS;
        haasS += (haasT - haasS) * cSlow;
        haasAmt += (amtT - haasAmt) * cs;
        const float mh = m - midLow.process (m, 0);
        haas.push (mh);
        if (haasAmt > 1.0e-5f) s += haasAmt * haas.read (haasS);
        L[i] = m + s; R[i] = m - s;
    }
}

} // namespace tg
