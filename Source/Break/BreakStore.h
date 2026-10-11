#pragma once
// The Break Chopper's and Break Sequencer's saved settings: for each of the three breaks, its
// per-slice settings and manual slice markers; the eight patterns (A-H), each pattern's own break,
// and the pattern chain. Plus the analysis of each loaded break (slice points, tempo, which slices
// sound like kicks, snares or hats), which isn't saved but worked out again from the samples.
//
// The message thread edits under a lock; the audio thread takes a copy when the version changes
// (with a try-lock, so it never waits).
#include <JuceHeader.h>
#include <atomic>
#include <functional>

namespace tg
{

struct BreakSlice
{
    float pitch = 0.0f, gain = 1.0f, pan = 0.0f, trimStart = 0.0f, trimEnd = 1.0f, attack = 0.0f, decay = 0.0f;
    int choke = 1;          // 0: none; slices in the same group cut each other off (across all three breaks)
    bool reverse = false, loop = false;
    bool isDefault() const { return pitch == 0.0f && gain == 1.0f && pan == 0.0f && trimStart == 0.0f && trimEnd == 1.0f && attack == 0.0f && decay == 0.0f && choke == 1 && ! reverse && ! loop; }
};

struct BreakStep
{
    bool on = false, accent = false, reverse = false, tie = false;
    int slice = 0, roll = 1;   // roll: 1 = once, 2 / 3 / 4 = retriggered within the step
    float vel = 0.8f, pitch = 0.0f, prob = 1.0f;
    int brk = 0;               // 0: the Active Break; 1-3: always that break
    bool layer = false;        // a second hit stacked on this one ...
    int layerBrk = 2, layerSlice = 0;   // ... from this break (1-3) and slice
    float layerLevel = 0.7f;
};

// one break's own settings and its analysis
struct BreakSlot
{
    static constexpr int kSlices = 32;
    BreakSlice slice[kSlices];
    float markers[kSlices] {};   // manual markers (fractions of the sample, sorted; 0 is always a slice start)
    int nMarkers = 0;
    // ---- the analysis (not saved)
    int nSlices = 0;
    int start[kSlices + 1] {};   // slice k: frames start[k] .. start[k + 1]
    float kick[kSlices] {}, snare[kSlices] {}, hat[kSlices] {};   // 0..1: how much each slice sounds like a kick / snare / hat
    double origBpm = 0.0;        // the break's tempo (detected, from the bars, or typed in)
    int bars = 0;
    double sampleRate = 44100.0;
    float rms = 0.0f;            // its loudness (for Match Levels)

    void copyAnalysisFrom (const BreakSlot& a)
    {
        nSlices = a.nSlices; std::copy (std::begin (a.start), std::end (a.start), std::begin (start));
        std::copy (std::begin (a.kick), std::end (a.kick), std::begin (kick));
        std::copy (std::begin (a.snare), std::end (a.snare), std::begin (snare));
        std::copy (std::begin (a.hat), std::end (a.hat), std::begin (hat));
        origBpm = a.origBpm; bars = a.bars; sampleRate = a.sampleRate; rms = a.rms;
    }
};

struct BreakData
{
    static constexpr int kBreaks = 3, kSlices = BreakSlot::kSlices, kPatterns = 8, kSteps = 32;
    BreakSlot brk[kBreaks];
    BreakStep step[kPatterns][kSteps];
    int patBreak[kPatterns] {};  // 0: none; 1-3: starting this pattern makes that break the Active Break
    char chain[17] {};           // pattern chain, e.g. "AABA"
    int seed = 1;                // Shuffle / Jungle / Frankenbreak: each press takes the next seed
};

class BreakStore
{
public:
    BreakStore() { data = std::make_unique<BreakData>(); }

    // ---- message thread
    BreakData get() const { const juce::SpinLock::ScopedLockType l (lock); return *data; }
    // an edit that's saved (marks the patch edited and goes into undo)
    void edit (const std::function<void (BreakData&)>& fn)
    {
        { const juce::SpinLock::ScopedLockType l (lock); fn (*data); }
        editVersion.fetch_add (1); version.fetch_add (1);
    }
    // the analysis (not part of undo)
    void setAnalysis (const std::function<void (BreakData&)>& fn)
    {
        { const juce::SpinLock::ScopedLockType l (lock); fn (*data); }
        version.fetch_add (1);
    }
    uint32_t getVersion() const { return editVersion.load(); }   // (for undo: edits only)
    void reset() { edit ([] (BreakData& d) { auto a = d; d = BreakData(); copyAnalysis (a, d); }); }

