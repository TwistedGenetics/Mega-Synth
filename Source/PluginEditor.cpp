#include "PluginEditor.h"
#include "ParamFormat.h"
#include "Registry.h"

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

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    // bars: bipolar ranges fill from the centre, so 0 reads as empty
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    g.setColour (s.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zero = bipolar ? (float) x + (float) s.valueToProportionOfLength (0.0) * (float) w : (float) x;
    g.setColour (s.findColour (juce::Slider::trackColourId).withMultipliedAlpha (s.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (zero, pos), (float) y, std::abs (pos - zero), (float) h), 3.0f);
    if (bipolar)
    {
        g.setColour (col::muted.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (zero - 0.5f, (float) y + 2.0f, 1.0f, (float) h - 4.0f));
    }
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
Knob::Knob (MegaSynthProcessor& p, int idx, const juce::String& cap, juce::Colour c) : paramIndex (idx), proc (p)
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
    {
        // tooltip from the parameter registry: what it is, where it lives, and how it can be driven
        const auto& m = tg::meta (idx);
        juce::String tip = m.name + "  (" + m.module + ")";
        if (m.unit.isNotEmpty()) tip << "\nUnit: " << m.unit;
        tip << "\nScale: " << tg::scaleName (m.scale) << (m.bipolar ? ", bipolar" : "");
        if (m.modulatable) tip << "\nModulatable" << (m.audioRate ? " (audio rate too)" : "") << " - right-click to modulate";
        tip << "\nDouble-click to reset";
        slider.setTooltip (tip);
    }
    slider.onRightClick = [this] { showMenu(); };
}

void Knob::showMenu()
{
    const auto& m = tg::meta (paramIndex);
    if (! m.modulatable) return;
    juce::PopupMenu menu, add;
    menu.addSectionHeader (m.name);
    SrcKind lastKind = K_NONE;
    for (int s = 1; s < MS_COUNT; ++s)
    {
        if (kModSrcKind[s] == K_AUDIO && ! m.audioRate) continue;
        if (kModSrcKind[s] != lastKind && lastKind != K_NONE) add.addSeparator();
        lastKind = kModSrcKind[s];
        add.addItem (1000 + s, kModSrcNames[s]);
    }
    menu.addSubMenu ("Modulate with", add);
    if (paramIndex < P_macro1 || paramIndex >= P_macro1 + 8)
    {
        juce::PopupMenu mac;
        for (int k = 0; k < 8; ++k) mac.addItem (1000 + MS_Macro1 + k, proc.getMacroName (k));
        menu.addSubMenu ("Assign to macro", mac);
    }
    bool any = false;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        const auto c = proc.routes.get (i);
        if (c.dst != paramIndex || c.src == MS_None) continue;
        if (! any) { menu.addSeparator(); any = true; }
        const float a = proc.param (P_mod1Amt + i)->convertFrom0to1 (proc.param (P_mod1Amt + i)->getValue());
        juce::PopupMenu sub;
        sub.addItem (2000 + i, "Show in Mod Matrix");
        sub.addItem (3000 + i, "Remove");
        menu.addSubMenu ("Route " + juce::String (i + 1) + ": " + kModSrcNames[c.src] + "  " + formatParam (P_mod1Amt + i, a), sub);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider),
                        [this] (int r)
                        {
                            if (r >= 1000 && r < 1000 + MS_COUNT)
                            {
                                const bool macro = r - 1000 >= MS_Macro1 && r - 1000 < MS_Macro1 + 8;
                                const int slot = proc.addRoute (r - 1000, paramIndex, macro ? 0.5f : 0.25f);
                                if (slot >= 0 && onShowRoute) onShowRoute (-1 - slot);   // negative: just note it, don't switch tabs
                            }
                            else if (r >= 2000 && r < 2000 + kNumRoutes) { if (onShowRoute) onShowRoute (r - 2000); }
                            else if (r >= 3000 && r < 3000 + kNumRoutes) proc.clearRoute (r - 3000);
                            updateMod();
                        });
}

void Knob::updateMod()
{
    bool h = false; float lo = 0, hi = 0;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        const auto c = proc.routes.get (i);
        if (! c.active() || c.dst != paramIndex) continue;
        h = true;
        const float a = proc.param (P_mod1Amt + i)->convertFrom0to1 (proc.param (P_mod1Amt + i)->getValue());
        if (kModSrcBipolar[c.src] && ! c.unipolar) { lo -= std::abs (a); hi += std::abs (a); }
        else if (a > 0) hi += a; else lo += a;
    }
    const float lv = h ? proc.getEngine().liveOffset[(size_t) paramIndex].load (std::memory_order_relaxed) : 0.0f;
    if (h != hasMod || std::abs (lo - modLo) > 1.0e-5f || std::abs (hi - modHi) > 1.0e-5f || std::abs (lv - live) > 0.002f)
    {
        hasMod = h; modLo = lo; modHi = hi; live = lv;
        repaint();
    }
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    if (! hasMod) return;
    const auto layout = slider.getLookAndFeel().getSliderLayout (slider);
    const auto bounds = layout.sliderBounds.toFloat().translated ((float) slider.getX(), (float) slider.getY()).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const auto rp = slider.getRotaryParameters();
    const float span = rp.endAngleRadians - rp.startAngleRadians;
    const float base = (float) slider.valueToProportionOfLength (slider.getValue());
    const float p0 = juce::jlimit (0.0f, 1.0f, base + modLo), p1 = juce::jlimit (0.0f, 1.0f, base + modHi);
    const float r = radius - 7.5f;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r, r, 0.0f, rp.startAngleRadians + p0 * span, rp.startAngleRadians + std::max (p1, p0 + 0.003f) * span, true);
    g.setColour (col::mod.withAlpha (0.85f));
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float a = rp.startAngleRadians + juce::jlimit (0.0f, 1.0f, base + live) * span;
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre ({ c.x + r * std::sin (a), c.y - r * std::cos (a) }));
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
static void addSourceItems (juce::ComboBox& box, bool includeAudio, const juce::String& noneText)
{
    box.addItem (noneText, MS_None + 1);
    const char* headings[] = { "", "LFOs", "Envelopes", "MIDI / Performance", "MPE", "Random", "Note", "Followers", "Audio rate (pitch, level, filter, FM)", "Macros" };
    SrcKind last = K_NONE;
    for (int s = 1; s < MS_COUNT; ++s)
    {
        if (kModSrcKind[s] == K_AUDIO && ! includeAudio) continue;
        if (kModSrcKind[s] != last) { box.addSectionHeading (headings[kModSrcKind[s]]); last = kModSrcKind[s]; }
        box.addItem (kModSrcNames[s], s + 1);
    }
}

