#pragma once
// The jungle / drum & bass bank: 100 factory presets in the browser's categories (15 Reese, 12 Sub & Bass,
// 12 Neuro, 8 Hoover & Rave, 10 Stabs & Chords, 10 Pads & Atmos, 8 Leads, 10 Breaks & Drums, 8 FX & Risers,
// 7 Keys & Plucks). Each is written against the parameter table like the first 36, on the same clean start.
// The basses keep everything below 120 Hz in the middle (Stereo Tools' Bass Mono); every preset has named
// macros; the levels are matched afterwards (see FactoryDnbTrims.h).
// (included by Factory.h, after the PresetBuilder and the first 36 presets' helpers)

namespace tg
{
namespace dnb
{
using B = PresetBuilder;

// ---- shared starting points ---------------------------------------------------------------------------
inline void bass (B& b, float glide = 0.0f)
{
    factory_detail::monoBass (b, glide);
    b.set (P_fxOnStereo, 1).set (P_sttMono, 1).set (P_sttMonoFreq, 120).set (P_filterCutoff, 900).set (P_fEnvAmt, 0);
}
inline void ott (B& b, float depth) { b.set (P_fxOnMultiband, 1).set (P_mbcDepth, depth); }
inline void dryFx (B& b) { b.set (P_reverbMix, 0.0f).set (P_delayMix, 0.0f).set (P_chorusMix, 0.0f); }
inline void space (B& b, float reverb, float delay, int sync = 6, float chorus = 0.0f, float size = 2.4f)
{
    b.set (P_reverbMix, reverb).set (P_reverbSize, size).set (P_delayMix, delay).set (P_delaySync, (float) sync).set (P_chorusMix, chorus);
}
// the Reese engine: voices (2-4), wave (0 saw, 1 square, 2 blend), detune mode (0 cents, 1 Hz, 2 beats)
inline void reese (B& b, int voices, int wave, int mode, int beat, float cents, float twist, float notch, float sub, float drive, float width = 0.6f)
{
    b.set (P_rsLevel, 0.75f).set (P_rsVoices, (float) (voices - 2)).set (P_rsWave, (float) wave).set (P_rsDetMode, (float) mode)
     .set (P_rsBeat, (float) beat).set (P_rsCents, cents).set (P_rsTwist, twist).set (P_rsNotch, notch).set (P_rsSub, sub)
     .set (P_rsDrive, drive).set (P_rsWidth, width).set (P_rsMonoBelow, 120).set (P_rsRetrig, 1);
}
inline void reeseMacros (B& b)
{
    b.macro (0, "Twist", { { P_rsTwist, 0.5f } });
    b.macro (1, "Notch", { { P_rsNotch, 0.6f }, { P_rsNotchMove, 0.4f } });
    b.macro (2, "Drive", { { P_rsDrive, 0.5f } });
    b.macro (3, "Open", { { P_filterCutoff, 0.3f } });
}
inline void pluckEnv (B& b, float decay, float fenv)
{
    b.set (P_ampA, 0.001f).set (P_ampD, decay).set (P_ampS, 0.0f).set (P_ampR, decay * 0.8f)
     .set (P_fEnvAmt, fenv).set (P_fEnvA, 0.001f).set (P_fEnvD, decay * 0.6f).set (P_fEnvS, 0.0f);
}
inline void padEnv (B& b, float attack, float release)
{
    b.set (P_ampA, attack).set (P_ampD, 1.0f).set (P_ampS, 0.85f).set (P_ampR, release).set (P_fEnvAmt, 0);
}
inline void stabChord (B& b, int chord, int filterModel, float cutoff, float snap)
{
    b.set (P_chdOn, 1).set (P_chdPreset, (float) chord).set (P_polyphony, 12)
     .set (P_filterMode, (float) filterModel).set (P_filterCutoff, cutoff).set (P_filterRes, 3.0f);
    pluckEnv (b, 0.4f, snap);
    b.route (MS_Velocity, P_filterCutoff, 0.12f);
}
inline void breakKit (B& b, int demo)
{
    factory_detail::breakBase (b);
    b.breakDemo = demo;
    b.set (P_bcBars, 2);
}

// ======================================================================================================
// Reese (15)
inline void rs01 (B& b)   // Amen Brother Reese
{
    bass (b); reese (b, 2, 0, 2, 3, 14, 0.2f, 0.4f, 0.5f, 0.2f);
    b.set (P_filterMode, 2).set (P_filterCutoff, 900).set (P_filterRes, 2.0f).set (P_fEnvAmt, 600).set (P_fEnvD, 0.4f).set (P_fEnvS, 0.5f);
    ott (b, 0.4f); reeseMacros (b);
}
inline void rs02 (B& b)   // Rolling Quarter Reese
{
    bass (b); reese (b, 3, 0, 2, 4, 14, 0.15f, 0.3f, 0.45f, 0.25f, 0.7f);
    b.set (P_filterMode, 0).set (P_filterSlope, 3).set (P_filterCutoff, 1400).set (P_filterRes, 1.5f);
    ott (b, 0.35f); reeseMacros (b);
}
inline void rs03 (B& b)   // Dark Matter Reese
{
    bass (b); reese (b, 4, 0, 0, 3, 20, 0.25f, 0.2f, 0.55f, 0.5f);
    b.set (P_rsRatio, 2).set (P_rsTone, 0.5f).set (P_filterMode, 13).set (P_filterCutoff, 600).set (P_filterRes, 2.5f)
     .set (P_wmMix, 0.3f).set (P_wmFold, 0.2f);
    ott (b, 0.45f); reeseMacros (b);
}
inline void rs04 (B& b)   // Tech Reese Growl
{
    bass (b); reese (b, 2, 0, 2, 5, 14, 0.6f, 0.5f, 0.4f, 0.6f);
    b.set (P_rsTwistRate, 18).set (P_rsNotchFreq, 1200).set (P_rsNotchMove, 0.7f)
     .set (P_filterMode, 10).set (P_filterCutoff, 1600).set (P_filterRes, 3.0f);
    ott (b, 0.55f); reeseMacros (b);
    b.macro (4, "Growl Rate", { { P_rsTwistRate, 0.3f } });
}
inline void rs05 (B& b)   // Liquid Reese
{
    bass (b); reese (b, 2, 0, 1, 3, 14, 0.12f, 0.25f, 0.5f, 0.0f, 0.5f);
    b.set (P_rsHz, 1.2f).set (P_filterMode, 1).set (P_filterCutoff, 1800).set (P_filterRes, 1.0f).set (P_reverbMix, 0.08f).set (P_reverbSize, 1.8f);
    reeseMacros (b);
}
inline void rs06 (B& b)   // Steppa Square Reese
{
    bass (b); reese (b, 2, 1, 2, 2, 14, 0.1f, 0.2f, 0.6f, 0.15f);
    b.set (P_filterMode, 11).set (P_filterCutoff, 700).set (P_filterRes, 2.0f).set (P_fEnvAmt, 500).set (P_fEnvD, 0.3f).set (P_fEnvS, 0.4f);
    ott (b, 0.3f); reeseMacros (b);
}
inline void rs07 (B& b)   // Blend Reese 174
{
    bass (b); reese (b, 4, 2, 2, 7, 14, 0.2f, 0.6f, 0.4f, 0.3f);
    b.set (P_rsRatio, 1).set (P_rsNotchSync, 1).set (P_filterMode, 2).set (P_filterCutoff, 1100).set (P_filterRes, 2.0f);
    ott (b, 0.4f); reeseMacros (b);
}
inline void rs08 (B& b)   // Reese Fold Stack
{
    bass (b); reese (b, 2, 0, 2, 6, 14, 0.3f, 0.3f, 0.3f, 0.35f);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.35f).set (P_osc1Oct, -1)
     .set (P_wmMix, 0.5f).set (P_wmFold, 0.45f).set (P_wmDrive, 0.4f)
     .set (P_filterMode, 0).set (P_filterCutoff, 1300).set (P_filterRes, 2.0f);
    ott (b, 0.5f); reeseMacros (b);
    b.macro (4, "Fold", { { P_wmFold, 0.4f } });
}
inline void rs09 (B& b)   // Ghost Reese
{
    bass (b); reese (b, 2, 0, 0, 3, 8, 0.1f, 0.8f, 0.5f, 0.0f, 0.4f);
    b.set (P_rsNotchSync, 0).set (P_rsNotchRate, 0.3f).set (P_rsNotchMove, 0.8f).set (P_rsNotchFreq, 700)
     .set (P_filterMode, 1).set (P_filterCutoff, 1200).set (P_filterRes, 1.0f);
    reeseMacros (b);
}
inline void rs10 (B& b)   // Rotating Reese
{
    bass (b); reese (b, 3, 0, 2, 3, 14, 0.15f, 0.3f, 0.45f, 0.2f, 1.0f);
    b.set (P_rsRotate, 0.6f).set (P_rsRotRate, 0.25f).set (P_filterMode, 0).set (P_filterCutoff, 1500);
    ott (b, 0.35f); reeseMacros (b);
    b.macro (4, "Rotation", { { P_rsRotate, 0.4f } });
}
inline void rs11 (B& b)   // Classic 94 Reese (the oscillators, no Reese engine)
{
    bass (b, 0.02f);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc1Detune, -14).set (P_osc2Wave, 0).set (P_osc2Gain, 0.6f).set (P_osc2Detune, 14)
     .set (P_subWave, 1).set (P_subGain, 0.5f).set (P_subOct, -1)
     .set (P_filterMode, 2).set (P_filterCutoff, 650).set (P_filterRes, 2.0f).set (P_filterDrive, 3.0f)
     .set (P_filter2On, 1).set (P_filterRouting, 0).set (P_filter2Type, FT_NOTCH).set (P_filter2Slope, 1).set (P_filter2Cutoff, 600)
     .set (P_filter2LfoAmt, 0.4f).set (P_filter2LfoSrc, 1).set (P_lfo2Rate, 0.18f).set (P_lfo2Depth, 1.0f);
    ott (b, 0.35f);
    b.macro (0, "Detune", { { P_osc1Detune, -0.15f }, { P_osc2Detune, 0.15f } });
    b.macro (1, "Notch", { { P_filter2Cutoff, 0.3f } });
    b.macro (2, "Open", { { P_filterCutoff, 0.3f } });
}
inline void rs12 (B& b)   // Kick-Locked Reese (Break React)
{
    bass (b); reese (b, 2, 0, 2, 3, 14, 0.2f, 0.35f, 0.55f, 0.2f);
    b.set (P_filterMode, 2).set (P_filterCutoff, 1000).set (P_filterRes, 1.8f)
     .set (P_rxOn, 1).set (P_rxListen, 1).set (P_rxReset, 1).set (P_rxPitch, 3.0f).set (P_rxDecay, 80).set (P_rxNotch, 0.4f)
     .set (P_rxFromNote, 1).set (P_rxNote, 24);
    ott (b, 0.4f); reeseMacros (b);
    b.macro (4, "Kick", { { P_rxPitch, 0.15f }, { P_rxDuck, 0.4f } });
}
inline void rs13 (B& b)   // Hoover Reese Hybrid
{
    bass (b, 0.04f); reese (b, 4, 1, 2, 4, 14, 0.2f, 0.2f, 0.4f, 0.3f);
    b.set (P_supersawGain, 0.2f).set (P_supersawVoices, 7).set (P_supersawSpread, 30).set (P_supersawOct, -1)
     .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.2f).set (P_mEnv1S, 0.0f)
     .set (P_filterMode, 10).set (P_filterCutoff, 1600).set (P_filterRes, 1.5f);
    b.route (MS_ModEnv1, P_supersawSemi, -0.12f);
    ott (b, 0.4f); reeseMacros (b);
    b.macro (4, "Hoover", { { P_supersawGain, 0.4f } });
}
inline void rs14 (B& b)   // Sub Heavy Reese
{
    bass (b); reese (b, 2, 0, 2, 3, 14, 0.15f, 0.2f, 0.9f, 0.3f);
    b.set (P_filterMode, 13).set (P_filterCutoff, 450).set (P_filterRes, 1.5f).set (P_fEnvAmt, 300).set (P_fEnvD, 0.3f).set (P_fEnvS, 0.3f);
    ott (b, 0.3f); reeseMacros (b);
}
inline void rs15 (B& b)   // Halftime Wide Reese
{
    bass (b); reese (b, 3, 0, 2, 2, 14, 0.2f, 0.45f, 0.5f, 0.25f, 0.9f);
    b.set (P_rsRotate, 0.3f).set (P_rsRotRate, 0.12f).set (P_filterMode, 3).set (P_filterCutoff, 1200).set (P_filterRes, 2.0f);
    ott (b, 0.6f); reeseMacros (b);
}

