#include "Engine.h"
#include <cstring>

namespace tg
{

static constexpr int kCtrl = 16; // control-rate block (samples)

static inline double pow2 (double x) { return std::exp2 (x); }
static inline double pow2z (float x) { return x == 0.0f ? 1.0 : std::exp2 ((double) x); }   // exp2 (+-0) is exactly 1
// p - floor(p), with the common cases done without floor: the same operations on the same values, so bit for bit
// the same result (p + 0.0 turns -0 into +0, as p - floor(p) does)
static inline double wrap01 (double p)
{
    if (p >= 0.0 && p < 1.0) return p + 0.0;
    if (p >= 1.0 && p < 2.0) return p - 1.0;
    if (p >= -1.0 && p < 0.0) return p - (-1.0);
    p -= std::floor (p); return p;
}

double syncBeats (const char* key)
{
    struct E { const char* k; double b; };
    static const E table[] = {
        { "2bar", 8 }, { "4bar", 16 }, { "1bar", 4 }, { "1/2", 2 }, { "1/2d", 3 }, { "1/2t", 4.0 / 3.0 },
        { "1/4", 1 }, { "1/4d", 1.5 }, { "1/4t", 2.0 / 3.0 }, { "1/8", 0.5 }, { "1/8d", 0.75 }, { "1/8t", 1.0 / 3.0 },
        { "1/16", 0.25 }, { "1/16d", 0.375 }, { "1/16t", 1.0 / 6.0 }, { "1/32", 0.125 }
    };
    for (auto& e : table) if (std::strcmp (e.k, key) == 0) return e.b;
    return 0.0;
}

double syncSeconds (const ChoiceList& list, int idx, double tempo, double fallback)
{
    if (idx <= 0 || idx >= list.size) return fallback;
    const double beats = syncBeats (list.keys[idx]);
    if (beats <= 0) return fallback;
    return (60.0 / std::max (1.0, tempo)) * beats;
}

double syncRate (const ChoiceList& list, int idx, double tempo, double fallback)
{
    const double s = syncSeconds (list, idx, tempo, 0.0);
    if (idx <= 0 || s <= 0) return fallback;
    return 1.0 / s;
}

//==============================================================================
//==============================================================================
void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    gran.prepare (sampleRate);
    reese.prepare (sampleRate);
    for (auto& c : core) c.prepare (sampleRate);
    waveMut.prepare (sampleRate);
    dna.prepare (sampleRate);
    reso.prepare (sampleRate);
    active = false;
}

static thread_local uint32_t voiceRandState = 0x9E3779B9u;   // (per thread: the random patch generator measures on its own thread)
void resetVoiceRandom() { voiceRandState = 0x9E3779B9u; }   // tests: compare renders from the same random start
static float voiceRand01()
{
    voiceRandState ^= voiceRandState << 13; voiceRandState ^= voiceRandState >> 17; voiceRandState ^= voiceRandState << 5;
    return (voiceRandState & 0xFFFFFF) / 16777216.0f;
}

static inline float lfoShape (int wave, double cycle)
{
    switch (wave)
    {
        case 1:  return (float) (1.0 - 4.0 * std::abs (cycle - 0.5));   // triangle
        case 2:  return (float) (cycle * 2.0 - 1.0);                    // sawtooth
        case 3:  return cycle < 0.5 ? 1.0f : -1.0f;                     // square
        default: return (float) std::sin (cycle * 2.0 * kPi);           // sine
    }
}

void Voice::start (int k, double freq, double glideFrom, const StartOptions& o, const Snapshot& s, uint64_t ord, const ModContext& mc)
{
    key = k;
    order = ord;
    active = true;
    released = false;
    t = 0.0;
    { const int r = clampv (s.i (P_subRoute), 0, 2); for (int j = 0; j < 3; ++j) subRG[j] = j == r ? 1.0f : 0.0f; }   // (a new note starts on its route)
    for (auto& c : core) c.stop();   // (a Core slot starts with the note when it is on: see updateControl)
    if (s.f (P_rsLevel) > 0.0f)
    {
        reese.start (clampv (s.i (P_rsRetrig), 0, 2), (uint32_t) (ord * 131u + (uint64_t) k));
        const int cps = clampv (s.i (P_rsCpSync), 0, 7);
        reese.syncLfos (s.fxBeat, kRsLfoSyncBeats[clampv (s.i (P_rsFmtSync), 0, 6)], cps > 0 ? kRsCpSyncBeats[cps] : 0.0);
    }
    startedAt = voiceRand01() * 1000.0;
    for (int i = 0; i < 9; ++i) ssDriftSin[i] = std::sin ((startedAt + i * 0.37) * 7.123);   // fixed per note
    gran.start (ord * 131u + (uint64_t) k);   // its own random sequence (the shared voice one is left alone)
    panWidth = -1.0f; panVoices = -1;
    accentBoost = o.accentBoost;
    filterAccent = o.filterAccent;
    const float vs = s.f (P_velSens);
    velGain = 1.0f - vs + vs * clampv (o.velocity, 0.0f, 1.0f);

    // ---- modulation sources: per-note state (seeded from the note, not from the shared
    // random generator, so adding the matrix leaves the oscillators' random phases as they were)
    velocity = clampv (o.velocity, 0.0f, 1.0f);
    channel = clampv (o.channel, 1, 16);
    midiNote = o.note >= 0 ? o.note : clampv ((int) std::lround (69.0 + 12.0 * std::log2 (std::max (1.0, freq) / 440.0)), 0, 127);
    rng = (uint32_t) (ord * 2654435761u) ^ (uint32_t) (k * 40503u) ^ 0x5bd1e995u;
    if (rng == 0) rng = 1;
    randNote = nextRand();
    rndA = nextRand(); rndB = nextRand(); drA = nextRand(); drB = nextRand();
    shVal = 0.0f; chaosX = 0.3f + 0.4f * (0.5f + 0.5f * nextRand());
    rndPh = driftPh2 = 0.0;
    inst.seed (rng ^ 0xA5A5A5A5u);
    envF = audF = trFast = trSlow = 0.0f;
    for (auto& a : aPrev) a = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        lfoPh[i] = 0.0; lfoDepthNow[i] = s.f (P_lfo1Depth + 3 * i);
        lfoElapsed[i] = 0.0; lfoSmInit[i] = false;
        const int xb = P_lfo1Sync + kLfoXStride * i;
        lfoXMod[i][0] = s.f (xb + 2); lfoXMod[i][1] = s.f (xb + 4); lfoXMod[i][2] = s.f (xb + 6);
    }
    rstate.reset();
    lastDt = 0.0f;
    waveMut.reset();
    dna.reset();
    dnaStarted = false; dnaFade = 0;
    reso.reset();
    resoRinging = false;
    arFx.reset();
    arPh = 0.0;
    std::fill (std::begin (srcV), std::end (srcV), 0.0f);

    // Envelope times are read once, at note-on: routes onto them use the note-on source values
    // (velocity, note, random per note, wheel...), so "velocity -> attack" works as expected.
    const float* envV = s.v;
    if (mc.any() && mc.routes->anyEnvTime)
    {
        t = 0.0; released = false;
        ampEnv.init (s.f (P_ampA), s.f (P_ampD), s.f (P_ampS), s.f (P_ampR));
        filtEnv = ampEnv;
        for (auto& e : modEnv) e = ampEnv;
        computeSources (s, mc.in, 0.0f);
        modSnap = s;
        RouteState tmp;
        applyRoutes (*mc.routes, s.v, modSnap.v, srcV, tmp, 0.0f, RF_EnvTime);
        envV = modSnap.v;
    }
    if (s.v[P_ciAmount] > 1.0e-4f && s.v[P_ciEnv] > 1.0e-4f)
    {
        // Cell Instability: each note's envelope times differ slightly
        if (envV != modSnap.v) { modSnap = s; envV = modSnap.v; }
        applyInstability (modSnap, true);
    }
    ampEnv.init (envV[P_ampA], envV[P_ampD], envV[P_ampS], envV[P_ampR]);
    filtEnv.init (envV[P_fEnvA], envV[P_fEnvD], envV[P_fEnvS], envV[P_fEnvR]);
    for (int e = 0; e < 3; ++e)
        modEnv[e].init (envV[P_mEnv1A + 4 * e], envV[P_mEnv1D + 4 * e], envV[P_mEnv1S + 4 * e], envV[P_mEnv1R + 4 * e]);

    filter.configure (s.i (P_filterMode), s.i (P_filterType), s.i (P_filterSlope), true, clampv (s.i (P_flt1Char), 0, CM_COUNT - 1));
    filter.setCharacter (s.f (P_flt1CharAmt));
    for (auto& r : ringSlots) { r.hp.reset(); r.lp.reset(); }
    stereo = false;

    ph1 = ph2 = ph3 = phSub = phCar = phMod = 0.0;
    if (s.f (P_analogDrift) > 0.001f)
    {
        // free-running oscillators: no two notes start with the same phase relationship
        ph1 = voiceRand01(); ph2 = voiceRand01(); ph3 = voiceRand01(); phSub = voiceRand01();
    }
    for (int k = 0; k < 5; ++k) { driftPh[k] = voiceRand01(); driftRate[k] = 0.08f + 0.35f * voiceRand01(); }
    for (auto& p : ssPh) p = voiceRand01();
    std::copy (std::begin (ssPh), std::end (ssPh), std::begin (ssPh0));
    for (auto* d : { &rxDcWt[0], &rxDcWt[1], &rxDcSaw, &rxDcCx }) d->n = 0;
    coreXf[0] = coreXf[1] = 0;
    rxFiltKick = false;
    std::fill (std::begin (srcVals), std::end (srcVals), 0.0f);
    for (auto& w : wt) { w.pos = -1.0; w.stopped = false; w.loaded = false; w.wav = nullptr; }   // start set on the first control update

    // Initialise every smoother at its target. Oscillators start at the glide-from pitch.
    startUnison (s);
    for (int k = 0; k < 2; ++k)
    {
        MsegShape sh;
        if (mc.msegs != nullptr && mc.msegs->tryGet (k, sh)) mseg[k].start (sh);
        else mseg[k].start (mseg[k].s);   // (the editor is mid-edit: the shape this voice had)
    }
    updatePitchEnv (s);
    baseFreq = glideFrom;
    updateLfoOut (s, 0, mc);
    ModState m;
    computeMod (s, m);
    updateControl (s, nullptr, m, true);
    baseFreq = freq;

    srcMute.v = 0.0f;
}

void Voice::release()
{
    if (released) return;
    released = true;
    ampEnv.release (t);
    filtEnv.release (t);
    for (auto& e : modEnv) e.release (t);
    for (auto& m : mseg) m.release();
    for (auto& c : core) if (c.isStarted()) c.release();
}

void Voice::computeMod (const Snapshot& s, ModState& mod) const
{
    mod.clear();
    for (int k = 0; k < 3; ++k)
    {
        const int tgt = s.i (P_lfoAssignTarget0 + 3 * k);
        if (tgt <= MT_none || tgt >= MT_COUNT) continue;
        const int src = clampv (s.i (P_lfoAssignSource0 + 3 * k), 0, 2);
        const int wave = s.i (P_lfo1Wave + 3 * src);
        const float depth = s.f (P_lfo1Depth + 3 * src);
        const float raw = lfoOut[src];   // phase advances at the (possibly modulated) rate
        juce::ignoreUnused (wave);
        mod.m[tgt] += raw * depth * s.f (P_lfoAssignAmt0 + 3 * k) * kTargetRange[tgt];
    }
    for (int k = 0; k < 3; ++k)
    {
        const int tgt = s.i (P_envAssignTarget0 + 3 * k);
        if (tgt <= MT_none || tgt >= MT_COUNT) continue;
        const int src = clampv (s.i (P_envAssignSource0 + 3 * k), 0, 2);
        const float level = (float) modEnv[src].eval (t);
        mod.m[tgt] += level * s.f (P_envAssignAmt0 + 3 * k) * kTargetRange[tgt];
    }
}

void Voice::computeSources (const Snapshot& s, const GlobalModInputs* in, float dt)
{
    float* v = srcV;
    for (int k = 0; k < 4; ++k)
        v[MS_Lfo1 + k] = lfoOut[k] * lfoDepthNow[k];
    v[MS_AmpEnv]    = (float) ampEnv.eval (t);
    v[MS_FilterEnv] = (float) filtEnv.eval (t);
    for (int e = 0; e < 3; ++e) v[MS_ModEnv1 + e] = (float) modEnv[e].eval (t);
    v[MS_Mseg1] = mseg[0].level; v[MS_Mseg2] = mseg[1].level;
    v[MS_Velocity] = velocity;
    v[MS_Note]     = clampv ((midiNote - 60) / 60.0f, -1.0f, 1.0f);
    v[MS_KeyTrack] = midiNote / 127.0f;
    if (in != nullptr)
    {
        const int ch = channel - 1;
        v[MS_Aftertouch]  = in->aftertouch;
        v[MS_PolyAT]      = in->polyAT[midiNote & 127];
        v[MS_ModWheel]    = in->wheel;
        v[MS_PitchBend]   = in->bend;
        v[MS_CcA]         = in->ccA;
        v[MS_CcB]         = in->ccB;
        v[MS_MpePressure] = in->mpePressure[ch];
        v[MS_MpeSlide]    = in->mpeSlide[ch];
        v[MS_MpeGlide]    = in->mpeGlide[ch];
        v[MS_ArpPos]      = in->arpPos;
        v[MS_ArpGate]     = in->arpGate;
        v[MS_Sidechain]   = in->sidechain;
        v[MS_BsStep] = in->bsStep; v[MS_BsPattern] = in->bsPattern; v[MS_BsSlice] = in->bsSlice; v[MS_BsBreak] = in->bsBreak; v[MS_BsFill] = in->bsFill; v[MS_LsStep] = in->lsStep; v[MS_LsPattern] = in->lsPattern;
        v[MS_ReesePhase] = rsRun ? (float) reese.beatPhase() : 0.0f;
        v[MS_CoreAEnv] = coreRun[0] ? core[0].envelope (0) : 0.0f;
        v[MS_CoreBEnv] = coreRun[1] ? core[1].envelope (0) : 0.0f;
    }
    v[MS_RandNote] = randNote;
    v[MS_CellNoise] = inst.value (CI_Source);
    for (int k = 0; k < 8; ++k) v[MS_Macro1 + k] = s.v[P_macro1 + k];
    for (int k = 0; k < 4; ++k) v[MS_PfX1 + k] = 2.0f * s.v[P_pfX1 + k] - 1.0f;   // the XY pads (bipolar)

    // random clock (Random Rate): smooth random, stepped random, sample & hold of LFO 1, chaos
    rndPh += (double) clampv (s.f (P_randRate), 0.01f, 50.0f) * dt;
    while (rndPh >= 1.0)
    {
        rndPh -= 1.0;
        rndA = rndB; rndB = nextRand();
        shVal = v[MS_Lfo1];
        chaosX = 3.91f * chaosX * (1.0f - chaosX);
        if (! (chaosX > 0.001f && chaosX < 0.999f)) chaosX = 0.37f;
    }
    v[MS_RandSmooth] = rndA + (rndB - rndA) * (float) (0.5 - 0.5 * std::cos (kPi * rndPh));
    v[MS_RandStep]   = rndB;
    v[MS_SampleHold] = shVal;
    v[MS_Chaos]      = 2.0f * chaosX - 1.0f;
    // slow analog-style wander, about one new point every 6 seconds
    driftPh2 += 0.16 * dt;
    while (driftPh2 >= 1.0) { driftPh2 -= 1.0; drA = drB; drB = nextRand(); }
    v[MS_Drift] = drA + (drB - drA) * (float) (0.5 - 0.5 * std::cos (kPi * driftPh2));

    v[MS_Gate]    = released ? 0.0f : 1.0f;
    v[MS_NoteOn]  = (float) std::exp (-t / 0.08);
    v[MS_NoteOff] = released && ampEnv.releaseTime >= 0.0 ? (float) std::exp (-(t - ampEnv.releaseTime) / 0.08) : 0.0f;
    v[MS_EnvFollow]   = clampv (envF * 2.0f, 0.0f, 1.0f);
    v[MS_AudioFollow] = clampv (audF * 2.0f, 0.0f, 1.0f);
    v[MS_TransFollow] = clampv ((trFast - trSlow) * 6.0f, 0.0f, 1.0f);
}

// Cell Instability's built-in routes: target, the parameters it moves, and how far at 100%
// (in normalised units). "skipZero": a control that's at zero (an oscillator switched off,
// no FM) stays at zero, so instability varies what's there rather than adding new things.
void Voice::applyInstability (Snapshot& sn, bool noteOn) const
{
    const auto& nt = normTable();
    const float amount = sn.v[P_ciAmount];
    struct E { int target, param; float depth; bool skipZero; };
    static const E table[] = {
        // pitch: +-50 cents at full (semitone dials span 24 semitones)
        { CI_Pitch, P_osc1Semi, 0.5f / 24.0f, false }, { CI_Pitch, P_osc2Semi, 0.5f / 24.0f, false }, { CI_Pitch, P_osc3Semi, 0.5f / 24.0f, false },
        { CI_Pitch, P_subSemi, 0.5f / 24.0f, false }, { CI_Pitch, P_osc4Semi, 0.5f / 24.0f, false }, { CI_Pitch, P_wt2Semi, 0.5f / 24.0f, false },
        { CI_Pitch, P_complexSemi, 0.5f / 24.0f, false }, { CI_Pitch, P_supersawSemi, 0.5f / 24.0f, false },
        { CI_Cutoff, P_filterCutoff, 0.25f, false }, { CI_Cutoff, P_filter2Cutoff, 0.25f, false },
        { CI_Res, P_filterRes, 0.2f, false }, { CI_Res, P_filter2Res, 0.2f, false },
        // levels move against each other, so the balance shifts rather than the volume
        { CI_Level, P_osc1Gain, 0.15f, true }, { CI_Level, P_osc2Gain, -0.15f, true }, { CI_Level, P_osc3Gain, 0.15f, true },
        { CI_Level, P_subGain, -0.15f, true }, { CI_Level, P_osc4Gain, 0.15f, true }, { CI_Level, P_wt2Gain, -0.15f, true },
        { CI_Level, P_complexGain, 0.15f, true }, { CI_Level, P_supersawGain, -0.15f, true },
        { CI_Fold, P_wmFold, 0.3f, false },
        { CI_Scan, P_osc4Position, 0.2f, false }, { CI_Scan, P_wt2Position, -0.2f, false },
        { CI_Fm, P_arFm, 0.15f, true }, { CI_Fm, P_complexFm, 0.15f, true }, { CI_Fm, P_fmAmount, 0.1f, true },
        { CI_Dna, P_dnaAmount, 0.3f, false }, { CI_Dna, P_dnaChar, -0.2f, false },
        { CI_Env, P_ampA, 0.12f, false }, { CI_Env, P_ampD, 0.12f, false }, { CI_Env, P_ampR, -0.12f, false },
        { CI_Env, P_fEnvA, -0.12f, false }, { CI_Env, P_fEnvD, 0.12f, false }, { CI_Env, P_fEnvR, 0.12f, false },
        { CI_Reso, P_resPitch, 0.01f, false }, { CI_Reso, P_resDecay, 0.15f, false } };
    static const int amountParam[] = { P_ciPitch, P_ciCutoff, P_ciRes, P_ciLevel, P_ciFold, P_ciScan, P_ciFm, P_ciDna, P_ciEnv, P_ciReso };
    for (const auto& e : table)
    {
        if ((e.target == CI_Env) != noteOn) continue;    // envelope times only at note-on; the rest continuously
        const float a = amount * sn.v[amountParam[e.target]];
        if (a <= 1.0e-4f) continue;
        const float base = sn.v[e.param];
        const float b = normFast (nt, e.param, base);
        if (e.skipZero && b <= 1.0e-6f) continue;
        sn.v[e.param] = plainFast (nt, e.param, b + a * e.depth * inst.value (e.target));
    }
}

