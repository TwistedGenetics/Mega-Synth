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

// The sidechain input's level, with the threshold marked
class SidechainMeter : public juce::Component, private juce::Timer
{
public:
    explicit SidechainMeter (MegaSynthProcessor& p) : proc (p) { startTimerHz (30); }
    ~SidechainMeter() override { stopTimer(); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    MegaSynthProcessor& proc;
    float shown = 0.0f;
};

// (v0.1.72) the colour of each effect group (the FX tab's sub-tabs, the Rack's tags and the Mixer's Effect Returns)
inline juce::Colour fxGroupColour (int group)
{
    static const juce::uint32 c[tg::FG_COUNT] = { 0xffff9f43, 0xff59a8ff, 0xff4fd1c5, 0xffb98cff, 0xffffd166, 0xffff6b6b, 0xff7bd88f };
    return juce::Colour (c[juce::jlimit (0, tg::FG_COUNT - 1, group)]);
}

// (v0.1.72) an effect's state light, beside its On: off (and how to hear it), on but at Mix 0, on and waiting for sound,
// or how much it's adding now; the Gated Reverb's also shows its gate opening. Click it to switch the effect on or off.
class FxLight : public juce::Component, private juce::Timer
{
public:
    FxLight (MegaSynthProcessor& p, int slot) : proc (p), slot (slot) { startTimerHz (15); setTooltip(); }
    ~FxLight() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String stateText() const;   // (what it shows, for the tests)
private:
    void setTooltip() {}
    void timerCallback() override { if (isShowing()) repaint(); }
    MegaSynthProcessor& proc;
    const int slot;
    float shown = 0.0f;
};

// (v0.1.72) one effect in the Mixer's Effect Returns: its On light (click to switch) and its Return or Mix
class ReturnCell : public juce::Component, private juce::Timer
{
public:
    ReturnCell (MegaSynthProcessor&, int slot, int levelParam, const juce::String& name, juce::Colour);
    ~ReturnCell() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    const int slot, levelParam;
    std::function<void (int slot)> onOpen;   // (v0.1.74) a click on the name: that effect's page
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    MegaSynthProcessor& proc;
    juce::String name;
    juce::Colour colour;
    juce::Slider level;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
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
    void sortByGroup();
    void mouseDoubleClick (const juce::MouseEvent&) override;   // (v0.1.74) a row's name: that effect's page
    std::function<void (int slot)> onOpen;   // (v0.1.72) puts the effects in group order (Delays, Reverbs, Chorus, Phase, Width, Dirt, Motion)
    static constexpr int kRowH = 23, kTop = 30;   // (24 slots since v0.1.72)
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
    juce::TextButton sortBtn { "Sort by group" };
};

} // namespace tgui
