#include "Engine.h"
#include <cstring>

namespace tg
{

static constexpr int kCtrl = 16; // control-rate block (samples)

static inline double pow2 (double x) { return std::exp2 (x); }
static inline double wrap01 (double p) { p -= std::floor (p); return p; }

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
// FilterChain: the 17 filter "flavours" of the browser synth, built from the same
// Web Audio biquad stages, waveshapers and feedback paths.
enum FilterMode { FM_LP, FM_SALLENKEY, FM_LADDER, FM_STEINER, FM_SVF, FM_OTA, FM_POLIVOKS, FM_SWCAP, FM_ACID,
                  FM_MS20, FM_OBERHEIM, FM_SEM, FM_TB303, FM_MOOGFAT, FM_ARPODYSSEY, FM_CS15, FM_SH2 };

void FilterChain::configure (int newMode)
{
    mode = newMode;
    fbKind = FbNone; preK = 0; postK = 0; fbGain = 0; ladder = false; limitOut = false; tb303 = false;
    auto set = [this] (std::initializer_list<Biquad::Type> t)
    {
        numStages = 0;
        for (auto x : t) types[numStages++] = x;
    };
    using B = Biquad;
    switch (mode)
    {
        case FM_LP:          set ({ B::LP }); break;
        case FM_SALLENKEY:   set ({ B::LP, B::LP }); break;
        case FM_LADDER:      set ({}); ladder = true; break;
        case FM_STEINER:     set ({ B::HP, B::BP, B::LP }); break;
        case FM_SVF:         set ({ B::BP, B::LP }); break;
        case FM_OTA:         set ({ B::LP, B::LP }); preK = 4.0f; break;
        case FM_POLIVOKS:    set ({ B::HP, B::LP }); postK = 7.0f; break;
        case FM_SWCAP:       set ({ B::LP, B::NOTCH, B::LP }); break;
        case FM_MS20:        set ({ B::HP, B::LP }); break;
        case FM_OBERHEIM:    set ({ B::HP, B::LP }); break;
        case FM_SEM:         set ({ B::BP, B::LP }); break;
        case FM_TB303:       set ({}); ladder = true; tb303 = true; limitOut = true; break;
        case FM_MOOGFAT:     set ({}); ladder = true; break;
        case FM_ARPODYSSEY:  set ({ B::HP, B::LP }); break;
        case FM_CS15:        set ({ B::HP, B::BP, B::LP }); break;
        case FM_SH2:         set ({ B::LP, B::LP }); break;
        case FM_ACID:
        default:             set ({ B::LP, B::LP }); preK = 6.0f; limitOut = true; break;
    }
    reset();
}

void FilterChain::reset()
{
    for (auto& s : st) s.reset();
    fbState[0] = fbState[1] = 0;
    for (auto& ch : ls) for (auto& v : ch) v = 0.0f;
    for (auto& b : par) b.reset();
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    tbX[0] = tbX[1] = tbY[0] = tbY[1] = 0.0f;
}

void FilterChain::copyChannel (int from, int to)
{
    for (auto& s : st) s.copyState (from, to);
    fbState[to] = fbState[from];
    for (int k = 0; k < 4; ++k) ls[to][k] = ls[from][k];
    for (auto& b : par) b.copyState (from, to);
    dcX[to] = dcX[from]; dcY[to] = dcY[from];
    tbX[to] = tbX[from]; tbY[to] = tbY[from];
}

void FilterChain::setLadder (float c, float spread, float k, double sr)
{
    for (int i = 0; i < 4; ++i)
    {
        const double fc = clampv ((double) std::max (20.0f, c * (1.0f - i * spread)), 10.0, sr * 0.45);
        const double g = std::tan (kPi * fc / sr);
        lg[i] = (float) (g / (1.0 + g));
    }
    lk = k;
}

void FilterChain::update (float c, float res, double sr)
{
    auto mx = [] (float a, float b) { return std::max (a, b); };
    auto mn = [] (float a, float b) { return std::min (a, b); };
    dcR = (float) (1.0 - 2.0 * kPi * 8.0 / sr);
    auto S = [&] (int k, float f, float q)
    {
        if (types[k] == Biquad::HP) f = std::max (20.0f, f * (1.0f - 0.85f * bassKeep));
        st[k].set (types[k], f, q, sr);
        if (types[k] == Biquad::BP) par[k].set (Biquad::LP, f, -3.01, sr);   // Butterworth low-pass at the same frequency
    };
    switch (mode)
    {
        case FM_LP:
            S (0, c, res); break;
        case FM_SALLENKEY:
            S (0, c, mx (0.2f, res * 0.18f));
            S (1, mx (20, c * 0.92f), mx (0.2f, res * 0.18f)); break;
        case FM_LADDER:
            setLadder (c, 0.0f, 4.0f * mn (0.98f, res / 30.0f), sr); break;
        case FM_STEINER:
            S (0, mx (20, c * 0.55f), mx (0.2f, res * 0.12f));
            S (1, c, mx (0.2f, res * 0.35f));
            S (2, mn (18000, c * 1.15f), mx (0.2f, res * 0.18f)); break;
        case FM_SVF:
            S (0, c, mx (0.2f, res * 0.28f));
            S (1, mn (18000, c * 1.08f), mx (0.2f, res * 0.14f)); break;
        case FM_OTA:
            S (0, c, mx (0.2f, res * 0.16f));
            S (1, mx (20, c * 0.88f), mx (0.2f, res * 0.22f)); break;
        case FM_POLIVOKS:
            S (0, mx (20, c * 0.65f), mx (0.2f, res * 0.2f));
            S (1, c, mx (0.2f, res * 0.55f)); break;
        case FM_SWCAP:
            S (0, c, mx (0.2f, res * 0.12f));
            S (1, mx (20, c * 1.35f), mx (0.2f, res * 0.35f));
            S (2, mx (20, c * 0.82f), mx (0.2f, res * 0.12f)); break;
        case FM_MS20:
            S (0, mx (20, c * 0.62f), mx (0.25f, res * 0.18f));
            S (1, c, mx (0.3f, res * 0.72f)); break;
        case FM_OBERHEIM:
            S (0, mx (20, c * 0.28f), mx (0.2f, res * 0.08f));
            S (1, mn (18000, c * 1.08f), mx (0.2f, res * 0.2f)); break;
        case FM_SEM:
            S (0, c, mx (0.2f, res * 0.18f));
            S (1, mn (18000, c * 1.12f), mx (0.2f, res * 0.14f)); break;
        case FM_TB303:
            tbA = (float) (1.0 / (1.0 + 2.0 * kPi * 150.0 / sr));
            setLadder (c, 0.0f, 4.0f * mn (0.98f, res / 24.0f), sr); break;
        case FM_MOOGFAT:
            setLadder (c, 0.03f, 4.0f * mn (0.985f, res / 22.0f), sr); break;
        case FM_ARPODYSSEY:
            S (0, mx (20, c * 0.42f), mx (0.2f, res * 0.12f));
            S (1, mn (18000, c * 1.04f), mx (0.2f, res * 0.24f)); break;
        case FM_CS15:
            S (0, mx (20, c * 0.5f), mx (0.2f, res * 0.1f));
            S (1, c, mx (0.2f, res * 0.42f));
            S (2, mn (18000, c * 1.1f), mx (0.2f, res * 0.16f)); break;
        case FM_SH2:
            S (0, c, mx (0.2f, res * 0.16f));
            S (1, mx (20, c * 0.9f), mx (0.2f, res * 0.36f)); break;
        case FM_ACID:
        default:
        {
            // resonance fades below ~500 Hz so a closed filter doesn't boom at its cutoff
            const float rs = clampv ((c - 60.0f) / 440.0f, 0.2f, 1.0f);
            S (0, c, res * 0.8f * rs);
            S (1, mx (20, c * 0.95f), res * 1.2f * rs);
            fbGain = mn (0.92f, res / 28.0f); break;
        }
    }
}

//==============================================================================
void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    active = false;
}

