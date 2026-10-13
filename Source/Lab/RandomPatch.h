#pragma once
// The random patch generator: a whole new patch from a seed, steered by a category and a complexity,
// using only the engines it's allowed. Everything here is a pure function of the options and the seed
// (and how many files the sample folders hold), so the same seed always gives the same patch. The
// processor applies the result, loads the picked samples and matches the level (PluginProcessor.cpp).
#include <JuceHeader.h>
#include "../Params.h"
#include "../PatchMeta.h"
#include "../CoreAlgorithms.h"
#include "../Presets/Factory.h"
#include "../Break/BreakTools.h"
#include "../LfoShapes.h"

namespace tg
{
namespace randompatch
{

// the "Use" tick boxes (and No oscillators, which is the opposite way round)
enum Use { U_Wavetables, U_Complex, U_SuperSaw, U_Granular, U_Reese, U_Core, U_Breaks, U_NoteMut, U_BusMut, U_Effects, U_ModMatrix, U_Sequencers, U_ArpChord, U_COUNT };
inline const char* const kUseNames[U_COUNT] = { "Wavetables", "Complex", "SuperSaw", "Granular Synth", "Reese", "Core A / B", "Break Chopper",
                                                "Note Mutation", "Bus Mutation", "Effects", "Mod Matrix", "Sequencers", "Arp / Chord" };
inline bool useDefault (int u) { return u != U_Breaks && u != U_Sequencers; }
// "Keep" groups: the 13 Mutate lock groups, then the samples and the breaks
constexpr int kKeepSamples = 13, kKeepBreaks = 14, kKeepCount = 15;
inline const char* keepName (int k)
{
    static const char* const n[kKeepCount] = { "Pitch", "Oscillators", "Wavetables", "Levels", "Filter", "Envelopes", "Modulation", "Effects",
                                               "DNA Splice", "Wave / Audio-rate", "Resonator", "Granular / Spectral", "Feedback", "Samples", "Breaks" };
    return n[juce::jlimit (0, kKeepCount - 1, k)];
}
// categories: the browser's (without Uncategorised); -1 = Any
constexpr int kNumCategories = 10;
enum Cat { C_Reese, C_Sub, C_Neuro, C_Hoover, C_Stabs, C_Pads, C_Leads, C_Breaks, C_Fx, C_Keys };
inline juce::String categoryName (int c) { return patchCategories()[juce::jlimit (0, kNumCategories - 1, c)]; }
inline const char* const kComplexityNames[3] = { "Simple", "Medium", "Wild" };

struct Options
{
    int category = -1;          // -1 Any
    int complexity = 1;         // 0 Simple, 1 Medium, 2 Wild
    uint32_t seed = 1;
    bool noOsc = false;
    bool use[U_COUNT] {};
    uint32_t keepMask = 0;      // bit k = keep group k (kKeepCount groups)
    bool keepCurrent = false;   // the new patch in B, the current sound in A
    Options() { for (int u = 0; u < U_COUNT; ++u) use[u] = useDefault (u); }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("category", category); o->setProperty ("complexity", complexity); o->setProperty ("seed", (juce::int64) seed);
        o->setProperty ("noOsc", noOsc); o->setProperty ("keep", (juce::int64) keepMask); o->setProperty ("keepCurrent", keepCurrent);
        juce::Array<juce::var> u; for (bool b : use) u.add (b);
        o->setProperty ("use", u);
        return juce::var (o);
    }
    void fromVar (const juce::var& v)
    {
        *this = Options();
        if (! v.isObject()) return;
        category = juce::jlimit (-1, kNumCategories - 1, (int) v.getProperty ("category", -1));
        complexity = juce::jlimit (0, 2, (int) v.getProperty ("complexity", 1));
        seed = (uint32_t) (juce::int64) v.getProperty ("seed", 1);
        noOsc = (bool) v.getProperty ("noOsc", false);
        keepMask = (uint32_t) (juce::int64) v.getProperty ("keep", 0);
        keepCurrent = (bool) v.getProperty ("keepCurrent", false);
        if (auto* a = v.getProperty ("use", {}).getArray()) for (int i = 0; i < U_COUNT && i < a->size(); ++i) use[i] = (bool) (*a)[i];
    }
};

// a small, platform-independent random sequence (xorshift64*)
struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull) { if (s == 0) s = 1; for (int i = 0; i < 4; ++i) next(); }
    uint32_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return (uint32_t) ((s * 0x2545F4914F6CDD1Dull) >> 32); }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float range (float a, float b) { return a + (b - a) * uni(); }
    float logRange (float a, float b) { return a * std::pow (b / a, uni()); }
    int pick (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
    bool chance (float p) { return uni() < p; }
    template <typename T> T of (std::initializer_list<T> l) { return *(l.begin() + pick ((int) l.size())); }
};

// what the generator decided
struct Result
{
    bool ok = false;
    juce::String error;
    int category = 0;
    PresetBuilder b;
    juce::String name;
    juce::StringArray engines;          // what makes the sound, in words
    // samples: an index into the sorted file list, or -1 (none); with no folder, a made-up tone / the built-in breaks
    int wtFile[2] { -1, -1 }, granFile = -1;
    bool wtUsed[2] { false, false }, granUsed = false;
    int nBreaks = 0;                    // 0-3 breaks to load
    int breakFile[3] { -1, -1, -1 };    // file index, or -1 = a synthesised break (demo variant breakDemoVar[k])
    int breakDemoVar[3] { 0, 1, 2 };
    int breakStretch[3] { 1, 1, 1 }, breakColour[3] { 0, 0, 0 };
    // after the breaks are sliced: which pattern tools to run on patterns A-D (-1 = none) and the chain
    int patTool[4] { -1, -1, -1, -1 };
    juce::String chain;
    uint32_t toolSeed = 1;
    int fillTool = -1;   // the fill written into Pattern H (the Fill Pattern), from the pattern A of the break; -1: none
    juce::StringArray notes;            // fallbacks and such, for the page
};

namespace detail
{
    using B = PresetBuilder;

