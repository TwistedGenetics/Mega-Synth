#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamFormat.h"
#include "Registry.h"

using namespace tg;

namespace
{
    // Parameters that only exist in the plugin (not in the browser patch format's "params" block)
    // Parameters added with the modulation matrix (v0.2): stored in the patch's "plugin" block
    // and reset to their defaults when a patch without them is loaded.
    bool isNewPluginParam (const juce::String& id)
    {
        return id.startsWith ("lfo4") || id == "randRate" || id == "ccANum" || id == "ccBNum" || (id.startsWith ("mod") && id.endsWith ("Amt"))
            || id.startsWith ("macro") || id.startsWith ("scene") || id.startsWith ("wm") || id.startsWith ("ar");
    }

    bool isBrowserParam (const juce::String& id)
    {
        return ! (id == "velSens" || id == "bendRange" || id == "warmth" || id == "bassKeep" || id == "analogDrift" || id == "seqRun" || id == "seqClock"
                  || id.startsWith ("lfoAssign") || id.startsWith ("envAssign") || isNewPluginParam (id));
    }

    int findKey (const ChoiceList& list, const juce::String& v)
    {
        for (int i = 0; i < list.size; ++i) if (v == list.keys[i]) return i;
        for (int i = 0; i < list.size; ++i) if (v.equalsIgnoreCase (list.labels[i])) return i;
        return -1;
    }

    const ChoiceList* choiceListFor (int idx)
    {
        switch (idx)
        {
           #define TG_F(id, name, mn, mx, df, st, sk)
           #define TG_C(id, name, list, df) case P_##id: return &list;
           #define TG_B(id, name, df)
           #define TG_A(id, name, df)
            TG_PARAMS (TG_F, TG_C, TG_B, TG_A)
           #undef TG_F
           #undef TG_C
           #undef TG_B
           #undef TG_A
            default: return nullptr;
        }
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MegaSynthProcessor::createLayout (MegaSynthProcessor& proc)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const int version = 1;

    auto addFloat = [&] (int idx, const char* name, float mn, float mx, float df, float step, float skew)
    {
        juce::NormalisableRange<float> range (mn, mx, step);
        if (skew > 0) range.setSkewForCentre (skew);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kParamIds[idx], version }, name, range, df,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([idx] (float v, int) { return formatParam (idx, v); })
                .withValueFromStringFunction ([idx] (const juce::String& s) { return parseParam (idx, s); })));
    };
    auto addChoice = [&] (int idx, const char* name, const ChoiceList& list, int df)
    {
        juce::StringArray labels;
        for (int i = 0; i < list.size; ++i) labels.add (list.labels[i]);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { kParamIds[idx], version }, name, labels, df));
    };
    auto addBool = [&] (int idx, const char* name, bool df)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kParamIds[idx], version }, name, df));
    };
    auto addAmount = [&] (int idx, const char* name, float df)
    {
        const int targetIdx = idx - 1;   // the matching "...Target" parameter precedes every amount
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kParamIds[idx], version }, name, juce::NormalisableRange<float> (-1.0f, 1.0f), df,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([&proc, targetIdx] (float v, int)
                {
                    const int t = proc.raw[(size_t) targetIdx] != nullptr ? (int) proc.raw[(size_t) targetIdx]->load() : 0;
                    return formatModAmount (t, v);
                })
                .withValueFromStringFunction ([&proc, targetIdx] (const juce::String& s)
                {
                    const int t = proc.raw[(size_t) targetIdx] != nullptr ? (int) proc.raw[(size_t) targetIdx]->load() : 0;
                    return juce::jlimit (-1.0f, 1.0f, s.getFloatValue() / kTargetRange[juce::jlimit (0, (int) MT_COUNT - 1, t)]);
                })));
    };

   #define TG_F(id, name, mn, mx, df, st, sk) addFloat (P_##id, name, (float) (mn), (float) (mx), (float) (df), (float) (st), (float) (sk));
   #define TG_C(id, name, list, df) addChoice (P_##id, name, list, df);
   #define TG_B(id, name, df) addBool (P_##id, name, df);
   #define TG_A(id, name, df) addAmount (P_##id, name, (float) (df));
    TG_PARAMS (TG_F, TG_C, TG_B, TG_A)
   #undef TG_F
   #undef TG_C
   #undef TG_B
   #undef TG_A

    return layout;
}

MegaSynthProcessor::MegaSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MegaSynth", createLayout (*this))
{
    for (int i = 0; i < P_COUNT; ++i)
    {
        params[(size_t) i] = apvts.getParameter (kParamIds[i]);
        raw[(size_t) i] = apvts.getRawParameterValue (kParamIds[i]);
        jassert (params[(size_t) i] != nullptr && raw[(size_t) i] != nullptr);
    }
    formats.registerBasicFormats();
    WaveBank::get();   // build the oscillator tables up front
    engine.setModulation (&routes, &modInputs, &scenes);
    normTable();
    addListener (this);
    lastStepsVersion = steps.getVersion();
    lastRoutesVersion = routes.getVersion();
    pushHistory ("Init");
    markOriginal();
    startTimerHz (20);
}

MegaSynthProcessor::~MegaSynthProcessor()
{
    stopTimer();
    removeListener (this);
}