// ======================================================================================================
// Sub & Bass (12)
inline void sb01 (B& b)   // Pure Sine Sub
{
    bass (b);
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.9f).set (P_osc1Oct, -1).set (P_filterCutoff, 400).set (P_filterRes, 0.5f);
    b.macro (0, "Harmonics", { { P_wmMix, 0.4f }, { P_wmDrive, 0.4f } });
    b.macro (1, "Tone", { { P_filterCutoff, 0.3f } });
}
inline void sb02 (B& b)   // 808 Boom
{
    bass (b);
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.9f).set (P_osc1Oct, -1).set (P_ampD, 1.2f).set (P_ampS, 0.0f).set (P_ampR, 0.4f)
     .set (P_pEnvAmt, 12).set (P_pEnvD, 0.06f).set (P_filterCutoff, 800).set (P_wmMix, 0.2f).set (P_wmDrive, 0.5f);
    b.macro (0, "Punch", { { P_pEnvAmt, 0.2f } });
    b.macro (1, "Length", { { P_ampD, 0.3f } });
    b.macro (2, "Dirt", { { P_wmMix, 0.5f } });
}
inline void sb03 (B& b)   // Triangle Rumble
{
    bass (b);
    b.set (P_osc1Wave, 2).set (P_osc1Gain, 0.85f).set (P_osc1Oct, -1).set (P_filterCutoff, 500).set (P_wmMix, 0.25f).set (P_wmFold, 0.2f);
    b.macro (0, "Fold", { { P_wmFold, 0.4f } });
    b.macro (1, "Tone", { { P_filterCutoff, 0.3f } });
}
inline void sb04 (B& b)   // Square Sub Steppa
{
    bass (b);
    b.set (P_subWave, 0).set (P_subGain, 0.8f).set (P_subOct, -1).set (P_osc1Wave, 3).set (P_osc1Gain, 0.4f).set (P_osc1Oct, -1)
     .set (P_filterMode, 11).set (P_filterCutoff, 300).set (P_filterRes, 0.5f);
    b.macro (0, "Square", { { P_filterCutoff, 0.35f } });
    b.macro (1, "Weight", { { P_osc1Gain, 0.4f } });
}
inline void sb05 (B& b)   // Warm Saw Bass
{
    bass (b, 0.02f);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_subWave, 1).set (P_subGain, 0.6f).set (P_subOct, -1)
     .set (P_filterMode, 13).set (P_filterCutoff, 500).set (P_filterRes, 2.0f).set (P_fEnvAmt, 900).set (P_fEnvD, 0.25f).set (P_fEnvS, 0.2f);
    ott (b, 0.3f);
    b.macro (0, "Open", { { P_filterCutoff, 0.3f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
}
inline void sb06 (B& b)   // Jump Up Wobble Sub
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.55f).set (P_subWave, 1).set (P_subGain, 0.6f).set (P_subOct, -1)
     .set (P_filterMode, 2).set (P_filterCutoff, 250).set (P_filterRes, 4.0f)
     .set (P_lfo1Sync, 6).set (P_lfo1Retrig, 1).set (P_lfo1Depth, 1.0f).set (P_filterLfoAmt, 0.5f).set (P_filterLfoSrc, 0);
    ott (b, 0.35f);
    b.macro (0, "Wobble", { { P_filterLfoAmt, 0.35f } });
    b.macro (1, "Res", { { P_filterRes, 0.3f } });
}
inline void sb07 (B& b)   // Dub Pressure Sub
{
    bass (b);
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.8f).set (P_osc1Oct, -1).set (P_osc2Wave, 2).set (P_osc2Gain, 0.25f)
     .set (P_filterCutoff, 600).set (P_fxOnShaper, 1).set (P_vshRate, 0).set (P_vshDepth, 0.5f).set (P_vshSmooth, 0.4f);
    b.macro (0, "Pump", { { P_vshDepth, 0.5f } });
    b.macro (1, "Grit", { { P_osc2Gain, 0.4f } });
}
inline void sb08 (B& b)   // Plucky Sub
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_subWave, 1).set (P_subGain, 0.7f).set (P_subOct, -1)
     .set (P_filterMode, 0).set (P_filterCutoff, 300).set (P_filterRes, 2.0f);
    pluckEnv (b, 0.35f, 2500);
    b.macro (0, "Decay", { { P_ampD, 0.3f }, { P_fEnvD, 0.3f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.25f } });
}
inline void sb09 (B& b)   // Ragga Bass
{
    bass (b, 0.05f);
    b.set (P_osc1Wave, 1).set (P_osc1Gain, 0.4f).set (P_subWave, 1).set (P_subGain, 0.7f).set (P_subOct, -1)
     .set (P_filterMode, 11).set (P_filterCutoff, 450).set (P_filterRes, 1.5f).set (P_fEnvAmt, 400).set (P_fEnvD, 0.2f).set (P_fEnvS, 0.3f);
    b.macro (0, "Square", { { P_osc1Gain, 0.4f } });
    b.macro (1, "Glide", { { P_porta, 0.1f } });
}
inline void sb10 (B& b)   // Kick-Locked Sub (Break React)
{
    bass (b);
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.9f).set (P_osc1Oct, -1).set (P_filterCutoff, 500)
     .set (P_rxOn, 1).set (P_rxListen, 1).set (P_rxReset, 1).set (P_rxPitch, 5.0f).set (P_rxDecay, 40).set (P_rxFromNote, 1).set (P_rxNote, 24);
    b.macro (0, "Kick", { { P_rxPitch, 0.15f } });
    b.macro (1, "Duck", { { P_rxDuck, 0.6f } });
}
inline void sb11 (B& b)   // Bubble Bass
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_subWave, 1).set (P_subGain, 0.6f).set (P_subOct, -1)
     .set (P_filterMode, 4).set (P_filterType, FT_BP).set (P_filterSlope, 1).set (P_filterCutoff, 300).set (P_filterRes, 8.0f)
     .set (P_fEnvAmt, 1500).set (P_fEnvD, 0.12f).set (P_fEnvS, 0.0f);
    b.macro (0, "Bubble", { { P_fEnvAmt, 0.25f } });
    b.macro (1, "Res", { { P_filterRes, 0.2f } });
}
inline void sb12 (B& b)   // Rollers Sub
{
    bass (b, 0.08f);
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.8f).set (P_osc1Oct, -1).set (P_osc2Wave, 2).set (P_osc2Gain, 0.3f)
     .set (P_filterCutoff, 700).set (P_filterKeyTrack, 0.6f);
    b.macro (0, "Glide", { { P_porta, 0.15f } });
    b.macro (1, "Body", { { P_osc2Gain, 0.4f } });
}

