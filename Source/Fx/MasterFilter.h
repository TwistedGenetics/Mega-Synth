#pragma once
// Master Filter: a stereo filter at the very end of the chain, after every effect, followed by the
// Output trim. Low pass, high pass, band pass or notch, 12 or 24 dB/oct, with drive and a dry/wet mix.
// Its Model is Clean (the state-variable filter below, as it has always been), one of the 17 classic
// models of Filter 1 and 2, or (Character) one of the character models.
// It is a state-variable filter in the "TPT" form (Zavalishin / Simper): stable however fast the
// cutoff moves, so it can be swept by LFOs and macros without zipper noise or blow-ups.
// Off (the default) it isn't run at all; the Output trim at 0 dB isn't applied at all.
#include <JuceHeader.h>
#include <cmath>
#include <complex>
#include "../Filter/Filter.h"

namespace tg
{

class MasterFilter
{
public:
    enum Type { LP, HP, BP, Notch };

    void prepare (double sampleRate) { sr = sampleRate; scratchL.assign (8192, 0.0f); scratchR.assign (8192, 0.0f); reset(); }
    void reset() { for (auto& st : s) st = {}; primed = false; charUnit.reset(); charMix = 0.0f; charOn = 0; }

    // model: 0 = Clean (the state-variable filter), 1-17 = the classic models (filterMode + 1)
    struct Settings { bool on = false; int type = LP; bool steep = false; float cutoff = 1000.0f, res = 0.1f, driveDb = 0.0f, mix = 1.0f; int charModel = 0; float charAmt = 0.5f; int model = 0; };
    static int unitType (int t) { static const int ft[] = { FT_LP, FT_HP, FT_BP, FT_NOTCH }; return ft[clampv (t, 0, 3)]; }

    void process (float* L, float* R, int n, const Settings& p)
    {
        if (! p.on) { primed = false; charMix = 0.0f; charOn = 0; return; }   // off: untouched (and it starts clean next time)
        if (p.charModel > 0 || p.model > 0 || charMix > 0.0f) { processWithCharacter (L, R, n, p); return; }
        const float cutT = std::log (juce::jlimit (20.0f, (float) (sr * 0.45), p.cutoff));
        const float resT = juce::jlimit (0.0f, 1.0f, p.res), mixT = juce::jlimit (0.0f, 1.0f, p.mix);
        const float drvT = std::pow (10.0f, juce::jlimit (0.0f, 24.0f, p.driveDb) / 20.0f);
        if (! primed) { logCut = cutT; resS = resT; mixS = mixT; drvS = drvT; for (auto& st : s) st = {}; primed = true; }
        const float c = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));   // 20 ms smoothing
        for (int i = 0; i < n; ++i)
        {
            logCut += (cutT - logCut) * c; resS += (resT - resS) * c; mixS += (mixT - mixS) * c; drvS += (drvT - drvS) * c;
            if ((i & 3) == 0) updateCoefs (p.steep);   // coefficients every 4 samples (smoothing keeps them close)
            float* io[2] = { L, R };
            for (int ch = 0; ch < 2; ++ch)
            {
                const float dry = io[ch][i];
                float x = dry;
                if (drvS > 1.0001f) x = std::tanh (drvS * x) / std::tanh (drvS);   // unity at full scale; louder and rounder below it
                float y = tick (s[ch * 2], x, g1, k1, p.type);
                if (p.steep) y = tick (s[ch * 2 + 1], y, g1, k2, p.type);
                io[ch][i] = dry + (y - dry) * mixS;
            }
        }
        if (! std::isfinite (s[0].ic1) || ! std::isfinite (s[2].ic1)) reset();
    }

    // A character model (Formant, Comb, DJ...) or a classic model (Ladder, MS-20, TB-303...): run
    // through a FilterUnit, crossfading from / to the plain filter over 10 ms when it's switched on or off
    void processWithCharacter (float* L, float* R, int n, const Settings& p)
    {
        const int cm = clampv (p.charModel, 0, (int) CM_COUNT - 1);
        const int md = clampv (p.model, 0, 17);
        const bool classicUnit = cm == 0 && md > 0;   // a classic model (a character model wins, as on Filter 1 and 2)
        // what the unit runs: a character model (exactly as before), or the classic model at this type and slope
        const int key = cm > 0 ? cm : (classicUnit ? 1000 + md * 16 + p.type * 2 + (p.steep ? 1 : 0) : 0);
        if (key != 0 && key != charOn)
        {
            if (charOn == 0)
            {
                charCut = juce::jlimit (20.0f, (float) (sr * 0.45), p.cutoff);
                charDrv = std::pow (10.0f, juce::jlimit (0.0f, 24.0f, p.driveDb) / 20.0f); charMixW = juce::jlimit (0.0f, 1.0f, p.mix);
            }
            if (cm > 0) charUnit.configure (0, FT_LP, 1, charOn == 0, cm);
            else charUnit.configure (md - 1, unitType (p.type), p.steep ? 3 : 1, charOn == 0, 0);
            charOn = key;
        }
        const float target = key != 0 ? 1.0f : 0.0f;
        const float step = 1.0f / (0.01f * (float) sr);
        charUnit.setCharacter (p.charAmt);
        if (! classicUnit) charUnit.setDrive (1.0f);   // (a classic model takes the drive itself, below)
        charUnit.setMix (1.0f);
        std::vector<float>& a = scratchL; std::vector<float>& b = scratchR;
        if ((int) a.size() < n) { a.resize ((size_t) n); b.resize ((size_t) n); }
        std::copy (L, L + n, a.begin()); std::copy (R, R + n, b.begin());
        // the plain filter (as when no character is chosen) while crossfading
        if (charMix < 1.0f || target < 1.0f) { Settings plain = p; plain.charModel = 0; processPlain (a.data(), b.data(), n, plain); }
        const float cutT = juce::jlimit (20.0f, (float) (sr * 0.45), p.cutoff);
        const float drvT = std::pow (10.0f, juce::jlimit (0.0f, 24.0f, p.driveDb) / 20.0f), mixT = juce::jlimit (0.0f, 1.0f, p.mix);
        const float c = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
            {
                charCut += (cutT - charCut) * (1.0f - std::pow (1.0f - c, 16.0f)); charUnit.update (charCut, 0.1f + 24.9f * p.res, sr);
                if (classicUnit) charUnit.setDrive (charDrv);   // the model's own input stage (clean at 0 dB)
            }
            charDrv += (drvT - charDrv) * c; charMixW += (mixT - charMixW) * c;
            const float dl = L[i], dr = R[i];
            float xl = dl, xr = dr;
            if (! classicUnit && charDrv > 1.0001f) { xl = std::tanh (charDrv * xl) / std::tanh (charDrv); xr = std::tanh (charDrv * xr) / std::tanh (charDrv); }
            float yl, yr;
            charUnit.processFrame (xl, xr, true, yl, yr);
            yl = dl + (yl - dl) * charMixW; yr = dr + (yr - dr) * charMixW;
            charMix = target > charMix ? std::min (target, charMix + step) : std::max (target, charMix - step);
            L[i] = a[(size_t) i] + (yl - a[(size_t) i]) * charMix;
            R[i] = b[(size_t) i] + (yr - b[(size_t) i]) * charMix;
        }
        if (charMix <= 0.0f && key == 0) { charOn = 0; charUnit.reset(); }
    }
    void processPlain (float* L, float* R, int n, const Settings& p) { Settings q = p; q.charModel = 0; q.model = 0; const float keep = charMix; charMix = 0.0f; process (L, R, n, q); charMix = keep; }

    // magnitude response at f (for drawing the curve), from the same design
    static float magnitudeDb (float f, const Settings& p, double sampleRate)
    {
        const double fc = juce::jlimit (20.0, sampleRate * 0.45, (double) p.cutoff);
        auto stage = [&] (double q)
        {
            // analogue prototype, frequencies pre-warped as the bilinear transform does
            const double wa = std::tan (juce::MathConstants<double>::pi * juce::jmin (f, (float) (sampleRate * 0.499)) / sampleRate);
            const double wc = std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
            const std::complex<double> sj (0.0, wa / wc);
            const auto den = sj * sj + sj / q + 1.0;
            std::complex<double> num;
            switch (p.type)
            {
                case HP: num = sj * sj; break;
                case BP: num = sj / q; break;
                case Notch: num = sj * sj + 1.0; break;
                default: num = 1.0; break;
            }
            return std::abs (num / den);
        };
        double m = p.steep ? stage (q1For (p.res, true)) * stage (q2For (p.res)) : stage (q1For (p.res, false));
        return (float) (20.0 * std::log10 (std::max (1.0e-9, m)));
    }

