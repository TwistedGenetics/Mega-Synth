#pragma once
// v0.1.72: the choruses and phasers. Each works in place on a stereo block like the rack's other inserts; the rack
// blends it by its slot's Mix. The choruses output only their delayed voices (so Mix 50% is the classic dry + wet
// chorus); the phasers output the phaser's own mix of dry and shifted signal (so Mix 100% is the pedal).
//
//   Ensemble        a string-machine tri-chorus (after the Solina / ARP string ensemble idea): three delay taps swept by a
//                   slow and a fast LFO at 120 degree phases, spread across the stereo field
//   BBD Chorus      a single bucket-brigade style chorus (after the Boss CE pedals): a triangle sweep, a dark BBD filter,
//                   and the stereo output's second side swept in the opposite direction
//   Multi-Chorus    2 to 8 voices, each its own sweep, spread across the field, with feedback
//   Script Phase    a 4-stage phaser after the MXR Phase 90 (script logo: no feedback; Feedback adds the block logo's)
//   Colour Phase    a 4-stage phaser after the Electro-Harmonix Small Stone, with its Colour switch (more resonance)
//   Dual Phaser     two 6-stage phasers after the Mu-Tron Bi-Phase: in parallel (one per side) or in series
//   Barber-pole     an endless phaser: overlapping sweeps fade in at one end and out at the other, so it seems to rise
//                   (or fall) for ever
//   Vibe            4 unequal stages swept by a lamp-and-photocell style LFO (after the Uni-Vibe): Chorus or Vibrato
// All are approximations of each unit's idea and sound, not circuit models.
#include <JuceHeader.h>
#include "../DSP.h"
#include "../Params.h"
#include "RackFx.h"

namespace tg
{

constexpr int kModSub = 16;   // the sweeps are worked out every 16 samples and joined by straight lines in between

// a first-order all-pass (y = a x + x1 - a y1), its coefficient from a corner frequency
struct Ap1
{
    float x1 = 0.0f, y1 = 0.0f;
    void reset() { x1 = y1 = 0.0f; }
    inline float tick (float x, float a) { const float y = a * x + x1 - a * y1; x1 = x; y1 = y; return y; }
    static inline float coef (double fc, double sr) { const double t = std::tan (kPi * clampv (fc, 10.0, sr * 0.45) / sr); return (float) ((t - 1.0) / (t + 1.0)); }
};

class EnsembleChorus
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, phS = 0.0, phF = 0.0;
    DelayLine dl;
    float lpL = 0.0f, lpR = 0.0f, depthS = -1.0f, speedS = -1.0f;
    double dPrev[3] {};
};

class BbdChorus
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, ph = 0.0;
    DelayLine dl[2];
    Biquad lp, hp;
    bool lastBright = false;
    float depthS = -1.0f, stereoS = -1.0f, rateS = -1.0f;
};

class MultiChorus
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    static constexpr int kMax = 8;
    double sr = 48000.0, ph[kMax] {};
    DelayLine dl;
    float fbS = 0.0f, depthS = -1.0f, rateS = -1.0f, spreadS = -1.0f, lastWet = 0.0f;
    float voiceG[kMax] {};   // each voice fades in or out when Voices changes
    double dPrev[kMax] {};
};

// the phasers: a chain of all-passes per side, swept by an LFO
class ScriptPhase
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, ph = 0.0;
    Ap1 ap[2][4];
    float fb[2] {}, fbS = 0.0f, rateS = -1.0f, aPrev = 0.0f;
};

class ColourPhase
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, ph = 0.0;
    Ap1 ap[2][4];
    float fb[2] {}, fbS = 0.0f, rateS = -1.0f, aPrev = 0.0f;
};

class DualPhaser
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, ph[2] {};
    Ap1 ap[2][2][6];   // [phaser][side][stage]
    float fb[2][2] {}, modeS = -1.0f, depthS = -1.0f, rateS[2] { -1.0f, -1.0f }, fadeIn = 1.0f, aPrev[2] {};
};

class BarberPhaser
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    static constexpr int kChains = 3, kStages = 8;
    double sr = 48000.0, ph = 0.0;
    Ap1 ap[2][kChains][kStages];
    float fb[2][kChains] {}, rateS = -1.0f, dirS = -2.0f, aPrev[2][kChains] {}, wPrev[2][kChains] {};
};

class VibeFx
{
public:
    void prepare (double sr);
    void reset();
    void process (float* L, float* R, int n, const float* v, const FxContext&);
private:
    double sr = 48000.0, ph = 0.0;
    Ap1 ap[2][4];
    float lamp = 0.0f, intS = -1.0f, modeS = -1.0f, rateS = -1.0f, aPrev[4] {};
};

} // namespace tg
