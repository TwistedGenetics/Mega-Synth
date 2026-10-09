#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Engine.h"
#include "Sequencer.h"

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    tg::StepStore steps;
    std::atomic<int> currentStep { -1 };
    tg::RouteStore routes;                    // modulation matrix routes (amounts are the mod1Amt.. parameters)
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
    static constexpr int kStateVersion = 2;
    bool canUndo() const { return historyPos > 0; }
    bool canRedo() const { return historyPos + 1 < (int) history.size(); }
    void undo();
    void redo();
    void returnToOriginal();
    void pushHistory (const juce::String& label);   // record the current state now
    void markOriginal();                             // the current state becomes "the original"
    juce::String lastHistoryLabel() const { return historyPos >= 0 ? history[(size_t) historyPos].label : juce::String(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (MegaSynthProcessor&);
    void timerCallback() override;
    void fillSnapshot (int numSamples);
    void handleMidi (const juce::MidiMessage&);
    void seqTick (int stepIndex, double stepSeconds);
    void seqStopHeld();
    void setParamFromAudio (int index, float plainValue);

    struct Snapshot
    {
        juce::ValueTree params;
        juce::String steps, patchName, label, routes, scenes, macroNames;
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
    bool restoring = false;
    std::atomic<bool> snapshotPending { false };
    int snapshotDelay = 0;
    uint32_t lastStepsVersion = 0, lastRoutesVersion = 0;
    tg::GlobalModInputs modInputs;
    std::atomic<int> sceneEditIndex { 0 };
    bool lastMorph = false;
    uint32_t lastScenesVersion = 0;
    juce::String macroNames[8];
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
