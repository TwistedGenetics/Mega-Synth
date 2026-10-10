#include "FxPages.h"
#include "../PluginEditor.h"

using namespace tg;

namespace tgui
{

//==============================================================================
void ShaperCurve::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    auto p = r.reduced (8.0f);
    g.setColour (col::border.withAlpha (0.6f));
    for (int k = 1; k < 4; ++k) g.drawVerticalLine ((int) (p.getX() + p.getWidth() * k / 4), p.getY(), p.getBottom());
    const auto& st = proc.fxRack;
    juce::Path path;
    for (int i = 0; i <= FxRackStore::kCurve; ++i)
    {
        const float x = p.getX() + p.getWidth() * i / FxRackStore::kCurve;
        const float y = p.getBottom() - p.getHeight() * st.curve (i % FxRackStore::kCurve);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    juce::Path fill (path);
    fill.lineTo (p.getRight(), p.getBottom()); fill.lineTo (p.getX(), p.getBottom()); fill.closeSubPath();
    g.setColour (col::fx.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (col::fx);
    g.strokePath (path, juce::PathStrokeType (2.0f));
    // playhead
    const float ph = proc.getEngine().fx().shaper.phaseShown.load();
    const bool on = proc.param (P_fxOnShaper)->getValue() > 0.5f;
    if (on)
    {
        g.setColour (col::text.withAlpha (0.6f));
        g.drawVerticalLine ((int) (p.getX() + p.getWidth() * ph), p.getY(), p.getBottom());
    }
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("draw the volume over one cycle", r.reduced (10.0f, 4.0f), juce::Justification::topRight);
}

void ShaperCurve::drawAt (juce::Point<float> pt)
{
    auto p = getLocalBounds().toFloat().reduced (8.0f);
    const int idx = juce::jlimit (0, FxRackStore::kCurve - 1, (int) ((pt.x - p.getX()) / p.getWidth() * FxRackStore::kCurve));
    const float val = juce::jlimit (0.0f, 1.0f, (p.getBottom() - pt.y) / p.getHeight());
    auto& st = proc.fxRack;
    if (lastIdx >= 0 && lastIdx != idx)
    {
        // fill the points between the last drag position and this one
        const int a = std::min (lastIdx, idx), b = std::max (lastIdx, idx);
        for (int k = a; k <= b; ++k)
        {
            const float t = b == a ? 1.0f : (float) (k - lastIdx) / (float) (idx - lastIdx);
            st.setCurve (k, lastVal + (val - lastVal) * t);
        }
    }
    else st.setCurve (idx, val);
    lastIdx = idx; lastVal = val;
    repaint();
}

void ShaperCurve::mouseDown (const juce::MouseEvent& e) { lastIdx = -1; drawAt (e.position); }
void ShaperCurve::mouseDrag (const juce::MouseEvent& e) { drawAt (e.position); }
void ShaperCurve::mouseUp (const juce::MouseEvent&) { lastIdx = -1; }

//==============================================================================
void MultibandMeters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    const char* names[] = { "Low", "Mid", "High" };
    const float w = (r.getWidth() - 16.0f) / 3.0f;
    for (int b = 0; b < 3; ++b)
    {
        const float db = proc.getEngine().fx().multiband.meter[b].load();
        shown[b] += (db - shown[b]) * 0.3f;
        auto bar = juce::Rectangle<float> (r.getX() + 8.0f + b * w + 6.0f, r.getY() + 18.0f, w - 12.0f, r.getHeight() - 36.0f);
        g.setColour (col::panel3);
        g.fillRoundedRectangle (bar, 4.0f);
        const float zero = bar.getCentreY();
        const float h = juce::jlimit (-1.0f, 1.0f, shown[b] / 24.0f) * bar.getHeight() * 0.5f;
        g.setColour (h < 0 ? col::fx : col::mixer);   // pink: compressing, green: lifting
        if (h < 0) g.fillRect (bar.getX(), zero, bar.getWidth(), -h);
        else g.fillRect (bar.getX(), zero - h, bar.getWidth(), h);
        g.setColour (col::muted);
        g.drawHorizontalLine ((int) zero, bar.getX(), bar.getRight());
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (names[b], (int) bar.getX(), (int) r.getY() + 2, (int) bar.getWidth(), 14, juce::Justification::centred);
        g.drawText ((shown[b] > 0.05f ? "+" : "") + juce::String (shown[b], 1) + " dB", (int) bar.getX() - 6, (int) bar.getBottom() + 2, (int) bar.getWidth() + 12, 14, juce::Justification::centred);
    }
}

//==============================================================================
void StutterPad::set (bool on)
{
    down = on;
    auto* p = proc.param (P_rptTrigger);
    if (on) p->beginChangeGesture();
    p->setValueNotifyingHost (on ? 1.0f : 0.0f);
    if (! on) p->endChangeGesture();
    repaint();
}

void StutterPad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const int act = proc.getEngine().fx().stutter.activity.load();
    g.setColour (down ? col::fx : (act > 0 ? col::fx.withAlpha (0.55f) : col::panel3));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (act > 0 ? col::fx.brighter (0.3f) : col::border);
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (act > 0 ? "REPEAT " + juce::String (act) : "HOLD TO REPEAT", r, juce::Justification::centred);
}

