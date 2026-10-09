#pragma once
#include <JuceHeader.h>
#include <memory>
#include <atomic>
#include "Params.h"
#include "DSP.h"
#include "ModMatrix.h"

namespace tg
{

// Everything the audio thread needs for one block, read once from the parameters.
struct Snapshot
{
    float v[P_COUNT] {};
    double sampleRate = 48000.0;
    double fxTempo = 130.0;   // tempo used for tempo-synced effects
    float bendSemis = 0.0f;   // current pitch-bend in semitones

    inline float f (int p) const { return v[p]; }
    inline int   i (int p) const { return (int) std::lround (v[p]); }
};

// A loaded WAV for Oscillator 4.
struct WaveSample
{
    std::vector<float> ch[2];
    int numChannels = 1;
    int length = 0;
    double sampleRate = 44100.0;
    float peak = 1.0f;
    double duration() const { return length / sampleRate; }
};

struct ModState
{
    float m[MT_COUNT] {};
    void clear() { std::fill (std::begin (m), std::end (m), 0.0f); }
    float operator[] (int k) const { return m[k]; }
};

// Web Audio "hard muted" mix value: zero if the knob is at zero, otherwise knob + modulation.
inline float hardMuted (float knob, float mod, float maxV)
{
    if (! (knob > 0.0001f)) return 0.0f;
    return clampv (knob + mod, 0.0f, maxV);
}

double syncBeats (const char* key);                       // e.g. "1/8d" -> 0.75
double syncSeconds (const ChoiceList& list, int idx, double tempo, double fallback);
double syncRate (const ChoiceList& list, int idx, double tempo, double fallback);

//==============================================================================
struct FilterChain
{
    int mode = -1;
    int numStages = 0;
    Biquad st[4];
    Biquad::Type types[4] {};
    float fbState[2] { 0, 0 };
    float fbGain = 0.0f;
    enum FbKind { FbNone, FbToInput, FbToPre } fbKind = FbNone;
    float preK = 0.0f;     // extra drive stage after the input drive (OTA / TB-303 / acid)
    float postK = 0.0f;    // saturator after the filter (Polivoks)
    bool limitOut = false; // soft limiter for the acid modes, whose resonance can reach +30 dB

    // Ladder / Minimoog modes: a 4-pole one-pole cascade with inverted feedback.
    // (In the browser these were Web Audio feedback loops, which don't resonate there.)
    bool ladder = false;
    float lg[4] { 0.5f, 0.5f, 0.5f, 0.5f };
    float ls[2][4] {};
    float lk = 0.0f;
    // TB-303 mode: the ladder with its resonance feedback high-passed (~150 Hz), as in the
    // real 303, so resonance fades at low cutoff and the filter closes right down.
    bool tb303 = false;
    float tbA = 0.98f;
    float tbX[2] { 0, 0 }, tbY[2] { 0, 0 };

    // Analog warmth (0 = exactly the browser's behaviour)
    float warm = 0.0f;       // soft, slightly asymmetric input saturation instead of a hard clip
    float bassKeep = 0.0f;   // lowers high-pass stages and adds low end back around band-pass stages
    Biquad par[4];           // parallel low-pass for band-pass stages (Bass Keep)
    float dcX[2] { 0, 0 }, dcY[2] { 0, 0 }, dcR = 0.9987f;

    inline float inputShape (float x, float k) const
    {
        if (warm <= 0.0f) return driveShape (x, k);
        const float c = clampv (x, -1.0f, 1.0f);
        const float soft = std::abs (x) < 1.5f ? x - (4.0f / 27.0f) * x * x * x : (x > 0 ? 1.0f : -1.0f);
        float xi = c + (soft - c) * warm;
        xi += 0.12f * warm * xi * xi;          // a touch of 2nd harmonic
        return fastTanh (k * xi);
    }

    inline float dcBlock (float v, int ch)
    {
        const float y = v - dcX[ch] + dcR * dcY[ch];
        dcX[ch] = v; dcY[ch] = y;
        return y;
    }

    void configure (int newMode);
    void update (float cutoff, float res, double sr);
    void setLadder (float cutoff, float spread, float k, double sr);
    void reset();
    void copyChannel (int from, int to);

