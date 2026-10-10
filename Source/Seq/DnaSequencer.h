#pragma once
// Sonic DNA Sequencer: 1-32 steps, each a transform type and an amount. The running step
// drives that transform's own set of destinations (Fold moves the folder, Crush the bit
// depth...), crossfaded into the next step by Glide, and is also a Mod Matrix source.
//
// Four patterns (A-D), each with two lanes (lane 2 has its own length, for polymeter). Every
// step also has a probability, a ratchet count (1-4 repeats inside the step), a glide setting
// (global / on / off) and a lock (the generator leaves locked steps alone). Pattern A lane 1
// is the original 32-step sequence, so older patches load into it unchanged.
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "../ModMatrix.h"

namespace tg
{

enum DnaTransform { DT_Off, DT_Fold, DT_Crush, DT_Decimate, DT_Shift, DT_Ring, DT_Fm, DT_Splice, DT_Resonate, DT_Filter,
                    DT_Grain, DT_Blur, DT_Mutate, DT_Octave, DT_COUNT };
inline const char* const kDnaTransformNames[DT_COUNT] = { "-", "Fold", "Crush", "Decimate", "Shift", "Ring", "FM", "Splice",
                                                          "Resonate", "Filter", "Grain", "Blur", "Mutate", "Octave" };

// What each transform moves (normalised offset at amount 100%)
struct DnaTarget { int param; float depth; };
inline const std::vector<DnaTarget>& dnaTransformTargets (int t)
{
    static const std::array<std::vector<DnaTarget>, DT_COUNT> table = []
    {
        std::array<std::vector<DnaTarget>, DT_COUNT> a;
        a[DT_Fold]     = { { P_wmMix, 0.8f }, { P_wmFold, 0.6f }, { P_wmDrive, 0.3f } };
        a[DT_Crush]    = { { P_wmMix, 0.8f }, { P_wmBits, -0.75f } };
        a[DT_Decimate] = { { P_wmMix, 0.8f }, { P_wmDown, 0.6f } };
        a[DT_Shift]    = { { P_arShiftMix, 1.0f }, { P_arShift, 0.2f } };
        a[DT_Ring]     = { { P_arRing, 0.9f } };
        a[DT_Fm]       = { { P_arFm, 0.5f } };
        a[DT_Splice]   = { { P_dnaMix, 0.9f }, { P_dnaAmount, 0.3f } };
        a[DT_Resonate] = { { P_resMix, 0.8f } };
        a[DT_Filter]   = { { P_filterCutoff, -0.45f }, { P_filterRes, 0.2f }, { P_filter2Cutoff, -0.45f }, { P_filter2Res, 0.2f } };   // both filters
        a[DT_Grain]    = { { P_grMix, 0.9f } };
        a[DT_Blur]     = { { P_spBlur, 0.9f } };
        a[DT_Mutate]   = { { P_mutAmount, 0.6f } };
        for (int p : { P_osc1Semi, P_osc2Semi, P_osc3Semi, P_subSemi, P_osc4Semi, P_wt2Semi, P_complexSemi, P_supersawSemi })
            a[DT_Octave].push_back ({ p, 0.5f });   // +12 semitones
        return a;
    }();
    return table[(size_t) juce::jlimit (0, DT_COUNT - 1, t)];
}


struct DnaStep
{
    int type = DT_Off;
    float amount = 0.7f;
    float prob = 1.0f;     // 0..1
    int ratchet = 1;       // 1..4
    int glide = 0;         // 0 = the global Glide, 1 = on, 2 = off
    bool lock = false;     // the generator leaves it alone
    bool operator== (const DnaStep& o) const
    {
        return type == o.type && amount == o.amount && prob == o.prob && ratchet == o.ratchet && glide == o.glide && lock == o.lock;
    }
};

// Generator settings (message thread)
enum DnaGenStyle { DG_Random, DG_Euclid, DG_Variation };
struct DnaGenSettings
{
    int style = DG_Random;
    int seed = 1;
    float fill = 0.6f;        // how many steps it makes active
    float amtMin = 0.4f, amtMax = 1.0f;
    float extras = 0.2f;      // chance of a ratchet / probability on an active step
    float change = 0.3f;      // Variation: how much it changes
    int rotate = 0;           // Euclidean rotation
    uint32_t mask = 0;        // transforms it may use (bit t); 0 = all
};

class DnaSeqStore
{
public:
    static constexpr int kSteps = 32, kPatterns = 4, kLanes = 2, kChainMax = 8;
    DnaSeqStore() { clearAll(); }

