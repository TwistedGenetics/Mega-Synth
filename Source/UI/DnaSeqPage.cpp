#include "DnaSeqPage.h"
#include "../PluginEditor.h"

using namespace tg;

namespace tgui
{

static const juce::Colour kDna = col::wavetable;

static void styleSmall (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, col::panel3);
    b.setColour (juce::TextButton::buttonOnColourId, kDna.withAlpha (0.85f));
}

void TinyButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool t = getToggleState();
    g.setColour (t ? on : (down ? col::panel3.brighter (0.2f) : (over ? col::panel3.brighter (0.1f) : col::panel3)));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (t ? on.brighter (0.3f) : col::border);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (t ? juce::Colours::white : col::muted);
    g.setFont (juce::Font (juce::FontOptions (10.5f, t ? juce::Font::bold : juce::Font::plain)));
    g.drawText (getButtonText(), r, juce::Justification::centred, false);
}

//==============================================================================
DnaCell::DnaCell (MegaSynthProcessor& p, int i, std::function<int()> l) : proc (p), index (i), lane (std::move (l))
{
    for (int t = 0; t < DT_COUNT; ++t) type.addItem (kDnaTransformNames[t], t + 1);
    type.setTooltip ("Transform for this step");
    type.onChange = [this] { const int t = type.getSelectedId() - 1; if (t >= 0) push ([t] (DnaStep& s) { s.type = t; }); };
    amount.setSliderStyle (juce::Slider::LinearBarVertical);
    amount.setRange (0.0, 1.0, 0.01);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amount.setColour (juce::Slider::trackColourId, kDna.withAlpha (0.7f));
    amount.setColour (juce::Slider::backgroundColourId, col::panel3);
    amount.setTooltip ("Amount");
    amount.setPopupDisplayEnabled (true, true, nullptr);
    amount.onValueChange = [this] { const float v = (float) amount.getValue(); push ([v] (DnaStep& s) { s.amount = v; }); };
    prob.setSliderStyle (juce::Slider::LinearBar);
    prob.setRange (0.0, 100.0, 1.0);
    prob.setTextValueSuffix ("%");
    prob.setColour (juce::Slider::trackColourId, col::accent.withAlpha (0.55f));
    prob.setColour (juce::Slider::backgroundColourId, col::panel3);
    prob.setColour (juce::Slider::textBoxTextColourId, col::text);
    prob.setTooltip ("Probability: the chance this step plays each time round");
    prob.onValueChange = [this] { const float v = (float) prob.getValue() / 100.0f; push ([v] (DnaStep& s) { s.prob = v; }); };
    ratchet.setTooltip ("Ratchet: repeats inside the step (click to cycle 1-4)");
    ratchet.onClick = [this] { push ([] (DnaStep& s) { s.ratchet = s.ratchet >= 4 ? 1 : s.ratchet + 1; }); };
    glide.setTooltip ("Glide into the next step: G = the global Glide, G+ = always, G- = never (click to cycle)");
    glide.onClick = [this] { push ([] (DnaStep& s) { s.glide = (s.glide + 1) % 3; }); };
    lock.setTooltip ("Lock: the generator leaves this step alone");
    lock.onClick = [this] { push ([] (DnaStep& s) { s.lock = ! s.lock; }); };
    ratchet.on = kDna.withAlpha (0.85f); glide.on = col::accent.withAlpha (0.8f); lock.on = juce::Colour (0xffd9a441);
    for (auto* c : std::initializer_list<juce::Component*> { &type, &amount, &prob, &ratchet, &glide, &lock }) addAndMakeVisible (c);
    refresh();
}

void DnaCell::push (std::function<void (DnaStep&)> change)
{
    if (updating) return;
    const int pat = proc.dnaEditPattern(), l = lane();
    DnaStep s = proc.dnaSteps.get (pat, l, index);
    change (s);
    proc.dnaSteps.set (pat, l, index, s);
    refresh();
}

void DnaCell::refresh()
{
    const DnaStep s = proc.dnaSteps.get (proc.dnaEditPattern(), lane(), index);
    updating = true;
    type.setSelectedId (s.type + 1, juce::dontSendNotification);
    if (! amount.isMouseButtonDown()) amount.setValue (s.amount, juce::dontSendNotification);
    if (! prob.isMouseButtonDown()) prob.setValue (std::round (s.prob * 100.0f), juce::dontSendNotification);
    ratchet.setButtonText ("x" + juce::String (s.ratchet));
    ratchet.setToggleState (s.ratchet > 1, juce::dontSendNotification);
    glide.setButtonText (s.glide == 0 ? "G" : (s.glide == 1 ? "G+" : "G-"));
    glide.setToggleState (s.glide != 0, juce::dontSendNotification);
    lock.setButtonText (s.lock ? "L" : "-");
    lock.setToggleState (s.lock, juce::dontSendNotification);
    updating = false;
    repaint();
}