// Cycle length in beats of an LFO Sync choice (0 = off)
static double lfoSyncBeats (int idx)
{
    if (idx <= 0 || idx >= kListLfoSync.size) return 0.0;
    return syncBeats (kListLfoSync.keys[idx]);
}

//==============================================================================
// Unison: side voices at symmetric positions around the oscillator (which stays the centre voice).
static float uniSidePos (int j, int m)
{
    // m side voices: +1, -1, then halves... for m = 1 just +1
    const int pairs = (m + 1) / 2;
    const float mag = (float) (j / 2 + 1) / (float) pairs;
    return (j % 2 == 0) ? mag : -mag;
}

void Voice::startUnison (const Snapshot& s)
{
    const bool same = s.v[P_uniSamePhase] > 0.5f;
    for (auto& row : wtU) for (auto& u : row) { u.pos = -1.0; u.loaded = false; u.stopped = false; }
    for (int k = 0; k < kUniOscs; ++k)
    {
        const int n = clampv (s.i (P_uni1Voices + kUniStride * k), 1, 8);
        if (n <= 1) continue;   // (no random numbers drawn: patches without unison play exactly as before)
        for (int j = 0; j < kUniSide; ++j)
        {
            const double ph = same ? std::fmod (0.137 + 0.618034 * (j + 1) + 0.23 * k, 1.0) : (double) voiceRand01();
            if (k < 3) uniPh[k][j] = ph;
            else if (k == 5) { uniPhCar[j] = ph; uniPhMod[j] = same ? std::fmod (0.31 + 0.381966 * (j + 1), 1.0) : (double) voiceRand01(); }
            else { wtU[k - 3][j].pos = -1.0 - ph; wtU[k - 3][j].stopped = false; wtU[k - 3][j].loaded = false; }   // (start offset set on the first update)
        }
    }
}

void Voice::updateUnison (const Snapshot& s)
{
    uniAny = false;
    for (int k = 0; k < kUniOscs; ++k)
    {
        const int b = P_uni1Voices + kUniStride * k;
        const int n = clampv (s.i (b), 1, 8);
        uniN[k] = n;
        if (n <= 1) { uniGc[k] = 1.0f; continue; }
        const int m = n - 1;
        const float det = clampv (s.f (b + 1), 0.0f, 100.0f), spread = clampv (s.f (b + 2), 0.0f, 1.0f), blend = clampv (s.f (b + 3), 0.0f, 1.0f);
        const bool ssCurve = s.i (b + 4) == 1;
        // level: the centre at 1, the sides at Blend, normalised so the sum keeps about the same power
        const float norm = 1.0f / std::sqrt (1.0f + (float) m * blend * blend);
        uniGc[k] = norm; uniGs[k] = blend * norm;
        for (int j = 0; j < m; ++j)
        {
            const float p = uniSidePos (j, m);
            const float shaped = ssCurve ? (p < 0 ? -1.0f : 1.0f) * (0.3f * std::abs (p) + 0.7f * std::abs (p) * p * p) : p;
            uniRatio[k][j] = (float) std::exp2 (det * shaped / 1200.0);
            const float pan = clampv (p * spread, -1.0f, 1.0f);
            const float a = (pan + 1.0f) * 0.25f * (float) kPi;
            uniPL[k][j] = std::cos (a) * 1.41421356f; uniPR[k][j] = std::sin (a) * 1.41421356f;
        }
        if (spread > 0.001f) uniAny = true;
        if (k == 3 || k == 4)
        {
            // sample playback: the side players follow the main one's settings, at their own rate
            const WtPlayer& w = wt[k - 3];
            for (int j = 0; j < m; ++j)
            {
                WtPlayer& u = wtU[k - 3][j];
                u.wav = w.wav; u.loop = w.loop; u.reverse = w.reverse; u.ls = w.ls; u.le = w.le; u.comp = w.comp;
                u.rate.v = w.rate.v * uniRatio[k][j];
                if (w.loaded && w.pos >= 0.0 && u.pos < 0.0)
                {
                    // start alongside the main player, a little later (up to 30 ms) unless Same Phase
                    const double off = (-u.pos - 1.0) * 0.03 * w.wav->sampleRate;
                    u.pos = w.pos + off;
                    if (u.loop && u.pos >= u.le) u.pos = u.ls + std::fmod (u.pos - u.ls, std::max (1.0e-6, u.le - u.ls));
                    u.stopped = false;
                }
                u.loaded = w.loaded && u.pos >= 0.0;
            }
        }
    }
}

void Voice::advanceLfos (const Snapshot& s, int n)
{
    const double secs = n / sr;
    for (int k = 0; k < 4; ++k)
    {
        const int xb = P_lfo1Sync + kLfoXStride * k;
        const double cyc = lfoSyncBeats (s.i (xb));
        if (cyc > 0.0)
        {
            const double hz = s.fxBeatInc * sr / cyc;   // cycles per second at the current tempo
            lfoPh[k] = wrap01 (lfoPh[k] + hz * secs);
            lfoElapsed[k] += hz * secs;
        }
        else
        {
            lfoPh[k] = wrap01 (lfoPh[k] + s.f (P_lfo1Rate + 3 * k) * secs);
            lfoElapsed[k] += s.f (P_lfo1Rate + 3 * k) * secs;
        }
        lfoDepthNow[k] = s.f (P_lfo1Depth + 3 * k);
        lfoXMod[k][0] = s.f (xb + 2); lfoXMod[k][1] = s.f (xb + 4); lfoXMod[k][2] = s.f (xb + 6);
    }
    for (int k = 0; k < 2; ++k)
    {
        const bool synced = s.v[P_mseg1Sync + 2 * k] > 0.5f;
        mseg[k].advance (synced ? s.fxBeatInc * n : secs, clampv (s.f (P_mseg1Scale + 2 * k), 0.25f, 4.0f));
    }
}

// Pitch envelope: up to the amount over Attack, back to 0 over Decay (Curve > 0: a fast drop like an 808)
void Voice::updateMixer (const Snapshot& s, bool first, float c)
{
    bool anySolo = false;
    for (int k = 0; k < kMxStrips; ++k) anySolo |= s.v[mxParam (k) + 2] > 0.5f;
    bool active = false;
    for (int k = 0; k < kMxStrips; ++k)
    {
        const int b = mxParam (k);
        float pan = clampv (s.f (b), -1.0f, 1.0f);
        if (std::abs (pan) < 1.0e-6f) pan = 0.0f;
        const bool muted = s.v[b + 1] > 0.5f || (anySolo && s.v[b + 2] <= 0.5f);
        mxPan[k].target = pan; mxGate[k].target = muted ? 0.0f : 1.0f;
        if (first) { mxPan[k].v = mxPan[k].target; mxGate[k].v = mxGate[k].target; }
        else
        {
            mxPan[k].step (c); mxGate[k].step (c);
            if (std::abs (mxPan[k].v - mxPan[k].target) < 1.0e-5f) mxPan[k].v = mxPan[k].target;
            if (std::abs (mxGate[k].v - mxGate[k].target) < 1.0e-5f) mxGate[k].v = mxGate[k].target;
        }
        if (k != kMxLayerStrip)   // (the Break Layer isn't a voice source: its strip never puts the voices on the Mixer path)
            active |= mxPan[k].v != 0.0f || mxGate[k].v != 1.0f || mxPan[k].target != 0.0f || mxGate[k].target != 1.0f;
        // balance: the centre leaves both sides at full level; panning turns the other side down
        mxL[k] = mxGate[k].v * std::min (1.0f, 1.0f - mxPan[k].v);
        mxR[k] = mxGate[k].v * std::min (1.0f, 1.0f + mxPan[k].v);
    }
    mxActive = active;
}

void Voice::updatePitchEnv (const Snapshot& s)
{
    const float amt = s.f (P_pEnvAmt);
    if (std::abs (amt) < 1.0e-4f) { for (auto& r : peRatio) r = 1.0f; return; }
    const double A = std::max (0.0, (double) s.f (P_pEnvA)), D = std::max (0.001, (double) s.f (P_pEnvD));
    const float e = std::exp2 (2.0f * clampv (s.f (P_pEnvCurve), -1.0f, 1.0f));
    double env;
    if (t < A) env = std::pow (t / A, 1.0 / e);
    else env = t - A < D ? std::pow (1.0 - (t - A) / D, (double) e) : 0.0;
    const float vel = clampv (s.f (P_pEnvVel), 0.0f, 1.0f);
    const float semis = amt * (float) env * (1.0f - vel + vel * velocity);
    const float r = (float) std::exp2 (semis / 12.0);
    const int tgt = clampv (s.i (P_pEnvTarget), 0, 8);
    for (int i = 0; i < 8; ++i) peRatio[i] = (tgt == 0 || tgt == i + 1) ? r : 1.0f;
}

void Voice::updateLfoOut (const Snapshot& s, int offset, const ModContext& mc)
{
    const double beat = s.fxBeat + s.fxBeatInc * offset;
    const double secPerBeat = s.fxBeatInc > 0.0 ? 1.0 / (s.fxBeatInc * sr) : 0.5;
    for (int k = 0; k < 4; ++k)
    {
        const int xb = P_lfo1Sync + kLfoXStride * k;
        const double cyc = lfoSyncBeats (s.i (xb));
        const int retrig = s.i (xb + 1);
        const bool oneShot = s.i (xb + 3) == 1;
        const double phOff = (double) clampv (lfoXMod[k][0], 0.0f, 1.0f);
        double ph;
        if (retrig == 0 || (retrig == 1 && cyc <= 0.0 && mc.lfoFree == nullptr))
        {
            // each note: the voice's own phase (the original behaviour)
            ph = oneShot ? std::min (phOff + lfoElapsed[k], 0.9999999) : wrap01 (lfoPh[k] + phOff);
        }
        else if (retrig == 1)
        {
            // free: one phase shared by every voice (locked to the song position when synced)
            ph = cyc > 0.0 ? wrap01 (beat / cyc + phOff)
                           : wrap01 (mc.lfoFree[k] + s.f (P_lfo1Rate + 3 * k) * offset / sr + phOff);
        }
        else
        {
            // each bar / host start: cycles since that point
            double origin;
            if (retrig == 2)
            {
                const double bpb = s.fxBeatsPerBar > 0.0 ? s.fxBeatsPerBar : 4.0;
                origin = s.fxBarOrigin + std::floor ((beat - s.fxBarOrigin) / bpb) * bpb;
            }
            else origin = s.fxHostStart;
            const double beatsIn = std::max (0.0, beat - origin);
            const double el = cyc > 0.0 ? beatsIn / cyc : beatsIn * secPerBeat * s.f (P_lfo1Rate + 3 * k);
            ph = oneShot ? std::min (phOff + el, 0.9999999) : wrap01 (el + phOff);
        }
        lfoPhNow[k] = ph;
        float raw;
        if (s.v[xb + 7] > 0.5f && mc.lfoShapes != nullptr) raw = 2.0f * mc.lfoShapes->eval (k, ph) - 1.0f;
        else raw = lfoShape (clampv (s.i (P_lfo1Wave + 3 * k), 0, 3), ph);
        if (s.v[xb + 5] > 0.5f) raw = 0.5f + 0.5f * raw;                     // unipolar
        const float fade = lfoXMod[k][2];
        if (fade > 1.0e-4f && t < fade) raw *= (float) (t / fade);          // fade in after the note starts
        const float sm = clampv (lfoXMod[k][1], 0.0f, 1.0f);
        if (sm > 1.0e-4f && lfoSmInit[k])
        {
            const double tc = 0.001 + 0.5 * sm * sm;                         // up to half a second of slew
            lfoSm[k] += (raw - lfoSm[k]) * (float) (1.0 - std::exp (-(double) kCtrl / (sr * tc)));
            raw = lfoSm[k];
        }
        else lfoSm[k] = raw;
        lfoSmInit[k] = true;
        lfoOut[k] = raw;
    }
}

struct WtParamSet { int semi, det, oct, root, pos, win, mode, ls, le, dir, norm, gain; int mGain, mDet, mPos, mWin, mLs, mLe; };
static const WtParamSet kWtParams[2] = {
    { P_osc4Semi, P_osc4Detune, P_osc4Oct, P_osc4Root, P_osc4Position, P_osc4Window, P_osc4LoopMode, P_osc4LoopStart, P_osc4LoopEnd,
      P_osc4Direction, P_osc4Normalize, P_osc4Gain, MT_osc4Gain, MT_osc4Detune, MT_osc4Position, MT_osc4Window, MT_osc4LoopStart, MT_osc4LoopEnd },
    { P_wt2Semi, P_wt2Detune, P_wt2Oct, P_wt2Root, P_wt2Position, P_wt2Window, P_wt2LoopMode, P_wt2LoopStart, P_wt2LoopEnd,
      P_wt2Direction, P_wt2Normalize, P_wt2Gain, MT_wt2Gain, MT_wt2Detune, MT_wt2Position, MT_wt2Window, MT_wt2LoopStart, MT_wt2LoopEnd } };

void Voice::updateWt (int slot, const Snapshot& s, const ModState& mod, double bendC, float cPort, float c10)
{
    WtPlayer& w = wt[slot];
    const WtParamSet& P = kWtParams[slot];
    w.level.target = w.loaded ? hardMuted (s.f (P.gain), mod[P.mGain], 2) : 0.0f;
    w.level.step (c10);
    w.comp = (w.loaded && s.i (P.norm) == 0) ? 1.0f / std::max (0.25f, w.wav->peak) : 1.0f;
    if (! w.loaded) return;

    const double det = clampv (s.f (P.det) + mod[P.mDet], -50.0f, 50.0f);
    const double f = baseFreq * pow2 (std::round (s.f (P.oct))) * pow2 ((100.0 * s.f (P.semi) + det + bendC) / 1200.0);
    const double root = midiToFreq (std::round (s.f (P.root)));
    w.rate.target = (float) clampv (f / std::max (10.0, root), 0.01, 16.0);
    w.rate.step (cPort);

    const double len = w.wav->length;
    const int mode = s.i (P.mode);
    const double scan = clampv (s.f (P.pos) + mod[P.mPos], 0.0f, 1.0f);
    const double win  = clampv (s.f (P.win) + mod[P.mWin], 0.001f, 1.0f);
    const double ms   = clampv (s.f (P.ls) + mod[P.mLs], 0.0f, 1.0f);
    const double me   = clampv (s.f (P.le) + mod[P.mLe], 0.0f, 1.0f);
    double startPos = 0;
    if (mode == 3)      { w.loop = false; startPos = len * std::min (scan, 0.999); w.ls = 0; w.le = len; }
    else if (mode == 0) { w.loop = true; startPos = 0; w.ls = 0; w.le = len; }
    else if (mode == 2)
    {
        double a = std::min (ms, me), b = std::max (ms, me);
        if (b - a < 0.001) b = std::min (1.0, a + 0.001);
        w.loop = true; startPos = len * a; w.ls = len * a; w.le = len * b;
    }
    else
    {
        const double loopLen = len * win;
        const double maxStart = std::max (0.0, len - loopLen);
        startPos = clampv (scan * maxStart, 0.0, maxStart);
        w.loop = true; w.ls = startPos; w.le = std::min (len, startPos + loopLen);
    }
    w.le = std::max (w.ls + 0.001 * w.wav->sampleRate, w.le);
    w.reverse = s.i (P.dir) == 1;
    w.start = startPos;
    if (w.pos < 0.0) { w.pos = startPos; w.stopped = false; }
}

