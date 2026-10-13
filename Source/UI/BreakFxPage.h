#pragma once
// v0.1.72: Breaks > Break FX. A tick per break and effect: which effects each break plays through. The synth always goes
// through everything; a break ticked for everything shares the synth's effects (as always); a break routed apart plays
// through its own copies of the effects it's ticked for, with the same settings (a "lane").
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "FxPages.h"

namespace tgui
{

class BreakFxGrid : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    explicit BreakFxGrid (MegaSynthProcessor& p) : proc (p)
    {
        using namespace tg;
        auto busOn = [this] (int which)
        {
            auto plain = [this] (int i) { return proc.param (i)->convertFrom0to1 (proc.param (i)->getValue()); };
            if (which == 0) return plain (P_grMix) > 1.0e-4f;
            if (which == 1) return plain (P_spOn) > 0.5f;
            float a = 0.0f; for (int i = P_fbGrGr; i <= P_fbOutDl; ++i) a += std::abs (plain (i));
            return a > 1.0e-4f;
        };
        units.push_back ({ "Granular", "Bus Mutation", BX_Gran, col::fx, [busOn] { return busOn (0); } });
        units.push_back ({ "Spectral", "Bus Mutation", BX_Spec, col::fx, [busOn] { return busOn (1); } });
        units.push_back ({ "Feedback", "Bus Mutation", BX_Fb, col::fx, [busOn] { return busOn (2); } });
        static const int order[] = { FS_Delay, FS_Dub, FS_Reverb, FS_Plate, FS_Gated, FS_Spring, FS_Ir, FS_Chorus, FS_Dim, FS_Ens, FS_Bbd, FS_Multi,
                                     FS_Flanger, FS_Script, FS_Colour, FS_Dual, FS_Barber, FS_Vibe, FS_Stereo, FS_Widen, FS_Sampler, FS_Multiband, FS_Stutter, FS_Shaper };
        static const char* shortName[FS_COUNT] = { "Beat Repeat", "Vintage Sampler", "Flanger / Phaser", "Tape Delay", "Juno Chorus", "90s Reverbs", "Volume Shaper",
                                                   "Multiband", "Stereo Tools", "Dub Delay", "Spring", "Gated", "Plate", "Widener", "Dimension", "IR Reverb",
                                                   "Ensemble", "BBD Chorus", "Multi-Chorus", "Script", "Colour", "Dual", "Barber-pole", "Vibe" };
        for (int s : order)
        {
            const int g = fxSlotGroup (s);
            units.push_back ({ shortName[s], kFxGroupNames[g], BX_Rack0 + s, fxGroupColour (g), [this, s] { return proc.param (fxOnParam (s))->getValue() > 0.5f && proc.param (fxMixParam (s))->getValue() > 0.0f; } });
        }
        units.push_back ({ "Performance FX", "Perf FX", BX_Perf, col::accent, [] { return true; } });
        units.push_back ({ "Master Filter", "Master", BX_MF, col::filter, [this] { return proc.param (P_mfOn)->getValue() > 0.5f; } });
        startTimerHz (8);
    }
    ~BreakFxGrid() override { stopTimer(); }

    // the four presets for all the breaks: 0 everything, 1 dry, 2 space only, 3 dirt only
    void applyPreset (int k)
    {
        using namespace tg;
        uint32_t m = kBxAll;
        if (k == 1) m = 0u;
        else if (k == 2 || k == 3)
        {
            m = (1u << BX_Perf) | (1u << BX_MF);
            for (int s = 0; s < FS_COUNT; ++s)
            {
                const int g = fxSlotGroup (s);
                if (k == 2 ? (g == FG_Delays || g == FG_Reverbs || g == FG_Width) : g == FG_Dirt) m |= 1u << (BX_Rack0 + s);
            }
        }
        for (int b = 0; b < 4; ++b) proc.fxRack.setBreakFx (b, m);
        proc.pushHistory ("Break FX preset");
        repaint();
    }
    static const char* presetName (int k)
    {
        static const char* n[4] = { "Everything", "Dry drums", "Drums: space only", "Drums: dirt only" };
        return n[juce::jlimit (0, 3, k)];
    }
    // what each break does now (and the lanes it needs), in words
    juce::String routingText() const
    {
        using namespace tg;
        const int route = juce::jlimit (0, 3, juce::roundToInt (proc.param (P_bcFxRoute)->convertFrom0to1 (proc.param (P_bcFxRoute)->getValue())));
        juce::String t;
        juce::Array<uint32_t> keys;
        for (int b = 0; b < 4; ++b)
        {
            const uint32_t e = proc.fxRack.breakFx (b) & bxRouteMask (route);
            t << "Break " << (b + 1) << ": ";
            if (e == kBxAll) t << "with the synth, through everything";
            else if (e == bxRouteMask (1)) t << "past the effects, into Performance FX (shared with the synth)";
            else if (e == bxRouteMask (2)) t << "past the effects, into the Master Filter (shared)";
            else if (e == 0u) t << "dry: after everything";
            else
            {
                int n = 0; for (int u = 0; u < BX_COUNT; ++u) n += (int) (e >> u & 1u);
                keys.addIfNotAlreadyThere (e);
                t << "its own lane " << (keys.indexOf (e) + 1) << " (" << n << " of " << (int) BX_COUNT << " stages)";
            }
            t << (b < 3 ? "\n" : "");
        }
        lanesNeeded = keys.size();
        return t;
    }
    int lanesNeededNow() const { routingText(); return lanesNeeded; }

