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
    // Parameters added with the filter overhaul (state version 3)
    bool isFilterV2Param (const juce::String& id)
    {
        return id == "filterType" || id == "filterSlope" || id == "filterMix" || id == "filterKeyTrack" || id == "filterLfoAmt" || id == "filterLfoSrc"
            || id.startsWith ("filter2") || id == "filterRouting" || id == "filterBalance" || id == "filterStereoSplit";
    }

    // Parameters added with the effects rack (state version 6)
    bool isFxV2Param (const juce::String& id)
    {
        for (auto* p : { "fxOn", "fxMix", "mbc", "smp", "rpt", "flp", "vsh", "stt" }) if (id.startsWith (p)) return true;
        return false;
    }

    // Parameters added with LFO sync and custom shapes (state version 7)
    bool isLfoV2Param (const juce::String& id)
    {
        if (! id.startsWith ("lfo") || id.length() < 5 || ! juce::CharacterFunctions::isDigit (id[3])) return false;
        const auto rest = id.substring (4);
        for (auto* x : { "Sync", "Retrig", "Phase", "Mode", "Smooth", "Uni", "Fade", "Custom" }) if (rest == x) return true;
        return false;
    }

    bool isNewPluginParam (const juce::String& id)
    {
        return id.startsWith ("lfo4") || id == "randRate" || id == "ccANum" || id == "ccBNum" || (id.startsWith ("mod") && id.endsWith ("Amt"))
            || id.startsWith ("macro") || id.startsWith ("scene") || id.startsWith ("wm") || id.startsWith ("ar") || id.startsWith ("dna") || id.startsWith ("res") || id.startsWith ("gr") || id.startsWith ("sp") || id.startsWith ("bus") || id.startsWith ("fb") || id.startsWith ("cap") || id.startsWith ("ci") || id.startsWith ("mut") || id.startsWith ("ds") || id == "quality"
            || isFilterV2Param (id) || isFxV2Param (id) || isLfoV2Param (id) || id.startsWith ("chd") || id.startsWith ("sch") || id.startsWith ("uni") || id.startsWith ("pEnv") || id.startsWith ("mseg")
            || id == "wheelLfo1" || id == "sustainPedal" || id.startsWith ("mf") || id == "outGain" || id.startsWith ("mx") || id.startsWith ("flt") || id.startsWith ("pf") || id.startsWith ("gs") || id.startsWith ("bc") || id.startsWith ("bs") || id.startsWith ("rs") || id.startsWith ("rx");
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
        if (skew < 0) range = logRange (mn, mx, step);
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
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),   // off unless the host routes something in
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
    engine.setDnaSequencer (&dnaSteps);
    engine.fx().setRack (&fxRack);
    engine.setLfoShapes (&lfoShapes);
    engine.setMsegs (&msegs);
    engine.setBreakStore (&breakStore);
    for (auto& c : ccMap) c.store (-1);
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
    const auto in = layouts.getMainInputChannelSet();   // the sidechain (input bus 0)
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void MegaSynthProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    maxBlock = std::max (32, block);
    engine.prepare (sr, maxBlock);
    scBuf.setSize (2, maxBlock, false, true, false);
    updateLatency();
    capRecording = false; capStopReq = false; capPos = 0;
    capBuf.setSize (2, (int) (kCaptureSeconds * sr) + 8, false, true, false);
    grainLoad.setSize (2, (int) (4.0 * sr), false, true, false);
    snap.sampleRate = sr;
    fillSnapshot (0);
    const double tempo = snap.fxTempo;
    engine.fx().updateImpulse (juce::jlimit (0.5, 4.5, syncSeconds (kListReverbSync, snap.i (P_reverbSync), tempo, snap.f (P_reverbSize))));
    sustained.fill (false); sustainDown = false;
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
    bendSemis = bendNorm * snap.f (P_bendRange);
    snap.bendSemis = bendSemis;
}

bool MegaSynthProcessor::dnaGateRouted() const
{
    for (int i = 0; i < tg::kNumRoutes; ++i)
    {
        const auto r = routes.get (i);
        if (r.active() && (r.src == MS_DnaGate || r.via == MS_DnaGate)) return true;
    }
    return false;
}

void MegaSynthProcessor::midiLearn (int param) { learnParam.store (param); midiMapVersion.fetch_add (1); }
void MegaSynthProcessor::midiForget (int param)
{
    for (auto& c : ccMap) { int expect = param; c.compare_exchange_strong (expect, -1); }
    if (learnParam.load() == param) learnParam.store (-1);
    midiMapVersion.fetch_add (1);
}
int MegaSynthProcessor::ccForParam (int param) const
{
    for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() == param) return c;
    return -1;
}
juce::String MegaSynthProcessor::midiMapToString() const
{
    juce::StringArray a;
    for (int c = 0; c < 128; ++c) { const int p = ccMap[(size_t) c].load(); if (p >= 0 && p < P_COUNT) a.add (juce::String (c) + ":" + kParamIds[p]); }
    return a.joinIntoString (",");
}
void MegaSynthProcessor::midiMapFromString (const juce::String& s)
{
    for (auto& c : ccMap) c.store (-1);
    juce::StringArray a; a.addTokens (s, ",", {});
    for (auto& e : a)
    {
        const int cc = e.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int p = tg::indexForId (e.fromFirstOccurrenceOf (":", false, false));
        if (cc >= 0 && cc < 120 && p >= 0) ccMap[(size_t) cc].store (p);
    }
    midiMapVersion.fetch_add (1);
}

// ---- chord memory and arpeggiator
void MegaSynthProcessor::startVoice (int key, int note, float vel, int ch, float accent)
{
    dnaClock.noteOn (midiPos);
    engine.fx().shaperNoteOn (fxBeatStart + fxBeatInc * midiPos);
    Voice::StartOptions o;
    o.velocity = vel;
    o.channel = ch;
    o.note = note;
    if (accent > 0.0f) { o.accentBoost = 0.35f * accent; o.filterAccent = 1500.0f * accent; }
    modInputs.polyAT[note & 127] = 0.0f;
    engine.noteOn (key, midiToFreq (note), o, snap);
}

void MegaSynthProcessor::routeNoteOn (int note, float vel, int ch)
{
    note &= 127;
    if (arpStore.learning.load()) { learnNotes.push_back (note); ++learnHeld; }
    // chord memory: the key becomes the chord
    int tones[8]; int n = 1; tones[0] = note;
    const bool chord = snap.i (P_chdOn) != 0;
    if (chord)
    {
        const auto iv = tg::chordIntervals (juce::jlimit (0, 8, snap.i (P_chdPreset)), arpStore,
                                            juce::jlimit (0, 3, snap.i (P_chdSpread)), juce::jlimit (-3, 3, snap.i (P_chdInversion)));
        n = 0;
        for (int x : iv) if (n < 8 && note + x >= 0 && note + x <= 127) tones[n++] = note + x;
        if (n == 0) { tones[0] = note; n = 1; }
    }
    chordCount[(size_t) note] = n;
    for (int j = 0; j < n; ++j) chordTones[(size_t) note][(size_t) j] = tones[j];
    if (snap.i (P_arpOn) != 0)
    {
        noteRoute[(size_t) note] = 1;
        for (int j = 0; j < n; ++j) arp.keyDown (tones[j], vel, fxBeatStart + fxBeatInc * midiPos);
        return;
    }
    noteRoute[(size_t) note] = chord ? 2 : 0;
    if (! chord) { startVoice (note, note, vel, ch, 0.0f); return; }   // as before
    engine.polyFloor = n;
    for (int j = 0; j < n; ++j) startVoice (2000 + note * 16 + j, tones[j], vel, ch, 0.0f);
    engine.polyFloor = 0;
}

void MegaSynthProcessor::sustainRelease()
{
    for (int k = 0; k < 128; ++k)
        if (sustained[(size_t) k]) { sustained[(size_t) k] = false; routeNoteOff (k); }
}

void MegaSynthProcessor::routeNoteOff (int note)
{
    note &= 127;
    if (arpStore.learning.load() && learnHeld > 0 && --learnHeld == 0 && ! learnNotes.empty())
    {
        // the chord just played (and let go) becomes the Learned chord
        arpStore.setLearned (learnNotes);
        learnNotes.clear();
        arpStore.learning = false;
        setParamFromAudio (P_chdPreset, 0.0f);
        setParamFromAudio (P_chdOn, 1.0f);
    }
    const int n = juce::jlimit (1, 8, chordCount[(size_t) note]);
    if (noteRoute[(size_t) note] == 1)
    {
        for (int j = 0; j < n; ++j) arp.keyUp (chordTones[(size_t) note][(size_t) j]);
        return;
    }
    if (noteRoute[(size_t) note] == 0) { engine.noteOff (note); return; }   // as before
    for (int j = 0; j < n; ++j) engine.noteOff (2000 + note * 16 + j);
}

void MegaSynthProcessor::arpTick (double beatNow)
{
    arpEvents.clear();
    arp.tick (beatNow, arpStore, [this] { arpRng ^= arpRng << 13; arpRng ^= arpRng >> 17; arpRng ^= arpRng << 5; return (float) (arpRng & 0xFFFFFF) / 16777216.0f; }, arpEvents);
    int ons = 0; for (auto& e : arpEvents) ons += e.on ? 1 : 0;
    engine.polyFloor = ons > 1 ? ons : 0;
    for (auto& e : arpEvents)
    {
        if (arpLog != nullptr) arpLog->push_back ({ beatNow, e.note, e.on, e.vel });
        if (e.on) startVoice (3000 + e.note, e.note, e.vel, 1, e.accent);
        else engine.noteOff (3000 + e.note);
    }
    engine.polyFloor = 0;
    if (ons > 0) { modInputs.arpGate = 1.0f; modInputs.arpPos = arp.position(); arpStepShown.store (arp.currentStep()); }
}

//==============================================================================
// The Break Sequencer: one step per 1/16 (odd steps pushed by Swing), locked to the beat clock
static double bsStepBeat (int64_t k, float swing) { return (double) k * 0.25 + ((k & 1) ? (double) swing * 0.125 : 0.0); }

void MegaSynthProcessor::bsStart (double beat)
{
    bsRunning = true;
    bsNumRolls = 0;
    // the step that starts at or after this beat is the next one played (play pressed mid-bar: it lands on the grid)
    bsLastStep = (int64_t) std::ceil (beat * 4.0 - 1.0e-6) - 1;
    bsChainIdx = -1;
    bsRng = 0x9E3779B9u;
    bsFillState = 0; bsOffset = 0; bsSkipChain = false; bsFillRng = 0x2545F491u;
    bsFillCountdown.store (-1); bsFillPosShown.store (-1); bsFillFirstIdx.store (-1);
    modInputs.bsFill = 0.0f;
}

static int64_t posMod (int64_t a, int64_t m) { return ((a % m) + m) % m; }

// a fill pressed (the Fill parameter, button or key) or let go
void MegaSynthProcessor::bsFillPress (bool down)
{
    static const int lens[] = { 4, 8, 16, 32 };
    if (down)
    {
        if (! bsRunning || bsFillState != 0) return;
        bsFillLen = lens[juce::jlimit (0, 3, snap.i (P_bsFillLen))];
        bsFillHeld = snap.i (P_bsFillMode) == 1;
        const int steps = snap.i (P_bsSteps) == 1 ? 32 : 16;
        const int how = juce::jlimit (0, 2, snap.i (P_bsFillStart));
        int64_t start = bsLastStep + 1;   // the next step to play
        if (how == 0) while (posMod (start, 4) != 0) ++start;                      // the next beat
        else if (how == 1) while (posMod (start, 16) != 0) ++start;                // the next bar
        else while (posMod (start - bsOffset + bsFillLen, steps) != 0) ++start;   // so it ends where the pattern does
        bsFillStart = start;
        bsFillEnd = bsFillHeld ? INT64_MAX : start + bsFillLen;
        bsFillState = 1;
        bsShowFill (bsLastStep, steps);
    }
    else if (bsFillHeld)
    {
        if (bsFillState == 1) bsFillState = 0;                    // let go before it started
        else if (bsFillState == 2) bsFillEnd = bsLastStep + 1;    // it ends with this step
    }
}

void MegaSynthProcessor::bsShowFill (int64_t k, int steps)
{
    bsFillCountdown.store (bsFillState == 1 ? (int) std::min<int64_t> (9999, bsFillStart - k) : -1, std::memory_order_relaxed);
    bsFillPosShown.store (bsFillState == 2 ? (int) std::min<int64_t> (9999, k - bsFillStart) : -1, std::memory_order_relaxed);
    bsFillLenShown.store (bsFillHeld ? 0 : bsFillLen, std::memory_order_relaxed);
    bsFillFirstIdx.store (bsFillState != 0 ? (int) posMod (bsFillStart - bsOffset, steps) : -1, std::memory_order_relaxed);
}

double MegaSynthProcessor::bsDue() const
{
    double due = bsStepBeat (bsLastStep + 1, juce::jlimit (0.0f, 0.75f, snap.f (P_bsSwing)));
    for (int i = 0; i < bsNumRolls; ++i) due = std::min (due, bsRolls[i].beat);
    return due;
}

