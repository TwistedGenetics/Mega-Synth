#pragma once
// The global effects bus: master stage, then the effects rack (send effects, inserts) in order.
#include <JuceHeader.h>
#include <atomic>
#include "../EngineTypes.h"
#include "RackFx.h"
#include "SpaceFx.h"

namespace tg
{

//==============================================================================
// Global effects: tape/BBD delay, 90s reverb + shimmer, Juno chorus, reverse pitch reverb.
class FxBus
{
public:
    void setSidechain (const float* l, const float* r) { scL = l; scR = r; }   // for the next process call (null: none)

    void prepare (double sr, int maxBlock);
    void reset();
    // In-place: L/R hold the summed voices on input and the final mix on output.
    // delayIn: extra signal fed into the delay line (feedback matrix); delayOut: the delay line's output
    void process (float* L, float* R, int n, const Snapshot&, const ModState& mod,
                  const float* const* delayIn = nullptr, float* const* delayOut = nullptr);

    // Called from the message thread: rebuilds the reverb impulse when the size changes.
    void updateImpulse (double seconds);
    double impulseSeconds() const { return irSeconds.load(); }

    // ---- the rack
    void setRack (const FxRackStore* st) { rack = st; }
    void setDnaGate (float g) { dnaGate = g; }
    void setStutterHeld (bool h) { stutterMidi = h; }
    void shaperNoteOn (double beat) { shaper.noteOn (beat); }
    MultibandComp multiband;
    BeatRepeat stutter;
    VolumeShaper shaper;

    // ---- v0.1.70: the Space effects
    void addSpaceHits (uint32_t bits) { pendingHits |= bits; }   // (SxHitBit) hits at the start of the next block
    void setThrowPad (bool held) { throwPad = held; }            // the Dub Delay's Throw from its learned pad
    void loadIr (juce::AudioBuffer<float>&& ir, double irRate) { irRev.load (std::move (ir), irRate); }
    bool irReady() const { return irRev.ready(); }
    float gatedGateShown() const { return gated.gateShown(); }
    bool dubSending() const { return dub.sending(); }
    std::atomic<float> duckShown[SD_COUNT] {};   // each duck's gain at the end of the last block (for the page)

private:
    // Delay, chorus and the reverbs (the send effects) for one run of adjacent send slots:
    // each hears the run's input and adds its return. mask: bit FS_Delay / FS_Chorus / FS_Reverb.
    void processSends (float* L, float* R, int n, const Snapshot&, const ModState& mod, int mask, const float* slotGain,
                       const float* const* delayIn, float* const* delayOut);
    void freezeTail (float* cl, float* cr, int n, bool freezing);
    void prepareSpace (int n, const Snapshot&);
    const FxRackStore* rack = nullptr;
    VintageSampler sampler;
    FlangerPhaser flanger;
    StereoTools stereo;
    float dnaGate = 0.0f;
    bool stutterMidi = false;
    float slotS[FS_COUNT] {};          // each slot's smoothed On x Mix
    bool slotsReady = false;
    uint64_t orderKey = 0;             // the order being played
    std::array<int, FS_COUNT> order {};
    float duck = 1.0f;                 // output dip while the order changes
    bool reordering = false;
    std::vector<float> dryBuf[2], runBuf[2];
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
    const float* scL = nullptr; const float* scR = nullptr;

    DubDelay dub; SpringReverb spring; GatedReverb gated; PlateReverb plate; MicroWidener widen; DimensionChorus dim; IrReverb irRev;
    SpaceDucker duckers[SD_COUNT];
    std::vector<float> duckBuf[SD_COUNT];
    std::vector<char> scHitBuf, hitBuf;
    SidechainDetector duckSc;
    uint32_t pendingHits = 0;
    bool throwPad = false;
    const float* duckFor (int d) const { return duckOn[d] ? duckBuf[d].data() : nullptr; }
    bool duckOn[SD_COUNT] {};
    // the 90s reverb's Freeze: its tail recorded, then played back as overlapping grains
    std::vector<float> frzBuf[2], frzGain;
    int frzW = 0, frzFrozenAt = 0;
    float frzS = 0.0f;
    struct FrzGrain { double pos = 0.0; int age = 0; } frzG[2];
    uint32_t frzRng = 0xF2EE2Eu;
};

} // namespace tg