    void paint (juce::Graphics& g) override
    {
        using namespace tg;
        const int route = juce::jlimit (0, 3, juce::roundToInt (proc.param (P_bcFxRoute)->convertFrom0to1 (proc.param (P_bcFxRoute)->getValue())));
        const uint32_t rmask = bxRouteMask (route);
        // headers: the groups (click: all / none for every break), then each stage's name, standing up
        juce::String lastGroup;
        int gx0 = 0;
        auto drawGroup = [&] (const juce::String& name, int x0, int x1, juce::Colour c)
        {
            auto r = juce::Rectangle<float> ((float) x0 + 1.0f, 0.0f, (float) (x1 - x0) - 2.0f, (float) kGroupH - 3.0f);
            g.setColour (c.withAlpha (0.22f)); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (c); g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            g.drawFittedText (name, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        };
        for (int u = 0; u <= (int) units.size(); ++u)
        {
            const juce::String grp = u < (int) units.size() ? juce::String (units[(size_t) u].group) : juce::String ("~");
            if (grp != lastGroup)
            {
                if (u > 0) drawGroup (lastGroup, gx0, colX (u), units[(size_t) u - 1].colour);
                lastGroup = grp; gx0 = colX (u);
            }
            if (u == (int) units.size()) break;
            const auto& un = units[(size_t) u];
            const bool on = un.isOn();
            g.saveState();
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) colX (u) + kColW * 0.5f, (float) (kGroupH + kNameH) - 4.0f));
            g.setColour (on ? col::text : col::muted);
            g.setFont (juce::Font (juce::FontOptions (11.0f, on ? juce::Font::bold : juce::Font::plain)));
            g.drawText (un.name, juce::Rectangle<float> ((float) colX (u) + kColW * 0.5f, (float) (kGroupH + kNameH) - 4.0f - kColW * 0.5f, (float) kNameH - 4.0f, (float) kColW), juce::Justification::centredLeft);
            g.restoreState();
        }
        // the rows
        for (int b = 0; b < 4; ++b)
        {
            const int y = rowY (b);
            const uint32_t m = proc.fxRack.breakFx (b);
            g.setColour (col::panel2); g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, (float) y, (float) getWidth(), (float) kRowH - 4.0f), 6.0f);
            g.setColour (col::text); g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText ("Break " + juce::String (b + 1), 8, y, 70, kRowH - 4, juce::Justification::centredLeft);
            for (auto [x, label] : { std::pair<int, const char*> { kAllX, "All" }, { kNoneX, "None" } })
            {
                auto r = juce::Rectangle<float> ((float) x, (float) y + 7.0f, 34.0f, (float) kRowH - 18.0f);
                g.setColour (col::panel3); g.fillRoundedRectangle (r, 4.0f);
                g.setColour (col::muted); g.setFont (juce::Font (juce::FontOptions (10.5f)));
                g.drawText (label, r, juce::Justification::centred);
            }
            for (int u = 0; u < (int) units.size(); ++u)
            {
                const auto& un = units[(size_t) u];
                const bool ticked = (m >> un.bit & 1u) != 0, blocked = (rmask >> un.bit & 1u) == 0, on = un.isOn();
                auto box = juce::Rectangle<float> ((float) colX (u) + (kColW - 18) * 0.5f, (float) y + (kRowH - 4 - 18) * 0.5f, 18.0f, 18.0f);
                const float a = on ? 1.0f : 0.45f;   // (an effect that's off now is greyed, still tickable)
                g.setColour ((ticked && ! blocked ? un.colour : col::panel3).withMultipliedAlpha (ticked && ! blocked ? a : 1.0f));
                g.fillRoundedRectangle (box, 3.0f);
                g.setColour (un.colour.withAlpha (0.6f * a + 0.2f)); g.drawRoundedRectangle (box, 3.0f, 1.0f);
                if (ticked)
                {
                    juce::Path tick; tick.startNewSubPath (box.getX() + 4.0f, box.getCentreY()); tick.lineTo (box.getX() + 8.0f, box.getBottom() - 4.0f); tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.0f);
                    g.setColour ((blocked ? col::muted : col::bg).withAlpha (blocked ? 0.8f : 1.0f)); g.strokePath (tick, juce::PathStrokeType (2.0f));
                }
                if (blocked) { g.setColour (col::complex.withAlpha (0.8f)); g.drawLine (box.getX() - 1.0f, box.getBottom() + 1.0f, box.getRight() + 1.0f, box.getY() - 1.0f, 1.5f); }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        using namespace tg;
        const int u = unitAt (e.x);
        if (e.y < kGroupH && u >= 0)
        {
            // a group: all its stages on for every break, or (when they all are) off
            const juce::String grp = units[(size_t) u].group;
            bool all = true;
            for (int b = 0; b < 4; ++b) for (auto& un : units) if (grp == un.group) all = all && (proc.fxRack.breakFx (b) >> un.bit & 1u) != 0;
            for (int b = 0; b < 4; ++b)
            {
                uint32_t m = proc.fxRack.breakFx (b);
                for (auto& un : units) if (grp == un.group) m = all ? (m & ~(1u << un.bit)) : (m | (1u << un.bit));
                proc.fxRack.setBreakFx (b, m);
            }
            proc.pushHistory ("Break FX: " + grp);
            repaint();
            return;
        }
        const int b = rowAt (e.y);
        if (b < 0) return;
        if (e.x >= kAllX && e.x < kAllX + 34) { proc.fxRack.setBreakFx (b, kBxAll); proc.pushHistory ("Break FX: all"); repaint(); return; }
        if (e.x >= kNoneX && e.x < kNoneX + 34) { proc.fxRack.setBreakFx (b, 0u); proc.pushHistory ("Break FX: none"); repaint(); return; }
        if (u < 0) return;
        proc.fxRack.setBreakFx (b, proc.fxRack.breakFx (b) ^ (1u << units[(size_t) u].bit));
        proc.pushHistory ("Break FX");
        repaint();
    }
    juce::String getTooltipFor (juce::Point<int> p) const
    {
        const int u = unitAt (p.x);
        if (u < 0) return {};
        const auto& un = units[(size_t) u];
        return juce::String (un.name) + (un.isOn() ? "" : " (off now: a tick takes effect once it's on)") + (p.y < kGroupH ? "  -  click: the whole group, on or off, for every break" : "");
    }
    juce::String getTooltip() override { return getTooltipFor (getMouseXYRelative()); }
    int numUnits() const { return (int) units.size(); }

    static constexpr int kGroupH = 22, kNameH = 96, kRowH = 40, kLabelW = 150, kAllX = 72, kNoneX = 110;