static uint32_t voiceRandState = 0x9E3779B9u;
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
    startedAt = voiceRand01() * 1000.0;
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
    envF = audF = trFast = trSlow = 0.0f;
    for (auto& a : aPrev) a = 0.0f;
    for (int i = 0; i < 4; ++i) { lfoPh[i] = 0.0; lfoDepthNow[i] = s.f (P_lfo1Depth + 3 * i); }
    rstate.reset();
    lastDt = 0.0f;
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
    ampEnv.init (envV[P_ampA], envV[P_ampD], envV[P_ampS], envV[P_ampR]);
    filtEnv.init (envV[P_fEnvA], envV[P_fEnvD], envV[P_fEnvS], envV[P_fEnvR]);
    for (int e = 0; e < 3; ++e)
        modEnv[e].init (envV[P_mEnv1A + 4 * e], envV[P_mEnv1D + 4 * e], envV[P_mEnv1S + 4 * e], envV[P_mEnv1R + 4 * e]);

    filter.configure (s.i (P_filterMode));
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
    std::fill (std::begin (srcVals), std::end (srcVals), 0.0f);
    for (auto& w : wt) { w.pos = -1.0; w.stopped = false; w.loaded = false; w.wav = nullptr; }   // start set on the first control update

    // Initialise every smoother at its target. Oscillators start at the glide-from pitch.
    baseFreq = glideFrom;
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
        const float raw = lfoShape (wave, lfoPh[src]);   // phase advances at the (possibly modulated) rate
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
        v[MS_Lfo1 + k] = lfoShape (clampv (s.i (P_lfo1Wave + 3 * k), 0, 3), lfoPh[k]) * lfoDepthNow[k];
    v[MS_AmpEnv]    = (float) ampEnv.eval (t);
    v[MS_FilterEnv] = (float) filtEnv.eval (t);
    for (int e = 0; e < 3; ++e) v[MS_ModEnv1 + e] = (float) modEnv[e].eval (t);
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
    }
    v[MS_RandNote] = randNote;

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

