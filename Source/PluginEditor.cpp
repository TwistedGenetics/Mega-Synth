#include "PluginEditor.h"
#include "ParamFormat.h"

using namespace tg;
using namespace tgui;

static constexpr int kDesignW = 1200;
static constexpr int kDesignH = 860;

//==============================================================================
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::panel2);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::border);
    setColour (juce::ComboBox::arrowColourId, col::muted);
    setColour (juce::PopupMenu::backgroundColourId, col::panel2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::TextButton::buttonColourId, col::panel3);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::text);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ToggleButton::tickColourId, col::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, col::muted);
    setColour (juce::Slider::textBoxTextColourId, col::muted);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::accent.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, col::panel2);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TabbedComponent::backgroundColourId, col::bg);
    setColour (juce::TabbedComponent::outlineColourId, col::border);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, col::border);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfff7f8fa));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff12171d));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour (0xff3f83b7).withAlpha (0.75f));
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour (0xffc6e6ff).withAlpha (0.5f));
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lineW = 3.5f;
    const float arcR = radius - lineW * 0.5f;
    const juce::Colour fill = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (col::panel3);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // bipolar sliders draw from the centre
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = start + zeroPos * (end - start);
    const float a1 = start + pos * (end - start);
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
    g.setColour (fill);
    g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (col::panel2);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.25f, radius * 1.25f).withCentre (c));
    const juce::Point<float> tip (c.x + (radius * 0.55f) * std::sin (a1), c.y - (radius * 0.55f) * std::cos (a1));
    g.setColour (col::text);
    g.drawLine (c.x, c.y, tip.x, tip.y, 2.0f);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (col::muted);
    g.fillPath (arrow);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - 18, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = base;
    if (down) c = c.brighter (0.25f); else if (over) c = c.brighter (0.12f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
}

void Look::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (3.0f, 3.0f);
    const bool front = b.isFrontTab();
    g.setColour (front ? col::panel3 : (over ? col::panel2.brighter (0.05f) : col::panel2));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (front ? col::accent : col::border);
    g.drawRoundedRectangle (r, 7.0f, front ? 2.0f : 1.0f);
    g.setColour (front ? col::text : col::muted);
    g.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::bold)));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
Knob::Knob (MegaSynthProcessor& p, int idx, const juce::String& cap, juce::Colour c)
{
    slider.setColour (juce::Slider::textBoxTextColourId, col::muted);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 15);
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    addAndMakeVisible (slider);
    caption.setText (cap, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, col::muted);
    caption.setFont (juce::Font (juce::FontOptions (11.5f)));
    caption.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (caption);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kParamIds[idx], slider);
    slider.setDoubleClickReturnValue (true, (double) p.param (idx)->convertFrom0to1 (p.param (idx)->getDefaultValue()));
    slider.setTooltip (p.param (idx)->getName (64));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (15));
    slider.setBounds (r);
}

Choice::Choice (MegaSynthProcessor& p, int idx, const juce::String& cap)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.param (idx)))
        box.addItemList (c->choices, 1);
    addAndMakeVisible (box);
    caption.setText (cap, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, col::muted);
    caption.setFont (juce::Font (juce::FontOptions (11.5f)));
    addAndMakeVisible (caption);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, kParamIds[idx], box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (16));
    box.setBounds (r.removeFromTop (24));
}

//==============================================================================
Section::Section (MegaSynthProcessor& p, const juce::String& t, juce::Colour c) : colour (c), proc (p), title (t) {}

Knob* Section::knob (int idx, const juce::String& cap, int width)
{
    auto* k = new Knob (proc, idx, cap, colour);
    add (k, width, 84);
    return k;
}

Choice* Section::choice (int idx, const juce::String& cap, int width)
{
    auto* c = new Choice (proc, idx, cap);
    add (c, width, 44);
    return c;
}

void Section::add (juce::Component* c, int w, int h)
{
    owned.add (c);
    addAndMakeVisible (c);
    items.push_back ({ c, w, h, pendingBreak });
    pendingBreak = false;
}

void Section::newRow() { pendingBreak = true; }

void Section::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 12, r.getY() + 9, 4, 16), 2.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText (title, juce::Rectangle<float> (r.getX() + 22, r.getY() + 6, r.getWidth() - 30, 22), juce::Justification::centredLeft);
}

void Section::resized()
{
    const int pad = 10, gap = 6, top = 32;
    int x = pad, y = top, rowH = 0;
    for (auto& it : items)
    {
        if (it.rowBreak || (x > pad && x + it.w > getWidth() - pad))
        {
            x = pad; y += rowH + gap; rowH = 0;
        }
        it.c->setBounds (x, y, it.w, it.h);
        x += it.w + gap;
        rowH = std::max (rowH, it.h);
    }
}

