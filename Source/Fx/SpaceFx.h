#pragma once
// v0.1.70: the new Space effects. Each one works in place on a stereo block like the rack's other effects (RackFx.h);
// the rack blends it by its slot's Mix / Return. The delay and the reverbs add what they make to what they hear
// (out = in + wet), so the slot's Return sets how much of it is heard.
//
//   Dub Delay       ping-pong, filters and saturation in the feedback (up to 110%: a soft limiter keeps it bounded),
//                   and a Throw: only what comes in while it's held (Hold / Latch) goes into the delay
//   Spring Reverb   2 or 3 modelled springs: a loop with a chain of stretched all-passes (the "drip" chirp), Kick the
//                   tank (a knock: noise and a thump into the springs), Freeze
//   Gated Reverb    an 8-line feedback delay network, its output gated for a Length after each trigger
//   Plate Reverb    Dattorro's plate (1997), with Freeze
//   Widener         two pitch-shifted copies (+/- cents) on the two sides, each delayed a little; the lows stay mono
//   Dimension       two short modulated delays cross-mixed (Roland SDD-320 style: four modes, combinable)
//   IR Reverb       convolution with your own impulse (or a synthesised room, hall, plate or spring)
// Every reverb and delay can duck (SpaceDucker) under hits: Break React's kicks / snares / hits, either sequencer,
// the sidechain, the synth's notes or a Duck note.
#include <JuceHeader.h>
#include <atomic>
#include "../DSP.h"
#include "../Params.h"
#include "RackFx.h"

namespace tg
{

// the hits a block starts with (bits: the Duck sources, kSxDuckSrc order; Sidechain is found per sample)
enum SxHitBit { SH_Kick = 1, SH_Snare = 2, SH_Hits = 4, SH_Main = 8, SH_Layer = 16, SH_Sc = 32, SH_Notes = 64, SH_Note = 128 };

// a state-variable filter (Cytomic / Simper), low, band and high outputs
struct SxSvf
{
    float ic1 = 0.0f, ic2 = 0.0f, g = 0.1f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    void set (double f, double q, double sr)
    {
        g = (float) std::tan (kPi * clampv (f, 10.0, sr * 0.45) / sr);
        k = (float) (1.0 / std::max (0.3, q));
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// One duck: an envelope that rises over Attack on a hit and falls back over Release; gain = 1 - Amount x envelope
struct SpaceDucker
{
    float env = 0.0f; int attLeft = 0; float attStep = 0.0f;
    void reset() { env = 0.0f; attLeft = 0; }
    // gains for this block into g (n samples); hit[i]: a hit on sample i
    void run (float* g, int n, const bool* hit, float amount, float attMs, float relMs, double sr)
    {
        const int attN = std::max (1, (int) std::lround (attMs * 0.001 * sr));
        const float relC = (float) std::exp (std::log (0.01) / std::max (1.0, relMs * 0.001 * sr));   // (to 1% over Release)
        for (int i = 0; i < n; ++i)
        {
            if (hit[i]) { attLeft = attN; attStep = (1.0f - env) / (float) attN; }
            if (attLeft > 0) { env += attStep; if (--attLeft == 0) env = 1.0f; }
            else env *= relC;
            g[i] = 1.0f - amount * env;
        }
    }
};

//==============================================================================
class DubDelay
{
public:
    void prepare (double sr);
    void reset();
    // throwHeld: the Throw parameter or its pad; duck: per-sample gains (null: none)
    void process (float* L, float* R, int n, const float* v, const FxContext&, bool throwHeld, const float* duck);
    bool sending() const { return sendS > 0.5f; }
private:
    double sr = 48000.0;
    DelayLine dl[2];
    SxSvf hp[2], lp[2];
    float timeS = -1.0f, sendS = 0.0f;
    bool latched = false, lastHeld = false;
    float lastHp = -1, lastLp = -1, lastRes = -1;
    float lpComp = 1.0f;
};

//==============================================================================
class SpringReverb
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&, const float* duck);
private:
    static constexpr int kSprings = 3, kAp = 10;
    struct Spring
    {
        DelayLine loop;
        float apX[kAp][4] {}, apY[kAp][4] {};   // stretched all-passes (each sample delayed by up to 4)
        int apPos = 0;
        float lpS = 0.0f, dcX = 0.0f, dcY = 0.0f;
        double baseDelay = 0.0;
    };
    Spring spr[2][kSprings];
    double sr = 48000.0, lfoPh = 0.0;
    float freezeS = 0.0f, inS = 1.0f;
    bool lastKick = false;
    int kickLeft = 0; uint32_t rng = 0x5eed5u; float thumpPh = 0.0f;
    float hpX[2] {}, hpY[2] {};
};

//==============================================================================
class GatedReverb
{
public:
    void prepare (double sr);
    void reset();
    // hits: the block's start-of-block hit bits; scHit: sidechain hits per sample (null: none)
    void process (float* L, float* R, int n, const float* v, const FxContext&, uint32_t hits, const bool* scHit, const float* duck);
    float gateShown() const { return gate; }
private:
    static constexpr int kLines = 8;
    DelayLine line[kLines], pre[2];
    float lp[kLines] {};
    double sr = 48000.0;
    float gate = 0.0f, levelEnv = 0.0f;
    int openLeft = 0, openLen = 1;
    bool armed = true;
    float freezeS = 0.0f;
};

//==============================================================================
class PlateReverb
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&, const float* duck);
private:
    double sr = 48000.0, scale = 1.0, lfoPh = 0.0;
    DelayLine pre;
    struct Ap { DelayLine d; double len = 1; inline float tick (float x, float g, double dlen) { const float z = d.read (dlen - 1.0); const float w = x + g * z; d.push (w); return z - g * w; } };
    Ap inAp[4], decAp1[2], decAp2[2];
    DelayLine tankDl1[2], tankDl2[2];
    float bw = 0.0f, damp[2] {}, tankFb[2] {};
    float freezeS = 0.0f;
};

//==============================================================================
class MicroWidener
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, ph[2] {};
    DelayLine dl[2];
    Biquad hp, hp2;
    float lastCut = -1.0f;
};

//==============================================================================
class DimensionChorus
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, ph = 0.0;
    DelayLine dl[2];
    Biquad bbd;
    float depthS = -1.0f, rateS = 0.25f;
};

//==============================================================================
class IrReverb
{
public:
    void prepare (double sr, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&, const float* duck);
    // the message thread: a new impulse (loaded in the background by the convolution)
    void load (juce::AudioBuffer<float>&& ir, double irRate);
    bool ready() const { return conv.getCurrentIRSize() > 1; }
private:
    double sr = 48000.0;
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 1024 } };
    juce::AudioBuffer<float> buf;
    DelayLine pre[2];
    Biquad lp, hp;
    float lastLp = -1, lastHp = -1;
};

// the synthesised impulses (Room, Hall, Plate, Spring): original, made from noise, deterministic
juce::AudioBuffer<float> makeSpaceImpulse (int type, double sr, double maxSeconds);

} // namespace tg
