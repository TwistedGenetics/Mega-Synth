#pragma once
// DNA Splice: builds a new waveform from two of the voice's own sources (A and B, any
// oscillator or wavetable, at the note's pitch) in one of seven ways. Runs per voice,
// after the oscillator mix and before Wave Mutation.
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "../DSP.h"

namespace tg
{

enum DnaMode { DNA_Waveform, DNA_Crossover, DNA_Harmonic, DNA_Spectral, DNA_Transient, DNA_AmpDna, DNA_Morph, DNA_COUNT };

struct DnaParams
{
    float mix = 0, amount = 0.5f, chr = 0.5f;
    int mode = 0, srcA = 0, srcB = 1;
    bool active() const { return mix > 1.0e-4f; }
};

class DnaSplice
{
public:
    static constexpr int kBands = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dlLen = juce::nextPowerOfTwo ((int) (sr / 15.0) + 8);
        dlA.assign ((size_t) dlLen, 0.0f);
        dlB.assign ((size_t) dlLen, 0.0f);
        bandsSet = false; lastFc = -1.0;
        geneC = 1.0f - std::exp (-1.0f / (float) (0.004 * sr));
        reset();
    }

    void reset()
    {
        std::fill (dlA.begin(), dlA.end(), 0.0f);
        std::fill (dlB.begin(), dlB.end(), 0.0f);
        dlPos = 0;
        ph = 0.0; gene = 0.0f; geneTarget = 0.0f;
        for (auto* b : { &lp2, &hp2, &lp4[0], &lp4[1], &hp4[0], &hp4[1] }) b->reset();
        for (int k = 0; k < kBands; ++k) { bpA[k].reset(); bpB[k].reset(); envA[k] = envB[k] = 0.0f; }
        ampA = ampB = 0.0f;
        rng = 0x1234567u;
    }

    // Set up the filters / timing for one mode for the coming block.
    void configure (int mode, const DnaParams& p, double noteHz)
    {
        hz = juce::jlimit (8.0, sr * 0.45, noteHz);
        amount = juce::jlimit (0.0f, 1.0f, p.amount);
        chr = juce::jlimit (0.0f, 1.0f, p.chr);
        switch (mode)
        {
            case DNA_Crossover:
            {
                const double fc = juce::jlimit (30.0, sr * 0.45, hz * std::exp2 (7.0 * amount));
                if (std::abs (fc - lastFc) > 0.01)
                {
                    lastFc = fc;
                    lp2.set (Biquad::LP, fc, -6.0206, sr);   // Q 0.5: Linkwitz-Riley 12 dB
                    hp2.set (Biquad::HP, fc, -6.0206, sr);
                    for (auto& b : lp4) b.set (Biquad::LP, fc, -3.0103, sr);   // Butterworth pairs: Linkwitz-Riley 24 dB
                    for (auto& b : hp4) b.set (Biquad::HP, fc, -3.0103, sr);
                }
                break;
            }
            case DNA_Spectral:
            {
                if (! bandsSet)
                {
                    bandsSet = true;
                    const double top = std::min (8000.0, sr * 0.4);
                    for (int k = 0; k < kBands; ++k)
                    {
                        const double f = 180.0 * std::pow (top / 180.0, k / (double) (kBands - 1));
                        bpA[k].set (Biquad::BP, f, 3.0, sr);
                        bpB[k].set (Biquad::BP, f, 3.0, sr);
                    }
                }
                envCoef = 1.0f - std::exp (-1.0f / (float) ((0.002 + 0.06 * chr) * sr));
                break;
            }
            case DNA_AmpDna:
                envCoef = 1.0f - std::exp (-1.0f / (float) ((0.001 + 0.05 * chr) * sr));
                break;
            default: break;
        }
    }

    // Write the newest A and B into the history used by the harmonic mode (once per sample).
    inline void push (float a, float b)
    {
        dlA[(size_t) dlPos] = a; dlB[(size_t) dlPos] = b;
        dlPos = (dlPos + 1) & (dlLen - 1);
    }

    // Advance the per-voice cycle phasor (once per sample, after run()).
    inline void tick()
    {
        ph += hz / sr;
        if (ph >= 1.0)
        {
            ph -= std::floor (ph);
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            geneTarget = ((rng & 0xFFFF) / 65536.0f) < amount ? 1.0f : 0.0f;   // each cycle's "gene" comes from A or B
        }
        gene += (geneTarget - gene) * geneC;
    }

