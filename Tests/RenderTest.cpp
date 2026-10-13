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
#include "UI/PerfFxPage.h"
#include "UI/SamplePages.h"
#include "UI/BreakPages.h"
#include "UI/Overview.h"
#include "UI/CorePage.h"

using namespace tg;

static int failures = 0;
namespace juce { namespace detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); } }   // (to run posted messages in the test)
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

static bool isLfoV2ParamForTest (const juce::String& id)
{
    if (! id.startsWith ("lfo") || id.length() < 5 || ! juce::CharacterFunctions::isDigit (id[3])) return false;
    const auto rest = id.substring (4);
    for (auto* x : { "Sync", "Retrig", "Phase", "Mode", "Smooth", "Uni", "Fade", "Custom" }) if (rest == x) return true;
    return false;
}

static bool showPage (juce::AudioProcessorEditor* ed, const juce::String& name)
{
    auto* e = dynamic_cast<MegaSynthEditor*> (ed);
    return e != nullptr && e->showPage (name);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    // the tests start from the bare parameter defaults (what every golden render and old test assumes);
    // the Init tests switch Init-on-create back on, with the user Init kept in a temporary folder
    MegaSynthProcessor::applyInitOnCreate = false;
    const auto testDataDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("megasynth_test_data");
    testDataDir.createDirectory();
    setenv ("MEGASYNTH_DATA_DIR", testDataDir.getFullPathName().toRawUTF8(), 1);
    // (and a patch folder of its own, so the tests never touch your patches)
    testDataDir.getChildFile ("Patches").deleteRecursively();
    setenv ("MEGASYNTH_PATCH_DIR", testDataDir.getChildFile ("Patches").getFullPathName().toRawUTF8(), 1);
    MegaSynthProcessor::getUserInitFile().deleteFile();

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
        ed->setSize (1200, 890);
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        juce::ignoreUnused (findTabs);
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        const char* fxPages[] = { "Space", "Dirt", "Motion", "Rack" };
        for (int k = 0; k < 4; ++k)
        {
            showPage (ed.get(), fxPages[k]);
            auto img = ed->createComponentSnapshot (juce::Rectangle<int> (0, 88, 1200, 670), true, 1.0f);
            auto f = dir.getChildFile ("fx_ui_" + juce::String (k) + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        return 0;
    }

    // Demo renders of the Jungle / DnB bank in a simulated DAW: 172 BPM, play pressed mid-bar, 8 bars
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_DEMOS", {}).isNotEmpty())
    {
        struct FakeHead : juce::AudioPlayHead
        {
            PositionInfo info;
            juce::Optional<PositionInfo> getPosition() const override { return info; }
        } head;
        const double bpm = 172.0, startPpq = 2.5, beats = 32.0;
        const auto& list = factoryPresets();
        for (int idx = 0; idx < (int) list.size(); ++idx)
        {
            if (juce::String (list[(size_t) idx].category) != "Jungle / DnB") continue;
            const juce::String desc (list[(size_t) idx].description);
            auto p = std::make_unique<MegaSynthProcessor>();
            p->setPlayConfigDetails (0, 2, sr, 256); p->prepareToPlay (sr, 256);
            p->setPlayHead (&head);
            p->loadFactoryPreset (idx);
            // notes as (beat from the start, length in beats, note, velocity)
            struct N { double at, len; int note, vel; };
            std::vector<N> notes;
            auto bars = [&] (std::function<void (double)> oneBar2) { for (double b = 0; b < beats; b += 8.0) oneBar2 (b); };
            if (desc.startsWith ("Bass"))
                bars ([&] (double o)
                {
                    const int line[16] = { 41, 0, 41, 44, 0, 41, 39, 0, 41, 0, 46, 44, 0, 39, 41, 0 };
                    for (int k = 0; k < 16; ++k) if (line[k]) notes.push_back ({ o + k * 0.5, 0.42, line[k] - 12, 105 });
                });
            else if (desc.startsWith ("Pad"))
                bars ([&] (double o)
                {
                    for (int n : { 53, 56, 60, 63 }) notes.push_back ({ o, 3.9, n, 90 });
                    for (int n : { 49, 53, 56, 60 }) notes.push_back ({ o + 4, 3.9, n, 90 });
                });
            else if (desc.startsWith ("Arp"))
                bars ([&] (double o)
                {
                    for (int n : { 53, 56, 60 }) notes.push_back ({ o, 3.9, n, 100 });
                    for (int n : { 49, 53, 56 }) notes.push_back ({ o + 4, 3.9, n, 100 });
                    if (desc.contains ("one key")) { notes.erase (notes.end() - 6, notes.end()); notes.push_back ({ o, 3.9, 53, 100 }); notes.push_back ({ o + 4, 3.9, 49, 100 }); }
                });
            else if (desc.startsWith ("Lead"))
                bars ([&] (double o)
                {
                    const int mel[16] = { 65, 0, 68, 70, 72, 0, 70, 68, 65, 0, 63, 65, 68, 0, 0, 0 };
                    for (int k = 0; k < 16; ++k) if (mel[k]) notes.push_back ({ o + k * 0.5, mel[k + 1 < 16 ? k + 1 : 15] == 0 ? 0.95 : 0.45, mel[k], 100 });
                });
            else   // stabs and plucks: off-beat chords
                bars ([&] (double o)
                {
                    for (double t : { 0.0, 0.75, 1.5, 2.5, 3.25, 4.0, 4.75, 5.5, 6.5, 7.0 })
                        for (int n : { 53, 56, 60 })
                            if (n == 53 || ! desc.contains ("chord memory")) notes.push_back ({ o + t, 0.22, n, 100 });   // chord memory: one key
                });
            if (juce::String (list[(size_t) idx].name) == "Ragga Siren Stab")
                notes.push_back ({ 13.0, 1.0, 28, 100 });   // hold E0 for a beat: Beat Repeat
            const double spb = 60.0 / bpm * sr;
            const int total = (int) ((beats + 2.0) * spb);
            juce::AudioBuffer<float> out (2, total), buf (2, 256);
            for (int pos = 0; pos < total; pos += 256)
            {
                const int n = std::min (256, total - pos);
                buf.setSize (2, n, false, false, true); buf.clear();
                const double ppq = startPpq + pos / spb;
                head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                juce::MidiBuffer midi;
                for (auto& nt : notes)
                {
                    const double lead = 4.0 - startPpq;   // notes sit on the song's bars; play was pressed mid-bar
                    const int on = (int) std::lround ((nt.at + lead) * spb), off = (int) std::lround ((nt.at + nt.len + lead) * spb);
                    if (on >= pos && on < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, nt.note, (juce::uint8) nt.vel), on - pos);
                    if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, nt.note), off - pos);
                }
                p->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
            }
            std::cout << list[(size_t) idx].name << ": peak " << out.getMagnitude (0, total) << std::endl;
            writeWav (out, sr, "demo_" + juce::String (list[(size_t) idx].name).replaceCharacter (' ', '_') + ".wav");
        }
        return 0;
    }

    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_LFOSHOT", {}).isNotEmpty())
    {
        const juce::String tabName = juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_LFOSHOT", {}) == "1" ? juce::String ("LFOs")
                                                                                                             : juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_LFOSHOT", {});
        auto p = make();
        setP (*p, P_arpOn, 1.0f); setP (*p, P_chdOn, 1.0f);
        p->arpStore.setStep (2, ArpStore::ST_Accent, 1.0f); p->arpStore.setStep (5, ArpStore::ST_Rest, 1.0f); p->arpStore.setStep (9, ArpStore::ST_On, 0.4f);
        p->lfoShapes.setPreset (1, LfoShapeStore::PR_Wub); setP (*p, P_lfo1Custom + kLfoXStride, 1);
        setP (*p, P_lfo1Sync + kLfoXStride, 9); setP (*p, P_lfo1Retrig + 2 * kLfoXStride, 2);
        p->lfoShapes.setPreset (3, LfoShapeStore::PR_Stairs); setP (*p, P_lfo1Custom + 3 * kLfoXStride, 1);
        p->msegs.setPreset (1, 2); p->msegs.setPreset (0, 3); setP (*p, P_pEnvAmt, 12.0f);
        for (int k : { 0, 1, 3 }) setP (*p, P_uni1Voices + kUniStride * k, 5.0f);
        render (*p, 0.3, chord (0.0, 1.0, { 48 }));
        render (*p, 0.05, { { 0.0, juce::MidiMessage::pitchWheel (1, 12000) }, { 0.0, juce::MidiMessage::controllerEvent (1, 1, 80) }, { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) } });
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 890);
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        showPage (ed.get(), tabName);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        auto f = dir.getChildFile ("tab_" + tabName.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") + ".png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }

    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BROWSERSHOT", {}).isNotEmpty())
    {
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 890);
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
        ed->setSize (1200, 890);
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        showPage (ed.get(), "DNA Sequencer");
        auto img = ed->createComponentSnapshot (juce::Rectangle<int> (0, 88, 1200, 670), true, 1.0f);
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
                ed->setSize (1200, 890);
                std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
                {
                    if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
                    for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
                    return nullptr;
                };
                showPage (ed.get(), "Filters");
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
            {   // hiss follows the signal: there while playing, gone in silence
                auto w = params(); w[P_smpModel] = 2; w[P_smpNoise] = 1.0f; w[P_smpDrive] = 0;
                VintageSampler t; t.prepare (fs);
                const int N = 96000; std::vector<float> l (N), r (N);
                for (int i = 0; i < N; ++i) l[(size_t) i] = r[(size_t) i] = i < 24000 ? 0.3f * (float) std::sin (2.0 * kPi * 220.0 * i / fs) : 0.0f;
                t.process (l.data(), r.data(), N, w.data(), ctx);
                float tail = 0; for (int i = N - 4800; i < N; ++i) tail = std::max (tail, std::abs (l[(size_t) i]));
                auto clean = params(); clean[P_smpModel] = 2; clean[P_smpNoise] = 0.0f; clean[P_smpDrive] = 0;
                VintageSampler u; u.prepare (fs);
                std::vector<float> l2 (24000), r2 (24000);
                for (int i = 0; i < 24000; ++i) l2[(size_t) i] = r2[(size_t) i] = 0.3f * (float) std::sin (2.0 * kPi * 220.0 * i / fs);
                u.process (l2.data(), r2.data(), 24000, clean.data(), ctx);
                double diff = 0; for (int i = 4800; i < 24000; ++i) diff += std::pow (l[(size_t) i] - l2[(size_t) i], 2.0);
                diff = std::sqrt (diff / 19200.0);
                CHECK (tail < 1.0e-5f && diff > 0.0005, "sampler hiss rides on the signal (playing " + juce::String (20 * std::log10 (diff), 1) + " dB, silence " + juce::String (tail) + ")");
            }
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
            for (int k = 0; k < FS_COUNT; ++k) setP (*p, fxOnParam (k), 1.0f);
            uint32_t r = 99; auto rnd = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f; };
            bool finite = true; float peak = 0;
            for (int blk = 0; blk < 60; ++blk)
            {
                for (int i = 0; i < P_COUNT; ++i)
                {
                    const juce::String id (kParamIds[i]);
                    if (! (id.startsWith ("mbc") || id.startsWith ("smp") || id.startsWith ("rpt") || id.startsWith ("flp") || id.startsWith ("vsh") || id.startsWith ("stt") || id.startsWith ("sx"))) continue;
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
    // ---- LFO sync, retrigger, one-shot, shapes (state version 7)
    auto lfoTests = [&]
    {
        std::cout << "LFOs" << std::endl;
        struct FakeHead : juce::AudioPlayHead
        {
            PositionInfo info;
            juce::Optional<PositionInfo> getPosition() const override { return info; }
        };
        const int X = kLfoXStride;
        auto routeProbe = [] (MegaSynthProcessor& p, int src)   // a route so the voice computes its sources (depth 0: no effect)
        {
            RouteConfig c; c.on = true; c.src = src; c.dst = P_filterRes; p.routes.set (0, c); setP (p, P_mod1Amt, 0.0f);
        };
        // phases follow the song position, whatever the tempo and wherever play is pressed
        for (double bpm : { 90.0, 140.0, 174.0 })
        {
            for (int retrig : { 1, 2, 3 })   // free (song position), each bar, host start
            {
                FakeHead head;
                auto p = std::make_unique<MegaSynthProcessor>();
                p->setPlayConfigDetails (0, 2, sr, 64); p->prepareToPlay (sr, 64);
                p->setPlayHead (&head);
                const int syncIdx = retrig == 2 ? 14 : 5;   // 1 bar / 1/8 triplet
                const double cyc = retrig == 2 ? 4.0 : 1.0 / 3.0;
                setP (*p, P_lfo1Sync, (float) syncIdx); setP (*p, P_lfo1Retrig, (float) retrig); setP (*p, P_lfo1Phase, 0.125f);
                const double start = 5.37, inc = bpm / 60.0 / sr;
                double ppq = start, worst = 0.0;
                juce::AudioBuffer<float> buf (2, 64);
                for (int b = 0; b < 600; ++b)
                {
                    head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                    head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                    head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                    juce::MidiBuffer midi; if (b == 3) midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 10);
                    buf.clear(); p->processBlock (buf, midi);
                    if (b > 4)
                    {
                        const double at = ppq + 48 * inc;   // the last 16-sample control step of the block
                        double origin = 0.0;
                        if (retrig == 2) origin = std::floor (at / 4.0) * 4.0;
                        if (retrig == 3) origin = start;
                        double want = (at - origin) / cyc + 0.125; want -= std::floor (want);
                        double d = std::abs (p->getEngine().lfoPlayhead[0].load() - want); d = std::min (d, 1.0 - d);
                        worst = std::max (worst, d);
                    }
                    ppq += 64 * inc;
                }
                CHECK (worst < 1e-4, "LFO locked to the DAW (" + juce::String (bpm) + " BPM, retrigger " + juce::String (retrig) + "): worst phase error " + juce::String (worst));
            }
        }
        // synced, each note: one cycle per 1/4 at the synth's own tempo when the DAW isn't playing
        {
            auto p = make();
            setP (*p, P_lfo1Sync, 9.0f); setP (*p, P_seqTempo, 120.0f);
            render (*p, 0.002, chord (0.0, 2.0, { 48 }));
            double last = p->getEngine().lfoPlayhead[0].load(); int wraps = 0;
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < (int) (2.2 * sr / 512); ++b)   // 2.2 s: 4 full cycles
            {
                buf.clear(); juce::MidiBuffer m; p->processBlock (buf, m);
                const double ph = p->getEngine().lfoPlayhead[0].load();
                if (ph < last) ++wraps;
                last = ph;
            }
            CHECK (wraps == 4, "1/4 at 120 BPM: 2 cycles a second (" + juce::String (wraps) + " wraps in 2.2 s)");
        }
        // Phase knob and one-shot (each note)
        {
            auto p = make();
            setP (*p, P_lfo1Phase, 0.25f); setP (*p, P_lfo1Mode, 1.0f); setP (*p, P_lfo1Rate, 4.0f);
            render (*p, 0.0004, chord (0.0, 2.0, { 48 }));
            CHECK (std::abs (p->getEngine().lfoPlayhead[0].load() - 0.25f) < 0.01f, "Phase sets where each note starts");
            render (*p, 0.5, {});
            CHECK (p->getEngine().lfoPlayhead[0].load() > 0.9999f, "one-shot holds at the end");
        }
        // Free (Hz): one phase for every note
        {
            auto a = make(), b = make();
            setP (*a, P_lfo1Retrig, 1.0f); setP (*b, P_lfo1Retrig, 1.0f);
            render (*a, 0.3, chord (0.0, 2.0, { 48 })); render (*a, 0.2, {});
            render (*b, 0.3, {}); render (*b, 0.2, chord (0.0, 2.0, { 55 }));
            CHECK (std::abs (a->getEngine().lfoPlayhead[0].load() - b->getEngine().lfoPlayhead[0].load()) < 1e-4f, "Free LFO doesn't restart with notes");
        }
        // custom shapes: the table matches the drawn curve; steps hold; presets
        {
            LfoShapeStore st;
            std::vector<LfoShapeStore::Point> pts { { 0.0f, 0.0f }, { 0.5f, 1.0f, LfoShapeStore::CV_Smooth }, { 0.75f, 0.2f, LfoShapeStore::CV_Step } };
            st.setPoints (0, pts);
            double worst = 0;
            for (int i = 0; i < 2000; ++i) { const double x = (i + 0.37) / 2000.0; worst = std::max (worst, (double) std::abs (st.eval (0, x) - LfoShapeStore::shapeAt (pts, x))); }
            CHECK (worst < 0.01, "custom shape table matches the curve (" + juce::String (worst) + ")");
            CHECK (std::abs (st.eval (0, 0.8) - 0.2f) < 1e-6f && std::abs (st.eval (0, 0.99) - 0.2f) < 1e-6f, "stepped segment holds");
            CHECK (std::abs (st.eval (0, 0.25) - 0.5f) < 0.01f && std::abs (st.eval (0, 0.625) - 0.6f) < 0.02f, "linear / smooth segments");
            st.setPreset (1, LfoShapeStore::PR_SawDown);
            CHECK (std::abs (st.eval (1, 0.25) - 0.75f) < 0.01f, "saw down preset");
            st.setPreset (1, LfoShapeStore::PR_Square);
            CHECK (st.eval (1, 0.2) > 0.99f && st.eval (1, 0.7) < 0.01f, "square preset");
            pts[0].bend = 0.8f; st.setPoints (2, pts);
            CHECK (st.eval (2, 0.1) > 0.3f, "bend bows a segment");
            LfoShapeStore u; u.fromVar (st.toVar());
            CHECK (std::abs (u.eval (0, 0.6) - st.eval (0, 0.6)) < 1e-6f && u.getPoints (0).size() == 3, "shapes save and load");
        }
        // in the synth: custom, unipolar, smoothing and fade change what LFO 1 sends
        {
            auto run = [&] (std::function<void (MegaSynthProcessor&)> setup)
            {
                auto p = make(); routeProbe (*p, MS_Lfo1); setP (*p, P_lfo1Depth, 1.0f); setP (*p, P_lfo1Rate, 5.0f); setP (*p, P_lfo1Wave, 3.0f);
                setup (*p);
                std::vector<float> v;
                render (*p, 0.0004, chord (0.0, 2.0, { 48 }));
                juce::AudioBuffer<float> buf (2, 64);
                for (int b = 0; b < (int) (1.0 * sr / 64); ++b) { buf.clear(); juce::MidiBuffer m; p->processBlock (buf, m); v.push_back (p->getEngine().liveSrc[MS_Lfo1].load()); }
                return v;
            };
            auto lo = [] (const std::vector<float>& v) { return *std::min_element (v.begin(), v.end()); };
            auto maxJump = [] (const std::vector<float>& v) { float m = 0; for (size_t i = 1; i < v.size(); ++i) m = std::max (m, std::abs (v[i] - v[i - 1])); return m; };
            const auto plain = run ([] (MegaSynthProcessor&) {});
            CHECK (lo (plain) < -0.99f && maxJump (plain) > 1.9f, "square LFO swings -1..1");
            const auto uni = run ([] (MegaSynthProcessor& p) { setP (p, P_lfo1Uni, 1.0f); });
            CHECK (lo (uni) > -1e-6f, "unipolar stays 0..1");
            const auto sm = run ([] (MegaSynthProcessor& p) { setP (p, P_lfo1Smooth, 0.6f); });
            CHECK (maxJump (sm) < 0.6f, "smoothing slews the square (" + juce::String (maxJump (sm)) + ")");
            const auto fd = run ([] (MegaSynthProcessor& p) { setP (p, P_lfo1Fade, 0.5f); });
            float early = 0; for (int i = 0; i < 40; ++i) early = std::max (early, std::abs (fd[(size_t) i]));
            float late = 0; for (size_t i = 600; i < fd.size(); ++i) late = std::max (late, std::abs (fd[i]));
            CHECK (early < 0.4f && late > 0.99f, "fade in");
            const auto cu = run ([] (MegaSynthProcessor& p) { p.lfoShapes.setPreset (0, LfoShapeStore::PR_Stairs); setP (p, P_lfo1Custom, 1.0f); });
            std::set<int> lv; for (float x : cu) lv.insert (juce::roundToInt ((x + 1.0f) * 3.5f));
            CHECK (lv.size() >= 7 && lv.size() <= 9, "custom stairs: 8 levels (" + juce::String ((int) lv.size()) + ")");
            // also drives the legacy assignment slots and the filter's LFO amount
            auto p = make(); setP (*p, P_filterLfoAmt, 0.5f); setP (*p, P_lfo1Uni, 1.0f); setP (*p, P_lfo1Depth, 1.0f);
            auto q = make(); setP (*q, P_filterLfoAmt, 0.5f); setP (*q, P_lfo1Depth, 1.0f);
            juce::AudioBuffer<float> c1, c2;
            for (auto* x : { p.get(), q.get() }) { setP (*x, P_analogDrift, 0.0f); setP (*x, P_supersawGain, 0.0f); setP (*x, P_complexGain, 0.0f); }
            render (*p, 0.5, chord (0.0, 0.5, { 48 }), &c1); render (*q, 0.5, chord (0.0, 0.5, { 48 }), &c2);
            double diff = 0; for (int i = 0; i < c1.getNumSamples(); ++i) diff = std::max (diff, (double) std::abs (c1.getSample (0, i) - c2.getSample (0, i)));
            CHECK (diff > 1e-3, "the filter's LFO follows the new options");
        }
        // saved with projects and patches; undo; old patches and projects get the original behaviour
        {
            auto p = make();
            setP (*p, P_lfo1Sync + 2 * X, 6.0f); setP (*p, P_lfo1Custom + 2 * X, 1.0f);
            p->lfoShapes.setPreset (2, LfoShapeStore::PR_Wub);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (juce::roundToInt (q->param (P_lfo1Sync + 2 * X)->convertFrom0to1 (q->param (P_lfo1Sync + 2 * X)->getValue())) == 6
                   && q->lfoShapes.toJson() == p->lfoShapes.toJson(), "LFO settings and shapes saved with the project");
            const auto patch = p->exportBrowserPatch();
            auto r = make(); r->importBrowserPatch (patch);
            CHECK (r->lfoShapes.toJson() == p->lfoShapes.toJson() && r->param (P_lfo1Custom + 2 * X)->getValue() > 0.5f, "LFO shapes saved in patches");
            // a patch from before (no LFO extras): back to the original LFOs
            auto obj = juce::JSON::parse (patch);
            if (auto* ex = obj["plugin"].getDynamicObject()) for (auto& prop : juce::NamedValueSet (ex->getProperties())) if (isLfoV2ParamForTest (prop.name.toString())) ex->removeProperty (prop.name);
            if (auto* o = obj.getDynamicObject()) o->removeProperty ("lfoShapes");
            r->importBrowserPatch (juce::JSON::toString (obj));
            CHECK (r->param (P_lfo1Sync + 2 * X)->getValue() < 1e-6f && r->param (P_lfo1Custom + 2 * X)->getValue() < 0.5f, "old patches load with the original LFOs");
            // an old project: state version 6
            auto xml = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"6\"");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (xml), old);
            auto o = make(); o->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (o->param (P_lfo1Sync + 2 * X)->getValue() < 1e-6f, "old projects load with the original LFOs");
            // undo
            p->pushHistory ("x");
            p->lfoShapes.setPreset (2, LfoShapeStore::PR_Stairs); p->pushHistory ("shape");
            p->undo();
            CHECK (p->lfoShapes.getPoints (2).size() == 4, "shape edits are undoable");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_LFOONLY", {}).isNotEmpty())
    {
        lfoTests();
        std::cout << (failures == 0 ? "LFO TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- Chord memory and arpeggiator (state version 8)
    auto arpTests = [&]
    {
        std::cout << "Chord memory / arpeggiator" << std::endl;
        struct FakeHead : juce::AudioPlayHead
        {
            PositionInfo info;
            juce::Optional<PositionInfo> getPosition() const override { return info; }
        };
        // chord shapes
        {
            ArpStore st;
            CHECK ((chordIntervals (1, st, 0, 0) == std::vector<int> { 0, 3, 7, 10 }), "minor 7");
            CHECK ((chordIntervals (6, st, 0, 0) == std::vector<int> { 0, 7, 12 }), "power");
            CHECK ((chordIntervals (1, st, 0, 1) == std::vector<int> { 3, 7, 10, 12 }), "first inversion");
            CHECK ((chordIntervals (1, st, 0, -1) == std::vector<int> { -2, 0, 3, 7 }), "inversion down");
            CHECK ((chordIntervals (1, st, 1, 0) == std::vector<int> { 0, 7, 15, 22 }), "spread: every other note up an octave");
            st.setLearned ({ 62, 65, 69 });
            CHECK ((chordIntervals (0, st, 0, 0) == std::vector<int> { 0, 3, 7 }), "learned chord relative to its lowest note");
        }
        // chord memory in the synth: one key, a whole chord, in poly and mono
        for (int poly : { 10, 1 })
        {
            auto p = make(); setP (*p, P_polyphony, (float) poly); setP (*p, P_chdOn, 1.0f); setP (*p, P_chdPreset, 7.0f);
            render (*p, 0.1, { { 0.0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) } });
            CHECK (p->getEngine().activeVoiceCount() == 5, "rave minor: 5 voices from one key (poly " + juce::String (poly) + ")");
            render (*p, 0.05, { { 0.0, juce::MidiMessage::noteOff (1, 48) } });
            int held = 0; for (int j = 0; j < 8; ++j) held += p->getEngine().isHeld (2000 + 48 * 16 + j) ? 1 : 0;
            CHECK (held == 0, "letting go releases the whole chord");
        }
        // Learn
        {
            auto p = make(); p->arpStore.learning = true;
            render (*p, 0.05, { { 0.0, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) }, { 0.01, juce::MidiMessage::noteOn (1, 53, (juce::uint8) 100) },
                                { 0.02, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) }, { 0.03, juce::MidiMessage::noteOff (1, 53) },
                                { 0.035, juce::MidiMessage::noteOff (1, 57) }, { 0.04, juce::MidiMessage::noteOff (1, 50) } });
            CHECK (! p->arpStore.learning.load() && (p->arpStore.getLearned() == std::vector<int> { 0, 3, 7 }), "Learn stores the chord played");
            CHECK (p->param (P_chdOn)->getValue() > 0.5f && p->param (P_chdPreset)->getValue() < 1e-6f, "Learn switches chord memory on with the learned chord");
        }
        // the arpeggiator in a simulated DAW: steps on the song's grid at any tempo, play pressed mid-bar
        auto runArp = [&] (double bpm, std::function<void (MegaSynthProcessor&)> setup, std::vector<std::pair<double, juce::MidiMessage>> ev, double beats,
                           std::vector<MegaSynthProcessor::ArpLogEntry>& log, double startPpq = 5.37)
        {
            FakeHead head;
            auto p = std::make_unique<MegaSynthProcessor>();
            p->setPlayConfigDetails (0, 2, sr, 512); p->prepareToPlay (sr, 512);
            p->setPlayHead (&head);
            setP (*p, P_arpOn, 1.0f);
            setup (*p);
            p->arpLog = &log;
            const double inc = bpm / 60.0 / sr;
            juce::AudioBuffer<float> buf (2, 256);
            double ppq = startPpq;
            for (int pos = 0; ppq < startPpq + beats; pos += 256)
            {
                head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                juce::MidiBuffer midi;
                for (auto& e : ev) { const int at = (int) std::lround ((e.first - startPpq) / inc); if (at >= pos && at < pos + 256) midi.addEvent (e.second, at - pos); }
                buf.clear(); p->processBlock (buf, midi);
                ppq += 256 * inc;
            }
            p->arpLog = nullptr;
            p->setPlayHead (nullptr);   // the fake head goes out of scope
            return p;
        };
        auto onsOf = [] (const std::vector<MegaSynthProcessor::ArpLogEntry>& log) { std::vector<MegaSynthProcessor::ArpLogEntry> v; for (auto& e : log) if (e.on) v.push_back (e); return v; };
        auto hold = [] (std::initializer_list<int> notes, double at) { std::vector<std::pair<double, juce::MidiMessage>> v; for (int n : notes) v.push_back ({ at, juce::MidiMessage::noteOn (1, n, (juce::uint8) 100) }); return v; };
        for (double bpm : { 90.0, 140.0, 174.0 })
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            runArp (bpm, [] (MegaSynthProcessor&) {}, hold ({ 48, 52, 55 }, 6.1), 4.0, log);
            const auto on = onsOf (log);
            const double tol = 1.5 * bpm / 60.0 / sr;   // a sample and a half
            bool grid = on.size() >= 10; double worst = 0;
            for (auto& e : on) { const double d = std::abs (e.beat / 0.25 - std::round (e.beat / 0.25)) * 0.25; worst = std::max (worst, d); grid &= d <= tol; }
            CHECK (grid, "arp steps on the 1/16 grid at " + juce::String (bpm) + " BPM (" + juce::String ((int) on.size()) + " notes, worst " + juce::String (worst, 8) + " beats)");
            CHECK (! on.empty() && std::abs (on[0].beat - 6.25) < tol, "first arp note on the next grid line after the key");
            bool order = true; const int want[3] = { 48, 52, 55 };
            for (size_t i = 0; i < on.size(); ++i) order &= on[i].note == want[i % 3];
            CHECK (order, "Up mode: 48 52 55 48 ...");
            // gate: each note ends half a step later
            bool gate = true;
            for (auto& e : log) if (! e.on) { const double frac = (e.beat - std::floor (e.beat / 0.25) * 0.25) / 0.25; gate &= std::abs (frac - 0.5) < 0.01; }
            CHECK (gate, "Gate 50%");
        }
        // a key pressed just after a grid line plays straight away
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            runArp (120.0, [] (MegaSynthProcessor&) {}, hold ({ 60 }, 6.01), 1.0, log);
            const auto on = onsOf (log);
            CHECK (! on.empty() && std::abs (on[0].beat - 6.01) < 0.002, "a slightly late key isn't held back to the next step");
        }
        // swing, octaves, modes, repeats
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            runArp (120.0, [] (MegaSynthProcessor& p) { setP (p, P_arpSwing, 0.5f); }, hold ({ 48, 52 }, 6.0), 2.0, log);
            const auto on = onsOf (log);
            bool sw = on.size() >= 6;
            for (auto& e : on) { const double k = e.beat / 0.25; const double fr = k - std::floor (k + 1e-6); const bool odd = ((long long) std::floor (k + 1e-6)) & 1;
                                 sw &= odd ? std::abs (fr - 0.5) < 0.01 : fr < 0.01; }
            CHECK (sw, "Swing 50%: every second step half a step late");
        }
        {
            auto seqOf = [&] (int mode, int octaves, int repeats)
            {
                std::vector<MegaSynthProcessor::ArpLogEntry> log;
                runArp (120.0, [=] (MegaSynthProcessor& p) { setP (p, P_arpMode, (float) mode); setP (p, P_arpOctaves, (float) octaves); setP (p, P_arpRepeats, (float) repeats); },
                        hold ({ 60, 64, 67 }, 6.0), 3.0, log);
                std::vector<int> v; for (auto& e : onsOf (log)) v.push_back (e.note);
                return v;
            };
            auto starts = [] (const std::vector<int>& v, std::vector<int> w) { if (v.size() < w.size()) return false; for (size_t i = 0; i < w.size(); ++i) if (v[i] != w[i]) return false; return true; };
            CHECK (starts (seqOf (1, 1, 1), { 67, 64, 60, 67 }), "Down");
            CHECK (starts (seqOf (2, 1, 1), { 60, 64, 67, 64, 60, 64 }), "Up / Down");
            CHECK (starts (seqOf (3, 1, 1), { 67, 64, 60, 64, 67 }), "Down / Up");
            CHECK (starts (seqOf (0, 2, 1), { 60, 64, 67, 72, 76, 79, 60 }), "two octaves");
            CHECK (starts (seqOf (0, 1, 2), { 60, 60, 64, 64, 67, 67 }), "repeats");
            const auto rnd = seqOf (5, 1, 1);
            bool inSet = rnd.size() > 8; for (int n : rnd) inSet &= n == 60 || n == 64 || n == 67;
            CHECK (inSet, "Random stays on the held notes");
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            runArp (120.0, [] (MegaSynthProcessor& p) { setP (p, P_arpMode, 6.0f); }, hold ({ 60, 64, 67 }, 6.0), 1.0, log);
            const auto on = onsOf (log);
            CHECK (on.size() >= 6 && on[0].beat == on[1].beat && on[1].beat == on[2].beat, "Chord mode plays them together");
        }
        // as played; pattern rests, accents and probability
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            auto ev = hold ({ 67 }, 6.0); ev.push_back ({ 6.001, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }); ev.push_back ({ 6.002, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) });
            runArp (120.0, [] (MegaSynthProcessor& p) { setP (p, P_arpMode, 4.0f); }, ev, 2.0, log);
            const auto on = onsOf (log);
            CHECK (on.size() >= 3 && on[0].note == 67 && on[1].note == 60 && on[2].note == 64, "As played");
        }
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            auto p = runArp (120.0, [] (MegaSynthProcessor& q)
            {
                setP (q, P_arpSteps, 4.0f); setP (q, P_arpVelMode, 1.0f); setP (q, P_arpVelFixed, 0.5f);
                q.arpStore.setStep (1, ArpStore::ST_Rest, 1.0f); q.arpStore.setStep (2, ArpStore::ST_Accent, 1.0f); q.arpStore.setStep (3, ArpStore::ST_On, 0.0f);
            }, hold ({ 60, 64 }, 6.0), 4.0, log);
            const auto on = onsOf (log);
            bool ok = on.size() >= 6;
            for (auto& e : on)
            {
                const int step = ((int) std::lround ((e.beat - 6.0) / 0.25)) % 4;
                ok &= step == 0 || step == 2;   // step 2 (index 1) rests, step 4 (index 3) never plays
                if (step == 2) ok &= e.vel > 0.7f; else ok &= std::abs (e.vel - 0.5f) < 1e-3f;
            }
            CHECK (ok, "pattern: rest, accent and a 0% step");
            CHECK (on.size() >= 2 && on[0].note == 60 && on[1].note == 64, "a rest doesn't skip a note");
        }
        // latch and each-bar restart
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            auto ev = hold ({ 60, 64 }, 6.0);
            ev.push_back ({ 6.6, juce::MidiMessage::noteOff (1, 60) }); ev.push_back ({ 6.6, juce::MidiMessage::noteOff (1, 64) });
            runArp (120.0, [] (MegaSynthProcessor& p) { setP (p, P_arpLatch, 1.0f); }, ev, 3.0, log);
            bool late = false; for (auto& e : onsOf (log)) late |= e.beat > 8.0;
            CHECK (late, "Latch keeps playing after the keys are let go");
            std::vector<MegaSynthProcessor::ArpLogEntry> log2;
            auto ev2 = ev; ev2.push_back ({ 7.5, juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100) });
            runArp (120.0, [] (MegaSynthProcessor& p) { setP (p, P_arpLatch, 1.0f); }, ev2, 4.0, log2);
            bool only72 = true; int n72 = 0; for (auto& e : onsOf (log2)) if (e.beat > 7.6) { only72 &= e.note == 72; ++n72; }
            CHECK (only72 && n72 > 4, "a new chord after letting go replaces the latched one");
            std::vector<MegaSynthProcessor::ArpLogEntry> log3;
            runArp (120.0, [] (MegaSynthProcessor& p) { setP (p, P_arpRestart, 1.0f); setP (p, P_arpRate, 4.0f); }, hold ({ 60, 64, 67, 71, 74 }, 6.0), 6.0, log3);   // 1/8T: 6 per bar over 5 notes
            bool bar = false; for (auto& e : onsOf (log3)) if (std::abs (e.beat - 8.0) < 0.01) bar = e.note == 60;
            CHECK (bar, "Each bar: the cycle starts again at the bar line");
        }
        // chord memory feeds the arpeggiator; the arp drives DNA restarts and its own mod sources
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            runArp (120.0, [] (MegaSynthProcessor& p) { setP (p, P_chdOn, 1.0f); setP (p, P_chdPreset, 6.0f); }, hold ({ 48 }, 6.0), 2.0, log);
            std::vector<int> v; for (auto& e : onsOf (log)) v.push_back (e.note);
            juce::StringArray dbg; for (int x : v) dbg.add (juce::String (x));
            CHECK (v.size() >= 3 && v[0] == 48 && v[1] == 55 && v[2] == 60, "one key + chord memory = an arpeggiated chord (" + dbg.joinIntoString (" ") + ")");
            std::vector<MegaSynthProcessor::ArpLogEntry> log2;
            auto p = runArp (120.0, [] (MegaSynthProcessor& q)
            {
                setP (q, P_dsOn, 1.0f); setP (q, P_dsRestart, 1.0f); setP (q, P_dsRate, 0.0f);   // 1/32, restart on each note
                for (int i = 0; i < 16; ++i) q.dnaSteps.set (i, DT_Fold, 0.5f);
                RouteConfig c; c.on = true; c.src = MS_ArpGate; c.dst = P_filterRes; q.routes.set (0, c);
            }, hold ({ 60, 64 }, 6.0), 0.66, log2);   // ends just after the step at beat 6
            CHECK (p->getEngine().dnaSeqStep.load() <= 1, "arp notes restart the DNA Sequencer (" + juce::String (p->getEngine().dnaSeqStep.load()) + ")");
            CHECK (p->getEngine().liveSrc[MS_ArpGate].load() > 0.3f, "Arp Step Gate pulses (" + juce::String (p->getEngine().liveSrc[MS_ArpGate].load()) + ")");
        }
        // switching off ends the arp's notes; saved with projects / patches; old ones get it off; undo
        {
            std::vector<MegaSynthProcessor::ArpLogEntry> log;
            auto p = runArp (120.0, [] (MegaSynthProcessor& q) { setP (q, P_arpGate, 1.0f); }, hold ({ 60 }, 6.0), 0.5, log);
            setP (*p, P_arpOn, 0.0f);
            render (*p, 0.05, {});
            CHECK (! p->getEngine().isHeld (3060), "switching the arp off ends its note");
            setP (*p, P_arpMode, 3.0f); setP (*p, P_chdPreset, 4.0f);
            p->arpStore.setStep (5, ArpStore::ST_Accent, 0.5f); p->arpStore.setLearned ({ 0, 5, 9 });
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q->arpStore.toJson() == p->arpStore.toJson() && q->param (P_arpMode)->getValue() == p->param (P_arpMode)->getValue(), "arp saved with the project");
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (r->arpStore.toJson() == p->arpStore.toJson() && r->param (P_chdPreset)->getValue() == p->param (P_chdPreset)->getValue(), "arp and chord saved in patches");
            setP (*p, P_arpOn, 1.0f); juce::MemoryBlock mb2; p->getStateInformation (mb2);
            auto x2 = getXmlFromBinaryForTest (mb2).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"7\"");
            {   // a version 7 project has no arpeggiator data at all
                const int a0 = x2.indexOf (" arp=\"");
                if (a0 >= 0) { const int a1 = x2.indexOfChar (a0 + 6, '"'); x2 = x2.substring (0, a0) + x2.substring (a1 + 1); }
            }
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x2), old);
            auto o = make(); o->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (o->param (P_arpOn)->getValue() < 0.5f && o->arpStore.getState (5) == ArpStore::ST_On, "old projects load with the arp off and a fresh pattern");
            p->pushHistory ("x"); p->arpStore.setStep (0, ArpStore::ST_Rest, 1.0f); p->pushHistory ("step"); p->undo();
            CHECK (p->arpStore.getState (0) == ArpStore::ST_On, "pattern edits are undoable");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ARPONLY", {}).isNotEmpty())
    {
        arpTests();
        std::cout << (failures == 0 ? "ARP TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- Sidechain input (state version 9)
    auto scTests = [&]
    {
        std::cout << "Sidechain" << std::endl;
        {
            // every parameter's range is sound (a skew centre outside the range gives NaN)
            auto p = make();
            for (int i = 0; i < P_COUNT; ++i)
            {
                auto* prm = p->param (i);
                const float d = prm->convertTo0to1 (meta (i).def);
                CHECK (std::isfinite (d) && d >= -1e-6f && d <= 1.0f + 1e-6f && std::isfinite (prm->convertFrom0to1 (0.5f)), juce::String ("range of ") + meta (i).id);
            }
        }
        {
            auto p = std::make_unique<MegaSynthProcessor>();
            CHECK (p->getBusCount (true) == 1 && p->getBus (true, 0)->getName() == "Sidechain" && ! p->getBus (true, 0)->isEnabledByDefault(), "an optional sidechain input, off by default");
            juce::AudioProcessor::BusesLayout l; l.outputBuses.add (juce::AudioChannelSet::stereo());
            l.inputBuses.add (juce::AudioChannelSet::disabled()); CHECK (p->checkBusesLayoutSupported (l), "no sidechain");
            l.inputBuses.getReference (0) = juce::AudioChannelSet::stereo(); CHECK (p->checkBusesLayoutSupported (l), "stereo sidechain");
            l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); CHECK (p->checkBusesLayoutSupported (l), "mono sidechain");
            CHECK (p->getVST3ClientExtensions() != nullptr && ! p->getVST3ClientExtensions()->getPluginHasMainInput(), "VST3: the input is an aux (sidechain) bus");
        }
        // a processor with the sidechain switched on; kick-like hits on the input
        auto makeSc = [&]
        {
            auto p = std::make_unique<MegaSynthProcessor>();
            juce::AudioProcessor::BusesLayout l; l.outputBuses.add (juce::AudioChannelSet::stereo()); l.inputBuses.add (juce::AudioChannelSet::stereo());
            p->setBusesLayout (l);
            p->setRateAndBufferSizeDetails (sr, 512);
            p->prepareToPlay (sr, 512);
            return p;
        };
        auto runSc = [&] (MegaSynthProcessor& p, double secs, std::function<float (int)> scSig, std::vector<std::pair<double, juce::MidiMessage>> ev, std::vector<float>& outL)
        {
            juce::AudioBuffer<float> buf (2, 512);
            const int total = (int) (secs * sr);
            for (int pos = 0; pos < total; pos += 512)
            {
                for (int i = 0; i < 512; ++i) { const float x = scSig (pos + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
                juce::MidiBuffer midi;
                for (auto& e : ev) { const int at = (int) (e.first * sr); if (at >= pos && at < pos + 512) midi.addEvent (e.second, at - pos); }
                p.processBlock (buf, midi);
                for (int i = 0; i < 512; ++i) outL.push_back (buf.getSample (0, i));
            }
        };
        // kicks every 0.37 s (deliberately off the beat grid)
        auto kicks = [&] (int i) { const int per = (int) (0.37 * sr), k = i % per; return k < 2400 ? 0.9f * std::exp (-k / 600.0f) * (float) std::sin (k * 0.012) : 0.0f; };
        {
            auto p = makeSc(); std::vector<float> o;
            runSc (*p, 0.75, kicks, {}, o);   // ends just after a kick
            float pk = 0; for (float x : o) pk = std::max (pk, std::abs (x));
            CHECK (pk < 1e-6f, "the sidechain is never heard in the output (" + juce::String (pk) + ")");
            CHECK (p->scMeter.load() > 0.1f, "sidechain meter");
        }
        {
            // Volume Shaper on sidechain hits: the dips follow the kicks, not the beat
            auto p = makeSc();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix }) setP (*p, i, 0.0f);
            setP (*p, P_fxOnShaper, 1.0f); setP (*p, P_vshSidechain, 1.0f); setP (*p, P_vshDepth, 1.0f); setP (*p, P_vshSmooth, 0.0f); setP (*p, P_vshRate, 0.0f);
            std::vector<float> o;
            runSc (*p, 2.0, kicks, { { 0.0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) } }, o);
            auto rmsAt = [&] (double t0, double t1) { double e = 0; int n = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) { e += o[(size_t) i] * o[(size_t) i]; ++n; } return std::sqrt (e / std::max (1, n)); };
            double dip = 0, after = 0;
            for (int k = 1; k < 5; ++k) { const double t = k * 0.37; dip += rmsAt (t + 0.005, t + 0.03); after += rmsAt (t + 0.2, t + 0.3); }
            std::cout << "  shaper on sidechain hits: just after a kick " << juce::String (20 * std::log10 (dip / after + 1e-9), 1) << " dB vs later" << std::endl;
            CHECK (dip < after * 0.3, "each sidechain hit ducks the synth");
            auto q = makeSc();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix }) setP (*q, i, 0.0f);
            setP (*q, P_fxOnShaper, 1.0f); setP (*q, P_vshSidechain, 1.0f); setP (*q, P_vshDepth, 1.0f); setP (*q, P_schThresh, -3.0f);   // above the kicks: no hits
            std::vector<float> o2;
            runSc (*q, 2.0, kicks, { { 0.0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) } }, o2);
            double d2 = 0; for (int k = 2; k < 5; ++k) { const double t = k * 0.37; double e = 0; for (int i = (int) ((t + 0.005) * sr); i < (int) ((t + 0.03) * sr); ++i) e += o2[(size_t) i] * o2[(size_t) i]; d2 += std::sqrt (e / (0.025 * sr)); }
            CHECK (d2 / 3.0 > (dip / 4.0) * 2.5, "the threshold decides what counts as a hit");
        }
        {
            // Multiband: the downward side listens to the sidechain
            auto level = [&] (bool sc, bool kicking)
            {
                auto p = makeSc();
                for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix }) setP (*p, i, 0.0f);
                setP (*p, P_fxOnMultiband, 1.0f); setP (*p, P_mbcSidechain, sc ? 1.0f : 0.0f); setP (*p, P_mbcDepth, 1.0f);
                for (int b = 0; b < 3; ++b) { setP (*p, P_mbcUpL + 4 * b, 0.0f); setP (*p, P_mbcDownL + 4 * b, 1.0f); }
                setP (*p, P_masterVolume, 0.05f);   // the synth itself stays under the threshold
                for (int i : { P_analogDrift, P_supersawGain, P_complexGain }) setP (*p, i, 0.0f);   // no random start phases
                std::vector<float> o;
                runSc (*p, 1.5, kicking ? std::function<float (int)> ([&] (int i) { return 0.9f * (float) std::sin (i * 0.02); }) : [] (int) { return 0.0f; },
                       { { 0.0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) } }, o);
                double e = 0; for (size_t i = o.size() / 2; i < o.size(); ++i) e += o[i] * o[i];
                return std::sqrt (e / (o.size() / 2));
            };
            const double quiet = level (true, false), loud = level (true, true), off = level (false, true);
            std::cout << "  multiband sidechain: " << juce::String (20 * std::log10 (loud / quiet), 1) << " dB when the sidechain is loud" << std::endl;
            CHECK (loud < quiet * 0.5 && std::abs (off / quiet - 1.0) < 0.05, "multiband sidechain ducks only when switched on");
        }
        {
            // mod source, and old projects
            auto p = makeSc();
            RouteConfig c; c.on = true; c.src = MS_Sidechain; c.dst = P_filterRes; p->routes.set (0, c);
            std::vector<float> o;
            runSc (*p, 0.3, [] (int i) { return 0.5f * (float) std::sin (i * 0.02); }, { { 0.0, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) } }, o);
            CHECK (p->getEngine().liveSrc[MS_Sidechain].load() > 0.3f, "Sidechain Envelope mod source");
            setP (*p, P_vshSidechain, 1.0f); setP (*p, P_schThresh, -10.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"8\"");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
            auto q = make(); q->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (q->param (P_vshSidechain)->getValue() < 0.5f && std::abs (q->param (P_schThresh)->convertFrom0to1 (q->param (P_schThresh)->getValue()) + 24.0f) < 0.1f, "old projects: sidechain off");
            auto r = make(); r->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (r->param (P_vshSidechain)->getValue() > 0.5f, "sidechain settings saved");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_SCONLY", {}).isNotEmpty())
    {
        scTests();
        std::cout << (failures == 0 ? "SC TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- Unison on every oscillator (state version 10)
    auto uniTests = [&]
    {
        std::cout << "Unison" << std::endl;
        auto clean = [&]
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc2Gain, P_osc3Gain, P_subGain }) setP (*p, i, 0.0f);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f);
            return p;
        };
        auto renderNote = [&] (MegaSynthProcessor& p, juce::AudioBuffer<float>& cap, int note = 57) { render (p, 1.0, chord (0.0, 0.9, { note }), &cap); };
        auto maxDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            double d = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) d = std::max (d, (double) std::abs (a.getSample (c, i) - b.getSample (c, i))); return d;
        };
        {
            // one voice: exactly the oscillator as it was, whatever the other unison settings
            auto a = clean(), b = clean();
            setP (*b, P_uni1Detune, 70.0f); setP (*b, P_uni1Spread, 1.0f); setP (*b, P_uni1Blend, 1.0f); setP (*b, P_uni1Curve, 1.0f); setP (*b, P_uniSamePhase, 1.0f);
            juce::AudioBuffer<float> ca, cb; renderNote (*a, ca); renderNote (*b, cb);
            CHECK (maxDiff (ca, cb) == 0.0, "1 voice = no unison, bit for bit");
        }
        {
            // detune lands where it should: 2 voices, sine, centred, 50 cents apart
            auto p = clean(); setP (*p, P_osc1Wave, 3.0f); setP (*p, P_uni1Voices, 2.0f); setP (*p, P_uni1Detune, 50.0f); setP (*p, P_uni1Spread, 0.0f); setP (*p, P_uni1Blend, 1.0f);
            juce::AudioBuffer<float> c; renderNote (*p, c, 69);
            std::vector<float> x (32768, 0.0f);
            for (int i = 0; i < 16384; ++i) x[(size_t) i] = c.getSample (0, 8000 + i) * (0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / 16383.0));
            juce::dsp::FFT fft (15); x.resize (65536, 0.0f); fft.performFrequencyOnlyForwardTransform (x.data());
            auto peakNear = [&] (double f) { const double bin = f * 32768.0 / sr; int best = (int) bin; for (int k = (int) bin - 6; k <= (int) bin + 6; ++k) if (x[(size_t) k] > x[(size_t) best]) best = k; return best * sr / 32768.0; };
            const double f0 = peakNear (440.0), f1 = peakNear (440.0 * std::pow (2.0, 50.0 / 1200.0));
            std::cout << "  2 voices, 50 cents: peaks at " << juce::String (f0, 1) << " and " << juce::String (f1, 1) << " Hz" << std::endl;
            CHECK (std::abs (f0 - 440.0) < 1.5 && std::abs (f1 - 452.9) < 1.5, "unison detune in cents");
        }
        {
            // more voices: wider in stereo, about the same loudness
            auto a = clean(), b = clean();
            setP (*b, P_uni1Voices, 7.0f); setP (*b, P_uni1Detune, 25.0f); setP (*b, P_uni1Spread, 1.0f);
            juce::AudioBuffer<float> ca, cb; renderNote (*a, ca); renderNote (*b, cb);
            auto rms = [] (const juce::AudioBuffer<float>& c, int ch) { double e = 0; for (int i = 4800; i < 40000; ++i) e += c.getSample (ch, i) * c.getSample (ch, i); return std::sqrt (e / 35200); };
            double side = 0; for (int i = 4800; i < 40000; ++i) { const double sd = 0.5 * (cb.getSample (0, i) - cb.getSample (1, i)); side += sd * sd; }
            side = std::sqrt (side / 35200);
            const double lvl = 20 * std::log10 (rms (cb, 0) / rms (ca, 0));
            std::cout << "  7 voices: " << juce::String (lvl, 1) << " dB vs 1 voice, side " << juce::String (20 * std::log10 (side / rms (cb, 0)), 1) << " dB" << std::endl;
            CHECK (std::abs (lvl) < 4.0 && side > rms (cb, 0) * 0.2, "unison spreads in stereo at about the same level");
            // same phase every note: two notes identical; random: different
            auto s1 = clean(), s2 = clean();
            for (auto* q : { s1.get(), s2.get() }) { setP (*q, P_uni1Voices, 5.0f); setP (*q, P_uniSamePhase, 1.0f); }
            juce::AudioBuffer<float> c1, c2; renderNote (*s1, c1); renderNote (*s2, c2);
            CHECK (maxDiff (c1, c2) < 1e-6, "Same phase: every note starts the same");
            auto r1 = clean(), r2 = clean();
            for (auto* q : { r1.get(), r2.get() }) setP (*q, P_uni1Voices, 5.0f);
            juce::AudioBuffer<float> d1, d2; renderNote (*r1, d1); renderNote (*r2, d2);
            CHECK (maxDiff (d1, d2) > 1e-3, "random phases: notes differ");
        }
        {
            // Osc 2 / 3, Complex and the wavetables
            for (int k : { 1, 2, 5 })
            {
                auto p = clean();
                setP (*p, P_osc1Gain, 0.0f);
                setP (*p, k == 1 ? P_osc2Gain : (k == 2 ? P_osc3Gain : P_complexGain), 0.6f);
                setP (*p, P_uni1Voices + kUniStride * k, 4.0f); setP (*p, P_uni1Spread + kUniStride * k, 1.0f);
                juce::AudioBuffer<float> c; renderNote (*p, c);
                double d = 0; for (int i = 4800; i < 40000; ++i) d = std::max (d, (double) std::abs (c.getSample (0, i) - c.getSample (1, i)));
                CHECK (d > 0.01, "unison on oscillator " + juce::String (k + 1));
            }
            juce::AudioBuffer<float> sine (1, 48000);
            for (int i = 0; i < 48000; ++i) sine.setSample (0, i, 0.5f * std::sin (2.0 * kPi * 440.0 * i / 48000.0) + 0.2f * std::sin (2.0 * kPi * 1320.0 * i / 48000.0));
            juce::MemoryBlock wavData;
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::MemoryOutputStream (wavData, false), 48000.0, 1, 16, {}, 0));
                w->writeFromAudioSampleBuffer (sine, 0, 48000);
            }
            for (int slot = 0; slot < 2; ++slot)
            {
                auto p = clean(); setP (*p, P_osc1Gain, 0.0f);
                CHECK (p->loadSampleData (wavData, "u.wav", slot), "sample");
                setP (*p, slot == 0 ? P_osc4Gain : P_wt2Gain, 0.8f);
                setP (*p, P_uni1Voices + kUniStride * (3 + slot), 5.0f); setP (*p, P_uni1Spread + kUniStride * (3 + slot), 1.0f);
                juce::AudioBuffer<float> c; render (*p, 1.0, chord (0.0, 0.9, { 69 }), &c);
                double d = 0, e = 0; for (int i = 4800; i < 40000; ++i) { d = std::max (d, (double) std::abs (c.getSample (0, i) - c.getSample (1, i))); e = std::max (e, (double) std::abs (c.getSample (0, i))); }
                CHECK (d > 0.01 && e < 4.0, "unison on wavetable " + juce::String (slot + 1));
            }
        }
        {
            // CPU per unison voice
            auto cost = [&] (int voices)
            {
                auto p = make(); setP (*p, P_polyphony, 8.0f);
                for (int k : { 0, 1, 2 }) setP (*p, P_uni1Voices + kUniStride * k, (float) voices);
                double cpu = 0; render (*p, 2.0, chord (0.0, 2.0, { 48, 52, 55, 59, 62, 65, 69, 72 }), nullptr, &cpu);
                return cpu / 2.0 * 100.0;
            };
            const double c1 = cost (1), c8 = cost (8);
            const double perVoice = (c8 - c1) / (8 * 3 * 7);
            std::cout << "  CPU: 8 notes, Osc 1-3 at 1 voice " << juce::String (c1, 1) << "%, at 8 voices " << juce::String (c8, 1)
                      << "%: about " << juce::String (perVoice, 3) << "% of a core per extra unison voice" << std::endl;
            CHECK (perVoice < 0.5, "unison voices are cheap enough");
        }
        {
            auto p = make(); setP (*p, P_uni2Voices, 6.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"9\"");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
            auto q = make(); q->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (juce::roundToInt (q->param (P_uni2Voices)->convertFrom0to1 (q->param (P_uni2Voices)->getValue())) == 1, "old projects: no unison");
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (juce::roundToInt (r->param (P_uni2Voices)->convertFrom0to1 (r->param (P_uni2Voices)->getValue())) == 6, "unison saved in patches");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_UNIONLY", {}).isNotEmpty())
    {
        uniTests();
        std::cout << (failures == 0 ? "UNI TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- Pitch envelope and MSEGs (state version 11)
    auto envTests = [&]
    {
        std::cout << "Pitch envelope / MSEG" << std::endl;
        // the envelope runner on its own
        {
            MsegStore st; st.setPreset (0, 3);   // ADSR: 0.01 up, 0.3 to 0.6 (sustain), 0.5 to 0
            MsegRun r; r.start (st.get (0));
            float lv = 0; for (int i = 0; i < 10; ++i) lv = r.advance (0.001, 1.0f);
            CHECK (std::abs (lv - 1.0f) < 1e-4f, "MSEG reaches stage 1's level");
            for (int i = 0; i < 1000; ++i) lv = r.advance (0.001, 1.0f);
            CHECK (std::abs (lv - 0.6f) < 1e-4f && ! r.done, "MSEG holds at the sustain stage");
            r.release(); for (int i = 0; i < 250; ++i) lv = r.advance (0.001, 1.0f);
            CHECK (lv > 0.0f && lv < 0.6f, "release moves on from the sustain");
            for (int i = 0; i < 300; ++i) lv = r.advance (0.001, 1.0f);
            CHECK (lv < 1e-4f && r.done, "MSEG ends at the last stage");
        }
        {
            MsegStore st; st.setPreset (0, 2);   // rhythmic loop, 0.5 s per pass
            MsegRun r; r.start (st.get (0));
            int peaks = 0; float prev = 0, prev2 = 0;
            for (int i = 0; i < 1000; ++i) { const float v = r.advance (0.001, 1.0f); if (prev > 0.95f && prev >= v && prev > prev2) ++peaks; prev2 = prev; prev = v; }
            CHECK (peaks >= 2 && ! r.done, "loop keeps going while held (" + juce::String (peaks) + " full peaks in 1 s)");
            r.release(); float v = 0; for (int i = 0; i < 2000; ++i) v = r.advance (0.001, 1.0f);
            CHECK (r.done && v < 1e-4f, "release leaves the loop and finishes");
            MsegRun h; h.start (st.get (0)); for (int i = 0; i < 300; ++i) h.advance (0.001, 2.0f);
            CHECK (h.position() < 0.2f, "time scale 2: half as far along (" + juce::String (h.position(), 3) + ")");
            MsegRun q; q.start (st.get (0)); for (int i = 0; i < 300; ++i) q.advance (0.001, 1.0f);
            CHECK (q.position() > 0.2f && q.position() < 0.45f, "position follows time");
        }
        CHECK (std::abs (MsegStore::curveAt (0.5f, 0.0f) - 0.5f) < 1e-6f && MsegStore::curveAt (0.5f, 1.0f) < 0.1f && MsegStore::curveAt (0.5f, -1.0f) > 0.9f, "stage curves");
        {
            // store: clamps and round trip
            MsegStore st; MsegShape s; s.n = 3; s.st[0] = { 0.1f, 2.0f, 0.0f }; s.st[1] = { 99.0f, 0.5f, 5.0f }; s.st[2] = { 0.2f, 0.0f, 0.0f }; s.sustain = 7; s.loopStart = 2; s.loopEnd = 1;
            st.set (1, s);
            const auto g = st.get (1);
            CHECK (g.st[0].level == 1.0f && g.st[1].time == 16.0f && g.st[1].curve == 1.0f && g.sustain == -1 && g.loopStart == -1, "MSEG edits are kept in range");
            MsegStore t; t.fromVar (st.toVar());
            CHECK (t.toJson() == st.toJson(), "MSEG round trip");
        }
        // in the synth
        auto clean = [&]
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc2Gain, P_osc3Gain, P_subGain }) setP (*p, i, 0.0f);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f); setP (*p, P_osc1Wave, 3.0f);
            return p;
        };
        auto freqIn = [&] (const juce::AudioBuffer<float>& c, int ch, double t0, double t1)
        {
            double first = -1, last = -1; int n = 0;
            for (int i = (int) (t0 * sr) + 1; i < (int) (t1 * sr); ++i)
            {
                const float a = c.getSample (ch, i - 1), b = c.getSample (ch, i);
                if (a < 0.0f && b >= 0.0f) { const double x = (i - 1 + a / (a - b)) / sr; if (first < 0) first = x; last = x; ++n; }
            }
            return n > 1 ? (n - 1) / (last - first) : 0.0;
        };
        {
            auto off = clean(), on = clean();
            setP (*on, P_pEnvAmt, 12.0f); setP (*on, P_pEnvD, 0.15f);
            juce::AudioBuffer<float> a, b; render (*off, 1.0, chord (0.0, 0.9, { 57 }), &a); render (*on, 1.0, chord (0.0, 0.9, { 57 }), &b);
            const double early = freqIn (b, 0, 0.002, 0.012), late = freqIn (b, 0, 0.5, 0.8), plain = freqIn (a, 0, 0.002, 0.012);
            std::cout << "  +12 st: " << juce::String (early, 1) << " Hz at the start, " << juce::String (late, 1) << " Hz later (off: " << juce::String (plain, 1) << " Hz)" << std::endl;
            CHECK (early > 400.0 && std::abs (late - 220.0) < 1.0 && std::abs (plain - 220.0) < 3.0, "pitch envelope sweeps down to the note");
            // target: Osc 2 only leaves Osc 1 alone
            auto t = clean(); setP (*t, P_pEnvAmt, 12.0f); setP (*t, P_pEnvTarget, 2.0f);
            juce::AudioBuffer<float> c; render (*t, 1.0, chord (0.0, 0.9, { 57 }), &c);
            double d = 0; for (int i = 0; i < c.getNumSamples(); ++i) d = std::max (d, (double) std::abs (c.getSample (0, i) - a.getSample (0, i)));
            CHECK (d == 0.0, "target Osc 2: Osc 1 untouched");
            // velocity: a soft note sweeps less
            auto v = clean(); setP (*v, P_pEnvAmt, 12.0f); setP (*v, P_pEnvVel, 1.0f);
            juce::AudioBuffer<float> s; render (*v, 1.0, { { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 40) }, { 0.9, juce::MidiMessage::noteOff (1, 57) } }, &s);
            const double soft = freqIn (s, 0, 0.002, 0.012);
            CHECK (soft > 225.0 && soft < early - 60.0, "velocity scales the sweep (" + juce::String (soft, 1) + " Hz)");
            // negative: a scoop up from below
            auto n = clean(); setP (*n, P_pEnvAmt, -12.0f);
            juce::AudioBuffer<float> u; render (*n, 1.0, chord (0.0, 0.9, { 57 }), &u);
            CHECK (freqIn (u, 0, 0.002, 0.02) < 150.0, "negative amount scoops up");
            // attack: rises to the peak first
            auto at = clean(); setP (*at, P_pEnvAmt, 12.0f); setP (*at, P_pEnvA, 0.1f);
            juce::AudioBuffer<float> w; render (*at, 1.0, chord (0.0, 0.9, { 57 }), &w);
            CHECK (freqIn (w, 0, 0.002, 0.012) < freqIn (w, 0, 0.09, 0.11), "attack rises to the peak");
        }
        {
            // MSEG through the mod matrix, synced to the song: 1 beat per stage at 120 BPM
            struct FakeHead : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
            for (double bpm : { 90.0, 140.0, 174.0 })
            {
                FakeHead head;
                auto p = std::make_unique<MegaSynthProcessor>();
                p->setPlayConfigDetails (0, 2, sr, 512); p->prepareToPlay (sr, 512); p->setPlayHead (&head);
                MsegShape s; s.n = 1; s.start = 0.0f; s.st[0] = { 1.0f, 1.0f, 0.0f };
                p->msegs.set (1, s);
                setP (*p, P_mseg2Sync, 1.0f);
                p->addRoute (MS_Mseg2, P_filterRes, 0.0f);
                const double inc = bpm / 60.0 / sr; double ppq = 7.31;   // play pressed mid-bar
                juce::AudioBuffer<float> buf (2, 64);
                float at = -1;
                const int stopAt = (int) std::lround (0.5 / inc);   // half a beat
                for (int pos = 0; pos < stopAt; pos += 64)
                {
                    head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                    head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                    juce::MidiBuffer m; if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                    buf.clear(); p->processBlock (buf, m); ppq += 64 * inc;
                    at = p->getEngine().msegLevelShown[1].load();
                }
                p->setPlayHead (nullptr);
                CHECK (std::abs (at - 0.5f) < 0.03f, "synced MSEG: half way after half a beat at " + juce::String (bpm) + " BPM (" + juce::String (at, 3) + ")");
            }
        }
        {
            // saved with the project, the patch and undo; old projects have no pitch envelope
            auto p = make(); setP (*p, P_pEnvAmt, 7.0f); p->msegs.setPreset (0, 2); setP (*p, P_mseg1Scale, 2.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q->msegs.toJson() == p->msegs.toJson() && std::abs (q->param (P_pEnvAmt)->convertFrom0to1 (q->param (P_pEnvAmt)->getValue()) - 7.0f) < 0.01f, "pitch env and MSEGs saved in the project");
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (r->msegs.toJson() == p->msegs.toJson(), "MSEGs saved in patches");
            auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"10\"");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
            auto o = make(); o->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (std::abs (o->param (P_pEnvAmt)->convertFrom0to1 (o->param (P_pEnvAmt)->getValue())) < 1e-4f, "old projects: no pitch envelope");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ENVONLY", {}).isNotEmpty())
    {
        envTests();
        std::cout << (failures == 0 ? "ENV TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

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
            for (int i = 0; i < (int) factoryPresets().size(); ++i) CHECK (cats.contains (tg::factoryMeta (i).category), "factory category listed");
            auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("megasynth_favs_test.txt");
            tmp.deleteFile();
            tgui::Favourites fav (tmp);
            const auto all = c.filter ("All", "", fav);
            CHECK (all.size() >= (int) factoryPresets().size(), "All lists every factory preset");
            const auto& f0 = factoryPresets()[0];
            auto hit = c.filter ("All", juce::String (f0.name).toUpperCase(), fav);
            bool found = false; for (int i : hit) found |= c[i].factory && c[i].index == 0;
            CHECK (found, "search finds a preset by name (any case)");
            auto cat0 = c.filter (tg::factoryMeta (0).category, "", fav);
            bool only = cat0.size() > 0; for (int i : cat0) only &= c[i].category == tg::factoryMeta (0).category;
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
            CHECK (ed->getWidth() == 1500 && std::abs (ed->getHeight() - 1113) <= 1, "editor opens at the project's size");
            ed->setSize (900, 668);
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
        for (int k = 0; k < FS_COUNT; ++k) setP (*p, fxOnParam (k), 1.0f);
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
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ALLSHOTS", {}).isNotEmpty())
    {
        // a snapshot of every tab and sub-tab (renders/page_<name>.png)
        auto p = make();
        setP (*p, P_arpOn, 1.0f); setP (*p, P_pEnvAmt, 12.0f);
        setP (*p, P_mfOn, 1.0f); setP (*p, P_mfCutoff, 2500.0f); setP (*p, P_mfRes, 0.6f); setP (*p, P_mfSlope, 1.0f);
        if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ALLSHOTS", {}) == "char")
        { setP (*p, P_flt1Char, (float) CM_Formant); setP (*p, P_flt1CharAmt, 0.3f); setP (*p, P_filterRes, 10.0f); setP (*p, P_mfChar, (float) CM_Comb); setP (*p, P_mfCharAmt, 0.85f); setP (*p, P_mfCutoff, 300.0f); }
        p->msegs.setPreset (1, 2);
        setP (*p, P_cAEngine, 1.0f); p->applyCoreAlgorithm (0, 2); setP (*p, P_cA6M6, 1.2f); setP (*p, P_cA4Ratio, 2.0f); setP (*p, P_cA5Fixed, 1.0f);
        {
            // a made-up sample for the Granular Synth page
            juce::AudioBuffer<float> b (2, (int) (3.0 * 44100));
            for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / 44100.0;
                b.setSample (c, i, (float) (0.6 * std::sin (2.0 * kPi * 220.0 * t + c) * std::exp (-1.5 * std::fmod (t, 0.75)) + 0.2 * std::sin (2.0 * kPi * 331.0 * t)));
            }
            juce::MemoryBlock mb;
            { auto* os = new juce::MemoryOutputStream (mb, false); juce::WavAudioFormat wav;
              std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, 44100.0, 2, 16, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
            p->loadSampleData (mb, "Made-up pad.wav", MegaSynthProcessor::kSlotGranular);
            setP (*p, P_gsOn, 1.0f); setP (*p, P_gsScan, 0.4f);
            // and the demo break for the Break Chopper / Sequencer pages
            setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 0.0f); setP (*p, P_bsSteps, 1.0f);
            p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "Mega Break 170 (synthesised).wav", MegaSynthProcessor::kSlotBreak);
            for (int k = 1; k < 3; ++k)
            {
                setP (*p, tg::breakParams (k).sliceMode, 3.0f);
                p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, k == 1 ? 174.0 : 165.0, k), 44100.0),
                                   k == 1 ? "Mega Break 2-Step 174 (synthesised).wav" : "Mega Break Rollers 165 (synthesised).wav", MegaSynthProcessor::breakSlot (k));
            }
            setP (*p, P_bcActive, 2.0f); setP (*p, P_bcColour2, 2.0f); setP (*p, P_bcStretch2, 2.0f);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::jungle (d, 0, 32, 4); tg::breaktools::frankenbreak (d, 0, 32, 3, 0.7f, 0); tg::BreakStore::setChain (d, "AABA"); d.patBreak[0] = 2;
                                                        d.step[0][0].stretch = 1; d.step[0][9].stretch = 2; d.step[0][9].cycleMs = 120.0f; d.step[0][23].stretch = 3; d.step[0][12].nudge = 0.1f;
                                                        d.brk[1].slice[0].stretch = 2; d.brk[1].slice[0].cycleMs = 90.0f; });
        }
        render (*p, 0.3, chord (0.0, 1.0, { 48 }));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 890);
        auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        // a fill waiting to start, for the Break Sequencer's grid (the countdown and length show)
        setP (*p, P_bsFillPat, 8.0f);
        p->bsPatternShown.store (0); p->bsFillFirstIdx.store (16); p->bsFillCountdown.store (5); p->bsFillLenShown.store (16);
        for (auto& name : e->allPageNames())
        {
            e->showPage (name);
            e->timerCallbackForTests();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("page_" + name.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            std::cout << "SHOT " << name << std::endl;
        }
        {
            // the Break Chopper zoomed in on a slice, in Manual markers mode (the trim handles and the overview frame show)
            p->markersFromSlices (1);
            e->showPage ("Break Chopper"); e->timerCallbackForTests();
            if (auto* wv = dynamic_cast<tgui::SampleWaveView*> (e->breakWaveForTests))
            {
                p->breakStore.setAnalysis ([] (tg::BreakData& d) { d.brk[1].slice[0].trimStart = 0.1f; d.brk[1].slice[0].trimEnd = 0.8f; });
                wv->zoomTo ({ 0.05, 0.22 });
            }
            e->timerCallbackForTests();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("page_BreakChopper_zoom.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        {
            // the Filters page with a character model on Filter 1 and a classic model (TB-303) on the Master Filter
            setP (*p, P_flt1Char, (float) tg::CM_Resampler); setP (*p, P_mfOn, 1.0f); setP (*p, P_mfModel, 13.0f); setP (*p, P_mfRes, 0.7f);
            e->showPage ("Filters"); e->timerCallbackForTests();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("page_Filters_models.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        {
            // mod depth on the knobs: two routes on Filter 1's Cutoff (LFO 1 and Mod Env 1), one on Resonance
            setP (*p, P_flt1Char, 0.0f);
            p->addRoute (MS_Lfo1, P_filterCutoff, 0.3f); p->addRoute (MS_ModEnv1, P_filterCutoff, -0.2f); p->addRoute (MS_Macro2, P_filterRes, 0.4f);
            e->showPage ("Filters"); e->timerCallbackForTests();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("page_Filters_moddepth.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        {
            // the Reese engine on (4 voices, Beats mode, notch and sub), the Reese page, the Mixer and the Overview
            setP (*p, P_rsLevel, 0.8f); setP (*p, P_rsVoices, 2.0f); setP (*p, P_rsNotch, 0.6f); setP (*p, P_rsSub, 0.4f); setP (*p, P_rsDrive, 0.3f);
            render (*p, 0.2, chord (0.0, 0.2, { 36 }));
            for (auto nm : { "Reese", "Mixer", "Overview" })
            {
                e->showPage (nm); e->timerCallbackForTests();
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                auto f = dir.getChildFile (juce::String ("page_") + nm + "_on.png"); f.deleteFile();
                juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            }
        }
        {
            // Break React on, listening to snares, with a negative Offset (the line under it explains)
            setP (*p, P_rxOn, 1.0f); setP (*p, P_rxListen, 2.0f); setP (*p, P_rxPitch, 5.0f); setP (*p, P_rxDuck, 0.3f); setP (*p, P_rxOffset, -4.0f);
            e->showPage ("Break React"); e->timerCallbackForTests();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("page_BreakReact_on.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        {
            // Core A: a 2 s operator Release cut by a 0.3 s Amp Release (the amber hint shows); Key Entry on, advancing 1
            setP (*p, P_cA1Rel, 2.0f); setP (*p, P_ampR, 0.3f); setP (*p, P_bsKeyEntry, 1.0f); setP (*p, P_bsAdvance, 1.0f);
            for (auto nm : { "Core A", "Break Sequencer" })
            {
                e->showPage (nm); e->timerCallbackForTests();
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                auto f = dir.getChildFile (juce::String ("page_") + juce::String (nm).removeCharacters (" ") + "_new.png"); f.deleteFile();
                juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            }
        }
        {
            // the Break Sequencer editing the layer: Break 4 loaded, layer pattern A owns Break 4 (Original), Own patterns, the Fill pad learned
            p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 172.0, 2), 44100.0), "Mega Break Hats 172 (synthesised).wav", MegaSynthProcessor::breakSlot (3));
            setP (*p, P_bsLOn, 1.0f); setP (*p, P_bsLFollow, 1.0f); setP (*p, P_bsLPattern, 0.0f); setP (*p, P_bsKeyFixed, 1.0f); setP (*p, P_bsKeyVel, 0.7f);
            p->breakStore.edit ([] (tg::BreakData& d) { d.lpatBreak[0] = 4; });
            p->runBreakTool (tg::breaktools::ToolHatShuffle, true, 0);
            p->runBreakTool (MegaSynthProcessor::BtOriginal, true, 0);
            p->fillPadKind = 1; p->fillPadNum = 36;
            e->showPage ("Break Sequencer"); e->setBreakEditLayerForTests (true); e->timerCallbackForTests(); e->timerCallbackForTests();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("page_BreakSequencer_layer.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            e->setBreakEditLayerForTests (false);
            // (v0.1.71) pads learned for A-D, pairs (A with Layer B, B with Layer C), Pattern Break 2 (yellow), the Snare Roll's settings open
            for (int k = 0; k < 4; ++k) { p->patPadKind[k] = 1; p->patPadNum[k] = 36 + k; }
            p->patPadKind[8] = 2; p->patPadNum[8] = 20;
            p->breakStore.edit ([] (tg::BreakData& d) { d.lpair[0] = 2; d.lpair[1] = 3; d.lpair[2] = 0; d.patBreak[0] = 2; d.step[0][5].brk = 3; });
            setP (*p, P_bsLFollow, 0.0f); setP (*p, P_bsPattern, 0.0f);
            e->showPage ("Break Sequencer"); e->timerCallbackForTests(); e->timerCallbackForTests();
            for (auto nm : { "pairs", "roll" })
            {
                if (juce::String (nm) == "roll") e->showRollPanelForTests (true);
                e->timerCallbackForTests();
                auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                auto f2 = dir.getChildFile (juce::String ("page_BreakSequencer_") + nm + ".png"); f2.deleteFile();
                juce::FileOutputStream os2 (f2); juce::PNGImageFormat().writeImageToStream (img2, os2);
            }
            e->showRollPanelForTests (false);
        }
        {
            // Random Patch after making one (the report shows)
            tg::randompatch::Options o; o.seed = 1172; o.category = tg::randompatch::C_Reese; o.complexity = 1;
            p->randomOptions = o;
            p->generateRandomPatchNow (o);
            e->showPage ("Random Patch"); e->timerCallbackForTests(); e->timerCallbackForTests();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = dir.getChildFile ("page_RandomPatch_made.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_UICONTROLS", {}).isNotEmpty())
    {
        // every parameter that has a control somewhere in the editor (knobs, switches, menus, segment pickers)
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 890);
        std::set<juce::String> ids;
        std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
        {
            if (auto* k = dynamic_cast<tgui::Knob*> (c)) ids.insert (kParamIds[k->paramIndex]);
            if (auto* sg = dynamic_cast<tgui::Segmented*> (c); sg != nullptr && sg->getParam() >= 0) ids.insert (kParamIds[sg->getParam()]);
            if (c->getProperties().contains ("megaParam")) ids.insert (kParamIds[(int) c->getProperties()["megaParam"]]);
            if (c->getProperties().contains ("megaParam2")) ids.insert (kParamIds[(int) c->getProperties()["megaParam2"]]);   // (the Filter list: model and character)
            if (auto* xy = dynamic_cast<tgui::PerfXY*> (c)) { ids.insert (kParamIds[xy->px()]); ids.insert (kParamIds[xy->py()]); }
            for (auto* ch : c->getChildren()) walk (ch);
        };
        walk (ed.get());
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        if (auto* tabs = findTabs (ed.get()))
            for (int t = 0; t < tabs->getNumTabs(); ++t)
                if (auto* pg = tabs->getTabContentComponent (t); pg != nullptr && pg->getParentComponent() == nullptr) walk (pg);
        for (auto& id : ids) std::cout << "UICTL " << id << std::endl;
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_DUMPDEFAULTS", {}).isNotEmpty())
    {
        auto p = make();
        for (int i = 0; i < P_COUNT; ++i)
        {
            auto* prm = p->param (i);
            std::cout << "DEF " << kParamIds[i] << " | " << prm->getName (60) << " | " << prm->getCurrentValueAsText() << " | norm " << prm->getValue() << std::endl;
        }
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_WHEELS", {}).isNotEmpty())
    {
        // does pitch bend / the mod wheel change the sound at all, preset by preset?
        const auto& list = factoryPresets();
        auto diffWith = [&] (int idx, std::vector<std::pair<double, juce::MidiMessage>> extra)
        {
            auto run = [&] (bool withExtra)
            {
                auto p = make(); if (idx >= 0) p->loadFactoryPreset (idx);
                for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) setP (*p, i, 0.0f);   // no background-loaded reverbs
                auto ev = chord (0.0, 1.4, { 57 });
                if (withExtra) for (auto& e : extra) ev.push_back (e);
                juce::AudioBuffer<float> c; render (*p, 1.5, ev, &c);
                return c;
            };
            auto a = run (false), b = run (true);
            double d = 0, e = 0;
            for (int i = (int) (0.6 * sr); i < (int) (1.3 * sr); ++i) { d += std::pow (a.getSample (0, i) - b.getSample (0, i), 2.0); e += std::pow (a.getSample (0, i), 2.0); }
            return 10.0 * std::log10 ((d + 1e-20) / (e + 1e-20));   // dB of difference relative to the signal
        };
        auto pitchOf = [&] (const juce::AudioBuffer<float>& c, double t0)
        {
            // autocorrelation pitch over 0.15 s (55-1000 Hz), mono
            const int a = (int) (t0 * sr), N = (int) (0.15 * sr);
            std::vector<double> x ((size_t) N + 2000);
            for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] = c.getSample (0, a + i) + c.getSample (1, a + i);
            double best = -1; int bestLag = 0;
            for (int lag = (int) (sr / 1000); lag < (int) (sr / 55); ++lag)
            {
                double num = 0, e1 = 0, e2 = 0;
                for (int i = 0; i < N; ++i) { num += x[(size_t) i] * x[(size_t) (i + lag)]; e1 += x[(size_t) i] * x[(size_t) i]; e2 += x[(size_t) (i + lag)] * x[(size_t) (i + lag)]; }
                const double r = num / std::sqrt (e1 * e2 + 1e-20);
                if (r > best + 0.02) { best = r; bestLag = lag; }   // first clear peak wins (avoids octave errors)
            }
            return bestLag > 0 ? sr / bestLag : 0.0;
        };
        auto lvl = [&] (const juce::AudioBuffer<float>& c, double t0, double t1) { double e = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) e += c.getSample (0, i) * c.getSample (0, i); return std::sqrt (e / ((t1 - t0) * sr)); };
        for (int idx = -1; idx < (int) list.size(); ++idx)
        {
            auto p = make(); if (idx >= 0) p->loadFactoryPreset (idx);
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix }) setP (*p, i, 0.0f);
            setP (*p, P_arpOn, 0.0f);
            juce::AudioBuffer<float> c;
            render (*p, 2.0, { { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) }, { 0.8, juce::MidiMessage::pitchWheel (1, 16383) },
                               { 1.9, juce::MidiMessage::noteOff (1, 57) } }, &c);
            const double f0 = pitchOf (c, 0.5), f1 = pitchOf (c, 1.3);
            std::cout << "WHEELS " << (idx < 0 ? juce::String ("<default state>") : juce::String (list[(size_t) idx].name)).paddedRight (' ', 24)
                      << " bend range " << juce::String (p->param (P_bendRange)->convertFrom0to1 (p->param (P_bendRange)->getValue()), 0)
                      << "  pitch " << juce::String (f0, 1) << " -> " << juce::String (f1, 1) << " Hz (x" << juce::String (f1 / std::max (1.0, f0), 3) << ")"
                      << "  level " << juce::String (lvl (c, 0.5, 0.75), 3) << std::endl;
        }
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_VST3", {}).isNotEmpty())
    {
        // Host the BUILT plug-in the way a DAW does: JUCE's VST3 hosting turns CCs and pitch bend into
        // parameter changes through the plug-in's IMidiMapping, just as Ableton and other VST3 hosts do.
        const juce::File bundle (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_VST3", {}));
        juce::VST3PluginFormat fmt;
        juce::OwnedArray<juce::PluginDescription> descs;
        fmt.findAllTypesForFile (descs, bundle.getFullPathName());
        CHECK (descs.size() > 0, "found the VST3");
        if (descs.isEmpty()) return 1;
        juce::String err;
        auto h = fmt.createInstanceFromDescription (*descs[0], sr, 512, err);
        CHECK (h != nullptr, "VST3 instance: " + err);
        if (h == nullptr) return 1;
        h->setPlayConfigDetails (0, 2, sr, 512);
        h->prepareToPlay (sr, 512);
        auto ref = make();   // our own copy, for parameter names and ranges
        auto hostParam = [&] (int idx) -> juce::AudioProcessorParameter*
        {
            const auto name = ref->param (idx)->getName (200);
            for (auto* hp : h->getParameters()) if (hp->getName (200) == name) return hp;
            return nullptr;
        };
        auto hostSet = [&] (int idx, float plain)
        {
            auto* hp = hostParam (idx);
            CHECK (hp != nullptr, "host sees parameter " + juce::String (kParamIds[idx]));
            if (hp != nullptr) hp->setValueNotifyingHost (ref->param (idx)->convertTo0to1 (plain));
        };
        auto hostGet = [&] (int idx) { auto* hp = hostParam (idx); return hp != nullptr ? ref->param (idx)->convertFrom0to1 (hp->getValue()) : -1.0f; };
        // a plain sine on Osc 1, nothing else
        for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain,
                       P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain, P_lfoAssignAmt0, P_ciAmount, P_mutAmount })
            hostSet (i, 0.0f);
        hostSet (P_filterCutoff, 20000.0f); hostSet (P_osc1Wave, 3.0f); hostSet (P_bendRange, 2.0f);
        juce::AudioBuffer<float> out (2, (int) (3.0 * sr)), buf (2, 512);
        std::vector<std::pair<int, juce::MidiMessage>> ev;
        auto runFor = [&] (double secs, std::vector<std::pair<double, juce::MidiMessage>> events)
        {
            const int total = (int) (secs * sr);
            out.setSize (2, total, false, true, true); out.clear();
            for (int pos = 0; pos < total; pos += 512)
            {
                const int n = std::min (512, total - pos);
                buf.setSize (2, n, false, false, true); buf.clear();
                juce::MidiBuffer m;
                for (auto& e : events) { const int at = (int) (e.first * sr); if (at >= pos && at < pos + n) m.addEvent (e.second, at - pos); }
                h->processBlock (buf, m);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
            }
        };
        auto freqIn = [&] (double t0, double t1)
        {
            double first = -1, last = -1; int k = 0;
            for (int i = (int) (t0 * sr) + 1; i < (int) (t1 * sr); ++i)
            {
                const float a = out.getSample (0, i - 1), b = out.getSample (0, i);
                if (a < 0.0f && b >= 0.0f) { const double x = (i - 1 + a / (a - b)) / sr; if (first < 0) first = x; last = x; ++k; }
            }
            return k > 1 ? (k - 1) / (last - first) : 0.0;
        };
        runFor (0.3, {});   // let the parameter changes land
        runFor (2.0, { { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) },
                       { 0.5, juce::MidiMessage::pitchWheel (1, 16383) },
                       { 1.0, juce::MidiMessage::pitchWheel (1, 8192) },
                       { 1.9, juce::MidiMessage::noteOff (1, 57) } });
        const double f0 = freqIn (0.2, 0.45), fUp = freqIn (0.7, 0.95), fBack = freqIn (1.2, 1.45);
        std::cout << "  VST3: held note " << juce::String (f0, 1) << " Hz, bent up " << juce::String (fUp, 1) << " Hz, back " << juce::String (fBack, 1) << " Hz" << std::endl;
        CHECK (std::abs (f0 - 220.0) < 1.0, "VST3: the note plays at 220 Hz");
        CHECK (std::abs (fUp - 220.0 * std::pow (2.0, 2.0 / 12.0)) < 1.5, "VST3: pitch bend up 2 st on a held note");
        CHECK (std::abs (fBack - 220.0) < 1.0, "VST3: bend returns to centre");
        hostSet (P_bendRange, 12.0f);
        runFor (0.2, {});
        runFor (1.0, { { 0.0, juce::MidiMessage::pitchWheel (1, 0) }, { 0.05, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) },
                       { 0.9, juce::MidiMessage::noteOff (1, 57) }, { 0.95, juce::MidiMessage::pitchWheel (1, 8192) } });
        const double fDown = freqIn (0.3, 0.6);
        std::cout << "  VST3: new note with full bend down, range 12: " << juce::String (fDown, 1) << " Hz" << std::endl;
        CHECK (std::abs (fDown - 110.0) < 1.0, "VST3: bend range honoured, and applies to a new note");
        // CC7 reaches the plug-in (volume -> master volume)
        runFor (0.5, { { 0.0, juce::MidiMessage::controllerEvent (1, 7, 0) } });
        runFor (0.3, {});
        CHECK (hostGet (P_masterVolume) < 0.01f, "VST3: CC7 arrives (" + juce::String (hostGet (P_masterVolume), 3) + ")");
        // CC1 reaches the plug-in (mod wheel -> LFO 1 depth, as in the browser synth; Init switches that link off)
        hostSet (P_wheelLfo1, 1.0f); runFor (0.1, {});
        runFor (0.5, { { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) } });
        runFor (0.3, {});
        CHECK (hostGet (P_lfo1Depth) > 0.99f * ref->param (P_lfo1Depth)->getNormalisableRange().end, "VST3: CC1 arrives (LFO 1 depth " + juce::String (hostGet (P_lfo1Depth), 3) + ")");
        // CC64 through the host: a released key keeps sounding until the pedal comes up
        hostSet (P_bendRange, 2.0f); hostSet (P_masterVolume, 0.25f);
        runFor (0.2, {});
        runFor (1.6, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0.05, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) },
                       { 0.2, juce::MidiMessage::noteOff (1, 57) }, { 0.8, juce::MidiMessage::controllerEvent (1, 64, 0) } });
        auto rms = [&] (double t0, double t1) { double e = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) e += out.getSample (0, i) * out.getSample (0, i); return std::sqrt (e / ((t1 - t0) * sr)); };
        std::cout << "  VST3 sustain: held " << rms (0.6, 0.75) << ", after pedal up " << rms (1.4, 1.6) << std::endl;
        CHECK (rms (0.6, 0.75) > 0.01 && rms (1.4, 1.6) < rms (0.6, 0.75) * 0.01, "VST3: sustain pedal");
        h->releaseResources();
        h.reset();
        {
            // a brand-new instance in the host starts from Init, and the mod wheel brings in vibrato
            auto h2 = fmt.createInstanceFromDescription (*descs[0], sr, 512, err);
            h2->setPlayConfigDetails (0, 2, sr, 512); h2->prepareToPlay (sr, 512);
            { std::unique_ptr<juce::AudioProcessorEditor> e2 (h2->createEditorIfNeeded()); }   // no message loop here, so let the editor apply Init
            auto get2 = [&] (int idx) { const auto name = ref->param (idx)->getName (200); for (auto* hp : h2->getParameters()) if (hp->getName (200) == name) return ref->param (idx)->convertFrom0to1 (hp->getValue()); return -1.0f; };
            CHECK (get2 (P_osc2Gain) < 1e-4f && get2 (P_fxOnReverb) < 0.5f && get2 (P_filterCutoff) > 19000.0f, "VST3: a new instance starts at Init");
            for (auto* hp : h2->getParameters()) if (hp->getName (200) == ref->param (P_osc1Wave)->getName (200)) hp->setValueNotifyingHost (ref->param (P_osc1Wave)->convertTo0to1 (3.0f));
            auto run2 = [&] (double secs, std::vector<std::pair<double, juce::MidiMessage>> events)
            {
                const int total = (int) (secs * sr); out.setSize (2, total, false, true, true); out.clear();
                for (int pos = 0; pos < total; pos += 512)
                {
                    const int n = std::min (512, total - pos); buf.setSize (2, n, false, false, true); buf.clear();
                    juce::MidiBuffer m; for (auto& e : events) { const int at = (int) (e.first * sr); if (at >= pos && at < pos + n) m.addEvent (e.second, at - pos); }
                    h2->processBlock (buf, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
                }
            };
            run2 (0.2, {});
            run2 (1.0, { { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) }, { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) }, { 0.99, juce::MidiMessage::noteOff (1, 57) } });
            double lo = 1e9, hi = 0;
            for (double t = 0.3; t < 0.9; t += 0.02) { const double f = freqIn (t, t + 0.03); lo = std::min (lo, f); hi = std::max (hi, f); }
            const double cents = 1200.0 * std::log2 (hi / lo);
            std::cout << "  VST3 Init + mod wheel: " << juce::String (cents, 1) << " cents of vibrato" << std::endl;
            CHECK (cents > 25.0 && cents < 90.0, "VST3: the mod wheel adds vibrato on Init");
            h2->releaseResources();
        }
        std::cout << (failures == 0 ? "VST3 HOST TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_HASHALL", {}).isNotEmpty())
    {
        // a fingerprint of every factory preset's output (DAW at 172 BPM, play pressed mid-bar), plus the worst case, at all three qualities
        struct FakeHead : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        const auto& list = factoryPresets();
        auto hashRun = [&] (std::function<void (MegaSynthProcessor&)> setup, const juce::String& name)
        {
            FakeHead head;
            auto p = std::make_unique<MegaSynthProcessor>();
            p->setPlayConfigDetails (0, 2, sr, 256); p->prepareToPlay (sr, 256); p->setPlayHead (&head);
            setup (*p);
            const double bpm = 172.0, inc = bpm / 60.0 / sr; double ppq = 6.3;
            juce::AudioBuffer<float> buf (2, 256);
            for (int b = 0; b < 400; ++b)   // let the convolution reverbs load their impulses (a background thread) before listening
            {
                head.info.setIsPlaying (false); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                buf.clear(); juce::MidiBuffer none; p->processBlock (buf, none); juce::Thread::sleep (2);
            }
            uint64_t h = 1469598103934665603ull;
            const int total = (int) (2.5 * sr);
            for (int pos = 0; pos < total; pos += 256)
            {
                head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                juce::MidiBuffer m;
                if (pos == 256 * 4) for (int nt : { 41, 53, 60, 65, 68 }) m.addEvent (juce::MidiMessage::noteOn (1, nt, (juce::uint8) 110), 17);
                if (pos == 256 * 300) for (int nt : { 53, 65 }) m.addEvent (juce::MidiMessage::noteOff (1, nt), 3);
                if (pos == 256 * 330) m.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 70), 100);
                buf.clear(); p->processBlock (buf, m); ppq += 256 * inc;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i) { uint32_t b; const float v = buf.getSample (c, i); std::memcpy (&b, &v, 4); h = (h ^ b) * 1099511628211ull; }
            }
            p->setPlayHead (nullptr);
            std::cout << "HASH " << name << " " << juce::String::toHexString ((juce::int64) h) << std::endl;
        };
        // (the voices share one random sequence across instances, so the order matters: the first 32 presets in the
        // order the reference fingerprints were taken, then the presets added since)
        // (in batches, in the order they were added: 32 originals, the three Break presets, Break Frankenstein)
        const int batches[] = { 0, 32, 35, 36, (int) list.size() };   // (... then the jungle / drum & bass bank)
        for (int bt = 0; bt + 1 < (int) (sizeof (batches) / sizeof (batches[0])); ++bt)
            for (int q = 0; q < 3; ++q)
                for (int idx = batches[bt]; idx < batches[bt + 1]; ++idx)
                    hashRun ([&] (MegaSynthProcessor& p) { p.loadFactoryPreset (idx); setP (p, P_quality, (float) q); }, juce::String (q) + ":" + list[(size_t) idx].name);
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BENCH", {}).isNotEmpty())
    {
        // CPU benchmark: every factory preset holding a 6-note chord for 3 s (best of 3), and the worst case (best of 3)
        const auto& list = factoryPresets();
        double total = 0, top = 0; juce::String topName;
        const int runs = juce::jlimit (1, 9, juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BENCH", "3").getIntValue());
        for (int idx = 0; idx < (int) list.size(); ++idx)
        {
            double best = 1e9;
            for (int run = 0; run < runs; ++run)
            {
                auto p = make(); p->loadFactoryPreset (idx);
                render (*p, 0.3, {});
                double cpu = 0; render (*p, 3.0, chord (0.0, 2.5, { 48, 55, 60, 63, 67, 70 }), nullptr, &cpu);
                best = std::min (best, cpu / 3.0 * 100.0);
            }
            total += best; if (best > top) { top = best; topName = list[(size_t) idx].name; }
        }
        double w = 1e9; for (int run = 0; run < runs; ++run) w = std::min (w, worst (1));
        std::cout << "BENCH presets: average " << juce::String (total / (double) list.size(), 2) << "% of a core, heaviest " << topName << " "
                  << juce::String (top, 2) << "%; worst case (Normal) " << juce::String (w, 2) << "%" << std::endl;
        return 0;
    }

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

        // every factory preset goes quiet after the notes stop (no hiss or hum on an idle track)
        {
            const auto& list = factoryPresets();
            for (int i = 0; i < (int) list.size(); ++i)
            {
                auto p = make(); p->loadFactoryPreset (i);
                setP (*p, P_bsOn, 0.0f);   // (a running Break Sequencer keeps playing on its own, which is the point of it)
                render (*p, 0.6, chord (0.0, 0.5, { 48, 55 }));
                render (*p, 12.0, {});
                auto st = render (*p, 0.5, {});
                const double db = 20.0 * std::log10 (st.peak + 1e-12);
                if (db > -90.0) std::cout << "  idle after 12 s: " << list[(size_t) i].name << " " << juce::String (db, 1) << " dBFS" << std::endl;
                CHECK (db < -80.0, juce::String ("goes quiet when idle: ") + list[(size_t) i].name);
            }
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
    // ---- MIDI input: pitch bend range, sustain pedal, mod wheel, learn on any control; the clean Init patch
    auto midiTests = [&]
    {
        std::cout << "MIDI input / Init" << std::endl;
        auto clean = [&]
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc2Gain, P_osc3Gain, P_subGain }) setP (*p, i, 0.0f);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f); setP (*p, P_osc1Wave, 3.0f);
            return p;
        };
        auto freqIn = [&] (const juce::AudioBuffer<float>& c, double t0, double t1)
        {
            double first = -1, last = -1; int k = 0;
            for (int i = (int) (t0 * sr) + 1; i < (int) (t1 * sr); ++i)
            {
                const float a = c.getSample (0, i - 1), b = c.getSample (0, i);
                if (a < 0.0f && b >= 0.0f) { const double x = (i - 1 + a / (a - b)) / sr; if (first < 0) first = x; last = x; ++k; }
            }
            return k > 1 ? (k - 1) / (last - first) : 0.0;
        };
        auto rmsIn = [&] (const juce::AudioBuffer<float>& c, double t0, double t1)
        {
            double e = 0; const int a = (int) (t0 * sr), b = (int) (t1 * sr);
            for (int i = a; i < b; ++i) e += c.getSample (0, i) * c.getSample (0, i) + c.getSample (1, i) * c.getSample (1, i);
            return std::sqrt (e / std::max (1, 2 * (b - a)));
        };
        {
            // changing the bend range bends a note that is already bent
            auto p = clean();
            juce::AudioBuffer<float> a; render (*p, 0.4, { { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) }, { 0.0, juce::MidiMessage::pitchWheel (1, 16383) } }, &a);
            const double f2 = freqIn (a, 0.2, 0.39);
            setP (*p, P_bendRange, 12.0f);
            juce::AudioBuffer<float> b; render (*p, 0.4, {}, &b);
            const double f12 = freqIn (b, 0.2, 0.39);
            std::cout << "  full bend: range 2 " << juce::String (f2, 1) << " Hz, range 12 " << juce::String (f12, 1) << " Hz" << std::endl;
            CHECK (std::abs (f2 - 246.94) < 1.0 && std::abs (f12 - 440.0) < 1.5, "bend range applies to a held, bent note");
        }
        {
            // sustain pedal
            auto run = [&] (bool pedalParam, juce::AudioBuffer<float>& c)
            {
                auto p = clean(); setP (*p, P_sustainPedal, pedalParam ? 1.0f : 0.0f);
                render (*p, 1.6, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0.05, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) },
                                   { 0.2, juce::MidiMessage::noteOff (1, 57) }, { 0.8, juce::MidiMessage::controllerEvent (1, 64, 0) } }, &c);
                return std::move (p);
            };
            juce::AudioBuffer<float> on, off;
            run (true, on); run (false, off);
            const double heldOn = rmsIn (on, 0.6, 0.75), afterOn = rmsIn (on, 1.4, 1.6), heldOff = rmsIn (off, 1.4, 1.6);
            std::cout << "  sustain: held " << heldOn << ", after pedal up " << afterOn << "; pedal switched off, same time: " << heldOff << std::endl;
            CHECK (heldOn > 0.02 && afterOn < heldOn * 0.01, "sustain pedal holds a released key until the pedal comes up");
            CHECK (heldOff < heldOn * 0.01, "Sustain Pedal off: CC64 is ignored");
            // re-striking a sustained key doesn't pile up voices
            auto p = clean();
            std::vector<std::pair<double, juce::MidiMessage>> ev { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) } };
            for (int k = 0; k < 6; ++k) { ev.push_back ({ 0.01 + 0.1 * k, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }); ev.push_back ({ 0.06 + 0.1 * k, juce::MidiMessage::noteOff (1, 60) }); }
            render (*p, 0.7, ev);
            render (*p, 1.5, {});   // the re-struck notes ring out; only the last one is still held by the pedal
            CHECK (p->getEngine().activeVoiceCount() == 1 && p->getEngine().isHeld (60), "re-striking under the pedal: " + juce::String (p->getEngine().activeVoiceCount()) + " voice(s) left");
            // chord memory under the pedal: the whole chord is released with the pedal
            auto q = clean(); setP (*q, P_chdOn, 1.0f); setP (*q, P_chdPreset, 7.0f);
            render (*q, 0.3, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0.01, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) }, { 0.1, juce::MidiMessage::noteOff (1, 48) } });
            int held = 0; for (int j = 0; j < 8; ++j) held += q->getEngine().isHeld (2000 + 48 * 16 + j) ? 1 : 0;
            render (*q, 0.05, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
            int after = 0; for (int j = 0; j < 8; ++j) after += q->getEngine().isHeld (2000 + 48 * 16 + j) ? 1 : 0;
            CHECK (held == 5 && after == 0, "sustained chord: held " + juce::String (held) + ", then released");
            // all notes off clears the pedal
            auto r = clean();
            render (*r, 0.3, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0.01, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) }, { 0.1, juce::MidiMessage::noteOff (1, 50) },
                               { 0.2, juce::MidiMessage::controllerEvent (1, 123, 0) } });
            render (*r, 0.05, { { 0.0, juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100) }, { 0.01, juce::MidiMessage::noteOff (1, 52) } });
            CHECK (! r->getEngine().isHeld (52) && ! r->getEngine().isHeld (50), "All Notes Off lifts the pedal");
        }
        {
            // mod wheel: the legacy LFO 1 Depth link can be switched off; the wheel is always a Mod Matrix source
            auto a = make(); render (*a, 0.02, { { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) } });
            CHECK (a->param (P_lfo1Depth)->getValue() > 0.99f, "wheel -> LFO 1 Depth (as before)");
            auto b = make(); setP (*b, P_wheelLfo1, 0.0f);
            const float before = b->param (P_lfo1Depth)->getValue();
            render (*b, 0.02, { { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) } });
            CHECK (std::abs (b->param (P_lfo1Depth)->getValue() - before) < 1e-6f, "switched off: the wheel leaves LFO 1 Depth alone");
            CHECK (std::abs (b->monWheel.load() - 1.0f) < 1e-6f && b->monCC.load() == 1 && b->monCCCount.load() == 1, "the MIDI monitor saw the wheel");
            render (*b, 0.02, { { 0.0, juce::MidiMessage::pitchWheel (1, 0) } });
            CHECK (b->monBend.load() < -0.99f && b->monBendCount.load() == 1, "the MIDI monitor saw the bend");
        }
        {
            // MIDI learn on a toggle and on a choice
            auto p = make();
            p->midiLearn (P_fxOnReverb); render (*p, 0.02, { { 0.0, juce::MidiMessage::controllerEvent (1, 25, 0) } });
            CHECK (p->ccForParam (P_fxOnReverb) == 25 && p->param (P_fxOnReverb)->getValue() < 0.5f, "toggle learned (off)");
            render (*p, 0.02, { { 0.0, juce::MidiMessage::controllerEvent (1, 25, 127) } });
            CHECK (p->param (P_fxOnReverb)->getValue() > 0.5f, "learned CC switches the toggle on");
            p->midiLearn (P_filterType); render (*p, 0.02, { { 0.0, juce::MidiMessage::controllerEvent (1, 26, 127) } });
            CHECK (p->ccForParam (P_filterType) == 26 && p->param (P_filterType)->getValue() > 0.99f, "choice learned: CC 127 picks the last entry");
            // undo leaves the MIDI map alone
            p->pushHistory ("x"); setP (*p, P_filterCutoff, 300.0f); p->pushHistory ("y"); p->undo();
            CHECK (p->ccForParam (P_fxOnReverb) == 25 && p->ccForParam (P_filterType) == 26, "undo keeps the MIDI map");
            // the editor tags its toggles and choice boxes so right-click can offer MIDI Learn
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            int tagged = 0, toggles = 0, combos = 0;
            std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
            {
                if (c->getProperties().contains ("megaParam"))
                {
                    ++tagged;
                    if (dynamic_cast<juce::ToggleButton*> (c) != nullptr) ++toggles;
                    if (dynamic_cast<juce::ComboBox*> (c) != nullptr) ++combos;
                }
                for (auto* ch : c->getChildren()) walk (ch);
            };
            walk (ed.get());
            std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
            {
                if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
                for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
                return nullptr;
            };
            if (auto* tabs = findTabs (ed.get()))
                for (int t = 0; t < tabs->getNumTabs(); ++t)
                    if (auto* pg = tabs->getTabContentComponent (t); pg != nullptr && pg->getParentComponent() == nullptr) walk (pg);   // pages not on screen
            std::cout << "  learnable toggles " << toggles << ", choice boxes " << combos << std::endl;
            CHECK (toggles >= 20 && combos >= 40, "toggles and choice boxes offer MIDI Learn");
        }
        // ---- Init
        auto initFile = MegaSynthProcessor::getUserInitFile();
        initFile.deleteFile();
        auto fresh = [&]
        {
            MegaSynthProcessor::applyInitOnCreate = true;
            auto p = make();
            p->initOnFirstUse();   // what the first timer tick (or opening the editor) does
            MegaSynthProcessor::applyInitOnCreate = false;
            return p;
        };
        auto plain = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
        {
            auto p = fresh();
            CHECK (p->getPatchName() == "Init", "a new instance is called Init");
            bool clean = plain (*p, P_osc1Gain) > 0.5f && plain (*p, P_fxOnReverb) < 0.5f && plain (*p, P_fxOnDelay) < 0.5f && plain (*p, P_fxOnChorus) < 0.5f;
            for (int i : { P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain, P_complexGain, P_supersawGain, P_mutAmount, P_ciAmount, P_dsOn, P_arpOn, P_chdOn, P_fEnvAmt, P_lfoAssignAmt0, P_envAssignAmt0 })
                clean = clean && std::abs (plain (*p, i)) < 1e-4f;
            for (int k = 0; k < 6; ++k) clean = clean && plain (*p, P_uni1Voices + kUniStride * k) < 1.5f;
            for (int k = 0; k < FS_COUNT; ++k) clean = clean && plain (*p, fxOnParam (k)) < 0.5f;
            CHECK (clean, "Init: Osc 1 only, no effects, no mutation, sequencers or arp");
            // only Osc 1 is heard, and nothing rings on after the release
            juce::AudioBuffer<float> c; render (*p, 1.5, chord (0.0, 0.5, { 57 }), &c);
            const double lvl = rmsIn (c, 0.2, 0.45), tail = rmsIn (c, 1.2, 1.5);
            setP (*p, P_osc1Gain, 0.0f);
            juce::AudioBuffer<float> d; render (*p, 0.6, chord (0.0, 0.5, { 57 }), &d);
            std::cout << "  Init: level " << lvl << ", tail " << tail << ", without Osc 1 " << rmsIn (d, 0.1, 0.45) << std::endl;
            CHECK (lvl > 0.02 && tail < 1e-5 && rmsIn (d, 0.1, 0.45) < 1e-6, "Init: Osc 1 is the only sound and there is no effects tail");
            // the wheel brings in vibrato
            auto v = fresh(); setP (*v, P_osc1Wave, 3.0f);
            auto spread = [&] (const juce::AudioBuffer<float>& b)
            {
                double lo = 1e9, hi = 0;
                for (double t = 0.3; t < 0.9; t += 0.02) { const double f = freqIn (b, t, t + 0.03); lo = std::min (lo, f); hi = std::max (hi, f); }
                return 1200.0 * std::log2 (hi / lo);
            };
            juce::AudioBuffer<float> w0, w1;
            render (*v, 1.0, chord (0.0, 1.0, { 57 }), &w0);
            render (*v, 1.0, { { 0.0, juce::MidiMessage::controllerEvent (1, 1, 127) }, { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) }, { 0.99, juce::MidiMessage::noteOff (1, 57) } }, &w1);
            std::cout << "  Init vibrato: wheel down " << juce::String (spread (w0), 1) << " cents, wheel up " << juce::String (spread (w1), 1) << " cents peak to peak" << std::endl;
            CHECK (spread (w0) < 3.0 && spread (w1) > 25.0 && spread (w1) < 90.0, "Init: the mod wheel adds vibrato");
        }
        {
            // Save as Init, the Init button, Reset to factory
            auto p = fresh(); setP (*p, P_filterCutoff, 900.0f); setP (*p, P_osc2Gain, 0.5f);
            CHECK (p->saveCurrentAsInit() && MegaSynthProcessor::hasUserInit(), "Save as Init");
            auto q = fresh();
            CHECK (std::abs (plain (*q, P_filterCutoff) - 900.0f) < 2.0f && std::abs (plain (*q, P_osc2Gain) - 0.5f) < 0.01f, "a new instance starts from the saved Init");
            auto r = make(); setP (*r, P_filterRes, 9.0f); r->pushHistory ("edit"); r->loadInitPatch();
            CHECK (std::abs (plain (*r, P_filterCutoff) - 900.0f) < 2.0f && plain (*r, P_filterRes) < 2.0f && r->getPatchName() == "Init", "the Init button loads the saved Init");
            r->undo();
            CHECK (std::abs (plain (*r, P_filterRes) - 9.0f) < 0.1f, "Init can be undone");
            CHECK (p->resetInitToFactory() && ! MegaSynthProcessor::hasUserInit(), "Reset Init to factory");
            auto s2 = fresh();
            CHECK (plain (*s2, P_filterCutoff) > 19000.0f && plain (*s2, P_osc2Gain) < 1e-4f, "after the reset: the clean Init again");
        }
        {
            // restoring a project never applies Init: an old project (state from before Init existed) comes back exactly
            auto old = make();   // the old start-up state (bare defaults), with a few edits
            setP (*old, P_filterCutoff, 1234.0f); old->addRoute (MS_Lfo2, P_filterRes, 0.3f);
            juce::MemoryBlock mb; old->getStateInformation (mb);
            MegaSynthProcessor::applyInitOnCreate = true;
            auto p = std::make_unique<MegaSynthProcessor>();
            p->setStateInformation (mb.getData(), (int) mb.getSize());
            p->setPlayConfigDetails (0, 2, sr, 512); p->prepareToPlay (sr, 512); p->initOnFirstUse();
            MegaSynthProcessor::applyInitOnCreate = false;
            CHECK (p->getPatchName() != "Init" || old->getPatchName() == "Init", "a restored project isn't replaced by Init");
            bool same = true; int firstDiff = -1;
            for (int i = 0; i < P_COUNT; ++i)
                if (std::abs (p->param (i)->getValue() - old->param (i)->getValue()) > 1e-6f) { same = false; if (firstDiff < 0) firstDiff = i; }
            CHECK (same, "a restored project gets every parameter back" + (firstDiff >= 0 ? juce::String (" (differs: ") + kParamIds[firstDiff] + ")" : juce::String()));
            CHECK (juce::JSON::toString (p->routes.toVar()) == juce::JSON::toString (old->routes.toVar()), "a restored project gets its routes back (Init's wheel route is gone)");
            juce::MemoryBlock m2; p->getStateInformation (m2);
            CHECK (getXmlFromBinaryForTest (m2) == getXmlFromBinaryForTest (mb), "the restored project saves out identically");
            // and the old start-up sound: a project saved with the defaults renders exactly as a bare instance
            auto a = make(); juce::MemoryBlock m3; a->getStateInformation (m3);
            // as a host loads a project: a new instance, the state, then prepare
            MegaSynthProcessor::applyInitOnCreate = true;
            auto b = std::make_unique<MegaSynthProcessor>();
            b->setStateInformation (m3.getData(), (int) m3.getSize());
            b->setPlayConfigDetails (0, 2, sr, 512); b->prepareToPlay (sr, 512); b->initOnFirstUse();
            MegaSynthProcessor::applyInitOnCreate = false;
            auto c2 = make();
            juce::AudioBuffer<float> x, y;
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) { setP (*b, i, 0.0f); setP (*c2, i, 0.0f); }
            tg::resetVoiceRandom(); render (*b, 1.0, chord (0.0, 0.5, { 48, 55 }), &x);
            tg::resetVoiceRandom(); render (*c2, 1.0, chord (0.0, 0.5, { 48, 55 }), &y);
            double diff = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < x.getNumSamples(); ++i) diff = std::max (diff, (double) std::abs (x.getSample (ch, i) - y.getSample (ch, i)));
            CHECK (diff == 0.0, "an old project on a new instance sounds exactly as before");
        }
        {
            tgui::PatchCatalogue c; c.rescan();
            CHECK (c.size() > 0 && c[0].name == "Init" && c[0].factory && c[0].index < 0 && c[0].category == "Init", "the browser lists Init first");
        }
        initFile.deleteFile();
    };
    // ---- Master Filter and Output trim (state version 12)
    auto masterTests = [&]
    {
        std::cout << "Master Filter / Output" << std::endl;
        // the filter on its own: gain of a steady sine after it settles
        auto gainDb = [&] (tg::MasterFilter::Settings st, double f)
        {
            tg::MasterFilter mf; mf.prepare (sr);
            const int n = (int) (0.5 * sr);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) (0.25 * std::sin (2.0 * kPi * f * i / sr));
            for (int pos = 0; pos < n; pos += 256) mf.process (l.data() + pos, r.data() + pos, std::min (256, n - pos), st);
            double ei = 0, eo = 0;
            for (int i = n / 2; i < n; ++i) { const double x = 0.25 * std::sin (2.0 * kPi * f * i / sr); ei += x * x; eo += (double) l[(size_t) i] * l[(size_t) i]; }
            return 10.0 * std::log10 (eo / ei);
        };
        tg::MasterFilter::Settings st; st.on = true; st.cutoff = 1000.0f; st.res = 0.0f;
        st.type = tg::MasterFilter::LP;
        const double lpPass = gainDb (st, 100.0), lpStop = gainDb (st, 10000.0), lpAt = gainDb (st, 1000.0);
        st.steep = true; const double lp24 = gainDb (st, 10000.0); st.steep = false;
        std::cout << "  LP 1 kHz: 100 Hz " << juce::String (lpPass, 1) << " dB, 1 kHz " << juce::String (lpAt, 1) << " dB, 10 kHz "
                  << juce::String (lpStop, 1) << " dB (12) / " << juce::String (lp24, 1) << " dB (24)" << std::endl;
        CHECK (std::abs (lpPass) < 0.3 && std::abs (lpAt + 3.0) < 0.7 && lpStop < -36.0 && lpStop > -44.0 && lp24 < -72.0, "low pass: 12 and 24 dB/oct");
        st.type = tg::MasterFilter::HP;
        const double hpPass = gainDb (st, 10000.0), hpStop = gainDb (st, 100.0);
        CHECK (std::abs (hpPass) < 0.3 && hpStop < -36.0, "high pass");
        st.type = tg::MasterFilter::BP;
        const double bpC = gainDb (st, 1000.0), bpOff = gainDb (st, 100.0);
        CHECK (std::abs (bpC) < 0.5 && bpOff < -15.0, "band pass: unity at the centre");
        st.type = tg::MasterFilter::Notch;
        const double nC = gainDb (st, 1000.0), nOff = gainDb (st, 100.0);
        CHECK (nC < -30.0 && std::abs (nOff) < 0.5, "notch");
        st.type = tg::MasterFilter::LP; st.res = 1.0f;
        CHECK (gainDb (st, 1000.0) > 15.0, "resonance peaks at the cutoff");
        st.res = 0.0f;
        // the drawn curve matches what the audio does
        for (auto [t, f] : { std::pair<int, double> { 0, 3000.0 }, { 1, 300.0 }, { 2, 2000.0 } })
        {
            st.type = t;
            CHECK (std::abs (tg::MasterFilter::magnitudeDb ((float) f, st, sr) - gainDb (st, f)) < 0.5, "curve matches the sound (type " + juce::String (t) + ")");
        }
        st.type = tg::MasterFilter::LP;
        // Mix 0: the dry signal exactly; Off: untouched
        {
            tg::MasterFilter mf; mf.prepare (sr);
            auto s0 = st; s0.mix = 0.0f; s0.cutoff = 200.0f;
            std::vector<float> l (2048), r (2048), l0, r0;
            for (int i = 0; i < 2048; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (0.01 * i * i);
            l0 = l; r0 = r;
            mf.process (l.data(), r.data(), 2048, s0);
            CHECK (l == l0 && r == r0, "Mix 0: bit for bit the dry signal");
            tg::MasterFilter::Settings off;
            mf.process (l.data(), r.data(), 2048, off);
            CHECK (l == l0, "off: untouched");
        }
        // a fast sweep doesn't click or blow up
        {
            tg::MasterFilter mf; mf.prepare (sr);
            auto sw = st; sw.res = 0.9f; sw.steep = true;
            std::vector<float> l (512), r (512);
            float worst = 0, prev = 0; bool finite = true;
            for (int b = 0; b < 400; ++b)
            {
                sw.cutoff = (b / 10) % 2 == 0 ? 80.0f : 12000.0f;   // jumps every 10 blocks
                for (int i = 0; i < 512; ++i) l[(size_t) i] = r[(size_t) i] = (float) (0.3 * std::sin (2.0 * kPi * 220.0 * (b * 512 + i) / sr));
                mf.process (l.data(), r.data(), 512, sw);
                for (float v : l) { finite &= std::isfinite (v); worst = std::max (worst, std::abs (v - prev)); prev = v; }
            }
            CHECK (finite && worst < 0.5f, "cutoff jumps are smoothed (largest step " + juce::String (worst, 3) + ")");
        }
        // Output trim
        {
            tg::OutputTrim t; t.prepare (sr);
            std::vector<float> l (4800, 0.5f), r (4800, 0.5f);
            t.process (l.data(), r.data(), 4800, 0.0f);
            CHECK (l[4799] == 0.5f, "Output at 0 dB: untouched");
            tg::OutputTrim t2; t2.prepare (sr);
            for (int k = 0; k < 4; ++k) { std::fill (l.begin(), l.end(), 0.5f); std::fill (r.begin(), r.end(), 0.5f); t2.process (l.data(), r.data(), 4800, -6.0f); }
            CHECK (std::abs (l[4799] - 0.5f * std::pow (10.0f, -6.0f / 20.0f)) < 1e-4f, "Output trim -6 dB");
        }
        // every parameter can be a route destination (the route store once held only the first 511)
        {
            tg::RouteStore rs; int bad = -1;
            for (int i = 0; i < P_COUNT && bad < 0; ++i)
                for (int src : { 1, MS_COUNT - 1 })
                {
                    RouteConfig c; c.on = true; c.src = src; c.dst = i; c.via = MS_COUNT - 1; c.curve = 3; c.unipolar = true;
                    rs.set (0, c); const auto g = rs.get (0);
                    if (g.dst != i || g.src != src || g.via != MS_COUNT - 1 || g.curve != 3 || ! g.unipolar || ! g.on) bad = i;
                }
            CHECK (bad < 0, "every parameter round-trips as a route destination" + (bad >= 0 ? juce::String (": ") + kParamIds[bad] : juce::String()));
            auto q = make(); const int slot = q->addRoute (MS_Macro1, P_pEnvAmt, 0.5f);
            CHECK (q->routes.get (slot).dst == P_pEnvAmt, "a route to the pitch envelope lands on the pitch envelope");
        }
        // in the synth: after everything, on every voice together; modulated by a macro; saved
        {
            auto run = [&] (std::function<void (MegaSynthProcessor&)> setup)
            {
                auto p = make();
                for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) setP (*p, i, 0.0f);
                setup (*p);
                juce::AudioBuffer<float> c; tg::resetVoiceRandom(); render (*p, 0.6, chord (0.0, 0.5, { 48, 55, 64 }), &c);
                double hf = 0, e = 0; float prev = 0;
                for (int i = (int) (0.2 * sr); i < (int) (0.45 * sr); ++i) { const float x = c.getSample (0, i); hf += (x - prev) * (x - prev); e += x * x; prev = x; }
                return std::make_pair (hf / std::max (1e-12, e), std::sqrt (e / (0.25 * sr)));
            };
            const auto plainR = run ([] (MegaSynthProcessor&) {});
            const auto lp = run ([&] (MegaSynthProcessor& q) { setP (q, P_mfOn, 1.0f); setP (q, P_mfCutoff, 300.0f); });
            const auto lpMacro = run ([&] (MegaSynthProcessor& q) { setP (q, P_mfOn, 1.0f); setP (q, P_mfCutoff, 300.0f); q.addRoute (MS_Macro1, P_mfCutoff, 0.8f); setP (q, P_macro1, 1.0f); });
            const auto quiet = run ([&] (MegaSynthProcessor& q) { setP (q, P_outGain, -12.0f); });
            std::cout << "  in the synth: brightness " << plainR.first << " -> " << lp.first << " (LP 300 Hz) -> " << lpMacro.first << " (macro opens it)" << std::endl;
            CHECK (lp.first < plainR.first * 0.2 && lpMacro.first > lp.first * 3.0, "the Master Filter darkens the whole synth; a macro route moves it");
            CHECK (std::abs (20.0 * std::log10 (quiet.second / plainR.second) + 12.0) < 0.5, "Output -12 dB");
            CHECK (tg::meta (P_mfCutoff).modulatable && tg::meta (P_outGain).modulatable && tg::meta (P_mfCutoff).group == MutGroup::None, "Mod Matrix destinations, not mutated");
            auto p = make(); setP (*p, P_mfOn, 1.0f); setP (*p, P_mfType, 1.0f); setP (*p, P_mfCutoff, 640.0f); setP (*p, P_outGain, -3.0f);
            auto plain = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (plain (*r, P_mfOn) > 0.5f && std::abs (plain (*r, P_mfCutoff) - 640.0f) < 1.0f && std::abs (plain (*r, P_outGain) + 3.0f) < 0.05f, "saved in patches");
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (plain (*q, P_mfOn) > 0.5f && std::abs (plain (*q, P_mfCutoff) - 640.0f) < 1.0f, "saved in projects");
            auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"11\"");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
            auto o = make(); o->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (plain (*o, P_mfOn) < 0.5f && std::abs (plain (*o, P_outGain)) < 1e-4f, "old projects: no Master Filter, Output 0 dB");
            // an old patch file loaded into an Init instance gets the old wheel behaviour and no Master Filter
            auto patchJson = juce::JSON::parse (p->exportBrowserPatch());
            if (auto* extra = patchJson["plugin"].getDynamicObject())
                for (auto id : { "mfOn", "mfType", "mfSlope", "mfCutoff", "mfRes", "mfDrive", "mfMix", "outGain", "wheelLfo1", "sustainPedal" }) extra->removeProperty (id);
            auto n2 = make(); setP (*n2, P_wheelLfo1, 0.0f); setP (*n2, P_mfOn, 1.0f);
            n2->importBrowserPatch (juce::JSON::toString (patchJson));
            CHECK (plain (*n2, P_wheelLfo1) > 0.5f && plain (*n2, P_mfOn) < 0.5f, "old patches: wheel to LFO 1 on, Master Filter off");
        }
        // ---- the one Filter list, and the Master Filter's models
        {
            auto plain = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
            // every model, type and slope on the Master Filter: stable (finite, bounded) at every cutoff and resonance, swept
            juce::StringArray bad;
            juce::Random rnd (7);
            std::vector<float> noise (8192); for (auto& v : noise) v = rnd.nextFloat() * 2.0f - 1.0f;
            double worstMs = 0.0; juce::String worstName;
            for (int model = 0; model <= 17; ++model)
                for (int cm = 0; cm < (model == 0 ? (int) tg::CM_COUNT : 1); ++cm)
                    for (int type = 0; type < 4; ++type)
                        for (int steep = 0; steep < 2; ++steep)
                        {
                            if (cm > 0 && (type > 0 || steep > 0)) continue;
                            for (float res : { 0.0f, 0.5f, 1.0f })
                            {
                                tg::MasterFilter mf; mf.prepare (sr);
                                tg::MasterFilter::Settings st; st.on = true; st.model = model; st.charModel = cm; st.type = type; st.steep = steep == 1; st.res = res; st.driveDb = 12.0f;
                                std::vector<float> l (noise), r (noise);
                                for (int i = 0; i < (int) r.size(); ++i) r[(size_t) i] *= -0.7f;   // (stereo: different left and right)
                                float pk = 0.0f; bool finite = true;
                                const auto t0 = juce::Time::getHighResolutionTicks();
                                for (int pos = 0, b = 0; pos < (int) l.size(); pos += 128, ++b)
                                {
                                    st.cutoff = 20.0f * std::pow (1000.0f, 0.5f + 0.5f * std::sin ((float) b * 0.37f));   // 20 Hz .. 20 kHz and back
                                    mf.process (l.data() + pos, r.data() + pos, 128, st);
                                }
                                const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0;
                                for (size_t i = 0; i < l.size(); ++i) { finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]); pk = std::max (pk, std::max (std::abs (l[i]), std::abs (r[i]))); }
                                const juce::String name = cm > 0 ? juce::String (tg::charModelInfo (cm).name) : juce::String (kMfModelLabels[model]) + " " + kMfTypeLabels[type] + (steep ? " 24" : " 12");
                                if (! finite || pk > 12.0f) bad.add (name + " res " + juce::String (res) + " (peak " + juce::String (pk, 1) + ")");
                                if (ms > worstMs && ! (model == 0 && cm == 0 && type == 0 && steep == 0 && res == 0.0f)) { worstMs = ms; worstName = name; }   // (the first run warms the caches)
                            }
                        }
            CHECK (bad.isEmpty(), "every Master Filter model, type and slope is stable when swept, at every resonance" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
            std::cout << "  CPU, the heaviest Master Filter model (" << worstName << ", stereo): " << juce::String (100.0 * worstMs / 1000.0 / (8192.0 / sr), 2) << "% of a core" << std::endl;
            // a classic model on the Master Filter sounds like it does on Filter 1: a Ladder low pass at 1 kHz cuts 4 kHz hard and keeps 100 Hz
            tg::MasterFilter::Settings ms; ms.on = true; ms.model = 3; ms.type = tg::MasterFilter::LP; ms.cutoff = 1000.0f; ms.res = 0.0f;
            CHECK (gainDb (ms, 4000.0) < -15.0 && std::abs (gainDb (ms, 100.0)) < 4.0, "a classic model (Ladder) on the Master Filter: " + juce::String (gainDb (ms, 4000.0), 1) + " dB at 4 kHz, "
                   + juce::String (gainDb (ms, 100.0), 1) + " dB at 100 Hz");
            ms.type = tg::MasterFilter::HP;
            CHECK (gainDb (ms, 100.0) < -15.0, "... and as a high pass");
            // the greyed controls are the ones a filter doesn't use: Resampler ignores Resonance, DJ Filter ignores Cutoff
            auto renderMf = [&] (tg::MasterFilter::Settings st2)
            {
                tg::MasterFilter mf; mf.prepare (sr);
                std::vector<float> l (noise), r (noise);
                for (int pos = 0; pos < (int) l.size(); pos += 256) mf.process (l.data() + pos, r.data() + pos, 256, st2);
                return l;
            };
            tg::MasterFilter::Settings rs; rs.on = true; rs.charModel = tg::CM_Resampler; rs.cutoff = 3000.0f; rs.res = 0.0f;
            auto a = renderMf (rs); rs.res = 1.0f; auto b = renderMf (rs);
            // (after the 10 ms crossfade in from the clean filter, which does use them)
            float d1 = 0; for (size_t i = 2048; i < a.size(); ++i) d1 = std::max (d1, std::abs (a[i] - b[i]));
            tg::MasterFilter::Settings dj; dj.on = true; dj.charModel = tg::CM_DJ; dj.cutoff = 300.0f; dj.charAmt = 0.2f;
            auto c = renderMf (dj); dj.cutoff = 9000.0f; auto e = renderMf (dj);
            float d2 = 0; for (size_t i = 2048; i < c.size(); ++i) d2 = std::max (d2, std::abs (c[i] - e[i]));
            CHECK (d1 < 1e-6f && d2 < 1e-6f && ! tg::charModelInfo (tg::CM_Resampler).usesRes, "greyed controls: Resampler ignores Resonance (" + juce::String (d1) + "), DJ Filter ignores Cutoff (" + juce::String (d2) + ")");
            if (false) CHECK (! tg::charModelInfo (tg::CM_Resampler).usesRes, "greyed controls: Resampler ignores Resonance, DJ Filter ignores Cutoff");
            // the drop-downs: one list writes the model and the character together
            auto p = make();
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            ed->setSize (1200, 890);
            std::vector<tgui::FilterPicker*> pickers;
            std::function<void (juce::Component*)> walk = [&] (juce::Component* comp) { if (auto* fp = dynamic_cast<tgui::FilterPicker*> (comp)) pickers.push_back (fp); for (auto* ch : comp->getChildren()) walk (ch); };
            if (auto* me = dynamic_cast<MegaSynthEditor*> (ed.get())) { me->showPage ("Filters"); me->timerCallbackForTests(); }
            walk (ed.get());
            tgui::FilterPicker *f1 = nullptr, *mfp = nullptr;
            for (auto* fp : pickers) { const int mp = (int) fp->getProperties()["megaParam"]; if (mp == P_filterMode) f1 = fp; if (mp == P_mfModel) mfp = fp; }
            CHECK (f1 != nullptr && mfp != nullptr && pickers.size() == 3, "one Filter list each for Filter 1, Filter 2 and the Master Filter (" + juce::String ((int) pickers.size()) + ")");
            if (f1 != nullptr && mfp != nullptr)
            {
                CHECK (f1->getNumItems() == 17 + 18 && mfp->getNumItems() == 18 + 18, "the lists hold every model and every character model");
                f1->setSelectedId (200 + tg::CM_Comb, juce::sendNotificationSync);
                CHECK (juce::roundToInt (plain (*p, P_flt1Char)) == tg::CM_Comb && juce::roundToInt (plain (*p, P_filterMode)) == 0, "choosing a character model sets Character (the model stays)");
                f1->setSelectedId (100 + 9, juce::sendNotificationSync);
                CHECK (juce::roundToInt (plain (*p, P_flt1Char)) == 0 && juce::roundToInt (plain (*p, P_filterMode)) == 9 && juce::roundToInt (plain (*p, P_filterSlope)) == nativeFilterSlope (9),
                       "choosing a model: Character off, the model at its own slope");
                mfp->setSelectedId (100 + 12, juce::sendNotificationSync);
                CHECK (juce::roundToInt (plain (*p, P_mfModel)) == 13 && juce::roundToInt (plain (*p, P_mfChar)) == 0, "the Master Filter's list: a classic model");
                mfp->setSelectedId (1, juce::sendNotificationSync);
                CHECK (juce::roundToInt (plain (*p, P_mfModel)) == 0, "... and Clean (SVF), as before");
                // every old (model, character) pair shows as one entry
                bool allShown = true;
                for (int m = 0; m < 17; ++m)
                    for (int cmd = 0; cmd < (int) tg::CM_COUNT; ++cmd)
                    {
                        setP (*p, P_filterMode, (float) m); setP (*p, P_flt1Char, (float) cmd);
                        f1->sync();
                        allShown = allShown && f1->getSelectedId() == tgui::FilterPicker::idFor (false, m, cmd) && f1->indexOfItemId (f1->getSelectedId()) >= 0;
                    }
                CHECK (allShown, "every old (model, character) pair shows as its one entry in the list");
            }
            // old projects: the Master Filter keeps its clean filter
            setP (*p, P_mfModel, 5.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto x = juce::parseXML (getXmlFromBinaryForTest (mb));
            if (x != nullptr)
            {
                x->setAttribute ("stateVersion", 20);
                if (auto* el = x->getChildByAttribute ("id", "mfModel")) x->removeChildElement (el, true);
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*x, old);
                auto o = make(); setP (*o, P_mfModel, 7.0f); o->setStateInformation (old.getData(), (int) old.getSize());
                CHECK (juce::roundToInt (plain (*o, P_mfModel)) == 0, "older projects: the Master Filter stays Clean");
            }
        }
        // ---- routing: the sub and the breaks past the effects
        {
            auto plain = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
            auto rmsOf = [&] (const juce::AudioBuffer<float>& b, double t0, double t1)
            {
                double a = 0; int n = 0;
                for (int i = (int) (t0 * sr); i < std::min (b.getNumSamples(), (int) (t1 * sr)); ++i, ++n) a += (double) b.getSample (0, i) * b.getSample (0, i);
                return std::sqrt (a / std::max (1, n));
            };
            auto subOnly = [&] (int route)
            {
                auto p = make();
                for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_complexGain, P_supersawGain, P_osc4Gain, P_wt2Gain, P_ringGain }) setP (*p, i, 0.0f);
                setP (*p, P_subGain, 0.6f); setP (*p, P_subWave, 1.0f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.005f); setP (*p, P_ampA, 0.001f);
                setP (*p, P_delayMix, 1.0f); setP (*p, P_delayFeedback, 0.6f); setP (*p, P_reverbMix, 1.0f);
                setP (*p, P_subRoute, (float) route);
                return p;
            };
            {
                juce::AudioBuffer<float> a, b;
                auto n = subOnly (0); render (*n, 1.6, chord (0.0, 0.3, { 48 }), &a);
                auto d = subOnly (1); render (*d, 1.6, chord (0.0, 0.3, { 48 }), &b);
                const double tailN = rmsOf (a, 0.6, 1.6), tailD = rmsOf (b, 0.6, 1.6), onD = rmsOf (b, 0.05, 0.25);
                CHECK (tailN > 1.0e-4 && tailD < 1.0e-6 && onD > 0.02, "Sub dry: a 100% delay and reverb hear none of it (tail " + juce::String (tailD, 8) + " vs " + juce::String (tailN, 5) + ")");
            }
            {
                // the voice filter (a high pass at 20 kHz): the normal sub is gone; the dry sub isn't
                juce::AudioBuffer<float> a, b;
                auto n = subOnly (0); setP (*n, P_filterType, 1.0f); setP (*n, P_filterCutoff, 20000.0f); setP (*n, P_fEnvAmt, 0.0f); setP (*n, P_delayMix, 0.0f); setP (*n, P_reverbMix, 0.0f); render (*n, 0.3, chord (0.0, 0.3, { 60 }), &a);
                auto d = subOnly (1); setP (*d, P_filterType, 1.0f); setP (*d, P_filterCutoff, 20000.0f); setP (*d, P_fEnvAmt, 0.0f); render (*d, 0.3, chord (0.0, 0.3, { 60 }), &b);
                CHECK (rmsOf (b, 0.05, 0.25) > 10.0 * rmsOf (a, 0.05, 0.25), "the dry sub skips the voice filter (" + juce::String (rmsOf (b, 0.05, 0.25), 5) + " / " + juce::String (rmsOf (a, 0.05, 0.25), 5) + ")");
                // the Master Filter: closed, it silences "before", not "after"
                juce::AudioBuffer<float> c, e;
                auto pre = subOnly (1); auto post = subOnly (2);
                for (auto* q : { pre.get(), post.get() }) { setP (*q, P_mfOn, 1.0f); setP (*q, P_mfCutoff, 20.0f); setP (*q, P_mfType, 0.0f); setP (*q, P_mfSlope, 1.0f); }
                render (*pre, 0.3, chord (0.0, 0.3, { 60 }), &c); render (*post, 0.3, chord (0.0, 0.3, { 60 }), &e);
                CHECK (rmsOf (e, 0.05, 0.25) > 0.02 && rmsOf (c, 0.05, 0.25) < 0.1 * rmsOf (e, 0.05, 0.25), "Sub dry after the Master Filter: a closed Master Filter doesn't touch it");
                // Master Volume still applies
                juce::AudioBuffer<float> f;
                setP (*post, P_masterVolume, 0.0f); render (*post, 0.3, chord (0.0, 0.3, { 60 }), &f);
                CHECK (rmsOf (f, 0.2, 0.3) < 1.0e-5, "the dry sub follows Master Volume (" + juce::String (rmsOf (f, 0.2, 0.3), 7) + ", was " + juce::String (rmsOf (e, 0.05, 0.25), 4) + ")");
            }
            {
                // Sub Mono: a dry sub panned hard left, below 120 Hz, comes out of both sides equally
                auto chan = [&] (bool mono)
                {
                    auto p = subOnly (1); setP (*p, P_mxSubPan, -1.0f); setP (*p, P_subMono, mono ? 1.0f : 0.0f);
                    juce::AudioBuffer<float> b; render (*p, 0.4, chord (0.0, 0.4, { 36 }), &b);   // (a sine sub at 33 Hz)
                    double l = 0, r = 0;
                    for (int i = (int) (0.1 * sr); i < (int) (0.35 * sr); ++i) { l += (double) b.getSample (0, i) * b.getSample (0, i); r += (double) b.getSample (1, i) * b.getSample (1, i); }
                    return std::make_pair (std::sqrt (l), std::sqrt (r));
                };
                const auto off = chan (false), on = chan (true);
                CHECK (off.second < 0.01 * off.first && std::abs (on.first - on.second) < 0.1 * on.first, "Sub Mono: a hard-panned 33 Hz dry sub comes out of both sides (L/R "
                       + juce::String (on.first, 2) + " / " + juce::String (on.second, 2) + ")");
            }
            {
                // switching the route while it plays: no click (a 10 ms crossfade)
                auto p = subOnly (0);
                setP (*p, P_delayMix, 0.0f); setP (*p, P_reverbMix, 0.0f); setP (*p, P_chorusMix, 0.0f); setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_fEnvAmt, 0.0f);
                juce::AudioBuffer<float> a, b;
                render (*p, 0.25, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) } }, &a);
                setP (*p, P_subRoute, 2.0f);
                render (*p, 0.25, {}, &b);
                float stepIn = 0, stepAt = 0;
                for (int i = 1; i < a.getNumSamples(); ++i) if (i > (int) (0.05 * sr)) stepIn = std::max (stepIn, std::abs (a.getSample (0, i) - a.getSample (0, i - 1)));
                for (int i = 1; i < (int) (0.03 * sr); ++i) stepAt = std::max (stepAt, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
                stepAt = std::max (stepAt, std::abs (b.getSample (0, 0) - a.getSample (0, a.getNumSamples() - 1)));
                CHECK (stepIn > 0.0f && stepAt < 1.6f * stepIn, "switching the Sub Route while it plays doesn't click (" + juce::String (stepAt / std::max (1e-9f, stepIn), 2) + "x the largest step)");
            }
            {
                // with the Spectral stage on (one frame of latency), the dry sub stays in time with the rest
                auto onset = [&] (bool sub)
                {
                    auto p = subOnly (sub ? 1 : 0);
                    setP (*p, P_delayMix, 0.0f); setP (*p, P_reverbMix, 0.0f); setP (*p, P_chorusMix, 0.0f); setP (*p, P_spOn, 1.0f); setP (*p, P_spMix, 0.0f);
                    if (! sub) { setP (*p, P_subGain, 0.0f); setP (*p, P_osc1Gain, 0.6f); setP (*p, P_osc1Wave, 3.0f); setP (*p, P_filterCutoff, 20000.0f); }
                    juce::AudioBuffer<float> b; render (*p, 0.5, chord (0.05, 0.4, { 60 }), &b);
                    for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 1.0e-3f) return i;
                    return -1;
                };
                const int a = onset (false), b = onset (true);
                CHECK (a > (int) (0.05 * sr) + 500 && std::abs (a - b) <= 24, "with Spectral on, the dry sub is delayed to match (" + juce::String (a) + " / " + juce::String (b) + ")");
            }
            {
                // the breaks: past the effects, and past the Master Filter
                auto brk = [&] (int route, bool mfClosed)
                {
                    auto p = make();
                    for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain }) setP (*p, i, 0.0f);
                    setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
                    p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "demo.wav", MegaSynthProcessor::kSlotBreak);
                    setP (*p, P_delayMix, 1.0f); setP (*p, P_delayFeedback, 0.6f); setP (*p, P_reverbMix, 1.0f);
                    setP (*p, P_bcFxRoute, (float) route);
                    if (mfClosed) { setP (*p, P_mfOn, 1.0f); setP (*p, P_mfCutoff, 20.0f); }
                    juce::AudioBuffer<float> b; render (*p, 1.6, chord (0.0, 0.05, { 36 }), &b);
                    if (std::getenv ("BRKDBG")) { std::cout << "   route " << route << ":"; for (int w = 0; w < 16; ++w) std::cout << " " << juce::String (rmsOf (b, w * 0.1, w * 0.1 + 0.1), 6); std::cout << std::endl; }
                    return std::make_pair (rmsOf (b, 0.02, 0.08), rmsOf (b, 0.7, 1.6));
                };
                const auto r0 = brk (0, false), r1 = brk (1, false), r2 = brk (2, false), r3 = brk (3, false);
                CHECK (r0.second > 1.0e-4 && r1.second < 1.0e-6 && r2.second < 1.0e-6 && r3.second < 1.0e-6 && r1.first > 0.01 && r2.first > 0.01 && r3.first > 0.01,
                       "Break Effects: past the effects, the delay and reverb hear nothing of the break (tails " + juce::String (r1.second, 7) + ", " + juce::String (r0.second, 4) + " through)");
                const auto c2 = brk (2, true), c3 = brk (3, true);
                CHECK (c3.first > 0.01 && c2.first < 0.25 * c3.first, "Break Effects: 'after everything' skips the Master Filter, 'into the Master Filter' doesn't (" + juce::String (c2.first, 5) + " / " + juce::String (c3.first, 5) + ")");
                std::cout << "  break routes, level / tail: " << r0.first << " " << r0.second << " | " << r1.first << " " << r1.second << " | " << r2.first << " " << r2.second << " | " << r3.first << " " << r3.second << std::endl;
            }
            {
                // CPU: 8 notes with the sub dry vs normal, and the break dry vs through (each at Eco / Normal / High)
                juce::String line;
                for (int q = 0; q < 3; ++q)
                {
                    double t[2] {};
                    for (int route = 0; route < 2; ++route)
                    {
                        auto p = subOnly (route); setP (*p, P_quality, (float) q); setP (*p, P_osc1Gain, 0.5f);
                        render (*p, 3.0, chord (0.0, 3.0, { 48, 52, 55, 59, 62, 64, 67, 71 }), nullptr, &t[route]);
                    }
                    line << (q == 0 ? "Eco " : q == 1 ? "Normal " : "High ") << juce::String (100.0 * t[0] / 3.0, 2) << "% -> " << juce::String (100.0 * t[1] / 3.0, 2) << "%  ";
                }
                std::cout << "  CPU, 8 notes, sub normal -> dry (% of a core): " << line << std::endl;
            }
            {
                // ---- mod depth with the mouse (Serum-style)
                auto p = make();
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                ed->setSize (1200, 890);
                auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
                e->showPage ("Filters");
                tgui::Knob* k = nullptr; tgui::Knob* admin = nullptr;
                std::function<void (juce::Component*)> walk = [&] (juce::Component* comp)
                {
                    if (auto* kn = dynamic_cast<tgui::Knob*> (comp)) { if (kn->paramIndex == P_filterCutoff && kn->isVisible() && k == nullptr) k = kn; if (kn->paramIndex == P_bcKeyBase) admin = kn; }
                    for (auto* ch : comp->getChildren()) walk (ch);
                };
                walk (ed.get());
                CHECK (k != nullptr, "the Cutoff knob");
                if (k != nullptr)
                {
                    auto amt = [&] (int slot) { return p->param (P_mod1Amt + slot)->convertFrom0to1 (p->param (P_mod1Amt + slot)->getValue()); };
                    auto src = juce::Desktop::getInstance().getMainMouseSource();
                    auto ev = [&] (juce::Point<float> knobPt, juce::ModifierKeys mods, int clicks = 1)
                    {
                        const auto sp = knobPt - k->slider.getPosition().toFloat();
                        return juce::MouseEvent (src, sp, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &k->slider, &k->slider, juce::Time::getCurrentTime(), sp, juce::Time::getCurrentTime(), clicks, false);
                    };
                    const juce::ModifierKeys lb (juce::ModifierKeys::leftButtonModifier);
                    const int s1 = p->addRoute (MS_Lfo1, P_filterCutoff, 0.25f);
                    k->updateMod();
                    CHECK (k->routeSlots().size() == 1 && k->selectedRoute() == s1, "the knob knows its route");
                    // drag the arc up 30 px: +0.2
                    p->pushHistory ("before");
                    const auto at = k->ringPointForTests (0, 0.95f);
                    k->slider.mouseDown (ev (at, lb));
                    k->slider.mouseDrag (ev (at.translated (0, -30), lb));
                    k->slider.mouseUp (ev (at.translated (0, -30), {}));
                    const double v0 = k->slider.getValue();
                    CHECK (std::abs (amt (s1) - 0.45f) < 0.002f, "dragging the arc up sets the depth (" + juce::String (amt (s1), 3) + ")");
                    p->pushHistory ("Edit");
                    p->undo();
                    CHECK (std::abs (amt (s1) - 0.25f) < 0.002f, "one Undo puts the depth back");
                    p->redo();
                    // Shift: fine
                    k->slider.mouseDown (ev (at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)));
                    k->slider.mouseDrag (ev (at.translated (0, -30), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)));
                    k->slider.mouseUp (ev (at.translated (0, -30), {}));
                    CHECK (std::abs (amt (s1) - 0.47f) < 0.002f, "Shift: fine steps (" + juce::String (amt (s1), 3) + ")");
                    // the middle of the knob: the value, not the depth
                    const auto mid = k->ringPointForTests (0, 0.5f) + (k->slider.getBounds().getCentre().toFloat() - k->ringPointForTests (0, 0.5f)) * 1.0f;
                    const float before = amt (s1);
                    k->slider.mouseDown (ev (mid, lb)); k->slider.mouseDrag (ev (mid.translated (0, -25), lb)); k->slider.mouseUp (ev (mid.translated (0, -25), {}));
                    CHECK (std::abs (amt (s1) - before) < 1e-6f && std::abs (k->slider.getValue() - v0) > 1.0, "dragging the knob itself moves its value, not the depth");
                    // Ctrl / Cmd anywhere: the depth
                    k->slider.mouseDown (ev (mid, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier)));
                    k->slider.mouseDrag (ev (mid.translated (0, 15), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier)));
                    k->slider.mouseUp (ev (mid.translated (0, 15), {}));
                    CHECK (std::abs (amt (s1) - (before - 0.1f)) < 0.002f, "Ctrl / Cmd-drag anywhere sets the depth");
                    // double-click the arc: depth 0
                    k->slider.mouseDoubleClick (ev (at, lb, 2));
                    CHECK (std::abs (amt (s1)) < 1e-6f, "double-click an arc: depth 0");
                    // a second route: the chip picks which one the mouse edits; the inner arc picks its own
                    const int s2 = p->addRoute (MS_Lfo2, P_filterCutoff, -0.3f);
                    k->updateMod();
                    CHECK (k->routeSlots().size() == 2 && k->selectedRoute() == s2, "a new route is the one edited");
                    k->mouseDown (juce::MouseEvent (src, { 5.0f, 5.0f }, lb, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, k, k, juce::Time::getCurrentTime(), { 5.0f, 5.0f }, juce::Time::getCurrentTime(), 1, false));
                    CHECK (k->selectedRoute() == s1, "the chip goes round the routes");
                    const auto inner = k->ringPointForTests (1, 0.95f);
                    k->slider.mouseDown (ev (inner, lb)); k->slider.mouseDrag (ev (inner.translated (0, -15), lb)); k->slider.mouseUp (ev (inner.translated (0, -15), {}));
                    CHECK (k->selectedRoute() == s2 && std::abs (amt (s2) - (-0.2f)) < 0.002f && std::abs (amt (s1)) < 1e-6f, "the inner arc edits the second route");
                    // Alt-click an arc: the route goes
                    k->slider.mouseDown (ev (inner, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier)));
                    k->slider.mouseUp (ev (inner, {}));
                    k->updateMod();
                    CHECK (! p->routes.get (s2).active() && k->routeSlots().size() == 1, "Alt-click an arc removes its route");
                    // drag a source onto the knob: a new route at 25%
                    juce::DragAndDropTarget::SourceDetails dd (juce::var ("modsrc:" + juce::String ((int) MS_ModEnv1)), nullptr, {});
                    CHECK (k->isInterestedInDragSource (dd) && (admin == nullptr || ! admin->isInterestedInDragSource (dd)), "knobs that can be modulated take a dropped source (others don't)");
                    k->itemDropped (dd);
                    bool found = false;
                    for (int i = 0; i < kNumRoutes; ++i) { const auto c = p->routes.get (i); if (c.active() && c.src == MS_ModEnv1 && c.dst == P_filterCutoff) found = std::abs (amt (i) - 0.25f) < 1e-4f && k->selectedRoute() == i; }
                    CHECK (found, "dropping Mod Env 1 on the knob makes a route at 25%, ready to edit");
                }
            }
            {
                // older projects: both routes as they always were
                auto p = make(); setP (*p, P_subRoute, 2.0f); setP (*p, P_bcFxRoute, 3.0f);
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto x = juce::parseXML (getXmlFromBinaryForTest (mb));
                if (x != nullptr)
                {
                    x->setAttribute ("stateVersion", 21);
                    for (auto* id : { "subRoute", "bcFxRoute", "subMono" }) if (auto* el = x->getChildByAttribute ("id", id)) x->removeChildElement (el, true);
                    juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*x, old);
                    auto o = make(); setP (*o, P_subRoute, 1.0f); setP (*o, P_bcFxRoute, 2.0f); o->setStateInformation (old.getData(), (int) old.getSize());
                    CHECK (plain (*o, P_subRoute) < 0.5f && plain (*o, P_bcFxRoute) < 0.5f, "older projects: the sub and the breaks go through the effects, as before");
                }
                juce::MemoryBlock mb2; p->getStateInformation (mb2);
                auto q = make(); q->setStateInformation (mb2.getData(), (int) mb2.getSize());
                CHECK (juce::roundToInt (plain (*q, P_subRoute)) == 2 && juce::roundToInt (plain (*q, P_bcFxRoute)) == 3, "the routes are saved");
            }
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_MFCPU", {}).isNotEmpty())
    {
        auto cost = [&] (bool on)
        {
            double best = 1e9;
            for (int run = 0; run < 5; ++run)
            {
                auto p = make(); for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) setP (*p, i, 0.0f);
                if (on) { setP (*p, P_mfOn, 1.0f); setP (*p, P_mfSlope, 1.0f); setP (*p, P_mfDrive, 6.0f); setP (*p, P_outGain, -3.0f); }
                double cpu = 0; render (*p, 3.0, chord (0.0, 2.9, { 48, 55, 60, 63, 67, 70 }), nullptr, &cpu);
                best = std::min (best, cpu / 3.0 * 100.0);
            }
            return best;
        };
        const double off = cost (false), on = cost (true);
        std::cout << "MFCPU off " << off << "% on (24 dB + drive + output) " << on << "% -> " << (on - off) << "% of a core" << std::endl;
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_MASTERONLY", {}).isNotEmpty())
    {
        masterTests();
        std::cout << (failures == 0 ? "MASTER TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- Mixer channel strips: pan, mute, solo; meters
    auto mixerTests = [&]
    {
        std::cout << "Mixer strips" << std::endl;
        auto clean = [&]
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain })
                setP (*p, i, 0.0f);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f); setP (*p, P_osc1Wave, 3.0f); setP (*p, P_osc2Wave, 3.0f);
            return p;
        };
        auto rms = [&] (const juce::AudioBuffer<float>& c, int ch, double t0, double t1) { double e = 0; for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i) e += c.getSample (ch, i) * c.getSample (ch, i); return std::sqrt (e / ((t1 - t0) * sr)); };
        auto play = [&] (MegaSynthProcessor& p, juce::AudioBuffer<float>& c) { tg::resetVoiceRandom(); render (p, 0.6, chord (0.0, 0.55, { 57 }), &c); };
        auto maxDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int ch = -1)
        {
            double d = 0; for (int c = 0; c < 2; ++c) if (ch < 0 || ch == c) for (int i = 0; i < a.getNumSamples(); ++i) d = std::max (d, (double) std::abs (a.getSample (c, i) - b.getSample (c, i))); return d;
        };
        juce::AudioBuffer<float> ref; { auto p = clean(); play (*p, ref); }
        {
            // pan: hard left leaves the left side as it was and silences the right; centre is exactly as before
            auto p = clean(); setP (*p, P_mxOsc1Pan, -1.0f);
            juce::AudioBuffer<float> c; play (*p, c);
            std::cout << "  Osc 1 hard left: L " << rms (c, 0, 0.2, 0.5) << " (was " << rms (ref, 0, 0.2, 0.5) << "), R " << rms (c, 1, 0.2, 0.5) << std::endl;
            CHECK (maxDiff (c, ref, 0) < 1e-5 && rms (c, 1, 0.2, 0.5) < 1e-4, "balance pan: hard left");
            auto q = clean(); setP (*q, P_mxOsc1Pan, 0.5f);
            juce::AudioBuffer<float> h; play (*q, h);
            CHECK (std::abs (rms (h, 0, 0.2, 0.5) / rms (ref, 0, 0.2, 0.5) - 0.5) < 0.01 && std::abs (rms (h, 1, 0.2, 0.5) / rms (ref, 1, 0.2, 0.5) - 1.0) < 0.01, "half right: left at half, right untouched");
        }
        {
            // mute / solo
            auto two = [&] { auto p = clean(); setP (*p, P_osc2Gain, 0.5f); setP (*p, P_osc2Oct, 1.0f); return p; };
            auto m = two(); setP (*m, P_mxOsc2Mute, 1.0f);
            juce::AudioBuffer<float> cm; play (*m, cm);
            CHECK (maxDiff (cm, ref) < 1e-5, "muting Osc 2 leaves Osc 1 alone");
            auto so = two(); setP (*so, P_mxOsc1Solo, 1.0f);
            juce::AudioBuffer<float> cs; play (*so, cs);
            CHECK (maxDiff (cs, ref) < 1e-5, "soloing Osc 1 takes Osc 2 out");
            auto both = two(); setP (*both, P_mxOsc1Solo, 1.0f); setP (*both, P_mxOsc2Solo, 1.0f);
            juce::AudioBuffer<float> cb, c2; play (*both, cb); auto plain2 = two(); play (*plain2, c2);
            CHECK (maxDiff (cb, c2) < 1e-5, "two solos: both heard");
            // a muted source is still an FM modulator
            auto fmOn = two(); setP (*fmOn, P_mxOsc2Mute, 1.0f); setP (*fmOn, P_fmSlot1Source, 1.0f); setP (*fmOn, P_fmSlot1Dest, 0.0f); setP (*fmOn, P_fmSlot1Amt, 400.0f);
            juce::AudioBuffer<float> cf; play (*fmOn, cf);
            CHECK (maxDiff (cf, ref) > 0.05, "a muted Osc 2 still modulates Osc 1");
            // switching mute on mid-note ramps (no click)
            auto r = clean();
            juce::AudioBuffer<float> a; tg::resetVoiceRandom(); render (*r, 0.3, { { 0.0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) } }, &a);
            setP (*r, P_mxOsc1Mute, 1.0f);
            juce::AudioBuffer<float> b; render (*r, 0.1, {}, &b);
            float step = 0; for (int i = 1; i < 2400; ++i) step = std::max (step, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
            const float natural = 0.3f * 2.0f * (float) kPi * 220.0f / (float) sr;   // a 220 Hz sine's own largest step at this level
            CHECK (step < natural * 1.5f + 0.01f && rms (b, 0, 0.06, 0.1) < 1e-4, "mute ramps out and goes silent");
            // the ring modulator's strip
            auto rg = clean(); setP (*rg, P_ringMix, 0.6f); setP (*rg, P_ringSlot1Amt, 0.7f); setP (*rg, P_mxRingPan, 1.0f);
            auto rg0 = clean(); setP (*rg0, P_ringMix, 0.6f); setP (*rg0, P_ringSlot1Amt, 0.7f);
            juce::AudioBuffer<float> cr, cr0; play (*rg, cr); play (*rg0, cr0);
            CHECK (maxDiff (cr, cr0, 1) < 1e-5 && maxDiff (cr, cr0, 0) > 1e-3, "Ring pan: right leaves the right side as it was");
        }
        {
            // meters: measured only while asked for; the right source shows
            auto p = clean();
            render (*p, 0.2, chord (0.0, 0.19, { 57 }));
            CHECK (p->getEngine().mixerPeak[0].load() == 0.0f, "no meters unless the Mixer is showing");
            p->getEngine().mixerMeters = true;
            render (*p, 0.2, chord (0.0, 0.19, { 57 }));
            const float o1 = p->getEngine().mixerPeak[0].load(), o2 = p->getEngine().mixerPeak[1].load();
            std::cout << "  meters: Osc 1 " << o1 << ", Osc 2 " << o2 << std::endl;
            CHECK (o1 > 0.1f && o2 == 0.0f, "Osc 1's meter moves, Osc 2's doesn't");
            p->getEngine().mixerMeters = false;
            CHECK (p->getEngine().outputPeak[0].load() > 0.01f, "output meter");
            // CPU of the meters (only while the Mixer is open)
            auto cost = [&] (bool on)
            {
                double best = 1e9;
                for (int run = 0; run < 3; ++run)
                {
                    auto q = make(); for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) setP (*q, i, 0.0f);
                    q->getEngine().mixerMeters = on;
                    double cpu = 0; render (*q, 2.0, chord (0.0, 1.9, { 48, 52, 55, 59, 62, 65, 69, 72 }), nullptr, &cpu);
                    best = std::min (best, cpu / 2.0 * 100.0);
                }
                return best;
            };
            const double off = cost (false), on = cost (true);
            std::cout << "  CPU: 8 notes " << juce::String (off, 2) << "% of a core, with the strip meters " << juce::String (on, 2) << "%" << std::endl;
            CHECK (on - off < 1.5, "strip meters are cheap");
        }
        {
            // saved; old patches centred and unmuted; pans are Mod Matrix destinations, not mutated
            auto plain = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
            auto p = make(); setP (*p, P_mxWt1Pan, 0.4f); setP (*p, P_mxSubMute, 1.0f);
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (std::abs (plain (*r, P_mxWt1Pan) - 0.4f) < 0.01f && plain (*r, P_mxSubMute) > 0.5f, "strips saved in patches");
            auto json = juce::JSON::parse (p->exportBrowserPatch());
            if (auto* extra = json["plugin"].getDynamicObject())
                for (int i = P_mxOsc1Pan; i < P_mxOsc1Pan + kMxStride * kMxSources; ++i) extra->removeProperty (kParamIds[i]);
            auto o = make(); setP (*o, P_mxOsc3Pan, -0.7f); o->importBrowserPatch (juce::JSON::toString (json));
            CHECK (std::abs (plain (*o, P_mxOsc3Pan)) < 1e-4f && plain (*o, P_mxSubMute) < 0.5f, "a patch from before the strips: centred, unmuted");
            CHECK (tg::meta (P_mxOsc1Pan).modulatable && ! tg::meta (P_mxOsc1Mute).modulatable && tg::meta (P_mxOsc1Pan).group == MutGroup::None, "pan modulatable, not mutated");
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_MIXONLY", {}).isNotEmpty())
    {
        mixerTests();
        std::cout << (failures == 0 ? "MIXER TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- filter character models (Formant, Power LP, Resonator, DJ...)
    auto charTests = [&]
    {
        std::cout << "Filter character models" << std::endl;
        // the model on its own (through FilterUnit, as the voice runs it): steady-state gain of a sine
        auto gainDb = [&] (int cm, float cutoff, float res, float amt, double f, float level = 0.1f)
        {
            FilterUnit u; u.setCharacter (amt); u.configure (0, FT_LP, 1, true, cm); u.setDrive (1.0f); u.setMix (1.0f);
            const int n = (int) (0.4 * sr);
            double ei = 0, eo = 0;
            for (int i = 0; i < n; ++i)
            {
                if ((i & 15) == 0) u.update (cutoff, res, sr);
                const float x = (float) (level * std::sin (2.0 * kPi * f * i / sr));
                float yl, yr; u.processFrame (x, x, false, yl, yr);
                if (i > n / 2) { ei += (double) x * x; eo += (double) yl * yl; }
            }
            return 10.0 * std::log10 ((eo + 1e-20) / (ei + 1e-20));
        };
        auto spectrumPeak = [&] (int cm, float cutoff, float res, float amt, double f)
        {
            FilterUnit u; u.setCharacter (amt); u.configure (0, FT_LP, 1, true, cm); u.setDrive (1.0f); u.setMix (1.0f);
            const int n = 32768;
            std::vector<float> y ((size_t) n * 2, 0.0f);
            for (int i = 0; i < n + 8192; ++i)
            {
                if ((i & 15) == 0) u.update (cutoff, res, sr);
                const float x = (float) (0.2 * std::sin (2.0 * kPi * f * i / sr));
                float yl, yr; u.processFrame (x, x, false, yl, yr);
                if (i >= 8192) y[(size_t) (i - 8192)] = yl * (0.5f - 0.5f * (float) std::cos (2.0 * kPi * (i - 8192) / (n - 1)));
            }
            juce::dsp::FFT fft (15); fft.performFrequencyOnlyForwardTransform (y.data());
            int best = 1; for (int k = 1; k < n / 2; ++k) if (y[(size_t) k] > y[(size_t) best]) best = k;
            return best * sr / n;
        };
        // 1. stability: noise, full resonance, the cutoff swept 20 Hz <-> 20 kHz every 10 ms, every model, both extremes of Character
        for (int cm = 1; cm < CM_COUNT; ++cm)
            for (float amt : { 0.0f, 1.0f })
            {
                FilterUnit u; u.setCharacter (amt); u.configure (0, FT_LP, 1, true, cm); u.setDrive (4.0f); u.setMix (1.0f);
                uint32_t z = 17; bool finite = true; float peak = 0;
                for (int i = 0; i < (int) (0.5 * sr); ++i)
                {
                    if ((i & 15) == 0) { const double ph = std::fmod (i / (0.01 * sr), 2.0); u.update ((float) (20.0 * std::pow (1000.0, ph < 1.0 ? ph : 2.0 - ph)), 25.0f, sr); }
                    z = z * 1664525u + 1013904223u; const float x = ((z >> 8) / 16777216.0f - 0.5f) * 1.6f;
                    float yl, yr; u.processFrame (x, -x, true, yl, yr);
                    finite &= std::isfinite (yl) && std::isfinite (yr); peak = std::max ({ peak, std::abs (yl), std::abs (yr) });
                }
                CHECK (finite && peak < 16.0f, juce::String ("stable under abuse: ") + charModelInfo (cm).name + " (character " + juce::String (amt) + ", peak " + juce::String (peak, 2) + ")");
            }
        // 2. behaviour
        CHECK (gainDb (CM_Formant, 1000.0f, 8.0f, 0.0f, 730.0) > gainDb (CM_Formant, 1000.0f, 8.0f, 0.0f, 1500.0) + 6.0, "Formant: vowel A peaks at its first formant (730 Hz)");
        CHECK (gainDb (CM_Formant, 2000.0f, 8.0f, 0.0f, 1460.0) > gainDb (CM_Formant, 2000.0f, 8.0f, 0.0f, 730.0) + 6.0, "Formant: Cutoff shifts the formants (x2)");
        CHECK (gainDb (CM_Formant2, 1000.0f, 8.0f, 0.0f, 850.0) > gainDb (CM_Formant2, 1000.0f, 8.0f, 0.0f, 1600.0) + 6.0, "Formant 2: its own vowel set (850 Hz)");
        CHECK (std::abs (gainDb (CM_VowelMorph, 1000.0f, 8.0f, 0.0f, 790.0) - gainDb (CM_VowelMorph, 1000.0f, 8.0f, 1.0f, 790.0)) < 0.01, "Vowel Morph loops: Character 0 and 1 are the same vowel");
        CHECK (gainDb (CM_PowerLP, 1000.0f, 1.0f, 0.0f, 100.0, 0.05f) > gainDb (CM_PowerLP, 1000.0f, 1.0f, 0.0f, 10000.0, 0.05f) + 50.0, "Power LP: 24 dB low pass");
        CHECK (gainDb (CM_PowerLP, 1000.0f, 20.0f, 0.0f, 1000.0, 0.05f) > -3.0, "Power LP: stays loud at high resonance");
        CHECK (gainDb (CM_PowerBP, 1000.0f, 4.0f, 0.0f, 1000.0, 0.05f) > gainDb (CM_PowerBP, 1000.0f, 4.0f, 0.0f, 100.0, 0.05f) + 20.0, "Power BP: band pass");
        {
            // All Pass: notches (somewhere near the cutoff the sound dips deep), and the level elsewhere stays put
            double lowest = 0; for (double f = 300; f < 3000; f *= 1.03) lowest = std::min (lowest, gainDb (CM_AllPass, 1000.0f, 2.0f, 0.0f, f));
            CHECK (lowest < -15.0 && std::abs (gainDb (CM_AllPass, 1000.0f, 2.0f, 0.0f, 60.0)) < 1.0, "All Pass: a deep notch, flat elsewhere");
            double lowestP = 0; for (double f = 200; f < 5000; f *= 1.03) lowestP = std::min (lowestP, gainDb (CM_Phaser, 1000.0f, 2.0f, 0.5f, f));
            CHECK (lowestP < -15.0, "Phaser Filter: notches");
        }
        {
            // Resonator / Comb: peaks at the cutoff frequency's harmonics
            const double on = gainDb (CM_Resonator, 200.0f, 12.0f, 0.9f, 400.0), off = gainDb (CM_Resonator, 200.0f, 12.0f, 0.9f, 300.0);
            CHECK (on > off + 10.0, "Resonator: rings at the cutoff's harmonics (400 Hz vs 300 Hz: " + juce::String (on - off, 1) + " dB)");
            CHECK (gainDb (CM_Comb, 200.0f, 12.0f, 0.95f, 400.0) > gainDb (CM_Comb, 200.0f, 12.0f, 0.95f, 300.0) + 6.0, "Comb +: peaks at multiples");
            CHECK (gainDb (CM_Comb, 200.0f, 12.0f, 0.05f, 300.0) > gainDb (CM_Comb, 200.0f, 12.0f, 0.05f, 400.0) + 6.0, "Comb -: peaks between them");
            CHECK (std::abs (gainDb (CM_Comb, 200.0f, 12.0f, 0.5f, 400.0)) < 0.1, "Comb centred: off");
        }
        {
            // DJ: centre = the dry sound exactly; left = low pass; right = high pass
            FilterUnit u; u.setCharacter (0.5f); u.configure (0, FT_LP, 1, true, CM_DJ); u.setDrive (1.0f); u.setMix (1.0f);
            bool same = true;
            for (int i = 0; i < 4800; ++i) { if ((i & 15) == 0) u.update (1000.0f, 1.5f, sr); const float x = (float) std::sin (0.01 * i * i) * 0.3f; float yl, yr; u.processFrame (x, x, false, yl, yr); same &= yl == x; }
            CHECK (same, "DJ Filter centred: bit for bit the dry sound");
            CHECK (gainDb (CM_DJ, 1000.0f, 1.5f, 0.1f, 8000.0) < -20.0 && gainDb (CM_DJ, 1000.0f, 1.5f, 0.1f, 80.0) > -1.0, "DJ Filter left: low pass");
            CHECK (gainDb (CM_DJ, 1000.0f, 1.5f, 0.9f, 80.0) < -20.0 && gainDb (CM_DJ, 1000.0f, 1.5f, 0.9f, 8000.0) > -1.0, "DJ Filter right: high pass");
        }
        {
            const double shifted = spectrumPeak (CM_Shifter, 20000.0f, 1.0f, 0.75f, 1000.0);
            std::cout << "  Shifter +1 kHz: a 1 kHz sine comes out at " << juce::String (shifted, 1) << " Hz" << std::endl;
            CHECK (std::abs (shifted - 2000.0) < 30.0, "Shifter: shifts by the Character amount");
            const double ring = spectrumPeak (CM_Ring, 1000.0f, 6.0f, 1.0f, 1000.0);
            CHECK (std::abs (ring - 2000.0) < 30.0 || ring < 30.0, "Ring Filter: ring modulates at the cutoff (sum / difference)");
            CHECK (gainDb (CM_AnalogueLP, 1000.0f, 1.0f, 0.5f, 100.0, 0.05f) > gainDb (CM_AnalogueLP, 1000.0f, 1.0f, 0.5f, 10000.0, 0.05f) + 40.0, "Analogue LP: a 4-pole low pass");
            CHECK (gainDb (CM_LPResample, 1000.0f, 1.0f, 1.0f, 100.0) > gainDb (CM_LPResample, 1000.0f, 1.0f, 1.0f, 8000.0) + 15.0, "LP + Resample: low pass");
            CHECK (gainDb (CM_DecimatorLP, 1000.0f, 1.0f, 0.2f, 100.0) > gainDb (CM_DecimatorLP, 1000.0f, 1.0f, 0.2f, 8000.0) + 15.0, "Decimator LP: low pass");
            const double dpA = gainDb (CM_DualPeak, 500.0f, 10.0f, 0.5f, 500.0), dpB = gainDb (CM_DualPeak, 500.0f, 10.0f, 0.5f, 500.0 * std::pow (2.0, 1.625)), dpMid = gainDb (CM_DualPeak, 500.0f, 10.0f, 0.5f, 900.0);
            CHECK (dpA > dpMid + 6.0 && dpB > dpMid + 6.0, "Dual Peak: two peaks");
            // Wave Shaper: more harmonics with more Character
            auto thd = [&] (float amt)
            {
                FilterUnit u; u.setCharacter (amt); u.configure (0, FT_LP, 1, true, CM_WaveShaper); u.setDrive (1.0f); u.setMix (1.0f);
                double e = 0, eh = 0; float prev = 0;
                for (int i = 0; i < 24000; ++i) { if ((i & 15) == 0) u.update (20000.0f, 0.1f, sr); const float x = 0.5f * (float) std::sin (2.0 * kPi * 100.0 * i / sr); float yl, yr; u.processFrame (x, x, false, yl, yr); if (i > 4800) { e += yl * yl; eh += (yl - prev) * (yl - prev); } prev = yl; }
                return eh / std::max (1e-12, e);
            };
            CHECK (thd (1.0f) > thd (0.0f) * 4.0, "Wave Shaper: Character adds harmonics");
            // Resampler: Cutoff sets the rate (a 3 kHz sine through a 2 kHz rate aliases down)
            CHECK (std::abs (spectrumPeak (CM_Resampler, 2000.0f, 1.0f, 0.0f, 3000.0) - 1000.0) < 30.0, "Resampler: aliases at the rate Cutoff sets");
        }
        // 3. in the synth: Off is exactly as before; switching models while a note plays doesn't blow up
        {
            auto p = make(); setP (*p, P_flt1Char, (float) CM_Formant); setP (*p, P_flt2Char, (float) CM_Comb); setP (*p, P_flt1CharAmt, 0.3f);
            juce::AudioBuffer<float> c; render (*p, 0.3, chord (0.0, 1.5, { 48, 55 }), &c);
            bool ok = true; float peak = 0;
            for (int k = 1; k < CM_COUNT; ++k)
            {
                setP (*p, P_flt1Char, (float) k); setP (*p, P_filter2On, k % 2 == 0 ? 1.0f : 0.0f); setP (*p, P_flt2Char, (float) (CM_COUNT - k));
                auto st = render (*p, 0.06, {}); ok &= st.finite; peak = std::max (peak, st.peak);
            }
            CHECK (ok && peak < 4.0f, "every model while a note plays: finite, bounded (peak " + juce::String (peak, 2) + ")");
            auto m = make(); setP (*m, P_mfOn, 1.0f); setP (*m, P_mfChar, (float) CM_Formant);
            auto st = render (*m, 0.5, chord (0.0, 0.4, { 48 }));
            setP (*m, P_mfChar, (float) CM_Off); auto st2 = render (*m, 0.3, chord (0.0, 0.2, { 50 }));
            CHECK (st.finite && st2.finite && st.peak < 4.0f, "Master Filter character models, on and off");
            CHECK (tg::meta (P_flt1CharAmt).modulatable && tg::meta (P_flt1Char).group == MutGroup::None, "Character is a Mod Matrix destination; models aren't mutated");
            // saved; old patches on their classic models
            auto plain = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
            auto a = make(); setP (*a, P_flt2Char, (float) CM_DualPeak); setP (*a, P_mfChar, (float) CM_DJ);
            auto r = make(); r->importBrowserPatch (a->exportBrowserPatch());
            CHECK (juce::roundToInt (plain (*r, P_flt2Char)) == CM_DualPeak && juce::roundToInt (plain (*r, P_mfChar)) == CM_DJ, "character models saved in patches");
            auto json = juce::JSON::parse (a->exportBrowserPatch());
            if (auto* extra = json["plugin"].getDynamicObject()) for (auto id : { "flt1Char", "flt1CharAmt", "flt2Char", "flt2CharAmt", "mfChar", "mfCharAmt" }) extra->removeProperty (id);
            auto o = make(); setP (*o, P_flt1Char, (float) CM_Ring); o->importBrowserPatch (juce::JSON::toString (json));
            CHECK (juce::roundToInt (plain (*o, P_flt1Char)) == CM_Off && juce::roundToInt (plain (*o, P_flt2Char)) == CM_Off, "older patches: classic models");
        }
        // 4. CPU per voice, per model
        {
            juce::String line;
            for (int cm = 0; cm < CM_COUNT; ++cm)
            {
                FilterUnit u; u.setCharacter (0.6f); u.configure (0, FT_LP, 1, true, cm); u.setDrive (2.0f); u.setMix (1.0f);
                const int n = (int) sr;
                const auto t0 = juce::Time::getHighResolutionTicks();
                float acc = 0;
                for (int i = 0; i < n; ++i) { if ((i & 15) == 0) u.update (800.0f + 400.0f * (i & 255) / 255.0f, 8.0f, sr); float yl, yr; u.processFrame ((float) std::sin (0.05 * i), (float) std::cos (0.05 * i), true, yl, yr); acc += yl; }
                const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                line << charModelInfo (cm).name << " " << juce::String (secs * 100.0, 3) << "%  ";
                if (acc == 12345.0f) std::cout << "";
            }
            std::cout << "  CPU per stereo voice (% of a core): " << line << std::endl;
        }
    };
    // ---- Performance FX
    auto perfTests = [&]
    {
        std::cout << "Performance FX" << std::endl;
        using PF = tg::PerfFx;
        auto sine = [&] (std::vector<float>& v, double f, float a, int64_t off = 0)
        { for (size_t i = 0; i < v.size(); ++i) v[i] = a * (float) std::sin (2.0 * kPi * f * (double) ((int64_t) i + off) / sr); };
        // a whole run through the stage on its own, block by block; pads: (pad, from sample, to sample)
        struct Press { int pad; int from, to; };
        auto runFx = [&] (PF& fx, const std::vector<float>& in, std::vector<float>& outL, std::vector<float>& outR, const std::vector<Press>& presses,
                          PF::Settings st, PF::Clock ck, int block = 64)
        {
            outL = in; outR = in;
            for (int pos = 0; pos < (int) in.size(); pos += block)
            {
                const int n = std::min (block, (int) in.size() - pos);
                for (int k = 0; k < PF::kPads; ++k) st.pad[k] = false;
                for (auto& pr : presses) if (pos >= pr.from && pos < pr.to) st.pad[pr.pad] = true;
                PF::Clock c = ck; c.beat = ck.beat + ck.beatInc * pos;
                fx.process (outL.data() + pos, outR.data() + pos, n, st, c);
            }
        };
        PF::Clock ck; ck.tempo = 120.0; ck.beatInc = 120.0 / 60.0 / sr; ck.beat = 0.0; ck.barOrigin = 0.0;
        // 1. nothing engaged: the audio is untouched, bit for bit
        {
            PF fx; fx.prepare (sr);
            std::vector<float> in ((size_t) sr), l, r;
            juce::Random rnd (7); for (auto& x : in) x = rnd.nextFloat() * 2.0f - 1.0f;
            PF::Settings st; runFx (fx, in, l, r, {}, st, ck);
            CHECK (l == in && r == in && ! fx.busy(), "nothing engaged: bit-exact bypass");
        }
        // 2. every pad: engaged, released, and the sound returns to the dry signal exactly (no clicks on the way)
        {
            juce::StringArray clicky, notBack;
            for (int k = 0; k < PF::kPads; ++k)
            {
                const int N = (int) ((k == PF::Echo || k == PF::Verb ? 14.0 : 3.0) * sr);
                std::vector<float> in ((size_t) N); sine (in, 220.0, 0.5f);
                PF fx; fx.prepare (sr);
                PF::Settings st; st.quant = 0;
                std::vector<float> l, r;
                const int from = 4801, to = (int) (1.3 * sr) + 17;
                runFx (fx, in, l, r, { { k, from, to } }, st, ck);
                // the slope of a clean 220 Hz sine at 0.5 is 0.0144 a sample; a click is a jump of a large part of the level
                float worst = 0.0f;
                for (int i = 1; i < N; ++i) worst = std::max (worst, std::abs (l[(size_t) i] - l[(size_t) (i - 1)]));
                const float limit = (k == PF::Crush || k == PF::Vinyl) ? 1.0f : 0.09f;   // bitcrush and vinyl crackle are steps by design
                if (worst > limit) clicky.add (juce::String (PF::padName (k)) + " " + juce::String (worst, 3));
                // back to dry: 10 ms after letting go (Pitch Down glides back first; the throws ring out)
                int back = to + (int) (0.01 * sr);
                if (k == PF::PitchDown) back = to + (int) (0.35 * sr);
                if (k == PF::Echo || k == PF::Verb)
                {
                    back = N;
                    for (int i = N - 1; i > to; --i) if (l[(size_t) i] != in[(size_t) i]) { back = i + 1; break; }
                    std::cout << "  " << PF::padName (k) << " tail: " << juce::String ((back - to) / sr, 2) << " s" << std::endl;
                    if (back >= N - 1) notBack.add (juce::String (PF::padName (k)) + " (tail still going)");
                }
                bool exact = true;
                for (int i = back; i < N; ++i) if (l[(size_t) i] != in[(size_t) i] || r[(size_t) i] != in[(size_t) i]) { exact = false; break; }
                if (! exact) notBack.add (PF::padName (k));
                // and while held it does something
                double d = 0; for (int i = from + 2000; i < to - 100; ++i) d += std::abs (l[(size_t) i] - in[(size_t) i]);
                if (d < 1.0 && k != PF::Dj) notBack.add (juce::String (PF::padName (k)) + " (does nothing)");
            }
            CHECK (clicky.isEmpty(), "no clicks engaging / releasing pads" + (clicky.isEmpty() ? juce::String() : ": " + clicky.joinIntoString (", ")));
            CHECK (notBack.isEmpty(), "every pad returns to the dry sound exactly" + (notBack.isEmpty() ? juce::String() : ": " + notBack.joinIntoString (", ")));
        }
        // 3. the DJ filter: dark to the left, thin to the right, transparent in the middle
        {
            auto bright = [&] (float pos)
            {
                PF fx; fx.prepare (sr); PF::Settings st; st.quant = 0; st.djPos = pos;
                std::vector<float> in ((size_t) sr), l, r; juce::Random rnd (3); for (auto& x : in) x = (rnd.nextFloat() - 0.5f) * 0.5f;
                runFx (fx, in, l, r, { { PF::Dj, 0, (int) sr } }, st, ck);
                double hi = 0, lo = 0; float a = 0, b = 0;
                for (size_t i = 24000; i < in.size(); ++i) { const float d = l[i] - a; a = l[i]; hi += d * d; b += (l[i] - b) * 0.01f; lo += b * b; }
                return std::make_pair (hi, lo);
            };
            const auto dark = bright (-0.8f), mid = bright (0.0f), thin = bright (0.8f);
            CHECK (dark.first < mid.first * 0.05 && thin.second < mid.second * 0.1, "DJ filter: LP left, HP right");
        }
        // 4. on the beat grid: 90 / 140 / 174 BPM, starting mid-bar; 1/16, 1/4 and bar grids
        for (double bpm : { 90.0, 140.0, 174.0 })
            for (int q : { 1, 3, 5 })
            {
                PF fx; fx.prepare (sr);
                PF::Clock c; c.tempo = bpm; c.beatInc = bpm / 60.0 / sr; c.beat = 7.31; c.barOrigin = 4.0; c.beatsPerBar = 4.0;
                PF::Settings st; st.quant = q; st.crushBits = 2.0f;
                const int N = (int) (3.0 * sr);
                std::vector<float> in ((size_t) N), l, r; sine (in, 330.0, 0.3f);
                const int press = 1000;
                runFx (fx, in, l, r, { { PF::Crush, press, N } }, st, c, 37);
                int first = -1; for (int i = 0; i < N; ++i) if (l[(size_t) i] != in[(size_t) i]) { first = i; break; }
                const double g = PF::gridBeats (q, 4.0), atPress = c.beat + c.beatInc * press;
                const double line = std::ceil ((atPress - c.barOrigin) / g - 1e-9) * g + c.barOrigin;
                const int expect = (int) std::ceil ((line - c.beat) / c.beatInc - 1e-9);
                CHECK (first >= 0 && std::abs (first - expect) <= 1, "quantised engage lands on the grid at " + juce::String (bpm) + " BPM, grid " + juce::String (g)
                       + " (sample " + juce::String (first) + ", grid line at " + juce::String (expect) + ")");
            }
        // 5. Beat Repeat at 174 BPM: a 1/16 repeated
        {
            const double bpm = 174.0;
            PF fx; fx.prepare (sr);
            PF::Clock c; c.tempo = bpm; c.beatInc = bpm / 60.0 / sr; c.beat = 0.0;
            PF::Settings st; st.quant = 0; st.rollRate = 2;
            const int N = (int) sr;
            std::vector<float> in ((size_t) N), l, r;
            for (int i = 0; i < N; ++i) in[(size_t) i] = 0.3f * (float) (std::sin (2.0 * kPi * 197.0 * i / sr) + std::sin (2.0 * kPi * 331.0 * i / sr + 0.4 * std::sin (2.0 * kPi * 3.0 * i / sr)));
            runFx (fx, in, l, r, { { PF::Roll, 0, N } }, st, c);
            const double slice = 0.25 * 60.0 / bpm * sr;
            double err = 0, ref = 0;
            for (int i = 200; i < (int) slice - 200; ++i)
            {
                const double a = l[(size_t) i], b = l[(size_t) (i + (int) std::lround (3 * slice))];
                err += (a - b) * (a - b); ref += a * a;
            }
            CHECK (err < ref * 0.05, "Beat Repeat at 174 BPM repeats the first 1/16 (" + juce::String (err / (ref + 1e-12), 4) + ")");
        }
        // 6. in the synth: host-locked, play pressed mid-bar; MIDI keys play the pads and aren't heard as notes
        for (double bpm : { 90.0, 140.0, 174.0 })
        {
            struct FakeHead : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
            FakeHead head;
            auto p = std::make_unique<MegaSynthProcessor>();
            p->setPlayConfigDetails (0, 2, sr, 512); p->prepareToPlay (sr, 512); p->setPlayHead (&head);
            setP (*p, P_pfQuant, 3.0f); setP (*p, P_pfKeys, 1.0f);
            const double inc = bpm / 60.0 / sr; double ppq = 7.31;
            juce::AudioBuffer<float> buf (2, 128);
            for (int pos = 0; pos < (int) (1.5 * sr); pos += 128)
            {
                head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                juce::MidiBuffer m;
                if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
                if (pos == 4096) m.addEvent (juce::MidiMessage::noteOn (1, 36 + PF::Crush, (juce::uint8) 100), 50);
                buf.clear(); p->processBlock (buf, m); ppq += 128 * inc;
            }
            p->setPlayHead (nullptr);
            const double at = p->getEngine().perf().lastEngagedBeat (PF::Crush);
            const double line = std::ceil (7.31 + 4146 * inc);   // the next beat after the key
            CHECK (std::abs (at - line) <= inc * 1.01, "host at " + juce::String (bpm) + " BPM, play pressed mid-bar: the MIDI key's pad lands on the beat (beat "
                   + juce::String (at, 6) + ", line " + juce::String (line) + ")");
            CHECK (p->param (P_pfCrush)->getValue() > 0.5f && p->getEngine().perf().state (PF::Crush) == PF::On, "the MIDI key set the pad's parameter and it plays");

        }
        {
            // the fourteenth key (C1 + 13 = C#2) plays Akai Stretch, and the pad works in the synth
            auto p = make(); setP (*p, P_pfKeys, 1.0f); setP (*p, P_pfQuant, 0.0f); setP (*p, P_pfAkaiPitch, 12.0f);
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 36 + 13, (juce::uint8) 100), 0);
            p->processBlock (b, m);
            CHECK (p->param (P_pfAkai)->getValue() > 0.5f && p->getEngine().perf().state (PF::Akai) == PF::On, "the 14th pad key plays Akai Stretch");
            auto q = make();
            juce::AudioBuffer<float> x, y;
            setP (*q, P_pfAkai, 0.0f); render (*q, 0.6, chord (0.0, 0.6, { 57 }), &x);
            auto q2 = make(); setP (*q2, P_pfAkai, 1.0f); setP (*q2, P_pfQuant, 0.0f); setP (*q2, P_pfAkaiPitch, 12.0f); render (*q2, 0.6, chord (0.0, 0.6, { 57 }), &y);
            float d = 0; for (int i = (int) (0.3 * sr); i < (int) (0.6 * sr); ++i) d = std::max (d, std::abs (x.getSample (0, i) - y.getSample (0, i)));
            CHECK (d > 0.01f, "Akai Stretch changes the synth's sound when its parameter is on");
        }
        {
            // the key isn't played as a note; Latch makes a key toggle
            auto p = make(); setP (*p, P_pfKeys, 1.0f);
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            p->processBlock (b, m);
            CHECK (b.getMagnitude (0, 0, 256) == 0.0f && p->param (P_pfRoll)->getValue() > 0.5f, "a pad key plays the pad, not a note");
            juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 36), 0); p->processBlock (b, off);
            CHECK (p->param (P_pfRoll)->getValue() < 0.5f, "letting go of the key lets go of the pad");
            setP (*p, P_pfLatch, 1.0f);
            juce::MidiBuffer on2; on2.addEvent (juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100), 0); on2.addEvent (juce::MidiMessage::noteOff (1, 37), 10);
            p->processBlock (b, on2);
            CHECK (p->param (P_pfReverse)->getValue() > 0.5f, "latched: a press switches the pad on and it stays on");
            juce::MidiBuffer on3; on3.addEvent (juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100), 0); p->processBlock (b, on3);
            CHECK (p->param (P_pfReverse)->getValue() < 0.5f, "latched: the next press switches it off");
        }
        // 7. parameters: automatable, MIDI-learnable, Mod Matrix destinations where continuous; the XY pads are sources
        {
            bool ok = true;
            for (int i = P_pfRoll; i <= P_pfSpring2; ++i)
            {
                const auto& m = tg::meta (i);
                if (m.module != "Performance FX" || m.group != MutGroup::None || m.category != Category::Fx) ok = false;
                if (m.scale != Scale::Toggle && m.scale != Scale::Choice && ! m.modulatable && i != P_pfX1 && i != P_pfY1 && i != P_pfX2 && i != P_pfY2) ok = false;
            }
            CHECK (ok && ! tg::meta (P_pfX1).modulatable && tg::meta (P_pfDjPos).modulatable, "Performance FX parameters: one module, continuous ones are destinations, not mutated");
            juce::StringArray badText;
            for (int i = P_pfRoll; i <= P_pfSpring2; ++i)
            {
                const auto& m = tg::meta (i);
                if (m.scale == Scale::Toggle || m.scale == Scale::Choice) continue;
                for (float t : { 0.1f, 0.5f, 0.93f })
                {
                    const float v = m.min + (m.max - m.min) * t;
                    const float back = parseParam (i, formatParam (i, v));
                    if (std::abs (back - v) > (m.max - m.min) * 0.01f) badText.add (kParamIds[i] + juce::String (" ") + formatParam (i, v));
                }
            }
            CHECK (badText.isEmpty(), "typed values read back" + (badText.isEmpty() ? juce::String() : ": " + badText.joinIntoString (", ")));
            auto p = make();
            setP (*p, P_pfX1, 1.0f);
            p->addRoute (MS_PfX1, P_pfDjPos, 0.5f);
            p->addRoute (MS_PfY2, P_filterCutoff, 0.5f);
            setP (*p, P_pfY2, 0.0f);
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            for (int k = 0; k < 4; ++k) { b.clear(); p->processBlock (b, m); m.clear(); }
            const float dj = p->getEngine().liveOffset[(size_t) P_pfDjPos].load(), cut = p->getEngine().liveOffset[(size_t) P_filterCutoff].load();
            CHECK (std::abs (dj - 0.5f) < 0.02f && std::abs (cut + 0.5f) < 0.02f, "XY pads drive Performance FX and voice destinations (" + juce::String (dj, 3) + ", " + juce::String (cut, 3) + ")");
        }
        // 8. saved in projects and patches, undo and A/B; old projects have every pad off
        {
            auto plain = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
            auto p = make(); setP (*p, P_pfEcho, 1.0f); setP (*p, P_pfDjPos, 0.4f); setP (*p, P_pfGatePattern, 5.0f); setP (*p, P_pfX2, 0.8f); setP (*p, P_pfLatch, 1.0f);
            auto same = [&] (MegaSynthProcessor& q) { return plain (q, P_pfEcho) > 0.5f && std::abs (plain (q, P_pfDjPos) - 0.4f) < 0.01f && juce::roundToInt (plain (q, P_pfGatePattern)) == 5
                                                         && std::abs (plain (q, P_pfX2) - 0.8f) < 0.01f && plain (q, P_pfLatch) > 0.5f; };
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (same (*q), "saved in projects");
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (same (*r), "saved in patches");
            auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"13\"");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
            auto o = make(); o->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (plain (*o, P_pfEcho) < 0.5f && std::abs (plain (*o, P_pfDjPos) + 0.6f) < 0.01f, "old projects: every pad off, settings at their defaults");
            auto u = make(); u->pushHistory ("x"); setP (*u, P_pfVerbSize, 0.2f); u->pushHistory ("verb"); u->undo();
            CHECK (std::abs (plain (*u, P_pfVerbSize) - 0.8f) < 0.01f, "undo");
            auto ab = make(); setP (*ab, P_pfCrushBits, 4.0f); ab->pushHistory ("x"); ab->abSelect (1); setP (*ab, P_pfCrushBits, 10.0f); ab->pushHistory ("y"); ab->abSelect (0);
            CHECK (std::abs (plain (*ab, P_pfCrushBits) - 4.0f) < 0.05f, "A/B");
        }
        // 9. the page: a control for every Performance FX parameter
        {
            auto p = make();
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            ed->setSize (1200, 890);
            auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
            CHECK (e != nullptr && e->showPage ("Performance FX") && e->currentPageName() == "Performance FX", "Perform > Performance FX opens");
            CHECK (e != nullptr && e->showPage ("Perform") && e->currentPageName() == "Perform", "Perform > Perform still opens");
            std::set<int> got;
            std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
            {
                if (auto* k = dynamic_cast<tgui::Knob*> (c)) got.insert (k->paramIndex);
                if (auto* xy = dynamic_cast<tgui::PerfXY*> (c)) { got.insert (xy->px()); got.insert (xy->py()); }
                if (auto* ch = dynamic_cast<tgui::Choice*> (c)) juce::ignoreUnused (ch);
                if (c->getProperties().contains ("megaParam")) got.insert ((int) c->getProperties()["megaParam"]);
                for (auto* ch : c->getChildren()) walk (ch);
            };
            walk (ed.get());
            juce::StringArray missing;
            for (int i = P_pfRoll; i <= P_pfSpring2; ++i) if (got.count (i) == 0) missing.add (kParamIds[i]);
            for (int i = P_pfAkai; i <= P_pfAkaiMix; ++i) if (got.count (i) == 0) missing.add (kParamIds[i]);
            CHECK (missing.isEmpty(), "every Performance FX parameter has a control" + (missing.isEmpty() ? juce::String() : ": " + missing.joinIntoString (", ")));
        }
        // 10. CPU: the stage idle, one pad, and every pad at once
        {
            auto cost = [&] (int pads)
            {
                PF fx; fx.prepare (sr); PF::Settings st; st.quant = 0;
                for (int k = 0; k < pads; ++k) st.pad[k] = true;
                std::vector<float> l (256), r (256);
                PF::Clock c = ck;
                const int blocks = (int) (10.0 * sr / 256);
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int b = 0; b < blocks; ++b) { sine (l, 220.0, 0.3f, b * 256); r = l; fx.process (l.data(), r.data(), 256, st, c); c.beat += 256 * c.beatInc; }
                return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / 10.0 * 100.0;
            };
            std::cout << "  CPU (% of a core): idle " << juce::String (cost (0), 3) << "  Beat Repeat " << juce::String (cost (1), 3)
                      << "  all 14 pads " << juce::String (cost (PF::kPads), 3) << std::endl;
            // Akai Stretch alone, per mode
            juce::String line;
            for (int mode = 0; mode < 4; ++mode)
            {
                PF fx; fx.prepare (sr); PF::Settings st; st.quant = 0; st.pad[PF::Akai] = true; st.akaiMode = mode; st.akaiPitch = mode == 0 ? 7.0f : 0.0f;
                std::vector<float> l (256), r (256);
                PF::Clock c = ck;
                const int blocks = (int) (10.0 * sr / 256);
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int b = 0; b < blocks; ++b) { sine (l, 220.0, 0.3f, b * 256); r = l; fx.process (l.data(), r.data(), 256, st, c); c.beat += 256 * c.beatInc; }
                line << (mode == 0 ? "Crunch " : mode == 1 ? "Slow " : mode == 2 ? "Fast " : "Halftime ") << juce::String (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / 10.0 * 100.0, 3) << "%  ";
            }
            std::cout << "  CPU, Akai Stretch (% of a core, the same at every Quality): " << line << std::endl;
        }
        // 11. Akai Stretch: pitch and length in each mode
        {
            auto peakHz = [&] (const std::vector<float>& x, int s0)
            {
                const int order = 14, N = 1 << order;
                std::vector<float> b ((size_t) N * 2, 0.0f);
                for (int i = 0; i < N && s0 + i < (int) x.size(); ++i) b[(size_t) i] = x[(size_t) (s0 + i)] * (0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / (N - 1)));
                juce::dsp::FFT fft (order); fft.performFrequencyOnlyForwardTransform (b.data());
                int k = 2; for (int i = 2; i < N / 2 - 1; ++i) if (b[(size_t) i] > b[(size_t) k]) k = i;
                const double a = b[(size_t) k - 1], c = b[(size_t) k], d = b[(size_t) k + 1];
                return (k + 0.5 * (a - d) / (a - 2.0 * c + d)) * sr / N;
            };
            // tone bursts on every beat (120 BPM: 0.5 s apart), 60 ms of 440 Hz each
            auto bursts = [&] (int N)
            {
                std::vector<float> v ((size_t) N, 0.0f);
                for (int i = 0; i < N; ++i) { const double t = i / sr, ph = std::fmod (t, 0.5); if (ph < 0.06) v[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPi * 440.0 * t) * (float) std::min (1.0, std::min (ph, 0.06 - ph) / 0.003); }
                return v;
            };
            // the onsets (rising past a threshold) after a time
            auto onsets = [&] (const std::vector<float>& x, int from)
            {
                std::vector<double> o; float env = 0.0f; bool on = false;
                for (int i = from; i < (int) x.size(); ++i)
                {
                    env = std::max (std::abs (x[(size_t) i]), env * 0.999f);
                    if (! on && env > 0.2f) { on = true; o.push_back (i / sr); }
                    if (on && env < 0.02f) on = false;
                }
                return o;
            };
            auto spacing = [] (const std::vector<double>& o) { double s = 0; int n = 0; for (size_t i = 1; i < o.size(); ++i) { s += o[i] - o[i - 1]; ++n; } return n > 0 ? s / n : 0.0; };
            const int N = (int) (6.0 * sr), press = (int) (2.0 * sr);
            auto run = [&] (int mode, bool hard, float pitch, std::vector<float>& out)
            {
                PF fx; fx.prepare (sr); PF::Settings st; st.quant = 3; st.akaiMode = mode; st.akaiHard = hard; st.akaiPitch = pitch;
                std::vector<float> r;
                runFx (fx, bursts (N), out, r, { { PF::Akai, press, N } }, st, ck);
                return fx.lastEngagedBeat (PF::Akai);
            };
            juce::StringArray bad;
            struct M { const char* name; int mode; bool hard; double spacing; };
            const M ms[] = { { "Slow x0.5", PF::AkaiSlow, true, 1.0 }, { "Slow x0.75", PF::AkaiSlow, false, 0.5 / 0.75 }, { "Fast x2", PF::AkaiFast, true, 0.25 },
                             { "Fast x1.5", PF::AkaiFast, false, 0.5 / 1.5 }, { "Halftime", PF::AkaiHalftime, true, 1.0 }, { "Crunch", PF::AkaiCrunch, true, 0.5 } };
            for (auto& m : ms)
            {
                std::vector<float> out;
                const double eng = run (m.mode, m.hard, 0.0f, out);
                const auto o = onsets (out, press + (int) (0.05 * sr));
                const double sp = spacing (o);
                // the pitch of one burst (Akai Cyclic flutters: within a line or two of 1 / cycle)
                double hz = 0.0;
                if (! o.empty())
                {
                    std::vector<float> one ((size_t) (1 << 14), 0.0f);
                    const int s0 = (int) (o.front() * sr);
                    for (int i = 0; i < (int) (0.05 * sr) && s0 + i < (int) out.size(); ++i) one[(size_t) i] = out[(size_t) (s0 + i)];
                    hz = peakHz (one, 0);
                }
                std::cout << "  Akai Stretch " << m.name << ": hits every " << juce::String (sp, 3) << " s (expect " << juce::String (m.spacing, 3) << "), pitch "
                          << juce::String (hz, 1) << " Hz, engaged on beat " << juce::String (eng, 4) << std::endl;
                if (o.size() < 3 || std::abs (sp - m.spacing) > 0.02 * m.spacing + 0.004 || std::abs (hz - 440.0) > 30.0 || std::abs (eng - std::round (eng)) > 1e-6) bad.add (m.name);
            }
            CHECK (bad.isEmpty(), "Akai Stretch: each mode's timing, the pitch kept, starting on the beat" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
            // Crunch with Pitch: time kept, pitch moved (a steady tone)
            {
                std::vector<float> in ((size_t) N); sine (in, 330.0, 0.4f);
                PF fx; fx.prepare (sr); PF::Settings st; st.quant = 0; st.akaiMode = PF::AkaiCrunch; st.akaiPitch = 12.0f;
                std::vector<float> l, r; runFx (fx, in, l, r, { { PF::Akai, press, N } }, st, ck);
                const double hz = peakHz (l, press + (int) (0.2 * sr));
                CHECK (std::abs (hz - 660.0) < 30.0, "Akai Crunch at +12: the pitch goes up an octave in real time (" + juce::String (hz, 1) + " Hz)");
            }
        }
    };
    // ---- Granular Synth
    auto granTests = [&]
    {
        std::cout << "Granular Synth" << std::endl;
        const int slot = MegaSynthProcessor::kSlotGranular;
        // a sine (or anything) as a WAV file in memory
        auto wavOf = [&] (double seconds, double rate, int chans, std::function<float (int, int)> fn)
        {
            juce::AudioBuffer<float> b (chans, (int) (seconds * rate));
            for (int c = 0; c < chans; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, fn (c, i));
            juce::MemoryBlock mb;
            {
                auto* os = new juce::MemoryOutputStream (mb, false);
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, rate, (unsigned) chans, 24, {}, 0));
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            return mb;
        };
        auto sineWav = [&] (double hz, double seconds = 1.0) { return wavOf (seconds, 44100.0, 1, [=] (int, int i) { return 0.8f * (float) std::sin (2.0 * kPi * hz * i / 44100.0); }); };
        // only the Granular Synth sounding, no effects
        auto granOnly = [&]
        {
            auto p = make();
            for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain, P_osc4Gain, P_wt2Gain, P_ringGain,
                           P_reverbMix, P_delayMix, P_chorusMix, P_shimmerMix, P_reverseMix }) setP (*p, i, 0.0f);
            setP (*p, P_filterCutoff, 18000.0f);
            // (spray scatters the grain starts; without it the cloud repeats at the Density rate, and its spectrum
            // becomes a comb of 1 / Density lines, which would fool the pitch measurement)
            setP (*p, P_gsOn, 1.0f); setP (*p, P_gsSpray, 40.0f); setP (*p, P_gsSpread, 0.0f); setP (*p, P_gsSize, 200.0f); setP (*p, P_gsDensity, 15.0f);
            return p;
        };
        auto peakHz = [&] (const juce::AudioBuffer<float>& b, double t0)
        {
            const int order = 15, N = 1 << order;
            std::vector<float> x ((size_t) N * 2, 0.0f);
            const int s0 = (int) (t0 * sr);
            for (int i = 0; i < N && s0 + i < b.getNumSamples(); ++i)
                x[(size_t) i] = b.getSample (0, s0 + i) * (0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / (N - 1)));
            juce::dsp::FFT fft (order); fft.performFrequencyOnlyForwardTransform (x.data());
            int k = 2; for (int i = 2; i < N / 2 - 1; ++i) if (x[(size_t) i] > x[(size_t) k]) k = i;
            const double a = x[(size_t) k - 1], c = x[(size_t) k], d = x[(size_t) k + 1];
            const double off = 0.5 * (a - d) / (a - 2.0 * c + d);
            return (k + off) * sr / N;
        };
        auto zcHz = [&] (const juce::AudioBuffer<float>& c, double t0, double t1)
        {
            double first = -1, last = -1; int n = 0;
            for (int i = (int) (t0 * sr) + 1; i < (int) (t1 * sr); ++i)
            {
                const float a = c.getSample (0, i - 1), b = c.getSample (0, i);
                if (a < 0.0f && b >= 0.0f) { const double x = (i - 1 + a / (a - b)) / sr; if (first < 0) first = x; last = x; ++n; }
            }
            return n > 1 ? (n - 1) / (last - first) : 0.0;
        };
        auto oneGrain = [&] (MegaSynthProcessor& p) { setP (p, P_gsRoot, 69.0f); setP (p, P_gsSpray, 0.0f); setP (p, P_gsSize, 500.0f); setP (p, P_gsDensity, 2.0f); setP (p, P_gsPosition, 0.1f); };
        // 1. off (the default) or with no sample: nothing changes
        {
            auto a = make(), b = make();
            // (the reverb's impulse loads on JUCE's background thread, so its first moments vary from run to run: leave it out)
            for (auto* q : { a.get(), b.get() }) setP (*q, P_reverbMix, 0.0f);
            b->loadSampleData (sineWav (440.0), "sine.wav", slot);   // loaded, but the Granular Synth is off
            tg::resetVoiceRandom(); juce::AudioBuffer<float> x; render (*a, 0.5, chord (0.0, 0.4, { 60, 64 }), &x);
            tg::resetVoiceRandom(); juce::AudioBuffer<float> y; render (*b, 0.5, chord (0.0, 0.4, { 60, 64 }), &y);
            float d = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < x.getNumSamples(); ++i) d = std::max (d, std::abs (x.getSample (c, i) - y.getSample (c, i)));
            CHECK (d == 0.0f, "loaded but off: bit-identical (" + juce::String (d) + ")");
            // switched on with no sample: nothing plays from it (the default-off case is covered bit for bit by the golden renders)
            auto o = make(); setP (*o, P_gsOn, 1.0f);
            const auto st = render (*o, 0.3, chord (0.0, 0.25, { 60 }));
            CHECK (st.finite && st.peak > 0.01f && o->getEngine().granGrainsShown.load() == 0, "on with no sample: no grains, the synth plays as usual");
        }
        // 2. pitch tracking: a 440 Hz sample with its root at A4 plays the note you press
        {
            juce::StringArray bad;
            for (auto [note, keyTrack, semis, expect] : { std::tuple<int, bool, float, double> { 69, true, 0.0f, 440.0 }, { 81, true, 0.0f, 880.0 }, { 57, true, 0.0f, 220.0 },
                                                          { 64, true, 0.0f, 440.0 * std::pow (2.0, -5.0 / 12.0) }, { 69, true, 7.0f, 440.0 * std::pow (2.0, 7.0 / 12.0) },
                                                          { 81, false, 0.0f, 440.0 } })
            {
                auto p = granOnly();
                p->loadSampleData (sineWav (440.0, 4.0), "sine.wav", slot);
                // one long grain at a time (500 ms, two a second): inside a grain the pitch is a clean sine
                setP (*p, P_gsSpray, 0.0f); setP (*p, P_gsSize, 500.0f); setP (*p, P_gsDensity, 2.0f); setP (*p, P_gsPosition, 0.1f);
                setP (*p, P_gsRoot, 69.0f); setP (*p, P_gsKeyTrack, keyTrack ? 1.0f : 0.0f); setP (*p, P_gsSemi, semis);
                juce::AudioBuffer<float> b; render (*p, 0.6, chord (0.0, 0.55, { note }), &b);
                const double hz = zcHz (b, 0.12, 0.38);
                std::cout << "  note " << note << (keyTrack ? "" : " (no key track)") << " +" << semis << " st: " << juce::String (hz, 2) << " Hz (expect " << juce::String (expect, 2) << ")" << std::endl;
                if (std::abs (hz / expect - 1.0) > 0.003) bad.add (juce::String (note));
            }
            CHECK (bad.isEmpty(), "pitch tracking" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
        }
        // 3. no clicks: every grain shape starts and ends at zero (second difference of the cloud stays smooth)
        {
            WaveSample w; w.numChannels = 1; w.sampleRate = sr; w.length = (int) sr; w.peak = 1.0f;
            w.ch[0].resize ((size_t) w.length); for (int i = 0; i < w.length; ++i) w.ch[0][(size_t) i] = (float) std::sin (2.0 * kPi * 220.0 * i / sr);
            w.ch[1] = w.ch[0];
            juce::StringArray clicky;
            for (int shape = 0; shape < 5; ++shape)
            {
                GranVoice g; g.prepare (sr); g.start (7);
                GranVoice::Params gp; gp.shape = shape; gp.sizeMs = 40.0f; gp.density = 35.0f; gp.sprayMs = 100.0f; gp.spread = 0.8f; gp.reverse = 0.3f; gp.maxGrains = 48;
                float prev[2] = {}, prev2[2] = {}, worst = 0.0f, peak = 0.0f;
                for (int i = 0; i < (int) sr; ++i)
                {
                    float l, r; g.tick (w, gp, l, r);
                    const float d2 = std::abs (l - 2.0f * prev[0] + prev2[0]);
                    if (i > 2) worst = std::max (worst, d2);
                    peak = std::max (peak, std::abs (l));
                    prev2[0] = prev[0]; prev[0] = l;
                }
                // a 220 Hz sine's second difference is 0.0021 of its level; a window that jumps would be ~1
                const float rel = worst / std::max (1e-6f, peak);
                std::cout << "  shape " << shape << ": worst 2nd difference " << juce::String (rel, 4) << " of the peak" << std::endl;
                if (rel > 0.05f) clicky.add (juce::String (shape));
            }
            CHECK (clicky.isEmpty(), "grain shapes don't click" + (clicky.isEmpty() ? juce::String() : ": " + clicky.joinIntoString (", ")));
            // a note's start and end through the synth: the amp envelope covers them
            auto p = granOnly(); p->loadSampleData (sineWav (440.0), "sine.wav", slot);
            setP (*p, P_ampA, 0.005f); setP (*p, P_ampR, 0.05f);
            juce::AudioBuffer<float> b; render (*p, 0.8, chord (0.05, 0.5, { 69 }), &b);
            float jump = 0, pk = 0; for (int i = 1; i < b.getNumSamples(); ++i) { jump = std::max (jump, std::abs (b.getSample (0, i) - b.getSample (0, i - 1))); pk = std::max (pk, std::abs (b.getSample (0, i))); }
            CHECK (pk > 0.01f && jump < pk * 0.12f, "note on / off in the synth: no clicks (" + juce::String (jump / std::max (1e-6f, pk), 3) + ")");
        }
        // 4. Quality limits the grains per note; CPU at Eco / Normal / High
        {
            WaveSample w; w.numChannels = 2; w.sampleRate = sr; w.length = (int) sr; w.peak = 1.0f;
            w.ch[0].assign ((size_t) w.length, 0.1f); w.ch[1] = w.ch[0];
            for (auto [cap, expect] : { std::pair<int, int> { 8, 8 }, { 24, 24 }, { 48, 48 } })
            {
                GranVoice g; g.prepare (sr); g.start (1);
                GranVoice::Params gp; gp.sizeMs = 500.0f; gp.density = 100.0f; gp.maxGrains = cap;
                int most = 0; float l, r;
                for (int i = 0; i < (int) sr; ++i) { g.tick (w, gp, l, r); most = std::max (most, g.activeGrains()); }
                CHECK (most == expect, "Quality cap: at most " + juce::String (expect) + " grains a note (" + juce::String (most) + ")");
            }
            juce::String line;
            for (auto [name, cap] : { std::pair<const char*, int> { "Eco", 8 }, { "Normal", 24 }, { "High", 48 } })
            {
                // 8 notes of a very dense cloud (100 grains a second of 400 ms: 40 would overlap), the grain engine alone
                GranVoice v[8]; for (int k = 0; k < 8; ++k) { v[k].prepare (sr); v[k].start ((uint64_t) k); }
                GranVoice::Params gp; gp.sizeMs = 400.0f; gp.density = 100.0f; gp.sprayMs = 80.0f; gp.scan = 0.3f; gp.spread = 0.6f; gp.maxGrains = cap;
                float acc = 0.0f, l, r;
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < (int) (5.0 * sr); ++i) for (int k = 0; k < 8; ++k) { gp.ratio = 1.0 + 0.1 * k; v[k].tick (w, gp, l, r); acc += l; }
                const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                line << name << " " << juce::String (secs / 5.0 * 100.0, 2) << "%  ";
                if (acc == 12345.0f) std::cout << "";
            }
            std::cout << "  CPU, 8 notes of a very dense cloud (grain engine, % of a core): " << line << std::endl;
        }
        // 5. saved in the project and the patch (the sample too), undo and A/B; long files are cut; old projects have none
        {
            auto p = granOnly();
            CHECK (p->loadSampleData (sineWav (330.0, 2.0), "tone.wav", slot), "a WAV loads");
            setP (*p, P_gsPosition, 0.6f); setP (*p, P_gsSize, 120.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            const auto* w = q->getSample (slot);
            CHECK (w != nullptr && std::abs (w->duration() - 2.0) < 0.01 && q->getSampleName (slot) == "tone.wav"
                   && std::abs (q->param (P_gsPosition)->convertFrom0to1 (q->param (P_gsPosition)->getValue()) - 0.6f) < 0.01f, "saved in projects, with the sample");
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (r->getSample (slot) != nullptr && r->getSampleName (slot) == "tone.wav" && r->param (P_gsOn)->getValue() > 0.5f, "saved in patches, with the sample");
            auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"14\"")
                                               .replace ("waveDataGran=", "oldIgnored=");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
            auto o = make(); o->loadSampleData (sineWav (100.0), "leftover.wav", slot);
            o->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (o->getSample (slot) == nullptr && o->param (P_gsOn)->getValue() < 0.5f, "old projects: no granular sample, off");
            // undo brings a cleared sample back
            p->pushHistory ("x"); p->clearSample (slot); p->pushHistory ("clear"); p->undo();
            CHECK (p->getSample (slot) != nullptr, "undo restores the sample");
            // a 25 s file is cut to 20 s, and only that is saved
            auto l = make();
            const auto big = wavOf (25.0, 44100.0, 1, [] (int, int i) { return 0.3f * (float) std::sin (0.05 * i); });
            l->loadSampleData (big, "long.wav", slot);
            const auto* lw = l->getSample (slot);
            CHECK (lw != nullptr && std::abs (lw->duration() - 20.0) < 0.01 && l->getSampleBytes (slot) < big.getSize() * 0.85, "long files are cut to 20 s (and saved cut)");
            juce::MemoryBlock lb; l->getStateInformation (lb);
            auto l2 = make(); l2->setStateInformation (lb.getData(), (int) lb.getSize());
            CHECK (l2->getSample (slot) != nullptr && std::abs (l2->getSample (slot)->duration() - 20.0) < 0.01, "the cut sample reloads");
            std::cout << "  " << tgui::sampleInfoText (*l, slot) << std::endl;
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            CHECK (fm.findFormatForFileExtension ("mp3") != nullptr && fm.findFormatForFileExtension ("flac") != nullptr && fm.findFormatForFileExtension ("aiff") != nullptr, "WAV, AIFF, FLAC and MP3 readers");
        }
        // 6. Mod Matrix destinations (per voice), the Mixer strip, DNA Splice
        {
            CHECK (tg::meta (P_gsPosition).modulatable && tg::meta (P_gsDensity).modulatable && ! tg::meta (P_gsRoot).modulatable && tg::meta (P_gsPosition).group == MutGroup::None,
                   "granular controls are Mod Matrix destinations and not mutated");
            auto p = granOnly(); p->loadSampleData (sineWav (440.0, 4.0), "sine.wav", slot); oneGrain (*p);
            p->addRoute (MS_Macro1, P_gsSemi, 0.25f); setP (*p, P_macro1, 1.0f);   // + a quarter of 48 semitones = +12
            juce::AudioBuffer<float> b; render (*p, 0.6, chord (0.0, 0.55, { 69 }), &b);
            const double hz = zcHz (b, 0.12, 0.38);
            CHECK (std::abs (hz / 880.0 - 1.0) < 0.004, "a macro route moves the granular pitch (" + juce::String (hz, 1) + " Hz)");
            auto m = granOnly(); m->loadSampleData (sineWav (440.0), "sine.wav", slot); setP (*m, P_mxGranMute, 1.0f);
            const auto st = render (*m, 0.5, chord (0.0, 0.45, { 69 }));
            CHECK (st.peak < 1e-6f, "the Mixer's Granular mute silences it");
            auto so = make(); so->loadSampleData (sineWav (440.0, 4.0), "sine.wav", slot); setP (*so, P_gsOn, 1.0f); setP (*so, P_mxGranSolo, 1.0f); oneGrain (*so);
            for (int i : { P_reverbMix, P_delayMix, P_chorusMix }) setP (*so, i, 0.0f);
            setP (*so, P_filterCutoff, 18000.0f); setP (*so, P_gsSpread, 0.0f);
            juce::AudioBuffer<float> sb; render (*so, 0.6, chord (0.0, 0.55, { 69 }), &sb);
            CHECK (std::abs (zcHz (sb, 0.12, 0.38) / 440.0 - 1.0) < 0.004, "soloed: only the granular is heard (" + juce::String (zcHz (sb, 0.12, 0.38), 1) + " Hz)");
            auto d = granOnly(); d->loadSampleData (sineWav (440.0), "sine.wav", slot);
            setP (*d, P_dnaMix, 1.0f); setP (*d, P_gsDna, 1.0f);
            const auto ds = render (*d, 0.5, chord (0.0, 0.45, { 69 }));
            CHECK (ds.finite && ds.peak > 1e-4f, "stands in for DNA Splice source A");
        }
    };
    // ---- Break Chopper and Break Sequencer
    auto breakTests = [&]
    {
        std::cout << "Break Chopper / Break Sequencer" << std::endl;
        const int slot = MegaSynthProcessor::kSlotBreak;
        struct FakeHead : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        auto wav16 = [&] (const juce::AudioBuffer<float>& b, double rate) { return tg::breaks::toWav16 (b, rate); };
        auto demo = [&] { return wav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0); };
        auto quiet = [&] (MegaSynthProcessor& p)
        {
            for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_complexGain, P_supersawGain, P_osc4Gain, P_wt2Gain, P_ringGain,
                           P_reverbMix, P_delayMix, P_chorusMix, P_shimmerMix, P_reverseMix }) setP (p, i, 0.0f);
        };
        // a "break" that is one steady sine, for pitch and length checks
        auto sineBreak = [&] (double hz, double seconds) {
            juce::AudioBuffer<float> b (1, (int) (seconds * 44100.0));
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.7f * (float) std::sin (2.0 * kPi * hz * i / 44100.0));
            return wav16 (b, 44100.0); };
        auto zc = [&] (const std::vector<float>& x, int a, int b)
        {
            double first = -1, last = -1; int n = 0;
            for (int i = std::max (1, a); i < std::min ((int) x.size(), b); ++i)
                if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f) { const double t = (i - 1 + x[(size_t) i - 1] / (x[(size_t) i - 1] - x[(size_t) i])) / sr; if (first < 0) first = t; last = t; ++n; }
            return n > 1 ? (n - 1) / (last - first) : 0.0;
        };
        // plays the processor against a fake DAW: tempo, play pressed at ppq0; returns the left channel
        auto runHost = [&] (MegaSynthProcessor& p, double bpm, double ppq0, double seconds, std::vector<std::pair<int, juce::MidiMessage>> ev = {}, int block = 128)
        {
            FakeHead head; p.setPlayHead (&head);
            const double inc = bpm / 60.0 / sr; double ppq = ppq0;
            juce::AudioBuffer<float> buf (2, block);
            std::vector<float> out;
            for (int pos = 0; pos < (int) (seconds * sr); pos += block)
            {
                head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                juce::MidiBuffer m;
                for (auto& e : ev) if (e.first >= pos && e.first < pos + block) m.addEvent (e.second, e.first - pos);
                buf.clear(); p.processBlock (buf, m); ppq += block * inc;
                for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
            }
            p.setPlayHead (nullptr);
            return out;
        };
        // 1. analysis: the demo break's tempo and slices
        {
            auto p = make(); setP (*p, P_bcOn, 1.0f);
            p->loadSampleData (demo(), "demo.wav", slot);
            auto d = p->breakStore.get();
            CHECK (std::abs (d.brk[0].origBpm - 170.0) < 0.5 && d.brk[0].bars == 2, "tempo detected from the length: 170 BPM, 2 bars (" + juce::String (d.brk[0].origBpm, 2) + ")");
            // the true hit times (16ths with the demo's small swing)
            std::vector<double> hits; const double st = 60.0 / 170.0 / 4.0;
            for (int s16 : { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 }) hits.push_back (s16 * st + ((s16 & 1) ? st * 0.08 : 0.0));
            int near = 0;
            for (int k = 1; k < d.brk[0].nSlices; ++k)
            {
                const double t = d.brk[0].start[k] / 44100.0;
                double best = 1.0; for (double h : hits) best = std::min (best, std::abs (t - h));
                if (best < 0.012) ++near;
            }
            std::cout << "  transients: " << d.brk[0].nSlices << " slices, " << near << " of " << (d.brk[0].nSlices - 1) << " within 12 ms of a real hit" << std::endl;
            CHECK (d.brk[0].nSlices >= 12 && d.brk[0].nSlices <= 32 && near >= (d.brk[0].nSlices - 1) * 9 / 10, "transient slicing finds the hits");
            setP (*p, P_bcSens, 0.0f); p->refreshBreakAnalysis();
            const int fewer = p->breakStore.get().brk[0].nSlices;
            setP (*p, P_bcSens, 1.0f); p->refreshBreakAnalysis();
            CHECK (fewer <= p->breakStore.get().brk[0].nSlices, "sensitivity: more slices when higher (" + juce::String (fewer) + " / " + juce::String (p->breakStore.get().brk[0].nSlices) + ")");
            setP (*p, P_bcSliceMode, 3.0f); p->refreshBreakAnalysis();
            d = p->breakStore.get();
            bool equal = d.brk[0].nSlices == 16;
            for (int k = 0; k < 16 && equal; ++k) equal = std::abs ((d.brk[0].start[k + 1] - d.brk[0].start[k]) - d.brk[0].start[16] / 16) <= 1;
            CHECK (equal, "Equal 16");
            setP (*p, P_bcSliceMode, 5.0f);
            p->breakStore.edit ([] (tg::BreakData& x) { x.brk[0].nMarkers = 2; x.brk[0].markers[0] = 0.25f; x.brk[0].markers[1] = 0.6f; });
            p->refreshBreakAnalysis();
            d = p->breakStore.get();
            CHECK (d.brk[0].nSlices == 3 && std::abs (d.brk[0].start[1] - (int) (0.25 * d.brk[0].start[3])) < 2, "manual markers");
            setP (*p, P_bcTempo, 85.0f); p->refreshBreakAnalysis();
            CHECK (std::abs (p->breakStore.get().brk[0].origBpm - 85.0) < 0.01, "a typed tempo wins");
        }
        // 2. stretching a steady 440 Hz "break" (typed at 120 BPM, one bar) to 90 BPM: length and pitch per mode
        {
            juce::StringArray bad;
            for (int mode = 0; mode < 3; ++mode)
            {
                auto p = make(); quiet (*p);
                setP (*p, P_bcOn, 1.0f); setP (*p, P_bcTempo, 120.0f); setP (*p, P_bcSliceMode, 1.0f); setP (*p, P_bcStretch, (float) mode);
                setP (*p, P_bcLevel, 1.0f); setP (*p, P_bcVelSens, 0.0f);
                p->loadSampleData (sineBreak (440.0, 2.0), "sine.wav", slot);   // equal 4: 0.5 s slices
                const auto out = runHost (*p, 90.0, 0.0, 1.2, { { 256, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) } });
                int last = 0; for (int i = 0; i < (int) out.size(); ++i) if (std::abs (out[(size_t) i]) > 1e-3f) last = i;
                const double secs = (last - 256) / sr, expectLen = 0.5 * 120.0 / 90.0;
                // pitch: the spectrum's peak (cyclic stretching jumps the phase every cycle, which fools zero crossings)
                double hz = 0.0;
                {
                    const int order = 14, N = 1 << order, s0 = 256 + (int) (0.05 * sr);
                    std::vector<float> x ((size_t) N * 2, 0.0f);
                    for (int i = 0; i < N && s0 + i < (int) out.size(); ++i) x[(size_t) i] = out[(size_t) (s0 + i)] * (0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / (N - 1)));
                    juce::dsp::FFT fft (order); fft.performFrequencyOnlyForwardTransform (x.data());
                    int k = 2; for (int i = 2; i < N / 2 - 1; ++i) if (x[(size_t) i] > x[(size_t) k]) k = i;
                    const double a = x[(size_t) k - 1], c = x[(size_t) k], dd = x[(size_t) k + 1];
                    hz = (k + 0.5 * (a - dd) / (a - 2.0 * c + dd)) * sr / N;
                }
                const double expectHz = mode == 0 ? 440.0 * 90.0 / 120.0 : 440.0;
                std::cout << "  " << kBcStretchLabels[mode] << ": slice lasts " << juce::String (secs, 3) << " s (expect " << juce::String (expectLen, 3) << "), pitch "
                          << juce::String (hz, 1) << " Hz (expect " << juce::String (expectHz, 1) << ")" << std::endl;
                // Akai Cyclic repeats 40 ms cycles, so a steady tone becomes lines 25 Hz apart around it (its "flutter");
                // its peak is within one line of the true pitch
                const double tol = mode == 1 ? 1000.0 / 40.0 : expectHz * 0.01;
                if (std::abs (secs - expectLen) > 0.01 || std::abs (hz - expectHz) > tol) bad.add (kBcStretchLabels[mode]);
            }
            CHECK (bad.isEmpty(), "stretch modes: length follows the tempo; Cyclic and Clean keep the pitch" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
        }
        // 3. no clicks: slices of a sine played back to back, choked, by key
        {
            juce::StringArray clicky;
            for (int mode = 0; mode < 3; ++mode)
            {
                auto p = make(); quiet (*p);
                setP (*p, P_bcOn, 1.0f); setP (*p, P_bcTempo, 120.0f); setP (*p, P_bcSliceMode, 2.0f); setP (*p, P_bcStretch, (float) mode);
                p->loadSampleData (sineBreak (220.0, 2.0), "sine.wav", slot);
                std::vector<std::pair<int, juce::MidiMessage>> ev;
                for (int k = 0; k < 8; ++k) ev.push_back ({ 300 + k * 4100, juce::MidiMessage::noteOn (1, 36 + (k * 3) % 8, (juce::uint8) 100) });
                const auto out = runHost (*p, 140.0, 0.0, 1.0, ev);
                float worst = 0, pk = 0;
                for (size_t i = 1; i < out.size(); ++i) { worst = std::max (worst, std::abs (out[i] - out[i - 1])); pk = std::max (pk, std::abs (out[i])); }
                // a 220 Hz sine's steepest step is 0.029 of its level; a click is a jump of a large part of it
                if (std::getenv ("BKDBG")) { size_t wi = 1; for (size_t i = 1; i < out.size(); ++i) if (std::abs (out[i] - out[i - 1]) > std::abs (out[wi] - out[wi - 1])) wi = i; std::cout << "    mode " << mode << " worst at " << wi << std::endl; }
                if (pk < 0.05f || worst > pk * 0.09f) clicky.add (juce::String (kBcStretchLabels[mode]) + " " + juce::String (worst / std::max (1e-6f, pk), 3));
            }
            CHECK (clicky.isEmpty(), "slices start, choke and end without clicks" + (clicky.isEmpty() ? juce::String() : ": " + clicky.joinIntoString (", ")));
        }
        // 4. the sequencer, host-locked: 90 / 140 / 174 BPM, play pressed mid-bar
        for (double bpm : { 90.0, 140.0, 174.0 })
        {
            auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
            p->loadSampleData (demo(), "demo.wav", slot);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 16); for (int i = 0; i < 16; ++i) { d.step[0][i].on = true; d.step[0][i].slice = i; } });
            std::vector<MegaSynthProcessor::BsLogEntry> log; p->bsLog = &log;
            runHost (*p, bpm, 7.31, 2.0, {}, 100);
            p->bsLog = nullptr;
            const double inc = bpm / 60.0 / sr;
            bool ok = ! log.empty() && std::abs (log.front().beat - 7.5) <= inc * 1.01 && log.front().step == 30;
            for (size_t i = 0; i < log.size() && ok; ++i)
            {
                const double want = 7.5 + 0.25 * (double) i;
                ok = std::abs (log[i].beat - want) <= inc * 1.01 && log[i].step == 30 + (int64_t) i && log[i].slice == (int) ((30 + i) % 16) && log[i].fired;
            }
            CHECK (ok, "Break Sequencer locked to the host at " + juce::String (bpm) + " BPM from mid-bar (" + juce::String ((int) log.size()) + " steps, first at "
                   + (log.empty() ? juce::String ("-") : juce::String (log.front().beat, 5)) + ")");
        }
        {
            // the DAW stopped: the sequencer waits
            auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
            p->loadSampleData (demo(), "demo.wav", slot);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 16); });
            std::vector<MegaSynthProcessor::BsLogEntry> log; p->bsLog = &log;
            FakeHead head; p->setPlayHead (&head);
            juce::AudioBuffer<float> buf (2, 256);
            for (int k = 0; k < 100; ++k) { head.info.setIsPlaying (false); head.info.setBpm (170.0); head.info.setPpqPosition (4.0); juce::MidiBuffer m; p->processBlock (buf, m); }
            p->setPlayHead (nullptr); p->bsLog = nullptr;
            CHECK (log.empty(), "the Break Sequencer waits while the DAW is stopped");
        }
        // 5. pattern switching: on the next bar; keys choose the pattern; the chain
        {
            auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f); setP (*p, P_bsKeys, 1.0f);
            p->loadSampleData (demo(), "demo.wav", slot);
            p->breakStore.edit ([] (tg::BreakData& d) { for (int q = 0; q < 8; ++q) for (int i = 0; i < 16; ++i) { d.step[q][i].on = true; d.step[q][i].slice = q; } });
            std::vector<MegaSynthProcessor::BsLogEntry> log; p->bsLog = &log;
            const double bpm = 172.0, inc = bpm / 60.0 / sr;
            const int keyAt = (int) ((1.3) / inc);   // a key for pattern C, 1.3 beats into the first bar
            runHost (*p, bpm, 0.0, 4.0 * 2 * 60.0 / bpm, { { keyAt, juce::MidiMessage::noteOn (1, 24 + 2, (juce::uint8) 100) }, { keyAt + 50, juce::MidiMessage::noteOff (1, 26) } });
            p->bsLog = nullptr;
            int firstC = -1; for (auto& e : log) if (e.pattern == 2) { firstC = (int) e.step; break; }
            CHECK (p->param (P_bsPattern)->convertFrom0to1 (p->param (P_bsPattern)->getValue()) > 1.5f && firstC == 16, "a pattern key switches on the next bar (step " + juce::String (firstC) + ")");
            setP (*p, P_bsSwitch, 1.0f);   // beat
            p->breakStore.edit ([] (tg::BreakData& d) { tg::BreakStore::setChain (d, "AABA"); });
            setP (*p, P_bsChainOn, 1.0f);
            log.clear(); p->bsLog = &log;
            runHost (*p, bpm, 0.0, 16.0 * 60.0 / bpm + 0.01);
            p->bsLog = nullptr;
            juce::String seq; for (auto& e : log) if (e.step % 16 == 0) seq << (char) ('A' + e.pattern);
            CHECK (seq.startsWith ("AABA"), "chain AABA (" + seq + ")");
        }
        // 6. the tools
        {
            tg::BreakData d; d.brk[0].nSlices = 16; for (int k = 0; k <= 16; ++k) d.brk[0].start[k] = k * 1000;
            for (int k = 0; k < 16; ++k) { d.brk[0].kick[k] = (k % 8 == 0) ? 1.0f : 0.1f; d.brk[0].snare[k] = (k % 8 == 4) ? 1.0f : 0.1f; }
            tg::breaktools::original (d, 0, 32);
            bool ok = true; for (int i = 0; i < 32; ++i) ok = ok && (i % 2 == 0 ? (d.step[0][i].on && d.step[0][i].slice == i / 2) : ! d.step[0][i].on);
            CHECK (ok, "Original: the slices in order, where they fall in time");
            auto e = d; tg::breaktools::jungle (d, 1, 32, 5); tg::breaktools::jungle (e, 1, 32, 5);
            bool same = true; for (int i = 0; i < 32; ++i) same = same && d.step[1][i].slice == e.step[1][i].slice && d.step[1][i].roll == e.step[1][i].roll;
            int rolls = 0; for (int i = 0; i < 32; ++i) rolls += d.step[1][i].roll > 1 ? 1 : 0;
            CHECK (same && rolls >= 4 && d.step[1][31].slice % 8 == 4, "Jungle edit: repeatable from its seed, a snare roll at the end");
            auto f = d; tg::breaktools::shuffle (f, 0, 32, 9);
            std::multiset<int> a, b; for (int i = 0; i < 32; ++i) if (d.step[0][i].on) { a.insert (d.step[0][i].slice); b.insert (f.step[0][i].slice); }
            CHECK (a == b, "Shuffle keeps the same slices");
        }
        // 7. saved in projects and patches (with the sample and patterns), undo; old projects have none
        {
            auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcStretch, 2.0f); setP (*p, P_bsOn, 1.0f);
            p->loadSampleData (demo(), "demo.wav", slot);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::jungle (d, 3, 32, 11); d.brk[0].slice[4].pitch = 3.0f; d.brk[0].slice[4].reverse = true; tg::BreakStore::setChain (d, "ABAC"); });
            const auto json = p->breakStore.toJson();
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q->breakStore.toJson() == json && q->getSample (slot) != nullptr && q->breakStore.get().brk[0].nSlices == p->breakStore.get().brk[0].nSlices
                   && q->param (P_bcStretch)->convertFrom0to1 (q->param (P_bcStretch)->getValue()) > 1.5f, "saved in projects (sample, slices, patterns)");
            auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
            CHECK (r->breakStore.toJson() == json && r->getSample (slot) != nullptr, "saved in patches");
            p->pushHistory ("x"); p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::clear (d, 3); }); p->pushHistory ("clear"); p->undo();
            CHECK (p->breakStore.toJson() == json, "undo brings the pattern back");
            auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"15\"")
                                               .replace ("waveDataBreak=", "oldIgnored=").replace ("breakStore=", "oldIgnored2=");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
            auto o = make(); o->setStateInformation (old.getData(), (int) old.getSize());
            CHECK (o->getSample (slot) == nullptr && o->param (P_bcOn)->getValue() < 0.5f && ! o->breakStore.get().step[3][31].on, "old projects: no break, off");
            // a 35 s file is cut to 30 s
            juce::AudioBuffer<float> big (1, (int) (35.0 * 22050)); for (int i = 0; i < big.getNumSamples(); ++i) big.setSample (0, i, 0.2f * (float) std::sin (0.01 * i));
            auto l = make(); l->loadSampleData (wav16 (big, 22050.0), "long.wav", slot);
            CHECK (l->getSample (slot) != nullptr && std::abs (l->getSample (slot)->duration() - 30.0) < 0.01, "long breaks are cut to 30 s");
        }
        // 8. Mod Matrix sources, the strip, colour, the synth filter
        {
            CHECK (tg::meta (P_bcCycle).modulatable && tg::meta (P_bsSwing).modulatable && ! tg::meta (P_bcKeyBase).modulatable && tg::meta (P_bcPitch).group == MutGroup::None,
                   "Break controls: Mod Matrix destinations, not mutated");
            auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
            p->loadSampleData (demo(), "demo.wav", slot);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 16); });
            p->addRoute (MS_BsStep, P_filterCutoff, 0.5f);
            runHost (*p, 172.0, 0.0, 0.6, { { 0, juce::MidiMessage::noteOn (1, 72, (juce::uint8) 90) } });
            const float live = p->getEngine().liveOffset[(size_t) P_filterCutoff].load();
            CHECK (live > 0.01f, "Break Seq Step drives a route (" + juce::String (live, 3) + ")");
            auto level = [&] (std::function<void (MegaSynthProcessor&)> fn)
            {
                auto q = make(); quiet (*q); setP (*q, P_bcOn, 1.0f); setP (*q, P_bcSliceMode, 3.0f);
                q->loadSampleData (demo(), "demo.wav", slot); fn (*q);
                const auto out = runHost (*q, 170.0, 0.0, 0.4, { { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110) } });
                double e = 0, hi = 0; float prev = 0;
                for (float v : out) { e += (double) v * v; hi += (double) (v - prev) * (v - prev); prev = v; }
                return std::make_pair (e, hi);
            };
            const auto plain = level ([] (MegaSynthProcessor&) {});
            const auto muted = level ([] (MegaSynthProcessor& q) { setP (q, P_mxBreakMute, 1.0f); });
            const auto dark = level ([] (MegaSynthProcessor& q) { setP (q, P_bcThruFilter, 1.0f); setP (q, P_filterCutoff, 200.0f); });
            const auto sp = level ([] (MegaSynthProcessor& q) { setP (q, P_bcColour, 1.0f); });
            CHECK (plain.first > 1.0 && muted.first < plain.first * 0.01, "the Mixer's Break mute (" + juce::String (plain.first, 2) + " / " + juce::String (muted.first, 4) + ")");
            CHECK (dark.second < plain.second * 0.3, "through the synth's filter (off by default)");
            CHECK (sp.first > 1.0 && sp.second != plain.second, "SP-1200 colour changes the sound");
            auto sol = make(); setP (*sol, P_bcOn, 1.0f); setP (*sol, P_mxBreakSolo, 1.0f);
            const auto st2 = render (*sol, 0.3, chord (0.0, 0.25, { 72 }));
            CHECK (st2.peak < 1e-6f, "Break soloed: the synth's notes are muted");
        }
        // 9. the factory presets with the synthesised break
        for (int i = 32; i < (int) tg::factoryPresets().size(); ++i)
        {
            if (i >= 36 && tg::factoryMeta (i).category != "Breaks & Drums") continue;   // (the bank's break presets too)
            auto p = make(); p->loadFactoryPreset (i);
            const auto d = p->breakStore.get();
            bool anyStep = false; for (auto& st : d.step[0]) anyStep = anyStep || st.on;
            const auto out = runHost (*p, 172.0, 0.0, 2.0, { { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) } });
            float pk = 0; bool finite = true; for (float v : out) { pk = std::max (pk, std::abs (v)); finite = finite && std::isfinite (v); }
            CHECK (p->getSample (slot) != nullptr && d.brk[0].nSlices == 16 && finite && pk > 0.05f && pk < 1.5f && (i == 34 || anyStep || p->param (P_bsOn)->getValue() < 0.5f),
                   juce::String ("factory preset ") + tg::factoryPresets()[(size_t) i].name + " plays (" + juce::String (pk, 2) + ")");
        }
        // 11. three breaks: the shared zone, the Break Select keys, levels, layers, Frankenbreak, saving
        {
            // three steady tones as three "breaks" (one bar at 120 BPM, Equal 4): 440 / 660 / 880 Hz
            auto three = [&] (MegaSynthProcessor& p)
            {
                quiet (p);
                setP (p, P_bcOn, 1.0f); setP (p, P_bcStretch, 0.0f); setP (p, P_bcVelSens, 0.0f); setP (p, P_bcLevel, 1.0f);
                for (int k = 0; k < 3; ++k)
                {
                    const auto bp = tg::breakParams (k);
                    setP (p, bp.tempo, 120.0f); setP (p, bp.sliceMode, 1.0f); setP (p, bp.stretch, 0.0f);
                    p.loadSampleData (sineBreak (440.0 * (1.0 + 0.5 * k), 2.0), "tone" + juce::String (k) + ".wav", MegaSynthProcessor::breakSlot (k));
                    p.breakStore.edit ([k] (tg::BreakData& d) { for (auto& sl : d.brk[k].slice) sl.loop = true; });
                }
            };
            auto hzOf = [&] (const std::vector<float>& x, int a, int b) { return zc (x, a, b); };
            {
                auto p = make(); three (*p);
                setP (*p, P_bcActive, 2.0f);
                const auto out = runHost (*p, 120.0, 0.0, 0.5, { { 256, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) } });
                CHECK (std::abs (hzOf (out, 2000, 20000) / 660.0 - 1.0) < 0.01, "the shared zone plays the Active Break (Break 2: " + juce::String (hzOf (out, 2000, 20000), 1) + " Hz)");
            }
            {
                // A0 / A#0 / B0 choose the break (and aren't played); a held key keeps its slice across the switch
                auto p = make(); three (*p);
                const auto out = runHost (*p, 120.0, 0.0, 1.0, { { 256, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) },
                                                                { 12000, juce::MidiMessage::noteOn (1, 35, (juce::uint8) 100) },
                                                                { 24000, juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100) },
                                                                { 36000, juce::MidiMessage::noteOff (1, 36) }, { 36000, juce::MidiMessage::noteOff (1, 37) } });
                const double heldHz = hzOf (out, 14000, 22000);
                CHECK (p->param (P_bcActive)->convertFrom0to1 (p->param (P_bcActive)->getValue()) > 2.5f, "B0 makes Break 3 the Active Break");
                CHECK (std::abs (heldHz / 440.0 - 1.0) < 0.01, "a held key keeps its slice after the switch (" + juce::String (heldHz, 1) + " Hz)");
                float after = 0; for (int i = 40000; i < 47000; ++i) after = std::max (after, std::abs (out[(size_t) i]));
                CHECK (after < 1e-4f, "letting go stops the looping slices (by key, whichever break they came from)");
                // the second key, pressed after the switch, played Break 3: both tones from 24000
                juce::AudioBuffer<float> none;
                (void) none;
            }
            {
                // On the Beat: the switch waits for the next beat
                auto p = make(); three (*p); setP (*p, P_bcSelQuant, 1.0f);
                const double inc = 120.0 / 60.0 / sr;
                FakeHead head; p->setPlayHead (&head);
                double ppq = 0.3; juce::AudioBuffer<float> buf (2, 64);
                double changedAt = -1.0;
                for (int pos = 0; pos < (int) (sr * 0.8); pos += 64)
                {
                    head.info.setIsPlaying (true); head.info.setBpm (120.0); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (0.0);
                    juce::MidiBuffer m; if (pos == 64) m.addEvent (juce::MidiMessage::noteOn (1, 34, (juce::uint8) 100), 10);
                    buf.clear(); p->processBlock (buf, m);
                    if (changedAt < 0 && p->param (P_bcActive)->convertFrom0to1 (p->param (P_bcActive)->getValue()) > 1.5f) changedAt = ppq;
                    ppq += 64 * inc;
                }
                p->setPlayHead (nullptr);
                CHECK (changedAt > 0.98 && changedAt < 1.0 + 64 * inc * 1.01, "On the Beat: Break 2 comes in on the next beat (" + juce::String (changedAt, 4) + ")");
            }
            {
                // the three level faders; one strip for all
                auto lvl = [&] (int active, float level)
                {
                    auto p = make(); three (*p); setP (*p, P_bcActive, (float) active); setP (*p, tg::breakParams (active - 1).level, level);
                    const auto out = runHost (*p, 120.0, 0.0, 0.4, { { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) } });
                    float pk = 0; for (size_t i = 4000; i < out.size(); ++i) pk = std::max (pk, std::abs (out[i]));
                    return pk;
                };
                const float full = lvl (2, 1.0f), half = lvl (2, 0.5f);
                CHECK (std::abs (half / full - 0.5f) < 0.02f, "Break 2's level fader (" + juce::String (half / full, 3) + ")");
                auto m = make(); three (*m); setP (*m, P_mxBreakMute, 1.0f); setP (*m, P_bcActive, 3.0f);
                const auto out = runHost (*m, 120.0, 0.0, 0.3, { { 0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100) } });
                float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v));
                CHECK (pk < 1e-6f, "one Break strip mutes all three");
            }
            {
                // no clicks when the Active Break changes while slices play
                auto p = make(); three (*p);
                for (int k = 0; k < 3; ++k) p->breakStore.edit ([k] (tg::BreakData& d) { for (auto& sl : d.brk[k].slice) sl.loop = false; });
                std::vector<std::pair<int, juce::MidiMessage>> ev;
                for (int i = 0; i < 12; ++i)
                {
                    ev.push_back ({ 300 + i * 3000, juce::MidiMessage::noteOn (1, 36 + (i % 4), (juce::uint8) 100) });
                    ev.push_back ({ 1800 + i * 3000, juce::MidiMessage::noteOn (1, 33 + (i % 3), (juce::uint8) 100) });
                }
                const auto out = runHost (*p, 120.0, 0.0, 0.8, ev);
                float worst = 0, pk = 0;
                for (size_t i = 1; i < out.size(); ++i) { worst = std::max (worst, std::abs (out[i] - out[i - 1])); pk = std::max (pk, std::abs (out[i])); }
                CHECK (pk > 0.05f && worst < pk * 0.12f, "no clicks switching breaks while playing (" + juce::String (worst / std::max (1e-6f, pk), 3) + ")");
            }
            {
                // the sequencer: steps pinned to a break, "Active" steps, Alternate, layers, a pattern's own break
                auto p = make(); three (*p); setP (*p, P_bsOn, 1.0f);
                p->breakStore.edit ([] (tg::BreakData& d)
                {
                    for (int i = 0; i < 16; ++i) { d.step[0][i].on = true; d.step[0][i].slice = 0; }
                    d.step[0][1].brk = 3;
                    d.step[0][2].layer = true; d.step[0][2].layerBrk = 2;
                    d.patBreak[1] = 3;
                });
                setP (*p, P_bsAlternate, 1.0f); setP (*p, P_bsAltBreak, 1.0f);   // every other bar: Break 2
                std::vector<MegaSynthProcessor::BsLogEntry> log; p->bsLog = &log;
                runHost (*p, 120.0, 0.0, 4.0 * 2 * 60.0 / 120.0 + 0.01, {}, 64);
                p->bsLog = nullptr;
                bool ok = log.size() >= 32;
                for (auto& e : log)
                {
                    const int idx = (int) (e.step % 16), bar = (int) (e.step / 16);
                    const int want = idx == 1 ? 2 : (bar % 2 == 1 ? 1 : 0);
                    ok = ok && e.brk == want;
                }
                CHECK (ok, "steps play their break: pinned, Active, and Alternate every other bar");
                // a layered step sounds two hits at once
                auto q = make(); three (*q); setP (*q, P_bsOn, 1.0f);
                q->breakStore.edit ([] (tg::BreakData& d) { d.step[0][0].on = true; d.step[0][0].layer = true; d.step[0][0].layerBrk = 3; });
                FakeHead head; q->setPlayHead (&head);
                head.info.setIsPlaying (true); head.info.setBpm (120.0); head.info.setPpqPosition (0.0); head.info.setPpqPositionOfLastBarStart (0.0);
                juce::AudioBuffer<float> buf (2, 64); juce::MidiBuffer m; q->processBlock (buf, m);
                q->setPlayHead (nullptr);
                CHECK (q->getEngine().breaks().activeVoices() == 2, "a layered step plays both hits");
                // switching to pattern B makes its break the Active Break
                auto r = make(); three (*r); setP (*r, P_bsOn, 1.0f); setP (*r, P_bsSwitch, 0.0f);
                r->breakStore.edit ([] (tg::BreakData& d) { d.patBreak[1] = 3; d.step[1][0].on = true; });
                setP (*r, P_bsPattern, 1.0f);
                runHost (*r, 120.0, 0.0, 0.3);
                CHECK (r->param (P_bcActive)->convertFrom0to1 (r->param (P_bcActive)->getValue()) > 2.5f, "a pattern's own break becomes the Active Break");
            }
            {
                // Frankenbreak: kicks, snares and hats each from a different break, matching kind for kind
                auto p = make(); setP (*p, P_bcOn, 1.0f);
                for (int k = 0; k < 3; ++k)
                {
                    setP (*p, tg::breakParams (k).sliceMode, 3.0f);
                    p->loadSampleData (wav16 (tg::breaks::makeDemoBreak (44100.0, 170.0 - 3 * k, k), 44100.0), "b" + juce::String (k) + ".wav", MegaSynthProcessor::breakSlot (k));
                }
                auto d = p->breakStore.get();
                auto kindOf = [] (const tg::BreakSlot& b, int k) { return b.kick[k] >= b.snare[k] && b.kick[k] >= b.hat[k] ? 0 : (b.snare[k] >= b.hat[k] ? 1 : 2); };
                tg::breaktools::original (d, 0, 32, 0, true);
                auto f = d; tg::breaktools::frankenbreak (f, 0, 32, 3, 1.0f, 0);
                int swapped = 0, matched = 0; std::set<int> fromBreaks;
                for (int i = 0; i < 32; ++i)
                {
                    const auto& o = d.step[0][i]; const auto& n = f.step[0][i];
                    if (! o.on || n.brk == 1) continue;
                    ++swapped; fromBreaks.insert (n.brk);
                    if (kindOf (d.brk[0], o.slice) == kindOf (f.brk[n.brk - 1], n.slice)) ++matched;
                }
                std::cout << "  Frankenbreak: " << swapped << " hits swapped, " << matched << " the same kind, from " << (int) fromBreaks.size() << " other breaks" << std::endl;
                CHECK (swapped >= 6 && matched == swapped && fromBreaks.size() >= 1, "Frankenbreak takes kicks / snares / hats from the other breaks, kind for kind");
                auto g = d; tg::breaktools::frankenbreak (g, 0, 32, 3, 1.0f, 0);
                bool same = true; for (int i = 0; i < 32; ++i) same = same && g.step[0][i].slice == f.step[0][i].slice && g.step[0][i].brk == f.step[0][i].brk;
                CHECK (same, "Frankenbreak is repeatable from its seed");
                auto sw = d; tg::breaktools::swapBreak (sw, 0, 32, 2);
                bool all3 = true; for (int i = 0; i < 32; ++i) if (sw.step[0][i].on) all3 = all3 && sw.step[0][i].brk == 3 && sw.step[0][i].slice == d.step[0][i].slice % d.brk[2].nSlices;
                CHECK (all3, "Swap Break: the same pattern on Break 3");
                // Match Levels
                setP (*p, P_bcLevel2, 1.0f);
                CHECK (p->matchBreakLevels(), "Match Levels");
                double lo = 1e9, hi = 0;
                for (int k = 0; k < 3; ++k) { const double l = p->param (tg::breakParams (k).level)->convertFrom0to1 (p->param (tg::breakParams (k).level)->getValue()) * p->breakStore.get().brk[k].rms; lo = std::min (lo, l); hi = std::max (hi, l); }
                CHECK (hi / lo < 1.05, "Match Levels evens out the breaks' loudness");
                // saved: the three samples, their slices and settings, the patterns' breaks and layers
                p->breakStore.edit ([] (tg::BreakData& x) { x.brk[2].slice[3].pitch = 5.0f; x.brk[1].nMarkers = 1; x.brk[1].markers[0] = 0.3f; x.step[2][4].on = true; x.step[2][4].brk = 2;
                                                            x.step[2][4].layer = true; x.step[2][4].layerBrk = 3; x.step[2][4].layerSlice = 7; x.patBreak[2] = 2; });
                setP (*p, P_bcActive, 3.0f); setP (*p, P_bcColour3, 2.0f);
                const auto json = p->breakStore.toJson();
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
                bool samples = true; for (int k = 0; k < 3; ++k) samples = samples && q->getSample (MegaSynthProcessor::breakSlot (k)) != nullptr && q->breakStore.get().brk[k].nSlices == 16;
                CHECK (samples && q->breakStore.toJson() == json && q->param (P_bcActive)->convertFrom0to1 (q->param (P_bcActive)->getValue()) > 2.5f
                       && q->param (P_bcColour3)->convertFrom0to1 (q->param (P_bcColour3)->getValue()) > 1.5f, "three breaks saved in projects");
                auto r = make(); r->importBrowserPatch (p->exportBrowserPatch());
                samples = true; for (int k = 0; k < 3; ++k) samples = samples && r->getSample (MegaSynthProcessor::breakSlot (k)) != nullptr;
                CHECK (samples && r->breakStore.toJson() == json, "three breaks saved in patches");
                // an older project (one break): Break 1, active, full level, the select keys off
                auto x = getXmlFromBinaryForTest (mb).replace ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\"", "stateVersion=\"16\"")
                                                   .replace ("waveDataBreak2=", "o1=").replace ("waveDataBreak3=", "o2=");
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (x), old);
                auto o = make(); o->setStateInformation (old.getData(), (int) old.getSize());
                CHECK (o->getSample (MegaSynthProcessor::breakSlot (0)) != nullptr && o->getSample (MegaSynthProcessor::breakSlot (1)) == nullptr
                       && std::abs (o->param (P_bcActive)->convertFrom0to1 (o->param (P_bcActive)->getValue()) - 1.0f) < 0.01f
                       && std::abs (o->param (P_bcLevel1)->convertFrom0to1 (o->param (P_bcLevel1)->getValue()) - 1.0f) < 0.001f
                       && o->param (P_bcSelKeys)->getValue() < 0.5f, "older projects: their break is Break 1, active, full level; select keys off");
                // the warning for three big breaks
                CHECK (! tgui::breakInfoText (*p, 0).contains ("MB to the project"), "no size warning for small breaks");
            }
        }
        // 12. chop mode: taps while the break plays become markers, where they were hit
        {
            auto chopRun = [&] (MegaSynthProcessor& p, std::vector<int> taps, double seconds)
            {
                p.breakPreviewReq.store (0.0);   // (the editor asks for this when chop mode goes on)
                std::vector<std::pair<int, juce::MidiMessage>> ev;
                for (int t : taps) { ev.push_back ({ t, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }); ev.push_back ({ t + 200, juce::MidiMessage::noteOff (1, 60) }); }
                juce::AudioBuffer<float> buf (2, 128);
                for (int pos = 0; pos < (int) (seconds * sr); pos += 128)
                {
                    juce::MidiBuffer m; for (auto& e : ev) if (e.first >= pos && e.first < pos + 128) m.addEvent (e.second, e.first - pos);
                    buf.clear(); p.processBlock (buf, m);
                }
                p.applyChopTaps();
            };
            const double fileRate = 44100.0 / sr;
            {
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcChopMode, 1.0f); setP (*p, P_bcChopSnapTr, 0.0f); setP (*p, P_bcChopSnapZero, 0.0f);
                setP (*p, P_bcChopLatency, 0.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bcSliceMode, 5.0f); p->clearMarkers (0);
                const std::vector<int> taps { 5000, 21000, 40007, 61111 };
                chopRun (*p, taps, 1.5);
                const auto d = p->breakStore.get();
                bool ok = d.brk[0].nMarkers == (int) taps.size();
                for (size_t i = 0; i < taps.size() && ok; ++i)
                    ok = std::abs (d.brk[0].markers[i] * d.brk[0].start[d.brk[0].nSlices] - taps[i] * fileRate) < 0.001 * 44100.0;
                CHECK (ok, "chop taps land where they were hit (within 1 ms; " + juce::String (d.brk[0].nMarkers) + " markers)");
                CHECK (d.brk[0].nSlices == (int) taps.size() + 1, "each tap makes a slice");
                CHECK (p->undoLastChop() && p->breakStore.get().brk[0].nMarkers == (int) taps.size() - 1, "Undo chop removes the last tap");
            }
            {
                // latency: a tap 25 ms after a hit, with 25 ms latency compensation, marks the hit
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcChopMode, 1.0f); setP (*p, P_bcChopSnapTr, 0.0f); setP (*p, P_bcChopSnapZero, 0.0f);
                setP (*p, P_bcChopLatency, 25.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bcSliceMode, 5.0f); p->clearMarkers (0);
                chopRun (*p, { 30000 }, 1.0);
                const auto d = p->breakStore.get();
                const double got = d.brk[0].markers[0] * d.brk[0].start[d.brk[0].nSlices], want = (30000 - 0.025 * sr) * fileRate;
                CHECK (d.brk[0].nMarkers == 1 && std::abs (got - want) < 0.001 * 44100.0, "tap latency is taken off (" + juce::String ((got - want) / 44.1, 3) + " ms)");
            }
            {
                // snap: taps a little late land on the transient
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcChopMode, 1.0f); setP (*p, P_bcChopLatency, 0.0f); setP (*p, P_bcChopSnapZero, 0.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bcSliceMode, 5.0f); p->clearMarkers (0);
                const double st16 = 60.0 / 170.0 / 4.0;
                std::vector<int> taps;
                for (int s16 : { 4, 10, 12 }) taps.push_back ((int) ((s16 * st16 + 0.009) * sr / 1.0));   // 9 ms late
                chopRun (*p, taps, 2.0);
                const auto d = p->breakStore.get();
                bool ok = d.brk[0].nMarkers == 3;
                for (int i = 0; i < d.brk[0].nMarkers && ok; ++i)
                {
                    const double t = d.brk[0].markers[i] * d.brk[0].start[d.brk[0].nSlices] / 44100.0;
                    double best = 1.0; for (int s16 : { 4, 10, 12 }) best = std::min (best, std::abs (t - s16 * st16));
                    ok = best < 0.0075;   // (the detector marks the start of the rise, a few ms before the peak; the tap was 9 ms late)
                }
                juce::String got; for (int i = 0; i < d.brk[0].nMarkers; ++i) got << juce::String (d.brk[0].markers[i] * d.brk[0].start[d.brk[0].nSlices] / 44100.0 / st16, 3) << " ";
                CHECK (ok, "Snap to transients: taps 9 ms late land on the hits (at 16ths " + got + ")");
            }
            {
                // chop mode keeps the keys to itself; Clear / Snap all / markers from slices
                auto p = make(); setP (*p, P_bcOn, 1.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bcSliceMode, 3.0f); p->refreshBreakAnalysis();
                p->markersFromSlices (0);
                auto d = p->breakStore.get();
                CHECK (d.brk[0].nMarkers == 15 && juce::roundToInt (p->param (P_bcSliceMode)->convertFrom0to1 (p->param (P_bcSliceMode)->getValue())) == 5 && d.brk[0].nSlices == 16,
                       "chop mode starts from the slices there are (16 slices -> 15 markers)");
                p->snapAllMarkers (0);
                d = p->breakStore.get();
                CHECK (d.brk[0].nMarkers >= 8 && d.brk[0].nMarkers <= 15, "Snap all to transients (" + juce::String (d.brk[0].nMarkers) + " markers)");
                p->clearMarkers (0);
                CHECK (p->breakStore.get().brk[0].nSlices == 1, "Clear markers: one slice");
            }
            {
                // the page: click adds a marker, double-click splits a slice, right-click and Delete remove one
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcChopSnapTr, 0.0f); setP (*p, P_bcChopSnapZero, 0.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bcSliceMode, 5.0f); p->clearMarkers (0);
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                ed->setSize (1200, 890);
                auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
                e->showPage ("Break Chopper"); e->timerCallbackForTests();
                auto* wv = dynamic_cast<tgui::SampleWaveView*> (e->breakWaveForTests);
                CHECK (wv != nullptr, "the wave view");
                if (wv != nullptr)
                {
                    auto src = juce::Desktop::getInstance().getMainMouseSource();
                    auto click = [&] (float frac, juce::ModifierKeys mods, int clicks)
                    {
                        const float x = 4.0f + frac * (wv->getWidth() - 8.0f), y = 60.0f;
                        juce::MouseEvent me (src, { x, y }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, wv, wv, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), clicks, false);
                        if (clicks >= 2) wv->mouseDoubleClick (me); else wv->mouseDown (me);
                    };
                    auto nm = [&] { return p->breakStore.get().brk[0].nMarkers; };
                    click (0.25f, {}, 1); click (0.5f, {}, 1);
                    CHECK (nm() == 2, "a click adds a marker (" + juce::String (nm()) + ")");
                    click (0.75f, {}, 2);
                    const auto d = p->breakStore.get();
                    CHECK (nm() == 3 && std::abs (d.brk[0].markers[2] - 0.75f) < 0.01f, "a double-click splits a slice in half");
                    click (0.26f, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::popupMenuClickModifier), 1);
                    CHECK (nm() == 2, "a right-click removes the nearest marker");
                    click (p->breakStore.get().brk[0].markers[0], {}, 1);   // select it ...
                    wv->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
                    CHECK (nm() == 1, "Delete removes the selected marker");
                }
            }
        }
        // 13. zoom and moving markers
        {
            auto setup = [&] (std::unique_ptr<MegaSynthProcessor>& p)
            {
                p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcChopSnapTr, 0.0f); setP (*p, P_bcChopSnapZero, 0.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bcSliceMode, 5.0f); p->clearMarkers (0);
                for (double t : { 0.3, 0.6, 0.9 }) p->addChopMarker (0, t * 44100.0, false);
            };
            auto frameOf = [] (MegaSynthProcessor& p, int i) { const auto d = p.breakStore.get(); return (double) d.brk[0].markers[i] * d.brk[0].start[d.brk[0].nSlices]; };
            {
                std::unique_ptr<MegaSynthProcessor> p; setup (p);
                const auto* w = p->getSample (slot);
                // snap modes
                setP (*p, P_bcMarkerSnap, 0.0f);
                const double none = p->moveMarker (0, 1, 26501.3, true);
                CHECK (std::abs (none - 26501.3) < 0.01, "Marker Snap none: the marker goes where it's dragged");
                setP (*p, P_bcMarkerSnap, 1.0f);
                const double z = p->moveMarker (0, 1, 26501.0, true);
                const int zi = (int) std::round (z);
                const bool zc = zi > 0 && zi < w->length - 1 && ((w->ch[0][(size_t) zi - 1] <= 0.0f) != (w->ch[0][(size_t) zi] <= 0.0f) || std::abs (w->ch[0][(size_t) zi]) < 1e-4f);
                CHECK (zc && std::abs (z - 26501.0) < 0.002 * 44100.0, "Marker Snap zero crossing (" + juce::String (z - 26501.0, 1) + " samples away)");
                setP (*p, P_bcMarkerSnap, 2.0f);
                const double st16 = 60.0 / 170.0 / 4.0 * 44100.0;
                const double tr = p->moveMarker (0, 1, 10 * st16 + 0.008 * 44100.0, true);   // 8 ms after the snare on 16th 10
                CHECK (std::abs (tr - 10 * st16) < 0.0075 * 44100.0, "Marker Snap transient: lands on the hit (" + juce::String ((tr - 10 * st16) / 44.1, 2) + " ms)");
                for (int mode = 3; mode <= 5; ++mode)
                {
                    setP (*p, P_bcMarkerSnap, (float) mode);
                    const double grid = (mode == 3 ? 0.5 : mode == 4 ? 0.25 : 0.125) * 60.0 / 170.0 * 44100.0;
                    const double n = std::floor (30000.0 / grid);   // (between its neighbours)
                    const double gf = p->moveMarker (0, 1, (n + 0.3) * grid, true);
                    CHECK (std::abs (gf - n * grid) < 1.0, "Marker Snap grid " + juce::String (mode == 3 ? "1/8" : mode == 4 ? "1/16" : "1/32") + " at the break's tempo");
                }
                // neighbours never cross; slice 1 always starts at 0
                setP (*p, P_bcMarkerSnap, 0.0f);
                const double next = frameOf (*p, 2);
                const double past = p->moveMarker (0, 1, next + 5000.0, true);
                CHECK (past < next && std::abs (past - (next - MegaSynthProcessor::kMarkerGap)) < 1.0 && frameOf (*p, 2) == next, "a dragged marker stops before its neighbour");
                const double before = p->moveMarker (0, 0, -9999.0, true);
                const auto d = p->breakStore.get();
                CHECK (before >= MegaSynthProcessor::kMarkerGap - 0.5 && d.brk[0].start[0] == 0 && d.brk[0].nMarkers == 3, "the first marker stops after the start; slice 1 starts at 0");
                bool sorted = true; for (int i = 1; i < d.brk[0].nMarkers; ++i) sorted = sorted && d.brk[0].markers[i] > d.brk[0].markers[i - 1];
                CHECK (sorted, "markers stay in order");
            }
            {
                // one undo step per drag; saved with the patch
                std::unique_ptr<MegaSynthProcessor> p; setup (p);
                p->pushHistory ("Edit");
                const double orig = frameOf (*p, 1);
                const auto v0 = p->breakStore.getVersion();
                for (int i = 1; i <= 20; ++i) p->moveMarker (0, 1, orig + i * 50.0, false);   // the drag
                CHECK (p->breakStore.getVersion() == v0, "dragging makes no undo steps until it ends");
                p->commitMarkerEdit();
                CHECK (p->breakStore.getVersion() == v0 + 1, "the end of a drag makes one");
                p->pushHistory ("Edit");
                const double moved = frameOf (*p, 1);
                CHECK (std::abs (moved - (orig + 1000.0)) < 1.0, "the marker moved by the drag");
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
                CHECK (std::abs (frameOf (*q, 1) - moved) < 1.0, "a moved marker is saved and loaded");
                p->undo();
                CHECK (std::abs (frameOf (*p, 1) - orig) < 1.0, "one Undo puts the marker back where the drag started");
                // the arrow keys
                const double f0 = frameOf (*q, 1);
                q->nudgeMarker (0, 1, 10.0);
                CHECK (std::abs (frameOf (*q, 1) - (f0 + 441.0)) < 1.0, "Shift+arrow nudges 10 ms");
                q->nudgeMarker (0, 1, -1.0);
                CHECK (std::abs (frameOf (*q, 1) - (f0 + 441.0 - 44.1)) < 1.0, "an arrow nudges 1 ms");
                // trim
                q->setSliceTrim (0, 1, false, 0.2f); q->setSliceTrim (0, 1, true, 0.1f);
                const auto ds = q->breakStore.get().brk[0].slice[1];
                CHECK (std::abs (ds.trimStart - 0.2f) < 1e-5f && std::abs (ds.trimEnd - 0.25f) < 1e-5f, "trim handles keep the end after the start");
            }
            {
                // the page: zoom to 1 sample per pixel at most; dragging a marker (and a slice start in Equal mode)
                std::unique_ptr<MegaSynthProcessor> p; setup (p);
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                ed->setSize (1200, 890);
                auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
                e->showPage ("Break Chopper"); e->timerCallbackForTests();
                auto* wv = dynamic_cast<tgui::SampleWaveView*> (e->breakWaveForTests);
                CHECK (wv != nullptr, "the wave view (zoom)");
                if (wv != nullptr)
                {
                    for (int i = 0; i < 40; ++i) wv->zoomAround (0.5, 0.5);
                    CHECK (wv->samplesPerPixel() >= 0.99 && wv->samplesPerPixel() < 1.01, "full zoom: 1 sample per pixel (" + juce::String (wv->samplesPerPixel(), 3) + ")");
                    wv->setView (0.0, 1.0);
                    CHECK (wv->viewLen == 1.0 && wv->viewStart == 0.0, "whole break");
                    auto src = juce::Desktop::getInstance().getMainMouseSource();
                    auto ev = [&] (float x, juce::ModifierKeys mods) { return juce::MouseEvent (src, { x, 60.0f }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, wv, wv, juce::Time::getCurrentTime(), { x, 60.0f }, juce::Time::getCurrentTime(), 1, false); };
                    setP (*p, P_bcMarkerSnap, 0.0f);
                    const auto v0 = p->breakStore.getVersion();
                    const double len = p->breakStore.get().brk[0].start[p->breakStore.get().brk[0].nSlices];
                    const float x0 = wv->xFor (frameOf (*p, 1) / len);
                    wv->mouseDown (ev (x0, {}));
                    for (int i = 1; i <= 10; ++i) wv->mouseDrag (ev (x0 + i * 4.0f, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
                    wv->mouseUp (ev (x0 + 40.0f, {}));
                    CHECK (std::abs (frameOf (*p, 1) / len - wv->fracAtX (x0 + 40.0f)) < 0.0005 && p->breakStore.getVersion() == v0 + 1 && p->breakStore.get().brk[0].nMarkers == 3,
                           "dragging a marker on the wave moves it (one undo step)");
                    // Equal mode: dragging a slice start makes Manual markers first
                    setP (*p, P_bcSliceMode, 3.0f); p->refreshBreakAnalysis(); e->timerCallbackForTests();
                    const auto d = p->breakStore.get();
                    const float xs = wv->xFor ((double) d.brk[0].start[4] / len);
                    wv->mouseDown (ev (xs, {}));
                    for (int i = 1; i <= 5; ++i) wv->mouseDrag (ev (xs + i * 2.0f, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
                    wv->mouseUp (ev (xs + 10.0f, {}));
                    const auto d2 = p->breakStore.get();
                    CHECK (juce::roundToInt (p->param (P_bcSliceMode)->convertFrom0to1 (p->param (P_bcSliceMode)->getValue())) == 5 && d2.brk[0].nMarkers == 15 && d2.brk[0].nSlices == 16
                           && std::abs ((double) d2.brk[0].start[4] / len - wv->fracAtX (xs + 10.0f)) < 0.0005,
                           "dragging a slice start in Equal mode switches to Manual markers and moves it");
                    // a click on a marker (no drag) changes nothing
                    const auto v1 = p->breakStore.getVersion();
                    const float xm = wv->xFor ((double) d2.brk[0].start[8] / len);
                    wv->mouseDown (ev (xm, {})); wv->mouseUp (ev (xm, {}));
                    CHECK (p->breakStore.getVersion() == v1 && p->breakStore.get().brk[0].nMarkers == 15, "a click on a marker just selects it");
                }
            }
        }
        // 14. the Stage 4 pattern tools, and per-hit / per-slice stretch
        {
            auto stepsEqual = [] (const tg::BreakData& x, const tg::BreakData& y, int pat)
            {
                for (int i = 0; i < 32; ++i)
                {
                    const auto &a = x.step[pat][i], &b = y.step[pat][i];
                    if (a.on != b.on || a.slice != b.slice || a.brk != b.brk || a.roll != b.roll || a.vel != b.vel || a.pitch != b.pitch || a.reverse != b.reverse || a.prob != b.prob || a.nudge != b.nudge) return false;
                }
                return true;
            };
            // three analysed breaks: kicks on slices 0 and 8, snares on 4 and 12, the rest hats
            tg::BreakData base;
            for (int b = 0; b < 3; ++b)
            {
                auto& x = base.brk[b]; x.nSlices = 16; x.bars = 1; x.origBpm = 170.0;
                for (int k = 0; k <= 16; ++k) x.start[k] = k * 1000;
                for (int k = 0; k < 16; ++k) { x.kick[k] = (k % 8 == 0) ? 1.0f : 0.05f; x.snare[k] = (k % 8 == 4) ? 1.0f : 0.05f; x.hat[k] = (k % 2 == 1) ? 0.6f : 0.1f; }
            }
            tg::breaktools::original (base, 0, 32);
            auto kindAt = [] (const tg::BreakData& d, int i) { return tg::breaktools::stepKind (d, d.step[0][i], 0); };
            juce::StringArray notRepeatable, noKick;
            for (int t = 0; t < tg::breaktools::kNumTools; ++t)
            {
                for (bool all : { false, true })
                {
                    auto a = base, b = base, c = base;
                    tg::breaktools::applyTool (t, a, 0, 32, 7, 0.7f, 0, all);
                    tg::breaktools::applyTool (t, b, 0, 32, 7, 0.7f, 0, all);
                    if (! stepsEqual (a, b, 0)) notRepeatable.add (tg::breaktools::toolName (t));
                    tg::breaktools::applyTool (t, c, 0, 32, 7, 0.0f, 0, all);   // (Amount 0 runs too)
                    if (t == tg::breaktools::ToolTwoStep || t == tg::breaktools::ToolCutUp || t == tg::breaktools::ToolHalftime)
                        for (int bar : { 0, 16 })
                            if (! a.step[0][bar].on || kindAt (a, bar) != tg::breaktools::Kick) noKick.add (juce::String (tg::breaktools::toolName (t)) + (all ? " (all)" : ""));
                }
            }
            CHECK (notRepeatable.isEmpty(), "every new tool is repeatable from its seed" + (notRepeatable.isEmpty() ? juce::String() : ": " + notRepeatable.joinIntoString (", ")));
            CHECK (noKick.isEmpty(), "Two-Step, Cut-up and Halftime keep a kick on 1" + (noKick.isEmpty() ? juce::String() : ": " + noKick.joinIntoString (", ")));
            {
                auto a = base; tg::breaktools::applyTool (tg::breaktools::ToolTwoStep, a, 0, 32, 3, 1.0f, 0, false);
                bool ok = true;
                for (int b0 : { 0, 16 })
                {
                    ok = ok && kindAt (a, b0 + 6) == tg::breaktools::Kick && kindAt (a, b0 + 4) == tg::breaktools::Snare && kindAt (a, b0 + 12) == tg::breaktools::Snare;
                    for (int o = 1; o < 16; ++o) if (o != 6 && a.step[0][b0 + o].on && kindAt (a, b0 + o) == tg::breaktools::Kick) ok = false;
                }
                CHECK (ok, "Two-Step: kicks on 1 and the 'and' of 2 only, snares on 2 and 4");
                auto h = base; tg::breaktools::applyTool (tg::breaktools::ToolHalftime, h, 0, 32, 3, 1.0f, 0, false);
                bool ht = true;
                for (int b0 : { 0, 16 }) for (int o = 0; o < 16; ++o) if (h.step[0][b0 + o].on && kindAt (h, b0 + o) == tg::breaktools::Snare) ht = ht && o == 8;
                CHECK (ht && h.step[0][8].on && h.step[0][24].on, "Halftime: the snare on 3 only");
                auto cu = base; tg::breaktools::applyTool (tg::breaktools::ToolCutUp, cu, 0, 32, 11, 1.0f, 0, false);
                CHECK (kindAt (cu, 4) == tg::breaktools::Snare && kindAt (cu, 12) == tg::breaktools::Snare && kindAt (cu, 20) == tg::breaktools::Snare && ! stepsEqual (cu, base, 0),
                       "Cut-up: 1/8s move, the snares on 2 and 4 stay");
                auto sb = base; tg::breaktools::applyTool (tg::breaktools::ToolSnareBuild, sb, 0, 32, 4, 1.0f, 0, false, true);   // (the old Snare Build, kept for older projects' fills)
                const auto& l = sb.step[0];
                CHECK (l[16].roll == 2 && l[31].roll == 4 && l[24].roll == 3 && l[31].pitch > l[16].pitch && kindAt (sb, 31) == tg::breaktools::Snare, "Snare Build: x2, x3, x4, rising");
                auto th = base; tg::breaktools::applyTool (tg::breaktools::ToolThinOut, th, 0, 32, 4, 1.0f, 0, false);
                int before = 0, after = 0; for (int i = 0; i < 32; ++i) { before += base.step[0][i].on ? 1 : 0; after += (th.step[0][i].on && th.step[0][i].prob >= 1.0f) ? 1 : 0; }
                CHECK (after < before && th.step[0][0].on && th.step[0][4].on, "Thin Out: fewer sure hits, the kick on 1 and the backbeat stay");
                auto hs = base; tg::breaktools::applyTool (tg::breaktools::ToolHatShuffle, hs, 0, 32, 4, 1.0f, 0, false);
                bool onlyHats = true; for (int i = 0; i < 32; ++i) if (hs.step[0][i].slice != base.step[0][i].slice) onlyHats = onlyHats && kindAt (base, i) == tg::breaktools::Hat && kindAt (hs, i) == tg::breaktools::Hat;
                CHECK (onlyHats, "Hat Shuffle moves only hats");
                auto fr = base; tg::breaktools::applyTool (tg::breaktools::ToolRollers, fr, 0, 32, 4, 1.0f, 0, true);
                int other = 0; for (int i = 0; i < 32; ++i) other += fr.step[0][i].brk > 1 ? 1 : 0;
                CHECK (other > 0, "with \"all three breaks\", tools draw hits from the other breaks");
                auto hu = base; tg::breaktools::applyTool (tg::breaktools::ToolHumanise, hu, 0, 32, 4, 1.0f, 0, false);
                bool beatsOn = true; int late = 0; for (int i = 0; i < 32; ++i) { if (i % 4 == 0) beatsOn = beatsOn && hu.step[0][i].nudge == 0.0f; late += hu.step[0][i].nudge > 0.0f ? 1 : 0; }
                CHECK (beatsOn && late > 0, "Humanise: off-beat hits a little late, the beats on the grid");
                // no analysis: kicks and snares from where they fall in the bar
                auto na = base; for (auto& x : na.brk) { std::fill (std::begin (x.kick), std::end (x.kick), 0.0f); std::fill (std::begin (x.snare), std::end (x.snare), 0.0f); std::fill (std::begin (x.hat), std::end (x.hat), 0.0f); }
                tg::breaktools::applyTool (tg::breaktools::ToolTwoStep, na, 0, 32, 3, 1.0f, 0, false);
                CHECK (na.step[0][0].slice == 0 && na.step[0][4].slice == 4 && na.step[0][12].slice == 4, "with no analysis: the kick from beat 1, the snare from beat 2");
            }
            {
                // saved: per-step stretch, cycle and lateness; per-slice stretch and cycle
                tg::BreakStore st;
                st.edit ([] (tg::BreakData& d) { d.step[2][5].on = true; d.step[2][5].stretch = 3; d.step[2][5].cycleMs = 120.0f; d.step[2][5].nudge = 0.2f;
                                                  d.step[2][9].stretch = 1; d.brk[1].slice[3].stretch = 2; d.brk[1].slice[3].cycleMs = 80.0f; });
                tg::BreakStore st2; st2.fromVar (juce::JSON::parse (st.toJson()));
                const auto d = st2.get();
                CHECK (d.step[2][5].stretch == 3 && d.step[2][5].cycleMs == 120.0f && std::abs (d.step[2][5].nudge - 0.2f) < 1e-6f && d.step[2][9].stretch == 1
                       && d.brk[1].slice[3].stretch == 2 && d.brk[1].slice[3].cycleMs == 80.0f, "per-hit and per-slice stretch are saved");
            }
            // per-hit stretch: a steady 440 Hz "break" at 120 BPM played by the sequencer at 90 BPM
            auto peakHz = [&] (const std::vector<float>& out, int s0)
            {
                const int order = 14, N = 1 << order;
                std::vector<float> x ((size_t) N * 2, 0.0f);
                for (int i = 0; i < N && s0 + i < (int) out.size(); ++i) x[(size_t) i] = out[(size_t) (s0 + i)] * (0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / (N - 1)));
                juce::dsp::FFT fft (order); fft.performFrequencyOnlyForwardTransform (x.data());
                int k = 2; for (int i = 2; i < N / 2 - 1; ++i) if (x[(size_t) i] > x[(size_t) k]) k = i;
                const double a = x[(size_t) k - 1], c = x[(size_t) k], dd = x[(size_t) k + 1];
                return (k + 0.5 * (a - dd) / (a - 2.0 * c + dd)) * sr / N;
            };
            auto hit = [&] (int breakMode, int stepStretch, int sliceStretch, float stepCycle, float nudge = 0.0f, int at = 0)
            {
                auto p = make(); quiet (*p);
                setP (*p, P_bcOn, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcTempo, 120.0f); setP (*p, P_bcSliceMode, 1.0f); setP (*p, P_bcStretch, (float) breakMode);
                setP (*p, P_bcLevel, 1.0f); setP (*p, P_bcVelSens, 0.0f);
                p->loadSampleData (sineBreak (440.0, 2.0), "sine.wav", slot);
                p->breakStore.edit ([&] (tg::BreakData& d) { auto& s = d.step[0][at]; s.on = true; s.slice = 0; s.stretch = stepStretch; s.cycleMs = stepCycle; s.nudge = nudge;
                                                              d.brk[0].slice[0].stretch = sliceStretch; });
                return runHost (*p, 90.0, 0.0, 1.3);
            };
            auto measure = [&] (const std::vector<float>& out, double& secs, double& hz, int& first)
            {
                first = -1; int last = 0;
                for (int i = 0; i < (int) out.size(); ++i) if (std::abs (out[(size_t) i]) > 1e-3f) { if (first < 0) first = i; last = i; }
                secs = (last - first) / sr; hz = peakHz (out, first + (int) (0.05 * sr));
            };
            struct Case { const char* name; int brkMode, stepSt, sliceSt; float cyc; double hz; };
            const Case cases[] = { { "break Akai, no overrides", 1, 0, 0, 0.0f, 440.0 },
                                   { "step Repitch over a Akai break", 1, 1, 0, 0.0f, 330.0 },
                                   { "slice Repitch (step Default)", 1, 0, 1, 0.0f, 330.0 },
                                   { "step Clean wins over slice Repitch", 1, 3, 1, 0.0f, 440.0 },
                                   { "step Akai over a Repitch break", 0, 2, 0, 0.0f, 440.0 },
                                   { "step Akai with a 120 ms cycle", 0, 2, 0, 120.0f, 440.0 } };
            juce::StringArray bad; std::vector<std::vector<float>> outs;
            for (auto& c : cases)
            {
                const auto out = hit (c.brkMode, c.stepSt, c.sliceSt, c.cyc);
                double secs, hz; int first; measure (out, secs, hz, first);
                const double tol = c.hz == 440.0 && (c.brkMode == 1 && c.stepSt == 0 && c.sliceSt == 0 || c.stepSt == 2) ? 1000.0 / (c.cyc > 0 ? c.cyc : 40.0) : c.hz * 0.01;
                std::cout << "  " << c.name << ": " << juce::String (secs, 3) << " s, " << juce::String (hz, 1) << " Hz" << std::endl;
                if (std::abs (secs - 0.5 * 120.0 / 90.0) > 0.012 || std::abs (hz - c.hz) > tol) bad.add (c.name);
                outs.push_back (out);
            }
            CHECK (bad.isEmpty(), "per-hit stretch: the step's setting, then the slice's, then the break's (length and pitch)" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
            double diff = 0.0; for (size_t i = 0; i < outs[4].size(); ++i) diff = std::max (diff, (double) std::abs (outs[4][i] - outs[5][i]));
            CHECK (diff > 0.05, "a hit's own Cycle Length changes how it's crunched");
            // Humanise's lateness: a hit 0.4 of a step late
            {
                double s1, h1; int f0, f1;
                measure (hit (1, 0, 0, 0.0f, 0.0f, 2), s1, h1, f0);
                measure (hit (1, 0, 0, 0.0f, 0.4f, 2), s1, h1, f1);
                const double want = 0.4 * 0.25 * 60.0 / 90.0 * sr;
                CHECK (std::abs ((f1 - f0) - want) < 2.0, "a late hit plays that much of a step late (" + juce::String (f1 - f0) + " samples, want " + juce::String (want, 1) + ")");
            }
        }
        // 15. fills: one global Fill Pattern, called from the Fill pad (quantised), plays in time and jumps back
        {
            const int fillKey = 30;
            // pattern A: step i plays slice i; B: slice (i + 8) % 16; C (the Fill Pattern): slice 15 - i
            // (old Fill Length index -> Size: 0 = 1 beat -> 1, 1 = 2 beats -> 2, 2 = 1 bar -> 3)
            auto setup = [&] (int start, int len, int mode, int ret)
            {
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                p->fillPadKind = 1; p->fillPadNum = fillKey;
                setP (*p, P_bsFillPat, 3.0f); setP (*p, P_bsFillStart, (float) start); setP (*p, P_bsFillSize, (float) (len + 1));
                setP (*p, P_bsFillMode, (float) mode); setP (*p, P_bsFillReturn, (float) ret);
                p->breakStore.edit ([] (tg::BreakData& d)
                {
                    for (int i = 0; i < 16; ++i)
                    {
                        d.step[0][i].on = true; d.step[0][i].slice = i;
                        d.step[1][i].on = true; d.step[1][i].slice = (i + 8) % 16;
                        d.step[2][i].on = true; d.step[2][i].slice = 15 - i;
                    }
                });
                return p;
            };
            auto runMsgs = [&] (MegaSynthProcessor& p, double bpm, double ppq0, double beats, std::vector<std::pair<double, juce::MidiMessage>> msgs)
            {
                std::vector<MegaSynthProcessor::BsLogEntry> log; p.bsLog = &log;
                std::vector<std::pair<int, juce::MidiMessage>> ev;
                for (auto& kv : msgs) ev.push_back ({ (int) ((kv.first - ppq0) * 60.0 / bpm * sr), kv.second });
                runHost (p, bpm, ppq0, beats * 60.0 / bpm, ev, 100);
                p.bsLog = nullptr;
                std::vector<MegaSynthProcessor::BsLogEntry> mainOnly;
                for (auto& e : log) if (! e.layer) mainOnly.push_back (e);
                return mainOnly;
            };
            auto run = [&] (MegaSynthProcessor& p, double bpm, double ppq0, double beats, std::vector<std::pair<double, bool>> keys)
            {
                std::vector<std::pair<double, juce::MidiMessage>> msgs;
                for (auto& kv : keys) msgs.push_back ({ kv.first, kv.second ? juce::MidiMessage::noteOn (1, fillKey, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, fillKey) });
                return runMsgs (p, bpm, ppq0, beats, msgs);
            };
            // the main pattern is where it would have been, every step outside [f0, f1); the fill inside
            // (a fill plays the last steps of the Fill Pattern, as many as its size: fill step j is pattern step j - size)
            auto check = [&] (const std::vector<MegaSynthProcessor::BsLogEntry>& log, int64_t f0, int64_t f1, double bpm, juce::String& why, int L = 0)
            {
                if (L == 0) L = (int) (f1 - f0);
                const double inc = bpm / 60.0 / sr;
                for (auto& e : log)
                {
                    if (std::abs (e.beat - 0.25 * (double) e.step) > inc * 1.01) { why = "step " + juce::String (e.step) + " off the grid"; return false; }
                    const bool in = e.step >= f0 && e.step < f1;
                    if (e.fill != in) { why = "step " + juce::String (e.step) + (in ? " should be the fill" : " shouldn't be the fill"); return false; }
                    const int want = in ? 15 - (int) ((((e.step - f0 - L) % 16) + 16) % 16) : (int) (e.step % 16);
                    if (e.slice != want || ! e.fired) { why = "step " + juce::String (e.step) + " played slice " + juce::String (e.slice) + ", want " + juce::String (want); return false; }
                    if (in && e.pattern != 2) { why = "fill step " + juce::String (e.step) + " isn't Pattern C"; return false; }
                }
                return true;
            };
            for (double bpm : { 90.0, 140.0, 174.0 })
            {
                juce::String why;
                {
                    // play pressed mid-bar (beat 7.31); the Fill pad at beat 9.3; next bar, 1 bar
                    auto p = setup (1, 2, 0, 0);
                    const auto log = run (*p, bpm, 7.31, 10.0, { { 9.3, true }, { 9.5, false } });
                    CHECK (! log.empty() && log.front().step == 30 && check (log, 48, 64, bpm, why) && log.back().step >= 68,
                           "fill at " + juce::String (bpm) + " BPM: starts on the next bar, the pattern returns on the step it would have reached " + why);
                }
                {
                    auto p = setup (0, 1, 0, 0);   // next beat, 2 beats
                    const auto log = run (*p, bpm, 7.31, 6.0, { { 9.3, true }, { 9.4, false } });
                    CHECK (check (log, 40, 48, bpm, why), "fill on the next beat, 2 beats, at " + juce::String (bpm) + " BPM " + why);
                }
                {
                    auto p = setup (2, 0, 0, 0);   // the end of the pattern, 1 beat
                    const auto log = run (*p, bpm, 7.31, 6.0, { { 9.3, true }, { 9.4, false } });
                    CHECK (check (log, 44, 48, bpm, why), "fill timed to end with the pattern, at " + juce::String (bpm) + " BPM " + why);
                }
            }
            {
                // Size: Whole pattern = all 16 steps of the Fill Pattern from its first step
                auto p = setup (1, 2, 0, 0); setP (*p, P_bsFillSize, 0.0f);
                const auto log = run (*p, 172.0, 7.31, 10.0, { { 9.3, true }, { 9.5, false } });
                juce::String why;
                CHECK (check (log, 48, 64, 172.0, why), "Size Whole pattern plays the whole Fill Pattern " + why);
            }
            {
                // Restart: after the fill, the pattern starts from its first step
                auto p = setup (0, 0, 0, 1);
                const auto log = run (*p, 172.0, 7.31, 6.0, { { 9.3, true }, { 9.4, false } });
                bool ok = ! log.empty();
                for (auto& e : log)
                {
                    const int want = e.step >= 40 && e.step < 44 ? 3 - (int) (e.step - 40) : (e.step >= 44 ? (int) ((e.step - 44) % 16) : (int) (e.step % 16));
                    ok = ok && e.slice == want && e.fill == (e.step >= 40 && e.step < 44);
                }
                CHECK (ok, "When it ends: Restart the pattern starts again from step 1 after the fill");
            }
            {
                // Hold: plays from the next bar until let go
                auto p = setup (1, 2, 1, 0);
                const auto log = run (*p, 172.0, 7.31, 10.0, { { 9.3, true }, { 13.1, false } });
                juce::String why;
                CHECK (check (log, 48, 53, 172.0, why, 16), "a held fill plays until the pad is let go " + why);
            }
            {
                // the pad as a CC (value >= 64 is pressed), quantised the same way
                auto p = setup (1, 2, 0, 0); p->fillPadKind = 2; p->fillPadNum = 20;
                const auto log = runMsgs (*p, 172.0, 7.31, 10.0, { { 9.3, juce::MidiMessage::controllerEvent (1, 20, 127) }, { 9.5, juce::MidiMessage::controllerEvent (1, 20, 0) } });
                juce::String why;
                CHECK (check (log, 48, 64, 172.0, why), "a CC Fill pad starts a fill on the next bar " + why);
                CHECK (p->fillPadText() == "CC 20", "the pad shows as CC 20 (" + p->fillPadText() + ")");
            }
            {
                // with a chain running: the chain carries on underneath
                auto p = setup (1, 2, 0, 0);
                p->breakStore.edit ([] (tg::BreakData& d) { tg::BreakStore::setChain (d, "AB"); });
                setP (*p, P_bsChainOn, 1.0f);
                const auto log = run (*p, 172.0, 7.31, 14.0, { { 9.3, true }, { 9.5, false } });
                bool ok = ! log.empty();
                for (auto& e : log)
                {
                    const int pat = (int) ((e.step / 16) % 2 == 0 ? 1 : 0);   // (the chain starts at step 30: A, then B at step 32, A at 48 ...)
                    const bool in = e.step >= 48 && e.step < 64;
                    if (e.step < 32) continue;
                    const int want = in ? 15 - (int) (e.step - 48) : (pat == 0 ? (int) (e.step % 16) : (int) ((e.step % 16 + 8) % 16));
                    ok = ok && e.pattern == (in ? 2 : pat) && e.fill == in && e.slice == want;
                }
                CHECK (ok, "fills with a chain running: the chain position carries on underneath");
            }
            {
                // the same Fill Pattern is called from any pattern (written in C, called from A and from B)
                auto p = setup (1, 2, 0, 0); setP (*p, P_bsPattern, 1.0f);
                const auto log = run (*p, 172.0, 8.0, 10.0, { { 9.3, true }, { 9.5, false } });
                bool ok = ! log.empty(); int n = 0;
                for (auto& e : log) { const bool in = e.step >= 48 && e.step < 64; n += in ? 1 : 0; ok = ok && e.fill == in && e.slice == (in ? 15 - (int) (e.step - 48) : (int) ((e.step % 16 + 8) % 16)); }
                CHECK (ok && n == 16, "the global Fill Pattern is called from Pattern B too");
            }
            {
                // calling the fill while the Fill Pattern itself is playing does nothing
                auto p = setup (1, 2, 0, 0); setP (*p, P_bsPattern, 2.0f);
                const auto log = run (*p, 172.0, 8.0, 10.0, { { 9.3, true }, { 9.5, false } });
                bool any = false, ok = ! log.empty();
                for (auto& e : log) { any = any || e.fill; ok = ok && e.pattern == 2 && e.slice == 15 - (int) (e.step % 16); }
                CHECK (! any && ok, "calling the fill while the Fill Pattern plays does nothing");
                auto q = setup (1, 2, 0, 0); setP (*q, P_bsFillPat, 0.0f);
                const auto none = run (*q, 172.0, 8.0, 10.0, { { 9.3, true }, { 9.5, false } });
                any = false; for (auto& e : none) any = any || e.fill;
                CHECK (! any, "Fill Pattern Off: the pad does nothing");
            }
            {
                // Auto Fill every 4 bars plays the Fill Pattern, ending with the phrase
                auto p = setup (1, 2, 0, 0);
                setP (*p, P_bsAutoFill, 1.0f); setP (*p, P_bsAutoFillProb, 1.0f); p->fillPadKind = 0;
                const auto log = run (*p, 172.0, 0.0, 36.0, {});
                bool ok = log.size() >= 140;
                for (auto& e : log) { const bool in = (e.step >= 48 && e.step < 64) || (e.step >= 112 && e.step < 128); ok = ok && e.fill == in && (! in || e.slice == 15 - (int) (e.step % 16)); }
                CHECK (ok, "Auto Fill every 4 bars: the Fill Pattern over the last bar of each phrase");
                auto q = setup (1, 2, 0, 0);
                setP (*q, P_bsAutoFill, 1.0f); setP (*q, P_bsAutoFillProb, 0.0f);
                const auto none = run (*q, 172.0, 0.0, 36.0, {});
                bool any = false; for (auto& e : none) any = any || e.fill;
                CHECK (! any, "Auto Fill at 0% chance plays none");
            }
            {
                // the Fill parameter (automation / MIDI learn / the button)
                auto p = setup (1, 2, 0, 0);
                setP (*p, P_bsFill, 1.0f);
                const auto log = run (*p, 172.0, 7.31, 10.0, {});
                juce::String why;
                CHECK (check (log, 32, 48, 172.0, why), "the Fill parameter starts a fill " + why);
            }
            {
                // the Fill pad: Learn takes the next note or CC; the pad's note isn't played by the synth
                auto p = make(); p->fillPadLearn = true;
                juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 44, (juce::uint8) 100), 0);
                p->processBlock (b, m);
                CHECK (p->fillPadKind.load() == 1 && p->fillPadNum.load() == 44 && ! p->fillPadLearn.load(), "Learn takes the next note as the Fill pad (" + p->fillPadText() + ")");
                auto c = make(); c->fillPadLearn = true;
                juce::MidiBuffer m2; m2.addEvent (juce::MidiMessage::controllerEvent (1, 21, 100), 0); c->processBlock (b, m2);
                CHECK (c->fillPadKind.load() == 2 && c->fillPadNum.load() == 21, "Learn takes a CC as the Fill pad");
                auto level = [&] (bool pad)
                {
                    auto q = make(); if (pad) { q->fillPadKind = 1; q->fillPadNum = 60; }
                    double e = 0.0;
                    for (int i = 0; i < 40; ++i)
                    {
                        juce::AudioBuffer<float> o (2, 256); o.clear(); juce::MidiBuffer mm;
                        if (i == 0) mm.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                        q->processBlock (o, mm); e += o.getMagnitude (0, 256);
                    }
                    return e;
                };
                const double withPad = level (true), without = level (false);
                CHECK (without > 0.1 && withPad < 1.0e-6, "the Fill pad's note doesn't sound (" + juce::String (withPad) + " vs " + juce::String (without) + ")");
            }
            {
                // saved with the project
                auto p = setup (1, 2, 1, 1); setP (*p, P_bsAutoFill, 2.0f); setP (*p, P_bsFillSize, 2.0f);
                p->fillPadKind = 2; p->fillPadNum = 33;
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
                auto plain = [] (MegaSynthProcessor& r, int id) { return juce::roundToInt (r.param (id)->convertFrom0to1 (r.param (id)->getValue())); };
                CHECK (plain (*q, P_bsFillPat) == 3 && plain (*q, P_bsFillSize) == 2 && plain (*q, P_bsFillMode) == 1 && plain (*q, P_bsFillReturn) == 1 && plain (*q, P_bsAutoFill) == 2
                       && q->fillPadKind.load() == 2 && q->fillPadNum.load() == 33, "fill settings and the Fill pad are saved with the project");
                // an older project from before fills (v0.1.19): no fills, no pad
                auto xml = juce::parseXML (getXmlFromBinaryForTest (mb));
                if (xml != nullptr)
                {
                    xml->setAttribute ("stateVersion", 19);
                    for (auto* id : { "bsFill", "bsFillKey", "bsFillMode", "bsFillLen", "bsFillStart", "bsFillReturn", "bsAutoFill", "bsAutoFillProb", "bsFillPat", "bsFillSize" })
                        if (auto* e = xml->getChildByAttribute ("id", id)) xml->removeChildElement (e, true);
                    xml->removeAttribute ("fillPadKind"); xml->removeAttribute ("fillPadNum");
                    juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (*xml, mb2);
                    auto r = make(); setP (*r, P_bsAutoFill, 3.0f); setP (*r, P_bsFillKey, 50.0f); setP (*r, P_bsFill, 1.0f);
                    r->setStateInformation (mb2.getData(), (int) mb2.getSize());
                    CHECK (r->param (P_bsAutoFill)->getValue() == 0.0f && r->param (P_bsFill)->getValue() == 0.0f && plain (*r, P_bsFillPat) == 0
                           && r->fillPadKind.load() == 0, "older projects from before fills load with fills off");
                }
            }
            {
                // a v0.1.68 project: Fill Key note 50 and an Auto fill on A -> the pad is note 50, the Auto fill written into empty Pattern H
                auto p = setup (1, 2, 0, 0); p->fillPadKind = 0; p->fillPadNum = -1;
                setP (*p, P_bsFillPat, 0.0f); setP (*p, P_bsFillSize, 0.0f); setP (*p, P_bsFillLen, 1.0f);
                setP (*p, P_bsFillKey, 50.0f); setP (*p, P_bsAutoFill, 1.0f);
                p->breakStore.edit ([] (tg::BreakData& d) { d.fillSrc[0] = 0; d.fillSeed = 5; d.patBreak[0] = 2; });
                p->refreshAutoFills (true);
                const auto before = p->breakStore.get();
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto xml = juce::parseXML (getXmlFromBinaryForTest (mb));
                if (xml != nullptr)
                {
                    xml->setAttribute ("stateVersion", 28);
                    xml->removeAttribute ("fillPadKind"); xml->removeAttribute ("fillPadNum");
                    juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (*xml, mb2);
                    auto r = make(); r->setStateInformation (mb2.getData(), (int) mb2.getSize());
                    auto plain = [] (MegaSynthProcessor& x, int id) { return juce::roundToInt (x.param (id)->convertFrom0to1 (x.param (id)->getValue())); };
                    const auto d = r->breakStore.get();
                    bool same = true; for (int i = 0; i < 16; ++i) same = same && d.step[7][i].on == before.autoFill[0][i].on && d.step[7][i].slice == before.autoFill[0][i].slice && d.step[7][i].roll == before.autoFill[0][i].roll;
                    std::cout << "  conversion: " << r->lastFillConversion << std::endl;
                    CHECK (r->fillPadKind.load() == 1 && r->fillPadNum.load() == 50, "an older project's Fill Key note becomes the Fill pad");
                    CHECK (plain (*r, P_bsFillPat) == 8 && plain (*r, P_bsFillSize) == 2 && same && d.patBreak[7] == 2,
                           "an older project's Auto fill is written into empty Pattern H (its break kept, 2 beats) and becomes the Fill Pattern");
                }
                // an older project whose A named Pattern C as its fill: C becomes the Fill Pattern
                auto q = setup (1, 2, 0, 0); q->fillPadKind = 0; setP (*q, P_bsFillPat, 0.0f); setP (*q, P_bsFillLen, 2.0f);
                q->breakStore.edit ([] (tg::BreakData& d) { d.fillSrc[0] = 3; });
                juce::MemoryBlock mq; q->getStateInformation (mq);
                if (auto x2 = juce::parseXML (getXmlFromBinaryForTest (mq)))
                {
                    x2->setAttribute ("stateVersion", 28); x2->removeAttribute ("fillPadKind"); x2->removeAttribute ("fillPadNum");
                    juce::MemoryBlock m3; juce::AudioProcessor::copyXmlToBinary (*x2, m3);
                    auto r = make(); r->setStateInformation (m3.getData(), (int) m3.getSize());
                    CHECK (juce::roundToInt (r->param (P_bsFillPat)->convertFrom0to1 (r->param (P_bsFillPat)->getValue())) == 3 && r->fillPadKind.load() == 0,
                           "an older project's fill that named Pattern C: C is the Fill Pattern");
                }
            }
        }
        // 16. v0.1.69: Break 4, each pattern owning its break, Key Entry following it, the layer sequencer
        {
            auto loadBreaks = [&] (MegaSynthProcessor& p, int n)
            {
                setP (p, P_bcOn, 1.0f);
                for (int k = 0; k < n; ++k)
                {
                    p.loadSampleData (wav16 (tg::breaks::makeDemoBreak (44100.0, k == 1 ? 174.0 : 170.0, k % 3), 44100.0), "b" + juce::String (k + 1) + ".wav", MegaSynthProcessor::breakSlot (k));
                    setP (p, tg::breakParams (k).sliceMode, 3.0f);
                }
                p.refreshBreakAnalysis();
            };
            auto logRun = [&] (MegaSynthProcessor& p, double bpm, double ppq0, double beats, std::vector<std::pair<int, juce::MidiMessage>> ev = {})
            {
                std::vector<MegaSynthProcessor::BsLogEntry> log; p.bsLog = &log;
                runHost (p, bpm, ppq0, beats * 60.0 / bpm, ev, 128);
                p.bsLog = nullptr;
                return log;
            };
            auto plain = [] (MegaSynthProcessor& r, int id) { return r.param (id)->convertFrom0to1 (r.param (id)->getValue()); };
            // Break 4: loads, plays, is saved; Break Select's fourth key (below the other three) picks it
            {
                auto p = make(); loadBreaks (*p, 4);
                const auto d = p->breakStore.get();
                CHECK (d.brk[3].nSlices == 16 && p->getSample (MegaSynthProcessor::breakSlot (3)) != nullptr, "Break 4 loads and slices (" + juce::String (d.brk[3].nSlices) + ")");
                setP (*p, P_bsOn, 1.0f); setP (*p, P_bcActive, 4.0f);
                p->breakStore.edit ([] (tg::BreakData& x) { for (int i = 0; i < 16; ++i) { x.step[0][i].on = true; x.step[0][i].slice = i; } });
                const auto log = logRun (*p, 172.0, 0.0, 4.0);
                bool ok = log.size() >= 15; for (auto& e : log) ok = ok && e.fired && e.brk == 3;
                CHECK (ok, "the sequencer plays Break 4 when it's the Active Break");
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
                CHECK (q->getSample (MegaSynthProcessor::breakSlot (3)) != nullptr && q->breakStore.get().brk[3].nSlices == 16 && juce::roundToInt (plain (*q, P_bcActive)) == 4,
                       "Break 4 is saved with the project");
                auto fresh = make();
                CHECK (fresh->getSample (MegaSynthProcessor::breakSlot (3)) == nullptr, "Break 4 starts empty (older projects have none)");
                setP (*p, P_bcSelKeys, 1.0f); setP (*p, P_bcActive, 1.0f); setP (*p, P_bcSelQuant, 0.0f);
                const int kb = juce::roundToInt (plain (*p, P_bcKeyBase));
                juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, kb - 4, (juce::uint8) 100), 0);
                p->processBlock (b, m);
                const int after4 = juce::roundToInt (plain (*p, P_bcActive));
                juce::MidiBuffer m2; m2.addEvent (juce::MidiMessage::noteOn (1, kb - 2, (juce::uint8) 100), 0); p->processBlock (b, m2);
                CHECK (after4 == 4 && juce::roundToInt (plain (*p, P_bcActive)) == 2, "Break Select: the key below the three picks Break 4; the others still pick 1-3");
            }
            // each pattern owns its break: Original uses it (its timing), pins its steps, and the pattern plays it
            {
                auto p = make(); loadBreaks (*p, 3);
                setP (*p, P_bcSliceMode2, 0.0f); setP (*p, P_bcSliceMode3, 0.0f);   // (Breaks 2 and 3 sliced by their hits: different timings)
                p->refreshBreakAnalysis();
                p->breakStore.edit ([] (tg::BreakData& d) { d.patBreak[0] = 2; d.patBreak[1] = 3; });
                const int b0 = p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 0);
                const int b1 = p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 1);
                const auto d = p->breakStore.get();
                auto want = std::make_unique<tg::BreakData> (d);
                tg::breaktools::original (*want, 0, 16, 1, true); tg::breaktools::original (*want, 1, 16, 2, true);
                bool same = true, pinned = true, differ = false;
                for (int i = 0; i < 16; ++i)
                {
                    for (int q = 0; q < 2; ++q) same = same && d.step[q][i].on == want->step[q][i].on && d.step[q][i].slice == want->step[q][i].slice;
                    pinned = pinned && d.step[0][i].brk == 2 && d.step[1][i].brk == 3;
                    differ = differ || d.step[0][i].on != d.step[1][i].on;
                }
                std::cout << "  pattern breaks: A from Break " << (b0 + 1) << " (" << d.brk[1].nSlices << " slices), B from Break " << (b1 + 1) << " (" << d.brk[2].nSlices << " slices)" << std::endl;
                CHECK (b0 == 1 && b1 == 2 && same && pinned && differ, "Original loads the pattern's own break's slices and timing, pinned to it (A: Break 2, B: Break 3)");
                // a tool keeps the pattern's break
                p->runBreakTool (tg::breaktools::ToolStutter, false, 0);
                bool still = true; for (auto& s : p->breakStore.get().step[0]) if (s.on) still = still && s.brk == 2;
                CHECK (still, "the tools work from the pattern's break and keep its hits on it");
                // pattern A starts: Break 2 becomes the Active Break; an Active pattern leaves it
                setP (*p, P_bsOn, 1.0f); setP (*p, P_bcActive, 1.0f);
                logRun (*p, 172.0, 0.0, 2.0);
                CHECK (juce::roundToInt (plain (*p, P_bcActive)) == 2, "a pattern with its own break makes it the Active Break when it starts");
                auto q = make(); loadBreaks (*q, 2); setP (*q, P_bsOn, 1.0f); setP (*q, P_bcActive, 2.0f);
                q->breakStore.edit ([] (tg::BreakData& x) { for (int i = 0; i < 16; ++i) { x.step[0][i].on = true; x.step[0][i].slice = i; } });
                const auto lq = logRun (*q, 172.0, 0.0, 2.0);
                bool act = ! lq.empty(); for (auto& e : lq) act = act && e.brk == 1;
                CHECK (act && juce::roundToInt (plain (*q, P_bcActive)) == 2 && q->breakStore.get().patBreak[0] == 0, "a pattern set to Active plays the Active Break, as before");
                // saved with the project; older projects load every pattern as Active
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto r = make(); r->setStateInformation (mb.getData(), (int) mb.getSize());
                CHECK (r->breakStore.get().patBreak[0] == 2 && r->breakStore.get().patBreak[1] == 3, "each pattern's break is saved with the project");
            }
            // Key Entry: the keys play the edited pattern's break; Fixed velocity; into the layer
            {
                auto p = make(); loadBreaks (*p, 4); setP (*p, P_bcActive, 1.0f); setP (*p, P_bsKeyEntry, 1.0f);
                const int kb = juce::roundToInt (plain (*p, P_bcKeyBase));
                p->keyBreakOverride = 3;
                juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, kb + 2, (juce::uint8) 40), 0);
                p->processBlock (b, m);
                auto ents = p->takeKeyEntries();
                CHECK (ents.size() == 1 && ents[0].brk == 3 && ents[0].slice == 2, "with the edited pattern on Break 4, the slice keys play Break 4 (no need to move the Active Break)");
                setP (*p, P_bsKeyFixed, 1.0f); setP (*p, P_bsKeyVel, 0.5f);
                p->applyKeyEntry (0, 3, ents[0], false);
                setP (*p, P_bsKeyFixed, 0.0f);
                p->applyKeyEntry (0, 4, ents[0], false);
                p->applyKeyEntry (2, 5, ents[0], false, true);
                const auto d = p->breakStore.get();
                CHECK (std::abs (d.step[0][3].vel - 0.5f) < 0.01f && std::abs (d.step[0][4].vel - ents[0].vel) < 0.01f && d.step[0][3].brk == 4,
                       "Fixed velocity: entered hits get the set velocity (" + juce::String (d.step[0][3].vel, 2) + "), off: the key's (" + juce::String (d.step[0][4].vel, 2) + ")");
                CHECK (d.lstep[2][5].on && d.lstep[2][5].slice == 2 && ! d.step[2][5].on, "Key Entry into the layer sequencer's pattern");
                // the page sets which break the keys play: the edited pattern's own (Active: the Active Break)
                p->breakStore.edit ([] (tg::BreakData& x) { x.patBreak[0] = 4; x.lpatBreak[1] = 2; });
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
                e->showPage ("Break Sequencer"); e->timerCallbackForTests();
                const int ovA = p->keyBreakOverride.load();
                setP (*p, P_bsPattern, 1.0f); e->timerCallbackForTests();
                const int ovB = p->keyBreakOverride.load();
                setP (*p, P_bsLFollow, 1.0f); setP (*p, P_bsLPattern, 1.0f); e->setBreakEditLayerForTests (true); e->timerCallbackForTests();
                const int ovL = p->keyBreakOverride.load();
                CHECK (ovA == 3 && ovB == -1 && ovL == 1 && p->keyEntryLayer.load(), "the keys follow the edited pattern's break: A on Break 4, B Active, layer B on Break 2 ("
                       + juce::String (ovA) + " " + juce::String (ovB) + " " + juce::String (ovL) + ")");
                e->setBreakEditLayerForTests (false); setP (*p, P_bsKeyEntry, 0.0f);
                e->showPage ("Mixer"); e->timerCallbackForTests();
                CHECK (p->keyBreakOverride.load() == -1, "away from the page (and Key Entry off) the keys play the Active Break again");
            }
            // the layer sequencer
            auto layerSetup = [&] ()
            {
                auto p = make(); loadBreaks (*p, 4);
                quiet (*p);
                setP (*p, P_bsOn, 1.0f); setP (*p, P_bsLOn, 1.0f); setP (*p, P_bcActive, 1.0f);
                p->breakStore.edit ([] (tg::BreakData& d)
                {
                    for (int i = 0; i < 16; ++i)
                    {
                        d.step[0][i].on = true; d.step[0][i].slice = i;
                        d.lstep[0][i].on = (i % 2) == 1; d.lstep[0][i].slice = 15 - i;     // A: off-beat 16ths from Break 4
                        d.lstep[1][i].on = (i % 4) == 2; d.lstep[1][i].slice = 3;
                        d.lstep[2][i].on = (i % 4) == 0; d.lstep[2][i].slice = 5;
                    }
                    d.lpatBreak[0] = 4; d.lpatBreak[1] = 4; d.lpatBreak[2] = 4;
                });
                return p;
            };
            {
                // Follow main: the main's letter, on exactly the same steps
                auto p = layerSetup();
                const auto log = logRun (*p, 172.0, 3.37, 8.0);
                const double inc = 172.0 / 60.0 / sr;
                int nl = 0, nm = 0; bool ok = true;
                for (auto& e : log)
                {
                    ok = ok && std::abs (e.beat - 0.25 * (double) e.step) <= inc * 1.01;
                    if (e.layer && ! e.fired) continue;
                    if (e.layer) { ++nl; ok = ok && e.pattern == 0 && e.brk == 3 && (e.step % 2) == 1 && e.slice == 15 - (int) (e.step % 16); }
                    else { ++nm; ok = ok && e.brk == 0 && e.slice == (int) (e.step % 16); }
                }
                CHECK (ok && nl >= 15 && nm >= 31, "Follow main: the layer plays its pattern A from Break 4 on the same 16th grid (" + juce::String (nl) + " layer, " + juce::String (nm) + " main steps)");
                // the main switches pattern: the layer follows the letter
                auto q = layerSetup();
                q->breakStore.edit ([] (tg::BreakData& d) { tg::BreakStore::setChain (d, "AB"); for (int i = 0; i < 16; ++i) d.step[1][i] = d.step[0][i]; });
                setP (*q, P_bsChainOn, 1.0f);
                const auto lq = logRun (*q, 172.0, 0.0, 16.0);
                bool fol = true; int nB = 0;
                for (auto& e : lq) if (e.layer) { const int want = (int) ((e.step / 16) % 2); fol = fol && e.pattern == want; nB += e.pattern == 1 ? 1 : 0; }
                CHECK (fol && nB > 0, "Follow main: with the main's chain AB, the layer plays its A then its B");
            }
            {
                // Own: its own chain, BC
                auto p = layerSetup();
                setP (*p, P_bsLFollow, 1.0f); setP (*p, P_bsLChainOn, 1.0f);
                p->breakStore.edit ([] (tg::BreakData& d) { tg::BreakStore::setChain (d.lchain, "BC"); });
                const auto log = logRun (*p, 172.0, 0.0, 16.0);
                bool ok = true; int n = 0;
                for (auto& e : log) if (e.layer && e.fired) { ++n; ok = ok && e.pattern == ((e.step / 16) % 2 == 0 ? 1 : 2) && e.slice == (e.pattern == 1 ? 3 : 5); }
                CHECK (ok && n >= 12, "Own: the layer runs its own chain (BC) while the main plays A");
                // its own Swing: its off-beats move, the main's don't
                setP (*p, P_bsLSwing, 0.5f);
                const auto ls = logRun (*p, 172.0, 0.0, 4.0);
                bool sw = true; for (auto& e : ls) if (! e.layer) sw = sw && std::abs (e.beat - 0.25 * (double) e.step) < 0.002;
                CHECK (sw, "the layer's Swing leaves the main's timing alone");
            }
            {
                // the two sequencers don't choke each other (each its own choke groups); within one they still do
                auto voicesAfter = [&] (bool layerHit, bool secondMain)
                {
                    auto p = layerSetup();
                    p->breakStore.edit ([&] (tg::BreakData& d)
                    {
                        for (int i = 0; i < 16; ++i) { d.step[0][i].on = false; d.lstep[0][i].on = false; }
                        d.step[0][0].on = true; d.step[0][0].slice = 0;
                        if (secondMain) { d.step[0][1].on = true; d.step[0][1].slice = 4; }
                        if (layerHit) { d.lstep[0][1].on = true; d.lstep[0][1].slice = 4; }
                    });
                    runHost (*p, 172.0, 0.0, 0.45 * 60.0 / 172.0);
                    return p->getEngine().breaks().activeVoices();
                };
                const int mainMain = voicesAfter (false, true), mainLayer = voicesAfter (true, false);
                CHECK (mainMain == 1 && mainLayer == 2, "choke: two main hits cut each other (" + juce::String (mainMain) + " voice), a main and a layer hit both ring (" + juce::String (mainLayer) + ")");
            }
            {
                // its Mixer strip: level, mute; the layer is heard only through it
                auto energy = [&] (int setupMode)
                {
                    auto p = layerSetup();
                    if (setupMode == 1) setP (*p, P_bsLOn, 0.0f);
                    if (setupMode == 2) setP (*p, P_bsLLevel, 0.0f);
                    if (setupMode == 3) setP (*p, P_mxLayerMute, 1.0f);
                    const auto out = runHost (*p, 172.0, 0.0, 4.0 * 60.0 / 172.0);
                    double e = 0.0; for (float x : out) e += (double) x * x;
                    return e;
                };
                const double both = energy (0), off = energy (1), zero = energy (2), muted = energy (3);
                CHECK (both > off * 1.05 && std::abs (zero - off) < 1.0e-6 * off && std::abs (muted - off) < 1.0e-6 * off,
                       "the layer's Mixer strip: Level 0 or Mute leaves just the main (" + juce::String (both / off, 3) + "x with the layer)");
                auto p = layerSetup();
                runHost (*p, 172.0, 0.0, 2.0 * 60.0 / 172.0);
                CHECK (p->getEngine().mixerPeak[kMxLayerStrip].load() > 0.0f, "the Break Layer strip's meter moves");
            }
            {
                // Break React: from the main, the layer or both
                auto hits = [&] (int from)
                {
                    auto p = layerSetup();
                    p->breakStore.edit ([] (tg::BreakData& d) { for (int i = 0; i < 16; ++i) { d.step[0][i].on = (i % 4) == 0; d.lstep[0][i].on = (i % 4) == 2; } });
                    setP (*p, P_rxOn, 1.0f); setP (*p, P_rxListen, 0.0f); setP (*p, P_rxFrom, (float) from);
                    runHost (*p, 172.0, 0.0, 8.0 * 60.0 / 172.0);
                    return p->getEngine().rxHitsShown.load();
                };
                const int hm = hits (0), hl = hits (1), hb = hits (2);
                std::cout << "  Break React hits: main " << hm << ", layer " << hl << ", both " << hb << std::endl;
                CHECK (hm >= 7 && hl >= 7 && hb == hm + hl, "Break React listens to the main sequencer, the layer, or both");
            }
            {
                // during a main fill: the layer keeps going, or plays its own pattern of the Fill Pattern's letter
                for (int mode = 0; mode < 2; ++mode)
                {
                    auto p = layerSetup();
                    setP (*p, P_bsFillPat, 3.0f); setP (*p, P_bsFillSize, 3.0f); setP (*p, P_bsFillStart, 1.0f); setP (*p, P_bsLFill, (float) mode);
                    p->breakStore.edit ([] (tg::BreakData& d) { for (int i = 0; i < 16; ++i) { d.step[2][i].on = true; d.step[2][i].slice = 15 - i; } });
                    setP (*p, P_bsFill, 1.0f);
                    const auto log = logRun (*p, 172.0, 0.5, 8.0);
                    bool ok = true; int inFill = 0;
                    for (auto& e : log)
                    {
                        if (! e.layer) continue;
                        const bool f = e.step >= 16 && e.step < 32;
                        if (mode == 0) ok = ok && ! e.fill && e.pattern == 0;
                        else { ok = ok && e.fill == f && e.pattern == (f ? 2 : 0); inFill += f ? 1 : 0; }
                    }
                    CHECK (ok && (mode == 0 || inFill >= 3), juce::String (mode == 0 ? "during a fill the layer keeps going" : "during a fill the layer plays its own Fill Pattern letter (C)"));
                }
            }
            {
                // saved with the project, undo and A/B; an older project has an empty layer, switched off
                auto p = layerSetup();
                setP (*p, P_bsLFollow, 1.0f); setP (*p, P_mxLayerPan, 0.3f);
                p->breakStore.edit ([] (tg::BreakData& d) { tg::BreakStore::setChain (d.lchain, "ABBA"); d.lpatBreak[5] = 3; });
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
                const auto d = q->breakStore.get();
                CHECK (d.lstep[0][1].on && d.lstep[0][1].slice == 14 && d.lpatBreak[5] == 3 && juce::String (d.lchain) == "ABBA" && plain (*q, P_bsLOn) > 0.5f
                       && plain (*q, P_bsLFollow) > 0.5f && std::abs (plain (*q, P_mxLayerPan) - 0.3f) < 0.01f, "the layer sequencer is saved with the project");
                p->pushHistory ("before");
                p->breakStore.edit ([] (tg::BreakData& x) { x.lstep[3][0].on = true; x.lstep[3][0].slice = 9; });
                p->pushHistory ("layer edit");
                p->undo();
                CHECK (! p->breakStore.get().lstep[3][0].on, "Undo takes back a layer edit");
                auto fresh = make(); const auto f = fresh->breakStore.get();
                bool empty = plain (*fresh, P_bsLOn) < 0.5f && f.lchain[0] == 0;
                for (int q2 = 0; q2 < 8; ++q2) { empty = empty && f.lpatBreak[q2] == 0; for (int i = 0; i < 32; ++i) empty = empty && ! f.lstep[q2][i].on; }
                CHECK (empty, "a new (or older) project's layer is empty and off");
            }
            {
                // CPU: both sequencers on 32nd rolls from all four breaks, Clean stretch, at 172 BPM
                auto p = layerSetup();
                for (int k = 0; k < 4; ++k) setP (*p, tg::breakParams (k).stretch, 2.0f);
                p->breakStore.edit ([] (tg::BreakData& d) { for (int i = 0; i < 16; ++i) { d.step[0][i].roll = 2; d.step[0][i].brk = 1 + (i % 3); d.lstep[0][i].on = true; d.lstep[0][i].roll = 2; } });
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                runHost (*p, 172.0, 0.0, 10.0);
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                std::cout << "  CPU, both sequencers + 4 breaks, rolls, Clean stretch: " << juce::String (ms / 100.0, 1) << "% of one core (10 s in " << juce::String (ms, 0) << " ms)" << std::endl;
                CHECK (ms < 4000.0, "both sequencers with four breaks run well within real time");
            }
        }
        // 17. v0.1.71: one shared layer bank paired with the main patterns, pattern pads, the jungle snare roll
        {
            auto plainOf = [] (MegaSynthProcessor& r, int id) { return r.param (id)->convertFrom0to1 (r.param (id)->getValue()); };
            auto logRun2 = [&] (MegaSynthProcessor& p, double bpm, double ppq0, double beats, std::vector<std::pair<int, juce::MidiMessage>> ev = {})
            {
                std::vector<MegaSynthProcessor::BsLogEntry> log; p.bsLog = &log;
                runHost (p, bpm, ppq0, beats * 60.0 / bpm, ev, 128);
                p.bsLog = nullptr;
                return log;
            };
            auto twoSeq = [&] ()
            {
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                p->loadSampleData (wav16 (tg::breaks::makeDemoBreak (44100.0, 165.0, 2), 44100.0), "b4.wav", MegaSynthProcessor::breakSlot (3));
                setP (*p, P_bcSliceMode4, 3.0f); p->refreshBreakAnalysis();
                setP (*p, P_bsOn, 1.0f); setP (*p, P_bsLOn, 1.0f);
                p->breakStore.edit ([] (tg::BreakData& d)
                {
                    for (int q = 0; q < 8; ++q)
                        for (int i = 0; i < 16; ++i)
                        {
                            d.step[q][i].on = true; d.step[q][i].slice = i;
                            d.lstep[q][i].on = (i % 2) == 1; d.lstep[q][i].slice = q;   // (layer pattern q plays slice q: easy to see which)
                        }
                });
                return p;
            };
            {
                // main A -> Layer B, main B -> Layer C, through a chain AB: the layer switches with the main, on the same step
                auto p = twoSeq();
                p->breakStore.edit ([] (tg::BreakData& d) { d.lpair[0] = 2; d.lpair[1] = 3; tg::BreakStore::setChain (d, "AB"); });
                setP (*p, P_bsChainOn, 1.0f);
                const auto log = logRun2 (*p, 172.0, 0.0, 16.0);
                bool ok = true; int nl = 0;
                for (auto& e : log)
                {
                    if (! e.layer || ! e.fired) continue;
                    ++nl;
                    const int mainPat = (int) ((e.step / 16) % 2);
                    const int want = mainPat == 0 ? 1 : 2;
                    ok = ok && e.pattern == want && e.slice == want;
                }
                CHECK (ok && nl >= 30, "Paired: main A plays Layer B, main B plays Layer C, switching on the same step (" + juce::String (nl) + " layer hits)");
                // Off: no layer under that main pattern
                p->breakStore.edit ([] (tg::BreakData& d) { d.lpair[1] = 0; });
                const auto l2 = logRun2 (*p, 172.0, 0.0, 16.0);
                bool offOk = true; int underB = 0;
                for (auto& e : l2) if (e.layer && e.fired && (e.step / 16) % 2 == 1) ++underB;
                for (auto& e : l2) if (e.layer && e.fired && (e.step / 16) % 2 == 0) offOk = offOk && e.pattern == 1;
                CHECK (underB == 0 && offOk, "a main pattern paired with Off has no layer");
                // the pairs are saved with the project; older projects (no pairs) are A-H with A-H
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
                const auto dq = q->breakStore.get();
                CHECK (dq.lpair[0] == 2 && dq.lpair[1] == 0 && dq.lpair[2] == 3, "the layer pairs are saved with the project");
                auto fresh = make();
                CHECK (fresh->breakStore.get().pairsIdentity() && plainOf (*fresh, P_bsLFollow) < 0.5f, "a new (or older) project: Paired, A-H with A-H (what Follow main was)");
            }
            {
                // during a fill: Play the Fill Pattern's pair
                auto p = twoSeq();
                p->breakStore.edit ([] (tg::BreakData& d) { d.lpair[2] = 6; });   // (the Fill Pattern C plays with Layer F)
                setP (*p, P_bsFillPat, 3.0f); setP (*p, P_bsFillSize, 3.0f); setP (*p, P_bsFillStart, 1.0f); setP (*p, P_bsLFill, 1.0f);
                setP (*p, P_bsFill, 1.0f);
                const auto log = logRun2 (*p, 172.0, 0.5, 8.0);
                bool ok = true; int inFill = 0;
                for (auto& e : log) if (e.layer && e.fired) { const bool f = e.step >= 16 && e.step < 32; ok = ok && e.pattern == (f ? 5 : 0); inFill += f ? 1 : 0; }
                CHECK (ok && inFill >= 6, "during a fill the layer plays the Fill Pattern's pair (C with Layer F)");
            }
            {
                // the layer editor edits the layer bank: changing the main pattern doesn't move it; Edit pair makes it jump to the pair
                auto p = twoSeq();
                p->breakStore.edit ([] (tg::BreakData& d) { d.lpair[1] = 5; });
                std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
                auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
                e->showPage ("Break Sequencer"); e->setBreakEditLayerForTests (true);
                setP (*p, P_bsLPattern, 3.0f); e->timerCallbackForTests();
                setP (*p, P_bsPattern, 1.0f); e->timerCallbackForTests();
                const int still = juce::roundToInt (plainOf (*p, P_bsLPattern));
                setP (*p, P_bsLEditPair, 1.0f); setP (*p, P_bsPattern, 0.0f); e->timerCallbackForTests();
                setP (*p, P_bsPattern, 1.0f); e->timerCallbackForTests();
                const int jumped = juce::roundToInt (plainOf (*p, P_bsLPattern));
                CHECK (still == 3 && jumped == 4, "the layer editor keeps its own pattern when the main changes (" + juce::String (still) + "); Edit pair jumps to the pair (" + juce::String (jumped) + ")");
            }
            {
                // pattern pads: learn a note for B, press it: B is selected (on the next bar, as a click does); the note isn't played
                auto p = twoSeq(); setP (*p, P_bsSwitch, 2.0f);
                p->patPadLearn = 1;
                { juce::AudioBuffer<float> b (2, 128); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100), 0); p->processBlock (b, m); }
                CHECK (p->patPadKind[1].load() == 1 && p->patPadNum[1].load() == 50 && p->patPadLearn.load() < 0 && p->patPadText (1) == "N50", "Learn takes the next note as B's pad");
                const double bpm = 172.0;
                std::vector<std::pair<int, juce::MidiMessage>> ev = { { (int) (1.3 * 60.0 / bpm * sr), juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) },
                                                                      { (int) (1.5 * 60.0 / bpm * sr), juce::MidiMessage::noteOff (1, 50) } };
                const auto log = logRun2 (*p, bpm, 0.0, 8.0, ev);
                bool ok = true; int firstB = -1;
                for (auto& e : log) if (! e.layer) { if (e.pattern == 1 && firstB < 0) firstB = (int) e.step; ok = ok && (e.step < 16 ? e.pattern == 0 : e.pattern == 1); }
                CHECK (ok && firstB == 16 && juce::roundToInt (plainOf (*p, P_bsPattern)) == 1, "a pad press selects B, starting on the next bar (Switch), as a click does");
                // a CC pad: one press selects once (no scrubbing through A-H)
                auto q = twoSeq(); q->patPadKind[3] = 2; q->patPadNum[3] = 21; setP (*q, P_bsPattern, 0.0f);
                for (int v : { 127, 64, 127, 0 }) { juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 21, v), 0); q->processBlock (b, m); }
                CHECK (juce::roundToInt (plainOf (*q, P_bsPattern)) == 3, "a CC pad selects its pattern (D) whatever values the button sends");
                // learn A-H in a row; the layer's pads pick the layer pattern
                auto r = make(); r->patPadLearnRun = true; r->patPadLearn = 8;
                for (int k = 0; k < 8; ++k) { juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (10, 36 + k, (juce::uint8) 100), 0); r->processBlock (b, m); }
                bool row = r->patPadLearn.load() < 0; for (int k = 0; k < 8; ++k) row = row && r->patPadKind[8 + k].load() == 1 && r->patPadNum[8 + k].load() == 36 + k;
                CHECK (row, "Learn A-H in a row: eight pads, one after another");
                { juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (10, 41, (juce::uint8) 100), 0); r->processBlock (b, m); }
                CHECK (juce::roundToInt (plainOf (*r, P_bsLPattern)) == 5, "a layer pad picks the layer pattern (F)");
                juce::MemoryBlock mb; r->getStateInformation (mb);
                auto r2 = make(); r2->setStateInformation (mb.getData(), (int) mb.getSize());
                CHECK (r2->patPadKind[12].load() == 1 && r2->patPadNum[12].load() == 40 && r2->patPadKind[1].load() == 0, "the pattern pads are saved with the project");
                // the pad's note isn't played
                auto level = [&] (bool asPad)
                {
                    auto s2 = make(); if (asPad) { s2->patPadKind[2] = 1; s2->patPadNum[2] = 60; }
                    double e = 0;
                    for (int i = 0; i < 30; ++i) { juce::AudioBuffer<float> o (2, 256); o.clear(); juce::MidiBuffer mm; if (i == 0) mm.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); s2->processBlock (o, mm); e += o.getMagnitude (0, 256); }
                    return e;
                };
                CHECK (level (false) > 0.1 && level (true) < 1.0e-6, "a pattern pad's note isn't played");
            }
            {
                // the per-step roll: x8 lands on 32 even 128ths of a beat... (8 per 16th), the ramp climbs, Tight cuts each hit short
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bsOn, 1.0f);
                p->breakStore.edit ([] (tg::BreakData& d) { d.step[0][4].on = true; d.step[0][4].slice = 4; d.step[0][4].roll = 8; d.step[0][4].rollRamp = 0.8f; d.step[0][4].level = 0.9f; });
                juce::AudioBuffer<float> cap;
                const auto out = runHost (*p, 120.0, 0.0, 2.0 * 60.0 / 120.0);
                // the hits: onsets every 1/8 of a 16th (at 120 BPM: 15.6 ms), rising in level
                const double hitGap = 0.25 / 8.0 * 60.0 / 120.0 * sr, t0 = 1.0 * 60.0 / 120.0 * sr;
                std::vector<double> peaks;
                for (int h = 0; h < 8; ++h)
                {
                    float pk = 0; const int a = (int) (t0 + h * hitGap), b = (int) (t0 + h * hitGap + 0.6 * hitGap);
                    for (int i = a; i < b && i < (int) out.size(); ++i) pk = std::max (pk, std::abs (out[(size_t) i]));
                    peaks.push_back (pk);
                }
                juce::String pl; for (auto x : peaks) pl << juce::String (x, 3) << " ";
                std::cout << "  roll x8 with Ramp 80%: the hits' peaks " << pl << std::endl;
                CHECK (peaks.back() > peaks.front() * 2.0 && peaks.front() > 0.0, "a roll's Ramp: its repeats climb (" + pl + ")");
                // Tight: with it, a single roll's last hit is gone a little after its gap
                auto tailOf = [&] (float tight)
                {
                    auto q = make(); setP (*q, P_bcOn, 1.0f); setP (*q, P_bcSliceMode, 3.0f); q->loadSampleData (demo(), "demo.wav", slot);
                    for (int k = 0; k < 16; ++k) q->breakStore.edit ([k] (tg::BreakData& d) { d.brk[0].slice[k].choke = 0; });
                    setP (*q, P_bsOn, 1.0f);
                    q->breakStore.edit ([tight] (tg::BreakData& d) { d.step[0][4].on = true; d.step[0][4].slice = 4; d.step[0][4].roll = 2; d.step[0][4].rollTight = tight; });
                    const auto o = runHost (*q, 120.0, 0.0, 2.0 * 60.0 / 120.0);
                    // the level from 2 gaps after the second hit: a 32nd at 120 BPM is 62.5 ms
                    const int from = (int) (t0 + 3.0 * 0.125 * 60.0 / 120.0 * sr), to = from + (int) (0.05 * sr);
                    double e = 0; for (int i = from; i < to && i < (int) o.size(); ++i) e += (double) o[(size_t) i] * o[(size_t) i];
                    return e;
                };
                const double loose = tailOf (0.0f), tight = tailOf (1.0f);
                CHECK (loose > 1.0e-6 && tight < loose * 0.01, "Roll Tight cuts each hit short (" + juce::String (tight / std::max (1.0e-12, loose), 4) + " of the energy left)");
            }
            {
                // the Snare Roll tool: one clean snare all the way, speeding up, building, a small rise, ending with the pattern
                auto p = make(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
                p->loadSampleData (demo(), "demo.wav", slot);
                setP (*p, P_bsSteps, 1.0f);
                p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 0);
                const auto before = p->breakStore.get();
                setP (*p, P_bsRollLen, 2.0f); setP (*p, P_bsRollCurve, 1.0f); setP (*p, P_bsRollCresc, 0.8f); setP (*p, P_bsRollPitch, 2.0f); setP (*p, P_bsRollVar, 0.0f);
                p->runBreakTool (tg::breaktools::ToolSnareBuild, false, 0);
                const auto d = p->breakStore.get();
                const auto& b = d.brk[0];
                const int want = tg::breaktools::cleanSnare (b);
                bool sameSlice = true, rising = true, fast = true; float lastLvl = -1.0f, maxPitch = 0.0f; int prevR = 1;
                for (int i = 16; i < 32; ++i)
                {
                    const auto& s = d.step[0][i];
                    sameSlice = sameSlice && s.on && s.slice == want;
                    rising = rising && s.level >= lastLvl - 1.0e-6f; lastLvl = s.level;
                    fast = fast && s.roll >= prevR; prevR = s.roll;
                    maxPitch = std::max (maxPitch, s.pitch + s.rollPitch);
                }
                bool untouched = true; for (int i = 0; i < 16; ++i) untouched = untouched && d.step[0][i].slice == before.step[0][i].slice && d.step[0][i].on == before.step[0][i].on;
                std::cout << "  Snare Roll (1 bar, Accelerate): slice " << (want + 1) << " (snare " << juce::String (b.snare[want], 2) << ", kick " << juce::String (b.kick[want], 2) << "), rolls";
                for (int i = 16; i < 32; ++i) std::cout << " x" << d.step[0][i].roll; std::cout << "; levels " << juce::String (d.step[0][16].level, 2) << " .. " << juce::String (d.step[0][31].level, 2) << std::endl;
                CHECK (sameSlice && rising && fast && d.step[0][16].roll == 1 && d.step[0][31].roll == 4 && d.step[0][16].level < 0.3f && d.step[0][31].level > 0.95f
                       && maxPitch <= 2.0f + 1.0e-3f && maxPitch > 1.5f && untouched,
                       "Snare Roll: the cleanest snare all through, 16ths to 32nds to 64ths, a crescendo, a 2 st rise, only the last bar changed");
                CHECK (b.snare[want] > 0.3f, "the Auto snare is a snare (by the analysis)");
                // Buzz finish: the last half beat at x8; Gap: the last 16th silent, or a reverse snare
                setP (*p, P_bsRollCurve, 2.0f); setP (*p, P_bsRollGap, 2.0f);
                p->runBreakTool (tg::breaktools::ToolSnareBuild, false, 0);
                const auto d2 = p->breakStore.get();
                CHECK (d2.step[0][29].roll == 8 && d2.step[0][30].roll == 8 && d2.step[0][31].reverse && d2.step[0][31].roll == 1, "Buzz finish x8, and a reverse snare into the downbeat");
                setP (*p, P_bsRollGap, 1.0f); setP (*p, P_bsRollLen, 0.0f);
                p->runBreakTool (tg::breaktools::ToolSnareBuild, false, 0);
                const auto d3 = p->breakStore.get();
                CHECK (! d3.step[0][31].on && d3.step[0][28].on && d3.step[0][30].on, "a 1-beat roll with a 1/16 gap before the downbeat");
                // an older pattern made with the old Snare Build keeps its steps; saved and loaded, the roll's shape stays
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
                const auto dq = q->breakStore.get();
                bool same = true; for (int i = 0; i < 32; ++i) { const auto& x = d3.step[0][i]; const auto& y = dq.step[0][i]; same = same && x.roll == y.roll && x.level == y.level && x.rollRamp == y.rollRamp && x.rollTight == y.rollTight && x.rollPitch == y.rollPitch; }
                CHECK (same, "the roll's steps (Roll x8, Level, Ramp, Tight, Pitch) are saved with the project");
            }
            {
                // CPU: both sequencers rolling x8 on every step (Ramp, Tight, Pitch on), four breaks loaded, 172 BPM
                auto p = twoSeq();
                p->breakStore.edit ([] (tg::BreakData& d) { for (int q = 0; q < 8; ++q) for (int i = 0; i < 16; ++i) for (auto* st : { &d.step[q][i], &d.lstep[q][i] }) { st->on = true; st->roll = 8; st->rollRamp = 0.6f; st->rollTight = 0.5f; st->rollPitch = 2.0f; } });
                double best = 1.0e9;
                for (int run = 0; run < 3; ++run) { const auto t0 = juce::Time::getMillisecondCounterHiRes(); runHost (*p, 172.0, 0.0, 5.0); best = std::min (best, juce::Time::getMillisecondCounterHiRes() - t0); }
                std::cout << "  CPU, both sequencers rolling x8 on every step: " << juce::String (best / 50.0, 2) << "% of one core" << std::endl;
                CHECK (best / 50.0 < 15.0, "64th rolls on both sequencers stay well within real time");
            }
        }
        // 10. CPU: 8 slices at once in each stretch mode, Eco / Normal / High (the chopper alone)
        {
            WaveSample w; w.numChannels = 2; w.sampleRate = 44100.0; const auto b = tg::breaks::makeDemoBreak (44100.0, 170.0, 0);
            w.length = b.getNumSamples(); w.ch[0].assign (b.getReadPointer (0), b.getReadPointer (0) + w.length); w.ch[1].assign (b.getReadPointer (1), b.getReadPointer (1) + w.length); w.peak = 1.0f;
            juce::String line;
            for (int mode = 0; mode < 3; ++mode)
            {
                line << kBcStretchLabels[mode] << ":";
                for (int q = 0; q < 3; ++q)
                {
                    tg::BreakChopper c; c.prepare (sr, 256); c.setSample (0, &w);
                    tg::BreakData d; tg::breaks::analyse (&w, 3, 0.5f, 2, 0.0, d.brk[0]);
                    for (int k = 0; k < 16; ++k) { d.brk[0].slice[k].choke = 0; d.brk[0].slice[k].loop = true; }
                    c.data() = d;
                    tg::BreakChopper::Settings st; st.b[0].stretch = mode; st.quality = q; st.hostTempo = 140.0;
                    for (int k = 0; k < 8; ++k) { tg::BreakChopper::Trigger t; t.slice = k * 2; c.trigger (t, st); }
                    std::vector<float> l (256), r (256);
                    std::vector<float> v (P_COUNT, 0.0f);
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    for (int blk = 0; blk < (int) (5.0 * sr / 256); ++blk) { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); c.process (l.data(), r.data(), 256, st, v.data()); }
                    line << " " << juce::String (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / 5.0 * 100.0, 2) << "%";
                }
                line << "   ";
            }
            std::cout << "  CPU, 8 looping slices at once (Eco / Normal / High, % of a core): " << line << std::endl;
            // three breaks at once: 8 slices spread over them, each break with its own colour
            {
                tg::BreakChopper c; c.prepare (sr, 256);
                tg::BreakData d;
                for (int k = 0; k < 3; ++k) { c.setSample (k, &w); tg::breaks::analyse (&w, 3, 0.5f, 2, 0.0, d.brk[k]); for (auto& sl : d.brk[k].slice) { sl.choke = 0; sl.loop = true; } }
                c.data() = d;
                tg::BreakChopper::Settings st; st.hostTempo = 140.0;
                for (int k = 0; k < 3; ++k) { st.b[k].stretch = k; st.b[k].colour = k + 1; st.b[k].level = 0.8f; st.b[k].pan = 0.2f * (float) (k - 1); }
                for (int k = 0; k < 8; ++k) { tg::BreakChopper::Trigger t; t.slice = k * 2; t.brk = k % 3; c.trigger (t, st); }
                std::vector<float> l (256), r (256), v (P_COUNT, 0.0f);
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int blk = 0; blk < (int) (5.0 * sr / 256); ++blk) { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); c.process (l.data(), r.data(), 256, st, v.data()); }
                std::cout << "  CPU, 8 slices over three breaks (one each: Repitch / Cyclic / Clean, all coloured), Normal: "
                          << juce::String (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / 5.0 * 100.0, 2) << "% of a core" << std::endl;
            }
        }
    };
    // ---- the Reese engine
    auto reeseTests = [&]
    {
        std::cout << "Reese engine" << std::endl;
        auto plainOf = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
        // the beating rate of a two-voice Reese in a signal: demodulated at the note, low-passed, the power
        // envelope's strongest component near the expected rate (a fine search of its spectrum)
        auto beatRateOf = [&] (const std::vector<float>& x, double f, double expect, int skip)
        {
            const double fc = std::min (f * 0.35, std::max (3.0 * expect, 2.0));
            const double a = 1.0 - std::exp (-2.0 * kPi * fc / sr);
            std::complex<double> z[4] {};
            const int D = std::max (1, (int) (sr / (16.0 * std::max (fc, expect))));
            std::vector<double> env;
            for (int i = 0; i < (int) x.size(); ++i)
            {
                const double ph = 2.0 * kPi * f * i / sr;
                std::complex<double> c = (double) x[(size_t) i] * std::complex<double> (std::cos (ph), -std::sin (ph));
                for (auto& s : z) { s += (c - s) * a; c = s; }
                if (i >= skip && (i - skip) % D == 0) env.push_back (std::norm (z[3]));
            }
            double mean = 0; for (double e : env) mean += e; mean /= (double) std::max<size_t> (1, env.size());
            const double fs2 = sr / D;
            const int n = (int) env.size();
            auto mag = [&] (double fr)
            {
                std::complex<double> acc {};
                for (int i = 0; i < n; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
                    const double ph = 2.0 * kPi * fr * i / fs2;
                    acc += (env[(size_t) i] - mean) * w * std::complex<double> (std::cos (ph), -std::sin (ph));
                }
                return std::abs (acc);
            };
            double best = expect, bm = -1;
            for (int k = 0; k <= 80; ++k) { const double fr = expect * (0.8 + 0.4 * k / 80.0); const double m = mag (fr); if (m > bm) { bm = m; best = fr; } }
            double lo = best - expect * 0.005, hi = best + expect * 0.005;
            for (int it = 0; it < 40; ++it) { const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3; if (mag (m1) < mag (m2)) lo = m1; else hi = m2; }
            return 0.5 * (lo + hi);
        };
        auto noteHz = [] (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); };
        auto plainSettings = []
        {
            ReeseSettings s; s.twist = 0.0f; s.width = 0.0f; s.monoHz = 0.0f; s.rotate = 0.0f; return s;
        };
        auto renderOsc = [&] (ReeseOsc& o, double f, double tempo, const ReeseSettings& s, int samples, std::vector<float>& l, std::vector<float>* r = nullptr)
        {
            o.setFrequency (f, tempo, s);
            l.resize ((size_t) samples); if (r) r->resize ((size_t) samples);
            for (int i = 0; i < samples; ++i) { float a, b; o.tick (a, b); l[(size_t) i] = a + b; if (r) { l[(size_t) i] = a; (*r)[(size_t) i] = b; } }
        };
        // 1. Beats mode: the beating period matches the note value within 1%, at four tempos, C1 to C5
        {
            double worst = 0; juce::String worstAt;
            int cases = 0;
            const int beats[] = { 0, 2, 3, 4, 7, 9, 11 };   // 1/16, 1/4, 1/2, 1 bar, 1/4 dotted, 1/4T... (and 4 bars below)
            for (double bpm : { 90.0, 140.0, 172.0, 174.0 })
                for (int note : { 24, 36, 48, 60, 72 })
                    for (int b : beats)
                    {
                        const double expect = bpm / 60.0 / kRsBeatBeats[b];
                        if (expect > noteHz (note) * 0.4) continue;   // (a beating faster than the note itself is not a Reese)
                        ReeseOsc o; o.prepare (sr); o.start (1, 1);
                        auto s = plainSettings(); s.mode = 2; s.beat = b;
                        std::vector<float> x;
                        const int skip = (int) (sr * 1.5 / std::min (noteHz (note) * 0.35, std::max (3.0 * expect, 2.0)));
                        renderOsc (o, noteHz (note), bpm, s, skip + (int) (sr * 6.0 / expect), x);
                        const double got = beatRateOf (x, noteHz (note), expect, skip);
                        const double err = std::abs (got / expect - 1.0);
                        ++cases;
                        if (err > worst) { worst = err; worstAt = juce::String (bpm) + " BPM, note " + juce::String (note) + ", " + kListRsBeat.labels[b]; }
                    }
            std::cout << "  Beats mode: " << cases << " cases, worst period error " << juce::String (worst * 100.0, 3) << "% (" << worstAt << ")" << std::endl;
            CHECK (worst < 0.01, "Reese beating period within 1% of the note value");
            // the 4-bar setting at 90 BPM (the slowest: a 10.7 s cycle)
            {
                ReeseOsc o; o.prepare (sr); o.start (1, 1);
                auto s = plainSettings(); s.mode = 2; s.beat = 0;
                const double expect = 90.0 / 60.0 / 16.0;
                std::vector<float> x; renderOsc (o, noteHz (36), 90.0, s, (int) (sr * 4.0) + (int) (sr * 5.0 / expect), x);
                const double got = beatRateOf (x, noteHz (36), expect, (int) (sr * 4.0));
                std::cout << "  4 bars at 90 BPM: " << juce::String (1.0 / got, 3) << " s (expected " << juce::String (1.0 / expect, 3) << " s)" << std::endl;
                CHECK (std::abs (got / expect - 1.0) < 0.01, "4-bar beating at 90 BPM within 1%");
            }
            // 3 and 4 voices: every difference between voices a whole multiple of the beat (x3 and x5),
            // so the pattern repeats once per note value; golden: voices 3-4 beat freely
            for (int ratio = 0; ratio < 3; ++ratio)
            {
                ReeseOsc o; o.prepare (sr);
                auto s = plainSettings(); s.mode = 2; s.beat = 3; s.voices = 2; s.ratio = ratio;
                o.setFrequency (noteHz (36), 172.0, s);
                const double d = o.beatRate();
                double maxFrac = 0;
                for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j)
                {
                    const double q = (o.voiceFrequency (j) - o.voiceFrequency (i)) / d;
                    maxFrac = std::max (maxFrac, std::abs (q - std::round (q)));
                }
                if (ratio < 2) CHECK (maxFrac < 1e-9, juce::String ("4 voices x") + (ratio == 0 ? "3" : "5") + ": locked to the beat");
                else CHECK (maxFrac > 0.05, "4 voices golden: free (not locked)");
            }
            // the voice frequencies are centred on the note (the pitch does not drift with the beat)
            {
                ReeseOsc o; o.prepare (sr); auto s = plainSettings(); s.mode = 2; s.beat = 0; s.voices = 2;
                o.setFrequency (noteHz (45), 172.0, s);
                double mean = 0; for (int k = 0; k < 4; ++k) mean += o.voiceFrequency (k) / 4.0;
                CHECK (std::abs (mean - noteHz (45)) < 1e-9, "voices centred on the note");
            }
        }
        // 2. Hz and Cents modes
        {
            ReeseOsc o; o.prepare (sr); o.start (1, 1);
            auto s = plainSettings(); s.mode = 1; s.hz = 2.0f;
            for (int note : { 36, 60 })
            {
                std::vector<float> x; renderOsc (o, noteHz (note), 172.0, s, (int) (sr * 4.0), x);
                CHECK (std::abs (beatRateOf (x, noteHz (note), 2.0, (int) sr) / 2.0 - 1.0) < 0.01, "Hz mode: the same beating on every note (" + juce::String (note) + ")");
            }
            s.mode = 0; s.cents = 20.0f;
            o.setFrequency (noteHz (48), 120.0, s);
            CHECK (std::abs (1200.0 * std::log2 (o.voiceFrequency (1) / o.voiceFrequency (0)) - 20.0) < 1e-6, "Cents mode: voices 1-2 are 20 cents apart");
        }
        // 3. the engine: Beats mode follows the host tempo (and a tempo change), Reese only
        auto reeseOnly = [&]
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain })
                setP (*p, i, 0.0f);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f); setP (*p, P_ampS, 1.0f);
            setP (*p, P_rsLevel, 0.7f); setP (*p, P_rsTwist, 0.0f); setP (*p, P_rsWidth, 0.0f);
            return p;
        };
        struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        {
            auto p = reeseOnly(); setP (*p, P_rsBeat, 4.0f);   // 1/4
            Head head; head.info.setBpm (140.0); head.info.setIsPlaying (true); p->setPlayHead (&head);
            const int half = (int) (sr * 4.0);
            juce::AudioBuffer<float> all (2, half * 2), buf (2, 256);
            for (int pos = 0; pos < half * 2; pos += 256)
            {
                if (pos >= half) head.info.setBpm (172.0);
                juce::MidiBuffer m; if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), 0);
                buf.clear(); p->processBlock (buf, m);
                for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, buf, c, 0, 256);
            }
            p->setPlayHead (nullptr);
            std::vector<float> a ((size_t) half), b ((size_t) half);
            for (int i = 0; i < half; ++i) { a[(size_t) i] = all.getSample (0, i) + all.getSample (1, i); b[(size_t) i] = all.getSample (0, half + i) + all.getSample (1, half + i); }
            const double r1 = beatRateOf (a, noteHz (45), 140.0 / 60.0, (int) (sr * 0.5)), r2 = beatRateOf (b, noteHz (45), 172.0 / 60.0, (int) (sr * 0.5));
            std::cout << "  in the synth, 1/4 at 140 then 172 BPM: " << juce::String (r1, 4) << " Hz, " << juce::String (r2, 4) << " Hz" << std::endl;
            CHECK (std::abs (r1 / (140.0 / 60.0) - 1.0) < 0.01 && std::abs (r2 / (172.0 / 60.0) - 1.0) < 0.01, "the beating follows the host tempo, and a tempo change");
            float jump = 0; for (int i = half - 4; i < half + 4; ++i) jump = std::max (jump, std::abs (all.getSample (0, i + 1) - all.getSample (0, i)));
            float typical = 0; for (int i = half / 2; i < half / 2 + 2000; ++i) typical = std::max (typical, std::abs (all.getSample (0, i + 1) - all.getSample (0, i)));
            CHECK (jump <= typical * 1.5f, "no click at the tempo change");
        }
        // 4. no aliasing above -60 dB at C7 (Quality High), with and without the drive
        {
            const int N = 1 << 16;
            auto aliasDb = [&] (float drive, int voices, int wave, int quality = 2)
            {
                auto p = reeseOnly(); setP (*p, P_quality, (float) quality); setP (*p, P_rsDrive, drive); setP (*p, P_rsVoices, (float) voices); setP (*p, P_rsWave, (float) wave);
                setP (*p, P_rsDetMode, 0.0f); setP (*p, P_rsCents, drive > 0.0f ? 0.0f : 14.0f); setP (*p, P_outGain, -12.0f);
                juce::AudioBuffer<float> c; render (*p, 0.4 + N / sr, chord (0.0, 5.0, { 96 }), &c);
                std::vector<float> fft ((size_t) N * 2, 0.0f);
                const int off = (int) (0.4 * sr);
                for (int i = 0; i < N; ++i)
                {
                    const double w = 0.35875 - 0.48829 * std::cos (2 * kPi * i / N) + 0.14128 * std::cos (4 * kPi * i / N) - 0.01168 * std::cos (6 * kPi * i / N);
                    fft[(size_t) i] = (float) (c.getSample (0, off + i) * w);
                }
                juce::dsp::FFT f (16); f.performFrequencyOnlyForwardTransform (fft.data());
                // the voices' harmonics (and the notes' intermodulation, which sits next to them) are allowed;
                // anything else is aliasing
                ReeseOsc probe; probe.prepare (sr); ReeseSettings s = plainSettings(); s.mode = 0; s.cents = drive > 0.0f ? 0.0f : 14.0f; s.voices = voices;
                probe.setFrequency (noteHz (96), 130.0, s);
                double fLo = 1e9, fHi = 0; for (int k = 0; k < probe.voiceCount(); ++k) { fLo = std::min (fLo, probe.voiceFrequency (k)); fHi = std::max (fHi, probe.voiceFrequency (k)); }
                double peak = 0, alias = 0, aliasHz = 0;
                for (int k = 2; k < N / 2; ++k)
                {
                    const double hz = k * sr / N, m = fft[(size_t) k];
                    peak = std::max (peak, m);
                    const double h = std::round (hz / noteHz (96));
                    // (the detuned voices' harmonics spread round each harmonic; with the drive the voices are not
                    //  detuned, so its intermodulation does not hide anything: aliases of C7 land about 146 Hz off
                    //  the harmonics at 48 kHz)
                    const bool harmonic = h >= 1 && std::abs (hz - h * noteHz (96)) < h * (fHi - fLo) + 12.0;
                    if (! harmonic && hz > 30.0 && m > alias) { alias = m; aliasHz = hz; }
                }
                juce::ignoreUnused (aliasHz);
                return 20.0 * std::log10 (std::max (1e-12, alias / peak));
            };
            const double a0 = aliasDb (0.0f, 0, 0), a1 = aliasDb (0.0f, 2, 2), a2 = aliasDb (0.6f, 0, 0), a3 = aliasDb (1.0f, 0, 1);
            std::cout << "  C7, High: aliasing " << juce::String (a0, 1) << " dB (2 saws), " << juce::String (a1, 1) << " dB (4 voices, blend), "
                      << juce::String (a2, 1) << " dB (2 saws, drive 60%, no detune), " << juce::String (a3, 1) << " dB (2 squares, drive 100%, no detune)" << std::endl;
            CHECK (a0 < -60.0 && a1 < -60.0 && a2 < -60.0 && a3 < -60.0, "no aliasing above -60 dB at C7 on High");
            std::cout << "  (2 squares, drive 100%: Normal " << juce::String (aliasDb (1.0f, 0, 1, 1), 1) << " dB, Eco " << juce::String (aliasDb (1.0f, 0, 1, 0), 1) << " dB)" << std::endl;
        }
        // 5. Mono Below: the low end the same both sides, the top still wide
        {
            auto sideMid = [&] (float monoHz, double lo, double hi)
            {
                ReeseOsc o; o.prepare (sr); o.start (1, 3);
                auto s = plainSettings(); s.width = 1.0f; s.rotate = 0.5f; s.rotRate = 0.7f; s.voices = 2; s.monoHz = monoHz; s.mode = 2; s.beat = 3;
                std::vector<float> l, r; renderOsc (o, noteHz (24), 172.0, s, 1 << 17, l, &r);
                const int N = 1 << 16;
                std::vector<float> fm ((size_t) N * 2, 0.0f), fs ((size_t) N * 2, 0.0f);
                for (int i = 0; i < N; ++i) { const float w = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * i / N); fm[(size_t) i] = 0.5f * (l[(size_t) (N + i)] + r[(size_t) (N + i)]) * w; fs[(size_t) i] = 0.5f * (l[(size_t) (N + i)] - r[(size_t) (N + i)]) * w; }
                juce::dsp::FFT f (16); f.performFrequencyOnlyForwardTransform (fm.data()); f.performFrequencyOnlyForwardTransform (fs.data());
                double em = 0, es = 0;
                for (int k = 1; k < N / 2; ++k) { const double hz = k * sr / N; if (hz >= lo && hz < hi) { em += (double) fm[(size_t) k] * fm[(size_t) k]; es += (double) fs[(size_t) k] * fs[(size_t) k]; } }
                return 10.0 * std::log10 (std::max (1e-20, es) / std::max (1e-20, em));
            };
            const double offLow = sideMid (0.0f, 20.0, 70.0), onLow = sideMid (120.0f, 20.0, 70.0), onHigh = sideMid (120.0f, 500.0, 4000.0);
            std::cout << "  Mono Below 120 Hz, C1 (side / mid): below 70 Hz " << juce::String (offLow, 1) << " dB -> " << juce::String (onLow, 1) << " dB; 500 Hz - 4 kHz " << juce::String (onHigh, 1) << " dB" << std::endl;
            CHECK (onLow < -30.0 && onLow < offLow - 20.0, "Mono Below: the low end is mono");
            CHECK (onHigh > -12.0, "Mono Below: the top stays wide");
            // in the synth (default 120 Hz): C1, full width
            auto p = reeseOnly(); setP (*p, P_rsWidth, 1.0f); setP (*p, P_rsRotate, 0.5f);
            juce::AudioBuffer<float> c; render (*p, 1.5, chord (0.0, 1.5, { 24 }), &c);
            const int N = 1 << 16, off = (int) (0.1 * sr);
            std::vector<float> fm ((size_t) N * 2, 0.0f), fs ((size_t) N * 2, 0.0f);
            for (int i = 0; i < N; ++i) { const float w = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * i / N); fm[(size_t) i] = 0.5f * (c.getSample (0, off + i) + c.getSample (1, off + i)) * w; fs[(size_t) i] = 0.5f * (c.getSample (0, off + i) - c.getSample (1, off + i)) * w; }
            juce::dsp::FFT f (16); f.performFrequencyOnlyForwardTransform (fm.data()); f.performFrequencyOnlyForwardTransform (fs.data());
            double em = 0, es = 0;
            for (int k = 1; k < N / 2; ++k) { const double hz = k * sr / N; if (hz >= 20 && hz < 70) { em += (double) fm[(size_t) k] * fm[(size_t) k]; es += (double) fs[(size_t) k] * fs[(size_t) k]; } }
            const double db = 10.0 * std::log10 (std::max (1e-20, es) / std::max (1e-20, em));
            std::cout << "  in the synth, C1 full width: side / mid below 70 Hz " << juce::String (db, 1) << " dB" << std::endl;
            CHECK (db < -30.0, "in the synth: the Reese's low end is mono");
        }
        // 6. the sub: a sine an octave below voice 1, phase-locked to it (no drift over 20 s, any mode)
        for (int mode : { 0, 2 })
        {
            auto s = plainSettings(); s.mode = mode; s.beat = 2; s.voices = 2;
            ReeseOsc a, b; a.prepare (sr); b.prepare (sr); a.start (1, 1); b.start (1, 1);
            s.sub = 0.0f; std::vector<float> x0; renderOsc (a, noteHz (36), 172.0, s, (int) (sr * 20.0), x0);
            s.sub = 1.0f; std::vector<float> x1; renderOsc (b, noteHz (36), 172.0, s, (int) (sr * 20.0), x1);
            const double f0 = b.voiceFrequency (0);
            auto phaseAt = [&] (int from)
            {
                std::complex<double> acc {};
                const int len = (int) std::round (sr / (f0 * 0.5) * 20.0);
                for (int i = from; i < from + len; ++i) { const double ph = 2.0 * kPi * (f0 * 0.5) * i / sr; acc += (double) (x1[(size_t) i] - x0[(size_t) i]) * std::complex<double> (std::cos (ph), -std::sin (ph)); }
                return std::arg (acc);
            };
            const double p0 = phaseAt (0), p1 = phaseAt ((int) (sr * 18.0));
            double d = std::abs (p1 - p0); if (d > kPi) d = 2.0 * kPi - d;
            std::cout << "  sub vs voice 1 (" << (mode == 0 ? "Cents" : "Beats") << "): phase drift over 18 s " << juce::String (d * 180.0 / kPi, 4) << " degrees" << std::endl;
            CHECK (d < 0.01, "the sub is phase-locked to voice 1 (an octave down)");
        }
        // 7. off: bit-exact silence and no change to anything else
        {
            juce::AudioBuffer<float> ref, alt;
            { auto p = make(); setP (*p, P_reverbMix, 0.0f); tg::resetVoiceRandom(); render (*p, 0.8, chord (0.0, 0.6, { 45, 52 }), &ref); }
            { auto p = make(); setP (*p, P_reverbMix, 0.0f);
              for (auto pr : std::initializer_list<std::pair<int, float>> { { P_rsVoices, 2 }, { P_rsSub, 1 }, { P_rsDrive, 1 }, { P_rsNotch, 1 }, { P_rsWidth, 1 }, { P_mxReesePan, 0.8f } }) setP (*p, pr.first, pr.second);
              tg::resetVoiceRandom(); render (*p, 0.8, chord (0.0, 0.6, { 45, 52 }), &alt); }
            double d = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < ref.getNumSamples(); ++i) d = std::max (d, (double) std::abs (ref.getSample (c, i) - alt.getSample (c, i)));
            CHECK (d == 0.0, "Reese off (Level 0): the sound is bit-identical, whatever its other settings");
            auto p = reeseOnly(); setP (*p, P_rsLevel, 0.0f);
            juce::AudioBuffer<float> c; render (*p, 0.5, chord (0.0, 0.5, { 45 }), &c);
            CHECK (c.getMagnitude (0, c.getNumSamples()) == 0.0f, "Reese off, nothing else on: exact silence");
            // turned off while playing: it fades, then exact zeros
            auto q = reeseOnly(); juce::AudioBuffer<float> c1, c2;
            render (*q, 0.3, chord (0.0, 2.0, { 45 }), &c1);
            CHECK (c1.getMagnitude (0, (int) (0.1 * sr), (int) (0.2 * sr)) > 0.05f, "Reese on: sound");
            setP (*q, P_rsLevel, 0.0f); render (*q, 0.5, {}, &c2);
            // (the Reese itself stops exactly; what is left is the voice filter's own tail dying away)
            CHECK (c2.getMagnitude (0, (int) (0.25 * sr), (int) (0.25 * sr)) < 1.0e-9f, "Reese turned off while playing: silent (below -180 dB) after the fade");
            float step = 0; for (int i = 0; i + 1 < 2000; ++i) step = std::max (step, std::abs (c2.getSample (0, i + 1) - c2.getSample (0, i)));
            CHECK (step < 0.2f, "turning it off does not click");
        }
        // 8. Retrigger: Reset starts every note the same; Random different; Free carries on
        {
            auto first = [&] (int retrig, int note2)
            {
                auto p = reeseOnly(); setP (*p, P_rsRetrig, (float) retrig); setP (*p, P_ampR, 0.005f); setP (*p, P_ampA, 0.0f);
                juce::AudioBuffer<float> c; render (*p, 0.6, { { 0.0, juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100) }, { 0.25, juce::MidiMessage::noteOff (1, 45) },
                                                           { 0.4, juce::MidiMessage::noteOn (1, note2, (juce::uint8) 100) } }, &c);
                double d = 0; const int a = 100, b = (int) (0.4 * sr) + 100;
                for (int i = 0; i < 600; ++i) d = std::max (d, (double) std::abs (c.getSample (0, a + i) - c.getSample (0, b + i)));
                return d;
            };
            std::cout << "  Retrigger: two notes' starts differ by " << first (1, 45) << " (Reset), " << first (2, 45) << " (Random), " << first (0, 45) << " (Free)" << std::endl;
            CHECK (first (1, 45) < 0.02 && first (2, 45) > 0.02, "Retrigger: Reset repeats the attack, Random does not");
        }
        // 9. the Mixer strip (pan, mute, solo), a Mod Matrix route on its Level, the Reese Phase source
        {
            auto p = reeseOnly(); setP (*p, P_mxReesePan, -1.0f);
            juce::AudioBuffer<float> c; render (*p, 0.4, chord (0.0, 0.4, { 45 }), &c);
            CHECK (c.getRMSLevel (1, 2000, 10000) < c.getRMSLevel (0, 2000, 10000) * 0.05f, "Mixer: Reese pan hard left");
            auto q = reeseOnly(); setP (*q, P_mxReeseMute, 1.0f); render (*q, 0.4, chord (0.0, 0.4, { 45 }), &c);
            CHECK (c.getMagnitude (0, 4000, 10000) < 1e-6f, "Mixer: Reese mute");
            auto r = reeseOnly(); setP (*r, P_osc1Gain, 0.8f); setP (*r, P_mxReeseSolo, 1.0f); juce::AudioBuffer<float> c2; render (*r, 0.4, chord (0.0, 0.4, { 45 }), &c2);
            auto r2 = reeseOnly(); setP (*r2, P_osc1Gain, 0.8f); setP (*r2, P_osc1Wave, 3.0f); setP (*r2, P_mxReeseSolo, 1.0f); juce::AudioBuffer<float> c3; render (*r2, 0.4, chord (0.0, 0.4, { 45 }), &c3);
            CHECK (c2.getRMSLevel (0, 4000, 10000) > 0.01f && std::abs (c2.getRMSLevel (0, 4000, 10000) - c3.getRMSLevel (0, 4000, 10000)) < 1e-4f, "Mixer: Reese solo (Osc 1 not heard)");
            auto m = reeseOnly(); setP (*m, P_rsLevel, 0.1f); m->addRoute (MS_Macro1, P_rsLevel, 0.8f); setP (*m, P_macro1, 1.0f);
            juce::AudioBuffer<float> cm, cn; render (*m, 0.4, chord (0.0, 0.4, { 45 }), &cm);
            auto n = reeseOnly(); setP (*n, P_rsLevel, 0.1f); render (*n, 0.4, chord (0.0, 0.4, { 45 }), &cn);
            CHECK (cm.getRMSLevel (0, 4000, 10000) > cn.getRMSLevel (0, 4000, 10000) * 3.0f, "Mod Matrix: a route on Reese Level");
            juce::StringArray notMod;
            for (int i = P_rsLevel; i <= P_rsTone; ++i)
            {
                const auto& mt = tg::meta (i);
                const bool cont = mt.scale != tg::Scale::Choice && i != P_rsOct && i != P_rsSemi && i != P_rsNotchSync;
                if (cont && ! mt.modulatable) notMod.add (kParamIds[i]);
            }
            CHECK (notMod.isEmpty(), "every continuous Reese control is a Mod Matrix destination" + (notMod.isEmpty() ? juce::String() : ": " + notMod.joinIntoString (", ")));
            // the Reese Phase source: a route from it to the Width moves the sound (and it rises with the beat)
            auto w = reeseOnly(); setP (*w, P_rsWidth, 0.0f); w->addRoute (MS_ReesePhase, P_rsWidth, 1.0f);
            juce::AudioBuffer<float> cw; render (*w, 1.0, chord (0.0, 1.0, { 45 }), &cw);
            double side = 0; for (int i = (int) (0.2 * sr); i < (int) sr; ++i) side += std::abs (cw.getSample (0, i) - cw.getSample (1, i));
            CHECK (side > 1.0, "Reese Phase as a Mod Matrix source");
        }
        // 10. saved: patches, projects, undo, A/B; old projects and patches load with the Reese off
        {
            auto p = make(); setP (*p, P_rsLevel, 0.6f); setP (*p, P_rsVoices, 2.0f); setP (*p, P_rsBeat, 7.0f); setP (*p, P_mxReesePan, 0.3f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (std::abs (plainOf (*q, P_rsLevel) - 0.6f) < 1e-4f && juce::roundToInt (plainOf (*q, P_rsVoices)) == 2 && juce::roundToInt (plainOf (*q, P_rsBeat)) == 7
                   && std::abs (plainOf (*q, P_mxReesePan) - 0.3f) < 1e-4f, "Reese settings saved with the project");
            auto x = juce::parseXML (getXmlFromBinaryForTest (mb));
            if (x != nullptr)
            {
                x->setAttribute ("stateVersion", 23);
                for (int i = P_rsLevel; i <= P_mxReeseSolo; ++i) if (auto* el = x->getChildByAttribute ("id", kParamIds[i])) x->removeChildElement (el, true);
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*x, old);
                auto o = make(); setP (*o, P_rsLevel, 0.9f); o->setStateInformation (old.getData(), (int) old.getSize());
                CHECK (plainOf (*o, P_rsLevel) == 0.0f && juce::roundToInt (plainOf (*o, P_rsVoices)) == 0, "older projects load with the Reese off");
            }
            const auto patch = p->exportBrowserPatch();
            // 11. CPU: 8 notes held, the Reese on top of the Init sound (best of 3 runs, 3 s each)
            {
                auto cost = [&] (int voices, int quality, float drive)
                {
                    double best = 1e9;
                    for (int run = 0; run < 3; ++run)
                    {
                        auto q = make(); for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) setP (*q, i, 0.0f);
                        setP (*q, P_quality, (float) quality);
                        if (voices > 0) { setP (*q, P_rsLevel, 0.6f); setP (*q, P_rsVoices, (float) (voices - 2)); setP (*q, P_rsDrive, drive); setP (*q, P_rsNotch, 0.5f); }
                        double cpu = 0; render (*q, 3.0, chord (0.0, 2.9, { 36, 40, 43, 47, 48, 52, 55, 59 }), nullptr, &cpu);
                        best = std::min (best, cpu / 3.0 * 100.0);
                    }
                    return best;
                };
                for (int quality = 0; quality < 3; ++quality)
                {
                    const double base = cost (0, quality, 0.0f);
                    juce::String line; line << "  CPU, 8 notes, " << kQualityLabels[quality] << ": Init " << juce::String (base, 2) << "%; Reese adds";
                    for (int v : { 2, 3, 4 }) line << " " << v << "v " << juce::String (cost (v, quality, 0.0f) - base, 2) << "%";
                    line << "; 4v + drive " << juce::String (cost (4, quality, 0.5f) - base, 2) << "%";
                    std::cout << line << " (of one core)" << std::endl;
                }
            }
            auto r = make(); CHECK (r->importBrowserPatch (patch).isEmpty() && std::abs (plainOf (*r, P_rsLevel) - 0.6f) < 1e-4f, "Reese settings in patch files");
        }
    };
    // ---- Break-Reactive mode
    auto reactTests = [&]
    {
        std::cout << "Break-Reactive mode" << std::endl;
        auto plainOf = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
        // a clean synth: Osc 1 alone (a sine unless asked), filter open, no effects, flat amp envelope
        auto clean = [&] (int wave = 3)
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain })
                setP (*p, i, 0.0f);
            setP (*p, P_osc1Gain, 0.8f); setP (*p, P_osc1Wave, (float) wave);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f); setP (*p, P_envAssignAmt0, 0.0f); setP (*p, P_ampA, 0.001f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.01f);
            return p;
        };
        auto reactOn = [&] (MegaSynthProcessor& p) { setP (p, P_rxOn, 1.0f); setP (p, P_rxFromNote, 1.0f); setP (p, P_rxNote, 24.0f); setP (p, P_rxFromBreaks, 0.0f); };
        auto on = [] (double t, int n) { return std::pair<double, juce::MidiMessage> { t, juce::MidiMessage::noteOn (1, n, (juce::uint8) 100) }; };
        auto off = [] (double t, int n) { return std::pair<double, juce::MidiMessage> { t, juce::MidiMessage::noteOff (1, n) }; };
        const int T = (int) (0.5 * sr) + 37;          // the trigger: on no block boundary
        const double tT = (T + 0.25) / sr;            // (as a time that lands on that sample)
        auto maxDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int to)
        {
            double d = 0; for (int c = 0; c < 2; ++c) for (int i = from; i < to; ++i) d = std::max (d, (double) std::abs (a.getSample (c, i) - b.getSample (c, i))); return d;
        };
        auto firstDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from)
        {
            for (int i = from; i < a.getNumSamples(); ++i) if (std::abs (a.getSample (0, i) - b.getSample (0, i)) > 1.0e-7f) return i;
            return -1;
        };
        // 1. the MIDI trigger: the phase is reset on that exact sample (after it, the same as a note started there)
        {
            juce::AudioBuffer<float> a, b, c;
            { auto p = clean(); reactOn (*p); tg::resetVoiceRandom(); render (*p, 0.7, { on (0.0, 57), on (tT, 24), off (tT + 0.01, 24) }, &a); }
            { auto p = clean(); reactOn (*p); tg::resetVoiceRandom(); render (*p, 0.7, { on (0.0131, 57), on (tT, 24), off (tT + 0.01, 24) }, &b); }
            { auto p = clean(); tg::resetVoiceRandom(); render (*p, 0.7, { on (tT, 57) }, &c); }
            juce::AudioBuffer<float> d;
            { auto p = clean(); reactOn (*p); tg::resetVoiceRandom(); render (*p, 0.7, { on (0.0, 57) }, &d); }
            // the fundamental's phase after the reset, against a note started on the hit (and one started 13 ms later)
            auto phaseOf = [&] (const juce::AudioBuffer<float>& x) { std::complex<double> acc {}; for (int i = T + 1000; i < T + 5800; ++i) acc += (double) x.getSample (0, i) * std::polar (1.0, -2.0 * kPi * 220.0 * i / sr); return std::arg (acc); };
            auto apart = [&] (const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
            { double dp = phaseOf (x) - phaseOf (y); while (dp > kPi) dp -= 2 * kPi; while (dp < -kPi) dp += 2 * kPi; return dp / (2 * kPi * 220.0) * sr; };
            const double ac = apart (a, c), ab = apart (a, b);
            std::cout << "  MIDI trigger: after the reset the phase is " << juce::String (ac, 3) << " samples from a note started on the hit ("
                      << juce::String (ab, 3) << " for a note held from a different time)" << std::endl;
            CHECK (std::abs (ac) < 1.0 && std::abs (ab) < 1.0, "phase reset on the trigger's own sample (within 1 sample)");
            const int fd = firstDiff (a, d, T - 2000);
            CHECK (fd >= T && fd <= T + 2, "nothing changes before the hit");
            // declicked: around the reset no step bigger than the sine's own (allowing the fade)
            float natural = 0; for (int i = T - 3000; i < T - 100; ++i) natural = std::max (natural, std::abs (a.getSample (0, i + 1) - a.getSample (0, i)));
            float around = 0; for (int i = T - 2; i < T + 200; ++i) around = std::max (around, std::abs (a.getSample (0, i + 1) - a.getSample (0, i)));
            std::cout << "  the reset's largest step " << around << " (the sine's own " << natural << ")" << std::endl;
            CHECK (around < natural * 2.5f, "the phase reset is declicked");
        }
        // 2. the slice keys and the Break Sequencer: the reaction on the slice's first sample; Offset; negative Offset
        auto breakPatch = [&] (bool rx, float offsetMs)
        {
            auto p = clean(); setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f);
            p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "demo.wav", MegaSynthProcessor::kSlotBreak);
            if (rx) { setP (*p, P_rxOn, 1.0f); setP (*p, P_rxListen, 0.0f); setP (*p, P_rxOffset, offsetMs); }
            return p;
        };
        for (float offsetMs : { 0.0f, 10.0f, -5.0f })
        {
            // A: the synth with the break muted (reacting); C: the same not reacting; B: the break alone
            juce::AudioBuffer<float> a, b, c;
            const int key = 36 + 4;   // (a slice key)
            { auto p = breakPatch (true, offsetMs); setP (*p, P_mxBreakMute, 1.0f); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 69), on (tT, key), off (tT + 0.05, key) }, &a); }
            { auto p = breakPatch (false, 0.0f); setP (*p, P_mxBreakMute, 1.0f); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 69), on (tT, key), off (tT + 0.05, key) }, &c); }
            { auto p = breakPatch (true, offsetMs); setP (*p, P_osc1Gain, 0.0f); tg::resetVoiceRandom(); render (*p, 0.8, { on (tT, key), off (tT + 0.05, key) }, &b); }
            const int H = firstDiff (a, c, 1000) - 1;   // (the reset's first sample is still all old phase; it shows from the next)
            int onset = -1; for (int i = 1000; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 1.0e-7f) { onset = i; break; }
            const int want = (int) std::lround (offsetMs * 0.001 * sr);
            std::cout << "  slice key, Offset " << offsetMs << " ms: the slice starts at " << onset << ", the reset at " << H << " (" << (H - onset) << " samples, want " << want << ")" << std::endl;
            CHECK (H > 0 && onset > 0 && std::abs ((H - onset) - want) <= 1, "slice keys: the reaction within 1 sample of the hit (+ Offset " + juce::String (offsetMs) + " ms)");
            if (offsetMs == 0.0f) CHECK (std::abs (onset - T) <= 1, "Offset 0: the break plays on time");
            if (offsetMs < 0.0f) CHECK (std::abs (onset - T - 240) <= 1 && std::abs (H - T) <= 1, "negative Offset: the breaks play 5 ms late, the synth on the hit");
        }
        {
            // the Break Sequencer, started by the host on a known sample (its first step on that sample)
            struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
            auto runSeq = [&] (MegaSynthProcessor& p, juce::AudioBuffer<float>& out, int startAt, bool noteOn)
            {
                Head head; p.setPlayHead (&head);
                const int total = (int) (1.2 * sr);
                out.setSize (2, total); juce::AudioBuffer<float> buf (2, 256);
                const double inc = 172.0 / 60.0 / sr; double ppq = 0.0;
                for (int pos = 0; pos < total; pos += 256)
                {
                    const bool playing = pos >= startAt;
                    head.info.setIsPlaying (playing); head.info.setBpm (172.0); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                    juce::MidiBuffer m; if (pos == 0 && noteOn) m.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
                    buf.clear(); p.processBlock (buf, m);
                    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, 256);
                    if (playing) ppq += 256 * inc;
                }
                p.setPlayHead (nullptr);
            };
            const int startAt = 256 * 90;
            juce::AudioBuffer<float> a, b, c;
            auto seqOn = [] (MegaSynthProcessor& p) { setP (p, P_bsOn, 1.0f); p.breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 16); }); };
            { auto p = breakPatch (true, 0.0f); seqOn (*p); setP (*p, P_mxBreakMute, 1.0f); tg::resetVoiceRandom(); runSeq (*p, a, startAt, true); }
            { auto p = breakPatch (false, 0.0f); seqOn (*p); setP (*p, P_mxBreakMute, 1.0f); tg::resetVoiceRandom(); runSeq (*p, c, startAt, true); }
            { auto p = breakPatch (false, 0.0f); seqOn (*p); setP (*p, P_osc1Gain, 0.0f); tg::resetVoiceRandom(); runSeq (*p, b, startAt, false); }
            const int H = firstDiff (a, c, 1000) - 1;
            int onset = -1; for (int i = 1000; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 1.0e-7f) { onset = i; break; }
            std::cout << "  Break Sequencer: first step at " << onset << ", the reset at " << H << std::endl;
            CHECK (H > 0 && std::abs (H - onset) <= 1 && std::abs (onset - startAt) <= 1, "Break Sequencer: the reaction within 1 sample of the hit");
        }
        // 3. which hits: all / kicks / snares / hats, a chosen break, from the Break Sequencer playing the demo break
        {
            auto count = [&] (int listen, int brk, float sens = 0.5f)
            {
                auto p = breakPatch (true, 0.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_rxListen, (float) listen); setP (*p, P_rxBreak, (float) brk); setP (*p, P_rxSens, sens);
                p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 32); });
                const int before = p->getEngine().rxHitsShown.load();
                render (*p, 60.0 / 170.0 * 8.0, chord (0.0, 2.8, { 57 }));
                return p->getEngine().rxHitsShown.load() - before;
            };
            const int all = count (0, 0), kicks = count (1, 0), snares = count (2, 0), hats = count (3, 0), b2 = count (0, 2), loose = count (1, 0, 1.0f), strict = count (1, 0, 0.0f);
            std::cout << "  two bars of the demo break: " << all << " hits, " << kicks << " kicks, " << snares << " snares, " << hats << " hats; Break 2 only: " << b2
                      << "; kicks at sensitivity 100% / 0%: " << loose << " / " << strict << std::endl;
            CHECK (all >= 8 && kicks > 0 && snares > 0 && kicks + snares + hats <= all && kicks < all && b2 == 0 && loose >= kicks && strict <= kicks,
                   "listening for kicks, snares, hats or all hits, from the chosen break");
        }
        // 4. the MIDI note and the sidechain
        {
            auto p = clean(); reactOn (*p);
            render (*p, 1.0, { on (0.0, 57), on (0.2, 24), off (0.25, 24), on (0.5, 24), off (0.55, 24), on (0.7, 24) });
            CHECK (p->getEngine().rxHitsShown.load() == 3, "the trigger note: one hit per press (" + juce::String (p->getEngine().rxHitsShown.load()) + ")");
            juce::AudioBuffer<float> c; auto q = clean(); reactOn (*q); render (*q, 0.3, { on (0.0, 57), on (0.1, 24) }, &c);
            CHECK (c.getMagnitude (0, 0, c.getNumSamples()) > 0.1f, "the trigger note isn't played, the synth is");
        }
        {
            auto makeSc = [&]
            {
                auto p = std::make_unique<MegaSynthProcessor>();
                juce::AudioProcessor::BusesLayout l; l.outputBuses.add (juce::AudioChannelSet::stereo()); l.inputBuses.add (juce::AudioChannelSet::stereo());
                p->setBusesLayout (l); p->setRateAndBufferSizeDetails (sr, 512); p->prepareToPlay (sr, 512);
                return p;
            };
            auto kicksSig = [&] (int i) { const int per = (int) (0.37 * sr), k = i % per; return k < 2400 ? 0.9f * std::exp (-k / 600.0f) * (float) std::sin (k * 0.012) : 0.0f; };
            auto hatsSig = [&] (int i) { const int per = (int) (0.25 * sr), k = i % per; static uint32_t r = 1; static float prev = 0.0f; r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                                         const float nz = (float) (r & 0xffff) / 32768.0f - 1.0f, hp = nz - prev; prev = nz;   // (bright noise: mostly above 5 kHz)
                                         return k < 1500 ? 0.4f * std::exp (-k / 200.0f) * hp : 0.0f; };
            auto run = [&] (std::function<float (int)> sig, int listen)
            {
                auto p = makeSc(); setP (*p, P_rxOn, 1.0f); setP (*p, P_rxFromSc, 1.0f); setP (*p, P_rxFromBreaks, 0.0f); setP (*p, P_rxListen, (float) listen);
                juce::AudioBuffer<float> buf (2, 512);
                for (int pos = 0; pos < (int) (2.0 * sr); pos += 512)
                {
                    for (int i = 0; i < 512; ++i) { const float x = sig (pos + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
                    juce::MidiBuffer midi; if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
                    p->processBlock (buf, midi);
                }
                return p->getEngine().rxHitsShown.load();
            };
            const int kAll = run (kicksSig, 0), kKick = run (kicksSig, 1), kHat = run (kicksSig, 3), hAll = run (hatsSig, 0), hHat = run (hatsSig, 3), hKick = run (hatsSig, 1);
            std::cout << "  sidechain, 2 s: kick-like hits every 0.37 s (6 due): " << kAll << " (as kicks " << kKick << ", as hats " << kHat << "); hat-like every 0.25 s: "
                      << hAll << " (as hats " << hHat << ", as kicks " << hKick << ")" << std::endl;
            CHECK (kAll == 6 && kKick == 6 && kHat == 0 && hAll == 9 && hHat == 9 && hKick == 0, "sidechain transients trigger it, roughly told apart");
        }
        // 5. shapes: the envelope (attack, then down to 1% over the Decay), and each action in the synth
        {
            tg::BreakReact r; r.prepare (sr, 512);
            tg::BreakReact::Settings st; st.on = true; st.attackMs = 10.0f; st.decayMs = 100.0f;
            r.noteHit (st, 1.0f);
            std::vector<float> env;
            for (int b = 0; b < 40; ++b) { r.begin (512, st, nullptr, nullptr); for (int i = 0; i < 512; ++i) env.push_back (r.envelope()[i]); }
            const int pk = (int) (std::max_element (env.begin(), env.end()) - env.begin());
            const float at50 = env[(size_t) (pk + (int) (0.05 * sr))], at100 = env[(size_t) (pk + (int) (0.1 * sr))];
            std::cout << "  envelope: peak at " << juce::String (pk / sr * 1000.0, 2) << " ms, " << juce::String (at50, 4) << " 50 ms later, " << juce::String (at100, 4) << " 100 ms later" << std::endl;
            CHECK (std::abs (pk - 480) <= 2 && std::abs (at50 - 0.1f) < 0.01f && std::abs (at100 - 0.01f) < 0.002f, "the envelope's attack and decay");
        }
        auto freqAt = [&] (const juce::AudioBuffer<float>& b, int from, int len)
        {
            // the frequency from upward zero crossings (interpolated) over that stretch
            double first = -1, last = -1; int n = 0;
            for (int i = from; i < from + len; ++i)
            {
                const float x0 = b.getSample (0, i), x1 = b.getSample (0, i + 1);
                if (x0 < 0.0f && x1 >= 0.0f) { const double t = i + x0 / (x0 - x1); if (first < 0) first = t; else ++n; last = t; }
            }
            return n > 0 ? n / ((last - first) / sr) : 0.0;
        };
        {
            juce::AudioBuffer<float> a;
            auto p = clean(); reactOn (*p); setP (*p, P_rxReset, 0.0f); setP (*p, P_rxPitch, 12.0f); setP (*p, P_rxDecay, 1000.0f);
            render (*p, 2.4, { on (0.0, 57), on (tT, 24) }, &a);
            const double f0 = freqAt (a, T - 4800, 4700), f1 = freqAt (a, T + 2, 480), f2 = freqAt (a, T + (int) (1.6 * sr), 4800);
            std::cout << "  pitch kick +12 st (1 s decay): " << juce::String (f0, 1) << " Hz before, " << juce::String (f1, 1) << " Hz in the first 10 ms, " << juce::String (f2, 1) << " Hz 1.6 s later" << std::endl;
            CHECK (std::abs (f1 / f0 - 2.0) < 0.1 && std::abs (f2 / f0 - 1.0) < 0.01, "pitch kick: up an octave on the hit, back over the decay");
        }
        {
            juce::AudioBuffer<float> a;
            auto p = clean(); reactOn (*p); setP (*p, P_rxReset, 0.0f); setP (*p, P_rxDuck, 1.0f); setP (*p, P_rxAttack, 0.0f); setP (*p, P_rxDecay, 100.0f);
            render (*p, 1.0, { on (0.0, 57), on (tT, 24) }, &a);
            const float before = a.getRMSLevel (0, T - 4800, 4800), dip = a.getRMSLevel (0, T + 48, 240), after = a.getRMSLevel (0, T + (int) (0.25 * sr), 4800);
            float natural = 0; for (int i = T - 3000; i < T - 100; ++i) natural = std::max (natural, std::abs (a.getSample (0, i + 1) - a.getSample (0, i)));
            float around = 0; for (int i = T - 2; i < T + 400; ++i) around = std::max (around, std::abs (a.getSample (0, i + 1) - a.getSample (0, i)));
            std::cout << "  level duck 100%: " << juce::String (20 * std::log10 (dip / before + 1e-9), 1) << " dB just after, " << juce::String (20 * std::log10 (after / before + 1e-9), 2)
                      << " dB 250 ms later; largest step " << around << " (the sine's own " << natural << ")" << std::endl;
            CHECK (dip < before * 0.2f && after > before * 0.98f && around < natural * 1.5f, "level duck: a dip on the hit, back over the decay, no click");
        }
        {
            auto thirdHarm = [&] (const juce::AudioBuffer<float>& b, int from, int len)
            {
                std::complex<double> h1 {}, h3 {};
                const double f = 220.0;
                for (int i = from; i < from + len; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (i - from) / len), x = b.getSample (0, i) * w;
                    h1 += x * std::polar (1.0, -2.0 * kPi * f * i / sr); h3 += x * std::polar (1.0, -2.0 * kPi * 3.0 * f * i / sr);
                }
                return 20.0 * std::log10 (std::abs (h3) / std::abs (h1) + 1e-12);
            };
            juce::AudioBuffer<float> a;
            auto p = clean(); reactOn (*p); setP (*p, P_rxReset, 0.0f); setP (*p, P_rxDrive, 1.0f); setP (*p, P_rxDecay, 300.0f); setP (*p, P_osc1Gain, 1.0f);
            render (*p, 1.0, { on (0.0, 57), on (tT, 24) }, &a);
            const double before = thirdHarm (a, T - 4800, 4400), during = thirdHarm (a, T + 10, 2400);
            std::cout << "  drive kick: 3rd harmonic " << juce::String (before, 1) << " dB before, " << juce::String (during, 1) << " dB on the hit" << std::endl;
            CHECK (during > before + 20.0, "drive kick: saturation on the hit");
        }
        {
            // the Reese's notch jumps on the hit and comes back
            auto run = [&] (float jump)
            {
                juce::AudioBuffer<float> a;
                auto p = clean(); reactOn (*p); setP (*p, P_rxReset, 0.0f); setP (*p, P_osc1Gain, 0.0f); setP (*p, P_rsLevel, 0.7f); setP (*p, P_rsNotch, 1.0f); setP (*p, P_rsNotchMove, 0.0f);
                setP (*p, P_rsRetrig, 1.0f); setP (*p, P_rsTwist, 0.0f); setP (*p, P_rxNotch, jump); setP (*p, P_rxDecay, 100.0f);
                tg::resetVoiceRandom(); render (*p, 1.4, { on (0.0, 45), on (tT, 24) }, &a);
                return a;
            };
            const auto a = run (1.0f), b = run (0.0f);
            const double during = maxDiff (a, b, T + 100, T + 2400), later = maxDiff (a, b, T + (int) (0.7 * sr), T + (int) (0.8 * sr));
            std::cout << "  Reese notch jump: changes the sound by " << during << " on the hit, " << later << " 700 ms later" << std::endl;
            CHECK (during > 0.05 && later < 0.002, "notch jump: the Reese's notch moves on the hit and comes back");
            // and the Reese's phase reset (Reset: every note's voices start at 0) lands exactly on the hit
            juce::AudioBuffer<float> x, y;
            { auto p = clean(); reactOn (*p); setP (*p, P_osc1Gain, 0.0f); setP (*p, P_rsLevel, 0.7f); setP (*p, P_rsRetrig, 1.0f); setP (*p, P_rsTwist, 0.0f); setP (*p, P_rsRotate, 0.0f);
              setP (*p, P_rsMonoBelow, 0.0f); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 45), on (tT, 24) }, &x); }
            { auto p = clean(); setP (*p, P_osc1Gain, 0.0f); setP (*p, P_rsLevel, 0.7f); setP (*p, P_rsRetrig, 1.0f); setP (*p, P_rsTwist, 0.0f); setP (*p, P_rsRotate, 0.0f);
              setP (*p, P_rsMonoBelow, 0.0f); tg::resetVoiceRandom(); render (*p, 0.8, { on (tT, 45) }, &y); }
            int best = 99; double bestD = 1e9;
            for (int sh = -4; sh <= 4; ++sh)
            {
                double d = 0; for (int i = T + 300; i < T + 5000; ++i) d = std::max (d, (double) std::abs (x.getSample (0, i) - y.getSample (0, i + sh)));
                if (d < bestD) { bestD = d; best = sh; }
            }
            std::cout << "  Reese phase reset vs a note started on the hit: " << bestD << " (alignment " << best << ")" << std::endl;
            CHECK (best == 0 && bestD < 0.06, "the Reese's phases reset on the hit");
        }
        // 6. the oscillator tick boxes: an unticked oscillator doesn't react
        {
            juce::AudioBuffer<float> a, c;
            { auto p = clean(); reactOn (*p); setP (*p, P_rxOsc1, 0.0f); setP (*p, P_rxPitch, 12.0f); setP (*p, P_rxDuck, 1.0f); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 57), on (tT, 24) }, &a); }
            { auto p = clean(); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 57) }, &c); }
            CHECK (maxDiff (a, c, 0, a.getNumSamples()) == 0.0, "Osc 1 unticked: untouched");
        }
        // 7. Humanise: each hit moved a random few ms; the breaks either way (they play late enough), a note only later
        {
            auto spread = [&] (bool fromBreak)
            {
                tg::BreakReact r; r.prepare (sr, 512);
                tg::BreakReact::Settings st; st.on = true; st.listen = 0; st.humanMs = 5.0f;
                int lo = 1 << 30, hi = -(1 << 30); std::set<int> seen;
                for (int k = 0; k < 40; ++k)
                {
                    const int64_t at = r.now();
                    if (fromBreak) r.breakHit (st, 0, 1.0f, 0.0f, 0.0f, 1.0f); else r.noteHit (st, 1.0f);
                    for (int b = 0; b < 2; ++b)
                    {
                        const int64_t blockStart = r.now();
                        r.begin (512, st, nullptr, nullptr);
                        for (int i = 0; i < 512; ++i) if (r.hits()[i]) { const int d = (int) (blockStart + i - at); lo = std::min (lo, d); hi = std::max (hi, d); seen.insert (d); }
                    }
                }
                std::cout << "  Humanise 5 ms, " << (fromBreak ? "breaks (played 5 ms late)" : "the trigger note") << ": hits " << lo << " to " << hi << " samples after the trigger (" << seen.size() << " different)" << std::endl;
                return fromBreak ? (lo >= 0 && hi <= 480 && lo < 120 && hi > 360 && seen.size() >= 20) : (lo >= 0 && hi <= 240 && hi > 180 && seen.size() >= 20);
            };
            CHECK (spread (true) && spread (false), "Humanise: hits moved randomly within the setting");
        }
        // 8. off: bit-exact; on with no hits: unchanged too
        {
            juce::AudioBuffer<float> ref, alt, alt2;
            auto prep = [&] (MegaSynthProcessor& p) { setP (p, P_reverbMix, 0.0f); };
            { auto p = make(); prep (*p); tg::resetVoiceRandom(); render (*p, 0.8, chord (0.0, 0.6, { 45, 52 }), &ref); }
            { auto p = make(); prep (*p);
              for (auto pr : std::initializer_list<std::pair<int, float>> { { P_rxListen, 0 }, { P_rxPitch, 12 }, { P_rxDrive, 1 }, { P_rxDuck, 1 }, { P_rxNotch, 1 }, { P_rxOffset, -10 }, { P_rxFromNote, 1 }, { P_rxNote, 52 } })
                  setP (*p, pr.first, pr.second);
              tg::resetVoiceRandom(); render (*p, 0.8, chord (0.0, 0.6, { 45, 52 }), &alt); }
            CHECK (maxDiff (ref, alt, 0, ref.getNumSamples()) == 0.0, "Break React off: bit-identical, whatever its settings");
            { auto p = make(); prep (*p); setP (*p, P_rxOn, 1.0f); setP (*p, P_rxPitch, 12.0f); setP (*p, P_rxDuck, 1.0f); tg::resetVoiceRandom(); render (*p, 0.8, chord (0.0, 0.6, { 45, 52 }), &alt2); }
            CHECK (maxDiff (ref, alt2, 0, ref.getNumSamples()) == 0.0, "Break React on, no hits: unchanged");
        }
        // 9. the sources: Break Hit moves a route; Break Hit Type says what the last hit was
        {
            juce::AudioBuffer<float> a, c;
            { auto p = clean(); reactOn (*p); setP (*p, P_rxReset, 0.0f); setP (*p, P_osc1Gain, 0.3f); p->addRoute (MS_BreakHit, P_osc1Gain, 0.7f); setP (*p, P_rxDecay, 200.0f);
              render (*p, 0.8, { on (0.0, 57), on (tT, 24) }, &a); }
            { auto p = clean(); setP (*p, P_osc1Gain, 0.3f); render (*p, 0.8, { on (0.0, 57) }, &c); }
            CHECK (a.getRMSLevel (0, T + 480, 2400) > c.getRMSLevel (0, T + 480, 2400) * 1.8f && std::abs (a.getRMSLevel (0, T - 4000, 3000) - c.getRMSLevel (0, T - 4000, 3000)) < 1e-4f,
                   "Break Hit as a Mod Matrix source");
            auto p = breakPatch (true, 0.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_rxListen, 2.0f);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 32); });
            render (*p, 1.0, chord (0.0, 1.0, { 57 }));
            CHECK (p->getEngine().rxHitsShown.load() > 0 && std::abs (p->getEngine().breakReact().hitType() - 0.5f) < 1e-6f, "Break Hit Type: 0.5 after a snare");
        }
        // 10. saved: projects, patches; older projects load with it off
        {
            auto p = make(); setP (*p, P_rxOn, 1.0f); setP (*p, P_rxListen, 2.0f); setP (*p, P_rxOffset, -4.0f); setP (*p, P_rxOsc2, 0.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (plainOf (*q, P_rxOn) > 0.5f && juce::roundToInt (plainOf (*q, P_rxListen)) == 2 && std::abs (plainOf (*q, P_rxOffset) + 4.0f) < 1e-3f && plainOf (*q, P_rxOsc2) < 0.5f,
                   "Break React settings saved with the project");
            auto x = juce::parseXML (getXmlFromBinaryForTest (mb));
            if (x != nullptr)
            {
                x->setAttribute ("stateVersion", 24);
                for (int i = P_rxOn; i <= P_rxReese; ++i) if (auto* el = x->getChildByAttribute ("id", kParamIds[i])) x->removeChildElement (el, true);
                juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*x, old);
                auto o = make(); setP (*o, P_rxOn, 1.0f); o->setStateInformation (old.getData(), (int) old.getSize());
                CHECK (plainOf (*o, P_rxOn) < 0.5f, "older projects load with Break React off");
            }
            auto r = make(); CHECK (r->importBrowserPatch (p->exportBrowserPatch()).isEmpty() && plainOf (*r, P_rxOn) > 0.5f && juce::roundToInt (plainOf (*r, P_rxListen)) == 2,
                                    "Break React settings in patch files");
            juce::StringArray notMod;
            for (int i = P_rxOn; i <= P_rxReese; ++i)
            {
                const auto& mt = tg::meta (i);
                if (mt.scale != tg::Scale::Choice && mt.scale != tg::Scale::Toggle && i != P_rxNote && ! mt.modulatable) notMod.add (kParamIds[i]);
            }
            CHECK (notMod.isEmpty(), "every continuous Break React control is a Mod Matrix destination" + (notMod.isEmpty() ? juce::String() : ": " + notMod.joinIntoString (", ")));
        }
        // 11. CPU: 8 notes, every action, a hit every 16th at 172 BPM (MIDI trigger)
        {
            auto cost = [&] (bool rx)
            {
                double best = 1e9;
                for (int run = 0; run < 3; ++run)
                {
                    auto q = make(); for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix }) setP (*q, i, 0.0f);
                    std::vector<std::pair<double, juce::MidiMessage>> ev;
                    for (int n : { 36, 40, 43, 47, 48, 52, 55, 59 }) ev.push_back (on (0.0, n));
                    if (rx)
                    {
                        reactOn (*q); setP (*q, P_rxPitch, 5.0f); setP (*q, P_rxDrive, 0.5f); setP (*q, P_rxDuck, 0.5f); setP (*q, P_rxNote, 100.0f);
                        for (double t = 0.0; t < 3.0; t += 60.0 / 172.0 / 4.0) { ev.push_back (on (t, 100)); ev.push_back (off (t + 0.01, 100)); }
                    }
                    double cpu = 0; render (*q, 3.0, ev, nullptr, &cpu);
                    best = std::min (best, cpu / 3.0 * 100.0);
                }
                return best;
            };
            const double a = cost (false), b = cost (true);
            std::cout << "  CPU, 8 notes: " << juce::String (a, 2) << "% off, " << juce::String (b, 2) << "% reacting to every 16th (+" << juce::String (b - a, 2) << "% of a core)" << std::endl;
        }
    };
    // ---- v0.1.68: Slice Keys On / Off / Auto, and Break React's rows (Kick / Snare / Hat), new actions, engines, hit choice
    auto rowTests = [&]
    {
        std::cout << "Slice Keys and Break React rows" << std::endl;
        auto plainOf = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
        auto clean = [&] (int wave = 3)
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain })
                setP (*p, i, 0.0f);
            setP (*p, P_osc1Gain, 0.8f); setP (*p, P_osc1Wave, (float) wave);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f); setP (*p, P_envAssignAmt0, 0.0f); setP (*p, P_ampA, 0.001f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.01f);
            return p;
        };
        auto withBreak = [&] (MegaSynthProcessor& p)
        {
            setP (p, P_bcOn, 1.0f); setP (p, P_bcSliceMode, 3.0f);
            p.loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "demo.wav", MegaSynthProcessor::kSlotBreak);
        };
        auto on = [] (double t, int n, int v = 100) { return std::pair<double, juce::MidiMessage> { t, juce::MidiMessage::noteOn (1, n, (juce::uint8) v) }; };
        auto off = [] (double t, int n) { return std::pair<double, juce::MidiMessage> { t, juce::MidiMessage::noteOff (1, n) }; };
        // blocks of 256 with a hook before each (to change settings mid-note), the output kept
        auto run = [&] (MegaSynthProcessor& p, double seconds, std::vector<std::pair<double, juce::MidiMessage>> ev, std::function<void (int)> before, juce::AudioBuffer<float>* out)
        {
            const int total = (int) (seconds * sr);
            if (out) out->setSize (2, total);
            juce::AudioBuffer<float> buf (2, 256);
            for (int pos = 0; pos < total; pos += 256)
            {
                const int n = std::min (256, total - pos);
                if (before) before (pos);
                buf.setSize (2, n, false, false, true); buf.clear();
                juce::MidiBuffer m;
                for (auto& e : ev) { const int at = (int) (e.first * sr); if (at >= pos && at < pos + n) m.addEvent (e.second, at - pos); }
                p.processBlock (buf, m);
                if (out) for (int c = 0; c < 2; ++c) out->copyFrom (c, pos, buf, c, 0, n);
            }
        };
        auto rmsOf = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            double e = 0; to = std::min (to, b.getNumSamples()); for (int i = from; i < to; ++i) e += (double) b.getSample (0, i) * b.getSample (0, i);
            return std::sqrt (e / std::max (1, to - from));
        };
        const int sliceKey = 36 + 4;
        // 1. Slice Keys: On (the slice, no synth note), Off (the synth), Auto (the synth while the sequencer plays)
        {
            auto synthLevel = [&] (int mode, bool seq, bool keyEntry)
            {
                auto p = clean(); withBreak (*p); setP (*p, P_mxBreakMute, 1.0f); setP (*p, P_bcSliceKeys, (float) mode);
                if (seq) { setP (*p, P_bsOn, 1.0f); p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 16); }); }
                if (keyEntry) setP (*p, P_bsKeyEntry, 1.0f);
                juce::AudioBuffer<float> b; run (*p, 0.6, { on (0.2, sliceKey), off (0.5, sliceKey) }, nullptr, &b);
                return rmsOf (b, (int) (0.25 * sr), (int) (0.45 * sr));
            };
            const double onL = synthLevel (0, false, false), offL = synthLevel (1, false, false), autoStopped = synthLevel (2, false, false),
                         autoRunning = synthLevel (2, true, false), onRunning = synthLevel (0, true, false), autoKeyEntry = synthLevel (2, true, true);
            std::cout << "  slice key " << sliceKey << ", synth level (break muted): On " << onL << ", Off " << offL << ", Auto stopped " << autoStopped
                      << ", Auto with the sequencer playing " << autoRunning << " (On " << onRunning << ", Auto + Key Entry " << autoKeyEntry << ")" << std::endl;
            CHECK (onL < 1.0e-6 && onRunning < 1.0e-6, "Slice Keys On: a slice key plays the break, not the synth (as before)");
            CHECK (offL > 0.1, "Slice Keys Off: the slice keys play the synth");
            CHECK (autoStopped < 1.0e-6 && autoRunning > 0.1, "Slice Keys Auto: the break while the sequencer is stopped, the synth while it plays");
            CHECK (autoKeyEntry < 1.0e-6, "Slice Keys Auto with Key Entry on: the slice keys stay on");
            // the Select Keys follow too
            {
                auto p = clean(); withBreak (*p); setP (*p, P_mxBreakMute, 1.0f); setP (*p, P_bcSliceKeys, 1.0f);
                juce::AudioBuffer<float> b; run (*p, 0.6, { on (0.2, 35), off (0.5, 35) }, nullptr, &b);
                CHECK (rmsOf (b, (int) (0.25 * sr), (int) (0.45 * sr)) > 0.1 && plainOf (*p, P_bcActive) == 1.0f, "Slice Keys Off: the Break Select keys play the synth too");
            }
            // Auto follows the sequencer starting and stopping (the same key, pressed while it plays and after)
            {
                auto p = clean(); withBreak (*p); setP (*p, P_mxBreakMute, 1.0f); setP (*p, P_bcSliceKeys, 2.0f);
                p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 16); });
                juce::AudioBuffer<float> b;
                run (*p, 1.2, { on (0.1, sliceKey), off (0.4, sliceKey), on (0.7, sliceKey), off (1.0, sliceKey) },
                     [&] (int pos) { setP (*p, P_bsOn, pos < (int) (0.5 * sr) ? 1.0f : 0.0f); }, &b);
                const double whilePlaying = rmsOf (b, (int) (0.15 * sr), (int) (0.35 * sr)), afterStop = rmsOf (b, (int) (0.75 * sr), (int) (0.95 * sr));
                std::cout << "  Auto: sequencer playing " << whilePlaying << ", then stopped " << afterStop << std::endl;
                CHECK (whilePlaying > 0.1 && afterStop < 1.0e-6, "Slice Keys Auto follows the sequencer starting and stopping");
            }
            // no stuck notes: switching with a key held (Off -> On, and On -> Off)
            {
                auto p = clean(); withBreak (*p); setP (*p, P_mxBreakMute, 1.0f); setP (*p, P_bcSliceKeys, 1.0f);
                run (*p, 1.0, { on (0.1, sliceKey), off (0.4, sliceKey) }, [&] (int pos) { if (pos == 256 * 50) setP (*p, P_bcSliceKeys, 0.0f); }, nullptr);
                CHECK (p->getEngine().activeVoiceCount() == 0, "no stuck note: a key played as a note under Off, let go under On");
                auto q = clean(); withBreak (*q); setP (*q, P_mxBreakMute, 1.0f); setP (*q, P_bcSliceKeys, 0.0f);
                juce::AudioBuffer<float> b;
                run (*q, 1.0, { on (0.1, sliceKey), off (0.4, sliceKey) }, [&] (int pos) { if (pos == 256 * 50) setP (*q, P_bcSliceKeys, 1.0f); }, &b);
                CHECK (q->getEngine().activeVoiceCount() == 0 && rmsOf (b, 0, b.getNumSamples()) < 1.0e-6, "no stuck note: a slice key let go under Off (it releases the slice, no synth note)");
            }
        }
        // 2. the rows: Kick / Snare / Hat, a type set by hand, None, and rows together
        {
            // slice keys 0-15 with types set by hand: 0-3 kick, 4-7 snare, 8-11 hat, 12-15 none
            auto rowsPatch = [&] (std::function<void (MegaSynthProcessor&)> setup)
            {
                auto p = clean(); withBreak (*p); setP (*p, P_rxOn, 1.0f); setP (*p, P_rxMainOn, 0.0f);
                p->breakStore.edit ([] (tg::BreakData& d) { for (int k = 0; k < 16; ++k) d.brk[0].slice[k].hitType = k < 4 ? 1 : (k < 8 ? 2 : (k < 12 ? 3 : 4)); });
                setup (*p);
                std::vector<std::pair<double, juce::MidiMessage>> ev;
                for (int k = 0; k < 16; ++k) { ev.push_back (on (0.05 + k * 0.05, 36 + k)); ev.push_back (off (0.08 + k * 0.05, 36 + k)); }
                run (*p, 1.0, ev, nullptr, nullptr);
                const auto& en = p->getEngine();
                return std::array<int, 4> { en.rxRowHitsShown[0].load(), en.rxRowHitsShown[1].load(), en.rxRowHitsShown[2].load(), en.rxRowHitsShown[3].load() };
            };
            const auto kick = rowsPatch ([&] (MegaSynthProcessor& p) { setP (p, P_rxKOn, 1.0f); });
            const auto snare = rowsPatch ([&] (MegaSynthProcessor& p) { setP (p, P_rxSOn, 1.0f); });
            const auto hat = rowsPatch ([&] (MegaSynthProcessor& p) { setP (p, P_rxHOn, 1.0f); });
            const auto all3 = rowsPatch ([&] (MegaSynthProcessor& p) { setP (p, P_rxKOn, 1.0f); setP (p, P_rxSOn, 1.0f); setP (p, P_rxHOn, 1.0f); setP (p, P_rxMainOn, 1.0f); setP (p, P_rxListen, 0.0f); });
            const auto mainKicks = rowsPatch ([&] (MegaSynthProcessor& p) { setP (p, P_rxMainOn, 1.0f); setP (p, P_rxListen, 1.0f); setP (p, P_rxKOn, 1.0f); });
            std::cout << "  rows (16 slice keys: 4 kick, 4 snare, 4 hat, 4 None by hand): Kick row " << kick[1] << ", Snare row " << snare[2] << ", Hat row " << hat[3]
                      << "; all rows + Main (all hits): " << all3[0] << " / " << all3[1] << " / " << all3[2] << " / " << all3[3] << std::endl;
            CHECK (kick[1] == 4 && kick[2] == 0 && kick[3] == 0 && kick[0] == 0, "the Kick row reacts to the kicks only");
            CHECK (snare[2] == 4 && snare[1] == 0 && hat[3] == 4 && hat[1] == 0, "the Snare and Hat rows react to their own hits only");
            CHECK (all3[0] == 12 && all3[1] == 4 && all3[2] == 4 && all3[3] == 4, "rows combine (Main on all hits fires with them; None never counts)");
            CHECK (mainKicks[0] == 4 && mainKicks[1] == 4, "the Main row (Listen: kicks) and the Kick row both fire on a kick");
            // a type set by hand wins over the analysis: the analysis' kicks, set to Snare, fire the Snare row
            {
                auto p = clean(); withBreak (*p); setP (*p, P_rxOn, 1.0f); setP (*p, P_rxMainOn, 0.0f); setP (*p, P_rxKOn, 1.0f); setP (*p, P_rxSOn, 1.0f);
                const auto d = p->breakStore.get();
                int kicks = 0; std::vector<int> kickSlices;
                for (int k = 0; k < d.brk[0].nSlices; ++k) if (d.brk[0].kick[k] >= d.brk[0].snare[k] && d.brk[0].kick[k] >= d.brk[0].hat[k] && d.brk[0].kick[k] >= 0.475f) { ++kicks; kickSlices.push_back (k); }
                p->breakStore.edit ([&] (tg::BreakData& dd) { for (int k : kickSlices) dd.brk[0].slice[k].hitType = 2; });
                std::vector<std::pair<double, juce::MidiMessage>> ev;
                for (int k : kickSlices) { ev.push_back (on (0.05 + k * 0.05, 36 + k)); ev.push_back (off (0.08 + k * 0.05, 36 + k)); }
                run (*p, 1.0, ev, nullptr, nullptr);
                CHECK (kicks > 0 && p->getEngine().rxRowHitsShown[1].load() == 0 && p->getEngine().rxRowHitsShown[2].load() == kicks,
                       "a slice's React Type wins over the analysis (its kicks, set to Snare, fire the Snare row: " + juce::String (kicks) + ")");
            }
        }
        // 3. Env Retrigger: on the hit (within 1 sample), from where the envelope was (no step), held by the pedal
        {
            auto retrigPatch = [&] (int mode)
            {
                auto p = clean(); setP (*p, P_ampA, 0.05f); setP (*p, P_ampD, 0.1f); setP (*p, P_ampS, 0.3f); setP (*p, P_ampR, 0.05f);
                setP (*p, P_rxOn, 1.0f); setP (*p, P_rxFromNote, 1.0f); setP (*p, P_rxNote, 24.0f); setP (*p, P_rxFromBreaks, 0.0f);
                setP (*p, P_rxReset, 0.0f); setP (*p, P_rxRetrig, (float) mode);
                return p;
            };
            const int T = (int) (0.5 * sr) + 37; const double tT = (T + 0.25) / sr;
            juce::AudioBuffer<float> a, b;
            { auto p = retrigPatch (1); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 57), on (tT, 24), off (tT + 0.01, 24) }, &a); }
            { auto p = retrigPatch (0); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 57), on (tT, 24), off (tT + 0.01, 24) }, &b); }
            int first = -1; for (int i = T - 2000; i < a.getNumSamples(); ++i) if (std::abs (a.getSample (0, i) - b.getSample (0, i)) > 1.0e-7f) { first = i; break; }
            const double before = rmsOf (a, T - 2400, T - 200), after = rmsOf (a, T + 2200, T + 2600);
            // (a step shows as a spike in the second difference; a sine's own is small and smooth)
            auto d2 = [&] (int i) { return std::abs (a.getSample (0, i + 1) - 2.0f * a.getSample (0, i) + a.getSample (0, i - 1)); };
            float natural = 0; for (int i = T + 2000; i < T + 2400; ++i) natural = std::max (natural, d2 (i));
            float around = 0; for (int i = T - 2; i < T + 200; ++i) around = std::max (around, d2 (i));
            std::cout << "  Env Retrigger (Amp): first change at " << first << " (hit " << T << "); level " << before << " before, " << after << " near the new peak; largest step "
                      << around << " (the note's own " << natural << ")" << std::endl;
            CHECK (std::abs (first - T) <= 1, "Env Retrigger: the amp envelope starts again within 1 sample of the hit");
            CHECK (after > before * 2.5, "Env Retrigger: from the sustain level back up through the attack");
            CHECK (around <= natural * 1.05f, "Env Retrigger: no step (it starts from where the envelope was; second difference around the hit no bigger than the note's own at its peak)");
            // the sustain pedal: the key let go, the pedal down - still retriggered
            juce::AudioBuffer<float> c;
            {
                auto p = retrigPatch (1); tg::resetVoiceRandom();
                render (*p, 0.8, { on (0.0, 57), { 0.1, juce::MidiMessage::controllerEvent (1, 64, 127) }, off (0.2, 57), on (tT, 24), off (tT + 0.01, 24) }, &c);
            }
            CHECK (rmsOf (c, T + 2200, T + 2600) > rmsOf (c, T - 2400, T - 200) * 2.5, "Env Retrigger works with the sustain pedal holding the note");
            // released notes aren't retriggered
            juce::AudioBuffer<float> d;
            { auto p = retrigPatch (1); setP (*p, P_ampR, 2.0f); tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 57), off (0.3, 57), on (tT, 24), off (tT + 0.01, 24) }, &d); }
            CHECK (rmsOf (d, T + 2200, T + 2600) < rmsOf (d, T - 2400, T - 200), "Env Retrigger leaves a released note alone");
        }
        // 4. Filter Kick: Filter 1 opens on the hit and is back after the decay
        {
            auto bright = [&] (const juce::AudioBuffer<float>& x, int from, int len)
            {
                // the share of the signal above ~2 kHz (a first difference against the signal)
                double hi = 0, all = 0; for (int i = from + 1; i < from + len; ++i) { const double d = x.getSample (0, i) - x.getSample (0, i - 1); hi += d * d; all += (double) x.getSample (0, i) * x.getSample (0, i); }
                return std::sqrt (hi / std::max (1e-12, all));
            };
            const int T = (int) (0.5 * sr);
            juce::AudioBuffer<float> a;
            {
                auto p = clean (0); setP (*p, P_filterCutoff, 400.0f); setP (*p, P_filterRes, 0.7f);
                setP (*p, P_rxOn, 1.0f); setP (*p, P_rxFromNote, 1.0f); setP (*p, P_rxNote, 24.0f); setP (*p, P_rxFromBreaks, 0.0f); setP (*p, P_rxReset, 0.0f);
                setP (*p, P_rxCut, 3.0f); setP (*p, P_rxDecay, 60.0f);
                tg::resetVoiceRandom(); render (*p, 1.0, { on (0.0, 45), on (T / sr, 24), off (T / sr + 0.01, 24) }, &a);
            }
            const double b0 = bright (a, T - 2400, 2000), b1 = bright (a, T + 48, 480), b2 = bright (a, T + (int) (0.3 * sr), 2000);
            std::cout << "  Filter Kick +3 oct on a 400 Hz low-pass: brightness " << b0 << " before, " << b1 << " just after the hit, " << b2 << " after the decay" << std::endl;
            CHECK (b1 > b0 * 1.6 && std::abs (b2 - b0) < b0 * 0.05, "Filter Kick: Filter 1 opens on the hit and is back after the decay");
        }
        // 5. Velocity Follow: a hit at half velocity reacts about half as much (the Level Duck's depth)
        {
            auto duckDepth = [&] (int vel)
            {
                auto p = clean(); setP (*p, P_rxOn, 1.0f); setP (*p, P_rxFromNote, 1.0f); setP (*p, P_rxNote, 24.0f); setP (*p, P_rxFromBreaks, 0.0f); setP (*p, P_rxReset, 0.0f);
                setP (*p, P_rxDuck, 1.0f); setP (*p, P_rxVel, 1.0f); setP (*p, P_rxAttack, 0.5f); setP (*p, P_rxDecay, 400.0f);
                juce::AudioBuffer<float> b; tg::resetVoiceRandom(); render (*p, 0.8, { on (0.0, 57), on (0.5, 24, vel), off (0.51, 24) }, &b);
                const double lv = rmsOf (b, (int) (0.40 * sr), (int) (0.48 * sr)), dip = rmsOf (b, (int) (0.503 * sr), (int) (0.508 * sr));
                return 1.0 - dip / lv;
            };
            const double full = duckDepth (127), half = duckDepth (64);
            std::cout << "  Velocity Follow 100%, Level Duck 100%: a full-velocity hit ducks " << juce::String (full * 100.0, 1) << "%, a half-velocity one " << juce::String (half * 100.0, 1) << "%" << std::endl;
            CHECK (full > 0.85 && std::abs (half / full - 0.5) < 0.08, "Velocity Follow: a half-velocity hit reacts about half as much");
        }
        // 6. which hits count: Beats, Every, Chance (16 bars of 16th trigger notes on the synth's own clock)
        {
            auto counts = [&] (int beats, int every, float chance, int seed, std::vector<int>* places = nullptr)
            {
                auto p = clean(); setP (*p, P_seqTempo, 120.0f);
                setP (*p, P_rxOn, 1.0f); setP (*p, P_rxFromNote, 1.0f); setP (*p, P_rxNote, 24.0f); setP (*p, P_rxFromBreaks, 0.0f); setP (*p, P_rxReset, 0.0f);
                setP (*p, P_rxBeats, (float) beats); setP (*p, P_rxEvery, (float) every); setP (*p, P_rxChance, chance); setP (*p, P_rxSeed, (float) seed);
                std::vector<std::pair<double, juce::MidiMessage>> ev;
                const double step = 0.125;   // a 16th at 120 BPM
                for (int k = 0; k < 16 * 16; ++k) { ev.push_back (on (k * step + 0.5 / sr, 24)); ev.push_back (off (k * step + 0.02, 24)); }
                run (*p, 16 * 2.0 + 0.1, ev, nullptr, nullptr);
                const auto& en = p->getEngine();
                if (places != nullptr) { const int c = en.rxLogCount.load(); for (int k = std::max (0, c - tg::Engine::kRxLog); k < c; ++k) places->push_back (en.rxLog[(size_t) (k % tg::Engine::kRxLog)].load()); }
                return en.rxRowHitsShown[0].load();
            };
            const int all = counts (0, 0, 1.0f, 1), beat = counts (1, 0, 1.0f, 1), back = counts (2, 0, 1.0f, 1), offb = counts (3, 0, 1.0f, 1), ev2 = counts (0, 1, 1.0f, 1), ev4 = counts (0, 3, 1.0f, 1);
            if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG", {}).isNotEmpty())
            {
                std::vector<int> pl; counts (2, 0, 1.0f, 1, &pl);
                for (int v : pl) std::cout << "    backbeat hit at " << (v & 4095) / 4096.0 * 4.0 << " beats" << std::endl;
            }
            std::vector<int> pa, pb, pc;
            const int ch1 = counts (0, 0, 0.5f, 7, &pa), ch2 = counts (0, 0, 0.5f, 7, &pb), ch3 = counts (0, 0, 0.5f, 8, &pc);
            std::cout << "  16 bars of 16ths: all " << all << ", on the beat " << beat << ", backbeat " << back << ", off-beats " << offb << ", every 2nd " << ev2 << ", every 4th " << ev4
                      << "; Chance 50%: " << ch1 << " (again " << ch2 << ", another seed " << ch3 << ")" << std::endl;
            CHECK (all == 256 && beat == 64 && back == 32 && offb == 192, "Beats: on the beat 4 a bar, the backbeat 2, the off-beats 12");
            CHECK (ev2 == 128 && ev4 == 64, "Every: every 2nd and every 4th hit, counted from each bar");
            CHECK (ch1 > 96 && ch1 < 160 && ch1 == ch2 && pa == pb && pa != pc, "Chance: about half, the same hits every playthrough with the same seed, others with another");
        }
        // 7. the other engines: back where a fresh note starts them, after the reset
        {
            const int T = (int) (0.5 * sr) + 37; const double tT = (T + 0.25) / sr;
            auto compare = [&] (const char* name, std::function<void (MegaSynthProcessor&)> setup)
            {
                auto mk = [&] (bool react)
                {
                    auto p = clean(); setP (*p, P_osc1Gain, 0.0f); setP (*p, P_supersawDrift, 0.0f); setup (*p);
                    setP (*p, P_rxOn, react && juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG2", {}).isEmpty() ? 1.0f : 0.0f); setP (*p, P_rxFromNote, 1.0f); setP (*p, P_rxNote, 24.0f); setP (*p, P_rxFromBreaks, 0.0f);
                    setP (*p, P_rxPitch, 0.0f); setP (*p, P_rxDuck, 0.0f);
                    for (int i : { P_rxOsc1, P_rxOsc2, P_rxOsc3, P_rxSub, P_rxReese }) setP (*p, i, 0.0f);
                    return p;
                };
                juce::AudioBuffer<float> a, b;
                { auto p = mk (true); tg::resetVoiceRandom(); render (*p, 0.75, { on (0.0, 57), on (tT, 24), off (tT + 0.01, 24) }, &a); }
                { auto p = mk (false); tg::resetVoiceRandom(); render (*p, 0.75, { on (tT, 57) }, &b); }
                if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG", {}).isNotEmpty())
                {
                    int bestLag = 0; double best = 1e9;
                    for (int lag = -400; lag <= 400; ++lag) { double e = 0; for (int i = T + 5000; i < T + 6000; ++i) { const double df = a.getSample (0, i) - b.getSample (0, i + lag); e += df * df; } if (e < best) { best = e; bestLag = lag; } }
                    std::cout << "    " << name << " best lag " << bestLag << " err " << best << std::endl;
                }
                if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG", {}).isNotEmpty())
                    for (int i = T + 5000; i < T + 5008; ++i) std::cout << "    " << name << " " << i << ": " << a.getSample (0, i) << " / " << b.getSample (0, i) << std::endl;
                double d = 0, lv = 0;
                // (from 100 ms on: a new note fades its sources in over the first few ms)
                for (int i = T + 4800; i < T + 9600; ++i) { d = std::max (d, (double) std::abs (a.getSample (0, i) - b.getSample (0, i))); lv = std::max (lv, (double) std::abs (b.getSample (0, i))); }
                float natural = 0; for (int i = T - 3000; i < T - 100; ++i) natural = std::max (natural, std::abs (a.getSample (0, i + 1) - a.getSample (0, i)));
                float around = 0; int at = -1; for (int i = T - 2; i < T + 100; ++i) { const float st = std::abs (a.getSample (0, i + 1) - a.getSample (0, i)); if (st > around) { around = st; at = i; } }
                if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG", {}).isNotEmpty())
                    for (int i = at - 3; i < at + 4; ++i) std::cout << "    max step near " << i << ": " << a.getSample (0, i) << std::endl;
                std::cout << "  " << name << ": after the reset, " << juce::String (juce::Decibels::gainToDecibels (d / std::max (1e-9, lv), -200.0), 1)
                          << " dB from a note started on the hit; the reset's largest step " << around << " (its own " << natural << ")" << std::endl;
                return std::pair<double, bool> { d / std::max (1e-9, lv), around <= natural * 1.6f };
            };
            const auto core = compare ("Core A", [&] (MegaSynthProcessor& p) { setP (p, P_cAEngine, 1.0f); setP (p, P_rxCoreA, 1.0f); });
            const auto saw = compare ("SuperSaw", [&] (MegaSynthProcessor& p) { setP (p, P_supersawGain, 0.6f); setP (p, P_rxSaw, 1.0f); });
            const auto cx = compare ("Complex", [&] (MegaSynthProcessor& p) { setP (p, P_complexGain, 0.6f); setP (p, P_rxComplex, 1.0f); });
            CHECK (core.first < 1.0e-3 && core.second, "Core A reacts: phases and envelopes back where a fresh note starts them, declicked");
            CHECK (saw.first < 1.0e-3 && saw.second, "SuperSaw reacts: phases back to the note's start, declicked");
            CHECK (cx.first < 3.0e-3 && cx.second, "Complex reacts: phases back to the note's start, declicked");
            // WT 1: back to its start (plus Jump), declicked
            {
                juce::AudioBuffer<float> smp (1, 44100);
                for (int i = 0; i < smp.getNumSamples(); ++i) smp.setSample (0, i, 0.6f * (float) std::sin (2.0 * kPi * 220.0 * i / 44100.0 + 0.00002 * i * i / 44100.0));
                const auto wav = tg::breaks::toWav16 (smp, 44100.0);
                auto wtSetup = [&] (MegaSynthProcessor& p) { p.loadSampleData (wav, "wt.wav", 0); setP (p, P_osc4Gain, 0.7f); setP (p, P_rxWt1, 1.0f); };
                if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG", {}).isNotEmpty())
                {
                    auto p = clean(); setP (*p, P_osc1Gain, 0.0f); wtSetup (*p);
                    juce::AudioBuffer<float> x; render (*p, 1.5, { on (0.0, 57) }, &x);
                    for (int k = 0; k < 15; ++k) std::cout << "    WT held rms " << k * 0.1 << " s: " << rmsOf (x, (int) (k * 0.1 * sr), (int) ((k + 1) * 0.1 * sr)) << std::endl;
                }
                // (a new note's wavetable starts at its first control update, up to one control block after the key: so the match is
                // looked for within that many samples)
                juce::AudioBuffer<float> a, b;
                auto mk = [&] (bool react)
                {
                    auto p = clean(); setP (*p, P_osc1Gain, 0.0f); wtSetup (*p);
                    setP (*p, P_rxOn, react ? 1.0f : 0.0f); setP (*p, P_rxFromNote, 1.0f); setP (*p, P_rxNote, 24.0f); setP (*p, P_rxFromBreaks, 0.0f);
                    for (int i : { P_rxOsc1, P_rxOsc2, P_rxOsc3, P_rxSub, P_rxReese }) setP (*p, i, 0.0f);
                    return p;
                };
                // two notes started at different times, both restarted by the same hit: the same from there on
                { auto p = mk (true); tg::resetVoiceRandom(); render (*p, 0.75, { on (0.0, 57), on (tT, 24), off (tT + 0.01, 24) }, &a); }
                { auto p = mk (true); tg::resetVoiceRandom(); render (*p, 0.75, { on (0.0131, 57), on (tT, 24), off (tT + 0.01, 24) }, &b); }
                int bestLag = 0; double best = 1e9, lv = 0;
                for (int lag = 0; lag <= 0; ++lag)
                {
                    double e = 0; for (int i = T + 4800; i < T + 9600; ++i) e = std::max (e, (double) std::abs (a.getSample (0, i) - b.getSample (0, i + lag)));
                    if (e < best) { best = e; bestLag = lag; }
                }
                for (int i = T + 4800; i < T + 9600; ++i) lv = std::max (lv, (double) std::abs (b.getSample (0, i)));
                float natural = 0; for (int i = T - 3000; i < T - 100; ++i) natural = std::max (natural, std::abs (a.getSample (0, i + 1) - a.getSample (0, i)));
                float around = 0; for (int i = T - 2; i < T + 100; ++i) around = std::max (around, std::abs (a.getSample (0, i + 1) - a.getSample (0, i)));
                std::cout << "  WT 1: after the reset, " << juce::String (juce::Decibels::gainToDecibels (best / lv, -200.0), 1) << " dB between two notes started at different times, both restarted by the hit (" << bestLag
                          << " samples apart); the reset's largest step " << around << " (its own " << natural << ")" << std::endl;
                CHECK (best / lv < 2.0e-3 && around <= natural * 1.6f, "WT 1 reacts: playback back at its start, declicked");
            }
            // Granular: a burst of grains with the hit envelope
            {
                auto grains = [&] (bool react)
                {
                    auto p = clean(); setP (*p, P_osc1Gain, 0.0f);
                    juce::AudioBuffer<float> smp (1, 44100); for (int i = 0; i < smp.getNumSamples(); ++i) smp.setSample (0, i, 0.4f * (float) std::sin (2.0 * kPi * 330.0 * i / 44100.0));
                    p->loadSampleData (tg::breaks::toWav16 (smp, 44100.0), "g.wav", MegaSynthProcessor::kSlotGranular);
                    setP (*p, P_gsOn, 1.0f); setP (*p, P_gsDensity, 8.0f);
                    setP (*p, P_rxOn, 1.0f); setP (*p, P_rxFromNote, 1.0f); setP (*p, P_rxNote, 24.0f); setP (*p, P_rxFromBreaks, 0.0f); setP (*p, P_rxDecay, 300.0f);
                    setP (*p, P_rxGran, react ? 1.0f : 0.0f); setP (*p, P_rxGranBurst, 1.0f);
                    int most = 0;
                    run (*p, 0.8, { on (0.0, 57), on (0.5, 24), off (0.51, 24) }, [&] (int pos)
                    {
                        if (pos > (int) (0.5 * sr)) most = std::max (most, p->getEngine().granGrainsShown.load());
                        if (react && juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG", {}).isNotEmpty() && pos > (int) (0.45 * sr) && pos < (int) (0.6 * sr))
                            std::cout << "    grains at " << pos << ": " << p->getEngine().granGrainsShown.load() << " env " << p->getEngine().rxEnvShown.load() << std::endl;
                    }, nullptr);
                    return most;
                };
                const int with = grains (true), without = grains (false);
                std::cout << "  Granular Burst 100%: up to " << with << " grains after the hit (" << without << " without)" << std::endl;
                CHECK (with >= without * 3, "Granular reacts: a burst of grains on the hit");
            }
        }
        // 8. detection: Classic and Band energy against the demo breaks' own hits
        {
            int okC = 0, okB = 0, total = 0;
            for (int variant = 0; variant < 3; ++variant)
            {
                const double bpm = 170.0, step = 60.0 / bpm / 4.0, swing = step * (variant == 2 ? 0.14 : 0.08);
                auto buf = tg::breaks::makeDemoBreak (44100.0, bpm, variant);
                std::vector<int> label (32, -1);
                auto mark = [&] (std::initializer_list<int> steps, int type) { for (int s : steps) if (label[(size_t) s] < 0 || type < label[(size_t) s]) label[(size_t) s] = type; };
                if (variant == 1)
                {
                    mark ({ 0, 6, 16, 22, 26 }, 0); mark ({ 4, 12, 20, 28, 15, 31 }, 1);
                    for (int s = 0; s < 32; ++s) mark ({ s }, 2);
                }
                else if (variant == 2)
                {
                    mark ({ 0, 10, 16, 26 }, 0); mark ({ 4, 12, 20, 28 }, 1); mark ({ 2, 3, 7, 9, 11, 14, 15, 18, 19, 23, 25, 27, 30, 31 }, 1);
                    for (int s = 0; s < 32; s += 2) mark ({ s }, 2);
                }
                else
                {
                    mark ({ 0, 10, 16, 18, 26, 27 }, 0); mark ({ 4, 12, 20, 28 }, 1); mark ({ 7, 9, 15, 23, 25, 30 }, 1);
                    for (int s = 0; s < 32; s += 2) mark ({ s }, 2);
                    for (int s = 1; s < 32; s += 4) mark ({ s }, 2);
                }
                tg::WaveSample w; w.numChannels = 2; w.length = buf.getNumSamples(); w.sampleRate = 44100.0;
                for (int c = 0; c < 2; ++c) w.ch[c].assign (buf.getReadPointer (c), buf.getReadPointer (c) + w.length);
                tg::BreakSlot slot;
                std::vector<int> steps;
                for (int s = 0; s < 32; ++s) if (label[(size_t) s] >= 0) { steps.push_back (s); slot.markers[slot.nMarkers++] = (float) ((s * step + ((s & 1) ? swing : 0.0)) * 44100.0 / w.length); }
                tg::breaks::analyse (&w, 5, 0.5f, 0, 0.0, slot);
                for (int k = 0; k < slot.nSlices && k < (int) steps.size(); ++k)
                {
                    const int want = label[(size_t) steps[(size_t) k]];
                    auto argmax = [] (float a, float b, float c) { return a >= b && a >= c ? 0 : (b >= c ? 1 : 2); };
                    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWDBG", {}).isNotEmpty())
                        std::cout << "    v" << variant << " step " << steps[(size_t) k] << " want " << want << "  B " << slot.kickB[k] << " " << slot.snareB[k] << " " << slot.hatB[k]
                                  << "  C " << slot.kick[k] << " " << slot.snare[k] << " " << slot.hat[k] << std::endl;
                    okC += argmax (slot.kick[k], slot.snare[k], slot.hat[k]) == want ? 1 : 0;
                    okB += argmax (slot.kickB[k], slot.snareB[k], slot.hatB[k]) == want ? 1 : 0;
                    ++total;
                }
            }
            std::cout << "  detection on the 3 demo breaks (" << total << " labelled hits): Classic " << juce::String (100.0 * okC / total, 1) << "%, Band energy "
                      << juce::String (100.0 * okB / total, 1) << "%" << std::endl;
            CHECK (total > 60 && okB >= okC, "Band energy detection is at least as accurate as Classic on the demo breaks");
        }
        // 10. CPU: 8 notes, every row and engine reacting to a hit on every 16th at 172 BPM, each quality
        {
            auto cost = [&] (int quality, bool react)
            {
                double best = 1e9;
                for (int rep = 0; rep < 2; ++rep)
                {
                    auto q = make(); setP (*q, P_quality, (float) quality); setP (*q, P_seqTempo, 172.0f);
                    for (int i : { P_reverbMix, P_delayMix, P_chorusMix }) setP (*q, i, 0.0f);
                    setP (*q, P_supersawGain, 0.3f); setP (*q, P_complexGain, 0.3f); setP (*q, P_cAEngine, 1.0f); setP (*q, P_rsLevel, 0.4f);
                    // (both: the trigger note is taken by Break React, so it never plays a voice; off: no row on)
                    setP (*q, P_rxOn, 1.0f); setP (*q, P_rxFromNote, 1.0f); setP (*q, P_rxNote, 24.0f); setP (*q, P_rxFromBreaks, 0.0f); setP (*q, P_rxListen, 0.0f);
                    if (! react) setP (*q, P_rxMainOn, 0.0f);
                    if (react)
                    {
                        for (int r = 0; r < kRxRows; ++r)
                        {
                            setP (*q, rxRowParam (r, RXF_On), 1.0f); setP (*q, rxRowParam (r, RXF_Pitch), 2.0f); setP (*q, rxRowParam (r, RXF_Duck), 0.3f);
                            setP (*q, rxRowParam (r, RXF_Cut), 1.0f); setP (*q, rxRowParam (r, RXF_Retrig), 3.0f); setP (*q, rxRowParam (r, RXF_Drive), 0.2f);
                        }
                        for (int i : { P_rxWt1, P_rxWt2, P_rxCoreA, P_rxCoreB, P_rxSaw, P_rxComplex, P_rxGran }) setP (*q, i, 1.0f);
                    }
                    std::vector<std::pair<double, juce::MidiMessage>> ev;
                    for (int nn : { 36, 43, 48, 55, 60, 64, 67, 72 }) ev.push_back (on (0.0, nn));
                    const double six = 60.0 / 172.0 / 4.0;
                    for (int k = 0; k < (int) (3.0 / six); ++k) { ev.push_back (on (k * six + 0.001, 24)); ev.push_back (off (k * six + 0.02, 24)); }
                    double cpu = 0; render (*q, 3.0, ev, nullptr, &cpu);
                    best = std::min (best, cpu / 3.0 * 100.0);
                }
                return best;
            };
            const char* qn[3] = { "Eco", "Normal", "High" };
            for (int qu = 0; qu < 3; ++qu)
            {
                const double a = cost (qu, false), b = cost (qu, true);
                std::cout << "  CPU, 8 notes (Osc 1, SuperSaw, Complex, Core A, Reese), every row and engine reacting to every 16th at 172 BPM, " << qn[qu] << ": "
                          << juce::String (a, 1) << "% off, " << juce::String (b, 1) << "% reacting (+" << juce::String (b - a, 1) << "% of a core)" << std::endl;
            }
        }
        // 9. presets: only the Break React controls, one undo step; saved and loaded
        {
            auto p = clean(); setP (*p, P_filterCutoff, 1234.0f); p->pushHistory ("start");
            p->applyReactPreset (2);
            CHECK (plainOf (*p, P_rxOn) == 1.0f && plainOf (*p, P_rxSOn) == 1.0f && plainOf (*p, P_rxSCut) == 2.0f && std::abs (plainOf (*p, P_filterCutoff) - 1234.0f) < 1.0f,
                   "a Break React preset sets its rows, nothing else");
            p->undo();
            CHECK (plainOf (*p, P_rxOn) == 0.0f && plainOf (*p, P_rxSOn) == 0.0f, "a Break React preset is one undo step");
            p->applyReactPreset (1);
            juce::MemoryBlock st; p->getStateInformation (st);
            auto q = make(); q->setStateInformation (st.getData(), (int) st.getSize());
            bool same = true; for (int i = P_bcSliceKeys; i <= P_rxGranBurst; ++i) same &= q->param (i)->getValue() == p->param (i)->getValue();
            CHECK (same, "the new Slice Keys / Break React settings are saved with the project");
        }
    };
    // ---- patch categories: category, tags, author, note; the browser's filters; many at once; Deleted; import / export
    auto categoryTests = [&]
    {
        std::cout << "Patch categories" << std::endl;
        const auto folder = MegaSynthProcessor::getPatchFolder();
        folder.deleteRecursively(); folder.createDirectory();
        CHECK (folder.isAChildOf (juce::File::getSpecialLocation (juce::File::tempDirectory)), "(the tests use their own patch folder)");
        // 1. the factory presets: every one in a real category, by Mega Synth, its description as the note, the old grouping as tags
        {
            juce::StringArray bad;
            for (int i = 0; i < (int) factoryPresets().size(); ++i)
            {
                const auto m = tg::factoryMeta (i);
                if (! tg::patchCategories().contains (m.category) || m.category == tg::PatchMeta::uncategorised() || m.author != "Mega Synth"
                    || m.note != factoryPresets()[(size_t) i].description || m.tags.isEmpty())
                    bad.add (factoryPresets()[(size_t) i].name);
            }
            CHECK (bad.isEmpty(), "every factory preset has a category, tags, author and note" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
            CHECK (tg::factoryMeta (1).category == "Reese" && tg::factoryMeta (1).tags.contains ("bass") && tg::factoryMeta (32).category == "Breaks & Drums"
                   && tg::factoryMeta (32).tags.contains ("jungle") && tg::factoryMeta (32).tags.contains ("dnb"), "factory categories (Reese Mutation: Reese, tagged bass; Break Chop 170: Breaks & Drums)");
        }
        // 2. patch files carry the information; the sound is the same with or without it; old files are Uncategorised
        tg::PatchMeta m1; m1.category = "Neuro"; m1.tags = { "dark", "rolling" }; m1.author = "Jamie"; m1.note = "Mod wheel opens it up";
        {
            auto p = make(); setP (*p, P_filterCutoff, 1234.0f); p->setPatchMeta (m1);
            const auto json = p->exportBrowserPatch();
            auto q = make(); CHECK (q->importBrowserPatch (json).isEmpty() && q->getPatchMeta() == m1, "a patch file keeps its category, tags, author and note");
            auto v = juce::JSON::parse (json); v.getDynamicObject()->removeProperty ("meta");
            auto o = make(); o->setPatchMeta (m1);
            CHECK (o->importBrowserPatch (juce::JSON::toString (v)).isEmpty() && o->getPatchMeta().categoryShown() == "Uncategorised" && o->getPatchMeta().tags.isEmpty(),
                   "a patch from before categories loads as Uncategorised");
            CHECK (std::abs (o->param (P_filterCutoff)->convertFrom0to1 (o->param (P_filterCutoff)->getValue()) - 1234.0f) < 0.5f, "... with its sound");
            // projects, undo, A/B
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto r = make(); r->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (r->getPatchMeta() == m1, "the information is saved with the project");
            auto u = make(); u->loadFactoryPreset (1); u->loadFactoryPreset (18);
            CHECK (u->getPatchMeta().category == "Reese" && u->getPatchMeta().note == factoryPresets()[18].description, "loading a factory preset brings its information");
            u->loadInitPatch();
            CHECK (u->getPatchMeta().categoryShown() == "Uncategorised", "Init is Uncategorised");
        }
        // 3. the catalogue: your patches under their categories, the tag and author filters, search in tags and notes,
        //    an old patch file Uncategorised, folders still listed
        auto savePatch = [&] (const juce::String& name, const tg::PatchMeta* m, const juce::String& sub = {})
        {
            auto p = make(); setP (*p, P_filterCutoff, 500.0f + 100.0f * (float) name.length());
            auto dir = sub.isEmpty() ? folder : folder.getChildFile (sub); dir.createDirectory();
            auto f = dir.getChildFile (name + ".megasynth");
            if (m != nullptr) p->setPatchMeta (*m);
            p->savePatchToFile (f);
            if (m == nullptr) { auto v = juce::JSON::parse (f.loadFileAsString()); v.getDynamicObject()->removeProperty ("meta"); f.replaceWithText (juce::JSON::toString (v)); }
            return f;
        };
        tg::PatchMeta m2; m2.category = "Reese"; m2.tags = { "dark", "174" }; m2.author = "Someone"; m2.note = "Wide and moving";
        tg::PatchMeta m3; m3.category = "Pads & Atmos"; m3.tags = { "lush" }; m3.author = "Jamie";
        const auto fA = savePatch ("Alpha", &m1), fB = savePatch ("Bravo", &m2), fC = savePatch ("Charlie", &m3, "Live set"), fOld = savePatch ("Oldie", nullptr);
        tgui::Favourites fav (folder.getChildFile ("favourites.txt"));
        {
            tgui::PatchCatalogue c; c.rescan();
            const auto cats = c.categories();
            bool allCats = true; for (auto& k : tg::patchCategories()) allCats = allCats && cats.contains (k);
            CHECK (allCats && cats.contains ("Factory") && cats.contains ("Your patches") && cats.contains ("Live set") && cats.contains ("Deleted"), "browser categories: the ten, Uncategorised, Factory, yours, folders, Deleted");
            auto names = [&] (const juce::Array<int>& rows) { juce::StringArray n; for (int i : rows) if (! c[i].factory) n.add (c[i].name); n.sort (false); return n.joinIntoString (","); };
            CHECK (names (c.filter ("Neuro", "", fav)) == "Alpha" && names (c.filter ("Reese", "", fav)) == "Bravo" && names (c.filter ("Uncategorised", "", fav)) == "Oldie",
                   "your patches under their categories; an old one Uncategorised");
            CHECK (names (c.filter ("All", "", fav, "dark")) == "Alpha,Bravo" && names (c.filter ("All", "", fav, {}, "Jamie")) == "Alpha,Charlie"
                   && names (c.filter ("All", "", fav, "dark", "Jamie")) == "Alpha", "tag and author filters");
            CHECK (names (c.filter ("All", "wheel", fav)) == "Alpha" && names (c.filter ("All", "174 wide", fav)) == "Bravo", "search covers notes and tags");
            CHECK (names (c.filter ("Live set", "", fav)) == "Charlie" && names (c.filter ("Your patches", "", fav)) == "Alpha,Bravo,Charlie,Oldie", "folders and Your patches still work");
            int factoryReese = 0; for (int i : c.filter ("Reese", "", fav)) factoryReese += c[i].factory ? 1 : 0;
            CHECK (factoryReese >= 2 && c.filter ("Factory", "", fav).size() == (int) factoryPresets().size() + 1, "factory presets under the same categories");
            CHECK (c.allTags().contains ("dark") && c.allTags().contains ("lush") && c.allAuthors().contains ("Jamie") && c.allAuthors().contains ("Mega Synth"), "the tag and author lists");
            // favourites (a factory preset and one of yours)
            int iB = -1, iF = -1; for (int i = 0; i < c.size(); ++i) { if (c[i].name == "Bravo") iB = i; if (c[i].factory && c[i].index == 3) iF = i; }
            fav.toggle (c[iB].key()); fav.toggle (c[iF].key());
            CHECK (c.filter ("Favourites", "", fav).size() == 2 && c.filter ("Favourites", "", fav, "dark").size() == 1, "favourites, also with the filters");
        }
        // 4. the information written into a file: the sound untouched
        {
            const auto before = juce::JSON::parse (fOld.loadFileAsString());
            tg::PatchMeta m; m.category = "Leads"; m.tags = { "old" };
            CHECK (tgui::patchlib::writeMeta (fOld, m) && tgui::patchlib::readMeta (fOld) == m, "writing a patch's information");
            auto after = juce::JSON::parse (fOld.loadFileAsString());
            after.getDynamicObject()->removeProperty ("meta");
            CHECK (juce::JSON::toString (before) == juce::JSON::toString (after), "... and nothing else in the file changes");
        }
        // 5. many at once in the browser: a category, then tags added and removed
        {
            auto p = make();
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            ed->setSize (1200, 890);
            tgui::PatchBrowser b (*p);
            b.setSize (1200, 640);
            b.open();
            b.showCategory ("Your patches");
            juce::Array<int> rows; for (int r = 0; r < b.shownCount(); ++r) if (b.shownEntry (r).name != "Bravo") rows.add (r);
            b.selectRowsForTests (rows);
            b.setCategoryForSelection ("Hoover & Rave");
            CHECK (tgui::patchlib::readMeta (fA).category == "Hoover & Rave" && tgui::patchlib::readMeta (fC).category == "Hoover & Rave"
                   && tgui::patchlib::readMeta (fOld).category == "Hoover & Rave" && tgui::patchlib::readMeta (fB).category == "Reese",
                   "recategorise several patches at once (and only those)");
            CHECK (tgui::patchlib::readMeta (fA).tags == m1.tags && tgui::patchlib::readMeta (fA).author == "Jamie", "... keeping their other information");
            b.addTagsToSelection ({ "rave", "93" }, false);
            CHECK (tgui::patchlib::readMeta (fC).tags.contains ("rave") && tgui::patchlib::readMeta (fOld).tags.contains ("93") && ! tgui::patchlib::readMeta (fB).tags.contains ("rave"), "tags added to several at once");
            b.addTagsToSelection ({ "93" }, true);
            CHECK (! tgui::patchlib::readMeta (fC).tags.contains ("93") && tgui::patchlib::readMeta (fC).tags.contains ("rave"), "tags removed from several at once");
            // the same for factory presets: kept as your edits, the preset itself unchanged
            b.showCategory ("Factory");
            juce::Array<int> fr; for (int r = 0; r < b.shownCount(); ++r) if (b.shownEntry (r).index == 5 || b.shownEntry (r).index == 6) fr.add (r);
            b.selectRowsForTests (fr);
            b.setCategoryForSelection ("FX & Risers");
            CHECK (MegaSynthProcessor::factoryMetaFor (5).category == "FX & Risers" && MegaSynthProcessor::factoryMetaFor (6).category == "FX & Risers"
                   && tg::factoryMeta (5).category == "Keys & Plucks", "factory presets recategorised (as your edits)");
            CHECK (tgui::patchlib::saveFactoryMeta (5, tg::factoryMeta (5)) && tgui::patchlib::saveFactoryMeta (6, tg::factoryMeta (6))
                   && MegaSynthProcessor::factoryMetaFor (5) == tg::factoryMeta (5) && MegaSynthProcessor::factoryMetaFor (6) == tg::factoryMeta (6), "... and put back");
        }
        // 6. Deleted: out of the lists, restorable with its information, or gone for good
        {
            const auto d = tgui::patchlib::deletePatch (fC);
            CHECK (d.existsAsFile() && ! fC.exists() && d.isAChildOf (MegaSynthProcessor::getDeletedFolder()) && ! MegaSynthProcessor::getPatchFiles().contains (d),
                   "a deleted patch goes to Deleted and out of the patch lists");
            tgui::PatchCatalogue c; c.rescan();
            int inDeleted = 0; for (int i : c.filter ("Deleted", "", fav)) inDeleted += c[i].name == "Charlie" ? 1 : 0;
            int elsewhere = 0; for (int i : c.filter ("All", "", fav)) elsewhere += c[i].name == "Charlie" ? 1 : 0;
            CHECK (inDeleted == 1 && elsewhere == 0 && tgui::patchlib::originalPath (d) == "Live set/Charlie.megasynth", "the Deleted list (and where it came from)");
            const auto back = tgui::patchlib::restorePatch (d);
            CHECK (back == fC && fC.existsAsFile() && tgui::patchlib::readMeta (fC).category == "Hoover & Rave", "restored where it was, with its information");
            const auto d2 = tgui::patchlib::deletePatch (fC);
            CHECK (tgui::patchlib::deleteForever (d2) && ! d2.exists() && tgui::patchlib::deletedFiles().isEmpty(), "deleted for good");
        }
        // 7. export and import keep the information (a factory preset too); a file that isn't a patch is refused
        {
            const auto out = folder.getParentDirectory().getChildFile ("export_test"); out.deleteRecursively(); out.createDirectory();
            const auto ef = out.getChildFile ("Amen Reese.megasynth");
            CHECK (tgui::patchlib::exportFactory (18, ef) && tgui::patchlib::readMeta (ef) == MegaSynthProcessor::factoryMetaFor (18), "a factory preset exported with its information");
            fB.copyFileTo (out.getChildFile ("Bravo.megasynth"));
            out.getChildFile ("notes.txt").replaceWithText ("not a patch");
            juce::StringArray errs;
            const auto in = tgui::patchlib::importFiles ({ ef, out.getChildFile ("Bravo.megasynth"), out.getChildFile ("notes.txt") }, errs);
            CHECK (in.size() == 2 && errs.size() == 1 && tgui::patchlib::readMeta (in[0]).category == "Reese" && tgui::patchlib::readMeta (in[1]) == m2
                   && in[1] != fB && in[1].getFileNameWithoutExtension() == "Bravo 2", "imported with their information (a new name when one's taken; not-patches refused)");
            auto p = make(); CHECK (p->loadPatchFromFile (in[0]).isEmpty() && p->getPatchMeta().category == "Reese", "an imported patch loads with its information");
            out.deleteRecursively();
        }
        // 8. Save: the dialog's category, tags, author and note go into the file; the author is remembered
        {
            auto p = make(); p->loadFactoryPreset (20);   // (Hoover 93)
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
            // (the dialog itself needs a screen; what it offers and what it saves are tested here)
            auto si = e->defaultSaveInfo();
            CHECK (si.meta.category == "Hoover & Rave" && si.meta.author == MegaSynthProcessor::getDefaultAuthor() && si.name == "Hoover 93",
                   "Save offers the patch's name and category (a factory preset's author isn't copied)");
            si.name = "My Hoover"; si.meta.tags = tg::PatchMeta::splitTags ("hoover, 93 , Rave"); si.meta.author = "Jamie"; si.meta.note = "Wheel for vibrato";
            e->saveWithInfo (si, false);
            const auto f = folder.getChildFile ("My Hoover.megasynth");
            const auto m = tgui::patchlib::readMeta (f);
            CHECK (f.existsAsFile() && m.category == "Hoover & Rave" && m.tags == juce::StringArray ({ "hoover", "93", "rave" }) && m.author == "Jamie" && m.note == "Wheel for vibrato",
                   "saved with its information");
            CHECK (MegaSynthProcessor::getDefaultAuthor() == "Jamie" && e->defaultSaveInfo().meta.author == "Jamie", "the author is remembered for next time");
        }
        // 10. the author list: remembered (most recent first, no case duplicates), kept on disk, forgettable
        {
            MegaSynthProcessor::rememberAuthor ("DJ Hype"); MegaSynthProcessor::rememberAuthor ("Jamie"); MegaSynthProcessor::rememberAuthor ("dj hype");
            auto list = MegaSynthProcessor::getAuthorList ({ "Someone", "Mega Synth", "JAMIE" });
            CHECK (list[0] == "dj hype" && list[1] == "Jamie" && list.size() == 4 && list.contains ("Someone") && list.contains ("Mega Synth"),
                   "author list: most recent first, no case duplicates, the patches' authors too (" + list.joinIntoString (", ") + ")");
            CHECK (juce::JSON::parse (MegaSynthProcessor::getAuthorListFile().loadFileAsString()).isObject(), "kept in authors.json (so it lasts)");
            const auto before = fB.loadFileAsString();
            MegaSynthProcessor::forgetAuthor ("Someone");
            CHECK (! MegaSynthProcessor::getAuthorList ({ "Someone" }).contains ("Someone") && fB.loadFileAsString() == before && tgui::patchlib::readMeta (fB).author == "Someone",
                   "forgetting a name takes it off the list and changes no patch");
            MegaSynthProcessor::rememberAuthor ("Someone");
            CHECK (MegaSynthProcessor::getAuthorList ({ "Someone" })[0] == "Someone", "typing it again brings it back");
        }
        // 11. the filters refresh at once; several patches' author and note set together
        {
            auto p = make();
            tgui::PatchBrowser b (*p); b.setSize (1200, 640); b.open();
            b.showCategory ("Your patches");
            int rowA = -1; for (int r = 0; r < b.shownCount(); ++r) if (b.shownEntry (r).name == "Alpha") rowA = r;
            b.selectRowsForTests ({ rowA });
            CHECK (b.authorChoices().contains ("Jamie") && b.authorChoices().contains ("Mega Synth"), "the author field lists the authors used before");
            // a new author on one patch: in the filter straight away
            b.setAuthorForSelection ("Goldie");
            CHECK (b.authorFilterItems().contains ("Goldie") && tgui::patchlib::readMeta (fA).author == "Goldie", "a new author shows in Any author without reopening");
            b.setAuthorFilterForTests ("Goldie");
            CHECK (b.authorFilterValue() == "Goldie" && b.shownCount() == 1, "filter by the new author");
            b.selectRowsForTests ({ 0 });
            b.setAuthorForSelection ("Jamie");   // (the only Goldie patch renamed away)
            CHECK (b.authorFilterValue().isEmpty() && ! b.authorFilterItems().contains ("Goldie") && b.shownCount() > 1, "a vanished author: the filter goes back to Any");
            // several at once, one of them a factory preset; the loaded patch picks it up
            b.showCategory ("All");
            juce::Array<int> rows; int iB = -1, iC = -1, iF = -1;
            for (int r = 0; r < b.shownCount(); ++r)
            {
                const auto& e = b.shownEntry (r);
                if (e.name == "Bravo") iB = r; if (e.name == "My Hoover") iC = r; if (e.factory && e.index == 7) iF = r;
            }
            rows.add (iB); rows.add (iC); rows.add (iF);
            const auto mB = tgui::patchlib::readMeta (fB);
            const auto fH = folder.getChildFile ("My Hoover.megasynth");
            const auto mH = tgui::patchlib::readMeta (fH);
            p->loadPatchFromFile (fB);
            b.selectRowsForTests (rows);
            CHECK (b.panelAuthor().isEmpty() && b.panelCategory().isEmpty() && b.panelNote().isEmpty(), "mixed values show as (mixed), the fields blank");
            b.setAuthorForSelection ("Ed Rush");
            const auto nB = tgui::patchlib::readMeta (fB), nH = tgui::patchlib::readMeta (fH), nF = MegaSynthProcessor::factoryMetaFor (7);
            CHECK (nB.author == "Ed Rush" && nH.author == "Ed Rush" && nF.author == "Ed Rush", "Set author changes all three (the factory one as your edit)");
            CHECK (nB.category == mB.category && nB.tags == mB.tags && nB.note == mB.note && nH.category == mH.category && nH.tags == mH.tags && nH.note == mH.note
                   && nF.category == tg::factoryMeta (7).category && nF.note == tg::factoryMeta (7).note, "... their category, tags and notes untouched");
            CHECK (p->getPatchMeta().author == "Ed Rush", "the loaded patch picks it up");
            CHECK (b.panelAuthor() == "Ed Rush", "now they share it, the panel shows it");
            b.setNoteForSelection ("");
            CHECK (tgui::patchlib::readMeta (fB).note == mB.note, "a blank note keeps each one's own");
            b.setNoteForSelection ("Roll it");
            CHECK (tgui::patchlib::readMeta (fB).note == "Roll it" && tgui::patchlib::readMeta (fH).note == "Roll it" && MegaSynthProcessor::factoryMetaFor (7).note == "Roll it"
                   && b.panelNote() == "Roll it", "Set note on all of them");
            tgui::patchlib::saveFactoryMeta (7, tg::factoryMeta (7));
            // the header's Save with an open browser: its filter lists the new author at once
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
            e->showBrowserForTests (true);
            auto si = e->defaultSaveInfo(); si.name = "Bukem Bass"; si.meta.author = "LTJ";
            e->saveWithInfo (si, false);
            CHECK (e->browserForTests() != nullptr && e->browserForTests()->authorFilterItems().contains ("LTJ"), "saving from the header updates the open browser's Any author");
            e->showBrowserForTests (false);
        }
        // 9. the browser opens with all this (and draws)
        {
            auto p = make();
            tgui::PatchBrowser b (*p); b.setSize (1200, 640); b.open();
            b.showCategory ("All");
            b.selectRowsForTests ({ 3 });
            auto img = b.createComponentSnapshot (b.getLocalBounds(), true, 1.0f);
            auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
            auto f = dir.getChildFile ("page_PatchBrowser.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            b.showCategory ("Your patches");
            juce::Array<int> two { 0, 1 }; b.selectRowsForTests (two);
            auto img2 = b.createComponentSnapshot (b.getLocalBounds(), true, 1.0f);
            auto f2 = dir.getChildFile ("page_PatchBrowser_multi.png"); f2.deleteFile();
            juce::FileOutputStream os2 (f2); juce::PNGImageFormat().writeImageToStream (img2, os2);
            CHECK (b.shownCount() > 0, "the browser lists them");
        }
        folder.deleteRecursively(); folder.createDirectory();
        MegaSynthProcessor::setDefaultAuthor ({});
    };
    // ---- the jungle / drum & bass bank: each preset's audition phrase (by category), at 172 BPM with the host playing
    struct Audition { juce::AudioBuffer<float> out; float peak = 0; bool finite = true; double sideDb = -200; };
    auto auditionOf = [&] (int index)
    {
        struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        const auto cat = tg::factoryMeta (index).category;
        const double bpm = 172.0, spb = 60.0 / bpm;
        // notes: { beat, length in beats, note }
        std::vector<std::array<double, 3>> notes;
        if (cat == "Reese" || cat == "Sub & Bass" || cat == "Neuro")
            notes = { { 0, 1.5, 45 }, { 2, 0.5, 45 }, { 2.75, 1, 48 }, { 4, 2, 41 }, { 6.5, 1.25, 43 } };
        else if (cat == "Hoover & Rave") notes = { { 0, 1.5, 57 }, { 2, 1, 60 }, { 3.5, 1.5, 55 }, { 5.5, 2, 57 } };
        else if (cat == "Stabs & Chords") for (int k = 0; k < 8; ++k) notes.push_back ({ k + 0.5, 0.25, (double) (k == 2 ? 60 : (k == 3 || k == 7 ? 55 : (k == 6 ? 53 : 57))) });
        else if (cat == "Pads & Atmos") notes = { { 0, 4, 57 }, { 0, 4, 60 }, { 0, 4, 64 }, { 0, 4, 67 }, { 4, 4, 53 }, { 4, 4, 57 }, { 4, 4, 60 }, { 4, 4, 64 } };
        else if (cat == "Leads") notes = { { 0, 0.75, 69 }, { 1, 0.5, 72 }, { 1.75, 0.75, 74 }, { 2.75, 0.5, 72 }, { 3.5, 1.5, 69 }, { 5.5, 0.5, 67 }, { 6, 2, 69 } };
        else if (cat == "FX & Risers") notes = { { 0, 6, 57 } };
        else if (cat == "Keys & Plucks") { const int arp[] = { 57, 60, 64, 67, 69, 67, 64, 60 }; for (int k = 0; k < 16; ++k) notes.push_back ({ k * 0.5, 0.4, (double) arp[k % 8] }); }
        auto p = std::make_unique<MegaSynthProcessor>();
        p->setPlayConfigDetails (0, 2, sr, 256); p->prepareToPlay (sr, 256);
        p->loadFactoryPreset (index);
        if (cat == "Breaks & Drums" && p->param (P_bsOn)->getValue() < 0.5f)
            for (int k = 0; k < 16; ++k) notes.push_back ({ k * 0.5, 0.4, (double) (36 + (k * 5) % 16) });   // (slices on keys: play them)
        Head head; p->setPlayHead (&head);
        juce::AudioBuffer<float> buf (2, 256);
        for (int b = 0; b < 400; ++b)   // (the reverbs load their impulses in the background first)
        {
            head.info.setIsPlaying (false); head.info.setBpm (bpm); head.info.setPpqPosition (0.0);
            buf.clear(); juce::MidiBuffer none; p->processBlock (buf, none); juce::Thread::sleep (1);
        }
        Audition a;
        const int total = (int) ((8.0 * spb + 1.5) * sr);
        a.out.setSize (2, total);
        double ppq = 0.0; const double inc = bpm / 60.0 / sr;
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            buf.setSize (2, n, false, false, true);
            head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            juce::MidiBuffer m;
            for (auto& nt : notes)
            {
                const int on = (int) std::lround (nt[0] * spb * sr), offAt = (int) std::lround ((nt[0] + nt[1]) * spb * sr);
                if (on >= pos && on < pos + n) m.addEvent (juce::MidiMessage::noteOn (1, (int) nt[2], (juce::uint8) 100), on - pos);
                if (offAt >= pos && offAt < pos + n) m.addEvent (juce::MidiMessage::noteOff (1, (int) nt[2]), offAt - pos);
            }
            buf.clear(); p->processBlock (buf, m); ppq += n * inc;
            for (int c = 0; c < 2; ++c) a.out.copyFrom (c, pos, buf, c, 0, n);
        }
        p->setPlayHead (nullptr);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) { const float v = a.out.getSample (c, i); if (! std::isfinite (v)) a.finite = false; else a.peak = std::max (a.peak, std::abs (v)); }
        // the low end's width: side against mid, 30-100 Hz
        const int N = 1 << 17;
        std::vector<float> fm ((size_t) N * 2, 0.0f), fs ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N && i < total; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * i / (float) std::min (N, total));
            fm[(size_t) i] = 0.5f * (a.out.getSample (0, i) + a.out.getSample (1, i)) * w; fs[(size_t) i] = 0.5f * (a.out.getSample (0, i) - a.out.getSample (1, i)) * w;
        }
        juce::dsp::FFT f (17); f.performFrequencyOnlyForwardTransform (fm.data()); f.performFrequencyOnlyForwardTransform (fs.data());
        double em = 0, es = 0;
        for (int k = 1; k < N / 2; ++k) { const double hz = k * sr / N; if (hz >= 30 && hz < 100) { em += (double) fm[(size_t) k] * fm[(size_t) k]; es += (double) fs[(size_t) k] * fs[(size_t) k]; } }
        a.sideDb = 10.0 * std::log10 ((es + 1e-20) / (em + 1e-20));
        return a;
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_PRESETCAL", {}).isNotEmpty())
    {
        // the bank's Output trims: each audition phrase's peak brought to -12 dBFS (paste into FactoryDnbTrims.h)
        juce::String table;
        for (int i = 0; i < (int) tg::dnbBank().size(); ++i)
        {
            const int idx = tg::kFirstDnb + i;
            const auto a = auditionOf (idx);
            const float cur = tg::dnbTrim (i);
            const float peakDb = 20.0f * std::log10 (std::max (1.0e-9f, a.peak));
            const float trim = juce::jlimit (-24.0f, 12.0f, std::round ((cur + (-12.0f - peakDb)) * 10.0f) / 10.0f);
            std::cout << "CAL " << i << " " << tg::dnbBank()[(size_t) i].name << " peak " << juce::String (peakDb, 2) << " dB (trim " << cur << ") -> " << trim
                      << (cur + (-12.0f - peakDb) > 12.0f ? "  TOO QUIET" : "") << std::endl;
            table << juce::String (trim, 1) << "f" << (i % 10 == 9 ? ",\n" : ", ");
        }
        std::cout << "TABLE\n" << table << std::endl;
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_AUDITION", {}).isNotEmpty())
    {
        // one reel per category: each preset's phrase, 0.4 s apart (renders/audition_<category>.wav)
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        for (auto& c : tg::patchCategories())
        {
            std::vector<juce::AudioBuffer<float>> parts;
            for (int i = 0; i < (int) tg::dnbBank().size(); ++i)
                if (juce::String (tg::dnbBank()[(size_t) i].category) == c) parts.push_back (auditionOf (tg::kFirstDnb + i).out);
            if (parts.empty()) continue;
            int len = 0; for (auto& b : parts) len += b.getNumSamples() + (int) (0.4 * sr);
            juce::AudioBuffer<float> reel (2, len); reel.clear();
            int at = 0; for (auto& b : parts) { for (int ch = 0; ch < 2; ++ch) reel.copyFrom (ch, at, b, ch, 0, b.getNumSamples()); at += b.getNumSamples() + (int) (0.4 * sr); }
            auto f = dir.getChildFile ("audition_" + c.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") + ".wav"); f.deleteFile();
            std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 16, {}, 0));
            if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (reel, 0, len); }
            std::cout << "REEL " << f.getFileName() << " " << parts.size() << " presets" << std::endl;
        }
        return 0;
    }
    // ---- the Core oscillator slots: the Kernel FM engine (accuracy, envelopes, stability, aliasing, silence when off, CPU)
    auto coreTests = [&]
    {
        std::cout << "Core oscillator (Kernel FM)" << std::endl;
        auto plainOf = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
        auto sine = [] { CoreSettings s; s.uni = 1; s.op[0].level = 1.0f; s.op[0].out = 1.0f; return s; };
        // render a kernel on its own (mono: the left channel)
        auto run = [&] (CoreSettings s, double hz, int samples, std::vector<float>& out, int note = 60, float vel = 1.0f, int releaseAt = -1, std::vector<float>* env1 = nullptr)
        {
            CoreKernel k; k.prepare (sr); k.start (s, 0, 1, note, vel);
            out.assign ((size_t) samples, 0.0f);
            if (env1) env1->assign ((size_t) samples, 0.0f);
            for (int pos = 0; pos < samples; pos += 16)
            {
                const int n = std::min (16, samples - pos);
                if (releaseAt >= 0 && pos == releaseAt) k.release();
                k.configure (s, hz, n);
                for (int i = 0; i < n; ++i)
                {
                    float a, b; k.tick (a, b); out[(size_t) (pos + i)] = a;
                    if (env1) (*env1)[(size_t) (pos + i)] = k.envelope (0);
                }
            }
        };
        // the magnitude of one frequency (Hann window over a whole number of its cycles: the window is 1 s)
        auto magAt = [&] (const std::vector<float>& x, int from, int len, double f)
        {
            std::complex<double> acc {};
            for (int i = 0; i < len; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / len);
                const double ph = 2.0 * kPi * f * i / sr;
                acc += (double) x[(size_t) (from + i)] * w * std::complex<double> (std::cos (ph), -std::sin (ph));
            }
            return std::abs (acc) * 4.0 / len;   // (a sine of amplitude A reads A)
        };
        // the frequency of a near-sine: zero crossings (rising, interpolated) over the span
        auto freqOf = [&] (const std::vector<float>& x, int from)
        {
            double first = -1, last = -1; int count = 0;
            for (int i = from + 1; i < (int) x.size(); ++i)
                if (x[(size_t) i - 1] <= 0.0f && x[(size_t) i] > 0.0f)
                {
                    const double t = (i - 1) + x[(size_t) i - 1] / (double) (x[(size_t) i - 1] - x[(size_t) i]);
                    if (first < 0) first = t; else ++count;
                    last = t;
                }
            return count > 0 ? count * sr / (last - first) : 0.0;
        };
        // 1. the Bessel check: a 2-operator pair, carrier 1000 Hz, modulator 250 Hz, index 1 (and 2.4): the sidebands
        //    at 1000 +/- n x 250 are J_n(index) relative to J_0, within 0.5 dB (n = 1, 2, 3, both sides)
        for (int q = 0; q < 3; ++q)
            for (double index : { 1.0, 2.0 })
            {
                CoreSettings s = sine(); s.quality = q;
                s.op[0].ratio = 4.0; s.op[1].ratio = 1.0; s.op[1].level = 1.0f; s.op[1].out = 0.0f; s.op[1].m[0] = (float) index;
                std::vector<float> x; run (s, 250.0, (int) (sr * 1.3), x);
                const int from = (int) (sr * 0.2), len = (int) sr;
                const double c0 = magAt (x, from, len, 1000.0);
                double worst = 0;
                // (upper sidebands 1-4 and lower 1-2: the lower third has the reflected fifth on it)
                for (int n = 1; n <= 4; ++n)
                    for (int sgn : { -1, 1 })
                    {
                        if (sgn < 0 && n > 2) continue;
                        const double expect = std::abs (std::cyl_bessel_j ((double) n, index) / std::cyl_bessel_j (0.0, index));
                        const double got = magAt (x, from, len, 1000.0 + sgn * n * 250.0) / c0;
                        worst = std::max (worst, std::abs (20.0 * std::log10 (got / expect)));
                    }
                std::cout << "  Bessel (" << kQualityLabels[q] << ", index " << index << "): carrier " << juce::String (c0, 4)
                          << " (J0 " << juce::String (std::cyl_bessel_j (0.0, index), 4) << "), worst sideband error " << juce::String (worst, 3) << " dB" << std::endl;
                CHECK (worst < 0.5, "FM sidebands match the Bessel functions within 0.5 dB");
                CHECK (std::abs (20.0 * std::log10 (c0 / std::abs (std::cyl_bessel_j (0.0, index)))) < 0.5, "FM carrier amplitude is J0(index)");
            }
        // 1:1 against an ideal sin (wt + I sin wt), harmonics 1-6 (the folded sidebands included)
        {
            CoreSettings s = sine(); s.op[1].level = 1.0f; s.op[1].m[0] = 1.5f;
            std::vector<float> x; run (s, 500.0, (int) (sr * 1.3), x);
            std::vector<float> ideal (x.size());
            for (size_t i = 0; i < x.size(); ++i) { const double w = 2.0 * kPi * 500.0 * (double) i / sr; ideal[i] = (float) std::sin (w + 1.5 * std::sin (w)); }
            double worst = 0;
            for (int h = 1; h <= 6; ++h)
            {
                const double a = magAt (x, (int) (sr * 0.2), (int) sr, 500.0 * h), b = magAt (ideal, (int) (sr * 0.2), (int) sr, 500.0 * h);
                if (b > 0.01) worst = std::max (worst, std::abs (20.0 * std::log10 (a / b)));
            }
            std::cout << "  1:1 index 1.5 vs the ideal formula: worst harmonic error " << juce::String (worst, 3) << " dB" << std::endl;
            CHECK (worst < 0.5, "1:1 FM matches sin (wt + I sin wt)");
        }
        // 2. tuning: ratio (with fine) and fixed frequencies within 0.1 cent
        {
            double worst = 0;
            struct T { double note; double ratio; double fine; bool fixed; double hz; };
            for (auto t : { T { 110.0, 1.0, 0.0, false, 0 }, T { 220.0, 3.5, 7.0, false, 0 }, T { 55.0, 0.5, -12.5, false, 0 }, T { 261.63, 12.0, 0.0, false, 0 },
                            T { 100.0, 1.0, 0.0, true, 1234.5 }, T { 100.0, 1.0, -30.0, true, 77.7 } })
            {
                for (int q = 0; q < 3; ++q)
                {
                    CoreSettings s = sine(); s.quality = q; s.op[0].ratio = t.ratio; s.op[0].fine = t.fine; s.op[0].fixed = t.fixed; s.op[0].hz = t.hz;
                    std::vector<float> x; run (s, t.note, (int) (sr * 2.0), x);
                    const double expect = (t.fixed ? t.hz : t.note * t.ratio) * std::pow (2.0, t.fine / 1200.0);
                    worst = std::max (worst, std::abs (1200.0 * std::log2 (freqOf (x, (int) (sr * 0.05)) / expect)));
                }
            }
            std::cout << "  tuning: worst error " << juce::String (worst, 5) << " cents (ratios, fine, fixed Hz, all qualities)" << std::endl;
            CHECK (worst < 0.1, "operator tuning within 0.1 cent");
        }
        // 3. envelope timing within 1 ms: delay + attack peak, hold, decay to -60 dB of its span, release to -60 dB
        {
            CoreSettings s = sine();
            auto& o = s.op[0]; o.dly = 0.05f; o.att = 0.1f; o.hold = 0.05f; o.dec = 0.2f; o.sus = 0.5f; o.rel = 0.3f;
            for (float spd : { 1.0f, 2.0f })
            {
                s.envSpd = spd;
                const int relAt = (int) (sr * 1.0 / 16) * 16;
                std::vector<float> x, e; run (s, 220.0, (int) (sr * 1.6), x, 60, 1.0f, relAt, &e);
                auto firstAt = [&] (int from, std::function<bool (float)> f) { for (int i = from; i < (int) e.size(); ++i) if (f (e[(size_t) i])) return i; return -1; };
                const double t0 = firstAt (0, [] (float v) { return v > 0.0f; }) / sr;
                const double tPeak = firstAt (0, [] (float v) { return v >= 1.0f; }) / sr;
                const double tDec = firstAt ((int) (tPeak * sr) + 1, [] (float v) { return v < 0.999f; }) / sr;   // hold ends
                const double tSus = firstAt ((int) (tDec * sr), [&] (float v) { return v <= o.sus + (1.0f - o.sus) * 0.001f; }) / sr;
                const double tRel = firstAt (relAt, [&] (float v) { return v <= o.sus * 0.001f; }) / sr - relAt / sr;
                const double errs[] = { t0 - o.dly / spd, tPeak - (o.dly + o.att) / spd, tDec - (o.dly + o.att + o.hold) / spd,
                                        tSus - (o.dly + o.att + o.hold + o.dec) / spd, tRel - o.rel / spd };
                double worst = 0; for (double d : errs) worst = std::max (worst, std::abs (d));
                std::cout << "  envelope (speed x" << spd << "): delay " << juce::String (t0 * 1000, 2) << " ms, peak " << juce::String (tPeak * 1000, 2)
                          << " ms, decay from " << juce::String (tDec * 1000, 2) << " ms, sustain " << juce::String (tSus * 1000, 2) << " ms, release "
                          << juce::String (tRel * 1000, 2) << " ms; worst error " << juce::String (worst * 1000, 3) << " ms" << std::endl;
                CHECK (worst < 0.001, "operator envelope stages within 1 ms");
            }
        }
        // 4. feedback: stable (finite, bounded, still sounding) from none to the top of the range, every quality and waveform
        {
            float peak = 0; bool fin = true; double minRms = 1e9;
            for (int q = 0; q < 3; ++q)
                for (int w = 0; w < 8; ++w)
                    for (float fb : { 0.5f, 1.5f, 4.0f, 12.0f })
                    {
                        CoreSettings s = sine(); s.quality = q; s.op[0].wave = w; s.op[0].m[0] = fb; s.bright = 2.0f;
                        std::vector<float> x; run (s, 110.0, (int) (sr * 1.0), x);
                        double ss = 0;
                        for (float v : x) { fin &= std::isfinite (v); peak = std::max (peak, std::abs (v)); ss += (double) v * v; }
                        minRms = std::min (minRms, std::sqrt (ss / x.size()));
                    }
            std::cout << "  feedback up to 24 rad: peak " << juce::String (peak, 3) << ", lowest RMS " << juce::String (minRms, 3) << std::endl;
            CHECK (fin && peak < 2.0f && minRms > 0.05, "feedback stays finite and bounded at every setting");
        }
        // 5. aliasing: a bright patch (feedback 1.5 into a 1:1 pair, index 4) at C6; everything that is not a harmonic
        //    of the note, below 20 kHz, relative to the fundamental, at each quality
        double aliasDb[3] {};
        for (int q = 0; q < 3; ++q)
        {
            CoreSettings s = sine(); s.quality = q;
            s.op[1].level = 1.0f; s.op[1].m[0] = 4.0f; s.op[1].m[1] = 1.5f;
            const double f0 = 1046.502;
            std::vector<float> x; run (s, f0, (int) (sr * 1.7), x);
            const int from = (int) (sr * 0.2), len = 65536;
            std::vector<std::complex<double>> fx ((size_t) len);
            for (int i = 0; i < len; ++i) fx[(size_t) i] = x[(size_t) (from + i)] * (0.35875 - 0.48829 * std::cos (2 * kPi * i / len) + 0.14128 * std::cos (4 * kPi * i / len) - 0.01168 * std::cos (6 * kPi * i / len));
            juce::dsp::FFT fft (16);
            std::vector<juce::dsp::Complex<float>> in ((size_t) len), out ((size_t) len);
            for (int i = 0; i < len; ++i) in[(size_t) i] = { (float) fx[(size_t) i].real(), 0.0f };
            fft.perform (in.data(), out.data(), false);
            const double binHz = sr / len;
            double fund = 0, worstAlias = 0;
            for (int b = 1; b < (int) (20000.0 / binHz); ++b)
            {
                const double f = b * binHz, m = std::abs (std::complex<double> (out[(size_t) b].real(), out[(size_t) b].imag()));
                const double h = f / f0, dist = std::abs (h - std::round (h)) * f0;
                if (std::abs (f - f0) < 3 * binHz) fund = std::max (fund, m);
                else if (dist > 6 * binHz) worstAlias = std::max (worstAlias, m);
            }
            aliasDb[q] = 20.0 * std::log10 (worstAlias / fund);
            std::cout << "  aliasing at C6 (" << kQualityLabels[q] << "): worst non-harmonic component " << juce::String (aliasDb[q], 1) << " dB below the fundamental" << std::endl;
        }
        CHECK (aliasDb[2] < -70.0, "High: aliasing at C6 more than 70 dB down");
        CHECK (aliasDb[1] < -50.0, "Normal: aliasing at C6 more than 50 dB down");
        CHECK (aliasDb[2] < aliasDb[0] - 20.0, "oversampling lowers the aliasing (High vs Eco)");

        // ---- in the synth
        auto coreOnly = [&] (int slot = 0)
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_supersawGain, P_complexGain, P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain })
                setP (*p, i, 0.0f);
            setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_lfoAssignAmt0, 0.0f); setP (*p, P_ampS, 1.0f);
            setP (*p, coreParam (slot, CS_Engine), 1.0f);
            return p;
        };
        // 6. off: every Core setting moved but the engine Off renders bit-for-bit like the defaults
        {
            auto a = make(); auto b = make(); setP (*a, P_reverbMix, 0.0f); setP (*b, P_reverbMix, 0.0f);
            for (int i = P_cAEngine; i <= P_cB6Out; ++i)
            {
                if (i == coreParam (0, CS_Engine) || i == coreParam (1, CS_Engine)) continue;
                auto* prm = b->param (i); prm->setValueNotifyingHost (std::fmod (prm->getValue() + 0.37f, 1.0f));
            }
            juce::AudioBuffer<float> ca, cb;
            tg::resetVoiceRandom(); render (*a, 1.5, chord (0.0, 1.0, { 45, 52, 57 }), &ca);
            tg::resetVoiceRandom(); render (*b, 1.5, chord (0.0, 1.0, { 45, 52, 57 }), &cb);
            bool same = true;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < ca.getNumSamples(); ++i) same &= ca.getSample (c, i) == cb.getSample (c, i);
            CHECK (same, "Core slots Off: bit-for-bit the same sound whatever their settings");

        }
        // 7. on: a sine at the note through the voice, both slots, its own Mixer strip (mute, pan), the level
        for (int slot = 0; slot < 2; ++slot)
        {
            auto p = coreOnly (slot);
            juce::AudioBuffer<float> cap;
            const auto st = render (*p, 1.0, chord (0.0, 0.9, { 69 }), &cap);
            std::vector<float> x ((size_t) cap.getNumSamples()); for (int i = 0; i < cap.getNumSamples(); ++i) x[(size_t) i] = cap.getSample (0, i);
            std::vector<float> y (x.begin() + (int) (sr * 0.2), x.begin() + (int) (sr * 0.8));
            const double f = freqOf (y, 0);
            std::cout << "  Core " << (slot ? "B" : "A") << " in the synth: " << juce::String (f, 3) << " Hz at A4, peak " << juce::String (st.peak, 3) << std::endl;
            CHECK (std::abs (1200.0 * std::log2 (f / 440.0)) < 1.0 && st.peak > 0.05f && st.finite, juce::String ("Core ") + (slot ? "B" : "A") + " plays the note");
            p = coreOnly (slot);   // (a fresh one: no release tail from the note above)
            setP (*p, P_mxCoreAMute + 3 * slot, 1.0f);
            const auto m = render (*p, 0.6, chord (0.0, 0.5, { 69 }));
            CHECK (m.peak < 1.0e-6f, juce::String ("Core ") + (slot ? "B" : "A") + " Mixer mute");
            p = coreOnly (slot); setP (*p, P_mxCoreAPan + 3 * slot, -1.0f);
            juce::AudioBuffer<float> pc; render (*p, 0.6, chord (0.0, 0.5, { 69 }), &pc);
            CHECK (pc.getMagnitude (1, 0, pc.getNumSamples()) < 0.01f * pc.getMagnitude (0, 0, pc.getNumSamples()) + 1.0e-6f, juce::String ("Core ") + (slot ? "B" : "A") + " Mixer pan hard left");
        }
        // 8. unison spread is stereo; one voice is mono
        {
            auto p = coreOnly(); setP (*p, coreParam (0, CS_Uni), 3.0f); setP (*p, coreParam (0, CS_Spread), 1.0f);
            juce::AudioBuffer<float> cap; render (*p, 0.8, chord (0.0, 0.7, { 57 }), &cap);
            double diff = 0, sum = 0;
            for (int i = 0; i < cap.getNumSamples(); ++i) { diff += std::abs (cap.getSample (0, i) - cap.getSample (1, i)); sum += std::abs (cap.getSample (0, i)); }
            CHECK (diff > 0.1 * sum, "unison spread makes the Core stereo");
        }
        // 9. the Core A Out (audio rate) and Op 1 Env sources, and a Mod Matrix route into an operator setting
        {
            auto p = coreOnly();
            juce::AudioBuffer<float> a; render (*p, 0.6, chord (0.0, 0.5, { 57 }), &a);
            p->addRoute (MS_CoreAEnv, coreOpParam (0, 1, CO_Level), 1.0f);   // op 1's envelope opens op 2...
            setP (*p, coreOpParam (0, 1, CO_M1), 3.0f);                               // ... which modulates op 1
            juce::AudioBuffer<float> b; render (*p, 0.6, chord (0.0, 0.5, { 57 }), &b);
            float d = 0; for (int i = 0; i < a.getNumSamples(); ++i) d = std::max (d, std::abs (a.getSample (0, i) - b.getSample (0, i)));
            CHECK (d > 0.05f, "Core A Op 1 Env routed to an operator level changes the sound");
        }
        // ... and Core A (audio rate) into the filter cutoff, per sample (with Osc 1 through the filter to hear it)
        {
            auto mk = [&]
            {
                auto p = coreOnly(); setP (*p, P_osc1Gain, 0.5f); setP (*p, P_filterCutoff, 800.0f); setP (*p, P_cALevel, 0.001f);
                setP (*p, P_reverbMix, 0.0f); tg::resetVoiceRandom(); return p;
            };
            auto a = mk(); juce::AudioBuffer<float> ca; render (*a, 0.5, chord (0.0, 0.4, { 45 }), &ca);
            auto b = mk(); b->addRoute (MS_CoreAAudio, P_filterCutoff, 1.0f); setP (*b, P_cALevel, 1.0f); setP (*b, P_mxCoreAMute, 1.0f);
            tg::resetVoiceRandom(); juce::AudioBuffer<float> cb; render (*b, 0.5, chord (0.0, 0.4, { 45 }), &cb);
            float d = 0; for (int i = 0; i < ca.getNumSamples(); ++i) d = std::max (d, std::abs (ca.getSample (0, i) - cb.getSample (0, i)));
            CHECK (d > 0.01f, "Core A (audio rate) drives the filter cutoff, even with its strip muted");
        }
        // 10. patches: a patch file carries the Core settings; old patches and projects load with the Core off
        {
            auto p = coreOnly(); setP (*p, coreOpParam (0, 2, CO_Ratio), 7.0f);
            const auto text = p->exportBrowserPatch();
            auto q = make(); q->importBrowserPatch (text);
            CHECK (juce::approximatelyEqual (plainOf (*q, coreOpParam (0, 2, CO_Ratio)), 7.0f) && plainOf (*q, P_cAEngine) > 0.5f, "a patch file keeps the Core settings");
            auto obj = juce::JSON::parse (text);
            if (auto* ex = obj["plugin"].getDynamicObject())
                for (auto& prop : juce::NamedValueSet (ex->getProperties()))
                    if (prop.name.toString().startsWith ("cA") || prop.name.toString().startsWith ("cB") || prop.name.toString().startsWith ("mxCore")) ex->removeProperty (prop.name);
            auto r = coreOnly(); r->importBrowserPatch (juce::JSON::toString (obj));
            CHECK (plainOf (*r, P_cAEngine) < 0.5f, "a patch from before the Core loads with it Off");
        }
        // 11. Algorithm writes the matrix (one undo step), the picture follows it, Reset puts the slot back
        {
            auto p = coreOnly();
            p->applyCoreAlgorithm (0, 0);   // the stack 6>5>4>3>2>1
            bool ok = true;
            for (int k = 0; k < 6; ++k)
                for (int j = 0; j < 6; ++j)
                    if (j != k) ok &= juce::approximatelyEqual (plainOf (*p, coreOpParam (0, k, CO_M1 + j)), (k == j + 1) ? 2.0f : 0.0f);
            ok &= plainOf (*p, coreOpParam (0, 0, CO_Out)) > 0.99f && plainOf (*p, coreOpParam (0, 3, CO_Out)) < 0.01f && plainOf (*p, coreOpParam (0, 5, CO_Level)) > 0.4f;
            CHECK (ok, "Algorithm: the stack written into the matrix");
            tgui::CoreAlgoView view (*p, 0, juce::Colours::white);
            int d[6]; tgui::CoreAlgoView::depths (view.read(), d);
            CHECK (d[0] == 0 && d[1] == 1 && d[5] == 5, "the routing picture reads the stack (carrier op 1, op 6 five steps out)");
            p->applyCoreAlgorithm (0, 11);   // all six heard
            tgui::CoreAlgoView::depths (view.read(), d);
            bool all = true; for (int k = 0; k < 6; ++k) all &= d[k] == 0;
            CHECK (all, "the routing picture: organ, all six heard");
            p->undo();
            CHECK (juce::approximatelyEqual (plainOf (*p, coreOpParam (0, 1, CO_M1)), 2.0f) && plainOf (*p, coreOpParam (0, 5, CO_Out)) < 0.01f, "Algorithm is one undo step");
            setP (*p, coreParam (0, CS_Bright), 1.7f);
            p->resetCoreSlot (0);
            CHECK (plainOf (*p, P_cAEngine) > 0.5f && juce::approximatelyEqual (plainOf (*p, coreParam (0, CS_Bright)), 1.0f) && plainOf (*p, coreOpParam (0, 1, CO_M1)) < 0.01f,
                   "Reset: the slot back to its defaults, the engine kept");
            // the pages are there
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            CHECK (showPage (ed.get(), "Core A") && showPage (ed.get(), "Core B"), "Oscillators > Core A and Core B pages");
        }
        // 13. Envelope: Amp + Operators (the default, as before) or Operators only (the operators' own envelopes,
        //     Release included, and the note rings on until they finish)
        {
            auto tailOf = [&] (int mode, float opRel, float ampR, juce::AudioBuffer<float>& c, int slot = 0)
            {
                auto p = coreOnly (slot); setP (*p, P_ampR, ampR); setP (*p, coreOpParam (slot, 0, CO_Rel), opRel);
                setP (*p, coreEnvModeParam (slot), (float) mode); setP (*p, P_reverbMix, 0.0f); setP (*p, P_delayMix, 0.0f); setP (*p, P_chorusMix, 0.0f);
                render (*p, 5.0, chord (0.0, 0.5, { 57 }), &c);
                return p;
            };
            // where the envelope of the output falls 60 dB below its level while held
            auto endOf = [&] (const juce::AudioBuffer<float>& c)
            {
                double held = 0; for (int i = (int) (0.3 * sr); i < (int) (0.5 * sr); ++i) held = std::max (held, (double) std::abs (c.getSample (0, i)));
                const int win = (int) (0.005 * sr);
                double last = 0.5;
                for (int i = (int) (0.5 * sr); i + win < c.getNumSamples(); i += win)
                {
                    double pk = 0; for (int j = 0; j < win; ++j) pk = std::max (pk, (double) std::abs (c.getSample (0, i + j)));
                    if (pk > held * 0.001) last = (i + win) / sr;
                }
                return last - 0.5;
            };
            juce::AudioBuffer<float> a, b, d;
            tailOf (1, 3.0f, 0.1f, a);
            tailOf (0, 3.0f, 0.1f, b);
            const double ra = endOf (a), rb = endOf (b);
            std::cout << "  Envelope: Operators only, op Release 3 s, Amp Release 0.1 s: rings " << juce::String (ra, 3) << " s after the key (Amp + Operators: "
                      << juce::String (rb, 3) << " s)" << std::endl;
            CHECK (std::abs (ra - 3.0) < 0.01, "Operators only: a 3 s operator Release rings for 3 s (within 10 ms)");
            CHECK (rb < 0.2, "Amp + Operators: the amp envelope still ends the note");
            tailOf (1, 3.0f, 0.1f, d, 1);
            CHECK (std::abs (endOf (d) - 3.0) < 0.01, "Core B: Operators only rings for its Release too");
            // with Osc 1 alongside: Osc 1 still follows the amp envelope (gone well before the Core)
            {
                auto p = coreOnly(); setP (*p, P_ampR, 0.1f); setP (*p, P_cA1Rel, 3.0f); setP (*p, P_cAEnvMode, 1.0f); setP (*p, P_reverbMix, 0.0f); setP (*p, P_delayMix, 0.0f); setP (*p, P_chorusMix, 0.0f);
                setP (*p, P_osc1Gain, 0.8f); setP (*p, P_osc1Wave, 0.0f); setP (*p, P_cALevel, 0.0001f);   // (the Core nearly silent: what's left after the key is Osc 1)
                juce::AudioBuffer<float> c; render (*p, 2.0, chord (0.0, 0.5, { 57 }), &c);
                double pk = 0; for (int i = (int) (0.75 * sr); i < (int) (1.0 * sr); ++i) pk = std::max (pk, (double) std::abs (c.getSample (0, i)));
                CHECK (pk < 1.0e-3, "Operators only: the other oscillators still take the amp envelope");
            }
            // the voice is freed once the tail ends (no CPU after it)
            {
                auto p = coreOnly(); setP (*p, P_ampR, 0.1f); setP (*p, P_cA1Rel, 0.5f); setP (*p, P_cAEnvMode, 1.0f);
                render (*p, 2.0, chord (0.0, 0.3, { 57 }));
                CHECK (p->getEngine().activeVoiceCount() == 0, "Operators only: the voice ends when the operators have finished");
            }
            // sustain pedal: held notes keep the operators sustaining, the release starts on pedal up
            {
                auto p = coreOnly(); setP (*p, P_ampR, 0.1f); setP (*p, P_cA1Rel, 1.0f); setP (*p, P_cAEnvMode, 1.0f); setP (*p, P_reverbMix, 0.0f); setP (*p, P_delayMix, 0.0f); setP (*p, P_chorusMix, 0.0f);
                auto ev = chord (0.0, 0.3, { 57 });
                ev.push_back ({ 0.1, juce::MidiMessage::controllerEvent (1, 64, 127) });
                ev.push_back ({ 1.0, juce::MidiMessage::controllerEvent (1, 64, 0) });
                juce::AudioBuffer<float> c; render (*p, 3.0, ev, &c);
                double at08 = 0, at25 = 0;
                for (int i = (int) (0.8 * sr); i < (int) (0.85 * sr); ++i) at08 = std::max (at08, (double) std::abs (c.getSample (0, i)));
                for (int i = (int) (2.5 * sr); i < (int) (2.55 * sr); ++i) at25 = std::max (at25, (double) std::abs (c.getSample (0, i)));
                CHECK (at08 > 0.05 && at25 < 1.0e-3, "Operators only: the sustain pedal holds it, the release follows pedal up");
            }
            // many long tails: polyphony still steals the quietest when it runs out
            {
                auto p = coreOnly(); setP (*p, P_ampR, 0.05f); setP (*p, P_cA1Rel, 8.0f); setP (*p, P_cAEnvMode, 1.0f); setP (*p, P_polyphony, 4.0f);
                std::vector<std::pair<double, juce::MidiMessage>> ev;
                for (int k = 0; k < 24; ++k) { ev.push_back ({ 0.05 * k, juce::MidiMessage::noteOn (1, 40 + k, (juce::uint8) 100) }); ev.push_back ({ 0.05 * k + 0.04, juce::MidiMessage::noteOff (1, 40 + k) }); }
                const auto st = render (*p, 2.0, ev);
                CHECK (st.finite && st.peak < 2.0f, "Operators only: 24 long tails, voices stolen cleanly");
            }
        }
        // 14. the Core goes through the Space effects like the other oscillators: each effect's share of the
        //     output (wet minus dry, against dry) for Core A / B against Osc 1, the same sine at the same note
        //     (unison without detune, spread across the stereo field: detuned stacks beat differently, which
        //     changes how much reverb a sound gets without saying anything about the routing)
        {
            auto shareOf = [&] (int src, int fx, int uni, int q, int preset)   // src: 0 Osc 1, 1 Core A, 2 Core B
            {
                double out[2] {};
                juce::AudioBuffer<float> c[2];
                for (int wet = 0; wet < 2; ++wet)
                {
                    auto p = make();
                    if (preset >= 0) p->loadFactoryPreset (preset);
                    for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain, P_complexGain, P_supersawGain, P_rsLevel, P_gsOn, P_bcOn, P_analogDrift,
                                   P_reverbMix, P_delayMix, P_shimmerMix, P_chorusMix, P_reverseMix, P_fxOnStereo, P_arpOn, P_chdOn, P_seqRun, P_bsOn, P_dsOn, P_mutAmount, P_ciAmount,
                                   P_grMix, P_spOn })   // (and the bus Granular / Spectral: their grains and frames are random, not Space)
                        setP (*p, i, 0.0f);
                    setP (*p, P_cAEngine, 0.0f); setP (*p, P_cBEngine, 0.0f);
                    setP (*p, P_filterCutoff, 20000.0f); setP (*p, P_filter2On, 0.0f); setP (*p, P_ampA, 0.005f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.3f); setP (*p, P_quality, (float) q);
                    const int uv = uni == 0 ? 1 : 8;
                    if (src == 0) { setP (*p, P_osc1Wave, 3.0f); setP (*p, P_osc1Gain, 0.5f); setP (*p, P_osc1Oct, 0.0f); setP (*p, P_osc1Semi, 0.0f); setP (*p, P_osc1Detune, 0.0f); setP (*p, P_uni1Voices, (float) uv); setP (*p, P_uni1Detune, 0.0f); setP (*p, P_uni1Spread, 1.0f); setP (*p, P_uni1Blend, 1.0f); }
                    else
                    {
                        const int s = src - 1;
                        setP (*p, coreParam (s, CS_Engine), 1.0f); setP (*p, coreParam (s, CS_Level), 0.5f); setP (*p, coreOpParam (s, 0, CO_Rel), 5.0f);
                        setP (*p, coreParam (s, CS_Uni), uni == 0 ? 0.0f : 5.0f); setP (*p, coreParam (s, CS_Detune), 0.0f); setP (*p, coreParam (s, CS_Spread), 1.0f);
                    }
                    if (wet)
                    {
                        switch (fx)
                        {
                            case 0: setP (*p, P_fxOnDelay, 1.0f); setP (*p, P_delayMix, 0.5f); break;
                            case 1: setP (*p, P_fxOnReverb, 1.0f); setP (*p, P_reverbMix, 0.5f); break;
                            case 2: setP (*p, P_fxOnReverb, 1.0f); setP (*p, P_shimmerMix, 0.5f); break;
                            case 3: setP (*p, P_fxOnChorus, 1.0f); setP (*p, P_chorusMix, 0.6f); break;
                            case 4: setP (*p, P_fxOnReverb, 1.0f); setP (*p, P_reverseMix, 0.5f); break;
                            default: setP (*p, P_fxOnStereo, 1.0f); setP (*p, P_sttMono, 0.0f); setP (*p, P_sttHaas, 20.0f); setP (*p, P_sttWidth, 1.6f); break;
                        }
                    }
                    juce::AudioBuffer<float> buf (2, 256);
                    for (int b = 0; b < 300; ++b) { buf.clear(); juce::MidiBuffer none; p->processBlock (buf, none); if (b % 3 == 0) juce::Thread::sleep (1); }   // (the reverbs load their impulses first)
                    tg::resetVoiceRandom();
                    render (*p, 2.5, chord (0.0, 1.0, { 69 }), &c[wet]);
                }
                double dd = 0, ww = 0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < c[0].getNumSamples(); ++i) { const double x = c[0].getSample (ch, i), y = c[1].getSample (ch, i); dd += x * x; ww += (y - x) * (y - x); }
                out[0] = dd;
                return 10.0 * std::log10 (std::max (1.0e-30, ww) / std::max (1.0e-30, out[0]));
            };
            const char* fxName[] = { "Tape Delay", "90s Reverb", "Shimmer", "Juno Chorus", "Reverse Reverb", "Stereo Tools (Haas, Width)" };
            double worst = 0; juce::String worstAt;
            for (int fx = 0; fx < 6; ++fx)
            {
                const double o = shareOf (0, fx, 0, 1, -1), a = shareOf (1, fx, 0, 1, -1);
                std::cout << "  " << fxName[fx] << ": effect share Osc 1 " << juce::String (o, 2) << " dB, Core A " << juce::String (a, 2) << " dB" << std::endl;
                if (std::abs (a - o) > worst) { worst = std::abs (a - o); worstAt = fxName[fx]; }
            }
            // ... and Core B, 8 unison voices, every quality, and in factory presets (Delay and Reverb)
            for (int fx : { 0, 1 })
            {
                for (int q = 0; q < 3; ++q)
                    for (int uni = 0; uni < 2; ++uni)
                    {
                        const double o = shareOf (0, fx, uni, q, -1), a = shareOf (1, fx, uni, q, -1), b = shareOf (2, fx, uni, q, -1);
                        for (double x : { a, b }) if (std::abs (x - o) > worst) { worst = std::abs (x - o); worstAt = juce::String (fxName[fx]) + ", " + kQualityLabels[q] + (uni ? ", 8 voices" : ""); }
                    }
                for (int preset : { 0, 33, tg::kFirstDnb + 4, tg::kFirstDnb + 60 })
                {
                    const double o = shareOf (0, fx, 0, 1, preset), a = shareOf (1, fx, 0, 1, preset);
                    if (std::abs (a - o) > worst) { worst = std::abs (a - o); worstAt = juce::String (fxName[fx]) + " in " + tg::factoryPresets()[(size_t) preset].name; }
                }
            }
            std::cout << "  Space effects: Core A / B against Osc 1, worst difference " << juce::String (worst, 3) << " dB (" << worstAt << ")" << std::endl;
            CHECK (worst < 0.5, "the Core goes through every Space effect like Osc 1 (within 0.5 dB)");
        }
        // 12. CPU: a 4-note chord on Init, then with Core A on top (all six operators in a stack with feedback,
        //     1 and 8 unison voices), per quality; the best of five runs
        {
            auto cpuOf = [&] (int uni, int q)
            {
                double best = 1e9;
                for (int rep = 0; rep < 5; ++rep)
                {
                    auto p = make(); setP (*p, P_quality, (float) q);
                    if (uni >= 0) { setP (*p, P_cAEngine, 1.0f); p->applyCoreAlgorithm (0, 0); setP (*p, P_cA6M6, 1.0f); setP (*p, coreParam (0, CS_Uni), (float) uni); }
                    render (*p, 0.2, {});
                    double cpu = 0; render (*p, 2.0, chord (0.0, 1.8, { 45, 52, 57, 60 }), nullptr, &cpu);
                    best = std::min (best, cpu / 2.0 * 100.0);
                }
                return best;
            };
            for (int q = 0; q < 3; ++q)
            {
                const double base = cpuOf (-1, q), one = cpuOf (0, q), eight = cpuOf (5, q);
                std::cout << "  CPU, 4 notes, " << kQualityLabels[q] << ": Init " << juce::String (base, 2) << "% of a core; with Core A (6-op stack) "
                          << juce::String (one, 2) << "% (+" << juce::String (one - base, 2) << "), with 8 unison voices " << juce::String (eight, 2)
                          << "% (+" << juce::String (eight - base, 2) << ")" << std::endl;
                if (q == 1) CHECK (one - base < 15.0, "Core A (one voice), 4 notes, Normal: under 15% of a core on top of Init");
            }
        }
    };
    // ---- Break Sequencer Key Entry: a slice key puts that slice on the selected step
    auto keyEntryTests = [&]
    {
        std::cout << "Break Sequencer Key Entry" << std::endl;
        auto setup = [&] (bool on, int adv)
        {
            auto p = make(); p->loadFactoryPreset (32);   // Break Chop 170 (the synthesised break, sliced)
            setP (*p, P_bsKeyEntry, on ? 1.0f : 0.0f); setP (*p, P_bsAdvance, (float) adv); setP (*p, P_bsOn, 0.0f);
            return p;
        };
        auto play = [&] (MegaSynthProcessor& p, std::vector<juce::MidiMessage> msgs)
        {
            juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m;
            int pos = 0; for (auto& x : msgs) m.addEvent (x, pos++);
            p.processBlock (buf, m);
            buf.clear(); juce::MidiBuffer none; p.processBlock (buf, none);
        };
        const auto kb = [] (MegaSynthProcessor& p) { return juce::roundToInt (p.param (P_bcKeyBase)->convertFrom0to1 (p.param (P_bcKeyBase)->getValue())); };
        const auto pat = [] (MegaSynthProcessor& p) { return juce::roundToInt (p.param (P_bsPattern)->convertFrom0to1 (p.param (P_bsPattern)->getValue())); };
        // on, Advance 1: step 5 gets slice 3 (on, the key's velocity, pinned to the Active Break) and the selection moves to step 6
        {
            auto p = setup (true, 1);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
            showPage (ed.get(), "Break Sequencer");
            p->breakStore.edit ([&] (tg::BreakData& d) { d.step[pat (*p)][4] = tg::BreakStep(); });
            p->pushHistory ("Test setup");
            e->setBreakSelectedStepForTests (4);
            play (*p, { juce::MidiMessage::noteOn (1, kb (*p) + 2, 0.5f) });
            e->breakKeyEntryForTests();
            const auto s = p->breakStore.get().step[pat (*p)][4];
            std::cout << "  step 5: on " << (int) s.on << ", slice " << s.slice + 1 << ", velocity " << juce::String (s.vel, 2) << ", break " << s.brk << "; selected step " << e->breakSelectedStepForTests() + 1 << std::endl;
            CHECK (s.on && s.slice == 2 && std::abs (s.vel - 0.5f) < 0.01f && s.brk == 1 && e->breakSelectedStepForTests() == 5, "Key Entry: slice 3 on step 5, the selection moves to step 6");
            CHECK (p->canUndo(), "Key Entry is an undo step");
            p->undo();
            CHECK (! p->breakStore.get().step[pat (*p)][4].on, "Undo takes the key entry back");
            // the on-screen keys work the same way (they reach the plug-in as MIDI)
            e->setBreakSelectedStepForTests (9);
            p->keyboardState.noteOn (1, kb (*p) + 5, 0.9f);
            play (*p, {});
            p->keyboardState.noteOff (1, kb (*p) + 5, 0.0f);
            e->breakKeyEntryForTests();
            const auto s2 = p->breakStore.get().step[pat (*p)][9];
            CHECK (s2.on && s2.slice == 5 && e->breakSelectedStepForTests() == 10, "Key Entry from the on-screen keys");
            // Advance 4 wraps round the pattern
            setP (*p, P_bsAdvance, 3.0f);
            const int steps = p->param (P_bsSteps)->getValue() > 0.5f ? 32 : 16;
            e->setBreakSelectedStepForTests (steps - 2);
            play (*p, { juce::MidiMessage::noteOn (1, kb (*p), 0.8f) });
            e->breakKeyEntryForTests();
            CHECK (e->breakSelectedStepForTests() == 2, "Advance 4 wraps round the pattern");
            // the sustain pedal (or Shift) adds a Layer hit to a step that's already on
            setP (*p, P_bsAdvance, 0.0f);
            e->setBreakSelectedStepForTests (4);
            p->breakStore.edit ([&] (tg::BreakData& d) { auto& x = d.step[pat (*p)][4]; x = tg::BreakStep(); x.on = true; x.slice = 0; });
            play (*p, { juce::MidiMessage::controllerEvent (1, 64, 127), juce::MidiMessage::noteOn (1, kb (*p) + 7, 0.6f) });
            e->breakKeyEntryForTests();
            const auto s3 = p->breakStore.get().step[pat (*p)][4];
            CHECK (s3.on && s3.slice == 0 && s3.layer && s3.layerSlice == 7 && e->breakSelectedStepForTests() == 4, "With the pedal down: a Layer hit, the main slice kept; Advance off stays put");
            play (*p, { juce::MidiMessage::controllerEvent (1, 64, 0) });
            // the slice still plays
            juce::AudioBuffer<float> c; render (*p, 0.3, { { 0.0, juce::MidiMessage::noteOn (1, kb (*p) + 1, 0.9f) } }, &c);
            CHECK (c.getMagnitude (0, 0, c.getNumSamples()) > 0.01f, "Key Entry: the slice is still heard");
            // Pattern Keys and Break Select keys keep doing their own jobs
            setP (*p, P_bsKeys, 1.0f); setP (*p, P_bcSelKeys, 1.0f);
            e->breakKeyEntryForTests();   // (the slice played just above goes in first)
            const auto before = p->breakStore.get();
            play (*p, { juce::MidiMessage::noteOn (1, 24 + 1, 0.8f), juce::MidiMessage::noteOn (1, kb (*p) - 2, 0.8f) });
            e->breakKeyEntryForTests();
            const auto after = p->breakStore.get();
            bool same = true; for (int q = 0; q < 8; ++q) for (int i = 0; i < 32; ++i) same &= after.step[q][i].on == before.step[q][i].on && after.step[q][i].slice == before.step[q][i].slice;
            CHECK (same, "Pattern Keys and Break Select keys: not entered as slices");
        }
        // off: nothing changes
        {
            auto p = setup (false, 1);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
            showPage (ed.get(), "Break Sequencer");
            e->setBreakSelectedStepForTests (4);
            const auto before = p->breakStore.get().step[pat (*p)][4];
            play (*p, { juce::MidiMessage::noteOn (1, kb (*p) + 2, 0.5f) });
            e->breakKeyEntryForTests();
            const auto s = p->breakStore.get().step[pat (*p)][4];
            CHECK (s.on == before.on && s.slice == before.slice && e->breakSelectedStepForTests() == 4, "Key Entry off: the pattern is left alone");
        }
        // a key played long before the page looked is dropped (no surprise entries later)
        {
            auto p = setup (true, 1);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
            showPage (ed.get(), "Break Sequencer");
            e->setBreakSelectedStepForTests (4);
            p->breakStore.edit ([&] (tg::BreakData& d) { d.step[pat (*p)][4] = tg::BreakStep(); });
            play (*p, { juce::MidiMessage::noteOn (1, kb (*p) + 2, 0.5f) });
            juce::Thread::sleep (700);
            e->breakKeyEntryForTests();
            CHECK (! p->breakStore.get().step[pat (*p)][4].on, "Key Entry: a stale key (over half a second old) isn't entered");
        }
    };
    // ---- the random patch generator
    auto plainOf = [] (MegaSynthProcessor& q, int i) { return q.param (i)->convertFrom0to1 (q.param (i)->getValue()); };
    // renders a patch's audition phrase (2 bars at 172 BPM, the transport running) and returns its peak (dBFS) and CPU (% of a core)
    auto phraseOf = [&] (MegaSynthProcessor& p, const juce::String& cat, juce::AudioBuffer<float>* keep = nullptr, double* cpuPct = nullptr)
    {
        struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        Head head;
        p.prepareToPlay (sr, 512);   // (a clean start, as the generator measures it)
        p.setPlayHead (&head);
        const double bpm = 172.0, inc = bpm / 60.0 / sr;
        juce::AudioBuffer<float> buf (2, 256);
        if (plainOf (p, P_reverbMix) + plainOf (p, P_shimmerMix) + plainOf (p, P_reverseMix) > 0.0f)
            for (int k = 0; k < 160; ++k) { head.info.setIsPlaying (false); head.info.setBpm (bpm); head.info.setPpqPosition (0.0); buf.clear(); juce::MidiBuffer none; p.processBlock (buf, none); if (k == 0) juce::Thread::sleep (150); }
        const bool chopKeys = plainOf (p, P_bcOn) > 0.5f && plainOf (p, P_bsOn) < 0.5f;
        const auto notes = MegaSynthProcessor::auditionPhrase (cat, chopKeys);
        std::vector<std::pair<int, juce::MidiMessage>> ev;
        for (auto& n : notes) { ev.push_back ({ (int) (n[0] / inc), juce::MidiMessage::noteOn (1, (int) n[2], (juce::uint8) 100) }); ev.push_back ({ (int) ((n[0] + n[1]) / inc), juce::MidiMessage::noteOff (1, (int) n[2]) }); }
        const int total = (int) (8.0 / inc) + (int) (0.6 * sr);
        if (keep) keep->setSize (2, total);
        double ppq = 0.0; float peak = 0.0f; bool fin = true;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            buf.setSize (2, n, false, false, true);
            head.info.setIsPlaying (pos < (int) (8.0 / inc)); head.info.setBpm (bpm); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            juce::MidiBuffer m; for (auto& e : ev) if (e.first >= pos && e.first < pos + n) m.addEvent (e.second, e.first - pos);
            buf.clear(); p.processBlock (buf, m); ppq += n * inc;
            for (int ch = 0; ch < 2; ++ch) { peak = std::max (peak, buf.getMagnitude (ch, 0, n)); for (int i = 0; i < n; ++i) fin &= std::isfinite (buf.getSample (ch, i)); if (keep) keep->copyFrom (ch, pos, buf, ch, 0, n); }
        }
        if (cpuPct) *cpuPct = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 / ((double) total / sr) * 100.0;
        p.setPlayHead (nullptr);
        return fin ? (double) juce::Decibels::gainToDecibels (peak, -200.0f) : 999.0;
    };
    // a folder of test files: three synthesised breaks (plus a too-short file and a text file), and three tones
    auto makeRandomFolders = [&]
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("megasynth_random_test");
        root.deleteRecursively();
        auto br = root.getChildFile ("Breaks"), sm = root.getChildFile ("Samples");
        br.getChildFile ("sub").createDirectory(); sm.createDirectory();
        const double bpms[3] = { 170.0, 174.0, 165.0 };
        for (int k = 0; k < 3; ++k)
        {
            const auto mb = tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, bpms[k], k), 44100.0);
            (k == 2 ? br.getChildFile ("sub") : br).getChildFile ("Test Break " + juce::String (k + 1) + ".wav").replaceWithData (mb.getData(), mb.getSize());
        }
        {
            juce::AudioBuffer<float> s (1, 22050 / 2); for (int i = 0; i < s.getNumSamples(); ++i) s.setSample (0, i, 0.5f * (float) std::sin (i * 0.05));
            const auto mb = tg::breaks::toWav16 (s, 44100.0); br.getChildFile ("Too Short.wav").replaceWithData (mb.getData(), mb.getSize());
            br.getChildFile ("readme.txt").replaceWithText ("not audio");
        }
        for (int k = 0; k < 3; ++k)
        {
            juce::AudioBuffer<float> s (1, 44100 * (k + 1));
            const double f = 110.0 * (k + 1);
            for (int i = 0; i < s.getNumSamples(); ++i) { const double ph = 2.0 * kPi * f * i / 44100.0; s.setSample (0, i, (float) (0.4 * std::sin (ph) + 0.2 * std::sin (2 * ph + i * 1.0e-5) + 0.1 * std::sin (3 * ph))); }
            const auto mb = tg::breaks::toWav16 (s, 44100.0); sm.getChildFile ("Test Tone " + juce::String (k + 1) + ".wav").replaceWithData (mb.getData(), mb.getSize());
        }
        return std::make_pair (br, sm);
    };
    auto randomTests = [&] (int seeds)
    {
        using namespace tg::randompatch;
        std::cout << "Random patch generator" << std::endl;
        const auto folders = makeRandomFolders();
        MegaSynthProcessor::setRandomFolder (0, folders.first); MegaSynthProcessor::rescanRandomFolder (0, true);
        MegaSynthProcessor::setRandomFolder (1, folders.second); MegaSynthProcessor::rescanRandomFolder (1, true);
        {
            const auto bl = MegaSynthProcessor::randomFolderList (0), sl = MegaSynthProcessor::randomFolderList (1);
            std::cout << "  folders: " << bl.files.size() << " breaks, " << sl.files.size() << " samples" << std::endl;
            CHECK (bl.files.size() == 3 && sl.files.size() == 3, "the folder scan finds the 3 breaks (in subfolders too, not the short file or the text file) and the 3 samples");
        }
        // (debug: MEGASYNTH_RANDOMDET=1 renders one patch on fresh instances, with parts switched off, to find a difference)
        if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMDET", {}).isNotEmpty())
        {
            Options o; o.seed = 31337; o.category = C_Pads; o.complexity = 2; o.use[U_Breaks] = true;
            auto g = make(); std::cout << "  " << g->generateRandomPatchNow (o).upToFirstOccurrenceOf ("\n", false, false) << std::endl;
            const auto ta = g->exportBrowserPatch();
            auto test = [&] (const char* what, std::vector<std::pair<int, float>> sets)
            {
                float worst = 0;
                juce::AudioBuffer<float> first;
                const bool quick = juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMDET", {}) == "quick";
                for (int k = 0; k < (quick ? 2 : 4); ++k)
                {
                    auto c = make(); c->importBrowserPatch (ta);
                    for (auto& [i, v] : sets) setP (*c, i, v);
                    juce::AudioBuffer<float> cc; tg::resetVoiceRandom(); phraseOf (*c, "Pads & Atmos", &cc);
                    if (k == 0) { first = cc; continue; }
                    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < cc.getNumSamples(); ++i) worst = std::max (worst, std::abs (cc.getSample (ch, i) - first.getSample (ch, i)));
                }
                std::cout << "    " << what << ": " << worst << std::endl;
            };
            test ("as made", {});
            if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMDET", {}) == "quick") return;
            test ("no reverb/shimmer", { { P_reverbMix, 0.0f }, { P_shimmerMix, 0.0f }, { P_reverseMix, 0.0f } });
            test ("no delay/chorus", { { P_delayMix, 0.0f }, { P_chorusMix, 0.0f } });
            test ("no breaks", { { P_bcOn, 0.0f } });
            test ("no bus granular", { { P_grMix, 0.0f } });
            test ("no granular synth", { { P_gsOn, 0.0f } });
            test ("no core", { { P_cAEngine, 0.0f }, { P_cBEngine, 0.0f } });
            test ("no mutation", { { P_wmMix, 0.0f }, { P_resMix, 0.0f }, { P_dnaMix, 0.0f }, { P_arFm, 0.0f }, { P_arRing, 0.0f } });
            return;
        }
        // (debug: MEGASYNTH_RANDOMPROF=seed,seed... profiles where each patch spends its CPU)
        if (auto prof = juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMPROF", {}); prof.isNotEmpty())
        {
            for (auto sd : juce::StringArray::fromTokens (prof, ",", {}))
            {
                const int s = sd.getIntValue() - 1000;
                Options o; o.seed = (uint32_t) (1000 + s); o.category = (s - 1) % kNumCategories; o.complexity = ((s - 1) / kNumCategories) % 3;
                o.use[U_Breaks] = (s % 3) == 0 || o.category == C_Breaks; o.use[U_Sequencers] = (s % 4) == 0;
                auto p = make();
                std::cout << "  " << p->generateRandomPatchNow (o).upToFirstOccurrenceOf ("\n", false, false) << std::endl;
                setP (*p, P_quality, 1.0f);
                const auto saved = p->exportBrowserPatch();
                auto run = [&] (const char* what, std::vector<std::pair<int, float>> sets)
                {
                    p->importBrowserPatch (saved); setP (*p, P_quality, 1.0f);
                    for (auto& [i, v] : sets) setP (*p, i, v);
                    tg::resetVoiceRandom(); double cpu = 0; phraseOf (*p, categoryName (o.category), nullptr, &cpu);
                    std::cout << "    " << what << ": " << juce::String (cpu, 1) << "%" << std::endl;
                };
                std::cout << "    poly " << plainOf (*p, P_polyphony) << " coreUni " << plainOf (*p, P_cAUni) << " envMode " << plainOf (*p, P_cAEnvMode) << " gs " << plainOf (*p, P_gsOn)
                          << " ssaw " << plainOf (*p, P_supersawGain) << " arp " << plainOf (*p, P_arpOn) << std::endl;
                run ("as made", {});
                run ("no Core", { { P_cAEngine, 0.0f }, { P_cBEngine, 0.0f } });
                run ("Core amp+ops", { { P_cAEnvMode, 0.0f } });
                run ("no granular", { { P_gsOn, 0.0f } });
                run ("no supersaw", { { P_supersawGain, 0.0f } });
                run ("no osc", { { P_osc1Gain, 0.0f }, { P_osc2Gain, 0.0f }, { P_osc3Gain, 0.0f }, { P_osc4Gain, 0.0f }, { P_wt2Gain, 0.0f } });
                run ("poly 6", { { P_polyphony, 6.0f } });
                run ("no reverb", { { P_reverbMix, 0.0f } });
                run ("no arp", { { P_arpOn, 0.0f } });
                run ("no resonator", { { P_resMix, 0.0f } });
                run ("no audio-rate", { { P_arFm, 0.0f }, { P_arRing, 0.0f } });
                run ("no bus granular", { { P_grMix, 0.0f } });
                run ("no unison", { { P_uni1Voices, 1.0f } });
            }
            return;
        }
        // 1. many seeds, every category and complexity: finite, matched, not silent, under the CPU budget
        {
            auto p = make();
            double lo = 1e9, hi = -1e9, worstCpu = 0; juce::String heaviest; int silent = 0, bad = 0, errors = 0;
            for (int s = 1; s <= seeds; ++s)
            {
                Options o; o.seed = (uint32_t) (1000 + s); o.category = (s - 1) % kNumCategories; o.complexity = ((s - 1) / kNumCategories) % 3;
                o.use[U_Breaks] = (s % 3) == 0 || o.category == C_Breaks; o.use[U_Sequencers] = (s % 4) == 0;
                const auto rep = p->generateRandomPatchNow (o);
                if (rep.startsWith ("Error")) { ++errors; std::cout << "  " << rep << std::endl; continue; }
                setP (*p, P_quality, 1.0f);
                tg::resetVoiceRandom();
                double cpu = 0;
                const double pk = phraseOf (*p, categoryName (o.category), nullptr, &cpu);
                if (pk > 900) { ++bad; continue; }
                if (pk < -60) ++silent;
                if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMDBG", {}).isNotEmpty())
                    std::cout << "    seed " << (int) o.seed << " " << categoryName (o.category) << " " << kComplexityNames[o.complexity] << ": " << juce::String (pk, 2) << " dBFS, " << juce::String (cpu, 1) << "%  "
                              << rep.upToFirstOccurrenceOf ("\n", false, false) << " rev " << plainOf (*p, P_reverbMix) << " out " << plainOf (*p, P_outGain) << std::endl;
                lo = std::min (lo, pk); hi = std::max (hi, pk);
                if (cpu > worstCpu) { worstCpu = cpu; heaviest = p->getPatchName() + " (" + categoryName (o.category) + ", " + kComplexityNames[o.complexity] + ", seed " + juce::String ((int) o.seed) + ")"; }
            }
            std::cout << "  " << seeds << " seeds: peaks " << juce::String (lo, 1) << " to " << juce::String (hi, 1) << " dBFS, " << silent << " silent, "
                      << bad << " not finite, " << errors << " errors; heaviest " << juce::String (worstCpu, 1) << "% of a core (Normal): " << heaviest << std::endl;
            CHECK (errors == 0 && bad == 0 && silent == 0, "every random patch renders finite and isn't silent");
            CHECK (lo >= -18.0 && hi <= -6.0, "every random patch peaks between -18 and -6 dBFS on its phrase");
            CHECK (worstCpu < 60.0, "every random patch under 60% of a core on Normal (its phrase)");
        }
        // 2. No oscillators, and each untick removes its engine
        {
            auto p = make();
            bool ok = true;
            for (int s = 0; s < 20; ++s)
            {
                Options o; o.seed = (uint32_t) (500 + s); o.category = s % kNumCategories; o.complexity = 2; o.noOsc = true; o.use[U_Breaks] = true;
                p->generateRandomPatchNow (o);
                for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain }) ok &= plainOf (*p, i) == 0.0f;
            }
            CHECK (ok, "No oscillators: Osc 1-3 and the Sub are at 0 in every patch");
            struct Off { int use; std::vector<int> zero; };
            const Off offs[] = { { U_Wavetables, { P_osc4Gain, P_wt2Gain } }, { U_Complex, { P_complexGain } }, { U_SuperSaw, { P_supersawGain } },
                                 { U_Granular, { P_gsOn } }, { U_Reese, { P_rsLevel } }, { U_Core, { P_cAEngine, P_cBEngine } }, { U_Breaks, { P_bcOn } },
                                 { U_NoteMut, { P_wmMix, P_dnaMix, P_resMix, P_arFm, P_arRing } }, { U_BusMut, { P_grMix, P_spOn } },
                                 { U_Sequencers, { P_dsOn, P_seqRun } }, { U_ArpChord, { P_arpOn, P_chdOn } } };
            juce::StringArray failed;
            for (auto& f : offs)
            {
                bool allOff = true;
                for (int s = 0; s < 20; ++s)
                {
                    Options o; o.seed = (uint32_t) (700 + s); o.category = s % kNumCategories; o.complexity = 2;
                    o.use[U_Breaks] = f.use != U_Breaks; o.use[U_Sequencers] = f.use != U_Sequencers; o.use[f.use] = false;
                    p->generateRandomPatchNow (o);
                    for (int i : f.zero) allOff &= plainOf (*p, i) == 0.0f;
                }
                if (! allOff) failed.add (kUseNames[f.use]);
            }
            CHECK (failed.isEmpty(), "each untick keeps its engine out" + (failed.isEmpty() ? juce::String() : ": " + failed.joinIntoString (", ")));
            Options none; for (auto& u : none.use) u = false; none.noOsc = true;
            CHECK (p->generateRandomPatchNow (none).startsWith ("Error"), "every source unticked: it says so and doesn't generate");
        }
        // 3. Keep leaves its groups bit for bit
        {
            auto p = make(); p->loadFactoryPreset (tg::kFirstDnb + 20);
            std::vector<float> before ((size_t) P_COUNT); for (int i = 0; i < P_COUNT; ++i) before[(size_t) i] = p->param (i)->getValue();
            const auto rackBefore = juce::JSON::toString (p->fxRack.toVar());
            Options o; o.seed = 4242; o.category = C_Neuro; o.complexity = 2; o.keepMask = (1u << tg::ML_Filter) | (1u << tg::ML_Fx) | (1u << tg::ML_Env);
            p->generateRandomPatchNow (o);
            int changed = 0, other = 0;
            for (int i = 0; i < P_COUNT; ++i)
            {
                const int g = tg::geneGroupOf (i);
                const bool same = p->param (i)->getValue() == before[(size_t) i];
                if (g == tg::ML_Filter || g == tg::ML_Fx || g == tg::ML_Env) changed += same ? 0 : 1;
                else other += same ? 0 : 1;
            }
            std::cout << "  Keep Filter, Effects, Envelopes: " << changed << " of their parameters changed; " << other << " others changed" << std::endl;
            CHECK (changed == 0 && other > 20 && juce::JSON::toString (p->fxRack.toVar()) == rackBefore, "Keep: the kept groups (and the rack order) stay bit for bit, the rest is new");
        }
        // 4. the same seed, settings and folders: the same patch, bit for bit, and the same sound
        {
            Options o; o.seed = 31337; o.category = C_Pads; o.complexity = 2; o.use[U_Breaks] = true;
            auto a = make(); auto b = make();
            a->generateRandomPatchNow (o); b->generateRandomPatchNow (o);
            const auto ta = a->exportBrowserPatch(), tb = b->exportBrowserPatch();
            CHECK (ta == tb, "the same seed and settings: the same patch, bit for bit");
            juce::AudioBuffer<float> ca, cb;
            tg::resetVoiceRandom(); phraseOf (*a, "Pads & Atmos", &ca);
            tg::resetVoiceRandom(); phraseOf (*b, "Pads & Atmos", &cb);
            float d = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < ca.getNumSamples(); ++i) d = std::max (d, std::abs (ca.getSample (ch, i) - cb.getSample (ch, i)));
            CHECK (d == 0.0f, "... and the same sound, bit for bit (" + juce::String (d) + ")");
            // saved and reloaded: identical
            auto c = make(); c->importBrowserPatch (ta);
            juce::AudioBuffer<float> cc; tg::resetVoiceRandom(); phraseOf (*c, "Pads & Atmos", &cc);
            float d2 = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < ca.getNumSamples(); ++i) d2 = std::max (d2, std::abs (ca.getSample (ch, i) - cc.getSample (ch, i)));
            auto e = make(); e->importBrowserPatch (ta);
            juce::AudioBuffer<float> ce; tg::resetVoiceRandom(); phraseOf (*e, "Pads & Atmos", &ce);
            float d3 = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < cc.getNumSamples(); ++i) d3 = std::max (d3, std::abs (cc.getSample (ch, i) - ce.getSample (ch, i)));
            std::cout << "  saved and loaded again: " << juce::String (juce::Decibels::gainToDecibels (d2, -300.0f), 1) << " dB from the patch as made (the rounding of a saved value), "
                      << (d3 == 0.0f ? "and the same every time it loads" : "different between loads") << std::endl;
            CHECK (d2 < 1.0e-6f && d3 == 0.0f, "a random patch saved and loaded again sounds the same (to the rounding every saved patch has), bit for bit on every load");
        }
        // 5. samples only from the chosen folders; an empty or missing folder falls back cleanly
        {
            auto p = make();
            juce::StringArray brNames, smNames; for (auto& f : MegaSynthProcessor::randomFolderList (0).files) brNames.add (f.getFileName());
            for (auto& f : MegaSynthProcessor::randomFolderList (1).files) smNames.add (f.getFileName());
            bool fromFolder = true; int usedBreaks = 0, usedSamples = 0;
            for (int s = 0; s < 24; ++s)
            {
                Options o; o.seed = (uint32_t) (900 + s); o.category = s % 2 == 0 ? C_Breaks : C_Pads; o.complexity = 2; o.use[U_Breaks] = true;
                p->generateRandomPatchNow (o);
                for (int k = 0; k < 3; ++k) { const auto n = p->getSampleName (MegaSynthProcessor::breakSlot (k)); if (n.isNotEmpty()) { ++usedBreaks; fromFolder &= brNames.contains (n); } }
                for (int k = 0; k < 3; ++k) { const auto n = p->getSampleName (k); if (n.isNotEmpty()) { ++usedSamples; fromFolder &= smNames.contains (n); } }
            }
            std::cout << "  24 patches: " << usedBreaks << " breaks and " << usedSamples << " samples, all from the folders: " << (fromFolder ? "yes" : "no") << std::endl;
            CHECK (fromFolder && usedBreaks > 0 && usedSamples > 0, "the samples and breaks come from the chosen folders only");
            auto empty = folders.first.getSiblingFile ("Empty"); empty.createDirectory();
            MegaSynthProcessor::setRandomFolder (0, empty); MegaSynthProcessor::rescanRandomFolder (0, true);
            MegaSynthProcessor::setRandomFolder (1, juce::File()); MegaSynthProcessor::rescanRandomFolder (1, true);
            Options o; o.seed = 77; o.category = C_Breaks; o.complexity = 1; o.use[U_Breaks] = true;
            const auto rep = p->generateRandomPatchNow (o);
            CHECK (! rep.startsWith ("Error") && p->getSampleName (MegaSynthProcessor::breakSlot (0)).contains ("synthesised") && rep.contains ("nothing usable"), "an empty breaks folder: the built-in break, and the page says so");
            Options o2; o2.seed = 78; o2.category = C_Pads; o2.complexity = 2; o2.noOsc = true; for (auto& u : o2.use) u = false; o2.use[U_Wavetables] = true; o2.use[U_Effects] = true;
            const auto rep2 = p->generateRandomPatchNow (o2);
            CHECK (! rep2.startsWith ("Error") && p->getSampleName (0) == "Random tone.wav" && rep2.contains ("no samples folder"), "no samples folder: a tone made by the generator, and the page says so");
            MegaSynthProcessor::setRandomFolder (0, folders.first); MegaSynthProcessor::rescanRandomFolder (0, true);
            MegaSynthProcessor::setRandomFolder (1, folders.second); MegaSynthProcessor::rescanRandomFolder (1, true);
        }
        // 6. Undo goes back to the patch before; Keep current puts it in A, the new one in B
        {
            auto p = make(); p->loadFactoryPreset (5);
            const auto before = p->exportBrowserPatch(); const auto nameBefore = p->getPatchName();
            Options o; o.seed = 99; o.category = C_Leads;
            p->generateRandomPatchNow (o);
            CHECK (p->getPatchName() != nameBefore && p->getPatchMeta().author == "Random" && p->getPatchMeta().tags.contains ("random"), "a random patch has its name, author Random and the tag");
            p->undo();
            CHECK (p->exportBrowserPatch() == before, "Undo goes back to the patch before");
            o.keepCurrent = true; o.seed = 100;
            p->generateRandomPatchNow (o);
            const auto made = p->exportBrowserPatch();
            CHECK (p->abSlot() == 1, "Keep current: the new patch is in B");
            p->abSelect (0);
            CHECK (p->exportBrowserPatch() == before, "... and the sound you had is in A");
            p->abSelect (1);
            CHECK (p->exportBrowserPatch() == made, "... and back to B");
        }
        // 7. the page's settings are saved with the project, not in patches
        {
            auto p = make(); p->randomOptions.category = C_Keys; p->randomOptions.noOsc = true; p->randomOptions.keepMask = 5; p->randomOptions.use[U_Reese] = false;
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q->randomOptions.category == C_Keys && q->randomOptions.noOsc && q->randomOptions.keepMask == 5 && ! q->randomOptions.use[U_Reese], "the Random Patch settings come back with the project");
            CHECK (! p->exportBrowserPatch().contains ("randomGen"), "... and aren't in patches");
        }
        // 8. in the background (as the page does it): the report arrives and the patch is loaded
        {
            auto p = make();
            Options o; o.seed = 5150; o.category = C_Reese;
            p->startRandomPatch (o);
            const auto t0 = juce::Time::getMillisecondCounter();
            while (p->randomBusy() && juce::Time::getMillisecondCounter() - t0 < 30000) { while (juce::detail::dispatchNextMessageOnSystemQueue (true)) {} juce::Thread::sleep (5); }
            CHECK (! p->randomBusy() && p->randomReport().startsWith ("Made") && p->getPatchMeta().author == "Random", "in the background: the patch is made and loaded (" + p->randomReport().upToFirstOccurrenceOf ("\n", false, false) + ")");
        }
    };
    // ---- the bank's checks: finite, peak below -1 dBFS, level within 3 dB of -12 dBFS, its information, mono low end
    //      for the basses, macros, unique names, the 36 before it unchanged in place
    auto presetBankTests = [&]
    {
        std::cout << "Jungle / DnB bank" << std::endl;
        const auto& bank = tg::dnbBank();
        CHECK (bank.size() == 100 && factoryPresets().size() == 136, "100 presets after the first 36");
        std::map<juce::String, int> perCat; for (auto& e : bank) ++perCat[e.category];
        CHECK (perCat["Reese"] == 15 && perCat["Sub & Bass"] == 12 && perCat["Neuro"] == 12 && perCat["Hoover & Rave"] == 8 && perCat["Stabs & Chords"] == 10
               && perCat["Pads & Atmos"] == 10 && perCat["Leads"] == 8 && perCat["Breaks & Drums"] == 10 && perCat["FX & Risers"] == 8 && perCat["Keys & Plucks"] == 7,
               "15 Reese, 12 Sub & Bass, 12 Neuro, 8 Hoover & Rave, 10 Stabs & Chords, 10 Pads & Atmos, 8 Leads, 10 Breaks & Drums, 8 FX & Risers, 7 Keys & Plucks");
        std::set<juce::String> names; bool unique = true;
        for (auto& f : factoryPresets()) unique = unique && names.insert (juce::String (f.name).toLowerCase()).second;
        CHECK (unique, "every factory preset's name is unique");
        CHECK (juce::String (factoryPresets()[35].name) == "Break Frankenstein" && juce::String (factoryPresets()[0].name) == "Init Genome", "the first 36 keep their places");
        juce::StringArray bad, loud, quiet, wide, noMacro, notFinite;
        float lo = 0, hi = -100, worstSide = -200;
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            const int idx = tg::kFirstDnb + i;
            const auto m = tg::factoryMeta (idx);
            if (m.category != bank[(size_t) i].category || m.tags.size() < 3 || m.author != "Mega Synth" || m.note.isEmpty()) bad.add (bank[(size_t) i].name);
            tg::PresetBuilder pb; tg::buildFactoryPreset (idx, pb);
            int named = 0; for (auto& n : pb.macroNames) named += n.isNotEmpty() ? 1 : 0;
            if (named < 2) noMacro.add (bank[(size_t) i].name);
            const auto a = auditionOf (idx);
            const float db = 20.0f * std::log10 (std::max (1.0e-9f, a.peak));
            lo = std::min (lo == 0 ? db : lo, db); hi = std::max (hi, db);
            if (! a.finite) notFinite.add (bank[(size_t) i].name);
            if (db > -1.0f) loud.add (juce::String (bank[(size_t) i].name) + " (" + juce::String (db, 1) + ")");
            if (std::abs (db + 12.0f) > 3.0f) quiet.add (juce::String (bank[(size_t) i].name) + " (" + juce::String (db, 1) + ")");
            const bool bassCat = m.category == "Reese" || m.category == "Sub & Bass" || m.category == "Neuro";
            if (bassCat) { worstSide = std::max (worstSide, (float) a.sideDb); if (a.sideDb > -20.0) wide.add (juce::String (bank[(size_t) i].name) + " (" + juce::String (a.sideDb, 1) + " dB)"); }
        }
        std::cout << "  audition peaks " << juce::String (lo, 1) << " to " << juce::String (hi, 1) << " dBFS; basses' side / mid below 100 Hz at most " << juce::String (worstSide, 1) << " dB" << std::endl;
        CHECK (notFinite.isEmpty(), "every preset's output is finite" + (notFinite.isEmpty() ? juce::String() : ": " + notFinite.joinIntoString (", ")));
        CHECK (loud.isEmpty(), "every preset peaks below -1 dBFS" + (loud.isEmpty() ? juce::String() : ": " + loud.joinIntoString (", ")));
        CHECK (quiet.isEmpty(), "every preset within 3 dB of -12 dBFS" + (quiet.isEmpty() ? juce::String() : ": " + quiet.joinIntoString (", ")));
        CHECK (bad.isEmpty(), "every preset has its category, tags, author and note" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")));
        CHECK (noMacro.isEmpty(), "every preset has named macros" + (noMacro.isEmpty() ? juce::String() : ": " + noMacro.joinIntoString (", ")));
        CHECK (wide.isEmpty(), "the basses are mono below 120 Hz" + (wide.isEmpty() ? juce::String() : ": " + wide.joinIntoString (", ")));
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BANKCPU", {}).isNotEmpty())
    {
        // the bank's CPU at each Quality: a 4-note chord held 2 s (best of 2), per category average and the heaviest
        for (int q = 0; q < 3; ++q)
        {
            double total = 0, top = 0; juce::String topName;
            for (int i = 0; i < (int) tg::dnbBank().size(); ++i)
            {
                double best = 1e9;
                for (int run = 0; run < 2; ++run)
                {
                    auto p = make(); p->loadFactoryPreset (tg::kFirstDnb + i); setP (*p, P_quality, (float) q);
                    render (*p, 0.2, {});
                    double cpu = 0; render (*p, 2.0, chord (0.0, 1.8, { 45, 52, 57, 60 }), nullptr, &cpu);
                    best = std::min (best, cpu / 2.0 * 100.0);
                }
                total += best; if (best > top) { top = best; topName = tg::dnbBank()[(size_t) i].name; }
            }
            std::cout << "BANKCPU " << kQualityLabels[q] << ": average " << juce::String (total / 100.0, 2) << "% of a core, heaviest " << topName << " " << juce::String (top, 2) << "%" << std::endl;
        }
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BANKONLY", {}).isNotEmpty())
    {
        presetBankTests();
        std::cout << (failures == 0 ? "BANK TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_CATONLY", {}).isNotEmpty())
    {
        categoryTests();
        std::cout << (failures == 0 ? "CATEGORY TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROWSONLY", {}).isNotEmpty())
    {
        rowTests();
        std::cout << (failures == 0 ? "ROW TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_REACTONLY", {}).isNotEmpty())
    {
        reactTests();
        std::cout << (failures == 0 ? "REACT TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BANKSEQ", {}).isNotEmpty())
    {
        // (the full run's order up to the bank, to check it in that state)
        reeseTests(); reactTests(); rowTests(); coreTests(); categoryTests(); tg::resetVoiceRandom(); presetBankTests();
        std::cout << (failures == 0 ? "BANKSEQ PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_COREREF", {}).isNotEmpty())
    {
        // (a Core-on render with no reverb, written raw, to compare two builds bit for bit)
        juce::MemoryBlock all;
        for (int variant = 0; variant < 4; ++variant)
        {
            auto p = make();
            for (int i : { P_reverbMix, P_shimmerMix, P_reverseMix, P_delayMix, P_chorusMix, P_analogDrift, P_osc1Gain }) setP (*p, i, 0.0f);
            setP (*p, P_cAEngine, 1.0f); p->applyCoreAlgorithm (0, variant * 3); setP (*p, P_cA2M2, 1.3f);
            if (variant & 1) { setP (*p, P_cAUni, 3.0f); setP (*p, P_osc1Gain, 0.5f); }
            if (variant >= 2) { setP (*p, P_cBEngine, 1.0f); setP (*p, P_cB1Rel, 2.0f); setP (*p, P_quality, (float) (variant - 1)); }
            tg::resetVoiceRandom();
            juce::AudioBuffer<float> c; render (*p, 2.0, chord (0.0, 1.2, { 45, 57, 64 }), &c);
            for (int ch = 0; ch < 2; ++ch) all.append (c.getReadPointer (ch), sizeof (float) * (size_t) c.getNumSamples());
        }
        juce::File (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_COREREF", {})).replaceWithData (all.getData(), all.getSize());
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_ROLLDEMO", {}).isNotEmpty())
    {
        // v0.1.71 roll comparison: at 160, 165 and 172 BPM, a bar of the break then a bar ending in a roll: the old Snare Build,
        // then four new Snare Rolls (Accelerate 1 bar; Straight 2 beats; Buzz finish 1 bar into a reverse snare; Accelerate
        // 2 beats with a 1/16 gap). Writes renders/rolls_old_vs_new.wav and renders/rolls_hits.csv (each hit's time and level)
        struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        juce::AudioBuffer<float> all (2, (int) (sr * 120.0)); all.clear();
        int at = 0;
        juce::String csv = "bpm,variant,beat,level,roll\n";
        struct V { const char* name; int len, curve, gap; bool old; };
        const V vars[] = { { "old Snare Build", 2, 1, 0, true }, { "Accelerate, 1 bar", 2, 1, 0, false }, { "Straight, 2 beats", 1, 0, 0, false },
                           { "Buzz finish, 1 bar, reverse snare", 2, 2, 2, false }, { "Accelerate, 2 beats, 1/16 gap", 1, 1, 1, false } };
        for (double bpm : { 160.0, 165.0, 172.0 })
            for (auto& vr : vars)
            {
                auto p = make();
                setP (*p, P_osc1Gain, 0.0f); setP (*p, P_reverbMix, 0.0f); setP (*p, P_delayMix, 0.0f); setP (*p, P_chorusMix, 0.0f);
                setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f); setP (*p, P_bcLevel, 0.8f); setP (*p, P_bcSync, 1.0f); setP (*p, P_bsOn, 1.0f);
                p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "Mega Break.wav", MegaSynthProcessor::kSlotBreak);
                p->refreshBreakAnalysis (true);
                p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 0);
                p->breakStore.edit ([] (tg::BreakData& d) { std::copy (d.step[0], d.step[0] + 32, d.step[1]); tg::BreakStore::setChain (d, "AB"); });
                setP (*p, P_bsChainOn, 1.0f);
                if (vr.old) p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::applyTool (tg::breaktools::ToolSnareBuild, d, 1, 16, 4, 0.7f, 0, false, true); });
                else
                {
                    setP (*p, P_bsRollLen, (float) vr.len); setP (*p, P_bsRollCurve, (float) vr.curve); setP (*p, P_bsRollGap, (float) vr.gap); setP (*p, P_bsRollVar, 0.3f);
                    p->runBreakTool (tg::breaktools::ToolSnareBuild, false, 1);
                }
                const auto d = p->breakStore.get();
                for (int i = 0; i < 16; ++i)
                {
                    const auto& s = d.step[1][i];
                    if (! s.on) continue;
                    const int R = juce::jlimit (1, 8, s.roll);
                    for (int rr = 0; rr < R; ++rr)
                    {
                        const float f = R > 1 ? (float) rr / (float) (R - 1) : 0.0f;
                        const float lvl = s.level * ((1.0f - s.rollRamp) + s.rollRamp * f) * (0.5f + 0.5f * s.vel * (s.accent ? 1.25f : 1.0f));
                        csv << juce::String (bpm, 0) << ",\"" << vr.name << "\"," << juce::String (i * 0.25 + 0.25 * rr / R + s.nudge * 0.25, 4) << "," << juce::String (lvl, 3) << "," << R << "\n";
                    }
                }
                Head head; p->prepareToPlay (sr, 256); p->setPlayHead (&head);
                const double inc = bpm / 60.0 / sr; const int n = (int) (8.0 / inc);
                juce::AudioBuffer<float> buf (2, 256); double ppq = 0.0;
                for (int pos = 0; pos < n && at + pos < all.getNumSamples(); pos += 256)
                {
                    const int m = std::min (256, n - pos);
                    head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                    buf.setSize (2, m, false, false, true); buf.clear(); juce::MidiBuffer mb; p->processBlock (buf, mb);
                    for (int c = 0; c < 2; ++c) all.copyFrom (c, at + pos, buf, c, 0, std::min (m, all.getNumSamples() - at - pos));
                    ppq += m * inc;
                }
                p->setPlayHead (nullptr);
                std::cout << "ROLLDEMO " << juce::String (bpm, 0) << " BPM, " << vr.name << " from " << juce::String (at / sr, 2) << " s" << std::endl;
                at += n;
            }
        all.setSize (2, at, true);
        const float pk = all.getMagnitude (0, at); if (pk > 0.0f) all.applyGain (juce::Decibels::decibelsToGain (-1.0f) / pk);
        writeWav (all, sr, "rolls_old_vs_new.wav");
        juce::File::getCurrentWorkingDirectory().getChildFile ("renders").getChildFile ("rolls_hits.csv").replaceWithText (csv);
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_PADDEMO", {}).isNotEmpty())
    {
        // v0.1.71 demo: 172 BPM, 16 bars. The main patterns switched from learned pads (A A..: A B A D | A B A D | A C A E | A B A D,
        // D and E being A with a roll at its end: Accelerate 2 beats, and a Buzz finish), the layer paired: A -> Layer A (hats),
        // B -> Layer C (a shaker), C -> Layer B, D and E -> Layer A
        auto p = make();
        setP (*p, P_osc1Gain, 0.0f); setP (*p, P_reverbMix, 0.0f); setP (*p, P_delayMix, 0.0f); setP (*p, P_chorusMix, 0.0f);
        setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSync, 1.0f); setP (*p, P_bcLevel, 0.8f);
        p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "Mega Break.wav", MegaSynthProcessor::breakSlot (0));
        p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 165.0, 2), 44100.0), "Mega Break Rollers.wav", MegaSynthProcessor::breakSlot (3));
        setP (*p, P_bcSliceMode, 3.0f); setP (*p, P_bcSliceMode4, 3.0f); p->refreshBreakAnalysis (true);
        setP (*p, P_bcActive, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bsSwitch, 2.0f); setP (*p, P_bsLOn, 1.0f); setP (*p, P_bsLLevel, 0.55f); setP (*p, P_mxLayerPan, 0.2f);
        p->breakStore.edit ([] (tg::BreakData& d) { for (int q = 0; q < 5; ++q) d.patBreak[q] = 1; });
        for (int q = 0; q < 5; ++q) p->runBreakTool (MegaSynthProcessor::BtOriginal, false, q);
        p->runBreakTool (MegaSynthProcessor::BtJungle, false, 1);
        p->runBreakTool (tg::breaktools::ToolTwoStep, false, 2);
        setP (*p, P_bsRollLen, 1.0f); setP (*p, P_bsRollCurve, 1.0f); setP (*p, P_bsRollCresc, 0.8f); p->runBreakTool (tg::breaktools::ToolSnareBuild, false, 3);
        setP (*p, P_bsRollCurve, 2.0f); p->runBreakTool (tg::breaktools::ToolSnareBuild, false, 4);
        p->breakStore.edit ([] (tg::BreakData& d)
        {
            const auto hats = tg::breaktools::topSlices (d.brk[3], d.brk[3].hat, 2);
            const int h0 = hats[0], h1 = hats.size() > 1 ? hats[1] : hats[0];
            for (int q = 0; q < 3; ++q) d.lpatBreak[q] = 4;
            for (int i = 0; i < 16; ++i)
            {
                d.lstep[0][i] = {}; d.lstep[1][i] = {}; d.lstep[2][i] = {};
                if (i % 4 == 2) { d.lstep[0][i].on = true; d.lstep[0][i].slice = h0; d.lstep[0][i].vel = 0.9f; }                       // Layer A: hats on the off-beat 8ths
                d.lstep[1][i].on = (i % 2) == 1; d.lstep[1][i].slice = h1; d.lstep[1][i].vel = 0.6f;                                     // Layer B: 16th off-beats
                d.lstep[2][i].on = true; d.lstep[2][i].slice = (i % 2) ? h1 : h0; d.lstep[2][i].vel = (i % 4 == 2) ? 0.85f : 0.4f;      // Layer C: a shaker
                d.lstep[2][i].level = (i % 4 == 2) ? 1.0f : 0.6f;
            }
            d.lpair[0] = 1; d.lpair[1] = 3; d.lpair[2] = 2; d.lpair[3] = 1; d.lpair[4] = 1;
        });
        // the pads: A-E on notes 36-40, learned as a controller would send them
        p->patPadLearnRun = true; p->patPadLearn = 0;
        for (int k = 0; k < 5; ++k) { juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (10, 36 + k, (juce::uint8) 100), 0); p->processBlock (b, m); }
        p->patPadLearn = -1; p->patPadLearnRun = false;
        const char order[] = "ABADABADACAEABAD";
        struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        Head head; p->prepareToPlay (sr, 256); p->setPlayHead (&head);
        const double bpm = 172.0, inc = bpm / 60.0 / sr;
        const int total = (int) (64.0 / inc) + (int) (1.5 * sr);
        juce::AudioBuffer<float> out (2, total), buf (2, 256);
        std::vector<std::pair<int, juce::MidiMessage>> ev;
        for (int b = 1; b < 16; ++b)
        {
            // each bar's pattern pressed on beat 3 of the bar before (Switch: next bar), as you'd hit the pad
            const int pad = 36 + (order[b] - 'A');
            ev.push_back ({ (int) (((b - 1) * 4.0 + 2.0) / inc), juce::MidiMessage::noteOn (10, pad, (juce::uint8) 110) });
            ev.push_back ({ (int) (((b - 1) * 4.0 + 2.3) / inc), juce::MidiMessage::noteOff (10, pad) });
        }
        std::vector<MegaSynthProcessor::BsLogEntry> log; p->bsLog = &log;
        double ppq = 0.0;
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            head.info.setIsPlaying (ppq < 64.0); head.info.setBpm (bpm); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            juce::MidiBuffer m; for (auto& e : ev) if (e.first >= pos && e.first < pos + n) m.addEvent (e.second, e.first - pos);
            buf.setSize (2, n, false, false, true); buf.clear(); p->processBlock (buf, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
            ppq += n * inc;
        }
        p->setPlayHead (nullptr); p->bsLog = nullptr;
        juce::String bars;
        for (int b = 0; b < 16; ++b)
        {
            char mp = '-', lp = '-';
            for (auto& e : log) if (e.step / 16 == b) { if (e.layer) { if (e.pattern >= 0) lp = (char) ('A' + e.pattern); } else mp = (char) ('A' + e.pattern); }
            bars << " " << (b + 1) << ":" << juce::String::charToString (mp) << "/" << juce::String::charToString (lp);
        }
        const float pk = out.getMagnitude (0, total); if (pk > 0.0f) out.applyGain (juce::Decibels::decibelsToGain (-1.0f) / pk);
        writeWav (out, sr, "pads_pairs_rolls_172.wav");
        std::cout << "PADDEMO bars (main/layer):" << bars << std::endl;
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_SPACEDEMO", {}).isNotEmpty())
    {
        // v0.1.70 reel: 172 BPM, 16 bars over the demo break.
        //  bars 1-4   Talking Neuro Reese with Detune Bloom, the IR Hall ducking under the break's kicks
        //  bars 5-8   Split Distort with a comb sweeping each bar
        //  bars 9-12  Hoover stabs through the Spring, a Dub Throw on the last beat of each bar
        //  bars 13-16 the Gated Reverb on the break's snares; a plate chord frozen into a pad (bar 15), the break stopped
        // "dry": the same notes and break with the new effects and Reese additions left off (the plain Reese)
        const bool wet = juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_SPACEDEMO", {}) != "dry";
        auto p = make();
        setP (*p, P_osc1Gain, 0.0f); setP (*p, P_ampA, 0.004f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.12f); setP (*p, P_polyphony, 4.0f);
        setP (*p, P_reverbMix, 0.0f); setP (*p, P_delayMix, 0.0f); setP (*p, P_chorusMix, 0.0f);
        setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f); setP (*p, P_bcLevel, 0.75f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcSliceKeys, 2.0f); setP (*p, P_bcSync, 1.0f);
        setP (*p, P_bcKeyBase, 96.0f);
        p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "Mega Break.wav", MegaSynthProcessor::kSlotBreak);
        p->refreshBreakAnalysis (true);
        setP (*p, P_bsSteps, 1.0f);
        p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 32); });
        struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        Head head; p->prepareToPlay (sr, 256); p->setPlayHead (&head);
        auto reese = [&] (int preset) { p->applyReesePreset (wet ? preset : 0); setP (*p, P_rsLevel, 0.6f); };
        // set up the IR hall first (its impulse loads in the background)
        if (wet)
        {
            setP (*p, P_sxOnIr, 1.0f); setP (*p, P_sxIrType, 2.0f); setP (*p, P_sxMixIr, 0.45f); setP (*p, P_sxIrHP, 180.0f);
            setP (*p, sxDuckParam (SD_Ir, SDF_Amt), 0.8f); setP (*p, sxDuckParam (SD_Ir, SDF_Src), 0.0f); setP (*p, sxDuckParam (SD_Ir, SDF_Att), 2.0f); setP (*p, sxDuckParam (SD_Ir, SDF_Rel), 300.0f);
            p->updateIrImpulse (true);
        }
        { juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; head.info.setIsPlaying (false); p->processBlock (b, m); juce::Thread::sleep (400); p->processBlock (b, m); }
        reese (1); if (wet) setP (*p, P_rsBloom, 1.0f);
        const double bpm = 172.0, inc = bpm / 60.0 / sr;
        const int bars = 16, total = (int) (bars * 4.0 / inc) + (int) (2.5 * sr);
        juce::AudioBuffer<float> out (2, total), buf (2, 256);
        const int bassNotes[4] = { 33, 33, 29, 31 };   // A1, A1, F1, G1
        double ppq = 0.0; int lastBar = -1, lastBeat = -1;
        std::vector<int> held;
        auto allOff = [&] (juce::MidiBuffer& m) { for (int nn : held) m.addEvent (juce::MidiMessage::noteOff (1, nn), 0); held.clear(); };
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            const bool playing = ppq < bars * 4.0;
            const int bar = (int) std::floor (ppq / 4.0), beat = (int) std::floor (ppq);
            juce::MidiBuffer m;
            if (playing && bar != lastBar)
            {
                lastBar = bar;
                if (bar == 4) { reese (3); if (wet) { setP (*p, P_rsCpMode, 1.0f); setP (*p, P_rsCpSync, 4.0f); setP (*p, P_rsCpDepth, 0.8f); setP (*p, P_rsCpFb, 0.7f); setP (*p, P_rsCpMix, 0.55f); } }
                if (bar == 8)
                {
                    reese (4);
                    if (wet)
                    {
                        setP (*p, sxDuckParam (SD_Ir, SDF_Amt), 0.0f); setP (*p, P_sxMixIr, 0.2f);
                        setP (*p, P_sxOnSpring, 1.0f); setP (*p, P_sxMixSpring, 0.45f); setP (*p, P_sxSprTension, 0.7f); setP (*p, P_sxSprDecay, 2.2f);
                        setP (*p, P_sxOnDub, 1.0f); setP (*p, P_sxMixDub, 0.6f); setP (*p, P_sxDubMode, 0.0f); setP (*p, P_sxDubSync, 6.0f); setP (*p, P_sxDubFb, 0.7f);
                        setP (*p, P_sxDubHP, 300.0f); setP (*p, P_sxDubLP, 2600.0f); setP (*p, P_sxDubDrive, 0.4f);
                    }
                }
                if (bar == 12)
                {
                    reese (0); setP (*p, P_rsLevel, 0.0f);
                    if (wet)
                    {
                        setP (*p, P_sxOnSpring, 0.0f); setP (*p, P_sxOnIr, 0.0f);
                        setP (*p, P_sxOnGated, 1.0f); setP (*p, P_sxMixGated, 0.55f); setP (*p, P_sxGateTrig, 2.0f); setP (*p, P_sxGateSync, 3.0f); setP (*p, P_sxGateSize, 0.85f);
                        setP (*p, P_sxOnPlate, 1.0f); setP (*p, P_sxMixPlate, 0.5f); setP (*p, P_sxPlDecay, 6.0f); setP (*p, P_sxPlMod, 0.5f);
                    }
                    // a chord for the pad (the SuperSaw)
                    setP (*p, P_supersawGain, 0.1f); setP (*p, P_ampA, 0.02f); setP (*p, P_ampR, 0.3f);
                }
                if (bar == 14) { if (wet) setP (*p, P_sxPlFreeze, 1.0f); setP (*p, P_bsOn, 0.0f); }
                std::cout << "SPACEDEMO bar " << (bar + 1) << std::endl;
            }
            if (playing && beat != lastBeat)
            {
                lastBeat = beat;
                const int bib = beat % 4;
                if (bar < 8)
                {
                    if (bib == 0) { allOff (m); const int nn = bassNotes[bar % 4]; m.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 100), 1); held.push_back (nn); }
                }
                else if (bar < 12)
                {
                    // Hoover stabs on 1, the "and" of 2 (here on 2) and 3; Throw held over beat 4
                    allOff (m);
                    if (bib != 3) { const int nn = (bar % 2 == 0) ? 45 : 43; m.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 110), 1); held.push_back (nn); }
                    if (wet) setP (*p, P_sxDubThrow, bib == 2 ? 1.0f : 0.0f);   // (the last stab goes into the delay)
                }
                else if (bar == 12 && bib == 0)
                {
                    allOff (m);
                    for (int nn : { 57, 60, 64, 69 }) { m.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 90), 1); held.push_back (nn); }
                }
                else if (bar == 14 && bib == 0) allOff (m);
            }
            if (! playing && ! held.empty()) allOff (m);
            head.info.setIsPlaying (playing); head.info.setBpm (bpm); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            buf.setSize (2, n, false, false, true); buf.clear(); p->processBlock (buf, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
            ppq += n * inc;
            if (! playing && wet && ppq > bars * 4.0 + 2.0) setP (*p, P_sxPlFreeze, 0.0f);   // (let the frozen pad go at the end)
        }
        p->setPlayHead (nullptr);
        const float pk = out.getMagnitude (0, total);
        if (pk > 0.0f) out.applyGain (juce::Decibels::decibelsToGain (-1.0f) / pk);
        writeWav (out, sr, wet ? "space_reese_demo_172.wav" : "space_reese_demo_172_dry.wav");
        std::cout << "SPACEDEMO written, " << juce::String (total / sr, 1) << " s, peak was " << juce::String (juce::Decibels::gainToDecibels (pk), 1) << " dBFS (normalised to -1)" << std::endl;
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RXDEMO", {}).isNotEmpty())
    {
        // Break React reel: 172 BPM, the demo break from the Break Sequencer, a held Reese + sub bass played in the slice zone
        // (Slice Keys Auto), four passes of 5 bars: Lock bass to kicks, Snare stabs, Rolling Reese, all three together
        auto p = make();
        setP (*p, P_osc1Gain, 0.0f); setP (*p, P_subGain, 0.45f); setP (*p, P_subWave, 0.0f);
        setP (*p, P_rsLevel, 0.45f); setP (*p, P_rsVoices, 1.0f); setP (*p, P_rsNotch, 0.3f);
        setP (*p, P_filterCutoff, 1400.0f); setP (*p, P_filterRes, 2.0f); setP (*p, P_ampA, 0.004f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.08f);
        setP (*p, P_fEnvA, 0.002f); setP (*p, P_fEnvD, 0.25f); setP (*p, P_fEnvS, 0.2f); setP (*p, P_fEnvAmt, 1200.0f);
        setP (*p, P_reverbMix, 0.08f); setP (*p, P_delayMix, 0.0f); setP (*p, P_chorusMix, 0.0f); setP (*p, P_polyphony, 1.0f);
        setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSliceMode, 3.0f); setP (*p, P_bcLevel, 0.8f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bcSliceKeys, 2.0f); setP (*p, P_bcSync, 1.0f);
        p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "Mega Break.wav", MegaSynthProcessor::kSlotBreak);
        p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 32); });
        setP (*p, P_bsSteps, 1.0f);
        struct Head : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        Head head; p->prepareToPlay (sr, 256); p->setPlayHead (&head);
        const double bpm = 172.0, inc = bpm / 60.0 / sr;
        const int barsPerPass = 5, total = (int) (4 * barsPerPass * 4.0 / inc);
        juce::AudioBuffer<float> out (2, total), buf (2, 256);
        const int bass[] = { 38, 41, 43, 36, 38 };   // D2, F2, G2, C2, D2: all in the slice zone, played as the bass
        double ppq = 0.0; int heldNote = -1;
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            const int bar = (int) std::floor (ppq / 4.0), pass = std::min (3, bar / barsPerPass);
            if (bar % barsPerPass == 0 && std::fmod (ppq, 4.0) < 256 * inc)
            {
                switch (pass)
                {
                    case 0: p->applyReactPreset (1); break;
                    case 1: p->applyReactPreset (2); break;
                    case 2: p->applyReactPreset (3); break;
                    default:
                        p->applyReactPreset (1);
                        setP (*p, P_rxSOn, 1.0f); setP (*p, P_rxSReset, 0.0f); setP (*p, P_rxSCut, 2.0f); setP (*p, P_rxSRes, 0.3f); setP (*p, P_rxSRetrig, 2.0f); setP (*p, P_rxSVel, 0.5f);
                        setP (*p, P_rxHOn, 1.0f); setP (*p, P_rxHReset, 0.0f); setP (*p, P_rxHNotch, 0.6f); setP (*p, P_rxHVel, 0.5f);
                        break;
                }
                if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RXDEMO", {}) == "off") setP (*p, P_rxOn, 0.0f);   // (the same without the reactions, to compare)
                std::cout << "RXDEMO pass " << (pass + 1) << " from " << juce::String (pos / sr, 2) << " s" << std::endl;
            }
            head.info.setIsPlaying (true); head.info.setBpm (bpm); head.info.setPpqPosition (ppq); head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            juce::MidiBuffer m;
            const int want = bass[bar % 5];
            if (want != heldNote && std::fmod (ppq, 4.0) < 256 * inc)
            {
                if (heldNote >= 0) m.addEvent (juce::MidiMessage::noteOff (1, heldNote), 0);
                m.addEvent (juce::MidiMessage::noteOn (1, want, (juce::uint8) 100), 1);
                heldNote = want;
            }
            buf.setSize (2, n, false, false, true); buf.clear(); p->processBlock (buf, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
            ppq += n * inc;
        }
        p->setPlayHead (nullptr);
        const float pk = out.getMagnitude (0, total);
        if (pk > 0.0f && juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RXDEMO", {}) != "off") out.applyGain (juce::Decibels::decibelsToGain (-3.0f) / pk);
        writeWav (out, sr, juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RXDEMO", {}) == "off" ? "break_react_reel_172_off.wav" : "break_react_reel_172.wav");
        std::cout << "RXDEMO written: renders/break_react_reel_172.wav, " << juce::String (total / sr, 1) << " s, peak was " << juce::String (juce::Decibels::gainToDecibels (pk), 1)
                  << " dBFS (normalised to -3)" << std::endl;
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMDEMO", {}).isNotEmpty())
    {
        // eight random patches (two each of Reese, Neuro, Pads and Breaks & Drums) as one audition reel at 172 BPM,
        // the breaks from a test folder of the synth's own demo breaks
        using namespace tg::randompatch;
        const auto folders = makeRandomFolders();
        MegaSynthProcessor::setRandomFolder (0, folders.first); MegaSynthProcessor::rescanRandomFolder (0, true);
        MegaSynthProcessor::setRandomFolder (1, folders.second); MegaSynthProcessor::rescanRandomFolder (1, true);
        const int cats[8] = { C_Reese, C_Reese, C_Neuro, C_Neuro, C_Pads, C_Pads, C_Breaks, C_Breaks };
        const uint32_t seeds[8] = { 11, 23, 37, 41, 53, 67, 71, 89 };
        std::vector<float> L, R;
        auto p = make();
        for (int k = 0; k < 8; ++k)
        {
            Options o; o.seed = seeds[k]; o.category = cats[k]; o.complexity = k % 2 == 0 ? 1 : 2; o.use[U_Breaks] = cats[k] == C_Breaks;
            const auto rep = p->generateRandomPatchNow (o);
            juce::AudioBuffer<float> c; tg::resetVoiceRandom();
            const double pk = phraseOf (*p, categoryName (cats[k]), &c);
            std::cout << "REEL " << (k + 1) << ": " << rep.upToFirstOccurrenceOf ("\n", false, false) << " peak " << juce::String (pk, 1) << " dBFS" << std::endl;
            for (int i = 0; i < c.getNumSamples(); ++i) { L.push_back (c.getSample (0, i)); R.push_back (c.getSample (1, i)); }
            for (int i = 0; i < (int) (0.3 * sr); ++i) { L.push_back (0.0f); R.push_back (0.0f); }
        }
        juce::AudioBuffer<float> all (2, (int) L.size());
        for (int i = 0; i < (int) L.size(); ++i) { all.setSample (0, i, L[(size_t) i]); all.setSample (1, i, R[(size_t) i]); }
        writeWav (all, sr, "random_patch_reel_172.wav");
        std::cout << "REEL written: renders/random_patch_reel_172.wav" << std::endl;
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMONLY", {}).isNotEmpty())
    {
        randomTests (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMONLY", {}).getIntValue() > 1 ? juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_RANDOMONLY", {}).getIntValue() : 300);
        std::cout << (failures == 0 ? "RANDOM TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_KEYENTRYONLY", {}).isNotEmpty())
    {
        keyEntryTests();
        std::cout << (failures == 0 ? "KEY ENTRY TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_COREONLY", {}).isNotEmpty())
    {
        coreTests();
        std::cout << (failures == 0 ? "CORE TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_REESEONLY", {}).isNotEmpty())
    {
        reeseTests();
        std::cout << (failures == 0 ? "REESE TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BREAKDEMOS", {}).isNotEmpty())
    {
        // three 8-bar demos at 172 BPM (renders/break_demo_*.wav)
        struct FakeHead : juce::AudioPlayHead { PositionInfo info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders"); dir.createDirectory();
        const double bpm = 172.0, inc = bpm / 60.0 / sr;
        const int total = (int) std::ceil (32.0 / inc) + (int) (0.5 * sr);
        std::function<void (double)> onBeat;   // (demo 7: changes settings as it plays)
        auto renderTo = [&] (MegaSynthProcessor& p, const juce::String& name, std::vector<std::pair<int, juce::MidiMessage>> ev)
        {
            FakeHead head; p.setPlayHead (&head);
            juce::AudioBuffer<float> all (2, total), buf (2, 256);
            double ppq = 0.0;
            for (int pos = 0; pos < total; pos += 256)
            {
                const int n = std::min (256, total - pos);
                buf.setSize (2, n, false, false, true);
                head.info.setIsPlaying (pos < (int) (32.0 / inc)); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                juce::MidiBuffer m; for (auto& e : ev) if (e.first >= pos && e.first < pos + n) m.addEvent (e.second, e.first - pos);
                if (onBeat) onBeat (ppq);
                buf.clear(); p.processBlock (buf, m); ppq += n * inc;
                for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, buf, c, 0, n);
            }
            p.setPlayHead (nullptr);
            auto f = dir.getChildFile (name); f.deleteFile();
            std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
            if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (all, 0, total); }
            std::cout << "DEMO " << f.getFullPathName() << " peak " << all.getMagnitude (0, total) << std::endl;
        };
        // 10. the Core oscillator over the demo break: bars 1-4 Core A alone (a Kernel FM bass: a 1:1 pair with a
        //     decaying index and feedback on the modulator, plus a sine an octave below), bars 5-8 Core B joins
        //     (a metallic 1:3.5 pluck layer, three unison voices spread) and Core A gets brighter
        auto coreDemo = [&]
        {
            auto p = make(); p->loadFactoryPreset (32);
            for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_supersawGain, P_complexGain }) setP (*p, i, 0.0f);
            setP (*p, P_ampA, 0.002f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.08f); setP (*p, P_filterCutoff, 20000.0f);
            setP (*p, P_bcKeyBase, 96.0f); setP (*p, P_quality, 2.0f);
            auto op = [&] (int s, int k, int w, float v) { setP (*p, coreOpParam (s, k, w), v); };
            setP (*p, P_cAEngine, 1.0f); setP (*p, P_cALevel, 0.55f); setP (*p, P_cAOct, -1.0f);
            op (0, 1, CO_Level, 1.0f); op (0, 1, CO_M1, 2.5f); op (0, 1, CO_M1 + 1, 0.9f); op (0, 1, CO_Dec, 0.35f); op (0, 1, CO_Sus, 0.25f);
            op (0, 2, CO_Ratio, 0.5f); op (0, 2, CO_Level, 0.7f); op (0, 2, CO_Out, 0.5f);
            setP (*p, P_cBLevel, 0.5f); setP (*p, P_cBUni, 2.0f); setP (*p, P_cBDetune, 8.0f); setP (*p, P_cBSpread, 0.8f);
            op (1, 0, CO_Dec, 0.2f); op (1, 0, CO_Sus, 0.0f);
            op (1, 1, CO_Ratio, 3.5f); op (1, 1, CO_Level, 1.0f); op (1, 1, CO_M1, 4.0f); op (1, 1, CO_Dec, 0.12f); op (1, 1, CO_Sus, 0.0f);
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            const int notes[] = { 45, 45, 41, 43, 45, 45, 48, 43 };
            for (int b = 0; b < 8; ++b)
                for (double off : { 0.0, 1.5, 3.0 })   // (three hits a bar: the bass rhythm)
                {
                    ev.push_back ({ (int) ((b * 4.0 + off) / inc), juce::MidiMessage::noteOn (1, notes[b], (juce::uint8) 100) });
                    ev.push_back ({ (int) ((b * 4.0 + off + (off < 3.0 ? 1.25 : 0.85)) / inc), juce::MidiMessage::noteOff (1, notes[b]) });
                }
            bool switched = false;
            onBeat = [&] (double beat) { if (! switched && beat >= 16.0 - 0.02) { switched = true; setP (*p, P_cBEngine, 1.0f); setP (*p, P_cABright, 1.6f); } };
            renderTo (*p, "core_demo_kernel_fm_172.wav", ev);
            onBeat = nullptr;
        };
        if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BREAKDEMOS", {}) == "core") { coreDemo(); return 0; }
        // 11. v0.1.69: the main sequencer on Break 1 (chain A-B-A-C), the layer on Break 4's hats (following the main), and
        //     the fill written in Pattern H on Break 3 (a snare build), called every 4 bars by Auto Fill and once by the Fill pad
        auto layerDemo = [&]
        {
            auto p = make();
            for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_supersawGain, P_complexGain }) setP (*p, i, 0.0f);
            setP (*p, P_reverbMix, 0.0f); setP (*p, P_delayMix, 0.0f);
            setP (*p, P_bcOn, 1.0f); setP (*p, P_bcSync, 1.0f);
            const int vars[4] = { 0, 1, 1, 2 }; const double bpms[4] = { 170.0, 174.0, 174.0, 165.0 };
            for (int k : { 0, 2, 3 })
            {
                p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, bpms[k], vars[k]), 44100.0), "Mega Break " + juce::String (k + 1) + " (synthesised).wav", MegaSynthProcessor::breakSlot (k));
                setP (*p, tg::breakParams (k).sliceMode, 3.0f);
            }
            p->refreshBreakAnalysis (true);
            setP (*p, P_bcActive, 1.0f); setP (*p, P_bsOn, 1.0f); setP (*p, P_bsSteps, 0.0f); setP (*p, P_bsChainOn, 1.0f);
            setP (*p, P_bsLOn, 1.0f); setP (*p, P_bsLFollow, 0.0f); setP (*p, P_bsLLevel, 0.6f); setP (*p, P_mxLayerPan, 0.25f); setP (*p, P_bsLSwing, 0.12f);
            setP (*p, P_bsFillPat, 8.0f); setP (*p, P_bsFillSize, 3.0f); setP (*p, P_bsFillStart, 1.0f); setP (*p, P_bsAutoFill, 1.0f); setP (*p, P_bsAutoFillProb, 1.0f);
            p->fillPadKind = 1; p->fillPadNum = 36;
            p->breakStore.edit ([] (tg::BreakData& d) { d.patBreak[0] = d.patBreak[1] = d.patBreak[2] = 1; d.patBreak[7] = 3; for (int q = 0; q < 3; ++q) d.lpatBreak[q] = 4; });
            p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 0);
            p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 1); p->runBreakTool (MegaSynthProcessor::BtJungle, false, 1);
            p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 2); p->runBreakTool (tg::breaktools::ToolTwoStep, false, 2);
            p->runBreakTool (MegaSynthProcessor::BtOriginal, false, 7); p->runBreakTool (tg::breaktools::ToolSnareBuild, false, 7);
            p->breakStore.edit ([] (tg::BreakData& d)
            {
                tg::BreakStore::setChain (d, "ABAC");
                // the layer: Break 4's two most hat-like slices - off-beat 8ths on A, 16ths on B, a shaker-ish 16th pattern on C
                const auto hats = tg::breaktools::topSlices (d.brk[3], d.brk[3].hat, 2);
                const int h0 = hats[0], h1 = hats.size() > 1 ? hats[1] : hats[0];
                for (int i = 0; i < 16; ++i)
                {
                    d.lstep[0][i] = {}; d.lstep[1][i] = {}; d.lstep[2][i] = {};
                    if (i % 4 == 2) { d.lstep[0][i].on = true; d.lstep[0][i].slice = h0; d.lstep[0][i].vel = 0.85f; }
                    d.lstep[1][i].on = true; d.lstep[1][i].slice = (i % 2) ? h1 : h0; d.lstep[1][i].vel = (i % 4 == 2) ? 0.85f : 0.45f;
                    d.lstep[2][i].on = (i % 4) != 0; d.lstep[2][i].slice = h1; d.lstep[2][i].vel = (i % 4 == 2) ? 0.8f : 0.4f;
                }
            });
            // the Fill pad pressed in bar 6 (beat 21.3): the fill plays bar 7 (Start: next bar); Auto Fill plays its own on bar 8 (the phrase end)
            const double bars = 16.0, beats = bars * 4.0;
            std::vector<std::pair<int, juce::MidiMessage>> ev = { { (int) (21.3 / inc), juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110) },
                                                                  { (int) (21.6 / inc), juce::MidiMessage::noteOff (1, 36) } };
            std::vector<MegaSynthProcessor::BsLogEntry> log; p->bsLog = &log;
            FakeHead head; p->setPlayHead (&head);
            const int n = (int) std::ceil (beats / inc) + (int) (0.5 * sr);
            juce::AudioBuffer<float> all (2, n), buf (2, 256);
            double ppq = 0.0;
            for (int pos = 0; pos < n; pos += 256)
            {
                const int m = std::min (256, n - pos);
                buf.setSize (2, m, false, false, true);
                head.info.setIsPlaying (pos < (int) (beats / inc)); head.info.setBpm (bpm); head.info.setPpqPosition (ppq);
                head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                juce::MidiBuffer mb; for (auto& e : ev) if (e.first >= pos && e.first < pos + m) mb.addEvent (e.second, e.first - pos);
                buf.clear(); p->processBlock (buf, mb); ppq += m * inc;
                for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, buf, c, 0, m);
            }
            p->setPlayHead (nullptr); p->bsLog = nullptr;
            const float pk = all.getMagnitude (0, n);
            if (pk > 0.0f) all.applyGain (juce::Decibels::decibelsToGain (-1.0f) / pk);
            auto f = dir.getChildFile ("break_demo_11_layer_and_fill_172.wav"); f.deleteFile();
            std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
            if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (all, 0, n); }
            // what played, bar by bar: the main's pattern (F: the fill) and the layer's
            juce::String bar;
            for (int b = 0; b < (int) bars; ++b)
            {
                char mainP = '-', layP = '-'; bool fill = false; int lh = 0;
                for (auto& e : log)
                {
                    if (e.step / 16 != b) continue;
                    if (e.layer) { layP = (char) ('A' + e.pattern); lh += e.fired ? 1 : 0; }
                    else { if (e.fill) fill = true; else mainP = (char) ('A' + e.pattern); }
                }
                bar << " " << (b + 1) << ":" << (fill ? juce::String ("H(fill)") : juce::String::charToString (mainP)) << "/" << juce::String::charToString (layP) << lh;
            }
            std::cout << "DEMO " << f.getFullPathName() << " (peak was " << pk << ")\n  bars (main/layer + layer hits):" << bar << std::endl;
        };
        if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BREAKDEMOS", {}) == "layer") { layerDemo(); return 0; }
        const int slot = MegaSynthProcessor::kSlotBreak;
        // 1. the break as it is: pattern A (Original) for 8 bars, Akai Cyclic, 170 -> 172 BPM
        {
            auto p = make(); p->loadFactoryPreset (32);
            setP (*p, P_bsChainOn, 0.0f); setP (*p, P_bsPattern, 0.0f); setP (*p, P_bcColour, 0.0f);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::BreakStore::setChain (d, ""); });
            renderTo (*p, "break_demo_1_original_172.wav", {});
        }
        // 2. a 140 BPM version of the break, Akai Cyclic-stretched up to 172 (x1.23), S950 colour
        {
            auto p = make(); p->loadFactoryPreset (32);
            p->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 140.0, 0), 44100.0), "Mega Break 140 (synthesised).wav", slot);
            p->refreshBreakAnalysis (true);
            p->breakStore.edit ([] (tg::BreakData& d) { tg::breaktools::original (d, 0, 32); tg::breaktools::jungle (d, 1, 32, 7); tg::BreakStore::setChain (d, "AAAB"); });
            setP (*p, P_bsChainOn, 1.0f); setP (*p, P_bcStretch, 1.0f); setP (*p, P_bcColour, 2.0f); setP (*p, P_bcCycle, 40.0f);
            renderTo (*p, "break_demo_2_akai_cyclic_140_to_172.wav", {});
        }
        // 3. a rearrange: pattern keys pressed mid-bar switch A -> B -> C -> D on the next bar (2 bars each)
        {
            auto p = make(); p->loadFactoryPreset (32);
            setP (*p, P_bsPattern, 0.0f); setP (*p, P_bsSwitch, 2.0f);
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            for (int k = 1; k < 4; ++k)
            {
                const int at = (int) ((k * 8.0 - 1.5) / inc);   // a beat and a half before the switch
                ev.push_back ({ at, juce::MidiMessage::noteOn (1, 24 + k, (juce::uint8) 100) });
                ev.push_back ({ at + 2000, juce::MidiMessage::noteOff (1, 24 + k) });
            }
            // and the sub under it
            for (int b = 0; b < 8; ++b) { ev.push_back ({ (int) (b * 4.0 / inc), juce::MidiMessage::noteOn (1, b % 4 == 3 ? 31 + 12 : 33 + 12, (juce::uint8) 90) });
                                          ev.push_back ({ (int) ((b * 4.0 + 3.5) / inc), juce::MidiMessage::noteOff (1, b % 4 == 3 ? 31 + 12 : 33 + 12) }); }
            renderTo (*p, "break_demo_3_pattern_switch_172.wav", ev);
        }
        // 4. three breaks: Break Frankenstein's chain (original, Frankenbreak edits), the Active Break moved
        //    by the Break Select keys every two bars (on the beat), and Alternate on its "Active" steps
        {
            auto p = make(); p->loadFactoryPreset (35);
            setP (*p, P_bcSelQuant, 1.0f);
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            const int sel[] = { 33, 34, 35, 33 };
            for (int k = 1; k < 4; ++k)
            {
                const int at = (int) ((k * 8.0 - 0.6) / inc);
                ev.push_back ({ at, juce::MidiMessage::noteOn (1, sel[k], (juce::uint8) 100) });
                ev.push_back ({ at + 1000, juce::MidiMessage::noteOff (1, sel[k]) });
            }
            for (int b = 0; b < 8; ++b) { ev.push_back ({ (int) (b * 4.0 / inc), juce::MidiMessage::noteOn (1, b % 4 == 3 ? 43 : 45, (juce::uint8) 90) });
                                          ev.push_back ({ (int) ((b * 4.0 + 3.5) / inc), juce::MidiMessage::noteOff (1, b % 4 == 3 ? 43 : 45) }); }
            renderTo (*p, "break_demo_4_three_breaks_172.wav", ev);
        }
        // 6. routing: a jungle loop and a sine sub under a big delay and reverb; (a) everything through the
        //    effects, (b) the sub dry (mono below 120 Hz) and the break skipping the effects
        for (int dryVersion = 0; dryVersion < 2; ++dryVersion)
        {
            auto p = make(); p->loadFactoryPreset (32);
            for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain }) setP (*p, i, 0.0f);
            setP (*p, P_subGain, 0.8f); setP (*p, P_subWave, 1.0f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.08f);
            setP (*p, P_delayMix, 0.55f); setP (*p, P_delayFeedback, 0.6f); setP (*p, P_reverbMix, 0.7f);
            setP (*p, P_subRoute, dryVersion == 1 ? 1.0f : 0.0f); setP (*p, P_subMono, dryVersion == 1 ? 1.0f : 0.0f);
            setP (*p, P_bcFxRoute, dryVersion == 1 ? 1.0f : 0.0f);
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            const int notes[] = { 45, 45, 43, 41, 45, 45, 48, 43 };   // (the sub sounds an octave lower)
            for (int b = 0; b < 8; ++b) { ev.push_back ({ (int) (b * 4.0 / inc), juce::MidiMessage::noteOn (1, notes[b], (juce::uint8) 100) });
                                          ev.push_back ({ (int) ((b * 4.0 + 2.5) / inc), juce::MidiMessage::noteOff (1, notes[b]) }); }
            renderTo (*p, dryVersion == 1 ? "break_demo_6b_sub_and_break_dry_172.wav" : "break_demo_6a_all_through_effects_172.wav", ev);
        }
        // 7. Akai Stretch on the whole mix: bars 1-2 dry, 3 Crunch (+5 st), 4 Slow x0.5, 5 Fast x2, 6 Halftime, 7 Crunch (-5 st), 8 dry
        {
            auto p = make(); p->loadFactoryPreset (32);
            setP (*p, P_pfQuant, 5.0f);   // (each change waits for the bar)
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            for (int b = 0; b < 8; ++b) { ev.push_back ({ (int) (b * 4.0 / inc), juce::MidiMessage::noteOn (1, b % 4 == 3 ? 43 : 45, (juce::uint8) 90) });
                                          ev.push_back ({ (int) ((b * 4.0 + 3.5) / inc), juce::MidiMessage::noteOff (1, b % 4 == 3 ? 43 : 45) }); }
            // just before each bar line: let go, set the next mode, press again (Quantise: it engages on the bar)
            int offDone = -1, onDone = -1;
            onBeat = [&] (double beat)
            {
                struct S { bool on; int mode; float pitch; };
                static const S plan[9] = { { false, 0, 0 }, { false, 0, 0 }, { true, 0, 5 }, { true, 1, 0 }, { true, 2, 0 }, { true, 3, 0 }, { true, 0, -5 }, { false, 0, 0 }, { false, 0, 0 } };
                const int nextBar = (int) std::floor (beat / 4.0) + 1;
                const double toLine = nextBar * 4.0 - beat;
                if (toLine < 0.12 && offDone != nextBar)
                {
                    offDone = nextBar;
                    const auto& pl = plan[juce::jlimit (0, 8, nextBar)];
                    setP (*p, P_pfAkai, 0.0f);
                    setP (*p, P_pfAkaiMode, (float) pl.mode); setP (*p, P_pfAkaiPitch, pl.pitch); setP (*p, P_pfAkaiSpeed, 1.0f);
                }
                else if (toLine < 0.06 && onDone != nextBar && offDone == nextBar)
                {
                    onDone = nextBar;
                    if (plan[juce::jlimit (0, 8, nextBar)].on) setP (*p, P_pfAkai, 1.0f);
                }
            };
            renderTo (*p, "perf_demo_akai_stretch_172.wav", ev);
            onBeat = nullptr;
        }
        // 8. the Reese engine over the demo break: bars 1-4 Beats mode (the beating locked to 1/2 notes at
        //    172 BPM, Phase Twist and the synced notch moving with it), bars 5-8 Cents mode (14 cents, free)
        {
            auto p = make(); p->loadFactoryPreset (32);
            for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_supersawGain, P_complexGain }) setP (*p, i, 0.0f);
            setP (*p, P_ampA, 0.003f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.06f); setP (*p, P_filterCutoff, 20000.0f);
            setP (*p, P_rsLevel, 0.4f); setP (*p, P_rsVoices, 2.0f); setP (*p, P_rsDetMode, 2.0f); setP (*p, P_rsBeat, 3.0f); setP (*p, P_rsRatio, 0.0f);
            setP (*p, P_rsTwist, 0.25f); setP (*p, P_rsNotch, 0.55f); setP (*p, P_rsNotchFreq, 700.0f); setP (*p, P_rsNotchSync, 1.0f);
            setP (*p, P_rsOct, -1.0f); setP (*p, P_rsSub, 0.5f); setP (*p, P_rsDrive, 0.35f); setP (*p, P_rsTone, 0.7f); setP (*p, P_rsWidth, 0.7f); setP (*p, P_rsRetrig, 1.0f);
            setP (*p, P_quality, 2.0f);
            setP (*p, P_bcKeyBase, 96.0f);   // (the slices' keys moved up out of the way, so the bass notes play the synth)
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            const int notes[] = { 45, 45, 41, 43, 45, 45, 48, 43 };   // (the Reese an octave down: A1 A1 F1 G1 ...)
            for (int b = 0; b < 8; ++b) { ev.push_back ({ (int) (b * 4.0 / inc), juce::MidiMessage::noteOn (1, notes[b], (juce::uint8) 100) });
                                          ev.push_back ({ (int) ((b * 4.0 + 3.75) / inc), juce::MidiMessage::noteOff (1, notes[b]) }); }
            bool switched = false;
            onBeat = [&] (double beat) { if (! switched && beat >= 16.0 - 0.02) { switched = true; setP (*p, P_rsDetMode, 0.0f); setP (*p, P_rsCents, 14.0f); } };
            renderTo (*p, "reese_demo_beats_then_cents_172.wav", ev);
            onBeat = nullptr;
        }
        // 9. Break-Reactive mode: the same Reese bass over the break, bars 1-4 as it is, bars 5-8 reacting to the
        //    kicks (phase reset, a +5 st pitch kick, a drive kick, a little duck)
        {
            auto p = make(); p->loadFactoryPreset (32);
            for (int i : { P_osc1Gain, P_osc2Gain, P_osc3Gain, P_subGain, P_supersawGain, P_complexGain }) setP (*p, i, 0.0f);
            setP (*p, P_ampA, 0.003f); setP (*p, P_ampS, 1.0f); setP (*p, P_ampR, 0.06f); setP (*p, P_filterCutoff, 20000.0f);
            setP (*p, P_bcKeyBase, 96.0f);
            setP (*p, P_rsLevel, 0.4f); setP (*p, P_rsVoices, 0.0f); setP (*p, P_rsDetMode, 2.0f); setP (*p, P_rsBeat, 3.0f); setP (*p, P_rsOct, -1.0f);
            setP (*p, P_rsTwist, 0.2f); setP (*p, P_rsNotch, 0.5f); setP (*p, P_rsNotchFreq, 600.0f); setP (*p, P_rsSub, 0.5f); setP (*p, P_rsDrive, 0.3f);
            setP (*p, P_rsTone, 0.7f); setP (*p, P_rsWidth, 0.6f); setP (*p, P_rsRetrig, 1.0f); setP (*p, P_quality, 2.0f);
            setP (*p, P_rxListen, 1.0f); setP (*p, P_rxReset, 1.0f); setP (*p, P_rxPitch, 5.0f); setP (*p, P_rxDrive, 0.4f); setP (*p, P_rxDuck, 0.25f);
            setP (*p, P_rxNotch, 0.5f); setP (*p, P_rxDecay, 90.0f);
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            const int notes[] = { 45, 45, 41, 43, 45, 45, 41, 43 };
            for (int b = 0; b < 8; ++b) { ev.push_back ({ (int) (b * 4.0 / inc), juce::MidiMessage::noteOn (1, notes[b], (juce::uint8) 100) });
                                          ev.push_back ({ (int) ((b * 4.0 + 3.75) / inc), juce::MidiMessage::noteOff (1, notes[b]) }); }
            bool switched = false;
            onBeat = [&] (double beat) { if (! switched && beat >= 16.0 - 0.02) { switched = true; setP (*p, P_rxOn, 1.0f); } };
            renderTo (*p, "react_demo_off_then_on_172.wav", ev);
            onBeat = nullptr;
            std::cout << "  (react demo: " << p->getEngine().rxHitsShown.load() << " kicks reacted to)" << std::endl;
        }
        // 5. fills: Break Frankenstein's chain with an Auto Fill every 4 bars (one bar long, ending the
        //    phrase), each fill played from Break 3 over the groove, and the pattern back on the downbeat
        {
            auto p = make(); p->loadFactoryPreset (35);
            // the fill written in Pattern H on Break 3 (a Snare Build made from A), the global Fill Pattern, 1 bar
            setP (*p, P_bsAutoFill, 1.0f); setP (*p, P_bsAutoFillProb, 1.0f); setP (*p, P_bsFillPat, 8.0f); setP (*p, P_bsFillSize, 3.0f);
            p->breakStore.edit ([] (tg::BreakData& d) { std::copy (d.step[0], d.step[0] + tg::BreakData::kSteps, d.step[7]); d.patBreak[7] = 3;
                                                        tg::breaktools::applyTool (tg::breaktools::ToolSnareBuild, d, 7, 32, 3, 0.8f, 2, false); });
            std::vector<std::pair<int, juce::MidiMessage>> ev;
            for (int b = 0; b < 8; ++b) { ev.push_back ({ (int) (b * 4.0 / inc), juce::MidiMessage::noteOn (1, b % 4 == 3 ? 43 : 45, (juce::uint8) 90) });
                                          ev.push_back ({ (int) ((b * 4.0 + 3.5) / inc), juce::MidiMessage::noteOff (1, b % 4 == 3 ? 43 : 45) }); }
            std::vector<MegaSynthProcessor::BsLogEntry> log; p->bsLog = &log;
            renderTo (*p, "break_demo_5_fills_every_4_bars_172.wav", ev);
            p->bsLog = nullptr;
            juce::String fills; int64_t last = -2;
            for (auto& e : log) if (e.fill) { if (e.step != last + 1) fills << " from step " << (int) e.step; last = e.step; }
            const auto d = p->breakStore.get();
            std::cout << "  demo 5 fills:" << fills << " (Pattern H on Break " << d.patBreak[7] << ")" << std::endl;
        }
        return 0;
    }
    // ---- v0.1.70: the new Space effects and the Reese additions
    auto spaceTests = [&]
    {
        std::cout << "Space effects / Reese additions (v0.1.70)" << std::endl;
        const double fs = 48000.0;
        auto defaults = [] { std::vector<float> v ((size_t) P_COUNT); for (int i = 0; i < P_COUNT; ++i) v[(size_t) i] = meta (i).def; return v; };
        auto rmsOf = [] (const std::vector<float>& x, int a, int b) { double e = 0; b = std::min (b, (int) x.size()); for (int i = std::max (0, a); i < b; ++i) e += (double) x[(size_t) i] * x[(size_t) i]; return std::sqrt (e / std::max (1, b - a)); };
        auto db = [] (double r) { return 20.0 * std::log10 (std::max (1.0e-12, r)); };
        FxContext ctx; ctx.sr = fs; ctx.tempo = 172.0;
        // runs an effect over a mono input (both channels), 256 at a time; fx (L, R, n, blockStart)
        auto runFx = [&] (const std::vector<float>& in, std::function<void (float*, float*, int, int)> fx, std::vector<float>* right = nullptr)
        {
            std::vector<float> L (in), R (in);
            for (int pos = 0; pos < (int) in.size(); pos += 256) fx (L.data() + pos, R.data() + pos, std::min (256, (int) in.size() - pos), pos);
            if (right != nullptr) *right = R;
            return L;
        };
        uint32_t nrng = 12345u;
        auto noise = [&] { nrng = nrng * 1664525u + 1013904223u; return (float) ((nrng >> 8) & 0xffff) / 32768.0f - 1.0f; };
        // ---- Dub Delay: 110% feedback builds up but stays bounded; 60% dies away
        {
            auto v = defaults();
            v[P_sxDubSync] = 0; v[P_sxDubTime] = 0.2f; v[P_sxDubMode] = 2; v[P_sxDubDrive] = 0.0f;
            std::vector<float> in ((size_t) (10.0 * fs), 0.0f);
            for (int i = 0; i < (int) (0.1 * fs); ++i) in[(size_t) i] = 0.5f * noise();
            for (float fb : { 1.1f, 0.6f })
            {
                v[P_sxDubFb] = fb;
                DubDelay d; d.prepare (fs);
                auto out = runFx (in, [&] (float* l, float* r, int n, int) { d.process (l, r, n, v.data(), ctx, false, nullptr); });
                float pk = 0; bool fin = true; for (float x : out) { pk = std::max (pk, std::abs (x)); fin &= std::isfinite (x); }
                const double last = rmsOf (out, (int) (9.0 * fs), (int) (10.0 * fs));
                std::cout << "  Dub Delay feedback " << juce::roundToInt (fb * 100) << "%: peak " << pk << ", last second " << juce::String (db (last), 1) << " dB" << std::endl;
                if (fb > 1.0f) CHECK (fin && pk < 2.0f && last > 0.01, "Dub Delay at 110%: a runaway that stays bounded (peak " + juce::String (pk, 2) + ")");
                else CHECK (fin && last < 1.0e-4, "Dub Delay at 60%: the repeats die away");
            }
        }
        // ---- Throw: Hold sends only while held; Latch between presses
        {
            auto v = defaults();
            v[P_sxDubSync] = 0; v[P_sxDubTime] = 0.25f; v[P_sxDubFb] = 0.0f; v[P_sxDubDrive] = 0.0f;
            std::vector<float> in ((size_t) (4.0 * fs));
            for (size_t i = 0; i < in.size(); ++i) in[i] = 0.4f * (float) std::sin (2.0 * kPi * 330.0 * (double) i / fs);
            for (int mode : { 0, 1 })
            {
                v[P_sxDubMode] = (float) mode;
                DubDelay d; d.prepare (fs);
                auto out = runFx (in, [&] (float* l, float* r, int n, int pos)
                {
                    const double t = pos / fs;
                    const bool held = mode == 0 ? (t >= 1.0 && t < 1.5) : ((t >= 1.0 && t < 1.05) || (t >= 2.0 && t < 2.05));
                    d.process (l, r, n, v.data(), ctx, held, nullptr);
                });
                std::vector<float> wet (in.size()); for (size_t i = 0; i < in.size(); ++i) wet[i] = out[i] - in[i];
                const double endAt = mode == 0 ? 1.5 : 2.0;
                const double before = rmsOf (wet, 0, (int) (1.2 * fs)), during = rmsOf (wet, (int) (1.3 * fs), (int) ((endAt + 0.2) * fs)), after = rmsOf (wet, (int) ((endAt + 0.3) * fs), (int) (4.0 * fs));
                CHECK (before < 1.0e-6 && during > 0.05 && after < 1.0e-4, juce::String (mode == 0 ? "Throw (Hold)" : "Throw (Latch)") + ": only what plays while it's on goes into the delay ("
                       + juce::String (db (before), 0) + " / " + juce::String (db (during), 0) + " / " + juce::String (db (after), 0) + " dB)");
            }
        }
        // ---- Ducking: dips within 1 ms of the hit, comes back over Release
        {
            SpaceDucker dk; std::vector<float> g (12000); std::vector<char> hit (12000, 0); hit[0] = 1;
            dk.run (g.data(), 12000, reinterpret_cast<const bool*> (hit.data()), 1.0f, 0.1f, 100.0f, fs);
            float gMin = 1.0f; for (int i = 0; i <= 48; ++i) gMin = std::min (gMin, g[(size_t) i]);
            CHECK (gMin < 0.02f && g[(size_t) (0.15 * fs)] > 0.9f, "a duck dips within 1 ms of its hit (down to " + juce::String (gMin, 3) + ") and comes back over Release");
            // through the plug-in: the Plate's duck listening to the synth's notes
            auto p = make();
            setP (*p, P_sxOnPlate, 1.0f); setP (*p, sxDuckParam (SD_Plate, SDF_Amt), 1.0f); setP (*p, sxDuckParam (SD_Plate, SDF_Src), 6.0f); setP (*p, sxDuckParam (SD_Plate, SDF_Att), 0.1f);
            juce::AudioBuffer<float> b (2, 48); juce::MidiBuffer none; p->processBlock (b, none);
            const float g0 = p->getEngine().fx().duckShown[SD_Plate].load();
            juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            b.clear(); p->processBlock (b, m);
            const float g1 = p->getEngine().fx().duckShown[SD_Plate].load();
            CHECK (g0 > 0.999f && g1 < 0.05f, "the Plate ducks on a synth note within 1 ms (" + juce::String (g1, 3) + ")");
            // the break's kicks (a slice set to Kick) duck; a snare listener doesn't
            for (int src : { 0, 1 })
            {
                auto q = make(); setP (*q, P_bcOn, 1.0f);
                q->loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, 0), 44100.0), "demo.wav", MegaSynthProcessor::kSlotBreak);
                q->breakStore.setAnalysis ([] (tg::BreakData& d) { d.brk[0].slice[0].hitType = 1; });
                setP (*q, P_sxOnIr, 1.0f); setP (*q, sxDuckParam (SD_Ir, SDF_Amt), 1.0f); setP (*q, sxDuckParam (SD_Ir, SDF_Src), (float) src); setP (*q, sxDuckParam (SD_Ir, SDF_Att), 0.1f);
                const int kb = juce::roundToInt (q->param (P_bcKeyBase)->convertFrom0to1 (q->param (P_bcKeyBase)->getValue()));
                juce::AudioBuffer<float> bb (2, 48); juce::MidiBuffer mm; mm.addEvent (juce::MidiMessage::noteOn (1, kb, (juce::uint8) 100), 0);
                q->processBlock (bb, mm); bb.clear(); juce::MidiBuffer e2; q->processBlock (bb, e2);
                const float gk = q->getEngine().fx().duckShown[SD_Ir].load();
                CHECK (src == 0 ? gk < 0.1f : gk > 0.999f, juce::String (src == 0 ? "a kick slice ducks what listens to Break kicks" : "... and not what listens to Break snares"));
            }
        }
        // ---- Freeze: the tail holds steady, without a click
        {
            auto v = defaults();
            std::vector<float> in ((size_t) (7.0 * fs), 0.0f);
            for (int i = 0; i < (int) (1.0 * fs); ++i) in[(size_t) i] = 0.3f * noise();
            for (int which = 0; which < 3; ++which)
            {
                const char* nm[3] = { "Plate", "Spring", "Gated Reverb" };
                double held[2] {}; float clickMax = 0, steadyMax = 0;
                for (int fz : { 1, 0 })
                {
                    PlateReverb pl; SpringReverb sp; GatedReverb gt;
                    pl.prepare (fs); sp.prepare (fs); gt.prepare (fs);
                    auto vv = v; vv[P_sxGateTrig] = 1;   // (no trigger: the gate opens only when frozen)
                    std::vector<float> R;
                    auto out = runFx (in, [&] (float* l, float* r, int n, int pos)
                    {
                        const bool f = fz == 1 && pos >= (int) (1.0 * fs);
                        vv[P_sxPlFreeze] = vv[P_sxSprFreeze] = vv[P_sxGateFreeze] = f ? 1.0f : 0.0f;
                        if (which == 0) pl.process (l, r, n, vv.data(), ctx, nullptr);
                        else if (which == 1) sp.process (l, r, n, vv.data(), ctx, nullptr);
                        else gt.process (l, r, n, vv.data(), ctx, 0, nullptr, nullptr);
                    });
                    held[fz] = rmsOf (out, (int) (5.0 * fs), (int) (6.0 * fs)) / std::max (1.0e-9, rmsOf (out, (int) (2.0 * fs), (int) (3.0 * fs)));
                    if (fz == 1)
                    {
                        for (int i = (int) (0.95 * fs); i < (int) (1.2 * fs); ++i) clickMax = std::max (clickMax, std::abs (out[(size_t) i] - out[(size_t) i - 1]));
                        for (int i = (int) (0.5 * fs); i < (int) (0.95 * fs); ++i) steadyMax = std::max (steadyMax, std::abs (out[(size_t) i] - out[(size_t) i - 1]));
                    }
                }
                std::cout << "  " << nm[which] << " Freeze: 3 s later " << juce::String (db (held[1]), 2) << " dB (not frozen " << juce::String (db (held[0]), 1) << " dB); largest step " << clickMax << " (steady " << steadyMax << ")" << std::endl;
                CHECK (std::abs (db (held[1])) < 1.5 && (which == 2 || db (held[0]) < -10.0) && clickMax < steadyMax * 1.6f, juce::String (nm[which]) + ": Freeze holds the tail steady, without a click");
            }
            // the 90s reverb (the convolution's tail, recorded and played as grains)
            double ratio[2] {};
            for (int fz : { 1, 0 })
            {
                auto p = make(); setP (*p, P_reverbMix, 0.6f); setP (*p, P_reverbSize, 3.0f);
                p->getEngine();   // (the impulse loads in the background)
                juce::Thread::sleep (300);
                render (*p, 0.2, {});
                juce::Thread::sleep (200);
                render (*p, 1.2, chord (0.0, 0.8, { 48, 55, 60 }));
                if (fz) setP (*p, P_sxRevFreeze, 1.0f);
                juce::AudioBuffer<float> cap; render (*p, 5.0, {}, &cap);
                std::vector<float> o (cap.getReadPointer (0), cap.getReadPointer (0) + cap.getNumSamples());
                ratio[fz] = rmsOf (o, (int) (3.5 * fs), (int) (4.5 * fs)) / std::max (1.0e-9, rmsOf (o, (int) (0.5 * fs), (int) (1.5 * fs)));
            }
            std::cout << "  90s Reverb Freeze: 3 s later " << juce::String (db (ratio[1]), 2) << " dB (not frozen " << juce::String (db (ratio[0]), 1) << " dB)" << std::endl;
            CHECK (std::abs (db (ratio[1])) < 2.0 && db (ratio[0]) < -10.0, "the 90s Reverb's Freeze holds its tail");
        }
        // ---- Widener: wide, mono-safe, the lows left alone
        {
            auto v = defaults();
            std::vector<float> in ((size_t) fs); for (auto& x : in) x = 0.3f * noise();
            MicroWidener w; w.prepare (fs);
            std::vector<float> R;
            auto L = runFx (in, [&] (float* l, float* r, int n, int) { w.process (l, r, n, v.data(), ctx); }, &R);
            std::vector<float> mono (in.size()), side (in.size());
            for (size_t i = 0; i < in.size(); ++i) { mono[i] = 0.5f * (L[i] + R[i]); side[i] = 0.5f * (L[i] - R[i]); }
            const double monoDb = db (rmsOf (mono, 4800, (int) in.size()) / rmsOf (in, 4800, (int) in.size())), sideDb = db (rmsOf (side, 4800, (int) in.size()) / rmsOf (in, 4800, (int) in.size()));
            std::vector<float> lo ((size_t) fs); for (size_t i = 0; i < lo.size(); ++i) lo[i] = 0.4f * (float) std::sin (2.0 * kPi * 50.0 * (double) i / fs);
            MicroWidener w2; w2.prepare (fs);
            std::vector<float> R2; auto L2 = runFx (lo, [&] (float* l, float* r, int n, int) { w2.process (l, r, n, v.data(), ctx); }, &R2);
            std::vector<float> side2 (lo.size()); for (size_t i = 0; i < lo.size(); ++i) side2[i] = 0.5f * (L2[i] - R2[i]);
            const double lowSide = db (rmsOf (side2, 4800, (int) lo.size()) / rmsOf (lo, 4800, (int) lo.size()));
            std::cout << "  Widener: mono sum " << juce::String (monoDb, 2) << " dB, side " << juce::String (sideDb, 1) << " dB; at 50 Hz the side is " << juce::String (lowSide, 1) << " dB" << std::endl;
            CHECK (monoDb > -0.5 && sideDb > -20.0 && lowSide < -30.0, "the Widener: wide, the mono sum keeps the dry sound, the lows stay mono");
        }
        // ---- the rack: all sixteen slots, a new slot with Return 0 changes nothing, older racks get the new slots after theirs
        {
            FxRackStore st; juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty ("order", "8,7,6,5,4,3,2,1,0");
            st.fromVar (juce::var (o.get()));
            const auto ord = st.getOrder();
            bool ok = ord[0] == 8 && ord[8] == 0; for (int k = 9; k < FS_COUNT; ++k) ok = ok && ord[(size_t) k] == k;
            CHECK (ok, "an older project's effect order is kept, the new effects after it");
            auto a = make(), b = make();
            setP (*b, P_sxOnPlate, 1.0f); setP (*b, P_sxMixPlate, 0.0f); setP (*b, P_sxOnDub, 1.0f); setP (*b, P_sxMixDub, 0.0f);
            juce::AudioBuffer<float> ca, cb;
            tg::resetVoiceRandom(); render (*a, 1.0, chord (0.0, 0.6, { 48, 60 }), &ca);
            tg::resetVoiceRandom(); render (*b, 1.0, chord (0.0, 0.6, { 48, 60 }), &cb);
            float diff = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < ca.getNumSamples(); ++i) diff = std::max (diff, std::abs (ca.getSample (c, i) - cb.getSample (c, i)));
            CHECK (diff == 0.0f, "a new effect on with its Return at 0 changes nothing (" + juce::String (diff) + ")");
        }
        // ---- IR Reverb: built-in impulses, your own file saved with the project
        {
            auto ir = makeSpaceImpulse (2, fs, 10.0);
            double e = 0; for (int i = 0; i < ir.getNumSamples(); ++i) e += (double) ir.getSample (0, i) * ir.getSample (0, i);
            CHECK (ir.getNumSamples() > (int) (2.0 * fs) && e > 0.0, "the built-in Hall impulse is made");
            auto p = make();
            juce::AudioBuffer<float> user (2, (int) (0.5 * 44100.0));
            for (int i = 0; i < user.getNumSamples(); ++i) { const float s = (float) std::exp (-8.0 * i / 44100.0) * (i % 7 == 0 ? 0.8f : -0.1f); user.setSample (0, i, s); user.setSample (1, i, s * 0.9f); }
            CHECK (p->loadSampleData (tg::breaks::toWav16 (user, 44100.0), "my room.wav", MegaSynthProcessor::kSlotIr), "an impulse file loads");
            setP (*p, P_sxOnIr, 1.0f); setP (*p, P_sxIrType, 0.0f); setP (*p, P_sxMixIr, 1.0f);
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q->getSample (MegaSynthProcessor::kSlotIr) != nullptr && q->getSampleName (MegaSynthProcessor::kSlotIr) == "my room.wav", "the impulse file is saved with the project");
            q->updateIrImpulse (true);
            for (int k = 0; k < 40 && ! q->getEngine().fx().irReady(); ++k) { juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer m; q->processBlock (b, m); juce::Thread::sleep (25); }
            CHECK (q->getEngine().fx().irReady(), "the impulse loads (in the background)");
            for (int i : { P_osc1Gain }) setP (*q, i, 0.6f);
            juce::AudioBuffer<float> cap; render (*q, 1.0, chord (0.0, 0.05, { 60 }), &cap);
            std::vector<float> o (cap.getReadPointer (1), cap.getReadPointer (1) + cap.getNumSamples());
            CHECK (rmsOf (o, (int) (0.25 * fs), (int) (0.4 * fs)) > 1.0e-4, "the IR Reverb plays the impulse's tail");
        }
        // ---- the Throw pad and the Duck note: learned / set, and not played
        {
            auto p = make(); p->throwPadLearn = true;
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0); p->processBlock (b, m);
            CHECK (p->throwPadKind.load() == 1 && p->throwPadNum.load() == 40, "Learn takes the next note as the Throw pad");
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q->throwPadKind.load() == 1 && q->throwPadNum.load() == 40, "the Throw pad is saved with the project");
            auto level = [&] (bool asDuck)
            {
                auto r = make(); if (asDuck) setP (*r, P_sxDuckNote, 60.0f);
                double e = 0;
                for (int i = 0; i < 30; ++i) { juce::AudioBuffer<float> o (2, 256); o.clear(); juce::MidiBuffer mm; if (i == 0) mm.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); r->processBlock (o, mm); e += o.getMagnitude (0, 256); }
                return e;
            };
            CHECK (level (false) > 0.1 && level (true) < 1.0e-6, "the Duck note isn't played");
        }
        // ---- older projects: the new controls at their defaults; the presets set only their own controls
        {
            auto p = make();
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto xml = juce::parseXML (getXmlFromBinaryForTest (mb));
            if (xml != nullptr)
            {
                xml->setAttribute ("stateVersion", 29);
                for (int i = P_sxOnDub; i <= P_rsDriftRate; ++i) if (auto* e = xml->getChildByAttribute ("id", kParamIds[i])) xml->removeChildElement (e, true);
                juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (*xml, mb2);
                auto r = make(); setP (*r, P_sxOnDub, 1.0f); setP (*r, P_rsFmtOn, 1.0f); setP (*r, P_rsBloom, 2.0f);
                r->setStateInformation (mb2.getData(), (int) mb2.getSize());
                auto plain = [] (MegaSynthProcessor& x, int i) { return x.param (i)->convertFrom0to1 (x.param (i)->getValue()); };
                CHECK (plain (*r, P_sxOnDub) < 0.5f && plain (*r, P_rsFmtOn) < 0.5f && plain (*r, P_rsBloom) == 0.0f, "older projects load with the new Space and Reese controls off");
            }
            auto q = make(); setP (*q, P_reverbMix, 0.33f); setP (*q, P_rsLevel, 0.0f);
            q->pushHistory ("set up");
            q->applySpacePreset (1);
            auto plain = [] (MegaSynthProcessor& x, int i) { return x.param (i)->convertFrom0to1 (x.param (i)->getValue()); };
            CHECK (std::abs (plain (*q, P_reverbMix) - 0.33f) < 0.01f && plain (*q, P_sxOnDub) > 0.5f && plain (*q, P_sxDubMode) < 0.5f, "a Space preset sets only the new Space controls");
            q->undo();
            CHECK (plain (*q, P_sxOnDub) < 0.5f, "... and Undo takes it back");
            q->applyReesePreset (4);
            CHECK (juce::roundToInt (plain (*q, P_rsWave)) == 3 && plain (*q, P_rsLevel) > 0.5f && std::abs (plain (*q, P_reverbMix) - 0.33f) < 0.01f, "the Hoover preset: the Reese only");
        }
        // ---- Reese: Bloom timing, the pitch envelope, Split Drive, drift, the shaper's aliasing
        {
            auto reeseRun = [&] (ReeseSettings rs, double note, double seconds, uint32_t seed, std::vector<float>* outR = nullptr, std::function<void (ReeseOsc&, double)> each = nullptr)
            {
                ReeseOsc o; o.prepare (fs); o.start (1, seed);
                std::vector<float> L ((size_t) (seconds * fs)), R (L.size());
                for (size_t i = 0; i < L.size(); i += 32)
                {
                    o.setFrequency (note, 172.0, rs);
                    if (each) each (o, (double) i / fs);
                    for (size_t j = i; j < std::min (L.size(), i + 32); ++j) o.tick (L[j], R[j]);
                }
                if (outR != nullptr) *outR = R;
                return L;
            };
            ReeseSettings rs; rs.cents = 20.0f; rs.mode = 0; rs.bloom = 1.0f;
            double d0 = 0, dHalf = 0, dFull = 0;
            reeseRun (rs, 110.0, 1.3, 7, nullptr, [&] (ReeseOsc& o, double t)
            {
                const double d = o.voiceFrequency (1) - o.voiceFrequency (0);
                if (t < 1.0e-9) d0 = d; if (std::abs (t - 0.5) < 0.0004) dHalf = d; if (t > 1.2 && dFull == 0) dFull = d;
            });
            CHECK (std::abs (d0) < 1.0e-6 && std::abs (dHalf / dFull - 0.5) < 0.02 && dFull > 1.0, "Detune Bloom: no detune at the start, half at half the Bloom time (" + juce::String (dHalf / dFull, 3) + "), all of it after");
            rs.bloomRev = true; double r0 = 0;
            reeseRun (rs, 110.0, 0.01, 7, nullptr, [&] (ReeseOsc& o, double t) { if (t < 1.0e-9) r0 = o.voiceFrequency (1) - o.voiceFrequency (0); });
            CHECK (std::abs (r0 / dFull - 3.0) < 0.05, "Bloom Reverse starts three times as wide");
            ReeseSettings pd; pd.pdMode = 1; pd.pdAmt = 12.0f; pd.pdTime = 0.5f; pd.cents = 0.0f;
            double f0 = 0, f1 = 0;
            auto mid = [] (ReeseOsc& o) { return 0.5 * (o.voiceFrequency (0) + o.voiceFrequency (1)); };   // (the note: halfway between the beating voices)
            reeseRun (pd, 110.0, 0.7, 3, nullptr, [&] (ReeseOsc& o, double t) { if (t < 1.0e-9) f0 = mid (o); if (t > 0.6 && f1 == 0) f1 = mid (o); });
            CHECK (std::abs (f0 / 220.0 - 1.0) < 0.01 && std::abs (f1 / 110.0 - 1.0) < 0.001, "the pitch envelope (Drop, 12 st): an octave up at the start, on the note after Time");
            // Split Drive: the lows stay clean (compared with no drive), the highs change; the single Drive changes the lows
            auto lowBand = [&] (const std::vector<float>& x) { std::vector<float> y (x.size()); float a = 0, b = 0; const float c = (float) (1.0 - std::exp (-2.0 * kPi * 70.0 / fs)); for (size_t i = 0; i < x.size(); ++i) { a += (x[i] - a) * c; b += (a - b) * c; y[i] = b; } return y; };
            ReeseSettings plainS; plainS.sub = 0.8f; plainS.cents = 10.0f;
            // (each against itself without the drive: Split with High Drive 0 is the same crossover, so only the drive differs)
            ReeseSettings splitS = plainS; splitS.split = true; splitS.splitHz = 150.0f; splitS.lowDrive = 0.0f; splitS.highDrive = 1.0f;
            ReeseSettings split0 = splitS; split0.highDrive = 0.0f;
            ReeseSettings driveS = plainS; driveS.drive = 1.0f;
            const auto a = lowBand (reeseRun (plainS, 41.2, 1.0, 5)), a0 = lowBand (reeseRun (split0, 41.2, 1.0, 5)), b = lowBand (reeseRun (splitS, 41.2, 1.0, 5)), c2 = lowBand (reeseRun (driveS, 41.2, 1.0, 5));
            std::vector<float> dSplit (a.size()), dDrive (a.size());
            for (size_t i = 0; i < a.size(); ++i) { dSplit[i] = b[i] - a0[i]; dDrive[i] = c2[i] - a[i]; }
            const double eS = rmsOf (dSplit, 4800, (int) a.size()) / rmsOf (a, 4800, (int) a.size()), eD = rmsOf (dDrive, 4800, (int) a.size()) / rmsOf (a, 4800, (int) a.size());
            std::cout << "  Split Drive: the lows (below 70 Hz) change by " << juce::String (db (eS), 1) << " dB (the single Drive: " << juce::String (db (eD), 1) << " dB)" << std::endl;
            CHECK (db (eS) < -20.0 && db (eD) > db (eS) + 6.0, "Split Drive leaves the low band (nearly) clean");
            // drift: the same every time from the same note; another note differs; Beats mode keeps its beating
            ReeseSettings dr; dr.drift = 1.0f; dr.driftRate = 2.0f;
            const auto x1 = reeseRun (dr, 110.0, 1.0, 11), x2 = reeseRun (dr, 110.0, 1.0, 11), x3 = reeseRun (dr, 110.0, 1.0, 12);
            bool same = true, differ = false; for (size_t i = 0; i < x1.size(); ++i) { same = same && x1[i] == x2[i]; differ = differ || x1[i] != x3[i]; }
            CHECK (same && differ, "Analog Drift: the same from the same note's seed, different for another");
            ReeseSettings bt; bt.mode = 2; bt.beat = 4; bt.drift = 1.0f; double beat = 0;
            reeseRun (bt, 110.0, 0.5, 9, nullptr, [&] (ReeseOsc& o, double) { beat = std::max (beat, std::abs (o.beatRate() - 172.0 / 60.0)); });
            CHECK (beat < 1.0e-9, "with Drift in Beats mode the beating stays locked to the tempo");
            // the shaper's aliasing: an aliasing measure at Normal (not oversampled) and High (x2)
            auto aliasDb = [&] (int quality)
            {
                ReeseSettings s; s.cents = 0.0f; s.shp = 1; s.shpAmt = 0.6f; s.quality = quality; s.width = 0.0f;
                const double f = 1661.22;   // (G#6)
                std::vector<float> R; auto L = reeseRun (s, f, 0.6, 1, &R);
                const int N = 16384; juce::dsp::FFT fft (14);
                std::vector<float> w ((size_t) N * 2, 0.0f);
                for (int i = 0; i < N; ++i) w[(size_t) i] = L[(size_t) (8000 + i)] * (0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / N));
                fft.performFrequencyOnlyForwardTransform (w.data());
                double harm = 0, other = 0;
                for (int k = 1; k < N / 2; ++k)
                {
                    const double hz = k * fs / N, h = hz / f, dist = std::abs (h - std::round (h)) * f;
                    const double e = (double) w[(size_t) k] * w[(size_t) k];
                    if (dist < 4.0 * fs / N) harm += e; else other += e;
                }
                return 10.0 * std::log10 (std::max (1.0e-20, other) / std::max (1.0e-20, harm));
            };
            const double aN = aliasDb (1), aH = aliasDb (2);
            std::cout << "  Shaper (Fold 60%, G#6): energy off the harmonics " << juce::String (aN, 1) << " dB at Normal, " << juce::String (aH, 1) << " dB at High (x2)" << std::endl;
            CHECK (aH < aN - 3.0, "the shaper aliases less at High (oversampled)");
            // every new part makes sound, finite, and different from without it
            ReeseSettings base; base.cents = 14.0f;
            const auto ref = reeseRun (base, 110.0, 0.5, 2);
            auto check = [&] (const char* what, ReeseSettings s)
            {
                const auto x = reeseRun (s, 110.0, 0.5, 2);
                bool fin = true; double d = 0; for (size_t i = 0; i < x.size(); ++i) { fin &= std::isfinite (x[i]) && std::abs (x[i]) < 8.0f; d += std::abs (x[i] - ref[i]); }
                CHECK (fin && d > 1.0, juce::String ("Reese ") + what + ": finite and audible");
            };
            { auto s = base; s.fmtOn = true; s.fmtVowel = 0.5f; check ("formant", s); }
            { auto s = base; s.fmtOn = true; s.fmtMove = 1; s.fmtLfoInc = 4.0 / fs; s.fmtShape = 3; check ("formant LFO", s); }
            { auto s = base; s.cp = 1; s.cpFb = 0.9f; check ("comb", s); }
            { auto s = base; s.cp = 2; s.cpStages = 12; s.cpFb = -0.8f; s.cpLfoInc = 2.0 / fs; check ("phaser", s); }
            { auto s = base; s.wave = 3; s.stack = 2; check ("Hoover + stack", s); }
            { auto s = base; s.fm = 0.8f; check ("cross-FM", s); }
            { auto s = base; s.sync = true; s.syncRatio = 3.3f; check ("sync", s); }
            for (int t = 1; t <= 4; ++t) { auto s = base; s.shp = t; s.shpAmt = 0.8f; s.shpSym = 0.4f; check ("shaper", s); }
            // the formant's vowels: U is darker than A
            auto bright = [&] (float vow)
            {
                auto s = base; s.fmtOn = true; s.fmtVowel = vow; s.fmtMix = 1.0f;
                const auto x = reeseRun (s, 110.0, 0.5, 2);
                std::vector<float> hp (x.size()); float lp = 0; const float c = (float) (1.0 - std::exp (-2.0 * kPi * 1500.0 / fs));
                for (size_t i = 0; i < x.size(); ++i) { lp += (x[i] - lp) * c; hp[i] = x[i] - lp; }
                return rmsOf (hp, 4800, (int) x.size()) / rmsOf (x, 4800, (int) x.size());
            };
            CHECK (bright (1.0f) < bright (0.0f) * 0.7, "the formant: U is darker than A");
        }
        // ---- CPU, measured directly (less noisy): each effect on 10 s of stereo noise; one Reese note (4 voices) with each part,
        //      times 8 for 8 notes (the quickest of 3 runs each)
        {
            auto timeIt = [&] (std::function<void()> f) { double best = 1.0e9; for (int r = 0; r < 3; ++r) { const auto t0 = juce::Time::getMillisecondCounterHiRes(); f(); best = std::min (best, juce::Time::getMillisecondCounterHiRes() - t0); } return best; };
            auto v = defaults();
            const int N = (int) (10.0 * fs);
            std::vector<float> nL ((size_t) N), nR ((size_t) N); for (int i = 0; i < N; ++i) { nL[(size_t) i] = 0.2f * noise(); nR[(size_t) i] = 0.2f * noise(); }
            juce::String line = "  Effect alone, % of one core:";
            auto fxCost = [&] (const char* name, std::function<void (float*, float*, int)> proc)
            {
                std::vector<float> L (nL), R (nR);
                const double ms = timeIt ([&] { for (int pos = 0; pos < N; pos += 256) proc (L.data() + pos, R.data() + pos, std::min (256, N - pos)); });
                line << " " << name << " " << juce::String (ms / 100.0, 2);
                return ms / 100.0;
            };
            DubDelay dd; dd.prepare (fs); SpringReverb sp; sp.prepare (fs); GatedReverb gt; gt.prepare (fs); PlateReverb pl; pl.prepare (fs);
            MicroWidener wd; wd.prepare (fs); DimensionChorus dm; dm.prepare (fs); IrReverb ir; ir.prepare (fs, 256);
            ir.load (makeSpaceImpulse (2, fs, 10.0), fs);
            for (int k = 0; k < 40 && ! ir.ready(); ++k) { std::vector<float> a (256), b (256); ir.process (a.data(), b.data(), 256, v.data(), ctx, nullptr); juce::Thread::sleep (25); }
            double worstFx = 0;
            worstFx = std::max (worstFx, fxCost ("Dub", [&] (float* l, float* r, int n) { dd.process (l, r, n, v.data(), ctx, true, nullptr); }));
            worstFx = std::max (worstFx, fxCost ("Spring", [&] (float* l, float* r, int n) { sp.process (l, r, n, v.data(), ctx, nullptr); }));
            worstFx = std::max (worstFx, fxCost ("Gated", [&] (float* l, float* r, int n) { gt.process (l, r, n, v.data(), ctx, 0, nullptr, nullptr); }));
            worstFx = std::max (worstFx, fxCost ("Plate", [&] (float* l, float* r, int n) { pl.process (l, r, n, v.data(), ctx, nullptr); }));
            worstFx = std::max (worstFx, fxCost ("Widen", [&] (float* l, float* r, int n) { wd.process (l, r, n, v.data(), ctx); }));
            worstFx = std::max (worstFx, fxCost ("Dim", [&] (float* l, float* r, int n) { dm.process (l, r, n, v.data(), ctx); }));
            worstFx = std::max (worstFx, fxCost ("IR Hall (3.2 s)", [&] (float* l, float* r, int n) { ir.process (l, r, n, v.data(), ctx, nullptr); }));
            std::cout << line << std::endl;
            CHECK (worstFx < 6.0, "each new effect costs under 6% of a core");
            { juce::dsp::Convolution c (juce::dsp::Convolution::NonUniform { 1024 }); c.prepare ({ fs, 256, 2 }); CHECK (c.getLatency() == 0, "the IR Reverb's convolution adds no latency"); }
            for (int q = 0; q < 3; ++q)
            {
                static const char* qn[3] = { "Eco", "Normal", "High" };
                auto noteCost = [&] (ReeseSettings rs)
                {
                    rs.quality = q; rs.voices = 2;
                    const int M = (int) (4.0 * fs);
                    return timeIt ([&]
                    {
                        ReeseOsc o; o.prepare (fs); o.start (1, 3);
                        float a = 0, b = 0, acc = 0;
                        for (int i = 0; i < M; i += 32) { o.setFrequency (110.0, 172.0, rs); for (int j = 0; j < 32; ++j) { o.tick (a, b); acc += a; } }
                        if (acc == 12345.0f) std::cout << "";
                    }) / 40.0 * 8.0;   // (4 s of one note -> % of a core, x8 notes)
                };
                ReeseSettings base; base.cents = 14.0f;
                const double b0 = noteCost (base);
                juce::String rl; rl << "  Reese, 8 notes x 4 voices, " << qn[q] << " (" << juce::String (b0, 2) << "%), each part adds:";
                struct P { const char* n; std::function<void (ReeseSettings&)> f; };
                std::vector<P> parts = {
                    { "Formant", [] (ReeseSettings& s) { s.fmtOn = true; s.fmtMove = 1; s.fmtLfoInc = 1.0 / 48000.0; } },
                    { "Bloom", [] (ReeseSettings& s) { s.bloom = 1.0f; } },
                    { "Split", [] (ReeseSettings& s) { s.split = true; s.highDrive = 0.6f; s.lowDrive = 0.2f; } },
                    { "Shaper", [] (ReeseSettings& s) { s.shp = 1; s.shpAmt = 0.5f; } },
                    { "Comb", [] (ReeseSettings& s) { s.cp = 1; } },
                    { "Phaser", [] (ReeseSettings& s) { s.cp = 2; s.cpStages = 12; } },
                    { "Hoover+stack", [] (ReeseSettings& s) { s.wave = 3; s.stack = 2; } },
                    { "FM+Sync", [] (ReeseSettings& s) { s.fm = 0.5f; s.sync = true; s.syncRatio = 2.5f; } },
                    { "Drift", [] (ReeseSettings& s) { s.drift = 0.5f; } },
                    { "all of them", [] (ReeseSettings& s) { s.fmtOn = true; s.bloom = 1.0f; s.split = true; s.shp = 1; s.cp = 2; s.cpStages = 12; s.wave = 3; s.stack = 2; s.fm = 0.5f; s.sync = true; s.drift = 0.5f; } } };
                double worst = 0;
                for (auto& pp : parts) { auto s2 = base; pp.f (s2); const double c = noteCost (s2) - b0; worst = std::max (worst, c); rl << " " << pp.n << " +" << juce::String (c, 2); }
                std::cout << rl << std::endl;
                CHECK (worst < 32.0, juce::String ("the Reese with everything on stays under 32% of a core more (8 notes) at ") + qn[q]);
            }
        }
        // ---- CPU: each new effect and each Reese part at each quality (8 notes; added to the same patch without it)
        {
            auto cpuOf = [&] (std::function<void (MegaSynthProcessor&)> setup, bool reese, int quality)
            {
                auto p = make(); setP (*p, P_quality, (float) quality);
                if (reese) { setP (*p, P_osc1Gain, 0.0f); setP (*p, P_rsLevel, 0.8f); setP (*p, P_rsVoices, 2.0f); }
                if (setup) setup (*p);
                render (*p, 0.3, {}); juce::Thread::sleep (100);
                double best = 1.0e9;
                for (int run = 0; run < 3; ++run)   // (the quickest of three: less noise from whatever else the machine is doing)
                {
                    double secs = 0;
                    tg::resetVoiceRandom();
                    render (*p, 2.0, chord (0.0, 1.9, { 36, 43, 48, 52, 55, 60, 64, 67 }), nullptr, &secs);
                    best = std::min (best, secs / 2.0 * 100.0);
                }
                return best;
            };
            juce::String line;
            for (int q = 0; q < 3; ++q)
            {
                static const char* qn[3] = { "Eco", "Normal", "High" };
                const double base = cpuOf (nullptr, false, q);
                line << "  " << qn[q] << " (plain " << juce::String (base, 1) << "%):";
                struct E { const char* n; int slot; };
                for (auto e : { E { "Dub", FS_Dub }, E { "Spring", FS_Spring }, E { "Gated", FS_Gated }, E { "Plate", FS_Plate }, E { "Widen", FS_Widen }, E { "Dim", FS_Dim }, E { "IR", FS_Ir } })
                {
                    const double c = cpuOf ([&] (MegaSynthProcessor& p) { setP (p, fxOnParam (e.slot), 1.0f); if (e.slot == FS_Ir) { p.updateIrImpulse (true); juce::Thread::sleep (300); } }, false, q);
                    line << " " << e.n << " +" << juce::String (c - base, 1);
                    CHECK (c - base < 10.0, juce::String (e.n) + " costs under 10% of a core at " + qn[q]);
                }
                const double rb = cpuOf (nullptr, true, q);
                line << "\n    Reese (8 notes, 4 voices) " << juce::String (rb, 1) << "%:";
                struct Rp { const char* n; std::function<void (MegaSynthProcessor&)> f; };
                std::vector<Rp> parts = {
                    { "Formant", [] (MegaSynthProcessor& p) { setP (p, P_rsFmtOn, 1.0f); setP (p, P_rsFmtMove, 1.0f); } },
                    { "Bloom", [] (MegaSynthProcessor& p) { setP (p, P_rsBloom, 1.0f); } },
                    { "Split", [] (MegaSynthProcessor& p) { setP (p, P_rsSplitOn, 1.0f); } },
                    { "Shaper", [] (MegaSynthProcessor& p) { setP (p, P_rsShpType, 1.0f); } },
                    { "Comb", [] (MegaSynthProcessor& p) { setP (p, P_rsCpMode, 1.0f); } },
                    { "Phaser", [] (MegaSynthProcessor& p) { setP (p, P_rsCpMode, 2.0f); setP (p, P_rsCpStages, 2.0f); } },
                    { "Hoover", [] (MegaSynthProcessor& p) { setP (p, P_rsWave, 3.0f); setP (p, P_rsStack, 2.0f); } },
                    { "FM+Sync", [] (MegaSynthProcessor& p) { setP (p, P_rsFm, 0.5f); setP (p, P_rsSync, 1.0f); } },
                    { "Drift", [] (MegaSynthProcessor& p) { setP (p, P_rsDrift, 0.5f); } } };
                for (auto& rp : parts)
                {
                    const double c = cpuOf ([&] (MegaSynthProcessor& p) { setP (p, P_rsLevel, 0.8f); setP (p, P_osc1Gain, 0.0f); setP (p, P_rsVoices, 2.0f); rp.f (p); }, true, q);
                    line << " " << rp.n << " +" << juce::String (c - rb, 1);
                    CHECK (c - rb < 15.0, juce::String ("Reese ") + rp.n + " costs under 15% of a core more at " + qn[q]);
                }
                line << "\n";
            }
            std::cout << "  CPU (% of one core, added):\n" << line;
        }
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_SPACEONLY", {}).isNotEmpty())
    {
        spaceTests();
        std::cout << (failures == 0 ? "SPACE TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_BREAKONLY", {}).isNotEmpty())
    {
        breakTests();
        std::cout << (failures == 0 ? "BREAK TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_GRANONLY", {}).isNotEmpty())
    {
        granTests();
        std::cout << (failures == 0 ? "GRAN TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_PERFONLY", {}).isNotEmpty())
    {
        perfTests();
        std::cout << (failures == 0 ? "PERF TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_CHARONLY", {}).isNotEmpty())
    {
        charTests();
        std::cout << (failures == 0 ? "CHAR TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // ---- the tab layout: every control kept, pages found by name (old names too), Overview links
    auto layoutTests = [&]
    {
        std::cout << "Tab layout" << std::endl;
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 890);
        auto* e = dynamic_cast<MegaSynthEditor*> (ed.get());
        CHECK (e != nullptr, "editor");
        if (e == nullptr) return;
        std::set<juce::String> ids;
        std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
        {
            if (auto* k = dynamic_cast<tgui::Knob*> (c)) ids.insert (kParamIds[k->paramIndex]);
            if (auto* sg = dynamic_cast<tgui::Segmented*> (c); sg != nullptr && sg->getParam() >= 0) ids.insert (kParamIds[sg->getParam()]);
            if (c->getProperties().contains ("megaParam")) ids.insert (kParamIds[(int) c->getProperties()["megaParam"]]);
            if (c->getProperties().contains ("megaParam2")) ids.insert (kParamIds[(int) c->getProperties()["megaParam2"]]);   // (the Filter list: model and character)
            if (auto* xy = dynamic_cast<tgui::PerfXY*> (c)) { ids.insert (kParamIds[xy->px()]); ids.insert (kParamIds[xy->py()]); }
            for (auto* ch : c->getChildren()) walk (ch);
        };
        walk (ed.get());
        std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
            for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
            return nullptr;
        };
        auto* tabs = findTabs (ed.get());
        if (tabs != nullptr)
            for (int t = 0; t < tabs->getNumTabs(); ++t)
                if (auto* pg = tabs->getTabContentComponent (t); pg != nullptr && pg->getParentComponent() == nullptr) walk (pg);
        juce::StringArray before;
        before.addLines (juce::File (__FILE__).getSiblingFile ("ui_controls_v0.1.71.txt").loadFileAsString());
        before.removeEmptyStrings();
        juce::StringArray lost;
        // (v0.1.69: the Fill Key and Fill Length are replaced by the Fill pad and Size, as asked; they're kept, hidden, for older projects)
        for (auto& id : before) if (ids.count (id) == 0 && id != "bsFillKey" && id != "bsFillLen") lost.add (id);
        std::cout << "  " << before.size() << " parameters had a control in v0.1.71 (Performance FX, the Granular Synth, the Break Chopper / Sequencer, three breaks, chop mode, Marker Snap, the pattern tools, fills, the one Filter list, the routes, Akai Stretch, the Reese engine, Break React, the Core A / B slots, Core Envelope, Key Entry, Slice Keys and the Break React rows added, the Breaks tab, Break 4, the layer sequencer, the Fill Pattern and pad, Fixed velocity and Break React Sequencer added; the Fill Key and Fill Length replaced by the pad and Size, the v0.1.70 Space effects, ducking and Reese additions added, the Snare Roll settings and Edit pair added; v0.1.40's 466 all kept); now " << (int) ids.size() << std::endl;
        CHECK (before.size() > 400 && lost.isEmpty(), "every parameter keeps its control" + (lost.isEmpty() ? juce::String() : ": lost " + lost.joinIntoString (", ")));
        // the tabs, in signal-flow order
        CHECK (tabs != nullptr && tabs->getTabNames() == juce::StringArray ({ "Perform", "Overview", "Oscillators", "Breaks", "Mixer", "Filters", "Envelopes",
                                                                             "FX & Mutation", "Modulation", "Sequencers", "Lab" }), "eleven tabs in signal-flow order (Breaks after Oscillators)");
        // every page can be shown by name, and only that sub-page is visible
        for (auto& name : e->allPageNames())
        {
            CHECK (e->showPage (name) && e->currentPageName() == name, "show page " + name);
        }
        for (auto old : { "Effects", "Filter & Env", "Env+", "Mutation", "Macros/Scenes", "DNA Seq", "Sequencer", "LFOs", "Mod Matrix", "Unison" })
            CHECK (e->showPage (old), juce::String ("old tab name still opens a page: ") + old);
        CHECK (! e->showPage ("No Such Page"), "unknown page name");
        // every Overview block opens a page that exists
        for (int b = 0; b < 11; ++b) CHECK (e->allPageNames().contains (tgui::SignalFlowView::pageForBox (b)), "Overview block " + juce::String (b) + " has a page");
        // "Show in Mod Matrix" lands on the Mod Matrix page
        p->addRoute (MS_Lfo1, P_filterCutoff, 0.3f);
        e->showPage ("Perform");
        e->showRoute (0);
        CHECK (e->currentPageName() == "Mod Matrix", "Show in Mod Matrix opens Modulation > Mod Matrix");
        // section titles are short; their explanations sit behind an (i)
        int infos = 0; juce::StringArray longTitles;
        std::function<void (juce::Component*)> walk2 = [&] (juce::Component* c)
        {
            if (auto* sct = dynamic_cast<tgui::Section*> (c))
            {
                if (sct->getInfo().isNotEmpty()) ++infos;
                if (sct->getTitle().contains ("  (") || sct->getTitle().length() > 40) longTitles.add (sct->getTitle());
            }
            for (auto* ch : c->getChildren()) walk2 (ch);
        };
        walk2 (ed.get());
        if (tabs != nullptr) for (int t = 0; t < tabs->getNumTabs(); ++t) if (auto* pg = tabs->getTabContentComponent (t); pg != nullptr && pg->getParentComponent() == nullptr) walk2 (pg);
        std::cout << "  sections with an (i): " << infos << std::endl;
        CHECK (infos >= 20 && longTitles.isEmpty(), "short section titles" + (longTitles.isEmpty() ? juce::String() : ": " + longTitles.joinIntoString (" | ")));
        // the open tab and sub-tabs come back with the project
        e->showPage ("Dirt"); e->showPage ("Arp / Chord"); e->showPage ("Wavetables");
        e->timerCallbackForTests();
        ed.reset();
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (q->createEditor());
        auto* e2 = dynamic_cast<MegaSynthEditor*> (ed2.get());
        CHECK (e2 != nullptr && e2->currentPageName() == "Wavetables", "the project reopens on the tab it was on");
        if (e2 != nullptr)
        {
            e2->showPage ("FX & Mutation");   // the tab, not a sub-tab: its last sub-tab
            CHECK (e2->currentPageName() == "Dirt", "each tab keeps its last sub-tab");
            e2->showPage ("Sequencers");
            CHECK (e2->currentPageName() == "Arp / Chord", "each tab keeps its last sub-tab (Sequencers)");
        }
        auto o = make(); std::unique_ptr<juce::AudioProcessorEditor> ed3 (o->createEditor());
        CHECK (dynamic_cast<MegaSynthEditor*> (ed3.get())->currentPageName() == "Perform", "a new project opens on Perform");
    };
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_UIONLY", {}).isNotEmpty())
    {
        layoutTests();
        std::cout << (failures == 0 ? "UI TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_MIDIONLY", {}).isNotEmpty())
    {
        midiTests();
        std::cout << (failures == 0 ? "MIDI TESTS PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
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
        CHECK (p->exportBrowserPatch().contains ("\"version\": " + juce::String (MegaSynthProcessor::kStateVersion)), "patch version missing");
        juce::MemoryBlock st; p->getStateInformation (st);
        CHECK (getXmlFromBinaryForTest (st).contains ("stateVersion=\"" + juce::String (MegaSynthProcessor::kStateVersion) + "\""), "state version missing");

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
    midiTests();
    layoutTests();
    masterTests();
    mixerTests();
    charTests();
    perfTests();
    reeseTests();
    reactTests();
    rowTests();
    coreTests();
    keyEntryTests();
    randomTests (40);
    categoryTests();
    tg::resetVoiceRandom();   // (the bank's levels are measured from the random sequence's start, as when run on its own)
    presetBankTests();
    granTests();
    breakTests();
    spaceTests();
    hardTests();
    lfoTests();
    arpTests();
    scTests();
    uniTests();
    envTests();

    // ---- Stage 17: factory presets, quality setting, CPU budget
    {
        std::cout << "Stage 17: factory presets" << std::endl;
        const auto& list = factoryPresets();
        CHECK (list.size() == 136, "136 factory presets (36, then the jungle / drum & bass bank)");
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
            same &= q->lfoShapes.toJson() == p->lfoShapes.toJson() && q->arpStore.toJson() == p->arpStore.toJson();
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
        ed->setSize (1200, 890);
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
