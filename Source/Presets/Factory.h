#pragma once
// Factory presets: the 28 demo patches that ship inside the plugin.
//
// Each preset is written as code against the parameter table (so it can never refer to a
// control that doesn't exist) and is built on top of a clean starting point: every parameter
// at its default, every oscillator level at zero and the legacy LFO/Env slots silent. The
// processor turns a built preset into a normal patch state (parameters, mod-matrix routes,
// DNA Sequencer steps, scenes and macro names), so anything a factory preset does can be
// saved, edited, mutated and bred like a user patch.
#include <JuceHeader.h>
#include <array>
#include <vector>
#include "../Params.h"
#include "../Registry.h"
#include "../ModMatrix.h"
#include "../Seq/DnaSequencer.h"

namespace tg
{

struct PresetBuilder
{
    struct Route { int src = MS_None, dst = -1; float depth = 0; int curve = MC_Linear; bool unipolar = false; int via = MS_None; float viaDepth = 1.0f; float smoothMs = 0; };

    std::array<float, P_COUNT> v {};
    std::vector<Route> routes;
    std::array<DnaTransform, DnaSeqStore::kSteps> dnaType {};
    std::array<float, DnaSeqStore::kSteps> dnaAmt {};
    bool scenes[4] { false, false, false, false };
    std::array<std::array<float, P_COUNT>, 4> scene {};
    juce::String macroNames[8];
    // DNA Sequencer v2 steps (any pattern / lane, with probability, ratchet and glide), the chain,
    // and the effects rack order and Volume Shaper curve. Older presets don't use these.
    struct DnaStepAt { int pat, lane, i; DnaStep s; };
    std::vector<DnaStepAt> dnaSteps2;
    juce::String dnaChain;
    std::array<int, FS_COUNT> rack {};
    bool rackSet = false;
    int shaperCurve = 0;

    PresetBuilder()
    {
        for (int i = 0; i < P_COUNT; ++i) v[(size_t) i] = meta (i).def;
        for (int p : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain, P_complexGain, P_supersawGain,
                       P_lfoAssignAmt0, P_envAssignAmt0 })
            v[(size_t) p] = 0.0f;
        dnaType.fill (DT_Off);
        dnaAmt.fill (0.7f);
    }

    // Choosing a classic model also picks its own slope (Low Pass at that slope = the classic circuit);
    // set P_filterSlope / P_filterType afterwards to use the model's character on the multimode core.
    PresetBuilder& set (int p, float plain)
    {
        v[(size_t) p] = plain;
        if (p == P_filterMode) v[(size_t) P_filterSlope] = (float) nativeFilterSlope ((int) std::lround (plain));
        return *this;
    }

    // A mod-matrix route; depth is -1..1 of the destination's range (the route's Amount knob).
    PresetBuilder& route (int src, int dst, float depth, int curve = MC_Linear, bool unipolar = false, float smoothMs = 0.0f)
    {
        jassert (routes.size() < (size_t) kNumRoutes);
        Route r; r.src = src; r.dst = dst; r.depth = depth; r.curve = curve; r.unipolar = unipolar; r.smoothMs = smoothMs;
        routes.push_back (r);
        return *this;
    }

    // Names a macro and routes it (0..1, unipolar) to each destination.
    PresetBuilder& macro (int k, const juce::String& name, std::initializer_list<std::pair<int, float>> dsts)
    {
        macroNames[k] = name;
        for (auto& d : dsts) route (MS_Macro1 + k, d.first, d.second);
        return *this;
    }

    PresetBuilder& dna (std::initializer_list<std::pair<DnaTransform, float>> steps)
    {
        int i = 0;
        for (auto& s : steps) { dnaType[(size_t) i] = s.first; dnaAmt[(size_t) i] = s.second; ++i; }
        return *this;
    }

    // One DNA step: pattern 0-3 (A-D), lane 0/1, step 0-31
    PresetBuilder& dnaStep (int pat, int lane, int i, int type, float amount, float prob = 1.0f, int ratchet = 1, int glide = 0)
    {
        DnaStep st; st.type = type; st.amount = amount; st.prob = prob; st.ratchet = ratchet; st.glide = glide;
        dnaSteps2.push_back ({ pat, lane, i, st });
        return *this;
    }
    // A whole lane from a string, one character per step: '.' off, 'f' Fold, 'c' Crush, 'd' Decimate, 's' Shift,
    // 'r' Ring, 'm' FM, 'p' Splice, 'o' Resonate (res), 'F' Filter, 'g' Grain, 'b' Blur, 'u' Mutate, 'O' Octave.
    // Upper-case letters other than F and O aren't used; amounts come from the matching entry of amts (cycled).
    PresetBuilder& dnaLane (int pat, int lane, const char* pattern, std::initializer_list<float> amts = { 0.8f })
    {
        std::vector<float> a (amts);
        int k = 0;
        for (int i = 0; pattern[i] != 0 && i < DnaSeqStore::kSteps; ++i)
        {
            int t = DT_Off;
            switch (pattern[i])
            {
                case 'f': t = DT_Fold; break;      case 'c': t = DT_Crush; break;   case 'd': t = DT_Decimate; break;
                case 's': t = DT_Shift; break;     case 'r': t = DT_Ring; break;    case 'm': t = DT_Fm; break;
                case 'p': t = DT_Splice; break;    case 'o': t = DT_Resonate; break; case 'F': t = DT_Filter; break;
                case 'g': t = DT_Grain; break;     case 'b': t = DT_Blur; break;    case 'u': t = DT_Mutate; break;
                case 'O': t = DT_Octave; break;    default: break;
            }
            dnaStep (pat, lane, i, t, t == DT_Off ? 0.7f : a[(size_t) (k++ % (int) a.size())]);
        }
        return *this;
    }
    PresetBuilder& rackOrder (std::initializer_list<int> order)
    {
        int i = 0; for (int s : order) if (i < FS_COUNT) rack[(size_t) i++] = s;
        rackSet = i == FS_COUNT;
        jassert (rackSet);
        return *this;
    }

    // Stores scene k as the current settings plus the given changes (call after the base sound is set).
    PresetBuilder& storeScene (int k, std::initializer_list<std::pair<int, float>> changes)
    {
        scene[(size_t) k] = v;
        for (auto& c : changes) scene[(size_t) k][(size_t) c.first] = c.second;
        scenes[k] = true;
        return *this;
    }

    juce::String dnaString() const
    {
        juce::StringArray a;
        for (int i = 0; i < DnaSeqStore::kSteps; ++i) a.add (juce::String ((int) dnaType[(size_t) i]) + ":" + juce::String (dnaAmt[(size_t) i], 3));
        return a.joinIntoString (",");
    }
};

struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* description;
    void (*build) (PresetBuilder&);
};

namespace factory_detail
{
    using B = PresetBuilder;

    inline void monoBass (B& b, float glide = 0.0f)
    {
        b.set (P_polyphony, 1).set (P_porta, glide).set (P_ampA, 0.003f).set (P_ampD, 0.3f).set (P_ampS, 1.0f).set (P_ampR, 0.12f)
         .set (P_reverbMix, 0.0f).set (P_chorusMix, 0.0f).set (P_delayMix, 0.0f).set (P_bassKeep, 0.8f);
    }

    // 1 ------------------------------------------------------------------------------------------
    inline void initGenome (B& b)
    {
        b.set (P_osc1Gain, 0.8f).set (P_osc2Gain, 0.5f).set (P_subGain, 0.35f)
         .set (P_filterCutoff, 2600).set (P_filterRes, 1.5f).set (P_envAssignAmt0, 2500.0f / 8000.0f);
        b.macro (0, "Mutate",    { { P_mutAmount, 1.0f } });
        b.macro (1, "Fold",      { { P_wmMix, 0.7f }, { P_wmFold, 0.5f } });
        b.macro (2, "Splice",    { { P_dnaMix, 0.9f } });
        b.macro (3, "Resonate",  { { P_resMix, 0.7f } });
        b.macro (4, "Shift",     { { P_arShiftMix, 0.8f }, { P_arShift, 0.05f } });
        b.macro (5, "Grain",     { { P_grMix, 0.8f } });
        b.macro (6, "Instability", { { P_ciAmount, 1.0f } });
        b.macro (7, "Space",     { { P_reverbMix, 0.5f }, { P_delayMix, 0.4f } });
    }