void MegaSynthProcessor::bsTick (double beat)
{
    auto& ch = engine.breaks();
    const auto st = engine.breakSettings (snap);
    // rolls (retriggers within a step)
    for (int i = 0; i < bsNumRolls;)
    {
        if (bsRolls[i].beat <= beat + 1.0e-9) { ch.trigger (bsRolls[i].t, st); bsRolls[i] = bsRolls[--bsNumRolls]; }
        else ++i;
    }
    const float swing = juce::jlimit (0.0f, 0.75f, snap.f (P_bsSwing));
    const auto& d = ch.data();
    const int steps = snap.i (P_bsSteps) == 1 ? 32 : 16;
    // the Fill parameter (automation, MIDI learn, the page's button)
    const bool fillParam = snap.v[P_bsFill] > 0.5f;
    if (fillParam != bsFillParamLast) { bsFillParamLast = fillParam; bsFillPress (fillParam); }
    int guard = 0;
    while (bsStepBeat (bsLastStep + 1, swing) <= beat + 1.0e-9 && ++guard < 64)
    {
        const int64_t k = ++bsLastStep;
        // a fill ends: the pattern comes back where it would have been by now (it kept running
        // underneath), or starts again from its first step
        if (bsFillState == 2 && k >= bsFillEnd)
        {
            bsFillState = 0;
            if (snap.i (P_bsFillReturn) == 1) { bsOffset = k; bsSkipChain = true; }
        }
        const int idx = (int) posMod (k - bsOffset, steps);
        // which pattern: the chain (moving on each time the pattern starts again), or the selected one,
        // switched on the next step, beat or bar
        int chainLen = 0; while (chainLen < 16 && d.chain[chainLen] != 0) ++chainLen;
        if (snap.v[P_bsChainOn] > 0.5f && chainLen > 0)
        {
            if ((idx == 0 && ! bsSkipChain) || bsChainIdx < 0)
            {
                bsChainIdx = (bsChainIdx + 1) % chainLen; bsPlaying = juce::jlimit (0, 7, d.chain[bsChainIdx] - 'A');
                bsApplyPatternBreak (d);
            }
        }
        else
        {
            const int want = juce::jlimit (0, 7, snap.i (P_bsPattern));
            const int sw = juce::jlimit (0, 2, snap.i (P_bsSwitch));
            if (want != bsPlaying && (sw == 0 || (sw == 1 && (k & 3) == 0) || (sw == 2 && (k & 15) == 0))) { bsPlaying = want; bsApplyPatternBreak (d); }
        }
        bsSkipChain = false;
        // Auto Fill: every 4, 8 or 16 bars (by chance), timed to end with the phrase
        static const int lens[] = { 4, 8, 16, 32 }, autoBars[] = { 0, 4, 8, 16 };
        const int afBars = autoBars[juce::jlimit (0, 3, snap.i (P_bsAutoFill))];
        if (bsFillState == 0 && afBars > 0)
        {
            const int L = lens[juce::jlimit (0, 3, snap.i (P_bsFillLen))];
            if (posMod (k + L, 16 * afBars) == 0)
            {
                bsFillRng ^= bsFillRng << 13; bsFillRng ^= bsFillRng >> 17; bsFillRng ^= bsFillRng << 5;
                if ((float) (bsFillRng & 0xffffff) / 16777216.0f < snap.f (P_bsAutoFillProb))
                { bsFillState = 1; bsFillStart = k; bsFillEnd = k + L; bsFillLen = L; bsFillHeld = false; }
            }
        }
        if (bsFillState == 1 && k >= bsFillStart) { bsFillState = 2; bsFillPat = bsPlaying; }
        const bool inFill = bsFillState == 2 && k >= bsFillStart && k < bsFillEnd;
        // the step: the pattern's, or the fill's (its source's last steps, as long as the fill)
        const tg::BreakStep* sp = &d.step[bsPlaying][idx];
        int fillBrk = -1;
        if (inFill)
        {
            const int fj = (int) posMod (k - bsFillStart, bsFillLen);
            const int srcIdx = (int) posMod (fj - bsFillLen, steps);
            const int src = juce::jlimit (0, 8, d.fillSrc[bsFillPat]);
            sp = src > 0 ? &d.step[src - 1][srcIdx] : &d.autoFill[bsFillPat][srcIdx];
            if (d.fillBrk[bsFillPat] > 0) fillBrk = juce::jlimit (0, 2, d.fillBrk[bsFillPat] - 1);
        }
        const tg::BreakStep& s = *sp;
        modInputs.bsFill = inFill ? 1.0f : 0.0f;
        bsShowFill (k, steps);
        bool fired = false; int firedBrk = -1;
        if (s.on && ! s.tie)
        {
            bsRng ^= bsRng << 13; bsRng ^= bsRng >> 17; bsRng ^= bsRng << 5;
            const float r = (float) (bsRng & 0xffffff) / 16777216.0f;
            if (r < s.prob)
            {
                tg::BreakChopper::Trigger t;
                t.slice = s.slice; t.vel = juce::jlimit (0.0f, 1.25f, s.vel * (s.accent ? 1.25f : 1.0f)); t.pitch = s.pitch; t.reverse = s.reverse;
                // which break: the step's own, or the Active Break (swapped every other bar or two with Alternate)
                int brk = s.brk > 0 ? s.brk - 1 : (fillBrk >= 0 ? fillBrk : juce::jlimit (0, 2, snap.i (P_bcActive) - 1));
                if (s.brk == 0 && ! inFill)
                {
                    static const int altBars[] = { 0, 1, 2, 4 };
                    const int ab = altBars[juce::jlimit (0, 3, snap.i (P_bsAlternate))];
                    if (ab > 0 && ((k / 16) / ab) % 2 == 1) brk = juce::jlimit (0, 2, snap.i (P_bsAltBreak));
                }
                t.brk = brk;
                t.stretch = s.stretch > 0 ? s.stretch - 1 : -1; t.cycleMs = s.cycleMs;   // per-hit stretch (none: the slice's, then the break's)
                // Humanise: the hit can be played a little late (a fraction of a step), from the rolls queue
                const double late = s.nudge > 0.0f ? 0.25 * juce::jlimit (0.0, 0.5, (double) s.nudge) : 0.0;
                const double sb = bsStepBeat (k, swing) + late;
                if (late > 0.0 && bsNumRolls < 15) bsRolls[bsNumRolls++] = { sb, t };
                else ch.trigger (t, st);
                fired = true; firedBrk = brk;
                for (int rr = 1; rr < s.roll && bsNumRolls < 15; ++rr) bsRolls[bsNumRolls++] = { sb + 0.25 * rr / s.roll, t };
                if (s.layer)
                {
                    // a second hit stacked on it, from another break (it doesn't choke the first)
                    tg::BreakChopper::Trigger l = t;
                    l.brk = juce::jlimit (0, 2, s.layerBrk - 1); l.slice = s.layerSlice; l.level = s.layerLevel; l.noChoke = true;
                    if (late > 0.0 && bsNumRolls < 15) bsRolls[bsNumRolls++] = { sb, l };
                    else ch.trigger (l, st);
                }
            }
        }
        if (bsLog != nullptr) bsLog->push_back ({ beat, k, bsPlaying, s.slice, fired, firedBrk, inFill });
        const int ns = ch.numSlices (ch.lastBreakPlayed());
        modInputs.bsStep = (float) idx / (float) (steps - 1);
        modInputs.bsPattern = (float) bsPlaying / 7.0f;
        modInputs.bsSlice = ns > 1 ? (float) ch.lastSlicePlayed() / (float) (ns - 1) : 0.0f;
        modInputs.bsBreak = (float) ch.lastBreakPlayed() * 0.5f;
        bsStepShown.store (idx, std::memory_order_relaxed);
        bsPatternShown.store (bsPlaying, std::memory_order_relaxed);
    }
}

// a new pattern can bring its own break: it becomes the Active Break
void MegaSynthProcessor::bsApplyPatternBreak (const tg::BreakData& d)
{
    const int pb = d.patBreak[juce::jlimit (0, 7, bsPlaying)];
    if (pb > 0 && snap.i (P_bcActive) != pb) { setParamFromAudio (P_bcActive, (float) pb); snap.v[P_bcActive] = (float) pb; }
}

void MegaSynthProcessor::refreshAutoFills (bool force)
{
    // each pattern's Auto fill: one of Snare Build, Stutter, Rollers, Pitch Dive or Frankenbreak (chosen
    // per pattern from the fill seed), made from that pattern. Remade whenever the patterns, the
    // seed or the analysis change; never saved (the same seed always makes the same fill)
    const int steps = raw[P_bsSteps]->load() > 0.5f ? 32 : 16;
    const float amount = std::max (0.5f, raw[P_bsAmount]->load());
    juce::String key;
    key << (int) breakStore.getVersion() << "|" << steps << "|" << amount << "|" << lastBreakKey[0] << "|" << lastBreakKey[1] << "|" << lastBreakKey[2];
    if (key == lastFillKey && ! force) return;
    lastFillKey = key;
    const auto d = breakStore.get();
    int loaded = 0; for (auto& b : d.brk) loaded += b.nSlices > 0 ? 1 : 0;
    static const int kAuto[] = { tg::breaktools::ToolSnareBuild, tg::breaktools::ToolStutter, tg::breaktools::ToolRollers, tg::breaktools::ToolPitchDive, -1 };
    std::vector<tg::BreakStep> out ((size_t) (tg::BreakData::kPatterns * tg::BreakData::kSteps));
    int tools[tg::BreakData::kPatterns] {};
    auto tmp = std::make_unique<tg::BreakData>();
    for (int p = 0; p < tg::BreakData::kPatterns; ++p)
    {
        const int gb = d.fillBrk[p] > 0 ? d.fillBrk[p] - 1 : (d.patBreak[p] > 0 ? d.patBreak[p] - 1 : 0);
        tg::breaktools::Rng r (d.fillSeed * 977 + p * 131);
        int pick = r.below (5);
        if (pick == 4 && loaded < 2) pick = r.below (4);   // (Frankenbreak needs two breaks)
        const int seed = d.fillSeed * 7919 + p;
        // (a tool that would leave this pattern as it is, e.g. Rollers on a pattern with every step on: the next one)
        int chosen = pick;
        for (int tries = 0; tries < 5; ++tries, pick = (pick + 1) % 5)
        {
            if (pick == 4 && loaded < 2) continue;
            chosen = pick;
            *tmp = d;
            if (kAuto[pick] >= 0) tg::breaktools::applyTool (kAuto[pick], *tmp, p, steps, seed, amount, gb, false);
            else tg::breaktools::frankenbreak (*tmp, p, steps, seed, amount, gb);
            bool differs = false;
            for (int i = 0; i < steps && ! differs; ++i)
            {
                const auto &x = tmp->step[p][i], &y = d.step[p][i];
                differs = x.on != y.on || x.slice != y.slice || x.roll != y.roll || x.pitch != y.pitch || x.reverse != y.reverse || x.brk != y.brk || x.vel != y.vel;
            }
            if (differs) break;
        }
        std::copy (tmp->step[p], tmp->step[p] + tg::BreakData::kSteps, out.begin() + p * tg::BreakData::kSteps);
        tools[p] = chosen;
    }
    breakStore.setAnalysis ([&] (tg::BreakData& x)
    {
        for (int p = 0; p < tg::BreakData::kPatterns; ++p)
        {
            std::copy (out.begin() + p * tg::BreakData::kSteps, out.begin() + (p + 1) * tg::BreakData::kSteps, x.autoFill[p]);
            x.autoFillTool[p] = tools[p];
        }
    });
}

void MegaSynthProcessor::refreshBreakAnalysis (bool force)
{
    const auto d = breakStore.get();
    for (int k = 0; k < tg::BreakData::kBreaks; ++k)
    {
        const auto* w = currentWave[breakSlot (k)].load();
        const auto bp = tg::breakParams (k);
        const int mode = juce::roundToInt (raw[bp.sliceMode]->load()), bars = juce::roundToInt (raw[bp.bars]->load());
        const float sens = raw[bp.sens]->load(), typed = raw[bp.tempo]->load();
        juce::String key;
        key << juce::String::toHexString ((juce::pointer_sized_int) w) << "|" << mode << "|" << (mode == 0 ? juce::String (sens, 3) : juce::String()) << "|" << bars << "|" << typed;
        const auto& b = d.brk[k];
        if (mode == 5) for (int i = 0; i < b.nMarkers; ++i) key << "," << juce::String (b.markers[i], 6);
        if (key == lastBreakKey[k] && ! force) continue;
        lastBreakKey[k] = key;
        tg::BreakSlot tmp = b;
        tg::breaks::analyse (w, mode, sens, bars, typed, tmp);
        breakStore.setAnalysis ([&] (tg::BreakData& x) { x.brk[k].copyAnalysisFrom (tmp); });
    }
}

//==============================================================================
// Chop mode and markers
double MegaSynthProcessor::snapChopFrame (int brk, double frame)
{
    return snapFrameTo (brk, frame, raw[P_bcChopSnapTr]->load() > 0.5f, raw[P_bcChopSnapZero]->load() > 0.5f);
}

double MegaSynthProcessor::snapFrameTo (int brk, double frame, bool toTransient, bool toZero)
{
    const auto* w = getSample (breakSlot (brk));
    if (w == nullptr) return frame;
    const double sr = w->sampleRate;
    if (toTransient)
    {
        if (onsetsFor[brk] != w) { onsets[brk] = tg::breaks::transients (*w, 1.0f); onsetsFor[brk] = w; }
        double best = frame, bestD = 0.02 * sr;   // within 20 ms
        for (int at : onsets[brk]) if (std::abs (at - frame) < bestD) { bestD = std::abs (at - frame); best = at; }
        frame = best;
    }
    if (toZero)
    {
        // the nearest zero crossing within 2 ms (where the left channel changes sign)
        const auto& a = w->ch[0];
        const int c = juce::jlimit (1, w->length - 2, (int) std::lround (frame)), r = (int) (0.002 * sr);
        for (int d = 0; d <= r; ++d)
        {
            for (int sgn : { -1, 1 })
            {
                const int i = c + sgn * d;
                if (i < 1 || i >= w->length) continue;
                if ((a[(size_t) i - 1] <= 0.0f) != (a[(size_t) i] <= 0.0f)) return (double) i;
            }
        }
    }
    return frame;
}

double MegaSynthProcessor::snapMarkerFrame (int brk, double frame)
{
    // dragging a marker: the Marker Snap setting (none, zero crossing, transient, or a grid at the break's tempo)
    brk = juce::jlimit (0, 2, brk);
    const auto* w = getSample (breakSlot (brk));
    if (w == nullptr) return frame;
    const int mode = juce::roundToInt (raw[P_bcMarkerSnap]->load());
    if (mode == 1 || mode == 2) return snapFrameTo (brk, frame, mode == 2, mode == 1);
    if (mode >= 3)
    {
        const double bpm = breakStore.get().brk[brk].origBpm;
        if (bpm <= 1.0) return frame;
        const double beats = mode == 3 ? 0.5 : (mode == 4 ? 0.25 : 0.125);
        const double gridF = beats * 60.0 / bpm * w->sampleRate;
        return std::round (frame / gridF) * gridF;
    }
    return frame;
}

