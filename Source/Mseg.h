#pragma once
// Multi-stage envelopes (MSEG 1 and 2). Each has a start level and up to 8 stages; a stage moves to
// its level over its time with its curve. One stage can be the sustain point, and a range of stages
// can loop while the note is held. Times are in seconds, or in beats when the envelope is synced.
// The message thread edits; the voices read a copy made at note-on (so editing never glitches a note).
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

namespace tg
{

struct MsegShape
{
    static constexpr int kMaxStages = 8;
    struct Stage { float time = 0.2f, level = 0.0f, curve = 0.0f; };   // curve -1..1 (0 = straight)
    float start = 0.0f;
    std::array<Stage, kMaxStages> st {};
    int n = 0;
    int sustain = -1;                 // stage index held while the key is down (-1: none)
    int loopStart = -1, loopEnd = -1; // stages looped while the key is down (-1: no loop)
};

class MsegStore
{
public:
    static constexpr int kEnvs = 2;
    MsegStore() { for (int k = 0; k < kEnvs; ++k) setPreset (k, 0); }

    MsegShape get (int k) const { const juce::ScopedLock sl (lock); return shapes[(size_t) juce::jlimit (0, kEnvs - 1, k)]; }
    void set (int k, MsegShape s)
    {
        s.n = juce::jlimit (1, MsegShape::kMaxStages, s.n);
        for (int i = 0; i < s.n; ++i)
        {
            auto& t = s.st[(size_t) i];
            t.time = juce::jlimit (0.0f, 16.0f, t.time); t.level = juce::jlimit (0.0f, 1.0f, t.level); t.curve = juce::jlimit (-1.0f, 1.0f, t.curve);
        }
        s.start = juce::jlimit (0.0f, 1.0f, s.start);
        if (s.sustain >= s.n) s.sustain = -1;
        if (s.loopStart < 0 || s.loopEnd < s.loopStart || s.loopEnd >= s.n) s.loopStart = s.loopEnd = -1;
        { const juce::ScopedLock sl (lock); shapes[(size_t) juce::jlimit (0, kEnvs - 1, k)] = s; }
        version.fetch_add (1);
    }
    // 0 pluck (fast up, decay), 1 swell, 2 rhythmic loop (two bumps, looping), 3 ADSR-like with sustain
    void setPreset (int k, int p)
    {
        MsegShape s;
        switch (p)
        {
            case 1: s.start = 0.0f; s.n = 2; s.st[0] = { 1.5f, 1.0f, -0.5f }; s.st[1] = { 1.0f, 0.0f, 0.3f }; s.sustain = 0; break;
            case 2: s.start = 0.0f; s.n = 4; s.st[0] = { 0.02f, 1.0f, 0.0f }; s.st[1] = { 0.2f, 0.2f, 0.5f }; s.st[2] = { 0.02f, 0.8f, 0.0f }; s.st[3] = { 0.26f, 0.0f, 0.5f };
                    s.loopStart = 0; s.loopEnd = 3; break;
            case 3: s.start = 0.0f; s.n = 3; s.st[0] = { 0.01f, 1.0f, 0.0f }; s.st[1] = { 0.3f, 0.6f, 0.4f }; s.st[2] = { 0.5f, 0.0f, 0.3f }; s.sustain = 1; break;
            default: s.start = 0.0f; s.n = 2; s.st[0] = { 0.005f, 1.0f, 0.0f }; s.st[1] = { 0.4f, 0.0f, 0.6f }; break;
        }
        set (k, s);
    }
    static const char* presetName (int p) { static const char* n[] = { "Pluck", "Swell", "Rhythmic loop", "ADSR" }; return n[juce::jlimit (0, 3, p)]; }

