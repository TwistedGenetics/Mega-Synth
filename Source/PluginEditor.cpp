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
#include "UI/CorePage.h"
#include "CoreAlgorithms.h"

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
    caption.setInterceptsMouseClicks (false, false);
    // mod depth with the mouse: on a route's arc, or anywhere with Ctrl / Cmd
    slider.onDepthDown = [this] (const juce::MouseEvent& e)
    {
        if (slots.empty()) return false;
        const auto p = e.getEventRelativeTo (this).position;
        const int k = routeAtRing (p);
        const bool anywhere = e.mods.isCommandDown() || e.mods.isCtrlDown();
        if (k < 0 && ! anywhere) return false;
        if (k >= 0) selSlot = slots[(size_t) k];
        if (selSlot < 0) return false;
        if (e.mods.isAltDown() && k >= 0) { proc.clearRoute (selSlot); updateMod(); repaint(); return true; }   // Alt-click an arc: remove it
        dragSlot = selSlot; dragStart = amountOf (dragSlot); dragY = p.y;
        proc.param (P_mod1Amt + dragSlot)->beginChangeGesture();
        repaint();
        return true;
    };
    slider.onDepthDrag = [this] (const juce::MouseEvent& e)
    {
        if (dragSlot < 0) return;
        const float dy = dragY - e.getEventRelativeTo (this).position.y;   // up: more
        setAmount (dragSlot, dragStart + dy / 150.0f * (e.mods.isShiftDown() ? 0.1f : 1.0f));
        updateMod(); repaint();
    };
    slider.onDepthUp = [this] (const juce::MouseEvent&)
    {
        if (dragSlot >= 0) proc.param (P_mod1Amt + dragSlot)->endChangeGesture();   // one undo step for the drag
        dragSlot = -1; repaint();
    };
    slider.onDepthDouble = [this] (const juce::MouseEvent& e)
    {
        const int k = routeAtRing (e.getEventRelativeTo (this).position);
        if (k < 0) return false;
        selSlot = slots[(size_t) k];
        auto* prm = proc.param (P_mod1Amt + selSlot);
        prm->beginChangeGesture(); setAmount (selSlot, 0.0f); prm->endChangeGesture();   // double-click an arc: depth 0
        updateMod(); repaint();
        return true;
    };
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

juce::Colour tgui::modSourceColour (int src)
{
    switch (src > 0 && src < MS_COUNT ? kModSrcKind[src] : K_NONE)
    {
        case K_LFO:    return col::mod;
        case K_ENV:    return col::env;
        case K_MACRO:  return col::accent;
        case K_MIDI:   case K_MPE: return col::fm;
        case K_SEQ:    return col::seq;
        case K_RAND:   return col::supersaw;
        case K_FOLLOW: case K_AUDIO: return col::complex;
        default:       return col::filter;
    }
}

juce::String tgui::modSourceShort (int src)
{
    // initials and numbers: "LFO 1" -> L1, "Mod Wheel" -> MW, "Macro 3" -> M3
    if (src <= 0 || src >= MS_COUNT) return {};
    juce::String out;
    for (auto& w : juce::StringArray::fromTokens (kModSrcNames[src], " ", ""))
        out << (w.containsOnly ("0123456789") ? w : w.substring (0, 1).toUpperCase());
    return out.substring (0, 4);
}

ModSourceChip::ModSourceChip (int src, const juce::String& label) : source (src), text (label)
{
    setTooltip ("Drag onto any knob to modulate it with " + juce::String (kModSrcNames[src]) + " (a new route at 25%). "
                "Then drag the coloured ring around the knob to set the depth.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ModSourceChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto c = modSourceColour (source);
    g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (c); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (text, r, juce::Justification::centred);
}

void ModSourceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 4) return;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! dc->isDragAndDropActive()) dc->startDragging ("modsrc:" + juce::String (source), this);
}

float Knob::amountOf (int slot) const { auto* p = proc.param (P_mod1Amt + slot); return p->convertFrom0to1 (p->getValue()); }

void Knob::setAmount (int slot, float a)
{
    auto* p = proc.param (P_mod1Amt + slot);
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-1.0f, 1.0f, a)));
}

void Knob::updateMod()
{
    bool h = false; float lo = 0, hi = 0;
    std::vector<int> now;
    uint32_t key = 0;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        const auto c = proc.routes.get (i);
        if (! c.active() || c.dst != paramIndex) continue;
        h = true;
        now.push_back (i);
        const float a = amountOf (i);
        key = key * 131u + (uint32_t) (i * 7 + c.src) + (uint32_t) std::lround (a * 1000.0f);
        if (kModSrcBipolar[c.src] && ! c.unipolar) { lo -= std::abs (a); hi += std::abs (a); }
        else if (a > 0) hi += a; else lo += a;
    }
    if (now != slots)
    {
        // a new route is the one the mouse edits; a removed one hands over to the last
        int added = -1;
        for (int i : now) if (std::find (slots.begin(), slots.end(), i) == slots.end()) added = i;
        slots = now;
        if (added >= 0) selSlot = added;
        else if (std::find (slots.begin(), slots.end(), selSlot) == slots.end()) selSlot = slots.empty() ? -1 : slots.back();
        resized();
    }
    const float lv = h ? proc.getEngine().liveOffset[(size_t) paramIndex].load (std::memory_order_relaxed) : 0.0f;
    if (h != hasMod || key != shownRouteKey || std::abs (lo - modLo) > 1.0e-5f || std::abs (hi - modHi) > 1.0e-5f || std::abs (lv - live) > 0.002f)
    {
        hasMod = h; modLo = lo; modHi = hi; live = lv; shownRouteKey = key;
        repaint();
    }
    const int cc = proc.ccForParam (paramIndex);
    const bool learning = proc.learningParam() == paramIndex;
    if (cc != shownCC || learning != shownLearn) { shownCC = cc; shownLearn = learning; repaint(); }
}

void Knob::selectNextRoute()
{
    if (slots.empty()) return;
    auto it = std::find (slots.begin(), slots.end(), selSlot);
    selSlot = (it == slots.end() || it + 1 == slots.end()) ? slots.front() : *(it + 1);
    repaint();
}

Knob::Ring Knob::ring() const
{
    Ring g;
    const auto layout = slider.getLookAndFeel().getSliderLayout (const_cast<KnobSlider&> (slider));
    const auto bounds = layout.sliderBounds.toFloat().translated ((float) slider.getX(), (float) slider.getY()).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    g.c = bounds.getCentre(); g.r = radius - 10.0f;   // (just inside the knob's own value arc)
    const auto rp = slider.getRotaryParameters();
    g.start = rp.startAngleRadians; g.span = rp.endAngleRadians - rp.startAngleRadians;
    return g;
}

int Knob::routeAtRing (juce::Point<float> p) const
{
    if (slots.empty()) return -1;
    const auto g = ring();
    const float d = p.getDistanceFrom (g.c);
    const int n = (int) std::min<size_t> (slots.size(), 3);
    // the arcs sit 3.5 px apart, from the outside in; anything from just outside the outer one to just inside the inner one
    if (d > g.r + 5.0f || d < g.r - 3.5f * (float) (n - 1) - 3.5f) return -1;
    return juce::jlimit (0, n - 1, juce::roundToInt ((g.r - d) / 3.5f));
}

juce::Point<float> Knob::ringPointForTests (int routeIndex, float proportion) const
{
    const auto g = ring();
    const float a = g.start + proportion * g.span, r = g.r - 3.5f * (float) routeIndex;
    return { g.c.x + r * std::sin (a), g.c.y - r * std::cos (a) };
}

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    if (! d.description.toString().startsWith ("modsrc:")) return false;
    const auto& m = tg::meta (paramIndex);
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    return m.modulatable && src > 0 && src < MS_COUNT && (kModSrcKind[src] != K_AUDIO || m.audioRate);
}

void Knob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    const int slot = proc.addRoute (src, paramIndex, 0.25f);
    if (slot >= 0) { selSlot = slot; if (onShowRoute) onShowRoute (-1 - slot); }
    updateMod(); repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    // the route chip: the next route on this knob
    if (slots.size() > 0 && chipBounds().contains (e.getPosition())) selectNextRoute();
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    // MIDI learn: the mapped controller, or an outline while waiting for one
    if (shownLearn)
    {
        g.setColour (col::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
    }
    if (dropHover)
    {
        g.setColour (col::mod);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
    }
    if (shownCC >= 0 || shownLearn)
    {
        g.setColour (shownLearn ? col::accent : col::muted);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (shownLearn ? "LEARN" : "CC" + juce::String (shownCC), getLocalBounds().removeFromTop (14).removeFromRight (34), juce::Justification::centredRight);
    }
    if (! hasMod) return;
    const auto gr = ring();
    const float base = (float) slider.valueToProportionOfLength (slider.getValue());
    // one arc per route (up to three, outside in), in its source's colour; the edited one bolder
    for (size_t k = 0; k < slots.size() && k < 3; ++k)
    {
        const int slot = slots[k];
        const auto c = proc.routes.get (slot);
        const float a = amountOf (slot);
        const bool bip = kModSrcBipolar[c.src] && ! c.unipolar;
        const float p0 = juce::jlimit (0.0f, 1.0f, base + (bip ? -std::abs (a) : std::min (0.0f, a)));
        const float p1 = juce::jlimit (0.0f, 1.0f, base + (bip ? std::abs (a) : std::max (0.0f, a)));
        const float r = gr.r - 3.5f * (float) k;
        juce::Path arc;
        arc.addCentredArc (gr.c.x, gr.c.y, r, r, 0.0f, gr.start + p0 * gr.span, gr.start + std::max (p1, p0 + 0.003f) * gr.span, true);
        const bool sel = slot == selSlot;
        g.setColour (modSourceColour (c.src).withAlpha (sel ? 0.95f : 0.55f));
        g.strokePath (arc, juce::PathStrokeType (sel ? 2.6f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float a = gr.start + juce::jlimit (0.0f, 1.0f, base + live) * gr.span;
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre ({ gr.c.x + gr.r * std::sin (a), gr.c.y - gr.r * std::cos (a) }));
    // the route chip: which route the mouse edits (click: the next one)
    if (selSlot >= 0)
    {
        const auto c = proc.routes.get (selSlot);
        const auto cb = chipBounds().toFloat().reduced (0.5f);
        const auto colr = modSourceColour (c.src);
        g.setColour (colr.withAlpha (0.25f)); g.fillRoundedRectangle (cb, 6.0f);
        g.setColour (colr); g.drawRoundedRectangle (cb, 6.0f, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
        g.drawFittedText (modSourceShort (c.src) + (slots.size() > 1 ? "+" : ""), cb.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
    // while dragging a depth: the route, its depth and where it takes the knob
    if (dragSlot >= 0)
    {
        const auto c = proc.routes.get (dragSlot);
        const float amt = amountOf (dragSlot);
        auto* prm = proc.param (paramIndex);
        const float to = prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, prm->getValue() + amt));
        const juce::String t = juce::String (kModSrcNames[c.src]) + ": " + formatParam (P_mod1Amt + dragSlot, amt) + "  (to " + formatParam (paramIndex, to) + ")";
        auto box = getLocalBounds().removeFromBottom (16).toFloat();
        box = box.withSizeKeepingCentre (std::max (box.getWidth(), 120.0f), box.getHeight());
        g.setColour (col::panel.withAlpha (0.95f)); g.fillRoundedRectangle (box, 4.0f);
        g.setColour (modSourceColour (c.src)); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawFittedText (t, box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
}

void Knob::resized()
{
    auto r = getLocalBounds();
    auto cap = r.removeFromTop (15);
    caption.setBounds (slots.empty() ? cap : cap.withTrimmedLeft (std::min (24, getWidth() / 3)));   // (room for the route chip)
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

void Section::addTitleChip (juce::Component* c, int width)
{
    owned.add (c);
    addAndMakeVisible (c);
    titleChips.push_back ({ c, width });
    resized();
}

void Section::resized()
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)), title, 0.0f, 0.0f);
    const int tw = juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth());
    if (infoBadge != nullptr) infoBadge->setBounds (juce::jmin (getWidth() - 26, 23 + tw + 8), 9, 16, 16);
    int cx = 23 + tw + 8 + (infoBadge != nullptr ? 24 : 0);
    for (auto& [c, w] : titleChips) { c->setBounds (cx, 8, w, 18); cx += w + 4; }
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

    for (auto* b : { &copyBtn, &pasteBtn, &initBtn, &randomBtn, &octDown, &octUp, &undoBtn, &redoBtn, &originalBtn }) content.addAndMakeVisible (*b);
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
    saveBtn.onClick = [this] { showSaveDialog(); };
    saveBtn.setTooltip ("Save the patch into your patch folder, with a category, tags, author and note (or anywhere with Save As...)");
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
    randomBtn.setTooltip ("Random: a new patch from a new seed, with the settings on Lab > Random Patch (Undo goes back)");
    randomBtn.onClick = [this]
    {
        if (proc.randomBusy()) return;
        proc.randomOptions.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (1000000000) + 1u;
        proc.startRandomPatch (proc.randomOptions);
        setStatus ("Making a random patch (seed " + juce::String ((juce::int64) proc.randomOptions.seed) + ")...");
    };
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

namespace
{
    // a drop-down for a choice parameter with no caption (for a section's title row)
    struct BareChoice : juce::Component
    {
        BareChoice (MegaSynthProcessor& p, int idx)
        {
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.param (idx))) box.addItemList (c->choices, 1);
            addAndMakeVisible (box);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, kParamIds[idx], tagParam (box, idx));
        }
        void resized() override { box.setBounds (getLocalBounds()); }
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };

    // a small value bar for a float parameter with no caption (for a section's title row)
    struct BareSlider : juce::Component
    {
        BareSlider (MegaSynthProcessor& p, int idx, juce::Colour c)
        {
            s.setSliderStyle (juce::Slider::LinearBar);
            s.setColour (juce::Slider::trackColourId, c.withAlpha (0.6f));
            s.setColour (juce::Slider::textBoxTextColourId, col::text);
            addAndMakeVisible (s);
            att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kParamIds[idx], tagParam (s, idx));
        }
        void resized() override { s.setBounds (getLocalBounds()); }
        juce::Slider s;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    // a button that holds a switch parameter on while it's pressed (Throw, Kick the tank); lit while lit() says so
    struct MomentaryParamButton : juce::TextButton, private juce::Timer
    {
        MomentaryParamButton (MegaSynthProcessor& p, int idx, const juce::String& text, juce::Colour c, std::function<bool()> litFn)
            : juce::TextButton (text), proc (p), param (idx), lit (std::move (litFn))
        {
            tagParam (*this, idx);   // (right-click: MIDI Learn, like the other controls)
            setColour (juce::TextButton::buttonOnColourId, c);
            setClickingTogglesState (false);
            startTimerHz (15);
        }
        ~MomentaryParamButton() override { stopTimer(); }
        void mouseDown (const juce::MouseEvent& e) override { juce::TextButton::mouseDown (e); if (! e.mods.isPopupMenu()) set (1.0f); }
        void mouseUp (const juce::MouseEvent& e) override { juce::TextButton::mouseUp (e); set (0.0f); }
        void set (float v) { auto* prm = proc.param (param); prm->beginChangeGesture(); prm->setValueNotifyingHost (v); prm->endChangeGesture(); }
        void timerCallback() override { const bool on = proc.param (param)->getValue() > 0.5f || (lit && lit()); if (on != getToggleState()) setToggleState (on, juce::dontSendNotification); }
        MegaSynthProcessor& proc;
        int param;
        std::function<bool()> lit;
    };

    // a duck's gain, as a bar that dips
    struct DuckMeter : juce::Component, private juce::Timer
    {
        DuckMeter (MegaSynthProcessor& p, int d) : proc (p), duck (d) { startTimerHz (30); }
        ~DuckMeter() override { stopTimer(); }
        void timerCallback() override { const float v = proc.getEngine().fx().duckShown[duck].load(); if (std::abs (v - shown) > 0.002f) { shown = v; repaint(); } }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (col::panel3); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (col::fx.withAlpha (0.45f)); g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, shown)), 3.0f);
            g.setColour (col::text); g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawText (shown < 0.999f ? juce::String (juce::Decibels::gainToDecibels (shown, -60.0f), 1) + " dB" : juce::String ("no duck"), r, juce::Justification::centred);
        }
        MegaSynthProcessor& proc;
        int duck;
        float shown = 1.0f;
    };

    juce::Colour rxTypeColour (int type)
    {
        static const juce::Colour c[4] = { juce::Colour (0xffff5a5a), juce::Colour (0xffffd166), juce::Colour (0xff4dd0e1), juce::Colour (0xffb0b8c8) };
        return c[juce::jlimit (0, 3, type)];
    }

    // Break React's hit lights (K, S, H and any) and a lane of the recent hits, where they fell in the bar
    struct ReactLights : juce::Component
    {
        explicit ReactLights (MegaSynthProcessor& p) : proc (p) {}
        void tick()
        {
            const auto& en = proc.getEngine();
            const int c = en.rxLogCount.load();
            for (int k = std::max (lastCount, c - tg::Engine::kRxLog); k < c; ++k)
            {
                const int v = en.rxLog[(size_t) (k % tg::Engine::kRxLog)].load();
                flash[(v >> 12) & 3] = 1.0f;
            }
            lastCount = c;
            for (auto& f : flash) f *= 0.8f;
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            const char* names[4] = { "K", "S", "H", "?" };
            const auto& en = proc.getEngine();
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            for (int k = 0; k < 4; ++k)
            {
                const auto r = juce::Rectangle<float> (4.0f + k * 30.0f, 4.0f, 22.0f, 22.0f);
                g.setColour (rxTypeColour (k).withAlpha (0.18f + 0.82f * flash[k]));
                g.fillEllipse (r);
                g.setColour (rxTypeColour (k));
                g.drawEllipse (r, 1.2f);
                g.setColour (flash[k] > 0.4f ? juce::Colours::black : rxTypeColour (k));
                g.drawText (names[k], r, juce::Justification::centred);
            }
            // the lane: one bar, its beats, and the last 32 hits (newest brightest), a row per type
            const auto lane = juce::Rectangle<float> (128.0f, 2.0f, (float) getWidth() - 132.0f, 26.0f);
            g.setColour (juce::Colour (0xff141a24)); g.fillRoundedRectangle (lane, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            for (int b = 1; b < 16; ++b) g.fillRect (lane.getX() + lane.getWidth() * b / 16.0f, lane.getY() + (b % 4 == 0 ? 0.0f : 18.0f), 1.0f, b % 4 == 0 ? lane.getHeight() : 8.0f);
            const int c = en.rxLogCount.load();
            const int n = std::min (32, c);
            for (int j = 0; j < n; ++j)
            {
                const int k = c - 1 - j;
                const int v = en.rxLog[(size_t) (k % tg::Engine::kRxLog)].load();
                const float place = (float) (v & 4095) / 4096.0f;
                const int type = (v >> 12) & 3;
                const float y = lane.getY() + 2.0f + (type == 3 ? 0 : type) * 7.5f;
                g.setColour (rxTypeColour (type).withAlpha (1.0f - 0.8f * (float) j / 32.0f));
                g.fillRect (lane.getX() + place * lane.getWidth() - 1.0f, y, 3.0f, 7.0f);
            }
        }
        MegaSynthProcessor& proc;
        int lastCount = 0;
        float flash[4] {};
    };
}

