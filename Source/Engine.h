#pragma once
#include <JuceHeader.h>
#include <memory>
#include <atomic>
#include "Params.h"
#include "DSP.h"
#include "ModMatrix.h"
#include "Mut/WaveMutator.h"
#include "Mut/AudioRate.h"
#include "Mut/DnaSplice.h"
#include "Mut/Resonator.h"
#include "Reese.h"
#include "CoreKernel.h"
#include "BreakReact.h"
#include "Fx/MasterFilter.h"
#include "Fx/PerfFx.h"
#include "GranSynth.h"
#include "Break/BreakChopper.h"
#include "Mut/Granular.h"
#include "Mut/Spectral.h"
#include "Mut/FeedbackMatrix.h"
#include "Mut/Instability.h"
#include "Mut/Mutator.h"
#include "Seq/DnaSequencer.h"
#include "Filter/Filter.h"
#include "EngineTypes.h"
#include "Fx/RackFx.h"
#include "Fx/FxBus.h"
#include "LfoShapes.h"
#include "Mseg.h"

namespace tg
{
void resetVoiceRandom();   // the shared per-voice random sequence back to its start (tests)

// Snapshot, WaveSample, ModState and the sync helpers live in EngineTypes.h; FxBus in Fx/FxBus.h.


//==============================================================================
// (FilterChain, the classic filter models, lives in Filter/Filter.h)

// What the voices need from the modulation matrix for one block.
struct ModContext
{
    const RouteSet* routes = nullptr;
    const GlobalModInputs* in = nullptr;
    const SceneStore* scenes = nullptr;
    bool morph = false;      // scene morph on: routes onto Scene X/Y re-morph this voice's parameters
    const MutationTable* mut = nullptr;   // set when routes move the Mutate amount: each voice mutates itself
    uint32_t lockMask = 0;
    float dnaSeq = 0.0f, dnaSeq2 = 0.0f, dnaGate = 0.0f;   // the DNA Sequencer's lanes and step gate (Mod Matrix sources)
    const LfoShapeStore* lfoShapes = nullptr;   // custom LFO shapes
    const double* lfoFree = nullptr;            // shared "Free" LFO phases (Hz mode) at the start of this render call
    const MsegStore* msegs = nullptr;           // multi-stage envelope shapes
    bool meters = false;                        // the Mixer is on screen: measure each source's level
    const BreakReact* rx = nullptr;             // Break-Reactive mode on: this call's hits and envelope
    bool any() const { return routes != nullptr && routes->n > 0; }
};

//==============================================================================
class Voice
{
public:
    void prepare (double sr);

    struct StartOptions
    {
        float accentBoost = 0.0f;
        float filterAccent = 0.0f;
        float velocity = 1.0f;
        int channel = 1;        // MIDI channel (for MPE sources)
        int note = -1;          // MIDI note number; derived from the frequency when -1
    };

    void start (int key, double freq, double glideFromFreq, const StartOptions&, const Snapshot&, uint64_t order,
                const ModContext& = {});
    void release();
    void retune (double freq) { baseFreq = freq; }
    void kill() { active = false; }

    // Adds this voice's output into L/R. Writes its modulation values to modOut, and (when
    // liveOut is given) how far the matrix moved each destination, in normalised units.
    void render (float* L, float* R, int numSamples, const Snapshot&, const WaveSample* const* wavs, ModState& modOut,
                 const ModContext& = {}, float* liveOut = nullptr);

    float srcV[MS_COUNT] {};   // this voice's modulation sources (control rate)
    // Sub Route: the dry sub buses (0: before the Master Filter, 1: after it), set by the engine; the
    // voice adds its sub there (after its amp envelope and Mixer strip, skipping the filter and the bus)
    float* subOut[2][2] {};
    bool subDryUsed = false;

    bool active = false;
    bool released = false;
    int key = -1;
    uint64_t order = 0;
    double baseFreq = 440.0;
    float accentBoost = 0.0f;
    float filterAccent = 0.0f;
    float velGain = 1.0f;

    float currentLevel() const { return (float) ampEnv.eval (t); }
    float lfoPhaseShown (int k) const { return (float) lfoPhNow[k & 3]; }
    float msegShown (int k) const { return mseg[k & 1].position(); }
    float msegLevel (int k) const { return mseg[k & 1].level; }

private:
    float subRG[3] { 1.0f, 0.0f, 0.0f };   // Sub Route gains (normal, dry before, dry after), ramped over 10 ms
    void computeSources (const Snapshot&, const GlobalModInputs*, float dt);
    void advanceLfos (const Snapshot&, int numSamples);
    void updateLfoOut (const Snapshot&, int offset, const ModContext&);   // the LFOs' outputs for this control step
    void computeMod (const Snapshot&, ModState&) const;
    void updateControl (const Snapshot&, const WaveSample* const* wavs, const ModState&, bool first);