    // ---- audio thread: a fresh copy when something changed (never blocks)
    bool pull (BreakData& dst, uint32_t& seen) const
    {
        const uint32_t v = version.load();
        if (v == seen) return false;
        const juce::SpinLock::ScopedTryLockType l (lock);
        if (! l.isLocked()) return false;
        dst = *data; seen = v;
        return true;
    }

    // ---- saving (only what differs from the defaults). Break 1 is saved where the single break
    // always was ("slices", "markers"), so older plug-ins and older tests read it unchanged.
    static void slotToVar (const BreakSlot& b, juce::DynamicObject& o)
    {
        juce::Array<juce::var> sl;
        for (int k = 0; k < BreakSlot::kSlices; ++k)
        {
            const auto& s = b.slice[k];
            if (s.isDefault()) continue;
            auto* x = new juce::DynamicObject();
            x->setProperty ("i", k); x->setProperty ("pitch", s.pitch); x->setProperty ("gain", s.gain); x->setProperty ("pan", s.pan);
            x->setProperty ("ts", s.trimStart); x->setProperty ("te", s.trimEnd); x->setProperty ("a", s.attack); x->setProperty ("d", s.decay);
            x->setProperty ("choke", s.choke); x->setProperty ("rev", s.reverse); x->setProperty ("loop", s.loop);
            sl.add (juce::var (x));
        }
        o.setProperty ("slices", sl);
        juce::Array<juce::var> mk;
        for (int i = 0; i < b.nMarkers; ++i) mk.add (b.markers[i]);
        o.setProperty ("markers", mk);
    }
    static void slotFromVar (BreakSlot& b, const juce::var& v)
    {
        if (auto* sl = v["slices"].getArray())
            for (auto& x : *sl)
            {
                const int k = (int) x["i"];
                if (k < 0 || k >= BreakSlot::kSlices) continue;
                auto& s = b.slice[k];
                s.pitch = juce::jlimit (-24.0f, 24.0f, (float) x.getProperty ("pitch", 0.0));
                s.gain = juce::jlimit (0.0f, 2.0f, (float) x.getProperty ("gain", 1.0));
                s.pan = juce::jlimit (-1.0f, 1.0f, (float) x.getProperty ("pan", 0.0));
                s.trimStart = juce::jlimit (0.0f, 0.95f, (float) x.getProperty ("ts", 0.0));
                s.trimEnd = juce::jlimit (s.trimStart + 0.05f, 1.0f, (float) x.getProperty ("te", 1.0));
                s.attack = juce::jlimit (0.0f, 0.5f, (float) x.getProperty ("a", 0.0));
                s.decay = juce::jlimit (0.0f, 4.0f, (float) x.getProperty ("d", 0.0));
                s.choke = juce::jlimit (0, 8, (int) x.getProperty ("choke", 1));
                s.reverse = (bool) x.getProperty ("rev", false);
                s.loop = (bool) x.getProperty ("loop", false);
            }
        if (auto* mk = v["markers"].getArray())
        {
            for (auto& m : *mk) if (b.nMarkers < BreakSlot::kSlices) b.markers[b.nMarkers++] = juce::jlimit (0.0f, 1.0f, (float) m);
            std::sort (b.markers, b.markers + b.nMarkers);
        }
    }

    juce::var toVar() const
    {
        const auto d = get();
        auto* o = new juce::DynamicObject();
        slotToVar (d.brk[0], *o);
        for (int k = 1; k < BreakData::kBreaks; ++k)
        {
            auto* bo = new juce::DynamicObject();
            slotToVar (d.brk[k], *bo);
            const bool empty = bo->getProperty ("slices").getArray()->isEmpty() && bo->getProperty ("markers").getArray()->isEmpty();
            if (empty) delete bo;
            else o->setProperty ("break" + juce::String (k + 1), juce::var (bo));
        }
        juce::Array<juce::var> pats;
        for (int p = 0; p < BreakData::kPatterns; ++p)
        {
            juce::Array<juce::var> steps;
            for (int i = 0; i < BreakData::kSteps; ++i)
            {
                const auto& s = d.step[p][i];
                if (! s.on && ! s.tie && s.slice == 0 && s.brk == 0 && ! s.layer) continue;
                auto* x = new juce::DynamicObject();
                x->setProperty ("i", i); x->setProperty ("on", s.on); x->setProperty ("slice", s.slice); x->setProperty ("vel", s.vel);
                if (s.accent) x->setProperty ("acc", true);
                if (s.reverse) x->setProperty ("rev", true);
                if (s.tie) x->setProperty ("tie", true);
                if (s.roll > 1) x->setProperty ("roll", s.roll);
                if (s.pitch != 0.0f) x->setProperty ("pitch", s.pitch);
                if (s.prob < 1.0f) x->setProperty ("prob", s.prob);
                if (s.brk != 0) x->setProperty ("brk", s.brk);
                if (s.layer) { x->setProperty ("layer", true); x->setProperty ("lb", s.layerBrk); x->setProperty ("ls", s.layerSlice); x->setProperty ("ll", s.layerLevel); }
                steps.add (juce::var (x));
            }
            pats.add (steps);
        }
        o->setProperty ("patterns", pats);
        o->setProperty ("chain", juce::String (d.chain));
        o->setProperty ("seed", d.seed);
        bool anyPatBreak = false;
        for (int p : d.patBreak) anyPatBreak = anyPatBreak || p != 0;
        if (anyPatBreak) { juce::Array<juce::var> pb; for (int p : d.patBreak) pb.add (p); o->setProperty ("patBreak", pb); }
        return juce::var (o);
    }
    juce::String toJson() const { return juce::JSON::toString (toVar(), true); }

