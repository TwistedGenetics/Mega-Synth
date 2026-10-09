#pragma once
// Universal modulation matrix: sources, curves, and the lock-free route store shared
// between the UI (message thread) and the audio engine.
//
// Destinations are registry parameters (Registry.h): any parameter flagged modulatable
// can be a destination. Depth is applied in the destination's normalised (0..1) space,
// so it respects each parameter's own scaling (log for frequency, cents/semitones for pitch).

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include "Params.h"

namespace tg
{

enum SrcKind { K_NONE, K_LFO, K_ENV, K_MIDI, K_MPE, K_RAND, K_NOTE, K_FOLLOW, K_AUDIO };

// key (stable, saved in patches), display name, kind, bipolar
#define TG_MOD_SOURCES(X) \
    X(None,        "None",              K_NONE,   false) \
    X(Lfo1,        "LFO 1",             K_LFO,    true)  \
    X(Lfo2,        "LFO 2",             K_LFO,    true)  \
    X(Lfo3,        "LFO 3",             K_LFO,    true)  \
    X(Lfo4,        "LFO 4",             K_LFO,    true)  \
    X(AmpEnv,      "Amp Env",           K_ENV,    false) \
    X(FilterEnv,   "Filter Env",        K_ENV,    false) \
    X(ModEnv1,     "Mod Env 1",         K_ENV,    false) \
    X(ModEnv2,     "Mod Env 2",         K_ENV,    false) \
    X(ModEnv3,     "Mod Env 3",         K_ENV,    false) \
    X(Velocity,    "Velocity",          K_MIDI,   false) \
    X(Note,        "Note Number",       K_MIDI,   true)  \
    X(KeyTrack,    "Key Tracking",      K_MIDI,   false) \
    X(Aftertouch,  "Aftertouch",        K_MIDI,   false) \
    X(PolyAT,      "Poly Aftertouch",   K_MIDI,   false) \
    X(ModWheel,    "Mod Wheel",         K_MIDI,   false) \
    X(PitchBend,   "Pitch Bend",        K_MIDI,   true)  \
    X(CcA,         "MIDI CC A",         K_MIDI,   false) \
    X(CcB,         "MIDI CC B",         K_MIDI,   false) \
    X(MpePressure, "MPE Pressure",      K_MPE,    false) \
    X(MpeSlide,    "MPE Slide",         K_MPE,    true)  \
    X(MpeGlide,    "MPE Glide (X)",     K_MPE,    true)  \
    X(RandNote,    "Random Per Note",   K_RAND,   true)  \
    X(RandSmooth,  "Random Smooth",     K_RAND,   true)  \
    X(RandStep,    "Random Stepped",    K_RAND,   true)  \
    X(SampleHold,  "Sample & Hold",     K_RAND,   true)  \
    X(Chaos,       "Chaos",             K_RAND,   true)  \
    X(Drift,       "Drift",             K_RAND,   true)  \
    X(Gate,        "Note Gate",         K_NOTE,   false) \
    X(NoteOn,      "Note On",           K_NOTE,   false) \
    X(NoteOff,     "Note Off",          K_NOTE,   false) \
    X(EnvFollow,   "Envelope Follower", K_FOLLOW, false) \
    X(AudioFollow, "Audio Follower",    K_FOLLOW, false) \
    X(TransFollow, "Transient Follower",K_FOLLOW, false) \
    X(Osc1Audio,   "Osc 1 (audio rate)",K_AUDIO,  true)  \
    X(Osc2Audio,   "Osc 2 (audio rate)",K_AUDIO,  true)  \
    X(Osc3Audio,   "Osc 3 (audio rate)",K_AUDIO,  true)  \
    X(SubAudio,    "Sub (audio rate)",  K_AUDIO,  true)

enum ModSource
{
#define TG_X(key, name, kind, bip) MS_##key,
    TG_MOD_SOURCES(TG_X)
#undef TG_X
    MS_COUNT
};

inline const char* const kModSrcKeys[]  = {
#define TG_X(key, name, kind, bip) #key,
    TG_MOD_SOURCES(TG_X)
#undef TG_X
};
inline const char* const kModSrcNames[] = {
#define TG_X(key, name, kind, bip) name,
    TG_MOD_SOURCES(TG_X)
#undef TG_X
};
inline const SrcKind kModSrcKind[] = {
#define TG_X(key, name, kind, bip) kind,
    TG_MOD_SOURCES(TG_X)
#undef TG_X
};
inline const bool kModSrcBipolar[] = {
#define TG_X(key, name, kind, bip) bip,
    TG_MOD_SOURCES(TG_X)
#undef TG_X
};

enum ModCurve { MC_Linear, MC_Exp, MC_Log, MC_SCurve, MC_Inverted, MC_Rectified, MC_Quantized, MC_Smooth, MC_Stepped, MC_COUNT };
inline const char* const kModCurveNames[] = { "Linear", "Exponential", "Logarithmic", "S-Curve", "Inverted", "Rectified", "Quantized", "Smooth", "Stepped" };

inline float applyCurve (int curve, float x)
{
    const float a = std::abs (x), sg = x < 0 ? -1.0f : 1.0f;
    switch (curve)
    {
        case MC_Exp:       return sg * a * a;
        case MC_Log:       return sg * std::sqrt (a);
        case MC_SCurve:    return sg * a * a * (3.0f - 2.0f * a);
        case MC_Inverted:  return -x;
        case MC_Rectified: return a;
        case MC_Quantized: return std::round (x * 8.0f) / 8.0f;
        default:           return x;   // Linear, plus Smooth / Stepped which act over time (in the voice)
    }
}

constexpr int kNumRoutes = 32;

struct RouteConfig
{
    bool on = false;
    int src = MS_None;
    int dst = -1;          // registry parameter index, -1 = none
    int curve = MC_Linear;
    bool unipolar = false; // "+" polarity: bipolar sources mapped to 0..1
    int via = MS_None;
    float viaDepth = 1.0f; // 0..1
    float smoothMs = 0.0f; // 0..2000