    // ---- full access
    DnaStep get (int pat, int lane, int i) const
    {
        const size_t k = idx (pat, lane, i);
        const uint32_t b = bits[k].load (std::memory_order_relaxed);
        DnaStep s;
        s.type = juce::jlimit (0, DT_COUNT - 1, (int) (b & 15u));
        s.ratchet = 1 + (int) ((b >> 4) & 3u);
        s.glide = juce::jlimit (0, 2, (int) ((b >> 6) & 3u));
        s.lock = ((b >> 8) & 1u) != 0;
        s.amount = amt[k].load (std::memory_order_relaxed);
        s.prob = prb[k].load (std::memory_order_relaxed);
        return s;
    }
    void set (int pat, int lane, int i, const DnaStep& s)
    {
        const size_t k = idx (pat, lane, i);
        amt[k].store (juce::jlimit (0.0f, 1.0f, s.amount));
        prb[k].store (juce::jlimit (0.0f, 1.0f, s.prob));
        bits[k].store ((uint32_t) juce::jlimit (0, DT_COUNT - 1, s.type) | ((uint32_t) (juce::jlimit (1, 4, s.ratchet) - 1) << 4)
                       | ((uint32_t) juce::jlimit (0, 2, s.glide) << 6) | ((s.lock ? 1u : 0u) << 8));
        version.fetch_add (1);
    }

    // ---- the original single-lane interface: pattern A, lane 1
    int getType (int i) const { return get (0, 0, i).type; }
    float getAmount (int i) const { return get (0, 0, i).amount; }
    void set (int i, int t, float a) { auto s = get (0, 0, i); s.type = t; s.amount = a; set (0, 0, i, s); }
    juce::String toString() const   // "type:amount,..." (pattern A lane 1, as saved by older versions)
    {
        juce::StringArray a;
        for (int i = 0; i < kSteps; ++i) a.add (juce::String (getType (i)) + ":" + juce::String (getAmount (i), 3));
        return a.joinIntoString (",");
    }
    void fromString (const juce::String& s)   // an older patch: everything else back to the defaults
    {
        clearAll();
        juce::StringArray a; a.addTokens (s, ",", {});
        for (int i = 0; i < kSteps && i < a.size(); ++i)
        {
            DnaStep st;
            st.type = a[i].upToFirstOccurrenceOf (":", false, false).getIntValue();
            st.amount = a[i].fromFirstOccurrenceOf (":", false, false).getFloatValue();
            set (0, 0, i, st);
        }
        version.fetch_add (1);
    }

    void clearAll()
    {
        for (int p = 0; p < kPatterns; ++p) for (int l = 0; l < kLanes; ++l) for (int i = 0; i < kSteps; ++i) set (p, l, i, DnaStep {});
        setChain ("AABA");
        gen = DnaGenSettings {};
        version.fetch_add (1);
    }

    // ---- pattern chain: up to 8 entries of A-D, played one pattern cycle each
    void setChain (const juce::String& c)
    {
        uint32_t packed = 0; int n = 0;
        for (auto ch : c.toUpperCase())
            if (ch >= 'A' && ch <= 'D' && n < kChainMax) packed |= (uint32_t) (ch - 'A') << (2 * n++);
        chain.store (packed | ((uint32_t) n << 16));
        version.fetch_add (1);
    }
    juce::String getChain() const
    {
        juce::String s;
        for (int k = 0; k < chainLength(); ++k) s << juce::String::charToString ((juce::juce_wchar) ('A' + chainAt (k)));
        return s;
    }
    int chainLength() const { return (int) ((chain.load (std::memory_order_relaxed) >> 16) & 15u); }
    int chainAt (int k) const { return (int) ((chain.load (std::memory_order_relaxed) >> (2 * (k % kChainMax))) & 3u); }