bool MegaSynthProcessor::addChopMarker (int brk, double frame, bool snap)
{
    brk = juce::jlimit (0, 2, brk);
    const auto* w = getSample (breakSlot (brk));
    if (w == nullptr) return false;
    if (snap) frame = snapChopFrame (brk, frame);
    const float f = (float) (frame / (double) w->length);
    if (f <= 0.001f || f >= 0.999f) return false;
    bool added = false;
    breakStore.edit ([&] (tg::BreakData& d)
    {
        auto& b = d.brk[brk];
        for (int i = 0; i < b.nMarkers; ++i) if (std::abs (b.markers[i] - f) * w->length < 0.005 * w->sampleRate) return;   // (one there already)
        if (b.nMarkers >= tg::BreakSlot::kSlices - 1) return;
        b.markers[b.nMarkers++] = f; std::sort (b.markers, b.markers + b.nMarkers);
        added = true;
    });
    if (added)
    {
        chopHistory.push_back ({ brk, f });
        auto* prm = params[(size_t) tg::breakParams (brk).sliceMode];
        if (juce::roundToInt (prm->convertFrom0to1 (prm->getValue())) != 5) prm->setValueNotifyingHost (prm->convertTo0to1 (5.0f));
        refreshBreakAnalysis();
    }
    return added;
}

double MegaSynthProcessor::moveMarker (int brk, int idx, double frame, bool snap)
{
    // live while dragging: no undo step yet (commitMarkerEdit makes one at the end of the drag)
    brk = juce::jlimit (0, 2, brk);
    const auto* w = getSample (breakSlot (brk));
    if (w == nullptr) return -1.0;
    const auto d = breakStore.get();
    const auto& b = d.brk[brk];
    if (idx < 0 || idx >= b.nMarkers) return -1.0;
    if (snap) frame = snapMarkerFrame (brk, frame);
    // never past its neighbours (or the ends): slice 1 always starts at 0
    const double lo = (idx > 0 ? (double) b.markers[idx - 1] * w->length : 0.0) + kMarkerGap;
    const double hi = (idx + 1 < b.nMarkers ? (double) b.markers[idx + 1] * w->length : (double) w->length) - kMarkerGap;
    if (hi <= lo) return (double) b.markers[idx] * w->length;
    frame = juce::jlimit (lo, hi, frame);
    const float f = (float) (frame / (double) w->length);
    breakStore.setAnalysis ([brk, idx, f] (tg::BreakData& x) { if (idx < x.brk[brk].nMarkers) x.brk[brk].markers[idx] = f; });
    refreshBreakAnalysis();
    return frame;
}

void MegaSynthProcessor::setSliceTrim (int brk, int slice, bool end, float value)
{
    brk = juce::jlimit (0, 2, brk);
    slice = juce::jlimit (0, tg::BreakSlot::kSlices - 1, slice);
    breakStore.setAnalysis ([=] (tg::BreakData& x)
    {
        auto& s = x.brk[brk].slice[slice];
        if (end) s.trimEnd = juce::jlimit (s.trimStart + 0.05f, 1.0f, value);
        else s.trimStart = juce::jlimit (0.0f, std::min (0.95f, s.trimEnd - 0.05f), value);
    });
}

void MegaSynthProcessor::commitMarkerEdit()
{
    breakStore.edit ([] (tg::BreakData&) {});   // one undo step for the whole drag or nudge
}

bool MegaSynthProcessor::nudgeMarker (int brk, int idx, double ms)
{
    const auto* w = getSample (breakSlot (juce::jlimit (0, 2, brk)));
    if (w == nullptr) return false;
    const auto d = breakStore.get();
    if (idx < 0 || idx >= d.brk[brk].nMarkers) return false;
    const double from = (double) d.brk[brk].markers[idx] * w->length;
    const double to = moveMarker (brk, idx, from + ms * 0.001 * w->sampleRate, false);
    if (to < 0.0 || std::abs (to - from) < 0.5) return false;
    commitMarkerEdit();
    return true;
}

bool MegaSynthProcessor::undoLastChop()
{
    if (chopHistory.empty()) return false;
    const auto [brk, f] = chopHistory.back();
    chopHistory.pop_back();
    breakStore.edit ([brk = brk, f = f] (tg::BreakData& d)
    {
        auto& b = d.brk[brk];
        for (int i = 0; i < b.nMarkers; ++i)
            if (b.markers[i] == f) { for (int j = i; j + 1 < b.nMarkers; ++j) b.markers[j] = b.markers[j + 1]; --b.nMarkers; break; }
    });
    refreshBreakAnalysis();
    return true;
}

void MegaSynthProcessor::tapChop()
{
    const double fr = previewFrameShown.load();
    if (fr < 0.0) return;
    // the frame playing now, from the last block's frame and how long ago that was; less the latency
    const double ms = juce::Time::getMillisecondCounterHiRes() - previewStampMs.load();
    const double rate = previewFileRate.load() * getSampleRate();
    const double frame = fr + (ms - (double) raw[P_bcChopLatency]->load()) * 0.001 * rate;
    addChopMarker (previewBreakShown.load(), frame);
}

void MegaSynthProcessor::applyChopTaps()
{
    int s1, n1, s2, n2;
    chopFifo.prepareToRead (chopFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) addChopMarker (chopBrks[s1 + i], chopFrames[s1 + i]);
    for (int i = 0; i < n2; ++i) addChopMarker (chopBrks[s2 + i], chopFrames[s2 + i]);
    chopFifo.finishedRead (n1 + n2);
}

void MegaSynthProcessor::markersFromSlices (int brk)
{
    brk = juce::jlimit (0, 2, brk);
    const auto* w = getSample (breakSlot (brk));
    auto* prm = params[(size_t) tg::breakParams (brk).sliceMode];
    if (w == nullptr || juce::roundToInt (prm->convertFrom0to1 (prm->getValue())) == 5) return;
    breakStore.edit ([w, brk] (tg::BreakData& d)
    {
        auto& b = d.brk[brk];
        b.nMarkers = 0;
        for (int k = 1; k < b.nSlices && b.nMarkers < tg::BreakSlot::kSlices - 1; ++k) b.markers[b.nMarkers++] = (float) b.start[k] / (float) w->length;
    });
    prm->setValueNotifyingHost (prm->convertTo0to1 (5.0f));
    refreshBreakAnalysis();
}

void MegaSynthProcessor::clearMarkers (int brk)
{
    brk = juce::jlimit (0, 2, brk);
    breakStore.edit ([brk] (tg::BreakData& d) { d.brk[brk].nMarkers = 0; });
    auto* prm = params[(size_t) tg::breakParams (brk).sliceMode];
    prm->setValueNotifyingHost (prm->convertTo0to1 (5.0f));
    chopHistory.clear();
    refreshBreakAnalysis();
}

void MegaSynthProcessor::snapAllMarkers (int brk)
{
    brk = juce::jlimit (0, 2, brk);
    const auto* w = getSample (breakSlot (brk));
    if (w == nullptr) return;
    markersFromSlices (brk);
    const auto d = breakStore.get();
    std::vector<float> moved;
    for (int i = 0; i < d.brk[brk].nMarkers; ++i) moved.push_back ((float) (snapChopFrame (brk, d.brk[brk].markers[i] * (double) w->length) / w->length));
    std::sort (moved.begin(), moved.end());
    moved.erase (std::unique (moved.begin(), moved.end(), [w] (float a, float b) { return std::abs (a - b) * w->length < 64.0f; }), moved.end());
    breakStore.edit ([&] (tg::BreakData& x)
    {
        auto& b = x.brk[brk];
        b.nMarkers = 0;
        for (float f : moved) if (b.nMarkers < tg::BreakSlot::kSlices - 1) b.markers[b.nMarkers++] = f;
    });
    refreshBreakAnalysis();
}

bool MegaSynthProcessor::matchBreakLevels()
{
    const auto d = breakStore.get();
    double sum = 0.0; int n = 0;
    for (int k = 0; k < tg::BreakData::kBreaks; ++k) if (getSample (breakSlot (k)) != nullptr && d.brk[k].rms > 1.0e-5f) { sum += d.brk[k].rms; ++n; }
    if (n < 2) return false;
    const double target = sum / n;
    for (int k = 0; k < tg::BreakData::kBreaks; ++k)
    {
        if (getSample (breakSlot (k)) == nullptr || d.brk[k].rms <= 1.0e-5f) continue;
        auto* prm = params[(size_t) tg::breakParams (k).level];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jlimit (0.25, 2.0, target / d.brk[k].rms)));
        prm->endChangeGesture();
    }
    pushHistory ("Match break levels");
    return true;
}