RouteRow::RouteRow (MegaSynthProcessor& p, int s, std::function<juce::String()> searchText)
    : slot (s), proc (p), search (std::move (searchText))
{
    on.setTooltip ("Route on / off");
    on.setColour (juce::ToggleButton::tickColourId, col::mod);
    on.onClick = [this] { push(); };

    addSourceItems (src, true, "-");
    src.setTooltip ("Modulation source. Audio-rate sources (an oscillator's raw output) can drive pitch, levels, cutoff, resonance and FM depths sample by sample.");
    src.onChange = [this] { push(); };

    dest.setTooltip ("Destination: any modulatable parameter. Type in the search box above to filter the list.");
    dest.onClick = [this] { chooseDest(); };

    amount.setSliderStyle (juce::Slider::LinearBar);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amount.setColour (juce::Slider::trackColourId, col::mod.withAlpha (0.6f));
    amount.setColour (juce::Slider::backgroundColourId, col::panel3);
    amtAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kParamIds[P_mod1Amt + slot], amount);
    amount.setDoubleClickReturnValue (true, 0.0);
    amount.setTooltip ("Depth: how far the source moves the destination, as a share of the destination's full travel. Double-click for zero.");
    amount.onValueChange = [this] { amtText.setText (formatParam (P_mod1Amt + slot, (float) amount.getValue()), juce::dontSendNotification); };
    amtText.setJustificationType (juce::Justification::centred);
    amtText.setColour (juce::Label::textColourId, col::text);
    amtText.setFont (juce::Font (juce::FontOptions (11.5f)));
    amount.onValueChange();

    for (int c = 0; c < MC_COUNT; ++c) curve.addItem (kModCurveNames[c], c + 1);
    curve.setTooltip ("Response curve. Smooth adds a 60 ms glide; Stepped holds the value and updates 12 times a second; Quantized snaps to 8 levels.");
    curve.onChange = [this] { push(); };

    polarity.setTooltip ("Polarity of bipolar sources: +/- swings both ways around the knob, + only pushes up (source mapped to 0..1)");
    polarity.onClick = [this] { auto c = proc.routes.get (slot); c.unipolar = ! c.unipolar; proc.routes.set (slot, c); refresh(); };

    addSourceItems (via, false, "No via");
    via.setTooltip ("Via: a second source that scales this route's depth (e.g. mod wheel opens an LFO's vibrato)");
    via.onChange = [this] { push(); };

    for (auto* sl : { &viaDepth, &smooth })
    {
        sl->setSliderStyle (juce::Slider::LinearBar);
        sl->setColour (juce::Slider::trackColourId, col::mod.withAlpha (0.35f));
        sl->setColour (juce::Slider::backgroundColourId, col::panel3);
        sl->setColour (juce::Slider::textBoxTextColourId, col::text);
        sl->onDragEnd = [this] { push(); };
        sl->onValueChange = [this, sl] { if (! sl->isMouseButtonDown()) push(); };
    }
    viaDepth.setRange (0.0, 1.0, 0.01);
    viaDepth.textFromValueFunction = [] (double v) { return "via " + juce::String (juce::roundToInt (v * 100)) + "%"; };
    viaDepth.setTooltip ("How much the via source scales the depth");
    smooth.setRange (0.0, 2000.0, 1.0);
    smooth.setSkewFactorFromMidPoint (150.0);
    smooth.textFromValueFunction = [] (double v) { return v < 0.5 ? juce::String ("no smooth") : juce::String (juce::roundToInt (v)) + " ms"; };
    smooth.setTooltip ("Smoothing (slew) applied to the source before the curve");
    viaDepth.updateText(); smooth.updateText();

    clear.setTooltip ("Clear this route");
    clear.onClick = [this] { proc.clearRoute (slot); refresh(); };

    for (juce::Component* c : { (juce::Component*) &on, (juce::Component*) &src, (juce::Component*) &dest, (juce::Component*) &amount,
                                (juce::Component*) &amtText, (juce::Component*) &curve, (juce::Component*) &polarity, (juce::Component*) &via,
                                (juce::Component*) &viaDepth, (juce::Component*) &smooth, (juce::Component*) &clear })
        addAndMakeVisible (c);
    refresh();
}

void RouteRow::push()
{
    auto c = proc.routes.get (slot);
    const bool wasEmpty = c.src == MS_None && c.dst < 0;
    c.on = on.getToggleState();
    c.src = juce::jmax (0, src.getSelectedId() - 1);
    c.curve = juce::jmax (0, curve.getSelectedId() - 1);
    c.via = juce::jmax (0, via.getSelectedId() - 1);
    c.viaDepth = (float) viaDepth.getValue();
    c.smoothMs = (float) smooth.getValue();
    c.dst = dstIndex;
    if (wasEmpty && c.src != MS_None) c.on = true;
    proc.routes.set (slot, c);
    refresh();
}

