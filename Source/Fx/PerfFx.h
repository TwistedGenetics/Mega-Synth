#pragma once
// Performance FX: thirteen momentary pads played live on the whole mix (after the effects rack,
// before the Master Filter and the Output trim). Each pad is a real parameter; it engages on the
// next beat-grid line chosen by Quantise and fades in and out over 4 ms. Several can be held at once;
// they run in the fixed order below. With nothing engaged (and the echo / reverb tails gone) the
// stage doesn't touch the audio at all.
//
//   1  Time:   Beat Repeat, Reverse, Tape Stop, Tape Start, Backspin, Akai Stretch  (the last one engaged wins)
//   2  Pitch Down       3  Stutter Gate     4  Trance Gate     5  DJ Filter
//   6  Bitcrush         7  Vinyl            8  Echo Throw      9  Reverb Throw / Freeze
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace tg
{

class PerfFx
{
public:
    enum Pad { Roll, Reverse, TapeStop, TapeStart, Brake, PitchDown, Stutter, Gate, Dj, Crush, Vinyl, Echo, Verb, Akai, kPads };
    enum AkaiMode { AkaiCrunch, AkaiSlow, AkaiFast, AkaiHalftime };
    enum State { Off, Pending, On, Releasing };
    static constexpr int kTimePads = 5;

    struct Settings
    {
        bool pad[kPads] {};
        int quant = 1, rollRate = 2, stutterRate = 1, gatePattern = 2, echoTime = 2;
        float tapeBeats = 1.0f, glide = 0.3f, pitchSemis = -12.0f, gateDepth = 1.0f, djPos = -0.6f, djRes = 0.3f;
        float crushBits = 6.0f, crushRate = 8000.0f, vinyl = 0.6f, echoFb = 0.6f, echoMix = 0.7f, verbSize = 0.8f, verbMix = 0.6f;
        bool verbFreeze = false;
        // Akai Stretch: S1000-style cyclic time-stretch of the whole mix
        int akaiMode = AkaiCrunch, akaiSync = 0; bool akaiHard = true;
        float akaiCycleMs = 40.0f, akaiXfade = 0.35f, akaiPitch = 0.0f, akaiMix = 1.0f;
    };
    struct Clock { double beat = 0.0, beatInc = 0.0, barOrigin = 0.0, beatsPerBar = 4.0, tempo = 120.0; };

    static const char* padName (int k)
    {
        static const char* n[] = { "Beat Repeat", "Reverse", "Tape Stop", "Tape Start", "Backspin", "Pitch Down", "Stutter",
                                   "Trance Gate", "DJ Filter", "Bitcrush", "Vinyl", "Echo Throw", "Reverb Throw", "Akai Stretch" };
        return n[juce::jlimit (0, kPads - 1, k)];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        histLen = (int) std::ceil (8.0 * sr) + 8;   // 8 s of what came in, for Reverse / Backspin
        for (auto& h : hist) h.assign ((size_t) histLen, 0.0f);
        for (auto& b : pitchBuf) b.assign ((size_t) kPitchN, 0.0f);
        for (auto& b : wowBuf) b.assign ((size_t) kWowN, 0.0f);
        echoLen = (int) std::ceil (3.0 * sr) + 8;
        for (auto& b : echoBuf) b.assign ((size_t) echoLen, 0.0f);
        const double k = sr / 44100.0;
        static const int combT[4] = { 1116, 1188, 1277, 1356 }, apT[2] = { 556, 441 };
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int c = 0; c < 4; ++c) comb[ch][c].buf.assign ((size_t) std::max (8, (int) ((combT[c] + 23 * ch) * k)), 0.0f);
            for (int a = 0; a < 2; ++a) ap[ch][a].buf.assign ((size_t) std::max (8, (int) ((apT[a] + 23 * ch) * k)), 0.0f);
        }
        reset();
    }

    void reset()
    {
        for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
        wpos = 0;
        for (int k = 0; k < kPads; ++k) { st[k] = Off; w[k] = 0.0f; padShown[k].store (0, std::memory_order_relaxed); }
        engageOrder.clear();
        for (auto& b : pitchBuf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : wowBuf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : echoBuf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& ch : comb) for (auto& c : ch) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.pos = 0; c.lp = 0.0f; }
        for (auto& ch : ap) for (auto& a : ch) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.pos = 0; }
        pitchW = 0; pitchPhase = 0.0; pitchSemiNow = 0.0f;
        wowW = 0; wowPhase = 0.0; crackle = 0.0f; vinylLp[0] = vinylLp[1] = 0.0f;
        echoW = 0; echoSend = 0.0f; echoLp[0] = echoLp[1] = 0.0f; echoActive = false; echoQuiet = 0; echoDelay = -1.0;
        verbSend = 0.0f; verbActive = false; verbQuiet = 0; verbFrozen = 0.0f; verbFill = 0.0;
        djState[0] = djState[1] = {}; djPosS = 0.0f; djPrimed = false;
        crushPhase = 1.0; crushHold[0] = crushHold[1] = 0.0f;
        stutG = gateG = 1.0f;
        rng = 0x2545F491u;
        active = false;
    }

    // true while anything is (or is about to be) changing the sound
    bool busy() const { return active; }

    std::atomic<int> padShown[kPads] {};   // 0 off, 1 waiting for the grid, 2 on (for the pads' lights)

    void process (float* L, float* R, int n, const Settings& p, const Clock& c)
    {
        if (n <= 0) return;
        // what came in is always kept (Reverse and Backspin play the past); this alone changes nothing
        for (int i = 0; i < n; ++i)
        {
            const int h = (int) ((wpos + i) % histLen);
            hist[0][(size_t) h] = L[i]; hist[1][(size_t) h] = R[i];
        }
        const int64_t blockStart = wpos;
        wpos += n;

        bool anyPad = false;
        for (int k = 0; k < kPads; ++k) anyPad = anyPad || p.pad[k] || st[k] != Off;
        if (! anyPad && ! echoActive && ! verbActive) { active = false; return; }   // bypass: the audio is untouched
        active = true;

        // new presses wait for the grid; releases start fading at once
        const double grid = gridBeats (p.quant, c.beatsPerBar);
        for (int k = 0; k < kPads; ++k)
        {
            if (p.pad[k] && (st[k] == Off || st[k] == Releasing))
            {
                if (grid <= 0.0 || c.beatInc <= 0.0) engage (k, blockStart, p, c, 0);
                else
                {
                    const double rel = (c.beat - c.barOrigin) / grid;
                    double next = std::ceil (rel - 1.0e-9) * grid + c.barOrigin;
                    pendAt[k] = next;
                    st[k] = Pending;
                }
            }
            else if (! p.pad[k] && (st[k] == On || st[k] == Pending))
            {
                st[k] = st[k] == Pending ? Off : Releasing;
                if (st[k] == Off) removeOrder (k);
            }
        }

        const double tempo = std::max (20.0, c.tempo);
        const double spb = 60.0 / tempo * sr;   // samples per beat
        const float fade = (float) (1.0 / (0.004 * sr));
        const float sm1 = 1.0f - (float) std::exp (-1.0 / (0.001 * sr));
        const float sm20 = 1.0f - (float) std::exp (-1.0 / (0.02 * sr));

        for (int i = 0; i < n; ++i)
        {
            const double beat = c.beat + c.beatInc * i;
            const int64_t now = blockStart + i;
            // the grid: pending pads engage on the first sample at or past their line
            for (int k = 0; k < kPads; ++k)
                if (st[k] == Pending && beat >= pendAt[k] - 1.0e-9) engage (k, now, p, c, i);
            for (int k = 0; k < kPads; ++k)
            {
                const float target = st[k] == On ? 1.0f : 0.0f;
                if (k == PitchDown && st[k] == Releasing && pitchSemiNow < -0.02f) continue;   // glides back up before fading
                if (w[k] != target) w[k] = target > w[k] ? std::min (target, w[k] + fade) : std::max (target, w[k] - fade);
                if (st[k] == Releasing && w[k] <= 0.0f) { st[k] = Off; removeOrder (k); }
            }

            float xl = L[i], xr = R[i];

            // ---- 1. time: the pads in the order they were engaged, the newest on top
            for (int k : engageOrder)
            {
                if ((k >= kTimePads && k != Akai) || w[k] <= 0.0f) continue;
                float yl, yr;
                if (k == Akai)
                {
                    akaiRead (now, spb, p, yl, yr);
                    const float m = juce::jlimit (0.0f, 1.0f, p.akaiMix);
                    yl = xl + (yl - xl) * m; yr = xr + (yr - xr) * m;
                }
                else timeRead (k, now, spb, p, c, yl, yr);
                xl += (yl - xl) * w[k]; xr += (yr - xr) * w[k];
            }

            // ---- 2. pitch down: a two-tap delay-line shifter gliding to the amount set
            if (w[PitchDown] > 0.0f || st[PitchDown] != Off)
            {
                const float tgt = st[PitchDown] == On ? p.pitchSemis : 0.0f;
                const float rate = (float) (std::abs (p.pitchSemis) / (std::max (0.005f, p.glide) * sr));
                pitchSemiNow = tgt < pitchSemiNow ? std::max (tgt, pitchSemiNow - rate) : std::min (tgt, pitchSemiNow + rate);
                const double ratio = std::pow (2.0, pitchSemiNow / 12.0);
                const int win = (int) (0.05 * sr);
                pitchBuf[0][(size_t) pitchW] = xl; pitchBuf[1][(size_t) pitchW] = xr;
                pitchPhase += (1.0 - ratio) / win;
                pitchPhase -= std::floor (pitchPhase);
                float ol = 0.0f, orr = 0.0f;
                for (int t = 0; t < 2; ++t)
                {
                    double ph = pitchPhase + 0.5 * t; ph -= std::floor (ph);
                    const double d = 16.0 + ph * win;
                    const float g = (float) std::pow (std::sin (juce::MathConstants<double>::pi * ph), 2.0);
                    ol += g * readRing (pitchBuf[0], kPitchN, pitchW - d);
                    orr += g * readRing (pitchBuf[1], kPitchN, pitchW - d);
                }
                pitchW = (pitchW + 1) & (kPitchN - 1);
                xl += (ol - xl) * w[PitchDown]; xr += (orr - xr) * w[PitchDown];
            }
            else pitchSemiNow = 0.0f;

            // ---- 3. stutter: a square gate on the beat grid
            if (w[Stutter] > 0.0f)
            {
                static const double per[3] = { 0.25, 0.125, 0.0625 };
                const double P = per[juce::jlimit (0, 2, p.stutterRate)];
                const double ph = (beat - c.barOrigin) / P;
                const float tgt = (ph - std::floor (ph)) < 0.5 ? 1.0f : 0.0f;
                stutG += (tgt - stutG) * sm1;
                const float g = 1.0f + (stutG - 1.0f) * w[Stutter];
                xl *= g; xr *= g;
            }
            else stutG = 1.0f;

            // ---- 4. trance gate: a 16-step pattern across the bar
            if (w[Gate] > 0.0f)
            {
                static const uint16_t pat[8] = { 0b1010101010101010, 0b0010001000100010, 0b1011011010110110, 0b1101101101101101,
                                                 0b1001001001001001, 0b1011101110111011, 0b1111111100000000, 0b1000100010101111 };
                const uint16_t m = pat[juce::jlimit (0, 7, p.gatePattern)];
                const int step = (int) std::floor ((beat - c.barOrigin) / 0.25);
                const int s16 = ((step % 16) + 16) % 16;
                const bool on = (m >> (15 - s16)) & 1;
                const float tgt = on ? 1.0f : 1.0f - juce::jlimit (0.0f, 1.0f, p.gateDepth);
                gateG += (tgt - gateG) * (sm1 * 0.5f);
                const float g = 1.0f + (gateG - 1.0f) * w[Gate];
                xl *= g; xr *= g;
            }
            else gateG = 1.0f;

            // ---- 5. DJ filter: one knob, low pass to the left, high pass to the right
            if (w[Dj] > 0.0f)
            {
                if (! djPrimed) { djPosS = p.djPos; djPrimed = true; }
                djPosS += (juce::jlimit (-1.0f, 1.0f, p.djPos) - djPosS) * sm20;
                if ((i & 7) == 0) djCoefs();
                const float k = (float) (1.0 / (0.7071 * std::pow (12.0, (double) juce::jlimit (0.0f, 1.0f, p.djRes))));
                float yl = djTick (djState[0], xl, k), yr = djTick (djState[1], xr, k);
                xl += (yl - xl) * w[Dj]; xr += (yr - xr) * w[Dj];
            }
            else { djPrimed = false; djState[0] = djState[1] = {}; }

            // ---- 6. bitcrush: fewer bits, a lower sample rate
            if (w[Crush] > 0.0f)
            {
                crushPhase += juce::jlimit (200.0, sr, (double) p.crushRate) / sr;
                if (crushPhase >= 1.0)
                {
                    crushPhase -= std::floor (crushPhase);
                    const float stp = 2.0f / std::pow (2.0f, juce::jlimit (2.0f, 16.0f, p.crushBits));
                    crushHold[0] = std::round (xl / stp) * stp; crushHold[1] = std::round (xr / stp) * stp;
                }
                xl += (crushHold[0] - xl) * w[Crush]; xr += (crushHold[1] - xr) * w[Crush];
            }
            else crushPhase = 1.0;

            // ---- 7. vinyl: wow, crackle, hiss and a duller top
            if (w[Vinyl] > 0.0f)
            {
                const float a = juce::jlimit (0.0f, 1.0f, p.vinyl);
                wowBuf[0][(size_t) wowW] = xl; wowBuf[1][(size_t) wowW] = xr;
                wowPhase += 0.55 / sr; wowPhase -= std::floor (wowPhase);
                const double d = (0.003 + 0.0012 * a * std::sin (juce::MathConstants<double>::twoPi * wowPhase)) * sr;
                float yl = readRing (wowBuf[0], kWowN, wowW - d), yr = readRing (wowBuf[1], kWowN, wowW - d);
                wowW = (wowW + 1) & (kWowN - 1);
                const float lpc = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * (9000.0 - 5500.0 * a) / sr);
                vinylLp[0] += (yl - vinylLp[0]) * lpc; vinylLp[1] += (yr - vinylLp[1]) * lpc;
                if (rand01() < 0.0004f * a) crackle = (rand01() * 2.0f - 1.0f) * 0.25f * a;
                crackle *= 0.55f;
                const float hiss = (rand01() * 2.0f - 1.0f) * 0.004f * a;
                yl = vinylLp[0] + crackle + hiss; yr = vinylLp[1] + crackle + hiss;
                xl += (yl - xl) * w[Vinyl]; xr += (yr - xr) * w[Vinyl];
            }
            else { vinylLp[0] = xl; vinylLp[1] = xr; }

            // ---- 8. echo throw: what's played while held goes into a synced ping-pong delay that rings on
            {
                const float sendT = st[Echo] == On ? 1.0f : 0.0f;
                echoSend += (sendT - echoSend) * sm1;
                if (echoSend > 1.0e-6f || echoActive)
                {
                    if (! echoActive) { echoActive = true; echoQuiet = 0; }
                    static const double eb[5] = { 0.25, 0.5, 0.75, 1.0, 2.0 };
                    const double dT = std::min ((double) echoLen - 4.0, eb[juce::jlimit (0, 4, p.echoTime)] * spb);
                    if (echoDelay < 0.0) echoDelay = dT;
                    echoDelay += (dT - echoDelay) * 0.0005;
                    const float dl = readRing (echoBuf[0], echoLen, echoW - echoDelay), dr = readRing (echoBuf[1], echoLen, echoW - echoDelay);
                    const float fb = juce::jlimit (0.0f, 0.95f, p.echoFb);
                    const float lpc = 0.35f;
                    echoLp[0] += (dr - echoLp[0]) * lpc; echoLp[1] += (dl - echoLp[1]) * lpc;   // ping-pong: each side feeds the other
                    echoBuf[0][(size_t) echoW] = xl * echoSend + echoLp[0] * fb;
                    echoBuf[1][(size_t) echoW] = xr * echoSend * 0.3f + echoLp[1] * fb;
                    if (++echoW >= echoLen) echoW = 0;
                    const float m = juce::jlimit (0.0f, 1.0f, p.echoMix);
                    xl += dl * m; xr += dr * m;
                    if (echoSend < 1.0e-6f && std::abs (dl) + std::abs (dr) < 1.0e-5f) ++echoQuiet;   // -100 dB: gone else echoQuiet = 0;
                    if (echoQuiet > (int) (dT + sr * 0.05))   // a whole delay of silence: the tail has gone
                    {
                        echoActive = false; echoSend = 0.0f; echoDelay = -1.0;
                        std::fill (echoBuf[0].begin(), echoBuf[0].end(), 0.0f); std::fill (echoBuf[1].begin(), echoBuf[1].end(), 0.0f);
                        echoLp[0] = echoLp[1] = 0.0f;
                    }
                }
            }

            // ---- 9. reverb throw (and freeze: hold the tail still while the pad is held)
            {
                const bool held = st[Verb] == On;
                const bool freezeNow = held && p.verbFreeze && verbFill >= 0.25 * spb;
                if (held) verbFill += 1.0; else verbFill = 0.0;
                const float sendT = held && ! freezeNow ? 1.0f : 0.0f;
                verbSend += (sendT - verbSend) * sm1;
                verbFrozen += ((freezeNow ? 1.0f : 0.0f) - verbFrozen) * sm1;
                if (verbSend > 1.0e-6f || verbActive)
                {
                    if (! verbActive) { verbActive = true; verbQuiet = 0; }
                    const float fb = 0.70f + 0.28f * juce::jlimit (0.0f, 1.0f, p.verbSize);
                    const float fbN = fb + (1.0f - fb) * verbFrozen, damp = 0.3f * (1.0f - verbFrozen);
                    const float in = (xl + xr) * 0.5f * 0.12f * verbSend;
                    float out[2];
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        float acc = 0.0f;
                        for (auto& cb : comb[ch])
                        {
                            const float y = cb.buf[(size_t) cb.pos];
                            cb.lp = y * (1.0f - damp) + cb.lp * damp;
                            cb.buf[(size_t) cb.pos] = in + cb.lp * fbN;
                            if (++cb.pos >= (int) cb.buf.size()) cb.pos = 0;
                            acc += y;
                        }
                        for (auto& a : ap[ch])
                        {
                            const float b = a.buf[(size_t) a.pos];
                            const float y = -acc + b;
                            a.buf[(size_t) a.pos] = acc + b * 0.5f;
                            if (++a.pos >= (int) a.buf.size()) a.pos = 0;
                            acc = y;
                        }
                        out[ch] = acc;
                    }
                    const float m = juce::jlimit (0.0f, 1.0f, p.verbMix);
                    xl += out[0] * m; xr += out[1] * m;
                    if (verbSend < 1.0e-6f && verbFrozen < 1.0e-4f && std::abs (out[0]) + std::abs (out[1]) < 1.0e-5f) ++verbQuiet; else verbQuiet = 0;
                    if (verbQuiet > (int) (0.1 * sr))
                    {
                        verbActive = false; verbSend = 0.0f;
                        for (auto& chs : comb) for (auto& cb : chs) { std::fill (cb.buf.begin(), cb.buf.end(), 0.0f); cb.lp = 0.0f; }
                        for (auto& chs : ap) for (auto& a : chs) std::fill (a.buf.begin(), a.buf.end(), 0.0f);
                    }
                }
            }

            L[i] = xl; R[i] = xr;
        }
        for (int k = 0; k < kPads; ++k) padShown[k].store (st[k] == Pending ? 1 : (st[k] == On ? 2 : 0), std::memory_order_relaxed);
        if (! std::isfinite (L[n - 1]) || ! std::isfinite (R[n - 1])) reset();
    }

    State state (int k) const { return st[juce::jlimit (0, kPads - 1, k)]; }
    double lastEngagedBeat (int k) const { return engagedBeat[juce::jlimit (0, kPads - 1, k)]; }   // (tests)
    float weight (int k) const { return w[juce::jlimit (0, kPads - 1, k)]; }

    static double gridBeats (int quant, double beatsPerBar)
    {
        switch (quant) { case 1: return 0.25; case 2: return 0.5; case 3: return 1.0; case 4: return 2.0; case 5: return beatsPerBar > 0 ? beatsPerBar : 4.0; default: return 0.0; }
    }