void Voice::updateControl (const Snapshot& s, const WaveSample* const* wavs, const ModState& mod, bool first)
{
    const double dtc = kCtrl / sr;
    const float cPort = first ? 1.0f : smoothCoef (dtc, std::max (0.001, (double) s.f (P_porta)));
    const float c10 = first ? 1.0f : smoothCoef (dtc, 0.01);
    const float c20 = first ? 1.0f : smoothCoef (dtc, 0.02);

    const double bendC = s.bendSemis * 100.0;
    const double pitch = mod[MT_pitch] + bendC;
    auto oscF = [&] (int octP, double cents) { return baseFreq * pow2 (std::round (s.f (octP))) * pow2 (cents / 1200.0); };

    // Analog Drift: each oscillator wanders slowly and independently by a few cents
    const float driftAmt = s.f (P_analogDrift);
    for (int k = 0; k < 5; ++k)
    {
        driftPh[k] = wrap01 (driftPh[k] + driftRate[k] * dtc);
        driftCents[k] = driftAmt * 6.0f * (float) (0.65 * std::sin (2.0 * kPi * driftPh[k]) + 0.35 * std::sin (2.0 * kPi * (2.37 * driftPh[k] + 0.3 * k)));
    }
    auto semi = [&] (int p) { return 100.0 * s.f (p); };   // whole semitones unless the matrix sweeps them
    f1.target = (float) oscF (P_osc1Oct, semi (P_osc1Semi) + s.f (P_osc1Detune) + pitch + driftCents[0]);
    f2.target = (float) oscF (P_osc2Oct, semi (P_osc2Semi) + s.f (P_osc2Detune) + pitch + driftCents[1]);
    f3.target = (float) oscF (P_osc3Oct, semi (P_osc3Semi) + s.f (P_osc3Detune) + pitch + driftCents[2]);
    fSub.target = (float) oscF (P_subOct, semi (P_subSemi) + pitch + driftCents[3]);
    f1.step (cPort); f2.step (cPort); f3.step (cPort); fSub.step (cPort);

    wave1 = kWaveChoiceToId[clampv (s.i (P_osc1Wave), 0, 9)];
    wave2 = kWaveChoiceToId[clampv (s.i (P_osc2Wave), 0, 9)];
    wave3 = kWaveChoiceToId[clampv (s.i (P_osc3Wave), 0, 9)];
    waveSub = kSubChoiceToId[clampv (s.i (P_subWave), 0, 4)];
    waveA = kWaveChoiceToId[clampv (s.i (P_complexWaveA), 0, 9)];
    waveB = kWaveBChoiceToId[clampv (s.i (P_complexWaveB), 0, 9)];

    for (int k = 0; k < 2; ++k)
    {
        const WaveSample* w = wavs != nullptr ? wavs[k] : nullptr;
        if (w != wt[k].wav) { wt[k].wav = w; wt[k].pos = -1.0; wt[k].stopped = false; }
        wt[k].loaded = w != nullptr && w->length > 1;
    }
    const bool wtLoaded = wt[0].loaded;
    // the Granular Synth: on with a sample loaded (its level fades, so switching it never clicks)
    {
        const WaveSample* g = wavs != nullptr ? wavs[2] : nullptr;
        if (g != gsWav) { gsWav = g; if (g != nullptr) gran.start (order * 131u + (uint64_t) key); }
        const bool on = s.v[P_gsOn] > 0.5f && gsWav != nullptr && gsWav->length > 1;
        lGs.target = on ? clampv (s.f (P_gsLevel), 0.0f, 1.0f) : 0.0f;
        if (first) lGs.v = lGs.target; else lGs.step (c10);
        if (! on && lGs.v < 1.0e-5f) lGs.v = 0.0f;
        gsRun = gsWav != nullptr && (on || lGs.v > 0.0f);
        gsDnaSlot = clampv (s.i (P_gsDna), 0, 2);
        if (gsRun)
        {
            gsP.position = s.f (P_gsPosition); gsP.scan = s.f (P_gsScan); gsP.sprayMs = s.f (P_gsSpray); gsP.sizeMs = clampv (s.f (P_gsSize), 5.0f, 500.0f);
            gsP.density = clampv (s.f (P_gsDensity), 1.0f, 100.0f); gsP.shape = clampv (s.i (P_gsShape), 0, 4);
            gsP.semis = s.f (P_gsSemi) + s.f (P_gsCents) * 0.01f; gsP.pitchRand = s.f (P_gsPitchRand);
            gsP.reverse = s.f (P_gsReverse); gsP.spread = s.f (P_gsSpread);
            gsP.maxGrains = s.i (P_quality) == 0 ? 8 : (s.i (P_quality) == 2 ? 48 : 24);
            const double rootHz = 440.0 * pow2 ((clampv (s.f (P_gsRoot), 0.0f, 127.0f) - 69.0) / 12.0);
            const double pe = s.i (P_pEnvTarget) == 0 ? (double) peRatio[0] : 1.0;   // the pitch envelope when it targets all oscillators
            gsP.ratio = (s.v[P_gsKeyTrack] > 0.5f ? baseFreq / rootHz : 1.0) * pow2 (pitch / 1200.0) * pe;   // (pitch: bend and the LFO pitch slots)
        }
    }

    // the Reese engine: its voices' frequencies for this block (Beats: the beating at the song's tempo)
    lRs.target = clampv (s.f (P_rsLevel), 0.0f, 1.0f);
    if (first) lRs.v = lRs.target; else lRs.step (c10);
    if (lRs.target <= 0.0f && lRs.v < 1.0e-5f) lRs.v = 0.0f;
    rsRun = lRs.target > 0.0f || lRs.v > 0.0f;
    if (rsRun)
    {
        ReeseSettings rs;
        rs.voices = clampv (s.i (P_rsVoices), 0, 2); rs.wave = clampv (s.i (P_rsWave), 0, 3); rs.mode = clampv (s.i (P_rsDetMode), 0, 2);
        rs.beat = clampv (s.i (P_rsBeat), 0, 12); rs.ratio = clampv (s.i (P_rsRatio), 0, 2); rs.retrig = clampv (s.i (P_rsRetrig), 0, 2);
        rs.cents = s.f (P_rsCents); rs.hz = s.f (P_rsHz); rs.twist = s.f (P_rsTwist); rs.twistRate = s.f (P_rsTwistRate);
        rs.width = clampv (s.f (P_rsWidth), 0.0f, 1.0f); rs.rotate = clampv (s.f (P_rsRotate), 0.0f, 1.0f); rs.rotRate = s.f (P_rsRotRate);
        rs.monoHz = s.f (P_rsMonoBelow); rs.notch = clampv (s.f (P_rsNotch), 0.0f, 1.0f); rs.notchFreq = s.f (P_rsNotchFreq);
        rs.notchMove = clampv (s.f (P_rsNotchMove), 0.0f, 1.0f); rs.notchSync = s.v[P_rsNotchSync] > 0.5f; rs.notchRate = s.f (P_rsNotchRate);
        rs.quality = clampv (s.i (P_quality), 0, 2);
        rs.sub = clampv (s.f (P_rsSub), 0.0f, 1.0f); rs.drive = clampv (s.f (P_rsDrive), 0.0f, 1.0f); rs.tone = clampv (s.f (P_rsTone), 0.0f, 1.0f);
        {
            // (v0.1.70) formants, bloom, split drive, shaper, comb / phaser, Hoover, cross-FM / sync, drift
            const double tempo = s.fxTempo > 0.0 ? s.fxTempo : (double) s.f (P_seqTempo);
            rs.fmtOn = s.v[P_rsFmtOn] > 0.5f; rs.fmtBefore = s.i (P_rsFmtPos) == 1; rs.fmtMove = clampv (s.i (P_rsFmtMove), 0, 2); rs.fmtShape = clampv (s.i (P_rsFmtShape), 0, 3);
            rs.fmtVowel = clampv (s.f (P_rsFmtVowel), 0.0f, 1.0f); rs.fmtRes = clampv (s.f (P_rsFmtRes), 0.0f, 1.0f); rs.fmtMix = clampv (s.f (P_rsFmtMix), 0.0f, 1.0f);
            rs.fmtShift = s.f (P_rsFmtShift); rs.fmtDepth = clampv (s.f (P_rsFmtDepth), 0.0f, 1.0f);
            rs.fmtLfoInc = tempo / 60.0 / kRsLfoSyncBeats[clampv (s.i (P_rsFmtSync), 0, 6)] / sr;
            const int bs = clampv (s.i (P_rsBloomSync), 0, 5);
            rs.bloom = bs > 0 ? (float) (kRsBloomSyncBeats[bs] * 60.0 / std::max (20.0, tempo)) : std::max (0.0f, s.f (P_rsBloom));
            rs.bloomRev = s.v[P_rsBloomRev] > 0.5f; rs.bloomCurve = clampv (s.i (P_rsBloomCurve), 0, 1);
            rs.split = s.v[P_rsSplitOn] > 0.5f; rs.splitHz = s.f (P_rsSplitFreq); rs.lowDrive = clampv (s.f (P_rsLowDrive), 0.0f, 1.0f);
            rs.highDrive = clampv (s.f (P_rsHighDrive), 0.0f, 1.0f); rs.highTone = s.f (P_rsHighTone);
            rs.shp = clampv (s.i (P_rsShpType), 0, 4); rs.shpAmt = s.f (P_rsShpAmt); rs.shpSym = s.f (P_rsShpSym);
            static const int stages[3] = { 4, 8, 12 };
            rs.cp = clampv (s.i (P_rsCpMode), 0, 2); rs.cpStages = stages[clampv (s.i (P_rsCpStages), 0, 2)];
            rs.cpFb = s.f (P_rsCpFb); rs.cpDepth = s.f (P_rsCpDepth); rs.cpOffset = s.f (P_rsCpOffset); rs.cpMix = s.f (P_rsCpMix);
            const int cps = clampv (s.i (P_rsCpSync), 0, 7);
            rs.cpLfoInc = (cps > 0 ? tempo / 60.0 / kRsCpSyncBeats[cps] : (double) std::max (0.0f, s.f (P_rsCpRate))) / sr;
            rs.pw = s.f (P_rsPw); rs.pwmRate = s.f (P_rsPwmRate); rs.pwmDepth = s.f (P_rsPwmDepth); rs.stack = clampv (s.i (P_rsStack), 0, 2);
            rs.pdMode = clampv (s.i (P_rsPdMode), 0, 3); rs.pdAmt = s.f (P_rsPdAmt); rs.pdTime = s.f (P_rsPdTime);
            const float envAmt = s.f (P_rsFmEnvAmt);
            const float env = envAmt != 0.0f ? (float) (s.i (P_rsFmEnvSrc) == 0 ? ampEnv.eval (t) : modEnv[0].eval (t)) * envAmt : 0.0f;
            rs.fm = clampv (s.f (P_rsFm) + env, 0.0f, 1.0f);
            rs.sync = s.v[P_rsSync] > 0.5f; rs.syncRatio = clampv (s.f (P_rsSyncRatio) * std::pow (2.0f, 2.0f * env), 1.0f, 16.0f);
            rs.drift = clampv (s.f (P_rsDrift), 0.0f, 1.0f); rs.driftRate = s.f (P_rsDriftRate);
        }
        const double pe = s.i (P_pEnvTarget) == 0 ? (double) peRatio[0] : 1.0;   // the pitch envelope when it targets all oscillators
        const double fr = baseFreq * pow2 (std::round (s.f (P_rsOct))) * pow2 ((100.0 * std::round (s.f (P_rsSemi)) + pitch) / 1200.0) * pe;
        reese.setFrequency (fr, s.fxTempo > 0.0 ? s.fxTempo : (double) s.f (P_seqTempo), rs);
    }

    // the Core oscillator slots: their settings for this block (the kernel takes them in render, per sub-block)
    for (int c = 0; c < 2; ++c)
    {
        const bool on = s.i (coreParam (c, CS_Engine)) >= 1;
        lCore[c].target = on ? clampv (s.f (coreParam (c, CS_Level)), 0.0f, 1.0f) : 0.0f;
        if (first) lCore[c].v = lCore[c].target; else lCore[c].step (c10);
        if (lCore[c].target <= 0.0f && lCore[c].v < 1.0e-5f) lCore[c].v = 0.0f;
        coreRun[c] = lCore[c].target > 0.0f || (lCore[c].v > 0.0f && core[c].isStarted());
        coreOpsOnly[c] = coreRun[c] && s.i (coreEnvModeParam (c)) == 1;
        if (! coreRun[c]) continue;
        CoreSettings& cs = coreSet[c];
        static const int uniN[6] = { 1, 2, 3, 4, 6, 8 };
        cs.uni = uniN[clampv (s.i (coreParam (c, CS_Uni)), 0, 5)];
        cs.detune = clampv (s.f (coreParam (c, CS_Detune)), 0.0f, 100.0f);
        cs.spread = clampv (s.f (coreParam (c, CS_Spread)), 0.0f, 1.0f);
        cs.bright = clampv (s.f (coreParam (c, CS_Bright)), 0.0f, 4.0f);
        cs.envSpd = clampv (s.f (coreParam (c, CS_EnvSpd)), 0.05f, 16.0f);
        cs.quality = clampv (s.i (P_quality), 0, 2);
        for (int k = 0; k < CoreKernel::kOps; ++k)
        {
            auto& o = cs.op[k];
            auto op = [&] (int w) { return s.f (coreOpParam (c, k, w)); };
            o.wave = clampv (s.i (coreOpParam (c, k, CO_Wave)), 0, 7);
            o.ratio = clampv (op (CO_Ratio), 0.0f, 64.0f); o.fixed = op (CO_Fixed) > 0.5f; o.hz = clampv (op (CO_Hz), 0.0f, 24000.0f);
            o.fine = op (CO_Fine); o.level = clampv (op (CO_Level), 0.0f, 1.0f); o.vel = clampv (op (CO_Vel), 0.0f, 1.0f);
            o.key = clampv (op (CO_Key), -1.0f, 1.0f);
            o.dly = std::max (0.0f, op (CO_Dly)); o.att = std::max (0.0f, op (CO_Att)); o.hold = std::max (0.0f, op (CO_Hold));
            o.dec = std::max (0.001f, op (CO_Dec)); o.sus = clampv (op (CO_Sus), 0.0f, 1.0f); o.rel = std::max (0.001f, op (CO_Rel));
            for (int j = 0; j < 6; ++j) o.m[j] = clampv (op (CO_M1 + j), 0.0f, 24.0f);
            o.out = clampv (op (CO_Out), 0.0f, 1.0f);
        }
        const double pe = s.i (P_pEnvTarget) == 0 ? (double) peRatio[0] : 1.0;
        coreHz[c] = baseFreq * pow2 (std::round (s.f (coreParam (c, CS_Oct)))) * pow2 ((100.0 * std::round (s.f (coreParam (c, CS_Semi))) + s.f (coreParam (c, CS_Fine)) + pitch) / 1200.0) * pe;
        if (on && ! core[c].isStarted())
        {
            core[c].start (cs, clampv (s.i (coreParam (c, CS_Retrig)), 0, 2), (uint32_t) (order * 131u + (uint64_t) key + 977u * (uint64_t) c), key, velocity);
            if (released) core[c].release();
        }
    }
    opsOnlyAny = coreOpsOnly[0] || coreOpsOnly[1];

    // --- mixer levels
    l1.target   = hardMuted (s.f (P_osc1Gain), mod[MT_osc1Gain], 2);
    l2.target   = hardMuted (s.f (P_osc2Gain), mod[MT_osc2Gain], 2);
    l3.target   = hardMuted (s.f (P_osc3Gain), mod[MT_osc3Gain], 2);
    lSub.target = hardMuted (s.f (P_subGain), mod[MT_subGain], 2);
    lC.target   = hardMuted (s.f (P_complexGain), mod[MT_complexGain], 2);
    lSS.target  = hardMuted (s.f (P_supersawGain), mod[MT_supersawGain], 2);
    for (S* x : { &l1, &l2, &l3, &lSub, &lC, &lSS }) x->step (c10);
    updateWt (0, s, mod, bendC, cPort, c10);
    updateWt (1, s, mod, bendC, cPort, c10);

    legacyFm.target = clampv (s.f (P_fmAmount) + mod[MT_fmAmount], 0.0f, 4000.0f);
    const float rm = hardMuted (s.f (P_ringMix), mod[MT_ringMix], 1);
    ringMix.target = rm;
    dry.target = std::max (0.0f, 1.0f - rm * 0.7f);
    const float rg = hardMuted (s.f (P_ringGain), mod[MT_ringGain], 1);
    ringGainS.target = rg;
    legacyFm.step (c10); ringMix.step (c10); dry.step (c10); ringGainS.step (c10);

    // --- "source mute": silence the voice when every source is at zero
    const float audible = l1.target + l2.target + l3.target + lSub.target + wt[0].level.target + wt[1].level.target + lC.target + lSS.target
                        + rm * std::max (0.0f, s.f (P_ringGain)) + lGs.target + lRs.target + lCore[0].target + lCore[1].target;   // (+ 0 when the Granular Synth, the Reese and the Core slots are off: unchanged)
    const float mute = audible > 0.0001f ? 1.0f : 0.0f;
    srcMute.target = mute;
    srcMute.step (first ? 1.0f : smoothCoef (dtc, mute > 0 ? 0.008 : 0.02));
    const float ampMod = clampv (1.0f + mod[MT_amp] + accentBoost, 0.0f, 2.5f);
    vGain.target = mute * ampMod * velGain;
    vGain.step (c20);

    // --- filter
    filter.setCharacter (s.f (P_flt1CharAmt));
    filter.configure (s.i (P_filterMode), s.i (P_filterType), s.i (P_filterSlope), first, clampv (s.i (P_flt1Char), 0, CM_COUNT - 1));
    filter.setAnalog (s.f (P_warmth), s.f (P_bassKeep));
    const float fe = (float) filtEnv.eval (t);
    // Cutoff: knob (with any matrix routes), accent, the filter envelope (Env Amt, 100% = +10 kHz
    // at the envelope's peak) and the classic LFO/Env slots, all in Hz as before; then key tracking
    // and the filter LFO as octave offsets, so they act the same at every cutoff.
    // Key tracking and the filter LFO as octave offsets
    auto octaves = [&] (int pKt, int pLfoAmt, int pLfoSrc)
    {
        double octs = 0.0;
        const float kt = s.f (pKt);
        if (kt > 0.0f) octs += kt * std::log2 (std::max (1.0, baseFreq) / 261.6256);        // 100%: follows the keyboard from middle C
        const float la = s.f (pLfoAmt);
        if (la != 0.0f)
        {
            const int src = clampv (s.i (pLfoSrc), 0, 3);
            octs += la * 4.0 * lfoOut[src] * lfoDepthNow[src];   // 100% = +-4 octaves
        }
        return octs;
    };
    float cut = clampv (s.f (P_filterCutoff) + filterAccent + s.f (P_fEnvAmt) * fe + mod[MT_cutoff], 20.0f, 20000.0f);
    double octs = octaves (P_filterKeyTrack, P_filterLfoAmt, P_filterLfoSrc);
    if (octs != 0.0) cut = clampv ((float) (cut * pow2 (octs)), 20.0f, 20000.0f);
    cutoff.target = cut;
    res.target = clampv (s.f (P_filterRes) + mod[MT_resonance], 0.1f, 30.0f);
    filtDrive.target = clampv (s.f (P_filterDrive) + mod[MT_drive], 1.0f, 30.0f);
    filtMix.target = clampv (s.f (P_filterMix), 0.0f, 1.0f);
    cutoff.step (c10); res.step (c10); filtDrive.step (c10); filtMix.step (c10);
    filter.setDrive (filtDrive.v);
    filter.setMix (filtMix.v);
    filter.update (cutoff.v, res.v, sr);

    // --- Filter 2: off, after Filter 1 (serial), beside it (parallel), or beside it in stereo (split)
    const int mode = s.i (P_filter2On) == 0 ? R_Off : (s.i (P_filterRouting) == 0 ? R_Serial : (s.i (P_filterStereoSplit) != 0 ? R_Split : R_Parallel));
    if (first) { routeMode = mode; routeFade = 0; }
    else if (mode != routeMode)
    {
        // crossfade from the old routing to the new; a Filter 2 that was off starts from silence
        f2old = filter2;
        oldRouteMode = routeMode;
        if (routeMode == R_Off) filter2.reset();
        routeMode = mode;
        routeFade = FilterUnit::kFadeLen;
    }
    balS.target = clampv (s.f (P_filterBalance), 0.0f, 1.0f);
    balS.step (c10);
    if (routeMode != R_Off || routeFade > 0)
    {
        filter2.setCharacter (s.f (P_flt2CharAmt));
        filter2.configure (s.i (P_filter2Mode), s.i (P_filter2Type), s.i (P_filter2Slope), first || filter2Fresh, clampv (s.i (P_flt2Char), 0, CM_COUNT - 1));
        filter2Fresh = false;
        filter2.setAnalog (s.f (P_warmth), s.f (P_bassKeep));
        float c2 = clampv (s.f (P_filter2Cutoff) + filterAccent + s.f (P_filter2EnvAmt) * fe, 20.0f, 20000.0f);
        const double o2 = octaves (P_filter2KeyTrack, P_filter2LfoAmt, P_filter2LfoSrc);
        if (o2 != 0.0) c2 = clampv ((float) (c2 * pow2 (o2)), 20.0f, 20000.0f);
        cutoff2.target = c2;
        res2.target = clampv (s.f (P_filter2Res), 0.1f, 30.0f);
        filtDrive2.target = clampv (s.f (P_filter2Drive), 1.0f, 30.0f);
        filtMix2.target = clampv (s.f (P_filter2Mix), 0.0f, 1.0f);
        const float k2 = first || jumpF2 ? 1.0f : c10;
        jumpF2 = false;
        cutoff2.step (k2); res2.step (k2); filtDrive2.step (k2); filtMix2.step (k2);
        filter2.setDrive (filtDrive2.v);
        filter2.setMix (filtMix2.v);
        filter2.update (cutoff2.v, res2.v, sr);
        if (routeFade > 0) { f2old.setDrive (filtDrive2.v); f2old.setMix (filtMix2.v); f2old.update (cutoff2.v, res2.v, sr); }
    }
    else { filter2Fresh = true; jumpF2 = true; }   // configure and settle at once when it comes back on

    // --- FM matrix
    for (int k = 0; k < 4; ++k)
    {
        const int src = s.i (P_fmSlot1Source + 3 * k), dst = s.i (P_fmSlot1Dest + 3 * k);
        FmSlot& f = fmSlots[k];
        f.src = clampv (src, 0, 5); f.dst = clampv (dst, 0, 5);
        f.on = f.src != f.dst && (f.src != 3 || wtLoaded) && (f.dst != 3 || wtLoaded);
        f.scale = f.dst == 3 ? 1.0f / 4400.0f : 1.0f;
        fmAmt[k].target = s.f (P_fmSlot1Amt + 3 * k);
        fmAmt[k].step (c10);
    }

    // --- ring-mod matrix
    for (int k = 0; k < 2; ++k)
    {
        RingSlot& r = ringSlots[k];
        r.src = clampv (s.i (P_ringSlot1Source + 3 * k), 0, 5);
        r.dst = clampv (s.i (P_ringSlot1Dest + 3 * k), 0, 6);
        r.on = (r.src != 3 || wtLoaded) && (r.dst != 3 || wtLoaded);
        const float amt = clampv (s.f (P_ringSlot1Amt + 3 * k), 0.0f, 1.0f);
        const bool master = r.dst == 6;
        ringDepth[k].target = 0.25f + amt * 3.75f;
        ringOut[k].target = rg * amt * (master ? 0.45f : 0.8f);
        ringDepth[k].step (c10); ringOut[k].step (c10);
        r.hp.set (Biquad::HP, master ? 120.0 : 40.0, 1.0, sr);
        r.lp.set (Biquad::LP, master ? 9500.0 : 12000.0, 1.0, sr);
        r.shapeK = 1.0f + (0.6f + amt * 1.8f) * 7.0f;
    }

    // --- Osc 5: complex oscillator
    const double cDet = clampv (s.f (P_complexDetune) + mod[MT_complexDetune], -50.0f, 50.0f);
    cBase.target = (float) oscF (P_complexOct, semi (P_complexSemi) + cDet + bendC + driftCents[4]);
    cRatio.target = clampv (s.f (P_complexRatio) + mod[MT_complexRatio], 0.125f, 8.0f);
    cFm.target = clampv (s.f (P_complexFm) + mod[MT_complexFm], 0.0f, 1500.0f);
    cShapeK = clampv (s.f (P_complexShape) + mod[MT_complexShape], 1.0f, 25.0f);
    cMixS.target = clampv (s.f (P_complexMix) + mod[MT_complexMix], 0.0f, 1.0f);
    cBase.step (cPort); cRatio.step (cPort); cFm.step (c10); cMixS.step (c10);

    // --- Osc 6: unison SuperSaw
    const double ssDet = clampv (s.f (P_supersawDetune) + mod[MT_supersawDetune], -50.0f, 50.0f);
    ssBase.target = (float) oscF (P_supersawOct, semi (P_supersawSemi) + ssDet + bendC);
    ssBase.step (cPort);
    ssVoices = clampv ((int) std::lround (s.f (P_supersawVoices) + mod[MT_supersawVoices]), 2, 9);
    const float spread = clampv (s.f (P_supersawSpread) + mod[MT_supersawSpread], 0.0f, 80.0f);
    const float width  = clampv (s.f (P_supersawStereo) + mod[MT_supersawStereo], 0.0f, 1.0f);
    const float drift  = clampv (s.f (P_supersawDrift) + mod[MT_supersawDrift], 0.0f, 20.0f);
    const float denom = (float) std::max (1, ssVoices - 1);
    for (int i = 0; i < 9; ++i)
    {
        const bool on = i < ssVoices;
        const float pos = on ? (i / denom) * 2.0f - 1.0f : 0.0f;
        ssCents[i].target = (float) (pos * spread + ssDriftSin[i] * drift);
        ssGain[i].target = on ? (1.0f / std::sqrt ((float) ssVoices)) * (0.9f - std::abs (pos) * 0.2f) : 0.0f;
        ssCents[i].step (c10); ssGain[i].step (c10);
    }
    // pans only change with the width or the voice count
    if (width != panWidth || ssVoices != panVoices)
    {
        panWidth = width; panVoices = ssVoices;
        for (int i = 0; i < 9; ++i)
        {
            const float pos = i < ssVoices ? (i / denom) * 2.0f - 1.0f : 0.0f;
            const float p = clampv (pos * width, -1.0f, 1.0f);
            const float x = (p + 1.0f) * 0.5f;
            ssPanL[i] = std::cos (x * (float) kPi * 0.5f);
            ssPanR[i] = std::sin (x * (float) kPi * 0.5f);
        }
    }

    updateUnison (s);
    updateMixer (s, first, c10);
    // stereo processing is only needed for the SuperSaw spread, a stereo sample, spread unison or a panned source
    bool stereoWt = uniAny || mxActive || (rsRun && (s.f (P_rsWidth) > 0.001f || s.f (P_rsRotate) > 0.001f))
                 || (coreRun[0] && coreSet[0].uni > 1 && coreSet[0].spread > 0.0005f) || (coreRun[1] && coreSet[1].uni > 1 && coreSet[1].spread > 0.0005f);
    for (auto& w : wt) if (w.loaded && w.wav->numChannels > 1 && w.level.target > 0) stereoWt = true;
    if (! stereo && ((lSS.target > 0 && width > 0.001f) || stereoWt))
    {
        stereo = true;
        filter.copyChannel (0, 1); filter2.copyChannel (0, 1); f2old.copyChannel (0, 1);
        for (auto& r : ringSlots) { r.hp.copyState (0, 1); r.lp.copyState (0, 1); }
    }
}

