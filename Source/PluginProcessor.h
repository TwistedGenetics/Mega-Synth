#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Engine.h"
#include "Sequencer.h"
#include "Mut/Capture.h"
#include "Lab/Breeder.h"
#include "Presets/Factory.h"
#include "Seq/DnaGenerator.h"

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

    juce::String exportBrowserPatch() const;
    juce::String importBrowserPatch (const juce::String& json);   // returns an error message, or empty on success
    void resetToDefaults();
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
    void setPatchName (const juce::String& n) { const juce::ScopedLock sl (nameLock); patchName = n; }
    void generateRandomPhrase();

    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }

    // ---- history: undo / redo / return to original (message thread)
    static constexpr int kStateVersion = 6;   // 3: filter overhaul; 4: Filter 2; 5: DNA Sequencer update; 6: effects rack
    bool canUndo() const { return historyPos > 0; }
    bool canRedo() const { return historyPos + 1 < (int) history.size(); }
    void undo();
    void redo();
    void returnToOriginal();
    void pushHistory (const juce::String& label);   // record the current state now
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
    void setFxDefaultsForOldPatch();       // ... and from before the effects rack   // every parameter, route, scene, step and sample back to the defaults
    void handleMidi (const juce::MidiMessage&);
    void seqTick (int stepIndex, double stepSeconds);
    void seqStopHeld();
    void setParamFromAudio (int index, float plainValue);

    struct Snapshot
    {
        juce::ValueTree params;
        juce::String steps, patchName, label, routes, scenes, macroNames, dnaSteps, fxRack;
        int sceneEdit = 0;
        std::shared_ptr<const juce::MemoryBlock> wave[2];
        juce::String waveName[2];
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
    std::array<std::atomic<int>, 128> ccMap;   // MIDI learn: controller -> parameter (-1 = none)
    std::atomic<int> learnParam { -1 };
    std::atomic<uint32_t> midiMapVersion { 0 };
    double fxBeatStart = 0.0, fxBeatInc = 0.0, fxBeatsInternal = 0.0;   // the effects' beat clock for this block
    uint32_t lastRackVersion = 0;
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
    std::atomic<tg::WaveSample*> currentWave[2] { { nullptr }, { nullptr } };
    std::vector<std::unique_ptr<tg::WaveSample>> waveKeep;
    juce::MemoryBlock waveData[2];
    std::shared_ptr<const juce::MemoryBlock> waveShared[2];   // same bytes, shared with history snapshots
    juce::String waveName[2];
    mutable juce::CriticalSection waveLock;
    juce::AudioFormatManager formats;

    // Euclidean live update tracking (message thread)
    int lastEuclid[4] { -1, -1, -1, -1 };
    juce::Random rng;
    juce::String patchName { "Init" };
    mutable juce::CriticalSection nameLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaSynthProcessor)
};