    // 2 ------------------------------------------------------------------------------------------
    inline void reeseMutation (B& b)
    {
        monoBass (b, 0.02f);
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.7f).set (P_osc1Detune, -14)
         .set (P_osc2Wave, 0).set (P_osc2Gain, 0.7f).set (P_osc2Detune, 14).set (P_osc2Oct, 0)
         .set (P_subWave, 1).set (P_subGain, 0.55f).set (P_subOct, -1)
         .set (P_filterMode, 2).set (P_filterCutoff, 850).set (P_filterRes, 3.0f).set (P_filterDrive, 3.5f)
         .set (P_fEnvAmt, 500).set (P_fEnvD, 0.4f).set (P_fEnvS, 0.4f)
         .set (P_wmMix, 0.35f).set (P_wmDrive, 0.5f).set (P_wmFold, 0.25f)
         .set (P_ciAmount, 0.25f).set (P_ciRate, 0.3f).set (P_ciPitch, 0.3f)
         .set (P_lfo1Wave, 0).set (P_lfo1Rate, 0.18f).set (P_lfo1Depth, 1.0f)
         .set (P_lfo2Wave, 1).set (P_lfo2Rate, 0.31f).set (P_lfo2Depth, 1.0f).set (P_warmth, 0.65f);
        b.route (MS_Lfo1, P_filterCutoff, 0.12f);
        b.route (MS_Lfo2, P_wmFold, 0.3f);
        b.macro (0, "Growl", { { P_wmMix, 0.5f }, { P_wmFold, 0.4f }, { P_filterCutoff, 0.25f } });
        b.macro (1, "Mutate", { { P_mutAmount, 0.8f } });
    }

    // 3 ------------------------------------------------------------------------------------------
    inline void subHelix (B& b)
    {
        monoBass (b);
        b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.9f).set (P_osc1Oct, -1)
         .set (P_subWave, 1).set (P_subGain, 0.4f).set (P_subOct, -1)
         .set (P_filterCutoff, 1800).set (P_filterRes, 0.7f)
         .set (P_arMod, 0).set (P_arRatio, 2.0f).set (P_arFm, 0.06f).set (P_arFmTarget, 1)
         .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.12f).set (P_mEnv1S, 0.0f);
        b.route (MS_ModEnv1, P_arFm, 0.25f);
        b.route (MS_Velocity, P_arFm, 0.1f);
        b.macro (0, "Helix", { { P_arFm, 0.35f }, { P_arRatio, 0.08f } });
        b.macro (1, "Drive", { { P_wmMix, 0.5f }, { P_wmDrive, 0.5f } });
    }

    // 4 ------------------------------------------------------------------------------------------
    inline void neuroSplice (B& b)
    {
        monoBass (b, 0.03f);
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.75f)
         .set (P_osc2Wave, 1).set (P_osc2Gain, 0.6f).set (P_osc2Oct, -1).set (P_osc2Detune, 0)
         .set (P_subWave, 1).set (P_subGain, 0.4f)
         .set (P_dnaMix, 0.8f).set (P_dnaMode, 3).set (P_dnaA, 0).set (P_dnaB, 1).set (P_dnaAmount, 0.5f).set (P_dnaChar, 0.6f)
         .set (P_filterMode, 2).set (P_filterCutoff, 1300).set (P_filterRes, 6.0f).set (P_filterDrive, 4.0f)
         .set (P_wmMix, 0.4f).set (P_wmFold, 0.45f).set (P_wmDrive, 0.4f)
         .set (P_lfo1Wave, 0).set (P_lfo1Rate, 3.0f).set (P_lfo1Depth, 1.0f);
        b.route (MS_Lfo1, P_dnaAmount, 0.45f);
        b.route (MS_Lfo1, P_filterCutoff, 0.3f, MC_SCurve);
        b.macro (0, "Wobble Rate", { { P_lfo1Rate, 0.35f } });
        b.macro (1, "Splice", { { P_dnaChar, 0.4f }, { P_dnaMix, 0.2f } });
        b.route (MS_ModWheel, P_wmFold, 0.4f);
    }

    // 5 ------------------------------------------------------------------------------------------
    inline void foldedAcid (B& b)
    {
        monoBass (b, 0.06f);
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.85f)
         .set (P_filterMode, 12).set (P_filterCutoff, 480).set (P_filterRes, 17.0f).set (P_filterDrive, 2.0f)
         .set (P_fEnvAmt, 3600).set (P_fEnvA, 0.002f).set (P_fEnvD, 0.22f).set (P_fEnvS, 0.0f)
         .set (P_ampD, 0.4f).set (P_ampS, 0.8f)
         .set (P_wmMix, 0.6f).set (P_wmFold, 0.45f).set (P_wmDrive, 0.35f)
         .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.18f).set (P_mEnv1S, 0.0f)
         .set (P_delayMix, 0.15f).set (P_delaySync, 6).set (P_delayFeedback, 0.3f);
        b.route (MS_ModEnv1, P_wmFold, 0.3f);
        b.route (MS_Velocity, P_filterCutoff, 0.15f);
        b.macro (0, "Acid", { { P_filterCutoff, 0.3f }, { P_wmFold, 0.3f } });
        b.macro (1, "Resonance", { { P_filterRes, 0.2f } });
    }

    // 6 ------------------------------------------------------------------------------------------
    inline void ringMutantBell (B& b)
    {
        b.set (P_masterVolume, 0.18f).set (P_osc1Wave, 3).set (P_osc1Gain, 0.8f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.2f).set (P_osc2Oct, 1)
         .set (P_arMod, 0).set (P_arRatio, 3.5f).set (P_arRing, 0.55f)
         .set (P_resMix, 0.35f).set (P_resTuning, 5).set (P_resDecay, 2.8f).set (P_resDamping, 0.3f).set (P_resModes, 10)
         .set (P_ampA, 0.002f).set (P_ampD, 1.6f).set (P_ampS, 0.0f).set (P_ampR, 1.4f)
         .set (P_filterCutoff, 9000).set (P_reverbMix, 0.35f).set (P_chorusMix, 0.15f);
        b.route (MS_Velocity, P_arRing, 0.3f);
        b.route (MS_ModWheel, P_arRatio, 0.15f);
        b.macro (0, "Metal", { { P_arRing, 0.4f }, { P_resInharm, 0.6f } });
        b.macro (1, "Ring Time", { { P_resDecay, 0.3f } });
    }

    // 7 ------------------------------------------------------------------------------------------
    inline void freqShiftPad (B& b)
    {
        b.set (P_supersawGain, 0.6f).set (P_supersawSpread, 28).set (P_osc1Wave, 0).set (P_osc1Gain, 0.3f)
         .set (P_ampA, 1.2f).set (P_ampD, 1.0f).set (P_ampS, 0.85f).set (P_ampR, 2.5f)
         .set (P_filterCutoff, 3500).set (P_filterRes, 1.0f)
         .set (P_arShift, 9.0f).set (P_arShiftMix, 0.5f)
         .set (P_lfo3Wave, 0).set (P_lfo3Rate, 0.09f).set (P_lfo3Depth, 1.0f)
         .set (P_chorusMix, 0.4f).set (P_reverbMix, 0.45f).set (P_reverbSize, 3.5f);
        b.route (MS_Lfo3, P_arShift, 0.02f);
        b.route (MS_Lfo3, P_filterCutoff, 0.08f);
        b.macro (0, "Shift", { { P_arShift, 0.08f }, { P_arShiftMix, 0.4f } });
        b.macro (1, "Bright", { { P_filterCutoff, 0.3f } });
    }

    // 8 ------------------------------------------------------------------------------------------
    inline void spectralFreezeChoir (B& b)
    {
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.6f).set (P_osc2Detune, 9)
         .set (P_osc3Wave, 8).set (P_osc3Gain, 0.3f).set (P_osc3Oct, 1)
         .set (P_ampA, 0.9f).set (P_ampS, 0.9f).set (P_ampR, 2.0f)
         .set (P_filterCutoff, 4200)
         .set (P_spOn, 1).set (P_spSize, 2).set (P_spMix, 0.75f).set (P_spBlur, 0.55f).set (P_spFormant, 3.0f).set (P_spTilt, -0.25f)
         .set (P_reverbMix, 0.4f).set (P_chorusMix, 0.3f)
         .set (P_lfo2Wave, 0).set (P_lfo2Rate, 0.15f).set (P_lfo2Depth, 1.0f);
        b.route (MS_Lfo2, P_spFormant, 0.12f);
        b.route (MS_ModWheel, P_spBlur, 0.4f);
        b.macro (0, "Blur", { { P_spBlur, 0.45f } });
        b.macro (1, "Vowel", { { P_spFormant, 0.3f } });
    }

    // 9 ------------------------------------------------------------------------------------------
    inline void granularCloud (B& b)
    {
        b.set (P_osc1Wave, 2).set (P_osc1Gain, 0.6f).set (P_supersawGain, 0.35f)
         .set (P_ampA, 0.6f).set (P_ampS, 0.9f).set (P_ampR, 2.0f)
         .set (P_filterCutoff, 5000)
         .set (P_grMix, 0.65f).set (P_grSize, 180).set (P_grDensity, 35).set (P_grPosition, 0.6f).set (P_grJitter, 0.55f)
         .set (P_grPitchRand, 0.12f).set (P_grSpread, 0.9f).set (P_grReverse, 0.3f).set (P_grFeedback, 0.3f)
         .set (P_reverbMix, 0.4f)
         .set (P_lfo3Wave, 1).set (P_lfo3Rate, 0.07f).set (P_lfo3Depth, 1.0f);
        b.route (MS_Lfo3, P_grPosition, 0.1f);
        b.route (MS_RandSmooth, P_grPitch, 0.02f);
        b.macro (0, "Density", { { P_grDensity, 0.35f } });
        b.macro (1, "Scatter", { { P_grJitter, 0.4f }, { P_grPitchRand, 0.3f } });
    }

    // 10 -----------------------------------------------------------------------------------------
    inline void marimbaCell (B& b)
    {
        b.set (P_masterVolume, 0.18f).set (P_osc1Wave, 2).set (P_osc1Gain, 0.8f)
         .set (P_ampA, 0.001f).set (P_ampD, 0.06f).set (P_ampS, 0.0f).set (P_ampR, 0.06f)
         .set (P_filterCutoff, 6000)
         .set (P_resMix, 0.85f).set (P_resTuning, 2).set (P_resDecay, 0.8f).set (P_resDamping, 0.55f).set (P_resModes, 8)
         .set (P_ciAmount, 0.15f).set (P_ciReso, 0.6f)
         .set (P_reverbMix, 0.25f);
        b.route (MS_Velocity, P_resDamping, -0.25f);
        b.macro (0, "Mallet", { { P_resDamping, -0.4f } });
        b.macro (1, "Wood > Metal", { { P_resInharm, 0.5f } });
    }

    // 11 -----------------------------------------------------------------------------------------
    inline void membraneDrum (B& b)
    {
        b.set (P_masterVolume, 0.18f).set (P_osc1Wave, 3).set (P_osc1Gain, 0.8f).set (P_osc1Oct, -1).set (P_subWave, 1).set (P_subGain, 0.4f)
         .set (P_ampA, 0.001f).set (P_ampD, 0.12f).set (P_ampS, 0.0f).set (P_ampR, 0.1f)
         .set (P_filterCutoff, 4000)
         .set (P_resMix, 0.7f).set (P_resTuning, 3).set (P_resPitch, -12).set (P_resDecay, 0.45f).set (P_resDamping, 0.6f)
         .set (P_wmMix, 0.2f).set (P_wmDrive, 0.5f).set (P_reverbMix, 0.15f);
        b.route (MS_Velocity, P_resDamping, -0.3f);
        b.route (MS_RandNote, P_resPitch, 0.02f);
        b.macro (0, "Tension", { { P_resPitch, 0.2f } });
        b.macro (1, "Ring", { { P_resDecay, 0.3f } });
    }

    // 12 -----------------------------------------------------------------------------------------
    inline void chaosEngine (B& b)
    {
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_complexGain, 0.4f).set (P_complexFm, 500)
         .set (P_filterMode, 6).set (P_filterCutoff, 1800).set (P_filterRes, 5.0f)
         .set (P_ampA, 0.01f).set (P_ampS, 0.8f).set (P_ampR, 0.8f)
         .set (P_wmMix, 0.5f).set (P_wmFold, 0.5f)
         .set (P_ciAmount, 0.6f).set (P_ciRate, 2.0f)
         .set (P_grMix, 0.35f).set (P_grSize, 60).set (P_grDensity, 30)
         .set (P_spOn, 1).set (P_spSize, 1).set (P_spMix, 0.5f).set (P_spScramble, 0.3f)
         .set (P_fbGrSp, 0.3f).set (P_fbSpGr, 0.25f).set (P_fbSafety, 0.7f)
         .set (P_mutAmount, 0.25f).set (P_mutSeed, 777).set (P_randRate, 6.0f);
        b.route (MS_Chaos, P_wmFold, 0.35f);
        b.route (MS_SampleHold, P_filterCutoff, 0.3f);
        b.route (MS_RandStep, P_spScramble, 0.3f);
        b.macro (0, "Entropy", { { P_mutAmount, 0.6f }, { P_ciAmount, 0.4f } });
        b.macro (1, "Feedback", { { P_fbGrSp, 0.4f }, { P_fbSpGr, 0.4f } });
    }

    // 13 -----------------------------------------------------------------------------------------
    inline void breedingPad (B& b)
    {
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.55f).set (P_osc2Detune, 11)
         .set (P_subGain, 0.2f)
         .set (P_ampA, 0.8f).set (P_ampS, 0.85f).set (P_ampR, 2.2f)
         .set (P_filterCutoff, 3000).set (P_filterRes, 2.0f)
         .set (P_chorusMix, 0.35f).set (P_reverbMix, 0.4f);
        // The four scenes are four relatives of the same sound; two slow LFOs wander between them.
        b.storeScene (0, {});
        b.storeScene (1, { { P_wmMix, 0.6f }, { P_wmFold, 0.65f }, { P_filterCutoff, 2200 } });
        b.storeScene (2, { { P_dnaMix, 0.8f }, { P_dnaMode, 6 }, { P_dnaA, 0 }, { P_dnaB, 1 }, { P_filterCutoff, 4200 } });
        b.storeScene (3, { { P_resMix, 0.5f }, { P_resTuning, 4 }, { P_resDecay, 2.5f }, { P_filterCutoff, 5200 } });
        b.set (P_sceneMorph, 1).set (P_sceneX, 0.5f).set (P_sceneY, 0.5f)
         .set (P_lfo3Wave, 0).set (P_lfo3Rate, 0.07f).set (P_lfo3Depth, 1.0f)
         .set (P_lfo4Wave, 1).set (P_lfo4Rate, 0.045f).set (P_lfo4Depth, 1.0f);
        b.route (MS_Lfo3, P_sceneX, 0.5f);
        b.route (MS_Lfo4, P_sceneY, 0.5f);
        b.macro (0, "Wander", { { P_lfo3Rate, 0.3f }, { P_lfo4Rate, 0.3f } });
    }

    // 14 -----------------------------------------------------------------------------------------
    inline void sequencedDna (B& b)
    {
        monoBass (b, 0.0f);
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.75f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.45f).set (P_osc2Oct, -1)
         .set (P_subWave, 1).set (P_subGain, 0.4f)
         .set (P_filterMode, 2).set (P_filterCutoff, 1600).set (P_filterRes, 4.0f)
         .set (P_dsOn, 1).set (P_dsSteps, 16).set (P_dsRate, 2).set (P_dsGlide, 0.1f).set (P_dsDepth, 0.9f)
         .set (P_arMod, 0).set (P_arRatio, 1.5f).set (P_dnaA, 0).set (P_dnaB, 1).set (P_dnaMode, 1)
         .set (P_resTuning, 0);
        b.dna ({ { DT_Fold, 0.8f }, { DT_Off, 0.7f }, { DT_Crush, 0.6f }, { DT_Ring, 0.5f },
                 { DT_Filter, 0.7f }, { DT_Off, 0.7f }, { DT_Shift, 0.4f }, { DT_Splice, 0.8f },
                 { DT_Fold, 0.5f }, { DT_Decimate, 0.5f }, { DT_Off, 0.7f }, { DT_Fm, 0.6f },
                 { DT_Resonate, 0.6f }, { DT_Off, 0.7f }, { DT_Octave, 1.0f }, { DT_Filter, 0.9f } });
        b.route (MS_DnaSeq, P_filterCutoff, 0.15f);
        b.macro (0, "Sequence Depth", { { P_dsDepth, -0.6f } });
        b.macro (1, "Glide", { { P_dsGlide, 0.6f } });
    }

    // 15 -----------------------------------------------------------------------------------------
    inline void feedbackOrganism (B& b)
    {
        b.set (P_osc1Wave, 4).set (P_osc1Gain, 0.7f).set (P_osc3Wave, 3).set (P_osc3Gain, 0.3f).set (P_osc3Oct, 1)
         .set (P_ampA, 0.002f).set (P_ampD, 0.4f).set (P_ampS, 0.2f).set (P_ampR, 0.6f)
         .set (P_filterCutoff, 3800).set (P_filterRes, 3.0f)
         .set (P_grMix, 0.4f).set (P_grSize, 120).set (P_grDensity, 25)
         .set (P_spOn, 1).set (P_spSize, 1).set (P_spMix, 0.5f).set (P_spShift, 30)
         .set (P_delayMix, 0.3f).set (P_delayFeedback, 0.45f).set (P_delaySync, 6)
         .set (P_fbDlGr, 0.4f).set (P_fbGrSp, 0.35f).set (P_fbSpDl, 0.3f).set (P_fbOutGr, 0.2f)
         .set (P_fbTime, 220).set (P_fbTone, 4000).set (P_fbSafety, 0.6f)
         .set (P_lfo2Wave, 0).set (P_lfo2Rate, 0.2f).set (P_lfo2Depth, 1.0f);
        b.route (MS_Lfo2, P_fbTime, 0.1f);
        b.route (MS_EnvFollow, P_spShift, 0.03f);
        b.macro (0, "Growth", { { P_fbDlGr, 0.4f }, { P_fbSpDl, 0.4f }, { P_fbOutGr, 0.3f } });
        b.macro (1, "Tone", { { P_fbTone, 0.3f } });
    }

    // 16 -----------------------------------------------------------------------------------------
    inline void jungleStab (B& b)
    {
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.45f).set (P_osc2Semi, 3)
         .set (P_osc3Wave, 0).set (P_osc3Gain, 0.45f).set (P_osc3Semi, 7).set (P_osc3Oct, 0)
         .set (P_ampA, 0.001f).set (P_ampD, 0.35f).set (P_ampS, 0.0f).set (P_ampR, 0.3f)
         .set (P_filterMode, 9).set (P_filterCutoff, 1500).set (P_filterRes, 4.0f)
         .set (P_fEnvAmt, 4000).set (P_fEnvA, 0.001f).set (P_fEnvD, 0.15f).set (P_fEnvS, 0.0f)
         .set (P_resMix, 0.15f).set (P_resTuning, 4).set (P_resDecay, 0.7f)
         .set (P_reverbMix, 0.35f).set (P_delayMix, 0.25f).set (P_delaySync, 6).set (P_chorusMix, 0.2f);
        b.route (MS_Velocity, P_filterCutoff, 0.15f);
        b.macro (0, "Mutate", { { P_mutAmount, 0.7f } });
        b.macro (1, "Rave", { { P_wmMix, 0.5f }, { P_wmBits, -0.4f }, { P_wmDown, 0.25f } });
    }

    // 17 -----------------------------------------------------------------------------------------
    inline void mutantLead (B& b)
    {
        b.set (P_polyphony, 1).set (P_porta, 0.05f)
         .set (P_supersawGain, 0.55f).set (P_supersawVoices, 7).set (P_supersawSpread, 18).set (P_osc1Wave, 4).set (P_osc1Gain, 0.4f)
         .set (P_ampA, 0.005f).set (P_ampS, 0.9f).set (P_ampR, 0.3f)
         .set (P_filterCutoff, 4500).set (P_filterRes, 2.0f)
         .set (P_wmMix, 0.3f).set (P_wmAsym, 0.4f).set (P_wmDrive, 0.4f)
         .set (P_arMod, 0).set (P_arRatio, 2.0f).set (P_arFm, 0.05f)
         .set (P_lfoAssignAmt0, 20.0f / 1200.0f).set (P_lfo1Rate, 5.5f)
         .set (P_mutAmount, 0.18f).set (P_mutSeed, 4242)
         .set (P_delayMix, 0.25f).set (P_delaySync, 6).set (P_reverbMix, 0.3f);
        b.route (MS_ModWheel, P_arFm, 0.35f);
        b.route (MS_Aftertouch, P_wmFold, 0.4f);
        b.macro (0, "Mutate", { { P_mutAmount, 0.6f } });
        b.macro (1, "Edge", { { P_wmMix, 0.4f }, { P_filterCutoff, 0.2f } });
    }

    // 18 -----------------------------------------------------------------------------------------
    inline void fmHelixBass (B& b)
    {
        monoBass (b);
        b.set (P_complexGain, 0.8f).set (P_complexWaveA, 3).set (P_complexWaveB, 0).set (P_complexRatio, 1.0f)
         .set (P_complexFm, 350).set (P_complexShape, 3.0f).set (P_complexMix, 0.8f).set (P_complexOct, -1)
         .set (P_subWave, 1).set (P_subGain, 0.5f)
         .set (P_filterCutoff, 2500).set (P_filterRes, 1.0f)
         .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.15f).set (P_mEnv1S, 0.1f);
        b.route (MS_ModEnv1, P_complexFm, 0.3f);
        b.route (MS_Velocity, P_complexFm, 0.1f);
        b.macro (0, "FM", { { P_complexFm, 0.4f } });
        b.macro (1, "Fold", { { P_complexShape, 0.3f } });
    }

    //==============================================================================================
    // Jungle / DnB bank: built around Filter 2, the DNA Sequencer update and the effects rack

    // 19 -----------------------------------------------------------------------------------------
    inline void amenReese (B& b)
    {
        monoBass (b, 0.03f);
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.62f).set (P_osc1Detune, -18)
         .set (P_osc2Wave, 0).set (P_osc2Gain, 0.62f).set (P_osc2Detune, 18)
         .set (P_subWave, 1).set (P_subGain, 0.5f).set (P_subOct, -1)
         .set (P_filterMode, 2).set (P_filterCutoff, 750).set (P_filterRes, 2.5f).set (P_filterDrive, 4.0f)
         .set (P_fEnvAmt, 900).set (P_fEnvD, 0.35f).set (P_fEnvS, 0.3f)
         // Filter 2: a slowly sweeping notch after the ladder (serial) for the phasey reese movement
         .set (P_filter2On, 1).set (P_filterRouting, 0).set (P_filter2Mode, 4).set (P_filter2Type, FT_NOTCH).set (P_filter2Slope, 1)
         .set (P_filter2Cutoff, 520).set (P_filter2Res, 2.0f).set (P_filter2LfoAmt, 0.45f).set (P_filter2LfoSrc, 1)
         .set (P_lfo2Wave, 0).set (P_lfo2Rate, 0.12f).set (P_lfo2Depth, 1.0f)
         .set (P_wmMix, 0.25f).set (P_wmDrive, 0.4f).set (P_ciAmount, 0.15f).set (P_ciRate, 0.3f)
         .set (P_fxOnMultiband, 1).set (P_fxMixMultiband, 0.6f).set (P_mbcDepth, 0.4f)
         .set (P_fxOnStereo, 1).set (P_sttMono, 1).set (P_sttMonoFreq, 140).set (P_sttWidth, 1.3f);
        b.macro (0, "Growl", { { P_wmMix, 0.5f }, { P_wmFold, 0.4f } });
        b.macro (1, "Notch", { { P_filter2Cutoff, 0.35f } });
        b.macro (2, "OTT", { { P_mbcDepth, 0.5f } });
        b.macro (3, "Open", { { P_filterCutoff, 0.3f } });
    }

    // 20 -----------------------------------------------------------------------------------------
    inline void neuroRollers (B& b)
    {
        monoBass (b);
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.65f)
         .set (P_osc2Wave, 1).set (P_osc2Gain, 0.5f).set (P_osc2Oct, -1)
         .set (P_subWave, 1).set (P_subGain, 0.4f)
         .set (P_filterMode, 0).set (P_filterType, FT_BP).set (P_filterSlope, 1).set (P_filterCutoff, 900).set (P_filterRes, 5.0f).set (P_filterDrive, 3.0f)
         // Filter 2 in parallel: a low pass that the second DNA lane moves on its own 12-step loop
         .set (P_filter2On, 1).set (P_filterRouting, 1).set (P_filter2Mode, 0).set (P_filter2Type, FT_LP).set (P_filter2Slope, 3)
         .set (P_filter2Cutoff, 400).set (P_filter2Res, 3.0f).set (P_filterBalance, 0.5f)
         .set (P_wmMix, 0.4f).set (P_wmFold, 0.35f)
         .set (P_dsOn, 1).set (P_dsSteps, 16).set (P_dsRate, 2).set (P_dsSwing, 0.12f).set (P_dsGlide, 0.15f).set (P_dsDepth, 0.9f)
         .set (P_dsLane2On, 1).set (P_dsLane2Steps, 12).set (P_dsChainOn, 1)
         .set (P_fxOnMultiband, 1).set (P_mbcDepth, 0.55f)
         .set (P_fxOnStereo, 1).set (P_sttMono, 1);
        b.dnaLane (0, 0, "F.f.F.cFf.F.p.Fr", { 0.8f, 0.6f, 0.9f, 0.5f });
        b.dnaLane (0, 1, "f..s..f..m..", { 0.6f, 0.4f });
        b.dnaLane (1, 0, "FFf.c.FFrFf.ppFF", { 0.9f, 0.5f, 0.7f });
        b.dnaLane (1, 1, "s.f.s.f.mmf.", { 0.5f, 0.7f });
        b.dnaStep (0, 0, 14, DT_Filter, 0.9f, 1.0f, 3);   // a ratchet into the bar line
        b.dnaStep (0, 0, 10, DT_Filter, 0.8f, 0.6f);      // sometimes
        b.dnaStep (1, 0, 15, DT_Filter, 1.0f, 1.0f, 4);
        b.dnaChain = "AAAB";
        b.route (MS_DnaSeq, P_filterCutoff, 0.3f);
        b.route (MS_DnaSeq2, P_filter2Cutoff, 0.35f);
        b.route (MS_DnaGate, P_wmFold, 0.2f);
        b.macro (0, "Growl", { { P_wmFold, 0.45f }, { P_wmMix, 0.3f } });
        b.macro (1, "Balance", { { P_filterBalance, 0.5f } });
        b.macro (2, "OTT", { { P_mbcDepth, 0.45f } });
    }

    // 21 -----------------------------------------------------------------------------------------
    inline void hoover93 (B& b)
    {
        b.set (P_polyphony, 1).set (P_porta, 0.07f)
         .set (P_supersawGain, 0.7f).set (P_supersawVoices, 9).set (P_supersawSpread, 35)
         .set (P_osc1Wave, 4).set (P_osc1Gain, 0.5f).set (P_osc1Oct, -1)
         .set (P_ampA, 0.01f).set (P_ampD, 0.3f).set (P_ampS, 0.9f).set (P_ampR, 0.25f)
         .set (P_filterMode, 0).set (P_filterCutoff, 3800).set (P_filterRes, 1.5f).set (P_filterDrive, 2.0f)
         .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.25f).set (P_mEnv1S, 0.0f)
         .set (P_fxOnFlanger, 1).set (P_fxMixFlanger, 0.55f).set (P_flpMode, 0).set (P_flpRate, 0.15f)
         .set (P_flpDepth, 0.8f).set (P_flpFeedback, 0.55f).set (P_flpSpread, 0.7f)
         .set (P_reverbMix, 0.25f).set (P_delayMix, 0.2f).set (P_delaySync, 6).set (P_chorusMix, 0.0f);
        b.route (MS_ModEnv1, P_supersawSemi, -0.15f);   // the scoop up into each note
        b.route (MS_ModEnv1, P_osc1Semi, -0.15f);
        b.route (MS_ModWheel, P_flpDepth, 0.2f);
        b.macro (0, "Scoop", { { P_mEnv1D, 0.2f } });
        b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
    }

    // 22 -----------------------------------------------------------------------------------------
    inline void raggaSirenStab (B& b)
    {
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.38f).set (P_osc2Semi, 3)
         .set (P_osc3Wave, 0).set (P_osc3Gain, 0.38f).set (P_osc3Semi, 10)
         .set (P_ampA, 0.001f).set (P_ampD, 0.4f).set (P_ampS, 0.0f).set (P_ampR, 0.3f)
         .set (P_filterMode, 9).set (P_filterCutoff, 1800).set (P_filterRes, 3.0f)
         .set (P_fEnvAmt, 3500).set (P_fEnvA, 0.001f).set (P_fEnvD, 0.18f).set (P_fEnvS, 0.0f)
         // S950 crunch first, then Beat Repeat (hold E0 to stutter the stab)
         .set (P_fxOnSampler, 1).set (P_smpModel, 1).set (P_smpBits, 12).set (P_smpRate, 22050).set (P_smpDrive, 0.35f).set (P_smpNoise, 0.15f)
         .set (P_fxOnStutter, 1).set (P_rptMidi, 3).set (P_rptLength, 2).set (P_rptShrink, 0.35f).set (P_rptPitch, 3).set (P_rptDuration, 1)
         .set (P_delayMix, 0.3f).set (P_delaySync, 6).set (P_reverbMix, 0.25f).set (P_chorusMix, 0.15f);
        b.rackOrder ({ FS_Sampler, FS_Stutter, FS_Flanger, FS_Delay, FS_Chorus, FS_Reverb, FS_Shaper, FS_Multiband, FS_Stereo });
        b.route (MS_Velocity, P_filterCutoff, 0.12f);
        b.macro (0, "Crunch", { { P_smpDrive, 0.5f }, { P_smpNoise, 0.2f } });
        b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
    }

    // 23 -----------------------------------------------------------------------------------------
    inline void subPressure (B& b)
    {
        monoBass (b);
        b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.85f).set (P_osc1Oct, -1)
         .set (P_osc2Wave, 2).set (P_osc2Gain, 0.22f)
         .set (P_filterMode, 0).set (P_filterSlope, 0).set (P_filterCutoff, 600).set (P_filterRes, 0.7f).set (P_filterKeyTrack, 0.8f)
         .set (P_fEnvAmt, 400).set (P_fEnvD, 0.15f).set (P_fEnvS, 0.0f)
         .set (P_fxOnMultiband, 1).set (P_mbcDepth, 0.3f)
         .set (P_fxOnStereo, 1).set (P_sttMono, 1).set (P_sttMonoFreq, 150).set (P_warmth, 0.5f);
        b.route (MS_Velocity, P_filterCutoff, 0.1f);
        b.macro (0, "Drive", { { P_wmMix, 0.4f }, { P_wmDrive, 0.5f } });
        b.macro (1, "Harmonics", { { P_filterCutoff, 0.3f } });
    }

    // 24 -----------------------------------------------------------------------------------------
    inline void liquidPad (B& b)
    {
        b.set (P_supersawGain, 0.45f).set (P_supersawVoices, 7).set (P_supersawSpread, 25)
         .set (P_osc1Wave, 2).set (P_osc1Gain, 0.22f).set (P_osc1Oct, 1)
         .set (P_ampA, 0.6f).set (P_ampD, 1.0f).set (P_ampS, 0.8f).set (P_ampR, 1.8f)
         .set (P_filterMode, 0).set (P_filterSlope, 2).set (P_filterCutoff, 2400).set (P_filterRes, 1.2f)
         .set (P_filterLfoAmt, 0.2f).set (P_filterLfoSrc, 1).set (P_lfo2Rate, 0.11f).set (P_lfo2Depth, 1.0f)
         // Filter 1 on the left, a drifting band pass on the right
         .set (P_filter2On, 1).set (P_filterRouting, 1).set (P_filterStereoSplit, 1)
         .set (P_filter2Type, FT_BP).set (P_filter2Slope, 1).set (P_filter2Cutoff, 1200).set (P_filter2Res, 2.5f)
         .set (P_filter2LfoAmt, 0.35f).set (P_filter2LfoSrc, 2).set (P_lfo3Rate, 0.08f).set (P_lfo3Depth, 1.0f)
         .set (P_fxOnFlanger, 1).set (P_flpMode, 1).set (P_flpStages, 1).set (P_flpRate, 0.1f).set (P_flpDepth, 0.6f)
         .set (P_flpFeedback, 0.4f).set (P_fxMixFlanger, 0.5f)
         .set (P_fxOnShaper, 1).set (P_vshRate, 0).set (P_vshDepth, 0.45f).set (P_vshSmooth, 0.5f)
         .set (P_reverbMix, 0.45f).set (P_reverbSize, 3.5f).set (P_chorusMix, 0.35f).set (P_delayMix, 0.15f);
        b.macro (0, "Pump", { { P_vshDepth, 0.5f } });
        b.macro (1, "Bright", { { P_filterCutoff, 0.3f }, { P_filter2Cutoff, 0.3f } });
    }

    // 25 -----------------------------------------------------------------------------------------
    inline void dreadWobble (B& b)
    {
        monoBass (b, 0.02f);
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f)
         .set (P_osc2Wave, 1).set (P_osc2Gain, 0.5f).set (P_osc2Oct, -1)
         .set (P_subWave, 1).set (P_subGain, 0.45f)
         .set (P_filterMode, 0).set (P_filterSlope, 3).set (P_filterCutoff, 220).set (P_filterRes, 5.5f).set (P_filterDrive, 3.0f)
         // the DNA Sequencer as a tempo-synced wobble: its value moves the cutoff, a little fold rides along
         .set (P_dsOn, 1).set (P_dsRate, 3).set (P_dsSteps, 6).set (P_dsGlide, 0.6f).set (P_dsDepth, 0.2f)
         .set (P_fxOnShaper, 1).set (P_vshDepth, 0.35f).set (P_vshSmooth, 0.4f)
         .set (P_fxOnMultiband, 1).set (P_mbcDepth, 0.35f)
         .set (P_fxOnStereo, 1).set (P_sttMono, 1);
        b.dnaLane (0, 0, "ffffff", { 0.2f, 1.0f, 0.5f, 1.0f, 0.3f, 0.8f });
        b.dnaLane (1, 0, "ffffff", { 1.0f, 0.2f, 1.0f, 0.2f, 0.6f, 0.6f });
        b.shaperCurve = 3;   // triplet pump
        b.route (MS_DnaSeq, P_filterCutoff, 0.4f);
        b.macro (0, "Wobble", { { P_dsDepth, 0.4f } });
        b.macro (1, "Res", { { P_filterRes, 0.3f } });
    }

    // 26 -----------------------------------------------------------------------------------------
    inline void pirateRadioLead (B& b)
    {
        b.set (P_polyphony, 1).set (P_porta, 0.04f)
         .set (P_osc1Wave, 1).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.4f).set (P_osc2Detune, 8)
         .set (P_ampA, 0.004f).set (P_ampS, 0.85f).set (P_ampR, 0.2f)
         .set (P_filterMode, 11).set (P_filterCutoff, 2600).set (P_filterRes, 2.0f)
         .set (P_fxOnSampler, 1).set (P_smpModel, 0).set (P_smpDrive, 0.3f)
         .set (P_fxOnFlanger, 1).set (P_flpMode, 1).set (P_flpStages, 0).set (P_flpRate, 0.3f).set (P_fxMixFlanger, 0.4f)
         .set (P_delayMix, 0.3f).set (P_delaySync, 6).set (P_reverbMix, 0.2f).set (P_chorusMix, 0.0f);
        b.route (MS_ModWheel, P_flpDepth, 0.3f);
        b.macro (0, "Grit", { { P_smpDrive, 0.5f }, { P_wmMix, 0.3f } });
        b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
    }

    // 27 -----------------------------------------------------------------------------------------
    inline void stutterPluck (B& b)
    {
        b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 4).set (P_osc2Gain, 0.35f).set (P_osc2Oct, 1)
         .set (P_ampA, 0.001f).set (P_ampD, 0.25f).set (P_ampS, 0.0f).set (P_ampR, 0.2f)
         .set (P_filterMode, 0).set (P_filterCutoff, 1200).set (P_filterRes, 3.0f)
         .set (P_fEnvAmt, 5000).set (P_fEnvA, 0.001f).set (P_fEnvD, 0.12f).set (P_fEnvS, 0.0f)
         .set (P_fxOnStutter, 1).set (P_rptChance, 0.35f).set (P_rptLength, 2).set (P_rptDuration, 1)
         .set (P_rptShrink, 0.5f).set (P_rptPitch, 5).set (P_rptGate, 0.7f)
         .set (P_fxOnFlanger, 1).set (P_flpTZ, 1).set (P_flpRate, 0.25f).set (P_fxMixFlanger, 0.35f)
         .set (P_delayMix, 0.35f).set (P_delaySync, 7).set (P_reverbMix, 0.3f);
        b.macro (0, "Chaos", { { P_rptChance, 0.5f } });
        b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
    }

    // 28 -----------------------------------------------------------------------------------------
    inline void jungleAtmos (B& b)
    {
        b.set (P_osc1Wave, 2).set (P_osc1Gain, 0.4f).set (P_osc3Wave, 3).set (P_osc3Gain, 0.3f).set (P_osc3Oct, 1)
         .set (P_supersawGain, 0.25f)
         .set (P_ampA, 1.2f).set (P_ampD, 1.0f).set (P_ampS, 0.9f).set (P_ampR, 2.5f)
         .set (P_filterCutoff, 3000).set (P_filterRes, 1.0f)
         .set (P_grMix, 0.35f).set (P_grSize, 150).set (P_grDensity, 20)
         .set (P_fxOnSampler, 1).set (P_smpModel, 2).set (P_smpDrive, 0.1f).set (P_smpNoise, 0.2f).set (P_fxMixSampler, 0.7f)
         .set (P_reverbMix, 0.55f).set (P_reverbSize, 4.0f).set (P_chorusMix, 0.4f).set (P_delayMix, 0.2f)
         .set (P_fxOnStereo, 1).set (P_sttMono, 1).set (P_sttWidth, 1.5f).set (P_sttHaas, 8);
        // the E-mu samples the whole reverb wash, like resampling a pad from a hardware sampler
        b.rackOrder ({ FS_Stutter, FS_Flanger, FS_Delay, FS_Chorus, FS_Reverb, FS_Sampler, FS_Shaper, FS_Multiband, FS_Stereo });
        b.route (MS_ModWheel, P_grMix, 0.4f);
        b.macro (0, "Grain", { { P_grMix, 0.5f } });
        b.macro (1, "Space", { { P_reverbMix, 0.4f } });
    }
}