void DnaCell::setPlaying (bool p) { if (p != playing) { playing = p; repaint(); } }

void DnaCell::resized()
{
    auto r = getLocalBounds().reduced (3);
    r.removeFromTop (14);
    type.setBounds (r.removeFromTop (22));
    r.removeFromTop (3);
    auto bottom = r.removeFromBottom (20);
    const int bw = bottom.getWidth() / 3;
    ratchet.setBounds (bottom.removeFromLeft (bw).reduced (1, 0));
    glide.setBounds (bottom.removeFromLeft (bw).reduced (1, 0));
    lock.setBounds (bottom.reduced (1, 0));
    r.removeFromBottom (3);
    prob.setBounds (r.removeFromBottom (16));
    r.removeFromBottom (3);
    amount.setBounds (r.withSizeKeepingCentre (std::min (r.getWidth(), 30), r.getHeight()));
}

void DnaCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const DnaStep s = proc.dnaSteps.get (proc.dnaEditPattern(), lane(), index);
    g.setColour (playing ? kDna.withAlpha (0.35f) : (s.lock ? col::panel3 : col::panel2));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (playing ? kDna : (s.lock ? kDna.withAlpha (0.6f) : col::border));
    g.drawRoundedRectangle (r, 6.0f, s.lock ? 1.5f : 1.0f);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (juce::String (index + 1), 0, 1, getWidth(), 14, juce::Justification::centred);
}

