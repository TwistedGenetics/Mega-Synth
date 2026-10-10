#pragma once
// DNA Seq tab, below the clock section: pattern / lane / chain / MIDI bar, editing tools, the
// generator and the 32 step cells (type, amount, probability, ratchet, glide, lock).
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "FilterPanel.h"   // Segmented

namespace tgui
{

// A small button that draws its own short label (the step cells are narrow)
class TinyButton : public juce::Button
{
public:
    TinyButton() : juce::Button ({}) {}
    juce::Colour on;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class DnaCell : public juce::Component, public juce::SettableTooltipClient
{
public:
    DnaCell (MegaSynthProcessor&, int index, std::function<int()> lane);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    void setPlaying (bool);
private:
    void push (std::function<void (tg::DnaStep&)> change);
    MegaSynthProcessor& proc;
    const int index;
    std::function<int()> lane;
    bool playing = false, updating = false;
    juce::ComboBox type;
    juce::Slider amount, prob;
    TinyButton ratchet, glide, lock;
};

class DnaSeqPanel : public juce::Component, private juce::Timer
{
public:
    explicit DnaSeqPanel (MegaSynthProcessor&);
    ~DnaSeqPanel() override { stopTimer(); }
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    void refreshAll();
    void readGen();
    void writeGen();
    MegaSynthProcessor& proc;
    int lane = 0;
    Segmented pattern, laneSel;
    juce::ToggleButton chainOn { "Chain" }, lane2On { "Lane 2 On" };
    juce::TextEditor chain;
    juce::ComboBox midiSel, style;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chainAtt, lane2Att;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiAtt;
    juce::TextButton copy { "Copy" }, paste { "Paste" }, left { "<<" }, right { ">>" }, reverse { "Reverse" }, clear { "Clear" };
    juce::TextButton seedDown { "<" }, seedUp { ">" }, generate { "Generate" };
    juce::Label seedLabel, genCap, usesCap;
    juce::Slider fill, amtMin, amtMax, extras, change, rotate;
    juce::OwnedArray<juce::TextButton> uses;   // transforms the generator may use
    juce::OwnedArray<DnaCell> cells;
    uint32_t lastVersion = 0;
    int lastPattern = -1, lastLane = -1, lastStep = -2, lastLen = -1;
};

} // namespace tgui