void MegaSynthProcessor::setParamFromAudio (int index, float plain)
{
    auto* p = params[(size_t) index];
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void MegaSynthProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int ch = juce::jlimit (1, 16, m.getChannel());
    // DNA Sequencer pattern select: four notes choose A-D (and aren't played)
    if (m.isNoteOnOrOff())
    {
        static const int base[] = { -1, 0, 12, 24 };
        const int b = base[juce::jlimit (0, 3, snap.i (P_dsMidiSelect))];
        const int nn = m.getNoteNumber();
        if (b >= 0 && nn >= b && nn < b + 4)
        {
            if (m.isNoteOn()) setParamFromAudio (P_dsPattern, (float) (nn - b));
            return;
        }
    }
    // Beat Repeat's MIDI trigger: the note holds the repeat (and isn't played)
    if (m.isNoteOnOrOff())
    {
        static const int rptBase[] = { -1, 4, 16, 28 };
        const int rn = rptBase[juce::jlimit (0, 3, snap.i (P_rptMidi))];
        if (rn >= 0 && m.getNoteNumber() == rn) { engine.fx().setStutterHeld (m.isNoteOn()); return; }
    }
    // Performance FX: thirteen keys from the chosen note play the pads (and aren't played)
    if (m.isNoteOnOrOff())
    {
        static const int pfBase[] = { -1, 36, 48, 60, 72, 84, 96 };
        const int pb = pfBase[juce::jlimit (0, 6, snap.i (P_pfKeys))];
        const int nn = m.getNoteNumber();
        if (pb >= 0 && nn >= pb && nn < pb + kPfPads)
        {
            const int idx = pfPadParam (nn - pb);
            float v;
            if (snap.i (P_pfLatch) != 0) { if (! m.isNoteOn()) return; v = snap.v[idx] > 0.5f ? 0.0f : 1.0f; }   // latched: each press toggles
            else v = m.isNoteOn() ? 1.0f : 0.0f;
            setParamFromAudio (idx, v);
            snap.v[idx] = v;   // from this sample on, not from the next block
            return;
        }
    }
    // Break-Reactive mode's trigger note: a hit now (and it isn't played)
    if (m.isNoteOnOrOff() && snap.v[P_rxOn] > 0.5f && snap.v[P_rxFromNote] > 0.5f && m.getNoteNumber() == juce::roundToInt (snap.f (P_rxNote)))
    {
        if (m.isNoteOn()) engine.reactNoteHit (snap, m.getFloatVelocity());
        return;
    }
    // chop mode: while the break plays, every key or pad marks a slice there (and isn't played)
    if (m.isNoteOnOrOff() && snap.v[P_bcOn] > 0.5f && snap.v[P_bcChopMode] > 0.5f && engine.breaks().previewing())
    {
        if (m.isNoteOn())
        {
            auto& ch = engine.breaks();
            // what was heard when the key was hit: the frame playing now, less the output latency
            const double frame = ch.previewFrame() - juce::jlimit (0.0, 100.0, (double) snap.f (P_bcChopLatency)) * 0.001 * getSampleRate() * ch.previewFileRate();
            int s1, n1, s2, n2;
            chopFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) { chopFrames[s1] = frame; chopBrks[s1] = ch.previewBreak(); chopFifo.finishedWrite (1); }
        }
        return;
    }
    // the Break Chopper: pattern keys (8 from the chosen note) and the slices on keys (from First Key up)
    if (m.isNoteOnOrOff() && snap.v[P_bcOn] > 0.5f)
    {
        const int nn = m.getNoteNumber();
        static const int patBase[] = { -1, 24, 72, 84, 96 };
        const int pb = patBase[juce::jlimit (0, 4, snap.i (P_bsKeys))];
        // the Fill key (it isn't played as a note)
        const int fk = juce::roundToInt (snap.f (P_bsFillKey));
        if (fk >= 0 && nn == fk) { bsFillPress (m.isNoteOn()); return; }
        if (pb >= 0 && nn >= pb && nn < pb + 8)
        {
            if (m.isNoteOn()) { setParamFromAudio (P_bsPattern, (float) (nn - pb)); snap.v[P_bsPattern] = (float) (nn - pb); }
            return;
        }
        const int kb = juce::jlimit (0, 127, snap.i (P_bcKeyBase));
        // the three Break Select keys just below the zone choose the Active Break
        if (snap.v[P_bcSelKeys] > 0.5f && nn >= kb - 3 && nn < kb)
        {
            if (m.isNoteOn())
            {
                const int k = nn - (kb - 3);
                if (snap.v[P_bcSelQuant] > 0.5f && fxBeatInc > 0.0) { breakSelPending = k; breakSelBeat = std::floor (fxBeatStart + fxBeatInc * midiPos + 1.0e-6) + 1.0; }
                else { setParamFromAudio (P_bcActive, (float) (k + 1)); snap.v[P_bcActive] = (float) (k + 1); breakSelPending = -1; }
            }
            return;
        }
        const int active = juce::jlimit (0, 2, snap.i (P_bcActive) - 1);
        const int ns = engine.breaks().numSlices (active);
        if (m.isNoteOff() && nn >= kb && nn < kb + tg::BreakData::kSlices) engine.breaks().release (nn);   // (whichever break that key started)
        if (ns > 0 && nn >= kb && nn < kb + ns)
        {
            if (m.isNoteOn())
            {
                tg::BreakChopper::Trigger t; t.slice = nn - kb; t.vel = m.getFloatVelocity(); t.fromKey = true; t.brk = active; t.key = nn;
                engine.breaks().trigger (t, engine.breakSettings (snap));
                modInputs.bsSlice = ns > 1 ? (float) t.slice / (float) (ns - 1) : 0.0f;
                modInputs.bsBreak = (float) active * 0.5f;
            }
            return;
        }
    }
    if (m.isNoteOn())
    {
        monNoteCount.fetch_add (1, std::memory_order_relaxed); monCh.store (ch, std::memory_order_relaxed);
        const int nn = m.getNoteNumber() & 127;
        if (sustained[(size_t) nn]) { sustained[(size_t) nn] = false; routeNoteOff (nn); }   // re-striking a sustained key
        routeNoteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
    }
    else if (m.isNoteOff())
    {
        const int nn = m.getNoteNumber() & 127;
        if (sustainDown) sustained[(size_t) nn] = true;   // held by the pedal
        else routeNoteOff (nn);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const float v = m.getControllerValue() / 127.0f;
        monCC.store (cc, std::memory_order_relaxed); monCCVal.store (m.getControllerValue(), std::memory_order_relaxed);
        monCh.store (ch, std::memory_order_relaxed); monCCCount.fetch_add (1, std::memory_order_relaxed);
        if (cc == 1) monWheel.store (v, std::memory_order_relaxed);
        if (cc == 64)
        {
            const bool down = m.getControllerValue() >= 64 && snap.i (P_sustainPedal) != 0;
            if (sustainDown && ! down) sustainRelease();
            sustainDown = down; monSustain.store (down, std::memory_order_relaxed);
        }
        // MIDI learn: the first controller moved after "MIDI Learn" is mapped to that parameter
        if (cc < 120)
        {
            const int learn = learnParam.load();
            if (learn >= 0)
            {
                for (auto& c : ccMap) { int expect = learn; c.compare_exchange_strong (expect, -1); }
                ccMap[(size_t) cc].store (learn);
                learnParam.store (-1);
                midiMapVersion.fetch_add (1);
            }
            const int mapped = ccMap[(size_t) cc].load();
            if (mapped >= 0 && mapped < P_COUNT) params[(size_t) mapped]->setValueNotifyingHost (v);
        }
        if (cc == juce::roundToInt (snap.f (P_ccANum))) modInputs.ccA = v;
        if (cc == juce::roundToInt (snap.f (P_ccBNum))) modInputs.ccB = v;
        if (cc == 74) modInputs.mpeSlide[ch - 1] = v;              // MPE slide (Y)
        if (cc == 1) modInputs.wheel = v;
        if (cc == 1) { if (snap.i (P_wheelLfo1) != 0) setParamFromAudio (P_lfo1Depth, v); }   // mod wheel -> LFO1 depth (as in the browser; can be switched off)
        else if (cc == 7) setParamFromAudio (P_masterVolume, v);   // volume -> master volume
        else if (cc == 120 || cc == 123) { sustained.fill (false); sustainDown = false; monSustain.store (false); engine.allNotesOff(); }
    }
    else if (m.isProgramChange())
    {
        const int prog = m.getProgramChangeNumber();
        if (prog < (int) tg::factoryPresets().size()) pendingProgram.store (prog);   // loads on the message thread
    }
    else if (m.isChannelPressure())
    {
        const float v = m.getChannelPressureValue() / 127.0f;
        modInputs.aftertouch = v;
        monPressure.store (v, std::memory_order_relaxed);
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
        bendNorm = b;
        bendSemis = bendNorm * snap.f (P_bendRange);
        snap.bendSemis = bendSemis;
        monBend.store (b, std::memory_order_relaxed); monCh.store (ch, std::memory_order_relaxed); monBendCount.fetch_add (1, std::memory_order_relaxed);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        sustained.fill (false); sustainDown = false; monSustain.store (false);
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
        dnaClock.noteOn (midiPos);
        engine.fx().shaperNoteOn (fxBeatStart + fxBeatInc * midiPos);
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
    // the sidechain arrives in the same channels the synth writes to: take a copy first
    bool scActive = false;
    if (auto* inBus = getBus (true, 0); inBus != nullptr && inBus->isEnabled() && getTotalNumInputChannels() > 0 && n <= scBuf.getNumSamples())
    {
        auto in = getBusBuffer (buffer, true, 0);
        if (in.getNumChannels() > 0)
        {
            scBuf.copyFrom (0, 0, in, 0, 0, n);
            scBuf.copyFrom (1, 0, in, in.getNumChannels() > 1 ? 1 : 0, 0, n);
            scActive = true;
            const float rel = (float) std::exp (-1.0 / (0.08 * sampleRate));
            float e = scEnvelope, pk = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float a = std::max (std::abs (scBuf.getSample (0, i)), std::abs (scBuf.getSample (1, i)));
                e = a > e ? a : e * rel;
                pk = std::max (pk, a);
            }
            scEnvelope = e;
            scMeter.store (std::max (pk, scMeter.load() * 0.8f));
        }
    }
    if (! scActive) { scEnvelope = 0.0f; scMeter.store (scMeter.load() * 0.8f); }
    modInputs.sidechain = juce::jlimit (0.0f, 1.0f, scEnvelope);
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    bool hostPlaying = false, hasBar = false, hostTransport = false;
    double ppq = 0.0, bpm = 0.0, barStart = 0.0, beatsPerBar = 4.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
            if (auto bs = pos->getPpqPositionOfLastBarStart()) { barStart = *bs; hasBar = true; }
            if (auto ts = pos->getTimeSignature()) if (ts->numerator > 0 && ts->denominator > 0) beatsPerBar = ts->numerator * 4.0 / ts->denominator;
            hostPlaying = pos->getIsPlaying();
            hostTransport = true;
        }
    }
    hostTempo = bpm;
    fillSnapshot (n);

    const WaveSample* wavs[6];
    for (int k = 0; k < 6; ++k) wavs[k] = currentWave[k].load();
    if (capStopReq.load()) { capRecording = false; capStopReq = false; capReady = true; }
    // ---- the Break Chopper: the newest slices and settings; a slice the editor wants auditioned
    const bool bcOn = snap.v[P_bcOn] > 0.5f;
    if (bcOn)
    {
        engine.pullBreak (wavs + kSlotBreak);
        const double pr = breakPreviewReq.exchange (-1.0);
        if (pr >= 0.0)
        {
            const int a = juce::jlimit (0, 2, snap.i (P_bcActive) - 1);
            if (wavs[kSlotBreak + a] != nullptr) engine.breaks().startPreview (a, pr * wavs[kSlotBreak + a]->length);
        }
        else if (pr < -1.5) engine.breaks().stopPreview();
        const int au = breakAudition.exchange (-1);
        if (au >= 0)
        {
            tg::BreakChopper::Trigger t; t.slice = au % 100; t.brk = au / 100; t.vel = 0.9f;   // (break * 100 + slice)
            engine.breaks().trigger (t, engine.breakSettings (snap));
        }
    }
    else { breakAudition.store (-1); breakPreviewReq.store (-1.0); }
    if (grainLoadReady.load())
    {
        engine.loadGranular (grainLoad.getReadPointer (0), grainLoad.getReadPointer (1), grainLoadLen);
        grainLoadReady = false;
    }

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

    // ---- DNA Sequencer clock: Host (the DAW's beat position while it plays, else the tempo),
    // Internal (the synth's tempo) or Free (Hz); restart, start offset and swing on top
    const bool dsOn = snap.i (P_dsOn) != 0;
    int dsChunk = maxBlock;
    {
        tg::DnaClock::Block b;
        b.on = dsOn; b.sync = juce::jlimit (0, 2, snap.i (P_dsSync)); b.rate = juce::jlimit (0, 8, snap.i (P_dsRate));
        // Swing, Start Offset and Free Rate can be Mod Matrix destinations (previous block's modulation)
        b.restart = juce::jlimit (0, 3, snap.i (P_dsRestart));
        b.freeHz = juce::jlimit (0.1, 20.0, (double) (snap.f (P_dsFreeHz) + engine.dnaClockModOffset (tg::Engine::DCM_FreeHz)));
        b.offset = juce::jlimit (0.0, 31.0, (double) std::round (snap.f (P_dsOffset) + engine.dnaClockModOffset (tg::Engine::DCM_Offset)));
        b.swing = juce::jlimit (0.0, 0.75, (double) (snap.f (P_dsSwing) + engine.dnaClockModOffset (tg::Engine::DCM_Swing)));
        b.swingGrid = snap.i (P_dsSwingGrid);
        b.barSteps = juce::jlimit (1, 32, snap.i (P_dsSteps));
        b.hostPlaying = hostPlaying; b.ppq = ppq; b.bpm = bpm; b.barStartPpq = barStart; b.hasBar = hasBar; b.beatsPerBar = beatsPerBar;
        b.fallbackTempo = b.sync == tg::DS_Internal ? (double) snap.f (P_seqTempo) : (bpm > 0.0 ? bpm : (double) snap.f (P_seqTempo));
        if (b.sync == tg::DS_Internal) b.bpm = 0.0, b.hostPlaying = false;
        b.sampleRate = sampleRate;
        dnaClock.beginBlock (b);
        // The original sequencer worked block by block; the new features (ratchets, swing, the gate
        // source, restarts...) need finer timing, so then the block is evaluated every 64 samples.
        const bool legacy = b.sync == tg::DS_Host && b.restart == tg::DR_Never && b.offset == 0.0 && b.swing <= 0.0
                            && snap.i (P_dsDirection) == 0 && snap.i (P_dsLane2On) == 0 && snap.i (P_dsPattern) == 0 && snap.i (P_dsChainOn) == 0
                            && snap.f (P_dsDensity) >= 1.0f && std::abs (snap.f (P_dsProbScale) - 1.0f) < 1.0e-4f && dnaSteps.legacyOnly() && ! dnaGateRouted();
        if (dsOn && ! legacy) dsChunk = std::min (maxBlock, 64);
    }

    // ---- bar and host-start positions for the LFOs' "Each bar" / "Host start" retrigger
    {
        const bool playing = hostPlaying && bpm > 0.0;
        if (playing && ! hostWasPlayingLfo) hostStartBeat = ppq;
        if (! playing) hostStartBeat = 0.0;
        hostWasPlayingLfo = playing;
        snap.fxHostStart = hostStartBeat;
        snap.fxBarOrigin = playing && hasBar ? barStart : 0.0;
        snap.fxBeatsPerBar = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    }

    // ---- the effects' beat clock: the DAW's position while it plays, otherwise carrying on at the tempo
    {
        const double tempo = snap.fxTempo;
        if (hostPlaying && bpm > 0.0) { fxBeatStart = ppq; fxBeatInc = bpm / 60.0 / sampleRate; }
        else { fxBeatStart = fxBeatsInternal; fxBeatInc = tempo / 60.0 / sampleRate; }
        fxBeatsInternal = fxBeatStart + fxBeatInc * n;
    }

    // ---- arpeggiator settings for this block (Swing and Gate can be modulated: last block's offsets)
    const bool arpOn = snap.i (P_arpOn) != 0;
    {
        tg::Arpeggiator::Settings a;
        a.mode = juce::jlimit (0, 6, snap.i (P_arpMode)); a.octaves = juce::jlimit (1, 4, snap.i (P_arpOctaves));
        a.rate = juce::jlimit (0, 8, snap.i (P_arpRate)); a.velMode = snap.i (P_arpVelMode); a.velFixed = snap.f (P_arpVelFixed);
        a.repeats = juce::jlimit (1, 4, snap.i (P_arpRepeats)); a.restart = snap.i (P_arpRestart); a.steps = juce::jlimit (1, 16, snap.i (P_arpSteps));
        a.swing = juce::jlimit (0.0, 0.75, (double) (snap.f (P_arpSwing) + engine.arpModOffset (0)));
        a.gate = juce::jlimit (0.05, 1.0, (double) (snap.f (P_arpGate) + engine.arpModOffset (1)));
        a.latch = snap.i (P_arpLatch) != 0;
        a.beatsPerBar = snap.fxBeatsPerBar; a.barOrigin = snap.fxBarOrigin;
        arp.configure (a);
        if (arpWasOn && ! arpOn)
        {
            // switched off: end its notes; keys held now play normally from their next press
            arpEvents.clear(); arp.allOff (arpEvents);
            for (auto& e : arpEvents) engine.noteOff (3000 + e.note);
            arp.reset();
        }
        arpWasOn = arpOn;
        modInputs.arpGate *= (float) std::exp (-(double) n / (0.03 * sampleRate));
    }
    auto beatAt = [this] (int p) { return fxBeatStart + fxBeatInc * p; };
    // ---- the Break Sequencer: runs with the Break Chopper on and a break loaded, on the beat clock
    {
        // (in a DAW it plays while the song plays; with no transport at all, e.g. standalone, on the synth's own tempo)
        const int anySlices = engine.breaks().numSlices (0) + engine.breaks().numSlices (1) + engine.breaks().numSlices (2);
        const bool run = bcOn && snap.v[P_bsOn] > 0.5f && anySlices > 0 && fxBeatInc > 0.0 && (hostPlaying || ! hostTransport);
        if (run && ! bsRunning) bsStart (beatAt (0));
        if (! run && bsRunning) { bsRunning = false; bsNumRolls = 0; bsStepShown.store (-1); bsFillState = 0; bsFillCountdown.store (-1); bsFillPosShown.store (-1); bsFillFirstIdx.store (-1); modInputs.bsFill = 0.0f; }
        if (run)
        {
            // a jump in the song position (or the DAW starting): pick up from where it is now
            const double b0 = beatAt (0), due = bsDue();
            if (b0 > due + 0.25 || b0 < due - 0.5) bsStart (b0);
            bsTick (b0);
        }
    }

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
        if ((arpOn && arp.active()) || arp.sounding())
        {
            const double due = arp.nextDue (beatAt (pos));
            if (due < 1.0e299 && fxBeatInc > 0.0)
                next = std::min (next, pos + std::max (1, (int) std::ceil ((due - beatAt (pos)) / fxBeatInc - 1.0e-6)));
        }
        if (bsRunning)
            next = std::min (next, pos + std::max (1, (int) std::ceil ((bsDue() - beatAt (pos)) / fxBeatInc - 1.0e-6)));
        if (breakSelPending >= 0 && fxBeatInc > 0.0)
            next = std::min (next, pos + std::max (1, (int) std::ceil ((breakSelBeat - beatAt (pos)) / fxBeatInc - 1.0e-6)));

        // render up to the next event, in chunks no larger than the prepared block size
        while (pos < next)
        {
            const int len = std::min (next - pos, dsChunk);
            engine.keepPreFx = capRecording.load (std::memory_order_relaxed) && snap.i (P_capPoint) == 0;
            engine.setDnaPosition (dsOn ? dnaClock.stepsAt (pos + 0.5 * len) : 0.0);
            snap.fxBeat = fxBeatStart + fxBeatInc * pos; snap.fxBeatInc = fxBeatInc;
            engine.setSidechain (scActive ? scBuf.getReadPointer (0) + pos : nullptr, scActive ? scBuf.getReadPointer (1) + pos : nullptr);
            engine.render (L + pos, R + pos, len, snap, wavs);
            if (capRecording.load (std::memory_order_relaxed))
            {
                // Capture: what goes to the effects, or what you hear
                const bool pre = snap.i (P_capPoint) == 0;
                const float* sl = pre ? engine.preFx (0) : L + pos;
                const float* sr2 = pre ? engine.preFx (1) : R + pos;
                int cp = capPos.load (std::memory_order_relaxed);
                const int m = std::min (len, capBuf.getNumSamples() - cp);
                if (m > 0) { capBuf.copyFrom (0, cp, sl, m); capBuf.copyFrom (1, cp, sr2, m); cp += m; capPos.store (cp); }
                if (cp >= capBuf.getNumSamples()) { capRecording = false; capReady = true; }
            }
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
        midiPos = pos;
        while (it != end && (*it).samplePosition <= pos)
        {
            handleMidi ((*it).getMessage());
            ++it;
        }
        if ((arpOn && arp.active()) || arp.sounding()) arpTick (beatAt (pos));
        if (breakSelPending >= 0 && beatAt (pos) >= breakSelBeat - 1.0e-9)
        {
            // a Break Select key on the beat
            setParamFromAudio (P_bcActive, (float) (breakSelPending + 1)); snap.v[P_bcActive] = (float) (breakSelPending + 1);
            breakSelPending = -1;
        }
        if (bsRunning) bsTick (beatAt (pos));
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

    dnaClock.endBlock (n);
    {
        auto& ch = engine.breaks();
        previewFrameShown.store (ch.previewing() ? ch.previewFrame() : -1.0, std::memory_order_relaxed);
        previewFileRate.store (ch.previewFileRate(), std::memory_order_relaxed);
        previewBreakShown.store (ch.previewBreak(), std::memory_order_relaxed);
        previewStampMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    }
    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);

    // safety: never pass NaN/inf to the host
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) { engine.reset(); buffer.clear(); return; }
    }
    // the output meter (the editor reads and resets it)
    for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
    {
        const float pk = buffer.getMagnitude (c, 0, n);
        auto& m = engine.outputPeak[(size_t) c];
        if (pk > m.load (std::memory_order_relaxed)) m.store (pk, std::memory_order_relaxed);
    }

    // spectrum display tap (a copy only; the analysis happens in the editor)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
        int w = scopeWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { scope[w] = 0.5f * (l[i] + r[i]); w = (w + 1) & (kScopeLen - 1); }
        scopeWrite.store (w, std::memory_order_release);
    }
}