    bool active() const { return on && src != MS_None && dst >= 0; }
};

// Lock-free route storage: the UI writes, the audio thread reads once per block.
class RouteStore
{
public:
    RouteConfig get (int i) const
    {
        const auto& r = routes[(size_t) i];
        const uint32_t p = r.packed.load (std::memory_order_relaxed);
        RouteConfig c;
        c.on = (p & 1u) != 0;
        c.src = (int) ((p >> 1) & 63u);
        c.dst = (int) ((p >> 7) & 511u) - 1;
        c.curve = (int) ((p >> 16) & 15u);
        c.unipolar = ((p >> 20) & 1u) != 0;
        c.via = (int) ((p >> 22) & 63u);
        c.viaDepth = r.viaDepth.load (std::memory_order_relaxed);
        c.smoothMs = r.smoothMs.load (std::memory_order_relaxed);
        if (c.src >= MS_COUNT) c.src = MS_None;
        if (c.via >= MS_COUNT) c.via = MS_None;
        if (c.dst >= P_COUNT) c.dst = -1;
        return c;
    }

    void set (int i, const RouteConfig& c)
    {
        auto& r = routes[(size_t) i];
        const uint32_t p = (c.on ? 1u : 0u)
                         | ((uint32_t) juce::jlimit (0, 63, c.src) << 1)
                         | ((uint32_t) juce::jlimit (0, 511, c.dst + 1) << 7)
                         | ((uint32_t) juce::jlimit (0, 15, c.curve) << 16)
                         | ((c.unipolar ? 1u : 0u) << 20)
                         | ((uint32_t) juce::jlimit (0, 63, c.via) << 22);
        r.viaDepth.store (juce::jlimit (0.0f, 1.0f, c.viaDepth));
        r.smoothMs.store (juce::jlimit (0.0f, 2000.0f, c.smoothMs));
        r.packed.store (p);
        version.fetch_add (1);
    }

    void clear (int i) { set (i, RouteConfig {}); }
    void clearAll() { for (int i = 0; i < kNumRoutes; ++i) clear (i); }
    uint32_t getVersion() const { return version.load(); }

    int firstFree() const
    {
        for (int i = 0; i < kNumRoutes; ++i) if (get (i).dst < 0 && get (i).src == MS_None) return i;
        return -1;
    }

