#pragma once
// Wave Mutation: per-voice waveshaping between the oscillator mixer and the filter.
// Drive, bend, asymmetry, fold, shape and rectify run 2x oversampled (they create
// harmonics that would otherwise alias); bit depth and sample-rate reduction run at the
// normal rate afterwards, because their aliasing is the point.
#include <JuceHeader.h>
#include <cmath>

namespace tg
{

struct WaveMutatorParams
{
    float mix = 0, drive = 0, fold = 0, shape = 0, bend = 0, asym = 0, rect = 0, bits = 16, down = 1;
    bool active() const { return mix > 1.0e-4f; }
};

class WaveMutator
{
public:
    static constexpr int kMaxBlock = 16;

    void prepare (double sampleRate)
    {
        juce::ignoreUnused (sampleRate);
        os.initProcessing ((size_t) kMaxBlock);
        latency = juce::jlimit (0, kDryLen - 1, (int) std::lround (os.getLatencyInSamples()));
        reset();
    }

    void reset()
    {
        os.reset();
        for (auto& ch : dryBuf) std::fill (std::begin (ch), std::end (ch), 0.0f);
        dryPos = 0;
        for (int c = 0; c < 2; ++c) { dcX[c] = dcY[c] = 0.0f; held[c] = 0.0f; }
        holdAcc = 1.0e9f;
        wasActive = false;
    }

    int getLatency() const { return latency; }

    // In place on L/R (n <= kMaxBlock). R is processed too when stereo.
    void process (float* L, float* R, int n, bool stereo, const WaveMutatorParams& p, double sr)
    {
        if (! p.active()) { if (wasActive) reset(); return; }
        wasActive = true;

        // remember the dry signal, delayed by the oversampler's latency for the dry/wet mix
        float dryL[kMaxBlock], dryR[kMaxBlock];
        for (int i = 0; i < n; ++i)
        {
            dryBuf[0][(size_t) dryPos] = L[i];
            dryBuf[1][(size_t) dryPos] = R[i];
            const int rd = (dryPos - latency + kDryLen) % kDryLen;
            dryL[i] = dryBuf[0][(size_t) rd];
            dryR[i] = dryBuf[1][(size_t) rd];
            dryPos = (dryPos + 1) % kDryLen;
        }

        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        auto up = os.processSamplesUp (block);

        const float gain = 1.0f + 15.0f * p.drive * p.drive;
        const float bendExp = std::exp2 (-1.5f * p.bend);
        const float foldK = (float) juce::MathConstants<double>::halfPi * (1.0f + 6.0f * p.fold);
        const float foldMix = std::sqrt (juce::jlimit (0.0f, 1.0f, p.fold));
        const float shapeK = 1.0f + 9.0f * p.shape;
        const float shapeNorm = 1.0f / std::tanh (shapeK);
        const float bias = 0.6f * p.asym;
        const int chs = 2;   // both channels always, so a voice that turns stereo mid-note has clean state
        for (int c = 0; c < chs; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                float x = d[i] * gain + bias;
                if (p.bend != 0.0f) x = (x < 0 ? -1.0f : 1.0f) * std::pow (std::abs (x), bendExp);
                if (foldMix > 0.0f) x += (std::sin (foldK * x) - x) * foldMix;
                if (p.shape > 0.0f) x += (std::tanh (shapeK * x) * shapeNorm - x) * p.shape;
                if (p.rect > 0.0f) x += (std::abs (x) - x) * p.rect;
                d[i] = juce::jlimit (-4.0f, 4.0f, x);
            }
        }
        os.processSamplesDown (block);

        const float dcR = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 10.0 / sr);
        const bool crush = p.bits < 15.95f;
        const float q = std::exp2 (juce::jlimit (1.0f, 16.0f, p.bits) - 1.0f);
        const bool decim = p.down > 1.01f;
        for (int i = 0; i < n; ++i)
        {
            bool take = true;
            if (decim) { holdAcc += 1.0f; take = holdAcc >= p.down; if (take) holdAcc -= p.down * std::floor (holdAcc / p.down); }
            for (int c = 0; c < chs; ++c)
            {
                float* d = chans[c];
                // the asymmetry and rectifier add DC: block it before it reaches the filter
                float y = d[i] - dcX[c] + dcR * dcY[c];
                dcX[c] = d[i]; dcY[c] = y;
                if (crush) y = std::round (y * q) / q;
                if (decim) { if (take) held[c] = y; y = held[c]; }
                d[i] = y;
            }
            if (! stereo) R[i] = L[i];
            L[i] = dryL[i] + (L[i] - dryL[i]) * p.mix;
            R[i] = dryR[i] + (R[i] - dryR[i]) * p.mix;
        }
    }

private:
    juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, true };
    static constexpr int kDryLen = 64;
    float dryBuf[2][kDryLen] {};
    int dryPos = 0, latency = 0;
    float dcX[2] {}, dcY[2] {}, held[2] {};
    float holdAcc = 1.0e9f;
    bool wasActive = false;
};

} // namespace tg