//==============================================================================
RackList::RackList (MegaSynthProcessor& p) : proc (p)
{
    for (int s = 0; s < FS_COUNT; ++s)
    {
        auto* t = on.add (new juce::ToggleButton ("On"));
        t->setColour (juce::ToggleButton::tickColourId, col::fx);
        onAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, kParamIds[P_fxOnStutter + 2 * s], *t));
        addAndMakeVisible (t);
        auto* m = mix.add (new juce::Slider());
        m->setSliderStyle (juce::Slider::LinearBar);
        m->setColour (juce::Slider::trackColourId, col::fx.withAlpha (0.5f));
        m->setColour (juce::Slider::backgroundColourId, col::panel3);
        m->setColour (juce::Slider::textBoxTextColourId, col::text);
        m->setTooltip (fxSlotIsSend (s) ? "Mix: scales this effect's return" : "Mix: dry / wet");
        mixAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, kParamIds[P_fxMixStutter + 2 * s], *m));
        m->textFromValueFunction = [] (double v) { return "Mix " + juce::String (juce::roundToInt (v * 100)) + "%"; };
        m->updateText();
        addAndMakeVisible (m);
    }
    order = proc.fxRack.getOrder();
    startTimerHz (10);
}

void RackList::timerCallback()
{
    if (proc.fxRack.orderKey() != lastKey) { lastKey = proc.fxRack.orderKey(); order = proc.fxRack.getOrder(); layoutRows(); repaint(); }
}

void RackList::resized() { layoutRows(); }

void RackList::layoutRows()
{
    const int W = getWidth();
    for (int i = 0; i < FS_COUNT; ++i)
    {
        const int s = order[(size_t) i];
        const int y = kTop + i * kRowH;
        on[s]->setBounds (W - 330, y + 8, 60, 24);
        mix[s]->setBounds (W - 260, y + 8, 240, 24);
    }
}

int RackList::rowAt (int y) const { return juce::jlimit (0, FS_COUNT - 1, (y - kTop) / kRowH); }

void RackList::paint (juce::Graphics& g)
{
    g.setColour (col::muted);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Signal flows top to bottom. Drag a row to move it. Tape Delay, Juno Chorus and the Reverbs are send effects: next to each other they all hear the same signal (as they always have).",
                0, 0, getWidth(), 24, juce::Justification::centredLeft);
    for (int i = 0; i < FS_COUNT; ++i)
    {
        const int s = order[(size_t) i];
        auto r = juce::Rectangle<float> (0.0f, (float) (kTop + i * kRowH), (float) getWidth(), (float) kRowH - 4.0f);
        const bool dragging = dragFrom == i;
        g.setColour (dragging ? col::panel3 : col::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (dragOver == i && dragFrom >= 0 && dragOver != dragFrom ? col::fx : col::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, dragOver == i && dragFrom >= 0 ? 2.0f : 1.0f);
        g.setColour (col::muted);
        for (int k = 0; k < 3; ++k) g.fillRect (r.getX() + 12.0f, r.getCentreY() - 5.0f + k * 4.0f, 14.0f, 2.0f);   // grip
        g.setColour (fxSlotIsSend (s) ? col::text.withAlpha (0.85f) : col::text);
        g.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::bold)));
        g.drawText (juce::String (i + 1) + ".  " + kFxSlotNames[s], r.withTrimmedLeft (36.0f), juce::Justification::centredLeft);
        if (fxSlotIsSend (s))
        {
            g.setColour (col::muted);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("send", r.withTrimmedLeft (240.0f), juce::Justification::centredLeft);
        }
    }
}

void RackList::mouseDown (const juce::MouseEvent& e)
{
    if (e.x > getWidth() - 340) return;   // the On / Mix controls
    dragFrom = rowAt (e.y); dragOver = dragFrom; dragY = e.y;
    repaint();
}
void RackList::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    dragOver = rowAt (e.y);
    repaint();
}
void RackList::mouseUp (const juce::MouseEvent&)
{
    if (dragFrom >= 0 && dragOver >= 0 && dragOver != dragFrom)
    {
        proc.fxRack.move (dragFrom, dragOver);
        proc.pushHistory ("Move effect");
    }
    dragFrom = dragOver = -1;
    timerCallback();
    repaint();
}

} // namespace tgui