    // true when only pattern A lane 1 holds anything beyond type and amount defaults (an older patch)
    bool legacyOnly() const
    {
        const DnaStep def;
        for (int p = 0; p < kPatterns; ++p)
            for (int l = 0; l < kLanes; ++l)
                for (int i = 0; i < kSteps; ++i)
                {
                    const DnaStep s = get (p, l, i);
                    if (p == 0 && l == 0) { if (s.prob < 1.0f || s.ratchet != 1 || s.glide != 0) return false; }
                    else if (! (s.type == def.type && s.amount == def.amount && s.prob == def.prob && s.ratchet == 1 && s.glide == 0)) return false;
                }
        return true;
    }
    bool anyRatchet (int pat) const
    {
        for (int l = 0; l < kLanes; ++l) for (int i = 0; i < kSteps; ++i) if (get (pat, l, i).ratchet > 1) return true;
        return false;
    }

    // ---- everything, for projects, patches and undo
    juce::var toVar() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("v", 2);
        juce::Array<juce::var> pats;
        for (int p = 0; p < kPatterns; ++p)
        {
            auto* po = new juce::DynamicObject();
            for (int l = 0; l < kLanes; ++l)
            {
                juce::StringArray a;
                for (int i = 0; i < kSteps; ++i)
                {
                    const DnaStep s = get (p, l, i);
                    a.add (juce::String (s.type) + ":" + juce::String (s.amount, 3) + ":" + juce::String (s.prob, 3) + ":" + juce::String (s.ratchet)
                           + ":" + juce::String (s.glide) + ":" + (s.lock ? "1" : "0"));
                }
                po->setProperty (l == 0 ? "lane1" : "lane2", a.joinIntoString (","));
            }
            pats.add (juce::var (po));
        }
        root->setProperty ("patterns", pats);
        root->setProperty ("chain", getChain());
        auto* g = new juce::DynamicObject();
        g->setProperty ("style", gen.style); g->setProperty ("seed", gen.seed); g->setProperty ("fill", gen.fill);
        g->setProperty ("min", gen.amtMin); g->setProperty ("max", gen.amtMax); g->setProperty ("extras", gen.extras);
        g->setProperty ("change", gen.change); g->setProperty ("rotate", gen.rotate); g->setProperty ("mask", (int) gen.mask);
        root->setProperty ("generator", juce::var (g));
        return juce::var (root);
    }
    bool fromVar (const juce::var& v)   // false (and nothing changed) when v isn't a version-2 sequence
    {
        if (! v.isObject() || (int) v.getProperty ("v", 0) < 2) return false;
        clearAll();
        if (auto* pats = v["patterns"].getArray())
            for (int p = 0; p < kPatterns && p < pats->size(); ++p)
                for (int l = 0; l < kLanes; ++l)
                {
                    juce::StringArray a; a.addTokens ((*pats)[p][l == 0 ? "lane1" : "lane2"].toString(), ",", {});
                    for (int i = 0; i < kSteps && i < a.size(); ++i)
                    {
                        juce::StringArray f; f.addTokens (a[i], ":", {});
                        DnaStep s;
                        s.type = f[0].getIntValue();
                        if (f.size() > 1) s.amount = f[1].getFloatValue();
                        if (f.size() > 2) s.prob = f[2].getFloatValue();
                        if (f.size() > 3) s.ratchet = f[3].getIntValue();
                        if (f.size() > 4) s.glide = f[4].getIntValue();
                        if (f.size() > 5) s.lock = f[5].getIntValue() != 0;
                        set (p, l, i, s);
                    }
                }
        setChain (v.getProperty ("chain", "AABA").toString());
        const juce::var g = v["generator"];
        if (g.isObject())
        {
            gen.style = juce::jlimit (0, 2, (int) g.getProperty ("style", 0)); gen.seed = (int) g.getProperty ("seed", 1);
            gen.fill = (float) (double) g.getProperty ("fill", 0.6); gen.amtMin = (float) (double) g.getProperty ("min", 0.4);
            gen.amtMax = (float) (double) g.getProperty ("max", 1.0); gen.extras = (float) (double) g.getProperty ("extras", 0.2);
            gen.change = (float) (double) g.getProperty ("change", 0.3); gen.rotate = (int) g.getProperty ("rotate", 0);
            gen.mask = (uint32_t) (int) g.getProperty ("mask", 0);
        }
        version.fetch_add (1);
        return true;
    }
    juce::String toJson() const { return juce::JSON::toString (toVar(), true); }
    void fromJsonOrLegacy (const juce::String& s)
    {
        if (s.trimStart().startsWithChar ('{') && fromVar (juce::JSON::parse (s))) return;
        fromString (s);
    }