    double sr = 48000.0;
    double t = 0.0;            // seconds since note-on
    double startedAt = 0.0;    // random seed for SuperSaw drift
    EnvState ampEnv, filtEnv, modEnv[3];

    // smoothed values (Web Audio setTargetAtTime emulation)
    struct S { float v = 0, target = 0; inline void step (float c) { v += (target - v) * c; } };
    S f1, f2, f3, fSub, cBase, cRatio, ssBase;
    S l1, l2, l3, lSub, lC, lSS;
    // Mixer strips (Osc 1, 2, 3, Sub, WT 1, WT 2, Complex, SuperSaw, Ring): balance pan and mute / solo.
    // Only while one is away from centre / unmuted does the mix use them (otherwise it is exactly as before).
    S mxPan[kMxStrips], mxGate[kMxStrips];
    float mxL[kMxStrips] {}, mxR[kMxStrips] {};
    bool mxActive = false;
public:
    float mxPeak[kMxStrips] {};   // the level of each source in the last block (when the Mixer is showing)
    int granGrains() const { return gsRun ? gran.activeGrains() : 0; }
    double granPlayhead() const { return gsRun ? gran.playhead() : -1.0; }
private:
    void updateMixer (const Snapshot&, bool first, float c);

    // Sample / wavetable oscillators (Osc 4 = WT 1, and WT 2)
    struct WtPlayer
    {
        const WaveSample* wav = nullptr;
        bool loaded = false;
        S rate, level;
        double pos = -1.0;
        bool stopped = false, loop = true, reverse = false;
        double ls = 0, le = 0;
        float comp = 1.0f;

        inline void tick (double fmRate, double isr, float& outL, float& outR)
        {
            outL = outR = 0.0f;
            if (! loaded || stopped) return;
            const int wlen = wav->length;
            const float* wl = wav->ch[0].data();
            const float* wr = wav->ch[wav->numChannels > 1 ? 1 : 0].data();
            int i0 = (int) std::floor (pos);
            const float fr = (float) (pos - i0);
            int i1 = i0 + 1;
            i0 = clampv (i0, 0, wlen - 1); i1 = clampv (i1, 0, wlen - 1);
            if (reverse) { i0 = wlen - 1 - i0; i1 = wlen - 1 - i1; }
            outL = wl[i0] + (wl[i1] - wl[i0]) * fr;
            outR = wr[i0] + (wr[i1] - wr[i0]) * fr;
            const double inc = ((double) rate.v + fmRate) * wav->sampleRate * isr;
            pos += inc;
            if (loop)
            {
                const double span = std::max (1.0e-6, le - ls);
                if (pos >= le) pos = ls + std::fmod (pos - ls, span);
                else if (pos < ls && inc < 0) pos = le - std::fmod (ls - pos, span);
            }
            else if (pos >= wlen || pos < 0) stopped = true;
        }
    };
    WtPlayer wt[2];
    // the Granular Synth (sample slot 2)
    GranVoice gran;
    ReeseOsc reese;          // the Reese engine
    S lRs;                   // its level (smoothed)
    bool rsRun = false;      // on, or still fading out
    // the Core oscillator slots (A, B)
    CoreKernel core[2];
    CoreSettings coreSet[2];
    S lCore[2];              // their levels (smoothed)
    bool coreRun[2] {};      // on, or still fading out
    double coreHz[2] {};
    // Break-Reactive mode: Osc 1-3 and the sub's old phases, faded out just after a phase reset
    double rxOldPh[4] {};
    int rxXf[4] {};
    float rxDuckSm = 1.0f;   // the level duck, smoothed
    GranVoice::Params gsP;
    const WaveSample* gsWav = nullptr;
    S lGs;
    bool gsRun = false;      // on, with a sample, or still fading out
    int gsDnaSlot = 0;       // 1 / 2: stands in for DNA Splice source A / B
    // ---- unison: up to 7 extra (side) voices per oscillator: Osc 1-3, WT 1, WT 2, Complex
    static constexpr int kUniSide = 7;
    int uniN[kUniOscs] { 1, 1, 1, 1, 1, 1 };              // voices (1 = off: the oscillator as it always was)
    float uniRatio[kUniOscs][kUniSide] {}, uniPL[kUniOscs][kUniSide] {}, uniPR[kUniOscs][kUniSide] {};
    float uniGc[kUniOscs] { 1, 1, 1, 1, 1, 1 }, uniGs[kUniOscs] {};   // centre and side gains
    double uniPh[3][kUniSide] {}, uniPhCar[kUniSide] {}, uniPhMod[kUniSide] {};
    WtPlayer wtU[2][kUniSide];
    bool uniAny = false;
    void updateUnison (const Snapshot&);
    void startUnison (const Snapshot&);
    void updateWt (int slot, const Snapshot&, const ModState&, double bendC, float cPort, float c10);
    S legacyFm, ringMix, dry, ringGainS, srcMute, vGain;
    S cFm, cMixS;
    S cutoff, res, filtDrive, filtMix;
    S cutoff2, res2, filtDrive2, filtMix2, balS;
    S fmAmt[4], ringDepth[2], ringOut[2];
    S ssCents[9], ssGain[9];
    float cShapeK = 4.5f;
    int ssVoices = 7;
    float ssPanL[9] {}, ssPanR[9] {};
    double ssDriftSin[9] {};
    float panWidth = -1.0f; int panVoices = -1;