void Voice::advanceLfos (const Snapshot& s, int n)
{
    const double secs = n / sr;
    for (int k = 0; k < 4; ++k)
    {
        lfoPh[k] = wrap01 (lfoPh[k] + s.f (P_lfo1Rate + 3 * k) * secs);
        lfoDepthNow[k] = s.f (P_lfo1Depth + 3 * k);
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
                        + rm * std::max (0.0f, s.f (P_ringGain));
    const float mute = audible > 0.0001f ? 1.0f : 0.0f;
    srcMute.target = mute;
    srcMute.step (first ? 1.0f : smoothCoef (dtc, mute > 0 ? 0.008 : 0.02));
    const float ampMod = clampv (1.0f + mod[MT_amp] + accentBoost, 0.0f, 2.5f);
    vGain.target = mute * ampMod * velGain;
    vGain.step (c20);

    // --- filter
    const int fmode = s.i (P_filterMode);
    if (fmode != filter.mode) filter.configure (fmode);
    filter.warm = s.f (P_warmth);
    filter.bassKeep = s.f (P_bassKeep);
    const float fe = (float) filtEnv.eval (t);
    cutoff.target = clampv (s.f (P_filterCutoff) + filterAccent + s.f (P_fEnvAmt) * fe + mod[MT_cutoff], 20.0f, 18000.0f);
    res.target = clampv (s.f (P_filterRes) + mod[MT_resonance], 0.1f, 30.0f);
    drive = clampv (s.f (P_filterDrive) + mod[MT_drive], 1.0f, 30.0f);
    cutoff.step (c10); res.step (c10);
    filter.update (cutoff.v, res.v, sr);

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
        ssCents[i].target = (float) (pos * spread + std::sin ((startedAt + i * 0.37) * 7.123) * drift);
        ssGain[i].target = on ? (1.0f / std::sqrt ((float) ssVoices)) * (0.9f - std::abs (pos) * 0.2f) : 0.0f;
        ssCents[i].step (c10); ssGain[i].step (c10);
        const float p = clampv (pos * width, -1.0f, 1.0f);
        const float x = (p + 1.0f) * 0.5f;
        ssPanL[i] = std::cos (x * (float) kPi * 0.5f);
        ssPanR[i] = std::sin (x * (float) kPi * 0.5f);
    }

    // stereo processing is only needed for the SuperSaw spread or a stereo sample
    bool stereoWt = false;
    for (auto& w : wt) if (w.loaded && w.wav->numChannels > 1 && w.level.target > 0) stereoWt = true;
    if (! stereo && ((lSS.target > 0 && width > 0.001f) || stereoWt))
    {
        stereo = true;
        filter.copyChannel (0, 1);
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
        const Snapshot* sp = &s;
        if (routed)
        {
            computeSources (s, mc.in, lastDt);
            modSnap = s;
            applyRoutes (*mc.routes, s.v, modSnap.v, srcV, rstate, lastDt, RF_All, liveOut);
            sp = &modSnap;
        }
        const Snapshot& ps = *sp;
        lastDt = (float) (n * isr);

        ModState mod;
        computeMod (ps, mod);
        updateControl (ps, wavs, mod, false);
        modOut = mod;

        // ---- audio-rate routes: an oscillator's raw output driving a destination per sample
        struct ARoute { int ad, src, curve; float k; bool uni; };
        ARoute ar[kNumRoutes]; int nar = 0;
        bool arCut = false, arFmSlot = false;
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
                ar[nar++] = { ad, r.src - MS_Osc1Audio, r.curve, k, r.unipolar };
                arCut |= ad == AD_Cut || ad == AD_Res;
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
        const bool needComplex = lC.v > 1.0e-6f || lC.target > 0 || usedAsSource[4];
        const bool needSS = lSS.v > 1.0e-6f || lSS.target > 0 || usedAsSource[5];
        const bool legacyRing = ringMix.v > 1.0e-6f || ringMix.target > 0;

        double ssFreq[9];
        for (int i = 0; i < 9; ++i) ssFreq[i] = ssBase.v * pow2 (ssCents[i].v / 1200.0);
        const int ssCount = needSS ? 9 : 0;

        // follower coefficients (per sample)
        const float fA = 1.0f - std::exp (-1.0f / (0.005f * (float) sr)), fR = 1.0f - std::exp (-1.0f / (0.12f * (float) sr));
        const float tfA = 1.0f - std::exp (-1.0f / (0.001f * (float) sr)), tfR = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        const float tsA = 1.0f - std::exp (-1.0f / (0.03f * (float) sr)), tsR = 1.0f - std::exp (-1.0f / (0.25f * (float) sr));

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
                    filter.update (clampv (cutoff.v * (float) pow2 (ad[AD_Cut]), 20.0f, 18000.0f), clampv (res.v + ad[AD_Res], 0.1f, 30.0f), sr);
            }
            auto pm = [&] (int a) { return ! ARType::value ? 1.0 : pow2 (ad[a]); };

            // Osc 2 and 3 first so Osc 1's legacy FM uses this sample's Osc 2 (as in Web Audio)
            double fr = ! ARType::value ? (double) (f2.v + fmIn (1)) : f2.v * pm (AD_P2) + fmIn (1);
            const float o2 = bank.sample (wave2, ph2, (float) std::abs (fr) * fisr);
            ph2 = wrap01 (ph2 + fr * isr); srcVals[1] = o2;

            fr = ! ARType::value ? (double) (f3.v + fmIn (2)) : f3.v * pm (AD_P3) + fmIn (2);
            const float o3 = bank.sample (wave3, ph3, (float) std::abs (fr) * fisr);
            ph3 = wrap01 (ph3 + fr * isr); srcVals[2] = o3;

            const double fsub = ! ARType::value ? (double) fSub.v : fSub.v * pm (AD_PSub);
            const float osub = bank.sample (waveSub, phSub, (float) fsub * fisr);
            phSub = wrap01 (phSub + fsub * isr);

            // Osc 4 (WT 1) and WT 2: sample playback
            float o4L, o4R, w2L, w2R;
            if (! ARType::value)
            {
                wt[0].tick (fmIn (3), isr, o4L, o4R);
                wt[1].tick (0.0, isr, w2L, w2R);
            }
            else
            {
                wt[0].tick (fmIn (3) + wt[0].rate.v * (pm (AD_PWt1) - 1.0), isr, o4L, o4R);
                wt[1].tick (wt[1].rate.v * (pm (AD_PWt2) - 1.0), isr, w2L, w2R);
            }
            const float o4m = 0.5f * (o4L + o4R);
            srcVals[3] = o4m;

            // Osc 5: complex oscillator (B frequency-modulates A, blended, then wavefolded)
            float cOut = 0;
            if (needComplex)
            {
                const double cb = ! ARType::value ? (double) cBase.v : cBase.v * pm (AD_PCx);
                const double mf = cb * cRatio.v;
                const float om = bank.sample (waveB, phMod, (float) std::abs (mf) * fisr);
                phMod = wrap01 (phMod + mf * isr);
                const double cf = ! ARType::value ? (double) (cBase.v + cFm.v * om + fmIn (4))
                                            : cb + std::max (0.0f, cFm.v + ad[AD_CxFm]) * om + fmIn (4);
                const float oc = bank.sample (waveA, phCar, (float) std::abs (cf) * fisr);
                phCar = wrap01 (phCar + cf * isr);
                const float shapeK = ! ARType::value ? cShapeK : clampv (cShapeK + ad[AD_CxShape], 1.0f, 25.0f);
                cOut = driveShape (oc * cMixS.v + om * (1.0f - cMixS.v), shapeK) * lvl (lC.v, AD_LCx);
            }
            srcVals[4] = cOut;

            // Osc 6: SuperSaw
            float ssMono = 0, ssL = 0, ssR = 0;
            if (ssCount > 0)
            {
                const float fmS6 = fmIn (5);
                const double ssm = pm (AD_PSs);
                for (int u = 0; u < 9; ++u)
                {
                    const float g = ssGain[u].v;
                    if (g <= 1.0e-7f) continue;
                    const double f = (! ARType::value ? ssFreq[u] : ssFreq[u] * ssm) + fmS6;
                    const float sw = bank.sample (W_SAW, ssPh[u], (float) std::abs (f) * fisr) * g;
                    ssPh[u] = wrap01 (ssPh[u] + f * isr);
                    ssMono += sw; ssL += sw * ssPanL[u]; ssR += sw * ssPanR[u];
                }
                const float gss = lvl (lSS.v, AD_LSs);
                ssMono *= gss; ssL *= gss; ssR *= gss;
            }
            srcVals[5] = ssMono;

            // Osc 1 (with the legacy Osc2 -> Osc1 FM)
            fr = ! ARType::value ? (double) (f1.v + legacyFm.v * o2 + fmIn (0))
                          : f1.v * pm (AD_P1) + std::max (0.0f, legacyFm.v + ad[AD_LegFm]) * o2 + fmIn (0);
            const float o1 = bank.sample (wave1, ph1, (float) std::abs (fr) * fisr);
            ph1 = wrap01 (ph1 + fr * isr); srcVals[0] = o1;
            aPrev[0] = o1; aPrev[1] = o2; aPrev[2] = o3; aPrev[3] = osub;

            // mixer
            const float g1 = lvl (l1.v, AD_L1), g2 = lvl (l2.v, AD_L2), g3 = lvl (l3.v, AD_L3), gs = lvl (lSub.v, AD_LSub);
            const float o4g = lvl (wt[0].level.v, AD_LWt1) * wt[0].comp;
            const float w2g = lvl (wt[1].level.v, AD_LWt2) * wt[1].comp;
            const float mono = o1 * g1 + o2 * g2 + o3 * g3 + osub * gs + cOut;
            const float mL = mono + o4L * o4g + w2L * w2g + ssL;
            const float mR = mono + o4R * o4g + w2R * w2g + ssR;

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
            const float xL = mL * dry.v + rL * rmix;
            const float xR = mR * dry.v + rR * rmix;
            const float yL = filter.process (xL, 0, drive);
            const float yR = stereo ? filter.process (xR, 1, drive) : yL;

            const float g = srcMute.v * (float) ampEnv.eval (t) * vGain.v;
            L[done + i] += yL * g;
            R[done + i] += yR * g;
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
        };
        if (nar > 0) sampleLoop (std::true_type {}); else sampleLoop (std::false_type {});
        done += n;
        advanceLfos (ps, n);

        if (ampEnv.finished (t)) active = false;
    }
}