//==============================================================================
AssignSlot::AssignSlot (MegaSynthProcessor& p, int firstParam, const juce::String& titleText, juce::Colour c)
    : proc (p), first (firstParam), title (titleText), colour (c)
{
    if (auto* sc = dynamic_cast<juce::AudioParameterChoice*> (p.param (first)))
        source.addItemList (sc->choices, 1);
    for (int i = 0; i < MT_COUNT; ++i) target.addItem (kTargetLabels[i], i + 1);
    addAndMakeVisible (source);
    addAndMakeVisible (target);
    srcAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, kParamIds[first], source);

    // Choosing a new target from the menu resets the amount to that target's default depth,
    // like the browser version. Host automation of the target leaves the amount alone.
    target.onChange = [this]
    {
        const int t = target.getSelectedItemIndex();
        if (t < 0) return;
        auto* tp = proc.param (first + 1);
        auto* ap = proc.param (first + 2);
        tp->beginChangeGesture(); tp->setValueNotifyingHost (tp->convertTo0to1 ((float) t)); tp->endChangeGesture();
        ap->beginChangeGesture(); ap->setValueNotifyingHost (ap->convertTo0to1 (kTargetDefault[t] / kTargetRange[t])); ap->endChangeGesture();
        refresh();
    };

    amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amount.setColour (juce::Slider::rotarySliderFillColourId, colour);
    addAndMakeVisible (amount);
    amtAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kParamIds[first + 2], amount);
    amount.setDoubleClickReturnValue (true, 0.0);
    amount.onValueChange = [this] { refresh(); };
    amountText.setJustificationType (juce::Justification::centred);
    amountText.setColour (juce::Label::textColourId, col::muted);
    amountText.setFont (juce::Font (juce::FontOptions (11.5f)));
    addAndMakeVisible (amountText);
    refresh();
}

void AssignSlot::refresh()
{
    const int t = juce::jlimit (0, (int) MT_COUNT - 1, (int) proc.param (first + 1)->convertFrom0to1 (proc.param (first + 1)->getValue()));
    if (target.getSelectedItemIndex() != t) target.setSelectedItemIndex (t, juce::dontSendNotification);
    amountText.setText (formatModAmount (t, (float) amount.getValue()), juce::dontSendNotification);
    amount.setEnabled (t != MT_none);
}

void AssignSlot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r, 9.0f, 1.0f);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText (title, 10, 4, 120, 16, juce::Justification::centredLeft);
}

void AssignSlot::resized()
{
    auto r = getLocalBounds().reduced (8, 4);
    r.removeFromTop (16);
    auto knob = r.removeFromRight (70);
    amount.setBounds (knob.removeFromTop (r.getHeight() - 14).withSizeKeepingCentre (44, 44));
    amountText.setBounds (knob);
    r.removeFromRight (6);
    source.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    target.setBounds (r.removeFromTop (24));
}

//==============================================================================
StepCell::StepCell (MegaSynthProcessor& p, int i) : proc (p), index (i)
{
    note.addItem ("REST", 1);
    for (int n = 0; n < 12; ++n) note.addItem (kNoteKeys[n], n + 2);
    for (int o = -2; o <= 2; ++o) oct.addItem ((o > 0 ? "+" : "") + juce::String (o), o + 3);
    tie.addItem ("Norm", 1); tie.addItem ("Tie", 2); tie.addItem ("Slide", 3); tie.addItem ("Rest", 4);
    note.setTooltip ("Note"); oct.setTooltip ("Octave"); tie.setTooltip ("Normal / Tie / Slide / Rest");
    for (auto* c : { &note, &oct, &tie })
    {
        c->onChange = [this] { push(); };
        addAndMakeVisible (*c);
    }
    accent.onClick = [this] { push(); };
    accent.setColour (juce::ToggleButton::tickColourId, col::seq);
    addAndMakeVisible (accent);
    refresh();
}

void StepCell::push()
{
    Step s;
    s.note = note.getSelectedId() - 2;
    s.oct = oct.getSelectedId() - 3;
    s.tie = juce::jmax (0, tie.getSelectedId() - 1);
    s.accent = accent.getToggleState();
    proc.steps.set (index, s);
}

void StepCell::refresh()
{
    const Step s = proc.steps.get (index);
    note.setSelectedId (s.note + 2, juce::dontSendNotification);
    oct.setSelectedId (s.oct + 3, juce::dontSendNotification);
    tie.setSelectedId (s.tie + 1, juce::dontSendNotification);
    accent.setToggleState (s.accent, juce::dontSendNotification);
}

void StepCell::setPlaying (bool p)
{
    if (p != playing) { playing = p; repaint(); }
}

void StepCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (playing ? col::seq.withAlpha (0.28f) : col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (playing ? col::seq : col::border);
    g.drawRoundedRectangle (r, 8.0f, playing ? 2.0f : 1.0f);
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (juce::String (index + 1), 6, 3, 30, 14, juce::Justification::centredLeft);
}

void StepCell::resized()
{
    auto r = getLocalBounds().reduced (4);
    r.removeFromTop (15);
    for (auto* c : { &note, &oct, &tie }) { c->setBounds (r.removeFromTop (22)); r.removeFromTop (3); }
    accent.setBounds (r.removeFromTop (22));
}

//==============================================================================
MegaSynthEditor::MegaSynthEditor (MegaSynthProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);

    title.setText ("MEGA SYNTH", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    subtitle.setText ("Twisted Genetics  -  6 oscillators, FM + ring matrix, 17 filters, sequencer", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, col::muted);
    subtitle.setFont (juce::Font (juce::FontOptions (12.0f)));
    for (auto* l : { &title, &subtitle, &status, &octLabel }) content.addAndMakeVisible (*l);
    status.setColour (juce::Label::textColourId, col::accent);
    status.setFont (juce::Font (juce::FontOptions (12.0f)));
    status.setJustificationType (juce::Justification::centredRight);
    octLabel.setJustificationType (juce::Justification::centred);
    octLabel.setColour (juce::Label::textColourId, col::muted);

    const std::pair<int, const char*> hk[] = { { P_masterVolume, "Master" }, { P_polyphony, "Polyphony" }, { P_porta, "Portamento" },
                                               { P_velSens, "Velocity" }, { P_bendRange, "Bend Range" } };
    for (auto& [idx, name] : hk)
    {
        auto* k = headerKnobs.add (new Knob (proc, idx, name, col::master));
        content.addAndMakeVisible (k);
    }

    for (auto* b : { &copyBtn, &pasteBtn, &initBtn, &octDown, &octUp }) content.addAndMakeVisible (*b);
    subtitle.setVisible (false);

    // ---- patch browser
    for (auto* b : { &patchPrev, &patchNext, &saveBtn }) content.addAndMakeVisible (*b);
    content.addAndMakeVisible (patchBox);
    patchBox.setTooltip ("Saved patches (Music/Mega Synth/Patches)");
    patchBox.onChange = [this]
    {
        const int id = patchBox.getSelectedId();
        if (id >= 1 && id <= patchFiles.size()) { loadPatch (patchFiles[id - 1]); return; }
        if (id == 9001)
        {
            chooser = std::make_unique<juce::FileChooser> ("Open a Mega Synth patch", MegaSynthProcessor::getPatchFolder(), "*.megasynth;*.json;*.txt");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f != juce::File()) loadPatch (f);
                                      refreshPatchList();
                                  });
        }
        else if (id == 9002) MegaSynthProcessor::getPatchFolder().revealToUser();
        refreshPatchList();
    };
    patchPrev.onClick = [this] { stepPatch (-1); };
    patchNext.onClick = [this] { stepPatch (1); };
    saveBtn.onClick = [this]
    {
        juce::String name = proc.getPatchName();
        if (name.isEmpty() || name == "Init" || name == "Imported patch") name = "My Patch";
        chooser = std::make_unique<juce::FileChooser> ("Save patch", MegaSynthProcessor::getPatchFolder().getChildFile (juce::File::createLegalFileName (name) + ".megasynth"), "*.megasynth");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  f = f.withFileExtension (".megasynth");
                                  setStatus (proc.savePatchToFile (f) ? "Saved " + f.getFileNameWithoutExtension() : "Couldn't save the patch there");
                                  refreshPatchList();
                              });
    };
    refreshPatchList();

    copyBtn.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (proc.exportBrowserPatch());
        setStatus ("Patch copied - it also pastes into the browser version's Load Patch");
    };
    pasteBtn.onClick = [this]
    {
        const auto err = proc.importBrowserPatch (juce::SystemClipboard::getTextFromClipboard());
        setStatus (err.isEmpty() ? "Patch loaded from clipboard" : err);
    };
    initBtn.onClick = [this]
    {
        juce::NativeMessageBox::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Init patch",
            "Reset every control, the sequence and the sample to defaults?", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) { proc.resetToDefaults(); setStatus ("Patch reset"); } }));
    };
    octDown.onClick = [this] { shiftOctave (-1); };
    octUp.onClick = [this] { shiftOctave (1); };

    tabs.setTabBarDepth (36);
    tabs.setOutline (0);
    content.addAndMakeVisible (tabs);
    buildPages();

    keyboard.setAvailableRange (36, 96);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setScrollButtonsVisible (false);
    content.addAndMakeVisible (keyboard);

    setResizable (true, true);
    setResizeLimits (kDesignW * 2 / 3, kDesignH * 2 / 3, kDesignW * 2, kDesignH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kDesignW / kDesignH);
    setSize (kDesignW * 9 / 10, kDesignH * 9 / 10);   // starts at 90% so it fits laptop screens; drag the corner to resize
    // Inside a DAW, never take the computer keyboard: clicking a control would
    // otherwise steal keys from the host's own QWERTY keyboard (e.g. Live's
    // Computer MIDI Keyboard). The standalone app keeps QWERTY note entry.
    const bool standalone = proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    std::function<void (juce::Component&)> setFocus = [&] (juce::Component& c)
    {
        c.setWantsKeyboardFocus (standalone);
        c.setMouseClickGrabsKeyboardFocus (standalone);
        for (auto* ch : c.getChildren()) setFocus (*ch);
    };
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* page = tabs.getTabContentComponent (i)) setFocus (*page);
    setFocus (content);
    setWantsKeyboardFocus (standalone);
    setMouseClickGrabsKeyboardFocus (standalone);
    startTimerHz (30);
}