    // oscillator state
    double ph1 = 0, ph2 = 0, ph3 = 0, phSub = 0, phCar = 0, phMod = 0;
    double ssPh[9] {};
    double driftPh[5] {}; float driftRate[5] {}; float driftCents[5] {};   // Analog Drift (osc1-3, sub, complex)
    int wave1 = 0, wave2 = 0, wave3 = 0, waveSub = 0, waveA = 0, waveB = 0;

    // routing (resolved per control block)
    struct FmSlot { int src, dst; bool on; float scale; };
    FmSlot fmSlots[4] {};
    struct RingSlot { int src, dst; bool on; float shapeK; Biquad hp, lp; };
    RingSlot ringSlots[2];

    float srcVals[6] {};    // latest raw/FM-source outputs: osc1, osc2, osc3, osc4(mono), complex, supersaw
    FilterUnit filter;      // Filter 1
    FilterUnit filter2;     // Filter 2
    FilterUnit f2old;       // Filter 2 as it was, while a routing change crossfades
    enum { R_Off, R_Serial, R_Parallel, R_Split };
    int routeMode = R_Off, oldRouteMode = R_Off, routeFade = 0;
    bool filter2Fresh = true, jumpF2 = true;
    // Filter 2's place: after Filter 1 (serial) or beside it (parallel / stereo split, weighted by Balance)
    inline void route (int m, FilterUnit& f2, float xL, float xR, float y1L, float y1R, float& oL, float& oR)
    {
        if (m == R_Off) { oL = y1L; oR = y1R; return; }
        if (m == R_Serial) { f2.processFrame (y1L, y1R, stereo, oL, oR); return; }
        float bL, bR;
        f2.processFrame (xL, xR, stereo, bL, bR);
        const float b = balS.v;
        const float gA = std::min (1.0f, 2.0f * (1.0f - b)), gB = std::min (1.0f, 2.0f * b);   // centre: both at full
        if (m == R_Split) { oL = y1L * gA; oR = bR * gB; return; }
        oL = y1L * gA + bL * gB; oR = y1R * gA + bR * gB;
    }
    bool stereo = false;

    // ---- modulation matrix state
    int midiNote = 60, channel = 1;
    float velocity = 1.0f;
    double lfoPh[4] {};          // LFO phases (free per voice, reset at note-on)
    float lfoDepthNow[4] {};
    double lfoElapsed[4] {};     // cycles since note-on (one-shot)
    double lfoPhNow[4] {};       // the phase actually used this step (for the editor's playhead)
    float lfoOut[4] {};          // shaped output (before depth) this control step
    float lfoSm[4] {};           // smoothing state
    bool lfoSmInit[4] {};
    MsegRun mseg[2];             // the two multi-stage envelopes
    float peRatio[8] { 1, 1, 1, 1, 1, 1, 1, 1 };   // pitch envelope as a frequency ratio: osc 1-3, sub, WT 1, WT 2, complex, supersaw
    void updatePitchEnv (const Snapshot&);
    float lfoXMod[4][3] {};      // Phase, Smooth and Fade In as modulated last step (they shape this step's output)
    uint32_t rng = 1;
    float randNote = 0.0f;
    double rndPh = 0.0, driftPh2 = 0.0;
    float rndA = 0, rndB = 0, shVal = 0, chaosX = 0.5f, drA = 0, drB = 0;
    float envF = 0, audF = 0, trFast = 0, trSlow = 0;
    float lastDt = 0.0f;
    float aPrev[6] {};           // previous sample of Osc 1, 2, 3, Sub, Core A and Core B, for audio-rate routes
    RouteState rstate;
    Snapshot modSnap;