    uint32_t getVersion() const { return version.load(); }
    DnaGenSettings gen;   // message thread only

private:
    static size_t idx (int pat, int lane, int i) { return (size_t) (((pat & 3) * kLanes + (lane & 1)) * kSteps + (i & 31)); }
    std::array<std::atomic<uint32_t>, kPatterns * kLanes * kSteps> bits;
    std::array<std::atomic<float>, kPatterns * kLanes * kSteps> amt, prb;
    std::atomic<uint32_t> chain { 0 };
    std::atomic<uint32_t> version { 0 };
};

//==============================================================================
// Deterministic randomness: the same position always gives the same decision, so a song
// bounces the same way twice and a seed always gives the same pattern.
inline uint32_t dnaHash (uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
inline float dnaHash01 (uint32_t a, uint32_t b, uint32_t c) { return (dnaHash (a, b, c) >> 8) * (1.0f / 16777216.0f); }

enum DnaDirection { DD_Forward, DD_Reverse, DD_PingPong, DD_Random };

// Which step of an n-step lane plays at absolute step k
inline int dnaIndex (int64_t k, int n, int dir, uint32_t salt)
{
    if (n <= 1) return 0;
    const int f = (int) (((k % n) + n) % n);
    switch (dir)
    {
        case DD_Reverse:  return n - 1 - f;
        case DD_PingPong: { const int64_t P = 2 * (int64_t) n - 2; const int m = (int) (((k % P) + P) % P); return m < n ? m : (int) (P - m); }
        case DD_Random:   return (int) (dnaHash ((uint32_t) k, (uint32_t) (k >> 32), salt) % (uint32_t) n);
        default:          return f;
    }
}

struct DnaPlayParams
{
    int len[2] { 16, 12 };
    bool lane2 = false;
    float glide = 0.2f;
    int direction = DD_Forward;
    float density = 1.0f;      // 0..1: thins the pattern (1 = every programmed step)
    float probScale = 1.0f;    // 0..2: 1 = as written, 2 = every step always
    int pattern = 0;
    bool chain = false;
};

struct DnaFrame
{
    float w[DT_COUNT] {};
    float value[2] { 0, 0 };   // the blended step amount of each lane (Mod Matrix sources)
    float gate = 0.0f;         // pulse at every step / ratchet hit (Mod Matrix source)
    int step[2] { -1, -1 };
    int pattern = 0;
};

// Ratchet shape inside one repeat: quick rise, hold, fall, gap
inline float dnaRatchetEnv (float u)
{
    if (u < 0.03f) return u / 0.03f;
    if (u < 0.55f) return 1.0f;
    if (u < 0.8f) return 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (u - 0.55f) / 0.25f);
    return 0.0f;
}