void MegaSynthProcessor::readScope (float* dest, int n) const
{
    n = std::min (n, kScopeLen);
    const int w = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i) dest[i] = scope[(w - n + i + kScopeLen) & (kScopeLen - 1)];
}

//==============================================================================
void MegaSynthProcessor::updateLatency()
{
    // the spectral stage delays the output by one FFT frame while it's on; tell the host
    const int want = raw[P_spOn]->load() > 0.5f ? spectralSize (effectiveSpectralSize ((int) raw[P_spSize]->load(), (int) raw[P_quality]->load())) : 0;
    if (want != getLatencySamples()) setLatencySamples (want);
}

void MegaSynthProcessor::timerCallback()
{
    {
        const int prog = pendingProgram.exchange (-1);
        if (prog >= 0) { stateRestored = true; loadFactoryPreset (prog); }
    }
    initOnFirstUse();   // a new instance: Init, on the message thread, once (never over a restored project)
    refreshBreakAnalysis();
    refreshAutoFills();
    {
        // chop mode switched on: Manual markers from the slices there are now, and the break starts playing
        const bool chop = raw[P_bcChopMode]->load() > 0.5f;
        if (chop && ! lastChopMode)
        {
            const int a = juce::jlimit (0, 2, juce::roundToInt (raw[P_bcActive]->load()) - 1);
            markersFromSlices (a);
            breakPreviewReq.store (0.0);
        }
        if (! chop && lastChopMode) breakPreviewReq.store (-2.0);
        lastChopMode = chop;
        applyChopTaps();
    }
    updateLatency();
    pollCapture();
    // history: record a snapshot shortly after an edit finishes (knob released, step changed)
    syncSceneEdits();
    if (! restoring)
    {
        const auto sv = scenes.version.load();
        if (sv != lastScenesVersion) { lastScenesVersion = sv; snapshotPending = true; snapshotDelay = 0; }
        const auto v = steps.getVersion();
        if (v != lastStepsVersion) { lastStepsVersion = v; snapshotPending = true; snapshotDelay = 0; }
        const auto dv = dnaSteps.getVersion();
        if (dv != lastDnaVersion) { lastDnaVersion = dv; snapshotPending = true; snapshotDelay = 0; }
        const auto fv = fxRack.getVersion();
        if (fv != lastRackVersion) { lastRackVersion = fv; snapshotPending = true; snapshotDelay = 0; }
        const auto lv = lfoShapes.getVersion();
        if (lv != lastLfoShapeVersion) { lastLfoShapeVersion = lv; snapshotPending = true; snapshotDelay = 0; }
        const auto mv = msegs.getVersion();
        if (mv != lastMsegVersion) { lastMsegVersion = mv; snapshotPending = true; snapshotDelay = 0; }
        const auto av = arpStore.getVersion();
        if (av != lastArpVersion) { lastArpVersion = av; snapshotPending = true; snapshotDelay = 0; }
        const auto bv = breakStore.getVersion();
        if (bv != lastBreakVersion) { lastBreakVersion = bv; snapshotPending = true; snapshotDelay = 0; }
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

    slot = juce::jlimit (0, kSampleSlots - 1, slot);
    auto ws = std::make_unique<WaveSample>();
    const double capSecs = sampleCapSeconds (slot);
    const bool trimmed = (double) reader->lengthInSamples > reader->sampleRate * capSecs;
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * capSecs));
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

    // a file longer than the cap is kept (and saved) as just the part that's used, as a 24-bit WAV
    juce::MemoryBlock kept;
    if (trimmed && slot >= 2)
    {
        juce::MemoryOutputStream* mos = new juce::MemoryOutputStream (kept, false);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (mos, ws->sampleRate, (unsigned int) tmp.getNumChannels(), 24, {}, 0));
        if (w != nullptr) { w->writeFromAudioSampleBuffer (tmp, 0, len); w.reset(); }
        else { delete mos; kept = data; }
    }
    const juce::MemoryBlock& store = (trimmed && slot >= 2 && kept.getSize() > 0) ? kept : data;
    const juce::ScopedLock sl (waveLock);
    waveData[slot] = store;
    waveShared[slot] = std::make_shared<const juce::MemoryBlock> (store);
    waveName[slot] = name;
    currentWave[slot].store (ws.get());
    waveKeep.push_back (std::move (ws));
    // free old samples once they're well out of use (never the two current ones)
    while (waveKeep.size() > 10)
    {
        auto it = std::find_if (waveKeep.begin(), waveKeep.end(), [this] (const auto& w)
                                { for (auto& c : currentWave) if (w.get() == c.load()) return false; return true; });
        if (it == waveKeep.end()) break;
        waveKeep.erase (it);
    }
    if (slot >= kSlotBreak) refreshBreakAnalysis (true);
    return true;
}

void MegaSynthProcessor::clearSample (int slot)
{
    const juce::ScopedLock sl (waveLock);
    slot = juce::jlimit (0, kSampleSlots - 1, slot);
    currentWave[slot].store (nullptr);
    waveData[slot].reset();
    waveShared[slot].reset();
    waveName[slot].clear();
    if (slot >= kSlotBreak) refreshBreakAnalysis (true);
}

juce::String MegaSynthProcessor::getSampleStatus (int slot) const
{
    const juce::ScopedLock sl (waveLock);
    slot = juce::jlimit (0, kSampleSlots - 1, slot);
    const auto* w = currentWave[slot].load();
    if (w == nullptr) return slot >= 2 ? "No sample loaded" : "No wavetable loaded";
    return "Loaded: " + waveName[slot] + "  " + juce::String (w->duration(), 2) + "s  peak " + juce::String (w->peak, 2);
}

//==============================================================================
void MegaSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    state.setProperty ("seqSteps", steps.toString(), nullptr);
    state.setProperty ("modRoutes", juce::JSON::toString (routes.toVar(), true), nullptr);
    state.setProperty ("dnaSeqSteps", dnaSteps.toString(), nullptr);   // pattern A lane 1, as older versions read it
    state.setProperty ("dnaSeq2", dnaSteps.toJson(), nullptr);         // everything
    state.setProperty ("fxRack", juce::JSON::toString (fxRack.toVar(), true), nullptr);
    state.setProperty ("lfoShapes", lfoShapes.toJson(), nullptr);
    state.setProperty ("arp", arpStore.toJson(), nullptr);
    state.setProperty ("breakStore", breakStore.toJson(), nullptr);
    state.setProperty ("msegs", msegs.toJson(), nullptr);
    state.setProperty ("midiMap", midiMapToString(), nullptr);        // MIDI learn belongs to the project, not the patch
    state.setProperty ("uiWidth", uiWidth.load(), nullptr);
    if (getUiTabs().isNotEmpty()) state.setProperty ("uiTabs", getUiTabs(), nullptr);
    state.setProperty ("program", curProgram.load(), nullptr);
    state.setProperty ("scenes", juce::JSON::toString (scenes.toVar(), true), nullptr);
    state.setProperty ("sceneEdit", getEditScene(), nullptr);
    state.setProperty ("macroNames", macroNamesJoined(), nullptr);
    state.setProperty ("capture", juce::JSON::toString (captureToVar(), true), nullptr);
    state.setProperty ("mutHistory", getMutationHistory().joinIntoString ("|"), nullptr);
    state.setProperty ("lab", juce::JSON::toString (lab.toVar(), true), nullptr);
    state.setProperty ("labVariation", labVariation, nullptr);
    state.setProperty ("patchName", getPatchName(), nullptr);
    state.setProperty ("patchMeta", juce::JSON::toString (getPatchMeta().toVar(), true), nullptr);
    {
        const juce::ScopedLock sl (waveLock);
        for (int k = 0; k < kSampleSlots; ++k)
        {
            const juce::String suffix = sampleSuffix (k);
            if (k >= 2 && waveData[k].getSize() == 0) continue;   // (no empty properties for the newer slots)
            state.setProperty ("waveName" + suffix, waveName[k], nullptr);
            state.setProperty ("waveData" + suffix, waveData[k].getSize() > 0 ? juce::Base64::toBase64 (waveData[k].getData(), waveData[k].getSize()) : juce::String(), nullptr);
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void MegaSynthProcessor::setStateInformation (const void* data, int size)
{
    stateRestored = true;   // a project: never put Init over it
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid()) return;
    // Version 1 (v0.1.x) state has no version number; its parameters, steps and samples
    // map 1:1 onto version 2, so no conversion is needed yet. Later versions migrate here.
    const int version = (int) tree.getProperty ("stateVersion", 1);
    apvts.replaceState (tree);
    if (version < 3) setFilterDefaultsForOldPatch();
    if (version < 4) setFilter2DefaultsForOldPatch();
    if (version < 5) setDnaDefaultsForOldPatch();
    if (version < 6) setFxDefaultsForOldPatch();
    if (version < 7) setLfoDefaultsForOldPatch();
    if (version < 8) setArpDefaultsForOldPatch();
    if (version < 9) setSidechainDefaultsForOldPatch();
    if (version < 10) setUnisonDefaultsForOldPatch();
    if (version < 11) setEnvDefaultsForOldPatch();
    if (version < 12) { setMasterDefaultsForOldPatch(); setMixerDefaultsForOldPatch(); }
    if (version < 13) setCharDefaultsForOldPatch();
    if (version < 14) setPerfDefaultsForOldPatch();
    if (version < 15) setGranDefaultsForOldPatch();
    if (version < 16) setBreakDefaultsForOldPatch();
    if (version < 17) setBreak3DefaultsForOldPatch();
    if (version < 18) setChopDefaultsForOldPatch();
    if (version < 19) setToolDefaultsForOldPatch();
    if (version < 20) setFillDefaultsForOldPatch();
    if (version < 21) setMfModelDefaultsForOldPatch();
    if (version < 22) setRouteDefaultsForOldPatch();
    if (version < 23) setAkaiDefaultsForOldPatch();
    if (version < 24) setReeseDefaultsForOldPatch();
    if (version < 25) setReactDefaultsForOldPatch();
    fxRack.fromVar (juce::JSON::parse (tree.getProperty ("fxRack").toString()));   // older projects: default order
    lfoShapes.fromVar (juce::JSON::parse (tree.getProperty ("lfoShapes").toString()));
    arpStore.fromVar (juce::JSON::parse (tree.getProperty ("arp").toString()));
    breakStore.fromVar (juce::JSON::parse (tree.getProperty ("breakStore").toString()));
    msegs.fromVar (juce::JSON::parse (tree.getProperty ("msegs").toString()));
    midiMapFromString (tree.getProperty ("midiMap").toString());
    if (tree.hasProperty ("uiWidth")) uiWidth.store ((int) tree.getProperty ("uiWidth"));
    setUiTabs (tree.getProperty ("uiTabs").toString());
    pendingProgram.store (-1);   // a program change still waiting must not replace the restored state
    curProgram.store (juce::jlimit (0, (int) tg::factoryPresets().size() - 1, (int) tree.getProperty ("program", 0)));

    if (tree.hasProperty ("patchName")) setPatchName (tree.getProperty ("patchName").toString());
    setPatchMeta (tg::PatchMeta::fromVar (juce::JSON::parse (tree.getProperty ("patchMeta", {}).toString())));
    const juce::String stepStr = tree.getProperty ("seqSteps").toString();
    if (stepStr.isNotEmpty()) steps.fromString (stepStr);
    routes.fromVar (juce::JSON::parse (tree.getProperty ("modRoutes").toString()));   // none in v1 state
    if (! (tree.hasProperty ("dnaSeq2") && dnaSteps.fromVar (juce::JSON::parse (tree.getProperty ("dnaSeq2").toString()))))
        dnaSteps.fromString (tree.getProperty ("dnaSeqSteps").toString());
    scenes.fromVar (juce::JSON::parse (tree.getProperty ("scenes").toString()));
    sceneEditIndex = juce::jlimit (0, 3, (int) tree.getProperty ("sceneEdit", 0));
    setMacroNamesJoined (tree.getProperty ("macroNames").toString());
    captureFromVar (juce::JSON::parse (tree.getProperty ("capture").toString()));
    {
        std::vector<float> defs ((size_t) P_COUNT);
        for (int i = 0; i < P_COUNT; ++i) defs[(size_t) i] = tg::meta (i).def;
        lab.fromVar (juce::JSON::parse (tree.getProperty ("lab").toString()), defs);
        labVariation = (float) (double) tree.getProperty ("labVariation", 0.1);
    }
    { const juce::ScopedLock sl (nameLock); mutHistory.clear(); mutHistory.addTokens (tree.getProperty ("mutHistory").toString(), "|", {}); mutHistory.removeEmptyStrings(); }
    lastMorph = raw[P_sceneMorph]->load() > 0.5f;

    for (int k = 0; k < kSampleSlots; ++k)
    {
        const juce::String suffix = sampleSuffix (k);
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
    root->setProperty ("meta", getPatchMeta().toVar());   // category, tags, author, note
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
    root->setProperty ("dnaSequence", dnaSteps.toString());   // pattern A lane 1 (older versions)
    root->setProperty ("dnaSequencer", dnaSteps.toVar());     // everything
    root->setProperty ("fxRack", fxRack.toVar());
    root->setProperty ("lfoShapes", lfoShapes.toVar());
    root->setProperty ("arp", arpStore.toVar());
    root->setProperty ("break", breakStore.toVar());
    root->setProperty ("msegs", msegs.toVar());
    root->setProperty ("scenes", scenes.toVar());
    root->setProperty ("sceneEdit", getEditScene());
    root->setProperty ("capture", captureToVar());
    if (lab.find (lab.current) != nullptr) root->setProperty ("lineage", lab.lineage (lab.current, 6));   // the patch's family tree, for reference
    {
        juce::Array<juce::var> mh;
        for (auto& h : getMutationHistory()) mh.add (h);
        root->setProperty ("mutationHistory", mh);
    }
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
        for (int k = 0; k < kSampleSlots; ++k)
        {
            const juce::Identifier key (samplePatchKey (k));   // the browser only knows "wavetable"
            if (k >= 2 && waveData[k].getSize() == 0) continue;
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
    setPatchMeta (tg::PatchMeta::fromVar (patch["meta"]));   // (none, from before categories: Uncategorised)

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
        if (extra == nullptr || ! extra->hasProperty ("filterSlope")) setFilterDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("filter2On")) setFilter2DefaultsForOldPatch();
        routes.fromVar (patch["modMatrix"]);
        if (! dnaSteps.fromVar (patch["dnaSequencer"])) dnaSteps.fromString (patch["dnaSequence"].toString());
        if (extra == nullptr || ! extra->hasProperty ("dsSync")) setDnaDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("fxOnDelay")) setFxDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("lfo1Sync")) setLfoDefaultsForOldPatch();
        fxRack.fromVar (patch["fxRack"]);
        lfoShapes.fromVar (patch["lfoShapes"]);
        if (extra == nullptr || ! extra->hasProperty ("arpOn")) setArpDefaultsForOldPatch();
        arpStore.fromVar (patch["arp"]);
        breakStore.fromVar (patch["break"]);
        if (extra == nullptr || ! extra->hasProperty ("pEnvAmt")) setEnvDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("wheelLfo1")) setMidiDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("mfOn")) setMasterDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("mxOsc1Pan")) setMixerDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("flt1Char")) setCharDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("pfRoll")) setPerfDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("gsOn")) setGranDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("bcOn")) setBreakDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("bcActive")) setBreak3DefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("bcChopMode")) setChopDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("bsToolSource")) setToolDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("bsFillMode")) setFillDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("mfModel")) setMfModelDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("subRoute")) setRouteDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("pfAkai")) setAkaiDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("rsLevel")) setReeseDefaultsForOldPatch();
        if (extra == nullptr || ! extra->hasProperty ("rxOn")) setReactDefaultsForOldPatch();
        msegs.fromVar (patch["msegs"]);
        scenes.fromVar (patch["scenes"]);
        sceneEditIndex = juce::jlimit (0, 3, (int) patch.getProperty ("sceneEdit", 0));
        const juce::var names = patch["macroNames"];
        for (int k = 0; k < 8; ++k) setMacroName (k, names.isArray() && k < names.size() ? names[k].toString() : juce::String());
        lastMorph = raw[P_sceneMorph]->load() > 0.5f;
        captureFromVar (patch["capture"]);
        const juce::ScopedLock sl (nameLock);
        mutHistory.clear();
        if (patch["mutationHistory"].isArray()) for (auto& h : *patch["mutationHistory"].getArray()) mutHistory.add (h.toString());
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
    for (int k = 0; k < kSampleSlots; ++k)
    {
        const juce::var wt = patch[samplePatchKey (k)];
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
    resetPatchState();
    setPatchMeta ({});
    setPatchName ("Init");
    pushHistory ("Init");
    markOriginal();
}

