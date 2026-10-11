#pragma once
// Character filter models (inspired by Beep Street's Sunrizer, plus extras). Included by Filter.h
// after Svf and OnePole, so it can use them; FilterUnit runs one of these instead of its classic
// engines when a filter's Character model isn't Off. Each model uses Cutoff and Resonance, plus one
// "Character" amount (0..1) whose meaning depends on the model (see charModelInfo()).
// Everything is per channel (stereo), bounded under any modulation, and allocation-free.
#include <array>
#include <cmath>

namespace tg
{

enum CharModel
{
    CM_Off, CM_Formant, CM_Formant2, CM_AllPass, CM_PowerLP, CM_PowerBP, CM_WaveShaper, CM_Resonator, CM_Resampler,
    CM_LPResample, CM_Shifter, CM_AnalogueLP, CM_DJ, CM_Comb, CM_VowelMorph, CM_Phaser, CM_Ring, CM_DecimatorLP, CM_DualPeak,
    CM_COUNT
};

struct CharModelInfo { const char* name; const char* character; const char* about; bool curveApprox; bool usesRes; };
inline const CharModelInfo& charModelInfo (int m)
{
    static const CharModelInfo info[CM_COUNT] = {
        { "Off", "", "The classic Model above.", false, true },
        { "Formant", "Vowel (A E I O U)", "Three vowel resonances (a male voice); Cutoff shifts them (1 kHz = natural), Resonance narrows them.", false, true },
        { "Formant 2", "Vowel (A E I O U)", "A brighter set of vowel peaks (a female voice), narrower and louder.", false, true },
        { "All Pass", "Spread", "Two all-pass stages mixed with the dry sound: a pair of moving notches. Character spreads the second stage up to two octaves.", false, true },
        { "Power LP", "Saturation", "A 24 dB low pass driven hard, that stays loud at high resonance.", false, true },
        { "Power BP", "Saturation", "A driven 24 dB band pass, loud at high resonance.", false, true },
        { "Wave Shaper", "Fold amount", "A wavefolder into a resonant low pass: more harmonics in, then filtered.", false, true },
        { "Resonator", "Decay", "A tuned comb (a plucked or struck body) at the cutoff frequency: set Key Track to 100% to play it in tune. Resonance = brightness.", false, true },
        { "Resampler", "Bit depth", "Sample-rate reduction as a filter: Cutoff sets the rate (no anti-aliasing: that's the sound). Resonance isn't used.", true, false },
        { "LP + Resample", "Resample rate", "A resonant low pass, then sample-rate reduction (Character: low = gritty).", true, true },
        { "Shifter", "Shift (centre = none)", "A frequency shifter (up to +-2 kHz) into a resonant low pass: inharmonic, metallic.", true, true },
        { "Analogue LP", "Drift / heat", "A warm 4-pole ladder with a little drift and saturation in every stage.", false, true },
        { "DJ Filter", "DJ knob (L: low pass, R: high pass)", "One knob: low pass to the left, high pass to the right, flat in the middle. Cutoff isn't used.", false, true },
        { "Comb +/-", "Polarity and feedback (centre = off)", "A comb at the cutoff frequency: left of centre negative (hollow), right positive (metallic). Resonance = brightness.", false, true },
        { "Vowel Morph", "Vowel (loops A E I O U A)", "Vowel peaks that loop round, so an LFO sweeps through the vowels without a jump. Cutoff shifts them.", false, true },
        { "Phaser Filter", "Stages (2-12)", "All-pass stages with feedback, centred on the cutoff: phaser notches you can play with the cutoff.", false, true },
        { "Ring Filter", "Ring amount", "A band pass ring-modulated at its own cutoff frequency: bell-like and metallic.", true, true },
        { "Decimator LP", "Bit crush in the loop", "A resonant low pass whose resonance is bit-crushed inside the filter: gritty, digital resonance.", true, true },
        { "Dual Peak", "Second peak (up to 3 octaves)", "Two resonant peaks, the second up to three octaves above the cutoff: talking basses.", false, true } };
    return info[m >= 0 && m < CM_COUNT ? m : 0];
}

class CharFilter
{
public:
    static constexpr int kDelay = 4096;   // comb / resonator delay (lowest pitch: sample rate / 4096)

