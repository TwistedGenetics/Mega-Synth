#pragma once
// The filter section.
//
// FilterUnit is one complete filter: drive, the filter itself, gain compensation and the
// wet/dry mix. It holds two engines:
//   - FilterChain: the 17 classic models (built from the browser synth's Web Audio biquads,
//     ladders and shapers). A model at Low Pass and its own slope runs exactly as it always has.
//   - MultiCore: zero-delay-feedback state-variable filters, one-poles and a 4-pole ladder
//     giving Low/High/Band Pass, Notch, Peak and All Pass at 6/12/18/24 dB per octave. The
//     selected model lends it its character (input stage, resonance strength, how hard the
//     resonance saturates, output shaping).
// Type, slope or model changes crossfade between the old and new engine over ~6 ms, so
// switching (or a scene morph) doesn't click. Everything a FilterUnit needs comes in through
// its own calls, so a second unit (serial or parallel) can be added to the voice later.
#include <cmath>
#include <algorithm>
#include "../DSP.h"
#include "../Params.h"

namespace tg
{

// Clean up to t, then a smooth knee that levels off at 1.5 t: only peaks above t are touched.
inline float softKnee (float x, float t)
{
    const float a = std::abs (x);
    if (a <= t) return x;
    const float h = 0.5f * t;
    const float y = t + h * fastTanh ((a - t) / h);
    return x < 0.0f ? -y : y;
}

// Shared input stage: the drive into the filter.
// At Drive 0 dB it is clean (linear below full scale; louder peaks are rounded off, not clipped).
// Turning Drive up blends in the saturation (fully in by +12 dB): tanh, or with Warmth a softer,
// slightly asymmetric curve with a touch of 2nd harmonic.
inline float filterInputShape (float x, float k, float warm)
{
    const float clean = softKnee (k * x, 1.0f);
    if (k <= 1.0f) return clean;
    float sat;
    if (warm <= 0.0f) sat = driveShape (x, k);
    else
    {
        const float c = clampv (x, -1.0f, 1.0f);
        const float soft = std::abs (x) < 1.5f ? x - (4.0f / 27.0f) * x * x * x : (x > 0 ? 1.0f : -1.0f);
        float xi = c + (soft - c) * warm;
        xi += 0.12f * warm * xi * xi;          // a touch of 2nd harmonic
        sat = fastTanh (k * xi);
    }
    const float a = std::min (1.0f, 0.5f * std::log2 (k));
    return clean + (sat - clean) * a;
}

// How much of the drive / model saturation is in, from the Drive knob: 0 at 0 dB, all at +12 dB
inline float driveCharacter (float k) { return k <= 1.0f ? 0.0f : std::min (1.0f, 0.5f * std::log2 (k)); }
// A model's own shaper (OTA, acid, Polivoks), blended in by the Drive knob
// At 0 dB it is a clean gain that keeps the model's level in line with the others.
inline float modelShape (float v, float k, float amount)
{
    const float g = k >= 7.0f ? 2.8f : (k >= 6.0f ? 1.7f : (k >= 4.0f ? 1.9f : 1.0f));   // Polivoks, acid, OTA
    if (amount <= 0.0f) return v * g;
    const float s = driveShape (v, k);
    return amount >= 1.0f ? s : v * g + (s - v * g) * amount;
}

// Output-level compensation for Drive: drive pushes the signal into the shaper (denser, more
// harmonics, louder); this takes most of the level rise back out. Exactly 1 at no drive.
inline float driveCompensation (float k)
{
    return k <= 1.0f ? 1.0f : 1.0f / std::sqrt (1.0f + 0.2f * (k - 1.0f));
}

//==============================================================================
// The 17 classic models (Filter Model), as they were before the filter overhaul.
struct FilterChain
{
    int mode = -1;
    int numStages = 0;
    Biquad st[4];
    Biquad::Type types[4] {};
    float fbState[2] { 0, 0 };
    float fbGain = 0.0f;
    enum FbKind { FbNone, FbToInput, FbToPre } fbKind = FbNone;
    float preK = 0.0f;     // extra drive stage after the input drive (OTA / TB-303 / acid)
    float postK = 0.0f;    // saturator after the filter (Polivoks)
    float character = 0.0f;   // how much of preK / postK is in (follows Drive: clean at 0 dB)
    bool limitOut = false; // soft limiter for the acid modes, whose resonance can reach +30 dB

