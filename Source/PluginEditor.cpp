#include "PluginEditor.h"
#include "UI/MixerConsole.h"
#include "UI/FilterPanel.h"
#include "UI/DnaSeqPage.h"
#include "UI/FxPages.h"
#include "UI/LfoPage.h"
#include "UI/ArpPage.h"
#include "ParamFormat.h"
#include "Registry.h"
#include "UI/Overview.h"
#include "UI/PerfFxPage.h"
#include "UI/SamplePages.h"
#include "UI/BreakPages.h"

using namespace tg;
using namespace tgui;

static constexpr int kDesignW = 1200;
static constexpr int kDesignH = 890;

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
    auto r = b.getLocalBounds().toFloat().reduced (1.5f, 3.0f);
    const bool front = b.isFrontTab();
    g.setColour (front ? col::panel3 : (over ? col::panel2.brighter (0.05f) : col::panel2));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (front ? col::accent : col::border);
    g.drawRoundedRectangle (r, 7.0f, front ? 2.0f : 1.0f);
    g.setColour (front ? col::text : col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
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
    juce::PopupMenu menu, add;
    menu.addSectionHeader (m.name);
    const int cc = proc.ccForParam (paramIndex);
    menu.addItem (5001, proc.learningParam() == paramIndex ? "MIDI Learn: move a controller..." : "MIDI Learn");
    if (cc >= 0) menu.addItem (5002, "Forget MIDI (CC " + juce::String (cc) + ")");
    menu.addSeparator();
    if (! m.modulatable)
    {
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [this] (int r)
        {
            if (r == 5001) proc.midiLearn (paramIndex);
            else if (r == 5002) proc.midiForget (paramIndex);
            updateMod(); repaint();
        });
        return;
    }
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
                            else if (r == 5001) proc.midiLearn (paramIndex);
                            else if (r == 5002) proc.midiForget (paramIndex);
                            updateMod(); repaint();
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
    const int cc = proc.ccForParam (paramIndex);
    const bool learning = proc.learningParam() == paramIndex;
    if (cc != shownCC || learning != shownLearn) { shownCC = cc; shownLearn = learning; repaint(); }
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    // MIDI learn: the mapped controller, or an outline while waiting for one
    if (shownLearn)
    {
        g.setColour (col::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
    }
    if (shownCC >= 0 || shownLearn)
    {
        g.setColour (shownLearn ? col::accent : col::muted);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (shownLearn ? "LEARN" : "CC" + juce::String (shownCC), getLocalBounds().removeFromTop (14).removeFromRight (34), juce::Justification::centredRight);
    }
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
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, kParamIds[idx], tagParam (box, idx));
}

void Choice::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (16));
    box.setBounds (r.removeFromTop (24));
}

//==============================================================================
namespace
{
    // the (i) next to a section title: hover for the explanation
    struct InfoBadge : public juce::Component, public juce::SettableTooltipClient
    {
        explicit InfoBadge (juce::Colour c) : colour (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (colour.withAlpha (isMouseOver() ? 0.9f : 0.55f));
            g.drawEllipse (r, 1.3f);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText ("i", r, juce::Justification::centred);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        juce::Colour colour;
    };
}

Section::Section (MegaSynthProcessor& p, const juce::String& t, juce::Colour c) : colour (c), proc (p), title (t)
{
    // "Name  (explanation)": the name stays in the title, the explanation moves behind the (i)
    const int open = t.indexOf ("  (");
    if (open > 0 && t.endsWithChar (')'))
    {
        title = t.substring (0, open);
        addInfo (t.substring (open + 3, t.length() - 1).trim());
    }
}

void Section::addInfo (const juce::String& text)
{
    info = info.isEmpty() ? text : info + "\n\n" + text;
    if (infoBadge == nullptr)
    {
        infoBadge = std::make_unique<InfoBadge> (col::muted);
        addAndMakeVisible (*infoBadge);
    }
    auto* b = static_cast<InfoBadge*> (infoBadge.get());
    b->setTooltip (info);
    b->setHelpText (info);
    resized();
}

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
    if (infoBadge != nullptr)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)), title, 0.0f, 0.0f);
        const int tw = juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth());
        infoBadge->setBounds (juce::jmin (getWidth() - 26, 23 + tw + 8), 9, 16, 16);
    }
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
    srcAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, kParamIds[first], tagParam (source, first));

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
    const char* headings[] = { "", "LFOs", "Envelopes", "MIDI / Performance", "MPE", "Random", "Note", "Followers", "Audio rate (pitch, level, filter, FM)", "Macros", "Sequencer" };
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
void CaptureView::refresh()
{
    const auto& b = proc.getRawCapture();
    const int W = std::max (1, getWidth());
    peaks.assign ((size_t) W, 0.0f);
    const int n = b.getNumSamples();
    if (n == 0) { repaint(); return; }
    for (int x = 0; x < W; ++x)
    {
        const int a = (int) ((int64_t) n * x / W), e = std::max (a + 1, (int) ((int64_t) n * (x + 1) / W));
        float m = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) m = std::max (m, b.getMagnitude (c, a, e - a));
        peaks[(size_t) x] = m;
    }
    float top = 1.0e-6f; for (float p : peaks) top = std::max (top, p);
    for (auto& p : peaks) p /= top;
    repaint();
}

void CaptureView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (proc.isCapturing())
    {
        const float f = (float) (proc.captureSecondsRecorded() / MegaSynthProcessor::kCaptureSeconds);
        g.setColour (juce::Colour (0xffff6b6b).withAlpha (0.35f));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, f)), 8.0f);
        g.setColour (col::text);
        g.drawText ("Recording " + juce::String (proc.captureSecondsRecorded(), 1) + " s", r, juce::Justification::centred);
        return;
    }
    if (! proc.hasCapture() || peaks.empty())
    {
        g.setColour (col::muted);
        g.drawText ("Press Record, play, press Stop (up to 8 seconds)", r, juce::Justification::centred);
        return;
    }
    const auto& s = proc.captureSettings;
    const float a = std::min (s.trimStart, s.trimEnd) * r.getWidth(), b = std::max (s.trimStart, s.trimEnd) * r.getWidth();
    g.setColour (col::bg.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (0, 0, a, r.getHeight()));
    g.fillRect (juce::Rectangle<float> (b, 0, r.getWidth() - b, r.getHeight()));
    const float mid = r.getCentreY(), h = r.getHeight() * 0.45f;
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const bool in = x >= a && x <= b;
        g.setColour (in ? col::accent : col::muted.withAlpha (0.5f));
        g.drawVerticalLine (x, mid - peaks[(size_t) x] * h, mid + peaks[(size_t) x] * h + 1.0f);
    }
    g.setColour (col::text);
    g.fillRect (juce::Rectangle<float> (a - 1.0f, 0, 2.0f, r.getHeight()));
    g.fillRect (juce::Rectangle<float> (b - 1.0f, 0, 2.0f, r.getHeight()));
}

void CaptureView::mouseDown (const juce::MouseEvent& e)
{
    const float x = e.position.x / (float) getWidth();
    auto& s = proc.captureSettings;
    dragging = std::abs (x - s.trimStart) < std::abs (x - s.trimEnd) ? 1 : 2;
    mouseDrag (e);
}

void CaptureView::mouseDrag (const juce::MouseEvent& e)
{
    if (! proc.hasCapture()) return;
    const float x = juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth());
    auto& s = proc.captureSettings;
    if (dragging == 1) s.trimStart = std::min (x, s.trimEnd - 0.002f); else s.trimEnd = std::max (x, s.trimStart + 0.002f);
    repaint();
    if (onEdit) onEdit();
}

//==============================================================================
FbGrid::FbGrid (MegaSynthProcessor& p)
{
    static const char* src[] = { "Granular", "Spectral", "Delay", "Output" };
    static const char* dst[] = { "Granular", "Spectral", "Delay" };
    for (int s = 0; s < 4; ++s)
        for (int d = 0; d < 3; ++d)
        {
            auto& sl = amt[s][d];
            sl.setSliderStyle (juce::Slider::LinearBar);
            sl.setColour (juce::Slider::trackColourId, col::complex.withAlpha (0.6f));
            sl.setColour (juce::Slider::backgroundColourId, col::panel3);
            sl.setColour (juce::Slider::textBoxTextColourId, col::text);
            sl.setTooltip (juce::String ("Feed ") + src[s] + " back into " + dst[d] + " (Feedback Time later)");
            att[s][d] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kParamIds[P_fbGrGr + s * 3 + d], sl);
            sl.setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (sl);
        }
}

void FbGrid::resized()
{
    const int x0 = 84, cw = (getWidth() - x0) / 3;
    for (int s = 0; s < 4; ++s)
        for (int d = 0; d < 3; ++d) amt[s][d].setBounds (x0 + d * cw + 2, 20 + s * 28, cw - 6, 24);
}

void FbGrid::paint (juce::Graphics& g)
{
    static const char* src[] = { "Granular", "Spectral", "Delay", "Output" };
    static const char* dst[] = { "> Granular", "> Spectral", "> Delay" };
    const int x0 = 84, cw = (getWidth() - x0) / 3;
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.setColour (col::muted);
    for (int d = 0; d < 3; ++d) g.drawText (dst[d], x0 + d * cw, 0, cw, 18, juce::Justification::centred);
    for (int s = 0; s < 4; ++s) g.drawText (src[s], 0, 20 + s * 28, x0 - 6, 24, juce::Justification::centredRight);
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
    const int savedWidth = p.uiWidth.load();   // read before any resize below overwrites it
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
    qualityChoice = std::make_unique<tgui::Choice> (proc, P_quality, "Quality");
    qualityChoice->box.setTooltip ("Eco: no Wave Mutation oversampling, Resonator up to 6 modes, Granular up to 24 grains, "
                                   "Spectral FFT up to 1024. Normal: as designed. High: 4x oversampled Wave Mutation.");
    content.addAndMakeVisible (*qualityChoice);

    for (auto* b : { &copyBtn, &pasteBtn, &initBtn, &octDown, &octUp, &undoBtn, &redoBtn, &originalBtn }) content.addAndMakeVisible (*b);
    undoBtn.onClick = [this] { proc.undo(); setStatus ("Undo"); };
    redoBtn.onClick = [this] { proc.redo(); setStatus ("Redo"); };
    originalBtn.onClick = [this] { proc.returnToOriginal(); setStatus ("Returned to the original patch (Undo to go back)"); };
    undoBtn.setTooltip ("Undo the last change (knob moves, steps, patch loads, samples)");
    redoBtn.setTooltip ("Redo");
    originalBtn.setTooltip ("Go back to the patch as it was when it was loaded. Undo returns to where you were.");
    subtitle.setVisible (false);

    // ---- patch browser
    for (auto* b : { &patchPrev, &patchNext, &saveBtn, &browseBtn, &abA, &abB, &abCopy, &sizeBtn }) content.addAndMakeVisible (*b);
    browseBtn.setTooltip ("Browse patches: search, categories, favourites, load as you browse");
    browseBtn.onClick = [this] { showBrowser (browser == nullptr || ! browser->isVisible()); };
    abA.setTooltip ("A/B compare: play version A of this patch");
    abB.setTooltip ("A/B compare: play version B. Both start as the loaded patch; edit one, then switch to hear the difference.");
    abCopy.setTooltip ("Copy the version you're playing to the other slot");
    for (auto* b : { &abA, &abB }) { b->setClickingTogglesState (false); b->setColour (juce::TextButton::buttonOnColourId, col::accent.withAlpha (0.55f)); }
    abA.onClick = [this] { proc.abSelect (0); setStatus ("Playing A"); };
    abB.onClick = [this] { proc.abSelect (1); setStatus ("Playing B"); };
    abCopy.onClick = [this] { proc.abCopyToOther(); setStatus (proc.abSlot() == 0 ? "Copied A to B" : "Copied B to A"); };
    sizeBtn.setTooltip ("Window size (or drag the bottom-right corner). The size is saved with your project.");
    sizeBtn.onClick = [this] { showSizeMenu(); };
    content.addAndMakeVisible (patchBox);
    patchBox.setTooltip ("Saved patches (Music/Mega Synth/Patches)");
    patchBox.onChange = [this]
    {
        const int id = patchBox.getSelectedId();
        if (id >= 1 && id <= patchFiles.size()) { loadPatch (patchFiles[id - 1]); return; }
        if (id > kFactoryIdBase && id <= kFactoryIdBase + (int) tg::factoryPresets().size()) { loadFactory (id - kFactoryIdBase - 1); return; }
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
    initBtn.onClick = [this] { showInitMenu(); };
    initBtn.setTooltip ("Init: load a clean starting patch, save the current patch as your own Init, or reset it");
    octDown.onClick = [this] { shiftOctave (-1); };
    octUp.onClick = [this] { shiftOctave (1); };

    tabs.setTabBarDepth (32);
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
    {
        // back to the tab and sub-tabs this project had open
        const auto saved = proc.getUiTabs();
        if (saved.isNotEmpty())
        {
            juce::StringArray subs; subs.addTokens (saved.fromFirstOccurrenceOf (";", false, false), ",", {});
            for (int t = 0; t < juce::jmin (subs.size(), tabs.getNumTabs()); ++t)
                if (auto* g = dynamic_cast<GroupPage*> (tabs.getTabContentComponent (t))) g->show (subs[t].getIntValue());
            const int tab = saved.upToFirstOccurrenceOf (";", false, false).getIntValue();
            if (tab >= 0 && tab < tabs.getNumTabs()) tabs.setCurrentTabIndex (tab);
        }
        lastUiTabs = saved;
    }

    keyboard.setAvailableRange (36, 96);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setScrollButtonsVisible (false);
    content.addAndMakeVisible (keyboard);

    setResizable (true, true);
    setResizeLimits (kDesignW * 2 / 3, kDesignH * 2 / 3, kDesignW * 2, kDesignH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kDesignW / kDesignH);
    {
        // the project's saved size, otherwise 90% so it fits laptop screens; drag the corner to resize
        const int saved = savedWidth;
        const int w = saved > 0 ? juce::jlimit (kDesignW * 2 / 3, kDesignW * 2, saved) : kDesignW * 9 / 10;
        setSize (w, juce::roundToInt (w * (double) kDesignH / kDesignW));
        sizeReady = true;
        if (sizeReady) proc.uiWidth = getWidth();
    }
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
    midiMonitor = std::make_unique<tgui::MidiMonitor> (proc);
    content.addAndMakeVisible (*midiMonitor);
    midiMonitor->setBounds (kDesignW - 240, 764, 230, 120);
    content.addMouseListener (&learnClicks, true);
    startTimerHz (30);
}

void MegaSynthEditor::LearnClicks::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    juce::Component* c = e.originalComponent;
    for (int up = 0; c != nullptr && up < 4 && ! c->getProperties().contains ("megaParam"); ++up) c = c->getParentComponent();
    if (c == nullptr || ! c->getProperties().contains ("megaParam")) return;
    const int param = (int) c->getProperties()["megaParam"];
    if (param < 0 || param >= tg::P_COUNT) return;
    if (auto* b = dynamic_cast<juce::Button*> (c)) b->setState (juce::Button::buttonNormal);   // a right-click must not flip the toggle
    auto& proc = ed.proc;
    juce::PopupMenu menu;
    menu.addSectionHeader (tg::meta (param).name);
    menu.addItem (1, proc.learningParam() == param ? "MIDI Learn: move a controller..." : "MIDI Learn");
    const int cc = proc.ccForParam (param);
    if (cc >= 0) menu.addItem (2, "Forget MIDI (CC " + juce::String (cc) + ")");
    juce::Component::SafePointer<juce::Component> target (c);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (c), [this, param] (int r)
    {
        if (r == 1) { ed.proc.midiLearn (param); ed.setStatus ("MIDI Learn: move a controller for " + tg::meta (param).name); }
        else if (r == 2) { ed.proc.midiForget (param); ed.setStatus ("MIDI mapping removed"); }
    });
}

void MegaSynthEditor::showInitMenu()
{
    juce::PopupMenu m;
    const bool user = MegaSynthProcessor::hasUserInit();
    m.addItem (1, user ? "Load Init (your saved Init)" : "Load Init (clean: one saw, no effects)");
    m.addSeparator();
    m.addItem (2, "Save current patch as Init");
    m.addItem (3, "Reset Init to factory", user);
    m.addSeparator();
    m.addItem (4, "Reset every control to its default (the old Init)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&initBtn), [this] (int r)
    {
        if (r == 1) { proc.loadInitPatch(); setStatus ("Init loaded"); refreshPatchList(); }
        else if (r == 2) setStatus (proc.saveCurrentAsInit() ? "Saved as Init: new instances and the Init button start from this patch" : "Couldn't save the Init file");
        else if (r == 3) setStatus (proc.resetInitToFactory() ? "Init reset to the clean factory Init" : "Couldn't remove the saved Init");
        else if (r == 4) { proc.resetToDefaults(); setStatus ("Every control reset to its default"); refreshPatchList(); }
    });
}

MegaSynthEditor::~MegaSynthEditor()
{
    stopTimer();
    proc.getEngine().mixerMeters.store (false);
    setLookAndFeel (nullptr);
}

//==============================================================================
GroupPage::GroupPage (MegaSynthProcessor& p, const juce::StringArray& subNames, juce::Colour c) : labels (subNames)
{
    juce::StringArray upper;
    for (auto& n : labels) upper.add (n.toUpperCase());
    strip = std::make_unique<Segmented> (p, -1, upper, c);
    strip->onSelect = [this] (int k) { show (k); };
    addAndMakeVisible (*strip);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* pg = subs.add (new Page());
        addChildComponent (pg);
    }
    show (0);
}

GroupPage::~GroupPage() = default;

void GroupPage::show (int i)
{
    i = juce::jlimit (0, subs.size() - 1, i);
    shown = i;
    for (int k = 0; k < subs.size(); ++k) subs[k]->setVisible (k == i);
    strip->setLocal (i);
    if (onShow) onShow (i);
}

void GroupPage::resized()
{
    const int W = getWidth(), H = getHeight();
    int longest = 0;
    for (auto& n : labels) longest = juce::jmax (longest, n.length());
    const int stripW = juce::jmin (W - 20, labels.size() * juce::jmax (110, longest * 9 + 30));
    strip->setBounds (10, 4, stripW, kStripH - 8);
    for (auto* pg : subs) pg->setBounds (0, kStripH, W, H - kStripH);
}

bool MegaSynthEditor::showPage (const juce::String& nameIn)
{
    // names from before the tabs were regrouped still work
    static const std::map<juce::String, juce::String> oldNames {
        { "Effects", "Space" }, { "Filter & Env", "Filters" }, { "Env+", "Pitch Envelope" }, { "Mutation", "Note Mutation" },
        { "Macros/Scenes", "Macros & Scenes" }, { "DNA Seq", "DNA Sequencer" }, { "Sequencer", "Step Sequencer" } };
    juce::String name = nameIn;
    if (auto o = oldNames.find (name); o != oldNames.end() && pageIndex.find (name) == pageIndex.end()) name = o->second;
    auto it = pageIndex.find (name);
    if (it == pageIndex.end())
        for (auto e = pageIndex.begin(); e != pageIndex.end(); ++e) if (e->first.equalsIgnoreCase (name)) { it = e; break; }
    if (it == pageIndex.end()) return false;
    tabs.setCurrentTabIndex (it->second.tab);
    if (it->second.sub >= 0)
        if (auto* g = dynamic_cast<GroupPage*> (tabs.getTabContentComponent (it->second.tab))) g->show (it->second.sub);
    return true;
}

juce::StringArray MegaSynthEditor::allPageNames() const
{
    juce::StringArray a;
    for (int t = 0; t < tabs.getNumTabs(); ++t)
    {
        if (auto* g = dynamic_cast<GroupPage*> (tabs.getTabContentComponent (t))) for (auto& n : g->names()) a.add (n);
        else a.add (tabs.getTabNames()[t]);
    }
    return a;
}

juce::String MegaSynthEditor::currentPageName() const
{
    const int t = tabs.getCurrentTabIndex();
    if (auto* g = dynamic_cast<GroupPage*> (tabs.getTabContentComponent (t))) return g->names()[g->current()];
    return tabs.getTabNames()[t];
}