// ======================================================================================================
// Neuro (12)
inline void ne01 (B& b)   // Fold Machine
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.45f).set (P_osc2Oct, -1).set (P_subWave, 1).set (P_subGain, 0.4f)
     .set (P_wmMix, 0.6f).set (P_wmFold, 0.5f).set (P_wmDrive, 0.4f).set (P_filterCutoff, 1500).set (P_filterRes, 2.0f)
     .set (P_dsOn, 1).set (P_dsSteps, 16).set (P_dsRate, 2).set (P_dsDepth, 0.8f);
    b.dnaLane (0, 0, "F.f.Ff.cF.f.Fp.f", { 0.8f, 0.5f, 0.9f });
    b.route (MS_DnaSeq, P_filterCutoff, 0.3f);
    ott (b, 0.6f);
    b.macro (0, "Fold", { { P_wmFold, 0.4f } });
    b.macro (1, "Sequence", { { P_dsDepth, 0.2f } });
    b.macro (2, "OTT", { { P_mbcDepth, 0.3f } });
}
inline void ne02 (B& b)   // Comb Growl
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.5f).set (P_osc2Detune, 10).set (P_subWave, 1).set (P_subGain, 0.4f)
     .set (P_flt1Char, (float) CM_Comb).set (P_flt1CharAmt, 0.8f).set (P_filterCutoff, 400).set (P_filterRes, 6.0f)
     .set (P_lfo2Sync, 6).set (P_lfo2Retrig, 1).set (P_lfo2Depth, 1.0f).set (P_filterLfoAmt, 0.4f).set (P_filterLfoSrc, 1);
    ott (b, 0.55f);
    b.macro (0, "Comb", { { P_flt1CharAmt, 0.2f }, { P_filterRes, 0.2f } });
    b.macro (1, "Wobble", { { P_filterLfoAmt, 0.3f } });
}
inline void ne03 (B& b)   // Formant Talker
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.4f).set (P_subWave, 1).set (P_subGain, 0.4f)
     .set (P_flt1Char, (float) CM_VowelMorph).set (P_flt1CharAmt, 0.9f).set (P_filterCutoff, 900).set (P_filterRes, 4.0f)
     .set (P_lfo1Sync, 9).set (P_lfo1Retrig, 1).set (P_lfo1Depth, 1.0f).set (P_filterLfoAmt, 0.45f).set (P_filterLfoSrc, 0);
    ott (b, 0.5f);
    b.macro (0, "Vowel", { { P_filterCutoff, 0.35f } });
    b.macro (1, "Talk", { { P_filterLfoAmt, 0.3f } });
}
inline void ne04 (B& b)   // FM Snarl
{
    bass (b);
    b.set (P_complexGain, 0.6f).set (P_complexOct, -1).set (P_complexRatio, 2).set (P_complexFm, 500).set (P_complexShape, 6).set (P_complexMix, 0.7f)
     .set (P_subWave, 1).set (P_subGain, 0.45f).set (P_filterCutoff, 2000).set (P_filterRes, 1.5f)
     .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.3f).set (P_mEnv1S, 0.3f);
    b.route (MS_ModEnv1, P_complexFm, 0.3f);
    ott (b, 0.55f);
    b.macro (0, "FM", { { P_complexFm, 0.4f } });
    b.macro (1, "Fold", { { P_complexShape, 0.3f } });
}
inline void ne05 (B& b)   // Ring Mod Neuro
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.5f).set (P_subWave, 1).set (P_subGain, 0.45f)
     .set (P_arMod, 0).set (P_arRatio, 1.5f).set (P_arRing, 0.5f).set (P_filterCutoff, 1600).set (P_filterRes, 2.5f)
     .set (P_lfo1Sync, 3).set (P_lfo1Retrig, 1).set (P_lfo1Depth, 1.0f).set (P_filterLfoAmt, 0.3f).set (P_filterLfoSrc, 0);
    ott (b, 0.5f);
    b.macro (0, "Ring", { { P_arRing, 0.4f } });
    b.macro (1, "Ratio", { { P_arRatio, 0.1f } });
}
inline void ne06 (B& b)   // Bit Grinder
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.4f).set (P_osc2Oct, -1).set (P_subWave, 1).set (P_subGain, 0.45f)
     .set (P_wmMix, 0.7f).set (P_wmBits, 6).set (P_wmDown, 4).set (P_filterCutoff, 2500).set (P_filterRes, 1.5f)
     .set (P_lfo2Sync, 5).set (P_lfo2Retrig, 1).set (P_lfo2Depth, 1.0f).set (P_filterLfoAmt, 0.35f).set (P_filterLfoSrc, 1);
    ott (b, 0.5f);
    b.macro (0, "Crush", { { P_wmBits, -0.3f } });
    b.macro (1, "Wobble", { { P_filterLfoAmt, 0.3f } });
}
inline void ne07 (B& b)   // Phaser Wub
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.5f).set (P_osc2Oct, -1).set (P_subWave, 1).set (P_subGain, 0.45f)
     .set (P_filterMode, 0).set (P_filterSlope, 3).set (P_filterCutoff, 220).set (P_filterRes, 5.0f)
     .set (P_lfo1Sync, 6).set (P_lfo1Retrig, 1).set (P_lfo1Depth, 1.0f).set (P_filterLfoAmt, 0.6f).set (P_filterLfoSrc, 0)
     .set (P_fxOnFlanger, 1).set (P_flpMode, 1).set (P_flpStages, 1).set (P_flpRate, 0.4f).set (P_fxMixFlanger, 0.5f);
    b.lfoCustom (0, 4);
    ott (b, 0.45f);
    b.macro (0, "Wub", { { P_filterLfoAmt, 0.3f } });
    b.macro (1, "Phase", { { P_fxMixFlanger, 0.4f } });
}
inline void ne08 (B& b)   // Spectral Neuro
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.5f).set (P_osc2Detune, 12).set (P_subWave, 1).set (P_subGain, 0.45f)
     .set (P_filterCutoff, 1800).set (P_spOn, 1).set (P_spMix, 0.5f).set (P_spShift, 120).set (P_spScramble, 0.2f);
    ott (b, 0.5f);
    b.macro (0, "Shift", { { P_spShift, 0.15f } });
    b.macro (1, "Scramble", { { P_spScramble, 0.4f } });
}
inline void ne09 (B& b)   // Neuro Reese Sequence
{
    bass (b); reese (b, 2, 0, 2, 5, 14, 0.3f, 0.3f, 0.4f, 0.4f);
    b.set (P_wmMix, 0.5f).set (P_wmFold, 0.5f).set (P_filterCutoff, 1400).set (P_filterRes, 2.5f)
     .set (P_dsOn, 1).set (P_dsSteps, 16).set (P_dsRate, 2).set (P_dsDepth, 0.8f);
    b.dnaLane (0, 0, "F..fF.c.F.fF..pF", { 0.9f, 0.6f, 0.8f });
    b.route (MS_DnaSeq, P_filterCutoff, 0.3f);
    ott (b, 0.55f); reeseMacros (b);
    b.macro (4, "Fold", { { P_wmFold, 0.4f } });
}
inline void ne10 (B& b)   // Resampled Mid
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.5f).set (P_subWave, 1).set (P_subGain, 0.45f)
     .set (P_wmMix, 0.45f).set (P_wmFold, 0.35f).set (P_filterCutoff, 1700).set (P_filterRes, 2.0f)
     .set (P_fxOnSampler, 1).set (P_smpModel, 2).set (P_smpDrive, 0.4f).set (P_smpNoise, 0.05f)
     .set (P_lfo2Sync, 9).set (P_lfo2Retrig, 1).set (P_lfo2Depth, 1.0f).set (P_filterLfoAmt, 0.3f).set (P_filterLfoSrc, 1);
    ott (b, 0.55f);
    b.macro (0, "Crunch", { { P_smpDrive, 0.5f } });
    b.macro (1, "Fold", { { P_wmFold, 0.4f } });
}
inline void ne11 (B& b)   // Tearout Squeal
{
    bass (b);
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 4).set (P_osc2Gain, 0.45f).set (P_osc2Oct, 1).set (P_subWave, 1).set (P_subGain, 0.45f)
     .set (P_flt1Char, (float) CM_Comb).set (P_flt1CharAmt, 0.9f).set (P_filterCutoff, 1800).set (P_filterRes, 10.0f)
     .set (P_lfo1Sync, 3).set (P_lfo1Retrig, 1).set (P_lfo1Depth, 1.0f).set (P_filterLfoAmt, 0.35f).set (P_filterLfoSrc, 0);
    ott (b, 0.6f);
    b.macro (0, "Squeal", { { P_filterRes, 0.3f } });
    b.macro (1, "Rate", { { P_filterLfoAmt, 0.3f } });
}
inline void ne12 (B& b)   // Robot Voice
{
    bass (b);
    b.set (P_osc1Wave, 1).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.5f).set (P_subWave, 1).set (P_subGain, 0.45f)
     .set (P_flt1Char, (float) CM_Formant2).set (P_flt1CharAmt, 0.85f).set (P_filterCutoff, 1100).set (P_filterRes, 3.0f)
     .set (P_dsOn, 1).set (P_dsSteps, 8).set (P_dsRate, 3).set (P_dsDepth, 0.6f);
    b.dnaLane (0, 0, "FpFfFpFc", { 0.8f, 0.5f });
    b.route (MS_DnaSeq, P_filterCutoff, 0.35f);
    ott (b, 0.5f);
    b.macro (0, "Vowel", { { P_filterCutoff, 0.3f } });
    b.macro (1, "Sequence", { { P_dsDepth, 0.3f } });
}