    void configure (int m) { model = m; reset(); }
    int getModel() const { return model; }

    void reset()
    {
        for (auto* s : { &s1, &s2, &s3, &s4, &s5 }) s->reset();
        for (auto& c : ch)
        {
            c = {};
        }
        driftPos = 0.0f; driftT = 0.0f;
    }
    void copyChannel (int from, int to)
    {
        for (auto* s : { &s1, &s2, &s3, &s4, &s5 }) s->copy (from, to);
        ch[(size_t) to] = ch[(size_t) from];
    }

    // cutoff in Hz (already clamped), res = the Resonance knob (0.1 .. 25), amt = Character 0..1
    void update (float cutoff, float res, float amt, double sampleRate)
    {
        sr = sampleRate;
        fc = cutoff; amount = clampv (amt, 0.0f, 1.0f);
        r = clampv ((res - 0.1f) / 24.9f, 0.0f, 1.0f);
        const double nyq = sr * 0.45;
        auto gOf = [&] (double f) { return (float) std::tan (kPi * std::min (nyq, std::max (10.0, f)) / sr); };
        const float q = 0.55f * std::pow (60.0f, r);
        const float k = 1.0f / q;
        switch (model)
        {
            case CM_Formant: case CM_Formant2: case CM_VowelMorph:
            {
                float f[3], a[3];
                vowel (f, a);
                const float shift = clampv (fc / 1000.0f, 0.25f, 4.0f);
                const float fq = (model == CM_Formant2 ? 7.0f : 4.0f) + r * (model == CM_Formant2 ? 30.0f : 20.0f);
                Svf* st[3] = { &s1, &s2, &s3 };
                for (int i = 0; i < 3; ++i) { st[i]->set (gOf (f[i] * shift), 1.0f / fq); fGain[i] = a[i]; }
                outGain = model == CM_Formant2 ? 2.4f : 1.8f;
                break;
            }
            case CM_AllPass:
                s1.set (gOf (fc), k * 2.0f);
                s2.set (gOf (fc * std::pow (2.0, 2.0 * amount)), k * 2.0f);
                break;
            case CM_PowerLP: case CM_PowerBP:
                s1.set (gOf (fc), 1.0f / 0.5412f);
                s2.set (gOf (fc), std::max (0.02f, std::min (k, 1.0f / 1.3066f)));
                break;
            case CM_WaveShaper: case CM_LPResample: case CM_Shifter: case CM_Ring:
                s1.set (gOf (fc), std::max (0.03f, k));
                break;
            case CM_DecimatorLP:
                s1.set (gOf (fc), std::max (0.03f, k));
                crushQ = std::pow (2.0f, 14.0f - 11.0f * amount);
                break;
            case CM_Resonator: case CM_Comb:
                delaySamples = (float) clampv (sr / std::max (1.0, (double) fc), 2.0, (double) kDelay - 2.0);
                damp = 0.15f + 0.84f * r;   // brightness of the loop
                break;
            case CM_Resampler:
                holdInc = (float) clampv (2.0 * fc / sr, 0.0005, 1.0);
                crushQ = std::pow (2.0f, 16.0f - 12.0f * amount);
                break;
            case CM_AnalogueLP:
                alK = 4.1f * std::pow (r, 0.8f);
                break;
            case CM_DJ:
            {
                // flat in the middle, a low pass to the left, a high pass to the right
                const float a = amount;
                if (a < 0.45f) { djType = 0; djBlend = std::min (1.0f, (0.45f - a) / 0.08f); s1.set (gOf (20000.0 * std::pow (150.0 / 20000.0, (0.45 - a) / 0.45)), std::max (0.05f, k)); }
                else if (a > 0.55f) { djType = 1; djBlend = std::min (1.0f, (a - 0.55f) / 0.08f); s1.set (gOf (20.0 * std::pow (6000.0 / 20.0, (a - 0.55) / 0.45)), std::max (0.05f, k)); }
                else { djType = -1; djBlend = 0.0f; }
                break;
            }
            case CM_Phaser:
            {
                phStages = 2 + 2 * (int) std::lround (amount * 5.0f);
                const float t = gOf (fc);
                phA = (t - 1.0f) / (t + 1.0f);
                phFb = 0.92f * r;
                break;
            }
            case CM_DualPeak:
                s1.set (gOf (fc), std::max (0.02f, k * 0.7f));
                s2.set (gOf (fc * std::pow (2.0, 0.25 + 2.75 * amount)), std::max (0.02f, k * 0.7f));
                break;
            default: break;
        }
        if (model == CM_Shifter) shiftInc = (float) ((amount - 0.5f) * 2.0f * 2000.0f / sr);
        if (model == CM_Ring) ringInc = (float) (fc / sr);
        if (model == CM_LPResample) { holdInc = (float) clampv (500.0 * std::pow ((sr * 0.5) / 500.0, (double) amount) * 2.0 / sr, 0.0005, 1.0); crushQ = 0.0f; }
        if (model == CM_AnalogueLP)
        {
            // a slow random walk on the cutoff (up to about +-25 cents at full Character)
            driftT += 1.0f;
            if (driftT > 64.0f) { driftT = 0.0f; driftTarget = (rnd() * 2.0f - 1.0f); }
            driftPos += (driftTarget - driftPos) * 0.02f;
            const double f = fc * std::pow (2.0, (double) (driftPos * amount * 25.0f) / 1200.0);
            const float g = gOf (f);
            alG = g / (1.0f + g);
        }
    }