bool MegaSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MegaSynthProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    maxBlock = std::max (32, block);
    engine.prepare (sr, maxBlock);
    snap.sampleRate = sr;
    fillSnapshot (0);
    const double tempo = snap.fxTempo;
    engine.fx().updateImpulse (juce::jlimit (0.5, 4.5, syncSeconds (kListReverbSync, snap.i (P_reverbSync), tempo, snap.f (P_reverbSize))));
    seqRunning = false;
    seqHeldKey = -1;
    seqReleaseIn = -1;
    currentStep = -1;
}

void MegaSynthProcessor::fillSnapshot (int)
{
    for (int i = 0; i < P_COUNT; ++i) snap.v[i] = raw[(size_t) i]->load (std::memory_order_relaxed);
    if (snap.v[P_sceneMorph] > 0.5f) morphScenes (scenes, snap.v[P_sceneX], snap.v[P_sceneY], snap.v);
    snap.sampleRate = sampleRate;
    const double ht = hostTempo.load();
    snap.fxTempo = ht > 0 ? ht : (double) snap.f (P_seqTempo);
    snap.bendSemis = bendSemis;
}

void MegaSynthProcessor::setParamFromAudio (int index, float plain)
{
    auto* p = params[(size_t) index];
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void MegaSynthProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int ch = juce::jlimit (1, 16, m.getChannel());
    if (m.isNoteOn())
    {
        Voice::StartOptions o;
        o.velocity = m.getFloatVelocity();
        o.channel = ch;
        o.note = m.getNoteNumber();
        modInputs.polyAT[m.getNoteNumber() & 127] = 0.0f;
        engine.noteOn (m.getNoteNumber(), midiToFreq (m.getNoteNumber()), o, snap);
    }
    else if (m.isNoteOff())
    {
        engine.noteOff (m.getNoteNumber());
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const float v = m.getControllerValue() / 127.0f;
        if (cc == juce::roundToInt (snap.f (P_ccANum))) modInputs.ccA = v;
        if (cc == juce::roundToInt (snap.f (P_ccBNum))) modInputs.ccB = v;
        if (cc == 74) modInputs.mpeSlide[ch - 1] = v;              // MPE slide (Y)
        if (cc == 1) modInputs.wheel = v;
        if (cc == 1) setParamFromAudio (P_lfo1Depth, v);           // mod wheel -> LFO1 depth (as in the browser)
        else if (cc == 7) setParamFromAudio (P_masterVolume, v);   // volume -> master volume
        else if (cc == 120 || cc == 123) engine.allNotesOff();
    }
    else if (m.isChannelPressure())
    {
        const float v = m.getChannelPressureValue() / 127.0f;
        modInputs.aftertouch = v;
        modInputs.mpePressure[ch - 1] = v;
    }
    else if (m.isAftertouch())
    {
        modInputs.polyAT[m.getNoteNumber() & 127] = m.getAfterTouchValue() / 127.0f;
    }
    else if (m.isPitchWheel())
    {
        const float b = (float) ((m.getPitchWheelValue() - 8192) / 8192.0);
        modInputs.bend = b;
        modInputs.mpeGlide[ch - 1] = b;
        bendSemis = (float) ((m.getPitchWheelValue() - 8192) / 8192.0) * snap.f (P_bendRange);
        snap.bendSemis = bendSemis;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        engine.allNotesOff();
    }
}

void MegaSynthProcessor::seqStopHeld()
{
    if (seqHeldKey >= 0) engine.noteOff (seqHeldKey);
    // release any other sequencer voices that may still be held
    for (int k = 1000; k < 1128; ++k) if (engine.findActive (k)) engine.noteOff (k);
    seqHeldKey = -1;
    seqReleaseIn = -1;
}

void MegaSynthProcessor::seqTick (int stepIndex, double stepSeconds)
{
    currentStep = stepIndex;
    const Step d = steps.get (stepIndex);
    const float accentAmt = snap.f (P_seqAccentAmt);
    const float boost = d.accent ? accentAmt * 0.65f : 0.0f;
    const float filterAccent = d.accent ? 2200.0f * accentAmt : 0.0f;

    if (d.isRest())
    {
        if (seqHeldKey >= 0) { engine.noteOff (seqHeldKey); seqHeldKey = -1; }
        return;
    }

    const int midi = juce::jlimit (0, 127, d.note + 12 * (5 + d.oct));
    const int key = 1000 + midi;
    const double freq = midiToFreq (midi);
    Voice* held = seqHeldKey >= 0 ? engine.findActive (seqHeldKey) : nullptr;
    const bool legato = d.tie == TieTie || d.tie == TieSlide;

    if (held != nullptr && legato)
    {
        held->accentBoost = boost;
        held->filterAccent = filterAccent;
        if (d.tie == TieSlide)
        {
            held->retune (freq);
            engine.rekey (held, key);
            seqHeldKey = key;
        }
    }
    else
    {
        if (seqHeldKey >= 0) engine.noteOff (seqHeldKey);
        Voice::StartOptions o;
        o.accentBoost = boost;
        o.filterAccent = filterAccent;
        engine.noteOn (key, freq, o, snap);
        seqHeldKey = key;
    }

    if (d.tie == TieNormal)
    {
        seqReleaseIn = stepSeconds * snap.f (P_seqGate) * sampleRate;
        seqReleaseKey = seqHeldKey;
    }
}

void MegaSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    bool hostPlaying = false;
    double ppq = 0.0, bpm = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
            hostPlaying = pos->getIsPlaying();
        }
    }
    hostTempo = bpm;
    fillSnapshot (n);

    const WaveSample* wavs[2] = { currentWave[0].load(), currentWave[1].load() };

    // ---- sequencer transport
    const bool runParam = snap.i (P_seqRun) != 0;
    const bool hostClock = snap.i (P_seqClock) == 1;
    const int seqLen = juce::jlimit (1, 32, snap.i (P_seqLength));
    double stepSamples = (60.0 / std::max (1.0, (double) snap.f (P_seqTempo)) / 4.0) * sampleRate;
    int64_t hostNext = 0;

    if (! hostClock)
    {
        if (runParam && ! seqRunning) { seqRunning = true; seqStep = 0; seqSamplesToNext = 0.0; }
        if (! runParam && seqRunning) { seqRunning = false; seqStopHeld(); currentStep = -1; }
        if (seqStep >= seqLen) seqStep = 0;
    }
    else
    {
        const bool shouldRun = runParam && hostPlaying && bpm > 0.0;
        if (! shouldRun && seqRunning) { seqRunning = false; seqStopHeld(); currentStep = -1; }
        if (shouldRun)
        {
            if (! seqRunning) { seqRunning = true; }
            stepSamples = (60.0 / bpm / 4.0) * sampleRate;
            const double stepPos = ppq * 4.0;
            int64_t nextB = (int64_t) std::ceil (stepPos - 1.0e-6);
            if (! hostWasPlaying) seqStep = -1;
            if (nextB == (int64_t) seqStep && hostWasPlaying) ++nextB;   // seqStep holds the last host step ticked
            seqSamplesToNext = std::max (0.0, ((double) nextB - stepPos) * stepSamples);
            hostNext = nextB;
        }
    }
    hostWasPlaying = hostClock && seqRunning;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    juce::HeapBlock<float> monoR;
    if (R == nullptr) { monoR.calloc ((size_t) n); R = monoR.get(); }

    auto it = midi.cbegin();
    const auto end = midi.cend();
    int pos = 0;
    for (;;)
    {
        int next = n;
        if (it != end) next = std::min (next, juce::jlimit (pos, n, (*it).samplePosition));
        if (seqRunning) next = std::min (next, pos + (int) std::ceil (std::max (0.0, seqSamplesToNext)));
        if (seqReleaseIn >= 0) next = std::min (next, pos + (int) std::ceil (std::max (0.0, seqReleaseIn)));

        // render up to the next event, in chunks no larger than the prepared block size
        while (pos < next)
        {
            const int len = std::min (next - pos, maxBlock);
            engine.render (L + pos, R + pos, len, snap, wavs);
            pos += len;
            if (seqRunning) seqSamplesToNext -= len;
            if (seqReleaseIn >= 0) seqReleaseIn -= len;
        }

        // events at this position: gate release, then MIDI, then the next step
        if (seqReleaseIn >= 0 && seqReleaseIn <= 0.5)
        {
            if (seqReleaseKey >= 0 && seqReleaseKey == seqHeldKey) { engine.noteOff (seqHeldKey); seqHeldKey = -1; }
            seqReleaseIn = -1;
        }
        while (it != end && (*it).samplePosition <= pos)
        {
            handleMidi ((*it).getMessage());
            ++it;
        }
        if (seqRunning && seqSamplesToNext <= 0.5)
        {
            if (hostClock)
            {
                const int idx = (int) (((hostNext % seqLen) + seqLen) % seqLen);
                seqTick (idx, stepSamples / sampleRate);
                seqStep = (int) hostNext;
                ++hostNext;
            }
            else
            {
                seqTick (seqStep, stepSamples / sampleRate);
                seqStep = (seqStep + 1) % seqLen;
            }
            seqSamplesToNext += stepSamples;
        }
        if (pos >= n) break;
    }

    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);

    // safety: never pass NaN/inf to the host
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) { engine.reset(); buffer.clear(); return; }
    }
}

