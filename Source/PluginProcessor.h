#pragma once
#include <JuceHeader.h>
#include "PatchMeta.h"
#include "Params.h"
#include "Engine.h"
#include "Break/BreakAnalysis.h"
#include "Break/BreakTools.h"
#include "Sequencer.h"
#include "Mut/Capture.h"
#include "Lab/Breeder.h"
#include "Presets/Factory.h"
#include "Seq/DnaGenerator.h"
#include "Seq/Arp.h"
#include "Lab/RandomPatch.h"

class MegaSynthProcessor : public juce::AudioProcessor,
                           private juce::Timer,
                           private juce::AudioProcessorListener
{
public:
    MegaSynthProcessor();
    ~MegaSynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    // Host programs = the factory presets. A program change from the host (or a MIDI Program Change)
    // loads on the message thread; asking for the program that's already current does nothing, so a
    // host restoring its program number after a project loads never overwrites the restored patch.
    int getNumPrograms() override;
    int getCurrentProgram() override { return getNumPrograms() == 1 ? 0 : curProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    tg::StepStore steps;
    std::atomic<int> currentStep { -1 };
    tg::RouteStore routes;
    tg::DnaSeqStore dnaSteps;                 // Sonic DNA Sequencer steps
    tg::FxRackStore fxRack;                   // effects rack order and the Volume Shaper curve
    tg::LfoShapeStore lfoShapes;              // the four LFOs' custom shapes
    tg::ArpStore arpStore;                    // the arpeggiator's step pattern and the learned chord
    tg::MsegStore msegs;                      // the two multi-stage envelopes
    tg::BreakStore breakStore;                // the Break Chopper's slice settings and markers, the Break Sequencer's patterns
    void refreshBreakAnalysis (bool force = false);
    bool matchBreakLevels();
    // ---- chop mode (manual chopping): taps while the break plays become slice markers
    std::atomic<double> breakPreviewReq { -1.0 };   // the editor: play from this fraction (0..1), or -2 to stop
    std::atomic<double> previewFrameShown { -1.0 }, previewFileRate { 1.0 };
    std::atomic<double> previewStampMs { 0.0 };      // when that frame was being played (for taps from the editor)
    std::atomic<int> previewBreakShown { 0 };
    bool previewIsPlaying() const { return previewFrameShown.load() >= 0.0; }
    void tapChop();                                  // the editor's Tap button or space bar (message thread)
    bool addChopMarker (int brk, double frame, bool snap = true);   // message thread
    bool undoLastChop();
    void applyChopTaps();                            // taps from MIDI (audio thread) into markers (message thread)
    // Break Sequencer Key Entry: the slice keys played since the last call (oldest first; ones older than
    // half a second are dropped). Message thread.
    struct KeyEntry { int brk = 0, slice = 0; float vel = 0.8f; bool sustain = false; };
    std::vector<KeyEntry> takeKeyEntries();
    // put a played slice on a step of a pattern (layer: as the step's Layer hit); one undo step
    void applyKeyEntry (int pattern, int step, const KeyEntry& k, bool layer, bool layerSeq = false);
    // the Break Sequencer's tools on a pattern of the main or the layer sequencer, from that pattern's break (pinned to it when
    // it has its own; the Active Break, unpinned, when it's set to Active). tool: one of the BreakTool values below, or a
    // breaktools tool (0..kNumTools-1). Returns the break it used (0-3)
    enum BreakTool { BtOriginal = -1, BtReverse = -2, BtShuffle = -3, BtJungle = -4, BtFranken = -5, BtSwap = -6, BtClear = -7 };
    int runBreakTool (int tool, bool layerSeq, int pattern);
    tg::breaktools::RollSettings rollSettings() const;   // (v0.1.71) the Snare Roll's settings, from its parameters   // layer: as the step's Layer hit; layerSeq: into the layer sequencer
    double snapChopFrame (int brk, double frame);
    double snapMarkerFrame (int brk, double frame);  // dragging a marker: Marker Snap
    static constexpr double kMarkerGap = 64.0;       // samples a marker keeps from its neighbours
    double moveMarker (int brk, int idx, double frame, bool snap);   // live while dragging (returns where it landed, or -1)
    void setSliceTrim (int brk, int slice, bool end, float value);  // live while dragging a trim handle
    void commitMarkerEdit();                         // the end of a drag: one undo step
    bool nudgeMarker (int brk, int idx, double ms);  // the arrow keys
    double snapFrameTo (int brk, double frame, bool toTransient, bool toZero);    // to the nearest transient (+/-20 ms) and zero crossing, as switched on
    void clearMarkers (int brk);
    void snapAllMarkers (int brk);
    void markersFromSlices (int brk);                // Manual mode, starting from the slices it has now   // the three Break levels set so the loaded breaks are about equally loud (message thread)
    double getHostTempoShown() const { const double h = hostTempo.load(); return h > 0.0 ? h : (double) raw[tg::P_seqTempo]->load(); }   // slice the loaded break again when its sample or slicing changed (message thread)
    std::atomic<int> breakAudition { -1 };    // the editor asks for a slice to be played
    std::atomic<int> bsStepShown { -1 }, bsPatternShown { 0 };   // the Break Sequencer's playing step and pattern
    std::atomic<int> lsStepShown { -1 }, lsPatternShown { 0 };   // ... and the layer sequencer's
    // the Fill pad: a note or CC learned from the controller (saved with the project, like the MIDI map)
    std::atomic<int> fillPadKind { 0 }, fillPadNum { -1 };       // kind: 0 none, 1 note, 2 CC
    std::atomic<bool> fillPadLearn { false };
    bool fillPadCcDown = false;
    juce::String fillPadText() const;                            // e.g. "Note 36 (C1)", "CC 20", "none"
    // (v0.1.70) the Dub Delay's Throw pad, learned like the Fill pad (saved with the project)
    std::atomic<int> throwPadKind { 0 }, throwPadNum { -1 };
    std::atomic<bool> throwPadLearn { false };
    juce::String throwPadText() const;
    // (v0.1.71) the pattern pads: a learned note or CC per A-H button, 0-7 the main sequencer's, 8-15 the layer's; a press
    // selects that pattern as a click on its button does (saved with the project)
    static constexpr int kPatPads = 16;
    std::atomic<int> patPadKind[kPatPads] {}, patPadNum[kPatPads] {};   // kind: 0 none, 1 note, 2 CC
    std::atomic<int> patPadLearn { -1 };                                   // the button waiting for a pad (-1: none)
    std::atomic<bool> patPadLearnRun { false };                            // ... and then the next, up to H
    bool patPadCcDown[kPatPads] {};
    juce::String patPadText (int i) const;                                 // e.g. "N36", "CC20" ("" when none)
    void clearPatPads (int from, int to) { for (int i = from; i <= to; ++i) { patPadKind[i] = 0; patPadNum[i] = -1; } }
    // (v0.1.72) the Keys pad: a learned note or CC that flips the Keys switch (Slice Keys) between Synth and Breaks (isn't played; saved)
    std::atomic<int> keysPadKind { 0 }, keysPadNum { -1 };
    std::atomic<bool> keysPadLearn { false };
    bool keysPadCcDown = false;
    juce::String keysPadText() const;
    void updateIrImpulse (bool force);   // the IR Reverb's impulse, rebuilt when its settings change
    juce::String lastIrKey;
    juce::CriticalSection irLock;
    // (v0.1.72) the IR Reverb's impulse is built on its own thread, so it loads with the editor closed and in hosts that
    // don't run the message thread's timers while rendering
    struct IrWorker : juce::Thread
    {
        explicit IrWorker (MegaSynthProcessor& p) : juce::Thread ("Mega Synth IR"), owner (p) {}
        void run() override { while (! threadShouldExit()) { owner.updateIrImpulse (false); wait (100); } }
        MegaSynthProcessor& owner;
    };
    std::unique_ptr<IrWorker> irWorker;
    // the break a pattern plays (and its tools use): its own, or the Active Break when it's set to Active
    int patternBreak (const tg::BreakData& d, int pattern, bool layer) const;
    // the editor's Key Entry / sequencer page: the slice keys play this break (-1: the Active Break)
    std::atomic<int> keyBreakOverride { -1 };
    // Key Entry into the layer sequencer (set by the page)
    std::atomic<bool> keyEntryLayer { false };
    // the fill, for the step grid: steps until it starts (-1: none waiting), steps played (-1: none
    // playing), its length in steps (0: held), and the pattern step it starts on (-1: none)
    std::atomic<int> bsFillCountdown { -1 }, bsFillPosShown { -1 }, bsFillLenShown { 0 }, bsFillFirstIdx { -1 };
    void refreshAutoFills (bool force = false);   // each pattern's Auto fill, as before v0.1.69 (message thread; for converting older fills)
    juce::String convertOldFills (bool always);   // an older project's fill into the Fill Pattern (see the .cpp); returns what changed
    juce::String lastFillConversion;              // ... for the status line
    bool importLegacyFills = false;
    struct BsLogEntry { double beat; int64_t step; int pattern, slice; bool fired; int brk = -1; bool fill = false; bool layer = false; };
    std::vector<BsLogEntry>* bsLog = nullptr;   // tests: every Break Sequencer step with the beat it played on
    std::atomic<int> arpStepShown { -1 };     // for the editor
    struct ArpLogEntry { double beat; int note; bool on; float vel; };
    std::vector<ArpLogEntry>* arpLog = nullptr;   // tests: every arpeggiator event with its beat
    // Output tap for the spectrum display: the audio thread only copies samples in; the editor reads
    static constexpr int kScopeLen = 8192;
    float scope[kScopeLen] {};
    std::atomic<int> scopeWrite { 0 };
    void readScope (float* dest, int n) const;   // the newest n samples (message thread)
    int getDnaSeqStep() const { return engine.dnaSeqStep.load(); }
    int getDnaSeqStep2() const { return engine.dnaSeqStep2.load(); }
    int getDnaPatternPlaying() const { return engine.dnaPatternPlaying.load(); }                    // modulation matrix routes (amounts are the mod1Amt.. parameters)
    const tg::Engine& getEngine() const { return engine; }
    // Adds a route from src to dest in the first free slot with the given depth; returns the slot or -1.
    int addRoute (int src, int destParam, float depth);
    void clearRoute (int slot);

    // ---- macros and scenes (message thread)
    tg::SceneStore scenes;
    void storeScene (int k);                 // the panel's current sound becomes scene k
    void recallScene (int k);                // load scene k onto the panel
    void editScene (int k);                  // with morph on: the panel now shows and edits scene k
    int getEditScene() const { return sceneEditIndex.load(); }
    void clearScenes();
    void syncSceneEdits();                   // called by the timer: panel edits go into the edited scene
    // ---- Capture / Resample (message thread, except the recording itself)
    enum CaptureTarget { CT_WT1, CT_WT2, CT_DnaA, CT_DnaB, CT_Granular, CT_CycleWT1, CT_CycleWT2 };
    static constexpr double kCaptureSeconds = 8.0;
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return capRecording.load() || capStopReq.load(); }
    double captureSecondsRecorded() const { return capPos.load() / std::max (1.0, sampleRate); }
    bool hasCapture() const { return capRaw.getNumSamples() > 0; }
    void pollCapture();                                  // timer: pick up a finished recording
    tg::CaptureSettings captureSettings;
    juce::AudioBuffer<float> getEditedCapture() const;   // with trim / reverse / fades / normalise
    double detectCapturePitch() const;                   // Hz, 0 if unknown
    juce::String sendCapture (CaptureTarget);            // error message, or empty
    bool saveCaptureWav (const juce::File&) const;
    const juce::AudioBuffer<float>& getRawCapture() const { return capRaw; }
    double getCaptureRate() const { return capRate; }
    void setRawCapture (const juce::AudioBuffer<float>& b, double rate) { capRaw = b; capRate = rate; }
    juce::var captureToVar() const;
    void captureFromVar (const juce::var&);
    // ---- Master Mutate (message thread)
    void mutateNewSeed();                    // a fresh random seed
    void mutateStepSeed (int delta);         // previous / next seed
    void commitMutation();                   // bake the current mutation into the knobs; Mutate goes back to 0
    juce::StringArray getMutationHistory() const { const juce::ScopedLock sl (nameLock); return mutHistory; }
    // ---- Genetic Lab (message thread)
    tg::GeneticLab lab;

    // ---- the random patch generator (Lab > Random Patch, and Random in the header)
    tg::randompatch::Options randomOptions;                // the page's settings (saved with the project, not in patches)
    // the sample folders it draws from: 0 = breaks, 1 = samples (wavetables and the Granular Synth). An app setting,
    // kept in the patch folder (random_folders.json); each folder is scanned in the background and its list cached.
    static juce::File getRandomFolder (int which);
    static void setRandomFolder (int which, const juce::File&);
    struct FolderList { juce::File folder; juce::Array<juce::File> files; bool scanning = false, scanned = false, capped = false; };
    static FolderList randomFolderList (int which);
    static void rescanRandomFolder (int which, bool wait = false);
    static constexpr int kRandomScanCap = 10000;
    // make a patch: synchronously (tests), or in the background (the page and the header); the report says what
    // was made (or starts "Error:")
    juce::String generateRandomPatchNow (const tg::randompatch::Options&);
    void startRandomPatch (const tg::randompatch::Options&);
    bool randomBusy() const { return rpBusy.load(); }
    juce::String randomReport() const { const juce::ScopedLock sl (rpLock); return rpReport; }
    std::atomic<int> randomReportVersion { 0 };
    std::atomic<bool> sliceKeysActive { false };   // the slice keys took notes in the last block
    // for the pages: do the slice keys take notes now (Slice Keys On; Off never; Auto unless the Break Sequencer is playing, or with Key Entry)
    // Break React's starting points (0 Off, 1 Lock bass to kicks, 2 Snare stabs, 3 Rolling Reese, 4 Duck on kick): set only
    // the Break React controls, as one undo step
    static constexpr int kNumReactPresets = 5;
    static constexpr int kNumSpacePresets = 6, kNumReesePresets = 6, kNumChorusPresets = 9;
    static const char* spacePresetName (int k);
    static const char* chorusPresetName (int k);
    void applyChorusPreset (int k);   // (v0.1.72) the Chorus & Phase presets: they set only the new choruses and phasers
    static const char* reesePresetName (int k);
    void applySpacePreset (int k);
    void applyReesePreset (int k);
    static const char* reactPresetName (int k);
    void applyReactPreset (int k);
    bool sliceKeysTakeNotes() const
    {
        const int sk = juce::roundToInt (params[(size_t) tg::P_bcSliceKeys]->convertFrom0to1 (params[(size_t) tg::P_bcSliceKeys]->getValue()));
        if (sk == 0) return true;
        if (sk == 1) return false;
        return bsStepShown.load() < 0 || params[(size_t) tg::P_bsKeyEntry]->getValue() > 0.5f;
    }
    // the phrase a patch of a category is measured on (and auditioned with): { beat, length in beats, note } at 172 BPM
    static std::vector<std::array<double, 3>> auditionPhrase (const juce::String& category, bool breakKeys);
    float labVariation = 0.1f;
    void labSetParent (int which);                 // 0 = A, 1 = B: the current sound becomes that parent
    void labSetParentFromId (int which, const juce::String& id);
    int labBreed();                                // returns the brood index, or -1 (parents missing)
    void labAudition (const juce::String& id);     // load a lab patch onto the panel
    tg::LabPatch currentAsLabPatch (const juce::String& name);
    juce::String getMacroName (int k) const;
    void setMacroName (int k, const juce::String&);

    // Sample slots: 0 = Osc 4 / WT 1, 1 = WT 2
    bool loadSampleFile (const juce::File&, int slot = 0);
    bool loadSampleData (const juce::MemoryBlock&, const juce::String& name, int slot = 0);
    void clearSample (int slot = 0);
    juce::String getSampleStatus (int slot = 0) const;
    // sample slots: 0 WT 1, 1 WT 2, 2 the Granular Synth, 3 the Break Chopper
    // sample slots: 0 WT 1, 1 WT 2, 2 the Granular Synth, 3-5 the Break Chopper's Breaks 1-3
    static constexpr int kSampleSlots = 8, kSlotGranular = 2, kSlotBreak = 3, kSlotIr = 7;   // (7: the IR Reverb's own impulse, v0.1.70)
    static bool isBreakSlot (int k) { return k >= kSlotBreak && k < kSlotBreak + tg::BreakData::kBreaks; }
    static int breakSlot (int k) { return kSlotBreak + juce::jlimit (0, tg::BreakData::kBreaks - 1, k); }
    static juce::String sampleSuffix (int k) { static const char* s[] = { "", "2", "Gran", "Break", "Break2", "Break3", "Break4", "IR" }; return s[juce::jlimit (0, 7, k)]; }
    static const char* samplePatchKey (int k) { static const char* s[] = { "wavetable", "wavetable2", "granularSample", "breakSample", "breakSample2", "breakSample3", "breakSample4", "irSample" }; return s[juce::jlimit (0, 7, k)]; }
    // the longest audio a slot keeps: the wavetable slots 120 s as before; the Granular Synth 20 s and the
    // Break Chopper 30 s (8 bars at 64 BPM), so a project doesn't grow past roughly 10 MB per sample
    static double sampleCapSeconds (int k) { return k == kSlotGranular ? 20.0 : (k == kSlotIr ? 10.0 : (k >= kSlotBreak ? 30.0 : 120.0)); }
    const tg::WaveSample* getSample (int slot) const { return currentWave[juce::jlimit (0, kSampleSlots - 1, slot)].load(); }
    size_t getSampleBytes (int slot) const { const juce::ScopedLock sl (waveLock); return waveData[juce::jlimit (0, kSampleSlots - 1, slot)].getSize(); }
    juce::String getSampleName (int slot) const { const juce::ScopedLock sl (waveLock); return waveName[juce::jlimit (0, kSampleSlots - 1, slot)]; }

    juce::String exportBrowserPatch() const;
    juce::String importBrowserPatch (const juce::String& json);   // returns an error message, or empty on success
    void resetToDefaults();
    // Init: a clean starting patch (one saw, no effects), or the user's own Init when one has been saved.
    // New instances start from it; a project or patch being loaded always replaces it completely.
    void loadInitPatch();                    // the Init button and the browser's Init entry (undoable)
    void applyInitState();                   // just the settings (no undo step)
    void applyFactoryInit();                 // the built-in clean Init, on top of the parameter defaults
    bool saveCurrentAsInit();                // "Save as Init": shared by every instance and project
    bool resetInitToFactory();               // forget the saved Init
    static bool hasUserInit();
    static juce::File getUserInitFile();     // in the app's data folder (MEGASYNTH_DATA_DIR overrides, for tests)
    static inline std::atomic<bool> applyInitOnCreate { true };
    void initOnFirstUse();                   // Init for a new instance (see the .cpp)
    std::atomic<bool> stateRestored { false }, initDone { false };
    static constexpr float kInitWheelVibrato = 0.02f;   // Mod Matrix depth of the Init patch's wheel vibrato   // tests turn this off to start from the bare defaults
    bool loadFactoryPreset (int index);
    void copyFilter1To2();
    // ---- MIDI learn (saved with the project; patches don't change it)
    void midiLearn (int param);              // the next controller moved is mapped to param
    void midiForget (int param);
    int ccForParam (int param) const;        // -1 when not mapped
    int learningParam() const { return learnParam.load(); }
    uint32_t getMidiMapVersion() const { return midiMapVersion.load(); }
    juce::String midiMapToString() const;
    void midiMapFromString (const juce::String&);
    std::atomic<int> uiWidth { 0 };
    // the open tab and each tab's last sub-tab ("tab;sub,sub,..."), saved with the project
    juce::String getUiTabs() const { const juce::ScopedLock sl (nameLock); return uiTabs; }
    void setUiTabs (const juce::String& t) { const juce::ScopedLock sl (nameLock); uiTabs = t; }
    std::atomic<float> scMeter { 0.0f };     // sidechain input level (peak, for the editor)
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &vst3Ext; }
    std::atomic<int> curProgram { 0 }, pendingProgram { -1 };          // editor width the project was saved with (0 = default)
    // DNA Sequencer editing (message thread, undoable). lane 0/1; acts on the selected pattern.
    enum DnaEdit { DE_Generate, DE_SeedDown, DE_SeedUp, DE_Copy, DE_Paste, DE_ShiftLeft, DE_ShiftRight, DE_Reverse, DE_Clear };
    void dnaEdit (DnaEdit, int lane);
    int dnaEditPattern() const { return juce::jlimit (0, 3, (int) std::lround (raw[tg::P_dsPattern]->load())); }
    int dnaLaneLength (int lane) const { return juce::jlimit (1, 32, (int) std::lround (raw[lane == 0 ? tg::P_dsSteps : tg::P_dsLane2Steps]->load())); }
    bool dnaHasClipboard() const { return dnaClipValid; }                // Filter 2 takes all of Filter 1's settings (undoable)   // tg::factoryPresets() index; false when out of range

    // Patch files (.megasynth = the browser patch JSON plus a name and the plugin-only settings)
    static juce::File getPatchFolder();
    static juce::Array<juce::File> getPatchFiles();
    bool savePatchToFile (const juce::File&);
    juce::String loadPatchFromFile (const juce::File&);   // error message, or empty on success
    juce::String getPatchName() const { const juce::ScopedLock sl (nameLock); return patchName; }
    // the patch's category, tags, author and note (not sound: saved in patch files and projects)
    tg::PatchMeta getPatchMeta() const { const juce::ScopedLock sl (nameLock); return patchMeta; }
    void setPatchMeta (const tg::PatchMeta& m) { const juce::ScopedLock sl (nameLock); patchMeta = m; }
    static tg::PatchMeta factoryMetaFor (int index);                  // a factory preset's, with your edits to it
    static juce::File getFactoryMetaFile();                           // those edits (factory_meta.json in the patch folder)
    static juce::File getDeletedFolder();                             // deleted patches wait here to be restored
    static juce::String getDefaultAuthor();                           // the author last typed when saving (remembered)
    static void setDefaultAuthor (const juce::String&);
    // every author used before, most recent first (authors.json in the patch folder): the names typed when saving,
    // and the authors of the patches found there, less any you've told it to forget
    static juce::StringArray getAuthorList (const juce::StringArray& foundInPatches = {});
    static void rememberAuthor (const juce::String&);       // to the front of the list (and no longer forgotten)
    static void forgetAuthor (const juce::String&);         // off the list (no patch changes)
    static juce::File getAuthorListFile();
    void setPatchName (const juce::String& n) { const juce::ScopedLock sl (nameLock); patchName = n; }
    void generateRandomPhrase();

    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }

    // ---- history: undo / redo / return to original (message thread)
    static constexpr int kStateVersion = 32;   // 3: filter overhaul; 4: Filter 2; 5: DNA Sequencer update; 6: effects rack; 7: LFO sync / shapes; 8: arp / chord memory; 9: sidechain; 10: unison; 11: pitch env / MSEG; 12: Master Filter / Output / Mixer strips; 13: filter character models; 14: Performance FX; 15: Granular Synth; 16: Break Chopper / Sequencer; 17: three breaks; 18: chop mode; 19: pattern tools / per-hit stretch; 20: fills; 21: Master Filter models; 22: sub / break routing; 23: Akai Stretch pad; 24: the Reese engine; 25: Break-Reactive mode; 26: the Core oscillator slots; 27: Core envelope mode, Break Sequencer key entry; 28: Slice Keys Auto, Break React rows, 29: Break 4, layer sequencer, fill pattern; 30: the Space effects, the Reese additions; 31: snare roll, pattern pads, layer pairs; 32: choruses and phasers, Glide Mode, Break FX, Keys pad
    bool canUndo() const { return historyPos > 0; }
    bool canRedo() const { return historyPos + 1 < (int) history.size(); }
    void undo();
    void redo();
    void returnToOriginal();
    void pushHistory (const juce::String& label);   // record the current state now
    // the Core slots: write one of the starting routings (CoreAlgorithms.h) into a slot's matrix; reset a slot
    // to its defaults (the engine choice kept). Both are ordinary parameter changes, one undo step each.
    void applyCoreAlgorithm (int slot, int algorithm);
    void resetCoreSlot (int slot);
    void markOriginal();                             // the current state becomes "the original"
    // ---- A/B compare: two versions of the patch to switch between (both start as the loaded patch)
    int abSlot() const { return abSel; }
    void abSelect (int slot);                         // 0 = A, 1 = B (undoable)
    void abCopyToOther();                             // the other slot becomes a copy of this one
    juce::String lastHistoryLabel() const { return historyPos >= 0 ? history[(size_t) historyPos].label : juce::String(); }

    void timerCallback() override;   // message thread housekeeping (20 Hz); public so tests can run it
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (MegaSynthProcessor&);
    void fillSnapshot (int numSamples);