void MegaSynthEditor::buildPages()
{
    // The tabs, in signal-flow order. A tab with sub-tabs is a GroupPage; every page is found by its name.
    struct TabDef { const char* name; juce::StringArray subs; juce::Colour colour; };
    const TabDef layout[] = {
        { "Perform", { "Perform", "Performance FX" }, col::accent }, { "Overview", {}, col::accent },
        { "Oscillators", { "Osc 1-3 & Sub", "Wavetables", "Complex & SuperSaw", "Unison", "FM & Ring", "Granular Synth", "Reese", "Reese Shape", "Core A", "Core B" }, col::osc },
        { "Breaks", { "Break Chopper", "Break Sequencer", "Break React" }, col::complex },
        { "Mixer", {}, col::mixer }, { "Filters", {}, col::filter },
        { "Envelopes", { "Amp & Filter", "Mod Envelopes", "Pitch Envelope", "MSEG 1 & 2" }, col::env },
        { "FX & Mutation", { "Note Mutation", "Bus Mutation", "Space", "Dub & Spring", "Reverbs", "Width", "Ducking", "Dirt", "Motion", "Rack" }, col::fx },
        { "Modulation", { "LFOs", "Mod Matrix", "Assignments", "Macros & Scenes", "Cell Instability" }, col::mod },
        { "Sequencers", { "Step Sequencer", "Arp / Chord", "DNA Sequencer" }, col::seq },
        { "Lab", { "Mutate / Lab", "Capture", "Random Patch" }, col::wavetable } };
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
        for (int k = 0; k < 8; ++k) macSec->addTitleChip (new ModSourceChip (MS_Macro1 + k, "M" + juce::String (k + 1)), 30);
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
                                  "after you let go. Akai Stretch is the S1000's cyclic time-stretch on the whole mix: Crunch (real time; Pitch "
                                  "shifts it), Slow (from the press, x0.75 or x0.5), Fast (the last bar looped at x1.5 or x2) or Halftime (the last "
                                  "bar over two). Right-click a pad for MIDI Learn; MIDI Keys plays them from 14 keys.)", col::fx);
        auto* ticker = page->own (new PerfTicker());
        juce::Array<PerfPad*> pads;
        for (int k = 0; k < kPfPads; ++k)
        {
            auto* pd = page->own (new PerfPad (proc, k, (k < PerfFx::kTimePads || k == PerfFx::Akai) ? col::seq : (k >= PerfFx::Echo ? col::mod : col::fx)));
            pads.add (pd); ticker->pads.add (pd);
        }
        auto* latch = page->own (new ParamToggle (proc, P_pfLatch, "Latch", col::fx));
        auto* quant = page->own (new Choice (proc, P_pfQuant, "Quantise"));
        auto* keys = page->own (new Choice (proc, P_pfKeys, "MIDI Keys"));
        auto* order = page->own (new juce::Label ({}, "Order: 1 Time (Repeat / Reverse / Tape Stop / Tape Start / Backspin / Akai Stretch)  >  2 Pitch Down  >  3 Stutter  >  4 Trance Gate  >  "
                                                     "5 DJ Filter  >  6 Bitcrush  >  7 Vinyl  >  8 Echo Throw  >  9 Reverb Throw  >  Master Filter"));
        order->setColour (juce::Label::textColourId, col::muted);
        order->setFont (juce::Font (juce::FontOptions (11.0f)));

        auto* xySec = sec (page, "XY Pads  (Kaoss-style: X and Y are Mod Matrix sources, so each axis can drive any Performance FX setting or any "
                                 "Mod Matrix destination. Assign adds a route (you can change its depth in the Mod Matrix). Spring: back to the centre "
                                 "when you let go; off, it stays where you leave it.)", col::mod);
        for (int k = 0; k < 4; ++k) xySec->addTitleChip (new ModSourceChip (MS_PfX1 + k, juce::String (k % 2 == 0 ? "X" : "Y") + juce::String (k / 2 + 1)), 30);
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
        set->choice (P_pfAkaiMode, "Akai Mode", 110)->box.setTooltip ("Crunch: real time, Pitch shifts it through the cycles. Slow: from the press. Fast: the last bar looped faster. Halftime: the last bar over two.");
        set->choice (P_pfAkaiSpeed, "Akai Speed (Slow / Fast)", 150);
        set->choice (P_pfAkaiSync, "Akai Cycle", 120);
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
        set->knob (P_pfAkaiCycle, "Akai Cycle", 66);
        set->knob (P_pfAkaiXfade, "Akai Xfade", 66);
        set->knob (P_pfAkaiPitch, "Akai Pitch", 66);
        set->knob (P_pfAkaiMix, "Akai Mix", 66);

        page->onResize = [page, padSec, pads, latch, quant, keys, order, xySec, xys, xyCtl, set]
        {
            const int g = 10, W = page->getWidth();
            const int padW = 760;
            padSec->setBounds (g, g, padW, 300);
            const int cols = 5, pw = (padW - 20 - (cols - 1) * 8) / cols, ph = 64;
            for (int k = 0; k < pads.size(); ++k)
                pads[k]->setBounds (g + 10 + (k % cols) * (pw + 8), g + 34 + (k / cols) * (ph + 8), pw, ph);
            // the last row: Vinyl, Echo, Reverb, Akai Stretch, then Latch; below: MIDI Keys, Quantise and the order
            const int ry = g + 34 + 2 * (ph + 8), cx = g + 10 + 4 * (pw + 8);
            latch->setBounds (cx, ry + 16, 76, 30);
            keys->setBounds (g + 10, ry + ph + 10, 200, 44);
            quant->setBounds (g + 216, ry + ph + 10, 140, 44);
            order->setBounds (g + 364, ry + ph + 12, padW - 374, 40);
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
        sub->newRow();
        sub->choice (P_subRoute, "Route", 254)->box.setTooltip ("Normal: through the filters and every effect, as always. Dry: the sub skips the filters, "
                                                               "the bus stages, the effects and Performance FX (keeping its envelope, level and Mixer strip), "
                                                               "and joins before or after the Master Filter.");
        sub->newRow();
        sub->add (new ParamToggle (proc, P_subMono, "Mono below 120 Hz (dry)", col::osc), 220, 30);

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
            sub->setBounds (g + 3 * (w4 + g), g, w4, 258);
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

    // ---------------------------------------------------------------- Breaks > Break Chopper
    {
        auto* page = addPage ("Break Chopper");
        auto* ticker = page->own (new StoreTicker());
        auto selected = std::make_shared<int> (0);
        auto activeOf = [this] { return juce::jlimit (0, tg::BreakData::kBreaks - 1, juce::roundToInt (proc.param (P_bcActive)->convertFrom0to1 (proc.param (P_bcActive)->getValue())) - 1); };
        auto* smp = sec (page, "Breaks  (Four breaks, each with its own sample, slicing, tempo, slice settings, stretch and colour. "
                               "The selector picks the Active Break: the one you edit here and the one the keys play. All four share one key zone "
                               "from First Key up (C1 by default); the Break Select keys just below it (A0, A#0, B0 for Breaks 1-3, G#0 for Break 4) switch the Active Break "
                               "while you play. Click a slice's label to hear and edit it. Drag a marker to move it (Alt: fine, no snap; arrow keys nudge); the "
                               "triangles along the bottom are the slice's trim. Mouse wheel or pinch zooms, Shift+wheel scrolls. In Manual markers mode a "
                               "click adds a marker, a double-click splits a slice, right-click or Delete removes one. Each break is saved inside the "
                               "project and patch, cut to 30 s at most.)", col::complex);
        auto* selector = page->own (new Segmented (proc, -1, { "Break 1", "Break 2", "Break 3", "Break 4" }, breakColour (0)));
        tagParam (*selector, P_bcActive);
        selector->setTooltip ("The Active Break: the one shown and edited here, and the one the keys play");
        selector->onSelect = [this] (int k) { auto* prm = proc.param (P_bcActive); prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (k + 1))); prm->endChangeGesture(); };
        juce::Array<SampleWaveView*> minis;
        for (int k = 0; k < tg::BreakData::kBreaks; ++k)
        {
            auto* mv = page->own (new SampleWaveView (proc, MegaSynthProcessor::breakSlot (k), breakColour (k)));
            mv->emptyText = "Break " + juce::String (k + 1) + ": empty";
            mv->outlineShade = true;
            minis.add (mv);
        }
        auto* wave = page->own (new SampleWaveView (proc, MegaSynthProcessor::breakSlot (0), breakColour (0)));
        for (int k = 0; k < tg::BreakData::kBreaks; ++k)
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
        for (int k = 0; k < tg::BreakData::kBreaks; ++k)
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
            sn->choice (P_bcFxRoute, "Effects (all breaks)", 200)->box.setTooltip ("Through the effects (as always), or past them so the delay and reverb don't touch the breaks: "
                                                                                 "into Performance FX, into the Master Filter, or after everything. Also on the Mixer page.");
            sound.add (sn);
        }
        auto* all = sec (page, "All Breaks  (Repitch: faster = higher, like a sampler. Akai Cyclic: S1000-style cyclic time-stretch; Cycle "
                               "Length and Crossfade set its grain and flutter. Clean: WSOLA, which finds the best place to splice; Quality sets how "
                               "hard it searches. Tempo Sync stretches every break to the song's tempo. Through Synth Filter sends the breaks through "
                               "Filter 1's settings. Select Keys: the three keys below the zone choose the Active Break (On the Beat: it changes on "
                               "the next beat). Level, Pan, Mute and Solo are the Mixer's Break strip.)", col::complex);
        {
            // Slice Keys: On (as always), Off (the keys play the synth), Auto (off while the Break Sequencer plays)
            auto* skLbl = new juce::Label ({}, "Slice Keys");
            skLbl->setFont (juce::Font (juce::FontOptions (12.0f)));
            skLbl->setColour (juce::Label::textColourId, col::muted);
            skLbl->setJustificationType (juce::Justification::centredRight);
            all->addTitleChip (skLbl, 70);
            auto* sk = new BareChoice (proc, P_bcSliceKeys);
            sk->box.setTooltip ("Slice Keys. On: the slice keys (from First Key up) and the Select Keys play the breaks. Off: they play the synth. "
                                "Auto: they play the synth while the Break Sequencer is playing (so Break React gets its hits from the patterns "
                                "and you get the whole keyboard), and the breaks when it's stopped or Key Entry is on.");
            all->addTitleChip (sk, 84);
            auto* skState = new juce::Label();
            skState->setFont (juce::Font (juce::FontOptions (11.5f)));
            skState->setColour (juce::Label::textColourId, juce::Colour (0xffffb347));
            all->addTitleChip (skState, 230);
            sliceKeysStateLabel = skState;
        }
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
        knobs.add (new StoreKnob ("React Type", 0, 4, 1, col::complex, sliceGet ([] (const tg::BreakSlice& x) { return (float) x.hitType; }), sliceSet ([] (tg::BreakSlice& x, float v) { x.hitType = juce::roundToInt (v); }),
                                  [] (double v) { static const char* n[] = { "Auto", "Kick", "Snare", "Hat", "None" }; return juce::String (n[juce::jlimit (0, 4, juce::roundToInt (v))]); }));
        knobs.getLast()->setTooltip ("What Break React counts this slice as: Auto (from the analysis), Kick, Snare, Hat, or None (never a hit)");
        toggles.add (new StoreToggle ("Reverse", col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.reverse; }), sliceSet ([] (tg::BreakSlice& x, bool v) { x.reverse = v; })));
        toggles.add (new StoreToggle ("Loop", col::complex, sliceGet ([] (const tg::BreakSlice& x) { return x.loop; }), sliceSet ([] (tg::BreakSlice& x, bool v) { x.loop = v; })));
        for (auto* k : knobs) ed->add (k, 64, 84);
        ed->add (toggles[0], 78, 40);
        ed->add (toggles[1], 60, 40);
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
                for (int k = 0; k < tg::BreakData::kBreaks; ++k) { slicing[k]->setVisible (k == a); sound[k]->setVisible (k == a); }
                ed->colour = breakColour (a); smp->colour = breakColour (a);
                ed->repaint(); smp->repaint();
            }
            for (auto* k : knobs) k->refresh();
            for (auto* t : toggles) t->refresh();
            info->setText (breakInfoText (proc, a), juce::dontSendNotification);
            if (sliceKeysStateLabel != nullptr)
            {
                const int sk = juce::roundToInt (proc.param (P_bcSliceKeys)->convertFrom0to1 (proc.param (P_bcSliceKeys)->getValue()));
                const juce::String st = sk == 0 ? juce::String() : proc.sliceKeysTakeNotes() ? juce::String ("on (Auto, sequencer stopped)")
                                      : (sk == 1 ? juce::String ("off: the keys play the synth") : juce::String ("off (Auto, sequencer running)"));
                if (sliceKeysStateLabel->getText() != st) sliceKeysStateLabel->setText (st, juce::dontSendNotification);
            }
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
            selector->setBounds (g + 10, g + 34, 330, 26);
            const int mx = g + 350, mw = (W - 2 * g - 360) / 4;
            for (int k = 0; k < tg::BreakData::kBreaks; ++k) minis[k]->setBounds (mx + k * mw, g + 34, mw - 6, 26);
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

    // ---------------------------------------------------------------- Oscillators > Reese
    {
        auto* page = addPage ("Reese");
        reesePresetsChip = [this] (Section* s)
        {
            auto* b = new juce::TextButton ("Reese Presets...");
            b->setTooltip ("Presets for the Reese engine only (the Reese and Reese Shape pages): they set just its controls, as one undo step");
            b->onClick = [this, b]
            {
                juce::PopupMenu m;
                for (int k = 0; k < MegaSynthProcessor::kNumReesePresets; ++k) m.addItem (k + 1, MegaSynthProcessor::reesePresetName (k));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b), [this] (int r)
                {
                    if (r <= 0) return;
                    proc.applyReesePreset (r - 1);
                    setStatus (juce::String ("Reese: ") + MegaSynthProcessor::reesePresetName (r - 1));
                });
            };
            s->addTitleChip (b, 120);
        };
        const auto c = juce::Colour (0xffff7a59);
        auto* eng = sec (page, "Reese Engine", c);
        reesePresetsChip (eng);
        eng->addInfo ("2, 3 or 4 detuned saws (or squares) per note, made for jungle and drum & bass basses. It goes through the filters "
                      "and everything else like the other oscillators, with its own Mixer strip. Off (Level 0) it costs nothing and "
                      "changes nothing.");
        eng->knob (P_rsLevel, "Level", 62);
        eng->choice (P_rsVoices, "Voices", 100);
        eng->choice (P_rsWave, "Waveform", 120);
        eng->knob (P_rsOct, "Octave", 60);
        eng->knob (P_rsSemi, "Semi", 60);
        eng->choice (P_rsRetrig, "Retrigger", 100)->box.setTooltip ("Free: the voices run on from the last note (analogue). Reset: they start together (tight). Random: a new start each note.");
        auto* det = sec (page, "Detune", c);
        det->addInfo ("Cents: as usual (the beating speeds up with the note). Hz: the same beating on every note. Beats: the beating is a "
                      "note value at the song's tempo, worked out for every note, so the Reese wobble lands on the beat at any pitch "
                      "(and follows tempo changes). Voices 3 and 4 beat x3 or x5 as fast (still locked) or at the golden ratio (free).");
        det->choice (P_rsDetMode, "Mode", 180);
        auto* kCents = det->knob (P_rsCents, "Cents", 62);
        auto* kHz = det->knob (P_rsHz, "Hz", 62);
        auto* cBeat = det->choice (P_rsBeat, "Beat", 110);
        det->choice (P_rsRatio, "Voices 3-4", 120);
        auto* info = page->own (new juce::Label());
        info->setColour (juce::Label::textColourId, col::muted);
        info->setFont (juce::Font (juce::FontOptions (11.5f)));
        info->setJustificationType (juce::Justification::topLeft);
        auto* mv = sec (page, "Phase & Width", c);
        mv->addInfo ("Phase Twist pushes the voices' phases back and forth: slow is the classic swirl, fast (10-40 Hz) a growl. Width spreads "
                     "the voices; Rotation moves them round the stereo field. Mono Below keeps everything under that frequency in the "
                     "middle (off at 0).");
        mv->knob (P_rsTwist, "Twist", 60);
        mv->knob (P_rsTwistRate, "Twist Rate", 66);
        mv->knob (P_rsWidth, "Width", 60);
        mv->knob (P_rsRotate, "Rotation", 62);
        mv->knob (P_rsRotRate, "Rot. Rate", 62);
        mv->knob (P_rsMonoBelow, "Mono Below", 70);
        auto* nt = sec (page, "Notch", c);
        nt->addInfo ("A moving notch inside the oscillator: the hollow, phasey Reese movement without using the filters. Synced: it moves with "
                     "the beating; off, at its own Rate. Move: how far it sweeps (up to two octaves each way).");
        nt->knob (P_rsNotch, "Depth", 60);
        nt->knob (P_rsNotchFreq, "Freq", 62);
        nt->knob (P_rsNotchMove, "Move", 60);
        nt->add (new ParamToggle (proc, P_rsNotchSync, "Synced", c), 86, 40);
        auto* kNr = nt->knob (P_rsNotchRate, "Rate", 60);
        auto* ch = sec (page, "Character", c);
        ch->addInfo ("Sub: a sine an octave down, locked to voice 1. Drive: a soft saturator inside the oscillator (before the filters); Tone: "
                     "darker to the left.");
        ch->knob (P_rsSub, "Sub", 60);
        ch->knob (P_rsDrive, "Drive", 60);
        ch->knob (P_rsTone, "Tone", 60);
        auto* ticker = page->own (new StoreTicker());
        ticker->tick = [this, kCents, kHz, cBeat, kNr, info]
        {
            auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
            const int mode = juce::roundToInt (plain (P_rsDetMode));
            kCents->setAlpha (mode == 0 ? 1.0f : 0.4f); kHz->setAlpha (mode == 1 ? 1.0f : 0.4f); cBeat->setAlpha (mode == 2 ? 1.0f : 0.4f);
            kNr->setAlpha (plain (P_rsNotchSync) > 0.5f ? 0.4f : 1.0f);
            // what the detune means now: the beating, and the voices at A1
            const double tempo = proc.getHostTempoShown() > 0.0 ? proc.getHostTempoShown() : (double) plain (P_seqTempo);
            tg::ReeseSettings rs;
            rs.voices = juce::roundToInt (plain (P_rsVoices)); rs.mode = mode; rs.beat = juce::roundToInt (plain (P_rsBeat)); rs.ratio = juce::roundToInt (plain (P_rsRatio));
            rs.cents = plain (P_rsCents); rs.hz = plain (P_rsHz);
            tg::ReeseOsc o; o.prepare (48000.0);
            const double a1 = 55.0 * std::pow (2.0, std::round (plain (P_rsOct)) + std::round (plain (P_rsSemi)) / 12.0);
            o.setFrequency (a1, tempo, rs);
            juce::String t;
            t << "Beating: " << juce::String (o.beatRate(), 2) << " Hz";
            if (mode == 2) t << " (one per " << tg::kRsBeatLabels[juce::jlimit (0, 12, rs.beat)] << " at " << juce::String (tempo, 1) << " BPM)";
            t << "\nAt A1 the voices are ";
            for (int k = 0; k < o.voiceCount(); ++k) t << (k ? " / " : "") << juce::String (o.voiceFrequency (k), 2);
            t << " Hz";
            if (plain (P_rsLevel) <= 0.0f) t << "\nLevel is 0: the Reese is off.";
            info->setText (t, juce::dontSendNotification);
        };
        ticker->tick();
        page->onResize = [page, eng, det, info, mv, nt, ch]
        {
            const int g = 10, W = page->getWidth();
            eng->setBounds (g, g, 560, 130);
            det->setBounds (g + 570, g, W - 2 * g - 570, 130);
            info->setBounds (g + 590, g + 140, W - 2 * g - 600, 60);
            mv->setBounds (g, g + 140, 560, 130);
            nt->setBounds (g, g + 280, 560, 130);
            ch->setBounds (g + 570, g + 210, W - 2 * g - 570, 130);
        };
    }

    // ---------------------------------------------------------------- Oscillators > Reese Shape (v0.1.70)
    {
        auto* page = addPage ("Reese Shape");
        const auto c = juce::Colour (0xffff7a59);
        auto* fm = sec (page, "Formant  (Three band-pass filters at a vowel's formants: Vowel morphs A - E - I - O - U, Shift moves them all (+/-12 st), "
                              "Resonance sharpens them, Mix blends with the plain Reese. Movement: a synced LFO (Sine, Triangle, Saw or Random, "
                              "locked to the song from each note) or following the notch. Vowel is a Mod Matrix destination, so Break React, MSEGs "
                              "or a macro can make it talk. Before or after the drive. The vowels are approximations of a voice's formants.)", c);
        reesePresetsChip (fm);
        fm->add (new ParamToggle (proc, P_rsFmtOn, "On", c), 56, 40);
        fm->choice (P_rsFmtMove, "Movement", 130);
        fm->choice (P_rsFmtSync, "Rate", 90);
        fm->choice (P_rsFmtShape, "Shape", 96);
        fm->choice (P_rsFmtPos, "Position", 130);
        fm->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_rsFmtVowel, "Vowel" }, { P_rsFmtRes, "Resonance" }, { P_rsFmtMix, "Mix" },
                               { P_rsFmtShift, "Shift" }, { P_rsFmtDepth, "LFO Depth" } })
            fm->knob (idx, n, 66);
        auto* bl = sec (page, "Detune Bloom  (Each note's detune opens from nothing to the set amount over Bloom (seconds, or synced), Linear or "
                              "Exponential; Reverse starts at three times as wide and settles to it. 0 is off.)", c);
        bl->knob (P_rsBloom, "Bloom", 62);
        bl->choice (P_rsBloomSync, "Sync", 100);
        bl->choice (P_rsBloomCurve, "Curve", 110);
        bl->add (new ParamToggle (proc, P_rsBloomRev, "Reverse", c), 90, 40);
        auto* hv = sec (page, "Hoover & Pitch  (Waveform Hoover (PWM) on the Reese page: pulses made of two saws, their width swept by the PWM, "
                              "Mentasm-style. Octave Stack adds an octave down (and up) to every voice, with any waveform. Pitch Envelope: each "
                              "note starts above (Drop), below (Rise) or dips and comes back (Drop-Rise), by Amount over Time.)", c);
        hv->choice (P_rsStack, "Octave Stack", 150);
        hv->choice (P_rsPdMode, "Pitch Envelope", 120);
        hv->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_rsPw, "Pulse Width" }, { P_rsPwmRate, "PWM Rate" }, { P_rsPwmDepth, "PWM Depth" },
                               { P_rsPdAmt, "Env Amount" }, { P_rsPdTime, "Env Time" } })
            hv->knob (idx, n, 66);
        auto* dr = sec (page, "Split Drive & Shaper  (Split Drive: the lows and the highs driven apart around Split, the highs through their own "
                              "tone, so the sub stays clean while the top tears; off, the Reese page's Drive works as before. The Shaper "
                              "shapes each voice before they're added together - Fold, Tanh, Asym or Bitwise - so the beating itself distorts; "
                              "at High quality it's oversampled x2.)", c);
        dr->add (new ParamToggle (proc, P_rsSplitOn, "Split", c), 64, 40);
        for (auto [idx, n] : { std::pair<int, const char*> { P_rsSplitFreq, "Split" }, { P_rsLowDrive, "Low Drive" }, { P_rsHighDrive, "High Drive" }, { P_rsHighTone, "High Tone" } })
            dr->knob (idx, n, 60);
        dr->choice (P_rsShpType, "Shaper", 96);
        dr->knob (P_rsShpAmt, "Amount", 58);
        dr->knob (P_rsShpSym, "Symmetry", 60);
        auto* cpS = sec (page, "Comb / Phaser  (Comb: tuned to the note (Offset in semitones), with Feedback +/-; Phaser: 4, 8 or 12 stages swept "
                               "around the note. Depth moves it with its own LFO, synced (from each note) or free in Hz; Depth 0 holds it still.)", c);
        cpS->choice (P_rsCpMode, "Mode", 100);
        cpS->choice (P_rsCpStages, "Phaser", 110);
        cpS->choice (P_rsCpSync, "Rate Sync", 110);
        cpS->newRow();
        for (auto [idx, n] : { std::pair<int, const char*> { P_rsCpFb, "Feedback" }, { P_rsCpRate, "Rate (Hz)" }, { P_rsCpDepth, "Depth" },
                               { P_rsCpOffset, "Offset" }, { P_rsCpMix, "Mix" } })
            cpS->knob (idx, n, 66);
        auto* xs = sec (page, "Cross-FM, Sync & Drift  (Cross-FM: voice 1 moves the other voices' phase. Sync: each voice hard-synced to its own "
                              "pitch at Ratio, so the detune still beats while the sync sweeps (not band-limited: some aliasing at high ratios). "
                              "The envelope (Amp or Mod Env 1) adds to FM and sweeps the Ratio. Drift: slow random pitch (up to 12 cents) and "
                              "phase wander per voice, the same every time for the same notes; in Beats mode only the whole note drifts.)", c);
        xs->knob (P_rsFm, "Cross-FM", 60);
        xs->add (new ParamToggle (proc, P_rsSync, "Sync", c), 64, 40);
        xs->knob (P_rsSyncRatio, "Ratio", 56);
        xs->choice (P_rsFmEnvSrc, "Envelope", 124);
        xs->knob (P_rsFmEnvAmt, "Env Amt", 58);
        xs->knob (P_rsDrift, "Drift", 56);
        xs->knob (P_rsDriftRate, "Drift Rate", 62);
        page->onResize = [page, fm, bl, hv, dr, cpS, xs]
        {
            const int g = 10, W = page->getWidth(), lw = 560, rx = g + lw + 10, rw = W - rx - g;
            fm->setBounds (g, g, lw, 176);
            bl->setBounds (g, g + 186, lw, 130);
            hv->setBounds (g, g + 326, lw, 176);
            dr->setBounds (rx, g, rw, 130);
            cpS->setBounds (rx, g + 140, rw, 176);
            xs->setBounds (rx, g + 326, rw, 130);
        };
    }

    // ---------------------------------------------------------------- Oscillators > Core A, Core B
    for (int slot = 0; slot < 2; ++slot)
    {
        auto* page = addPage (slot == 0 ? "Core A" : "Core B");
        const auto c = slot == 0 ? juce::Colour (0xff59d0ff) : juce::Colour (0xffb98cff);
        const juce::String nm = slot == 0 ? "Core A" : "Core B";
        auto* top = sec (page, nm, c);
        top->addInfo ("A slot for a Core engine, heard alongside everything else (own Mixer strip, then the filters). Kernel FM: six "
                      "operators, each a waveform at a ratio of the note (or a fixed frequency) with its own envelope; any operator "
                      "can modulate any other and itself. Off costs nothing and changes nothing. Unison stacks the whole kernel; "
                      "Brightness scales every modulation amount; Envelope Speed makes all six envelopes faster or slower.");
        top->choice (tg::coreParam (slot, tg::CS_Engine), "Engine", 104);
        top->knob (tg::coreParam (slot, tg::CS_Level), "Level", 56);
        top->knob (tg::coreParam (slot, tg::CS_Oct), "Octave", 56);
        top->knob (tg::coreParam (slot, tg::CS_Semi), "Semi", 56);
        top->knob (tg::coreParam (slot, tg::CS_Fine), "Fine", 56);
        top->choice (tg::coreParam (slot, tg::CS_Uni), "Unison", 92);
        top->knob (tg::coreParam (slot, tg::CS_Detune), "Detune", 56);
        top->knob (tg::coreParam (slot, tg::CS_Spread), "Spread", 56);
        top->choice (tg::coreParam (slot, tg::CS_Retrig), "Retrigger", 90)->box.setTooltip ("Reset: every note starts the operators together (the usual for FM). Free: they run on from the last note. Random: a new start each note.");
        top->knob (tg::coreParam (slot, tg::CS_Bright), "Bright", 56);
        top->knob (tg::coreParam (slot, tg::CS_EnvSpd), "Env Speed", 60);

        auto* rt = sec (page, "Routing", c);
        rt->addInfo ("What the matrix is doing now: the operators you hear on the right, the ones modulating them to the left, a loop "
                     "for feedback (thicker lines: more). Algorithm writes a starting routing into the matrix (connections at 2, "
                     "the heard operators' outputs, and a level for any it uses that was at 0); then edit it freely. "
                     "Reset puts this slot back to its defaults (one sine).");
        auto* view = page->own (new CoreAlgoView (proc, slot, c));
        auto* algoBtn = page->own (new juce::TextButton ("Algorithm..."));
        auto* resetBtn = page->own (new juce::TextButton ("Reset"));
        algoBtn->onClick = [this, slot, algoBtn]
        {
            juce::PopupMenu m;
            for (int a = 0; a < tg::kNumCoreAlgorithms; ++a) m.addItem (a + 1, tg::kCoreAlgorithms[a].name);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (algoBtn), [this, slot] (int r)
            {
                if (r <= 0) return;
                proc.applyCoreAlgorithm (slot, r - 1);
                setStatus (juce::String ("Core ") + (slot ? "B" : "A") + " routing: " + tg::kCoreAlgorithms[r - 1].name);
            });
        };
        resetBtn->onClick = [this, slot] { proc.resetCoreSlot (slot); setStatus (juce::String ("Core ") + (slot ? "B" : "A") + " reset"); };

        auto* ops = sec (page, "Operators", c);
        auto* envMode = new BareChoice (proc, tg::coreEnvModeParam (slot));
        envMode->box.setTooltip ("Envelope. Amp + Operators: the Core goes through the amp envelope like the other oscillators, so a Release "
                                 "longer than the Amp Release is cut there. Operators only: the operators' own envelopes shape it, Release "
                                 "included, and the note rings on until they finish.");
        ops->addTitleChip (envMode, 170);
        auto* relHint = new juce::Label();
        relHint->setFont (juce::Font (juce::FontOptions (11.5f)));
        relHint->setColour (juce::Label::textColourId, juce::Colour (0xffffb347));
        ops->addTitleChip (relHint, 560);
        ops->addInfo ("One row per operator. Ratio: times the note (Fixed: a frequency of its own, for clangs and noise). Level: how loud it "
                      "is heard, and how hard it modulates. Vel: how much velocity changes the level. Key: louder up the keyboard (+) "
                      "or quieter (-). Out: how much of it you hear (0: it only modulates). The envelope is Delay, Attack, Hold, "
                      "Decay, Sustain, Release. The last six knobs send this operator into operators 1-6 (the modulation index, in "
                      "radians); the one on its own row is feedback.");
        juce::Array<Knob*> ratioKnobs, hzKnobs;
        juce::Array<juce::Component*> dimmable;
        const char* envCaps[6] = { "Delay", "Attack", "Hold", "Decay", "Sustain", "Release" };
        const int envW[6] = { 44, 44, 44, 44, 44, 44 };
        for (int k = 0; k < 6; ++k)
        {
            if (k > 0) ops->newRow();
            auto* lbl = new juce::Label ({}, juce::String (k + 1));
            lbl->setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
            lbl->setColour (juce::Label::textColourId, c);
            lbl->setJustificationType (juce::Justification::centred);
            ops->add (lbl, 18, 58);
            auto op = [slot, k] (int w) { return tg::coreOpParam (slot, k, w); };
            auto knob = [&] (int w, const juce::String& cap, int width) { auto* kn = new Knob (proc, op (w), cap, c); ops->add (kn, width, 60); return kn; };
            auto* wv = new Choice (proc, op (tg::CO_Wave), "Wave"); ops->add (wv, 84, 44);
            ratioKnobs.add (knob (tg::CO_Ratio, "Ratio", 48));
            ops->add (new ParamToggle (proc, op (tg::CO_Fixed), "Fixed", c), 58, 24);
            hzKnobs.add (knob (tg::CO_Hz, "Hz", 54));
            knob (tg::CO_Fine, "Fine", 56);
            knob (tg::CO_Level, "Level", 48);
            knob (tg::CO_Vel, "Vel", 44);
            knob (tg::CO_Key, "Key", 52);
            knob (tg::CO_Out, "Out", 44);
            for (int e = 0; e < 6; ++e) knob (tg::CO_Dly + e, envCaps[e], envW[e]);
            for (int j = 0; j < 6; ++j)
                knob (tg::CO_M1 + j, j == k ? juce::String ("FB") : juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + juce::String (j + 1), 40);
        }
        auto* ticker = page->own (new StoreTicker());
        ticker->tick = [this, slot, ratioKnobs, hzKnobs, ops, top, relHint]
        {
            auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
            const bool on = plain (tg::coreParam (slot, tg::CS_Engine)) > 0.5f;
            {
                // a heard operator's Release longer than the Amp Release is cut there (in Amp + Operators)
                const float ampR = plain (tg::P_ampR), spd = std::max (0.05f, plain (tg::coreParam (slot, tg::CS_EnvSpd)));
                float longest = 0.0f;
                for (int k = 0; k < 6; ++k)
                    if (plain (tg::coreOpParam (slot, k, tg::CO_Level)) > 0.0f && plain (tg::coreOpParam (slot, k, tg::CO_Out)) > 0.0f)
                        longest = std::max (longest, plain (tg::coreOpParam (slot, k, tg::CO_Rel)) / spd);
                const bool cut = on && plain (tg::coreEnvModeParam (slot)) < 0.5f && longest > ampR + 0.01f;
                const juce::String txt = cut ? "Release " + juce::String (longest, 2) + " s is cut at the Amp Release (" + juce::String (ampR, 2)
                                               + " s): choose Operators only to hear it" : juce::String();
                if (relHint->getText() != txt) relHint->setText (txt, juce::dontSendNotification);
            }
            ops->setAlpha (on ? 1.0f : 0.55f);
            for (int k = 0; k < 6; ++k)
            {
                const bool fixed = plain (tg::coreOpParam (slot, k, tg::CO_Fixed)) > 0.5f;
                ratioKnobs[k]->setAlpha (fixed ? 0.4f : 1.0f); hzKnobs[k]->setAlpha (fixed ? 1.0f : 0.4f);
            }
            juce::ignoreUnused (top);
        };
        ticker->tick();
        page->onResize = [page, top, rt, view, algoBtn, resetBtn, ops]
        {
            const int g = 10, W = page->getWidth();
            top->setBounds (g, g, 820, 126);
            rt->setBounds (g + 830, g, W - 2 * g - 830, 126);
            view->setBounds (rt->getX() + 8, rt->getY() + 30, rt->getWidth() - 96, 90);
            algoBtn->setBounds (rt->getRight() - 86, rt->getY() + 34, 78, 24);
            resetBtn->setBounds (rt->getRight() - 86, rt->getY() + 64, 78, 24);
            algoBtn->toFront (false); resetBtn->toFront (false); view->toFront (false);
            ops->setBounds (g, g + 134, W - 2 * g, 432);
        };
    }

    // ---------------------------------------------------------------- Breaks > Break React
    {
        auto* page = addPage ("Break React");
        const auto c = juce::Colour (0xffffb347);
        auto* top = sec (page, "Break React", c);
        top->addInfo ("Break-Reactive mode: the synth listens to the breaks (and, if you like, a MIDI note or the host's sidechain) and "
                      "reacts on their hits. Four rows below: the Main row (the original one, with its Listen choice) and a row each for kicks, "
                      "snares and hats, each with its own actions. Off, it changes nothing. A slice counts as a kick, snare or hat from the "
                      "Break Chopper's analysis (Detection: Classic, or Band energy: low / mid / top in the first 20 ms), or as you set it per "
                      "slice on the Break Chopper page. Slice Keys: Auto hands the slice keys back to the synth while the Break Sequencer plays. "
                      "Presets set only these controls (one undo step).");
        top->addTitleChip (new ModSourceChip (MS_BreakHit, "Hit"), 34);
        top->addTitleChip (new ModSourceChip (MS_BreakHitType, "Type"), 40);
        top->addTitleChip (new ModSourceChip (MS_KickHit, "Kick"), 38);
        top->addTitleChip (new ModSourceChip (MS_SnareHit, "Snare"), 44);
        top->addTitleChip (new ModSourceChip (MS_HatHit, "Hat"), 34);
        top->add (new ParamToggle (proc, P_rxOn, "On", c), 56, 40);
        auto* fromChoice = top->choice (P_rxBreak, "From", 104);
        top->choice (P_rxDetect, "Detection", 120);
        top->choice (P_bcSliceKeys, "Slice Keys", 92);
        auto* presetBtn = new juce::TextButton ("Presets...");
        top->add (presetBtn, 96, 28);
        presetBtn->onClick = [this, presetBtn]
        {
            juce::PopupMenu m;
            for (int k = 1; k < MegaSynthProcessor::kNumReactPresets; ++k) m.addItem (k + 1, MegaSynthProcessor::reactPresetName (k));
            m.addSeparator();
            m.addItem (1, "Off");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn), [this] (int r)
            {
                if (r <= 0) return;
                proc.applyReactPreset (r - 1);
                setStatus (juce::String ("Break React: ") + MegaSynthProcessor::reactPresetName (r - 1));
            });
        };
        auto* trg = sec (page, "Triggers", c);
        trg->addInfo ("Where hits come from. Breaks: every slice the Break Sequencer, the slice keys or a fill plays (Sequencer: the main "
                      "Break Sequencer's hits, the layer's, or both). Note: a key of your own "
                      "(it isn't played) - put it in a MIDI clip to fire the reactions anywhere; it's a hit for every row that's on. Sidechain: "
                      "drum hits found in the host's sidechain input (a rough guess at kick / snare / hat from its low and high end).");
        trg->add (new ParamToggle (proc, P_rxFromBreaks, "Breaks", c), 80, 40);
        trg->choice (P_rxFrom, "Sequencer", 130)->box.setTooltip ("Which Break Sequencer's hits count: the main one, the layer, or both (slice keys and fills always count)");
        trg->add (new ParamToggle (proc, P_rxFromNote, "Note", c), 70, 40);
        auto* kNote = trg->knob (P_rxNote, "Key", 64);
        trg->add (new ParamToggle (proc, P_rxFromSc, "Sidechain", c), 96, 40);
        auto* kSc = trg->knob (P_rxScSens, "SC Sens", 64);

        auto* rows = sec (page, "Rows", c);
        rows->addInfo ("One row per kind of hit, each with its own actions. Reset: the ticked engines start their cycle again on the hit (faded "
                       "over 1.5 ms). Pitch: a jump that falls back over the Decay. Drive: a burst of saturation. Duck: a dip. Notch: the "
                       "Reese's notch leaps up. Retrigger: held notes start their amp and / or filter envelope again on the hit (from where "
                       "they are, so no click). Filter: Filter 1's cutoff jumps (octaves); Res: its resonance. Vel: how much a quieter hit "
                       "(a ghost note) reacts less. Beats / Every / Chance: which hits count - on the beat, the backbeat or the off-beats; "
                       "every 2nd, 3rd or 4th (counted from each bar); a chance per hit, seeded (Seed) so each playthrough is the same.");
        juce::Array<juce::Component*> rowParts[4];
        const char* rowNames[4] = { "Main", "Kick", "Snare", "Hat" };
        for (int r = 0; r < 4; ++r)
        {
            if (r > 0) rows->newRow();
            const auto rc = r == 0 ? c : rxTypeColour (r - 1);
            auto* lbl = new juce::Label ({}, rowNames[r]);
            lbl->setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
            lbl->setColour (juce::Label::textColourId, rc);
            rows->add (lbl, 50, 58);
            auto* on = new ParamToggle (proc, rxRowParam (r, RXF_On), "On", rc);
            rows->add (on, 50, 24);
            juce::Array<juce::Component*>& parts = rowParts[r];
            if (r == 0) { auto* l = new Choice (proc, P_rxListen, "Listen"); rows->add (l, 96, 44); parts.add (l); }
            else { auto* sp = new juce::Label ({}, juce::String (r == 1 ? "kicks" : (r == 2 ? "snares" : "hats"))); sp->setColour (juce::Label::textColourId, col::muted); sp->setJustificationType (juce::Justification::centred); rows->add (sp, 96, 44); parts.add (sp); }
            auto knob = [&] (int f, const juce::String& cap, int w) { auto* kn = new Knob (proc, rxRowParam (r, f), cap, rc); rows->add (kn, w, 60); parts.add (kn); };
            auto pick = [&] (int f, const juce::String& cap, int w) { auto* ch = new Choice (proc, rxRowParam (r, f), cap); rows->add (ch, w, 44); parts.add (ch); };
            knob (RXF_Sens, "Sens", 50);
            { auto* t = new ParamToggle (proc, rxRowParam (r, RXF_Reset), "Reset", rc); rows->add (t, 64, 24); parts.add (t); }
            knob (RXF_Pitch, "Pitch", 50); knob (RXF_Drive, "Drive", 50); knob (RXF_Duck, "Duck", 50); knob (RXF_Notch, "Notch", 50);
            pick (RXF_Retrig, "Retrigger", 102);
            knob (RXF_Cut, "Filter", 50); knob (RXF_Res, "Res", 44); knob (RXF_Vel, "Vel", 44);
            pick (RXF_Beats, "Beats", 122); pick (RXF_Every, "Every", 98);
            knob (RXF_Chance, "Chance", 52);
        }

        auto* shp = sec (page, "Shape & Timing", c);
        shp->addInfo ("The hits' shape: Attack, then down to 1% over the Decay (the Hit sources follow it too). Offset moves the reaction "
                      "later (or earlier: then the breaks play that much later, as the synth can't know a hit is coming). Humanise: a random "
                      "few ms either way on each hit. Seed: the rows' Chance.");
        shp->knob (P_rxAttack, "Attack", 54);
        shp->knob (P_rxDecay, "Decay", 54);
        shp->knob (P_rxOffset, "Offset", 54);
        shp->knob (P_rxHuman, "Humanise", 62);
        shp->knob (P_rxSeed, "Seed", 52);
        auto* eng = sec (page, "Engines", c);
        eng->addInfo ("Which engines react. Osc 1-3, Sub and Reese take every action (the Notch is the Reese's own). WT 1 / WT 2 start again "
                      "from their position on a Reset hit, plus Jump (a share of the loop, so each hit starts somewhere else). Core A / B: "
                      "their operators' phases and envelopes start again. SuperSaw and Complex: their phases start again. Granular: a burst "
                      "of grains (Burst) with the hit envelope.");
        eng->add (new ParamToggle (proc, P_rxOsc1, "Osc 1", c), 62, 24);
        eng->add (new ParamToggle (proc, P_rxOsc2, "Osc 2", c), 62, 24);
        eng->add (new ParamToggle (proc, P_rxOsc3, "Osc 3", c), 62, 24);
        eng->add (new ParamToggle (proc, P_rxSub, "Sub", c), 52, 24);
        eng->add (new ParamToggle (proc, P_rxReese, "Reese", c), 66, 24);
        eng->add (new ParamToggle (proc, P_rxCoreA, "Core A", c), 70, 24);
        eng->add (new ParamToggle (proc, P_rxCoreB, "Core B", c), 70, 24);
        eng->newRow();
        eng->add (new ParamToggle (proc, P_rxWt1, "WT 1", c), 58, 24);
        eng->add (new ParamToggle (proc, P_rxWt2, "WT 2", c), 58, 24);
        eng->add (new ParamToggle (proc, P_rxSaw, "SuperSaw", c), 84, 24);
        eng->add (new ParamToggle (proc, P_rxComplex, "Complex", c), 78, 24);
        eng->add (new ParamToggle (proc, P_rxGran, "Granular", c), 80, 24);
        eng->knob (P_rxWtJump, "Jump", 50);
        eng->knob (P_rxGranBurst, "Burst", 50);
        auto* hits = sec (page, "Hits", c);
        hits->addInfo ("K, S and H light on each kick, snare and hat it reacts to (? for a trigger note or an unsorted sidechain hit). The lane is "
                       "one bar: the last 32 hits, where they fell in it, a row per type.");
        auto* lights = new ReactLights (proc);
        hits->add (lights, 290, 30);
        auto* info = page->own (new juce::Label());
        info->setColour (juce::Label::textColourId, col::muted);
        info->setFont (juce::Font (juce::FontOptions (12.0f)));
        info->setJustificationType (juce::Justification::topLeft);
        auto* ticker = page->own (new StoreTicker());
        ticker->tick = [this, kNote, kSc, info, lights, rowParts, fromChoice]
        {
            {
                // (v0.1.71) From in its break's colour (Any: the active break's)
                auto pl = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
                const int fb = juce::roundToInt (pl (P_rxBreak));
                const auto c = breakColour (fb > 0 ? fb - 1 : juce::jlimit (0, 3, juce::roundToInt (pl (P_bcActive)) - 1));
                if (fromChoice->box.findColour (juce::ComboBox::outlineColourId) != c) { fromChoice->box.setColour (juce::ComboBox::outlineColourId, c); fromChoice->box.setColour (juce::ComboBox::arrowColourId, c); }
            }
            auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
            kNote->setAlpha (plain (P_rxFromNote) > 0.5f ? 1.0f : 0.4f);
            kSc->setAlpha (plain (P_rxFromSc) > 0.5f ? 1.0f : 0.4f);
            for (int r = 0; r < 4; ++r)
            {
                const float a = plain (rxRowParam (r, RXF_On)) > 0.5f ? 1.0f : 0.45f;
                for (auto* pc : rowParts[r]) pc->setAlpha (a);
            }
            lights->tick();
            juce::String t;
            if (plain (P_rxOn) < 0.5f) t << "Off: switch it on to react.";
            else
            {
                const auto& en = proc.getEngine();
                t << "Hits: " << en.rxHitsShown.load() << "   (kick row " << en.rxRowHitsShown[1].load() << ", snare " << en.rxRowHitsShown[2].load()
                  << ", hat " << en.rxRowHitsShown[3].load() << ")";
                if (plain (P_rxFromBreaks) > 0.5f && plain (P_rxOffset) - plain (P_rxHuman) < 0.0f)
                    t << "\nThe breaks play " << juce::String (plain (P_rxHuman) - plain (P_rxOffset), 1) << " ms late, so the synth can react first.";
            }
            const int sk = juce::roundToInt (plain (P_bcSliceKeys));
            t << "\nSlice keys: " << (plain (P_bcOn) < 0.5f ? juce::String ("off (Break Chopper off)")
                                     : proc.sliceKeysTakeNotes() ? (sk == 2 ? juce::String ("on (Auto, sequencer stopped)") : juce::String ("on"))
                                     : (sk == 1 ? juce::String ("off: those keys play the synth") : juce::String ("off (Auto, sequencer running): those keys play the synth")));
            if (info->getText() != t) info->setText (t, juce::dontSendNotification);
        };
        ticker->tick();
        page->onResize = [page, top, trg, rows, shp, eng, hits, info]
        {
            const int g = 10, W = page->getWidth();
            top->setBounds (g, g, 600, 100);
            trg->setBounds (g + 610, g, W - 2 * g - 610, 100);
            rows->setBounds (g, g + 108, W - 2 * g, 296);
            shp->setBounds (g, g + 412, 320, 160);
            eng->setBounds (g + 330, g + 412, 530, 160);
            hits->setBounds (g + 870, g + 412, W - 2 * g - 870, 160);
            info->setBounds (g + 880, g + 482, W - 2 * g - 890, 66);
        };
    }

    // ---------------------------------------------------------------- Breaks > Break Sequencer
    // Two sequencers on one page: the main one and the layer (e.g. hats or a shaker from another break). Main / Layer picks which
    // one the controls, the grid and the Step section edit; both keep playing whichever is shown.
    {
        auto* page = addPage ("Break Sequencer");
        breakSeqPage = page;
        auto* ticker = page->own (new StoreTicker());
        auto* grid = page->own (new BreakStepGrid (proc));
        auto editLayer = std::make_shared<bool> (false);
        auto clip = std::make_shared<std::vector<tg::BreakStep>>();
        auto plainOf = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
        // the pattern a sequencer edits: the main's selected one; the layer bank's own selected one (never moved by the main)
        auto patFor = [plainOf] (bool layer) { return juce::jlimit (0, 7, juce::roundToInt (plainOf (layer ? P_bsLPattern : P_bsPattern))); };
        auto activeBreak = [plainOf] { return juce::jlimit (0, tg::BreakData::kBreaks - 1, juce::roundToInt (plainOf (P_bcActive)) - 1); };
        juce::Array<StoreKnob*> knobs; juce::Array<StoreToggle*> toggles; juce::Array<StoreChoice*> storeChoices;
        juce::Array<Choice*> withChoices;   // (Alternate's "With": tinted in its break's colour)
        juce::Array<Segmented*> patButtons;
        // the Snare Roll's settings (shown over the grid when its button is pressed)
        auto rollLayer = std::make_shared<bool> (false);
        auto* rollSec = sec (page, "Snare Roll  (The early-90s jungle roll: one clean snare retriggered all the way through, speeding up (16ths, "
                                   "32nds, 64ths, or a buzz at the end), building from ghost level to full, each hit cut short so the fast part stays "
                                   "tight, a small pitch rise, landing on the next downbeat. Length: how long, always ending with the pattern. Snare: "
                                   "Auto picks the cleanest snare slice (the most snare and the least kick, from the analysis). Gap: the last 16th "
                                   "silent, or a reverse snare into the downbeat. Variation: small seeded changes to the levels and the timing. Roll it "
                                   "writes the roll into the edited pattern (a new one each press; Undo takes it back). The steps it writes use the "
                                   "step's Roll, Level, Roll Ramp, Roll Tight and Roll Pitch, so you can edit any of it after.)", juce::Colour (0xffff6b6b));
        juce::Array<juce::TextButton*> editButtons;
        struct ChainEd { juce::TextEditor* ed; bool layer; };
        std::vector<ChainEd> chains;
        Section* tops[2] {};
        for (int li = 0; li < 2; ++li)
        {
            const bool layer = li == 1;
            const juce::Colour c = layer ? col::complex : col::seq;
            auto* top = sec (page, layer
                ? juce::String ("Layer Sequencer  (A second Break Sequencer with everything the main one has, for a layer such as hats or a shaker "
                                "from another break. It has one bank of eight layer patterns, A-H, for the whole project. Paired: each main pattern "
                                "plays the layer pattern it's paired with (the main row's Layer setting, e.g. main A with Layer B, main B with "
                                "Layer C; A-H with A-H unless you change them), switching with it, on the same steps; these A-H buttons pick the "
                                "layer pattern you edit, and Edit pair makes them jump to the pair when the main pattern changes. Own: its own "
                                "pattern, Switch, Pattern Keys and chain. Right-click an A-H button to learn a pad for it. Its hits choke only each other "
                                "(their own choke groups), and it has its own Mixer strip (Break Layer: level, pan, mute, solo, swing). Each of its "
                                "patterns can own its break too. Break React can listen to it (Sequencer: Layer or Both). Layer Seq Step and Layer "
                                "Seq Pattern are Mod Matrix sources.)")
                : juce::String ("Main Sequencer  (Eight patterns, A-H, of 16 or 32 sixteenth steps, locked to the song while it plays (and to the "
                                "synth's tempo when it doesn't). Pick the pattern to edit and play here, or from 8 keys (Pattern Keys); a new pattern "
                                "starts on the next step, beat or bar (Switch). Chain plays patterns in order, e.g. AABA. Each pattern owns its break "
                                "(Pattern Break: Break 1-4; Active follows the Active Break, as before): Original and the tools use it, and when the "
                                "pattern starts it becomes the Active Break. Layer: the layer pattern it plays with (Paired). Right-click an A-H "
                                "button to learn a pad (a note or CC) for it: a press selects it as a click does. Each step plays a slice, with its "
                                "own velocity, accent, pitch, reverse, roll, probability or tie. Step, pattern and slice are Mod Matrix sources.)"),
                c);
            tops[li] = top;
            // title row: which sequencer to edit, and where the tools draw from
            for (int e = 0; e < 2; ++e)
            {
                auto* b = new juce::TextButton (e == 0 ? "Main" : "Layer");
                b->setTooltip ("Edit the main sequencer or the layer sequencer (both play whichever you're editing)");
                b->setColour (juce::TextButton::buttonOnColourId, e == 0 ? col::seq : col::complex);
                b->onClick = [this, e] { if (breakEditLayerFn) breakEditLayerFn (e == 1); };
                editButtons.add (b);
                top->addTitleChip (b, 54);
            }
            auto* tdLabel = new juce::Label ({}, "Tools draw from");
            tdLabel->setColour (juce::Label::textColourId, col::muted);
            tdLabel->setFont (juce::Font (juce::FontOptions (11.5f)));
            top->addTitleChip (tdLabel, 92);
            auto* td = new BareChoice (proc, P_bsToolSource);
            td->box.setTooltip ("This pattern's break: the tools use the edited pattern's break (or the Active Break when it's set to Active). All four breaks: they mix in hits from every loaded break");
            top->addTitleChip (td, 170);
            if (layer)
            {
                auto* ep = new ParamToggle (proc, P_bsLEditPair, "Edit pair", c);
                ep->setTooltip ("Edit pair: when the main pattern changes, the layer's A-H buttons jump to the layer pattern it's paired with");
                top->addTitleChip (ep, 84);
            }
            if (layer) top->addTitleChip (new ModSourceChip (MS_LsStep, "Step"), 40);
            if (layer) top->addTitleChip (new ModSourceChip (MS_LsPattern, "Pattern"), 56);
            // row 1
            top->add (new ParamToggle (proc, layer ? P_bsLOn : P_bsOn, "Run", c), 70, 40);
            auto* patSeg = new Segmented (proc, layer ? P_bsLPattern : P_bsPattern, { "A", "B", "C", "D", "E", "F", "G", "H" }, c);
            const int padBase = layer ? 8 : 0;
            patSeg->footText = [this, padBase] (int i) { return proc.patPadText (padBase + i); };
            if (! layer)
                patSeg->cornerText = [this, plainOf] (int i)
                {
                    if (plainOf (P_bsLFollow) > 0.5f) return juce::String();
                    const int pr = proc.breakStore.get().lpair[juce::jlimit (0, 7, i)];
                    return pr > 0 ? juce::String::charToString ((juce::juce_wchar) ('A' + pr - 1)) : juce::String ("-");
                };
            patSeg->onRightClick = [this, padBase, layer] (int i)
            {
                const juce::String nm = juce::String (layer ? "layer " : "") + juce::String::charToString ((juce::juce_wchar) ('A' + i));
                juce::PopupMenu m;
                m.addItem (1, "Learn a pad for " + nm + " (then hit a pad or press a CC button)");
                m.addItem (2, "Clear the pad for " + nm, proc.patPadKind[padBase + i].load() != 0);
                m.addSeparator();
                m.addItem (3, juce::String ("Learn A-H in a row (eight pads, from ") + nm + ")");
                m.addItem (4, "Clear all " + juce::String (layer ? "layer " : "") + "A-H pads");
                m.showMenuAsync (juce::PopupMenu::Options(), [this, padBase, i, nm] (int r)
                {
                    if (r == 1) { proc.patPadLearnRun = false; proc.patPadLearn = padBase + i; setStatus ("Hit a pad (or press a CC button) for " + nm); }
                    if (r == 2) { proc.clearPatPads (padBase + i, padBase + i); setStatus ("Pad for " + nm + " cleared"); }
                    if (r == 3) { proc.patPadLearnRun = true; proc.patPadLearn = padBase + i; setStatus ("Hit the pads for " + nm + " onwards, one after another"); }
                    if (r == 4) { proc.clearPatPads (padBase, padBase + 7); setStatus ("Pattern pads cleared"); }
                });
            };
            patSeg->setTooltip ("Click to pick the pattern (it starts on the next step, beat or bar: Switch). Right-click: learn a pad (a note or "
                                "a CC button) for it; the learned pad shows under the letter" + juce::String (layer ? "" : "; the small letter is the layer pattern it's paired with"));
            patButtons.add (patSeg);
            top->add (patSeg, 240, 36);
            top->choice (layer ? P_bsLSteps : P_bsSteps, "Length", 90);
            top->choice (layer ? P_bsLSwitch : P_bsSwitch, "Switch", 100);
            top->choice (layer ? P_bsLKeys : P_bsKeys, "Pattern Keys", layer ? 130 : 150);
            if (layer) top->choice (P_bsLFollow, "Plays", 112)->box.setTooltip ("Paired: the layer pattern paired with the playing main pattern (the main row's Layer setting), in step with it. Own: its own pattern, switching and chain");
            top->knob (layer ? P_bsLSwing : P_bsSwing, "Swing", 62);
            if (layer) top->knob (P_bsLLevel, "Level", 62);
            top->knob (P_bsAmount, "Amount", 62);
            auto* patBreak = new StoreKnob ("Pattern Break", 0, 4, 1, c,
                                            [this, patFor, layer] { const auto d = proc.breakStore.get(); const int p = patFor (layer); return (float) (layer ? d.lpatBreak[p] : d.patBreak[p]); },
                                            [this, patFor, layer] (float v) { const int p = patFor (layer); proc.breakStore.edit ([p, v, layer] (tg::BreakData& d) { (layer ? d.lpatBreak : d.patBreak)[p] = juce::roundToInt (v); }); },
                                            [] (double v) { return v < 0.5 ? juce::String ("Active") : "Break " + juce::String (juce::roundToInt (v)); });
            patBreak->setTooltip (layer ? "The break this layer pattern plays (Active: whichever is active). Original and the tools use it."
                                        : "The break this pattern plays: Original and the tools use it, and it becomes the Active Break when the pattern starts (Active: no change, as before)");
            patBreak->colourFor = [patBreak, activeBreak] { const int v = juce::roundToInt (patBreak->slider.getValue()); return breakColour (v > 0 ? v - 1 : activeBreak()); };
            patBreak->isFollowing = [patBreak] { return patBreak->slider.getValue() < 0.5; };
            top->add (patBreak, 84, 84);
            knobs.add (patBreak);
            top->knob (P_bsVarAmount, "Variation", 66);
            if (! layer)
            {
                // the layer pattern this main pattern plays with (Paired)
                juce::StringArray items { "Off" }; for (int q = 0; q < 8; ++q) items.add ("Layer " + juce::String::charToString ((juce::juce_wchar) ('A' + q)));
                auto* pair = new StoreChoice ("Layer", items,
                                              [this, patFor] { return proc.breakStore.get().lpair[patFor (false)]; },
                                              [this, patFor] (int v) { const int p = patFor (false); proc.breakStore.edit ([p, v] (tg::BreakData& d) { d.lpair[p] = juce::jlimit (0, 8, v); }); });
                pair->setTooltip ("The layer pattern this main pattern plays with (when the layer plays Paired): e.g. main A with Layer B, main B with Layer C. Off: no layer for this pattern");
                top->add (pair, 100, 44);
                storeChoices.add (pair);
            }
            top->newRow();
            // row 2: chain, the pattern tools, Alternate
            top->add (new ParamToggle (proc, layer ? P_bsLChainOn : P_bsChainOn, "Chain", c), 76, 40);
            auto* chainEd = new juce::TextEditor();
            chainEd->setTooltip ("Patterns to play in order, e.g. AABA (up to 16)");
            chainEd->setInputRestrictions (16, "ABCDEFGHabcdefgh");
            chainEd->onTextChange = [this, chainEd, layer] { const auto t = chainEd->getText(); proc.breakStore.edit ([t, layer] (tg::BreakData& d) { tg::BreakStore::setChain (layer ? d.lchain : d.chain, t); }); };
            top->add (chainEd, 130, 26);
            chains.push_back ({ chainEd, layer });
            auto run = [this, patFor, layer] (int tool, const juce::String& name)
            {
                const int b = proc.runBreakTool (tool, layer, patFor (layer));
                setStatus (name + " on " + (layer ? "layer " : "") + "pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + patFor (layer)))
                           + ", from Break " + juce::String (b + 1) + " (Undo takes it back)");
            };
            struct T { const char* name; int tool; const char* tip; };
            const T oldTools[] = {
                { "Original", MegaSynthProcessor::BtOriginal, "The pattern's break as it was: its slices in order, each on the step where it falls (its own break's timing)" },
                { "Reverse", MegaSynthProcessor::BtReverse, "The pattern back to front" },
                { "Shuffle", MegaSynthProcessor::BtShuffle, "The same slices in a new order (each press a new shuffle)" },
                { "Jungle", MegaSynthProcessor::BtJungle, "A jungle edit of the original: snare ghosts and swaps, a kick double, a reversed hit, a cut-up and a snare roll (each press a new one)" },
                { "Franken", MegaSynthProcessor::BtFranken, "Frankenbreak: the pattern's break's groove, with its kicks, snares and hats each taken from a different loaded break (Amount: how many hits are swapped)" },
                { "Swap", MegaSynthProcessor::BtSwap, "Swap Break: the same pattern, played with the pattern's break's slices at the same places" } };
            for (auto& t : oldTools)
            {
                auto* b = new juce::TextButton (t.name); b->setTooltip (t.tip);
                const int tool = t.tool; const juce::String nm (t.name);
                b->onClick = [run, tool, nm] { run (tool, nm); };
                top->add (b, 64, 26);
            }
            auto* copyB = new juce::TextButton ("Copy"); copyB->setTooltip ("Copy this pattern (paste it into any pattern of either sequencer)");
            copyB->onClick = [this, clip, patFor, layer] { const auto d = proc.breakStore.get(); const auto* src = layer ? d.lstep[patFor (layer)] : d.step[patFor (layer)]; clip->assign (src, src + tg::BreakData::kSteps); setStatus ("Pattern copied"); };
            auto* pasteB = new juce::TextButton ("Paste"); pasteB->setTooltip ("Paste the copied pattern here");
            pasteB->onClick = [this, clip, patFor, layer] { if (clip->empty()) return; const int p = patFor (layer); proc.breakStore.edit ([&] (tg::BreakData& d) { std::copy (clip->begin(), clip->end(), layer ? d.lstep[p] : d.step[p]); }); };
            auto* clearB = new juce::TextButton ("Clear"); clearB->setTooltip ("Clear this pattern");
            clearB->onClick = [run] { run (MegaSynthProcessor::BtClear, "Clear"); };
            for (auto* b : { copyB, pasteB, clearB }) top->add (b, 64, 26);
            top->choice (layer ? P_bsLAlternate : P_bsAlternate, "Alternate", 150);
            withChoices.add (top->choice (layer ? P_bsLAltBreak : P_bsAltBreak, "With", 96));
            top->newRow();
            // row 3: the Stage 4 tools: each press applies straight away (a new seed each time) and Undo takes it back
            static const char* tips[] = {
                "Rollers: quiet 16th ghost snares around the backbeat (Amount: how many)",
                "Two-Step: kick on 1 and the 'and' of 2, snares on 2 and 4, the rest from the pattern (Amount: how many of its other kicks go)",
                "Halftime: the snare on 3 only, kicks on 1 and the 'and' of 3, hats from the break (Amount: how many hats)",
                "Double-time: each half bar squeezed into a quarter and played twice (Amount: how many half bars)",
                "Snare Roll: an early-90s jungle snare roll into the end of the pattern (opens its settings: length, speed-up, crescendo, tightness, pitch, which snare, the gap)",
                "Stutter: a hit repeated 2-4 times as 1/16s or 1/32s, at random places (Amount: how many)",
                "Reverse Swell: a reversed snare leading into the backbeats (Amount: how many)",
                "Cut-up: 1/8s swapped at random, keeping the kick on 1 and the snares on 2 and 4 (Amount: how many swaps)",
                "Ghosts: quiet hits on empty steps (Amount: how many)",
                "Hat Shuffle: only the hat-like hits change places (Amount: how many of them)",
                "Thin Out: hits removed or made less likely, for breakdowns; the kick on 1 and the backbeats stay (Amount: how many)",
                "Humanise: small changes to velocity, and hits played a little late within the swing (Amount: how much)",
                "Pitch Dive: the pitch falls over the last beat, down to an octave (Amount: how far)",
                "Variation: small random changes to the pattern as it is (the Variation knob: how many)" };
            for (int t = 0; t < tg::breaktools::kNumTools; ++t)
            {
                auto* b = new juce::TextButton (tg::breaktools::toolName (t));
                b->setTooltip (juce::String (tips[t]) + ". From the pattern's break; applied straight away, a new result each press; Undo takes it back.");
                const juce::String nm (tg::breaktools::toolName (t));
                if (t == tg::breaktools::ToolSnareBuild)
                {
                    b->setTooltip (tips[t]);
                    b->onClick = [rollSec, rollLayer, layer] { *rollLayer = layer; rollSec->setVisible (! rollSec->isVisible()); rollSec->toFront (false); };
                }
                else b->onClick = [run, t, nm] { run (t, nm); };
                top->add (b, 76, 26);
            }
        }
        // ---- the Snare Roll's settings
        rollSec->choice (P_bsRollLen, "Length", 92);
        rollSec->choice (P_bsRollCurve, "Rate", 190);
        rollSec->choice (P_bsRollGap, "Before the downbeat", 140);
        rollSec->knob (P_bsRollCresc, "Crescendo", 70);
        rollSec->knob (P_bsRollTight, "Tightness", 70);
        rollSec->knob (P_bsRollPitch, "Pitch Rise", 70);
        rollSec->knob (P_bsRollSnare, "Snare", 70);
        rollSec->knob (P_bsRollVar, "Variation", 70);
        {
            auto* rollIt = new juce::TextButton ("Roll it");
            rollIt->setTooltip ("Write the roll into the end of the edited pattern (a new one each press; Undo takes it back)");
            rollIt->setColour (juce::TextButton::buttonColourId, juce::Colour (0xffff6b6b).withAlpha (0.55f));
            rollIt->onClick = [this, rollLayer, patFor]
            {
                const bool l = *rollLayer;
                const int b = proc.runBreakTool (tg::breaktools::ToolSnareBuild, l, patFor (l));
                setStatus ("Snare Roll on " + juce::String (l ? "layer " : "") + "pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + patFor (l)))
                           + ", from Break " + juce::String (b + 1) + " (Undo takes it back)");
            };
            rollSec->add (rollIt, 90, 30);
            auto* closeB = new juce::TextButton ("Close");
            closeB->onClick = [rollSec] { rollSec->setVisible (false); };
            rollSec->add (closeB, 64, 30);
        }
        rollSec->setVisible (false);
        breakRollPanel = rollSec;
        // ---- the selected step (of the sequencer being edited)
        auto* stepSec = sec (page, "Step", col::seq);
        {
            auto* ke = new ParamToggle (proc, P_bsKeyEntry, "Key Entry", col::seq);
            ke->setTooltip ("Key Entry: a slice key (keyboard, pads or the on-screen keys) puts that slice on the selected step, switched on, "
                            "with the key's velocity (or the fixed one) and pinned to its break, and you still hear it. The keys play the edited "
                            "pattern's own break, so changing pattern changes the keys. Hold Shift (or the sustain pedal) to add it as the step's "
                            "Layer hit instead. Advance moves the selection on after each key.");
            stepSec->addTitleChip (ke, 100);
            auto* adv = new BareChoice (proc, P_bsAdvance);
            adv->box.setTooltip ("Advance: after each Key Entry, move the selection on by this many steps (wrapping round the pattern)");
            stepSec->addTitleChip (adv, 112);
            auto* fx = new ParamToggle (proc, P_bsKeyFixed, "Fixed vel", col::seq);
            fx->setTooltip ("Fixed velocity: every hit Key Entry puts in gets the velocity set beside it, however hard the key was played");
            stepSec->addTitleChip (fx, 84);
            auto* kv = new BareSlider (proc, P_bsKeyVel, col::seq);
            kv->s.setTooltip ("The fixed Key Entry velocity");
            stepSec->addTitleChip (kv, 64);
        }
        auto& store = proc.breakStore;
        auto stepGet = [&store, grid] (auto fn) { return [&store, grid, fn] { const auto d = store.get(); const int p = grid->editedPattern(), i = juce::jlimit (0, 31, grid->selected);
                                                                              return fn (grid->layer ? d.lstep[p][i] : d.step[p][i]); }; };
        auto stepSet = [&store, grid] (auto fn) { return [&store, grid, fn] (auto v) { const int p = grid->editedPattern(), i = juce::jlimit (0, 31, grid->selected), lay = grid->layer;
                                                                                       store.edit ([&] (tg::BreakData& d) { fn (lay ? d.lstep[p][i] : d.step[p][i], v); }); }; };
        toggles.add (new StoreToggle ("On", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.on; }), stepSet ([] (tg::BreakStep& x, bool v) { x.on = v; })));
        juce::Array<StoreKnob*> stepKnobs;
        stepKnobs.add (new StoreKnob ("Slice", 1, 32, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) (x.slice + 1); }), stepSet ([] (tg::BreakStep& x, float v) { x.slice = juce::roundToInt (v) - 1; x.on = true; }), [] (double v) { return juce::String (juce::roundToInt (v)); }));
        stepKnobs.add (new StoreKnob ("Velocity", 0, 1, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.vel; }), stepSet ([] (tg::BreakStep& x, float v) { x.vel = v; }), [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        toggles.add (new StoreToggle ("Accent", col::supersaw, stepGet ([] (const tg::BreakStep& x) { return x.accent; }), stepSet ([] (tg::BreakStep& x, bool v) { x.accent = v; })));
        stepKnobs.add (new StoreKnob ("Pitch", -24, 24, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.pitch; }), stepSet ([] (tg::BreakStep& x, float v) { x.pitch = v; }), [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }));
        toggles.add (new StoreToggle ("Reverse", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.reverse; }), stepSet ([] (tg::BreakStep& x, bool v) { x.reverse = v; })));
        // (Roll: x2, x3, x4, x6 or x8; 5 and 7 round to their neighbours)
        stepKnobs.add (new StoreKnob ("Roll", 1, 8, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.roll; }),
                                      stepSet ([] (tg::BreakStep& x, float v) { int r = juce::roundToInt (v); if (r == 5) r = x.roll > 5 ? 4 : 6; if (r == 7) r = x.roll > 7 ? 6 : 8; x.roll = r; }),
                                      [] (double v) { const int r = juce::roundToInt (v); return r < 2 ? juce::String ("off") : "x" + juce::String (r == 5 ? 6 : (r == 7 ? 8 : r)); }));
        stepKnobs.add (new StoreKnob ("Probability", 0, 1, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.prob; }), stepSet ([] (tg::BreakStep& x, float v) { x.prob = v; }), [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        toggles.add (new StoreToggle ("Tie", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.tie; }), stepSet ([] (tg::BreakStep& x, bool v) { x.tie = v; })));
        auto* brkKnob = new StoreKnob ("Break", 0, 4, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.brk; }), stepSet ([] (tg::BreakStep& x, float v) { x.brk = juce::roundToInt (v); }),
                                       [] (double v) { return v < 0.5 ? juce::String ("Pattern") : "Break " + juce::String (juce::roundToInt (v)); });
        brkKnob->setTooltip ("Which break this step plays: Pattern (the pattern's break, or the Active Break when that's Active) or always Break 1-4");
        brkKnob->colourFor = [this, brkKnob, grid, activeBreak]
        {
            const int v = juce::roundToInt (brkKnob->slider.getValue());
            if (v > 0) return breakColour (v - 1);
            const auto d = proc.breakStore.get(); const int p = grid->editedPattern(); const int pb = grid->layer ? d.lpatBreak[p] : d.patBreak[p];
            return breakColour (pb > 0 ? pb - 1 : activeBreak());
        };
        brkKnob->isFollowing = [brkKnob] { return brkKnob->slider.getValue() < 0.5; };
        auto* layerT = new StoreToggle ("Layer", col::seq, stepGet ([] (const tg::BreakStep& x) { return x.layer; }), stepSet ([] (tg::BreakStep& x, bool v) { x.layer = v; }));
        auto* lb = new StoreKnob ("Layer Break", 1, 4, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.layerBrk; }), stepSet ([] (tg::BreakStep& x, float v) { x.layerBrk = juce::roundToInt (v); x.layer = true; }),
                                  [] (double v) { return "Break " + juce::String (juce::roundToInt (v)); });
        lb->colourFor = [lb] { return breakColour (juce::jlimit (1, 4, juce::roundToInt (lb->slider.getValue())) - 1); };
        auto* ls = new StoreKnob ("Layer Slice", 1, 32, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) (x.layerSlice + 1); }), stepSet ([] (tg::BreakStep& x, float v) { x.layerSlice = juce::roundToInt (v) - 1; x.layer = true; }),
                                  [] (double v) { return juce::String (juce::roundToInt (v)); });
        auto* ll = new StoreKnob ("Layer Level", 0, 1.5, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.layerLevel; }), stepSet ([] (tg::BreakStep& x, float v) { x.layerLevel = v; }),
                                  [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        auto* stStretch = new StoreKnob ("Stretch", 0, 3, 1, col::seq, stepGet ([] (const tg::BreakStep& x) { return (float) x.stretch; }), stepSet ([] (tg::BreakStep& x, float v) { x.stretch = juce::roundToInt (v); }),
                                         [] (double v) { static const char* n[] = { "Default", "Repitch", "Akai", "Clean" }; return juce::String (n[juce::jlimit (0, 3, juce::roundToInt (v))]); });
        stStretch->setTooltip ("This hit's stretch: Default (the slice's, else the break's), Repitch, Akai Cyclic or Clean");
        auto* stCycle = new StoreKnob ("Cycle", 0, 200, 0.5, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.cycleMs; }), stepSet ([] (tg::BreakStep& x, float v) { x.cycleMs = v < 10.0f ? 0.0f : v; }),
                                       [] (double v) { return v < 10.0 ? juce::String ("Default") : juce::String (v, 1) + " ms"; });
        stCycle->setTooltip ("This hit's Akai Cyclic cycle length (Default: the slice's, else Cycle Length). Long cycles crunch a single hit.");
        auto* stLate = new StoreKnob ("Late", 0, 0.5, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.nudge; }), stepSet ([] (tg::BreakStep& x, float v) { x.nudge = v; }),
                                      [] (double v) { return v < 0.005 ? juce::String ("on time") : juce::String (juce::roundToInt (v * 100)) + "% step"; });
        stLate->setTooltip ("How late this hit plays, as a share of a step (Humanise sets it)");
        // (v0.1.71) the step's level and its roll's shape
        auto* stLevel = new StoreKnob ("Level", 0, 1, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.level; }), stepSet ([] (tg::BreakStep& x, float v) { x.level = v; }),
                                       [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        stLevel->setTooltip ("The step's level, on top of its velocity (the Snare Roll's crescendo uses it)");
        auto* stRamp = new StoreKnob ("Roll Ramp", 0, 1, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.rollRamp; }), stepSet ([] (tg::BreakStep& x, float v) { x.rollRamp = v; }),
                                      [] (double v) { return v < 0.005 ? juce::String ("flat") : juce::String (juce::roundToInt (v * 100)) + "%"; });
        stRamp->setTooltip ("Roll Ramp: the roll's repeats climb from this much quieter up to the step's level (a crescendo inside the step)");
        auto* stTight = new StoreKnob ("Roll Tight", 0, 1, 0.01, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.rollTight; }), stepSet ([] (tg::BreakStep& x, float v) { x.rollTight = v; }),
                                       [] (double v) { return v < 0.005 ? juce::String ("off") : juce::String (juce::roundToInt (v * 100)) + "%"; });
        stTight->setTooltip ("Roll Tight: each hit (and each repeat) is cut short; at 100% it lasts only as long as the gap to the next repeat");
        auto* stRp = new StoreKnob ("Roll Pitch", -12, 12, 0.1, col::seq, stepGet ([] (const tg::BreakStep& x) { return x.rollPitch; }), stepSet ([] (tg::BreakStep& x, float v) { x.rollPitch = v; }),
                                    [] (double v) { return std::abs (v) < 0.05 ? juce::String ("0 st") : (v > 0 ? "+" : "") + juce::String (v, 1) + " st"; });
        stRp->setTooltip ("Roll Pitch: the repeats rise (or fall) by this much across the step");
        stepSec->add (toggles[0], 56, 40);
        stepSec->add (brkKnob, 70, 84);
        for (auto* k : stepKnobs) stepSec->add (k, 70, 84);
        for (auto* k : { stStretch, stCycle, stLate }) stepSec->add (k, 70, 84);
        stepSec->newRow();
        for (int i = 1; i < toggles.size(); ++i) stepSec->add (toggles[i], 72, 40);
        stepSec->add (layerT, 64, 40);
        for (auto* k : { lb, ls, ll }) stepSec->add (k, 62, 84);
        for (auto* k : { stLevel, stRamp, stTight, stRp }) stepSec->add (k, 56, 84);
        for (auto* k : stepKnobs) knobs.add (k);
        knobs.add (brkKnob); knobs.add (lb); knobs.add (ls); knobs.add (ll); toggles.add (layerT);
        knobs.add (stStretch); knobs.add (stCycle); knobs.add (stLate);
        for (auto* k : { stLevel, stRamp, stTight, stRp }) knobs.add (k);
        auto* stepName = page->own (new juce::Label());
        stepName->setColour (juce::Label::textColourId, col::text);
        stepName->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        stepName->setJustificationType (juce::Justification::centredRight);
        grid->onSelect = [] (int) {};
        // ---- fills: one Fill Pattern for the whole project, called from any pattern
        const juce::Colour fc (0xffff9f43);
        auto* fillSec = sec (page, "Fill", fc);
        fillSec->addInfo ("Fill plays the Fill Pattern (one of A-H, the same whichever pattern is playing: e.g. write your fill in H and call it "
                          "from A-G), then the pattern comes back exactly where it would have been, as if it had kept running underneath (or, "
                          "with Restart, from its first step). Chains carry on underneath too. The fill plays the Fill Pattern's own break. "
                          "Start: on the next beat, the next bar, or timed to end with the pattern. Size: the whole Fill Pattern, or its last "
                          "1 beat, 2 beats or 1 bar. Mode: Trigger plays it once; Hold plays while you hold the pad. Calling it while the Fill "
                          "Pattern itself is playing does nothing. The Fill pad: press Learn, then hit a pad (or move a CC) on your controller; "
                          "that note isn't played, and the pad is saved with the project. Auto Fill plays the Fill Pattern every 4, 8 or 16 bars, "
                          "by Chance, ending with the phrase. The layer can keep going under a fill, or play the layer pattern paired with the "
                          "Fill Pattern. Fill Active is a Mod Matrix source.");
        auto* padLabel = new juce::Label();
        padLabel->setColour (juce::Label::textColourId, col::text);
        padLabel->setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        fillSec->addTitleChip (padLabel, 190);
        fillSec->add (new FillButton (proc), 56, 40);
        auto* learnB = new juce::TextButton ("Learn");
        learnB->setTooltip ("Learn the Fill pad: press, then hit a pad (a note) or move a button / CC on your controller");
        learnB->setClickingTogglesState (false);
        learnB->setColour (juce::TextButton::buttonOnColourId, fc);
        learnB->onClick = [this] { proc.fillPadLearn = ! proc.fillPadLearn.load(); };
        auto* clearPad = new juce::TextButton ("Clear");
        clearPad->setTooltip ("Forget the Fill pad");
        clearPad->onClick = [this] { proc.fillPadLearn = false; proc.fillPadKind = 0; proc.fillPadNum = -1; setStatus ("Fill pad cleared"); };
        fillSec->add (learnB, 60, 26);
        fillSec->add (clearPad, 52, 26);
        fillSec->choice (P_bsLFill, "Layer during fill", 168);
        fillSec->newRow();
        fillSec->choice (P_bsFillPat, "Fill Pattern", 104);
        fillSec->choice (P_bsFillSize, "Size", 108);
        fillSec->choice (P_bsFillStart, "Start", 112);
        fillSec->newRow();
        fillSec->choice (P_bsFillMode, "Mode", 80);
        fillSec->choice (P_bsFillReturn, "When it ends", 132);
        fillSec->choice (P_bsAutoFill, "Auto Fill", 96);
        fillSec->knob (P_bsAutoFillProb, "Chance", 52);
        // which sequencer is being edited
        breakEditLayerFn = [this, editLayer, grid, tops, editButtons] (bool l)
        {
            *editLayer = l; grid->layer = l; proc.keyEntryLayer = l;
            tops[0]->setVisible (! l); tops[1]->setVisible (l);
            for (int i = 0; i < editButtons.size(); ++i) editButtons[i]->setToggleState ((i % 2 == 1) == l, juce::dontSendNotification);
            grid->refresh(); grid->repaint();
        };
        breakEditLayerFn (false);
        auto lastMainPat = std::make_shared<int> (-1);
        ticker->tick = [this, grid, knobs, toggles, chains, stepName, padLabel, learnB, editLayer, patFor, storeChoices, withChoices, patButtons, lastMainPat, plainOf]
        {
            // Edit pair: the layer's A-H follow the main pattern's pair when the main pattern changes
            {
                const int mp = patFor (false);
                if (mp != *lastMainPat)
                {
                    if (*lastMainPat >= 0 && plainOf (P_bsLEditPair) > 0.5f && plainOf (P_bsLFollow) < 0.5f)
                    {
                        const int pr = proc.breakStore.get().lpair[mp];
                        if (pr > 0) { auto* prm = proc.param (P_bsLPattern); prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (pr - 1))); prm->endChangeGesture(); }
                    }
                    *lastMainPat = mp;
                }
            }
            breakKeyEntry (*grid);
            grid->refresh();
            for (auto* k : knobs) k->refresh();
            for (auto* sc : storeChoices) sc->refresh();
            for (int w = 0; w < withChoices.size(); ++w)
            {
                const auto c = breakColour (juce::jlimit (0, 3, juce::roundToInt (plainOf (w == 0 ? P_bsAltBreak : P_bsLAltBreak))));
                if (withChoices[w]->box.findColour (juce::ComboBox::outlineColourId) != c) { withChoices[w]->box.setColour (juce::ComboBox::outlineColourId, c); withChoices[w]->box.setColour (juce::ComboBox::arrowColourId, c); }
            }
            {
                // the pads under the letters, the pairs in the corners: repaint when they change
                juce::String key; for (int i = 0; i < MegaSynthProcessor::kPatPads; ++i) key << proc.patPadText (i) << ",";
                const auto d0 = proc.breakStore.get(); for (int p : d0.lpair) key << p; key << (int) plainOf (P_bsLFollow);
                if (key != padKeyShown) { padKeyShown = key; for (auto* b : patButtons) b->repaint(); }
            }
            for (auto* t : toggles) t->refresh();
            const auto d = proc.breakStore.get();
            for (auto& ce : chains)
            {
                const juce::String txt (ce.layer ? d.lchain : d.chain);
                if (! ce.ed->hasKeyboardFocus (true) && ce.ed->getText() != txt) ce.ed->setText (txt, false);
            }
            const bool l = *editLayer;
            const int p = patFor (l), pb = l ? d.lpatBreak[p] : d.patBreak[p];
            // the slice keys play the edited pattern's own break (the Active Break when it's set to Active)
            proc.keyBreakOverride = pb > 0 ? pb - 1 : -1;
            stepName->setText ("Step " + juce::String (grid->selected + 1) + " of " + (l ? "layer " : "") + "pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + p))
                               + (pb > 0 ? "  (Break " + juce::String (pb) + ")" : juce::String ("  (Active Break)")), juce::dontSendNotification);
            const bool learning = proc.fillPadLearn.load();
            const juce::String pt = learning ? juce::String ("Pad: hit a pad or move a CC...") : "Pad: " + proc.fillPadText();
            if (padLabel->getText() != pt) padLabel->setText (pt, juce::dontSendNotification);
            learnB->setToggleState (learning, juce::dontSendNotification);
        };
        ticker->tick();
        breakGridForTests = grid;
        page->onResize = [page, tops, grid, stepSec, stepName, fillSec, rollSec]
        {
            const int g = 10, W = page->getWidth();
            for (auto* t : tops) t->setBounds (g, g, W - 2 * g, 214);
            grid->setBounds (g + 4, g + 218, W - 2 * g - 8, 138);
            rollSec->setBounds (g, g + 218, W - 2 * g, 138);
            stepSec->setBounds (g, g + 362, 772, 226);
            stepName->setBounds (g + 470, g + 368, 292, 20);
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
            { "Break", P_bcLevel, P_bcPitch, "Pitch", -1, col::complex },
            { "Reese", P_rsLevel, P_rsWidth, "Width", 99, juce::Colour (0xffff7a59) },
            { "Core A", tg::coreParam (0, tg::CS_Level), tg::coreParam (0, tg::CS_Bright), "Bright", 99, juce::Colour (0xff59d0ff) },
            { "Core B", tg::coreParam (1, tg::CS_Level), tg::coreParam (1, tg::CS_Bright), "Bright", 99, juce::Colour (0xffb98cff) },   // (99: lit only when the pitch envelope moves all)
            { "Brk Layer", P_bsLLevel, P_bsLSwing, "Swing", -1, col::complex } };
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
            rt->knob (idx, n, 54);
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
        w->knob (P_warmth, "Warmth", 56);
        w->knob (P_bassKeep, "Bass Keep", 56);
        w->knob (P_analogDrift, "Drift", 56);
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
        // routing: the sub and the breaks can skip the effects
        auto* rtg = sec (page, "Routing", col::mixer);
        rtg->addInfo ("Sub Route: Normal goes through the filters and every effect. Dry skips the filters, the bus stages, the effects and "
                      "Performance FX, and joins before or after the Master Filter (it keeps its envelope, level, pan, mute and solo).  "
                      "Break Effects: the breaks through the effects (as always), or past them: into Performance FX, into the Master Filter, "
                      "or after everything. Dry signals get Master Volume and are delayed to match the Spectral stage, so they stay in time.");
        rtg->choice (P_subRoute, "Sub Route", 170);
        rtg->add (new ParamToggle (proc, P_subMono, "Mono < 120 Hz", col::mixer), 118, 40);
        rtg->choice (P_bcFxRoute, "Break Effects", 196);
        page->onResize = [page, stripList, srcHelp, engS, chipList, rt, w, outS, outL, outR, rtg]
        {
            const int g = 10, W = page->getWidth();
            const int stripW = 60, stripsW = stripW * kMxStrips + 2 * 4;
            srcHelp->setBounds (g, g, stripsW + 8, 32 + 360 + 8);
            for (int k = 0; k < stripList.size(); ++k) { stripList[k]->setBounds (g + 4 + k * stripW, g + 32, stripW - 4, 360); stripList[k]->toFront (false); }
            // under the strips: Analog, Routing, Effect Returns; on the right: the engines and the output
            const int by = g + 410;
            w->setBounds (g, by, 200, 136);
            rtg->setBounds (g + 208, by, 318, 136);
            rt->setBounds (g + 534, by, stripsW + 8 - 534, 136);
            const int rx = g + stripsW + 8 + g, rw = W - rx - g;
            engS->setBounds (rx, g, rw, 270);
            const int cw = (rw - 20) / 4;
            for (int k = 0; k < chipList.size(); ++k)
            {
                const int row = k / 4, c = k % 4;
                chipList[k]->setBounds (rx + 10 + c * cw, g + 32 + row * 114, cw - 6, 110);
                chipList[k]->toFront (false);
            }
            outS->setBounds (rx, g + 280, rw - 46, 226);
            outL->setBounds (rx + rw - 40, g + 280, 14, 226);
            outR->setBounds (rx + rw - 22, g + 280, 14, 226);
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
    // ---------------------------------------------------------------- Lab > Random Patch
    {
        using namespace tg::randompatch;
        auto* page = addPage ("Random Patch");
        const auto c = col::wavetable;
        auto* gen = sec (page, "Random Patch", c);
        gen->addInfo ("A whole new patch from a seed: a category steers what it uses and how it sounds, Complexity how much it switches on. "
                      "Every patch is in tune, levelled to about -12 dBFS on a short phrase, and the basses keep everything under 120 Hz in the "
                      "middle. Generate picks a new seed; < and > step through seeds; type a seed to make that one again (the same seed, settings "
                      "and folders always give the same patch). It gets a name, its category, the tag \"random\", author Random and its seed in "
                      "the note, and named macros. Undo goes back; Keep current puts it in B with your sound in A. Random in the header does the same "
                      "with these settings.");
        auto* catBox = new juce::ComboBox();
        catBox->addItem ("Any category", 1);
        for (int k = 0; k < kNumCategories; ++k) catBox->addItem (categoryName (k), k + 2);
        auto* cxBox = new juce::ComboBox();
        for (int k = 0; k < 3; ++k) cxBox->addItem (kComplexityNames[k], k + 1);
        auto* seedEd = new juce::TextEditor();
        seedEd->setInputRestrictions (10, "0123456789");
        seedEd->setTooltip ("The seed: type one and press Enter to make that patch");
        auto* prevB = new juce::TextButton ("<"), *nextB = new juce::TextButton (">");
        auto* genB = new juce::TextButton ("Generate");
        genB->setColour (juce::TextButton::buttonColourId, c.withAlpha (0.35f));
        auto* keepCur = new juce::ToggleButton ("Keep current (A / B)");
        keepCur->setTooltip ("Put the new patch in B and keep the sound you have now in A, to compare");
        auto* lblCat = new juce::Label ({}, "Category"), *lblCx = new juce::Label ({}, "Complexity"), *lblSeed = new juce::Label ({}, "Seed");
        for (auto* l : { lblCat, lblCx, lblSeed }) { l->setColour (juce::Label::textColourId, col::muted); l->setFont (juce::Font (juce::FontOptions (11.5f))); }
        gen->add (lblCat, 64, 24); gen->add (catBox, 170, 26); gen->add (lblCx, 76, 24); gen->add (cxBox, 110, 26);
        gen->newRow();
        gen->add (lblSeed, 64, 24); gen->add (seedEd, 110, 26); gen->add (prevB, 30, 26); gen->add (nextB, 30, 26); gen->add (genB, 120, 30); gen->add (keepCur, 170, 26);
        auto* report = page->own (new juce::Label());
        report->setColour (juce::Label::textColourId, col::text);
        report->setFont (juce::Font (juce::FontOptions (12.0f)));
        report->setJustificationType (juce::Justification::topLeft);
        report->setMinimumHorizontalScale (1.0f);

        auto* useS = sec (page, "Use", c);
        useS->addInfo ("What the generator may use. No oscillators keeps Osc 1-3 and the Sub off, so the sound has to come from the other engines. "
                       "Break Chopper and Sequencers are off to start with.");
        juce::Array<juce::ToggleButton*> useT;
        auto* noOscT = new juce::ToggleButton ("No oscillators");
        useS->add (noOscT, 132, 24);
        for (int u = 0; u < U_COUNT; ++u) { auto* tb = new juce::ToggleButton (kUseNames[u]); useT.add (tb); useS->add (tb, 132, 24); }

        auto* keepS = sec (page, "Keep from the current patch", c);
        keepS->addInfo ("Ticked groups stay exactly as they are in the patch you have now (the same groups as Mutate's locks, plus your samples and "
                        "breaks); the rest is new. For example, keep your filter and effects and randomise only the sources.");
        juce::Array<juce::ToggleButton*> keepT;
        for (int k = 0; k < kKeepCount; ++k) { auto* tb = new juce::ToggleButton (keepName (k)); keepT.add (tb); keepS->add (tb, 132, 24); }

        auto* folS = sec (page, "Sample folders", c);
        folS->addInfo ("Breaks: a folder of drum breaks (WAV, AIFF, FLAC or MP3, 1-30 s; subfolders too). Samples: a folder for the wavetables and "
                       "the Granular Synth. A patch picks from them at random (by its seed) and the files it uses are saved inside it. With no "
                       "folder, the breaks are the built-in synthesised ones and the wavetables play a tone the generator makes. Each folder is "
                       "scanned in the background (up to 10,000 files). The folders are a setting of the app, not of a patch.");
        juce::Label* folLabel[2]; juce::Label* folCount[2];
        for (int w = 0; w < 2; ++w)
        {
            auto* name = new juce::Label ({}, w == 0 ? "Breaks" : "Samples");
            name->setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
            name->setColour (juce::Label::textColourId, col::text);
            folLabel[w] = new juce::Label(); folLabel[w]->setColour (juce::Label::textColourId, col::muted); folLabel[w]->setFont (juce::Font (juce::FontOptions (11.5f)));
            folLabel[w]->setMinimumHorizontalScale (0.6f);
            folCount[w] = new juce::Label(); folCount[w]->setColour (juce::Label::textColourId, col::text); folCount[w]->setFont (juce::Font (juce::FontOptions (11.5f)));
            auto* choose = new juce::TextButton ("Choose...");
            auto* clear = new juce::TextButton ("Clear");
            auto* rescan = new juce::TextButton ("Rescan");
            choose->onClick = [this, w]
            {
                chooser = std::make_unique<juce::FileChooser> (w == 0 ? "Choose a folder of breaks" : "Choose a folder of samples", MegaSynthProcessor::getRandomFolder (w));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                      [w] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.isDirectory()) MegaSynthProcessor::setRandomFolder (w, f); });
            };
            clear->onClick = [w] { MegaSynthProcessor::setRandomFolder (w, juce::File()); };
            rescan->onClick = [w] { MegaSynthProcessor::rescanRandomFolder (w); };
            folS->add (name, 70, 24); folS->add (choose, 84, 26); folS->add (clear, 60, 26); folS->add (rescan, 70, 26); folS->add (folCount[w], 200, 24);
            folS->newRow();
            folS->add (folLabel[w], 500, 20);
            folS->newRow();
        }

        // the settings live in the processor (saved with the project)
        auto pull = [this, catBox, cxBox, seedEd, keepCur, noOscT, useT, keepT]
        {
            const auto& o = proc.randomOptions;
            catBox->setSelectedId (o.category + 2, juce::dontSendNotification);
            cxBox->setSelectedId (o.complexity + 1, juce::dontSendNotification);
            if (! seedEd->hasKeyboardFocus (true)) seedEd->setText (juce::String ((juce::int64) o.seed), false);
            keepCur->setToggleState (o.keepCurrent, juce::dontSendNotification);
            noOscT->setToggleState (o.noOsc, juce::dontSendNotification);
            for (int u = 0; u < U_COUNT; ++u) useT[u]->setToggleState (o.use[u], juce::dontSendNotification);
            for (int k = 0; k < kKeepCount; ++k) keepT[k]->setToggleState (((o.keepMask >> k) & 1u) != 0, juce::dontSendNotification);
        };
        catBox->onChange = [this, catBox] { proc.randomOptions.category = catBox->getSelectedId() - 2; };
        cxBox->onChange = [this, cxBox] { proc.randomOptions.complexity = cxBox->getSelectedId() - 1; };
        keepCur->onClick = [this, keepCur] { proc.randomOptions.keepCurrent = keepCur->getToggleState(); };
        noOscT->onClick = [this, noOscT] { proc.randomOptions.noOsc = noOscT->getToggleState(); };
        for (int u = 0; u < U_COUNT; ++u) useT[u]->onClick = [this, u, useT] { proc.randomOptions.use[u] = useT[u]->getToggleState(); };
        for (int k = 0; k < kKeepCount; ++k)
            keepT[k]->onClick = [this, k, keepT] { auto& m = proc.randomOptions.keepMask; m = keepT[k]->getToggleState() ? (m | (1u << k)) : (m & ~(1u << k)); };
        auto go = [this] (uint32_t seed)
        {
            if (proc.randomBusy()) return;
            proc.randomOptions.seed = seed;
            proc.startRandomPatch (proc.randomOptions);
            setStatus ("Making a random patch (seed " + juce::String ((juce::int64) seed) + ")...");
        };
        genB->onClick = [go] { go ((uint32_t) juce::Random::getSystemRandom().nextInt (1000000000) + 1u); };
        prevB->onClick = [this, go] { go (std::max (1u, proc.randomOptions.seed) - 1u); };
        nextB->onClick = [this, go] { go (proc.randomOptions.seed + 1u); };
        seedEd->onReturnKey = [go, seedEd] { go ((uint32_t) seedEd->getText().getLargeIntValue()); };
        pull();
        auto* ticker = page->own (new StoreTicker());
        ticker->tick = [this, pull, report, genB, prevB, nextB, folLabel0 = folLabel[0], folLabel1 = folLabel[1], folCount0 = folCount[0], folCount1 = folCount[1]]
        {
            pull();
            const bool busy = proc.randomBusy();
            for (auto* bt : { genB, prevB, nextB }) bt->setEnabled (! busy);
            const auto rep = proc.randomReport();
            report->setText (rep.isEmpty() ? juce::String ("Press Generate for a new patch.") : rep, juce::dontSendNotification);
            juce::Label* fl[2] = { folLabel0, folLabel1 }; juce::Label* fc[2] = { folCount0, folCount1 };
            for (int w = 0; w < 2; ++w)
            {
                const auto l = MegaSynthProcessor::randomFolderList (w);
                fl[w]->setText (l.folder == juce::File() ? juce::String ("No folder: ") + (w == 0 ? "the built-in breaks are used" : "a tone made by the generator is used")
                                                         : l.folder.getFullPathName(), juce::dontSendNotification);
                const juce::String what = w == 0 ? "breaks" : "samples";
                fc[w]->setText (l.folder == juce::File() ? juce::String() : l.scanning ? "scanning..." : juce::String (l.files.size()) + " " + what + (l.capped ? " (the first 10,000 files)" : ""),
                                juce::dontSendNotification);
            }
        };
        ticker->tick();
        page->onResize = [page, gen, report, useS, keepS, folS]
        {
            const int g = 10, W = page->getWidth();
            gen->setBounds (g, g, 600, 110);
            report->setBounds (g + 8, g + 118, 590, 160);
            useS->setBounds (g + 610, g, W - 3 * g - 600, 160);
            keepS->setBounds (g, g + 290, 600, 160);
            folS->setBounds (g + 610, g + 170, W - 3 * g - 600, 170);
        };
    }

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
        a->addTitleChip (new ModSourceChip (MS_AmpEnv, "drag"), 44);
        for (auto [idx, n] : { std::pair<int, const char*> { P_ampA, "Attack" }, { P_ampD, "Decay" }, { P_ampS, "Sustain" }, { P_ampR, "Release" } })
            a->knob (idx, n);
        auto* fe = sec (pageEnv, "Filter Envelope", col::env);
        fe->addTitleChip (new ModSourceChip (MS_FilterEnv, "drag"), 44);
        for (auto [idx, n] : { std::pair<int, const char*> { P_fEnvA, "Attack" }, { P_fEnvD, "Decay" }, { P_fEnvS, "Sustain" }, { P_fEnvR, "Release" } })
            fe->knob (idx, n);
        // the Master Filter: after every effect, before the Output trim
        auto* mfs = sec (page, "Master Filter", col::filter);
        mfs->addInfo ("A stereo filter at the very end: everything the synth makes, every effect included, passes through it, "
                      "then the Output level. Off, the sound passes straight through. Cutoff, Resonance, Drive and Mix can be modulated "
                      "by anything shared by all notes (LFOs, macros, the mod wheel, the sidechain...): routes follow the newest note's "
                      "sources. (Master Volume, on the Mixer, stays before the effects as it always has, so older patches sound the same.)");
        mfs->add (new ParamToggle (proc, P_mfOn, "On", col::filter), 60, 40);
        mfs->add (new CaptionedPicker (proc, P_mfModel, P_mfChar, true, "Filter"), 180, 44);
        auto* mfType = mfs->choice (P_mfType, "Type", 96);
        auto* mfSlope = mfs->choice (P_mfSlope, "Slope", 86);
        mfs->newRow();
        auto* mfCut = mfs->knob (P_mfCutoff, "Cutoff");
        auto* mfRes = mfs->knob (P_mfRes, "Resonance");
        mfs->knob (P_mfDrive, "Drive");
        mfs->knob (P_mfMix, "Mix");
        auto* mfCh = mfs->knob (P_mfCharAmt, "Character");
        mfs->knob (P_outGain, "Output");
        auto* mfv = page->own (new MasterFilterView (proc));
        // the controls the chosen filter doesn't use are greyed out (they keep their settings)
        auto* mfTick = page->own (new StoreTicker());
        mfTick->tick = [this, mfType, mfSlope, mfCut, mfRes, mfCh]
        {
            const int cm = juce::roundToInt (proc.param (P_mfChar)->convertFrom0to1 (proc.param (P_mfChar)->getValue()));
            const bool resUsed = cm == 0 || charModelInfo (cm).usesRes;
            mfType->setAlpha (cm > 0 ? 0.4f : 1.0f); mfSlope->setAlpha (cm > 0 ? 0.4f : 1.0f);
            mfCh->setAlpha (cm > 0 ? 1.0f : 0.4f);
            mfRes->setAlpha (resUsed ? 1.0f : 0.4f);
            mfCut->setAlpha (cm == CM_DJ ? 0.4f : 1.0f);
            mfCh->slider.setTooltip (cm > 0 ? juce::String ("Character: ") + charModelInfo (cm).character : juce::String ("Character: only the character models use it (this filter doesn't)"));
        };
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
            toggle (s, fxOnParam (slot), "On", 56);
            s->knob (fxMixParam (slot), fxSlotIsReturn (slot) ? "Return" : "Mix");
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
        r->addTitleChip (new ParamToggle (proc, P_sxRevFreeze, "Freeze", col::fx), 80);   // (v0.1.70)
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

        // ---- v0.1.70: the new Space effects, on their own pages (Dub & Spring, Reverbs, Width, Ducking)
        {
            const juce::Colour fxc = col::fx;
            auto presetsChip = [this] (Section* s)
            {
                auto* b = new juce::TextButton ("Space Presets...");
                b->setTooltip ("Presets for the new Space effects only (Dub Delay, Spring, Gated, Plate, Widener, Dimension, IR and the ducks): they set just those, as one undo step");
                b->onClick = [this, b]
                {
                    juce::PopupMenu m;
                    for (int k = 1; k < MegaSynthProcessor::kNumSpacePresets; ++k) m.addItem (k + 1, MegaSynthProcessor::spacePresetName (k));
                    m.addSeparator(); m.addItem (1, MegaSynthProcessor::spacePresetName (0));
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b), [this] (int r)
                    {
                        if (r <= 0) return;
                        proc.applySpacePreset (r - 1);
                        setStatus (juce::String ("Space: ") + MegaSynthProcessor::spacePresetName (r - 1));
                    });
                };
                s->addTitleChip (b, 118);
            };
            // Dub & Spring
            auto* pDS = addPage ("Dub & Spring");
            auto* dub = own (pDS, mk ("Dub Delay  (A ping-pong delay with filters and saturation in its feedback: Feedback goes to 110%, where the repeats "
                                      "build up into a dub runaway, held back by a soft limiter so it never runs away to clipping. Throw: in Hold, only "
                                      "what plays while you hold the Throw pad (or its learned note / CC, or the automatable Throw parameter) goes into "
                                      "the delay, and the repeats ring on after you let go; Latch: each press switches it on or off; Always: a normal "
                                      "delay. Return: how much is heard. Its duck is on the Ducking page.)"));
            presetsChip (dub);
            slotControls (dub, FS_Dub);
            dub->choice (P_sxDubSync, "Sync", 120);
            dub->choice (P_sxDubMode, "Throw Mode", 110);
            auto* throwBtn = new MomentaryParamButton (proc, P_sxDubThrow, "THROW", juce::Colour (0xffff9f43),
                                                       [this] { return proc.getEngine().fx().dubSending() && juce::roundToInt (proc.param (P_sxDubMode)->convertFrom0to1 (proc.param (P_sxDubMode)->getValue())) != 2; });
            throwBtn->setTooltip ("Throw: hold to send into the Dub Delay (Hold mode), or press to switch it on and off (Latch). Lit while it sends.");
            dub->add (throwBtn, 110, 56);
            auto* tLearn = new juce::TextButton ("Learn");
            tLearn->setTooltip ("Learn the Throw pad: press, then hit a pad (a note) or press a button that sends a CC");
            tLearn->setClickingTogglesState (false);
            tLearn->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff9f43));
            tLearn->onClick = [this] { proc.throwPadLearn = ! proc.throwPadLearn.load(); };
            auto* tClear = new juce::TextButton ("Clear");
            tClear->onClick = [this] { proc.throwPadLearn = false; proc.throwPadKind = 0; proc.throwPadNum = -1; setStatus ("Throw pad cleared"); };
            dub->add (tLearn, 60, 26); dub->add (tClear, 52, 26);
            auto* tLabel = new juce::Label();
            tLabel->setColour (juce::Label::textColourId, col::text);
            tLabel->setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
            dub->add (tLabel, 200, 26);
            dub->newRow();
            for (auto [idx, n] : { std::pair<int, const char*> { P_sxDubTime, "Time" }, { P_sxDubFb, "Feedback" }, { P_sxDubHP, "Low Cut" }, { P_sxDubLP, "High Cut" },
                                   { P_sxDubRes, "Resonance" }, { P_sxDubDrive, "Drive" }, { P_sxDubWidth, "Ping-Pong" } })
                dub->knob (idx, n);
            auto* spr = own (pDS, mk ("Spring Reverb  (Two or three modelled springs: each a loop through a chain of stretched all-pass filters, which "
                                      "gives the dispersive \"drip\" chirp of a real tank. Tension: how much it drips. Kick the tank: the crash of a "
                                      "knocked spring unit (the button, or MIDI-learn / automate it). Freeze holds the springs ringing. A model, not a "
                                      "capture of any particular unit.)"));
            slotControls (spr, FS_Spring);
            spr->choice (P_sxSprCount, "Springs", 110);
            auto* kick = new MomentaryParamButton (proc, P_sxSprKick, "KICK THE TANK", juce::Colour (0xffff6b6b), nullptr);
            kick->setTooltip ("A knock on the tank: noise and a thump straight into the springs");
            spr->add (kick, 130, 56);
            spr->add (new ParamToggle (proc, P_sxSprFreeze, "Freeze", fxc), 86, 40);
            spr->newRow();
            for (auto [idx, n] : { std::pair<int, const char*> { P_sxSprTension, "Tension" }, { P_sxSprDecay, "Decay" }, { P_sxSprTone, "Tone" } })
                spr->knob (idx, n);
            auto* dsTicker = pDS->own (new StoreTicker());
            dsTicker->tick = [this, tLabel, tLearn]
            {
                const bool learning = proc.throwPadLearn.load();
                const juce::String t = learning ? juce::String ("Pad: hit a pad or move a CC...") : "Pad: " + proc.throwPadText();
                if (tLabel->getText() != t) tLabel->setText (t, juce::dontSendNotification);
                tLearn->setToggleState (learning, juce::dontSendNotification);
            };
            pDS->onResize = [pDS, dub, spr]
            {
                const int g = 10, W = pDS->getWidth(), y = 6;
                dub->setBounds (g, y, W - 2 * g, 222);
                spr->setBounds (g, y + 232, W - 2 * g, 222);
            };

            // Reverbs: Plate, Gated, IR
            auto* pRv = addPage ("Reverbs");
            auto* pl = own (pRv, mk ("Plate Reverb  (After Jon Dattorro's plate (1997): dense and bright, with no early reflections, good on snares and "
                                     "leads. Mod moves the tail a little so it doesn't ring. Freeze holds the tail.)"));
            presetsChip (pl);
            slotControls (pl, FS_Plate);
            pl->add (new ParamToggle (proc, P_sxPlFreeze, "Freeze", fxc), 86, 40);
            for (auto [idx, n] : { std::pair<int, const char*> { P_sxPlDecay, "Decay" }, { P_sxPlDamp, "Damping" }, { P_sxPlPre, "Pre-delay" },
                                   { P_sxPlWidth, "Width" }, { P_sxPlMod, "Mod" } })
                pl->knob (idx, n);
            auto* gt = own (pRv, mk ("Gated Reverb  (A big room (an 8-line feedback delay network) cut off by a gate: it opens for Length after each "
                                     "trigger - the input crossing Threshold, the break's kicks or snares, either sequencer, the sidechain, notes or "
                                     "the Duck note - and closes hard (Hard) or falls away over the Length (Curved). Freeze holds it open and ringing.)"));
            slotControls (gt, FS_Gated);
            gt->choice (P_sxGateTrig, "Trigger", 150);
            gt->choice (P_sxGateSync, "Length", 100);
            gt->choice (P_sxGateShape, "Gate", 90);
            gt->add (new ParamToggle (proc, P_sxGateFreeze, "Freeze", fxc), 86, 40);
            gt->newRow();
            for (auto [idx, n] : { std::pair<int, const char*> { P_sxGateSize, "Size" }, { P_sxGatePre, "Pre-delay" }, { P_sxGateTone, "Tone" },
                                   { P_sxGateLen, "Length (ms)" }, { P_sxGateThr, "Threshold" } })
                gt->knob (idx, n);
            auto* ir = own (pRv, mk ("IR Reverb  (Convolution with an impulse: your own WAV / AIFF (up to 10 s, saved inside the project like the breaks) "
                                     "or a built-in one - Room and Hall made from noise, Plate and Spring taken from this synth's own plate and spring. "
                                     "Length trims it; Reverse plays it backwards. It loads in the background, so it may take a moment to be heard.)"));
            slotControls (ir, FS_Ir);
            ir->choice (P_sxIrType, "Impulse", 120);
            auto* irLoad = new juce::TextButton ("Load...");
            irLoad->onClick = [this]
            {
                irChooser = std::make_unique<juce::FileChooser> ("Choose an impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                irChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    if (proc.loadSampleFile (f, MegaSynthProcessor::kSlotIr))
                    {
                        auto* prm = proc.param (P_sxIrType); prm->beginChangeGesture(); prm->setValueNotifyingHost (0.0f); prm->endChangeGesture();
                        setStatus ("Impulse loaded: " + f.getFileName());
                    }
                    else setStatus ("Couldn't read " + f.getFileName());
                });
            };
            auto* irClear = new juce::TextButton ("Clear");
            irClear->onClick = [this] { proc.clearSample (MegaSynthProcessor::kSlotIr); setStatus ("Impulse file cleared"); };
            ir->add (irLoad, 70, 26); ir->add (irClear, 56, 26);
            ir->add (new ParamToggle (proc, P_sxIrReverse, "Reverse", fxc), 90, 40);
            auto* irInfo = new juce::Label();
            irInfo->setColour (juce::Label::textColourId, col::muted);
            irInfo->setFont (juce::Font (juce::FontOptions (11.5f)));
            ir->add (irInfo, 330, 26);
            ir->newRow();
            for (auto [idx, n] : { std::pair<int, const char*> { P_sxIrPre, "Pre-delay" }, { P_sxIrLen, "Length" }, { P_sxIrLP, "High Cut" },
                                   { P_sxIrHP, "Low Cut" }, { P_sxIrWidth, "Width" } })
                ir->knob (idx, n);
            auto* rvTicker = pRv->own (new StoreTicker());
            rvTicker->tick = [this, irInfo]
            {
                const int type = juce::roundToInt (proc.param (P_sxIrType)->convertFrom0to1 (proc.param (P_sxIrType)->getValue()));
                juce::String t;
                if (type == 0) t = proc.getSample (MegaSynthProcessor::kSlotIr) != nullptr ? "File: " + proc.getSampleName (MegaSynthProcessor::kSlotIr) : juce::String ("No file loaded: Load... one");
                else t = juce::String ("Built-in: ") + tg::kSxIrTypeLabels[juce::jlimit (0, 4, type)];
                if (proc.param (P_sxOnIr)->getValue() > 0.5f) t << (proc.getEngine().fx().irReady() ? "  (ready)" : "  (loading...)");
                if (irInfo->getText() != t) irInfo->setText (t, juce::dontSendNotification);
            };
            pRv->onResize = [pRv, pl, gt, ir]
            {
                const int g = 10, W = pRv->getWidth(), y = 6;
                pl->setBounds (g, y, W - 2 * g, 128);
                gt->setBounds (g, y + 134, W - 2 * g, 220);
                ir->setBounds (g, y + 360, W - 2 * g, 220);
            };

            // Width: the micro-pitch widener and the dimension chorus
            auto* pW = addPage ("Width");
            auto* wd = own (pW, mk ("Micro-pitch Widener  (Two copies of the sound, one pitched up and one down by Cents, each a little delayed, on the two "
                                    "sides: very wide, H3000-style. Low Cut keeps the lows out of the copies, so the bass stays mono. The mono sum "
                                    "keeps the dry sound in full: nothing cancels.)"));
            presetsChip (wd);
            slotControls (wd, FS_Widen);
            for (auto [idx, n] : { std::pair<int, const char*> { P_sxWidCents, "Cents" }, { P_sxWidDelay, "Delay" }, { P_sxWidLowCut, "Low Cut" }, { P_sxWidWidth, "Width" } })
                wd->knob (idx, n);
            auto* dm = own (pW, mk ("Dimension Chorus  (In the style of the Roland SDD-320: two short delays swept in opposite directions and cross-mixed, "
                                    "for width without an obvious wobble. Modes 1-4 get deeper and faster; press more than one to combine them, like "
                                    "the original's buttons. An approximation of its idea, not a model of its circuit.)"));
            slotControls (dm, FS_Dim);
            for (int m = 0; m < 4; ++m) dm->add (new ParamToggle (proc, P_sxDim1 + m, juce::String (m + 1), fxc), 60, 40);
            pW->onResize = [pW, wd, dm]
            {
                const int g = 10, W = pW->getWidth(), y = 6;
                wd->setBounds (g, y, W - 2 * g, 130);
                dm->setBounds (g, y + 138, W - 2 * g, 130);
            };

            // Ducking: every reverb and delay can dip under the hits
            auto* pDk = addPage ("Ducking");
            auto* dk = own (pDk, mk ("Ducking  (Each reverb and delay can dip under the hits, then bloom back in the gaps: Amount (how far), Attack (how "
                                     "fast it dips), Release (how long it takes to come back) and what it listens to - the break's kicks, snares or any "
                                     "hit (sorted as Break React sorts them), the main or the layer sequencer's hits, the sidechain input, the synth's "
                                     "notes, or a Duck note of your own (it isn't played, so a MIDI clip can duck anywhere). Amount 0 is off.)"));
            presetsChip (dk);
            static const char* duckNames[SD_COUNT] = { "Tape Delay", "90s Reverbs", "Dub Delay", "Spring", "Gated Reverb", "Plate", "IR Reverb" };
            for (auto* h : { "", "Amount", "Attack", "Release", "Listens to", "Gain now" })
            {
                auto* l = new juce::Label ({}, h);
                l->setColour (juce::Label::textColourId, col::muted);
                l->setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
                dk->add (l, juce::String (h).isEmpty() ? 130 : (juce::String (h) == "Listens to" ? 170 : 150), 20);
            }
            for (int d = 0; d < SD_COUNT; ++d)
            {
                dk->newRow();
                auto* l = new juce::Label ({}, duckNames[d]);
                l->setColour (juce::Label::textColourId, col::text);
                l->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
                dk->add (l, 130, 24);
                for (int fld : { SDF_Amt, SDF_Att, SDF_Rel }) dk->add (new BareSlider (proc, sxDuckParam (d, fld), fxc), 150, 24);
                dk->add (new BareChoice (proc, sxDuckParam (d, SDF_Src)), 170, 24);
                dk->add (new DuckMeter (proc, d), 150, 24);
            }
            dk->newRow();
            dk->knob (P_sxDuckNote, "Duck Note", 80);
            auto* dkHelp = new juce::Label ({}, "The Duck note (Off below 0): a key that ducks whatever listens to \"Duck note\" (and the Gated Reverb's trigger), and isn't played.");
            dkHelp->setColour (juce::Label::textColourId, col::muted);
            dkHelp->setFont (juce::Font (juce::FontOptions (11.5f)));
            dk->add (dkHelp, 700, 40);
            pDk->onResize = [pDk, dk]
            {
                const int g = 10, W = pDk->getWidth(), y = 6;
                dk->setBounds (g, y, W - 2 * g, 400);
            };
        }
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
            env[i]->addTitleChip (new ModSourceChip (MS_ModEnv1 + i, "drag"), 44);
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
            ms[k]->addTitleChip (new ModSourceChip (MS_Mseg1 + k, "drag"), 44);
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
        auto* macSec = sec (page, "Macros  (right-click any knob > Assign to macro, or drag a macro chip onto it; double-click a name to rename)", col::accent);
        for (int k = 0; k < 8; ++k) macSec->addTitleChip (new ModSourceChip (MS_Macro1 + k, "M" + juce::String (k + 1)), 30);
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
    copyBtn.setBounds (818, 10, 88, 28);
    pasteBtn.setBounds (818, 44, 88, 28);
    initBtn.setBounds (910, 10, 48, 28);
    randomBtn.setBounds (962, 10, 62, 28);
    sizeBtn.setBounds (1028, 10, 44, 28);
    abA.setBounds (1076, 10, 30, 28);
    abB.setBounds (1108, 10, 30, 28);
    abCopy.setBounds (1140, 10, 46, 28);
    if (browser != nullptr) browser->setBounds (0, 88, kDesignW, 640);
    undoBtn.setBounds (910, 44, 78, 28);
    redoBtn.setBounds (992, 44, 80, 28);
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
    // the factory presets under their categories (in the browser's order)
    for (auto& c : tg::patchCategories())
    {
        bool heading = false;
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            if (tg::factoryMeta (i).categoryShown() != c) continue;
            if (! heading) { patchBox.addSectionHeading ("Factory - " + c); heading = true; }
            patchBox.addItem (factory[(size_t) i].name, kFactoryIdBase + i + 1);
        }
    }
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
            browser->onStatus = [this] (const juce::String& t) { setStatus (t); refreshPatchList(); };
            content.addChildComponent (*browser);
            browser->setBounds (0, 88, kDesignW, 640);
        }
        browser->open();
    }
    else if (browser != nullptr) browser->setVisible (false);
    browseBtn.setToggleState (show, juce::dontSendNotification);
}