    // ---- mutation modules (between the mixer and the filter)
    DnaSplice dna;
    DnaParams dnaCur, dnaPrev;   // the running splice, and the one it is fading from
    int dnaFade = 0;             // samples left in a mode / source crossfade
    bool dnaStarted = false;
    WaveMutator waveMut;
    Resonator reso;
    Instability inst;
    void applyInstability (Snapshot&, bool noteOn) const;
    bool resoRinging = false;
    AudioRateFx arFx;
    double arPh = 0.0;           // internal sine modulator phase            // the parameters with this voice's modulation applied
    float nextRand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 8388608.0f - 1.0f; }
};


//==============================================================================
class Engine
{
public:
    static constexpr int kMaxVoices = 32;

    void prepare (double sr, int maxBlock);
    void reset();

    void noteOn (int key, double freq, const Voice::StartOptions&, const Snapshot&);
    void noteOff (int key);
    void allNotesOff();
    Voice* findActive (int key);    // held (not released) voice for key
    bool isHeld (int key) const { for (auto& v : voices) if (v.active && ! v.released && v.key == key) return true; return false; }
    void rekey (Voice* v, int newKey) { if (v) v->key = newKey; }

    void render (float* L, float* R, int numSamples, const Snapshot&, const WaveSample* const* wavs);

    // Modulation matrix inputs (owned by the processor; read on the audio thread)
    void setModulation (const RouteStore* r, const GlobalModInputs* in, const SceneStore* sc = nullptr) { routeStore = r; modIn = in; scenes = sc; }
    // For the editor: how far each destination is currently moved (normalised), and source values
    std::array<std::atomic<float>, P_COUNT> liveOffset {};
    std::array<std::atomic<float>, MS_COUNT> liveSrc {};

