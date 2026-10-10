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

namespace tg
{
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

    bool active = false;
    bool released = false;
    int key = -1;
    uint64_t order = 0;
    double baseFreq = 440.0;
    float accentBoost = 0.0f;
    float filterAccent = 0.0f;
    float velGain = 1.0f;

    float currentLevel() const { return (float) ampEnv.eval (t); }

private:
    void computeSources (const Snapshot&, const GlobalModInputs*, float dt);
    void advanceLfos (const Snapshot&, int numSamples);
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
    uint32_t rng = 1;
    float randNote = 0.0f;
    double rndPh = 0.0, driftPh2 = 0.0;
    float rndA = 0, rndB = 0, shVal = 0, chaosX = 0.5f, drA = 0, drB = 0;
    float envF = 0, audF = 0, trFast = 0, trSlow = 0;
    float lastDt = 0.0f;
    float aPrev[4] {};           // previous sample of Osc 1, 2, 3 and Sub, for audio-rate routes
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
    void rekey (Voice* v, int newKey) { if (v) v->key = newKey; }

    void render (float* L, float* R, int numSamples, const Snapshot&, const WaveSample* const* wavs);

    // Modulation matrix inputs (owned by the processor; read on the audio thread)
    void setModulation (const RouteStore* r, const GlobalModInputs* in, const SceneStore* sc = nullptr) { routeStore = r; modIn = in; scenes = sc; }
    // For the editor: how far each destination is currently moved (normalised), and source values
    std::array<std::atomic<float>, P_COUNT> liveOffset {};
    std::array<std::atomic<float>, MS_COUNT> liveSrc {};

    FxBus& fx() { return fxBus; }
    const FxBus& fx() const { return fxBus; }
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

private:
    Voice voices[kMaxVoices];
    FxBus fxBus;
    Granular gran;           // bus stages: summed voices -> granular / spectral -> effects
    Spectral spec;
    FeedbackMatrix fbm;
    std::vector<float> fbIn[2], fbOut[2], preFxBuf[2];
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
    float dsClockMod[3] { 0, 0, 0 };   // last block's Mod Matrix offsets for Swing, Start Offset, Free Rate
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