//==============================================================================
void MegaSynthProcessor::timerCallback()
{
    // history: record a snapshot shortly after an edit finishes (knob released, step changed)
    syncSceneEdits();
    if (! restoring)
    {
        const auto sv = scenes.version.load();
        if (sv != lastScenesVersion) { lastScenesVersion = sv; snapshotPending = true; snapshotDelay = 0; }
        const auto v = steps.getVersion();
        if (v != lastStepsVersion) { lastStepsVersion = v; snapshotPending = true; snapshotDelay = 0; }
        const auto rv = routes.getVersion();
        if (rv != lastRoutesVersion) { lastRoutesVersion = rv; snapshotPending = true; snapshotDelay = 0; }
        if (snapshotPending && ++snapshotDelay >= 6)
        {
            snapshotDelay = 0;
            snapshotPending = false;
            pushHistory ("Edit");
        }
    }

    // rebuild the reverb impulse when its (possibly tempo-synced) size changes
    const double ht = hostTempo.load();
    const double tempo = ht > 0 ? ht : (double) raw[P_seqTempo]->load();
    const int syncIdx = (int) raw[P_reverbSync]->load();
    engine.fx().updateImpulse (juce::jlimit (0.5, 4.5, syncSeconds (kListReverbSync, syncIdx, tempo, raw[P_reverbSize]->load())));

    // Euclidean mode: re-flow the existing notes onto the new pattern when pulses/rotate/length change
    const int cur[4] = { (int) raw[P_seqGenMode]->load(), juce::roundToInt (raw[P_seqEuclidPulses]->load()),
                         juce::roundToInt (raw[P_seqEuclidRotate]->load()), juce::roundToInt (raw[P_seqLength]->load()) };
    const bool changed = cur[0] != lastEuclid[0] || cur[1] != lastEuclid[1] || cur[2] != lastEuclid[2] || cur[3] != lastEuclid[3];
    if (changed && lastEuclid[0] >= 0 && cur[0] == 1)
        seqgen::euclidLiveUpdate (steps, cur[3], cur[1], cur[2], (int) raw[P_seqScaleRoot]->load(), (int) raw[P_seqScaleType]->load(), rng);
    std::copy (cur, cur + 4, lastEuclid);
}

void MegaSynthProcessor::generateRandomPhrase()
{
    const int len = juce::roundToInt (raw[P_seqLength]->load());
    const int root = (int) raw[P_seqScaleRoot]->load();
    const int scale = (int) raw[P_seqScaleType]->load();
    if ((int) raw[P_seqGenMode]->load() == 1)
        seqgen::generateEuclid (steps, len, juce::roundToInt (raw[P_seqEuclidPulses]->load()),
                                juce::roundToInt (raw[P_seqEuclidRotate]->load()), root, scale, rng);
    else
        seqgen::generatePhrase (steps, len, root, scale, rng);
}

//==============================================================================
bool MegaSynthProcessor::loadSampleFile (const juce::File& f, int slot)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return false;
    return loadSampleData (mb, f.getFileName(), slot);
}

bool MegaSynthProcessor::loadSampleData (const juce::MemoryBlock& data, const juce::String& name, int slot)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, true)));
    if (reader == nullptr || reader->lengthInSamples <= 1) return false;

    auto ws = std::make_unique<WaveSample>();
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 120.0));
    ws->numChannels = reader->numChannels > 1 ? 2 : 1;
    ws->length = len;
    ws->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    juce::AudioBuffer<float> tmp ((int) std::max (1u, reader->numChannels), len);
    reader->read (&tmp, 0, len, 0, true, true);
    float peak = 0.0f;
    for (int c = 0; c < 2; ++c)
    {
        const int src = std::min (c, tmp.getNumChannels() - 1);
        ws->ch[c].assign (tmp.getReadPointer (src), tmp.getReadPointer (src) + len);
    }
    for (int c = 0; c < ws->numChannels; ++c) peak = std::max (peak, tmp.getMagnitude (std::min (c, tmp.getNumChannels() - 1), 0, len));
    ws->peak = std::max (peak, 0.0001f);

    const juce::ScopedLock sl (waveLock);
    slot = juce::jlimit (0, 1, slot);
    waveData[slot] = data;
    waveShared[slot] = std::make_shared<const juce::MemoryBlock> (data);
    waveName[slot] = name;
    currentWave[slot].store (ws.get());
    waveKeep.push_back (std::move (ws));
    // free old samples once they're well out of use (never the two current ones)
    while (waveKeep.size() > 6)
    {
        auto it = std::find_if (waveKeep.begin(), waveKeep.end(), [this] (const auto& w)
                                { return w.get() != currentWave[0].load() && w.get() != currentWave[1].load(); });
        if (it == waveKeep.end()) break;
        waveKeep.erase (it);
    }
    return true;
}

void MegaSynthProcessor::clearSample (int slot)
{
    const juce::ScopedLock sl (waveLock);
    slot = juce::jlimit (0, 1, slot);
    currentWave[slot].store (nullptr);
    waveData[slot].reset();
    waveShared[slot].reset();
    waveName[slot].clear();
}

juce::String MegaSynthProcessor::getSampleStatus (int slot) const
{
    const juce::ScopedLock sl (waveLock);
    slot = juce::jlimit (0, 1, slot);
    const auto* w = currentWave[slot].load();
    if (w == nullptr) return "No wavetable loaded";
    return "Loaded: " + waveName[slot] + "  " + juce::String (w->duration(), 2) + "s  peak " + juce::String (w->peak, 2);
}

