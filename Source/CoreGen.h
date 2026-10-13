#pragma once
// v0.1.74: making Core slot settings - the Randomise generator (controlled to crazy, by type) and the DX7 voice converter.
// Both fill a CorePlain (the slot's parameters, in their own units), which the processor then writes into the slot.
#include <JuceHeader.h>
#include <cmath>
#include "Params.h"
#include "CoreAlgorithms.h"
#include "Dx7.h"
#include "Registry.h"

namespace tg
{
struct CorePlain
{
    float head[kCoreSlotHead] {};
    float op[kCoreOps][kCoreOpStride] {};
    float ex[CX_COUNT] {};   // (v0.1.75) the slot's LFO and pitch envelope (coreExParam's order)
};
// the LFO and pitch envelope at their defaults (off: no vibrato, no tremolo, no pitch envelope)
inline void setCoreExDefaults (CorePlain& p) { for (int w = 0; w < CX_COUNT; ++w) p.ex[w] = meta (coreExParam (0, w)).def; }

namespace coregen
{
    enum Type { T_Any, T_Bass, T_EPiano, T_Bell, T_Brass, T_Pad, T_Pluck, T_Metallic, T_Noise, T_Lead, T_COUNT };
    inline const char* const kTypeNames[T_COUNT] = { "Any", "Bass", "E-piano", "Bell", "Brass", "Pad", "Pluck", "Metallic", "Noise / FX", "Lead" };
    enum Lock { L_Algorithm = 1, L_Envelopes = 2, L_Ratios = 4, L_Levels = 8 };

    struct Options { int type = T_Any; float crazy = 0.0f; int locks = 0; int64_t seed = 1; };

