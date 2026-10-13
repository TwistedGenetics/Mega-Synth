#pragma once
// The Reese engine: 2, 3 or 4 detuned band-limited saws (or squares) per note, built for jungle and
// drum & bass basses.
//
//  - Detune in Cents (as usual), in Hz (the same beating on every note) or in Beats: the beating is a
//    note value (1/16 .. 4 bars) at the song's tempo, worked out per note, so the wobble lands on the
//    beat at any pitch. Voices 3 and 4 beat at x3 or x5 that rate (still locked: every difference
//    between voices is a whole multiple of it) or at the golden ratio (free).
//  - Phase Twist: each voice's phase is pushed back and forth (slow: a swirl, fast: a growl).
//  - Width spreads the voices; Rotation moves them round the stereo field; Mono Below keeps the low end
//    in the middle (the side signal is high-passed there).
//  - A moving notch (synced to the beating, or free), a phase-locked sine sub an octave down, and a
//    soft drive with a tone control.
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <iterator>
#include "DSP.h"
#include "Params.h"

namespace tg
{

struct ReeseSettings
{
    int voices = 2, wave = 0, mode = 2, beat = 3, ratio = 0, retrig = 0;
    float cents = 14.0f, hz = 1.5f, twist = 0.15f, twistRate = 0.6f, width = 0.6f, rotate = 0.0f, rotRate = 0.15f, monoHz = 120.0f;
    float notch = 0.0f, notchFreq = 900.0f, notchMove = 0.5f, notchRate = 0.5f, sub = 0.0f, drive = 0.0f, tone = 1.0f;
    bool notchSync = true;
    int quality = 1;   // the drive is oversampled: Eco none, Normal x2, High x4
    // ---- v0.1.70 (every one off by default: the sound is then exactly as before)
    bool fmtOn = false, fmtBefore = false; int fmtMove = 0, fmtShape = 0;
    float fmtVowel = 0.0f, fmtRes = 0.5f, fmtMix = 1.0f, fmtShift = 0.0f, fmtDepth = 0.5f;
    double fmtLfoInc = 0.0;                          // (the LFOs' steps per sample; their phases are set from the song at note-on)
    float bloom = 0.0f; int bloomCurve = 0; bool bloomRev = false;   // seconds (0: off)
    bool split = false; float splitHz = 120.0f, lowDrive = 0.0f, highDrive = 0.5f, highTone = 8000.0f;
    int shp = 0; float shpAmt = 0.3f, shpSym = 0.0f;
    int cp = 0, cpStages = 8; float cpFb = 0.5f, cpDepth = 0.5f, cpOffset = 0.0f, cpMix = 0.5f;
    double cpLfoInc = 0.0;
    float pw = 0.5f, pwmRate = 0.6f, pwmDepth = 0.25f; int stack = 0;
    int pdMode = 0; float pdAmt = 12.0f, pdTime = 0.25f;
    float fm = 0.0f; bool sync = false; float syncRatio = 1.0f;   // (with the envelope already applied)
    float drift = 0.0f, driftRate = 0.5f;
    bool extras() const { return wave == 3 || stack > 0 || fm > 0.0f || sync || shp > 0 || drift > 0.0f; }
};

class ReeseOsc
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate; isr = 1.0 / sr;
        // the drive's oversampling filters: two half-band stages (48 -> 96 kHz: flat to 20 kHz, -90 dB from 28 kHz;
        // 96 -> 192 kHz: -90 dB from 76 kHz), used both ways
        hbA.design (17); hbB.design (5);
    }

    // a new note: the phases start free (as they were), together, or at random
    void start (int retrig, uint32_t seed)
    {
        if (retrig == 1) { for (auto& p : ph) p = 0.0; acc0 = 0.0; }
        else if (retrig == 2)
        {
            uint32_t r = seed * 2654435761u + 0x9E3779B9u;
            for (auto& p : ph) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; p = (double) (r & 0xffffff) / 16777216.0; }
            acc0 = ph[0];
        }
        for (auto& s : side) s = {};
        for (auto& s : notchSt) s = {};
        toneLp[0] = toneLp[1] = 0.0f;
        notchCount = 0;
        for (auto& x : upA) x.clear(); for (auto& x : upB) x.clear(); for (auto& x : dnA) x.clear(); for (auto& x : dnB) x.clear(); adaPrev[0] = adaPrev[1] = 0.0f; adaF[0] = adaF[1] = 1.0f; driveLatched = false; xfN = 0; pitchMul = 1.0; notchJump = 0.0f; panCount = 0;
        // (v0.1.70) a new note: the bloom, the pitch envelope and the drift start again (the drift from this note's own seed)
        noteSamples = 0; driftLast = 0; driftRng = seed * 2246822519u + 0x2545F491u; if (driftRng == 0) driftRng = 1;
        for (int k = 0; k < 4; ++k) { driftV[k] = driftT[k] = driftP[k] = driftPT[k] = 0.0f; ph2[k] = 0.0; slv[k] = ph[k]; }
        driftClock = 0.0;
        for (auto& f : fmtSt) for (auto& v : f) v = {};
        fmtCount = 0; for (auto& x : shpDn) x.clear();
        for (auto& c : combDl) std::fill (std::begin (c), std::end (c), 0.0f);
        combW = 0; for (auto& a : apSt) for (auto& v : a) v = 0.0f; apFb[0] = apFb[1] = 0.0f;
        for (auto& a : splitAda) a = {}; for (auto& ch : splitSt) for (auto& sv : ch) sv = {}; splitTone[0] = splitTone[1] = 0.0f;
        lastNoteFreq = 110.0;
    }

    // the voices' frequencies for this block: f = the note, tempo in BPM
    void setFrequency (double f, double tempo, const ReeseSettings& s)
    {
        // (the LFOs' phases run on here between blocks unless the engine sets them: keep ours if it didn't)
        st = s;
        n = std::clamp (s.voices + 2, 2, 4);
        // (v0.1.70) the pitch envelope (Hoover style) and the analog drift's shared part move the note itself
        const double tNote = (double) noteSamples * isr;
        if (s.pdMode > 0)
        {
            const double p = std::clamp (tNote / std::max (0.01, (double) s.pdTime), 0.0, 1.0);
            const double semis = s.pdMode == 1 ? s.pdAmt * (1.0 - p) * (1.0 - p) : (s.pdMode == 2 ? -s.pdAmt * (1.0 - p) * (1.0 - p) : -s.pdAmt * std::sin (kPi * p));
            f *= std::pow (2.0, semis / 12.0);
        }
        if (s.drift > 0.0f) updateDrift (s);
        if (s.drift > 0.0f && s.mode == 2) f *= std::pow (2.0, (double) (s.drift * 12.0f * driftV[0]) / 1200.0);   // (Beats: only the whole note drifts, so the beating stays locked)
        lastNoteFreq = f;
        // Detune Bloom: the detune opens from nothing over Bloom (or closes from 3x in Reverse)
        double bl = 1.0;
        if (s.bloom > 0.0f)
        {
            const double p = std::clamp (tNote / (double) s.bloom, 0.0, 1.0);
            const double q = s.bloomCurve == 1 ? (1.0 - std::exp (-4.0 * p)) / (1.0 - std::exp (-4.0)) : p;
            bl = s.bloomRev ? 3.0 - 2.0 * q : q;
        }
        static const double ratios[3] = { 3.0, 5.0, 1.6180339887 };
        const double r = ratios[std::clamp (s.ratio, 0, 2)];
        const double u[4] = { -1.0, 1.0, n == 3 ? r : -r, r };   // offsets, in half the beat rate
        double delta;   // the beating between voices 1 and 2, in Hz
        if (s.mode == 0)
        {
            const double c = (s.bloom > 0.0f ? std::max (0.0f, s.cents) * bl : std::max (0.0f, s.cents)) / 1200.0;
            for (int k = 0; k < n; ++k) freq[k] = f * std::pow (2.0, u[k] * c * 0.5);
            delta = freq[1] - freq[0];
        }
        else
        {
            delta = s.mode == 1 ? (double) std::max (0.0f, s.hz)
                                : std::max (20.0, tempo) / 60.0 / kRsBeatBeats[std::clamp (s.beat, 0, 12)];
            if (s.bloom > 0.0f) delta *= bl;
            for (int k = 0; k < n; ++k) freq[k] = std::max (0.0, f + u[k] * delta * 0.5);
        }
        beatHz = delta;
        if (s.drift > 0.0f && s.mode != 2)
            for (int k = 0; k < n; ++k) freq[k] *= std::pow (2.0, (double) (s.drift * 12.0f * driftV[k]) / 1200.0);   // (up to +/-12 cents each)
        // the Hoover's pulse widths: worked out per block (a line to where they'll be in 32 samples) rather than per sample
        if (s.wave == 3)
            for (int k = 0; k < 4; ++k)
            {
                const double a = 2.0 * kPi * (pwmPh + 0.25 * k), b = a + 2.0 * kPi * std::max (0.0f, s.pwmRate) * 32.0 * isr;
                pwW[k] = s.pw + s.pwmDepth * (float) std::sin (a);
                pwDW[k] = (s.pw + s.pwmDepth * (float) std::sin (b) - pwW[k]) / 32.0f;
            }
        // the split drive: its slow coefficients
        if (s.split)
        {
            splitG = (float) std::tan (kPi * std::clamp ((double) s.splitHz, 20.0, sr * 0.45) * isr);
            splitKl = 1.0f + 9.0f * std::clamp (s.lowDrive, 0.0f, 1.0f); splitKh = 1.0f + 9.0f * std::clamp (s.highDrive, 0.0f, 1.0f);
            splitToneC = 1.0f - (float) std::exp (-2.0 * kPi * std::clamp ((double) s.highTone, 200.0, 20000.0) * isr);
        }
        // (the coefficients that change slowly)
        if (s.monoHz > 1.0f) sideG = (float) std::tan (kPi * std::min ((double) s.monoHz, sr * 0.45) * isr);
        toneC = s.tone >= 0.999f ? 1.0f : 1.0f - (float) std::exp (-2.0 * kPi * std::min (20000.0, 800.0 * std::pow (2.0, 4.6 * (double) s.tone)) * isr);
        driveK = 1.0f + 9.0f * std::clamp (s.drive, 0.0f, 1.0f);
        driveN = std::sqrt (1.0f + driveK * driveK) / driveK;   // (full scale in, full scale out)
        driveMix = std::min (1.0f, 5.0f * std::max (0.0f, s.drive));
    }

    // one stereo sample
    inline void tick (float& outL, float& outR)
    {
        const float tw = (float) std::sin (2.0 * kPi * twPh);
        const float tw2 = (float) std::cos (2.0 * kPi * twPh);
        if ((panCount++ & 7) == 0)
        {
            // the voices' pans (spread by Width, moved round by Rotation; equal power), every 8 samples
            static const float basePan[3][4] = { { -1.0f, 1.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, -0.33f, 0.33f } };
            const float rot = st.rotate > 0.0f ? (float) (2.0 * kPi * rotPh) : 0.0f;
            const float norm = n == 2 ? 0.62f : (n == 3 ? 0.52f : 0.45f);
            for (int k = 0; k < n; ++k)
            {
                const float pan = std::clamp (st.width * basePan[n - 2][k] + (rot != 0.0f ? st.rotate * std::sin (rot + (float) k * 6.2831853f / (float) n) : 0.0f), -1.0f, 1.0f);
                const float a = (pan + 1.0f) * 0.7853982f;
                gL[k] = norm * std::cos (a); gR[k] = norm * std::sin (a);
            }
        }
        float l, r;
        ++noteSamples;
        // (once a note has used the drive it stays on the oversampled path, so moving the drive to and from
        //  zero does not jump between paths; the drive fades in from zero)
        if (st.drive > 0.0f && ! st.split) driveLatched = true;
        const int os = driveLatched ? (st.quality >= 2 ? 4 : (st.quality == 1 ? 2 : 1)) : 1;
        if (st.split)
        {
            // (v0.1.70) Split Drive: the lows and the highs driven apart (the highs with their own tone)
            voicesAndSub (l, r, tw, tw2, 1.0);
            if (st.fmtOn && st.fmtBefore) formants (l, r);
            splitDrive (l, r);
        }
        else if (os == 1)
        {
            voicesAndSub (l, r, tw, tw2, 1.0);
            if (st.fmtOn && st.fmtBefore) formants (l, r);
            if (st.drive > 0.0f) { l += (shape (l) - l) * driveMix; r += (shape (r) - r) * driveMix; }
        }
        else
        {
            // the drive oversampled (x2 or x4, by half-band stages) and anti-aliased (the antiderivative method):
            // the voices are made at the real rate (they have nothing above it), taken up, driven, and the drive's
            // new harmonics filtered off above 20 kHz on the way back down, so they do not fold back as aliasing
            voicesAndSub (l, r, tw, tw2, 1.0);
            if (st.fmtOn && st.fmtBefore) formants (l, r);
            float a2[2], b2[2];
            upA[0].up (hbA, l, a2); upA[1].up (hbA, r, b2);
            float da[2], db[2];
            for (int j = 0; j < 2; ++j)
            {
                if (os == 4)
                {
                    float a4[2], b4[2];
                    upB[0].up (hbB, a2[j], a4); upB[1].up (hbB, b2[j], b4);
                    for (int q = 0; q < 2; ++q) { a4[q] += (adaa (a4[q], 0) - a4[q]) * driveMix; b4[q] += (adaa (b4[q], 1) - b4[q]) * driveMix; }
                    da[j] = dnB[0].down (hbB, a4[0], a4[1]); db[j] = dnB[1].down (hbB, b4[0], b4[1]);
                }
                else { da[j] = a2[j] + (adaa (a2[j], 0) - a2[j]) * driveMix; db[j] = b2[j] + (adaa (b2[j], 1) - b2[j]) * driveMix; }
            }
            l = dnA[0].down (hbA, da[0], da[1]); r = dnA[1].down (hbA, db[0], db[1]);
        }
        // tone
        if (toneC < 1.0f) { toneLp[0] += (l - toneLp[0]) * toneC; toneLp[1] += (r - toneLp[1]) * toneC; l = toneLp[0]; r = toneLp[1]; }
        // (v0.1.70) the formants (after the drive) and the comb / phaser
        if (st.fmtOn && ! st.fmtBefore) formants (l, r);
        if (st.cp > 0) combPhaser (l, r);
        // the moving notch
        if (st.notch > 0.0f)
        {
            if ((notchCount++ & 15) == 0)
            {
                const double mp = st.notchSync ? beatPh : notchPh;
                const double fc = std::clamp ((double) st.notchFreq * std::pow (2.0, 2.0 * st.notchMove * std::sin (2.0 * kPi * mp) + (double) notchJump), 30.0, sr * 0.45);
                notchG = (float) std::tan (kPi * fc * isr);
            }
            const float k = 0.8f;   // (fairly wide)
            float* io[2] = { &l, &r };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = notchSt[c];
                const float a1 = 1.0f / (1.0f + notchG * (notchG + k)), a2 = notchG * a1, a3 = notchG * a2;
                const float x = *io[c];
                const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
                const float notched = x - k * v1;
                *io[c] = x + (notched - x) * st.notch;
            }
        }
        // mono below: the side signal high-passed (4-pole Butterworth), so the low end is the same both sides
        if (st.monoHz > 1.0f)
        {
            const float mid = 0.5f * (l + r);
            float sd = 0.5f * (l - r);
            static const float ks[2] = { 1.8477591f, 0.7653669f };
            for (int j = 0; j < 2; ++j)
            {
                auto& s = side[j];
                const float k = ks[j], a1 = 1.0f / (1.0f + sideG * (sideG + k)), a2 = sideG * a1, a3 = sideG * a2;
                const float v3 = sd - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
                sd = sd - k * v1 - v2;
            }
            l = mid + sd; r = mid - sd;
        }
        // the slow clocks
        beatPh += beatHz * isr; beatPh -= std::floor (beatPh);
        twPh += std::max (0.0f, st.twistRate) * isr; twPh -= std::floor (twPh);
        rotPh += std::max (0.0f, st.rotRate) * isr; rotPh -= std::floor (rotPh);
        notchPh += std::max (0.0f, st.notchRate) * isr; notchPh -= std::floor (notchPh);
        if (st.fmtOn) { fmtPh += st.fmtLfoInc; if (fmtPh >= 1.0) fmtPh -= std::floor (fmtPh); }
        if (st.cp > 0) { cpPh += st.cpLfoInc; if (cpPh >= 1.0) cpPh -= std::floor (cpPh); }
        if (st.wave == 3) { pwmPh += std::max (0.0f, st.pwmRate) * isr; pwmPh -= std::floor (pwmPh); }
        outL = l; outR = r;
    }

    // ---- Break-Reactive mode
    // every voice (and the sub) back to the start of its cycle now, the old phases faded out over xfade samples
    void resetPhases (int xfade)
    {
        std::copy (std::begin (ph), std::end (ph), std::begin (oldPh)); oldAcc = acc0;
        for (auto& p : ph) p = 0.0;
        acc0 = 0.0; beatPh = 0.0;
        xfLen = std::max (1, xfade); xfN = xfLen;
    }
    // (v0.1.70) the formant and comb / phaser LFOs start where the song is (cycle lengths in beats; 0: free running from 0)
    void syncLfos (double beat, double fmtBeats, double cpBeats)
    {
        fmtPh = fmtBeats > 0.0 ? beat / fmtBeats - std::floor (beat / fmtBeats) : 0.0;
        cpPh = cpBeats > 0.0 ? beat / cpBeats - std::floor (beat / cpBeats) : 0.0;
        pwmPh = 0.0;
    }
    void setPitchMul (double m) { pitchMul = m; }          // a pitch kick (1 = none)
    void setNotchJump (float octaves) { notchJump = octaves; }   // the notch moved up (read every 16 samples)
    double beatPhase() const { return beatPh; }     // where the beat cycle is (0..1): the "Reese Phase" Mod Matrix source
    double beatRate() const { return beatHz; }      // the beating between voices 1 and 2, in Hz
    double voiceFrequency (int k) const { return freq[std::clamp (k, 0, 3)]; }
    int voiceCount() const { return n; }