    // Saved with stable keys (source keys, parameter ids) so patches survive parameter reordering.
    juce::var toVar() const;
    void fromVar (const juce::var&);

private:
    struct R
    {
        std::atomic<uint32_t> packed { 0 };
        std::atomic<float> viaDepth { 1.0f }, smoothMs { 0.0f };
    };
    std::array<R, kNumRoutes> routes;
    std::atomic<uint32_t> version { 0 };
};

int modSourceForKey (const juce::String& key);   // -1 when unknown

// Inputs that come from MIDI rather than from a voice.
struct GlobalModInputs
{
    float wheel = 0, bend = 0, aftertouch = 0, ccA = 0, ccB = 0;
    float polyAT[128] {};
    float mpePressure[16] {}, mpeSlide[16] {}, mpeGlide[16] {};
};

//==============================================================================
// Fast, allocation-free normalised <-> plain conversion for the audio thread.
// Same maths as juce::NormalisableRange (with setSkewForCentre), without snapping.
struct NormTable
{
    float lo[P_COUNT] {}, span[P_COUNT] {}, skew[P_COUNT] {}, invSkew[P_COUNT] {};
    bool skewed[P_COUNT] {};
    bool global[P_COUNT] {};     // consumed by the global effects bus rather than by voices
    bool envTime[P_COUNT] {};    // read once at note-on (envelope stages)
    bool modulatable[P_COUNT] {};
    bool audioRate[P_COUNT] {};
};
const NormTable& normTable();   // call once from a non-audio thread first (prepare) to build it

inline float normFast (const NormTable& t, int i, float plain)
{
    if (t.span[i] <= 0.0f) return 0.0f;
    float p = (plain - t.lo[i]) / t.span[i];
    p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    return t.skewed[i] ? std::pow (p, t.skew[i]) : p;
}

inline float plainFast (const NormTable& t, int i, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    if (t.skewed[i]) n = n > 0.0f ? std::exp (std::log (n) * t.invSkew[i]) : 0.0f;
    return t.lo[i] + t.span[i] * n;
}

//==============================================================================
// Audio-rate destinations: what an audio-rate source (an oscillator's raw output) can
// drive sample by sample inside the voice.
enum AudioDest
{
    AD_P1, AD_P2, AD_P3, AD_PSub, AD_PWt1, AD_PWt2, AD_PCx, AD_PSs,     // pitch, in octaves
    AD_L1, AD_L2, AD_L3, AD_LSub, AD_LWt1, AD_LWt2, AD_LCx, AD_LSs,     // level (amplitude modulation)
    AD_Cut,                                                            // filter cutoff, in octaves
    AD_Res, AD_CxFm, AD_LegFm, AD_Fm1, AD_Fm2, AD_Fm3, AD_Fm4, AD_Ring, AD_CxShape,
    AD_COUNT
};
// Returns the AudioDest for a parameter (or -1) and the factor that turns a normalised
// depth into the destination's per-sample unit.
int audioDestFor (int paramIndex, float& scale);

//==============================================================================
// Per-route state kept by each voice (and by the engine for the global effects).
struct RouteState
{
    float sm[kNumRoutes] {}, hold[kNumRoutes] {};
    double holdPh = 0.0;
    bool first = true;
    void reset() { first = true; holdPh = 0.0; }
};

struct ResolvedRoute
{
    int slot = 0, src = 0, dst = 0, curve = 0, via = 0;
    bool unipolar = false, audio = false, toAmount = false;
    float viaDepth = 1.0f, smoothMs = 0.0f;
};

// The active routes, resolved once per engine block.
struct RouteSet
{
    int n = 0;
    ResolvedRoute r[kNumRoutes];
    bool anyAudio = false, anyGlobal = false, anyFollow = false, anyEnvTime = false;
    void build (const RouteStore&);
};

// One source value after polarity, smoothing, curve and via scaling.
float shapeRouteValue (const ResolvedRoute&, const float* srcV, RouteState&, float dtSeconds);

// Applies the control-rate routes: out = base with each destination moved in normalised space.
// Routes onto other routes' amounts are applied first, so depth modulation takes effect at once.
// onlyGlobal / onlyEnvTime restrict the destinations touched.
void applyRoutes (const RouteSet&, const float* baseV, float* outV, const float* srcV, RouteState&, float dtSeconds,
                  int filter, float* liveOffsetOut = nullptr);
enum { RF_All, RF_Global, RF_EnvTime };

} // namespace tg
