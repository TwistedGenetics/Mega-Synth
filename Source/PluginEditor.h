#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/PatchBrowser.h"
#include "UI/MidiMonitor.h"
#include "UI/MasterFilterView.h"

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
    int getTabButtonBestWidth (juce::TabBarButton& b, int) override
    {
        // as wide as the name needs, so 19 tabs fit without cutting names off
        return 16 + juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)), b.getButtonText());
    }
};

// Slider that hands right-clicks to its owner instead of dragging.
class KnobSlider : public juce::Slider
{
public:
    std::function<void()> onRightClick;
    // mod depth with the mouse (Serum-style): return true from onDepthDown to take the gesture
    std::function<bool (const juce::MouseEvent&)> onDepthDown, onDepthDouble;
    std::function<void (const juce::MouseEvent&)> onDepthDrag, onDepthUp;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { rightDown = true; if (onRightClick) onRightClick(); return; }
        rightDown = false;
        depth = onDepthDown && onDepthDown (e);
        if (! depth) juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (rightDown) return; if (depth) { if (onDepthDrag) onDepthDrag (e); return; } juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (depth) { depth = false; if (onDepthUp) onDepthUp (e); return; }
        if (! rightDown) juce::Slider::mouseUp (e);
        rightDown = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (onDepthDouble && onDepthDouble (e)) return; juce::Slider::mouseDoubleClick (e); }
private:
    bool rightDown = false, depth = false;
};

// A rotary knob with a caption above and the parameter's value text below.
// When the modulation matrix drives its parameter it shows a ring: the range the routes
// can reach, and a dot at the value the newest note is hearing right now.
class Knob : public juce::Component, public juce::DragAndDropTarget
{
public:
    Knob (MegaSynthProcessor&, int paramIndex, const juce::String& caption, juce::Colour);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;   // the route chip
    void updateMod();                          // called by the editor's timer
    KnobSlider slider;
    const int paramIndex;
    std::function<void (int slot)> onShowRoute;   // open the matrix at a route

    // ---- mod depth with the mouse (Serum-style). Each route on this knob is an arc in its source's
    // colour; drag an arc (or Ctrl / Cmd-drag anywhere) up or down to set that route's depth (Shift:
    // fine). Double-click an arc: depth 0. Alt-click an arc: remove the route. The chip in the corner
    // picks which route the mouse edits (click it to go round them). Drop a source chip (from an LFO,
    // envelope, MSEG, macro or XY pad) on a knob to make a new route at 25%.
    const std::vector<int>& routeSlots() const { return slots; }
    int selectedRoute() const { return selSlot; }
    void selectNextRoute();
    juce::Point<float> ringPointForTests (int routeIndex, float proportion) const;   // knob coordinates
    juce::Rectangle<int> chipBounds() const { return { 0, 1, std::min (24, getWidth() / 3), 13 }; }
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
private:
    void showMenu();
    struct Ring { juce::Point<float> c; float r = 0, start = 0, span = 0; };
    Ring ring() const;                        // in knob coordinates
    int routeAtRing (juce::Point<float> p) const;   // the route whose arc is there (index into slots), -1
    float amountOf (int slot) const;
    void setAmount (int slot, float a);
    MegaSynthProcessor& proc;
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool hasMod = false, dropHover = false;
    float modLo = 0, modHi = 0, live = 0;
    std::vector<int> slots;                   // the routes on this knob, in slot order
    int selSlot = -1;                         // the one the mouse edits
    int dragSlot = -1; float dragStart = 0, dragY = 0;   // a depth drag in progress
    uint32_t shownRouteKey = 0;
    int shownCC = -1; bool shownLearn = false;   // MIDI learn
};

// A source you can drag onto any knob to modulate it (LFOs, envelopes, MSEGs, macros, XY pads)
class ModSourceChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModSourceChip (int src, const juce::String& label);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    const int source;
private:
    juce::String text;
};
juce::Colour modSourceColour (int src);
juce::String modSourceShort (int src);

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
    // the explanation behind the (i) next to the title ("Name  (explanation)" titles are split automatically)
    void addInfo (const juce::String& text);
    // a small component beside the title (the drag-to-modulate source chips); takes ownership
    void addTitleChip (juce::Component* c, int width);
    juce::String getTitle() const { return title; }
    juce::String getInfo() const { return info; }
    juce::Colour colour;

private:
    MegaSynthProcessor& proc;
    juce::String title, info;
    std::unique_ptr<juce::Component> infoBadge;
    struct Item { juce::Component* c; int w, h; bool rowBreak; };
    std::vector<Item> items;
    juce::OwnedArray<juce::Component> owned;
    std::vector<std::pair<juce::Component*, int>> titleChips;
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

