#include "FxBus.h"
#include "../Filter/Filter.h"   // softKnee

namespace tg
{

// p - floor(p), with the common cases done without floor: the same operations on the same values, so bit for bit
// the same result (p + 0.0 turns -0 into +0, as p - floor(p) does)
static inline double wrap01 (double p)
{
    if (p >= 0.0 && p < 1.0) return p + 0.0;
    if (p >= 1.0 && p < 2.0) return p - 1.0;
    if (p >= -1.0 && p < 0.0) return p - (-1.0);
    p -= std::floor (p); return p;
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
    for (int c = 0; c < 2; ++c) { dryBuf[c].assign ((size_t) maxBlock + 16, 0.0f); runBuf[c].assign ((size_t) maxBlock + 16, 0.0f); }
    multiband.prepare (sr); sampler.prepare (sr); stutter.prepare (sr); flanger.prepare (sr); stereo.prepare (sr);
    dub.prepare (sr); spring.prepare (sr); gated.prepare (sr); plate.prepare (sr); widen.prepare (sr); dim.prepare (sr); irRev.prepare (sr, maxBlock);
    for (auto& d : duckBuf) d.assign ((size_t) maxBlock + 16, 1.0f);
    scHitBuf.assign ((size_t) maxBlock + 16, 0); hitBuf.assign ((size_t) maxBlock + 16, 0);
    for (auto& b : frzBuf) b.assign ((size_t) (sr * 2.0) + 16, 0.0f);
    frzGain.assign ((size_t) maxBlock + 16, 0.0f);
    irSeconds = 0.0;
    reset();
}

void FxBus::reset()
{
    for (int c = 0; c < 2; ++c) { dl[c].reset(); ch1[c].reset(); ch2[c].reset(); preDl[c].reset(); pitchDl[c].reset(); }
    tapeLP.reset(); tapeHP.reset(); revBP.reset(); revLP.reset(); shimHP.reset();
    warmLow.reset(); warmHigh.reset(); lastWarm = -1.0f;
    conv.reset();
    multiband.reset(); sampler.reset(); stutter.reset(); flanger.reset(); shaper.reset(); stereo.reset();
    dub.reset(); spring.reset(); gated.reset(); plate.reset(); widen.reset(); dim.reset(); irRev.reset();
    for (auto& d : duckers) d.reset();
    duckSc.reset(); pendingHits = 0;
    for (auto& b : frzBuf) std::fill (b.begin(), b.end(), 0.0f);
    frzW = 0; frzS = 0.0f; frzRng = 0xF2EE2Eu; frzG[0] = {}; frzG[1] = {};
    for (int k = 0; k < FS_COUNT; ++k) slotS[k] = 0.0f;
    slotsReady = false;
    orderKey = 0; duck = 1.0f; reordering = false;
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

// The 90s reverb's tail: recorded while it plays; while frozen, played back from the recording as two overlapping grains
// (0.3 s, sine windows, from random places in the last 1.9 s), crossfaded with the live reverb
void FxBus::freezeTail (float* cl, float* cr, int n, bool freezing)
{
    const int size = (int) frzBuf[0].size();
    if (! freezing)
    {
        for (int i = 0; i < n; ++i) { frzBuf[0][(size_t) frzW] = cl[i]; frzBuf[1][(size_t) frzW] = cr[i]; if (++frzW >= size) frzW = 0; }
        return;
    }
    const int G = (int) (0.3 * sr);
    const double span = 1.9 * sr;
    for (int i = 0; i < n; ++i)
    {
        float a = 0.0f, b = 0.0f;
        for (auto& g : frzG)
        {
            if (g.age < 0) { ++g.age; continue; }
            if (g.age == 0)
            {
                frzRng ^= frzRng << 13; frzRng ^= frzRng >> 17; frzRng ^= frzRng << 5;
                const double back = (double) G + 0.02 * sr + (double) (frzRng & 0xffff) / 65536.0 * (span - G);
                g.pos = (double) frzFrozenAt - back;
                while (g.pos < 0) g.pos += size;
            }
            const float w = (float) std::sin (kPi * (double) g.age / (double) G);
            const int p = ((int) g.pos + g.age) % size;
            a += frzBuf[0][(size_t) p] * w; b += frzBuf[1][(size_t) p] * w;
            if (++g.age >= G) g.age = 0;
        }
        const float m = frzGain[(size_t) i];
        cl[i] = cl[i] * (1.0f - m) + a * m;
        cr[i] = cr[i] * (1.0f - m) + b * m;
    }
}

// The ducks for this block: each one that is on gets its gains (the hits are at the block's start; the sidechain's per sample)
void FxBus::prepareSpace (int n, const Snapshot& s)
{
    const uint32_t hits = pendingHits;
    bool needSc = false;
    for (int d = 0; d < SD_COUNT; ++d)
    {
        duckOn[d] = s.f (sxDuckParam (d, SDF_Amt)) > 0.0f;
        if (duckOn[d] && s.i (sxDuckParam (d, SDF_Src)) == 5) needSc = true;
    }
    if (s.i (P_sxGateTrig) == 6 && s.v[P_sxOnGated] > 0.5f) needSc = true;
    if (needSc && scL != nullptr)
        for (int i = 0; i < n; ++i) scHitBuf[(size_t) i] = duckSc.tick (scL[i], scR != nullptr ? scR[i] : scL[i], 0.5f, 0.0015f, 0.18f) ? 1 : 0;
    else std::fill (scHitBuf.begin(), scHitBuf.begin() + n, (char) 0);
    static const uint32_t bitFor[8] = { SH_Kick, SH_Snare, SH_Hits, SH_Main, SH_Layer, SH_Sc, SH_Notes, SH_Note };
    for (int d = 0; d < SD_COUNT; ++d)
    {
        if (! duckOn[d]) { duckShown[d].store (1.0f, std::memory_order_relaxed); continue; }
        const int src = clampv (s.i (sxDuckParam (d, SDF_Src)), 0, 7);
        bool* hb = reinterpret_cast<bool*> (hitBuf.data());
        for (int i = 0; i < n; ++i) hb[i] = src == 5 ? scHitBuf[(size_t) i] != 0 : (i == 0 && (hits & bitFor[src]) != 0);
        duckers[d].run (duckBuf[d].data(), n, hb, clampv (s.f (sxDuckParam (d, SDF_Amt)), 0.0f, 1.0f),
                        s.f (sxDuckParam (d, SDF_Att)), s.f (sxDuckParam (d, SDF_Rel)), sr);
        duckShown[d].store (duckBuf[d][(size_t) n - 1], std::memory_order_relaxed);
    }
}

void FxBus::processSends (float* L, float* R, int n, const Snapshot& s, const ModState& mod, int mask, const float* slotGain,
                          const float* const* delayIn, float* const* delayOut)
{
    const double tempo = s.fxTempo;
    const float cs = smoothCoef (1.0 / sr, 0.02);
    const bool useD = (mask & (1 << FS_Delay)) != 0, useC = (mask & (1 << FS_Chorus)) != 0, useR = (mask & (1 << FS_Reverb)) != 0;
    const float gD = slotGain[FS_Delay], gC = slotGain[FS_Chorus], gR = slotGain[FS_Reverb];

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

    // (v0.1.70) the 90s reverb's Freeze: the reverb stops hearing new sound and its recorded tail plays on as grains
    const bool freezeOn = useR && s.v[P_sxRevFreeze] > 0.5f;
    const bool freezing = freezeOn || frzS > 0.0f;
    const bool convActive = useR && (rSend > 0 || sSend > 0 || vSend > 0 || rWetS > 1.0e-5f || sWetS > 1.0e-5f || vWetS > 1.0e-5f || freezing);
    if (freezing)
    {
        const float fc = smoothCoef (1.0 / sr, 0.03);
        if (frzS == 0.0f && freezeOn) { frzFrozenAt = frzW; frzG[0] = { 0.0, 0 }; frzG[1] = { 0.0, -(int) (0.15 * sr) }; }   // (the second grain half a grain later)
        for (int i = 0; i < n; ++i)
        {
            frzS += ((freezeOn ? 1.0f : 0.0f) - frzS) * fc;
            if (! freezeOn && frzS < 1.0e-4f) frzS = 0.0f;
            frzGain[(size_t) i] = frzS;
        }
    }
    const float* dDuck = duckFor (SD_Delay);
    const float* rDuck = duckFor (SD_Reverb);
    float* cl = convBuf.getWritePointer (0);
    float* cr = convBuf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        if (useD)
        {
            dSendS += (dSend - dSendS) * cs; dWetS += (dWet - dWetS) * cs;
            dTimeS += (dTimeT - dTimeS) * cs; dFbS += (dFbT - dFbS) * cs;
            wowDepthS += (flutter * 0.75f - wowDepthS) * cs; flDepthS += (flutter * 0.25f - flDepthS) * cs;
        }
        if (useC) { cSendS += (cSend - cSendS) * cs; cWetS += (cWet - cWetS) * cs; cDepthS += (cDepthT - cDepthS) * cs; }
        if (useR)
        {
            rSendS += (rSend - rSendS) * cs; rWetS += (rWet - rWetS) * cs;
            sSendS += (sSend - sSendS) * cs; sWetS += (sWet - sWetS) * cs;
            vSendS += (vSend - vSendS) * cs; vWetS += (vWet - vWetS) * cs;
            vPreS += (vPreT - vPreS) * cs; vPitchS += (vPitchT - vPitchS) * cs;
        }

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
            const float x = io[c][i];
            float out = x;
            if (useD)
            {
                // tape / BBD delay: tone filters sit inside the feedback loop
                const float y = dl[c].read (dSamp);
                if (delayOut != nullptr) delayOut[c][i] = y;
                float v = tapeLP.process (delayIn != nullptr ? x * dSendS + y * dFbS + delayIn[c][i] : x * dSendS + y * dFbS, c);
                v = tapeHP.process (v, c);
                dl[c].push (v);
                out = gD == 1.0f ? x + y * dWetS : x + y * dWetS * gD;
                if (dDuck != nullptr) out = x + (out - x) * dDuck[i];
            }
            else if (delayOut != nullptr) delayOut[c][i] = 0.0f;

            if (useC)
            {
                // Juno-style chorus: two modulated short delays
                const float cin = x * cSendS;
                ch1[c].push (cin); ch2[c].push (cin);
                const float cw = (ch1[c].read (cd1) + ch2[c].read (cd2)) * cWetS;
                out += gC == 1.0f ? cw : cw * gC;
            }

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
                (c == 0 ? cl : cr)[i] = freezing ? (rIn * rWetS + sIn * sWetS + vIn * vWetS) * (1.0f - frzGain[(size_t) i]) : rIn * rWetS + sIn * sWetS + vIn * vWetS;
            }
            io[c][i] = out;
        }
    }

    if (convActive)
    {
        juce::dsp::AudioBlock<float> block (convBuf.getArrayOfWritePointers(), 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
        freezeTail (cl, cr, n, freezing);
        if (rDuck != nullptr) for (int i = 0; i < n; ++i) { cl[i] *= rDuck[i]; cr[i] *= rDuck[i]; }
        if (gR == 1.0f) for (int i = 0; i < n; ++i) { L[i] += cl[i]; R[i] += cr[i]; }
        else            for (int i = 0; i < n; ++i) { L[i] += cl[i] * gR; R[i] += cr[i] * gR; }
    }
}

