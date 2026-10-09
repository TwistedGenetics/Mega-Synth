#pragma once
#include <JuceHeader.h>
#include "Params.h"

namespace tg
{

// Value display, following formatValue() in the browser synth.
inline juce::String formatParam (int idx, float v)
{
    const juce::String id (kParamIds[idx]);
    auto fixed = [] (double x, int d) { return juce::String (x, d); };
    static const char* noteNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    if (id == "osc4Root" || id == "wt2Root")
    {
        const int n = juce::roundToInt (v);
        return juce::String (n) + " (" + noteNames[((n % 12) + 12) % 12] + juce::String (n / 12 - 1) + ")";
    }
    if (id == "osc4Position" || id == "osc4Window" || id == "osc4LoopStart" || id == "osc4LoopEnd"
        || id == "wt2Position" || id == "wt2Window" || id == "wt2LoopStart" || id == "wt2LoopEnd"
        || id == "seqAccentAmt" || id == "velSens")
        return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id.startsWith ("mod") && id.endsWith ("Amt")) { const int n = juce::roundToInt (v * 100.0f); return (n > 0 ? "+" : "") + juce::String (n) + "%"; }
    if (id == "wmBits") return v >= 15.95f ? juce::String ("off") : fixed (v, 1) + " bit";
    if (id == "wmDown") return v <= 1.01f ? juce::String ("off") : "/" + fixed (v, 1);
    if (id == "arRatio") return "x" + fixed (v, 3);
    if (id == "arOffset" || id == "arShift") return (v > 0 ? "+" : "") + fixed (v, 1) + " Hz";
    if (id.startsWith ("wm") || id.startsWith ("ar")) return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id.startsWith ("macro") || id == "sceneX" || id == "sceneY") return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id == "ccANum" || id == "ccBNum") return "CC " + juce::String (juce::roundToInt (v));
    if (id == "keyboardOctave" || id.endsWith ("Oct")) return juce::String (juce::roundToInt (v)) + " oct";
    if (id.endsWith ("Semi")) { const int n = juce::roundToInt (v); return (n > 0 ? "+" : "") + juce::String (n) + " st"; }
    if (id.containsIgnoreCase ("Detune") || id == "supersawSpread") return juce::String (juce::roundToInt (v)) + " cents";
    if (id == "supersawDrift") return fixed (v, 1) + " cents";
    if (id == "polyphony") return juce::String (juce::roundToInt (v)) + " voices";
    if (id == "supersawVoices") return juce::String (juce::roundToInt (v)) + " voices";
    if (id == "bendRange") return juce::String (juce::roundToInt (v)) + " st";
    if (id == "filterCutoff") return juce::String (juce::roundToInt (v)) + " Hz";
    if (id == "fmAmount" || id.startsWith ("fmSlot") || id == "fEnvAmt" || id == "complexFm") return juce::String (juce::roundToInt (v));
    if (id == "filterRes" || id == "filterDrive" || id == "complexShape") return fixed (v, 1);
    if (id == "complexRatio") return "x" + fixed (v, 3);
    if (id == "porta") return fixed (v, 3) + " s";
    if (id == "seqTempo") return juce::String (juce::roundToInt (v)) + " BPM";
    if (id == "seqEuclidPulses") return juce::String (juce::roundToInt (v)) + " pulses";
    if (id == "seqEuclidRotate") return juce::String (juce::roundToInt (v)) + " steps";
    if (id == "seqLength") { const int n = juce::roundToInt (v); return juce::String (n) + (n == 1 ? " step" : " steps"); }
    if (id == "delayTime" || id == "reverseTime") return fixed (v, 2) + " s";
    if (id == "reverbSize") return fixed (v, 1) + " s";
    if (id == "reverbTone" || id == "tapeTone" || id == "shimmerBright") return juce::String (juce::roundToInt (v)) + " Hz";
    if (id == "chorusDepth" || id == "tapeFlutter" || id == "reversePitch") return fixed (v * 1000.0, 1) + " ms";
    if (id.contains ("Rate")) return fixed (v, 2) + " Hz";
    if (id.startsWith ("amp") || id.startsWith ("fEnv") || id.startsWith ("mEnv"))
    {
        if (id.endsWithChar ('S')) return fixed (v, 2);
        return fixed (v, 3) + " s";
    }
    return fixed (v, 2);
}

inline float parseParam (int idx, const juce::String& text)
{
    const juce::String id (kParamIds[idx]);
    float v = text.trim().upToFirstOccurrenceOf (" ", false, false).retainCharacters ("-0123456789.").getFloatValue();
    if (text.contains ("%")) v /= 100.0f;
    else if (text.contains ("ms") || ((id == "chorusDepth" || id == "tapeFlutter" || id == "reversePitch") && v > 0.5f)) v /= 1000.0f;
    return v;
}

inline juce::String formatModAmount (int target, float fraction)
{
    target = juce::jlimit (0, (int) MT_COUNT - 1, target);
    const float a = fraction * kTargetRange[target];
    const float r = kTargetRange[target];
    const int decimals = r >= 100 ? 0 : (r >= 10 ? 1 : (r >= 0.5f ? 2 : 4));
    juce::String s = juce::String (a, decimals);
    if (target == MT_pitch) s << " cents";
    else if (target == MT_cutoff || target == MT_tapeTone || target == MT_reverbTone || target == MT_shimmerBright) s << " Hz";
    return s;
}

} // namespace tg