    FxBus& fx() { return fxBus; }
    const FxBus& fx() const { return fxBus; }
    PerfFx& perf() { return perfFx; }
    BreakChopper& breaks() { return chopper; }
    const BreakChopper& breaks() const { return chopper; }
    void setBreakStore (const BreakStore* st) { breakStore = st; }
    void pullBreak (const WaveSample* const* w) { if (breakStore != nullptr) breakStore->pull (chopper.data(), breakSeen); for (int k = 0; k < BreakChopper::kBreaks; ++k) chopper.setSample (k, w[k]); }
    BreakChopper::Settings breakSettings (const Snapshot& s) const;   // the chopper's settings from the parameters
    std::atomic<double> breakPlayheadShown { -1.0 };
    std::atomic<int> breakSliceShown { -1 };
    const PerfFx& perf() const { return perfFx; }
    const Granular& granular() const { return gran; }
    // DNA Sequencer: the step store, and its position (in steps) at the middle of the next render call
    void setDnaSequencer (const DnaSeqStore* st) { dnaStore = st; }
    void setDnaPosition (double steps) { dsPos = steps; }
    std::atomic<int> dnaSeqStep { -1 }, dnaSeqStep2 { -1 }, dnaPatternPlaying { 0 };
    // The DNA clock runs in the processor before the engine renders, so its modulated settings
    // (Swing, Start Offset, Free Rate) use the Mod Matrix offset from the previous block. 0 when unmodulated.
    enum DnaClockMod { DCM_Swing, DCM_Offset, DCM_FreeHz };
    float dnaClockModOffset (int which) const { return dsClockMod[juce::jlimit (0, 2, which)]; }
    void loadGranular (const float* l, const float* r, int n) { gran.load (l, r, n); }
    // the bus signal just before the effects, for Capture ("before effects")
    bool keepPreFx = false;
    const float* preFx (int ch) const { return preFxBuf[ch].data(); }
    int activeVoiceCount() const;
    void setLfoShapes (const LfoShapeStore* st) { lfoShapes = st; }
    void setMsegs (const MsegStore* st) { msegStore = st; }
    void setSidechain (const float* l, const float* r) { fxBus.setSidechain (l, r); scL = l; scR = r; }   // for the next render call
    // Break-Reactive mode: its trigger note played now (the processor calls this between renders)
    void reactNoteHit (const Snapshot& s, float vel) { react.noteHit (reactSettings (s), vel); }
    BreakReact::Settings reactSettings (const Snapshot& s) const;
    const BreakReact& breakReact() const { return react; }
    std::atomic<int> rxHitsShown { 0 };          // for the page: hits so far, the last one's type, the envelope now
    std::atomic<float> rxTypeShown { 0.0f }, rxEnvShown { 0.0f };
    int polyFloor = 0;   // chord memory: a chord's notes always get their own voices, even in mono
    // Arpeggiator Swing / Gate as moved by the Mod Matrix last block (offsets; 0 when unmodulated)
    float arpModOffset (int which) const { return arpMod[juce::jlimit (0, 1, which)]; }
    std::atomic<float> lfoPlayhead[4] { -1.0f, -1.0f, -1.0f, -1.0f };   // newest voice's LFO phases (-1: no voice)
    std::atomic<float> msegPlayhead[2] { -1.0f, -1.0f }, msegLevelShown[2] { 0.0f, 0.0f };
    std::atomic<double> granPlayheadShown { -1.0 };   // the newest note's Granular Synth playhead (sample frames), -1 when none
    std::atomic<int> granGrainsShown { 0 };           // grains playing, all notes
    // the Mixer's meters: the editor switches them on while the Mixer is showing and reads (and resets) the peaks
    mutable std::atomic<bool> mixerMeters { false };
    mutable std::array<std::atomic<float>, kMxStrips> mixerPeak {};
    mutable std::array<std::atomic<float>, 2> outputPeak {};   // the final output, L / R (always measured: almost free)

private:
    const LfoShapeStore* lfoShapes = nullptr;
    const MsegStore* msegStore = nullptr;
    double lfoFreePh[4] {};
    Voice voices[kMaxVoices];
    FxBus fxBus;
    PerfFx perfFx;               // Performance FX: after the effects rack, before the Master Filter
    BreakChopper chopper;        // the Break Chopper: into the bus before the effects
    const BreakStore* breakStore = nullptr;
    uint32_t breakSeen = 0;
    float breakGateL = 1.0f, breakGateR = 1.0f;
    bool breakWasRunning = false;
    MasterFilter masterFilter;   // after every effect
    OutputTrim outTrim;          // the very last stage
    Granular gran;           // bus stages: summed voices -> granular / spectral -> effects
    Spectral spec;
    FeedbackMatrix fbm;
    std::vector<float> fbIn[2], fbOut[2], preFxBuf[2];
    // the dry buses (Sub Route, Break FX Route): 0 joins before Performance FX, 1 before the Master
    // Filter, 2 after it. They skip the bus stages and the effects, get Master Volume, and are delayed
    // by the Spectral stage's latency so they stay in time with the rest.
    std::vector<float> dryBus[3][2], brkTmp[2];
    std::vector<float> dryDelay[3][2];
    int dryDelayPos = 0, dryTail = 0;
    float brkRG[4] { 1.0f, 0.0f, 0.0f, 0.0f }, dryMasterS = -1.0f;
    // Sub Mono: the dry sub's side signal high-passed at 120 Hz (a 4-pole Butterworth: two state-variable stages per bus)
    float monoIc[2][2][2] {};   // [bus][stage][state]
    static constexpr int kDryDelayLen = 8192;
    ModState globalMod;     // modulation of global effects, taken from the newest voice
    uint64_t orderCounter = 0;
    double lastFreq = -1.0;
    double sr = 48000.0;
    std::vector<float> vL, vR;

    const RouteStore* routeStore = nullptr;
    const GlobalModInputs* modIn = nullptr;
    const SceneStore* scenes = nullptr;
    RouteSet routeSet;
    RouteState globalRouteState;
    Snapshot fxSnap, mutSnap, noteSnap, dsSnap;
    const DnaSeqStore* dnaStore = nullptr;
    double dsPos = 0.0;
    float dsValue = 0.0f, dsValue2 = 0.0f, dsGate = 0.0f;
    float dsClockMod[3] { 0, 0, 0 };
    float arpMod[2] { 0, 0 };
    Snapshot arpModSnap;
    RouteState arpRouteState;   // last block's Mod Matrix offsets for Swing, Start Offset, Free Rate
    BreakReact react;               // Break-Reactive mode
    const float* scL = nullptr; const float* scR = nullptr;   // the host's sidechain for this render call
    Snapshot rxModSnap;             // its listening and shape settings with last block's global routes
    RouteState rxRouteState;
    Snapshot dnaModSnap;            // the DNA Sequencer's own settings with any Mod Matrix routes applied
    RouteState dnaRouteState;
    float lastSrc[MS_COUNT] {};     // last block's global sources (for routes onto the DNA settings)
    MutationTable mutTab;
    uint32_t mutLockMask (const Snapshot& s) const
    {
        uint32_t m = 0;
        for (int k = 0; k < ML_COUNT; ++k) if (s.v[P_mutLock1 + k] > 0.5f) m |= 1u << k;
        return m;
    }
    float liveScratch[P_COUNT] {};
    float globalSrc[MS_COUNT] {};
};

} // namespace tg