    // what each type sounds like: its routings (from kCoreAlgorithms), how hard modulators drive, their ratios, envelope times
    struct TypeDef
    {
        std::vector<int> algos;
        float idxLo, idxHi;              // modulation index (radians), controlled
        std::vector<float> modRatios;    // modulators' ratios, controlled
        std::vector<float> carRatios;
        float fbMax;                     // feedback on the top modulator, controlled
        float cA[2], cD[2], cS[2], cR[2];   // carriers: attack, decay, sustain, release (ranges)
        float mA[2], mD[2], mS[2], mR[2];   // modulators
        bool inharmonic;
        int uniMax;                      // unison voices (index into 1 / 2 / 3 / 4 / 6 / 8)
    };
    inline const TypeDef& typeDef (int t)
    {
        static const TypeDef defs[T_COUNT] = {
            { {}, 0, 0, {}, {}, 0, {}, {}, {}, {}, {}, {}, {}, {}, false, 0 },   // (Any: one of the others)
            { { 8, 2, 3, 1 }, 1.0f, 3.0f, { 0.5f, 1, 1, 2, 3 }, { 0.5f, 1 }, 1.2f,
              { 0.001f, 0.005f }, { 0.3f, 0.8f }, { 0.6f, 0.9f }, { 0.05f, 0.2f }, { 0.001f, 0.003f }, { 0.08f, 0.3f }, { 0.1f, 0.4f }, { 0.08f, 0.15f }, false, 0 },
            { { 3, 4, 1 }, 0.5f, 2.2f, { 1, 14, 1, 1 }, { 1 }, 0.3f,
              { 0.001f, 0.002f }, { 1.5f, 3.0f }, { 0.0f, 0.15f }, { 0.3f, 0.6f }, { 0.001f, 0.002f }, { 0.3f, 0.8f }, { 0.0f, 0.1f }, { 0.25f, 0.4f }, false, 0 },
            { { 3, 4, 7, 1 }, 1.5f, 4.0f, { 3.5f, 2.75f, 1.25f, 4.25f, 7 }, { 1, 1, 2 }, 0.3f,
              { 0.001f, 0.002f }, { 2.0f, 6.0f }, { 0.0f, 0.0f }, { 1.0f, 3.0f }, { 0.001f, 0.002f }, { 1.0f, 3.0f }, { 0.0f, 0.05f }, { 0.8f, 1.5f }, true, 0 },
            { { 10, 2 }, 1.5f, 3.5f, { 1 }, { 1 }, 1.5f,
              { 0.03f, 0.08f }, { 0.25f, 0.4f }, { 0.75f, 0.9f }, { 0.12f, 0.2f }, { 0.05f, 0.12f }, { 0.25f, 0.4f }, { 0.6f, 0.8f }, { 0.12f, 0.2f }, false, 1 },
            { { 3, 4, 6, 7 }, 0.3f, 1.5f, { 1, 2, 0.5f }, { 1, 1, 2 }, 0.3f,
              { 0.4f, 1.5f }, { 0.8f, 1.5f }, { 0.75f, 0.9f }, { 1.2f, 3.0f }, { 0.5f, 2.0f }, { 1.0f, 2.0f }, { 0.5f, 0.8f }, { 1.5f, 3.0f }, false, 3 },
            { { 3, 2, 1, 8 }, 1.0f, 4.0f, { 1, 2, 3, 4 }, { 1 }, 0.5f,
              { 0.001f, 0.002f }, { 0.3f, 0.8f }, { 0.0f, 0.0f }, { 0.15f, 0.3f }, { 0.001f, 0.002f }, { 0.05f, 0.2f }, { 0.0f, 0.0f }, { 0.1f, 0.2f }, false, 0 },
            { { 0, 5, 9, 6 }, 3.0f, 7.0f, { 1.75f, 2.25f, 3.25f, 5.5f, 7.75f, 11 }, { 1, 1.75f }, 2.0f,
              { 0.001f, 0.003f }, { 1.0f, 4.0f }, { 0.0f, 0.15f }, { 0.8f, 2.0f }, { 0.001f, 0.003f }, { 0.5f, 2.0f }, { 0.0f, 0.2f }, { 0.5f, 1.5f }, true, 0 },
            { { 0, 5, 9 }, 5.0f, 12.0f, {}, { 1 }, 6.0f,
              { 0.001f, 0.3f }, { 0.1f, 2.0f }, { 0.0f, 0.8f }, { 0.1f, 1.5f }, { 0.001f, 0.5f }, { 0.05f, 2.0f }, { 0.0f, 1.0f }, { 0.1f, 1.5f }, true, 2 },
            { { 1, 2, 10, 9 }, 1.0f, 4.0f, { 1, 2, 3 }, { 1 }, 2.0f,
              { 0.005f, 0.02f }, { 0.25f, 0.4f }, { 0.8f, 0.9f }, { 0.15f, 0.3f }, { 0.01f, 0.03f }, { 0.3f, 0.6f }, { 0.5f, 0.7f }, { 0.15f, 0.3f }, false, 1 } };
        return defs[juce::jlimit (0, T_COUNT - 1, t)];
    }

    // the routing an algorithm writes: edges k > j and the carriers
    inline void algoEdges (int a, bool edge[6][6], bool carrier[6])
    {
        for (int k = 0; k < 6; ++k) { carrier[k] = false; for (int j = 0; j < 6; ++j) edge[k][j] = false; }
        const auto& al = kCoreAlgorithms[juce::jlimit (0, kNumCoreAlgorithms - 1, a)];
        for (const char* e = al.edges; e[0] != 0 && e[1] != 0; )
        {
            const int x = e[0] - '1', y = e[1] - '1';
            if (x >= 0 && x < 6 && y >= 0 && y < 6) edge[x][y] = true;
            e += 2; while (*e == ' ') ++e;
        }
        for (const char* c = al.carriers; *c != 0; ++c) if (*c >= '1' && *c <= '6') carrier[*c - '1'] = true;
    }