void RouteRow::refresh()
{
    const auto c = proc.routes.get (slot);
    dstIndex = c.dst;
    on.setToggleState (c.on, juce::dontSendNotification);
    src.setSelectedId (c.src + 1, juce::dontSendNotification);
    curve.setSelectedId (c.curve + 1, juce::dontSendNotification);
    via.setSelectedId (c.via + 1, juce::dontSendNotification);
    if (! viaDepth.isMouseButtonDown()) viaDepth.setValue (c.viaDepth, juce::dontSendNotification);
    if (! smooth.isMouseButtonDown()) smooth.setValue (c.smoothMs, juce::dontSendNotification);
    viaDepth.setEnabled (c.via != MS_None);
    viaDepth.setAlpha (c.via != MS_None ? 1.0f : 0.4f);
    polarity.setButtonText (c.unipolar ? "+" : "+/-");
    polarity.setEnabled (kModSrcBipolar[c.src]);
    dest.setButtonText (c.dst >= 0 ? tg::meta (c.dst).module + ": " + tg::meta (c.dst).name : juce::String ("Choose destination..."));
    juce::String warn;
    if (c.dst >= 0 && kModSrcKind[c.src] == K_AUDIO && ! tg::meta (c.dst).audioRate)
        warn = "An audio-rate source can only drive pitch, level, cutoff, resonance or FM depths - this route is inactive.";
    dest.setTooltip (warn.isNotEmpty() ? warn : juce::String ("Destination: any modulatable parameter. Type in the search box above to filter the list."));
    dest.setColour (juce::TextButton::textColourOffId, warn.isNotEmpty() ? juce::Colour (0xffff6b6b) : col::text);
    setAlpha (c.on || c.src == MS_None ? 1.0f : 0.55f);
    repaint();
}

void RouteRow::chooseDest()
{
    const auto c = proc.routes.get (slot);
    const bool audioOnly = kModSrcKind[c.src] == K_AUDIO;
    const juce::String q = search ? search().trim() : juce::String();
    juce::PopupMenu menu;
    menu.addItem (100000, "None");
    menu.addSeparator();
    const auto dests = modulationDestinations();
    if (q.isNotEmpty())
    {
        int shown = 0;
        for (int d : dests)
        {
            const auto& m = tg::meta (d);
            if (! (m.name.containsIgnoreCase (q) || m.module.containsIgnoreCase (q) || m.id.containsIgnoreCase (q))) continue;
            menu.addItem (d + 1, m.module + ": " + m.name + (m.audioRate ? "  (audio rate)" : ""), ! audioOnly || m.audioRate, d == c.dst);
            ++shown;
        }
        if (shown == 0) menu.addItem (-1, "Nothing matches \"" + q + "\"", false);
    }
    else
    {
        juce::String module; juce::PopupMenu sub;
        auto flush = [&] { if (module.isNotEmpty()) menu.addSubMenu (module, sub); sub = {}; };
        for (int d : dests)
        {
            const auto& m = tg::meta (d);
            if (m.module != module) { flush(); module = m.module; }
            sub.addItem (d + 1, m.name + (m.audioRate ? "  (audio rate)" : ""), ! audioOnly || m.audioRate, d == c.dst);
        }
        flush();
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dest),
                        [this] (int r)
                        {
                            if (r == 0) return;
                            const bool fresh = dstIndex < 0;
                            dstIndex = r == 100000 ? -1 : r - 1;
                            push();
                            if (fresh && dstIndex >= 0 && std::abs (amount.getValue()) < 1.0e-6)
                                amount.setValue (0.25, juce::sendNotificationSync);   // a new route starts audible
                        });
}

void RouteRow::updateLive()
{
    const auto c = proc.routes.get (slot);
    const float sv = c.src != MS_None ? proc.getEngine().liveSrc[(size_t) c.src].load (std::memory_order_relaxed) : 0.0f;
    const float dv = c.dst >= 0 ? proc.getEngine().liveOffset[(size_t) c.dst].load (std::memory_order_relaxed) : 0.0f;
    if (std::abs (sv - srcLive) > 0.01f || std::abs (dv - dstLive) > 0.005f || highlight)
    {
        srcLive = sv; dstLive = dv;
        repaint();
    }
}

void RouteRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
    g.setColour (highlight ? col::mod.withAlpha (0.25f) : ((slot % 2) ? col::panel : col::panel2));
    g.fillRoundedRectangle (r, 5.0f);
    highlight = false;
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (juce::String (slot + 1), 26, 0, 22, getHeight(), juce::Justification::centred);

    // meters: the source's value (centre line = 0 for bipolar sources) and the destination's movement
    const auto c = proc.routes.get (slot);
    auto meter = [&] (juce::Rectangle<float> m, float v, bool bip)
    {
        g.setColour (col::panel3);
        g.fillRoundedRectangle (m, 2.0f);
        g.setColour (col::mod);
        if (bip)
        {
            const float mid = m.getCentreX(), x = mid + juce::jlimit (-1.0f, 1.0f, v) * m.getWidth() * 0.5f;
            g.fillRect (juce::Rectangle<float> (std::min (mid, x), m.getY(), std::abs (x - mid) + 1.0f, m.getHeight()));
        }
        else g.fillRect (m.withWidth (m.getWidth() * juce::jlimit (0.0f, 1.0f, v)));
    };
    if (c.src != MS_None && kModSrcKind[c.src] != K_AUDIO)
        meter ({ (float) getWidth() - 86.0f, 7.0f, 50.0f, 5.0f }, srcLive, kModSrcBipolar[c.src]);
    if (c.dst >= 0)
        meter ({ (float) getWidth() - 86.0f, (float) getHeight() - 12.0f, 50.0f, 5.0f }, dstLive, true);
}

