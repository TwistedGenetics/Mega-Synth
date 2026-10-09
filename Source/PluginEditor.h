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
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.5f)); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 133; }
};

// Slider that hands right-clicks to its owner instead of dragging.
class KnobSlider : public juce::Slider
{
public:
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { rightDown = true; if (onRightClick) onRightClick(); return; }
        rightDown = false; juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! rightDown) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! rightDown) juce::Slider::mouseUp (e); rightDown = false; }
private:
    bool rightDown = false;
};

// A rotary knob with a caption above and the parameter's value text below.
// When the modulation matrix drives its parameter it shows a ring: the range the routes
// can reach, and a dot at the value the newest note is hearing right now.
class Knob : public juce::Component
{
public:
    Knob (MegaSynthProcessor&, int paramIndex, const juce::String& caption, juce::Colour);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void updateMod();                          // called by the editor's timer
    KnobSlider slider;
    const int paramIndex;
    std::function<void (int slot)> onShowRoute;   // open the matrix at a route
private:
    void showMenu();
    MegaSynthProcessor& proc;
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool hasMod = false;
    float modLo = 0, modHi = 0, live = 0;
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

// One row of the modulation matrix.
class RouteRow : public juce::Component
{
public:
    RouteRow (MegaSynthProcessor&, int slot, std::function<juce::String()> searchText);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();          // from the route store
    void updateLive();       // meters
    void chooseDest();
    const int slot;
private:
    void push();
    MegaSynthProcessor& proc;
    std::function<juce::String()> search;
    juce::ToggleButton on;
    juce::ComboBox src, curve, via;
    juce::TextButton dest, polarity, clear { "X" };
    juce::Slider amount, viaDepth, smooth;
    juce::Label amtText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtAtt;
    int dstIndex = -1;
    float srcLive = 0, dstLive = 0;
    bool highlight = false;
public:
    void flash() { highlight = true; repaint(); }
};

// XY pad for the scene morph (A top-left, B top-right, C bottom-left, D bottom-right)
class XYPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit XYPad (MegaSynthProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void update();   // timer
private:
    void setFrom (juce::Point<float>);
    MegaSynthProcessor& proc;
    float x = 0, y = 0, lx = 0, ly = 0;
    bool morph = false, stored[4] {};
    int edit = 0;
};

// A macro knob with an editable name and a count of what it drives
class MacroCell : public juce::Component
{
public:
    MacroCell (MegaSynthProcessor&, int index);
    void resized() override;
    void paint (juce::Graphics&) override;
    void update();
    Knob knob;
private:
    MegaSynthProcessor& proc;
    const int index;
    juce::Label name, info;
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

// Load / Clear buttons and status for a sample oscillator
class SampleBar : public juce::Component
{
public:
    SampleBar()
    {
        for (auto* b : { &load, &clear }) addAndMakeVisible (*b);
        status.setColour (juce::Label::textColourId, col::wavetable);
        status.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (status);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        load.setBounds (r.removeFromLeft (110).reduced (0, 1));
        r.removeFromLeft (6);
        clear.setBounds (r.removeFromLeft (70).reduced (0, 1));
        r.removeFromLeft (8);
        status.setBounds (r);
    }
    juce::TextButton load { "Load Sample" }, clear { "Clear" };
    juce::Label status;
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
    juce::Label title, subtitle, status;
    tgui::SampleBar* sampleBars[2] { nullptr, nullptr };   // owned by the Oscillators page
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, originalBtn { "Original" };
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::TextButton copyBtn { "Copy Patch" },
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
    juce::Array<tgui::Knob*> allKnobs;
    juce::Array<tgui::RouteRow*> routeRows;
    std::unique_ptr<juce::Viewport> matrixView;
    juce::TextEditor matrixSearch;
    int matrixTab = -1;
    uint32_t lastRouteVersion = 0;
    tgui::XYPad* xyPad = nullptr;
    juce::Label* dnaInfo = nullptr;
    int lastDnaMode = -1;
    juce::Array<tgui::MacroCell*> macroCells;
    juce::TextButton sceneEditBtn[4], sceneStoreBtn[4];
    juce::Label sceneState[4];
    juce::ToggleButton morphBtn { "Morph between scenes" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> morphAtt;
    juce::TextButton clearScenesBtn { "Clear scenes" };
    void updateSceneButtons();
    void showRoute (int slot);
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
