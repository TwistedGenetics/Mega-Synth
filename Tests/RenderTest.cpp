// Offline test: renders the synth without a host and checks the output is sane.
#include <JuceHeader.h>
#include <set>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamFormat.h"
#include "Registry.h"
#include "Mut/Mutator.h"

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
        CHECK (std::abs (fromNormalised (P_filterCutoff, 0.5f) - 1200.0f) < 1.0f, "log scaling centre");

        auto p = make();
        CHECK (p->exportBrowserPatch().contains ("\"version\": 2"), "patch version missing");
        juce::MemoryBlock st; p->getStateInformation (st);
        CHECK (getXmlFromBinaryForTest (st).contains ("stateVersion=\"2\""), "state version missing");

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
                std::cout << "  centroid: saw " << cs << ", tilt +1 " << ct << ", formant +12 " << cf << " (pitch " << fp << " Hz)" << std::endl;
                CHECK (ct > 1.3 * cs && cf > 1.2 * cs, "tilt / formant");
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

    // ---- Stage 17: factory presets, quality setting, CPU budget
    {
        std::cout << "Stage 17: factory presets" << std::endl;
        const auto& list = factoryPresets();
        CHECK (list.size() == 18, "18 factory presets");
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