void RouteRow::resized()
{
    auto r = getLocalBounds().reduced (2, 3);
    on.setBounds (r.removeFromLeft (24));
    r.removeFromLeft (24);
    src.setBounds (r.removeFromLeft (140)); r.removeFromLeft (4);
    dest.setBounds (r.removeFromLeft (212)); r.removeFromLeft (4);
    amount.setBounds (r.removeFromLeft (120)); r.removeFromLeft (2);
    amtText.setBounds (r.removeFromLeft (46)); r.removeFromLeft (4);
    curve.setBounds (r.removeFromLeft (104)); r.removeFromLeft (4);
    polarity.setBounds (r.removeFromLeft (36)); r.removeFromLeft (4);
    via.setBounds (r.removeFromLeft (120)); r.removeFromLeft (4);
    viaDepth.setBounds (r.removeFromLeft (72)); r.removeFromLeft (4);
    smooth.setBounds (r.removeFromLeft (80));
    clear.setBounds (r.removeFromRight (28));
}

//==============================================================================
XYPad::XYPad (MegaSynthProcessor& p) : proc (p)
{
    setTooltip ("Drag to morph between scenes A-D (Morph must be on). Scene X and Y can also be automated or modulated in the Mod Matrix.");
}

void XYPad::update()
{
    auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
    const float nx = plain (P_sceneX), ny = plain (P_sceneY);
    const float nlx = juce::jlimit (0.0f, 1.0f, nx + proc.getEngine().liveOffset[P_sceneX].load());
    const float nly = juce::jlimit (0.0f, 1.0f, ny + proc.getEngine().liveOffset[P_sceneY].load());
    const bool nm = plain (P_sceneMorph) > 0.5f;
    bool st[4]; bool changed = false;
    for (int k = 0; k < 4; ++k) { st[k] = proc.scenes.stored[k].load(); changed |= st[k] != stored[k]; stored[k] = st[k]; }
    const int ne = proc.getEditScene();
    if (changed || nm != morph || ne != edit || std::abs (nx - x) > 1e-4f || std::abs (ny - y) > 1e-4f
        || std::abs (nlx - lx) > 2e-3f || std::abs (nly - ly) > 2e-3f)
    {
        x = nx; y = ny; lx = nlx; ly = nly; morph = nm; edit = ne;
        repaint();
    }
}

void XYPad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    auto inner = r.reduced (14.0f);
    g.setColour (col::panel3);
    for (int i = 1; i < 4; ++i)
    {
        const float fx = inner.getX() + inner.getWidth() * i / 4.0f, fy = inner.getY() + inner.getHeight() * i / 4.0f;
        g.drawVerticalLine ((int) fx, inner.getY(), inner.getBottom());
        g.drawHorizontalLine ((int) fy, inner.getX(), inner.getRight());
    }
    // corner weights shown as a glow
    float w[4]; sceneWeights (lx, ly, w);
    const juce::Point<float> corners[4] = { inner.getTopLeft(), inner.getTopRight(), inner.getBottomLeft(), inner.getBottomRight() };
    for (int k = 0; k < 4; ++k)
    {
        const auto c = corners[k];
        if (morph && stored[k])
        {
            g.setColour (col::mod.withAlpha (0.12f + 0.5f * w[k]));
            g.fillEllipse (juce::Rectangle<float> (28.0f + 30.0f * w[k], 28.0f + 30.0f * w[k]).withCentre (c));
        }
        g.setColour (k == edit && morph ? col::mod : (stored[k] ? col::text : col::muted));
        g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + k)), juce::Rectangle<float> (24, 24).withCentre (c), juce::Justification::centred);
    }
    auto pt = [&] (float px, float py) { return juce::Point<float> (inner.getX() + px * inner.getWidth(), inner.getY() + py * inner.getHeight()); };
    if (std::abs (lx - x) > 1e-3f || std::abs (ly - y) > 1e-3f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (9, 9).withCentre (pt (lx, ly)));
    }
    g.setColour (morph ? col::mod : col::muted);
    g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre (pt (x, y)));
    g.setColour (col::bg);
    g.drawEllipse (juce::Rectangle<float> (16, 16).withCentre (pt (x, y)), 2.0f);
    if (! morph)
    {
        g.setColour (col::muted);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText ("Morph is off", r.removeFromBottom (30), juce::Justification::centred);
    }
}

void XYPad::setFrom (juce::Point<float> p)
{
    auto inner = getLocalBounds().toFloat().reduced (15.0f);
    const float nx = juce::jlimit (0.0f, 1.0f, (p.x - inner.getX()) / inner.getWidth());
    const float ny = juce::jlimit (0.0f, 1.0f, (p.y - inner.getY()) / inner.getHeight());
    proc.param (P_sceneX)->setValueNotifyingHost (nx);
    proc.param (P_sceneY)->setValueNotifyingHost (ny);
    update();
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    proc.param (P_sceneX)->beginChangeGesture(); proc.param (P_sceneY)->beginChangeGesture();
    setFrom (e.position);
}
void XYPad::mouseDrag (const juce::MouseEvent& e) { setFrom (e.position); }
void XYPad::mouseUp (const juce::MouseEvent&)
{
    proc.param (P_sceneX)->endChangeGesture(); proc.param (P_sceneY)->endChangeGesture();
}

//==============================================================================
MacroCell::MacroCell (MegaSynthProcessor& p, int i)
    : knob (p, P_macro1 + i, {}, col::accent), proc (p), index (i)
{
    addAndMakeVisible (knob);
    name.setText (p.getMacroName (i), juce::dontSendNotification);
    name.setEditable (false, true, false);
    name.setJustificationType (juce::Justification::centred);
    name.setColour (juce::Label::textColourId, col::text);
    name.setColour (juce::Label::backgroundWhenEditingColourId, col::panel3);
    name.setColour (juce::Label::textWhenEditingColourId, col::text);
    name.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this]
    {
        proc.setMacroName (index, name.getText());
        name.setText (proc.getMacroName (index), juce::dontSendNotification);
        proc.pushHistory ("Rename macro");
    };
    addAndMakeVisible (name);
    info.setJustificationType (juce::Justification::centred);
    info.setColour (juce::Label::textColourId, col::muted);
    info.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (info);
    update();
}