//==============================================================================
void MegaSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    state.setProperty ("seqSteps", steps.toString(), nullptr);
    state.setProperty ("modRoutes", juce::JSON::toString (routes.toVar(), true), nullptr);
    state.setProperty ("scenes", juce::JSON::toString (scenes.toVar(), true), nullptr);
    state.setProperty ("sceneEdit", getEditScene(), nullptr);
    state.setProperty ("macroNames", macroNamesJoined(), nullptr);
    state.setProperty ("patchName", getPatchName(), nullptr);
    {
        const juce::ScopedLock sl (waveLock);
        for (int k = 0; k < 2; ++k)
        {
            const juce::String suffix = k == 0 ? "" : "2";
            state.setProperty ("waveName" + suffix, waveName[k], nullptr);
            state.setProperty ("waveData" + suffix, waveData[k].getSize() > 0 ? juce::Base64::toBase64 (waveData[k].getData(), waveData[k].getSize()) : juce::String(), nullptr);
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void MegaSynthProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid()) return;
    // Version 1 (v0.1.x) state has no version number; its parameters, steps and samples
    // map 1:1 onto version 2, so no conversion is needed yet. Later versions migrate here.
    const int version = (int) tree.getProperty ("stateVersion", 1);
    juce::ignoreUnused (version);
    apvts.replaceState (tree);

    if (tree.hasProperty ("patchName")) setPatchName (tree.getProperty ("patchName").toString());
    const juce::String stepStr = tree.getProperty ("seqSteps").toString();
    if (stepStr.isNotEmpty()) steps.fromString (stepStr);
    routes.fromVar (juce::JSON::parse (tree.getProperty ("modRoutes").toString()));   // none in v1 state
    scenes.fromVar (juce::JSON::parse (tree.getProperty ("scenes").toString()));
    sceneEditIndex = juce::jlimit (0, 3, (int) tree.getProperty ("sceneEdit", 0));
    setMacroNamesJoined (tree.getProperty ("macroNames").toString());
    lastMorph = raw[P_sceneMorph]->load() > 0.5f;

    for (int k = 0; k < 2; ++k)
    {
        const juce::String suffix = k == 0 ? "" : "2";
        const juce::String wd = tree.getProperty ("waveData" + suffix).toString();
        juce::MemoryOutputStream mo;
        if (wd.isNotEmpty() && juce::Base64::convertFromBase64 (mo, wd))
            loadSampleData (mo.getMemoryBlock(), tree.getProperty ("waveName" + suffix).toString(), k);
        else clearSample (k);
    }

    lastEuclid[0] = -1;   // don't re-flow the restored sequence
    if (! restoring)
    {
        {
            const juce::ScopedLock sl (historyLock);
            history.clear();
            historyPos = -1;
        }
        pushHistory ("Project loaded");
        markOriginal();
    }
}

//==============================================================================
juce::String MegaSynthProcessor::exportBrowserPatch() const
{
    auto* root = new juce::DynamicObject();
    auto* p = new juce::DynamicObject();
    for (int i = 0; i < P_COUNT; ++i)
    {
        const juce::String id (kParamIds[i]);
        if (! isBrowserParam (id)) continue;
        const float v = raw[(size_t) i]->load();
        if (const auto* list = choiceListFor (i))
            p->setProperty (id, juce::String (list->keys[juce::jlimit (0, list->size - 1, (int) v)]));
        else
            p->setProperty (id, std::abs (v - std::round (v)) < 1.0e-6f ? juce::String ((int) std::round (v)) : juce::String (v, 6).trimCharactersAtEnd ("0"));
    }
    root->setProperty ("format", "megasynth");
    root->setProperty ("version", kStateVersion);
    root->setProperty ("params", juce::var (p));
    root->setProperty ("name", getPatchName());
    {
        // settings that only exist in the plugin (the browser ignores this block)
        auto* extra = new juce::DynamicObject();
        for (int i : { P_velSens, P_bendRange, P_warmth, P_bassKeep, P_analogDrift, P_seqClock })
            extra->setProperty (kParamIds[i], raw[(size_t) i]->load());
        for (int i = 0; i < P_COUNT; ++i)
            if (isNewPluginParam (kParamIds[i])) extra->setProperty (kParamIds[i], raw[(size_t) i]->load());
        root->setProperty ("plugin", juce::var (extra));
    }
    root->setProperty ("modMatrix", routes.toVar());
    root->setProperty ("scenes", scenes.toVar());
    root->setProperty ("sceneEdit", getEditScene());
    {
        juce::Array<juce::var> names;
        for (int k = 0; k < 8; ++k) names.add (getMacroName (k));
        root->setProperty ("macroNames", names);
    }

    static const char* ties[] = { "normal", "tie", "slide", "rest" };
    juce::Array<juce::var> seq;
    for (int i = 0; i < 32; ++i)
    {
        const Step s = steps.get (i);
        auto* o = new juce::DynamicObject();
        o->setProperty ("note", s.note < 0 ? juce::String ("REST") : juce::String (kNoteKeys[s.note]));
        o->setProperty ("oct", s.oct);
        o->setProperty ("tie", ties[s.tie]);
        o->setProperty ("accent", s.accent ? "accent" : "normal");
        seq.add (juce::var (o));
    }
    root->setProperty ("sequence", seq);

    auto assign = [&] (int firstParam, const char* const* srcKeys)
    {
        juce::Array<juce::var> arr;
        for (int k = 0; k < 3; ++k)
        {
            const int src = juce::jlimit (0, 2, (int) raw[(size_t) (firstParam + 3 * k)]->load());
            const int tgt = juce::jlimit (0, (int) MT_COUNT - 1, (int) raw[(size_t) (firstParam + 3 * k + 1)]->load());
            const float amt = raw[(size_t) (firstParam + 3 * k + 2)]->load() * kTargetRange[tgt];
            auto* o = new juce::DynamicObject();
            o->setProperty ("source", juce::String (srcKeys[src]));
            o->setProperty ("target", juce::String (kTargetKeys[tgt]));
            o->setProperty ("amount", amt);
            arr.add (juce::var (o));
        }
        return arr;
    };
    root->setProperty ("lfoAssignments", assign (P_lfoAssignSource0, kLfoSrcKeys));
    root->setProperty ("envAssignments", assign (P_envAssignSource0, kEnvSrcKeys));

    {
        const juce::ScopedLock sl (waveLock);
        for (int k = 0; k < 2; ++k)
        {
            const juce::Identifier key (k == 0 ? "wavetable" : "wavetable2");   // the browser only knows "wavetable"
            if (waveData[k].getSize() > 0)
            {
                auto* w = new juce::DynamicObject();
                const bool isWav = waveName[k].endsWithIgnoreCase (".wav");
                w->setProperty ("name", waveName[k]);
                w->setProperty ("data", juce::String (isWav ? "data:audio/wav;base64," : "data:application/octet-stream;base64,")
                                            + juce::Base64::toBase64 (waveData[k].getData(), waveData[k].getSize()));
                root->setProperty (key, juce::var (w));
            }
            else root->setProperty (key, juce::var());
        }
    }
    return juce::JSON::toString (juce::var (root), true);
}

