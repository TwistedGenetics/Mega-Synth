#pragma once
// Overview tab: signal flow, the genetic network (live Mod Matrix routes), a spectrum
// analyser and the DNA strand (Mutate / lock state). Everything here runs on the message
// thread from values the audio thread already publishes; nothing is added to the audio path.
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

class SignalFlowView : public juce::Component
{
public:
    explicit SignalFlowView (MegaSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();
private:
    MegaSynthProcessor& proc;
    juce::String lastKey;
};

class NetworkView : public juce::Component
{
public:
    explicit NetworkView (MegaSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();
private:
    MegaSynthProcessor& proc;
    float phase = 0;
};

class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (MegaSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();
private:
    void analyse();
    MegaSynthProcessor& proc;
    juce::dsp::FFT fft { 12 };
    std::vector<float> data = std::vector<float> (8192, 0.0f);
    std::vector<float> db = std::vector<float> (2048, -100.0f), peak = std::vector<float> (2048, -100.0f);
};

class DnaStrandView : public juce::Component
{
public:
    explicit DnaStrandView (MegaSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();
private:
    MegaSynthProcessor& proc;
    float spin = 0;
};

} // namespace tgui
