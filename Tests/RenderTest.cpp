// Offline test: renders the synth without a host and checks the output is sane.
#include <JuceHeader.h>
#include <set>
#include <thread>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamFormat.h"
#include "Registry.h"
#include "Mut/Mutator.h"
#include "UI/FilterPanel.h"
#include "UI/PatchBrowser.h"

using namespace tg;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << std::endl; } } while (0)

struct Stats { float peak = 0; double rms = 0; bool finite = true; };

static void setP (MegaSynthProcessor& p, int idx, float plain)
{
    auto* prm = p.param (idx);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static Stats render (MegaSynthProcessor& p, double seconds, std::vector<std::pair<double, juce::MidiMessage>> events,
                     juce::AudioBuffer<float>* capture = nullptr, double* cpuSeconds = nullptr)
{
    const int block = 512;
    const double sr = p.getSampleRate();
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, block);
    Stats s;
    double sumSq = 0;
    if (capture) capture->setSize (2, total);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (auto& e : events)
        {
            const int at = (int) (e.first * sr);
            if (at >= pos && at < pos + n) midi.addEvent (e.second, at - pos);
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
        {
            const float* d = buf.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (d[i])) s.finite = false;
                s.peak = std::max (s.peak, std::abs (d[i]));
                sumSq += (double) d[i] * d[i];
            }
            if (capture) capture->copyFrom (c, pos, buf, c, 0, n);
        }
    }
    if (cpuSeconds) *cpuSeconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    s.rms = std::sqrt (sumSq / std::max (1, total * 2));
    return s;
}

static juce::String getXmlFromBinaryForTest (const juce::MemoryBlock& mb)
{
    // JUCE's copyXmlToBinary format: magic, size, then the XML text
    if (mb.getSize() < 9) return {};
    return juce::String::fromUTF8 (static_cast<const char*> (mb.getData()) + 8, (int) mb.getSize() - 8);
}