void MegaSynthEditor::buildPages()
{
    // The tabs, in signal-flow order. A tab with sub-tabs is a GroupPage; every page is found by its name.
    struct TabDef { const char* name; juce::StringArray subs; juce::Colour colour; };
    const TabDef layout[] = {
        { "Perform", { "Perform", "Performance FX" }, col::accent }, { "Overview", {}, col::accent },
        { "Oscillators", { "Osc 1-3 & Sub", "Wavetables", "Complex & SuperSaw", "Unison", "FM & Ring", "Granular Synth", "Break Chopper" }, col::osc },
        { "Mixer", {}, col::mixer }, { "Filters", {}, col::filter },
        { "Envelopes", { "Amp & Filter", "Mod Envelopes", "Pitch Envelope", "MSEG 1 & 2" }, col::env },
        { "FX & Mutation", { "Note Mutation", "Bus Mutation", "Space", "Dirt", "Motion", "Rack" }, col::fx },
        { "Modulation", { "LFOs", "Mod Matrix", "Assignments", "Macros & Scenes", "Cell Instability" }, col::mod },
        { "Sequencers", { "Step Sequencer", "Arp / Chord", "DNA Sequencer", "Break Sequencer" }, col::seq },
        { "Lab", { "Mutate / Lab", "Capture" }, col::wavetable } };
    for (auto& t : layout)
    {
        const int tabIndex = tabs.getNumTabs();
        if (t.subs.isEmpty())
        {
            auto* page = new Page();
            tabs.addTab (t.name, col::bg, page, true);
            pageIndex[t.name] = { tabIndex, -1, page };
        }
        else
        {
            auto* group = new GroupPage (proc, t.subs, t.colour);
            tabs.addTab (t.name, col::bg, group, true);
            for (int i = 0; i < group->numSubs(); ++i) pageIndex[t.subs[i]] = { tabIndex, i, group->sub (i) };
            if (pageIndex.find (t.name) == pageIndex.end()) pageIndex[t.name] = { tabIndex, -1, group->sub (0) };   // (a group can share its first page's name)
        }
    }
    auto addPage = [this] (const juce::String& name) -> Page*
    {
        auto* page = pageFor (name);
        jassert (page != nullptr);
        return page;
    };
    auto sec = [this] (Page* page, const juce::String& t, juce::Colour c) { return page->own (new Section (proc, t, c)); };

    // ---------------------------------------------------------------- Perform
    {
        // the controls you reach for while playing: macros, scenes, Mutate, the DNA Sequencer and the main sound knobs
        auto* page = addPage ("Perform");
        auto* macSec = sec (page, "Macros", col::accent);
        juce::Array<MacroCell*> cells;
        for (int i = 0; i < 8; ++i) { auto* m = page->own (new MacroCell (proc, i)); macroCells.add (m); cells.add (m); }
        auto* scSec = sec (page, "Scenes", col::mod);
        auto* pad = page->own (new XYPad (proc));
        perfXY = pad;
        perfMorphBtn.setColour (juce::ToggleButton::tickColourId, col::mod);
        perfMorphAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_sceneMorph], tagParam (perfMorphBtn, P_sceneMorph));
        page->addAndMakeVisible (perfMorphBtn);

        auto* mu = sec (page, "Mutate", col::wavetable);
        mu->knob (P_mutAmount, "Mutate");
        auto* seedBox = new juce::Component();
        perfSeed.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        perfSeed.setJustificationType (juce::Justification::centred);
        perfSeed.setColour (juce::Label::textColourId, col::text);
        perfSeed.setText ("Seed #" + juce::String (juce::roundToInt (proc.param (P_mutSeed)->convertFrom0to1 (proc.param (P_mutSeed)->getValue()))), juce::dontSendNotification);
        seedBox->addAndMakeVisible (perfSeed);
        for (auto* b : { &perfSeedDown, &perfSeedUp, &perfNewSeed, &perfCommit }) seedBox->addAndMakeVisible (*b);
        perfSeedDown.onClick = [this] { proc.mutateStepSeed (-1); };
        perfSeedUp.onClick = [this] { proc.mutateStepSeed (1); };
        perfNewSeed.onClick = [this] { proc.mutateNewSeed(); };
        perfCommit.onClick = [this] { proc.commitMutation(); setStatus ("Mutation committed"); };
        perfSeed.setBounds (30, 0, 120, 30); perfSeedDown.setBounds (0, 0, 28, 30); perfSeedUp.setBounds (152, 0, 28, 30);
        perfNewSeed.setBounds (0, 38, 88, 28); perfCommit.setBounds (92, 38, 88, 28);
        mu->add (seedBox, 180, 70);

        auto* ds = sec (page, "DNA Sequencer", col::wavetable);
        auto* dsOn = new juce::ToggleButton ("On");
        dsOn->setColour (juce::ToggleButton::tickColourId, col::wavetable);
        ds->add (dsOn, 54, 40);
        perfAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, kParamIds[P_dsOn], tagParam (*dsOn, P_dsOn)));
        auto* pat = new Segmented (proc, P_dsPattern, { "A", "B", "C", "D" }, col::wavetable);
        ds->add (pat, 150, 30);
        ds->newRow();
        ds->knob (P_dsSwing, "Swing");
        ds->knob (P_dsDensity, "Density");
        ds->knob (P_dsDepth, "Depth");

        auto* snd = sec (page, "Sound", col::filter);
        for (auto [idx, n] : { std::pair<int, const char*> { P_filterCutoff, "Cutoff" }, { P_filterRes, "Resonance" }, { P_filterDrive, "Drive" },
                               { P_fEnvAmt, "Env Amt" }, { P_filter2Cutoff, "F2 Cutoff" }, { P_filterBalance, "F1 / F2" } })
            snd->knob (idx, n);
        snd->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_delayMix, "Delay" }, { P_reverbMix, "Reverb" }, { P_vshDepth, "Pump" },
                               { P_mbcDepth, "OTT" }, { P_smpDrive, "Crunch" } })
            snd->knob (idx, n);
        auto* rpt = new StutterPad (proc);
        snd->add (rpt, 108, 70);
        auto* arpS = sec (page, "Arp / Chord / Pitch / Unison", col::seq);
        arpS->add (new ParamToggle (proc, P_arpOn, "Arp", col::seq), 64, 40);
        arpS->add (new ParamToggle (proc, P_arpLatch, "Latch", col::seq), 74, 40);
        arpS->choice (P_arpRate, "Rate", 100);
        arpS->choice (P_arpMode, "Mode", 96);
        arpS->add (new ParamToggle (proc, P_chdOn, "Chord", col::seq), 74, 40);
        arpS->newRow();
        arpS->knob (P_arpGate, "Gate", 64);
        arpS->knob (P_pEnvAmt, "Pitch Env", 64);
        arpS->knob (P_pEnvD, "Env Decay", 64);
        arpS->knob (P_uni1Voices, "Osc 1 Unison", 72);
        arpS->knob (P_uni1Detune, "Detune", 64);
        auto* help = page->own (new juce::Label ({}, "Right-click any knob for MIDI Learn, modulation and macros. "
                                                     "Pump, OTT and Crunch need the Volume Shaper, Multiband and Vintage Sampler switched on (FX & Mutation > Rack); "
                                                     "the repeat pad needs Beat Repeat."));
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (11.5f)));
        page->onResize = [this, page, macSec, cells, scSec, pad, mu, ds, snd, arpS, help]
        {
            const int g = 10, W = page->getWidth(), H = page->getHeight();
            macSec->setBounds (g, g, W - 2 * g, 160);
            const int cw = (W - 2 * g - 20) / 8;
            for (int i = 0; i < cells.size(); ++i) cells[i]->setBounds (g + 10 + i * cw, g + 32, cw - 8, 120);
            scSec->setBounds (g, 180, 300, H - 180 - g - 22);
            pad->setBounds (g + 20, 214, 260, 260);
            perfMorphBtn.setBounds (g + 20, 482, 200, 26);
            mu->setBounds (320, 180, 330, 150);
            ds->setBounds (320, 340, 330, H - 340 - g - 22);
            snd->setBounds (660, 180, W - g - 660, 212);
            arpS->setBounds (660, 400, W - g - 660, H - 400 - g - 22);
            help->setBounds (g, H - 28, W - 2 * g, 20);
        };
    }

    // ---------------------------------------------------------------- Perform > Performance FX
    {
        auto* page = addPage ("Performance FX");
        auto* padSec = sec (page, "Pads  (Hold a pad to play it; with Latch on, click it on and off. A new pad waits for the next line of the "
                                  "Quantise grid, then fades in over 4 ms; letting go fades it out at once. Several can play together, "
                                  "in the order numbered on the pads: 1 the time pads (the last one pressed wins), 2 Pitch Down, 3 Stutter, "
                                  "4 Trance Gate, 5 DJ Filter, 6 Bitcrush, 7 Vinyl, 8 Echo Throw, 9 Reverb Throw. Echo and Reverb Throw ring on "
                                  "after you let go. Right-click a pad for MIDI Learn; MIDI Keys plays them from 13 keys.)", col::fx);
        auto* ticker = page->own (new PerfTicker());
        juce::Array<PerfPad*> pads;
        for (int k = 0; k < kPfPads; ++k)
        {
            auto* pd = page->own (new PerfPad (proc, k, k < PerfFx::kTimePads ? col::seq : (k >= PerfFx::Echo ? col::mod : col::fx)));
            pads.add (pd); ticker->pads.add (pd);
        }
        auto* latch = page->own (new ParamToggle (proc, P_pfLatch, "Latch", col::fx));
        auto* quant = page->own (new Choice (proc, P_pfQuant, "Quantise"));
        auto* keys = page->own (new Choice (proc, P_pfKeys, "MIDI Keys"));
        auto* order = page->own (new juce::Label ({}, "Order: 1 Time (Repeat / Reverse / Tape Stop / Tape Start / Backspin)  >  2 Pitch Down  >  3 Stutter  >  4 Trance Gate  >  "
                                                     "5 DJ Filter  >  6 Bitcrush  >  7 Vinyl  >  8 Echo Throw  >  9 Reverb Throw  >  Master Filter"));
        order->setColour (juce::Label::textColourId, col::muted);
        order->setFont (juce::Font (juce::FontOptions (11.0f)));

        auto* xySec = sec (page, "XY Pads  (Kaoss-style: X and Y are Mod Matrix sources, so each axis can drive any Performance FX setting or any "
                                 "Mod Matrix destination. Assign adds a route (you can change its depth in the Mod Matrix). Spring: back to the centre "
                                 "when you let go; off, it stays where you leave it.)", col::mod);
        juce::Array<PerfXY*> xys;
        juce::Array<juce::Component*> xyCtl;
        for (int k = 0; k < 2; ++k)
        {
            auto* xy = page->own (new PerfXY (proc, k));
            xys.add (xy); ticker->xys.add (xy);
            auto* ax = page->own (new juce::TextButton ("Assign X"));
            auto* ay = page->own (new juce::TextButton ("Assign Y"));
            ax->onClick = [xy, ax] { xy->showAssign (true, ax); };
            ay->onClick = [xy, ay] { xy->showAssign (false, ay); };
            auto* sp = page->own (new ParamToggle (proc, P_pfSpring1 + k, "Spring", col::mod));
            xyCtl.add (ax); xyCtl.add (ay); xyCtl.add (sp);
        }

        auto* set = sec (page, "Pad Settings", col::fx);
        set->choice (P_pfRollRate, "Repeat Length", 104);
        set->knob (P_pfTapeTime, "Tape Time", 66);
        set->knob (P_pfPitchAmt, "Pitch Amt", 66);
        set->knob (P_pfPitchGlide, "Glide", 66);
        set->choice (P_pfStutterRate, "Stutter Rate", 96);
        set->choice (P_pfGatePattern, "Gate Pattern", 118);
        set->knob (P_pfGateDepth, "Gate Depth", 66);
        set->knob (P_pfDjPos, "DJ Filter", 66);
        set->knob (P_pfDjRes, "DJ Res", 66);
        set->newRow();
        set->knob (P_pfCrushBits, "Crush Bits", 66);
        set->knob (P_pfCrushRate, "Crush Rate", 66);
        set->knob (P_pfVinylAmt, "Vinyl", 66);
        set->choice (P_pfEchoTime, "Echo Time", 130);
        set->knob (P_pfEchoFb, "Echo Fdbk", 66);
        set->knob (P_pfEchoMix, "Echo Level", 66);
        set->knob (P_pfVerbSize, "Verb Size", 66);
        set->knob (P_pfVerbMix, "Verb Level", 66);
        set->add (new ParamToggle (proc, P_pfVerbFreeze, "Freeze", col::fx), 80, 40);

        page->onResize = [page, padSec, pads, latch, quant, keys, order, xySec, xys, xyCtl, set]
        {
            const int g = 10, W = page->getWidth();
            const int padW = 760;
            padSec->setBounds (g, g, padW, 300);
            const int cols = 5, pw = (padW - 20 - (cols - 1) * 8) / cols, ph = 64;
            for (int k = 0; k < pads.size(); ++k)
                pads[k]->setBounds (g + 10 + (k % cols) * (pw + 8), g + 34 + (k / cols) * (ph + 8), pw, ph);
            // the last row: Vinyl, Echo, Reverb, then the switches
            const int ry = g + 34 + 2 * (ph + 8), cx = g + 10 + 3 * (pw + 8);
            latch->setBounds (cx, ry + 16, 76, 30);
            quant->setBounds (cx + 80, ry, pw * 2 + 8 - 80, 44);
            keys->setBounds (g + 10, ry + ph + 10, 220, 44);
            order->setBounds (g + 240, ry + ph + 18, padW - 250, 34);
            order->setMinimumHorizontalScale (0.8f);
            const int xx = g + padW + g, xw = W - xx - g;
            xySec->setBounds (xx, g, xw, 300);
            const int side = juce::jmin ((xw - 30) / 2, 220);
            for (int k = 0; k < 2; ++k)
            {
                const int x0 = xx + 10 + k * (side + 10);
                xys[k]->setBounds (x0, g + 34, side, side - 2);
                const int by = g + 34 + side + 4, bw = (side - 8) / 3;
                xyCtl[k * 3]->setBounds (x0, by, bw, 24);
                xyCtl[k * 3 + 1]->setBounds (x0 + bw + 4, by, bw, 24);
                xyCtl[k * 3 + 2]->setBounds (x0 + 2 * (bw + 4), by - 3, bw + 6, 30);
            }
            set->setBounds (g, g + 310, W - 2 * g, 216);
        };
    }

    // ---------------------------------------------------------------- Overview
    {
        auto* page = addPage ("Overview");
        auto* flow = page->own (new SignalFlowView (proc));
        auto* net = page->own (new NetworkView (proc));
        auto* spec = page->own (new SpectrumView (proc));
        auto* dna = page->own (new DnaStrandView (proc));
        flowView = flow; netView = net; specView = spec; dnaView = dna;
        flow->onOpen = [this] (const juce::String& name) { showPage (name); };
        page->onResize = [page, flow, net, spec, dna]
        {
            const int g = 10, W = page->getWidth();
            flow->setBounds (g, g, W - 2 * g, 150);
            net->setBounds (g, 170, 690, 270);
            spec->setBounds (g + 700, 170, W - 2 * g - 700, 270);
            dna->setBounds (g, 450, W - 2 * g, 146);
        };
    }

    // ---------------------------------------------------------------- Oscillators
    {
        auto* page = addPage ("Osc 1-3 & Sub");
        auto* pageWt = addPage ("Wavetables");
        auto* pageCx = addPage ("Complex & SuperSaw");
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
            wtS[k] = sec (pageWt, k == 0 ? "Wavetable 1  (Osc 4)" : "Wavetable 2", col::wavetable);
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

        auto* cx = sec (pageCx, "Oscillator 5 / Complex", col::complex);
        cx->choice (P_complexWaveA, "Primary Wave A", 170);
        cx->choice (P_complexWaveB, "Mod Wave B", 170);
        cx->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_complexOct, "Octave" }, { P_complexSemi, "Semi" }, { P_complexDetune, "Detune" },
                               { P_complexRatio, "Mod Ratio" }, { P_complexFm, "FM Index" }, { P_complexShape, "Wavefold" },
                               { P_complexMix, "A/B Blend" }, { P_complexGain, "Level" } })
            cx->knob (idx, n, 64);

        auto* ss = sec (pageCx, "Oscillator 6 / Unison SuperSaw", col::supersaw);
        for (auto [idx, n] : { std::pair<int, const char*> { P_supersawOct, "Octave" }, { P_supersawSemi, "Semi" }, { P_supersawDetune, "Detune" },
                               { P_supersawVoices, "Voices" }, { P_supersawSpread, "Spread" }, { P_supersawStereo, "Stereo" },
                               { P_supersawDrift, "Drift" }, { P_supersawGain, "Level" } })
            ss->knob (idx, n, 64);

        page->onResize = [page, o, sub]
        {
            const int W = page->getWidth(), g = 10;
            const int w4 = (W - g * 5) / 4;
            for (int i = 0; i < 3; ++i) o[i]->setBounds (g + i * (w4 + g), g, w4, 170);
            sub->setBounds (g + 3 * (w4 + g), g, w4, 170);
        };
        pageWt->onResize = [pageWt, wtS]
        {
            const int W = pageWt->getWidth(), g = 10, w2 = (W - g * 3) / 2;
            wtS[0]->setBounds (g, g, w2, 208);
            wtS[1]->setBounds (g * 2 + w2, g, w2, 208);
        };
        pageCx->onResize = [pageCx, cx, ss]
        {
            const int W = pageCx->getWidth(), g = 10;
            cx->setBounds (g, g, W - 2 * g, 150);
            ss->setBounds (g, g + 160, W - 2 * g, 150);
        };
    }

    // ---------------------------------------------------------------- Oscillators > Granular Synth
    {
        auto* page = addPage ("Granular Synth");
        const int slot = MegaSynthProcessor::kSlotGranular;
        auto* smp = sec (page, "Sample  (Each note plays its own cloud of grains from this sample. Click or drag on the wave to set Position. "
                               "The sample is saved inside the project and patch; anything longer than 20 s is cut to 20 s, which keeps a "
                               "project to about 10 MB at most. Quality limits the grains per note: Eco 8, Normal 24, High 48.)", col::supersaw);
        auto* wave = page->own (new SampleWaveView (proc, slot, col::supersaw));
        wave->setTooltip ("Click or drag to set Position");
        auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
        wave->markers = [this, plain, slot]
        {
            std::vector<SampleWaveView::Marker> m;
            m.push_back ({ (double) plain (P_gsPosition), col::text, "Pos", true });
            const auto* w = proc.getSample (slot);
            const double ph = proc.getEngine().granPlayheadShown.load();
            if (w != nullptr && ph >= 0.0) m.push_back ({ ph / std::max (1, w->length), col::supersaw.brighter (0.5f), {}, false });
            return m;
        };
        wave->shade = [this, plain, slot]
        {
            const auto* w = proc.getSample (slot);
            if (w == nullptr) return juce::Range<double>();
            const double p0 = plain (P_gsPosition), spray = plain (P_gsSpray) * 0.001 / std::max (0.01, w->duration());
            return juce::Range<double> (std::max (0.0, p0 - spray), std::min (1.0, p0 + spray + plain (P_gsSize) * 0.001 / std::max (0.01, w->duration())));
        };
        auto setPos = [this] (double f, const juce::MouseEvent&) { proc.param (P_gsPosition)->setValueNotifyingHost ((float) f); };
        wave->onClick = setPos; wave->onDrag = setPos;
        auto* load = page->own (new juce::TextButton ("Load Sample"));
        auto* clear = page->own (new juce::TextButton ("Clear"));
        auto* info = page->own (new juce::Label());
        info->setColour (juce::Label::textColourId, col::muted);
        info->setFont (juce::Font (juce::FontOptions (11.5f)));
        granInfo = info;
        info->setText (sampleInfoText (proc, slot), juce::dontSendNotification);
        load->onClick = [this, slot]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a sample for the Granular Synth", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, slot] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f == juce::File()) return;
                                      const bool ok = proc.loadSampleFile (f, slot);
                                      setStatus (ok ? "Sample loaded into the Granular Synth" : "Couldn't read that audio file");
                                      if (ok && proc.param (P_gsOn)->getValue() < 0.5f) proc.param (P_gsOn)->setValueNotifyingHost (1.0f);
                                      proc.pushHistory ("Load granular sample");
                                  });
        };
        clear->onClick = [this, slot] { proc.clearSample (slot); proc.pushHistory ("Clear granular sample"); setStatus ("Granular Synth sample cleared"); };
        auto* on = page->own (new ParamToggle (proc, P_gsOn, "On", col::supersaw));

        auto* gr = sec (page, "Grains", col::supersaw);
        gr->knob (P_gsPosition, "Position", 68);
        gr->knob (P_gsScan, "Scan", 68);
        gr->knob (P_gsSpray, "Spray", 68);
        gr->knob (P_gsSize, "Grain Size", 68);
        gr->knob (P_gsDensity, "Density", 68);
        gr->choice (P_gsShape, "Grain Shape", 130);
        gr->knob (P_gsReverse, "Reverse", 68);
        gr->knob (P_gsSpread, "Spread", 68);
        auto* pit = sec (page, "Pitch", col::osc);
        pit->knob (P_gsSemi, "Pitch", 64);
        pit->knob (P_gsCents, "Fine", 64);
        pit->knob (P_gsPitchRand, "Random", 64);
        pit->knob (P_gsRoot, "Root Note", 64);
        pit->add (new ParamToggle (proc, P_gsKeyTrack, "Key Track", col::osc), 100, 40);
        auto* out = sec (page, "Output  (Level goes into the mix before the filters, with its own strip on the Mixer page. It can also stand in for "
                               "DNA Splice source A or B.)", col::mixer);
        out->knob (P_gsLevel, "Level", 64);
        out->knob (P_mxGranPan, "Pan", 64);
        out->add (new ParamToggle (proc, P_mxGranMute, "Mute", col::mixer), 70, 40);
        out->add (new ParamToggle (proc, P_mxGranSolo, "Solo", col::mixer), 70, 40);
        out->choice (P_gsDna, "DNA Splice", 190);
        page->onResize = [page, smp, wave, load, clear, info, on, gr, pit, out]
        {
            const int g = 10, W = page->getWidth();
            smp->setBounds (g, g, W - 2 * g, 228);
            wave->setBounds (g + 10, g + 34, W - 2 * g - 20, 150);
            on->setBounds (g + 10, g + 190, 60, 28);
            load->setBounds (g + 76, g + 192, 110, 26);
            clear->setBounds (g + 192, g + 192, 70, 26);
            info->setBounds (g + 270, g + 192, W - 2 * g - 280, 26);
            gr->setBounds (g, g + 238, W - 2 * g, 130);
            pit->setBounds (g, g + 378, 470, 130);
            out->setBounds (g + 480, g + 378, W - 2 * g - 480, 130);
        };
    }

    // ---------------------------------------------------------------- Oscillators > Break Chopper
    {
        auto* page = addPage ("Break Chopper");
        auto* ticker = page->own (new StoreTicker());
        auto selected = std::make_shared<int> (0);
        auto activeOf = [this] { return juce::jlimit (0, 2, juce::roundToInt (proc.param (P_bcActive)->convertFrom0to1 (proc.param (P_bcActive)->getValue())) - 1); };
        auto* smp = sec (page, "Breaks  (Three breaks, each with its own sample, slicing, tempo, slice settings, stretch and colour. "
                               "The selector picks the Active Break: the one you edit here and the one the keys play. All three share one key zone "
                               "from First Key up (C1 by default); the three Break Select keys just below it (A0, A#0, B0) switch the Active Break "
                               "while you play. Click a slice's label to hear and edit it. Drag a marker to move it (Alt: fine, no snap; arrow keys nudge); the "
                               "triangles along the bottom are the slice's trim. Mouse wheel or pinch zooms, Shift+wheel scrolls. In Manual markers mode a "
                               "click adds a marker, a double-click splits a slice, right-click or Delete removes one. Each break is saved inside the "
                               "project and patch, cut to 30 s at most.)", col::complex);
        auto* selector = page->own (new Segmented (proc, -1, { "Break 1", "Break 2", "Break 3" }, breakColour (0)));
        tagParam (*selector, P_bcActive);
        selector->setTooltip ("The Active Break: the one shown and edited here, and the one the keys play");
        selector->onSelect = [this] (int k) { auto* prm = proc.param (P_bcActive); prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (k + 1))); prm->endChangeGesture(); };
        juce::Array<SampleWaveView*> minis;
        for (int k = 0; k < 3; ++k)
        {
            auto* mv = page->own (new SampleWaveView (proc, MegaSynthProcessor::breakSlot (k), breakColour (k)));
            mv->emptyText = "Break " + juce::String (k + 1) + ": empty";
            mv->outlineShade = true;
            minis.add (mv);
        }
        auto* wave = page->own (new SampleWaveView (proc, MegaSynthProcessor::breakSlot (0), breakColour (0)));
        for (int k = 0; k < 3; ++k)
        {
            auto* mv = minis[k];
            // the Active Break's small wave shows where the zoomed view is; click or drag on it to move the view
            mv->shade = [this, wave, k, activeOf] { return k == activeOf() && wave->viewLen < 0.999 ? juce::Range<double> (wave->viewStart, wave->viewStart + wave->viewLen) : juce::Range<double>(); };
            mv->onClick = [selector, wave, k, activeOf] (double f, const juce::MouseEvent&)
            {
                if (k == activeOf()) wave->setView (f - wave->viewLen * 0.5, wave->viewLen);
                else if (selector->onSelect) selector->onSelect (k);
            };
            mv->onDrag = [wave, k, activeOf] (double f, const juce::MouseEvent&) { if (k == activeOf()) wave->setView (f - wave->viewLen * 0.5, wave->viewLen); };
            mv->setTooltip ("Break " + juce::String (k + 1) + ": click to make it the Active Break. On the Active Break, the frame shows the zoomed view: click or drag to move it.");
        }
        wave->emptyText = "Load a break (WAV, AIFF, FLAC or MP3)";
        wave->markers = [this, selected, activeOf]
        {
            std::vector<SampleWaveView::Marker> m;
            const int a = activeOf();
            const auto d = proc.breakStore.get();
            const auto& b = d.brk[a];
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w == nullptr || b.nSlices <= 0) return m;
            const int kb = juce::roundToInt (proc.param (P_bcKeyBase)->convertFrom0to1 (proc.param (P_bcKeyBase)->getValue()));
            for (int k = 0; k < b.nSlices; ++k)
                m.push_back ({ (double) b.start[k] / w->length, k == *selected ? col::text : breakColour (a).brighter (0.4f),
                               juce::MidiMessage::getMidiNoteName (kb + k, true, true, 3), k == *selected });
            // the selected slice's trim start and end: handles along the bottom
            const int sk = juce::jlimit (0, b.nSlices - 1, *selected);
            const double s0 = b.start[sk], len = b.start[sk + 1] - b.start[sk];
            m.push_back ({ (s0 + b.slice[sk].trimStart * len) / w->length, col::supersaw, {}, false, true });
            m.push_back ({ (s0 + b.slice[sk].trimEnd * len) / w->length, col::supersaw, {}, false, true });
            const double ph = proc.getEngine().breakPlayheadShown.load();
            if (ph >= 0.0) m.push_back ({ ph, col::supersaw, {}, false });
            return m;
        };
        wave->zoomable = true;
        wave->shade = [this, selected, activeOf]
        {
            const int a = activeOf();
            const auto d = proc.breakStore.get(); const auto& b = d.brk[a];
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w == nullptr || *selected >= b.nSlices) return juce::Range<double>();
            return juce::Range<double> ((double) b.start[*selected] / w->length, (double) b.start[*selected + 1] / w->length);
        };
        auto selMarker = std::make_shared<int> (-1);
        auto isManual = [this, activeOf] { const int a = activeOf(); return juce::roundToInt (proc.param (breakParams (a).sliceMode)->convertFrom0to1 (proc.param (breakParams (a).sliceMode)->getValue())) == 5; };
        auto selectSliceAt = [this, selected, activeOf] (double f)
        {
            const int a = activeOf();
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w == nullptr) return;
            const auto d = proc.breakStore.get(); const auto& b = d.brk[a];
            for (int k = 0; k < b.nSlices; ++k)
                if (f * w->length >= b.start[k] && f * w->length < b.start[k + 1]) { *selected = k; proc.breakAudition.store (a * 100 + k); break; }
        };
        // the marker within a few pixels of a click, or -1
        auto markerNear = [this, activeOf, wave] (double f)
        {
            const auto d = proc.breakStore.get(); const auto& b = d.brk[activeOf()];
            const float x = wave->xFor (f);
            int best = -1; float bestD = 5.0f;
            for (int i = 0; i < b.nMarkers; ++i) if (std::abs (wave->xFor (b.markers[i]) - x) < bestD) { bestD = std::abs (wave->xFor (b.markers[i]) - x); best = i; }
            return best;
        };
        wave->onClick = [this, selectSliceAt, isManual, markerNear, selMarker, activeOf] (double f, const juce::MouseEvent& e)
        {
            // the label strip along the top always picks (and plays) a slice; so does any click outside Manual mode
            if (! isManual() || e.position.y < 18.0f) { selectSliceAt (f); return; }
            const int near = markerNear (f);
            if (near >= 0) { *selMarker = near; selectSliceAt (f); return; }   // a marker: select it (Delete removes it)
            const int a = activeOf();
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w != nullptr && proc.addChopMarker (a, f * w->length, ! e.mods.isAltDown())) { proc.pushHistory ("Add marker"); selectSliceAt (f); }
        };
        wave->onDoubleClick = [this, isManual, activeOf] (double f)
        {
            // split the slice under the pointer in half
            if (! isManual()) return;
            const int a = activeOf();
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w == nullptr) return;
            const auto d = proc.breakStore.get(); const auto& b = d.brk[a];
            for (int k = 0; k < b.nSlices; ++k)
                if (f * w->length >= b.start[k] && f * w->length < b.start[k + 1]) { proc.addChopMarker (a, 0.5 * (b.start[k] + b.start[k + 1]), false); break; }
        };
        auto removeMarker = [this, activeOf] (int idx)
        {
            const int a = activeOf();
            proc.breakStore.edit ([a, idx] (tg::BreakData& d)
            {
                auto& b = d.brk[a];
                if (idx < 0 || idx >= b.nMarkers) return;
                for (int i = idx; i + 1 < b.nMarkers; ++i) b.markers[i] = b.markers[i + 1];
                --b.nMarkers;
            });
            proc.refreshBreakAnalysis();
        };
        wave->onRightClick = [isManual, markerNear, removeMarker, this, activeOf] (double f)
        {
            if (! isManual()) return;
            const auto d = proc.breakStore.get(); const auto& b = d.brk[activeOf()];
            int best = 0; for (int i = 1; i < b.nMarkers; ++i) if (std::abs (b.markers[i] - f) < std::abs (b.markers[best] - f)) best = i;
            if (b.nMarkers > 0) removeMarker (best);
            juce::ignoreUnused (markerNear);
        };
        wave->onKey = [this, selMarker, removeMarker, isManual, activeOf] (const juce::KeyPress& k)
        {
            if (k == juce::KeyPress::spaceKey && proc.param (P_bcChopMode)->getValue() > 0.5f) { proc.tapChop(); return true; }
            if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && isManual() && *selMarker >= 0) { removeMarker (*selMarker); *selMarker = -1; return true; }
            // the arrow keys nudge the selected marker: 1 ms, or 10 ms with Shift
            const int kc = k.getKeyCode();
            if ((kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey) && isManual() && *selMarker >= 0)
            {
                const double ms = (kc == juce::KeyPress::leftKey ? -1.0 : 1.0) * (k.getModifiers().isShiftDown() ? 10.0 : 1.0);
                proc.nudgeMarker (activeOf(), *selMarker, ms);
                return true;
            }
            return false;
        };
        // dragging a marker (left / right) or the selected slice's trim handles (along the bottom)
        struct DragState { int kind = 0, idx = -1, slice = 0; bool moved = false; float x0 = 0.0f; };
        auto drag = std::make_shared<DragState>();
        wave->onDragStart = [this, wave, drag, selected, selMarker, isManual, activeOf] (double f, const juce::MouseEvent& e)
        {
            const int a = activeOf();
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w == nullptr) return false;
            const auto d = proc.breakStore.get(); const auto& b = d.brk[a];
            if (b.nSlices <= 0) return false;
            *drag = {}; drag->x0 = e.position.x;
            const float x = wave->xFor (f);
            // a trim handle (the bottom strip)
            if (e.position.y > wave->inner().getBottom() - 14.0f)
            {
                const int sk = juce::jlimit (0, b.nSlices - 1, *selected);
                const double s0 = b.start[sk], len = b.start[sk + 1] - b.start[sk];
                const float xs = wave->xFor ((s0 + b.slice[sk].trimStart * len) / w->length), xe = wave->xFor ((s0 + b.slice[sk].trimEnd * len) / w->length);
                const float ds = std::abs (xs - x), de = std::abs (xe - x);
                if (std::min (ds, de) < 7.0f) { drag->kind = de < ds ? 3 : 2; drag->slice = sk; return true; }
            }
            if (e.position.y < 18.0f) return false;   // the label strip picks slices
            // a marker: in Manual mode the markers themselves; otherwise a slice start (converted to Manual once it moves)
            float bestD = 5.0f; int best = -1;
            if (isManual()) { for (int i = 0; i < b.nMarkers; ++i) { const float dd = std::abs (wave->xFor (b.markers[i]) - x); if (dd < bestD) { bestD = dd; best = i; } } }
            else for (int k = 1; k < b.nSlices; ++k) { const float dd = std::abs (wave->xFor ((double) b.start[k] / w->length) - x); if (dd < bestD) { bestD = dd; best = k - 1; } }
            if (best < 0) return false;
            drag->kind = 1; drag->idx = best; *selMarker = best;
            return true;
        };
        wave->onDragMove = [this, drag, isManual, activeOf] (double f, const juce::MouseEvent& e)
        {
            if (! drag->moved && std::abs (e.position.x - drag->x0) < 3.0f) return;   // a click, not a drag
            const int a = activeOf();
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w == nullptr) return;
            if (drag->kind == 1)
            {
                if (! isManual()) proc.markersFromSlices (a);   // Transient or Equal: to Manual markers first
                proc.moveMarker (a, drag->idx, f * w->length, ! e.mods.isAltDown());   // Alt: fine, no snap
            }
            else if (drag->kind >= 2)
            {
                const auto d = proc.breakStore.get(); const auto& b = d.brk[a];
                const double s0 = b.start[drag->slice], len = std::max (1, b.start[drag->slice + 1] - b.start[drag->slice]);
                proc.setSliceTrim (a, drag->slice, drag->kind == 3, (float) ((f * w->length - s0) / len));
            }
            drag->moved = true;
        };
        wave->onDragEnd = [this, drag, selectSliceAt, wave]
        {
            if (drag->moved) proc.commitMarkerEdit();   // one undo step per drag
            else if (drag->kind == 1)
            {
                // a click on a marker: select it, and the slice after it
                const auto d = proc.breakStore.get(); juce::ignoreUnused (d);
                selectSliceAt (wave->fracAtX (drag->x0 + 3.0f));
            }
            *drag = {};
        };
        auto* load = page->own (new juce::TextButton ("Load"));
        auto* clear = page->own (new juce::TextButton ("Clear"));
        auto* toMarkers = page->own (new juce::TextButton ("Slices to markers"));
        auto* matchTempo = page->own (new juce::TextButton ("Match Tempo"));
        auto* matchLevels = page->own (new juce::TextButton ("Match Levels"));
        toMarkers->setTooltip ("Manual markers mode, starting from the slices you have now");
        matchTempo->setTooltip ("Tempo Sync on: every break is stretched to the song's tempo");
        matchLevels->setTooltip ("Sets the three Break levels so the loaded breaks are about equally loud");
        auto* info = page->own (new juce::Label());
        info->setColour (juce::Label::textColourId, col::muted);
        info->setFont (juce::Font (juce::FontOptions (11.0f)));
        info->setJustificationType (juce::Justification::topLeft);
        load->onClick = [this, activeOf]
        {
            const int a = activeOf();
            chooser = std::make_unique<juce::FileChooser> ("Load Break " + juce::String (a + 1), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, a] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f == juce::File()) return;
                                      const bool ok = proc.loadSampleFile (f, MegaSynthProcessor::breakSlot (a));
                                      setStatus (ok ? "Loaded into Break " + juce::String (a + 1) : "Couldn't read that audio file");
                                      if (ok && proc.param (P_bcOn)->getValue() < 0.5f) proc.param (P_bcOn)->setValueNotifyingHost (1.0f);
                                      proc.pushHistory ("Load break");
                                  });
        };
        clear->onClick = [this, activeOf] { const int a = activeOf(); proc.clearSample (MegaSynthProcessor::breakSlot (a)); proc.pushHistory ("Clear break"); setStatus ("Break " + juce::String (a + 1) + " cleared"); };
        toMarkers->onClick = [this, activeOf]
        {
            const int a = activeOf();
            const auto* w = proc.getSample (MegaSynthProcessor::breakSlot (a));
            if (w == nullptr) return;
            proc.breakStore.edit ([w, a] (tg::BreakData& d)
            {
                auto& b = d.brk[a];
                b.nMarkers = 0;
                for (int k = 1; k < b.nSlices && b.nMarkers < tg::BreakSlot::kSlices - 1; ++k) b.markers[b.nMarkers++] = (float) b.start[k] / (float) w->length;
            });
            auto* prm = proc.param (breakParams (a).sliceMode);
            prm->setValueNotifyingHost (prm->convertTo0to1 (5.0f));
            proc.refreshBreakAnalysis();
        };
        matchTempo->onClick = [this] { proc.param (P_bcSync)->setValueNotifyingHost (1.0f); setStatus ("Tempo Sync on: every break follows the song's tempo"); };
        matchLevels->onClick = [this] { setStatus (proc.matchBreakLevels() ? "Break levels matched" : "Load at least two breaks to match their levels"); };
        auto* on = page->own (new ParamToggle (proc, P_bcOn, "On", col::complex));
        // ---- chop mode: play the break and tap keys, pads, the space bar or Tap to mark slices
        auto* chop = page->own (new ParamToggle (proc, P_bcChopMode, "Chop", col::supersaw));
        chop->setTooltip ("Chop mode: the break plays; every key, pad or space bar (or Tap) marks a slice where you hit it");
        auto* play = page->own (new juce::TextButton ("Play"));
        auto* tap = page->own (new juce::TextButton ("Tap"));
        auto* undoChop = page->own (new juce::TextButton ("Undo chop"));
        auto* clearMk = page->own (new juce::TextButton ("Clear marks"));
        auto* snapAll = page->own (new juce::TextButton ("Snap all"));
        auto* snapTr = page->own (new ParamToggle (proc, P_bcChopSnapTr, "Transient", col::supersaw));
        auto* snapZero = page->own (new ParamToggle (proc, P_bcChopSnapZero, "Zero", col::supersaw));
        auto* latency = page->own (new juce::Slider (juce::Slider::LinearBar, juce::Slider::TextBoxLeft));
        latencyAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kParamIds[P_bcChopLatency], tagParam (*latency, P_bcChopLatency));
        latency->textFromValueFunction = [] (double v) { return "Latency " + juce::String (v, 1) + " ms"; };
        latency->setColour (juce::Slider::trackColourId, col::supersaw.withAlpha (0.5f));
        latency->setTooltip ("Tap Latency: how long your audio interface takes to play what the plug-in makes. Taps are moved back by this much.");
        latency->updateText();
        play->onClick = [this] { proc.breakPreviewReq.store (proc.previewIsPlaying() ? -2.0 : 0.0); };
        tap->onClick = [this] { proc.tapChop(); };
        undoChop->onClick = [this] { if (! proc.undoLastChop()) setStatus ("No chops to undo"); };
        clearMk->onClick = [this, activeOf] { proc.clearMarkers (activeOf()); proc.pushHistory ("Clear markers"); };
        clearMk->setTooltip ("Clear markers: the whole break is one slice again");
        snapAll->setTooltip ("Snap all to transients: every marker moves to the nearest transient within 20 ms");
        snapAll->onClick = [this, activeOf] { proc.snapAllMarkers (activeOf()); proc.pushHistory ("Snap markers"); };
        auto* zoomOut = page->own (new juce::TextButton ("-"));
        auto* zoomIn = page->own (new juce::TextButton ("+"));
        auto* zoomAll = page->own (new juce::TextButton ("All"));
        auto* zoomSlice = page->own (new juce::TextButton ("Slice"));
        zoomOut->setTooltip ("Zoom out (or the mouse wheel / pinch on the wave)");
        zoomIn->setTooltip ("Zoom in (or the mouse wheel / pinch on the wave; Shift+wheel scrolls)");
        zoomAll->setTooltip ("Show the whole break");
        zoomSlice->setTooltip ("Zoom to the selected slice");
        zoomOut->onClick = [wave] { wave->zoomAround (wave->viewStart + wave->viewLen * 0.5, 2.0); };
        zoomIn->onClick = [wave] { wave->zoomAround (wave->viewStart + wave->viewLen * 0.5, 0.5); };
        zoomAll->onClick = [wave] { wave->setView (0.0, 1.0); };
        zoomSlice->onClick = [wave] { if (wave->shade) wave->zoomTo (wave->shade()); };
        auto* scroll = page->own (new WaveScrollBar());
        scroll->onMove = [wave] (double st) { wave->setView (st, wave->viewLen); };
        wave->onViewChanged = [wave, scroll] { scroll->show (wave->viewStart, wave->viewLen); };
        auto* markerSnap = page->own (new juce::ComboBox());
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.param (P_bcMarkerSnap))) markerSnap->addItemList (c->choices, 1);
        markerSnapAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, kParamIds[P_bcMarkerSnap], tagParam (*markerSnap, P_bcMarkerSnap));
        markerSnap->setTooltip ("Marker Snap: where a dragged marker lands (hold Alt for fine moves with no snap; the arrow keys nudge the selected marker 1 ms, Shift 10 ms)");
        snapTr->setTooltip ("Taps and new markers move to the nearest transient within 20 ms");
        snapZero->setTooltip ("... and then to the nearest zero crossing within 2 ms");

        // ---- each break's own slicing and sound (one set shows: the Active Break's)
        juce::Array<Section*> slicing, sound;
        for (int k = 0; k < 3; ++k)
        {
            const auto bp = breakParams (k);
            auto* sl = sec (page, "Break " + juce::String (k + 1) + " Slicing & Tempo", breakColour (k));
            sl->choice (bp.sliceMode, "Slicing", 132);
            sl->knob (bp.sens, "Sensitivity", 66);
            sl->choice (bp.bars, "Bars", 90);
            sl->knob (bp.tempo, "Tempo (0 = auto)", 92);
            slicing.add (sl);
            auto* sn = sec (page, "Break " + juce::String (k + 1) + " Sound", breakColour (k));
            sn->choice (bp.stretch, "Stretch", 140);
            sn->choice (bp.colour, "Colour", 130);
            sn->knob (bp.level, "Level", 62);
            sn->knob (bp.pan, "Pan", 62);
            sound.add (sn);
        }
        auto* all = sec (page, "All Breaks  (Repitch: faster = higher, like a sampler. Akai Cyclic: S1000-style cyclic time-stretch; Cycle "
                               "Length and Crossfade set its grain and flutter. Clean: WSOLA, which finds the best place to splice; Quality sets how "
                               "hard it searches. Tempo Sync stretches every break to the song's tempo. Through Synth Filter sends the breaks through "
                               "Filter 1's settings. Select Keys: the three keys below the zone choose the Active Break (On the Beat: it changes on "
                               "the next beat). Level, Pan, Mute and Solo are the Mixer's Break strip.)", col::complex);
        all->add (new ParamToggle (proc, P_bcSync, "Tempo Sync", col::complex), 110, 40);
        all->knob (P_bcCycle, "Cycle", 62);
        all->knob (P_bcXfade, "Crossfade", 62);
        all->knob (P_bcPitch, "Pitch", 62);
        all->knob (P_bcVelSens, "Velocity", 62);
        all->add (new ParamToggle (proc, P_bcThruFilter, "Through Synth Filter", col::filter), 170, 40);
        all->newRow();
        all->knob (P_bcKeyBase, "First Key", 66);
        all->add (new ParamToggle (proc, P_bcSelKeys, "Select Keys", col::complex), 124, 40);
        all->add (new ParamToggle (proc, P_bcSelQuant, "On the Beat", col::complex), 116, 40);
        all->knob (P_bcLevel, "Level", 62);
        all->knob (P_mxBreakPan, "Pan", 62);
        all->add (new ParamToggle (proc, P_mxBreakMute, "Mute", col::mixer), 70, 40);
        all->add (new ParamToggle (proc, P_mxBreakSolo, "Solo", col::mixer), 70, 40);

        // ---- the selected slice of the Active Break
        auto* ed = sec (page, "Slice", col::complex);
        auto& store = proc.breakStore;
        auto sliceGet = [&store, selected, activeOf] (auto fn) { return [&store, selected, activeOf, fn] { const auto d = store.get(); return fn (d.brk[activeOf()].slice[juce::jlimit (0, 31, *selected)]); }; };
        auto sliceSet = [&store, selected, activeOf] (auto fn) { return [&store, selected, activeOf, fn] (auto v) { const int a = activeOf(); store.edit ([&] (tg::BreakData& d) { fn (d.brk[a].slice[juce::jlimit (0, 31, *selected)], v); }); }; };
        auto num = [] (int dp, const char* unit) { return [dp, unit] (double v) { return juce::String (v, dp) + unit; }; };
        juce::Array<StoreKnob*> knobs; juce::Array<StoreToggle*> toggles;
        knobs.add (new StoreKnob ("Pitch", -24, 24, 0.1, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.pitch; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.pitch = v; }), num (1, " st")));
        knobs.add (new StoreKnob ("Gain", 0, 2, 0.01, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.gain; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.gain = v; }), num (2, "")));
        knobs.add (new StoreKnob ("Pan", -1, 1, 0.01, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.pan; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.pan = v; }), num (2, "")));
        knobs.add (new StoreKnob ("Trim Start", 0, 0.95, 0.01, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.trimStart; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.trimStart = std::min (v, x.trimEnd - 0.05f); }), [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        knobs.add (new StoreKnob ("Trim End", 0.05, 1, 0.01, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.trimEnd; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.trimEnd = std::max (v, x.trimStart + 0.05f); }), [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        knobs.add (new StoreKnob ("Attack", 0, 0.5, 0.001, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.attack; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.attack = v; }), [] (double v) { return juce::String (juce::roundToInt (v * 1000)) + " ms"; }));
        knobs.add (new StoreKnob ("Decay", 0, 4, 0.01, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.decay; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.decay = v; }), [] (double v) { return v < 0.005 ? juce::String ("off") : juce::String (v, 2) + " s"; }));
        knobs.add (new StoreKnob ("Choke", 0, 8, 1, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return (float) x.choke; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.choke = juce::roundToInt (v); }), [] (double v) { return v < 0.5 ? juce::String ("none") : "group " + juce::String (juce::roundToInt (v)); }));
        knobs.add (new StoreKnob ("Stretch", 0, 3, 1, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return (float) x.stretch; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.stretch = juce::roundToInt (v); }),
                                  [] (double v) { static const char* n[] = { "Break's", "Repitch", "Akai", "Clean" }; return juce::String (n[juce::jlimit (0, 3, juce::roundToInt (v))]); }));
        knobs.add (new StoreKnob ("Cycle", 0, 200, 0.5, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.cycleMs; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.cycleMs = v < 10.0f ? 0.0f : v; }),
                                  [] (double v) { return v < 10.0 ? juce::String ("Break's") : juce::String (v, 1) + " ms"; }));
        knobs.getLast()->setTooltip ("This slice's Akai Cyclic cycle length (Break's: the Cycle Length knob). Used when a step doesn't set its own.");
        knobs[knobs.size() - 2]->setTooltip ("This slice's stretch (Break's: the break's Stretch). A step's own Stretch wins over this.");
        toggles.add (new StoreToggle ("Reverse", col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.reverse; }), sliceSet ([] (tg::BreakSlice& x, bool v) { x.reverse = v; })));
        toggles.add (new StoreToggle ("Loop", col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.loop; }), sliceSet ([] (tg::BreakSlice& x, bool v) { x.loop = v; })));
        for (auto* k : knobs) ed->add (k, 70, 84);
        for (auto* t : toggles) ed->add (t, 80, 40);
        auto* sliceName = page->own (new juce::Label());
        sliceName->setColour (juce::Label::textColourId, col::text);
        sliceName->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        auto lastActive = std::make_shared<int> (-1);
        ticker->tick = [this, knobs, toggles, info, sliceName, selected, activeOf, lastActive, selector, wave, slicing, sound, ed, smp, play]
        {
            play->setButtonText (proc.previewIsPlaying() ? "Stop" : "Play");
            const int a = activeOf();
            if (a != *lastActive)
            {
                // a new Active Break: its wave, its controls, its colour
                *lastActive = a; *selected = 0;
                selector->setLocal (a); selector->setColour (breakColour (a));
                wave->setSlot (MegaSynthProcessor::breakSlot (a)); wave->setColour (breakColour (a));
                for (int k = 0; k < 3; ++k) { slicing[k]->setVisible (k == a); sound[k]->setVisible (k == a); }
                ed->colour = breakColour (a); smp->colour = breakColour (a);
                ed->repaint(); smp->repaint();
            }
            for (auto* k : knobs) k->refresh();
            for (auto* t : toggles) t->refresh();
            info->setText (breakInfoText (proc, a), juce::dontSendNotification);
            const auto d = proc.breakStore.get();
            if (*selected >= std::max (1, d.brk[a].nSlices)) *selected = 0;
            const int kb = juce::roundToInt (proc.param (P_bcKeyBase)->convertFrom0to1 (proc.param (P_bcKeyBase)->getValue()));
            sliceName->setText (d.brk[a].nSlices > 0 ? "Break " + juce::String (a + 1) + ", slice " + juce::String (*selected + 1) + "  (" + juce::MidiMessage::getMidiNoteName (kb + *selected, true, true, 3) + ")"
                                                      : juce::String ("No slices"), juce::dontSendNotification);
        };
        ticker->tick();
        breakWaveForTests = wave;
        page->onResize = [page, smp, selector, minis, wave, load, clear, toMarkers, matchTempo, matchLevels, info, on, slicing, sound, all, ed, sliceName,
                          chop, play, tap, undoChop, clearMk, snapAll, snapTr, snapZero, latency, zoomOut, zoomIn, zoomAll, zoomSlice, scroll, markerSnap]
        {
            const int g = 10, W = page->getWidth();
            smp->setBounds (g, g, W - 2 * g, 238);
            selector->setBounds (g + 10, g + 34, 270, 26);
            const int mx = g + 290, mw = (W - 2 * g - 300) / 3;
            for (int k = 0; k < 3; ++k) minis[k]->setBounds (mx + k * mw, g + 34, mw - 6, 26);
            wave->setBounds (g + 10, g + 66, W - 2 * g - 20, 84);
            scroll->setBounds (g + 10, g + 151, W - 2 * g - 20, 11);
            on->setBounds (g + 10, g + 168, 56, 28);
            load->setBounds (g + 68, g + 170, 62, 24);
            clear->setBounds (g + 134, g + 170, 56, 24);
            toMarkers->setBounds (g + 194, g + 170, 124, 24);
            matchTempo->setBounds (g + 322, g + 170, 100, 24);
            matchLevels->setBounds (g + 426, g + 170, 100, 24);
            info->setBounds (g + 536, g + 166, W - 2 * g - 546, 36);
            int x = g + 10; const int y = g + 204;
            auto put = [&x, y] (juce::Component* c, int w) { c->setBounds (x, y, w, 26); x += w + 6; };
            put (chop, 66); put (play, 52); put (tap, 48); put (undoChop, 84); put (clearMk, 90); put (snapAll, 72);
            put (snapTr, 94); put (snapZero, 62); put (latency, 150);
            x += 6; put (markerSnap, 120);
            x += 6; put (zoomOut, 28); put (zoomIn, 28); put (zoomAll, 40); put (zoomSlice, 50);
            for (auto* s : slicing) s->setBounds (g, g + 246, 510, 118);
            for (auto* s : sound) s->setBounds (g + 520, g + 246, W - 2 * g - 520, 118);
            ed->setBounds (g, g + 372, 510, 216);
            sliceName->setBounds (g + 70, g + 378, 330, 20);
            all->setBounds (g + 520, g + 372, W - 2 * g - 520, 216);
        };
    }

    // ---------------------------------------------------------------- Sequencers > Break Sequencer
    {
        auto* page = addPage ("Break Sequencer");
        auto* ticker = page->own (new StoreTicker());
        auto* top = sec (page, "Patterns  (Eight patterns, A-H, of 16 or 32 sixteenth steps, locked to the song while it plays (and to the synth's "
                               "tempo when it doesn't). Pick the pattern to edit and play here, or from 8 keys (Pattern Keys); a new pattern starts "
                               "on the next step, beat or bar (Switch). Chain plays patterns in order, e.g. AABA. Each step plays a slice of the "
                               "Break Chopper's break, with its own velocity, accent, pitch, reverse, roll (2, 3 or 4 hits in the step), probability "
                               "or tie (let the last slice ring on). Step, pattern and slice are Mod Matrix sources.)", col::seq);
        auto* pat = new Segmented (proc, P_bsPattern, { "A", "B", "C", "D", "E", "F", "G", "H" }, col::seq);
        top->add (new ParamToggle (proc, P_bsOn, "Run", col::seq), 70, 40);
        top->add (pat, 240, 30);
        top->choice (P_bsSteps, "Length", 90);
        top->choice (P_bsSwitch, "Switch", 100);
        top->choice (P_bsKeys, "Pattern Keys", 150);
        top->knob (P_bsSwing, "Swing", 62);
        top->knob (P_bsAmount, "Amount", 62);
        auto* patBreak = new StoreKnob ("Pattern Break", 0, 3, 1, col::seq,
                                        [this] { const auto d = proc.breakStore.get(); return (float) d.patBreak[juce::jlimit (0, 7, juce::roundToInt (proc.param (P_bsPattern)->convertFrom0to1 (proc.param (P_bsPattern)->getValue())))]; },
                                        [this] (float v) { const int p = juce::jlimit (0, 7, juce::roundToInt (proc.param (P_bsPattern)->convertFrom0to1 (proc.param (P_bsPattern)->getValue())));
                                                           proc.breakStore.edit ([p, v] (tg::BreakData& d) { d.patBreak[p] = juce::roundToInt (v); }); },
                                        [] (double v) { return v < 0.5 ? juce::String ("keep") : "Break " + juce::String (juce::roundToInt (v)); });
        patBreak->setTooltip ("When this pattern starts, it makes this break the Active Break (keep: no change)");
        top->add (patBreak, 84, 84);
        top->knob (P_bsVarAmount, "Variation", 66);
        top->choice (P_bsToolSource, "Tools draw from", 168);
        top->newRow();
        top->add (new ParamToggle (proc, P_bsChainOn, "Chain", col::seq), 76, 40);
        auto* chainEd = new juce::TextEditor();
        chainEd->setTooltip ("Patterns to play in order, e.g. AABA (up to 16)");
        chainEd->setInputRestrictions (16, "ABCDEFGHabcdefgh");
        chainEd->onTextChange = [this, chainEd] { const auto t = chainEd->getText(); proc.breakStore.edit ([t] (tg::BreakData& d) { tg::BreakStore::setChain (d, t); }); };
        top->add (chainEd, 130, 26);
        juce::Array<juce::TextButton*> tools;
        for (auto* n : { "Original", "Reverse", "Shuffle", "Jungle", "Franken", "Swap", "Copy", "Paste", "Clear" }) { auto* b = new juce::TextButton (n); tools.add (b); top->add (b, 64, 26); }
        top->choice (P_bsAlternate, "Alternate", 150);
        top->choice (P_bsAltBreak, "With", 96);
        top->newRow();
        // the Stage 4 tools: each press applies straight away (a new seed each time) and Undo takes it back
        juce::Array<juce::TextButton*> newTools;
        for (int t = 0; t < tg::breaktools::kNumTools; ++t) { auto* b = new juce::TextButton (tg::breaktools::toolName (t)); newTools.add (b); top->add (b, 76, 26); }
        auto* grid = page->own (new BreakStepGrid (proc));
        auto clip = std::make_shared<std::vector<tg::BreakStep>>();
        auto edited = [grid] { return grid->editedPattern(); };
        auto activeBreak = [this] { return juce::jlimit (0, 2, juce::roundToInt (proc.param (P_bcActive)->convertFrom0to1 (proc.param (P_bcActive)->getValue())) - 1); };
        auto amount = [this] { return proc.param (P_bsAmount)->convertFrom0to1 (proc.param (P_bsAmount)->getValue()); };
        tools[0]->onClick = [this, grid, edited, activeBreak] { const int a = activeBreak(); proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::original (d, edited(), grid->numSteps(), a); }); };
        tools[1]->onClick = [this, grid, edited] { proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::reverseOrder (d, edited(), grid->numSteps()); }); };
        tools[2]->onClick = [this, grid, edited] { proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::shuffle (d, edited(), grid->numSteps(), d.seed++); }); };
        tools[3]->onClick = [this, grid, edited, activeBreak] { const int a = activeBreak(); proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::jungle (d, edited(), grid->numSteps(), d.seed++, a); }); };
        tools[4]->onClick = [this, grid, edited, activeBreak, amount] { const int a = activeBreak(); const float am = amount(); proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::frankenbreak (d, edited(), grid->numSteps(), d.seed++, am, a); }); };
        tools[5]->onClick = [this, grid, edited, activeBreak] { const int a = activeBreak(); proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::swapBreak (d, edited(), grid->numSteps(), a); }); };
        tools[6]->onClick = [this, edited, clip] { const auto d = proc.breakStore.get(); clip->assign (d.step[edited()], d.step[edited()] + tg::BreakData::kSteps); setStatus ("Pattern copied"); };
        tools[7]->onClick = [this, edited, clip] { if (clip->empty()) return; proc.breakStore.edit ([&] (tg::BreakData& d) { std::copy (clip->begin(), clip->end(), d.step[edited()]); }); };
        tools[8]->onClick = [this, edited] { proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::clear (d, edited()); }); };
        tools[4]->setTooltip ("Frankenbreak: the Active Break's groove, with its kicks, snares and hats each taken from a different loaded break (Amount: how many hits are swapped; each press a new mix)");
        tools[5]->setTooltip ("Swap Break: the same pattern, played with the Active Break's slices at the same places");
        {
            static const char* tips[] = {
                "Rollers: quiet 16th ghost snares around the backbeat (Amount: how many)",
                "Two-Step: kick on 1 and the 'and' of 2, snares on 2 and 4, the rest from the pattern (Amount: how many of its other kicks go)",
                "Halftime: the snare on 3 only, kicks on 1 and the 'and' of 3, hats from the break (Amount: how many hats)",
                "Double-time: each half bar squeezed into a quarter and played twice (Amount: how many half bars)",
                "Snare Build: a snare roll into the end of the pattern, x2 then x3 then x4, rising in pitch (Amount: how long and how high)",
                "Stutter: a hit repeated 2-4 times as 1/16s or 1/32s, at random places (Amount: how many)",
                "Reverse Swell: a reversed snare leading into the backbeats (Amount: how many)",
                "Cut-up: 1/8s swapped at random, keeping the kick on 1 and the snares on 2 and 4 (Amount: how many swaps)",
                "Ghosts: quiet hits on empty steps (Amount: how many)",
                "Hat Shuffle: only the hat-like hits change places (Amount: how many of them)",
                "Thin Out: hits removed or made less likely, for breakdowns; the kick on 1 and the backbeats stay (Amount: how many)",
                "Humanise: small changes to velocity, and hits played a little late within the swing (Amount: how much)",
                "Pitch Dive: the pitch falls over the last beat, down to an octave (Amount: how far)",
                "Variation: small random changes to the pattern as it is (the Variation knob: how many)" };
            for (int t = 0; t < newTools.size(); ++t)
            {
                newTools[t]->setTooltip (juce::String (tips[t]) + ". Applied straight away, a new result each press; Undo takes it back.");
                newTools[t]->onClick = [this, t, grid, edited, activeBreak, amount]
                {
                    const int a = activeBreak();
                    const float am = t == tg::breaktools::ToolVariation ? proc.param (P_bsVarAmount)->convertFrom0to1 (proc.param (P_bsVarAmount)->getValue()) : amount();
                    const bool all = proc.param (P_bsToolSource)->getValue() > 0.5f;
                    const int steps = grid->numSteps(), pat = edited();
                    proc.breakStore.edit ([&] (tg::BreakData& d) { tg::breaktools::applyTool (t, d, pat, steps, d.seed++, am, a, all); });
                    setStatus (juce::String (tg::breaktools::toolName (t)) + " applied (Undo takes it back)");
                };
            }
        }
        tools[0]->setTooltip ("The break as it was: its slices in order, each on the step where it falls");
        tools[1]->setTooltip ("The pattern back to front");
        tools[2]->setTooltip ("The same slices in a new order (each press a new shuffle)");
        tools[3]->setTooltip ("A jungle edit of the original: snare ghosts and swaps, a kick double, a reversed hit, a cut-up and a snare roll (each press a new one)");

        auto* stepSec = sec (page, "Step", col::seq);
        auto& store = proc.breakStore;
        auto stepGet = [&store, grid] (auto fn) { return [&store, grid, fn] { const auto d = store.get(); return fn (d.step[grid->editedPattern()][juce::jlimit (0, 31, grid->selected)]); }; };
        auto stepSet = [&store, grid] (auto fn) { return [&store, grid, fn] (auto v) { store.edit ([&] (tg::BreakData& d) { fn (d.step[grid->editedPattern()][juce::jlimit (0, 31, grid->selected)], v); }); }; };
        juce::Array<StoreKnob*> knobs; juce::Array<StoreToggle*> toggles;
        toggles.add (new StoreToggle ("On", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.on; }), stepSet ([] (tg::BreakStep& x, bool v) { x.on = v; })));
        knobs.add (new StoreKnob ("Slice", 1, 32, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) (x.slice + 1); }), stepSet ([] (tg::BreakStep& x, float v) { x.slice = juce::roundToInt (v) - 1; x.on = true; }), [] (double v) { return juce::String (juce::roundToInt (v)); }));
        knobs.add (new StoreKnob ("Velocity", 0, 1, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.vel; }), stepSet ([] (tg::BreakStep& x, float v) { x.vel = v; }), [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        toggles.add (new StoreToggle ("Accent", col::supersaw, stepGet ([] (const tg::BreakStep& x) { return x.accent; }), stepSet ([] (tg::BreakStep& x, bool v) { x.accent = v; })));
        knobs.add (new StoreKnob ("Pitch", -24, 24, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.pitch; }), stepSet ([] (tg::BreakStep& x, float v) { x.pitch = v; }), [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }));
        toggles.add (new StoreToggle ("Reverse", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.reverse; }), stepSet ([] (tg::BreakStep& x, bool v) { x.reverse = v; })));
        knobs.add (new StoreKnob ("Roll", 1, 4, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.roll; }), stepSet ([] (tg::BreakStep& x, float v) { x.roll = juce::roundToInt (v); }), [] (double v) { return v < 1.5 ? juce::String ("off") : "x" + juce::String (juce::roundToInt (v)); }));
        knobs.add (new StoreKnob ("Probability", 0, 1, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.prob; }), stepSet ([] (tg::BreakStep& x, float v) { x.prob = v; }), [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        toggles.add (new StoreToggle ("Tie", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.tie; }), stepSet ([] (tg::BreakStep& x, bool v) { x.tie = v; })));
        auto* brkKnob = new StoreKnob ("Break", 0, 3, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.brk; }), stepSet ([] (tg::BreakStep& x, float v) { x.brk = juce::roundToInt (v); }),
                                       [] (double v) { return v < 0.5 ? juce::String ("Active") : "Break " + juce::String (juce::roundToInt (v)); });
        auto* layerT = new StoreToggle ("Layer", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.layer; }), stepSet ([] (tg::BreakStep& x, bool v) { x.layer = v; }));
        auto* lb = new StoreKnob ("Layer Break", 1, 3, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.layerBrk; }), stepSet ([] (tg::BreakStep& x, float v) { x.layerBrk = juce::roundToInt (v); x.layer = true; }),
                                  [] (double v) { return "Break " + juce::String (juce::roundToInt (v)); });
        auto* ls = new StoreKnob ("Layer Slice", 1, 32, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) (x.layerSlice + 1); }), stepSet ([] (tg::BreakStep& x, float v) { x.layerSlice = juce::roundToInt (v) - 1; x.layer = true; }),
                                  [] (double v) { return juce::String (juce::roundToInt (v)); });
        auto* ll = new StoreKnob ("Layer Level", 0, 1.5, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.layerLevel; }), stepSet ([] (tg::BreakStep& x, float v) { x.layerLevel = v; }),
                                  [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        brkKnob->setTooltip ("Which break this step plays: Active (whichever is active) or always Break 1, 2 or 3");
        auto* stStretch = new StoreKnob ("Stretch", 0, 3, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.stretch; }), stepSet ([] (tg::BreakStep& x, float v) { x.stretch = juce::roundToInt (v); }),
                                         [] (double v) { static const char* n[] = { "Default", "Repitch", "Akai", "Clean" }; return juce::String (n[juce::jlimit (0, 3, juce::roundToInt (v))]); });
        stStretch->setTooltip ("This hit's stretch: Default (the slice's, else the break's), Repitch, Akai Cyclic or Clean");
        auto* stCycle = new StoreKnob ("Cycle", 0, 200, 0.5, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.cycleMs; }), stepSet ([] (tg::BreakStep& x, float v) { x.cycleMs = v < 10.0f ? 0.0f : v; }),
                                       [] (double v) { return v < 10.0 ? juce::String ("Default") : juce::String (v, 1) + " ms"; });
        stCycle->setTooltip ("This hit's Akai Cyclic cycle length (Default: the slice's, else Cycle Length). Long cycles crunch a single hit.");
        auto* stLate = new StoreKnob ("Late", 0, 0.5, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.nudge; }), stepSet ([] (tg::BreakStep& x, float v) { x.nudge = v; }),
                                      [] (double v) { return v < 0.005 ? juce::String ("on time") : juce::String (juce::roundToInt (v * 100)) + "% step"; });
        stLate->setTooltip ("How late this hit plays, as a share of a step (Humanise sets it)");
        stepSec->add (toggles[0], 56, 40);
        stepSec->add (brkKnob, 70, 84);
        for (auto* k : knobs) stepSec->add (k, 70, 84);
        for (auto* k : { stStretch, stCycle, stLate }) stepSec->add (k, 70, 84);
        stepSec->newRow();
        for (int i = 1; i < toggles.size(); ++i) stepSec->add (toggles[i], 80, 40);
        stepSec->add (layerT, 72, 40);
        for (auto* k : { lb, ls, ll }) stepSec->add (k, 70, 84);
        knobs.add (brkKnob); knobs.add (lb); knobs.add (ls); knobs.add (ll); knobs.add (patBreak); toggles.add (layerT);
        knobs.add (stStretch); knobs.add (stCycle); knobs.add (stLate);
        auto* stepName = page->own (new juce::Label());
        stepName->setColour (juce::Label::textColourId, col::text);
        stepName->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        grid->onSelect = [] (int) {};
        // ---- fills
        auto* fillSec = sec (page, "Fill", juce::Colour (0xffff9f43));
        fillSec->addInfo ("Fill plays a fill, then the pattern comes back exactly where it would have been, as if it had kept running "
                          "underneath (or, with Restart, from its first step). Chains carry on underneath too. Start: on the next beat, the next bar, "
                          "or timed to end with the pattern. One-shot plays the set Length; Held plays while you hold it. Each pattern has its own "
                          "Fill Source: Auto (one of Snare Build, Stutter, Rollers, Pitch Dive or Frankenbreak, chosen per pattern and made from it; "
                          "New Fills picks again) or the last steps of a pattern A-H. Fill Break plays the fill from another break. Auto Fill plays one "
                          "every 4, 8 or 16 bars, by Chance, ending with the phrase. Fill Active is a Mod Matrix source.");
        fillSec->add (new FillButton (proc), 64, 40);
        fillSec->choice (P_bsFillMode, "Mode", 92);
        fillSec->choice (P_bsFillLen, "Length", 86);
        fillSec->choice (P_bsFillStart, "Start", 112);
        fillSec->newRow();
        auto patOf = [this] { return juce::jlimit (0, 7, juce::roundToInt (proc.param (P_bsPattern)->convertFrom0to1 (proc.param (P_bsPattern)->getValue()))); };
        auto* fSrc = new StoreKnob ("Fill Source", 0, 8, 1, juce::Colour (0xffff9f43),
                                    [this, patOf] { return (float) proc.breakStore.get().fillSrc[patOf()]; },
                                    [this, patOf] (float v) { const int p = patOf(); proc.breakStore.edit ([p, v] (tg::BreakData& d) { d.fillSrc[p] = juce::roundToInt (v); }); },
                                    [this, patOf] (double v)
                                    {
                                        const int n = juce::roundToInt (v);
                                        if (n > 0) return "Pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + n - 1));
                                        static const char* tn[] = { "Build", "Stutter", "Rollers", "Dive", "Franken" };
                                        return "Auto " + juce::String (tn[juce::jlimit (0, 4, proc.breakStore.get().autoFillTool[patOf()])]);
                                    });
        fSrc->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 84, 18);
        fSrc->setTooltip ("Where this pattern's fill comes from: Auto (made from the pattern by a tool) or the last steps of pattern A-H");
        auto* fBrk = new StoreKnob ("Fill Break", 0, 3, 1, juce::Colour (0xffff9f43),
                                    [this, patOf] { return (float) proc.breakStore.get().fillBrk[patOf()]; },
                                    [this, patOf] (float v) { const int p = patOf(); proc.breakStore.edit ([p, v] (tg::BreakData& d) { d.fillBrk[p] = juce::roundToInt (v); }); },
                                    [] (double v) { return v < 0.5 ? juce::String ("as steps") : "Break " + juce::String (juce::roundToInt (v)); });
        fBrk->setTooltip ("The break the fill plays (for its Active steps): e.g. a fill from Break 2 over a groove from Break 1");
        fillSec->add (fSrc, 84, 84);
        fillSec->add (fBrk, 70, 84);
        fillSec->knob (P_bsAutoFillProb, "Chance", 60);
        fillSec->knob (P_bsFillKey, "Fill Key", 64);
        auto* newFills = new juce::TextButton ("New Fills");
        newFills->setTooltip ("New Auto fills for every pattern (a new fill seed; Undo goes back)");
        newFills->onClick = [this] { proc.breakStore.edit ([] (tg::BreakData& d) { ++d.fillSeed; }); };
        fillSec->add (newFills, 72, 26);
        fillSec->newRow();
        fillSec->choice (P_bsFillReturn, "When it ends", 164);
        fillSec->choice (P_bsAutoFill, "Auto Fill", 128);
        knobs.add (fSrc); knobs.add (fBrk);
        ticker->tick = [this, grid, knobs, toggles, chainEd, stepName]
        {
            grid->refresh();
            for (auto* k : knobs) k->refresh();
            for (auto* t : toggles) t->refresh();
            const auto d = proc.breakStore.get();
            if (! chainEd->hasKeyboardFocus (true) && chainEd->getText() != juce::String (d.chain)) chainEd->setText (juce::String (d.chain), false);
            stepName->setText ("Step " + juce::String (grid->selected + 1) + " of pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + grid->editedPattern())),
                               juce::dontSendNotification);
        };
        ticker->tick();
        breakGridForTests = grid;
        page->onResize = [page, top, grid, stepSec, stepName, fillSec]
        {
            const int g = 10, W = page->getWidth();
            top->setBounds (g, g, W - 2 * g, 210);
            grid->setBounds (g + 4, g + 216, W - 2 * g - 8, 140);
            stepSec->setBounds (g, g + 362, 772, 226);
            stepName->setBounds (g + 70, g + 368, 300, 20);
            fillSec->setBounds (g + 780, g + 362, W - 2 * g - 780, 226);
        };
    }

    // ---------------------------------------------------------------- Mixer & FM
    {
        auto* page = addPage ("Mixer");
        // ---- channel strips, in the order the sources are listed everywhere else
        struct StripDef { const char* name; int level, extra; const char* extraCap; int pEnv; juce::Colour c; };
        const StripDef strips[kMxStrips] = {
            { "Osc 1", P_osc1Gain, P_uni1Voices, "Voices", 1, col::osc },
            { "Osc 2", P_osc2Gain, P_uni1Voices + kUniStride, "Voices", 2, col::osc },
            { "Osc 3", P_osc3Gain, P_uni1Voices + 2 * kUniStride, "Voices", 3, col::osc },
            { "Sub", P_subGain, -1, "", 4, col::osc },
            { "WT 1", P_osc4Gain, P_uni1Voices + 3 * kUniStride, "Voices", 5, col::wavetable },
            { "WT 2", P_wt2Gain, P_uni1Voices + 4 * kUniStride, "Voices", 6, col::wavetable },
            { "Complex", P_complexGain, P_uni1Voices + 5 * kUniStride, "Voices", 7, col::complex },
            { "SuperSaw", P_supersawGain, P_supersawVoices, "Voices", 8, col::supersaw },
            { "Ring", P_ringGain, P_ringMix, "Ring Mix", -1, col::fm },
            { "Granular", P_gsLevel, P_gsDensity, "Density", 99, col::supersaw },
            { "Break", P_bcLevel, P_bcPitch, "Pitch", -1, col::complex } };   // (99: lit only when the pitch envelope moves all)
        juce::Array<ChannelStrip*> stripList;
        for (int k = 0; k < kMxStrips; ++k)
            stripList.add (page->own (new ChannelStrip (proc, k, strips[k].name, strips[k].level, strips[k].extra, strips[k].extraCap, strips[k].pEnv, strips[k].c)));
        mixerStrips = stripList;
        auto* srcHelp = sec (page, "Sources", col::mixer);
        srcHelp->addInfo ("Each source's level, pan, unison voices (or the SuperSaw's voices, or the ring modulator's wet / dry) and Mute / Solo. "
                          "Pan is a balance: the centre leaves both sides at full level, so a centred mix sounds exactly as before. "
                          "A muted source still works as an FM or ring modulator. PITCH ENV lights up when the pitch envelope moves that source. "
                          "The meters show each source before the filter (only measured while this page is open).");

        // ---- the engines (mutation stages), lit when they're doing something
        auto plainOf = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
        auto* engS = sec (page, "Engines", col::fx);
        engS->addInfo ("The mutation stages, in the order the sound goes through them. Lit when they're doing something; click one to open its page.");
        struct EngDef { const char* name; int mix; std::function<bool()> on; const char* page; juce::Colour c; };
        const EngDef engs[] = {
            { "DNA Splice", P_dnaMix, [=] { return plainOf (P_dnaMix) > 1.0e-4f; }, "Note Mutation", col::wavetable },
            { "Wave Mutation", P_wmMix, [=] { return plainOf (P_wmMix) > 1.0e-4f; }, "Note Mutation", col::fx },
            { "Audio-Rate", -1, [=] { return plainOf (P_arFm) + plainOf (P_arAm) + plainOf (P_arRing) + plainOf (P_arShiftMix) > 1.0e-4f; }, "Note Mutation", col::mod },
            { "Resonator", P_resMix, [=] { return plainOf (P_resMix) > 1.0e-4f; }, "Note Mutation", col::filter },
            { "Granular", P_grMix, [=] { return plainOf (P_grMix) > 1.0e-4f; }, "Bus Mutation", col::supersaw },
            { "Spectral", P_spMix, [=] { return plainOf (P_spOn) > 0.5f; }, "Bus Mutation", col::fx },
            { "Feedback", -1, [=] { float a = 0; for (int i = P_fbGrGr; i <= P_fbOutDl; ++i) a += std::abs (plainOf (i)); return a > 1.0e-4f; }, "Bus Mutation", col::complex } };
        juce::Array<EngineChip*> chipList;
        for (auto& e : engs)
        {
            auto* chip = page->own (new EngineChip (proc, e.name, e.mix, e.on, e.c));
            const juce::String target (e.page);
            chip->onOpen = [this, target] { showPage (target); };
            chipList.add (chip);
        }
        engineChips = chipList;

        // ---- effect returns
        auto* rt = sec (page, "Effect Returns", col::mixer);
        for (auto [idx, n] : { std::pair<int, const char*> { P_delayMix, "Delay" }, { P_reverbMix, "Reverb" }, { P_shimmerMix, "Shimmer" },
                               { P_chorusMix, "Chorus" }, { P_reverseMix, "Reverse" } })
            rt->knob (idx, n, 66);
        auto* pageFm = addPage ("FM & Ring");
        auto* fmS = sec (pageFm, "FM Matrix  (source -> destination, Hz of deviation)", col::fm);
        for (int i = 0; i < 4; ++i)
        {
            fmS->choice (P_fmSlot1Source + 3 * i, "Slot " + juce::String (i + 1) + " Source", 120);
            fmS->choice (P_fmSlot1Dest + 3 * i, "Destination", 120);
            fmS->knob (P_fmSlot1Amt + 3 * i, "Amount");
            if (i % 2 == 1) fmS->newRow();
        }
        fmS->knob (P_fmAmount, "FM 2>1 (legacy)", 96);
        auto* rg = sec (pageFm, "Ring Mod Matrix", col::fm);
        for (int i = 0; i < 2; ++i)
        {
            rg->choice (P_ringSlot1Source + 3 * i, "Slot " + juce::String (i + 1) + " Source", 120);
            rg->choice (P_ringSlot1Dest + 3 * i, "Carrier", 140);
            rg->knob (P_ringSlot1Amt + 3 * i, "Amount");
            rg->newRow();
        }
        // Analog Warmth: the colour of the whole output
        auto* w = sec (page, "Analog", col::supersaw);
        w->knob (P_warmth, "Warmth", 64);
        w->knob (P_bassKeep, "Bass Keep", 64);
        w->knob (P_analogDrift, "Drift", 64);
        w->addInfo ("Warmth: softer, rounder filter Drive saturation, plus a gentle low lift and smoother top.  "
                    "Bass Keep: holds onto low end in the classic models with high-pass or band-pass stages.  "
                    "Drift: each oscillator wanders slightly and starts free-running.  All at zero = the browser's exact sound.");
        // the output: Master Volume (before the effects, as always), the Master Filter and the Output trim (the very end)
        auto* outS = sec (page, "Output", col::master);
        outS->addInfo ("Master Volume comes before the effects, as it always has. At the very end: the Master Filter (Filters tab), "
                       "then Output. The meter shows what leaves the plug-in, with the peak held.");
        outS->knob (P_masterVolume, "Master", 58);
        outS->add (new ParamToggle (proc, P_mfOn, "Master Filter", col::filter), 112, 40);
        outS->knob (P_mfCutoff, "MF Cutoff", 66);
        outS->knob (P_outGain, "Output", 58);
        auto* outL = page->own (new LevelMeter()); auto* outR = page->own (new LevelMeter());
        outMeters[0] = outL; outMeters[1] = outR;
        page->onResize = [page, stripList, srcHelp, engS, chipList, rt, w, outS, outL, outR]
        {
            const int g = 10, W = page->getWidth();
            const int stripW = 70, stripsW = stripW * kMxStrips + 2 * 4;
            srcHelp->setBounds (g, g, stripsW + 8, 32 + 360 + 8);
            for (int k = 0; k < stripList.size(); ++k) { stripList[k]->setBounds (g + 4 + k * stripW, g + 32, stripW - 4, 360); stripList[k]->toFront (false); }
            w->setBounds (g, g + 410, 260, 130);
            const int rx = g + stripsW + 8 + g, rw = W - rx - g;
            engS->setBounds (rx, g, rw, 270);
            const int cw = (rw - 20) / 4;
            for (int k = 0; k < chipList.size(); ++k)
            {
                const int row = k / 4, c = k % 4;
                chipList[k]->setBounds (rx + 10 + c * cw, g + 32 + row * 114, cw - 6, 110);
                chipList[k]->toFront (false);
            }
            rt->setBounds (rx, g + 280, rw, 130);
            outS->setBounds (rx, g + 420, rw - 46, 130);
            outL->setBounds (rx + rw - 40, g + 420, 14, 130);
            outR->setBounds (rx + rw - 22, g + 420, 14, 130);
        };
        pageFm->onResize = [pageFm, fmS, rg]
        {
            const int W = pageFm->getWidth(), g = 10;
            fmS->setBounds (g, g, 720, 220);
            rg->setBounds (g * 2 + 720, g, W - 3 * g - 720, 220);
        };
    }

    // ---------------------------------------------------------------- Mutation
    {
        auto* page = addPage ("Note Mutation");
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
        auto* rsS = sec (page, "Resonator  (16 tuned modes after the filter and amp; rings on after the note)", col::filter);
        rsS->choice (P_resTuning, "Tuning", 150);
        rsS->knob (P_resMix, "Mix");
        rsS->knob (P_resModes, "Modes");
        rsS->knob (P_resPitch, "Pitch");
        rsS->knob (P_resDecay, "Decay");
        rsS->knob (P_resDamping, "Damping");
        rsS->knob (P_resInharm, "Stiffness");
        rsS->knob (P_resSpread, "Spread");
        rsS->knob (P_resFeedback, "Feedback");
        page->onResize = [this, page, dnS, wmS, arS, rsS]
        {
            const int g = 10, W = page->getWidth();
            dnS->setBounds (g, g, W - 2 * g, 136);
            dnaInfo->setBounds (g + 700, g + 34, W - 2 * g - 710, 96);
            dnaInfo->toFront (false);
            wmS->setBounds (g, 156, W - 2 * g, 136);
            arS->setBounds (g, 302, W - 2 * g, 136);
            rsS->setBounds (g, 448, W - 2 * g, 136);
        };
    }

    // ---------------------------------------------------------------- Bus mutation (runs once on all notes, before the effects)
    {
        auto* page = addPage ("Bus Mutation");
        auto* grS = sec (page, "Granular  (all notes together, before the effects)", col::supersaw);
        grS->knob (P_grMix, "Mix");
        grS->knob (P_grSize, "Size");
        grS->knob (P_grDensity, "Density");
        grS->knob (P_grPosition, "Position");
        grS->knob (P_grJitter, "Jitter");
        grS->knob (P_grPitch, "Pitch");
        grS->knob (P_grPitchRand, "Pitch Jitter");
        grS->knob (P_grReverse, "Reverse");
        grS->knob (P_grSpread, "Spread");
        grS->knob (P_grFeedback, "Feedback");
        auto* frz = new juce::ToggleButton ("Freeze");
        frz->setColour (juce::ToggleButton::tickColourId, col::supersaw);
        frz->setTooltip ("Stop recording: the grains keep playing what's in the buffer, even after the notes end");
        grS->add (frz, 90, 30);
        freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_grFreeze], tagParam (*frz, P_grFreeze));
        auto* spS = sec (page, "Spectral  (FFT resynthesis; adds one frame of latency, reported to your DAW, while it's on)", col::fx);
        auto* spOn = new juce::ToggleButton ("On");
        spOn->setColour (juce::ToggleButton::tickColourId, col::fx);
        spOn->setTooltip ("Switch the spectral stage in. While it's on, the synth's output is delayed by one FFT frame; your DAW is told and compensates.");
        spS->add (spOn, 60, 30);
        spOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_spOn], tagParam (*spOn, P_spOn));
        spS->choice (P_spSize, "FFT Size (quality)", 150);
        spS->knob (P_spMix, "Mix");
        spS->knob (P_spBlur, "Blur");
        spS->knob (P_spShift, "Shift");
        spS->knob (P_spScramble, "Scramble");
        spS->knob (P_spTilt, "Tilt");
        spS->knob (P_spMorph, "Morph");
        spS->knob (P_spFormant, "Formant");
        spS->knob (P_spFeedback, "Feedback");
        auto* spFz = new juce::ToggleButton ("Freeze");
        spFz->setColour (juce::ToggleButton::tickColourId, col::fx);
        spFz->setTooltip ("Hold the current spectrum. Morph blends the live sound towards the last frozen spectrum.");
        spS->add (spFz, 90, 30);
        spFreezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_spFreeze], tagParam (*spFz, P_spFreeze));
        auto* ordS = sec (page, "Order", col::muted);
        ordS->choice (P_busOrder, "Bus order", 180);
        auto* fbS = sec (page, "Feedback Matrix  (from > to; every path is DC-blocked, filtered, soft-clipped and limited)", col::complex);
        auto* grid = page->own (new FbGrid (proc));
        Knob* fbK[3] = { page->own (new Knob (proc, P_fbTime, "Time", col::complex)),
                         page->own (new Knob (proc, P_fbTone, "Tone", col::complex)),
                         page->own (new Knob (proc, P_fbSafety, "Safety", col::complex)) };
        grS->addInfo ("Granular records the last 4 seconds of the synth and plays it back as up to 64 overlapping grains. "
            "Position = how far back the grains read (Jitter scatters it), Size and Density set the grain length and how many start each second, "
            "Pitch / Pitch Jitter transpose them, Reverse is the chance a grain plays backwards, Spread pans them. Feedback writes the grains back "
            "into the recording for washes that build up. Freeze holds the buffer: play a chord, freeze, and keep playing over the frozen cloud. "
            "These modules run once on all notes (per-note granular would cost about 16x the CPU); their knobs still take Mod Matrix routes, using "
            "the newest note's sources.\n\n"
            "Spectral: Blur smears the sound over time, Shift moves every partial by the same number of Hz, Scramble shuffles nearby "
            "frequencies, Tilt brightens or darkens (6 dB per octave), Formant moves the tone colour without changing the pitch, Freeze holds "
            "the current spectrum, Morph blends towards the frozen one, Feedback feeds the output spectrum back in. Bigger FFT sizes sound "
            "smoother on sustained sounds; smaller ones keep attacks sharper and add less latency. Feedback Matrix: each path arrives "
            "Time later; Safety lowers how hard loops can drive, and while any path is up the output never passes 0 dBFS.");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.5f)));
        help->setJustificationType (juce::Justification::topLeft);
        page->onResize = [page, grS, spS, ordS, fbS, grid, fbK, help]
        {
            const int g = 10, W = page->getWidth();
            grS->setBounds (g, g, W - 2 * g, 136);
            spS->setBounds (g, 156, W - 2 * g, 136);
            ordS->setBounds (g, 302, 220, 90);
            fbS->setBounds (g + 230, 302, W - 2 * g - 230, 172);
            grid->setBounds (g + 244, 334, 560, 128);
            grid->toFront (false);
            for (int k = 0; k < 3; ++k) { fbK[k]->setBounds (g + 830 + k * 80, 350, 72, 84); fbK[k]->toFront (false); }
            help->setBounds (g + 6, 484, W - 2 * g - 12, 110);
        };
    }

    // ---------------------------------------------------------------- Capture / Resample
    {
        auto* page = addPage ("Capture");
        auto* cs = sec (page, "Capture / Resample  (record what the synth plays, edit it, send it back in)", col::accent);
        capView = page->own (new CaptureView (proc));
        capView->onEdit = [this] { updateCaptureInfo(); };
        auto* point = page->own (new Choice (proc, P_capPoint, "Record from"));
        capRecord.setTooltip ("Start / stop recording (stops by itself after 8 seconds)");
        capRecord.onClick = [this]
        {
            if (proc.isCapturing()) proc.stopCapture();
            else { proc.startCapture(); setStatus ("Recording - play something, then press Stop"); }
        };
        auto edited = [this] { proc.captureSettings.reverse = capReverse.getToggleState(); proc.captureSettings.normalise = capNormalise.getToggleState();
                               proc.captureSettings.zeroCross = capZero.getToggleState(); proc.captureSettings.fadeInMs = (float) capFadeIn.getValue();
                               proc.captureSettings.fadeOutMs = (float) capFadeOut.getValue(); updateCaptureInfo(); };
        for (auto* t : { &capReverse, &capNormalise, &capZero }) { t->setColour (juce::ToggleButton::tickColourId, col::accent); t->onClick = edited; }
        for (auto* sl : { &capFadeIn, &capFadeOut })
        {
            sl->setSliderStyle (juce::Slider::LinearBar);
            sl->setColour (juce::Slider::trackColourId, col::accent.withAlpha (0.45f));
            sl->setColour (juce::Slider::backgroundColourId, col::panel3);
            sl->setColour (juce::Slider::textBoxTextColourId, col::text);
            sl->onValueChange = edited;
        }
        capFadeIn.setRange (0.0, 500.0, 0.1); capFadeIn.setSkewFactorFromMidPoint (50.0);
        capFadeOut.setRange (0.0, 2000.0, 0.1); capFadeOut.setSkewFactorFromMidPoint (200.0);
        capFadeIn.textFromValueFunction = [] (double v) { return "Fade in " + juce::String (v, v < 10 ? 1 : 0) + " ms"; };
        capFadeOut.textFromValueFunction = [] (double v) { return "Fade out " + juce::String (v, v < 10 ? 1 : 0) + " ms"; };
        capInfo.setColour (juce::Label::textColourId, col::text);
        capInfo.setFont (juce::Font (juce::FontOptions (13.0f)));
        static const std::pair<int, const char*> sends[] = {
            { MegaSynthProcessor::CT_WT1, "> WT 1" }, { MegaSynthProcessor::CT_WT2, "> WT 2" },
            { MegaSynthProcessor::CT_DnaA, "> DNA A" }, { MegaSynthProcessor::CT_DnaB, "> DNA B" },
            { MegaSynthProcessor::CT_Granular, "> Granular" },
            { MegaSynthProcessor::CT_CycleWT1, "Single cycle > WT 1" }, { MegaSynthProcessor::CT_CycleWT2, "Single cycle > WT 2" } };
        static const char* tips[] = { "Load the edited capture into Wavetable 1, tuned to its detected pitch",
                                      "Load the edited capture into Wavetable 2, tuned to its detected pitch",
                                      "Load into Wavetable 1 and make it DNA Splice source A",
                                      "Load into Wavetable 2 and make it DNA Splice source B",
                                      "Put it in the Granular buffer and freeze it, so the grains play it",
                                      "Cut one cycle at the detected pitch and load it into Wavetable 1 as a looping single-cycle wave",
                                      "Cut one cycle at the detected pitch and load it into Wavetable 2 as a looping single-cycle wave" };
        for (int k = 0; k < 7; ++k)
        {
            auto* b = capSends.add (new juce::TextButton (sends[k].second));
            b->setTooltip (tips[k]);
            const auto target = (MegaSynthProcessor::CaptureTarget) sends[k].first;
            b->onClick = [this, target, b]
            {
                const auto err = proc.sendCapture (target);
                setStatus (err.isEmpty() ? "Capture sent " + b->getButtonText().replace ("> ", "to ") : err);
            };
        }
        capSave.onClick = [this]
        {
            if (! proc.hasCapture()) { setStatus ("Nothing captured yet"); return; }
            chooser = std::make_unique<juce::FileChooser> ("Save capture", MegaSynthProcessor::getPatchFolder().getParentDirectory().getChildFile ("Capture.wav"), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (f == juce::File()) return;
                                      setStatus (proc.saveCaptureWav (f.withFileExtension (".wav")) ? "Saved " + f.getFileName() : "Couldn't save there");
                                  });
        };
        for (juce::Component* c : { (juce::Component*) &capRecord, (juce::Component*) &capSave, (juce::Component*) &capReverse, (juce::Component*) &capNormalise,
                                    (juce::Component*) &capZero, (juce::Component*) &capFadeIn, (juce::Component*) &capFadeOut, (juce::Component*) &capInfo })
            page->addAndMakeVisible (c);
        for (auto* b : capSends) page->addAndMakeVisible (b);
        cs->addInfo ("Record from 'After effects' to capture exactly what you hear, or 'Before effects' for the synth and bus "
            "mutation without delay / reverb / chorus. Drag the white markers to trim. Pitch is detected automatically: sending to a wavetable sets its "
            "root note so the sample plays in tune on the keyboard. Resample loop: capture, send to a wavetable, mutate it, capture again. "
            "The last capture is saved with your project and in patch files.");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.5f)));
        help->setJustificationType (juce::Justification::topLeft);
        page->onResize = [this, page, cs, point, help]
        {
            const int g = 10, W = page->getWidth();
            cs->setBounds (g, g, W - 2 * g, 420);
            capRecord.setBounds (g + 14, 44, 110, 34);
            point->setBounds (g + 140, 38, 240, 44);
            capSave.setBounds (W - g - 134, 44, 120, 30);
            capInfo.setBounds (g + 400, 46, 560, 28);
            capView->setBounds (g + 14, 90, W - 2 * g - 28, 170);
            capFadeIn.setBounds (g + 14, 272, 200, 26);
            capFadeOut.setBounds (g + 222, 272, 200, 26);
            capReverse.setBounds (g + 440, 272, 100, 26);
            capNormalise.setBounds (g + 545, 272, 110, 26);
            capZero.setBounds (g + 660, 272, 220, 26);
            int x = g + 14;
            for (auto* b : capSends) { const int w = b->getButtonText().length() > 10 ? 170 : 100; b->setBounds (x, 316, w, 32); x += w + 8; }
            help->setBounds (g + 14, 360, W - 2 * g - 28, 70);
        };
        capFadeIn.setValue (proc.captureSettings.fadeInMs, juce::dontSendNotification);
        capFadeOut.setValue (proc.captureSettings.fadeOutMs, juce::dontSendNotification);
        capReverse.setToggleState (proc.captureSettings.reverse, juce::dontSendNotification);
        capNormalise.setToggleState (proc.captureSettings.normalise, juce::dontSendNotification);
        capZero.setToggleState (proc.captureSettings.zeroCross, juce::dontSendNotification);
    }

    // ---------------------------------------------------------------- Filter & Envelopes
    {
        auto* page = addPage ("Filters");
        auto* f = page->own (new FilterPanel (proc));
        auto* pageEnv = addPage ("Amp & Filter");
        auto* a = sec (pageEnv, "Amp Envelope", col::env);
        for (auto [idx, n] : { std::pair<int, const char*> { P_ampA, "Attack" }, { P_ampD, "Decay" }, { P_ampS, "Sustain" }, { P_ampR, "Release" } })
            a->knob (idx, n);
        auto* fe = sec (pageEnv, "Filter Envelope", col::env);
        for (auto [idx, n] : { std::pair<int, const char*> { P_fEnvA, "Attack" }, { P_fEnvD, "Decay" }, { P_fEnvS, "Sustain" }, { P_fEnvR, "Release" } })
            fe->knob (idx, n);
        // the Master Filter: after every effect, before the Output trim
        auto* mfs = sec (page, "Master Filter", col::filter);
        mfs->addInfo ("A stereo filter at the very end: everything the synth makes, every effect included, passes through it, "
                      "then the Output level. Off, the sound passes straight through. Cutoff, Resonance, Drive and Mix can be modulated "
                      "by anything shared by all notes (LFOs, macros, the mod wheel, the sidechain...): routes follow the newest note's "
                      "sources. (Master Volume, on the Mixer, stays before the effects as it always has, so older patches sound the same.)");
        mfs->add (new ParamToggle (proc, P_mfOn, "On", col::filter), 60, 40);
        mfs->choice (P_mfType, "Type", 120);
        mfs->choice (P_mfSlope, "Slope", 110);
        mfs->choice (P_mfChar, "Character", 140)->box.setTooltip ("A character model (Formant, DJ Filter, Comb...) instead of the plain filter above");
        mfs->newRow();
        mfs->knob (P_mfCutoff, "Cutoff");
        mfs->knob (P_mfRes, "Resonance");
        mfs->knob (P_mfDrive, "Drive");
        mfs->knob (P_mfMix, "Mix");
        mfs->knob (P_mfCharAmt, "Character");
        mfs->knob (P_outGain, "Output");
        auto* mfv = page->own (new MasterFilterView (proc));
        page->onResize = [page, f, mfs, mfv]
        {
            const int g = 10, W = page->getWidth();
            f->setBounds (g, g, W - 2 * g, 262);
            mfs->setBounds (g, 282, W - 2 * g, 170);
            mfv->setBounds (g + 470, 282 + 34, W - 2 * g - 482, 126);
            mfv->toFront (false);
        };
        pageEnv->onResize = [pageEnv, a, fe]
        {
            const int g = 10;
            a->setBounds (g, g, 330, 140);
            fe->setBounds (350, g, 330, 140);
        };
    }

    // ---------------------------------------------------------------- Effects
    {
        // four sub-tabs of FX & Mutation: Space (delay, reverbs, chorus, stereo), Dirt (sampler, multiband),
        // Motion (beat repeat, flanger / phaser, volume shaper) and the Rack (order, On, Mix)
        Page* sub[4] = { addPage ("Space"), addPage ("Dirt"), addPage ("Motion"), addPage ("Rack") };
        auto keep = [] (Page* holder, auto* comp) { holder->children.add (comp); holder->addAndMakeVisible (comp); return comp; };
        auto own = [keep] (Page* holder, Section* s) { return keep (holder, s); };
        auto mk = [this] (const juce::String& t) { return new Section (proc, t, col::fx); };
        auto toggle = [this] (Section* s, int param, const juce::String& text, int w = 110)
        {
            auto* t = new juce::ToggleButton (text);
            t->setColour (juce::ToggleButton::tickColourId, col::fx);
            s->add (t, w, 40);
            fxAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, kParamIds[param], tagParam (*t, param)));
            return t;
        };
        auto slotControls = [&] (Section* s, int slot)
        {
            toggle (s, P_fxOnStutter + 2 * slot, "On", 56);
            s->knob (P_fxMixStutter + 2 * slot, fxSlotIsSend (slot) ? "Return" : "Mix");
        };

        // ---- SPACE (the original effects, unchanged, plus Stereo Tools)
        auto* d = own (sub[0], mk ("Tape / BBD Delay"));
        d->choice (P_delaySync, "Delay Sync", 150);
        d->choice (P_flutterSync, "Flutter Sync", 150);
        d->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_delayTime, "Time" }, { P_delayFeedback, "Feedback" }, { P_tapeTone, "Tone" },
                               { P_tapeFlutter, "Flutter" }, { P_delayMix, "Return" } })
            d->knob (idx, n);
        auto* r = own (sub[0], mk ("90s Reverb + Shimmer"));
        r->choice (P_reverbSync, "Reverb / Shimmer Sync", 170);
        r->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_reverbSize, "Size" }, { P_reverbTone, "Tone" }, { P_shimmerBright, "Shimmer Bright" },
                               { P_reverbMix, "Reverb" }, { P_shimmerMix, "Shimmer" } })
            r->knob (idx, n);
        auto* c = own (sub[0], mk ("Juno Chorus"));
        c->choice (P_chorusSync, "Chorus Sync", 150);
        c->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_chorusRate, "Rate" }, { P_chorusDepth, "Depth" }, { P_chorusMix, "Return" } })
            c->knob (idx, n);
        auto* v = own (sub[0], mk ("Reverse Pitch Shifter Reverb"));
        v->choice (P_reverseSync, "Reverse Sync", 150);
        v->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_reverseTime, "Time" }, { P_reversePitch, "Pitch Drift" }, { P_reverseMix, "Return" } })
            v->knob (idx, n);
        auto* st = own (sub[0], mk ("Stereo Tools  (mono-safe: the mono sum is never changed)"));
        toggle (st, P_fxOnStereo, "On", 56);
        toggle (st, P_sttMono, "Bass Mono", 110)->setTooltip ("Removes the stereo spread below Mono Below, so the sub stays centred");
        for (auto [idx, n] : { std::pair<int, const char*> { P_sttMonoFreq, "Mono Below" }, { P_sttWidth, "Width" }, { P_sttHaas, "Haas" }, { P_fxMixStereo, "Mix" } })
            st->knob (idx, n);

        // ---- DIRT
        auto* sm = own (sub[1], mk ("Vintage Sampler"));
        slotControls (sm, FS_Sampler);
        sm->choice (P_smpModel, "Model", 220)->box.setTooltip ("SP-1200: 12-bit, 26 kHz, no anti-alias filter.  S950: 12-bit at Rate, with anti-alias and the resonant Filter.  "
                                                                "E-mu: 12-bit, 27.8 kHz, gentle filters.  Free: Bits, Rate and Anti-Alias as set.");
        toggle (sm, P_smpAA, "Anti-Alias (Free)", 150);
        for (auto [idx, n] : { std::pair<int, const char*> { P_smpBits, "Bits (Free)" }, { P_smpRate, "Rate" }, { P_smpCutoff, "Filter" },
                               { P_smpRes, "Resonance" }, { P_smpNoise, "Noise" }, { P_smpDrive, "Input Clip" } })
            sm->knob (idx, n);
        auto* mb = own (sub[1], mk ("Multiband  (OTT style: upward and downward compression in three bands)"));
        slotControls (mb, FS_Multiband);
        for (auto [idx, n] : { std::pair<int, const char*> { P_mbcXLow, "Low / Mid" }, { P_mbcXHigh, "Mid / High" }, { P_mbcDepth, "Depth" },
                               { P_mbcTime, "Time" }, { P_mbcGain, "Output" } })
            mb->knob (idx, n);
        mb->newRow();
        const char* bandName[] = { "Low", "Mid", "High" };
        for (int b = 0; b < 3; ++b)
            for (auto [off, n] : { std::pair<int, const char*> { 0, "Up" }, { 1, "Down" }, { 2, "In" }, { 3, "Out" } })
                mb->knob (P_mbcUpL + 4 * b + off, juce::String (bandName[b]) + " " + n);
        toggle (mb, P_mbcSidechain, "Sidechain", 100)->setTooltip ("The downward compression listens to the sidechain input (a kick or break routed in from another track) instead of the synth");
        auto* metersC = keep (sub[1], new MultibandMeters (proc));

        // ---- MOTION
        auto* br = own (sub[2], mk ("Beat Repeat"));
        slotControls (br, FS_Stutter);
        br->choice (P_rptLength, "Length", 80);
        br->choice (P_rptDuration, "Duration", 100);
        br->choice (P_rptMidi, "MIDI Trigger", 150);
        br->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_rptShrink, "Shrink" }, { P_rptPitch, "Pitch Drop" }, { P_rptGate, "Gate" }, { P_rptChance, "Chance" } })
            br->knob (idx, n);
        toggle (br, P_rptReverse, "Reverse", 90);
        toggle (br, P_rptDna, "DNA Gate", 100)->setTooltip ("Each DNA Sequencer step starts a repeat");
        auto* pad = new StutterPad (proc);
        br->add (pad, 170, 60);
        auto* fl = own (sub[2], mk ("Flanger / Phaser"));
        slotControls (fl, FS_Flanger);
        fl->choice (P_flpMode, "Mode", 100);
        fl->choice (P_flpSync, "Sync", 80);
        fl->choice (P_flpStages, "Phaser", 100);
        fl->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_flpRate, "Rate" }, { P_flpDepth, "Depth" }, { P_flpFeedback, "Feedback" },
                               { P_flpManual, "Manual" }, { P_flpSpread, "Spread" }, { P_flpEnvSens, "Env Sens" } })
            fl->knob (idx, n);
        toggle (fl, P_flpTZ, "Through-Zero", 120);
        toggle (fl, P_flpEnv, "Envelope", 100)->setTooltip ("The sweep follows the input level instead of the LFO");
        auto* vs = own (sub[2], mk ("Volume Shaper  (sidechain-style pump)"));
        slotControls (vs, FS_Shaper);
        vs->choice (P_vshRate, "Cycle", 80);
        vs->choice (P_vshTrig, "Trigger", 160);
        vs->knob (P_vshDepth, "Depth");
        vs->knob (P_vshSmooth, "Smooth");
        const char* presetNames[] = { "Pump", "Gate", "1/8 Pump", "Triplet" };
        for (int k = 0; k < 4; ++k)
        {
            auto* b = new juce::TextButton (presetNames[k]);
            b->onClick = [this, k] { proc.fxRack.setCurvePreset (k); };
            vs->add (b, 76, 28);
        }
        vs->newRow();
        toggle (vs, P_vshSidechain, "Sidechain hits", 130)->setTooltip ("Each hit on the sidechain input (a kick routed in from another track) restarts the curve, instead of the Trigger");
        vs->knob (P_schThresh, "Threshold");
        vs->knob (P_schAttack, "Attack");
        vs->knob (P_schRelease, "Release");
        vs->add (new SidechainMeter (proc), 150, 60);
        auto* curve = keep (sub[2], new ShaperCurve (proc));

        // ---- RACK
        auto* rack = keep (sub[3], new RackList (proc));

        auto* sp = sub[0]; auto* dp = sub[1]; auto* mp = sub[2]; auto* rp = sub[3];
        sp->onResize = [sp, d, r, c, v, st]
        {
            const int g = 10, W = sp->getWidth(), w2 = (W - g * 3) / 2, y = 6;
            d->setBounds (g, y, w2, 176);
            r->setBounds (g * 2 + w2, y, w2, 176);
            c->setBounds (g, y + 184, w2, 176);
            v->setBounds (g * 2 + w2, y + 184, w2, 176);
            st->setBounds (g, y + 368, W - 2 * g, 136);
        };
        dp->onResize = [dp, sm, mb, metersC]
        {
            const int g = 10, W = dp->getWidth(), y = 6;
            sm->setBounds (g, y, W - 2 * g, 136);
            mb->setBounds (g, y + 146, W - 2 * g - 230, 236);
            metersC->setBounds (W - g - 220, y + 146, 220, 236);
        };
        mp->onResize = [mp, br, fl, vs, curve]
        {
            const int g = 10, W = mp->getWidth(), w2 = (W - g * 3) / 2, y = 6;
            br->setBounds (g, y, w2, 290);
            fl->setBounds (g * 2 + w2, y, w2, 290);
            vs->setBounds (g, y + 296, 600, 258);
            curve->setBounds (g + 610, y + 296, W - 2 * g - 610, 258);
        };
        rp->onResize = [rp, rack]
        {
            const int g = 10;
            rack->setBounds (g, 10, rp->getWidth() - 2 * g, rp->getHeight() - 20);
        };
    }

    // ---------------------------------------------------------------- Modulation
    {
        // LFO 1-4 (wave, rate, depth and the rest) live on Modulation > LFOs
        auto* pageMe = addPage ("Mod Envelopes");
        Section* env[3];
        for (int i = 0; i < 3; ++i)
        {
            env[i] = sec (pageMe, "Mod Envelope " + juce::String (i + 1), col::env);
            env[i]->knob (P_mEnv1A + 4 * i, "Attack");
            env[i]->knob (P_mEnv1D + 4 * i, "Decay");
            env[i]->knob (P_mEnv1S + 4 * i, "Sustain");
            env[i]->knob (P_mEnv1R + 4 * i, "Release");
        }
        pageMe->onResize = [pageMe, env]
        {
            const int g = 10, W = pageMe->getWidth(), w3 = (W - g * 4) / 3;
            for (int i = 0; i < 3; ++i) env[i]->setBounds (g + i * (w3 + g), g, w3, 120);
        };

        auto* page = addPage ("Assignments");
        auto* asg = sec (page, "Assignments  (pick a target, then set the depth; double-click a knob to zero it)", col::mod);
        for (int i = 0; i < 3; ++i)
            assignSlots.add (page->own (new AssignSlot (proc, P_lfoAssignSource0 + 3 * i, "LFO SLOT " + juce::String (i + 1), col::mod)));
        for (int i = 0; i < 3; ++i)
            assignSlots.add (page->own (new AssignSlot (proc, P_envAssignSource0 + 3 * i, "ENV SLOT " + juce::String (i + 1), col::env)));
        juce::Array<AssignSlot*> sl (assignSlots);
        auto* midi = sec (page, "MIDI Controllers", col::mod);
        midi->add (new ParamToggle (proc, P_wheelLfo1, "Mod wheel sets LFO 1 Depth", col::mod), 230, 30);
        midi->add (new ParamToggle (proc, P_sustainPedal, "Sustain pedal (CC64)", col::mod), 190, 30);
        auto* mw = new juce::Label ({}, "The mod wheel is also a Mod Matrix source. CC7 sets master volume. The MIDI IN panel shows what arrives.");
        mw->setColour (juce::Label::textColourId, col::muted);
        mw->setFont (juce::Font (juce::FontOptions (12.0f)));
        midi->add (mw, 640, 30);
        page->onResize = [page, asg, sl, midi]
        {
            const int g = 10, W = page->getWidth();
            asg->setBounds (g, g, W - 2 * g, 186);
            const int sw = (W - 2 * g - 20 - 2 * 8) / 3;
            for (int i = 0; i < 6; ++i)
            {
                const int row = i / 3, c = i % 3;
                sl[i]->setBounds (g + 10 + c * (sw + 8), g + 30 + row * 78, sw, 74);
                sl[i]->toFront (false);
            }
            midi->setBounds (g, g + 196, W - 2 * g, 76);
        };

        auto* pageCi = addPage ("Cell Instability");
        auto* ci = sec (pageCi, "Cell Instability  (each note wanders on its own: smooth per-note noise on the targets below)", col::env);
        ci->knob (P_ciAmount, "Amount", 70);
        ci->knob (P_ciRate, "Rate", 70);
        for (auto [idx, n] : { std::pair<int, const char*> { P_ciPitch, "Pitch" }, { P_ciCutoff, "Cutoff" }, { P_ciRes, "Resonance" },
                               { P_ciLevel, "Osc Levels" }, { P_ciFold, "Wave Fold" }, { P_ciScan, "WT Scan" }, { P_ciFm, "FM" },
                               { P_ciDna, "DNA Splice" }, { P_ciEnv, "Envelopes" }, { P_ciReso, "Resonator" } })
            ci->knob (idx, n, 70);
        pageCi->onResize = [pageCi, ci]
        {
            const int g = 10, W = pageCi->getWidth();
            ci->setBounds (g, g, W - 2 * g, 126);
        };
    }

    // ---------------------------------------------------------------- Pitch envelope and MSEGs
    {
        auto* pagePe = addPage ("Pitch Envelope");
        auto* page = addPage ("MSEG 1 & 2");
        auto* pe = sec (pagePe, "Pitch Envelope  (808 drops, hoover scoops)", col::env);
        pe->knob (P_pEnvAmt, "Amount");
        pe->knob (P_pEnvA, "Attack");
        pe->knob (P_pEnvD, "Decay");
        pe->knob (P_pEnvCurve, "Curve");
        pe->knob (P_pEnvVel, "Velocity");
        pe->choice (P_pEnvTarget, "Target", 150);
        Section* ms[2]; MsegEditor* ed[2];
        for (int k = 0; k < 2; ++k)
        {
            ms[k] = sec (page, "MSEG " + juce::String (k + 1) + "  (multi-stage envelope: a Mod Matrix source)", col::env);
            ms[k]->add (new ParamToggle (proc, P_mseg1Sync + 2 * k, "Sync (beats)", col::env), 120, 30);
            ms[k]->knob (P_mseg1Scale + 2 * k, "Time Scale", 70);
            ms[k]->newRow();
            auto* pr = new juce::TextButton ("Presets");
            pr->onClick = [this, k, pr]
            {
                juce::PopupMenu m;
                for (int i = 0; i < 4; ++i) m.addItem (i + 1, MsegStore::presetName (i));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pr), [this, k] (int r) { if (r > 0) { proc.msegs.setPreset (k, r - 1); proc.pushHistory ("MSEG preset"); } });
            };
            ms[k]->add (pr, 80, 28);
            ed[k] = page->own (new MsegEditor (proc, k));
        }
        ms[0]->addInfo ("Route MSEG 1 / 2 in the Mod Matrix (or right-click a knob). Sustain holds while the key is down; "
                                                     "a loop repeats while the key is down; after the key is let go the stages after the sustain / loop play. "
                                                     "Sync measures stage times in beats instead of seconds.");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.0f)));
        help->setJustificationType (juce::Justification::topLeft);
        pagePe->onResize = [pagePe, pe]
        {
            const int g = 10, W = pagePe->getWidth();
            pe->setBounds (g, g, W - 2 * g, 120);
        };
        page->onResize = [page, ms, ed, help]
        {
            const int g = 10, W = page->getWidth();
            for (int k = 0; k < 2; ++k)
            {
                const int y = g + k * 220;
                ms[k]->setBounds (g, y, W - 2 * g, 210);
                ed[k]->setBounds (g + 230, y + 34, W - 2 * g - 242, 166);
                ed[k]->toFront (false);
            }
            help->setBounds (g, g + 440, W - 2 * g, 40);
        };
    }

    // ---------------------------------------------------------------- Unison (per oscillator)
    {
        auto* page = addPage ("Unison");
        const char* oscNames[] = { "Oscillator 1", "Oscillator 2", "Oscillator 3", "Wavetable 1", "Wavetable 2", "Complex (Osc 5)" };
        const juce::Colour oscCol[] = { col::osc, col::osc, col::osc, col::wavetable, col::wavetable, col::complex };
        Section* rows[kUniOscs]; UnisonView* views[kUniOscs];
        for (int k = 0; k < kUniOscs; ++k)
        {
            const int b = P_uni1Voices + kUniStride * k;
            rows[k] = sec (page, oscNames[k], oscCol[k]);
            rows[k]->knob (b, "Voices", 64);
            rows[k]->knob (b + 1, "Detune", 64);
            rows[k]->knob (b + 2, "Spread", 64);
            rows[k]->knob (b + 3, "Blend", 64);
            rows[k]->choice (b + 4, "Detune Curve", 118);
            views[k] = page->own (new UnisonView (proc, k));
        }
        auto* same = page->own (new ParamToggle (proc, P_uniSamePhase, "Same phase every note", col::osc));
        same->setTooltip ("Off: the extra voices start at random phases on every note (livelier). On: the same every time (punchier, repeatable).");
        rows[0]->addInfo ("Each oscillator can stack up to 8 voices: the oscillator itself in the centre, plus detuned copies spread "
                                                     "across the stereo field. Blend sets the copies' level against the centre. FM, ring and DNA Splice use the "
                                                     "centre voice. Each extra voice costs about as much CPU as the oscillator itself.");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.0f)));
        help->setJustificationType (juce::Justification::topLeft);
        page->onResize = [page, rows, views, same, help]
        {
            const int g = 10, W = page->getWidth(), w2 = (W - 3 * g) / 2, h = 160;
            for (int k = 0; k < kUniOscs; ++k)
            {
                const int x = g + (k % 2) * (w2 + g), y = g + (k / 2) * (h + g);
                rows[k]->setBounds (x, y, w2, h);
                views[k]->setBounds (x + w2 - 170, y + 36, 160, h - 46);
                views[k]->toFront (false);
            }
            same->setBounds (g, g + 3 * (h + g), 240, 26);
            help->setBounds (g + 250, g + 3 * (h + g), W - 2 * g - 250, 40);
        };
    }

    // ---------------------------------------------------------------- LFOs (sync, retrigger, shapes)
    {
        auto* page = addPage ("LFOs");
        LfoPanel* lp[4];
        for (int k = 0; k < 4; ++k) lp[k] = page->own (new LfoPanel (proc, k, col::mod));
        page->onResize = [page, lp]
        {
            const int g = 8, W = page->getWidth(), H = page->getHeight();
            const int h = (H - 5 * g) / 4;
            for (int k = 0; k < 4; ++k) lp[k]->setBounds (g, g + k * (h + g), W - 2 * g, h);
        };
    }

    // ---------------------------------------------------------------- Arp / Chord
    {
        auto* page = addPage ("Arp / Chord");
        auto* ch = sec (page, "Chord Memory  (one key plays a whole chord)", col::seq);
        ch->add (new ParamToggle (proc, P_chdOn, "On", col::seq), 56, 40);
        ch->choice (P_chdPreset, "Chord", 210);
        ch->knob (P_chdSpread, "Spread");
        ch->knob (P_chdInversion, "Inversion");
        auto* learn = new juce::TextButton ("Learn");
        learn->setTooltip ("Play a chord and let go: it becomes the Learned chord (relative to its lowest note) and Chord Memory turns on");
        learn->onClick = [this] { proc.arpStore.learning = ! proc.arpStore.learning.load(); };
        ch->add (learn, 80, 30);
        auto* disp = page->own (new ChordDisplay (proc));
        auto* ar = sec (page, "Arpeggiator  (plays the held notes, or chords, on the beat)", col::seq);
        ar->add (new ParamToggle (proc, P_arpOn, "On", col::seq), 56, 40);
        ar->add (new ParamToggle (proc, P_arpLatch, "Latch", col::seq), 74, 40);
        ar->choice (P_arpMode, "Mode", 130);
        ar->choice (P_arpRate, "Rate", 130);
        ar->knob (P_arpOctaves, "Octaves");
        ar->knob (P_arpSwing, "Swing");
        ar->knob (P_arpGate, "Gate");
        ar->knob (P_arpRepeats, "Repeats");
        ar->newRow();
        ar->choice (P_arpVelMode, "Velocity", 130);
        ar->knob (P_arpVelFixed, "Fixed Vel");
        ar->choice (P_arpRestart, "Restart", 130);
        ar->knob (P_arpSteps, "Pattern Steps", 90);
        auto* pat = sec (page, "Pattern  (click: On / Accent / Rest; drag: probability)", col::seq);
        auto* grid = page->own (new ArpPatternGrid (proc));
        ch->addInfo ("Chord Memory feeds the arpeggiator. Both play through the voices, so the DNA Sequencer "
                                                     "(restart on each note), the Volume Shaper (each note) and Beat Repeat all follow the arpeggiated notes. "
                                                     "Arp Position and Arp Step Gate are Mod Matrix sources. The step sequencer's notes aren't arpeggiated.");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.0f)));
        help->setJustificationType (juce::Justification::topLeft);
        page->onResize = [page, ch, disp, ar, pat, grid, help]
        {
            const int g = 10, W = page->getWidth();
            ch->setBounds (g, g, W - 2 * g, 120);
            disp->setBounds (W - g - 330, g + 40, 316, 34);
            ar->setBounds (g, 140, W - 2 * g, 210);
            pat->setBounds (g, 360, W - 2 * g, 170);
            grid->setBounds (g + 12, 392, W - 2 * g - 24, 126);
            help->setBounds (g, 540, W - 2 * g, 60);
        };
    }

    // ---------------------------------------------------------------- Mod Matrix
    {
        auto* page = addPage ("Mod Matrix");
        auto* srcSec = sec (page, "More sources", col::mod);
        // (LFO 4's Wave, Rate and Depth are with the other LFOs: Modulation > LFOs)
        srcSec->knob (P_randRate, "Random Rate", 70);
        srcSec->knob (P_ccANum, "CC A", 56);
        srcSec->knob (P_ccBNum, "CC B", 56);
        srcSec->addInfo ("Any source to any knob. Right-click a knob to modulate it; a cyan ring shows the range and a dot shows the "
                                                     "value the newest note hears. Depth is a share of the destination's travel. Envelope times are set at note-on.");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
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
            srcSec->setBounds (g, g, W - 2 * g, 128);
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
        juce::Array<MacroCell*> myCells;
        for (int i = 0; i < 8; ++i) { auto* m = page->own (new MacroCell (proc, i)); macroCells.add (m); myCells.add (m); }
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
        morphAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_sceneMorph], tagParam (morphBtn, P_sceneMorph));
        morphBtn.setColour (juce::ToggleButton::tickColourId, col::mod);
        morphBtn.setTooltip ("On: the sound is a blend of the four scenes at the XY position, and the panel edits the selected scene");
        clearScenesBtn.onClick = [this] { proc.clearScenes(); proc.pushHistory ("Clear scenes"); setStatus ("Scenes cleared"); updateSceneButtons(); };
        page->addAndMakeVisible (morphBtn);
        page->addAndMakeVisible (clearScenesBtn);
        auto* sx = page->own (new Knob (proc, P_sceneX, "Scene X", col::mod));
        auto* sy = page->own (new Knob (proc, P_sceneY, "Scene Y", col::mod));
        scSec->addInfo ("Each scene is a complete sound: every oscillator, filter, envelope, LFO and effect setting "
            "(not the matrix routes, macros, volume or sequencer).\n\n"
            "1. Make a sound and press Store A. Change it and Store B, and so on.\n"
            "2. Turn on Morph and drag the pad. Knobs blend smoothly; switches (waveforms, filter type) "
            "take the nearest scene's setting.\n"
            "3. With Morph on, the panel shows the selected scene (A-D buttons) and every knob you turn edits that scene.\n"
            "With Morph off, the A-D buttons recall a scene onto the panel.\n\n"
            "Scene X / Y are ordinary parameters: automate them, assign them to a macro, or route an LFO or the mod wheel to them in the Mod Matrix "
            "(each note then morphs on its own).");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.5f)));
        help->setJustificationType (juce::Justification::topLeft);
        juce::Array<MacroCell*> cells (myCells);   // this page's own eight (the Perform page has its own)
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

    // ---------------------------------------------------------------- Master Mutate
    {
        auto* page = addPage ("Mutate / Lab");
        auto* ms = sec (page, "Master Mutate  (seeded variations on top of your patch; the knobs don't move until you Commit)", col::complex);
        auto* big = page->own (new Knob (proc, P_mutAmount, "Mutate", col::complex));
        mutSeedLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        mutSeedLabel.setColour (juce::Label::textColourId, col::text);
        mutSeedLabel.setJustificationType (juce::Justification::centred);
        mutSeedLabel.setTooltip ("The seed: the same patch, seed and amount always give exactly the same sound");
        mutNew.setTooltip ("A new random seed");
        mutPrev.setTooltip ("Previous seed");
        mutNext.setTooltip ("Next seed");
        mutCommit.setTooltip ("Bake the current mutation into the knobs (Mutate goes back to 0). Undo reverses it.");
        mutNew.onClick = [this] { proc.mutateNewSeed(); };
        mutPrev.onClick = [this] { proc.mutateStepSeed (-1); };
        mutNext.onClick = [this] { proc.mutateStepSeed (1); };
        mutCommit.onClick = [this]
        {
            if (proc.param (P_mutAmount)->getValue() <= 0.0001f) { setStatus ("Turn Mutate up first"); return; }
            proc.commitMutation();
            setStatus ("Mutation committed to the knobs (Undo to go back)");
        };
        for (auto* b : { &mutNew, &mutPrev, &mutNext, &mutCommit }) page->addAndMakeVisible (*b);
        page->addAndMakeVisible (mutSeedLabel);
        auto* lockTitle = page->own (new juce::Label ({}, "Locks  (a locked group never mutates)"));
        lockTitle->setColour (juce::Label::textColourId, col::muted);
        lockTitle->setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        for (int k = 0; k < ML_COUNT; ++k)
        {
            auto* t = mutLocks.add (new juce::ToggleButton (kMutLockNames[k]));
            t->setColour (juce::ToggleButton::tickColourId, col::complex);
            mutLockAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, kParamIds[P_mutLock1 + k], tagParam (*t, P_mutLock1 + k)));
            page->addAndMakeVisible (t);
        }
        mutHistLabel.setColour (juce::Label::textColourId, col::muted);
        mutHistLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        mutHistLabel.setJustificationType (juce::Justification::topLeft);
        page->addAndMakeVisible (mutHistLabel);
        ms->addInfo ("Mutate adds an offset to every unlocked parameter, computed from the seed, the amount and the parameter's own value. "
            "0% is exactly your patch. Offsets move together within each group (a shared random 'gene'), and are shaped: short attacks stay short "
            "until extreme amounts, feedback tends to go down rather than up, FM and modulator ratios lean towards harmonic values, pitch moves in "
            "whole semitones, and anything switched off (a level, a mix, a depth at zero) stays off. Waveforms switch at higher amounts. "
            "Mutate is a normal parameter: automate it, put it on a macro, or route velocity to it so each note is a different variation.");
        auto* help = page->own (new juce::Label());   // (the explanation is behind the section's (i))
        help->setColour (juce::Label::textColourId, col::muted);
        help->setFont (juce::Font (juce::FontOptions (12.5f)));
        help->setJustificationType (juce::Justification::topLeft);
        // ---- Genetic Lab
        auto* lb = sec (page, "Genetic Lab  (two parents, eight children; each child takes whole groups from one parent or the other)", col::wavetable);
        for (int k = 0; k < 2; ++k)
        {
            labParent[k].setColour (juce::Label::textColourId, col::text);
            labParent[k].setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            labUse[k].setButtonText (k == 0 ? "Current sound > A" : "Current sound > B");
            labUse[k].setTooltip ("Make the sound you're hearing now parent " + juce::String (k == 0 ? "A" : "B"));
            labUse[k].onClick = [this, k] { proc.labSetParent (k); updateLab(); };
        }
        labBreedBtn.onClick = [this]
        {
            const int b = proc.labBreed();
            if (b < 0) { setStatus ("Choose parent A and parent B first"); return; }
            labGen = b;
            setStatus ("Bred generation " + juce::String (proc.lab.find (proc.lab.broods[(size_t) b].children[0])->generation) + " - click a child to hear it");
            updateLab();
        };
        labVar.setSliderStyle (juce::Slider::LinearBar);
        labVar.setRange (0.0, 0.5, 0.01);
        labVar.setColour (juce::Slider::trackColourId, col::wavetable.withAlpha (0.45f));
        labVar.setColour (juce::Slider::backgroundColourId, col::panel3);
        labVar.setColour (juce::Slider::textBoxTextColourId, col::text);
        labVar.textFromValueFunction = [] (double v) { return "Variation " + juce::String (juce::roundToInt (v * 100)) + "%"; };
        labVar.setValue (proc.labVariation, juce::dontSendNotification);
        labVar.onValueChange = [this] { proc.labVariation = (float) labVar.getValue(); };
        labVar.setTooltip ("A light mutation on top of each child (0 = pure combinations of the parents)");
        labGenPrev.onClick = [this] { if (labGen > 0) { --labGen; updateLab(); } };
        labGenNext.onClick = [this] { if (labGen + 1 < (int) proc.lab.broods.size()) { ++labGen; updateLab(); } };
        for (int k = 0; k < 8; ++k)
        {
            labChild[k].setButtonText ("Child " + juce::String (k + 1));
            labChild[k].onClick = [this, k]
            {
                if (labGen < 0 || labGen >= (int) proc.lab.broods.size()) return;
                proc.labAudition (proc.lab.broods[(size_t) labGen].children[(size_t) k]);
                updateLab();
            };
        }
        auto toParent = [this] (int which)
        {
            if (proc.lab.find (proc.lab.current) == nullptr) { setStatus ("Click a child first"); return; }
            proc.labSetParentFromId (which, proc.lab.current);
            updateLab();
        };
        labToA.onClick = [toParent] { toParent (0); };
        labToB.onClick = [toParent] { toParent (1); };
        labSave.onClick = [this]
        {
            const auto* c = proc.lab.find (proc.lab.current);
            const juce::String name = c != nullptr ? c->name.replace ("/", "-") : proc.getPatchName();
            chooser = std::make_unique<juce::FileChooser> ("Save child", MegaSynthProcessor::getPatchFolder().getChildFile (juce::File::createLegalFileName (name) + ".megasynth"), "*.megasynth");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (f == juce::File()) return;
                                      setStatus (proc.savePatchToFile (f.withFileExtension (".megasynth")) ? "Saved " + f.getFileNameWithoutExtension() : "Couldn't save there");
                                      refreshPatchList();
                                  });
        };
        labGenLabel.setColour (juce::Label::textColourId, col::text);
        labGenLabel.setJustificationType (juce::Justification::centred);
        labLine.setColour (juce::Label::textColourId, col::muted);
        labLine.setFont (juce::Font (juce::FontOptions (12.0f)));
        labLine.setJustificationType (juce::Justification::topLeft);
        labLine.setMinimumHorizontalScale (1.0f);
        for (juce::Component* c : { (juce::Component*) &labParent[0], (juce::Component*) &labParent[1], (juce::Component*) &labUse[0], (juce::Component*) &labUse[1],
                                    (juce::Component*) &labBreedBtn, (juce::Component*) &labVar, (juce::Component*) &labGenPrev, (juce::Component*) &labGenNext,
                                    (juce::Component*) &labGenLabel, (juce::Component*) &labToA, (juce::Component*) &labToB, (juce::Component*) &labSave,
                                    (juce::Component*) &labLine })
            page->addAndMakeVisible (c);
        for (auto& b : labChild) page->addAndMakeVisible (b);
        labGen = (int) proc.lab.broods.size() - 1;
        updateLab();

        page->onResize = [this, page, ms, big, lockTitle, help, lb]
        {
            {
                const int g = 10, W = page->getWidth();
                lb->setBounds (g, 300, W - 2 * g, 296);
                for (int k = 0; k < 2; ++k)
                {
                    labUse[k].setBounds (g + 14 + k * 580, 334, 160, 28);
                    labParent[k].setBounds (g + 184 + k * 580, 334, 390, 28);
                }
                labBreedBtn.setBounds (g + 14, 374, 160, 32);
                labVar.setBounds (g + 184, 378, 180, 24);
                labGenPrev.setBounds (g + 400, 376, 36, 28);
                labGenLabel.setBounds (g + 440, 376, 300, 28);
                labGenNext.setBounds (g + 744, 376, 36, 28);
                const int cw = (W - 2 * g - 28 - 7 * 8) / 8;
                for (int k = 0; k < 8; ++k) labChild[k].setBounds (g + 14 + k * (cw + 8), 418, cw, 40);
                labToA.setBounds (g + 14, 470, 150, 28);
                labToB.setBounds (g + 172, 470, 150, 28);
                labSave.setBounds (g + 330, 470, 180, 28);
                labLine.setBounds (g + 14, 508, W - 2 * g - 28, 80);
            }
            const int g = 10, W = page->getWidth();
            ms->setBounds (g, g, W - 2 * g, 280);
            big->setBounds (g + 20, 44, 140, 150);
            mutSeedLabel.setBounds (g + 180, 52, 200, 34);
            mutPrev.setBounds (g + 180, 94, 44, 30);
            mutNew.setBounds (g + 230, 94, 104, 30);
            mutNext.setBounds (g + 340, 94, 44, 30);
            mutCommit.setBounds (g + 180, 140, 204, 34);
            lockTitle->setBounds (g + 420, 40, 400, 20);
            for (int k = 0; k < mutLocks.size(); ++k) mutLocks[k]->setBounds (g + 420 + (k % 4) * 180, 64 + (k / 4) * 30, 175, 26);
            mutHistLabel.setBounds (g + 20, 200, 380, 80);
            help->setBounds (g + 420, 190, W - 2 * g - 430, 90);
        };
    }

    // ---------------------------------------------------------------- Sonic DNA Sequencer
    {
        auto* page = addPage ("DNA Sequencer");
        auto* c = sec (page, "Sonic DNA Sequencer  (each step transforms the sound; also a Mod Matrix source)", col::wavetable);
        auto* on = new juce::ToggleButton ("On");
        on->setColour (juce::ToggleButton::tickColourId, col::wavetable);
        c->add (on, 54, 40);
        dsOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_dsOn], tagParam (*on, P_dsOn));
        c->choice (P_dsSync, "Sync", 120)->box.setTooltip ("Host: the DAW's tempo and beat position (the synth's tempo while the DAW is stopped).  Internal: the synth's tempo.  Free: Free Rate in Hz.");
        c->choice (P_dsRate, "Rate", 120);
        c->choice (P_dsRestart, "Restart", 160)->box.setTooltip ("Never: runs on (in Host sync, locked to the song position).  Each note: starts again at every note.  Each bar: starts again every bar.  Host start: starts when the DAW starts playing.");
        c->choice (P_dsDirection, "Direction", 110);
        c->choice (P_dsSwingGrid, "Swing on", 70);
        c->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_dsSteps, "Steps" }, { P_dsLane2Steps, "Lane 2 Steps" }, { P_dsFreeHz, "Free Rate" },
                               { P_dsSwing, "Swing" }, { P_dsOffset, "Start Offset" }, { P_dsGlide, "Glide" }, { P_dsDepth, "Depth" },
                               { P_dsDensity, "Density" }, { P_dsProbScale, "Probability" } })
            c->knob (idx, n);
        auto* panel = page->own (new DnaSeqPanel (proc));
        page->onResize = [page, c, panel]
        {
            const int g = 10, W = page->getWidth();
            c->setBounds (g, g, W - 2 * g, 178);
            panel->setBounds (g, 196, W - 2 * g, page->getHeight() - 202);
        };
    }

    // ---------------------------------------------------------------- Sequencer
    {
        auto* page = addPage ("Step Sequencer");
        auto* c = sec (page, "Sequencer", col::seq);
        seqRunAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParamIds[P_seqRun], tagParam (seqRunBtn, P_seqRun));
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
    proc.uiWidth = getWidth();

    title.setBounds (14, 8, 220, 30);
    patchPrev.setBounds (14, 42, 26, 24);
    patchBox.setBounds (44, 42, 196, 24);
    patchNext.setBounds (244, 42, 26, 24);
    browseBtn.setBounds (274, 42, 62, 24);
    saveBtn.setBounds (340, 42, 90, 24);
    qualityChoice->setBounds (330, 0, 100, 40);
    int x = 440;
    for (auto* k : headerKnobs) { k->setBounds (x, 4, 72, 80); x += 74; }
    copyBtn.setBounds (820, 10, 120, 28);
    pasteBtn.setBounds (820, 44, 120, 28);
    initBtn.setBounds (948, 10, 58, 28);
    sizeBtn.setBounds (1010, 10, 58, 28);
    abA.setBounds (1076, 10, 30, 28);
    abB.setBounds (1108, 10, 30, 28);
    abCopy.setBounds (1140, 10, 46, 28);
    if (browser != nullptr) browser->setBounds (0, 88, kDesignW, 640);
    undoBtn.setBounds (948, 44, 58, 28);
    redoBtn.setBounds (1010, 44, 58, 28);
    originalBtn.setBounds (1076, 44, 110, 28);
    status.setBounds (812, 74, 374, 14);
    tabs.setBounds (0, 88, kDesignW, 670);
    octDown.setBounds (10, 768, 70, 28);
    octUp.setBounds (10, 802, 70, 28);
    octLabel.setBounds (6, 836, 80, 40);
    keyboard.setBounds (90, 764, kDesignW - 100 - 236, 120);
    if (midiMonitor != nullptr) midiMonitor->setBounds (kDesignW - 240, 764, 230, 120);
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f);
}