juce::File MegaSynthProcessor::getUserInitFile()
{
    const auto over = juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_DATA_DIR", {});
    juce::File dir;
    if (over.isNotEmpty()) dir = juce::File (over);
    else
    {
       #if JUCE_MAC
        dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support").getChildFile ("Mega Synth");
       #else
        dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Mega Synth");
       #endif
    }
    return dir.getChildFile ("UserInit.megasynth-init");
}

bool MegaSynthProcessor::hasUserInit() { return getUserInitFile().existsAsFile(); }

void MegaSynthProcessor::applyFactoryInit()
{
    setPatchMeta ({});
    resetPatchState();
    auto set = [this] (int idx, float plain)
    {
        auto* prm = params[(size_t) idx];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    };
    // one saw on Osc 1; every other source off
    set (P_osc1Wave, 0.0f); set (P_osc1Gain, 0.8f);
    for (int i : { P_osc2Gain, P_osc3Gain, P_subGain, P_osc4Gain, P_wt2Gain, P_complexGain, P_supersawGain, P_ringGain })
        set (i, 0.0f);
    set (P_warmth, 0.0f); set (P_analogDrift, 0.0f);
    // filter open, no envelope sweep
    set (P_filterCutoff, 20000.0f); set (P_fEnvAmt, 0.0f); set (P_envAssignAmt0, 0.0f);
    // a plain amp envelope
    set (P_ampA, 0.005f); set (P_ampD, 0.2f); set (P_ampS, 1.0f); set (P_ampR, 0.25f);
    // no effects
    for (int i : { P_delayMix, P_reverbMix, P_chorusMix, P_shimmerMix, P_reverseMix }) set (i, 0.0f);
    for (int i : { P_fxOnDelay, P_fxOnChorus, P_fxOnReverb }) set (i, 0.0f);
    // no vibrato until the mod wheel asks for it: the wheel adds up to about 25 cents of LFO 1 (5 Hz) to the pitch
    set (P_lfoAssignAmt0, 0.0f);
    set (P_wheelLfo1, 0.0f);
    addRoute (MS_ModWheel, P_lfoAssignAmt0, kInitWheelVibrato);
}

void MegaSynthProcessor::applyInitState()
{
    if (hasUserInit())
    {
        const auto text = getUserInitFile().loadFileAsString();
        resetPatchState();
        if (importBrowserPatch (text).isEmpty()) return;
    }
    applyFactoryInit();
}

// A new instance starts from Init, applied on the message thread at the processor's first timer tick or
// when its editor opens, and only if the host hasn't restored a project (or chosen a program) by then.
// (Not in prepareToPlay: changing parameters while the host is activating the plug-in upsets some hosts.) Applying it lazily means an existing
// project always loads into an untouched instance, so it comes back exactly as it did before Init existed.
void MegaSynthProcessor::initOnFirstUse()
{
    if (initDone || stateRestored || ! applyInitOnCreate.load()) return;
    initDone = true;
    applyInitState();
    setPatchName ("Init");
    pushHistory ("Init");
    markOriginal();
}

void MegaSynthProcessor::loadInitPatch()
{
    applyInitState();
    setPatchName ("Init");
    pushHistory ("Init");
    markOriginal();
}

bool MegaSynthProcessor::saveCurrentAsInit()
{
    auto f = getUserInitFile();
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (exportBrowserPatch());
}

bool MegaSynthProcessor::resetInitToFactory()
{
    auto f = getUserInitFile();
    return ! f.existsAsFile() || f.deleteFile();
}

void MegaSynthProcessor::resetPatchState()
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
    dnaSteps.fromString ({});
    fxRack.fromVar ({});
    lfoShapes.fromVar ({});
    arpStore.fromVar ({});
    msegs.fromVar ({});
    scenes.clear();
    sceneEditIndex = 0;
    for (int k = 0; k < 8; ++k) setMacroName (k, {});
    lastMorph = false;
    for (int k = 0; k < kSampleSlots; ++k) clearSample (k);
    breakStore.reset();
    lastEuclid[0] = -1;
}