    // One sample of the splice for 'mode'. t = seconds since note-on.
    inline float run (int mode, float a, float b, double t)
    {
        switch (mode)
        {
            case DNA_Waveform:
            {
                // A for the first part of every cycle, B for the rest, with soft edges
                const float split = 0.05f + 0.9f * amount, w = 0.002f + 0.2f * chr;
                const float f = (float) ph;
                const float rise = sat (0.5f + (f - split) / w), fall = sat (0.5f + (1.0f - f) / w), wrap = sat (0.5f - f / w);
                const float wB = std::max (std::min (rise, fall), wrap);
                return a + (b - a) * wB;
            }
            case DNA_Crossover:
            {
                // A below the crossover, B above; Character goes from 12 to 24 dB slopes
                const float y2 = lp2.process (a, 0) - hp2.process (b, 0);
                const float y4 = lp4[1].process (lp4[0].process (a, 0), 0) + hp4[1].process (hp4[0].process (b, 0), 0);
                return y2 + (y4 - y2) * chr;
            }
            case DNA_Harmonic:
            {
                // every Nth harmonic (N = 2..8) taken from B, the rest from A
                const int N = 2 + (int) std::lround (chr * 6.0f);
                const double T = sr / hz;
                float hA = 0, hB = 0;
                for (int k = 0; k < N; ++k) { const double d = k * T / N; hA += read (dlA, d); hB += read (dlB, d); }
                hA /= (float) N; hB /= (float) N;
                return a + amount * (hB - hA);
            }
            case DNA_Spectral:
            {
                // cross-synthesis: B's sound shaped by A's spectral envelope (8 bands)
                float voc = 0.0f;
                for (int k = 0; k < kBands; ++k)
                {
                    const float xa = bpA[k].process (a, 0), xb = bpB[k].process (b, 0);
                    envA[k] += (std::abs (xa) - envA[k]) * envCoef;
                    envB[k] += (std::abs (xb) - envB[k]) * envCoef;
                    voc += xb * std::min (10.0f, envA[k] / (envB[k] + 1.0e-4f));
                }
                return b + (voc - b) * amount;
            }
            case DNA_Transient:
            {
                // A's attack, then B's body
                const double len = 0.002 * std::pow (250.0, (double) amount), fade = 0.001 + 0.2 * chr;
                const float wB = sat ((float) ((t - len) / fade));
                return a + (b - a) * (wB * wB * (3.0f - 2.0f * wB));
            }
            case DNA_AmpDna:
            {
                // B's waveform with A's amplitude contour
                ampA += (std::abs (a) - ampA) * envCoef;
                ampB += (std::abs (b) - ampB) * envCoef;
                const float y = b * std::min (10.0f, ampA / (ampB + 1.0e-4f));
                return b + (y - b) * amount;
            }
            case DNA_Morph:
            default:
            {
                // equal-power morph, or (with Character) cycle-by-cycle gene shuffling
                const float th = amount * 1.5707963f;
                const float smooth = a * std::cos (th) + b * std::sin (th);
                const float shuffle = a + (b - a) * gene;
                return smooth + (shuffle - smooth) * chr;
            }
        }
    }

private:
    static inline float sat (float x) { return x < 0 ? 0.0f : (x > 1 ? 1.0f : x); }
    inline float read (const std::vector<float>& d, double delay) const
    {
        double rp = (double) dlPos - 1.0 - delay;
        while (rp < 0) rp += dlLen;
        const int i0 = (int) rp;
        const float fr = (float) (rp - i0);
        const int i1 = (i0 + 1) & (dlLen - 1);
        return d[(size_t) i0] + (d[(size_t) i1] - d[(size_t) i0]) * fr;
    }

    double sr = 48000.0, hz = 220.0, ph = 0.0, lastFc = -1.0;
    float amount = 0.5f, chr = 0.5f;
    std::vector<float> dlA, dlB;
    int dlLen = 4096, dlPos = 0;
    Biquad lp2, hp2, lp4[2], hp4[2];
    Biquad bpA[kBands], bpB[kBands];
    bool bandsSet = false;
    float envA[kBands] {}, envB[kBands] {}, envCoef = 0.01f, ampA = 0, ampB = 0;
    float gene = 0, geneTarget = 0, geneC = 0.005f;
    uint32_t rng = 0x1234567u;
};

} // namespace tg
