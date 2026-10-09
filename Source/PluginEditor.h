#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace tgui
{

namespace col
{
    const juce::Colour bg      { 0xff0d1117 };
    const juce::Colour panel   { 0xff161b22 };
    const juce::Colour panel2  { 0xff1f2630 };
    const juce::Colour panel3  { 0xff27303c };
    const juce::Colour text    { 0xffedf3fa };
    const juce::Colour muted   { 0xff9fb0c3 };
    const juce::Colour accent  { 0xff66b3ff };
    const juce::Colour border  { 0xff344252 };
    const juce::Colour master  { 0xff66b3ff };
    const juce::Colour osc     { 0xffff9f43 };
    const juce::Colour wavetable { 0xfff368e0 };
    const juce::Colour complex { 0xffff6b6b };
    const juce::Colour supersaw { 0xfffeca57 };
    const juce::Colour mixer   { 0xff1dd1a1 };
    const juce::Colour fm      { 0xff48dbfb };
    const juce::Colour filter  { 0xff8e6cf0 };
    const juce::Colour env     { 0xff10ac84 };
    const juce::Colour mod     { 0xff00d2d3 };
    const juce::Colour fx      { 0xffff9ff3 };
    const juce::Colour seq     { 0xff54a0ff };
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.5f)); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 150; }
};

// A rotary knob with a caption above and the parameter's value text below.
class Knob : public juce::Component
{
public:
    Knob (MegaSynthProcessor&, int paramIndex, const juce::String& caption, juce::Colour);
    void resized() override;
    juce::Slider slider;
private:
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// Caption + drop-down attached to a choice parameter.
class Choice : public juce::Component
{
public:
    Choice (MegaSynthProcessor&, int paramIndex, const juce::String& caption);
    void resized() override;
    juce::ComboBox box;
private:
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

// A titled, coloured panel that lays its controls out in rows.
class Section : public juce::Component
{
public:
    Section (MegaSynthProcessor&, const juce::String& title, juce::Colour);
    Knob* knob (int paramIndex, const juce::String& caption, int width = 72);
    Choice* choice (int paramIndex, const juce::String& caption, int width = 150);
    void add (juce::Component* c, int width, int height);   // takes ownership
    void newRow();
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Colour colour;

private:
    MegaSynthProcessor& proc;
    juce::String title;
    struct Item { juce::Component* c; int w, h; bool rowBreak; };
    std::vector<Item> items;
    juce::OwnedArray<juce::Component> owned;
    bool pendingBreak = false;
};

// Modulation assignment slot: source, target, bipolar amount.
class AssignSlot : public juce::Component
{
public:
    AssignSlot (MegaSynthProcessor&, int firstParam, const juce::String& title, juce::Colour);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
private:
    MegaSynthProcessor& proc;
    int first;
    juce::String title;
    juce::Colour colour;
    juce::ComboBox source, target;
    juce::Slider amount;
    juce::Label amountText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtAtt;
};

class StepCell : public juce::Component
{
public:
    StepCell (MegaSynthProcessor&, int index);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    void setPlaying (bool);
private:
    void push();
    MegaSynthProcessor& proc;
    int index;
    bool playing = false;
    juce::ComboBox note, oct, tie;
    juce::ToggleButton accent { "Accent" };
};

class Page : public juce::Component
{
public:
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
    void resized() override { if (onResize) onResize(); }
    std::function<void()> onResize;
    juce::OwnedArray<juce::Component> children;
    template <typename T> T* own (T* c) { children.add (c); addAndMakeVisible (c); return c; }
};

} // namespace tgui

//==============================================================================
class MegaSynthEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit MegaSynthEditor (MegaSynthProcessor&);
    ~MegaSynthEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void buildPages();
    void setStatus (const juce::String&);
    void shiftOctave (int delta);
    void refreshPatchList();
    void loadPatch (const juce::File&);
    void stepPatch (int delta);

    MegaSynthProcessor& proc;
    tgui::Look look;

    juce::Component content;   // fixed 1200x800 design, scaled to the window
    juce::Label title, subtitle, sampleStatus, status;
    juce::TextButton loadBtn { "Load Sample" }, clearBtn { "Clear Sample" }, copyBtn { "Copy Patch" },
                     pasteBtn { "Paste Patch" }, initBtn { "Init" }, octDown { "Oct -" }, octUp { "Oct +" };
    juce::OwnedArray<tgui::Knob> headerKnobs;
    juce::ComboBox patchBox;
    juce::TextButton patchPrev { "<" }, patchNext { ">" }, saveBtn { "Save Patch" };
    juce::Array<juce::File> patchFiles;
    juce::String lastShownName;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::MidiKeyboardComponent keyboard;
    juce::Label octLabel;

    juce::Array<tgui::AssignSlot*> assignSlots;   // owned by their tab pages
    juce::Array<tgui::StepCell*> stepCells;
    juce::TextButton seqRandomBtn { "Random Phrase" };
    juce::ToggleButton seqRunBtn { "Run Sequencer" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqRunAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    uint32_t lastStepVersion = 0;
    int lastPlaying = -2, lastLength = -1, lastOct = 99;
    int statusTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaSynthEditor)
};