    inline void quietDefaults (B& b)
    {
        // the builder starts from the parameter defaults with the oscillators silent; the generator also
        // starts the effects dry (the defaults have some reverb, delay and chorus) and decides them itself
        b.set (P_reverbMix, 0.0f).set (P_delayMix, 0.0f).set (P_chorusMix, 0.0f).set (P_shimmerMix, 0.0f).set (P_reverseMix, 0.0f)
         .set (P_analogDrift, 0.15f).set (P_masterVolume, 0.25f).set (P_wheelLfo1, 0.0f);
    }

    // the Core: an algorithm into the builder's matrix (as Algorithm... does), plus levels
    inline void coreAlgorithm (B& b, int slot, int algorithm)
    {
        const auto& al = kCoreAlgorithms[juce::jlimit (0, kNumCoreAlgorithms - 1, algorithm)];
        bool edge[6][6] {}, carrier[6] {}, used[6] {};
        for (const char* e = al.edges; e[0] != 0 && e[1] != 0; )
        {
            const int a = e[0] - '1', c = e[1] - '1';
            if (a >= 0 && a < 6 && c >= 0 && c < 6) { edge[a][c] = true; used[a] = used[c] = true; }
            e += 2; while (*e == ' ') ++e;
        }
        int nc = 0;
        for (const char* c = al.carriers; *c != 0; ++c) if (*c >= '1' && *c <= '6') { carrier[*c - '1'] = true; used[*c - '1'] = true; ++nc; }
        for (int k = 0; k < 6; ++k)
        {
            for (int j = 0; j < 6; ++j) if (j != k) b.set (coreOpParam (slot, k, CO_M1 + j), edge[k][j] ? 2.0f : 0.0f);
            b.set (coreOpParam (slot, k, CO_Out), carrier[k] ? 1.0f / std::sqrt ((float) std::max (1, nc)) : 0.0f);
            b.set (coreOpParam (slot, k, CO_Level), used[k] ? (carrier[k] ? 1.0f : 0.5f) : 0.0f);
        }
    }
    inline bool coreIsCarrier (const B& b, int slot, int k) { return b.v[(size_t) coreOpParam (slot, k, CO_Out)] > 0.0f; }
    inline bool coreIsUsed (const B& b, int slot, int k) { return b.v[(size_t) coreOpParam (slot, k, CO_Level)] > 0.0f; }
}