juce::String MegaSynthProcessor::importBrowserPatch (const juce::String& text)
{
    const juce::var patch = juce::JSON::parse (text.trim());
    // Browser patches and v0.1.x .megasynth files carry no "version" (= version 1).
    // They map onto version 2 unchanged; future versions add their migration here.
    if (! patch.isObject()) return "That isn't a patch (expected the JSON copied by Save Patch).";

    auto setPlain = [this] (int idx, float plain)
    {
        auto* prm = params[(size_t) idx];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    };

    if (auto* p = patch["params"].getDynamicObject())
    {
        for (auto& prop : p->getProperties())
        {
            const juce::String id = prop.name.toString();
            int idx = -1;
            for (int i = 0; i < P_COUNT; ++i) if (id == kParamIds[i]) { idx = i; break; }
            if (idx < 0) continue;
            const juce::String val = prop.value.toString();
            if (const auto* list = choiceListFor (idx))
            {
                const int k = findKey (*list, val);
                if (k >= 0) setPlain (idx, (float) k);
            }
            else setPlain (idx, val.getFloatValue());
        }
    }

    if (auto* extra = patch["plugin"].getDynamicObject())
    {
        for (int i : { P_velSens, P_bendRange, P_warmth, P_bassKeep, P_analogDrift, P_seqClock })
            if (extra->hasProperty (kParamIds[i]))
                setPlain (i, (float) (double) extra->getProperty (kParamIds[i]));
    }
    // matrix settings: patches from before the matrix get it empty, so they sound as they did
    {
        auto* extra = patch["plugin"].getDynamicObject();
        for (int i = 0; i < P_COUNT; ++i)
        {
            if (! isNewPluginParam (kParamIds[i])) continue;
            if (extra != nullptr && extra->hasProperty (kParamIds[i])) setPlain (i, (float) (double) extra->getProperty (kParamIds[i]));
            else setPlain (i, meta (i).def);
        }
        routes.fromVar (patch["modMatrix"]);
        scenes.fromVar (patch["scenes"]);
        sceneEditIndex = juce::jlimit (0, 3, (int) patch.getProperty ("sceneEdit", 0));
        const juce::var names = patch["macroNames"];
        for (int k = 0; k < 8; ++k) setMacroName (k, names.isArray() && k < names.size() ? names[k].toString() : juce::String());
        lastMorph = raw[P_sceneMorph]->load() > 0.5f;
    }
    {
        const juce::String n = patch["name"].toString();
        setPatchName (n.isNotEmpty() ? n : juce::String ("Imported patch"));
    }

    auto readAssign = [&] (const juce::var& arr, int firstParam, const ChoiceList& srcList)
    {
        if (! arr.isArray()) return;
        for (int k = 0; k < 3 && k < arr.size(); ++k)
        {
            const juce::var a = arr[k];
            const int src = findKey (srcList, a["source"].toString());
            const int tgt = findKey (kListTarget, a["target"].toString());
            if (src >= 0) setPlain (firstParam + 3 * k, (float) src);
            if (tgt >= 0)
            {
                setPlain (firstParam + 3 * k + 1, (float) tgt);
                const float amount = (float) (double) a["amount"];
                setPlain (firstParam + 3 * k + 2, juce::jlimit (-1.0f, 1.0f, amount / kTargetRange[tgt]));
            }
        }
    };
    readAssign (patch["lfoAssignments"], P_lfoAssignSource0, kListLfoSrc);
    readAssign (patch["envAssignments"], P_envAssignSource0, kListEnvSrc);

    const juce::var seq = patch["sequence"];
    if (seq.isArray())
    {
        for (int i = 0; i < 32 && i < seq.size(); ++i)
        {
            const juce::var s = seq[i];
            Step st;
            const juce::String note = s["note"].toString();
            st.note = note == "REST" ? -1 : findKey (kListNote, note);
            st.oct = juce::jlimit (-2, 2, s["oct"].toString().getIntValue());
            const juce::String tie = s["tie"].toString();
            st.tie = tie == "tie" ? TieTie : tie == "slide" ? TieSlide : tie == "rest" ? TieRest : TieNormal;
            st.accent = s["accent"].toString() == "accent";
            steps.set (i, st);
        }
    }

    juce::String sampleError;
    for (int k = 0; k < 2; ++k)
    {
        const juce::var wt = patch[k == 0 ? "wavetable" : "wavetable2"];
        if (wt.isObject())
        {
            const juce::String b64 = wt["data"].toString().fromFirstOccurrenceOf ("base64,", false, false);
            juce::MemoryOutputStream mo;
            if (b64.isEmpty() || ! juce::Base64::convertFromBase64 (mo, b64) || ! loadSampleData (mo.getMemoryBlock(), wt["name"].toString(), k))
                sampleError = "Patch loaded, but a wavetable sample couldn't be decoded.";
        }
        else clearSample (k);
    }
    if (sampleError.isNotEmpty()) { lastEuclid[0] = -1; return sampleError; }

    lastEuclid[0] = -1;
    return {};
}

