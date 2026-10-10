#pragma once
#include <JuceHeader.h>
#include "Params.h"

namespace tg
{

// "80 Hz", "350 Hz", "1.2 kHz", "20 kHz"
inline juce::String formatHz (float v)
{
    if (juce::roundToInt (v) < 1000) return juce::String (juce::roundToInt (v)) + " Hz";
    juce::String k (v / 1000.0, v < 9995.0f ? 2 : 1);
    if (k.containsChar ('.')) k = k.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return k + " kHz";
}

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
    // ---- effects rack
    {
        auto pct = [&] { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        auto db = [&] { return (v > 0.05f ? "+" : "") + fixed (v, 1) + " dB"; };
        if (id.startsWith ("fxMix")) return pct();
        if (id == "mbcXLow" || id == "mbcXHigh" || id == "smpRate" || id == "smpCutoff" || id == "sttMonoFreq") return formatHz (v);
        if (id.startsWith ("mbcIn") || id.startsWith ("mbcOut") || id == "mbcGain") return db();
        if (id.startsWith ("mbc") || id == "smpRes" || id == "smpNoise" || id == "smpDrive" || id == "rptShrink" || id == "rptGate" || id == "rptChance"
            || id == "flpDepth" || id == "flpManual" || id == "flpSpread" || id == "flpEnvSens" || id == "vshDepth" || id == "vshSmooth" || id == "sttWidth") return pct();
        if (id == "smpBits") return fixed (v, 1) + " bit";
        if (id == "rptPitch") return v < 0.05f ? juce::String ("none") : "-" + fixed (v, 1) + " st";
        if (id == "flpRate") return fixed (v, 2) + " Hz";
        if (id == "flpFeedback") { const int n = juce::roundToInt (v * 100.0f); return (n > 0 ? "+" : "") + juce::String (n) + "%"; }
        if (id == "sttHaas") return v < 0.05f ? juce::String ("off") : fixed (v, 1) + " ms";
    }
    if (id == "dsLane2Steps") { const int n = juce::roundToInt (v); return juce::String (n) + (n == 1 ? " step" : " steps"); }
    if (id == "dsOffset") { const int n = juce::roundToInt (v); return n == 0 ? juce::String ("none") : "+" + juce::String (n) + (n == 1 ? " step" : " steps"); }
    if (id == "dsSwing" || id == "dsDensity" || id == "dsProbScale") return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id == "dsSteps") { const int n = juce::roundToInt (v); return juce::String (n) + (n == 1 ? " step" : " steps"); }
    if (id == "dsFreeHz") return fixed (v, 2) + " Hz";
    if (id == "dsGlide" || id == "dsDepth") return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id == "mutSeed") return "#" + juce::String (juce::roundToInt (v));
    if (id == "ciRate") return fixed (v, 2) + " Hz";
    if (id == "fbTime") return fixed (v, v < 100 ? 1 : 0) + " ms";
    if (id == "fbTone") return v >= 1000 ? fixed (v / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (id == "spShift") return (v > 0 ? "+" : "") + fixed (v, 1) + " Hz";
    if (id == "spFormant") return (v > 0 ? "+" : "") + fixed (v, 2) + " st";
    if (id == "grSize") return fixed (v, v < 100 ? 1 : 0) + " ms";
    if (id == "grDensity") return fixed (v, v < 10 ? 1 : 0) + " /s";
    if (id == "grPosition") return fixed (v, 2) + " s";
    if (id == "grPitch") return (v > 0 ? "+" : "") + fixed (v, 2) + " st";
    if (id == "resModes") return juce::String (juce::roundToInt (v)) + " modes";
    if (id == "resPitch") return (v > 0 ? "+" : "") + fixed (v, 2) + " st";
    if (id == "resDecay") return fixed (v, v < 1 ? 2 : 1) + " s";
    if (id == "wmBits") return v >= 15.95f ? juce::String ("off") : fixed (v, 1) + " bit";
    if (id == "wmDown") return v <= 1.01f ? juce::String ("off") : "/" + fixed (v, 1);
    if (id == "arRatio") return "x" + fixed (v, 3);
    if (id == "arOffset" || id == "arShift") return (v > 0 ? "+" : "") + fixed (v, 1) + " Hz";
    if (id.startsWith ("wm") || id.startsWith ("ar") || id.startsWith ("dna") || id.startsWith ("res") || id.startsWith ("gr") || id.startsWith ("sp") || id.startsWith ("fb") || id.startsWith ("ci") || id == "mutAmount") return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id.startsWith ("macro") || id == "sceneX" || id == "sceneY") return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id == "ccANum" || id == "ccBNum") return "CC " + juce::String (juce::roundToInt (v));
    if (id == "keyboardOctave" || id.endsWith ("Oct")) return juce::String (juce::roundToInt (v)) + " oct";
    if (id.endsWith ("Semi")) { const int n = juce::roundToInt (v); return (n > 0 ? "+" : "") + juce::String (n) + " st"; }
    if (id.containsIgnoreCase ("Detune") || id == "supersawSpread") return juce::String (juce::roundToInt (v)) + " cents";
    if (id == "supersawDrift") return fixed (v, 1) + " cents";
    if (id == "polyphony") return juce::String (juce::roundToInt (v)) + " voices";
    if (id == "supersawVoices") return juce::String (juce::roundToInt (v)) + " voices";
    if (id == "bendRange") return juce::String (juce::roundToInt (v)) + " st";
    if (id == "filterCutoff" || id == "filter2Cutoff") return formatHz (v);
    if (id == "fEnvAmt" || id == "filter2EnvAmt") { const int n = juce::roundToInt (v / 100.0f); return (n > 0 ? "+" : "") + juce::String (n) + "%"; }   // 100% = 10 kHz
    if (id == "filterLfoAmt" || id == "filter2LfoAmt") { const int n = juce::roundToInt (v * 100.0f); return (n > 0 ? "+" : "") + juce::String (n) + "%"; }
    if (id == "filterBalance") { const int n = juce::roundToInt (v * 100.0f); return juce::String (100 - n) + " / " + juce::String (n); }
    if (id == "filterMix" || id == "filterKeyTrack" || id == "filter2Mix" || id == "filter2KeyTrack") return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (id == "filterDrive" || id == "filter2Drive") { const double db = 20.0 * std::log10 (std::max (1.0f, v)); return db < 0.05 ? juce::String ("0 dB") : "+" + fixed (db, 1) + " dB"; }
    if (id == "fmAmount" || id.startsWith ("fmSlot") || id == "complexFm") return juce::String (juce::roundToInt (v));
    if (id == "filterRes" || id == "filter2Res" || id == "complexShape") return fixed (v, 1);
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
    if (id == "fEnvAmt" || id == "filter2EnvAmt") return text.contains ("%") || std::abs (v) <= 100.0f ? v * 100.0f : v;   // percent (100% = 10 kHz), or Hz
    if ((id == "filterDrive" || id == "filter2Drive") && text.containsIgnoreCase ("db")) return std::pow (10.0f, v / 20.0f);
    if (text.containsIgnoreCase ("khz") || text.trim().endsWithIgnoreCase ("k")) return v * 1000.0f;
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