void MegaSynthEditor::refreshPatchList()
{
    patchFiles = MegaSynthProcessor::getPatchFiles();
    const auto folder = MegaSynthProcessor::getPatchFolder();
    patchBox.clear (juce::dontSendNotification);
    const auto& factory = tg::factoryPresets();
    patchBox.addSectionHeading ("Factory");
    for (int i = 0; i < (int) factory.size(); ++i)
        patchBox.addItem (juce::String (factory[(size_t) i].category) + " - " + factory[(size_t) i].name, kFactoryIdBase + i + 1);
    patchBox.addSeparator();
    if (patchFiles.size() > 0) patchBox.addSectionHeading ("Your patches");
    for (int i = 0; i < patchFiles.size(); ++i)
        patchBox.addItem (patchFiles[i].getRelativePathFrom (folder).upToLastOccurrenceOf (".", false, false), i + 1);
    if (patchFiles.size() > 0) patchBox.addSeparator();
    patchBox.addItem ("Open patch file...", 9001);
    patchBox.addItem ("Show patch folder", 9002);

    const auto name = proc.getPatchName();
    lastShownName = name;
    for (int i = 0; i < patchFiles.size(); ++i)
        if (patchFiles[i].getFileNameWithoutExtension() == name) { patchBox.setSelectedId (i + 1, juce::dontSendNotification); return; }
    for (int i = 0; i < (int) factory.size(); ++i)
        if (name == factory[(size_t) i].name) { patchBox.setSelectedId (kFactoryIdBase + i + 1, juce::dontSendNotification); return; }
    patchBox.setText (name, juce::dontSendNotification);
}