void MegaSynthProcessor::resetToDefaults()
{
    for (auto* p : params)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
    tg::StepStore fresh;
    for (int i = 0; i < 32; ++i) steps.set (i, fresh.get (i));
    routes.clearAll();
    scenes.clear();
    sceneEditIndex = 0;
    for (int k = 0; k < 8; ++k) setMacroName (k, {});
    lastMorph = false;
    clearSample (0);
    clearSample (1);
    lastEuclid[0] = -1;
    setPatchName ("Init");
    pushHistory ("Init");
    markOriginal();
}

juce::File MegaSynthProcessor::getPatchFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Mega Synth").getChildFile ("Patches");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> MegaSynthProcessor::getPatchFiles()
{
    auto files = getPatchFolder().findChildFiles (juce::File::findFiles, true, "*.megasynth");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getRelativePathFrom (getPatchFolder()).compareNatural (b.getRelativePathFrom (getPatchFolder())) < 0; });
    return files;
}

//==============================================================================
MegaSynthProcessor::Snapshot MegaSynthProcessor::captureSnapshot (const juce::String& label)
{
    Snapshot s;
    s.params = apvts.copyState();
    s.steps = steps.toString();
    s.routes = juce::JSON::toString (routes.toVar(), true);
    s.scenes = juce::JSON::toString (scenes.toVar(), true);
    s.sceneEdit = getEditScene();
    s.macroNames = macroNamesJoined();
    s.patchName = getPatchName();
    s.label = label;
    const juce::ScopedLock sl (waveLock);
    for (int k = 0; k < 2; ++k) { s.wave[k] = waveShared[k]; s.waveName[k] = waveName[k]; }
    return s;
}

bool MegaSynthProcessor::sameState (const Snapshot& a, const Snapshot& b) const
{
    return a.steps == b.steps && a.routes == b.routes && a.scenes == b.scenes && a.macroNames == b.macroNames
        && a.sceneEdit == b.sceneEdit && a.patchName == b.patchName
        && a.wave[0] == b.wave[0] && a.wave[1] == b.wave[1]
        && a.params.isEquivalentTo (b.params);
}

void MegaSynthProcessor::restoreSnapshot (const Snapshot& s)
{
    restoring = true;
    apvts.replaceState (s.params.createCopy());
    steps.fromString (s.steps);
    routes.fromVar (juce::JSON::parse (s.routes));
    lastRoutesVersion = routes.getVersion();
    scenes.fromVar (juce::JSON::parse (s.scenes));
    lastScenesVersion = scenes.version.load();
    sceneEditIndex = s.sceneEdit;
    setMacroNamesJoined (s.macroNames);
    lastMorph = raw[P_sceneMorph]->load() > 0.5f;
    setPatchName (s.patchName);
    for (int k = 0; k < 2; ++k)
    {
        if (s.wave[k] == waveShared[k]) continue;
        if (s.wave[k] != nullptr)
        {
            loadSampleData (*s.wave[k], s.waveName[k], k);
            const juce::ScopedLock sl (waveLock);
            waveShared[k] = s.wave[k];   // keep pointer identity with the snapshot
        }
        else clearSample (k);
    }
    lastStepsVersion = steps.getVersion();
    snapshotPending = false;
    lastEuclid[0] = -1;
    restoring = false;
}

void MegaSynthProcessor::pushHistory (const juce::String& label)
{
    const juce::ScopedLock sl (historyLock);
    auto snap = captureSnapshot (label);
    if (historyPos >= 0 && sameState (snap, history[(size_t) historyPos])) return;
    history.resize ((size_t) (historyPos + 1));
    history.push_back (std::move (snap));
    while (history.size() > 200) history.erase (history.begin());
    historyPos = (int) history.size() - 1;
}

void MegaSynthProcessor::markOriginal()
{
    const juce::ScopedLock sl (historyLock);
    original = captureSnapshot ("Original");
}

void MegaSynthProcessor::undo()
{
    const juce::ScopedLock sl (historyLock);
    pushHistory ("Edit");   // keep any change made since the last snapshot
    if (historyPos <= 0) return;
    --historyPos;
    restoreSnapshot (history[(size_t) historyPos]);
}

