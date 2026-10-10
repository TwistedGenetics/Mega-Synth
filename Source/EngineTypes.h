#pragma once
// Types shared by the engine and the effects bus: the per-block parameter snapshot, loaded samples,
// the legacy modulation state and the tempo-sync helpers.
#include <JuceHeader.h>
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
    double fxBeat = 0.0;      // beat position (the DAW's while it plays, otherwise the synth's clock) at the render start
    double fxBeatInc = 0.0;   // beats per sample
    float bendSemis = 0.0f;   // current pitch-bend in semitones

    inline float f (int p) const { return v[p]; }
    inline int   i (int p) const { const float x = v[p]; return (int) (x >= 0.0f ? x + 0.5f : x - 0.5f); }   // = lround, inline
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

} // namespace tg