void Voice::render (float* L, float* R, int numSamples, const Snapshot& s, const WaveSample* const* wavs, ModState& modOut,
                    const ModContext& mc, float* liveOut)
{
    const WaveBank& bank = WaveBank::get();
    const double isr = 1.0 / sr;
    const float fisr = (float) isr;
    const bool routed = mc.any();
    const bool follow = routed && mc.routes->anyFollow;

    int done = 0;
    while (done < numSamples && active)
    {
        const int n = std::min (kCtrl, numSamples - done);

        // ---- modulation matrix: sources, then a per-voice copy of the parameters with the
        // routes applied. Everything below reads that copy, so every registered parameter is
        // a destination without the oscillators, filter or effects knowing about the matrix.
        updateLfoOut (s, done, mc);
        const Snapshot* sp = &s;
        const bool ciOn = s.v[P_ciAmount] > 1.0e-4f;
        if (ciOn || routed) inst.advance (lastDt, s.v[P_ciRate]);
        if (routed)
        {
            computeSources (s, mc.in, lastDt);
            srcV[MS_DnaSeq] = mc.dnaSeq; srcV[MS_DnaSeq2] = mc.dnaSeq2; srcV[MS_DnaGate] = mc.dnaGate;
            srcV[MS_BreakHit] = mc.rx != nullptr ? mc.rx->envelope()[done] : 0.0f;
            srcV[MS_BreakHitType] = mc.rx != nullptr ? mc.rx->hitType() : 0.0f;
            for (int r = 1; r < kRxRows; ++r) srcV[MS_KickHit + r - 1] = mc.rx != nullptr && mc.rx->rowLive (r) ? mc.rx->envelope (r)[done] : 0.0f;
            modSnap = s;
            if (mc.morph && mc.scenes != nullptr && mc.routes->anySceneXY)
            {
                // the scene XY position is modulated: move it, re-morph, then apply the other routes on top
                applyRoutes (*mc.routes, s.v, modSnap.v, srcV, rstate, lastDt, RF_All, liveOut, RP_SceneXY);
                morphScenes (*mc.scenes, modSnap.v[P_sceneX], modSnap.v[P_sceneY], modSnap.v);
                applyRoutes (*mc.routes, modSnap.v, modSnap.v, srcV, rstate, lastDt, RF_All, liveOut, RP_AfterScene);
            }
            else applyRoutes (*mc.routes, s.v, modSnap.v, srcV, rstate, lastDt, RF_All, liveOut);
            if (mc.mut != nullptr) applyMutation (*mc.mut, modSnap.v[P_mutAmount], mc.lockMask, modSnap.v);
            sp = &modSnap;
        }
        if (ciOn)
        {
            if (sp != &modSnap) { modSnap = s; sp = &modSnap; }
            applyInstability (modSnap, false);
        }
        const Snapshot& ps = *sp;
        lastDt = (float) (n * isr);

        ModState mod;
        computeMod (ps, mod);
        updateControl (ps, wavs, mod, false);
        for (int c = 0; c < 2; ++c) if (coreRun[c]) { core[c].configure (coreSet[c], coreHz[c], n); if (coreXf[c] > 0) coreOld[c].configure (coreSet[c], coreHz[c], n); }
        updatePitchEnv (ps);
        modOut = mod;

        // ---- audio-rate routes: an oscillator's raw output driving a destination per sample
        struct ARoute { int ad, src, curve; float k; bool uni; };
        ARoute ar[kNumRoutes]; int nar = 0;
        bool arCut = false, arCut2 = false, arFmSlot = false;
        if (routed && mc.routes->anyAudio)
        {
            for (int i = 0; i < mc.routes->n; ++i)
            {
                const ResolvedRoute& r = mc.routes->r[i];
                if (! r.audio) continue;
                float scale = 1.0f;
                const int ad = audioDestFor (r.dst, scale);
                if (ad < 0) continue;
                float k = ps.v[P_mod1Amt + r.slot] * scale;
                if (r.via != MS_None)
                {
                    float vv = srcV[r.via];
                    if (kModSrcBipolar[r.via]) vv = 0.5f * (vv + 1.0f);
                    k *= 1.0f - r.viaDepth + r.viaDepth * vv;
                }
                if (std::abs (k) < 1.0e-9f) continue;
                ar[nar++] = { ad, r.src >= MS_CoreAAudio ? 4 + (r.src - MS_CoreAAudio) : r.src - MS_Osc1Audio, r.curve, k, r.unipolar };
                arCut |= ad == AD_Cut || ad == AD_Res;
                arCut2 |= ad == AD_Cut2 || ad == AD_Res2;
                arFmSlot |= ad >= AD_Fm1 && ad <= AD_Fm4;
            }
        }
        float ad[AD_COUNT];

        // gather FM routes per destination
        float fmA[6][4]; int fmS[6][4]; int fmK[6][4]; int fmN[6] = { 0, 0, 0, 0, 0, 0 };
        for (int k = 0; k < 4; ++k)
        {
            const FmSlot& f = fmSlots[k];
            if (! f.on) continue;
            const float a = fmAmt[k].v * f.scale;
            if (std::abs (a) < 1.0e-9f && ! arFmSlot) continue;
            fmA[f.dst][fmN[f.dst]] = a; fmS[f.dst][fmN[f.dst]] = f.src; fmK[f.dst][fmN[f.dst]] = k; ++fmN[f.dst];
        }
        // which units are needed this block
        bool usedAsSource[6] = { false, false, false, false, false, false };
        for (int k = 0; k < 6; ++k) if (fmN[k] > 0) for (int j = 0; j < fmN[k]; ++j) usedAsSource[fmS[k][j]] = true;
        bool ringOn[2];
        for (int k = 0; k < 2; ++k)
        {
            ringOn[k] = ringSlots[k].on && ringOut[k].v > 1.0e-6f;
            if (ringOn[k]) { usedAsSource[ringSlots[k].src] = true; if (ringSlots[k].dst < 6) usedAsSource[ringSlots[k].dst] = true; }
        }
        // ---- DNA Splice: A and B are any two of the voice's own sources
        DnaParams dnp;
        dnp.mix = ps.f (P_dnaMix); dnp.mode = clampv (ps.i (P_dnaMode), 0, DNA_COUNT - 1);
        dnp.srcA = clampv (ps.i (P_dnaA), 0, 7); dnp.srcB = clampv (ps.i (P_dnaB), 0, 7);
        dnp.amount = ps.f (P_dnaAmount); dnp.chr = ps.f (P_dnaChar);
        const bool dnaOn = dnp.active();
        if (dnaOn)
        {
            if (! dnaStarted) { dnaCur = dnp; dnaStarted = true; dnaFade = 0; }
            else if (dnp.mode != dnaCur.mode || dnp.srcA != dnaCur.srcA || dnp.srcB != dnaCur.srcB)
            {
                // a new mode or source fades in over 10 ms instead of switching with a click
                dnaPrev = dnaCur; dnaFade = std::max (1, (int) (0.01 * sr));
            }
            dnaCur = dnp;
            dnaPrev.amount = dnp.amount; dnaPrev.chr = dnp.chr; dnaPrev.mix = dnp.mix;
            const double noteHz = baseFreq * pow2 (ps.bendSemis / 12.0);
            dna.configure (dnaCur.mode, dnaCur, noteHz);
            if (dnaFade > 0 && dnaPrev.mode != dnaCur.mode) dna.configure (dnaPrev.mode, dnaPrev, noteHz);
        }
        else dnaStarted = false;
        const int dnaFadeLen = std::max (1, (int) (0.01 * sr));
        auto dnaUses = [&] (int k) { return dnaOn && (dnaCur.srcA == k || dnaCur.srcB == k || (dnaFade > 0 && (dnaPrev.srcA == k || dnaPrev.srcB == k))); };

        const bool needComplex = lC.v > 1.0e-6f || lC.target > 0 || usedAsSource[4] || dnaUses (6);
        const bool needSS = lSS.v > 1.0e-6f || lSS.target > 0 || usedAsSource[5] || dnaUses (7);
        const bool legacyRing = ringMix.v > 1.0e-6f || ringMix.target > 0;

        double ssFreq[9];
        if (needSS) for (int i = 0; i < 9; ++i) ssFreq[i] = ssBase.v * pow2 (ssCents[i].v / 1200.0);
        const int ssCount = needSS ? 9 : 0;

        // follower coefficients (per sample)
        const float fA = 1.0f - std::exp (-1.0f / (0.005f * (float) sr)), fR = 1.0f - std::exp (-1.0f / (0.12f * (float) sr));
        const float tfA = 1.0f - std::exp (-1.0f / (0.001f * (float) sr)), tfR = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        const float tsA = 1.0f - std::exp (-1.0f / (0.03f * (float) sr)), tsR = 1.0f - std::exp (-1.0f / (0.25f * (float) sr));

        // ---- mutation modules for this block
        WaveMutatorParams wmp;
        wmp.mix = ps.f (P_wmMix); wmp.drive = ps.f (P_wmDrive); wmp.fold = ps.f (P_wmFold); wmp.shape = ps.f (P_wmShape);
        wmp.bend = ps.f (P_wmBend); wmp.asym = ps.f (P_wmAsym); wmp.rect = ps.f (P_wmRect); wmp.bits = ps.f (P_wmBits); wmp.down = ps.f (P_wmDown);
        wmp.oversample = clampv (ps.i (P_quality), 0, 2);
        AudioRateParams arp;
        arp.mod = clampv (ps.i (P_arMod), 0, 5); arp.ratio = ps.f (P_arRatio); arp.offsetHz = ps.f (P_arOffset);
        arp.fm = ps.f (P_arFm); arp.fmTarget = clampv (ps.i (P_arFmTarget), 0, 4); arp.am = ps.f (P_arAm); arp.ring = ps.f (P_arRing);
        arp.shiftHz = ps.f (P_arShift); arp.shiftMix = ps.f (P_arShiftMix);
        const bool arFmOn = arp.fmActive(), arFxOn = arp.fxActive(), needMod = arFmOn || arFxOn;
        const double modHz = baseFreq * pow2 (ps.bendSemis / 12.0) * arp.ratio + arp.offsetHz;
        const float fmIndex = arp.fm * 8.0f;
        bool fmT[AD_COUNT] = {};
        if (arFmOn)
        {
            const int sets[5][8] = { { AD_P1, AD_P2, AD_P3, AD_PSub, -1 }, { AD_P1, -1 }, { AD_P2, -1 }, { AD_P3, -1 },
                                     { AD_P1, AD_P2, AD_P3, AD_PSub, AD_PWt1, AD_PWt2, AD_PCx, AD_PSs } };
            for (int k = 0; k < 8 && sets[arp.fmTarget][k] >= 0; ++k) fmT[sets[arp.fmTarget][k]] = true;
        }
        const double rotInc = 2.0 * kPi * arp.shiftHz / sr;
        const bool shifting = arFxOn && arp.shiftMix > 0.0f;
        const double rotCos = shifting ? std::cos (rotInc) : 1.0, rotSin = shifting ? std::sin (rotInc) : 0.0;
        float xb[2][kCtrl], mb[kCtrl], cutB[kCtrl], resB[kCtrl], cutB2[kCtrl], resB2[kCtrl];
        // Sub Route: the sub split off before the filter (dry), ramped between the routes
        const int subRoute = clampv (ps.i (P_subRoute), 0, 2);
        const bool subSplit = subOut[0][0] != nullptr && (subRoute != 0 || subRG[1] > 0.0f || subRG[2] > 0.0f);
        const float subRc = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
        float sdb[2][2][kCtrl];
        // ---- Break-Reactive mode: which oscillators react, and how much (this voice's own amounts)
        const BreakReact* rx = mc.rx;
        bool rxT[5] { false, false, false, false, false };
        float rxPitchAmt = 0.0f, rxDriveAmt = 0.0f, rxDuckAmt = 0.0f, rxNotchAmt = 0.0f;
        bool rxResetOn = false;
        const int rxXfLen = std::max (1, (int) std::lround (0.0015 * sr));   // (a phase reset fades the old phase out over 1.5 ms)
        const float rxDuckC = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
        if (rx != nullptr)
        {
            rxT[0] = ps.v[P_rxOsc1] > 0.5f; rxT[1] = ps.v[P_rxOsc2] > 0.5f; rxT[2] = ps.v[P_rxOsc3] > 0.5f; rxT[3] = ps.v[P_rxSub] > 0.5f; rxT[4] = ps.v[P_rxReese] > 0.5f;
            rxResetOn = ps.v[P_rxReset] > 0.5f;
            rxPitchAmt = clampv (ps.f (P_rxPitch), -24.0f, 24.0f); rxDriveAmt = clampv (ps.f (P_rxDrive), 0.0f, 1.0f);
            rxDuckAmt = clampv (ps.f (P_rxDuck), 0.0f, 1.0f); rxNotchAmt = clampv (ps.f (P_rxNotch), 0.0f, 1.0f);
        }
        // ... the Kick / Snare / Hat rows live this call, the Main row's new actions, and the other engines
        struct RxRowV { const float* env; const uint8_t* hit; float pitch, drive, duck, notch, cut, res; bool reset; int retrig; };
        RxRowV rxRow[4]; int nRxRow = 0;
        float rxCut0 = 0.0f, rxRes0 = 0.0f; int rxRt0 = 0;
        bool rxFilt = false, rxRtA = false, rxRtF = false, rxEngWt[2] { false, false }, rxEngCore[2] { false, false }, rxEngSaw = false, rxEngCx = false, rxEngGran = false;
        float rxJump = 0.0f, rxBurst = 0.0f;
        uint8_t rxRtHitA[kCtrl], rxRtHitF[kCtrl];
        if (rx != nullptr)
        {
            rxCut0 = clampv (ps.f (P_rxCut), -4.0f, 4.0f); rxRes0 = clampv (ps.f (P_rxRes), 0.0f, 1.0f); rxRt0 = clampv (ps.i (P_rxRetrig), 0, 3);
            for (int r = 1; r < kRxRows; ++r)
            {
                if (! rx->rowLive (r)) continue;
                RxRowV& w = rxRow[nRxRow++];
                w.env = rx->envelope (r); w.hit = rx->hits (r);
                w.pitch = clampv (ps.f (rxRowParam (r, RXF_Pitch)), -24.0f, 24.0f); w.drive = clampv (ps.f (rxRowParam (r, RXF_Drive)), 0.0f, 1.0f);
                w.duck = clampv (ps.f (rxRowParam (r, RXF_Duck)), 0.0f, 1.0f); w.notch = clampv (ps.f (rxRowParam (r, RXF_Notch)), 0.0f, 1.0f);
                w.cut = clampv (ps.f (rxRowParam (r, RXF_Cut)), -4.0f, 4.0f); w.res = clampv (ps.f (rxRowParam (r, RXF_Res)), 0.0f, 1.0f);
                w.reset = ps.v[rxRowParam (r, RXF_Reset)] > 0.5f; w.retrig = clampv (ps.i (rxRowParam (r, RXF_Retrig)), 0, 3);
                rxFilt |= w.cut != 0.0f || w.res != 0.0f;
            }
            rxFilt |= rxCut0 != 0.0f || rxRes0 != 0.0f;
            rxEngWt[0] = ps.v[P_rxWt1] > 0.5f; rxEngWt[1] = ps.v[P_rxWt2] > 0.5f; rxEngCore[0] = ps.v[P_rxCoreA] > 0.5f; rxEngCore[1] = ps.v[P_rxCoreB] > 0.5f;
            rxEngSaw = ps.v[P_rxSaw] > 0.5f; rxEngCx = ps.v[P_rxComplex] > 0.5f; rxEngGran = ps.v[P_rxGran] > 0.5f && gsRun;
            rxJump = clampv (ps.f (P_rxWtJump), 0.0f, 1.0f); rxBurst = clampv (ps.f (P_rxGranBurst), 0.0f, 1.0f);
            // Env Retrigger: which samples start the amp / filter envelope again (held notes)
            auto rtFor = [] (int mode, bool amp) { return amp ? (mode == 1 || mode == 3) : (mode == 2 || mode == 3); };
            for (int pass = 0; pass < 2; ++pass)
            {
                const bool amp = pass == 0;
                uint8_t* out = amp ? rxRtHitA : rxRtHitF;
                bool any = false;
                std::fill (out, out + n, (uint8_t) 0);
                if (rtFor (rxRt0, amp)) { const uint8_t* h = rx->hits(); for (int i = 0; i < n; ++i) if (h[done + i]) { out[i] = 1; any = true; } }
                for (int k = 0; k < nRxRow; ++k)
                    if (rtFor (rxRow[k].retrig, amp)) for (int i = 0; i < n; ++i) if (rxRow[k].hit[done + i]) { out[i] = 1; any = true; }
                (amp ? rxRtA : rxRtF) = any;
            }
        }
        float rxCutO[kCtrl], rxResA[kCtrl];
        const float gsDensBase = gsP.density;
        // a reacting engine's restart: the step it makes faded out over 1.5 ms (n -1: it restarted on this sample)
        auto rxDeclick = [rxXfLen] (RxDeclick& d, float& l, float& r, float* prev)
        {
            if (d.n == -1) { d.l = prev[0] - l; d.r = prev[1] - r; d.n = rxXfLen; }
            if (d.n > 0) { const float w = (float) d.n / (float) rxXfLen; l += d.l * w; r += d.r * w; --d.n; }
            prev[0] = l; prev[1] = r;
        };
        auto kickSat = [] (float x, float d)
        {
            // the drive kick: pushed into a soft curve, faded in from nothing
            const float z = x * (1.0f + 6.0f * d), y = z / std::sqrt (1.0f + z * z);
            return x + (y - x) * std::min (1.0f, 4.0f * d);
        };
        if (subSplit) subDryUsed = true;

        // ---- Resonator (after the filter and amp envelope; it rings on after the note)
        ResonatorParams rsp;
        rsp.mix = ps.f (P_resMix); rsp.tuning = clampv (ps.i (P_resTuning), 0, RT_COUNT - 1); rsp.modes = clampv (ps.i (P_resModes), 1, ps.i (P_quality) == 0 ? 6 : 16);   // Quality Eco: at most 6 modes
        rsp.pitch = ps.f (P_resPitch); rsp.decay = ps.f (P_resDecay); rsp.damping = ps.f (P_resDamping);
        rsp.inharm = ps.f (P_resInharm); rsp.spread = ps.f (P_resSpread); rsp.feedback = ps.f (P_resFeedback);
        const bool resOn = rsp.active();
        if (resOn) reso.configure (rsp, baseFreq * pow2 (ps.bendSemis / 12.0));
        else if (resoRinging) { reso.reset(); resoRinging = false; }
        float resPeak = 0.0f;
        // with feedback the bank can sustain itself: after the note ends it gets 3x its decay, then fades out
        const double resLimit = ampEnv.releaseTime >= 0.0 ? ampEnv.releaseTime + ampEnv.r + 3.0 * rsp.decay + 0.5 : 1.0e9;

        // Core slots set to "Operators only" skip the amp envelope: the other sources take it here, before
        // the filter, and the Core's own operator envelopes shape it (the amp stage below is then left out)
        float coreBL[kCtrl], coreBR[kCtrl], ampPre[kCtrl];
        if (opsOnlyAny)
            for (int i = 0; i < n; ++i)
            {
                if (rxRtA && rxRtHitA[i]) ampEnv.retrigger (t + i * isr);
                coreBL[i] = coreBR[i] = 0.0f; ampPre[i] = (float) ampEnv.eval (t + i * isr);
            }
        auto sampleLoop = [&] (auto arTag)
        {
        using ARType = decltype (arTag);   // a type, not a constexpr local, so nested lambdas see it on every compiler
        auto fmIn = [&] (int d)
        {
            float a = 0.0f;
            if constexpr (! ARType::value)
                for (int k = 0; k < fmN[d]; ++k) a += fmA[d][k] * srcVals[fmS[d][k]];
            else
                for (int k = 0; k < fmN[d]; ++k)
                    a += (fmA[d][k] + ad[AD_Fm1 + fmK[d][k]] * fmSlots[fmK[d][k]].scale) * srcVals[fmS[d][k]];
            return a;
        };
        auto lvl = [&] (float v, int a)
        {
            if constexpr (! ARType::value) { juce::ignoreUnused (a); return v; }
            else return v > 0.0f ? clampv (v + ad[a], 0.0f, 2.0f) : 0.0f;
        };
        for (int i = 0; i < n; ++i)
        {
            // the audio-rate transform's modulator (from the previous sample of the oscillators)
            float m = 0.0f;
            if (needMod)
            {
                switch (arp.mod)
                {
                    case 0:  m = (float) std::sin (2.0 * kPi * arPh); arPh = wrap01 (arPh + modHz * isr); break;
                    case 5:  m = nextRand(); break;
                    default: m = aPrev[arp.mod - 1]; break;
                }
            }
            mb[i] = m;
            // Break-Reactive mode: a hit here resets the ticked oscillators' phases; its envelope kicks the
            // pitch, the drive and the level, and moves the Reese's notch
            float rxPm = 1.0f, rxG = 1.0f, rxD = 0.0f;
            bool rxResetNow = false;
            float rxEAny = 0.0f;
            if (rx != nullptr)
            {
                const int gi = done + i;
                bool resetHit = rxResetOn && rx->hits()[gi] != 0, anyHit = rx->hits()[gi] != 0;
                for (int k = 0; k < nRxRow; ++k) { if (rxRow[k].reset && rxRow[k].hit[gi] != 0) resetHit = true; anyHit |= rxRow[k].hit[gi] != 0; }
                if (anyHit && rxEngGran) gran.burstNow();
                if (resetHit)
                {
                    double* phs[4] = { &ph1, &ph2, &ph3, &phSub };
                    for (int k = 0; k < 4; ++k) if (rxT[k]) { rxOldPh[k] = *phs[k]; *phs[k] = 0.0; rxXf[k] = rxXfLen; }
                    if (rxT[4] && rsRun) reese.resetPhases (rxXfLen);
                    rxResetNow = true;
                }
                const float e = rx->envelope()[gi];
                if (e > 0.0f)
                {
                    if (rxPitchAmt != 0.0f) rxPm = (float) pow2 ((double) (rxPitchAmt * e) / 12.0);
                    rxG = 1.0f - rxDuckAmt * e; rxD = rxDriveAmt * e;
                }
                rxEAny = e;
                float notchT = 0.0f, cutO = rxCut0 * e, resA = rxRes0 * e;
                for (int k = 0; k < nRxRow; ++k)
                {
                    const RxRowV& w = rxRow[k];
                    const float er = w.env[gi];
                    if (er <= 0.0f) continue;
                    if (w.pitch != 0.0f) rxPm *= (float) pow2 ((double) (w.pitch * er) / 12.0);
                    rxG *= 1.0f - w.duck * er; rxD = std::min (1.0f, rxD + w.drive * er);
                    notchT += w.notch * er; cutO += w.cut * er; resA += w.res * er;
                    rxEAny = std::max (rxEAny, er);
                }
                if (rxFilt) { rxCutO[i] = cutO; rxResA[i] = resA; }
                if (rsRun) { reese.setPitchMul (rxT[4] ? rxPm : 1.0); reese.setNotchJump (rxT[4] ? (nRxRow > 0 ? 2.0f * (rxNotchAmt * e + notchT) : 2.0f * rxNotchAmt * e) : 0.0f); }
                if (rxResetNow)
                {
                    // the other engines that react: back to the start (the jump each makes is faded out below)
                    for (int w = 0; w < 2; ++w)
                        if (rxEngWt[w] && wt[w].loaded)
                        {
                            const double span = std::max (1.0, wt[w].le - wt[w].ls);
                            double p0 = wt[w].start + rxJump * span;
                            if (wt[w].loop && p0 >= wt[w].le) p0 = wt[w].ls + std::fmod (p0 - wt[w].ls, span);
                            wt[w].pos = p0; wt[w].stopped = false; rxDcWt[w].n = -1;
                        }
                    for (int c = 0; c < 2; ++c) if (rxEngCore[c] && coreRun[c]) { coreOld[c] = core[c]; core[c].rxRestart(); coreXf[c] = 2 * rxXfLen; }
                    if (rxEngSaw && ssCount > 0) { std::copy (std::begin (ssPh0), std::end (ssPh0), std::begin (ssPh)); rxDcSaw.n = -1; }
                    if (rxEngCx && needComplex) { phCar = phMod = 0.0; rxDcCx.n = -1; }
                }
                if (rxEngGran && rxEAny > 0.0f)
                {
                    gsP.density = std::min (100.0f, gsDensBase * (1.0f + 7.0f * rxBurst * rxEAny));
                    gran.limitWait (sr / std::max (0.5, (double) gsP.density));
                }
                rxDuckSm += (rxG - rxDuckSm) * rxDuckC;   // (the duck smoothed over about 1 ms, so it doesn't click)
                if (std::abs (rxDuckSm - rxG) < 1.0e-6f) rxDuckSm = rxG;
                rxG = rxDuckSm;
            }
            // (a reset oscillator's old phase, fading out)
            auto rxFade = [&] (int k, float now, int wave, double f)
            {
                const float old = bank.sample (wave, rxOldPh[k], (float) std::abs (f) * fisr);
                rxOldPh[k] = wrap01 (rxOldPh[k] + f * isr);
                const float w = (float) rxXf[k] / (float) rxXfLen; --rxXf[k];
                return now + (old - now) * w;
            };
            if constexpr (ARType::value)
            {
                std::fill (ad, ad + AD_COUNT, 0.0f);
                for (int k = 0; k < nar; ++k)
                {
                    float x = aPrev[ar[k].src];
                    if (ar[k].uni) x = 0.5f * (x + 1.0f);
                    ad[ar[k].ad] += ar[k].k * applyCurve (ar[k].curve, x);
                }
                if (arCut)
                {
                    cutB[i] = clampv (cutoff.v * (float) pow2z (ad[AD_Cut]), 20.0f, 20000.0f);
                    resB[i] = clampv (res.v + ad[AD_Res], 0.1f, 30.0f);
                }
                if (arCut2)
                {
                    cutB2[i] = clampv (cutoff2.v * (float) pow2z (ad[AD_Cut2]), 20.0f, 20000.0f);
                    resB2[i] = clampv (res2.v + ad[AD_Res2], 0.1f, 30.0f);
                }
            }
            const double fmMul = 1.0 + (double) fmIndex * m;   // through-zero linear FM
            auto pm = [&] (int a) { return ! ARType::value ? 1.0 : pow2z (ad[a]) * (fmT[a] ? fmMul : 1.0); };

            // Osc 2 and 3 first so Osc 1's legacy FM uses this sample's Osc 2 (as in Web Audio)
            double fr = ! ARType::value ? (double) (f2.v * peRatio[1] + fmIn (1)) : f2.v * peRatio[1] * pm (AD_P2) + fmIn (1);
            if (rx != nullptr && rxT[1]) fr *= rxPm;
            float o2 = bank.sample (wave2, ph2, (float) std::abs (fr) * fisr);
            if (rxXf[1] > 0) o2 = rxFade (1, o2, wave2, fr);
            ph2 = wrap01 (ph2 + fr * isr); srcVals[1] = o2;
            float uL[kUniOscs] {}, uR[kUniOscs] {};
            auto uniOsc = [&] (int k, int wave, double f0)
            {
                for (int j = 0; j < uniN[k] - 1; ++j)
                {
                    const double f = f0 * uniRatio[k][j];
                    const float x = bank.sample (wave, uniPh[k][j], (float) std::abs (f) * fisr);
                    uniPh[k][j] = wrap01 (uniPh[k][j] + f * isr);
                    uL[k] += x * uniPL[k][j]; uR[k] += x * uniPR[k][j];
                }
            };
            if (uniN[1] > 1) uniOsc (1, wave2, fr);

            fr = ! ARType::value ? (double) (f3.v * peRatio[2] + fmIn (2)) : f3.v * peRatio[2] * pm (AD_P3) + fmIn (2);
            if (rx != nullptr && rxT[2]) fr *= rxPm;
            float o3 = bank.sample (wave3, ph3, (float) std::abs (fr) * fisr);
            if (rxXf[2] > 0) o3 = rxFade (2, o3, wave3, fr);
            ph3 = wrap01 (ph3 + fr * isr); srcVals[2] = o3;
            if (uniN[2] > 1) uniOsc (2, wave3, fr);

            double fsub = ! ARType::value ? (double) (fSub.v * peRatio[3]) : fSub.v * peRatio[3] * pm (AD_PSub);
            if (rx != nullptr && rxT[3]) fsub *= rxPm;
            float osub = bank.sample (waveSub, phSub, (float) fsub * fisr);
            if (rxXf[3] > 0) osub = rxFade (3, osub, waveSub, fsub);
            phSub = wrap01 (phSub + fsub * isr);

            // Osc 4 (WT 1) and WT 2: sample playback
            float o4L, o4R, w2L, w2R;
            if (! ARType::value)
            {
                wt[0].tick (fmIn (3) + wt[0].rate.v * (peRatio[4] - 1.0f), isr, o4L, o4R);
                wt[1].tick (wt[1].rate.v * (peRatio[5] - 1.0f), isr, w2L, w2R);
            }
            else
            {
                wt[0].tick (fmIn (3) + wt[0].rate.v * (peRatio[4] * pm (AD_PWt1) - 1.0), isr, o4L, o4R);
                wt[1].tick (wt[1].rate.v * (peRatio[5] * pm (AD_PWt2) - 1.0), isr, w2L, w2R);
            }
            if (rx != nullptr)
            {
                if (rxEngWt[0]) rxDeclick (rxDcWt[0], o4L, o4R, rxPrevWt[0]);
                if (rxEngWt[1]) rxDeclick (rxDcWt[1], w2L, w2R, rxPrevWt[1]);
            }
            const float o4m = 0.5f * (o4L + o4R);
            srcVals[3] = o4m;
            for (int w = 0; w < 2; ++w)
            {
                const int k = 3 + w;
                if (uniN[k] <= 1) continue;
                const double fmw = w == 0 ? (double) fmIn (3) : 0.0;
                for (int j = 0; j < uniN[k] - 1; ++j)
                {
                    float a, b;
                    wtU[w][j].tick (fmw * uniRatio[k][j], isr, a, b);
                    uL[k] += a * uniPL[k][j]; uR[k] += b * uniPR[k][j];
                }
            }

            // Osc 5: complex oscillator (B frequency-modulates A, blended, then wavefolded)
            float cOut = 0, cRaw = 0;
            if (needComplex)
            {
                const double cb = ! ARType::value ? (double) (cBase.v * peRatio[6]) : cBase.v * peRatio[6] * pm (AD_PCx);
                const double mf = cb * cRatio.v;
                const float om = bank.sample (waveB, phMod, (float) std::abs (mf) * fisr);
                phMod = wrap01 (phMod + mf * isr);
                const double cf = ! ARType::value ? (double) (cBase.v + cFm.v * om + fmIn (4))
                                            : cb + std::max (0.0f, cFm.v + ad[AD_CxFm]) * om + fmIn (4);
                const float oc = bank.sample (waveA, phCar, (float) std::abs (cf) * fisr);
                phCar = wrap01 (phCar + cf * isr);
                const float shapeK = ! ARType::value ? cShapeK : clampv (cShapeK + ad[AD_CxShape], 1.0f, 25.0f);
                cRaw = driveShape (oc * cMixS.v + om * (1.0f - cMixS.v), shapeK);
                if (rx != nullptr && rxEngCx) { float dummy = cRaw, prev2[2] = { rxPrevCx, rxPrevCx }; rxDeclick (rxDcCx, cRaw, dummy, prev2); rxPrevCx = prev2[0]; }
                cOut = cRaw * lvl (lC.v, AD_LCx);
                if (uniN[5] > 1)
                {
                    const float fmc = fmIn (4);
                    const float cFmNow = ! ARType::value ? cFm.v : std::max (0.0f, cFm.v + ad[AD_CxFm]);
                    for (int j = 0; j < uniN[5] - 1; ++j)
                    {
                        const double cbj = cb * uniRatio[5][j], mfj = cbj * cRatio.v;
                        const float omj = bank.sample (waveB, uniPhMod[j], (float) std::abs (mfj) * fisr);
                        uniPhMod[j] = wrap01 (uniPhMod[j] + mfj * isr);
                        const double cfj = cbj + cFmNow * omj + fmc;
                        const float ocj = bank.sample (waveA, uniPhCar[j], (float) std::abs (cfj) * fisr);
                        uniPhCar[j] = wrap01 (uniPhCar[j] + cfj * isr);
                        const float x = driveShape (ocj * cMixS.v + omj * (1.0f - cMixS.v), shapeK);
                        uL[5] += x * uniPL[5][j]; uR[5] += x * uniPR[5][j];
                    }
                    const float gc = lvl (lC.v, AD_LCx);
                    uL[5] *= gc; uR[5] *= gc;
                }
            }
            srcVals[4] = cOut;

            // Osc 6: SuperSaw
            float ssMono = 0, ssL = 0, ssR = 0, ssRaw = 0;
            if (ssCount > 0)
            {
                const float fmS6 = fmIn (5);
                const double ssm = pm (AD_PSs) * peRatio[7];
                for (int u = 0; u < 9; ++u)
                {
                    const float g = ssGain[u].v;
                    if (g <= 1.0e-7f) continue;
                    const double f = (! ARType::value ? ssFreq[u] * peRatio[7] : ssFreq[u] * ssm) + fmS6;
                    const float sw = bank.sample (W_SAW, ssPh[u], (float) std::abs (f) * fisr) * g;
                    ssPh[u] = wrap01 (ssPh[u] + f * isr);
                    ssMono += sw; ssL += sw * ssPanL[u]; ssR += sw * ssPanR[u];
                }
                if (rx != nullptr && rxEngSaw)
                {
                    // (the mono sum follows the left / right offsets' mean)
                    const float l0 = ssL, r0 = ssR;
                    rxDeclick (rxDcSaw, ssL, ssR, rxPrevSaw);
                    ssMono += 0.5f * ((ssL - l0) + (ssR - r0));
                }
                const float gss = lvl (lSS.v, AD_LSs);
                ssRaw = ssMono;
                ssMono *= gss; ssL *= gss; ssR *= gss;
            }
            srcVals[5] = ssMono;

            // Osc 1 (with the legacy Osc2 -> Osc1 FM)
            fr = ! ARType::value ? (double) (f1.v * peRatio[0] + legacyFm.v * o2 + fmIn (0))
                          : f1.v * peRatio[0] * pm (AD_P1) + std::max (0.0f, legacyFm.v + ad[AD_LegFm]) * o2 + fmIn (0);
            if (rx != nullptr && rxT[0]) fr *= rxPm;
            float o1 = bank.sample (wave1, ph1, (float) std::abs (fr) * fisr);
            if (rxXf[0] > 0) o1 = rxFade (0, o1, wave1, fr);
            ph1 = wrap01 (ph1 + fr * isr); srcVals[0] = o1;
            if (uniN[0] > 1) uniOsc (0, wave1, fr);
            aPrev[0] = o1; aPrev[1] = o2; aPrev[2] = o3; aPrev[3] = osub;

            // the Granular Synth's cloud
            float gsL = 0.0f, gsR = 0.0f, gsRaw = 0.0f;
            if (gsRun)
            {
                gran.tick (*gsWav, gsP, gsL, gsR);
                gsRaw = 0.5f * (gsL + gsR);
                gsL *= lGs.v; gsR *= lGs.v;
            }

            // mixer
            float g1 = lvl (l1.v, AD_L1), g2 = lvl (l2.v, AD_L2), g3 = lvl (l3.v, AD_L3);
            float gs = lvl (lSub.v, AD_LSub);
            if (rx != nullptr && rxG != 1.0f) { if (rxT[0]) g1 *= rxG; if (rxT[1]) g2 *= rxG; if (rxT[2]) g3 *= rxG; if (rxT[3]) gs *= rxG; }
            float subD = 0.0f;
            if (subSplit)
            {
                for (int k = 0; k < 3; ++k)
                {
                    const float tg = k == subRoute ? 1.0f : 0.0f;
                    subRG[k] += (tg - subRG[k]) * subRc;
                    if (std::abs (tg - subRG[k]) < 1.0e-5f) subRG[k] = tg;
                }
                subD = osub * gs;     // the dry sub
                gs *= subRG[0];       // what stays in the mix
            }
            const float o4g = lvl (wt[0].level.v, AD_LWt1) * wt[0].comp;
            const float w2g = lvl (wt[1].level.v, AD_LWt2) * wt[1].comp;
            const float mono = o1 * g1 * uniGc[0] + o2 * g2 * uniGc[1] + o3 * g3 * uniGc[2] + osub * gs + cOut * uniGc[5];
            float mL = mono + o4L * o4g * uniGc[3] + w2L * w2g * uniGc[4] + ssL;
            float mR = mono + o4R * o4g * uniGc[3] + w2R * w2g * uniGc[4] + ssR;
            if (uniN[0] > 1 || uniN[1] > 1 || uniN[2] > 1 || uniN[3] > 1 || uniN[4] > 1 || uniN[5] > 1)
            {
                const float gk[kUniOscs] = { g1, g2, g3, o4g, w2g, 1.0f };
                for (int k = 0; k < kUniOscs; ++k)
                    if (uniN[k] > 1) { mL += uL[k] * gk[k] * uniGs[k]; mR += uR[k] * gk[k] * uniGs[k]; }
            }
            if (mxActive || mc.meters)
            {
                // the Mixer strips: each source panned and muted / soloed (and measured for the meters)
                const float t0 = o1 * g1 * uniGc[0], t1 = o2 * g2 * uniGc[1], t2 = o3 * g3 * uniGc[2], t3 = osub * gs, t6 = cOut * uniGc[5];
                float sL[kMxSources] = { t0, t1, t2, t3, o4L * o4g * uniGc[3], w2L * w2g * uniGc[4], t6, ssL, 0.0f };
                float sR[kMxSources] = { t0, t1, t2, t3, o4R * o4g * uniGc[3], w2R * w2g * uniGc[4], t6, ssR, 0.0f };
                if (uniN[0] > 1 || uniN[1] > 1 || uniN[2] > 1 || uniN[3] > 1 || uniN[4] > 1 || uniN[5] > 1)
                {
                    const float gk[kUniOscs] = { g1, g2, g3, o4g, w2g, 1.0f };
                    static constexpr int strip[kUniOscs] = { 0, 1, 2, 4, 5, 6 };
                    for (int k = 0; k < kUniOscs; ++k)
                        if (uniN[k] > 1) { sL[strip[k]] += uL[k] * gk[k] * uniGs[k]; sR[strip[k]] += uR[k] * gk[k] * uniGs[k]; }
                }
                if (mc.meters)
                    for (int k = 0; k < kMxSources - 1; ++k) mxPeak[k] = std::max (mxPeak[k], std::max (std::abs (sL[k]), std::abs (sR[k])));
                if (mxActive)
                {
                    float a = 0.0f, b = 0.0f;
                    for (int k = 0; k < kMxSources - 1; ++k) { a += sL[k] * mxL[k]; b += sR[k] * mxR[k]; }
                    mL = a; mR = b;
                }
            }
            if (rx != nullptr && rxD > 0.0f && (rxT[0] || rxT[1] || rxT[2] || rxT[3]))
            {
                // the drive kick on the ticked oscillators (through their strips, as they're heard)
                float tl = 0.0f, tr = 0.0f;
                auto add = [&] (int strip, float l, float r) { if (mxActive) { l *= mxL[strip]; r *= mxR[strip]; } tl += l; tr += r; };
                const float* oo[3] = { &o1, &o2, &o3 };
                const float gg[3] = { g1, g2, g3 };
                for (int k = 0; k < 3; ++k)
                    if (rxT[k])
                    {
                        float l = *oo[k] * gg[k] * uniGc[k], r = l;
                        if (uniN[k] > 1) { l += uL[k] * gg[k] * uniGs[k]; r += uR[k] * gg[k] * uniGs[k]; }
                        add (k, l, r);
                    }
                if (rxT[3]) add (3, osub * gs, osub * gs);
                mL += kickSat (tl, rxD) - tl; mR += kickSat (tr, rxD) - tr;
            }
            if (rsRun)
            {
                // the Reese engine, through its own Mixer strip, into the mix before the filters
                float a, b; reese.tick (a, b);
                a *= lRs.v; b *= lRs.v;
                if (rx != nullptr && rxT[4]) { a *= rxG; b *= rxG; if (rxD > 0.0f) { a = kickSat (a, rxD); b = kickSat (b, rxD); } }
                if (mc.meters) mxPeak[kMxReeseStrip] = std::max (mxPeak[kMxReeseStrip], std::max (std::abs (a), std::abs (b)));
                if (mxActive) { a *= mxL[kMxReeseStrip]; b *= mxR[kMxReeseStrip]; }
                mL += a; mR += b;
            }
            for (int c = 0; c < 2; ++c)
            {
                if (! coreRun[c]) continue;
                // a Core slot, through its own Mixer strip, into the mix before the filters
                float a, b; core[c].tick (a, b);
                if (coreXf[c] > 0)
                {
                    float oa, ob; coreOld[c].tick (oa, ob);
                    const float w = (float) coreXf[c] / (float) (2 * rxXfLen);
                    a += (oa - a) * w; b += (ob - b) * w;
                    --coreXf[c];
                }
                a *= lCore[c].v; b *= lCore[c].v;
                aPrev[4 + c] = 0.5f * (a + b);
                if (mc.meters) mxPeak[kMxCoreStrip + c] = std::max (mxPeak[kMxCoreStrip + c], std::max (std::abs (a), std::abs (b)));
                if (mxActive) { a *= mxL[kMxCoreStrip + c]; b *= mxR[kMxCoreStrip + c]; }
                if (coreOpsOnly[c]) { coreBL[i] += a; coreBR[i] += b; }
                else { mL += a; mR += b; }
            }
            if (subSplit)
            {
                // the dry sub: its Mixer strip's pan and mute / solo, then out to the dry buses
                const float pl = mxActive ? mxL[3] : 1.0f, pr = mxActive ? mxR[3] : 1.0f;
                const float a1 = subD * subRG[1], a2 = subD * subRG[2];
                sdb[0][0][i] = a1 * pl; sdb[0][1][i] = a1 * pr;
                sdb[1][0][i] = a2 * pl; sdb[1][1][i] = a2 * pr;
                if (mc.meters) mxPeak[3] = std::max (mxPeak[3], std::abs (a1 + a2));
            }
            if (gsRun)
            {
                // its own Mixer strip (pan, mute / solo, meter), then into the mix before the filters
                if (mc.meters) mxPeak[kMxSources] = std::max (mxPeak[kMxSources], std::max (std::abs (gsL), std::abs (gsR)));
                if (mxActive) { gsL *= mxL[kMxSources]; gsR *= mxR[kMxSources]; }
                mL += gsL; mR += gsR;
            }
            if (dnaOn)
            {
                // the splice of A and B, blended with the oscillator mix
                const float srcs[8] = { o1, o2, o3, osub, o4m * wt[0].comp, 0.5f * (w2L + w2R) * wt[1].comp, cRaw, ssRaw };
                float a = srcs[dnaCur.srcA], b = srcs[dnaCur.srcB];
                if (gsDnaSlot == 1 && gsRun) a = gsRaw;   // the Granular Synth can stand in for A or B
                else if (gsDnaSlot == 2 && gsRun) b = gsRaw;
                const double tn = t + i * isr;
                float sp;
                if (dnaFade > 0 && dnaPrev.mode == dnaCur.mode)
                {
                    // same mode, new sources: crossfade the inputs (the mode keeps one filter state)
                    const float f = (float) dnaFade / (float) dnaFadeLen;
                    a += (srcs[dnaPrev.srcA] - a) * f; b += (srcs[dnaPrev.srcB] - b) * f;
                    sp = dna.run (dnaCur.mode, a, b, tn);
                    --dnaFade;
                }
                else
                {
                    sp = dna.run (dnaCur.mode, a, b, tn);
                    if (dnaFade > 0)
                    {
                        // new mode: run the old one alongside and crossfade the outputs
                        const float old = dna.run (dnaPrev.mode, srcs[dnaPrev.srcA], srcs[dnaPrev.srcB], tn);
                        sp += (old - sp) * ((float) dnaFade / (float) dnaFadeLen);
                        --dnaFade;
                    }
                }
                dna.push (a, b);
                dna.tick();
                mL += (sp - mL) * dnp.mix;
                mR += (sp - mR) * dnp.mix;
            }

            // ring modulation
            float rL = 0, rR = 0;
            if (legacyRing)
            {
                const float lr = o1 * (1.0f + o3) * ringGainS.v;
                rL = lr; rR = lr;
            }
            for (int k = 0; k < 2; ++k)
            {
                if (! ringOn[k]) continue;
                RingSlot& r = ringSlots[k];
                auto post = [&] (int idx, bool right) -> float
                {
                    switch (idx)
                    {
                        case 0: return o1 * g1;
                        case 1: return o2 * g2;
                        case 2: return o3 * g3;
                        case 3: return (right ? o4R : o4L) * o4g;
                        case 4: return cOut;
                        case 5: return ssMono;
                        default: return right ? mR : mL;
                    }
                };
                const float srcV2 = r.src == 3 ? o4m * o4g : post (r.src, false);
                const float g = 2.0f + srcV2 * ringDepth[k].v;
                const float yl = r.lp.process (r.hp.process (driveShape (post (r.dst, false) * g, r.shapeK), 0), 0) * ringOut[k].v;
                rL += yl;
                if (stereo) rR += r.lp.process (r.hp.process (driveShape (post (r.dst, true) * g, r.shapeK), 1), 1) * ringOut[k].v;
                else rR += yl;
            }

            const float rmix = ! ARType::value ? ringMix.v : clampv (ringMix.v + ad[AD_Ring], 0.0f, 1.0f);
            if (mxActive) { rL *= mxL[kMxSources - 1]; rR *= mxR[kMxSources - 1]; }
            if (mc.meters) mxPeak[kMxSources - 1] = std::max (mxPeak[kMxSources - 1], std::max (std::abs (rL), std::abs (rR)) * rmix);
            if (opsOnlyAny)
            {
                xb[0][i] = (mL * dry.v + rL * rmix) * ampPre[i] + coreBL[i] * dry.v;
                xb[1][i] = (mR * dry.v + rR * rmix) * ampPre[i] + coreBR[i] * dry.v;
            }
            else
            {
                xb[0][i] = mL * dry.v + rL * rmix;
                xb[1][i] = mR * dry.v + rR * rmix;
            }
        }
        };
        if (nar > 0 || arFmOn) sampleLoop (std::true_type {}); else sampleLoop (std::false_type {});

        // ---- Wave Mutation and the audio-rate transform, then the filter and amp
        waveMut.process (xb[0], xb[1], n, stereo, wmp, sr);
        if (arFxOn)
            for (int i = 0; i < n; ++i) arFx.process (xb[0][i], xb[1][i], mb[i], stereo, arp, rotCos, rotSin);

        for (int i = 0; i < n; ++i)
        {
            if (arCut) filter.update (cutB[i], resB[i], sr);
            if (rxFilt)
            {
                // Break React's Filter Kick: Filter 1's cutoff (octaves) and resonance jump with the hit envelopes
                if (rxCutO[i] != 0.0f || rxResA[i] != 0.0f)
                {
                    const float c0 = arCut ? cutB[i] : cutoff.v, r0 = arCut ? resB[i] : res.v;
                    filter.update (clampv (c0 * (float) pow2 ((double) rxCutO[i]), 20.0f, 20000.0f), clampv (r0 + 12.0f * rxResA[i], 0.1f, 30.0f), sr);
                    rxFiltKick = true;
                }
                else if (rxFiltKick && ! arCut) { filter.update (cutoff.v, res.v, sr); rxFiltKick = false; }
            }
            if (rx != nullptr)
            {
                if (rxRtA && rxRtHitA[i] && ! opsOnlyAny) ampEnv.retrigger (t);
                if (rxRtF && rxRtHitF[i]) filtEnv.retrigger (t);
            }
            const float xL = xb[0][i], xR = xb[1][i];
            float yL, yR;
            filter.processFrame (xL, xR, stereo, yL, yR);
            if (routeMode != R_Off || routeFade > 0)
            {
                if (arCut2 && routeMode != R_Off) filter2.update (cutB2[i], resB2[i], sr);
                float nL, nR;
                route (routeMode, filter2, xL, xR, yL, yR, nL, nR);
                if (routeFade > 0)
                {
                    float pL, pR;
                    route (oldRouteMode, f2old, xL, xR, yL, yR, pL, pR);
                    const float lin = 1.0f - (float) routeFade / (float) FilterUnit::kFadeLen;
                    const float f = lin * lin * (3.0f - 2.0f * lin);
                    nL = pL + (nL - pL) * f;
                    nR = pR + (nR - pR) * f;
                    --routeFade;
                }
                yL = nL; yR = nR;
            }

            const float ae = (float) ampEnv.eval (t);
            const float g = srcMute.v * (opsOnlyAny ? 1.0f : ae) * vGain.v;
            float oL = yL * g, oR = yR * g;
            if (subSplit)
            {
                const float gs = srcMute.v * ae * vGain.v;
                for (int r = 0; r < 2; ++r) { subOut[r][0][done + i] += sdb[r][0][i] * gs; subOut[r][1][done + i] += sdb[r][1][i] * gs; }
            }
            if (resOn)
            {
                float rl, rr;
                reso.process (0.5f * (oL + oR), rl, rr);
                if (t > resLimit - 0.3) { const float tg2 = (float) clampv ((resLimit - t) / 0.3, 0.0, 1.0); rl *= tg2; rr *= tg2; }
                oL += (rl - oL) * rsp.mix;
                oR += (rr - oR) * rsp.mix;
                resPeak = std::max (resPeak, std::max (std::abs (rl), std::abs (rr)));
            }
            L[done + i] += oL;
            R[done + i] += oR;
            t += isr;

            if (follow)
            {
                const float a = std::abs (yL * g), b = std::abs (xL);
                envF += (a - envF) * (a > envF ? fA : fR);
                audF += (b - audF) * (b > audF ? fA : fR);
                trFast += (a - trFast) * (a > trFast ? tfA : tfR);
                trSlow += (a - trSlow) * (a > trSlow ? tsA : tsR);
            }
        }
        done += n;
        advanceLfos (ps, n);

        resoRinging = resOn && resPeak > 1.0e-5f;
        coreTail = 0.0f;
        if (opsOnlyAny)
            for (int c = 0; c < 2; ++c) if (coreOpsOnly[c]) coreTail = std::max (coreTail, core[c].heardLevel() * lCore[c].v);
        if (ampEnv.finished (t) && (! resoRinging || t > resLimit) && ! (opsOnlyAny && coreTail > 1.0e-4f)) active = false;
    }
}

