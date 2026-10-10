#pragma once
// Sonic DNA Sequencer: 1-32 steps, each a transform type and an amount. The running step
// drives that transform's own set of destinations (Fold moves the folder, Crush the bit
// depth...), crossfaded into the next step by Glide, and is also a Mod Matrix source.
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

class DnaSeqStore
{
public:
    static constexpr int kSteps = 32;
    DnaSeqStore() { for (int i = 0; i < kSteps; ++i) { type[i].store (DT_Off); amount[i].store (0.7f); } }
    int getType (int i) const { return type[(size_t) (i & 31)].load (std::memory_order_relaxed); }
    float getAmount (int i) const { return amount[(size_t) (i & 31)].load (std::memory_order_relaxed); }
    void set (int i, int t, float a)
    {
        type[(size_t) (i & 31)].store (juce::jlimit (0, DT_COUNT - 1, t));
        amount[(size_t) (i & 31)].store (juce::jlimit (0.0f, 1.0f, a));
        version.fetch_add (1);
    }
    uint32_t getVersion() const { return version.load(); }
    juce::String toString() const
    {
        juce::StringArray a;
        for (int i = 0; i < kSteps; ++i) a.add (juce::String (getType (i)) + ":" + juce::String (getAmount (i), 3));
        return a.joinIntoString (",");
    }
    void fromString (const juce::String& s)
    {
        juce::StringArray a; a.addTokens (s, ",", {});
        for (int i = 0; i < kSteps; ++i)
        {
            if (i < a.size()) set (i, a[i].upToFirstOccurrenceOf (":", false, false).getIntValue(), a[i].fromFirstOccurrenceOf (":", false, false).getFloatValue());
            else set (i, DT_Off, 0.7f);
        }
    }
private:
    std::array<std::atomic<int>, kSteps> type;
    std::array<std::atomic<float>, kSteps> amount;
    std::atomic<uint32_t> version { 0 };
};

// Weights of each transform at position 'pos' (in steps), with Glide crossfading into the next step.
// Returns the sequencer's output value (the blended step amount) for the Mod Matrix source.
inline float dnaSeqWeights (const DnaSeqStore& st, double pos, int numSteps, float glide, float* w)
{
    for (int t = 0; t < DT_COUNT; ++t) w[t] = 0.0f;
    numSteps = juce::jlimit (1, DnaSeqStore::kSteps, numSteps);
    const double fl = std::floor (pos);
    const int cur = (int) (((int64_t) fl % numSteps + numSteps) % numSteps);
    const int nxt = (cur + 1) % numSteps;
    const float frac = (float) (pos - fl);
    float x = 0.0f;   // share of the next step
    if (glide > 0.0f && frac > 1.0f - glide)
    {
        const float u = (frac - (1.0f - glide)) / glide;
        x = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u);
    }
    const float ac = st.getAmount (cur), an = st.getAmount (nxt);
    w[st.getType (cur)] += (1.0f - x) * ac;
    w[st.getType (nxt)] += x * an;
    w[DT_Off] = 0.0f;
    const float vc = st.getType (cur) == DT_Off ? 0.0f : ac, vn = st.getType (nxt) == DT_Off ? 0.0f : an;
    return vc + (vn - vc) * x;
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

} // namespace tg