private:
    struct State { float ic1 = 0.0f, ic2 = 0.0f; };
    // 12 dB: Q from 0.71 (no peak) up to about 18. 24 dB: a 4th-order Butterworth pair, with the
    // resonance on the second stage.
    static double q1For (float res, bool steep) { return steep ? 0.5412 : 0.7071 * std::pow (25.0, (double) juce::jlimit (0.0f, 1.0f, res)); }
    static double q2For (float res) { return 1.3066 * std::pow (14.0, (double) juce::jlimit (0.0f, 1.0f, res)); }
    void updateCoefs (bool steep)
    {
        const double fc = std::exp ((double) logCut);
        g1 = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
        k1 = (float) (1.0 / q1For (resS, steep));
        k2 = (float) (1.0 / q2For (resS));
    }
    static inline float tick (State& st, float x, float g, float k, int type)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - st.ic2;
        const float v1 = a1 * st.ic1 + a2 * v3;
        const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
        st.ic1 = 2.0f * v1 - st.ic1;
        st.ic2 = 2.0f * v2 - st.ic2;
        switch (type)
        {
            case HP: return x - k * v1 - v2;
            case BP: return k * v1;            // unity at the centre
            case Notch: return x - k * v1;
            default: return v2;
        }
    }
    double sr = 48000.0;
    State s[4];   // L stage 1, L stage 2, R stage 1, R stage 2
    float logCut = 0.0f, resS = 0.0f, mixS = 1.0f, drvS = 1.0f, g1 = 0.1f, k1 = 1.4f, k2 = 0.8f;
    bool primed = false;
    // character models
    FilterUnit charUnit;
    int charOn = 0;   // what the unit is set up for: 0 none, 1-18 a character model, 1000+ a classic model / type / slope
    float charMix = 0.0f, charCut = 1000.0f, charDrv = 1.0f, charMixW = 1.0f;
    std::vector<float> scratchL, scratchR;
};

// The Output trim: the last thing before the plug-in's output
class OutputTrim
{
public:
    void prepare (double sampleRate) { sr = sampleRate; primed = false; }
    void process (float* L, float* R, int n, float db)
    {
        const float target = std::abs (db) < 1.0e-3f ? 1.0f : std::pow (10.0f, juce::jlimit (-24.0f, 12.0f, db) / 20.0f);   // (0 dB exactly, whatever the rounding)
        if (! primed) { g = target; primed = true; }
        if (target == 1.0f && g == 1.0f) return;   // 0 dB: not applied at all
        const float c = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            g += (target - g) * c;
            if (std::abs (target - g) < 1.0e-6f) g = target;
            L[i] *= g; R[i] *= g;
        }
    }
private:
    double sr = 48000.0;
    float g = 1.0f;
    bool primed = false;
};

} // namespace tg