//==============================================================================
BreakReact::Settings Engine::reactSettings (const Snapshot& s) const
{
    BreakReact::Settings r;
    r.on = s.v[P_rxOn] > 0.5f;
    r.listen = clampv (s.i (P_rxListen), 0, 3); r.brk = clampv (s.i (P_rxBreak), 0, 4);
    r.sens = clampv (s.f (P_rxSens), 0.0f, 1.0f);
    r.fromBreaks = s.v[P_rxFromBreaks] > 0.5f; r.fromSc = s.v[P_rxFromSc] > 0.5f; r.scSens = clampv (s.f (P_rxScSens), 0.0f, 1.0f);
    r.attackMs = clampv (s.f (P_rxAttack), 0.0f, 100.0f); r.decayMs = clampv (s.f (P_rxDecay), 5.0f, 2000.0f);
    r.offsetMs = clampv (s.f (P_rxOffset), -10.0f, 30.0f); r.humanMs = clampv (s.f (P_rxHuman), 0.0f, 10.0f);
    for (int k = 0; k < kRxRows; ++k)
    {
        auto& w = r.row[k];
        w.on = s.v[rxRowParam (k, RXF_On)] > 0.5f;
        w.sens = clampv (s.f (rxRowParam (k, RXF_Sens)), 0.0f, 1.0f);
        w.vel = clampv (s.f (rxRowParam (k, RXF_Vel)), 0.0f, 1.0f);
        w.chance = clampv (s.f (rxRowParam (k, RXF_Chance)), 0.0f, 1.0f);
        w.beats = clampv (s.i (rxRowParam (k, RXF_Beats)), 0, 3);
        w.every = 1 + clampv (s.i (rxRowParam (k, RXF_Every)), 0, 3);
    }
    r.seed = juce::roundToInt (s.f (P_rxSeed));
    r.beat0 = s.fxBeat; r.beatInc = s.fxBeatInc; r.barOrigin = s.fxBarOrigin; r.beatsPerBar = s.fxBeatsPerBar;
    return r;
}

