#pragma once
// The effects added with the effects rack: Multiband compressor (OTT style), Vintage Sampler,
// Beat Repeat, Flanger / Phaser, Volume Shaper and Stereo Tools, plus the rack's own store
// (slot order and the Volume Shaper's curve). Every effect works in place on a stereo block;
// the rack (FxBus) blends each one with its input by the slot's Mix.
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "../DSP.h"
#include "../Params.h"

namespace tg
{

// What the effects know about time
struct FxContext
{
    double sr = 48000.0;
    double tempo = 120.0;
    double beat = 0.0;        // beat position at the start of the block (the DAW's, or the synth's own clock)
    double beatInc = 0.0;     // beats per sample
    float dnaGate = 0.0f;     // DNA Step Gate (Mod Matrix source)
    int quality = 1;          // 0 Eco, 1 Normal, 2 High
};

//==============================================================================
// Rack order and the Volume Shaper curve (UI writes, audio reads)
class FxRackStore
{
public:
    static constexpr int kCurve = 64;
    FxRackStore() { setDefaultOrder(); setCurvePreset (0); }

    void setDefaultOrder() { std::array<int, FS_COUNT> o; for (int i = 0; i < FS_COUNT; ++i) o[(size_t) i] = i; setOrder (o); }
    void setOrder (const std::array<int, FS_COUNT>& o)
    {
        // must be a permutation; otherwise keep the default
        uint32_t seen = 0; for (int s : o) if (s >= 0 && s < FS_COUNT) seen |= 1u << s;
        std::array<int, FS_COUNT> use = o;
        if (seen != (1u << FS_COUNT) - 1) for (int i = 0; i < FS_COUNT; ++i) use[(size_t) i] = i;
        uint64_t p = 0;
        for (int i = 0; i < FS_COUNT; ++i) p |= (uint64_t) use[(size_t) i] << (4 * i);
        packed.store (p);
        version.fetch_add (1);
    }
    std::array<int, FS_COUNT> getOrder() const
    {
        std::array<int, FS_COUNT> o; const uint64_t p = packed.load (std::memory_order_relaxed);
        for (int i = 0; i < FS_COUNT; ++i) o[(size_t) i] = (int) ((p >> (4 * i)) & 15u);
        return o;
    }
    uint64_t orderKey() const { return packed.load (std::memory_order_relaxed); }
    void move (int from, int to)   // positions in the order
    {
        auto o = getOrder();
        from = juce::jlimit (0, FS_COUNT - 1, from); to = juce::jlimit (0, FS_COUNT - 1, to);
        const int s = o[(size_t) from];
        if (from < to) for (int i = from; i < to; ++i) o[(size_t) i] = o[(size_t) i + 1];
        else for (int i = from; i > to; --i) o[(size_t) i] = o[(size_t) i - 1];
        o[(size_t) to] = s;
        setOrder (o);
    }

    float curve (int i) const { return pts[(size_t) (i & (kCurve - 1))].load (std::memory_order_relaxed); }
    void setCurve (int i, float v) { pts[(size_t) (i & (kCurve - 1))].store (juce::jlimit (0.0f, 1.0f, v)); version.fetch_add (1); }
    // 0 classic pump, 1 gate, 2 1/8 pump (two per cycle), 3 triplet (three per cycle)
    void setCurvePreset (int preset)
    {
        for (int i = 0; i < kCurve; ++i)
        {
            const float x = (float) i / kCurve;
            float v = 1.0f;
            auto pump = [] (float u) { return 1.0f - std::exp (-u * 7.0f); };
            switch (preset)
            {
                case 1: v = (std::fmod (x * 4.0f, 1.0f) < 0.5f) ? 1.0f : 0.0f; break;
                case 2: v = pump (std::fmod (x * 2.0f, 1.0f)); break;
                case 3: v = pump (std::fmod (x * 3.0f, 1.0f)); break;
                default: v = pump (x); break;
            }
            pts[(size_t) i].store (v);
        }
        version.fetch_add (1);
    }
    float curveAt (double phase) const
    {
        const double p = (phase - std::floor (phase)) * kCurve;
        const int i0 = (int) p;
        const float f = (float) (p - i0);
        const float a = curve (i0), b = curve (i0 + 1);
        return a + (b - a) * f;
    }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        juce::StringArray ord; for (int s : getOrder()) ord.add (juce::String (s));
        o->setProperty ("order", ord.joinIntoString (","));
        juce::StringArray c; for (int i = 0; i < kCurve; ++i) c.add (juce::String (curve (i), 3));
        o->setProperty ("shaperCurve", c.joinIntoString (","));
        return juce::var (o);
    }
    void fromVar (const juce::var& v)   // missing or invalid parts get their defaults
    {
        setDefaultOrder(); setCurvePreset (0);
        if (! v.isObject()) return;
        juce::StringArray ord; ord.addTokens (v["order"].toString(), ",", {});
        if (ord.size() == FS_COUNT) { std::array<int, FS_COUNT> o; for (int i = 0; i < FS_COUNT; ++i) o[(size_t) i] = ord[i].getIntValue(); setOrder (o); }
        juce::StringArray c; c.addTokens (v["shaperCurve"].toString(), ",", {});
        if (c.size() == kCurve) for (int i = 0; i < kCurve; ++i) pts[(size_t) i].store (juce::jlimit (0.0f, 1.0f, c[i].getFloatValue()));
        version.fetch_add (1);
    }
    uint32_t getVersion() const { return version.load(); }

private:
    std::atomic<uint64_t> packed { 0 };
    std::array<std::atomic<float>, kCurve> pts;
    std::atomic<uint32_t> version { 0 };
};

