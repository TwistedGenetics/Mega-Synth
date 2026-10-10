#pragma once
// Filter section of the Filter & Env tab. Filter 1 and Filter 2 share one panel: the switch in
// the header picks which one the type / slope buttons, model and knobs show. The header also
// has Filter 2 On, Serial / Parallel, Stereo Split, Balance and Copy 1 > 2, and the curve on
// the right is the combined response, worked out from the real filter code.
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

class Knob;

// The parameters of one filter
struct FilterParams { int mode, type, slope, cutoff, res, drive, mix, env, lfoAmt, lfoSrc, keyTrack; };
const FilterParams& filterParams (int which);   // 0 = Filter 1, 1 = Filter 2

// A row of buttons attached to a choice parameter (or, with paramIndex -1, a local selection).
// Disabled segments (mask) can't be chosen.
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
    void setColour (juce::Colour c) { colour = c; repaint(); }
    int getLocal() const { return local; }
    std::function<void (int)> onSelect;   // local mode
private:
    int segmentAt (juce::Point<int>) const;
    MegaSynthProcessor& proc;
    const int param;
    juce::StringArray labels;
    juce::Colour colour;
    int mask = -1, shown = -1, hover = -1, local = 0;
};

class FilterResponse : public juce::Component
{
public:
    explicit FilterResponse (MegaSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update (int selected);   // recompute when the settings change
private:
    MegaSynthProcessor& proc;
    juce::String lastKey;
    std::vector<float> mag[3];     // dB per FFT bin: Filter 1, Filter 2, combined
    float cutoff[2] { 1000.0f, 1000.0f };
    int routing = 0, sel = 0;      // routing: 0 off, 1 serial, 2 parallel, 3 stereo split
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
    void show (int which);
    void setParam (int idx, float plain);
    float plain (int idx) const;
    MegaSynthProcessor& proc;
    int sel = 0;
    Segmented which, routing;
    juce::OwnedArray<Segmented> type, slope;            // [filter]
    juce::OwnedArray<Knob> knobs[2];
    juce::OwnedArray<juce::ComboBox> lfoSrc;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoAtt;
    juce::ComboBox model;
    juce::ToggleButton f2On { "Filter 2 On" }, split { "Stereo Split" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> f2OnAtt, splitAtt;
    juce::TextButton copyBtn { "Copy 1 > 2" };
    std::unique_ptr<Knob> balance;
    juce::Label modelCap, lfoCap, typeCap, slopeCap, info;
    FilterResponse response;
};

} // namespace tgui
