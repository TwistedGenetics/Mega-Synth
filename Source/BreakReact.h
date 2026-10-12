#pragma once
// Break-Reactive mode: the synth listens to the breaks (or a MIDI note, or the host's sidechain) and,
// on each hit it's listening for, resets oscillator phases, kicks the pitch, the drive, ducks the level
// and jumps the Reese notch. This class turns the hits into, for each render call, the samples where a
// hit lands and one shared envelope (attack / decay); each voice applies the actions with its own amounts.
//
// Timing: a slice starts on the first sample of the render call after it's triggered, so a break hit
// lands there, plus the Offset (and a random Humanise, either way). A negative Offset (the synth before
// the drum) works by playing the breaks that much later: the synth can't react to a hit before it's known.
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace tg
{

class BreakReact
{
public:
    enum HitType { Kick = 0, Snare = 1, Hat = 2, Any = 3 };
    struct Settings
    {
        bool on = false;
        int listen = 1;          // 0 all hits, 1 kicks, 2 snares, 3 hats
        int brk = 0;             // 0 any break, 1-3 that break
        float sens = 0.5f;       // how clearly a slice must be that kind of hit
        bool fromBreaks = true, fromSc = false;
        float scSens = 0.5f;
        float attackMs = 0.5f, decayMs = 120.0f, offsetMs = 0.0f, humanMs = 0.0f;
        // the rows: 0 the Main row (Listen above), 1 Kick, 2 Snare, 3 Hat. Each can be switched off, and counts
        // only some hits (on the beat, every Nth, a seeded chance); Velocity Follow scales its envelope by the hit
        struct Row { bool on = false; float sens = 0.5f, vel = 0.0f, chance = 1.0f; int beats = 0, every = 1; };
        Row row[4];
        int seed = 1;
        // the beat clock at this render call's first sample (beatInc 0: none)
        double beat0 = 0.0, beatInc = 0.0, barOrigin = 0.0, beatsPerBar = 4.0;
        Settings() { row[0].on = true; }
        bool typedRows() const { return row[1].on || row[2].on || row[3].on; }
    };
    static constexpr int kRows = 4;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        env.assign ((size_t) std::max (64, maxBlock) + 16, 0.0f);
        hit.assign (env.size(), 0);
        for (int r = 1; r < kRows; ++r) { renv[r].assign (env.size(), 0.0f); rhit[r].assign (env.size(), 0); }
        dl[0].assign ((size_t) (0.03 * sr) + 8, 0.0f); dl[1].assign (dl[0].size(), 0.0f);
        reset();
    }
    void reset()
    {
        nPending = 0; clock = 0; lastType = 0.0f; nHits = 0;
        for (auto& w : rs) w = RowState();
        for (auto& c : everyCount) c = 0;
        for (auto& b : everyBar) b = -1;
        for (auto& c : chanceCount) c = 0;
        for (int r = 1; r < kRows; ++r) { std::fill (renv[r].begin(), renv[r].end(), 0.0f); std::fill (rhit[r].begin(), rhit[r].end(), (uint8_t) 0); liveRow[r] = false; }
        std::fill (env.begin(), env.end(), 0.0f); std::fill (hit.begin(), hit.end(), (uint8_t) 0);
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        dpos = 0; scFast = scSlow = scLp = scHp = 0.0f; scHold = scWin = 0;
    }

    // ---- hits, between render calls (they land at the start of the next one)
    // a slice from break brk with its kick / snare / hat scores (0..1)
    void breakHit (const Settings& s, int brk, float kick, float snare, float hat, float vel)
    {
        if (! s.on || ! s.fromBreaks) return;
        if (s.brk > 0 && brk != s.brk - 1) return;
        const int type = kick >= snare && kick >= hat ? Kick : (snare >= hat ? Snare : Hat);
        const float score = type == Kick ? kick : (type == Snare ? snare : hat);
        auto need = [] (float sens) { return 0.85f - 0.75f * std::clamp (sens, 0.0f, 1.0f); };   // (sensitivity 50%: half as strong as the break's strongest)
        unsigned mask = 0;
        if (s.row[0].on && (s.listen == 0 || (type == s.listen - 1 && score >= need (s.sens)))) mask |= 1u;
        if (s.row[1 + type].on && score >= need (s.row[1 + type].sens)) mask |= 2u << type;
        if (mask == 0) return;
        schedule (s, clock + breakDelay (s), clock, type, vel, mask);
    }
    // a slice whose type is set by hand (Kick, Snare or Hat): it counts as that, whatever its scores
    void typedHit (const Settings& s, int brk, int type, float vel)
    {
        if (! s.on || ! s.fromBreaks) return;
        if (s.brk > 0 && brk != s.brk - 1) return;
        unsigned mask = 0;
        if (s.row[0].on && (s.listen == 0 || type == s.listen - 1)) mask |= 1u;
        if (s.row[1 + type].on) mask |= 2u << type;
        if (mask == 0) return;
        schedule (s, clock + breakDelay (s), clock, type, vel, mask);
    }
    // the trigger note (or anything else that should count as a hit right now)
    void noteHit (const Settings& s, float vel)
    {
        if (! s.on) return;
        unsigned mask = 0;
        for (int r = 0; r < kRows; ++r) if (s.row[r].on) mask |= 1u << r;   // (a trigger note is a hit for every row)
        if (mask == 0) return;
        schedule (s, clock, clock, Any, vel, mask);
    }

    // ---- once per render call, before the voices: the hits that land in it and the envelope
    // scL / scR: the host's sidechain for this call (or null)
    void begin (int n, const Settings& s, const float* scL, const float* scR)
    {
        const int len = std::min (n, (int) env.size());
        std::fill (hit.begin(), hit.begin() + len, (uint8_t) 0);
        nHits = 0;
        // sidechain transients: a fast follower jumping well above a slow one. To tell a kick from a snare or a hat
        // it listens to the first 5 ms (its low end and its top against the whole), so then the hit lands 5 ms after
        // the transient; listening to all hits, it lands on the transient
        if (s.on && s.fromSc && scL != nullptr)
        {
            const float aF = 1.0f - std::exp (-1.0f / (0.0005f * (float) sr)), rF = 1.0f - std::exp (-1.0f / (0.012f * (float) sr));
            const float aS = 1.0f - std::exp (-1.0f / (0.06f * (float) sr));
            const float cLo = 1.0f - std::exp (-2.0f * 3.14159265f * 150.0f / (float) sr), cHi = 1.0f - std::exp (-2.0f * 3.14159265f * 5000.0f / (float) sr);
            const float ratio = 1.6f + 3.0f * (1.0f - std::clamp (s.scSens, 0.0f, 1.0f)), floor = 0.002f + 0.05f * (1.0f - std::clamp (s.scSens, 0.0f, 1.0f));
            const int win = std::max (1, (int) (0.005 * sr));
            for (int i = 0; i < len; ++i)
            {
                const float x = 0.5f * (scL[i] + (scR != nullptr ? scR[i] : scL[i]));
                const float a = std::abs (x);
                scFast += (a - scFast) * (a > scFast ? aF : rF);
                scSlow += (a - scSlow) * aS;
                scLp += (x - scLp) * cLo; scHp += (x - scHp) * cHi;
                if (scWin > 0)
                {
                    const float h = x - scHp;
                    eLo += (double) scLp * scLp; eHi += (double) h * h; eAll += (double) x * x;
                    if (--scWin == 0)
                    {
                        // mostly low end: a kick; mostly top: a hat; otherwise a snare
                        const int type = eLo > 0.5 * eAll ? Kick : (eHi > 0.4 * eAll ? Hat : Snare);
                        unsigned mask = 0;
                        if (s.row[0].on && s.listen > 0 && type == s.listen - 1) mask |= 1u;
                        if (s.row[1 + type].on) mask |= 2u << type;
                        if (mask != 0) schedule (s, clock + i, clock + i, type, scVel, mask);
                    }
                }
                if (scHold > 0) { --scHold; continue; }
                if (scFast > floor && scFast > scSlow * ratio)
                {
                    scHold = (int) (0.04 * sr);
                    scVel = std::min (1.0f, scFast * 2.0f);
                    if (s.listen == 0 && s.row[0].on) schedule (s, clock + i, clock + i, Any, scVel, 1u);
                    if ((s.listen > 0 && s.row[0].on) || s.typedRows()) { scWin = win; eLo = eHi = eAll = 0.0; }
                }
            }
        }
        // the hits due in this call: mark them and restart the envelope there
        const float atk = std::max (0.25f, s.attackMs) * 0.001f * (float) sr;
        const float aInc = 1.0f / std::max (1.0f, atk);
        const float dec = std::exp (-4.6f / std::max (1.0f, s.decayMs * 0.001f * (float) sr));   // (down to 1% over the decay time)
        int next = 0;
        sortPending();
        {
            // the rows' Beats / Every / Chance, for the hits not judged yet (in the order they land), on this call's beat clock
            int k = 0;
            for (int j = 0; j < nPending; ++j)
            {
                auto pd = pending[j];
                if (! pd.filtered) { pd.mask = filterRows (s, pd.at, pd.mask); pd.filtered = true; }
                if (pd.mask != 0) pending[k++] = pd;
            }
            nPending = k;
        }
        bool rowsUsed[kRows] = { true, false, false, false };
        for (int r = 1; r < kRows; ++r)
        {
            rowsUsed[r] = s.row[r].on || rs[r].e != 0.0f;
            if (rowsUsed[r]) std::fill (rhit[r].begin(), rhit[r].begin() + len, (uint8_t) 0);
            else if (liveRow[r]) { std::fill (renv[r].begin(), renv[r].begin() + len, 0.0f); std::fill (rhit[r].begin(), rhit[r].begin() + len, (uint8_t) 0); }
            liveRow[r] = rowsUsed[r];
        }
        nRowHits[0] = nRowHits[1] = nRowHits[2] = nRowHits[3] = 0;
        nCallHits = 0;
        for (int i = 0; i < len; ++i)
        {
            const int64_t t = clock + i;
            while (next < nPending && pending[next].at <= t)
            {
                const auto& pd = pending[next];
                ++nHits; if (pd.type != Any) lastType = 0.5f * (float) pd.type;
                if (nCallHits < kMaxCallHits) callHit[nCallHits++] = { i, pd.type, pd.mask };
                for (int r = 0; r < kRows; ++r)
                {
                    if ((pd.mask & (1u << r)) == 0) continue;
                    ++nRowHits[r];
                    auto& w = rs[r];
                    w.stage = 1;
                    w.level = s.row[r].vel > 0.0f ? 1.0f - s.row[r].vel + s.row[r].vel * std::clamp (pd.vel, 0.0f, 1.0f) : 1.0f;
                    if (r == 0) hit[(size_t) i] = 1;
                    else rhit[r][(size_t) i] = 1;
                }
                ++next;
            }
            {
                // the Main row (as before Break React had rows)
                auto& w = rs[0];
                if (w.stage == 1) { w.e += aInc; if (w.e >= 1.0f) { w.e = 1.0f; w.stage = 2; } }
                else if (w.stage == 2) { w.e *= dec; if (w.e < 1.0e-5f) { w.e = 0.0f; w.stage = 0; } }
                env[(size_t) i] = w.level == 1.0f ? w.e : w.e * w.level;
            }
            for (int r = 1; r < kRows; ++r)
            {
                if (! rowsUsed[r]) continue;
                auto& w = rs[r];
                if (w.stage == 1) { w.e += aInc; if (w.e >= 1.0f) { w.e = 1.0f; w.stage = 2; } }
                else if (w.stage == 2) { w.e *= dec; if (w.e < 1.0e-5f) { w.e = 0.0f; w.stage = 0; } }
                renv[r][(size_t) i] = w.e * w.level;
            }
        }
        // keep the ones still to come
        int k = 0;
        for (int j = next; j < nPending; ++j) pending[k++] = pending[j];
        nPending = k;
        clock += n;
    }

    // Break-Reactive mode off: nothing pending, the envelope at rest, the clock running on
    void idle (int n)
    {
        if (nPending > 0 || rs[0].e != 0.0f || nHits > 0) { nPending = 0; rs[0].e = 0.0f; rs[0].stage = 0; nHits = 0; }
        nCallHits = 0;
        for (int r = 1; r < kRows; ++r) { rs[r].e = 0.0f; rs[r].stage = 0; if (liveRow[r]) { std::fill (renv[r].begin(), renv[r].end(), 0.0f); std::fill (rhit[r].begin(), rhit[r].end(), (uint8_t) 0); liveRow[r] = false; } }
        clock += n;
    }

    // what the voices read (for the current render call)
    const float* envelope() const { return env.data(); }
    const uint8_t* hits() const { return hit.data(); }
    // row r (0 the Main row, the same as envelope() / hits()); live: it has something this call
    const float* envelope (int r) const { return r == 0 ? env.data() : renv[r].data(); }
    const uint8_t* hits (int r) const { return r == 0 ? hit.data() : rhit[r].data(); }
    bool rowLive (int r) const { return r == 0 || rowLiveFlag (r); }
    int rowHitCount (int r) const { return nRowHits[r]; }
    // this call's hits, for the page's lights: the sample, the type (Kick, Snare, Hat, Any) and the rows it fired
    struct CallHit { int at, type; unsigned mask; };
    static constexpr int kMaxCallHits = 16;
    int callHitCount() const { return nCallHits; }
    const CallHit& callHitAt (int k) const { return callHit[k]; }
    int hitCount() const { return nHits; }
    float hitType() const { return lastType; }     // the last hit: 0 kick, 0.5 snare, 1 hat
    int64_t now() const { return clock; }

    // the breaks' delay for a negative Offset (samples)
    int breakDelay (const Settings& s) const
    {
        if (! s.on || ! s.fromBreaks) return 0;
        const float early = s.humanMs - s.offsetMs;   // the earliest a reaction can be, before the hit
        return early > 0.0f ? std::min ((int) dl[0].size() - 4, (int) std::lround (early * 0.001 * sr)) : 0;
    }
    // the breaks through that delay (in place); run every call so the line empties after it's turned off
    void delayBreaks (float* l, float* r, int n, int delay)
    {
        const int N = (int) dl[0].size();
        if (N < 8) return;
        for (int i = 0; i < n; ++i)
        {
            dl[0][(size_t) dpos] = l[i]; dl[1][(size_t) dpos] = r[i];
            const int rp = (dpos - delay + N) % N;
            l[i] = dl[0][(size_t) rp]; r[i] = dl[1][(size_t) rp];
            dpos = (dpos + 1) % N;
        }
    }

private:
    struct Pending { int64_t at; int type; float vel; unsigned mask; bool filtered; };
    // which rows count a hit landing at sample `at`: On the beat / Backbeat / Off-beats, every Nth (counted per bar), and a
    // chance seeded by the hit's place in the song (so a playthrough repeats exactly)
    unsigned filterRows (const Settings& s, int64_t at, unsigned mask)
    {
        const bool clocked = s.beatInc > 0.0;
        const double b = s.beat0 + (double) (at - clock) * s.beatInc - s.barOrigin;
        const double bpb = std::max (1.0, s.beatsPerBar);
        for (int r = 0; r < kRows; ++r)
        {
            if ((mask & (1u << r)) == 0) continue;
            const auto& w = s.row[r];
            bool ok = true;
            if (w.beats > 0 && clocked)
            {
                const double nearest = std::round (b);
                const bool onBeat = std::abs (b - nearest) < 0.1;
                const int64_t idx = (int64_t) nearest;
                const int inBar = (int) (((idx % (int64_t) bpb) + (int64_t) bpb) % (int64_t) bpb);
                ok = w.beats == 1 ? onBeat : (w.beats == 2 ? (onBeat && (inBar & 1) == 1) : ! onBeat);
            }
            if (ok && w.every > 1)
            {
                if (clocked)
                {
                    const int64_t bar = (int64_t) std::floor ((b + 0.1) / bpb);
                    if (bar != everyBar[r]) { everyBar[r] = bar; everyCount[r] = 0; }
                }
                ok = everyCount[r] % w.every == 0;
                ++everyCount[r];
            }
            if (ok && w.chance < 1.0f)
            {
                const uint64_t place = clocked ? (uint64_t) (int64_t) std::llround (b * 24.0) : (uint64_t) chanceCount[r]++;
                uint64_t h = (uint64_t) (uint32_t) s.seed * 0x9E3779B97F4A7C15ull ^ (uint64_t) (r + 1) * 0xC2B2AE3D27D4EB4Full ^ place * 0x165667B19E3779F9ull;
                h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull; h ^= h >> 33;
                ok = (float) (h & 0xFFFFFF) / 16777216.0f < w.chance;
            }
            if (! ok) mask &= ~(1u << r);
        }
        return mask;
    }
    void schedule (const Settings& s, int64_t base, int64_t earliest, int type, float vel, unsigned mask)   // (no earlier than earliest: nothing reacts before it's heard)
    {
        if (nPending >= kMaxPending) return;
        float ms = s.offsetMs;
        if (s.humanMs > 0.0f)
        {
            // either way for the breaks (they're played late enough to allow it); only later for a note or the
            // sidechain, which can't be heard before they happen
            rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
            const float u = (float) (rnd & 0xffffff) / 16777216.0f;
            ms += base > earliest ? s.humanMs * (2.0f * u - 1.0f) : s.humanMs * u;
        }
        const int64_t at = std::max (earliest, base + (int64_t) std::lround (ms * 0.001 * sr));
        pending[nPending++] = { at, type, vel, mask, false };   // (which rows count it is decided in begin(), on that call's beat clock)
    }
    void sortPending() { std::sort (pending, pending + nPending, [] (const Pending& a, const Pending& b) { return a.at < b.at; }); }

    static constexpr int kMaxPending = 64;
    double sr = 48000.0;
    Pending pending[kMaxPending];
    int nPending = 0;
    std::vector<float> env;
    std::vector<uint8_t> hit;
    int nHits = 0;
    struct RowState { float e = 0.0f, level = 1.0f; int stage = 0; };
    RowState rs[kRows];
    std::vector<float> renv[kRows];
    std::vector<uint8_t> rhit[kRows];
    bool liveRow[kRows] {};
    bool rowLiveFlag (int r) const { return liveRow[r]; }
    int nRowHits[kRows] {};
    CallHit callHit[kMaxCallHits] {}; int nCallHits = 0;
    int everyCount[kRows] {}; int64_t everyBar[kRows] { -1, -1, -1, -1 }; int chanceCount[kRows] {};
    int64_t clock = 0;
    float lastType = 0.0f;
    uint32_t rnd = 0x2545F491u;
    std::vector<float> dl[2];
    int dpos = 0;
    float scFast = 0.0f, scSlow = 0.0f, scLp = 0.0f, scHp = 0.0f, scVel = 0.0f;
    double eLo = 0.0, eHi = 0.0, eAll = 0.0;
    int scHold = 0, scWin = 0;
};

} // namespace tg