void MegaSynthEditor::showBrowser (bool show)
{
    if (show)
    {
        if (browser == nullptr)
        {
            browser = std::make_unique<tgui::PatchBrowser> (proc);
            browser->onLoadFactory = [this] (int i) { loadFactory (i); };
            browser->onLoadFile = [this] (const juce::File& f) { loadPatch (f); };
            browser->onLoadInit = [this] { proc.loadInitPatch(); setStatus ("Init loaded"); refreshPatchList(); };
            browser->onClose = [this] { showBrowser (false); };
            content.addChildComponent (*browser);
            browser->setBounds (0, 88, kDesignW, 640);
        }
        browser->open();
    }
    else if (browser != nullptr) browser->setVisible (false);
    browseBtn.setToggleState (show, juce::dontSendNotification);
}

void MegaSynthEditor::showSizeMenu()
{
    juce::PopupMenu m;
    const int pcts[] = { 75, 90, 100, 125, 150 };
    const int cur = juce::roundToInt (100.0 * getWidth() / kDesignW);
    for (int pc : pcts) m.addItem (pc, juce::String (pc) + "%", true, std::abs (pc - cur) <= 1);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sizeBtn),
                     [safe = juce::Component::SafePointer<MegaSynthEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         const int w = kDesignW * r / 100;
                         safe->setSize (w, juce::roundToInt (w * (double) kDesignH / kDesignW));
                     });
}

