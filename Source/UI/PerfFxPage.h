#pragma once
// Perform > Performance FX: the pads, two Kaoss-style XY pads and the pads' settings.
// Included from PluginEditor.cpp after PluginEditor.h (it uses the colours, Knob and Section).
#include <JuceHeader.h>
#include "../Registry.h"

namespace tgui
{

// One pad: hold it (or, with Latch on, click to switch it on and off). Its light shows when it's
// waiting for the beat grid (outline) and when it's playing (filled).
class PerfPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    PerfPad (MegaSynthProcessor& p, int padIndex, juce::Colour c) : proc (p), pad (padIndex), colour (c)
    {
        tagParam (*this, tg::pfPadParam (pad));   // right-click: MIDI Learn
        setTooltip (juce::String (tg::PerfFx::padName (pad)) + ": hold to play (Latch: click on / off). Right-click for MIDI Learn.");
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        auto* prm = proc.param (tg::pfPadParam (pad));
        prm->beginChangeGesture();
        if (latched()) prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
        else prm->setValueNotifyingHost (1.0f);
        holding = true;
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! holding) return;
        holding = false;
        auto* prm = proc.param (tg::pfPadParam (pad));
        if (! latched()) prm->setValueNotifyingHost (0.0f);
        prm->endChangeGesture();
        repaint();
    }
    void refresh()
    {
        const int s = proc.getEngine().perf().padShown[pad].load (std::memory_order_relaxed);
        const bool want = proc.param (tg::pfPadParam (pad))->getValue() > 0.5f;
        const int key = (s << 1) | (want ? 1 : 0);
        if (key != shown || s == 1) { shown = key; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const int s = shown >> 1;
        const bool want = (shown & 1) != 0;
        g.setColour (s == 2 ? colour : col::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        if (s == 1 || (want && s == 0))
        {
            const float blink = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.02f);
            g.setColour (col::supersaw.withAlpha (0.5f + 0.5f * blink));
            g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.5f);
        }
        else { g.setColour (s == 2 ? colour.brighter (0.4f) : col::border); g.drawRoundedRectangle (r, 8.0f, 1.2f); }
        const auto ink = s == 2 ? col::text : col::text;
        g.setColour (ink.withAlpha (0.7f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (juce::String (stageOf (pad)), r.reduced (7.0f, 5.0f), juce::Justification::topLeft);
        const int base = keyBase();
        if (base >= 0) g.drawText (juce::MidiMessage::getMidiNoteName (base + pad, true, true, 3), r.reduced (7.0f, 5.0f), juce::Justification::topRight);
        g.setColour (ink);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawFittedText (tg::PerfFx::padName (pad), getLocalBounds().reduced (8, 14), juce::Justification::centred, 2);
    }
    static int stageOf (int k) { return (k < tg::PerfFx::kTimePads || k == tg::PerfFx::Akai) ? 1 : k - tg::PerfFx::kTimePads + 2; }
private:
    bool latched() const { return proc.param (tg::P_pfLatch)->getValue() > 0.5f; }
    int keyBase() const
    {
        static const int b[] = { -1, 36, 48, 60, 72, 84, 96 };
        return b[juce::jlimit (0, 6, juce::roundToInt (proc.param (tg::P_pfKeys)->convertFrom0to1 (proc.param (tg::P_pfKeys)->getValue())))];
    }
    MegaSynthProcessor& proc;
    const int pad;
    juce::Colour colour;
    int shown = -1;
    bool holding = false;
};

// A Kaoss-style XY pad. X and Y are Mod Matrix sources (XY Pad n X / Y), so they can drive anything;
// Assign makes the routes. With Spring on it glides back to the centre when let go.
class PerfXY : public juce::Component, public juce::SettableTooltipClient
{
public:
    PerfXY (MegaSynthProcessor& p, int padIndex) : proc (p), k (padIndex)
    {
        setTooltip ("Drag to play. X and Y are Mod Matrix sources: use Assign X / Assign Y, or pick \"XY Pad "
                    + juce::String (k + 1) + "\" in any knob's Modulate menu.");
    }
    int px() const { return tg::P_pfX1 + 2 * k; }
    int py() const { return tg::P_pfY1 + 2 * k; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        dragging = true; springing = false;
        proc.param (px())->beginChangeGesture(); proc.param (py())->beginChangeGesture();
        setFrom (e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) setFrom (e.position); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        proc.param (px())->endChangeGesture(); proc.param (py())->endChangeGesture();
        if (proc.param (tg::P_pfSpring1 + k)->getValue() > 0.5f) springing = true;
    }
    // (timer) the spring, the dot and the axis names
    void refresh()
    {
        if (springing && ! dragging)
        {
            auto* a = proc.param (px()); auto* b = proc.param (py());
            float x = a->getValue(), y = b->getValue();
            x += (0.5f - x) * 0.3f; y += (0.5f - y) * 0.3f;
            if (std::abs (x - 0.5f) < 0.004f && std::abs (y - 0.5f) < 0.004f) { x = y = 0.5f; springing = false; }
            a->setValueNotifyingHost (x); b->setValueNotifyingHost (y);
        }
        const juce::String nx = axisNames (true), ny = axisNames (false);
        const float x = proc.param (px())->getValue(), y = proc.param (py())->getValue();
        if (std::abs (x - lx) > 1.0e-4f || std::abs (y - ly) > 1.0e-4f || nx != namesX || ny != namesY)
        {
            lx = x; ly = y; namesX = nx; namesY = ny; repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (col::panel2); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (col::border); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        auto in = r.reduced (10.0f);
        g.setColour (col::border.withAlpha (0.6f));
        g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
        g.drawVerticalLine ((int) in.getCentreX(), in.getY(), in.getBottom());
        const float dx = in.getX() + lx * in.getWidth(), dy = in.getBottom() - ly * in.getHeight();
        g.setColour (col::fx.withAlpha (0.25f)); g.fillEllipse (dx - 16, dy - 16, 32, 32);
        g.setColour (col::fx); g.fillEllipse (dx - 7, dy - 7, 14, 14);
        g.setColour (col::muted);
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText ("X: " + (namesX.isEmpty() ? juce::String ("not assigned") : namesX), in.withTrimmedTop (in.getHeight() - 14), juce::Justification::centredLeft, true);
        g.drawText ("Y: " + (namesY.isEmpty() ? juce::String ("not assigned") : namesY), in.withHeight (14), juce::Justification::centredLeft, true);
    }
    // the Assign menu for one axis
    void showAssign (bool xAxis, juce::Component* target)
    {
        const int src = tg::MS_PfX1 + 2 * k + (xAxis ? 0 : 1);
        juce::PopupMenu menu, perf, any;
        std::map<juce::String, juce::PopupMenu> byModule;
        for (int d : tg::modulationDestinations())
        {
            const auto& m = tg::meta (d);
            if (m.module == "Performance FX") perf.addItem (10000 + d, m.name.fromFirstOccurrenceOf ("Perf ", false, false));
            else byModule[m.module].addItem (10000 + d, m.name);
        }
        menu.addSectionHeader (juce::String ("XY Pad ") + juce::String (k + 1) + (xAxis ? " X" : " Y") + " drives...");
        menu.addSubMenu ("Performance FX", perf);
        for (auto& [mod, sub] : byModule) any.addSubMenu (mod, sub);
        menu.addSubMenu ("Any Mod Matrix destination", any);
        menu.addSeparator();
        menu.addItem (1, "Clear this axis");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                            [this, src] (int r)
                            {
                                if (r == 1)
                                {
                                    for (int i = 0; i < tg::kNumRoutes; ++i) if (proc.routes.get (i).src == src) proc.clearRoute (i);
                                }
                                else if (r >= 10000) proc.addRoute (src, r - 10000, 0.5f);
                                refresh(); repaint();
                            });
    }
private:
    juce::String axisNames (bool xAxis) const
    {
        const int src = tg::MS_PfX1 + 2 * k + (xAxis ? 0 : 1);
        juce::StringArray n;
        for (int i = 0; i < tg::kNumRoutes; ++i)
        {
            const auto c = proc.routes.get (i);
            if (c.src == src && c.dst >= 0) n.add (tg::meta (c.dst).name.replace ("Perf ", ""));
        }
        return n.joinIntoString (", ");
    }
    void setFrom (juce::Point<float> p)
    {
        auto in = getLocalBounds().toFloat().reduced (11.0f);
        proc.param (px())->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (p.x - in.getX()) / in.getWidth()));
        proc.param (py())->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (in.getBottom() - p.y) / in.getHeight()));
        refresh();
    }
    MegaSynthProcessor& proc;
    const int k;
    bool dragging = false, springing = false;
    float lx = 0.5f, ly = 0.5f;
    juce::String namesX, namesY;
};

// Keeps the pads' lights and the XY pads moving while the page is showing
class PerfTicker : public juce::Component, private juce::Timer
{
public:
    PerfTicker() { startTimerHz (30); }
    ~PerfTicker() override { stopTimer(); }
    juce::Array<PerfPad*> pads;
    juce::Array<PerfXY*> xys;
    void tick() { for (auto* p : pads) p->refresh(); for (auto* x : xys) x->refresh(); }
private:
    void timerCallback() override { tick(); }
};

} // namespace tgui