    // Randomise: `from` is the slot as it is (for the locks); returns the new settings and, in `what`, a short description
    inline CorePlain randomise (const CorePlain& from, const Options& o, juce::String& what)
    {
        juce::Random rng (o.seed);
        auto rr = [&] (float a, float b) { return a + (b - a) * rng.nextFloat(); };
        auto rlog = [&] (float a, float b) { a = std::max (a, 1.0e-4f); b = std::max (b, a); return std::exp (rr (std::log (a), std::log (b))); };
        const float c = juce::jlimit (0.0f, 1.0f, o.crazy);
        int type = o.type;
        if (type == T_Any) type = 1 + rng.nextInt (T_COUNT - 1);
        const auto& T = typeDef (type);
        CorePlain p = from;
        setCoreExDefaults (p);   // (a new patch: no LFO or pitch envelope)
        // routing
        bool edge[6][6], carrier[6];
        if (o.locks & L_Algorithm)
        {
            for (int k = 0; k < 6; ++k) { carrier[k] = from.op[k][CO_Out] > 0.0f; for (int j = 0; j < 6; ++j) edge[k][j] = j != k && from.op[k][CO_M1 + j] > 0.0f; }
        }
        else
        {
            const int a = c > 0.7f && rng.nextFloat() < c - 0.5f ? rng.nextInt (kNumCoreAlgorithms) : T.algos[(size_t) rng.nextInt ((int) T.algos.size())];
            algoEdges (a, edge, carrier);
            // crazy: extra connections, upwards ones too
            const int extra = (int) std::floor (c * c * 4.0f * rng.nextFloat());
            for (int x = 0; x < extra; ++x) { const int k = rng.nextInt (6), j = rng.nextInt (6); if (k != j) edge[k][j] = true; }
        }
        int nc = 0; for (int k = 0; k < 6; ++k) nc += carrier[k] ? 1 : 0;
        if (nc == 0) { carrier[0] = true; nc = 1; }
        bool used[6] {};
        for (int k = 0; k < 6; ++k) { used[k] = carrier[k]; for (int j = 0; j < 6; ++j) used[k] = used[k] || edge[k][j]; }
        // which operator gets feedback: the first modulator nobody modulates (the top of a stack)
        int fbOp = -1;
        for (int k = 5; k >= 0 && fbOp < 0; --k)
        {
            bool modulated = false; for (int x = 0; x < 6; ++x) modulated = modulated || (x != k && edge[x][k]);
            if (used[k] && ! carrier[k] && ! modulated) fbOp = k;
        }
        if (fbOp < 0) fbOp = 5;
        const float idxScale = 1.0f + 2.0f * c;
        for (int k = 0; k < 6; ++k)
        {
            float* q = p.op[k];
            if (! (o.locks & L_Algorithm))
            {
                for (int j = 0; j < 6; ++j)
                    q[CO_M1 + j] = (j != k && edge[k][j]) ? juce::jlimit (0.0f, 12.0f, rr (T.idxLo, T.idxHi) * idxScale) : 0.0f;
                q[CO_M1 + k] = k == fbOp ? juce::jlimit (0.0f, 12.0f, rr (0.0f, T.fbMax) * (1.0f + 4.0f * c)) : 0.0f;
                q[CO_Out] = carrier[k] ? 1.0f / std::sqrt ((float) nc) : 0.0f;
            }
            if (! (o.locks & L_Ratios))
            {
                q[CO_Wave] = c > 0.6f && rng.nextFloat() < (c - 0.5f) ? (float) rng.nextInt (8) : 0.0f;
                const auto& set = carrier[k] ? T.carRatios : T.modRatios;
                float r = set.empty() ? std::round (rr (0.25f, 16.0f) * 4.0f) / 4.0f : set[(size_t) rng.nextInt ((int) set.size())];
                if (rng.nextFloat() < c * c) r = std::round (rlog (0.25f, 16.0f) * 4.0f) / 4.0f;
                q[CO_Ratio] = juce::jlimit (0.25f, 32.0f, std::max (0.25f, r));
                const bool inh = T.inharmonic || rng.nextFloat() < c;
                q[CO_Fine] = carrier[k] && type == T_Pad ? rr (-7.0f, 7.0f) : (inh ? rr (-30.0f, 30.0f) * (0.5f + c) : 0.0f);
                const bool fixedOp = ! carrier[k] && (type == T_Noise ? rng.nextFloat() < 0.4f : rng.nextFloat() < std::max (0.0f, c - 0.7f));
                q[CO_Fixed] = fixedOp ? 1.0f : 0.0f;
                q[CO_Hz] = fixedOp ? rlog (40.0f, 9000.0f) : 440.0f;
            }
            if (! (o.locks & L_Levels))
            {
                q[CO_Level] = used[k] ? (carrier[k] ? rr (0.85f, 1.0f) : 1.0f) : 0.0f;
                q[CO_Vel] = carrier[k] ? rr (0.1f, 0.4f) : (type == T_EPiano || type == T_Pluck || type == T_Brass ? rr (0.3f, 0.7f) : rr (0.0f, 0.4f));
                q[CO_Key] = carrier[k] ? 0.0f : juce::jlimit (-1.0f, 1.0f, rr (-0.15f, 0.0f) + rr (-0.5f, 0.5f) * c);
            }
            if (! (o.locks & L_Envelopes))
            {
                const float* A = carrier[k] ? T.cA : T.mA; const float* D = carrier[k] ? T.cD : T.mD;
                const float* S = carrier[k] ? T.cS : T.mS; const float* R = carrier[k] ? T.cR : T.mR;
                const float wild = std::pow (2.0f, rr (-3.0f, 3.0f) * c);
                q[CO_Dly] = ! carrier[k] && rng.nextFloat() < c * 0.5f ? rr (0.0f, 0.3f * c) : 0.0f;
                q[CO_Att] = juce::jlimit (0.0f, 10.0f, rlog (A[0], A[1]) * wild);
                q[CO_Hold] = 0.0f;
                q[CO_Dec] = juce::jlimit (0.001f, 10.0f, rlog (D[0], D[1]) * wild);
                q[CO_Sus] = juce::jlimit (0.0f, 1.0f, rr (S[0], S[1]) * (1.0f - c) + rng.nextFloat() * c);
                q[CO_Rel] = juce::jlimit (0.001f, 10.0f, rlog (R[0], R[1]) * wild);
            }
        }
        // the slot: on, sensible level, unison for pads
        p.head[CS_Engine] = 1.0f;
        if (! (o.locks & L_Levels)) p.head[CS_Level] = 0.7f;
        p.head[CS_Bright] = 1.0f; p.head[CS_EnvSpd] = 1.0f; p.head[CS_Retrig] = 0.0f;
        const int uni = T.uniMax > 0 ? rng.nextInt (T.uniMax + 1) : 0;
        p.head[CS_Uni] = (float) juce::jlimit (0, 5, uni + (c > 0.8f ? rng.nextInt (2) : 0));
        p.head[CS_Detune] = rr (6.0f, 16.0f); p.head[CS_Spread] = rr (0.4f, 0.8f);
        // the level check: a heard operator that is loud enough, and one that is still there a moment after the attack
        bool ok = false;
        for (int k = 0; k < 6; ++k)
        {
            float* q = p.op[k];
            if (q[CO_Out] <= 0.0f) continue;
            if (q[CO_Level] < 0.5f) q[CO_Level] = 0.8f;
            if (q[CO_Att] > 2.5f) q[CO_Att] = 2.5f;
            if (q[CO_Sus] < 0.05f && q[CO_Dec] < 0.08f) q[CO_Dec] = 0.08f;
            ok = true;
        }
        if (! ok) { p.op[0][CO_Out] = 1.0f; p.op[0][CO_Level] = 1.0f; p.op[0][CO_Sus] = 0.8f; }
        what = juce::String (kTypeNames[type]) + (c < 0.25f ? ", controlled" : c < 0.75f ? ", wild" : ", crazy") + ", seed " + juce::String (o.seed);
        return p;
    }