// The rack: master volume and Warmth first, then the slots in their order. Adjacent send
// slots (delay, chorus, reverbs) share one input as they always have, so the default order
// is exactly the original effects section.
void FxBus::process (float* L, float* R, int n, const Snapshot& s, const ModState& mod,
                     const float* const* delayIn, float* const* delayOut)
{
    const float cs = smoothCoef (1.0 / sr, 0.02);
    const float masterT = s.f (P_masterVolume);
    const float warm = s.f (P_warmth);
    if (warm != lastWarm)
    {
        lastWarm = warm;
        warmLow.setShelf (true, 120.0, 6.0 * warm, sr);
        warmHigh.setShelf (false, 8000.0, -3.0 * warm, sr);
    }
    const bool doWarm = warm > 0.001f;
    for (int i = 0; i < n; ++i)
    {
        masterS += (masterT - masterS) * cs;
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c)
        {
            float x = io[c][i] * masterS;
            if (doWarm) x = warmHigh.process (warmLow.process (x, c), c);
            io[c][i] = x;
        }
    }

    // ---- order (a change dips the output for a few ms instead of clicking)
    const uint64_t key = rack != nullptr ? rack->orderKey() : 0;
    if (orderKey == 0) { orderKey = key; order = rack != nullptr ? rack->getOrder() : std::array<int, FS_COUNT> {}; if (rack == nullptr) for (int k = 0; k < FS_COUNT; ++k) order[(size_t) k] = k; }
    if (key != orderKey) reordering = true;

    // ---- slot gains: On x Mix, smoothed over the block (a slot that's fully off is skipped)
    float gain[FS_COUNT], g0[FS_COUNT];
    const float blockK = 1.0f - std::pow (1.0f - cs, (float) n);
    for (int k = 0; k < FS_COUNT; ++k)
    {
        const float t = s.f (fxOnParam (k)) > 0.5f ? clampv (s.f (fxMixParam (k)), 0.0f, 1.0f) : 0.0f;
        if (! slotsReady) slotS[k] = t;   // first block: start where the settings are
        g0[k] = slotS[k];
        slotS[k] = std::abs (t - slotS[k]) < 1.0e-4f ? t : slotS[k] + (t - slotS[k]) * blockK;
        gain[k] = slotS[k];
    }
    slotsReady = true;

    prepareSpace (n, s);
    const uint32_t spaceHits = pendingHits;
    pendingHits = 0;
    FxContext ctx;
    ctx.sr = sr; ctx.tempo = s.fxTempo; ctx.beat = s.fxBeat; ctx.beatInc = s.fxBeatInc; ctx.dnaGate = dnaGate;
    ctx.quality = s.i (P_quality);
    ctx.scL = scL; ctx.scR = scR;
    const bool stutterHeld = s.f (P_rptTrigger) > 0.5f || stutterMidi;

    int pos = 0;
    while (pos < FS_COUNT)
    {
        const int slot = order[(size_t) pos];
        if (fxSlotIsSend (slot))
        {
            int mask = 0;
            while (pos < FS_COUNT && fxSlotIsSend (order[(size_t) pos])) { const int sl = order[(size_t) pos]; if (gain[sl] > 0.0f || g0[sl] > 0.0f) mask |= 1 << sl; ++pos; }
            if (mask != 0) processSends (L, R, n, s, mod, mask, gain, delayIn, delayOut);
            else if (delayOut != nullptr) { std::fill (delayOut[0], delayOut[0] + n, 0.0f); std::fill (delayOut[1], delayOut[1] + n, 0.0f); }
            continue;
        }
        ++pos;
        const bool needed = gain[slot] > 0.0f || g0[slot] > 0.0f || (slot == FS_Stutter && stutterHeld && gain[slot] > 0.0f);
        if (! needed) continue;
        if (g0[slot] <= 0.0f)   // switched on: start from a clean state
            switch (slot)
            {
                case FS_Stutter: stutter.reset(); break;
                case FS_Sampler: sampler.reset(); break;
                case FS_Flanger: flanger.reset(); break;
                case FS_Shaper: shaper.reset(); break;
                case FS_Multiband: multiband.reset(); break;
                case FS_Stereo: stereo.reset(); break;
                case FS_Dub: dub.reset(); break;
                case FS_Spring: spring.reset(); break;
                case FS_Gated: gated.reset(); break;
                case FS_Plate: plate.reset(); break;
                case FS_Widen: widen.reset(); break;
                case FS_Dim: dim.reset(); break;
                case FS_Ir: irRev.reset(); break;
                default: break;
            }
        // insert effect: process a copy, then blend by the (ramped) slot gain
        std::copy (L, L + n, dryBuf[0].begin()); std::copy (R, R + n, dryBuf[1].begin());
        switch (slot)
        {
            case FS_Stutter:   stutter.process (L, R, n, s.v, ctx, stutterHeld); break;
            case FS_Sampler:   sampler.process (L, R, n, s.v, ctx); break;
            case FS_Flanger:   flanger.process (L, R, n, s.v, ctx); break;
            case FS_Shaper:    if (rack != nullptr) shaper.process (L, R, n, s.v, ctx, *rack); break;
            case FS_Multiband: multiband.process (L, R, n, s.v, ctx); break;
            case FS_Stereo:    stereo.process (L, R, n, s.v, ctx); break;
            case FS_Dub:       dub.process (L, R, n, s.v, ctx, s.v[P_sxDubThrow] > 0.5f || throwPad, duckFor (SD_Dub)); break;
            case FS_Spring:    spring.process (L, R, n, s.v, ctx, duckFor (SD_Spring)); break;
            case FS_Gated:     gated.process (L, R, n, s.v, ctx, spaceHits, reinterpret_cast<const bool*> (scHitBuf.data()), duckFor (SD_Gated)); break;
            case FS_Plate:     plate.process (L, R, n, s.v, ctx, duckFor (SD_Plate)); break;
            case FS_Widen:     widen.process (L, R, n, s.v, ctx); break;
            case FS_Dim:       dim.process (L, R, n, s.v, ctx); break;
            case FS_Ir:        irRev.process (L, R, n, s.v, ctx, duckFor (SD_Ir)); break;
            default: break;
        }
        const float a = g0[slot], b = gain[slot];
        if (a == 1.0f && b == 1.0f) continue;
        const float inv = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float m = a + (b - a) * (i + 1) * inv;
            L[i] = dryBuf[0][(size_t) i] + (L[i] - dryBuf[0][(size_t) i]) * m;
            R[i] = dryBuf[1][(size_t) i] + (R[i] - dryBuf[1][(size_t) i]) * m;
        }
    }

    // ---- safety: with the new effects in use, absurd levels (above +12 dBFS) are rounded off
    bool anyNew = false;
    for (int k : { FS_Stutter, FS_Sampler, FS_Flanger, FS_Shaper, FS_Multiband, FS_Stereo, FS_Dub, FS_Spring, FS_Gated, FS_Plate, FS_Widen, FS_Dim, FS_Ir }) anyNew |= gain[k] > 0.0f || g0[k] > 0.0f;
    if (anyNew)
        for (int i = 0; i < n; ++i) { L[i] = softKnee (L[i], 4.0f); R[i] = softKnee (R[i], 4.0f); }

    // ---- reorder: fade the output down (~3 ms), switch, fade back up
    if (reordering || duck < 1.0f)
    {
        const float k = smoothCoef (1.0 / sr, 0.001);
        for (int i = 0; i < n; ++i)
        {
            duck += ((reordering ? 0.0f : 1.0f) - duck) * k;
            if (! reordering && duck > 0.9999f) duck = 1.0f;
            L[i] *= duck; R[i] *= duck;
        }
        if (reordering && duck < 1.0e-3f && rack != nullptr) { order = rack->getOrder(); orderKey = key; reordering = false; }
    }
}

} // namespace tg
