#include "ArpPage.h"
#include "../PluginEditor.h"

using namespace tg;

namespace tgui
{

ArpPatternGrid::ArpPatternGrid (MegaSynthProcessor& p) : proc (p)
{
    setTooltip ("Click a step: On > Accent > Rest. Drag up or down to set how likely it is to play. Pattern Steps sets the length.");
    startTimerHz (30);
}

void ArpPatternGrid::timerCallback()
{
    if (! isShowing()) return;
    const int st = proc.arpStepShown.load();
    const int len = juce::roundToInt (proc.param (P_arpSteps)->convertFrom0to1 (proc.param (P_arpSteps)->getValue()));
    if (st != shownStep || proc.arpStore.getVersion() != shownVersion || len != shownLen)
    {
        shownStep = st; shownVersion = proc.arpStore.getVersion(); shownLen = len;
        repaint();
    }
}

int ArpPatternGrid::stepAt (float x) const
{
    return juce::jlimit (0, ArpStore::kSteps - 1, (int) (x / (getWidth() / (float) ArpStore::kSteps)));
}

void ArpPatternGrid::paint (juce::Graphics& g)
{
    const float w = getWidth() / (float) ArpStore::kSteps, h = (float) getHeight();
    const int len = juce::roundToInt (proc.param (P_arpSteps)->convertFrom0to1 (proc.param (P_arpSteps)->getValue()));
    const bool on = proc.param (P_arpOn)->getValue() > 0.5f;
    for (int i = 0; i < ArpStore::kSteps; ++i)
    {
        auto r = juce::Rectangle<float> (i * w + 2.0f, 0.0f, w - 4.0f, h).reduced (0.0f, 1.0f);
        const bool inLen = i < len;
        const int st = proc.arpStore.getState (i);
        const float pr = proc.arpStore.getProb (i);
        g.setColour (col::panel3.withAlpha (inLen ? 1.0f : 0.35f));
        g.fillRoundedRectangle (r, 5.0f);
        if (st != ArpStore::ST_Rest)
        {
            auto fill = r.withTop (r.getBottom() - r.getHeight() * (0.15f + 0.85f * pr));
            g.setColour ((st == ArpStore::ST_Accent ? col::fx : col::seq).withAlpha (inLen ? 0.9f : 0.3f));
            g.fillRoundedRectangle (fill, 5.0f);
        }
        if (on && i == shownStep)
        {
            g.setColour (col::text);
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 2.0f);
        }
        if (i % 4 == 0) { g.setColour (col::muted); g.setFont (juce::Font (juce::FontOptions (10.0f))); g.drawText (juce::String (i + 1), r.withHeight (14.0f), juce::Justification::centred); }
        g.setColour (col::text.withAlpha (inLen ? 0.85f : 0.3f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (st == ArpStore::ST_Rest ? "REST" : (st == ArpStore::ST_Accent ? "ACC" : (pr < 0.995f ? juce::String (juce::roundToInt (pr * 100)) + "%" : juce::String())),
                    r.withTop (r.getBottom() - 16.0f), juce::Justification::centred);
    }
}

void ArpPatternGrid::mouseDown (const juce::MouseEvent& e)
{
    dragStep = stepAt (e.position.x); dragStartY = e.position.y; dragStartProb = proc.arpStore.getProb (dragStep); dragged = false;
}

void ArpPatternGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0) return;
    if (std::abs (e.position.y - dragStartY) > 3.0f) dragged = true;
    if (! dragged) return;
    const float p = juce::jlimit (0.0f, 1.0f, dragStartProb + (dragStartY - e.position.y) / (float) getHeight());
    int st = proc.arpStore.getState (dragStep);
    if (st == ArpStore::ST_Rest) st = ArpStore::ST_On;
    proc.arpStore.setStep (dragStep, st, p);
    repaint();
}

void ArpPatternGrid::mouseUp (const juce::MouseEvent&)
{
    if (dragStep >= 0 && ! dragged)
    {
        const int st = proc.arpStore.getState (dragStep);
        const int next = st == ArpStore::ST_On ? ArpStore::ST_Accent : (st == ArpStore::ST_Accent ? ArpStore::ST_Rest : ArpStore::ST_On);
        proc.arpStore.setStep (dragStep, next, proc.arpStore.getProb (dragStep));
    }
    dragStep = -1;
    repaint();
}

//==============================================================================
void ChordDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 6.0f);
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int preset = juce::roundToInt (proc.param (P_chdPreset)->convertFrom0to1 (proc.param (P_chdPreset)->getValue()));
    const int spread = juce::roundToInt (proc.param (P_chdSpread)->convertFrom0to1 (proc.param (P_chdSpread)->getValue()));
    const int inv = juce::roundToInt (proc.param (P_chdInversion)->convertFrom0to1 (proc.param (P_chdInversion)->getValue()));
    const auto iv = chordIntervals (preset, proc.arpStore, spread, inv);
    juce::StringArray a;
    for (int x : iv) a.add (juce::String (names[((x % 12) + 12) % 12]) + juce::String (3 + (int) std::floor (x / 12.0)));
    g.setColour (proc.arpStore.learning.load() ? col::fx : col::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (proc.arpStore.learning.load() ? juce::String ("LEARNING: play a chord, then let go") : "On C3: " + a.joinIntoString ("  "),
                r.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
}

} // namespace tgui