    // Ladder / Minimoog modes: a 4-pole one-pole cascade with inverted feedback.
    // (In the browser these were Web Audio feedback loops, which don't resonate there.)
    bool ladder = false;
    float lg[4] { 0.5f, 0.5f, 0.5f, 0.5f };
    float ls[2][4] {};
    float lk = 0.0f;
    // TB-303 mode: the ladder with its resonance feedback high-passed (~150 Hz), as in the
    // real 303, so resonance fades at low cutoff and the filter closes right down.
    bool tb303 = false;
    float tbA = 0.98f;
    float tbX[2] { 0, 0 }, tbY[2] { 0, 0 };

    // Analog warmth (0 = exactly the browser's behaviour)
    float warm = 0.0f;       // soft, slightly asymmetric input saturation instead of a hard clip
    float bassKeep = 0.0f;   // lowers high-pass stages and adds low end back around band-pass stages
    Biquad par[4];           // parallel low-pass for band-pass stages (Bass Keep)
    float dcX[2] { 0, 0 }, dcY[2] { 0, 0 }, dcR = 0.9987f;

    inline float inputShape (float x, float k) const { return filterInputShape (x, k, warm); }

    inline float dcBlock (float v, int ch)
    {
        const float y = v - dcX[ch] + dcR * dcY[ch];
        dcX[ch] = v; dcY[ch] = y;
        return y;
    }

    void configure (int newMode);
    void update (float cutoff, float res, double sr);
    void setLadder (float cutoff, float spread, float k, double sr);
    void reset();
    void copyChannel (int from, int to);

    inline float process (float x, int ch, float drive)
    {
        if (ladder)
        {
            float fb = fbState[ch];
            if (tb303)
            {
                const float y = tbA * (tbY[ch] + fb - tbX[ch]);
                tbX[ch] = fb; tbY[ch] = y;
                fb = y;
            }
            float v = inputShape (x - lk * fb, drive);
            float* s = ls[ch];
            for (int k = 0; k < 4; ++k)
            {
                const float u = (v - s[k]) * lg[k];
                v = u + s[k];
                s[k] = v + u;
            }
            if (! (std::abs (v) < 64.0f)) { reset(); v = 0.0f; }   // runaway (or NaN) under extreme modulation: start again
            fbState[ch] = v;
            v *= tb303 ? (1.0f + lk * 0.45f) : (1.0f + lk * 0.35f);   // make up some of the bass lost to resonance
            if (limitOut) v = softKnee (v, 1.5f);   // only extreme resonance peaks
            return dcBlock (v, ch);
        }

        float v = inputShape (x, drive);
        if (preK > 0.0f)
        {
            if (fbKind == FbToPre) v += fbGain * fbState[ch];
            v = modelShape (v, preK, character);
        }
        for (int k = 0; k < numStages; ++k)
        {
            if (types[k] == Biquad::BP && bassKeep > 0.0f)
                v = st[k].process (v, ch) + bassKeep * par[k].process (v, ch);
            else
                v = st[k].process (v, ch);
        }
        if (! (std::abs (v) < 64.0f)) { reset(); v = 0.0f; }   // runaway (or NaN) under extreme modulation: start again
        fbState[ch] = v;
        if (postK > 0.0f) v = modelShape (v, postK, character);
        if (limitOut) v = softKnee (v, 1.5f);   // only extreme resonance peaks
        return dcBlock (v, ch);
    }
};


//==============================================================================
// What a classic model brings to the multimode core.
struct ModelVoice
{
    float preK = 0;        // extra shaper after the drive (OTA, acid)
    float postK = 0;       // saturator after the filter (Polivoks)
    float character = 0;   // how much of preK / postK is in (follows Drive)
    float resScale = 1;    // how strong its resonance is
    float satT = 3;        // where its resonance starts to saturate (lower = grittier)
    bool limitOut = false; // soft output limiter (acid, TB-303)
};
const ModelVoice& modelVoice (int model);

// Topology-preserving state-variable filter (Zavalishin / Cytomic). Stable under fast
// modulation; the band state is soft-limited so extreme resonance stays bounded.
struct Svf
{
    float a1 = 1, a2 = 0, a3 = 0, k = 2;
    float ic1[2] { 0, 0 }, ic2[2] { 0, 0 };
    void set (float g, float damping)
    {
        k = damping;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float v0, int ch, float& lp, float& bp, float sat)
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        if (sat > 0.0f) ic1[ch] = softKnee (ic1[ch], sat);   // bounds resonance; normal levels pass untouched
        lp = v2; bp = v1;
    }
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
    void copy (int from, int to) { ic1[to] = ic1[from]; ic2[to] = ic2[from]; }
};

