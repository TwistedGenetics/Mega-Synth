#pragma once
// Chord memory and arpeggiator. Both sit between the incoming notes and the voices and run on the
// audio thread. Chord memory turns each key into a chord (a preset or one you play in with Learn);
// the arpeggiator plays the held notes (or chords) one step at a time on the beat grid.
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "../Params.h"

namespace tg
{

//==============================================================================
// The learned chord and the arpeggiator's 16-step pattern (message thread writes, audio reads)
class ArpStore
{
public:
    static constexpr int kSteps = 16, kMaxChord = 8;
    enum StepState { ST_Rest, ST_On, ST_Accent };
    ArpStore() { reset(); }
    void reset()
    {
        for (int i = 0; i < kSteps; ++i) { state[(size_t) i].store (ST_On); prob[(size_t) i].store (1.0f); }
        setLearned ({ 0, 3, 7, 10 });
        version.fetch_add (1);
    }
    int getState (int i) const { return state[(size_t) (i & 15)].load (std::memory_order_relaxed); }
    float getProb (int i) const { return prob[(size_t) (i & 15)].load (std::memory_order_relaxed); }
    void setStep (int i, int st, float p)
    {
        state[(size_t) (i & 15)].store (juce::jlimit (0, 2, st));
        prob[(size_t) (i & 15)].store (juce::jlimit (0.0f, 1.0f, p));
        version.fetch_add (1);
    }
    // learned chord: semitones above the lowest note (first is 0)
    std::vector<int> getLearned() const
    {
        std::vector<int> v;
        const int n = learnedCount.load();
        for (int i = 0; i < n; ++i) v.push_back (learned[(size_t) i].load());
        return v;
    }
    void setLearned (std::vector<int> iv)
    {
        std::sort (iv.begin(), iv.end());
        iv.erase (std::unique (iv.begin(), iv.end()), iv.end());
        if (iv.empty()) iv.push_back (0);
        const int base = iv.front();
        const int n = std::min ((int) iv.size(), kMaxChord);
        learnedCount.store (0);
        for (int i = 0; i < n; ++i) learned[(size_t) i].store (juce::jlimit (0, 48, iv[(size_t) i] - base));
        learnedCount.store (n);
        version.fetch_add (1);
    }
    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        juce::StringArray st;
        for (int i = 0; i < kSteps; ++i) st.add (juce::String (getState (i)) + ":" + juce::String (getProb (i), 3));
        o->setProperty ("pattern", st.joinIntoString (","));
        juce::StringArray ch; for (int x : getLearned()) ch.add (juce::String (x));
        o->setProperty ("chord", ch.joinIntoString (","));
        return juce::var (o);
    }
    void fromVar (const juce::var& v)   // missing parts: defaults
    {
        reset();
        if (! v.isObject()) return;
        juce::StringArray st; st.addTokens (v["pattern"].toString(), ",", {});
        for (int i = 0; i < kSteps && i < st.size(); ++i)
            setStep (i, st[i].upToFirstOccurrenceOf (":", false, false).getIntValue(), st[i].containsChar (':') ? st[i].fromFirstOccurrenceOf (":", false, false).getFloatValue() : 1.0f);
        juce::StringArray ch; ch.addTokens (v["chord"].toString(), ",", {});
        if (ch.size() > 0) { std::vector<int> iv; for (auto& c : ch) iv.push_back (c.getIntValue()); setLearned (iv); }
        version.fetch_add (1);
    }
    juce::String toJson() const { return juce::JSON::toString (toVar(), true); }
    uint32_t getVersion() const { return version.load(); }

    std::atomic<bool> learning { false };   // the next chord played is stored (audio thread finishes it)

private:
    std::array<std::atomic<int>, kSteps> state;
    std::array<std::atomic<float>, kSteps> prob;
    std::array<std::atomic<int>, kMaxChord> learned;
    std::atomic<int> learnedCount { 0 };
    std::atomic<uint32_t> version { 0 };
};

// The chord for a preset (semitones above the key), then spread and inversion applied
inline std::vector<int> chordIntervals (int preset, const ArpStore& st, int spread, int inversion)
{
    std::vector<int> iv;
    switch (preset)
    {
        case 1: iv = { 0, 3, 7, 10 }; break;
        case 2: iv = { 0, 4, 7, 11 }; break;
        case 3: iv = { 0, 3, 7, 10, 14 }; break;
        case 4: iv = { 0, 2, 7 }; break;
        case 5: iv = { 0, 5, 7 }; break;
        case 6: iv = { 0, 7, 12 }; break;
        case 7: iv = { 0, 7, 15, 22, 26 }; break;   // rave minor: open voicing of 1 5 b3 b7 9
        case 8: iv = { 0, 12, 24 }; break;
        default: iv = st.getLearned(); break;
    }
    // inversion: +k moves the lowest k notes up an octave, -k the highest down
    const int n = (int) iv.size();
    if (n > 1)
    {
        for (int k = 0; k < std::abs (inversion); ++k)
        {
            std::sort (iv.begin(), iv.end());
            if (inversion > 0) iv.front() += 12; else iv.back() -= 12;
        }
        std::sort (iv.begin(), iv.end());
        // spread: every other note up by Spread octaves, opening the voicing
        if (spread > 0)
            for (int j = 1; j < n; j += 2) iv[(size_t) j] += 12 * spread;
        std::sort (iv.begin(), iv.end());
    }
    return iv;
}

