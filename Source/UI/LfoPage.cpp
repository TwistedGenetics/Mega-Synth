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

namespace tgui
{
MsegEditor::MsegEditor (MegaSynthProcessor& p, int e) : proc (p), env (e)
{
    shape = proc.msegs.get (env);
    scaleT = std::max (0.5f, totalTime() * 1.25f);
    setTooltip ("Drag points (left-right: stage time, up-down: level). Double-click to add a stage, right-click a point for "
                "sustain, loop or delete, shift-drag a stage to curve it.");
    startTimerHz (30);
}

float MsegEditor::totalTime() const { float t = 0; for (int i = 0; i < shape.n; ++i) t += shape.st[(size_t) i].time; return t; }

juce::Point<float> MsegEditor::pt (int i) const
{
    auto a = getLocalBounds().toFloat().reduced (10.0f, 12.0f);
    float t = 0; float lv = shape.start;
    for (int k = 0; k <= i && k < shape.n; ++k) { t += shape.st[(size_t) k].time; lv = shape.st[(size_t) k].level; }
    return { a.getX() + a.getWidth() * t / scaleT, a.getBottom() - a.getHeight() * lv };
}

int MsegEditor::pointAt (juce::Point<float> m) const
{
    for (int i = shape.n - 1; i >= -1; --i) if (pt (i).getDistanceFrom (m) < 9.0f) return i;
    return -2;
}

void MsegEditor::commit()
{
    proc.msegs.set (env, shape);
    lastVersion = proc.msegs.getVersion();
    repaint();
}

void MsegEditor::timerCallback()
{
    if (! isShowing()) return;
    bool dirty = false;
    if (proc.msegs.getVersion() != lastVersion && drag < -1 && bend < 0)
    {
        lastVersion = proc.msegs.getVersion(); shape = proc.msegs.get (env);
        scaleT = std::max (0.5f, totalTime() * 1.25f); dirty = true;
    }
    const float ph = proc.getEngine().msegPlayhead[env].load();
    if (std::abs (ph - lastPlay) > 1.0e-4f) { lastPlay = ph; dirty = true; }
    if (dirty) repaint();
}

void MsegEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (10.0f, 12.0f);
    if (shape.loopStart >= 0)
    {
        const float x0 = pt (shape.loopStart - 1).x, x1 = pt (shape.loopEnd).x;
        g.setColour (col::env.withAlpha (0.12f));
        g.fillRect (x0, a.getY(), x1 - x0, a.getHeight());
        g.setColour (col::env);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("loop", (int) x0 + 3, (int) a.getY(), 40, 12, juce::Justification::left);
    }
    juce::Path path;
    path.startNewSubPath (pt (-1));
    float from = shape.start;
    for (int i = 0; i < shape.n; ++i)
    {
        const auto p0 = pt (i - 1), p1 = pt (i);
        const auto& st = shape.st[(size_t) i];
        for (int k = 1; k <= 24; ++k)
        {
            const float u = k / 24.0f;
            const float lv = from + (st.level - from) * tg::MsegStore::curveAt (u, st.curve);
            path.lineTo (p0.x + (p1.x - p0.x) * u, a.getBottom() - a.getHeight() * lv);
        }
        from = st.level;
    }
    g.setColour (col::env);
    g.strokePath (path, juce::PathStrokeType (2.0f));
    for (int i = -1; i < shape.n; ++i)
    {
        const auto p = pt (i);
        const bool sus = i >= 0 && i == shape.sustain;
        g.setColour (sus ? col::fx : col::text);
        g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
        if (sus) { g.setFont (juce::Font (juce::FontOptions (10.0f))); g.drawText ("sustain", (int) p.x - 20, (int) p.y - 18, 40, 12, juce::Justification::centred); }
    }
    const float ph = proc.getEngine().msegPlayhead[env].load();
    if (ph >= 0.0f)
    {
        const float x = a.getX() + a.getWidth() * std::min (ph, scaleT) / scaleT;
        g.setColour (col::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        const float lv = proc.getEngine().msegLevelShown[env].load();
        g.fillEllipse (x - 3.5f, a.getBottom() - a.getHeight() * lv - 3.5f, 7.0f, 7.0f);
    }
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    const bool sync = proc.param (P_mseg1Sync + 2 * env)->getValue() > 0.5f;
    g.drawText (juce::String (totalTime(), 2) + (sync ? " beats" : " s"), r.reduced (8.0f, 2.0f), juce::Justification::bottomRight);
}

void MsegEditor::mouseDown (const juce::MouseEvent& e)
{
    const int i = pointAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (i < 0) return;
        juce::PopupMenu m;
        m.addItem (1, "Sustain here", true, shape.sustain == i);
        m.addItem (2, "Loop starts at this stage", true, shape.loopStart == i);
        m.addItem (3, "Loop ends at this stage", true, shape.loopEnd == i);
        m.addItem (4, "No loop", shape.loopStart >= 0);
        m.addItem (5, "No sustain", shape.sustain >= 0);
        m.addItem (6, "Straight (no curve)", shape.st[(size_t) i].curve != 0.0f);
        m.addSeparator();
        m.addItem (7, "Delete stage", shape.n > 1);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                         [safe = juce::Component::SafePointer<MsegEditor> (this), i] (int r)
                         {
                             if (safe == nullptr || r <= 0) return;
                             auto& s = safe->shape;
                             switch (r)
                             {
                                 case 1: s.sustain = (s.sustain == i ? -1 : i); s.loopStart = s.loopEnd = -1; break;
                                 case 2: s.loopStart = i; if (s.loopEnd < i) s.loopEnd = s.n - 1; s.sustain = -1; break;
                                 case 3: s.loopEnd = i; if (s.loopStart < 0 || s.loopStart > i) s.loopStart = 0; s.sustain = -1; break;
                                 case 4: s.loopStart = s.loopEnd = -1; break;
                                 case 5: s.sustain = -1; break;
                                 case 6: s.st[(size_t) i].curve = 0.0f; break;
                                 case 7:
                                     for (int k = i; k + 1 < s.n; ++k) s.st[(size_t) k] = s.st[(size_t) k + 1];
                                     --s.n;
                                     if (s.sustain >= s.n) s.sustain = -1;
                                     if (s.loopEnd >= s.n) s.loopStart = s.loopEnd = -1;
                                     break;
                                 default: break;
                             }
                             safe->commit();
                             safe->proc.pushHistory ("MSEG");
                         });
        return;
    }
    if (e.mods.isShiftDown())
    {
        bend = -1;
        for (int k = 0; k < shape.n; ++k) if (e.position.x >= pt (k - 1).x && e.position.x <= pt (k).x) bend = k;
        if (bend >= 0) { bendStart = shape.st[(size_t) bend].curve; bendY = e.position.y; }
        return;
    }
    drag = i;
}

void MsegEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto a = getLocalBounds().toFloat().reduced (10.0f, 12.0f);
    if (bend >= 0)
    {
        shape.st[(size_t) bend].curve = juce::jlimit (-1.0f, 1.0f, bendStart - (bendY - e.position.y) / 80.0f);
        commit();
        return;
    }
    if (drag < -1) return;
    const float lv = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    if (drag == -1) { shape.start = lv; commit(); return; }
    const float prevX = pt (drag - 1).x;
    const float t = juce::jlimit (0.0f, 16.0f, (e.position.x - prevX) / a.getWidth() * scaleT);
    shape.st[(size_t) drag].time = std::round (t * 1000.0f) / 1000.0f;
    shape.st[(size_t) drag].level = lv;
    commit();
}

void MsegEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag >= -1 || bend >= 0) { proc.pushHistory ("MSEG"); scaleT = std::max (0.5f, totalTime() * 1.25f); repaint(); }
    drag = -2; bend = -1;
}

void MsegEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pointAt (e.position) >= -1 || shape.n >= tg::MsegShape::kMaxStages) return;
    const auto a = getLocalBounds().toFloat().reduced (10.0f, 12.0f);
    const float tClick = (e.position.x - a.getX()) / a.getWidth() * scaleT;
    const float lv = juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    float t0 = 0; int at = shape.n;
    for (int k = 0; k < shape.n; ++k) { const float t1 = t0 + shape.st[(size_t) k].time; if (tClick < t1) { at = k; break; } t0 = t1; }
    for (int k = shape.n; k > at; --k) shape.st[(size_t) k] = shape.st[(size_t) k - 1];
    if (at < shape.n) { const float rest = shape.st[(size_t) at + 1].time - (tClick - t0); shape.st[(size_t) at + 1].time = std::max (0.001f, rest); }
    shape.st[(size_t) at] = { std::max (0.001f, tClick - t0), lv, 0.0f };
    ++shape.n;
    if (shape.sustain >= at) ++shape.sustain;
    if (shape.loopStart >= at) { ++shape.loopStart; ++shape.loopEnd; } else if (shape.loopEnd >= at) ++shape.loopEnd;
    commit();
    proc.pushHistory ("MSEG");
}
} // namespace tgui