private:
    static constexpr int kPitchN = 1 << 15, kWowN = 1 << 12;

    void engage (int k, int64_t now, const Settings& p, const Clock& c, int i)
    {
        engagedBeat[k] = c.beat + c.beatInc * i;
        if (st[k] != On) { removeOrder (k); engageOrder.push_back (k); }
        // the shifter's and the wow's delay lines start from the recent past, not from whatever they last held
        if ((k == PitchDown || k == Vinyl) && w[k] <= 0.0f)
        {
            std::vector<float>* buf = k == PitchDown ? pitchBuf : wowBuf;   // (each is an array of two channels)
            int& wi = k == PitchDown ? pitchW : wowW;
            const int N = k == PitchDown ? kPitchN : kWowN;
            const int m = std::min (N - 1, k == PitchDown ? (int) (0.05 * sr) + 64 : N - 1);
            for (int ch = 0; ch < 2; ++ch)
                for (int j = 1; j <= m; ++j)
                {
                    const int64_t src = now - j;
                    const int h = (int) (((src % histLen) + histLen) % histLen);
                    buf[ch][(size_t) ((wi - j) & (N - 1))] = src >= 0 ? hist[ch][(size_t) h] : 0.0f;
                }
        }
        st[k] = On;
        if (k == Akai)
        {
            const double spb = 60.0 / std::max (20.0, c.tempo) * sr;
            ak.start = now; ak.age = 0; ak.cycIndex = -1; ak.cycStart = ak.prevStart = 0.0;
            ak.bar = std::min ((double) histLen * 0.4, (c.beatsPerBar > 0 ? c.beatsPerBar : 4.0) * spb);
        }
        if (k < kTimePads)
        {
            auto& t = tr[k];
            t.start = now; t.pos = (double) now; t.elapsed = 0.0;
            t.rate = k == Reverse ? -1.0 : 1.0;
            const double spb = 60.0 / std::max (20.0, c.tempo) * sr;
            static const double rb[6] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
            t.slice = std::max (16.0, rb[juce::jlimit (0, 5, p.rollRate)] * spb);
            t.span = std::min (4.0 * sr, (c.beatsPerBar > 0 ? c.beatsPerBar : 4.0) * spb);
        }
    }
    void removeOrder (int k) { engageOrder.erase (std::remove (engageOrder.begin(), engageOrder.end(), k), engageOrder.end()); }

    struct TimeReader { int64_t start = 0; double pos = 0.0, rate = 1.0, elapsed = 0.0, slice = 1000.0, span = 48000.0; };
    TimeReader tr[kTimePads];

    // ---- Akai Stretch: the Break Chopper's Akai Cyclic method on the recent audio. Short cycles, each
    // read at the pitch from where the sound "should be by now", repeated or skipped as the speed
    // needs, the last one crossfaded into the next (the S1000's flutter at 1 / Cycle Length).
    //   Crunch: real time (one cycle late), Pitch shifts it.  Slow: from the press at x0.75 / x0.5.
    //   Fast: the last bar looped at x1.5 / x2.  Halftime: the last bar stretched over two.
    struct AkaiState { int64_t start = 0, age = 0, cycIndex = -1, cycAge = 0; double cycStart = 0.0, prevStart = 0.0, bar = 48000.0; };
    AkaiState ak;
    double akaiCycle (double spb, const Settings& p) const
    {
        if (p.akaiSync == 1) return spb / 8.0;    // 1/32
        if (p.akaiSync == 2) return spb / 16.0;   // 1/64
        return (double) juce::jlimit (10.0f, 200.0f, p.akaiCycleMs) * 0.001 * sr;
    }
    // where the sound should be by now (a position in the history), for this output sample
    double akaiProgress (int64_t now, double lag, const Settings& p) const
    {
        const double e = (double) (now - ak.start);
        switch (p.akaiMode)
        {
            case AkaiSlow:
            {
                const double s = p.akaiHard ? 0.5 : 0.75;
                return (double) ak.start - lag + s * e;
            }
            case AkaiFast:
            case AkaiHalftime:
            {
                // the last bar before the press, looped (faster, or at half speed over two bars)
                const double s = p.akaiMode == AkaiHalftime ? 0.5 : (p.akaiHard ? 2.0 : 1.5);
                const double regionStart = (double) ak.start - ak.bar - lag;
                return regionStart + std::fmod (s * e, ak.bar);
            }
            default: return (double) now - lag;   // Crunch: real time
        }
    }
    void akaiRead (int64_t now, double spb, const Settings& p, float& yl, float& yr)
    {
        const double pitchRate = std::pow (2.0, (double) juce::jlimit (-12.0f, 12.0f, p.akaiPitch) / 12.0);
        const int64_t C = std::max ((int64_t) 16, (int64_t) akaiCycle (spb, p));
        const int64_t X = std::max ((int64_t) 8, (int64_t) (juce::jlimit (0.0f, 1.0f, p.akaiXfade) * 0.5f * (float) C));
        const double lag = pitchRate * (double) C + 4.0;   // a cycle reads ahead of where it starts: start it that far back
        // Slow drifts further behind: before it falls out of the history, it starts again from now
        if (p.akaiMode == AkaiSlow && (double) now - akaiProgress (now, lag, p) > (double) histLen - 2.0 * (double) C - sr)
            ak.start = now;
        const int64_t k = ak.age / C;
        if (k != ak.cycIndex)
        {
            ak.prevStart = ak.cycStart + pitchRate * (double) C;
            ak.cycStart = akaiProgress (now, lag, p);
            if (ak.cycIndex < 0) ak.prevStart = ak.cycStart;
            ak.cycIndex = k; ak.cycAge = 0;
        }
        const double t = (double) ak.cycAge;
        auto rd = [&] (double pos, float& l, float& r)
        {
            pos = std::min (pos, (double) now);
            if (pos < (double) (wpos - histLen + 4)) { l = r = 0.0f; return; }
            l = readHist (0, pos); r = readHist (1, pos);
        };
        rd (ak.cycStart + pitchRate * t, yl, yr);
        if (ak.cycAge < X && k > 0)
        {
            float pl, pr;
            rd (ak.prevStart + pitchRate * t, pl, pr);
            const float f = (float) (t + 1.0) / (float) (X + 1);
            yl = pl + (yl - pl) * f; yr = pr + (yr - pr) * f;
        }
        ++ak.cycAge; ++ak.age;
    }

    // one sample from a time pad's reader (the history already holds the current sample)
    void timeRead (int k, int64_t now, double spb, const Settings& p, const Clock&, float& yl, float& yr)
    {
        auto& t = tr[k];
        const double T = std::max (16.0, (double) p.tapeBeats * spb);   // tape time in samples
        const double ramp = 0.0015 * sr;                                 // declick at loop points
        float g = 1.0f;
        double pos = t.pos;
        switch (k)
        {
            case Roll:
            {
                double e = (double) (now - t.start);
                const double ph = std::fmod (e, t.slice);
                pos = (double) t.start + ph;
                g = (float) std::min (1.0, std::min ((t.slice - ph) / ramp, e < t.slice ? 1.0e9 : ph / ramp));
                if (e >= t.slice && ph < 1.0)
                {
                    // a new repeat: pick up a changed length here
                    static const double rb[6] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
                    const double s2 = std::max (16.0, rb[juce::jlimit (0, 5, p.rollRate)] * spb);
                    if (std::abs (s2 - t.slice) > 0.5) { t.start = now; t.slice = s2; pos = (double) now; }
                }
                break;
            }
            case Reverse:
            {
                const double e = (double) (now - t.start);
                const double ph = std::fmod (e, t.span);
                pos = (double) t.start - ph;
                g = (float) std::min (1.0, std::min ((t.span - ph) / ramp, e < t.span ? 1.0e9 : ph / ramp));
                break;
            }
            case TapeStop:
            {
                const double r = std::max (0.0, 1.0 - t.elapsed / T);
                t.rate = r * r * (3.0 - 2.0 * r) * 0.3 + r * 0.7;   // slows, then stops
                t.pos += t.rate; t.elapsed += 1.0;
                pos = t.pos;
                g = (float) std::min (1.0, (1.0 - t.elapsed / T) / 0.08);
                break;
            }
            case TapeStart:
            {
                const double r = std::min (1.0, t.elapsed / T);
                t.rate = r * r;
                t.pos += t.rate; t.elapsed += 1.0;
                pos = t.pos;
                g = (float) std::min (1.0, r / 0.15);
                break;
            }
            case Brake:
            {
                // a hand on the record: snaps back, spins backwards and slows to a stop
                const double grab = 0.03 * sr;
                const double e = t.elapsed;
                if (e < grab) t.rate = 1.0 - 4.0 * (e / grab);
                else { const double r = std::max (0.0, 1.0 - (e - grab) / T); t.rate = -3.0 * r * r; }
                t.pos += t.rate; t.elapsed += 1.0;
                pos = t.pos;
                g = (float) std::min (1.0, (1.0 - (e - grab) / T) / 0.1);
                break;
            }
            default: break;
        }
        g = juce::jlimit (0.0f, 1.0f, g);
        // never ahead of what's been written, never further back than the history
        pos = std::min (pos, (double) now);
        if (g <= 0.0f || pos < (double) (wpos - histLen + 4)) { yl = yr = 0.0f; return; }
        yl = g * readHist (0, pos); yr = g * readHist (1, pos);
    }

    float readHist (int ch, double pos) const
    {
        const double fl = std::floor (pos);
        const float fr = (float) (pos - fl);
        const int64_t i0 = (int64_t) fl;
        const int a = (int) (((i0 % histLen) + histLen) % histLen), b = (a + 1) % histLen;
        const auto& h = hist[ch];
        return h[(size_t) a] + (h[(size_t) b] - h[(size_t) a]) * fr;
    }
    static float readRing (const std::vector<float>& b, int len, double pos)
    {
        double fl = std::floor (pos);
        const float fr = (float) (pos - fl);
        int a = (int) fl % len; if (a < 0) a += len;
        const int c = a + 1 >= len ? 0 : a + 1;
        return b[(size_t) a] + (b[(size_t) c] - b[(size_t) a]) * fr;
    }

    struct Svf { float ic1 = 0.0f, ic2 = 0.0f; };
    void djCoefs()
    {
        const float pos = djPosS;
        double fc;
        if (pos <= 0.0f) { djHp = false; fc = 20000.0 * std::pow (0.0025, (double) -pos); }   // -1: 50 Hz
        else { djHp = true; fc = 20.0 * std::pow (500.0, (double) pos); }                      // +1: 10 kHz
        fc = juce::jlimit (20.0, sr * 0.45, fc);
        djG = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
        djAmt = std::abs (pos);
    }
    float djTick (Svf& s, float x, float k) const
    {
        const float g = djG, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
        const float y = djHp ? x - k * v1 - v2 : v2;
        // near the centre the filter fades out, so the middle of the knob is the dry sound
        const float m = juce::jlimit (0.0f, 1.0f, djAmt / 0.08f);
        return x + (y - x) * m;
    }

    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f; }

    struct Comb { std::vector<float> buf; int pos = 0; float lp = 0.0f; };
    struct AllPass { std::vector<float> buf; int pos = 0; };

    double sr = 48000.0;
    std::vector<float> hist[2];
    int histLen = 1;
    int64_t wpos = 0;
    State st[kPads] {};
    float w[kPads] {};
    double pendAt[kPads] {}, engagedBeat[kPads] {};
    std::vector<int> engageOrder;   // (reserved in the constructor: no allocation on the audio thread)
    bool active = false;

    std::vector<float> pitchBuf[2];
    int pitchW = 0; double pitchPhase = 0.0; float pitchSemiNow = 0.0f;
    float stutG = 1.0f, gateG = 1.0f;
    Svf djState[2]; float djPosS = 0.0f, djG = 0.5f, djAmt = 0.0f; bool djHp = false, djPrimed = false;
    double crushPhase = 1.0; float crushHold[2] {};
    std::vector<float> wowBuf[2]; int wowW = 0; double wowPhase = 0.0; float crackle = 0.0f, vinylLp[2] {};
    std::vector<float> echoBuf[2]; int echoLen = 1, echoW = 0, echoQuiet = 0; float echoSend = 0.0f, echoLp[2] {}; bool echoActive = false; double echoDelay = -1.0;
    Comb comb[2][4]; AllPass ap[2][2];
    float verbSend = 0.0f, verbFrozen = 0.0f; bool verbActive = false; int verbQuiet = 0; double verbFill = 0.0;
    uint32_t rng = 0x2545F491u;

public:
    PerfFx() { engageOrder.reserve (kPads + 1); }
};

} // namespace tg