// Topology-preserving one-pole (6 dB per octave).
struct OnePole
{
    float G = 0.5f;
    float s[2] { 0, 0 };
    void set (float g) { G = g / (1.0f + g); }
    inline float lp (float x, int ch)
    {
        const float v = (x - s[ch]) * G;
        const float y = v + s[ch];
        s[ch] = y + v;
        return y;
    }
    void reset() { s[0] = s[1] = 0.0f; }
    void copy (int from, int to) { s[to] = s[from]; }
};

struct MultiCore
{
    int type = FT_LP, slope = 1;
    ModelVoice mv;
    float warm = 0.0f;
    float r = 0.0f;              // resonance 0..1 (after the model's scaling)
    Svf a, b;
    OnePole p;
    float bump = 0.0f;           // 6 dB: resonant bump added from a state-variable band-pass
    float bpGainA = 1, bpGainB = 1, outK = 0, peakGain = 0;
    // 24 dB low pass: a 4-pole ladder (one-pole stages, inverted feedback), which self-oscillates
    float lg = 0.5f, lk = 0.0f;
    float ls[2][4] {}, lfb[2] { 0, 0 };
    float dcX[2] { 0, 0 }, dcY[2] { 0, 0 }, dcR = 0.9987f;

    void configure (int filterType, int effectiveSlope, int model);
    void update (float cutoff, float res, double sr);
    void reset();
    void copyChannel (int from, int to);
    float process (float x, int ch, float drive);
};

//==============================================================================
class FilterUnit
{
public:
    // model: classic model (filterMode); type: FilterType; slope: 0..3 (6/12/18/24 dB)
    void configure (int model, int type, int slope, bool immediate);
    void update (float cutoff, float res, double sr);     // clamps cutoff to 20 Hz .. min (20 kHz, 0.45 fs)
    void setAnalog (float warmth, float bassKeep);
    void setDrive (float k)
    {
        drive = std::max (1.0f, k); comp = driveCompensation (drive);
        const float c = driveCharacter (drive);
        for (Core* x : { &cur, &old }) { x->chain.character = c; x->multi.mv.character = c; }
    }
    void setMix (float m) { mix = clampv (m, 0.0f, 1.0f); }
    void reset();
    void copyChannel (int from, int to);

    inline void processFrame (float xL, float xR, bool stereo, float& yL, float& yR)
    {
        hist[0][hpos] = xL; hist[1][hpos] = xR; hpos = (hpos + 1) & (kHist - 1);
        lastStereo = stereo;
        yL = run (cur, xL, 0);
        yR = stereo ? run (cur, xR, 1) : yL;
        if (fade > 0)
        {
            const float oL = run (old, xL, 0);
            const float oR = stereo ? run (old, xR, 1) : oL;
            const float lin = 1.0f - (float) fade / (float) kFadeLen;
            const float f = lin * lin * (3.0f - 2.0f * lin);   // S-shaped: eases in and out
            yL = oL + (yL - oL) * f;
            yR = oR + (yR - oR) * f;
            --fade;
        }
        if (comp != 1.0f) { yL *= comp; yR *= comp; }
        if (mix < 1.0f) { yL = xL + (yL - xL) * mix; yR = (stereo ? xR : xL) + (yR - (stereo ? xR : xL)) * mix; }
    }

    bool isClassic() const { return cur.classic; }
    // For the response display: leave out the model's extra shapers, whose small-signal gain
    // (a saturator is very loud for a tiny test signal) would misrepresent the curve.
    void setPreview (bool p) { preview = p; }
    int model() const { return cur.model; }
    static constexpr int kFadeLen = 480;   // 10 ms at 48 kHz

private:
    struct Core
    {
        FilterChain chain;
        MultiCore multi;
        bool classic = true;
        int model = -1, type = -1, slope = -1;
    };
    inline float run (Core& c, float x, int ch) { return c.classic ? c.chain.process (x, ch, drive) : c.multi.process (x, ch, drive); }
    void setup (Core& c, int model, int type, int slope);

    Core cur, old;
    // the last few milliseconds of input: a newly chosen filter is run over them first, so it
    // joins the crossfade already settled instead of starting from silence (no start-up ring)
    static constexpr int kHist = 512;
    float hist[2][kHist] {};
    int hpos = 0;
    bool lastStereo = false, primePending = false;
    int fade = 0;
    float drive = 1.0f, comp = 1.0f, mix = 1.0f;
    float warm = 0.0f, keep = 0.0f;
    bool preview = false;
};

} // namespace tg