//==============================================================================
void FxBus::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = std::max (32, block);
    for (int c = 0; c < 2; ++c)
    {
        dl[c].prepare ((int) (sr * 1.4));
        ch1[c].prepare ((int) (sr * 0.06));
        ch2[c].prepare ((int) (sr * 0.06));
        preDl[c].prepare ((int) (sr * 2.1));
        pitchDl[c].prepare ((int) (sr * 0.09));
    }
    convBuf.setSize (2, maxBlock);
    conv.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    irSeconds = 0.0;
    reset();
}

void FxBus::reset()
{
    for (int c = 0; c < 2; ++c) { dl[c].reset(); ch1[c].reset(); ch2[c].reset(); preDl[c].reset(); pitchDl[c].reset(); }
    tapeLP.reset(); tapeHP.reset(); revBP.reset(); revLP.reset(); shimHP.reset();
    warmLow.reset(); warmHigh.reset(); lastWarm = -1.0f;
    conv.reset();
}

void FxBus::updateImpulse (double seconds)
{
    if (std::abs (seconds - irSeconds.load()) < 0.001) return;
    const int len = std::max (1, (int) std::floor (sr * seconds));
    juce::AudioBuffer<float> ir (2, len);
    juce::Random rng (0x7a11);
    double power = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        float* d = ir.getWritePointer (c);
        for (int i = 0; i < len; ++i)
        {
            const double decay = std::pow (1.0 - (double) i / len, 2.8);
            const double early = i < sr * 0.03 ? 1.4 : 1.0;
            d[i] = (float) ((rng.nextFloat() * 2.0 - 1.0) * decay * early);
            power += (double) d[i] * d[i];
        }
    }
    // ConvolverNode's default normalisation (Chromium's calculateNormalizationScale)
    power = std::sqrt (power / (2.0 * len));
    if (! std::isfinite (power) || power < 0.000125) power = 0.000125;
    const float scale = (float) ((1.0 / power) * std::pow (10.0, -58.0 * 0.05) * (44100.0 / sr));
    ir.applyGain (scale);
    conv.loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::yes,
                              juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    irSeconds = seconds;
}