//==============================================================================
DnaSeqPanel::DnaSeqPanel (MegaSynthProcessor& p)
    : proc (p),
      pattern (p, P_dsPattern, { "A", "B", "C", "D" }, kDna),
      laneSel (p, -1, { "LANE 1", "LANE 2" }, kDna)
{
    pattern.setTooltip ("Pattern A-D: the one that plays (when Chain is off) and the one you edit");
    laneSel.setTooltip ("Which lane the steps below show. Lane 2 has its own length and runs alongside lane 1.");
    laneSel.onSelect = [this] (int l) { lane = l; refreshAll(); };
    addAndMakeVisible (pattern);
    addAndMakeVisible (laneSel);

    chainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_dsChainOn], chainOn);
    lane2Att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_dsLane2On], lane2On);
    chainOn.setTooltip ("Play the patterns in the chain order, one pattern cycle (lane 1's length) each");
    lane2On.setTooltip ("Lane 2: a second set of steps with its own length (Lane 2 Steps), for polymeter");
    for (auto* t : { &chainOn, &lane2On }) { t->setColour (juce::ToggleButton::tickColourId, kDna); addAndMakeVisible (*t); }
    chain.setText (proc.dnaSteps.getChain(), false);
    chain.setInputRestrictions (8, "ABCDabcd");
    chain.setTooltip ("Chain: up to 8 patterns, e.g. AABA");
    chain.onTextChange = [this] { proc.dnaSteps.setChain (chain.getText()); };
    addAndMakeVisible (chain);
    for (int i = 0; i < kListDsMidi.size; ++i) midiSel.addItem (kListDsMidi.labels[i], i + 1);
    midiAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, kParamIds[P_dsMidiSelect], midiSel);
    midiSel.setTooltip ("Pick the pattern from MIDI: these four notes select A-D (they don't play a sound)");
    addAndMakeVisible (midiSel);

    auto tool = [this] (juce::TextButton& b, MegaSynthProcessor::DnaEdit e, const char* tip)
    {
        b.setTooltip (tip);
        b.onClick = [this, e] { proc.dnaEdit (e, lane); refreshAll(); };
        addAndMakeVisible (b);
    };
    tool (copy, MegaSynthProcessor::DE_Copy, "Copy this lane's 32 steps");
    tool (paste, MegaSynthProcessor::DE_Paste, "Paste the copied steps into this lane");
    tool (left, MegaSynthProcessor::DE_ShiftLeft, "Shift the steps one to the left (within the lane's length)");
    tool (right, MegaSynthProcessor::DE_ShiftRight, "Shift the steps one to the right (within the lane's length)");
    tool (reverse, MegaSynthProcessor::DE_Reverse, "Reverse the steps (within the lane's length)");
    tool (clear, MegaSynthProcessor::DE_Clear, "Clear this lane (locked steps stay)");
    tool (generate, MegaSynthProcessor::DE_Generate, "New seed, new pattern (locked steps stay; undoable)");
    tool (seedDown, MegaSynthProcessor::DE_SeedDown, "Previous seed");
    tool (seedUp, MegaSynthProcessor::DE_SeedUp, "Next seed");
    generate.setColour (juce::TextButton::buttonColourId, kDna.withAlpha (0.6f));

    style.addItem ("Random", 1); style.addItem ("Euclidean", 2); style.addItem ("Variation", 3);
    style.setTooltip ("Random: steps anywhere.  Euclidean: Fill's hits spread evenly (Rotate moves them).  Variation: small changes to the current pattern.");
    style.onChange = [this] { writeGen(); };
    addAndMakeVisible (style);
    auto bar = [this] (juce::Slider& s, const juce::String& name, double lo, double hi, double step, const juce::String& suffix, const char* tip)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (lo, hi, step);
        s.textFromValueFunction = [name, suffix] (double v) { return name + " " + juce::String (juce::roundToInt (v)) + suffix; };
        s.setColour (juce::Slider::trackColourId, kDna.withAlpha (0.5f));
        s.setColour (juce::Slider::backgroundColourId, col::panel3);
        s.setColour (juce::Slider::textBoxTextColourId, col::text);
        s.setTooltip (tip);
        s.onValueChange = [this] { writeGen(); };
        addAndMakeVisible (s);
    };
    bar (fill, "Fill", 0, 100, 1, "%", "How many steps the generator makes active (Euclidean: number of hits)");
    bar (amtMin, "Min", 0, 100, 1, "%", "Lowest step amount");
    bar (amtMax, "Max", 0, 100, 1, "%", "Highest step amount");
    bar (extras, "Extras", 0, 100, 1, "%", "Chance of a ratchet or a probability on an active step");
    bar (change, "Change", 0, 100, 1, "%", "Variation: how much it changes");
    bar (rotate, "Rotate", 0, 31, 1, "", "Euclidean: rotates the hits");
    for (int t = 1; t < DT_COUNT; ++t)
    {
        auto* b = uses.add (new juce::TextButton (kDnaTransformNames[t]));
        b->setClickingTogglesState (true);
        styleSmall (*b);
        b->setTooltip ("Let the generator use " + juce::String (kDnaTransformNames[t]));
        b->onClick = [this] { writeGen(); };
        addAndMakeVisible (b);
    }
    for (auto* l : { &seedLabel, &genCap, &usesCap })
    {
        l->setColour (juce::Label::textColourId, col::muted);
        l->setFont (juce::Font (juce::FontOptions (11.5f)));
        addAndMakeVisible (*l);
    }
    seedLabel.setJustificationType (juce::Justification::centred);
    seedLabel.setColour (juce::Label::textColourId, col::text);
    genCap.setText ("Generator", juce::dontSendNotification);
    usesCap.setText ("uses:", juce::dontSendNotification);

    for (int i = 0; i < DnaSeqStore::kSteps; ++i)
    {
        auto* c = cells.add (new DnaCell (proc, i, [this] { return lane; }));
        addAndMakeVisible (c);
    }
    readGen();
    startTimerHz (15);
}

void DnaSeqPanel::readGen()
{
    const auto& g = proc.dnaSteps.gen;
    style.setSelectedId (g.style + 1, juce::dontSendNotification);
    fill.setValue (std::round (g.fill * 100), juce::dontSendNotification);
    amtMin.setValue (std::round (g.amtMin * 100), juce::dontSendNotification);
    amtMax.setValue (std::round (g.amtMax * 100), juce::dontSendNotification);
    extras.setValue (std::round (g.extras * 100), juce::dontSendNotification);
    change.setValue (std::round (g.change * 100), juce::dontSendNotification);
    rotate.setValue (g.rotate, juce::dontSendNotification);
    for (int t = 1; t < DT_COUNT; ++t) uses[t - 1]->setToggleState (g.mask == 0 || (g.mask & (1u << t)) != 0, juce::dontSendNotification);
    seedLabel.setText ("#" + juce::String (g.seed), juce::dontSendNotification);
    change.setVisible (g.style == DG_Variation);
    rotate.setVisible (g.style == DG_Euclid);
    fill.setVisible (g.style != DG_Variation);
}

