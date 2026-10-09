// Offline test: renders the synth without a host and checks the output is sane.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ParamFormat.h"

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

        // state round trip keeps the sample and sequence
        p->steps.set (5, Step { 7, 1, TieSlide, true });
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (q->getSampleStatus().startsWith ("Loaded"), "sample not restored from state");
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

    // ---- 8. editor snapshots (offscreen) for every tab
    if (juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_SNAPSHOTS", {}).isNotEmpty())
    {
        std::cout << "Editor snapshots" << std::endl;
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (1200, 800);
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
