#pragma once
// Parameter table for Mega Synth.
// Parameter IDs match the control ids of the original browser synth so that
// patches exported from the browser version can be imported directly.

namespace tg
{

struct ChoiceList
{
    const char* const* keys;   // values used by the browser patch JSON
    const char* const* labels; // what the user sees
    int size;
};

// ---------------------------------------------------------------- choice lists
inline const char* const kWaveKeys[]   = { "sawtooth","square","triangle","sine","pulse25","pulse12","trapezoid","organ","oddsine","sinefold" };
inline const char* const kWaveLabels[] = { "Sawtooth","Square","Triangle","Sine","Pulse 25%","Pulse 12.5%","Trapezoid","Organ","Odd Sine","Sine Fold" };
inline const char* const kWaveBKeys[]   = { "sine","sawtooth","square","triangle","pulse25","pulse12","trapezoid","organ","oddsine","sinefold" };
inline const char* const kWaveBLabels[] = { "Sine","Sawtooth","Square","Triangle","Pulse 25%","Pulse 12.5%","Trapezoid","Organ","Odd Sine","Sine Fold" };
inline const char* const kSubKeys[]   = { "square","sine","triangle","pulse25","sawtooth" };
inline const char* const kSubLabels[] = { "Square","Sine","Triangle","Pulse 25%","Sawtooth" };
inline const char* const kLoopKeys[]   = { "full","window","manual","oneshot" };
inline const char* const kLoopLabels[] = { "Full Buffer Loop","Scan Window Loop","Manual Loop Start/End","One Shot" };
inline const char* const kDirKeys[]   = { "forward","reverse" };
inline const char* const kDirLabels[] = { "Forward","Reverse" };
inline const char* const kOnOffKeys[]   = { "on","off" };
inline const char* const kOnOffLabels[] = { "On","Off" };
inline const char* const kOscKeys[]   = { "osc1","osc2","osc3","osc4","complex","supersaw","master" };
inline const char* const kOscLabels[] = { "Osc1","Osc2","Osc3","Osc4 WT","Osc5 Complex","Osc6 SuperSaw","Master Dry Mix" };
inline const char* const kFilterKeys[]   = { "lp","sallenkey","ladder","steiner","svf","ota","polivoks","swcap","acid","ms20","oberheim","sem","tb303","moogfat","arpodyssey","cs15","sh2" };
inline const char* const kFilterLabels[] = { "Standard LP","Sallen-Key LP","Ladder","Steiner-Parker","State Variable","Discrete OTA","Polivoks","Switched Capacitor","Acid Filter Approx","MS-20","Oberheim","SEM","TB-303","Minimoog - Fat Cat","Arp Odyssey","CS-15","SH-2" };
inline const char* const kDelaySyncKeys[]   = { "off","2bar","1bar","1/2","1/4","1/8","1/8d","1/8t","1/16","1/16d","1/16t","1/32" };
inline const char* const kDelaySyncLabels[] = { "Off (Manual)","2 Bars","1 Bar","1/2","1/4","1/8","1/8 Dotted","1/8 Triplet","1/16","1/16 Dotted","1/16 Triplet","1/32" };
inline const char* const kFlutterSyncKeys[]   = { "off","1bar","1/2","1/4","1/8","1/8t","1/16" };
inline const char* const kFlutterSyncLabels[] = { "Off (Manual)","1 Bar","1/2","1/4","1/8","1/8 Triplet","1/16" };
inline const char* const kReverbSyncKeys[]   = { "off","1/4","1/2","1bar","2bar","4bar","1/2d","1/2t" };
inline const char* const kReverbSyncLabels[] = { "Off (Manual)","1/4","1/2","1 Bar","2 Bars","4 Bars","1/2 Dotted","1/2 Triplet" };
inline const char* const kModSyncKeys[]   = { "off","1bar","1/2","1/4","1/8","1/8d","1/8t","1/16" };
inline const char* const kModSyncLabels[] = { "Off (Manual)","1 Bar","1/2","1/4","1/8","1/8 Dotted","1/8 Triplet","1/16" };
inline const char* const kLfoWaveKeys[]   = { "sine","triangle","sawtooth","square" };
inline const char* const kLfoWaveLabels[] = { "Sine","Triangle","Sawtooth","Square" };
inline const char* const kNoteKeys[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
inline const char* const kScaleKeys[]   = { "major","minor","harmonicminor","melodicminor","dorian","phrygian","mixolydian","pentminor","pentmajor","blues","chromatic" };
inline const char* const kScaleLabels[] = { "Major","Minor","Harmonic Minor","Melodic Minor","Dorian","Phrygian","Mixolydian","Minor Pentatonic","Major Pentatonic","Blues","Chromatic" };
inline const char* const kGenKeys[]   = { "phrase","euclidean" };
inline const char* const kGenLabels[] = { "Phrase","Euclidean" };
inline const char* const kClockKeys[]   = { "internal","host" };
inline const char* const kClockLabels[] = { "Internal Tempo","Host Transport" };
inline const char* const kLfoSrcKeys[]   = { "lfo1","lfo2","lfo3" };
inline const char* const kLfoSrcLabels[] = { "LFO1","LFO2","LFO3" };
inline const char* const kArModKeys[]   = { "sine","osc1","osc2","osc3","sub","noise" };
inline const char* const kArModLabels[] = { "Sine (ratio)","Osc 1","Osc 2","Osc 3","Sub","Noise" };
inline const char* const kArFmTKeys[]   = { "osc123sub","osc1","osc2","osc3","all" };
inline const char* const kArFmTLabels[] = { "Osc 1-3 + Sub","Osc 1","Osc 2","Osc 3","Every oscillator" };
inline const char* const kDnaModeKeys[]   = { "waveform","crossover","harmonic","spectral","transient","ampdna","morph" };
inline const char* const kDnaModeLabels[] = { "Waveform Splice","Crossover","Harmonic","Spectral","Transient / Body","Amplitude DNA","Morph / Gene Shuffle" };
inline const char* const kDnaSrcKeys[]   = { "osc1","osc2","osc3","sub","wt1","wt2","complex","supersaw" };
inline const char* const kDnaSrcLabels[] = { "Osc 1","Osc 2","Osc 3","Sub","WT 1","WT 2","Complex","SuperSaw" };
inline const char* const kResTuneKeys[]   = { "harmonic","odd","bar","membrane","plate","bell" };
inline const char* const kResTuneLabels[] = { "Harmonic (string)","Odd (tube)","Bar (marimba)","Membrane (drum)","Plate","Bell" };
inline const char* const kSpSizeKeys[]   = { "512","1024","2048","4096" };
inline const char* const kSpSizeLabels[] = { "512 (fast, 11 ms)","1024","2048","4096 (smooth, 85 ms)" };
inline const char* const kBusOrderKeys[]   = { "grain_spectral","spectral_grain" };
inline const char* const kBusOrderLabels[] = { "Granular > Spectral","Spectral > Granular" };
inline const char* const kCapPointKeys[]   = { "prefx","postfx" };
inline const char* const kCapPointLabels[] = { "Before effects","After effects (what you hear)" };
inline const char* const kDsRateKeys[]   = { "1/32","1/16t","1/16","1/8t","1/8","1/4","1/2","1bar","free" };
inline const char* const kDsRateLabels[] = { "1/32","1/16 Triplet","1/16","1/8 Triplet","1/8","1/4","1/2","1 Bar","Free (Hz)" };
inline const char* const kQualityKeys[]   = { "eco","normal","high" };
inline const char* const kQualityLabels[] = { "Eco (lower CPU)","Normal","High" };
inline const char* const kFilterTypeKeys[]   = { "lp","hp","bp","notch","peak","ap" };
inline const char* const kFilterTypeLabels[] = { "Low Pass","High Pass","Band Pass","Notch","Peak / Bell","All Pass" };
inline const char* const kFilterSlopeKeys[]   = { "6","12","18","24" };
inline const char* const kFilterSlopeLabels[] = { "6 dB/oct","12 dB/oct","18 dB/oct","24 dB/oct" };
inline const char* const kDsSyncKeys[]   = { "host","internal","free" };
inline const char* const kDsSyncLabels[] = { "Host (DAW)","Internal","Free (Hz)" };
inline const char* const kDsRestartKeys[]   = { "never","note","bar","hoststart" };
inline const char* const kDsRestartLabels[] = { "Never (free running)","Each note","Each bar","Host start" };
inline const char* const kDsGridKeys[]   = { "1/16","1/8" };
inline const char* const kDsDirKeys[]   = { "forward","reverse","pingpong","random" };
inline const char* const kDsDirLabels[] = { "Forward","Reverse","Ping-pong","Random" };
inline const char* const kDsPatKeys[]   = { "A","B","C","D" };
inline const char* const kDsMidiKeys[]   = { "off","0","12","24" };
inline const char* const kDsMidiLabels[] = { "Off","MIDI 0-3 (C-2 to D#-2)","MIDI 12-15 (C-1 to D#-1)","MIDI 24-27 (C0 to D#0)" };
inline const char* const kSmpModelKeys[]   = { "sp1200","s950","emu","free" };
inline const char* const kSmpModelLabels[] = { "SP-1200 (12-bit, 26 kHz)","Akai S950 (12-bit, variable)","E-mu (12-bit, 27.8 kHz)","Free" };
inline const char* const kRptLenKeys[]   = { "1/4","1/8","1/16","1/32" };
inline const char* const kRptDurKeys[]   = { "1/2beat","1beat","2beats","1bar" };
inline const char* const kRptDurLabels[] = { "1/2 beat","1 beat","2 beats","1 bar" };
inline const char* const kRptMidiKeys[]   = { "off","4","16","28" };
inline const char* const kRptMidiLabels[] = { "Off","MIDI 4 (E-2)","MIDI 16 (E-1)","MIDI 28 (E0)" };
inline const char* const kFlpModeKeys[]   = { "flanger","phaser" };
inline const char* const kFlpModeLabels[] = { "Flanger","Phaser" };
inline const char* const kFlpStageKeys[]   = { "4","8","12" };
inline const char* const kFlpStageLabels[] = { "4 stages","8 stages","12 stages" };
inline const char* const kVshRateKeys[]   = { "1/4","1/2","1bar" };
inline const char* const kVshRateLabels[] = { "1/4","1/2","1 Bar" };
inline const char* const kVshTrigKeys[]   = { "beat","note","dna" };
inline const char* const kVshTrigLabels[] = { "Locked to the beat","Each note","DNA step gate" };
inline const char* const kFilterRouteKeys[]   = { "serial","parallel" };
inline const char* const kFilterRouteLabels[] = { "Serial (1 > 2)","Parallel (1 + 2)" };
inline const char* const kFilterLfoKeys[]   = { "lfo1","lfo2","lfo3","lfo4" };
inline const char* const kFilterLfoLabels[] = { "LFO 1","LFO 2","LFO 3","LFO 4" };
// LFO sync (Off = the Rate knob in Hz), retrigger and mode
inline const char* const kLfoSyncKeys[]   = { "off","1/32","1/16t","1/16","1/16d","1/8t","1/8","1/8d","1/4t","1/4","1/4d","1/2t","1/2","1/2d","1bar","2bar","4bar" };
inline const char* const kLfoSyncLabels[] = { "Off (Hz)","1/32","1/16 Triplet","1/16","1/16 Dotted","1/8 Triplet","1/8","1/8 Dotted","1/4 Triplet","1/4","1/4 Dotted",
                                              "1/2 Triplet","1/2","1/2 Dotted","1 Bar","2 Bars","4 Bars" };
inline const char* const kLfoRetrigKeys[]   = { "note","free","bar","hoststart" };
inline const char* const kLfoRetrigLabels[] = { "Each note","Free (shared)","Each bar","Host start" };
inline const char* const kLfoModeKeys[]   = { "loop","oneshot" };
inline const char* const kLfoModeLabels[] = { "Loop","One-shot" };
// Chord memory and arpeggiator
inline const char* const kChdPresetKeys[]   = { "learned","min7","maj7","min9","sus2","sus4","power","raveminor","octaves" };
inline const char* const kChdPresetLabels[] = { "Learned chord","Minor 7","Major 7","Minor 9","Sus 2","Sus 4","Power (5th)","Rave minor (open 1 5 b3 b7 9)","Octaves" };
inline const char* const kArpModeKeys[]   = { "up","down","updown","downup","played","random","chord" };
inline const char* const kArpModeLabels[] = { "Up","Down","Up / Down","Down / Up","As played","Random","Chord" };
inline const char* const kArpRateKeys[]   = { "1/32","1/16t","1/16","1/16d","1/8t","1/8","1/8d","1/4t","1/4" };
inline const char* const kArpRateLabels[] = { "1/32","1/16 Triplet","1/16","1/16 Dotted","1/8 Triplet","1/8","1/8 Dotted","1/4 Triplet","1/4" };
inline const char* const kArpVelKeys[]   = { "played","fixed" };
inline const char* const kArpVelLabels[] = { "As played","Fixed" };
inline const char* const kArpRestartKeys[]   = { "first","bar" };
inline const char* const kArpRestartLabels[] = { "First note","Each bar" };
inline const char* const kUniCurveKeys[]   = { "linear","supersaw" };
inline const char* const kUniCurveLabels[] = { "Linear","SuperSaw (outer voices wider)" };
inline const char* const kPEnvTargetKeys[]   = { "all","osc1","osc2","osc3","sub","wt1","wt2","complex","supersaw" };
inline const char* const kPEnvTargetLabels[] = { "All oscillators","Osc 1","Osc 2","Osc 3","Sub","Wavetable 1","Wavetable 2","Complex","SuperSaw" };
inline const char* const kMfTypeKeys[]   = { "lp","hp","bp","notch" };
inline const char* const kCharModelKeys[]   = { "off","formant","formant2","allpass","powerlp","powerbp","waveshaper","resonator","resampler","lpresample","shifter","analoguelp","dj","comb","vowelmorph","phaser","ring","decimatorlp","dualpeak" };
inline const char* const kCharModelLabels[] = { "Off (Model)","Formant","Formant 2","All Pass","Power LP","Power BP","Wave Shaper","Resonator","Resampler","LP + Resample","Shifter","Analogue LP","DJ Filter","Comb +/-","Vowel Morph","Phaser Filter","Ring Filter","Decimator LP","Dual Peak" };
inline const char* const kMfTypeLabels[] = { "Low Pass","High Pass","Band Pass","Notch" };
inline const char* const kMfSlopeKeys[]   = { "12","24" };
inline const char* const kMfSlopeLabels[] = { "12 dB/oct","24 dB/oct" };
// Performance FX (state version 14)
inline const char* const kPfQuantKeys[]   = { "off","1/16","1/8","1/4","1/2","bar" };
inline const char* const kPfQuantLabels[] = { "Off (instant)","1/16","1/8","1/4","1/2","1 bar" };
inline const char* const kPfRollKeys[]    = { "1/4","1/8","1/16","1/32","1/8T","1/16T" };
inline const char* const kPfRollLabels[]  = { "1/4","1/8","1/16","1/32","1/8 triplet","1/16 triplet" };
inline const char* const kPfStutKeys[]    = { "1/16","1/32","1/64" };
inline const char* const kPfStutLabels[]  = { "1/16","1/32","1/64" };
inline const char* const kPfGateKeys[]    = { "8ths","offbeat","trance1","trance2","dotted","gallop","half","build" };
inline const char* const kPfGateLabels[]  = { "Straight 8ths","Offbeat","Trance 1","Trance 2","Dotted","Gallop","Half bar","Build" };
inline const char* const kPfEchoKeys[]    = { "1/16","1/8","3/16","1/4","1/2" };
inline const char* const kPfEchoLabels[]  = { "1/16","1/8","3/16 (dotted 8th)","1/4","1/2" };
// Granular Synth (state version 15)
inline const char* const kGsShapeKeys[]   = { "hann","triangle","tukey","perc","gauss" };
inline const char* const kGsShapeLabels[] = { "Hann (smooth)","Triangle","Flat top","Percussive","Gaussian" };
inline const char* const kGsDnaKeys[]     = { "off","a","b" };
inline const char* const kGsDnaLabels[]   = { "Off","Replaces DNA Source A","Replaces DNA Source B" };
// Break Chopper and Break Sequencer (state version 16)
inline const char* const kBcBarsKeys[]    = { "auto","1","2","4","8","16" };
inline const char* const kBcBarsLabels[]  = { "Auto","1 bar","2 bars","4 bars","8 bars","16 bars" };
inline const char* const kBcSliceKeys[]   = { "transient","eq4","eq8","eq16","eq32","manual" };
inline const char* const kBcSliceLabels[] = { "Transients","Equal 4","Equal 8","Equal 16","Equal 32","Manual markers" };
inline const char* const kBcStretchKeys[]   = { "repitch","cyclic","clean" };
inline const char* const kBcStretchLabels[] = { "Repitch","Akai Cyclic","Clean (WSOLA)" };
inline const char* const kBcColourKeys[]   = { "off","sp1200","s950","emu","12bit" };
inline const char* const kBcColourLabels[] = { "Off","SP-1200","Akai S950","E-mu","12-bit" };
inline const char* const kBsPatKeys[]    = { "A","B","C","D","E","F","G","H" };
inline const char* const kBsPatLabels[]  = { "A","B","C","D","E","F","G","H" };
inline const char* const kBsStepsKeys[]   = { "16","32" };
inline const char* const kBsStepsLabels[] = { "16 steps","32 steps" };
inline const char* const kBsSwitchKeys[]   = { "step","beat","bar" };
inline const char* const kBsSwitchLabels[] = { "Next step","Next beat","Next bar" };
inline const char* const kBsKeysKeys[]   = { "off","24","72","84","96" };
inline const char* const kBsKeysLabels[] = { "Off","MIDI 24-31 (C0-G0)","MIDI 72-79 (C4-G4)","MIDI 84-91 (C5-G5)","MIDI 96-103 (C6-G6)" };
inline const char* const kBsAltKeys[]   = { "off","1bar","2bar","4bar" };
inline const char* const kBsAltLabels[] = { "Off","Every other bar","Every other 2 bars","Every other 4 bars" };
inline const char* const kBsAltBreakKeys[]   = { "1","2","3" };
inline const char* const kBsAltBreakLabels[] = { "Break 1","Break 2","Break 3" };
inline const char* const kBsToolSrcKeys[]   = { "active","all" };
inline const char* const kBsToolSrcLabels[] = { "From the Active Break","From all three breaks" };
inline const char* const kBsFillModeKeys[]   = { "oneshot","held" };
inline const char* const kBsFillModeLabels[] = { "One-shot","Held" };
inline const char* const kBsFillLenKeys[]    = { "beat","2beats","bar","2bars" };
inline const char* const kBsFillLenLabels[]  = { "1 beat","2 beats","1 bar","2 bars" };
inline const char* const kBsFillStartKeys[]   = { "beat","bar","end" };
inline const char* const kBsFillStartLabels[] = { "Next beat","Next bar","End of pattern" };
inline const char* const kBsFillRetKeys[]   = { "carry","restart" };
inline const char* const kBsFillRetLabels[] = { "Pattern carries on","Restart the pattern" };
inline const char* const kBsAutoFillKeys[]   = { "off","4","8","16" };
inline const char* const kBsAutoFillLabels[] = { "Off","Every 4 bars","Every 8 bars","Every 16 bars" };
inline const char* const kBcSnapKeys[]   = { "none","zero","transient","g8","g16","g32" };
inline const char* const kBcSnapLabels[] = { "Snap: none","Snap: zero crossing","Snap: transient","Snap: grid 1/8","Snap: grid 1/16","Snap: grid 1/32" };
inline const char* const kPfKeysKeys[]    = { "off","36","48","60","72","84","96" };
inline const char* const kPfKeysLabels[]  = { "Off","MIDI 36-48 (C1-C2)","MIDI 48-60 (C2-C3)","MIDI 60-72 (C3-C4)","MIDI 72-84 (C4-C5)","MIDI 84-96 (C5-C6)","MIDI 96-108 (C6-C7)" };
inline const char* const kEnvSrcKeys[]   = { "env1","env2","env3" };
inline const char* const kEnvSrcLabels[] = { "ENV1","ENV2","ENV3" };

// Modulation targets (shared by LFO and envelope assignment slots)
#define TG_MOD_TARGETS(X) \
    X(none,          "None",              5000,  0) \
    X(pitch,         "Pitch (cents)",     1200,  30) \
    X(cutoff,        "Filter Cutoff",     8000,  2500) \
    X(resonance,     "Resonance",         20,    2) \
    X(osc1Gain,      "Osc1 Level",        1,     0.2) \
    X(osc2Gain,      "Osc2 Level",        1,     0.2) \
    X(osc3Gain,      "Osc3 Level",        1,     0.2) \
    X(subGain,       "Sub Level",         1,     0.2) \
    X(osc4Gain,      "Osc4 Level",        1,     0.2) \
    X(complexGain,   "Complex Level",     1,     0.2) \
    X(supersawGain,  "SuperSaw Level",    1,     0.2) \
    X(fmAmount,      "FM Amount",         1500,  300) \
    X(ringMix,       "Ring Mix",          1,     0.35) \
    X(drive,         "Drive",             20,    5) \
    X(amp,           "Amplitude",         1,     0.4) \
    X(delayMix,      "Delay Mix",         1,     0.1) \
    X(reverbMix,     "Reverb Mix",        1,     0.1) \
    X(shimmerMix,    "Shimmer Mix",       1,     0.1) \
    X(chorusMix,     "Chorus Mix",        1,     0.1) \
    X(reverseMix,    "Reverse Reverb Mix",1,     0.1) \
    X(osc4Detune,    "Osc4 Detune",       50,    12) \
    X(osc4Position,  "WT Scan Position",  1,     0.15) \
    X(osc4Window,    "WT Scan Window",    1,     0.15) \
    X(osc4LoopStart, "WT Loop Start",     1,     0.15) \
    X(osc4LoopEnd,   "WT Loop End",       1,     0.15) \
    X(complexDetune, "Complex Detune",    50,    12) \
    X(complexRatio,  "Complex Ratio",     4,     0.5) \
    X(complexFm,     "Complex FM",        1500,  220) \
    X(complexShape,  "Complex Shape",     24,    4) \
    X(complexMix,    "Complex Mix",       1,     0.2) \
    X(supersawDetune,"SuperSaw Detune",   50,    12) \
    X(supersawVoices,"SuperSaw Voices",   7,     1) \
    X(supersawSpread,"SuperSaw Spread",   80,    12) \
    X(supersawStereo,"SuperSaw Stereo",   1,     0.2) \
    X(supersawDrift, "SuperSaw Drift",    20,    2) \
    X(ringGain,      "Ring Gain",         1,     0.2) \
    X(delayTime,     "Delay Time",        0.85,  0.08) \
    X(delayFeedback, "Delay Feedback",    0.9,   0.12) \
    X(tapeTone,      "Tape Tone",         8000,  1200) \
    X(tapeFlutter,   "Tape Flutter",      0.02,  0.002) \
    X(reverbSize,    "Reverb Size",       4,     0.5) \
    X(reverbTone,    "Reverb Tone",       10000, 1200) \
    X(shimmerBright, "Shimmer Brightness",10000, 1200) \
    X(chorusRate,    "Chorus Rate",       3,     0.25) \
    X(chorusDepth,   "Chorus Depth",      0.015, 0.002) \
    X(reverseTime,   "Reverse Time",      1.5,   0.12) \
    X(reversePitch,  "Reverse Pitch",     0.03,  0.004) \
    X(wt2Gain,       "WT2 Level",         1,     0.2) \
    X(wt2Detune,     "WT2 Detune",        50,    12) \
    X(wt2Position,   "WT2 Scan Position", 1,     0.15) \
    X(wt2Window,     "WT2 Scan Window",   1,     0.15) \
    X(wt2LoopStart,  "WT2 Loop Start",    1,     0.15) \
    X(wt2LoopEnd,    "WT2 Loop End",      1,     0.15)

enum ModTarget
{
#define TG_X(key, label, range, def) MT_##key,
    TG_MOD_TARGETS(TG_X)
#undef TG_X
    MT_COUNT
};

inline const char* const kTargetKeys[] = {
#define TG_X(key, label, range, def) #key,
    TG_MOD_TARGETS(TG_X)
#undef TG_X
};
inline const char* const kTargetLabels[] = {
#define TG_X(key, label, range, def) label,
    TG_MOD_TARGETS(TG_X)
#undef TG_X
};
inline const float kTargetRange[] = {
#define TG_X(key, label, range, def) (float) (range),
    TG_MOD_TARGETS(TG_X)
#undef TG_X
};
inline const float kTargetDefault[] = {
#define TG_X(key, label, range, def) (float) (def),
    TG_MOD_TARGETS(TG_X)
#undef TG_X
};

#define TG_LIST(name) ChoiceList { name##Keys, name##Labels, (int) (sizeof (name##Keys) / sizeof (name##Keys[0])) }
inline const ChoiceList kListWave        = TG_LIST(kWave);
inline const ChoiceList kListWaveB       = TG_LIST(kWaveB);
inline const ChoiceList kListSub         = TG_LIST(kSub);
inline const ChoiceList kListLoop        = TG_LIST(kLoop);
inline const ChoiceList kListDir         = TG_LIST(kDir);
inline const ChoiceList kListOnOff       = TG_LIST(kOnOff);
inline const ChoiceList kListOscSrc      { kOscKeys, kOscLabels, 6 };
inline const ChoiceList kListRingDst     { kOscKeys, kOscLabels, 7 };
inline const ChoiceList kListFilter      = TG_LIST(kFilter);
inline const ChoiceList kListDelaySync   = TG_LIST(kDelaySync);
inline const ChoiceList kListFlutterSync = TG_LIST(kFlutterSync);
inline const ChoiceList kListReverbSync  = TG_LIST(kReverbSync);
inline const ChoiceList kListModSync     = TG_LIST(kModSync);
inline const ChoiceList kListLfoWave     = TG_LIST(kLfoWave);
inline const ChoiceList kListNote        { kNoteKeys, kNoteKeys, 12 };
inline const ChoiceList kListScale       = TG_LIST(kScale);
inline const ChoiceList kListGen         = TG_LIST(kGen);
inline const ChoiceList kListClock       = TG_LIST(kClock);
inline const ChoiceList kListLfoSrc      = TG_LIST(kLfoSrc);
inline const ChoiceList kListEnvSrc      = TG_LIST(kEnvSrc);
inline const ChoiceList kListTarget      { kTargetKeys, kTargetLabels, MT_COUNT };
inline const ChoiceList kListDnaMode     { kDnaModeKeys, kDnaModeLabels, 7 };
inline const ChoiceList kListDnaSrc      { kDnaSrcKeys, kDnaSrcLabels, 8 };
inline const ChoiceList kListResTune     { kResTuneKeys, kResTuneLabels, 6 };
inline const ChoiceList kListSpSize      { kSpSizeKeys, kSpSizeLabels, 4 };
inline const ChoiceList kListBusOrder    { kBusOrderKeys, kBusOrderLabels, 2 };
inline const ChoiceList kListCapPoint    { kCapPointKeys, kCapPointLabels, 2 };
inline const ChoiceList kListDsRate      { kDsRateKeys, kDsRateLabels, 9 };
inline const ChoiceList kListQuality     { kQualityKeys, kQualityLabels, 3 };
inline const ChoiceList kListFilterType  { kFilterTypeKeys, kFilterTypeLabels, 6 };
inline const ChoiceList kListFilterSlope { kFilterSlopeKeys, kFilterSlopeLabels, 4 };
inline const ChoiceList kListFilterLfo   { kFilterLfoKeys, kFilterLfoLabels, 4 };
inline const ChoiceList kListFilterRoute { kFilterRouteKeys, kFilterRouteLabels, 2 };
inline const ChoiceList kListSmpModel    { kSmpModelKeys, kSmpModelLabels, 4 };
inline const ChoiceList kListRptLen      { kRptLenKeys, kRptLenKeys, 4 };
inline const ChoiceList kListRptDur      { kRptDurKeys, kRptDurLabels, 4 };
inline const ChoiceList kListRptMidi     { kRptMidiKeys, kRptMidiLabels, 4 };
inline const ChoiceList kListFlpMode     { kFlpModeKeys, kFlpModeLabels, 2 };
inline const ChoiceList kListFlpStages   { kFlpStageKeys, kFlpStageLabels, 3 };
inline const ChoiceList kListVshRate     { kVshRateKeys, kVshRateLabels, 3 };
inline const ChoiceList kListVshTrig     { kVshTrigKeys, kVshTrigLabels, 3 };
inline const ChoiceList kListDsSync      { kDsSyncKeys, kDsSyncLabels, 3 };
inline const ChoiceList kListDsRestart   { kDsRestartKeys, kDsRestartLabels, 4 };
inline const ChoiceList kListDsGrid      { kDsGridKeys, kDsGridKeys, 2 };
inline const ChoiceList kListDsDir       { kDsDirKeys, kDsDirLabels, 4 };
inline const ChoiceList kListDsPattern   { kDsPatKeys, kDsPatKeys, 4 };
inline const ChoiceList kListDsMidi      { kDsMidiKeys, kDsMidiLabels, 4 };
inline const ChoiceList kListLfoSync     = TG_LIST(kLfoSync);
inline const ChoiceList kListLfoRetrig   = TG_LIST(kLfoRetrig);
inline const ChoiceList kListLfoMode     = TG_LIST(kLfoMode);
inline const ChoiceList kListChdPreset   = TG_LIST(kChdPreset);
inline const ChoiceList kListArpMode     = TG_LIST(kArpMode);
inline const ChoiceList kListArpRate     = TG_LIST(kArpRate);
inline const ChoiceList kListArpVel      = TG_LIST(kArpVel);
inline const ChoiceList kListArpRestart  = TG_LIST(kArpRestart);
inline const ChoiceList kListUniCurve    = TG_LIST(kUniCurve);
inline const ChoiceList kListPEnvTarget  = TG_LIST(kPEnvTarget);
inline const ChoiceList kListMfType  = TG_LIST(kMfType);
inline const ChoiceList kListCharModel = TG_LIST(kCharModel);
inline const ChoiceList kListMfSlope = TG_LIST(kMfSlope);
inline const ChoiceList kListPfQuant = TG_LIST(kPfQuant);
inline const ChoiceList kListPfRoll  = TG_LIST(kPfRoll);
inline const ChoiceList kListPfStut  = TG_LIST(kPfStut);
inline const ChoiceList kListPfGate  = TG_LIST(kPfGate);
inline const ChoiceList kListPfEcho  = TG_LIST(kPfEcho);
inline const ChoiceList kListPfKeys  = TG_LIST(kPfKeys);
inline const ChoiceList kListGsShape = TG_LIST(kGsShape);
inline const ChoiceList kListGsDna   = TG_LIST(kGsDna);
inline const ChoiceList kListBcBars    = TG_LIST(kBcBars);
inline const ChoiceList kListBcSlice   = TG_LIST(kBcSlice);
inline const ChoiceList kListBcStretch = TG_LIST(kBcStretch);
inline const ChoiceList kListBcColour  = TG_LIST(kBcColour);
inline const ChoiceList kListBsPat     = TG_LIST(kBsPat);
inline const ChoiceList kListBsSteps   = TG_LIST(kBsSteps);
inline const ChoiceList kListBsSwitch  = TG_LIST(kBsSwitch);
inline const ChoiceList kListBsKeys    = TG_LIST(kBsKeys);
inline const ChoiceList kListBsAlt     = TG_LIST(kBsAlt);
inline const ChoiceList kListBcSnap    = TG_LIST(kBcSnap);
inline const ChoiceList kListBsAltBreak = TG_LIST(kBsAltBreak);
inline const ChoiceList kListBsToolSrc  = TG_LIST(kBsToolSrc);
inline const ChoiceList kListBsFillMode = TG_LIST(kBsFillMode);
inline const ChoiceList kListBsFillLen  = TG_LIST(kBsFillLen);
inline const ChoiceList kListBsFillStart = TG_LIST(kBsFillStart);
inline const ChoiceList kListBsFillRet  = TG_LIST(kBsFillRet);
inline const ChoiceList kListBsAutoFill = TG_LIST(kBsAutoFill);
// Performance FX pads, in the order they're processed: P_pfRoll + k
constexpr int kPfPads = 13;

// LFO extras (state version 7): 8 parameters per LFO, LFO k at P_lfo1Sync + kLfoXStride * k
constexpr int kLfoXStride = 8;
// Unison (state version 10): 5 parameters per oscillator (Osc 1-3, WT 1, WT 2, Complex) at P_uni1Voices + kUniStride * k
constexpr int kUniStride = 5, kUniOscs = 6;
// Mixer channel strips: Pan, Mute, Solo per source (Osc 1, 2, 3, Sub, WT 1, WT 2, Complex, SuperSaw, Ring), from P_mxOsc1Pan
constexpr int kMxStride = 3, kMxSources = 9;
// ... plus strips added later, each Pan / Mute / Solo in a row: 9 = Granular Synth, 10 = Break Chopper
constexpr int kMxStrips = 11;

// Filter types (filterType) and slopes (filterSlope, 0..3 = 6/12/18/24 dB per octave)
// The effects rack (Effects tab): every effect is a slot with On and Mix, in a user-set order
enum FxSlot { FS_Stutter, FS_Sampler, FS_Flanger, FS_Delay, FS_Chorus, FS_Reverb, FS_Shaper, FS_Multiband, FS_Stereo, FS_COUNT };
inline const char* const kFxSlotNames[FS_COUNT] = { "Beat Repeat", "Vintage Sampler", "Flanger / Phaser", "Tape Delay", "Juno Chorus",
                                                    "Reverbs", "Volume Shaper", "Multiband", "Stereo Tools" };
// The effects that are sends (they add their output to what they hear); next to each other they share one input.
inline bool fxSlotIsSend (int s) { return s == FS_Delay || s == FS_Chorus || s == FS_Reverb; }

enum FilterType { FT_LP, FT_HP, FT_BP, FT_NOTCH, FT_PEAK, FT_AP, FT_TYPE_COUNT };
// The slope each classic model has as a low-pass (its original circuit). Low Pass at this slope
// runs that model exactly as before the filter overhaul; patches from before it load with it.
inline int nativeFilterSlope (int model)
{
    static const int s[17] = { 1, 3, 3, 2, 2, 3, 1, 3, 3, 1, 1, 2, 3, 3, 1, 2, 3 };
    return model >= 0 && model < 17 ? s[model] : 1;
}
// Slopes that mean something for each type (bit k = slope k). Peak / Bell has none.
inline int filterSlopeMask (int type)
{
    switch (type)
    {
        case FT_BP: case FT_NOTCH: return 0b1010;   // 12, 24
        case FT_PEAK: return 0;
        default: return 0b1111;                       // LP, HP, All Pass: 6, 12, 18, 24
    }
}
// The slope actually used: the nearest one the type allows.
inline int effectiveFilterSlope (int type, int slope)
{
    const int mask = filterSlopeMask (type);
    if (mask == 0) return 1;
    if (mask & (1 << slope)) return slope;
    return slope <= 1 ? 1 : 3;
}
inline const ChoiceList kListArMod       { kArModKeys, kArModLabels, 6 };
inline const ChoiceList kListArFmT       { kArFmTKeys, kArFmTLabels, 5 };
#undef TG_LIST

// ---------------------------------------------------------------- parameters
// F(id, name, min, max, default, step, skewCentre (0 = linear, -1 = logarithmic: equal distance per octave))
// C(id, name, list, defaultIndex)
// B(id, name, default)
// A(id, name, default fraction)  modulation amount, stored as -1..1 of the target's range
#define TG_PARAMS(F, C, B, A) \
    F(masterVolume,  "Master Volume",      0, 1, 0.25, 0.01, 0) \
    F(polyphony,     "Polyphony",          1, 16, 10, 1, 0) \
    F(keyboardOctave,"Keyboard Octave",   -2, 2, 0, 1, 0) \
    F(porta,         "Portamento",         0, 0.5, 0, 0.001, 0.08) \
    F(velSens,       "Velocity Sensitivity", 0, 1, 0, 0.01, 0) \
    F(bendRange,     "Pitch Bend Range",   0, 24, 2, 1, 0) \
    F(warmth,        "Warmth",             0, 1, 0.5, 0.01, 0) \
    F(bassKeep,      "Bass Keep",          0, 1, 0.5, 0.01, 0) \
    F(analogDrift,   "Analog Drift",       0, 1, 0.3, 0.01, 0) \
    F(osc1Semi,      "Osc1 Semitones",   -12, 12, 0, 1, 0) \
    F(osc2Semi,      "Osc2 Semitones",   -12, 12, 0, 1, 0) \
    F(osc3Semi,      "Osc3 Semitones",   -12, 12, 0, 1, 0) \
    F(subSemi,       "Sub Semitones",    -12, 12, 0, 1, 0) \
    F(osc4Semi,      "WT1 Semitones",    -12, 12, 0, 1, 0) \
    F(wt2Semi,       "WT2 Semitones",    -12, 12, 0, 1, 0) \
    F(complexSemi,   "Complex Semitones",-12, 12, 0, 1, 0) \
    F(supersawSemi,  "SuperSaw Semitones",-12, 12, 0, 1, 0) \
    C(osc1Wave,  "Osc1 Waveform", kListWave, 0) \
    F(osc1Detune,"Osc1 Detune", -50, 50, 0, 1, 0) \
    F(osc1Oct,   "Osc1 Octave", -2, 2, 0, 1, 0) \
    C(osc2Wave,  "Osc2 Waveform", kListWave, 0) \
    F(osc2Detune,"Osc2 Detune", -50, 50, 7, 1, 0) \
    F(osc2Oct,   "Osc2 Octave", -2, 2, 0, 1, 0) \
    C(osc3Wave,  "Osc3 Waveform", kListWave, 0) \
    F(osc3Detune,"Osc3 Detune", -50, 50, -7, 1, 0) \
    F(osc3Oct,   "Osc3 Octave", -2, 2, -1, 1, 0) \
    C(subWave,   "Sub Waveform", kListSub, 0) \
    F(subOct,    "Sub Octave", -3, 0, -1, 1, 0) \
    F(osc4Detune,   "WT Detune", -50, 50, 0, 1, 0) \
    F(osc4Oct,      "WT Octave", -2, 2, 0, 1, 0) \
    F(osc4Root,     "WT Root Note", 24, 84, 69, 1, 0) \
    F(osc4Position, "WT Scan Position", 0, 1, 0, 0.001, 0) \
    F(osc4Window,   "WT Scan Window", 0.001, 1, 1, 0.001, 0) \
    C(osc4LoopMode, "WT Loop Mode", kListLoop, 1) \
    F(osc4LoopStart,"WT Loop Start", 0, 1, 0, 0.001, 0) \
    F(osc4LoopEnd,  "WT Loop End", 0, 1, 1, 0.001, 0) \
    C(osc4Direction,"WT Direction", kListDir, 0) \
    C(osc4Normalize,"WT Normalize", kListOnOff, 0) \
    F(wt2Detune,   "WT2 Detune", -50, 50, 0, 1, 0) \
    F(wt2Oct,      "WT2 Octave", -2, 2, 0, 1, 0) \
    F(wt2Root,     "WT2 Root Note", 24, 84, 69, 1, 0) \
    F(wt2Position, "WT2 Scan Position", 0, 1, 0, 0.001, 0) \
    F(wt2Window,   "WT2 Scan Window", 0.001, 1, 1, 0.001, 0) \
    C(wt2LoopMode, "WT2 Loop Mode", kListLoop, 1) \
    F(wt2LoopStart,"WT2 Loop Start", 0, 1, 0, 0.001, 0) \
    F(wt2LoopEnd,  "WT2 Loop End", 0, 1, 1, 0.001, 0) \
    C(wt2Direction,"WT2 Direction", kListDir, 0) \
    C(wt2Normalize,"WT2 Normalize", kListOnOff, 0) \
    F(wt2Gain,     "WT2 Level", 0, 1, 0.4, 0.01, 0) \
    C(complexWaveA, "Complex Wave A", kListWave, 0) \
    C(complexWaveB, "Complex Wave B", kListWaveB, 0) \
    F(complexDetune,"Complex Detune", -50, 50, 0, 1, 0) \
    F(complexOct,   "Complex Octave", -2, 2, 0, 1, 0) \
    F(complexRatio, "Complex Mod Ratio", 0.125, 8, 2, 0.125, 0) \
    F(complexFm,    "Complex FM Index", 0, 1500, 260, 1, 0) \
    F(complexShape, "Complex Wavefold", 1, 25, 4.5, 0.1, 0) \
    F(complexMix,   "Complex A/B Blend", 0, 1, 0.65, 0.01, 0) \
    F(supersawDetune,"SuperSaw Detune", -50, 50, 0, 1, 0) \
    F(supersawOct,   "SuperSaw Octave", -2, 2, 0, 1, 0) \
    F(supersawVoices,"SuperSaw Voices", 2, 9, 7, 1, 0) \
    F(supersawSpread,"SuperSaw Spread", 0, 80, 22, 1, 0) \
    F(supersawStereo,"SuperSaw Stereo Width", 0, 1, 0.65, 0.01, 0) \
    F(supersawDrift, "SuperSaw Analog Drift", 0, 20, 3.5, 0.1, 0) \
    F(osc1Gain,     "Osc1 Level", 0, 1, 0.8, 0.01, 0) \
    F(osc2Gain,     "Osc2 Level", 0, 1, 0.5, 0.01, 0) \
    F(osc3Gain,     "Osc3 Level", 0, 1, 0.35, 0.01, 0) \
    F(subGain,      "Sub Level", 0, 1, 0.35, 0.01, 0) \
    F(osc4Gain,     "WT Level", 0, 1, 0.4, 0.01, 0) \
    F(complexGain,  "Complex Level", 0, 1, 0.35, 0.01, 0) \
    F(supersawGain, "SuperSaw Level", 0, 1, 0.35, 0.01, 0) \
    F(fmAmount,     "Legacy FM Depth", 0, 1500, 0, 1, 0) \
    F(ringMix,      "Ring Wet/Dry", 0, 1, 0, 0.01, 0) \
    F(ringGain,     "Ring Output Level", 0, 1, 0.5, 0.01, 0) \
    F(delayMix,     "Delay Return", 0, 1, 0.18, 0.01, 0) \
    F(reverbMix,    "90s Reverb Return", 0, 1, 0.22, 0.01, 0) \
    F(shimmerMix,   "Shimmer Return", 0, 1, 0, 0.01, 0) \
    F(chorusMix,    "Juno Chorus Return", 0, 1, 0.28, 0.01, 0) \
    F(reverseMix,   "Reverse Reverb Return", 0, 1, 0, 0.01, 0) \
    C(fmSlot1Source,"FM1 Source", kListOscSrc, 1) \
    C(fmSlot1Dest,  "FM1 Dest", kListOscSrc, 0) \
    F(fmSlot1Amt,   "FM1 Amount", 0, 1200, 0, 1, 0) \
    C(fmSlot2Source,"FM2 Source", kListOscSrc, 0) \
    C(fmSlot2Dest,  "FM2 Dest", kListOscSrc, 1) \
    F(fmSlot2Amt,   "FM2 Amount", 0, 1200, 0, 1, 0) \
    C(fmSlot3Source,"FM3 Source", kListOscSrc, 2) \
    C(fmSlot3Dest,  "FM3 Dest", kListOscSrc, 2) \
    F(fmSlot3Amt,   "FM3 Amount", 0, 1200, 0, 1, 0) \
    C(fmSlot4Source,"FM4 Source", kListOscSrc, 3) \
    C(fmSlot4Dest,  "FM4 Dest", kListOscSrc, 3) \
    F(fmSlot4Amt,   "FM4 Amount", 0, 1200, 0, 1, 0) \
    C(ringSlot1Source,"Ring1 Source", kListOscSrc, 2) \
    C(ringSlot1Dest,  "Ring1 Carrier", kListRingDst, 0) \
    F(ringSlot1Amt,   "Ring1 Amount", 0, 1, 0, 0.01, 0) \
    C(ringSlot2Source,"Ring2 Source", kListOscSrc, 3) \
    C(ringSlot2Dest,  "Ring2 Carrier", kListRingDst, 6) \
    F(ringSlot2Amt,   "Ring2 Amount", 0, 1, 0, 0.01, 0) \
    C(filterMode,   "Filter Model", kListFilter, 0) \
    F(filterCutoff, "Cutoff", 20, 20000, 2200, 0.1, -1) \
    F(filterRes,    "Resonance", 0.1, 25, 1.5, 0.1, 0) \
    F(filterDrive,  "Drive", 1, 25, 1, 0.1, 0) \
    F(ampA, "Amp Attack", 0.001, 3, 0.01, 0.001, 0.4) \
    F(ampD, "Amp Decay", 0.001, 3, 0.2, 0.001, 0.4) \
    F(ampS, "Amp Sustain", 0, 1, 0.75, 0.01, 0) \
    F(ampR, "Amp Release", 0.001, 5, 0.45, 0.001, 0.6) \
    F(fEnvAmt, "Filter Env Amount", -10000, 10000, 2200, 1, 0) \
    F(fEnvA, "Filter Attack", 0.001, 3, 0.01, 0.001, 0.4) \
    F(fEnvD, "Filter Decay", 0.001, 3, 0.25, 0.001, 0.4) \
    F(fEnvS, "Filter Sustain", 0, 1, 0.3, 0.01, 0) \
    F(fEnvR, "Filter Release", 0.001, 5, 0.35, 0.001, 0.6) \
    F(delayTime,    "Delay Time", 0.05, 0.9, 0.32, 0.01, 0) \
    C(delaySync,    "Delay Sync", kListDelaySync, 0) \
    F(delayFeedback,"Delay Feedback", 0, 0.9, 0.35, 0.01, 0) \
    F(tapeTone,     "Delay Tone", 600, 9000, 3400, 10, 0) \
    F(tapeFlutter,  "Flutter Amount", 0, 0.02, 0.0025, 0.0001, 0) \
    C(flutterSync,  "Flutter Sync", kListFlutterSync, 0) \
    F(reverbSize,   "Reverb Size", 0.5, 4.5, 2.4, 0.1, 0) \
    C(reverbSync,   "Reverb/Shimmer Sync", kListReverbSync, 0) \
    F(reverbTone,   "Reverb Tone", 800, 12000, 5200, 10, 0) \
    F(shimmerBright,"Shimmer Brightness", 1200, 12000, 5400, 10, 0) \
    F(chorusRate,   "Chorus Rate", 0.05, 3, 0.8, 0.01, 0) \
    C(chorusSync,   "Chorus Sync", kListModSync, 0) \
    F(chorusDepth,  "Chorus Depth", 0, 0.015, 0.0048, 0.0001, 0) \
    F(reverseTime,  "Reverse Reverb Time", 0.05, 1.5, 0.42, 0.01, 0) \
    C(reverseSync,  "Reverse Reverb Sync", kListModSync, 0) \
    F(reversePitch, "Reverse Pitch Drift", 0, 0.03, 0.008, 0.0001, 0) \
    C(lfo1Wave, "LFO1 Wave", kListLfoWave, 0) \
    F(lfo1Rate, "LFO1 Rate", 0.01, 20, 5, 0.01, 2) \
    F(lfo1Depth,"LFO1 Depth", 0, 1, 0.5, 0.01, 0) \
    C(lfo2Wave, "LFO2 Wave", kListLfoWave, 0) \
    F(lfo2Rate, "LFO2 Rate", 0.01, 20, 2, 0.01, 2) \
    F(lfo2Depth,"LFO2 Depth", 0, 1, 0.5, 0.01, 0) \
    C(lfo3Wave, "LFO3 Wave", kListLfoWave, 0) \
    F(lfo3Rate, "LFO3 Rate", 0.01, 20, 0.4, 0.01, 2) \
    F(lfo3Depth,"LFO3 Depth", 0, 1, 0.5, 0.01, 0) \
    C(lfo4Wave, "LFO4 Wave", kListLfoWave, 0) \
    F(lfo4Rate, "LFO4 Rate", 0.01, 20, 1, 0.01, 2) \
    F(lfo4Depth,"LFO4 Depth", 0, 1, 1, 0.01, 0) \
    F(mEnv1A, "Mod Env1 Attack", 0.001, 4, 0.01, 0.001, 0.5) \
    F(mEnv1D, "Mod Env1 Decay", 0.001, 4, 0.2, 0.001, 0.5) \
    F(mEnv1S, "Mod Env1 Sustain", 0, 1, 0, 0.01, 0) \
    F(mEnv1R, "Mod Env1 Release", 0.001, 4, 0.2, 0.001, 0.5) \
    F(mEnv2A, "Mod Env2 Attack", 0.001, 4, 0.01, 0.001, 0.5) \
    F(mEnv2D, "Mod Env2 Decay", 0.001, 4, 0.3, 0.001, 0.5) \
    F(mEnv2S, "Mod Env2 Sustain", 0, 1, 0, 0.01, 0) \
    F(mEnv2R, "Mod Env2 Release", 0.001, 4, 0.3, 0.001, 0.5) \
    F(mEnv3A, "Mod Env3 Attack", 0.001, 4, 0.5, 0.001, 0.5) \
    F(mEnv3D, "Mod Env3 Decay", 0.001, 4, 1.2, 0.001, 0.5) \
    F(mEnv3S, "Mod Env3 Sustain", 0, 1, 0, 0.01, 0) \
    F(mEnv3R, "Mod Env3 Release", 0.001, 4, 1, 0.001, 0.5) \
    C(lfoAssignSource0, "LFO Slot1 Source", kListLfoSrc, 0) \
    C(lfoAssignTarget0, "LFO Slot1 Target", kListTarget, 1) \
    A(lfoAssignAmt0,    "LFO Slot1 Amount", 30.0 / 1200.0) \
    C(lfoAssignSource1, "LFO Slot2 Source", kListLfoSrc, 1) \
    C(lfoAssignTarget1, "LFO Slot2 Target", kListTarget, 2) \
    A(lfoAssignAmt1,    "LFO Slot2 Amount", 0) \
    C(lfoAssignSource2, "LFO Slot3 Source", kListLfoSrc, 2) \
    C(lfoAssignTarget2, "LFO Slot3 Target", kListTarget, 0) \
    A(lfoAssignAmt2,    "LFO Slot3 Amount", 0) \
    C(envAssignSource0, "Env Slot1 Source", kListEnvSrc, 0) \
    C(envAssignTarget0, "Env Slot1 Target", kListTarget, 2) \
    A(envAssignAmt0,    "Env Slot1 Amount", 2500.0 / 8000.0) \
    C(envAssignSource1, "Env Slot2 Source", kListEnvSrc, 1) \
    C(envAssignTarget1, "Env Slot2 Target", kListTarget, 0) \
    A(envAssignAmt1,    "Env Slot2 Amount", 0) \
    C(envAssignSource2, "Env Slot3 Source", kListEnvSrc, 2) \
    C(envAssignTarget2, "Env Slot3 Target", kListTarget, 0) \
    A(envAssignAmt2,    "Env Slot3 Amount", 0) \
    B(seqRun,          "Sequencer Run", false) \
    C(seqClock,        "Sequencer Clock", kListClock, 0) \
    F(seqTempo,        "Sequencer Tempo", 40, 200, 130, 1, 0) \
    F(seqGate,         "Sequencer Gate", 0.05, 1, 0.75, 0.01, 0) \
    C(seqScaleRoot,    "Scale Root", kListNote, 0) \
    C(seqScaleType,    "Scale Type", kListScale, 1) \
    F(seqAccentAmt,    "Accent Amount", 0, 1, 0.35, 0.01, 0) \
    F(seqLength,       "Sequence Length", 1, 32, 16, 1, 0) \
    C(seqGenMode,      "Generator Mode", kListGen, 0) \
    F(seqEuclidPulses, "Euclidean Pulses", 1, 32, 7, 1, 0) \
    F(seqEuclidRotate, "Euclidean Rotate", 0, 31, 0, 1, 0) \
    F(randRate,        "Random Rate", 0.05, 20, 2, 0.01, 2) \
    F(ccANum,          "MIDI CC A Number", 0, 127, 74, 1, 0) \
    F(ccBNum,          "MIDI CC B Number", 0, 127, 71, 1, 0) \
    F(mod1Amt, "Mod 1 Amount", -1, 1, 0, 0.001, 0) \
    F(mod2Amt, "Mod 2 Amount", -1, 1, 0, 0.001, 0) \
    F(mod3Amt, "Mod 3 Amount", -1, 1, 0, 0.001, 0) \
    F(mod4Amt, "Mod 4 Amount", -1, 1, 0, 0.001, 0) \
    F(mod5Amt, "Mod 5 Amount", -1, 1, 0, 0.001, 0) \
    F(mod6Amt, "Mod 6 Amount", -1, 1, 0, 0.001, 0) \
    F(mod7Amt, "Mod 7 Amount", -1, 1, 0, 0.001, 0) \
    F(mod8Amt, "Mod 8 Amount", -1, 1, 0, 0.001, 0) \
    F(mod9Amt, "Mod 9 Amount", -1, 1, 0, 0.001, 0) \
    F(mod10Amt, "Mod 10 Amount", -1, 1, 0, 0.001, 0) \
    F(mod11Amt, "Mod 11 Amount", -1, 1, 0, 0.001, 0) \
    F(mod12Amt, "Mod 12 Amount", -1, 1, 0, 0.001, 0) \
    F(mod13Amt, "Mod 13 Amount", -1, 1, 0, 0.001, 0) \
    F(mod14Amt, "Mod 14 Amount", -1, 1, 0, 0.001, 0) \
    F(mod15Amt, "Mod 15 Amount", -1, 1, 0, 0.001, 0) \
    F(mod16Amt, "Mod 16 Amount", -1, 1, 0, 0.001, 0) \
    F(mod17Amt, "Mod 17 Amount", -1, 1, 0, 0.001, 0) \
    F(mod18Amt, "Mod 18 Amount", -1, 1, 0, 0.001, 0) \
    F(mod19Amt, "Mod 19 Amount", -1, 1, 0, 0.001, 0) \
    F(mod20Amt, "Mod 20 Amount", -1, 1, 0, 0.001, 0) \
    F(mod21Amt, "Mod 21 Amount", -1, 1, 0, 0.001, 0) \
    F(mod22Amt, "Mod 22 Amount", -1, 1, 0, 0.001, 0) \
    F(mod23Amt, "Mod 23 Amount", -1, 1, 0, 0.001, 0) \
    F(mod24Amt, "Mod 24 Amount", -1, 1, 0, 0.001, 0) \
    F(mod25Amt, "Mod 25 Amount", -1, 1, 0, 0.001, 0) \
    F(mod26Amt, "Mod 26 Amount", -1, 1, 0, 0.001, 0) \
    F(mod27Amt, "Mod 27 Amount", -1, 1, 0, 0.001, 0) \
    F(mod28Amt, "Mod 28 Amount", -1, 1, 0, 0.001, 0) \
    F(mod29Amt, "Mod 29 Amount", -1, 1, 0, 0.001, 0) \
    F(mod30Amt, "Mod 30 Amount", -1, 1, 0, 0.001, 0) \
    F(mod31Amt, "Mod 31 Amount", -1, 1, 0, 0.001, 0) \
    F(mod32Amt, "Mod 32 Amount", -1, 1, 0, 0.001, 0) \
    F(macro1, "Macro 1", 0, 1, 0, 0.001, 0) \
    F(macro2, "Macro 2", 0, 1, 0, 0.001, 0) \
    F(macro3, "Macro 3", 0, 1, 0, 0.001, 0) \
    F(macro4, "Macro 4", 0, 1, 0, 0.001, 0) \
    F(macro5, "Macro 5", 0, 1, 0, 0.001, 0) \
    F(macro6, "Macro 6", 0, 1, 0, 0.001, 0) \
    F(macro7, "Macro 7", 0, 1, 0, 0.001, 0) \
    F(macro8, "Macro 8", 0, 1, 0, 0.001, 0) \
    F(sceneX, "Scene X", 0, 1, 0, 0.001, 0) \
    F(sceneY, "Scene Y", 0, 1, 0, 0.001, 0) \
    B(sceneMorph, "Scene Morph", false) \
    F(wmMix,   "Wave Mutation Mix", 0, 1, 0, 0.01, 0) \
    F(wmDrive, "Wave Mutation Drive", 0, 1, 0.3, 0.01, 0) \
    F(wmFold,  "Wave Fold", 0, 1, 0.4, 0.01, 0) \
    F(wmShape, "Wave Shape", 0, 1, 0, 0.01, 0) \
    F(wmBend,  "Wave Bend", -1, 1, 0, 0.01, 0) \
    F(wmAsym,  "Wave Asymmetry", -1, 1, 0, 0.01, 0) \
    F(wmRect,  "Wave Rectify", 0, 1, 0, 0.01, 0) \
    F(wmBits,  "Bit Depth", 1, 16, 16, 0.01, 0) \
    F(wmDown,  "Sample Rate Reduce", 1, 32, 1, 0.01, 4) \
    C(arMod,      "Audio-Rate Modulator", kListArMod, 0) \
    F(arRatio,    "Modulator Ratio", 0.125, 16, 1, 0.001, 2) \
    F(arOffset,   "Modulator Offset", -500, 500, 0, 0.1, 0) \
    F(arFm,       "Audio-Rate FM", 0, 1, 0, 0.001, 0) \
    C(arFmTarget, "FM Target", kListArFmT, 0) \
    F(arAm,       "Audio-Rate AM", 0, 1, 0, 0.01, 0) \
    F(arRing,     "Audio-Rate Ring", 0, 1, 0, 0.01, 0) \
    F(arShift,    "Frequency Shift", -1000, 1000, 0, 0.1, 0) \
    F(arShiftMix, "Frequency Shift Mix", 0, 1, 0, 0.01, 0) \
    F(dnaMix,    "DNA Splice Mix", 0, 1, 0, 0.01, 0) \
    C(dnaMode,   "DNA Splice Mode", kListDnaMode, 0) \
    C(dnaA,      "DNA Source A", kListDnaSrc, 0) \
    C(dnaB,      "DNA Source B", kListDnaSrc, 1) \
    F(dnaAmount, "DNA Splice Amount", 0, 1, 0.5, 0.001, 0) \
    F(dnaChar,   "DNA Splice Character", 0, 1, 0.5, 0.001, 0) \
    F(resMix,      "Resonator Mix", 0, 1, 0, 0.01, 0) \
    C(resTuning,   "Resonator Tuning", kListResTune, 0) \
    F(resModes,    "Resonator Modes", 1, 16, 8, 1, 0) \
    F(resPitch,    "Resonator Pitch", -24, 24, 0, 0.01, 0) \
    F(resDecay,    "Resonator Decay", 0.02, 10, 1.2, 0.001, 1) \
    F(resDamping,  "Resonator Damping", 0, 1, 0.5, 0.01, 0) \
    F(resInharm,   "Resonator Inharmonicity", 0, 1, 0, 0.01, 0) \
    F(resSpread,   "Resonator Stereo Spread", 0, 1, 0.5, 0.01, 0) \
    F(resFeedback, "Resonator Feedback", 0, 1, 0, 0.01, 0) \
    F(grMix,       "Granular Mix", 0, 1, 0, 0.01, 0) \
    F(grSize,      "Grain Size", 5, 500, 80, 0.1, 60) \
    F(grDensity,   "Grain Density", 1, 200, 20, 0.01, 20) \
    F(grPosition,  "Grain Position", 0, 3, 0.25, 0.001, 0.5) \
    F(grJitter,    "Grain Position Jitter", 0, 1, 0.2, 0.01, 0) \
    F(grPitch,     "Grain Pitch", -24, 24, 0, 0.01, 0) \
    F(grPitchRand, "Grain Pitch Jitter", 0, 1, 0, 0.01, 0) \
    F(grReverse,   "Grain Reverse Chance", 0, 1, 0, 0.01, 0) \
    F(grSpread,    "Grain Stereo Spread", 0, 1, 0.5, 0.01, 0) \
    F(grFeedback,  "Granular Feedback", 0, 0.95, 0, 0.01, 0) \
    B(grFreeze,    "Granular Freeze", false) \
    B(spOn,        "Spectral On", false) \
    C(spSize,      "Spectral FFT Size", kListSpSize, 2) \
    F(spMix,       "Spectral Mix", 0, 1, 1, 0.01, 0) \
    B(spFreeze,    "Spectral Freeze", false) \
    F(spBlur,      "Spectral Blur", 0, 1, 0, 0.01, 0) \
    F(spShift,     "Spectral Shift", -1000, 1000, 0, 0.1, 0) \
    F(spScramble,  "Spectral Scramble", 0, 1, 0, 0.01, 0) \
    F(spTilt,      "Spectral Tilt", -1, 1, 0, 0.01, 0) \
    F(spMorph,     "Spectral Morph", 0, 1, 0, 0.01, 0) \
    F(spFormant,   "Spectral Formant", -12, 12, 0, 0.01, 0) \
    F(spFeedback,  "Spectral Feedback", 0, 0.9, 0, 0.01, 0) \
    C(busOrder,    "Bus Order", kListBusOrder, 0) \
    F(fbGrGr, "Feedback Granular > Granular", 0, 1, 0, 0.01, 0) \
    F(fbGrSp, "Feedback Granular > Spectral", 0, 1, 0, 0.01, 0) \
    F(fbGrDl, "Feedback Granular > Delay", 0, 1, 0, 0.01, 0) \
    F(fbSpGr, "Feedback Spectral > Granular", 0, 1, 0, 0.01, 0) \
    F(fbSpSp, "Feedback Spectral > Spectral", 0, 1, 0, 0.01, 0) \
    F(fbSpDl, "Feedback Spectral > Delay", 0, 1, 0, 0.01, 0) \
    F(fbDlGr, "Feedback Delay > Granular", 0, 1, 0, 0.01, 0) \
    F(fbDlSp, "Feedback Delay > Spectral", 0, 1, 0, 0.01, 0) \
    F(fbDlDl, "Feedback Delay > Delay", 0, 1, 0, 0.01, 0) \
    F(fbOutGr, "Feedback Output > Granular", 0, 1, 0, 0.01, 0) \
    F(fbOutSp, "Feedback Output > Spectral", 0, 1, 0, 0.01, 0) \
    F(fbOutDl, "Feedback Output > Delay", 0, 1, 0, 0.01, 0) \
    F(fbTime,      "Feedback Time", 10, 1000, 120, 0.1, 150) \
    F(fbTone,      "Feedback Tone", 200, 20000, 6000, 1, 2000) \
    F(fbSafety,    "Feedback Safety", 0, 1, 0.5, 0.01, 0) \
    C(capPoint,    "Capture Point", kListCapPoint, 1) \
    F(ciAmount,    "Cell Instability", 0, 1, 0, 0.01, 0) \
    F(ciRate,      "Instability Rate", 0.05, 10, 0.5, 0.01, 1) \
    F(ciPitch,     "Instability > Pitch", 0, 1, 0.5, 0.01, 0) \
    F(ciCutoff,    "Instability > Cutoff", 0, 1, 0.4, 0.01, 0) \
    F(ciRes,       "Instability > Resonance", 0, 1, 0.2, 0.01, 0) \
    F(ciLevel,     "Instability > Osc Levels", 0, 1, 0.3, 0.01, 0) \
    F(ciFold,      "Instability > Wave Fold", 0, 1, 0.3, 0.01, 0) \
    F(ciScan,      "Instability > WT Scan", 0, 1, 0.3, 0.01, 0) \
    F(ciFm,        "Instability > FM", 0, 1, 0.3, 0.01, 0) \
    F(ciDna,       "Instability > DNA Splice", 0, 1, 0.3, 0.01, 0) \
    F(ciEnv,       "Instability > Envelopes", 0, 1, 0.3, 0.01, 0) \
    F(ciReso,      "Instability > Resonator", 0, 1, 0.3, 0.01, 0) \
    F(mutAmount,   "Mutate", 0, 1, 0, 0.001, 0) \
    F(mutSeed,     "Mutate Seed", 0, 999999, 1, 1, 0) \
    B(mutLock1, "Mutate Lock 1", false) \
    B(mutLock2, "Mutate Lock 2", false) \
    B(mutLock3, "Mutate Lock 3", false) \
    B(mutLock4, "Mutate Lock 4", false) \
    B(mutLock5, "Mutate Lock 5", false) \
    B(mutLock6, "Mutate Lock 6", false) \
    B(mutLock7, "Mutate Lock 7", false) \
    B(mutLock8, "Mutate Lock 8", false) \
    B(mutLock9, "Mutate Lock 9", false) \
    B(mutLock10, "Mutate Lock 10", false) \
    B(mutLock11, "Mutate Lock 11", false) \
    B(mutLock12, "Mutate Lock 12", false) \
    B(mutLock13, "Mutate Lock 13", false) \
    B(dsOn,        "DNA Sequencer On", false) \
    F(dsSteps,     "DNA Sequencer Steps", 1, 32, 16, 1, 0) \
    C(dsRate,      "DNA Sequencer Rate", kListDsRate, 2) \
    F(dsFreeHz,    "DNA Sequencer Free Rate", 0.1, 20, 4, 0.01, 2) \
    F(dsGlide,     "DNA Sequencer Glide", 0, 1, 0.2, 0.01, 0) \
    F(dsDepth,     "DNA Sequencer Depth", 0, 1, 1, 0.01, 0) \
    C(quality,     "Quality", kListQuality, 1) \
    C(filterType,  "Filter Type", kListFilterType, 0) \
    C(filterSlope, "Filter Slope", kListFilterSlope, 1) \
    F(filterMix,   "Filter Mix", 0, 1, 1, 0.01, 0) \
    F(filterKeyTrack, "Filter Key Tracking", 0, 1, 0, 0.01, 0) \
    F(filterLfoAmt, "Filter LFO Amount", -1, 1, 0, 0.001, 0) \
    C(filterLfoSrc, "Filter LFO Source", kListFilterLfo, 0) \
    B(filter2On,   "Filter 2 On", false) \
    C(filterRouting, "Filter Routing", kListFilterRoute, 0) \
    F(filterBalance, "Filter Balance", 0, 1, 0.5, 0.01, 0) \
    B(filterStereoSplit, "Filter Stereo Split", false) \
    C(filter2Mode, "Filter 2 Model", kListFilter, 0) \
    C(filter2Type, "Filter 2 Type", kListFilterType, 1) \
    C(filter2Slope, "Filter 2 Slope", kListFilterSlope, 1) \
    F(filter2Cutoff, "Filter 2 Cutoff", 20, 20000, 400, 0.1, -1) \
    F(filter2Res,  "Filter 2 Resonance", 0.1, 25, 1.5, 0.1, 0) \
    F(filter2Drive, "Filter 2 Drive", 1, 25, 1, 0.1, 0) \
    F(filter2Mix,  "Filter 2 Mix", 0, 1, 1, 0.01, 0) \
    F(filter2EnvAmt, "Filter 2 Env Amount", -10000, 10000, 0, 1, 0) \
    F(filter2KeyTrack, "Filter 2 Key Tracking", 0, 1, 0, 0.01, 0) \
    F(filter2LfoAmt, "Filter 2 LFO Amount", -1, 1, 0, 0.001, 0) \
    C(filter2LfoSrc, "Filter 2 LFO Source", kListFilterLfo, 0) \
    C(dsSync,      "DNA Sequencer Sync", kListDsSync, 0) \
    C(dsRestart,   "DNA Sequencer Restart", kListDsRestart, 0) \
    F(dsOffset,    "DNA Sequencer Start Offset", 0, 31, 0, 1, 0) \
    F(dsSwing,     "DNA Sequencer Swing", 0, 0.75, 0, 0.01, 0) \
    C(dsSwingGrid, "DNA Sequencer Swing Grid", kListDsGrid, 0) \
    C(dsDirection, "DNA Sequencer Direction", kListDsDir, 0) \
    B(dsLane2On,   "DNA Sequencer Lane 2 On", false) \
    F(dsLane2Steps, "DNA Sequencer Lane 2 Steps", 1, 32, 12, 1, 0) \
    C(dsPattern,   "DNA Sequencer Pattern", kListDsPattern, 0) \
    B(dsChainOn,   "DNA Sequencer Chain", false) \
    F(dsDensity,   "DNA Sequencer Density", 0, 1, 1, 0.01, 0) \
    F(dsProbScale, "DNA Sequencer Probability Scale", 0, 2, 1, 0.01, 0) \
    C(dsMidiSelect, "DNA Sequencer MIDI Pattern Select", kListDsMidi, 0) \
    B(fxOnStutter, "Beat Repeat On", false) \
    F(fxMixStutter, "Beat Repeat Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnSampler, "Vintage Sampler On", false) \
    F(fxMixSampler, "Vintage Sampler Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnFlanger, "Flanger / Phaser On", false) \
    F(fxMixFlanger, "Flanger / Phaser Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnDelay, "Delay On", true) \
    F(fxMixDelay, "Delay Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnChorus, "Chorus On", true) \
    F(fxMixChorus, "Chorus Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnReverb, "Reverbs On", true) \
    F(fxMixReverb, "Reverbs Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnShaper, "Volume Shaper On", false) \
    F(fxMixShaper, "Volume Shaper Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnMultiband, "Multiband On", false) \
    F(fxMixMultiband, "Multiband Mix", 0, 1, 1, 0.01, 0) \
    B(fxOnStereo, "Stereo Tools On", false) \
    F(fxMixStereo, "Stereo Tools Mix", 0, 1, 1, 0.01, 0) \
    F(mbcXLow,  "Multiband Low / Mid", 40, 1000, 150, 1, -1) \
    F(mbcXHigh, "Multiband Mid / High", 1000, 12000, 2500, 1, -1) \
    F(mbcUpL,   "Multiband Low Upward", 0, 1, 0.5, 0.01, 0) \
    F(mbcDownL, "Multiband Low Downward", 0, 1, 0.5, 0.01, 0) \
    F(mbcInL,   "Multiband Low Input", -24, 24, 0, 0.1, 0) \
    F(mbcOutL,  "Multiband Low Output", -24, 24, 0, 0.1, 0) \
    F(mbcUpM,   "Multiband Mid Upward", 0, 1, 0.5, 0.01, 0) \
    F(mbcDownM, "Multiband Mid Downward", 0, 1, 0.5, 0.01, 0) \
    F(mbcInM,   "Multiband Mid Input", -24, 24, 0, 0.1, 0) \
    F(mbcOutM,  "Multiband Mid Output", -24, 24, 0, 0.1, 0) \
    F(mbcUpH,   "Multiband High Upward", 0, 1, 0.5, 0.01, 0) \
    F(mbcDownH, "Multiband High Downward", 0, 1, 0.5, 0.01, 0) \
    F(mbcInH,   "Multiband High Input", -24, 24, 0, 0.1, 0) \
    F(mbcOutH,  "Multiband High Output", -24, 24, 0, 0.1, 0) \
    F(mbcDepth, "Multiband Depth", 0, 1, 0.5, 0.01, 0) \
    F(mbcTime,  "Multiband Time", 0, 1, 0.5, 0.01, 0) \
    F(mbcGain,  "Multiband Output Gain", -24, 24, 0, 0.1, 0) \
    C(smpModel, "Sampler Model", kListSmpModel, 0) \
    F(smpBits,  "Sampler Bits", 1, 16, 12, 0.1, 0) \
    F(smpRate,  "Sampler Rate", 2000, 48000, 26040, 1, -1) \
    B(smpAA,    "Sampler Anti-Alias", true) \
    F(smpCutoff, "Sampler Filter", 500, 20000, 12000, 1, -1) \
    F(smpRes,   "Sampler Filter Resonance", 0, 1, 0.2, 0.01, 0) \
    F(smpNoise, "Sampler Noise", 0, 1, 0.1, 0.01, 0) \
    F(smpDrive, "Sampler Input Clip", 0, 1, 0.2, 0.01, 0) \
    C(rptLength, "Repeat Length", kListRptLen, 1) \
    F(rptShrink, "Repeat Shrink", 0, 1, 0, 0.01, 0) \
    F(rptPitch,  "Repeat Pitch Drop", 0, 12, 0, 0.1, 0) \
    B(rptReverse, "Repeat Reverse", false) \
    F(rptGate,   "Repeat Gate", 0.1, 1, 1, 0.01, 0) \
    B(rptTrigger, "Repeat Trigger", false) \
    F(rptChance, "Repeat Chance", 0, 1, 0, 0.01, 0) \
    C(rptDuration, "Repeat Duration", kListRptDur, 1) \
    C(rptMidi,   "Repeat MIDI Trigger", kListRptMidi, 0) \
    B(rptDna,    "Repeat on DNA Step Gate", false) \
    C(flpMode,   "Flanger / Phaser Mode", kListFlpMode, 0) \
    F(flpRate,   "Flanger / Phaser Rate", 0.01, 10, 0.3, 0.01, 1) \
    C(flpSync,   "Flanger / Phaser Sync", kListModSync, 0) \
    F(flpDepth,  "Flanger / Phaser Depth", 0, 1, 0.7, 0.01, 0) \
    F(flpFeedback, "Flanger / Phaser Feedback", -0.95, 0.95, 0.5, 0.01, 0) \
    F(flpManual, "Flanger / Phaser Manual", 0, 1, 0.5, 0.01, 0) \
    B(flpTZ,     "Flanger Through-Zero", false) \
    C(flpStages, "Phaser Stages", kListFlpStages, 0) \
    F(flpSpread, "Flanger / Phaser Stereo Spread", 0, 1, 0.5, 0.01, 0) \
    B(flpEnv,    "Flanger / Phaser Envelope Mode", false) \
    F(flpEnvSens, "Flanger / Phaser Envelope Sensitivity", 0, 1, 0.5, 0.01, 0) \
    C(vshRate,   "Volume Shaper Rate", kListVshRate, 0) \
    F(vshDepth,  "Volume Shaper Depth", 0, 1, 0.8, 0.01, 0) \
    F(vshSmooth, "Volume Shaper Smooth", 0, 1, 0.3, 0.01, 0) \
    C(vshTrig,   "Volume Shaper Trigger", kListVshTrig, 0) \
    B(sttMono,   "Bass Mono", true) \
    F(sttMonoFreq, "Bass Mono Below", 50, 300, 120, 1, -1) \
    F(sttWidth,  "Stereo Width", 0, 2, 1, 0.01, 0) \
    F(sttHaas,   "Haas Delay", 0, 30, 0, 0.1, 0) \
    C(lfo1Sync,   "LFO1 Sync", kListLfoSync, 0) \
    C(lfo1Retrig, "LFO1 Retrigger", kListLfoRetrig, 0) \
    F(lfo1Phase,  "LFO1 Phase", 0, 1, 0, 0.001, 0) \
    C(lfo1Mode,   "LFO1 Mode", kListLfoMode, 0) \
    F(lfo1Smooth, "LFO1 Smooth", 0, 1, 0, 0.01, 0) \
    B(lfo1Uni,    "LFO1 Unipolar", false) \
    F(lfo1Fade,   "LFO1 Fade In", 0, 10, 0, 0.01, 0.4) \
    B(lfo1Custom, "LFO1 Custom Shape", false) \
    C(lfo2Sync,   "LFO2 Sync", kListLfoSync, 0) \
    C(lfo2Retrig, "LFO2 Retrigger", kListLfoRetrig, 0) \
    F(lfo2Phase,  "LFO2 Phase", 0, 1, 0, 0.001, 0) \
    C(lfo2Mode,   "LFO2 Mode", kListLfoMode, 0) \
    F(lfo2Smooth, "LFO2 Smooth", 0, 1, 0, 0.01, 0) \
    B(lfo2Uni,    "LFO2 Unipolar", false) \
    F(lfo2Fade,   "LFO2 Fade In", 0, 10, 0, 0.01, 0.4) \
    B(lfo2Custom, "LFO2 Custom Shape", false) \
    C(lfo3Sync,   "LFO3 Sync", kListLfoSync, 0) \
    C(lfo3Retrig, "LFO3 Retrigger", kListLfoRetrig, 0) \
    F(lfo3Phase,  "LFO3 Phase", 0, 1, 0, 0.001, 0) \
    C(lfo3Mode,   "LFO3 Mode", kListLfoMode, 0) \
    F(lfo3Smooth, "LFO3 Smooth", 0, 1, 0, 0.01, 0) \
    B(lfo3Uni,    "LFO3 Unipolar", false) \
    F(lfo3Fade,   "LFO3 Fade In", 0, 10, 0, 0.01, 0.4) \
    B(lfo3Custom, "LFO3 Custom Shape", false) \
    C(lfo4Sync,   "LFO4 Sync", kListLfoSync, 0) \
    C(lfo4Retrig, "LFO4 Retrigger", kListLfoRetrig, 0) \
    F(lfo4Phase,  "LFO4 Phase", 0, 1, 0, 0.001, 0) \
    C(lfo4Mode,   "LFO4 Mode", kListLfoMode, 0) \
    F(lfo4Smooth, "LFO4 Smooth", 0, 1, 0, 0.01, 0) \
    B(lfo4Uni,    "LFO4 Unipolar", false) \
    F(lfo4Fade,   "LFO4 Fade In", 0, 10, 0, 0.01, 0.4) \
    B(lfo4Custom, "LFO4 Custom Shape", false) \
    B(chdOn,       "Chord Memory On", false) \
    C(chdPreset,   "Chord Memory Chord", kListChdPreset, 1) \
    F(chdSpread,   "Chord Memory Spread", 0, 3, 0, 1, 0) \
    F(chdInversion, "Chord Memory Inversion", -3, 3, 0, 1, 0) \
    B(arpOn,       "Arpeggiator On", false) \
    C(arpMode,     "Arpeggiator Mode", kListArpMode, 0) \
    F(arpOctaves,  "Arpeggiator Octaves", 1, 4, 1, 1, 0) \
    C(arpRate,     "Arpeggiator Rate", kListArpRate, 2) \
    F(arpSwing,    "Arpeggiator Swing", 0, 0.75, 0, 0.01, 0) \
    F(arpGate,     "Arpeggiator Gate", 0.05, 1, 0.5, 0.01, 0) \
    B(arpLatch,    "Arpeggiator Latch", false) \
    C(arpVelMode,  "Arpeggiator Velocity", kListArpVel, 0) \
    F(arpVelFixed, "Arpeggiator Fixed Velocity", 0.05, 1, 0.8, 0.01, 0) \
    F(arpRepeats,  "Arpeggiator Repeats", 1, 4, 1, 1, 0) \
    C(arpRestart,  "Arpeggiator Restart", kListArpRestart, 0) \
    F(arpSteps,    "Arpeggiator Pattern Steps", 1, 16, 16, 1, 0) \
    B(vshSidechain, "Volume Shaper Sidechain Trigger", false) \
    B(mbcSidechain, "Multiband Sidechain", false) \
    F(schThresh,   "Sidechain Threshold", -60, 0, -24, 0.1, 0) \
    F(schAttack,   "Sidechain Attack", 0.1, 50, 1, 0.1, 5) \
    F(schRelease,  "Sidechain Release", 10, 500, 80, 1, 80) \
    F(uni1Voices, "Osc1 Unison Voices", 1, 8, 1, 1, 0) \
    F(uni1Detune, "Osc1 Unison Detune", 0, 100, 20, 0.1, 0) \
    F(uni1Spread, "Osc1 Unison Spread", 0, 1, 0.5, 0.01, 0) \
    F(uni1Blend,  "Osc1 Unison Blend", 0, 1, 0.5, 0.01, 0) \
    C(uni1Curve,  "Osc1 Unison Detune Curve", kListUniCurve, 0) \
    F(uni2Voices, "Osc2 Unison Voices", 1, 8, 1, 1, 0) \
    F(uni2Detune, "Osc2 Unison Detune", 0, 100, 20, 0.1, 0) \
    F(uni2Spread, "Osc2 Unison Spread", 0, 1, 0.5, 0.01, 0) \
    F(uni2Blend,  "Osc2 Unison Blend", 0, 1, 0.5, 0.01, 0) \
    C(uni2Curve,  "Osc2 Unison Detune Curve", kListUniCurve, 0) \
    F(uni3Voices, "Osc3 Unison Voices", 1, 8, 1, 1, 0) \
    F(uni3Detune, "Osc3 Unison Detune", 0, 100, 20, 0.1, 0) \
    F(uni3Spread, "Osc3 Unison Spread", 0, 1, 0.5, 0.01, 0) \
    F(uni3Blend,  "Osc3 Unison Blend", 0, 1, 0.5, 0.01, 0) \
    C(uni3Curve,  "Osc3 Unison Detune Curve", kListUniCurve, 0) \
    F(uni4Voices, "WT1 Unison Voices", 1, 8, 1, 1, 0) \
    F(uni4Detune, "WT1 Unison Detune", 0, 100, 20, 0.1, 0) \
    F(uni4Spread, "WT1 Unison Spread", 0, 1, 0.5, 0.01, 0) \
    F(uni4Blend,  "WT1 Unison Blend", 0, 1, 0.5, 0.01, 0) \
    C(uni4Curve,  "WT1 Unison Detune Curve", kListUniCurve, 0) \
    F(uni5Voices, "WT2 Unison Voices", 1, 8, 1, 1, 0) \
    F(uni5Detune, "WT2 Unison Detune", 0, 100, 20, 0.1, 0) \
    F(uni5Spread, "WT2 Unison Spread", 0, 1, 0.5, 0.01, 0) \
    F(uni5Blend,  "WT2 Unison Blend", 0, 1, 0.5, 0.01, 0) \
    C(uni5Curve,  "WT2 Unison Detune Curve", kListUniCurve, 0) \
    F(uni6Voices, "Complex Unison Voices", 1, 8, 1, 1, 0) \
    F(uni6Detune, "Complex Unison Detune", 0, 100, 20, 0.1, 0) \
    F(uni6Spread, "Complex Unison Spread", 0, 1, 0.5, 0.01, 0) \
    F(uni6Blend,  "Complex Unison Blend", 0, 1, 0.5, 0.01, 0) \
    C(uni6Curve,  "Complex Unison Detune Curve", kListUniCurve, 0) \
    B(uniSamePhase, "Unison Same Phase Every Note", false) \
    F(pEnvAmt,     "Pitch Env Amount", -48, 48, 0, 0.1, 0) \
    F(pEnvA,       "Pitch Env Attack", 0, 2, 0, 0.001, 0.2) \
    F(pEnvD,       "Pitch Env Decay", 0.001, 3, 0.15, 0.001, 0.3) \
    F(pEnvCurve,   "Pitch Env Curve", -1, 1, 0.5, 0.01, 0) \
    C(pEnvTarget,  "Pitch Env Target", kListPEnvTarget, 0) \
    F(pEnvVel,     "Pitch Env Velocity", 0, 1, 0, 0.01, 0) \
    B(mseg1Sync,   "MSEG 1 Sync", false) \
    F(mseg1Scale,  "MSEG 1 Time Scale", 0.25, 4, 1, 0.01, 1) \
    B(mseg2Sync,   "MSEG 2 Sync", false) \
    F(mseg2Scale,  "MSEG 2 Time Scale", 0.25, 4, 1, 0.01, 1) \
    B(wheelLfo1,   "Mod Wheel to LFO 1 Depth", true) \
    B(sustainPedal, "Sustain Pedal", true) \
    B(mfOn,        "Master Filter On", false) \
    C(mfType,      "Master Filter Type", kListMfType, 0) \
    C(mfSlope,     "Master Filter Slope", kListMfSlope, 0) \
    F(mfCutoff,    "Master Filter Cutoff", 20, 20000, 1000, 0.1, 1000) \
    F(mfRes,       "Master Filter Resonance", 0, 1, 0.1, 0.01, 0) \
    F(mfDrive,     "Master Filter Drive", 0, 24, 0, 0.1, 0) \
    F(mfMix,       "Master Filter Mix", 0, 1, 1, 0.01, 0) \
    F(outGain,     "Output", -24, 12, 0, 0.1, 0) \
    F(mxOsc1Pan,  "Osc 1 Pan", -1, 1, 0, 0.01, 0) \
    B(mxOsc1Mute, "Osc 1 Mute", false) \
    B(mxOsc1Solo, "Osc 1 Solo", false) \
    F(mxOsc2Pan,  "Osc 2 Pan", -1, 1, 0, 0.01, 0) \
    B(mxOsc2Mute, "Osc 2 Mute", false) \
    B(mxOsc2Solo, "Osc 2 Solo", false) \
    F(mxOsc3Pan,  "Osc 3 Pan", -1, 1, 0, 0.01, 0) \
    B(mxOsc3Mute, "Osc 3 Mute", false) \
    B(mxOsc3Solo, "Osc 3 Solo", false) \
    F(mxSubPan,  "Sub Pan", -1, 1, 0, 0.01, 0) \
    B(mxSubMute, "Sub Mute", false) \
    B(mxSubSolo, "Sub Solo", false) \
    F(mxWt1Pan,  "WT 1 Pan", -1, 1, 0, 0.01, 0) \
    B(mxWt1Mute, "WT 1 Mute", false) \
    B(mxWt1Solo, "WT 1 Solo", false) \
    F(mxWt2Pan,  "WT 2 Pan", -1, 1, 0, 0.01, 0) \
    B(mxWt2Mute, "WT 2 Mute", false) \
    B(mxWt2Solo, "WT 2 Solo", false) \
    F(mxComplexPan,  "Complex Pan", -1, 1, 0, 0.01, 0) \
    B(mxComplexMute, "Complex Mute", false) \
    B(mxComplexSolo, "Complex Solo", false) \
    F(mxSupersawPan,  "SuperSaw Pan", -1, 1, 0, 0.01, 0) \
    B(mxSupersawMute, "SuperSaw Mute", false) \
    B(mxSupersawSolo, "SuperSaw Solo", false) \
    F(mxRingPan,  "Ring Pan", -1, 1, 0, 0.01, 0) \
    B(mxRingMute, "Ring Mute", false) \
    B(mxRingSolo, "Ring Solo", false) \
    C(flt1Char,    "Filter Character", kListCharModel, 0) \
    F(flt1CharAmt, "Filter Character Amount", 0, 1, 0.5, 0.01, 0) \
    C(flt2Char,    "Filter 2 Character", kListCharModel, 0) \
    F(flt2CharAmt, "Filter 2 Character Amount", 0, 1, 0.5, 0.01, 0) \
    C(mfChar,      "Master Filter Character", kListCharModel, 0) \
    F(mfCharAmt,   "Master Filter Character Amount", 0, 1, 0.5, 0.01, 0) \
    B(pfRoll,      "Perf Beat Repeat", false) \
    B(pfReverse,   "Perf Reverse", false) \
    B(pfTapeStop,  "Perf Tape Stop", false) \
    B(pfTapeStart, "Perf Tape Start", false) \
    B(pfBrake,     "Perf Backspin", false) \
    B(pfPitchDown, "Perf Pitch Down", false) \
    B(pfStutter,   "Perf Stutter Gate", false) \
    B(pfGate,      "Perf Trance Gate", false) \
    B(pfDj,        "Perf DJ Filter", false) \
    B(pfCrush,     "Perf Bitcrush", false) \
    B(pfVinyl,     "Perf Vinyl", false) \
    B(pfEcho,      "Perf Echo Throw", false) \
    B(pfVerb,      "Perf Reverb Throw", false) \
    C(pfQuant,     "Perf Quantise", kListPfQuant, 1) \
    B(pfLatch,     "Perf Latch", false) \
    C(pfKeys,      "Perf MIDI Keys", kListPfKeys, 0) \
    C(pfRollRate,  "Perf Repeat Length", kListPfRoll, 2) \
    F(pfTapeTime,  "Perf Tape Time", 0.25, 4, 1, 0.01, 0) \
    F(pfPitchGlide,"Perf Pitch Glide", 0, 2, 0.3, 0.01, 0) \
    F(pfPitchAmt,  "Perf Pitch Amount", -24, 0, -12, 0.1, 0) \
    C(pfStutterRate,"Perf Stutter Rate", kListPfStut, 1) \
    C(pfGatePattern,"Perf Gate Pattern", kListPfGate, 2) \
    F(pfGateDepth, "Perf Gate Depth", 0, 1, 1, 0.01, 0) \
    F(pfDjPos,     "Perf DJ Filter Position", -1, 1, -0.6, 0.01, 0) \
    F(pfDjRes,     "Perf DJ Filter Resonance", 0, 1, 0.3, 0.01, 0) \
    F(pfCrushBits, "Perf Crush Bits", 2, 16, 6, 0.01, 0) \
    F(pfCrushRate, "Perf Crush Rate", 1000, 48000, 8000, 1, -1) \
    F(pfVinylAmt,  "Perf Vinyl Amount", 0, 1, 0.6, 0.01, 0) \
    C(pfEchoTime,  "Perf Echo Time", kListPfEcho, 2) \
    F(pfEchoFb,    "Perf Echo Feedback", 0, 0.95, 0.6, 0.01, 0) \
    F(pfEchoMix,   "Perf Echo Level", 0, 1, 0.7, 0.01, 0) \
    F(pfVerbSize,  "Perf Reverb Size", 0, 1, 0.8, 0.01, 0) \
    F(pfVerbMix,   "Perf Reverb Level", 0, 1, 0.6, 0.01, 0) \
    B(pfVerbFreeze,"Perf Reverb Freeze", false) \
    F(pfX1,        "Perf XY 1 X", 0, 1, 0.5, 0.001, 0) \
    F(pfY1,        "Perf XY 1 Y", 0, 1, 0.5, 0.001, 0) \
    F(pfX2,        "Perf XY 2 X", 0, 1, 0.5, 0.001, 0) \
    F(pfY2,        "Perf XY 2 Y", 0, 1, 0.5, 0.001, 0) \
    B(pfSpring1,   "Perf XY 1 Spring", true) \
    B(pfSpring2,   "Perf XY 2 Spring", true) \
    B(gsOn,        "Granular Synth On", false) \
    F(gsLevel,     "Granular Synth Level", 0, 1, 0.7, 0.01, 0) \
    F(gsPosition,  "Granular Position", 0, 1, 0.25, 0.001, 0) \
    F(gsScan,      "Granular Scan", -2, 2, 0, 0.01, 0) \
    F(gsSpray,     "Granular Spray", 0, 500, 20, 0.1, 60) \
    F(gsSize,      "Granular Grain Size", 5, 500, 80, 0.1, -1) \
    F(gsDensity,   "Granular Density", 1, 100, 20, 0.1, -1) \
    C(gsShape,     "Granular Grain Shape", kListGsShape, 0) \
    F(gsSemi,      "Granular Pitch", -24, 24, 0, 1, 0) \
    F(gsCents,     "Granular Fine", -100, 100, 0, 0.1, 0) \
    F(gsPitchRand, "Granular Pitch Random", 0, 12, 0, 0.01, 0) \
    F(gsReverse,   "Granular Reverse", 0, 1, 0, 0.01, 0) \
    F(gsSpread,    "Granular Stereo Spread", 0, 1, 0.3, 0.01, 0) \
    F(gsRoot,      "Granular Root Note", 24, 96, 60, 1, 0) \
    B(gsKeyTrack,  "Granular Key Track", true) \
    C(gsDna,       "Granular as DNA Source", kListGsDna, 0) \
    F(mxGranPan,   "Granular Pan", -1, 1, 0, 0.01, 0) \
    B(mxGranMute,  "Granular Mute", false) \
    B(mxGranSolo,  "Granular Solo", false) \
    B(bcOn,        "Break Chopper On", false) \
    F(bcLevel,     "Break Level", 0, 1, 0.8, 0.01, 0) \
    C(bcBars,      "Break Bars", kListBcBars, 0) \
    F(bcTempo,     "Break Original Tempo", 0, 250, 0, 0.01, 0) \
    C(bcSliceMode, "Break Slicing", kListBcSlice, 0) \
    F(bcSens,      "Break Transient Sensitivity", 0, 1, 0.5, 0.01, 0) \
    F(bcKeyBase,   "Break First Key", 0, 108, 36, 1, 0) \
    C(bcStretch,   "Break Stretch", kListBcStretch, 1) \
    B(bcSync,      "Break Tempo Sync", true) \
    F(bcCycle,     "Break Cycle Length", 10, 200, 40, 0.1, 50) \
    F(bcXfade,     "Break Crossfade", 0, 1, 0.35, 0.01, 0) \
    F(bcPitch,     "Break Pitch", -24, 24, 0, 0.01, 0) \
    F(bcVelSens,   "Break Velocity", 0, 1, 0.5, 0.01, 0) \
    C(bcColour,    "Break Colour", kListBcColour, 0) \
    B(bcThruFilter,"Break Through Synth Filter", false) \
    F(mxBreakPan,  "Break Pan", -1, 1, 0, 0.01, 0) \
    B(mxBreakMute, "Break Mute", false) \
    B(mxBreakSolo, "Break Solo", false) \
    B(bsOn,        "Break Sequencer Run", false) \
    C(bsPattern,   "Break Pattern", kListBsPat, 0) \
    C(bsSteps,     "Break Pattern Length", kListBsSteps, 0) \
    C(bsSwitch,    "Break Pattern Switch", kListBsSwitch, 2) \
    B(bsChainOn,   "Break Pattern Chain", false) \
    C(bsKeys,      "Break Pattern Keys", kListBsKeys, 0) \
    F(bsSwing,     "Break Swing", 0, 0.75, 0, 0.01, 0) \
    F(bcActive,    "Active Break", 1, 3, 1, 1, 0) \
    B(bcSelKeys,   "Break Select Keys", true) \
    B(bcSelQuant,  "Break Select on the Beat", false) \
    F(bcLevel1,    "Break 1 Level", 0, 2, 1, 0.01, 0) \
    F(bcPan1,      "Break 1 Pan", -1, 1, 0, 0.01, 0) \
    C(bcSliceMode2, "Break 2 Slicing", kListBcSlice, 0) \
    F(bcSens2,   "Break 2 Transient Sensitivity", 0, 1, 0.5, 0.01, 0) \
    C(bcBars2,   "Break 2 Bars", kListBcBars, 0) \
    F(bcTempo2,  "Break 2 Original Tempo", 0, 250, 0, 0.01, 0) \
    C(bcStretch2,"Break 2 Stretch", kListBcStretch, 1) \
    C(bcColour2, "Break 2 Colour", kListBcColour, 0) \
    F(bcLevel2,  "Break 2 Level", 0, 2, 1, 0.01, 0) \
    F(bcPan2,    "Break 2 Pan", -1, 1, 0, 0.01, 0) \
    C(bcSliceMode3, "Break 3 Slicing", kListBcSlice, 0) \
    F(bcSens3,   "Break 3 Transient Sensitivity", 0, 1, 0.5, 0.01, 0) \
    C(bcBars3,   "Break 3 Bars", kListBcBars, 0) \
    F(bcTempo3,  "Break 3 Original Tempo", 0, 250, 0, 0.01, 0) \
    C(bcStretch3,"Break 3 Stretch", kListBcStretch, 1) \
    C(bcColour3, "Break 3 Colour", kListBcColour, 0) \
    F(bcLevel3,  "Break 3 Level", 0, 2, 1, 0.01, 0) \
    F(bcPan3,    "Break 3 Pan", -1, 1, 0, 0.01, 0) \
    C(bsAlternate, "Break Alternate", kListBsAlt, 0) \
    C(bsAltBreak,  "Break Alternate With", kListBsAltBreak, 1) \
    F(bsAmount,    "Break Tool Amount", 0, 1, 0.6, 0.01, 0) \
    B(bcChopMode,  "Break Chop Mode", false) \
    B(bcChopSnapTr,"Break Chop Snap to Transients", true) \
    B(bcChopSnapZero,"Break Chop Snap to Zero Crossings", true) \
    F(bcChopLatency,"Break Chop Tap Latency", 0, 100, 10, 0.1, 0) \
    C(bcMarkerSnap,"Break Marker Snap", kListBcSnap, 1) \
    C(bsToolSource,"Break Tool Source", kListBsToolSrc, 0) \
    F(bsVarAmount, "Break Variation Amount", 0, 1, 0.3, 0.01, 0) \
    B(bsFill,      "Break Fill", false) \
    F(bsFillKey,   "Break Fill Key", -1, 127, -1, 1, 0) \
    C(bsFillMode,  "Break Fill Mode", kListBsFillMode, 0) \
    C(bsFillLen,   "Break Fill Length", kListBsFillLen, 2) \
    C(bsFillStart, "Break Fill Start", kListBsFillStart, 1) \
    C(bsFillReturn,"Break Fill Return", kListBsFillRet, 0) \
    C(bsAutoFill,  "Break Auto Fill", kListBsAutoFill, 0) \
    F(bsAutoFillProb,"Break Auto Fill Chance", 0, 1, 1, 0.01, 0)

enum ParamIndex
{
#define TG_F(id, name, mn, mx, df, st, sk) P_##id,
#define TG_C(id, name, list, df) P_##id,
#define TG_B(id, name, df) P_##id,
#define TG_A(id, name, df) P_##id,
    TG_PARAMS(TG_F, TG_C, TG_B, TG_A)
#undef TG_F
#undef TG_C
#undef TG_B
#undef TG_A
    P_COUNT
};

inline const char* const kParamIds[] = {
#define TG_F(id, name, mn, mx, df, st, sk) #id,
#define TG_C(id, name, list, df) #id,
#define TG_B(id, name, df) #id,
#define TG_A(id, name, df) #id,
    TG_PARAMS(TG_F, TG_C, TG_B, TG_A)
#undef TG_F
#undef TG_C
#undef TG_B
#undef TG_A
};

// a Mixer strip's Pan parameter (Mute and Solo follow it)
inline constexpr int mxParam (int strip) { return strip < kMxSources ? P_mxOsc1Pan + kMxStride * strip : (strip == kMxSources ? P_mxGranPan : P_mxBreakPan); }

// The Break Chopper's per-break parameters (break k = 0, 1, 2)
struct BreakParamSet { int sliceMode, sens, bars, tempo, stretch, colour, level, pan; };
inline BreakParamSet breakParams (int k)
{
    if (k <= 0) return { P_bcSliceMode, P_bcSens, P_bcBars, P_bcTempo, P_bcStretch, P_bcColour, P_bcLevel1, P_bcPan1 };
    const int b = k == 1 ? P_bcSliceMode2 : P_bcSliceMode3;
    return { b, b + 1, b + 2, b + 3, b + 4, b + 5, b + 6, b + 7 };
}

} // namespace tg