MegaSynthEditor::SaveInfo MegaSynthEditor::defaultSaveInfo() const
{
    SaveInfo si;
    si.name = proc.getPatchName();
    if (si.name.isEmpty() || si.name == "Init" || si.name == "Imported patch") si.name = "My Patch";
    si.meta = proc.getPatchMeta();
    // (a factory preset's author is "Mega Synth": a patch saved from it is yours)
    if (si.meta.author.isEmpty() || si.meta.author == "Mega Synth") si.meta.author = MegaSynthProcessor::getDefaultAuthor();
    return si;
}

void MegaSynthEditor::showSaveDialog()
{
    const auto si = defaultSaveInfo();
    saveWin = std::make_unique<juce::AlertWindow> ("Save patch", "Into your patch folder. The category, tags, author and note go with it "
                                                   "(edit them later in the browser).", juce::MessageBoxIconType::NoIcon, this);
    saveWin->addTextEditor ("name", si.name, "Name");
    saveWin->addComboBox ("category", tg::patchCategories(), "Category");
    if (auto* cb = saveWin->getComboBoxComponent ("category"))
    {
        const int i = tg::patchCategories().indexOf (si.meta.categoryShown());
        if (i >= 0) cb->setSelectedItemIndex (i, juce::dontSendNotification);
        else { cb->addItem (si.meta.categoryShown(), cb->getNumItems() + 1); cb->setSelectedItemIndex (cb->getNumItems() - 1, juce::dontSendNotification); }
    }
    saveWin->addTextEditor ("tags", si.meta.tagsText(), "Tags (commas or spaces)");
    saveWin->addComboBox ("author", MegaSynthProcessor::getAuthorList (authorsInPatches()), "Author (type a name or pick one)");
    if (auto* ab = saveWin->getComboBoxComponent ("author")) { ab->setEditableText (true); ab->setText (si.meta.author, juce::dontSendNotification); }
    saveWin->addTextEditor ("note", si.meta.note, "Note");
    saveWin->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWin->addButton ("Save As...", 2);
    saveWin->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWin->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MegaSynthEditor> (this)] (int r)
                              { if (safe != nullptr) safe->finishSave (r); }), false);
}