void FxBus::process (float* L, float* R, int n, const Snapshot& s, const ModState& mod)
{
    const double tempo = s.fxTempo;
    const float cs = smoothCoef (1.0 / sr, 0.02);

    // --- targets
    const float masterT = s.f (P_masterVolume);
    const float warm = s.f (P_warmth);
    if (warm != lastWarm)
    {
        lastWarm = warm;
        warmLow.setShelf (true, 120.0, 6.0 * warm, sr);
        warmHigh.setShelf (false, 8000.0, -3.0 * warm, sr);
    }
    const bool doWarm = warm > 0.001f;

    const float dSend = hardMuted (s.f (P_delayMix), 0, 1);
    const float dWet = hardMuted (s.f (P_delayMix), mod[MT_delayMix], 1);
    const double dBase = clampv (syncSeconds (kListDelaySync, s.i (P_delaySync), tempo, s.f (P_delayTime)), 0.01, 1.2);
    const float dTimeT = (float) clampv (dBase + mod[MT_delayTime], 0.001, 1.2);
    const float dFbT = clampv (s.f (P_delayFeedback) + mod[MT_delayFeedback], 0.0f, 0.95f);
    const float tone = clampv (s.f (P_tapeTone) + mod[MT_tapeTone], 200.0f, 18000.0f);
    tapeLP.set (Biquad::LP, tone, 1.0, sr);
    tapeHP.set (Biquad::HP, std::max (30.0f, tone * 0.08f), 1.0, sr);
    const float flutter = clampv (s.f (P_tapeFlutter) + mod[MT_tapeFlutter], 0.0f, 0.03f);
    const double flBase = syncRate (kListFlutterSync, s.i (P_flutterSync), tempo, 0.18 + flutter * 12.0);
    const double wowInc = flBase / sr, flutInc = std::max (flBase * 8.0, 0.1) / sr;

    const float cSend = hardMuted (s.f (P_chorusMix), 0, 1);
    const float cWet = hardMuted (s.f (P_chorusMix), mod[MT_chorusMix], 1);
    const double chRate = syncRate (kListModSync, s.i (P_chorusSync), tempo,
                                    clampv (s.f (P_chorusRate) + mod[MT_chorusRate], 0.01f, 12.0f));
    const double chInc1 = chRate / sr, chInc2 = std::max (chRate * 0.83, 0.01) / sr;
    const float cDepthT = clampv (s.f (P_chorusDepth) + mod[MT_chorusDepth], 0.0f, 0.03f);

    const float rSend = hardMuted (s.f (P_reverbMix), 0, 1);
    const float rWet = hardMuted (s.f (P_reverbMix), mod[MT_reverbMix], 1);
    revLP.set (Biquad::LP, clampv (s.f (P_reverbTone) + mod[MT_reverbTone], 300.0f, 18000.0f), 1.0, sr);
    const float sSend = hardMuted (s.f (P_shimmerMix), 0, 1);
    const float sWet = hardMuted (s.f (P_shimmerMix), mod[MT_shimmerMix], 1);
    shimHP.set (Biquad::HP, clampv (s.f (P_shimmerBright) + mod[MT_shimmerBright], 300.0f, 18000.0f), 1.0, sr);

    const float vSend = hardMuted (s.f (P_reverseMix), 0, 1);
    const float vWet = hardMuted (s.f (P_reverseMix), mod[MT_reverseMix], 1);
    const float vPreT = (float) clampv (syncSeconds (kListModSync, s.i (P_reverseSync), tempo,
                                                     s.f (P_reverseTime) + mod[MT_reverseTime]), 0.01, 2.0);
    const float vPitchT = clampv (s.f (P_reversePitch) + mod[MT_reversePitch], 0.0f, 0.08f);
    const double revRate = std::max (syncRate (kListModSync, s.i (P_reverseSync), tempo, 0.12 + vPitchT * 18.0), 0.05);
    const double revInc = revRate / sr;
    revBP.set (Biquad::BP, std::min ((double) (1200.0f + vPitchT * 180000.0f), sr * 0.49), 1.0, sr);

    const bool convActive = rSend > 0 || sSend > 0 || vSend > 0 || rWetS > 1.0e-5f || sWetS > 1.0e-5f || vWetS > 1.0e-5f;
    float* cl = convBuf.getWritePointer (0);
    float* cr = convBuf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        masterS += (masterT - masterS) * cs;
        dSendS += (dSend - dSendS) * cs; dWetS += (dWet - dWetS) * cs;
        dTimeS += (dTimeT - dTimeS) * cs; dFbS += (dFbT - dFbS) * cs;
        wowDepthS += (flutter * 0.75f - wowDepthS) * cs; flDepthS += (flutter * 0.25f - flDepthS) * cs;
        cSendS += (cSend - cSendS) * cs; cWetS += (cWet - cWetS) * cs; cDepthS += (cDepthT - cDepthS) * cs;
        rSendS += (rSend - rSendS) * cs; rWetS += (rWet - rWetS) * cs;
        sSendS += (sSend - sSendS) * cs; sWetS += (sWet - sWetS) * cs;
        vSendS += (vSend - vSendS) * cs; vWetS += (vWet - vWetS) * cs;
        vPreS += (vPreT - vPreS) * cs; vPitchS += (vPitchT - vPitchS) * cs;

        // modulation sources
        const double wow = std::sin (2.0 * kPi * wowPh), flt = std::sin (2.0 * kPi * flutPh);
        wowPh = wrap01 (wowPh + wowInc); flutPh = wrap01 (flutPh + flutInc);
        const double c1 = std::sin (2.0 * kPi * chPh1), c2 = std::sin (2.0 * kPi * chPh2);
        chPh1 = wrap01 (chPh1 + chInc1); chPh2 = wrap01 (chPh2 + chInc2);
        const double rv = std::sin (2.0 * kPi * revPh);
        revPh = wrap01 (revPh + revInc);

        const double dTime = clampv ((double) dTimeS + wow * wowDepthS + flt * flDepthS, 0.0, 1.2);
        const double dSamp = std::max (0.0, dTime * sr - 1.0);
        const double cd1 = clampv (0.008 + c1 * cDepthS, 0.0, 0.05) * sr;
        const double cd2 = clampv (0.012 + c2 * cDepthS * 1.18, 0.0, 0.05) * sr;
        const double pre = std::max (0.0, vPreS * sr - 1.0);
        const double pd = clampv (0.03 + rv * vPitchS, 0.0, 0.08) * sr;

        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            float x = io[c][i] * masterS;
            if (doWarm) x = warmHigh.process (warmLow.process (x, c), c);

            // tape / BBD delay: tone filters sit inside the feedback loop
            const float y = dl[c].read (dSamp);
            float v = tapeLP.process (x * dSendS + y * dFbS, c);
            v = tapeHP.process (v, c);
            dl[c].push (v);
            float out = x + y * dWetS;

            // Juno-style chorus: two modulated short delays
            const float cin = x * cSendS;
            ch1[c].push (cin); ch2[c].push (cin);
            out += (ch1[c].read (cd1) + ch2[c].read (cd2)) * cWetS;

            if (convActive)
            {
                // all three reverbs share the same impulse, so their inputs are summed
                // (weighted by their return levels) into one convolution
                const float rIn = revLP.process (x * rSendS, c);
                const float sx = clampv (x * sSendS, -1.0f, 1.0f);
                const float sIn = shimHP.process ((sx < 0 ? -1.0f : 1.0f) * std::pow (std::abs (sx), 0.35f), c);
                preDl[c].push (x * vSendS);
                pitchDl[c].push (preDl[c].read (pre));
                const float vIn = revBP.process (pitchDl[c].read (pd), c);
                (c == 0 ? cl : cr)[i] = rIn * rWetS + sIn * sWetS + vIn * vWetS;
            }
            io[c][i] = out;
        }
    }

    if (convActive)
    {
        juce::dsp::AudioBlock<float> block (convBuf.getArrayOfWritePointers(), 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
        for (int i = 0; i < n; ++i) { L[i] += cl[i]; R[i] += cr[i]; }
    }
}