void MacroCell::update()
{
    int n = 0;
    for (int r = 0; r < kNumRoutes; ++r) { const auto c = proc.routes.get (r); if (c.active() && c.src == MS_Macro1 + index) ++n; }
    const juce::String t = n == 0 ? juce::String ("right-click a knob to assign") : juce::String (n) + (n == 1 ? " target" : " targets");
    if (info.getText() != t) info.setText (t, juce::dontSendNotification);
    const auto nm = proc.getMacroName (index);
    if (! name.isBeingEdited() && name.getText() != nm) name.setText (nm, juce::dontSendNotification);
}

void MacroCell::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (20));
    info.setBounds (r.removeFromBottom (16));
    knob.setBounds (r.withSizeKeepingCentre (76, r.getHeight()));
}

void MacroCell::paint (juce::Graphics& g)
{
    g.setColour (col::panel2);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f);
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
    tooltips = std::make_unique<juce::TooltipWindow> (this, 600);

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

    for (auto* b : { &copyBtn, &pasteBtn, &initBtn, &octDown, &octUp, &undoBtn, &redoBtn, &originalBtn }) content.addAndMakeVisible (*b);
    undoBtn.onClick = [this] { proc.undo(); setStatus ("Undo"); };
    redoBtn.onClick = [this] { proc.redo(); setStatus ("Redo"); };
    originalBtn.onClick = [this] { proc.returnToOriginal(); setStatus ("Returned to the original patch (Undo to go back)"); };
    undoBtn.setTooltip ("Undo the last change (knob moves, steps, patch loads, samples)");
    redoBtn.setTooltip ("Redo");
    originalBtn.setTooltip ("Go back to the patch as it was when it was loaded. Undo returns to where you were.");
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
        proc.pushHistory ("Paste patch");
        proc.markOriginal();
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

    // every knob can show its modulation and open the matrix
    std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
    {
        if (auto* k = dynamic_cast<Knob*> (&c)) allKnobs.add (k);
        for (auto* ch : c.getChildren()) collect (*ch);
    };
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* page = tabs.getTabContentComponent (i)) collect (*page);
    collect (content);
    for (auto* k : allKnobs) k->onShowRoute = [this] (int slot) { showRoute (slot); };

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
            o[i]->knob (P_osc1Oct + 3 * i, "Octave", 60);
            o[i]->knob (P_osc1Semi + i, "Semi", 60);
            o[i]->knob (P_osc1Detune + 3 * i, "Detune", 60);
            o[i]->knob (P_osc1Gain + i, "Level", 60);
        }
        auto* sub = sec (page, "Sub Oscillator", col::osc);
        sub->choice (P_subWave, "Waveform", 254);
        sub->newRow();
        sub->knob (P_subOct, "Octave", 60);
        sub->knob (P_subSemi, "Semi", 60);
        sub->knob (P_subGain, "Level", 60);

        // the two sample / wavetable oscillators
        struct WtIds { int mode, dir, norm, semi, det, oct, root, pos, win, ls, le, gain; };
        const WtIds ids[2] = {
            { P_osc4LoopMode, P_osc4Direction, P_osc4Normalize, P_osc4Semi, P_osc4Detune, P_osc4Oct, P_osc4Root, P_osc4Position, P_osc4Window, P_osc4LoopStart, P_osc4LoopEnd, P_osc4Gain },
            { P_wt2LoopMode, P_wt2Direction, P_wt2Normalize, P_wt2Semi, P_wt2Detune, P_wt2Oct, P_wt2Root, P_wt2Position, P_wt2Window, P_wt2LoopStart, P_wt2LoopEnd, P_wt2Gain } };
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
                                          proc.pushHistory ("Load sample");
                                      });
            };
            bar->clear.onClick = [this, k] { proc.clearSample (k); proc.pushHistory ("Clear sample"); setStatus ("Wavetable " + juce::String (k + 1) + " cleared"); };
            wtS[k]->newRow();
            wtS[k]->choice (ids[k].mode, "Loop Mode", 180);
            wtS[k]->choice (ids[k].dir, "Direction", 110);
            wtS[k]->choice (ids[k].norm, "Normalize", 110);
            wtS[k]->newRow();
            for (auto [idx, n] : { std::pair<int, const char*> { ids[k].oct, "Octave" }, { ids[k].semi, "Semi" }, { ids[k].det, "Detune" },
                                   { ids[k].root, "Root Note" }, { ids[k].pos, "Scan Pos" }, { ids[k].win, "Window" },
                                   { ids[k].ls, "Loop Start" }, { ids[k].le, "Loop End" }, { ids[k].gain, "Level" } })
                wtS[k]->knob (idx, n, 56);
        }

        auto* cx = sec (page, "Oscillator 5 / Complex", col::complex);
        cx->choice (P_complexWaveA, "Primary Wave A", 170);
        cx->choice (P_complexWaveB, "Mod Wave B", 170);
        cx->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_complexOct, "Octave" }, { P_complexSemi, "Semi" }, { P_complexDetune, "Detune" },
                               { P_complexRatio, "Mod Ratio" }, { P_complexFm, "FM Index" }, { P_complexShape, "Wavefold" },
                               { P_complexMix, "A/B Blend" }, { P_complexGain, "Level" } })
            cx->knob (idx, n, 64);

        auto* ss = sec (page, "Oscillator 6 / Unison SuperSaw", col::supersaw);
        for (auto [idx, n] : { std::pair<int, const char*> { P_supersawOct, "Octave" }, { P_supersawSemi, "Semi" }, { P_supersawDetune, "Detune" },
                               { P_supersawVoices, "Voices" }, { P_supersawSpread, "Spread" }, { P_supersawStereo, "Stereo" },
                               { P_supersawDrift, "Drift" }, { P_supersawGain, "Level" } })
            ss->knob (idx, n, 64);

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

    // ---------------------------------------------------------------- Mutation
    {
        auto* page = addPage ("Mutation");
        auto* dnS = sec (page, "DNA Splice  (builds a new wave from two of the note's own sources)", col::wavetable);
        dnS->choice (P_dnaMode, "Splice Mode", 170);
        dnS->choice (P_dnaA, "Source A", 110);
        dnS->choice (P_dnaB, "Source B", 110);
        dnS->knob (P_dnaMix, "Mix");
        dnS->knob (P_dnaAmount, "Amount");
        dnS->knob (P_dnaChar, "Character");
        dnaInfo = page->own (new juce::Label());
        dnaInfo->setColour (juce::Label::textColourId, col::text);
        dnaInfo->setFont (juce::Font (juce::FontOptions (12.5f)));
        dnaInfo->setJustificationType (juce::Justification::topLeft);
        auto* wmS = sec (page, "Wave Mutation  (after the oscillator mix, before the filter; each note on its own)", col::complex);
        wmS->knob (P_wmMix, "Mix");
        wmS->knob (P_wmDrive, "Drive");
        wmS->knob (P_wmFold, "Fold");
        wmS->knob (P_wmShape, "Shape");
        wmS->knob (P_wmBend, "Bend");
        wmS->knob (P_wmAsym, "Asymmetry");
        wmS->knob (P_wmRect, "Rectify");
        wmS->knob (P_wmBits, "Bit Depth");
        wmS->knob (P_wmDown, "Rate Reduce");
        auto* arS = sec (page, "Audio-Rate Transform  (FM, AM, ring modulation and frequency shift)", col::fm);
        arS->choice (P_arMod, "Modulator", 130);
        arS->knob (P_arRatio, "Ratio");
        arS->knob (P_arOffset, "Offset");
        arS->knob (P_arFm, "FM");
        arS->choice (P_arFmTarget, "FM Target", 140);
        arS->knob (P_arAm, "AM");
        arS->knob (P_arRing, "Ring");
        arS->knob (P_arShift, "Shift");
        arS->knob (P_arShiftMix, "Shift Mix");
        auto* help = page->own (new juce::Label ({}, "Wave Mutation: Drive pushes the mix into the shaper; Bend curves it, Asymmetry adds even harmonics, "
            "Fold wraps peaks back over (wavefolding), Shape saturates, Rectify flips the negative half up. These run at twice the sample rate to "
            "keep aliasing down. Bit Depth and Rate Reduce come after, for deliberate digital grit. Mix 0 = bypassed.\n\n"
            "Order: DNA Splice -> Wave Mutation -> Audio-Rate Transform -> filter.\n\n"
        "Audio-Rate Transform: the Modulator is an internal sine at Ratio x the note (plus Offset in Hz, for clangorous inharmonic tones), "
            "one of the note's own oscillators, or noise. FM bends the chosen oscillators' pitch with it every sample (through-zero), "
            "AM and Ring multiply the sound by it, and Shift moves every frequency up or down by a fixed number of Hz (not a pitch shift - "
            "harmonics stop being harmonic). Every knob here can be modulated from the Mod Matrix."));
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.5f)));
        help->setJustificationType (juce::Justification::topLeft);
        page->onResize = [this, page, dnS, wmS, arS, help]
        {
            const int g = 10, W = page->getWidth();
            dnS->setBounds (g, g, W - 2 * g, 130);
            dnaInfo->setBounds (g + 700, g + 34, W - 2 * g - 710, 90);
            dnaInfo->toFront (false);
            wmS->setBounds (g, 150, W - 2 * g, 130);
            arS->setBounds (g, 290, W - 2 * g, 130);
            help->setBounds (g + 6, 432, W - 2 * g - 12, 160);
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

    // ---------------------------------------------------------------- Mod Matrix
    {
        auto* page = addPage ("Mod Matrix");
        matrixTab = tabs.getNumTabs() - 1;
        auto* srcSec = sec (page, "More sources", col::mod);
        srcSec->choice (P_lfo4Wave, "LFO 4 Wave", 110);
        srcSec->knob (P_lfo4Rate, "LFO 4 Rate", 64);
        srcSec->knob (P_lfo4Depth, "LFO 4 Depth", 64);
        srcSec->knob (P_randRate, "Random Rate", 70);
        srcSec->knob (P_ccANum, "CC A", 56);
        srcSec->knob (P_ccBNum, "CC B", 56);
        auto* help = page->own (new juce::Label ({}, "Any source to any knob. Right-click a knob to modulate it; a cyan ring shows the range and a dot shows the "
                                                     "value the newest note hears. Depth is a share of the destination's travel. Envelope times are set at note-on."));
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.0f)));
        help->setJustificationType (juce::Justification::topLeft);

        auto* listSec = sec (page, "Routes", col::mod);
        page->addAndMakeVisible (matrixSearch);
        matrixSearch.setTextToShowWhenEmpty ("Search destinations (e.g. cutoff, osc 2, reverb)", col::muted);
        matrixSearch.setFont (juce::Font (juce::FontOptions (12.5f)));
        auto* header = page->own (new juce::Component());
        struct H { const char* t; int x, w; };
        static const H heads[] = { { "On", 0, 30 }, { "#", 26, 24 }, { "Source", 48, 140 }, { "Destination", 192, 212 }, { "Depth", 408, 168 },
                                   { "Curve", 580, 104 }, { "Pol.", 688, 36 }, { "Via", 728, 120 }, { "Via depth", 852, 72 }, { "Smooth", 928, 80 },
                                   { "Src / Dst", 1066, 70 } };
        for (auto& h : heads)
        {
            auto* l = new juce::Label ({}, h.t);
            l->setColour (juce::Label::textColourId, col::muted);
            l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            l->setBounds (h.x, 0, h.w, 16);
            header->addAndMakeVisible (l);
            page->children.add (l);
        }
        auto* list = new juce::Component();
        for (int i = 0; i < kNumRoutes; ++i)
        {
            auto* row = new RouteRow (proc, i, [this] { return matrixSearch.getText(); });
            list->addAndMakeVisible (row);
            page->children.add (row);
            routeRows.add (row);
        }
        matrixView = std::make_unique<juce::Viewport>();
        matrixView->setViewedComponent (list, true);   // the viewport owns the list; rows are owned by the page (deleted after)
        matrixView->setScrollBarsShown (true, false);
        matrixView->setScrollBarThickness (8);
        page->addAndMakeVisible (*matrixView);
        auto* view = matrixView.get();
        juce::Array<RouteRow*> rows (routeRows);
        page->onResize = [page, srcSec, help, listSec, header, view, list, rows, this]
        {
            const int g = 10, W = page->getWidth();
            srcSec->setBounds (g, g, 520, 128);
            help->setBounds (540, g + 6, W - 540 - g, 120);
            listSec->setBounds (g, 146, W - 2 * g, page->getHeight() - 146 - g);
            matrixSearch.setBounds (W - g - 330, 152, 316, 22);
            header->setBounds (g + 10, 180, W - 2 * g - 20, 16);
            view->setBounds (g + 8, 198, W - 2 * g - 16, page->getHeight() - 198 - g - 6);
            const int rw = view->getWidth() - 10;
            list->setSize (rw, 30 * kNumRoutes);
            for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * 30, rw, 30);
        };
    }

    // ---------------------------------------------------------------- Macros & Scenes
    {
        auto* page = addPage ("Macros & Scenes");
        auto* macSec = sec (page, "Macros  (right-click any knob > Assign to macro; double-click a name to rename)", col::accent);
        for (int i = 0; i < 8; ++i) macroCells.add (page->own (new MacroCell (proc, i)));
        auto* scSec = sec (page, "Scenes", col::mod);
        xyPad = page->own (new XYPad (proc));
        for (int k = 0; k < 4; ++k)
        {
            const juce::String L = juce::String::charToString ((juce::juce_wchar) ('A' + k));
            sceneEditBtn[k].setButtonText (L);
            sceneEditBtn[k].setClickingTogglesState (false);
            sceneEditBtn[k].onClick = [this, k]
            {
                const bool morph = proc.param (P_sceneMorph)->getValue() > 0.5f;
                if (morph) { proc.editScene (k); setStatus ("Editing scene " + juce::String::charToString ((juce::juce_wchar) ('A' + k))); }
                else if (proc.scenes.stored[k].load()) { proc.recallScene (k); proc.pushHistory ("Recall scene"); setStatus ("Recalled scene " + juce::String::charToString ((juce::juce_wchar) ('A' + k))); }
                else setStatus ("Scene " + juce::String::charToString ((juce::juce_wchar) ('A' + k)) + " is empty - press Store first");
                updateSceneButtons();
            };
            sceneStoreBtn[k].setButtonText ("Store " + L);
            sceneStoreBtn[k].setTooltip ("Store the panel's current sound as scene " + L);
            sceneStoreBtn[k].onClick = [this, k] { proc.storeScene (k); setStatus ("Stored scene " + juce::String::charToString ((juce::juce_wchar) ('A' + k))); updateSceneButtons(); };
            sceneState[k].setColour (juce::Label::textColourId, col::muted);
            sceneState[k].setFont (juce::Font (juce::FontOptions (12.0f)));
            for (juce::Component* c : { (juce::Component*) &sceneEditBtn[k], (juce::Component*) &sceneStoreBtn[k], (juce::Component*) &sceneState[k] })
                page->addAndMakeVisible (c);
        }
        morphAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_sceneMorph], morphBtn);
        morphBtn.setColour (juce::ToggleButton::tickColourId, col::mod);
        morphBtn.setTooltip ("On: the sound is a blend of the four scenes at the XY position, and the panel edits the selected scene");
        clearScenesBtn.onClick = [this] { proc.clearScenes(); proc.pushHistory ("Clear scenes"); setStatus ("Scenes cleared"); updateSceneButtons(); };
        page->addAndMakeVisible (morphBtn);
        page->addAndMakeVisible (clearScenesBtn);
        auto* sx = page->own (new Knob (proc, P_sceneX, "Scene X", col::mod));
        auto* sy = page->own (new Knob (proc, P_sceneY, "Scene Y", col::mod));
        auto* help = page->own (new juce::Label ({}, "Each scene is a complete sound: every oscillator, filter, envelope, LFO and effect setting "
            "(not the matrix routes, macros, volume or sequencer).\n\n"
            "1. Make a sound and press Store A. Change it and Store B, and so on.\n"
            "2. Turn on Morph and drag the pad. Knobs blend smoothly; switches (waveforms, filter type) "
            "take the nearest scene's setting.\n"
            "3. With Morph on, the panel shows the selected scene (A-D buttons) and every knob you turn edits that scene.\n"
            "With Morph off, the A-D buttons recall a scene onto the panel.\n\n"
            "Scene X / Y are ordinary parameters: automate them, assign them to a macro, or route an LFO or the mod wheel to them in the Mod Matrix "
            "(each note then morphs on its own)."));
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.5f)));
        help->setJustificationType (juce::Justification::topLeft);
        juce::Array<MacroCell*> cells (macroCells);
        auto* pad = xyPad;
        updateSceneButtons();
        page->onResize = [this, page, macSec, cells, scSec, pad, sx, sy, help]
        {
            const int g = 10, W = page->getWidth();
            macSec->setBounds (g, g, W - 2 * g, 160);
            const int cw = (W - 2 * g - 20) / 8;
            for (int i = 0; i < cells.size(); ++i) cells[i]->setBounds (g + 10 + i * cw, g + 32, cw - 8, 120);
            scSec->setBounds (g, 180, W - 2 * g, page->getHeight() - 180 - g);
            const int padSize = juce::jmin (380, page->getHeight() - 180 - g - 44);
            pad->setBounds (g + 12, 214, padSize, padSize);
            int x = g + 12 + padSize + 24, y = 214;
            for (int k = 0; k < 4; ++k)
            {
                sceneEditBtn[k].setBounds (x, y + k * 40, 44, 32);
                sceneStoreBtn[k].setBounds (x + 52, y + k * 40, 90, 32);
                sceneState[k].setBounds (x + 150, y + k * 40, 130, 32);
            }
            morphBtn.setBounds (x, y + 170, 220, 26);
            clearScenesBtn.setBounds (x, y + 206, 120, 28);
            sx->setBounds (x, y + 250, 72, 84);
            sy->setBounds (x + 80, y + 250, 72, 84);
            help->setBounds (x + 300, y, W - g - 12 - (x + 300), page->getHeight() - y - g - 8);
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
    undoBtn.setBounds (948, 44, 58, 28);
    redoBtn.setBounds (1010, 44, 58, 28);
    originalBtn.setBounds (1076, 44, 110, 28);
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

void MegaSynthEditor::updateSceneButtons()
{
    const bool morph = proc.param (P_sceneMorph)->getValue() > 0.5f;
    const int edit = proc.getEditScene();
    for (int k = 0; k < 4; ++k)
    {
        const bool stored = proc.scenes.stored[k].load();
        const bool editing = morph && k == edit;
        sceneEditBtn[k].setColour (juce::TextButton::buttonColourId, editing ? col::mod.withAlpha (0.55f) : col::panel3);
        sceneEditBtn[k].setTooltip (morph ? "Edit scene " + sceneEditBtn[k].getButtonText() + " (the panel shows it)"
                                          : "Recall scene " + sceneEditBtn[k].getButtonText() + " onto the panel");
        const juce::String t = ! stored ? "empty" : (editing ? "stored - editing" : "stored");
        if (sceneState[k].getText() != t) sceneState[k].setText (t, juce::dontSendNotification);
    }
}

void MegaSynthEditor::showRoute (int slot)
{
    if (slot < 0)
    {
        // a route was just added from a knob's menu
        slot = -1 - slot;
        if (auto c = proc.routes.get (slot); c.dst >= 0)
            setStatus (juce::String ("Route ") + juce::String (slot + 1) + ": " + kModSrcNames[c.src] + " -> " + tg::meta (c.dst).name
                       + " at +25% (drag the knob's depth in Mod Matrix)");
        return;
    }
    if (matrixTab < 0 || slot >= routeRows.size()) return;
    tabs.setCurrentTabIndex (matrixTab);
    matrixView->setViewPosition (0, juce::jmax (0, slot * 30 - 60));
    routeRows[slot]->flash();
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
    undoBtn.setEnabled (proc.canUndo() || proc.lastHistoryLabel().isNotEmpty());
    redoBtn.setEnabled (proc.canRedo());

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

    // modulation matrix: rows follow the store; knobs show their rings
    const auto rv = proc.routes.getVersion();
    if (rv != lastRouteVersion) { lastRouteVersion = rv; for (auto* r : routeRows) r->refresh(); }
    if (matrixTab >= 0 && tabs.getCurrentTabIndex() == matrixTab)
        for (auto* r : routeRows) r->updateLive();
    for (auto* k : allKnobs) if (k->isShowing()) k->updateMod();
    if (dnaInfo != nullptr && dnaInfo->isShowing())
    {
        const int mode = (int) proc.param (P_dnaMode)->convertFrom0to1 (proc.param (P_dnaMode)->getValue());
        if (mode != lastDnaMode)
        {
            lastDnaMode = mode;
            static const char* info[] = {
                "Waveform Splice: A plays the first part of every cycle, B the rest.\nAmount = where in the cycle B takes over. Character = how soft the joins are.",
                "Crossover: A below a frequency, B above it.\nAmount = crossover point (from the note's fundamental up to its 128th harmonic). Character = slope, 12 to 24 dB/oct.",
                "Harmonic: every Nth harmonic comes from B, the rest from A.\nAmount = how much of B's harmonics replace A's. Character = N, from every 2nd (even harmonics) to every 8th.",
                "Spectral: B's sound shaped by A's spectrum (8-band cross-synthesis, like a vocoder).\nAmount = blend from plain B to the cross-synthesis. Character = how fast the bands follow A.",
                "Transient / Body: A's attack, then B's sustain.\nAmount = how long A lasts (2 ms to 0.5 s). Character = crossfade length.",
                "Amplitude DNA: B's waveform following A's loudness contour.\nAmount = blend. Character = how fast it follows A.",
                "Morph / Gene Shuffle: Amount moves from A to B.\nCharacter turns the smooth morph into a cycle-by-cycle shuffle, each cycle taken from A or B (Amount = chance of B)." };
            dnaInfo->setText (info[juce::jlimit (0, 6, mode)], juce::dontSendNotification);
        }
    }
    if (xyPad != nullptr && xyPad->isShowing())
    {
        xyPad->update();
        for (auto* m : macroCells) m->update();
        updateSceneButtons();
    }

    if (statusTicks > 0 && --statusTicks == 0) status.setText ({}, juce::dontSendNotification);
}