void MegaSynthEditor::finishSave (int button)
{
    if (saveWin == nullptr) return;
    auto w = std::move (saveWin);
    if (button == 0) return;
    SaveInfo si;
    si.meta.category = w->getComboBoxComponent ("category") != nullptr ? w->getComboBoxComponent ("category")->getText() : tg::PatchMeta::uncategorised();
    si.meta.tags = tg::PatchMeta::splitTags (w->getTextEditorContents ("tags"));
    si.meta.author = w->getComboBoxComponent ("author") != nullptr ? w->getComboBoxComponent ("author")->getText().trim() : juce::String();
    si.meta.note = w->getTextEditorContents ("note").trim();
    si.name = w->getTextEditorContents ("name").trim();
    saveWithInfo (si, button == 2);
}

void MegaSynthEditor::saveWithInfo (const SaveInfo& si, bool saveAs)
{
    if (si.meta.author.isNotEmpty()) MegaSynthProcessor::setDefaultAuthor (si.meta.author);
    proc.setPatchMeta (si.meta);
    const auto name = si.name.trim().isEmpty() ? juce::String ("My Patch") : si.name.trim();
    const auto f = MegaSynthProcessor::getPatchFolder().getChildFile (juce::File::createLegalFileName (name) + ".megasynth");
    if (saveAs)
    {
        chooser = std::make_unique<juce::FileChooser> ("Save patch", f, "*.megasynth");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto r = fc.getResult();
                                  if (r != juce::File()) savePatchAs (r.withFileExtension (".megasynth"));
                              });
        return;
    }
    if (f.existsAsFile())
    {
        auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Replace it?")
                        .withMessage (f.getFileNameWithoutExtension() + " is already saved. Replace it?").withButton ("Replace").withButton ("Cancel")
                        .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (opts, [safe = juce::Component::SafePointer<MegaSynthEditor> (this), f] (int r) { if (safe != nullptr && r == 1) safe->savePatchAs (f); });
        return;
    }
    savePatchAs (f);
}

