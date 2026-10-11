#pragma once
// The Break Chopper's player: up to 8 slices sounding at once, each stretched to the song's tempo
// (or not), then the chosen sampler colour, optionally the synth's Filter 1, its Mixer strip, and
// into the bus before the effects.
//
// Stretch modes
//  - Repitch: the slice is simply played faster or slower; tempo and pitch move together.
//  - Akai Cyclic: like the S1000's cyclic time-stretch: the slice is cut into short cycles (Cycle
//    Length), each played at the slice's pitch from where it should be by now; cycles are repeated
//    (slower) or skipped (faster) and crossfaded (Crossfade).
//  - Clean (WSOLA): windowed overlap-add with a search for the best-matching place to splice, so
//    stretching keeps transients and tone with far less of the cyclic "flutter".
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "BreakStore.h"
#include "../EngineTypes.h"
#include "../Params.h"
#include "../Filter/Filter.h"
#include "../Fx/RackFx.h"
#include "../GranSynth.h"

namespace tg
{

class BreakChopper
{
public:
    enum Stretch { Repitch, Cyclic, Clean };
    struct Settings
    {
        float level = 0.8f, pitch = 0.0f, velSens = 0.5f, cycleMs = 40.0f, xfade = 0.35f;
        int stretch = Cyclic, colour = 0, quality = 1;
        bool sync = true, thruFilter = false;
        double hostTempo = 120.0;
        float stripL = 1.0f, stripR = 1.0f;   // the Mixer strip (balance pan, mute / solo), already worked out
    };
    static constexpr int kVoices = 8;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& b : bus) b.assign ((size_t) std::max (64, maxBlock) + 16, 0.0f);
        colourFx.prepare (sr);
        filter.reset();
        reset();
    }
    void reset()
    {
        for (auto& v : voices) v.on = false;
        colourFx.reset(); filter.reset();
        tail = 0; peakShown = 0.0f;
    }

    // the analysis and per-slice settings to play from (the engine pulls a fresh copy when it changes)
    BreakData& data() { return d; }
    void setSample (const WaveSample* w) { if (w != wav) { wav = w; for (auto& v : voices) v.on = false; } }
    const WaveSample* sample() const { return wav; }
    int numSlices() const { return wav != nullptr ? d.nSlices : 0; }

    // ---- control (audio thread, between renders)
    struct Trigger { int slice = 0; float vel = 1.0f, pitch = 0.0f; bool reverse = false; bool fromKey = false; };
    void trigger (const Trigger& t, const Settings& s)
    {
        if (wav == nullptr || d.nSlices <= 0) return;
        const int k = ((t.slice % d.nSlices) + d.nSlices) % d.nSlices;
        const BreakSlice& sl = d.slice[k];
        lastSlice = k;
        // the choke group cuts the others off (fast fade)
        if (sl.choke > 0) for (auto& v : voices) if (v.on && v.choke == sl.choke) v.stop (sr);
        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.on) { slot = &v; break; }
        if (slot == nullptr) { slot = &voices[0]; for (auto& v : voices) if (v.age > slot->age) slot = &v; }
        Voice& v = *slot;
        v = Voice();
        v.on = true; v.slice = k; v.choke = sl.choke; v.loop = sl.loop; v.held = t.fromKey;
        const double a = d.start[k], b = d.start[k + 1], len = b - a;
        v.s0 = a + sl.trimStart * len; v.s1 = a + sl.trimEnd * len;
        v.dir = (t.reverse != sl.reverse) ? -1 : 1;
        const float velG = 1.0f - s.velSens + s.velSens * juce::jlimit (0.0f, 1.25f, t.vel);
        v.gain = sl.gain * velG;
        const float pan = juce::jlimit (-1.0f, 1.0f, sl.pan);
        v.pl = std::min (1.0f, 1.0f - pan); v.pr = std::min (1.0f, 1.0f + pan);
        v.attack = std::max (0.0015f, sl.attack); v.decay = sl.decay;
        v.semis = s.pitch + sl.pitch + t.pitch;
        v.startHead();
    }
    // a key let go: looping slices stop, one-shots play on
    void release (int slice)
    {
        for (auto& v : voices) if (v.on && v.held && v.slice == slice) { v.held = false; if (v.loop) v.stop (sr); }
    }
    void stopAll() { for (auto& v : voices) if (v.on) v.stop (sr); }
    int lastSlicePlayed() const { return lastSlice; }
    int activeVoices() const { int n = 0; for (auto& v : voices) n += v.on ? 1 : 0; return n; }
    double ratio (const Settings& s) const { return s.sync && d.origBpm > 1.0 ? s.hostTempo / d.origBpm : 1.0; }

    // Renders the slices and adds them (coloured, filtered, through the strip) into L / R.
    // snapV: the parameters (Filter 1 settings for "through synth filter")
    void process (float* L, float* R, int n, const Settings& s, const float* snapV)
    {
        bool any = false;
        for (auto& v : voices) any = any || v.on;
        if (! any && tail <= 0) { peakShown = 0.0f; return; }
        float* bl = bus[0].data(); float* br = bus[1].data();
        if ((int) bus[0].size() < n) return;
        std::fill (bl, bl + n, 0.0f); std::fill (br, br + n, 0.0f);
        if (wav != nullptr && wav->length > 1)
        {
            const double tempoRatio = ratio (s);
            const double fileRate = wav->sampleRate / sr;
            for (auto& v : voices) if (v.on) renderVoice (v, bl, br, n, s, tempoRatio, fileRate);
        }
        any = false;
        for (auto& v : voices) any = any || v.on;
        tail = any ? (int) (0.3 * sr) : tail - n;   // the colour and filter ring on a little after the last slice

        // ---- the sampler colour (the Vintage Sampler's models)
        if (s.colour > 0)
        {
            static const int model[] = { 0, 0, 1, 2, 3 };
            colourV[P_smpModel] = (float) model[juce::jlimit (0, 4, s.colour)];
            colourV[P_smpBits] = 12.0f; colourV[P_smpRate] = s.colour == 2 ? 22050.0f : 32000.0f; colourV[P_smpAA] = 1.0f;
            colourV[P_smpCutoff] = s.colour == 2 ? 11000.0f : 14000.0f; colourV[P_smpRes] = 0.1f;
            colourV[P_smpNoise] = 0.05f; colourV[P_smpDrive] = 0.25f;
            FxContext ctx; ctx.sr = sr; ctx.quality = s.quality;
            colourFx.process (bl, br, n, colourV.data(), ctx);
        }
        // ---- through the synth's Filter 1 (off by default)
        if (s.thruFilter && snapV != nullptr)
        {
            const int model = juce::jlimit (0, 30, (int) std::lround (snapV[P_filterMode]));
            const int type = (int) std::lround (snapV[P_filterType]), slope = (int) std::lround (snapV[P_filterSlope]);
            const int ch = (int) std::lround (snapV[P_flt1Char]);
            if (model != fModel || type != fType || slope != fSlope || ch != fChar)
            {
                filter.configure (model, type, slope, fModel < 0, juce::jlimit (0, (int) CM_COUNT - 1, ch));
                fModel = model; fType = type; fSlope = slope; fChar = ch;
            }
            filter.setCharacter (snapV[P_flt1CharAmt]);
            filter.setDrive (std::max (1.0f, snapV[P_filterDrive]));
            filter.setMix (1.0f);
            for (int i = 0; i < n; ++i)
            {
                if ((i & 15) == 0) filter.update (juce::jlimit (20.0f, 20000.0f, snapV[P_filterCutoff]), juce::jlimit (0.1f, 30.0f, snapV[P_filterRes]), sr);
                float yl, yr; filter.processFrame (bl[i], br[i], true, yl, yr);
                bl[i] = yl; br[i] = yr;
            }
        }
        else fModel = -1;
        // ---- the strip and level, then into the bus
        float pk = 0.0f;
        const float g = juce::jlimit (0.0f, 1.0f, s.level);
        for (int i = 0; i < n; ++i)
        {
            const float l = bl[i] * g, r = br[i] * g;
            pk = std::max (pk, std::max (std::abs (l), std::abs (r)));
            L[i] += l * s.stripL; R[i] += r * s.stripR;
        }
        peakShown = std::max (peakShown, pk);
    }
    float takePeak() { const float p = peakShown; peakShown = 0.0f; return p; }

    // for the display: the newest voice's position (fraction of the sample), -1 when none
    double playheadFrac() const
    {
        const Voice* nv = nullptr;
        for (auto& v : voices) if (v.on && (nv == nullptr || v.age < nv->age)) nv = &v;
        if (nv == nullptr || wav == nullptr || wav->length < 2) return -1.0;
        return juce::jlimit (0.0, 1.0, nv->progress / (double) wav->length);
    }

