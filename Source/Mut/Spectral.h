#pragma once
// Spectral: a short-time Fourier (phase vocoder) stage on the summed voices, before the
// effects. Frame sizes 512-4096 with 4x overlap; the latency (one frame) is reported to
// the host while the stage is switched on, and the dry signal is delayed to match.
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include <array>
#include <memory>

namespace tg
{

struct SpectralParams
{
    bool on = false, freeze = false;
    int sizeIndex = 2;           // 512, 1024, 2048, 4096
    float mix = 1, blur = 0, shiftHz = 0, scramble = 0, tilt = 0, morph = 0, formant = 0, feedback = 0;
};

inline int spectralSize (int index) { return 512 << juce::jlimit (0, 3, index); }
// Quality Eco caps the FFT at 1024 points (less CPU, less latency)
inline int effectiveSpectralSize (int sizeIndex, int quality) { return quality == 0 ? std::min (sizeIndex, 1) : sizeIndex; }

class Spectral
{
public:
    static constexpr int kMaxN = 4096;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int o = 0; o < 4; ++o) ffts[(size_t) o] = std::make_unique<juce::dsp::FFT> (9 + o);
        for (int c = 0; c < 2; ++c)
        {
            inRing[c].assign (kMaxN, 0.0f); outRing[c].assign (kMaxN, 0.0f);
            lastPhase[c].assign (kMaxN / 2 + 1, 0.0f); synPhase[c].assign (kMaxN / 2 + 1, 0.0f);
            smooth[c].assign (kMaxN / 2 + 1, 0.0f); prevOut[c].assign (kMaxN / 2 + 1, 0.0f);
            capMag[c].assign (kMaxN / 2 + 1, 0.0f); capFreq[c].assign (kMaxN / 2 + 1, 0.0f);
        }
        frame.assign (kMaxN * 2, 0.0f);
        mag.assign (kMaxN / 2 + 1, 0.0f); freq.assign (kMaxN / 2 + 1, 0.0f);
        omag.assign (kMaxN / 2 + 1, 0.0f); ofreq.assign (kMaxN / 2 + 1, 0.0f);
        env.assign (kMaxN / 2 + 1, 0.0f); perm.assign (kMaxN / 2 + 1, 0);
        window.assign (kMaxN, 0.0f);
        N = 0;
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
            std::fill (lastPhase[c].begin(), lastPhase[c].end(), 0.0f);
            std::fill (synPhase[c].begin(), synPhase[c].end(), 0.0f);
            std::fill (smooth[c].begin(), smooth[c].end(), 0.0f);
            std::fill (prevOut[c].begin(), prevOut[c].end(), 0.0f);
        }
        wpos = 0; hopCount = 0; wasFrozen = false; hasCapture = false;
        wasOn = false;
    }

    int latency (const SpectralParams& p) const { return p.on ? spectralSize (p.sizeIndex) : 0; }

    void process (float* L, float* R, int n, const SpectralParams& p)
    {
        if (! p.on) { if (wasOn) reset(); return; }
        const int want = spectralSize (p.sizeIndex);
        if (want != N || ! wasOn) setSize (want);
        wasOn = true;
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float dry = inRing[c][(size_t) wpos];      // the input from exactly N samples ago
                const float wet = outRing[c][(size_t) wpos];
                outRing[c][(size_t) wpos] = 0.0f;
                inRing[c][(size_t) wpos] = io[c][i];
                io[c][i] = dry + (ceiling (wet) - dry) * p.mix;
            }
            wpos = (wpos + 1) % N;
            if (++hopCount >= H) { hopCount = 0; processFrame (p); }
        }
    }

