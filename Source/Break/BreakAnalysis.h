#pragma once
// Break analysis: the tempo (from the length and bar count, or typed in), the slice points
// (transients, equal divisions or manual markers), and a rough kick / snare score per slice for the
// Jungle edit generator. Plus the factory demo break, synthesised here from scratch (no samples of
// anyone's record are shipped).
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "BreakStore.h"
#include "../EngineTypes.h"

namespace tg
{
namespace breaks
{

// The tempo: typed in, or from a bar count, or (Auto) the bar count that puts it nearest 130 BPM
// within 88-190. Auto assumes the file is trimmed to whole bars of 4/4.
inline double tempoFor (double seconds, int barsChoice, double typed, int& barsOut)
{
    static const int barsOf[] = { 0, 1, 2, 4, 8, 16 };
    barsOut = 0;
    if (seconds <= 0.01) return 0.0;
    if (typed > 1.0) { barsOut = (int) std::lround (typed * seconds / 240.0); return typed; }
    const int b = barsOf[juce::jlimit (0, 5, barsChoice)];
    if (b > 0) { barsOut = b; return b * 240.0 / seconds; }
    double best = 0.0, bestD = 1e9;
    for (int k : { 1, 2, 4, 8, 16 })
    {
        const double bpm = k * 240.0 / seconds;
        if (bpm < 88.0 || bpm > 190.0) continue;
        if (std::abs (bpm - 130.0) < bestD) { bestD = std::abs (bpm - 130.0); best = bpm; barsOut = k; }
    }
    if (best <= 0.0) { barsOut = std::max (1, (int) std::lround (130.0 * seconds / 240.0)); best = barsOut * 240.0 / seconds; }
    return best;
}

// Transient onsets (frames), strongest first when there are more than fit
inline std::vector<int> transients (const WaveSample& w, float sens)
{
    const int hop = 256, n = w.length / hop;
    std::vector<float> e ((size_t) std::max (1, n), -100.0f);
    const float* a = w.ch[0].data();
    const float* b = w.ch[w.numChannels > 1 ? 1 : 0].data();
    float prev = 0.0f, peakDb = -100.0f;
    for (int h = 0; h < n; ++h)
    {
        double acc = 0.0;
        for (int i = h * hop; i < (h + 1) * hop; ++i)
        {
            const float x = 0.5f * (a[i] + b[i]);
            const float d = x - prev; prev = x;   // a first difference: weights the attack's high end
            acc += (double) d * d;
        }
        e[(size_t) h] = (float) (10.0 * std::log10 (acc / hop + 1e-12));
        peakDb = std::max (peakDb, e[(size_t) h]);
    }
    const float thr = 12.0f - 9.0f * juce::jlimit (0.0f, 1.0f, sens);              // rise needed: 12 dB .. 3 dB
    const int minGap = (int) ((0.12 - 0.08 * juce::jlimit (0.0f, 1.0f, sens)) * w.sampleRate);   // 120 .. 40 ms apart
    struct On { int at; float strength; };
    std::vector<On> on;
    for (int h = 2; h < n; ++h)
    {
        const float rise = e[(size_t) h] - std::min (e[(size_t) h - 1], e[(size_t) h - 2]);
        if (rise < thr || e[(size_t) h] < peakDb - 45.0f) continue;
        if (h + 1 < n && e[(size_t) h + 1] - std::min (e[(size_t) h], e[(size_t) h - 1]) > rise) continue;   // the peak of the rise is next
        // the attack starts where the level first passes a tenth of what follows
        int s0 = std::max (0, (h - 1) * hop), s1 = std::min (w.length - 1, (h + 1) * hop);
        float pk = 0.0f; for (int i = s0; i <= s1; ++i) pk = std::max (pk, std::abs (a[i]));
        int at = s0; for (int i = s0; i <= s1; ++i) if (std::abs (a[i]) > 0.1f * pk) { at = i; break; }
        at = std::max (0, at - 32);
        if (! on.empty() && at - on.back().at < minGap) { if (rise > on.back().strength) on.back() = { at, rise }; continue; }
        on.push_back ({ at, rise });
    }
    if ((int) on.size() > BreakSlot::kSlices - 1)
    {
        std::sort (on.begin(), on.end(), [] (const On& x, const On& y) { return x.strength > y.strength; });
        on.resize ((size_t) BreakSlot::kSlices - 1);
    }
    std::vector<int> out;
    for (auto& o : on) out.push_back (o.at);
    std::sort (out.begin(), out.end());
    return out;
}

// Works out one break's slices (into b.start / b.nSlices), its tempo, its loudness and each slice's
// kick / snare / hat score
inline void analyse (const WaveSample* w, int mode, float sens, int barsChoice, double typedBpm, BreakSlot& b)
{
    b.nSlices = 0; b.origBpm = 0.0; b.bars = 0; b.rms = 0.0f;
    std::fill (std::begin (b.start), std::end (b.start), 0);
    if (w == nullptr || w->length < 64) return;
    b.sampleRate = w->sampleRate;
    b.origBpm = tempoFor (w->duration(), barsChoice, typedBpm, b.bars);
    std::vector<int> st { 0 };
    const int len = w->length;
    switch (mode)
    {
        case 1: case 2: case 3: case 4:
        {
            const int n = 4 << (mode - 1);   // 4, 8, 16, 32
            for (int k = 1; k < n; ++k) st.push_back ((int) ((int64_t) k * len / n));
            break;
        }
        case 5:
            for (int i = 0; i < b.nMarkers; ++i) { const int at = (int) (b.markers[i] * len); if (at > 64 && at < len - 64) st.push_back (at); }
            break;
        default:
            for (int at : transients (*w, sens)) if (at > 256) st.push_back (at);
            break;
    }
    std::sort (st.begin(), st.end());
    st.erase (std::unique (st.begin(), st.end(), [] (int x, int y) { return std::abs (x - y) < 64; }), st.end());
    b.nSlices = std::min ((int) st.size(), (int) BreakSlot::kSlices);
    for (int k = 0; k < b.nSlices; ++k) b.start[k] = st[(size_t) k];
    b.start[b.nSlices] = len;

    const float* a = w->ch[0].data();
    const float* r = w->ch[w->numChannels > 1 ? 1 : 0].data();
    {
        double e = 0.0; for (int i = 0; i < len; ++i) e += 0.5 * ((double) a[i] * a[i] + (double) r[i] * r[i]);
        b.rms = (float) std::sqrt (e / len);
    }
    // kick / snare scores from the first 60 ms of each slice: low end against the bright noisy part
    // (exactly as before three breaks, so factory patterns made from them don't change)
    float maxK = 1e-9f, maxS = 1e-9f, maxH = 1e-9f;
    const float cl = 1.0f - std::exp (-2.0f * 3.14159f * 150.0f / (float) w->sampleRate);
    const float ch = 1.0f - std::exp (-2.0f * 3.14159f * 2000.0f / (float) w->sampleRate);
    const float c6 = 1.0f - std::exp (-2.0f * 3.14159f * 6000.0f / (float) w->sampleRate);
    for (int k = 0; k < b.nSlices; ++k)
    {
        const int s0 = b.start[k], s1 = std::min (b.start[k + 1], s0 + (int) (0.06 * w->sampleRate));
        float lp = 0.0f, lp2 = 0.0f; double lo = 0.0, hi = 0.0;
        for (int i = s0; i < s1; ++i) { lp += (a[i] - lp) * cl; lp2 += (a[i] - lp2) * ch; lo += (double) lp * lp; const float h = a[i] - lp2; hi += (double) h * h; }
        const double mid = std::max (0.0, hi * 0.5);
        b.kick[k] = (float) (lo / (lo + hi + 1e-12) * std::sqrt (lo + hi));
        b.snare[k] = (float) (mid / (lo + hi + 1e-12) * std::sqrt (lo + hi));
        // hats: mostly above 6 kHz, little low end, and gone quickly (30-90 ms against the first 30 ms)
        const int s2 = std::min (b.start[k + 1], s0 + (int) (0.09 * w->sampleRate)), split = s0 + (int) (0.03 * w->sampleRate);
        float l6 = 0.0f, ll = 0.0f; double top = 0.0, low = 0.0, all = 0.0, early = 0.0, late = 0.0;
        for (int i = s0; i < s2; ++i)
        {
            l6 += (a[i] - l6) * c6; ll += (a[i] - ll) * cl;
            const float t = a[i] - l6;
            top += (double) t * t; low += (double) ll * ll; all += (double) a[i] * a[i];
            (i < split ? early : late) += (double) a[i] * a[i];
        }
        const double ring = late / (early + 1e-12);
        b.hat[k] = (float) (top / (all + 1e-12) * std::sqrt (all) * std::max (0.0, 1.0 - 4.0 * low / (all + 1e-12)) * std::max (0.1, 1.0 - ring));
        maxK = std::max (maxK, b.kick[k]); maxS = std::max (maxS, b.snare[k]); maxH = std::max (maxH, b.hat[k]);
    }
    for (int k = 0; k < b.nSlices; ++k) { b.kick[k] /= maxK; b.snare[k] /= maxS; b.hat[k] /= maxH; }
}

// ---- the factory demo break: two bars, synthesised (kick, snare with ghost notes, hats), at the given tempo
inline juce::AudioBuffer<float> makeDemoBreak (double sr = 44100.0, double bpm = 170.0, int variant = 0)
{
    const double step = 60.0 / bpm / 4.0;   // a 1/16
    const int len = (int) std::ceil (32 * step * sr);
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    uint32_t rng = 0x51ED27u + (uint32_t) variant * 7919u;
    auto rnd = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f * 2.0f - 1.0f; };
    auto add = [&] (int at, int c, float v) { if (at >= 0 && at < len) out.addSample (c, at, v); };
    auto kick = [&] (double t, float vel)
    {
        const int s0 = (int) (t * sr);
        double ph = 0.0;
        for (int i = 0; i < (int) (0.32 * sr); ++i)
        {
            const double x = i / sr;
            const double f = (variant == 1 ? 52.0 : 46.0) + 110.0 * std::exp (-x / 0.028);
            ph += f / sr;
            float v = (float) (std::sin (2.0 * juce::MathConstants<double>::pi * ph) * std::exp (-x / 0.16));
            if (i < (int) (0.004 * sr)) v += 0.35f * rnd() * (1.0f - (float) i / (float) (0.004 * sr));   // beater click
            v = std::tanh (1.6f * v) * vel;
            add (s0 + i, 0, v); add (s0 + i, 1, v);
        }
    };
    auto snare = [&] (double t, float vel)
    {
        const int s0 = (int) (t * sr);
        float bp1 = 0.0f, bp2 = 0.0f;
        for (int i = 0; i < (int) (0.24 * sr); ++i)
        {
            const double x = i / sr;
            const float tone = (float) (std::sin (2.0 * juce::MathConstants<double>::pi * ((variant == 2 ? 210.0 : 186.0) * x + 2.0 * std::exp (-x / 0.01))) * std::exp (-x / 0.07));
            const float nz = rnd();
            bp1 += (nz - bp1) * 0.45f; bp2 += (bp1 - bp2) * 0.45f;
            const float noise = (nz - bp2) * (float) std::exp (-x / 0.11);
            const float v = std::tanh (1.3f * (0.55f * tone + 0.75f * noise)) * vel;
            add (s0 + i, 0, v * 0.95f); add (s0 + i, 1, v);
        }
    };
    auto hat = [&] (double t, float vel, bool open)
    {
        const int s0 = (int) (t * sr);
        float hp = 0.0f;
        const double dec = open ? 0.18 : 0.035;
        for (int i = 0; i < (int) ((open ? 0.3 : 0.08) * sr); ++i)
        {
            const float nz = rnd();
            hp += (nz - hp) * 0.25f;
            const float v = (nz - hp) * (float) std::exp (-(i / sr) / dec) * vel * 0.35f;
            add (s0 + i, 0, v * 0.7f); add (s0 + i, 1, v);
        }
    };
    const double swing = step * (variant == 2 ? 0.14 : 0.08);   // a little push on the off 16ths
    auto at = [&] (int s) { return s * step + ((s & 1) ? swing : 0.0); };
    if (variant == 1)
    {
        // "2-step": kick on 1 and the "and" of 2, a tight backbeat, 16th hats with an open one per bar
        for (int s : { 0, 6, 16, 22, 26 }) kick (at (s), s == 26 ? 0.75f : 1.0f);
        for (int s : { 4, 12, 20, 28 }) snare (at (s), 1.0f);
        for (int s : { 15, 31 }) snare (at (s), 0.3f);
        for (int s = 0; s < 32; ++s) hat (at (s), (s % 2 == 0) ? 0.7f : 0.4f, s == 10 || s == 26);
    }
    else if (variant == 2)
    {
        // "rollers": a sparse kick under a rolling bed of ghost snares and swung hats
        for (int s : { 0, 10, 16, 26 }) kick (at (s), 1.0f);
        for (int s : { 4, 12, 20, 28 }) snare (at (s), 0.9f);
        for (int s : { 2, 3, 7, 9, 11, 14, 15, 18, 19, 23, 25, 27, 30, 31 }) snare (at (s), 0.18f + 0.05f * (float) (s % 4));
        for (int s = 0; s < 32; s += 2) hat (at (s), (s % 4 == 0) ? 0.45f : 0.75f, false);
    }
    else
    {
        // an original two-bar pattern (16ths): kicks, backbeat snares with ghosts, a shuffled hat line
        const int kicks[] = { 0, 10, 16, 18, 26, 27 };
        const int snares[] = { 4, 12, 20, 28 };
        const int ghosts[] = { 7, 9, 15, 23, 25, 30 };
        for (int s : kicks) kick (at (s), s == 27 ? 0.7f : 1.0f);
        for (int s : snares) snare (at (s), 0.95f);
        for (int s : ghosts) snare (at (s), 0.28f + 0.06f * (float) (s % 3));
        for (int s = 0; s < 32; s += 2) hat (at (s), (s % 4 == 2) ? 0.9f : 0.6f, s == 14 || s == 30);
        for (int s = 1; s < 32; s += 4) hat (at (s), 0.35f, false);
    }
    // a short room
    for (int c = 0; c < 2; ++c)
    {
        std::vector<float> dry (out.getReadPointer (c), out.getReadPointer (c) + len);
        const int d1 = (int) ((0.011 + 0.003 * c) * sr), d2 = (int) ((0.023 + 0.004 * c) * sr);
        for (int i = 0; i < len; ++i)
            out.addSample (c, i, 0.18f * (i >= d1 ? dry[(size_t) (i - d1)] : 0.0f) + 0.1f * (i >= d2 ? dry[(size_t) (i - d2)] : 0.0f));
    }
    const float pk = std::max (out.getMagnitude (0, 0, len), out.getMagnitude (1, 0, len));
    if (pk > 0.0f) out.applyGain (0.89f / pk);
    return out;
}

inline juce::MemoryBlock toWav16 (const juce::AudioBuffer<float>& b, double sr)
{
    juce::MemoryBlock mb;
    auto* os = new juce::MemoryOutputStream (mb, false);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, sr, (unsigned int) b.getNumChannels(), 16, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    else delete os;
    return mb;
}

} // namespace breaks
} // namespace tg
