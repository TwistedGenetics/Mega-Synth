#pragma once
// Filter section of the Filter & Env tab: type and slope buttons, the classic model, the main
// knobs (Cutoff, Resonance, Drive, Mix), the modulation knobs (Env Amt, LFO Amt + source, Key
// Track) and a response curve drawn from the real filter code.
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

class Knob;

// A row of buttons attached to a choice parameter. Disabled segments (mask) can't be chosen.
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (MegaSynthProcessor&, int paramIndex, juce::StringArray labels, juce::Colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void setMask (int m) { if (m != mask) { mask = m; repaint(); } }
    void setShown (int s) { if (s != shown) { shown = s; repaint(); } }   // the segment actually in use
    std::function<juce::String (int)> tooltipFor;
private:
    int segmentAt (juce::Point<int>) const;
    MegaSynthProcessor& proc;
    const int param;
    juce::StringArray labels;
    juce::Colour colour;
    int mask = -1, shown = -1, hover = -1;
};

class FilterResponse : public juce::Component
{
public:
    explicit FilterResponse (MegaSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();   // recompute when the settings change
private:
    MegaSynthProcessor& proc;
    juce::String lastKey;
    std::vector<float> magDb;      // per FFT bin
    float cutoff = 1000.0f;
    juce::String caption;
};

class FilterPanel : public juce::Component, private juce::Timer
{
public:
    explicit FilterPanel (MegaSynthProcessor&);
    ~FilterPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void sync();
    void setParam (int idx, float plain);
    float plain (int idx) const;
    MegaSynthProcessor& proc;
    Segmented type, slope;
    juce::ComboBox model, lfoSrc;
    juce::Label modelCap, lfoCap, typeCap, slopeCap, info;
    juce::OwnedArray<Knob> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoAtt;
    FilterResponse response;
};

} // namespace tgui