//==============================================================================
struct ArpEvent { bool on; int note; float vel; float accent; };

// The arpeggiator: given the held notes it produces note-ons and note-offs. Its clock is a beat
// position (the DAW's while it plays), so steps land on the song's grid. The processor asks when
// the next event is due (nextDue), renders up to it, then calls tick at that beat.
class Arpeggiator
{
public:
    struct Settings
    {
        int mode = 0, octaves = 1, rate = 2, velMode = 0, repeats = 1, restart = 0, steps = 16;
        double swing = 0.0, gate = 0.5, velFixed = 0.8;
        bool latch = false;
        double beatsPerBar = 4.0, barOrigin = 0.0;
    };
    static double rateBeats (int r)
    {
        static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0 };
        return b[juce::jlimit (0, 8, r)];
    }

    void reset() { held.clear(); latched.clear(); physical.clear(); offs.clear(); stepIndex = 0; noteIndex = 0; repeatCount = 0; nextBeat = -1.0; lastBar = -1; gatePulse = 0.0f; lastStep = -1; }
    void configure (const Settings& s)
    {
        cfg = s;
        if (s.latch && ! latchedMode) latched = held;
        if (! s.latch && latchedMode) latched.clear();
        latchedMode = s.latch;
    }

    // keys the player holds (already expanded by chord memory)
    void keyDown (int note, float vel, double beatNow)
    {
        const bool wasActive = active();
        if (latchedMode && physical.empty()) latched.clear();   // a new gesture after letting go replaces the latched notes
        bool dup = false; for (auto& h : held) dup |= h.note == note;
        if (! dup) held.push_back ({ note, vel, ++order });
        physical.push_back (note);
        if (latchedMode) { bool d2 = false; for (auto& h : latched) d2 |= h.note == note; if (! d2) latched.push_back ({ note, vel, order }); }
        if (! wasActive || (latchedMode && physical.size() == 1 && latched.size() == 1))
        {
            if (cfg.restart == 0) { stepIndex = 0; noteIndex = 0; repeatCount = 0; }
            const double stepB = rateBeats (cfg.rate);
            nextBeat = gridAfter (beatNow - 0.15 * stepB, stepB);   // pressed just after a grid line: play now
        }
    }
    void keyUp (int note)
    {
        for (size_t i = 0; i < held.size(); ++i) if (held[i].note == note) { held.erase (held.begin() + (long) i); break; }
        for (size_t i = 0; i < physical.size(); ++i) if (physical[i] == note) { physical.erase (physical.begin() + (long) i); break; }
    }
    bool active() const { return ! notes().empty(); }
    bool sounding() const { return ! offs.empty(); }

    // beat at which the next event is due (a huge number when nothing is)
    double nextDue (double beatNow) const
    {
        double d = 1.0e300;
        for (auto& o : offs) d = std::min (d, o.beat);
        if (active() && nextBeat >= 0.0) d = std::min (d, swungBeat (nextBeat, rateBeats (cfg.rate)));
        return std::max (d, beatNow);
    }

    // everything due at beatNow
    template <typename Rng>
    void tick (double beatNow, const ArpStore& st, Rng&& rand01, std::vector<ArpEvent>& out)
    {
        const double stepB = rateBeats (cfg.rate), eps = 1.0e-7;
        for (size_t k = 0; k < offs.size();)
        {
            // due, or the transport jumped back: end it now
            if (offs[k].beat <= beatNow + eps || offs[k].beat - beatNow > 4.0 * stepB + 4.0) { out.push_back ({ false, offs[k].note, 0.0f, 0.0f }); offs.erase (offs.begin() + (long) k); }
            else ++k;
        }
        if (! active()) { nextBeat = -1.0; return; }
        if (nextBeat < 0.0 || nextBeat - beatNow > 2.0 * stepB || beatNow - nextBeat > 2.0 * stepB) nextBeat = gridAfter (beatNow - eps, stepB);   // (re)lock to the grid
        if (cfg.restart == 1)
        {
            const double bpb = cfg.beatsPerBar > 0.0 ? cfg.beatsPerBar : 4.0;
            const long bar = (long) std::floor ((beatNow - cfg.barOrigin) / bpb + 1.0e-9);
            if (bar != lastBar) { if (lastBar >= 0) { stepIndex = 0; noteIndex = 0; repeatCount = 0; } lastBar = bar; }
        }
        if (beatNow + eps >= swungBeat (nextBeat, stepB))
        {
            fire (nextBeat, stepB, st, rand01, out);
            nextBeat += stepB;
        }
    }
    // the arp switched off (or a reset): end what's sounding
    void allOff (std::vector<ArpEvent>& out)
    {
        for (auto& o : offs) out.push_back ({ false, o.note, 0.0f, 0.0f });
        offs.clear();
    }

    // the order the notes play in this cycle (for tests)
    std::vector<int> sequence() const { std::vector<int> v; for (auto& h : expanded()) v.push_back (h.note); return v; }
    int currentStep() const { return lastStep; }
    float position() const { return pos01; }
    float gatePulse = 0.0f;   // 1 at each step that plays; the processor decays it