inline const std::array<FactoryPreset, 28>& factoryPresets()
{
    using namespace factory_detail;
    static const std::array<FactoryPreset, 28> list { {
        { "Init Genome",           "Init",   "Two saws and a sub, with the eight macros wired to the main mutation engines.", initGenome },
        { "Reese Mutation",        "Bass",   "Detuned reese with slow wave-fold and filter drift plus Cell Instability.", reeseMutation },
        { "Sub Helix",             "Bass",   "Sine sub with an audio-rate FM blip at the front of each note.", subHelix },
        { "Neuro Splice",          "Bass",   "Spectral DNA splice of saw and square, wobbled by LFO 1.", neuroSplice },
        { "Folded Acid",           "Bass",   "TB-303 filter acid line with an envelope-driven wave folder.", foldedAcid },
        { "Ring Mutant Bell",      "Keys",   "Inharmonic audio-rate ring modulation into a bell resonator.", ringMutantBell },
        { "Frequency Shift Pad",   "Pad",    "Supersaw pad slowly drifting through a frequency shifter.", freqShiftPad },
        { "Spectral Freeze Choir", "Pad",    "Blurred, formant-shifted spectral choir; mod wheel adds blur.", spectralFreezeChoir },
        { "Granular Cloud",        "Pad",    "Dense reversing grain cloud with wandering position.", granularCloud },
        { "Marimba Cell",          "Perc",   "A short click exciting the bar resonator.", marimbaCell },
        { "Membrane Drum",         "Perc",   "Tuned membrane resonator; velocity opens the head.", membraneDrum },
        { "Chaos Engine",          "FX",     "Chaos, sample & hold and a granular/spectral feedback loop, with fixed mutation.", chaosEngine },
        { "Breeding Pad",          "Pad",    "Four related scenes (clean, folded, spliced, resonated) morphed by two slow LFOs.", breedingPad },
        { "Sequenced DNA",         "Bass",   "16-step DNA Sequencer cycling fold, crush, ring, splice and more (1/16 synced).", sequencedDna },
        { "Feedback Organism",     "FX",     "Delay, granular and spectral stages feeding each other through the feedback matrix.", feedbackOrganism },
        { "Jungle Stab",           "Keys",   "Minor chord stab with MS-20 filter snap, dotted delay and a touch of plate.", jungleStab },
        { "Mutant Lead",           "Lead",   "Mono supersaw lead with a fixed mutation seed; mod wheel adds FM, aftertouch folds.", mutantLead },
        { "FM Helix Bass",         "Bass",   "Complex-oscillator FM bass with an envelope on the index.", fmHelixBass },
        { "Amen Reese",            "Jungle / DnB", "Bass: detuned reese through a ladder, then a slow notch sweep (Filter 2 serial), OTT and bass mono.", amenReese },
        { "Neuro Rollers",         "Jungle / DnB", "Bass: two DNA lanes (16 and 12 steps) with ratchets, swing and an AAAB chain move parallel band and low pass filters.", neuroRollers },
        { "Hoover 93",             "Jungle / DnB", "Lead: 9-voice hoover with a pitch scoop into each note and a slow flanger; mod wheel deepens it.", hoover93 },
        { "Ragga Siren Stab",      "Jungle / DnB", "Stab: minor 7 stab through an S950, then Beat Repeat (hold E0 to stutter).", raggaSirenStab },
        { "Sub Pressure",          "Jungle / DnB", "Bass: clean sine sub, 6 dB filter with key tracking, gentle OTT, mono below 150 Hz.", subPressure },
        { "Liquid Pad",            "Jungle / DnB", "Pad: supersaw with Filter 1 left and a drifting band pass right, phaser and a quarter-note pump.", liquidPad },
        { "Dread Wobble",          "Jungle / DnB", "Bass: tempo-synced wobble from the DNA Sequencer (1/8 triplets) plus a triplet volume pump.", dreadWobble },
        { "Pirate Radio Lead",     "Jungle / DnB", "Lead: square lead crunched by the SP-1200 with a 4-stage phaser; mod wheel adds depth.", pirateRadioLead },
        { "Stutter Pluck",         "Jungle / DnB", "Pluck: random Beat Repeats (35% chance per beat), through-zero flanger and triplet delay.", stutterPluck },
        { "Jungle Atmos",          "Jungle / DnB", "Pad: soft grains and a big reverb, then resampled by the E-mu at the end of the rack.", jungleAtmos },
    } };
    return list;
}


} // namespace tg