// Capture: waveform of the last recording with the trim region; drag the edges to trim
class CaptureView : public juce::Component
{
public:
    explicit CaptureView (MegaSynthProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void refresh();                 // rebuild the overview after a new recording
    std::function<void()> onEdit;
private:
    MegaSynthProcessor& proc;
    std::vector<float> peaks;
    int dragging = 0;
};

// Feedback matrix grid: rows = where the signal comes from, columns = where it goes
class FbGrid : public juce::Component
{
public:
    explicit FbGrid (MegaSynthProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::Slider amt[4][3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att[4][3];
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

// A top-level tab with sub-tabs: a strip of segments at the top, one sub-page shown below it
class Segmented;
class ChannelStrip; class EngineChip; class LevelMeter; class BreakStepGrid;   // the Mixer console (UI/MixerConsole.h)
class GroupPage : public juce::Component
{
public:
    GroupPage (MegaSynthProcessor&, const juce::StringArray& subNames, juce::Colour);
    ~GroupPage() override;
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
    void resized() override;
    Page* sub (int i) const { return subs[i]; }
    int numSubs() const { return subs.size(); }
    const juce::StringArray& names() const { return labels; }
    void show (int i);
    int current() const { return shown; }
    std::function<void (int)> onShow;
    static constexpr int kStripH = 34;
private:
    juce::StringArray labels;
    juce::OwnedArray<Page> subs;
    std::unique_ptr<Segmented> strip;
    int shown = 0;
};

} // namespace tgui

//==============================================================================
class MegaSynthEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer,
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
public:
    // pages by name (a top-level tab, or a sub-tab such as "Mod Matrix"); old tab names still work
    bool showPage (const juce::String& name);
    juce::StringArray allPageNames() const;
    juce::String currentPageName() const;
    void timerCallbackForTests();   // the editor's timer, and the pages' own refreshers (no message loop in tests)
private:
    struct PageRef { int tab = -1, sub = -1; tgui::Page* page = nullptr; };
    std::map<juce::String, PageRef> pageIndex;
    juce::String lastUiTabs;
    juce::Array<tgui::ChannelStrip*> mixerStrips;
    juce::Array<tgui::EngineChip*> engineChips;
    tgui::LevelMeter* outMeters[2] { nullptr, nullptr };
    void updateMixerView();
    tgui::Page* pageFor (const juce::String& name) const { auto it = pageIndex.find (name); return it != pageIndex.end() ? it->second.page : nullptr; }
    void setStatus (const juce::String&);
    void breakKeyEntry (tgui::BreakStepGrid& grid);   // Break Sequencer Key Entry (from the page's ticker)
    void shiftOctave (int delta);
    void refreshPatchList();
    void loadPatch (const juce::File&);
    void loadFactory (int index);
    static constexpr int kFactoryIdBase = 5000;
    void stepPatch (int delta);

    MegaSynthProcessor& proc;
    tgui::Look look;

    juce::Component content;   // fixed 1200x800 design, scaled to the window
    juce::Label title, subtitle, status;
    tgui::SampleBar* sampleBars[2] { nullptr, nullptr };   // owned by the Oscillators page
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, originalBtn { "Original" };
    std::unique_ptr<juce::TooltipWindow> tooltips;
    juce::TextButton copyBtn { "Copy Patch" },
                     pasteBtn { "Paste Patch" }, initBtn { "Init" }, octDown { "Oct -" }, octUp { "Oct +" }, randomBtn { "Random" };
    int lastRandomReport = 0;
    juce::OwnedArray<tgui::Knob> headerKnobs;
    std::unique_ptr<tgui::Choice> qualityChoice;
    juce::ComboBox patchBox;
    juce::TextButton patchPrev { "<" }, patchNext { ">" }, saveBtn { "Save" }, browseBtn { "Browse" };
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "Copy" }, sizeBtn { "Size" };
    std::unique_ptr<tgui::PatchBrowser> browser;
    void showBrowser (bool);
    void showSizeMenu();
public:
    void showSaveDialog();                       // Save: name, category, tags, author, note
    struct SaveInfo { juce::String name; tg::PatchMeta meta; };
    SaveInfo defaultSaveInfo() const;            // what the dialog offers
    void saveWithInfo (const SaveInfo&, bool saveAs);
    static juce::StringArray authorsInPatches();   // (for the Save dialog's author list)
    void breakKeyEntryForTests();   // (tests) run the Break Sequencer page's Key Entry now
    int breakSelectedStepForTests() const;
    void setBreakSelectedStepForTests (int step);
    tgui::PatchBrowser* browserForTests() { return browser.get(); }
    void showBrowserForTests (bool b) { showBrowser (b); }
private:
    void finishSave (int button);
    void savePatchAs (const juce::File&);
    std::unique_ptr<juce::AlertWindow> saveWin;
    bool sizeReady = false;   // the window size is only remembered once the editor has opened at its size
    juce::Array<juce::File> patchFiles;
    juce::String lastShownName;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    // the on-screen keyboard, with the Break Chopper's key zone tinted
    class ZoneKeyboard : public juce::MidiKeyboardComponent
    {
    public:
        using juce::MidiKeyboardComponent::MidiKeyboardComponent;
        std::function<juce::Colour (int)> tint;   // transparent: none
        void drawWhiteNote (int n, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour line, juce::Colour text) override
        {
            juce::MidiKeyboardComponent::drawWhiteNote (n, g, area, down, over, line, text);
            if (tint) { const auto c = tint (n); if (! c.isTransparent()) { g.setColour (c.withAlpha (0.55f)); g.fillRect (area.withTrimmedTop (area.getHeight() * 0.78f).reduced (1.0f, 0.0f)); } }
        }
        void drawBlackNote (int n, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour fill) override
        {
            juce::MidiKeyboardComponent::drawBlackNote (n, g, area, down, over, fill);
            if (tint) { const auto c = tint (n); if (! c.isTransparent()) { g.setColour (c.withAlpha (0.8f)); g.fillRect (area.withTrimmedTop (area.getHeight() * 0.7f).reduced (2.0f, 0.0f)); } }
        }
    };
    ZoneKeyboard keyboard;
    juce::String lastZoneKey;
    std::unique_ptr<tgui::MidiMonitor> midiMonitor;
    // right-click on any toggle or choice box: MIDI Learn / Forget (knobs have their own menu)
    struct LearnClicks : juce::MouseListener
    {
        explicit LearnClicks (MegaSynthEditor& e) : ed (e) {}
        void mouseDown (const juce::MouseEvent&) override;
        MegaSynthEditor& ed;
    } learnClicks { *this };
    void showInitMenu();
    juce::Label octLabel;