public:
    void updateLatency();   // message thread (timer) and prepareToPlay
private:
    void resetPatchState();
    void setFilterDefaultsForOldPatch();   // patches / projects from before the filter overhaul
    void setFilter2DefaultsForOldPatch();  // ... and from before Filter 2
    void setDnaDefaultsForOldPatch();      // ... and from before the DNA Sequencer update
    void setFxDefaultsForOldPatch();
    void setLfoDefaultsForOldPatch();      // ... and from before LFO sync / custom shapes
    void setArpDefaultsForOldPatch();      // ... and from before chord memory / the arpeggiator
    void setSidechainDefaultsForOldPatch(); // ... and from before the sidechain input
    void setUnisonDefaultsForOldPatch();   // ... and from before per-oscillator unison
    void setMasterDefaultsForOldPatch();   // ... and from before the Master Filter and Output trim
    void setMidiDefaultsForOldPatch();
    void setMixerDefaultsForOldPatch();
    void setCharDefaultsForOldPatch();
    void setPerfDefaultsForOldPatch();      // ... and from before Performance FX
    void setGranDefaultsForOldPatch();      // ... and from before the Granular Synth
    void setBreakDefaultsForOldPatch();     // ... and from before the Break Chopper
    void setBreak3DefaultsForOldPatch();    // ... and from before three breaks
    // the random patch generator's working parts (see generateRandomPatchNow)
    std::unique_ptr<MegaSynthProcessor> calibrator;   // a second synth, off the audio path, to measure a new patch's level
    std::unique_ptr<juce::Thread> rpThread;
    std::atomic<bool> rpBusy { false };
    std::shared_ptr<std::atomic<bool>> rpAlive = std::make_shared<std::atomic<bool>> (true);
    juce::CriticalSection rpLock;
    juce::String rpReport;
    bool rpPrepare (const tg::randompatch::Options&, int attempt, tg::randompatch::Result&, juce::String& error);   // message thread
    double rpMeasure (int category, double& seconds);                                                            // the calibrator only (any thread)
    juce::String rpFinish (const tg::randompatch::Options&, const tg::randompatch::Result&);                     // message thread
    void setRandomReport (const juce::String& r) { { const juce::ScopedLock sl (rpLock); rpReport = r; } randomReportVersion.fetch_add (1); }
    void setV27DefaultsForOldPatch();
    void setV28DefaultsForOldPatch();
    void setV30DefaultsForOldPatch();
    void setV32DefaultsForOldPatch();       // (v0.1.72) choruses / phasers, Glide Mode
    void setV31DefaultsForOldPatch();       // ... and from before the Snare Roll's settings       // ... and from before the new Space effects / Reese additions
    void setV29DefaultsForOldPatch();       // ... and from before Break 4 / the layer sequencer / the fill pattern       // ... and from before Slice Keys / the Break React rows       // ... and from before the Core envelope mode / key entry
    void setCoreDefaultsForOldPatch();      // ... and from before the Core oscillator slots
    void setReeseDefaultsForOldPatch();     // ... and from before the Reese engine
    void setReactDefaultsForOldPatch();     // ... and from before Break-Reactive mode
    void setAkaiDefaultsForOldPatch();      // ... and from before the Akai Stretch pad
    void setRouteDefaultsForOldPatch();     // ... and from before the sub / break routes
    void setMfModelDefaultsForOldPatch();   // ... and from before the Master Filter had the models
    void setFillDefaultsForOldPatch();      // ... and from before fills
    void setToolDefaultsForOldPatch();      // ... and from before the stage-4 pattern tools
    void setChopDefaultsForOldPatch();      // ... and from before chop mode      // ... and from before the filter character models     // ... and from before the Mixer strips (pan / mute / solo)     // ... and from before the wheel / sustain switches
    void setEnvDefaultsForOldPatch();      // ... and from before the pitch envelope and MSEGs       // ... and from before the effects rack   // every parameter, route, scene, step and sample back to the defaults
    void handleMidi (const juce::MidiMessage&);
    void seqTick (int stepIndex, double stepSeconds);
    void seqStopHeld();
    void setParamFromAudio (int index, float plainValue);

    struct Snapshot
    {
        juce::ValueTree params;
        tg::PatchMeta meta;
        juce::String steps, patchName, label, routes, scenes, macroNames, dnaSteps, fxRack, lfoShapes, arpStore, msegs, breakStore;
        int sceneEdit = 0;
        std::shared_ptr<const juce::MemoryBlock> wave[kSampleSlots];
        juce::String waveName[kSampleSlots];
    };
    Snapshot captureSnapshot (const juce::String& label);
    void restoreSnapshot (const Snapshot&);
    bool sameState (const Snapshot&, const Snapshot&) const;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { snapshotPending = true; }
    std::vector<Snapshot> history;
    int historyPos = -1;
    Snapshot original;
    Snapshot abOther;   // the slot not being played
    int abSel = 0;
    bool restoring = false;
    std::atomic<bool> snapshotPending { false };
    int snapshotDelay = 0;
    uint32_t lastStepsVersion = 0, lastRoutesVersion = 0, lastDnaVersion = 0;
    tg::DnaClock dnaClock;          // DNA Sequencer clock (audio thread)
    tg::DnaLane dnaClip {};         // DNA Sequencer copy / paste
    bool dnaClipValid = false;
    int midiPos = 0;                // sample position of the MIDI event being handled
    // ---- chord memory and arpeggiator (audio thread)
    void routeNoteOn (int note, float vel, int ch);
    void routeNoteOff (int note);
    void startVoice (int key, int note, float vel, int ch, float accent);
    void arpTick (double beatNow);
    tg::Arpeggiator arp;
    std::vector<tg::ArpEvent> arpEvents;
    std::array<std::array<int, 8>, 128> chordTones {};
    std::array<int, 128> chordCount {}, noteRoute {};   // noteRoute: 0 voice (key = note), 1 arpeggiator, 2 chord
    std::vector<int> learnNotes; int learnHeld = 0;
    bool arpWasOn = false;
    uint32_t arpRng = 0x2545F491u;
    double fxBeatAtBlock = 0.0;
    juce::AudioBuffer<float> scBuf;
    float scEnvelope = 0.0f;
    // VST3: the input bus is a sidechain (aux), not a main input, so hosts treat this as an instrument
    struct Vst3Ext : juce::VST3ClientExtensions { bool getPluginHasMainInput() const override { return false; } } vst3Ext;
    std::array<std::atomic<int>, 128> ccMap;   // MIDI learn: controller -> parameter (-1 = none)
    std::atomic<int> learnParam { -1 };
    std::atomic<uint32_t> midiMapVersion { 0 };
    double fxBeatStart = 0.0, fxBeatInc = 0.0, fxBeatsInternal = 0.0;   // the effects' beat clock for this block
    uint32_t lastRackVersion = 0, lastLfoShapeVersion = 0, lastArpVersion = 0, lastMsegVersion = 0, lastBreakVersion = 0;
    juce::String lastBreakKey[tg::BreakData::kBreaks];
    juce::AbstractFifo chopFifo { 64 };
    juce::AbstractFifo keyEntryFifo { 64 };
    struct KeyEntryRaw { int brk = 0, slice = 0; float vel = 0.8f; bool sustain = false; uint32_t ms = 0; } keyEntryBuf[64];
    double chopFrames[64] {}; int chopBrks[64] {};
    std::vector<std::pair<int, float>> chopHistory;   // the markers chop mode added, newest last
    const tg::WaveSample* onsetsFor[tg::BreakData::kBreaks] {};
    std::vector<int> onsets[tg::BreakData::kBreaks];                       // every transient (high sensitivity), for snapping
    bool lastChopMode = false;
    int breakSelPending = -1; double breakSelBeat = 0.0;   // a Break Select key waiting for the beat
    void bsApplyPatternBreak (const tg::BreakData&);
    // the Break Sequencer (audio thread)
    int64_t bsLastStep = INT64_MIN;
    int bsPlaying = 0, bsChainIdx = -1;
    bool bsRunning = false;
    uint8_t sliceKeyOwner[128] {};   // Slice Keys Off / Auto: 1 = this key's note-on went to the Break Chopper
    uint32_t bsRng = 0x9E3779B9u;
    struct BsRoll { double beat; tg::BreakChopper::Trigger t; };
    static constexpr int kMaxRolls = 64;
    BsRoll bsRolls[kMaxRolls]; int bsNumRolls = 0;
    // fills (audio thread): 0 none, 1 waiting for its start, 2 playing; steps [start, end)
    int bsFillState = 0, bsFillPat = 0, bsFillLen = 16;   // bsFillPat: the fill pattern playing (or waiting)
    int64_t bsFillStart = 0, bsFillEnd = 0, bsOffset = 0;   // bsOffset: where the pattern restarted (Restart the pattern)
    bool bsFillHeld = false, bsFillParamLast = false, bsSkipChain = false, bsJustStarted = false;
    uint32_t bsFillRng = 0x2545F491u;
    void bsFillPress (bool down);
    void bsShowFill (int64_t k, int steps);
    juce::String lastFillKey;
    double bsDue() const;          // the beat of the next step or roll
    void bsTick (double beat);     // plays what's due at this beat
    void bsStart (double beat);
    int fillLenSteps (int steps) const;   // the fill's length: the whole fill pattern, or its last 1 beat, 2 beats or 1 bar
    // the layer sequencer (audio thread): its own patterns, steps, chain and rolls; Follow main takes the main's pattern
    struct LayerRun { bool running = false; int64_t lastStep = INT64_MIN; int playing = 0, chainIdx = -1; uint32_t rng = 0x85EBCA6Bu;
                      BsRoll rolls[kMaxRolls]; int numRolls = 0; int64_t offset = 0; bool skipChain = false; };
    LayerRun lay;
    void lsStart (double beat);
    double lsDue() const;
    void lsTick (double beat);
    void bsPlayStep (const tg::BreakStep& s, int64_t k, double stepBeat, int pattern, bool inFill, bool layer, BsRoll* rolls, int& numRolls, uint32_t& rng, int& firedBrk, bool& fired);
    double hostStartBeat = 0.0; bool hostWasPlayingLfo = false;
    bool dnaGateRouted() const;     // a Mod Matrix route uses the DNA step gate
    tg::GlobalModInputs modInputs;
    juce::AudioBuffer<float> capBuf;            // audio thread writes while recording
    std::atomic<bool> capRecording { false }, capStopReq { false }, capReady { false };
    std::atomic<int> capPos { 0 };
    juce::AudioBuffer<float> capRaw;            // the last recording (message thread)
    double capRate = 48000.0;
    juce::AudioBuffer<float> grainLoad;         // capture -> granular hand-over
    std::atomic<bool> grainLoadReady { false };
    int grainLoadLen = 0;
    std::atomic<int> sceneEditIndex { 0 };
    bool lastMorph = false;
    uint32_t lastScenesVersion = 0;
    juce::String macroNames[8];
    juce::StringArray mutHistory;            // committed mutations, oldest first
    juce::String macroNamesJoined() const;
    void setMacroNamesJoined (const juce::String&);
    juce::CriticalSection historyLock;

    std::array<juce::RangedAudioParameter*, tg::P_COUNT> params {};
    std::array<std::atomic<float>*, tg::P_COUNT> raw {};

    tg::Engine engine;
    tg::Snapshot snap;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    float bendSemis = 0.0f;
    float bendNorm = 0.0f;                  // the wheel, -1..1 (the range is applied every block, so changing it bends held notes too)
    bool sustainDown = false;
    std::array<bool, 128> sustained {};     // keys let go while the sustain pedal is down
    void sustainRelease();