void DnaSeqPanel::writeGen()
{
    auto& g = proc.dnaSteps.gen;
    g.style = juce::jlimit (0, 2, style.getSelectedId() - 1);
    g.fill = (float) fill.getValue() / 100.0f;
    g.amtMin = (float) amtMin.getValue() / 100.0f;
    g.amtMax = (float) amtMax.getValue() / 100.0f;
    g.extras = (float) extras.getValue() / 100.0f;
    g.change = (float) change.getValue() / 100.0f;
    g.rotate = (int) rotate.getValue();
    uint32_t m = 0; bool all = true;
    for (int t = 1; t < DT_COUNT; ++t) { if (uses[t - 1]->getToggleState()) m |= 1u << t; else all = false; }
    g.mask = all ? 0u : (m == 0 ? (1u << DT_Fold) : m);
    readGen();
}

void DnaSeqPanel::refreshAll()
{
    for (auto* c : cells) c->refresh();
    readGen();
    if (! chain.hasKeyboardFocus (true)) chain.setText (proc.dnaSteps.getChain(), false);
    lastVersion = proc.dnaSteps.getVersion();
}

void DnaSeqPanel::timerCallback()
{
    if (! isShowing()) return;
    const int pat = proc.dnaEditPattern();
    if (proc.dnaSteps.getVersion() != lastVersion || pat != lastPattern || lane != lastLane)
    {
        lastPattern = pat; lastLane = lane;
        refreshAll();
    }
    pattern.repaint();
    const int step = proc.getDnaPatternPlaying() == pat ? (lane == 0 ? proc.getDnaSeqStep() : proc.getDnaSeqStep2()) : -1;
    if (step != lastStep) { lastStep = step; for (int i = 0; i < cells.size(); ++i) cells[i]->setPlaying (i == step); }
    const int len = proc.dnaLaneLength (lane);
    if (len != lastLen) { lastLen = len; for (int i = 0; i < cells.size(); ++i) cells[i]->setAlpha (i < len ? 1.0f : 0.35f); }
    paste.setEnabled (proc.dnaHasClipboard());
}

void DnaSeqPanel::paint (juce::Graphics& g)
{
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText ("Pattern", 0, 0, 60, 30, juce::Justification::centredLeft);
    g.drawText ("Edit", 650, 0, 40, 30, juce::Justification::centredLeft);
    g.setColour (col::border);
    g.drawVerticalLine (640, 2.0f, 30.0f);
    g.drawHorizontalLine (40, 0.0f, (float) getWidth());
}

void DnaSeqPanel::resized()
{
    // row 1: pattern, chain, MIDI select | lane being edited, lane 2 on
    pattern.setBounds (56, 2, 168, 28);
    chainOn.setBounds (236, 4, 70, 24);
    chain.setBounds (306, 4, 90, 24);
    midiSel.setBounds (406, 4, 226, 24);
    laneSel.setBounds (690, 2, 190, 28);
    lane2On.setBounds (892, 4, 100, 24);
    // row 2: tools | generator
    int x = 0;
    for (auto* b : { &copy, &paste, &left, &right, &reverse, &clear }) { const int w = b == &left || b == &right ? 34 : 62; b->setBounds (x, 48, w, 26); x += w + 4; }
    x += 14;
    genCap.setBounds (x, 48, 64, 26); x += 64;
    style.setBounds (x, 48, 100, 26); x += 106;
    fill.setBounds (x, 48, 92, 26); change.setBounds (x, 48, 92, 26); x += 96;
    amtMin.setBounds (x, 48, 82, 26); x += 86;
    amtMax.setBounds (x, 48, 82, 26); x += 86;
    extras.setBounds (x, 48, 96, 26); x += 100;
    rotate.setBounds (x, 48, 86, 26); x += 90;
    seedDown.setBounds (x, 48, 26, 26); x += 28;
    seedLabel.setBounds (x, 48, 64, 26); x += 66;
    seedUp.setBounds (x, 48, 26, 26); x += 30;
    generate.setBounds (x, 48, getWidth() - x, 26);
    // row 3: the transforms the generator may use
    usesCap.setBounds (334, 80, 40, 22);
    x = 374;
    const int cw = (getWidth() - x) / (DT_COUNT - 1);
    for (auto* b : uses) { b->setBounds (x, 80, cw - 3, 22); x += cw; }
    // the steps: two rows of 16
    const int top = 110, cellW = getWidth() / 16, cellH = (getHeight() - top - 6) / 2;
    for (int i = 0; i < cells.size(); ++i) cells[i]->setBounds ((i % 16) * cellW, top + (i / 16) * (cellH + 6), cellW - 3, cellH);
}

} // namespace tgui