static void writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::String& name)
{
    auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders");
    dir.createDirectory();
    auto f = dir.getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

static std::vector<std::pair<double, juce::MidiMessage>> chord (double on, double off, std::initializer_list<int> notes)
{
    std::vector<std::pair<double, juce::MidiMessage>> ev;
    for (int n : notes) { ev.push_back ({ on, juce::MidiMessage::noteOn (1, n, (juce::uint8) 100) }); ev.push_back ({ off, juce::MidiMessage::noteOff (1, n) }); }
    return ev;
}

static int tabByName (juce::TabbedComponent* t, const juce::String& name)
{
    if (t != nullptr) for (int i = 0; i < t->getNumTabs(); ++i) if (t->getTabNames()[i].equalsIgnoreCase (name)) return i;
    return 0;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;

    auto make = [&]
    {
        auto p = std::make_unique<MegaSynthProcessor>();
        p->setPlayConfigDetails (0, 2, sr, 512);
        p->prepareToPlay (sr, 512);
        return p;
    };

    // quick profiling run: MEGASYNTH_PROFILE=1 (16 voices, effects off, 2 s), then exit
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_PROFILE", {}).isNotEmpty())
    {
        auto p = make();
        setP (*p, P_polyphony, 16);
        for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_complexGain, P_supersawGain }) setP (*p, i, 0.0f);
        std::vector<std::pair<double, juce::MidiMessage>> ev;
        for (int n = 0; n < 16; ++n) ev.push_back ({ 0.01 * n, juce::MidiMessage::noteOn (1, 40 + n * 2, (juce::uint8) 90) });
        double cpu = 0;
        render (*p, 2.0, ev, nullptr, &cpu);
        std::cout << "profile render " << cpu << " s" << std::endl;
        return 0;
    }

    // ---- Filter overhaul: the filter unit on its own (frequency responses, accuracy, stability)
    auto filterUnitTests = [&]
    {
        std::cout << "Filter: unit responses" << std::endl;
        const double fs = 48000.0;
        // steady-state gain (dB) of a FilterUnit for a sine at f
        auto gainAt = [&] (int model, int type, int slope, float cutoff, float res, double f, float drive = 1.0f, float mix = 1.0f)
        {
            FilterUnit u;
            u.configure (model, type, slope, true);
            u.setDrive (drive); u.setMix (mix);
            u.update (cutoff, res, fs);
            const int n = (int) (fs * 0.25), skip = (int) (fs * 0.15);
            double si = 0, so = 0;
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.001f * (float) std::sin (2.0 * kPi * f * i / fs);
                float yl, yr; u.processFrame (x, x, false, yl, yr);
                if (i >= skip) { si += (double) x * x; so += (double) yl * yl; }
            }
            return 10.0 * std::log10 (std::max (1e-30, so) / si);
        };
        const char* tn[] = { "LP", "HP", "BP", "Notch", "Peak", "AP" };
        const float fc = 1000.0f;
        for (int type = 0; type < 6; ++type)
            for (int slope = 0; slope < 4; ++slope)
            {
                if (! (filterSlopeMask (type) & (1 << slope)) && ! (type == FT_PEAK && slope == 1)) continue;
                const double gLo = gainAt (0, type, slope, fc, 0.1f, 125.0), gM = gainAt (0, type, slope, fc, 0.1f, 1000.0),
                             gHi = gainAt (0, type, slope, fc, 0.1f, 4000.0), gHi2 = gainAt (0, type, slope, fc, 0.1f, 8000.0);
                std::cout << "  " << tn[type] << " " << (6 * (slope + 1)) << " dB: 125 Hz " << juce::String (gLo, 1) << "  1k " << juce::String (gM, 1)
                          << "  4k " << juce::String (gHi, 1) << "  8k " << juce::String (gHi2, 1) << std::endl;
                const double perOct = 6.0 * (slope + 1);
                if (type == FT_LP) CHECK (std::abs ((gHi - gHi2) - perOct) < 3.0 && std::abs (gLo) < 1.5, juce::String ("LP slope ") + juce::String (perOct));
                if (type == FT_HP) CHECK (std::abs ((gainAt (0, type, slope, fc, 0.1f, 250.0) - gLo) - perOct) < 3.0 && std::abs (gHi2) < 1.5, juce::String ("HP slope ") + juce::String (perOct));
                if (type == FT_BP) CHECK (gM > gLo + 10 && gM > gHi2 + 10 && std::abs (gM) < 3.0, "band pass peaks at the cutoff");
                if (type == FT_NOTCH) CHECK (gM < -20 && std::abs (gLo) < 3 && std::abs (gHi2) < 3, "notch cuts at the cutoff only");
                if (type == FT_PEAK) CHECK (gM > 1.5 && std::abs (gLo) < 1.0 && std::abs (gHi2) < 1.0, "peak boosts at the cutoff only");
                if (type == FT_AP) CHECK (std::abs (gLo) < 0.5 && std::abs (gM) < 0.5 && std::abs (gHi2) < 0.5, "all pass is flat");
            }
        // BP centre (and so the cutoff) is where the display says, across the range
        for (float c : { 50.0f, 200.0f, 1000.0f, 5000.0f, 15000.0f })
        {
            const double g0 = gainAt (0, FT_BP, 1, c, 15.0f, c), gDn = gainAt (0, FT_BP, 1, c, 15.0f, c * 0.94), gUp = gainAt (0, FT_BP, 1, c, 15.0f, c * 1.06);
            CHECK (g0 > gDn && g0 > gUp, "band-pass centre at " + juce::String (c) + " Hz");
        }
        // resonance: strong peak near the cutoff for LP / HP
        for (int type : { FT_LP, FT_HP })
            for (int slope : { 1, 2, 3 })
            {
                const double pk = gainAt (0, type, slope, 1000.0f, 22.0f, 1000.0) - gainAt (0, type, slope, 1000.0f, 0.1f, 1000.0);
                CHECK (pk > 9.0, juce::String (tn[type]) + " " + juce::String (6 * (slope + 1)) + " dB resonance (" + juce::String (pk, 1) + " dB)");
            }

        // 24 dB low pass self-oscillates at the top of the resonance knob, at the cutoff, and stays bounded
        for (float c : { 220.0f, 880.0f, 3000.0f })
        {
            FilterUnit u; u.configure (0, FT_LP, 3, true); u.update (c, 25.0f, fs);
            int crossings = 0; float last = 0, peak = 0; const int n = (int) fs;
            for (int i = 0; i < n; ++i)
            {
                float yl, yr; u.processFrame (i < 10 ? 0.2f : 0.0f, 0.0f, false, yl, yr);
                if (i > n / 2) { if (last <= 0 && yl > 0) ++crossings; peak = std::max (peak, std::abs (yl)); }
                last = yl;
            }
            const double freq = crossings / 0.5;
            std::cout << "  self-oscillation at " << c << " Hz cutoff: " << freq << " Hz, peak " << peak << std::endl;
            CHECK (peak > 0.05f && peak < 3.0f, "24 dB low pass self-oscillates, bounded");
            CHECK (std::abs (freq / c - 1.0) < 0.12, "self-oscillation pitch follows the cutoff");
        }

        // mix 0% is the dry signal exactly; drive is compensated
        {
            FilterUnit u; u.configure (2, FT_LP, 3, true); u.setMix (0.0f); u.update (300.0f, 10.0f, fs);
            float md = 0;
            for (int i = 0; i < 2000; ++i) { const float x = 0.3f * (float) std::sin (i * 0.05); float yl, yr; u.processFrame (x, x, false, yl, yr); md = std::max (md, std::abs (yl - x)); }
            CHECK (md == 0.0f, "Filter Mix 0% = untouched input");
            auto rmsDrive = [&] (float k)
            {
                FilterUnit v; v.configure (0, FT_LP, 3, true); v.setDrive (k); v.update (6000.0f, 1.5f, fs);
                double so = 0; const int n = 24000;
                for (int i = 0; i < n; ++i) { const float x = 0.35f * (float) (2.0 * std::fmod (110.0 * i / fs, 1.0) - 1.0); float yl, yr; v.processFrame (x, x, false, yl, yr); so += (double) yl * yl; }
                return 10.0 * std::log10 (so / n);
            };
            const double d1 = rmsDrive (1.0f), d25 = rmsDrive (25.0f);
            std::cout << "  drive 25 vs 1: " << juce::String (d25 - d1, 1) << " dB" << std::endl;
            CHECK (d25 - d1 < 8.0 && d25 - d1 > 0.5, "drive adds density without a big level jump");
        }

        // stability: every model / type / slope, extreme resonance, cutoff thrown around every 16 samples
        {
            uint32_t rs = 12345; auto rnd = [&] { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xFFFFFF) / 16777216.0f; };
            bool ok = true; float worst = 0;
            for (int model = 0; model < 17; ++model)
                for (int type = 0; type < 6; ++type)
                    for (int slope = 0; slope < 4; ++slope)
                    {
                        FilterUnit u; u.configure (model, type, slope, true); u.setDrive (1.0f + 24.0f * rnd());
                        for (int i = 0; i < 6000; ++i)
                        {
                            if ((i & 15) == 0) u.update (20.0f * std::pow (1000.0f, rnd()), rnd() < 0.5f ? 25.0f : 30.0f * rnd(), fs);
                            const float x = 2.0f * rnd() - 1.0f;
                            float yl, yr; u.processFrame (x, x, false, yl, yr);
                            if (! std::isfinite (yl)) ok = false;
                            worst = std::max (worst, std::abs (yl));
                        }
                    }
            std::cout << "  stress: worst peak " << worst << std::endl;
            CHECK (ok && worst < 64.0f, "every filter stays stable and bounded under extreme settings");
        }

        // Drive at 0 dB is clean: every model, type and slope behaves linearly at normal levels
        // (a loud signal gives the same result as a tiny one, scaled up)
        {
            double worst = -300; juce::String where;
            for (int model = 0; model < 17; ++model)
                for (int type = 0; type < 6; ++type)
                    for (int slope = 0; slope < 4; ++slope)
                    {
                        if (filterSlopeMask (type) != 0 && ! (filterSlopeMask (type) & (1 << slope))) continue;
                        FilterUnit big, small;
                        for (auto* u : { &big, &small }) { u->setAnalog (0.5f, 0.5f); u->configure (model, type, slope, true); u->setDrive (1.0f); u->update (1000.0f, 1.5f, fs); }
                        double e = 0, d = 0;
                        for (int i = 0; i < 9600; ++i)
                        {
                            const float x = 0.45f * (float) std::sin (2.0 * kPi * 220.0 * i / fs) + 0.35f * (float) std::sin (2.0 * kPi * 1500.0 * i / fs);
                            float b, sm, r;
                            big.processFrame (x, x, false, b, r);
                            small.processFrame (x * 0.001f, x * 0.001f, false, sm, r);
                            if (i > 2400) { e += (double) b * b; d += (double) (b - sm * 1000.0f) * (b - sm * 1000.0f); }
                        }
                        const double db = 10 * std::log10 (d / std::max (e, 1e-20) + 1e-30);
                        if (db > worst) { worst = db; where = juce::String (kFilterLabels[model]) + " " + kFilterTypeLabels[type] + " " + kFilterSlopeLabels[slope]; }
                    }
            std::cout << "  Drive 0 dB distortion, worst case: " << juce::String (worst, 1) << " dB (" << where << ")" << std::endl;
            CHECK (worst < -70.0, "Drive 0 dB is clean for every model, type and slope");
        }

        // switching type / slope / model mid-sound crossfades instead of clicking: the largest
        // sample-to-sample step while switching is no bigger than either setting's own steady one
        {
            auto run = [&] (int seg0, int segs, bool switching, std::vector<float>& out)
            {
                FilterUnit u;
                auto cfg = [&] (int k, bool imm) { u.configure ((k * 5) % 17, k % 6, (k / 2) % 4, imm); u.update (800.0f, 8.0f, fs); };
                cfg (seg0, true);
                out.assign ((size_t) (segs * 4800), 0.0f);
                for (int i = 0; i < segs * 4800; ++i)
                {
                    if (switching && i % 4800 == 0 && i > 0) cfg (seg0 + i / 4800, false);
                    const float x = 0.5f * (float) std::sin (2.0 * kPi * 330.0 * i / fs);
                    float yl, yr; u.processFrame (x, x, false, yl, yr);
                    out[(size_t) i] = yl;
                }
            };
            int bad = 0;
            for (int k = 0; k < 24; ++k)
            {
                std::vector<float> a, b, sw;
                run (k, 1, false, a); run (k + 1, 1, false, b); run (k, 2, true, sw);
                auto maxStep = [] (const std::vector<float>& v, int from, int to)
                {
                    float m = 0; for (int i = std::max (1, from); i < to; ++i) m = std::max (m, std::abs (v[(size_t) i] - v[(size_t) i - 1])); return m;
                };
                const float steady = std::max (maxStep (a, 2400, 4800), maxStep (b, 2400, 4800));
                const float atSwitch = maxStep (sw, 4800 - 4, 4800 + 600);
                if (atSwitch > steady * 1.25f + 0.002f) { ++bad; std::cout << "    switch " << k << ": " << atSwitch << " vs steady " << steady << std::endl; }
            }
            CHECK (bad == 0, "type / slope / model changes don't click");
        }

        // display
        CHECK (formatParam (P_filterCutoff, 80.0f) == "80 Hz" && formatParam (P_filterCutoff, 350.0f) == "350 Hz"
               && formatParam (P_filterCutoff, 1200.0f) == "1.2 kHz" && formatParam (P_filterCutoff, 8500.0f) == "8.5 kHz"
               && formatParam (P_filterCutoff, 20000.0f) == "20 kHz", "cutoff shows Hz / kHz (" + formatParam (P_filterCutoff, 1200.0f) + ")");
        CHECK (std::abs (parseParam (P_filterCutoff, "1.2 kHz") - 1200.0f) < 0.5f && std::abs (parseParam (P_filterCutoff, "350 Hz") - 350.0f) < 0.5f, "typing Hz / kHz");
        CHECK (formatParam (P_fEnvAmt, 2200.0f) == "+22%" && formatParam (P_fEnvAmt, -10000.0f) == "-100%", "Env Amt in percent");
        CHECK (meta (P_filterCutoff).min == 20.0f && meta (P_filterCutoff).max == 20000.0f, "cutoff range 20 Hz - 20 kHz");
        CHECK (std::abs (fromNormalised (P_filterCutoff, 0.5f) - 632.5f) < 1.0f, "cutoff knob is logarithmic (centre = 632 Hz)");
        for (int prm : { P_filterCutoff, P_filterRes, P_filterDrive, P_filterMix, P_fEnvAmt, P_filterKeyTrack, P_filterLfoAmt })
            CHECK (meta (prm).modulatable, juce::String ("modulation destination: ") + meta (prm).id);

        // ---- in the voice: key tracking, bipolar envelope, filter LFO, routing, migration
        std::cout << "Filter: in the synth" << std::endl;
        auto centroid = [&] (const juce::AudioBuffer<float>& b, int start)
        {
            juce::dsp::FFT fft (12);
            std::vector<float> d (8192, 0.0f);
            for (int i = 0; i < 4096; ++i)
                d[(size_t) i] = b.getSample (0, start + i) * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / 4095.0f));
            fft.performFrequencyOnlyForwardTransform (d.data());
            double num = 0, den = 0;
            for (int k = 1; k < 2048; ++k) { const double m = d[(size_t) k]; num += m * k * sr / 4096.0; den += m; }
            return den > 0 ? num / den : 0.0;
        };
        auto basic = [&] (MegaSynthProcessor& p)
        {
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain, P_lfoAssignAmt0, P_envAssignAmt0 })
                setP (p, i, 0.0f);
            setP (p, P_fEnvAmt, 0.0f); setP (p, P_analogDrift, 0.0f);
            setP (p, P_ampA, 0.002f); setP (p, P_ampS, 1.0f);
        };
        auto noteCentroid = [&] (std::function<void (MegaSynthProcessor&)> setup, int note, double at)
        {
            auto p = make(); basic (*p); setup (*p);
            juce::AudioBuffer<float> cap;
            render (*p, at + 0.12, { { 0.0, juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) } }, &cap);
            return centroid (cap, (int) (at * sr));
        };
        {
            auto kt = [&] (float amount) { return [amount] (MegaSynthProcessor& p) { setP (p, P_filterCutoff, 400.0f); setP (p, P_filterRes, 0.1f); setP (p, P_filterKeyTrack, amount); }; };
            const double r0 = noteCentroid (kt (0.0f), 72, 0.3) / noteCentroid (kt (0.0f), 48, 0.3);
            const double r1 = noteCentroid (kt (1.0f), 72, 0.3) / noteCentroid (kt (1.0f), 48, 0.3);
            std::cout << "  brightness ratio two octaves apart: key track 0% " << r0 << ", 100% " << r1 << std::endl;
            CHECK (r1 > 3.2 && r1 < 5.0 && r1 > r0 * 1.5, "key tracking 100% follows the keyboard");
        }
        {
            // negative envelope: starts darker than the knob and opens as the envelope decays
            auto env = [&] (float amt) { return [amt] (MegaSynthProcessor& p) { setP (p, P_filterCutoff, 6000.0f); setP (p, P_fEnvAmt, amt); setP (p, P_fEnvA, 0.001f); setP (p, P_fEnvD, 0.15f); setP (p, P_fEnvS, 0.0f); }; };
            const double early = noteCentroid (env (-5000.0f), 45, 0.02), late = noteCentroid (env (-5000.0f), 45, 0.6);
            const double earlyPos = noteCentroid (env (5000.0f), 45, 0.02), latePos = noteCentroid (env (5000.0f), 45, 0.6);
            std::cout << "  env -50%: early " << early << " late " << late << ";  +50%: early " << earlyPos << " late " << latePos << std::endl;
            CHECK (early < late * 0.8 && earlyPos > latePos * 1.1, "Env Amt is bipolar");
        }
        {
            auto lfo = [&] (float amt) { return [amt] (MegaSynthProcessor& p) { setP (p, P_filterCutoff, 800.0f); setP (p, P_filterLfoAmt, amt); setP (p, P_filterLfoSrc, 1.0f);
                                                                                 setP (p, P_lfo2Rate, 1.0f); setP (p, P_lfo2Depth, 1.0f); setP (p, P_lfo2Wave, 0.0f); }; };
            // LFO 2 sine at 1 Hz: +peak at 0.25 s, -peak at 0.75 s
            const double up = noteCentroid (lfo (0.5f), 45, 0.19), dn = noteCentroid (lfo (0.5f), 45, 0.69);
            const double upNeg = noteCentroid (lfo (-0.5f), 45, 0.19), dnNeg = noteCentroid (lfo (-0.5f), 45, 0.69);
            std::cout << "  filter LFO +50%: " << up << " / " << dn << ",  -50%: " << upNeg << " / " << dnNeg << std::endl;
            CHECK (up > dn * 1.5 && upNeg < dnNeg / 1.5, "filter LFO amount is bipolar and moves the cutoff");
        }
        {
            // the new controls are matrix destinations that really move the sound
            auto p = make(); basic (*p);
            setP (*p, P_filterCutoff, 300.0f); setP (*p, P_filterMix, 0.0f);
            juce::AudioBuffer<float> a, b;
            render (*p, 0.4, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100) } }, &a);
            auto q = make(); basic (*q);
            setP (*q, P_filterCutoff, 300.0f); setP (*q, P_filterMix, 0.0f);
            q->addRoute (MS_Macro1, P_filterMix, 1.0f); setP (*q, P_macro1, 1.0f);
            render (*q, 0.4, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100) } }, &b);
            const double ca = centroid (a, (int) (0.25 * sr)), cb = centroid (b, (int) (0.25 * sr));
            CHECK (cb < ca * 0.6, "Macro -> Filter Mix moves the dry/wet balance");
        }
        {
            // 44.1 kHz, cutoff at the top, every type: stable
            auto p = std::make_unique<MegaSynthProcessor>();
            p->setPlayConfigDetails (0, 2, 44100.0, 512); p->prepareToPlay (44100.0, 512);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_filterRes, 25.0f);
            bool ok = true;
            for (int type = 0; type < 6; ++type)
            {
                setP (*p, P_filterType, (float) type); setP (*p, P_filterSlope, 3.0f);
                auto st = render (*p, 0.3, chord (0.0, 0.25, { 60, 84 }));
                ok = ok && st.finite && st.peak < 6.0f;
            }
            CHECK (ok, "cutoff at 20 kHz is safe at 44.1 kHz");
        }
        {
            // a project saved before the overhaul: Ladder model, no new parameters -> Ladder at 24 dB, low pass, fully wet
            auto p = make();
            setP (*p, P_filterMode, 2.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto xml = juce::parseXML (getXmlFromBinaryForTest (mb));
            CHECK (xml != nullptr, "state xml");
            if (xml != nullptr)
            {
                xml->setAttribute ("stateVersion", 2);
                for (auto* id : { "filterType", "filterSlope", "filterMix", "filterKeyTrack", "filterLfoAmt", "filterLfoSrc" })
                    if (auto* e = xml->getChildByAttribute ("id", id)) xml->removeChildElement (e, true);
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
                auto q = make();
                setP (*q, P_filterSlope, 0.0f); setP (*q, P_filterType, 2.0f); setP (*q, P_filterMix, 0.3f);
                q->setStateInformation (old.getData(), (int) old.getSize());
                auto plain = [&] (int i) { return q->param (i)->convertFrom0to1 (q->param (i)->getValue()); };
                CHECK (std::lround (plain (P_filterSlope)) == 3 && std::lround (plain (P_filterType)) == FT_LP && plain (P_filterMix) > 0.999f,
                       "old project: classic model keeps its own slope, low pass, fully wet");
            }
            // an old patch file (no filter settings in its plugin block)
            auto q = make();
            setP (*q, P_filterSlope, 0.0f);
            const juce::String patch = R"({"format":"megasynth","params":{"filterMode":"tb303","filterCutoff":"900"},"plugin":{"velSens":0}})";
            CHECK (q->importBrowserPatch (patch).isEmpty(), "old patch imports");
            CHECK (std::lround (q->param (P_filterSlope)->convertFrom0to1 (q->param (P_filterSlope)->getValue())) == 3, "old patch: TB-303 at its own 24 dB slope");
            CHECK (std::abs (q->param (P_filterCutoff)->convertFrom0to1 (q->param (P_filterCutoff)->getValue()) - 900.0f) < 0.5f, "old patch cutoff kept in Hz");
        }
        // ---- Filter 2: serial, parallel, balance, stereo split, click-free routing changes
        std::cout << "Filter 2" << std::endl;
        {
            // a 220 Hz sine; Filter 1 wide open, Filter 2 a steep high pass at 2 kHz
            auto setup = [&] (MegaSynthProcessor& p)
            {
                basic (p);
                setP (p, P_osc1Wave, 3.0f); setP (p, P_warmth, 0.0f);
                setP (p, P_filterCutoff, 20000.0f); setP (p, P_filterRes, 0.1f);
                setP (p, P_filter2Type, (float) FT_HP); setP (p, P_filter2Slope, 3.0f); setP (p, P_filter2Cutoff, 2000.0f);
            };
            auto lr = [&] (std::function<void (MegaSynthProcessor&)> extra)
            {
                auto p = make(); setup (*p); extra (*p);
                juce::AudioBuffer<float> cap;
                render (*p, 0.5, { { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) } }, &cap);
                double l = 0, r = 0;
                for (int i = (int) (0.25 * sr); i < cap.getNumSamples(); ++i) { l += cap.getSample (0, i) * cap.getSample (0, i); r += cap.getSample (1, i) * cap.getSample (1, i); }
                return std::make_pair (10 * std::log10 (l + 1e-20), 10 * std::log10 (r + 1e-20));
            };
            const auto off = lr ([] (MegaSynthProcessor&) {});
            const auto ser = lr ([&] (MegaSynthProcessor& p) { setP (p, P_filter2On, 1.0f); });
            const auto par = lr ([&] (MegaSynthProcessor& p) { setP (p, P_filter2On, 1.0f); setP (p, P_filterRouting, 1.0f); });
            const auto parB = lr ([&] (MegaSynthProcessor& p) { setP (p, P_filter2On, 1.0f); setP (p, P_filterRouting, 1.0f); setP (p, P_filterBalance, 1.0f); });
            const auto spl = lr ([&] (MegaSynthProcessor& p) { setP (p, P_filter2On, 1.0f); setP (p, P_filterRouting, 1.0f); setP (p, P_filterStereoSplit, 1.0f); });
            std::cout << "  220 Hz through (Filter 1 open, Filter 2 HP 2 kHz): off " << off.first << " dB, serial " << ser.first << ", parallel " << par.first
                      << ", parallel balance->2 " << parB.first << ", split L " << spl.first << " R " << spl.second << std::endl;
            CHECK (ser.first < off.first - 40, "serial: Filter 2 filters Filter 1's output");
            CHECK (std::abs (par.first - off.first) < 1.0, "parallel: Filter 1's path at full level with Balance centred");
            CHECK (parB.first < off.first - 40, "Balance fully to Filter 2 leaves only Filter 2");
            CHECK (std::abs (spl.first - off.first) < 1.0 && spl.second < off.second - 40, "stereo split: Filter 1 left, Filter 2 right");

            // switching routing while a note plays doesn't click
            auto p = make(); setup (*p);
            setP (*p, P_filter2On, 1.0f);
            juce::AudioBuffer<float> a, b;
            render (*p, 0.3, { { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) } }, &a);
            setP (*p, P_filterRouting, 1.0f);   // serial (silent) -> parallel (full)
            render (*p, 0.3, {}, &b);
            float steady = 0, sw = 0;
            for (int i = 1; i < b.getNumSamples(); ++i)
            {
                const float d = std::abs (b.getSample (0, i) - b.getSample (0, i - 1));
                if (i > (int) (0.2 * sr)) steady = std::max (steady, d); else if (i < 2048) sw = std::max (sw, d);
            }
            std::cout << "  routing change: largest step " << sw << " (steady " << steady << ")" << std::endl;
            CHECK (sw < steady * 1.3f + 0.003f, "routing changes crossfade");
        }
        {
            auto p = make();
            setP (*p, P_filterMode, 9.0f); setP (*p, P_filterType, (float) FT_BP); setP (*p, P_filterCutoff, 777.0f); setP (*p, P_filterLfoAmt, -0.3f);
            p->copyFilter1To2();
            auto pl = [&] (int i) { return p->param (i)->convertFrom0to1 (p->param (i)->getValue()); };
            CHECK (std::lround (pl (P_filter2Mode)) == 9 && std::lround (pl (P_filter2Type)) == FT_BP && std::abs (pl (P_filter2Cutoff) - 777.0f) < 0.5f
                   && std::abs (pl (P_filter2LfoAmt) + 0.3f) < 0.002f, "Copy 1 > 2");
            for (int prm : { P_filter2Cutoff, P_filter2Res, P_filter2Drive, P_filter2Mix, P_filter2EnvAmt, P_filter2KeyTrack, P_filter2LfoAmt, P_filterBalance })
                CHECK (meta (prm).modulatable, juce::String ("modulation destination: ") + meta (prm).id);
            CHECK (meta (P_filter2Cutoff).audioRate && meta (P_filter2Res).audioRate, "Filter 2 cutoff / resonance at audio rate");
            // a project from before Filter 2 (state version 3): Filter 2 comes back off
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto xml = juce::parseXML (getXmlFromBinaryForTest (mb));
            if (xml != nullptr)
            {
                xml->setAttribute ("stateVersion", 3);
                for (int i = 0; i < P_COUNT; ++i)
                    if (juce::String (kParamIds[i]).startsWith ("filter2") || i == P_filterRouting || i == P_filterBalance || i == P_filterStereoSplit)
                        if (auto* e = xml->getChildByAttribute ("id", kParamIds[i])) xml->removeChildElement (e, true);
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
                auto q = make();
                setP (*q, P_filter2On, 1.0f); setP (*q, P_filterRouting, 1.0f);
                q->setStateInformation (old.getData(), (int) old.getSize());
                CHECK (q->param (P_filter2On)->getValue() < 0.5f && q->param (P_filterRouting)->getValue() < 0.5f, "old project: Filter 2 off");
            }
        }

    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_FXSHOT", {}).isNotEmpty())
    {
        auto p = make();
        for (int i : { P_fxOnMultiband, P_fxOnStutter, P_fxOnShaper, P_fxOnFlanger }) setP (*p, i, 1.0f);
        p->fxRack.move (0, 4);
        render (*p, 0.5, chord (0.0, 0.4, { 48, 60 }));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 860);
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        auto* tabs = findTabs (ed.get());
        const int fxTab = tabByName (tabs, "Effects");
        tabs->setCurrentTabIndex (fxTab);
        auto* pageC = tabs->getTabContentComponent (fxTab);
        tgui::Segmented* pick = nullptr;
        for (auto* ch : pageC->getChildren()) if (auto* sg = dynamic_cast<tgui::Segmented*> (ch)) pick = sg;
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        for (int k = 0; k < 4; ++k)
        {
            if (pick != nullptr && pick->onSelect) pick->onSelect (k);
            auto img = ed->createComponentSnapshot (juce::Rectangle<int> (0, 88, 1200, 640), true, 1.0f);
            auto f = dir.getChildFile ("fx_ui_" + juce::String (k) + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        return 0;
    }

    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BROWSERSHOT", {}).isNotEmpty())
    {
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 860);
        std::function<juce::TextButton* (juce::Component*, const juce::String&)> findBtn = [&] (juce::Component* c, const juce::String& t) -> juce::TextButton*
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getButtonText() == t) return b;
            for (auto* ch : c->getChildren()) if (auto* b = findBtn (ch, t)) return b;
            return nullptr;
        };
        if (auto* b = findBtn (ed.get(), "Browse")) b->onClick();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        auto f = dir.getChildFile ("browser_ui.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }

    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_DNASHOT", {}).isNotEmpty())
    {
        auto p = make();
        setP (*p, P_dsOn, 1.0f); setP (*p, P_dsLane2On, 1.0f); setP (*p, P_dsSwing, 0.3f);
        p->dnaSteps.gen.extras = 0.5f;
        p->dnaEdit (MegaSynthProcessor::DE_SeedUp, 0);
        auto st = p->dnaSteps.get (0, 0, 3); st.lock = true; p->dnaSteps.set (0, 0, 3, st);
        render (*p, 0.3, chord (0.0, 0.2, { 48 }));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 860);
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        if (auto* tabs = findTabs (ed.get())) tabs->setCurrentTabIndex (tabByName (tabs, "DNA Seq"));
        auto img = ed->createComponentSnapshot (juce::Rectangle<int> (0, 88, 1200, 640), true, 1.0f);
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        auto f = dir.getChildFile ("dna_ui.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }

    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_DIAG", {}).isNotEmpty())
    {
        {
            // does the output depend on the host's buffer size?
            juce::AudioBuffer<float> out[2];
            for (int k = 0; k < 2; ++k)
            {
                const int bs = k == 0 ? 512 : 64;
                auto p = std::make_unique<MegaSynthProcessor>();
                p->setPlayConfigDetails (0, 2, sr, bs); p->prepareToPlay (sr, bs);
                for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain }) setP (*p, i, 0.0f);
                const int total = (int) (0.5 * sr);
                out[k].setSize (2, total);
                juce::AudioBuffer<float> buf (2, bs);
                for (int pos = 0; pos < total; pos += bs)
                {
                    juce::MidiBuffer midi; if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
                    buf.clear(); p->processBlock (buf, midi);
                    for (int c = 0; c < 2; ++c) out[k].copyFrom (c, pos, buf, c, 0, std::min (bs, total - pos));
                }
            }
            double d = 0, e = 0;
            for (int i = 0; i < out[0].getNumSamples(); ++i) { d += std::pow (out[0].getSample (0, i) - out[1].getSample (0, i), 2); e += std::pow (out[0].getSample (0, i), 2); }
            std::cout << "  buffer 512 vs 64: difference " << 10 * std::log10 (d / e + 1e-30) << " dB" << std::endl;
        }
        // filter "clean" check: filter wide open, Mix 100% vs 0%; the difference is what the filter adds
        auto cleanTest = [&] (const juce::String& name, std::function<void (MegaSynthProcessor&)> setup, std::initializer_list<int> notes)
        {
            juce::AudioBuffer<float> b[2];
            for (int m = 0; m < 2; ++m)
            {
                auto p = make();
                for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift }) setP (*p, i, 0.0f);
                setup (*p);
                setP (*p, P_filterMix, m == 0 ? 0.0f : 1.0f);
                render (*p, 1.0, chord (0.0, 0.9, notes), &b[m]);
            }
            double e0 = 0, ed = 0; float pk = 0;
            for (int i = (int) (0.2 * sr); i < (int) (0.8 * sr); ++i)
            {
                const float d = b[1].getSample (0, i) - b[0].getSample (0, i);
                e0 += b[0].getSample (0, i) * b[0].getSample (0, i); ed += d * d;
                pk = std::max (pk, std::abs (b[0].getSample (0, i)));
            }
            std::cout << "  " << name << ": dry peak " << pk << ", filter adds " << juce::String (10 * std::log10 (ed / e0 + 1e-30), 1) << " dB" << std::endl;
        };
        auto open = [] (int type, float cut) { return [type, cut] (MegaSynthProcessor& p) { setP (p, P_filterType, (float) type); setP (p, P_filterCutoff, cut); setP (p, P_filterRes, 0.1f);
                                                                                            setP (p, P_fEnvAmt, 0.0f); setP (p, P_envAssignAmt0, 0.0f); setP (p, P_lfoAssignAmt0, 0.0f); }; };
        for (float w : { 0.0f, 0.5f })
        {
            std::cout << "warmth " << w << std::endl;
            auto with = [&] (std::function<void (MegaSynthProcessor&)> f) { return [f, w] (MegaSynthProcessor& p) { f (p); setP (p, P_warmth, w); }; };
            cleanTest ("default oscs, LP12 classic 20k, one note", with (open (FT_LP, 20000.0f)), { 57 });
            cleanTest ("default oscs, LP12 classic 20k, chord", with (open (FT_LP, 20000.0f)), { 48, 52, 55, 60 });
            cleanTest ("default oscs, HP12 20 Hz, chord", with (open (FT_HP, 20.0f)), { 48, 52, 55, 60 });
            cleanTest ("one saw 0.3, LP12 20k", with ([&] (MegaSynthProcessor& p) { open (FT_LP, 20000.0f) (p);
                for (int i : { P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain }) setP (p, i, 0.0f); setP (p, P_osc1Gain, 0.3f); }), { 57 });
        }
        // harmonic distortion of a sine through the open filter, by level
        auto thd = [&] (float gain, int type, float cut, float mix, int model = 0, float drive = 1.0f)
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain,
                           P_fEnvAmt, P_envAssignAmt0, P_lfoAssignAmt0, P_warmth }) setP (*p, i, 0.0f);
            setP (*p, P_osc1Wave, 3.0f); setP (*p, P_osc1Gain, gain); setP (*p, P_ampS, 1.0f);
            setP (*p, P_filterMode, (float) model); setP (*p, P_filterSlope, (float) nativeFilterSlope (model));
            setP (*p, P_filterType, (float) type); setP (*p, P_filterCutoff, cut); setP (*p, P_filterRes, 0.1f); setP (*p, P_filterMix, mix); setP (*p, P_filterDrive, drive);
            juce::AudioBuffer<float> b;
            render (*p, 0.6, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 127) } }, &b);   // 110 Hz
            juce::dsp::FFT fft (14);
            std::vector<float> d (32768, 0.0f);
            const int st = (int) (0.2 * sr);
            for (int i = 0; i < 16384 && st + i < b.getNumSamples(); ++i) d[(size_t) i] = b.getSample (0, st + i) * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / 16383.0f));
            fft.performFrequencyOnlyForwardTransform (d.data());
            auto band = [&] (double f) { const int k = (int) std::round (f * 16384 / sr); float m = 0; for (int j = k - 3; j <= k + 3; ++j) m = std::max (m, d[(size_t) j]); return m; };
            const double f0 = 110.0; double h = 0;
            for (int n = 2; n <= 9; ++n) h += band (f0 * n) * band (f0 * n);
            return 10 * std::log10 (h / (band (f0) * band (f0)) + 1e-30);
        };
        for (float dr : { 1.5f, 2.0f, 4.0f, 8.0f, 25.0f })
            std::cout << "  drive " << juce::String (20 * std::log10 (dr), 1) << " dB, sine 0.5: harmonics " << juce::String (thd (0.5f, FT_LP, 20000.0f, 1.0f, 0, dr), 1) << " dB" << std::endl;
        for (float g : { 0.2f, 0.5f, 0.8f, 1.0f, 1.6f })
            std::cout << "  sine level " << g << ": harmonics  mix0 " << juce::String (thd (g, FT_LP, 20000.0f, 0.0f), 1) << " dB,  LP12 classic " << juce::String (thd (g, FT_LP, 20000.0f, 1.0f), 1)
                      << ",  HP12 multimode " << juce::String (thd (g, FT_HP, 20.0f, 1.0f), 1) << ",  LP24 ladder model " << juce::String (thd (g, FT_LP, 20000.0f, 1.0f, 2), 1) << std::endl;
        return 0;
    }

    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_FILTERONLY", {}).isNotEmpty())
    {
        filterUnitTests();
        if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_SNAPSHOTS", {}).isNotEmpty())
        {
            const float looks[][8] = { { 9, FT_HP, 3, 18.0f, 900, 0, 0, 0 }, { 2, FT_LP, 3, 24.0f, 400, 1, 0, 0 }, { 0, FT_BP, 1, 10.0f, 1500, 1, 1, 0 },
                                       { 6, FT_PEAK, 1, 15.0f, 3000, 1, 1, 1 }, { 0, FT_NOTCH, 3, 4.0f, 700, 0, 0, 0 } };   // + Filter 2 on, parallel, split
            int n = 0;
            for (auto& lk : looks)
            {
                auto p = make();
                setP (*p, P_filterMode, lk[0]); setP (*p, P_filterType, lk[1]); setP (*p, P_filterSlope, lk[2]); setP (*p, P_filterRes, lk[3]); setP (*p, P_filterCutoff, lk[4]);
                setP (*p, P_filter2On, lk[5]); setP (*p, P_filterRouting, lk[6]); setP (*p, P_filterStereoSplit, lk[7]); setP (*p, P_filter2Res, 8.0f);
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                ed->setSize (1200, 860);
                std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
                {
                    if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
                    for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
                    return nullptr;
                };
                if (auto* tabs = findTabs (ed.get())) tabs->setCurrentTabIndex (tabByName (tabs, "Filter & Env"));
                auto img = ed->createComponentSnapshot (juce::Rectangle<int> (0, 88, 1200, 310), true, 1.0f);
                auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
                auto f = dir.getChildFile ("filter_ui_" + juce::String (n++) + ".png"); f.deleteFile();
                juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            }
        }
        std::cout << (failures == 0 ? "FILTER TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- DNA Sequencer update: clock, swing, restart, generator, probability, ratchets, lanes, chain, MIDI, undo
    auto dnaTests = [&]
    {
        std::cout << "DNA Sequencer update" << std::endl;
        const double fs = 48000.0;
        auto block = [&] (int sync, int restart, bool playing, double ppq, double bpm)
        {
            DnaClock::Block b {};
            b.on = true; b.sync = sync; b.rate = 2; b.restart = restart; b.freeHz = 4.0; b.offset = 0; b.swing = 0; b.swingGrid = 0; b.barSteps = 16;
            b.hostPlaying = playing; b.ppq = ppq; b.bpm = bpm; b.barStartPpq = 0; b.hasBar = false; b.beatsPerBar = 4.0; b.fallbackTempo = 120.0; b.sampleRate = fs;
            return b;
        };
        // host sync: the step follows the DAW's beat position at any tempo, including starting mid-bar
        {
            bool ok = true;
            for (double bpm : { 90.0, 140.0, 174.0, 87.5 })
                for (double ppq : { 0.0, 5.37, 13.999, 101.26 })
                {
                    DnaClock c; c.beginBlock (block (DS_Host, DR_Never, true, ppq, bpm));
                    for (double smp : { 0.0, 100.0, 511.0 })
                    {
                        const double expect = (ppq + smp * bpm / 60.0 / fs) / 0.25;
                        if (std::abs (c.stepsAt (smp) - expect) > 1e-6) ok = false;
                    }
                }
            CHECK (ok, "host sync: position = DAW beat position / step length");
            DnaClock c; c.beginBlock (block (DS_Host, DR_Never, true, 5.37, 174.0));
            CHECK ((int) std::floor (c.stepsAt (0)) % 16 == 5, "starting mid-bar (beat 5.37) lands on step 6 of 16");
            // Internal ignores the DAW; Host falls back to the tempo when the DAW stops, carrying on
            DnaClock d; auto bi = block (DS_Internal, DR_Never, true, 33.0, 174.0); bi.hostPlaying = false; bi.bpm = 0; d.beginBlock (bi);
            CHECK (std::abs (d.stepsAt (0)) < 1e-9 && std::abs (d.stepsAt (fs) - 120.0 / 60.0 / 0.25) < 1e-6, "internal: runs from the start at the synth's tempo");
            DnaClock h; h.beginBlock (block (DS_Host, DR_Never, true, 8.0, 120.0)); h.endBlock (480);
            h.beginBlock (block (DS_Host, DR_Never, false, 0.0, 120.0));
            CHECK (std::abs (h.stepsAt (0) - (8.0 + 480 * 2.0 / fs) / 0.25) < 1e-6, "host stopped: carries on from where the DAW was");
        }
        // swing: 50% on 1/16 delays every second 16th by half a 16th; downbeats don't move
        {
            DnaClock c; auto b = block (DS_Host, DR_Never, true, 0.0, 120.0); b.swing = 0.5; c.beginBlock (b);
            auto stepAtBeat = [&] (double beat) { return (int) std::floor (c.stepsAt (beat * 60.0 / 120.0 * fs)); };
            CHECK (stepAtBeat (0.30) == 0 && stepAtBeat (0.37) == 0 && stepAtBeat (0.38) == 1 && stepAtBeat (0.49) == 1 && stepAtBeat (0.501) == 2 && stepAtBeat (0.874) == 2 && stepAtBeat (0.876) == 3,
                   "swing 50%: off-beat 16ths start at 0.375 beat, downbeats on the beat");
            b.swingGrid = 1; c.beginBlock (b);   // on 1/8: the off-beat eighth (beat 0.5) moves to 0.75
            CHECK (stepAtBeat (0.74) == 1 && stepAtBeat (0.76) == 2, "swing on 1/8");
        }
        // restart / offset
        {
            DnaClock c; auto b = block (DS_Host, DR_Bar, true, 0.0, 120.0); b.hasBar = true; b.barStartPpq = 3.0; b.beatsPerBar = 3.0;   // 3/4
            b.ppq = 4.1; c.beginBlock (b);
            CHECK ((int) std::floor (c.stepsAt (0)) == 4, "each bar: restarts at the bar line (3/4)");
            b.restart = DR_Note; b.ppq = 10.0; c.beginBlock (b); c.noteOn (100.0);
            CHECK (std::abs (c.stepsAt (100.0)) < 1e-9 && std::abs (c.stepsAt (100.0 + fs * 0.125)) - 1.0 < 1e-6, "each note: restarts at the note");
            b.restart = DR_Never; b.offset = 3; b.ppq = 0.0; c.beginBlock (b);
            CHECK (std::abs (c.stepsAt (0) - 3.0) < 1e-9, "start offset");
            DnaClock f; auto bf = block (DS_Free, DR_Never, true, 7.0, 120.0); f.beginBlock (bf);
            CHECK (std::abs (f.stepsAt (fs) - 4.0) < 1e-6, "free: Free Rate in steps per second");
        }
        // generator: same seed = same pattern; locks survive; Euclidean spreads hits evenly
        {
            DnaSeqStore a, b2;
            DnaGenSettings g; g.seed = 4242; g.extras = 0.4f;
            dnaGenerate (a, 0, 0, 16, g); dnaGenerate (b2, 0, 0, 16, g);
            bool same = true; for (int i = 0; i < 32; ++i) same &= a.get (0, 0, i) == b2.get (0, 0, i);
            CHECK (same, "same seed, same pattern");
            g.seed = 4243; dnaGenerate (b2, 0, 0, 16, g);
            bool diff = false; for (int i = 0; i < 16; ++i) diff |= ! (a.get (0, 0, i) == b2.get (0, 0, i));
            CHECK (diff, "another seed, another pattern");
            DnaStep keep { DT_Octave, 0.33f, 0.4f, 3, 2, true };
            a.set (0, 0, 5, keep);
            for (int s : { 1, 2, 3 }) { g.seed = s; for (int st : { DG_Random, DG_Euclid, DG_Variation }) { g.style = st; dnaGenerate (a, 0, 0, 16, g); } }
            CHECK (a.get (0, 0, 5) == keep, "locked steps survive every generator style");
            DnaSeqStore e; DnaGenSettings ge; ge.style = DG_Euclid; ge.fill = 5.0f / 16.0f; ge.extras = 0;
            dnaGenerate (e, 0, 0, 16, ge);
            juce::String hits; int count = 0;
            for (int i = 0; i < 16; ++i) { const bool h = e.get (0, 0, i).type != DT_Off; hits << (h ? "x" : "."); count += h ? 1 : 0; }
            std::cout << "  euclidean 5 of 16: " << hits << std::endl;
            CHECK (count == 5, "euclidean: 5 hits");
            // generating with the same seed after a variation gives the variation again (undo aside)
        }
        // probability, density, ratchets, direction, lanes, chain (the evaluator)
        {
            DnaSeqStore st;
            for (int i = 0; i < 16; ++i) st.set (0, 0, i, DnaStep { DT_Fold, 1.0f, 0.5f, 1, 2, false });
            DnaPlayParams pp; pp.len[0] = 16; pp.glide = 0;
            int fired = 0; const int N = 4000;
            for (int k = 0; k < N; ++k) { DnaFrame f; dnaEvaluate (st, k + 0.1, pp, f); fired += f.w[DT_Fold] > 0.5f ? 1 : 0; }
            std::cout << "  probability 50%: played " << (100.0 * fired / N) << "% of steps" << std::endl;
            CHECK (std::abs ((double) fired / N - 0.5) < 0.03, "probability 50% plays about half the time");
            auto rate = [&] (float scale, float density)
            {
                DnaPlayParams q = pp; q.probScale = scale; q.density = density; int n = 0;
                for (int k = 0; k < N; ++k) { DnaFrame f; dnaEvaluate (st, k + 0.1, q, f); n += f.w[DT_Fold] > 0.5f ? 1 : 0; }
                return (double) n / N;
            };
            CHECK (rate (0.0f, 1.0f) == 0.0 && rate (2.0f, 1.0f) == 1.0 && std::abs (rate (1.5f, 1.0f) - 0.75) < 0.03, "probability scale: 0 = none, 200% = all");
            for (int i = 0; i < 16; ++i) st.set (0, 0, i, DnaStep { DT_Fold, 1.0f, 1.0f, 1, 2, false });
            CHECK (std::abs (rate (1.0f, 0.5f) - 0.5) < 0.2 && rate (1.0f, 0.0f) == 0.0, "density thins the pattern");
            // ratchet 3: three hits inside the step, each with a gate pulse
            st.set (0, 0, 2, DnaStep { DT_Crush, 1.0f, 1.0f, 3, 2, false });
            bool rok = true;
            for (int j = 0; j < 3; ++j)
            {
                DnaFrame on, off;
                dnaEvaluate (st, 2.0 + (j + 0.3) / 3.0, pp, on);
                dnaEvaluate (st, 2.0 + (j + 0.9) / 3.0, pp, off);
                DnaFrame hit; dnaEvaluate (st, 2.0 + (j + 0.001) / 3.0, pp, hit);
                rok &= on.w[DT_Crush] > 0.99f && off.w[DT_Crush] < 0.01f && hit.gate > 0.95f;
            }
            CHECK (rok, "ratchet x3: three hits per step at 0, 1/3 and 2/3, each with a gate pulse");
            // direction
            juce::String fwd, rev, png;
            for (int k = 0; k < 8; ++k) { fwd << dnaIndex (k, 4, DD_Forward, 1); rev << dnaIndex (k, 4, DD_Reverse, 1); png << dnaIndex (k, 4, DD_PingPong, 1); }
            CHECK (fwd == "01230123" && rev == "32103210" && png == "01232101", "directions (" + png + ")");
            std::set<int> seen; for (int k = 0; k < 200; ++k) seen.insert (dnaIndex (k, 8, DD_Random, 7));
            CHECK (seen.size() == 8, "random direction visits every step");
            // lane 2: its own length (polymeter), its own steps
            pp.lane2 = true; pp.len[1] = 12;
            st.set (0, 1, 7, DnaStep { DT_Ring, 0.8f, 1.0f, 1, 2, false });
            DnaFrame f; dnaEvaluate (st, 19.5, pp, f);
            CHECK (f.step[0] == 3 && f.step[1] == 7 && std::abs (f.w[DT_Ring] - 0.8f) < 1e-5f && std::abs (f.value[1] - 0.8f) < 1e-5f, "lane 2 runs its own 12 steps");
            // chain AB with 4-step patterns
            st.setChain ("AB"); pp.chain = true; pp.len[0] = 4; pp.lane2 = false;
            st.set (1, 0, 0, DnaStep { DT_Octave, 1.0f, 1.0f, 1, 2, false });
            DnaFrame c0, c1, c2; dnaEvaluate (st, 0.5, pp, c0); dnaEvaluate (st, 4.5, pp, c1); dnaEvaluate (st, 8.5, pp, c2);
            CHECK (c0.pattern == 0 && c1.pattern == 1 && c2.pattern == 0 && c1.w[DT_Octave] > 0.99f, "chain A B A ...");
        }
        // in the synth, with a simulated DAW transport: the playing step follows the song position
        {
            struct FakeHead : juce::AudioPlayHead
            {
                PositionInfo info;
                juce::Optional<PositionInfo> getPosition() const override { return info; }
            } head;
            bool ok = true; juce::String where;
            for (double bpm : { 90.0, 140.0, 174.0 })
            {
                auto p = std::make_unique<MegaSynthProcessor>();
                p->setPlayConfigDetails (0, 2, sr, 64); p->prepareToPlay (sr, 64);
                p->setPlayHead (&head);
                for (int i = 0; i < 16; ++i) p->dnaSteps.set (i, DT_Fold, 0.5f);
                setP (*p, P_dsOn, 1.0f);
                juce::AudioBuffer<float> buf (2, 64);
                double ppq = 5.37;   // press play mid-bar
                for (int b = 0; b < 400; ++b)
                {
                    head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                    juce::MidiBuffer midi; buf.clear(); p->processBlock (buf, midi);
                    const int expect = (int) std::floor ((ppq + 32.0 * bpm / 60.0 / sr) / 0.25) % 16;
                    if (p->getDnaSeqStep() != expect) { ok = false; where = juce::String (bpm) + " BPM block " + juce::String (b); }
                    ppq += 64.0 * bpm / 60.0 / sr;
                }
            }
            CHECK (ok, "plugin follows the DAW's song position (" + where + ")");
        }

        // in the synth: MIDI pattern select, undo, gate source, routed density, old projects
        {
            auto p = make();
            setP (*p, P_dsOn, 1.0f); setP (*p, P_dsMidiSelect, 2.0f);   // MIDI 12-15
            auto s = render (*p, 0.3, { { 0.0, juce::MidiMessage::noteOn (1, 14, (juce::uint8) 100) } });
            CHECK (std::lround (p->param (P_dsPattern)->convertFrom0to1 (p->param (P_dsPattern)->getValue())) == 2 && s.peak < 1e-6f,
                   "MIDI note 14 selects pattern C and plays nothing");

            auto q = make();
            const juce::String before = q->dnaSteps.toJson();
            q->dnaEdit (MegaSynthProcessor::DE_Generate, 0);
            const juce::String after = q->dnaSteps.toJson();
            q->undo();
            CHECK (after != before && q->dnaSteps.toJson() == before, "generating is undoable");
            q->redo();
            CHECK (q->dnaSteps.toJson() == after, "...and redoable");

            // the gate source pulses with the steps
            auto r = make();
            for (int i = 0; i < 16; ++i) r->dnaSteps.set (i, DT_Fold, 0.5f);
            setP (*r, P_dsOn, 1.0f); setP (*r, P_dsSync, (float) DS_Internal); setP (*r, P_seqTempo, 120.0f);
            r->addRoute (MS_DnaGate, P_osc1Gain, 0.3f);
            float gmax = 0, gmin = 1;
            for (int b = 0; b < 40; ++b)
            {
                render (*r, 512.0 / sr, b == 0 ? chord (0.0, 5.0, { 48 }) : std::vector<std::pair<double, juce::MidiMessage>> {});
                const float g = r->getEngine().liveSrc[(size_t) MS_DnaGate].load();
                gmax = std::max (gmax, g); gmin = std::min (gmin, g);
            }
            CHECK (gmax > 0.5f && gmin < 0.2f, "DNA step gate source pulses");

            // a route onto Density really thins it: Macro 1 -> Density -100% leaves the sound as with the sequencer off
            auto withSeq = [&] (bool routed, bool on)
            {
                auto m = make();
                for (int i = 0; i < 16; ++i) m->dnaSteps.set (i, DT_Crush, 1.0f);
                setP (*m, P_dsOn, on ? 1.0f : 0.0f); setP (*m, P_dsSync, (float) DS_Internal);
                if (routed) { m->addRoute (MS_Macro1, P_dsDensity, -1.0f); setP (*m, P_macro1, 1.0f); }
                for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain }) setP (*m, i, 0.0f);   // no random start phases
                juce::AudioBuffer<float> cap; render (*m, 0.5, chord (0.0, 0.45, { 48 }), &cap);
                return cap;
            };
            auto a1 = withSeq (true, true), a2 = withSeq (true, false), a3 = withSeq (false, true);   // a2: same route, sequencer off
            double d12 = 0, d32 = 0;
            for (int i = (int) (0.1 * sr); i < (int) (0.4 * sr); ++i) { d12 += std::abs (a1.getSample (0, i) - a2.getSample (0, i)); d32 += std::abs (a3.getSample (0, i) - a2.getSample (0, i)); }
            std::cout << "  density route: diff vs off " << d12 << ", unrouted " << d32 << std::endl;
            CHECK (d12 < d32 * 0.05, "Mod Matrix route onto Density works");

            // a project from before the update (state version 4) on the old Free rate: comes back free-running, pattern A, one lane
            auto o = make();
            setP (*o, P_dsOn, 1.0f); setP (*o, P_dsRate, 8.0f); o->dnaSteps.set (2, DT_Ring, 0.6f);
            juce::MemoryBlock mb; o->getStateInformation (mb);
            auto xml = juce::parseXML (getXmlFromBinaryForTest (mb));
            if (xml != nullptr)
            {
                xml->setAttribute ("stateVersion", 4);
                xml->removeAttribute ("dnaSeq2");
                for (auto* id : { "dsSync", "dsRestart", "dsOffset", "dsSwing", "dsSwingGrid", "dsDirection", "dsLane2On", "dsLane2Steps", "dsPattern", "dsChainOn", "dsDensity", "dsProbScale", "dsMidiSelect" })
                    if (auto* e = xml->getChildByAttribute ("id", id)) xml->removeChildElement (e, true);
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
                auto q2 = make();
                setP (*q2, P_dsLane2On, 1.0f); setP (*q2, P_dsSwing, 0.4f); setP (*q2, P_dsPattern, 3.0f);
                q2->setStateInformation (old.getData(), (int) old.getSize());
                auto pl = [&] (int i) { return q2->param (i)->convertFrom0to1 (q2->param (i)->getValue()); };
                CHECK (std::lround (pl (P_dsSync)) == DS_Free && pl (P_dsLane2On) < 0.5f && pl (P_dsSwing) == 0.0f && std::lround (pl (P_dsPattern)) == 0
                       && q2->dnaSteps.getType (2) == DT_Ring, "old project: free rate kept, new settings at their defaults, steps in pattern A");
            }
            // round trip of everything
            auto t = make();
            t->dnaSteps.set (2, 1, 9, DnaStep { DT_Blur, 0.25f, 0.6f, 4, 1, true }); t->dnaSteps.setChain ("ABCD"); t->dnaSteps.gen.seed = 77;
            juce::MemoryBlock m2; t->getStateInformation (m2);
            auto u = make(); u->setStateInformation (m2.getData(), (int) m2.getSize());
            CHECK (u->dnaSteps.toJson() == t->dnaSteps.toJson(), "project round trip (patterns, lanes, chain, generator)");
            auto v = make();
            CHECK (v->importBrowserPatch (t->exportBrowserPatch()).isEmpty() && v->dnaSteps.toJson() == t->dnaSteps.toJson(), "patch round trip");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_DNAONLY", {}).isNotEmpty())
    {
        dnaTests();
        std::cout << (failures == 0 ? "DNA TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- Effects rack: each new effect, timing against the beat, mono safety, clicks, stability, compatibility
    auto fxTests = [&]
    {
        std::cout << "Effects rack" << std::endl;
        const double fs = 48000.0;
        auto params = [] { std::vector<float> v ((size_t) P_COUNT); for (int i = 0; i < P_COUNT; ++i) v[(size_t) i] = meta (i).def; return v; };
        auto levelDb = [] (const std::vector<float>& x, size_t from) { double e = 0; for (size_t i = from; i < x.size(); ++i) e += (double) x[i] * x[i]; return 10 * std::log10 (e / (x.size() - from) + 1e-30); };
        FxContext ctx; ctx.sr = fs; ctx.tempo = 120.0; ctx.beatInc = 2.0 / fs;

        // multiband: neutral = the input exactly; downward 20:1 above -18 dB; upward 4:1 below -36 dB
        {
            auto v = params(); v[P_mbcDepth] = 0.0f;
            MultibandComp m; m.prepare (fs);
            std::vector<float> L (9600), R (9600), in (9600);
            for (int i = 0; i < 9600; ++i) in[(size_t) i] = L[(size_t) i] = R[(size_t) i] = 0.3f * (float) std::sin (i * 0.031) + 0.2f * (float) std::sin (i * 0.27) + 0.1f * (float) std::sin (i * 1.3);
            m.process (L.data(), R.data(), 9600, v.data(), ctx);
            float md = 0; for (int i = 0; i < 9600; ++i) md = std::max (md, std::abs (L[(size_t) i] - in[(size_t) i]));
            CHECK (md < 1e-5f, "multiband neutral = input (no crossover smearing), max diff " + juce::String (md));
            auto run = [&] (float amp, float up, float down)
            {
                auto w = params(); w[P_mbcDepth] = 1.0f;
                for (int b = 0; b < 3; ++b) { w[(size_t) (P_mbcUpL + 4 * b)] = up; w[(size_t) (P_mbcDownL + 4 * b)] = down; }
                MultibandComp c; c.prepare (fs);
                std::vector<float> a ((size_t) fs), bR ((size_t) fs);
                for (size_t i = 0; i < a.size(); ++i) a[i] = bR[i] = amp * (float) std::sin (2.0 * kPi * 1000.0 * i / fs);
                c.process (a.data(), bR.data(), (int) a.size(), w.data(), ctx);
                return levelDb (a, a.size() / 2) + 3.0103;   // peak dBFS of the sine
            };
            const double dn = run (0.5f, 0.0f, 1.0f), upw = run (0.00316f, 1.0f, 0.0f);
            std::cout << "  multiband: -6 dBFS in -> " << juce::String (dn, 1) << " dBFS (20:1 above -18);  -50 dBFS in -> " << juce::String (upw, 1) << " dBFS (4:1 below -36)" << std::endl;
            CHECK (std::abs (dn - (-6.02 - 12.0 * 0.95)) < 1.0, "multiband downward ratio");
            CHECK (std::abs (upw - (-50.0 + 14.0 * 0.75)) < 1.0, "multiband upward ratio");
        }
        // vintage sampler: bit depth, the machine's rate, aliasing with / without the anti-alias filter
        {
            auto v = params(); v[P_smpModel] = 3; v[P_smpBits] = 4; v[P_smpRate] = 48000; v[P_smpAA] = 0; v[P_smpDrive] = 0; v[P_smpNoise] = 0; v[P_smpCutoff] = 20000;
            VintageSampler s; s.prepare (fs);
            std::vector<float> L (4800), R (4800);
            for (int i = 0; i < 4800; ++i) L[(size_t) i] = R[(size_t) i] = 0.7f * (float) std::sin (i * 0.013);
            s.process (L.data(), R.data(), 4800, v.data(), ctx);
            std::set<int> levels; bool onGrid = true;
            for (float x : L) { levels.insert ((int) std::lround (x * 8)); onGrid &= std::abs (x * 8 - std::round (x * 8)) < 1e-4f; }
            CHECK (onGrid && levels.size() <= 16, "4 bits: 16 levels (" + juce::String ((int) levels.size()) + " used)");
            auto runModel = [&] (int model, double freq, std::vector<float>& out)
            {
                auto w = params(); w[P_smpModel] = (float) model; w[P_smpRate] = 26040; w[P_smpDrive] = 0; w[P_smpNoise] = 0; w[P_smpCutoff] = 20000; w[P_smpRes] = 0;
                VintageSampler t; t.prepare (fs);
                out.assign (16384, 0.0f); std::vector<float> r2 (16384);
                for (int i = 0; i < 16384; ++i) out[(size_t) i] = r2[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPi * freq * i / fs);
                t.process (out.data(), r2.data(), 16384, w.data(), ctx);
            };
            std::vector<float> sp, s9;
            {   // noise in, so every new sample differs from the last
                auto w = params(); w[P_smpModel] = 0; w[P_smpDrive] = 0; w[P_smpNoise] = 0;
                VintageSampler t; t.prepare (fs);
                sp.assign (16384, 0.0f); std::vector<float> r2 (16384);
                uint32_t z = 3; for (int i = 0; i < 16384; ++i) { z = z * 1664525u + 1013904223u; sp[(size_t) i] = r2[(size_t) i] = ((z >> 8) / 16777216.0f - 0.5f); }
                t.process (sp.data(), r2.data(), 16384, w.data(), ctx);
            }
            int runs = 0; for (size_t i = 1; i < sp.size(); ++i) runs += sp[i] != sp[i - 1] ? 1 : 0;
            const double hold = (double) sp.size() / std::max (1, runs);
            std::cout << "  SP-1200: a new sample every " << juce::String (hold, 3) << " output samples (48 kHz / 26.04 kHz = 1.843)" << std::endl;
            CHECK (std::abs (hold - 48000.0 / 26040.0) < 0.05, "SP-1200 runs at 26.04 kHz");
            auto bandDb = [&] (std::vector<float> x, double f)
            {
                juce::dsp::FFT fft (14); x.resize (32768, 0.0f);
                for (int i = 0; i < 16384; ++i) x[(size_t) i] *= 0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / 16383.0);
                fft.performFrequencyOnlyForwardTransform (x.data());
                const int k = (int) std::round (f * 16384 / fs); float m = 0; for (int j = k - 3; j <= k + 3; ++j) m = std::max (m, x[(size_t) j]);
                return 20 * std::log10 (m + 1e-9);
            };
            runModel (0, 15000.0, sp); runModel (1, 15000.0, s9);
            const double aliasSp = bandDb (sp, 26040.0 - 15000.0) - bandDb (sp, 15000.0), aliasS9 = bandDb (s9, 26040.0 - 15000.0) - bandDb (s9, 15000.0);
            std::cout << "  15 kHz in: alias at 11.04 kHz vs 15 kHz  SP-1200 " << juce::String (aliasSp, 1) << " dB,  S950 (anti-alias filter) " << juce::String (aliasS9, 1) << " dB" << std::endl;
            CHECK (aliasSp > -3.0, "SP-1200 aliases (no anti-alias filter)");
            const double sp11 = bandDb (sp, 11040.0), s911 = bandDb (s9, 11040.0);
            CHECK (s911 < sp11 - 15.0, "S950's anti-alias filter removes most of the alias (" + juce::String (sp11 - s911, 1) + " dB less)");
        }
        // flanger: the delay sweeps 0.3 .. 10.3 ms with Manual; through-zero lines up with the dry path
        {
            auto delayPeak = [&] (float manual, bool tz)
            {
                auto v = params(); v[P_flpDepth] = 0; v[P_flpFeedback] = 0; v[P_flpManual] = manual; v[P_flpTZ] = tz ? 1.0f : 0.0f;
                FlangerPhaser f; f.prepare (fs);
                std::vector<float> L (2000, 0.0f), R (2000, 0.0f); L[100] = R[100] = 1.0f;
                f.process (L.data(), R.data(), 2000, v.data(), ctx);
                std::vector<std::pair<float, int>> pk;
                for (int i = 0; i < 2000; ++i) if (std::abs (L[(size_t) i]) > 0.1f) pk.push_back ({ L[(size_t) i], i - 100 });
                return pk;
            };
            auto lo = delayPeak (0.0f, false), hi = delayPeak (1.0f, false), tz = delayPeak (0.5f, true);
            double hiMs = -1; { float wsum = 0, wpos = 0; for (auto& t : hi) if (t.second > 100) { wsum += std::abs (t.first); wpos += std::abs (t.first) * t.second; } if (wsum > 0) hiMs = wpos / wsum / fs * 1000.0; }
            std::cout << "  flanger delay: Manual 100% -> " << juce::String (hiMs, 2) << " ms;  through-zero, no depth: " << (int) tz.size() << " tap(s)" << std::endl;
            float tzSum = 0; for (auto& t : tz) tzSum += std::abs (t.first);
            CHECK (std::abs (hiMs - 10.0) < 0.1 && ! lo.empty() && lo.back().second < 20, "flanger delay range 0.3 .. 10 ms");
            CHECK (tz.size() >= 1 && tz.size() <= 2 && tzSum > 1.1f, "through-zero: wet and dry meet");
        }
        // volume shaper: locked to the beat at several tempos
        {
            bool ok = true;
            for (double tempo : { 90.0, 140.0, 174.0 })
            {
                auto v = params(); v[P_vshDepth] = 1.0f; v[P_vshSmooth] = 0.0f; v[P_vshRate] = 0;
                FxRackStore st; st.setCurvePreset (1);   // gate: on for 1/16, off for 1/16
                VolumeShaper sh; sh.reset();
                FxContext c = ctx; c.tempo = tempo; c.beatInc = tempo / 60.0 / fs; c.beat = 10.0;
                std::vector<float> L ((size_t) fs), R ((size_t) fs, 1.0f); std::fill (L.begin(), L.end(), 1.0f);
                sh.process (L.data(), R.data(), (int) L.size(), v.data(), c, st);
                auto at = [&] (double beat) { return L[(size_t) ((beat - 10.0) / c.beatInc)]; };
                ok &= at (10.06) > 0.99f && at (10.19) < 0.01f && at (11.06) > 0.99f && at (11.20) < 0.01f;
            }
            CHECK (ok, "volume shaper follows the beat (90 / 140 / 174 BPM)");
        }
        // stereo tools: the mono sum never changes; Bass Mono removes low side; Width scales the side
        {
            auto v = params(); v[P_sttWidth] = 2.0f; v[P_sttHaas] = 20.0f; v[P_sttMono] = 1.0f; v[P_sttMonoFreq] = 150.0f;
            StereoTools t; t.prepare (fs);
            std::vector<float> L (9600), R (9600), sum (9600);
            uint32_t r = 7; auto rnd = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f - 0.5f; };
            for (int i = 0; i < 9600; ++i) { L[(size_t) i] = rnd(); R[(size_t) i] = rnd(); sum[(size_t) i] = L[(size_t) i] + R[(size_t) i]; }
            t.process (L.data(), R.data(), 9600, v.data(), ctx);
            float md = 0; for (int i = 0; i < 9600; ++i) md = std::max (md, std::abs (L[(size_t) i] + R[(size_t) i] - sum[(size_t) i]));
            CHECK (md < 1e-5f, "stereo tools: mono sum unchanged");
            auto side = [&] (double f)
            {
                StereoTools u; u.prepare (fs); auto w = params(); w[P_sttMonoFreq] = 150.0f;
                std::vector<float> a ((size_t) fs), b ((size_t) fs);
                for (size_t i = 0; i < a.size(); ++i) { a[i] = 0.5f * (float) std::sin (2.0 * kPi * f * i / fs); b[i] = -a[i]; }
                u.process (a.data(), b.data(), (int) a.size(), w.data(), ctx);
                return levelDb (a, a.size() / 2) + 9.03;   // 0 dB = unchanged
            };
            const double s40 = side (40.0), s2k = side (2000.0);
            std::cout << "  bass mono: side signal at 40 Hz " << juce::String (s40, 1) << " dB, at 2 kHz " << juce::String (s2k, 1) << " dB" << std::endl;
            CHECK (s40 < -20.0 && std::abs (s2k) < 0.5, "bass mono below 150 Hz only");
        }

        // Beat Repeat on its own: it records from the trigger (the first pass is the live input), then loops that slice
        {
            BeatRepeat br; br.prepare (sr);
            std::array<float, P_COUNT> v {}; for (int i = 0; i < P_COUNT; ++i) v[(size_t) i] = meta (i).def;
            v[P_rptLength] = 2; v[P_rptGate] = 1; v[P_rptShrink] = 0; v[P_rptPitch] = 0;   // 1/16 at 120 BPM = 6000 samples
            FxContext ctx; ctx.sr = sr; ctx.tempo = 120.0;
            const int seg = 6000, n = 64;
            std::vector<float> in, out;
            for (int b = 0; b < 400; ++b)
            {
                float L[64], R[64];
                for (int i = 0; i < n; ++i) { const int t = b * n + i; L[i] = R[i] = std::sin (0.0123f * t) * (0.2f + 0.8f * (float) ((t / 997) % 7) / 7.0f); in.push_back (L[i]); }
                br.process (L, R, n, v.data(), ctx, b >= 100);
                for (int i = 0; i < n; ++i) out.push_back (L[i]);
            }
            const int t0 = 100 * n;
            double e1 = 0, e2 = 0;
            for (int i = 200; i < seg - 200; ++i) e1 = std::max (e1, (double) std::abs (out[(size_t) (t0 + i)] - in[(size_t) (t0 + i)]));
            for (int i = 200; i < seg - 200; ++i) e2 = std::max (e2, (double) std::abs (out[(size_t) (t0 + seg + i)] - in[(size_t) (t0 + i)]));
            CHECK (e1 < 1e-3 && e2 < 1e-3, "Beat Repeat: first pass is live, then the slice from the trigger repeats (" + juce::String (e1) + ", " + juce::String (e2) + ")");
        }

        // in the synth, with a simulated DAW transport: Beat Repeat starts on the beat and repeats the 1/8 after it
        {
            struct FakeHead : juce::AudioPlayHead
            {
                PositionInfo info;
                juce::Optional<PositionInfo> getPosition() const override { return info; }
            } head;
            for (double bpm : { 90.0, 174.0 })
            {
                auto p = std::make_unique<MegaSynthProcessor>();
                p->setPlayConfigDetails (0, 2, sr, 64); p->prepareToPlay (sr, 64);
                p->setPlayHead (&head);
                for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain }) setP (*p, i, 0.0f);
                setP (*p, P_fxOnStutter, 1.0f); setP (*p, P_rptChance, 1.0f); setP (*p, P_rptLength, 1.0f); setP (*p, P_rptDuration, 1.0f);
                setP (*p, P_lfoAssignAmt0, 0.0f);
                juce::AudioBuffer<float> buf (2, 64);
                double ppq = 2.37; int firstActive = -1; double firstPpq = 0;
                std::vector<float> out;
                for (int b = 0; b < 1200; ++b)
                {
                    head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                    juce::MidiBuffer midi; if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), 0);
                    buf.clear(); p->processBlock (buf, midi);
                    for (int i = 0; i < 64; ++i) out.push_back (buf.getSample (0, i));
                    if (firstActive < 0 && p->getEngine().fx().stutter.activity.load() > 0) { firstActive = b; firstPpq = ppq; }
                    ppq += 64.0 * bpm / 60.0 / sr;
                }
                const double endPpq = firstPpq + 64.0 * bpm / 60.0 / sr;
                CHECK (firstActive >= 0 && std::floor (endPpq) == 3.0 && std::floor (firstPpq) == 2.0, "beat repeat starts at the beat line (" + juce::String (bpm) + " BPM)");
                // during the repeat the output repeats every 1/8 note
                const int seg = (int) std::lround (0.5 * 60.0 / bpm * sr);
                const int st = firstActive * 64 + seg + seg / 4;
                double num = 0, d1 = 0, d2 = 0;
                for (int i = st; i < st + seg / 2; ++i) { num += out[(size_t) i] * out[(size_t) (i - seg)]; d1 += out[(size_t) i] * out[(size_t) i]; d2 += out[(size_t) (i - seg)] * out[(size_t) (i - seg)]; }
                const double corr = num / std::sqrt (d1 * d2 + 1e-30);
                CHECK (corr > 0.98, "beat repeat repeats the 1/8 (correlation " + juce::String (corr, 3) + ")");
            }
        }

        // no clicks: switching an effect on and moving it in the rack while a note plays
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_analogDrift, P_supersawGain, P_complexGain, P_lfoAssignAmt0 }) setP (*p, i, 0.0f);
            setP (*p, P_osc1Wave, 3.0f); setP (*p, P_osc2Gain, 0.0f); setP (*p, P_osc3Gain, 0.0f); setP (*p, P_subGain, 0.0f);
            juce::AudioBuffer<float> a, b, c;
            render (*p, 0.5, chord (0.0, 3.0, { 57 }), &a);
            setP (*p, P_fxOnFlanger, 1.0f); setP (*p, P_fxOnMultiband, 1.0f); setP (*p, P_fxOnSampler, 1.0f);
            render (*p, 0.4, {}, &b);
            p->fxRack.move (5, 0);   // the reverbs to the top
            p->fxRack.move (8, 2);
            render (*p, 0.4, {}, &c);
            auto maxStep = [] (const juce::AudioBuffer<float>& x, int from, int to) { float m = 0; for (int i = std::max (1, from); i < to; ++i) m = std::max (m, std::abs (x.getSample (0, i) - x.getSample (0, i - 1))); return m; };
            const float steadyA = maxStep (a, 12000, a.getNumSamples()), steadyB = maxStep (b, 9000, b.getNumSamples()), steadyC = maxStep (c, 9000, c.getNumSamples());
            const float swOn = maxStep (b, 0, 3000), swMove = maxStep (c, 0, 3000);
            std::cout << "  largest sample step: switching on " << swOn << " (steady " << std::max (steadyA, steadyB) << "), reordering " << swMove << " (steady " << std::max (steadyB, steadyC) << ")" << std::endl;
            CHECK (swOn < std::max (steadyA, steadyB) * 1.3f + 0.002f, "switching effects on doesn't click");
            CHECK (swMove < std::max (steadyB, steadyC) * 1.3f + 0.002f, "reordering doesn't click");
            CHECK (p->getLatencySamples() == 0, "the effects add no latency");
        }

        // stability: everything on, extreme settings thrown around
        {
            auto p = make();
            for (int k = 0; k < FS_COUNT; ++k) setP (*p, P_fxOnStutter + 2 * k, 1.0f);
            uint32_t r = 99; auto rnd = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f; };
            bool finite = true; float peak = 0;
            for (int blk = 0; blk < 60; ++blk)
            {
                for (int i = 0; i < P_COUNT; ++i)
                {
                    const juce::String id (kParamIds[i]);
                    if (! (id.startsWith ("mbc") || id.startsWith ("smp") || id.startsWith ("rpt") || id.startsWith ("flp") || id.startsWith ("vsh") || id.startsWith ("stt"))) continue;
                    p->param (i)->setValueNotifyingHost (rnd() < 0.5f ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd());
                }
                auto st = render (*p, 0.05, blk == 0 ? chord (0.0, 3.0, { 36, 48, 60, 72 }) : std::vector<std::pair<double, juce::MidiMessage>> {});
                finite &= st.finite; peak = std::max (peak, st.peak);
            }
            std::cout << "  stress: peak " << peak << std::endl;
            CHECK (finite && peak < 8.0f, "effects stay stable and bounded under extreme settings");
        }

        // compatibility: a project from before the rack gets the new effects off and the default order
        {
            auto o = make();
            setP (*o, P_delayMix, 0.4f);
            juce::MemoryBlock mb; o->getStateInformation (mb);
            auto xml = juce::parseXML (getXmlFromBinaryForTest (mb));
            if (xml != nullptr)
            {
                xml->setAttribute ("stateVersion", 5);
                xml->removeAttribute ("fxRack");
                juce::Array<juce::XmlElement*> drop;
                for (auto* e : xml->getChildIterator())
                {
                    const juce::String id = e->getStringAttribute ("id");
                    for (auto* pre : { "fxOn", "fxMix", "mbc", "smp", "rpt", "flp", "vsh", "stt" }) if (id.startsWith (pre)) { drop.add (e); break; }
                }
                for (auto* e : drop) xml->removeChildElement (e, true);
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
                auto q = make();
                setP (*q, P_fxOnSampler, 1.0f); setP (*q, P_fxOnDelay, 0.0f); q->fxRack.move (0, 7);
                q->setStateInformation (old.getData(), (int) old.getSize());
                auto ord = q->fxRack.getOrder(); bool identity = true; for (int k = 0; k < FS_COUNT; ++k) identity &= ord[(size_t) k] == k;
                CHECK (q->param (P_fxOnSampler)->getValue() < 0.5f && q->param (P_fxOnDelay)->getValue() > 0.5f && identity, "old project: new effects off, original ones on, default order");
            }
            auto t = make(); t->fxRack.move (2, 6); t->fxRack.setCurve (5, 0.25f);
            auto u = make(); u->importBrowserPatch (t->exportBrowserPatch());
            CHECK (u->fxRack.getOrder() == t->fxRack.getOrder() && std::abs (u->fxRack.curve (5) - 0.25f) < 0.002f, "rack order and shaper curve saved in patches");
            t->fxRack.move (0, 3); t->pushHistory ("move");
            const auto moved = t->fxRack.getOrder();
            t->undo();
            CHECK (t->fxRack.getOrder() != moved, "rack moves are undoable");
        }
    };
    // ---- Playability: MIDI learn, A/B compare, patch browser catalogue, window size memory
    auto playTests = [&]
    {
        std::cout << "Playability" << std::endl;
        auto cc = [] (int n, int v) { return std::vector<std::pair<double, juce::MidiMessage>> { { 0.0, juce::MidiMessage::controllerEvent (1, n, v) } }; };
        auto norm = [] (MegaSynthProcessor& q, int i) { return q.param (i)->getValue(); };
        {
            auto p = make();
            CHECK (p->ccForParam (P_filterCutoff) < 0, "nothing mapped at start");
            p->midiLearn (P_filterCutoff);
            CHECK (p->learningParam() == P_filterCutoff, "learn armed");
            render (*p, 0.02, cc (21, 0));
            CHECK (p->ccForParam (P_filterCutoff) == 21 && p->learningParam() < 0, "first CC moved is learned");
            CHECK (norm (*p, P_filterCutoff) < 0.01f, "learned CC sets the parameter (0)");
            render (*p, 0.02, cc (21, 127));
            CHECK (norm (*p, P_filterCutoff) > 0.99f, "learned CC sets the parameter (127)");
            render (*p, 0.02, cc (21, 64));
            CHECK (std::abs (norm (*p, P_filterCutoff) - 64.0f / 127.0f) < 0.01f, "learned CC is linear over the knob");
            // a second parameter on another CC; re-learning moves a CC
            p->midiLearn (P_delayMix); render (*p, 0.02, cc (22, 100));
            CHECK (p->ccForParam (P_delayMix) == 22 && p->ccForParam (P_filterCutoff) == 21, "two mappings");
            p->midiLearn (P_filterRes); render (*p, 0.02, cc (21, 10));
            CHECK (p->ccForParam (P_filterRes) == 21 && p->ccForParam (P_filterCutoff) < 0, "re-learning a CC takes it from the old parameter");
            p->midiLearn (P_filterRes); render (*p, 0.02, cc (23, 10));
            CHECK (p->ccForParam (P_filterRes) == 23, "re-learning a parameter moves it to the new CC");
            CHECK (p->midiMapToString().indexOf ("21:") < 0, "the old CC is released");
            // channel-mode messages are never learned
            p->midiLearn (P_filterDrive); render (*p, 0.02, cc (123, 0));
            CHECK (p->ccForParam (P_filterDrive) < 0 && p->learningParam() == P_filterDrive, "CC 120+ (channel mode) aren't learned");
            p->midiForget (P_filterDrive);
            CHECK (p->learningParam() < 0, "forget cancels a pending learn");
            // forget
            p->midiForget (P_delayMix);
            const float before = norm (*p, P_delayMix);
            render (*p, 0.02, cc (22, 0));
            CHECK (p->ccForParam (P_delayMix) < 0 && std::abs (norm (*p, P_delayMix) - before) < 1e-6f, "forgotten CC no longer moves the parameter");
            // the map belongs to the project: saved with it, untouched by loading patches
            p->midiLearn (P_mutAmount); render (*p, 0.02, cc (30, 50));
            p->uiWidth = 1500;
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q->ccForParam (P_filterRes) == 23 && q->ccForParam (P_mutAmount) == 30, "MIDI map saved with the project");
            CHECK (q->uiWidth.load() == 1500, "window size saved with the project");
            q->loadFactoryPreset (1);
            CHECK (q->ccForParam (P_filterRes) == 23, "loading a patch keeps the MIDI map");
            const auto patch = q->exportBrowserPatch();
            auto r = make(); r->importBrowserPatch (patch);
            CHECK (r->ccForParam (P_filterRes) < 0, "patches don't carry the MIDI map");
            // old projects (no map) load with nothing mapped
            auto o = make(); juce::MemoryBlock m0; o->getStateInformation (m0);
            auto o2 = make(); o2->setStateInformation (m0.getData(), (int) m0.getSize());
            CHECK (o2->midiMapToString().isEmpty() && o2->uiWidth.load() == 0, "no map, default size");
            // mapped CC works through the editor-free path at any time in a block, and the mapping is audible
            auto a = make(); setP (*a, P_filterCutoff, 200.0f);
            a->midiLearn (P_filterCutoff);
            std::vector<std::pair<double, juce::MidiMessage>> ev { { 0.0, juce::MidiMessage::controllerEvent (1, 40, 0) }, { 0.0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) },
                                                                   { 0.25, juce::MidiMessage::controllerEvent (1, 40, 127) } };
            juce::AudioBuffer<float> cap; render (*a, 0.5, ev, &cap);
            auto hf = [&] (int from, int to) { double e = 0, d = 0; float prev = 0; for (int i = from; i < to; ++i) { const float x = cap.getSample (0, i); d += (x - prev) * (x - prev); e += x * x; prev = x; } return d / std::max (1e-12, e); };
            std::cout << "  hf " << hf (int (0.15 * sr), int (0.25 * sr)) << " -> " << hf (int (0.4 * sr), int (0.5 * sr)) << std::endl;
            CHECK (hf (int (0.4 * sr), int (0.5 * sr)) > 2.0 * hf (int (0.15 * sr), int (0.25 * sr)), "learned CC opens the filter audibly");
        }
        {
            // A/B compare
            auto p = make();
            setP (*p, P_filterCutoff, 1000.0f); p->pushHistory ("x"); p->markOriginal();
            auto cut = [&] { return p->param (P_filterCutoff)->convertFrom0to1 (p->param (P_filterCutoff)->getValue()); };
            CHECK (p->abSlot() == 0, "starts on A");
            p->abSelect (1);
            CHECK (p->abSlot() == 1 && std::abs (cut() - 1000.0f) < 1.0f, "B starts as the loaded patch");
            setP (*p, P_filterCutoff, 5000.0f); p->fxRack.move (0, 5);
            const auto orderB = p->fxRack.getOrder();
            p->abSelect (0);
            CHECK (std::abs (cut() - 1000.0f) < 1.0f && p->fxRack.getOrder() != orderB, "A keeps its own settings (params and rack)");
            p->abSelect (1);
            CHECK (std::abs (cut() - 5000.0f) < 2.0f && p->fxRack.getOrder() == orderB, "B keeps its edits");
            p->abSelect (1);
            CHECK (std::abs (cut() - 5000.0f) < 2.0f, "selecting the current slot changes nothing");
            p->abCopyToOther();
            p->abSelect (0);
            CHECK (std::abs (cut() - 5000.0f) < 2.0f, "Copy makes the other slot the same");
            setP (*p, P_filterCutoff, 300.0f); p->abSelect (1);
            p->undo();
            CHECK (p->abSlot() == 1 && std::abs (cut() - 300.0f) < 1.0f, "undo after switching brings back the sound before the switch");
            p->loadFactoryPreset (0);
            CHECK (p->abSlot() == 0, "loading a patch resets A/B");
            // switching while playing doesn't blow up
            auto q = make(); q->markOriginal(); setP (*q, P_filterRes, 0.9f);
            std::vector<std::pair<double, juce::MidiMessage>> ev { { 0.0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) } };
            auto st1 = render (*q, 0.2, ev); q->abSelect (1); auto st2 = render (*q, 0.2, {}); q->abSelect (0); auto st3 = render (*q, 0.2, {});
            CHECK (st1.finite && st2.finite && st3.finite && st2.peak < 4.0f, "A/B while playing");
        }
        {
            // patch browser catalogue and favourites
            tgui::PatchCatalogue c; c.rescan();
            const auto cats = c.categories();
            CHECK (cats[0] == "All" && cats[1] == "Favourites" && cats.contains ("Your patches"), "browser categories");
            for (auto& f : factoryPresets()) CHECK (cats.contains (f.category), "factory category listed");
            auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("megasynth_favs_test.txt");
            tmp.deleteFile();
            tgui::Favourites fav (tmp);
            const auto all = c.filter ("All", "", fav);
            CHECK (all.size() >= (int) factoryPresets().size(), "All lists every factory preset");
            const auto& f0 = factoryPresets()[0];
            auto hit = c.filter ("All", juce::String (f0.name).toUpperCase(), fav);
            bool found = false; for (int i : hit) found |= c[i].factory && c[i].index == 0;
            CHECK (found, "search finds a preset by name (any case)");
            auto cat0 = c.filter (f0.category, "", fav);
            bool only = cat0.size() > 0; for (int i : cat0) only &= c[i].category == f0.category;
            CHECK (only, "category filter");
            CHECK (c.filter ("All", "zzqq-no-such-patch", fav).isEmpty(), "search with no match");
            const auto words = juce::String (f0.category) + " " + juce::String (f0.name).upToFirstOccurrenceOf (" ", false, false);
            CHECK (c.filter ("All", words, fav).size() > 0, "several words, all must match");
            CHECK (c.filter ("Favourites", "", fav).isEmpty(), "no favourites yet");
            fav.toggle (c[all[2]].key());
            CHECK (c.filter ("Favourites", "", fav).size() == 1, "favourite added");
            tgui::Favourites fav2 (tmp);
            CHECK (fav2.contains (c[all[2]].key()), "favourites saved");
            fav2.toggle (c[all[2]].key());
            CHECK (tgui::Favourites (tmp).all().isEmpty(), "favourite removed");
            tmp.deleteFile();
        }
        {
            // window size memory
            auto p = make(); p->uiWidth = 1500;
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            CHECK (ed->getWidth() == 1500 && std::abs (ed->getHeight() - 1075) <= 1, "editor opens at the project's size");
            ed->setSize (900, 645);
            CHECK (p->uiWidth.load() == 900, "resizing is remembered");
            auto q = make();
            std::unique_ptr<juce::AudioProcessorEditor> e2 (q->createEditor());
            CHECK (e2->getWidth() == 1080, "new projects open at 90%");
        }
    };
    // everything on at once: 16 voices, every oscillator and engine, both filters, every effect, Quality High
    auto worst = [&] (int quality)
    {
        auto p = make();
        setP (*p, P_polyphony, 16); setP (*p, P_quality, (float) quality);
        for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain, P_complexGain, P_supersawGain }) setP (*p, i, 0.3f);
        setP (*p, P_supersawVoices, 9);
        for (int i : { P_wmMix, P_arRing, P_arFm, P_arShiftMix, P_dnaMix, P_resMix, P_grMix, P_ciAmount, P_mutAmount }) setP (*p, i, 0.5f);
        setP (*p, P_spOn, 1); setP (*p, P_spMix, 0.5f); setP (*p, P_fbDlGr, 0.3f);
        setP (*p, P_filter2On, 1); setP (*p, P_filterRouting, 1); setP (*p, P_filterType, (float) FT_BP); setP (*p, P_filterSlope, 3);
        setP (*p, P_filter2Type, (float) FT_LP); setP (*p, P_filter2Slope, 3);
        for (int k = 0; k < FS_COUNT; ++k) setP (*p, P_fxOnStutter + 2 * k, 1.0f);
        setP (*p, P_reverbMix, 0.4f); setP (*p, P_shimmerMix, 0.3f); setP (*p, P_reverseMix, 0.3f);
        setP (*p, P_rptChance, 0.5f); setP (*p, P_flpTZ, 1);
        setP (*p, P_dsOn, 1); setP (*p, P_dsLane2On, 1); setP (*p, P_dsSwing, 0.2f);
        p->dnaEdit (MegaSynthProcessor::DE_Generate, 0); p->dnaEdit (MegaSynthProcessor::DE_Generate, 1);
        std::vector<std::pair<double, juce::MidiMessage>> ev;
        for (int n = 0; n < 16; ++n) ev.push_back ({ 0.01 * n, juce::MidiMessage::noteOn (1, 36 + n * 3, (juce::uint8) 100) });
        render (*p, 0.5, {});   // let the reverbs load
        double cpu = 0;
        auto st = render (*p, 3.0, ev, nullptr, &cpu);
        const double pct = cpu / 3.0 * 100.0;
        std::cout << "  worst case (" << kQualityLabels[quality] << "): " << pct << "% of one core, peak " << st.peak << std::endl;
        CHECK (st.finite && st.peak < 8.0f, "worst case stays finite and bounded");
        return pct;
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_WORST", {}).isNotEmpty()) { worst (1); return 0; }

    // ---- Hardening: worst-case CPU, host programs, DNA clock settings as Mod Matrix destinations
    auto hardTests = [&]
    {
        std::cout << "Hardening" << std::endl;
        const double eco = worst (0), normal = worst (1), high = worst (2);
        CHECK (eco <= normal * 1.15, "Eco is no heavier than Normal");
        CHECK (high < 200.0, "worst case at High runs in real time with headroom on one core (here: under 2x)");

        // host programs = factory presets
        {
            auto p = make();
            const auto& list = factoryPresets();
            CHECK (p->getNumPrograms() == (int) list.size(), "one host program per factory preset");
            CHECK (p->getProgramName (3) == list[3].name && p->getProgramName (-1).isEmpty() && p->getProgramName (999).isEmpty(), "program names");
            p->setCurrentProgram (5);   // the test runs on the message thread: loads straight away
            CHECK (p->getCurrentProgram() == 5 && p->getPatchName() == list[5].name, "setCurrentProgram loads the preset");
            setP (*p, P_filterCutoff, 123.0f);
            p->setCurrentProgram (5);
            CHECK (std::abs (p->param (P_filterCutoff)->convertFrom0to1 (p->param (P_filterCutoff)->getValue()) - 123.0f) < 1.0f, "re-selecting the current program keeps your edits");
            p->setCurrentProgram (999);
            CHECK (p->getCurrentProgram() == 5, "out-of-range programs are ignored");
            // a project saved after editing restores its patch even if the host re-sends the program number
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            q->setCurrentProgram (5);
            CHECK (q->getCurrentProgram() == 5 && std::abs (q->param (P_filterCutoff)->convertFrom0to1 (q->param (P_filterCutoff)->getValue()) - 123.0f) < 1.0f, "host restoring the program number doesn't overwrite the project");
            // an old project (no program saved) and a host that sets program 0 on load
            auto o = make(); setP (*o, P_filterCutoff, 456.0f);
            juce::MemoryBlock m0; o->getStateInformation (m0);
            auto xml = juce::String::fromUTF8 ((const char*) m0.getData() + 8, (int) m0.getSize() - 8);
            CHECK (xml.contains ("program=\"0\""), "program saved");
            auto o2 = make(); o2->setStateInformation (m0.getData(), (int) m0.getSize()); o2->setCurrentProgram (0);
            CHECK (std::abs (o2->param (P_filterCutoff)->convertFrom0to1 (o2->param (P_filterCutoff)->getValue()) - 456.0f) < 1.0f, "program 0 on load keeps the project");
            // from another thread: queued for the message thread
            auto t = make();
            std::thread th ([&] { t->setCurrentProgram (7); });
            th.join();
            CHECK (t->getCurrentProgram() == 7 && t->getPatchName() != list[7].name, "off the message thread the load waits");
            t->timerCallback();
            CHECK (t->getPatchName() == list[7].name, "... and happens on the message thread");
            // VST3: one program (no automatable Program parameter), but MIDI Program Change still works
            {
                juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
                auto v3 = make();
                juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
                CHECK (v3->getNumPrograms() == 1 && v3->getCurrentProgram() == 0, "VST3 has a single program");
                v3->setCurrentProgram (4);
                CHECK (v3->getPatchName() != list[4].name, "VST3 ignores host program changes");
                render (*v3, 0.02, { { 0.0, juce::MidiMessage::programChange (1, 4) } });
                v3->timerCallback();
                CHECK (v3->getPatchName() == list[4].name, "VST3 still takes MIDI Program Change");
            }
            // MIDI Program Change
            render (*t, 0.02, { { 0.0, juce::MidiMessage::programChange (1, 9) } });
            t->timerCallback();
            CHECK (t->getPatchName() == list[9].name && t->getCurrentProgram() == 9, "MIDI Program Change loads a preset");
            // a pending program change doesn't overwrite a project the host restores afterwards
            render (*t, 0.02, { { 0.0, juce::MidiMessage::programChange (1, 2) } });
            t->setStateInformation (mb.getData(), (int) mb.getSize());
            t->timerCallback();
            CHECK (t->getPatchName() == p->getPatchName(), "restoring state cancels a pending program change");
        }

        // DNA clock settings can be modulated (Swing, Start Offset, Free Rate, Lane 2 Steps)
        {
            for (int id : { P_dsSwing, P_dsOffset, P_dsFreeHz, P_dsLane2Steps }) CHECK (meta (id).modulatable, juce::String ("modulatable: ") + meta (id).id);
            auto routeTo = [] (MegaSynthProcessor& p, int slot, int src, int dst, float amt)
            {
                RouteConfig c; c.on = true; c.src = src; c.dst = dst; p.routes.set (slot, c); setP (p, P_mod1Amt + slot, amt);
            };
            auto stepsIn = [&] (MegaSynthProcessor& p, double secs, int lane)
            {
                juce::AudioBuffer<float> b (2, 64); int changes = 0, last = -2;
                for (int k = 0; k < (int) (secs * sr / 64); ++k)
                {
                    b.clear(); juce::MidiBuffer m; if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
                    p.processBlock (b, m);
                    const int s2 = lane == 0 ? p.getEngine().dnaSeqStep.load() : p.getEngine().dnaSeqStep2.load();
                    if (s2 != last) { ++changes; last = s2; }
                }
                return changes;
            };
            auto base = [&] (float macro)
            {
                auto p = make();
                setP (*p, P_dsOn, 1); setP (*p, P_dsSync, 2); setP (*p, P_dsFreeHz, 4.0f); setP (*p, P_dsSteps, 16);
                for (int i = 0; i < 16; ++i) p->dnaSteps.set (i, DT_Fold, 0.5f);
                setP (*p, P_macro1, macro);
                return p;
            };
            auto a = base (0.0f), b = base (1.0f);
            routeTo (*a, 0, MS_Macro1, P_dsFreeHz, 0.5f); routeTo (*b, 0, MS_Macro1, P_dsFreeHz, 0.5f);
            const int sa = stepsIn (*a, 2.0, 0), sb = stepsIn (*b, 2.0, 0);
            std::cout << "  free rate: " << sa << " steps unmodulated, " << sb << " with Macro 1 up" << std::endl;
            CHECK (sa >= 7 && sa <= 9 && sb > sa * 2, "Macro 1 speeds up the Free rate");
            // Start Offset: the step that plays first moves
            auto c = base (1.0f); routeTo (*c, 0, MS_Macro1, P_dsOffset, 0.25f);   // 0.25 of 0..31 = ~8 steps
            setP (*c, P_dsSync, 1); setP (*c, P_dsRestart, 1);   // internal clock, restart on each note
            juce::AudioBuffer<float> blk (2, 64);
            for (int k = 0; k < 4; ++k) { blk.clear(); juce::MidiBuffer m; c->processBlock (blk, m); }   // the route settles
            { blk.clear(); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); c->processBlock (blk, m); }
            const int first = c->getEngine().dnaSeqStep.load();
            std::cout << "  offset route: first step " << first << std::endl;
            CHECK (first >= 6 && first <= 9, "Macro 1 moves the Start Offset");
            // Lane 2 length
            auto d = base (1.0f); setP (*d, P_dsLane2On, 1); setP (*d, P_dsLane2Steps, 4);
            for (int i = 0; i < 32; ++i) d->dnaSteps.set (0, 1, i, DnaStep { DT_Crush, 0.5f });
            routeTo (*d, 0, MS_Macro1, P_dsLane2Steps, 0.25f);   // 4 + ~8 = 12
            int maxStep = 0;
            for (int k = 0; k < (int) (4.0 * sr / 64); ++k)
            {
                blk.clear(); juce::MidiBuffer m; d->processBlock (blk, m);
                maxStep = std::max (maxStep, d->getEngine().dnaSeqStep2.load());
            }
            CHECK (maxStep >= 9, "Macro 1 lengthens Lane 2");
            // Swing: off-beat steps arrive later
            auto sw = [&] (float macro)
            {
                auto p = make();
                setP (*p, P_dsOn, 1); setP (*p, P_dsSync, 1); setP (*p, P_dsRate, 2); setP (*p, P_macro1, macro);
                for (int i = 0; i < 16; ++i) p->dnaSteps.set (i, DT_Fold, 0.5f);
                routeTo (*p, 0, MS_Macro1, P_dsSwing, 0.5f);
                juce::AudioBuffer<float> bb (2, 32); int when = -1;
                for (int k = 0; k < (int) (1.0 * sr / 32) && when < 0; ++k)
                {
                    bb.clear(); juce::MidiBuffer m; p->processBlock (bb, m);
                    if (p->getEngine().dnaSeqStep.load() == 1) when = k;
                }
                return when;
            };
            const int w0 = sw (0.0f), w1 = sw (1.0f);
            std::cout << "  swing route: step 2 at block " << w0 << " vs " << w1 << std::endl;
            CHECK (w1 > w0 + 3, "Macro 1 adds Swing");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_HARDONLY", {}).isNotEmpty())
    {
        hardTests();
        std::cout << (failures == 0 ? "HARD TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_PLAYONLY", {}).isNotEmpty())
    {
        playTests();
        std::cout << (failures == 0 ? "PLAY TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_FXONLY", {}).isNotEmpty())
    {
        fxTests();
        std::cout << (failures == 0 ? "FX TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

   #include "Golden.inc"

    // ---- 1. default patch chord
    {
        std::cout << "Default patch chord" << std::endl;
        auto p = make();
        juce::AudioBuffer<float> cap;
        double cpu = 0;
        auto s = render (*p, 4.0, chord (0.1, 1.5, { 48, 52, 55, 60 }), &cap, &cpu);
        std::cout << "  peak " << s.peak << " rms " << s.rms << " cpu " << cpu << "s for 4s audio" << std::endl;
        CHECK (s.finite, "non-finite output");
        CHECK (s.peak > 0.01f, "silent");
        CHECK (s.peak < 4.0f, "too loud");
        writeWav (cap, sr, "01_default_chord.wav");
    }

    // ---- 2. every filter mode, low and high resonance
    {
        std::cout << "Filter modes" << std::endl;
        for (int mode = 0; mode < kListFilter.size; ++mode)
        {
            for (float res : { 1.5f, 12.0f, 25.0f })
            {
                auto p = make();
                setP (*p, P_filterMode, (float) mode);
                setP (*p, P_filterRes, res);
                setP (*p, P_filterCutoff, 800.0f);
                juce::AudioBuffer<float> cap;
                auto s = render (*p, 1.6, chord (0.05, 0.9, { 36 }), &cap);
                std::cout << "  " << juce::String (kFilterLabels[mode]).paddedRight (' ', 22) << " res " << res
                          << "  peak " << s.peak << "  rms " << s.rms << std::endl;
                CHECK (s.finite, juce::String (kFilterLabels[mode]) + " non-finite");
                CHECK (s.peak > 0.002f, juce::String (kFilterLabels[mode]) + " silent");
                CHECK (s.peak < 8.0f, juce::String (kFilterLabels[mode]) + " blew up");
                if (res == 25.0f) writeWav (cap, sr, "02_filter_" + juce::String (mode).paddedLeft ('0', 2) + "_" + kFilterKeys[mode] + ".wav");
            }
        }
    }

    // ---- 3. sequencer (internal clock) with a random phrase
    {
        std::cout << "Sequencer" << std::endl;
        auto p = make();
        setP (*p, P_filterMode, 12.0f);  // TB-303
        setP (*p, P_filterRes, 14.0f);
        p->generateRandomPhrase();
        setP (*p, P_seqRun, 1.0f);
        juce::AudioBuffer<float> cap;
        auto s = render (*p, 6.0, {}, &cap);
        std::cout << "  peak " << s.peak << " rms " << s.rms << " last step " << p->currentStep.load() << std::endl;
        CHECK (s.finite, "seq non-finite");
        CHECK (s.peak > 0.01f, "seq silent");
        CHECK (p->currentStep.load() >= 0, "sequencer never stepped");
        writeWav (cap, sr, "03_sequencer_303.wav");
        setP (*p, P_seqRun, 0.0f);
        auto tail = render (*p, 6.0, {});
        juce::AudioBuffer<float> last;
        auto quiet = render (*p, 0.5, {}, &last);
        std::cout << "  after stop: peak " << quiet.peak << std::endl;
        CHECK (quiet.peak < 0.001f, "sequencer kept playing after stop");
        juce::ignoreUnused (tail);
    }

    // ---- 4. modulation + FM + ring + all effects up
    {
        std::cout << "Mod / FM / ring / effects" << std::endl;
        auto p = make();
        setP (*p, P_fmAmount, 600);
        setP (*p, P_fmSlot2Amt, 300);         // osc1 -> osc2 (feedback pair with the legacy osc2 -> osc1)
        setP (*p, P_ringMix, 0.6f);
        setP (*p, P_ringSlot1Amt, 0.7f);
        setP (*p, P_ringSlot2Dest, 6);
        setP (*p, P_ringSlot2Source, 4);
        setP (*p, P_ringSlot2Amt, 0.5f);
        setP (*p, P_shimmerMix, 0.5f);
        setP (*p, P_reverseMix, 0.5f);
        setP (*p, P_delaySync, 6);
        setP (*p, P_lfoAssignTarget1, (float) MT_delayMix);
        setP (*p, P_lfoAssignAmt1, 0.5f);
        setP (*p, P_envAssignTarget1, (float) MT_complexFm);
        setP (*p, P_envAssignAmt1, 0.6f);
        juce::AudioBuffer<float> cap;
        auto s = render (*p, 5.0, chord (0.1, 2.0, { 45, 57, 64 }), &cap);
        std::cout << "  peak " << s.peak << " rms " << s.rms << std::endl;
        CHECK (s.finite, "non-finite");
        CHECK (s.peak > 0.01f && s.peak < 8.0f, "level out of range");
        writeWav (cap, sr, "04_mod_fm_ring_fx.wav");
    }

    // ---- 5. Osc 4 sample playback
    {
        std::cout << "Sample oscillator" << std::endl;
        juce::AudioBuffer<float> sine (1, 48000);
        for (int i = 0; i < 48000; ++i) sine.setSample (0, i, 0.5f * std::sin (2.0 * kPi * 440.0 * i / 48000.0));
        juce::MemoryBlock wavData;
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::MemoryOutputStream (wavData, false), 48000.0, 1, 16, {}, 0));
            w->writeFromAudioSampleBuffer (sine, 0, 48000);
        }
        auto p = make();
        CHECK (p->loadSampleData (wavData, "sine.wav"), "WAV decode failed");
        std::cout << "  " << p->getSampleStatus() << std::endl;
        for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain }) setP (*p, i, 0.0f);
        setP (*p, P_osc4Gain, 1.0f);
        setP (*p, P_filterCutoff, 12000.0f);
        for (int mode = 0; mode < 4; ++mode)
        {
            setP (*p, P_osc4LoopMode, (float) mode);
            auto s = render (*p, 1.5, chord (0.05, 0.8, { 69 }));
            std::cout << "  loop mode " << kLoopLabels[mode] << ": peak " << s.peak << std::endl;
            CHECK (s.finite && s.peak > 0.01f, juce::String ("sample osc silent in mode ") + kLoopLabels[mode]);
        }

        // Wavetable 2 on its own
        CHECK (p->loadSampleData (wavData, "sine2.wav", 1), "WT2 decode failed");
        setP (*p, P_osc4Gain, 0.0f);
        setP (*p, P_wt2Gain, 1.0f);
        {
            auto s2 = render (*p, 1.0, chord (0.05, 0.6, { 57 }));
            std::cout << "  wavetable 2: peak " << s2.peak << std::endl;
            CHECK (s2.finite && s2.peak > 0.01f, "WT2 silent");
        }
        setP (*p, P_osc4Gain, 1.0f);

        // state round trip keeps the sample and sequence
        p->steps.set (5, Step { 7, 1, TieSlide, true });
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (q->getSampleStatus().startsWith ("Loaded"), "sample not restored from state");
        CHECK (q->getSampleStatus (1).contains ("sine2"), "WT2 sample not restored from state");
        const Step st = q->steps.get (5);
        CHECK (st.note == 7 && st.oct == 1 && st.tie == TieSlide && st.accent, "sequence not restored from state");
        CHECK (std::abs (q->param (P_osc4Gain)->getValue() - p->param (P_osc4Gain)->getValue()) < 1.0e-6f, "params not restored");
        std::cout << "  state size " << state.getSize() << " bytes" << std::endl;

        // browser patch round trip
        const auto json = p->exportBrowserPatch();
        auto r = make();
        const auto err = r->importBrowserPatch (json);
        CHECK (err.isEmpty(), "import error: " + err);
        CHECK (r->getSampleStatus().startsWith ("Loaded"), "sample not imported from patch JSON");
        CHECK (std::abs (r->param (P_osc4Gain)->getValue() - p->param (P_osc4Gain)->getValue()) < 1.0e-4f, "osc4Gain not imported");
        CHECK (r->steps.get (5).note == 7, "sequence not imported");

        // patch file round trip, including plugin-only settings and the name
        setP (*p, P_warmth, 0.9f);
        setP (*p, P_seqClock, 1.0f);
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Reese Test.megasynth");
        CHECK (p->savePatchToFile (tmp), "patch save failed");
        auto q2 = make();
        const auto e2 = q2->loadPatchFromFile (tmp);
        CHECK (e2.isEmpty(), "patch load error " + e2);
        CHECK (q2->getPatchName() == "Reese Test", "patch name not restored: " + q2->getPatchName());
        CHECK (std::abs (q2->param (P_warmth)->getValue() - p->param (P_warmth)->getValue()) < 1.0e-4f, "warmth not saved in patch");
        CHECK (juce::roundToInt (q2->param (P_seqClock)->convertFrom0to1 (q2->param (P_seqClock)->getValue())) == 1, "seq clock not saved in patch");
        CHECK (q2->getSampleStatus().startsWith ("Loaded"), "sample not saved in patch file");
        CHECK (q2->getSampleStatus (1).contains ("sine2"), "WT2 sample not saved in patch file");
        tmp.deleteFile();
    }

    // ---- 6. a patch in the browser's own JSON format
    {
        std::cout << "Browser patch import" << std::endl;
        auto p = make();
        const char* json = R"({"params":{"masterVolume":"0.3","osc1Wave":"pulse25","filterMode":"tb303","filterCutoff":"900",
            "delaySync":"1/8d","seqScaleRoot":"F#","seqScaleType":"phrygian","fmSlot1Source":"complex","ringSlot2Dest":"master","polyphony":"6"},
            "sequence":[{"note":"F#","oct":"-1","tie":"slide","accent":"accent"},{"note":"REST","oct":"0","tie":"rest","accent":"normal"}],
            "lfoAssignments":[{"source":"lfo2","target":"cutoff","amount":1200},{"source":"lfo2","target":"none","amount":0},{"source":"lfo3","target":"none","amount":0}],
            "envAssignments":[{"source":"env1","target":"pitch","amount":-600},{"source":"env2","target":"none","amount":0},{"source":"env3","target":"none","amount":0}],
            "wavetable":null})";
        const auto err = p->importBrowserPatch (json);
        CHECK (err.isEmpty(), "import error " + err);
        auto plain = [&] (int i) { return p->param (i)->convertFrom0to1 (p->param (i)->getValue()); };
        CHECK (juce::roundToInt (plain (P_osc1Wave)) == 4, "osc1Wave");
        CHECK (juce::roundToInt (plain (P_filterMode)) == 12, "filterMode");
        CHECK (std::abs (plain (P_filterCutoff) - 900.0f) < 1.0f, "cutoff");
        CHECK (juce::roundToInt (plain (P_delaySync)) == 6, "delaySync");
        CHECK (juce::roundToInt (plain (P_seqScaleRoot)) == 6, "scale root");
        CHECK (juce::roundToInt (plain (P_lfoAssignSource0)) == 1, "lfo slot source");
        CHECK (std::abs (plain (P_lfoAssignAmt0) * kTargetRange[MT_cutoff] - 1200.0f) < 1.0f, "lfo slot amount");
        CHECK (std::abs (plain (P_envAssignAmt0) * kTargetRange[MT_pitch] + 600.0f) < 1.0f, "env slot amount");
        CHECK (p->steps.get (0).note == 6 && p->steps.get (0).oct == -1 && p->steps.get (0).tie == TieSlide, "step 1");
        auto s = render (*p, 1.0, chord (0.0, 0.5, { 50 }));
        CHECK (s.finite && s.peak > 0.001f, "imported patch silent");
    }

    // ---- 6b. semitone dial tunes correctly (A3 + 7 semitones = E4, 329.6 Hz)
    {
        auto p = make();
        for (int i : { P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain, P_delayMix, P_reverbMix, P_chorusMix, P_analogDrift })
            setP (*p, i, 0.0f);
        setP (*p, P_osc1Wave, 3.0f);   // sine
        setP (*p, P_osc1Semi, 7.0f);
        setP (*p, P_filterCutoff, 12000.0f);
        juce::AudioBuffer<float> cap;
        render (*p, 1.0, chord (0.0, 0.9, { 57 }), &cap);
        int zc = 0; const int a = 12000, b = 36000;
        for (int i = a + 1; i < b; ++i) if (cap.getSample (0, i - 1) < 0 && cap.getSample (0, i) >= 0) ++zc;
        const double f = zc / ((b - a) / sr);
        std::cout << "Semitone dial: measured " << f << " Hz (expected 329.6)" << std::endl;
        CHECK (std::abs (f - 329.63) < 3.0, "semitone tuning off");
    }

    // ---- Stage 2: registry, versioning, history
    {
        std::cout << "Registry / versioning / history" << std::endl;
        const auto& reg = registry();
        CHECK ((int) reg.size() == P_COUNT, "registry size");
        int mods = 0, audio = 0, noModule = 0;
        for (auto& m : reg) { mods += m.modulatable; audio += m.audioRate; noModule += m.module.isEmpty(); }
        std::cout << "  " << reg.size() << " parameters, " << mods << " modulatable, " << audio << " audio-rate capable" << std::endl;
        CHECK (noModule == 0, "parameter without a module");
        const auto& cut = meta (P_filterCutoff);
        CHECK (cut.path == "filter.filterCutoff" && cut.scale == Scale::Log && cut.unit == "Hz" && cut.modulatable && cut.audioRate, "filterCutoff metadata");
        CHECK (! meta (P_polyphony).modulatable && meta (P_osc1Detune).bipolar, "admin / bipolar flags");
        for (int i : { P_filterCutoff, P_porta, P_osc1Detune, P_ampA })
        {
            const float v = meta (i).def;
            CHECK (std::abs (fromNormalised (i, toNormalised (i, v)) - v) < 1.0e-3f * std::max (1.0f, std::abs (v)), "normalise round trip " + meta (i).id);
        }
        CHECK (std::abs (fromNormalised (P_filterCutoff, 0.5f) - 632.5f) < 1.0f, "log scaling centre (20 Hz - 20 kHz, per octave)");

        auto p = make();
        CHECK (p->exportBrowserPatch().contains ("\"version\": 6"), "patch version missing");
        juce::MemoryBlock st; p->getStateInformation (st);
        CHECK (getXmlFromBinaryForTest (st).contains ("stateVersion=\"6\""), "state version missing");

        // history: edit, undo, redo, original
        const float orig = p->param (P_filterCutoff)->getValue();
        setP (*p, P_filterCutoff, 500.0f);
        p->pushHistory ("cutoff");
        p->steps.set (3, Step { 4, 1, TieNormal, true });
        p->pushHistory ("step");
        p->undo();
        CHECK (p->steps.get (3).note != 4, "undo didn't revert the step");
        CHECK (std::abs (p->param (P_filterCutoff)->convertFrom0to1 (p->param (P_filterCutoff)->getValue()) - 500.0f) < 1.0f, "undo went too far");
        p->undo();
        CHECK (std::abs (p->param (P_filterCutoff)->getValue() - orig) < 1.0e-5f, "undo didn't restore cutoff");
        p->redo(); p->redo();
        CHECK (p->steps.get (3).note == 4, "redo didn't restore the step");
        p->returnToOriginal();
        CHECK (std::abs (p->param (P_filterCutoff)->getValue() - orig) < 1.0e-5f && p->steps.get (3).note != 4, "return to original");
        p->undo();
        CHECK (p->steps.get (3).note == 4, "undo after return to original");
        std::cout << "  history ok" << std::endl;
    }

    // ---- Stage 3: universal modulation matrix
    {
        std::cout << "Modulation matrix" << std::endl;
        // a clean single sine on Osc 1 so pitch can be measured from zero crossings
        auto clean = [&]
        {
            auto p = make();
            for (int i : { P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain, P_osc4Gain, P_wt2Gain,
                           P_delayMix, P_reverbMix, P_chorusMix, P_analogDrift, P_warmth, P_bassKeep, P_lfoAssignAmt0, P_envAssignAmt0 })
                setP (*p, i, 0.0f);
            setP (*p, P_osc1Wave, 3.0f);          // sine
            setP (*p, P_filterCutoff, 12000.0f);
            return p;
        };
        auto freqOf = [&] (const juce::AudioBuffer<float>& cap, double t0, double t1)
        {
            int zc = 0; const int a = (int) (t0 * sr), b = (int) (t1 * sr);
            for (int i = a + 1; i < b; ++i) if (cap.getSample (0, i - 1) < 0 && cap.getSample (0, i) >= 0) ++zc;
            return zc / ((b - a) / sr);
        };
        auto note = [] (int n, float vel) { return std::vector<std::pair<double, juce::MidiMessage>> {
            { 0.0, juce::MidiMessage::noteOn (1, n, vel) }, { 0.9, juce::MidiMessage::noteOff (1, n) } }; };
        auto route = [] (MegaSynthProcessor& p, int slot, int src, int dst, float amt, int curve = MC_Linear, int via = MS_None, float viaDepth = 1.0f)
        {
            RouteConfig c; c.on = true; c.src = src; c.dst = dst; c.curve = curve; c.via = via; c.viaDepth = viaDepth;
            p.routes.set (slot, c);
            setP (p, P_mod1Amt + slot, amt);
        };

        // curves
        CHECK (applyCurve (MC_Exp, 0.5f) == 0.25f && applyCurve (MC_Inverted, 0.3f) == -0.3f && applyCurve (MC_Rectified, -0.4f) == 0.4f
               && std::abs (applyCurve (MC_Log, 0.25f) - 0.5f) < 1e-6f && applyCurve (MC_Quantized, 0.3f) == 0.25f, "curve shapes");

        juce::AudioBuffer<float> cap;
        { auto p = clean(); render (*p, 1.0, note (57, 1.0f), &cap); }
        const double f0 = freqOf (cap, 0.2, 0.8);
        std::cout << "  no routes: " << f0 << " Hz" << std::endl;
        CHECK (std::abs (f0 - 220.0) < 2.0, "base pitch");

        // velocity -> Osc 1 semitones, +50% of the dial's 24 st span = +12 st
        { auto p = clean(); route (*p, 0, MS_Velocity, P_osc1Semi, 0.5f); render (*p, 1.0, note (57, 1.0f), &cap); }
        double f = freqOf (cap, 0.2, 0.8);
        std::cout << "  velocity -> osc1 semi: " << f << " Hz" << std::endl;
        CHECK (std::abs (f - 440.0) < 3.0, "velocity -> semitone route");

        // two routes onto one destination add up
        { auto p = clean(); route (*p, 0, MS_Velocity, P_osc1Semi, 0.25f); route (*p, 5, MS_Velocity, P_osc1Semi, 0.25f); render (*p, 1.0, note (57, 1.0f), &cap); }
        CHECK (std::abs (freqOf (cap, 0.2, 0.8) - 440.0) < 3.0, "many-to-one routes");

        // via: mod wheel scales the route; wheel down = no change, wheel up = full depth
        {
            auto p = clean(); route (*p, 0, MS_Velocity, P_osc1Semi, 0.5f, MC_Linear, MS_ModWheel, 1.0f);
            render (*p, 1.0, note (57, 1.0f), &cap);
            CHECK (std::abs (freqOf (cap, 0.2, 0.8) - 220.0) < 2.0, "via at zero should mute the route");
            auto ev = note (57, 1.0f); ev.insert (ev.begin(), { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) });
            render (*p, 1.0, ev, &cap);
            CHECK (std::abs (freqOf (cap, 0.2, 0.8) - 440.0) < 3.0, "via at full");
        }

        // modulation of a modulation depth: the wheel opens route 1's depth via route 2
        {
            auto p = clean(); route (*p, 0, MS_Velocity, P_osc1Semi, 0.0f); route (*p, 1, MS_ModWheel, P_mod1Amt, 1.0f);
            auto ev = note (57, 1.0f); ev.insert (ev.begin(), { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) });
            render (*p, 1.0, ev, &cap);
            const double fm = freqOf (cap, 0.2, 0.8);
            std::cout << "  wheel -> route depth: " << fm << " Hz" << std::endl;
            CHECK (std::abs (fm - 440.0) < 3.0, "route depth modulation");   // depth 0 pushed to +100%: +12 st (clamped at the dial's end)
        }

        // LFO 4 -> cutoff changes the sound; the same route at zero depth (almost exactly) doesn't
        {
            auto run = [&] (int routeMode, juce::AudioBuffer<float>& out)
            {
                auto p = clean(); setP (*p, P_osc1Wave, 0.0f); setP (*p, P_filterCutoff, 800.0f); setP (*p, P_lfo4Rate, 3.0f);
                if (routeMode > 0) route (*p, 0, MS_Lfo4, P_filterCutoff, routeMode == 1 ? 0.4f : 0.0f);
                render (*p, 1.0, note (45, 0.8f), &out);
            };
            juce::AudioBuffer<float> a, b, c;
            run (0, a); run (1, b); run (2, c);
            auto maxDiff = [] (const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
            { float d = 0; for (int i = 0; i < x.getNumSamples(); ++i) d = std::max (d, std::abs (x.getSample (0, i) - y.getSample (0, i))); return d; };
            CHECK (maxDiff (a, b) > 0.01f, "LFO -> cutoff had no effect");
            std::cout << "  zero-depth route max diff " << maxDiff (a, c) << std::endl;
            CHECK (maxDiff (a, c) < 1.0e-3f, "zero-depth route changed the sound");
        }

        // audio rate: Osc 2's output driving Osc 1's pitch (FM) and the cutoff
        for (int dst : { P_osc1Semi, P_filterCutoff, P_osc1Gain })
        {
            auto p = clean(); setP (*p, P_osc2Wave, 3.0f); setP (*p, P_osc2Semi, 7.0f); setP (*p, P_filterCutoff, 1500.0f);
            juce::AudioBuffer<float> a, b;
            render (*p, 0.6, note (57, 1.0f), &a);
            route (*p, 0, MS_Osc2Audio, dst, 0.3f);
            const auto st = render (*p, 0.6, note (57, 1.0f), &b);
            double e = 0; for (int i = 0; i < a.getNumSamples(); ++i) e += std::pow (a.getSample (0, i) - b.getSample (0, i), 2.0);
            std::cout << "  audio-rate Osc2 -> " << kParamIds[dst] << ": diff energy " << e << " peak " << st.peak << std::endl;
            CHECK (st.finite && e > 1.0, "audio-rate route had no effect");
        }
        // an audio-rate source onto a control-only destination is ignored, not half-applied
        { RouteStore rs; RouteConfig c; c.on = true; c.src = MS_Osc1Audio; c.dst = P_reverbMix; rs.set (0, c); RouteSet set; set.build (rs); CHECK (set.n == 0, "audio source on control-rate destination"); }

        // velocity -> amp attack is read at note-on
        {
            auto p = clean(); setP (*p, P_ampA, 0.005f); route (*p, 0, MS_Velocity, P_ampA, 0.6f);
            juce::AudioBuffer<float> slow, fast;
            render (*p, 0.5, note (57, 1.0f), &slow);
            render (*p, 0.5, note (57, 0.05f), &fast);
            auto lvlAt = [&] (const juce::AudioBuffer<float>& b, double t) { return b.getMagnitude (0, (int) (t * sr), 480); };
            std::cout << "  velocity -> attack: level at 50 ms " << lvlAt (slow, 0.05) << " (hard hit, slow attack) vs " << lvlAt (fast, 0.05) << " (soft)" << std::endl;
            CHECK (lvlAt (slow, 0.05) < 0.3f * lvlAt (fast, 0.05), "velocity -> attack");
        }

        // global effects follow the matrix too (wheel -> delay return with no note playing is fine)
        { auto p = make(); route (*p, 0, MS_ModWheel, P_delayMix, 0.8f); route (*p, 1, MS_Lfo1, P_chorusMix, 0.5f);
          auto st = render (*p, 1.0, chord (0.0, 0.5, { 60 }), &cap); CHECK (st.finite, "global routes"); }

        // save / load / undo
        {
            auto p = clean(); route (*p, 3, MS_RandSmooth, P_filterRes, -0.35f, MC_SCurve, MS_Velocity, 0.5f);
            juce::MemoryBlock st; p->getStateInformation (st);
            auto q = make(); q->setStateInformation (st.getData(), (int) st.getSize());
            const auto c = q->routes.get (3);
            CHECK (c.on && c.src == MS_RandSmooth && c.dst == P_filterRes && c.curve == MC_SCurve && c.via == MS_Velocity
                   && std::abs (c.viaDepth - 0.5f) < 1e-6f && std::abs (q->param (P_mod4Amt)->convertFrom0to1 (q->param (P_mod4Amt)->getValue()) + 0.35f) < 1e-3f, "route state round trip");
            const auto json = p->exportBrowserPatch();
            CHECK (json.contains ("modMatrix") && json.contains ("RandSmooth") && json.contains ("filterRes"), "route in patch");
            auto r = make(); r->importBrowserPatch (json);
            CHECK (r->routes.get (3).src == MS_RandSmooth, "route patch import");
            // an older patch (no matrix) clears the routes
            juce::String old = json.replace ("\"modMatrix\"", "\"ignored\"");
            r->importBrowserPatch (old);
            CHECK (! r->routes.get (3).active(), "old patch should clear routes");
            // undo
            p->pushHistory ("x");
            p->routes.clear (3);
            p->pushHistory ("cleared");
            p->undo();
            CHECK (p->routes.get (3).active(), "undo restores a route");
        }

        // ---- Stage 4: macros
        {
            std::cout << "Macros & scenes" << std::endl;
            auto p = clean(); route (*p, 0, MS_Macro1, P_osc1Semi, 0.5f);
            render (*p, 1.0, note (57, 1.0f), &cap);
            CHECK (std::abs (freqOf (cap, 0.2, 0.8) - 220.0) < 2.0, "macro at zero");
            setP (*p, P_macro1, 1.0f);
            render (*p, 1.2, note (57, 1.0f), &cap);
            CHECK (std::abs (freqOf (cap, 0.4, 1.0) - 440.0) < 3.0, "macro at full");
            // a chain: mod wheel -> macro 2 -> (macro 2 -> osc1 semi)
            auto q = clean(); route (*q, 0, MS_Macro2, P_osc1Semi, 0.5f); route (*q, 1, MS_ModWheel, P_macro2, 1.0f);
            auto ev = note (57, 1.0f); ev.insert (ev.begin(), { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) });
            render (*q, 1.0, ev, &cap);
            const double fc = freqOf (cap, 0.2, 0.8);
            std::cout << "  wheel -> macro -> pitch: " << fc << " Hz" << std::endl;
            CHECK (std::abs (fc - 440.0) < 3.0, "route onto a macro drives the macro's routes");
        }

        // ---- Stage 4: scenes
        {
            auto p = clean();
            p->storeScene (0);                                   // A: osc1 +0 st, Standard LP
            setP (*p, P_osc1Semi, 12.0f); setP (*p, P_filterMode, 12.0f);
            p->storeScene (1);                                   // B: +12 st, TB-303
            setP (*p, P_osc1Semi, 0.0f); setP (*p, P_filterMode, 0.0f);
            CHECK (std::abs (p->scenes.plainValue (1, P_osc1Semi) - 12.0f) < 1e-3f && ! p->scenes.stored[2].load(), "scene store");
            setP (*p, P_sceneMorph, 1.0f);
            auto at = [&] (float x) { setP (*p, P_sceneX, x); setP (*p, P_sceneY, 0.0f); render (*p, 1.2, note (57, 1.0f), &cap); return freqOf (cap, 0.4, 1.0); };
            const double fa = at (0.0f), fb = at (1.0f), fm = at (0.5f);
            std::cout << "  morph A " << fa << " Hz, B " << fb << " Hz, halfway " << fm << " Hz (expect 220 / 440 / 311)" << std::endl;
            CHECK (std::abs (fa - 220.0) < 2.0 && std::abs (fb - 440.0) < 3.0 && std::abs (fm - 311.1) < 3.0, "scene morph pitch");
            // choices come from the nearest scene
            float v[P_COUNT]; for (int i = 0; i < P_COUNT; ++i) v[i] = 0.0f;
            morphScenes (p->scenes, 0.8f, 0.0f, v);
            CHECK ((int) std::lround (v[P_filterMode]) == 12, "nearest scene's filter type");
            morphScenes (p->scenes, 0.2f, 0.0f, v);
            CHECK ((int) std::lround (v[P_filterMode]) == 0, "nearest scene's filter type (A)");
            // velocity -> Scene X: each note morphs on its own
            setP (*p, P_sceneX, 0.0f);
            route (*p, 0, MS_Velocity, P_sceneX, 1.0f);
            render (*p, 1.2, note (57, 1.0f), &cap);
            const double fv = freqOf (cap, 0.4, 1.0);
            std::cout << "  velocity -> scene X: " << fv << " Hz" << std::endl;
            CHECK (std::abs (fv - 440.0) < 3.0, "modulated scene position");
            p->clearRoute (0);

            // editing with morph on writes into the edited scene
            p->editScene (1);
            CHECK (std::abs (p->param (P_osc1Semi)->convertFrom0to1 (p->param (P_osc1Semi)->getValue()) - 12.0f) < 1e-3f, "edit scene recalls it");
            setP (*p, P_osc1Detune, 25.0f);
            p->syncSceneEdits();
            CHECK (std::abs (p->scenes.plainValue (1, P_osc1Detune) - 25.0f) < 0.01f && std::abs (p->scenes.plainValue (0, P_osc1Detune)) < 0.01f, "panel edits go to the edited scene");

            // save / load / patch / undo
            p->setMacroName (2, "Wobble");
            juce::MemoryBlock st; p->getStateInformation (st);
            auto q = make(); q->setStateInformation (st.getData(), (int) st.getSize());
            CHECK (q->scenes.stored[1].load() && std::abs (q->scenes.plainValue (1, P_osc1Semi) - 12.0f) < 1e-3f && q->getEditScene() == 1
                   && q->getMacroName (2) == "Wobble", "scenes and macro names in state");
            const auto json = p->exportBrowserPatch();
            auto r = make(); r->importBrowserPatch (json);
            CHECK (r->scenes.stored[0].load() && r->getMacroName (2) == "Wobble" && r->param (P_sceneMorph)->getValue() > 0.5f, "scenes in patch");
            r->importBrowserPatch (json.replace ("\"scenes\"", "\"x\"").replace ("\"sceneMorph\"", "\"y\""));
            CHECK (! r->scenes.stored[0].load() && r->param (P_sceneMorph)->getValue() < 0.5f, "patch without scenes clears them");
            p->pushHistory ("before");
            p->clearScenes();
            p->pushHistory ("cleared");
            p->undo();
            CHECK (p->scenes.stored[1].load(), "undo restores scenes");
        }

        // ---- Stage 5: wave mutation and the audio-rate transform
        {
            std::cout << "Wave mutation / audio-rate transform" << std::endl;
            // magnitude spectrum of 0.2 .. 0.88 s, as (frequency of the peak, energy near f, centroid)
            struct Spec { std::vector<float> mag; double binHz = 0; };
            auto spectrum = [&] (const juce::AudioBuffer<float>& b)
            {
                const int order = 15, N = 1 << order;
                juce::dsp::FFT fft (order);
                std::vector<float> d ((size_t) N * 2, 0.0f);
                const int start = (int) (0.2 * sr);
                for (int i = 0; i < N && start + i < b.getNumSamples(); ++i)
                    d[(size_t) i] = b.getSample (0, start + i) * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (N - 1)));
                fft.performFrequencyOnlyForwardTransform (d.data());
                Spec sp; sp.mag.assign (d.begin(), d.begin() + N / 2); sp.binHz = sr / N;
                return sp;
            };
            auto peakHz = [] (const Spec& sp) { size_t k = 1; for (size_t i = 2; i < sp.mag.size(); ++i) if (sp.mag[i] > sp.mag[k]) k = i; return k * sp.binHz; };
            auto near = [] (const Spec& sp, double f) { const int k = (int) std::lround (f / sp.binHz); float e = 0; for (int i = k - 3; i <= k + 3; ++i) e += sp.mag[(size_t) i] * sp.mag[(size_t) i]; return e; };
            auto centroid = [] (const Spec& sp) { double a = 0, w = 0; for (size_t i = 1; i < sp.mag.size(); ++i) { a += sp.mag[i] * i * sp.binHz; w += sp.mag[i]; } return a / std::max (1e-9, w); };
            auto run = [&] (std::function<void (MegaSynthProcessor&)> setup, juce::AudioBuffer<float>& out)
            {
                auto p = clean(); setP (*p, P_ampR, 0.05f); setup (*p);
                return render (*p, 1.0, note (57, 1.0f), &out);
            };
            juce::AudioBuffer<float> base, b;
            run ([] (MegaSynthProcessor&) {}, base);
            const auto sBase = spectrum (base);
            CHECK (std::abs (peakHz (sBase) - 220.0) < 3.0, "spectrum helper");

            // mix at zero = untouched, whatever the other settings
            run ([] (MegaSynthProcessor& p) { setP (p, P_wmFold, 1.0f); setP (p, P_wmBits, 3.0f); setP (p, P_arShift, 300.0f); setP (p, P_arRing, 0.0f);
                                              setP (p, P_dnaMode, 3.0f); setP (p, P_dnaAmount, 1.0f); setP (p, P_dnaA, 6.0f);
                                              setP (p, P_resTuning, 5.0f); setP (p, P_resFeedback, 1.0f);
                                              setP (p, P_grFreeze, 1.0f); setP (p, P_grFeedback, 0.9f); setP (p, P_grPitch, 7.0f);
                                              setP (p, P_spShift, 200.0f); setP (p, P_spFreeze, 1.0f); setP (p, P_busOrder, 1.0f);
                                              setP (p, P_fbTime, 50.0f); setP (p, P_fbSafety, 0.0f); }, b);
            float md = 0; for (int i = 0; i < b.getNumSamples(); ++i) md = std::max (md, std::abs (b.getSample (0, i) - base.getSample (0, i)));
            CHECK (md == 0.0f, "modules at zero mix must be bit-exact");

            // frequency shifter: 220 Hz moves to 320 Hz / 120 Hz
            run ([] (MegaSynthProcessor& p) { setP (p, P_arShift, 100.0f); setP (p, P_arShiftMix, 1.0f); }, b);
            const double up = peakHz (spectrum (b));
            run ([] (MegaSynthProcessor& p) { setP (p, P_arShift, -100.0f); setP (p, P_arShiftMix, 1.0f); }, b);
            const double down = peakHz (spectrum (b));
            std::cout << "  frequency shift +100 Hz: " << up << " Hz, -100 Hz: " << down << " Hz" << std::endl;
            CHECK (std::abs (up - 320.0) < 3.0 && std::abs (down - 120.0) < 3.0, "frequency shifter");
            {
                const auto sp = spectrum (b);
                CHECK (near (sp, 220.0) < 0.01f * near (sp, 120.0), "frequency shifter leaves the original behind");
            }

            // ring modulation by the internal sine at 1.5x: 110 + 550 Hz, no 220
            run ([] (MegaSynthProcessor& p) { setP (p, P_arRing, 1.0f); setP (p, P_arRatio, 1.5f); }, b);
            {
                const auto sp = spectrum (b);
                std::cout << "  ring: 110 Hz " << near (sp, 110) << ", 220 Hz " << near (sp, 220) << ", 550 Hz " << near (sp, 550) << std::endl;
                CHECK (near (sp, 110) > 50 * near (sp, 220) && near (sp, 550) > 50 * near (sp, 220), "ring modulation sidebands");
            }
            // AM: carrier kept, sidebands added
            run ([] (MegaSynthProcessor& p) { setP (p, P_arAm, 1.0f); setP (p, P_arRatio, 0.25f); }, b);
            {
                const auto sp = spectrum (b);
                CHECK (near (sp, 220) > near (sp, 165) && near (sp, 165) > 0.05f * near (sp, 220) && near (sp, 275) > 0.05f * near (sp, 220), "AM sidebands");
            }
            // FM: Osc 1 frequency-modulated by the sine adds sidebands
            run ([] (MegaSynthProcessor& p) { setP (p, P_arFm, 0.3f); setP (p, P_arRatio, 2.0f); }, b);
            const double cFm = centroid (spectrum (b)), c0 = centroid (sBase);
            std::cout << "  FM centroid " << cFm << " Hz vs " << c0 << " Hz" << std::endl;
            CHECK (cFm > 1.5 * c0, "audio-rate FM");
            // FM by Osc 2 at audio rate, only Osc 1 targeted
            run ([] (MegaSynthProcessor& p) { setP (p, P_arFm, 0.2f); setP (p, P_arMod, 2.0f); setP (p, P_arFmTarget, 1.0f); setP (p, P_osc2Semi, 7.0f); }, b);
            CHECK (centroid (spectrum (b)) > 1.3 * c0, "FM from Osc 2");

            // wave folding adds harmonics; bit crushing too
            run ([] (MegaSynthProcessor& p) { setP (p, P_wmMix, 1.0f); setP (p, P_wmDrive, 0.5f); setP (p, P_wmFold, 0.8f); }, b);
            const double cFold = centroid (spectrum (b));
            run ([] (MegaSynthProcessor& p) { setP (p, P_wmMix, 1.0f); setP (p, P_wmFold, 0.0f); setP (p, P_wmDrive, 0.0f); setP (p, P_wmBits, 3.0f); }, b);
            const double cBits = centroid (spectrum (b));
            std::cout << "  centroid: clean " << c0 << ", folded " << cFold << ", 3-bit " << cBits << std::endl;
            CHECK (cFold > 2.0 * c0 && cBits > 2.0 * c0, "wave mutation adds harmonics");

            // a neutral shaper at 50% mix doesn't comb-filter (the dry path is latency-matched)
            run ([] (MegaSynthProcessor& p) { setP (p, P_wmMix, 0.5f); setP (p, P_wmFold, 0.0f); setP (p, P_wmDrive, 0.0f); }, b);
            {
                double e0 = 0, e1 = 0;
                for (int i = (int) (0.2 * sr); i < (int) (0.8 * sr); ++i) { e0 += base.getSample (0, i) * base.getSample (0, i); e1 += b.getSample (0, i) * b.getSample (0, i); }
                std::cout << "  neutral 50% mix level " << std::sqrt (e1 / e0) << " of dry" << std::endl;
                CHECK (std::abs (std::sqrt (e1 / e0) - 1.0) < 0.05, "dry/wet latency match");
            }
            // ---- Stage 6: DNA Splice
            {
                std::cout << "DNA Splice" << std::endl;
                auto two = [&] (MegaSynthProcessor& p, int waveA, int waveB, float semiB)
                {
                    setP (p, P_osc1Wave, (float) waveA); setP (p, P_osc2Wave, (float) waveB);
                    setP (p, P_osc2Semi, std::fmod (semiB, 12.0f)); setP (p, P_osc2Oct, std::floor (semiB / 12.0f));
                    setP (p, P_osc2Detune, 0.0f); setP (p, P_dnaMix, 1.0f);
                    setP (p, P_dnaA, 0.0f); setP (p, P_dnaB, 1.0f);
                };
                auto rms = [&] (const juce::AudioBuffer<float>& x) { double e = 0; int c = 0; for (int i = (int) (0.2 * sr); i < (int) (0.8 * sr); ++i, ++c) e += x.getSample (0, i) * x.getSample (0, i); return std::sqrt (e / c); };

                // crossover with A = B, right at the crossover frequency: flat (Linkwitz-Riley sums to an all-pass)
                for (float ch : { 0.0f, 1.0f })
                {
                    run ([&] (MegaSynthProcessor& p) { two (p, 3, 3, 0); setP (p, P_dnaB, 0.0f); setP (p, P_dnaMode, 1.0f); setP (p, P_dnaAmount, 0.0f); setP (p, P_dnaChar, ch); }, b);
                    juce::AudioBuffer<float> ref;
                    run ([&] (MegaSynthProcessor& p) { setP (p, P_osc1Gain, 1.0f); }, ref);
                    std::cout << "  crossover A=B (" << (ch < 0.5f ? "12" : "24") << " dB) level " << rms (b) / rms (ref) << " of the source" << std::endl;
                    CHECK (std::abs (rms (b) / rms (ref) - 1.0) < 0.05, "crossover balance");
                }
                // crossover at ~440 Hz: 220 Hz from A and 880 Hz from B pass; swapped, both are filtered out
                run ([&] (MegaSynthProcessor& p) { two (p, 3, 3, 24); setP (p, P_dnaMode, 1.0f); setP (p, P_dnaAmount, 1.0f / 7.0f); setP (p, P_dnaChar, 1.0f); }, b);
                const double passed = rms (b);
                run ([&] (MegaSynthProcessor& p) { two (p, 3, 3, 24); setP (p, P_dnaA, 1.0f); setP (p, P_dnaB, 0.0f); setP (p, P_dnaMode, 1.0f); setP (p, P_dnaAmount, 1.0f / 7.0f); setP (p, P_dnaChar, 1.0f); }, b);
                std::cout << "  crossover 220|880 Hz passed " << passed << ", swapped " << rms (b) << std::endl;
                CHECK (rms (b) < 0.35 * passed, "crossover separation");

                // harmonic: every 2nd harmonic of a saw replaced by B's (a sine has none) -> odd harmonics only
                run ([&] (MegaSynthProcessor& p) { two (p, 0, 3, 0); setP (p, P_dnaMode, 2.0f); setP (p, P_dnaAmount, 1.0f); setP (p, P_dnaChar, 0.0f); }, b);
                {
                    const auto sp = spectrum (b);
                    std::cout << "  harmonic splice: 2nd " << near (sp, 440) << " vs 3rd " << near (sp, 660) << std::endl;
                    CHECK (near (sp, 440) < 0.02f * near (sp, 660) && near (sp, 220) > near (sp, 660), "harmonic splice removes even harmonics");
                }

                // transient / body: A (220 Hz) for ~55 ms, then B (880 Hz)
                run ([&] (MegaSynthProcessor& p) { two (p, 3, 3, 24); setP (p, P_dnaMode, 4.0f); setP (p, P_dnaAmount, 0.6f); setP (p, P_dnaChar, 0.0f); setP (p, P_ampA, 0.001f); }, b);
                const double early = freqOf (b, 0.005, 0.045), late = freqOf (b, 0.3, 0.8);
                std::cout << "  transient/body: " << early << " Hz then " << late << " Hz" << std::endl;
                CHECK (std::abs (early - 220) < 40 && std::abs (late - 880) < 5, "transient / body");

                // morph ends
                run ([&] (MegaSynthProcessor& p) { two (p, 3, 3, 24); setP (p, P_dnaMode, 6.0f); setP (p, P_dnaAmount, 0.0f); setP (p, P_dnaChar, 0.0f); }, b);
                const double m0 = freqOf (b, 0.2, 0.8);
                run ([&] (MegaSynthProcessor& p) { two (p, 3, 3, 24); setP (p, P_dnaMode, 6.0f); setP (p, P_dnaAmount, 1.0f); setP (p, P_dnaChar, 0.0f); }, b);
                const double m1 = freqOf (b, 0.2, 0.8);
                CHECK (std::abs (m0 - 220) < 3 && std::abs (m1 - 880) < 5, "morph ends");

                // every mode: finite, audible, and switching modes mid-note doesn't click
                double worstJump = 0, normalJump = 0;
                for (int mode = 0; mode < 7; ++mode)
                {
                    auto p = clean(); two (*p, 0, 1, 7); setP (*p, P_dnaMode, (float) mode); setP (*p, P_dnaAmount, 0.6f);
                    juce::AudioBuffer<float> x1, x2;
                    auto st = render (*p, 0.4, { { 0.0, juce::MidiMessage::noteOn (1, 57, 1.0f) } }, &x1);
                    CHECK (st.finite && st.peak > 0.01f && st.peak < 4.0f, "DNA mode " + juce::String (mode) + " output");
                    for (int i = (int) (0.2 * sr); i < x1.getNumSamples(); ++i) normalJump = std::max (normalJump, (double) std::abs (x1.getSample (0, i) - x1.getSample (0, i - 1)));
                    setP (*p, P_dnaMode, (float) ((mode + 3) % 7));
                    render (*p, 0.1, {}, &x2);
                    for (int i = 1; i < x2.getNumSamples(); ++i) worstJump = std::max (worstJump, (double) std::abs (x2.getSample (0, i) - x2.getSample (0, i - 1)));
                }
                std::cout << "  mode switch: largest step " << worstJump << " (steady playing: " << normalJump << ")" << std::endl;
                CHECK (worstJump < 1.5 * normalJump, "mode switch should crossfade");
            }

            // ---- Stage 7: Resonator
            {
                std::cout << "Resonator" << std::endl;
                // a short noise burst (Osc 1 ring-modulated by noise) strikes the bank
                auto strike = [&] (MegaSynthProcessor& p, int tuning, float inharm, float pitch)
                {
                    setP (p, P_arRing, 1.0f); setP (p, P_arMod, 5.0f);
                    setP (p, P_ampA, 0.001f); setP (p, P_ampD, 0.02f); setP (p, P_ampS, 0.0f); setP (p, P_ampR, 0.02f);
                    setP (p, P_resMix, 1.0f); setP (p, P_resTuning, (float) tuning); setP (p, P_resModes, 16.0f);
                    setP (p, P_resDecay, 3.0f); setP (p, P_resDamping, 0.0f); setP (p, P_resInharm, inharm); setP (p, P_resPitch, pitch);
                    setP (p, P_resSpread, 0.0f);
                };
                auto peakIn = [&] (const Spec& sp, double lo, double hi)
                {
                    size_t best = (size_t) (lo / sp.binHz); double sum = 0; int cnt = 0;
                    for (size_t i = (size_t) (lo / sp.binHz); i <= (size_t) (hi / sp.binHz) && i < sp.mag.size(); ++i) { sum += sp.mag[i]; ++cnt; if (sp.mag[i] > sp.mag[best]) best = i; }
                    return std::make_pair (best * sp.binHz, sp.mag[best] / std::max (1e-9, sum / cnt));
                };
                const char* names[] = { "harmonic", "odd", "bar", "membrane", "plate", "bell" };
                for (int tuning = 0; tuning < RT_COUNT; ++tuning)
                {
                    auto p = clean(); strike (*p, tuning, 0.0f, 0.0f);
                    juce::AudioBuffer<float> x;
                    render (*p, 1.0, note (57, 1.0f), &x);
                    const auto sp = spectrum (x);
                    juce::String line = "  " + juce::String (names[tuning]) + ":";
                    for (int k = 0; k < 5; ++k)
                    {
                        const double f = 220.0 * resonatorRatio (tuning, k, 0.0f);
                        // neighbouring modes can sit close together (membrane, bell): search a narrow window
                        const double win = std::max (0.015 * f, 8.0 * sp.binHz);
                        const auto pk = peakIn (sp, f - win, f + win);
                        line << " " << juce::String (pk.first, 1) << "/" << juce::String (f, 1) << " (x" << juce::String (pk.second, 0) << ")";
                        CHECK (std::abs (pk.first - f) < 0.004 * f + 2.0 * sp.binHz && pk.second > 2.0,
                               juce::String ("resonator mode ") + names[tuning] + " " + juce::String (k + 1));
                    }
                    std::cout << line << " Hz" << std::endl;
                }
                // stiffness stretches the upper modes; Pitch transposes the bank
                {
                    auto p = clean(); strike (*p, RT_Harmonic, 1.0f, 0.0f);
                    juce::AudioBuffer<float> x; render (*p, 1.0, note (57, 1.0f), &x);
                    const double f8 = 220.0 * 8.0 * std::sqrt (1.0 + 0.002 * 64.0);
                    const auto pk = peakIn (spectrum (x), f8 * 0.98, f8 * 1.02);
                    std::cout << "  inharmonic 8th mode " << pk.first << " Hz (expected " << f8 << ", harmonic would be 1760)" << std::endl;
                    CHECK (std::abs (pk.first - f8) < 4.0, "inharmonicity");
                    auto q = clean(); strike (*q, RT_Harmonic, 0.0f, 12.0f); setP (*q, P_resModes, 1.0f);
                    render (*q, 1.0, note (57, 1.0f), &x);
                    CHECK (std::abs (peakHz (spectrum (x)) - 440.0) < 3.0, "resonator pitch offset");
                }
                // it rings after the note, then the voice frees itself
                {
                    auto p = clean(); strike (*p, RT_Bar, 0.0f, 0.0f); setP (*p, P_resDecay, 1.0f);
                    juce::AudioBuffer<float> x;
                    render (*p, 1.0, note (57, 1.0f), &x);
                    const float ring = x.getMagnitude (0, (int) (0.4 * sr), (int) (0.1 * sr));
                    const float strikePk = x.getMagnitude (0, 0, (int) (0.05 * sr));
                    render (*p, 4.0, {}, &x);
                    std::cout << "  strike peak " << strikePk << ", ringing at 0.4 s: " << ring << ", voices after 5 s: " << p->getEngine().activeVoiceCount() << std::endl;
                    CHECK (strikePk > 0.05f && ring > 0.03f * strikePk && p->getEngine().activeVoiceCount() == 0, "resonator tail and voice release");
                }
                // maximum feedback and decay stays bounded and still ends
                {
                    auto p = clean(); strike (*p, RT_Plate, 1.0f, 0.0f); setP (*p, P_resFeedback, 1.0f); setP (*p, P_resDecay, 10.0f);
                    setP (*p, P_ampS, 1.0f);
                    juce::AudioBuffer<float> x;
                    auto st = render (*p, 2.0, chord (0.0, 1.0, { 45, 52, 57, 64 }), &x);
                    CHECK (st.finite && st.peak < 4.0f, "resonator feedback bounded");
                    render (*p, 33.0, {});
                    CHECK (p->getEngine().activeVoiceCount() == 0, "self-sustaining resonator still ends");
                }
            }

            // ---- Stage 8: Granular (bus)
            {
                std::cout << "Granular" << std::endl;
                auto rmsOf = [&] (const juce::AudioBuffer<float>& x, double t0, double t1) { double e = 0; int c = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr) && i < x.getNumSamples(); ++i, ++c) e += x.getSample (0, i) * x.getSample (0, i); return std::sqrt (e / std::max (1, c)); };
                // grains an octave up
                run ([] (MegaSynthProcessor& p) { setP (p, P_grMix, 1.0f); setP (p, P_grPitch, 12.0f); setP (p, P_grDensity, 10.0f); setP (p, P_grSize, 250.0f); setP (p, P_grJitter, 0.0f); }, b);
                const double gp = peakHz (spectrum (b));
                std::cout << "  grains +12 st: " << gp << " Hz" << std::endl;
                CHECK (std::abs (gp - 440.0) < 4.0, "grain pitch");
                // level stays close to the dry level at moderate settings
                run ([] (MegaSynthProcessor& p) { setP (p, P_grMix, 1.0f); }, b);
                const double ratio = rmsOf (b, 0.3, 0.8) / rmsOf (base, 0.3, 0.8);
                std::cout << "  granular level " << ratio << " of dry" << std::endl;
                CHECK (ratio > 0.4 && ratio < 2.5, "granular level");
                // freeze: the sound keeps going after the note has gone
                {
                    auto p = clean(); setP (*p, P_ampR, 0.05f); setP (*p, P_grMix, 1.0f); setP (*p, P_grPosition, 0.1f); setP (*p, P_grJitter, 0.05f);
                    juce::AudioBuffer<float> x;
                    render (*p, 0.5, { { 0.0, juce::MidiMessage::noteOn (1, 57, 1.0f) } }, &x);
                    setP (*p, P_grFreeze, 1.0f);
                    render (*p, 2.0, { { 0.0, juce::MidiMessage::noteOff (1, 57) } }, &x);
                    const double held = rmsOf (x, 1.4, 1.9);
                    std::cout << "  frozen 1.5 s after note-off: rms " << held << ", pitch " << peakHz (spectrum (x)) << " Hz" << std::endl;
                    CHECK (held > 0.01 && std::abs (peakHz (spectrum (x)) - 220.0) < 4.0 && p->getEngine().granular().activeGrains() > 0, "granular freeze");
                    setP (*p, P_grFreeze, 0.0f);
                    render (*p, 4.5, {}, &x);
                    CHECK (rmsOf (x, 4.0, 4.4) < 1e-4, "unfrozen buffer empties");
                }
                // maximum density and size, every random option, feedback, and LFOs sweeping position and pitch
                {
                    auto p = make();
                    setP (*p, P_grMix, 1.0f); setP (*p, P_grDensity, 200.0f); setP (*p, P_grSize, 500.0f); setP (*p, P_grJitter, 1.0f);
                    setP (*p, P_grPitchRand, 1.0f); setP (*p, P_grReverse, 0.5f); setP (*p, P_grSpread, 1.0f); setP (*p, P_grFeedback, 0.95f);
                    route (*p, 0, MS_Lfo1, P_grPosition, 0.5f); route (*p, 1, MS_Lfo2, P_grPitch, 0.3f); route (*p, 2, MS_RandStep, P_grSize, 0.4f);
                    double cpu = 0;
                    auto st = render (*p, 6.0, chord (0.0, 4.0, { 48, 55, 60, 64 }), nullptr, &cpu);
                    std::cout << "  max density + feedback + modulation: peak " << st.peak << ", " << cpu / 6.0 * 100.0 << "% of one core" << std::endl;
                    CHECK (st.finite && st.peak < 4.0f, "granular at maximum");
                }
            }

            // ---- Stage 9: Spectral (bus)
            {
                std::cout << "Spectral" << std::endl;
                // switched on but neutral: exactly the dry sound, one frame late; the latency is reported
                for (int size = 0; size < 4; ++size)
                {
                    auto p = clean(); setP (*p, P_ampR, 0.05f); setP (*p, P_osc1Wave, 0.0f); setP (*p, P_spOn, 1.0f); setP (*p, P_spSize, (float) size);
                    p->updateLatency();
                    juce::AudioBuffer<float> x, ref;
                    render (*p, 1.0, note (57, 1.0f), &x);
                    auto q = clean(); setP (*q, P_ampR, 0.05f); setP (*q, P_osc1Wave, 0.0f);
                    render (*q, 1.0, note (57, 1.0f), &ref);
                    const int N = spectralSize (size);
                    float md = 0, pk = 0;
                    for (int i = N + (int) (0.1 * sr); i < x.getNumSamples(); ++i) { md = std::max (md, std::abs (x.getSample (0, i) - ref.getSample (0, i - N))); pk = std::max (pk, std::abs (ref.getSample (0, i - N))); }
                    std::cout << "  FFT " << N << ": latency reported " << p->getLatencySamples() << ", difference from delayed dry " << md / pk << std::endl;
                    CHECK (p->getLatencySamples() == N && md < 1.0e-3f * pk, "spectral transparency / latency");
                }
                auto spec = [&] (std::function<void (MegaSynthProcessor&)> f, juce::AudioBuffer<float>& out)
                {
                    run ([&] (MegaSynthProcessor& p) { setP (p, P_spOn, 1.0f); setP (p, P_spSize, 3.0f); f (p); }, out);
                };
                spec ([] (MegaSynthProcessor& p) { setP (p, P_spShift, 100.0f); }, b);
                const double sh = peakHz (spectrum (b));
                std::cout << "  spectral shift +100 Hz: " << sh << " Hz" << std::endl;
                CHECK (std::abs (sh - 320.0) < 4.0, "spectral shift");
                // tilt and formant move the brightness of a saw (its pitch stays)
                spec ([] (MegaSynthProcessor& p) { setP (p, P_osc1Wave, 0.0f); }, b);
                const double cs = centroid (spectrum (b));
                spec ([] (MegaSynthProcessor& p) { setP (p, P_osc1Wave, 0.0f); setP (p, P_spTilt, 1.0f); }, b);
                const double ct = centroid (spectrum (b));
                spec ([] (MegaSynthProcessor& p) { setP (p, P_osc1Wave, 0.0f); setP (p, P_spFormant, 12.0f); }, b);
                const double cf = centroid (spectrum (b));
                const double fp = peakHz (spectrum (b));
                spec ([] (MegaSynthProcessor& p) { setP (p, P_osc1Wave, 0.0f); setP (p, P_spFormant, -12.0f); }, b);
                const double cfd = centroid (spectrum (b));
                std::cout << "  centroid: saw " << cs << ", tilt +1 " << ct << ", formant +12 " << cf << ", formant -12 " << cfd << " (pitch " << fp << " Hz)" << std::endl;
                CHECK (ct > 1.3 * cs && cf > 1.1 * cs && cf > 1.3 * cfd, "tilt / formant");
                // freeze holds the sound after the note; with blur and maximum feedback it stays bounded
                {
                    auto p = clean(); setP (*p, P_ampR, 0.05f); setP (*p, P_spOn, 1.0f); setP (*p, P_spSize, 3.0f);
                    juce::AudioBuffer<float> x;
                    render (*p, 0.6, { { 0.0, juce::MidiMessage::noteOn (1, 57, 1.0f) } }, &x);
                    setP (*p, P_spFreeze, 1.0f);
                    render (*p, 2.0, { { 0.0, juce::MidiMessage::noteOff (1, 57) } }, &x);
                    double e = 0; for (int i = (int) (1.4 * sr); i < (int) (1.9 * sr); ++i) e += x.getSample (0, i) * x.getSample (0, i);
                    const double frozen = std::sqrt (e / (0.5 * sr));
                    std::cout << "  frozen spectrum 1.5 s after note-off: rms " << frozen << ", pitch " << peakHz (spectrum (x)) << " Hz" << std::endl;
                    CHECK (frozen > 0.01 && std::abs (peakHz (spectrum (x)) - 220.0) < 4.0, "spectral freeze");
                    setP (*p, P_spFeedback, 0.9f); setP (*p, P_spBlur, 1.0f); setP (*p, P_spScramble, 1.0f); setP (*p, P_spMorph, 1.0f); setP (*p, P_spTilt, 1.0f);
                    auto st = render (*p, 3.0, chord (0.0, 2.0, { 45, 57, 64, 72 }), &x);
                    setP (*p, P_spFreeze, 0.0f);
                    auto st2 = render (*p, 3.0, chord (0.0, 2.0, { 40, 52, 76 }), &x);
                    std::cout << "  freeze + feedback + everything: peak " << st.peak << " / " << st2.peak << std::endl;
                    CHECK (st.finite && st2.finite && st.peak < 6.0f && st2.peak < 6.0f, "spectral feedback bounded");
                }
                // CPU of the largest frame
                {
                    auto p = make(); setP (*p, P_spOn, 1.0f); setP (*p, P_spSize, 3.0f); setP (*p, P_spShift, 50.0f); setP (*p, P_spFormant, 3.0f);
                    double cpu = 0; render (*p, 5.0, chord (0.0, 4.0, { 48, 55, 60 }), nullptr, &cpu);
                    std::cout << "  spectral (4096, shift + formant) with 3 notes: " << cpu / 5.0 * 100.0 << "% of one core" << std::endl;
                }
            }

            // ---- Stage 10: Feedback Matrix
            {
                std::cout << "Feedback matrix" << std::endl;
                auto win = [&] (const juce::AudioBuffer<float>& x, double t0, double t1) { return x.getMagnitude (0, (int) (t0 * sr), (int) ((t1 - t0) * sr)); };
                // a 30 ms blip, fed from the output back into the granular input 300 ms later: an echo
                {
                    auto blip = [&] (float amt, juce::AudioBuffer<float>& x)
                    {
                        auto p = clean(); setP (*p, P_ampA, 0.001f); setP (*p, P_ampD, 0.03f); setP (*p, P_ampS, 0.0f); setP (*p, P_ampR, 0.01f);
                        setP (*p, P_fbOutGr, amt); setP (*p, P_fbTime, 300.0f); setP (*p, P_fbTone, 20000.0f); setP (*p, P_fbSafety, 0.0f);
                        render (*p, 1.2, { { 0.0, juce::MidiMessage::noteOn (1, 57, 1.0f) }, { 0.05, juce::MidiMessage::noteOff (1, 57) } }, &x);
                    };
                    juce::AudioBuffer<float> off, onB;
                    blip (0.0f, off); blip (1.0f, onB);
                    const float e1 = win (onB, 0.30, 0.34), e2 = win (onB, 0.60, 0.64), d = win (off, 0.30, 0.34), gap = win (onB, 0.15, 0.28);
                    std::cout << "  output -> granular echo: 300 ms " << e1 << ", 600 ms " << e2 << ", without feedback " << d << ", in between " << gap << std::endl;
                    CHECK (e1 > 0.005f && e2 > 0.0005f && e2 < e1 && d < 1e-6f && gap < 0.1f * e1, "feedback path timing");
                }
                // every path at full, no safety, every mutation and every internal feedback at maximum:
                // the output never passes the 0 dBFS ceiling and never goes NaN
                {
                    auto p = make();
                    for (int i = P_fbGrGr; i <= P_fbOutDl; ++i) setP (*p, i, 1.0f);
                    setP (*p, P_fbSafety, 0.0f); setP (*p, P_fbTime, 10.0f); setP (*p, P_fbTone, 20000.0f); setP (*p, P_masterVolume, 1.0f);
                    for (int i : { P_wmMix, P_wmDrive, P_wmFold, P_arRing, P_arAm, P_dnaMix, P_resMix, P_resFeedback, P_grMix, P_grFeedback, P_spFeedback, P_spBlur, P_spTilt })
                        setP (*p, i, p->param (i)->convertFrom0to1 (1.0f));
                    setP (*p, P_spOn, 1.0f); setP (*p, P_spSize, 0.0f); setP (*p, P_delayFeedback, 0.9f); setP (*p, P_delayMix, 1.0f);
                    setP (*p, P_grDensity, 200.0f); setP (*p, P_resDecay, 10.0f);
                    auto st = render (*p, 10.0, chord (0.0, 6.0, { 36, 48, 55, 60, 67, 72 }));
                    std::cout << "  everything at maximum for 10 s: peak " << st.peak << " (ceiling 1.0), rms " << st.rms << std::endl;
                    CHECK (st.finite && st.peak <= 1.0f, "feedback safety ceiling");
                }
                // Feedback Safety lowers how hard the loops can drive (default patch, every path at full)
                {
                    double r[2];
                    for (int k = 0; k < 2; ++k)
                    {
                        auto p = make();
                        for (int i = P_fbGrGr; i <= P_fbOutDl; ++i) setP (*p, i, 1.0f);
                        setP (*p, P_fbSafety, k == 0 ? 0.0f : 1.0f);
                        r[k] = render (*p, 4.0, chord (0.0, 3.0, { 48, 55, 60 })).rms;
                    }
                    std::cout << "  all paths at full: rms " << r[0] << " at safety 0%, " << r[1] << " at 100%" << std::endl;
                    CHECK (r[1] < 0.7 * r[0], "feedback safety");
                }
            }

            // ---- Stage 11: Capture / Resample
            {
                std::cout << "Capture" << std::endl;
                auto p = clean(); setP (*p, P_ampR, 0.05f);
                auto capture = [&] (MegaSynthProcessor& q, double secs, int noteNum)
                {
                    q.startCapture();
                    render (q, secs, { { 0.0, juce::MidiMessage::noteOn (1, noteNum, 1.0f) }, { secs - 0.1, juce::MidiMessage::noteOff (1, noteNum) } });
                    q.stopCapture();
                    render (q, 0.02, {});
                    q.pollCapture();
                };
                capture (*p, 1.0, 57);
                const double len1 = p->getRawCapture().getNumSamples() / sr;
                const double hz1 = p->detectCapturePitch();
                std::cout << "  captured " << len1 << " s, detected " << hz1 << " Hz" << std::endl;
                CHECK (p->hasCapture() && std::abs (len1 - 1.0) < 0.03 && std::abs (hz1 - 220.0) < 1.0, "capture and pitch detection");
                // edit tools
                p->captureSettings.trimStart = 0.2f; p->captureSettings.trimEnd = 0.6f; p->captureSettings.reverse = true;
                const double elen = p->getEditedCapture().getNumSamples() / sr;
                CHECK (std::abs (elen - 0.4 * len1) < 0.03 && std::abs (p->getEditedCapture().getMagnitude (0, p->getEditedCapture().getNumSamples()) - 0.89f) < 0.01f, "trim / normalise");
                p->captureSettings = {};

                // send to WT 1 (tuned to the detected pitch): the sample plays back in tune
                CHECK (p->sendCapture (MegaSynthProcessor::CT_WT1).isEmpty(), "send to WT 1");
                setP (*p, P_osc1Gain, 0.0f); setP (*p, P_osc4Gain, 0.8f); setP (*p, P_osc4LoopMode, 0.0f);
                juce::AudioBuffer<float> x;
                render (*p, 1.0, note (64, 1.0f), &x);
                const double fwt = peakHz (spectrum (x));
                std::cout << "  played back from WT 1 at E4: " << fwt << " Hz (expected 329.6)" << std::endl;
                CHECK (std::abs (fwt - 329.6) < 4.0, "captured sample in tune");

                // mutate it and capture again: capture -> mutate -> capture
                setP (*p, P_wmMix, 1.0f); setP (*p, P_wmFold, 0.7f); setP (*p, P_wmDrive, 0.5f);
                capture (*p, 0.8, 57);
                const double hz2 = p->detectCapturePitch();
                std::cout << "  re-captured through Wave Mutation: " << p->getRawCapture().getNumSamples() / sr << " s, " << hz2 << " Hz" << std::endl;
                CHECK (p->hasCapture() && std::abs (hz2 - 220.0) < 2.0, "capture loop");
                CHECK (p->sendCapture (MegaSynthProcessor::CT_WT2).isEmpty(), "send to WT 2");
                // ...and it all survives saving and reloading
                juce::MemoryBlock st; p->getStateInformation (st);
                auto q = make(); q->setStateInformation (st.getData(), (int) st.getSize());
                CHECK (q->hasCapture() && q->getSampleStatus (0).contains ("Capture") && q->getSampleStatus (1).contains ("Capture")
                       && std::abs (q->detectCapturePitch() - 220.0) < 2.0, "capture state round trip");
                const auto json = p->exportBrowserPatch();
                auto r = make(); r->importBrowserPatch (json);
                CHECK (r->hasCapture() && r->getSampleStatus (1).contains ("Capture"), "capture in patch");

                // single cycle -> WT 2, played at A4 = 440 Hz
                {
                    auto c = clean(); setP (*c, P_ampR, 0.05f); setP (*c, P_osc1Wave, 0.0f);
                    capture (*c, 0.6, 57);
                    CHECK (c->sendCapture (MegaSynthProcessor::CT_CycleWT2).isEmpty(), "single cycle");
                    setP (*c, P_osc1Gain, 0.0f); setP (*c, P_wt2Gain, 0.8f);
                    render (*c, 1.0, note (69, 1.0f), &x);
                    const double fc = peakHz (spectrum (x));
                    std::cout << "  single cycle from a saw, played at A4: " << fc << " Hz" << std::endl;
                    CHECK (std::abs (fc - 440.0) < 4.0, "single cycle pitch");
                    // DNA source B = the captured sample
                    CHECK (c->sendCapture (MegaSynthProcessor::CT_DnaB).isEmpty() && (int) c->param (P_dnaB)->convertFrom0to1 (c->param (P_dnaB)->getValue()) == 5, "send to DNA B");
                }
                // granular: the capture becomes the (frozen) grain buffer, playing with no notes held
                {
                    CHECK (p->sendCapture (MegaSynthProcessor::CT_Granular).isEmpty(), "send to granular");
                    render (*p, 1.0, {}, &x);
                    const double g = x.getMagnitude (0, (int) (0.5 * sr), (int) (0.4 * sr));
                    std::cout << "  granular playing the capture with no notes: peak " << g << std::endl;
                    CHECK (g > 0.01, "capture into granular");
                }
                // before-effects capture point
                {
                    auto c = clean(); setP (*c, P_capPoint, 0.0f); setP (*c, P_ampR, 0.05f);
                    capture (*c, 0.5, 57);
                    CHECK (c->hasCapture() && std::abs (c->detectCapturePitch() - 220.0) < 1.0, "capture before effects");
                }
            }

            // ---- Stage 12: Cell Instability
            {
                std::cout << "Cell Instability" << std::endl;
                // the noise bank itself: inside -1..1, continuous, with a continuous slope
                {
                    Instability in; in.seed (12345);
                    const double dt = 16.0 / 48000.0;
                    float prev = in.value (0), prevD = 0, maxD = 0, maxJerk = 0, lo = 1, hi = -1;
                    for (int i = 0; i < 200000; ++i)
                    {
                        in.advance (dt, 8.0);
                        const float v = in.value (0), d = v - prev;
                        maxD = std::max (maxD, std::abs (d)); if (i > 0) maxJerk = std::max (maxJerk, std::abs (d - prevD));
                        lo = std::min (lo, v); hi = std::max (hi, v); prev = v; prevD = d;
                    }
                    std::cout << "  noise: range " << lo << " .. " << hi << ", largest step " << maxD << ", largest slope change " << maxJerk << std::endl;
                    // at 8 points/s (x1.3) and 16-sample blocks, a smooth curve moves < pi * 1.3 * 8 * dt per block
                    CHECK (lo >= -1.0f && hi <= 1.0f && maxD < 3.3f * 1.3f * 8.0f * (float) dt && maxJerk < 0.002f, "instability smoothness");
                }
                // pitch instability on a held note: wanders within +-50 cents, smoothly, and differs per note
                {
                    auto p = clean(); setP (*p, P_ciAmount, 1.0f); setP (*p, P_ciRate, 2.0f);
                    for (int i : { P_ciCutoff, P_ciRes, P_ciLevel, P_ciFold, P_ciScan, P_ciFm, P_ciDna, P_ciEnv, P_ciReso }) setP (*p, i, 0.0f);
                    setP (*p, P_ciPitch, 1.0f);
                    juce::AudioBuffer<float> x;
                    render (*p, 6.0, { { 0.0, juce::MidiMessage::noteOn (1, 69, 1.0f) }, { 5.9, juce::MidiMessage::noteOff (1, 69) } }, &x);
                    // period-by-period frequency from zero crossings (interpolated)
                    std::vector<double> cents;
                    double last = -1;
                    for (int i = (int) (0.2 * sr); i < (int) (5.8 * sr); ++i)
                    {
                        const float a0 = x.getSample (0, i - 1), a1 = x.getSample (0, i);
                        if (a0 < 0 && a1 >= 0)
                        {
                            const double tz = i - 1 + a0 / (a0 - a1);
                            if (last > 0) cents.push_back (1200.0 * std::log2 ((sr / (tz - last)) / 440.0));
                            last = tz;
                        }
                    }
                    double lo = 1e9, hi = -1e9, step = 0;
                    for (size_t k = 0; k < cents.size(); ++k) { lo = std::min (lo, cents[k]); hi = std::max (hi, cents[k]); if (k) step = std::max (step, std::abs (cents[k] - cents[k - 1])); }
                    std::cout << "  pitch wanders " << lo << " .. " << hi << " cents, largest change between cycles " << step << " cents" << std::endl;
                    CHECK (lo > -52 && hi < 52 && hi - lo > 15 && step < 1.0, "pitch instability range / smoothness");
                }
                // every target at once, full amount, fast: still finite, and the envelope times vary per note
                {
                    auto p = make(); setP (*p, P_ciAmount, 1.0f); setP (*p, P_ciRate, 10.0f);
                    for (int i : { P_ciPitch, P_ciCutoff, P_ciRes, P_ciLevel, P_ciFold, P_ciScan, P_ciFm, P_ciDna, P_ciEnv, P_ciReso }) setP (*p, i, 1.0f);
                    setP (*p, P_wmMix, 0.5f); setP (*p, P_dnaMix, 0.5f); setP (*p, P_resMix, 0.3f); setP (*p, P_arFm, 0.2f);
                    route (*p, 0, MS_CellNoise, P_reverbMix, 0.3f);
                    auto st = render (*p, 4.0, chord (0.0, 3.0, { 48, 55, 60, 64, 67 }));
                    CHECK (st.finite && st.peak < 4.0f, "instability on every target");
                }
            }

            // ---- Stage 13: Master Mutate
            {
                std::cout << "Master Mutate" << std::endl;
                auto patchSetup = [&] (MegaSynthProcessor& p)
                {
                    setP (p, P_osc1Wave, 0.0f); setP (p, P_osc2Gain, 0.4f); setP (p, P_filterCutoff, 1500.0f); setP (p, P_filterRes, 6.0f);
                    setP (p, P_complexGain, 0.2f); setP (p, P_wmMix, 0.3f);
                };
                auto renderWith = [&] (float amount, int seed, uint32_t locks, juce::AudioBuffer<float>& out)
                {
                    auto p = clean(); patchSetup (*p);
                    setP (*p, P_mutAmount, amount); setP (*p, P_mutSeed, (float) seed);
                    for (int k = 0; k < ML_COUNT; ++k) setP (*p, P_mutLock1 + k, (locks >> k) & 1u ? 1.0f : 0.0f);
                    render (*p, 1.2, chord (0.0, 1.0, { 45, 52, 57 }), &out);
                };
                auto maxDiff = [] (const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
                { float d = 0; for (int i = 0; i < x.getNumSamples(); ++i) d = std::max (d, std::abs (x.getSample (0, i) - y.getSample (0, i))); return d; };
                juce::AudioBuffer<float> orig, m0, a1, a2, other, allLocked;
                renderWith (0.0f, 1, 0, orig);
                renderWith (0.0f, 777, 0, m0);
                renderWith (0.6f, 42, 0, a1);
                renderWith (0.6f, 42, 0, a2);
                renderWith (0.6f, 43, 0, other);
                renderWith (0.9f, 42, (1u << ML_COUNT) - 1, allLocked);
                std::cout << "  0% vs original: " << maxDiff (orig, m0) << ", seed 42 twice: " << maxDiff (a1, a2) << ", seed 42 vs original: " << maxDiff (a1, orig)
                          << ", seed 42 vs 43: " << maxDiff (a1, other) << ", all 13 locked: " << maxDiff (allLocked, orig) << std::endl;
                CHECK (maxDiff (orig, m0) == 0.0f, "mutate 0% must equal the original");
                CHECK (maxDiff (a1, a2) == 0.0f, "same patch + seed + amount must reproduce exactly");
                CHECK (maxDiff (a1, orig) > 0.01f && maxDiff (a1, other) > 0.01f, "mutation changes the sound, per seed");
                CHECK (maxDiff (allLocked, orig) == 0.0f, "locks hold");

                // each lock protects exactly its own parameters
                {
                    MutationTable t; t.build (99);
                    float base[P_COUNT];
                    auto q = clean(); patchSetup (*q);
                    for (int i = 0; i < P_COUNT; ++i) base[i] = q->param (i)->convertFrom0to1 (q->param (i)->getValue());
                    bool ok = true; int moved = 0;
                    for (int k = 0; k < ML_COUNT; ++k)
                    {
                        float v[P_COUNT]; std::copy (base, base + P_COUNT, v);
                        applyMutation (t, 1.0f, 1u << k, v);
                        for (int i = 0; i < P_COUNT; ++i)
                        {
                            if (mutLockOf (i) == k && v[i] != base[i]) ok = false;
                            if (mutLockOf (i) != k && v[i] != base[i]) ++moved;
                        }
                    }
                    CHECK (ok && moved > 0, "each lock holds its group");
                    // never mutated: sequencer, master volume, macros, the Mutate controls themselves
                    float v[P_COUNT]; std::copy (base, base + P_COUNT, v);
                    applyMutation (t, 1.0f, 0, v);
                    for (int i : { (int) P_seqTempo, (int) P_masterVolume, (int) P_macro1, (int) P_mutAmount, (int) P_polyphony, (int) P_osc2Gain + 0 })
                        if (i != P_osc2Gain) CHECK (v[i] == base[i], juce::String ("must not mutate ") + kParamIds[i]);
                    // switched-off things stay off
                    CHECK (v[P_supersawGain] == 0.0f && v[P_resMix] == 0.0f && v[P_fbOutGr] == 0.0f, "zero levels stay zero");
                }
                // correlation inside a group, none across groups (over many seeds)
                {
                    double same = 0, cross = 0, var = 0; int n = 0;
                    for (uint32_t sd = 1; sd < 4001; ++sd)
                    {
                        MutationTable t; t.build (sd);
                        same += t.dir[P_filterCutoff] * t.dir[P_filterDrive];
                        cross += t.dir[P_filterCutoff] * t.dir[P_ampD];
                        var += t.dir[P_filterCutoff] * t.dir[P_filterCutoff];
                        ++n;
                    }
                    std::cout << "  direction correlation: same group " << same / var << ", different groups " << cross / var << std::endl;
                    CHECK (same / var > 0.35 && std::abs (cross / var) < 0.08, "group correlation");
                }
                // shaping: a short attack stays short at moderate amounts; ratios lean to harmonic values
                {
                    int shortOk = 0, ratioNear = 0, total = 0;
                    for (uint32_t sd = 1; sd < 501; ++sd)
                    {
                        MutationTable t; t.build (sd);
                        float v[P_COUNT];
                        for (int i = 0; i < P_COUNT; ++i) v[i] = meta (i).def;
                        v[P_ampA] = 0.003f; v[P_complexRatio] = 2.0f;
                        applyMutation (t, 0.5f, 0, v);
                        shortOk += v[P_ampA] < 0.03f;
                        const float r = v[P_complexRatio];
                        ratioNear += std::abs (r - std::round (r * 2.0f) / 2.0f) < 0.15f;
                        ++total;
                    }
                    std::cout << "  at 50%: short attacks stayed under 30 ms in " << shortOk << "/" << total << ", ratios within 0.15 of a half-harmonic " << ratioNear << "/" << total << std::endl;
                    CHECK (shortOk == total && ratioNear > total * 0.9, "mutation shaping");
                }
                // commit bakes it in: Mutate back to 0, the sound is (to knob resolution) the mutated one, undo restores
                {
                    auto p = clean(); patchSetup (*p);
                    setP (*p, P_mutAmount, 0.6f); setP (*p, P_mutSeed, 42.0f);
                    p->pushHistory ("before");
                    float before[P_COUNT], expect[P_COUNT];
                    for (int i = 0; i < P_COUNT; ++i) before[i] = expect[i] = p->param (i)->convertFrom0to1 (p->param (i)->getValue());
                    { MutationTable t; t.build (42); applyMutation (t, 0.6f, 0, expect); }
                    p->commitMutation();
                    // every knob now holds the mutated value, to its own resolution (e.g. whole cents of detune)
                    int bad = 0, changed = 0;
                    for (int i = 0; i < P_COUNT; ++i)
                    {
                        if (i == P_mutAmount) continue;
                        const float got = p->param (i)->convertFrom0to1 (p->param (i)->getValue());
                        const float step = std::max (meta (i).step, 1.0e-4f * (meta (i).max - meta (i).min));
                        if (std::abs (got - expect[i]) > 0.5f * step + 1.0e-4f * std::max (1.0f, std::abs (expect[i]))) ++bad;
                        if (got != before[i]) ++changed;
                    }
                    std::cout << "  commit: " << changed << " knobs changed, " << bad << " off by more than their resolution" << std::endl;
                    juce::AudioBuffer<float> c;
                    render (*p, 1.2, chord (0.0, 1.0, { 45, 52, 57 }), &c);
                    CHECK (p->param (P_mutAmount)->getValue() == 0.0f && bad == 0 && changed > 20 && c.getMagnitude (0, c.getNumSamples()) > 0.01f
                           && p->getMutationHistory().size() == 1, "commit");
                    p->undo();
                    CHECK (p->param (P_mutAmount)->getValue() > 0.5f, "undo a commit");
                }
                // Mutate is modulatable: velocity -> Mutate makes soft and hard notes different patches
                {
                    auto p = clean(); patchSetup (*p); setP (*p, P_mutSeed, 7.0f);
                    route (*p, 0, MS_Velocity, P_mutAmount, 1.0f);
                    juce::AudioBuffer<float> soft, hard;
                    render (*p, 0.8, note (57, 0.05f), &soft);
                    render (*p, 0.8, note (57, 1.0f), &hard);
                    CHECK (maxDiff (soft, hard) > 0.01f, "velocity-modulated mutation");
                }
            }

            // ---- Stage 14: Genetic Lab
            {
                std::cout << "Genetic Lab" << std::endl;
                auto p = clean();
                p->setPatchName ("Parent A");
                p->labSetParent (0);
                // a very different parent B: every group changed
                setP (*p, P_osc1Wave, 1.0f); setP (*p, P_osc1Detune, 12.0f); setP (*p, P_osc2Gain, 0.6f); setP (*p, P_filterMode, 12.0f);
                setP (*p, P_filterCutoff, 700.0f); setP (*p, P_ampA, 0.3f); setP (*p, P_lfo1Rate, 9.0f); setP (*p, P_delayMix, 0.4f);
                setP (*p, P_osc1Semi, 7.0f); setP (*p, P_osc4Position, 0.5f); setP (*p, P_dnaMix, 0.5f); setP (*p, P_wmMix, 0.6f);
                setP (*p, P_resMix, 0.3f); setP (*p, P_grMix, 0.2f); setP (*p, P_fbGrSp, 0.3f);
                route (*p, 0, MS_Lfo2, P_filterCutoff, 0.4f);
                p->setPatchName ("Parent B");
                p->labSetParent (1);
                const auto* A = p->lab.find (p->lab.parentA);
                const auto* B = p->lab.find (p->lab.parentB);
                CHECK (A != nullptr && B != nullptr && A->routes != B->routes, "parents stored");
                p->labVariation = 0.0f;
                const int brood = p->labBreed();
                CHECK (brood == 0 && (int) p->lab.broods[0].children.size() == 8, "breed 8 children");
                // every child takes each whole group from one parent (and everything outside the groups from A)
                int wholeGroups = 0, violations = 0, fromBoth = 0;
                std::set<uint32_t> masks;
                for (auto& id : p->lab.broods[0].children)
                {
                    const auto* c = p->lab.find (id);
                    A = p->lab.find (p->lab.parentA); B = p->lab.find (p->lab.parentB);
                    bool usesA = false, usesB = false;
                    for (int g = 0; g < ML_COUNT; ++g)
                    {
                        const bool b = (c->fromB >> g) & 1u;
                        for (int i = 0; i < P_COUNT; ++i)
                        {
                            if (geneGroupOf (i) != g) continue;
                            if (c->values[(size_t) i] != (b ? B : A)->values[(size_t) i]) ++violations;
                        }
                        (b ? usesB : usesA) = true;
                        ++wholeGroups;
                    }
                    for (int i = 0; i < P_COUNT; ++i) if (geneGroupOf (i) < 0 && c->values[(size_t) i] != A->values[(size_t) i]) ++violations;
                    if (c->routes != (((c->fromB >> ML_Mod) & 1u) ? B : A)->routes) ++violations;
                    fromBoth += usesA && usesB;
                    masks.insert (c->fromB);
                }
                std::cout << "  8 children x 13 groups: " << violations << " values not from that group's parent, " << fromBoth << "/8 mix both parents, "
                          << masks.size() << " different combinations" << std::endl;
                CHECK (violations == 0 && fromBoth == 8 && masks.size() >= 6, "children take whole groups from a parent");

                // audition a child, make it a parent, breed again: generation 2, and the tree leads back
                const auto child = p->lab.broods[0].children[3];
                p->labAudition (child);
                CHECK (std::abs (p->param (P_filterCutoff)->convertFrom0to1 (p->param (P_filterCutoff)->getValue()) - p->lab.find (child)->values[P_filterCutoff]) < 1.0f, "audition loads the child");
                juce::AudioBuffer<float> x;
                CHECK (render (*p, 1.0, chord (0.0, 0.8, { 48, 55, 60 }), &x).finite, "child plays");
                p->labSetParent (0);
                CHECK (p->lab.parentA == child, "the auditioned child becomes parent A (same id)");
                p->labVariation = 0.1f;
                const int brood2 = p->labBreed();
                const auto* g2 = p->lab.find (p->lab.broods[(size_t) brood2].children[0]);
                const auto line = p->lab.lineage (g2->id, 3);
                std::cout << "  generation " << g2->generation << " lineage: " << line << std::endl;
                CHECK (g2->generation == 2 && line.contains ("Gen 1 / 4") && line.contains ("Parent A") && line.contains ("Parent B"), "ancestry");
                const auto* par = p->lab.find (g2->parentA);
                CHECK (par != nullptr && p->lab.find (par->parentA) != nullptr && p->lab.find (par->parentA)->name == "Parent A", "walk back through generations");
                // the lab survives saving and reloading
                juce::MemoryBlock st; p->getStateInformation (st);
                auto q = make(); q->setStateInformation (st.getData(), (int) st.getSize());
                CHECK (q->lab.archive.size() == p->lab.archive.size() && q->lab.broods.size() == 2 && q->lab.find (g2->id) != nullptr
                       && q->lab.find (g2->id)->values[P_filterCutoff] == g2->values[P_filterCutoff], "lab state round trip");
            }

            // ---- Stage 15: Sonic DNA Sequencer
            {
                std::cout << "DNA Sequencer" << std::endl;
                // a host at 120 BPM, playing from bar 1
                struct HostHead : juce::AudioPlayHead
                {
                    double bpm = 120.0, ppq = 0.0; bool playing = true;
                    juce::Optional<PositionInfo> getPosition() const override
                    {
                        PositionInfo pi; pi.setBpm (bpm); pi.setPpqPosition (ppq); pi.setIsPlaying (playing);
                        pi.setTimeSignature (TimeSignature { 4, 4 });
                        return pi;
                    }
                } head;
                auto p = clean(); setP (*p, P_ampR, 0.05f);   // a sine, so the crush steps' new harmonics stand out
                p->setPlayHead (&head);
                for (int i = 0; i < 16; ++i) p->dnaSteps.set (i, i % 2 == 0 ? DT_Crush : DT_Off, 1.0f);
                p->dnaSteps.set (5, DT_Octave, 1.0f);
                setP (*p, P_dsOn, 1.0f); setP (*p, P_dsSteps, 16.0f); setP (*p, P_dsRate, 2.0f); setP (*p, P_dsGlide, 0.0f);
                // run it like a host: 512-sample blocks, the play position advancing with them
                const int block = 512; const double secs = 4.0;
                juce::AudioBuffer<float> buf (2, block), out (2, (int) (secs * sr));
                int wrongStep = 0, blocks = 0;
                std::vector<int> stepAt;
                for (int pos = 0; pos < out.getNumSamples(); pos += block)
                {
                    head.ppq = pos / sr * (head.bpm / 60.0);
                    juce::MidiBuffer midi;
                    if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
                    buf.clear();
                    p->processBlock (buf, midi);
                    out.copyFrom (0, pos, buf, 0, 0, std::min (block, out.getNumSamples() - pos));
                    const double midPpq = head.ppq + 0.5 * block / sr * (head.bpm / 60.0);
                    const int expect = (int) std::floor (midPpq / 0.25) % 16;
                    wrongStep += p->getDnaSeqStep() != expect;
                    stepAt.push_back (p->getDnaSeqStep());
                    ++blocks;
                }
                std::cout << "  120 BPM, 1/16 steps: " << blocks - wrongStep << "/" << blocks << " blocks on the step the host's beat position says" << std::endl;
                CHECK (wrongStep == 0, "DNA sequencer step timing vs host tempo");
                // audible per step: crush steps are brighter than clean ones; the octave step is an octave up
                auto stepCentroid = [&] (int stepIndex, int bar)
                {
                    const double t0 = (bar * 16 + stepIndex) * 0.125 + 0.03, t1 = t0 + 0.08;   // 125 ms steps
                    const int a0 = (int) (t0 * sr), n = (int) ((t1 - t0) * sr);
                    juce::dsp::FFT fft (12); std::vector<float> d (8192, 0.0f);
                    for (int i = 0; i < 4096; ++i) d[(size_t) i] = i < n ? out.getSample (0, a0 + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / n)) : 0.0f;
                    fft.performFrequencyOnlyForwardTransform (d.data());
                    double num = 0, den = 0; for (int k = 1; k < 2048; ++k) { num += d[(size_t) k] * k * sr / 4096; den += d[(size_t) k]; }
                    return num / den;
                };
                const double cCrush = stepCentroid (2, 1), cClean = stepCentroid (3, 1), cOct = stepCentroid (5, 1);
                std::cout << "  centroid: crush step " << cCrush << " Hz, clean step " << cClean << " Hz, octave step " << cOct << " Hz" << std::endl;
                CHECK (cCrush > 1.5 * cClean, "crush step audible");
                {
                    // pitch of the octave step from zero crossings
                    int zc = 0; const int a0 = (int) ((16 + 5) * 0.125 * sr + 0.02 * sr), a1 = a0 + (int) (0.09 * sr);
                    for (int i = a0 + 1; i < a1; ++i) if (out.getSample (0, i - 1) < 0 && out.getSample (0, i) >= 0) ++zc;
                    const double f = zc / ((a1 - a0) / sr);
                    std::cout << "  octave step pitch " << f << " Hz (expected ~440)" << std::endl;
                    CHECK (std::abs (f - 440.0) < 25.0, "octave step");
                }
                // every transform moves its own destinations
                {
                    int moved = 0;
                    for (int t = 1; t < DT_COUNT; ++t)
                    {
                        float v[P_COUNT]; for (int i = 0; i < P_COUNT; ++i) v[i] = meta (i).def;
                        float w[DT_COUNT] = {}; w[t] = 1.0f;
                        float before[P_COUNT]; std::copy (v, v + P_COUNT, before);
                        applyDnaSeq (w, 1.0f, v);
                        bool all = true;
                        for (auto& d : dnaTransformTargets (t)) all = all && v[d.param] != before[d.param];
                        moved += all;
                    }
                    CHECK (moved == DT_COUNT - 1, "every transform moves its destinations");
                }
                // as a Mod Matrix source, free-running: route it to Osc 1 pitch and hear steps
                {
                    auto q = clean(); setP (*q, P_ampR, 0.05f);
                    for (int i = 0; i < 4; ++i) q->dnaSteps.set (i, DT_Off, i % 2 == 0 ? 0.0f : 1.0f);
                    for (int i = 0; i < 4; ++i) q->dnaSteps.set (i, DT_Ring, i % 2 == 0 ? 0.0f : 1.0f);
                    setP (*q, P_dsOn, 1.0f); setP (*q, P_dsSteps, 4.0f); setP (*q, P_dsRate, 8.0f); setP (*q, P_dsFreeHz, 4.0f); setP (*q, P_dsDepth, 0.0f);
                    route (*q, 0, MS_DnaSeq, P_osc1Semi, 0.5f);
                    juce::AudioBuffer<float> x;
                    render (*q, 2.0, { { 0.0, juce::MidiMessage::noteOn (1, 57, 1.0f) } }, &x);
                    auto fAt = [&] (double t0) { int zc = 0; const int a0 = (int) (t0 * sr), a1 = a0 + (int) (0.15 * sr);
                                                  for (int i = a0 + 1; i < a1; ++i) if (x.getSample (0, i - 1) < 0 && x.getSample (0, i) >= 0) ++zc; return zc / 0.15; };
                    const double f0 = fAt (1.02), f1 = fAt (1.27);
                    std::cout << "  as a source (free 4 Hz): " << f0 << " Hz then " << f1 << " Hz" << std::endl;
                    CHECK (std::abs (f0 - 220.0) < 10.0 && std::abs (f1 - 440.0) < 15.0, "DNA sequencer as a modulation source");
                }
                // steps survive save / load
                juce::MemoryBlock st; p->getStateInformation (st);
                auto r = make(); r->setStateInformation (st.getData(), (int) st.getSize());
                CHECK (r->dnaSteps.getType (5) == DT_Octave && r->dnaSteps.getType (2) == DT_Crush, "DNA sequencer state");
            }

            // everything at maximum stays finite and bounded
            {
                auto p = make();
                for (int i : { P_wmMix, P_wmDrive, P_wmFold, P_wmShape, P_wmBend, P_wmAsym, P_wmRect, P_arFm, P_arAm, P_arRing, P_arShiftMix, P_dnaMix, P_dnaAmount, P_resMix, P_resFeedback, P_resInharm, P_grMix, P_grJitter, P_grPitchRand, P_grReverse }) setP (*p, i, 1.0f);
                setP (*p, P_grFeedback, 0.95f); setP (*p, P_grDensity, 200.0f); setP (*p, P_grSize, 500.0f);
                setP (*p, P_dnaMode, 3.0f);
                setP (*p, P_wmBits, 1.0f); setP (*p, P_wmDown, 32.0f); setP (*p, P_arShift, 1000.0f); setP (*p, P_arMod, 5.0f);
                auto st = render (*p, 2.0, chord (0.0, 1.5, { 36, 48, 60, 72 }));
                std::cout << "  everything at max: peak " << st.peak << std::endl;
                CHECK (st.finite && st.peak < 6.0f, "extreme mutation settings");
            }
        }

        // stress: every slot in use with random sources, destinations and curves
        {
            auto p = make();
            setP (*p, P_polyphony, 8);
            juce::Random rr (1234);
            const auto dests = modulationDestinations();
            for (int k = 0; k < kNumRoutes; ++k)
            {
                RouteConfig c; c.on = true; c.src = 1 + rr.nextInt (MS_COUNT - 1); c.dst = dests[(size_t) rr.nextInt ((int) dests.size())];
                c.curve = rr.nextInt (MC_COUNT); c.via = rr.nextBool() ? 0 : 1 + rr.nextInt (MS_COUNT - 1); c.smoothMs = rr.nextFloat() * 200;
                c.unipolar = rr.nextBool();
                p->routes.set (k, c);
                setP (*p, P_mod1Amt + k, rr.nextFloat() * 2 - 1);
            }
            double cpu = 0;
            auto st = render (*p, 3.0, chord (0.0, 2.0, { 36, 48, 55, 60, 64, 67, 72, 79 }), nullptr, &cpu);
            std::cout << "  32 random routes, 8 voices: peak " << st.peak << ", " << cpu / 3.0 * 100.0 << "% of one core" << std::endl;
            CHECK (st.finite && st.peak < 8.0f, "random route stress");
        }
    }

    // ---- Stage 16: the spectrum tap is a copy of the output, nothing more
    {
        auto p = make();
        juce::AudioBuffer<float> cap;
        render (*p, 0.5, chord (0.0, 0.4, { 57 }), &cap);
        float tail[512]; p->readScope (tail, 512);
        float md = 0;
        for (int i = 0; i < 512; ++i) md = std::max (md, std::abs (tail[i] - 0.5f * (cap.getSample (0, cap.getNumSamples() - 512 + i) + cap.getSample (1, cap.getNumSamples() - 512 + i))));
        CHECK (md < 1e-7f, "spectrum tap mirrors the output");
    }

    filterUnitTests();

    dnaTests();
    fxTests();
    playTests();
    hardTests();

    // ---- Stage 17: factory presets, quality setting, CPU budget
    {
        std::cout << "Stage 17: factory presets" << std::endl;
        const auto& list = factoryPresets();
        CHECK (list.size() == 28, "28 factory presets");
        std::set<juce::String> names;
        for (auto& fp : list) names.insert (fp.name);
        CHECK (names.size() == list.size(), "factory preset names are unique");

        double worstCpu = 0; juce::String worstName;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            PresetBuilder b; list[(size_t) i].build (b);
            CHECK (b.routes.size() <= (size_t) kNumRoutes, juce::String (list[(size_t) i].name) + ": too many routes");
            for (auto& r : b.routes)
                CHECK (r.dst >= 0 && meta (r.dst).modulatable, juce::String (list[(size_t) i].name) + ": route to a non-modulatable parameter " + (r.dst >= 0 ? meta (r.dst).id : juce::String ("-")));
            for (int k = 0; k < P_COUNT; ++k)
                CHECK (b.v[(size_t) k] >= meta (k).min - 1e-4f && b.v[(size_t) k] <= meta (k).max + 1e-4f,
                       juce::String (list[(size_t) i].name) + ": " + meta (k).id + " out of range");

            auto p = make();
            CHECK (p->loadFactoryPreset (i), "preset loads");
            CHECK (p->getPatchName() == list[(size_t) i].name, "preset sets the patch name");
            int active = 0;
            for (int r = 0; r < kNumRoutes; ++r) active += p->routes.get (r).active() ? 1 : 0;
            CHECK (active == (int) b.routes.size(), juce::String (list[(size_t) i].name) + ": routes installed");
            for (int r = 0; r < (int) b.routes.size(); ++r)
                CHECK (std::abs (p->param (P_mod1Amt + r)->convertFrom0to1 (p->param (P_mod1Amt + r)->getValue()) - b.routes[(size_t) r].depth) < 2e-3f, "route depth installed");

            const bool mono = b.v[P_polyphony] < 1.5f;
            std::vector<std::pair<double, juce::MidiMessage>> ev;
            if (mono)
            {
                const int notes[] = { 36, 43, 39, 41 };
                for (int n = 0; n < 4; ++n)
                {
                    ev.push_back ({ 0.05 + n * 0.5, juce::MidiMessage::noteOn (1, notes[n], (juce::uint8) 110) });
                    ev.push_back ({ 0.45 + n * 0.5, juce::MidiMessage::noteOff (1, notes[n]) });
                }
            }
            else ev = chord (0.05, 1.8, { 48, 55, 60, 63, 67, 72 });
            ev.push_back ({ 0.3, juce::MidiMessage::controllerEvent (1, 1, 80) });   // a little mod wheel
            juce::AudioBuffer<float> cap;
            double cpu = 0;
            auto s = render (*p, 3.0, ev, &cap, &cpu);
            const double pct = cpu / 3.0 * 100.0;
            std::cout << "  " << list[(size_t) i].name << ": peak " << s.peak << " rms " << s.rms << " cpu " << pct << "%" << std::endl;
            CHECK (s.finite, juce::String (list[(size_t) i].name) + ": non-finite");
            CHECK (s.peak > 0.02f, juce::String (list[(size_t) i].name) + ": too quiet");
            CHECK (s.peak < 3.0f, juce::String (list[(size_t) i].name) + ": too loud");
            CHECK (s.rms > 0.003, juce::String (list[(size_t) i].name) + ": no body");
            writeWav (cap, sr, "preset_" + juce::String (i + 1).paddedLeft ('0', 2) + "_" + juce::String (list[(size_t) i].name).replaceCharacter (' ', '_') + ".wav");
            if (pct > worstCpu) { worstCpu = pct; worstName = list[(size_t) i].name; }

            // the preset survives the host's save / restore
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make();
            q->setStateInformation (mb.getData(), (int) mb.getSize());
            bool same = q->getPatchName() == p->getPatchName();
            for (int k = 0; k < P_COUNT; ++k) same &= std::abs (q->param (k)->getValue() - p->param (k)->getValue()) < 1e-6f;
            for (int r = 0; r < kNumRoutes; ++r) same &= q->routes.get (r).src == p->routes.get (r).src && q->routes.get (r).dst == p->routes.get (r).dst;
            same &= q->dnaSteps.toString() == p->dnaSteps.toString();
            same &= q->dnaSteps.toJson() == p->dnaSteps.toJson();
            same &= q->fxRack.getOrder() == p->fxRack.getOrder();
            for (int k = 0; k < 4; ++k) same &= q->scenes.stored[k].load() == p->scenes.stored[k].load();
            CHECK (same, juce::String (list[(size_t) i].name) + ": state round trip");
        }
        std::cout << "  heaviest preset: " << worstName << " at " << worstCpu << "% of one core" << std::endl;
        CHECK (worstCpu < 60.0, "every factory preset stays inside the CPU budget (60% of one core, 6 voices)");

        // specific features really present
        {
            auto p = make();
            p->loadFactoryPreset (12);   // Breeding Pad
            int stored = 0; for (int k = 0; k < 4; ++k) stored += p->scenes.stored[k].load() ? 1 : 0;
            CHECK (stored == 4, "Breeding Pad stores four scenes");
            CHECK (p->param (P_sceneMorph)->getValue() > 0.5f, "Breeding Pad morphs");
            p->loadFactoryPreset (13);   // Sequenced DNA
            int steps = 0; for (int k = 0; k < 16; ++k) steps += p->dnaSteps.getType (k) != DT_Off ? 1 : 0;
            CHECK (steps >= 10, "Sequenced DNA has a step pattern");
            CHECK (p->param (P_dsOn)->getValue() > 0.5f, "Sequenced DNA runs the DNA Sequencer");
            CHECK (p->getMacroName (0) == "Sequence Depth", "preset macro names");
            // the Jungle / DnB bank uses the newer features for real
            auto byName = [&] (const char* n) { for (int i = 0; i < (int) list.size(); ++i) if (juce::String (list[(size_t) i].name) == n) return i; return -1; };
            p->loadFactoryPreset (byName ("Amen Reese"));
            CHECK (p->param (P_filter2On)->getValue() > 0.5f && juce::roundToInt (p->param (P_filter2Type)->convertFrom0to1 (p->param (P_filter2Type)->getValue())) == FT_NOTCH, "Amen Reese: notch on Filter 2");
            p->loadFactoryPreset (byName ("Neuro Rollers"));
            CHECK (p->dnaSteps.getChain() == "AAAB" && p->param (P_dsLane2On)->getValue() > 0.5f, "Neuro Rollers: chain and lane 2");
            CHECK (p->dnaSteps.get (0, 0, 14).ratchet == 3 && p->dnaSteps.get (0, 0, 10).prob < 0.7f && p->dnaSteps.get (1, 1, 0).type == DT_Shift, "Neuro Rollers: ratchet, probability, pattern B lane 2");
            CHECK (! p->dnaSteps.legacyOnly(), "Neuro Rollers is a v2 DNA pattern");
            p->loadFactoryPreset (byName ("Ragga Siren Stab"));
            CHECK (p->fxRack.getOrder()[0] == FS_Sampler && p->fxRack.getOrder()[1] == FS_Stutter, "Ragga Siren Stab: rack order");
            {
                // holding E0 (MIDI 28) repeats
                std::vector<std::pair<double, juce::MidiMessage>> ev { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
                                                                       { 0.1, juce::MidiMessage::noteOn (1, 28, (juce::uint8) 100) } };
                render (*p, 0.4, ev);
                CHECK (p->getEngine().fx().stutter.activity.load() > 0, "Ragga Siren Stab: E0 triggers Beat Repeat");
            }
            p->loadFactoryPreset (byName ("Dread Wobble"));
            CHECK (p->fxRack.curve (0) < 0.05f && p->fxRack.curve (22) < 0.25f && p->fxRack.curve (10) > 0.9f, "Dread Wobble: triplet pump curve");
            p->loadFactoryPreset (byName ("Init Genome"));
            { FxRackStore ref; CHECK (p->fxRack.getOrder() == ref.getOrder() && std::abs (p->fxRack.curve (10) - ref.curve (10)) < 1e-6f, "older presets get the default rack"); }
            p->loadFactoryPreset (0);
            CHECK (p->getMacroName (0) == "Mutate" && p->getMacroName (7) == "Space", "Init Genome macro names");
        }

        // Loading a preset clears the previous one completely and keeps the player's quality setting
        {
            auto p = make();
            setP (*p, P_quality, 0);
            p->loadFactoryPreset (12);
            p->loadFactoryPreset (4);
            int stored = 0; for (int k = 0; k < 4; ++k) stored += p->scenes.stored[k].load() ? 1 : 0;
            CHECK (stored == 0, "scenes cleared by the next preset");
            CHECK (p->param (P_sceneMorph)->getValue() < 0.5f, "scene morph cleared");
            CHECK (std::lround (p->param (P_quality)->convertFrom0to1 (p->param (P_quality)->getValue())) == 0, "quality kept across preset loads");
            CHECK (p->canUndo(), "preset load is undoable");
        }

        // Quality: Eco is cheaper than Normal where it changes something, High still renders cleanly.
        // Heavy patch: Chaos Engine plus a 16-mode resonator and the 4096-point spectral stage.
        {
            std::cout << "Stage 17: quality" << std::endl;
            std::unique_ptr<MegaSynthProcessor> procs[3];
            for (int q = 0; q < 3; ++q)
            {
                procs[q] = make();
                procs[q]->loadFactoryPreset (11);
                setP (*procs[q], P_resMix, 0.4f); setP (*procs[q], P_resModes, 16); setP (*procs[q], P_spSize, 3);
                setP (*procs[q], P_polyphony, 16);
                setP (*procs[q], P_quality, (float) q);
                procs[q]->updateLatency();
            }
            std::vector<std::pair<double, juce::MidiMessage>> ev;
            for (int n = 0; n < 12; ++n) ev.push_back ({ 0.01 * n, juce::MidiMessage::noteOn (1, 40 + n * 3, (juce::uint8) 100) });
            double best[3] = { 1e9, 1e9, 1e9 };
            for (int rep = 0; rep < 3; ++rep)
                for (int q = 0; q < 3; ++q)
                {
                    double cpu = 0;
                    auto s = render (*procs[q], 2.0, rep == 0 ? ev : std::vector<std::pair<double, juce::MidiMessage>> {}, nullptr, &cpu);
                    CHECK (s.finite && s.peak < 4.0f && s.peak > 0.01f, "quality " + juce::String (q) + " renders cleanly");
                    best[q] = std::min (best[q], cpu / 2.0 * 100.0);
                }
            std::cout << "  heavy patch, 12 voices: Eco " << best[0] << "%  Normal " << best[1] << "%  High " << best[2] << "%" << std::endl;
            CHECK (best[0] < best[1] * 0.95, "Eco is clearly cheaper than Normal");
            CHECK (procs[0]->getLatencySamples() <= procs[1]->getLatencySamples(), "Eco latency is no higher than Normal");
        }
    }

    // ---- 7. CPU: 16 voices of everything
    {
        std::cout << "CPU load" << std::endl;
        auto p = make();
        setP (*p, P_polyphony, 16);
        setP (*p, P_filterMode, 13);
        setP (*p, P_shimmerMix, 0.4f);
        setP (*p, P_reverseMix, 0.4f);
        setP (*p, P_supersawVoices, 9);
        std::vector<std::pair<double, juce::MidiMessage>> ev;
        for (int n = 0; n < 16; ++n) { ev.push_back ({ 0.01 * n, juce::MidiMessage::noteOn (1, 40 + n * 2, (juce::uint8) 90) }); }
        double cpu = 0;
        auto s = render (*p, 10.0, ev, nullptr, &cpu);
        std::cout << "  10s of 16 voices rendered in " << cpu << "s (" << (cpu / 10.0 * 100.0) << "% of one core)" << std::endl;
        CHECK (s.finite, "non-finite under load");
        for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix }) setP (*p, i, 0.0f);
        render (*p, 6.0, {});
        render (*p, 10.0, ev, nullptr, &cpu);
        std::cout << "  same without effects: " << (cpu / 10.0 * 100.0) << "%" << std::endl;
        for (int i : { P_complexGain, P_supersawGain }) setP (*p, i, 0.0f);
        render (*p, 6.0, {});
        render (*p, 10.0, ev, nullptr, &cpu);
        std::cout << "  ...and without Osc 5/6: " << (cpu / 10.0 * 100.0) << "%" << std::endl;
    }

    // ---- 9. warmth on/off comparison renders
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_WARMCOMPARE", {}).isNotEmpty())
    {
        for (int mode : { 0, 2, 3, 4, 9, 10, 12 })
            for (int on = 0; on < 2; ++on)
            {
                auto p = make();
                setP (*p, P_filterMode, (float) mode);
                for (int i : { P_delayMix, P_reverbMix, P_chorusMix }) setP (*p, i, 0.0f);
                if (! on) for (int i : { P_warmth, P_bassKeep, P_analogDrift }) setP (*p, i, 0.0f);
                juce::AudioBuffer<float> cap;
                render (*p, 1.2, chord (0.02, 1.0, { 36, 48 }), &cap);
                writeWav (cap, sr, "warm_" + juce::String (mode).paddedLeft ('0', 2) + (on ? "_on.wav" : "_off.wav"));
            }
    }

    // ---- 10. TB-303 closing test
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_303", {}).isNotEmpty())
    {
        for (int variant = 0; variant < 4; ++variant)
            for (float cut : { 40.0f, 400.0f, 2000.0f })
            {
                const float envAmt = 0.0f;
                auto p = make();
                setP (*p, P_filterMode, variant == 1 ? 0.0f : 12.0f);
                if (variant == 2) for (int i : { P_warmth, P_bassKeep, P_analogDrift }) setP (*p, i, 0.0f);
                if (variant == 3) setP (*p, P_lfoAssignTarget0, 0.0f);
                std::cout << "  variant " << variant;
                setP (*p, P_filterRes, 12.0f);
                setP (*p, P_filterCutoff, cut);
                setP (*p, P_fEnvAmt, envAmt);
                for (int i : { P_delayMix, P_reverbMix, P_chorusMix }) setP (*p, i, 0.0f);
                for (int i : { P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain }) setP (*p, i, 0.0f);
                juce::AudioBuffer<float> cap;
                render (*p, 0.6, chord (0.0, 0.5, { 45 }), &cap);
                writeWav (cap, sr, "tb_v" + juce::String (variant) + "_" + juce::String ((int) cut) + ".wav");
                float first = 0, sustain = 0;
                for (int i = 0; i < 2400; ++i) first = std::max (first, std::abs (cap.getSample (0, i)));
                double ss = 0; for (int i = 12000; i < 22000; ++i) ss += cap.getSample (0, i) * cap.getSample (0, i);
                sustain = (float) std::sqrt (ss / 10000);
                std::cout << "  envAmt " << envAmt << " cutoff " << cut << ": first 50ms peak " << juce::Decibels::gainToDecibels (first)
                          << " dB, sustain rms " << juce::Decibels::gainToDecibels (sustain) << " dB" << std::endl;
            }
    }

    // ---- 8. editor snapshots (offscreen) for every tab
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_SNAPSHOTS", {}).isNotEmpty())
    {
        std::cout << "Editor snapshots" << std::endl;
        auto p = make();
        p->addRoute (MS_Lfo1, P_filterCutoff, 0.3f);
        p->addRoute (MS_Velocity, P_osc1Semi, 0.25f);
        p->addRoute (MS_Osc2Audio, P_filterRes, -0.2f);
        p->addRoute (MS_ModWheel, P_delayMix, 0.5f);
        for (auto [i, v] : { std::pair<int, float> { P_dnaMix, 0.4f }, { P_wmMix, 0.5f }, { P_resMix, 0.2f }, { P_grMix, 0.3f }, { P_fbGrSp, 0.4f },
                             { P_fbOutGr, 0.2f }, { P_mutAmount, 0.35f }, { P_mutLock3, 1.0f }, { P_mutLock7, 1.0f } })
            setP (*p, i, v);
        render (*p, 0.6, chord (0.0, 0.55, { 48, 60, 64 }));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 860);
        std::function<void (juce::Component*)> rings = [&] (juce::Component* c)
        {
            if (auto* k = dynamic_cast<tgui::Knob*> (c)) k->updateMod();
            for (auto* ch : c->getChildren()) rings (ch);
        };
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        auto* tabs = findTabs (ed.get());
        CHECK (tabs != nullptr, "no tabs");
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders");
        dir.createDirectory();
        for (int i = 0; tabs != nullptr && i < tabs->getNumTabs(); ++i)
        {
            tabs->setCurrentTabIndex (i);
            rings (ed.get());
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("ui_" + juce::String (i) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
