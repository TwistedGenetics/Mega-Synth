#pragma once
// Arp / Chord tab: chord memory (preset or learned chord, spread, inversion) and the arpeggiator
// (mode, octaves, rate, swing, gate, latch, velocity, repeats, restart and a 16-step pattern).
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

// A toggle attached to a boolean parameter (owns its attachment)
class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (MegaSynthProcessor& p, int param, const juce::String& text, juce::Colour c) : juce::ToggleButton (text)
    {
        setColour (juce::ToggleButton::tickColourId, c);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, tg::kParamIds[param], *this);
    }
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

// The 16-step accent / rest pattern: click cycles On > Accent > Rest, drag up / down sets the
// step's probability. The playing step is lit.
class ArpPatternGrid : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ArpPatternGrid (MegaSynthProcessor&);
    ~ArpPatternGrid() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    int stepAt (float x) const;
    MegaSynthProcessor& proc;
    int dragStep = -1; float dragStartY = 0, dragStartProb = 1; bool dragged = false;
    int shownStep = -2; uint32_t shownVersion = ~0u; int shownLen = -1;
};

// Shows the chord memory's chord as note names, and the Learn button's state
class ChordDisplay : public juce::Component, private juce::Timer
{
public:
    explicit ChordDisplay (MegaSynthProcessor& p) : proc (p) { startTimerHz (8); }
    ~ChordDisplay() override { stopTimer(); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    MegaSynthProcessor& proc;
};

} // namespace tgui