void MegaSynthEditor::loadFactory (int index)
{
    if (proc.loadFactoryPreset (index))
        setStatus (juce::String ("Loaded ") + tg::factoryPresets()[(size_t) index].name + " - " + tg::factoryPresets()[(size_t) index].description);
    refreshPatchList();
}

void MegaSynthEditor::loadPatch (const juce::File& f)
{
    const auto err = proc.loadPatchFromFile (f);
    setStatus (err.isEmpty() ? "Loaded " + f.getFileNameWithoutExtension() : err);
    refreshPatchList();
}

void MegaSynthEditor::stepPatch (int delta)
{
    // Steps through the factory presets, then the saved patches, and wraps round.
    patchFiles = MegaSynthProcessor::getPatchFiles();
    const auto& factory = tg::factoryPresets();
    const int nf = (int) factory.size(), n = nf + patchFiles.size();
    const auto name = proc.getPatchName();
    int cur = -1;
    for (int i = 0; i < patchFiles.size(); ++i)
        if (patchFiles[i].getFileNameWithoutExtension() == name) cur = nf + i;
    if (cur < 0)
        for (int i = 0; i < nf; ++i)
            if (name == factory[(size_t) i].name) cur = i;
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    if (next < nf) loadFactory (next);
    else loadPatch (patchFiles[next - nf]);
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

void MegaSynthEditor::updateCaptureInfo()
{
    if (capView == nullptr) return;
    capView->repaint();
    if (! proc.hasCapture()) { capInfo.setText ("No capture", juce::dontSendNotification); return; }
    const auto ed = proc.getEditedCapture();
    const double hz = tg::capture::detectPitch (ed, proc.getCaptureRate());
    juce::String t = "Length " + juce::String (ed.getNumSamples() / proc.getCaptureRate(), 2) + " s";
    if (hz > 0.0)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const int m = (int) std::lround (midi);
        t << "    Pitch " << juce::String (hz, 1) << " Hz  (" << names[((m % 12) + 12) % 12] << juce::String (m / 12 - 1)
          << (std::abs (midi - m) > 0.05 ? juce::String ((midi - m) * 100.0 > 0 ? " +" : " ") + juce::String (juce::roundToInt ((midi - m) * 100.0)) + " cents" : juce::String()) << ")";
    }
    else t << "    No steady pitch";
    capInfo.setText (t, juce::dontSendNotification);
}