// Weights of each transform at position 'pos' (in steps). Glide crossfades into the next step.
// With pattern A, one lane and default step settings this is exactly the original sequencer.
inline void dnaEvaluate (const DnaSeqStore& st, double pos, const DnaPlayParams& pp, DnaFrame& out)
{
    for (int t = 0; t < DT_COUNT; ++t) out.w[t] = 0.0f;
    out.gate = 0.0f;
    const double fl = std::floor (pos);
    const int64_t k = (int64_t) fl;
    const float frac = (float) (pos - fl);
    int pat = juce::jlimit (0, 3, pp.pattern);
    const int n0 = juce::jlimit (1, DnaSeqStore::kSteps, pp.len[0]);
    if (pp.chain && st.chainLength() > 0)
    {
        const int64_t cyc = (k >= 0 ? k : k - n0 + 1) / n0;
        const int cl = st.chainLength();
        pat = st.chainAt ((int) (((cyc % cl) + cl) % cl));
    }
    out.pattern = pat;
    auto probEff = [&] (float p) { return pp.probScale <= 1.0f ? p * pp.probScale : p + (1.0f - p) * (pp.probScale - 1.0f); };
    for (int lane = 0; lane < (pp.lane2 ? 2 : 1); ++lane)
    {
        const int n = juce::jlimit (1, DnaSeqStore::kSteps, pp.len[lane]);
        const uint32_t salt = 0xD1A5u + (uint32_t) lane * 977u;
        const int cur = dnaIndex (k, n, pp.direction, salt), nxt = dnaIndex (k + 1, n, pp.direction, salt);
        const DnaStep sc = st.get (pat, lane, cur), sn = st.get (pat, lane, nxt);
        auto fires = [&] (const DnaStep& s, int i, int64_t kk)
        {
            if (s.type == DT_Off) return false;
            const float pe = probEff (s.prob);
            if (pe < 1.0f && dnaHash01 ((uint32_t) kk, (uint32_t) (kk >> 32), salt ^ 0x51EDu) >= pe) return false;
            if (pp.density < 1.0f && dnaHash01 ((uint32_t) i, (uint32_t) pat, salt ^ 0xDE45u) >= pp.density) return false;
            return true;
        };
        const bool fc = fires (sc, cur, k), fn = fires (sn, nxt, k + 1);
        const float g = sc.glide == 0 ? pp.glide : (sc.glide == 1 ? (pp.glide > 0.0f ? pp.glide : 0.5f) : 0.0f);
        float x = 0.0f;   // share of the next step
        if (g > 0.0f && frac > 1.0f - g)
        {
            const float u = (frac - (1.0f - g)) / g;
            x = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u);
        }
        float env = 1.0f, sub = frac;
        if (sc.ratchet > 1)
        {
            const float r = frac * (float) sc.ratchet;
            sub = r - std::floor (r);
            env = dnaRatchetEnv (sub);
        }
        const float ac = (fc ? sc.amount : 0.0f) * env, an = fn ? sn.amount : 0.0f;
        out.w[sc.type] += (1.0f - x) * ac;
        out.w[sn.type] += x * an;
        const float vc = fc ? ac : 0.0f, vn = fn ? an : 0.0f;
        out.value[lane] = vc + (vn - vc) * x;
        out.step[lane] = cur;
        if (fc) out.gate = std::max (out.gate, std::exp (-5.0f * sub));
    }
    if (! pp.lane2) { out.value[1] = 0.0f; out.step[1] = -1; }
    out.w[DT_Off] = 0.0f;
}

inline void applyDnaSeq (const float* w, float depth, float* v)
{
    const auto& nt = normTable();
    for (int t = 1; t < DT_COUNT; ++t)
    {
        if (w[t] <= 1.0e-5f) continue;
        for (auto& d : dnaTransformTargets (t))
            v[d.param] = plainFast (nt, d.param, normFast (nt, d.param, v[d.param]) + d.depth * w[t] * depth);
    }
}

//==============================================================================
// The sequencer's clock (audio thread): where in the pattern each moment of the block is.
enum DnaSync { DS_Host, DS_Internal, DS_Free };
enum DnaRestart { DR_Never, DR_Note, DR_Bar, DR_HostStart };