void MegaSynthProcessor::setMasterDefaultsForOldPatch()
{
    // Before the Master Filter and the Output trim: filter off, Output 0 dB
    for (int i = 0; i < P_COUNT; ++i)
    {
        const juce::String id (kParamIds[i]);
        if (! (id.startsWith ("mf") || id == "outGain")) continue;
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setPerfDefaultsForOldPatch()
{
    // Before Performance FX: every pad off, the settings and XY pads at their defaults
    for (int i = P_pfRoll; i <= P_pfSpring2; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setGranDefaultsForOldPatch()
{
    // Before the Granular Synth: off, its settings and Mixer strip at their defaults
    for (int i = P_gsOn; i <= P_mxGranSolo; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setBreakDefaultsForOldPatch()
{
    // Before the Break Chopper and its sequencer: off, defaults, an empty pattern store
    for (int i = P_bcOn; i <= P_bsSwing; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
    breakStore.reset();
}

void MegaSynthProcessor::setBreak3DefaultsForOldPatch()
{
    // Before three breaks: Break 1 active at full level, and the Break Select keys off (in an older
    // project those keys may be notes the synth plays)
    for (int i = P_bcActive; i <= P_bsAmount; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (i == P_bcSelKeys ? 0.0f : prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setChopDefaultsForOldPatch()
{
    for (int i = P_bcChopMode; i <= P_bcMarkerSnap; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setReactDefaultsForOldPatch()
{
    for (int i = P_rxOn; i <= P_rxReese; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setReeseDefaultsForOldPatch()
{
    for (int i = P_rsLevel; i <= P_mxReeseSolo; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setAkaiDefaultsForOldPatch()
{
    for (int i = P_pfAkai; i <= P_pfAkaiMix; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setRouteDefaultsForOldPatch()
{
    for (int i = P_subRoute; i <= P_subMono; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setMfModelDefaultsForOldPatch()
{
    auto* prm = params[(size_t) P_mfModel];
    prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
}

void MegaSynthProcessor::setFillDefaultsForOldPatch()
{
    for (int i = P_bsFill; i <= P_bsAutoFillProb; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setToolDefaultsForOldPatch()
{
    for (int i = P_bsToolSource; i <= P_bsVarAmount; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setCharDefaultsForOldPatch()
{
    // Before the character models: every filter on its classic model
    for (int i : { P_flt1Char, P_flt1CharAmt, P_flt2Char, P_flt2CharAmt, P_mfChar, P_mfCharAmt })
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setMixerDefaultsForOldPatch()
{
    // Before the Mixer's channel strips: every source centred, nothing muted or soloed
    for (int i = P_mxOsc1Pan; i < P_mxOsc1Pan + kMxStride * kMxSources; ++i)
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setMidiDefaultsForOldPatch()
{
    // Before the switches: the wheel always set LFO 1 Depth, and there was no sustain pedal (its default is on)
    for (int i : { P_wheelLfo1, P_sustainPedal })
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setEnvDefaultsForOldPatch()
{
    // Before the pitch envelope and the MSEGs: no pitch envelope, default MSEG shapes
    for (int i = 0; i < P_COUNT; ++i)
    {
        const juce::String id (kParamIds[i]);
        if (! (id.startsWith ("pEnv") || id.startsWith ("mseg"))) continue;
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
    msegs.fromVar ({});
}

void MegaSynthProcessor::setUnisonDefaultsForOldPatch()
{
    // Before per-oscillator unison: one voice each
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (! juce::String (kParamIds[i]).startsWith ("uni")) continue;
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setSidechainDefaultsForOldPatch()
{
    // Before the sidechain input: nothing listens to it
    for (int i : { P_vshSidechain, P_mbcSidechain, P_schThresh, P_schAttack, P_schRelease })
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setArpDefaultsForOldPatch()
{
    // Before chord memory and the arpeggiator: both off, notes play as they always did
    for (int i = 0; i < P_COUNT; ++i)
    {
        const juce::String id (kParamIds[i]);
        if (! (id.startsWith ("arp") || id.startsWith ("chd"))) continue;
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
    arpStore.fromVar ({});
}

void MegaSynthProcessor::setLfoDefaultsForOldPatch()
{
    // Before LFO sync and shapes: Hz rate, each note restarts the LFO, no phase / smoothing / fade, the waveform
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (! isLfoV2Param (kParamIds[i])) continue;
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
    lfoShapes.fromVar ({});
}

void MegaSynthProcessor::setFxDefaultsForOldPatch()
{
    // Before the effects rack: the new effects off, the original ones on at full return
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (! isFxV2Param (kParamIds[i])) continue;
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::setDnaDefaultsForOldPatch()
{
    // Before the DNA Sequencer update: host sync, no restart / offset / swing, forward, one lane,
    // pattern A, everything plays. A sequence on the old "Free" rate keeps running free.
    for (int i : { P_dsSync, P_dsRestart, P_dsOffset, P_dsSwing, P_dsSwingGrid, P_dsDirection, P_dsLane2On, P_dsLane2Steps,
                   P_dsPattern, P_dsChainOn, P_dsDensity, P_dsProbScale, P_dsMidiSelect })
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
    if ((int) std::lround (raw[P_dsRate]->load()) == 8)
    {
        auto* prm = params[(size_t) P_dsSync];
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) tg::DS_Free));
    }
}

void MegaSynthProcessor::setFilter2DefaultsForOldPatch()
{
    // Before Filter 2 existed: it's off, so the patch sounds as it did
    for (int i : { P_filter2On, P_filterRouting, P_filterBalance, P_filterStereoSplit, P_filter2Mode, P_filter2Type, P_filter2Slope, P_filter2Cutoff,
                   P_filter2Res, P_filter2Drive, P_filter2Mix, P_filter2EnvAmt, P_filter2KeyTrack, P_filter2LfoAmt, P_filter2LfoSrc })
    {
        auto* prm = params[(size_t) i];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
    }
}

void MegaSynthProcessor::dnaEdit (DnaEdit e, int lane)
{
    lane = juce::jlimit (0, 1, lane);
    const int pat = dnaEditPattern(), len = dnaLaneLength (lane);
    auto& g = dnaSteps.gen;
    juce::String label;
    switch (e)
    {
        case DE_Generate:
            g.seed = juce::Random::getSystemRandom().nextInt ({ 1, 1000000 });
            tg::dnaGenerate (dnaSteps, pat, lane, len, g); label = "Generate DNA pattern"; break;
        case DE_SeedDown: g.seed = std::max (1, g.seed - 1); tg::dnaGenerate (dnaSteps, pat, lane, len, g); label = "DNA seed"; break;
        case DE_SeedUp:   g.seed = g.seed + 1;               tg::dnaGenerate (dnaSteps, pat, lane, len, g); label = "DNA seed"; break;
        case DE_Copy:     dnaClip = tg::dnaGetLane (dnaSteps, pat, lane); dnaClipValid = true; return;
        case DE_Paste:    if (! dnaClipValid) return; tg::dnaSetLane (dnaSteps, pat, lane, dnaClip); label = "Paste DNA steps"; break;
        case DE_ShiftLeft:  tg::dnaShift (dnaSteps, pat, lane, len, -1); label = "Shift DNA steps"; break;
        case DE_ShiftRight: tg::dnaShift (dnaSteps, pat, lane, len, 1); label = "Shift DNA steps"; break;
        case DE_Reverse:  tg::dnaReverse (dnaSteps, pat, lane, len); label = "Reverse DNA steps"; break;
        case DE_Clear:    tg::dnaClear (dnaSteps, pat, lane); label = "Clear DNA steps"; break;
    }
    lastDnaVersion = dnaSteps.getVersion();   // recorded here with a name, not again as "Edit"
    pushHistory (label);
}

void MegaSynthProcessor::copyFilter1To2()
{
    const std::pair<int, int> map[] = { { P_filterMode, P_filter2Mode }, { P_filterType, P_filter2Type }, { P_filterSlope, P_filter2Slope },
                                        { P_filterCutoff, P_filter2Cutoff }, { P_filterRes, P_filter2Res }, { P_filterDrive, P_filter2Drive },
                                        { P_filterMix, P_filter2Mix }, { P_fEnvAmt, P_filter2EnvAmt }, { P_filterKeyTrack, P_filter2KeyTrack },
                                        { P_filterLfoAmt, P_filter2LfoAmt }, { P_filterLfoSrc, P_filter2LfoSrc } };
    for (auto [from, to] : map)
    {
        auto* prm = params[(size_t) to];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (params[(size_t) from]->convertFrom0to1 (params[(size_t) from]->getValue())));
        prm->endChangeGesture();
    }
    pushHistory ("Copy filter 1 to 2");
}

void MegaSynthProcessor::setFilterDefaultsForOldPatch()
{
    // Before the overhaul every model was a low-pass at its own slope, fully wet, with no key
    // tracking and no filter LFO: set exactly that, so old patches sound as they did.
    auto setPlain = [this] (int idx, float plain)
    {
        auto* prm = params[(size_t) idx];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    };
    setPlain (P_filterType, (float) FT_LP);
    setPlain (P_filterSlope, (float) nativeFilterSlope ((int) std::lround (raw[P_filterMode]->load())));
    setPlain (P_filterMix, 1.0f);
    setPlain (P_filterKeyTrack, 0.0f);
    setPlain (P_filterLfoAmt, 0.0f);
    setPlain (P_filterLfoSrc, 0.0f);
}

// VST3 turns programs into an automatable "Program" parameter; a host or validator that sets it along
// with everything else would load a preset over the values it just set. So VST3 shows one program and
// uses the patch browser (and MIDI Program Change); AU, standalone and the rest list the factory presets.
int MegaSynthProcessor::getNumPrograms() { return wrapperType == wrapperType_VST3 ? 1 : (int) tg::factoryPresets().size(); }

const juce::String MegaSynthProcessor::getProgramName (int i)
{
    const auto& list = tg::factoryPresets();
    if (getNumPrograms() == 1) return i == 0 ? juce::String ("Mega Synth") : juce::String();
    return i >= 0 && i < (int) list.size() ? juce::String (list[(size_t) i].name) : juce::String();
}

void MegaSynthProcessor::setCurrentProgram (int i)
{
    if (i < 0 || i >= getNumPrograms() || i == curProgram.load()) return;
    stateRestored = true;
    curProgram.store (i);
    if (juce::MessageManager::existsAndIsCurrentThread()) { pendingProgram.store (-1); loadFactoryPreset (i); }
    else pendingProgram.store (i);   // picked up by the timer
}

bool MegaSynthProcessor::loadFactoryPreset (int index)
{
    const auto& list = tg::factoryPresets();
    if (index < 0 || index >= (int) list.size()) return false;
    curProgram.store (index);
    tg::PresetBuilder b;
    tg::buildFactoryPreset (index, b);

    // Quality and the keyboard octave are the player's settings, not part of the sound.
    const float keepQuality = raw[tg::P_quality]->load(), keepOct = raw[tg::P_keyboardOctave]->load();
    resetPatchState();
    b.v[tg::P_quality] = keepQuality;
    b.v[tg::P_keyboardOctave] = keepOct;

    for (int i = 0; i < (int) b.routes.size() && i < tg::kNumRoutes; ++i)
    {
        const auto& r = b.routes[(size_t) i];
        tg::RouteConfig c;
        c.on = true; c.src = r.src; c.dst = r.dst; c.curve = r.curve; c.unipolar = r.unipolar;
        c.via = r.via; c.viaDepth = r.viaDepth; c.smoothMs = r.smoothMs;
        routes.set (i, c);
        b.v[(size_t) (tg::P_mod1Amt + i)] = r.depth;
    }
    for (int k = 0; k < 4; ++k)
        if (b.scenes[k]) scenes.store (k, b.scene[(size_t) k].data());
    dnaSteps.fromString (b.dnaString());
    for (auto& d : b.dnaSteps2) dnaSteps.set (d.pat, d.lane, d.i, d.s);
    if (b.dnaChain.isNotEmpty()) dnaSteps.setChain (b.dnaChain);
    if (b.rackSet) fxRack.setOrder (b.rack);
    if (b.shaperCurve != 0) fxRack.setCurvePreset (b.shaperCurve);
    for (int k = 0; k < 4; ++k) if (b.lfoShape[k] >= 0) lfoShapes.setPreset (k, b.lfoShape[k]);
    for (auto& st : b.arpPattern) arpStore.setStep (st.i, st.state, st.prob);
    if (! b.learnedChord.empty()) arpStore.setLearned (b.learnedChord);
    for (int k = 0; k < 8; ++k) setMacroName (k, b.macroNames[k]);

    for (int i = 0; i < tg::P_COUNT; ++i)
    {
        auto* p = params[(size_t) i];
        const float nv = p->convertTo0to1 (b.v[(size_t) i]);
        if (std::abs (nv - p->getValue()) < 1.0e-7f) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost (nv);
        p->endChangeGesture();
    }
    // the Break Chopper's demo break (synthesised here, not a sample of a record), sliced, then its patterns
    if (b.breakDemo >= 0)
    {
        loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 170.0, b.breakDemo), 44100.0), "Mega Break 170 (synthesised).wav", kSlotBreak);
        if (b.breakDemo2 >= 0) loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 174.0, b.breakDemo2), 44100.0), "Mega Break 2-Step 174 (synthesised).wav", breakSlot (1));
        if (b.breakDemo3 >= 0) loadSampleData (tg::breaks::toWav16 (tg::breaks::makeDemoBreak (44100.0, 165.0, b.breakDemo3), 44100.0), "Mega Break Rollers 165 (synthesised).wav", breakSlot (2));
        refreshBreakAnalysis (true);
        if (b.breakEdit) breakStore.edit (b.breakEdit);
    }
    setPatchName (list[(size_t) index].name);
    setPatchMeta (factoryMetaFor (index));
    pushHistory ("Load " + juce::String (list[(size_t) index].name));
    markOriginal();
    return true;
}

juce::File MegaSynthProcessor::getPatchFolder()
{
    const auto over = juce::SystemStats::getEnvironmentVariable ("MEGASYNTH_PATCH_DIR", {});   // (tests)
    auto dir = over.isNotEmpty() ? juce::File (over) : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Mega Synth").getChildFile ("Patches");
    dir.createDirectory();
    return dir;
}

juce::String MegaSynthProcessor::getDefaultAuthor() { return getUserInitFile().getSiblingFile ("author.txt").loadFileAsString().trim(); }
juce::File MegaSynthProcessor::getAuthorListFile() { return getPatchFolder().getChildFile ("authors.json"); }

namespace
{
    struct AuthorStore { juce::StringArray recent, hidden; };
    AuthorStore loadAuthors()
    {
        AuthorStore st;
        const auto v = juce::JSON::parse (MegaSynthProcessor::getAuthorListFile().loadFileAsString());
        if (auto* a = v.getProperty ("recent", {}).getArray()) for (auto& x : *a) if (x.toString().trim().isNotEmpty()) st.recent.add (x.toString().trim());
        if (auto* a = v.getProperty ("forgotten", {}).getArray()) for (auto& x : *a) if (x.toString().trim().isNotEmpty()) st.hidden.add (x.toString().trim());
        return st;
    }
    void saveAuthors (const AuthorStore& st)
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> r, h; for (auto& x : st.recent) r.add (x); for (auto& x : st.hidden) h.add (x);
        o->setProperty ("recent", r); o->setProperty ("forgotten", h);
        MegaSynthProcessor::getAuthorListFile().replaceWithText (juce::JSON::toString (juce::var (o)));
    }
    bool containsNoCase (const juce::StringArray& a, const juce::String& s) { return a.contains (s, true); }
}

juce::StringArray MegaSynthProcessor::getAuthorList (const juce::StringArray& foundInPatches)
{
    const auto st = loadAuthors();
    juce::StringArray out;
    for (auto& a : st.recent) if (! containsNoCase (out, a)) out.add (a);
    juce::StringArray extra;
    for (auto& a : foundInPatches)
    {
        const auto t = a.trim();
        if (t.isNotEmpty() && ! containsNoCase (out, t) && ! containsNoCase (extra, t) && ! containsNoCase (st.hidden, t)) extra.add (t);
    }
    extra.sortNatural();
    out.addArray (extra);
    return out;
}

void MegaSynthProcessor::rememberAuthor (const juce::String& name)
{
    const auto n = name.trim();
    if (n.isEmpty()) return;
    auto st = loadAuthors();
    for (int i = st.recent.size(); --i >= 0;) if (st.recent[i].equalsIgnoreCase (n)) st.recent.remove (i);
    for (int i = st.hidden.size(); --i >= 0;) if (st.hidden[i].equalsIgnoreCase (n)) st.hidden.remove (i);
    st.recent.insert (0, n);
    saveAuthors (st);
}

void MegaSynthProcessor::forgetAuthor (const juce::String& name)
{
    const auto n = name.trim();
    if (n.isEmpty()) return;
    auto st = loadAuthors();
    for (int i = st.recent.size(); --i >= 0;) if (st.recent[i].equalsIgnoreCase (n)) st.recent.remove (i);
    if (! containsNoCase (st.hidden, n)) st.hidden.add (n);
    saveAuthors (st);
}

void MegaSynthProcessor::setDefaultAuthor (const juce::String& a)
{
    rememberAuthor (a);
    auto f = getUserInitFile().getSiblingFile ("author.txt");
    f.getParentDirectory().createDirectory();
    f.replaceWithText (a.trim());
}

juce::File MegaSynthProcessor::getDeletedFolder() { return getPatchFolder().getChildFile ("Deleted"); }
juce::File MegaSynthProcessor::getFactoryMetaFile() { return getPatchFolder().getChildFile ("factory_meta.json"); }

tg::PatchMeta MegaSynthProcessor::factoryMetaFor (int index)
{
    auto m = tg::factoryMeta (index);
    const auto f = getFactoryMetaFile();
    if (f.existsAsFile() && index >= 0 && index < (int) tg::factoryPresets().size())
    {
        const auto all = juce::JSON::parse (f.loadFileAsString());
        const auto v = all.getProperty (juce::String ("factory:") + tg::factoryPresets()[(size_t) index].name, {});
        if (v.isObject()) m = tg::PatchMeta::fromVar (v);
    }
    return m;
}

juce::Array<juce::File> MegaSynthProcessor::getPatchFiles()
{
    auto files = getPatchFolder().findChildFiles (juce::File::findFiles, true, "*.megasynth");
    const auto bin = getDeletedFolder();
    files.removeIf ([&bin] (const juce::File& f) { return f.isAChildOf (bin); });   // (deleted patches aren't listed)
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
    s.dnaSteps = dnaSteps.toJson();
    s.fxRack = juce::JSON::toString (fxRack.toVar(), true);
    s.lfoShapes = lfoShapes.toJson();
    s.arpStore = arpStore.toJson();
    s.breakStore = breakStore.toJson();
    s.msegs = msegs.toJson();
    s.scenes = juce::JSON::toString (scenes.toVar(), true);
    s.sceneEdit = getEditScene();
    s.macroNames = macroNamesJoined();
    s.patchName = getPatchName();
    s.meta = getPatchMeta();
    s.label = label;
    const juce::ScopedLock sl (waveLock);
    for (int k = 0; k < kSampleSlots; ++k) { s.wave[k] = waveShared[k]; s.waveName[k] = waveName[k]; }
    return s;
}

bool MegaSynthProcessor::sameState (const Snapshot& a, const Snapshot& b) const
{
    return a.steps == b.steps && a.routes == b.routes && a.dnaSteps == b.dnaSteps && a.fxRack == b.fxRack && a.lfoShapes == b.lfoShapes && a.arpStore == b.arpStore && a.breakStore == b.breakStore && a.msegs == b.msegs && a.scenes == b.scenes && a.macroNames == b.macroNames
        && a.sceneEdit == b.sceneEdit && a.patchName == b.patchName && a.meta == b.meta
        && a.wave[0] == b.wave[0] && a.wave[1] == b.wave[1] && a.wave[2] == b.wave[2] && a.wave[3] == b.wave[3] && a.wave[4] == b.wave[4] && a.wave[5] == b.wave[5]
        && a.params.isEquivalentTo (b.params);
}

void MegaSynthProcessor::restoreSnapshot (const Snapshot& s)
{
    restoring = true;
    apvts.replaceState (s.params.createCopy());
    steps.fromString (s.steps);
    routes.fromVar (juce::JSON::parse (s.routes));
    lastRoutesVersion = routes.getVersion();
    dnaSteps.fromJsonOrLegacy (s.dnaSteps);
    lastDnaVersion = dnaSteps.getVersion();
    fxRack.fromVar (juce::JSON::parse (s.fxRack));
    lastRackVersion = fxRack.getVersion();
    lfoShapes.fromVar (juce::JSON::parse (s.lfoShapes));
    lastLfoShapeVersion = lfoShapes.getVersion();
    arpStore.fromVar (juce::JSON::parse (s.arpStore));
    lastArpVersion = arpStore.getVersion();
    breakStore.fromVar (juce::JSON::parse (s.breakStore));
    lastBreakVersion = breakStore.getVersion();
    msegs.fromVar (juce::JSON::parse (s.msegs));
    lastMsegVersion = msegs.getVersion();
    scenes.fromVar (juce::JSON::parse (s.scenes));
    lastScenesVersion = scenes.version.load();
    sceneEditIndex = s.sceneEdit;
    setMacroNamesJoined (s.macroNames);
    lastMorph = raw[P_sceneMorph]->load() > 0.5f;
    setPatchName (s.patchName);
    setPatchMeta (s.meta);
    for (int k = 0; k < kSampleSlots; ++k)
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
    abOther = original;
    abSel = 0;
}

void MegaSynthProcessor::abSelect (int slot)
{
    const juce::ScopedLock sl (historyLock);
    slot = slot != 0 ? 1 : 0;
    if (slot == abSel) return;
    pushHistory ("Edit");
    auto current = captureSnapshot (slot == 0 ? "Compare A" : "Compare B");
    if (abOther.params.isValid()) restoreSnapshot (abOther);
    abOther = std::move (current);
    abSel = slot;
    pushHistory (slot == 0 ? "Compare A" : "Compare B");
}

void MegaSynthProcessor::abCopyToOther()
{
    const juce::ScopedLock sl (historyLock);
    abOther = captureSnapshot ("Copy");
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

//==============================================================================
tg::LabPatch MegaSynthProcessor::currentAsLabPatch (const juce::String& name)
{
    tg::LabPatch p;
    p.name = name;
    p.values.resize (P_COUNT);
    for (int i = 0; i < P_COUNT; ++i) p.values[(size_t) i] = raw[(size_t) i]->load();
    p.routes = routes.toVar();
    return p;
}

void MegaSynthProcessor::labSetParent (int which)
{
    // if the panel is showing a lab patch, that one becomes the parent (keeping its family tree)
    juce::String id;
    if (const auto* cur = lab.find (lab.current))
    {
        bool same = true;
        for (int i = 0; i < P_COUNT && same; ++i)
            if (tg::geneGroupOf (i) >= 0 && std::abs (cur->values[(size_t) i] - raw[(size_t) i]->load()) > 1.0e-4f * std::max (1.0f, std::abs (cur->values[(size_t) i]))) same = false;
        if (same) id = cur->id;
    }
    if (id.isEmpty()) id = lab.add (currentAsLabPatch (getPatchName().isNotEmpty() ? getPatchName() : juce::String ("Patch")));
    (which == 0 ? lab.parentA : lab.parentB) = id;
}

void MegaSynthProcessor::labSetParentFromId (int which, const juce::String& id)
{
    if (lab.find (id) != nullptr) (which == 0 ? lab.parentA : lab.parentB) = id;
}

int MegaSynthProcessor::labBreed()
{
    if (lab.find (lab.parentA) == nullptr || lab.find (lab.parentB) == nullptr) return -1;
    return lab.breed (lab.parentA, lab.parentB, labVariation, (uint32_t) rng.nextInt());
}

void MegaSynthProcessor::labAudition (const juce::String& id)
{
    const auto* p = lab.find (id);
    if (p == nullptr) return;
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (tg::geneGroupOf (i) < 0 || i >= (int) p->values.size()) continue;
        auto* prm = params[(size_t) i];
        const float nv = prm->convertTo0to1 (p->values[(size_t) i]);
        if (std::abs (nv - prm->getValue()) < 1.0e-7f) continue;
        prm->beginChangeGesture(); prm->setValueNotifyingHost (nv); prm->endChangeGesture();
    }
    routes.fromVar (p->routes);
    lab.current = id;
    setPatchName (p->name);
    pushHistory ("Lab: " + p->name);
}

//==============================================================================
void MegaSynthProcessor::mutateNewSeed()
{
    auto* p = params[P_mutSeed];
    p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) rng.nextInt (1000000))); p->endChangeGesture();
}

void MegaSynthProcessor::mutateStepSeed (int delta)
{
    auto* p = params[P_mutSeed];
    const int cur = juce::roundToInt (raw[P_mutSeed]->load());
    const int nv = ((cur + delta) % 1000000 + 1000000) % 1000000;
    p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) nv)); p->endChangeGesture();
}

void MegaSynthProcessor::commitMutation()
{
    const float amount = raw[P_mutAmount]->load();
    if (amount <= 1.0e-4f) return;
    float v[P_COUNT];
    for (int i = 0; i < P_COUNT; ++i) v[i] = raw[(size_t) i]->load();
    tg::MutationTable t;
    t.build ((uint32_t) juce::roundToInt (v[P_mutSeed]));
    uint32_t mask = 0;
    for (int k = 0; k < tg::ML_COUNT; ++k) if (v[P_mutLock1 + k] > 0.5f) mask |= 1u << k;
    float out[P_COUNT];
    std::copy (v, v + P_COUNT, out);
    tg::applyMutation (t, amount, mask, out);
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (out[i] == v[i]) continue;
        auto* p = params[(size_t) i];
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (out[i])); p->endChangeGesture();
    }
    auto* a = params[P_mutAmount];
    a->beginChangeGesture(); a->setValueNotifyingHost (0.0f); a->endChangeGesture();
    {
        const juce::ScopedLock sl (nameLock);
        mutHistory.add ("Seed #" + juce::String (juce::roundToInt (v[P_mutSeed])) + " at " + juce::String (juce::roundToInt (amount * 100.0f)) + "%");
        while (mutHistory.size() > 50) mutHistory.remove (0);
    }
    pushHistory ("Commit mutation");
}

//==============================================================================
void MegaSynthProcessor::startCapture()
{
    if (capBuf.getNumSamples() == 0) return;   // not prepared yet
    capReady = false; capStopReq = false; capPos = 0;
    capRecording = true;
}

void MegaSynthProcessor::stopCapture()
{
    if (capRecording.load()) capStopReq = true;   // the audio thread finishes the block it's in, then hands over
}

void MegaSynthProcessor::pollCapture()
{
    if (! capReady.load()) return;
    capReady = false;
    const int n = capPos.load();
    if (n < 64) return;
    capRaw.setSize (2, n);
    for (int c = 0; c < 2; ++c) capRaw.copyFrom (c, 0, capBuf, c, 0, n);
    capRate = sampleRate;
    captureSettings.trimStart = 0.0f; captureSettings.trimEnd = 1.0f;
}

juce::AudioBuffer<float> MegaSynthProcessor::getEditedCapture() const
{
    return tg::capture::render (capRaw, captureSettings, capRate);
}

double MegaSynthProcessor::detectCapturePitch() const
{
    return tg::capture::detectPitch (getEditedCapture(), capRate);
}

juce::String MegaSynthProcessor::sendCapture (CaptureTarget t)
{
    if (! hasCapture()) return "Nothing captured yet";
    auto edited = getEditedCapture();
    if (edited.getNumSamples() < 16) return "The trimmed capture is too short";
    const double hz = tg::capture::detectPitch (edited, capRate);
    auto setPlain = [this] (int idx, float v)
    {
        auto* p = params[(size_t) idx];
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
    };
    auto toSlot = [&] (const juce::AudioBuffer<float>& b, int slot, const juce::String& name)
    {
        if (! loadSampleData (tg::capture::toWav (b, capRate), name, slot)) return false;
        // tune the slot so the captured pitch plays back at the note you press
        if (hz > 0.0)
            setPlain (slot == 0 ? P_osc4Root : P_wt2Root, (float) juce::jlimit (24, 84, (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0))));
        return true;
    };
    const juce::String stamp = juce::Time::getCurrentTime().formatted ("%H%M%S");
    switch (t)
    {
        case CT_WT1: case CT_WT2: case CT_DnaA: case CT_DnaB:
        {
            const int slot = (t == CT_WT1 || t == CT_DnaA) ? 0 : 1;
            if (! toSlot (edited, slot, "Capture " + stamp + ".wav")) return "Couldn't load the capture";
            if (t == CT_DnaA) setPlain (P_dnaA, 4.0f + (float) slot);
            if (t == CT_DnaB) setPlain (P_dnaB, 4.0f + (float) slot);
            break;
        }
        case CT_CycleWT1: case CT_CycleWT2:
        {
            auto cyc = tg::capture::singleCycle (edited, capRate, hz);
            if (cyc.getNumSamples() == 0) return "No steady pitch found for a single cycle";
            const int slot = t == CT_CycleWT1 ? 0 : 1;
            // the cycle is stored at a rate that makes it exactly the root note's pitch, so it plays in tune
            const int rootMidi = juce::jlimit (24, 84, (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
            const double rootHz = 440.0 * std::exp2 ((rootMidi - 69) / 12.0);
            if (! loadSampleData (tg::capture::toWav (cyc, std::round (tg::capture::kCycleLen * rootHz)), "Cycle " + stamp + ".wav", slot))
                return "Couldn't load the cycle";
            setPlain (slot == 0 ? P_osc4Root : P_wt2Root, (float) rootMidi);
            setPlain (slot == 0 ? P_osc4LoopMode : P_wt2LoopMode, 0.0f);   // loop the whole (single-cycle) buffer
            break;
        }
        case CT_Granular:
        {
            if (grainLoadReady.load()) return "Busy - try again";
            const int n = std::min (edited.getNumSamples(), grainLoad.getNumSamples());
            for (int c = 0; c < 2; ++c) grainLoad.copyFrom (c, 0, edited, std::min (c, edited.getNumChannels() - 1), edited.getNumSamples() - n, n);
            grainLoadLen = n;
            setPlain (P_grFreeze, 1.0f);
            if (raw[P_grMix]->load() < 0.01f) setPlain (P_grMix, 1.0f);
            grainLoadReady = true;   // after the parameters, so the granular stage is on when the buffer arrives
            break;
        }
    }
    pushHistory ("Send capture");
    return {};
}

juce::var MegaSynthProcessor::captureToVar() const
{
    if (! hasCapture()) return {};
    auto* o = new juce::DynamicObject();
    const auto mb = tg::capture::toWav (capRaw, capRate);
    o->setProperty ("data", "data:audio/wav;base64," + juce::Base64::toBase64 (mb.getData(), mb.getSize()));
    o->setProperty ("trimStart", captureSettings.trimStart);
    o->setProperty ("trimEnd", captureSettings.trimEnd);
    o->setProperty ("fadeInMs", captureSettings.fadeInMs);
    o->setProperty ("fadeOutMs", captureSettings.fadeOutMs);
    o->setProperty ("reverse", captureSettings.reverse);
    o->setProperty ("normalise", captureSettings.normalise);
    o->setProperty ("zeroCross", captureSettings.zeroCross);
    return juce::var (o);
}

void MegaSynthProcessor::captureFromVar (const juce::var& v)
{
    capRaw.setSize (2, 0);
    captureSettings = {};
    if (! v.isObject()) return;
    const juce::String b64 = v["data"].toString().fromFirstOccurrenceOf ("base64,", false, false);
    juce::MemoryOutputStream mo;
    if (b64.isEmpty() || ! juce::Base64::convertFromBase64 (mo, b64)) return;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (mo.getMemoryBlock(), true)));
    if (reader == nullptr) return;
    const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * kCaptureSeconds) + 8);
    capRaw.setSize (2, n);
    reader->read (&capRaw, 0, n, 0, true, true);
    capRate = reader->sampleRate;
    captureSettings.trimStart = (float) (double) v.getProperty ("trimStart", 0.0);
    captureSettings.trimEnd = (float) (double) v.getProperty ("trimEnd", 1.0);
    captureSettings.fadeInMs = (float) (double) v.getProperty ("fadeInMs", 2.0);
    captureSettings.fadeOutMs = (float) (double) v.getProperty ("fadeOutMs", 10.0);
    captureSettings.reverse = (bool) v.getProperty ("reverse", false);
    captureSettings.normalise = (bool) v.getProperty ("normalise", true);
    captureSettings.zeroCross = (bool) v.getProperty ("zeroCross", true);
}

bool MegaSynthProcessor::saveCaptureWav (const juce::File& f) const
{
    if (! hasCapture()) return false;
    const auto mb = tg::capture::toWav (getEditedCapture(), capRate);
    return mb.getSize() > 0 && f.replaceWithData (mb.getData(), mb.getSize());
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
    initOnFirstUse();
    return new MegaSynthEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MegaSynthProcessor();
}
