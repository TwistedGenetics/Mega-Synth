#pragma once
// Effects tab extras: the Volume Shaper curve editor, multiband meters, the Beat Repeat trigger
// pad and the rack list (drag to reorder, On and Mix per slot).
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

class ShaperCurve : public juce::Component, private juce::Timer
{
public:
    explicit ShaperCurve (MegaSynthProcessor& p) : proc (p) { startTimerHz (30); }
    ~ShaperCurve() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    void drawAt (juce::Point<float>);
    MegaSynthProcessor& proc;
    int lastIdx = -1; float lastVal = 0;
};

class MultibandMeters : public juce::Component, private juce::Timer
{
public:
    explicit MultibandMeters (MegaSynthProcessor& p) : proc (p) { startTimerHz (30); }
    ~MultibandMeters() override { stopTimer(); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    MegaSynthProcessor& proc;
    float shown[3] { 0, 0, 0 };
};

// Momentary trigger for Beat Repeat (holds the Repeat Trigger parameter while pressed) with an activity light
class StutterPad : public juce::Component, private juce::Timer
{
public:
    explicit StutterPad (MegaSynthProcessor& p) : proc (p) { startTimerHz (30); setTooltip ("Hold to repeat"); }
    ~StutterPad() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { set (true); }
    void mouseUp (const juce::MouseEvent&) override { set (false); }
    void setTooltip (const juce::String& t) { tip = t; }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    void set (bool on);
    MegaSynthProcessor& proc;
    bool down = false;
    juce::String tip;
};

class RackList : public juce::Component, private juce::Timer
{
public:
    explicit RackList (MegaSynthProcessor&);
    ~RackList() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    void layoutRows();
    int rowAt (int y) const;
    MegaSynthProcessor& proc;
    juce::OwnedArray<juce::ToggleButton> on;
    juce::OwnedArray<juce::Slider> mix;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> mixAtt;
    std::array<int, tg::FS_COUNT> order {};
    uint64_t lastKey = ~0ull;
    int dragFrom = -1, dragY = 0, dragOver = -1;
    static constexpr int kRowH = 40, kTop = 30;
};

} // namespace tgui
