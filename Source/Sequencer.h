#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "Params.h"

namespace tg
{

enum StepTie { TieNormal = 0, TieTie = 1, TieSlide = 2, TieRest = 3 };

struct Step
{
    int note = -1;      // -1 = REST, 0..11 = C..B
    int oct = 0;        // -2..2
    int tie = TieNormal;
    bool accent = false;

    bool isRest() const { return note < 0 || tie == TieRest; }
};

// 32 steps shared between the UI (message thread) and the audio thread.
class StepStore
{
public:
    StepStore()
    {
        for (int i = 0; i < 32; ++i)
        {
            Step s;
            if (i == 0) s.note = 0;   // browser default: step 1 = C, the rest REST
            set (i, s);
        }
    }

    Step get (int i) const { return unpack (steps[(size_t) juce::jlimit (0, 31, i)].load (std::memory_order_relaxed)); }
    void set (int i, const Step& s) { steps[(size_t) juce::jlimit (0, 31, i)].store (pack (s), std::memory_order_relaxed); version.fetch_add (1); }
    uint32_t getVersion() const { return version.load(); }

    juce::String toString() const
    {
        juce::StringArray a;
        for (int i = 0; i < 32; ++i) a.add (juce::String ((int) steps[(size_t) i].load()));
        return a.joinIntoString (",");
    }
    void fromString (const juce::String& str)
    {
        auto a = juce::StringArray::fromTokens (str, ",", "");
        for (int i = 0; i < 32 && i < a.size(); ++i) set (i, unpack ((uint32_t) a[i].getIntValue()));
    }

    static uint32_t pack (const Step& s)
    {
        return (uint32_t) (s.note + 1) | ((uint32_t) (s.oct + 2) << 5) | ((uint32_t) s.tie << 8) | ((uint32_t) (s.accent ? 1 : 0) << 10);
    }
    static Step unpack (uint32_t v)
    {
        Step s;
        s.note = juce::jlimit (-1, 11, (int) (v & 31) - 1);
        s.oct = juce::jlimit (-2, 2, (int) ((v >> 5) & 7) - 2);
        s.tie = juce::jlimit (0, 3, (int) ((v >> 8) & 3));
        s.accent = ((v >> 10) & 1) != 0;
        return s;
    }

private:
    std::array<std::atomic<uint32_t>, 32> steps {};
    std::atomic<uint32_t> version { 0 };
};

// Phrase / Euclidean generators, ported from the browser synth.
namespace seqgen
{
    std::vector<int> scaleNotes (int root, int scaleType);
    std::vector<int> euclid (int steps, int pulses, int rotation);
    void generatePhrase (StepStore&, int length, int root, int scaleType, juce::Random&);
    void generateEuclid (StepStore&, int length, int pulses, int rotation, int root, int scaleType, juce::Random&);
    void euclidLiveUpdate (StepStore&, int length, int pulses, int rotation, int root, int scaleType, juce::Random&);
}

} // namespace tg