MegaSynthEditor::~MegaSynthEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MegaSynthEditor::buildPages()
{
    auto addPage = [this] (const juce::String& name) -> Page*
    {
        auto* page = new Page();
        tabs.addTab (name, col::bg, page, true);
        return page;
    };
    auto sec = [this] (Page* page, const juce::String& t, juce::Colour c) { return page->own (new Section (proc, t, c)); };

    // ---------------------------------------------------------------- Oscillators
    {
        auto* page = addPage ("Oscillators");
        Section* o[3];
        for (int i = 0; i < 3; ++i)
        {
            o[i] = sec (page, "Oscillator " + juce::String (i + 1), col::osc);
            o[i]->choice (P_osc1Wave + 3 * i, "Waveform", 254);
            o[i]->newRow();
            o[i]->knob (P_osc1Detune + 3 * i, "Detune");
            o[i]->knob (P_osc1Oct + 3 * i, "Octave");
            o[i]->knob (P_osc1Gain + i, "Level");
        }
        auto* sub = sec (page, "Sub Oscillator", col::osc);
        sub->choice (P_subWave, "Waveform", 254);
        sub->newRow();
        sub->knob (P_subOct, "Octave");
        sub->knob (P_subGain, "Level");

        // the two sample / wavetable oscillators
        struct WtIds { int mode, dir, norm, det, oct, root, pos, win, ls, le, gain; };
        const WtIds ids[2] = {
            { P_osc4LoopMode, P_osc4Direction, P_osc4Normalize, P_osc4Detune, P_osc4Oct, P_osc4Root, P_osc4Position, P_osc4Window, P_osc4LoopStart, P_osc4LoopEnd, P_osc4Gain },
            { P_wt2LoopMode, P_wt2Direction, P_wt2Normalize, P_wt2Detune, P_wt2Oct, P_wt2Root, P_wt2Position, P_wt2Window, P_wt2LoopStart, P_wt2LoopEnd, P_wt2Gain } };
        Section* wtS[2];
        for (int k = 0; k < 2; ++k)
        {
            wtS[k] = sec (page, k == 0 ? "Wavetable 1  (Osc 4)" : "Wavetable 2", col::wavetable);
            auto* bar = new SampleBar();
            sampleBars[k] = bar;
            wtS[k]->add (bar, 545, 28);
            bar->load.onClick = [this, k]
            {
                chooser = std::make_unique<juce::FileChooser> ("Load a sample for Wavetable " + juce::String (k + 1), juce::File(),
                                                               "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this, k] (const juce::FileChooser& fc)
                                      {
                                          const auto f = fc.getResult();
                                          if (f == juce::File()) return;
                                          setStatus (proc.loadSampleFile (f, k) ? "Sample loaded into Wavetable " + juce::String (k + 1)
                                                                               : "Couldn't read that audio file");
                                      });
            };
            bar->clear.onClick = [this, k] { proc.clearSample (k); setStatus ("Wavetable " + juce::String (k + 1) + " cleared"); };
            wtS[k]->newRow();
            wtS[k]->choice (ids[k].mode, "Loop Mode", 180);
            wtS[k]->choice (ids[k].dir, "Direction", 110);
            wtS[k]->choice (ids[k].norm, "Normalize", 110);
            wtS[k]->newRow();
            for (auto [idx, n] : { std::pair<int, const char*> { ids[k].det, "Detune" }, { ids[k].oct, "Octave" }, { ids[k].root, "Root Note" },
                                   { ids[k].pos, "Scan Pos" }, { ids[k].win, "Window" }, { ids[k].ls, "Loop Start" },
                                   { ids[k].le, "Loop End" }, { ids[k].gain, "Level" } })
                wtS[k]->knob (idx, n, 62);
        }

        auto* cx = sec (page, "Oscillator 5 / Complex", col::complex);
        cx->choice (P_complexWaveA, "Primary Wave A", 170);
        cx->choice (P_complexWaveB, "Mod Wave B", 170);
        cx->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_complexDetune, "Detune" }, { P_complexOct, "Octave" }, { P_complexRatio, "Mod Ratio" },
                               { P_complexFm, "FM Index" }, { P_complexShape, "Wavefold" }, { P_complexMix, "A/B Blend" }, { P_complexGain, "Level" } })
            cx->knob (idx, n);

        auto* ss = sec (page, "Oscillator 6 / Unison SuperSaw", col::supersaw);
        for (auto [idx, n] : { std::pair<int, const char*> { P_supersawDetune, "Detune" }, { P_supersawOct, "Octave" }, { P_supersawVoices, "Voices" },
                               { P_supersawSpread, "Spread" }, { P_supersawStereo, "Stereo" }, { P_supersawDrift, "Drift" }, { P_supersawGain, "Level" } })
            ss->knob (idx, n);

        page->onResize = [page, o, sub, wtS, cx, ss]
        {
            const int W = page->getWidth(), g = 10;
            const int w4 = (W - g * 5) / 4, w2 = (W - g * 3) / 2;
            for (int i = 0; i < 3; ++i) o[i]->setBounds (g + i * (w4 + g), g, w4, 170);
            sub->setBounds (g + 3 * (w4 + g), g, w4, 170);
            wtS[0]->setBounds (g, 190, w2, 208);
            wtS[1]->setBounds (g * 2 + w2, 190, w2, 208);
            cx->setBounds (g, 408, w2, 180);
            ss->setBounds (g * 2 + w2, 408, w2, 180);
        };
    }

    // ---------------------------------------------------------------- Mixer & FM
    {
        auto* page = addPage ("Mixer & Routing");
        auto* lv = sec (page, "Oscillator Levels", col::mixer);
        for (auto [idx, n] : { std::pair<int, const char*> { P_osc1Gain, "Osc 1" }, { P_osc2Gain, "Osc 2" }, { P_osc3Gain, "Osc 3" }, { P_subGain, "Sub" },
                               { P_osc4Gain, "WT 1" }, { P_wt2Gain, "WT 2" }, { P_complexGain, "Complex" }, { P_supersawGain, "SuperSaw" }, { P_masterVolume, "Master" } })
            lv->knob (idx, n, 56);
        auto* rt = sec (page, "FM / Ring / FX Return Levels", col::mixer);
        for (auto [idx, n] : { std::pair<int, const char*> { P_fmAmount, "FM 2>1" }, { P_ringMix, "Ring Mix" }, { P_ringGain, "Ring Out" },
                               { P_delayMix, "Delay" }, { P_reverbMix, "Reverb" }, { P_shimmerMix, "Shimmer" }, { P_chorusMix, "Chorus" }, { P_reverseMix, "Reverse" } })
            rt->knob (idx, n, 62);
        auto* fmS = sec (page, "FM Matrix  (source -> destination, Hz of deviation)", col::fm);
        for (int i = 0; i < 4; ++i)
        {
            fmS->choice (P_fmSlot1Source + 3 * i, "Slot " + juce::String (i + 1) + " Source", 120);
            fmS->choice (P_fmSlot1Dest + 3 * i, "Destination", 120);
            fmS->knob (P_fmSlot1Amt + 3 * i, "Amount");
            if (i % 2 == 1) fmS->newRow();
        }
        auto* rg = sec (page, "Ring Mod Matrix", col::fm);
        for (int i = 0; i < 2; ++i)
        {
            rg->choice (P_ringSlot1Source + 3 * i, "Slot " + juce::String (i + 1) + " Source", 120);
            rg->choice (P_ringSlot1Dest + 3 * i, "Carrier", 140);
            rg->knob (P_ringSlot1Amt + 3 * i, "Amount");
            rg->newRow();
        }
        page->onResize = [page, lv, rt, fmS, rg]
        {
            const int W = page->getWidth(), g = 10, w2 = (W - g * 3) / 2;
            lv->setBounds (g, g, w2, 136);
            rt->setBounds (g * 2 + w2, g, w2, 136);
            fmS->setBounds (g, 156, 720, 220);
            rg->setBounds (g * 2 + 720, 156, W - 3 * g - 720, 220);
        };
    }

    // ---------------------------------------------------------------- Filter & Envelopes
    {
        auto* page = addPage ("Filter & Env");
        auto* f = sec (page, "Filter", col::filter);
        f->choice (P_filterMode, "Filter Type", 230);
        f->newRow();
        f->knob (P_filterCutoff, "Cutoff");
        f->knob (P_filterRes, "Resonance");
        f->knob (P_filterDrive, "Drive");
        auto* a = sec (page, "Amp Envelope", col::env);
        for (auto [idx, n] : { std::pair<int, const char*> { P_ampA, "Attack" }, { P_ampD, "Decay" }, { P_ampS, "Sustain" }, { P_ampR, "Release" } })
            a->knob (idx, n);
        auto* fe = sec (page, "Filter Envelope", col::env);
        for (auto [idx, n] : { std::pair<int, const char*> { P_fEnvAmt, "Amount" }, { P_fEnvA, "Attack" }, { P_fEnvD, "Decay" }, { P_fEnvS, "Sustain" }, { P_fEnvR, "Release" } })
            fe->knob (idx, n);
        auto* w = sec (page, "Analog Warmth", col::supersaw);
        w->knob (P_warmth, "Warmth");
        w->knob (P_bassKeep, "Bass Keep");
        w->knob (P_analogDrift, "Drift");
        auto* wInfo = new juce::Label ({}, "Warmth: soft, rounder saturation instead of hard clipping, plus a gentle low lift and smoother top.  "
                                           "Bass Keep: holds onto low end in filter types with high-pass or band-pass stages.  "
                                           "Drift: each oscillator wanders slightly and starts free-running.  All at zero = the browser's exact sound.");
        wInfo->setColour (juce::Label::textColourId, col::muted);
        wInfo->setFont (juce::Font (juce::FontOptions (12.0f)));
        wInfo->setJustificationType (juce::Justification::topLeft);
        w->add (wInfo, 470, 84);
        page->onResize = [page, f, a, fe, w]
        {
            const int g = 10;
            f->setBounds (g, g, 260, 210);
            a->setBounds (280, g, 330, 140);
            fe->setBounds (620, g, 410, 140);
            w->setBounds (280, 160, 750, 130);
        };
    }

    // ---------------------------------------------------------------- Effects
    {
        auto* page = addPage ("Effects");
        auto* d = sec (page, "Tape / BBD Delay", col::fx);
        d->choice (P_delaySync, "Delay Sync", 150);
        d->choice (P_flutterSync, "Flutter Sync", 150);
        d->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_delayTime, "Time" }, { P_delayFeedback, "Feedback" }, { P_tapeTone, "Tone" },
                               { P_tapeFlutter, "Flutter" }, { P_delayMix, "Return" } })
            d->knob (idx, n);
        auto* r = sec (page, "90s Reverb + Shimmer", col::fx);
        r->choice (P_reverbSync, "Reverb / Shimmer Sync", 170);
        r->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_reverbSize, "Size" }, { P_reverbTone, "Tone" }, { P_shimmerBright, "Shimmer Bright" },
                               { P_reverbMix, "Reverb" }, { P_shimmerMix, "Shimmer" } })
            r->knob (idx, n);
        auto* c = sec (page, "Juno Chorus", col::fx);
        c->choice (P_chorusSync, "Chorus Sync", 150);
        c->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_chorusRate, "Rate" }, { P_chorusDepth, "Depth" }, { P_chorusMix, "Return" } })
            c->knob (idx, n);
        auto* v = sec (page, "Reverse Pitch Shifter Reverb", col::fx);
        v->choice (P_reverseSync, "Reverse Sync", 150);
        v->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_reverseTime, "Time" }, { P_reversePitch, "Pitch Drift" }, { P_reverseMix, "Return" } })
            v->knob (idx, n);
        auto* note = page->own (new juce::Label ({}, "Synced times follow the host tempo (or the sequencer tempo when the host doesn't send one)."));
        note->setColour (juce::Label::textColourId, col::muted);
        note->setFont (juce::Font (juce::FontOptions (12.0f)));
        page->onResize = [page, d, r, c, v, note]
        {
            const int g = 10, W = page->getWidth(), w2 = (W - g * 3) / 2;
            d->setBounds (g, g, w2, 200);
            r->setBounds (g * 2 + w2, g, w2, 200);
            c->setBounds (g, 220, w2, 200);
            v->setBounds (g * 2 + w2, 220, w2, 200);
            note->setBounds (g, 430, W - 2 * g, 20);
        };
    }

    // ---------------------------------------------------------------- Modulation
    {
        auto* page = addPage ("Modulation");
        Section* lfo[3]; Section* env[3];
        for (int i = 0; i < 3; ++i)
        {
            lfo[i] = sec (page, "LFO " + juce::String (i + 1), col::mod);
            lfo[i]->choice (P_lfo1Wave + 3 * i, "Wave", 110);
            lfo[i]->knob (P_lfo1Rate + 3 * i, "Rate");
            lfo[i]->knob (P_lfo1Depth + 3 * i, "Depth");
            env[i] = sec (page, "Mod Envelope " + juce::String (i + 1), col::env);
            env[i]->knob (P_mEnv1A + 4 * i, "Attack");
            env[i]->knob (P_mEnv1D + 4 * i, "Decay");
            env[i]->knob (P_mEnv1S + 4 * i, "Sustain");
            env[i]->knob (P_mEnv1R + 4 * i, "Release");
        }
        auto* asg = sec (page, "Assignments  (pick a target, then set the depth; double-click a knob to zero it)", col::mod);
        for (int i = 0; i < 3; ++i)
            assignSlots.add (page->own (new AssignSlot (proc, P_lfoAssignSource0 + 3 * i, "LFO SLOT " + juce::String (i + 1), col::mod)));
        for (int i = 0; i < 3; ++i)
            assignSlots.add (page->own (new AssignSlot (proc, P_envAssignSource0 + 3 * i, "ENV SLOT " + juce::String (i + 1), col::env)));
        auto* mw = page->own (new juce::Label ({}, "Mod wheel (CC1) sets LFO 1 depth. CC7 sets master volume."));
        mw->setColour (juce::Label::textColourId, col::muted);
        mw->setFont (juce::Font (juce::FontOptions (12.0f)));
        juce::Array<AssignSlot*> sl (assignSlots);
        page->onResize = [page, lfo, env, asg, sl, mw]
        {
            const int g = 10, W = page->getWidth();
            const int w3 = (W - g * 4) / 3;
            for (int i = 0; i < 3; ++i)
            {
                lfo[i]->setBounds (g + i * (w3 + g), g, w3, 130);
                env[i]->setBounds (g + i * (w3 + g), 148, w3, 130);
            }
            asg->setBounds (g, 286, W - 2 * g, 196);
            const int sw = (W - 2 * g - 20 - 2 * 8) / 3;
            for (int i = 0; i < 6; ++i)
            {
                const int row = i / 3, c = i % 3;
                sl[i]->setBounds (g + 10 + c * (sw + 8), 286 + 30 + row * 78, sw, 74);
                sl[i]->toFront (false);
            }
            mw->setBounds (g, 484, W - 2 * g, 18);
        };
    }

    // ---------------------------------------------------------------- Sequencer
    {
        auto* page = addPage ("Sequencer");
        auto* c = sec (page, "Sequencer", col::seq);
        seqRunAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_seqRun], seqRunBtn);
        seqRunBtn.setColour (juce::ToggleButton::tickColourId, col::seq);
        seqRandomBtn.onClick = [this] { proc.generateRandomPhrase(); };
        auto* buttons = new juce::Component();   // holds Run + Random at the start of the section's first row
        buttons->addAndMakeVisible (seqRunBtn);
        buttons->addAndMakeVisible (seqRandomBtn);
        seqRunBtn.setBounds (0, 4, 136, 24);
        seqRandomBtn.setBounds (0, 36, 136, 28);
        c->add (buttons, 136, 80);
        c->choice (P_seqClock, "Clock", 140);
        c->choice (P_seqGenMode, "Generator Mode", 120);
        c->choice (P_seqScaleRoot, "Scale Root", 70);
        c->choice (P_seqScaleType, "Scale Type", 150);
        for (auto [idx, n] : { std::pair<int, const char*> { P_seqTempo, "Tempo" }, { P_seqGate, "Gate" }, { P_seqLength, "Length" },
                               { P_seqAccentAmt, "Accent" }, { P_seqEuclidPulses, "Euclid Pulses" }, { P_seqEuclidRotate, "Euclid Rotate" } })
            c->knob (idx, n);

        for (int i = 0; i < 32; ++i) stepCells.add (page->own (new StepCell (proc, i)));
        juce::Array<StepCell*> cells (stepCells);

        page->onResize = [page, c, cells]
        {
            const int g = 10, W = page->getWidth();
            c->setBounds (g, g, W - 2 * g, 130);
            const int cw = (W - 2 * g) / 16;
            for (int i = 0; i < 32; ++i)
                cells[i]->setBounds (g + (i % 16) * cw, 150 + (i / 16) * 166, cw - 3, 162);
        };
    }
}

void MegaSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void MegaSynthEditor::resized()
{
    const float scale = (float) getWidth() / (float) kDesignW;
    content.setBounds (0, 0, kDesignW, kDesignH);
    content.setTransform (juce::AffineTransform::scale (scale));

    title.setBounds (14, 8, 220, 30);
    patchPrev.setBounds (14, 42, 26, 24);
    patchBox.setBounds (44, 42, 250, 24);
    patchNext.setBounds (298, 42, 26, 24);
    saveBtn.setBounds (330, 42, 100, 24);
    int x = 440;
    for (auto* k : headerKnobs) { k->setBounds (x, 4, 72, 80); x += 74; }
    copyBtn.setBounds (820, 10, 120, 28);
    pasteBtn.setBounds (820, 44, 120, 28);
    initBtn.setBounds (948, 10, 120, 28);
    status.setBounds (812, 74, 374, 14);
    tabs.setBounds (0, 88, kDesignW, 640);
    octDown.setBounds (10, 738, 70, 28);
    octUp.setBounds (10, 772, 70, 28);
    octLabel.setBounds (6, 806, 80, 40);
    keyboard.setBounds (90, 734, kDesignW - 100, 120);
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f);
}

void MegaSynthEditor::refreshPatchList()
{
    patchFiles = MegaSynthProcessor::getPatchFiles();
    const auto folder = MegaSynthProcessor::getPatchFolder();
    patchBox.clear (juce::dontSendNotification);
    for (int i = 0; i < patchFiles.size(); ++i)
        patchBox.addItem (patchFiles[i].getRelativePathFrom (folder).upToLastOccurrenceOf (".", false, false), i + 1);
    if (patchFiles.size() > 0) patchBox.addSeparator();
    patchBox.addItem ("Open patch file...", 9001);
    patchBox.addItem ("Show patch folder", 9002);

    const auto name = proc.getPatchName();
    lastShownName = name;
    for (int i = 0; i < patchFiles.size(); ++i)
        if (patchFiles[i].getFileNameWithoutExtension() == name) { patchBox.setSelectedId (i + 1, juce::dontSendNotification); return; }
    patchBox.setText (name, juce::dontSendNotification);
}