BreakChopper::Settings Engine::breakSettings (const Snapshot& s) const
{
    BreakChopper::Settings st;
    st.level = s.f (P_bcLevel); st.pitch = s.f (P_bcPitch); st.velSens = s.f (P_bcVelSens);
    st.cycleMs = clampv (s.f (P_bcCycle), 10.0f, 200.0f); st.xfade = s.f (P_bcXfade);
    st.quality = clampv (s.i (P_quality), 0, 2);
    for (int k = 0; k < BreakChopper::kBreaks; ++k)
    {
        const auto bp = breakParams (k);
        st.b[k].stretch = clampv (s.i (bp.stretch), 0, 2); st.b[k].colour = clampv (s.i (bp.colour), 0, 4);
        st.b[k].level = s.f (bp.level); st.b[k].pan = s.f (bp.pan);
    }
    st.sync = s.v[P_bcSync] > 0.5f; st.thruFilter = s.v[P_bcThruFilter] > 0.5f;
    st.hostTempo = s.fxTempo;
    return st;
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& v : voices) v.prepare (sr);
    fxBus.prepare (sr, maxBlock);
    masterFilter.prepare (sr); outTrim.prepare (sr); perfFx.prepare (sr); chopper.prepare (sr, maxBlock); react.prepare (sr, maxBlock);
    gran.prepare (sr);
    spec.prepare (sr);
    fbm.prepare (sr, maxBlock);
    for (int c = 0; c < 2; ++c) { fbIn[c].assign ((size_t) maxBlock + 16, 0.0f); fbOut[c].assign ((size_t) maxBlock + 16, 0.0f); preFxBuf[c].assign ((size_t) maxBlock + 16, 0.0f); }
    for (int c = 0; c < 2; ++c)
    {
        brkTmp[c].assign ((size_t) maxBlock + 16, 0.0f);
        for (int b = 0; b < 3; ++b) { dryBus[b][c].assign ((size_t) maxBlock + 16, 0.0f); dryDelay[b][c].assign ((size_t) kDryDelayLen, 0.0f); }
    }
    dryDelayPos = 0; dryTail = 0; dryMasterS = -1.0f;
    globalMod.clear();
    normTable();   // build the conversion tables off the audio thread
    globalRouteState.reset();
    lastFreq = -1.0;
}