    juce::Array<tgui::AssignSlot*> assignSlots;   // owned by their tab pages
    juce::Array<tgui::Knob*> allKnobs;
    juce::Array<tgui::RouteRow*> routeRows;
    std::unique_ptr<juce::Viewport> matrixView;
    juce::TextEditor matrixSearch;
    uint32_t lastRouteVersion = 0;
    tgui::XYPad* xyPad = nullptr;
    juce::Component* perfXY = nullptr;   // Perform page
    juce::ToggleButton perfMorphBtn { "Morph between scenes" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> perfMorphAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> perfAtts;
    juce::Label perfSeed;
    juce::TextButton perfSeedDown { "<" }, perfSeedUp { ">" }, perfNewSeed { "New seed" }, perfCommit { "Commit" };
    juce::Label* dnaInfo = nullptr;
    juce::TextButton mutNew { "New seed" }, mutPrev { "<" }, mutNext { ">" }, mutCommit { "Commit" };
    juce::Label mutSeedLabel, mutHistLabel;
    juce::OwnedArray<juce::ToggleButton> mutLocks;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> mutLockAtts, fxAtts;
    int lastSeedShown = -1, lastHistCount = -1;
    // Genetic Lab
    juce::Label labParent[2], labGenLabel, labLine;
    juce::TextButton labUse[2], labBreedBtn { "Breed 8 children" }, labGenPrev { "<" }, labGenNext { ">" },
                     labToA { "Child > Parent A" }, labToB { "Child > Parent B" }, labSave { "Save child as patch..." };
    juce::TextButton labChild[8];
    juce::Slider labVar;
    int labGen = -1;
    juce::String labShownState;
    void updateLab();
    juce::Component* flowView = nullptr;   // Overview tab (UI/Overview.h)
    juce::Component* netView = nullptr;
    juce::Component* specView = nullptr;
    juce::Component* dnaView = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dsOnAtt;
    tgui::CaptureView* capView = nullptr;
    juce::TextButton capRecord { "Record" }, capSave { "Save WAV..." };
    juce::OwnedArray<juce::TextButton> capSends;
    juce::ToggleButton capReverse { "Reverse" }, capNormalise { "Normalise" }, capZero { "Snap to zero crossings" };
    juce::Slider capFadeIn, capFadeOut;
    juce::Label capInfo;
    int lastCapLen = -1;
    void updateCaptureInfo();
    int lastDnaMode = -1;
    juce::Array<tgui::MacroCell*> macroCells;
    juce::TextButton sceneEditBtn[4], sceneStoreBtn[4];
    juce::Label sceneState[4];
    juce::ToggleButton morphBtn { "Morph between scenes" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> morphAtt, freezeAtt, spOnAtt, spFreezeAtt;
    juce::TextButton clearScenesBtn { "Clear scenes" };
    void updateSceneButtons();
public:
    void showRoute (int slot);   // opens Modulation > Mod Matrix at that route (public for tests)
private:
    juce::Array<tgui::StepCell*> stepCells;
    juce::TextButton seqRandomBtn { "Random Phrase" };
    juce::ToggleButton seqRunBtn { "Run Sequencer" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqRunAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label* granInfo = nullptr;   // Granular Synth sample: name, length, size
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> latencyAtt;   // Break Chopper: chop tap latency
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> markerSnapAtt;   // Break Chopper: marker snap
public:
    juce::Component* breakWaveForTests = nullptr;
    juce::Component* breakGridForTests = nullptr;
private:

    uint32_t lastStepVersion = 0;
    int lastPlaying = -2, lastLength = -1, lastOct = 99;
    int statusTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MegaSynthEditor)
};