private:
    struct Held { int note; float vel; uint64_t ord; };
    struct Off { double beat; int note; };
    const std::vector<Held>& notes() const { return latchedMode ? latched : held; }
    static double gridAfter (double b, double stepB) { return std::ceil (b / stepB - 1.0e-9) * stepB; }
    double swungBeat (double grid, double stepB) const
    {
        // every second step of the grid starts late by Swing of a step
        const long long k = std::llround (grid / stepB);
        return (k & 1) ? grid + cfg.swing * stepB : grid;
    }
    std::vector<Held> expanded() const
    {
        std::vector<Held> base = notes();
        if (cfg.mode != 4) std::sort (base.begin(), base.end(), [] (const Held& a, const Held& b) { return a.note < b.note; });
        else std::sort (base.begin(), base.end(), [] (const Held& a, const Held& b) { return a.ord < b.ord; });
        std::vector<Held> up;
        for (int o = 0; o < juce::jlimit (1, 4, cfg.octaves); ++o)
            for (auto h : base) { h.note += 12 * o; if (h.note <= 127) up.push_back (h); }
        auto bounce = [] (std::vector<Held>& v)   // up then back down, without repeating the ends
        {
            const auto fwd = v;
            for (size_t i = fwd.size() >= 2 ? fwd.size() - 2 : 0; i >= 1 && i < fwd.size(); --i) v.push_back (fwd[i]);
        };
        switch (cfg.mode)
        {
            case 1: std::reverse (up.begin(), up.end()); break;
            case 2: bounce (up); break;
            case 3: std::reverse (up.begin(), up.end()); bounce (up); break;
            default: break;
        }
        return up;
    }
    template <typename Rng>
    void fire (double gridBeat, double stepB, const ArpStore& st, Rng&& rand01, std::vector<ArpEvent>& out)
    {
        const int steps = juce::jlimit (1, ArpStore::kSteps, cfg.steps);
        const int si = stepIndex % steps;
        stepIndex = (stepIndex + 1) % steps;
        lastStep = si;
        const int state = st.getState (si);
        const float r = rand01();
        if (state == ArpStore::ST_Rest || r >= st.getProb (si)) return;   // a rest (or a skipped step) doesn't move the note on
        const auto seq = expanded();
        if (seq.empty()) return;
        const double offBeat = swungBeat (gridBeat, stepB) + juce::jlimit (0.05, 1.0, cfg.gate) * stepB * 0.999;
        const float accent = state == ArpStore::ST_Accent ? 1.0f : 0.0f;
        auto play = [&] (const Held& h)
        {
            float vel = cfg.velMode == 1 ? (float) cfg.velFixed : h.vel;
            if (accent > 0.0f) vel = std::min (1.0f, vel * 1.25f + 0.1f);
            for (size_t k = 0; k < offs.size(); ++k)   // the same note still sounding: end it first
                if (offs[k].note == h.note) { out.push_back ({ false, h.note, 0.0f, 0.0f }); offs.erase (offs.begin() + (long) k); break; }
            out.push_back ({ true, h.note, vel, accent });
            offs.push_back ({ offBeat, h.note });
        };
        if (cfg.mode == 6) { for (auto& h : seq) play (h); pos01 = 0.0f; }
        else
        {
            int idx;
            if (cfg.mode == 5) idx = juce::jlimit (0, (int) seq.size() - 1, (int) (rand01() * (float) seq.size()));
            else idx = noteIndex % (int) seq.size();   // (noteIndex keeps counting, so notes added later join in order)
            play (seq[(size_t) idx]);
            pos01 = seq.size() > 1 ? (float) idx / (float) (seq.size() - 1) : 0.0f;
            if (++repeatCount >= juce::jlimit (1, 4, cfg.repeats)) { repeatCount = 0; noteIndex = (noteIndex + 1) % 65536; }
        }
        gatePulse = 1.0f;
    }

    Settings cfg;
    std::vector<Held> held, latched;
    std::vector<int> physical;
    std::vector<Off> offs;
    bool latchedMode = false;
    uint64_t order = 0;
    int stepIndex = 0, noteIndex = 0, repeatCount = 0, lastStep = -1;
    long lastBar = -1;
    double nextBeat = -1.0;
    float pos01 = 0.0f;
};

} // namespace tg