    // ---- DX7 voice to Core settings; approximations lists what couldn't be carried over exactly
    inline CorePlain fromDx7 (const dx7::Voice& v, juce::StringArray& approximations)
    {
        CorePlain p;
        setCoreExDefaults (p);
        const auto& al = dx7::kAlgos[juce::jlimit (0, 31, v.algorithm)];
        bool edge[6][6] {}, carrier[6] {};
        for (const char* e = al.edges; e[0] != 0 && e[1] != 0; )
        {
            const int x = e[0] - '1', y = e[1] - '1';
            if (x >= 0 && x < 6 && y >= 0 && y < 6) edge[x][y] = true;
            e += 2; while (*e == ' ') ++e;
        }
        int nc = 0;
        for (const char* c = al.carriers; *c != 0; ++c) if (*c >= '1' && *c <= '6') { carrier[*c - '1'] = true; ++nc; }
        bool ratioClamped = false, keyApprox = false, rateScale = false, risingDecay = false;
        for (int k = 0; k < 6; ++k)
        {
            const auto& o = v.op[k];
            float* q = p.op[k];
            q[CO_Wave] = 0.0f;
            const double peak = dx7::levelGain (o.level[0]);
            q[CO_Level] = (float) juce::jlimit (0.0, 1.0, dx7::levelGain (o.outLevel) * std::max (peak, dx7::levelGain (std::max (o.level[1], o.level[2]))));
            if (o.mode == 1) { q[CO_Fixed] = 1.0f; q[CO_Hz] = (float) juce::jlimit (1.0, 16000.0, dx7::fixedHzOf (o)); q[CO_Ratio] = 1.0f; q[CO_Fine] = 0.0f; }
            else
            {
                const double r = dx7::ratioOf (o);
                const double qq = juce::jlimit (0.25, 32.0, std::max (0.25, std::round (r * 4.0) / 4.0));
                double cents = 1200.0 * std::log2 (r / qq) + (o.detune - 7) * 1.5;
                if (std::abs (cents) > 100.0) { ratioClamped = true; cents = juce::jlimit (-100.0, 100.0, cents); }
                q[CO_Fixed] = 0.0f; q[CO_Hz] = 440.0f; q[CO_Ratio] = (float) qq; q[CO_Fine] = (float) cents;
            }
            q[CO_Vel] = (float) o.kvs / 7.0f;
            // level scaling as one slope per octave: the right side (up the keyboard), or the left side when the right is 0
            float key = 0.0f;
            if (o.rd > 0) key = (o.rc >= 2 ? 1.0f : -1.0f) * (float) o.rd / 99.0f * 0.75f;
            else if (o.ld > 0) key = (o.lc >= 2 ? -1.0f : 1.0f) * (float) o.ld / 99.0f * 0.75f;
            if (o.rd > 0 || o.ld > 0) keyApprox = true;
            q[CO_Key] = juce::jlimit (-1.0f, 1.0f, key);
            rateScale = rateScale || o.rateScale > 0;
            p.ex[CX_Trem1 + k] = (float) dx7::ampModSens (o.ams);
            // the envelope: L4 (start) up to L1 at R1, to L2 at R2, to L3 (sustain) at R3; R4 back to L4 on release
            auto t = [] (int rate, int from, int to) { return dx7::rateSeconds (rate) * std::abs (to - from) / 99.0; };
            const double top = std::max (1.0e-6, dx7::levelGain (std::max (o.level[0], std::max (o.level[1], o.level[2]))));
            q[CO_Dly] = 0.0f; q[CO_Hold] = 0.0f;
            q[CO_Att] = (float) juce::jlimit (0.0, 10.0, t (o.rate[0], o.level[3], o.level[0]));
            q[CO_Dec] = (float) juce::jlimit (0.001, 10.0, t (o.rate[1], o.level[0], o.level[1]) + t (o.rate[2], o.level[1], o.level[2]));
            q[CO_Sus] = (float) juce::jlimit (0.0, 1.0, dx7::levelGain (o.level[2]) / top);
            q[CO_Rel] = (float) juce::jlimit (0.001, 10.0, std::max (0.005, t (o.rate[3], o.level[2], o.level[3])));
            if (o.level[1] > o.level[0] || o.level[2] > o.level[1]) risingDecay = true;
            // the routing: DX7 modulators reach about 4 pi radians at full output (12 here); the carriers are heard
            for (int j = 0; j < 6; ++j) q[CO_M1 + j] = (j != k && edge[k][j]) ? 12.0f : 0.0f;
            q[CO_M1 + k] = (k == al.fbOp - 1 && v.feedback > 0) ? (float) (juce::MathConstants<double>::pi * v.feedback / 7.0) : 0.0f;
            q[CO_Out] = carrier[k] ? 1.0f / std::sqrt ((float) std::max (1, nc)) : 0.0f;
        }
        p.head[CS_Engine] = 1.0f; p.head[CS_Level] = 0.7f;
        const int tr = v.transpose - 24;
        p.head[CS_Oct] = (float) juce::jlimit (-3, 3, tr / 12); p.head[CS_Semi] = (float) juce::jlimit (-12, 12, tr - 12 * (tr / 12)); p.head[CS_Fine] = 0.0f;
        p.head[CS_Uni] = 0.0f; p.head[CS_Detune] = 12.0f; p.head[CS_Spread] = 0.5f;
        p.head[CS_Retrig] = v.oscSync ? 0.0f : 1.0f; p.head[CS_Bright] = 1.0f; p.head[CS_EnvSpd] = 1.0f;
        approximations.add ("each envelope's four rates and levels as Attack / Decay / Sustain / Release");
        if (risingDecay) approximations.add ("envelopes that rise again after the attack (they decay here)");
        if (ratioClamped) approximations.add ("a ratio more than 100 cents from a quarter step (the nearest the Core's Ratio and Fine can reach)");
        if (keyApprox) approximations.add ("keyboard level scaling, as one slope per octave");
        if (rateScale) approximations.add ("keyboard rate scaling (not carried over)");
        // (v0.1.75) the LFO and the pitch envelope, into the slot's own
        p.ex[CX_LfoWave] = (float) juce::jlimit (0, 5, v.lfoWave);
        p.ex[CX_LfoRate] = (float) dx7::lfoHz (v.lfoSpeed);
        p.ex[CX_LfoDelay] = (float) dx7::lfoDelaySeconds (v.lfoDelay);
        p.ex[CX_LfoSync] = v.lfoSync ? 1.0f : 0.0f;
        p.ex[CX_Vib] = (float) juce::jlimit (0.0, 1200.0, dx7::vibratoCents (v.lfoPmd, v.pms));
        p.ex[CX_Trem] = (float) v.lfoAmd / 99.0f;
        bool pitchEnv = false; for (int i = 0; i < 4; ++i) pitchEnv = pitchEnv || v.pitchLevel[i] != 50;
        p.ex[CX_Peg] = pitchEnv ? 1.0f : 0.0f;
        for (int i = 0; i < 4; ++i) p.ex[CX_PegL1 + i] = (float) juce::jlimit (-48.0, 48.0, dx7::pitchLevelSemis (v.pitchLevel[i]));
        for (int i = 0; i < 4; ++i)
        {
            const int from = v.pitchLevel[(i + 3) % 4], to = v.pitchLevel[i];   // (segment 1 starts from Level 4)
            p.ex[CX_PegT1 + i] = (float) juce::jlimit (0.0, 30.0, dx7::pitchSegmentSeconds (v.pitchRate[i], from, to));
        }
        if (v.lfoPmd > 0 || v.lfoAmd > 0)
            approximations.add ("the LFO's speed, delay and depths (from published DX7 behaviour, close but not measured against a DX7); it runs per note "
                                + juce::String (v.lfoSync ? "from the note's start" : "from a random point (the DX7's runs freely across all notes)"));
        if (pitchEnv) approximations.add ("the pitch envelope's rates and levels (approximate; its segments are straight lines in semitones)");
        approximations.add ("the DX7's mod wheel / aftertouch routing isn't in a voice file: to bring vibrato in on the wheel, add a Mod Matrix "
                            "route from Mod Wheel to this slot's Vibrato");
        if (v.feedback > 0) approximations.add ("feedback " + juce::String (v.feedback) + " as " + juce::String (juce::MathConstants<double>::pi * v.feedback / 7.0, 2) + " radians");
        if (v.algorithm == 3 || v.algorithm == 5) approximations.add ("algorithm " + juce::String (v.algorithm + 1) + "'s feedback loop through two operators, on the top one only");
        return p;
    }
} // namespace coregen
} // namespace tg