private:
    void setSize (int newN)
    {
        N = newN; H = N / 4;
        order = 9 + (int) std::lround (std::log2 (N / 512.0));
        for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / N);   // periodic Hann
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
            std::fill (lastPhase[c].begin(), lastPhase[c].end(), 0.0f);
            std::fill (synPhase[c].begin(), synPhase[c].end(), 0.0f);
            std::fill (smooth[c].begin(), smooth[c].end(), 0.0f);
            std::fill (prevOut[c].begin(), prevOut[c].end(), 0.0f);
        }
        wpos = 0; hopCount = 0; hasCapture = false; wasFrozen = false;
    }

    inline uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

    // untouched below 1.5, then a soft knee that never exceeds 2.0
    static inline float ceiling (float x)
    {
        const float a = std::abs (x);
        if (a <= 1.5f) return x;
        return (x < 0 ? -1.0f : 1.0f) * (1.5f + 0.5f * std::tanh ((a - 1.5f) * 2.0f));
    }

    static inline float wrapPi (float x)
    {
        const float twoPi = 2.0f * juce::MathConstants<float>::pi;
        x -= twoPi * std::floor ((x + juce::MathConstants<float>::pi) / twoPi);
        return x;
    }

    void processFrame (const SpectralParams& p)
    {
        const int bins = N / 2;
        const float twoPi = 2.0f * juce::MathConstants<float>::pi;
        const float binHz = (float) (sr / N);
        const float expect = twoPi * (float) H / (float) N;          // phase advance per hop, per bin index
        const bool neutral = ! p.freeze && p.blur <= 0.0f && std::abs (p.shiftHz) < 0.01f && p.scramble <= 0.0f
                          && std::abs (p.tilt) < 1.0e-4f && p.morph <= 0.0f && std::abs (p.formant) < 0.01f && p.feedback <= 0.0f;
        const bool freezeStart = p.freeze && ! wasFrozen;
        wasFrozen = p.freeze;

        // scramble: a fresh local shuffle of the bins every frame (same for both channels)
        if (p.scramble > 0.0f)
        {
            const int range = 1 + (int) (p.scramble * 48.0f);
            for (int k = 0; k <= bins; ++k) perm[(size_t) k] = k;
            for (int k = 1; k < bins; ++k)
            {
                const int j = juce::jlimit (1, bins - 1, k + (int) (rnd() % (uint32_t) (2 * range + 1)) - range);
                std::swap (perm[(size_t) k], perm[(size_t) j]);
            }
        }

        for (int c = 0; c < 2; ++c)
        {
            // gather the frame (oldest sample first) and analyse
            for (int i = 0; i < N; ++i) frame[(size_t) i] = inRing[c][(size_t) ((wpos + i) % N)] * window[(size_t) i];
            std::fill (frame.begin() + N, frame.begin() + 2 * N, 0.0f);
            ffts[(size_t) (order - 9)]->performRealOnlyForwardTransform (frame.data(), true);

            if (neutral)
            {
                // nothing to change: keep the phases exactly (perfect reconstruction)
                for (int k = 0; k <= bins; ++k)
                {
                    const float re = frame[(size_t) (2 * k)], im = frame[(size_t) (2 * k + 1)];
                    const float ph = std::atan2 (im, re);
                    lastPhase[c][(size_t) k] = ph; synPhase[c][(size_t) k] = ph;
                    prevOut[c][(size_t) k] = smooth[c][(size_t) k] = std::sqrt (re * re + im * im);
                }
            }
            else
            {
                for (int k = 0; k <= bins; ++k)
                {
                    const float re = frame[(size_t) (2 * k)], im = frame[(size_t) (2 * k + 1)];
                    const float m = std::sqrt (re * re + im * im), ph = std::atan2 (im, re);
                    const float d = wrapPi (ph - lastPhase[c][(size_t) k] - expect * k);
                    lastPhase[c][(size_t) k] = ph;
                    mag[(size_t) k] = m;
                    freq[(size_t) k] = (float) k + d / expect;               // true frequency, in bins
                }
                double eIn = 0.0;
                for (int k = 0; k <= bins; ++k) eIn += (double) mag[(size_t) k] * mag[(size_t) k];
                if (freezeStart || (! hasCapture && p.morph > 0.0f))
                {
                    std::copy (mag.begin(), mag.begin() + bins + 1, capMag[c].begin());
                    std::copy (freq.begin(), freq.begin() + bins + 1, capFreq[c].begin());
                    if (c == 1) hasCapture = true;
                }
                if (p.freeze)
                {
                    std::copy (capMag[c].begin(), capMag[c].begin() + bins + 1, mag.begin());
                    std::copy (capFreq[c].begin(), capFreq[c].begin() + bins + 1, freq.begin());
                }
                if (p.freeze || p.morph > 0.0f)
                {
                    double eCap = 0.0;
                    for (int k = 0; k <= bins; ++k) eCap += (double) capMag[c][(size_t) k] * capMag[c][(size_t) k];
                    eIn = std::max (eIn, eCap);
                }
                else if (p.morph > 0.0f)
                    for (int k = 0; k <= bins; ++k) mag[(size_t) k] += (capMag[c][(size_t) k] - mag[(size_t) k]) * p.morph;

                // feedback: the last output spectrum is fed back into this one
                if (p.feedback > 0.0f)
                    for (int k = 0; k <= bins; ++k) mag[(size_t) k] += p.feedback * prevOut[c][(size_t) k];

                // blur: magnitudes smeared over time
                if (p.blur > 0.0f)
                {
                    const float a = 1.0f - 0.97f * p.blur;
                    for (int k = 0; k <= bins; ++k) { auto& s = smooth[c][(size_t) k]; s += (mag[(size_t) k] - s) * a; mag[(size_t) k] = s; }
                }

                // shift every partial by the same number of Hz, and scramble
                std::fill (omag.begin(), omag.begin() + bins + 1, 0.0f);
                const float shiftBins = p.shiftHz / binHz;
                for (int k = 0; k <= bins; ++k)
                {
                    const int src = p.scramble > 0.0f ? perm[(size_t) k] : k;
                    const float f = freq[(size_t) src] + shiftBins;
                    const int j = (int) std::lround ((float) k + shiftBins);
                    if (j < 1 || j >= bins) continue;
                    if (mag[(size_t) src] > omag[(size_t) j]) { ofreq[(size_t) j] = f; }
                    omag[(size_t) j] += mag[(size_t) src];
                }

                // formant shift: move the spectral envelope, keep the partials where they are
                if (std::abs (p.formant) >= 0.01f)
                {
                    const int w = std::max (2, N / 128);
                    double acc = 0.0;
                    for (int k = 0; k <= std::min (bins, w); ++k) acc += omag[(size_t) k];
                    for (int k = 0; k <= bins; ++k)
                    {
                        const int a0 = k - w - 1, a1 = k + w;
                        if (a1 <= bins && k > 0) acc += omag[(size_t) a1];
                        if (a0 >= 0) acc -= omag[(size_t) a0];
                        env[(size_t) k] = (float) std::max (1.0e-9, acc / (2 * w + 1));
                    }
                    const float ratio = std::exp2 (p.formant / 12.0f);
                    for (int k = 1; k < bins; ++k)
                    {
                        const float sk = k / ratio;
                        const int s0 = (int) sk;
                        if (s0 >= bins) { omag[(size_t) k] = 0.0f; continue; }
                        const float e = env[(size_t) s0] + (env[(size_t) std::min (bins, s0 + 1)] - env[(size_t) s0]) * (sk - s0);
                        omag[(size_t) k] *= std::min (20.0f, e / env[(size_t) k]);
                    }
                }

                // tilt: +/- 6 dB per octave around 1 kHz
                if (std::abs (p.tilt) >= 1.0e-4f)
                    for (int k = 1; k <= bins; ++k) omag[(size_t) k] *= std::min (16.0f, std::pow (std::max (20.0f, k * binHz) / 1000.0f, p.tilt));

                // safety: feedback, blur, tilt and bins piling up can add a lot of energy; cap the frame
                // at +12 dB over what came in (or over the held spectrum when frozen / morphing)
                double eOut = 0.0;
                for (int k = 0; k <= bins; ++k) eOut += (double) omag[(size_t) k] * omag[(size_t) k];
                const float cap = eOut > 16.0 * eIn + 1.0e-12 ? (float) std::sqrt (16.0 * eIn / eOut) : 1.0f;

                // resynthesise: each bin's phase runs at its (possibly shifted) frequency
                for (int k = 0; k <= bins; ++k)
                {
                    const float m = std::min (omag[(size_t) k] * cap, 1.0e4f);
                    const float f = omag[(size_t) k] > 0.0f ? ofreq[(size_t) k] : (float) k;
                    float& sp = synPhase[c][(size_t) k];
                    sp = wrapPi (sp + expect * f);
                    prevOut[c][(size_t) k] = m * 0.98f;
                    frame[(size_t) (2 * k)] = m * std::cos (sp);
                    frame[(size_t) (2 * k + 1)] = m * std::sin (sp);
                }
            }

            ffts[(size_t) (order - 9)]->performRealOnlyInverseTransform (frame.data());
            // Hann analysis x Hann synthesis at 4x overlap sums to 1.5
            const float g = 1.0f / 1.5f;
            for (int i = 0; i < N; ++i)
            {
                float y = frame[(size_t) i] * window[(size_t) i] * g;
                if (! std::isfinite (y)) y = 0.0f;
                outRing[c][(size_t) ((wpos + i) % N)] += y;
            }
        }
    }

    double sr = 48000.0;
    std::array<std::unique_ptr<juce::dsp::FFT>, 4> ffts;
    std::vector<float> inRing[2], outRing[2], lastPhase[2], synPhase[2], smooth[2], prevOut[2], capMag[2], capFreq[2];
    std::vector<float> frame, mag, freq, omag, ofreq, env, window;
    std::vector<int> perm;
    int N = 0, H = 0, order = 11, wpos = 0, hopCount = 0;
    bool wasFrozen = false, hasCapture = false, wasOn = false;
    uint32_t rng = 0x9E3779B9u;
};

} // namespace tg