void MegaSynthEditor::updateLab()
{
    auto& lab = proc.lab;
    if (labGen >= (int) lab.broods.size()) labGen = (int) lab.broods.size() - 1;
    if (labGen < 0 && ! lab.broods.empty()) labGen = (int) lab.broods.size() - 1;
    // only touch the components when something changed
    juce::String key = lab.parentA + lab.parentB + lab.current + juce::String (labGen) + juce::String ((int) lab.broods.size()) + juce::String ((int) lab.archive.size());
    if (key == labShownState) return;
    labShownState = key;
    for (int k = 0; k < 2; ++k)
    {
        const auto* p = lab.find (k == 0 ? lab.parentA : lab.parentB);
        labParent[k].setText (juce::String (k == 0 ? "A: " : "B: ") + (p != nullptr ? p->name : juce::String ("(not set)")), juce::dontSendNotification);
    }
    const bool hasGen = labGen >= 0 && labGen < (int) lab.broods.size();
    if (hasGen)
    {
        const auto& b = lab.broods[(size_t) labGen];
        const auto* c0 = lab.find (b.children.empty() ? juce::String() : b.children[0]);
        const auto* pa = lab.find (b.parentA); const auto* pb = lab.find (b.parentB);
        labGenLabel.setText ("Generation " + juce::String (c0 != nullptr ? c0->generation : 0) + ":  " + (pa ? pa->name : juce::String ("?")) + "  x  " + (pb ? pb->name : juce::String ("?")),
                             juce::dontSendNotification);
    }
    else labGenLabel.setText ("No children yet", juce::dontSendNotification);
    for (int k = 0; k < 8; ++k)
    {
        const bool ok = hasGen && k < (int) lab.broods[(size_t) labGen].children.size();
        labChild[k].setEnabled (ok);
        const bool isCur = ok && lab.broods[(size_t) labGen].children[(size_t) k] == lab.current;
        labChild[k].setColour (juce::TextButton::buttonColourId, isCur ? col::wavetable.withAlpha (0.55f) : col::panel3);
        if (ok)
        {
            const auto* c = lab.find (lab.broods[(size_t) labGen].children[(size_t) k]);
            juce::String tip = "From B: ";
            juce::StringArray fromB;
            for (int g = 0; g < ML_COUNT; ++g) if (c != nullptr && ((c->fromB >> g) & 1u)) fromB.add (kMutLockNames[g]);
            labChild[k].setTooltip (c != nullptr ? c->name + "\n" + tip + fromB.joinIntoString (", ") + "\nEverything else from A" : juce::String());
        }
    }
    labGenPrev.setEnabled (labGen > 0);
    labGenNext.setEnabled (labGen + 1 < (int) lab.broods.size());
    labLine.setText (lab.find (lab.current) != nullptr ? "Now playing: " + lab.lineage (lab.current, 4)
                                                      : juce::String ("Make two sounds the parents (load or build a patch, then 'Current sound > A', then the other > B), "
                                                                      "press Breed and click the children. A child you like can become a parent; "
                                                                      "< and > step back and forward through the generations. The family tree is saved with the project, "
                                                                      "and a patch saved from the lab records its lineage."),
                     juce::dontSendNotification);
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
    if (slot >= routeRows.size()) return;
    showPage ("Mod Matrix");
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

void MegaSynthEditor::updateMixerView()
{
    auto* mixer = pageFor ("Mixer");
    const bool showing = mixer != nullptr && mixer->isShowing();
    proc.getEngine().mixerMeters.store (showing, std::memory_order_relaxed);   // the strip meters cost a little: only while you can see them
    for (int c = 0; c < 2; ++c)
    {
        const float pk = proc.getEngine().outputPeak[(size_t) c].exchange (0.0f, std::memory_order_relaxed);
        if (showing && outMeters[c] != nullptr) outMeters[c]->push (pk);
    }
    if (! showing) return;
    for (int k = 0; k < mixerStrips.size(); ++k)
    {
        mixerStrips[k]->pushMeter (proc.getEngine().mixerPeak[(size_t) k].exchange (0.0f, std::memory_order_relaxed));
        mixerStrips[k]->refresh();
    }
    for (auto* c : engineChips) c->refresh();
}

void MegaSynthEditor::timerCallback()
{
    updateMixerView();
    // remember the open tab and sub-tabs in the project
    {
        juce::StringArray subsNow;
        for (int t = 0; t < tabs.getNumTabs(); ++t)
            subsNow.add (juce::String (dynamic_cast<GroupPage*> (tabs.getTabContentComponent (t)) != nullptr
                                       ? static_cast<GroupPage*> (tabs.getTabContentComponent (t))->current() : 0));
        const auto now = juce::String (tabs.getCurrentTabIndex()) + ";" + subsNow.joinIntoString (",");
        if (now != lastUiTabs) { lastUiTabs = now; proc.setUiTabs (now); }
    }
    abA.setToggleState (proc.abSlot() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.abSlot() == 1, juce::dontSendNotification);
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
    {
        // the Break Chopper's key zone on the keyboard: the Active Break's slices, and the three Break Select keys
        auto plainOf = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
        const bool on = plainOf (P_bcOn) > 0.5f, sel = plainOf (P_bcSelKeys) > 0.5f;
        const int kb = juce::roundToInt (plainOf (P_bcKeyBase)), a = juce::jlimit (0, 2, juce::roundToInt (plainOf (P_bcActive)) - 1);
        const int ns = on ? proc.breakStore.get().brk[a].nSlices : 0;
        juce::String key; key << (on ? 1 : 0) << (sel ? 1 : 0) << kb << a << ns;
        if (key != lastZoneKey)
        {
            lastZoneKey = key;
            keyboard.tint = [on, sel, kb, a, ns] (int n)
            {
                if (! on) return juce::Colours::transparentBlack;
                if (n >= kb && n < kb + ns) return breakColour (a);
                if (sel && n >= kb - 3 && n < kb) return breakColour (n - (kb - 3)).withMultipliedAlpha (0.8f);
                return juce::Colours::transparentBlack;
            };
            keyboard.repaint();
        }
    }
    if (granInfo != nullptr && granInfo->isShowing())
    {
        auto t = sampleInfoText (proc, MegaSynthProcessor::kSlotGranular);
        if (proc.getSample (MegaSynthProcessor::kSlotGranular) != nullptr) t << "   grains playing: " << proc.getEngine().granGrainsShown.load();
        granInfo->setText (t, juce::dontSendNotification);
    }
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
    if (auto* mp = pageFor ("Mod Matrix"); mp != nullptr && mp->isShowing())
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
    if (labLine.isShowing()) updateLab();
    if (flowView != nullptr && flowView->isShowing())
    {
        static_cast<SignalFlowView*> (flowView)->update();
        static_cast<NetworkView*> (netView)->update();
        static_cast<SpectrumView*> (specView)->update();
        static_cast<DnaStrandView*> (dnaView)->update();
    }
    if (perfXY != nullptr && perfXY->isShowing())
    {
        static_cast<XYPad*> (perfXY)->update();
        for (auto* m : macroCells) m->update();
        const int seed = juce::roundToInt (proc.param (P_mutSeed)->convertFrom0to1 (proc.param (P_mutSeed)->getValue()));
        perfSeed.setText ("Seed #" + juce::String (seed), juce::dontSendNotification);
    }
    if (mutSeedLabel.isShowing())
    {
        const int seed = juce::roundToInt (proc.param (P_mutSeed)->convertFrom0to1 (proc.param (P_mutSeed)->getValue()));
        if (seed != lastSeedShown) { lastSeedShown = seed; mutSeedLabel.setText ("Seed #" + juce::String (seed), juce::dontSendNotification); }
        const auto hist = proc.getMutationHistory();
        if (hist.size() != lastHistCount)
        {
            lastHistCount = hist.size();
            juce::String t = "Committed: ";
            if (hist.isEmpty()) t << "none yet";
            for (int i = std::max (0, hist.size() - 4); i < hist.size(); ++i) t << "\n  " << hist[i];
            mutHistLabel.setText (t, juce::dontSendNotification);
        }
    }
    if (capView != nullptr && capView->isShowing())
    {
        const bool rec = proc.isCapturing();
        capRecord.setButtonText (rec ? "Stop" : "Record");
        capRecord.setColour (juce::TextButton::buttonColourId, rec ? juce::Colour (0xffb83a3a) : col::panel3);
        const int len = proc.getRawCapture().getNumSamples();
        if (rec) capView->repaint();
        else if (len != lastCapLen) { lastCapLen = len; capView->refresh(); updateCaptureInfo(); }
    }
    if (xyPad != nullptr && xyPad->isShowing())
    {
        xyPad->update();
        for (auto* m : macroCells) m->update();
        updateSceneButtons();
    }

    if (statusTicks > 0 && --statusTicks == 0) status.setText ({}, juce::dontSendNotification);
}