void MegaSynthProcessor::redo()
{
    const juce::ScopedLock sl (historyLock);
    if (historyPos + 1 >= (int) history.size()) return;
    ++historyPos;
    restoreSnapshot (history[(size_t) historyPos]);
}

void MegaSynthProcessor::returnToOriginal()
{
    const juce::ScopedLock sl (historyLock);
    pushHistory ("Edit");
    restoreSnapshot (original);
    pushHistory ("Return to original");
}

bool MegaSynthProcessor::savePatchToFile (const juce::File& f)
{
    setPatchName (f.getFileNameWithoutExtension());
    return f.replaceWithText (exportBrowserPatch());
}

juce::String MegaSynthProcessor::loadPatchFromFile (const juce::File& f)
{
    const auto text = f.loadFileAsString();
    if (text.isEmpty()) return "Couldn't read " + f.getFileName();
    const auto err = importBrowserPatch (text);
    setPatchName (f.getFileNameWithoutExtension());
    pushHistory ("Load " + f.getFileNameWithoutExtension());
    markOriginal();
    return err;
}

int MegaSynthProcessor::addRoute (int src, int destParam, float depth)
{
    const int slot = routes.firstFree();
    if (slot < 0) return -1;
    RouteConfig c;
    c.on = true; c.src = src; c.dst = destParam;
    routes.set (slot, c);
    auto* p = params[(size_t) (P_mod1Amt + slot)];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-1.0f, 1.0f, depth)));
    p->endChangeGesture();
    return slot;
}

void MegaSynthProcessor::clearRoute (int slot)
{
    if (slot < 0 || slot >= kNumRoutes) return;
    routes.clear (slot);
    auto* p = params[(size_t) (P_mod1Amt + slot)];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    p->endChangeGesture();
}

//==============================================================================
void MegaSynthProcessor::storeScene (int k)
{
    if (k < 0 || k > 3) return;
    float v[P_COUNT];
    for (int i = 0; i < P_COUNT; ++i) v[i] = raw[(size_t) i]->load();
    scenes.store (k, v);
}

void MegaSynthProcessor::recallScene (int k)
{
    if (k < 0 || k > 3 || ! scenes.stored[k].load()) return;
    const auto& nt = normTable();
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (! nt.scene[i]) continue;
        auto* p = params[(size_t) i];
        const float nv = p->convertTo0to1 (scenes.plainValue (k, i));
        if (std::abs (nv - p->getValue()) < 1.0e-7f) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost (nv);
        p->endChangeGesture();
    }
}

void MegaSynthProcessor::editScene (int k)
{
    if (k < 0 || k > 3) return;
    syncSceneEdits();                       // keep any pending edit in the scene being left
    if (! scenes.stored[k].load()) storeScene (k);
    sceneEditIndex = k;
    recallScene (k);
}

void MegaSynthProcessor::clearScenes()
{
    scenes.clear();
    auto* m = params[P_sceneMorph];
    m->beginChangeGesture(); m->setValueNotifyingHost (0.0f); m->endChangeGesture();
    lastMorph = false;
}

void MegaSynthProcessor::syncSceneEdits()
{
    const bool morph = raw[P_sceneMorph]->load() > 0.5f;
    if (morph && ! lastMorph)
    {
        // morph switched on: empty scenes start as the current sound, and the panel shows the edited scene
        for (int k = 0; k < 4; ++k) if (! scenes.stored[k].load()) storeScene (k);
        lastMorph = true;
        recallScene (getEditScene());
        return;
    }
    lastMorph = morph;
    if (! morph) return;
    const int k = getEditScene();
    const auto& nt = normTable();
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (! nt.scene[i]) continue;
        const float plain = raw[(size_t) i]->load();
        if (std::abs (normFast (nt, i, plain) - scenes.v[k][i].load()) > 1.0e-6f) scenes.storeOne (k, i, plain);
    }
}

juce::String MegaSynthProcessor::getMacroName (int k) const
{
    const juce::ScopedLock sl (nameLock);
    k = juce::jlimit (0, 7, k);
    return macroNames[k].isNotEmpty() ? macroNames[k] : "Macro " + juce::String (k + 1);
}

void MegaSynthProcessor::setMacroName (int k, const juce::String& n)
{
    const juce::ScopedLock sl (nameLock);
    k = juce::jlimit (0, 7, k);
    macroNames[k] = (n == "Macro " + juce::String (k + 1)) ? juce::String() : n.trim().substring (0, 24).removeCharacters ("|");
}

juce::String MegaSynthProcessor::macroNamesJoined() const
{
    const juce::ScopedLock sl (nameLock);
    juce::StringArray a;
    for (auto& n : macroNames) a.add (n);
    return a.joinIntoString ("|");
}

void MegaSynthProcessor::setMacroNamesJoined (const juce::String& s)
{
    juce::StringArray a;
    a.addTokens (s, "|", {});
    for (int k = 0; k < 8; ++k) setMacroName (k, k < a.size() ? a[k] : juce::String());
}

juce::AudioProcessorEditor* MegaSynthProcessor::createEditor()
{
    return new MegaSynthEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MegaSynthProcessor();
}