// ======================================================================================================
// Hoover & Rave (8)
inline void hv01 (B& b)   // Dominator 92
{
    b.set (P_polyphony, 1).set (P_porta, 0.08f)
     .set (P_supersawGain, 0.7f).set (P_supersawVoices, 9).set (P_supersawSpread, 45).set (P_osc1Wave, 4).set (P_osc1Gain, 0.5f).set (P_osc1Oct, -1)
     .set (P_ampA, 0.01f).set (P_ampD, 0.3f).set (P_ampS, 0.9f).set (P_ampR, 0.3f).set (P_filterCutoff, 4200).set (P_filterRes, 1.2f).set (P_fEnvAmt, 0)
     .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.35f).set (P_mEnv1S, 0.0f)
     .set (P_fxOnFlanger, 1).set (P_fxMixFlanger, 0.6f).set (P_flpRate, 0.12f).set (P_flpFeedback, 0.6f);
    space (b, 0.25f, 0.2f, 6);
    b.route (MS_ModEnv1, P_supersawSemi, -0.2f); b.route (MS_ModEnv1, P_osc1Semi, -0.2f);
    b.macro (0, "Scoop", { { P_mEnv1D, 0.2f } });
    b.macro (1, "Flange", { { P_fxMixFlanger, 0.3f } });
}
inline void hv02 (B& b)   // Mentasm Stab
{
    b.set (P_osc1Wave, 1).set (P_osc1Gain, 0.45f).set (P_osc1Detune, -20).set (P_osc2Wave, 1).set (P_osc2Gain, 0.45f).set (P_osc2Detune, 0)
     .set (P_osc3Wave, 1).set (P_osc3Gain, 0.45f).set (P_osc3Detune, 20).set (P_osc3Oct, 0)
     .set (P_pEnvAmt, -5).set (P_pEnvD, 0.12f).set (P_filterMode, 9).set (P_filterCutoff, 2500).set (P_filterRes, 2.0f);
    pluckEnv (b, 0.5f, 2500);
    space (b, 0.25f, 0.25f, 6);
    b.macro (0, "Dip", { { P_pEnvAmt, -0.15f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
}
inline void hv03 (B& b)   // Rave Organ
{
    b.set (P_osc1Wave, 7).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 7).set (P_osc2Gain, 0.4f).set (P_osc2Oct, 1).set (P_osc2Detune, 5)
     .set (P_filterCutoff, 5000).set (P_fEnvAmt, 0).set (P_polyphony, 8);
    pluckEnv (b, 0.35f, 0);
    space (b, 0.2f, 0.15f, 6, 0.3f);
    b.macro (0, "Drawbar", { { P_osc2Gain, 0.4f } });
    b.macro (1, "Length", { { P_ampD, 0.3f } });
}
inline void hv04 (B& b)   // Hoover Glide Lead
{
    b.set (P_polyphony, 1).set (P_porta, 0.12f)
     .set (P_supersawGain, 0.6f).set (P_supersawVoices, 7).set (P_supersawSpread, 35).set (P_osc1Wave, 5).set (P_osc1Gain, 0.45f)
     .set (P_ampA, 0.01f).set (P_ampS, 0.9f).set (P_ampR, 0.3f).set (P_filterCutoff, 3500).set (P_fEnvAmt, 0)
     .set (P_fxOnFlanger, 1).set (P_fxMixFlanger, 0.45f).set (P_flpRate, 0.2f);
    space (b, 0.25f, 0.25f, 6);
    b.route (MS_ModWheel, P_flpDepth, 0.3f);
    b.macro (0, "Glide", { { P_porta, 0.2f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
}
inline void hv05 (B& b)   // Belgian Hoover
{
    b.set (P_polyphony, 1).set (P_porta, 0.05f)
     .set (P_supersawGain, 0.75f).set (P_supersawVoices, 9).set (P_supersawSpread, 55).set (P_osc1Wave, 4).set (P_osc1Gain, 0.4f).set (P_osc1Oct, -1)
     .set (P_ampA, 0.005f).set (P_ampS, 0.9f).set (P_ampR, 0.25f).set (P_filterCutoff, 3800).set (P_fEnvAmt, 0)
     .set (P_mEnv1A, 0.08f).set (P_mEnv1D, 0.25f).set (P_mEnv1S, 0.0f)
     .set (P_fxOnFlanger, 1).set (P_flpTZ, 1).set (P_flpRate, 0.1f).set (P_fxMixFlanger, 0.5f);
    space (b, 0.25f, 0.2f, 6);
    b.route (MS_ModEnv1, P_supersawSemi, 0.1f);
    b.route (MS_Lfo1, P_supersawSemi, 0.02f);
    b.macro (0, "Rise", { { P_mEnv1A, 0.2f } });
    b.macro (1, "Spread", { { P_supersawSpread, 0.2f } });
}
inline void hv06 (B& b)   // Alarm Hoover
{
    b.set (P_polyphony, 1).set (P_supersawGain, 0.6f).set (P_supersawVoices, 7).set (P_supersawSpread, 30).set (P_osc1Wave, 1).set (P_osc1Gain, 0.4f)
     .set (P_ampA, 0.01f).set (P_ampS, 0.9f).set (P_ampR, 0.3f).set (P_filterCutoff, 3000).set (P_fEnvAmt, 0)
     .set (P_lfo2Wave, 3).set (P_lfo2Sync, 6).set (P_lfo2Depth, 1.0f);
    b.route (MS_Lfo2, P_supersawSemi, 0.15f); b.route (MS_Lfo2, P_osc1Semi, 0.15f);
    space (b, 0.3f, 0.2f, 6);
    b.macro (0, "Alarm", { { P_mod1Amt, 0.1f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
}
inline void hv07 (B& b)   // Hardcore Hoover Chords
{
    b.set (P_supersawGain, 0.55f).set (P_supersawVoices, 7).set (P_supersawSpread, 35).set (P_osc1Wave, 4).set (P_osc1Gain, 0.35f)
     .set (P_chdOn, 1).set (P_chdPreset, 1).set (P_polyphony, 12).set (P_filterCutoff, 3200).set (P_fEnvAmt, 0)
     .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.2f).set (P_mEnv1S, 0.0f)
     .set (P_fxOnFlanger, 1).set (P_fxMixFlanger, 0.4f).set (P_flpRate, 0.15f);
    pluckEnv (b, 0.6f, 0);
    b.route (MS_ModEnv1, P_supersawSemi, -0.12f);
    space (b, 0.3f, 0.25f, 6);
    b.macro (0, "Scoop", { { P_mEnv1D, 0.2f } });
    b.macro (1, "Length", { { P_ampD, 0.3f } });
}
inline void hv08 (B& b)   // Arp Rave Pulse
{
    b.set (P_polyphony, 6).set (P_osc1Wave, 5).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 4).set (P_osc2Gain, 0.4f).set (P_osc2Detune, 10)
     .set (P_filterMode, 9).set (P_filterCutoff, 2600).set (P_filterRes, 3.0f)
     .set (P_arpOn, 1).set (P_arpMode, 0).set (P_arpOctaves, 2).set (P_arpRate, 2).set (P_arpGate, 0.5f);
    pluckEnv (b, 0.2f, 2500);
    b.arpPat ("AxxAxxAxAxxAxxAx");
    space (b, 0.2f, 0.3f, 6);
    b.macro (0, "Gate", { { P_arpGate, 0.35f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
}

// ======================================================================================================
// Stabs & Chords (10)
inline void st01 (B& b)   // Minor 9 Stab
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.35f).set (P_osc2Detune, 7);
    stabChord (b, 3, 9, 1500, 4000);
    b.set (P_fxOnSampler, 1).set (P_smpModel, 1).set (P_smpDrive, 0.3f);
    space (b, 0.3f, 0.28f, 6, 0.2f);
    b.macro (0, "Snap", { { P_fEnvAmt, 0.2f } });
    b.macro (1, "Crunch", { { P_smpDrive, 0.5f } });
}
inline void st02 (B& b)   // Dub Chord Delay
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.45f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.4f);
    stabChord (b, 1, 0, 1200, 3000);
    b.set (P_delayFeedback, 0.6f);
    space (b, 0.35f, 0.45f, 7, 0.15f);
    b.macro (0, "Echo", { { P_delayFeedback, 0.3f }, { P_delayMix, 0.3f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
}
inline void st03 (B& b)   // Open Rave Stab
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.4f).set (P_osc2Detune, 12);
    stabChord (b, 7, 10, 1800, 4500);
    space (b, 0.3f, 0.3f, 6, 0.25f);
    b.macro (0, "Snap", { { P_fEnvAmt, 0.2f } });
    b.macro (1, "Res", { { P_filterRes, 0.25f } });
}
inline void st04 (B& b)   // Sus Skank
{
    b.set (P_osc1Wave, 1).set (P_osc1Gain, 0.45f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.35f);
    stabChord (b, 4, 11, 2200, 3000);
    b.set (P_ampD, 0.18f).set (P_ampR, 0.12f).set (P_fEnvD, 0.1f);
    space (b, 0.25f, 0.3f, 6, 0.2f);
    b.macro (0, "Length", { { P_ampD, 0.3f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
}
inline void st05 (B& b)   // Power Stab
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.45f).set (P_osc2Detune, 9)
     .set (P_wmMix, 0.4f).set (P_wmDrive, 0.6f);
    stabChord (b, 6, 2, 2000, 3500);
    space (b, 0.25f, 0.2f, 6);
    b.macro (0, "Drive", { { P_wmDrive, 0.3f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
}
inline void st06 (B& b)   // Glass Major 7
{
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.35f).set (P_osc2Oct, 1)
     .set (P_fmSlot1Source, 1).set (P_fmSlot1Dest, 0).set (P_fmSlot1Amt, 180);
    stabChord (b, 2, 0, 4000, 2000);
    space (b, 0.35f, 0.3f, 7, 0.25f);
    b.macro (0, "Glass", { { P_fmSlot1Amt, 0.2f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}
inline void st07 (B& b)   // Jungle Horn Stab
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.45f).set (P_osc2Detune, 8).set (P_osc3Wave, 4).set (P_osc3Gain, 0.3f).set (P_osc3Oct, 0);
    stabChord (b, 1, 12, 900, 2800);
    b.set (P_fEnvA, 0.02f).set (P_fEnvD, 0.25f).set (P_ampD, 0.5f);
    space (b, 0.3f, 0.2f, 6);
    b.macro (0, "Brass", { { P_fEnvAmt, 0.25f } });
    b.macro (1, "Res", { { P_filterRes, 0.2f } });
}
inline void st08 (B& b)   // Stutter Chord
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.35f)
     .set (P_fxOnStutter, 1).set (P_rptChance, 0.4f).set (P_rptLength, 2).set (P_rptDuration, 1).set (P_rptShrink, 0.4f);
    stabChord (b, 1, 9, 1600, 3800);
    space (b, 0.25f, 0.25f, 6);
    b.macro (0, "Stutter", { { P_rptChance, 0.5f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
}
inline void st09 (B& b)   // Octave Sampler Stab
{
    b.set (P_osc1Wave, 1).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.35f)
     .set (P_fxOnSampler, 1).set (P_smpModel, 0).set (P_smpDrive, 0.35f).set (P_smpNoise, 0.08f);
    stabChord (b, 8, 0, 2400, 3000);
    space (b, 0.25f, 0.3f, 6);
    b.macro (0, "Crunch", { { P_smpDrive, 0.5f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
}
inline void st10 (B& b)   // Reese Chord Stab
{
    reese (b, 2, 0, 0, 3, 18, 0.2f, 0.3f, 0.0f, 0.0f, 0.8f);   // (no Reese drive: sixteen voices of it would be heavy)
    b.set (P_rsMonoBelow, 0).set (P_rsLevel, 0.6f);
    stabChord (b, 1, 2, 1800, 3000);
    b.set (P_ampD, 0.5f);
    space (b, 0.3f, 0.25f, 6);
    b.macro (0, "Notch", { { P_rsNotch, 0.5f } });
    b.macro (1, "Snap", { { P_fEnvAmt, 0.2f } });
}

// ======================================================================================================
// Pads & Atmos (10)
inline void pd01 (B& b)   // Liquid Strings
{
    b.set (P_supersawGain, 0.5f).set (P_supersawVoices, 7).set (P_supersawSpread, 25).set (P_osc1Wave, 2).set (P_osc1Gain, 0.2f)
     .set (P_filterMode, 0).set (P_filterSlope, 2).set (P_filterCutoff, 2600).set (P_filterRes, 1.0f).set (P_polyphony, 10);
    padEnv (b, 0.7f, 2.0f);
    space (b, 0.45f, 0.15f, 6, 0.35f, 3.5f);
    b.macro (0, "Bright", { { P_filterCutoff, 0.3f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}
inline void pd02 (B& b)   // Dark Atmos
{
    b.set (P_osc1Wave, 2).set (P_osc1Gain, 0.45f).set (P_osc2Wave, 3).set (P_osc2Gain, 0.35f).set (P_osc2Detune, 9)
     .set (P_filterCutoff, 900).set (P_filterRes, 1.5f).set (P_grMix, 0.3f).set (P_grSize, 180).set (P_grDensity, 15).set (P_polyphony, 10);
    padEnv (b, 1.2f, 2.5f);
    space (b, 0.55f, 0.1f, 6, 0.3f, 4.0f);
    b.macro (0, "Grain", { { P_grMix, 0.5f } });
    b.macro (1, "Open", { { P_filterCutoff, 0.35f } });
}
inline void pd03 (B& b)   // Shimmer Heaven
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.35f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.35f).set (P_osc2Oct, 1)
     .set (P_filterCutoff, 3000).set (P_shimmerMix, 0.45f).set (P_polyphony, 10);
    padEnv (b, 0.9f, 2.5f);
    space (b, 0.4f, 0.1f, 6, 0.3f, 3.8f);
    b.set (P_shimmerMix, 0.45f);
    b.macro (0, "Shimmer", { { P_shimmerMix, 0.4f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.3f } });
}
inline void pd04 (B& b)   // Granular Haze
{
    b.set (P_supersawGain, 0.35f).set (P_supersawVoices, 5).set (P_osc1Wave, 3).set (P_osc1Gain, 0.35f).set (P_osc1Oct, 1)
     .set (P_filterCutoff, 2400).set (P_grMix, 0.55f).set (P_grSize, 120).set (P_grDensity, 30).set (P_grJitter, 0.5f).set (P_grReverse, 0.3f).set (P_polyphony, 10);
    padEnv (b, 1.0f, 2.2f);
    space (b, 0.45f, 0.15f, 6, 0.2f, 3.2f);
    b.macro (0, "Grain", { { P_grMix, 0.4f } });
    b.macro (1, "Reverse", { { P_grReverse, 0.5f } });
}
inline void pd05 (B& b)   // Spectral Choir Pad
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.4f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.3f).set (P_osc2Detune, 10)
     .set (P_flt1Char, (float) CM_Formant).set (P_flt1CharAmt, 0.6f).set (P_filterCutoff, 1000).set (P_filterRes, 2.0f)
     .set (P_spOn, 1).set (P_spMix, 0.5f).set (P_spBlur, 0.6f).set (P_polyphony, 10);
    padEnv (b, 0.8f, 2.0f);
    space (b, 0.45f, 0.1f, 6, 0.2f, 3.5f);
    b.macro (0, "Blur", { { P_spBlur, 0.3f } });
    b.macro (1, "Vowel", { { P_filterCutoff, 0.3f } });
}
inline void pd06 (B& b)   // Rainforest Pad
{
    b.set (P_osc1Wave, 4).set (P_osc1Gain, 0.4f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.35f).set (P_osc2Oct, 1)
     .set (P_filterCutoff, 1600).set (P_filterRes, 2.0f).set (P_filterLfoAmt, 0.25f).set (P_filterLfoSrc, 1).set (P_lfo2Rate, 0.09f).set (P_lfo2Depth, 1.0f)
     .set (P_fxOnSampler, 1).set (P_smpModel, 2).set (P_smpNoise, 0.25f).set (P_fxMixSampler, 0.5f).set (P_polyphony, 10);
    padEnv (b, 1.0f, 2.5f);
    space (b, 0.5f, 0.2f, 7, 0.3f, 3.8f);
    b.macro (0, "Drift", { { P_filterLfoAmt, 0.3f } });
    b.macro (1, "Dust", { { P_smpNoise, 0.3f } });
}
inline void pd07 (B& b)   // Amen Dream Pad
{
    b.set (P_supersawGain, 0.5f).set (P_supersawVoices, 9).set (P_supersawSpread, 30)
     .set (P_filterCutoff, 2000).set (P_fxOnFlanger, 1).set (P_flpMode, 1).set (P_flpStages, 1).set (P_flpRate, 0.08f).set (P_fxMixFlanger, 0.5f).set (P_polyphony, 10);
    padEnv (b, 0.8f, 2.2f);
    space (b, 0.45f, 0.2f, 6, 0.25f, 3.5f);
    b.macro (0, "Phase", { { P_fxMixFlanger, 0.4f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.3f } });
}
inline void pd08 (B& b)   // Detroit Strings
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.4f).set (P_osc1Detune, -9).set (P_osc2Wave, 0).set (P_osc2Gain, 0.4f).set (P_osc2Detune, 9)
     .set (P_osc3Wave, 0).set (P_osc3Gain, 0.3f).set (P_osc3Oct, 1).set (P_osc3Detune, 0)
     .set (P_filterMode, 10).set (P_filterCutoff, 2200).set (P_filterRes, 1.0f).set (P_polyphony, 10);
    padEnv (b, 0.6f, 1.8f);
    space (b, 0.4f, 0.15f, 6, 0.5f, 3.0f);
    b.macro (0, "Ensemble", { { P_chorusMix, 0.4f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.3f } });
}
inline void pd09 (B& b)   // Ambient Reese Pad
{
    reese (b, 3, 0, 2, 0, 14, 0.15f, 0.4f, 0.0f, 0.0f, 1.0f);
    b.set (P_rsMonoBelow, 0).set (P_rsRotate, 0.4f).set (P_rsRotRate, 0.08f).set (P_rsLevel, 0.6f)
     .set (P_filterCutoff, 1800).set (P_polyphony, 10);
    padEnv (b, 1.5f, 3.0f);
    space (b, 0.5f, 0.15f, 6, 0.0f, 4.0f);
    b.macro (0, "Notch", { { P_rsNotch, 0.5f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}
inline void pd10 (B& b)   // Sub Drone
{
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.3f).set (P_osc2Oct, 1)
     .set (P_wmMix, 0.3f).set (P_wmFold, 0.3f).set (P_filterCutoff, 1200)
     .set (P_lfo3Rate, 0.05f).set (P_lfo3Depth, 1.0f).set (P_polyphony, 10);
    b.route (MS_Lfo3, P_wmFold, 0.3f);
    padEnv (b, 2.0f, 3.0f);
    space (b, 0.5f, 0.0f, 6, 0.0f, 4.2f);
    b.macro (0, "Fold", { { P_wmFold, 0.4f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}

// ======================================================================================================
// Leads (8)
inline void ld01 (B& b)   // Pirate Square
{
    b.set (P_polyphony, 1).set (P_porta, 0.05f).set (P_osc1Wave, 1).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.35f).set (P_osc2Detune, 6)
     .set (P_ampA, 0.004f).set (P_ampS, 0.85f).set (P_ampR, 0.2f).set (P_filterMode, 9).set (P_filterCutoff, 2800).set (P_filterRes, 2.0f).set (P_fEnvAmt, 800);
    space (b, 0.2f, 0.3f, 6);
    b.route (MS_ModWheel, P_osc1Semi, 0.01f);
    b.macro (0, "Bright", { { P_filterCutoff, 0.25f } });
    b.macro (1, "Glide", { { P_porta, 0.15f } });
}
inline void ld02 (B& b)   // Sine Whistle
{
    b.set (P_polyphony, 1).set (P_porta, 0.06f).set (P_osc1Wave, 3).set (P_osc1Gain, 0.7f).set (P_osc1Oct, 1)
     .set (P_ampA, 0.02f).set (P_ampS, 0.9f).set (P_ampR, 0.3f).set (P_filterCutoff, 8000).set (P_fEnvAmt, 0)
     .set (P_lfo1Rate, 5.5f).set (P_lfo1Depth, 1.0f);
    b.route (MS_Lfo1, P_osc1Detune, 0.08f);
    space (b, 0.3f, 0.35f, 7);
    b.macro (0, "Vibrato", { { P_mod1Amt, 0.1f } });
    b.macro (1, "Echo", { { P_delayMix, 0.3f } });
}
inline void ld03 (B& b)   // Acid 303 Lead
{
    b.set (P_polyphony, 1).set (P_porta, 0.06f).set (P_osc1Wave, 0).set (P_osc1Gain, 0.7f)
     .set (P_ampA, 0.002f).set (P_ampD, 0.3f).set (P_ampS, 0.6f).set (P_ampR, 0.12f)
     .set (P_filterMode, 12).set (P_filterCutoff, 700).set (P_filterRes, 15.0f).set (P_fEnvAmt, 3000).set (P_fEnvD, 0.2f).set (P_fEnvS, 0.0f);
    space (b, 0.15f, 0.3f, 6);
    b.macro (0, "Cutoff", { { P_filterCutoff, 0.35f } });
    b.macro (1, "Env Mod", { { P_fEnvAmt, 0.25f } });
    b.macro (2, "Accent", { { P_filterRes, 0.2f } });
}
inline void ld04 (B& b)   // Saw Lead 95
{
    b.set (P_polyphony, 1).set (P_porta, 0.03f).set (P_supersawGain, 0.6f).set (P_supersawVoices, 5).set (P_supersawSpread, 18)
     .set (P_ampA, 0.005f).set (P_ampS, 0.85f).set (P_ampR, 0.25f).set (P_filterMode, 0).set (P_filterCutoff, 3500).set (P_filterRes, 1.5f).set (P_fEnvAmt, 0);
    space (b, 0.25f, 0.3f, 6);
    b.macro (0, "Bright", { { P_filterCutoff, 0.25f } });
    b.macro (1, "Spread", { { P_supersawSpread, 0.3f } });
}
inline void ld05 (B& b)   // Ragga Horn Lead
{
    b.set (P_polyphony, 1).set (P_porta, 0.04f).set (P_osc1Wave, 4).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.35f).set (P_osc2Detune, 5)
     .set (P_ampA, 0.01f).set (P_ampS, 0.8f).set (P_ampR, 0.2f).set (P_filterMode, 12).set (P_filterCutoff, 1100).set (P_filterRes, 3.0f)
     .set (P_fEnvAmt, 2200).set (P_fEnvA, 0.03f).set (P_fEnvD, 0.3f).set (P_fEnvS, 0.3f);
    space (b, 0.2f, 0.25f, 6);
    b.macro (0, "Brass", { { P_fEnvAmt, 0.25f } });
    b.macro (1, "Res", { { P_filterRes, 0.2f } });
}
inline void ld06 (B& b)   // SP Crunch Lead
{
    b.set (P_polyphony, 1).set (P_osc1Wave, 1).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.4f).set (P_osc2Detune, 10)
     .set (P_ampA, 0.004f).set (P_ampS, 0.85f).set (P_ampR, 0.2f).set (P_filterMode, 11).set (P_filterCutoff, 2400).set (P_filterRes, 2.0f).set (P_fEnvAmt, 0)
     .set (P_fxOnSampler, 1).set (P_smpModel, 0).set (P_smpDrive, 0.4f).set (P_smpNoise, 0.08f);
    space (b, 0.2f, 0.3f, 6);
    b.macro (0, "Crunch", { { P_smpDrive, 0.5f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
}
inline void ld07 (B& b)   // FM Lead
{
    b.set (P_polyphony, 1).set (P_porta, 0.03f).set (P_complexGain, 0.6f).set (P_complexRatio, 1).set (P_complexFm, 300).set (P_complexShape, 2).set (P_complexMix, 0.6f)
     .set (P_ampA, 0.004f).set (P_ampS, 0.85f).set (P_ampR, 0.2f).set (P_filterCutoff, 5000).set (P_fEnvAmt, 0)
     .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.25f).set (P_mEnv1S, 0.4f);
    b.route (MS_ModEnv1, P_complexFm, 0.2f);
    space (b, 0.2f, 0.25f, 6);
    b.macro (0, "FM", { { P_complexFm, 0.4f } });
    b.macro (1, "Fold", { { P_complexShape, 0.3f } });
}
inline void ld08 (B& b)   // Formant Lead
{
    b.set (P_polyphony, 1).set (P_porta, 0.05f).set (P_osc1Wave, 0).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.4f)
     .set (P_ampA, 0.01f).set (P_ampS, 0.85f).set (P_ampR, 0.25f)
     .set (P_flt1Char, (float) CM_VowelMorph).set (P_flt1CharAmt, 0.8f).set (P_filterCutoff, 1200).set (P_filterRes, 3.0f).set (P_fEnvAmt, 0)
     .set (P_lfo2Rate, 0.4f).set (P_lfo2Depth, 1.0f).set (P_filterLfoAmt, 0.3f).set (P_filterLfoSrc, 1);
    space (b, 0.25f, 0.3f, 6);
    b.macro (0, "Vowel", { { P_filterCutoff, 0.3f } });
    b.macro (1, "Talk", { { P_filterLfoAmt, 0.3f } });
}

// ======================================================================================================
// Breaks & Drums (10): the synthesised demo breaks (no samples of anyone's record), chopped different ways
inline void bk01 (B& b)   // Original Break 170
{
    breakKit (b, 0);
    b.set (P_bcStretch, 1).set (P_bcColour, 2).set (P_bsOn, 1);
    b.breakEdit = [] (BreakData& d) { breaktools::original (d, 0, 32); breaktools::ghosts (d, 1, 32, 3, 0.6f, 0, false); };
    b.macro (0, "Crunch", { { P_bcCycle, 0.4f } });
    b.macro (1, "Pitch", { { P_bcPitch, 0.15f } });
}
inline void bk02 (B& b)   // Two-Step Roller
{
    breakKit (b, 1);
    b.set (P_bcStretch, 2).set (P_bcColour, 1).set (P_bsOn, 1).set (P_bsSwing, 0.1f);
    b.breakEdit = [] (BreakData& d) { breaktools::original (d, 0, 32); breaktools::twoStep (d, 0, 32, 5, 0.7f, 0, false); };
    b.macro (0, "Swing", { { P_bsSwing, 0.3f } });
    b.macro (1, "Pitch", { { P_bcPitch, 0.15f } });
}
inline void bk03 (B& b)   // Rollers 165 Swing
{
    breakKit (b, 2);
    b.set (P_bcStretch, 2).set (P_bcColour, 0).set (P_bsOn, 1).set (P_bsSwing, 0.15f);
    b.breakEdit = [] (BreakData& d) { breaktools::original (d, 0, 32); breaktools::rollers (d, 0, 32, 7, 0.6f, 0, false); };
    b.macro (0, "Swing", { { P_bsSwing, 0.3f } });
    b.macro (1, "Level", { { P_bcLevel, 0.2f } });
}
inline void bk04 (B& b)   // Chopped Frankenbreak
{
    breakKit (b, 0);
    b.set (P_bcStretch, 1).set (P_bcColour, 2).set (P_bsOn, 1).set (P_bsChainOn, 1);
    b.breakEdit = [] (BreakData& d) { breaktools::frankenbreak (d, 0, 32, 4, 0.8f, 0); breaktools::frankenbreak (d, 1, 32, 9, 0.7f, 1); BreakStore::setChain (d, "AABA"); };
    b.macro (0, "Crunch", { { P_bcCycle, 0.4f } });
    b.macro (1, "Pitch", { { P_bcPitch, 0.15f } });
}
inline void bk05 (B& b)   // Reverse Edits
{
    breakKit (b, 0);
    b.set (P_bcStretch, 2).set (P_bcColour, 1).set (P_bsOn, 1).set (P_bsChainOn, 1);
    b.breakEdit = [] (BreakData& d) { breaktools::original (d, 0, 32); breaktools::reverseSwell (d, 1, 32, 2, 0.6f, 0, false); BreakStore::setChain (d, "AAAB"); };
    b.macro (0, "Pitch", { { P_bcPitch, 0.15f } });
    b.macro (1, "Level", { { P_bcLevel, 0.2f } });
}
inline void bk06 (B& b)   // Halftime Break
{
    breakKit (b, 1);
    b.set (P_bcStretch, 1).set (P_bcColour, 3).set (P_bsOn, 1);
    b.breakEdit = [] (BreakData& d) { breaktools::original (d, 0, 32); breaktools::halftime (d, 0, 32, 1, 0.8f, 0, false); };
    b.macro (0, "Crunch", { { P_bcCycle, 0.4f } });
    b.macro (1, "Pitch", { { P_bcPitch, 0.15f } });
}
inline void bk07 (B& b)   // SP Crunch Break
{
    breakKit (b, 2);
    b.set (P_bcStretch, 1).set (P_bcColour, 1).set (P_bsOn, 1);
    b.breakEdit = [] (BreakData& d) { breaktools::jungle (d, 0, 32, 11); breaktools::snareBuild (d, 1, 32, 4, 0.7f, 0, false); };
    b.macro (0, "Crunch", { { P_bcCycle, 0.4f } });
    b.macro (1, "Pitch", { { P_bcPitch, 0.15f } });
}
inline void bk08 (B& b)   // Break Keys 174
{
    breakKit (b, 1);
    b.set (P_bcStretch, 0).set (P_bcColour, 2).set (P_bcSync, 0).set (P_bsOn, 0).set (P_bcVelSens, 0.8f);
    b.breakEdit = [] (BreakData& d) { for (int k = 0; k < BreakData::kSlices; ++k) d.brk[0].slice[k].choke = 0; };
    b.macro (0, "Pitch", { { P_bcPitch, 0.15f } });
    b.macro (1, "Level", { { P_bcLevel, 0.2f } });
}
inline void bk09 (B& b)   // Fill Machine
{
    breakKit (b, 0);
    b.set (P_bcStretch, 1).set (P_bcColour, 2).set (P_bsOn, 1).set (P_bsAutoFill, 1).set (P_bsAutoFillProb, 1).set (P_bsFillLen, 1);
    b.breakEdit = [] (BreakData& d) { breaktools::original (d, 0, 32); };
    b.macro (0, "Swing", { { P_bsSwing, 0.3f } });
    b.macro (1, "Pitch", { { P_bcPitch, 0.15f } });
}
inline void bk10 (B& b)   // Akai Cyclic Stretch
{
    breakKit (b, 2);
    b.set (P_bcStretch, 1).set (P_bcColour, 2).set (P_bcCycle, 60).set (P_bsOn, 1);
    b.breakEdit = [] (BreakData& d) { breaktools::original (d, 0, 32); breaktools::stutter (d, 1, 32, 6, 0.5f); BreakStore::setChain (d, "AAAB"); };
    b.set (P_bsChainOn, 1);
    b.macro (0, "Cycle", { { P_bcCycle, 0.4f } });
    b.macro (1, "Pitch", { { P_bcPitch, 0.15f } });
}

// ======================================================================================================
// FX & Risers (8)
inline void fx01 (B& b)   // Noise Riser
{
    b.set (P_supersawGain, 0.6f).set (P_supersawVoices, 9).set (P_supersawSpread, 80).set (P_osc1Wave, 1).set (P_osc1Gain, 0.3f)
     .set (P_ampA, 0.5f).set (P_ampS, 1.0f).set (P_ampR, 0.8f).set (P_filterMode, 0).set (P_filterCutoff, 300).set (P_filterRes, 4.0f).set (P_fEnvAmt, 0)
     .set (P_mEnv1A, 3.5f).set (P_mEnv1D, 0.5f).set (P_mEnv1S, 1.0f).set (P_mEnv1R, 1.0f);
    b.route (MS_ModEnv1, P_filterCutoff, 0.8f); b.route (MS_ModEnv1, P_supersawSemi, 0.6f);
    space (b, 0.45f, 0.3f, 6, 0.0f, 3.5f);
    b.macro (0, "Rise Time", { { P_mEnv1A, -0.4f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}
inline void fx02 (B& b)   // Downlifter
{
    b.set (P_supersawGain, 0.5f).set (P_supersawVoices, 7).set (P_supersawSpread, 50).set (P_osc1Wave, 0).set (P_osc1Gain, 0.3f)
     .set (P_ampA, 0.005f).set (P_ampD, 2.5f).set (P_ampS, 0.0f).set (P_ampR, 1.0f).set (P_filterCutoff, 6000).set (P_fEnvAmt, -5000).set (P_fEnvD, 2.0f).set (P_fEnvS, 0.0f)
     .set (P_pEnvAmt, -24).set (P_pEnvD, 2.0f);
    space (b, 0.5f, 0.3f, 6, 0.0f, 3.8f);
    b.macro (0, "Drop", { { P_pEnvAmt, -0.2f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}
inline void fx03 (B& b)   // Laser Zap
{
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.7f).set (P_osc1Oct, 1).set (P_pEnvAmt, 36).set (P_pEnvD, 0.08f).set (P_filterCutoff, 10000).set (P_fEnvAmt, 0);
    pluckEnv (b, 0.25f, 0);
    space (b, 0.3f, 0.45f, 7);
    b.set (P_delayFeedback, 0.55f);
    b.macro (0, "Zap", { { P_pEnvAmt, 0.2f } });
    b.macro (1, "Echo", { { P_delayMix, 0.3f } });
}
inline void fx04 (B& b)   // Dub Siren
{
    b.set (P_polyphony, 1).set (P_osc1Wave, 1).set (P_osc1Gain, 0.55f).set (P_ampA, 0.01f).set (P_ampS, 0.9f).set (P_ampR, 0.4f)
     .set (P_filterMode, 9).set (P_filterCutoff, 2500).set (P_filterRes, 3.0f).set (P_fEnvAmt, 0)
     .set (P_lfo1Wave, 1).set (P_lfo1Rate, 2.5f).set (P_lfo1Depth, 1.0f).set (P_delayFeedback, 0.65f);
    b.route (MS_Lfo1, P_osc1Semi, 0.4f);
    space (b, 0.35f, 0.5f, 7);
    b.macro (0, "Siren Depth", { { P_mod1Amt, 0.3f } });
    b.macro (1, "Echo", { { P_delayFeedback, 0.25f } });
}
inline void fx05 (B& b)   // Impact Boom
{
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.85f).set (P_osc1Oct, -1).set (P_osc2Wave, 2).set (P_osc2Gain, 0.3f)
     .set (P_ampA, 0.001f).set (P_ampD, 1.8f).set (P_ampS, 0.0f).set (P_ampR, 1.5f).set (P_pEnvAmt, 24).set (P_pEnvD, 0.25f)
     .set (P_filterCutoff, 2000).set (P_fEnvAmt, 0).set (P_wmMix, 0.3f).set (P_wmDrive, 0.5f);
    space (b, 0.5f, 0.0f, 6, 0.0f, 4.2f);
    b.macro (0, "Punch", { { P_pEnvAmt, 0.2f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}
inline void fx06 (B& b)   // Radio Static
{
    b.set (P_osc1Wave, 4).set (P_osc1Gain, 0.4f).set (P_supersawGain, 0.4f).set (P_supersawSpread, 80).set (P_supersawVoices, 9)
     .set (P_filterMode, 4).set (P_filterType, FT_BP).set (P_filterSlope, 1).set (P_filterCutoff, 1500).set (P_filterRes, 4.0f).set (P_fEnvAmt, 0)
     .set (P_wmMix, 0.6f).set (P_wmBits, 5).set (P_wmDown, 6)
     .set (P_fxOnSampler, 1).set (P_smpModel, 2).set (P_smpNoise, 0.6f).set (P_smpDrive, 0.5f)
     .set (P_lfo3Rate, 0.3f).set (P_lfo3Depth, 1.0f).set (P_filterLfoAmt, 0.3f).set (P_filterLfoSrc, 2);
    space (b, 0.25f, 0.2f, 6);
    b.macro (0, "Tuning", { { P_filterCutoff, 0.35f } });
    b.macro (1, "Static", { { P_smpNoise, 0.3f } });
}
inline void fx07 (B& b)   // Reverse Swell
{
    b.set (P_supersawGain, 0.45f).set (P_supersawVoices, 7).set (P_osc1Wave, 2).set (P_osc1Gain, 0.3f).set (P_osc1Oct, 1)
     .set (P_ampA, 0.002f).set (P_ampD, 0.6f).set (P_ampS, 0.0f).set (P_ampR, 0.4f).set (P_filterCutoff, 3000).set (P_fEnvAmt, 0)
     .set (P_reverseMix, 0.7f).set (P_reverseTime, 0.9f);
    space (b, 0.4f, 0.15f, 6, 0.0f, 3.5f);
    b.set (P_reverseMix, 0.7f);
    b.macro (0, "Reverse", { { P_reverseMix, 0.3f } });
    b.macro (1, "Length", { { P_reverseTime, 0.3f } });
}
inline void fx08 (B& b)   // Alarm Siren
{
    b.set (P_polyphony, 1).set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 1).set (P_osc2Gain, 0.35f).set (P_osc2Oct, 1)
     .set (P_ampA, 0.01f).set (P_ampS, 0.9f).set (P_ampR, 0.3f).set (P_filterCutoff, 3500).set (P_fEnvAmt, 0)
     .set (P_lfo1Wave, 3).set (P_lfo1Sync, 9).set (P_lfo1Depth, 1.0f);
    b.route (MS_Lfo1, P_osc1Semi, 0.25f); b.route (MS_Lfo1, P_osc2Semi, 0.25f);
    space (b, 0.3f, 0.3f, 6);
    b.macro (0, "Interval", { { P_mod1Amt, 0.2f }, { P_mod2Amt, 0.2f } });
    b.macro (1, "Bright", { { P_filterCutoff, 0.25f } });
}

// ======================================================================================================
// Keys & Plucks (7)
inline void ky01 (B& b)   // Rhodes Bed
{
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.55f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.3f).set (P_osc2Oct, 1)
     .set (P_fmSlot1Source, 1).set (P_fmSlot1Dest, 0).set (P_fmSlot1Amt, 120).set (P_filterCutoff, 3500).set (P_polyphony, 10)
     .set (P_lfo3Rate, 4.0f).set (P_lfo3Depth, 1.0f);
    b.route (MS_Lfo3, P_osc1Gain, 0.08f);
    pluckEnv (b, 1.2f, 0);
    b.set (P_ampS, 0.3f).set (P_ampR, 0.6f);
    space (b, 0.3f, 0.15f, 6, 0.35f);
    b.macro (0, "Bark", { { P_fmSlot1Amt, 0.2f } });
    b.macro (1, "Tremolo", { { P_mod1Amt, 0.15f } });
}
inline void ky02 (B& b)   // Ragga Skank Pluck
{
    b.set (P_osc1Wave, 1).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 0).set (P_osc2Gain, 0.3f).set (P_osc2Detune, 6)
     .set (P_filterMode, 9).set (P_filterCutoff, 1800).set (P_filterRes, 2.0f).set (P_polyphony, 8);
    pluckEnv (b, 0.15f, 3000);
    space (b, 0.25f, 0.3f, 7);
    b.macro (0, "Snap", { { P_fEnvAmt, 0.2f } });
    b.macro (1, "Length", { { P_ampD, 0.25f } });
}
inline void ky03 (B& b)   // Glass Bell
{
    // Osc 2 (silent, a 3.5:1 ratio up) frequency-modulates Osc 1; the index dies away like a struck bell
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 3).set (P_osc2Gain, 0.0f).set (P_osc2Semi, 7).set (P_osc2Oct, 1)
     .set (P_fmSlot1Source, 1).set (P_fmSlot1Dest, 0).set (P_fmSlot1Amt, 260).set (P_filterCutoff, 9000).set (P_polyphony, 10)
     .set (P_mEnv1A, 0.001f).set (P_mEnv1D, 0.8f).set (P_mEnv1S, 0.1f);
    b.route (MS_ModEnv1, P_fmSlot1Amt, 0.4f);
    pluckEnv (b, 1.4f, 0);
    space (b, 0.35f, 0.3f, 7);
    b.macro (0, "Strike", { { P_fmSlot1Amt, 0.3f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f } });
}
inline void ky04 (B& b)   // Marimba Roll
{
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.6f).set (P_resMix, 0.7f).set (P_resTuning, 2).set (P_resDecay, 0.6f).set (P_resModes, 8)
     .set (P_filterCutoff, 6000).set (P_polyphony, 10);
    pluckEnv (b, 0.08f, 0);
    space (b, 0.25f, 0.2f, 6);
    b.macro (0, "Decay", { { P_resDecay, 0.3f } });
    b.macro (1, "Body", { { P_resMix, 0.3f } });
}
inline void ky05 (B& b)   // Liquid Pluck
{
    b.set (P_osc1Wave, 0).set (P_osc1Gain, 0.5f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.35f).set (P_osc2Oct, 1)
     .set (P_filterMode, 0).set (P_filterCutoff, 900).set (P_filterRes, 2.5f).set (P_polyphony, 10);
    pluckEnv (b, 0.3f, 4500);
    space (b, 0.35f, 0.35f, 7, 0.2f);
    b.macro (0, "Snap", { { P_fEnvAmt, 0.2f } });
    b.macro (1, "Space", { { P_reverbMix, 0.3f }, { P_delayMix, 0.2f } });
}
inline void ky06 (B& b)   // Organ Bounce
{
    b.set (P_osc1Wave, 7).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 7).set (P_osc2Gain, 0.3f).set (P_osc2Oct, -1)
     .set (P_filterCutoff, 4500).set (P_polyphony, 10);
    pluckEnv (b, 0.25f, 0);
    b.set (P_ampS, 0.4f);
    space (b, 0.2f, 0.2f, 6, 0.4f);
    b.macro (0, "Drawbar", { { P_osc2Gain, 0.4f } });
    b.macro (1, "Chorus", { { P_chorusMix, 0.4f } });
}
inline void ky07 (B& b)   // Kalimba Drop
{
    b.set (P_osc1Wave, 3).set (P_osc1Gain, 0.6f).set (P_osc2Wave, 2).set (P_osc2Gain, 0.15f).set (P_osc2Oct, 2)
     .set (P_resMix, 0.5f).set (P_resTuning, 5).set (P_resDecay, 0.9f).set (P_resInharm, 0.3f).set (P_filterCutoff, 7000).set (P_polyphony, 10);
    pluckEnv (b, 0.5f, 0);
    space (b, 0.3f, 0.3f, 7);
    b.macro (0, "Metal", { { P_resInharm, 0.4f } });
    b.macro (1, "Decay", { { P_resDecay, 0.3f } });
}

} // namespace dnb

// The bank, in browser category order
struct DnbEntry { const char* name; const char* category; const char* tags; const char* description; void (*build) (PresetBuilder&); };
inline const std::vector<DnbEntry>& dnbBank()
{
    using namespace dnb;
    static const std::vector<DnbEntry> list {
        // Reese
        { "Amen Brother Reese", "Reese", "reese beats locked rolling", "Two-voice Reese locked to half notes, a synced notch and a sine sub, through a ladder.", rs01 },
        { "Rolling Quarter Reese", "Reese", "reese beats rolling wide", "Three voices beating every quarter note (x3 locked), wide above 120 Hz.", rs02 },
        { "Dark Matter Reese", "Reese", "reese dark heavy golden", "Four voices at the golden ratio (never quite repeats), driven and darkened.", rs03 },
        { "Tech Reese Growl", "Reese", "reese tech growl neuro", "Fast Phase Twist growl, 1/8 beating and a moving notch. Macro 5 sets the growl rate.", rs04 },
        { "Liquid Reese", "Reese", "reese liquid smooth", "Smooth Hz-detuned Reese (1.2 Hz on every note) for liquid rollers.", rs05 },
        { "Steppa Square Reese", "Reese", "reese square steppa halftime", "Square-wave Reese beating once a bar, with a heavy sub.", rs06 },
        { "Blend Reese 174", "Reese", "reese blend dotted", "Saw/square blend, four voices x5, dotted-half beating and a synced notch.", rs07 },
        { "Reese Fold Stack", "Reese", "reese fold neuro 16th", "Reese plus an octave-down saw through the wave folder, beating every 16th.", rs08 },
        { "Ghost Reese", "Reese", "reese subtle notch", "Gentle cents-detuned Reese with a deep free-running notch.", rs09 },
        { "Rotating Reese", "Reese", "reese wide rotation", "Three voices circling the stereo field (mono under 120 Hz).", rs10 },
        { "Classic 94 Reese", "Reese", "reese classic oldskool 94", "The old way: two detuned saws and a sub, a ladder, and a sweeping notch.", rs11 },
        { "Kick-Locked Reese", "Reese", "reese break-react kick", "Break React: the Reese restarts and kicks up on each kick (load a break, or play C0).", rs12 },
        { "Hoover Reese Hybrid", "Reese", "reese hoover hybrid", "Square Reese with a scooping supersaw an octave down.", rs13 },
        { "Sub Heavy Reese", "Reese", "reese sub heavy", "A dark Reese that's mostly sub, for big systems.", rs14 },
        { "Halftime Wide Reese", "Reese", "reese halftime wide ott", "One beat per bar, slow rotation, plenty of multiband.", rs15 },
        // Sub & Bass
        { "Pure Sine Sub", "Sub & Bass", "sub sine clean", "A clean sine sub. Macro 1 adds harmonics for small speakers.", sb01 },
        { "808 Boom", "Sub & Bass", "sub 808 boom", "Sine with a pitch drop at the start and a long tail.", sb02 },
        { "Triangle Rumble", "Sub & Bass", "sub triangle warm", "Triangle sub with a touch of fold.", sb03 },
        { "Square Sub Steppa", "Sub & Bass", "sub square steppa", "Filtered square sub with a sine underneath.", sb04 },
        { "Warm Saw Bass", "Sub & Bass", "bass saw warm", "Saw and sine through a fat Moog-style filter with a short envelope.", sb05 },
        { "Jump Up Wobble Sub", "Sub & Bass", "bass wobble jumpup", "A 1/8 wobble locked to the song.", sb06 },
        { "Dub Pressure Sub", "Sub & Bass", "sub dub pump", "Sine sub pumping on the quarter notes.", sb07 },
        { "Plucky Sub", "Sub & Bass", "bass pluck short", "Short plucked sub for rolling basslines.", sb08 },
        { "Ragga Bass", "Sub & Bass", "bass ragga glide", "Square-and-sine ragga bass with a little glide.", sb09 },
        { "Kick-Locked Sub", "Sub & Bass", "sub break-react kick", "Break React: the sub's phase resets and kicks on each kick (load a break, or play C0).", sb10 },
        { "Bubble Bass", "Sub & Bass", "bass bubble resonant", "Resonant band-pass blip on every note.", sb11 },
        { "Rollers Sub", "Sub & Bass", "sub rollers glide", "Gliding sine-and-triangle sub for rollers.", sb12 },
        // Neuro
        { "Fold Machine", "Neuro", "neuro fold sequence", "Wave folder and a 16-step DNA sequence moving the filter.", ne01 },
        { "Comb Growl", "Neuro", "neuro comb growl", "Comb character filter wobbling at 1/8.", ne02 },
        { "Formant Talker", "Neuro", "neuro formant talk", "Vowel-morph filter swept every quarter note.", ne03 },
        { "FM Snarl", "Neuro", "neuro fm snarl", "Complex-oscillator FM with an envelope on the index.", ne04 },
        { "Ring Mod Neuro", "Neuro", "neuro ring metallic", "Audio-rate ring modulation with a 1/16 filter wobble.", ne05 },
        { "Bit Grinder", "Neuro", "neuro bitcrush grind", "Bit-crushed and down-sampled, wobbling in 1/8 triplets.", ne06 },
        { "Phaser Wub", "Neuro", "neuro wub phaser", "The drawn wub shape on the cutoff, through a phaser.", ne07 },
        { "Spectral Neuro", "Neuro", "neuro spectral shift", "Spectral shift and scramble on detuned saws.", ne08 },
        { "Neuro Reese Sequence", "Neuro", "neuro reese sequence", "Folded Reese with a DNA sequence on the filter.", ne09 },
        { "Resampled Mid", "Neuro", "neuro resample crunch", "Folded mid bass resampled through the E-mu.", ne10 },
        { "Tearout Squeal", "Neuro", "neuro tearout squeal", "Screaming comb resonance moving in 16ths.", ne11 },
        { "Robot Voice", "Neuro", "neuro robot formant", "Formant filter stepped by an 8-step sequence.", ne12 },
        // Hoover & Rave
        { "Dominator 92", "Hoover & Rave", "hoover rave 92 scoop", "Deep pitch-scooping hoover through a flanger.", hv01 },
        { "Mentasm Stab", "Hoover & Rave", "rave mentasm stab", "Three detuned squares with a pitch dip.", hv02 },
        { "Rave Organ", "Hoover & Rave", "rave organ stab", "Bouncy rave organ.", hv03 },
        { "Hoover Glide Lead", "Hoover & Rave", "hoover lead glide", "Mono hoover with a long glide (mod wheel deepens the flanger).", hv04 },
        { "Belgian Hoover", "Hoover & Rave", "hoover belgian rise", "Wide hoover rising into each note, through-zero flanged.", hv05 },
        { "Alarm Hoover", "Hoover & Rave", "hoover alarm rave", "Hoover jumping in 1/8s like an alarm.", hv06 },
        { "Hardcore Hoover Chords", "Hoover & Rave", "hoover chords hardcore", "One key plays a minor 7 hoover chord with a scoop.", hv07 },
        { "Arp Rave Pulse", "Hoover & Rave", "rave arp pulse", "Two-octave 1/16 arpeggio with accents.", hv08 },
        // Stabs & Chords
        { "Minor 9 Stab", "Stabs & Chords", "stab minor9 sampler", "One key plays a minor 9 through an MS-20 and an S950.", st01 },
        { "Dub Chord Delay", "Stabs & Chords", "chord dub delay", "Minor 7 stab into a dotted-eighth echo.", st02 },
        { "Open Rave Stab", "Stabs & Chords", "stab rave open", "The open rave minor voicing through an Oberheim filter.", st03 },
        { "Sus Skank", "Stabs & Chords", "stab sus skank short", "Short sus2 skank for the offbeats.", st04 },
        { "Power Stab", "Stabs & Chords", "stab power distorted", "Driven power-chord stab.", st05 },
        { "Glass Major 7", "Stabs & Chords", "chord major7 glass fm", "Glassy FM major 7 for liquid tunes.", st06 },
        { "Jungle Horn Stab", "Stabs & Chords", "stab horn brass", "Brassy minor 7 stab through the TB-303 filter.", st07 },
        { "Stutter Chord", "Stabs & Chords", "chord stutter repeat", "Minor 7 chord with random Beat Repeats.", st08 },
        { "Octave Sampler Stab", "Stabs & Chords", "stab octave sp1200", "Octave stab crunched by the SP-1200.", st09 },
        { "Reese Chord Stab", "Stabs & Chords", "chord reese stab", "Reese-engine minor 7 stab.", st10 },
        // Pads & Atmos
        { "Liquid Strings", "Pads & Atmos", "pad strings liquid", "Soft supersaw strings with chorus and a big reverb.", pd01 },
        { "Dark Atmos", "Pads & Atmos", "atmos dark grain", "Dark detuned pad with grains.", pd02 },
        { "Shimmer Heaven", "Pads & Atmos", "pad shimmer bright", "Octave pad into the shimmer reverb.", pd03 },
        { "Granular Haze", "Pads & Atmos", "atmos granular haze", "Reversing grain haze over a soft pad.", pd04 },
        { "Spectral Choir Pad", "Pads & Atmos", "pad choir spectral", "Formant choir blurred by the spectral stage.", pd05 },
        { "Rainforest Pad", "Pads & Atmos", "pad rainforest dust", "Drifting pulse pad with sampler dust.", pd06 },
        { "Amen Dream Pad", "Pads & Atmos", "pad dream phaser", "Slow-phased supersaw pad.", pd07 },
        { "Detroit Strings", "Pads & Atmos", "pad strings ensemble", "Three detuned saws in an ensemble.", pd08 },
        { "Ambient Reese Pad", "Pads & Atmos", "pad reese ambient", "Reese beating over four bars, slowly rotating.", pd09 },
        { "Sub Drone", "Pads & Atmos", "atmos drone fold", "Sine drone with a slowly moving fold.", pd10 },
        // Leads
        { "Pirate Square", "Leads", "lead square pirate", "Square lead through an MS-20 with a dotted echo.", ld01 },
        { "Sine Whistle", "Leads", "lead sine whistle", "Whistling sine lead with vibrato.", ld02 },
        { "Acid 303 Lead", "Leads", "lead acid 303", "TB-303 acid line: macros for cutoff, env mod and accent.", ld03 },
        { "Saw Lead 95", "Leads", "lead saw 95", "Five-voice saw lead.", ld04 },
        { "Ragga Horn Lead", "Leads", "lead horn ragga", "Brassy pulse lead.", ld05 },
        { "SP Crunch Lead", "Leads", "lead crunch sp1200", "Square lead crunched by the SP-1200.", ld06 },
        { "FM Lead", "Leads", "lead fm bright", "Bright FM lead with an envelope on the index.", ld07 },
        { "Formant Lead", "Leads", "lead formant vowel", "Talking vowel lead.", ld08 },
        // Breaks & Drums
        { "Original Break 170", "Breaks & Drums", "break 170 original", "The demo break as played, then ghost notes; Akai Cyclic to the song.", bk01 },
        { "Two-Step Roller", "Breaks & Drums", "break 2step 174", "The 2-step demo break with a two-step edit.", bk02 },
        { "Rollers 165 Swing", "Breaks & Drums", "break rollers swing", "The rollers demo break, swung, Clean stretch.", bk03 },
        { "Chopped Frankenbreak", "Breaks & Drums", "break frankenbreak chop", "Two Frankenbreak edits chained AABA.", bk04 },
        { "Reverse Edits", "Breaks & Drums", "break reverse swell", "Original, then reverse swells every fourth bar.", bk05 },
        { "Halftime Break", "Breaks & Drums", "break halftime", "Halftime edit of the 2-step break.", bk06 },
        { "SP Crunch Break", "Breaks & Drums", "break sp1200 crunch", "Jungle edit through the SP-1200, snare build on B.", bk07 },
        { "Break Keys 174", "Breaks & Drums", "break keys play", "The 2-step break's slices on keys (C1 up), no sequencer.", bk08 },
        { "Fill Machine", "Breaks & Drums", "break fill auto", "An Auto Fill every 4 bars.", bk09 },
        { "Akai Cyclic Stretch", "Breaks & Drums", "break akai stretch", "Long-cycle Akai stretch with a stutter edit.", bk10 },
        // FX & Risers
        { "Noise Riser", "FX & Risers", "riser noise build", "Hold it: a 3.5-second rise in pitch and brightness.", fx01 },
        { "Downlifter", "FX & Risers", "fx downlifter drop", "Two-octave drop over two seconds.", fx02 },
        { "Laser Zap", "FX & Risers", "fx laser zap", "Sine zap with a triplet echo.", fx03 },
        { "Dub Siren", "FX & Risers", "fx siren dub", "Classic dub siren into a long echo.", fx04 },
        { "Impact Boom", "FX & Risers", "fx impact boom", "Pitch-dropping sine boom in a big room.", fx05 },
        { "Radio Static", "FX & Risers", "fx radio static", "Tuning-in-the-pirate-station noise.", fx06 },
        { "Reverse Swell", "FX & Risers", "fx reverse swell", "Short hit drowned in reverse reverb.", fx07 },
        { "Alarm Siren", "FX & Risers", "fx alarm siren", "Two-tone alarm in quarter notes.", fx08 },
        // Keys & Plucks
        { "Rhodes Bed", "Keys & Plucks", "keys rhodes liquid", "Electric-piano-ish keys with tremolo.", ky01 },
        { "Ragga Skank Pluck", "Keys & Plucks", "pluck skank ragga", "Short skank pluck for the offbeats.", ky02 },
        { "Glass Bell", "Keys & Plucks", "keys bell glass fm", "FM bell: the strike fades like the real thing.", ky03 },
        { "Marimba Roll", "Keys & Plucks", "pluck marimba resonator", "A click exciting the marimba resonator.", ky04 },
        { "Liquid Pluck", "Keys & Plucks", "pluck liquid delay", "Bright filter pluck into delay and reverb.", ky05 },
        { "Organ Bounce", "Keys & Plucks", "keys organ bounce", "Bouncy organ with chorus.", ky06 },
        { "Kalimba Drop", "Keys & Plucks", "pluck kalimba bell", "Sine pluck through an inharmonic bell resonator.", ky07 },
    };
    return list;
}

} // namespace tg
