#pragma once
// The global effects bus: master stage, then the effects rack (send effects, inserts) in order.
#include <JuceHeader.h>
#include <atomic>
#include "../EngineTypes.h"
#include "RackFx.h"

namespace tg
{

//==============================================================================
// Global effects: tape/BBD delay, 90s reverb + shimmer, Juno chorus, reverse pitch reverb.
class FxBus
{
public:
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

private:
    // Delay, chorus and the reverbs (the send effects) for one run of adjacent send slots:
    // each hears the run's input and adds its return. mask: bit FS_Delay / FS_Chorus / FS_Reverb.
    void processSends (float* L, float* R, int n, const Snapshot&, const ModState& mod, int mask, const float* slotGain,
                       const float* const* delayIn, float* const* delayOut);
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
};

} // namespace tg
