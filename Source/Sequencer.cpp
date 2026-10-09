#include "Sequencer.h"

namespace tg::seqgen
{

static const int kScales[11][12] = {
    { 0,2,4,5,7,9,11,-1 },          // major
    { 0,2,3,5,7,8,10,-1 },          // minor
    { 0,2,3,5,7,8,11,-1 },          // harmonic minor
    { 0,2,3,5,7,9,11,-1 },          // melodic minor
    { 0,2,3,5,7,9,10,-1 },          // dorian
    { 0,1,3,5,7,8,10,-1 },          // phrygian
    { 0,2,4,5,7,9,10,-1 },          // mixolydian
    { 0,3,5,7,10,-1 },              // minor pentatonic
    { 0,2,4,7,9,-1 },               // major pentatonic
    { 0,3,5,6,7,10,-1 },            // blues
    { 0,1,2,3,4,5,6,7,8,9,10,11 }   // chromatic
};

std::vector<int> scaleNotes (int root, int scaleType)
{
    scaleType = juce::jlimit (0, 10, scaleType);
    std::vector<int> out;
    for (int i = 0; i < 12; ++i)
    {
        const int semi = kScales[scaleType][i];
        if (semi < 0 || (i > 0 && semi == 0)) break;
        out.push_back ((root + semi) % 12);
    }
    return out;
}

std::vector<int> euclid (int steps, int pulses, int rotation)
{
    steps = std::max (1, steps);
    pulses = juce::jlimit (0, steps, pulses);
    rotation = ((rotation % steps) + steps) % steps;
    std::vector<int> p ((size_t) steps, 0);
    if (pulses == 0) return p;
    if (pulses == steps) { std::fill (p.begin(), p.end(), 1); return p; }
    for (int i = 0; i < steps; ++i)
        p[(size_t) i] = ((i + 1) * pulses / steps) > (i * pulses / steps) ? 1 : 0;
    if (rotation)
    {
        std::vector<int> r ((size_t) steps);
        for (int i = 0; i < steps; ++i) r[(size_t) i] = p[(size_t) ((i - rotation + steps) % steps)];
        return r;
    }
    return p;
}

template <typename T> static T pick (const std::vector<T>& v, juce::Random& r) { return v[(size_t) r.nextInt ((int) v.size())]; }

static void restFrom (StepStore& s, int from)
{
    for (int i = from; i < 32; ++i) s.set (i, Step { -1, 0, TieRest, false });
}

void generatePhrase (StepStore& store, int length, int root, int scaleType, juce::Random& r)
{
    const auto notes = scaleNotes (root, scaleType);
    const std::vector<int> octPool { -1, 0, 0, 0, 1 };
    int prevNote = pick (notes, r), prevOct = 0;
    for (int i = 0; i < length; ++i)
    {
        const double roll = r.nextDouble();
        int note = prevNote, oct = prevOct, tie = TieNormal;
        bool accent = r.nextDouble() < 0.28;
        if (i == 0 || roll < 0.7) { note = pick (notes, r); oct = pick (octPool, r); }
        if (roll > 0.86) { note = -1; tie = TieRest; accent = false; }
        else if (i > 0 && roll > 0.72) tie = r.nextDouble() < 0.55 ? TieSlide : TieTie;
        if (note >= 0) { prevNote = note; prevOct = oct; }
        store.set (i, Step { note, note >= 0 ? oct : 0, tie, accent });
    }
    restFrom (store, length);
}

void generateEuclid (StepStore& store, int length, int pulses, int rotation, int root, int scaleType, juce::Random& r)
{
    const auto notes = scaleNotes (root, scaleType);
    const auto pat = euclid (length, pulses, rotation);
    const std::vector<int> octPool { -1, 0, 0, 1 };
    int prevNote = pick (notes, r), prevOct = 0;
    for (int i = 0; i < length; ++i)
    {
        if (pat[(size_t) i] != 1) { store.set (i, Step { -1, 0, TieRest, false }); continue; }
        int note = prevNote, oct = prevOct;
        const double move = r.nextDouble();
        if (i == 0 || move < 0.72) note = pick (notes, r);
        if (i == 0 || move < 0.4) oct = pick (octPool, r);
        int tie = TieNormal;
        if (pat[(size_t) ((i + 1) % length)] == 1 && r.nextDouble() < 0.22) tie = r.nextDouble() < 0.5 ? TieSlide : TieTie;
        const bool accent = (i % 4 == 0) || r.nextDouble() < 0.22;
        prevNote = note; prevOct = oct;
        store.set (i, Step { note, oct, tie, accent });
    }
    restFrom (store, length);
}

void euclidLiveUpdate (StepStore& store, int length, int pulses, int rotation, int root, int scaleType, juce::Random& r)
{
    const auto pat = euclid (length, pulses, rotation);
    std::vector<Step> activeSteps;
    for (int i = 0; i < length; ++i) { auto s = store.get (i); if (! s.isRest()) activeSteps.push_back (s); }
    if (activeSteps.empty()) { generateEuclid (store, length, pulses, rotation, root, scaleType, r); return; }
    size_t k = 0;
    for (int i = 0; i < length; ++i)
    {
        if (pat[(size_t) i] != 1) { store.set (i, Step { -1, 0, TieRest, false }); continue; }
        const Step src = activeSteps[k++ % activeSteps.size()];
        const bool nextPulse = pat[(size_t) ((i + 1) % length)] == 1;
        const int tie = nextPulse && (src.tie == TieTie || src.tie == TieSlide) ? src.tie : TieNormal;
        store.set (i, Step { src.note, src.oct, tie, src.accent });
    }
    restFrom (store, length);
}

} // namespace tg::seqgen
