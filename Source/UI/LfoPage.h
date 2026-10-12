#pragma once
// LFOs tab: the four LFOs with sync, retrigger, phase, one-shot, smoothing, fade-in, unipolar and
// a drawable custom shape with a live playhead.
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

class Knob;
class Choice;
class Segmented;

// Shows an LFO's shape (the waveform, or the custom points) with the newest note's playhead.
// Editing the points turns Custom on.
//   drag a point to move it, double-click empty space to add one, right-click a point for its
//   curve (linear / smooth / step) or to delete it, shift-drag a segment to bend it.
class LfoShapeEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    LfoShapeEditor (MegaSynthProcessor&, int lfo);
    ~LfoShapeEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    int grid = 0;   // snap: 0 off, else divisions per cycle (4, 8, 16, 6, 12)
private:
    void timerCallback() override;
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (8.0f, 8.0f); }
    juce::Point<float> toScreen (float x, float y) const;
    float snapX (float x) const;
    int pointAt (juce::Point<float>) const;
    void commit (std::vector<tg::LfoShapeStore::Point>);
    MegaSynthProcessor& proc;
    const int lfo;
    std::vector<tg::LfoShapeStore::Point> pts;
    int drag = -1, bendSeg = -1;
    float bendStart = 0.0f, bendY = 0.0f;
    uint32_t lastVersion = ~0u;
    float lastPlayhead = -2.0f;
    float lastParamKey = -1.0f;
};

class LfoPanel : public juce::Component, private juce::Timer
{
public:
    LfoPanel (MegaSynthProcessor&, int lfo, juce::Colour);
    ~LfoPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static std::vector<tg::LfoShapeStore::Point>& clipboard();
    static bool& clipboardValid();
    juce::OwnedArray<Knob> knobs;
private:
    void timerCallback() override;
    MegaSynthProcessor& proc;
    const int lfo;
    juce::Colour colour;
    std::unique_ptr<Choice> wave, sync, retrig, mode;
    juce::ToggleButton uni { "Unipolar" }, custom { "Custom" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> btnAtt;
    std::unique_ptr<Segmented> gridSel;
    juce::TextButton presets { "Presets" }, copy { "Copy" }, paste { "Paste" };
    std::unique_ptr<LfoShapeEditor> editor;
    std::unique_ptr<juce::Component> chip;   // drag onto a knob to modulate it
};

} // namespace tgui

namespace tgui
{
// MSEG editor: stages as points along time (x) and level (y).
//   drag a point (time and level), double-click to add a stage, right-click a point for sustain / loop / delete,
//   shift-drag a stage to bend its curve.
class MsegEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    MsegEditor (MegaSynthProcessor&, int env);
    ~MsegEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    float totalTime() const;
    juce::Point<float> pt (int i) const;   // i = -1: start level
    int pointAt (juce::Point<float>) const;
    void commit();
    MegaSynthProcessor& proc;
    const int env;
    tg::MsegShape shape;
    float scaleT = 1.0f;   // seconds shown across the width (fixed while dragging)
    int drag = -2, bend = -1; float bendStart = 0, bendY = 0;
    uint32_t lastVersion = ~0u; float lastPlay = -2.0f;
};
} // namespace tgui