private:
    struct Voice
    {
        bool on = false, held = false, loop = false;
        int slice = 0, choke = 1, dir = 1;
        double s0 = 0, s1 = 0;            // the (trimmed) slice in sample frames
        float gain = 1.0f, pl = 1.0f, pr = 1.0f, attack = 0.002f, decay = 0.0f, semis = 0.0f;
        int64_t age = 0;                  // output samples since the start
        double progress = 0;              // where in the slice the tempo says we should be (frames)
        // cyclic: the current and the previous cycle's heads
        double cycStart = 0, prevStart = 0; int64_t cycIndex = -1; int64_t cycAge = 0;
        // clean: two overlapping grains
        double gPos[2] {}; int gAge[2] { -1, -1 }; int nextGrain = 0; int64_t hopIndex = -1;
        // repitch
        double head = 0;
        // envelope
        float env = 0.0f, rel = 1.0f, relStep = 0.0f, decayMul = 1.0f;
        bool releasing = false;

        void startHead() { progress = dir > 0 ? s0 : s1 - 1.0; head = progress; age = 0; env = 0.0f; rel = 1.0f; releasing = false; cycIndex = -1; hopIndex = -1; gAge[0] = gAge[1] = -1; }
        void stop (double sr) { if (! releasing) { releasing = true; relStep = (float) (1.0 / (0.003 * sr)); } }
    };

    static inline void readAt (const WaveSample& w, double pos, float& l, float& r)
    {
        if (pos < 0.0 || pos >= (double) (w.length - 1)) { l = r = 0.0f; return; }
        const int i = (int) pos;
        const float f = (float) (pos - i);
        const float* a = w.ch[0].data();
        const float* b = w.ch[w.numChannels > 1 ? 1 : 0].data();
        l = a[i] + (a[i + 1] - a[i]) * f;
        r = b[i] + (b[i + 1] - b[i]) * f;
    }

    void renderVoice (Voice& v, float* bl, float* br, int n, const Settings& s, double tempoRatio, double fileRate)
    {
        const WaveSample& w = *wav;
        const double pitchRate = std::pow (2.0, v.semis / 12.0) * fileRate;   // how fast the heads read (pitch only)
        const double adv = tempoRatio * fileRate;                            // how fast the slice is gone through
        const double repRate = pitchRate * tempoRatio;                       // repitch: both together
        const int C = std::max (16, (int) (s.cycleMs * 0.001 * sr));
        const int X = std::max (8, (int) (juce::jlimit (0.0f, 1.0f, s.xfade) * 0.5f * (float) C));   // crossfade (up to half a cycle)
        const int W = std::max (64, (int) (s.cycleMs * 0.001 * sr * 1.5)) & ~1;   // the clean window, its hop is half
        const int Ha = W / 2;
        const float atkStep = (float) (1.0 / (v.attack * sr));
        const float decayMul = v.decay > 0.0f ? (float) std::exp (-1.0 / (v.decay * sr)) : 1.0f;
        const double fadeOut = 0.0035 * sr + 2.0;   // (longer than the 3 ms release, so it always finishes)
        for (int i = 0; i < n && v.on; ++i)
        {
            // where the slice should be by now, and how much is left
            const double rate = s.stretch == Repitch ? repRate : adv;
            double remain = v.dir > 0 ? (v.s1 - v.progress) / rate : (v.progress - v.s0) / rate;
            if (remain <= 0.0)
            {
                if (v.loop && ! v.releasing) { v.startHead(); remain = (v.s1 - v.s0) / rate; }
                else { v.on = false; break; }
            }
            if (! v.loop && remain < fadeOut) v.stop (sr);   // a quick fade at the end of the slice
            float yl = 0.0f, yr = 0.0f;
            switch (s.stretch)
            {
                case Repitch:
                {
                    readAt (w, v.head, yl, yr);
                    v.head += v.dir * repRate;
                    break;
                }
                case Cyclic:
                {
                    const int64_t k = v.age / C;
                    if (k != v.cycIndex)
                    {
                        // a new cycle starts where the slice should be by now; the last one fades out
                        v.prevStart = v.cycStart + v.dir * pitchRate * (double) C;
                        v.cycStart = v.progress;
                        if (v.cycIndex < 0) v.prevStart = v.cycStart;
                        v.cycIndex = k; v.cycAge = 0;
                    }
                    const double t = (double) v.cycAge;
                    readAt (w, v.cycStart + v.dir * pitchRate * t, yl, yr);
                    if (v.cycAge < X && k > 0)
                    {
                        float pl, pr;
                        readAt (w, v.prevStart + v.dir * pitchRate * t, pl, pr);
                        const float f = (float) (t + 1.0) / (float) (X + 1);
                        yl = pl + (yl - pl) * f; yr = pr + (yr - pr) * f;
                    }
                    ++v.cycAge;
                    break;
                }
                default:   // Clean (WSOLA)
                {
                    const int64_t h = v.age / Ha;
                    if (h != v.hopIndex)
                    {
                        v.hopIndex = h;
                        // a new grain: near where the slice should be, at the spot that best continues the last grain
                        const int g = v.nextGrain; v.nextGrain ^= 1;
                        const int o = g ^ 1;
                        double target = v.progress;
                        if (v.gAge[o] >= 0)
                        {
                            const double natural = v.gPos[o] + v.dir * pitchRate * (double) Ha;
                            target = bestSplice (w, natural, target, W, pitchRate * v.dir, s.quality);
                        }
                        v.gPos[g] = target; v.gAge[g] = 0;
                    }
                    for (int g = 0; g < 2; ++g)
                    {
                        if (v.gAge[g] < 0) continue;
                        const float win = GranVoice::windowFast (0, (float) v.gAge[g] / (float) W);   // Hann = sin^2, from a table
                        // the very first grain starts at full level (no half-window dip at the attack)
                        const float wv = (v.hopIndex == 0 && v.gAge[g] < Ha && g == 0 && v.age < Ha) ? 1.0f : win;
                        float a, b; readAt (w, v.gPos[g] + v.dir * pitchRate * (double) v.gAge[g], a, b);
                        yl += a * wv; yr += b * wv;
                        if (++v.gAge[g] >= W) v.gAge[g] = -1;
                    }
                    break;
                }
            }
            // envelope: attack, optional decay, the release
            if (v.env < 1.0f && ! v.releasing) v.env = std::min (1.0f, v.env + atkStep);
            else if (decayMul < 1.0f) v.env *= decayMul;
            if (v.releasing) { v.rel -= v.relStep; if (v.rel <= 0.0f) { v.on = false; break; } }
            const float g = v.gain * v.env * v.rel;
            bl[i] += yl * g * v.pl; br[i] += yr * g * v.pr;
            v.progress += v.dir * rate;
            ++v.age;
        }
    }

    // WSOLA: within a quarter window of the target, the start whose audio best matches the natural continuation
    static double bestSplice (const WaveSample& w, double natural, double target, int W, double step, int quality)
    {
        const int tol = W / 5, L = W / 2;
        const int coarse = quality >= 2 ? 2 : (quality == 1 ? 4 : 8), jStep = quality >= 2 ? 3 : 6;
        const float* a = w.ch[0].data();
        auto at = [&] (double p) -> float { const int i = (int) p; return (i >= 0 && i < w.length) ? a[i] : 0.0f; };
        // the natural continuation, sampled once
        float nat[512]; double off[512]; int m = 0;
        for (int j = 0; j < L && m < 512; j += jStep, ++m) { off[m] = step * j; nat[m] = at (natural + off[m]); }
        auto score = [&] (double c)
        {
            float acc = 0.0f, e = 1e-9f;
            const int ic = (int) c;
            const bool inside = ic >= 0 && ic + (int) std::abs (step * L) + 2 < w.length && step > 0.0;
            for (int k = 0; k < m; ++k)
            {
                const float y = inside ? a[ic + (int) off[k]] : at (c + off[k]);
                acc += nat[k] * y; e += y * y;
            }
            return acc / std::sqrt (e);
        };
        double best = target; float bestScore = -1e30f;
        for (int dlt = -tol; dlt <= tol; dlt += coarse) { const float sc = score (target + dlt); if (sc > bestScore) { bestScore = sc; best = target + dlt; } }
        const double c0 = best;   // then to the sample
        for (int dlt = -coarse + 1; dlt < coarse; ++dlt) { const float sc = score (c0 + dlt); if (sc > bestScore) { bestScore = sc; best = c0 + dlt; } }
        return best;
    }

    double sr = 48000.0;
    const WaveSample* wav = nullptr;
    BreakData d;
    std::array<Voice, kVoices> voices {};
    std::vector<float> bus[2];
    VintageSampler colourFx;
    std::array<float, P_COUNT> colourV {};
    FilterUnit filter;
    int fModel = -1, fType = -1, fSlope = -1, fChar = -1;
    int tail = 0, lastSlice = 0;
    float peakShown = 0.0f;
};

} // namespace tg
