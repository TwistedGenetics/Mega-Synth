#pragma once
// The Break Chopper page (Oscillators > Break Chopper) and the Break Sequencer page
// (Sequencers > Break Sequencer): the per-slice and per-step editors, the step grid.
// Their settings live in the processor's BreakStore (saved with the patch, the project, undo and A/B).
// Included from PluginEditor.cpp after PluginEditor.h and SamplePages.h.
#include <JuceHeader.h>

namespace tgui
{

// A knob for a value in the BreakStore (not a plug-in parameter): caption, knob, value text
class StoreKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    StoreKnob (const juce::String& cap, double lo, double hi, double step, juce::Colour c,
               std::function<float()> get, std::function<void (float)> set, std::function<juce::String (double)> fmt)
        : getter (std::move (get)), setter (std::move (set))
    {
        caption.setText (cap, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::Font (juce::FontOptions (11.5f)));
        caption.setColour (juce::Label::textColourId, col::muted);
        addAndMakeVisible (caption);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 70, 18);
        slider.setRange (lo, hi, step);
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.textFromValueFunction = std::move (fmt);
        slider.onValueChange = [this] { if (! updating) setter ((float) slider.getValue()); };
        addAndMakeVisible (slider);
    }
    void refresh() { updating = true; slider.setValue (getter(), juce::dontSendNotification); slider.updateText(); updating = false; }
    void resized() override { caption.setBounds (0, 0, getWidth(), 16); slider.setBounds (0, 16, getWidth(), getHeight() - 16); }
    juce::Slider slider;
private:
    juce::Label caption;
    std::function<float()> getter;
    std::function<void (float)> setter;
    bool updating = false;
};

// A switch for a value in the BreakStore
class StoreToggle : public juce::ToggleButton
{
public:
    StoreToggle (const juce::String& t, juce::Colour c, std::function<bool()> get, std::function<void (bool)> set) : juce::ToggleButton (t), getter (std::move (get)), setter (std::move (set))
    {
        setColour (juce::ToggleButton::tickColourId, c);
        onClick = [this] { setter (getToggleState()); };
    }
    void refresh() { setToggleState (getter(), juce::dontSendNotification); }
private:
    std::function<bool()> getter;
    std::function<void (bool)> setter;
};

inline juce::Colour breakColourForGrid (int k)
{
    static const juce::Colour c[] = { juce::Colour (0xffff6b6b), juce::Colour (0xfffeca57), juce::Colour (0xff00d2d3) };
    return c[juce::jlimit (0, 2, k)];
}

// The step grid: 16 or 32 cells with the slice number, velocity, accent, roll, reverse and tie
class BreakStepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit BreakStepGrid (MegaSynthProcessor& p) : proc (p)
    {
        setTooltip ("Click a step to edit it below; double-click switches it on or off. The playing step is lit.");
    }
    std::function<void (int)> onSelect;
    int selected = 0;
    void refresh()
    {
        const auto d = proc.breakStore.get();
        const int pat = editedPattern(), steps = numSteps();
        juce::String key; key << pat << "|" << steps << "|" << selected << "|" << proc.bsStepShown.load() << "|" << proc.bsPatternShown.load();
        for (int i = 0; i < steps; ++i) { const auto& s = d.step[pat][i]; key << (s.on ? "1" : "0") << s.slice << s.roll << (s.accent ? "a" : "") << (s.reverse ? "r" : "") << (s.tie ? "t" : "") << juce::roundToInt (s.vel * 20.0f) << "b" << s.brk << (s.layer ? "L" : "") << juce::roundToInt (s.pitch) << "s" << s.stretch << juce::roundToInt (s.cycleMs) << "n" << juce::roundToInt (s.nudge * 100); }
        if (key != lastKey) { lastKey = key; data = d; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        const int steps = numSteps(), pat = editedPattern();
        const int playing = proc.bsPatternShown.load() == pat ? proc.bsStepShown.load() : -1;
        for (int i = 0; i < steps; ++i)
        {
            auto r = cell (i).toFloat().reduced (2.0f);
            const auto& s = data.step[pat][i];
            g.setColour (i == playing ? col::seq.withAlpha (0.55f) : ((i / 4) % 2 == 0 ? col::panel2 : col::panel3));
            g.fillRoundedRectangle (r, 5.0f);
            if (s.on || s.tie)
            {
                const float h = (r.getHeight() - 22.0f) * juce::jlimit (0.0f, 1.0f, s.vel);
                const juce::Colour bc = s.brk > 0 ? breakColourForGrid (s.brk - 1) : col::seq;
                g.setColour ((s.accent ? bc.brighter (0.5f) : bc).withAlpha (s.tie ? 0.35f : 0.85f));
                g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 3, r.getBottom() - 3 - h, r.getWidth() - 6, h), 3.0f);
                g.setColour (col::text);
                g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
                g.drawText (s.tie ? juce::String ("tie") : juce::String (s.slice + 1), r.withHeight (18.0f), juce::Justification::centred);
                juce::String marks;
                if (s.roll > 1) marks << "x" << s.roll;
                if (s.reverse) marks << " R";
                if (s.pitch != 0.0f) marks << " " << (s.pitch > 0 ? "+" : "") << juce::roundToInt (s.pitch);
                if (s.brk > 0) marks << " B" << s.brk;
                if (s.layer) marks << " +" << s.layerBrk;
                if (s.stretch > 0) marks << " " << (s.stretch == 1 ? "R" : (s.stretch == 2 ? "A" : "C")) << (s.cycleMs > 0.0f ? "*" : "");
                else if (s.cycleMs > 0.0f) marks << " *";
                if (s.nudge > 0.0f) marks << " >";
                g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                g.drawText (marks, r.withTrimmedTop (16.0f).withHeight (13.0f), juce::Justification::centred);
            }
            if (i == selected) { g.setColour (col::text); g.drawRoundedRectangle (r, 5.0f, 1.6f); }
            g.setColour (col::muted.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (r.getX(), r.getBottom() + 1.0f, r.getWidth(), 11.0f), juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = stepAt (e.getPosition());
        if (i < 0) return;
        selected = i; if (onSelect) onSelect (i);
        if (e.getNumberOfClicks() >= 2)
        {
            const int pat = editedPattern();
            proc.breakStore.edit ([&] (tg::BreakData& d)
            {
                auto& s = d.step[pat][i];
                s.on = ! s.on;
                const int ab = juce::jlimit (0, 2, juce::roundToInt (proc.param (tg::P_bcActive)->convertFrom0to1 (proc.param (tg::P_bcActive)->getValue())) - 1);
                const int ns = d.brk[s.brk > 0 ? s.brk - 1 : ab].nSlices;
                if (s.on && s.slice == 0 && i > 0 && ns > 0) s.slice = (i * ns / numSteps()) % ns;   // a sensible first slice
            });
        }
        lastKey.clear(); refresh();
    }
    int editedPattern() const { return juce::jlimit (0, 7, juce::roundToInt (proc.param (tg::P_bsPattern)->convertFrom0to1 (proc.param (tg::P_bsPattern)->getValue()))); }
    int numSteps() const { return proc.param (tg::P_bsSteps)->getValue() > 0.5f ? 32 : 16; }
private:
    juce::Rectangle<int> cell (int i) const
    {
        const int per = 16, rows = numSteps() / per, row = i / per, c = i % per;
        const int w = getWidth() / per, h = (getHeight() - rows * 12) / rows;
        return { c * w, row * (h + 12), w, h };
    }
    int stepAt (juce::Point<int> p) const { for (int i = 0; i < numSteps(); ++i) if (cell (i).contains (p)) return i; return -1; }
    MegaSynthProcessor& proc;
    tg::BreakData data;
    juce::String lastKey;
};