juce::StringArray MegaSynthEditor::authorsInPatches()
{
    tgui::PatchCatalogue c; c.rescan();
    return c.allAuthors();
}

void MegaSynthEditor::savePatchAs (const juce::File& f)
{
    setStatus (proc.savePatchToFile (f) ? "Saved " + f.getFileNameWithoutExtension() + " (" + proc.getPatchMeta().categoryShown() + ")" : "Couldn't save the patch there");
    refreshPatchList();
    if (browser != nullptr && browser->isVisible()) browser->refresh();   // (a new author or tag shows in its filters at once)
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
    // a random patch arrived: say what it is
    if (proc.randomReportVersion.load() != lastRandomReport)
    {
        lastRandomReport = proc.randomReportVersion.load();
        const auto r = proc.randomReport();
        if (r.isNotEmpty() && ! proc.randomBusy()) { setStatus (r.upToFirstOccurrenceOf ("\n", false, false)); refreshPatchList(); }
        randomBtn.setEnabled (! proc.randomBusy());
    }
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
        // (Slice Keys Off, or Auto while the Break Sequencer runs: the keys play the synth, so no zone)
        const bool on = plainOf (P_bcOn) > 0.5f && proc.sliceKeysTakeNotes(), sel = plainOf (P_bcSelKeys) > 0.5f;
        // (the Break Sequencer page, or Key Entry: the edited pattern's own break)
        auto pageShown = [this] (juce::Component* c) { if (c == nullptr) return false; for (; c != nullptr && c != this; c = c->getParentComponent()) if (! c->isVisible()) return false; return true; };
        if (! pageShown (breakSeqPage) && plainOf (P_bsKeyEntry) < 0.5f) proc.keyBreakOverride = -1;
        const int ov = proc.keyBreakOverride.load();
        const int kb = juce::roundToInt (plainOf (P_bcKeyBase)), a = ov >= 0 ? juce::jlimit (0, tg::BreakData::kBreaks - 1, ov) : juce::jlimit (0, tg::BreakData::kBreaks - 1, juce::roundToInt (plainOf (P_bcActive)) - 1);
        const int ns = on ? proc.breakStore.get().brk[a].nSlices : 0;
        juce::String key; key << (on ? 1 : 0) << (sel ? 1 : 0) << kb << a << ns;
        if (key != lastZoneKey)
        {
            lastZoneKey = key;
            keyboard.tint = [on, sel, kb, a, ns] (int n)
            {
                if (! on) return juce::Colours::transparentBlack;
                if (n >= kb && n < kb + ns) return breakColour (a);
                if (sel && n >= kb - 4 && n < kb) return breakColour (n == kb - 4 ? 3 : n - (kb - 3)).withMultipliedAlpha (0.8f);
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
    // an older project's fills, converted when it loaded (v0.1.69): say what was done
    if (proc.lastFillConversion.isNotEmpty()) { setStatus (proc.lastFillConversion); proc.lastFillConversion.clear(); }
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

void MegaSynthEditor::timerCallbackForTests()
{
    timerCallback();
    std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
    {
        if (auto* fp = dynamic_cast<tgui::FilterPanel*> (c)) fp->refreshForTests();
        if (auto* pk = dynamic_cast<tgui::FilterPicker*> (c)) pk->sync();
        if (auto* kn = dynamic_cast<tgui::Knob*> (c)) kn->updateMod();
        if (auto* mv = dynamic_cast<tgui::MasterFilterView*> (c)) mv->refreshForTests();
        if (auto* st = dynamic_cast<tgui::StoreTicker*> (c); st != nullptr && st->tick && st->getParentComponent() != nullptr && st->getParentComponent()->isVisible()) st->tick();
        for (auto* ch : c->getChildren()) walk (ch);
    };
    walk (this);
}

// Break Sequencer Key Entry: the slice keys played since the last tick go onto the selected step (and the
// selection moves on by Advance). Shift on the page, or the sustain pedal, adds them as the step's Layer hit.
void MegaSynthEditor::breakKeyEntry (BreakStepGrid& grid)
{
    const auto entries = proc.takeKeyEntries();
    if (entries.empty() || proc.param (P_bsKeyEntry)->getValue() < 0.5f) return;
    static const int advSteps[] = { 0, 1, 2, 4 };
    const int adv = advSteps[juce::jlimit (0, 3, juce::roundToInt (proc.param (P_bsAdvance)->convertFrom0to1 (proc.param (P_bsAdvance)->getValue())))];
    const bool shift = juce::ModifierKeys::currentModifiers.isShiftDown();
    for (auto& k : entries)
    {
        proc.applyKeyEntry (grid.editedPattern(), grid.selected, k, shift || k.sustain, grid.layer);
        if (adv > 0) grid.selected = (grid.selected + adv) % grid.numSteps();
    }
    if (grid.onSelect) grid.onSelect (grid.selected);
    setStatus ("Key Entry: slice " + juce::String (entries.back().slice + 1) + " of Break " + juce::String (entries.back().brk + 1)
               + (adv > 0 ? ", now on step " + juce::String (grid.selected + 1) : juce::String()));
}

void MegaSynthEditor::breakKeyEntryForTests() { if (auto* g = dynamic_cast<BreakStepGrid*> (breakGridForTests)) breakKeyEntry (*g); }
int MegaSynthEditor::breakSelectedStepForTests() const { if (auto* g = dynamic_cast<BreakStepGrid*> (breakGridForTests)) return g->selected; return -1; }
void MegaSynthEditor::setBreakSelectedStepForTests (int step) { if (auto* g = dynamic_cast<BreakStepGrid*> (breakGridForTests)) g->selected = step; }