private:
    // the voices (and the sub) for one sample at the rate sr / step
    inline void voicesAndSub (float& outL, float& outR, float tw, float tw2, double step)
    {
        if (st.extras())
        {
            // (v0.1.70) Hoover / octave stack / cross-FM / sync / shaper / drift; the shaper oversampled x2 at High
            if (st.shp > 0 && st.quality >= 2)
            {
                float a0, b0, a1, b1;
                sumVoicesX (a0, b0, tw, tw2, 0.5); sumVoicesX (a1, b1, tw, tw2, 0.5);
                outL = shpDn[0].down (hbA, a0, a1); outR = shpDn[1].down (hbA, b0, b1);
            }
            else sumVoicesX (outL, outR, tw, tw2, 1.0);
            return;
        }
        sumVoices (ph, acc0, outL, outR, tw, tw2, step);
        if (xfN > 0)
        {
            // just after a phase reset (Break-Reactive mode): the old phases fade out over a moment, so it doesn't click
            float a, b; sumVoices (oldPh, oldAcc, a, b, tw, tw2, step);
            const float w = (float) xfN / (float) xfLen;
            outL += (a - outL) * w; outR += (b - outR) * w;
            --xfN;
        }
    }
    inline void sumVoices (double* phs, double& acc, float& outL, float& outR, float tw, float tw2, double step)
    {
        const WaveBank& bank = WaveBank::get();
        const float twistAmt = 0.25f * std::clamp (st.twist, 0.0f, 1.0f);
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            const double f = freq[k] * pitchMul;
            const float fos = (float) (f * isr);   // (the table for the real rate: nothing above its Nyquist)
            // phase twist: voices pushed in alternate directions (k even: sine, odd: cosine)
            const float off = twistAmt * ((k & 1) ? tw2 : tw) * (k < 2 ? 1.0f : -1.0f);
            double p = phs[k] + off; p -= std::floor (p);
            float x;
            switch (st.wave)
            {
                case 1:  x = bank.sample (W_SQUARE, p, fos); break;
                case 2:  x = 0.5f * (bank.sample (W_SAW, p, fos) + bank.sample (W_SQUARE, p, fos)); break;
                default: x = bank.sample (W_SAW, p, fos); break;
            }
            l += x * gL[k]; r += x * gR[k];
            phs[k] += f * isr * step; if (phs[k] >= 1.0) phs[k] -= std::floor (phs[k]);
        }
        // the sub: a sine an octave down, locked to voice 1 (its phase runs over two of voice 1's cycles)
        acc += freq[0] * pitchMul * isr * step; if (acc >= 2.0) acc -= 2.0;
        if (st.sub > 0.0f) { const float sb = st.sub * 0.8f * (float) std::sin (kPi * acc); l += sb; r += sb; }
        outL = l; outR = r;
    }
    // ---- v0.1.70
    // the extended voices: Hoover (PWM) wave, octave stack, cross-FM from voice 1, hard sync (each voice to its own
    // pitch, so the detune still beats), the per-voice shaper and the drift's phase wander
    inline void sumVoicesX (float& outL, float& outR, float tw, float tw2, double step)
    {
        const WaveBank& bank = WaveBank::get();
        const float twistAmt = 0.25f * std::clamp (st.twist, 0.0f, 1.0f);
        float l = 0.0f, r = 0.0f, x0 = 0.0f;
        const float fmAmt = std::clamp (st.fm, 0.0f, 1.0f) * 0.35f;
        const double ratio = st.sync ? std::clamp ((double) st.syncRatio, 1.0, 16.0) : 1.0;
        for (int k = 0; k < n; ++k)
        {
            const double f = freq[k] * pitchMul;
            const double inc = f * isr * step;
            const float fos = (float) inc * (float) ratio;
            const float off = twistAmt * ((k & 1) ? tw2 : tw) * (k < 2 ? 1.0f : -1.0f) + driftP[k];
            double p = (st.sync ? slv[k] : ph[k]) + off;
            if (k > 0 && fmAmt > 0.0f) p += fmAmt * x0;   // (cross-FM: voice 1 moves the others' phase)
            p -= std::floor (p);
            float x;
            switch (st.wave)
            {
                case 1:  x = bank.sample (W_SQUARE, p, fos); break;
                case 2:  x = 0.5f * (bank.sample (W_SAW, p, fos) + bank.sample (W_SQUARE, p, fos)); break;
                case 3:
                {
                    // Hoover: a pulse made of two saws (its width swept by the PWM), with a saw underneath
                    const float w = std::clamp (pwW[k], 0.03f, 0.97f);
                    pwW[k] += pwDW[k] * (float) step;
                    double q = p + w; q -= std::floor (q);
                    const float sa = bank.sample (W_SAW, p, fos);
                    x = 0.55f * (sa - bank.sample (W_SAW, q, fos)) + 0.45f * sa;
                    break;
                }
                default: x = bank.sample (W_SAW, p, fos); break;
            }
            if (st.stack > 0)
            {
                double pd = ph2[k] + off; pd -= std::floor (pd);
                x += 0.6f * bank.sample (W_SAW, pd, fos * 0.5f);
                if (st.stack == 2) { double pu = 2.0 * p; pu -= std::floor (pu); x += 0.35f * bank.sample (W_SAW, pu, fos * 2.0f); }
                x *= 0.7f;
                ph2[k] += inc * 0.5; if (ph2[k] >= 1.0) ph2[k] -= std::floor (ph2[k]);
            }
            if (st.shp > 0) x = shaper (x);
            if (k == 0) x0 = x;
            l += x * gL[k]; r += x * gR[k];
            ph[k] += inc;
            if (st.sync)
            {
                slv[k] += inc * ratio;
                if (ph[k] >= 1.0) slv[k] = (ph[k] - std::floor (ph[k])) * ratio;   // (the voice's own cycle restarts its synced wave)
                if (slv[k] >= 1.0) slv[k] -= std::floor (slv[k]);
            }
            if (ph[k] >= 1.0) ph[k] -= std::floor (ph[k]);
        }
        acc0 += freq[0] * pitchMul * isr * step; if (acc0 >= 2.0) acc0 -= 2.0;
        if (st.sub > 0.0f) { const float sb = st.sub * 0.8f * (float) std::sin (kPi * acc0); l += sb; r += sb; }
        outL = l; outR = r;
    }
    // the per-voice shaper: Fold, Tanh, Asym or Bitwise (Amount: how hard; Symmetry: a bias, so even harmonics too)
    inline float shaper (float x) const
    {
        const float a = std::clamp (st.shpAmt, 0.0f, 1.0f), sym = std::clamp (st.shpSym, -1.0f, 1.0f);
        switch (st.shp)
        {
            case 1:
            {
                const float u = x * (1.0f + 5.0f * a) + 0.5f * sym;
                float t = std::fmod (u + 1.0f, 4.0f); if (t < 0.0f) t += 4.0f;
                return 0.8f * (t < 2.0f ? t - 1.0f : 3.0f - t);
            }
            case 2: { const float k = 1.0f + 6.0f * a, b = 0.3f * sym; return 0.8f * (std::tanh (k * (x + b)) - std::tanh (k * b)); }
            case 3: { const float k = 1.0f + 6.0f * a; return 0.8f * (x >= 0.0f ? std::tanh (k * x) : std::tanh (k * (0.35f + 0.35f * sym) * x) * (0.6f - 0.3f * sym)); }
            case 4:
            {
                const float q = std::pow (2.0f, 7.0f - 5.0f * a);
                const float y = std::round ((x + 0.05f * sym) * q) / q;
                return 0.8f * std::clamp (y, -1.0f, 1.0f);
            }
            default: return x;
        }
    }
    // the drift: each voice's own slow random walk (seeded by the note), in pitch and a little phase
    void updateDrift (const ReeseSettings& s)
    {
        const double blockT = (double) (noteSamples - driftLast) * isr;   // (the time since the last block)
        driftLast = noteSamples;
        const double rate = std::max (0.05f, s.driftRate);
        driftClock += blockT * rate;
        auto rnd = [this] { driftRng ^= driftRng << 13; driftRng ^= driftRng >> 17; driftRng ^= driftRng << 5; return (float) (driftRng & 0xffffff) / 8388608.0f - 1.0f; };
        if (driftClock >= 1.0) { driftClock -= std::floor (driftClock); for (int k = 0; k < 4; ++k) { driftT[k] = rnd(); driftPT[k] = rnd(); } }
        const float c = (float) std::min (1.0, blockT * rate * 2.0);
        for (int k = 0; k < 4; ++k)
        {
            driftV[k] += (driftT[k] - driftV[k]) * c;
            driftP[k] = s.mode == 2 ? 0.0f : driftP[k] + (driftPT[k] * 0.015f * s.drift - driftP[k]) * c;
        }
    }
    // the formant filter: three band-passes at a vowel's formants (A E I O U, morphed), Shift moving them all
    inline void formants (float& l, float& r)
    {
        if ((fmtCount++ & 15) == 0)
        {
            static const float F[5][3] = { { 600, 1040, 2250 }, { 400, 1620, 2400 }, { 250, 1750, 2600 }, { 400, 750, 2400 }, { 350, 600, 2400 } };
            static const float G[5][3] = { { 1.0f, 0.45f, 0.35f }, { 1.0f, 0.25f, 0.35f }, { 1.0f, 0.06f, 0.16f }, { 1.0f, 0.28f, 0.09f }, { 1.0f, 0.1f, 0.025f } };
            float lfo = 0.0f;
            if (st.fmtMove == 1)
            {
                const double p = fmtPh;
                if (st.fmtShape == 3)
                {
                    if (p < lastFmtPh) { fmtRng ^= fmtRng << 13; fmtRng ^= fmtRng >> 17; fmtRng ^= fmtRng << 5; fmtHold = (float) (fmtRng & 0xffff) / 32768.0f - 1.0f; }
                    lfo = fmtHold;
                }
                else lfo = st.fmtShape == 0 ? (float) std::sin (2.0 * kPi * p) : st.fmtShape == 1 ? (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p) : (float) (2.0 * p - 1.0);
                lastFmtPh = p;
            }
            else if (st.fmtMove == 2) lfo = (float) std::sin (2.0 * kPi * (st.notchSync ? beatPh : notchPh));
            const float v = std::clamp (st.fmtVowel + 0.5f * st.fmtDepth * lfo, 0.0f, 1.0f) * 4.0f;
            const int i0 = std::min (3, (int) v); const float fr = v - (float) i0;
            const float shift = std::pow (2.0f, std::clamp (st.fmtShift, -12.0f, 12.0f) / 12.0f);
            const float q = 4.0f + 16.0f * std::clamp (st.fmtRes, 0.0f, 1.0f);
            for (int j = 0; j < 3; ++j)
            {
                const float fj = std::exp (std::log (F[i0][j]) * (1.0f - fr) + std::log (F[i0 + 1][j]) * fr) * shift;
                fmtGain[j] = G[i0][j] * (1.0f - fr) + G[i0 + 1][j] * fr;
                fmtG[j] = (float) std::tan (kPi * std::clamp ((double) fj, 40.0, sr * 0.45) * isr);
                fmtK[j] = 1.0f / q;
            }
        }
        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c)
        {
            const float x = *io[c];
            float y = 0.0f;
            for (int j = 0; j < 3; ++j)
            {
                auto& s = fmtSt[c][j];
                const float g = fmtG[j], k = fmtK[j];
                const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
                y += k * v1 * fmtGain[j];   // (k x band: unity gain at its centre)
            }
            *io[c] = x + (2.2f * y - x) * std::clamp (st.fmtMix, 0.0f, 1.0f);
        }
    }
    // the comb (tuned to the note) or the phaser (swept around it), with its own LFO
    inline void combPhaser (float& l, float& r)
    {
        const double lfo = std::sin (2.0 * kPi * cpPh) * std::clamp (st.cpDepth, 0.0f, 1.0f);
        const float mix = std::clamp (st.cpMix, 0.0f, 1.0f), fb = std::clamp (st.cpFb, -0.95f, 0.95f);
        float* io[2] = { &l, &r };
        if (st.cp == 1)
        {
            const double fc = std::clamp (lastNoteFreq * std::pow (2.0, ((double) st.cpOffset + 12.0 * lfo) / 12.0), 25.0, sr * 0.25);
            const double d = std::clamp (sr / fc, 2.0, (double) kComb - 4.0);
            for (int c = 0; c < 2; ++c)
            {
                double rp = (double) combW - d; while (rp < 0) rp += kComb;
                const int i0 = (int) rp; const float fr = (float) (rp - i0);
                const float z = combDl[c][i0] + (combDl[c][(i0 + 1) % kComb] - combDl[c][i0]) * fr;
                const float x = *io[c], y = x + fb * z;
                combDl[c][combW] = std::clamp (y, -4.0f, 4.0f);
                *io[c] = x + (0.6f * y - x) * mix;
            }
            combW = (combW + 1) % kComb;
        }
        else
        {
            const int ns = st.cpStages <= 4 ? 4 : (st.cpStages >= 12 ? 12 : 8);
            const double fc = std::clamp (lastNoteFreq * 4.0 * std::pow (2.0, (double) st.cpOffset / 12.0 + 3.0 * lfo), 30.0, sr * 0.45);
            const float t = (float) std::tan (kPi * fc * isr), a = (t - 1.0f) / (t + 1.0f);
            for (int c = 0; c < 2; ++c)
            {
                const float x = *io[c];
                float y = x + fb * apFb[c];
                for (int q = 0; q < ns; ++q) { const float o = a * y + apSt[c][q]; apSt[c][q] = y - a * o; y = o; }
                apFb[c] = std::clamp (y, -4.0f, 4.0f);
                *io[c] = x + (0.5f * (x + y) - x) * mix;
            }
        }
    }
    // Split Drive: a 24 dB/oct crossover (Linkwitz-Riley: the two bands add back to the input's level), each band through
    // the drive's curve with its own amount (anti-aliased); the high band is high-passed again after its drive (so the
    // new harmonics and intermodulation it makes stay out of the sub) and goes through its own tone
    struct Adaa { float prev = 0.0f, F = 1.0f; };
    inline float adaaK (float x, float k, Adaa& a) const
    {
        const float z = k * x, F = std::sqrt (1.0f + z * z), d = x - a.prev;
        const float norm = std::sqrt (1.0f + k * k) / k;
        float y;
        if (std::abs (d) > 1.0e-3f) y = (F - a.F) / (k * d) * norm;
        else { const float m = k * 0.5f * (x + a.prev); y = m / std::sqrt (1.0f + m * m) * norm; }
        a.prev = x; a.F = F;
        return y;
    }
    inline void splitDrive (float& l, float& r)
    {
        float* io[2] = { &l, &r };
        const float ml = std::min (1.0f, 5.0f * st.lowDrive), mh = std::min (1.0f, 5.0f * st.highDrive);
        const float g = splitG, k = 1.41421356f;
        for (int c = 0; c < 2; ++c)
        {
            auto* s = splitSt[c];
            const float x = *io[c];
            float lp1, hp1, lo, d0, hpA, hi;
            svfTick (s[0], x, g, k, lp1, hp1);
            svfTick (s[1], lp1, g, k, lo, d0);      // (low: two Butterworth low-passes)
            svfTick (s[2], x, g, k, d0, hpA);
            svfTick (s[3], hpA, g, k, d0, hi);      // (high: two Butterworth high-passes)
            const float lo2 = st.lowDrive > 0.0f ? lo + (adaaK (lo, splitKl, splitAda[c * 2]) - lo) * ml : lo;
            float hi2 = hi;
            if (st.highDrive > 0.0f)
            {
                hi2 = hi + (adaaK (hi, splitKh, splitAda[c * 2 + 1]) - hi) * mh;
                svfTick (s[4], hi2, g, k, d0, hi2);
                svfTick (s[5], hi2, g, k, d0, hi2);   // (24 dB/oct)
            }
            splitTone[c] += (hi2 - splitTone[c]) * splitToneC; hi2 = splitTone[c];
            *io[c] = lo2 + hi2;
        }
    }
    static constexpr int kComb = 4096;
    float combDl[2][kComb] {};
    int combW = 0;
    float apSt[2][12] {}, apFb[2] {};
    double lastNoteFreq = 110.0;
    int64_t noteSamples = 0, driftLast = 0;
    uint32_t driftRng = 1;
    float driftV[4] {}, driftT[4] {}, driftP[4] {}, driftPT[4] {};
    double driftClock = 0.0, ph2[4] {}, slv[4] {}, pwmPh = 0.0, fmtPh = 0.0, cpPh = 0.0;
    float pwW[4] { 0.5f, 0.5f, 0.5f, 0.5f }, pwDW[4] {};
    struct SvfS { float ic1 = 0.0f, ic2 = 0.0f; };
    static inline void svfTick (SvfS& s, float x, float g, float k, float& lp, float& hp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
        lp = v2; hp = x - k * v1 - v2;
    }
    SvfS fmtSt[2][3], splitSt[2][6];
    float fmtG[3] { 0.05f, 0.1f, 0.2f }, fmtK[3] { 0.1f, 0.1f, 0.1f }, fmtGain[3] { 1, 0, 0 }, fmtHold = 0.0f;
    double lastFmtPh = 0.0; uint32_t fmtRng = 0x51DEu;
    int fmtCount = 0;
    Adaa splitAda[4];
    float splitG = 0.01f, splitKl = 1.0f, splitKh = 1.0f, splitToneC = 1.0f, splitTone[2] {};
    double oldPh[4] {}, oldAcc = 0.0;
    int xfN = 0, xfLen = 1;
    double pitchMul = 1.0;
    float notchJump = 0.0f;
    // the drive's curve: k x / sqrt (1 + (k x)^2), a smooth saturation like tanh whose antiderivative
    // (sqrt (1 + (k x)^2) / k) is cheap; anti-aliased: the mean of the curve between this sample and the last
    inline float shape (float x) const { const float z = driveK * x; return z / std::sqrt (1.0f + z * z) * driveN; }
    inline float adaa (float x, int c)
    {
        const float z = driveK * x, F = std::sqrt (1.0f + z * z);
        const float xp = adaPrev[c], d = x - xp;
        float y;
        if (std::abs (d) > 1.0e-3f) y = (F - adaF[c]) / (driveK * d) * driveN;
        else y = shape (0.5f * (x + xp));
        adaPrev[c] = x; adaF[c] = F;
        return y;
    }
    float adaF[2] { 1.0f, 1.0f };
    float driveMix = 0.0f;
    bool driveLatched = false;
    float adaPrev[2] {};
    float gL[4] {}, gR[4] {};
    int panCount = 0;
    // a half-band low-pass (length 4m + 3: every other tap is zero, the centre is 1/2), Kaiser-windowed
    struct HalfBand
    {
        float side[48] {};   // the non-zero taps, h[2j]
        int L = 0, m = 0;
        void design (int mm)
        {
            m = mm; const int N = 4 * m + 3; L = (N + 1) / 2;
            const double c = (N - 1) * 0.5, beta = 8.96;
            auto i0 = [] (double x) { double s = 1, t = 1; for (int j = 1; j < 30; ++j) { t *= (x / (2 * j)) * (x / (2 * j)); s += t; } return s; };
            double sum = 0;
            for (int j = 0; j < L; ++j)
            {
                const double k = 2.0 * j - c, r = k / c;
                const double h = std::sin (0.5 * kPi * k) / (kPi * k) * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
                side[j] = (float) h; sum += h;
            }
            for (int j = 0; j < L; ++j) side[j] = (float) (side[j] * 0.5 / sum);   // (DC gain 1, with the centre's 1/2)
            L = (L + 7) & ~7;   // (padded with zero taps to a multiple of 8, for the vector loop)
        }
    };
    // one channel's history for going up (one sample in, two out) or down (two in, one out)
    struct HbState
    {
        float x[96] {}, f[96] {};   // (doubled rings, so a run of taps is always contiguous)
        int pos = 0;
        void clear() { std::fill (std::begin (x), std::end (x), 0.0f); std::fill (std::begin (f), std::end (f), 0.0f); pos = 0; }
        inline float dot (const HalfBand& hb, const float* h) const
        {
            float s[8] {};   // (8 running sums: vector instructions)
            for (int j = 0; j < hb.L; j += 8) for (int q = 0; q < 8; ++q) s[q] += hb.side[j + q] * h[j + q];
            return ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
        }
        inline void up (const HalfBand& hb, float in, float* out)
        {
            pos = (pos == 0 ? hb.L : pos) - 1; x[pos] = x[pos + hb.L] = in;   // x[pos + j] = the input j samples ago
            out[0] = 2.0f * dot (hb, x + pos);
            out[1] = x[pos + hb.m];                                           // (the centre tap, delayed)
        }
        inline float down (const HalfBand& hb, float first, float second)
        {
            pos = (pos == 0 ? hb.L : pos) - 1; x[pos] = x[pos + hb.L] = second; f[pos] = f[pos + hb.L] = first;
            return dot (hb, x + pos) + 0.5f * f[pos + hb.m];
        }
    };
    HalfBand hbA, hbB;
    HbState upA[2], upB[2], dnA[2], dnB[2], shpDn[2];
    struct Svf { float ic1 = 0.0f, ic2 = 0.0f; };
    double sr = 48000.0, isr = 1.0 / 48000.0;
    ReeseSettings st;
    int n = 2;
    double ph[4] {}, freq[4] { 110.0, 110.0, 110.0, 110.0 }, acc0 = 0.0;
    double beatPh = 0.0, beatHz = 0.0, twPh = 0.0, rotPh = 0.0, notchPh = 0.0;
    Svf side[2], notchSt[2];
    float sideG = 0.01f, notchG = 0.05f, toneC = 1.0f, toneLp[2] {}, driveK = 1.0f, driveN = 1.0f;
    int notchCount = 0;
};

} // namespace tg