// Keeps the store-bound controls showing the store's values
class StoreTicker : public juce::Component, private juce::Timer
{
public:
    StoreTicker() { startTimerHz (15); }
    ~StoreTicker() override { stopTimer(); }
    std::function<void()> tick;
private:
    void timerCallback() override { if (tick && getParentComponent() != nullptr && getParentComponent()->isShowing()) tick(); }
};

// The scroll bar under a zoomed wave (it moves the view; the view moves it)
class WaveScrollBar : public juce::ScrollBar, private juce::ScrollBar::Listener
{
public:
    WaveScrollBar() : juce::ScrollBar (false) { setRangeLimits (0.0, 1.0); setCurrentRange (0.0, 1.0); setAutoHide (false); addListener (this); }
    ~WaveScrollBar() override { removeListener (this); }
    std::function<void (double start)> onMove;
    void show (double start, double len) { updating = true; setCurrentRange (start, len, juce::dontSendNotification); updating = false; }
private:
    void scrollBarMoved (juce::ScrollBar*, double start) override { if (! updating && onMove) onMove (start); }
    bool updating = false;
};

// each break's colour, everywhere it's shown (the page, the step grid, the keyboard)
inline juce::Colour breakColour (int k) { return breakColourForGrid (k); }

inline juce::String breakInfoText (const MegaSynthProcessor& p, int k)
{
    const int slot = MegaSynthProcessor::breakSlot (k);
    juce::String t;
    if (p.getSample (slot) == nullptr) t = "Break " + juce::String (k + 1) + ": empty";
    else
    {
        const auto d = p.breakStore.get();
        const auto& b = d.brk[k];
        t = sampleInfoText (p, slot);
        t << "\n" << b.nSlices << " slices   " << juce::String (b.origBpm, 1) << " BPM";
        if (b.bars > 0) t << " (" << b.bars << (b.bars == 1 ? " bar)" : " bars)");
        const double host = p.getHostTempoShown();
        if (b.origBpm > 1.0 && host > 0.0)
        {
            const bool sync = p.param (tg::P_bcSync)->getValue() > 0.5f;
            t << "   at " << juce::String (host, 1) << " BPM: " << (sync ? "x" + juce::String (host / b.origBpm, 3) : juce::String ("not synced (x1)"));
        }
    }
    // the three together: a warning above 20 MB
    double mb = 0.0;
    for (int j = 0; j < 3; ++j) mb += (double) p.getSampleBytes (MegaSynthProcessor::breakSlot (j)) / (1024.0 * 1024.0);
    if (mb > 20.0) t << "   (all three breaks add " << juce::String (mb, 1) << " MB to the project: saving and undo get slower)";
    return t;
}

} // namespace tgui