public:
    // MIDI input monitor (written on the audio thread, shown by the editor)
    std::atomic<float> monBend { 0.0f }, monWheel { 0.0f }, monPressure { 0.0f };
    std::atomic<int> monCC { -1 }, monCCVal { 0 }, monCh { 0 };
    std::atomic<bool> monSustain { false };
    std::atomic<uint32_t> monBendCount { 0 }, monCCCount { 0 }, monNoteCount { 0 };
private:
    std::atomic<double> hostTempo { 0.0 };

    // sequencer runtime (audio thread)
    bool seqRunning = false;
    int seqStep = 0;
    double seqSamplesToNext = 0.0;
    int seqHeldKey = -1;
    double seqReleaseIn = -1.0;
    int seqReleaseKey = -1;
    bool hostWasPlaying = false;

    // sample for Osc 4
    std::atomic<tg::WaveSample*> currentWave[kSampleSlots] { { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr }, { nullptr } };
    std::vector<std::unique_ptr<tg::WaveSample>> waveKeep;
    juce::MemoryBlock waveData[kSampleSlots];
    std::shared_ptr<const juce::MemoryBlock> waveShared[kSampleSlots];   // same bytes, shared with history snapshots
    juce::String waveName[kSampleSlots];
    mutable juce::CriticalSection waveLock;
    juce::AudioFormatManager formats;

    // Euclidean live update tracking (message thread)
    int lastEuclid[4] { -1, -1, -1, -1 };
    juce::Random rng;
    juce::String patchName { "Init" };
    tg::PatchMeta patchMeta;
    juce::String uiTabs;
    mutable juce::CriticalSection nameLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaSynthProcessor)
};

// Marks a toggle or choice box with the parameter it controls, so a right-click can offer MIDI Learn
template <class C> C& tagParam (C& c, int paramIndex) { c.getProperties().set ("megaParam", paramIndex); return c; }