// The patch for these options. nBreakFiles / nSampleFiles: how many usable files the folders hold (0 = none or no
// folder). attempt: a retry after a silent result (the same seed, a different draw).
inline Result generate (const Options& o, int nBreakFiles, int nSampleFiles, int attempt = 0)
{
    using namespace detail;
    Result res;
    Rng r ((uint64_t) o.seed * (uint64_t) 2654435761u + (uint64_t) attempt * (uint64_t) 0x51ED27u + (uint64_t) 7);
    B& b = res.b;
    quietDefaults (b);
    const int cx = juce::jlimit (0, 2, o.complexity);
    const int cat = o.category >= 0 ? juce::jlimit (0, kNumCategories - 1, o.category) : r.pick (kNumCategories);
    res.category = cat;
    const bool bassCat = cat == C_Reese || cat == C_Sub || cat == C_Neuro;

    // ---- the sources: what may play, then what this category likes
    enum Src { S_Classic, S_Wavetable, S_Complex, S_SuperSaw, S_Granular, S_Reese, S_Core, S_Breaks, S_COUNT };
    bool allow[S_COUNT] = { ! o.noOsc, o.use[U_Wavetables], o.use[U_Complex], o.use[U_SuperSaw], o.use[U_Granular], o.use[U_Reese], o.use[U_Core], o.use[U_Breaks] };
    bool anyAllowed = false; for (bool a : allow) anyAllowed |= a;
    if (! anyAllowed) { res.error = "Nothing is allowed to make a sound: tick at least one source (or untick No oscillators)."; return res; }
    //                         Classic WT  Complex SSaw Gran Reese Core Breaks
    static const float w[kNumCategories][S_COUNT] = {
        /* Reese  */          { 3,   0.5f, 0.5f, 0,   0,   6,   1,   0 },
        /* Sub    */          { 6,   0,    0,    0,   0,   1,   1.5f, 0 },
        /* Neuro  */          { 3,   2,    3,    0,   0,   2,   4,   0 },
        /* Hoover */          { 4,   0.5f, 0.5f, 3,   0,   1,   1,   0 },
        /* Stabs  */          { 4,   1,    1,    2,   0,   0.5f, 3,  0 },
        /* Pads   */          { 3,   3,    0.5f, 3,   3,   0.5f, 2,  0 },
        /* Leads  */          { 4,   1,    2,    1,   0,   0.5f, 3,  0 },
        /* Breaks */          { 0.5f, 0,   0,    0,   0,   0,   0.5f, 8 },
        /* FX     */          { 2,   2,    3,    1,   3,   0.5f, 3,  0 },
        /* Keys   */          { 3,   1,    0.5f, 0,   0.5f, 0,  5,   0 } };
    int nSrc = cx == 0 ? 1 : (cx == 1 ? 1 + (r.chance (0.5f) ? 1 : 0) : 2 + (r.chance (0.5f) ? 1 : 0));
    bool chosen[S_COUNT] {};
    for (int n = 0; n < nSrc; ++n)
    {
        float tot = 0; for (int s = 0; s < S_COUNT; ++s) if (allow[s] && ! chosen[s]) tot += w[cat][s];
        int pickS = -1;
        if (tot > 0.0f)
        {
            float x = r.uni() * tot;
            for (int s = 0; s < S_COUNT; ++s) if (allow[s] && ! chosen[s] && w[cat][s] > 0.0f) { x -= w[cat][s]; if (x <= 0.0f) { pickS = s; break; } }
            if (pickS < 0) for (int s = S_COUNT - 1; s >= 0; --s) if (allow[s] && ! chosen[s] && w[cat][s] > 0.0f) { pickS = s; break; }
        }
        else if (n == 0)
        {
            // nothing this category likes is allowed: any allowed source (not the breaks, unless that's all there is)
            std::vector<int> any; for (int s = 0; s < S_COUNT; ++s) if (allow[s] && s != S_Breaks) any.push_back (s);
            if (any.empty()) any.push_back (S_Breaks);
            pickS = any[(size_t) r.pick ((int) any.size())];
        }
        if (pickS < 0) break;
        chosen[pickS] = true;
    }
    // the breaks bring a sine sub on the other keys when the oscillators may play; a jungle bass may get a break under it
    if (chosen[S_Breaks] && cat == C_Breaks && allow[S_Classic]) chosen[S_Classic] = true;
    if (! chosen[S_Breaks] && allow[S_Breaks] && cat != C_Breaks && cx >= 1 && r.chance (0.3f)) chosen[S_Breaks] = true;

    // ---- the voice: envelopes and polyphony for the category
    switch (cat)
    {
        case C_Reese: case C_Sub: case C_Neuro:
            b.set (P_polyphony, 1).set (P_porta, r.chance (0.3f) ? r.range (0.02f, 0.08f) : 0.0f)
             .set (P_ampA, 0.003f).set (P_ampD, 0.3f).set (P_ampS, 1.0f).set (P_ampR, r.range (0.08f, 0.2f)).set (P_bassKeep, 0.8f);
            break;
        case C_Hoover:
            b.set (P_polyphony, r.chance (0.5f) ? 1 : 8).set (P_ampA, r.range (0.003f, 0.02f)).set (P_ampD, 0.4f).set (P_ampS, 1.0f).set (P_ampR, r.range (0.2f, 0.4f));
            if (r.chance (0.7f)) b.set (P_pEnvAmt, -r.range (5.0f, 12.0f)).set (P_pEnvA, 0.0f).set (P_pEnvD, r.range (0.08f, 0.25f));
            break;
        case C_Stabs:
            b.set (P_polyphony, 8).set (P_ampA, 0.001f).set (P_ampD, r.range (0.25f, 0.6f)).set (P_ampS, 0.0f).set (P_ampR, 0.3f)
             .set (P_fEnvA, 0.001f).set (P_fEnvD, r.range (0.12f, 0.35f)).set (P_fEnvS, 0.0f).set (P_fEnvAmt, r.range (1500.0f, 5000.0f));
            break;
        case C_Pads:
            b.set (P_polyphony, 8).set (P_ampA, r.range (0.4f, 2.0f)).set (P_ampD, 1.0f).set (P_ampS, 0.85f).set (P_ampR, r.range (1.5f, 4.0f)).set (P_fEnvAmt, 0.0f);
            break;
        case C_Leads:
            b.set (P_polyphony, 1).set (P_porta, r.chance (0.4f) ? r.range (0.03f, 0.12f) : 0.0f)
             .set (P_ampA, r.range (0.005f, 0.05f)).set (P_ampD, 0.4f).set (P_ampS, 0.8f).set (P_ampR, r.range (0.2f, 0.5f));
            break;
        case C_Breaks:
            b.set (P_polyphony, 1).set (P_ampA, 0.004f).set (P_ampS, 1.0f).set (P_ampR, 0.12f);
            break;
        case C_Fx:
            b.set (P_polyphony, 4).set (P_ampA, r.range (0.2f, 2.0f)).set (P_ampD, 1.0f).set (P_ampS, 0.9f).set (P_ampR, r.range (1.0f, 3.0f));
            if (r.chance (0.6f)) b.set (P_pEnvAmt, r.of ({ -24.0f, -12.0f, 12.0f, 24.0f })).set (P_pEnvA, r.chance (0.5f) ? r.range (0.5f, 2.0f) : 0.0f).set (P_pEnvD, r.range (0.8f, 3.0f));
            break;
        default:   // Keys & Plucks
            b.set (P_polyphony, 8).set (P_ampA, 0.001f).set (P_ampD, r.range (0.5f, 1.5f)).set (P_ampS, r.range (0.0f, 0.3f)).set (P_ampR, r.range (0.4f, 1.0f))
             .set (P_fEnvA, 0.001f).set (P_fEnvD, r.range (0.2f, 0.6f)).set (P_fEnvS, 0.0f).set (P_fEnvAmt, r.range (800.0f, 3000.0f));
            break;
    }
    const float bassOct = bassCat ? -1.0f : 0.0f;

    // ---- the filter
    {
        int model = 0; int ch = 0; float cut = 2000, reso = 1.5f, drive = 1.0f;
        switch (cat)
        {
            case C_Reese:  model = r.of ({ 2, 13, 2, 10 }); cut = r.logRange (400, 1300); reso = r.range (1.0f, 4.0f); drive = r.range (1.0f, 3.0f); break;
            case C_Sub:    model = r.of ({ 0, 1, 4 });      cut = r.logRange (200, 600);  reso = r.range (0.5f, 1.5f); break;
            case C_Neuro:
                model = r.of ({ 2, 9, 10, 3, 6 }); cut = r.logRange (600, 3000); reso = r.range (2.0f, 9.0f); drive = r.range (1.5f, 6.0f);
                if (r.chance (0.5f)) ch = r.of ({ (int) CM_Comb, (int) CM_Formant, (int) CM_VowelMorph, (int) CM_PowerBP, (int) CM_WaveShaper, (int) CM_Phaser });
                break;
            case C_Hoover: model = r.of ({ 2, 5, 6, 13 }); cut = r.logRange (1500, 5000); reso = r.range (1.0f, 4.0f); break;
            case C_Stabs:  model = r.of ({ 2, 11, 13, 0 }); cut = r.logRange (800, 3000); reso = r.range (1.5f, 5.0f); break;
            case C_Pads:   model = r.of ({ 4, 1, 11, 0 });  cut = r.logRange (1200, 6000); reso = r.range (0.8f, 3.0f); break;
            case C_Leads:  model = r.of ({ 13, 2, 9, 12 }); cut = r.logRange (1000, 4000); reso = r.range (1.5f, 6.0f); break;
            case C_Breaks: model = 0; cut = 900; reso = 1.0f; break;
            case C_Fx:
                model = r.pick (17); cut = r.logRange (500, 6000); reso = r.range (1.0f, 8.0f);
                if (r.chance (0.4f)) ch = r.of ({ (int) CM_Shifter, (int) CM_Ring, (int) CM_Resampler, (int) CM_Comb, (int) CM_DualPeak });
                break;
            default:       model = r.of ({ 11, 0, 4 });     cut = r.logRange (1500, 6000); reso = r.range (0.8f, 3.0f); break;
        }
        b.set (P_filterMode, (float) model).set (P_filterCutoff, cut).set (P_filterRes, std::min (reso, 12.0f)).set (P_filterDrive, std::min (drive, 8.0f));
        if (ch > 0) b.set (P_flt1Char, (float) ch).set (P_flt1CharAmt, r.range (0.2f, 0.7f));
        if (cat == C_Reese) b.set (P_fEnvAmt, r.chance (0.5f) ? r.range (300.0f, 1500.0f) : 0.0f).set (P_fEnvD, 0.3f);
        // a second filter, sometimes
        if (cx >= 1 && r.chance (cx == 2 ? 0.4f : 0.15f) && cat != C_Sub && cat != C_Breaks)
            b.set (P_filter2On, 1.0f).set (P_filterRouting, r.chance (0.6f) ? 0.0f : 1.0f).set (P_filter2Mode, (float) r.of ({ 0, 4, 2 }))
             .set (P_filter2Type, (float) r.of ({ 1, 2, 3 })).set (P_filter2Cutoff, r.logRange (150, 2000)).set (P_filter2Res, r.range (1.0f, 6.0f));
    }

    // ---- each source
    if (chosen[S_Classic])
    {
        const bool breaksSub = chosen[S_Breaks] && cat == C_Breaks;
        if (breaksSub || cat == C_Sub)
        {
            // (with the breaks, the sub plays above the slices, two octaves down: A3 sounds A1)
            b.set (P_osc1Wave, (float) r.of ({ 3, 3, 2 })).set (P_osc1Gain, 0.8f).set (P_osc1Oct, breaksSub ? -2.0f : -1.0f);
            if (! breaksSub && r.chance (0.35f)) b.set (P_pEnvAmt, r.range (12.0f, 24.0f)).set (P_pEnvA, 0.0f).set (P_pEnvD, r.range (0.05f, 0.15f)).set (P_pEnvTarget, 1.0f);
            res.engines.add ("a sine sub");
        }
        else
        {
            static const int sawLike[] = { 0, 1, 4, 5, 6 };
            switch (cat)
            {
                case C_Reese:
                    b.set (P_osc1Wave, 0).set (P_osc2Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Gain, 0.6f).set (P_osc2Detune, r.range (10.0f, 25.0f) * (r.chance (0.5f) ? 1 : -1))
                     .set (P_osc1Oct, -1).set (P_osc2Oct, -1).set (P_subWave, 1).set (P_subGain, r.range (0.3f, 0.6f));
                    break;
                case C_Neuro:
                    b.set (P_osc1Wave, (float) r.of ({ 0, 1, 8, 9 })).set (P_osc2Wave, (float) r.of ({ 4, 5, 8, 1 })).set (P_osc1Gain, 0.7f).set (P_osc2Gain, r.range (0.2f, 0.5f))
                     .set (P_osc2Detune, r.range (-12.0f, 12.0f)).set (P_osc1Oct, -1).set (P_osc2Oct, r.of ({ -1.0f, 0.0f }));
                    break;
                case C_Hoover:
                    b.set (P_osc1Wave, 0).set (P_osc2Wave, 0).set (P_osc3Wave, 4).set (P_osc1Gain, 0.6f).set (P_osc2Gain, 0.45f).set (P_osc3Gain, 0.3f)
                     .set (P_osc2Semi, 7).set (P_osc3Oct, -1).set (P_uni1Voices, (float) (4 + r.pick (4))).set (P_uni1Detune, r.range (30.0f, 60.0f)).set (P_uni1Spread, 0.8f);
                    break;
                case C_Pads:
                    b.set (P_osc1Wave, (float) r.of ({ 0, 2, 7 })).set (P_osc2Wave, (float) r.of ({ 2, 3, 7 })).set (P_osc1Gain, 0.6f).set (P_osc2Gain, 0.4f).set (P_osc2Oct, 1)
                     .set (P_uni1Voices, (float) (3 + r.pick (4))).set (P_uni1Detune, r.range (15.0f, 30.0f)).set (P_uni1Spread, 0.9f);
                    break;
                case C_Leads:
                    b.set (P_osc1Wave, (float) r.of ({ 1, 4, 0, 5 })).set (P_osc2Wave, (float) r.of ({ 0, 1, 3 })).set (P_osc1Gain, 0.7f).set (P_osc2Gain, r.range (0.15f, 0.35f))
                     .set (P_osc2Semi, r.of ({ 0.0f, 7.0f, 12.0f })).set (P_osc2Detune, r.range (-8.0f, 8.0f));
                    break;
                case C_Keys:
                    b.set (P_osc1Wave, (float) r.of ({ 2, 3, 7 })).set (P_osc2Wave, 3).set (P_osc1Gain, 0.7f).set (P_osc2Gain, r.range (0.1f, 0.3f)).set (P_osc2Oct, 1);
                    break;
                default:   // Stabs, FX
                    b.set (P_osc1Wave, (float) sawLike[r.pick (5)]).set (P_osc2Wave, (float) sawLike[r.pick (5)]).set (P_osc1Gain, 0.6f).set (P_osc2Gain, 0.45f)
                     .set (P_osc2Detune, r.range (4.0f, 12.0f)).set (P_osc1Oct, cat == C_Fx ? (float) r.of ({ -1, 0, 1 }) : 0.0f);
                    break;
            }
            res.engines.add ("oscillators");
        }
    }
    if (chosen[S_Wavetable])
    {
        res.wtUsed[0] = true;
        res.wtFile[0] = nSampleFiles > 0 ? r.pick (nSampleFiles) : -1;
        b.set (P_osc4Gain, 0.6f).set (P_osc4Oct, bassOct).set (P_osc4Position, r.uni()).set (P_osc4Normalize, 1);
        if (cx == 2 && r.chance (0.5f))
        {
            res.wtUsed[1] = true;
            res.wtFile[1] = nSampleFiles > 0 ? r.pick (nSampleFiles) : -1;
            b.set (P_wt2Gain, 0.4f).set (P_wt2Oct, bassOct).set (P_wt2Position, r.uni()).set (P_wt2Normalize, 1).set (P_wt2Detune, r.range (-10.0f, 10.0f));
        }
        res.engines.add ("a wavetable");
    }
    if (chosen[S_Complex])
    {
        b.set (P_complexGain, 0.5f).set (P_complexWaveA, (float) r.pick (10)).set (P_complexWaveB, (float) r.pick (10)).set (P_complexOct, bassOct)
         .set (P_complexRatio, r.of ({ 0.5f, 1.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f })).set (P_complexFm, r.logRange (40.0f, cat == C_Neuro ? 900.0f : 500.0f))
         .set (P_complexShape, r.range (1.5f, cat == C_Neuro ? 12.0f : 6.0f)).set (P_complexMix, r.range (0.3f, 0.8f));
        res.engines.add ("the Complex oscillator");
    }
    if (chosen[S_SuperSaw])
    {
        b.set (P_supersawGain, 0.5f).set (P_supersawVoices, (float) ((cat == C_Pads || cat == C_Stabs) ? 4 + r.pick (3) : 5 + r.pick (5))).set (P_supersawSpread, r.range (15.0f, 40.0f))
         .set (P_supersawStereo, bassCat ? 0.2f : r.range (0.5f, 1.0f)).set (P_supersawDrift, r.range (2.0f, 6.0f)).set (P_supersawOct, bassOct);
        res.engines.add ("the SuperSaw");
    }
    if (chosen[S_Granular])
    {
        res.granUsed = true;
        res.granFile = nSampleFiles > 0 ? r.pick (nSampleFiles) : -1;
        const bool longG = cat == C_Pads || cat == C_Fx;
        b.set (P_gsOn, 1).set (P_gsLevel, 0.6f).set (P_gsPosition, r.uni()).set (P_gsScan, r.range (-0.3f, 0.3f)).set (P_gsSpray, r.range (10.0f, 100.0f))
         .set (P_gsSize, longG ? r.range (150.0f, 400.0f) : r.range (40.0f, 150.0f)).set (P_gsDensity, r.range (10.0f, 40.0f)).set (P_gsShape, (float) r.pick (5))
         .set (P_gsKeyTrack, 1).set (P_gsReverse, cat == C_Fx ? r.range (0.2f, 0.5f) : 0.0f).set (P_gsSpread, r.range (0.3f, 0.9f)).set (P_gsSemi, bassCat ? -12.0f : 0.0f);
        res.engines.add ("the Granular Synth");
    }
    if (chosen[S_Reese])
    {
        const bool manyNotesR = cat == C_Pads || cat == C_Stabs || cat == C_Keys || b.v[(size_t) P_polyphony] > 1.0f;
        const int voices = manyNotesR ? 2 + r.pick (2) : 2 + r.pick (3);   // (CPU: 4 voices on a mono patch only)
        const int mode = r.chance (0.5f) ? 2 : (r.chance (0.6f) ? 0 : 1);
        dnb::reese (b, voices, r.pick (3), mode, r.of ({ 2, 3, 3, 4, 5 }), r.range (8.0f, 25.0f), r.range (0.1f, 0.4f),
                    r.chance (0.6f) ? r.range (0.15f, 0.6f) : 0.0f, bassCat ? r.range (0.3f, 0.6f) : 0.0f,
                    manyNotesR ? 0.0f : (voices == 4 ? r.range (0.0f, 0.2f) : r.range (0.0f, 0.4f)), r.range (0.4f, 0.8f));
        b.set (P_rsOct, bassCat ? -1.0f : 0.0f).set (P_rsLevel, 0.65f);
        if (! bassCat) b.set (P_rsMonoBelow, 0.0f);
        res.engines.add ("the Reese");
    }
    if (chosen[S_Core])
    {
        // (CPU: both slots only on a mono patch; unison only for pads, and not with three sources)
        const bool manyNotes = cat == C_Pads || cat == C_Stabs || cat == C_Keys || b.v[(size_t) P_polyphony] > 1.0f;
        int nChosen = 0; for (bool c : chosen) nChosen += c ? 1 : 0;
        const int nSlots = (cx == 2 && ! manyNotes && r.chance (0.4f)) ? 2 : 1;
        for (int s = 0; s < nSlots; ++s)
        {
            int algo = 0;
            switch (cat)
            {
                case C_Reese: case C_Sub: algo = r.of ({ 0, 2, 8, 9 }); break;
                case C_Neuro:  algo = r.of ({ 0, 8, 9, 1, 2 }); break;
                case C_Keys:   algo = r.of ({ 3, 5, 6, 1 }); break;
                case C_Leads:  algo = r.of ({ 1, 2, 10, 0 }); break;
                case C_Pads:   algo = r.of ({ 3, 7, 11, 6 }); break;
                default:       algo = r.pick (kNumCoreAlgorithms); break;
            }
            coreAlgorithm (b, s, algo);
            b.set (coreParam (s, CS_Engine), 1.0f).set (coreParam (s, CS_Level), nSlots == 2 ? 0.5f : 0.7f).set (coreParam (s, CS_Oct), bassOct + (s == 1 ? 1.0f : 0.0f))
             .set (coreParam (s, CS_Bright), r.range (0.8f, 1.3f));
            if (cat == C_Pads && nChosen <= 2) b.set (coreParam (s, CS_Uni), (float) r.pick (2)).set (coreParam (s, CS_Detune), r.range (6.0f, 16.0f)).set (coreParam (s, CS_Spread), 0.8f);
            const bool keysLike = cat == C_Keys || cat == C_Stabs;
            const bool opsOnly = keysLike && r.chance (0.5f);
            if (opsOnly) b.set (coreEnvModeParam (s), 1.0f);
            for (int k = 0; k < 6; ++k)
            {
                if (! coreIsUsed (b, s, k)) continue;
                const bool car = coreIsCarrier (b, s, k);
                auto op = [&] (int wch, float v) { b.set (coreOpParam (s, k, wch), v); };
                if (car) op (CO_Ratio, (cat == C_Reese || cat == C_Sub) ? r.of ({ 1.0f, 1.0f, 0.5f }) : 1.0f);
                else op (CO_Ratio, cat == C_Keys && r.chance (0.3f) ? 3.5f : r.of ({ 0.5f, 1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f }));
                if (cat == C_Fx && ! car && r.chance (0.25f)) { op (CO_Fixed, 1.0f); op (CO_Hz, r.logRange (60.0f, 3000.0f)); }
                if (! car)
                {
                    // the modulation into whatever it feeds
                    for (int j = 0; j < 6; ++j)
                        if (j != k && b.v[(size_t) coreOpParam (s, k, CO_M1 + j)] > 0.0f)
                            op (CO_M1 + j, r.range (0.8f, cat == C_Neuro ? 5.0f : 3.5f));
                    op (CO_Level, r.range (0.4f, 1.0f));
                }
                if (car && cat == C_Hoover) op (CO_Wave, (float) r.of ({ 0, 6, 7 }));
                // envelopes
                if (keysLike || cat == C_Breaks)
                {
                    op (CO_Att, 0.001f); op (CO_Dec, car ? r.range (0.4f, 1.5f) : r.range (0.1f, 0.6f)); op (CO_Sus, car ? r.range (0.0f, 0.3f) : r.range (0.0f, 0.2f));
                    // (CPU: with Operators only the operators' Release is the voice's life, so shorter)
                    op (CO_Rel, car ? (opsOnly ? r.range (0.25f, 0.6f) : r.range (0.3f, 1.2f)) : 0.3f);
                }
                else if (cat == C_Pads || cat == C_Fx)
                {
                    op (CO_Att, r.range (0.05f, 1.0f)); op (CO_Dec, r.range (0.5f, 3.0f)); op (CO_Sus, r.range (0.4f, 1.0f)); op (CO_Rel, r.range (0.8f, 3.0f));
                }
                else if (! car)
                {
                    op (CO_Dec, r.range (0.15f, 0.6f)); op (CO_Sus, r.range (0.15f, 0.6f));
                }
            }
            // a touch of feedback on the top operator
            for (int k = 5; k >= 0; --k)
                if (coreIsUsed (b, s, k)) { if (r.chance (0.6f)) b.set (coreOpParam (s, k, CO_M1 + k), r.range (0.2f, cat == C_Neuro ? 1.8f : 1.0f)); break; }
        }
        res.engines.add (nSlots == 2 ? "Core A and B (Kernel FM)" : "Core A (Kernel FM)");
    }
    if (chosen[S_Breaks])
    {
        res.nBreaks = cx == 0 ? 1 : (cx == 1 ? 1 + r.pick (2) : 2 + r.pick (2));
        for (int k = 0; k < res.nBreaks; ++k)
        {
            res.breakFile[k] = nBreakFiles > 0 ? r.pick (nBreakFiles) : -1;
            res.breakDemoVar[k] = k;
            res.breakStretch[k] = r.chance (0.5f) ? 1 : (r.chance (0.6f) ? 0 : 2);
            res.breakColour[k] = r.chance (0.5f) ? r.pick (5) : 0;
            const auto bp = breakParams (k);
            b.set (bp.sliceMode, 0).set (bp.bars, 0).set (bp.stretch, (float) res.breakStretch[k]).set (bp.colour, (float) res.breakColour[k]).set (bp.level, 1.0f);
        }
        b.set (P_bcOn, 1).set (P_bcSync, 1).set (P_bcLevel, cat == C_Breaks ? 0.85f : 0.6f).set (P_bcActive, 1).set (P_bsOn, 1).set (P_bsKeys, 1).set (P_bsSteps, r.chance (0.5f) ? 1.0f : 0.0f)
         .set (P_bsSwing, r.chance (0.4f) ? r.range (0.05f, 0.25f) : 0.0f);
        if (res.nBreaks > 1) b.set (P_bsToolSource, 1);
        if (cat != C_Breaks) b.set (P_bcKeyBase, 96);   // (a break under another sound: its slices sit up out of the way, so the keys play the synth)
        res.patTool[0] = -2;   // Original
        res.patTool[1] = -3;   // Jungle edit
        res.patTool[2] = r.of ({ (int) breaktools::ToolRollers, (int) breaktools::ToolTwoStep, (int) breaktools::ToolHalftime, (int) breaktools::ToolCutUp });
        res.patTool[3] = r.of ({ (int) breaktools::ToolSnareBuild, (int) breaktools::ToolStutter, (int) breaktools::ToolGhosts, (int) breaktools::ToolVariation });
        res.toolSeed = r.next() | 1u;
        if (cx >= 1)
        {
            // a fill in Pattern H (the Fill Pattern), played by Auto Fill every 4, 8 or 16 bars (and from the Fill button / pad)
            b.set (P_bsAutoFill, (float) (1 + r.pick (3))).set (P_bsFillPat, 8).set (P_bsFillSize, 3);
            res.fillTool = r.of ({ (int) breaktools::ToolSnareBuild, (int) breaktools::ToolStutter, (int) breaktools::ToolRollers, (int) breaktools::ToolPitchDive });
        }
        if (cx == 2 && r.chance (0.5f)) { res.chain = r.of ({ "AABA", "AABC", "ABAC", "AAAD" }); b.set (P_bsChainOn, 1); }
        res.engines.add (res.nBreaks == 1 ? "a break" : juce::String (res.nBreaks) + " breaks");
    }

    // ---- bass categories: everything under 120 Hz in the middle (always, effects or not)
    if (bassCat || chosen[S_Breaks]) b.set (P_fxOnStereo, 1).set (P_sttMono, 1).set (P_sttMonoFreq, 120).set (P_sttWidth, 1.0f).set (P_sttHaas, 0.0f);

    // ---- note mutation
    if (o.use[U_NoteMut])
    {
        const float pWm = cat == C_Neuro ? 0.85f : (cx == 0 ? 0.1f : (cx == 1 ? 0.3f : 0.6f));
        if (r.chance (pWm))
            b.set (P_wmMix, r.range (0.4f, 1.0f)).set (P_wmFold, r.range (0.1f, cat == C_Neuro ? 0.8f : 0.4f)).set (P_wmDrive, r.range (0.1f, 0.5f)).set (P_wmShape, r.range (0.0f, 0.4f));
        if (cx == 2 && r.chance (0.35f) && (chosen[S_Classic] || chosen[S_Wavetable] || chosen[S_Complex]))
            b.set (P_arMod, 0).set (P_arRatio, r.of ({ 0.5f, 1.0f, 2.0f, 3.0f })).set (r.chance (0.5f) ? P_arFm : P_arRing, r.range (0.1f, 0.4f));
        // (CPU: the resonator bank rings on in every voice, so not with the Core's operators as well)
        if ((cat == C_Keys || cat == C_Pads) && r.chance (cx == 0 ? 0.15f : 0.4f) && ! chosen[S_Core])
            b.set (P_resMix, r.range (0.2f, 0.45f)).set (P_resTuning, (float) r.pick (6)).set (P_resDecay, r.range (0.3f, 1.5f));
        if (cx == 2 && chosen[S_Classic] && (chosen[S_Complex] || chosen[S_SuperSaw]) && r.chance (0.4f))
            b.set (P_dnaMix, r.range (0.3f, 0.7f)).set (P_dnaMode, (float) r.pick (7)).set (P_dnaA, 0).set (P_dnaB, chosen[S_Complex] ? 6.0f : 7.0f);
    }
    // ---- bus mutation
    if (o.use[U_BusMut] && (cat == C_Pads || cat == C_Fx || cx == 2) && r.chance (cat == C_Pads || cat == C_Fx ? 0.5f : 0.25f))
        b.set (P_grMix, r.range (0.15f, 0.45f)).set (P_grSize, r.range (60.0f, 200.0f)).set (P_grDensity, r.range (10.0f, 40.0f)).set (P_grJitter, r.range (0.2f, 0.6f))
         .set (P_grFeedback, r.range (0.0f, 0.25f));

    // ---- effects
    if (o.use[U_Effects])
    {
        float rev = 0, dly = 0, cho = 0;
        switch (cat)
        {
            case C_Pads:  rev = r.range (0.3f, 0.5f); cho = r.range (0.2f, 0.4f); dly = r.chance (0.4f) ? r.range (0.1f, 0.25f) : 0.0f; break;
            case C_Keys:  rev = r.range (0.15f, 0.3f); cho = r.chance (0.5f) ? r.range (0.15f, 0.35f) : 0.0f; dly = r.chance (0.5f) ? r.range (0.1f, 0.25f) : 0.0f; break;
            case C_Stabs: rev = r.range (0.12f, 0.3f); dly = r.range (0.1f, 0.3f); break;
            case C_Leads: rev = r.range (0.08f, 0.2f); dly = r.range (0.1f, 0.3f); break;
            case C_Hoover: rev = r.range (0.1f, 0.25f); dly = r.chance (0.4f) ? r.range (0.1f, 0.2f) : 0.0f; break;
            case C_Fx:    rev = r.range (0.25f, 0.5f); dly = r.range (0.15f, 0.4f); break;
            case C_Breaks: rev = r.chance (0.4f) ? r.range (0.05f, 0.15f) : 0.0f; break;
            default:      break;   // basses stay dry
        }
        b.set (P_reverbMix, rev).set (P_delayMix, dly).set (P_chorusMix, cho).set (P_delaySync, (float) r.of ({ 4, 5, 6, 7 }))
         .set (P_delayFeedback, r.range (0.2f, 0.55f)).set (P_reverbSize, r.range (1.5f, 3.5f));
        if (cat == C_Pads && r.chance (0.4f)) b.set (P_shimmerMix, r.range (0.15f, 0.35f));
        if (bassCat && r.chance (0.5f)) b.set (P_fxOnMultiband, 1).set (P_mbcDepth, r.range (0.25f, 0.6f));
        if ((cat == C_Breaks || chosen[S_Breaks]) && r.chance (0.5f)) b.set (P_fxOnSampler, 1).set (P_smpModel, (float) r.pick (3)).set (P_fxMixSampler, r.range (0.5f, 1.0f));
        if (cat == C_Hoover && r.chance (0.5f)) b.set (P_fxOnFlanger, 1).set (P_flpMode, (float) r.pick (2)).set (P_fxMixFlanger, r.range (0.3f, 0.6f));
        if ((cat == C_Stabs || cat == C_Pads) && cx >= 1 && r.chance (0.3f)) { b.set (P_fxOnShaper, 1).set (P_vshDepth, r.range (0.4f, 0.8f)); b.shaperCurve = 1 + r.pick (4); }
        if (! bassCat && ! chosen[S_Breaks] && cx >= 1 && r.chance (0.3f)) b.set (P_fxOnStereo, 1).set (P_sttMono, 1).set (P_sttWidth, r.range (1.1f, 1.6f));
    }

    // ---- modulation
    auto macroBright = [&]() -> int
    {
        if (chosen[S_Core]) return coreParam (0, CS_Bright);
        if (chosen[S_Wavetable]) return P_osc4Position;
        if (chosen[S_Reese]) return P_rsTwist;
        if (chosen[S_Complex]) return P_complexFm;
        if (b.v[(size_t) P_wmMix] > 0.0f) return P_wmFold;
        return P_filterRes;
    };
    if (o.use[U_ModMatrix])
    {
        int nRoutes = cx == 0 ? 1 : (cx == 1 ? 2 + r.pick (2) : 4 + r.pick (3));
        // playable first: the mod wheel opens the filter
        b.route (MS_ModWheel, P_filterCutoff, 0.25f, MC_Linear, true); --nRoutes;
        if (nRoutes > 0)
        {
            if (bassCat || cat == C_Hoover)
            {
                // a synced wobble on the filter (locked to the song)
                b.set (P_lfo1Sync, (float) r.of ({ 6, 9, 3, 12 })).set (P_lfo1Retrig, 1).set (P_lfo1Depth, 1.0f);
                if (r.chance (0.5f)) b.lfoCustom (0, r.of ({ (int) LfoShapeStore::PR_Wub, (int) LfoShapeStore::PR_Triangle, (int) LfoShapeStore::PR_SawDown }));
                b.route (MS_Lfo1, P_filterCutoff, r.range (0.12f, 0.3f));
            }
            else if (cat == C_Pads || cat == C_Fx)
            {
                b.set (P_lfo2Sync, (float) r.of ({ 14, 15, 16 })).set (P_lfo2Retrig, 2).set (P_lfo2Depth, 1.0f);
                b.route (MS_Lfo2, P_filterCutoff, r.range (0.1f, cat == C_Fx ? 0.4f : 0.2f));
            }
            else b.route (MS_Velocity, P_filterCutoff, r.range (0.08f, 0.2f));
            --nRoutes;
        }
        // then a few from the sounds that are there
        struct Pair { int src, dst; float lo, hi; };
        std::vector<Pair> pool;
        if (chosen[S_Wavetable]) pool.push_back ({ MS_Lfo3, P_osc4Position, 0.15f, 0.45f });
        if (chosen[S_Granular]) pool.push_back ({ MS_Lfo3, P_gsPosition, 0.1f, 0.3f });
        if (chosen[S_Core]) pool.push_back ({ MS_ModEnv1, coreParam (0, CS_Bright), 0.2f, 0.5f });
        if (chosen[S_Core]) pool.push_back ({ MS_Velocity, coreParam (0, CS_Bright), 0.1f, 0.3f });
        if (chosen[S_Complex]) pool.push_back ({ MS_Lfo1, P_complexFm, 0.1f, 0.3f });
        if (chosen[S_Reese]) pool.push_back ({ MS_ReesePhase, P_filterCutoff, 0.05f, 0.15f });
        if (chosen[S_SuperSaw]) pool.push_back ({ MS_Lfo4, P_supersawSpread, 0.1f, 0.3f });
        if (b.v[(size_t) P_wmMix] > 0.0f) pool.push_back ({ MS_ModEnv1, P_wmFold, 0.1f, 0.35f });
        pool.push_back ({ MS_Velocity, P_filterCutoff, 0.05f, 0.15f });
        pool.push_back ({ MS_Lfo4, P_mxOsc1Pan, 0.1f, 0.3f });
        pool.push_back ({ MS_RandNote, P_filterCutoff, 0.03f, 0.08f });
        for (int n = 0; n < nRoutes && ! pool.empty(); ++n)
        {
            const int i = r.pick ((int) pool.size());
            const auto pr = pool[(size_t) i];
            pool.erase (pool.begin() + i);
            b.route (pr.src, pr.dst, r.range (pr.lo, pr.hi));
        }
        b.set (P_lfo3Sync, (float) r.of ({ 12, 14, 15 })).set (P_lfo3Retrig, 1).set (P_lfo3Depth, 1.0f)
         .set (P_lfo4Rate, r.range (0.05f, 0.5f)).set (P_lfo4Depth, 1.0f)
         .set (P_mEnv1A, 0.003f).set (P_mEnv1D, r.range (0.15f, 0.6f)).set (P_mEnv1S, 0.0f);
    }
    // the macros: always, so it's playable straight away
    b.macro (0, "Filter", { { P_filterCutoff, 0.35f } });
    b.macro (1, "Bright", { { macroBright(), 0.4f } });
    b.macro (2, "Drive", { { P_filterDrive, 0.35f } });
    b.macro (3, "Space", { { P_reverbMix, 0.4f }, { P_delayMix, 0.3f } });

    // ---- sequencers and the arp / chord memory
    if (o.use[U_Sequencers] && (cat == C_Neuro || cat == C_Fx || cat == C_Breaks || cx == 2) && r.chance (0.6f))
    {
        static const char* lanes[] = { "f..F.f..f.F.f...", "F...f.c.F...f.s.", "f.f.F...f.c.F...", "c..f..F.c..f..Ff" };
        b.set (P_dsOn, 1).set (P_dsSync, 0).set (P_dsRate, 2).set (P_dsDepth, r.range (0.4f, 0.8f)).set (P_dsGlide, r.range (0.0f, 0.4f));
        b.dnaLane (0, 0, lanes[r.pick (4)], { r.range (0.5f, 0.9f), r.range (0.3f, 0.7f) });
        if (cx == 2 && r.chance (0.5f)) { b.set (P_dsLane2On, 1).set (P_dsLane2Steps, (float) r.of ({ 12, 6, 10 })); b.dnaLane (0, 1, "s..r..s.", { 0.4f }); }
    }
    if (o.use[U_ArpChord])
    {
        if (cat == C_Stabs && r.chance (0.6f)) b.set (P_chdOn, 1).set (P_chdPreset, (float) (1 + r.pick (8)));
        // (CPU: the arp fills every voice of a polyphonic keys patch, so not with the Core there)
        if ((cat == C_Keys || cat == C_Leads) && cx >= 1 && r.chance (0.35f) && ! (chosen[S_Core] && b.v[(size_t) P_polyphony] > 1.0f))
            b.set (P_arpOn, 1).set (P_arpMode, (float) r.of ({ 0, 2, 4 })).set (P_arpRate, (float) r.of ({ 2, 5, 2 })).set (P_arpOctaves, (float) (1 + r.pick (2))).set (P_arpGate, r.range (0.4f, 0.8f));
    }

    // ---- name and engines
    static const char* adj[] = { "Rolling", "Dark", "Liquid", "Steppa", "Ghost", "Acid", "Rude", "Cold", "Pirate", "Dread", "Neon", "Toxic", "Velvet",
                                 "Hollow", "Concrete", "Midnight", "Static", "Murky", "Shattered", "Feral", "Lost", "Heavy", "Glass", "Rusted" };
    static const char* noun[] = { "Pressure", "Signal", "Tunnel", "Mutant", "Riddim", "Engine", "Spectre", "Current", "Vapour", "Circuit", "Dub", "Breaker",
                                  "Machine", "Ritual", "Frequency", "Wobble", "Station", "Echo", "Swarm", "Pulse", "Mirage", "Furnace", "Orbit", "Siren" };
    res.name = juce::String (adj[r.pick ((int) (sizeof (adj) / sizeof (adj[0])))]) + " " + noun[r.pick ((int) (sizeof (noun) / sizeof (noun[0])))];
    res.ok = true;
    return res;
}

} // namespace randompatch
} // namespace tg