void Engine::reset()
{
    for (auto& v : voices) v.kill();
    fxBus.reset();
    perfFx.reset();
    chopper.reset();
    react.reset();
    masterFilter.reset();
    gran.reset();
    spec.reset();
    fbm.clear();
    globalMod.clear();
    for (int b = 0; b < 3; ++b) for (int c = 0; c < 2; ++c) std::fill (dryDelay[b][c].begin(), dryDelay[b][c].end(), 0.0f);
    dryTail = 0; dryMasterS = -1.0f;
    for (auto& m : monoIc) for (auto& st : m) st[0] = st[1] = 0.0f;
}

Voice* Engine::findActive (int key)
{
    for (auto& v : voices) if (v.active && ! v.released && v.key == key) return &v;
    return nullptr;
}

int Engine::activeVoiceCount() const
{
    int c = 0;
    for (auto& v : voices) if (v.active) ++c;
    return c;
}

void Engine::noteOn (int key, double freq, const Voice::StartOptions& o, const Snapshot& s)
{
    if (findActive (key) != nullptr) return;   // the browser ignores a repeated note-on
    fxBus.addSpaceHits (SH_Notes);   // (the Space ducks: Synth notes)

    // polyphony: release the oldest held voices (they keep their release tail)
    const int poly = std::max (clampv (s.i (P_polyphony), 1, 16), std::min (polyFloor, 16));
    for (;;)
    {
        int held = 0; Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.released) { ++held; if (! oldest || v.order < oldest->order) oldest = &v; }
        if (held < poly || ! oldest) break;
        oldest->release();
    }

    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (! slot)
    {
        // all slots busy with release tails: take the quietest releasing voice
        float best = 2.0f;
        for (auto& v : voices)
            if (v.released && v.currentLevel() < best) { best = v.currentLevel(); slot = &v; }
        if (! slot) slot = &voices[0];
    }

    const double glide = (s.f (P_porta) > 0.0005f && lastFreq > 0) ? lastFreq : freq;
    ModContext mc;
    mc.lfoShapes = lfoShapes; mc.lfoFree = lfoFreePh; mc.msegs = msegStore; mc.meters = mixerMeters.load (std::memory_order_relaxed);
    if (routeStore != nullptr)
    {
        routeSet.build (*routeStore);
        mc.routes = &routeSet; mc.in = modIn; mc.scenes = scenes; mc.morph = s.v[P_sceneMorph] > 0.5f;
    }
    // a new note starts from the mutated patch (envelope times are read now)
    const Snapshot* ns = &s;
    if (s.v[P_mutAmount] > 1.0e-4f)
    {
        const uint32_t seed = (uint32_t) std::lround (s.v[P_mutSeed]);
        if (seed != mutTab.seed) mutTab.build (seed);
        noteSnap = s;
        applyMutation (mutTab, s.v[P_mutAmount], mutLockMask (s), noteSnap.v);
        ns = &noteSnap;
    }
    slot->start (key, freq, glide, o, *ns, ++orderCounter, mc);
    lastFreq = freq;
}

void Engine::noteOff (int key)
{
    if (auto* v = findActive (key)) v->release();
}

void Engine::allNotesOff()
{
    for (auto& v : voices) if (v.active) v.release();
}

