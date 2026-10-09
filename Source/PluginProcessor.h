#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Engine.h"
#include "Sequencer.h"

class MegaSynthProcessor : public juce::AudioProcessor,
                           private juce::Timer
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

    bool loadSampleFile (const juce::File&);
    bool loadSampleData (const juce::MemoryBlock&, const juce::String& name);
    void clearSample();
    juce::String getSampleStatus() const;

    juce::String exportBrowserPatch() const;
    juce::String importBrowserPatch (const juce::String& json);   // returns an error message, or empty on success
    void resetToDefaults();
    void generateRandomPhrase();

    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (MegaSynthProcessor&);
    void timerCallback() override;
    void fillSnapshot (int numSamples);
    void handleMidi (const juce::MidiMessage&);
    void seqTick (int stepIndex, double stepSeconds);
    void seqStopHeld();
    void setParamFromAudio (int index, float plainValue);

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
    std::atomic<tg::WaveSample*> currentWave { nullptr };
    std::vector<std::unique_ptr<tg::WaveSample>> waveKeep;
    juce::MemoryBlock waveData;
    juce::String waveName;
    mutable juce::CriticalSection waveLock;
    juce::AudioFormatManager formats;

    // Euclidean live update tracking (message thread)
    int lastEuclid[4] { -1, -1, -1, -1 };
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaSynthProcessor)
};