void MegaSynthEditor::loadPatch (const juce::File& f)
{
    const auto err = proc.loadPatchFromFile (f);
    setStatus (err.isEmpty() ? "Loaded " + f.getFileNameWithoutExtension() : err);
    refreshPatchList();
}

void MegaSynthEditor::stepPatch (int delta)
{
    patchFiles = MegaSynthProcessor::getPatchFiles();
    if (patchFiles.isEmpty()) { setStatus ("No saved patches yet - use Save Patch"); return; }
    int cur = -1;
    for (int i = 0; i < patchFiles.size(); ++i)
        if (patchFiles[i].getFileNameWithoutExtension() == proc.getPatchName()) cur = i;
    const int n = patchFiles.size();
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    loadPatch (patchFiles[next]);
}

void MegaSynthEditor::setStatus (const juce::String& s)
{
    status.setText (s, juce::dontSendNotification);
    statusTicks = 30 * 6;
}

void MegaSynthEditor::shiftOctave (int delta)
{
    auto* p = proc.param (P_keyboardOctave);
    const float cur = p->convertFrom0to1 (p->getValue());
    const float nv = juce::jlimit (-2.0f, 2.0f, cur + (float) delta);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (nv));
    p->endChangeGesture();
}

bool MegaSynthEditor::keyPressed (const juce::KeyPress& k)
{
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (c == 'z') { shiftOctave (-1); return true; }
    if (c == 'x') { shiftOctave (1); return true; }
    return false;
}

