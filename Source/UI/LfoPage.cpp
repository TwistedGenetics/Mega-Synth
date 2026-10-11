#include "LfoPage.h"
#include "FilterPanel.h"   // Segmented
#include "../PluginEditor.h"

using namespace tg;

namespace tgui
{

using Pt = LfoShapeStore::Point;

static int xbase (int lfo) { return P_lfo1Sync + kLfoXStride * lfo; }

//==============================================================================
LfoShapeEditor::LfoShapeEditor (MegaSynthProcessor& p, int k) : proc (p), lfo (k)
{
    pts = proc.lfoShapes.getPoints (lfo);
    setTooltip ("Drag points to shape the LFO (this turns Custom on). Double-click to add a point, right-click a point "
                "for its curve or to delete it, shift-drag a segment to bend it.");
    startTimerHz (30);
}

void LfoShapeEditor::timerCallback()
{
    if (! isShowing()) return;
    bool dirty = false;
    if (proc.lfoShapes.getVersion() != lastVersion && drag < 0 && bendSeg < 0)
    {
        lastVersion = proc.lfoShapes.getVersion();
        pts = proc.lfoShapes.getPoints (lfo);
        dirty = true;
    }
    const float ph = proc.getEngine().lfoPlayhead[lfo].load();
    if (std::abs (ph - lastPlayhead) > 1.0e-4f) { lastPlayhead = ph; dirty = true; }
    const float key = proc.param (P_lfo1Wave + 3 * lfo)->getValue() * 10.0f + proc.param (xbase (lfo) + 7)->getValue()
                    + proc.param (xbase (lfo) + 5)->getValue() * 100.0f;
    if (key != lastParamKey) { lastParamKey = key; dirty = true; }
    if (dirty) repaint();
}

juce::Point<float> LfoShapeEditor::toScreen (float x, float y) const
{
    const auto a = area();
    return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
}

float LfoShapeEditor::snapX (float x) const
{
    if (grid <= 0) return juce::jlimit (0.0f, 1.0f, x);
    return juce::jlimit (0.0f, 1.0f, std::round (x * grid) / (float) grid);
}

int LfoShapeEditor::pointAt (juce::Point<float> m) const
{
    for (int i = 0; i < (int) pts.size(); ++i)
        if (toScreen (pts[(size_t) i].x, pts[(size_t) i].y).getDistanceFrom (m) < 9.0f) return i;
    return -1;
}

void LfoShapeEditor::commit (std::vector<Pt> p)
{
    // editing the shape means using it
    auto* c = proc.param (xbase (lfo) + 7);
    if (c->getValue() < 0.5f) { c->beginChangeGesture(); c->setValueNotifyingHost (1.0f); c->endChangeGesture(); }
    proc.lfoShapes.setPoints (lfo, std::move (p));
    lastVersion = proc.lfoShapes.getVersion();
    pts = proc.lfoShapes.getPoints (lfo);
    repaint();
}

void LfoShapeEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    const auto a = area();
    // grid
    const int div = grid > 0 ? grid : 4;
    for (int i = 1; i < div; ++i)
    {
        g.setColour (col::border.withAlpha (i * 4 % div == 0 ? 0.9f : 0.45f));
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * i / div), a.getY(), a.getBottom());
    }
    g.setColour (col::border.withAlpha (0.6f));
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());

    const bool isCustom = proc.param (xbase (lfo) + 7)->getValue() > 0.5f;
    const int wave = juce::roundToInt (proc.param (P_lfo1Wave + 3 * lfo)->convertFrom0to1 (proc.param (P_lfo1Wave + 3 * lfo)->getValue()));
    auto valueAt = [&] (double x) -> float
    {
        if (isCustom) return LfoShapeStore::shapeAt (pts, x);
        switch (wave)
        {
            case 1:  return (float) (1.0 - 2.0 * std::abs (x - 0.5));   // 0..1 versions of the waveforms
            case 2:  return (float) x;
            case 3:  return x < 0.5 ? 1.0f : 0.0f;
            default: return (float) (0.5 + 0.5 * std::sin (x * juce::MathConstants<double>::twoPi));
        }
    };
    juce::Path path;
    const int N = juce::jmax (64, (int) a.getWidth());
    for (int i = 0; i <= N; ++i)
    {
        const double x = (double) i / N;
        const auto pt = toScreen ((float) x, valueAt (std::min (x, 0.99999)));
        if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
    }
    juce::Path fill (path);
    fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
    g.setColour (col::mod.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (isCustom ? col::mod : col::mod.withAlpha (0.6f));
    g.strokePath (path, juce::PathStrokeType (2.0f));
    if (isCustom)
        for (auto& q : pts)
        {
            const auto pt = toScreen (q.x, q.y);
            g.setColour (q.curve == LfoShapeStore::CV_Step ? col::fx : (q.curve == LfoShapeStore::CV_Smooth ? col::mixer : col::text));
            g.fillEllipse (pt.x - 4.5f, pt.y - 4.5f, 9.0f, 9.0f);
        }
    // playhead
    const float ph = proc.getEngine().lfoPlayhead[lfo].load();
    if (ph >= 0.0f)
    {
        const auto pt = toScreen (ph, valueAt (ph));
        g.setColour (col::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) pt.x, a.getY(), a.getBottom());
        g.setColour (col::text);
        g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);
    }
    if (! isCustom)
    {
        g.setColour (col::muted);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("drag to draw your own shape", a.reduced (4.0f), juce::Justification::topRight);
    }
}

void LfoShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    const int i = pointAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (i < 0) return;
        juce::PopupMenu m;
        const int cur = pts[(size_t) i].curve;
        m.addItem (1, "Linear", true, cur == LfoShapeStore::CV_Linear);
        m.addItem (2, "Smooth", true, cur == LfoShapeStore::CV_Smooth);
        m.addItem (3, "Step (hold)", true, cur == LfoShapeStore::CV_Step);
        m.addItem (5, "Straight (no bend)", pts[(size_t) i].bend != 0.0f);
        m.addSeparator();
        m.addItem (4, "Delete point", i > 0 && pts.size() > 1);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                         [safe = juce::Component::SafePointer<LfoShapeEditor> (this), i] (int r)
                         {
                             if (safe == nullptr || r <= 0 || i >= (int) safe->pts.size()) return;
                             auto p = safe->pts;
                             if (r >= 1 && r <= 3) p[(size_t) i].curve = r - 1;
                             else if (r == 4) p.erase (p.begin() + i);
                             else if (r == 5) p[(size_t) i].bend = 0.0f;
                             safe->commit (p);
                         });
        return;
    }
    if (e.mods.isShiftDown())
    {
        // bend the segment under the mouse
        const auto a = area();
        const float x = (e.position.x - a.getX()) / a.getWidth();
        bendSeg = 0;
        for (int k = 0; k < (int) pts.size(); ++k) if (pts[(size_t) k].x <= x) bendSeg = k;
        bendStart = pts[(size_t) bendSeg].bend; bendY = e.position.y;
        return;
    }
    drag = i;
}

void LfoShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto a = area();
    if (bendSeg >= 0 && bendSeg < (int) pts.size())
    {
        pts[(size_t) bendSeg].bend = juce::jlimit (-1.0f, 1.0f, bendStart + (bendY - e.position.y) / 80.0f);
        commit (pts);
        return;
    }
    if (drag < 0) return;
    auto p = pts;
    float x = snapX ((e.position.x - a.getX()) / a.getWidth());
    const float y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    if (drag == 0) x = 0.0f;   // the first point stays at the start
    else
    {
        const float lo = p[(size_t) drag - 1].x + 0.001f, hi = drag + 1 < (int) p.size() ? p[(size_t) drag + 1].x - 0.001f : 0.999f;
        x = juce::jlimit (lo, std::max (lo, hi), x);
    }
    p[(size_t) drag].x = x; p[(size_t) drag].y = y;
    commit (p);
}

void LfoShapeEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0 || bendSeg >= 0) proc.pushHistory ("LFO shape");
    drag = -1; bendSeg = -1;
}

void LfoShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pointAt (e.position) >= 0 || (int) pts.size() >= LfoShapeStore::kMaxPoints) return;
    const auto a = area();
    auto p = pts;
    const float x = snapX ((e.position.x - a.getX()) / a.getWidth());
    const float y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    if (x <= 0.0f || x >= 1.0f) return;
    for (auto& q : p) if (std::abs (q.x - x) < 0.001f) return;
    p.push_back ({ x, y });
    commit (p);
    proc.pushHistory ("LFO shape");
}

//==============================================================================
std::vector<Pt>& LfoPanel::clipboard() { static std::vector<Pt> c; return c; }
bool& LfoPanel::clipboardValid() { static bool v = false; return v; }

