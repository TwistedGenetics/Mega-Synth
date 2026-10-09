#pragma once
// Resonator: a bank of up to 16 tuned two-pole resonators (modal synthesis), tuned to
// each note, excited by the voice after the filter and amp envelope. Its tail rings on
// after the note's envelope ends (the voice stays alive until it has died away).
#include <JuceHeader.h>
#include <cmath>
#include <array>
#include <algorithm>

namespace tg
{

enum ResTuning { RT_Harmonic, RT_Odd, RT_Bar, RT_Membrane, RT_Plate, RT_Bell, RT_COUNT };

struct ResonatorParams
{
    float mix = 0, pitch = 0, decay = 1, damping = 0.5f, inharm = 0, spread = 0.5f, feedback = 0;
    int tuning = 0, modes = 8;
    bool active() const { return mix > 1.0e-4f; }
};

// Mode frequency ratios (relative to the note) for each tuning, 16 modes each.
inline const std::array<std::array<float, 16>, RT_COUNT>& resonatorTables()
{
    static const auto tables = []
    {
        std::array<std::array<float, 16>, RT_COUNT> t {};
        for (int k = 0; k < 16; ++k)
        {
            t[RT_Harmonic][(size_t) k] = (float) (k + 1);
            t[RT_Odd][(size_t) k] = (float) (2 * k + 1);
        }
        // free-free bar (xylophone / marimba bar before tuning): (beta_n / beta_1)^2
        const double beta[] = { 4.7300408, 7.8532046, 10.9956078, 14.1371655 };
        for (int k = 0; k < 16; ++k)
        {
            const double b = k < 4 ? beta[k] : (2.0 * (k + 1) + 1.0) * juce::MathConstants<double>::pi * 0.5;
            t[RT_Bar][(size_t) k] = (float) ((b / beta[0]) * (b / beta[0]));
        }
        // circular membrane (drum head): Bessel zeros j(m,n) / j(0,1), in order
        const float membrane[16] = { 1.0f, 1.5933f, 2.1355f, 2.2954f, 2.6531f, 2.9173f, 3.1555f, 3.5001f,
                                     3.5985f, 3.6475f, 4.0589f, 4.1317f, 4.2304f, 4.6010f, 4.8312f, 4.9024f };
        // square plate (simply supported): (m^2 + n^2) / 2, in order
        std::vector<float> plate;
        for (int m = 1; m <= 8; ++m) for (int n = m; n <= 8; ++n) plate.push_back ((float) (m * m + n * n) * 0.5f);
        std::sort (plate.begin(), plate.end());
        // minor-third church bell partials: hum, prime, tierce, quint, nominal and upper partials
        const float bell[16] = { 0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f,
                                 4.166f, 5.433f, 6.796f, 8.215f, 9.687f, 11.21f, 12.78f, 14.39f };
        for (int k = 0; k < 16; ++k)
        {
            t[RT_Membrane][(size_t) k] = membrane[k];
            t[RT_Plate][(size_t) k] = plate[(size_t) k];
            t[RT_Bell][(size_t) k] = bell[k];
        }
        return t;
    }();
    return tables;
}

// The frequency ratio of mode k with the inharmonicity (stiffness) stretch applied.
inline float resonatorRatio (int tuning, int k, float inharm)
{
    const float r = resonatorTables()[(size_t) juce::jlimit (0, RT_COUNT - 1, tuning)][(size_t) k];
    return r * std::sqrt (1.0f + 0.002f * inharm * (float) ((k + 1) * (k + 1)));
}

class Resonator
{
public:
    static constexpr int kModes = 16;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        agcA = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
        agcR = 1.0f - std::exp (-1.0f / (0.25f * (float) sr));
        reset();
    }
    void reset()
    {
        for (auto& m : md) { m.y1 = m.y2 = m.x1 = m.x2 = 0.0; }
        last = 0.0f; env = 0.0f;
    }

    void configure (const ResonatorParams& p, double noteHz)
    {
        const int n = juce::jlimit (1, kModes, p.modes);
        const double base = noteHz * std::exp2 (p.pitch / 12.0);
        float wsum = 0.0f;
        for (int k = 0; k < kModes; ++k)
        {
            Mode& m = md[(size_t) k];
            const double f = base * resonatorRatio (p.tuning, k, p.inharm);
            m.on = k < n && f > 15.0 && f < sr * 0.45;
            if (! m.on) { m.y1 = m.y2 = m.x1 = m.x2 = 0.0; continue; }
            const double ratio = f / std::max (1.0, base);
            // higher modes die away faster as Damping goes up
            const double t60 = std::max (0.005, (double) p.decay * std::pow (ratio, -1.5 * p.damping));
            const double r = std::pow (10.0, -3.0 / (t60 * sr));
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
            m.a1 = -2.0 * r * std::cos (w);
            m.a2 = r * r;
            // a strike rings at the same level whatever the decay (impulse-normalised), with the gain
            // for a sustained tone at the mode's own frequency capped at 200x; the output levelling
            // below keeps sustained excitation from overloading
            m.b0 = std::min (std::sin (w), 200.0 * (1.0 - r));
            // alternate modes left / right by Spread
            const float side = (k % 2 == 0 ? -1.0f : 1.0f) * p.spread * (k == 0 ? 0.0f : 1.0f);
            m.gl = std::sqrt (0.5f * (1.0f - side)); m.gr = std::sqrt (0.5f * (1.0f + side));
            m.w = 1.0f / std::sqrt ((float) (k + 1));
            wsum += m.w;
        }
        norm = wsum > 0.0f ? 1.6f / wsum : 0.0f;
        fb = 0.6f * p.feedback;
    }

    // excitation in, stereo resonance out
    inline void process (float in, float& outL, float& outR)
    {
        const double x = in + fb * std::tanh (last);
        double l = 0.0, r = 0.0;
        for (auto& m : md)
        {
            if (! m.on) continue;
            const double y = m.b0 * (x - m.x2) - m.a1 * m.y1 - m.a2 * m.y2;
            m.x2 = m.x1; m.x1 = x; m.y2 = m.y1; m.y1 = y;
            l += y * m.w * m.gl; r += y * m.w * m.gr;
        }
        // levelling: a smooth gain reduction above ~0.9, then a soft ceiling
        l *= norm; r *= norm;
        const float pk = (float) std::max (std::abs (l), std::abs (r));
        env += (pk - env) * (pk > env ? agcA : agcR);
        const float g = env > 0.9f ? 0.9f / env : 1.0f;
        outL = 1.5f * std::tanh ((float) l * g / 1.5f);
        outR = 1.5f * std::tanh ((float) r * g / 1.5f);
        if (! std::isfinite (outL) || ! std::isfinite (outR)) { reset(); outL = outR = 0.0f; }
        last = 0.5f * (outL + outR);
    }

private:
    struct Mode { double a1 = 0, a2 = 0, b0 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0; float gl = 0.7f, gr = 0.7f, w = 1; bool on = false; };
    std::array<Mode, kModes> md {};
    double sr = 48000.0;
    float norm = 0, fb = 0, last = 0, env = 0, agcA = 0.1f, agcR = 0.001f;
};

} // namespace tg