    inline float process (float x, int c)
    {
        auto& st = ch[(size_t) c];
        float y = x;
        switch (model)
        {
            case CM_Formant: case CM_Formant2: case CM_VowelMorph:
            {
                float lp, bp, sum = 0.0f;
                s1.tick (x, c, lp, bp, 4.0f); sum += fGain[0] * bp * s1.k;
                s2.tick (x, c, lp, bp, 4.0f); sum += fGain[1] * bp * s2.k;
                s3.tick (x, c, lp, bp, 4.0f); sum += fGain[2] * bp * s3.k;
                y = sum * outGain * (1.0f + 3.0f * r);
                break;
            }
            case CM_AllPass:
            {
                float lp, bp;
                s1.tick (x, c, lp, bp, 4.0f);
                float a = x - 2.0f * s1.k * bp;   // a state-variable all-pass: x - 2k * band
                s2.tick (a, c, lp, bp, 4.0f);
                a = a - 2.0f * s2.k * bp;
                y = 0.5f * (x + a);
                break;
            }
            case CM_PowerLP: case CM_PowerBP:
            {
                const float drive = 1.0f + 4.0f * amount;
                const float v = fastTanh (x * drive) / fastTanh (drive);
                float lp, bp, lp2, bp2;
                s1.tick (v, c, lp, bp, 3.0f);
                s2.tick (model == CM_PowerLP ? lp : bp * s1.k, c, lp2, bp2, 3.0f);
                const float out = model == CM_PowerLP ? lp2 : bp2 * s2.k;
                y = fastTanh (out * (1.0f + 2.5f * r) * 0.8f) * 1.25f;
                break;
            }
            case CM_WaveShaper:
            {
                const float g = 1.0f + 7.0f * amount;
                const float v = std::sin (clampv (x * g, -8.0f, 8.0f) * 1.5707963f) * (amount > 0.0f ? 1.0f : 1.0f);
                const float in = amount <= 0.001f ? x : v;
                float lp, bp;
                s1.tick (in, c, lp, bp, 3.0f);
                y = lp * (1.0f + 0.5f * r);
                break;
            }
            case CM_Resonator: case CM_Comb:
            {
                float fb, sign = 1.0f;
                if (model == CM_Resonator) fb = 0.5f + 0.497f * amount;
                else { sign = amount < 0.5f ? -1.0f : 1.0f; fb = std::abs (amount - 0.5f) * 2.0f * 0.97f; }
                const float d = readDelay (st, delaySamples);
                st.lpz += (d - st.lpz) * damp;
                const float v = x + sign * fb * st.lpz;
                writeDelay (st, softKnee (v, 4.0f));
                y = model == CM_Resonator ? v * std::sqrt (1.0f - fb) * 1.3f : v * (1.0f - 0.5f * fb);
                break;
            }
            case CM_Resampler:
            {
                st.holdPh += holdInc;
                if (st.holdPh >= 1.0f) { st.holdPh -= 1.0f; st.held = std::round (x * crushQ) / crushQ; }
                y = st.held;
                break;
            }
            case CM_LPResample:
            {
                float lp, bp;
                s1.tick (x, c, lp, bp, 3.0f);
                st.holdPh += holdInc;
                if (st.holdPh >= 1.0f) { st.holdPh -= 1.0f; st.held = lp; }
                y = st.held * (1.0f + 0.3f * r);
                break;
            }
            case CM_Shifter:
            {
                // Hilbert pair (two all-pass chains, 90 degrees apart), then mix with a rotating phasor
                const float re = hilbert (st, 0, x), im = hilbert (st, 1, x);
                st.shPh += shiftInc; if (st.shPh >= 1.0f) st.shPh -= 1.0f; if (st.shPh < 0.0f) st.shPh += 1.0f;
                const float w = st.shPh * 6.2831853f;
                const float v = re * std::cos (w) + im * std::sin (w);   // the upper sideband: positive Character shifts up
                float lp, bp;
                s1.tick (v, c, lp, bp, 3.0f);
                y = lp;
                break;
            }
            case CM_AnalogueLP:
            {
                const float heat = 1.0f + 1.5f * amount;
                float v = fastTanh ((x - alK * st.alFb) * heat) / heat;
                for (int k = 0; k < 4; ++k)
                {
                    const float u = (fastTanh (v) - st.al[k]) * alG;
                    v = u + st.al[k];
                    st.al[k] = v + u;
                }
                if (! (std::abs (v) < 32.0f)) { for (auto& a : st.al) a = 0.0f; v = 0.0f; }
                st.alFb = v;
                y = v * (1.0f + alK * 0.3f);
                break;
            }
            case CM_DJ:
            {
                if (djType < 0) { y = x; s1.tick (x, c, st.djLp, st.djBp, 3.0f); break; }   // keep the state moving: no jump when it comes in
                float lp, bp;
                s1.tick (x, c, lp, bp, 3.0f);
                const float f = djType == 0 ? lp : x - s1.k * bp - lp;
                y = x + (f - x) * djBlend;
                break;
            }
            case CM_Phaser:
            {
                float v = x + phFb * st.phLast;
                for (int k = 0; k < phStages; ++k)
                {
                    const float o = phA * v + st.ph[k];
                    st.ph[k] = v - phA * o;
                    v = o;
                }
                v = softKnee (v, 4.0f);
                st.phLast = v;
                y = 0.5f * (x + v);
                break;
            }
            case CM_Ring:
            {
                float lp, bp;
                s1.tick (x, c, lp, bp, 3.0f);
                const float b = bp * s1.k * (1.0f + 2.0f * r);
                st.ringPh += ringInc; if (st.ringPh >= 1.0f) st.ringPh -= 1.0f;
                y = b * (1.0f - amount) + b * std::sin (st.ringPh * 6.2831853f) * amount * 1.4f;
                break;
            }
            case CM_DecimatorLP:
            {
                float lp, bp;
                s1.tick (x, c, lp, bp, 3.0f);
                s1.ic1[c] = std::round (s1.ic1[c] * crushQ) / crushQ;   // the resonance, bit-crushed inside the loop
                s1.ic2[c] = std::round (s1.ic2[c] * crushQ) / crushQ;
                y = lp;
                break;
            }
            case CM_DualPeak:
            {
                float lp, bp, lp2, bp2;
                s1.tick (x, c, lp, bp, 3.0f);
                s2.tick (x, c, lp2, bp2, 3.0f);
                y = (bp * s1.k + bp2 * s2.k) * (1.0f + 1.5f * r) * 0.8f + 0.25f * lp;
                break;
            }
            default: break;
        }
        if (! (std::abs (y) < 64.0f)) { reset(); y = 0.0f; }   // never a runaway
        return y;
    }

private:
    struct Chan
    {
        std::array<float, kDelay> buf {};
        int wpos = 0;
        float lpz = 0.0f, held = 0.0f, holdPh = 1.0f, shPh = 0.0f, ringPh = 0.0f, phLast = 0.0f, alFb = 0.0f, djLp = 0.0f, djBp = 0.0f;
        float ph[12] {}, al[4] {};
        float hx[2][4][2] {}, hy[2][4][2] {};   // the Hilbert pair's all-pass states (input and output history)
        float hOld = 0.0f;                      // path A's one-sample delay
    };
    void vowel (float* f, float* a) const
    {
        static const float male[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
        static const float female[5][3] = { { 850, 1220, 2810 }, { 610, 2330, 2990 }, { 310, 2790, 3310 }, { 590, 920, 2710 }, { 370, 950, 2670 } };
        static const float amp[3] = { 1.0f, 0.55f, 0.32f };
        const auto& tab = model == CM_Formant2 ? female : male;
        float pos = model == CM_VowelMorph ? amount * 5.0f : amount * 4.0f;   // Vowel Morph loops back to A
        int i0 = (int) std::floor (pos); const float t = pos - (float) i0;
        i0 = ((i0 % 5) + 5) % 5; const int i1 = model == CM_VowelMorph ? (i0 + 1) % 5 : std::min (4, i0 + 1);
        for (int k = 0; k < 3; ++k)
        {
            float v0 = tab[i0][k], v1 = tab[i1][k];
            if (model == CM_VowelMorph) { v0 = 0.5f * (male[i0][k] + female[i0][k]); v1 = 0.5f * (male[i1][k] + female[i1][k]); }
            f[k] = v0 * std::pow (v1 / v0, t);   // glide in pitch, not in Hz
            a[k] = amp[k];
        }
    }
    inline float readDelay (const Chan& c, float d) const
    {
        float rp = (float) c.wpos - d;
        while (rp < 0.0f) rp += (float) kDelay;
        const int i0 = (int) rp; const float fr = rp - (float) i0;
        const float a = c.buf[(size_t) (i0 & (kDelay - 1))], b = c.buf[(size_t) ((i0 + 1) & (kDelay - 1))];
        return a + (b - a) * fr;
    }
    inline void writeDelay (Chan& c, float v) { c.buf[(size_t) c.wpos] = v; c.wpos = (c.wpos + 1) & (kDelay - 1); }
    // Olli Niemitalo's 90-degree all-pass pair (second-order sections in z^-2)
    static constexpr float kHa[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    static constexpr float kHb[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    inline float hilbert (Chan& c, int path, float x) const
    {
        // each stage: y[n] = a^2 (x[n] + y[n-2]) - x[n-2]
        const float* coef = path == 0 ? kHa : kHb;
        float v = x;
        for (int k = 0; k < 4; ++k)
        {
            const float a2 = coef[k] * coef[k];
            const float y = a2 * (v + c.hy[path][k][1]) - c.hx[path][k][1];
            c.hx[path][k][1] = c.hx[path][k][0]; c.hx[path][k][0] = v;
            c.hy[path][k][1] = c.hy[path][k][0]; c.hy[path][k][0] = y;
            v = y;
        }
        if (path == 0) { const float o = c.hOld; c.hOld = v; return o; }
        return v;
    }
    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }

    int model = CM_Off;
    double sr = 48000.0;
    float fc = 1000.0f, amount = 0.5f, r = 0.0f;
    Svf s1, s2, s3, s4, s5;
    std::array<Chan, 2> ch {};
    float fGain[3] { 1, 0.5f, 0.3f }, outGain = 1.0f;
    float delaySamples = 100.0f, damp = 0.5f, holdInc = 1.0f, crushQ = 32768.0f, shiftInc = 0.0f, ringInc = 0.0f;
    float alK = 0.0f, alG = 0.5f, driftPos = 0.0f, driftTarget = 0.0f, driftT = 0.0f;
    int djType = -1; float djBlend = 0.0f;
    int phStages = 4; float phA = 0.0f, phFb = 0.0f;
    uint32_t seed = 0x2468ace1u;
};

} // namespace tg