LfoPanel::LfoPanel (MegaSynthProcessor& p, int k, juce::Colour c) : proc (p), lfo (k), colour (c)
{
    const int xb = xbase (lfo);
    wave = std::make_unique<Choice> (proc, P_lfo1Wave + 3 * lfo, "Wave");
    sync = std::make_unique<Choice> (proc, xb + 0, "Sync");
    retrig = std::make_unique<Choice> (proc, xb + 1, "Retrigger");
    mode = std::make_unique<Choice> (proc, xb + 3, "Mode");
    sync->box.setTooltip ("Off: the Rate knob (Hz). Otherwise one cycle lasts this note length at the DAW's tempo.");
    retrig->box.setTooltip ("Each note: every note starts its own LFO. Free: one LFO shared by all notes (synced: locked to the "
                            "song position). Each bar / Host start: restarts at every bar line / when the DAW starts.");
    mode->box.setTooltip ("One-shot runs once from the retrigger point, then holds at the end of the shape.");
    for (auto* ch : { wave.get(), sync.get(), retrig.get(), mode.get() }) addAndMakeVisible (ch);
    const std::pair<int, const char*> kn[] = { { P_lfo1Rate + 3 * lfo, "Rate" }, { P_lfo1Depth + 3 * lfo, "Depth" }, { xb + 2, "Phase" },
                                               { xb + 4, "Smooth" }, { xb + 6, "Fade In" } };
    for (auto& [idx, name] : kn) addAndMakeVisible (knobs.add (new Knob (proc, idx, name, colour)));
    for (auto* b : { &uni, &custom })
    {
        b->setColour (juce::ToggleButton::tickColourId, colour);
        addAndMakeVisible (*b);
    }
    uni.setTooltip ("0..1 instead of -1..1: modulation only goes one way");
    custom.setTooltip ("Use the drawn shape instead of the waveform");
    btnAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, kParamIds[xb + 5], uni));
    btnAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, kParamIds[xb + 7], custom));

    editor = std::make_unique<LfoShapeEditor> (proc, lfo);
    addAndMakeVisible (*editor);
    gridSel = std::make_unique<Segmented> (proc, -1, juce::StringArray { "No snap", "1/4", "1/8", "1/16", "T6", "T12" }, colour);
    gridSel->setTooltip ("Snap points across the cycle: quarters, eighths, sixteenths or triplets");
    gridSel->onSelect = [this] (int i) { static const int g[] = { 0, 4, 8, 16, 6, 12 }; editor->grid = g[juce::jlimit (0, 5, i)]; editor->repaint(); };
    addAndMakeVisible (*gridSel);
    presets.onClick = [this]
    {
        juce::PopupMenu m;
        for (int i = 0; i < LfoShapeStore::PR_COUNT; ++i) m.addItem (i + 1, LfoShapeStore::presetName (i));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presets), [this] (int r)
        {
            if (r <= 0) return;
            proc.lfoShapes.setPreset (lfo, r - 1, (uint32_t) juce::Time::getMillisecondCounter());
            auto* c = proc.param (xbase (lfo) + 7);
            c->beginChangeGesture(); c->setValueNotifyingHost (1.0f); c->endChangeGesture();
            proc.pushHistory ("LFO shape");
        });
    };
    copy.onClick = [this] { clipboard() = proc.lfoShapes.getPoints (lfo); clipboardValid() = true; };
    paste.onClick = [this]
    {
        if (! clipboardValid()) return;
        proc.lfoShapes.setPoints (lfo, clipboard());
        auto* c = proc.param (xbase (lfo) + 7);
        c->beginChangeGesture(); c->setValueNotifyingHost (1.0f); c->endChangeGesture();
        proc.pushHistory ("Paste LFO shape");
    };
    presets.setTooltip ("Load a shape into this LFO (turns Custom on)");
    copy.setTooltip ("Copy this LFO's custom shape");
    paste.setTooltip ("Paste a copied shape into this LFO");
    for (auto* b : { &presets, &copy, &paste }) addAndMakeVisible (*b);
    startTimerHz (10);
}

LfoPanel::~LfoPanel() { stopTimer(); }

void LfoPanel::timerCallback()
{
    // the Rate knob only counts when Sync is off; the waveform only when Custom is off
    const bool synced = proc.param (xbase (lfo))->getValue() > 0.001f;
    const bool cust = proc.param (xbase (lfo) + 7)->getValue() > 0.5f;
    knobs[0]->setAlpha (synced ? 0.35f : 1.0f);
    wave->setAlpha (cust ? 0.4f : 1.0f);
}

void LfoPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (12.0f, 10.0f, 4.0f, 16.0f, 2.0f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("LFO " + juce::String (lfo + 1), 22, 8, 80, 20, juce::Justification::centredLeft);
}

void LfoPanel::resized()
{
    const int W = getWidth(), H = getHeight();
    // left: controls; right: the shape
    int x = 12, y = 34;
    y = 28;
    wave->setBounds (x, y, 112, 40); sync->setBounds (x + 118, y, 120, 40);
    retrig->setBounds (x, y + 42, 112, 40); mode->setBounds (x + 118, y + 42, 120, 40);
    uni.setBounds (x, y + 86, 100, 22); custom.setBounds (x + 118, y + 86, 100, 22);
    const int kx = 250;
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (kx + i * 58, 40, 58, 76);
    const int ex = kx + knobs.size() * 58 + 8;
    gridSel->setBounds (ex, 8, std::min (260, W - ex - 170), 22);
    presets.setBounds (W - 168, 8, 64, 22); copy.setBounds (W - 100, 8, 44, 22); paste.setBounds (W - 52, 8, 44, 22);
    editor->setBounds (ex, 34, W - ex - 10, H - 44);
}

} // namespace tgui