    inline float process (float x, int ch, float drive)
    {
        if (ladder)
        {
            float fb = fbState[ch];
            if (tb303)
            {
                const float y = tbA * (tbY[ch] + fb - tbX[ch]);
                tbX[ch] = fb; tbY[ch] = y;
                fb = y;
            }
            float v = inputShape (x - lk * fb, drive);
            float* s = ls[ch];
            for (int k = 0; k < 4; ++k)
            {
                const float u = (v - s[k]) * lg[k];
                v = u + s[k];
                s[k] = v + u;
            }
            if (! std::isfinite (v)) { reset(); v = 0.0f; }
            fbState[ch] = v;
            v *= tb303 ? (1.0f + lk * 0.45f) : (1.0f + lk * 0.35f);   // make up some of the bass lost to resonance
            if (limitOut) v = 1.5f * std::tanh (v * (1.0f / 1.5f));
            return dcBlock (v, ch);
        }

        float v = inputShape (x, drive);
        if (preK > 0.0f)
        {
            if (fbKind == FbToPre) v += fbGain * fbState[ch];
            v = driveShape (v, preK);
        }
        for (int k = 0; k < numStages; ++k)
        {
            if (types[k] == Biquad::BP && bassKeep > 0.0f)
                v = st[k].process (v, ch) + bassKeep * par[k].process (v, ch);
            else
                v = st[k].process (v, ch);
        }
        if (! std::isfinite (v)) { reset(); v = 0.0f; }
        fbState[ch] = v;
        if (postK > 0.0f) v = driveShape (v, postK);
        if (limitOut) v = 1.5f * std::tanh (v * (1.0f / 1.5f));
        return dcBlock (v, ch);
    }
};

// What the voices need from the modulation matrix for one block.
struct ModContext
{
    const RouteSet* routes = nullptr;
    const GlobalModInputs* in = nullptr;
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
    S cutoff, res;
    S fmAmt[4], ringDepth[2], ringOut[2];
    S ssCents[9], ssGain[9];
    float drive = 1.0f, cShapeK = 4.5f;
    int ssVoices = 7;
    float ssPanL[9] {}, ssPanR[9] {};

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
    FilterChain filter;
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
    Snapshot modSnap;            // the parameters with this voice's modulation applied
    float nextRand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 8388608.0f - 1.0f; }
};

//==============================================================================
// Global effects: tape/BBD delay, 90s reverb + shimmer, Juno chorus, reverse pitch reverb.
class FxBus
{
public:
    void prepare (double sr, int maxBlock);
    void reset();
    // In-place: L/R hold the summed voices on input and the final mix on output.
    void process (float* L, float* R, int n, const Snapshot&, const ModState& mod);

    // Called from the message thread: rebuilds the reverb impulse when the size changes.
    void updateImpulse (double seconds);
    double impulseSeconds() const { return irSeconds.load(); }

private:
    double sr = 48000.0;
    int maxBlock = 512;

    float masterS = 0.25f;
    Biquad warmLow, warmHigh;   // Warmth: gentle low-shelf lift and high-shelf softening on the master
    float lastWarm = -1.0f;
    DelayLine dl[2];
    Biquad tapeLP, tapeHP;
    double wowPh = 0, flutPh = 0;
    float dTimeS = 0.32f, dFbS = 0.35f, dSendS = 0, dWetS = 0, wowDepthS = 0, flDepthS = 0;

    DelayLine ch1[2], ch2[2];
    double chPh1 = 0, chPh2 = 0;
    float cSendS = 0, cWetS = 0, cDepthS = 0.0048f;

    DelayLine preDl[2], pitchDl[2];
    double revPh = 0;
    Biquad revBP;
    float vSendS = 0, vWetS = 0, vPreS = 0.42f, vPitchS = 0.008f;

    Biquad revLP, shimHP;
    float rSendS = 0, rWetS = 0, sSendS = 0, sWetS = 0;

    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 256 } };
    juce::AudioBuffer<float> convBuf;
    std::atomic<double> irSeconds { 0.0 };
    bool convReady = false;
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
    void setModulation (const RouteStore* r, const GlobalModInputs* in) { routeStore = r; modIn = in; }
    // For the editor: how far each destination is currently moved (normalised), and source values
    std::array<std::atomic<float>, P_COUNT> liveOffset {};
    std::array<std::atomic<float>, MS_COUNT> liveSrc {};

    FxBus& fx() { return fxBus; }
    int activeVoiceCount() const;

private:
    Voice voices[kMaxVoices];
    FxBus fxBus;
    ModState globalMod;     // modulation of global effects, taken from the newest voice
    uint64_t orderCounter = 0;
    double lastFreq = -1.0;
    double sr = 48000.0;
    std::vector<float> vL, vR;

    const RouteStore* routeStore = nullptr;
    const GlobalModInputs* modIn = nullptr;
    RouteSet routeSet;
    RouteState globalRouteState;
    Snapshot fxSnap;
    float liveScratch[P_COUNT] {};
    float globalSrc[MS_COUNT] {};
};

} // namespace tg