    juce::var toVar() const
    {
        juce::Array<juce::var> all;
        for (int k = 0; k < kEnvs; ++k)
        {
            const auto s = get (k);
            auto* o = new juce::DynamicObject();
            o->setProperty ("start", s.start); o->setProperty ("sustain", s.sustain);
            o->setProperty ("loopStart", s.loopStart); o->setProperty ("loopEnd", s.loopEnd);
            juce::StringArray a;
            for (int i = 0; i < s.n; ++i) a.add (juce::String (s.st[(size_t) i].time, 4) + ":" + juce::String (s.st[(size_t) i].level, 4) + ":" + juce::String (s.st[(size_t) i].curve, 3));
            o->setProperty ("stages", a.joinIntoString (","));
            all.add (juce::var (o));
        }
        return juce::var (all);
    }
    void fromVar (const juce::var& v)   // missing: the default plucks
    {
        for (int k = 0; k < kEnvs; ++k)
        {
            if (! v.isArray() || k >= v.size() || ! v[k].isObject()) { setPreset (k, 0); continue; }
            const auto& o = v[k];
            MsegShape s;
            s.start = (float) (double) o.getProperty ("start", 0.0);
            s.sustain = (int) o.getProperty ("sustain", -1);
            s.loopStart = (int) o.getProperty ("loopStart", -1); s.loopEnd = (int) o.getProperty ("loopEnd", -1);
            juce::StringArray a; a.addTokens (o["stages"].toString(), ",", {});
            s.n = 0;
            for (auto& t : a)
            {
                juce::StringArray f; f.addTokens (t, ":", {});
                if (f.size() >= 2 && s.n < MsegShape::kMaxStages) s.st[(size_t) s.n++] = { f[0].getFloatValue(), f[1].getFloatValue(), f.size() > 2 ? f[2].getFloatValue() : 0.0f };
            }
            if (s.n == 0) setPreset (k, 0); else set (k, s);
        }
    }
    juce::String toJson() const { return juce::JSON::toString (toVar(), true); }
    uint32_t getVersion() const { return version.load(); }

    // audio thread: a copy for note-on, without waiting if the editor is mid-edit
    bool tryGet (int k, MsegShape& out) const
    {
        const juce::ScopedTryLock sl (lock);
        if (! sl.isLocked()) return false;
        out = shapes[(size_t) juce::jlimit (0, kEnvs - 1, k)];
        return true;
    }

    static float curveAt (float u, float curve)   // u 0..1 along a stage
    {
        if (std::abs (curve) < 1.0e-3f) return u;
        const float e = std::exp2 (3.0f * curve);   // >0: slow start, fast end; <0: fast start
        return std::pow (juce::jlimit (0.0f, 1.0f, u), e);
    }

private:
    juce::CriticalSection lock;
    std::array<MsegShape, kEnvs> shapes;
    std::atomic<uint32_t> version { 0 };
};

// One voice's run through an MSEG
struct MsegRun
{
    MsegShape s;
    int stage = 0;
    double inStage = 0.0;    // seconds (or beats) into the stage
    float from = 0.0f, level = 0.0f;
    bool released = false, done = false;
    float timeScaleUsed = 1.0f;

    void start (const MsegShape& shape) { s = shape; stage = 0; inStage = 0.0; from = level = s.start; released = false; done = s.n <= 0; }
    void release()
    {
        if (released) return;
        released = true;
        // continue after the sustain stage / the loop, from where it is now
        const int after = s.loopEnd >= 0 ? s.loopEnd + 1 : (s.sustain >= 0 ? s.sustain + 1 : -1);
        if (after >= 0 && stage < after) { stage = after; inStage = 0.0; from = level; done = stage >= s.n; }
    }
    // where it is, in the shape's own time units (for the editor's playhead)
    float position() const
    {
        double p = 0.0;
        for (int i = 0; i < stage && i < s.n; ++i) p += s.st[(size_t) i].time;
        return (float) (p + std::min ((double) s.st[(size_t) juce::jlimit (0, MsegShape::kMaxStages - 1, stage)].time, inStage / std::max (1.0e-6, (double) timeScaleUsed)));
    }
    // advance by dt (seconds, or beats when synced) and return the level 0..1
    float advance (double dt, float timeScale)
    {
        timeScaleUsed = timeScale;
        if (done) return level;
        double left = dt;
        for (int guard = 0; guard < 64 && left >= 0.0; ++guard)
        {
            const auto& st = s.st[(size_t) stage];
            const double len = std::max (1.0e-5, (double) st.time * timeScale);
            const bool hold = ! released && stage == s.sustain;
            if (inStage + left < len)
            {
                inStage += left;
                level = from + (st.level - from) * MsegStore::curveAt ((float) (inStage / len), st.curve);
                return level;
            }
            // the stage ends
            left -= (len - inStage);
            level = st.level; from = level; inStage = 0.0;
            if (hold) { inStage = len; return level; }                                                   // sustain: wait for the release
            if (! released && stage == s.loopEnd && s.loopStart >= 0) { stage = s.loopStart; continue; }   // loop
            if (++stage >= s.n) { done = true; stage = s.n - 1; return level; }
        }
        return level;
    }
};

} // namespace tg
