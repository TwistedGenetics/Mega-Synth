#pragma once
// Master Mutate: a deterministic, seeded set of offsets added on top of the patch.
// Nothing is written to the knobs (until Commit): for each unlocked parameter the offset
// comes from (seed, amount, the parameter's own value). Offsets are correlated inside each
// lock group through a shared random "gene", and shaped per parameter.
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include "../ModMatrix.h"

namespace tg
{

enum MutLock { ML_Pitch, ML_Osc, ML_Wavetable, ML_Levels, ML_Filter, ML_Env, ML_Mod, ML_Fx, ML_Dna, ML_Wave, ML_Reso, ML_Bus, ML_Feedback, ML_COUNT, ML_None = -1 };
inline const char* const kMutLockNames[ML_COUNT] = { "Pitch", "Oscillators", "Wavetables", "Levels", "Filter", "Envelopes", "Modulation",
                                                     "Effects", "DNA Splice", "Wave / Audio-rate", "Resonator", "Granular / Spectral", "Feedback" };

// How a parameter mutates.
struct MutInfo
{
    int lock = ML_None;       // ML_None = never mutated
    int gene = ML_None;       // the group this parameter belongs to for breeding (any type, not only mutable ones)
    float depth = 0.35f;      // normalised offset at 100%
    enum Shape { Plain, Attack, Stable, Ratio, Semitone, Choice } shape = Plain;
    uint32_t hash = 0;        // from the parameter id, so adding parameters never changes existing mutations
};

const MutInfo& mutInfo (int param);
int mutLockOf (int param);
int geneGroupOf (int param);   // ML_None: not inherited (sequencer, performance, macros, scenes, Mutate controls)

// Per-seed directions, computed when the seed changes (cheap; safe on the audio thread).
struct MutationTable
{
    float dir[P_COUNT] {};     // -1..1, correlated per lock group
    float pick[P_COUNT] {};    // 0..1, second random number (choices, thresholds)
    uint32_t seed = 0xFFFFFFFFu;
    void build (uint32_t newSeed);
};

// Applies the mutation for 'amount' (0..1) to v. lockMask bit k set = group k locked.
// globalOnly: only bus / effect parameters (for the effects snapshot).
void applyMutation (const MutationTable&, float amount, uint32_t lockMask, float* v, bool globalOnly = false);

} // namespace tg