void MegaSynthEditor::timerCallback()
{
    // computer-keyboard octave (A = C4 at octave 0, like the browser version)
    const int kbOct = juce::roundToInt (proc.param (P_keyboardOctave)->convertFrom0to1 (proc.param (P_keyboardOctave)->getValue()));
    if (kbOct != lastOct)
    {
        lastOct = kbOct;
        keyboard.setKeyPressBaseOctave (5 + kbOct);
        octLabel.setText ("Keys: C" + juce::String (4 + kbOct) + "\nZ / X shift", juce::dontSendNotification);
    }

    for (int k = 0; k < 2; ++k)
        if (sampleBars[k] != nullptr) sampleBars[k]->status.setText (proc.getSampleStatus (k), juce::dontSendNotification);
    if (proc.getPatchName() != lastShownName) refreshPatchList();

    const auto v = proc.steps.getVersion();
    if (v != lastStepVersion)
    {
        lastStepVersion = v;
        for (auto* c : stepCells) c->refresh();
    }
    const int playing = proc.currentStep.load();
    if (playing != lastPlaying)
    {
        lastPlaying = playing;
        for (int i = 0; i < stepCells.size(); ++i) stepCells[i]->setPlaying (i == playing);
    }
    const int len = juce::roundToInt (proc.param (P_seqLength)->convertFrom0to1 (proc.param (P_seqLength)->getValue()));
    if (len != lastLength)
    {
        lastLength = len;
        for (int i = 0; i < stepCells.size(); ++i) stepCells[i]->setAlpha (i < len ? 1.0f : 0.3f);
    }

    for (auto* s : assignSlots) s->refresh();

    if (statusTicks > 0 && --statusTicks == 0) status.setText ({}, juce::dontSendNotification);
}
