#pragma once
// v0.1.74: the Core pages' Randomise panel and the DX7 voice browser (shown in call-out boxes).
#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace tgui
{

class CoreRandomPanel : public juce::Component
{
public:
    using State = CoreRandState;
    CoreRandomPanel (MegaSynthProcessor& p, int slot, State& st, std::function<void (const juce::String&)> status)
        : proc (p), slot (slot), state (st), onStatus (std::move (status))
    {
        for (int t = 0; t < tg::coregen::T_COUNT; ++t) type.addItem (tg::coregen::kTypeNames[t], t + 1);
        type.setSelectedId (state.type + 1, juce::dontSendNotification);
        type.onChange = [this] { state.type = type.getSelectedId() - 1; };
        crazy.setRange (0.0, 1.0, 0.01);
        crazy.setValue (state.crazy, juce::dontSendNotification);
        crazy.setSliderStyle (juce::Slider::LinearHorizontal);
        crazy.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        crazy.onValueChange = [this] { state.crazy = (float) crazy.getValue(); };
        static const char* lockNames[4] = { "Algorithm", "Envelopes", "Ratios", "Levels" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = locks.add (new juce::ToggleButton (lockNames[i]));
            b->setToggleState ((state.locks >> i) & 1, juce::dontSendNotification);
            b->onClick = [this, i, b] { state.locks = b->getToggleState() ? (state.locks | (1 << i)) : (state.locks & ~(1 << i)); };
            addAndMakeVisible (b);
        }
        seed.setText (juce::String (state.seed), juce::dontSendNotification);
        seed.setInputRestrictions (10, "0123456789");
        roll.onClick = [this] { state.seed = 1 + juce::Random::getSystemRandom().nextInt (999999); seed.setText (juce::String (state.seed), juce::dontSendNotification); go(); };
        again.onClick = [this] { state.seed = juce::jmax ((juce::int64) 1, seed.getText().getLargeIntValue()); go(); };
        roll.setTooltip ("A new roll (a new seed)");
        again.setTooltip ("Roll the seed in the box again: the same seed and settings give the same patch");
        for (juce::Component* c : std::initializer_list<juce::Component*> { &type, &crazy, &seed, &roll, &again, &title, &left, &right, &lockTitle, &seedTitle }) addAndMakeVisible (c);
        title.setText ("Randomise Core " + juce::String (slot == 0 ? "A" : "B"), juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        left.setText ("Controlled", juce::dontSendNotification); right.setText ("Crazy", juce::dontSendNotification);
        right.setJustificationType (juce::Justification::right);
        lockTitle.setText ("Keep:", juce::dontSendNotification); seedTitle.setText ("Seed:", juce::dontSendNotification);
        setSize (340, 210);
    }
    void resized() override
    {
        title.setBounds (10, 6, 320, 22);
        type.setBounds (10, 34, 160, 24);
        left.setBounds (10, 64, 90, 18); right.setBounds (240, 64, 90, 18);
        crazy.setBounds (10, 82, 320, 22);
        lockTitle.setBounds (10, 110, 50, 22);
        for (int i = 0; i < locks.size(); ++i) locks[i]->setBounds (60 + (i % 2) * 120, 110 + (i / 2) * 24, 118, 22);
        seedTitle.setBounds (10, 166, 44, 24);
        seed.setBounds (54, 166, 90, 24);
        again.setBounds (150, 166, 80, 24);
        roll.setBounds (236, 166, 94, 24);
    }
private:
    void go()
    {
        tg::coregen::Options o;
        o.type = state.type; o.crazy = state.crazy; o.locks = state.locks; o.seed = state.seed;
        const auto what = proc.randomiseCore (slot, o);
        if (onStatus) onStatus (juce::String ("Core ") + (slot == 0 ? "A" : "B") + " randomised: " + what);
    }
    MegaSynthProcessor& proc;
    const int slot;
    State& state;
    std::function<void (const juce::String&)> onStatus;
    juce::ComboBox type;
    juce::Slider crazy;
    juce::OwnedArray<juce::ToggleButton> locks;
    juce::TextEditor seed;
    juce::TextButton roll { "Randomise" }, again { "Roll seed" };
    juce::Label title, left, right, lockTitle, seedTitle;
};

class Dx7Browser : public juce::Component, private juce::ListBoxModel
{
public:
    Dx7Browser (MegaSynthProcessor& p, std::vector<tg::dx7::Voice> v, const juce::String& fileName, const juce::String& note, std::function<void (const juce::String&)> status)
        : proc (p), voices (std::move (v)), onStatus (std::move (status))
    {
        title.setText (fileName + "  (" + juce::String ((int) voices.size()) + (voices.size() == 1 ? " voice)" : " voices)"), juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        info.setText (note.isNotEmpty() ? note : juce::String ("Pick a voice, then load it into Core A or B (its routing, operators and envelopes)."), juce::dontSendNotification);
        info.setFont (juce::Font (juce::FontOptions (11.5f)));
        info.setJustificationType (juce::Justification::topLeft);
        list.setModel (this);
        list.setRowHeight (20);
        list.selectRow (0);
        toA.onClick = [this] { load (0); };
        toB.onClick = [this] { load (1); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &title, &list, &toA, &toB, &info }) addAndMakeVisible (c);
        setSize (360, 470);
    }
    void resized() override
    {
        title.setBounds (10, 6, 340, 20);
        list.setBounds (10, 30, 340, 322);
        toA.setBounds (10, 358, 165, 26); toB.setBounds (185, 358, 165, 26);
        info.setBounds (10, 390, 340, 74);
    }
    int getNumRows() override { return (int) voices.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) voices.size()) return;
        if (selected) g.fillAll (col::accent.withAlpha (0.35f));
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText (juce::String (row + 1).paddedLeft ('0', 2) + "  " + voices[(size_t) row].name + "   (algorithm " + juce::String (voices[(size_t) row].algorithm + 1) + ")", 6, 0, w - 8, h, juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { load (0); }
private:
    void load (int slot)
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) voices.size()) return;
        const auto approx = proc.loadDx7Voice (slot, voices[(size_t) row]);
        const juce::String msg = "Loaded " + voices[(size_t) row].name + " into Core " + (slot == 0 ? "A" : "B") + ". Approximated: " + approx.joinIntoString ("; ") + ".";
        info.setText (msg, juce::dontSendNotification);
        if (onStatus) onStatus ("DX7 voice " + voices[(size_t) row].name + " loaded into Core " + (slot == 0 ? "A" : "B"));
    }
    MegaSynthProcessor& proc;
    std::vector<tg::dx7::Voice> voices;
    std::function<void (const juce::String&)> onStatus;
    juce::Label title, info;
    juce::ListBox list;
    juce::TextButton toA { "Load into Core A" }, toB { "Load into Core B" };
};

} // namespace tgui