//==============================================================================
// Linkwitz-Riley 4th-order low pass (or high pass): two Butterworth biquads
struct LR4
{
    Biquad a, b;
    double f = -1;
    Biquad::Type type = Biquad::LP;
    void set (double freq, double sr) { if (freq != f) { f = freq; a.set (type, freq, -3.0103, sr); b.set (type, freq, -3.0103, sr); } }
    inline float process (float x, int ch) { return (float) b.process (a.process (x, ch), ch); }
    void reset() { a.reset(); b.reset(); }
};

class MultibandComp
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
    std::atomic<float> meter[3] { 0, 0, 0 };   // net gain per band, dB (negative = compressing)
private:
    double sr = 48000.0;
    LR4 x1, x2;
    float env[3] { 0, 0, 0 }, gainDb[3] { 0, 0, 0 };
    float shownDb[3] { 0, 0, 0 };
};

class VintageSampler
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0;
    Biquad aa[4], out1;
    double ph = 0.0;
    float held[2] { 0, 0 }, lastIn[2] { 0, 0 };
    float hissEnv = 0.0f;   // the hiss follows the input level, so silence stays silent
    uint32_t rng = 0x1234567u;
    int lastModel = -1; float lastRate = -1, lastCut = -1, lastRes = -1;
};

class BeatRepeat
{
public:
    void prepare (double sr);
    void reset();
    // held: Trigger button or MIDI note held
    void process (float* L, float* R, int n, const float* v, const FxContext&, bool held);
    std::atomic<int> activity { 0 };   // repeat number while repeating (1, 2, ...), 0 when idle
private:
    double sr = 48000.0;
    std::vector<float> buf[2];
    int w = 0;
    bool active = false, wasHeld = false;
    float mixEnv = 0.0f;               // 0 = input, 1 = repeats
    double capStart = 0, segLen = 0, curLen = 0, pos = 0, rate = 1.0;
    int repeatNo = 0;
    double remaining = 0;              // samples left (-1: while held)
    float prevGate = 0.0f;
    uint32_t rng = 0xBEA7u;
    inline float readBuf (int ch, double p) const
    {
        const int size = (int) buf[ch].size();
        while (p < 0) p += size;
        while (p >= size) p -= size;
        const int i0 = (int) p; const int i1 = (i0 + 1) % size;
        const float f = (float) (p - i0);
        return buf[ch][(size_t) i0] + (buf[ch][(size_t) i1] - buf[ch][(size_t) i0]) * f;
    }
    void start (double lengthSamples, double duration);
};

class FlangerPhaser
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0;
    DelayLine dl[2], dry[2];
    double ph = 0.0;
    float fb[2] { 0, 0 }, env = 0.0f, dS = -1.0f;
    float apZ[2][12] {}, apFb[2] { 0, 0 }, apA[2] { 0, 0 };
};

class VolumeShaper
{
public:
    void reset() { g = 1.0f; anchor = 0.0; prevGate = 0.0f; }
    void process (float* L, float* R, int n, const float* v, const FxContext&, const FxRackStore&);
    void noteOn (double beatNow) { pendingNote = true; noteBeat = beatNow; }
    std::atomic<float> phaseShown { 0.0f };
private:
    float g = 1.0f, prevGate = 0.0f;
    double anchor = 0.0;
    bool pendingNote = false; double noteBeat = 0.0;
};

class StereoTools
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0;
    LR4 sideHigh, midLow;
    DelayLine haas;
    float haasS = 0.0f, haasAmt = 0.0f, widthS = 1.0f;
};

} // namespace tg