//==============================================================================
void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& v : voices) v.prepare (sr);
    fxBus.prepare (sr, maxBlock);
    globalMod.clear();
    normTable();   // build the conversion tables off the audio thread
    globalRouteState.reset();
    lastFreq = -1.0;
}

void Engine::reset()
{
    for (auto& v : voices) v.kill();
    fxBus.reset();
    globalMod.clear();
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

    // polyphony: release the oldest held voices (they keep their release tail)
    const int poly = clampv (s.i (P_polyphony), 1, 16);
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
    if (routeStore != nullptr)
    {
        routeSet.build (*routeStore);
        mc.routes = &routeSet; mc.in = modIn;
    }
    slot->start (key, freq, glide, o, s, ++orderCounter, mc);
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

void Engine::render (float* L, float* R, int numSamples, const Snapshot& s, const WaveSample* const* wavs)
{
    Voice* newest = nullptr;
    for (auto& v : voices) if (v.active && (! newest || v.order > newest->order)) newest = &v;

    ModContext mc;
    if (routeStore != nullptr)
    {
        routeSet.build (*routeStore);
        mc.routes = &routeSet; mc.in = modIn;
    }
    std::fill (std::begin (liveScratch), std::end (liveScratch), 0.0f);

    ModState scratch;
    for (auto& v : voices)
        if (v.active)
            v.render (L, R, numSamples, s, wavs, &v == newest ? globalMod : scratch, mc, &v == newest ? liveScratch : nullptr);

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
        }
    }
    if (mc.any() && routeSet.anyGlobal)
    {
        fxSnap = s;
        applyRoutes (routeSet, s.v, fxSnap.v, src, globalRouteState, (float) (numSamples / sr), RF_Global, liveScratch);
        fs = &fxSnap;
    }
    fxBus.process (L, R, numSamples, *fs, globalMod);

    for (int i = 0; i < P_COUNT; ++i) liveOffset[(size_t) i].store (liveScratch[i], std::memory_order_relaxed);
    for (int i = 0; i < MS_COUNT; ++i) liveSrc[(size_t) i].store (src[i], std::memory_order_relaxed);
}

} // namespace tg