// Swing: every second unit is late by swing x unit (0..0.75); 33% is about a triplet shuffle
inline double dnaSwing (double r, double unit, double swing)
{
    if (swing <= 0.0 || unit <= 0.0) return r;
    const double d = swing * unit, P = 2.0 * unit;
    const double c = std::floor (r / P), p = r - c * P;
    const double q = p < unit + d ? p * unit / (unit + d) : unit + (p - unit - d) * unit / (unit - d);
    return c * P + q;
}

struct DnaClock
{
    // set per block by beginBlock
    bool free = false;          // positions in steps (Free Hz) rather than beats
    double v0 = 0.0, vInc = 0.0; // beat (or step) position at the block start, and per sample
    double beatsPerStep = 0.25, swingUnit = 0.25, swing = 0.0, offset = 0.0;
    int restart = DR_Never, barSteps = 16;
    double barOrigin = 0.0, beatsPerBar = 4.0;
    // running state
    double internalBeats = 0.0, freeSteps = 0.0, anchor = 0.0;
    bool wasOn = false, hostWasPlaying = false;

    struct Block
    {
        bool on; int sync, rate, restart; double freeHz, offset, swing; int swingGrid; int barSteps;
        bool hostPlaying; double ppq, bpm, barStartPpq; bool hasBar; double beatsPerBar; double fallbackTempo; double sampleRate;
    };

    void beginBlock (const Block& b)
    {
        static const double bps[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0, 4.0 };
        if (b.on && ! wasOn) { internalBeats = 0.0; freeSteps = 0.0; anchor = 0.0; }
        wasOn = b.on;
        free = b.sync == DS_Free || b.rate >= 8;
        restart = b.restart;
        offset = b.offset;
        swing = juce::jlimit (0.0, 0.75, b.swing);
        barSteps = std::max (1, b.barSteps);
        beatsPerStep = bps[juce::jlimit (0, 7, b.rate)];
        swingUnit = free ? 1.0 : (b.swingGrid == 1 ? 0.5 : 0.25);
        const bool locked = ! free && b.sync == DS_Host && b.hostPlaying && b.bpm > 0.0;
        if (locked && ! hostWasPlaying && restart == DR_HostStart) anchor = b.ppq;
        hostWasPlaying = locked;
        if (free) { v0 = freeSteps; vInc = b.freeHz / b.sampleRate; }
        else if (locked)
        {
            v0 = b.ppq; vInc = b.bpm / 60.0 / b.sampleRate;
            internalBeats = b.ppq;   // carries on from here if the DAW stops
        }
        else
        {
            const double tempo = b.sync == DS_Host && b.bpm > 0.0 ? b.bpm : b.fallbackTempo;
            v0 = internalBeats; vInc = tempo / 60.0 / b.sampleRate;
        }
        beatsPerBar = b.beatsPerBar > 0.0 ? b.beatsPerBar : 4.0;
        barOrigin = locked && b.hasBar ? b.barStartPpq : 0.0;
    }
    double valueAt (double sample) const { return v0 + vInc * sample; }
    void noteOn (double sample) { if (restart == DR_Note) anchor = valueAt (sample); }
    // pattern position (steps) at a sample of the block
    double stepsAt (double sample) const
    {
        const double v = valueAt (sample);
        double a = 0.0;
        switch (restart)
        {
            case DR_Note: case DR_HostStart: a = anchor; break;
            case DR_Bar:
                if (free) a = std::floor (v / barSteps) * barSteps;
                else a = barOrigin + std::floor ((v - barOrigin) / beatsPerBar) * beatsPerBar;
                break;
            default: break;
        }
        const double rel = dnaSwing (v - a, swingUnit, swing);
        return (free ? rel : rel / beatsPerStep) + offset;
    }
    void endBlock (int n)
    {
        if (free) freeSteps += vInc * n;
        else internalBeats = v0 + vInc * n;
    }
};

} // namespace tg