    void fromVar (const juce::var& v)
    {
        edit ([&] (BreakData& d)
        {
            const auto analysis = d;
            d = BreakData();
            copyAnalysis (analysis, d);
            if (! v.isObject()) return;
            slotFromVar (d.brk[0], v);
            for (int k = 1; k < BreakData::kBreaks; ++k)
            {
                const auto bv = v[juce::Identifier ("break" + juce::String (k + 1))];
                if (bv.isObject()) slotFromVar (d.brk[k], bv);
            }
            if (auto* pats = v["patterns"].getArray())
                for (int p = 0; p < juce::jmin (BreakData::kPatterns, pats->size()); ++p)
                    if (auto* steps = (*pats)[p].getArray())
                        for (auto& x : *steps)
                        {
                            const int i = (int) x["i"];
                            if (i < 0 || i >= BreakData::kSteps) continue;
                            auto& s = d.step[p][i];
                            s.on = (bool) x.getProperty ("on", false);
                            s.slice = juce::jlimit (0, BreakData::kSlices - 1, (int) x.getProperty ("slice", 0));
                            s.vel = juce::jlimit (0.0f, 1.0f, (float) x.getProperty ("vel", 0.8));
                            s.accent = (bool) x.getProperty ("acc", false);
                            s.reverse = (bool) x.getProperty ("rev", false);
                            s.tie = (bool) x.getProperty ("tie", false);
                            s.roll = juce::jlimit (1, 4, (int) x.getProperty ("roll", 1));
                            s.pitch = juce::jlimit (-24.0f, 24.0f, (float) x.getProperty ("pitch", 0.0));
                            s.prob = juce::jlimit (0.0f, 1.0f, (float) x.getProperty ("prob", 1.0));
                            s.brk = juce::jlimit (0, 3, (int) x.getProperty ("brk", 0));
                            s.layer = (bool) x.getProperty ("layer", false);
                            s.layerBrk = juce::jlimit (1, 3, (int) x.getProperty ("lb", 2));
                            s.layerSlice = juce::jlimit (0, BreakData::kSlices - 1, (int) x.getProperty ("ls", 0));
                            s.layerLevel = juce::jlimit (0.0f, 1.5f, (float) x.getProperty ("ll", 0.7));
                        }
            setChain (d, v["chain"].toString());
            d.seed = juce::jmax (1, (int) v.getProperty ("seed", 1));
            if (auto* pb = v["patBreak"].getArray())
                for (int p = 0; p < juce::jmin (BreakData::kPatterns, pb->size()); ++p) d.patBreak[p] = juce::jlimit (0, 3, (int) (*pb)[p]);
        });
    }

    static void setChain (BreakData& d, const juce::String& text)
    {
        std::fill (std::begin (d.chain), std::end (d.chain), 0);
        int n = 0;
        for (auto c : text.toUpperCase())
            if (c >= 'A' && c <= 'H' && n < 16) d.chain[n++] = (char) c;
    }
    static void copyAnalysis (const BreakData& from, BreakData& to)
    {
        for (int k = 0; k < BreakData::kBreaks; ++k) to.brk[k].copyAnalysisFrom (from.brk[k]);
    }

private:
    std::unique_ptr<BreakData> data;
    mutable juce::SpinLock lock;
    std::atomic<uint32_t> version { 1 }, editVersion { 1 };
};

} // namespace tg
