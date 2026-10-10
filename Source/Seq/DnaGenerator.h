#pragma once
// DNA Sequencer pattern generator and editing tools (message thread). The generator is seeded:
// the same seed and settings always give the same pattern; locked steps are never touched.
#include "DnaSequencer.h"

namespace tg
{

struct DnaRng
{
    uint32_t a, n = 0;
    explicit DnaRng (uint32_t seed, uint32_t salt) : a (dnaHash (seed, salt, 0xA11CEu)) {}
    float next() { return dnaHash01 (a, n++, 0x9E37u); }
    int below (int k) { return std::min (k - 1, (int) (next() * (float) k)); }
};

inline std::vector<int> dnaGenTypes (uint32_t mask)
{
    std::vector<int> t;
    for (int k = 1; k < DT_COUNT; ++k) if (mask == 0 || (mask & (1u << k)) != 0) t.push_back (k);
    if (t.empty()) t.push_back (DT_Fold);
    return t;
}

// Writes a new pattern into one lane (its first 'len' steps).
inline void dnaGenerate (DnaSeqStore& st, int pat, int lane, int len, const DnaGenSettings& g)
{
    len = juce::jlimit (1, DnaSeqStore::kSteps, len);
    DnaRng rng ((uint32_t) g.seed, (uint32_t) (g.style * 131 + lane * 17));
    const auto types = dnaGenTypes (g.mask);
    const float lo = std::min (g.amtMin, g.amtMax), hi = std::max (g.amtMin, g.amtMax);
    auto fresh = [&] (DnaStep& s)
    {
        s.type = types[(size_t) rng.below ((int) types.size())];
        s.amount = lo + (hi - lo) * rng.next();
        s.prob = 1.0f; s.ratchet = 1; s.glide = 0;
        if (rng.next() < g.extras)
        {
            if (rng.next() < 0.5f) s.ratchet = 2 + rng.below (3);
            else s.prob = 0.5f + 0.4f * rng.next();
        }
    };
    const int pulses = g.fill <= 0.0f ? 0 : std::max (1, (int) std::lround (g.fill * len));
    for (int i = 0; i < len; ++i)
    {
        DnaStep s = st.get (pat, lane, i);
        // every step draws the same number of random values, so a lock never shifts the others
        DnaStep cand = s;
        const float r = rng.next();
        DnaStep rnd; fresh (rnd);
        switch (g.style)
        {
            case DG_Euclid:
            {
                const int k = ((i + g.rotate) % len + len) % len;
                const bool hit = (k * pulses) % len < pulses;
                cand = rnd; if (! hit) cand.type = DT_Off;
                break;
            }
            case DG_Variation:
            {
                if (r < g.change)
                {
                    const float op = rng.next();
                    if (cand.type == DT_Off || op < 0.25f) cand = (cand.type == DT_Off) ? rnd : DnaStep { DT_Off, cand.amount };
                    else if (op < 0.5f) cand.type = rnd.type;
                    else if (op < 0.75f) cand.amount = juce::jlimit (0.0f, 1.0f, cand.amount + (rng.next() - 0.5f) * 0.5f);
                    else cand.ratchet = cand.ratchet >= 4 ? 1 : cand.ratchet + 1;
                }
                else rng.next();
                break;
            }
            default:
                cand = rnd; if (r >= g.fill) cand.type = DT_Off;
                break;
        }
        if (s.lock) continue;
        cand.lock = false;
        st.set (pat, lane, i, cand);
    }
}

using DnaLane = std::array<DnaStep, DnaSeqStore::kSteps>;
inline DnaLane dnaGetLane (const DnaSeqStore& st, int pat, int lane)
{
    DnaLane l; for (int i = 0; i < DnaSeqStore::kSteps; ++i) l[(size_t) i] = st.get (pat, lane, i); return l;
}
inline void dnaSetLane (DnaSeqStore& st, int pat, int lane, const DnaLane& l)
{
    for (int i = 0; i < DnaSeqStore::kSteps; ++i) st.set (pat, lane, i, l[(size_t) i]);
}
// rotate the first len steps by d (+1 = right)
inline void dnaShift (DnaSeqStore& st, int pat, int lane, int len, int d)
{
    len = juce::jlimit (1, DnaSeqStore::kSteps, len);
    auto l = dnaGetLane (st, pat, lane), o = l;
    for (int i = 0; i < len; ++i) o[(size_t) (((i + d) % len + len) % len)] = l[(size_t) i];
    dnaSetLane (st, pat, lane, o);
}
inline void dnaReverse (DnaSeqStore& st, int pat, int lane, int len)
{
    len = juce::jlimit (1, DnaSeqStore::kSteps, len);
    auto l = dnaGetLane (st, pat, lane), o = l;
    for (int i = 0; i < len; ++i) o[(size_t) i] = l[(size_t) (len - 1 - i)];
    dnaSetLane (st, pat, lane, o);
}
inline void dnaClear (DnaSeqStore& st, int pat, int lane)   // locked steps stay
{
    for (int i = 0; i < DnaSeqStore::kSteps; ++i)
        if (! st.get (pat, lane, i).lock) st.set (pat, lane, i, DnaStep {});
}

} // namespace tg