private:
    struct Unit { const char* name; const char* group; int bit; juce::Colour colour; std::function<bool()> isOn; };
    std::vector<Unit> units;
    MegaSynthProcessor& proc;
    mutable int lanesNeeded = 0;
    float colW() const { return (float) (getWidth() - kLabelW) / (float) juce::jmax (1, (int) units.size()); }
    int colX (int u) const { return kLabelW + juce::roundToInt (u * colW()); }
    static constexpr int kColW = 34;
    int unitAt (int x) const { if (x < kLabelW) return -1; const int u = (int) ((x - kLabelW) / colW()); return u >= 0 && u < (int) units.size() ? u : -1; }
    int rowY (int b) const { return kGroupH + kNameH + 4 + b * kRowH; }
    int rowAt (int y) const { const int b = (y - kGroupH - kNameH - 4) / kRowH; return y >= kGroupH + kNameH + 4 && b < 4 ? b : -1; }
    void timerCallback() override { if (isShowing()) repaint(); }
};

// (v0.1.72) the Keys switch under Oct - / +: what the on-screen keyboard and your MIDI keys play in the slice zone - the synth,
// the break's slices, or Auto (slices while the break sequencer isn't running; with it running, the synth). It is the Break
// Chopper's Slice Keys setting (the same parameter, so it saves, automates and can be MIDI-learned: right-click).
class KeysSwitch : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit KeysSwitch (MegaSynthProcessor& p) : proc (p) { tagParam (*this, tg::P_bcSliceKeys); startTimerHz (10); }
    ~KeysSwitch() override { stopTimer(); }
    // the parameter's order is On (breaks), Off (synth), Auto; the switch shows Synth, Breaks, Auto
    static int valueFor (int row) { return row == 0 ? 1 : row == 1 ? 0 : 2; }
    int current() const { return juce::roundToInt (proc.param (tg::P_bcSliceKeys)->convertFrom0to1 (proc.param (tg::P_bcSliceKeys)->getValue())); }
    static juce::Colour colourFor (int v) { return v == 0 ? col::complex : v == 1 ? col::osc : col::accent; }
    void paint (juce::Graphics& g) override
    {
        static const char* names[3] = { "Synth", "Breaks", "Auto" };
        const int cur = current();
        const float h = (float) getHeight() / 3.0f;
        for (int r = 0; r < 3; ++r)
        {
            auto rc = juce::Rectangle<float> (0.0f, r * h, (float) getWidth(), h).reduced (0.0f, 1.0f);
            const bool on = valueFor (r) == cur;
            const auto c = colourFor (valueFor (r));
            g.setColour (on ? c.withAlpha (0.85f) : col::panel2); g.fillRoundedRectangle (rc, 4.0f);
            g.setColour (on ? c : col::border); g.drawRoundedRectangle (rc, 4.0f, 1.0f);
            g.setColour (on ? col::bg : col::muted);
            g.setFont (juce::Font (juce::FontOptions (11.0f, on ? juce::Font::bold : juce::Font::plain)));
            g.drawText (juce::String ("Keys: ") + names[r], rc, juce::Justification::centred);
        }
        if (proc.param (tg::P_bsKeyEntry)->getValue() > 0.5f && cur != 0)
        {
            // Key Entry takes the slice zone while it's on, whatever the switch says
            auto rc = juce::Rectangle<float> (0.0f, h, (float) getWidth(), h).reduced (0.0f, 1.0f);
            g.setColour (colourFor (0)); g.drawRoundedRectangle (rc.reduced (1.0f), 4.0f, 2.0f);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText ("KEY ENTRY", rc.withTrimmedLeft (rc.getWidth() - 50.0f), juce::Justification::centredRight);
        }
        if (proc.keysPadKind.load() != 0)   // (a learned Keys pad: a dot)
        {
            g.setColour (col::text); g.fillEllipse ((float) getWidth() - 7.0f, 3.0f, 4.0f, 4.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            // (MIDI Learn of the parameter comes from the editor's own right-click menu; the Keys pad is here)
            return;
        }
        const int r = juce::jlimit (0, 2, (int) (e.y / (getHeight() / 3.0f)));
        set (valueFor (r));
    }
    void set (int v)
    {
        auto* prm = proc.param (tg::P_bcSliceKeys);
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v)); prm->endChangeGesture();
        repaint();
    }
private:
    void timerCallback() override
    {
        const int v = current();
        const bool ke = proc.param (tg::P_bsKeyEntry)->getValue() > 0.5f;
        if (v != shown || ke != keShown) { shown = v; keShown = ke; repaint(); }
        static const char* tips[3] = { "Breaks: the slice zone plays the break's slices",
                                       "Synth: every key plays the synth, even with the break sequencer running",
                                       "Auto: the slice zone plays slices while the break sequencer is stopped; while it runs, the synth" };
        juce::String t = juce::String ("What the keys play in the slice zone (the Break Chopper's Slice Keys). ") + tips[juce::jlimit (0, 2, v)]
                         + ". Right-click: MIDI Learn (a CC). A Keys pad that flips Synth / Breaks is learned on the Break Sequencer page"
                         + (proc.keysPadKind.load() != 0 ? " (now: " + proc.keysPadText() + ")" : juce::String()) + "."
                         + (keShown ? " Key Entry is on: the slice zone plays the break's slices until it's off." : "");
        if (t != getTooltip()) setTooltip (t);
    }
    MegaSynthProcessor& proc;
    int shown = -1;
    bool keShown = false;
};

} // namespace tgui