void Engine::render (float* L, float* R, int numSamples, const Snapshot& sDry, const WaveSample* const* wavs)
{
    // ---- the arpeggiator runs in the processor: its Swing and Gate follow last block's global sources
    arpMod[0] = arpMod[1] = 0.0f;
    if (sDry.v[P_arpOn] > 0.5f && routeStore != nullptr && routeSet.n > 0 && routeSet.anyGlobal)
    {
        arpModSnap = sDry;
        applyRoutes (routeSet, sDry.v, arpModSnap.v, lastSrc, arpRouteState, (float) (numSamples / sr), RF_Global);
        arpMod[0] = arpModSnap.v[P_arpSwing] - sDry.v[P_arpSwing];
        arpMod[1] = arpModSnap.v[P_arpGate] - sDry.v[P_arpGate];
    }

    // ---- DNA Sequencer: the running step's transform moves its destinations (before Mutate)
    const Snapshot* seqBase = &sDry;
    dsValue = dsValue2 = dsGate = 0.0f;
    if (dnaStore != nullptr && sDry.v[P_dsOn] > 0.5f)
    {
        // its playback settings (glide, depth, density, probability) can be Mod Matrix destinations:
        // they follow last block's global sources
        const float* dv = sDry.v;
        if (routeStore != nullptr && routeSet.n > 0 && routeSet.anyGlobal)
        {
            dnaModSnap = sDry;
            applyRoutes (routeSet, sDry.v, dnaModSnap.v, lastSrc, dnaRouteState, (float) (numSamples / sr), RF_Global);
            dv = dnaModSnap.v;
        }
        dsClockMod[DCM_Swing] = dv[P_dsSwing] - sDry.v[P_dsSwing];
        dsClockMod[DCM_Offset] = dv[P_dsOffset] - sDry.v[P_dsOffset];
        dsClockMod[DCM_FreeHz] = dv[P_dsFreeHz] - sDry.v[P_dsFreeHz];
        DnaPlayParams pp;
        pp.len[0] = clampv ((int) std::lround (sDry.v[P_dsSteps]), 1, 32);
        pp.len[1] = clampv ((int) std::lround (dv[P_dsLane2Steps]), 1, 32);
        pp.lane2 = sDry.v[P_dsLane2On] > 0.5f;
        pp.glide = clampv (dv[P_dsGlide], 0.0f, 1.0f);
        pp.direction = clampv ((int) std::lround (sDry.v[P_dsDirection]), 0, 3);
        pp.density = clampv (dv[P_dsDensity], 0.0f, 1.0f);
        pp.probScale = clampv (dv[P_dsProbScale], 0.0f, 2.0f);
        pp.pattern = clampv ((int) std::lround (sDry.v[P_dsPattern]), 0, 3);
        pp.chain = sDry.v[P_dsChainOn] > 0.5f;
        DnaFrame fr;
        dnaEvaluate (*dnaStore, dsPos, pp, fr);
        dsValue = fr.value[0]; dsValue2 = fr.value[1]; dsGate = fr.gate;
        dsSnap = sDry;
        applyDnaSeq (fr.w, clampv (dv[P_dsDepth], 0.0f, 1.0f), dsSnap.v);
        seqBase = &dsSnap;
        dnaSeqStep.store (fr.step[0], std::memory_order_relaxed);
        dnaSeqStep2.store (fr.step[1], std::memory_order_relaxed);
        dnaPatternPlaying.store (fr.pattern, std::memory_order_relaxed);
    }
    else
    {
        dnaSeqStep.store (-1, std::memory_order_relaxed); dnaSeqStep2.store (-1, std::memory_order_relaxed);
        dsClockMod[0] = dsClockMod[1] = dsClockMod[2] = 0.0f;
    }
    const Snapshot& sIn = *seqBase;

    // ---- Master Mutate: seeded offsets on top of the patch (the knobs are never changed)
    const uint32_t seed = (uint32_t) std::lround (sIn.v[P_mutSeed]);
    if (seed != mutTab.seed) mutTab.build (seed);
    const uint32_t lockMask = mutLockMask (sIn);
    const bool mutRouted = routeStore != nullptr && [&] { RouteSet tmp; tmp.build (*routeStore); return tmp.anyMutAmt; }();
    const Snapshot* base = &sIn;
    if (! mutRouted && sIn.v[P_mutAmount] > 1.0e-4f)
    {
        mutSnap = sIn;
        applyMutation (mutTab, sIn.v[P_mutAmount], lockMask, mutSnap.v);
        base = &mutSnap;
    }
    const Snapshot& s = *base;

    Voice* newest = nullptr;
    for (auto& v : voices) if (v.active && (! newest || v.order > newest->order)) newest = &v;

    ModContext mc;
    mc.lfoShapes = lfoShapes; mc.lfoFree = lfoFreePh; mc.msegs = msegStore; mc.meters = mixerMeters.load (std::memory_order_relaxed);
    if (routeStore != nullptr)
    {
        routeSet.build (*routeStore);
        mc.routes = &routeSet; mc.in = modIn; mc.scenes = scenes; mc.morph = s.v[P_sceneMorph] > 0.5f;
        if (mutRouted) { mc.mut = &mutTab; mc.lockMask = lockMask; }
        mc.dnaSeq = dsValue; mc.dnaSeq2 = dsValue2; mc.dnaGate = dsGate;
    }
    std::fill (std::begin (liveScratch), std::end (liveScratch), 0.0f);

    // ---- Break-Reactive mode: the slices started since the last call (and the sidechain) become hits
    BreakReact::Settings rxs;
    {
        BreakChopper::HitLogEntry hl[BreakChopper::kHitLog];
        const int nh = chopper.takeHits (hl);
        // (v0.1.70) the Space effects' ducks and gate hear the break hits too: kick / snare by the same sorting as Break React
        if (nh > 0)
        {
            uint32_t bits = 0;
            const BreakData& bd0 = chopper.data();
            const bool band = s.i (P_rxDetect) == 1;
            for (int k = 0; k < nh; ++k)
            {
                const uint32_t seqBit = hl[k].layer ? SH_Layer : SH_Main;
                const BreakSlot& b = bd0.brk[clampv (hl[k].brk, 0, BreakChopper::kBreaks - 1)];
                const int sl = clampv (hl[k].slice, 0, BreakSlot::kSlices - 1);
                int type = b.slice[sl].hitType - 1;   // (set by hand: 0 kick, 1 snare, 2 hat; 3 none)
                if (type < 0)
                {
                    const float kk = band ? b.kickB[sl] : b.kick[sl], sn = band ? b.snareB[sl] : b.snare[sl], ht = band ? b.hatB[sl] : b.hat[sl];
                    type = kk >= sn && kk >= ht ? 0 : (sn >= ht ? 1 : 2);
                }
                if (type == 3) continue;   // (None: never a hit)
                bits |= SH_Hits | seqBit;
                if (type == 0) bits |= SH_Kick;
                if (type == 1) bits |= SH_Snare;
            }
            if (bits != 0) fxBus.addSpaceHits (bits);
        }
        if (s.v[P_rxOn] > 0.5f)
        {
            // (its listening and shape settings follow last block's global sources, like the arpeggiator's)
            const Snapshot* rs = &s;
            if (routeStore != nullptr && routeSet.n > 0 && routeSet.anyGlobal)
            {
                rxModSnap = s;
                applyRoutes (routeSet, s.v, rxModSnap.v, lastSrc, rxRouteState, (float) (numSamples / sr), RF_Global);
                rs = &rxModSnap;
            }
            rxs = reactSettings (*rs);
            const BreakData& bd = chopper.data();
            for (int k = 0; k < nh; ++k)
            {
                // From: the main sequencer (and the keys), the layer sequencer, or both
                const int from = clampv (s.i (P_rxFrom), 0, 2);
                if ((from == 0 && hl[k].layer) || (from == 1 && ! hl[k].layer)) continue;
                const BreakSlot& b = bd.brk[clampv (hl[k].brk, 0, BreakChopper::kBreaks - 1)];
                const int sl = clampv (hl[k].slice, 0, BreakSlot::kSlices - 1);
                const int ht = b.slice[sl].hitType;
                if (ht == 4) continue;                                                      // set to None: never a hit
                if (ht >= 1) { react.typedHit (rxs, hl[k].brk, ht - 1, hl[k].vel); continue; }  // set by hand
                if (s.i (P_rxDetect) == 1) react.breakHit (rxs, hl[k].brk, b.kickB[sl], b.snareB[sl], b.hatB[sl], hl[k].vel);
                else react.breakHit (rxs, hl[k].brk, b.kick[sl], b.snare[sl], b.hat[sl], hl[k].vel);
            }
            react.begin (numSamples, rxs, scL, scR);
            mc.rx = &react;
            if (react.hitCount() > 0) rxHitsShown.fetch_add (react.hitCount(), std::memory_order_relaxed);
            rxTypeShown.store (react.hitType(), std::memory_order_relaxed);
            rxEnvShown.store (react.envelope()[numSamples - 1], std::memory_order_relaxed);
            for (int r = 0; r < kRxRows; ++r)
            {
                if (react.rowHitCount (r) > 0) rxRowHitsShown[r].fetch_add (react.rowHitCount (r), std::memory_order_relaxed);
                rxRowEnvShown[r].store (react.rowLive (r) ? react.envelope (r)[numSamples - 1] : 0.0f, std::memory_order_relaxed);
            }
            for (int k = 0; k < react.callHitCount(); ++k)
            {
                const auto& h = react.callHitAt (k);
                double place = 0.0;
                if (s.fxBeatInc > 0.0)
                {
                    const double bpb = std::max (1.0, s.fxBeatsPerBar), b = s.fxBeat + h.at * s.fxBeatInc - s.fxBarOrigin;
                    place = (b - bpb * std::floor (b / bpb)) / bpb;
                }
                const int packed = clampv ((int) (place * 4096.0), 0, 4095) | (clampv (h.type, 0, 3) << 12) | ((int) (h.mask & 15u) << 16);
                const int at = rxLogCount.fetch_add (1, std::memory_order_relaxed);
                rxLog[(size_t) (at % kRxLog)].store (packed, std::memory_order_relaxed);
            }
        }
        else react.idle (numSamples);
    }

    ModState scratch;
    const bool dryReady = (int) dryBus[0][0].size() >= numSamples;
    if (dryReady) for (int b = 0; b < 3; ++b) for (int c = 0; c < 2; ++c) std::fill (dryBus[b][c].begin(), dryBus[b][c].begin() + numSamples, 0.0f);
    for (auto& v : voices)
    {
        for (int c = 0; c < 2; ++c) { v.subOut[0][c] = dryReady ? dryBus[1][c].data() : nullptr; v.subOut[1][c] = dryReady ? dryBus[2][c].data() : nullptr; }
        v.subDryUsed = false;
    }
    for (auto& v : voices)
        if (v.active)
            v.render (L, R, numSamples, s, wavs, &v == newest ? globalMod : scratch, mc, &v == newest ? liveScratch : nullptr);
    bool anyDry = false;
    for (auto& v : voices) anyDry = anyDry || v.subDryUsed;
    if (anyDry && s.v[P_subMono] > 0.5f)
    {
        // Sub Mono: below 120 Hz the dry sub is mono (its pan only moves what's above)
        const float g = (float) std::tan (kPi * 120.0 / sr);
        const float ks[2] = { 1.8477591f, 0.7653669f };   // 4th-order Butterworth
        for (int b = 0; b < 2; ++b)
        {
            float* l = dryBus[1 + b][0].data(); float* r = dryBus[1 + b][1].data();
            for (int i = 0; i < numSamples; ++i)
            {
                const float mid = 0.5f * (l[i] + r[i]);
                float hp = 0.5f * (l[i] - r[i]);
                for (int st = 0; st < 2; ++st)
                {
                    const float k = ks[st], a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                    float& ic1 = monoIc[b][st][0]; float& ic2 = monoIc[b][st][1];
                    const float v3 = hp - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
                    hp = hp - k * v1 - v2;   // the side, above 120 Hz only
                }
                l[i] = mid + hp; r[i] = mid - hp;
            }
        }
    }
    if (mc.meters)
    {
        // the Mixer's strip meters: the loudest voice's level per source, held until the editor reads it
        float pk[kMxStrips] {};
        for (auto& v : voices)
            for (int k = 0; k < kMxStrips; ++k) { if (v.active) pk[k] = std::max (pk[k], v.mxPeak[k]); v.mxPeak[k] = 0.0f; }
        for (int k = 0; k < kMxStrips; ++k)
            if (pk[k] > mixerPeak[k].load (std::memory_order_relaxed)) mixerPeak[k].store (pk[k], std::memory_order_relaxed);
    }
    // the shared "Free" LFO clocks (Hz mode) and the editor's playheads
    for (int k = 0; k < 4; ++k)
    {
        lfoFreePh[k] = wrap01 (lfoFreePh[k] + (double) s.f (P_lfo1Rate + 3 * k) * numSamples / sr);
        lfoPlayhead[k].store (newest != nullptr && newest->active ? newest->lfoPhaseShown (k) : -1.0f, std::memory_order_relaxed);
    }
    {
        int grains = 0;
        for (auto& v : voices) if (v.active) grains += v.granGrains();
        granGrainsShown.store (grains, std::memory_order_relaxed);
        granPlayheadShown.store (newest != nullptr && newest->active ? newest->granPlayhead() : -1.0, std::memory_order_relaxed);
    }
    for (int k = 0; k < 2; ++k)
    {
        msegPlayhead[k].store (newest != nullptr && newest->active ? newest->msegShown (k) : -1.0f, std::memory_order_relaxed);
        msegLevelShown[k].store (newest != nullptr && newest->active ? newest->msegLevel (k) : 0.0f, std::memory_order_relaxed);
    }

    // Global effects are shared by all voices, so routes onto them follow the newest voice's
    // sources (or, with no note playing, the MIDI controllers alone).
    const Snapshot* fs = &s;
    const float* src = newest != nullptr ? newest->srcV : globalSrc;
    if (newest == nullptr)
    {
        std::fill (std::begin (globalSrc), std::end (globalSrc), 0.0f);
        if (modIn != nullptr)
        {
            globalSrc[MS_ModWheel] = modIn->wheel; globalSrc[MS_PitchBend] = modIn->bend;
            globalSrc[MS_Aftertouch] = modIn->aftertouch; globalSrc[MS_CcA] = modIn->ccA; globalSrc[MS_CcB] = modIn->ccB;
            globalSrc[MS_ArpPos] = modIn->arpPos; globalSrc[MS_ArpGate] = modIn->arpGate; globalSrc[MS_Sidechain] = modIn->sidechain;
            globalSrc[MS_BsStep] = modIn->bsStep; globalSrc[MS_BsPattern] = modIn->bsPattern; globalSrc[MS_BsSlice] = modIn->bsSlice; globalSrc[MS_BsBreak] = modIn->bsBreak; globalSrc[MS_BsFill] = modIn->bsFill; globalSrc[MS_LsStep] = modIn->lsStep; globalSrc[MS_LsPattern] = modIn->lsPattern;
        }
        for (int k = 0; k < 8; ++k) globalSrc[MS_Macro1 + k] = s.v[P_macro1 + k];
        for (int k = 0; k < 4; ++k) globalSrc[MS_PfX1 + k] = 2.0f * s.v[P_pfX1 + k] - 1.0f;
        globalSrc[MS_DnaSeq] = dsValue; globalSrc[MS_DnaSeq2] = dsValue2; globalSrc[MS_DnaGate] = dsGate;
        globalSrc[MS_BreakHit] = mc.rx != nullptr ? react.envelope()[numSamples - 1] : 0.0f;
        globalSrc[MS_BreakHitType] = mc.rx != nullptr ? react.hitType() : 0.0f;
        for (int r = 1; r < kRxRows; ++r) globalSrc[MS_KickHit + r - 1] = mc.rx != nullptr && react.rowLive (r) ? react.envelope (r)[numSamples - 1] : 0.0f;
    }
    float* srcW = newest != nullptr ? newest->srcV : globalSrc;
    if (mc.any() && (routeSet.anyGlobal || (mc.morph && scenes != nullptr && routeSet.anySceneXY)))
    {
        fxSnap = s;
        const float dt = (float) (numSamples / sr);
        if (mc.morph && scenes != nullptr && routeSet.anySceneXY)
        {
            applyRoutes (routeSet, s.v, fxSnap.v, srcW, globalRouteState, dt, RF_Global, nullptr, RP_SceneXY);
            morphScenes (*scenes, fxSnap.v[P_sceneX], fxSnap.v[P_sceneY], fxSnap.v);
            applyRoutes (routeSet, fxSnap.v, fxSnap.v, srcW, globalRouteState, dt, RF_Global, liveScratch, RP_AfterScene);
        }
        else applyRoutes (routeSet, s.v, fxSnap.v, srcW, globalRouteState, dt, RF_Global, liveScratch);
        fs = &fxSnap;
    }
    if (mutRouted && s.v[P_mutAmount] > 1.0e-4f)
    {
        // the voices mutate themselves (their Mutate amount is modulated); the effects use the knob's amount
        if (fs != &fxSnap) { fxSnap = s; fs = &fxSnap; }
        applyMutation (mutTab, s.v[P_mutAmount], lockMask, fxSnap.v, true);
    }
    // ---- the Break Chopper: its slices join the bus here, before the bus stages and the effects
    if (fs->v[P_bcOn] > 0.5f || chopper.activeVoices() > 0)
    {
        if (fs->v[P_bcOn] > 0.5f)
        {
            if (breakStore != nullptr) breakStore->pull (chopper.data(), breakSeen);
            for (int k = 0; k < BreakChopper::kBreaks; ++k) chopper.setSample (k, wavs != nullptr ? wavs[3 + k] : nullptr);
        }
        else chopper.stopAll();
        auto st = breakSettings (*fs);
        // its Mixer strip (strip 10): balance pan, mute / solo like the others, ramped
        bool anySolo = false;
        for (int k = 0; k < kMxStrips; ++k) anySolo |= fs->v[mxParam (k) + 2] > 0.5f;
        const int b = mxParam (kMxSources + 1);
        const bool muted = fs->v[b + 1] > 0.5f || (anySolo && fs->v[b + 2] <= 0.5f);
        const float pan = clampv (fs->f (b), -1.0f, 1.0f);
        const float tl = muted ? 0.0f : std::min (1.0f, 1.0f - pan), tr = muted ? 0.0f : std::min (1.0f, 1.0f + pan);
        const float c = ! breakWasRunning ? 1.0f : 1.0f - std::exp (-(float) numSamples / (0.01f * (float) sr));   // (starting from silence: no ramp needed)
        breakGateL += (tl - breakGateL) * c; breakGateR += (tr - breakGateR) * c;
        if (std::abs (breakGateL - tl) < 1.0e-4f) breakGateL = tl;
        if (std::abs (breakGateR - tr) < 1.0e-4f) breakGateR = tr;
        st.stripL = breakGateL; st.stripR = breakGateR;
        {
            // the Break Layer's strip (the layer sequencer): its level, balance pan, mute / solo, ramped the same way
            const int lb = mxParam (kMxLayerStrip);
            const bool lm = fs->v[lb + 1] > 0.5f || (anySolo && fs->v[lb + 2] <= 0.5f);
            const float lp = clampv (fs->f (lb), -1.0f, 1.0f);
            const float ll = lm ? 0.0f : std::min (1.0f, 1.0f - lp), lr = lm ? 0.0f : std::min (1.0f, 1.0f + lp);
            layerGateL += (ll - layerGateL) * c; layerGateR += (lr - layerGateR) * c;
            if (std::abs (layerGateL - ll) < 1.0e-4f) layerGateL = ll;
            if (std::abs (layerGateR - lr) < 1.0e-4f) layerGateR = lr;
            st.layerL = layerGateL; st.layerR = layerGateR; st.layerLevel = clampv (fs->f (P_bsLLevel), 0.0f, 1.0f);
        }
        // Break FX Route: through the effects (as always), or past them into a dry bus, ramped over 10 ms
        const int route = dryReady ? clampv (fs->i (P_bcFxRoute), 0, 3) : 0;
        if (! breakWasRunning) for (int k = 0; k < 4; ++k) brkRG[k] = k == route ? 1.0f : 0.0f;   // (nothing playing: start on the route)
        // Break-Reactive mode with a negative Offset: the breaks play that much later (the synth reacts first)
        const int rxDelay = react.breakDelay (rxs);
        if (route == 0 && brkRG[0] == 1.0f && rxDelay == 0) { chopper.process (L, R, numSamples, st, fs->v); chopper.processLayer (L, R, numSamples, st, fs->v); }
        else
        {
            std::fill (brkTmp[0].begin(), brkTmp[0].begin() + numSamples, 0.0f); std::fill (brkTmp[1].begin(), brkTmp[1].begin() + numSamples, 0.0f);
            chopper.process (brkTmp[0].data(), brkTmp[1].data(), numSamples, st, fs->v);
            chopper.processLayer (brkTmp[0].data(), brkTmp[1].data(), numSamples, st, fs->v);
            if (rxDelay > 0) react.delayBreaks (brkTmp[0].data(), brkTmp[1].data(), numSamples, rxDelay);
            const float rc = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
            float* dst[4][2] = { { L, R }, { dryBus[0][0].data(), dryBus[0][1].data() }, { dryBus[1][0].data(), dryBus[1][1].data() }, { dryBus[2][0].data(), dryBus[2][1].data() } };
            for (int i = 0; i < numSamples; ++i)
            {
                for (int k = 0; k < 4; ++k)
                {
                    const float tg = k == route ? 1.0f : 0.0f;
                    brkRG[k] += (tg - brkRG[k]) * rc;
                    if (std::abs (tg - brkRG[k]) < 1.0e-5f) brkRG[k] = tg;
                    if (brkRG[k] != 0.0f) { dst[k][0][i] += brkTmp[0][(size_t) i] * brkRG[k]; dst[k][1][i] += brkTmp[1][(size_t) i] * brkRG[k]; }
                }
            }
            anyDry = true;
        }
        const float pk = chopper.takePeak();
        auto& m = mixerPeak[(size_t) (kMxSources + 1)];
        if (pk > m.load (std::memory_order_relaxed)) m.store (pk, std::memory_order_relaxed);
        const float lpk = chopper.takeLayerPeak();
        auto& lm2 = mixerPeak[(size_t) kMxLayerStrip];
        if (lpk > lm2.load (std::memory_order_relaxed)) lm2.store (lpk, std::memory_order_relaxed);
        breakPlayheadShown.store (chopper.playheadFrac (clampv (fs->i (P_bcActive) - 1, 0, BreakChopper::kBreaks - 1)), std::memory_order_relaxed);
        breakSliceShown.store (chopper.activeVoices() > 0 ? chopper.lastSlicePlayed() : -1, std::memory_order_relaxed);
        breakWasRunning = chopper.activeVoices() > 0;
    }
    else breakWasRunning = false;

    // ---- bus mutation stages, then the effects
    {
        GranularParams gp;
        gp.mix = fs->f (P_grMix); gp.sizeMs = fs->f (P_grSize); gp.density = fs->f (P_grDensity); gp.position = fs->f (P_grPosition);
        gp.jitter = fs->f (P_grJitter); gp.pitch = fs->f (P_grPitch); gp.pitchRand = fs->f (P_grPitchRand); gp.reverse = fs->f (P_grReverse);
        gp.spread = fs->f (P_grSpread); gp.feedback = fs->f (P_grFeedback); gp.freeze = fs->f (P_grFreeze) > 0.5f;
        gp.maxGrains = fs->i (P_quality) == 0 ? 24 : 64;
        SpectralParams sp;
        sp.on = fs->f (P_spOn) > 0.5f; sp.freeze = fs->f (P_spFreeze) > 0.5f; sp.sizeIndex = effectiveSpectralSize (clampv (fs->i (P_spSize), 0, 3), fs->i (P_quality));
        sp.mix = fs->f (P_spMix); sp.blur = fs->f (P_spBlur); sp.shiftHz = fs->f (P_spShift); sp.scramble = fs->f (P_spScramble);
        sp.tilt = fs->f (P_spTilt); sp.morph = fs->f (P_spMorph); sp.formant = fs->f (P_spFormant); sp.feedback = fs->f (P_spFeedback);
        // feedback matrix: each stage's input gets the paths aimed at it, its output is recorded
        FeedbackParams fp;
        for (int s = 0; s < FT_COUNT; ++s)
            for (int d = 0; d < FD_COUNT; ++d) fp.amt[s][d] = fs->f (P_fbGrGr + s * FD_COUNT + d);
        fp.timeMs = fs->f (P_fbTime); fp.toneHz = fs->f (P_fbTone); fp.safety = fs->f (P_fbSafety);
        const bool fbOn = fbm.begin (fp, numSamples);
        fxBus.setDnaGate (dsGate);
        auto keepPre = [&] { if (keepPreFx) { std::copy (L, L + numSamples, preFxBuf[0].begin()); std::copy (R, R + numSamples, preFxBuf[1].begin()); } };
        auto runGran = [&] { fbm.inject (FD_Granular, L, R); gran.process (L, R, numSamples, gp); fbm.write (FT_Granular, L, R); };
        auto runSpec = [&] { fbm.inject (FD_Spectral, L, R); spec.process (L, R, numSamples, sp); fbm.write (FT_Spectral, L, R); };
        if (fs->i (P_busOrder) == 1) { runSpec(); runGran(); }
        else                         { runGran(); runSpec(); }

        keepPre();
        if (fbOn)
        {
            float* din[2] = { fbIn[0].data(), fbIn[1].data() };
            float* dout[2] = { fbOut[0].data(), fbOut[1].data() };
            std::fill (din[0], din[0] + numSamples, 0.0f); std::fill (din[1], din[1] + numSamples, 0.0f);
            fbm.inject (FD_Delay, din[0], din[1]);
            fxBus.process (L, R, numSamples, *fs, globalMod, din, dout);
            fbm.write (FT_Delay, dout[0], dout[1]);
            fbm.write (FT_Output, L, R);
            fbm.end (L, R);
        }
        else fxBus.process (L, R, numSamples, *fs, globalMod);

        // ---- the dry buses: Master Volume, then the Spectral stage's latency, so they line up with the rest
        const bool dryOn = dryReady && (anyDry || dryTail > 0);
        if (dryOn)
        {
            const int lat = clampv (spec.latency (sp), 0, kDryDelayLen - 1);
            dryTail = anyDry ? lat + numSamples : std::max (0, dryTail - numSamples);
            const float mvT = fs->f (P_masterVolume);
            if (dryMasterS < 0.0f) dryMasterS = mvT;
            const float cs = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
            for (int i = 0; i < numSamples; ++i)
            {
                dryMasterS += (mvT - dryMasterS) * cs;
                const int w = (dryDelayPos + i) & (kDryDelayLen - 1), rd = (dryDelayPos + i - lat) & (kDryDelayLen - 1);
                for (int b = 0; b < 3; ++b)
                    for (int c = 0; c < 2; ++c)
                    {
                        float& x = dryBus[b][c][(size_t) i];
                        x *= dryMasterS;
                        if (lat > 0) { dryDelay[b][c][(size_t) w] = x; x = dryDelay[b][c][(size_t) rd]; }
                    }
            }
            dryDelayPos = (dryDelayPos + numSamples) & (kDryDelayLen - 1);
            for (int i = 0; i < numSamples; ++i) { L[i] += dryBus[0][0][(size_t) i]; R[i] += dryBus[0][1][(size_t) i]; }   // the break, into Performance FX
        }

        // ---- Performance FX (bypassed, untouched, while no pad is engaged)
        {
            PerfFx::Settings pf;
            for (int k = 0; k < PerfFx::kPads; ++k) pf.pad[k] = fs->v[pfPadParam (k)] > 0.5f;
            pf.akaiMode = clampv (fs->i (P_pfAkaiMode), 0, 3); pf.akaiHard = fs->i (P_pfAkaiSpeed) == 1; pf.akaiSync = clampv (fs->i (P_pfAkaiSync), 0, 2);
            pf.akaiCycleMs = fs->f (P_pfAkaiCycle); pf.akaiXfade = fs->f (P_pfAkaiXfade); pf.akaiPitch = fs->f (P_pfAkaiPitch); pf.akaiMix = fs->f (P_pfAkaiMix);
            pf.quant = clampv (fs->i (P_pfQuant), 0, 5); pf.rollRate = clampv (fs->i (P_pfRollRate), 0, 5);
            pf.stutterRate = clampv (fs->i (P_pfStutterRate), 0, 2); pf.gatePattern = clampv (fs->i (P_pfGatePattern), 0, 7);
            pf.echoTime = clampv (fs->i (P_pfEchoTime), 0, 4);
            pf.tapeBeats = fs->f (P_pfTapeTime); pf.glide = fs->f (P_pfPitchGlide); pf.pitchSemis = fs->f (P_pfPitchAmt);
            pf.gateDepth = fs->f (P_pfGateDepth); pf.djPos = fs->f (P_pfDjPos); pf.djRes = fs->f (P_pfDjRes);
            pf.crushBits = fs->f (P_pfCrushBits); pf.crushRate = fs->f (P_pfCrushRate); pf.vinyl = fs->f (P_pfVinylAmt);
            pf.echoFb = fs->f (P_pfEchoFb); pf.echoMix = fs->f (P_pfEchoMix); pf.verbSize = fs->f (P_pfVerbSize); pf.verbMix = fs->f (P_pfVerbMix);
            pf.verbFreeze = fs->f (P_pfVerbFreeze) > 0.5f;
            PerfFx::Clock ck;
            ck.beat = s.fxBeat; ck.beatInc = s.fxBeatInc; ck.barOrigin = s.fxBarOrigin; ck.beatsPerBar = s.fxBeatsPerBar; ck.tempo = s.fxTempo;
            perfFx.process (L, R, numSamples, pf, ck);
        }
        if (dryOn) for (int i = 0; i < numSamples; ++i) { L[i] += dryBus[1][0][(size_t) i]; R[i] += dryBus[1][1][(size_t) i]; }   // into the Master Filter

        // ---- the Master Filter (everything passes through it), then the Output trim
        MasterFilter::Settings mf;
        mf.on = fs->f (P_mfOn) > 0.5f; mf.type = clampv (fs->i (P_mfType), 0, 3); mf.steep = fs->i (P_mfSlope) == 1;
        mf.cutoff = fs->f (P_mfCutoff); mf.res = fs->f (P_mfRes); mf.driveDb = fs->f (P_mfDrive); mf.mix = fs->f (P_mfMix);
        mf.charModel = fs->i (P_mfChar); mf.charAmt = fs->f (P_mfCharAmt); mf.model = fs->i (P_mfModel);
        masterFilter.process (L, R, numSamples, mf);
        if (dryOn) for (int i = 0; i < numSamples; ++i) { L[i] += dryBus[2][0][(size_t) i]; R[i] += dryBus[2][1][(size_t) i]; }   // after it: only Output
        outTrim.process (L, R, numSamples, fs->f (P_outGain));
    }

    for (int i = 0; i < P_COUNT; ++i) liveOffset[(size_t) i].store (liveScratch[i], std::memory_order_relaxed);
    for (int i = 0; i < MS_COUNT; ++i) { liveSrc[(size_t) i].store (src[i], std::memory_order_relaxed); lastSrc[i] = src[i]; }
}

} // namespace tg
