#pragma once
// Capture / Resample: edit tools and pitch detection for audio recorded from the synth.
// (The recording itself happens in the processor's audio callback.)
#include <JuceHeader.h>
#include <cmath>

namespace tg
{

struct CaptureSettings
{
    float trimStart = 0.0f, trimEnd = 1.0f;     // fractions of the recording
    float fadeInMs = 2.0f, fadeOutMs = 10.0f;
    bool reverse = false, normalise = true, zeroCross = true;
};

namespace capture
{
    // Nearest point at or after 'i' (searching both ways, up to 'range') where the mono signal crosses zero upwards.
    inline int snapToZero (const juce::AudioBuffer<float>& b, int i, int range)
    {
        const int n = b.getNumSamples();
        auto mono = [&] (int k) { float s = 0; for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample (c, k); return s; };
        for (int d = 0; d <= range; ++d)
            for (int k : { i + d, i - d })
                if (k > 0 && k < n && mono (k - 1) <= 0.0f && mono (k) > 0.0f) return k;
        return juce::jlimit (0, n, i);
    }

    // Applies trim (optionally snapped to zero crossings), reverse, fades and normalisation.
    inline juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& raw, const CaptureSettings& s, double sr)
    {
        const int n = raw.getNumSamples();
        if (n < 2) return {};
        int a = (int) std::floor (juce::jlimit (0.0f, 1.0f, std::min (s.trimStart, s.trimEnd)) * n);
        int b = (int) std::ceil (juce::jlimit (0.0f, 1.0f, std::max (s.trimStart, s.trimEnd)) * n);
        if (s.zeroCross)
        {
            const int range = (int) (0.02 * sr);
            a = snapToZero (raw, a, range);
            b = snapToZero (raw, b, range);
        }
        b = juce::jlimit (0, n, b);
        if (b - a < 16) { a = juce::jlimit (0, n - 16, a); b = a + 16; }
        juce::AudioBuffer<float> out (raw.getNumChannels(), b - a);
        for (int c = 0; c < raw.getNumChannels(); ++c) out.copyFrom (c, 0, raw, c, a, b - a);
        if (s.reverse) out.reverse (0, out.getNumSamples());
        const int len = out.getNumSamples();
        const int fi = juce::jlimit (0, len / 2, (int) (s.fadeInMs * 0.001 * sr));
        const int fo = juce::jlimit (0, len / 2, (int) (s.fadeOutMs * 0.001 * sr));
        if (fi > 0) out.applyGainRamp (0, fi, 0.0f, 1.0f);
        if (fo > 0) out.applyGainRamp (len - fo, fo, 1.0f, 0.0f);
        if (s.normalise)
        {
            const float pk = out.getMagnitude (0, len);
            if (pk > 1.0e-6f) out.applyGain (0.89f / pk);   // -1 dBFS
        }
        return out;
    }

    // Pitch (Hz) by the YIN method on a window from the middle of the sound; 0 if none found.
    inline double detectPitch (const juce::AudioBuffer<float>& b, double sr)
    {
        const int n = b.getNumSamples();
        const int W = std::min (4096, n / 2);
        const int maxTau = std::min ((int) (sr / 30.0), n - W - 1), minTau = std::max (2, (int) (sr / 2000.0));
        if (W < 256 || maxTau <= minTau) return 0.0;
        const int start = std::max (0, n / 2 - W / 2 - maxTau / 2);
        std::vector<float> x ((size_t) (W + maxTau + 1));
        for (size_t i = 0; i < x.size(); ++i)
        {
            float s = 0; for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample (c, std::min (n - 1, start + (int) i));
            x[i] = s;
        }
        std::vector<double> d ((size_t) maxTau + 1, 0.0);
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double acc = 0.0;
            for (int i = 0; i < W; ++i) { const double e = x[(size_t) i] - x[(size_t) (i + tau)]; acc += e * e; }
            d[(size_t) tau] = acc;
        }
        // cumulative mean normalised difference
        double run = 0.0;
        std::vector<double> dn ((size_t) maxTau + 1, 1.0);
        for (int tau = 1; tau <= maxTau; ++tau) { run += d[(size_t) tau]; dn[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0; }
        int best = -1;
        for (int tau = minTau; tau < maxTau; ++tau)
            if (dn[(size_t) tau] < 0.15)
            {
                while (tau + 1 < maxTau && dn[(size_t) (tau + 1)] < dn[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best < 0) return 0.0;
        // parabolic interpolation around the minimum
        const double y0 = dn[(size_t) (best - 1)], y1 = dn[(size_t) best], y2 = dn[(size_t) (best + 1)];
        const double den = y0 - 2.0 * y1 + y2;
        const double t = best + (std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);
        return sr / t;
    }

    // One cycle at the detected pitch (exact, fractional period), starting on an upward zero
    // crossing near the middle, resampled to kCycleLen samples.
    static constexpr int kCycleLen = 2048;
    inline juce::AudioBuffer<float> singleCycle (const juce::AudioBuffer<float>& b, double sr, double hz)
    {
        if (hz <= 0.0) return {};
        const double period = sr / hz;
        const int n = b.getNumSamples();
        if (period < 8 || period * 2 + 4 > n) return {};
        const int s0 = juce::jlimit (0, n - (int) period - 2, snapToZero (b, n / 2 - (int) period, (int) period));
        juce::AudioBuffer<float> out (b.getNumChannels(), kCycleLen);
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < kCycleLen; ++i)
            {
                const double rp = s0 + period * i / kCycleLen;
                const int i0 = (int) rp;
                const float fr = (float) (rp - i0);
                out.setSample (c, i, b.getSample (c, i0) + (b.getSample (c, std::min (n - 1, i0 + 1)) - b.getSample (c, i0)) * fr);
            }
        const int period2 = kCycleLen;
        const float pk = out.getMagnitude (0, period2);
        if (pk > 1.0e-6f) out.applyGain (0.89f / pk);
        return out;
    }

    // 24-bit WAV bytes, so the result can be loaded into a wavetable slot and saved in patches.
    inline juce::MemoryBlock toWav (const juce::AudioBuffer<float>& b, double sr)
    {
        juce::MemoryBlock mb;
        {
            auto os = std::make_unique<juce::MemoryOutputStream> (mb, false);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned int) b.getNumChannels(), 24, {}, 0));
            if (w != nullptr)
            {
                os.release();
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }
        return mb;
    }
}

} // namespace tg
